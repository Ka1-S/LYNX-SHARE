#include "core/NativeEngines.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/FlSamplers.h"
#include "core/Fpc.h"
#include "core/GrossBeat.h"
#include "core/HardcoreFx.h"
#include "core/Harmless.h"
#include "core/HarmlessFx.h"
#include "core/LynxBanks.h"
#include "core/MidiOut.h"
#include "core/MixSuite.h"
#include "core/PhysicalModels.h"
#include "core/Slicex.h"
#include "flp/Flp.h"

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
    static const std::vector<InstrumentEngine> list = {
        { InstrumentKind::threeosc, "3x Osc", "3x Osc", own, false },
        { InstrumentKind::sytrus, "Sytrus", "Sytrus", fl, false },
        { InstrumentKind::harmor, "Harmor", "Harmor", fl, true },
        { InstrumentKind::harmless, "Harmless", "Harmless", own, false },
        { InstrumentKind::gms, "GMS", "GMS", fl, false },
        { InstrumentKind::morphine, "Morphine", "Morphine", fl, false },
        { InstrumentKind::ogun, "Ogun", "Ogun", fl, false },
        { InstrumentKind::toxic, "Toxic Biohazard", "Toxic Biohazard", fl, false },
        { InstrumentKind::poizone, "PoiZone", "PoiZone", fl, false },
        { InstrumentKind::sakura, "Sakura", "Sakura", fl, false },
        { InstrumentKind::kepler, "Kepler", "Kepler", fl, false },
        { InstrumentKind::keplerExo, "Kepler Exo", "Kepler Exo", fl, false },
        { InstrumentKind::transistorbass, "Transistor Bass", "Transistor Bass", fl, false },
        { InstrumentKind::boobass, "BooBass", "", fl, false }, // (its multisample: the user's BooBass_Eng_x64.dll)
        { InstrumentKind::simsynth, "SimSynth", "SimSynth", own, false },
        { InstrumentKind::minisynth, "MiniSynth", "MiniSynth", fl, false },
        { InstrumentKind::pocketSuperSaw, "Pocket SuperSaw", "", own, false }, // (FL Studio Mobile's SuperSaw: plays from its defaults)
        { InstrumentKind::dx10, "Fruity DX10", "Fruity DX10", own, false },
        { InstrumentKind::plucked, "Plucked!", "Plucked!", own, false },
        { InstrumentKind::sawer, "Sawer", "Sawer", fl, false },
        { InstrumentKind::beepmap, "BeepMap", "BeepMap", fl, false },
        { InstrumentKind::flkeys, "FL Keys", "FL Keys", fl, false },
        { InstrumentKind::slayer, "FL Slayer", "FL Slayer", fl, false },
        { InstrumentKind::granulizer, "Fruity Granulizer", "Fruity Granulizer", fl, false },
        { InstrumentKind::wavetraveller, "Wave Traveller", "Wave Traveller", own, false },
        { InstrumentKind::fruityslicer, "Fruity Slicer", "Fruity Slicer", own, false },
        { InstrumentKind::slicex, "Slicex", "", own, false }, // (round 190r: a new one is FL's Default preset values, decks empty)
        { InstrumentKind::drumaxx, "Drumaxx", "Drumaxx", fl, false },
        { InstrumentKind::drumpad, "Drumpad", "Drumpad", fl, false },
        { InstrumentKind::fpc, "FPC", "FPC", own, false }, // (without FL: FL's empty kit; the kits are FL's and the user's .fst files)
        { InstrumentKind::fruitkick, "Fruit Kick", "Fruit Kick", fl, false },
        { InstrumentKind::bassdrum, "BassDrum", "BassDrum", fl, false },
    };
    return list;
}

const std::vector<EffectEngine>& effectEngines()
{
    static const std::vector<EffectEngine> list = {
        { EffectKind::maximus, "Maximus", fl, false },
        { EffectKind::vocodex, "Vocodex", fl, false },
        { EffectKind::equo, "EQUO", fl, false },
        { EffectKind::distructor, "Distructor", fl, false },
        { EffectKind::lovePhilter, "Fruity Love Philter", fl, false },
        { EffectKind::grossBeat, "Gross Beat", fl, false },
        { EffectKind::convolver, "Fruity Convolver", fl, false },
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
    if (e.kind == InstrumentKind::boobass)
        return ! booBassDll (access).empty();
    // an engine with a LYNX preset bank starts from it without FL (round 168n)
    return e.needs == Needs::nothing || banks::hasBank (e.kind) || ! defaultPresetPath (e.presetFolder, true, access).empty();
}

bool available (const EffectEngine& e, const FlAccess& access)
{
    return e.needs == Needs::nothing || ! defaultPresetPath (e.presetFolder, false, access).empty();
}

std::optional<ChannelDef> makeChannel (InstrumentKind kind, const FlAccess& access)
{
    const auto* engine = instrumentEngine (kind);
    if (engine == nullptr)
        return std::nullopt;

    if (kind == InstrumentKind::boobass)
    {
        const auto dll = booBassDll (access);
        if (dll.empty() || ! booBassDllUsable (dll))
            return std::nullopt;
        ChannelDef def;
        def.name = engine->name;
        def.instrument = kind;
        def.samplePath = dll;
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
        return def;

    if (engine->needs != Needs::nothing)
        return std::nullopt;

    ChannelDef def; // LYNX's own default: the engine plays from an empty state
    def.name = engine->name;
    def.instrument = kind;
    if (kind == InstrumentKind::slicex)
        def.pluginState = slicex::write (slicex::fresh());
    if (kind == InstrumentKind::fpc) // FL's "Empty" kit (32 pads on FL's default notes, no samples)
        setFpcKit (def, fpc::emptyKit(), {});
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
