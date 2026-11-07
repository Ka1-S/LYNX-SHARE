// --- Video tracks and ADR (round 174d, docs/analise/REA_rodada174d.md)

#include "core/VideoTime.h"

TEST (video174d_rates)
{
    using namespace lynx::vtime;
    CHECK (rateAt (0).num == 24000 && rateAt (0).den == 1001 && rateAt (0).nominal() == 24);
    CHECK (rateAt (3).drop && rateAt (3).nominal() == 30 && ! rateAt (4).drop);
    for (int i = 0; i < rateCount; ++i)
    {
        CHECK (indexOf (rateAt (i)) == i);
        CHECK (rateFromId (rateId (rateAt (i))) == rateAt (i));
    }
    CHECK (rateName (rateAt (3)) == "29.97 fps drop frame");
    CHECK (rateName (rateAt (0)) == "23.976 fps" && rateName (rateAt (2)) == "25 fps" && rateName (rateAt (9)) == "59.94 fps");
    // a file's measured rate: the non-drop listed rate
    CHECK (nearestRate (29.97002997) == rateAt (4));
    CHECK (nearestRate (23.976) == rateAt (0));
    CHECK (nearestRate (25.0) == rateAt (2));
    CHECK (nearestRate (12.5)->fps() == 12.5);
    CHECK (! nearestRate (0.0));
}

TEST (video174d_drop_frame_timecode)
{
    using namespace lynx::vtime;
    const auto df = rateAt (3), ndf = rateAt (4), df60 = rateAt (8);
    // the minute boundaries: 00:00:59;29 → 00:01:00;02 (labels ;00 and ;01 skipped), but 00:10:00;00 exists
    CHECK (format (toTimecode (1799, df), df) == "00:00:59;29");
    CHECK (format (toTimecode (1800, df), df) == "00:01:00;02");
    CHECK (format (toTimecode (17982, df), df) == "00:10:00;00");
    CHECK (format (toTimecode (17981, df), df) == "00:09:59;29");
    // one hour of 29.97 DF = 107892 frames = 01:00:00;00 (drop frame keeps the clock)
    CHECK (format (toTimecode (107892, df), df) == "01:00:00;00");
    CHECK (format (toTimecode (107892, ndf), ndf) == "00:59:56:12");
    // 59.94 DF drops four labels
    CHECK (format (toTimecode (3600, df60), df60) == "00:01:00;04");
    // round trip over many frames
    bool ok = true;
    for (std::int64_t f = 0; f < 200000; f += 7)
        for (const auto& r : { df, ndf, df60, rateAt (2), rateAt (0) })
            ok = ok && toFrame (toTimecode (f, r), r) == f;
    CHECK (ok);
    // frame boundaries: k / fps is frame k
    CHECK (frameAt (frameStart (12345, ndf), ndf) == 12345);
    CHECK (frameAt (frameStart (12345, ndf) - 1e-4, ndf) == 12344);
    CHECK (format (-1.0, rateAt (2)) == "-00:00:01:00");
}

TEST (video174d_timecode_parse)
{
    using namespace lynx::vtime;
    const auto r25 = rateAt (2), df = rateAt (3);
    CHECK (parse ("01:02:03:04", r25) == (Timecode { 1, 2, 3, 4, false }));
    CHECK (parse ("1:2:3:4", r25) == (Timecode { 1, 2, 3, 4, false }));
    CHECK (parse ("1020304", r25) == (Timecode { 1, 2, 3, 4, false }));
    CHECK (parse ("3:04", r25) == (Timecode { 0, 0, 3, 4, false }));
    CHECK (parse (" 10.00.00.00 ", r25) == (Timecode { 10, 0, 0, 0, false }));
    CHECK (! parse ("00:00:00:25", r25)); // 25 fps: frames 0…24
    CHECK (! parse ("00:61:00:00", r25));
    CHECK (! parse ("1:2:x", r25));
    CHECK (! parse ("", r25));
    // a dropped label goes to the minute's first frame
    CHECK (parse ("00:01:00;00", df) == (Timecode { 0, 1, 0, 2, false }));
    const auto s = parseSeconds ("00:00:01:00", r25);
    CHECK (s && std::abs (*s - 1.0) < 1e-9);
    const auto h = parseSeconds ("01:00:00;00", df);
    CHECK (h && std::abs (*h - 107892.0 * 1001.0 / 30000.0) < 1e-9);
}

#include "core/VideoTrack.h"

TEST (video174d_track_showing_and_edits)
{
    using namespace lynx;
    VideoTrack t;
    CHECK (videotrack::addClip (t, "a.mp4", 10.0, 30.0, 1) == 0); // 10…40
    CHECK (videotrack::addClip (t, "b.mp4", 50.0, 20.0, 2) == 1); // 50…70
    CHECK (! videotrack::showing (t, 5.0));
    const auto s = videotrack::showing (t, 15.0);
    CHECK (s && s->clip == 0 && std::abs (s->fileSeconds - 5.0) < 1e-9);
    CHECK (! videotrack::showing (t, 45.0));
    CHECK (videotrack::showing (t, 69.9)->clip == 1);
    t.muted = true;
    CHECK (! videotrack::showing (t, 15.0));
    t.muted = false;
    // trimming the head moves the file offset with it, never before the file's start
    auto c = t.clips[0];
    videotrack::trimStart (c, 12.0, 0.04);
    CHECK (std::abs (c.start - 12.0) < 1e-9 && std::abs (c.offset - 2.0) < 1e-9 && std::abs (c.end() - 40.0) < 1e-9);
    videotrack::trimStart (c, 0.0, 0.04);
    CHECK (std::abs (c.start - 10.0) < 1e-9 && std::abs (c.offset) < 1e-9);
    videotrack::trimEnd (c, 100.0, 0.04); // the file has 30 s
    CHECK (std::abs (c.end() - 40.0) < 1e-9);
    videotrack::trimEnd (c, 20.0, 0.04);
    CHECK (std::abs (c.length - 10.0) < 1e-9);
    // a clip moved over another cuts it (the part after stays, its offset follows)
    const auto moved = videotrack::moveClip (t, 1, 30.0); // b: 30…50 over a's 30…40
    CHECK (t.clips.size() == 2 && moved == 1);
    CHECK (std::abs (t.clips[0].end() - 30.0) < 1e-9 && std::abs (t.clips[1].start - 30.0) < 1e-9);
    // a clip dropped inside another splits it in three
    VideoTrack u;
    videotrack::addClip (u, "a.mp4", 0.0, 60.0, 1);
    videotrack::addClip (u, "b.mp4", 20.0, 10.0, 2);
    CHECK (u.clips.size() == 3 && u.clips[1].path == "b.mp4" && std::abs (u.clips[2].start - 30.0) < 1e-9 && std::abs (u.clips[2].offset - 30.0) < 1e-9);
    CHECK (u.clips[2].uid != 1 && u.clips[2].uid != 2);
    // split
    CHECK (videotrack::splitClip (u, 0, 10.0, 9));
    CHECK (u.clips.size() == 4 && std::abs (u.clips[1].offset - 10.0) < 1e-9 && u.clips[1].uid == 9);
    CHECK (! videotrack::splitClip (u, 0, 0.0, 10));
    CHECK (std::abs (videotrack::videoEnd ({ t, u }) - 60.0) < 1e-9);
}

TEST (video174d_timecode_clock_and_tempo)
{
    using namespace lynx;
    VideoSettings v;
    v.rate = vtime::rateAt (2); // 25
    v.startTimecode = 3600.0;
    CHECK (vtime::format (videotrack::timecodeOf (v, 1.5), v.rate) == "01:00:01:12");
    CHECK (std::abs (videotrack::snapToFrame (v, 1.019) - 1.0) < 1e-9);
    CHECK (std::abs (videotrack::snapToFrame (v, 1.021) - 1.04) < 1e-9);
    // ticks ↔ seconds: the inverse of Song::secondsForTicks, with and without tempo automation
    auto song = Song::makeDefault();
    song.bpm = 120.0;
    CHECK (std::abs (videotrack::ticksForSeconds (song, 1.0) - 2.0 * ppq) < 1e-6);
    for (const auto ticks : { (std::int64_t) 0, (std::int64_t) ppq * 7 + 13, (std::int64_t) ppq * 333 })
        CHECK (std::abs (videotrack::ticksForSeconds (song, song.secondsForTicks (ticks)) - (double) ticks) < 1e-3);
}

TEST (video174d_tempo_map)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.bpm = 100.0;
    const videotrack::TempoMap flat (song, 60.0);
    CHECK (std::abs (flat.ticks (3.0) - 5.0 * ppq) < 1e-6 && std::abs (flat.seconds (5.0 * ppq) - 3.0) < 1e-9);
    // with tempo automation: the map agrees with Song::secondsForTicks and inverts itself
    song.mode = PlayMode::song;
    Automation a;
    a.target.kind = AutomationTargetKind::tempo;
    a.points = { { 0, 0.2f }, { (std::int64_t) ppq * 32, 0.8f } };
    song.automations.push_back (a);
    Clip c;
    c.kind = ClipKind::automation;
    c.automation = 0;
    c.start = 0;
    c.length = (std::int64_t) ppq * 32;
    song.playlist.push_back (c);
    const videotrack::TempoMap map (song, 120.0);
    bool ok = true;
    for (const auto ticks : { (std::int64_t) 0, (std::int64_t) ppq * 3, (std::int64_t) ppq * 17 + 5, (std::int64_t) ppq * 40 })
    {
        const auto s = song.secondsForTicks (ticks);
        ok = ok && std::abs (map.seconds ((double) ticks) - s) < 1e-6 && std::abs (map.ticks (s) - (double) ticks) < 1e-3;
        ok = ok && std::abs (videotrack::ticksForSeconds (song, s) - (double) ticks) < 1e-3;
    }
    CHECK (ok);
}

#include "core/Adr.h"
#include "core/DialogueAlign.h"

TEST (video174d_adr_cues_names_csv)
{
    using namespace lynx;
    std::vector<AdrCue> cues;
    CHECK (adr::nextId (cues) == "1");
    AdrCue a;
    a.id = "SC1-09";
    a.in = 12.0;
    a.out = 14.52;
    a.character = "Dr. Silva";
    a.line = "Say \"hello\", then go";
    a.notes = "softer";
    cues.push_back (a);
    CHECK (adr::nextId (cues) == "SC1-10");
    CHECK (adr::takeName (a, 3) == "SC1-09_Dr.-Silva_T3");
    CHECK (adr::nextTakeNumber (a) == 1);
    a.takes.push_back ({ 4, "x", 0, 0, false, "", false });
    CHECK (adr::nextTakeNumber (a) == 5);
    AdrCue b;
    b.id = "101";
    b.in = 2.0;
    b.out = 3.0;
    b.status = AdrCue::Status::approved;
    cues.push_back (b);
    adr::sortByTime (cues);
    CHECK (cues[0].id == "101" && adr::cueAt (cues, 13.0) == 1 && adr::cueAt (cues, 5.0) == -1);
    // CSV round trip at 25 fps from 01:00:00:00
    const auto rate = vtime::rateAt (2);
    const auto csv = adr::toCsv (cues, rate, 3600.0);
    CHECK (csv.find ("101,01:00:02:00,01:00:03:00,,,,Approved") != std::string::npos);
    CHECK (csv.find ("\"Say \"\"hello\"\", then go\"") != std::string::npos);
    const auto back = adr::fromCsv (csv, rate, 3600.0);
    CHECK (back.error.empty() && back.cues.size() == 2 && back.skipped == 0);
    CHECK (back.cues[1].line == a.line && back.cues[1].character == "Dr. Silva" && std::abs (back.cues[1].in - 12.0) < 1e-6
           && std::abs (back.cues[1].out - 14.52) < 1e-6 && back.cues[0].status == AdrCue::Status::approved);
    // other spreadsheets: semicolons, a BOM, other column names and order, seconds, a row without In
    const auto other = adr::fromCsv ("\xEF\xBB\xBFRole;Dialogue;Start;End\r\nAna;Oi;5,5;7\r\nBia;Tchau;;\r\nCaio;Hum;9;8\r\n", rate, 0.0);
    CHECK (other.error.empty() && other.cues.size() == 2 && other.skipped == 1);
    CHECK (other.cues[0].character == "Ana" && std::abs (other.cues[0].in - 5.5) < 1e-9 && std::abs (other.cues[0].out - 7.0) < 1e-9
           && other.cues[0].id == "1" && other.cues[1].id == "2");
    CHECK (std::abs (other.cues[1].out - 11.0) < 1e-9); // (Out before In: In + 2 s)
    CHECK (! adr::fromCsv ("a,b\n1,2\n", rate, 0.0).error.empty());
}

TEST (video174d_adr_signals_and_comp)
{
    using namespace lynx;
    AdrCue c;
    c.in = 10.0;
    c.out = 12.0;
    AdrSettings s;
    const auto beeps = adr::beepTimes (c.in);
    CHECK (beeps.size() == 3 && beeps[0] == 7.0 && beeps[2] == 9.0);
    const auto tone = adr::renderBeep (48000.0, 1000.0);
    CHECK (tone.size() == 1920 && std::abs (tone.front()) < 1e-6 && std::abs (tone[480 + 12]) > 0.2f);
    CHECK (! adr::signalsAt (c, s, 7.9, 0.04).streamer);
    CHECK (std::abs (*adr::signalsAt (c, s, 9.0, 0.04).streamer - 0.5) < 1e-9);
    CHECK (adr::signalsAt (c, s, 10.02, 0.04).punch && ! adr::signalsAt (c, s, 10.1, 0.04).punch);
    CHECK (adr::signalsAt (c, s, 11.0, 0.04).inLine && ! adr::signalsAt (c, s, 12.0, 0.04).inLine);
    // comp: take 1 plays, take 3 from 10.5 to 11, take 2 from 11.5 on
    c.selected = 1;
    adr::setCompRange (c, 10.5, 11.0, 3);
    adr::setCompRange (c, 11.5, 13.0, 2);
    auto spans = adr::compSpans (c);
    CHECK (spans.size() == 4 && spans[0].take == 1 && spans[1].take == 3 && spans[2].take == 1 && spans[3].take == 2 && spans[3].to == 12.0);
    // a range over the same take's neighbour merges; over another cuts it
    adr::setCompRange (c, 10.8, 11.6, 3);
    spans = adr::compSpans (c);
    CHECK (spans.size() == 3 && spans[1].take == 3 && std::abs (spans[1].from - 10.5) < 1e-9 && std::abs (spans[1].to - 11.6) < 1e-9
           && spans[2].take == 2 && std::abs (spans[2].from - 11.6) < 1e-9);
}

TEST (video174d_dialogue_alignment)
{
    using namespace lynx;
    // a "line": syllables (harmonic bursts of different pitches and vowel colours) with pauses; the take says the same
    // syllables 0.15 s later, the middle ones slower and the pauses changed
    constexpr double sr = 16000.0;
    struct Syl { double start, length, f0; };
    const std::vector<Syl> guideSyl { { 0.20, 0.18, 140 }, { 0.45, 0.12, 220 }, { 0.62, 0.25, 170 }, { 1.05, 0.15, 260 }, { 1.30, 0.20, 120 } };
    const std::vector<Syl> takeSyl { { 0.35, 0.18, 140 }, { 0.62, 0.16, 220 }, { 0.86, 0.32, 170 }, { 1.30, 0.15, 260 }, { 1.52, 0.20, 120 } };
    auto render = [&] (const std::vector<Syl>& syl, double total) {
        std::vector<float> x ((std::size_t) (total * sr), 0.0f);
        std::uint32_t noise = 1;
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            noise = noise * 1664525u + 1013904223u;
            x[i] = 0.002f * ((float) (noise >> 9) / 8388608.0f - 1.0f); // room noise
        }
        for (const auto& s : syl)
            for (auto i = (std::size_t) (s.start * sr); i < (std::size_t) ((s.start + s.length) * sr) && i < x.size(); ++i)
            {
                // (speech glides: the pitch rises 40 % over the syllable, so its inside has a shape to follow)
                const auto t = (double) i / sr - s.start;
                const auto env = std::sin (3.14159265 * t / s.length);
                const auto phase = 2 * 3.14159265 * s.f0 * (t + 0.2 * t * t / s.length);
                double v = 0.0;
                for (int h = 1; h <= 12; ++h)
                    v += std::sin (phase * h) / h * (h == (int) (s.f0 / 50) % 7 + 2 ? 3.0 : 1.0);
                x[i] += (float) (0.15 * env * v);
            }
        return x;
    };
    const auto guide = render (guideSyl, 1.7), take = render (takeSyl, 1.9);
    const auto map = align::align (guide, take, sr);
    CHECK (! map.empty());
    double worst = 0.0;
    for (std::size_t k = 0; k < guideSyl.size(); ++k)
        for (const auto f : { 0.25, 0.5, 0.75 })
        {
            const auto g = guideSyl[k].start + f * guideSyl[k].length;
            const auto want = takeSyl[k].start + f * takeSyl[k].length;
            worst = std::max (worst, std::abs (map.takeAt (g) - want));
            if (std::getenv ("LYNX_ALIGN_DEBUG"))
                std::printf ("    syllable %d at %.2f: %.1f ms\n", (int) k, f, (map.takeAt (g) - want) * 1000.0);
        }
    std::printf ("  dialogue alignment: worst syllable error %.1f ms\n", worst * 1000.0);
    CHECK (worst < 0.025);
    // the warped take: its syllables sit where the guide's are (the energy envelope follows the guide's)
    const auto warped = align::warp (take, sr, map, guide.size());
    CHECK (warped.size() == guide.size());
    auto energy = [&] (const std::vector<float>& x, double from, double to) {
        double e = 0.0;
        for (auto i = (std::size_t) (from * sr); i < (std::size_t) (to * sr) && i < x.size(); ++i)
            e += (double) x[i] * x[i];
        return e;
    };
    bool onTime = true;
    for (const auto& s : guideSyl)
        onTime = onTime && energy (warped, s.start + 0.02, s.start + s.length - 0.02) > 20.0 * energy (warped, s.start - 0.06, s.start - 0.02) + 1e-9;
    CHECK (onTime);
}

TEST (video174d_player_clock_follows_the_tempo)
{
    // FL's Video Player clock is the song's real time (round 174d A3): 120 BPM for 4 bars, then 60 BPM
    using namespace lynx;
    Song song;
    song.bpm = 120.0;
    song.mode = PlayMode::song;
    const auto bar = (std::int64_t) ppq * 4;
    Automation tempo;
    tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    tempo.points = { { 0, valueFromTempo (120.0), 0.0f }, { bar * 4 - 1, valueFromTempo (120.0), 0.0f },
                     { bar * 4, valueFromTempo (60.0), 0.0f }, { bar * 8, valueFromTempo (60.0), 0.0f } };
    song.automations.push_back (tempo);
    Clip at;
    at.kind = ClipKind::automation;
    at.automation = 0;
    at.start = 0;
    at.length = bar * 8;
    song.playlist.push_back (at);
    video::publishTempo (song);

    auto inst = video::makeInstrument();
    auto* player = video::playerOf (inst.get());
    ChannelDef def;
    def.instrument = InstrumentKind::videoplayer;
    def.pluginState = video::write (video::Settings {});
    inst->prepare (48000.0);
    inst->configure (def, song.bpm);
    std::vector<float> l (64, 0.0f), r (64, 0.0f);
    const auto timeAt = [&] (std::int64_t ticks) {
        inst->setSongPosition ((double) ticks);
        inst->render (l.data(), r.data(), 64, 1.0f, 1.0f);
        return player->currentTime();
    };
    CHECK (std::abs (timeAt (bar * 2) - 4.0) < 1e-3);                         // 8 beats at 120
    CHECK (std::abs (timeAt (bar * 5) - song.secondsForTicks (bar * 5)) < 1e-3);
    CHECK (std::abs (timeAt (bar * 5) - 12.0) < 0.05);                        // 8 s + 4 beats at 60 (not 10 s)
    // no tempo automation: the song's tempo
    video::publishTempo (Song {});
    inst->configure (def, 120.0);
    CHECK (std::abs (timeAt (bar * 5) - 10.0) < 1e-3);
}

TEST (video174d_song_length_counts_the_picture)
{
    // a project with only its picture so far plays (and its playhead runs) to the picture's end, not one bar
    using namespace lynx;
    Song song;
    song.bpm = 120.0;
    song.mode = PlayMode::song;
    song.patterns.emplace_back();
    const auto bar = (std::int64_t) ppq * 4;
    CHECK (song.songLength() == bar);
    VideoTrack vt;
    videotrack::addClip (vt, "picture.mp4", 1.0, 12.0, 1);
    song.videoTracks.push_back (vt);
    CHECK (song.songLength() == 26 * ppq); // 13 s at 120 BPM = 26 beats (the exact end, like FL's clips)
    Clip c;
    c.pattern = 0;
    c.start = 0;
    c.length = 40 * ppq;
    song.playlist.push_back (c);
    CHECK (song.songLength() == 40 * ppq);
}
