// --- Round 190cg: warp commands (core/WarpTools.h), arrangement commands (core/ArrangeOps.h) and the .als fields that
// carry them (time signature changes, the switched-off loop range, the tempo lane's range). The sets are written by the
// tests; no file of Live's is used.

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ArrangeOps.h"
#include "core/WarpTools.h"

namespace warp190cg
{
inline bool near (double a, double b, double tolerance = 1.0e-9) { return std::abs (a - b) <= tolerance; }

// a 4 s file at 120 bpm with its start and end markers: 8 beats
inline lynx::SamplerSettings fourSeconds()
{
    lynx::SamplerSettings s;
    lynx::warpedit::ensureMarkers (s, 4.0, 120.0);
    return s;
}

// a song with three audio tracks (inserts 1…3) and a lane of insert 1's volume on track 0
inline lynx::Song laneSong()
{
    lynx::Song song;
    song.mixer = lynx::Song::defaultMixer (4);
    song.playlistTracks.resize (4);
    for (int t = 0; t < 3; ++t)
    {
        song.playlistTracks[(std::size_t) t].mode = lynx::PlaylistTrackMode::audio;
        song.playlistTracks[(std::size_t) t].mixerTrack = t + 1;
    }
    lynx::Automation a;
    a.name = "Vol";
    a.target = { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
    a.points = { { 0, 0.2f, 0.0f }, { 960, 0.8f, 0.0f }, { 1920, 0.4f, 0.0f }, { 7680, 0.4f, 0.0f } };
    song.automations.push_back (a);
    song.playlistTracks[0].lanes.push_back ({ 0, 0 });
    return song;
}

inline lynx::Clip patternClip (int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = 0;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}
} // namespace warp190cg

TEST (warp190cg_segment_tempo_and_x2)
{
    using namespace lynx;
    using warp190cg::near;
    auto s = warp190cg::fourSeconds();
    CHECK (near (warptools::segmentBpm (s.warp, 0), 120.0));
    // a marker at 1 s moved from beat 2 to beat 3: the first second is read as 180 bpm, the rest as 100
    warpedit::moveMarker (s, warpedit::addMarker (s, 1.0), 3.0, 4.0);
    CHECK (near (warptools::segmentBpm (s.warp, 0), 180.0) && near (warptools::segmentBpm (s.warp, 1), 100.0));
    CHECK (near (warptools::segmentBpm (s.warp, 2), 100.0)); // the last marker answers with the segment that ends on it
    CHECK (warptools::segmentAt (s.warp, 0.5) == 0 && warptools::segmentAt (s.warp, 1.0) == 1 && warptools::segmentAt (s.warp, 9.0) == 1);
    CHECK (warptools::segmentBpm ({}, 0) == 0.0);

    // x2: every beat doubles (the audio is read as twice the tempo and plays half as fast); /2 brings it back
    CHECK (warptools::scaleTempo (s, 2.0));
    warpedit::syncLength (s, 4.0);
    CHECK (near (warptools::segmentBpm (s.warp, 0), 360.0) && near (s.warp[1].beats, 6.0) && s.stretchBeats == 16.0f);
    CHECK (warptools::scaleTempo (s, 0.5));
    warpedit::syncLength (s, 4.0);
    CHECK (near (s.warp[1].beats, 3.0) && s.stretchBeats == 8.0f);
    CHECK (! warptools::scaleTempo (s, 0.0));

    // typing a segment's tempo: the later markers keep their own tempos
    CHECK (warptools::setSegmentBpm (s, 0, 120.0));
    CHECK (near (s.warp[1].beats, 2.0) && near (warptools::segmentBpm (s.warp, 1), 100.0) && near (s.warp[2].beats, 7.0));
    CHECK (! warptools::setSegmentBpm (s, 2, 120.0) && ! warptools::setSegmentBpm (s, 0, 0.0));
}

TEST (warp190cg_set_1_1_1_here)
{
    using namespace lynx;
    using warp190cg::near;
    auto s = warp190cg::fourSeconds();
    // the audio at 0.5 s (beat 1) becomes beat 0: every marker shifts by one beat, the tempo stays
    CHECK (near (warptools::setOneOneOne (s, 0.5), 1.0));
    CHECK (near (s.warp[0].beats, -1.0) && near (s.warp[1].beats, 7.0) && near (warptools::segmentBpm (s.warp, 0), 120.0));
    CHECK (near (warpedit::beatAt (s.warp, 0.5), 0.0));
    warpedit::syncLength (s, 4.0);
    CHECK (s.stretchBeats == 8.0f); // the file still lasts 8 beats
    // a clip that starts on 1.1.1 skips the beat before it
    CHECK (near (warptools::playedBeatOfZero (s, 4.0), 1.0));
}

TEST (warp190cg_warp_from_here)
{
    using namespace lynx;
    using warp190cg::near;
    // markers 0 → 0, 1 s → 3, 2 s → 5, 4 s → 8. "Warp 90 BPM From Here" at 1.5 s: the markers after it go, the audio there
    // keeps its beat (4) and the rest runs at 90 bpm (2.5 s = 3.75 beats)
    auto s = warp190cg::fourSeconds();
    s.warp = { { 0.0, 0.0 }, { 1.0, 3.0 }, { 2.0, 5.0 }, { 4.0, 8.0 } };
    CHECK (warptools::warpTempoFromHere (s, 1.5, 4.0, 90.0));
    CHECK (s.warp.size() == 4 && s.warp[1] == (WarpPoint { 1.0, 3.0 }) && near (s.warp[2].seconds, 1.5) && near (s.warp[2].beats, 4.0));
    CHECK (near (s.warp[3].seconds, 4.0) && near (s.warp[3].beats, 7.75) && near (warptools::segmentBpm (s.warp, 2), 90.0));
    // on a marker: that marker is "here"
    CHECK (warptools::warpTempoFromHere (s, 1.0, 4.0, 120.0) && s.warp.size() == 3 && near (s.warp[2].beats, 9.0));
    // from the start: one tempo for the whole file; at the end or outside: nothing
    CHECK (warptools::warpTempoFromHere (s, 0.0, 4.0, 60.0) && s.warp.size() == 2 && near (s.warp[1].beats, 4.0));
    CHECK (! warptools::warpTempoFromHere (s, 4.0, 4.0, 60.0) && ! warptools::warpTempoFromHere (s, -1.0, 4.0, 60.0));

    // "Warp From Here": the analysis of the audio after 1 s gave beats at 0.1 s, 0.7 s and 1.3 s (local times, 100 bpm).
    // They are laid after the marker at 1 s, which keeps its beat: local 0 is that beat
    s.warp = { { 0.0, 0.0 }, { 1.0, 3.0 }, { 2.0, 5.0 }, { 4.0, 8.0 } };
    const std::vector<WarpPoint> tail { { 0.1, 0.0 }, { 0.7, 1.0 }, { 1.3, 2.0 } };
    CHECK (warptools::warpTailFromHere (s, 1.0, 4.0, tail));
    CHECK (s.warp.size() == 5 && near (s.warp[1].beats, 3.0));
    CHECK (near (s.warp[2].seconds, 1.1) && near (s.warp[2].beats, 3.0 + 0.1 / 0.6, 1e-9));
    CHECK (near (s.warp[4].seconds, 2.3) && near (warptools::segmentBpm (s.warp, 3), 100.0, 1e-6));
    CHECK (! warptools::warpTailFromHere (s, 1.0, 4.0, { { 0.0, 0.0 } }));

    // an analysis for real: a 100 bpm performance behind 2 s of something else warped at the song's 120
    std::vector<double> beatAt;
    for (int k = 0; k < 16; ++k)
        beatAt.push_back (k * 0.6);
    const auto sample = als175a::performance (beatAt, 9.6);
    autowarp::Options o;
    o.songBpm = 100.0;
    const auto found = autowarp::analyse (sample, o);
    CHECK (found.markers.size() >= 2 && near (found.bpm, 100.0, 0.5));
    SamplerSettings whole;
    warpedit::ensureMarkers (whole, 11.6, 120.0);
    CHECK (warptools::warpTailFromHere (whole, 2.0, 11.6, found.markers));
    CHECK (near (warptools::segmentBpm (whole.warp, 0), 120.0) && near (warptools::segmentBpm (whole.warp, 1), 100.0, 0.5));
    CHECK (near (warpedit::beatAt (whole.warp, 2.0), 4.0));
}

TEST (warp190cg_warp_as_loop_and_insert_markers)
{
    using namespace lynx;
    using warp190cg::near;
    // 3.7 s at 120 bpm in 4/4: nearest power of two of bars is 2 (4 s); 7 s: 4 bars; half a second: 1 bar at least
    CHECK (warptools::loopBars (3.7, 120.0, 4.0) == 2.0 && warptools::loopBars (7.0, 120.0, 4.0) == 4.0 && warptools::loopBars (0.5, 120.0, 4.0) == 1.0);
    CHECK (warptools::loopBars (4.5, 120.0, 3.0) == 4.0); // 3/4: a bar is 1.5 s
    SamplerSettings s;
    CHECK (warptools::warpAsLoop (s, 3.7, 8.0) && s.keepPitch && s.warp.size() == 2 && near (warptools::segmentBpm (s.warp, 0), 8.0 * 60.0 / 3.7));
    CHECK (! warptools::warpAsLoop (s, 0.0, 8.0));

    // markers at the edges of a selection and on its attacks: on the beats they play on now, so nothing moves
    auto t = warp190cg::fourSeconds();
    CHECK (warptools::insertMarkers (t, { 1.0, 3.0, 1.0002, 9.0 }) == 2);
    CHECK (t.warp.size() == 4 && near (t.warp[1].beats, 2.0) && near (t.warp[2].beats, 6.0));
    CHECK (warpStretchMap (t.warp, 4.0 * 48000.0, 48000.0, 4.0 * 48000.0).empty()); // still one tempo
}

TEST (warp190cg_quantize_puts_the_attacks_on_the_grid)
{
    using namespace lynx;
    using warp190cg::near;
    // a 2 s file at 120 bpm (4 beats); bursts played late or early around the eighth notes
    const std::vector<double> at { 0.27, 0.48, 0.79, 1.02, 1.23, 1.52 };
    const std::vector<double> grid { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5 };
    const auto in = als175a::bursts (at);
    SamplerSettings s;
    warpedit::ensureMarkers (s, 2.0, 120.0);
    CHECK (warptools::quantize (s, at, 0.0, 2.0, 0.5, 1.0) == 6);
    warpedit::syncLength (s, 2.0);
    CHECK (s.warp.size() == 8 && s.stretchBeats == 4.0f);
    for (std::size_t k = 0; k < at.size(); ++k)
        CHECK (near (warpedit::beatAt (s.warp, at[k]), grid[k] * 2.0));

    // rendered through every warp mode, each burst lands on its grid line within 8 ms
    const auto played = warpedit::playedMarkers (s, 2.0);
    CHECK (played.size() == 8);
    const auto map = warpStretchMap (s.warp, 88200.0, 44100.0, 88200.0);
    CHECK (! map.empty());
    for (auto method : { StretchMethod::general, StretchMethod::beats, StretchMethod::tones, StretchMethod::texture, StretchMethod::complex })
    {
        const auto out = timeStretchMapped (in, map, method);
        CHECK (out != nullptr && out->frames() == 88200);
        if (out == nullptr || out->frames() != 88200)
            continue;
        int placed = 0;
        for (std::size_t k = 0; k < at.size(); ++k)
        {
            // (the envelope peak sits a little after the burst's start: measured on the source, as round 175a does)
            const auto lag = als175a::envelopePeak (in, std::lround (at[k] * 44100.0)) - std::lround (at[k] * 44100.0);
            const auto want = std::lround (grid[k] * 44100.0) + lag;
            placed += std::abs (als175a::envelopePeak (*out, want) - want) <= 353;
        }
        CHECK (placed == 6);
        if (placed != 6)
            std::printf ("  quantize: method %d placed %d of 6 bursts\n", (int) method, placed);
    }

    // half the amount: half the way; a range: only the attacks inside it
    SamplerSettings half;
    warpedit::ensureMarkers (half, 2.0, 120.0);
    CHECK (warptools::quantize (half, at, 0.7, 1.1, 0.5, 0.5) == 2);
    CHECK (half.warp.size() == 4 && near (warpedit::beatAt (half.warp, 0.79), 0.5 * (1.58 + 1.5)) && near (warpedit::beatAt (half.warp, 1.02), 0.5 * (2.04 + 2.0)));
    // two attacks nearest the same line: the nearer one gets it, the other stays free
    SamplerSettings two;
    warpedit::ensureMarkers (two, 2.0, 120.0);
    CHECK (warptools::quantize (two, { 0.45, 0.52 }, 0.0, 2.0, 0.5, 1.0) == 1);
    CHECK (two.warp.size() == 3 && near (two.warp[1].seconds, 0.52) && near (two.warp[1].beats, 1.0));
    // markers already inside the range move too; the ones outside hold
    warpedit::moveMarker (two, warpedit::addMarker (two, 1.5), 3.2, 2.0);
    CHECK (warptools::quantize (two, {}, 1.2, 1.8, 1.0, 1.0) == 1 && near (warpedit::beatAt (two.warp, 1.5), 3.0) && near (two.warp[1].beats, 1.0));
    CHECK (warptools::quantize (two, at, 0.0, 2.0, 0.0, 1.0) == 0);
}

TEST (warp190cg_als_keeps_signature_changes_loop_range_and_tempo_view)
{
    using namespace lynx;
    // Live's set: 3/4 at the start, 7/8 from beat 12, 4/4 from beat 26; the loop brace at beats 4…12 with the switch off;
    // the tempo envelope shown from 80 to 160 BPM
    auto xml = als175a::richSet();
    const auto swap = [&xml] (const std::string& from, const std::string& to) {
        const auto at = xml.find (from);
        CHECK (at != std::string::npos);
        if (at != std::string::npos)
            xml.replace (at, from.size(), to);
    };
    swap ("<TimeSignature><Manual Value=\"200\" /></TimeSignature>",
          "<TimeSignature><Manual Value=\"200\" /><AutomationTarget Id=\"960\" /></TimeSignature>"
          "<TempoAutomationViewBottom Value=\"80\" /><TempoAutomationViewTop Value=\"160\" />");
    swap ("</DeviceChain><AutomationEnvelopes><Envelopes /></AutomationEnvelopes></MainTrack>",
          "</DeviceChain><AutomationEnvelopes><Envelopes><AutomationEnvelope Id=\"0\"><EnvelopeTarget><PointeeId Value=\"960\" /></EnvelopeTarget>"
          "<Automation><Events><EnumEvent Id=\"0\" Time=\"-63072000\" Value=\"200\" /><EnumEvent Id=\"1\" Time=\"12\" Value=\"303\" />"
          "<EnumEvent Id=\"2\" Time=\"26\" Value=\"201\" /></Events></Automation></AutomationEnvelope></Envelopes></AutomationEnvelopes></MainTrack>");
    swap ("<Transport>" + als175a::v ("LoopOn", "true"), "<Transport>" + als175a::v ("LoopOn", "false"));
    FlpImportReport report;
    const auto bytes = als175a::bytesOf (xml);
    const auto song = importAls (gzip::compressStored (bytes.data(), bytes.size()), als175a::options (&report), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    const auto signatures = [] (const Song& s) {
        std::vector<Marker> out;
        for (const auto& m : s.markers())
            if (m.isSignature())
                out.push_back (m);
        return out;
    };
    const auto sig = signatures (*song);
    CHECK (song->beatsPerBar == 3 && sig.size() == 2);
    if (sig.size() == 2)
    {
        CHECK (sig[0].tick == 12 * ppq && sig[0].numerator == 7 && sig[0].denominator == 8 && sig[0].action == Marker::timeSignature);
        CHECK (sig[1].tick == 26 * ppq && sig[1].numerator == 4 && sig[1].denominator == 4);
    }
    // the bars follow: four bars of 3/4 (12 beats), four of 7/8 (14 beats), then 4/4
    CHECK (song->barAt (12 * ppq).number == 5 && song->barAt (12 * ppq).length == ppq * 7 / 2 && song->barAt (26 * ppq).number == 9);
    CHECK (report.unsupported.count ("Automation of a device parameter (Main)") == 0);
    // the brace keeps its place with the switch off: the range is there, playback does not loop it
    CHECK (song->hasLoop() && song->loopOff && ! song->loopPlays() && song->loopStart == 4 * ppq && song->loopEnd == 12 * ppq);
    CHECK (song->tempoViewLow == 80.0 && song->tempoViewHigh == 160.0);
    CHECK (als175a::markerNamed (*song, "Verse") != nullptr); // the locator is still a plain marker

    // out to a set and back: the same signatures, range, switch and tempo view
    const auto als = exportAls (*song, {});
    CHECK (als.xml.find ("<EnumEvent Id=\"0\" Time=\"-63072000\" Value=\"200\"") != std::string::npos);
    CHECK (als.xml.find ("Time=\"12\" Value=\"303\"") != std::string::npos && als.xml.find ("Time=\"26\" Value=\"201\"") != std::string::npos);
    CHECK (als.xml.find ("<LoopLength Value=\"8\"") != std::string::npos);
    CHECK (als.xml.find ("<TempoAutomationViewBottom Value=\"80\"") != std::string::npos);
    const auto back = importAls (als.bytes, als175a::options (nullptr));
    CHECK (back.has_value());
    if (! back)
        return;
    const auto again = signatures (*back);
    CHECK (back->beatsPerBar == 3 && again.size() == 2 && again == sig);
    CHECK (back->hasLoop() && back->loopOff && back->loopStart == 4 * ppq && back->loopEnd == 12 * ppq);
    CHECK (back->tempoViewLow == 80.0 && back->tempoViewHigh == 160.0);
    CHECK (als175a::markerNamed (*back, "Verse") != nullptr && als175a::markerNamed (*back, "Verse")->tick == 6 * ppq);

    // a signature that is not n/4 at the song start goes out as it is (it used to be written as n/4)
    Song six;
    six.mixer = Song::defaultMixer (2);
    Marker m;
    m.numerator = 6;
    m.denominator = 8;
    m.action = Marker::timeSignature;
    six.addMarker (m);
    six.loopStart = 0;
    six.loopEnd = 4 * ppq;
    const auto sixOut = exportAls (six, {});
    CHECK (sixOut.xml.find ("<Manual Value=\"302\"") != std::string::npos);
    CHECK (sixOut.xml.find ("EnumEvent") == std::string::npos); // no changes: no envelope, as before
    const auto sixBack = importAls (sixOut.bytes, als175a::options (nullptr));
    CHECK (sixBack.has_value());
    if (sixBack)
    {
        CHECK (sixBack->loopPlays() && sixBack->loopEnd == 4 * ppq); // (a range that loops, as before)
        const auto first = signatures (*sixBack);
        CHECK (first.size() == 1 && first[0].tick == 0 && first[0].numerator == 6 && first[0].denominator == 8);
    }
}

TEST (warp190cg_als_keeps_quantized_warp_markers)
{
    using namespace lynx;
    using warp190cg::near;
    // the rich set's 2 s clip, its file length recorded; quantized in LYNX, exported, read back: the same marked beats
    auto xml = als175a::richSet();
    const std::string from = "</FileRef></SampleRef></AudioClip>";
    CHECK (xml.find (from) != std::string::npos);
    if (xml.find (from) == std::string::npos)
        return;
    xml.replace (xml.find (from), from.size(), "</FileRef><DefaultDuration Value=\"96000\" /><DefaultSampleRate Value=\"48000\" /></SampleRef></AudioClip>");
    const auto bytes = als175a::bytesOf (xml);
    auto song = importAls (gzip::compressStored (bytes.data(), bytes.size()), als175a::options (nullptr), nullptr);
    CHECK (song.has_value());
    if (! song)
        return;
    int channel = -1;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::audio)
            channel = c.channel;
    CHECK (channel >= 0);
    if (channel < 0)
        return;
    auto& sampler = song->channels[(std::size_t) channel].sampler;
    CHECK (warpedit::ensureMarkers (sampler, 2.0, song->bpm) && near (sampler.stretchBeats, 8.0, 1e-5));
    // attacks at 0.55 s and 1.4 s (beats 2.2 and 5.6) go to beats 2 and 6
    CHECK (warptools::quantize (sampler, { 0.55, 1.4 }, 0.0, 2.0, 1.0, 1.0) == 2);
    warpedit::syncLength (sampler, 2.0);
    const auto markers = sampler.warp;
    CHECK (markers.size() == 4);

    AlsExportOptions options;
    options.sampleSeconds = [] (const std::string&) { return 2.0; };
    const auto als = exportAls (*song, options);
    CHECK (als.xml.find ("SecTime=\"0.55\" BeatTime=\"2\"") != std::string::npos);
    CHECK (als.xml.find ("SecTime=\"1.4\" BeatTime=\"6\"") != std::string::npos);
    auto again = als175a::options (nullptr);
    again.common.sampleSeconds = [] (const std::string&) { return 2.0; };
    again.common.fileExists = [] (const std::string&) { return true; };
    const auto back = importAls (als.bytes, again);
    CHECK (back.has_value());
    if (! back)
        return;
    int kept = 0;
    for (const auto& c : back->channels)
        if (c.sampler.warp.size() >= 4)
        {
            ++kept;
            CHECK (near (warpedit::beatAt (c.sampler.warp, 0.55), 2.0, 1e-6) && near (warpedit::beatAt (c.sampler.warp, 1.4), 6.0, 1e-6));
            CHECK (near (warpedit::beatAt (c.sampler.warp, 2.0) - warpedit::beatAt (c.sampler.warp, 0.0), 8.0, 1e-6));
        }
    CHECK (kept == 1);
}

TEST (arrange190cg_loop_switch_and_length)
{
    using namespace lynx;
    Song song;
    song.loopStart = 3840;
    song.loopEnd = 7680;
    CHECK (song.hasLoop() && song.loopPlays());
    song.loopOff = true; // the switch: the range stays, playback runs through it
    CHECK (song.hasLoop() && ! song.loopPlays());
    CHECK (arrange::scaleLoop (song, 2.0) && song.loopStart == 3840 && song.loopEnd == 11520);
    CHECK (arrange::scaleLoop (song, 0.5) && arrange::scaleLoop (song, 0.5) && song.loopEnd == 5760);
    for (int k = 0; k < 12; ++k)
        arrange::scaleLoop (song, 0.5);
    CHECK (song.loopEnd - song.loopStart == ppq / 4 && ! arrange::scaleLoop (song, 0.5)); // never under a sixteenth
    arrange::setLoop (song, 960, 1920);
    CHECK (song.loopStart == 960 && song.loopEnd == 2880 && song.loopOff);
    arrange::setLoop (song, 960, 0);
    CHECK (! song.hasLoop());
    Song none;
    CHECK (! arrange::scaleLoop (none, 2.0));
}

TEST (arrange190cg_time_commands_carry_clips_markers_and_lanes)
{
    using namespace lynx;
    auto song = warp190cg::laneSong();
    song.patterns.resize (1);
    song.playlist = { warp190cg::patternClip (0, 0, 1920), warp190cg::patternClip (1, 960, 3840), warp190cg::patternClip (2, 5760, 960) };
    Marker verse;
    verse.tick = 1440;
    verse.name = "Verse";
    song.addMarker (verse);
    Marker late;
    late.tick = 5760;
    late.name = "Late";
    song.addMarker (late);

    // Copy Time of beats 1…3 (ticks 960…2880): the clip parts, the marker and the lane's curve inside it
    const auto time = arrange::copyTime (song, 960, 2880);
    CHECK (time.length == 1920 && time.clips.size() == 2 && time.markers.size() == 1 && time.lanes.size() == 1);
    CHECK (time.clips[0].start == 0 && time.clips[0].length == 960 && time.clips[0].offset == 960); // the first clip's second beat
    CHECK (time.clips[1].start == 0 && time.clips[1].length == 1920 && time.clips[1].offset == 0);
    CHECK (time.markers[0].tick == 480);
    CHECK (time.lanes[0].points.size() == 3 && time.lanes[0].points.front().value == 0.8f && time.lanes[0].points[1].tick == 960
           && time.lanes[0].points.back().tick == 1920 && time.lanes[0].points.back().value == 0.4f);
    CHECK (arrange::copyTime (song, 960, 960).empty());

    // Duplicate Time: the range again right after itself; the long clip is cut at the seam and its rest moves on, the
    // later clip and marker move by the range's length, the locator inside is repeated (round 190cy, Live 12.4.16)
    auto dup = song;
    arrange::duplicateTime (dup, 960, 2880);
    const auto at = [] (const Song& s, int track, std::int64_t start) -> const Clip* {
        for (const auto& c : s.playlist)
            if (c.track == track && c.start == start)
                return &c;
        return nullptr;
    };
    CHECK (dup.playlist.size() == 6);
    CHECK (at (dup, 0, 0) != nullptr && at (dup, 0, 0)->length == 1920);
    CHECK (at (dup, 0, 2880) != nullptr && at (dup, 0, 2880)->length == 960 && at (dup, 0, 2880)->offset == 960);
    CHECK (at (dup, 1, 960) != nullptr && at (dup, 1, 960)->length == 1920);
    CHECK (at (dup, 1, 2880) != nullptr && at (dup, 1, 2880)->length == 1920 && at (dup, 1, 2880)->offset == 0);
    CHECK (at (dup, 1, 4800) != nullptr && at (dup, 1, 4800)->length == 1920 && at (dup, 1, 4800)->offset == 1920);
    CHECK (at (dup, 2, 7680) != nullptr);
    CHECK (dup.markers().size() == 3 && dup.markers()[0].tick == 1440 && dup.markers()[1].tick == 1440 + 1920
           && dup.markers()[1].name == "Verse" && dup.markers()[2].tick == 7680);
    // the lane: the same curve twice (0.8 at tick 960 and again at 2880), the rest later
    const auto& lane = dup.automations[0];
    CHECK (std::abs (lane.valueAt (960.0) - 0.8f) < 1e-6 && std::abs (lane.valueAt (2880.0) - 0.8f) < 1e-6);
    CHECK (std::abs (lane.valueAt (1920.0) - 0.4f) < 1e-6 && std::abs (lane.valueAt (3840.0) - 0.4f) < 1e-6);
    CHECK (std::abs (lane.valueAt (0.0) - 0.2f) < 1e-6 && lane.points.back().tick == 7680 + 1920);

    // Cut Time, then Paste Time at the song start: the range moves to the front
    auto cut = song;
    const auto piece = arrange::cutTime (cut, 960, 2880);
    CHECK (piece.clips.size() == 2 && cut.markers().size() == 1 && cut.markers()[0].tick == 5760 - 1920);
    CHECK (at (cut, 0, 0) != nullptr && at (cut, 0, 0)->length == 960);
    CHECK (at (cut, 1, 960) != nullptr && at (cut, 1, 960)->length == 1920 && at (cut, 1, 960)->offset == 1920);
    CHECK (std::abs (cut.automations[0].valueAt (960.0) - 0.4f) < 1e-6 && std::abs (cut.automations[0].valueAt (0.0) - 0.2f) < 1e-6);
    arrange::pasteTime (cut, 0, piece);
    CHECK (at (cut, 0, 0) != nullptr && at (cut, 0, 0)->offset == 960 && at (cut, 1, 0) != nullptr && at (cut, 1, 0)->length == 1920);
    CHECK (at (cut, 0, 1920) != nullptr && at (cut, 0, 1920)->length == 960 && at (cut, 2, 5760) != nullptr);
    CHECK (cut.markers().size() == 2 && cut.markers()[0].tick == 480 && cut.markers()[0].name == "Verse" && cut.markers()[1].tick == 5760);
    CHECK (std::abs (cut.automations[0].valueAt (0.0) - 0.8f) < 1e-6 && std::abs (cut.automations[0].valueAt (960.0) - 0.4f) < 1e-6);
    // pasting clips whose source is gone: left out, the time still opens
    auto gone = song;
    gone.patterns.clear();
    const auto before = gone.playlist.size();
    arrange::pasteTime (gone, 0, piece);
    CHECK (gone.playlist.size() == before && gone.playlist.front().start == 1920);

    // a track without lanes: the automation of the project is untouched
    CHECK (arrange::laneAutomations (song).size() == 1 && arrange::laneAutomations (song, 1).empty());
}

TEST (arrange190cg_crop_and_fold)
{
    using namespace lynx;
    auto song = warp190cg::laneSong();
    song.patterns.resize (1);
    song.playlist = { warp190cg::patternClip (0, 0, 3840), warp190cg::patternClip (1, 960, 960), warp190cg::patternClip (2, 5760, 960) };
    song.playlist[0].fadeIn = 240;
    song.playlist[0].fadeOut = 240;
    // Crop to ticks 960…2880: the first clip keeps that part (its cut edges lose their fades), the second is inside
    // already, the third lies outside and stays
    CHECK (arrange::cropClips (song, { 0, 1, 2, 9 }, 960, 2880) == 1);
    CHECK (song.playlist[0].start == 960 && song.playlist[0].length == 1920 && song.playlist[0].offset == 960);
    CHECK (song.playlist[0].fadeIn == 0 && song.playlist[0].fadeOut == 0);
    CHECK (song.playlist[1].start == 960 && song.playlist[1].length == 960 && song.playlist[2].start == 5760);
    CHECK (! arrange::cropped (song.playlist[2], 0, 960).has_value());

    // folding: the flag per track, rows added as needed; all at once
    CHECK (! arrange::isFolded (song, 1) && arrange::setFolded (song, { 1, 6 }, true) == 2);
    CHECK (arrange::isFolded (song, 1) && arrange::isFolded (song, 6) && song.playlistTracks.size() == 7);
    CHECK (arrange::setFolded (song, { 1 }, true) == 0 && arrange::setFolded (song, { 40 }, false) == 0 && song.playlistTracks.size() == 7);
    CHECK (arrange::setAllFolded (song, true) == 5 && arrange::setAllFolded (song, false) == 7);
}

TEST (arrange190cg_automation_follows_clips)
{
    using namespace lynx;
    auto song = warp190cg::laneSong();
    // the clip over ticks 0…1920 of track 0 moves two beats later: the rise to 0.8 goes with it (now at tick 2880), the
    // place it left is a straight line between its edge values, the curve after the landing place is as before
    CHECK (arrange::carryLaneAutomation (song, 0, 0, 1920, 1920, false) == 1);
    const auto& a = song.automations[0];
    CHECK (std::abs (a.valueAt (2880.0) - 0.8f) < 1e-6 && std::abs (a.valueAt (1920.0) - 0.2f) < 1e-6 && std::abs (a.valueAt (3840.0) - 0.4f) < 1e-6);
    CHECK (std::abs (a.valueAt (960.0) - 0.3f) < 1e-3); // (0.2 → 0.4 across the place it left)
    CHECK (std::abs (a.valueAt (5000.0) - 0.4f) < 1e-6 && a.points.back().tick == 7680);
    for (std::size_t k = 1; k < a.points.size(); ++k)
        CHECK (a.points[k].tick >= a.points[k - 1].tick);

    // a copy: the source stays
    auto copy = warp190cg::laneSong();
    CHECK (arrange::carryLaneAutomation (copy, 0, 0, 1920, 3840, true) == 1);
    CHECK (std::abs (copy.automations[0].valueAt (960.0) - 0.8f) < 1e-6 && std::abs (copy.automations[0].valueAt (4800.0) - 0.8f) < 1e-6);
    CHECK (std::abs (copy.automations[0].valueAt (3840.0) - 0.2f) < 1e-6 && std::abs (copy.automations[0].valueAt (5761.0) - 0.4f) < 1e-3);
    // another track's clip, no move, or before the song start: nothing
    auto same = warp190cg::laneSong();
    CHECK (arrange::carryLaneAutomation (same, 1, 0, 1920, 960, false) == 0 && arrange::carryLaneAutomation (same, 0, 0, 1920, 0, false) == 0);
    CHECK (arrange::carryLaneAutomation (same, 0, 0, 1920, -960, false) == 0 && same.automations[0].points == warp190cg::laneSong().automations[0].points);
}

TEST (arrange190cg_group_is_a_bus)
{
    using namespace lynx;
    auto song = warp190cg::laneSong();
    // a group the Playlist's way: a header row (no insert of its own) followed by its grouped rows
    song.playlistTracks.insert (song.playlistTracks.begin(), PlaylistTrack {});
    song.playlistTracks[0].name = "Drums";
    song.playlistTracks[1].grouped = true; // insert 1
    song.playlistTracks[2].grouped = true; // insert 2
    song.mixer[2].sends.push_back ({ 4, 0.5f }); // (a return send it keeps)
    CHECK (arrange::groupOf (song, 2).header == 0 && arrange::groupOf (song, 2).last == 2 && ! arrange::groupOf (song, 3).valid());
    CHECK (arrange::insertOf (song, 1) == 1 && arrange::insertOf (song, 0) == -1 && arrange::groupBus (song, 1) == -1);

    const auto bus = arrange::makeGroupBus (song, 1);
    CHECK (bus == 5 && (int) song.mixer.size() == 6 && song.mixer[5].name == "Drums");
    CHECK (song.playlistTracks[0].mode == PlaylistTrackMode::audio && song.playlistTracks[0].mixerTrack == 5);
    // the members play only into the bus (and their other sends), the bus into the Master
    CHECK (! song.mixer[1].toMaster && song.mixer[1].sends.size() == 1 && song.mixer[1].sends[0].target == 5);
    CHECK (! song.mixer[2].toMaster && song.mixer[2].sends.size() == 2 && song.mixer[2].sends[0].target == 5 && song.mixer[2].sends[1].target == 4);
    CHECK (song.mixer[5].toMaster && song.mixer[3].toMaster);
    CHECK (arrange::groupBus (song, 2) == 5 && arrange::groupBus (song, 0) == 5);
    // again: nothing new
    CHECK (arrange::makeGroupBus (song, 0) == 5 && (int) song.mixer.size() == 6 && song.mixer[1].sends.size() == 1);

    // a member leaves: back to the Master
    CHECK (arrange::leaveGroupBus (song, 1, 5) && song.mixer[1].toMaster && song.mixer[1].sends.empty());
    CHECK (! arrange::leaveGroupBus (song, 1, 5) && arrange::groupBus (song, 0) == -1);

    // a group whose members have no insert: no bus; a track outside a group: none
    Song plain;
    plain.playlistTracks.resize (3);
    plain.playlistTracks[1].grouped = true;
    CHECK (arrange::makeGroupBus (plain, 0) == -1 && plain.mixer.empty());
    CHECK (arrange::makeGroupBus (song, 3) == -1);
}

// --- Round 190cy: what the live check of Live 12.4.16 (190cw) showed ------------------------------------------------

TEST (warp190cy_halve_and_double_scale_the_clip_with_the_markers)
{
    using namespace lynx;
    using warp190cg::near;
    // Live: ÷2 on a 2-bar loop at 120 BPM: segment 120 -> 60 BPM, End 3.1.1 -> 2.1.1, Loop Length 2.0.0 -> 1.0.0
    auto s = warp190cg::fourSeconds(); // 8 beats at 120
    Clip c;
    c.kind = ClipKind::audio;
    c.length = 8 * ppq; // two bars
    c.offset = ppq;     // starts one beat into the audio
    c.fadeOut = 6 * ppq;
    CHECK (warptools::scaleTempo (s, 0.5));
    warpedit::syncLength (s, 4.0);
    warptools::scaleClip (c, 0.5);
    CHECK (near (warptools::segmentBpm (s.warp, 0), 60.0) && s.stretchBeats == 4.0f);
    CHECK (c.length == 4 * ppq && c.offset == ppq / 2 && c.offsetFraction == 0.0f); // one bar: the same audio
    CHECK (c.fadeOut == 4 * ppq); // (a fade stays inside the clip)
    CHECK (warptools::scaleTempo (s, 2.0));
    warptools::scaleClip (c, 2.0);
    CHECK (c.length == 8 * ppq && c.offset == ppq);
    // an offset with a sub-tick part keeps its exact place; nothing for a factor that is not one
    c.offset = 3;
    c.offsetFraction = 0.25f;
    warptools::scaleClip (c, 0.5);
    CHECK (c.offset == 2 && std::abs (c.offsetFraction - (-0.375f)) < 1.0e-6f);
    const auto before = c;
    warptools::scaleClip (c, 0.0);
    CHECK (c == before);
    Clip tiny;
    tiny.length = 1;
    warptools::scaleClip (tiny, 0.5);
    CHECK (tiny.length == 1);
}

TEST (warp190cy_marker_drag_stretches_and_shift_drag_slides)
{
    using namespace lynx;
    using warp190cg::near;
    auto s = warp190cg::fourSeconds(); // 8 beats over 4 s
    const auto m = warpedit::addMarker (s, 2.0); // on beat 4
    CHECK (m == 1 && near (s.warp[1].beats, 4.0));
    const auto before = s.warp;
    // plain drag: the pointer at 3 s, where beat 6 played: the marker's audio (2 s) now plays on beat 6; the audio
    // before it stretches (6 beats over 2 s = 180 BPM) and the audio after it squeezes (2 beats over 2 s = 60 BPM)
    const auto beat = warptools::stretchDragBeat (before, 3.0);
    CHECK (near (beat, 6.0));
    warpedit::moveMarker (s, m, beat, 4.0);
    CHECK (near (s.warp[1].seconds, 2.0) && near (s.warp[1].beats, 6.0));
    CHECK (near (warptools::segmentBpm (s.warp, 0), 180.0) && near (warptools::segmentBpm (s.warp, 1), 60.0));
    CHECK (near (s.warp.back().beats, 8.0)); // the end stays
    // Shift + drag: the marker keeps beat 6 and is tied to the audio at 1 s instead (the audio slides under it)
    CHECK (warptools::slideMarker (s, m, 1.0));
    CHECK (near (s.warp[1].seconds, 1.0) && near (s.warp[1].beats, 6.0));
    // kept between its neighbours; not the start marker
    CHECK (warptools::slideMarker (s, m, 9.0) && s.warp[1].seconds < s.warp[2].seconds);
    CHECK (! warptools::slideMarker (s, 0, 1.0) && ! warptools::slideMarker (s, 7, 1.0));
}

TEST (arrange190cy_duplicate_time_repeats_the_locators_in_the_range)
{
    using namespace lynx;
    // Live: Duplicate Time on bars 3-5 holding locator L12 (bar 4): a second L12 two bars later; the 3/4 change after
    // the range moves right
    auto song = warp190cg::laneSong();
    song.patterns.resize (1);
    const std::int64_t bar = 4 * ppq;
    Marker l12;
    l12.tick = 3 * bar;
    l12.name = "L12";
    song.addMarker (l12);
    Marker sig;
    sig.tick = 6 * bar;
    sig.numerator = 3;
    sig.denominator = 4;
    sig.action = Marker::timeSignature;
    song.addMarker (sig);
    Marker sigInside = sig;
    sigInside.tick = 2 * bar + ppq;
    song.addMarker (sigInside);
    Marker loop;
    loop.tick = 2 * bar + 2 * ppq;
    loop.name = "Loop";
    loop.action = Marker::markerLoop;
    song.addMarker (loop);
    arrange::duplicateTime (song, 2 * bar, 4 * bar);
    std::vector<std::pair<std::int64_t, std::string>> got;
    for (const auto& m : song.markers())
        got.push_back ({ m.tick, m.isSignature() ? "sig" : m.name });
    const std::vector<std::pair<std::int64_t, std::string>> want {
        { 2 * bar + ppq, "sig" }, { 2 * bar + 2 * ppq, "Loop" }, { 3 * bar, "L12" }, { 5 * bar, "L12" }, { 8 * bar, "sig" } };
    CHECK (got == want);
}
