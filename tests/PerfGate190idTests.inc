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
