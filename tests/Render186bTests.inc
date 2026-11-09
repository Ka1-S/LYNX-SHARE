// Round 186b: a render that comes back into the project as an audio clip plays at the level of what it replaces —
// the mixer's "Render to audio (freeze)" (makeBounceSong / applyBounce), FL's pattern render ("Render and replace") and
// consolidation. Each was 5.18 dB loud: its audio clip channel was made at 100 % (12800) while a Playlist audio clip
// plays its file at unity at 10000 of 12800 (Instruments.cpp audioClipUnity). FL loads AudioTrack.fst for both of its
// renders (FUN_01188230 / FUN_012fbda0), whose channel volume is 10000, and never sets the volume. The bounce also played
// the insert's route-to-Master level twice and kept the Master's EQ / separation / automation in the file.
// Every case renders the song live, renders the file the UI renders, applies it, renders again and compares RMS levels.

#include "core/QuickRender.h"
#include "core/StandIns.h"
#include <cmath>
#include <map>
#include <memory>

namespace r186b
{
constexpr double rate = 48000.0;

struct Files
{
    std::map<std::string, std::shared_ptr<lynx::SampleData>> map;
    lynx::RackState::SampleProvider provider()
    {
        return [this] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
            const auto it = map.find (path);
            return it == map.end() ? nullptr : it->second;
        };
    }
};

struct Render
{
    std::vector<float> l, r;
    double rmsDb (double from, double to) const
    {
        double s = 0.0;
        long n = 0;
        for (auto i = (long) (from * rate); i < (long) (to * rate) && i < (long) l.size(); ++i, ++n)
            s += (double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i];
        return 10.0 * std::log10 (std::max (1e-30, s / (double) std::max (1L, 2 * n)));
    }
};

Render render (const lynx::Song& song, Files& files, double seconds)
{
    auto state = lynx::RackState::build (song, nullptr, rate, files.provider());
    lynx::RackRenderer rr;
    rr.prepare (rate, 512);
    Render o;
    const auto total = (int) (seconds * rate);
    o.l.resize ((std::size_t) total);
    o.r.resize ((std::size_t) total);
    double tick = 0.0;
    const auto tps = lynx::ticksPerSample (song.bpm, rate);
    for (int at = 0; at < total; at += 512)
    {
        const auto n = std::min (512, total - at);
        rr.process (state.get(), true, tick, n, o.l.data() + at, o.r.data() + at);
        tick += n * tps;
    }
    return o;
}

void store (Files& files, const std::string& path, const Render& o)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left = o.l;
    d->right = o.r;
    files.map[path] = d;
}

// a 0.25 sine (441 Hz, 6 s) on an audio clip of 4 s (8 beats at 120) feeding insert 1; the default Master (limiter)
lynx::Song sineSong (Files& files)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (6.0 * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.25f * (float) std::sin (6.283185307179586 * 441.0 * (double) i / rate);
    d->right = d->left;
    files.map["sine"] = d;

    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "sine";
    c.volume = lynx::audioClipUnityVolume;
    c.mixerTrack = 1;
    s.channels = { c };
    s.patterns = { lynx::Pattern {} };
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.length = 8 * lynx::ppq;
    s.playlist = { clip };
    return s;
}

// live vs bounced, in dB (the song's level over 0.5…3.5 s)
double bounceDiff (const lynx::Song& song, Files& files, double* fileDb = nullptr)
{
    const auto live = render (song, files, 5.0);
    const auto recorded = render (lynx::makeBounceSong (song, 1), files, 5.0); // what bounceInsertToAudio writes
    store (files, "bounce", recorded);
    auto after = song;
    const auto added = lynx::applyBounce (after, 1, "bounce", 8 * lynx::ppq);
    CHECK (added > 0 && after.mixer[1].muted && after.channels.back().volume == lynx::audioClipUnityVolume);
    if (fileDb != nullptr)
        *fileDb = recorded.rmsDb (0.5, 3.5);
    return render (after, files, 5.0).rmsDb (0.5, 3.5) - live.rmsDb (0.5, 3.5);
}
} // namespace r186b

TEST (r186b_audio_clip_unity_is_fl_audiotrack_template)
{
    // FL's Data/System/AudioTrack.fst: Levels event 0xDB = pan 6400, volume 10000 (of 12800)
    CHECK (lynx::audioClipUnityVolume == 10000.0f / 12800.0f);
    // a channel at that volume plays an audio clip's file at unity (the clip law's 1 / channelGain (10000 / 12800))
    // and 100 % plays it 5.18 dB loud: what every render back into the project did before round 186b
    CHECK (std::abs (20.0 * std::log10 (lynx::channelGain (1.0f) / lynx::channelGain (lynx::audioClipUnityVolume)) - 5.18) < 0.01);
    // stem stand-ins: a channel at 100 % with the clip gain stemGain plays at unity too (StandIns.cpp)
    CHECK (std::abs (lynx::standins::stemGain() - lynx::channelGain (lynx::audioClipUnityVolume)) < 1e-7f);
}

TEST (r186b_render_to_audio_plays_at_the_level_it_replaces)
{
    using namespace r186b;
    Files files;
    // plain: insert at unity, the default Master's limiter (well under its ceiling)
    {
        auto s = sineSong (files);
        double fileDb = 0.0;
        const auto d = bounceDiff (s, files, &fileDb);
        std::printf ("  render to audio, plain: %+.3f dB (file %.2f dBFS)\n", d, fileDb);
        CHECK (std::abs (d) < 0.01);
        CHECK (std::abs (fileDb - (20.0 * std::log10 (0.25) - 3.0103)) < 0.01); // the sine's own RMS: nothing added
    }
    // the insert's fader at half and the channel at 100 %: both are in the file, once
    {
        auto s = sineSong (files);
        s.channels[0].volume = 1.0f;
        s.mixer[1].volume = 0.5f;
        const auto d = bounceDiff (s, files);
        std::printf ("  render to audio, fader 0.5, channel 100 %%: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
    // the route to Master at 50 %: the new insert keeps the route, so the file must not hold it (was -6.02 dB)
    {
        auto s = sineSong (files);
        s.mixer[1].masterLevel = 0.5f;
        const auto d = bounceDiff (s, files);
        std::printf ("  render to audio, route 0.5: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
    // the Master's fader, EQ and volume automation apply once, when the bounce plays through it
    {
        auto s = sineSong (files);
        s.mixer[0].volume = 0.5f;
        const auto d = bounceDiff (s, files);
        std::printf ("  render to audio, Master fader 0.5: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
    {
        auto s = sineSong (files);
        s.mixer[0].eq.gainDb[1] = 6.0f;
        s.mixer[0].eq.hz[1] = 441.0f;
        const auto d = bounceDiff (s, files);
        std::printf ("  render to audio, Master EQ +6 dB at 441 Hz: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
    {
        auto s = sineSong (files);
        lynx::Automation a;
        a.target = { lynx::AutomationTargetKind::mixerVolume, 0 };
        a.points = { { 0, 0.4f }, { 8 * lynx::ppq, 0.4f } };
        s.automations.push_back (a);
        lynx::Clip ac;
        ac.kind = lynx::ClipKind::automation;
        ac.automation = 0;
        ac.track = 2;
        ac.length = 10 * lynx::ppq;
        s.playlist.push_back (ac);
        const auto d = bounceDiff (s, files);
        std::printf ("  render to audio, Master volume automated: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
}

TEST (r186b_pattern_render_and_consolidation_play_at_unity)
{
    using namespace r186b;
    Files files;
    // "Render and replace": the default beat's pattern, rendered in pattern mode, replaces its Playlist clip
    {
        auto s = lynx::Song::makeDefault();
        s.bpm = 120.0;
        s.mode = lynx::PlayMode::song;
        s.mixer[0].effects.clear(); // (FL renders through the Master and the clip plays into it again: keep it neutral)
        lynx::Clip clip;
        clip.pattern = 0;
        clip.length = 4 * lynx::ppq;
        s.playlist = { clip };
        const auto live = render (s, files, 2.0);
        store (files, "pattern", render (lynx::quickrender::makePatternRenderSong (s, 0), files, 2.0));
        auto after = s;
        const auto ch = lynx::quickrender::applyPatternRender (after, 0, "pattern", true);
        CHECK (after.channels[(std::size_t) ch].volume == lynx::audioClipUnityVolume);
        const auto d = render (after, files, 2.0).rmsDb (0.0, 1.9) - live.rmsDb (0.0, 1.9);
        std::printf ("  pattern render and replace: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
    // "Consolidate this track" from the song start
    {
        auto s = sineSong (files);
        s.mixer[0].effects.clear();
        const auto live = render (s, files, 5.0);
        lynx::quickrender::Plan plan;
        CHECK (lynx::quickrender::planTrack (s, 0, false, lynx::quickrender::Start::songStart, plan) == lynx::quickrender::Result::ok);
        auto rs = lynx::quickrender::makePlanRenderSong (s, plan);
        rs.loopStart = rs.loopEnd = 0; // (the export renders the range; here from 0, which is the plan's start)
        store (files, "consolidated", render (rs, files, 5.0));
        auto after = s;
        const auto ch = lynx::quickrender::applyConsolidation (after, plan, "consolidated", 8 * lynx::ppq);
        CHECK (after.channels[(std::size_t) ch].volume == lynx::audioClipUnityVolume);
        const auto d = render (after, files, 5.0).rmsDb (0.5, 3.5) - live.rmsDb (0.5, 3.5);
        std::printf ("  consolidate this track: %+.3f dB\n", d);
        CHECK (std::abs (d) < 0.01);
    }
}
