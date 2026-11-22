// Round 190pf (0.1.32, research190oe P13 and P12): the Chord lane (core/ChordLane.h, core/ChordLaneOps.h) — chord blocks
// over the song, named and kept in step with the time commands, detected from the clips' notes, made from the key
// regions, followed by channels while the song plays, lighting the Piano roll, feeding the Chord pad, written as MIDI notes
// and .flp markers — and Smart Tempo (core/SmartTempo.h): the project's tempo follows a free recording.

#include "core/ArrangeOps.h"
#include "core/ChordLane.h"
#include "core/ChordLaneOps.h"
#include "core/FlpExport.h"
#include "core/SectionOps.h"
#include "core/ScaleKeys.h"
#include "core/Scheduler.h"
#include "core/SmartTempo.h"
#include "flp/Flp.h"

namespace chordlane190pf
{
constexpr std::int64_t bar = 4 * (std::int64_t) lynx::ppq;

inline lynx::SongChord chordOf (int root, const char* suffix, std::int64_t start = 0, std::int64_t length = bar)
{
    lynx::SongChord c;
    lynx::chordlane::fromSuffix (root, suffix, -1, c);
    c.start = start;
    c.length = length;
    return c;
}

inline std::string names (const std::vector<lynx::SongChord>& list)
{
    std::string s;
    for (const auto& c : list)
        s += (s.empty() ? "" : " ") + lynx::chordlane::name (c);
    return s;
}

// A song with a piano channel (0), a kick (1) and a pad that follows the lane (2); pattern 0 = a four-chord loop over
// four bars (C, Am, F, G7 as whole notes, the bass an octave down), kicks on every beat on channel 1; one clip of it at 0
inline lynx::Song fourChords()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.mixer = Song::defaultMixer (4);
    s.channels.resize (3);
    s.channels[0].instrument = InstrumentKind::synth;
    s.channels[1].instrument = InstrumentKind::kick;
    s.channels[2].instrument = InstrumentKind::synth;
    s.playlistTracks.resize (4);
    Pattern p;
    p.name = "Loop";
    const std::vector<std::vector<int>> chords { { 48, 60, 64, 67 }, { 45, 57, 60, 64 }, { 41, 60, 65, 69 }, { 43, 59, 62, 65, 67 } };
    for (int b = 0; b < 4; ++b)
    {
        for (const auto k : chords[(std::size_t) b])
        {
            Note n;
            n.start = b * bar;
            n.length = bar - 24;
            n.key = k;
            n.channel = 0;
            p.notes.push_back (n);
        }
        for (int beat = 0; beat < 4; ++beat)
        {
            Note k;
            k.start = b * bar + beat * ppq;
            k.length = 48;
            k.key = 61; // (a C# kick on every beat: not harmony)
            k.channel = 1;
            p.notes.push_back (k);
        }
    }
    p.lengthTicks = 4 * bar;
    p.sortNotes();
    s.patterns.push_back (p);
    Clip c;
    c.pattern = 0;
    c.track = 0;
    c.start = 0;
    c.length = 4 * bar;
    s.playlist.push_back (c);
    return s;
}

struct Heard
{
    double tick;
    int channel, key, type;
};

inline std::vector<Heard> play (const lynx::Song& song)
{
    using namespace lynx;
    const auto tps = ticksPerSample (song.bpm, 48000.0);
    const auto index = makeScheduleIndex (song);
    std::vector<Heard> out;
    EventBuffer ev;
    const auto total = (std::int64_t) std::llround ((double) index.songLength / tps);
    for (std::int64_t at = 0; at < total; at += 512)
    {
        ev.clear();
        scheduleSong (song, (double) at * tps, (int) std::min<std::int64_t> (512, total - at), tps, ev, at == 0, &index, false);
        for (const auto& e : ev)
            out.push_back ({ ((double) at + e.sampleOffset) * tps, e.channel, e.key, (int) e.type });
    }
    return out;
}

// --- Smart Tempo's test recording: clicks on beats that speed up from 100 to 120 BPM over 16 bars, a louder click on
// each downbeat, after 0.3 s of silence; mono 44.1 kHz. `downbeats` gets the true downbeats (seconds).
inline std::vector<float> accelerando (double rate, std::vector<double>& downbeats, double from = 100.0, double to = 120.0, int bars = 16)
{
    const int beats = bars * 4 + 1;
    std::vector<double> at;
    double t = 0.3;
    for (int n = 0; n < beats; ++n)
    {
        at.push_back (t);
        const auto bpm = from + (to - from) * (double) n / (double) (beats - 1);
        t += 60.0 / bpm;
    }
    std::vector<float> out ((std::size_t) ((t + 1.0) * rate), 0.0f);
    std::uint32_t noise = 12345u;
    for (int n = 0; n < beats; ++n)
    {
        const auto down = n % 4 == 0;
        if (down)
            downbeats.push_back (at[(std::size_t) n]);
        const auto start = (std::size_t) std::llround (at[(std::size_t) n] * rate);
        for (std::size_t i = 0; i < (std::size_t) (0.04 * rate) && start + i < out.size(); ++i)
        {
            noise = noise * 1664525u + 1013904223u;
            const auto env = std::exp (-(double) i / (0.008 * rate));
            const auto tone = std::sin (2.0 * 3.14159265358979 * (down ? 1500.0 : 1000.0) * (double) i / rate);
            const auto hiss = ((double) (noise >> 8) / 8388608.0 - 1.0) * 0.3;
            out[start + i] += (float) ((down ? 0.9 : 0.5) * env * (tone + hiss));
        }
    }
    return out;
}
} // namespace chordlane190pf

TEST (chordlane190pf_names_shapes_and_follow)
{
    using namespace lynx;
    using namespace chordlane190pf;
    CHECK (names ({ chordOf (0, ""), chordOf (9, "m7"), chordOf (10, "maj7"), chordOf (7, "7"), chordOf (11, "m7b5"), chordOf (2, "sus4") })
           == "C Am7 A#maj7 G7 Bm7b5 Dsus4");
    CHECK (chordlane::name (chordOf (10, "maj7"), true) == "Bbmaj7");
    auto slash = chordOf (7, "");
    slash.bass = 11;
    CHECK (chordlane::name (slash) == "G/B");
    for (int i = 0; i < chordlane::shapeCount; ++i) // every listed shape names itself back
    {
        const auto c = chordlane::make (4, i);
        CHECK (chordlane::shapeOf (c) == i);
    }
    CHECK (chordlane::pitchClasses (chordOf (0, "7")) == ((1 << 0) | (1 << 4) | (1 << 7) | (1 << 10)));
    CHECK (chordlane::intervals (chordOf (2, "m9")) == std::vector<int> ({ 0, 3, 7, 10, 14 }));
    // follow: the nearest chord tone (a tie goes down); bass: the nearest bass note
    const auto f = chordOf (5, "");
    CHECK (chordlane::follow (64, f, chordlane::followChordTones) == 65 && chordlane::follow (62, f, chordlane::followChordTones) == 60
           && chordlane::follow (67, f, chordlane::followChordTones) == 65 && chordlane::follow (69, f, chordlane::followChordTones) == 69);
    CHECK (chordlane::follow (64, f, chordlane::followBass) == 65 && chordlane::follow (59, f, chordlane::followBass) == 53
           && chordlane::follow (64, f, chordlane::followOff) == 64);
    auto fOverA = f;
    fOverA.bass = 9;
    CHECK (chordlane::follow (60, fOverA, chordlane::followBass) == 57);
}

TEST (chordlane190pf_list_edits_and_time)
{
    using namespace lynx;
    using namespace chordlane190pf;
    std::vector<SongChord> list;
    CHECK (chordlane::put (list, chordOf (0, "", 0, 4 * bar)) == 0);
    CHECK (chordlane::put (list, chordOf (5, "", bar, bar)) == 1); // (cuts the C in two)
    CHECK (names (list) == "C F C" && list[0].length == bar && list[2].start == 2 * bar && list[2].length == 2 * bar
           && list[0].uid != list[2].uid);
    CHECK (chordlane::indexAt (list, bar - 1) == 0 && chordlane::indexAt (list, bar) == 1 && chordlane::indexAt (list, 4 * bar) == -1);
    chordlane::setChord (list, 2, chordOf (7, "7"));
    CHECK (names (list) == "C F G7" && list[2].start == 2 * bar);
    CHECK (chordlane::move (list, 1, 5 * bar) == 2 && names (list) == "C G7 F" && list[2].start == 5 * bar);
    chordlane::resize (list, 0, 3 * bar); // (G7 gives way: it starts where C ends now)
    CHECK (list[0].end() == 3 * bar && list[1].start == 3 * bar && list[1].length == bar);
    chordlane::remove (list, 1);
    CHECK (names (list) == "C F");
    // time: insert opens, delete closes, a piece pastes back
    chordlane::insertTime (list, bar, bar);
    CHECK (list[0].length == 4 * bar && list[1].start == 6 * bar);
    chordlane::deleteTime (list, 2 * bar, 3 * bar);
    CHECK (list[0].length == 3 * bar && list[1].start == 5 * bar);
    const auto part = chordlane::piece (list, 2 * bar, 6 * bar);
    CHECK (part.size() == 2 && part[0].start == 0 && part[0].length == bar && part[1].start == 3 * bar && part[1].length == bar);
    chordlane::pastePiece (list, 10 * bar, part);
    CHECK (names (list) == "C F C F" && list[2].start == 10 * bar && list[3].start == 13 * bar);
    // normalize: overlaps cut, bass = root dropped
    std::vector<SongChord> messy { chordOf (0, "", 2 * bar, 2 * bar), chordOf (2, "m", 0, 4 * bar) };
    messy[0].bass = 0;
    chordlane::normalize (messy);
    CHECK (messy.size() == 2 && messy[0].root == 2 && messy[0].length == 2 * bar && messy[1].bass == -1 && messy[0].uid != messy[1].uid);
}

TEST (chordlane190pf_arrange_and_sections_carry_chords)
{
    using namespace lynx;
    using namespace chordlane190pf;
    auto song = fourChords();
    song.chords() = { chordOf (0, "", 0, bar), chordOf (9, "m", bar, bar), chordOf (5, "", 2 * bar, bar), chordOf (7, "7", 3 * bar, bar) };
    chordlane::normalize (song.chords());
    // Copy / Paste Time take the chords along
    const auto time = arrange::copyTime (song, bar, 2 * bar);
    CHECK (time.chords.size() == 1 && time.chords[0].root == 9);
    arrange::pasteTime (song, 4 * bar, time);
    CHECK (names (song.chords()) == "C Am F G7 Am" && song.chords()[4].start == 4 * bar);
    arrange::deleteTime (song, 0, bar);
    CHECK (names (song.chords()) == "Am F G7 Am" && song.chords()[0].start == 0);
    // a section moved carries its chords (round 190ol's lane)
    auto s2 = fourChords();
    s2.chords() = { chordOf (0, "", 0, 2 * bar), chordOf (7, "", 2 * bar, 2 * bar) };
    s2.sections() = { { 0, 2 * bar, "A", 0, 1 }, { 2 * bar, 2 * bar, "B", 0, 2 } };
    CHECK (sections::move (s2, 1, 0));
    CHECK (names (s2.chords()) == "G C" && s2.chords()[0].start == 0 && s2.chords()[1].start == 2 * bar);
    // the arrangement copy keeps its own lane
    const auto added = s2.addArrangement ("Two", true);
    CHECK (added >= 0 && s2.arrangements[(std::size_t) added].chords.size() == 2);
}

TEST (chordlane190pf_detect_four_chord_loop)
{
    using namespace lynx;
    using namespace chordlane190pf;
    const auto song = fourChords();
    const auto notes = chordlane::songNotes (song, 0, 4 * bar);
    CHECK (notes.size() == 17); // (the kicks are no harmony)
    const auto found = chordlane::detect (song, notes, 0, 4 * bar);
    CHECK (names (found) == "C Am F G7");
    CHECK (found.size() == 4 && found[1].start == bar && found[1].length == bar && found[3].end() == 4 * bar);
    // half bars and beats find the same four blocks (equal neighbours join)
    CHECK (names (chordlane::detect (song, notes, 0, 4 * bar, chordlane::Resolution::halfBar)) == "C Am F G7");
    const auto beats = chordlane::detect (song, notes, 0, 4 * bar, chordlane::Resolution::beat);
    CHECK (names (beats) == "C Am F G7" && beats[2].start == 2 * bar && beats[2].length == bar);
    // a range, the selected clips only, then onto the lane (the range cleared first)
    CHECK (names (chordlane::detect (song, notes, bar, 3 * bar)) == "Am F");
    CHECK (chordlane::songNotes (song, 0, 4 * bar, { 5 }).empty());
    auto s = song;
    s.chords() = { chordOf (2, "", 0, 8 * bar) };
    CHECK (chordlane::write (s, 0, 4 * bar, found) == 4 && names (s.chords()) == "C Am F G7 D" && s.chords()[4].start == 4 * bar);
    // a clip longer than its pattern plays it again: the second pass's chords too
    auto twice = song;
    twice.playlist[0].length = 8 * bar;
    CHECK (names (chordlane::detect (twice, chordlane::songNotes (twice, 0, 8 * bar), 0, 8 * bar)) == "C Am F G7 C Am F G7");
    // one key list
    const auto am7 = chordlane::fromKeys ({ 57, 60, 64, 67 });
    CHECK (am7 && chordlane::name (*am7) == "Am7");
    const auto cOverE = chordlane::fromKeys ({ 52, 60, 67 });
    CHECK (cOverE && chordlane::name (*cOverE) == "C/E");
}

TEST (chordlane190pf_key_regions_degrees_pad_and_midi)
{
    using namespace lynx;
    using namespace chordlane190pf;
    // the degree chords of C major and A minor (the picker's list; round 190nu's degree stamp)
    std::string c, a;
    for (const auto& d : chordlane::degreeChords ({ 0, keyregions::majorScale() }))
        c += chordlane::name (d.chord) + ":" + d.numeral + " ";
    CHECK (c == "C:I Dm:ii Em:iii F:IV G:V Am:vi Bdim:vii\xc2\xb0 ");
    const auto sevenths = chordlane::degreeChords ({ 9, keyregions::minorScale() }, keyregions::DegreeSize::seventh);
    CHECK (sevenths.size() == 7 && chordlane::name (sevenths[0].chord) == "Am7" && chordlane::name (sevenths[4].chord) == "Em7");
    // chords from key regions: the project's key without regions, else one tonic chord per region
    auto song = fourChords();
    projectkey::set (song, projectkey::Key { 7, keyregions::majorScale() });
    CHECK (names (chordlane::fromKeyRegions (song, 0, 4 * bar)) == "G");
    keyregions::addKeyMarker (song.patterns[0], 2 * bar, { keyregions::State::key, { 9, keyregions::minorScale() } });
    const auto regions = chordlane::fromKeyRegions (song, 0, 4 * bar);
    CHECK (names (regions) == "G Am" && regions[1].start == 2 * bar && regions[1].end() == 4 * bar);
    // the Chord pad's set: each chord once, in song order
    song.chords() = { chordOf (0, "", 0, bar), chordOf (9, "m7", bar, bar), chordOf (0, "", 2 * bar, bar) };
    const auto pads = chordlane::padSet (song.chords());
    CHECK (pads.size() == 2 && pads[1].root == 9 && pads[1].quality == 1 && pads[1].extensions == std::vector<int> ({ 10 }));
    CHECK (scalekeys::setChord (pads, 1, 60, scalekeys::Spread::close) == std::vector<int> ({ 69, 72, 76, 79 }));
    // Export chords as MIDI: a new pattern on a new channel, its clip on a free track
    const auto channels = song.channels.size(), clips = song.playlist.size();
    const auto p = chordlane::exportAsPattern (song, -1, "Chords", "Chords");
    CHECK (p == (int) song.patterns.size() - 1 && song.channels.size() == channels + 1 && song.playlist.size() == clips + 1);
    const auto& notes = song.patterns[(std::size_t) p].notes;
    CHECK (notes.size() == 4 + 5 + 4); // C (3 + bass), Am7 (4 + bass), C again
    CHECK (std::count_if (notes.begin(), notes.end(), [] (const Note& n) { return n.start == bar && n.key == 57; }) == 1
           && std::count_if (notes.begin(), notes.end(), [] (const Note& n) { return n.start == bar && n.key == 79; }) == 1);
    CHECK (song.playlist.back().pattern == p && song.playlist.back().length == 3 * bar && song.playlist.back().track != 0);
}

TEST (chordlane190pf_follow_plays_moved_notes_and_highlight)
{
    using namespace lynx;
    using namespace chordlane190pf;
    // the pad (channel 2) holds E4 (64) and B3 (59) in every bar of a one-bar pattern placed four times; the lane is
    // C | F | G | (none)
    auto song = fourChords();
    Pattern pad;
    pad.name = "Pad";
    for (const auto k : { 64, 59 })
    {
        Note n;
        n.key = k;
        n.length = ppq;
        n.channel = 2;
        pad.notes.push_back (n);
    }
    pad.lengthTicks = bar;
    pad.sortNotes();
    song.patterns.push_back (pad);
    for (int b = 0; b < 4; ++b)
    {
        Clip c;
        c.pattern = 1;
        c.track = 1;
        c.start = b * bar;
        c.length = bar;
        song.playlist.push_back (c);
    }
    song.chords() = { chordOf (0, "", 0, bar), chordOf (5, "", bar, bar), chordOf (7, "", 2 * bar, bar) };
    const auto keysAt = [] (const std::vector<Heard>& heard, int channel, std::int64_t from, int type) {
        std::vector<int> out;
        for (const auto& h : heard)
            if (h.channel == channel && h.type == type && h.tick >= (double) from - 1.0 && h.tick < (double) (from + bar) - 1.0)
                out.push_back (h.key);
        std::sort (out.begin(), out.end());
        return out;
    };
    const auto on = (int) NoteEvent::Type::on, off = (int) NoteEvent::Type::off;
    const auto before = play (song);
    CHECK (keysAt (before, 2, bar, on) == std::vector<int> ({ 59, 64 })); // (nothing follows yet)
    song.channels[2].followChords = chordlane::followChordTones;
    const auto heard = play (song);
    CHECK (keysAt (heard, 2, 0, on) == std::vector<int> ({ 60, 64 }));       // C: B3 -> C4 (the nearest chord tone)
    CHECK (keysAt (heard, 2, bar, on) == std::vector<int> ({ 60, 65 }));     // F: E4 -> F4, B3 -> C4
    CHECK (keysAt (heard, 2, 2 * bar, on) == std::vector<int> ({ 59, 62 })); // G: E4 -> D4, B3 stays
    CHECK (keysAt (heard, 2, 3 * bar, on) == std::vector<int> ({ 59, 64 })); // no chord: as written
    CHECK (keysAt (heard, 2, bar, off) == std::vector<int> ({ 60, 65 }));    // the offs match their ons
    CHECK (keysAt (heard, 0, 0, on) == keysAt (before, 0, 0, on));           // the piano plays as written
    CHECK (song.patterns[1].notes.size() == 2 && song.patterns[1].notes[0].key + song.patterns[1].notes[1].key == 59 + 64
           && std::abs (song.patterns[1].notes[0].key - song.patterns[1].notes[1].key) == 5); // the pattern keeps its notes
    song.channels[2].followChords = chordlane::followBass;
    CHECK (keysAt (play (song), 2, bar, on) == std::vector<int> ({ 53, 65 })); // (B3 is six from F3 and F4: a tie goes down)
    // the Piano roll's chord highlight switches at the block edges
    CHECK (chordlane::highlightAt (song, bar - 1).root == 0 && chordlane::highlightAt (song, bar).root == 5
           && chordlane::highlightAt (song, bar).mask == ((1 << 5) | (1 << 9) | (1 << 0)) && chordlane::highlightAt (song, 3 * bar).root == -1);
}

TEST (chordlane190pf_flp_markers)
{
    using namespace lynx;
    using namespace chordlane190pf;
    auto song = fourChords();
    song.addMarker ({ 0, "Start", 0, 4 });
    song.chords() = { chordOf (0, "", 0, bar), chordOf (9, "m7", bar, bar) };
    const auto marked = chordlane::markersWithChords (song.markers(), song.chords());
    CHECK (marked.size() == 3 && marked[2].name == "Am7" && marked[2].tick == bar);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (! project.arrangements.empty());
    if (! project.arrangements.empty())
    {
        const auto& m = project.arrangements.front().markers;
        CHECK (m.size() == 3 && std::count_if (m.begin(), m.end(), [] (const flp::TimeMarker& x) { return x.name == "Am7" && x.type == 0; }) == 1);
    }
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("Chord lane") != std::string::npos; }));
    auto plain = song;
    plain.chords().clear();
    flpexport::Report plainReport;
    const auto plainProject = flp::parseProject (flpexport::exportFlp (plain, options, plainReport));
    CHECK (! plainProject.arrangements.empty() && plainProject.arrangements.front().markers.size() == 1);
    CHECK (std::none_of (plainReport.notes.begin(), plainReport.notes.end(), [] (const std::string& n) { return n.find ("Chord lane") != std::string::npos; }));
}

TEST (smarttempo190pf_accelerando_bars_and_plan)
{
    using namespace lynx;
    using namespace chordlane190pf;
    const double rate = 44100.0;
    std::vector<double> truth;
    const auto audio = accelerando (rate, truth);
    smarttempo::Options o;
    o.beatsPerBar = 4;
    const auto a = smarttempo::analyse (audio.data(), (long) audio.size(), rate, o);
    CHECK (a.ok() && a.bars.size() == truth.size());
    double worst = 0.0;
    for (std::size_t k = 0; k < std::min (a.bars.size(), truth.size()); ++k)
        worst = std::max (worst, std::abs (a.bars[k] - truth[k]));
    CHECK (worst < 0.010);

    // the song: 110 BPM, the recording's clip at bar 3 (offset 0) on an audio channel
    Song song;
    song.bpm = 110.0;
    song.mode = PlayMode::song;
    song.mixer = Song::defaultMixer (4);
    song.channels.resize (1);
    song.channels[0].instrument = InstrumentKind::audioClip;
    song.playlistTracks.resize (4);
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = 0;
    clip.track = 0;
    clip.start = 2 * bar;
    const auto seconds = (double) audio.size() / rate;
    clip.length = (std::int64_t) std::llround (seconds * 110.0 / 60.0 * ppq);
    song.playlist.push_back (clip);
    const auto tps = ticksPerSample (song.bpm, 48000.0);
    const auto noteBefore = audioClipNote (song, song.playlist[0], 0, tps);

    const auto plan = smarttempo::plan (song, 0, a.bars, seconds, 0.0);
    CHECK (plan.ok && plan.marks.size() == a.bars.size() - 1 && plan.firstBar == 2 * bar);
    CHECK (plan.maxErrorMs < 0.5);
    // the bar tempos: within 0.5 BPM of the true tempo of each bar
    double worstBpm = 0.0;
    for (std::size_t k = 0; k + 1 < truth.size() && k < plan.rawBpm.size(); ++k)
        worstBpm = std::max (worstBpm, std::abs (plan.rawBpm[k] - 240.0 / (truth[k + 1] - truth[k])));
    CHECK (worstBpm < 0.5);
    CHECK (plan.rawBpm.front() > 99.0 && plan.rawBpm.front() < 103.0 && plan.rawBpm.back() > 117.0 && plan.rawBpm.back() < 121.0);
    auto after = song;
    CHECK (smarttempo::apply (after, 0, plan));
    // the clip moved back by its 0.3 s lead-in so its first downbeat sits on bar 3
    const auto map = smarttempo::Map::of (after);
    const auto clipAt = map.seconds ((double) after.playlist[0].start);
    CHECK (after.playlist[0].start < 2 * bar && std::abs (map.seconds ((double) (2 * bar)) - clipAt - truth[0]) < 0.001);
    // every bar line within 10 ms of its downbeat
    double worstLine = 0.0;
    for (std::size_t k = 0; k < truth.size(); ++k)
        worstLine = std::max (worstLine, std::abs (map.seconds ((double) (2 * bar + (std::int64_t) k * bar)) - clipAt - truth[k]));
    CHECK (worstLine < 0.010);
    CHECK (tempomarkers::list (after).size() == plan.marks.size() && ! tempomarkers::list (after).front().glide);
    // the clip's audio plays as before (the same note: file start, pitch, stretch; no warp), to its end
    const auto noteAfter = audioClipNote (after, after.playlist[0], 0, tps);
    CHECK (noteAfter.startSeconds == noteBefore.startSeconds && noteAfter.cents == noteBefore.cents && after.playlist[0].stretch == 1.0
           && after.channels[0].sampler.stretchBeats == 0.0f && after.channels[0].sampler == song.channels[0].sampler);
    CHECK (std::abs (map.seconds ((double) after.playlist[0].end()) - clipAt - seconds) < 0.01);
    // smoothing averages the tempos: the bar lines drift, the preview says how far
    const auto smooth = smarttempo::plan (song, 0, a.bars, seconds, 1.0);
    CHECK (smooth.ok && smooth.maxErrorMs > 0.5 && smooth.marks.size() == plan.marks.size());
    // another tempo automation moves the tempo: nothing is written
    auto blocked = song;
    Automation t;
    t.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    t.points = { { 0, 0.2f, 0.0f } };
    blocked.automations.push_back (t);
    Clip tc;
    tc.kind = ClipKind::automation;
    tc.automation = 0;
    tc.track = 3;
    tc.length = bar;
    blocked.playlist.push_back (tc);
    CHECK (smarttempo::blockedBy (blocked) && ! smarttempo::plan (blocked, 0, a.bars, seconds).ok);
}

TEST (smarttempo190pf_half_double_shift_and_map)
{
    using namespace lynx;
    using namespace chordlane190pf;
    const double rate = 44100.0;
    std::vector<double> truth;
    const auto audio = accelerando (rate, truth, 110.0, 110.0, 8);
    smarttempo::Options o;
    const auto plainA = smarttempo::analyse (audio.data(), (long) audio.size(), rate, o);
    CHECK (plainA.bars.size() == 9);
    o.downbeatShift = 1;
    const auto shifted = smarttempo::analyse (audio.data(), (long) audio.size(), rate, o);
    CHECK (shifted.bars.size() == 8 && std::abs (shifted.bars[0] - plainA.beats[1]) < 1.0e-9);
    o.downbeatShift = 0;
    o.tempoFactor = 2.0;
    const auto twice = smarttempo::analyse (audio.data(), (long) audio.size(), rate, o);
    CHECK (twice.beats.size() == 2 * plainA.beats.size() - 1 && twice.bars.size() == 17);
    // the marker map: hold and glide segments, and its inverse
    smarttempo::Map m;
    m.startBpm = 120.0;
    m.marks = { { bar, 60.0, false }, { 2 * bar, 120.0, true } };
    CHECK (std::abs (m.seconds ((double) bar) - 2.0) < 1.0e-9 && std::abs (m.seconds (1.5 * (double) bar) - (2.0 + 4.0 * std::log (1.5))) < 1.0e-6); // (half the ramp: 60 -> 90)
    CHECK (std::abs (m.seconds ((double) (2 * bar)) - (2.0 + 4.0 * 60.0 / 60.0 * std::log (2.0))) < 1.0e-6); // (ramp 60 -> 120)
    CHECK (std::abs (m.tickAt (m.seconds (12345.0)) - 12345.0) < 0.01);
}
