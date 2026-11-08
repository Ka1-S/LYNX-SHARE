// --- Round 182e: issue #113, a track with an imported Patcher plays late (diagnostic) -----------------------------------

namespace flp182e
{
// where the impulse put in at `at` comes out (the largest |sample|), through `fx` in blocks of 512
int measuredDelay (lynx::Effect& fx, const lynx::EffectParams& params = {}, int at = 64, int frames = 16384)
{
    std::vector<float> l ((std::size_t) frames, 0.0f), r ((std::size_t) frames, 0.0f);
    l[(std::size_t) at] = r[(std::size_t) at] = 1.0f;
    for (int done = 0; done < frames; done += 512)
        fx.process (l.data() + done, r.data() + done, std::min (512, frames - done), params, 120.0);
    int best = 0;
    for (int i = 1; i < frames; ++i)
        if (std::abs (l[(std::size_t) i]) > std::abs (l[(std::size_t) best]))
            best = i;
    return best - at;
}
} // namespace flp182e

TEST (flp182e_patcher_reports_the_delay_it_adds)
{
    // Issue #113 (a reporter-like Patcher: FL-mode Parametric EQ 2 nodes in series): every processing mode, alone in a
    // slot and 14 in series inside a Patcher. What the effect reports must be the delay it adds, or the mixer's PDC
    // leaves the track late by the difference.
    using namespace lynx;
    const auto state = issues171a::peq2State();
    const auto parsed = peqfl::parsePeq2 (state);
    CHECK (parsed.has_value());
    if (! parsed)
        return;
    const peqfl::Processing modes[] = { peqfl::Processing::plain, peqfl::Processing::hq, peqfl::Processing::hqExternal, peqfl::Processing::hqAlternative };
    for (const bool linear : { false, true })
        for (const auto mode : modes)
        {
            auto s = *parsed;
            s.mask = 0; // every band off: only the processing path (resamplers / FIR) is left
            s.processing = mode;
            s.linearPhase = linear;
            auto slot = makeEffectSlot (EffectKind::parametricEq);
            peqfl::toParams (s, slot.params);

            auto alone = makeEffect (slot);
            alone->prepare (44100.0);
            const auto aloneReported = alone->latencySamples();
            const auto aloneMeasured = flp182e::measuredDelay (*alone, slot.params);

            auto patcher = makeEffectSlot (EffectKind::patcher);
            patcher.patcher.nodes.clear();
            patcher.patcher.wires.clear();
            for (int i = 0; i < 14; ++i)
            {
                PatchNode node;
                node.effect = slot;
                patcher.patcher.nodes.push_back (node);
                PatchWire w;
                w.from = i == 0 ? patchFromHost : i - 1;
                w.to = i;
                patcher.patcher.wires.push_back (w);
            }
            PatchWire out;
            out.from = 13;
            out.to = patchToHost;
            patcher.patcher.wires.push_back (out);
            auto graph = patcher::makeEffect (patcher, 44100.0, patcher::builtInFactories (44100.0));
            graph->prepare (44100.0);
            const auto graphReported = graph->latencySamples();
            const auto graphMeasured = flp182e::measuredDelay (*graph);
            const auto graphAfter = graph->latencySamples();
            std::printf ("  PEQ2 FL mode %d linear %d: alone reports %d, delays %d; 14 in a Patcher report %d (then %d), delay %d\n",
                         (int) mode, (int) linear, aloneReported, aloneMeasured, graphReported, graphAfter, graphMeasured);
            // (HQ's IIR half-band resamplers add a few samples of group delay that FL doesn't report either: not latency)
            CHECK (std::abs (aloneReported - aloneMeasured) <= 3);
            CHECK (std::abs (graphAfter - graphMeasured) <= 3 * 14);
            CHECK (graphReported == 14 * aloneReported && graphAfter == graphReported);
        }
}
