// --- BandLab (.blx) import and the Simple layout's region loop, round 190jr (B15, BUILD-LIST 13; core/BlxImport.h,
// core/RegionLoop.h). The projects here are synthetic revisions written field by field (no BandLab file is in the repo);
// the field names and units are the ones core/BlxImport.h lists.

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "core/BlxImport.h"
#include "core/PlaylistOps.h"
#include "core/ProjectKey.h"
#include "core/RegionLoop.h"
#include "core/SimpleTracks.h"

namespace blx190jr
{
using Bytes = std::vector<std::uint8_t>;

inline Bytes bytesOf (const std::string& s) { return Bytes (s.begin(), s.end()); }

// a one-track MIDI file: `notes` = {beat, length, key, velocity} at 480 ticks per quarter
inline Bytes midiFile (const std::vector<std::array<double, 4>>& notes)
{
    struct Ev { std::uint32_t tick; std::uint8_t status, d1, d2; };
    std::vector<Ev> ev;
    for (const auto& n : notes)
    {
        ev.push_back ({ (std::uint32_t) std::lround (n[0] * 480), 0x90, (std::uint8_t) n[2], (std::uint8_t) n[3] });
        ev.push_back ({ (std::uint32_t) std::lround ((n[0] + n[1]) * 480), 0x80, (std::uint8_t) n[2], 0 });
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.tick < b.tick; });
    Bytes track;
    auto varLen = [&track] (std::uint32_t v) {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = (std::uint8_t) (v & 0x7F);
        while ((v >>= 7) != 0)
            buf[n++] = (std::uint8_t) ((v & 0x7F) | 0x80);
        while (n > 0)
            track.push_back (buf[--n]);
    };
    // tempo meta (ignored by the reader) and a running-status note-off pair
    varLen (0);
    for (auto b : { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 })
        track.push_back ((std::uint8_t) b);
    std::uint32_t last = 0;
    for (const auto& e : ev)
    {
        varLen (e.tick - last);
        last = e.tick;
        track.push_back (e.status);
        track.push_back (e.d1);
        track.push_back (e.d2);
    }
    varLen (0);
    for (auto b : { 0xFF, 0x2F, 0x00 })
        track.push_back ((std::uint8_t) b);
    Bytes out { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0, 'M', 'T', 'r', 'k' };
    const auto n = (std::uint32_t) track.size();
    for (int s = 24; s >= 0; s -= 8)
        out.push_back ((std::uint8_t) (n >> s));
    out.insert (out.end(), track.begin(), track.end());
    return out;
}

// a revision: an instrument track (looped MIDI region), a voice track (an audio region, rate 1.25, gain, fades, a
// compressor and a 3-band EQ, volume automation), a drum machine track and an unknown effect
inline std::string revision()
{
    return R"({
  "id": "rev-1", "key": "D min", "volume": 1.0, "description": "",
  "song": { "id": "song-1", "name": "Scratch é Song" },
  "metronome": { "bpm": 100, "signature": { "notesCount": 3, "noteValue": 4 } },
  "mastering": { "preset": "cdMaster", "bypass": false },
  "auxChannels": [ { "id": "aux1", "preset": "sharedReverb", "returnLevel": 1, "effects": null } ],
  "samples": [
    { "id": "s-midi", "isMidi": true, "status": "Ready", "duration": 2.4 },
    { "id": "s-vox", "isMidi": false, "status": "Ready", "duration": 6.0, "file": "https://cdn.example/x/s-vox.m4a?sig=1" },
    { "id": "s-beat", "isMidi": true, "status": "Ready" }
  ],
  "tracks": [
    { "id": "t2", "order": 1, "name": "Vox", "type": "voice", "volume": 0.5, "pan": -0.5, "isMuted": true, "colorName": "Blue",
      "effects": [ { "slug": "compressor", "bypass": false, "params": { "attack": 0.001, "knee": 6, "ratio": 4, "release": 0.068, "threshold": -25.3 }, "automation": {} },
                   { "slug": "threeBandEq", "bypass": true, "params": { "lowGain": 0, "midGain": -3, "highGain": 6, "midFreq": 2000 } },
                   { "slug": "wahWah", "params": { "rate": 2 } } ],
      "effectsData": { "displayName": "Studio Vocals" },
      "auxSends": [ { "id": "aux1", "sendLevel": 0.25, "automation": [] } ],
      "automation": { "volume": [ { "position": 4, "value": 1 }, { "position": 8, "value": 0 } ], "pan": [] },
      "regions": [ { "id": "r2", "trackId": "t2", "sampleId": "s-vox", "name": "Take 1", "startPosition": 1.2, "endPosition": 4.8,
                     "sampleOffset": 0.5, "loopLength": 0, "playbackRate": 1.25, "pitchShift": -2, "gain": 0.5, "fadeIn": 0.3, "fadeOut": 0.6 } ] },
    { "id": "t1", "order": 0, "name": "Piano", "type": "instrument", "soundbank": "studio-grand-piano", "volume": 1, "pan": 0, "colorName": "Red",
      "regions": [ { "id": "r1", "trackId": "t1", "sampleId": "s-midi", "name": "Chords", "startPosition": 0, "endPosition": 6,
                     "sampleOffset": 0, "loopLength": 2.4, "playbackRate": 1, "pitchShift": 0 } ] },
    { "id": "t3", "order": 2, "name": "Beat", "type": "drum-machine", "volume": 2.5, "patterns": [ { "notes": [1, 2], "sampleId": "s-beat" } ],
      "regions": [ { "id": "r3", "sampleId": "s-beat", "startPosition": 2.4, "endPosition": 4.8 } ] }
  ]
})";
}

inline lynx::BlxImportOptions options (lynx::FlpImportReport* report, std::map<std::string, Bytes>* files)
{
    lynx::BlxImportOptions o;
    o.folder = "/dl/Scratch";
    for (const auto& [path, b] : *files)
        if (path.rfind (o.folder + "/", 0) == 0)
            o.files.push_back (path.substr (o.folder.size() + 1));
    o.common.report = report;
    o.common.fileExists = [files] (const std::string& p) { return files->count (p) > 0; };
    o.common.readFile = [files] (const std::string& p) {
        const auto it = files->find (p);
        return it == files->end() ? Bytes() : it->second;
    };
    return o;
}

inline std::map<std::string, Bytes> folder()
{
    std::map<std::string, Bytes> files;
    // the piano's MIDI: three notes, the last outside the 2.4 s (4-beat) pass
    files["/dl/Scratch/MIDI/s-midi.mid"] = midiFile ({ { 0.0, 1.0, 60, 100 }, { 1.0, 0.5, 64, 64 }, { 5.0, 1.0, 67, 90 } });
    files["/dl/Scratch/Audio/s-vox.wav"] = { 0 };
    files["/dl/Scratch/Audio/s-beat.mid"] = midiFile ({ { 0.0, 0.25, 36, 127 }, { 1.0, 0.25, 38, 100 }, { 1.5, 0.25, 42, 80 } });
    return files;
}
} // namespace blx190jr

TEST (blx190jr_json_and_keys)
{
    using namespace lynx;
    std::string error;
    const auto p = blx::parse (blx190jr::bytesOf (blx190jr::revision()), &error);
    CHECK (p.has_value());
    if (! p)
        return;
    CHECK (p->title == "Scratch \xC3\xA9 Song" && std::abs (p->bpm - 100.0) < 1e-9 && p->numerator == 3 && p->denominator == 4);
    CHECK (p->tracks.size() == 3 && p->tracks[0].name == "Piano" && p->tracks[1].name == "Vox"); // (by "order")
    CHECK (p->samples.size() == 3 && p->samples[0].midi && ! p->samples[1].midi && p->mastering == "cdMaster" && p->auxChannels == 1);
    CHECK (p->tracks[1].volumeAutomation.size() == 2 && p->tracks[1].reverbSend == 0.25 && p->tracks[1].effects.size() == 3);
    // wrapped revisions and a BOM are found too; broken JSON and zips say why
    const auto wrapped = "\xEF\xBB\xBF{ \"revision\": " + blx190jr::revision() + " }";
    CHECK (blx::parse (blx190jr::bytesOf (wrapped)).has_value());
    CHECK (blx::parse (blx190jr::bytesOf ("[" + blx190jr::revision() + "]")).has_value());
    CHECK (! blx::parse (blx190jr::bytesOf ("{ \"tracks\": [ }"), &error) && error.find ("JSON") != std::string::npos);
    CHECK (! blx::parse (blx190jr::bytesOf ("{ \"name\": 1 }"), &error) && error.find ("no tracks") != std::string::npos);
    CHECK (! blx::parse ({ 'P', 'K', 3, 4 }, &error) && error.find ("zipped") != std::string::npos);
    for (std::size_t n = 0; n < 200; n += 7) // (cut anywhere: no crash)
        (void) blx::parse (blx190jr::bytesOf (blx190jr::revision().substr (0, n)));
    // keys
    CHECK (blx::parseKey ("D min") == std::make_pair (2, true) && blx::parseKey ("F# Major") == std::make_pair (6, false));
    CHECK (blx::parseKey ("Bbm") == std::make_pair (10, true) && blx::parseKey ("B minor") == std::make_pair (11, true));
    CHECK (blx::parseKey ("C") == std::make_pair (0, false) && blx::parseKey ("").first == -1 && blx::parseKey ("H").first == -1);
    CHECK (blx::colourOf ("Violet") != 0 && blx::colourOf ("Teal") == 0);
}

TEST (blx190jr_midi_reader)
{
    using namespace lynx;
    const auto notes = blx::readMidi (blx190jr::midiFile ({ { 0.0, 1.0, 60, 100 }, { 0.5, 0.25, 64, 70 } }));
    CHECK (notes.has_value() && notes->size() == 2);
    if (notes && notes->size() == 2)
    {
        CHECK ((*notes)[0].key == 60 && std::abs ((*notes)[0].length - 1.0) < 1e-9 && (*notes)[0].velocity == 100);
        CHECK ((*notes)[1].key == 64 && std::abs ((*notes)[1].beat - 0.5) < 1e-9);
    }
    CHECK (! blx::readMidi ({ 1, 2, 3 }).has_value());
    auto bytes = blx190jr::midiFile ({ { 0.0, 1.0, 60, 100 } });
    for (std::size_t n = 0; n < bytes.size(); ++n) // (cut anywhere: no crash)
        (void) blx::readMidi (blx190jr::Bytes (bytes.begin(), bytes.begin() + (std::ptrdiff_t) n));
}

TEST (blx190jr_imports_simple_tracks)
{
    using namespace lynx;
    auto files = blx190jr::folder();
    FlpImportReport report;
    std::string error;
    const auto song = importBlxBytes (blx190jr::bytesOf (blx190jr::revision()), blx190jr::options (&report, &files), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    CHECK (std::abs (song->bpm - 100.0) < 1e-9 && song->beatsPerBar == 3 && song->info.title == "Scratch \xC3\xA9 Song");
    const auto key = projectkey::get (*song);
    CHECK (key && key->root == 2 && projectkey::isMinor (*key));
    CHECK (simpletracks::kindOf (*song, 0) == simpletracks::Kind::instrument && simpletracks::kindOf (*song, 1) == simpletracks::Kind::audio
           && simpletracks::kindOf (*song, 2) == simpletracks::Kind::drums);
    CHECK (song->playlistTracks[0].name == "Piano" && song->playlistTracks[1].muted && song->playlistTracks[0].colour == blx::colourOf ("Red"));

    // the piano: a 2.4 s (4 beats at 100 BPM) pass looped over 6 s = passes of 4, 4 and 2 beats, one shared pattern
    std::vector<const Clip*> piano, vox, beat;
    for (const auto& c : song->playlist)
        (c.track == 0 ? piano : c.track == 1 ? vox : beat).push_back (&c);
    CHECK (piano.size() == 3);
    if (piano.size() == 3)
    {
        CHECK (piano[0]->start == 0 && piano[0]->length == 4 * 960 && piano[1]->start == 4 * 960 && piano[2]->length == 2 * 960);
        CHECK (piano[0]->pattern == piano[1]->pattern && piano[1]->pattern == piano[2]->pattern);
        const auto& p = song->patterns[(std::size_t) piano[0]->pattern];
        CHECK (p.notes.size() == 2 && p.lengthTicks == 4 * 960 && p.name == "Chords"); // (the note at beat 5 is past the pass)
        // the import's loop is a region-loop run (core/RegionLoop.h)
        const auto run = regionloop::runOf (*song, (int) (piano[2] - song->playlist.data()));
        CHECK (run.passes() == 3 && run.end == 10 * 960);
    }
    // the voice take: 1.2 s → 2 beats, offset 0.5 s at rate 1.25 = 0.4 s = 2/3 beat, rate as the channel's stretch
    CHECK (vox.size() == 1);
    if (vox.size() == 1)
    {
        const auto& c = *vox[0];
        CHECK (c.kind == ClipKind::audio && c.start == 2 * 960 && c.length == 6 * 960);
        CHECK (std::abs ((double) c.offset + (double) c.offsetFraction - 640.0) < 1e-3 && std::abs (c.gain - 0.5f) < 1e-6f && c.pitch == -2.0f);
        CHECK (c.fadeIn == 480 && c.fadeOut == 960);
        const auto& ch = song->channels[(std::size_t) c.channel];
        CHECK (ch.samplePath == "/dl/Scratch/Audio/s-vox.wav" && ch.sampler.keepPitch && std::abs (ch.sampler.stretchBeats - 8.0f) < 1e-4f);
        const auto insert = simpletracks::insertOf (*song, 1);
        CHECK (insert > 0 && std::abs (song->mixer[(std::size_t) insert].pan + 0.5f) < 1e-6f
               && std::abs (faderGain (song->mixer[(std::size_t) insert].volume) - 0.5f) < 1e-4f);
        // compressor then EQ (bypassed); the unknown pedal is listed, not kept
        const auto& fx = song->mixer[(std::size_t) insert].effects;
        CHECK (fx.size() == 2 && fx[0].kind == EffectKind::compressor && fx[1].kind == EffectKind::eq && fx[0].enabled && ! fx[1].enabled);
        if (fx.size() == 2)
        {
            CHECK (std::abs (fx[0].params[0] - (-25.3f + 60.0f) / 60.0f) < 1e-5f && std::abs (fx[0].params[1] - 3.0f / 19.0f) < 1e-5f);
            CHECK (std::abs (fx[1].params[3] - (6.0f / 36.0f + 0.5f)) < 1e-5f);
        }
        CHECK (report.droppedEffects.count ("BandLab effect \"wahWah\"") == 1);
        // volume automation: a lane under the track, beats → ticks, linear → fader
        const auto& lanes = song->playlistTracks[1].lanes;
        CHECK (lanes.size() == 1);
        if (lanes.size() == 1)
        {
            const auto& a = song->automations[(std::size_t) lanes[0].automation];
            CHECK (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == insert && a.points.size() == 2);
            CHECK (a.points.size() == 2 && a.points[0].tick == 4 * 960 && std::abs (a.points[0].value - unityFader) < 1e-4f && a.points[1].value == 0.0f);
        }
        // the shared reverb send
        CHECK (simpletracks::sharedReverb (*song) > 0 && std::abs (simpletracks::reverbSend (*song, insert) - 0.25f) < 1e-4f);
    }
    // the drum machine: GM keys to the built-in drums, its volume capped
    CHECK (beat.size() == 1);
    if (beat.size() == 1)
    {
        const auto& p = song->patterns[(std::size_t) beat[0]->pattern];
        CHECK (beat[0]->start == 4 * 960 && p.notes.size() == 3);
        if (p.notes.size() == 3)
            CHECK (song->channels[(std::size_t) p.notes[0].channel].instrument == InstrumentKind::kick
                   && song->channels[(std::size_t) p.notes[1].channel].instrument == InstrumentKind::snare
                   && song->channels[(std::size_t) p.notes[2].channel].instrument == InstrumentKind::hat);
    }
    CHECK (report.partialEffects.count ("BandLab: track volume above +5.6 dB (capped)") == 1);
    CHECK (report.droppedEffects.count ("BandLab: Mastering preset \"cdMaster\" (use the Master's own effects)") == 1);
    CHECK (report.missingSamples == 0);
}

TEST (blx190jr_missing_and_found_files)
{
    using namespace lynx;
    auto files = blx190jr::folder();
    files.erase ("/dl/Scratch/Audio/s-vox.wav");
    files["/dl/Scratch/Take 1.flac"] = { 0 }; // (found by the region's name)
    FlpImportReport report;
    auto options = blx190jr::options (&report, &files);
    auto song = importBlxBytes (blx190jr::bytesOf (blx190jr::revision()), options);
    CHECK (song && report.missingSamples == 0);
    files.erase ("/dl/Scratch/Take 1.flac");
    files.erase ("/dl/Scratch/MIDI/s-midi.mid");
    FlpImportReport second;
    options = blx190jr::options (&second, &files);
    song = importBlxBytes (blx190jr::bytesOf (blx190jr::revision()), options);
    CHECK (song && second.missingSamples == 2 && second.missingFiles.size() == 2);
    // the user located the take: it is used
    options.common.relocations["/dl/Scratch/s-vox.wav"] = "/elsewhere/vox.wav";
    files["/elsewhere/vox.wav"] = { 0 };
    FlpImportReport third;
    options.common.report = &third;
    song = importBlxBytes (blx190jr::bytesOf (blx190jr::revision()), options);
    CHECK (song && third.missingSamples == 1);
    // without a folder listing: the direct names next to the .blx
    FlpImportReport fourth;
    auto direct = blx190jr::options (&fourth, &files);
    direct.files.clear();
    files["/dl/Scratch/Audio/s-vox.wav"] = { 0 };
    CHECK (blxSampleFile (nullptr, "s-vox", "", false, direct) == "/dl/Scratch/Audio/s-vox.wav");
    CHECK (blxSampleFile (nullptr, "s-beat", "", true, direct) == "/dl/Scratch/Audio/s-beat.mid");
    CHECK (blxSampleFile (nullptr, "nothing", "", true, direct).empty());
}

TEST (blx190jr_region_loop)
{
    using namespace lynx;
    Song song;
    song.playlist.clear();
    song.patterns.assign (2, Pattern {});
    song.patterns[0].lengthTicks = 4 * 960;
    Clip a;
    a.kind = ClipKind::pattern;
    a.pattern = 0;
    a.track = 2;
    a.start = 960;
    a.length = 4 * 960;
    a.fadeIn = 10;
    Clip other = a;
    other.pattern = 1;
    other.start = 15 * 960;
    song.playlist = { a, other };
    CHECK (regionloop::loopable (song, 0) && regionloop::runOf (song, 0).passes() == 1);
    // drag to 2.5 passes: whole passes, then a half
    CHECK (regionloop::loopTo (song, 0, a.start + 10 * 960) == a.start + 10 * 960);
    auto run = regionloop::runOf (song, 0);
    CHECK (run.passes() == 3 && run.pass == 4 * 960 && run.start == 960 && run.end == 11 * 960);
    CHECK (song.playlist.size() == 4 && song.playlist[2].fadeIn == 0 && song.playlist[3].length == 2 * 960 && song.playlist[3].uid == 0);
    // any repeat finds the same run; shorter than one pass is not possible
    CHECK (regionloop::runOf (song, 3).clips.front() == 0 && regionloop::runOf (song, 2).passes() == 3);
    CHECK (regionloop::loopTo (song, 2, 0) == a.end() && regionloop::runOf (song, 0).passes() == 1);
    // never past the next clip: Loop until next stops at it
    CHECK (regionloop::loopTo (song, 0, 40 * 960) == 15 * 960 && regionloop::runOf (song, 0).end == 15 * 960);
    CHECK (regionloop::nextStart (song, regionloop::runOf (song, 0)) == 15 * 960);
    CHECK (regionloop::unloop (song, 0) && song.playlist.size() == 2 && ! regionloop::unloop (song, 0));
    CHECK (regionloop::loopUntilNext (song, 0, 0) == 15 * 960 && regionloop::runOf (song, 0).passes() == 4);
    // the last clip of a track: until the song's end
    const auto last = regionloop::regionAt (song, 2, 15 * 960, other);
    CHECK (last >= 0 && regionloop::loopUntilNext (song, last, 23 * 960) == 23 * 960 && regionloop::runOf (song, last).passes() == 2);
    // a clip of another track or another source is not a repeat; a different gain breaks the run
    Clip lone = a;
    lone.track = 3;
    lone.start = a.end();
    song.playlist.push_back (lone);
    CHECK (regionloop::runOf (song, (int) song.playlist.size() - 1).passes() == 1);
    Clip louder = song.playlist[(std::size_t) regionloop::runOf (song, 0).clips[1]];
    CHECK (regionloop::unloop (song, 0));
    louder.start = a.end();
    louder.gain = 0.5f;
    song.playlist.push_back (louder);
    CHECK (regionloop::runOf (song, 0).passes() == 1);
    // audio clips loop too; automation clips and grid slots don't
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 0;
    audio.track = 5;
    audio.length = 960;
    song.playlist.push_back (audio);
    CHECK (regionloop::loopTo (song, (int) song.playlist.size() - 1, 3 * 960) == 3 * 960);
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.length = 960;
    song.playlist.push_back (automation);
    CHECK (! regionloop::loopable (song, (int) song.playlist.size() - 1) && regionloop::loopTo (song, (int) song.playlist.size() - 1, 9 * 960) == -1);
}
