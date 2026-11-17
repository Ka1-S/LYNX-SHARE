// Round 190kx: ARA 2 on audio clips (core/AraLink.h, core/AraRegions.h). The model mapping is checked here on every
// platform; the hosting itself (engine/AraHost.cpp, Melodyne) in the app's --aratest and the round's Windows sessions.

#include "core/AraRegions.h"
#include "core/FlClipMenu.h"
#include "core/NewtoneFx.h"

namespace ara190kx
{
lynx::Song songWithClips()
{
    lynx::Song s;
    s.bpm = 120.0; // (0.5 s a beat: 960 ticks = 0.5 s)
    lynx::ChannelDef audio;
    audio.instrument = lynx::InstrumentKind::audioClip;
    audio.samplePath = "vocal.wav";
    audio.uid = 77;
    audio.ara = { "VST3-Melodyne-1234", "Melodyne" };
    s.channels.push_back (audio);
    lynx::ChannelDef plain = audio;
    plain.uid = 78;
    plain.ara = {};
    s.channels.push_back (plain);
    auto clip = [&s] (int channel, std::int64_t start, std::int64_t length, std::int64_t offset, std::uint64_t uid) -> lynx::Clip& {
        lynx::Clip c;
        c.kind = lynx::ClipKind::audio;
        c.channel = channel;
        c.start = start;
        c.length = length;
        c.offset = offset;
        c.uid = uid;
        s.playlist.push_back (c);
        return s.playlist.back();
    };
    clip (0, 0, 1920, 0, 1);          // 0 s, 1 s long
    clip (0, 3840, 960, 960, 2);      // 2 s, 0.5 s long, from 0.5 s of the sample
    clip (1, 0, 1920, 0, 3);          // the other channel
    clip (0, 7680, 960, 0, 4).muted = true;
    auto& stretched = clip (0, 9600, 1920, 0, 5);
    stretched.stretch = 2.0;          // plays 1 s of audio over 2 s... (1920 ticks = 1 s of timeline = 0.5 s of audio)
    auto& grid = clip (0, 0, 960, 0, 6);
    grid.gridSlot = 0;                // a clip of the clip grid: not on the timeline
    return s;
}
} // namespace ara190kx

TEST (ara190kx_regions_follow_the_clips)
{
    using namespace lynx;
    auto s = ara190kx::songWithClips();
    CHECK (ara::isAraChannel (s, 0) && ! ara::isAraChannel (s, 1) && ! ara::isAraChannel (s, 7));
    CHECK (ara::anyAraChannel (s));
    const auto r = ara::regionsOf (s, 0);
    // the muted clip and the clip grid's clip are left out; the other channel's clip too
    CHECK (r.size() == 3);
    CHECK (r[0].clipUid == 1 && std::abs (r[0].playStart) < 1e-12 && std::abs (r[0].playDuration - 1.0) < 1e-12
           && std::abs (r[0].sourceStart) < 1e-12 && std::abs (r[0].sourceDuration - 1.0) < 1e-12 && ! r[0].stretched());
    CHECK (r[1].clipUid == 2 && std::abs (r[1].playStart - 2.0) < 1e-12 && std::abs (r[1].sourceStart - 0.5) < 1e-12
           && std::abs (r[1].playDuration - 0.5) < 1e-12);
    // a stretched clip: twice as long on the timeline as the audio it plays (ARA time-stretching)
    CHECK (r[2].clipUid == 5 && r[2].stretched() && std::abs (r[2].playDuration - 1.0) < 1e-12 && std::abs (r[2].sourceDuration - 0.5) < 1e-12);
    // a muted Playlist track mutes its clips' regions too
    s.playlistTracks.resize (1);
    s.playlistTracks[0].muted = true;
    CHECK (ara::regionsOf (s, 0).empty());
}

TEST (ara190kx_regions_end_with_the_sample)
{
    using namespace lynx;
    auto s = ara190kx::songWithClips();
    // a 1.25 s sample: the second clip (0.5 s from 0.5 s) fits; a clip from 1.5 s plays nothing
    auto r = ara::regionsOf (s, 0, 1.25);
    CHECK (r.size() == 3 && std::abs (r[0].sourceDuration - 1.0) < 1e-12);
    s.playlist[0].length = 3840; // 2 s of a 1.25 s sample: the region stops with the sample
    r = ara::regionsOf (s, 0, 1.25);
    CHECK (std::abs (r[0].sourceDuration - 1.25) < 1e-12 && std::abs (r[0].playDuration - 1.25) < 1e-12);
    s.playlist[1].offset = 3840; // starts 2 s into a 1.25 s sample
    r = ara::regionsOf (s, 0, 1.25);
    CHECK (std::none_of (r.begin(), r.end(), [] (const ara::Region& x) { return x.clipUid == 2; }));
}

TEST (ara190kx_ids_tempo_and_menu)
{
    using namespace lynx;
    const auto s = ara190kx::songWithClips();
    CHECK (ara::sourceId (s.channels[0]) == "lynx-ch-77" && ara::modificationId (s.channels[0]) == "lynx-mod-77");
    const auto t = ara::tempoEntries (s);
    CHECK (t.size() == 2 && t[1].quarters == 4.0 && std::abs (t[1].seconds - 2.0) < 1e-12);
    CHECK (std::abs (ara::secondsPerTick (s) - 0.5 / 960.0) < 1e-15);
    // the clip menu's LYNX extras list "Edit in ARA plugin" (FL 2026 has no ARA: not one of FL's items)
    const auto extras = flclipmenu::lynxExtras();
    CHECK (std::find (extras.begin(), extras.end(), std::string ("Edit in ARA plugin")) != extras.end());
    for (const auto& e : flclipmenu::fl())
        CHECK (e.text != "Edit in ARA plugin");
    // a channel's ARA link takes part in its equality (an edit into / out of ARA rebuilds its instrument)
    auto a = s.channels[0], b = s.channels[0];
    b.ara = {};
    CHECK (! (a == b));
}

// Round 190kx: Newtone's "Separate close notes" analysis (core/NewtoneFx.h AnalyseOptions::fineSplit) against the
// round's Melodyne 5 reference captures (Melodic detection of the same synthetic lines: every note of its own)
namespace ara190kx
{
// a sung line (harmonics through three vowel formants, 15 ms fades): (MIDI note or 0 for a rest, seconds), vibrato depth
// in cents at 5.5 Hz
lynx::SampleData sung (const std::vector<std::pair<double, double>>& spec, double vibratoCents = 0.0)
{
    lynx::SampleData s;
    s.sampleRate = 48000.0;
    double ph = 0.0;
    for (const auto& [midi, seconds] : spec)
    {
        const auto n = (int) (seconds * 48000.0);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (midi > 0.0)
            {
                const auto t = i / 48000.0;
                const auto f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) * std::pow (2.0, vibratoCents * std::sin (2.0 * M_PI * 5.5 * t) / 1200.0);
                ph += 2.0 * M_PI * f0 / 48000.0;
                for (int k = 1; k * f0 < 8000.0; ++k)
                {
                    const auto f = k * f0;
                    double g = 0.05;
                    for (const auto [F, B] : { std::pair { 700.0, 130.0 }, { 1220.0, 150.0 }, { 2600.0, 200.0 } })
                        g += 1.0 / std::sqrt (1.0 + std::pow ((f - F) / B, 2.0));
                    v += (float) (0.12 * g / k * std::sin (k * ph));
                }
                v *= (float) std::min ({ 1.0, i / 720.0, (n - i) / 720.0 });
            }
            s.left.push_back (v);
        }
    }
    return s;
}

int voicedNotes (const lynx::newtonefx::Track& t)
{
    return (int) std::count_if (t.pitches.begin(), t.pitches.end(), [] (const lynx::newtonefx::Note& p) { return p.voiced(); });
}
} // namespace ara190kx

TEST (melodyne190kx_newtone_fine_split_separates_close_notes)
{
    using namespace lynx;
    newtonefx::AnalyseOptions fine;
    fine.fineSplit = true;
    // 45 cents apart (E +40, F -15) with a 20-cent vibrato: FL's analysis joins them into one note (Center then puts the
    // E on F); the fine analysis keeps five notes, as Melodyne does
    const auto offtune = ara190kx::sung ({ { 60.3, 0.5 }, { 61.75, 0.5 }, { 64.4, 0.5 }, { 64.85, 0.5 }, { 67.2, 1.0 } }, 20.0);
    CHECK (ara190kx::voicedNotes (newtonefx::analyseNotes (offtune)) == 4);
    const auto split = newtonefx::analyseNotes (offtune, fine);
    CHECK (ara190kx::voicedNotes (split) == 5);
    // and Center 100 % puts each on its own semitone
    auto corrected = split;
    newtonefx::applyCentre (corrected, 1.0);
    std::vector<int> keys;
    for (const auto& p : corrected.pitches)
        if (p.voiced())
            keys.push_back ((int) std::lround (p.origPitch + p.shift));
    CHECK ((keys == std::vector<int> { 60, 62, 64, 65, 67 }));
    // a 35-cent vibrato is no step, a slow 50-cent drift is no step, an ordinary melody is unchanged
    const auto melody = std::vector<std::pair<double, double>> { { 60, .5 }, { 62, .5 }, { 64, .5 }, { 65, .5 }, { 67, 1.0 }, { 0, .25 }, { 65, .5 }, { 64, .5 }, { 62, .5 }, { 60, 1.0 } };
    CHECK (ara190kx::voicedNotes (newtonefx::analyseNotes (ara190kx::sung (melody, 35.0), fine)) == 9);
    CHECK (ara190kx::voicedNotes (newtonefx::analyseNotes (ara190kx::sung (melody), fine)) == 9);
    CHECK (ara190kx::voicedNotes (newtonefx::analyseNotes (ara190kx::sung (melody))) == 9);
}

TEST (melodyne190kx_newtone_fl_analysis_unchanged_by_default)
{
    using namespace lynx;
    // the default options are FL's analysis: the same notes as an explicit FL call, sample for sample
    const auto s = ara190kx::sung ({ { 57, 0.4 }, { 0, 0.1 }, { 59, 0.6 }, { 60.4, 0.6 } }, 25.0);
    const auto a = newtonefx::analyseNotes (s);
    const auto b = newtonefx::analyseNotes (s, newtonefx::AnalyseOptions {});
    CHECK (a.pitches.size() == b.pitches.size());
    for (std::size_t i = 0; i < a.pitches.size() && i < b.pitches.size(); ++i)
        CHECK (a.pitches[i].origStart == b.pitches[i].origStart && a.pitches[i].origEnd == b.pitches[i].origEnd
               && a.pitches[i].origPitch == b.pitches[i].origPitch);
}
