// --- Round 190dg: the quality gate of the Simple dock's one-knob effects (core/GoalKnobs.h), chain presets and Blend
// (core/ChainPresets.h, RackRenderer): per knob position the level, the peaks and the cost on four test sounds; a knob
// or Blend turned while the sound plays makes no click.

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>

#include "core/ChainPresets.h"
#include "core/Effects.h"
#include "core/GoalKnobs.h"
#include "core/RackRenderer.h"
#include "core/SoundLibrary.h"

namespace simple190dg
{
constexpr double rate = 48000.0;
constexpr int block = 256;

enum class Sig { pink, drums, chord, sine, quietDrums };

// Five test sounds, the same every time, mono, 4 s: pink noise at −20 dBFS RMS; a drum loop (kick, snare, hats, 120
// BPM) whose peaks reach −1 dBFS; a sustained low chord (rich in harmonics) at −3 dBFS peak; a 220 Hz sine at −6 dBFS;
// the drum loop again at −13 dBFS (dynamics and saturation depend on the level they get).
inline std::shared_ptr<const lynx::SampleData> signal (Sig which)
{
    auto make = [] (Sig s) {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = rate;
        auto& x = d->left;
        x.assign ((std::size_t) (rate * 4.0), 0.0f); // (sampler files longer than the renders)
        std::uint32_t seed = 0x190d;
        auto white = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
        };
        const auto pi2 = 6.283185307179586;
        if (s == Sig::pink)
        {
            float b0 = 0, b1 = 0, b2 = 0;
            for (auto& v : x)
            {
                const auto w = white();
                b0 = 0.99765f * b0 + w * 0.0990460f;
                b1 = 0.96300f * b1 + w * 0.2965164f;
                b2 = 0.57000f * b2 + w * 1.0526913f;
                v = b0 + b1 + b2 + w * 0.1848f;
            }
        }
        else if (s == Sig::drums || s == Sig::quietDrums)
        {
            const auto beat = (std::size_t) (rate * 0.5); // 120 BPM
            float hp = 0.0f, last = 0.0f;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const auto inBeat = i % beat, b = (i / beat) % 4;
                const auto t = (double) inBeat / rate;
                double v = 0.0;
                if (b == 0 || b == 2) // kick: a falling sine
                {
                    const auto f = 45.0 + 75.0 * std::exp (-t * 30.0);
                    v += std::sin (pi2 * (45.0 * t + 75.0 / 30.0 * (1.0 - std::exp (-t * 30.0)))) * std::exp (-t * 7.0) * (f > 0 ? 1.0 : 0.0);
                }
                const auto w = white();
                if (b == 1 || b == 3) // snare: tone and noise
                    v += 0.5 * std::sin (pi2 * 190.0 * t) * std::exp (-t * 25.0) + 0.6 * w * std::exp (-t * 18.0);
                // hats on every eighth: high-passed noise
                const auto th = (double) (i % (beat / 2)) / rate;
                hp = 0.6f * (hp + w - last);
                last = w;
                v += 0.25 * hp * std::exp (-th * 60.0);
                x[i] = (float) v;
            }
        }
        else if (s == Sig::chord)
        {
            const double f[] { 110.0, 164.81, 220.0, 277.18 };
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const auto t = (double) i / rate;
                double v = 0.0;
                for (const auto fr : f)
                    for (int h = 1; h <= 8; ++h)
                        v += std::sin (pi2 * fr * h * t + h) / h;
                x[i] = (float) v;
            }
        }
        else
            for (std::size_t i = 0; i < x.size(); ++i)
                x[i] = (float) std::sin (pi2 * 220.0 * (double) i / rate);
        // levels
        double sum = 0.0;
        float peak = 0.0f;
        for (auto v : x)
        {
            sum += (double) v * v;
            peak = std::max (peak, std::abs (v));
        }
        // (the sampler channel at its default volume and centre pan plays the file 5.2 dB down: made up here, so the
        // levels are the ones the insert gets)
        const auto path = std::pow (10.0, 5.2 / 20.0);
        const auto gain = path * (s == Sig::pink ? 0.1 / std::sqrt (sum / (double) x.size()) : s == Sig::drums ? 0.891 / peak : s == Sig::quietDrums ? 0.224 / peak : s == Sig::chord ? 0.708 / peak : 0.501 / peak);
        for (auto& v : x)
            v = (float) (v * gain);
        // a short fade in, so the start is not a click of its own
        for (std::size_t i = 0; i < 480; ++i)
            x[i] *= (float) i / 480.0f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    static const std::shared_ptr<const lynx::SampleData> all[] { make (Sig::pink), make (Sig::drums), make (Sig::chord), make (Sig::sine), make (Sig::quietDrums) };
    return all[(int) which];
}

inline const char* signalName (Sig s)
{
    return s == Sig::pink ? "pink" : s == Sig::drums ? "drums" : s == Sig::chord ? "chord" : s == Sig::sine ? "sine" : "drums-13";
}

struct Run
{
    std::vector<float> out; // the left output
    double nsPerSample = 0.0;
};

// The sound played through insert 1 (a sampler at unity: the insert gets the file's own samples), whose chain `setup`
// makes. `perBlock` (optional) changes the insert before each block, as the UI's edits do: a new state built from the
// one before, so the running effects are kept (AudioEngine::publishSong).
inline Run render (Sig which, const std::function<void (lynx::MixerTrack&)>& setup, int blocks = 0,
                   const std::function<void (int, lynx::MixerTrack&)>& perBlock = {})
{
    using namespace lynx;
    const auto data = signal (which);
    if (blocks <= 0)
        blocks = (int) (data->left.size() / block);
    Song s;
    s.bpm = 120.0;
    s.mixer.resize (2);
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = "signal";
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    setup (s.mixer[1]);
    auto provider = [data] (const std::string&) { return data; };
    auto state = RackState::build (s, nullptr, rate, provider);
    RackRenderer renderer;
    renderer.prepare (rate, block);
    LiveEventQueue live;
    NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    Run run;
    run.out.reserve ((std::size_t) (blocks * block));
    std::vector<float> l (block), r (block);
    std::int64_t ns = 0;
    for (int b = 0; b < blocks; ++b)
    {
        if (perBlock)
        {
            perBlock (b, s.mixer[1]);
            state = RackState::build (s, state.get(), rate, provider);
        }
        const auto t0 = std::chrono::steady_clock::now();
        renderer.process (state.get(), false, 0.0, block, l.data(), r.data(), &live, nullptr);
        ns += std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
        run.out.insert (run.out.end(), l.begin(), l.end());
    }
    run.nsPerSample = (double) ns / (double) (blocks * block);
    return run;
}

inline double rmsOf (const std::vector<float>& x, std::size_t from = 0, std::size_t to = 0)
{
    to = to == 0 ? x.size() : std::min (to, x.size());
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) std::max<std::size_t> (1, to - from));
}
inline float peakOf (const std::vector<float>& x, std::size_t from = 0)
{
    float p = 0.0f;
    for (std::size_t i = from; i < x.size(); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}
inline int oversOf (const std::vector<float>& x)
{
    int n = 0;
    for (auto v : x)
        n += std::abs (v) > 1.0f ? 1 : 0;
    return n;
}
inline bool finite (const std::vector<float>& x)
{
    for (auto v : x)
        if (! std::isfinite (v))
            return false;
    return true;
}
inline double dbOf (double ratio) { return 20.0 * std::log10 (std::max (1.0e-12, ratio)); }

// The largest jump of the signal's slope (its second difference) after `from`: a click is a jump far above what the
// same sound has when nothing moves
inline double kinkOf (const std::vector<float>& x, std::size_t from)
{
    double k = 0.0;
    for (std::size_t i = std::max<std::size_t> (from, 2); i < x.size(); ++i)
        k = std::max (k, std::abs ((double) x[i] - 2.0 * x[i - 1] + x[i - 2]));
    return k;
}

constexpr std::size_t settle = (std::size_t) (rate * 0.75); // (past the effects' first moments)

struct Point
{
    double level = 0.0;  // dB against the dry sound (RMS after `settle`)
    double peak = 0.0;   // dBFS
    int overs = 0;       // samples above full scale
    double ns = 0.0;     // processing cost per sample (the whole render)
    bool finite = true;
};

inline Point measure (Sig which, const std::function<void (lynx::MixerTrack&)>& setup, const Run& dry)
{
    const auto wet = render (which, setup);
    Point p;
    p.level = dbOf (rmsOf (wet.out, settle) / rmsOf (dry.out, settle));
    p.peak = dbOf (peakOf (wet.out));
    p.overs = oversOf (wet.out);
    p.ns = wet.nsPerSample;
    p.finite = finite (wet.out);
    return p;
}

// One knob turned from 0 to 100 % over `seconds` while `which` plays (a new value every block, the fastest the UI
// sends), then back: how far above the steady renders' own worst slope jump the sweep's is (1 = no click at all)
inline double sweepKink (int goal, Sig which, double seconds = 1.5)
{
    using namespace lynx;
    const auto blocks = (int) (seconds * rate / block);
    const auto sweep = render (which, [goal] (MixerTrack& t) { t.effects.push_back (goals::make (goal, 0.0f)); }, 2 * blocks + 40,
                               [goal, blocks] (int b, MixerTrack& t) {
                                   const auto up = b < 20 ? 0.0f : b < 20 + blocks ? (float) (b - 20) / (float) blocks : b < 20 + 2 * blocks ? 1.0f - (float) (b - 20 - blocks) / (float) blocks : 0.0f;
                                   goals::setAmount (t.effects[0], up);
                               });
    double steady = 1.0e-9;
    for (int i = 0; i <= 10; ++i)
    {
        const auto still = render (which, [goal, i] (MixerTrack& t) { t.effects.push_back (goals::make (goal, (float) i / 10.0f)); }, 2 * blocks + 40);
        steady = std::max (steady, kinkOf (still.out, 20 * block));
    }
    return kinkOf (sweep.out, 20 * block) / steady;
}
} // namespace simple190dg

namespace simple190dg
{
// The gate's bounds (round 190dg, HANDOFF.md "Quality gate"): the level against the dry sound at every knob position
// (a saturator with auto gain lifts quiet sounds and squashes hot ones, a leveller lets a hit's start through: hot
// drums may come out quieter, never more than 6 dB)
inline bool levelInBounds (double level) { return level <= 3.0 && level >= -6.0; }
// ... and the peaks: at most 3 dB over the input's own, or still below −6 dBFS; Punch's guard holds full scale
inline bool peakInBounds (int goal, Sig which, double inPeakDb, double outPeakDb)
{
    if (goal == lynx::goals::punch)
        return outPeakDb <= 0.01;
    // (a room on a steady pure tone beats against it: its modes, not a level the knob sets)
    if (goal == lynx::goals::room && which == Sig::sine)
        return outPeakDb <= 0.0;
    return outPeakDb <= inPeakDb + 3.0 || outPeakDb <= -6.0;
}
} // namespace simple190dg

// Every one-knob effect at nine knob positions on five sounds: the level stays near the dry one, moves in small steps,
// and no peak jumps (the gate of round 190dg; the full 21-position table with CPU is in the round's HANDOFF.md)
TEST (simple190dg_one_knob_gate)
{
    using namespace lynx;
    using namespace simple190dg;
    const bool print = std::getenv ("LYNX_GATE_PRINT") != nullptr;
    const Sig sigs[] { Sig::pink, Sig::drums, Sig::chord, Sig::sine, Sig::quietDrums };
    const int blocks = (int) (2.5 * rate / block);
    for (const auto which : sigs)
    {
        const auto dry = render (which, [] (MixerTrack&) {}, blocks);
        const auto inPeak = dbOf (peakOf (dry.out));
        CHECK (rmsOf (dry.out, settle) > 0.005);
        for (int id = 1; id <= goals::count; ++id)
        {
            double before = 0.0;
            for (int i = 0; i <= 8; ++i)
            {
                const auto amount = (float) i / 8.0f;
                const auto wet = render (which, [id, amount] (MixerTrack& t) { t.effects.push_back (goals::make (id, amount)); }, blocks);
                CHECK (finite (wet.out));
                const auto level = dbOf (rmsOf (wet.out, settle) / rmsOf (dry.out, settle));
                const auto peak = dbOf (peakOf (wet.out));
                if (print)
                    std::printf ("GATE %-9s %-8s %3.0f%% level %+5.2f dB peak %+5.2f dBFS\n", goals::name (id), signalName (which), amount * 100.0f, level, peak);
                CHECK (levelInBounds (level));
                CHECK (peakInBounds (id, which, inPeak, peak));
                if (i == 0)
                    CHECK (std::abs (level) < 0.3);
                else // (12.5 % apart: no step of more than 1.5 dB)
                    CHECK (std::abs (level - before) <= 1.5 || (id == goals::room && which == Sig::sine));
                before = level;
            }
        }
    }
}

// A knob turned while a held tone plays (one new value every block, the fastest edits come), and one jumped from 0 to
// 100 % and back: no click. "Kink" = the largest jump of the signal's slope, against the same sound's own worst with the
// knob held still anywhere (1 = nothing added).
TEST (simple190dg_knob_turns_without_clicks)
{
    using namespace lynx;
    using namespace simple190dg;
    const bool print = std::getenv ("LYNX_GATE_PRINT") != nullptr;
    for (int id = 1; id <= goals::count; ++id)
        for (const auto which : { Sig::sine, Sig::chord })
        {
            const auto turned = sweepKink (id, which, 1.0);
            const int blocks = (int) (1.0 * rate / block);
            const auto jump = render (which, [id] (MixerTrack& t) { t.effects.push_back (goals::make (id, 0.0f)); }, 2 * blocks + 40,
                                      [id, blocks] (int b, MixerTrack& t) { goals::setAmount (t.effects[0], b >= 20 + blocks / 2 && b < 20 + blocks ? 1.0f : 0.0f); });
            double steady = 1.0e-9;
            for (int i = 0; i <= 10; ++i)
                steady = std::max (steady, kinkOf (render (which, [id, i] (MixerTrack& t) { t.effects.push_back (goals::make (id, (float) i / 10.0f)); }, 2 * blocks + 40).out, 20 * block));
            const auto jumped = kinkOf (jump.out, 20 * block) / steady;
            if (print)
                std::printf ("TURN %-9s %-6s sweep %.2f jump %.2f\n", goals::name (id), signalName (which), turned, jumped);
            CHECK (turned < 1.5);
            CHECK (jumped < 1.5);
        }
}

// Blend moved while the sound plays, or jumped 100 → 0 → 100 % (a preset, a double click): it ramps, also through a
// chain with latency whose dry path starts empty, and the insert goes back to no stage at 100 %
TEST (simple190dg_blend_ramps)
{
    using namespace lynx;
    using namespace simple190dg;
    const int blocks = (int) (1.0 * rate / block);
    for (const char* name : { "Radio Booth", "Crunch Stack", "Growl", "Canyon", "Big Chorus" })
    {
        const auto* p = chains::find (name);
        CHECK (p != nullptr);
        if (p == nullptr)
            continue;
        for (const auto which : { Sig::sine, Sig::chord })
        {
            const auto setup = [p] (MixerTrack& t) {
                t.effects = chains::slots (*p);
                t.chainBlend = 1.0f;
            };
            const auto sweep = render (which, setup, 2 * blocks + 40, [blocks] (int b, MixerTrack& t) {
                const auto up = b < 20 ? 0.0f : b < 20 + blocks ? (float) (b - 20) / (float) blocks : b < 20 + 2 * blocks ? 1.0f - (float) (b - 20 - blocks) / (float) blocks : 0.0f;
                t.chainBlend = 1.0f - up;
            });
            const auto jump = render (which, setup, 2 * blocks + 40,
                                      [blocks] (int b, MixerTrack& t) { t.chainBlend = b >= 20 + blocks / 2 && b < 20 + blocks ? 0.0f : 1.0f; });
            double steady = 1.0e-9;
            for (int i = 0; i <= 10; ++i)
                steady = std::max (steady, kinkOf (render (which, [p, i] (MixerTrack& t) {
                                                       t.effects = chains::slots (*p);
                                                       t.chainBlend = (float) i / 10.0f;
                                                   }, 2 * blocks + 40).out, 20 * block));
            CHECK (kinkOf (sweep.out, 20 * block) / steady < 1.5);
            CHECK (kinkOf (jump.out, 20 * block) / steady < 1.5);
            // the jump's end: 100 % again, the same samples as a chain that never moved (once the dry path is out)
            const auto still = render (which, setup, 2 * blocks + 40);
            double diff = 0.0;
            for (std::size_t i = (std::size_t) ((20 + blocks + 80) * block); i < still.out.size(); ++i) // (after the ramp back)
                diff = std::max (diff, (double) std::abs (still.out[i] - jump.out[i]));
            CHECK (diff < 1.0e-6 || dbOf (diff / std::max (1.0e-9, (double) peakOf (still.out))) < -60.0);
        }
    }
    // a chain at a steady Blend renders as before this round: a first build starts at its Blend (no ramp from 100 %)
    Song s;
    s.mixer.resize (2);
    s.mixer[1].effects = chains::slots (*chains::find ("Canyon"));
    s.mixer[1].chainBlend = 0.3f;
    const auto st = RackState::build (s, nullptr, rate);
    CHECK (st->chainBlends.size() == 2 && st->chainBlends[1] != nullptr);
    if (st->chainBlends.size() == 2 && st->chainBlends[1] != nullptr)
    {
        CHECK (std::abs (st->chainBlends[1]->lastWet.load() - 0.3f) < 1.0e-6f && st->chainBlends[1]->warmup == 0);
        // the next live build at 100 %: kept for one ramp back, then gone
        s.mixer[1].chainBlend = 1.0f;
        const auto next = RackState::build (s, st.get(), rate);
        CHECK (next->chainBlends[1] != nullptr);
        next->chainBlends[1]->lastWet.store (1.0f);
        const auto after = RackState::build (s, next.get(), rate);
        CHECK (after->chainBlends[1] == nullptr);
    }
}

// The 22 chain presets at Blend 0, 50, 100 % and their own on three sounds: near the dry level, no peak jump (a chain
// stacks up to four effects: its peaks may rise 3.5 dB over the input's)
TEST (simple190dg_presets_gate)
{
    using namespace lynx;
    using namespace simple190dg;
    const int blocks = (int) (2.5 * rate / block);
    for (const auto which : { Sig::pink, Sig::drums, Sig::chord })
    {
        const auto dry = render (which, [] (MixerTrack&) {}, blocks);
        const auto inPeak = dbOf (peakOf (dry.out));
        for (const auto& p : chains::factory())
            for (const float blend : { 0.0f, 0.5f, 1.0f, p.blend })
            {
                const auto wet = render (which, [&p, blend] (MixerTrack& t) {
                    t.effects = chains::slots (p);
                    t.chainBlend = blend;
                }, blocks);
                const auto level = dbOf (rmsOf (wet.out, settle) / rmsOf (dry.out, settle));
                CHECK (finite (wet.out));
                CHECK (level < 3.0 && level > -4.5);
                CHECK (dbOf (peakOf (wet.out)) <= inPeak + 3.5);
                if (blend == 0.0f)
                    CHECK (std::abs (level) < 0.05);
            }
    }
}

// The installed instrument plugins as sounds (round 190dg): their category, the channel a pick makes, finding them again
TEST (simple190dg_sound_library_plugins)
{
    using namespace lynx;
    using soundlib::Category;
    CHECK (soundlib::pluginCategory ("Instrument|Synth", "Vital") == Category::synths);
    CHECK (soundlib::pluginCategory ("Instrument|Drum", "Battery") == Category::drums);
    CHECK (soundlib::pluginCategory ("Instrument|Piano", "Grand") == Category::keys);
    CHECK (soundlib::pluginCategory ("Instrument|Synth", "Keyscape") == Category::keys);
    CHECK (soundlib::pluginCategory ("Synth", "808 Studio") == Category::drums);
    CHECK (soundlib::pluginCategory ("Instrument|Synth|Sampler", "Orchestral Strings") == Category::strings);
    CHECK (soundlib::pluginCategory ("", "Sub Bass Machine") == Category::bass);
    CHECK (soundlib::pluginCategory ("Instrument|Organ", "B-3") == Category::organ);
    CHECK (soundlib::pluginCategory ("", "") == Category::synths);
    CHECK (std::string (soundlib::categoryName (Category::synths)).size() > 3);

    const auto list = soundlib::fromPlugins ({ { "VST3-Vital-1", "Vital", "Vital Audio", "VST3", "Instrument|Synth" },
                                               { "VST3-Drums-2", "Kit Maker", "Someone", "VST3", "Instrument|Drum" },
                                               { "", "No id", "", "", "" } });
    CHECK (list.size() == 2);
    for (const auto& s : list)
        CHECK (s.source == soundlib::Source::plugin && s.kind == InstrumentKind::plugin && ! s.pluginId.empty());
    const auto synths = soundlib::filter (list, Category::synths);
    CHECK (synths.size() == 1 && list[(std::size_t) synths[0]].name == "Vital" && list[(std::size_t) synths[0]].origin == "Vital Audio \xc2\xb7 VST3");
    CHECK (soundlib::filter (list, Category::drums, "kit").size() == 1 && soundlib::filter (list, Category::drums, "vital").empty());

    ChannelDef mine;
    mine.instrument = InstrumentKind::synth;
    mine.name = "My track";
    mine.volume = 0.42f;
    mine.mixerTrack = 5;
    const auto& vital = list[(std::size_t) synths[0]];
    const auto def = soundlib::withSound (mine, vital);
    CHECK (def.has_value());
    if (! def)
        return;
    CHECK (def->instrument == InstrumentKind::plugin && def->plugin.id == "VST3-Vital-1" && def->plugin.name == "Vital");
    CHECK (def->name == "My track" && def->volume == 0.42f && def->mixerTrack == 5); // the channel's own settings stay
    // a running plugin saves a state of its own: the pick is still found by its plugin
    auto running = *def;
    running.plugin.state = "c3RhdGU=";
    CHECK (soundlib::find (list, running) == synths[0]);
    running.plugin.id = "VST3-Other";
    CHECK (soundlib::find (list, running) == -1);
    CHECK (soundlib::auditionKeys (list[(std::size_t) soundlib::filter (list, Category::drums)[0]]) == std::vector<int> { 60 });
}
