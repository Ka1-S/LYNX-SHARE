// --- Round 180b: LYNX's own plugins as VST3 / CLAP ---------------------------------------------------------------------
// The plugin runs the app's renderer on a one-slot song: its input enters the insert through the metronome input, and
// the host's parameters reach the slot without a rebuild. That path must sound like the effect run alone.

#include <regex>

#include "core/NativeEngines.h"
#include "core/UpdateCheck.h"
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
    slot.sidechain = 2; // round 188b: as the plugin's song (plugin/ExportProcessor.cpp): the host's side-chain bus track
    song.mixer[1].effects = { slot };
    song.mixer.resize (3);
    song.mixer[2].effects.clear();
    song.mixer[2].toMaster = false;
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
    // round 188b: every exported effect (the catalog), plus the generic utility
    std::vector<lynx::EffectKind> kinds { lynx::EffectKind::utility };
    for (const auto& e : lynx::plugexport::catalog)
        if (! e.instrument)
            kinds.push_back (e.effect);
    for (const auto kind : kinds)
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

// --- Round 188b: every LYNX effect in the CLAP, a short list as VST3, a separate optional download -----------------------

TEST (pluginexport188b_catalog_identities_are_unique_and_lynx_named)
{
    std::set<std::string> names, ids;
    std::set<lynx::EffectKind> effects;
    auto instruments = 0;
    for (const auto& e : lynx::plugexport::catalog)
    {
        const std::string name = e.name, id = e.clapId;
        CHECK (names.insert (name).second);
        CHECK (ids.insert (id).second);
        CHECK (name.rfind ("LYNX ", 0) == 0);
        CHECK (id.rfind ("com.ka1-s.lynx.", 0) == 0);
        // LYNX names only (the user, 2026-11-08): never an FL Studio name
        for (const char* banned : { "Fruity", "FL ", "Image-Line", "Gross Beat", "Maximus", "Sytrus", "Harmor" })
            CHECK (name.find (banned) == std::string::npos);
        if (e.instrument)
            ++instruments;
        else
            CHECK (effects.insert (e.effect).second);
    }
    // every LYNX-named effect is in the CLAP (option C)
    for (const auto kind : { lynx::EffectKind::facetEq, lynx::EffectKind::apexLimiter, lynx::EffectKind::ridgeStrip, lynx::EffectKind::glowLeveler,
                             lynx::EffectKind::hushDeEsser, lynx::EffectKind::aether, lynx::EffectKind::ember, lynx::EffectKind::lynxTune,
                             lynx::EffectKind::epochVerb, lynx::EffectKind::atrium, lynx::EffectKind::foilPlate, lynx::EffectKind::nebula,
                             lynx::EffectKind::halo, lynx::EffectKind::horizon, lynx::EffectKind::echoform, lynx::EffectKind::spiralEcho,
                             lynx::EffectKind::orbit, lynx::EffectKind::latticeMod, lynx::EffectKind::crestCompressor,
                             lynx::EffectKind::prismMultiband, lynx::EffectKind::sentryGate, lynx::EffectKind::quellDenoiser,
                             lynx::EffectKind::geminiDoubler, lynx::EffectKind::voiceShift, lynx::EffectKind::kilnChannel,
                             lynx::EffectKind::fretChannel, lynx::EffectKind::shellChannel })
        CHECK (effects.count (kind) == 1);
    CHECK (instruments >= 1);
}

TEST (pluginexport188b_table_codes_are_unique)
{
    // the VST3 codes (hosts store them in their projects) and the built column, read from the table as the build reads it
    std::ifstream in ("src/plugin/plugins.def");
    CHECK (in.good());
    const std::regex line (R"re(^LYNX_PLUGIN \("([^"]+)", ([01]), [^,]+, [^,]+, "[^"]+", (0x[0-9a-f]+), "([^"]+)", ([012])\)$)re");
    std::set<std::string> codes;
    auto rows = 0, vst3 = 0, built = 0;
    for (std::string text; std::getline (in, text);)
    {
        if (text.rfind ("LYNX_PLUGIN ", 0) != 0)
            continue;
        std::smatch m;
        CHECK (std::regex_match (text, m, line));
        if (m.empty())
            continue;
        ++rows;
        CHECK (codes.insert (m[3].str()).second);
        built += m[5].str() != "0" ? 1 : 0;
        vst3 += m[5].str() == "2" ? 1 : 0;
    }
    CHECK (rows > 0);
    CHECK (built == (int) lynx::plugexport::catalog.size());
    CHECK (vst3 >= 1 && vst3 <= 10); // a short list: each VST3 carries the whole runtime (docs/PLUGIN_EXPORT.md)
}

TEST (pluginexport188b_plugin_downloads_are_never_taken_for_the_app)
{
    // The plugin bundles sit in the same GitHub release as the app (scripts/package-plugins.sh): the updater must never
    // pick one for an app update, and their checksum file is not the app's SHA256SUMS.txt
    using namespace lynx::update;
    std::vector<Asset> assets;
    for (const char* name : { "LYNX-Plugins-0.1.26-win-x64.zip", "LYNX-Plugins-0.1.26-linux-x86_64.tar.xz",
                              "LYNX-Plugins-0.1.26-macos-universal.tar.xz", "LYNX-Plugins-0.1.26-SHA256SUMS.txt" })
        assets.push_back ({ name, std::string ("https://example.invalid/") + name, 1 });
    for (const auto f : { Flavour::windowsInstaller, Flavour::windowsPortable, Flavour::macos, Flavour::linuxAppImage, Flavour::linuxTarball })
        CHECK (assetFor (f, assets) == -1);
    CHECK (checksumsAsset (assets) == -1);
}

TEST (pluginexport188b_side_chain_bus_reaches_the_detector)
{
    // The host's side-chain bus enters the side track (RackRenderer::setSideInput), the slot's side-chain source: with
    // Crest Compressor's external side chain on, a loud side signal compresses the main input; a silent one doesn't,
    // and the side track itself is never heard
    constexpr double rate = 48000.0;
    constexpr int size = 256;
    auto song = pluginExportSong (lynx::EffectKind::crestCompressor);
    const auto& info = lynx::effectInfo (lynx::EffectKind::crestCompressor);
    for (std::size_t i = 0; i < info.params.size(); ++i)
        if (std::string (info.params[i].name) == "External SC")
            song.mixer[1].effects[0].params[i] = 1.0f;
    auto state = lynx::RackState::build (song, nullptr, rate);
    const auto params = song.mixer[1].effects[0].params;
    const auto run = [&] (float sideLevel, float mainLevel) {
        auto renderer = std::make_unique<lynx::RackRenderer>();
        renderer->prepare (rate, size);
        std::vector<float> inL (size), inR (size), sideL (size), sideR (size), outL (size), outR (size);
        auto energy = 0.0;
        for (int b = 0; b < 200; ++b)
        {
            pluginExportInput (inL, inR, b);
            for (std::size_t i = 0; i < inL.size(); ++i)
            {
                sideL[i] = sideR[i] = sideLevel * (i % 64 < 32 ? 1.0f : -1.0f);
                inL[i] *= mainLevel;
                inR[i] *= mainLevel;
            }
            renderer->setMetronomeInput (inL.data(), inR.data(), 1);
            renderer->setSideInput (sideL.data(), sideR.data(), 2);
            renderer->setSlotParamsOverride (&params, 1, 0);
            renderer->process (state.get(), false, 0.0, size, outL.data(), outR.data());
            if (b >= 100)
                for (int i = 0; i < size; ++i)
                    energy += (double) outL[(std::size_t) i] * outL[(std::size_t) i];
        }
        return energy;
    };
    const auto quietSide = run (0.0f, 1.0f), loudSide = run (0.9f, 1.0f), sideOnly = run (0.9f, 0.0f);
    CHECK (quietSide > 0.0);
    CHECK (loudSide < quietSide * 0.7); // compressed by the side signal
    CHECK (sideOnly < 1.0e-9);          // the side track goes nowhere
}
