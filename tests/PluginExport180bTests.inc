// --- Round 180b: LYNX's own plugins as VST3 / CLAP ---------------------------------------------------------------------
// The plugin runs the app's renderer on a one-slot song: its input enters the insert through the metronome input, and
// the host's parameters reach the slot without a rebuild. That path must sound like the effect run alone.

#include "core/NativeEngines.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "plugin/ExportCatalog.h"

namespace
{
Song pluginExportSong (EffectKind kind)
{
    auto song = Song::makeDefault();
    song.channels.clear();
    song.mixer.resize (2);
    song.mixer[0].effects.clear();
    auto slot = makeEffectSlot (kind);
    slot.smartDisable = false;
    song.mixer[1].effects = { slot };
    return song;
}

void pluginExportInput (std::vector<float>& l, std::vector<float>& r, int block)
{
    for (std::size_t i = 0; i < l.size(); ++i)
    {
        l[i] = 0.3f * std::sin (0.031f * (float) (block * (int) l.size() + (int) i));
        r[i] = 0.5f * l[i];
    }
}
} // namespace

TEST (pluginexport180b_rack_path_equals_the_effect_alone)
{
    constexpr double rate = 48000.0;
    constexpr int size = 256;
    for (const auto kind : { EffectKind::facetEq, EffectKind::crestCompressor, EffectKind::echoform })
    {
        const auto song = pluginExportSong (kind);
        auto state = RackState::build (song, nullptr, rate);
        auto renderer = std::make_unique<RackRenderer>();
        renderer->prepare (rate, size);
        auto direct = makeEffect (kind);
        direct->prepare (rate);
        const auto params = song.mixer[1].effects[0].params;

        std::vector<float> inL (size), inR (size), outL (size), outR (size), l (size), r (size);
        auto worst = 0.0f;
        for (int b = 0; b < 100; ++b)
        {
            pluginExportInput (inL, inR, b);
            l = inL;
            r = inR;
            direct->setSongPosition (0.0);
            direct->setPlaying (false);
            direct->process (l.data(), r.data(), size, params, 120.0);
            renderer->setMetronomeInput (inL.data(), inR.data(), 1);
            renderer->setSlotParamsOverride (&params, 1, 0);
            renderer->process (state.get(), false, 0.0, size, outL.data(), outR.data());
            for (int i = 0; i < size; ++i)
                worst = std::max ({ worst, std::abs (outL[(std::size_t) i] - l[(std::size_t) i]), std::abs (outR[(std::size_t) i] - r[(std::size_t) i]) });
        }
        CHECK (worst < 1.0e-5f);
    }
}

TEST (pluginexport180b_host_parameters_reach_the_slot_without_a_rebuild)
{
    constexpr double rate = 48000.0;
    constexpr int size = 256;
    const auto song = pluginExportSong (EffectKind::utility);
    auto state = RackState::build (song, nullptr, rate);
    auto renderer = std::make_unique<RackRenderer>();
    renderer->prepare (rate, size);
    auto params = song.mixer[1].effects[0].params;
    const auto& info = effectInfo (EffectKind::utility);
    CHECK (! info.params.empty());
    std::vector<float> inL (size), inR (size), outL (size), outR (size);
    const auto energy = [&] {
        auto sum = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            pluginExportInput (inL, inR, b);
            renderer->setMetronomeInput (inL.data(), inR.data(), 1);
            renderer->setSlotParamsOverride (&params, 1, 0);
            renderer->process (state.get(), false, 0.0, size, outL.data(), outR.data());
        }
        for (auto v : outL)
            sum += v * v;
        return sum;
    };
    const auto before = energy();
    params[0] = params[0] < 0.5f ? 1.0f : 0.0f;
    CHECK (energy() != before);
}

TEST (pluginexport180b_catalog_needs_no_fl_data)
{
    // every exported engine starts from LYNX's own state, on a PC without FL Studio
    for (const auto& e : plugexport::catalog)
    {
        if (e.instrument)
        {
            const auto* engine = natives::instrumentEngine (e.synth);
            CHECK (engine != nullptr && engine->needs == natives::Needs::nothing);
            const auto channel = banks::defaultChannel (e.synth, e.name);
            CHECK (channel.has_value());
            CHECK (modelparams::tableOf (e.synth) != nullptr);
        }
        else
        {
            const auto* engine = natives::effectEngine (e.effect);
            CHECK (engine == nullptr || engine->needs == natives::Needs::nothing);
        }
    }
}
