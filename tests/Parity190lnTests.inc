// Round 190ln: REA on third-party plug-in binaries for parity (owner's permission of 2026-11-17), clean-room
// (docs/analise/REA_rodada190ln.md; lab /mnt/e/lynx-work/rea3_190ln). Translators only: no LYNX effect's sound changed.
// - Eiosis AirEQ / Slate VM AirEQ Air / Earth -> Facet EQ in physical units (was Facet flat).
// - Slate Infinity EQ -> Facet EQ in physical units (was Facet flat).
#include <cmath>
#include <string>

#include "core/FacetEq.h"
#include "core/PluginSubstitutes.h"

namespace parity190ln
{
// an XML archive as the references save it: <entry id=… type="float64" value=…/> (first entry of an id wins)
inline std::vector<std::uint8_t> archive (const char* client, std::initializer_list<std::pair<const char*, double>> entries)
{
    std::string t = std::string ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package version=\"1.0\"><manifest><client id=\"") + client
                    + "\"/></manifest><archives><archive client_id=\"" + client + "\" version=\"1\"><section id=\"Common\">";
    for (const auto& [id, v] : entries)
    {
        char buf[64];
        std::snprintf (buf, sizeof buf, "%.17e", v);
        t += std::string ("<entry id=\"") + id + "\" type=\"float64\" value=\"" + buf + "\"/>";
    }
    t += "</section></archive></archives></package>";
    return { t.begin(), t.end() };
}
inline lynx::EffectSlot load (const char* name, const std::vector<std::uint8_t>& state, int& translated)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = name;
    id.state = state;
    const auto* e = lynx::findPluginSubstitute (id, false);
    if (e == nullptr)
        return {};
    return lynx::makeSubstituteSlot (*e, &id, &translated);
}
inline double band (const lynx::EffectSlot& s, int b, int field) { return s.params[(std::size_t) lynx::facet::param (b, field)]; }
} // namespace parity190ln

TEST (parity190ln_aireq_translates_cuts_shelves_and_bells_onto_facet)
{
    using namespace lynx;
    int n = 0;
    // refcap182 S4: band 1 bell 250 Hz −4 dB Q 2, band 2 bell 4 kHz +3 dB Q 0.5 (+ the low cut on, 100 Hz steep 24 dB/oct)
    const auto st = parity190ln::archive ("AEqP-state", { { "Band1On", 0.75 }, { "Band1Frequency", 0.25 + 0.125 * (10.0 / 290.0) }, { "Band1Gain", 0.5 - 4.0 / 36.0 },
                                                          { "Band1Q", 0.625 + 0.125 * 0.45 / 0.95 }, { "Band2On", 0.75 }, { "Band2Frequency", 0.625 + 0.125 * (1250.0 / 4750.0) },
                                                          { "Band2Gain", 0.5 + 3.0 / 36.0 }, { "Band2Q", 0.375 - 0.125 * 0.15 / 0.22 }, { "HighPassOn", 0.75 },
                                                          { "HighPassFrequency", 0.125 + 0.125 * 70.0 / 75.0 }, { "HighPassType", 0.75 }, { "HighPassSteepSlope", 0.75 },
                                                          { "InputGain", 0.5 }, { "OutputGain", 0.5 }, { "Strength", 0.5 } });
    const auto s = parity190ln::load ("Eiosis AirEQ", st, n);
    CHECK (s.kind == EffectKind::facetEq && n >= 3);
    // the low cut first: 100 Hz, 24 dB/oct, Butterworth (Facet Q 1)
    CHECK (facet::shape ((float) parity190ln::band (s, 0, 0)) == facet::Shape::lowCut);
    CHECK (std::abs (facet::hz ((float) parity190ln::band (s, 0, 1)) - 100.0) < 1.0);
    CHECK (facet::slopeDb ((float) parity190ln::band (s, 0, 4)) == 24);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s, 0, 3)) - 1.0) < 0.02);
    // the bells: the reference's peak is larger than the dial and grows with its Q (fitted: −8.7 dB, Facet Q 1.71 / +4.4, 0.55)
    CHECK (facet::shape ((float) parity190ln::band (s, 1, 0)) == facet::Shape::bell);
    CHECK (std::abs (facet::hz ((float) parity190ln::band (s, 1, 1)) - 250.0) < 2.0);
    CHECK (std::abs (facet::gainDb ((float) parity190ln::band (s, 1, 2)) + 8.6) < 0.3);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s, 1, 3)) - 1.71) < 0.1);
    CHECK (std::abs (facet::gainDb ((float) parity190ln::band (s, 2, 2)) - 4.4) < 0.3);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s, 2, 3)) - 0.55) < 0.05);
    CHECK (facet::shape ((float) parity190ln::band (s, 3, 0)) == facet::Shape::off);
}

TEST (parity190ln_aireq_shelf_air_earth_and_vm_modules)
{
    using namespace lynx;
    int n = 0;
    // S3: band 1 as a shelf, 8 kHz +4 dB -> Facet low shelf, the gain as dialled, Q 2.07 (the cookbook shelf of slope 1)
    const auto s3 = parity190ln::load ("Eiosis AirEQ", parity190ln::archive ("AEqP-state", { { "Band1On", 0.75 }, { "Band1Type", 0.75 }, { "Band1Frequency", 0.75 + 0.125 * 500.0 / 8500.0 },
                                                                                            { "Band1Gain", 0.5 + 4.0 / 36.0 }, { "Band1Q", 0.5 }, { "InputGain", 0.5 } }), n);
    CHECK (facet::shape ((float) parity190ln::band (s3, 0, 0)) == facet::Shape::lowShelf);
    CHECK (std::abs (facet::gainDb ((float) parity190ln::band (s3, 0, 2)) - 4.0) < 0.05);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s3, 0, 3)) - 2.09) < 0.05);
    // VM AirEQ Air: its own ids (AirGain, HiCutOn / Freq / Slope, StandaloneInputGain); Air +9 dB -> a high shelf
    const auto air = parity190ln::load ("VM AirEQ Air", parity190ln::archive ("AEqA-state", { { "AirOn", 0.75 }, { "AirGain", 0.75 }, { "HiCutOn", 0.75 }, { "HiCutFreq", 0.875 },
                                                                                             { "HiCutSlope", 0.3 }, { "StandaloneInputGain", 0.5 }, { "StandaloneOutputGain", 0.625 } }), n);
    CHECK (air.kind == EffectKind::facetEq);
    CHECK (facet::shape ((float) parity190ln::band (air, 0, 0)) == facet::Shape::highShelf);
    CHECK (facet::gainDb ((float) parity190ln::band (air, 0, 2)) > 20.0);
    CHECK (facet::shape ((float) parity190ln::band (air, 1, 0)) == facet::Shape::highCut);
    CHECK (std::abs (facet::hz ((float) parity190ln::band (air, 1, 1)) - 16000.0) < 50.0);
    CHECK (facet::slopeDb ((float) parity190ln::band (air, 1, 4)) == 12);
    CHECK (std::abs (facet::outputDb (air.params[(std::size_t) facet::outputParam]) - 6.0) < 0.05); // output +6 of ±24
    // no readable state: Facet flat, as before
    lynx::PluginIdentity id;
    id.name = "Eiosis AirEQ";
    const auto* e = findPluginSubstitute (id, false);
    CHECK (e != nullptr && e->effect == EffectKind::facetEq);
    int t = -1;
    const auto flat = makeSubstituteSlot (*e, &id, &t);
    CHECK (t == 0 && facet::shape ((float) parity190ln::band (flat, 0, 0)) == facet::Shape::off);
}

TEST (parity190ln_infinity_eq_translates_its_bands)
{
    using namespace lynx;
    int n = 0;
    // refcap182 X2: low shelf 70 Hz +5, bell 400 Hz −5 (width 26 st), high shelf 8 kHz +4; a disabled band in between
    const auto s = parity190ln::load ("Inf EQ", parity190ln::archive ("InfE-state", { { "Band1State", 0.5 }, { "Band1Type", 1.5 / 12.0 }, { "Band1Frequency", 0.24306 },
                                                                                     { "Band1Gain", 0.569444 }, { "Band1Q1", 0.348672 }, { "Band2State", 1.0 / 6.0 },
                                                                                     { "Band3State", 0.5 }, { "Band3Type", 2.5 / 12.0 }, { "Band3Frequency", 0.460765 },
                                                                                     { "Band3Gain", 0.430556 }, { "Band3Width", 0.5 }, { "Band4State", 0.5 },
                                                                                     { "Band4Type", 3.5 / 12.0 }, { "Band4Frequency", 0.835161 }, { "Band4Gain", 0.555556 },
                                                                                     { "Gain", 0.5 } }), n);
    CHECK (s.kind == EffectKind::facetEq && n == 3);
    CHECK (facet::shape ((float) parity190ln::band (s, 0, 0)) == facet::Shape::lowShelf);
    CHECK (std::abs (facet::hz ((float) parity190ln::band (s, 0, 1)) - 70.0) < 1.0);
    CHECK (std::abs (facet::gainDb ((float) parity190ln::band (s, 0, 2)) - 5.0) < 0.01);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s, 0, 3)) - 1.0) < 0.03);
    CHECK (facet::shape ((float) parity190ln::band (s, 1, 0)) == facet::Shape::bell);
    CHECK (std::abs (facet::gainDb ((float) parity190ln::band (s, 1, 2)) + 5.0) < 0.01);
    CHECK (std::abs (facet::q ((float) parity190ln::band (s, 1, 3)) - 0.86) < 0.02); // 26 semitones = the cookbook bandwidth
    CHECK (facet::shape ((float) parity190ln::band (s, 2, 0)) == facet::Shape::highShelf);
    CHECK (std::abs (facet::hz ((float) parity190ln::band (s, 2, 1)) - 8000.0) < 50.0);
    CHECK (facet::shape ((float) parity190ln::band (s, 3, 0)) == facet::Shape::off);
}
