// Round tp172c: Ridge Strip's reference strips — SSL Native Channel Strip 2, 4K G and 4K B states load into the 4K
// voicing (Channel Strip 2: E / G EQ = Brown / Black, its own "out" positions, no VCA colour; 4K G: Black, LMF ÷3 /
// HMF ×3; 4K B: Brown, its compressor not modelled); the editor's three columns fit the window.
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
    CHECK (r::k4Voicing (p) && r::k4ColourIndex (p[r::k4Colour]) == 0 && p[r::compRatio] == 0.0f);
    CHECK (std::abs (r::bandGainDb (p[r::lfGain], false) - 6.0) < 1e-4);
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
