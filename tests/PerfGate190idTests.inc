// Round perfgate190id (community slice B12): the big-project performance gate's synthetic projects
// (core/PerfProjects.h). The gate compares measurements across builds, so the projects must be what they promise and
// the same bit for bit every time they are made; and they must play (a project that renders silence measures nothing).

namespace pg190id
{
using namespace lynx;

int clipsOf (const Song& song, ClipKind kind)
{
    return (int) std::count_if (song.playlist.begin(), song.playlist.end(), [kind] (const Clip& c) { return c.kind == kind; });
}

// `seconds` of the song from its start, as tools/perfbench renders it (the export song, live quality, one thread)
std::vector<float> render (const Song& song, const perfprojects::Samples& samples, double seconds, int block)
{
    std::int64_t ticks = 0;
    const auto exportSong = makeExportSong (song, true, 0, ticks);
    auto provider = [&samples] (const std::string& p) {
        const auto it = samples.find (p);
        return it != samples.end() ? it->second : std::shared_ptr<const SampleData> {};
    };
    auto state = RackState::build (exportSong, nullptr, 44100.0, provider);
    RackRenderer renderer;
    renderer.prepare (44100.0, 2048);
    LiveEventQueue live, midiLive;
    const auto total = (int) (seconds * 44100.0) / block * block;
    std::vector<float> out ((std::size_t) total * 2);
    std::vector<float> l ((std::size_t) block), r ((std::size_t) block);
    const auto tps = ticksPerSample (exportSong.bpm, 44100.0);
    for (int pos = 0; pos < total; pos += block)
    {
        renderer.process (state.get(), true, pos * tps, block, l.data(), r.data(), &live, nullptr, &midiLive);
        for (int i = 0; i < block; ++i)
            out[(std::size_t) (pos + i) * 2] = l[(std::size_t) i], out[(std::size_t) (pos + i) * 2 + 1] = r[(std::size_t) i];
    }
    return out;
}
} // namespace pg190id

TEST (perfgate_projects_have_the_promised_size)
{
    using namespace lynx;
    perfprojects::Samples samples;
    const auto big = perfprojects::big200 (samples);
    CHECK (big.channels.size() == 200);
    CHECK (big.mixer.size() == 121); // Master + 120 inserts
    CHECK (pg190id::clipsOf (big, ClipKind::pattern) + pg190id::clipsOf (big, ClipKind::audio) == 2000);
    CHECK (pg190id::clipsOf (big, ClipKind::automation) == 200);
    CHECK (big.automations.size() == 200);
    int lynxEffects = 0;
    for (const auto& m : big.mixer)
        lynxEffects += (int) m.effects.size();
    CHECK (lynxEffects == 100 * 2 + 25 + 20 * 3 + 2);
    CHECK (samples.size() == 8 + 40); // the samplers' one-shots and the audio channels' loops
    const auto automation = perfprojects::automation();
    CHECK (automation.automations.size() == 401);
    CHECK (pg190id::clipsOf (automation, ClipKind::automation) == 401);
    CHECK (automation.automations[0].points.size() == 257); // a point every 16th over 16 bars
    perfprojects::Samples streamSamples;
    const auto stream = perfprojects::stream (60, streamSamples);
    CHECK (stream.channels.size() == 24 && streamSamples.size() == 24);
    CHECK (streamSamples.begin()->second->frames() == 60 * 44100);
    for (const auto& name : perfprojects::names())
    {
        Song song;
        perfprojects::Samples s;
        CHECK (perfprojects::make (name, song, s));
    }
    Song none;
    perfprojects::Samples s;
    CHECK (! perfprojects::make ("nothing", none, s));
}

TEST (perfgate_projects_are_the_same_every_time_and_play)
{
    using namespace lynx;
    for (const auto* name : { "big200", "automation" })
    {
        Song a, b;
        perfprojects::Samples sa, sb;
        perfprojects::make (name, a, sa);
        perfprojects::make (name, b, sb);
        CHECK (a.automations == b.automations);
        CHECK (a.playlist.size() == b.playlist.size());
        bool sameClips = true;
        for (std::size_t i = 0; i < a.playlist.size() && i < b.playlist.size(); ++i)
            sameClips = sameClips && a.playlist[i].start == b.playlist[i].start && a.playlist[i].length == b.playlist[i].length
                        && a.playlist[i].track == b.playlist[i].track && a.playlist[i].kind == b.playlist[i].kind;
        CHECK (sameClips);
        bool sameMixer = a.mixer.size() == b.mixer.size();
        for (std::size_t t = 0; sameMixer && t < a.mixer.size(); ++t)
            sameMixer = a.mixer[t].effects == b.mixer[t].effects;
        CHECK (sameMixer);
        bool sameAudio = sa.size() == sb.size();
        for (auto i = sa.begin(), j = sb.begin(); sameAudio && i != sa.end(); ++i, ++j)
            sameAudio = i->first == j->first && i->second->left == j->second->left && i->second->right == j->second->right;
        CHECK (sameAudio);
        const auto x = pg190id::render (a, sa, 0.5, 256), y = pg190id::render (b, sb, 0.5, 256);
        CHECK (x == y);
        float peak = 0.0f;
        bool finite = true;
        for (const auto v : x)
            peak = std::max (peak, std::abs (v)), finite = finite && std::isfinite (v);
        CHECK (finite);
        CHECK (peak > 1.0e-3f);
    }
}

TEST (perfgate_tempo_scan_reads_only_the_tempo_clips_and_gives_the_same_tempo)
{
    // the shifter plan scans the song's tempo every 16th; it now reads only the clips tempoAt can use
    // (tempomap::tempoClips): the same tempo everywhere, with automation clips (two overlapping, one muted) and pattern
    // Tempo events
    using namespace lynx;
    auto song = perfprojects::automation(); // (400 automation clips of other targets + one tempo clip)
    AutomationTarget tempo;
    tempo.kind = AutomationTargetKind::tempo;
    const auto second = perfprojects::addAutomation (song, tempo, "Tempo 2", perfprojects::wavePoints (32.0, 0.5, valueFromTempo (118.0), valueFromTempo (140.0), 3));
    perfprojects::addAutomationClip (song, second, 38, 8 * 4 * ppq, 8 * 4 * ppq);
    perfprojects::addAutomationClip (song, second, 37, 20 * 4 * ppq, 4 * 4 * ppq);
    song.playlist.back().muted = true;
    PatternEvents lane;
    lane.target.kind = AutomationTargetKind::tempo;
    lane.events.push_back ({ 0, valueFromTempo (131.0) });
    lane.events.push_back ({ 2 * ppq, valueFromTempo (127.0) });
    song.patterns[3].events.push_back (lane);
    const auto clips = tempomap::tempoClips (song);
    CHECK (clips.size() < song.playlist.size() / 4);
    bool same = true;
    for (double t = 0.0; t <= (double) song.songLength() + ppq; t += ppq / 16.0)
        same = same && tempomap::tempoAt (song, t) == tempomap::tempoAtIn (song, clips, t);
    CHECK (same);
    CHECK (tempomap::active (song));
}

TEST (render_ahead_probe_keeps_four_times_fewer_overruns_for_up_to_five_percent_more_cpu)
{
    // owner decision 0.1.30 (round perfgate190id): with 4x fewer overruns or better, up to +5 % CPU keeps render-ahead
    // on; the 0.1.28 rule (fewer overruns at up to +2 %) and the CPU-win rule stay as they were
    using namespace lynx;
    const auto v = [] (double cpuOff, double cpuOn, double overOff, double overOn) {
        return RenderAhead::probeVerdict (cpuOff, cpuOn, overOff / 60.0, overOn / 60.0); // (per minute -> per second)
    };
    // the loaded-machine probes of round perfgate190id: synth100 at 64, 20x fewer at +2.5 % (kept off before)
    CHECK (v (2.089, 2.141, 29438.0, 1429.0).keep);
    CHECK (v (2.089, 2.141, 29438.0, 1429.0).forOverruns);
    // ... and Exoplanet at 64, 10x fewer at +12.8 %: still off
    CHECK (! v (1.048, 1.182, 14724.0, 1419.0).keep);
    // the edges: exactly 4x and +5 % on; just past either off
    CHECK (v (1.0, 1.05, 400.0, 100.0).keep);
    CHECK (! v (1.0, 1.0501, 400.0, 100.0).keep);
    CHECK (! v (1.0, 1.04, 400.0, 101.0).keep);
    // the 0.1.28 rule: 20 % fewer (here 3x) at up to +2 %
    CHECK (v (1.0, 1.02, 300.0, 100.0).keep);
    CHECK (! v (1.0, 1.03, 300.0, 100.0).keep);
    // a CPU win with no more overruns: kept, not "for overruns"
    CHECK (v (1.0, 0.85, 100.0, 100.0).keep);
    CHECK (! v (1.0, 0.85, 100.0, 100.0).forOverruns);
    CHECK (! v (1.0, 0.85, 100.0, 120.0).keep); // (more overruns)
    // no overruns with it off: only a CPU win can keep it
    CHECK (! v (1.0, 1.0, 0.0, 0.0).keep);
    CHECK (v (1.0, 0.8, 0.0, 0.0).keep);
}
