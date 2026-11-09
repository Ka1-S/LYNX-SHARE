// --- Round 184a, slice S9: "Export for other apps" (core/ExportBundle.h). The plan of the folder: one stem per Playlist
// track that sounds, a MIDI file per track with notes (never the current pattern's notes for an audio track), the
// key as a MIDI key signature, and a README with tempo, time signature, key and the files.

#include "core/ExportBundle.h"
#include "core/PatternBlocks.h"

namespace bundle184a
{
using namespace lynx;

inline int flScaleNamed (const char* name)
{
    const auto& list = notetools::flReferenceScales();
    for (int i = 0; i < (int) list.size(); ++i)
        if (std::string (list[(std::size_t) i].name) == name)
            return i;
    return -1;
}

// Drums (pattern 0 on track 0), a vocal (audio clip on track 1), nothing on track 2, a bass (pattern 1 on track 3),
// and a muted clip of pattern 1 on track 4 (no stem: nothing plays there)
inline Song song()
{
    Song s;
    s.bpm = 92.0;
    s.beatsPerBar = 4;
    s.mixer.resize (5);
    s.channels.resize (3);
    s.channels[0].name = "Kick";
    s.channels[1].name = "Bass";
    s.channels[2].name = "Vocal";
    s.channels[2].instrument = InstrumentKind::audioClip;
    s.patterns.resize (2);
    for (int i = 0; i < 4; ++i)
    {
        Note n;
        n.channel = 0;
        n.key = 60;
        n.start = (std::int64_t) i * ppq;
        n.length = ppq / 4;
        s.patterns[0].notes.push_back (n);
    }
    Note b;
    b.channel = 1;
    b.key = 45;
    b.start = 0;
    b.length = ppq * 2;
    s.patterns[1].notes.push_back (b);
    s.playlistTracks.resize (5);
    s.playlistTracks[0].name = "Drums 1";
    s.playlistTracks[1].name = "Voice: take/2"; // (illegal file-name characters)
    s.playlistTracks[3].name = "Bass";
    const auto bar = (std::int64_t) ppq * 4;
    Clip drums;
    drums.track = 0;
    drums.pattern = 0;
    drums.length = bar;
    s.playlist.push_back (drums);
    drums.start = bar;
    s.playlist.push_back (drums);
    Clip vocal;
    vocal.kind = ClipKind::audio;
    vocal.channel = 2;
    vocal.track = 1;
    vocal.start = bar;
    vocal.length = bar * 3;
    s.playlist.push_back (vocal);
    Clip bass;
    bass.track = 3;
    bass.pattern = 1;
    bass.length = bar;
    s.playlist.push_back (bass);
    bass.track = 4;
    bass.muted = true;
    s.playlist.push_back (bass);
    return s;
}
} // namespace bundle184a

TEST (bundle184a_plan_has_one_stem_per_track_that_sounds)
{
    using namespace bundle184a;
    const auto s = song();
    const auto p = exportbundle::plan (s, "My Song");
    CHECK (p.folder == "My Song (for other apps)");
    CHECK ((int) p.stems.size() == 3);
    CHECK (p.stems[0].track == 0 && p.stems[0].path == "Stems/01 Drums 1.wav");
    CHECK (p.stems[1].track == 1 && p.stems[1].number == 2);
    CHECK (p.stems[1].path.find (':') == std::string::npos && p.stems[1].path.find ("take") != std::string::npos);
    CHECK (p.stems[1].path.rfind ("Stems/", 0) == 0); // the only '/' is the folder's
    CHECK (p.stems[1].path.find ('/', 6) == std::string::npos);
    CHECK (p.stems[2].track == 3 && p.stems[2].path == "Stems/03 Bass.wav");
    CHECK ((int) p.stems[0].clips.size() == 2);
    // the song's whole length for every stem: the vocal ends last
    CHECK (p.endTick == (std::int64_t) ppq * 4 * 4);
}

TEST (bundle184a_midi_only_for_tracks_with_notes)
{
    using namespace bundle184a;
    const auto s = song();
    const auto p = exportbundle::plan (s, "My Song");
    CHECK ((int) p.midi.size() == 2);
    CHECK (p.midi[0].path == "MIDI/01 Drums 1.mid");
    CHECK (p.midi[1].path == "MIDI/03 Bass.mid"); // its stem's number
    CHECK (p.allMidi == "MIDI/My Song (all tracks).mid");
    // the drum track: its two clips' notes, nothing of the bass
    const auto drums = exportbundle::trackNotes (s, p.midi[0]);
    CHECK ((int) drums.size() == 8);
    CHECK (std::all_of (drums.begin(), drums.end(), [] (const Note& n) { return n.channel == 0; }));
    // an audio track never falls back to the current pattern (FL's MIDI export does when nothing is arranged)
    CHECK (exportbundle::trackNotes (s, p.stems[1]).empty());
    CHECK ((int) exportbundle::songNotes (s).size() == 9);
}

TEST (bundle184a_audio_only_song_has_no_midi)
{
    using namespace bundle184a;
    auto s = song();
    s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern; }),
                      s.playlist.end());
    const auto p = exportbundle::plan (s, "");
    CHECK (p.name == "LYNX SHARE");
    CHECK ((int) p.stems.size() == 1);
    CHECK (p.midi.empty());
    CHECK (p.allMidi.empty());
    CHECK (exportbundle::songNotes (s).empty()); // (the current pattern has notes, but nothing is arranged)
}

TEST (bundle184a_options_and_other_files)
{
    using namespace bundle184a;
    const auto s = song();
    auto p = exportbundle::plan (s, "Beat");
    CHECK (p.mixdown == "Beat - mixdown.wav");
    CHECK (p.flp == "Beat.flp");
    CHECK (p.als == "Beat Project/Beat.als" && p.alsFolder == "Beat Project");
    CHECK (! p.zip);
    exportbundle::Options o;
    o.mixdown = false;
    o.zip = true;
    p = exportbundle::plan (s, "Beat", o);
    CHECK (p.mixdown.empty());
    CHECK (p.zip);
    CHECK (exportbundle::plan (Song {}, "x").empty());
}

TEST (bundle184a_key_signature_from_the_project_key)
{
    using namespace bundle184a;
    Song s;
    CHECK (! exportbundle::keySignature (s));
    CHECK (exportbundle::keyText (s) == "none");
    const auto sig = [&s] (int root, const char* scale) {
        projectkey::set (s, projectkey::Key { root, flScaleNamed (scale) });
        return *exportbundle::keySignature (s);
    };
    CHECK (sig (9, "Minor Natural (Aeolian)") == (exportbundle::KeySignature { 0, true }));  // A minor
    CHECK (sig (4, "Major (Ionian)") == (exportbundle::KeySignature { 4, false }));          // E major
    CHECK (sig (5, "Major (Ionian)") == (exportbundle::KeySignature { -1, false }));         // F major
    CHECK (sig (10, "Major (Ionian)") == (exportbundle::KeySignature { -2, false }));        // B♭ major
    CHECK (sig (6, "Major (Ionian)") == (exportbundle::KeySignature { 6, false }));          // F♯ major
    CHECK (sig (1, "Minor Natural (Aeolian)") == (exportbundle::KeySignature { 4, true }));  // C♯ minor
    CHECK (sig (2, "Minor Natural (Aeolian)") == (exportbundle::KeySignature { -1, true })); // D minor
    CHECK (sig (7, "Other Mixolydian") == (exportbundle::KeySignature { 1, false }));        // G Mixolydian: a major third
    CHECK (exportbundle::keyText (s).rfind ("G ", 0) == 0);
}

TEST (bundle184a_readme_lists_tempo_key_and_files)
{
    using namespace bundle184a;
    auto s = song();
    projectkey::set (s, projectkey::Key { 9, flScaleNamed ("Minor Natural (Aeolian)") });
    const auto p = exportbundle::plan (s, "My Song");
    const auto text = exportbundle::readme (s, p, 125.0, { p.stems[0].path, p.flp }, { p.als + " (could not write)" });
    CHECK (text.find ("92 BPM") != std::string::npos);
    CHECK (text.find ("4/4") != std::string::npos);
    CHECK (text.find ("A Minor Natural (Aeolian)") != std::string::npos);
    CHECK (text.find ("2:05") != std::string::npos);
    CHECK (text.find ("Stems/01 Drums 1.wav") != std::string::npos);
    CHECK (text.find ("Not written:") != std::string::npos);
    CHECK (text.find ("\r\n") != std::string::npos); // (Notepad-friendly line ends)
    s.bpm = 128.5;
    CHECK (exportbundle::tempoText (s) == "128.50 BPM");
}

TEST (bundle184a_pattern_blocks_become_tracks)
{
    using namespace bundle184a;
    auto s = song();
    s.playlist.clear();
    s.patternBlocksOn = true;
    CHECK (patternblocks::place (s, 0, 0));
    CHECK (patternblocks::place (s, 1, (std::int64_t) ppq * 4));
    CHECK (exportbundle::plan (s, "x").stems.empty()); // (blocks play on no Playlist track)
    const auto ready = exportbundle::prepared (s);
    const auto p = exportbundle::plan (ready, "x");
    CHECK ((int) p.stems.size() == 2);
    CHECK ((int) p.midi.size() == 2);
}
