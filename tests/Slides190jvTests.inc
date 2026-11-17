#include "core/HostSlide.h"
#include "core/RackRenderer.h"
#include <cmath>

// Round 190jv (queue item 15): FL's slide notes step on FL's native generators. FL's host moves a sounding voice's Final
// pitch once per tick of the project (PPQ 96), in n + 1 equal steps for a slide note n ticks long, the first with the
// slide's start and the last at its end (REA_rodada190hg §1.1, measured on Slicex; REA_rodada190jv §3: FL's own renders
// of its render-test generators). core/HostSlide.h Steps, RackRenderer::stepsSlides, the Sampler's and LYNX Soundfont's
// own steps. LYNX's own generators, hosted plugins and the channel's porta keep the straight glide.

namespace slides190jv
{
constexpr double rate = 48000.0;

std::shared_ptr<lynx::SampleData> sine (double seconds, double hz)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.25f * (float) std::sin (6.283185307179586 * hz * (double) i / rate);
    d->right = d->left;
    return d;
}

// a Sampler channel (the sine at its root key) with a C5 for two beats and a slide note an octave up at FL tick 48,
// `flTicks` long; the left channel of `seconds` rendered in blocks of `block`
std::vector<float> render (lynx::InstrumentKind kind, int flTicks, int block, double seconds = 1.2)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    ChannelDef c;
    c.instrument = kind;
    c.name = "Sine";
    c.samplePath = "sine";
    s.channels = { c };
    Pattern p;
    Note n;
    n.key = 60;
    n.start = 0;
    n.length = 2 * ppq;
    Note slide;
    slide.key = 72;
    slide.start = 48 * ppq / 96;
    slide.length = flTicks * ppq / 96;
    slide.slide = true;
    p.notes = { n, slide };
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    const auto data = sine (3.0, 440.0);
    auto state = RackState::build (s, nullptr, rate, [data] (const std::string&) -> std::shared_ptr<const SampleData> { return data; });
    RackRenderer rr;
    rr.prepare (rate, block);
    const auto total = (int) (seconds * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, l.data() + at, r.data() + at, nullptr);
    }
    return l;
}

// the pitch (cents from 440 Hz) of each period that lies inside [from, to) samples: upward zero crossings, interpolated
std::vector<double> periodsCents (const std::vector<float>& x, double from, double to)
{
    std::vector<double> crossings, out;
    for (std::size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            crossings.push_back ((double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]));
    for (std::size_t k = 1; k < crossings.size(); ++k)
        if (crossings[k - 1] >= from && crossings[k] < to)
            out.push_back (1200.0 * std::log2 ((rate / (crossings[k] - crossings[k - 1])) / 440.0));
    return out;
}
} // namespace slides190jv

TEST (slides190jv_hostslide_steps_n_plus_one_on_the_ticks)
{
    using namespace lynx;
    // a slide 4 ticks long (1000 samples, a tick = 250): 5 steps of 240 cents, the first at once
    HostSlide s;
    HostSlide::pendingSteps() = { 4, 0.25f };
    s.start (1200.0f, 1000.0f);
    HostSlide::pendingSteps() = {};
    CHECK (s.moving() && s.stepping());
    CHECK (std::abs (s.advance (0.0f) - 240.0f) < 1.0e-3f); // (the first step: with the slide's start)
    CHECK (std::abs (s.advance (249.0f) - 240.0f) < 1.0e-3f);
    CHECK (std::abs (s.advance (1.0f) - 480.0f) < 1.0e-3f);   // tick 1
    CHECK (std::abs (s.advance (250.0f) - 720.0f) < 1.0e-3f); // tick 2
    CHECK (std::abs (s.advance (500.0f) - 1200.0f) < 1.0e-3f); // ticks 3 and 4: the end
    CHECK (! s.moving() && ! s.stepping());
    CHECK (std::abs (s.advance (1000.0f) - 1200.0f) < 1.0e-3f);

    // an engine that moves the pitch before it renders: the offset at the start of the samples it renders
    HostSlide f;
    HostSlide::pendingSteps() = { 4, 0.25f };
    f.start (1200.0f, 1000.0f);
    HostSlide::pendingSteps() = {};
    CHECK (std::abs (f.advanceFirst (1.0f) - 240.0f) < 1.0e-3f);   // the slide's first sample: step 0
    CHECK (std::abs (f.advanceFirst (249.0f) - 240.0f) < 1.0e-3f); // the rest of tick 0
    CHECK (std::abs (f.advanceFirst (250.0f) - 480.0f) < 1.0e-3f); // tick 1
    CHECK (std::abs (f.advanceFirst (250.0f) - 720.0f) < 1.0e-3f); // tick 2
    CHECK (std::abs (f.advanceFirst (250.0f) - 960.0f) < 1.0e-3f); // tick 3
    CHECK (std::abs (f.advanceFirst (250.0f) - 1200.0f) < 1.0e-3f); // the end
    CHECK (! f.moving());

    // without steps (porta, LYNX's own engines): the straight glide, as before
    HostSlide g;
    g.start (1200.0f, 1000.0f);
    CHECK (! g.stepping());
    CHECK (std::abs (g.advance (500.0f) - 600.0f) < 1.0e-3f);
    CHECK (std::abs (g.advanceFirst (500.0f) - 1200.0f) < 1.0e-3f); // (advanceFirst = advance for a glide: old sound kept)

    // a live pitch-knob move during a stepped slide moves the steps and the end with it
    HostSlide k;
    HostSlide::pendingSteps() = { 1, 0.5f };
    k.start (100.0f, 500.0f);
    HostSlide::pendingSteps() = {};
    k.follow (0.0f); // (not tracked: nothing)
    CHECK (std::abs (k.advance (0.0f) - 50.0f) < 1.0e-3f);
    k.jump = 30.0f;
    CHECK (std::abs (k.advance (250.0f) - 130.0f) < 1.0e-3f); // 50 + 30 jump + the last 50
    CHECK (! k.moving());
}

TEST (slides190jv_fl_natives_step_lynx_own_engines_glide)
{
    using namespace lynx;
    for (const auto k : { InstrumentKind::sampler, InstrumentKind::threeosc, InstrumentKind::sytrus, InstrumentKind::harmor,
                          InstrumentKind::directwave, InstrumentKind::flkeys, InstrumentKind::soundfont, InstrumentKind::slicex,
                          InstrumentKind::minisynth, InstrumentKind::transistorbass, InstrumentKind::fpc, InstrumentKind::patcher })
        CHECK (RackRenderer::stepsSlides (k));
    for (const auto k : { InstrumentKind::synth, InstrumentKind::kick, InstrumentKind::plugin, InstrumentKind::tetra, InstrumentKind::helix,
                          InstrumentKind::pocketSuperSaw, InstrumentKind::audioClip, InstrumentKind::layer })
        CHECK (! RackRenderer::stepsSlides (k));
}

TEST (slides190jv_sampler_slide_note_steps_on_fl_ticks)
{
    using namespace slides190jv;
    // FL tick at 120 BPM, 48 kHz: 250 samples. A slide note 2 ticks long, an octave up, at tick 48: +400 cents during
    // tick 48, +800 during tick 49, +1200 from tick 50 on (FL: n + 1 = 3 steps); before it the note's own pitch
    constexpr double tick = 250.0;
    for (const auto block : { 480, 256, 64 }) // (the rack starts a render call on each tick: any device block)
    {
        const auto x = render (lynx::InstrumentKind::sampler, 2, block);
        const auto near = [] (const std::vector<double>& v, double want) {
            return ! v.empty() && std::all_of (v.begin(), v.end(), [want] (double c) { return std::abs (c - want) < 12.0; });
        };
        // (a period that starts within one period of a step still carries some of the old pitch: skip 110 samples)
        const auto before = periodsCents (x, 40 * tick, 48 * tick);
        const auto t48 = periodsCents (x, 48 * tick + 110, 49 * tick);
        const auto t49 = periodsCents (x, 49 * tick + 110, 50 * tick);
        const auto after = periodsCents (x, 50 * tick + 110, 60 * tick);
        CHECK (near (before, 0.0));
        CHECK (near (t48, 400.0));
        CHECK (near (t49, 800.0));
        CHECK (near (after, 1200.0));
        if (! near (t48, 400.0) || ! near (t49, 800.0))
            std::printf ("slides190jv block %d: tick 48 %.0f c (%d), tick 49 %.0f c (%d)\n", block, t48.empty() ? -1.0 : t48.front(), (int) t48.size(),
                         t49.empty() ? -1.0 : t49.front(), (int) t49.size());
    }
    // a slide 4 ticks long: 240, 480, 720, 960, then 1200
    const auto x = render (lynx::InstrumentKind::sampler, 4, 480);
    int measured = 0;
    for (int k = 0; k < 4; ++k)
    {
        const auto v = periodsCents (x, (48 + k) * tick + 110, (49 + k) * tick);
        measured += v.empty() ? 0 : 1; // (a whole period fits in the rest of a tick only where the phase allows)
        for (auto c : v)
            CHECK (std::abs (c - 240.0 * (k + 1)) < 12.0);
    }
    CHECK (measured >= 2);
    const auto end = periodsCents (x, 52 * tick + 110, 60 * tick);
    CHECK (! end.empty() && std::all_of (end.begin(), end.end(), [] (double c) { return std::abs (c - 1200.0) < 12.0; }));
}
