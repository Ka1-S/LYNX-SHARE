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

// slice 5: Harrison 32Classic and MPC channel strip states -> Ridge's own engine (laws fitted on their trials)
namespace tp172c
{
inline lynx::EffectSlot harrisonSlot (const std::string& plugin, const std::string& xml)
{
    using namespace lynx;
    ForeignPlugin fp;
    fp.format = ForeignPlugin::Format::vst3;
    fp.name = plugin;
    fp.state = substtest::flVst3State (substtest::juceXmlComponent (xml));
    const auto id = pluginIdentity (fp);
    const auto* entry = findPluginSubstitute (id, false);
    if (entry == nullptr)
        return {};
    int n = 0;
    return makeSubstituteSlot (*entry, &id, &n);
}
inline std::string p32 (const char* id, double v) { return std::string ("<PARAM id=\"") + id + "\" value=\"" + std::to_string (v) + "\"/>"; }
} // namespace tp172c

TEST (ridge_harrison_strips_translated_tp172c5)
{
    using namespace lynx;
    using tp172c::p32;
    namespace r = mixsuite::ridge;
    const std::string base = p32 ("bypass", 0) + p32 ("compressor_in", 1) + p32 ("compressor_mode", 1) + p32 ("compressor_attack", 20)
                             + p32 ("compressor_release", 50) + p32 ("gate_in", 1) + p32 ("gate_depth", 0) + p32 ("gate_threshold", 0)
                             + p32 ("equalizer_in", 1) + p32 ("equalizer_eq_in", 1) + p32 ("equalizer_filters_in", 1)
                             + p32 ("equalizer_highpass_filter_freq", 25) + p32 ("equalizer_lowpass_filter_freq", 20000)
                             + p32 ("input_trim", 0) + p32 ("output_trim", 0) + p32 ("FaderLevel", 0);
    auto c32 = [&] (const std::string& params) {
        return tp172c::harrisonSlot ("Harrison 32Classic Channel Strip",
                                     "<SSL_PLUGIN_STATE PluginName=\"Harrison 32Classic Channel Strip\" Version=\"2.0.20\" StateASelected=\"1\"><A><PROCESSOR_STATE>"
                                         + base + params + "</PROCESSOR_STATE></A><B><PROCESSOR_STATE/></B></SSL_PLUGIN_STATE>");
    };
    auto s = c32 (p32 ("equalizer_band_freq_2", 1421.74) + p32 ("equalizer_band_gain_2", -4.0) + p32 ("compressor_ratio", 4.0)
                  + p32 ("compressor_threshold", -20.0) + p32 ("CompAutoMakeUp", 1.0));
    auto& p = s.params;
    CHECK (s.kind == EffectKind::ridgeStrip && ! r::k4Voicing (p) && ! r::modernVoicing (p) && p[r::curve] < 0.5f);
    CHECK (std::abs (r::hmfHz (p[r::hmfFreq]) - 1421.74) < 0.5 && std::abs (r::bandGainDb (p[r::hmfGain], false) + 4.0) < 1e-3);
    CHECK (std::abs (r::ratio (p[r::compRatio]) - 4.0) < 0.01 && std::abs (r::compThresholdDb (p[r::compThreshold]) + 19.0) < 1e-3);
    CHECK (std::abs (r::trimDb (p[r::output]) - 7.5) < 0.01);      // its auto make-up: half of 20 dB x (1 - 1/4)
    CHECK (std::abs (r::hpfHz (p[r::hpf]) - 23.25) < 0.1 && p[r::lpf] > 0.0f); // 12 dB/oct 25 Hz -> Ridge's 18 dB/oct lower
    s = c32 (p32 ("compressor_mode", 0) + p32 ("compressor_ratio", 10.0) + p32 ("gate_depth", 20) + p32 ("gate_threshold", -30)
             + p32 ("gate_mode", 1) + p32 ("equalizer_band_freq_4", 111.35) + p32 ("equalizer_band_gain_4", 13.74));
    CHECK (s.params[r::compRatio] == 0.0f);                                         // the Leveler: no static reduction
    {   // a -45 dBFS threshold: Ridge's -40 floor + 4 dB more input, taken back at the output
        const auto d = c32 (p32 ("compressor_ratio", 30.0) + p32 ("compressor_threshold", -45.0) + p32 ("CompAutoMakeUp", 0.0));
        CHECK (std::abs (r::compThresholdDb (d.params[r::compThreshold]) + 40.0) < 1e-3 && std::abs (r::trimDb (d.params[r::input]) - 4.0) < 0.01);
        CHECK (std::abs (r::trimDb (d.params[r::output]) + 4.0) < 0.01);
    }
    CHECK (std::abs (r::gateRangeDb (s.params[r::gateRange]) - 20.0) < 1e-3 && s.params[r::expander] > 0.5f);
    CHECK (std::abs (r::lfHz (s.params[r::lfFreq]) - 174.2) < 0.5 && std::abs (r::bandGainDb (s.params[r::lfGain], false) - 11.67) < 0.01);

    const std::string mpc = "<HarrisonMPCChannelStrip plugin_size=\"1.0\" version=\"327936\">" + p32 ("bypass", 0) + p32 ("mpc_compressorin", 1)
                            + p32 ("mpccomp_ratio", 1.5) + p32 ("mpccomp_threshold", -10) + p32 ("mpccomp_release", 100) + p32 ("mpccomp_attack", 5)
                            + p32 ("mpc_equalizerin", 1) + p32 ("mpceq_bandGain3", 6) + p32 ("mpceq_bandFreq3", 185) + p32 ("mpceq_bandQ3", 3)
                            + p32 ("mpceq_bandShape3", 7) + p32 ("mpceq_bandGain6", -5) + p32 ("mpceq_bandFreq6", 1838) + p32 ("mpceq_bandQ6", 3)
                            + p32 ("mpceq_bandShape6", 7) + p32 ("mpc_trim", 0) + "</HarrisonMPCChannelStrip>";
    s = tp172c::harrisonSlot ("Harrison_MPCChannelStrip", mpc);
    CHECK (s.kind == EffectKind::ridgeStrip && std::abs (r::ratio (s.params[r::compRatio]) - 1.5) < 0.01);
    CHECK (std::abs (r::lmfHz (s.params[r::lmfFreq]) - 200.0) < 0.5 && std::abs (r::bandGainDb (s.params[r::lmfGain], false) - 6.0) < 1e-3);
    CHECK (std::abs (r::hmfHz (s.params[r::hmfFreq]) - 1838.0) < 1.0 && std::abs (r::bandGainDb (s.params[r::hmfGain], false) + 5.0) < 1e-3);
}

// slice 6: Ridge's DRIVE section (off by default: the old sound, no latency), its curve, and the states that use it
// (Harrison 32C saturator, SSL Guitarstrip; the VCC Channel moved to Kiln Channel in slice 7)
TEST (ridge_drive_section_tp172c6)
{
    using namespace lynx;
    using tp172c::p32;
    namespace r = mixsuite::ridge;
    auto p = makeEffectSlot (EffectKind::ridgeStrip).params;
    CHECK ((int) effectInfo (EffectKind::ridgeStrip).params.size() == r::count && p[r::driveOn] == 0.0f);
    auto run = [] (const EffectParams& q, int* latency) {
        auto fx = makeEffect (EffectKind::ridgeStrip);
        fx->prepare (48000.0);
        std::vector<float> l (24000), rr (24000);
        for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.3f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        for (int at = 0; at < 24000; at += 512)
            fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), q, 120.0);
        if (latency != nullptr)
            *latency = fx->latencySamples();
        return l;
    };
    int lat = -1;
    const auto off = run (p, &lat);
    CHECK (lat == 0);
    auto q = p;
    q[r::driveModel] = r::driveModelValue (r::driveTube);   // the model alone changes nothing while the section is out
    q[r::driveAmount] = 1.0f;
    CHECK (run (q, nullptr) == off);
    q[r::driveOn] = 1.0f;
    const auto on = run (q, &lat);
    CHECK (lat == mixsuite::Oversampler::latency() && on != off);
    // the curve: Saturate at 0 dB = its measured +0.9 dB small-signal gain, compressing toward full scale
    auto c = p;
    c[r::driveModel] = r::driveModelValue (r::driveSaturate);
    c[r::driveAmount] = r::driveAmountValue (r::driveSaturate, 0.0);
    CHECK (std::abs (20.0 * std::log10 (r::driveCurve (c, 1e-4) / 1e-4) - 0.9) < 0.05);
    CHECK (r::driveCurve (c, 1.0) < 1.0 && r::driveCurve (c, 0.5) > r::driveCurve (c, 0.25) && r::driveCurve (c, -0.5) < 0.0);
    CHECK (std::abs (r::driveAmountUnits (r::driveSaturate, r::driveAmountValue (r::driveSaturate, 6.0)) - 6.0) < 1e-4);
    CHECK (std::string (r::driveModelName (r::driveGuitarAmp)) == "Guitar amp");

    // the 32C's saturator -> Saturate at its dial
    auto s = tp172c::harrisonSlot ("Harrison 32Classic Channel Strip",
                                   "<SSL_PLUGIN_STATE PluginName=\"Harrison 32Classic Channel Strip\" Version=\"2.0.20\" StateASelected=\"1\"><A><PROCESSOR_STATE>"
                                       + p32 ("bypass", 0) + p32 ("compressor_in", 0) + p32 ("gate_in", 0) + p32 ("equalizer_in", 1)
                                       + p32 ("input_trim", 0) + p32 ("output_trim", 0) + p32 ("FaderLevel", 0) + p32 ("saturator_in", 1)
                                       + p32 ("saturator_drive", 12.0) + p32 ("equalizer_eq_in", 1) + p32 ("equalizer_filters_in", 0)
                                       + p32 ("compressor_mode", 1) + "</PROCESSOR_STATE></A><B><PROCESSOR_STATE/></B></SSL_PLUGIN_STATE>");
    CHECK (s.params[r::driveOn] == 1.0f && r::driveModelIndex (s.params[r::driveModel]) == r::driveSaturate);
    CHECK (std::abs (r::driveAmountUnits (r::driveSaturate, s.params[r::driveAmount]) - 12.0) < 1e-3);
}

// slice 8: Fret Channel and Shell Channel, LYNX's guitar / bass and drum channel strips — their laws, latencies, process
// order, DSP sanity and the Guitarstrip / Drumstrip states translated 1:1 onto them
TEST (fret_shell_channels_tp172c8)
{
    using namespace lynx;
    namespace f = mixsuite::fret;
    namespace d = mixsuite::shell;
    namespace r = mixsuite::ridge;
    auto p32 = [] (const char* id, double x) { return std::string ("<PARAM id=\"") + id + "\" value=\"" + std::to_string (x) + "\"/>"; };
    CHECK ((int) effectInfo (EffectKind::fretChannel).params.size() == f::count && std::string (effectInfo (EffectKind::fretChannel).name) == "Fret Channel");
    CHECK ((int) effectInfo (EffectKind::shellChannel).params.size() == d::count && std::string (effectInfo (EffectKind::shellChannel).name) == "Shell Channel");
    // the laws round-trip in the reference's units
    CHECK (std::abs (f::midKHz (f::midValue (2.65)) - 2.65) < 1e-3 && std::abs (f::midKHz (f::midValue (0.7)) - 0.7) < 1e-3);
    CHECK (f::hpfHz (0.0f) == 0.0 && std::abs (f::hpfHz (f::hpfValue (80.0)) - 80.0) < 0.01);
    CHECK (std::abs (f::lpfKHz (f::lpfValue (8.0)) - 8.0) < 1e-3);
    for (int m = 0; m < 5; ++m)
        CHECK (f::modeOf (f::modeValue (m)) == m);
    CHECK (std::abs (d::thresholdDb (d::thresholdValue (-24.0)) + 24.0) < 1e-3 && std::abs (d::releaseS (d::releaseValue (0.5)) - 0.5) < 1e-4);
    CHECK (std::abs (d::hfHz (d::hfValue (8000.0)) - 8000.0) < 0.5 && std::abs (d::lfHz (d::lfValue (60.0)) - 60.0) < 0.01);
    // the process order: 120 orders, lexicographic over gate, shaper, low, high, listen mic; index <-> value <-> order
    for (int i = 0; i < 120; ++i)
    {
        CHECK (d::orderIndex (d::orderValue (i)) == i);
        CHECK (d::indexOf (d::orderOf (i)) == i);
    }
    CHECK ((d::orderOf (0) == std::array<int, d::modules> { d::gate, d::shaper, d::lowEnhancer, d::highEnhancer, d::listenMic }));
    CHECK ((d::orderOf (119) == std::array<int, d::modules> { d::listenMic, d::highEnhancer, d::lowEnhancer, d::shaper, d::gate }));

    auto run = [] (EffectKind kind, const EffectParams& q, int* latency) {
        auto fx = makeEffect (kind);
        fx->prepare (48000.0);
        std::vector<float> l (24000), rr (24000);
        for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        for (int at = 0; at < 24000; at += 512)
            fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), q, 120.0);
        if (latency != nullptr) *latency = fx->latencySamples();
        return l;
    };
    auto rmsDb = [] (const std::vector<float>& x, int from) {
        double e = 0;
        for (std::size_t i = (std::size_t) from; i < x.size(); ++i) e += (double) x[i] * x[i];
        return 10.0 * std::log10 (std::max (1e-30, e / (double) (x.size() - (std::size_t) from)));
    };
    const auto ref = 20.0 * std::log10 (0.25 / std::sqrt (2.0));
    // the latencies: Fret's 20 ms centre delay + the drive's oversampler, Shell's fixed 4.1 ms
    int lat = -1;
    const auto fp = makeEffectSlot (EffectKind::fretChannel).params;
    const auto fa = run (EffectKind::fretChannel, fp, &lat);
    CHECK (lat == f::baseDelaySamples (48000.0) + mixsuite::Oversampler::latency() && f::baseDelaySamples (48000.0) == 960);
    CHECK (std::abs (rmsDb (fa, 4800) - ref) < 1.0);
    const auto sp = makeEffectSlot (EffectKind::shellChannel).params;
    const auto sa = run (EffectKind::shellChannel, sp, &lat);
    CHECK (lat == 199 && d::latencySamples (48000.0) == 199);
    CHECK (std::abs (rmsDb (sa, 4800) - ref) < 1.0);
    // the output gains move the level by their dB; the drive colours the sine
    auto q = fp;
    q[f::output] = (float) ((6.0 + 24.0) / 48.0);
    CHECK (std::abs (rmsDb (run (EffectKind::fretChannel, q, nullptr), 4800) - rmsDb (fa, 4800) - 6.0) < 0.1);
    q = fp;
    q[f::driveIn] = 1.0f;
    q[f::guitarDrive] = 0.6f;
    CHECK (run (EffectKind::fretChannel, q, nullptr) != fa);
    auto t = sp;
    t[d::output] = (float) ((-6.0 + 20.0) / 40.0);
    CHECK (std::abs (rmsDb (run (EffectKind::shellChannel, t, nullptr), 4800) - rmsDb (sa, 4800) + 6.0) < 0.1);
    // EQ Auto listen: nothing on its own (the original renders the same with it on); the band the editor holds, alone
    q = fp;
    q[f::autoListen] = 1.0f;
    CHECK (run (EffectKind::fretChannel, q, nullptr) == fa);
    q[f::listenBand] = 2.0f / 3.0f;
    q[f::midFreq] = f::midValue (5.0);
    CHECK (rmsDb (run (EffectKind::fretChannel, q, nullptr), 4800) < rmsDb (fa, 4800) - 12.0); // 440 Hz far under a 5 kHz band
    q[f::midFreq] = f::midValue (0.5);
    CHECK (std::abs (rmsDb (run (EffectKind::fretChannel, q, nullptr), 4800) - rmsDb (fa, 4800)) < 3.0);
    t = sp;
    t[d::lfOn] = 1.0f;
    t[d::lfDrive] = 0.5f;
    t[d::lfAmount] = 1.0f;
    CHECK (run (EffectKind::shellChannel, t, nullptr) != sa);
    // tp172c9: the guitar Boost's knob law at drive (post-amp, asymmetric: about -11 dB at -50 %, +5 dB at +50 %), the bass
    // voicings ahead of the amp
    {
        auto low = [] (const EffectParams& v) {
            auto fx = makeEffect (EffectKind::fretChannel);
            fx->prepare (48000.0);
            std::vector<float> l (24000), rr (24000);
            for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.01f * (float) std::sin (2.0 * 3.14159265358979 * 50.0 * i / 48000.0);
            for (int at = 0; at < 24000; at += 512)
                fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), v, 120.0);
            double e = 0;
            for (std::size_t i = 4800; i < l.size(); ++i) e += (double) l[i] * l[i];
            return 10.0 * std::log10 (std::max (1e-30, e / (double) (l.size() - 4800)));
        };
        auto g = fp;
        g[f::driveIn] = 1.0f;
        g[f::guitarDrive] = 0.5f;
        const auto g0 = low (g);
        g[f::guitarBoost] = 0.25f;
        const auto gCut = low (g) - g0;
        g[f::guitarBoost] = 0.75f;
        const auto gLift = low (g) - g0;
        CHECK (gCut < -6.0 && gLift > 2.0 && gLift < 9.0 && -gCut > gLift);
        auto b = fp;
        b[f::driveIn] = 1.0f;
        b[f::driveModel] = 1.0f;
        b[f::bassDrive] = 0.5f;
        const auto b0 = run (EffectKind::fretChannel, b, nullptr);
        b[f::bassPunch] = 0.75f;
        CHECK (run (EffectKind::fretChannel, b, nullptr) != b0);
    }
    // tp172c9: part of the amps' tone sits before the curve: with the pre-amp at +30 dB the guitar amp compresses a
    // 10 kHz sine far less than a 1 kHz one (the original: +24 / +9 dB over drive 0), the bass amp its highs more
    {
        auto tone = [] (const EffectParams& v, double hz) {
            auto fx = makeEffect (EffectKind::fretChannel);
            fx->prepare (48000.0);
            std::vector<float> l (24000), rr (24000);
            for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.178f * (float) std::sin (2.0 * 3.14159265358979 * hz * i / 48000.0);
            for (int at = 0; at < 24000; at += 512)
                fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), v, 120.0);
            double e = 0;
            for (std::size_t i = 9600; i < l.size(); ++i) e += (double) l[i] * l[i];
            return 10.0 * std::log10 (std::max (1e-30, e / (double) (l.size() - 9600)));
        };
        auto g = fp;
        g[f::driveIn] = 1.0f;
        const auto lo1 = tone (g, 1000.0), lo10 = tone (g, 10000.0);
        g[f::preAmp] = 1.0f;
        const auto hi1 = tone (g, 1000.0), hi10 = tone (g, 10000.0);
        CHECK ((hi10 - lo10) - (hi1 - lo1) > 8.0);
        auto b = fp;
        b[f::driveIn] = 1.0f;
        b[f::driveModel] = 1.0f;
        const auto b1 = tone (b, 1000.0), b5 = tone (b, 5000.0);
        b[f::bassDrive] = 1.0f;
        CHECK ((tone (b, 5000.0) - b5) < (tone (b, 1000.0) - b1) - 2.0);
    }

    // Guitarstrip -> Fret Channel, 1:1 in its own units
    auto gs = [&p32] (const std::string& params) {
        return tp172c::harrisonSlot ("SSL Guitarstrip",
                                     "<SSL_PLUGIN_STATE PluginName=\"SSL Guitarstrip\" Version=\"1.3.2\" StateASelected=\"0\"><A><PROCESSOR_STATE>"
                                         + p32 ("Drive", 0) + "</PROCESSOR_STATE></A><B><PROCESSOR_STATE>" + params
                                         + "</PROCESSOR_STATE></B></SSL_PLUGIN_STATE>");
    };
    auto s = gs (p32 ("Mid Frequency", 2.65) + p32 ("Mid Gain", 6) + p32 ("Compressor", 1) + p32 ("Compressor Mode", 4) + p32 ("Compressor Threshold", -18)
                 + p32 ("Drive", 1) + p32 ("Drive Type", 0) + p32 ("Guitar Drive", 5.5) + p32 ("Guitar Pre-Amp", 12) + p32 ("High-Pass Filter", 20));
    CHECK (s.kind == EffectKind::fretChannel && s.enabled);
    CHECK (std::abs (f::midKHz (s.params[f::midFreq]) - 2.65) < 1e-3 && std::abs (f::shelfDb (s.params[f::midGain]) - 6.0) < 1e-3);
    CHECK (s.params[f::compIn] == 1.0f && f::modeOf (s.params[f::compMode]) == 4 && std::abs (f::thresholdDb (s.params[f::compThreshold]) + 18.0) < 1e-3);
    CHECK (s.params[f::driveIn] == 1.0f && s.params[f::driveModel] == 0.0f && std::abs (f::ampDrive (s.params[f::guitarDrive]) - 5.5) < 1e-3);
    CHECK (std::abs (f::preAmpDb (s.params[f::preAmp]) - 12.0) < 1e-3 && s.params[f::hpf] == 0.0f);
    CHECK (s.params[f::autoListen] == 0.0f);
    // (a state needs its ten-odd parameters to count as one)
    s = gs (p32 ("Bypass", 1) + p32 ("Drive", 1) + p32 ("Drive Type", 1) + p32 ("Bass Drive", 11) + p32 ("Low-Pass Filter", 20) + p32 ("EQ", 1)
            + p32 ("Input Gain", 0) + p32 ("Output Gain", 0) + p32 ("Low Gain", 0) + p32 ("Mid Gain", 0) + p32 ("High Gain", 0));
    CHECK (s.kind == EffectKind::fretChannel && ! s.enabled && s.params[f::driveModel] == 1.0f && s.params[f::bassDrive] == 1.0f && s.params[f::lpf] == 1.0f);

    // Drumstrip -> Shell Channel: every module, and its process order
    s = tp172c::harrisonSlot ("SSL Native Drumstrip v6",
                              "<SSL_PLUGIN_STATE PluginName=\"SSL Native Drumstrip v6\" Version=\"6.0\" StateASelected=\"1\"><A><PROCESSOR_STATE>"
                                  + p32 ("bypass", 0) + p32 ("gateOn", 1) + p32 ("gateOpen", -24) + p32 ("gateClose", -30) + p32 ("gateRange", 80)
                                  + p32 ("gateRelease", 0.5) + p32 ("gateAttack", 0.05) + p32 ("input", 3) + p32 ("output", -2) + p32 ("TransOn", 1)
                                  + p32 ("hfOn", 1) + p32 ("hfFreq", 8000) + p32 ("lfOn", 0) + p32 ("lmcOn", 1) + p32 ("processOrder", 119.0 / 119.0)
                                  + "</PROCESSOR_STATE></A><B><PROCESSOR_STATE/></B></SSL_PLUGIN_STATE>");
    CHECK (s.kind == EffectKind::shellChannel && s.enabled);
    CHECK (s.params[d::gateOn] == 1.0f && std::abs (d::thresholdDb (s.params[d::gateOpen]) + 24.0) < 1e-3 && std::abs (d::rangeDb (s.params[d::gateRange]) - 80.0) < 1e-3);
    CHECK (std::abs (d::releaseS (s.params[d::gateRelease]) - 0.5) < 1e-4 && std::abs (d::attackS (s.params[d::gateAttack]) - 0.05) < 1e-4);
    CHECK (std::abs (d::gainDb (s.params[d::input]) - 3.0) < 1e-3 && std::abs (d::gainDb (s.params[d::output]) + 2.0) < 1e-3);
    CHECK (s.params[d::tsOn] == 1.0f && s.params[d::hfOn] == 1.0f && s.params[d::lfOn] == 0.0f && s.params[d::lmcOn] == 1.0f);
    CHECK (std::abs (d::hfHz (s.params[d::hfFreq]) - 8000.0) < 0.5 && d::orderIndex (s.params[d::processOrder]) == 119);
}

// slice 7: Kiln Channel, LYNX's console channel — the six console models on the measured drive stages, its noise floor,
// group bypass shared by the group's members, the meters, and the VCC Channel state translated onto it
TEST (kiln_channel_tp172c7)
{
    using namespace lynx;
    namespace k = mixsuite::kiln;
    namespace r = mixsuite::ridge;
    const auto& info = effectInfo (EffectKind::kilnChannel);
    CHECK ((int) info.params.size() == k::count && std::string (info.name) == "Kiln Channel");
    auto p = makeEffectSlot (EffectKind::kilnChannel).params;
    CHECK (p[k::noiseReduction] == 1.0f && p[k::group] == 0.0f && std::abs (k::driveDb (p[k::drive]) - 6.0) < 1e-3);
    CHECK (std::abs (k::trimDb (p[k::input])) < 1e-6 && std::abs (k::mainDb (p[k::mainOut])) < 1e-6);
    for (int c = 0; c < 6; ++c)
        CHECK (k::consoleStep (k::consoleValue (c)) == c);
    CHECK (mixsuite::paramText (EffectKind::kilnChannel, k::console, k::consoleValue (2)) == "American A");
    CHECK (mixsuite::paramText (EffectKind::kilnChannel, k::group, 0.0f) == "None" && mixsuite::paramText (EffectKind::kilnChannel, k::group, 0.5f) == "4");
    auto run = [] (const EffectParams& q, int* latency, std::unique_ptr<Effect>* keep = nullptr) {
        auto fx = makeEffect (EffectKind::kilnChannel);
        fx->prepare (48000.0);
        std::vector<float> l (24000), rr (24000);
        for (int i = 0; i < 24000; ++i) l[(std::size_t) i] = rr[(std::size_t) i] = 0.25f * (float) std::sin (2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        for (int at = 0; at < 24000; at += 512)
            fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), q, 120.0);
        if (latency != nullptr)
            *latency = fx->latencySamples();
        if (keep != nullptr)
            *keep = std::move (fx);
        return l;
    };
    auto rmsDb = [] (const std::vector<float>& x, int from) {
        double e = 0;
        for (std::size_t i = (std::size_t) from; i < x.size(); ++i) e += (double) x[i] * x[i];
        return 10.0 * std::log10 (std::max (1e-30, e / (double) (x.size() - (std::size_t) from)));
    };
    int lat = -1;
    const auto a = run (p, &lat);
    CHECK (lat == mixsuite::Oversampler::latency());
    // each console colours the sine its own way; the level stays near unity (the stages are normalised at 1 kHz)
    auto q = p;
    q[k::console] = k::consoleValue (5);
    const auto tube = run (q, nullptr);
    CHECK (tube != a && std::abs (rmsDb (a, 4800) - rmsDb (tube, 4800)) < 3.0);
    CHECK (std::abs (rmsDb (a, 4800) - 20.0 * std::log10 (0.25 / std::sqrt (2.0))) < 2.0);
    // the output trim and the main output move the level by their dB
    q = p;
    q[k::output] = (float) ((6.0 + 18.0) / 36.0);
    CHECK (std::abs (rmsDb (run (q, nullptr), 4800) - rmsDb (a, 4800) - 6.0) < 0.1);
    q = p;
    q[k::mainOut] = (float) ((-12.0 + 24.0) / 48.0);
    CHECK (std::abs (rmsDb (run (q, nullptr), 4800) - rmsDb (a, 4800) + 12.0) < 0.1);
    // noise: off by default (noise reduction on); with it off, silence carries the console's hiss (Tube: ~ -102 dBFS)
    auto silence = [] (const EffectParams& q) {
        auto fx = makeEffect (EffectKind::kilnChannel);
        fx->prepare (48000.0);
        std::vector<float> l (24000, 0.0f), rr (24000, 0.0f);
        for (int at = 0; at < 24000; at += 512)
            fx->process (l.data() + at, rr.data() + at, std::min (512, 24000 - at), q, 120.0);
        double e = 0;
        for (const auto x : l) e += (double) x * x;
        return 10.0 * std::log10 (std::max (1e-30, e / 24000.0));
    };
    q = p;
    q[k::console] = k::consoleValue (5);
    CHECK (silence (q) < -200.0);
    q[k::noiseReduction] = 0.0f;
    CHECK (std::abs (silence (q) + 101.9) < 1.5);
    // group bypass: the console out (the main gains stay), shared by every member of the group
    q = p;
    q[k::groupBypass] = 1.0f;
    const auto byp = run (q, nullptr);
    CHECK (std::abs (rmsDb (byp, 4800) - 20.0 * std::log10 (0.25 / std::sqrt (2.0))) < 0.05);
    {
        auto member = p, other = p;
        member[k::group] = other[k::group] = 3.0f / 8.0f;
        member[k::groupBypass] = 1.0f;
        std::unique_ptr<Effect> held;
        run (member, nullptr, &held);
        const auto o = run (other, nullptr);
        CHECK (std::abs (rmsDb (o, 4800) - 20.0 * std::log10 (0.25 / std::sqrt (2.0))) < 0.05);
        held.reset();
        CHECK (run (other, nullptr) == a);
    }
    // the meters: input and output peaks on control outputs 2 / 3 (the VU and the side columns)
    {
        std::unique_ptr<Effect> fx;
        run (p, nullptr, &fx);
        CHECK (fx->controlOutput (2) > 0.2f && fx->controlOutput (3) > 0.2f);
    }

    // VCC Channel -> Kiln Channel: the playing slot's console, drive, trims and switches, the group and the main gains
    auto vcc = [] (const std::string& slot1Console, const std::string& group) {
        ForeignPlugin fp;
        fp.format = ForeignPlugin::Format::vst3;
        fp.name = "VM VCC Channel";
        const std::string xml = "<VccC-state><package version=\"1.0\"><section id=\"Common\"><entry id=\"Current slot\" type=\"uint32\" value=\"1\"/>"
                                "<entry id=\"Group\" type=\"float64\" value=\"" + group + "\"/></section>"
                                "<section id=\"Slot0\"><entry id=\"Console\" type=\"float64\" value=\"0.0\"/><entry id=\"Drive\" type=\"float64\" value=\"0.1\"/></section>"
                                "<section id=\"Slot1\"><entry id=\"Console\" type=\"float64\" value=\"" + slot1Console + "\"/>"
                                "<entry id=\"Drive\" type=\"float64\" value=\"0.75\"/><entry id=\"Input\" type=\"float64\" value=\"0.75\"/>"
                                "<entry id=\"Output\" type=\"float64\" value=\"0.25\"/><entry id=\"NoiseReduction\" type=\"float64\" value=\"0.25\"/>"
                                "<entry id=\"GroupBypass\" type=\"float64\" value=\"0.75\"/><entry id=\"StandaloneOutputGain\" type=\"float64\" value=\"0.625\"/>"
                                "<entry id=\"Bypass\" type=\"float64\" value=\"0.25\"/></section></package></VccC-state>";
        fp.state = substtest::flVst3State (std::vector<std::uint8_t> (xml.begin(), xml.end()));
        const auto id = pluginIdentity (fp);
        const auto* entry = findPluginSubstitute (id, false);
        if (entry == nullptr)
            return EffectSlot {};
        int n = 0;
        return makeSubstituteSlot (*entry, &id, &n);
    };
    auto s = vcc ("0.45", "0.5");                                         // the third step: US A; Group 4
    CHECK (s.kind == EffectKind::kilnChannel && s.enabled && k::consoleStep (s.params[k::console]) == 2);
    CHECK (std::abs (s.params[k::drive] - 0.75f) < 1e-6 && s.params[k::noiseReduction] == 0.0f && s.params[k::groupBypass] == 1.0f);
    CHECK (std::abs (k::trimDb (s.params[k::input]) - 9.0) < 0.01 && std::abs (k::trimDb (s.params[k::output]) + 9.0) < 0.01);
    CHECK (k::groupOf (s.params[k::group]) == 4 && std::abs (k::mainDb (s.params[k::mainOut]) - 6.0) < 0.01 && std::abs (k::mainDb (s.params[k::mainIn])) < 1e-6);
    CHECK (s.params[k::ioLink] == 1.0f && s.params[k::ioGroup] == 1.0f);         // absent from the state: the original's defaults (on)
    // Link I/O: the output moves against the input; off, the input moves alone
    {
        auto q = makeEffectSlot (EffectKind::kilnChannel).params;
        auto e = k::linkedEdit (q, k::input, 0.75f);
        CHECK (e.size() == 2 && e[1].first == k::output && std::abs (k::trimDb (e[1].second) + 9.0) < 0.01);
        q[k::ioLink] = 0.0f;
        CHECK (k::linkedEdit (q, k::input, 0.75f).size() == 1 && k::linkedEdit (q, k::output, 0.75f).size() == 1);
    }
    s = vcc ("0.95", "0.0");
    CHECK (k::consoleStep (s.params[k::console]) == 5 && k::groupOf (s.params[k::group]) == 0);
    (void) r::count;
}
