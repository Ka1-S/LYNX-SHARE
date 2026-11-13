#include "core/SimpleTracks.h"
#include "core/SimplePads.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "core/AutoWarp.h"
#include "core/EffectPresets.h"
#include "core/Effects.h"
#include "core/PlaylistOps.h"
#include "core/PlaylistTrackOps.h"
#include "core/ProjectKey.h"

namespace lynx::simpletracks
{
namespace
{
constexpr int playlistTrackCount = 500; // FL's Playlist tracks (trackmodes use the same bound)

PlaylistTrack& trackAt (Song& song, int track)
{
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    return song.playlistTracks[(std::size_t) track];
}

Result failed (Error error)
{
    Result r;
    r.error = error;
    return r;
}

bool validInsert (const Song& song, int insert) noexcept { return insert > 0 && insert < (int) song.mixer.size(); }
bool validChannel (const Song& song, int channel) noexcept { return channel >= 0 && channel < (int) song.channels.size(); }

// The name a new track gets: the asked one or the kind's, unique among the tracks ("Voice", "Voice 2"...)
std::string newTrackName (const Song& song, const Request& request)
{
    const auto base = request.name.empty() ? std::string (defaultName (request.kind)) : request.name;
    return pltracks::uniqueTrackName (song, base, -1);
}

void nameInsert (Song& song, int insert, const std::string& name, std::uint32_t colour)
{
    if (! validInsert (song, insert))
        return;
    auto& m = song.mixer[(std::size_t) insert];
    m.name = name;
    m.colour = colour;
}

Result addAudio (Song& song, int track, const Request& request)
{
    Result r;
    const auto name = newTrackName (song, request);
    const auto colour = defaultColour (request.kind);
    auto& t = trackAt (song, track);
    t.name = name;
    t.colour = colour;
    if (! trackmodes::makeAudioTrack (song, track))
        return failed (Error::mixerFull);
    const auto insert = song.playlistTracks[(std::size_t) track].mixerTrack;
    nameInsert (song, insert, name, colour);
    auto& m = song.mixer[(std::size_t) insert];
    if (request.kind == Kind::audio)
        m.inputChannel = -1;
    else
    {
        m.inputChannel = std::max (-1, request.input);
        m.inputStereo = request.inputStereo;
        // a voice / guitar / bass track records its input alone (FL's answer "Yes" to "switch to External input
        // only", issue #152): with FL's default Post EQ the take also held whatever else played through the insert
        if (m.inputChannel >= 0)
            m.recordSource = RecordSource::externalInput;
    }
    r.track = track;
    r.insert = insert;
    if (request.kind == Kind::audio && ! request.audioFile.empty() && request.audioLength > 0)
    {
        // the file on the track from the song's start, its channel playing through the track's insert
        ChannelDef c;
        const auto slash = request.audioFile.find_last_of ("/\\");
        auto stem = request.audioFile.substr (slash == std::string::npos ? 0 : slash + 1);
        if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos && dot > 0)
            stem.resize (dot);
        c.name = stem;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = request.audioFile;
        c.mixerTrack = insert;
        if (request.audioBeats > 0.0f)
        {
            // a loop: it lasts that many beats at any tempo, at its own pitch (round 190p)
            c.sampler.stretchBeats = request.audioBeats;
            c.sampler.keepPitch = true;
        }
        if (request.audioKeyRoot >= 0) // the file's key (round 190ds)
        {
            c.sampler.loopKeyRoot = request.audioKeyRoot % 12;
            c.sampler.loopKeyMinor = request.audioKeyMinor;
            c.sampler.loopKeySource = (std::uint8_t) std::clamp (request.audioKeySource, 0, 3);
        }
        song.channels.push_back (std::move (c));
        r.channels = { (int) song.channels.size() - 1 };
        Clip clip;
        clip.followKey = request.followKey;
        clip.kind = ClipKind::audio;
        clip.channel = r.channels[0];
        clip.track = track;
        clip.start = std::max<std::int64_t> (0, request.start);
        clip.length = request.audioBeats > 0.0f ? std::max<std::int64_t> (1, std::llround ((double) request.audioBeats * ppq)) : request.audioLength;
        song.playlist.push_back (clip);
        projectkey::retuneClip (song.playlist.back(), song, projectkey::get (song)); // (moved to the project key)
        song.mode = PlayMode::song;
    }
    return r;
}

// The notes of `request.pattern` on `channel`, placed at the song's start on `track` (an Instrument made from a MIDI track)
void placeInstrumentNotes (Song& song, int track, int channel, const std::string& name, std::uint32_t colour, Result& r, const Request& request)
{
    if (! request.pattern && request.emptyBars <= 0)
        return;
    Pattern pattern;
    if (request.pattern)
        pattern = *request.pattern;
    else
        pattern.lengthTicks = (std::int64_t) request.emptyBars * std::max (1, song.beatsPerBar) * ppq;
    pattern.name = name;
    pattern.colour = colour;
    for (auto& n : pattern.notes)
        n.channel = channel;
    pattern.sortNotes();
    song.patterns.push_back (std::move (pattern));
    r.pattern = (int) song.patterns.size() - 1;
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = r.pattern;
    clip.track = track;
    clip.start = std::max<std::int64_t> (0, request.start);
    clip.length = song.patterns[(std::size_t) r.pattern].effectiveLength (song.beatsPerBar);
    song.playlist.push_back (clip);
    song.mode = PlayMode::song;
}

Result addInstrument (Song& song, int track, const Request& request)
{
    if (! request.channel)
        return failed (Error::noChannel);
    const auto instrument = request.channel->instrument;
    if (instrument == InstrumentKind::audioClip || instrument == InstrumentKind::controller)
        return failed (Error::notAnInstrument);
    if (request.kind == Kind::sampler && instrument != InstrumentKind::sampler)
        return failed (Error::noChannel);

    const auto name = newTrackName (song, request);
    const auto colour = defaultColour (request.kind);
    auto channel = *request.channel;
    channel.name = name;
    channel.colour = colour;
    channel.mixerTrack = 0; // a free insert of its own (makeInstrumentTrack)
    channel.group = -1;
    song.channels.push_back (std::move (channel));
    const auto index = (int) song.channels.size() - 1;

    auto& t = trackAt (song, track);
    t.name = name;
    t.colour = colour;
    if (! trackmodes::makeInstrumentTrack (song, track, index))
        return failed (Error::mixerFull);
    Result r;
    r.track = track;
    r.insert = song.channels[(std::size_t) index].mixerTrack;
    r.channels = { index };
    nameInsert (song, r.insert, name, colour);
    placeInstrumentNotes (song, track, index, name, colour, r, request);
    return r;
}

Result addDrums (Song& song, int track, const Request& request)
{
    if (request.kit.empty())
        return failed (Error::noChannel);
    const auto insert = trackmodes::freeMixerTrack (song);
    if (insert <= 0)
        return failed (Error::mixerFull);

    Result r;
    const auto name = newTrackName (song, request);
    const auto colour = defaultColour (request.kind);
    song.channelGroups.push_back (name);
    r.group = (int) song.channelGroups.size() - 1;

    const auto first = (int) song.channels.size();
    for (auto channel : request.kit)
    {
        channel.mixerTrack = insert;
        channel.group = r.group;
        channel.colour = colour;
        if (request.kind == Kind::pads)
            simplepads::apply (channel, 0); // (its choke group becomes this insert's own)
        song.channels.push_back (std::move (channel));
        r.channels.push_back ((int) song.channels.size() - 1);
    }

    auto pattern = request.pattern.value_or (Pattern {});
    pattern.name = name;
    pattern.colour = colour;
    // the starter's notes name kit rows: they become the kit's channels; anything else is dropped
    std::vector<Note> notes;
    for (auto n : pattern.notes)
        if (n.channel >= 0 && n.channel < (int) request.kit.size())
        {
            n.channel = first + n.channel;
            notes.push_back (n);
        }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    pattern.notes = std::move (notes);
    song.patterns.push_back (std::move (pattern));
    r.pattern = (int) song.patterns.size() - 1;

    // FL's audio track linked to the drums' insert: the track keeps its insert while its pattern is empty
    auto& t = trackAt (song, track);
    t.name = name;
    t.colour = colour;
    t.mode = PlaylistTrackMode::audio;
    t.mixerTrack = insert;
    t.channel = -1;
    nameInsert (song, insert, name, colour);

    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = r.pattern;
    clip.track = track;
    clip.start = 0;
    clip.length = song.patterns[(std::size_t) r.pattern].effectiveLength (song.beatsPerBar);
    song.playlist.push_back (clip);
    // the same pattern again and again until `fillBars` bars are covered (whole clips: a 3-bar pattern covers 6)
    const auto wanted = (std::int64_t) std::clamp (request.fillBars, 0, 64) * song.beatsPerBar * ppq;
    for (auto at = clip.length; clip.length > 0 && at < wanted; at += clip.length)
    {
        clip.start = at;
        song.playlist.push_back (clip);
    }
    song.mode = PlayMode::song; // (the Simple workflow plays the song)
    if (request.bpm > 0.0)
        song.bpm = std::clamp (request.bpm, 10.0, 999.0);

    r.track = track;
    r.insert = insert;
    return r;
}

// The channels whose sound plays through `insert` and that make notes (not audio clip or controller channels)
bool insertHasInstruments (const Song& song, int insert)
{
    return std::any_of (song.channels.begin(), song.channels.end(), [insert] (const ChannelDef& c) {
        return c.mixerTrack == insert && c.instrument != InstrumentKind::audioClip && c.instrument != InstrumentKind::controller;
    });
}
} // namespace

const char* defaultName (Kind kind) noexcept
{
    switch (kind)
    {
        case Kind::voice: return "Voice";
        case Kind::instrument: return "Instrument";
        case Kind::drums: return "Drums";
        case Kind::sampler:
        case Kind::pads: return "Sampler";
        case Kind::guitar: return "Guitar";
        case Kind::bass: return "Bass";
        case Kind::audio: break;
    }
    return "Audio";
}

std::uint32_t defaultColour (Kind kind) noexcept
{
    // LYNX's own track colours, one per kind (muted tones that read on the dark theme next to the red accent)
    switch (kind)
    {
        case Kind::voice: return 0xFFD0645Au;
        case Kind::instrument: return 0xFF4FA3A5u;
        case Kind::drums: return 0xFFD39B3Eu;
        case Kind::sampler:
        case Kind::pads: return 0xFF8E6CC9u;
        case Kind::guitar: return 0xFF5C9E5Au;
        case Kind::bass: return 0xFF4C7FC2u;
        case Kind::audio: break;
    }
    return 0xFF9A8F7Fu;
}

bool isAudioKind (Kind kind) noexcept
{
    return kind == Kind::voice || kind == Kind::guitar || kind == Kind::bass || kind == Kind::audio;
}

int nextEmptyTrack (const Song& song)
{
    std::vector<bool> used ((std::size_t) playlistTrackCount, false);
    song.forEachClipList ([&used] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
            if (c.track >= 0 && c.track < playlistTrackCount)
                used[(std::size_t) c.track] = true;
    });
    for (int t = 0; t < (int) song.playlistTracks.size() && t < playlistTrackCount; ++t)
    {
        const auto& pt = song.playlistTracks[(std::size_t) t];
        if (pt.mode != PlaylistTrackMode::unassigned || ! pt.name.empty())
            used[(std::size_t) t] = true;
    }
    for (int t = 0; t < playlistTrackCount; ++t)
        if (! used[(std::size_t) t])
            return t;
    return -1;
}

double tempoInName (const std::string& name, bool* explicitBpm)
{
    auto stem = name;
    if (const auto dot = stem.find_last_of ('.'); dot != std::string::npos && dot > 0)
        stem.resize (dot);
    if (explicitBpm != nullptr)
        *explicitBpm = false;
    if (const auto bpm = autowarp::bpmInName (stem); bpm > 0.0)
    {
        if (explicitBpm != nullptr)
            *explicitBpm = true;
        return bpm;
    }
    // a whole number from 60 to 200 between separators (not part of a word: "Dm7", "808s", "4bars" don't count)
    const auto alnum = [] (char c) { return std::isalnum ((unsigned char) c) != 0; };
    for (std::size_t i = 0; i < stem.size();)
    {
        if (! alnum (stem[i]))
        {
            ++i;
            continue;
        }
        auto end = i;
        bool digits = true;
        while (end < stem.size() && alnum (stem[end]))
            digits = std::isdigit ((unsigned char) stem[end++]) != 0 && digits;
        if (digits && end - i >= 2 && end - i <= 3)
            if (const auto bpm = std::atoi (stem.substr (i, end - i).c_str()); bpm >= 60 && bpm <= 200)
                return (double) bpm;
        i = end;
    }
    return 0.0;
}

float loopBeats (const std::string& name, double seconds, float acidBeats, int beatsPerBar) noexcept
{
    if (! (seconds > 0.0) || seconds > 70.0)
        return 0.0f;
    if (acidBeats > 0.0f)
        return acidBeats;
    bool explicitBpm = false;
    const auto bpm = tempoInName (name, &explicitBpm);
    if (! (bpm > 0.0))
        return 0.0f;
    const auto beats = seconds * bpm / 60.0;
    const auto bar = (double) std::max (1, beatsPerBar);
    if (const auto bars = std::round (beats / bar); bars >= 1.0 && std::abs (beats - bars * bar) <= 0.25)
        return (float) (bars * bar);
    if (const auto whole = std::round (beats); whole >= 1.0 && whole < bar && std::abs (beats - whole) <= 0.08)
        return (float) whole;
    return explicitBpm && beats >= 1.0 ? (float) beats : 0.0f;
}

Result addTrack (Song& song, const Request& request)
{
    const auto track = nextEmptyTrack (song);
    if (track < 0)
        return failed (Error::noFreeTrack);

    // built on a copy: an error leaves the song exactly as it was (a free insert may already have been added)
    auto work = song;
    Result r;
    switch (request.kind)
    {
        case Kind::instrument:
        case Kind::sampler: r = addInstrument (work, track, request); break;
        case Kind::drums:
        case Kind::pads: r = addDrums (work, track, request); break;
        case Kind::voice:
        case Kind::guitar:
        case Kind::bass:
        case Kind::audio: r = addAudio (work, track, request); break;
    }
    if (r.ok())
        song = std::move (work);
    return r;
}

std::string rename (Song& song, int track, const std::string& name)
{
    if (name.empty() || track < 0 || track >= playlistTrackCount)
        return {};
    const auto kind = kindOf (song, track);
    const auto unique = pltracks::uniqueTrackName (song, name, track);
    auto& t = trackAt (song, track);
    const auto old = t.name;
    t.name = unique;
    if (! kind)
        return unique;

    const auto insert = insertOf (song, track);
    if (validInsert (song, insert))
        song.mixer[(std::size_t) insert].name = unique;
    if ((*kind == Kind::instrument || *kind == Kind::sampler) && validChannel (song, t.channel))
        song.channels[(std::size_t) t.channel].name = unique;
    if ((*kind == Kind::drums || *kind == Kind::pads) && ! old.empty())
    {
        // the kit's group and the track's pattern carry the track's name
        for (const auto& c : song.channels)
            if (c.mixerTrack == insert && c.group >= 0 && c.group < (int) song.channelGroups.size()
                && song.channelGroups[(std::size_t) c.group] == old)
                song.channelGroups[(std::size_t) c.group] = unique;
        for (const auto& clip : song.playlist)
            if (clip.track == track && clip.kind == ClipKind::pattern && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size()
                && song.patterns[(std::size_t) clip.pattern].name == old)
                song.patterns[(std::size_t) clip.pattern].name = unique;
    }
    return unique;
}

void recolour (Song& song, int track, std::uint32_t colour)
{
    const auto kind = kindOf (song, track);
    if (! kind)
        return;
    auto& t = trackAt (song, track);
    t.colour = colour;
    const auto insert = insertOf (song, track);
    if (validInsert (song, insert))
        song.mixer[(std::size_t) insert].colour = colour;
    if ((*kind == Kind::instrument || *kind == Kind::sampler) && validChannel (song, t.channel))
        song.channels[(std::size_t) t.channel].colour = colour;
    if (*kind == Kind::drums || *kind == Kind::pads)
        for (auto& c : song.channels)
            if (c.mixerTrack == insert && c.instrument != InstrumentKind::audioClip)
                c.colour = colour;
}

std::optional<Kind> kindOf (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return std::nullopt;
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.mode == PlaylistTrackMode::instrument && validChannel (song, t.channel))
        return song.channels[(std::size_t) t.channel].instrument == InstrumentKind::sampler ? Kind::sampler : Kind::instrument;
    if (t.mode == PlaylistTrackMode::audio && validInsert (song, t.mixerTrack))
    {
        if (! insertHasInstruments (song, t.mixerTrack))
            return Kind::audio;
        // round 190fo: a Sampler track's pads are marked on their channels
        const auto pads = std::any_of (song.channels.begin(), song.channels.end(), [insert = t.mixerTrack] (const ChannelDef& c) {
            return c.mixerTrack == insert && c.simplePad.index >= 0;
        });
        return pads ? Kind::pads : Kind::drums;
    }
    return std::nullopt;
}

int resetLevels (Song& song)
{
    int changed = 0;
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
    {
        if (! kindOf (song, t))
            continue;
        const auto insert = insertOf (song, t);
        if (! validInsert (song, insert))
            continue;
        auto& m = song.mixer[(std::size_t) insert];
        if (m.volume != unityFader || m.pan != 0.0f)
        {
            m.volume = unityFader;
            m.pan = 0.0f;
            ++changed;
        }
    }
    return changed;
}

int sharedReverb (const Song& song)
{
    for (int i = 1; i < (int) song.mixer.size(); ++i)
        if (const auto& m = song.mixer[(std::size_t) i]; m.name == "Reverb" && ! m.effects.empty() && m.effects.front().kind == EffectKind::atrium)
            return i;
    return -1;
}

int ensureSharedReverb (Song& song)
{
    if (const auto have = sharedReverb (song); have >= 0)
        return have;
    const auto insert = trackmodes::freeMixerTrack (song);
    if (insert <= 0 || insert >= (int) song.mixer.size())
        return -1;
    auto& m = song.mixer[(std::size_t) insert];
    m.name = "Reverb";
    m.colour = 0xFF5B7FA6u;
    m.inputChannel = -1;
    auto fx = makeEffectSlot (EffectKind::atrium);
    // a return: only the reverb (Mix fully wet), a medium room, the lows kept out of it
    for (const auto& [name, value] : { std::pair { "Mix", 1.0f }, { "Decay", 0.44f }, { "Low cut", 0.2f } })
        if (const auto p = presets::paramByName (EffectKind::atrium, name); p >= 0 && p < maxEffectParams)
            fx.params[(std::size_t) p] = value;
    m.effects.push_back (std::move (fx));
    return insert;
}

float reverbSend (const Song& song, int insert)
{
    const auto reverb = sharedReverb (song);
    if (reverb < 0 || insert <= 0 || insert >= (int) song.mixer.size() || insert == reverb)
        return 0.0f;
    for (const auto& s : song.mixer[(std::size_t) insert].sends)
        if (s.target == reverb)
            return s.level;
    return 0.0f;
}

bool setReverbSend (Song& song, int insert, float level)
{
    if (insert <= 0 || insert >= (int) song.mixer.size())
        return false;
    level = std::clamp (level, 0.0f, 1.0f);
    const auto reverb = level > 0.0f ? ensureSharedReverb (song) : sharedReverb (song);
    if (reverb < 0 || reverb == insert)
        return level <= 0.0f;
    auto& sends = song.mixer[(std::size_t) insert].sends;
    const auto at = std::find_if (sends.begin(), sends.end(), [reverb] (const MixerSend& s) { return s.target == reverb; });
    if (level <= 0.0f)
    {
        if (at != sends.end())
            sends.erase (at);
        return true;
    }
    if (at != sends.end())
        at->level = level;
    else
        sends.push_back ({ reverb, level, false });
    return true;
}

std::string stemTrackName (const std::string& stem)
{
    std::size_t i = 0;
    while (i < stem.size() && i < 3 && std::isdigit ((unsigned char) stem[i]))
        ++i;
    if (i == 0 || i >= stem.size() || (stem[i] != '_' && stem[i] != ' ' && stem[i] != '-'))
        return stem;
    auto j = i;
    while (j < stem.size() && (stem[j] == '_' || stem[j] == ' ' || stem[j] == '-'))
        ++j;
    return j < stem.size() ? stem.substr (j) : stem;
}

std::vector<FolderItem> folderPlan (std::vector<std::string> paths)
{
    const auto lower = [] (std::string s) {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);
        return s;
    };
    const auto fileName = [] (const std::string& p) { const auto at = p.find_last_of ('/'); return at == std::string::npos ? p : p.substr (at + 1); };
    const auto stemOf = [] (const std::string& f) { const auto dot = f.find_last_of ('.'); return dot == std::string::npos ? f : f.substr (0, dot); };
    const auto extOf = [&lower] (const std::string& f) { const auto dot = f.find_last_of ('.'); return dot == std::string::npos ? std::string() : lower (f.substr (dot + 1)); };
    // natural order of the file names (01 < 2 < 10), then the path
    const auto naturalLess = [&lower, &fileName] (const std::string& a, const std::string& b) {
        const auto x = lower (fileName (a)), y = lower (fileName (b));
        std::size_t i = 0, j = 0;
        while (i < x.size() && j < y.size())
        {
            if (std::isdigit ((unsigned char) x[i]) && std::isdigit ((unsigned char) y[j]))
            {
                std::size_t ei = i, ej = j;
                while (ei < x.size() && std::isdigit ((unsigned char) x[ei]))
                    ++ei;
                while (ej < y.size() && std::isdigit ((unsigned char) y[ej]))
                    ++ej;
                const auto nx = std::strtoull (x.substr (i, ei - i).c_str(), nullptr, 10), ny = std::strtoull (y.substr (j, ej - j).c_str(), nullptr, 10);
                if (nx != ny)
                    return nx < ny;
                i = ei, j = ej;
                continue;
            }
            if (x[i] != y[j])
                return x[i] < y[j];
            ++i, ++j;
        }
        return x.size() - i < y.size() - j || (x.size() - i == y.size() - j && a < b);
    };
    std::sort (paths.begin(), paths.end(), naturalLess);
    static const std::vector<std::string> audioExt { "wav", "aif", "aiff", "flac", "mp3", "ogg", "m4a", "wv" };
    std::vector<FolderItem> out;
    std::vector<std::string> audioNames;
    for (const auto& p : paths)
    {
        const auto file = fileName (p);
        const auto ext = extOf (file);
        if (file.empty() || file[0] == '.')
            continue;
        const auto midi = ext == "mid" || ext == "midi";
        if (! midi && std::find (audioExt.begin(), audioExt.end(), ext) == audioExt.end())
            continue;
        const auto stem = stemOf (file);
        if (midi && lower (stem).find ("(all tracks)") != std::string::npos)
            continue;
        if (! midi && lower (stem).size() >= 9 && lower (stem).rfind ("- mixdown") == lower (stem).size() - 9)
            continue; // (LYNX's export puts the whole song beside the stems)
        FolderItem item;
        item.path = p;
        item.name = stemTrackName (stem);
        item.midi = midi;
        if (! midi)
            audioNames.push_back (lower (item.name));
        out.push_back (std::move (item));
    }
    for (auto& item : out)
        if (item.midi && std::find (audioNames.begin(), audioNames.end(), lower (item.name)) != audioNames.end())
            item.muted = true;
    // audio first, then MIDI, each in name order (the stems are the song; the notes come after them)
    std::stable_partition (out.begin(), out.end(), [] (const FolderItem& i) { return ! i.midi; });
    return out;
}

bool isEmptySong (const Song& song)
{
    bool clips = false;
    song.forEachClipList ([&clips] (const std::vector<Clip>& list) { clips = clips || ! list.empty(); });
    if (clips)
        return false;
    for (const auto& p : song.patterns)
        if (! p.notes.empty())
            return false;
    for (const auto& t : song.playlistTracks)
        if (t.mode != PlaylistTrackMode::unassigned || ! t.name.empty())
            return false;
    return true;
}

int patternOf (const Song& song, int track)
{
    const auto kind = kindOf (song, track);
    if (kind != Kind::instrument && kind != Kind::sampler)
        return -1;
    const Clip* first = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.track == track && c.pattern >= 0 && c.pattern < (int) song.patterns.size()
            && (first == nullptr || c.start < first->start))
            first = &c;
    return first != nullptr ? first->pattern : -1;
}

int ensurePattern (Song& song, int track, int bars, bool* made)
{
    if (made != nullptr)
        *made = false;
    if (const auto p = patternOf (song, track); p >= 0)
        return p;
    const auto kind = kindOf (song, track);
    if (kind != Kind::instrument && kind != Kind::sampler)
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    Pattern pattern;
    pattern.name = ! t.name.empty() ? t.name : song.channels[(std::size_t) t.channel].name;
    pattern.colour = song.channels[(std::size_t) t.channel].colour;
    pattern.lengthTicks = (std::int64_t) std::max (1, bars) * std::max (1, song.beatsPerBar) * ppq;
    song.patterns.push_back (std::move (pattern));
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = (int) song.patterns.size() - 1;
    clip.track = track;
    clip.start = 0;
    clip.length = song.patterns.back().effectiveLength (song.beatsPerBar);
    song.playlist.push_back (clip);
    song.mode = PlayMode::song;
    if (made != nullptr)
        *made = true;
    return clip.pattern;
}

int insertOf (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.mode == PlaylistTrackMode::instrument && validChannel (song, t.channel))
    {
        const auto insert = song.channels[(std::size_t) t.channel].mixerTrack;
        return validInsert (song, insert) ? insert : validInsert (song, t.mixerTrack) ? t.mixerTrack : -1;
    }
    if (t.mode == PlaylistTrackMode::audio && validInsert (song, t.mixerTrack))
        return t.mixerTrack;
    return -1;
}
} // namespace lynx::simpletracks
