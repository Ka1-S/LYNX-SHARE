// Round 190mm (sub-round "scheps"): the Waves Scheps Omni Channel on Ridge Strip's hidden omni law (owner's permission
// of 2026-11-17 for clean-room REA of third-party binaries; /mnt/e/lynx-work/waves4_190mm/scheps/HANDOFF.md,
// docs/analise/REA_rodada190mm_scheps.md).
// - Ridge gets a hidden omni law block at fixed indices 100 … 191 (MixSuite.h OmniParam): 0 for new instances and every
//   project saved before (bit for bit: waves4_190mm/scheps/lab/gold.sh); only the Scheps translator sets it.
// - The Scheps Omni Channel had no entry before (such projects lost the plug-in): its five modules, their on / off and
//   the module order are read from its state.
#include <cmath>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/MixSuite.h"
#include "core/PluginSubstitutes.h"

namespace parity190mmscheps
{
namespace rg = lynx::mixsuite::ridge;
// a minimal state of the strip's form: the outer RealWorld list (order code at 57) and one slot per module
inline std::vector<std::uint8_t> state (const std::string& order, const std::string (&modules)[5], const bool (&bypass)[5])
{
    static const char* names[5] = { "Internal_SOC_Pre", "Internal_SOC_Gate", "Internal_SOC_DeEsser", "Internal_SOC_EQ", "Internal_SOC_Comp" };
    std::string outer;
    for (int i = 0; i < 61; ++i)
        outer += (i == 57 ? order : i == 58 ? std::string ("222221") : i < 6 ? std::string ("0") : std::string ("*")) + " ";
    std::string xml = "<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"\" GenericType=\"ACSS\">\n  <PresetHeader>\n   <PluginName>Scheps Omni Channel</PluginName>\n"
                      "   <ActiveSetup>SETUP_A</ActiveSetup>\n  </PresetHeader>\n  <PresetData Setup=\"SETUP_A\" SetupName=\"\">\n   <Parameters Type=\"RealWorld\">\n"
                      + outer + "\n</Parameters>\n   <PluginSpecificXMLData>\n";
    for (int k = 0; k < 5; ++k)
        xml += "    <slot index=\"" + std::to_string (k) + "\">\n     <slot_data>\n      <plugin_preset><![CDATA[<PresetChunkXMLTree version=\"3\">\n <Preset Name=\"\">\n"
               "  <PresetData Setup=\"SETUP_A\">\n   <Parameters Type=\"RealWorld\">\n" + modules[k] + "\n</Parameters>\n   <PluginSpecificXMLData />\n  </PresetData>\n"
               " </Preset>\n</PresetChunkXMLTree>\n]]></plugin_preset>\n      <plugin_name>" + names[k] + "</plugin_name>\n      <plugin_bypass>"
               + (bypass[k] ? "true" : "false") + "</plugin_bypass>\n     </slot_data>\n    </slot>\n";
    xml += "    <slot index=\"5\">\n    </slot>\n   </PluginSpecificXMLData>\n  </PresetData>\n </Preset>\n</PresetChunkXMLTree>\n";
    std::vector<std::uint8_t> c { 0, 0, 3, 0xe2, 0, 0, 0, 3, 0, 0, 0, 1, 'A', 'C', 'S', 'S', 's', 'e', 't', 'A', 0, 0, 3, 0xc6, 'X', 'P', 's', 't', 0 };
    c.insert (c.end(), xml.begin(), xml.end());
    return c;
}
inline std::string list (std::initializer_list<std::pair<int, double>> values, int n)
{
    std::vector<std::string> v ((std::size_t) n, "0");
    for (const auto& [i, x] : values) { char t[32]; std::snprintf (t, sizeof (t), "%g", x); v[(std::size_t) i] = t; }
    std::string s;
    for (const auto& x : v) s += x + " ";
    return s;
}
// the bank's defaults (every module present, values of a full reset) with some changes
inline std::string pre (std::initializer_list<std::pair<int, double>> c = {}) { auto s = list ({ { 15, 1 }, { 16, 20 }, { 17, 1 }, { 19, 1 }, { 20, 20000 }, { 21, 1 } }, 60); return c.size() ? list (c, 60) : s; }
inline std::string compModule (std::initializer_list<std::pair<int, double>> c) { return list (c, 88); }
inline lynx::EffectSlot translate (const std::vector<std::uint8_t>& st, int* n = nullptr)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = "WaveShell1-VST3 17.1_x64";
    id.state = st;
    const auto* e = lynx::findPluginSubstitute (id, false);
    if (e == nullptr) return lynx::makeEffectSlot (lynx::EffectKind::ridgeStrip);
    int k = 0;
    auto s = lynx::makeSubstituteSlot (*e, &id, &k);
    if (n != nullptr) *n = k;
    return s;
}
inline std::vector<float> sine (double sr, double hz, double db, int n)
{
    std::vector<float> x ((std::size_t) n);
    for (int i = 0; i < n; ++i) x[(std::size_t) i] = (float) (std::pow (10.0, db / 20) * std::sin (2 * 3.14159265358979 * hz * i / sr));
    return x;
}
inline void render (const lynx::EffectSlot& s, double sr, std::vector<float>& l, std::vector<float>& r)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    const int n = (int) l.size();
    for (int off = 0; off < n; off += 256)
        fx->process (l.data() + off, r.data() + off, std::min (256, n - off), s.params, 120.0);
}
inline double rmsDb (const std::vector<float>& x, int from)
{
    double e = 0;
    for (std::size_t i = (std::size_t) from; i < x.size(); ++i) e += (double) x[i] * x[i];
    return 10 * std::log10 (e / (double) (x.size() - (std::size_t) from) + 1e-30);
}
} // namespace parity190mmscheps

TEST (parity190mm_scheps_omni_law_is_hidden_and_off_for_new_and_old_instances)
{
    using namespace lynx;
    namespace rg = mixsuite::ridge;
    CHECK (effectInfo (EffectKind::ridgeStrip).params.size() == (std::size_t) rg::count && rg::count <= 100 && rg::omniEnd <= maxEffectParams);
    auto s = makeEffectSlot (EffectKind::ridgeStrip);
    const auto saved = savedEffectParamCount (s);
    CHECK (saved <= 100 && ! rg::omni::omniLawOn (s.params));
    for (int k = rg::omniLaw; k < rg::omniEnd; ++k) CHECK (s.params[(std::size_t) k] == 0.0f);
    s.params[(std::size_t) rg::omniLaw] = 1.0f;
    CHECK (rg::omni::omniLawOn (s.params) && savedEffectParamCount (s) == (std::size_t) rg::omniLaw + 1);
    // the scales round-trip
    CHECK (std::abs (rg::omni::db (rg::omni::dbValue (-24.0)) + 24.0) < 1e-4);
    CHECK (std::abs (rg::omni::hz (rg::omni::hzValue (2890.0)) - 2890.0) < 0.05);
    CHECK (std::abs (rg::omni::ms (rg::omni::msValue (88.9)) - 88.9) < 1e-3);
    CHECK (std::abs (rg::omni::ratio (rg::omni::ratioValue (3.5)) - 3.5) < 1e-4 && rg::omni::index (rg::omni::indexValue (3)) == 3);
    int order[5] = { 5, 1, 4, 3, 2 }, back[5] = {};
    rg::omni::orderOf (rg::omni::orderValue (order), back);
    CHECK (back[0] == 5 && back[1] == 1 && back[2] == 4 && back[3] == 3 && back[4] == 2);
    rg::omni::orderOf (0.0f, back); // a missing order is the reference's default chain: Pre, Comp, DS2, EQ, Gate
    CHECK (back[0] == rg::omni::pre && back[1] == rg::omni::comp && back[2] == rg::omni::ds2 && back[3] == rg::omni::eq && back[4] == rg::omni::gate);
}

TEST (parity190mm_scheps_law_off_is_ridge_bit_for_bit)
{
    using namespace lynx;
    using namespace parity190mmscheps;
    // omni values present but the law off: Ridge's own engine, sample for sample
    auto a = makeEffectSlot (EffectKind::ridgeStrip);
    auto b = a;
    for (int k = rg::omniLaw + 1; k < rg::omniEnd; ++k) b.params[(std::size_t) k] = 0.37f;
    for (const double sr : { 44100.0, 96000.0 })
    {
        auto l1 = sine (sr, 220.0, -6.0, 6000), r1 = sine (sr, 330.0, -9.0, 6000), l2 = l1, r2 = r1;
        render (a, sr, l1, r1);
        render (b, sr, l2, r2);
        CHECK (l1 == l2 && r1 == r2);
    }
}

TEST (parity190mm_scheps_state_translates_modules_order_and_bypass)
{
    using namespace lynx;
    using namespace parity190mmscheps;
    namespace o = rg::omni;
    // the bank's X4 compressor (VCA 16 : 1, -35 dB, 0.84 / 88.9 ms, +6 dB, 40 %) with the other modules bypassed
    const std::string mods[5] = { pre(), list ({}, 68), list ({}, 62), list ({ { 1, 1 }, { 7, 1 }, { 13, 1 }, { 19, 1 } }, 71),
                                  compModule ({ { 29, 0 }, { 34, 6 }, { 38, 16 }, { 40, -35 }, { 42, 88.9 }, { 44, 0.84 }, { 78, 40 } }) };
    const bool bypass[5] = { true, true, true, true, false };
    int n = 0;
    const auto s = translate (state ("153426", mods, bypass), &n);
    CHECK (s.kind == EffectKind::ridgeStrip && n > 0 && o::omniLawOn (s.params));
    CHECK (s.params[rg::oCompOn] == 1.0f && s.params[rg::oPreOn] == 0.0f && s.params[rg::oEqOn] == 0.0f && s.params[rg::oDsOn] == 0.0f);
    CHECK (std::abs (o::ratio (s.params[rg::oCompRatio]) - 16.0) < 1e-3 && std::abs (o::db (s.params[rg::oCompThr]) + 35.0) < 1e-3);
    CHECK (std::abs (o::ms (s.params[rg::oCompAtt]) - 0.84) < 1e-4 && std::abs (o::ms (s.params[rg::oCompRel]) - 88.9) < 1e-3);
    CHECK (std::abs (s.params[rg::oCompMix] - 0.4f) < 1e-6f && std::abs (o::db (s.params[rg::oCompOut]) - 6.0) < 1e-3);
    int order[5] = {};
    o::orderOf (s.params[rg::omniOrder], order);
    CHECK (order[0] == o::pre && order[1] == o::comp && order[2] == o::ds2 && order[3] == o::eq && order[4] == o::gate);
    // a moved module: the EQ (slot 4) first
    const auto t = translate (state ("413526", mods, bypass));
    o::orderOf (t.params[rg::omniOrder], order);
    CHECK (order[0] == o::eq && order[1] == o::pre && order[2] == o::ds2 && order[3] == o::comp && order[4] == o::gate);
}

TEST (parity190mm_scheps_bypassed_strip_passes_the_signal)
{
    using namespace lynx;
    using namespace parity190mmscheps;
    const std::string mods[5] = { pre(), list ({}, 68), list ({}, 62), list ({ { 1, 1 } }, 71), compModule ({ { 38, 3.5 } }) };
    const bool bypass[5] = { true, true, true, true, true };
    const auto s = translate (state ("153426", mods, bypass));
    auto l = sine (48000, 440, -3, 4800), r = sine (48000, 660, -12, 4800);
    const auto l0 = l, r0 = r;
    render (s, 48000, l, r);
    CHECK (l == l0 && r == r0); // the reference renders a full reset bit for bit (refcap182 D: -220 dB)
}

TEST (parity190mm_scheps_probe_based_behaviours)
{
    using namespace lynx;
    using namespace parity190mmscheps;
    namespace o = rg::omni;
    auto strip = [] { auto s = makeEffectSlot (EffectKind::ridgeStrip); s.params[rg::omniLaw] = 1.0f; s.params[rg::omniIn] = o::dbValue (0); s.params[rg::omniOut] = o::dbValue (0); return s; };
    // compressor: below the threshold the automatic make-up (probe mk1: VCA 16 : 1, -35 dB, 0.84 / 88.9 ms -> +9.91 dB)
    {
        auto s = strip();
        s.params[rg::oCompOn] = 1.0f; s.params[rg::oCompRatio] = o::ratioValue (16); s.params[rg::oCompThr] = o::dbValue (-35);
        s.params[rg::oCompAtt] = o::msValue (0.84); s.params[rg::oCompRel] = o::msValue (88.9); s.params[rg::oCompMix] = 1.0f; s.params[rg::oCompOut] = o::dbValue (0);
        auto l = sine (48000, 1000, -70, 24000), r = l;
        render (s, 48000, l, r);
        CHECK (std::abs (rmsDb (l, 12000) - (-73.01 + 9.91)) < 0.25);
        // well above: the gain falls (probe comp1 st_VCA_16.0_m35: -8.86 dB at -12 dB in)
        auto l2 = sine (48000, 1000, -12, 48000), r2 = l2;
        render (s, 48000, l2, r2);
        CHECK (std::abs (rmsDb (l2, 24000) - (-15.01 - 8.86)) < 1.0);
    }
    // EQ: the Tone band Wide +12 dB at 1 kHz peaks at 1 kHz (probe eq1); Pre high-pass 12 dB / oct at 100 Hz: -3 dB there
    {
        auto s = strip();
        s.params[rg::oEqOn] = 1.0f;
        const int tone = rg::oEqBand0 + 5;
        s.params[(std::size_t) tone] = 1.0f; s.params[(std::size_t) tone + 1] = o::indexValue (0); s.params[(std::size_t) tone + 2] = o::hzValue (1000);
        s.params[(std::size_t) tone + 3] = o::dbValue (12); s.params[(std::size_t) tone + 4] = o::qValue (1);
        auto l = sine (48000, 1000, -30, 24000), r = l;
        render (s, 48000, l, r);
        CHECK (std::abs (rmsDb (l, 12000) - (-33.01 + 12.0)) < 0.05);
        auto h = strip();
        h.params[rg::oPreOn] = 1.0f; h.params[rg::oHpOn] = 1.0f; h.params[rg::oHpSlope] = o::indexValue (1); h.params[rg::oHpHz] = o::hzValue (100); h.params[rg::oHpQ] = o::qValue (1);
        auto l2 = sine (48000, 100, -20, 48000), r2 = l2;
        render (h, 48000, l2, r2);
        CHECK (std::abs (rmsDb (l2, 24000) - (-23.01 - 3.01)) < 0.1);
    }
    // saturation: Even at 30 % adds +2.0 dB at every level (probe pre1 sat_Even_30)
    {
        auto s = strip();
        s.params[rg::oPreOn] = 1.0f; s.params[rg::oSatOn] = 1.0f; s.params[rg::oSatType] = o::indexValue (1); s.params[rg::oSatAmount] = 0.3f;
        auto l = sine (48000, 1000, -30, 24000), r = l;
        render (s, 48000, l, r);
        CHECK (std::abs (rmsDb (l, 12000) - (-33.01 + 2.0)) < 0.1);
    }
    // the order matters: a hard clipper before or after a +12 dB bell gives different results
    {
        auto s = strip();
        s.params[rg::oPreOn] = 1.0f; s.params[rg::oSatOn] = 1.0f; s.params[rg::oSatType] = o::indexValue (2); s.params[rg::oSatAmount] = 1.0f;
        s.params[rg::oEqOn] = 1.0f;
        const int tone = rg::oEqBand0 + 5;
        s.params[(std::size_t) tone] = 1.0f; s.params[(std::size_t) tone + 2] = o::hzValue (1000); s.params[(std::size_t) tone + 3] = o::dbValue (12);
        int a[5] = { o::pre, o::eq, o::comp, o::ds2, o::gate }, b[5] = { o::eq, o::pre, o::comp, o::ds2, o::gate };
        auto t = s;
        s.params[rg::omniOrder] = o::orderValue (a);
        t.params[rg::omniOrder] = o::orderValue (b);
        auto l1 = sine (48000, 1000, -6, 9600), r1 = l1, l2 = l1, r2 = l1;
        render (s, 48000, l1, r1);
        render (t, 48000, l2, r2);
        CHECK (std::abs (rmsDb (l1, 4800) - rmsDb (l2, 4800)) > 3.0);
    }
}
