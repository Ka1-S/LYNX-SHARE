// Round 177d, FL pain points (research-web-2.md §3; docs/analise/REA_rodada177d.md §2).
// Slice 177d-3: 6b gain-reduction meter (6a EQ bypass is UI + project file).
#include "core/MixerOps.h"

TEST (pain177d_gain_reduction_meter_only_for_lynx_dynamics)
{
    using lynx::EffectKind;
    for (auto k : { EffectKind::compressor, EffectKind::apexLimiter, EffectKind::crestCompressor, EffectKind::sentryGate,
                    EffectKind::quellDenoiser, EffectKind::ridgeStrip, EffectKind::glowLeveler, EffectKind::hushDeEsser })
        CHECK (lynx::mixerops::reportsGainReduction (k));
    for (auto k : { EffectKind::eq, EffectKind::reverb, EffectKind::delay, EffectKind::plugin, EffectKind::fruityCompressor })
        CHECK (! lynx::mixerops::reportsGainReduction (k));
}
