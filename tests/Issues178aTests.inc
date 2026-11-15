// Round 178a: GitHub issues #87, #63, #86 (docs/analise/REA_rodada178a.md)

#include "core/ClipOps.h"

// #87: an audio clip dropped at 130 BPM, then the tempo lowered to 108: the clip kept its length in ticks while its
// audio (played at the file's own speed) got shorter in bars, so the clip showed an empty tail. FL keeps such a clip's
// region in milliseconds and resizes it with the tempo; a clip whose channel follows the tempo keeps its beats.
TEST (issues178a_audio_clips_follow_a_tempo_change)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 130.0;
    song.playlist.clear();
    song.arrangements.resize (2);
    song.currentArrangement = 0;

    ChannelDef plain;
    plain.instrument = InstrumentKind::audioClip;
    plain.samplePath = "drill.wav";
    ChannelDef synced = plain; // Time stretch set: the whole file lasts 16 beats, whatever the tempo
    synced.sampler.stretchBeats = 16.0f;
    song.channels.push_back (plain);
    const auto plainIndex = (int) song.channels.size() - 1;
    song.channels.push_back (synced);
    const auto syncedIndex = (int) song.channels.size() - 1;

    // a 60 s file at 44.1 kHz, as the Playlist sizes a dropped clip
    const double frames = 60.0 * 44100.0, rate = 44100.0;
    Clip whole;
    whole.kind = ClipKind::audio;
    whole.channel = plainIndex;
    whole.start = 4 * ppq;
    whole.length = clipops::fileTicks (song.channels[(std::size_t) plainIndex], frames, rate, song.bpm);
    CHECK (whole.length == 130 * ppq); // 60 s at 130 BPM = 130 beats

    Clip trimmed = whole; // a part of it: skips 10 s, plays 20 s, with fades
    trimmed.track = 1;
    trimmed.offset = (std::int64_t) (10.0 * 130.0 / 60.0 * ppq);
    trimmed.length = (std::int64_t) (20.0 * 130.0 / 60.0 * ppq);
    trimmed.fadeIn = ppq;
    trimmed.fadeOut = 2 * ppq;

    Clip stretched = whole;
    stretched.track = 2;
    stretched.channel = syncedIndex;
    stretched.length = 16 * ppq;

    Clip pattern;
    pattern.kind = ClipKind::pattern;
    pattern.track = 3;
    pattern.length = 8 * ppq;

    song.playlist = { whole, trimmed, stretched, pattern };
    song.arrangements[1].clips = { whole };

    clipops::followTempo (song, 108.0);
    CHECK (song.bpm == 108.0);

    // the whole clip ends where its audio does at the new tempo: no empty tail
    const auto& w = song.playlist[0];
    const auto audio = clipops::fileTicks (song.channels[(std::size_t) plainIndex], frames, rate, 108.0);
    std::printf ("  whole clip at 108 BPM: %lld ticks, audio %lld ticks\n", (long long) w.length, (long long) audio);
    CHECK (std::llabs (w.length - audio) <= 1);
    CHECK (w.start == 4 * ppq); // the position stays

    // the trimmed clip plays the same seconds of the same file
    const auto& t = song.playlist[1];
    const auto seconds = [] (double ticks, double bpm) { return ticks / ppq * 60.0 / bpm; };
    CHECK (std::abs (seconds ((double) t.offset + t.offsetFraction, 108.0) - 10.0) < 1e-6);
    CHECK (std::abs (seconds ((double) t.length, 108.0) - 20.0) < 0.001);
    CHECK (std::abs (seconds ((double) t.fadeIn, 108.0) - seconds (ppq, 130.0)) < 0.001);
    CHECK (std::abs (seconds ((double) t.fadeOut, 108.0) - seconds (2 * ppq, 130.0)) < 0.001);

    // tempo-synced audio and pattern clips keep their beats
    CHECK (song.playlist[2].length == 16 * ppq);
    CHECK (song.playlist[3].length == 8 * ppq);

    // the stored arrangement follows too
    CHECK (std::llabs (song.arrangements[1].clips[0].length - audio) <= 1);

    // back to 130: the clips are as they were (to the tick), even after many small steps (a drag on the tempo)
    for (int i = 1; i <= 220; ++i)
        clipops::followTempo (song, 108.0 + 0.1 * i);
    clipops::followTempo (song, 130.0);
    CHECK (std::llabs (song.playlist[0].length - whole.length) <= 2);
    CHECK (std::llabs (song.playlist[1].offset - trimmed.offset) <= 2);
    CHECK (std::llabs (song.playlist[1].length - trimmed.length) <= 2);

    // the same tempo again changes nothing
    const auto before = song.playlist;
    clipops::followTempo (song, 130.0);
    CHECK (song.playlist == before);
}

#include "core/Arpeggiator.h"

// #63 (the rest of it): the Arpeggiator played every note centred, with no fine pitch or Mod X / Y: it kept only the
// held notes' keys and velocities. Each arpeggiated note now takes the expression of the held note it plays.
TEST (issues178a_arpeggiator_keeps_the_note_pan)
{
    using namespace lynx;
    ArpSettings s;
    s.direction = 1; // up
    s.stepBeats = 0.25f;
    Arpeggiator arp;
    const auto tps = ticksPerSample (120.0, 48000.0);

    std::array<NoteEvent, 2> in {};
    const float pans[] = { -1.0f, 0.75f };
    const int keys[] = { 60, 67 };
    for (int k = 0; k < 2; ++k)
    {
        auto& e = in[(std::size_t) k];
        e.type = NoteEvent::Type::on;
        e.channel = 0;
        e.key = keys[k];
        e.velocity = 0.8f;
        e.pan = pans[k];
        e.cents = k == 0 ? -30.0f : 40.0f;
        e.modX = k == 0 ? 0.1f : 0.9f;
        e.modY = k == 0 ? 0.2f : 0.7f;
        e.colour = (std::int8_t) (k + 3);
    }

    std::vector<NoteEvent> played;
    auto first = true;
    for (double tick = 0.0; tick < ppq * 2.0; tick += 512 * tps)
    {
        int out = 0;
        arp.process (0, first ? in.data() : nullptr, first ? 2 : 0, tick, tps, 512, s, arp.output.data(), out);
        first = false;
        for (int i = 0; i < out; ++i)
            if (arp.output[(std::size_t) i].type == NoteEvent::Type::on)
                played.push_back (arp.output[(std::size_t) i]);
    }
    CHECK (played.size() >= 6);
    for (const auto& e : played)
    {
        const auto k = e.key == 60 ? 0 : 1;
        CHECK (e.key == keys[k]);
        CHECK (e.pan == pans[k]);
        CHECK (e.cents == (k == 0 ? -30.0f : 40.0f));
        CHECK (e.modX == (k == 0 ? 0.1f : 0.9f) && e.modY == (k == 0 ? 0.2f : 0.7f));
        CHECK (e.colour == (std::int8_t) (k + 3));
    }

    // FL's Repeat 0 (the whole chord on each step) keeps each note's own pan too
    s.octaveRepeat = 0;
    arp.reset();
    played.clear();
    first = true;
    for (double tick = 0.0; tick < ppq; tick += 512 * tps)
    {
        int out = 0;
        arp.process (0, first ? in.data() : nullptr, first ? 2 : 0, tick, tps, 512, s, arp.output.data(), out);
        first = false;
        for (int i = 0; i < out; ++i)
            if (arp.output[(std::size_t) i].type == NoteEvent::Type::on)
                played.push_back (arp.output[(std::size_t) i]);
    }
    CHECK (played.size() >= 2);
    for (const auto& e : played)
        CHECK (e.pan == ((e.key % 12) == 0 ? -1.0f : 0.75f));
}

// #63: a note's Mod X / Mod Y on FL's native sampler voice: FL adds the note's Init.FCut / FRes ((b − 128) / 128 of its
// Mod X / Y bytes, REA_rodada76 §1) to the channel's filter X / Y. So a note at Mod X 64 (0.25) on a channel at filter X
// 228 sounds exactly like a centred note on a channel at filter X 100; a centred note is unchanged.
TEST (issues178a_note_mod_x_moves_the_fl_voice_filter)
{
    using namespace lynx;
    auto noise = std::make_shared<SampleData>();
    noise->sampleRate = 44100.0;
    std::uint32_t seed = 7;
    for (int i = 0; i < 22050; ++i)
    {
        seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
        noise->left.push_back ((float) (((double) seed / 2147483647.0 * 2.0 - 1.0) * 0.3));
    }
    const std::vector<std::int32_t> plain = { 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, 0 };
    const std::vector<std::int32_t> vol = { 4, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, -101 };
    auto render = [&] (int filterX, int filterY, std::optional<std::pair<float, float>> mod) {
        ChannelDef c;
        c.instrument = InstrumentKind::sampler;
        for (const auto* r : { &plain, &vol, &plain, &plain, &plain })
            c.sampler.flChannel.insert (c.sampler.flChannel.end(), r->begin(), r->end());
        c.sampler.flChannel.insert (c.sampler.flChannel.end(), { filterX, filterY, 0, 96 });
        auto inst = makeInstrument (InstrumentKind::sampler, noise);
        inst->prepare (44100.0);
        inst->configure (c, 120.0);
        inst->setSongPosition (0.0);
        if (mod)
            inst->setNextNoteModulation (mod->first, mod->second);
        inst->noteOn (60, 100.0f / 128.0f);
        std::vector<float> l (8000), r (8000);
        inst->render (l.data(), r.data(), 8000, 1.0f, 1.0f);
        return l;
    };
    const auto centred = render (100, 64, std::nullopt);
    CHECK (render (100, 64, std::pair { 0.5f, 0.5f }) == centred);              // Mod X / Y at centre: nothing changes
    const auto darker = render (100, 64, std::pair { 0.25f, 0.5f });
    CHECK (darker != centred);                                                 // the note's Mod X moves the cutoff
    const auto same = render (228, 64, std::pair { 0.25f, 0.5f });             // 228/256 − 0.5 = 100/256
    auto diff = 0.0;
    for (std::size_t i = 0; i < same.size(); ++i)
        diff = std::max (diff, (double) std::abs (same[i] - centred[i]));
    std::printf ("  channel X 228 + note Mod X 0.25 vs channel X 100: max diff %.3g\n", diff);
    CHECK (diff < 1e-6);
    CHECK (render (100, 64, std::pair { 0.5f, 0.9f }) != centred);             // Mod Y: the resonance
}

#include "core/TempoMap.h"
#include "core/PianoRollTools.h"
#include "core/MidiImport.h"
#include "core/FlpExport.h"

namespace issues178a
{
lynx::PatternEvents tempoLane (std::initializer_list<std::pair<std::int64_t, double>> events)
{
    lynx::PatternEvents lane;
    lane.target = { lynx::AutomationTargetKind::tempo, 0 };
    for (const auto& [tick, bpm] : events)
        lane.events.push_back ({ tick, lynx::valueFromTempo (bpm) });
    return lane;
}
bool near (double a, double b, double tolerance = 0.01) { return std::abs (a - b) <= tolerance; }
} // namespace issues178a

// #86, pattern mode: the current pattern's Tempo events, looped with it; before the first one, the project tempo.
TEST (issues178a_tempo_events_in_pattern_mode)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 130.0;
    song.mode = PlayMode::pattern;
    song.patterns.resize (1);
    song.currentPattern = 0;
    auto& p = song.patterns[0];
    CHECK (! tempomap::active (song));
    CHECK (song.tempoAt (ppq) == 130.0);

    p.events.push_back (issues178a::tempoLane ({ { ppq, 140.0 }, { 3 * ppq, 90.0 } }));
    const auto length = (double) p.effectiveLength (song.meter());
    CHECK (tempomap::active (song));
    CHECK (song.tempoAt (0.0) == 130.0);                                // before the first event
    CHECK (issues178a::near (song.tempoAt (ppq), 140.0));
    CHECK (issues178a::near (song.tempoAt (2.5 * ppq), 140.0));
    CHECK (issues178a::near (song.tempoAt (3 * ppq), 90.0));
    CHECK (issues178a::near (song.tempoAt (length + 0.5 * ppq), 90.0)); // the next loop: the last event still holds
    CHECK (issues178a::near (song.tempoAt (length + ppq), 140.0));

    // the time a beat lasts follows the events: one beat at 130, two at 140, then 90
    const auto seconds = song.secondsForTicks (4 * ppq);
    const auto expected = 60.0 / 130.0 + 2.0 * 60.0 / 140.0 + 60.0 / 90.0;
    std::printf ("  4 beats with tempo events: %.4f s (exact %.4f s)\n", seconds, expected);
    CHECK (std::abs (seconds - expected) < 0.01);

    // another pattern without events: the project tempo
    song.patterns.resize (2);
    song.currentPattern = 1;
    CHECK (! tempomap::active (song));
    CHECK (song.tempoAt (3 * ppq) == 130.0);
}

// #86, song mode (FL's time map): pattern clips play their Tempo events while the position is inside them and the last
// write holds afterwards (nothing is restored); inside a tempo automation clip the clip wins; after it, its last value
// holds until a later event.
TEST (issues178a_tempo_events_in_song_mode)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 120.0;
    song.mode = PlayMode::song;
    song.playlist.clear();
    song.automations.clear();
    song.patterns.resize (1);
    auto& p = song.patterns[0];
    p.lengthTicks = 4 * ppq;
    p.events.push_back (issues178a::tempoLane ({ { 0, 100.0 }, { 2 * ppq, 150.0 } }));

    Clip a;
    a.kind = ClipKind::pattern;
    a.pattern = 0;
    a.start = 8 * ppq;
    a.length = 4 * ppq;
    song.playlist.push_back (a);
    CHECK (tempomap::active (song));
    CHECK (song.tempoAt (4 * ppq) == 120.0);                               // before the clip
    CHECK (issues178a::near (song.tempoAt (8 * ppq), 100.0));
    CHECK (issues178a::near (song.tempoAt (11 * ppq), 150.0));
    CHECK (issues178a::near (song.tempoAt (40 * ppq), 150.0));             // held after the clip

    // a clip that starts in the middle of its pattern (offset 2 beats) plays only the events from there on
    Clip b = a;
    b.start = 20 * ppq;
    b.offset = 3 * ppq;
    b.length = ppq;
    b.track = 1;
    song.playlist.push_back (b);
    CHECK (issues178a::near (song.tempoAt (20.5 * ppq), 150.0));           // (the clip played no event: 150 holds)

    // a tempo automation clip from 16 to 24: inside it, its curve wins over the pattern clip's events
    Automation t;
    t.target = { AutomationTargetKind::tempo, 0 };
    t.points = { { 0, valueFromTempo (60.0) }, { 8 * ppq, valueFromTempo (60.0) } };
    song.automations.push_back (t);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.start = 16 * ppq;
    c.length = 8 * ppq;
    c.track = 2;
    song.playlist.push_back (c);
    Clip d = a; // a pattern clip overlapping the automation clip
    d.start = 18 * ppq;
    d.track = 3;
    song.playlist.push_back (d);
    CHECK (issues178a::near (song.tempoAt (12 * ppq), 150.0));
    CHECK (issues178a::near (song.tempoAt (16 * ppq), 60.0));
    CHECK (issues178a::near (song.tempoAt (21 * ppq), 60.0));             // the automation clip wins over d's events
    CHECK (issues178a::near (song.tempoAt (25 * ppq), 60.0));             // after it: the value it ended with
    Clip e = a;
    e.start = 26 * ppq;
    e.track = 4;
    song.playlist.push_back (e);
    CHECK (issues178a::near (song.tempoAt (26 * ppq), 100.0));            // a later event writes over it
    CHECK (issues178a::near (song.tempoAt (2 * ppq), 60.0));              // before anything: the first tempo clip's value
}

// #86: FL's .flp keeps the Tempo events as BPM × 1000 (0x40000005 events) and the initialized tempo in 0xD8; both go
// through LYNX's export and import unchanged.
TEST (issues178a_tempo_events_survive_flp)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 128.0;
    song.patterns.resize (1);
    song.patterns[0].events.push_back (issues178a::tempoLane ({ { 0, 128.0 }, { 4 * ppq, 95.5 }, { 8 * ppq, 300.0 } }));
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    auto found = 0;
    for (const auto& pattern : project.patterns)
        for (const auto& e : pattern.controlEvents)
            if (e.id.raw == flp::ControlId::tempo)
            {
                ++found;
                CHECK (e.value == 128000 || e.value == 95500 || e.value == 300000);
            }
    CHECK (found == 3);

    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, io);
    const PatternEvents* lane = nullptr;
    for (const auto& p : back.patterns)
        if (const auto* l = tempomap::tempoLane (p))
            lane = l;
    CHECK (lane != nullptr);
    if (lane != nullptr)
    {
        CHECK (lane->events.size() == 3);
        if (lane->events.size() == 3)
        {
            CHECK (lane->events[1].tick == 4 * ppq);
            CHECK (issues178a::near (tempoFromValue (lane->events[0].value), 128.0, 0.001));
            CHECK (issues178a::near (tempoFromValue (lane->events[1].value), 95.5, 0.001));
            CHECK (issues178a::near (tempoFromValue (lane->events[2].value), 300.0, 0.001));
        }
    }
}

// #86: FL's MIDI import writes every Set Tempo as a Tempo event of the new pattern, a whole BPM rounded half to even
// (10..522); the realign moves them with the notes. LYNX used to keep only the first tempo, and a file starting at
// exactly 120 BPM took its second one.
TEST (issues178a_midi_tempos_become_tempo_events)
{
    using namespace lynx;
    CHECK (midiimport::midiTempoBpm (60.0 / 120.0) == 120.0);
    CHECK (midiimport::midiTempoBpm (60.0 / 120.5) == 120.0);   // half to even
    CHECK (midiimport::midiTempoBpm (60.0 / 121.5) == 122.0);
    CHECK (midiimport::midiTempoBpm (60.0 / 99.7) == 100.0);
    CHECK (midiimport::midiTempoBpm (60.0 / 4.0) == 10.0);      // FL's range
    CHECK (midiimport::midiTempoBpm (60.0 / 900.0) == 522.0);

    Pattern p;
    p.events.push_back (issues178a::tempoLane ({ { 0, 77.0 } })); // the pattern's old Tempo events are replaced
    PatternEvents vol;
    vol.target = { AutomationTargetKind::channelVolume, 0 };
    vol.events.push_back ({ 0, 0.5f });
    p.events.push_back (vol);
    const std::vector<PatternEvent> tempos = { { 0, valueFromTempo (120.0) }, { 2 * ppq, valueFromTempo (140.0) },
                                               { 2 * ppq, valueFromTempo (141.0) }, { 6 * ppq, valueFromTempo (90.0) } };
    midiimport::setTempoEvents (p, tempos, ppq);
    CHECK (p.events.size() == 2);
    const auto* lane = tempomap::tempoLane (p);
    CHECK (lane != nullptr);
    if (lane != nullptr)
    {
        CHECK (lane->events.size() == 3);                         // one per tick: the last one of a tick wins
        if (lane->events.size() == 3)
        {
            CHECK (lane->events[0].tick == 0 && issues178a::near (tempoFromValue (lane->events[0].value), 120.0));
            CHECK (lane->events[1].tick == ppq && issues178a::near (tempoFromValue (lane->events[1].value), 141.0));
            CHECK (lane->events[2].tick == 5 * ppq);
        }
    }
    midiimport::setTempoEvents (p, {}, 0);
    CHECK (tempomap::tempoLane (p) == nullptr && p.events.size() == 1);
}

// #86: FL's event score (.fsc of format 0x18): the Event editor's Save automation as / Open automation.
TEST (issues178a_event_score_round_trip)
{
    using namespace lynx;
    const std::vector<PatternEvent> events = { { 0, valueFromTempo (140.0) }, { ppq / 2, 0.25f }, { 7 * ppq, 1.0f } };
    const auto bytes = prtools::writeEventScore (events);
    CHECK (bytes.size() > 22 && bytes[0] == 'F' && bytes[8] == 0x18);
    const auto back = prtools::parseEventScore (bytes);
    CHECK (back.has_value());
    if (back)
        CHECK (*back == events);

    // a note score is not an event score
    std::vector<Note> notes (1);
    notes[0].key = 60;
    notes[0].length = ppq;
    CHECK (! prtools::parseEventScore (prtools::writeScore (notes)).has_value());
    CHECK (! prtools::parseEventScore (std::vector<std::uint8_t> { 1, 2, 3 }).has_value());
}

// #86: the head and last record of a real "Save automation as" from FL 2026.1.7 (a Tempo lane, records' control = 5)
TEST (issues178a_event_score_from_real_fl_file)
{
    using namespace lynx;
    const std::vector<std::uint8_t> file = {
        0x46, 0x4c, 0x68, 0x64, 0x06, 0x00, 0x00, 0x00, 0x18, 0x00, 0x21, 0x00, 0x60, 0x00, 0x46, 0x4c, 0x64, 0x74, 0x81, 0x00,
        0x00, 0x00, 0xc7, 0x0c, 0x32, 0x36, 0x2e, 0x31, 0x2e, 0x37, 0x2e, 0x35, 0x36, 0x35, 0x34, 0x00, 0x9f, 0x16, 0x16, 0x00,
        0x00, 0xa9, 0x0f, 0x00, 0x00, 0x00, 0x1c, 0x01, 0xac, 0x01, 0x01, 0x00, 0xc0, 0x36, 0x46, 0x00, 0x4c, 0x00, 0x20, 0x00,
        0x53, 0x00, 0x74, 0x00, 0x75, 0x00, 0x64, 0x00, 0x69, 0x00, 0x6f, 0x00, 0x20, 0x00, 0x32, 0x00, 0x36, 0x00, 0x2e, 0x00,
        0x31, 0x00, 0x2e, 0x00, 0x37, 0x00, 0x2e, 0x00, 0x35, 0x00, 0x36, 0x00, 0x35, 0x00, 0x34, 0x00, 0x2e, 0x00, 0x35, 0x00,
        0x36, 0x00, 0x35, 0x00, 0x34, 0x00, 0x00, 0x00, 0x25, 0x01, 0x41, 0x00, 0x00, 0xdf, 0x24, 0x25, 0x00, 0x00, 0x00, 0x05,
        0x00, 0x00, 0x00, 0x27, 0x31, 0x44, 0x3d, 0x26, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x0e, 0x2d, 0x45, 0x3d, 0x2b,
        0x01, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0xa0, 0x9a, 0xe2, 0x3d
    };
    const auto events = prtools::parseEventScore (file);
    CHECK (events.has_value());
    if (! events)
        return;
    CHECK (events->size() == 3);
    CHECK ((*events)[0].tick == (std::int64_t) std::llround (37.0 * ppq / 96.0));
    CHECK ((*events)[2].tick == (std::int64_t) std::llround (299.0 * ppq / 96.0));
    CHECK (std::abs (tempoFromValue ((*events)[2].value) - 66.651) < 0.01); // FL's hint bar showed 66.651 BPM for it

    // what we write for Tempo carries the same control number
    const auto written = prtools::writeEventScore (*events, 5);
    CHECK (written[written.size() - 8] == 5);
    CHECK (prtools::parseEventScore (written) == events);
}

TEST (issues178a_event_lane_takes_the_chosen_controller)
{
    using namespace lynx;
    // the file FL imported in the Event editor (REA_rodada178a §11): C notes with CC 1 at 5, 21, ... each beat
    midiimport::ParsedMidi midi;
    midi.trackNames = { "", "other" };
    const std::int64_t beat = ppq;
    for (int i = 0; i < 8; ++i)
    {
        midi.events.push_back ({ 0, beat * i + beat, 0x90, (std::uint8_t) (48 + i), 100 });
        midi.events.push_back ({ 0, beat * i + beat, 0xB0, 1, (std::uint8_t) (i * 16 + 5) });
    }
    midi.events.push_back ({ 1, 0, 0xB1, 1, 127 });  // another track and MIDI channel
    midi.events.push_back ({ 0, 0, 0xB0, 7, 64 });   // another controller

    midiimport::Options o;
    CHECK (midiimport::controllerEvents (midi, o).size() == 10); // FL's "---": every controller, merged
    CHECK (midiimport::controllerEvents (midi, o)[0].tick == 0);

    o.controller = 1;
    o.track = 0;
    auto lane = midiimport::controllerEvents (midi, o);
    CHECK (lane.size() == 8);
    CHECK (lane[0].tick == beat && std::fabs (lane[0].value - 5.0f / 127.0f) < 1.0e-6f); // FL's .fsc: 0.03937
    CHECK (lane[1].tick == 2 * beat && std::fabs (lane[1].value - 21.0f / 127.0f) < 1.0e-6f); // FL's .fsc: 0.16535

    o.realign = true;
    CHECK (midiimport::controllerEvents (midi, o)[0].tick == 0); // the silence before the first note goes

    o.realign = false;
    o.track = -1;
    o.channels = 2; // MIDI channel 2 alone
    lane = midiimport::controllerEvents (midi, o);
    CHECK (lane.size() == 1 && lane[0].tick == 0 && lane[0].value == 1.0f);

    o.controller = 74;
    CHECK (midiimport::controllerEvents (midi, o).empty()); // FL then empties the lane
}

#include "core/BrowserView.h"

// FL's Browser shows one "Scores" root for Data\Patches\Scores and the user's Presets\Scores: a folder in both is one
// folder (its other copies merged in turn), a file in both takes the first folder's, case doesn't matter.
TEST (issues178a_merged_roots_list_the_union_of_their_folders)
{
    using namespace lynx;
    browserview::Listing user, data;
    user.folders = { "Automation" };
    user.files = { "Mine.fsc", "Randomness.fsc" };
    data.folders = { "Arpeggiator", "automation", "Chopping" };
    data.files = { "4 steps.fsc", "randomness.FSC" };

    const auto m = browserview::mergeListings ({ user, data });
    CHECK (m.folders.size() == 3);
    CHECK (m.folders[0].name == "Automation" && m.folders[0].from == 0);
    CHECK (m.folders[0].also == std::vector<int> { 1 });
    CHECK (m.folders[1].name == "Arpeggiator" && m.folders[1].from == 1 && m.folders[1].also.empty());
    CHECK (m.folders[2].name == "Chopping" && m.folders[2].from == 1);
    CHECK (m.files.size() == 3);
    CHECK (m.files[1].name == "Randomness.fsc" && m.files[1].from == 0 && m.files[1].also.empty());
    CHECK (m.files[2].name == "4 steps.fsc" && m.files[2].from == 1);

    // one folder alone lists as it is; none lists nothing
    CHECK (browserview::mergeListings ({ data }).folders.size() == 3);
    CHECK (browserview::mergeListings ({}).files.empty());
}

// Settings from before the merged roots. A user who hid "FL MIDI" (Data\Patches\Scores) or "Mixer presets (FL)" keeps
// the merged root, without that folder (excluded, saved with the view); one who hid their own folder (the merged
// root's first) still doesn't see the root.
TEST (issues178a_merged_roots_keep_a_hidden_old_root_hidden)
{
    using namespace lynx;
    const std::string user = "C:/Users/u/Documents/Image-Line/FL Studio/Presets/Mixer presets";
    const std::string data = "C:/Program Files/Image-Line/FL Studio 2026/Data/Patches/Mixer presets";

    browserview::View v; // hid FL's copy: the root stays, FL's folder is left out
    v.setShown (data, false);
    CHECK (browserview::migrateMergedRoot (v, user, { data }));
    CHECK (v.shows (user) && v.hidden.count (data) == 0 && v.excluded.count (data) == 1);
    CHECK (! browserview::migrateMergedRoot (v, user, { data })); // the next start: nothing more to do
    CHECK (browserview::View::fromText (v.toText()) == v);        // saved with the view

    browserview::View own; // hid their own folder: the merged root (the same id) stays hidden, FL's isn't left out
    own.setShown (user, false);
    CHECK (! browserview::migrateMergedRoot (own, user, { data }));
    CHECK (! own.shows (user) && own.excluded.empty());

    browserview::View untouched; // nothing hidden: nothing changes
    CHECK (! browserview::migrateMergedRoot (untouched, user, { data }) && untouched.hidden.empty());

    // FL not installed or no user folder: the root is the one folder there is, nothing merged
    browserview::View alone;
    alone.setShown (data, false);
    CHECK (! browserview::migrateMergedRoot (alone, data, {}) && ! alone.shows (data));
}
