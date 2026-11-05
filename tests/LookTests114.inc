// REA_rodada114: the editors of the "inspired by" engines follow the reference plugins' workflow. The editors
// themselves are UI (src/ui/ProEditors.cpp, checked with --plugui snapshots); here: the live meters the new limiter
// display reads, and the panel conventions shared by the reverbs / delays (caption above, value under each knob).

#include "core/ApexLimiter.h"

TEST (look114_apex_limiter_publishes_its_meters)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::apexLimiter);
    slot.params[0] = 0.4f; // +12 dB into a −0.1 dB ceiling
    auto fx = makeEffect (slot);
    fx->prepare (48000.0);
    fx->reset();
    std::vector<float> l (512), r (512);
    float maxGr = 0.0f, lastIn = 0.0f, lastOut = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 512; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 440.0 * (block * 512 + i) / 48000.0);
        fx->process (l.data(), r.data(), 512, slot.params, 120.0);
        maxGr = std::max (maxGr, fx->controlOutput (0));
        lastIn = fx->controlOutput (2);
        lastOut = fx->controlOutput (3);
    }
    // a 0.5 sine (−6 dBFS) + 12 dB needs ≈ 6 dB of reduction to sit at −0.1 dBFS
    const auto grDb = maxGr * 30.0f;
    std::printf ("  apex meters: GR %.2f dB, in %.3f, out %.3f\n", grDb, lastIn, lastOut);
    CHECK (grDb > 5.0f && grDb < 7.5f);
    CHECK (std::abs (lastIn - 0.5f) < 0.01f);
    CHECK (lastOut > 0.9f && lastOut <= std::pow (10.0f, -0.1f / 20.0f) + 1e-4f);
    CHECK (fx->controlOutput (5) < 0.0f); // round 135: output 5 is the integrated loudness (LUFS)
}

TEST (look114_reverb_panels_write_captions_above_their_knobs)
{
    using namespace lynx;
    for (const auto kind : { EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon,
                             EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod })
    {
        const auto& l = plugui::layoutFor (kind);
        int knobs = 0, above = 0;
        for (const auto& item : l.items)
            if (item.type == plugui::Item::knob && (item.style & plugui::showValue) != 0)
            {
                ++knobs;
                if ((item.style & plugui::captionAbove) != 0) ++above;
                CHECK (item.y >= 0.0f);
            }
        CHECK (knobs > 0 && above == knobs);
    }
    // the dynamics panels keep the caption under the knob
    for (const auto& item : plugui::layoutFor (EffectKind::glowLeveler).items)
        CHECK ((item.style & plugui::captionAbove) == 0);
}

TEST (look114_ridge_strip_reads_top_to_bottom_like_a_channel_strip)
{
    using namespace lynx;
    const auto& l = plugui::layoutFor (EffectKind::ridgeStrip);
    const auto at = [&l] (const char* param) -> const plugui::LayoutItem* {
        for (const auto& item : l.items)
            if (item.param != nullptr && std::string (item.param) == param) return &item;
        return nullptr;
    };
    for (const auto* p : { "HPF", "HF gain", "HMF gain", "LMF gain", "LF gain", "Comp. threshold", "Gate threshold", "Input", "Output" })
        CHECK (at (p) != nullptr);
    if (at ("HPF") == nullptr || at ("Output") == nullptr) return;
    // left column: filters, then HF above HMF above LMF above LF; right column: compressor above gate above gain
    CHECK (at ("HPF")->y < at ("HF gain")->y);
    CHECK (at ("HF gain")->y < at ("HMF gain")->y && at ("HMF gain")->y < at ("LMF gain")->y && at ("LMF gain")->y < at ("LF gain")->y);
    CHECK (at ("Comp. threshold")->y < at ("Gate threshold")->y && at ("Gate threshold")->y < at ("Output")->y);
    CHECK (at ("LF gain")->x < l.width * 0.5f && at ("Comp. threshold")->x > l.width * 0.5f);
    CHECK (l.height >= 700); // round tp172c: three columns like the reference strip (wider than tall), full strip height
}
