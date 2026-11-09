// --- Round 188e: issue #113, a Patcher holding a Fruity Limiter left its track late --------------------------------
// The reporter's finding (verified): the Fruity Limiter learned its look-ahead (its latency) only in process(), and the
// Patcher plans its delay compensation right after building its nodes, before any block: it planned 0, so the track
// played the look-ahead late. Fixed at the root (the latency is known after prepare()) and in the graph (a node whose
// latency moves later makes the rack build the graph again, as the mixer does for its slots).

#include "core/DynamicsSuite.h"

namespace pdc188e
{
// where an impulse put in at `at` comes out (the largest |sample|), through `fx` in blocks of 512
int delayOf (lynx::Effect& fx, const lynx::EffectParams& params, int at = 64, int frames = 8192)
{
    std::vector<float> l ((std::size_t) frames, 0.0f), r ((std::size_t) frames, 0.0f);
    l[(std::size_t) at] = r[(std::size_t) at] = 0.25f; // (under the limiter's threshold)
    for (int done = 0; done < frames; done += 512)
        fx.process (l.data() + done, r.data() + done, std::min (512, frames - done), params, 120.0);
    int best = 0;
    for (int i = 1; i < frames; ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) best]))
            best = i;
    return best - at;
}

// FL's Fruity Limiter with an ~11 ms look-ahead (979 samples at 44.1 kHz: the delay the reporter measured)
lynx::EffectSlot limiter()
{
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::fruityLimiter);
    slot.params[3] = 0.401f; // the look-ahead control (raw 4010 of 0 … 10000)
    return slot;
}

lynx::PatchWire wire (int from, int to, int fromPort = 0, int toPort = 0)
{
    lynx::PatchWire w;
    w.from = from;
    w.to = to;
    w.fromPort = fromPort;
    w.toPort = toPort;
    return w;
}

// The audio path of FL's "Multiband Sidechain" Patcher template, as the reporter describes it: a wet path through
// Parametric EQ 2 → Fruity Limiter → a "Wet" level node, a dry path through a "Dry" level node, both to the output,
// and a Peak Controller on the side-chain input (it only measures)
lynx::EffectSlot multibandSidechain (const lynx::EffectSlot& limiterNode)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::patcher);
    slot.patcher.nodes.clear();
    slot.patcher.wires.clear();
    auto add = [&slot] (EffectSlot fx) {
        PatchNode node;
        node.effect = std::move (fx);
        slot.patcher.nodes.push_back (node);
        return (int) slot.patcher.nodes.size() - 1;
    };
    const auto dry = add (makeEffectSlot (EffectKind::fruityBalance));
    const auto eq = add (makeEffectSlot (EffectKind::parametricEq));
    const auto lim = add (limiterNode);
    const auto wet = add (makeEffectSlot (EffectKind::fruityBalance));
    const auto peak = add (makeEffectSlot (EffectKind::peakController));
    for (const auto& w : { wire (patchFromHost, dry), wire (patchFromHost, eq), wire (eq, lim), wire (lim, wet), wire (dry, patchToHost),
                           wire (wet, patchToHost), wire (patchFromHost, peak, 1, 0) })
        slot.patcher.wires.push_back (w);
    return slot;
}

// An effect that delays by `samples` but reports it only once it has run (a plugin that reports late)
class LateReporting final : public lynx::Effect
{
public:
    explicit LateReporting (int samples) : latency (samples), line (samples) {}
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float* l, float* r, int n, const lynx::EffectParams&, double) noexcept override
    {
        line.process (l, r, n);
        ran.store (true);
    }
    int latencySamples() const noexcept override { return ran.load() ? latency : 0; }

private:
    int latency;
    lynx::DelayLine line;
    std::atomic<bool> ran { false };
};

// Two channels playing an impulse, channel k on insert k + 1; renders channel `which` alone through `state` and
// returns where its impulse lands on the master
int masterPeak (const lynx::RackState& state, int which, int frames = 4096)
{
    lynx::RackRenderer renderer;
    renderer.prepare (state.sampleRate, 256);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = which;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames);
    for (int done = 0; done < frames; done += 256)
        renderer.process (&state, false, 0.0, 256, l.data() + done, r.data() + done, done == 0 ? &live : nullptr);
    int best = 0;
    for (int i = 1; i < frames; ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) best]))
            best = i;
    return best;
}

lynx::Song twoTracks (const lynx::EffectSlot& onInsert1, const lynx::EffectSlot& onInsert2)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (3);
    for (int i = 0; i < 2; ++i)
    {
        lynx::ChannelDef c;
        c.instrument = lynx::InstrumentKind::sampler;
        c.samplePath = "impulse";
        c.volume = 1.0f;
        c.mixerTrack = i + 1;
        s.channels.push_back (c);
    }
    s.patterns.emplace_back();
    s.mixer[1].effects.push_back (onInsert1);
    s.mixer[2].effects.push_back (onInsert2);
    return s;
}

lynx::RackState::Factories impulseFactories()
{
    lynx::RackState::Factories f;
    f.samples = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (64, 0.0f);
        d->left[0] = 0.25f;
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    return f;
}
} // namespace pdc188e

TEST (pdc188e_fruity_limiter_reports_its_lookahead_before_the_first_block)
{
    using namespace lynx;
    for (const double rate : { 44100.0, 48000.0 })
    {
        const auto slot = pdc188e::limiter();
        auto fx = makeEffect (slot);
        fx->prepare (rate);
        const auto before = fx->latencySamples();
        const auto measured = pdc188e::delayOf (*fx, slot.params);
        std::printf ("  Fruity Limiter at %.0f Hz: reports %d before the first block, then %d; delays %d\n", rate, before,
                     fx->latencySamples(), measured);
        CHECK (before > 900);
        CHECK (before == measured && fx->latencySamples() == before);
    }
    // Fruity Compressor: its reported look-ahead follows the slot from the start (64 with it on, 0 with it off)
    auto comp = makeEffectSlot (EffectKind::fruityCompressor);
    for (const bool on : { true, false })
    {
        comp.params[fldyn::compressorControls] = on ? 1.0f : 0.0f;
        auto fx = makeEffect (comp);
        fx->prepare (44100.0);
        CHECK (fx->latencySamples() == (on ? 64 : 0));
    }
}

TEST (pdc188e_effects_report_their_latency_before_the_first_block)
{
    // Every built-in effect, at its defaults and at random settings (look-ahead / mode / oversampling switches among
    // them): what it reports after prepare(), before any block, is what it reports once it has run with those settings.
    // A Patcher plans its compensation from the first value (issue #113 found the Fruity Limiter reporting 0 there).
    using namespace lynx;
    std::uint32_t seed = 0x188e113u;
    auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (float) (seed >> 8) * (1.0f / 16777216.0f);
    };
    int checked = 0;
    for (int k = 0; k <= (int) lastEffectKind; ++k)
    {
        const auto kind = (EffectKind) k;
        if (kind == EffectKind::plugin || kind == EffectKind::patcher || kind == EffectKind::hardwareInsert)
            continue;
        for (int variant = 0; variant < 5; ++variant)
        {
            auto slot = makeEffectSlot (kind);
            if (variant > 0)
                for (auto& v : slot.params)
                    v = next();
            auto fx = makeEffect (slot);
            if (fx == nullptr)
                continue;
            fx->prepare (44100.0);
            const auto before = fx->latencySamples();
            std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
            l[10] = r[10] = 0.25f;
            for (int done = 0; done < 1024; done += 512)
                fx->process (l.data() + done, r.data() + done, 512, slot.params, 120.0);
            const auto after = fx->latencySamples();
            if (before != after)
                std::printf ("  %s (settings %d): %d before the first block, %d after\n", effectInfo (kind).name, variant, before, after);
            CHECK (before == after);
            ++checked;
        }
    }
    CHECK (checked > 500);
}

TEST (pdc188e_patcher_with_a_limiter_aligns_to_the_sample)
{
    using namespace lynx;
    const auto slot = pdc188e::multibandSidechain (pdc188e::limiter());
    auto graph = patcher::makeEffect (slot, 44100.0, patcher::builtInFactories (44100.0));
    graph->prepare (44100.0);
    auto alone = makeEffect (pdc188e::limiter());
    alone->prepare (44100.0);
    const auto lookahead = alone->latencySamples();
    const auto reported = graph->latencySamples();

    // the impulse leaves the Patcher once (the dry path waits for the wet one), as late as the Patcher reports
    std::vector<float> l (8192, 0.0f), r (8192, 0.0f);
    l[64] = r[64] = 0.25f;
    for (int done = 0; done < 8192; done += 512)
        graph->process (l.data() + done, r.data() + done, 512, slot.params, 120.0);
    int best = 0;
    for (int i = 1; i < 8192; ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) best]))
            best = i;
    std::printf ("  Multiband Sidechain Patcher: limiter %d, Patcher reports %d, delays %d, dry at the input %.4f\n", lookahead, reported,
                 best - 64, (double) l[64]);
    CHECK (lookahead > 900 && reported == lookahead && best - 64 == reported);
    CHECK (std::abs (l[64]) < 1e-6f); // (no undelayed dry copy)
    CHECK (graph->latencySamples() == reported && ! patcher::graphOf (graph.get())->latencyStale (&slot.patcher));

    // the limiter node switched off in the document: the plan no longer holds, the rack builds the graph again
    auto off = slot;
    off.patcher.nodes[2].effect.enabled = false;
    CHECK (patcher::graphOf (graph.get())->latencyStale (&off.patcher));
    CHECK (! patcher::acceptValues (graph.get(), off));
    CHECK (patcher::acceptValues (graph.get(), slot));
    auto rebuilt = patcher::makeEffect (off, 44100.0, patcher::builtInFactories (44100.0), graph.get());
    CHECK (rebuilt->latencySamples() == 0);
}

TEST (pdc188e_track_with_the_patcher_matches_the_bypassed_track)
{
    // The reporter's measurement: the track through the Patcher against the same track with the Patcher bypassed,
    // offset 0 samples (it was 979)
    using namespace lynx;
    for (const double rate : { 44100.0, 48000.0 })
    {
        const auto patch = pdc188e::multibandSidechain (pdc188e::limiter());
        auto bypassed = patch;
        bypassed.enabled = false;
        const auto song = pdc188e::twoTracks (patch, bypassed);
        const auto state = RackState::build (song, nullptr, rate, pdc188e::impulseFactories());
        const auto through = pdc188e::masterPeak (*state, 0);
        const auto plain = pdc188e::masterPeak (*state, 1);
        std::printf ("  at %.0f Hz: PDC total %d, the Patcher's track peaks at %d, the bypassed one at %d\n", rate, state->pdc.total, through,
                     plain);
        CHECK (state->pdc.total > 900);
        CHECK (through == plain);
        CHECK (state->currentLatencies() == state->latencies); // (nothing for the mixer to re-plan)

        // the same limiter straight on the insert, for reference: compensated too
        const auto direct = RackState::build (pdc188e::twoTracks (pdc188e::limiter(), bypassed), nullptr, rate, pdc188e::impulseFactories());
        CHECK (direct->pdc.total == state->pdc.total);
        CHECK (pdc188e::masterPeak (*direct, 0) == pdc188e::masterPeak (*direct, 1));
    }
}

TEST (pdc188e_patcher_replans_when_a_node_reports_its_latency_late)
{
    // A node that only reports its latency once it runs (a hosted plugin can): the Patcher's reported latency follows
    // it, the mixer sees the change, and the rack's next build plans the graph again with the running node kept
    using namespace lynx;
    auto patch = makeEffectSlot (EffectKind::patcher);
    patch.patcher.nodes.clear();
    patch.patcher.wires.clear();
    PatchNode late;
    late.effect.kind = EffectKind::plugin;
    late.effect.plugin.id = "late";
    PatchNode dry;
    dry.effect = makeEffectSlot (EffectKind::fruityBalance);
    patch.patcher.nodes = { late, dry };
    patch.patcher.wires = { pdc188e::wire (patchFromHost, 0), pdc188e::wire (patchFromHost, 1), pdc188e::wire (0, patchToHost),
                            pdc188e::wire (1, patchToHost) };
    auto bypassed = patch;
    bypassed.enabled = false;
    const auto song = pdc188e::twoTracks (patch, bypassed);

    auto f = pdc188e::impulseFactories();
    f.pluginEffects = [] (const PluginRef&, double) -> std::shared_ptr<Effect> { return std::make_shared<pdc188e::LateReporting> (300); };
    const auto first = RackState::build (song, nullptr, 48000.0, f);
    CHECK (first->pdc.total == 0); // (the node hasn't reported yet)
    pdc188e::masterPeak (*first, 0); // it runs
    const auto* graph = patcher::graphOf (first->effects[1][0].get());
    CHECK (graph != nullptr && graph->latencyStale() && first->effects[1][0]->latencySamples() == 300);
    CHECK (first->currentLatencies() != first->latencies); // the mixer's poll re-plans

    const auto next = RackState::build (song, first.get(), 48000.0, f);
    CHECK (next->effects[1][0] != first->effects[1][0]); // the graph was built again…
    const auto* again = patcher::graphOf (next->effects[1][0].get());
    CHECK (again != nullptr && again->effectAt (0) == graph->effectAt (0)); // …keeping the running node
    CHECK (next->pdc.total == 300 && next->effects[1][0]->latencySamples() == 300 && ! again->latencyStale());
    const auto through = pdc188e::masterPeak (*next, 0);
    const auto plain = pdc188e::masterPeak (*next, 1);
    std::printf ("  late-reporting node: the Patcher's track peaks at %d, the bypassed one at %d\n", through, plain);
    CHECK (through == plain);

    // nothing moved since: the next build keeps the running graph
    const auto third = RackState::build (song, next.get(), 48000.0, f);
    CHECK (third->effects[1][0] == next->effects[1][0]);
}

TEST (pdc188e_dynamics_suite_sound_unchanged_by_the_early_latency)
{
    // Crest / Prism / Sentry now keep the slot's settings through prepare() (their latency is known before the first
    // block). The sound must not move: an instance built with the settings renders bit-identical to one built without
    // them that gets them on its first block (what every slot did before)
    using namespace lynx;
    std::uint32_t seed = 0x188e0d1u;
    auto next = [&seed] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return (float) (seed >> 8) * (1.0f / 16777216.0f);
    };
    for (const auto kind : { EffectKind::crestCompressor, EffectKind::prismMultiband, EffectKind::sentryGate })
        for (int variant = 0; variant < 6; ++variant)
        {
            auto slot = makeEffectSlot (kind);
            if (variant > 0)
                for (auto& v : slot.params)
                    v = next();
            auto early = dyn::make (kind, &slot.params);
            auto late = dyn::make (kind, nullptr);
            early->prepare (44100.0);
            late->prepare (44100.0);
            std::vector<float> a (4096), b (4096), c (4096), d (4096);
            for (int i = 0; i < 4096; ++i)
            {
                a[(std::size_t) i] = c[(std::size_t) i] = 0.6f * std::sin ((float) i * 0.05f) * (i % 900 < 450 ? 1.0f : 0.1f);
                b[(std::size_t) i] = d[(std::size_t) i] = 0.5f * std::sin ((float) i * 0.031f);
            }
            for (int done = 0; done < 4096; done += 512)
            {
                early->process (a.data() + done, b.data() + done, 512, slot.params, 120.0);
                late->process (c.data() + done, d.data() + done, 512, slot.params, 120.0);
            }
            CHECK (early->latencySamples() == late->latencySamples());
            CHECK (std::memcmp (a.data(), c.data(), a.size() * sizeof (float)) == 0 && std::memcmp (b.data(), d.data(), b.size() * sizeof (float)) == 0);
        }
}
