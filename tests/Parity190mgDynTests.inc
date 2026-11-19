// Round 190mg (sub-round dyn, finished by the coordinator): the 190lf leftovers measured on our Waves demo-mode probes
// (docs/analise/REA_rodada190mg_dyn.md). Every law is hidden and 0 for new instances and every project saved before
// (bit for bit: waves3_190mg/dyn/lab/gold.sh); only the Waves translators set them.
// - Facet EQ [125] the PuigTec law: the output stage's waveshaper, the resampler's fractional delay, the Mains hum.
// - Limiter [6 … 8] L2's IDR: word length, TPDF dither, noise shaping on the L2 law's output.
// - Crest's fetAmp (CLA-76's output amplifier) is modelled but not set by the translator (it cost two files a step).
#include <cmath>
#include <vector>

#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/LimiterLaw.h"

TEST (parity190mg_dyn_laws_are_hidden_and_off_for_new_instances)
{
    using namespace lynx;
    auto facetSlot = makeEffectSlot (EffectKind::facetEq);
    CHECK (facetSlot.params[(std::size_t) facet::puigLawParam] == 0.0f && facet::puigLaw (facetSlot.params[(std::size_t) facet::puigLawParam]) == 0);
    const auto saved = savedEffectParamCount (facetSlot);
    facetSlot.params[(std::size_t) facet::puigLawParam] = facet::puigLawValue (1);
    CHECK (savedEffectParamCount (facetSlot) > saved && facet::puigLaw (facetSlot.params[(std::size_t) facet::puigLawParam]) == 1);
    auto lim = makeEffectSlot (EffectKind::limiter);
    for (const int k : { limiterlaw::ditherBitsParam, limiterlaw::ditherTypeParam, limiterlaw::ditherShapeParam })
        CHECK (lim.params[(std::size_t) k] == 0.0f);
    // the waveshaper is unity at low level (its harmonics are -80 dB and less there)
    CHECK (std::abs (facet::puigShape (1.0e-4) / 1.0e-4 - 1.0) < 1.0e-2);
}

TEST (parity190mg_dyn_puigtec_and_l2_translate_their_new_laws)
{
    using namespace lynx;
    namespace ll = limiterlaw;
    int n = 0;
    // PuigTec with Mains off (16 = 0): the stage only; Mains 1 = 60 Hz
    auto list = [] (const char* mains) {
        return parity190jh::list (20, { { 1, "20" }, { 5, "1" }, { 8, "1" }, { 10, "3" }, { 11, "4" }, { 13, "1" }, { 15, "1" }, { 16, mains } });
    };
    auto off = parity190jh::waves ("PuigTec EQP1A Stereo", "PuigTec EQP1A", list ("0"), n);
    CHECK (off.kind == EffectKind::facetEq && facet::puigLaw (off.params[(std::size_t) facet::puigLawParam]) == 1);
    auto hum = parity190jh::waves ("PuigTec EQP1A Stereo", "PuigTec EQP1A", list ("1"), n);
    CHECK (facet::puigLaw (hum.params[(std::size_t) facet::puigLawParam]) == 2);
    // L2 with IDR: Quantize 4 (16 bits), Type 1 (TPDF), Normal shaping
    auto l2 = parity190jh::waves ("L2 Stereo", "L2", "1 -6 -0.3 * * * * * 4 0\n0 1 2 1 0 0.1", n);
    CHECK (l2.kind == EffectKind::limiter && ll::isL2 (l2.params[(std::size_t) ll::lawParam]));
    CHECK (std::lround (l2.params[(std::size_t) ll::ditherBitsParam] * 32.0f) == 16);
    CHECK (std::lround (l2.params[(std::size_t) ll::ditherTypeParam] * 4.0f) == 2);
}
