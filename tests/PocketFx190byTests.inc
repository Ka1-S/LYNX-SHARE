// Round 190by: FL Studio Mobile's remaining effect modules (core/PocketFxD.cpp and the mappings of core/FlmEffects.cpp).
// The expected values are worked by hand from FLM's laws in docs/analise/REA_rodada190by.md. Uses the .flm builders of
// FlmImport182cTests.inc and PocketSuperSaw190byTests.inc.

namespace fx190by
{
struct Stereo
{
    std::vector<float> l, r;
};

// `input` through a fresh engine at `fs`, in blocks of 256
inline Stereo run (lynx::EffectKind kind, std::initializer_list<std::pair<int, float>> values, const Stereo& input, double fs = 48000.0,
                   const lynx::NoteMask* held = nullptr, double bpm = 120.0)
{
    using namespace lynx;
    auto fx = makeEffect (kind);
    fx->prepare (fs);
    auto p = makeEffectSlot (kind).params;
    for (const auto& [k, v] : values)
        p[(std::size_t) k] = v;
    auto out = input;
    const int n = (int) out.l.size();
    for (int at = 0; at < n; at += 256)
    {
        if (held != nullptr)
            fx->setHeldNotes (held, 1);
        fx->process (out.l.data() + at, out.r.data() + at, std::min (256, n - at), p, bpm);
    }
    return out;
}

inline Stereo silence (int n) { return { std::vector<float> ((std::size_t) n, 0.0f), std::vector<float> ((std::size_t) n, 0.0f) }; }
// silence with one sample of 1 at `at` on both sides
inline Stereo impulse (int n, int at)
{
    auto s = silence (n);
    s.l[(std::size_t) at] = s.r[(std::size_t) at] = 1.0f;
    return s;
}
inline double sum (const std::vector<float>& x, int from, int to)
{
    double a = 0.0;
    for (int i = std::max (from, 0); i < std::min (to, (int) x.size()); ++i)
        a += (double) x[(std::size_t) i];
    return a;
}
// the centre of the samples in [from, to), as a position
inline double centre (const std::vector<float>& x, int from, int to)
{
    double a = 0.0, w = 0.0;
    for (int i = std::max (from, 0); i < std::min (to, (int) x.size()); ++i)
        a += (double) i * (double) x[(std::size_t) i], w += (double) x[(std::size_t) i];
    return w != 0.0 ? a / w : 0.0;
}
inline bool allZero (const Stereo& s)
{
    return std::all_of (s.l.begin(), s.l.end(), [] (float v) { return v == 0.0f; }) && std::all_of (s.r.begin(), s.r.end(), [] (float v) { return v == 0.0f; });
}
inline bool finite (const Stereo& s)
{
    return std::all_of (s.l.begin(), s.l.end(), [] (float v) { return std::isfinite (v); }) && std::all_of (s.r.begin(), s.r.end(), [] (float v) { return std::isfinite (v); });
}

// an FLM module of `type` with the given PRMS values and the wrapper's Post Gain / Mix / Wet Pan at rest
inline flm182c::Bytes module (int type, const std::vector<float>& values)
{
    namespace t = flm182c;
    t::Bytes b, h, desc, prst, prms, smpr;
    t::u32 (b, (std::uint32_t) type);
    t::u32 (b, 3);
    t::u32 (h, (std::uint32_t) type);
    t::u32 (h, 0);
    t::u8 (h, 1);
    const t::Bytes add1 { 0, 0, 127 };
    t::fixed (desc, "Default", 256);
    t::fixed (desc, "", 256);
    t::lp32 (prst, "");
    t::lp32 (prst, "Default/Default");
    for (const auto v : values)
        t::f32 (prms, v);
    for (const auto v : { 0.5f, 1.0f, 0.5f })
        t::f32 (smpr, v);
    return t::chunk ("RMOd", t::cat ({ b, t::chunk ("HEAD", h), t::chunk ("DESC", desc), t::chunk ("PRST", prst), t::chunk ("ADD1", add1),
                                       t::chunk ("PRMS", prms), t::chunk ("SMPR", smpr) }));
}

// a one-channel .flm: a SuperSaw followed by the effect module; the imported song's effect slots of that channel's insert
inline std::vector<lynx::EffectSlot> importedSlots (int type, const std::vector<float>& values, lynx::FlpImportReport* report = nullptr)
{
    namespace t = flm182c;
    using namespace lynx;
    t::Bytes f, m;
    t::tag (f, "10LF");
    t::fixed (m, "2025-07-06 11-39-10", 256);
    t::f64 (m, 0);
    t::fixed (m, "", 256);
    t::fixed (m, "Fx", 256);
    t::u8 (m, 0);
    t::fixed (m, "", 2048);
    t::fixed (m, "2026-11-11 15:00:00", 256);
    const t::Bytes tdiv { 4, 4 };
    const auto bytes = t::cat ({ f, t::head ("Fx", 120.0), t::chunk ("META", m), t::chunk ("TDIV", tdiv), t::rack (flm::kindMaster, {}),
                                 t::rack (flm::kindNormal, { saw190by::sawModule (saw190by::defaultsVector()), module (type, values) }),
                                 t::channel (flm::kindMaster, "MASTER", 0, {}),
                                 t::channel (flm::kindNormal, "Lead", 1, { t::lane (0, "Lead", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60 } }) }) }) });
    FlpImportReport local;
    const auto song = importFlm (bytes, flm182c::options (report != nullptr ? report : &local), nullptr);
    std::vector<EffectSlot> out;
    if (! song)
        return out;
    for (const auto& track : song->mixer)
        for (const auto& slot : track.effects)
            out.push_back (slot);
    return out;
}
} // namespace fx190by

TEST (pocket190by_comb_laws_and_texts)
{
    using namespace lynx;
    CHECK (pocket::combMode (0.0f) == 0 && pocket::combMode (0.5f) == 1 && pocket::combMode (1.0f) == 2 && pocket::combMode (0.49f) == 0);
    CHECK (pocket::combFreeHz (0.0f) == 1.0f && pocket::combFreeHz (1.0f) == 2500.0f);
    CHECK (std::fabs (pocket::combFreeHz (0.5f) - 157.1875f) < 1e-3f);
    CHECK (std::fabs (pocket::combFreeHz (0.33f) - 30.636f) < 0.01f);
    // the defaults' note: octave round(3.5) = 4, note round(4.4) = 4 → 52 = E, 164.81 Hz
    CHECK (pocket::combNote (0.5f, 0.4f) == 52 && std::fabs (pocket::combNoteHz (52) - 164.814f) < 0.01f);
    CHECK (pocket::combNote (0.0f, 0.0f) == 0 && pocket::combNoteHz (0) == 0.0f);
    CHECK (pocket::combNote (1.0f, 1.0f) == 95 && pocket::combNoteHz (69) == 440.0f);
    CHECK (pocket::combOffsetOctaves (0.5f) == 0 && pocket::combOffsetOctaves (0.0f) == -2 && pocket::combOffsetOctaves (1.0f) == 2
           && pocket::combOffsetOctaves (0.214f) == -1 && pocket::combOffsetOctaves (0.755f) == 1);
    CHECK (std::fabs (pocket::combGlide (0.0f) - 0.2f) < 1e-6f && std::fabs (pocket::combGlide (1.0f) - 0.0001f) < 1e-7f
           && std::fabs (pocket::combGlide (0.25f) - 0.18751f) < 1e-5f);
    CHECK (pocket::combPolarity (0.0f) == 0 && pocket::combPolarity (1.0f / 3.0f) == 1 && pocket::combPolarity (2.0f / 3.0f) == 2
           && pocket::combPolarity (1.0f) == 3);
    // one loop: (⌊fs⌋ − 2) / f + 3 samples — not fs / f
    CHECK (std::fabs (pocket::combDelaySamples (164.814, 44100.0) - 270.56) < 0.01);
    CHECK (std::fabs (pocket::combDelaySamples (1000.0, 44100.0) - 47.098) < 0.001);

    const auto& info = effectInfo (EffectKind::pocketComb);
    CHECK (std::string (info.name) == "Pocket Comb Filter" && (int) info.params.size() == pocket::combParams);
    const float defaults[] = { 0.0f, 0.33f, 0.8f, 0.75f, 0.5f, 0.25f, 1.0f, 0.0f, 0.5f, 0.5f, 0.4f };
    const auto slot = makeEffectSlot (EffectKind::pocketComb);
    for (int i = 0; i < pocket::combParams; ++i)
        CHECK (slot.params[(std::size_t) i] == defaults[i] && info.params[(std::size_t) i].defaultValue == defaults[i]);
    const auto K = EffectKind::pocketComb;
    CHECK (formatEffectParam (K, 0, 0.0f, 120.0) == "Manual" && formatEffectParam (K, 0, 0.5f, 120.0) == "Lowest Note"
           && formatEffectParam (K, 0, 1.0f, 120.0) == "Detect Pitch");
    CHECK (formatEffectParam (K, 1, 0.5f, 120.0) == "157.2 Hz" && formatEffectParam (K, 1, 1.0f, 120.0) == "2.50 kHz");
    CHECK (formatEffectParam (K, 2, 0.8f, 120.0) == "80 %" && formatEffectParam (K, 3, 0.75f, 120.0) == "75 %");
    CHECK (formatEffectParam (K, 4, 0.5f, 120.0) == "0 Oct" && formatEffectParam (K, 4, 1.0f, 120.0) == "+2 Oct"
           && formatEffectParam (K, 4, 0.0f, 120.0) == "-2 Oct");
    CHECK (formatEffectParam (K, 6, 1.0f, 120.0) == "On" && formatEffectParam (K, 6, 0.0f, 120.0) == "Off");
    CHECK (formatEffectParam (K, 7, 0.0f, 120.0) == "Both +" && formatEffectParam (K, 7, 1.0f / 3.0f, 120.0) == "Left -"
           && formatEffectParam (K, 7, 2.0f / 3.0f, 120.0) == "Right -" && formatEffectParam (K, 7, 1.0f, 120.0) == "Both -");
    CHECK (formatEffectParam (K, 8, 1.0f, 120.0) == "5.0 %" && formatEffectParam (K, 8, 0.5f, 120.0) == "0.0 %");
    CHECK (formatEffectParam (K, 9, 0.5f, 120.0) == "3" && formatEffectParam (K, 10, 0.4f, 120.0) == "E" && formatEffectParam (K, 10, 0.0f, 120.0) == "C");
}

TEST (pocket190by_comb_tuning_level_and_sample_rates)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketComb;
    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int at = (int) fs; // one second in: the tape's speed has settled
        const int n = at + (int) (0.25 * fs);
        // Manual, Snap off: Pitch 0.5 = 157.19 Hz; no feedback, Mix 0.5: the dry untouched, one echo of half the level one
        // loop later, spread by linear interpolation over the two samples around (⌊fs⌋ − 2) / f + 3
        {
            const auto d = pocket::combDelaySamples (157.1875, fs);
            const auto y = run (K, { { 1, 0.5f }, { 2, 0.0f }, { 3, 0.5f }, { 5, 0.0f }, { 6, 0.0f } }, impulse (n, at), fs);
            CHECK (y.l[(std::size_t) at] == 1.0f && y.r[(std::size_t) at] == 1.0f);
            CHECK (std::fabs (sum (y.l, at + 1, n) - 0.5) < 1e-3);
            CHECK (std::fabs (centre (y.l, at + 1, n) - ((double) at + d)) < 0.02);
            CHECK (std::fabs (sum (y.l, at + (int) d, at + (int) d + 2) - 0.5) < 1e-3); // all of it on those two samples
            CHECK (y.l == y.r);
        }
        // feedback 0.5, Mix 1: echo k is 0.5^(k − 1), k loops later
        {
            const auto d = pocket::combDelaySamples (157.1875, fs);
            const auto y = run (K, { { 1, 0.5f }, { 2, 0.5f }, { 3, 1.0f }, { 5, 0.0f }, { 6, 0.0f } }, impulse (n, at), fs);
            for (int k = 1; k <= 3; ++k)
            {
                const int a = at + (int) ((double) k * d) - 3, b = a + 7;
                CHECK (std::fabs (sum (y.l, a, b) - std::pow (0.5, k - 1)) < 2e-3);
                CHECK (std::fabs (centre (y.l, a, b) - ((double) at + (double) k * d)) < 0.05);
            }
        }
        // Snap on (the default): the note of Pitch oct / Pitch note, 164.81 Hz; Offset does nothing in Manual
        {
            const auto d = pocket::combDelaySamples ((double) pocket::combNoteHz (52), fs);
            const auto y = run (K, { { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f }, { 4, 1.0f } }, impulse (n, at), fs);
            CHECK (std::fabs (centre (y.l, at + 1, n) - ((double) at + d)) < 0.02);
            CHECK (std::fabs (sum (y.l, at + 1, n) - 1.0) < 1e-3);
        }
    }
}

TEST (pocket190by_comb_polarity_asymmetry_and_modes)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketComb;
    const double fs = 48000.0;
    const int at = 48000, n = 60000;
    const auto d = pocket::combDelaySamples (157.1875, fs);
    // Polarity: the feedback's sign per side (the first echo is the input itself, the second one carries the sign)
    for (int pol = 0; pol < 4; ++pol)
    {
        const auto y = run (K, { { 1, 0.5f }, { 2, 0.5f }, { 3, 1.0f }, { 5, 0.0f }, { 6, 0.0f }, { 7, (float) pol / 3.0f } }, impulse (n, at), fs);
        const int a = at + (int) (2.0 * d) - 3, b = a + 7;
        CHECK (std::fabs (sum (y.l, a, b) - ((pol & 1) != 0 ? -0.5 : 0.5)) < 2e-3);
        CHECK (std::fabs (sum (y.r, a, b) - ((pol & 2) != 0 ? -0.5 : 0.5)) < 2e-3);
        CHECK (std::fabs (sum (y.l, at + (int) d - 3, at + (int) d + 4) - 1.0) < 2e-3);
    }
    // Asymmetry 1 = +5 %: the left side 5 % lower (a longer loop), the right side 5 % higher
    {
        const auto y = run (K, { { 1, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f }, { 6, 0.0f }, { 8, 1.0f } }, impulse (n, at), fs);
        CHECK (std::fabs (centre (y.l, at + 1, n) - ((double) at + pocket::combDelaySamples (157.1875 * 0.95, fs))) < 0.03);
        CHECK (std::fabs (centre (y.r, at + 1, n) - ((double) at + pocket::combDelaySamples (157.1875 * 1.05, fs))) < 0.03);
    }
    // Lowest Note: the lowest key held on the Channel Rack, with Offset in octaves; the last key while none is held
    {
        NoteMask held {};
        held[0] |= 1ull << 48;
        held[0] |= 1ull << 60;
        const auto y = run (K, { { 0, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f } }, impulse (n, at), fs, &held);
        CHECK (std::fabs (centre (y.l, at + 1, n) - ((double) at + pocket::combDelaySamples ((double) pocket::combNoteHz (48), fs))) < 0.03);
        const auto up = run (K, { { 0, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f }, { 4, 0.75f } }, impulse (n, at), fs, &held);
        CHECK (std::fabs (centre (up.l, at + 1, n) - ((double) at + pocket::combDelaySamples ((double) pocket::combNoteHz (60), fs))) < 0.03);
        // no key yet: the note of Pitch oct / Pitch note (E3 = key 52 at the defaults, as FLM inside FL plays it)
        NoteMask none {};
        const auto first = run (K, { { 0, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f } }, impulse (n, at), fs, &none);
        CHECK (std::fabs (centre (first.l, at + 1, n) - ((double) at + pocket::combDelaySamples ((double) pocket::combNoteHz (52), fs))) < 0.03);
        const auto other = run (K, { { 0, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f }, { 9, 5.0f / 7.0f }, { 10, 0.0f } }, impulse (n, at), fs, &none);
        CHECK (std::fabs (centre (other.l, at + 1, n) - ((double) at + pocket::combDelaySamples ((double) pocket::combNoteHz (60), fs))) < 0.03);
        // a key held, then released: the comb stays on it
        auto fx = makeEffect (K);
        fx->prepare (fs);
        auto p = makeEffectSlot (K).params;
        p[0] = 0.5f, p[2] = 0.0f, p[3] = 1.0f, p[5] = 0.0f;
        NoteMask key {};
        key[1] |= 1ull << (69 - 64);
        auto in = impulse (n, at);
        for (int pos = 0; pos < n; pos += 256)
        {
            fx->setHeldNotes (pos < 4800 ? &key : &none, 1);
            fx->process (in.l.data() + pos, in.r.data() + pos, std::min (256, n - pos), p, 120.0);
        }
        CHECK (std::fabs (centre (in.l, at + 1, n) - ((double) at + pocket::combDelaySamples (440.0, fs))) < 0.03);
    }
    // Detect Pitch: the period of a block, and the comb following a tone it hears (kept through the silence after it)
    {
        std::vector<float> block (2048);
        for (int i = 0; i < 2048; ++i)
            block[(std::size_t) i] = (float) std::sin (6.283185307179586 * (double) i / 100.25) + 0.4f * (float) std::sin (6.283185307179586 * 2.0 * (double) i / 100.25);
        CHECK (std::fabs (pocket::combDetectPeriod (block.data(), fs) - 100.25f) < 0.05f);
        const std::vector<float> quiet (2048, 0.0f);
        CHECK (pocket::combDetectPeriod (quiet.data(), fs) == 0.0f || std::isfinite (pocket::combDetectPeriod (quiet.data(), fs)));

        const int toneEnd = 36000, hit = 60000, total = 72000;
        auto in = silence (total);
        for (int i = 0; i < toneEnd; ++i)
            in.l[(std::size_t) i] = in.r[(std::size_t) i] = 0.25f * (float) std::sin (6.283185307179586 * 220.0 * (double) i / fs);
        in.l[(std::size_t) hit] = in.r[(std::size_t) hit] = 1.0f;
        const auto y = run (K, { { 0, 1.0f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f } }, in, fs);
        // (the detector reads 220 Hz to a fraction of a hertz: the loop within half a sample)
        CHECK (std::fabs (centre (y.l, hit + 1, total) - ((double) hit + pocket::combDelaySamples (220.0, fs))) < 0.5);
        CHECK (std::fabs (sum (y.l, hit + 1, total) - 1.0) < 1e-2);
        // an out-of-phase signal is read from one side instead of the (silent) middle
        auto wide = silence (total);
        for (int i = 0; i < toneEnd; ++i)
        {
            wide.l[(std::size_t) i] = 0.25f * (float) std::sin (6.283185307179586 * 330.0 * (double) i / fs);
            wide.r[(std::size_t) i] = -wide.l[(std::size_t) i];
        }
        wide.l[(std::size_t) hit] = wide.r[(std::size_t) hit] = 1.0f;
        const auto z = run (K, { { 0, 1.0f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f } }, wide, fs);
        CHECK (std::fabs (centre (z.l, hit + 1, total) - ((double) hit + pocket::combDelaySamples (330.0, fs))) < 0.5);
    }
}

TEST (pocket190by_comb_silence_glide_and_stability)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketComb;
    for (const auto fs : { 44100.0, 96000.0 })
    {
        // silence in, silence out — in every mode, at full feedback
        for (const auto mode : { 0.0f, 0.5f, 1.0f })
            CHECK (allZero (run (K, { { 0, mode }, { 2, 1.0f }, { 3, 1.0f } }, silence ((int) fs), fs)));
        // full feedback, both polarities negative, the extremes of every knob: finite and bounded on a burst
        auto burst = silence ((int) fs);
        for (int i = 0; i < 2000; ++i)
            burst.l[(std::size_t) i] = burst.r[(std::size_t) i] = 0.5f * (float) std::sin (0.05 * (double) i);
        for (const auto pitch : { 0.0f, 1.0f })
        {
            const auto y = run (K, { { 1, pitch }, { 2, 1.0f }, { 3, 1.0f }, { 5, 1.0f }, { 6, 0.0f }, { 7, 1.0f }, { 8, 0.0f } }, burst, fs);
            CHECK (finite (y));
            CHECK (*std::max_element (y.l.begin(), y.l.end()) < 50.0f);
        }
    }
    // Smoothing: a slower glide reaches the pitch later (Smoothing 1 = 0.0001 per sample: far from it after 0.1 s)
    {
        const double fs = 48000.0;
        const int at = 4800, n = 24000;
        const auto fast = run (K, { { 1, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 0.0f }, { 6, 0.0f } }, impulse (n, at), fs);
        const auto slow = run (K, { { 1, 0.5f }, { 2, 0.0f }, { 3, 1.0f }, { 5, 1.0f }, { 6, 0.0f } }, impulse (n, at), fs);
        const auto target = (double) at + pocket::combDelaySamples (157.1875, fs);
        CHECK (std::fabs (centre (fast.l, at + 1, n) - target) < 40.0); // (the tape's own 0.1 % glide is still moving)
        CHECK (centre (slow.l, at + 1, n) - target > 400.0 || sum (slow.l, at + 1, n) < 0.5); // a much longer loop
    }
    // reset: the same output again
    {
        auto fx = makeEffect (K);
        fx->prepare (48000.0);
        const auto p = makeEffectSlot (K).params;
        auto a = impulse (9600, 100), b = a;
        fx->process (a.l.data(), a.r.data(), 9600, p, 120.0);
        fx->reset();
        fx->process (b.l.data(), b.r.data(), 9600, p, 120.0);
        CHECK (a.l == b.l && a.r == b.r);
        CHECK (fx->latencySamples() == 0);
    }
}

TEST (pocket190by_comb_flm_import)
{
    using namespace lynx;
    // FLM's 12 values → the slot's 11 (FLM k + 1 at k); Enable is the slot's switch
    const std::vector<float> values { 1.0f, 0.5f, 0.21f, 0.62f, 0.9f, 0.755f, 0.74f, 0.0f, 1.0f / 3.0f, 0.5675f, 0.0f, 1.0f };
    const auto m = flmfx::map (flm188f::moduleOf (40, values));
    CHECK (m.slots.size() == 1 && m.slots[0].kind == EffectKind::pocketComb && m.slots[0].enabled);
    if (m.slots.size() == 1)
        for (int k = 0; k < pocket::combParams; ++k)
            CHECK (m.slots[0].params[(std::size_t) k] == values[(std::size_t) k + 1]);
    CHECK (std::string (m.label) == "FL Studio Mobile: Comb Filter");
    auto off = values;
    off[0] = 0.0f;
    const auto bypassed = flmfx::map (flm188f::moduleOf (40, off));
    CHECK (bypassed.slots.size() == 1 && ! bypassed.slots[0].enabled);
    // a module with fewer values keeps FLM's defaults for the rest
    const auto fewer = flmfx::map (flm188f::moduleOf (40, { 1.0f, 1.0f, 0.1f }));
    CHECK (fewer.slots.size() == 1 && fewer.slots[0].params[0] == 1.0f && fewer.slots[0].params[1] == 0.1f && fewer.slots[0].params[2] == 0.8f
           && fewer.slots[0].params[10] == 0.4f);
    // in a .flm: on the channel's insert, converted, not in the dropped list
    FlpImportReport report;
    const auto slots = fx190by::importedSlots (40, values, &report);
    const auto it = std::find_if (slots.begin(), slots.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::pocketComb; });
    CHECK (it != slots.end());
    if (it != slots.end())
        CHECK (it->params[1] == 0.21f && it->params[8] == 0.5675f && it->params[10] == 1.0f);
    CHECK (report.unsupported.count ("FL Studio Mobile: Comb Filter") == 0);
}

namespace fx190by
{
inline Stereo sine (int n, double hz, double fs, float left = 1.0f, float right = 1.0f)
{
    auto s = silence (n);
    for (int i = 0; i < n; ++i)
    {
        const auto v = std::sin (6.283185307179586 * hz * (double) i / fs);
        s.l[(std::size_t) i] = left * (float) v;
        s.r[(std::size_t) i] = right * (float) v;
    }
    return s;
}
inline double rms (const std::vector<float>& x, int from, int to)
{
    double a = 0.0;
    for (int i = from; i < to; ++i)
        a += (double) x[(std::size_t) i] * (double) x[(std::size_t) i];
    return std::sqrt (a / (double) std::max (1, to - from));
}
inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }
// a chain of mapped slots at `fs`, blocks of 256
inline Stereo runSlots (const std::vector<lynx::EffectSlot>& slots, const Stereo& input, double fs = 48000.0)
{
    auto out = input;
    const int n = (int) out.l.size();
    for (const auto& slot : slots)
    {
        auto fx = lynx::makeEffect (slot);
        fx->prepare (fs);
        for (int at = 0; at < n; at += 256)
            fx->process (out.l.data() + at, out.r.data() + at, std::min (256, n - at), slot.params, 120.0);
    }
    return out;
}
} // namespace fx190by

TEST (pocket190by_multiband_laws_and_texts)
{
    using namespace lynx;
    CHECK (std::fabs (pocket::multibandGain (0.5f) - 1.0f) < 1e-6f && std::fabs (pocket::multibandGain (1.0f) - 15.8489f) < 1e-3f
           && std::fabs (pocket::multibandGain (0.0f) - 0.0630957f) < 1e-6f);
    CHECK (std::fabs (pocket::multibandCrossoverHz (0.3f) - 129.96f) < 0.01f && std::fabs (pocket::multibandCrossoverHz (0.7f) - 1575.9f) < 0.1f
           && pocket::multibandCrossoverHz (0.0f) == 20.0f && std::fabs (pocket::multibandCrossoverHz (1.0f) - 10240.0f) < 0.01f);
    // the curve works on the amplitude: threshold −24 dB, ratio 4 → a full-scale level comes out at thr + (1 − thr)/4
    const auto t = pocket::multibandTransfer (0.5f, 0.1f);
    CHECK (std::fabs (t.thresholdDb + 24.0f) < 1e-5f && std::fabs (t.threshold - 0.0630957f) < 1e-6f && std::fabs (t.ratio - 4.0f) < 1e-5f);
    CHECK (std::fabs (pocket::multibandOutput (t, 1.0f) - (0.0630957f + 0.9369043f / 4.0f)) < 1e-6f);
    CHECK (pocket::multibandOutput (t, 0.05f) == 0.05f);
    CHECK (pocket::multibandTransfer (1.0f, 0.0f).threshold == 1.0f && pocket::multibandTransfer (1.0f, 1.0f).ratio == 31.0f);
    CHECK (std::fabs (pocket::multibandAttackSeconds (0.3f) - 0.0045f) < 1e-6f && pocket::multibandAttackSeconds (1.0f) == 0.05f);
    CHECK (std::fabs (pocket::multibandReleaseSeconds (0.3f) - 0.307f) < 1e-6f && std::fabs (pocket::multibandReleaseSeconds (0.0f) - 0.01f) < 1e-7f);
    // −20 dB in the time, counted at 44.1 kHz
    CHECK (pocket::multibandAttackCoefficient (0.0f) == 1.0f);
    CHECK (std::fabs (std::pow (1.0 - (double) pocket::multibandReleaseCoefficient (0.3f), 0.307 * 44100.0) - 0.1) < 2e-3);
    CHECK (std::fabs (std::pow (1.0 - (double) pocket::multibandAttackCoefficient (1.0f), 0.05 * 44100.0) - 0.1) < 2e-3);
    CHECK (pocket::multibandSolo (0.0f) == 0 && pocket::multibandSolo (1.0f / 3.0f) == 1 && pocket::multibandSolo (2.0f / 3.0f) == 2
           && pocket::multibandSolo (1.0f) == 3);

    const auto K = EffectKind::pocketMultiband;
    const auto& info = effectInfo (K);
    CHECK (std::string (info.name) == "Pocket Multiband Compressor" && (int) info.params.size() == pocket::multibandParams);
    // FLM's defaults: Master ½, crossovers 0.3 / 0.7, per band ½, 1, 0, 0.3, 0.3, ½, 0; widths ½
    const auto slot = makeEffectSlot (K);
    CHECK (slot.params[0] == 0.5f && slot.params[1] == 0.3f && slot.params[2] == 0.7f && slot.params[3] == 0.0f && slot.params[4] == 0.0f);
    for (int j = 0; j < 3; ++j)
    {
        const auto* b = &slot.params[(std::size_t) (5 + 7 * j)];
        CHECK (b[0] == 0.5f && b[1] == 1.0f && b[2] == 0.0f && b[3] == 0.3f && b[4] == 0.3f && b[5] == 0.5f && b[6] == 0.0f);
        CHECK (slot.params[(std::size_t) (26 + j)] == 0.5f);
    }
    CHECK (slot.params[29] == 0.0f);
    CHECK (formatEffectParam (K, 0, 0.5f, 120.0) == "0.0 dB" && formatEffectParam (K, 0, 0.31f, 120.0) == "-9.1 dB");
    CHECK (formatEffectParam (K, 1, 0.3f, 120.0) == "130.0 Hz" && formatEffectParam (K, 2, 0.7f, 120.0) == "1.6 kHz");
    CHECK (formatEffectParam (K, 3, 0.0f, 120.0) == "Off" && formatEffectParam (K, 3, 2.0f / 3.0f, 120.0) == "Mid");
    CHECK (formatEffectParam (K, 4, 0.5f, 120.0) == "25%");
    CHECK (formatEffectParam (K, 13, 0.5f, 120.0) == "-24.0 dB" && formatEffectParam (K, 14, 0.1f, 120.0) == "4.0"
           && formatEffectParam (K, 15, 0.3f, 120.0) == "4.5 ms" && formatEffectParam (K, 16, 0.3f, 120.0) == "307.0 ms"
           && formatEffectParam (K, 17, 0.75f, 120.0) == "12.0 dB" && formatEffectParam (K, 12, 0.75f, 120.0) == "12.0 dB");
    CHECK (formatEffectParam (K, 11, 0.0f, 120.0) == "On" && formatEffectParam (K, 18, 0.5f, 120.0) == "Bypass"
           && formatEffectParam (K, 25, 1.0f, 120.0) == "Mute");
    CHECK (formatEffectParam (K, 26, 0.5f, 120.0) == "100%" && formatEffectParam (K, 29, 0.0f, 120.0) == "Mode 1"
           && formatEffectParam (K, 29, 1.0f, 120.0) == "Mode 2");
}

TEST (pocket190by_multiband_split_level_and_sample_rates)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketMultiband;
    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = (int) fs, a = n / 2;
        // at rest the three bands add up to the input's level at every frequency (an all-pass)
        for (const auto hz : { 40.0, 130.0, 500.0, 1576.0, 6000.0 })
        {
            const auto y = run (K, {}, sine (n, hz, fs, 0.5f, 0.5f), fs);
            CHECK (std::fabs (db (rms (y.l, a, n) / (0.5 / std::sqrt (2.0)))) < 0.05);
            CHECK (y.l == y.r);
        }
        // one band alone (the others muted): −6 dB at its crossover, the pass band at 0 dB, 24 dB per octave beyond
        const auto low = [&] (double hz) {
            return db (rms (run (K, { { 18, 1.0f }, { 25, 1.0f } }, sine (n, hz, fs), fs).l, a, n) * std::sqrt (2.0));
        };
        const auto mid = [&] (double hz) {
            return db (rms (run (K, { { 11, 1.0f }, { 25, 1.0f } }, sine (n, hz, fs), fs).l, a, n) * std::sqrt (2.0));
        };
        const auto high = [&] (double hz) {
            return db (rms (run (K, { { 11, 1.0f }, { 18, 1.0f } }, sine (n, hz, fs), fs).l, a, n) * std::sqrt (2.0));
        };
        const auto lowHz = (double) pocket::multibandCrossoverHz (0.3f), highHz = (double) pocket::multibandCrossoverHz (0.7f);
        CHECK (std::fabs (low (lowHz) + 6.02) < 0.1 && std::fabs (low (30.0)) < 0.1);
        CHECK (std::fabs (low (4.0 * lowHz) - low (2.0 * lowHz) + 24.0) < 1.5);
        CHECK (std::fabs (mid (lowHz) + 6.02) < 0.15 && std::fabs (mid (highHz) + 6.02) < 0.15 && std::fabs (mid (450.0)) < 0.2);
        CHECK (std::fabs (high (highHz) + 6.02) < 0.1 && std::fabs (high (8000.0)) < 0.1);
        // the gain law in the mid band: threshold −24 dB, ratio 31, a 450 Hz tone of amplitude 0.9 → thr + (A − thr)/31
        {
            const auto y = run (K, { { 11, 1.0f }, { 25, 1.0f }, { 13, 0.5f }, { 14, 1.0f }, { 15, 0.0f } }, sine (n, 450.0, fs, 0.9f, 0.9f), fs);
            const auto plain = run (K, { { 11, 1.0f }, { 25, 1.0f } }, sine (n, 450.0, fs, 0.9f, 0.9f), fs);
            const auto level = rms (plain.l, a, n) * std::sqrt (2.0);
            const auto want = 0.0630957 + (level - 0.0630957) / 31.0;
            float peak = 0.0f;
            for (int i = a; i < n; ++i)
                peak = std::max (peak, std::fabs (y.l[(std::size_t) i]));
            CHECK (std::fabs ((double) peak / want - 1.0) < 0.03);
        }
        // master gain and band gain: plain gains
        {
            const auto y = run (K, { { 0, 0.25f }, { 10, 0.75f }, { 17, 0.75f }, { 24, 0.75f } }, sine (n, 450.0, fs, 0.5f, 0.5f), fs);
            CHECK (std::fabs (db (rms (y.l, a, n) / (0.5 / std::sqrt (2.0)))) < 0.05); // −12 dB master, +12 dB on every band
        }
    }
}

TEST (pocket190by_multiband_detector_dry_width_and_states)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketMultiband;
    const double fs = 44100.0;
    const int n = 44100, a = n / 2;
    const auto only = [] (std::initializer_list<std::pair<int, float>> more) {
        std::vector<std::pair<int, float>> v { { 11, 1.0f }, { 25, 1.0f } }; // the mid band alone
        v.insert (v.end(), more.begin(), more.end());
        return v;
    };
    const auto mid = [&] (std::initializer_list<std::pair<int, float>> more, float amplitude, float right = 1.0f) {
        auto fx = makeEffect (K);
        fx->prepare (fs);
        auto p = makeEffectSlot (K).params;
        for (const auto& [k, v] : only (more))
            p[(std::size_t) k] = v;
        auto s = sine (n, 450.0, fs, amplitude, amplitude * right);
        fx->process (s.l.data(), s.r.data(), n, p, 120.0);
        return s;
    };
    const auto peakOf = [&] (const std::vector<float>& x) {
        float peak = 0.0f;
        for (int i = a; i < n; ++i)
            peak = std::max (peak, std::fabs (x[(std::size_t) i]));
        return (double) peak;
    };
    const auto unit = peakOf (mid ({}, 1.0f).l); // the band's own level for a full-scale tone
    // the input gain counts twice for the detector: +12 dB on a −40 dB tone puts the audio at −28 dB, under the −24 dB
    // threshold, and the detector at −16 dB, over it: G = (thr + (d − thr)/31)/d
    {
        const auto y = mid ({ { 12, 0.75f }, { 13, 0.5f }, { 14, 1.0f }, { 15, 0.0f } }, 0.01f);
        const auto gin = std::pow (10.0, 12.0 / 20.0), level = 0.01 * unit * gin, d = level * gin, thr = 0.0630957;
        CHECK (level < thr && d > thr);
        CHECK (std::fabs (peakOf (y.l) / (level * (thr + (d - thr) / 31.0) / d) - 1.0) < 0.03);
    }
    // Dry mix adds to the band's gain: 1 with no compression doubles the band, 0.5 gives × 1.25
    CHECK (std::fabs (peakOf (mid ({ { 4, 1.0f } }, 0.25f).l) / (0.25 * unit) - 2.0) < 0.01);
    CHECK (std::fabs (peakOf (mid ({ { 4, 0.5f } }, 0.25f).l) / (0.25 * unit) - 1.25) < 0.01);
    // Width, mode 1: ½ as it is, 0 mono, 1 the side only
    {
        const auto same = mid ({}, 0.5f, 0.0f); // a left-only tone
        CHECK (std::fabs (peakOf (same.l) / (0.5 * unit) - 1.0) < 0.01 && peakOf (same.r) < 1e-4);
        const auto mono = mid ({ { 27, 0.0f } }, 0.5f, 0.0f);
        CHECK (std::fabs (peakOf (mono.l) / (0.25 * unit) - 1.0) < 0.01 && std::fabs (peakOf (mono.r) / (0.25 * unit) - 1.0) < 0.01);
        const auto side = mid ({ { 27, 1.0f } }, 0.5f, 0.0f);
        CHECK (std::fabs (peakOf (side.l) / (0.25 * unit) - 1.0) < 0.01 && std::fabs (side.l[(std::size_t) (n - 7)] + side.r[(std::size_t) (n - 7)]) < 1e-4f);
        // mode 2: below ½ the side is scaled by the knob; above it each side loses a share of the other
        const auto half = mid ({ { 27, 0.25f }, { 29, 1.0f } }, 0.5f, 0.0f);
        CHECK (std::fabs (peakOf (half.l) / (0.5 * unit) - 0.75) < 0.01 && std::fabs (peakOf (half.r) / (0.5 * unit) - 0.25) < 0.01);
        const auto wide = mid ({ { 27, 1.0f }, { 29, 1.0f } }, 0.5f, 0.0f);
        CHECK (std::fabs (peakOf (wide.l) / (0.5 * unit) - 1.0) < 0.01 && std::fabs (peakOf (wide.r) / (0.5 * unit) - 0.5) < 0.01);
    }
    // State: a bypassed band passes its part as it is (its gains and compressor are out), a muted one is silent
    {
        const auto bypassed = mid ({ { 18, 0.5f }, { 12, 1.0f }, { 17, 0.0f }, { 13, 0.2f }, { 14, 1.0f } }, 0.5f);
        CHECK (std::fabs (peakOf (bypassed.l) / (0.5 * unit) - 1.0) < 0.01);
        const auto muted = mid ({ { 18, 1.0f } }, 0.5f);
        CHECK (peakOf (muted.l) < 1e-5);
    }
    // LYNX's Solo: the same as muting the other two bands
    {
        auto p = makeEffectSlot (K).params;
        p[3] = 2.0f / 3.0f;
        auto fx = makeEffect (K);
        fx->prepare (fs);
        auto s = sine (n, 450.0, fs, 0.5f, 0.5f);
        fx->process (s.l.data(), s.r.data(), n, p, 120.0);
        const auto ref = mid ({}, 0.5f);
        CHECK (s.l == ref.l);
    }
    // silence in, silence out; the extremes stay finite
    for (const auto rate : { 44100.0, 96000.0 })
    {
        CHECK (allZero (run (K, { { 4, 1.0f }, { 14, 1.0f }, { 13, 0.0f } }, silence (8192), rate)));
        auto p = std::vector<std::pair<int, float>> {};
        auto fx = makeEffect (K);
        fx->prepare (rate);
        auto params = makeEffectSlot (K).params;
        for (int k = 0; k < pocket::multibandParams; ++k)
            params[(std::size_t) k] = (k % 7 == 4 || k % 7 == 6) ? 0.0f : 1.0f;
        params[11] = params[18] = params[25] = 0.0f;
        auto s = sine (8192, 997.0, rate);
        fx->process (s.l.data(), s.r.data(), 8192, params, 120.0);
        CHECK (finite (s));
        CHECK (fx->latencySamples() == 0);
    }
}

TEST (pocket190by_tape_stop)
{
    using namespace lynx;
    using namespace fx190by;
    const auto K = EffectKind::pocketTapeStop;
    // laws
    CHECK (pocket::tapeStopSeconds (0.2f) == 1.0f && pocket::tapeStopSeconds (0.0f) == 0.005f && pocket::tapeStopSeconds (1.0f) == 5.0f);
    CHECK (pocket::tapeStopSyncedStep (1.0f / 3.0f) == 3 && pocket::tapeStopSyncedStep (0.0f) == 0 && pocket::tapeStopSyncedStep (1.0f) == 9);
    CHECK (pocket::tapeStopSyncedSeconds (1.0f / 3.0f, 120.0) == 0.25f);                 // ½ beat
    CHECK (std::fabs (pocket::tapeStopSyncedSeconds (1.0f / 3.0f, 127.5) - 30.0f / 127.0f) < 1e-7f); // the tempo's whole number
    CHECK (pocket::tapeStopSyncedSeconds (0.0f, 120.0) == 0.005f && pocket::tapeStopSyncedSeconds (1.0f, 120.0) == 8.0f);
    CHECK (std::fabs (pocket::tapeStopBase (0.5f) - 1.0f / 0.995f) < 1e-6f && std::fabs (pocket::tapeStopBase (1.0f) - 0.005f) < 1e-6f
           && std::fabs (pocket::tapeStopBase (0.0f) - 200.0f) < 1e-3f);
    for (const auto warp : { 0.0f, 0.3f, 0.5f, 1.0f })
    {
        const auto b = pocket::tapeStopBase (warp);
        CHECK (std::fabs (pocket::tapeStopSpeed (b, 0.0f)) < 1e-6f && std::fabs (pocket::tapeStopSpeed (b, 1.0f) - 1.0f) < 1e-5f);
    }
    CHECK (std::fabs (pocket::tapeStopSpeed (pocket::tapeStopBase (0.5f), 0.5f) - 0.5f) < 0.002f);  // nearly a straight line
    CHECK (std::fabs (pocket::tapeStopSpeed (pocket::tapeStopBase (1.0f), 0.5f) - 0.934f) < 0.002f); // drops late
    CHECK (std::fabs (pocket::tapeStopSpeed (pocket::tapeStopBase (0.0f), 0.5f) - 0.066f) < 0.002f); // drops early
    const auto& info = effectInfo (K);
    CHECK (std::string (info.name) == "Pocket Tape Stop" && (int) info.params.size() == pocket::tapeStopParams);
    const auto slot = makeEffectSlot (K);
    CHECK (slot.params[0] == 0.2f && slot.params[1] == 0.2f && slot.params[2] == 1.0f / 3.0f && slot.params[3] == 1.0f / 3.0f
           && slot.params[4] == 1.0f && slot.params[5] == 0.5f && slot.params[6] == 0.0f);
    CHECK (formatEffectParam (K, 0, 0.2f, 120.0) == "1.00 s" && formatEffectParam (K, 1, 0.04f, 120.0) == "200 ms"
           && formatEffectParam (K, 0, 0.0f, 120.0) == "Off");
    CHECK (formatEffectParam (K, 2, 1.0f / 3.0f, 120.0) == "1/2 beat" && formatEffectParam (K, 3, 1.0f, 120.0) == "16 beats");
    CHECK (formatEffectParam (K, 4, 1.0f, 120.0) == "On" && formatEffectParam (K, 4, 0.0f, 120.0) == "Off"
           && formatEffectParam (K, 5, 0.5f, 120.0) == "0%" && formatEffectParam (K, 6, 1.0f, 120.0) == "On");

    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        const int n = (int) (2.0 * fs);
        const auto in = sine (n, 1000.0, fs, 0.5f, 0.25f);
        // Play on: the input, bit for bit; Play off from the start: silence
        CHECK (run (K, {}, in, fs).l == in.l && run (K, {}, in, fs).r == in.r);
        CHECK (allZero (run (K, { { 4, 0.0f } }, in, fs)));
        CHECK (allZero (run (K, {}, silence (4096), fs)));
        // a stop at 0.5 s over 0.2 s (Stop time 0.04), then a start at 1.0 s over 0.2 s
        auto fx = makeEffect (K);
        fx->prepare (fs);
        auto p = makeEffectSlot (K).params;
        p[0] = p[1] = 0.04f;
        auto y = in;
        const int stopAt = (int) (0.5 * fs), startAt = (int) (1.0 * fs), ramp = (int) (0.2 * fs);
        for (int at = 0; at < n;)
        {
            p[4] = at >= stopAt && at < startAt ? 0.0f : 1.0f;
            const int next = at < stopAt ? stopAt : at < startAt ? startAt : n;
            const int len = std::min (256, next - at);
            fx->process (y.l.data() + at, y.r.data() + at, len, p, 120.0);
            at += len;
        }
        const auto crossings = [&] (int from, int to) {
            int c = 0;
            for (int i = from + 1; i < to; ++i)
                c += y.l[(std::size_t) (i - 1)] < 0.0f && y.l[(std::size_t) i] >= 0.0f ? 1 : 0;
            return c;
        };
        // before the stop: the input
        CHECK (std::equal (y.l.begin(), y.l.begin() + stopAt, in.l.begin()));
        // the speed falls in a (nearly) straight line from 1 to 0: 3/8 of the time × 1 kHz in the first half of the
        // ramp, 1/8 in the second — the pitch falls with it
        CHECK (std::abs (crossings (stopAt, stopAt + ramp / 2) - 75) <= 2);
        CHECK (std::abs (crossings (stopAt + ramp / 2, stopAt + ramp) - 25) <= 2);
        CHECK (rms (y.l, stopAt, stopAt + ramp / 2) > 0.3); // at the tape's level (no fade on the way down)
        // then the last value dies away (about 11 ms) into silence
        CHECK (rms (y.l, stopAt + ramp + (int) (0.1 * fs), startAt) < 1e-4);
        CHECK (std::fabs (y.l[(std::size_t) (startAt - 1)]) < 1e-6f && std::fabs (y.r[(std::size_t) (startAt - 1)]) < 1e-6f);
        // the start: the pitch rises (fewer cycles first), and at the end of the ramp the input is back, bit for bit
        CHECK (std::abs (crossings (startAt, startAt + ramp / 2) - 25) <= 3);
        CHECK (std::equal (y.l.begin() + startAt + ramp + 2, y.l.end(), in.l.begin() + startAt + ramp + 2));
        CHECK (std::equal (y.r.begin() + startAt + ramp + 2, y.r.end(), in.r.begin() + startAt + ramp + 2));
        CHECK (finite (y));
    }
    // Warp: 1 keeps the speed up and drops late, 0 drops early — more / fewer cycles in the first half of the stop
    {
        const double fs = 48000.0;
        const auto first = [&] (float warp) {
            auto fx = makeEffect (K);
            fx->prepare (fs);
            auto p = makeEffectSlot (K).params;
            p[0] = 0.04f, p[5] = warp;
            auto y = sine (24000, 1000.0, fs);
            fx->process (y.l.data(), y.r.data(), 4800, p, 120.0);
            p[4] = 0.0f;
            fx->process (y.l.data() + 4800, y.r.data() + 4800, 19200, p, 120.0);
            int c = 0;
            for (int i = 4801; i < 4800 + 4800; ++i)
                c += y.l[(std::size_t) (i - 1)] < 0.0f && y.l[(std::size_t) i] >= 0.0f ? 1 : 0;
            return c;
        };
        CHECK (first (1.0f) > 88 && first (0.0f) < 40 && std::abs (first (0.5f) - 75) <= 2);
    }
    // Sync: the synced times at the tempo (½ beat at 120 = 0.25 s): the whole stop holds f·T/2 = 125 cycles
    {
        const double fs = 48000.0;
        auto fx = makeEffect (K);
        fx->prepare (fs);
        auto p = makeEffectSlot (K).params;
        p[6] = 1.0f;
        auto y = sine (48000, 1000.0, fs);
        fx->process (y.l.data(), y.r.data(), 4800, p, 120.0);
        p[4] = 0.0f;
        fx->process (y.l.data() + 4800, y.r.data() + 4800, 43200, p, 120.0);
        int c = 0;
        for (int i = 4801; i < 4800 + 12000; ++i)
            c += y.l[(std::size_t) (i - 1)] < 0.0f && y.l[(std::size_t) i] >= 0.0f ? 1 : 0;
        CHECK (std::abs (c - 125) <= 2);
        CHECK (rms (y.l, 4800 + 12000 + 4800, 48000) < 1e-4);
    }
    // with the song stopped the module does not run: the audio passes, whatever Play says
    {
        auto fx = makeEffect (K);
        fx->prepare (48000.0);
        auto p = makeEffectSlot (K).params;
        p[4] = 0.0f;
        auto y = sine (4800, 1000.0, 48000.0);
        const auto in = y;
        fx->setPlaying (false);
        fx->process (y.l.data(), y.r.data(), 4800, p, 120.0);
        CHECK (y.l == in.l);
        CHECK (fx->latencySamples() == 0);
    }
}

TEST (pocket190by_reverb32_and_tuned_eq_mappings)
{
    using namespace lynx;
    using namespace fx190by;
    // --- Reverb (module 32): the plate of modules 12 and 35 behind module 12's knobs and its own dry / wet mix
    const std::vector<float> defaults { 1.0f, 0.4f, 0.8f, 0.4f, 0.5f, 0.25f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 1.0f, 0.499f, 0.0f, 1.0f, 1.0f };
    const auto with = [&] (std::initializer_list<std::pair<int, float>> changes) {
        auto v = defaults;
        for (const auto& [k, x] : changes)
            v[(std::size_t) k] = x;
        return flmfx::map (flm188f::moduleOf (32, v));
    };
    {
        const auto m = with ({});
        // round 190dw: FLM's own engine (Pocket Reverb) with the module's values; pan, width and modulation play, so
        // nothing is listed any more
        CHECK (m.label == "FL Studio Mobile: Reverb" && m.slots.size() == 1 && m.slots[0].kind == EffectKind::pocketReverb && m.partial.empty());
        if (m.slots.size() == 1)
            for (int k = 0; k < 15; ++k)
                CHECK (m.slots[0].params[(std::size_t) k] == defaults[(std::size_t) k + 1]);
        CHECK (with ({ { 4, 0.8f } }).partial.empty() && with ({ { 13, 0.5f } }).partial.empty() && with ({ { 9, 1.0f } }).partial.empty());
        CHECK (! with ({ { 0, 0.0f } }).slots[0].enabled);
    }
    {
        // the dry signal: 1 up to MIX ½, then 2 − 2·MIX, times the dry level; the reverb's level follows min(2·MIX, 1)
        const int n = 48000;
        const auto dryOf = [&] (std::initializer_list<std::pair<int, float>> changes) {
            return (double) runSlots (with (changes).slots, impulse (n, 0)).l[0];
        };
        CHECK (std::fabs (dryOf ({}) - 1.0) < 2e-3);
        CHECK (std::fabs (dryOf ({ { 3, 0.75f } }) - 0.5) < 6e-3); // the plate's dry level is an integer control (steps under 0.1 dB)
        CHECK (std::fabs (dryOf ({ { 3, 1.0f } })) < 2e-3);
        CHECK (std::fabs (dryOf ({ { 15, 0.5f } }) - 0.5) < 6e-3);
        const auto tail = [&] (std::initializer_list<std::pair<int, float>> changes) {
            const auto y = runSlots (with (changes).slots, impulse (n, 0));
            return rms (y.l, 480, n);
        };
        const auto full = tail ({ { 3, 0.5f }, { 5, 0.0f } });
        CHECK (full > 1e-4);
        CHECK (std::fabs (tail ({ { 3, 0.25f }, { 5, 0.0f } }) / full - 0.5) < 0.02);
        CHECK (std::fabs (tail ({ { 3, 1.0f }, { 5, 0.0f } }) / full - 1.0) < 0.02);
        CHECK (std::fabs (tail ({ { 3, 0.5f }, { 5, 0.0f }, { 14, 0.5f } }) / full - 0.5) < 0.02);
        CHECK (tail ({ { 3, 0.0f }, { 5, 0.0f } }) < 1e-6);
        // the raw early reflections stay when the mix is fully dry
        CHECK (tail ({ { 3, 0.0f }, { 5, 1.0f } }) > 1e-4);
    }
    // --- Tuned EQ (module 30)
    CHECK (flmfx::tunedEqNote (0.0f) == 0 && flmfx::tunedEqNote (9.0f / 11.0f) == 9 && flmfx::tunedEqNote (1.0f) == 11 && flmfx::tunedEqNote (0.5f) == 5);
    CHECK (std::fabs (flmfx::tunedEqHz (9, 0) - 110.0) < 1e-9 && std::fabs (flmfx::tunedEqHz (9, 7) - 14080.0) < 1e-6
           && std::fabs (flmfx::tunedEqHz (0, 2) - 261.6256) < 1e-3 && std::fabs (flmfx::tunedEqHz (11, 0) - 123.4708) < 1e-3);
    CHECK (std::fabs (flmfx::tunedEqOctaves (0.6f) - 2.26) < 1e-6 && std::fabs (flmfx::tunedEqOctaves (0.0f) - 0.1) < 1e-9
           && std::fabs (flmfx::tunedEqOctaves (1.0f) - 10.1) < 1e-6);
    {
        const auto flat = flmfx::map (flm188f::moduleOf (30, { 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.6f }));
        CHECK (flat.label == "FL Studio Mobile: Tuned EQ" && flat.slots.size() == 1 && flat.slots[0].kind == EffectKind::parametricEq);
        // band 3 at +6 dB on the knob (twice that in the filter), Tuning A, Bandwidth 0.3 (0.37 octave): the level at its
        // centre (440 Hz) and around it, against FLM's band design
        const std::vector<float> values { 1.0f, 0.5f, 0.5f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 9.0f / 11.0f, 0.3f };
        const auto m = flmfx::map (flm188f::moduleOf (30, values));
        CHECK (m.slots.size() == 1);
        const auto band = flm188f::flmBand (1, 440.0, 6.0, flmfx::tunedEqOctaves (0.3f), 44100.0);
        for (const auto hz : { 220.0, 400.0, 440.0, 480.0, 880.0 })
        {
            const auto y = runSlots (m.slots, sine (24000, hz, 44100.0, 0.25f, 0.25f), 44100.0);
            const auto gain = db (rms (y.l, 12000, 24000) / (0.25 / std::sqrt (2.0)));
            CHECK (std::fabs (gain - flm188f::magnitudeDb ({ band }, hz, 44100.0)) < 0.15);
            if (hz == 440.0)
                CHECK (std::fabs (gain - 12.0) < 0.1);
        }
        // Tuning C moves the same band to 261.63 Hz
        auto c = values;
        c[9] = 0.0f;
        const auto y = runSlots (flmfx::map (flm188f::moduleOf (30, c)).slots, sine (24000, 261.6256, 44100.0, 0.25f, 0.25f), 44100.0);
        CHECK (std::fabs (db (rms (y.l, 12000, 24000) / (0.25 / std::sqrt (2.0))) - 12.0) < 0.1);
        // the shelves at the ends: band 1 lifts what is below its centre, band 8 what is above
        auto shelves = values;
        shelves[3] = 0.5f, shelves[1] = 0.75f, shelves[8] = 0.25f, shelves[10] = 0.464f; // (1.1 octaves)
        const auto s = flmfx::map (flm188f::moduleOf (30, shelves));
        const auto lowBand = flm188f::flmBand (0, 110.0, 6.0, flmfx::tunedEqOctaves (0.464f), 44100.0);
        const auto highBand = flm188f::flmBand (2, 14080.0, -6.0, flmfx::tunedEqOctaves (0.464f), 44100.0);
        for (const auto hz : { 40.0, 110.0, 1000.0, 14080.0, 18000.0 })
        {
            const auto z = runSlots (s.slots, sine (24000, hz, 44100.0, 0.25f, 0.25f), 44100.0);
            // the low shelf is FLM's own; LYNX's high shelf this close to the top of the band differs by under 1 dB
            CHECK (std::fabs (db (rms (z.l, 12000, 24000) / (0.25 / std::sqrt (2.0))) - flm188f::magnitudeDb ({ lowBand, highBand }, hz, 44100.0)) < (hz < 2000.0 ? 0.1 : 1.0));
        }
        CHECK (s.partial.empty());
        // a Bandwidth whose shape LYNX's EQ cannot make is reported
        auto narrow = shelves, wide = shelves;
        narrow[10] = 0.2f, wide[10] = 0.9f;
        CHECK (flmfx::map (flm188f::moduleOf (30, narrow)).partial.size() == 1 && flmfx::map (flm188f::moduleOf (30, wide)).partial.size() == 1);
    }
    // --- in a .flm: each on the channel's insert, none in the dropped list
    {
        FlpImportReport report;
        const auto mb = fx190by::importedSlots (36, std::vector<float> (27, 0.5f), &report);
        const auto it = std::find_if (mb.begin(), mb.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::pocketMultiband; });
        CHECK (it != mb.end());
        if (it != mb.end()) // 27 values in the file: the last four keep FLM's defaults; FLM's editor flag solos nothing
            CHECK (it->params[0] == 0.5f && it->params[25] == 0.5f && it->params[26] == 0.5f && it->params[29] == 0.0f && it->params[3] == 0.0f);
        const auto solo = flmfx::map (flm188f::moduleOf (36, { 1.0f, 0.5f, 0.3f, 0.7f, 1.0f, 0.25f }));
        CHECK (solo.slots.size() == 1 && solo.slots[0].params[3] == 0.0f && solo.slots[0].params[4] == 0.25f);
        CHECK (report.unsupported.count ("FL Studio Mobile: Multiband Compressor") == 0);
        const auto ts = fx190by::importedSlots (42, { 1.0f, 0.1f, 0.3f, 0.5f, 0.6f, 0.0f, 0.8f, 1.0f }, &report);
        const auto tape = std::find_if (ts.begin(), ts.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::pocketTapeStop; });
        CHECK (tape != ts.end());
        if (tape != ts.end())
            CHECK (tape->params[0] == 0.1f && tape->params[4] == 0.0f && tape->params[5] == 0.8f && tape->params[6] == 1.0f);
        const auto rv = fx190by::importedSlots (32, defaults, &report);
        CHECK (std::any_of (rv.begin(), rv.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::pocketReverb; })); // round 190dw
        const auto eq = fx190by::importedSlots (30, { 1.0f, 0.5f, 0.5f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f, 0.6f }, &report);
        CHECK (std::any_of (eq.begin(), eq.end(), [] (const EffectSlot& s) { return s.kind == EffectKind::parametricEq; }));
        for (const auto* name : { "FL Studio Mobile: Tape Stop", "FL Studio Mobile: Reverb", "FL Studio Mobile: Tuned EQ" })
            CHECK (report.unsupported.count (name) == 0);
    }
}
