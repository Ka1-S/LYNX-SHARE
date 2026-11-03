#include "core/EffectPresets.h"
#include "core/EffectResponse.h"

// Round 168r: designed editors and LYNX preset banks for group E (FL effects that had a generated page, LYNX's simple
// built-ins) and the Peak Controller's LFO (REA_rodada168r).

namespace design168r
{
inline const std::vector<lynx::EffectKind>& kinds()
{
    using K = lynx::EffectKind;
    static const std::vector<K> list { K::pitchCorrect, K::peakController, K::spreader, K::fruityFastLP, K::flFrequencyShifter,
                                       K::flLowLifter, K::flBassBoost, K::flDelay1, K::fl7BandEq, K::flCenter, K::flPhaseInverter,
                                       K::eq, K::delay, K::reverb, K::limiter, K::filter, K::distortion, K::chorus, K::utility,
                                       K::lofi, K::crossover, K::pitchShift, K::freqShift, K::rhythmGain, K::volumeGate };
    return list;
}

// two seconds of a test mix at -6 dBFS peak: a saw chord, a kick-like thump every half second, a little noise
inline void testSignal (std::vector<float>& l, std::vector<float>& r, double sr)
{
    const auto n = (int) (sr * 2.0);
    l.assign ((std::size_t) n, 0.0f);
    r.assign ((std::size_t) n, 0.0f);
    std::uint32_t seed = 12345;
    for (int i = 0; i < n; ++i)
    {
        const auto t = i / sr;
        double v = 0.0;
        for (const auto f : { 110.0, 138.6, 164.8 })
            v += (std::fmod (t * f, 1.0) * 2.0 - 1.0) * 0.12;
        const auto beat = std::fmod (t, 0.5);
        v += std::sin (6.283185307179586 * 55.0 * beat) * std::exp (-beat * 18.0) * 0.18;
        seed = seed * 1664525u + 1013904223u;
        v += ((double) (seed >> 8) / 8388608.0 - 1.0) * 0.02;
        l[(std::size_t) i] = (float) v;
        r[(std::size_t) i] = (float) (v * 0.9);
    }
}
} // namespace design168r

TEST (design168r_layouts_place_every_parameter)
{
    using namespace lynx;
    // every engine of the round has its own page (FL's arrangement for the FL effects, LYNX's for its own), every
    // parameter has a control, and no item leaves the body
    for (const auto kind : design168r::kinds())
    {
        const auto& l = plugui::layoutFor (kind);
        const auto& info = effectInfo (kind);
        const auto fl = plugui::hasFlLayout (kind);
        const auto& own = plugui::ownLayoutKinds();
        CHECK (fl || std::find (own.begin(), own.end(), kind) != own.end());
        CHECK (l.width > 100 && l.height > 40);
        CHECK (! l.items.empty());
        std::vector<bool> placed ((std::size_t) plugui::paramCount (kind), false);
        for (const auto& item : l.items)
        {
            CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) l.width + 0.5f && item.y + item.h <= (float) l.height + 0.5f);
            for (const auto* name : { item.param, item.param2 })
                if (name != nullptr)
                {
                    const auto p = plugui::paramIndex (kind, name);
                    if (p < 0)
                        std::printf ("  %s: unknown parameter '%s'\n", info.name, name);
                    CHECK (p >= 0);
                    if (p >= 0)
                        placed[(std::size_t) p] = true;
                }
        }
        for (const auto* name : l.menuSwitches)
            if (const auto p = plugui::paramIndex (kind, name); p >= 0)
                placed[(std::size_t) p] = true;
        for (const auto* name : l.editedElsewhere)
            if (const auto p = plugui::paramIndex (kind, name); p >= 0)
                placed[(std::size_t) p] = true;
        for (std::size_t p = 0; p < placed.size(); ++p)
            if (! placed[p])
            {
                std::printf ("  %s: parameter '%s' has no control\n", info.name, plugui::paramName (kind, (int) p));
                CHECK (false);
            }
    }
}

TEST (design168r_every_engine_has_a_lynx_bank_that_renders)
{
    using namespace lynx;
    // LYNX's own banks (clean-room settings): every preset resolves, and a test mix through it is neither silent nor
    // clipping nor broken (NaN)
    std::vector<float> inL, inR;
    constexpr double sr = 48000.0;
    design168r::testSignal (inL, inR, sr);
    for (const auto kind : design168r::kinds())
    {
        const auto& bank = presets::factory (kind);
        // (Center is a switch: on / off; Phase Inverter has three states)
        const auto wanted = kind == EffectKind::flCenter ? 2u : kind == EffectKind::flPhaseInverter ? 3u : 4u;
        if (bank.size() < wanted)
            std::printf ("  %s: only %d presets\n", effectInfo (kind).name, (int) bank.size());
        CHECK (bank.size() >= wanted);
        for (const auto& preset : bank)
        {
            std::vector<std::string> unresolved;
            auto slot = makeEffectSlot (kind);
            slot.params = presets::apply (kind, preset, 120.0, &unresolved);
            if (! unresolved.empty())
                std::printf ("  %s / %s: unresolved %s\n", effectInfo (kind).name, preset.name.c_str(), unresolved.front().c_str());
            CHECK (unresolved.empty());
            auto fx = makeEffect (slot);
            CHECK (fx != nullptr);
            if (fx == nullptr)
                continue;
            fx->prepare (sr);
            fx->reset();
            auto l = inL, r = inR;
            constexpr int block = 256;
            fx->setPlaying (true);
            for (std::size_t at = 0; at + block <= l.size(); at += block)
            {
                fx->setSongPosition ((double) at / sr * 2.0 * 960.0); // 120 BPM, 960 ticks per beat
                fx->process (l.data() + at, r.data() + at, block, slot.params, 120.0);
            }
            double sum = 0.0, peak = 0.0;
            bool finite = true;
            for (std::size_t i = l.size() / 4; i < l.size(); ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                sum += (double) l[i] * l[i] + (double) r[i] * r[i];
                peak = std::max ({ peak, (double) std::abs (l[i]), (double) std::abs (r[i]) });
            }
            const auto rms = std::sqrt (sum / (double) (l.size() * 3 / 2));
            if (! finite || rms < 0.003 || peak > 1.0)
                std::printf ("  %s / %s: rms %.4f peak %.3f%s\n", effectInfo (kind).name, preset.name.c_str(), rms, peak, finite ? "" : " NaN");
            CHECK (finite);
            CHECK (rms >= 0.003);
            CHECK (peak <= 1.0);
        }
    }
}

TEST (design168r_peak_controller_lfo_ramp_mute)
{
    using namespace lynx;
    constexpr double sr = 48000.0;
    auto slot = makeEffectSlot (EffectKind::peakController);
    CHECK (plugui::paramCount (EffectKind::peakController) == 12);
    CHECK (formatEffectParam (EffectKind::peakController, 8, 1.0f, 120.0) == "Random");
    CHECK (formatEffectParam (EffectKind::peakController, 8, 0.25f, 120.0) == "Triangle");
    auto& p = slot.params;
    p[1] = 0.5f;  // no peak amount
    p[4] = 0.0f;  // LFO base 0: the LFO adds base … base + amount
    p[5] = 1.0f;  // LFO amount +100 %
    p[7] = (8600.0f - 200.0f) / 65336.0f; // a short period
    auto run = [&] (int blocks, bool playing, std::vector<float>* lfo, std::vector<float>* audio = nullptr) {
        auto fx = makeEffect (slot);
        fx->prepare (sr);
        fx->reset();
        fx->setPlaying (playing);
        fx->setSongPosition (0.0);
        std::vector<float> l (256, 0.25f), r (256, 0.25f);
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.25f);
            std::fill (r.begin(), r.end(), 0.25f);
            fx->process (l.data(), r.data(), 256, slot.params, 120.0);
            if (lfo != nullptr)
                lfo->push_back (fx->controlOutput (2));
            if (audio != nullptr)
                audio->push_back (l[100]);
        }
    };
    // the LFO swings around its base across the whole range
    std::vector<float> lfo;
    run (400, false, &lfo);
    const auto [lo, hi] = std::minmax_element (lfo.begin(), lfo.end());
    CHECK (*lo < 0.1f && *hi > 0.9f);
    // amount 0 = just the base
    p[5] = 0.5f;
    lfo.clear();
    run (50, false, &lfo);
    CHECK (std::abs (lfo.back()) < 1e-4f);
    // Ramp: a jump of the base moves at most 1/8 of the range per host tick
    p[5] = 1.0f;
    p[8] = 0.5f; // square: jumps
    p[11] = 1.0f;
    lfo.clear();
    run (400, false, &lfo);
    const auto tickStep = (float) (120.0 / 60.0 * 256.0 / sr * 96.0 * 0.125);
    auto largest = 0.0f;
    for (std::size_t i = 1; i < lfo.size(); ++i)
        largest = std::max (largest, std::abs (lfo[i] - lfo[i - 1]));
    CHECK (largest <= tickStep + 1e-4f);
    CHECK (largest > tickStep * 0.5f);
    // Mute silences the audio it passes, not the controller
    p[10] = 1.0f;
    std::vector<float> audio;
    lfo.clear();
    run (20, false, &lfo, &audio);
    CHECK (audio.back() == 0.0f);
    p[10] = 0.0f;
    audio.clear();
    run (20, false, nullptr, &audio);
    CHECK (audio.back() == 0.25f);
    // locked to the song while playing: the same song position gives the same value
    p[11] = 0.0f;
    p[8] = 0.0f;
    std::vector<float> a, b;
    run (64, true, &a);
    run (64, true, &b);
    CHECK (a == b);
}

TEST (design168r_peak_controller_imports_fl_lfo_and_flags)
{
    using namespace lynx;
    // FL's state: ten int32 controls then Mute and Ramp bytes (REA_rodada168r §4)
    std::vector<std::uint8_t> d (42, 0);
    auto put = [&] (int at, std::int32_t v) { std::memcpy (d.data() + at, &v, 4); };
    put (0, 0);
    put (4, 128);
    put (8, 0);
    put (12, 64);
    put (16, 32768);
    put (20, -128);  // LFO amount
    put (24, 64);    // LFO tension
    put (28, 16584); // speed
    put (32, 3);     // saw
    put (36, 16384); // phase 90°
    d[41] = 1;       // Ramp
    const auto slot = effectFromFl ("Fruity Peak Controller", d);
    CHECK (slot && slot->kind == EffectKind::peakController);
    if (! slot)
        return;
    CHECK (std::abs (slot->params[5] - 0.25f) < 1e-4f);
    CHECK (std::abs (slot->params[6] - 0.75f) < 1e-4f);
    CHECK (std::abs (slot->params[7] - (16584.0f - 200.0f) / 65336.0f) < 1e-4f);
    CHECK (formatEffectParam (EffectKind::peakController, 8, slot->params[8], 120.0) == "Saw");
    CHECK (formatEffectParam (EffectKind::peakController, 9, slot->params[9], 120.0) == "90\xc2\xb0");
    CHECK (slot->params[10] == 0.0f && slot->params[11] == 1.0f);
}

TEST (design168r_response_and_transfer_pictures)
{
    using namespace lynx;
    // the measured pictures of the editors: Bass Boost lifts the lows, the Fast LP cuts the highs, a flat 7-band EQ
    // is flat, distortion bends the sine
    auto boost = makeEffectSlot (EffectKind::flBassBoost);
    boost.params[(std::size_t) plugui::paramIndex (EffectKind::flBassBoost, "Amount")] = 1.0f;
    boost.params[(std::size_t) plugui::paramIndex (EffectKind::flBassBoost, "Frequency")] = 0.3f;
    const auto b = fxresponse::responseDb (boost, { 60.0, 5000.0 });
    if (b.size() == 2)
        std::printf ("  bass boost: %.2f dB at 60 Hz, %.2f dB at 5 kHz\n", b[0], b[1]);
    CHECK (b.size() == 2 && b[0] > b[1] + 2.0f);
    auto lp = makeEffectSlot (EffectKind::fruityFastLP);
    lp.params[(std::size_t) plugui::paramIndex (EffectKind::fruityFastLP, "Cutoff")] = 0.3f;
    const auto f = fxresponse::responseDb (lp, { 100.0, 18000.0 });
    if (f.size() == 2)
        std::printf ("  fast LP: %.2f dB at 100 Hz, %.2f dB at 18 kHz\n", f[0], f[1]);
    CHECK (f.size() == 2 && f[0] > f[1]);
    const auto flat = fxresponse::responseDb (makeEffectSlot (EffectKind::fl7BandEq), { 63.0, 1000.0, 8000.0 });
    CHECK (flat.size() == 3);
    for (const auto v : flat)
        CHECK (std::abs (v) < 1.0f);
    auto dist = makeEffectSlot (EffectKind::distortion);
    dist.params[0] = 1.0f;
    const auto curve = fxresponse::transferCycle (dist, 64);
    CHECK (curve.size() == 64);
    const auto top = *std::max_element (curve.begin(), curve.end());
    CHECK (top > 0.05f);
}
