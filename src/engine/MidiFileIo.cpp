#include "engine/MidiFileIo.h"

#include "core/NoteChance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>

#include "core/ExportOptions.h"
#include "core/KeyRegions.h"
#include "core/TempoMap.h"
#include "core/MidiControls.h"
#include "engine/Text.h"

namespace lynx::MidiFileIo
{
namespace
{
struct Parsed
{
    std::vector<std::vector<Note>> tracks; // notes per track (channel = track index)
    std::vector<std::string> names;
    std::vector<std::vector<int>> midiChannels; // per track, per note: its MIDI channel 0..15
    double bpm = 120.0;
    int numerator = 0, denominator = 4; // the first time signature (0 = none)
    std::vector<PatternEvent> tempos; // every Set Tempo (issue #86)
    std::vector<midiimport::ParsedMidi::KeyChange> keys; // round 190nu
};

// Round 190nu (core/KeyRegions.h): the file's key signatures in time order; LYNX's "Key: <name>" marker meta event at the
// same tick gives the exact scale (a signature holds only major / minor and the accidentals)
template <typename Out>
void collectKeys (const juce::MidiFile& midi, Out& out, const std::function<std::int64_t (double)>& toTicks)
{
    std::vector<std::pair<std::int64_t, projectkey::Key>> signatures, texts;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto& track = *midi.getTrack (t);
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (m.isKeySignatureMetaEvent())
                signatures.emplace_back (toTicks (m.getTimeStamp()),
                                         keyregions::keyFromMidiSignature (m.getKeySignatureNumberOfSharpsOrFlats(), m.isKeySignatureMajorKey() ? 0 : 1));
            else if (m.isMetaEvent() && m.getMetaEventType() == 6)
                if (const auto key = keyregions::keyFromMidiText (m.getTextFromTextMetaEvent().toStdString()))
                    texts.emplace_back (toTicks (m.getTimeStamp()), *key);
        }
    }
    for (const auto& [tick, key] : texts) // (the exact scale wins over the signature at its tick)
    {
        signatures.erase (std::remove_if (signatures.begin(), signatures.end(), [tick] (const auto& s) { return s.first == tick; }), signatures.end());
        signatures.emplace_back (tick, key);
    }
    std::stable_sort (signatures.begin(), signatures.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [tick, key] : signatures)
        out.keys.push_back ({ tick, key.root, key.flScale });
}

// The key signatures (and LYNX's exact-scale markers) of `pattern`'s key regions on the tempo track; `base` = the project
// key (what applies before the first key marker)
void addKeySignatures (juce::MidiMessageSequence& tempo, const Pattern& pattern, std::optional<projectkey::Key> base, std::int64_t end)
{
    for (const auto& [tick, key] : keyregions::keyChanges (pattern, base, end))
    {
        const auto [sharps, minor] = keyregions::midiSignature (key);
        tempo.addEvent (juce::MidiMessage::keySignatureMetaEvent (sharps, minor != 0), (double) tick);
        tempo.addEvent (juce::MidiMessage::textMetaEvent (6, juce::String::fromUTF8 (keyregions::midiKeyText (key).c_str())), (double) tick);
    }
}

// The file's Set Tempo events in time order (FL's whole BPM) and the first one as the song's tempo (issue #86: it was
// the first one found while the tempo was still 120, so a file starting at exactly 120 took its second tempo)
template <typename Out>
void collectTempos (const juce::MidiFile& midi, Out& out, const std::function<std::int64_t (double)>& toTicks)
{
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto& track = *midi.getTrack (t);
        for (int i = 0; i < track.getNumEvents(); ++i)
            if (const auto& m = track.getEventPointer (i)->message; m.isTempoMetaEvent() && m.getTempoSecondsPerQuarterNote() > 0.0)
                out.tempos.push_back ({ toTicks (m.getTimeStamp()), valueFromTempo (midiimport::midiTempoBpm (m.getTempoSecondsPerQuarterNote())) });
    }
    std::stable_sort (out.tempos.begin(), out.tempos.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
}

// (round 190rm, sweep 190rj BUGS 9) a Standard MIDI File with only its header and track chunks: the SMF spec has readers
// skip chunk types they don't know, but JUCE's reader counts every chunk against the header's track count, so an
// unknown chunk (e.g. an "XFIH" info chunk before the tracks) made it miss the last track or refuse the file. The data
// is returned as it is when it holds no such chunk (or is no SMF: JUCE then says so as before).
juce::MemoryBlock smfTrackChunksOnly (juce::MemoryBlock data)
{
    const auto* d = static_cast<const std::uint8_t*> (data.getData());
    const auto size = data.getSize();
    const auto be32 = [d] (std::size_t at) { return ((std::uint32_t) d[at] << 24) | ((std::uint32_t) d[at + 1] << 16) | ((std::uint32_t) d[at + 2] << 8) | d[at + 3]; };
    const auto is = [d, size] (std::size_t at, const char* tag) { return at + 4 <= size && std::memcmp (d + at, tag, 4) == 0; };
    std::size_t head = 0;
    if (is (0, "RIFF")) // (RMID: JUCE looks for MThd within the first words)
        for (std::size_t at = 4; at + 4 <= size && at <= 36; at += 4)
            if (is (at, "MThd"))
            {
                head = at;
                break;
            }
    if (! is (head, "MThd") || head + 8 > size)
        return data;
    const auto headerEnd = head + 8 + (std::size_t) be32 (head + 4);
    if (headerEnd > size)
        return data;
    juce::MemoryBlock out (d + head, headerEnd - head);
    auto skipped = false;
    for (auto at = headerEnd; at + 8 <= size;)
    {
        const auto end = std::min (size, at + 8 + (std::size_t) be32 (at + 4));
        if (is (at, "MTrk"))
            out.append (d + at, end - at);
        else
            skipped = true;
        at = end;
    }
    return skipped ? out : data;
}

std::optional<Parsed> parse (const juce::File& file, juce::String& error)
{
    juce::MemoryBlock bytes;
    const auto read = file.loadFileAsData (bytes);
    juce::MemoryInputStream in (smfTrackChunksOnly (std::move (bytes)), true);
    juce::MidiFile midi;

    if (! read || ! midi.readFrom (in))
    {
        error = utf8 ("not a valid MIDI file");
        return std::nullopt;
    }

    const auto format = midi.getTimeFormat();

    if (format <= 0)
    {
        // SMPTE: convert to seconds, then to ticks at the default tempo
        midi.convertTimestampTicksToSeconds();
    }

    Parsed out;
    const auto filePpq = format > 0 ? (double) format : 0.0;
    auto toTicks = [&] (double t) {
        return filePpq > 0.0 ? (std::int64_t) std::llround (t * ppq / filePpq) : (std::int64_t) std::llround (t * out.bpm / 60.0 * ppq);
    };

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        auto track = *midi.getTrack (t);
        track.updateMatchedPairs();
        std::vector<Note> notes;
        std::vector<int> channels;
        std::string name;

        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto* e = track.getEventPointer (i);
            const auto& m = e->message;

            if (m.isTempoMetaEvent())
                continue; // (collectTempos)
            if (m.isTimeSignatureMetaEvent() && out.numerator == 0)
            {
                int num = 0, den = 0;
                m.getTimeSignatureInfo (num, den);
                if (num > 0 && den > 0)
                {
                    out.numerator = num;
                    out.denominator = den;
                }
            }
            else if (m.isTrackNameEvent() && name.empty())
                name = m.getTextFromTextMetaEvent().toStdString();
            else if (m.isNoteOn())
            {
                Note n;
                n.start = toTicks (m.getTimeStamp());
                const auto end = e->noteOffObject != nullptr ? toTicks (e->noteOffObject->message.getTimeStamp()) : n.start + ticksPerStep;
                n.length = std::max<std::int64_t> (1, end - n.start);
                n.key = m.getNoteNumber();
                n.velocity = m.getFloatVelocity();
                n.colour = juce::jlimit (0, noteColourCount - 1, m.getChannel() - 1); // FL: MIDI channel = note colour
                notes.push_back (n);
                channels.push_back (juce::jlimit (0, 15, m.getChannel() - 1));
            }
        }

        if (! notes.empty())
        {
            out.tracks.push_back (std::move (notes));
            out.names.push_back (name);
            out.midiChannels.push_back (std::move (channels));
        }
    }

    collectTempos (midi, out, toTicks);
    collectKeys (midi, out, toTicks);
    if (! out.tempos.empty())
        out.bpm = std::nearbyint (tempoFromValue (out.tempos.front().value)); // (FL's whole BPM, exact)

    if (out.tracks.empty())
    {
        error = utf8 ("the file has no notes");
        return std::nullopt;
    }

    return out;
}
} // namespace

std::optional<Song> loadSong (const juce::File& file, juce::String& error)
{
    auto parsed = parse (file, error);

    if (! parsed)
        return std::nullopt;

    Song song;
    song.bpm = parsed->bpm;
    // (round 190rm, sweep 190rj BUGS 9) the file's first time signature, as File › Import › MIDI file's "Import time
    // signatures" (on by default, core/MidiImport.cpp) does for a new project: a 3/4 file opened as 4/4 before
    if (parsed->numerator > 0 && meterUnit (parsed->denominator) == parsed->denominator)
    {
        song.beatsPerBar = std::clamp (parsed->numerator, 1, 16);
        song.beatUnit = parsed->denominator;
    }
    song.mixer = Song::defaultMixer();
    auto& pattern = song.patterns.emplace_back();
    pattern.name = file.getFileNameWithoutExtension().toStdString();

    for (std::size_t t = 0; t < parsed->tracks.size(); ++t)
    {
        ChannelDef c;
        c.name = parsed->names[t].empty() ? "MIDI " + std::to_string (t + 1) : parsed->names[t];
        c.instrument = InstrumentKind::synth;
        c.mixerTrack = song.claimFreeInsert();
        c.volume = 0.6f;
        song.channels.push_back (c);

        for (auto n : parsed->tracks[t])
        {
            n.channel = (int) t;
            pattern.notes.push_back (n);
        }
    }

    midiimport::setTempoEvents (pattern, parsed->tempos, 0); // every Set Tempo as the pattern's Tempo events (issue #86)
    // round 190nu: the key at the start is the project key, the later ones the pattern's key markers
    for (const auto& k : parsed->keys)
        if (projectkey::validFlScale (k.flScale))
        {
            if (k.tick <= 0)
                projectkey::set (song, projectkey::Key { k.root, k.flScale });
            else
                keyregions::addKeyMarker (pattern, k.tick, { keyregions::State::key, { k.root, k.flScale } });
        }
    pattern.sortNotes();
    return song;
}

static std::optional<midiimport::ParsedMidi> parseStream (juce::InputStream& source, bool opened, juce::String& error)
{
    juce::MemoryBlock bytes;
    if (opened)
        source.readIntoMemoryBlock (bytes, 200 * 1024 * 1024);
    juce::MemoryInputStream in (smfTrackChunksOnly (std::move (bytes)), true);
    juce::MidiFile midi;
    if (! opened || ! midi.readFrom (in))
    {
        error = utf8 ("not a valid MIDI file");
        return std::nullopt;
    }
    const auto format = midi.getTimeFormat();
    if (format <= 0)
        midi.convertTimestampTicksToSeconds();
    midiimport::ParsedMidi out;
    const auto filePpq = format > 0 ? (double) format : 0.0;
    auto toTicks = [&] (double t) {
        return filePpq > 0.0 ? (std::int64_t) std::llround (t * ppq / filePpq) : (std::int64_t) std::llround (t * out.bpm / 60.0 * ppq);
    };
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto& track = *midi.getTrack (t);
        std::string name;
        for (int i = 0; i < track.getNumEvents(); ++i)
        {
            const auto& m = track.getEventPointer (i)->message;
            if (m.isTempoMetaEvent())
                continue; // (collectTempos)
            if (m.isTimeSignatureMetaEvent() && out.numerator == 0)
            {
                int num = 0, den = 0;
                m.getTimeSignatureInfo (num, den);
                if (num > 0 && den > 0)
                {
                    out.numerator = num;
                    out.denominator = den;
                }
            }
            else if (m.isTrackNameEvent() && name.empty())
                name = m.getTextFromTextMetaEvent().toStdString();
            else if (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isProgramChange()) // (bend / program: MIDI Out import)
            {
                const auto* raw = m.getRawData();
                // a running-status note off may come as a note on with velocity 0: kept as it is (the import decides)
                out.events.push_back ({ t, toTicks (m.getTimeStamp()), raw[0], raw[1], m.getRawDataSize() > 2 ? raw[2] : (std::uint8_t) 0 });
            }
        }
        out.trackNames.push_back (name);
    }
    collectTempos (midi, out, toTicks);
    collectKeys (midi, out, toTicks);
    if (! out.tempos.empty())
        out.bpm = std::nearbyint (tempoFromValue (out.tempos.front().value)); // (FL's whole BPM, exact)
    return out;
}

std::optional<midiimport::ParsedMidi> parseFile (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);
    return parseStream (in, in.openedOk(), error);
}

std::optional<midiimport::ParsedMidi> parseBytes (const void* data, std::size_t size, juce::String& error)
{
    juce::MemoryInputStream in (data, size, false);
    return parseStream (in, size > 0, error);
}

juce::MemoryBlock writeNotes (const Song& song, const std::vector<Note>& notes, const std::string& trackName)
{
    juce::MidiMessageSequence seq;
    // notes with Chance or a Velocity Range: as they play (round 187c, core/NoteChance.h)
    for (const auto& n : chance::realised (notes, song.chanceSeed))
    {
        if (n.muted)
            continue;
        const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
        const auto midiChannel = juce::jlimit (1, 16, n.colour + 1); // FL: the note colour is the MIDI channel
        seq.addEvent (juce::MidiMessage::noteOn (midiChannel, n.key, velocity), (double) n.start);
        seq.addEvent (juce::MidiMessage::noteOff (midiChannel, n.key), (double) (n.start + std::max<std::int64_t> (1, n.length)));
    }
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / juce::jmax (1.0, song.bpm))), 0.0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::jmax (1, song.beatsPerBar), meterUnit (song.beatUnit)), 0.0);
    midi.addTrack (tempo);
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (trackName.c_str())), 0.0);
    seq.sort();
    seq.updateMatchedPairs();
    midi.addTrack (seq);
    juce::MemoryOutputStream out;
    midi.writeTo (out);
    return out.getMemoryBlock();
}

bool importInto (Song& song, const juce::File& file, const ImportOptions& o, juce::String& error)
{
    auto parsed = parseFile (file, error);
    if (! parsed)
        return false;
    std::string err;
    if (! midiimport::importInto (song, *parsed, o, file.getFileNameWithoutExtension().toStdString(), err))
    {
        error = utf8 (err.c_str());
        return false;
    }
    return true;
}

std::optional<std::vector<Note>> loadNotes (const juce::File& file, int channel, juce::String& error)
{
    auto parsed = parse (file, error);

    if (! parsed)
        return std::nullopt;

    std::vector<Note> notes;

    for (const auto& track : parsed->tracks)
        for (auto n : track)
        {
            n.channel = channel;
            notes.push_back (n);
        }

    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

namespace
{
// (round 190rm) the song's tempo changes and time signature changes, for FL's File › Export › MIDI file: before, only the
// tempo and signature at the start were written, so a song with a tempo automation (Thief slows to 60 BPM at its end)
// or a signature marker played at one tempo / meter in every other app. Tempo ramps are written as steps every 1/32 note.
struct SongTimeMap
{
    std::vector<std::pair<std::int64_t, double>> tempos;  // tick, BPM (after the start)
    std::vector<std::pair<std::int64_t, std::pair<int, int>>> signatures; // tick, numerator / denominator
};

bool writeMidi (const Song& song, int patternIndex, const juce::File& file, juce::String& error, const SongTimeMap* map);
} // namespace

bool savePattern (const Song& song, int patternIndex, const juce::File& file, juce::String& error)
{
    return writeMidi (song, patternIndex, file, error, nullptr);
}

namespace
{
bool writeMidi (const Song& song, int patternIndex, const juce::File& file, juce::String& error, const SongTimeMap* map)
{
    if (patternIndex < 0 || patternIndex >= (int) song.patterns.size())
    {
        error = "pattern does not exist";
        return false;
    }

    const auto& pattern = song.patterns[(std::size_t) patternIndex];
    std::map<int, juce::MidiMessageSequence> byChannel;

    for (const auto& n : chance::realised (pattern.notes, song.chanceSeed))
    {
        auto& seq = byChannel[n.channel];
        if (seq.getNumEvents() == 0) // (round 190rm: the track's name first, before its notes at tick 0, as SMF readers expect)
        {
            const auto name = n.channel >= 0 && n.channel < (int) song.channels.size() ? song.channels[(std::size_t) n.channel].name : std::string ("Channel");
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String::fromUTF8 (name.c_str())), 0.0);
        }
        const auto velocity = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (n.velocity * 127.0f));
        const auto midiChannel = juce::jlimit (1, 16, n.colour + 1); // FL: the note colour is the MIDI channel
        seq.addEvent (juce::MidiMessage::noteOn (midiChannel, n.key, velocity), (double) n.start);
        seq.addEvent (juce::MidiMessage::noteOff (midiChannel, n.key), (double) (n.start + std::max<std::int64_t> (1, n.length)));
    }

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);

    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / song.bpm)), 0.0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (song.beatsPerBar, meterUnit (song.beatUnit)), 0.0);
    if (map != nullptr)
    {
        for (const auto& [tick, bpm] : map->tempos)
            tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60'000'000.0 / bpm)), (double) tick);
        for (const auto& [tick, sig] : map->signatures)
            tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (sig.first, meterUnit (sig.second)), (double) tick);
    }
    addKeySignatures (tempo, pattern, projectkey::get (song), pattern.effectiveLength (song.meter())); // (round 190nu)
    midi.addTrack (tempo);

    for (auto& [channel, seq] : byChannel)
    {
        seq.sort();
        seq.updateMatchedPairs();
        midi.addTrack (seq);
    }

    file.deleteFile();
    juce::FileOutputStream out (file);

    if (out.failedToOpen() || ! midi.writeTo (out))
    {
        error = utf8 ("could not write ") + file.getFullPathName();
        return false;
    }

    return true;
}
} // namespace

bool saveSong (const Song& song, const juce::File& file, juce::String& error)
{
    // the arrangement flattened into one pattern, written like a pattern (one track per channel)
    Song flat;
    flat.bpm = song.bpm;
    flat.beatsPerBar = song.beatsPerBar;
    flat.beatUnit = song.beatUnit;
    flat.channels = song.channels;
    flat.patterns.resize (1);
    flat.patterns[0].notes = exportopts::midiExportNotes (song);
    flat.scaleHighlight = song.scaleHighlight; // round 190nu: the project key, and the key markers where they play
    flat.patterns[0].markers = keyregions::songKeyMarkers (song);
    SongTimeMap map;
    if (song.mode == PlayMode::song)
    {
        std::int64_t end = 0;
        for (const auto& n : flat.patterns[0].notes)
            end = std::max (end, n.start + n.length);
        for (const auto& c : song.playlist)
            end = std::max (end, c.end());
        if (tempomap::active (song))
        {
            auto last = song.bpm;
            for (std::int64_t t = 0; t <= end; t += ppq / 8)
                if (const auto bpm = tempomap::tempoAt (song, (double) t); std::abs (bpm - last) >= 0.005)
                {
                    if (t == 0)
                        flat.bpm = bpm; // (the start's tempo event)
                    else
                        map.tempos.push_back ({ t, bpm });
                    last = bpm;
                }
        }
        for (const auto& m : song.markers())
            if (m.isSignature() && m.tick > 0 && m.numerator > 0 && m.denominator > 0)
                map.signatures.push_back ({ m.tick, { m.numerator, m.denominator } });
    }
    return writeMidi (flat, 0, file, error, &map);
}
} // namespace lynx::MidiFileIo
