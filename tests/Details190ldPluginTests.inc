// Round 190ld: FL-native plugin details (DP-32 Fruity Vocoder band levels, DP-61/62 Patcher View › Activity /
// Performance). Core only; the editors' drawing is checked with shots.

// DP-32: Fruity Vocoder publishes its band levels (outputs 0…7, four 6-bit columns each) only while an editor shows
// them (setMetersShown); a signal lights some columns; HOLD freezes them
TEST (details190ld_vocoder_publishes_band_levels_while_shown)
{
    auto fx = lynx::makeEffect (lynx::EffectKind::vocoder);
    fx->prepare (48000.0);
    auto slot = lynx::makeEffectSlot (lynx::EffectKind::vocoder);
    std::vector<float> l (512), r (512);
    auto run = [&] (int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 512; ++i)
            {
                const auto t = (float) (b * 512 + i) / 48000.0f;
                l[(std::size_t) i] = 0.5f * std::sin (6.2831853f * 440.0f * t); // modulator
                r[(std::size_t) i] = 0.5f * std::sin (6.2831853f * 110.0f * t); // carrier
            }
            fx->process (l.data(), r.data(), 512, slot.params, 120.0);
        }
    };
    run (20);
    for (int o = 0; o < 8; ++o)
        CHECK (fx->controlOutput (o) == 0.0f); // nobody watches: nothing published
    fx->setMetersShown (true);
    run (2);
    std::uint32_t lit = 0;
    for (int o = 0; o < 8; ++o)
    {
        const auto v = fx->controlOutput (o);
        CHECK (v >= 0.0f && v < 16777216.0f && v == std::floor (v)); // an exact integer
        lit |= (std::uint32_t) v;
    }
    CHECK (lit != 0u);
    // HOLD (parameter 12, FL's hold control) freezes the envelopes, so the published levels stay
    slot.params[12] = 1.0f;
    run (1);
    std::array<float, 8> held {};
    for (int o = 0; o < 8; ++o)
        held[(std::size_t) o] = fx->controlOutput (o);
    run (4);
    for (int o = 0; o < 8; ++o)
        CHECK (fx->controlOutput (o) == held[(std::size_t) o]);
}

// DP-61/62: a Patcher graph measures nothing until an editor watches it; then each node's output peak and process time
// and the whole graph's time; the audio is the same either way
TEST (details190ld_patcher_graph_measures_only_while_watched)
{
    using namespace lynx;
    PatcherDoc d;
    PatchNode n;
    n.effect = makeEffectSlot (EffectKind::fruityFilter);
    const auto node = patcher::addNode (d, n);
    PatchWire in;
    in.from = patchFromHost;
    in.to = node;
    d.wires.push_back (in);
    PatchWire out;
    out.from = node;
    out.to = patchToHost;
    d.wires.push_back (out);
    patcher::Graph a (d, 48000.0, patcher::builtInFactories (48000.0)), b (d, 48000.0, patcher::builtInFactories (48000.0));
    std::vector<float> la (256), ra (256), lb (256), rb (256);
    auto fill = [] (std::vector<float>& x, std::vector<float>& y, int block) {
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = y[i] = 0.25f * std::sin (0.05f * (float) ((std::size_t) block * x.size() + i));
    };
    fill (la, ra, 0);
    a.process (la.data(), ra.data(), nullptr, nullptr, la.data(), ra.data(), 256, 120.0);
    CHECK (a.takeNodePeak (node) == 0.0f && a.nodeNanoseconds (node) == 0u && a.watchedNanoseconds() == 0u);
    fill (lb, rb, 0);
    b.process (lb.data(), rb.data(), nullptr, nullptr, lb.data(), rb.data(), 256, 120.0);
    a.watch(); // (b is never watched)
    fill (la, ra, 1);
    fill (lb, rb, 1);
    a.process (la.data(), ra.data(), nullptr, nullptr, la.data(), ra.data(), 256, 120.0);
    b.process (lb.data(), rb.data(), nullptr, nullptr, lb.data(), rb.data(), 256, 120.0);
    CHECK (la == lb && ra == rb); // watching changes no sample
    CHECK (a.takeNodePeak (node) > 0.0f);
    CHECK (a.takeNodePeak (node) == 0.0f); // taken
    CHECK (a.takeInputPeak() > 0.0f);
    CHECK (a.watchedFrames() == 256u);
    CHECK (a.watchedNanoseconds() >= a.nodeNanoseconds (node));
    CHECK (b.watchedFrames() == 0u && b.takeNodePeak (node) == 0.0f);
    CHECK (a.takeNodePeak (99) == 0.0f && a.nodeNanoseconds (-1) == 0u); // out of range
}
