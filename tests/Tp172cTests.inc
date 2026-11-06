// Round tp172c: Ridge Strip's reference strips — SSL Native Channel Strip 2, 4K G and 4K B states load into the 4K
// voicing (Channel Strip 2: E / G EQ = Brown / Black, its own "out" positions, no VCA colour; 4K G: Black, LMF ÷3 /
// HMF ×3; 4K B: Brown, its stepped compressor read as the 4K's, slice 4); the editor's three columns fit the window.
namespace tp172c
{
inline lynx::EffectSlot sslSlot (const std::string& plugin, const std::string& params)
{
    using namespace lynx;
    ForeignPlugin fp;
    fp.format = ForeignPlugin::Format::vst3;
    fp.name = plugin;
    fp.state = substtest::flVst3State (substtest::juceXmlComponent (
        "<SSL_PLUGIN_STATE PluginName=\"" + plugin + "\" Version=\"2.10.6\" StateASelected=\"1\"><A><PROCESSOR_STATE>" + params
        + "<PARAM id=\"Bypass\" value=\"0.0\"/><PARAM id=\"DynamicsIn\" value=\"1.0\"/><PARAM id=\"EqIn\" value=\"1.0\"/>"
          "<PARAM id=\"FiltersIn\" value=\"1.0\"/><PARAM id=\"InputTrim\" value=\"0.0\"/><PARAM id=\"OutputTrim\" value=\"0.0\"/>"
          "<PARAM id=\"FaderLevel\" value=\"0.0\"/><PARAM id=\"PhaseInvert\" value=\"0.0\"/><PARAM id=\"Pan\" value=\"0.0\"/>"
          "</PROCESSOR_STATE></A><B><PROCESSOR_STATE><PARAM id=\"CompRatio\" value=\"1.0\"/></PROCESSOR_STATE></B></SSL_PLUGIN_STATE>"));
    const auto id = pluginIdentity (fp);
    const auto* entry = findPluginSubstitute (id, false);
    if (entry == nullptr)
        return {};
    int n = 0;
    return makeSubstituteSlot (*entry, &id, &n);
}
} // namespace tp172c

TEST (ridge_channel_strip_2_state_translated_tp172c)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    auto p = tp172c::sslSlot ("SSL Native Channel Strip 2",
                              "<PARAM id=\"EqE\" value=\"0.0\"/><PARAM id=\"HighPassFreq\" value=\"20.0\"/><PARAM id=\"LowPassFreq\" value=\"35.0\"/>"
                              "<PARAM id=\"LowMidEqFreq\" value=\"0.4\"/><PARAM id=\"LowMidEqGain\" value=\"-4.0\"/><PARAM id=\"LowMidEqQ\" value=\"1.5\"/>"
                              "<PARAM id=\"CompRatio\" value=\"4.0\"/><PARAM id=\"CompThreshold\" value=\"-15.0\"/><PARAM id=\"CompRelease\" value=\"0.3\"/>"
                              "<PARAM id=\"CompAutoMakeUp\" value=\"0.0\"/>").params;
    // round tp172c: the Modern voicing (strip2), in the reference's own ranges
    CHECK (r::modernVoicing (p) && ! r::k4Voicing (p) && p[r::curve] > 0.5f); // G type
    CHECK (p[r::hpf] == 0.0f && p[r::lpf] == 0.0f && p[r::analog] < 0.5f && p[r::autoMakeup] < 0.5f);
    CHECK (std::abs (r::mLmfKHz (p[r::lmfFreq]) - 0.4) < 1e-4 && std::abs (r::mGainDb (p[r::lmfGain]) + 4.0) < 1e-4);
    CHECK (std::abs (r::mRatio (p[r::compRatio]) - 4.0) < 1e-3 && std::abs (r::eCompThresholdDb (p[r::compThreshold]) + 15.0) < 1e-4);
    p = tp172c::sslSlot ("SSL Native Channel Strip 2", "<PARAM id=\"EqE\" value=\"1.0\"/><PARAM id=\"HighPassFreq\" value=\"100.0\"/>"
                                                        "<PARAM id=\"CompRatio\" value=\"100.0\"/><PARAM id=\"CompPeak\" value=\"1.0\"/>").params;
    CHECK (p[r::curve] < 0.5f && std::abs (r::mHpfHz (p[r::hpf]) - 100.0) < 0.01); // E type
    CHECK (r::mRatio (p[r::compRatio]) > 1e6 && p[r::compPeak] > 0.5f); // the reference's "INF" (stored as 100)
    for (const double ratio : { 1.1, 1.8, 4.3, 10.0, 21.0, 38.0, 63.0 })
        CHECK (std::abs (r::mRatio (r::mRatioValue (ratio)) - ratio) < 1e-3 * ratio);

    p = tp172c::sslSlot ("SSL 4K G", "<PARAM id=\"LowMidEqFreq\" value=\"1.5\"/><PARAM id=\"LowMidEqDiv3\" value=\"1.0\"/>"
                                     "<PARAM id=\"HighMidEqFreq\" value=\"2.0\"/><PARAM id=\"HighMidEqMult3\" value=\"1.0\"/>").params;
    CHECK (r::k4Voicing (p) && r::k4ColourIndex (p[r::k4Colour]) == 1);
    CHECK (std::abs (r::lmfHz (p[r::lmfFreq]) - 500.0) < 0.1 && std::abs (r::hmfHz (p[r::hmfFreq]) - 6000.0) < 1.0);
    p = tp172c::sslSlot ("SSL 4K B", "<PARAM id=\"LowEqGain\" value=\"6.0\"/><PARAM id=\"CompRatio\" value=\"3.0\"/>").params;
    CHECK (r::k4Voicing (p) && r::k4ColourIndex (p[r::k4Colour]) == 0);
    CHECK (std::abs (r::bandGainDb (p[r::lfGain], false) - 6.0) < 1e-4);
}

// slice 4: the 4K B's stepped compressor (K4B_dyn / K4B_mk on its trial) and the 4K compressor's blend between its
// measured ratio nodes (a 10:1 dial acted like ~100:1: the 4K E's S4 setting was 6 dB too deep)
TEST (ridge_4k_b_steps_and_ratio_blend_tp172c4)
{
    using namespace lynx;
    namespace r = mixsuite::ridge;
    auto p = tp172c::sslSlot ("SSL 4K B", "<PARAM id=\"CompRatio\" value=\"2.0\"/><PARAM id=\"CompThreshold\" value=\"-15.0\"/>"
                                         "<PARAM id=\"CompAutoMakeUp\" value=\"1.0\"/><PARAM id=\"CompRelease\" value=\"3.0\"/>").params;
    CHECK (r::k4Voicing (p) && r::ratio (p[r::compRatio]) > 1e6);                    // "4:1" = near-limiting here
    CHECK (std::abs (r::eCompThresholdDb (p[r::compThreshold]) + 7.0) < 1e-3);       // its knee 8 dB higher
    CHECK (p[r::autoMakeup] < 0.5f && std::abs (r::trimDb (p[r::output]) - 12.6) < 0.01); // its make-up, in the output
    CHECK (std::abs (r::releaseSeconds (p[r::compRelease]) - 0.8) < 1e-3);           // step 3 = .8 s
    p = tp172c::sslSlot ("SSL 4K B", "<PARAM id=\"CompRatio\" value=\"0.0\"/><PARAM id=\"SaturationIn\" value=\"1.0\"/>"
                                     "<PARAM id=\"SaturationAmount\" value=\"50.0\"/>").params;
    CHECK (p[r::compRatio] == 0.0f && p[r::k4Pre] > 0.5f && std::abs (r::k4MicDb (p[r::k4Mic]) - 27.5) < 0.01);

    vmatch::echannel::Settings e;
    e.model = 1;
    e.autoMakeup = false;
    e.compRatio = 10.0;
    double last = 0.0;
    for (const double t : { -20.0, -15.0, -12.0, -10.0, -8.0, -5.0, 0.0, 5.0 }) // deeper for every lower threshold
    {
        e.compThreshold = t;
        const auto g = vmatch::echannel::compStaticGainDb (e, -9.0);
        CHECK (t == -20.0 || g > last);
        last = g;
    }
    e.compThreshold = -10.0;
    CHECK (vmatch::echannel::compStaticGainDb (e, -9.0) > -19.5); // the trial: about -17 dB (S4); it gave -23
}

TEST (ridge_three_column_layout_fits_tp172c)
{
    using namespace lynx;
    const auto& l = plugui::layoutFor (EffectKind::ridgeStrip);
    CHECK (l.width == 780);
    for (const auto& it : l.items)
        CHECK (it.x >= 0.0f && it.x + it.w <= (float) l.width + 0.5f && it.y + it.h <= (float) l.height + 0.5f);
}

// Round tp172c: the Modern voicing's measured laws (core/VoiceMatch.h strip2) — the E type's +12 mid bell peaks at
// +7.3 dB, the G type's at +9.25; a 0 dBFS sine at ratio 2, threshold -15 gets the trial's +8.9 dB of auto make-up;
// its 3rd-order high-pass at 200 Hz is -63 dB at 20 Hz; the engine is silent in, silent out and stays finite
TEST (ridge_modern_voicing_laws_tp172c)
{
    using namespace lynx;
    namespace s2 = vmatch::strip2;
    s2::Settings s;
    s.lmfGain = 12.0; s.lmfKHz = 0.8; s.lmfQ = 1.5;
    CHECK (std::abs (s2::responseDb (s, 48000.0, 848.0) - 7.27) < 0.15);
    s.typeG = true;
    CHECK (std::abs (s2::responseDb (s, 48000.0, 842.0) - 9.24) < 0.15);
    s = {};
    s.hpHz = 200.0;
    CHECK (std::abs (s2::responseDb (s, 48000.0, 20.0) + 63.0) < 1.0);
    s = {};
    s.compRatio = 2.0; s.compThreshold = -15.0;
    CHECK (std::abs (s2::makeupDb (s) - 8.9) < 0.2);
    s.compRatio = 10.0;
    CHECK (std::abs (s2::makeupDb (s) - 13.2) < 0.3);
    s.compRatio = 3.0; s.compThreshold = -12.0; // slice 2: the S5 render's +8.75 dB
    CHECK (std::abs (s2::makeupDb (s) - 8.75) < 0.2);
    s.compRatio = 10.0; s.compThreshold = -15.0;
    s2::Strip strip;
    strip.prepare (48000.0);
    s.gateRange = 20.0; s.gateThreshold = -20.0; s.lfGain = 6.0; s.hfGain = -6.0; s.compPeak = true;
    std::vector<float> l (4096), r (4096);
    for (int i = 0; i < 4096; ++i) l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (0.05 * i);
    strip.process (l.data(), r.data(), 4096, s);
    bool finite = true;
    for (float v : l) finite = finite && std::isfinite (v);
    CHECK (finite && strip.compReductionDb() > 1.0f);
}

// issue #77: a new Aether runs the measured dynamic air model (its Mid air was the round-114 exciter, nearly
// inaudible); its steady-tone lifts sit on the Fresh Air trial's (black-box sweep renders, -12 dBFS: Mid air 50 %
// +2.2 dB at 5 kHz, 100 % +4.8 dB at 8 kHz; High air 100 % +1.0 dB at 1 kHz) and quiet highs come up strongly
TEST (aether_issue77_default_model_and_levels_tp172c)
{
    using namespace mixsuite96;
    namespace air = lynx::mixsuite::aether;
    auto p = defaults (EffectKind::aether);
    CHECK (p[air::dynamic] == 1.0f);
    const double sr = 48000.0;
    auto gainDb = [&] (const lynx::EffectParams& q, double hz, double amp) {
        const auto y = run (EffectKind::aether, q, sine (hz, amp, 48000, sr), sr);
        return 20.0 * std::log10 (amplitude (y, hz, 24000, 16384, sr) / amp);
    };
    CHECK (std::abs (gainDb (p, 5000.0, 0.25)) < 0.3); // both amounts at 0: transparent
    p[air::midAir] = 0.5f;
    const auto mid50 = gainDb (p, 5000.0, 0.25);
    CHECK (mid50 > 1.5 && std::abs (mid50 - 2.2) < 1.0); // Mid air is clearly audible
    p[air::midAir] = 1.0f;
    CHECK (std::abs (gainDb (p, 8000.0, 0.25) - 4.8) < 1.0);
    p[air::midAir] = 0.0f;
    p[air::highAir] = 1.0f;
    CHECK (std::abs (gainDb (p, 1000.0, 0.25) - 1.0) < 0.6);
    CHECK (gainDb (p, 12000.0, 0.01) > 15.0); // a quiet 12 kHz tone: the dynamic lift (vocal air +20 dB on the trial)
    // a project saved before the dynamic model existed keeps the shelf + exciter it was set up with
    lynx::EffectSlot old = lynx::makeEffectSlot (EffectKind::aether);
    lynx::upgradeLoadedEffectParams (old, (std::size_t) air::dynamic);
    CHECK (old.params[air::dynamic] == 0.0f);
    lynx::EffectSlot now = lynx::makeEffectSlot (EffectKind::aether);
    lynx::upgradeLoadedEffectParams (now, (std::size_t) air::count);
    CHECK (now.params[air::dynamic] == 1.0f);
}
