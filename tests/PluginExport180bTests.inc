// --- Round 180b: LYNX's own plugins as VST3 / CLAP ---------------------------------------------------------------------
// The plugin runs the app's renderer on a one-slot song: its input enters the insert through the metronome input, and
// the host's parameters reach the slot without a rebuild. That path must sound like the effect run alone.

#include <regex>

#include "core/NativeEngines.h"
#include "core/LynxBanks.h"
#include "core/ModelParams.h"
#include "plugin/ExportCatalog.h"

namespace
{
lynx::Song pluginExportSong (lynx::EffectKind kind)
{
    auto song = lynx::Song::makeDefault();
    song.channels.clear();
    song.mixer.resize (2);
    song.mixer[0].effects.clear();
    auto slot = lynx::makeEffectSlot (kind);
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
    for (const auto kind : { lynx::EffectKind::facetEq, lynx::EffectKind::crestCompressor, lynx::EffectKind::echoform })
    {
        const auto song = pluginExportSong (kind);
        auto state = lynx::RackState::build (song, nullptr, rate);
        auto renderer = std::make_unique<lynx::RackRenderer>();
        renderer->prepare (rate, size);
        auto direct = lynx::makeEffect (kind);
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
    const auto song = pluginExportSong (lynx::EffectKind::utility);
    auto state = lynx::RackState::build (song, nullptr, rate);
    auto renderer = std::make_unique<lynx::RackRenderer>();
    renderer->prepare (rate, size);
    auto params = song.mixer[1].effects[0].params;
    const auto& info = lynx::effectInfo (lynx::EffectKind::utility);
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
    for (const auto& e : lynx::plugexport::catalog)
    {
        if (e.instrument)
        {
            const auto* engine = lynx::natives::instrumentEngine (e.synth);
            CHECK (engine != nullptr && engine->needs == lynx::natives::Needs::nothing);
            const auto channel = lynx::banks::defaultChannel (e.synth, e.name);
            CHECK (channel.has_value());
            CHECK (lynx::modelparams::tableOf (e.synth) != nullptr);
        }
        else
        {
            const auto* engine = lynx::natives::effectEngine (e.effect);
            CHECK (engine == nullptr || engine->needs == lynx::natives::Needs::nothing);
        }
    }
}

TEST (pluginexport180b_static_image_caches_are_registered)
{
    // A static juce::Image (or a cache of them) must register in ui/ImageCaches.h, so a plugin empties it before the host
    // unloads the DLL: freed at unload, its Direct2D device deadlocked FL Studio's exit (round 180b, dump of 09:35).
    // A lint over src/ (the tests run from the repository root): the declaration, then registerImageCache within 2 lines.
    namespace fs = std::filesystem;
    CHECK (fs::is_directory ("src/ui"));
    if (! fs::is_directory ("src/ui"))
        return;
    const std::regex declaration (R"(^\s*(?:inline\s+|thread_local\s+)*static\s+(?:inline\s+|thread_local\s+)*(?:const\s+)?[^;=(]*(?:juce::Image\b|juce::Drawable\b|SpriteCache\b)[^;=(]*(?:;|=|\{))");
    int found = 0;
    for (const auto& entry : fs::recursive_directory_iterator ("src"))
    {
        const auto ext = entry.path().extension().string();
        if (! entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".inc" && ext != ".mm"))
            continue;
        std::ifstream in (entry.path());
        std::vector<std::string> lines;
        for (std::string line; std::getline (in, line);)
            lines.push_back (line);
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (std::regex_search (lines[i], declaration))
            {
                ++found;
                bool registered = false;
                for (std::size_t k = i; k < std::min (lines.size(), i + 3); ++k)
                    registered = registered || lines[k].find ("registerImageCache") != std::string::npos;
                if (! registered)
                    std::printf ("  unregistered static image: %s:%zu\n", entry.path().string().c_str(), i + 1);
                CHECK (registered);
            }
    }
    CHECK (found >= 8); // (the lint still sees the caches it was written for)
}
