// Round 190ln (Kilohearts sub-round): the kHs snapins / Faturator / Disperser states translated onto LYNX stand-ins
// (core/KiloheartsStates.inc, docs/PLUGIN_FALLBACK.md "Kilohearts", docs/analise/REA_rodada190ln.md).
// The states here are built the way the plug-ins save them: u32 1, u32 size, a ZIP holding state.json whose "model"
// keeps every setting in the plug-in's own units as strings.
#include <cmath>
#include <complex>
#include <cstring>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/PluginSubstitutes.h"
#include "core/ZipLite.h"

#include "core/KiloheartsDsp.h" // round 190nh: the Trance Gate test reads the law

namespace parity190lnkhs
{
inline std::vector<std::uint8_t> state (const std::string& modelFields)
{
    const std::string json = "{\"meta\": {\"author\": \"t\"}, \"version\": {\"format\": \"1\", \"plugin\": \"1052\", \"sdk\": \"24\"}, "
                             "\"preset\": {\"path\": \"\", \"name\": \"\", \"edited\": \"true\"}, \"model\": {\"defaults\": {\"enabled\": \"true\"}, "
                             "\"globals\": {\"numModulations\": \"0\", \"modulations\": [{\"target\": {}, \"amount\": \"0\"}]}, "
                             + modelFields + "}}";
    std::vector<lynx::zip::NewEntry> entries { { "state.json", std::vector<std::uint8_t> (json.begin(), json.end()) } };
    const auto archive = lynx::zip::write (entries);
    std::vector<std::uint8_t> out (8, 0);
    const std::uint32_t one = 1, size = (std::uint32_t) archive.size();
    std::memcpy (out.data(), &one, 4);
    std::memcpy (out.data() + 4, &size, 4);
    out.insert (out.end(), archive.begin(), archive.end());
    return out;
}
inline lynx::EffectSlot load (const char* plugin, const std::vector<std::uint8_t>& st, int& translated)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = plugin;
    id.state = st;
    translated = -1;
    const auto* entry = lynx::findPluginSubstitute (id, false);
    if (entry == nullptr)
        return lynx::makeEffectSlot (lynx::EffectKind::eq);
    return lynx::makeSubstituteSlot (*entry, &id, &translated);
}
inline std::string text (const lynx::EffectSlot& s, int p) { return lynx::formatEffectParam (s.kind, p, s.params[(std::size_t) p], 120.0, s.params); }
inline int named (const lynx::EffectSlot& s, const char* name)
{
    const auto& info = lynx::effectInfo (s.kind);
    for (std::size_t i = 0; i < info.params.size(); ++i)
        if (std::strcmp (info.params[i].name, name) == 0)
            return (int) i;
    return -1;
}
} // namespace parity190lnkhs

TEST (parity190lnkhs_states_are_read_and_mapped_in_the_plugins_units)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    // kHs Gain: the stored gain is linear (5.623 = +15 dB)
    auto g = load ("kHs Gain", state ("\"mode\": \"decibel\", \"gain\": \"5.623414\", \"$type\": \"gain\""), n);
    CHECK (g.kind == EffectKind::utility && n == 1);
    CHECK (text (g, 0) == "+15.0 dB");
    // kHs Compressor: threshold linear (0.5011872 = -6 dB), ratio, times in seconds
    auto c = load ("kHs Compressor", state ("\"attack\": \"0.01\", \"release\": \"0.1\", \"makeup\": \"0\", \"mode\": \"peak\", \"ratio\": \"4\", "
                                            "\"threshold\": \"0.5011872\", \"$type\": \"compressor\""), n);
    CHECK (c.kind == EffectKind::compressor);
    CHECK (text (c, 0) == "-6.0 dB" && text (c, 1) == "4.0:1");
    CHECK (text (c, 2) == "10.0 ms" && text (c, 3) == "100 ms");
    // kHs Limiter: post · limit_thr (pre · x) = limit_(thr · post) (pre · post · x)
    auto l = load ("kHs Limiter", state ("\"pre_gain\": \"3.1622777\", \"post_gain\": \"0.70794576\", \"threshold\": \"0.5011872\", "
                                         "\"release\": \"0.1\", \"$type\": \"limiter\""), n);
    CHECK (l.kind == EffectKind::limiter && text (l, 0) == "-9.0 dB" && text (l, 1) == "+7.0 dB" && text (l, 2) == "100 ms");
    // kHs Haas: the right channel delayed 5 ms (its default) → Stereo Shaper's "R 5.0 ms"
    auto h = load ("kHs Haas", state ("\"channel\": \"true\", \"delay\": \"0.005\", \"$type\": \"haas\""), n);
    CHECK (h.kind == EffectKind::stereoShaper && text (h, 4) == "R 5.0 ms");
    auto hl = load ("kHs Haas", state ("\"channel\": \"false\", \"delay\": \"0.02\", \"$type\": \"haas\""), n);
    CHECK (text (hl, 4) == "L 20.0 ms");
    // kHs Filter: a 24 dB/oct low pass (slope ×2) at 1 kHz, Q 0.707 = Facet's Butterworth (Q 1)
    auto f = load ("kHs Filter", state ("\"type\": \"low_pass\", \"slope\": \"2\", \"cutoff\": \"1000\", \"q\": \"0.707\", \"gain\": \"0\", \"$type\": \"filter\""), n);
    CHECK (f.kind == EffectKind::facetEq);
    CHECK (facet::shape (f.params[0]) == facet::Shape::highCut && std::abs (facet::hz (f.params[1]) - 1000.0) < 1.0);
    CHECK (facet::slopeDb (f.params[4]) == 24 && std::abs (facet::q (f.params[3]) - 1.0) < 0.01);
}

TEST (parity190lnkhs_delay_time_and_synced_notes)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    auto d = load ("kHs Delay", state ("\"pan\": \"0\", \"delay_numerator\": \"3\", \"delay_time\": \"0.25\", \"delay_denominator\": \"denominator_16\", "
                                       "\"sync_mode\": \"ms\", \"ping_pong\": \"true\", \"duck\": \"0\", \"feedback\": \"0.4\", \"mix\": \"0.3\", "
                                       "\"tone\": \"0\", \"$type\": \"delay\""), n);
    CHECK (d.kind == EffectKind::echoform);
    CHECK (text (d, named (d, "Time L")) == "250.0 ms" && text (d, named (d, "Style")) == "Ping-pong");
    CHECK (text (d, named (d, "Feedback")) == "40%" && text (d, named (d, "Mode")) == "Clean");
    // (round 190nb: the measured linear mix — dry 1 − m through Echoform's equal-power Mix: cos (Mix × 90°) = 0.7)
    CHECK (std::abs (std::cos (0.5 * M_PI * d.params[(std::size_t) named (d, "Mix")]) - 0.7) < 1e-5);
    // synced 3/16 = a dotted eighth: Echoform's dotted 1/8
    auto s = load ("kHs Delay", state ("\"delay_numerator\": \"3\", \"delay_time\": \"0.2\", \"delay_denominator\": \"denominator_16\", "
                                       "\"sync_mode\": \"sync\", \"feedback\": \"0\", \"mix\": \"0.5\", \"$type\": \"delay\""), n);
    CHECK (text (s, named (s, "Sync L")) == "dotted" && text (s, named (s, "Note L")) == "1/8");
    // 5/16 has no Echoform note: its time at 120 BPM (625 ms)
    auto u = load ("kHs Delay", state ("\"delay_numerator\": \"5\", \"delay_time\": \"0.2\", \"delay_denominator\": \"denominator_16\", "
                                       "\"sync_mode\": \"sync\", \"feedback\": \"0\", \"mix\": \"0.5\", \"$type\": \"delay\""), n);
    CHECK (text (u, named (u, "Sync L")) == "ms" && text (u, named (u, "Time L")) == "625.0 ms");
}

TEST (parity190lnkhs_trance_gate_pattern_on_volume_gate) // (round 190nh: on Effector's Trance Gate law)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    std::string steps;
    for (int i = 0; i < 16; ++i)
        steps += std::string (i ? ", " : "") + "{\"on\": \"" + (i % 2 == 0 ? "true" : "false") + "\", \"tie\": \"false\"}";
    int n = 0;
    auto t = load ("kHs Trance Gate", state ("\"attack\": \"0.005\", \"decay\": \"0.005\", \"sustain\": \"1\", \"depth\": \"1\", \"release\": \"0.02\", "
                                             "\"rate\": \"one_16th\", \"swing\": \"0.5\", \"pattern\": \"1\", \"$type\": \"trance_gate\", "
                                             "\"patterns\": [{\"length\": \"16\", \"steps\": [" + steps + "]}]"), n);
    // round 190nh: the gate opens on Effector's hidden Trance Gate law (tests/Parity190nhKhsTests.inc), its steps as read
    CHECK (t.kind == EffectKind::effector && n > 16);
    namespace k = khsdsp;
    CHECK (k::lawOf (t.params[(std::size_t) k::lawParam]) == k::Law::tranceGate);
    CHECK (t.params[(std::size_t) k::gateStepParam] > 0.3f && t.params[(std::size_t) k::gateStepParam + 1] == 0.0f);
}

TEST (parity190lnkhs_disperser_is_an_all_pass_cascade_on_facet_and_off_by_default)
{
    using namespace lynx;
    using namespace parity190lnkhs;
    int n = 0;
    auto d = load ("Disperser", state ("\"order\": \"16\", \"cutoff\": \"200\", \"q\": \"0.5\", \"$type\": \"disperser\""), n);
    CHECK (d.kind == EffectKind::facetEq && n == 3);
    CHECK (facet::dispersionSections (d.params[(std::size_t) facet::dispersionCountParam]) == 16);
    CHECK (std::abs (facet::hz (d.params[(std::size_t) facet::dispersionFreqParam]) - 200.0) < 0.5);
    // a new Facet (and every project saved before) has the cascade off
    CHECK (facet::dispersionSections (makeEffectSlot (EffectKind::facetEq).params[(std::size_t) facet::dispersionCountParam]) == 0);
    // all-pass: an impulse keeps its energy; 16 sections at 200 Hz (Q 0.5) delay the lows by 16 × 2 / (2π · 200) s (analog
    // group delay at DC of a Q-0.5 section = 1 / (π f0) … here measured as the energy centroid of the response)
    const double sr = 48000.0;
    std::vector<float> l (96000, 0.0f), r (96000, 0.0f);
    l[100] = r[100] = 1.0f;
    auto fx = makeEffect (d);
    fx->prepare (sr);
    for (int off = 0; off < (int) l.size(); off += 256)
        fx->process (l.data() + off, r.data() + off, 256, d.params, 120.0);
    double e = 0.0;
    for (float v : l) e += (double) v * v;
    CHECK (std::abs (e - 1.0) < 1e-3);
    // the group delay at 50 Hz from the response's phase at 49 / 51 Hz: 32 first-order all-passes at 200 Hz give
    // 32 · 2a / (a² + ω²) ≈ 48 ms (a = 2π · 200, ω = 2π · 50); the gain there stays 0 dB
    auto bin = [&] (double f) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 100; i < l.size(); ++i)
        {
            const double ph = -2.0 * M_PI * f * (double) (i - 100) / sr;
            re += l[i] * std::cos (ph);
            im += l[i] * std::sin (ph);
        }
        return std::complex<double> (re, im);
    };
    const auto b49 = bin (49.0), b51 = bin (51.0);
    double dphi = std::arg (b51 / b49);
    const double gd = -dphi / (2.0 * M_PI * 2.0);
    CHECK (gd > 0.040 && gd < 0.056);
    CHECK (std::abs (std::abs (bin (50.0)) - 1.0) < 0.01);
    // a state of another plug-in type, or no ZIP: the entry's defaults (nothing translated)
    auto bad = load ("Disperser", std::vector<std::uint8_t> (40, 7), n);
    CHECK (bad.kind == EffectKind::facetEq && n == 0 && facet::dispersionSections (bad.params[(std::size_t) facet::dispersionCountParam]) == 0);
}
