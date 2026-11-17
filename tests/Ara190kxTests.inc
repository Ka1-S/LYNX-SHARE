// Round 190kx: ARA 2 on audio clips (core/AraLink.h, core/AraRegions.h). The model mapping is checked here on every
// platform; the hosting itself (engine/AraHost.cpp, Melodyne) in the app's --aratest and the round's Windows sessions.

#include "core/AraRegions.h"
#include "core/FlClipMenu.h"

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
