#include "core/NativeEngines.h"

#include "core/BassDrum.h"
#include "core/DirectWaveState.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FlSamplers.h"
#include "core/Fpc.h"
#include "core/Gms.h"
#include "core/GmsMobile.h"
#include "core/GrossBeat.h"
#include "core/HardcoreFx.h"
#include "core/Harmless.h"
#include "core/HarmlessFx.h"
#include "core/DrumSynthLive.h"
#include "core/Kepler.h"
#include "core/LynxBanks.h"
#include "core/MidiOut.h"
#include "core/MiniSynth.h"
#include "core/MixSuite.h"
#include "core/NativeData.h"
#include "core/NativeSlots.h"
#include "core/PhysicalModels.h"
#include "core/Sawer.h"
#include "core/Slayer.h"
#include "core/Slicex.h"
#include "core/Toxic.h"
#include "core/TransistorBass.h"
#include "flp/Flp.h"

// TR_NOOP marks the menu texts for translation (tools/i18n_keys.py); the UI translates them
#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::natives
{
namespace
{
constexpr auto fl = Needs::flStudio;
constexpr auto own = Needs::nothing;

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}

bool endsWith (const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && lower (s.substr (s.size() - tail.size())) == tail;
}

// The plugin's name as FL's import knows it (event 0xC9 of a channel / slot)
const char* flPluginName (InstrumentKind kind)
{
    switch (kind)
    {
        case InstrumentKind::toxic: return "Toxic Biohazard";
        case InstrumentKind::gms: return "GMS";
        case InstrumentKind::morphine: return "Morphine";
        default: return "";
    }
}

std::vector<std::uint8_t> readPreset (const std::string& path, const FlAccess& access)
{
    if (path.empty() || ! access.readFile)
        return {};
    return access.readFile (path);
}

FlpImportOptions importOptions (const FlAccess& access)
{
    FlpImportOptions options;
    options.roots = access.roots;
    options.fileExists = access.fileExists;
    options.readFile = access.readFile;
    options.saveEmbedded = access.saveEmbedded;
    options.fpcAsChannel = true; // (an FPC kit preset: the whole kit on the channel)
    return options;
}

std::optional<ChannelDef> channelFromPreset (InstrumentKind kind, const InstrumentEngine& engine, const std::string& path, const FlAccess& access)
{
    const auto bytes = readPreset (path, access);
    if (bytes.empty())
        return std::nullopt;
    try
    {
        flp::Project project;
        if (! endsWith (path, ".fst")) // Toxic Biohazard's .tbio, GMS' .gmsynth, Morphine's .mrp
        {
            flp::Channel c;
            c.pluginName = flPluginName (kind);
            // the state FL saves is "SSLF" + size + the preset file
            c.pluginState = { 'S', 'S', 'L', 'F' };
            for (int i = 0; i < 4; ++i)
                c.pluginState.push_back ((std::uint8_t) (bytes.size() >> (8 * i)));
            c.pluginState.insert (c.pluginState.end(), bytes.begin(), bytes.end());
            project.channels.push_back (c);
        }
        else
            project = flp::parseProject (bytes, true);

        if (project.channels.empty())
            return std::nullopt;
        auto& c = project.channels.front();
        c.name = engine.name;
        if (c.type == flp::ChannelType::unknown)
            c.type = flp::ChannelType::instrument;
        c.mixerTrack = -1;
        const auto song = importFlp (project, importOptions (access));
        if (song.channels.empty() || song.channels.front().instrument != kind)
            return std::nullopt;
        auto def = song.channels.front();
        def.name = engine.name;
        def.mixerTrack = 0;
        return def;
    }
    catch (const flp::Error&)
    {
        return std::nullopt;
    }
}
} // namespace

const std::vector<InstrumentEngine>& instrumentEngines()
{
    // name, preset folder (Generators), needs, beta. Engines that play from an empty state (LYNX's own defaults) need
    // nothing; the rest start from the user's FL "Default" preset (with its data files: DLL tables, wave folders...).
    // Round 190rh: an engine with a LYNX bank (core/LynxBankList.inc) and, where it reads FL data, a set of LYNX's own
    // (core/NativeData.h) needs nothing; with FL installed it still starts from FL's preset.
    static const std::vector<InstrumentEngine> list = {
        { InstrumentKind::threeosc, "3x Osc", "3x Osc", own, false },
        { InstrumentKind::sytrus, "Sytrus", "Sytrus", own, false },
        { InstrumentKind::harmor, "Harmor", "Harmor", own, true },
        { InstrumentKind::harmless, "Harmless", "Harmless", own, false },
        { InstrumentKind::gms, "GMS", "GMS", own, false },
        { InstrumentKind::morphine, "Morphine", "Morphine", own, false },
        { InstrumentKind::ogun, "Ogun", "Ogun", own, false },
        { InstrumentKind::autogun, "Autogun", "", own, false }, // (round 190rh: Ogun's engine and bank; FL's plugin database lists it too)
        { InstrumentKind::toxic, "Toxic Biohazard", "Toxic Biohazard", own, false },
        { InstrumentKind::poizone, "PoiZone", "PoiZone", own, false },
        { InstrumentKind::sakura, "Sakura", "Sakura", own, false },
        { InstrumentKind::kepler, "Kepler", "Kepler", own, false },
        { InstrumentKind::keplerExo, "Kepler Exo", "Kepler Exo", own, false },
        { InstrumentKind::transistorbass, "Transistor Bass", "Transistor Bass", own, false },
        { InstrumentKind::boobass, "BooBass", "", own, false }, // (its multisample: the user's BooBass_Eng_x64.dll)
        { InstrumentKind::simsynth, "SimSynth", "SimSynth", own, false },
        { InstrumentKind::minisynth, "MiniSynth", "MiniSynth", own, false },
        { InstrumentKind::pocketSuperSaw, "Pocket SuperSaw", "", own, false }, // (FL Studio Mobile's SuperSaw: plays from its defaults)
        { InstrumentKind::tetra, "Tetra", "", own, false }, // (round 190hs: LYNX's own FM synth, plays from its defaults: one sine)
        { InstrumentKind::helix, "Helix", "", own, false }, // (round 190ja: LYNX's own wavetable synth, plays from its defaults)
        { InstrumentKind::braid, "Braid", "", own, false }, // (round 190ko: LYNX's own two-engine macro synth, plays from its defaults)
        { InstrumentKind::lynxModel, "LYNX Model", "", own, false }, // (round 190ni: LYNX's own physical models, from its bank's Marimba)
        { InstrumentKind::dx10, "Fruity DX10", "Fruity DX10", own, false },
        { InstrumentKind::plucked, "Plucked!", "Plucked!", own, false },
        { InstrumentKind::sawer, "Sawer", "Sawer", own, false },
        { InstrumentKind::beepmap, "BeepMap", "BeepMap", own, false },
        { InstrumentKind::flkeys, "FL Keys", "FL Keys", fl, false },
        { InstrumentKind::slayer, "FL Slayer", "FL Slayer", own, false },
        { InstrumentKind::granulizer, "Fruity Granulizer", "Fruity Granulizer", own, false },
        { InstrumentKind::wavetraveller, "Wave Traveller", "Wave Traveller", own, false },
        { InstrumentKind::fruityslicer, "Fruity Slicer", "Fruity Slicer", own, false },
        { InstrumentKind::slicex, "Slicex", "", own, false }, // (round 190r: a new one is FL's Default preset values, decks empty)
        { InstrumentKind::drumaxx, "Drumaxx", "Drumaxx", own, false },
        { InstrumentKind::drumsynth, "Fruity DrumSynth Live", "", own, false }, // (round 190rh: from LYNX's bank; FL's own presets are 24-patch kits the engine does not read)
        { InstrumentKind::drumpad, "Drumpad", "Drumpad", own, false },
        { InstrumentKind::fpc, "FPC", "FPC", own, false }, // (without FL: FL's empty kit; the kits are FL's and the user's .fst files)
        { InstrumentKind::directwave, "DirectWave", "", own, false }, // (round 190hz: FL's new DirectWave, 128 empty programs)
        { InstrumentKind::fruitkick, "Fruit Kick", "Fruit Kick", own, false },
        { InstrumentKind::bassdrum, "BassDrum", "BassDrum", own, false },
    };
    return list;
}

const std::vector<EffectEngine>& effectEngines()
{
    static const std::vector<EffectEngine> list = {
        { EffectKind::maximus, "Maximus", own, false },
        { EffectKind::vocodex, "Vocodex", own, false },
        { EffectKind::equo, "EQUO", own, false },
        { EffectKind::distructor, "Distructor", own, false },
        { EffectKind::lovePhilter, "Fruity Love Philter", own, false },
        { EffectKind::grossBeat, "Gross Beat", own, false },
        { EffectKind::convolver, "Fruity Convolver", own, false },
        { EffectKind::flPitchShifter, "Pitch Shifter", own, false },
        { EffectKind::vintagePhaser, "Vintage Phaser", own, false },
        { EffectKind::squeeze, "Fruity Squeeze", own, false },
        { EffectKind::rhythmGain, "", own, false },
        { EffectKind::newtone, "", own, false }, // out of beta in round 165g (REA_rodada165g-flnewt165)
        { EffectKind::newtime, "", own, false }, // out of beta in round 167b (REA_rodada167b)
    };
    return list;
}

const InstrumentEngine* instrumentEngine (InstrumentKind kind)
{
    for (const auto& e : instrumentEngines())
        if (e.kind == kind)
            return &e;
    return nullptr;
}

const EffectEngine* effectEngine (EffectKind kind)
{
    for (const auto& e : effectEngines())
        if (e.kind == kind)
            return &e;
    return nullptr;
}

std::vector<std::string> presetCandidates (const std::string& presetFolder, bool generator, const FlAccess& access)
{
    std::vector<std::string> out;
    if (presetFolder.empty() || access.roots.data.empty())
        return out;
    const auto folder = std::string ("%FLStudioData%\\Patches\\Plugin presets\\") + (generator ? "Generators\\" : "Effects\\") + presetFolder;
    const auto exists = [&access] (const std::string& p) { return ! p.empty() && access.fileExists && access.fileExists (p); };

    // FL adds a new generator from its Plugin database (the Channel rack's "+" / the plugin picker load the entry's .fst
    // into the new channel, REA_rodada169l §3): for every generator that entry (Generators\<category>\<plugin>.fst)
    // comes first — Drumaxx' "Default Kit" (REA_rodada169e §1), Transistor Bass' "Factory programs", Sakura's "Bowed 6",
    // Ogun's version-11 state, and the engines whose Default preset differs from it (3x Osc, Sytrus, Harmor, Harmless,
    // PoiZone, MiniSynth, Sawer, FL Keys, Granulizer, Toxic, BeepMap: user decision, round 169l2). Where FL's Default.fst
    // equals it (Kepler, SimSynth, DX10, Plucked!, Fruit Kick) nothing changes; an entry LYNX can't read falls through to
    // the Default preset, then the folder.
    if (generator && access.listPresets)
    {
        const auto wanted = lower (presetFolder + ".fst");
        for (const auto& path : access.listPresets (resolveFlPath ("%FLStudioData%\\Patches\\Plugin database\\Generators", access.roots)))
        {
            const auto cut = path.find_last_of ("\\/");
            if (lower (cut == std::string::npos ? path : path.substr (cut + 1)) == wanted
                && std::find (out.begin(), out.end(), path) == out.end())
            {
                out.push_back (path);
                break;
            }
        }
    }

    for (const auto* name : { "Default.fst", "Default.tbio", "Default\\Default.tbio" })
        if (const auto path = resolveFlPath (folder + "\\" + name, access.roots); exists (path) && std::find (out.begin(), out.end(), path) == out.end())
            out.push_back (path);

    if (access.listPresets)
        for (const auto& path : access.listPresets (resolveFlPath (folder, access.roots)))
            if ((endsWith (path, ".fst") || endsWith (path, ".tbio") || endsWith (path, ".gmsynth") || endsWith (path, ".mrp"))
                && std::find (out.begin(), out.end(), path) == out.end())
                out.push_back (path);
    return out;
}

std::string defaultPresetPath (const std::string& presetFolder, bool generator, const FlAccess& access)
{
    const auto c = presetCandidates (presetFolder, generator, access);
    return c.empty() ? std::string() : c.front();
}

namespace
{
// BooBass has no preset folder: a new one is its three knobs at FL's Plugin database values (32767) over the
// multisample of the user's own engine DLL
std::string booBassDll (const FlAccess& access)
{
    const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\BooBass\\BooBass_Eng_x64.dll", access.roots);
    return ! dll.empty() && access.fileExists && access.fileExists (dll) ? dll : std::string();
}
} // namespace

std::optional<ChannelDef> channelFromPresetFile (InstrumentKind kind, const std::string& path, const FlAccess& access)
{
    const auto* engine = instrumentEngine (kind);
    return engine != nullptr ? channelFromPreset (kind, *engine, path, access) : std::nullopt;
}

std::optional<InstrumentKind> presetKind (const std::string& path, const FlAccess& access)
{
    if (endsWith (path, ".tbio"))
        return InstrumentKind::toxic;
    if (endsWith (path, ".gmsynth"))
        return InstrumentKind::gms;
    if (endsWith (path, ".mrp"))
        return InstrumentKind::morphine;
    if (! endsWith (path, ".fst"))
        return std::nullopt;
    const auto bytes = readPreset (path, access);
    if (bytes.empty())
        return std::nullopt;
    try
    {
        auto project = flp::parseProject (bytes, true);
        if (project.channels.empty() || project.channels.front().pluginName.empty())
            return std::nullopt;
        // imported as channelFromPreset does (the import's own table of FL's generators decides the engine)
        auto& c = project.channels.front();
        if (c.type == flp::ChannelType::unknown)
            c.type = flp::ChannelType::instrument;
        c.mixerTrack = -1;
        const auto song = importFlp (project, importOptions (access));
        if (song.channels.empty())
            return std::nullopt;
        const auto kind = song.channels.front().instrument;
        if (kind == InstrumentKind::midiOut || instrumentEngine (kind) != nullptr)
            return kind;
    }
    catch (const flp::Error&)
    {
    }
    return std::nullopt;
}

std::optional<ChannelDef> channelFromAnyPreset (const std::string& path, const std::string& presetName, const FlAccess& access)
{
    const auto kind = presetKind (path, access);
    if (! kind)
        return std::nullopt;
    ChannelDef blank;
    blank.instrument = *kind;
    if (const auto* engine = instrumentEngine (*kind))
        blank.name = engine->name;
    // FL's wrapper loads the preset into a new instance: the plugin's state from the file, the channel's defaults
    auto def = withPreset (blank, path, presetName, access);
    if (! def || def->instrument != *kind)
        return std::nullopt;
    def->mixerTrack = 0;
    return def;
}

std::string presetExtensionOf (InstrumentKind kind)
{
    switch (kind)
    {
        case InstrumentKind::toxic: return ".tbio";
        case InstrumentKind::gms: return ".gmsynth";
        case InstrumentKind::morphine: return ".mrp";
        default: return ".fst";
    }
}

std::string presetFolderOf (InstrumentKind kind)
{
    if (kind == InstrumentKind::midiOut)
        return "MIDI Out";
    const auto* engine = instrumentEngine (kind);
    return engine != nullptr ? engine->presetFolder : "";
}

std::optional<ChannelDef> withPreset (const ChannelDef& current, const std::string& path, const std::string& presetName, const FlAccess& access)
{
    ChannelDef out;
    if (current.instrument == InstrumentKind::midiOut)
    {
        const auto loaded = midiout::parse (midiout::stateFromFst (readPreset (path, access)));
        if (! loaded)
            return std::nullopt;
        auto state = *loaded;
        const auto now = midiout::stateOf (current.pluginState); // (FL keeps where it plays: channel and port)
        state.channel = now.channel;
        state.port = now.port;
        out = current;
        out.pluginState = midiout::write (state);
    }
    else
    {
        auto def = channelFromPresetFile (current.instrument, path, access);
        if (! def)
            return std::nullopt;
        // a plugin preset holds the plugin's state only: the channel's own settings stay (FL's wrapper loads the
        // .fst into the plugin)
        out = current;
        out.instrument = def->instrument;
        out.samplePath = def->samplePath;
        out.zones = def->zones;
        out.modelParams = def->modelParams;
        out.pluginState = def->pluginState;
        out.controllerState = def->controllerState;
        out.synth = def->synth;
        out.keyLabels = def->keyLabels;
    }
    if (! presetName.empty())
        out.name = presetName; // FL names the channel after the preset (measured, round 167o)
    return out;
}

bool available (const InstrumentEngine& e, const FlAccess& access)
{
    // an engine with a LYNX preset bank starts from it without FL (round 168n)
    return e.needs == Needs::nothing || banks::hasBank (e.kind) || ! defaultPresetPath (e.presetFolder, true, access).empty();
}

bool available (const EffectEngine& e, const FlAccess& access)
{
    return e.needs == Needs::nothing || ! defaultPresetPath (e.presetFolder, false, access).empty();
}

namespace
{
// the engines that read a data file of FL's: where it is under the installation, and whether LYNX can read it
struct FlData
{
    InstrumentKind kind;
    const char* path;
    bool (*usable) (const std::string&);
};
constexpr FlData flDataList[] = {
    { InstrumentKind::transistorbass, "Plugins\\Fruity\\Generators\\Transistor Bass\\engine_x64.dll", &transistorBassDllUsable },
    { InstrumentKind::minisynth, "Plugins\\Fruity\\Generators\\Minisynth\\oscdata.dat", &miniSynthDataUsable },
    { InstrumentKind::bassdrum, "Plugins\\Fruity\\Generators\\BassDrum", &bassDrumDataUsable },
    { InstrumentKind::gms, "Plugins\\Fruity\\Generators\\GMS\\Synth Waves", &gmsDataUsable },
    { InstrumentKind::boobass, "Plugins\\Fruity\\Generators\\BooBass\\BooBass_Eng_x64.dll", &booBassDllUsable },
    { InstrumentKind::slayer, "Plugins\\Fruity\\Generators\\FL Slayer\\Slayer_Engine.dll", &slayerDllUsable },
    { InstrumentKind::drumsynth, "Plugins\\Fruity\\Generators\\Fruity DrumSynth Live\\drumsynth_eng_x64.dll", &drumSynthDllUsable },
    { InstrumentKind::toxic, "Plugins\\Fruity\\Generators\\Toxic Biohazard\\Toxic Biohazard_x64.dll", &toxicDllUsable },
    { InstrumentKind::kepler, "Plugins\\Fruity\\Generators\\Kepler\\engine_x64.dll", [] (const std::string& p) { return keplerDllUsable (p) && ! keplerExoDllUsable (p); } },
    { InstrumentKind::keplerExo, "Plugins\\Fruity\\Generators\\Kepler Exo\\engine_x64.dll", &keplerExoDllUsable },
    { InstrumentKind::sawer, "Plugins\\Fruity\\Generators\\Sawer\\data.saw", [] (const std::string& path) { return ! sawerReadTable (path).empty(); } },
};

const FlData* flDataOf (InstrumentKind kind) noexcept
{
    for (const auto& d : flDataList)
        if (d.kind == kind)
            return &d;
    return nullptr;
}
} // namespace

DataSource dataSource (const ChannelDef& channel)
{
    const auto* data = flDataOf (channel.instrument);
    if (data == nullptr || transistorBassIsFlm (channel) || miniSynthIsFlm (channel) || gmsIsFlm (channel)) // (FL Studio Mobile's builds of the engines have their own rules)
        return DataSource::notNeeded;
    if ((channel.instrument == InstrumentKind::toxic || channel.instrument == InstrumentKind::drumsynth) && channel.samplePath.empty())
        return DataSource::notNeeded; // (the drawn wave and the sine only: LYNX's bank before round 190rh)
    if (nativedata::isOwn (channel.samplePath))
        return DataSource::own;
    if (! channel.samplePath.empty() && data->usable (channel.samplePath))
        return DataSource::fl;
    return nativedata::hasOwnSet (channel.instrument) && ! channel.samplePath.empty() ? DataSource::ownForFl : DataSource::missing;
}

std::string flDataPath (InstrumentKind kind, const FlAccess& access)
{
    const auto* data = flDataOf (kind);
    if (data == nullptr || access.roots.factoryData.empty())
        return {};
    const auto path = resolveFlPath (std::string ("%FLStudioFactoryData%\\") + data->path, access.roots);
    return ! path.empty() && data->usable (path) ? path : std::string();
}

std::vector<FlDataLine> flDataReport (const FlAccess& access)
{
    struct Row
    {
        const char* name;
        const char* path; // a file under the installation
        bool (*usable) (const std::string&); // null: its being there is enough
    };
    static const Row rows[] = {
        { "Transistor Bass", "Plugins\\Fruity\\Generators\\Transistor Bass\\engine_x64.dll", &transistorBassDllUsable },
        { "Kepler", "Plugins\\Fruity\\Generators\\Kepler\\engine_x64.dll", [] (const std::string& p) { return keplerDllUsable (p) && ! keplerExoDllUsable (p); } },
        { "Kepler Exo", "Plugins\\Fruity\\Generators\\Kepler Exo\\engine_x64.dll", &keplerExoDllUsable },
        { "FL Slayer", "Plugins\\Fruity\\Generators\\FL Slayer\\Slayer_Engine.dll", &slayerDllUsable },
        { "BooBass", "Plugins\\Fruity\\Generators\\BooBass\\BooBass_Eng_x64.dll", &booBassDllUsable },
        { "Fruity DrumSynth Live", "Plugins\\Fruity\\Generators\\Fruity DrumSynth Live\\drumsynth_eng_x64.dll", &drumSynthDllUsable },
        { "Toxic Biohazard", "Plugins\\Fruity\\Generators\\Toxic Biohazard\\Toxic Biohazard_x64.dll", &toxicDllUsable },
        { "MiniSynth", "Plugins\\Fruity\\Generators\\Minisynth\\oscdata.dat", &miniSynthDataUsable },
        { "Sawer", "Plugins\\Fruity\\Generators\\Sawer\\data.saw", [] (const std::string& p) { return ! sawerReadTable (p).empty(); } },
        { "BassDrum", "Plugins\\Fruity\\Generators\\BassDrum\\sounddata", nullptr },
        { "GMS", "Plugins\\Fruity\\Generators\\GMS\\Synth Waves\\Saw.wav", nullptr },
        { "FL Keys", "Plugins\\Fruity\\Generators\\FL Keys\\Wavtables\\default.wti", nullptr },
    };
    std::vector<FlDataLine> out;
    for (const auto& r : rows)
    {
        FlDataLine line { r.name };
        const auto path = resolveFlPath (std::string ("%FLStudioFactoryData%\\") + r.path, access.roots);
        line.there = ! path.empty() && access.fileExists && access.fileExists (path);
        if (line.there) // (a file this user may not read is there and not readable)
            line.readable = r.usable != nullptr ? r.usable (path) : access.readFile && ! access.readFile (path).empty();
        out.push_back (line);
    }
    return out;
}

std::string needsText (InstrumentKind kind)
{
    return kind == InstrumentKind::flkeys ? TR_NOOP ("{0} (needs FL Studio for its samples)") : "";
}

std::optional<ChannelDef> makeChannel (InstrumentKind kind, const FlAccess& access)
{
    const auto* engine = instrumentEngine (kind);
    if (engine == nullptr)
        return std::nullopt;

    if (kind == InstrumentKind::boobass)
    {
        const auto dll = booBassDll (access);
        ChannelDef def;
        def.name = engine->name;
        def.instrument = kind;
        def.samplePath = ! dll.empty() && booBassDllUsable (dll) ? dll : nativedata::ownPath (kind); // (round 190rh)
        // FL's new BooBass is its Plugin database state (Generators\Misc\BooBass.fst: version 1, the three knobs at
        // 0x7fff; round 169l)
        def.modelParams = { 32767.0f, 32767.0f, 32767.0f };
        return def;
    }

    // the user's FL default preset, imported like an .flp channel (the exact engine with its data files)
    int tries = 0;
    for (const auto& path : presetCandidates (engine->presetFolder, true, access))
    {
        if (++tries > 60)
            break;
        if (auto def = channelFromPreset (kind, *engine, path, access))
            return def;
    }

    // without FL: the first preset of LYNX's own bank (round 168n)
    if (auto def = banks::defaultChannel (kind, engine->name))
    {
        if (def->samplePath.empty() && (kind == InstrumentKind::toxic || kind == InstrumentKind::drumsynth)) // (round 190rh: their waves are LYNX's own)
        {
            const auto fl = flDataPath (kind, access);
            def->samplePath = fl.empty() ? nativedata::ownPath (kind) : fl;
        }
        if (kind == InstrumentKind::autogun) // its Magic Dust stage, where FL has it
            if (const auto dust = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Autogun\\Data\\SG.dat", access.roots);
                ! dust.empty() && access.fileExists && access.fileExists (dust))
                def->samplePath = dust;
        return def;
    }

    if (engine->needs != Needs::nothing)
        return std::nullopt;

    ChannelDef def; // LYNX's own default: the engine plays from an empty state
    def.name = engine->name;
    def.instrument = kind;
    if (kind == InstrumentKind::slicex)
        def.pluginState = slicex::write (slicex::fresh());
    if (kind == InstrumentKind::fpc) // FL's "Empty" kit (32 pads on FL's default notes, no samples)
        setFpcKit (def, fpc::emptyKit(), {});
    if (kind == InstrumentKind::directwave) // FL's new DirectWave (the plugin database's preset): no zones yet
    {
        def.pluginState = dw::writeDoc (dw::freshDoc());
        def.modelParams = dw::encodeFormats ({});
    }
    return def;
}

std::optional<ChannelDef> autogunShotChannel (const FlAccess& access)
{
    if (access.roots.data.empty() || ! access.readFile)
        return std::nullopt;
    const auto bytes = access.readFile (access.roots.data + "\\System\\Render tests\\Autogun.flp");
    if (bytes.empty())
        return std::nullopt;
    try
    {
        const auto song = importFlp (flp::parseProject (bytes, true), importOptions (access));
        for (const auto& c : song.channels)
            if (c.instrument == InstrumentKind::autogun)
            {
                auto def = c;
                def.name = "Autogun";
                def.mixerTrack = 0;
                return def;
            }
    }
    catch (const flp::Error&)
    {
    }
    return std::nullopt;
}

FlDataReader flDataReader (const FlAccess& access)
{
    return [&access] (const std::string& relative) -> std::vector<std::uint8_t> {
        const auto p = resolveFlPath ("%FLStudioFactoryData%\\" + relative, access.roots);
        if (p.empty() || ! access.readFile || (access.fileExists && ! access.fileExists (p)))
            return {};
        return access.readFile (p);
    };
}

std::optional<EffectSlot> makeSlot (EffectKind kind, const FlAccess& access, double bpm)
{
    const auto* engine = effectEngine (kind);
    if (engine == nullptr)
        return std::nullopt;

    if (const auto path = defaultPresetPath (engine->presetFolder, false, access); ! path.empty())
    {
        const auto bytes = readPreset (path, access);
        try
        {
            const auto project = flp::parseProject (bytes, true);
            if (! project.channels.empty() && ! project.channels.front().pluginState.empty())
            {
                auto slot = effectFromFl (project.channels.front().pluginName.empty() ? engine->presetFolder : project.channels.front().pluginName,
                                          project.channels.front().pluginState, bpm, flDataReader (access));
                if (slot && slot->kind == kind)
                {
                    if (kind == EffectKind::grossBeat)
                    {
                        grossbeat::setPpq (slot->state, 96);
                        // round 173d: FL's Default preset keeps every slot empty (FL fills a new instance from its default
                        // bank, not in the preset): with it every slot picked in the editor, and the attack / release /
                        // tension / mix knobs, did nothing. A new instance starts on LYNX's factory slots, as without FL
                        const auto names = grossbeat::slotNames (slot->state, false);
                        if (std::all_of (names.begin(), names.end(), [] (const std::string& n) { return n.empty(); }))
                            slot->state = grossbeat::factoryContainer();
                    }
                    return slot;
                }
            }
        }
        catch (const flp::Error&)
        {
        }
    }

    if (engine->needs != Needs::nothing)
        return std::nullopt;
    if (auto slot = nativeslots::make (kind)) // (round 190rh: a state of LYNX's own)
        return slot;
    return makeEffectSlot (kind);
}

std::optional<EffectSlot> newEffect (EffectKind kind, const FlAccess& access, double bpm)
{
    if (effectEngine (kind) != nullptr)
        return makeSlot (kind, access, bpm);
    auto slot = makeEffectSlot (kind);
    // round 173d: a new Hardcore carries the cabinet impulses of the user's FL installation, as one imported from an
    // .flp does (hardcore::fromFl); before, its Cabinet menu did nothing on a new instance. Without FL it has none.
    if (kind == EffectKind::hardcore && access.readFile)
    {
        std::vector<std::pair<int, std::vector<std::uint8_t>>> files;
        for (int c = 1; c <= hardcore::numCabinets; ++c)
        {
            const auto p = resolveFlPath ("%FLStudioFactoryData%\\" + hardcore::cabinetPath (c), access.roots);
            if (p.empty() || (access.fileExists && ! access.fileExists (p)))
                continue;
            if (auto b = access.readFile (p); ! b.empty())
                files.emplace_back (c, std::move (b));
        }
        if (! files.empty())
            slot.state = hardcore::packCabinets (files);
    }
    // round tp172d: an Ember added by hand opens on its Classic panel (the hardware saturator, its role); its multiband
    // page is one click away ("Advanced"). makeEffectSlot's default (the multiband engine) stays, for loaded projects
    if (kind == EffectKind::ember)
        slot.params[(std::size_t) mixsuite::ember::voicing] = 1.0f;
    return slot;
}

std::string effectMenuName (EffectKind kind)
{
    std::string name = effectInfo (kind).name;
    if (const auto* e = effectEngine (kind); e != nullptr && e->beta)
        name += " (beta)";
    return name;
}

std::string instrumentMenuName (const InstrumentEngine& e)
{
    return std::string (e.name) + (e.beta ? " (beta)" : "");
}
void followControl (ChannelDef& c, int index, const FlAccess& fl)
{
    if (c.instrument != InstrumentKind::harmless || index != harmlessComboIndex + 3 || c.modelParams.size() < (std::size_t) harmlessCompIndex)
        return;
    // the type's Maximus state follows the settings (byte count, then the bytes), as FlpImport stores it
    c.modelParams.resize ((std::size_t) harmlessCompIndex);
    const auto type = (int) std::lround (c.modelParams[(std::size_t) harmlessComboIndex + 3]);
    if (type <= 0 || ! fl.readFile)
        return;
    const auto path = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Harmless\\Data\\Compression\\" + std::to_string (type) + ".fst",
                                     fl.roots);
    if (path.empty() || (fl.fileExists && ! fl.fileExists (path)))
        return;
    const auto state = hlfx::maximusStateFromFst (fl.readFile (path));
    if (! state || hlfx::makeMaximusPreset (*state) == nullptr)
        return;
    c.modelParams.push_back ((float) state->size());
    for (const auto byte : *state)
        c.modelParams.push_back ((float) byte);
}

} // namespace lynx::natives
