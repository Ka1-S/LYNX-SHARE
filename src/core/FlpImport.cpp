#include "core/FlCurve.h"
#include "core/FlpImport.h"
#include "core/FruityDance.h"
#include "core/FlpExport.h"

#include "core/Fpc.h"
#include "core/FruitySlicer.h"

#include "core/Dvs.h"
#include "core/FlExtraFx.h"
#include "core/Scratcher.h"
#include "core/Transmitter.h"
#include "core/EquoFx.h"
#include "core/TransporterFl.h"
#include "core/ControlSurface.h"
#include "core/VoltageController.h"
#include "core/XyController.h"
#include "core/Vfx.h"
#include "core/VideoPlayer.h"
#include "core/Patcher.h"
#include <filesystem>
#include "core/EditorPolish.h"
#include "core/FruityPhaser.h"
#include "core/FlPluginDb.h"
#include "core/FlVoiceProcessor.h"
#include "core/HardcoreFx.h"
#include "core/GrossBeat.h"
#include "core/LovePhilter.h"
#include "core/DrumSynthLive.h"
#include "core/Gms.h"
#include "core/Harmless.h"
#include "core/Morphine.h"
#include "core/BassDrum.h"
#include "core/FruitKick.h"
#include "core/Harmor.h"
#include "core/ClipOps.h"
#include "core/MiniSynth.h"
#include "core/SimSynth.h"
#include "core/Drumaxx.h"
#include "core/Plucked.h"
#include "core/FlKeys.h"
#include "core/DirectWave.h"
#include "core/BeepMap.h"
#include "core/Sawer.h"
#include "core/Granulizer.h"
#include "core/WaveTraveller.h"
#include "core/SlicexInstrument.h"
#include "core/Ogun.h"
#include "core/Dx10.h"
#include "core/Kepler.h"
#include "core/TransistorBass.h"
#include "core/Slayer.h"
#include "core/PoiZone.h"
#include "core/Toxic.h"
#include "core/ThreeOsc.h"
#include "core/TimeStretch.h"
#include "core/HarmlessFx.h"
#include "core/SytrusEngine.h"
#include "core/PhysicalModels.h"

#include "core/ChannelMiscFx.h"
#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/Reeverb2.h"
#include "core/LuxeVerb.h"
#include "core/ConvolverFx.h"
#include "core/ChorusFx.h"
#include "core/FlangusFastDist.h"
#include "core/FlPatcher.h"
#include "core/FlSamplers.h"
#include "core/FlSynths.h"
#include "core/FlWrapper.h"
#include "core/FruityDynamics.h"
#include "core/SoundFont.h"
#include "core/SynthPresets.h"
#include "core/NewtoneFx.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cmath>
#include <map>
#include <ctime>

namespace lynx
{
double flDateTimeToUnix (double days)
{
    if (! (days > 25569.0 && days < 2958466.0)) // 1970-01-01 … 9999-12-31
        return 0.0;
    // the local wall-clock reading as a civil date (H. Hinnant's days → y/m/d), then mktime gives UTC
    const auto local = (days - 25569.0) * 86400.0;
    const auto secs = (long long) std::floor (local);
    long long z = secs / 86400 + 719468;
    const auto sod = secs % 86400;
    const long long era = z / 146097, doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    std::tm tm {};
    tm.tm_mday = (int) (doy - (153 * mp + 2) / 5 + 1);
    tm.tm_mon = (int) (mp < 10 ? mp + 2 : mp - 10);
    tm.tm_year = (int) (yoe + era * 400 + (tm.tm_mon <= 1 ? 1 : 0)) - 1900;
    tm.tm_hour = (int) (sod / 3600);
    tm.tm_min = (int) (sod / 60 % 60);
    tm.tm_sec = (int) (sod % 60);
    tm.tm_isdst = -1;
    const auto t = std::mktime (&tm);
    return t == (std::time_t) -1 ? local : (double) t + (local - (double) secs);
}

namespace
{
bool contains (const std::string& haystack, const char* needle)
{
    return haystack.find (needle) != std::string::npos;
}

std::string lowercase (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace

void applyFlLayerFlags (int flags, LayerSettings& layer)
{
    // REA_rodada164a §1 [DECOMP 0xF46490, 0xD29240]: bit 0 Random, bit 1 Crossfade, bit 2 Sequential (Random wins)
    layer.random = (flags & 1) != 0;
    layer.crossfade = (flags & 2) != 0;
    layer.sequential = (flags & 4) != 0 && ! layer.random;
}

InstrumentKind guessInstrument (const std::string& channelName, flp::ChannelType type)
{
    const auto name = lowercase (channelName);

    if (type == flp::ChannelType::sampler || type == flp::ChannelType::unknown)
    {
        if (contains (name, "kick") || contains (name, "bd") || contains (name, "808"))
            return InstrumentKind::kick;
        if (contains (name, "snare") || contains (name, "snr") || contains (name, "rim"))
            return InstrumentKind::snare;
        if (contains (name, "clap") || contains (name, "snap"))
            return InstrumentKind::clap;
        if (contains (name, "hat") || contains (name, "hh") || contains (name, "ride") || contains (name, "cym")
            || contains (name, "crash") || contains (name, "shaker") || contains (name, "perc") || contains (name, "tamb"))
            return InstrumentKind::hat;
    }

    return InstrumentKind::synth;
}

// `path` as it exists on disk when only the letter case differs (POSIX file systems are case-sensitive; FL's paths
// come from Windows). Returns `path` unchanged when it exists or no case-insensitive match is found.
std::string existingPathIgnoringCase (const std::string& path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || path.front() != '/' || fs::exists (path, ec))
        return path;
    auto lower = [] (std::string t) {
        std::transform (t.begin(), t.end(), t.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return t;
    };
    fs::path current ("/");
    for (const auto& part : fs::path (path).relative_path())
    {
        if (fs::exists (current / part, ec))
        {
            current /= part;
            continue;
        }
        const auto wanted = lower (part.string());
        bool found = false;
        for (fs::directory_iterator it (current, ec), end; ! ec && it != end; it.increment (ec))
            if (lower (it->path().filename().string()) == wanted)
            {
                current = it->path();
                found = true;
                break;
            }
        if (! found)
            return path;
    }
    return current.string();
}

std::string resolveFlPath (const std::string& flPath, const FlPathRoots& roots)
{
    const std::pair<const char*, const std::string*> variables[] = {
        { "%FLStudioFactoryData%", &roots.factoryData },
        { "%FLStudioUserData%", &roots.userData },
        { "%FLStudioData%", &roots.data },
        // FL 2026 [EXEC issue #124, 26.1.7]: a sample under FL's Shared data folder (Documents\Image-Line) is saved as
        // "%ILSharedData%<rest>" (no separator after the variable), one under its Downloads folder (FL Cloud sounds,
        // downloaded packs) as "%FLStudioDownloadsData%\<rest>"
        { "%FLStudioDownloadsData%", &roots.downloadsData },
        { "%ILSharedData%", &roots.sharedData },
    };

    for (const auto& [name, root] : variables)
    {
        const std::string variable (name);

        if (flPath.compare (0, variable.size(), variable) == 0)
        {
            if (root->empty())
                return {};
            auto rest = flPath.substr (variable.size());
            const auto separator = [] (char c) { return c == '\\' || c == '/'; };
            if (! rest.empty() && ! separator (rest.front()) && ! separator (root->back()))
                rest.insert (rest.begin(), '\\'); // %ILSharedData% is the folder with its trailing separator
            else if (! rest.empty() && separator (rest.front()) && separator (root->back()))
                rest.erase (rest.begin());
            auto path = *root + rest;
            // a POSIX root (macOS, Linux, a Wine prefix): FL's backslashes become '/', and FL's file names, written for
            // a case-insensitive file system, are matched ignoring case when the exact spelling does not exist
            if (root->front() == '/')
            {
                std::replace (path.begin(), path.end(), '\\', '/');
                path = existingPathIgnoringCase (path);
            }
            return path;
        }
    }

    return flPath;
}

namespace
{
const std::pair<const char*, EffectKind> flEffectTable[] = {
        { "Fruity Parametric EQ 2", EffectKind::parametricEq }, { "Fruity Parametric EQ", EffectKind::parametricEq },
        { "Fruity 7 Band EQ", EffectKind::parametricEq },
        { "Fruity Limiter", EffectKind::limiter },     { "Maximus", EffectKind::limiter },           { "Fruity Soft Clipper", EffectKind::distortion },
        { "Fruity Reeverb 2", EffectKind::reverb },    { "Fruity Reeverb", EffectKind::reverb },     { "LuxeVerb", EffectKind::reverb },
        { "Fruity Delay 3", EffectKind::delay },       { "Fruity Delay 2", EffectKind::delay },      { "Fruity Delay Bank", EffectKind::delay },
        { "Fruity Delay", EffectKind::delay },         { "Fruity Compressor", EffectKind::compressor },
        { "Fruity Multiband Compressor", EffectKind::compressor }, { "Soundgoodizer", EffectKind::compressor },
        { "Fruity WaveShaper", EffectKind::distortion }, { "Fruity Fast Dist", EffectKind::distortion },
        { "Fruity Blood Overdrive", EffectKind::distortion }, { "Distructor", EffectKind::distortion },
        { "Fruity Flangus", EffectKind::chorus },      { "Fruity Chorus", EffectKind::chorus },      { "Hyper Chorus", EffectKind::chorus },
        { "Vintage Chorus", EffectKind::chorus },      { "Fruity Flanger", EffectKind::chorus },
        { "Fruity Balance", EffectKind::utility },    { "Fruity PanOMatic", EffectKind::utility },  { "Fruity Stereo Shaper", EffectKind::utility },
        { "Fruity Center", EffectKind::utility },     { "Fruity Phase Inverter", EffectKind::utility },
        { "Fruity Stereo Enhancer", EffectKind::utility }, { "Fruity Convolver", EffectKind::reverb }, { "EQUO", EffectKind::parametricEq },
        { "Spreader", EffectKind::utility },          { "Transient Processor", EffectKind::utility },
        { "Fruity Phaser", EffectKind::chorus },      { "Fruity Bass Boost", EffectKind::eq },      { "Fruity Free Filter", EffectKind::filter },
        { "Fruity Love Philter", EffectKind::filter }, { "Fruity Filter", EffectKind::filter },      { "Fruity Fast LP", EffectKind::filter },
        { "Gross Beat", EffectKind::volumeGate },     { "Frequency Splitter", EffectKind::parametricEq },
        { "Fruity Peak Controller", EffectKind::peakController }, { "Fruity Formula Controller", EffectKind::formulaController },
        { "Pitch Shifter", EffectKind::pitchShift },  { "Frequency Shifter", EffectKind::freqShift },
        { "Emphasis", EffectKind::limiter },          { "Low Lifter", EffectKind::parametricEq },
        { "Fruity Send", EffectKind::utility },       { "Transporter", EffectKind::utility },
        { "Vintage Phaser", EffectKind::vintagePhaser },     { "Fruity Squeeze", EffectKind::squeeze },
        { "Hardcore", EffectKind::hardcore },
        { "Multiband Delay", EffectKind::delay },
        { "Emphasizer", EffectKind::emphasizer },
        { "Wave Candy", EffectKind::waveCandy },     { "ZGameEditor Visualizer", EffectKind::visualizer },
        { "Fruity Mute 2", EffectKind::fruityMute2 }, { "Fruity Big Clock", EffectKind::bigClock }, // round 165g
        { "Tuner", EffectKind::tuner },              { "Fruity HTML NoteBook", EffectKind::htmlNotebook },
        { "Fruity Scratcher", EffectKind::scratcher }, // round 165g (flscratch165)
        { "Transmitter", EffectKind::transmitter },  // round 165g (fltrans165)
};
} // namespace

std::optional<EffectKind> effectForFlPlugin (const std::string& name)
{
    // old presets (FL 3) store the name with different casing ("Fruity delay 2")
    const auto wanted = lowercase (name);

    for (const auto& [plugin, kind] : flEffectTable)
        if (wanted == lowercase (plugin))
            return kind;

    return std::nullopt;
}

// FL's own spelling of a native effect's name (Patcher nodes keep old casings: "Fruity delay 2", REA_rodada14 §1.3)
std::string canonicalFlEffectName (const std::string& name)
{
    const auto wanted = lowercase (name);
    for (const auto& entry : flEffectTable)
        if (wanted == lowercase (entry.first))
            return entry.first;
    return name;
}

// Range of an FL parameter's raw value (what FL's parameter setter stores and what pattern events 0xDF / 0xD8 hold),
// so that (raw − lo) / (hi − lo) is FL's normalised value, the one automation clips carry (REA_rodada85 §2).
// `plugin` = the FL plugin of the target slot or channel. Unknown ranges: no value.
std::optional<std::pair<int, int>> flRawRange (flp::ControlId id, const std::string& plugin)
{
    const auto p = id.param();

    if (id.isChannel())
    {
        if (p == 0 || p == 1) // volume 0..12800 (10000 = default), pan 0..12800 (6400 = centre)
            return std::pair { 0, 12800 };
        // Levels adjustment (round 190dz, TPluginForm OfsLevelsPanel): pan offset ±6400, volume multiplier 0..25600,
        // Mod X / Y offsets ±256
        if (p == 0x10)
            return std::pair { -6400, 6400 };
        if (p == 0x11)
            return std::pair { 0, 25600 };
        if (p == 0x13 || p == 0x14)
            return std::pair { -256, 256 };
    }
    else if (id.isMixer())
    {
        if (p == flp::ControlId::mixerVolume)
            return std::pair { 0, 16000 };
        if (p == flp::ControlId::mixerPan)
            return std::pair { -6400, 6400 };
        if (p == 0x1F01) // slot dry/wet
            return std::pair { 0, 12800 };
    }

    if (p < flp::ControlId::pluginParamBase)
        return std::nullopt;

    // plugin parameters: the plugin's own control integer (FL hands it to ProcessParam unscaled)
    const auto k = p - flp::ControlId::pluginParamBase;
    if ((plugin == "Fruity Soundfont Player" || plugin == "SoundFont Player") && k < 13)
        return sf::flParamRange (k);
    if (plugin == "Fruity Reeverb 2" && k < reeverb2ParamCount)
        return std::pair { reeverb2::ranges[(std::size_t) k].lo, reeverb2::ranges[(std::size_t) k].hi };
    if (plugin == "Fruity Reeverb" && k < reeverb1ParamCount)
        return std::pair { 0, reeverb1::maxima[(std::size_t) k] };
    if (plugin == "Fruity Convolver" && k < convolverParamCount)
        return std::pair { convolver::ranges[(std::size_t) k].lo, convolver::ranges[(std::size_t) k].hi };
    if (plugin == "Fruity Scratcher" && k < scratcher::count) // the platter wheel 0…30000, Mute 0/1 (1 = muted), …
        return std::pair { scratcher::controlRange (k).lo, scratcher::controlRange (k).hi };
    if (plugin == "Transporter" && k < transporter::numControls) // FL's 22 controls, each over its own range
        return std::pair { transporter::rawMin[(std::size_t) k], transporter::rawMax[(std::size_t) k] };
    if (plugin == "Transmitter" && k < transmitter::count) // 0…10000 for the knobs, the switches / selectors raw
        return std::pair { transmitter::controlRange (k).lo, transmitter::controlRange (k).hi };
    if (plugin == "LuxeVerb" && k < luxeVerbControlCount)
        return std::pair { luxeverb::ranges[(std::size_t) k].lo, luxeverb::ranges[(std::size_t) k].hi };
    return std::nullopt;
}

AutomationTarget automationTargetFor (flp::ControlId id, const std::map<int, int>& channelMap, int mixerTracks)
{
    AutomationTarget t;

    if (id.isTempo())
    {
        t.kind = AutomationTargetKind::tempo;
        return t;
    }

    if (id.isChannel())
    {
        const auto channel = channelMap.find (id.channel());

        if (channel == channelMap.end())
            return t;

        t.index = channel->second;
        const auto p = id.param();

        if (p == 0)
            t.kind = AutomationTargetKind::channelVolume;
        else if (p == 1)
            t.kind = AutomationTargetKind::channelPan;
        else if (p >= 0x10 && p <= 0x14 && leveladjust::valid (p - 0x10)) // Levels adjustment (round 190dz)
        {
            t.kind = AutomationTargetKind::channelAdjust;
            t.param = p - 0x10;
        }
        else if (p >= flp::ControlId::pluginParamBase)
        {
            t.kind = AutomationTargetKind::channelParam;
            t.param = p - flp::ControlId::pluginParamBase;
        }

        return t;
    }

    if (id.isMixer() && id.track() >= 0 && id.track() < mixerTracks)
    {
        t.index = id.track();
        const auto p = id.param();

        if (p == 0x1F01) // slot dry/wet
        {
            t.kind = AutomationTargetKind::effectMix;
            t.slot = id.slot();
        }
        else if (p == flp::ControlId::mixerVolume)
            t.kind = AutomationTargetKind::mixerVolume;
        else if (p == flp::ControlId::mixerPan)
            t.kind = AutomationTargetKind::mixerPan;
        else if (p >= flp::ControlId::pluginParamBase)
        {
            t.kind = AutomationTargetKind::effectParam;
            t.slot = id.slot();
            t.param = p - flp::ControlId::pluginParamBase;
        }
    }

    return t;
}

bool synthFrom3xOsc (const std::vector<std::uint8_t>& state, SynthSettings& synth)
{
    // version 10/11: 20 parameters + 4 bools (80 B); 12+: 21 parameters + 4 bools (+ HQ)
    if (state.size() < 84)
        return false;

    auto i32 = [&] (std::size_t offset) {
        return (std::int32_t) ((std::uint32_t) state[offset] | ((std::uint32_t) state[offset + 1] << 8)
                               | ((std::uint32_t) state[offset + 2] << 16) | ((std::uint32_t) state[offset + 3] << 24));
    };

    const auto version = i32 (0);

    if (version < 10 || version > 64)
        return false;

    // parameter i is at 0x04 + 4i; (i + 1) = 7·osc + field, fields [mix, pan, shape, coarse, fine, spo, detune]
    auto param = [&] (int osc, int field) { return i32 ((std::size_t) (4 + 4 * (7 * osc + field - 1))); };
    const auto boolsAt = version >= 12 ? (std::size_t) 0x58 : (std::size_t) 0x54;
    const bool am = version >= 11 && state.size() > boolsAt + 3 && state[boolsAt + 3] != 0;

    // levels (§2.2): osc 3 takes mix3/128, osc 2 a share of the rest, osc 1 what remains
    const auto g3 = std::clamp (param (2, 0), 0, 128) / 128.0f;
    const auto rest = am ? 1.0f : 1.0f - g3;
    const auto g2 = rest * std::clamp (param (1, 0), 0, 128) / 128.0f;
    // with AM, osc 3 is not summed: its level becomes the modulation depth (REA_rodada6 §3)
    const std::array<float, 3> levels { std::max (0.0f, rest - g2), g2, g3 };
    synth.am = am;

    for (int o = 0; o < 3; ++o)
    {
        auto& osc = synth.osc[(std::size_t) o];

        switch (param (o, 2))
        {
            case 0: osc.wave = Waveform::sine; break;
            case 1: osc.wave = Waveform::triangle; break;
            case 2: osc.wave = Waveform::square; break;
            case 5: osc.wave = Waveform::noise; break;
            default: osc.wave = Waveform::saw; break; // 3 saw, 4 rounded saw, 6 custom without sample
        }

        const auto coarse = std::clamp (param (o, 3), -24, 24);
        osc.octave = coarse / 12; // truncates toward zero; the remainder becomes semitones
        osc.semitones = coarse - osc.octave * 12;
        // stereo detune only sounds on the right channel: half of it counts as average detune
        osc.detune = std::clamp ((float) std::clamp (param (o, 4), -100, 100) + std::clamp (param (o, 6), -50, 50) * 0.5f, -100.0f, 100.0f);
        osc.pan = std::clamp (param (o, 1), -64, 64) / 64.0f;
        osc.level = levels[(std::size_t) o];
    }

    return true;
}

namespace
{
// FL channel filter cutoff (Hz) for normalized Mod X `x` (REA_rodada4 §3); 20 kHz = open.
double channelFilterHz (int type, double x)
{
    constexpr double twoPi = 6.283185307179586;
    x = std::clamp (x, 0.0, 1.0);

    switch (type)
    {
        case 0:
            return x >= 1.0 ? 20000.0 : -44100.0 / twoPi * std::log (1.0 - std::min ((std::pow (601.0, x) - 1.0) / 600.0, 0.999));
        case 1:
        case 2:
        case 3:
        case 4:
        case 5: return ((std::pow (51.0, x) - 1.0) * 0.0596 + 0.02) * 44100.0 / twoPi;
        case 6:
        case 7: return 44100.0 / (twoPi / 2.0) * std::asin (std::min (1.0, (std::pow (26.0, x) - 1.0) * 0.04) / 2.0);
        default: return 20000.0;
    }
}
} // namespace

float filterEnvelopeOctaves (int modX, int type, int amount)
{
    const auto x = std::clamp (modX / 256.0, 0.0, 1.0);
    const auto from = std::clamp (channelFilterHz (type, x), 20.0, 20000.0);
    const auto to = std::clamp (channelFilterHz (type, x + amount / 128.0), 20.0, 20000.0);
    return (float) std::log2 (to / from);
}

void synthFilterFromChannel (int modX, int modY, int type, float& cutoffHz, float& resonance, int& mode)
{
    mode = type == 2 ? 1 : type == 3 ? 2 : type == 4 ? 3 : 0; // BP, HP, BS; the rest is low-pass
    const auto x = std::clamp (modX / 256.0, 0.0, 1.0);
    const auto r = std::clamp (modY / 256.0, 0.0, 1.0);
    auto fromQ = [] (double q) { return (float) std::clamp ((2.0 - 1.0 / q) / 1.9, 0.0, 1.0); }; // the synth SVF: k = 1/Q
    resonance = 0.0f;

    if (type == 0) // Fast LP: 1 pole + feedback; feedback grows with Mod Y (approximation)
        resonance = (float) (r * 0.9);
    else if (type >= 1 && type <= 5) // LP/BP/HP/BS biquads (LPx2 = the LP twice)
        resonance = fromQ (r <= 0.0 ? 0.1 : (std::pow (51.0, 0.625 * r) - 1.0) * 0.4 + 0.1);
    else if (type == 6 || type == 7) // Chamberlin SVF
        resonance = fromQ (1.0 / (r <= 0.0 ? 1.0 : (r < 1.0 ? 1.0 - 0.8 * r : 0.2)));

    cutoffHz = (float) std::clamp (channelFilterHz (type, x), 20.0, 20000.0);
}

// The 364-byte channel-settings snapshot Fruity DX10 keeps (REA_rodada42 §3): the five 0xDA records (pan, volume,
// cutoff, resonance, pitch; 68 bytes each) followed by Mod X at 0x15c, Mod Y at 0x160 and the filter type at 0x168. A
// version-2 state carries its own (after the 22 parameters); for a version-1 state FL's host hands the plugin the
// channel's records (the plugin asks for them when it has no snapshot — inference from the request, not observed).
// Missing records take the plugin's defaults (neutral; the volume record with flag 4 and release tension −101).
std::vector<std::uint8_t> dx10Snapshot (const flp::Channel& c)
{
    std::vector<std::uint8_t> s (364, 0);
    auto put = [&] (std::size_t at, std::int32_t v) { std::memcpy (s.data() + at, &v, 4); };
    for (int k = 0; k < 5; ++k)
    {
        const auto& e = c.envelopes[(std::size_t) k];
        const auto base = (std::size_t) (0x44 * k);
        const std::int32_t defaults[17] = { k == 1 ? 4 : 0, 0, 100, 20000, 20000, 30000, 50, 20000, 0, 100, 20000, 0, 32950, 0, 0, 0, k == 1 ? -101 : 0 };
        for (int i = 0; i < 17; ++i)
            put (base + 4 * (std::size_t) i, defaults[i]);
        if (e.present)
            std::memcpy (s.data() + base, e.raw.data(), (std::size_t) std::min (e.rawSize, 0x44));
    }
    put (0x15c, c.hasLevels ? c.modX : 256);
    put (0x160, c.hasLevels ? c.modY : 0);
    put (0x168, c.hasLevels ? c.filterType : 0);
    return s;
}

void applyFlStretchPanel (SamplerSettings& s, int mode, int pitchCents, int mul, float timeTicks, double sampleSeconds, double bpm)
{
    if (mode != 0)
    {
        s.keepPitch = true;
        s.stretchMethod = stretchMethodForFlMode (mode);
        s.flStretchMode = mode;
        s.flStretchPitch = std::clamp (pitchCents, -4800, 4800);
        s.pitch = std::clamp (s.pitch + s.flStretchPitch / 100.0f, -24.0f, 24.0f);
    }

    if (timeTicks <= 0.0f && mul != 0 && sampleSeconds > 0.0 && bpm > 0.0)
    {
        s.stretchBeats = std::clamp ((float) (sampleSeconds * std::pow (2.0, mul / 10000.0) * bpm / 60.0), 0.0f, 4096.0f);
        s.stretchMul = std::clamp (mul, -20000, 20000); // (round 190dh: the page shows TIME = the sample's beats, and MUL)
    }
}

ChannelDef drumPadChannel (const ChannelDef& original, const DrumPad& pad, int channel)
{
    {
        ChannelDef def = original;
        def.name = original.name + " - " + pad.name;
        def.instrument = InstrumentKind::sampler;
        def.plugin = {};
        const auto single = pad.layers.size() == 1;
        def.samplePath = single ? pad.layers.front().path : std::string();
        // the pad and layer gains ride on the zones (FPC's law goes above unity: the channel volume curve would clip
        // them), the channel keeps its own volume (round 170i2)
        def.zones = pad.layers;
        for (auto& z : def.zones)
        {
            z.rootKey = 60, z.lowKey = 0, z.highKey = 127; // the pad plays at the sample's pitch
            z.gain *= pad.gain;
        }
        def.pan = std::clamp (original.pan + pad.pan, -1.0f, 1.0f);
        def.sampler = {};
        def.sampler.pitch = std::clamp (original.sampler.pitch + pad.tune, -24.0f, 24.0f);
        def.sampler.flVelocityLaw = true; // FL's host hands FPC the velocity as Init.Vol (REA_rodada170i §7)
        // kit-specific cut groups (don't mix with other channels' groups)
        def.cutGroup = pad.cutGroup > 0 ? 1000 * (channel + 1) + pad.cutGroup : 0;
        def.cutBy = pad.cutBy > 0 ? 1000 * (channel + 1) + pad.cutBy : 0;
        def.mixerTrack = std::clamp (original.mixerTrack + pad.mixerOffset, 0, maxMixerTracks - 1);
        return def;
    }
}

void splitDrumChannel (Song& song, int channel, const std::vector<DrumPad>& pads)
{
    if (channel < 0 || channel >= (int) song.channels.size() || pads.empty())
        return;

    const auto original = song.channels[(std::size_t) channel];
    const auto inserted = (int) pads.size() - 1; // the first pad takes the original channel's place
    std::vector<ChannelDef> padChannels;

    for (const auto& pad : pads)
        padChannels.push_back (drumPadChannel (original, pad, channel));

    song.channels[(std::size_t) channel] = padChannels.front();
    song.channels.insert (song.channels.begin() + channel + 1, padChannels.begin() + 1, padChannels.end());

    // channels after the original shift by `inserted` positions
    auto shift = [&] (int c) { return c > channel ? c + inserted : c; };

    for (auto& p : song.patterns)
    {
        std::vector<Note> kept;

        for (auto n : p.notes)
        {
            if (n.channel != channel)
            {
                n.channel = shift (n.channel);
                kept.push_back (n);
                continue;
            }

            for (int i = 0; i < (int) pads.size(); ++i)
                if (pads[(std::size_t) i].key == n.key)
                {
                    n.channel = channel + i;
                    n.key = 60;
                    kept.push_back (n);
                    break;
                }
        }

        p.notes = std::move (kept);
        p.sortNotes();

        // the Channel Rack's loop points: the pads loop like the kit did
        std::vector<ChannelLoop> loops;
        for (auto l : p.channelLoops)
        {
            if (l.channel != channel)
            {
                l.channel = shift (l.channel);
                loops.push_back (l);
                continue;
            }
            for (int i = 0; i < (int) pads.size(); ++i)
                loops.push_back ({ channel + i, l.setting });
        }
        p.channelLoops = std::move (loops);
    }

    song.forEachClipList ([&] (std::vector<Clip>& clips) {
        for (auto& c : clips)
            c.channel = c.channel >= 0 ? shift (c.channel) : c.channel;
    });

    // Layer children (round 164): shifted; a Layer of the kit plays every pad
    for (auto& l : song.channels)
    {
        std::vector<int> kids;
        for (const auto k : l.layer.children)
        {
            kids.push_back (shift (k));
            if (k == channel)
                for (int i = 1; i <= inserted; ++i)
                    kids.push_back (channel + i);
        }
        l.layer.children = std::move (kids);
    }

    song.forEachTarget ([&] (AutomationTarget& t) {
        if ((t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan
             || t.kind == AutomationTargetKind::channelParam || t.kind == AutomationTargetKind::channelAdjust) && t.index >= 0)
            t.index = shift (t.index);
    });
}

bool isFlSynthPresetPlugin (const std::string& pluginName)
{
    static const char* const supported[] = { "3x osc", "sytrus", "harmor", "gms", "harmless", "minisynth", "fruity dx10", "dx10",
                                             "fruit kick", "ogun", "autogun", "sakura", "kepler", "boobass", "poizone", "bassdrum",
                                             "toxic biohazard" };
    const auto name = lowercase (pluginName);
    return std::any_of (std::begin (supported), std::end (supported), [&] (const char* n) { return name == n; });
}

std::optional<SynthSettings> synthFromFlPreset (const std::vector<std::uint8_t>& bytes)
{
    // native GMS preset (.gmsynth): it is the plugin state without the "SSLF"
    if (bytes.size() >= 4 && (std::memcmp (bytes.data(), "4nyS", 4) == 0 || std::memcmp (bytes.data(), "3nyS", 4) == 0))
    {
        std::vector<std::uint8_t> state { 'S', 'S', 'L', 'F' };
        const auto n = (std::uint32_t) bytes.size();
        for (int i = 0; i < 4; ++i)
            state.push_back ((std::uint8_t) (n >> (8 * i)));
        state.insert (state.end(), bytes.begin(), bytes.end());
        SynthSettings synth;
        float gain = 1.0f;
        if (synthFromGms (state, 120.0, synth, gain))
            return synth;
        return std::nullopt;
    }

    try
    {
        auto project = flp::parseProject (bytes, true);

        if (project.channels.empty() || ! isFlSynthPresetPlugin (project.channels.front().pluginName))
            return std::nullopt;

        project.channels.front().type = flp::ChannelType::native; // native generator (the .fst doesn't say)
        FlpImportOptions options;
        options.threeOscAsSynth = true; // a 3x Osc preset fills the built-in synth
        const auto song = importFlp (project, options);

        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::sakura)
        {
            // the synth's preset button keeps the synth approximation of a Sakura patch
            SynthSettings synth;
            if (synthFromSakura (project.channels.front().pluginState, synth))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::sytrus)
        {
            // likewise for Sytrus: the preset button fills the built-in synth with the 3-oscillator conversion
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromSytrus (project.channels.front().pluginState, song.bpm, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::harmless)
        {
            // ... and of a Harmless patch
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromHarmless (project.channels.front().pluginState, song.bpm, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && (song.channels.front().instrument == InstrumentKind::gms || song.channels.front().instrument == InstrumentKind::morphine))
        {
            // ... and of GMS / Morphine patches (their 3-oscillator / averaged-spectrum conversions)
            SynthSettings synth;
            float gain = 1.0f;
            const auto& st = project.channels.front().pluginState;
            if (song.channels.front().instrument == InstrumentKind::gms ? synthFromGms (st, song.bpm, synth, gain) : synthFromMorphine (st, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::toxic)
        {
            // ... and of a Toxic Biohazard patch (the 2-operator conversion)
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromToxic (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::harmor)
        {
            // ... and of a Harmor patch (the spectral-oscillator conversion)
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromHarmor (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::dx10)
        {
            // ... and of a Fruity DX10 patch (the 3-oscillator FM conversion)
            SynthSettings synth;
            if (synthFromDX10 (project.channels.front().pluginState, synth))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::kepler)
        {
            // ... and of a Kepler patch (the synth conversion)
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromKepler (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (! song.channels.empty()
            && (song.channels.front().instrument == InstrumentKind::ogun || song.channels.front().instrument == InstrumentKind::autogun))
        {
            // ... and of an Ogun / Autogun patch (the spectral-oscillator conversion)
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromOgun (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::poizone)
        {
            // ... and of a PoiZone patch (the 2-oscillator conversion)
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromPoiZone (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::minisynth)
        {
            // ... and of a MiniSynth patch (the internal-synth conversion)
            SynthSettings synth;
            if (synthFromMiniSynth (project.channels.front().pluginState, song.bpm, synth))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::fruitkick)
        {
            // ... and of Fruit Kick / BassDrum patches (their sine + pitch-sweep conversions)
            SynthSettings synth;
            if (synthFromFruitKick (project.channels.front().pluginState, synth))
                return synth;
        }
        if (! song.channels.empty() && song.channels.front().instrument == InstrumentKind::bassdrum)
        {
            SynthSettings synth;
            float gain = 1.0f;
            if (synthFromBassDrum (project.channels.front().pluginState, synth, gain))
                return synth;
        }
        if (song.channels.empty() || song.channels.front().instrument != InstrumentKind::synth)
            return std::nullopt;

        return song.channels.front().synth;
    }
    catch (const flp::Error&)
    {
        return std::nullopt;
    }
}

// Old FL saved its own Fruity effects as VST2 plugins inside Fruity Wrapper: the wrapper keeps their parameters as
// 0…1 floats (REA_rodada89 §2). FL Studio 2026 opens them with its native plugin; each float lands linearly on the
// native control's range (raw = lo + round(v · (hi − lo))), in the native control order. The native state is rebuilt
// here and read by the exact engines. Evidence [FL render]: FL's own render tests System/Render tests/*.flp hold the
// thirteen VST2 Fruity effects that FL 11 shipped; rebuilt this way, 12 of them null against FL's renders at the
// 16-bit floor (REA_rodada170i §1), Fruity Chorus within its random LFO start phase. Fruity Compressor: an untouched
// instance's floats land exactly on the native defaults [DATA, RawFL demo]; its lookahead flag has no VST2 parameter.
// The Free Filter and Fruity Filter VST2s have no CENTER parameter: FL's render matches only with CENTER on (the
// native plugin's default, which the VST2 parameters never touch) [FL render, REA_rodada170i §1].
std::vector<std::uint8_t> wrappedFruityState (const ForeignPlugin& plugin)
{
    std::vector<std::uint8_t> native;
    FlVst2State state;
    if (! parseFlVst2State (plugin.state, state))
        return native;
    const auto name = lowercase (plugin.name);
    auto put = [&native] (std::int32_t v) {
        for (int b = 0; b < 4; ++b)
            native.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * b)));
    };
    auto value = [&state] (std::size_t k, std::int32_t lo, std::int32_t hi) {
        const auto v = k < state.params.size() ? (double) std::clamp (state.params[k], 0.0f, 1.0f) : 0.0;
        return lo + (std::int32_t) std::lround (v * (double) (hi - lo));
    };
    // version, then one i32 per control over [lo, hi]
    auto controls = [&] (std::int32_t version, std::initializer_list<std::pair<std::int32_t, std::int32_t>> ranges) {
        if (state.params.size() < ranges.size())
            return false;
        if (version >= 0)
            put (version);
        std::size_t k = 0;
        for (const auto& r : ranges)
            put (value (k++, r.first, r.second));
        return true;
    };
    auto fromMax = [&] (std::int32_t version, const auto& rawMax) {
        if (state.params.size() < rawMax.size())
            return false;
        put (version);
        for (std::size_t k = 0; k < rawMax.size(); ++k)
            put (value (k, 0, rawMax[k]));
        return true;
    };

    // Fruity Phaser (REA_rodada105_phaser): the nine floats over the native ranges, except the stage count, which the
    // old plugin kept over 0…23 stages (FL's render test: an untouched instance saves 8/23 for the native 8 stages =
    // raw 7) [DATA; only the defaults were seen, the other values' mapping is not confirmed]
    if (name == "fruity phaser" && state.params.size() >= (std::size_t) phaserfx::params)
    {
        phaserfx::State s {};
        for (int k = 0; k < phaserfx::params; ++k)
        {
            const auto v = (double) std::clamp (state.params[(std::size_t) k], 0.0f, 1.0f);
            s[(std::size_t) k] = k == 5 ? std::clamp ((int) std::lround (v * 23.0) - 1, 0, phaserfx::rawMax[5])
                                        : (int) std::lround (v * phaserfx::rawMax[(std::size_t) k]);
        }
        return phaserfx::serialize (s);
    }
    bool ok = false;
    if (name == "fruity compressor")
    {
        if (state.params.size() >= (std::size_t) fldyn::compressorControls)
        {
            put (1);
            for (std::size_t k = 0; k < (std::size_t) fldyn::compressorControls; ++k)
                put (value (k, fldyn::compressorMin[k], fldyn::compressorMax[k]));
            ok = true;
        }
    }
    else if (name == "fruity balance")
        ok = controls (-1, { { -128, 128 }, { 0, 320 } }); // (Balance's state has no version)
    else if (name == "fruity phase inverter")
        ok = controls (1, { { 0, 1024 } });
    else if (name == "fruity center")
        ok = controls (1, { { 0, 1 } });
    else if (name == "fruity bass boost")
        ok = controls (1, { { 0, 1024 }, { 0, 1024 } });
    else if (name == "fruity mute 2")
        ok = controls (1, { { 0, 1 }, { 0, 2 } });
    else if (name == "fruity fast lp")
        ok = controls (1, { { 0, 1024 }, { 0, 1024 } });
    else if (name == "fruity free filter")
    {
        ok = controls (2, { { 100, 700 }, { 0, 1024 }, { 0, 1024 }, { 0, 1024 } });
        native.push_back (1); // CENTER on
    }
    else if (name == "fruity filter")
    {
        ok = controls (2, { { 0, 1024 }, { 0, 1024 }, { 0, 1024 }, { 0, 1024 }, { 0, 1024 }, { 0, 1 } });
        native.push_back (1); // CENTER on
    }
    else if (name == "fruity flanger")
        ok = fromMax (1, flangerfx::rawMax);
    else if (name == "fruity chorus")
        ok = fromMax (1, fruitychorusfx::rawMax);
    else if (name == "fruity reeverb")
        ok = fromMax (1, reeverb1::maxima);
    if (! ok)
        native.clear();
    return native;
}

// A Patcher node's parameter port p in FL's numbering → LYNX's parameter of that node (round 171b): a hosted plugin
// keeps its index, a built-in effect goes through effectParamFromFl; −1 = no LYNX equivalent
static int flPatcherParam (const PatchNode& node, const std::string& flPlugin, int p)
{
    if (p < 0)
        return -1;
    if (node.generator)
        return node.channel.instrument == InstrumentKind::plugin ? p : -1;
    if (node.effect.kind == EffectKind::plugin)
        return p;
    if (const auto map = effectParamFromFl (canonicalFlEffectName (flPlugin), p))
        return map->param;
    return -1;
}

Song importFlp (const flp::Project& input, const FlpImportOptions& options)
{
    // Patcher used as an instrument: the channel becomes the main generator inside it (REA_rodada14 §1)
    std::optional<flp::Project> unpacked;
    for (std::size_t i = 0; i < input.channels.size(); ++i)
        if (lowercase (input.channels[i].pluginName) == "patcher")
            if (const auto graph = parsePatcher (input.channels[i].pluginState); graph && graph->mainGenerator >= 0)
            {
                if (! unpacked)
                    unpacked = input;
                const auto& node = graph->nodes[(std::size_t) graph->mainGenerator];
                unpacked->channels[i].pluginName = node.plugin;
                unpacked->channels[i].pluginState = node.state;
            }
    const auto& project = unpacked ? *unpacked : input;

    Song song;
    song.bpm = project.tempo > 0.0 ? project.tempo : 140.0;
    song.performanceMode = project.performanceMode; // (round 165m)
    song.autoCrossfades = project.autoCrossfades;   // FL's Playlist "Automatic crossfades" (round 167a)
    song.beatsPerBar = std::clamp (project.beatsPerBar, 1, 32);
    song.swing = std::clamp (project.swing / 128.0f, 0.0f, 1.0f);
    song.playTruncatedNotes = project.playTruncatedNotes; // 0x1E (REA round 167g)
    song.muteRunsEffects = true;                          // FL's muted insert (round 190y)
    song.patternClipTakeover = true;                      // FL's pattern clip takeover (issue #158)
    song.fastDeclick = project.fastDeclick;               // 0x28 (REA round 167h)
    song.panTriangular = (project.panLaw >> 1) == 1;       // 0x17: FL saves 2 × its pan-law mode (REA round 167j §1)
    song.flPluginLevels = true; // hosted plugins at Fruity Wrapper's level and velocity law (REA_rodada190ad.md)
    // REA round 168j: PDC "Automatic" 0x1D / "Compensate automations" 0x27, Time settings 0x23, the Piano Roll's scale
    // highlighting 0xF3, the data folder 0xCA
    song.autoPdc = project.autoPdc;
    song.compensateAutomations = project.compensateAutomations;
    song.timeSignatureMode = project.timeSignatureMode;
    if (project.hasScaleHighlight)
        song.scaleHighlight = { project.scaleHighlight >= -3 && project.scaleHighlight < 256 ? project.scaleHighlight : -2,
                                std::clamp (project.scaleRoot, 0, 11) };
    song.dataFolder = project.dataFolder;
    song.masterPitch = (float) std::clamp (project.mainPitch, -1200, 1200); // FL's render follows it (REA round 167g §1)
    song.channelGroups = project.filterGroups;
    song.info.title = project.title;
    song.info.author = project.artist;
    song.info.genre = project.genre;
    song.info.url = project.url;
    song.info.comments = project.comments;
    std::replace (song.info.comments.begin(), song.info.comments.end(), '\r', '\n'); // (FL saves CR line breaks)
    song.info.showOnOpen = project.showInfo; // 0x0A, Project info › Show on opening (REA round 167g)
    // 0xED: TDateTime days since 1899-12-30 (25569 days before 1970-01-01), FL's local time kept as it reads
    song.info.created = flDateTimeToUnix (project.createdDays);
    song.info.workSeconds = std::min (project.workDays, 36500.0) * 86400.0;

    // FL channels may have sparse indices; we map them to dense indices.
    std::map<int, int> channelMap;
    std::vector<std::pair<int, std::vector<DrumPad>>> drumKits; // FPC channels to split at the end
    std::optional<std::string> drumSynthDll; // the user's DrumSynth Live engine DLL, once checked ("" = not usable)

    // a sample on this PC: where the project says, else re-rooted from another PC's folders or next to the project
    // (`sampleUser` names what uses the sample, for the missing-files list; `record` false for paths only probed)
    std::string sampleUser;
    auto locateSample = [&] (const std::string& flPath, bool record = true) {
        auto found = locateFlSample (flPath, options.roots, options.projectFolder, options.fileExists);
        const auto saved = resolveFlPath (flPath, options.roots);
        if (found.empty())
        {
            if (const auto chosen = relocatedPath (options.relocations, saved); ! chosen.empty() && options.fileExists && options.fileExists (chosen))
                return chosen;
            if (record && options.report != nullptr)
                addMissingFile (options.report->missingFiles, saved, sampleUser);
        }
        else if (found != saved && options.report != nullptr)
        {
            ++options.report->relocatedSamples;
            if (record && std::none_of (options.report->relocatedFiles.begin(), options.report->relocatedFiles.end(),
                                        [&] (const auto& r) { return r.first == saved; }))
                options.report->relocatedFiles.push_back ({ saved, found });
        }
        return found;
    };

    // old presets keep a plugin's sample relative to FL's Data folder ("\\Patches\\…"). A path that already exists is
    // kept: on macOS / Linux the resolved path itself starts with '/' (round 170i: Granulizer and 3x Osc lost their
    // sample there)
    auto dataRelativeSample = [&options] (const std::string& path) {
        if (path.empty() || (path[0] != '\\' && path[0] != '/') || options.roots.factoryData.empty()
            || (options.fileExists && (options.fileExists (path) || options.fileExists (path + ".wav"))))
            return path;
        auto inData = resolveFlPath ("%FLStudioFactoryData%\\Data" + path, options.roots);
        return inData.empty() ? path : inData;
    };

    for (const auto& c : project.channels)
    {
        // automation clips become Song::automations (further below), not channels
        if (c.type == flp::ChannelType::automationClip)
            continue;

        channelMap[c.index] = (int) song.channels.size();

        ChannelDef def;
        def.name = c.name.empty() ? "Channel " + std::to_string (c.index + 1) : c.name;
        sampleUser = (c.type == flp::ChannelType::audioClip ? "Audio clip: " : "") + def.name;
        def.instrument = guessInstrument (def.name, c.type);
        // FL's Layer channel (REA_rodada164a §1): its children (FL's channel indices, mapped below) and options
        if (c.type == flp::ChannelType::layer)
        {
            def.instrument = InstrumentKind::layer;
            def.layer.children = c.layerChildren;
            applyFlLayerFlags (c.layerFlags, def.layer);
            def.layer.fade = std::clamp ((float) c.layerFade / 256.0f, 0.0f, 1.0f);
        }
        // every channel's key region (REA_rodada164a §2): FL's 0…256 range is every key
        if (c.keyRegionLow > 0 || c.keyRegionHigh < 256)
        {
            def.keyLow = std::clamp (c.keyRegionLow, 0, 127);
            def.keyHigh = std::clamp (c.keyRegionHigh, def.keyLow, 127);
        }

        // FL synthesizer that becomes the internal synth: a preset matching the name (in FLEX, the name
        // of the preset chosen in the plugin says more than the channel's)
        if (def.instrument == InstrumentKind::synth)
        {
            def.synth = presetForChannelName (def.name);
            if (lowercase (c.pluginName) == "flex")
                if (const auto names = flexPresetName (c.pluginState))
                    if (auto byPreset = presetForChannelName (names->second + " " + names->first); byPreset != SynthSettings {})
                        def.synth = byPreset;
        }

        const auto isAudioClip = c.type == flp::ChannelType::audioClip;

        if ((c.type == flp::ChannelType::sampler || isAudioClip) && ! c.samplePath.empty() && options.fileExists)
        {
            const auto path = locateSample (c.samplePath);

            if (! path.empty())
            {
                def.instrument = isAudioClip ? InstrumentKind::audioClip : InstrumentKind::sampler;
                def.samplePath = path;
            }
        }

        if (isAudioClip && def.instrument != InstrumentKind::audioClip)
            def.instrument = InstrumentKind::audioClip; // no file: stays silent, but the clip remains in the playlist

        def.mixerTrack = std::clamp (c.mixerTrack, 0, maxMixerTracks - 1);
        def.muted = ! c.enabled; // switched off in FL's Channel Rack: its notes don't play (REA_rodada80 §3.1)
        def.soloLock = c.soloLock;          // 0x20 "Lock active/mute state" (REA_rodada167l §5)
        def.ghostHidden = ! c.ghostShown;   // 0x32: the channel's ghost notes in the Piano Roll (REA_rodada167l §5)
        def.icon = std::clamp (c.icon, 0, 0xFFFF); // 0x9B (REA round 168j)
        def.flWrapperFlags = c.wrapperFlags;

        if (c.colour != 0)
            def.colour = 0xFF000000u | c.colour;

        if (c.filterGroup >= 0 && c.filterGroup < (int) project.filterGroups.size())
            def.group = c.filterGroup;

        if (c.hasLevels)
        {
            def.volume = std::clamp (c.volume / 12800.0f, 0.0f, 1.0f); // same curve (channelGain)
            def.pan = std::clamp ((c.pan - 6400) / 6400.0f, -1.0f, 1.0f);
            def.sampler.pitch = std::clamp (c.pitchCents / 100.0f, -24.0f, 24.0f);
        }

        // level adjustments (0xE5): FL multiplies the channel volume by the adjustment through the same curve
        // (gain = f(volume)·f(volAdj), REA_rodada3 §2 [DECOMP]; an FL render of one channel with volAdj 7286 vs 12800
        // differs by −10.4 dB, f() gives −10.7 dB, a linear factor −4.9 dB) and adds the pan adjustment to the pan.
        // Since round 190dz they are the channel's own Levels adjustment (Misc tab), no longer folded into its knobs: the
        // knobs read as in FL and Automate's clips (channel parameters 0x10 / 0x11) move them.
        if (c.hasLevelAdjust)
        {
            auto& l = def.misc.levels;
            l.pan = std::clamp (c.panAdj / 6400.0f, -1.0f, 1.0f);
            l.volume = std::clamp (c.volAdj / 12800.0f, 0.0f, 2.0f);
            l.modX = std::clamp (c.modXAdj / 256.0f, -1.0f, 1.0f);
            l.modY = std::clamp (c.modYAdj / 256.0f, -1.0f, 1.0f);
        }

        // sample root note: our sampler plays at the original pitch at C5 (60)
        if (c.rootNote >= 0 && c.rootNote <= 127)
            def.sampler.pitch = std::clamp (def.sampler.pitch + (float) (60 - c.rootNote), -24.0f, 24.0f);

        // an FL audio clip plays like a C5 note of its channel: the header pitch, the root note and the fine tune
        // transpose it too [FL: measured, REA_rodada164e-clippitch §2 — round 163 had dropped them on an assumption]

        def.swingMix = std::clamp (c.swingMix / 128.0f, 0.0f, 1.0f);
        def.cutGroup = c.cutGroup;
        def.cutBy = c.cutBy;

        // "cut itself" (group == cut by) and mono channel become the sampler's "cut"
        if ((c.cutGroup != 0 && c.cutGroup == c.cutBy) || c.maxVoices == 1)
            def.sampler.cut = true;

        def.synth.mono = c.monoFlag || c.maxVoices == 1;
        def.maxPolyphony = std::clamp (c.maxVoices, 0, 128); // FL's Polyphony › Max (0 = unlimited)

        // channel porta and slide time: n = round((27^(v/1024) - 1) * 48/26) ticks of 1/192 beat
        // (REA_rodada6 §4); the time also applies to notes with their own porta
        {
            const auto ticks = std::round ((std::pow (27.0, std::clamp (c.slideTime, 0, 1660) / 1024.0) - 1.0) * 48.0 / 26.0);
            def.synth.portaBeats = def.sampler.portaBeats = (float) std::max (1.0, ticks) / 192.0f;
            def.synth.porta = def.sampler.porta = c.portaFlag;
            // FL's channel layer plays Mono / Porta for every generator (round 168l): 0xDD's flags as they are (a Max of
            // 1 is not Mono: FL's limiter releases the older voice, VoiceLimit.h)
            def.misc.mono = c.monoFlag;
            def.misc.porta = c.portaFlag;
            def.misc.portaBeats = def.synth.portaBeats;
        }

        // the Misc tab (REA round 146b [EXEC: FL renders and saves]): time shift / gate, echo delay, tracking, main
        // pitch, pitch range, the arpeggiator
        {
            auto& m = def.misc;
            // FL rounds the shift to the project's ticks
            const auto filePpq = project.header.ppq > 0 ? project.header.ppq : 96;
            m.shiftBeats = c.timeShift > 0 ? (float) (std::nearbyint (channelmisc::flTimeKnobBeats (c.timeShift) * filePpq) / filePpq) : 0.0f;
            m.gateBeats = c.timeGate < 1446 ? (float) std::max (1.0 / filePpq, std::nearbyint (channelmisc::flTimeKnobBeats (c.timeGate) * filePpq) / filePpq) : 0.0f;
            m.fullPorta = c.fullPorta;
            m.echo.feedback = std::clamp (c.echoFeedback / 12800.0f, 0.0f, 2.0f);
            m.echo.pan = std::clamp ((c.echoPan - 6400) / 6400.0f, -1.0f, 1.0f);
            m.echo.pitch = (float) std::clamp (c.echoPitch, -4800, 4800);
            m.echo.echoes = std::clamp (c.echoes, 0, 16);
            m.echo.timeBeats = std::clamp (c.echoTime / 192.0f, 1.0f / 192.0f, 16.0f);
            m.echo.modX = std::clamp ((c.echoModX - 128) / 128.0f, -1.0f, 1.0f);
            m.echo.modY = std::clamp ((c.echoModY - 128) / 128.0f, -1.0f, 1.0f);
            m.echo.pingPong = (c.delayFlags & 2) != 0;
            m.echo.fat = (c.delayFlags & 4) != 0;
            for (int k = 0; k < 2; ++k)
                if (const auto& t = c.tracking[(std::size_t) k]; t.present)
                {
                    auto& to = k == 0 ? m.velocity : m.key;
                    to.mid = t.mid;
                    to.pan = std::clamp (t.pan / 64.0f, -1.0f, 1.0f);
                    to.modX = std::clamp (t.modX / 256.0f, -1.0f, 1.0f);
                    to.modY = std::clamp (t.modY / 256.0f, -1.0f, 1.0f);
                }
            m.pitchRange = std::clamp (c.pitchRange, 0, 48);
            m.mainPitch = c.mainPitch;
            // the root note keyboard and Add to key (round 149); a sampler's root is folded into its pitch above
            if (c.rootNote >= 0 && c.rootNote <= 127)
                m.rootNote = c.rootNote;
            m.addToKey = c.addToKey;
            m.fineTune = (float) std::clamp (c.fineTune, -100, 100);

            // arpeggiator: FL's directions 1 up, 2 down, 3 up-down bounce, 4 up-down sticky, 5 random
            static constexpr int directions[] = { 0, 1, 2, 3, 5, 4 };
            if (c.arpDirection >= 1 && c.arpDirection <= 5)
            {
                auto& a = def.arp;
                a.direction = directions[c.arpDirection];
                a.range = std::clamp (c.arpRange, 1, 4);
                a.stepBeats = (float) std::clamp (channelmisc::flTimeKnobBeats (c.arpTime), 1.0 / 192.0, 4.0);
                a.gate = std::clamp (c.arpGate / 48.0f, 0.05f, 128.0f / 48.0f);
                a.slide = c.arpSlide;
                a.octaveRepeat = std::clamp (c.arpRepeat, 0, 16); // 0: FL's chord at once (round 149)
                a.chordSustain = c.arpChord != -2;
                if (c.arpChord == 0)
                    a.chord = { 0 }; // "None": the root, stepping by octaves
                else if (c.arpChord > 0 && options.readFile && ! options.roots.factoryData.empty())
                {
                    const auto map = options.readFile (options.roots.factoryData + "\\System\\Config\\Chords.map");
                    a.chord = channelmisc::chordFromFlMap (std::string (map.begin(), map.end()), c.arpChord);
                }
            }
        }

        // volume envelope (record 1 of 0xDA) -> sampler ADSR
        const auto& volumeEnvelope = c.envelopes[1];
        const auto hasVolumeEnvelope = volumeEnvelope.present && volumeEnvelope.enabled;

        if (hasVolumeEnvelope)
        {
            const auto& env = volumeEnvelope;
            const auto beats = (env.flags & 1) != 0; // tempo-synced: units in beats
            auto seconds = [&] (int v) {
                const auto t = flp::Channel::envelopeTime (v);
                return (float) (beats ? t * 2.0 * 60.0 / song.bpm : t); // 0.128 beat instead of 0.064 s
            };

            def.sampler.attack = std::clamp (seconds (env.attack), 0.0f, 2.0f);
            def.sampler.decay = std::clamp (seconds (env.decay), 0.0f, 10.0f);
            def.sampler.sustain = std::clamp (env.sustain / 128.0f, 0.0f, 1.0f);
            def.sampler.release = std::clamp (std::max (seconds (env.release), 0.002f), 0.0f, 4.0f); // envelope on: the note ends

            // FL's own articulator (delay, hold, tensions, tick steps; REA_rodada69 §3) from the record as stored
            if (env.rawSize >= 68 && c.type == flp::ChannelType::sampler)
            {
                def.sampler.flVolumeEnvelope.resize (18);
                std::memcpy (def.sampler.flVolumeEnvelope.data(), env.raw.data(), 68);
                def.sampler.flVolumeEnvelope[17] = project.header.ppq > 0 ? project.header.ppq : 96;
            }
        }

        // FL's whole native sampler voice (REA_rodada76): the five articulator records as stored (a missing one keeps
        // the plugins' default record), Mod X/Y, filter type, PPQ
        if (c.type == flp::ChannelType::sampler && def.instrument == InstrumentKind::sampler)
        {
            const auto defaults = flvp::defaultSnapshot();
            def.sampler.flChannel.assign (90, 0);
            for (int k = 0; k < 5; ++k)
            {
                const auto& env = c.envelopes[(std::size_t) k];
                const auto* src = env.rawSize >= 68 ? env.raw.data() : defaults.data() + 0x44 * k;
                std::memcpy (def.sampler.flChannel.data() + 17 * k, src, 68);
            }
            def.sampler.flChannel[85] = c.modX;
            def.sampler.flChannel[86] = c.modY;
            def.sampler.flChannel[87] = c.filterType;
            def.sampler.flChannel[88] = project.header.ppq > 0 ? project.header.ppq : 96;
            def.sampler.flChannel[89] = project.panLaw;
        }

        // channel filter (Mod X/Y + type from 0xDB) applies to the sampler too
        synthFilterFromChannel (c.modX, c.modY, c.filterType, def.sampler.cutoff, def.sampler.resonance, def.sampler.filterType);

        // channel cutoff (record 2) and pitch (record 4) envelopes
        auto modEnvelope = [&] (const flp::Channel::Envelope& env, float amount) {
            ModEnvelope m;
            if (! env.present || ! env.enabled || amount == 0.0f)
                return m;

            const auto beats = (env.flags & 1) != 0;
            auto seconds = [&] (int v) {
                const auto t = flp::Channel::envelopeTime (v);
                return (float) std::clamp (beats ? t * 2.0 * 60.0 / song.bpm : t, 0.0, 20.0);
            };
            m.amount = amount;
            m.attack = seconds (env.attack);
            m.hold = seconds (env.hold);
            m.decay = seconds (env.decay);
            m.sustain = std::clamp (env.sustain / 128.0f, 0.0f, 1.0f);
            m.release = seconds (env.release);
            return m;
        };

        const auto filterEnv = modEnvelope (c.envelopes[2], filterEnvelopeOctaves (c.modX, c.filterType, c.envelopes[2].amount));
        const auto pitchEnv = modEnvelope (c.envelopes[4], std::clamp (c.envelopes[4].amount, -2400, 2400) / 100.0f);
        def.sampler.filterEnv = filterEnv;
        def.sampler.pitchEnv = pitchEnv;

        // channel LFOs (records 0 pan, 1 volume, 2 cutoff, 4 pitch; REA_rodada6 §2)
        {
            auto lfoFrom = [&] (const flp::Channel::Envelope& env, float amount) {
                ChannelLfo l;
                if (! env.present || env.lfoAmount == 0 || amount == 0.0f)
                    return l;

                l.sync = (env.flags & 0x02) != 0;
                auto time = [&] (int v) { // seconds, or beats when synced (double the scale)
                    const auto t = flp::Channel::envelopeTime (v);
                    return (float) std::clamp (l.sync ? t * 2.0 : t, 0.0, 64.0);
                };
                l.amount = amount;
                l.period = std::max (0.01f, time (env.lfoSpeed));
                l.shape = std::clamp (env.lfoShape, 0, 2);
                l.delay = time (env.lfoDelay);
                l.attack = time (env.lfoAttack);
                l.global = (env.flags & 0x20) != 0;
                return l;
            };

            std::array<ChannelLfo, lfoTargets> lfo {};
            lfo[lfoPan] = lfoFrom (c.envelopes[0], std::clamp (c.envelopes[0].lfoAmount, -64, 64) / 64.0f);
            lfo[lfoVolume] = lfoFrom (c.envelopes[1], std::clamp (c.envelopes[1].lfoAmount, -128, 128) / 128.0f);
            lfo[lfoCutoff] = lfoFrom (c.envelopes[2], filterEnvelopeOctaves (c.modX, c.filterType, c.envelopes[2].lfoAmount));
            lfo[lfoPitch] = lfoFrom (c.envelopes[4], std::clamp (c.envelopes[4].lfoAmount, -2400, 2400) / 100.0f);
            def.sampler.lfo = lfo;
            def.synth.lfo = lfo;
        }

        // third-party plugin (VST/CLAP) in Fruity Wrapper: the app looks for the same installed plugin;
        // until then (or if not found) the channel stays on the internal synth
        // FL generators with samples (REA_rodada9): DirectWave becomes a multi-sample sampler; FPC is
        // split into one channel per pad at the end of the import (notes go to the pad by key)
        auto existing = [&] (std::string path) { return locateSample (path); };
        auto installed = [&] (std::string path) { return locateSample (path, false); }; // FL's own data (FL Keys wavetables)

        // DirectWave: the exact engine (REA_rodada72); the multi-sample sampler stays as the fallback
        bool directWaveExact = false;
        if (c.pluginName == "DirectWave")
            directWaveExact = dw::importChannel (def, c.pluginState, existing, options.readFile, options.saveEmbedded);
        if (c.pluginName == "DirectWave" && ! directWaveExact)
            if (auto program = directWaveProgram (c.pluginState))
            {
                std::vector<SampleZone> zones;
                for (std::size_t i = 0; i < program->zones.size(); ++i)
                {
                    auto z = program->zones[i];
                    auto path = existing (z.path);
                    // file missing on this machine: use the audio the project stored with it
                    if (path.empty() && i < program->embeddedWav.size() && ! program->embeddedWav[i].empty() && options.saveEmbedded)
                        path = options.saveEmbedded (program->embeddedWav[i], def.name + " " + std::to_string (i + 1));
                    if (! path.empty())
                    {
                        z.path = path;
                        zones.push_back (std::move (z));
                    }
                }

                if (! zones.empty())
                {
                    def.instrument = InstrumentKind::sampler;
                    def.zones = std::move (zones);
                    def.sampler.attack = std::clamp (program->attack, 0.0f, 2.0f);
                    def.sampler.decay = std::clamp (program->decay, 0.0f, 10.0f);
                    def.sampler.sustain = program->sustain;
                    def.sampler.release = std::clamp (std::max (program->release, 0.01f), 0.0f, 4.0f);
                }
            }

        // Slicex: its own engine on FL's state (round 190l, core/SlicexInstrument.h); the sampler on the slices below
        // stays as the fallback
        const auto slicexExact = lowercase (c.pluginName) == "slicex" && slicex::importChannel (def, c.pluginState, existing, options.saveEmbedded);

        // Fruity Slicer / Slicex: sampler with the slices (slice i plays on key 60 + i, at the sample's pitch)
        {
            const auto name = lowercase (c.pluginName);
            std::optional<SlicedSample> sliced;

            if (name == "fruity slicer")
                sliced = fruitySlicer (c.pluginState);
            else if (name == "slicex" && ! slicexExact)
                sliced = slicexSample (c.pluginState);

            if (sliced)
            {
                std::string path;

                if (! sliced->embeddedWav.empty() && options.saveEmbedded) // audio that only exists inside the project
                    path = options.saveEmbedded (sliced->embeddedWav, def.name);
                if (path.empty())
                    path = existing (sliced->path);

                if (! path.empty())
                {
                    auto positions = sliced->positions;
                    std::int64_t frames = 0;

                    if (positions.empty()) // Slicex: the slices are the WAV "cue" points
                    {
                        const auto bytes = ! sliced->embeddedWav.empty() ? sliced->embeddedWav
                                                                         : options.readFile ? options.readFile (path) : std::vector<std::uint8_t> {};
                        positions = wavCuePoints (bytes, frames);
                    }

                    if (frames <= 0 && options.sampleFrames)
                        frames = options.sampleFrames (path);

                    // Fruity Slicer: its own generator on FL's state (round 180c); Slicex: the sampler
                    def.instrument = name == "fruity slicer" && fslicer::parse (c.pluginState) ? InstrumentKind::fruityslicer
                                                                                                : InstrumentKind::sampler;
                    if (def.instrument == InstrumentKind::fruityslicer)
                        def.pluginState = c.pluginState;
                    def.samplePath = path;
                    if (def.instrument != InstrumentKind::fruityslicer) // (Fruity Slicer: its state's PS, at the rack)
                        def.sampler.pitch = std::clamp (def.sampler.pitch + sliced->pitchCents / 100.0f, -24.0f, 24.0f);
                    // Slicex: FL's host hands the plugin Init.Vol (REA_rodada170i §6; Fruity Slicer's render test is
                    // ruled by its licensed stretch and does not show it, so it keeps the linear velocity)
                    def.sampler.flVelocityLaw = name == "slicex";

                    // Slicex's master volume (REA_rodada69 §5): a single full-range zone carries the gain
                    if (sliced->gain != 1.0f && ! path.empty())
                    {
                        SampleZone zone;
                        zone.path = path;
                        zone.gain = sliced->gain;
                        def.zones = { zone };
                    }

                    if (frames > 0)
                        for (auto p : positions)
                            if (p >= 0 && p < frames)
                                def.sampler.slices.push_back ((float) ((double) p / (double) frames));

                }
            }
        }

        // FL Keys: FL's engine on the wavetable (.wti) that ships with FL (REA_rodada69 §8); the zoned sampler is the
        // fallback when the wavetable cannot be read
        bool flKeysExact = false;
        if (c.pluginName == "FL Keys" && options.readFile && ! options.roots.factoryData.empty())
            flKeysExact = flkeys::importChannel (def, c.pluginState, options.roots.factoryData + "\\Plugins\\Fruity\\Generators\\FL Keys\\Wavtables",
                                                 options.readFile, installed, options.saveEmbedded);
        if (c.pluginName == "FL Keys" && ! flKeysExact && options.readFile && ! options.roots.factoryData.empty())
            if (const auto set = flKeysSet (c.pluginState))
            {
                const auto folder = options.roots.factoryData + "\\Plugins\\Fruity\\Generators\\FL Keys\\Wavtables";
                const auto wtiBytes = options.readFile (folder + "\\" + *set + ".wti");
                const std::string wti (wtiBytes.begin(), wtiBytes.end());
                std::vector<std::uint8_t> rawData;

                if (wti.find ("filetype=raw") != std::string::npos)
                    if (const auto file = wti.find ("samplefile="); file != std::string::npos)
                    {
                        auto name = wti.substr (file + 11, wti.find_first_of ("\r\n", file) - file - 11);
                        rawData = options.readFile (folder + "\\" + name);
                    }

                std::vector<SampleZone> zones;
                for (auto z : flKeysZones (wti, folder, rawData, options.saveEmbedded))
                    if (z.path = installed (z.path); ! z.path.empty())
                        zones.push_back (std::move (z));

                if (! zones.empty())
                {
                    def.instrument = InstrumentKind::sampler;
                    def.zones = std::move (zones);
                    def.sampler.release = 0.4f; // the piano releases the note with a short release
                }
            }

        // Wave Traveller: FL's engine (REA_rodada167f) on FL's state, with the sample its state names (FL loads that
        // one, not the channel's). Without the sample the channel stays silent, as in FL (the plugin refuses notes)
        if (c.pluginName == "Wave Traveller")
        {
            wavetrav::State wt;
            if (wavetrav::parseState (c.pluginState, wt))
            {
                def.instrument = InstrumentKind::wavetraveller;
                def.pluginState = c.pluginState;
                def.samplePath = wt.samplePath.empty() ? std::string() : existing (wt.samplePath);
            }
        }

        // Fruity Envelope Controller: "controller" channel (silent) that triggers the envelopes from notes
        if ((c.pluginName == "Fruity Envelope Controller" && parseEnvelopeController (c.pluginState))
            || (c.pluginName == "Fruity Keyboard Controller" && parseKeyboardController (c.pluginState)))
        {
            def.instrument = InstrumentKind::controller;
            def.controllerState = c.pluginState;
        }

        // Fruity DrumSynth Live: its engine with all 120 patches (REA_rodada30 §1); the recorded waveforms some patches
        // use are read at run time from the user's own engine DLL (without it they are silent, the synthesis is not).
        // Fallback when the state can't be parsed: the FL drum sample with the same name as the editor's sound
        if (c.pluginName == "Fruity DrumSynth Live")
        {
            if (auto params = drumSynthLiveParams (c.pluginState))
            {
                def.instrument = InstrumentKind::drumsynth;
                def.modelParams = std::move (*params);
                if (! drumSynthDll)
                {
                    drumSynthDll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Fruity DrumSynth Live\\drumsynth_eng_x64.dll",
                                                  options.roots);
                    if (drumSynthDll->empty() || ! options.fileExists || ! options.fileExists (*drumSynthDll) || ! drumSynthDllUsable (*drumSynthDll))
                        drumSynthDll = std::string();
                }
                def.samplePath = *drumSynthDll;
            }
            else if (const auto sound = drumSynthLiveSound (c.pluginState))
                for (const auto& candidate : drumSampleCandidates (*sound))
                    if (auto path = locateSample (candidate, false); ! path.empty())
                    {
                        def.instrument = InstrumentKind::sampler;
                        def.samplePath = std::move (path);
                        break;
                    }
        }

        // (a kit without a pad that plays, such as FL's "Empty" kit, still becomes an FPC channel when one is asked
        // for: a kit preset loaded onto an FPC channel)
        if (c.pluginName == "FPC")
            if (auto pads = fpcPads (c.pluginState); pads || options.fpcAsChannel)
            {
                std::vector<DrumPad> found;
                for (auto pad : pads ? *pads : std::vector<DrumPad>())
                {
                    std::vector<SampleZone> layers;
                    for (auto layer : pad.layers)
                        if (layer.path = existing (layer.path); ! layer.path.empty())
                            layers.push_back (std::move (layer));
                    pad.layers = std::move (layers);
                    if (! pad.layers.empty())
                        found.push_back (std::move (pad));
                }

                // One FPC channel (round 190q) when it plays what the pad channels play: every pad on the channel's own
                // mixer insert (a pad with an output offset needs its own insert: those kits keep a channel per pad)
                const auto oneInsert = std::all_of (found.begin(), found.end(), [] (const DrumPad& pad) { return pad.mixerOffset == 0; });
                const auto kit = fpc::read (c.pluginState);
                if (kit && ! options.fpcAsPads && (options.fpcAsChannel || (! found.empty() && oneInsert)))
                {
                    def.instrument = InstrumentKind::fpc;
                    def.plugin = {};
                    def.samplePath.clear();
                    setFpcKit (def, *kit, existing);
                    def.pluginState = c.pluginState; // (FL's own bytes)
                }
                else if (! found.empty())
                    drumKits.push_back ({ (int) song.channels.size(), std::move (found) });
            }

        if (c.pluginName == "Fruity Wrapper" && parseFlWrapper (c.pluginState, def.plugin.foreign))
        {
            def.plugin.name = def.plugin.foreign.name;
            applyFlWrapperOptions (def.plugin.foreign, def.plugin);
            def.plugin.midiInPort = def.plugin.foreign.midiInPort; // the MIDI Out channels on that port play it
            def.plugin.midiOutPort = def.plugin.foreign.midiOutPort; // its notes play the plugins on that port
            // the plugin window's "Detached" (0xD4 flag 0x4, issue #116 [EXEC + DATA: REA_rodada190i.md])
            def.plugin.detached = flDetached (c.wrapperFlags);
        }

        // MIDI Out (REA_rodada16 §1): a silent channel whose notes the renderer sends to the plugins on its port
        if (lowercase (c.pluginName) == "midi out" && c.pluginState.size() >= 28)
        {
            def.instrument = InstrumentKind::midiOut;
            def.pluginState = c.pluginState;
        }
        // Voltage Controller (REA_rodada164a §6): its notes as control voltages; FL's state kept as its settings
        if (c.pluginName == "Fruity Voltage Controller" && voltage::parse (c.pluginState))
        {
            def.instrument = InstrumentKind::voltage;
            def.pluginState = c.pluginState;
        }

        // Dashboard (REA_rodada164f-flimport): its value controls as LYNX's Dashboard panel (FL's state kept, for its
        // controller links below)
        if (c.pluginName == "Dashboard")
            if (auto dash = surface::parseFlDashboard (c.pluginState))
            {
                def.instrument = InstrumentKind::dashboard;
                def.patcher.surface = std::move (dash->controls);
                def.pluginState = c.pluginState;
            }

        // FL's VFX plugins (round 164b): note processors, with FL's state as it is
        if (const auto kind = vfx::kindOfFlName (c.pluginName))
        {
            def.instrument = InstrumentKind::vfx;
            vfx::State vs;
            vs.kind = *kind;
            vs.fl = c.pluginState.empty() ? vfx::defaultState (*kind) : c.pluginState;
            def.pluginState = vfx::writeState (vs);
        }

        // Fruit Kick, MiniSynth, ... (REA_rodada10): approximated by the internal synth; the channel LFOs are kept
        bool adoptedFromPlugin = false;
        if (def.instrument == InstrumentKind::synth)
        {
            const auto name = lowercase (c.pluginName);
            SynthSettings fromPlugin;
            float pluginGain = 1.0f;
            auto adopt = [&] {
                for (int i = 0; i < lfoTargets; ++i)
                    if (def.synth.lfo[(std::size_t) i].amount != 0.0f)
                        fromPlugin.lfo[(std::size_t) i] = def.synth.lfo[(std::size_t) i];
                def.synth = fromPlugin;
                adoptedFromPlugin = true;
                const auto g = channelGain (def.volume) * pluginGain;
                def.volume = std::clamp ((float) (std::log (10.0 * g + 1.0) / std::log (11.0)), 0.0f, 1.0f);
            };

            if (name == "3x osc" && ! options.threeOscAsSynth && threeOscStateUsable (c.pluginState))
            {
                // FL's 3x Osc engine (REA_rodada47) with the channel settings its voice processor keeps (the channel's
                // 0xDA/0xDB records, which FL restores after the plugin state) and the project's PPQ for the envelope
                // ticks; the channel's sample (if any) is the custom wave of shape 6. The internal-synth conversion
                // stays as the fallback (state unreadable)
                def.instrument = InstrumentKind::threeosc;
                const auto fromChannel = c.hasLevels || std::any_of (c.envelopes.begin(), c.envelopes.end(), [] (const auto& e) { return e.present; });
                def.pluginState = threeOscNormalisedState (c.pluginState, fromChannel ? dx10Snapshot (c) : std::vector<std::uint8_t> {});
                def.modelParams = { (float) (project.header.ppq > 0 ? project.header.ppq : 96) };
                if (! c.samplePath.empty() && options.fileExists)
                {
                    auto path = resolveFlPath (c.samplePath, options.roots);
                    path = dataRelativeSample (path);
                    if (! path.empty() && ! options.fileExists (path) && options.fileExists (path + ".wav"))
                        path += ".wav";
                    if (! path.empty() && options.fileExists (path))
                        def.samplePath = path;
                }
                adoptedFromPlugin = true;
            }
            else if (name == "fruity granulizer" && granulizerStateUsable (c.pluginState))
            {
                // FL's Fruity Granulizer engine (REA_rodada67) on the channel's sample (FL hands it to the plugin), with the
                // channel settings its voice processor keeps and the project's PPQ, as 3x Osc. Without the sample the
                // channel stays silent, as in FL (the plugin refuses notes)
                def.instrument = InstrumentKind::granulizer;
                const auto fromChannel = c.hasLevels || std::any_of (c.envelopes.begin(), c.envelopes.end(), [] (const auto& e) { return e.present; });
                def.pluginState = granulizerNormalisedState (c.pluginState, fromChannel ? dx10Snapshot (c) : std::vector<std::uint8_t> {});
                def.modelParams = { (float) (project.header.ppq > 0 ? project.header.ppq : 96) };
                if (! c.samplePath.empty() && options.fileExists)
                {
                    auto path = resolveFlPath (c.samplePath, options.roots);
                    path = dataRelativeSample (path);
                    if (! path.empty() && ! options.fileExists (path) && options.fileExists (path + ".wav"))
                        path += ".wav";
                    if (! path.empty() && options.fileExists (path))
                        def.samplePath = path;
                }
                adoptedFromPlugin = true;
            }
            else if (name == "harmless")
            {
                // FL's additive engine (REA_rodada32); the internal synth approximation stays as the fallback
                if (auto params = harmlessParams (c.pluginState))
                {
                    def.instrument = InstrumentKind::harmless;
                    def.modelParams = std::move (*params);
                    adoptedFromPlugin = true;
                    // rack compressor: Harmless's own preset <n>.fst from the user's installation (REA_rodada36 §2)
                    const auto compType = (int) def.modelParams[(std::size_t) harmlessComboIndex + 3];
                    if (compType > 0)
                    {
                        const auto path = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Harmless\\Data\\Compression\\"
                                                             + std::to_string (compType) + ".fst",
                                                         options.roots);
                        std::optional<std::vector<std::uint8_t>> state;
                        if (! path.empty() && options.readFile && (! options.fileExists || options.fileExists (path)))
                            state = hlfx::maximusStateFromFst (options.readFile (path));
                        if (state && hlfx::makeMaximusPreset (*state) != nullptr)
                        {
                            def.modelParams.push_back ((float) state->size());
                            for (const auto byte : *state)
                                def.modelParams.push_back ((float) byte);
                        }
                        else if (options.report != nullptr)
                            ++options.report->partialEffects["Harmless compressor (preset not found)"];
                    }
                }
                else if (synthFromHarmless (c.pluginState, song.bpm, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "fruit kick")
            {
                // FL's Fruit Kick engine with its voice framework (REA_rodada43 §1). A version-0 state has no envelope
                // snapshot of its own: FL hands the plugin the channel's envelope/filter records, so they are appended
                // to it. The sine + pitch-envelope conversion stays as the fallback for unreadable states.
                if (fruitKickStateUsable (c.pluginState))
                {
                    // the same 364-byte channel snapshot as DX10's (REA_rodada42 §3): five 68-byte records + Mod X/Y + type
                    const auto snap = dx10Snapshot (c);
                    std::vector<std::vector<std::uint8_t>> records;
                    for (int k = 0; k < 5; ++k)
                        records.emplace_back (snap.begin() + 68 * k, snap.begin() + 68 * (k + 1));
                    std::int32_t cut = 256, res = 0, type = 0;
                    std::memcpy (&cut, snap.data() + 0x15c, 4);
                    std::memcpy (&res, snap.data() + 0x160, 4);
                    std::memcpy (&type, snap.data() + 0x168, 4);
                    def.instrument = InstrumentKind::fruitkick;
                    def.pluginState = fruitKickWithChannelEnvelope (c.pluginState, records, cut, res, type);
                    adoptedFromPlugin = true;
                    if (options.report != nullptr)
                        for (const auto& f : fruitKickFallbacks (def.pluginState))
                            ++options.report->partialEffects["Fruit Kick: " + f];
                }
                else if (synthFromFruitKick (c.pluginState, fromPlugin))
                    adopt();
            }
            else if (name == "minisynth")
            {
                // FL's MiniSynth engine from the raw state (REA_rodada41) with its wave tables built at run time from the
                // installation's oscdata.dat; the internal-synth conversion stays as the fallback (state unreadable or
                // file not found)
                const auto oscdata = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Minisynth\\oscdata.dat", options.roots);
                if (miniSynthStateUsable (c.pluginState) && ! oscdata.empty() && options.fileExists && options.fileExists (oscdata))
                {
                    def.instrument = InstrumentKind::minisynth;
                    def.pluginState = c.pluginState;
                    def.samplePath = oscdata;
                    adoptedFromPlugin = true;
                }
                else if (synthFromMiniSynth (c.pluginState, song.bpm, fromPlugin))
                    adopt();
            }
            else if (name == "fruity dx10" || name == "dx10")
            {
                // FL's FM engine from the raw state (REA_rodada42), with the channel settings its voice processor
                // keeps (a version-2 state's own snapshot, else the channel's 0xDA/0xDB records) and the project's PPQ
                // for the envelope ticks; the 3-oscillator FM approximation stays as a fallback
                if (auto st = dx10StateWithSnapshot (c.pluginState, dx10Snapshot (c)); ! st.empty())
                {
                    def.instrument = InstrumentKind::dx10;
                    def.pluginState = std::move (st);
                    def.modelParams = { (float) (project.header.ppq > 0 ? project.header.ppq : 96) };
                    adoptedFromPlugin = true;
                }
                else if (synthFromDX10 (c.pluginState, fromPlugin))
                    adopt();
            }
            else if (name == "sytrus")
            {
                // FL's Sytrus engine from the raw state (REA_rodada31); the 3-oscillator conversion stays as a fallback
                if (sytrus::stateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::sytrus;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                }
                else if (synthFromSytrus (c.pluginState, song.bpm, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "harmor")
            {
                // FL's additive engine from the raw state (REA_rodada34); the spectral-oscillator conversion stays as
                // the fallback for states the engine can't read
                if (harmorStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::harmor;
                    // an old (version 11…15) state is stored in the current layout, as FL re-saves it (REA_rodada165g-flbeta165s §1)
                    def.pluginState = harmorUpgradeState (c.pluginState).value_or (c.pluginState);
                    adoptedFromPlugin = true;
                    // rack compressor: Harmor's own preset System\Compression\<n>.fst from the user's installation
                    // (REA_rodada38); its Maximus state goes into modelParams (size, bytes…)
                    const auto compType = harmorCompressorType (c.pluginState);
                    if (compType > 0)
                    {
                        const auto path = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Harmor\\System\\Compression\\"
                                                             + std::to_string (compType) + ".fst",
                                                         options.roots);
                        std::optional<std::vector<std::uint8_t>> state;
                        if (! path.empty() && options.readFile && (! options.fileExists || options.fileExists (path)))
                            state = hlfx::maximusStateFromFst (options.readFile (path));
                        if (state && hlfx::makeMaximusPreset (*state) != nullptr)
                        {
                            def.modelParams.clear();
                            def.modelParams.push_back ((float) state->size());
                            for (const auto byte : *state)
                                def.modelParams.push_back ((float) byte);
                        }
                        else if (options.report != nullptr)
                            ++options.report->partialEffects["Harmor compressor (preset not found)"];
                    }
                }
                else if (synthFromHarmor (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "gms")
            {
                // FL's GMS engine from the raw state (REA_rodada39 §1) with its oscillator waves read at run time from
                // the user's installation; the 3-oscillator conversion stays as the fallback (state unreadable or waves
                // not found)
                const auto folder = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\GMS\\Synth Waves", options.roots);
                const auto waves = gmsWaveNames (c.pluginState);
                const auto wavesFound = ! waves.empty() && ! folder.empty() && options.fileExists
                                        && std::all_of (waves.begin(), waves.end(), [&] (const std::string& w) {
                                               return w.empty() || options.fileExists (folder + "\\" + w + ".wav");
                                           });
                if (gmsStateUsable (c.pluginState) && wavesFound)
                {
                    def.instrument = InstrumentKind::gms;
                    def.pluginState = c.pluginState;
                    def.samplePath = folder;
                    adoptedFromPlugin = true;
                }
                else if (synthFromGms (c.pluginState, song.bpm, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "ogun" || name == "autogun")
            {
                // FL's inverse-FFT engine from the raw state (REA_rodada37). Autogun (REA_rodada57) runs the same engine on
                // its state variant, then its Magic Dust stage with the preset Autogun\Data\SG.dat of the user's FL
                // installation (read at run time; without it the stage is skipped). States the engine can't read keep the
                // spectral-oscillator conversion.
                if (name == "ogun" && ogunStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::ogun;
                    def.pluginState = ogunCurrentLayout (c.pluginState); // (versions 10…13 in today's layout, round 169l)
                    adoptedFromPlugin = true;
                }
                else if (name == "autogun" && autogunStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::autogun;
                    def.pluginState = c.pluginState;
                    const auto sg = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Autogun\\Data\\SG.dat", options.roots);
                    if (! sg.empty() && (! options.fileExists || options.fileExists (sg)))
                        def.samplePath = sg;
                    else if (options.report != nullptr && ogunParam (c.pluginState, 63).value_or (0) > 0)
                        ++options.report->partialEffects["Autogun: Magic Dust (SG.dat not found)"];
                    adoptedFromPlugin = true;
                }
                else if (synthFromOgun (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "sakura")
            {
                // the physical string model (REA_rodada28 §1); the old saw + triangle stays as a fallback
                if (auto params = sakuraParams (c.pluginState))
                {
                    def.instrument = InstrumentKind::sakura;
                    def.modelParams = std::move (*params);
                    adoptedFromPlugin = true;
                }
                else if (synthFromSakura (c.pluginState, fromPlugin))
                    adopt();
            }
            else if (name == "simsynth")
            {
                // FL's SimSynth engine from the raw state (REA_rodada64 §1–4); it needs no installation data. A state the
                // engine can't read keeps the channel unsupported (there is no conversion to fall back to)
                if (simSynthStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::simsynth;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "drumaxx")
            {
                // FL's Drumaxx engine from the raw state (REA_rodada67 §6–§9); it needs no installation data. A state the
                // engine can't read keeps the channel unsupported (there is no conversion to fall back to). A channel
                // with no state at all is the DLL's own defaults, which FL renders silent on every key
                // (REA_rodada169e §2, round 169l): a Drumaxx without a kit, silent like FL (not the stand-in synth)
                if (drumaxxStateUsable (c.pluginState) || c.pluginState.empty())
                {
                    def.instrument = InstrumentKind::drumaxx;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "wasp" || name == "wasp xt")
            {
                // FL 2026 has no 64-bit Wasp: its DLL is a placeholder that keeps the state and plays nothing (REA_rodada64 §5)
                def.instrument = InstrumentKind::wasp;
                def.pluginState = c.pluginState;
                adoptedFromPlugin = true;
            }
            else if (name == "fl slayer")
            {
                // FL's engine from the raw state (REA_rodada68) with the recorded pick excitations, chord and glide tables
                // read at run time from the user's own Slayer_Engine.dll; there is no conversion to fall back to (state or
                // DLL not usable: the channel stays unsupported)
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\FL Slayer\\Slayer_Engine.dll", options.roots);
                if (slayerStateUsable (c.pluginState) && ! dll.empty() && options.fileExists && options.fileExists (dll) && slayerDllUsable (dll))
                {
                    def.instrument = InstrumentKind::slayer;
                    def.pluginState = c.pluginState;
                    def.samplePath = dll;
                    // the root-note shift stays in the key; the rest of the channel pitch reaches FL Slayer as a pitch
                    // bend (ProcessEvent 4, REA_rodada79 §2.2)
                    def.modelParams = { c.rootNote >= 0 && c.rootNote <= 127 ? (float) (60 - c.rootNote) : 0.0f };
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "fruity video player")
            {
                // FL's state as it is (round 165g, core/VideoPlayer.h); the video's path through FL's folder names
                if (auto vs = video::read (c.pluginState))
                {
                    if (vs->path.find ('%') != std::string::npos)
                        if (const auto resolved = resolveFlPath (vs->path, options.roots); ! resolved.empty())
                            vs->path = resolved;
                    def.instrument = InstrumentKind::videoplayer;
                    def.pluginState = video::write (*vs);
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "fruity dance")
            {
                // a visual plugin: notes change the dance, its Gen_Render writes nothing (REA_rodada68 §8)
                def.instrument = InstrumentKind::fruitydance;
                def.pluginState = c.pluginState;
                // a sprite sheet inside FL's folder is saved as %FLStudioFactoryData%\... (round 190bf, fl190be Q5):
                // LYNX keeps the dancer with the file's real place (the export writes FL's own bytes back while it is
                // not edited: flOrigin below)
                if (const auto stored = dance::fromFl (c.pluginState); stored && ! stored->sheet.empty())
                    if (const auto expanded = dance::fromFl (c.pluginState, options.roots.factoryData); expanded && expanded->sheet != stored->sheet)
                        def.pluginState = dance::toState (*expanded);
                adoptedFromPlugin = true;
            }
            else if (name == "transistor bass")
            {
                // FL's engine from the raw state (REA_rodada57 §2) with the "303 Pulse" oscillator cycles read at run time
                // from the user's own engine DLL; there is no conversion to fall back to (state or DLL not usable: the
                // channel stays unsupported)
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Transistor Bass\\engine_x64.dll", options.roots);
                if (transistorBassStateUsable (c.pluginState) && ! dll.empty() && options.fileExists && options.fileExists (dll)
                    && transistorBassDllUsable (dll))
                {
                    def.instrument = InstrumentKind::transistorbass;
                    def.pluginState = c.pluginState;
                    def.samplePath = dll;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "fruity soundfont player" || name == "soundfont player")
            {
                // FL's SoundFont player → LYNX Soundfont: the font (FL path macros expanded; MissingFiles relocates it
                // when it moved), its preset and the overrides (REA_rodada158 §1)
                sf::FlPlayerState fl;
                if (sf::parseFlPlayerState (c.pluginState, fl))
                {
                    const auto resolved = resolveFlPath (fl.path, options.roots);
                    const auto info = resolved.empty() ? sf::FontInfo {} : sf::scanFontFile (resolved);
                    const auto name0 = def.name;
                    def.instrument = InstrumentKind::soundfont;
                    def.samplePath = resolved.empty() ? fl.path : resolved;
                    def.modelParams = sf::paramsFromFl (fl, info.presets);
                    def.pluginState.clear();
                    def.name = name0;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "plucked!")
            {
                // FL's Karplus-Strong engine from the raw state (REA_rodada65 §1); there was no conversion before
                if (pluckedStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::plucked;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "beepmap")
            {
                // FL's image-synthesis engine from the raw state, bitmap included (REA_rodada65 §2)
                if (beepMapStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::beepmap;
                    def.pluginState = c.pluginState;
                    def.modelParams = { (float) (project.header.ppq > 0 ? project.header.ppq : 96) };
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "sawer")
            {
                // FL's engine from the raw state (REA_rodada65 §3) with its oscillator cycle read at run time from the
                // user's Sawer\data.saw; there was no conversion before (state or file not usable: unsupported)
                const auto saw = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Sawer\\data.saw", options.roots);
                if (sawerStateUsable (c.pluginState) && ! saw.empty() && options.fileExists && options.fileExists (saw))
                {
                    def.instrument = InstrumentKind::sawer;
                    def.pluginState = c.pluginState;
                    def.samplePath = saw;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "kepler")
            {
                // FL's engine from the raw state (REA_rodada46) with its LFO-shape and noise tables read at run time from the
                // user's own engine DLL; the synth conversion stays as the fallback (state or DLL not usable)
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Kepler\\engine_x64.dll", options.roots);
                if (keplerStateUsable (c.pluginState) && ! keplerExoState (c.pluginState) && ! dll.empty() && options.fileExists
                    && options.fileExists (dll) && keplerDllUsable (dll) && ! keplerExoDllUsable (dll))
                {
                    def.instrument = InstrumentKind::kepler;
                    def.pluginState = c.pluginState;
                    def.samplePath = dll;
                    adoptedFromPlugin = true;
                }
                else if (synthFromKepler (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "kepler exo")
            {
                // FL 2026's Kepler Exo: Kepler's engine on the "Kepler-2" state (REA_rodada167d) with the tables of the user's own
                // Kepler Exo engine DLL; no synth conversion exists for it (state or DLL not usable: the channel stays empty)
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Kepler Exo\\engine_x64.dll", options.roots);
                if (keplerExoState (c.pluginState) && ! dll.empty() && options.fileExists && options.fileExists (dll) && keplerExoDllUsable (dll))
                {
                    def.instrument = InstrumentKind::keplerExo;
                    def.pluginState = c.pluginState;
                    def.samplePath = dll;
                    adoptedFromPlugin = true;
                }
            }
            else if (name == "boobass")
            {
                // FL's engine with its multisample read at run time from the user's own BooBass DLL (REA_rodada29 §4)
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\BooBass\\BooBass_Eng_x64.dll", options.roots);
                auto word = [&] (std::size_t at) { std::uint32_t v = 0; std::memcpy (&v, c.pluginState.data() + at, 4); return v; };
                if (c.pluginState.size() >= 16 && word (0) == 1 && ! dll.empty() && options.fileExists && options.fileExists (dll)
                    && booBassDllUsable (dll))
                {
                    def.instrument = InstrumentKind::boobass;
                    def.samplePath = dll;
                    for (std::size_t k = 0; k < 3; ++k)
                        def.modelParams.push_back ((float) std::clamp ((int) word (4 + 4 * k), 0, 65535));
                    adoptedFromPlugin = true;
                }
                else if (synthFromBooBass (c.pluginState, fromPlugin))
                    adopt();
            }
            else if (name == "poizone")
            {
                // FL's PoiZone engine from the raw state (REA_rodada44); the 2-oscillator conversion stays as the
                // fallback for states the engine can't read
                if (poizoneStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::poizone;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                }
                else if (synthFromPoiZone (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "bassdrum")
            {
                // FL's BassDrum engine (REA_rodada43 §2) reading its oscillator table and noise sample at run time from
                // the user's BassDrum folder; the sine + sweep conversion stays as the fallback (state unreadable or
                // folder not found)
                const auto folder = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\BassDrum", options.roots);
                const auto tableFound = ! folder.empty() && options.fileExists && options.fileExists (folder + "\\sounddata");
                if (bassDrumStateUsable (c.pluginState) && tableFound)
                {
                    def.instrument = InstrumentKind::bassdrum;
                    def.pluginState = c.pluginState;
                    def.samplePath = folder;
                    adoptedFromPlugin = true;
                    if (const auto noise = bassDrumNoiseName (c.pluginState); ! noise.empty() && options.report != nullptr
                        && ! options.fileExists (folder + "\\Noise Samples\\" + noise))
                        ++options.report->partialEffects["BassDrum noise sample (not found)"];
                }
                else if (synthFromBassDrum (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "toxic biohazard")
            {
                // FL's engine from the raw state (REA_rodada45); its built-in waves 2…32 are spectra read at run time from the
                // user's own Toxic Biohazard_x64.dll (SHA-256 checked). Without it (or for states the engine can't read) the
                // 2-operator conversion stays
                const auto dll = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Toxic Biohazard\\Toxic Biohazard_x64.dll",
                                                options.roots);
                const auto dllFound = ! dll.empty() && options.fileExists && options.fileExists (dll) && toxicDllUsable (dll);
                if (toxicStateUsable (c.pluginState) && (dllFound || ! toxicNeedsDll (c.pluginState)))
                {
                    def.instrument = InstrumentKind::toxic;
                    def.pluginState = c.pluginState;
                    if (dllFound)
                        def.samplePath = dll;
                    adoptedFromPlugin = true;
                }
                else if (synthFromToxic (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "morphine")
            {
                // FL's Morphine engine from the raw state (REA_rodada39 §2); its noise sample is read at run time from
                // the user's installation (not found: the noise stays silent). The averaged-spectrum conversion stays as
                // the fallback for states the engine can't read
                if (morphineStateUsable (c.pluginState))
                {
                    def.instrument = InstrumentKind::morphine;
                    def.pluginState = c.pluginState;
                    adoptedFromPlugin = true;
                    if (const auto noise = morphineNoiseName (c.pluginState); ! noise.empty())
                    {
                        const auto path = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Morphine\\Noise Samples\\" + noise,
                                                         options.roots);
                        if (! path.empty() && options.fileExists && options.fileExists (path))
                            def.samplePath = path;
                        else if (options.report != nullptr)
                            ++options.report->partialEffects["Morphine noise sample (not found)"];
                    }
                }
                else if (synthFromMorphine (c.pluginState, fromPlugin, pluginGain))
                    adopt();
            }
            else if (name == "drumpad" && drumpadParams (c.pluginState))
            {
                // the physical drum model (REA_rodada28 §2)
                const auto dp = drumpadParams (c.pluginState);
                def.instrument = InstrumentKind::drumpad;
                def.modelParams.assign (dp->begin(), dp->end());
                adoptedFromPlugin = true;
            }
            else if (name == "drumpad")
                if (const auto patch = drumpadPatchName (c.pluginState))
                {
                    // kick/snare/clap/hi-hat: the drum instruments; the rest, a drum on the synth
                    if (const auto drum = guessInstrument (*patch, flp::ChannelType::sampler); drum != InstrumentKind::synth)
                    {
                        def.instrument = drum;
                        adoptedFromPlugin = true;
                    }
                    else
                    {
                        fromPlugin = drumToneForName (*patch);
                        // confirmed: Pitch ×2^(2x − 1) and Mallet Amplitude 10^(−4(1 − x)) (the physical model isn't)
                        if (const auto dp = drumpadParams (c.pluginState))
                        {
                            const auto semis = 12.0f * (2.0f * std::clamp ((*dp)[29], 0.0f, 1.0f) - 1.0f);
                            for (auto& o : fromPlugin.osc)
                            {
                                o.semitones += (int) std::lround (semis);
                                o.detune += 100.0f * (semis - (float) std::lround (semis));
                            }
                            pluginGain = (float) std::pow (10.0, -4.0 * (1.0 - std::clamp ((double) (*dp)[2], 0.0, 1.0)));
                        }
                        adopt();
                    }
                }
        }

        // 3x Osc: oscillators from the plugin state; envelope and filter come from the channel (REA_rodada4)
        if (def.instrument == InstrumentKind::synth && c.pluginName == "3x Osc" && synthFrom3xOsc (c.pluginState, def.synth))
        {
            if (hasVolumeEnvelope)
            {
                def.synth.attack = def.sampler.attack;
                def.synth.decay = def.sampler.decay;
                def.synth.sustain = def.sampler.sustain;
                def.synth.release = def.sampler.release;
            }
            else
            {
                // without an envelope FL plays the note as a gate, with only a short declick
                def.synth.attack = 0.002f;
                def.synth.decay = 0.0f;
                def.synth.sustain = 1.0f;
                def.synth.release = 0.02f;
            }

            synthFilterFromChannel (c.modX, c.modY, c.filterType, def.synth.cutoff, def.synth.resonance, def.synth.filterType);

            def.synth.filterEnv = filterEnv;
            def.synth.pitchEnv = pitchEnv;
        }

        // sample region and start offset
        if (std::isfinite (c.sampleStart) && std::isfinite (c.sampleEnd) && std::isfinite (c.startOffset))
        {
            // FL's end is a fraction of what follows the start: the region is [start, start + end·(1 − start)]
            // (FLEngine FUN_00ff0db0, REA_rodada167h §2; FL's demos hold ends below their starts, e.g. 0.62 / 0.14)
            const auto start = std::clamp (c.sampleStart, 0.0, 1.0);
            const auto end = std::clamp (start + std::clamp (c.sampleEnd, 0.0, 1.0) * (1.0 - start), 0.0, 1.0);

            if (end > start)
            {
                def.sampler.regionStart = (float) start;
                def.sampler.regionEnd = (float) end;
                // the start offset: FL starts notes there, a fraction of the (cropped) region [FL render, §4]
                def.sampler.startOffset = (float) std::clamp (c.startOffset, 0.0, 1.0);
            }
        }

        // the Sampler's precomputed effects, loop flags and declicking mode (REA_rodada167h §1)
        // (audio clip channels: the precomputed effects and Reverse too — FL plays them, round 167k §8)
        if (c.type == flp::ChannelType::sampler || def.instrument == InstrumentKind::audioClip)
        {
            auto& p = def.sampler.precompute;
            p.fadeStereo = (c.fxFlags & 1) != 0;
            def.sampler.reverse = def.sampler.reverse || (c.fxFlags & 2) != 0;
            p.swapStereo = (c.fxFlags & 0x100) != 0;
            p.fadeIn = std::clamp (c.fadeIn, 0, 1024);
            p.fadeOut = std::clamp (c.fadeOut, 0, 1024);
            p.removeDc = c.removeDc;
            p.normalize = c.normalize;
            p.reversePolarity = c.reversePolarity;
            p.crossfade = std::clamp (c.crossfade, 0, 2048);
            p.trim = std::clamp (c.trim, 0, 32768);
            p.pingPong = c.pingPong;
            // the legacy precomputed effects (REA_rodada167l §1), within the ranges of FL's knobs
            p.boost = std::clamp (c.boost, 0, 256);
            p.eq = std::clamp (c.eq, 0, 256);
            p.ringMix = std::clamp (c.ringMix, 0, 256);
            p.ringFreq = std::clamp (c.ringFreq, 0, 256);
            p.stereoDelay = std::clamp (c.stereoDelay, 0, 4096);
            p.cutoff = std::clamp (c.cutoff, 16, 1024);
            p.resonance = std::clamp (c.resonance, 0, 640);
            p.reverb = std::clamp (c.reverb, 0, 256);
            p.reverbType = c.reverbType == 0 ? 0 : 1;
            p.pogo = std::clamp (c.pogo, 0, 512);
            p.clip = (c.fxFlags & 4) != 0;
            def.sampler.useLoopPoints = c.useLoopPoints();
            def.sampler.declickMode = std::clamp (c.declickMode, 0, 5);
            // round 190dh: the Content options (0x8F; kept for the way back) and the loop points of FL's loop knobs (0xFB)
            if (c.sampleFlags >= 0)
            {
                def.sampler.keepOnDisk = (c.sampleFlags & 0x100) != 0;
                def.sampler.resampleOnLoad = (c.sampleFlags & 1) != 0;
                def.sampler.loadRegions = (c.sampleFlags & 2) != 0;
                def.sampler.loadSliceMarkers = (c.sampleFlags & 4) != 0;
            }
            def.sampler.loopStart = c.loopStart;
            def.sampler.loopEnd = c.loopEnd;
        }

        // time-stretch: the sample lasts TIME/192 beats × MUL
        if (c.stretchTime > 0.0f)
        {
            def.sampler.stretchBeats = (float) (c.stretchTime / 192.0 * std::pow (2.0, c.stretchMul / 10000.0));
            def.sampler.stretchMul = std::clamp (c.stretchMul, -20000, 20000); // (the page's MUL; round 190dh)
            def.sampler.keepPitch = c.stretchMode != 0; // only FL's "Resample" changes the pitch
            def.sampler.stretchMethod = stretchMethodForFlMode (c.stretchMode);
            // Fruity Granulizer: FL hands the plugin TIME as its beat count, without MUL (REA_rodada165g-flgran165 §3)
            if (def.instrument == InstrumentKind::granulizer)
                def.sampler.stretchBeats = (float) (c.stretchTime / 192.0), def.sampler.stretchMul = 0;
        }

        // FL's time-stretching panel on a sample channel or an audio clip [FL: measured, REA_rodada164e-clippitch §2]:
        // every mode but Resample keeps the length (also without a TIME: the keys, the pitch knob and the root note
        // transpose keeping the length) and adds its PITCH knob; Resample ignores that knob. MUL without a TIME
        // stretches the sample by 2^(MUL/10000) (Resample: slower and lower), at the project's tempo here.
        if ((c.type == flp::ChannelType::sampler || isAudioClip)
            && (def.instrument == InstrumentKind::sampler || def.instrument == InstrumentKind::audioClip))
        {
            const auto needsLength = c.stretchTime <= 0.0f && c.stretchMul != 0 && ! def.samplePath.empty() && options.sampleSeconds;
            applyFlStretchPanel (def.sampler, c.stretchMode, c.stretchPitch, c.stretchMul, c.stretchTime,
                                 needsLength ? options.sampleSeconds (def.samplePath) : 0.0, song.bpm);
        }
        // report: what happened to this channel's generator/sample
        if (options.report != nullptr)
        {
            auto& r = *options.report;
            const auto plugin = lowercase (c.pluginName);

            if (plugin.empty())
                r.missingSamples += ! c.samplePath.empty() && def.samplePath.empty() ? 1 : 0;
            else if (plugin == "3x osc" || ((plugin == "directwave" || plugin == "fl keys") && ! def.zones.empty())
                     || ((plugin == "fruity slicer" || plugin == "slicex") && ! def.samplePath.empty())
                     || (plugin == "fpc" && ! drumKits.empty() && drumKits.back().first == (int) song.channels.size()))
                ++r.converted[c.pluginName];
            else if (def.instrument == InstrumentKind::sakura || def.instrument == InstrumentKind::drumpad
                     || def.instrument == InstrumentKind::boobass || def.instrument == InstrumentKind::drumsynth
                     || def.instrument == InstrumentKind::harmless || def.instrument == InstrumentKind::sytrus
                     || def.instrument == InstrumentKind::harmor || def.instrument == InstrumentKind::gms
                     || def.instrument == InstrumentKind::morphine
                     || def.instrument == InstrumentKind::ogun || def.instrument == InstrumentKind::minisynth
                     || def.instrument == InstrumentKind::dx10
                     || def.instrument == InstrumentKind::kepler || def.instrument == InstrumentKind::keplerExo || def.instrument == InstrumentKind::poizone
                     || def.instrument == InstrumentKind::fruitkick || def.instrument == InstrumentKind::bassdrum || def.instrument == InstrumentKind::toxic
                     || def.instrument == InstrumentKind::autogun || def.instrument == InstrumentKind::transistorbass
                     || def.instrument == InstrumentKind::simsynth || def.instrument == InstrumentKind::wasp
                     || def.instrument == InstrumentKind::plucked || def.instrument == InstrumentKind::beepmap
                     || def.instrument == InstrumentKind::sawer || def.instrument == InstrumentKind::flkeys
                     || def.instrument == InstrumentKind::granulizer || def.instrument == InstrumentKind::drumaxx
                     || def.instrument == InstrumentKind::slayer || def.instrument == InstrumentKind::fruitydance
                     || def.instrument == InstrumentKind::videoplayer || def.instrument == InstrumentKind::wavetraveller
                     || def.instrument == InstrumentKind::fruityslicer || def.instrument == InstrumentKind::slicex
                     || def.instrument == InstrumentKind::fpc
                     || def.instrument == InstrumentKind::directwave || def.instrument == InstrumentKind::soundfont)
                ++r.converted[c.pluginName]; // FL's own engine rebuilt from the plugin state
            else if ((plugin == "harmless" && def.instrument != InstrumentKind::harmless) || (plugin == "harmor" && def.instrument != InstrumentKind::harmor && adoptedFromPlugin) || (plugin == "minisynth" && def.instrument != InstrumentKind::minisynth) || plugin == "fruity dx10" || plugin == "dx10" || (plugin == "fruit kick" && def.instrument != InstrumentKind::fruitkick)
                     || ((plugin == "sytrus" || plugin == "gms" || plugin == "ogun" || plugin == "autogun" || plugin == "sakura" || plugin == "kepler" || plugin == "boobass" || plugin == "drumpad" || plugin == "poizone" || plugin == "bassdrum"
                         || plugin == "toxic biohazard" || plugin == "morphine") && adoptedFromPlugin)
                     || (plugin == "fruity drumsynth live" && def.instrument == InstrumentKind::sampler))
                ++r.approximated[c.pluginName];
            else if (((plugin == "fruity envelope controller" || plugin == "fruity keyboard controller") && def.instrument == InstrumentKind::controller)
                     || (plugin == "fruity drumsynth live" && def.instrument == InstrumentKind::drumsynth)
                     || (plugin == "harmless" && def.instrument == InstrumentKind::harmless)
                     || (plugin == "harmor" && def.instrument == InstrumentKind::harmor))
                ++r.converted[c.pluginName];
            else if (plugin == "fruity wrapper" && def.plugin.foreign.format == ForeignPlugin::Format::none)
                ++r.unsupported["Fruity Wrapper (plugin state not readable)"]; // e.g. a wrapper version newer than 12
            else if ((plugin == "midi out" && def.instrument == InstrumentKind::midiOut)
                     || (plugin == "fruity voltage controller" && def.instrument == InstrumentKind::voltage)
                     || (plugin == "dashboard" && def.instrument == InstrumentKind::dashboard))
                ++r.converted[c.pluginName];
            else if (def.instrument == InstrumentKind::vfx)
                ++r.converted[c.pluginName];
            else if (plugin != "fruity wrapper" && plugin != "fruity keyboard controller" && plugin != "midi out"
                     && def.instrument == InstrumentKind::synth)
            {
                const auto note = flextra::inertPluginNote (c.pluginName); // round 165g: ReWired, Video Player, FL Studio Mobile
                if (c.pluginName == "ReWired")
                    def.muted = true; // no sound of their own in LYNX: the stand-in synth must not play their notes
                ++r.unsupported[note.empty() ? c.pluginName : note]; // (Keyboard Controller only sends notes to other channels)
            }
        }

        // Export to FL Studio (round 171e): the original generator, written back while LYNX leaves it as imported (an FPC
        // becomes several channels and a wrapped plugin travels as its plugin reference: neither keeps one)
        if (c.type == flp::ChannelType::instrument && ! c.pluginName.empty() && c.pluginName != "Fruity Wrapper" && c.pluginName != "FPC")
        {
            def.flOrigin.plugin = c.pluginName;
            if (def.pluginState != c.pluginState)
                def.flOrigin.state = c.pluginState;
        }

        song.channels.push_back (std::move (def));
    }

    // Layer children: FL's channel indices → LYNX's (children that did not become channels are dropped)
    for (auto& def : song.channels)
        if (def.instrument == InstrumentKind::layer)
        {
            std::vector<int> mapped;
            for (const auto k : def.layer.children)
                if (const auto found = channelMap.find (k); found != channelMap.end())
                    mapped.push_back (found->second);
            def.layer.children = std::move (mapped);
            if (options.report != nullptr)
                ++options.report->converted["Layer"];
        }

    // Mixer: names, mute/solo, routing and levels from the .flp; enough inserts for all
    // routes (minimum 16, like a new project). FL's extra tracks (500/501) are left out.
    // FL's mixer track count (0x67, REA round 168j: FLP_FXTrackCount = the track records saved — Master, the inserts and
    // FL's "current" track): FL shows that many inserts; files without it: 16 or as many as are used
    int highestTrack = project.mixerTrackCount >= 3 ? project.mixerTrackCount - 2 : 16;
    for (const auto& c : song.channels)
        highestTrack = std::max (highestTrack, c.mixerTrack);

    for (const auto& insert : project.mixer)
        if (insert.index < maxMixerTracks)
            for (auto target : insert.routes)
                if (target < maxMixerTracks && (insert.index > 0 || target > 0))
                    highestTrack = std::max ({ highestTrack, target, insert.index });

    song.mixer = Song::defaultMixer (std::min (highestTrack, maxMixerTracks - 1));
    const auto tracks = (int) song.mixer.size();

    for (const auto& insert : project.mixer)
    {
        if (insert.index < 0 || insert.index >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) insert.index];

        if (insert.index > 0 && ! insert.name.empty())
            track.name = insert.name;

        // (0x2A off = FL draws the default colour whatever 0x95 holds; REA round 168j)
        if (insert.colour != 0 && insert.colour != 0x485156 && insert.colourSet != 0) // FL's default gray
            track.colour = 0xFF000000u | insert.colour;
        track.icon = std::clamp (insert.icon, 0, 0xFFFF); // 0x5F (REA round 168j)

        track.muted = (insert.flags & 0x08) == 0;
        track.invertPhase = (insert.flags & 0x01) != 0;
        track.swapChannels = (insert.flags & 0x02) != 0;
        track.solo = insert.index > 0 && (insert.flags & 0x1000) != 0;
        // REA round 146a (FL's insert writer FUN_00f3d620): bits 6-7 = dock (0 left, 1 middle, 2 right), 0x400 separator
        track.separator = insert.index > 0 && (insert.flags & 0x400) != 0;
        // (FL's last insert, the "Current" track, is saved with dock 0 too; LYNX keeps it in the middle)
        const auto dockBits = insert.flags >> 6 & 3;
        const auto isCurrentTrack = insert.index > 0 && insert.index + 1 == (int) project.mixer.size();
        track.dock = dockBits == 2 ? MixerDock::right : (dockBits == 0 && ! isCurrentTrack) ? MixerDock::left : MixerDock::middle;
        // FL's track latency (REA round 165f-mixer, FUN_00f3d620): the manual latency (0xEC f32), "Bypass track latency
        // compensation" (0x31) and "Compensate for master latency" (0xEC flag 0x4000 = off)
        track.latencyMs = std::clamp (insert.latencyMs, -maxTrackLatencyMs, maxTrackLatencyMs);
        if (insert.index > 0)
            track.latencyBypass = (LatencyBypass) std::clamp (insert.latencyBypass, 0, 2);
        track.compensateMasterLatency = (insert.flags & 0x4000) == 0;
        // 0x93 "Audio output target" (REA round 156): −1 none, 0 the first pair (the only values seen in 1162 .flp files:
        // every Master 0, one insert 0); other values depend on FL's device list and are not imported, nor a Master
        // set to none (the project would play silent)
        if (insert.output == 0 && insert.index > 0)
            track.outputChannel = 0;
        // 0xA5 the "Record" position (round 168x); a file without it keeps FL 2026's default, Post EQ
        if (insert.recordSource >= 0)
            track.recordSource = (RecordSource) std::clamp (insert.recordSource, 0, 5);
        // 0x9A "Audio input source" (REA round 157c): resolved against the open device when the project opens
        // (AudioEngine::setSong). FL plays an insert's input through it whether the insert is armed or not, stopped or
        // playing ([FL] FL Studio 2026, meters of unarmed inserts with an input): monitoring On
        // (no input and no 0xA6: "Auto", as these files always opened; round 190ev changed only new inserts' default)
        track.monitor = InputMonitor::automatic;
        if (insert.input >= 0)
        {
            track.flInput = insert.input;
            track.monitor = InputMonitor::on;
        }
        // 0xA6 "Monitor external input" (REA round 168j: ExtInputMonitorOffMenuClick's tags = LYNX's order); files
        // without it keep the rule above
        if (insert.monitor >= 0)
            track.monitor = (InputMonitor) std::clamp (insert.monitor, 0, 3);

        if (insert.index > 0 && insert.hasRoutes)
        {
            track.toMaster = std::find (insert.routes.begin(), insert.routes.end(), 0) != insert.routes.end();

            // The level comes from 0xE1 (below). Without a record a route is 100%, except in FL 11 and older,
            // whose templates route every insert to the send tracks at the end of the mixer (100+) at level 0.
            // FL 12+ plays an unrecorded route to insert 100+ at full level (REA_rodada89 §4.1 [EXEC]: FL's
            // "Advanced" templates feed bus tracks 117…125 that way); the exact version boundary is not confirmed.
            const auto oldSendTracks = project.versionNumber < 12'000'000u;
            for (auto target : insert.routes)
                if (target > 0 && target < tracks && target != insert.index)
                    track.sends.push_back ({ target, target >= 100 && oldSendTracks ? 0.0f : 1.0f });
        }
    }

    // native FL effects become the similar internal effects (default parameters: the internal
    // state of FL plugins is not known); the others are left out
    std::map<std::pair<int, int>, std::pair<int, std::string>> effectSlotMap; // (track, FL slot) -> (our effect, FL plugin)

    // inputs of each track (REA_rodada22 §1): [itself, the ones routing to it in ascending order (at
    // any level; level 0 = sidechain only)]; the index k stored in the plugins points into this list
    std::map<int, std::vector<int>> trackInputs;
    for (const auto& insert : project.mixer)
        if (insert.hasRoutes)
            for (const auto target : insert.routes)
                if (target > 0 && target != insert.index)
                    trackInputs[target].push_back (insert.index);
    auto sidechainSource = [&] (int track, int k) {
        if (k <= 0)
            return -1;
        auto inputs = trackInputs[track];
        std::sort (inputs.begin(), inputs.end());
        inputs.erase (std::unique (inputs.begin(), inputs.end()), inputs.end());
        inputs.insert (inputs.begin(), track);
        return k < (int) inputs.size() && inputs[(std::size_t) k] < tracks ? inputs[(std::size_t) k] : -1;
    };
    std::map<std::pair<int, int>, decltype (LovePhilterConversion::paramMap)> lovePhilterMaps; // Love Philter automation

    // data files of the user's FL installation that some effects load at run time (Soundgoodizer's presets,
    // REA_rodada40); nothing of them is stored in LYNX itself, only in the imported project
    const FlDataReader flData = [&options] (const std::string& relative) -> std::vector<std::uint8_t> {
        const auto path = resolveFlPath ("%FLStudioFactoryData%\\" + relative, options.roots);
        if (path.empty() || ! options.readFile || (options.fileExists && ! options.fileExists (path)))
            return {};
        return options.readFile (path);
    };
    // exact engines that fell back to their approximation (for the report)
    auto noteFallback = [&options, &project] (const std::string& plugin, EffectSlot& fx) {
        if (fx.kind == EffectKind::grossBeat) // its song-position sync counts in the project's ticks (REA_rodada53)
            grossbeat::setPpq (fx.state, project.header.ppq > 0 ? project.header.ppq : 96);
        if (options.report == nullptr || fx.kind == EffectKind::maximus || fx.kind == EffectKind::grossBeat)
            return;
        if (plugin == "Gross Beat")
            ++options.report->partialEffects["Gross Beat (state not read: volume gate only)"];
        if (plugin == "ZGameEditor Visualizer") // round 164c: its effects are approximated by LYNX Visualizer layers
            ++options.report->partialEffects["ZGameEditor Visualizer (scene approximated with LYNX Visualizer layers)"];
        if (plugin == "Maximus")
            ++options.report->partialEffects["Maximus (option not decoded: approximated)"];
        else if (plugin == "Soundgoodizer")
            ++options.report->partialEffects["Soundgoodizer (preset file not found: approximated)"];
        else if ((plugin == "Fruity Limiter" && fx.kind != EffectKind::fruityLimiter)
                 || (plugin == "Fruity Compressor" && fx.kind != EffectKind::fruityCompressor)
                 || (plugin == "Fruity Delay 2" && fx.kind != EffectKind::fruityDelay2)
                 || (plugin == "Fruity Delay 3" && fx.kind != EffectKind::fruityDelay3)
                 || (plugin == "Fruity Stereo Shaper" && fx.kind != EffectKind::stereoShaper)
                 || (plugin == "Fruity Stereo Enhancer" && fx.kind != EffectKind::stereoEnhancer)
                 || (plugin == "Transient Processor" && fx.kind != EffectKind::transientProcessor)
                 || (plugin == "Spreader" && fx.kind != EffectKind::spreader)
                 || (plugin == "EQUO" && fx.kind != EffectKind::equo)
                 || (plugin == "Pitch Shifter" && fx.kind != EffectKind::flPitchShifter)
                 || (plugin == "Frequency Splitter" && fx.kind != EffectKind::frequencySplitter)
                 || (plugin == "Distructor" && fx.kind != EffectKind::distructor)
                 || (plugin == "Multiband Delay" && fx.kind != EffectKind::multibandDelay))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if ((plugin == "Fruity WaveShaper" && fx.kind != EffectKind::fruityWaveShaper)
                 || (plugin == "Fruity Soft Clipper" && fx.kind != EffectKind::fruitySoftClipper)
                 || (plugin == "Fruity Delay Bank" && fx.kind != EffectKind::fruityDelayBank)
                 || (plugin == "Fruity Multiband Compressor" && fx.kind != EffectKind::fruityMultiband))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if ((plugin == "Fruity PanOMatic" && fx.kind != EffectKind::fruityPanOMatic)
                 || (plugin == "Fruity Balance" && fx.kind != EffectKind::fruityBalance)
                 || (plugin == "Fruity Send" && fx.kind != EffectKind::fruitySend)
                 || (plugin == "Transporter" && fx.kind != EffectKind::flTransporter)
                 || (plugin == "Fruity Blood Overdrive" && fx.kind != EffectKind::fruityBloodOverdrive))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if ((plugin == "Fruity Phase Inverter" && fx.kind != EffectKind::flPhaseInverter)
                 || (plugin == "Fruity Center" && fx.kind != EffectKind::flCenter)
                 || (plugin == "Fruity Bass Boost" && fx.kind != EffectKind::flBassBoost)
                 || (plugin == "Fruity 7 Band EQ" && fx.kind != EffectKind::fl7BandEq)
                 || (plugin == "Fruity Delay" && fx.kind != EffectKind::flDelay1))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if ((plugin == "Fruity Fast LP" && fx.kind != EffectKind::fruityFastLP)
                 || (plugin == "Fruity Free Filter" && fx.kind != EffectKind::fruityFreeFilter)
                 || (plugin == "Fruity Filter" && fx.kind != EffectKind::fruityFilter))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if (chorusControls (plugin) > 0 && fx.kind == EffectKind::chorus)
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if ((plugin == "Fruity Fast Dist" && fx.kind != EffectKind::fruityFastDist)
                 || (plugin == "Fruity Flangus" && fx.kind != EffectKind::fruityFlangus)
                 || (plugin == "Fruity Flanger" && fx.kind != EffectKind::fruityFlanger)
                 || (plugin == "Fruity Phaser" && fx.kind != EffectKind::fruityPhaser))
            ++options.report->partialEffects[plugin + " (state not readable: approximated)"];
        else if (plugin == "Hardcore" && fx.kind == EffectKind::hardcore
                 && ! hardcore::hasCabinet (fx, (int) hardcore::fromSlot (hardcore::pCabinet, fx.params[(std::size_t) hardcore::pCabinet])))
        {
            if (hardcore::fromSlot (hardcore::pCabinet, fx.params[(std::size_t) hardcore::pCabinet]) > 0.0f)
                ++options.report->partialEffects["Hardcore (cabinet impulse not found: cabinet off)"];
        }
    };

    // FL's Patcher as LYNX's real graph (REA_rodada141): every node (effects through the same converters as the mixer
    // slots, instruments through a one-channel import), every cable (audio with its gain, notes, control values mapped
    // to LYNX's parameters), the editor positions and the extra outputs
    std::function<std::optional<PatcherDoc> (const std::vector<std::uint8_t>&, const flp::Channel*)> patcherFromFl;
    // the last converted Patcher's side-chain pin: its bus k, an index into FL's input list of the insert, as for the
    // other effects (REA_rodada22 §1.2). The pin's name is only the source track's name when saved (Bien Duro: "7
    // Sidechain" on bus 1 and 4 is track 81, named "7 Sidechain"; issue #112: "Master"), REA round 182e
    int patcherSidechain = -1;
    patcherFromFl = [&] (const std::vector<std::uint8_t>& state, const flp::Channel* asChannel) -> std::optional<PatcherDoc> {
        const auto graph = parsePatcher (state);
        if (! graph)
            return std::nullopt;
        PatcherDoc doc;
        std::vector<int> nodeOf (graph->nodes.size(), -1);
        for (std::size_t n = 0; n < graph->nodes.size(); ++n)
        {
            const auto& fl = graph->nodes[n];
            PatchNode node;
            node.label = fl.label;
            // FL's boxes are small: its positions spread to LYNX's node size (FL's dense graphs sit ~60 px apart)
            constexpr float spreadX = 2.0f, spreadY = 1.6f;
            node.x = fl.x * spreadX;
            node.y = fl.y * spreadY;
            if (const auto kind = vfx::kindOfFlName (fl.plugin)) // a VFX node (an effect in FL's graph): voices in, voices out
            {
                node.generator = true;
                vfx::makeChannel (node.channel, *kind);
                vfx::State vs;
                vs.kind = *kind;
                vs.fl = fl.state.empty() ? vfx::defaultState (*kind) : fl.state;
                node.channel.pluginState = vfx::writeState (vs);
                if (! fl.label.empty())
                    node.channel.name = fl.label;
                node.channel.mixerTrack = 0;
                node.channel.volume = 1.0f;
                node.channel.pan = 0.0f;
                if ((options.report != nullptr))
                    ++options.report->converted["Patcher: " + fl.plugin];
            }
            else if (fl.generator)
            {
                node.generator = true;
                flp::Project mini;
                mini.header = project.header;
                mini.tempo = project.tempo;
                flp::Channel ch;
                if (asChannel != nullptr)
                    ch = *asChannel;
                ch.index = 0;
                ch.pluginName = fl.plugin;
                ch.pluginState = fl.state;
                if (! fl.label.empty())
                    ch.name = fl.label;
                mini.channels = { ch };
                auto miniOptions = options;
                miniOptions.report = nullptr;
                auto sub = importFlp (mini, miniOptions);
                if (sub.channels.empty())
                    continue;
                node.channel = std::move (sub.channels.front());
                node.channel.mixerTrack = 0;
                node.channel.volume = 1.0f; // (FL's node has no channel volume: unity)
                node.channel.pan = 0.0f;
            }
            else
            {
                std::optional<EffectSlot> made;
                ForeignPlugin foreign;
                if (lowercase (fl.plugin) == "patcher")
                {
                    if (auto inner = patcherFromFl (fl.state, nullptr))
                    {
                        made = makeEffectSlot (EffectKind::patcher);
                        made->patcher = std::move (*inner);
                    }
                }
                else if (fl.plugin == "Fruity Wrapper" && parseFlWrapper (fl.state, foreign))
                {
                    if (foreign.format == ForeignPlugin::Format::vst2 && effectForFlPlugin (foreign.name))
                        made = effectFromFl (canonicalFlEffectName (foreign.name), wrappedFruityState (foreign), song.bpm);
                    else
                    {
                        made = makeEffectSlot (EffectKind::plugin);
                        made->plugin.name = foreign.name;
                        applyFlWrapperOptions (foreign, made->plugin);
                        made->plugin.foreign = std::move (foreign);
                    }
                }
                else if ((made = effectFromFl (canonicalFlEffectName (fl.plugin), fl.state, song.bpm, flData)))
                    noteFallback (canonicalFlEffectName (fl.plugin), *made);
                if (! made)
                {
                    // a pass-through in its place (controllers and visualizers have nothing to pass anyway)
                    made = makeEffectSlot (EffectKind::utility);
                    if (node.label.empty())
                        node.label = fl.plugin;
                    if (options.report != nullptr)
                        ++options.report->partialEffects["Patcher: " + fl.plugin + " (no equivalent: passes the signal)"];
                }
                if ((fl.flags & 4) != 0)
                    made->enabled = false;
                node.effect = std::move (*made);
                // the extra outputs FL's splitters have (bands / difference) as LYNX bus heads
                for (const auto& cable : graph->cables)
                    for (const auto& [id, port] : graph->ports)
                        if (id == cable.source && port.node == (int) n && ! port.input && port.kind == 0 && port.index > 0
                            && std::none_of (node.busHeads.begin(), node.busHeads.end(), [&] (const PatchNode::BusHead& h) { return h.bus == port.index; }))
                        {
                            const auto head = patcherBranchHead (canonicalFlEffectName (fl.plugin), fl.state, port.index);
                            if (! head.valid)
                                continue;
                            PatchNode::BusHead h;
                            h.bus = port.index;
                            h.head = head.slot;
                            h.gain = head.gain;
                            node.busHeads.push_back (std::move (h));
                        }
            }
            nodeOf[n] = (int) doc.nodes.size();
            doc.nodes.push_back (std::move (node));
        }
        if (graph->ioPlaced)
        {
            doc.inputX = graph->inputX * 2.0f;
            doc.inputY = graph->inputY * 1.6f;
            doc.outputX = graph->outputX * 2.0f;
            doc.outputY = graph->outputY * 1.6f;
        }
        else if (! doc.nodes.empty()) // around the nodes
        {
            float lo = doc.nodes.front().x, hi = lo, mid = 0.0f;
            for (const auto& n : doc.nodes)
            {
                lo = std::min (lo, n.x);
                hi = std::max (hi, n.x);
                mid += n.y / (float) doc.nodes.size();
            }
            doc.inputX = lo - 160.0f;
            doc.outputX = hi + 160.0f;
            doc.inputY = doc.outputY = mid;
        }

        // A port by id. FL's controller parameter ports share one id between the input (parameter k) and the output
        // (0x10000000 + k, the same control: a Control Surface's knob is both, REA_rodada171b §1.3): a cable's source
        // is the output, its destination the input (`input` picks; −1 = the first one).
        auto portOf = [&] (std::uint64_t id, int input = -1) -> const PatcherPort* {
            const PatcherPort* first = nullptr;
            for (const auto& [pid, port] : graph->ports)
                if (pid == id)
                {
                    if (input < 0 || port.node < 0 || port.input == (input == 1))
                        return &port;
                    if (first == nullptr)
                        first = &port;
                }
            return first;
        };
        // the I/O nodes' MIDI pins as FL saved them (activated even without a cable; FL's node menu › Events, round 162)
        for (const auto& [pid, port] : graph->ports)
            if (port.node < 0 && port.kind == 1)
                if (const auto number = flPatcherMidiPinPort (port); number >= 0)
                {
                    auto& list = port.input ? doc.toMidiPorts : doc.fromMidiPorts;
                    if (std::find (list.begin(), list.end(), number) == list.end())
                        list.insert (std::upper_bound (list.begin(), list.end(), number), number);
                }
        for (const auto& c : graph->cables)
        {
            const auto* src = portOf (c.source, 0);
            const auto* dst = portOf (c.destination, 1);
            if (src == nullptr || dst == nullptr)
                continue;
            PatchWire w;
            w.enabled = ! c.disabled;
            w.gain = std::isfinite (c.gain) ? c.gain : 1.0f;
            if (src->node >= 0)
            {
                if ((std::size_t) src->node >= nodeOf.size() || nodeOf[(std::size_t) src->node] < 0)
                    continue;
                w.from = nodeOf[(std::size_t) src->node];
            }
            if (dst->node >= 0)
            {
                if ((std::size_t) dst->node >= nodeOf.size() || nodeOf[(std::size_t) dst->node] < 0)
                    continue;
                w.to = nodeOf[(std::size_t) dst->node];
            }
            if (c.kind == 0)
            {
                w.kind = PatchWire::Kind::audio;
                w.fromPort = src->node < 0 ? (src->index > 0 ? 1 : 0) : std::max (0, src->index);
                if (src->node < 0 && src->index > 0)
                    patcherSidechain = src->index;
                w.toPort = dst->node < 0 ? std::max (0, dst->index) : std::min (1, std::max (0, dst->index));
                if (w.to == patchToHost && w.toPort > 0) // an extra output: "Insert N" names its insert
                {
                    while ((int) doc.outputTracks.size() < w.toPort)
                        doc.outputTracks.push_back (-1);
                    int insertNumber = -1;
                    if (dst->name.rfind ("Insert ", 0) == 0)
                        insertNumber = std::atoi (dst->name.c_str() + 7);
                    if (insertNumber > 0 && insertNumber < maxMixerTracks)
                        doc.outputTracks[(std::size_t) w.toPort - 1] = insertNumber;
                }
            }
            else if (c.kind == 1)
            {
                w.kind = PatchWire::Kind::midi;
                if (w.from == patchFromHost) // "Notes", or "MIDI port N": what reaches the Patcher on that port (round 162)
                {
                    const auto port = flPatcherMidiPinPort (*src);
                    if (port < -1)
                        continue;
                    w.fromPort = port < 0 ? 0 : patchMidiPortPin (port);
                }
                else
                {
                    // a node's MIDI output: a MIDI Out's (round 162), a VFX node's voice output N (FL's pin N, 1…;
                    // round 164b); other plugins' MIDI outputs are not passed yet
                    const auto& source = doc.nodes[(std::size_t) w.from];
                    if (! source.generator)
                        continue;
                    if (source.channel.instrument == InstrumentKind::vfx)
                        w.fromPort = std::max (0, src->index - 1);
                    else if (source.channel.instrument == InstrumentKind::midiOut)
                        w.fromPort = 0;
                    else
                        continue;
                }
                if (w.to == patchToHost) // "MIDI port N": the notes go to the plugins on that MIDI port
                {
                    const auto number = flPatcherMidiPinPort (*dst);
                    if (number < 0)
                        continue;
                    w.toPort = number;
                }
                else
                    w.toPort = 0;
            }
            else
            {
                w.kind = PatchWire::Kind::control;
                if (w.from < 0 || w.to < 0)
                    continue; // (the Patcher's own published parameters)
                const auto& source = doc.nodes[(std::size_t) w.from];
                if (src->index >= 0 && src->index < 0x10000000) // a node's parameter output (round 171b): parameter p
                {
                    const auto p = flPatcherParam (source, graph->nodes[(std::size_t) src->node].plugin, src->index);
                    if (p < 0)
                    {
                        if (options.report != nullptr)
                            ++options.report->partialEffects["Patcher: parameter output of " + graph->nodes[(std::size_t) src->node].plugin + " (not mapped)"];
                        continue;
                    }
                    w.fromPort = patchParamOutput (p);
                }
                else
                {
                    w.fromPort = src->index >= 0x10000000 ? src->index - 0x10000000 : 0;
                    // a Control Surface's outputs: FL's numbers → the panel's order (as for mixer links, round 164)
                    if (! source.generator && source.effect.kind == EffectKind::controlSurface)
                        if (const auto k = surface::flOutputToLynx (source.effect.state, w.fromPort))
                            w.fromPort = *k;
                }
                // (a Peak Controller's outputs keep FL's numbers: 0 Peak+LFO, 1 Peak, 2 LFO, as LYNX's and as the mixer links;
                // REA_rodada17 §5.1, issue #111)
                if (! isPatchParamOutput (w.fromPort) && w.fromPort >= patcher::nodePorts (source).controlOuts)
                {
                    // the source has no such output in LYNX (an X-Y controller, a Control Surface…): its target keeps the
                    // value FL saved instead of being pinned to 0
                    if (options.report != nullptr)
                        ++options.report->partialEffects["Patcher: control from " + graph->nodes[(std::size_t) src->node].plugin + " (not reproduced)"];
                    continue;
                }
                const auto& target = doc.nodes[(std::size_t) w.to];
                const auto& flTarget = graph->nodes[(std::size_t) dst->node];
                if (target.generator)
                {
                    if (target.channel.instrument != InstrumentKind::plugin)
                        continue;
                    w.toPort = dst->index;
                }
                else if (target.effect.kind == EffectKind::plugin)
                    w.toPort = dst->index;
                else if (const auto map = effectParamFromFl (canonicalFlEffectName (flTarget.plugin), dst->index))
                {
                    w.toPort = map->param;
                    if (map->convert)
                    {
                        constexpr int points = 33;
                        for (int k = 0; k < points; ++k)
                            w.curve.push_back (std::clamp (map->convert ((float) k / (float) (points - 1)), 0.0f, 1.0f));
                    }
                }
                else
                {
                    if (options.report != nullptr)
                        ++options.report->partialEffects["Patcher: control of " + flTarget.plugin + " parameter " + std::to_string (dst->index)
                                                         + " (not mapped)"];
                    continue;
                }
            }
            doc.wires.push_back (std::move (w));
        }

        // the parameter ports FL shows (round 171b): class-2 node ports with the "visible" flag, parameter p (not a
        // controller output, 0x10000000 + k) — FL's node menu › Inputs / Outputs › Parameters, ticked
        for (const auto& [pid, port] : graph->ports)
        {
            if (port.node < 0 || port.kind != 2 || (port.flags & 2) == 0 || port.index < 0 || port.index >= 0x10000000
                || (std::size_t) port.node >= nodeOf.size() || nodeOf[(std::size_t) port.node] < 0)
                continue;
            auto& node = doc.nodes[(std::size_t) nodeOf[(std::size_t) port.node]];
            const auto p = flPatcherParam (node, graph->nodes[(std::size_t) port.node].plugin, port.index);
            if (p < 0)
                continue;
            auto& list = port.input ? node.paramIns : node.paramOuts;
            if (std::find (list.begin(), list.end(), p) == list.end())
                list.insert (std::upper_bound (list.begin(), list.end(), p), p);
        }

        // the Patcher's published parameters (record 1100): Patcher parameter k → the node's parameter behind that port
        for (const auto& [portId, k] : graph->exposed)
        {
            const auto* port = portOf (portId);
            if (port == nullptr || k < 0 || k > 4096 || port->node < 0 || (std::size_t) port->node >= nodeOf.size() || nodeOf[(std::size_t) port->node] < 0
                || port->kind != 2 || ! port->input)
                continue;
            PatchParam p;
            p.node = nodeOf[(std::size_t) port->node];
            const auto& target = doc.nodes[(std::size_t) p.node];
            const auto& flTarget = graph->nodes[(std::size_t) port->node];
            if (target.generator ? target.channel.instrument == InstrumentKind::plugin : target.effect.kind == EffectKind::plugin)
                p.param = port->index;
            else if (const auto map = target.generator ? std::nullopt : effectParamFromFl (canonicalFlEffectName (flTarget.plugin), port->index))
            {
                p.param = map->param;
                if (map->convert)
                    for (int i = 0; i < 33; ++i)
                        p.curve.push_back (std::clamp (map->convert ((float) i / 32.0f), 0.0f, 1.0f));
            }
            else
                continue;
            p.name = patcher::nodeName (target) + " " + std::to_string (port->index + 1);
            if ((int) doc.published.size() <= k)
                doc.published.resize ((std::size_t) k + 1);
            doc.published[(std::size_t) k] = std::move (p);
        }
        return doc;
    };

    // generator Patchers: the channel becomes the graph (the main generator stays in it as the import made it)
    for (const auto& c : input.channels)
        if (lowercase (c.pluginName) == "patcher")
            if (const auto mapped = channelMap.find (c.index); mapped != channelMap.end() && mapped->second < (int) song.channels.size())
                if (auto doc = patcherFromFl (c.pluginState, &c); doc && std::any_of (doc->nodes.begin(), doc->nodes.end(), [] (const PatchNode& n) {
                                                                      return n.generator;
                                                                  }))
                {
                    auto& def = song.channels[(std::size_t) mapped->second];
                    def.instrument = InstrumentKind::patcher;
                    def.patcher = std::move (*doc);
                    def.plugin = {};
                    def.pluginState.clear();
                    if (options.report != nullptr)
                        ++options.report->converted["Patcher (graph)"];
                }

    for (const auto& insert : project.mixer)
    {
        if (insert.index < 0 || insert.index >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) insert.index];

        for (const auto& original : insert.slots)
        {
            // Patcher with a simple serial chain: the plugins inside become consecutive slots
            std::vector<flp::MixerInsert::Slot> expanded { original };
            // Fruity Love Philter: the units become parametric EQ + gain/pan (or a parallel container)
            // (only for states the exact engine cannot read: the others go through effectFromFl, REA_rodada66)
            if (original.plugin == "Fruity Love Philter" && (int) track.effects.size() < maxEffectSlots
                && philter::parse (original.state) == nullptr)
                if (auto conversion = lovePhilterEffects (original.state))
                {
                    const auto first = (int) track.effects.size();
                    for (auto& fx : conversion->slots)
                        if ((int) track.effects.size() < maxEffectSlots)
                            track.effects.push_back (std::move (fx));
                    effectSlotMap.insert ({ { insert.index, original.index }, { first, original.plugin } });
                    if (conversion->paramMap)
                        lovePhilterMaps[{ insert.index, original.index }] = conversion->paramMap;
                    for (const auto& link : conversion->links) // cutoff envelope/LFO: live automation
                        if (first + std::max (link.source, link.target) < (int) track.effects.size())
                        {
                            Automation a;
                            a.name = "Love Philter (" + track.name + ")";
                            a.target = { AutomationTargetKind::effectParam, insert.index, first + link.target, link.param };
                            a.source = { insert.index, first + link.source, 0 };
                            a.curve = link.curve;
                            song.automations.push_back (std::move (a));
                        }
                    if (options.report != nullptr)
                    {
                        ++options.report->converted["Fruity Love Philter"];
                        for (const auto& note : conversion->notes)
                            ++options.report->partialEffects[note];
                    }
                    continue;
                }

            // Patcher (REA_rodada141): the slot holds FL's whole graph, run as it is
            if (lowercase (original.plugin) == "patcher" && (int) track.effects.size() < maxEffectSlots)
                if (patcherSidechain = -1; auto doc = patcherFromFl (original.state, nullptr))
                {
                    auto slot = makeEffectSlot (EffectKind::patcher);
                    slot.patcher = std::move (*doc);
                    if (const auto source = sidechainSource (insert.index, patcherSidechain); source > 0 && source != insert.index)
                        slot.sidechain = source;
                    effectSlotMap.insert ({ { insert.index, original.index }, { (int) track.effects.size(), original.plugin } });
                    slot.flOrigin = { original.plugin, original.state, 0 }; // (round 171e: Export to FL Studio)
                    track.effects.push_back (std::move (slot));
                    if (options.report != nullptr)
                        ++options.report->converted["Patcher (graph)"];
                    continue;
                }

            // Patcher with branches (REA_rodada20 §3.3): the audio graph reduced to series/parallel becomes slots and
            // nested parallel containers; splitter nodes enter via the band/bus used
            if (lowercase (original.plugin) == "patcher" && (int) track.effects.size() < maxEffectSlots)
                if (const auto graph = parsePatcher (original.state); graph && graph->serialChain.empty() && graph->audio)
                {
                    std::function<std::vector<EffectSlot> (const PatcherExpr&, float&)> build = [&] (const PatcherExpr& e, float& gain) {
                        std::vector<EffectSlot> chain;
                        gain *= e.gain;
                        switch (e.kind)
                        {
                            case PatcherExpr::Kind::identity: break;
                            case PatcherExpr::Kind::node:
                            {
                                const auto& node = graph->nodes[(std::size_t) e.node];
                                if (e.bus >= 0)
                                {
                                    const auto head = patcherBranchHead (node.plugin, node.state, e.bus);
                                    if (! head.valid)
                                    {
                                        if (options.report != nullptr)
                                            ++options.report->droppedEffects["Patcher: bus " + std::to_string (e.bus) + " of " + node.plugin];
                                        break;
                                    }
                                    gain *= head.gain;
                                    if (head.slot)
                                        chain.push_back (*head.slot);
                                    if (! head.note.empty() && options.report != nullptr)
                                        ++options.report->partialEffects[head.note];
                                }
                                else if (auto fx = effectFromFl (node.plugin, node.state, song.bpm, flData); fx && fx->kind != EffectKind::plugin)
                                {
                                    noteFallback (node.plugin, *fx);
                                    chain.push_back (std::move (*fx));
                                }
                                else if (options.report != nullptr)
                                    ++options.report->droppedEffects[node.plugin + " (in Patcher)"];
                                break;
                            }
                            case PatcherExpr::Kind::series:
                                for (const auto& c : e.children)
                                    for (auto& slot : build (c, gain))
                                        chain.push_back (std::move (slot));
                                break;
                            case PatcherExpr::Kind::parallel:
                            {
                                auto container = makeEffectSlot (EffectKind::parallel);
                                for (const auto& c : e.children)
                                {
                                    EffectBranch branch;
                                    auto branchGain = 1.0f;
                                    branch.chain = build (c, branchGain);
                                    branch.gain = std::clamp (branchGain, 0.0f, 4.0f);
                                    container.branches.push_back (std::move (branch));
                                }
                                chain.push_back (std::move (container));
                                break;
                            }
                        }
                        return chain;
                    };
                    auto gain = 1.0f;
                    auto chain = build (*graph->audio, gain);
                    if (std::abs (gain - 1.0f) > 1e-4f)
                    {
                        auto utility = makeEffectSlot (EffectKind::utility);
                        utility.params[0] = gain <= 0.0f ? 0.0f : std::clamp ((float) (20.0 * std::log10 (gain) / 48.0 + 0.5), 0.0f, 1.0f);
                        chain.push_back (std::move (utility));
                    }
                    if ((int) (track.effects.size() + chain.size()) > maxEffectSlots) // doesn't fit: a single-branch container
                    {
                        auto container = makeEffectSlot (EffectKind::parallel);
                        container.branches.push_back ({ std::move (chain), 1.0f });
                        chain = { std::move (container) };
                    }
                    effectSlotMap.insert ({ { insert.index, original.index }, { (int) track.effects.size(), original.plugin } });
                    for (auto& slot : chain)
                        track.effects.push_back (std::move (slot));
                    if (options.report != nullptr)
                        ++options.report->converted["Patcher (series/parallel)"];
                    continue;
                }

            if (lowercase (original.plugin) == "patcher")
                if (const auto graph = parsePatcher (original.state); graph && ! graph->serialChain.empty())
                {
                    expanded.clear();
                    for (const auto n : graph->serialChain)
                        expanded.push_back ({ original.index, graph->nodes[(std::size_t) n].plugin, graph->nodes[(std::size_t) n].state });
                    if (options.report != nullptr)
                        ++options.report->converted["Patcher (in series)"];
                }

            for (const auto& slot : expanded)
            {
                if ((int) track.effects.size() >= maxEffectSlots)
                    continue;

                ForeignPlugin foreign;
                std::optional<EffectSlot> made;

                const auto wrapped = slot.plugin == "Fruity Wrapper" && parseFlWrapper (slot.state, foreign);

                // old FL saved its own Fruity effects as VST2 inside the wrapper: becomes the internal effect
                if (wrapped && foreign.format == ForeignPlugin::Format::vst2 && effectForFlPlugin (foreign.name))
                    made = effectFromFl (canonicalFlEffectName (foreign.name), wrappedFruityState (foreign), song.bpm);
                else if (wrapped)
                {
                    // the app swaps in the installed plugin; without it, the slot is removed
                    made = makeEffectSlot (EffectKind::plugin);
                    made->plugin.name = foreign.name;
                    applyFlWrapperOptions (foreign, made->plugin);
                    // Processing › Connections (round 188d): the side input (input 2) listens to the insert its
                    // sidechain index picks in this insert's inputs list
                    if (made->plugin.inputs.size() > 1 && made->plugin.inputs[1].offset > 0 && made->plugin.inputs[1].isActive (true))
                        if (const auto source = sidechainSource (insert.index, made->plugin.inputs[1].offset); source > 0 && source != insert.index)
                            made->sidechain = source;
                    made->plugin.foreign = std::move (foreign);
                    made->plugin.detached = flDetached (slot.wrapperFlags); // (0xD4 flag 0x4, issue #116)
                }
                else
                {
                    made = effectFromFl (slot.plugin, slot.state, song.bpm, flData); // native effect with the saved settings (REA_rodada7)
                    if (made)
                        noteFallback (slot.plugin, *made);
                }

                if (! made)
                {
                    // the ones that don't touch audio (controllers, visualizers, notes) aren't missed
                    static const char* silent[] = { "Fruity Peak Controller", "Fruity X-Y Controller", "Control Surface",
                                                    "Fruity NoteBook 2", "Fruity NoteBook", "Edison",
                                                    "Fruity Formula Controller", "Fruity LSD" };
                    const auto harmless = std::any_of (std::begin (silent), std::end (silent), [&] (const char* n) { return slot.plugin == n; });

                    // FL plugins with nothing to reproduce here (vendor lighting, discontinued bridges): the note says why (round 165g)
                    if (const auto note = flextra::inertPluginNote (slot.plugin); ! note.empty())
                    {
                        if (options.report != nullptr)
                            ++options.report->droppedEffects[note];
                        continue;
                    }
                    if (options.report != nullptr && ! harmless)
                        ++options.report->droppedEffects[slot.plugin == "Fruity Wrapper" ? std::string ("Fruity Wrapper (plugin state not readable)")
                                                                                          : slot.plugin];
                    continue;
                }

                auto fx = std::move (*made);
                // FL's per-plugin "Smart disable" (0xD4 flag 0x20, REA round 139): off = processed even in silence
                if (slot.wrapperFlags >= 0)
                    fx.smartDisable = (slot.wrapperFlags & 0x20) != 0;

                // sidechain (REA_rodada22 §1): Fruity Limiter (i32 at 0xA4, v ≥ 6; only the compressor detector and only
                // with Comp threshold < 0 dB) and Fruity Compressor (i32 at 0x20, v3)
                if (slot.plugin == "Fruity Limiter" && slot.state.size() >= 0xA8)
                {
                    std::int32_t version = 0, k = 0, compThreshold = 1000;
                    std::memcpy (&version, slot.state.data(), 4);
                    std::memcpy (&k, slot.state.data() + 0xA4, 4);
                    std::memcpy (&compThreshold, slot.state.data() + 4 + 8 * 4, 4);
                    const auto source = version >= 6 ? sidechainSource (insert.index, k) : -1;
                    if (fx.kind == EffectKind::fruityLimiter)
                    {
                        // the exact engine: the side input feeds its compression detector (REA_rodada48)
                        fx.sidechain = source;
                        if (source >= 0 && compThreshold < 1000 && options.report != nullptr)
                            ++options.report->converted["Fruity Limiter (sidechain)"];
                    }
                    else if (source >= 0 && compThreshold < 1000)
                        if (auto comp = fruityLimiterCompressor (slot.state))
                        {
                            const auto enabled = fx.enabled;
                            fx = std::move (*comp);
                            fx.enabled = enabled;
                            fx.sidechain = source;
                            if (options.report != nullptr)
                                ++options.report->converted["Fruity Limiter (sidechain)"];
                        }
                }
                else if (slot.plugin == "LuxeVerb" && fx.kind == EffectKind::luxeVerb && slot.state.size() >= 8)
                {
                    // the state's second i32 is the sidechain input the envelope can follow (REA_rodada60 §3.1)
                    std::int32_t k = 0;
                    std::memcpy (&k, slot.state.data() + 4, 4);
                    if (k > 0)
                        fx.sidechain = sidechainSource (insert.index, k);
                }
                else if (slot.plugin == "Vocodex")
                {
                    // ModIn/CarIn index the same inputs list as the sidechains (REA_rodada22 §1)
                    if (const auto v = vocodexEffect (slot.state, flData))
                    {
                        fx.sidechain = sidechainSource (insert.index, v->modIn);
                        fx.sidechain2 = sidechainSource (insert.index, v->carIn);
                        if (options.report != nullptr)
                            for (const auto& note : v->notes)
                                ++options.report->partialEffects[note];
                    }
                }
                else if (fx.kind == EffectKind::scratcher)
                {
                    // the turntable's sample: FL's path with its macros (%FLStudioFactoryData%…), else the user's relocation
                    if (const auto st = scratcher::parse (slot.state))
                    {
                        const auto wanted = st->samplePath.empty() ? std::string() : resolveFlPath (st->samplePath, options.roots);
                        if (! wanted.empty() && options.fileExists && options.fileExists (wanted))
                            fx.samplePath = wanted;
                        else if (const auto chosen = relocatedPath (options.relocations, wanted);
                                 ! wanted.empty() && ! chosen.empty() && options.fileExists && options.fileExists (chosen))
                            fx.samplePath = chosen;
                        if (options.report != nullptr)
                        {
                            if (! wanted.empty() && fx.samplePath.empty())
                            {
                                ++options.report->missingSamples;
                                addMissingFile (options.report->missingFiles, wanted,
                                                (insert.name.empty() ? "Insert " + std::to_string (insert.index) : insert.name) + ": " + slot.plugin);
                            }
                            if (st->timecode != -1 && ! dvs::supported (st->timecode))
                                ++options.report->partialEffects["Fruity Scratcher (Traktor Mk2 / Timecode CD timecode not decoded: the platter stays silent)"];
                            else
                                ++options.report->converted[slot.plugin];
                        }
                    }
                }
                else if (fx.kind == EffectKind::flTransporter)
                {
                    // the detector follows FL's input track when one is chosen (the inputs list, like the Limiter's); the
                    // loops' send goes to the mixer insert of that number: the plug-in asks the host for the send buffer
                    // with the same call and index as Fruity Send (REA_rodada167n §1, REA_rodada105_send §2)
                    if (const auto st = transporter::parse (slot.state))
                    {
                        fx.sidechain = sidechainSource (insert.index, st->inputTrack);
                        const auto send = st->sendTrack;
                        fx.params[(std::size_t) transporter::sendParam] =
                            send > 0 && send < tracks && send != insert.index ? (float) send / (float) transporter::rawMax[transporter::sendParam] : 0.0f;
                    }
                }
                else if (fx.kind == EffectKind::equo)
                {
                    // the band sends go to the insert FL's send box names: the plug-in hands them to the host with the
                    // insert number, the same call as Fruity Send's (round 173d3, REA_rodada61 §2.1)
                    const auto send = equofx::sendInsert (fx.state);
                    fx.sendTracks[0] = send > 0 && send < tracks && send != insert.index ? send : 0;
                }
                else if (fx.kind == EffectKind::transmitter)
                {
                    // the detection follows FL's sidechain input when one is chosen (the inputs list, like the Limiter's);
                    // the transient / sustain parts go to the inserts FL's send menus name (the insert number, like the
                    // Fruity Send's destination: REA_rodada167b §1)
                    if (const auto st = transmitter::parse (slot.state))
                    {
                        fx.sidechain = sidechainSource (insert.index, st->sidechain);
                        const int sends[2] = { st->transientSend, st->sustainSend };
                        for (int o = 0; o < 2; ++o)
                            fx.sendTracks[(std::size_t) o] = sends[o] > 0 && sends[o] < tracks && sends[o] != insert.index ? sends[o] : 0;
                    }
                }
                else if (fx.kind == EffectKind::newtone || fx.kind == EffectKind::newtime)
                {
                    // the source sample: the path the host stored (record 3), else the engine's working copy (REA_rodada73 §2.3)
                    if (const auto parsed = newtonefx::parse (slot.state))
                    {
                        std::vector<std::string> candidates;
                        if (! parsed->sourcePath.empty())
                            candidates.push_back (resolveFlPath (parsed->sourcePath, options.roots));
                        for (const auto& t : parsed->tracks)
                            candidates.push_back (t.filePath);
                        for (const auto& path : candidates)
                            if (! path.empty() && options.fileExists && options.fileExists (path))
                            {
                                fx.samplePath = path;
                                break;
                            }
                        for (const auto& path : candidates) // located by the user (the "Missing files" window)
                            if (const auto chosen = relocatedPath (options.relocations, path);
                                fx.samplePath.empty() && ! chosen.empty() && options.fileExists && options.fileExists (chosen))
                                fx.samplePath = chosen;
                        if (options.report != nullptr && ! parsed->tracks.empty())
                        {
                            if (fx.samplePath.empty())
                            {
                                ++options.report->missingSamples;
                                if (! candidates.empty() && ! candidates.front().empty())
                                    addMissingFile (options.report->missingFiles, candidates.front(),
                                                    (insert.name.empty() ? "Insert " + std::to_string (insert.index) : insert.name) + ": " + slot.plugin);
                            }
                            // Newtone's note edits and Newtime's warp markers are rendered by LYNX's engines (REA_rodada95,
                            // REA_rodada167b: close, not sample-exact). Only slave mode plays them (otherwise FL passes the
                            // input through).
                            ++options.report->converted[slot.plugin];
                        }
                    }
                }
                else if (slot.plugin == "Fruity Compressor" && slot.state.size() >= 0x24
                         && (fx.kind == EffectKind::compressor || fx.kind == EffectKind::fruityCompressor))
                {
                    std::int32_t version = 0, k = 0;
                    std::memcpy (&version, slot.state.data(), 4);
                    std::memcpy (&k, slot.state.data() + 0x20, 4);
                    if (version == 3)
                        fx.sidechain = sidechainSource (insert.index, k);
                }

                // slot bypass and dry/wet (0xE1, p = 0x1F00 / 0x1F01)
                for (const auto& control : project.controls)
                    if (control.id.isMixer() && control.id.track() == insert.index && control.id.slot() == slot.index)
                    {
                        if (control.id.param() == 0x1F00)
                            fx.enabled = control.value != 0;
                        else if (control.id.param() == 0x1F01)
                            fx.mix = std::clamp (control.value / 12800.0f, 0.0f, 1.0f);
                    }

                effectSlotMap.insert ({ { insert.index, slot.index }, { (int) track.effects.size(), slot.plugin } });
                if (slot.index >= 0 && slot.index < maxEffectSlots)
                    fx.row = slot.index; // FL's slot place (round 170b): an effect in slot 5 stays in slot 5
                // Export to FL Studio (round 171e): the FL effect itself, written back while LYNX leaves it as imported (one
                // FL slot = one LYNX slot here; a hosted plugin travels as its plugin reference)
                if (fx.kind != EffectKind::plugin && expanded.size() == 1)
                    fx.flOrigin = { slot.plugin, slot.state, 0 };
                track.effects.push_back (fx);
            }
        }
    }

    // cycles shouldn't exist in the file; if they do, the send that closes the cycle is dropped
    for (int t = 1; t < tracks; ++t)
    {
        auto sends = std::move (song.mixer[(std::size_t) t].sends);
        song.mixer[(std::size_t) t].sends.clear();

        for (const auto& s : sends)
            song.setRoute (t, s.target, true, s.level);
    }

    for (const auto& control : project.controls)
    {
        if (! control.id.isMixer())
            continue;

        const auto t = control.id.track();
        const auto p = control.id.param();

        if (t < 0 || t >= tracks)
            continue;

        auto& track = song.mixer[(std::size_t) t];

        // FL ranges: volume 0..16000 (12800 = 0 dB), pan ±6400, route levels 0..16000 on the fader curve too
        // (12800 = 0 dB, 16000 = +5.6 dB; destination 0 = the route to Master). REA_rodada107 §3: FL renders with
        // a route level of 16000 / 12800 / 6400 differ by +5.6 / 0 / −12.7 dB, the fader curve's values.
        if (p == flp::ControlId::mixerVolume)
            track.volume = std::clamp (control.value / 16000.0f, 0.0f, 1.0f);
        else if (p == flp::ControlId::mixerPan)
            track.pan = std::clamp (control.value / 6400.0f, -1.0f, 1.0f);
        else if (p == flp::ControlId::mixerStereo)
            track.stereo = std::clamp (control.value / 64.0f, -1.0f, 1.0f);
        else if (p >= flp::ControlId::mixerEqBase && p < flp::ControlId::mixerEqBase + 24 && (p - flp::ControlId::mixerEqBase) % 8 < 3)
        {
            // track EQ: p = 0x1FD0 + group·8 + band (group 0 gain, 1 frequency, 2 width; REA_rodada6 §1)
            const auto group = (p - flp::ControlId::mixerEqBase) / 8;
            const auto band = (std::size_t) ((p - flp::ControlId::mixerEqBase) % 8);
            const auto v = (double) control.value;

            if (group == 0)
                track.eq.gainDb[band] = (float) std::clamp (v * 0.01, -18.0, 18.0);
            else if (group == 1)
                track.eq.hz[band] = (float) std::clamp (10.0 + 159.9 * (std::pow (101.0, v / 65536.0) - 1.0), 10.0, 16000.0);
            else
                track.eq.width[band] = (float) std::clamp (v / 65536.0, 0.0, 1.0);
        }
        else if (p == flp::ControlId::mixerSendBase)
            track.masterLevel = faderGain (control.value / 16000.0f);
        else if (p > flp::ControlId::mixerSendBase && p < flp::ControlId::mixerSendBase + tracks)
            for (auto& send : track.sends)
                if (send.target == p - flp::ControlId::mixerSendBase)
                    send.level = faderGain (control.value / 16000.0f);
    }

    // the FL template routes every insert to the send tracks at level 0; that makes no sound, only wastes CPU
    for (auto& t : song.mixer)
        t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(), [] (const MixerSend& s) { return s.level <= 0.0f; }),
                       t.sends.end());

    const auto filePpq = project.header.ppq > 0 ? (std::int64_t) project.header.ppq : 96;
    auto rescale = [filePpq] (std::int64_t ticks) { return ticks * ppq / filePpq; };
    auto importMarker = [&rescale] (const flp::TimeMarker& m) {
        Marker marker;
        marker.tick = rescale (m.position);
        marker.name = m.name;
        marker.name.erase (0, marker.name.find_first_not_of (' '));
        marker.name.erase (marker.name.find_last_not_of (' ') + 1);

        if (m.type != 8 && m.type > 0 && m.type < 256)
            marker.action = m.type; // FL's marker action (9 Start recording, 10 Stop recording, 11 Loop length, ...)
        if (m.type == 8 && m.numerator >= 1 && m.numerator <= 32 && m.denominator >= 1 && m.denominator <= 32)
        {
            marker.numerator = m.numerator;
            marker.denominator = m.denominator;
        }
        return marker;
    };

    std::map<int, int> patternMap; // FL index -> our dense index

    for (const auto& src : project.patterns)
    {
        Pattern p;
        p.name = src.name.empty() ? "Pattern " + std::to_string (src.index) : src.name;

        if (src.colour && *src.colour != 0x485156) // FL's default gray becomes an automatic colour
            p.colour = 0xFF000000u | *src.colour;

        // the Channel Rack's looping (round 165f-rack): FL keeps the loop points of every channel, by FL index
        // the Piano roll's pattern markers (round 168g)
        for (const auto& m : src.markers)
            p.markers.push_back (importMarker (m));
        std::stable_sort (p.markers.begin(), p.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });

        p.loopOn = src.loopOn;
        // the Channel Rack's pattern length selector (0xA4, REA round 168j): a fixed length, else automatic
        p.lengthTicks = src.fixedLength > 0 ? rescale ((std::int64_t) src.fixedLength) : 0;
        p.loopMode = (RackLoopMode) std::clamp (src.loopMode, 1, 3);
        for (const auto& [flChannel, setting] : src.channelLoops)
            if (const auto it = channelMap.find (flChannel); it != channelMap.end())
                if (std::none_of (p.channelLoops.begin(), p.channelLoops.end(), [&] (const ChannelLoop& l) { return l.channel == it->second; }))
                    p.channelLoops.push_back ({ it->second, std::max (setting, -3) });

        for (const auto& n : src.notes)
        {
            const auto it = channelMap.find (n.channel);

            if (it == channelMap.end())
                continue;

            Note note;
            note.start = rescale (n.position);
            // Step notes (length 0) play for one step.
            note.length = n.length > 0 ? rescale (n.length) : ticksPerStep;
            note.channel = it->second;
            note.key = n.key; // FL and MIDI use the same numbering; only the octave name differs
            note.velocity = std::clamp (n.velocity / 128.0f, 0.0f, 1.0f);
            note.pan = std::clamp ((n.pan - 64) / 64.0f, -1.0f, 1.0f);
            note.finePitch = (float) std::clamp ((int) n.finePitch - 120, -120, 120);
            note.muted = n.muted();
            note.slide = n.slide();
            note.porta = n.porta();
            note.colour = n.extra & 0x0F;
            note.group = n.group;
            p.notes.push_back (note);
        }

        p.sortNotes();

        if (! p.notes.empty() || ! src.controlEvents.empty() || ! p.markers.empty()) // a pattern of events (or markers) only stays
        {
            patternMap[src.index] = (int) song.patterns.size();
            song.patterns.push_back (std::move (p));
        }
    }

    // Our target for an FL controlID: the FL slot (0..9, with gaps) becomes our effect's
    // index (effect not imported = no target); internal effect parameter only with the FL-to-ours
    // map (`valueMap` receives the value conversion).
    auto resolveTarget = [&] (flp::ControlId id, std::function<float (float)>& valueMap) {
        auto target = automationTargetFor (id, channelMap, (int) song.mixer.size());

        // FL Soundfont Player → LYNX Soundfont: FL's P index and scale → LYNX's parameter (REA_rodada158 §1)
        if (target.kind == AutomationTargetKind::channelParam && target.index >= 0 && target.index < (int) song.channels.size()
            && song.channels[(std::size_t) target.index].instrument == InstrumentKind::soundfont)
        {
            const auto flIndex = target.param;
            if (const auto mapped = sf::automationFromFl (flIndex, 0.0f))
            {
                target.param = mapped->first;
                valueMap = [flIndex] (float v) { const auto m = sf::automationFromFl (flIndex, v); return m ? m->second : 0.0f; };
            }
            else
                target.kind = AutomationTargetKind::none;
        }

        // round 165: FL's Dashboard parameter p ≥ 1 = record p − 1 = LYNX's control p − 1 (its output); 0 is the patch
        // selector (not a LYNX target). The Voltage Controller's 0…15 (offsets, scales) are LYNX's own numbers.
        if (target.kind == AutomationTargetKind::channelParam && target.index >= 0 && target.index < (int) song.channels.size())
        {
            const auto& channel = song.channels[(std::size_t) target.index];
            if (channel.instrument == InstrumentKind::dashboard)
            {
                const auto k = target.param - 1;
                if (k >= 0 && k < (int) channel.patcher.surface.size())
                    target.param = surface::firstOutput (channel.patcher.surface, k);
                else
                    target.kind = AutomationTargetKind::none;
            }
            else if (channel.instrument == InstrumentKind::voltage && (target.param < 0 || target.param >= voltage::paramCount))
                target.kind = AutomationTargetKind::none;
        }

        if (target.kind == AutomationTargetKind::effectParam || target.kind == AutomationTargetKind::effectMix)
        {
            const auto found = effectSlotMap.find ({ target.index, target.slot });
            if (found == effectSlotMap.end())
                target.kind = AutomationTargetKind::none;
            else
            {
                target.slot = found->second.first;
                const auto& fx = song.mixer[(std::size_t) target.index].effects[(std::size_t) target.slot];

                if (target.kind == AutomationTargetKind::effectParam && found->second.second == "Fruity Love Philter"
                    && fx.kind != EffectKind::lovePhilter)
                {
                    const auto map = lovePhilterMaps.find ({ id.track(), id.slot() });
                    const auto mapped = map != lovePhilterMaps.end() && map->second ? map->second (target.param) : std::nullopt;
                    if (mapped)
                    {
                        target.slot = found->second.first + std::get<0> (*mapped);
                        target.param = std::get<1> (*mapped);
                        valueMap = std::get<2> (*mapped);
                    }
                    else
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && fx.kind == EffectKind::lovePhilter)
                {
                    // the exact Love Philter exposes 48 of FL's controls as (raw − min) / range (core/LovePhilter.h)
                    target.param = philter::paramOfFlIndex (target.param);
                    if (target.param < 0)
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && fx.kind == EffectKind::luxeVerb)
                {
                    // the exact LuxeVerb keeps FL's 49 controls as (raw − min) / range: FL's normalised value as is
                    if (target.param < 0 || target.param >= luxeVerbControlCount)
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && fx.kind == EffectKind::convolver)
                {
                    // the exact Convolver keeps FL's 4 automatable controls as (raw − min) / range
                    if (target.param < 0 || target.param >= convolverParamCount)
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && (fx.kind == EffectKind::reeverb2 || fx.kind == EffectKind::reeverb1))
                {
                    // the exact Reeverbs keep FL's controls as (raw − min) / range: FL's normalised value as is
                    if (target.param < 0 || target.param >= (fx.kind == EffectKind::reeverb2 ? 15 : reeverb1ParamCount))
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && fx.kind == EffectKind::patcher)
                {
                    // the Patcher's own parameter k: one it publishes (REA_rodada141), FL's normalised value as is
                    if (target.param < 0 || target.param >= (int) fx.patcher.published.size() || fx.patcher.published[(std::size_t) target.param].node < 0)
                        target.kind = AutomationTargetKind::none;
                }
                else if (target.kind == AutomationTargetKind::effectParam && fx.kind != EffectKind::plugin)
                {
                    if (const auto mapped = effectParamFromFl (found->second.second, target.param))
                    {
                        target.param = mapped->param;
                        valueMap = mapped->convert;
                    }
                    else
                        target.kind = AutomationTargetKind::none;
                }
            }
        }
        return target;
    };

    // FL's time map starts at the initialized Tempo (0xD8) when there is one, not at the project's tempo (REA_rodada178a
    // §3; Secret Agent Rendezvous: 110 vs 120)
    std::optional<float> initialTempo;

    // Pattern events (.flp 0xDF, REA_rodada85): per pattern and target, FL's raw values held from their tick on.
    // They become the pattern's event lanes (round 83) in FL's normalised scale, then our target's (valueMap).
    {
        auto pluginOf = [&] (flp::ControlId id) -> std::string {
            if (id.isChannel())
            {
                for (const auto& c : project.channels)
                    if (c.index == id.channel())
                        return c.pluginName;
            }
            else if (id.isMixer())
            {
                const auto found = effectSlotMap.find ({ id.track(), id.slot() });
                if (found != effectSlotMap.end())
                    return found->second.second;
            }
            return {};
        };
        auto note = [&] (const std::string& what) {
            if (options.report != nullptr)
                ++options.report->unsupported["Pattern events: " + what];
        };
        // FL value → our 0..1 for a target (nothing when the target or its scale is not reproduced)
        auto convertor = [&] (flp::ControlId id, AutomationTarget& target) -> std::function<float (std::int32_t)> {
            std::function<float (float)> valueMap;
            target = resolveTarget (id, valueMap);
            const auto plugin = pluginOf (id);
            if (target.kind == AutomationTargetKind::tempo)
            {
                // FL's Tempo events (issue #86, REA_rodada178a §2): BPM × 1000 (10 000…522 000), whole BPM in files
                // saved before FL 10.1.0 (FL's loader multiplies them, FUN_00f45960) — LYNX's tempo scale exactly
                const auto oldFile = project.versionNumber > 0 && project.versionNumber < 10001000u;
                return [oldFile] (std::int32_t raw) {
                    const auto milli = oldFile ? (double) raw * 1000.0 : (double) raw;
                    return (float) std::clamp ((milli - 10000.0) / 512000.0, 0.0, 1.0);
                };
            }
            if (target.kind == AutomationTargetKind::none)
            {
                note ("target not reproduced" + (plugin.empty() ? std::string() : " (" + plugin + ")"));
                return {};
            }
            const auto range = flRawRange (id, plugin);
            if (! range || range->second <= range->first)
            {
                note ("value scale not known (" + (plugin.empty() ? std::string ("FL parameter") : plugin) + ")");
                return {};
            }
            const auto lo = (double) range->first, span = (double) (range->second - range->first);
            return [lo, span, valueMap] (std::int32_t raw) {
                auto v = (float) std::clamp (((double) raw - lo) / span, 0.0, 1.0);
                return valueMap ? std::clamp (valueMap (v), 0.0f, 1.0f) : v;
            };
        };

        for (const auto& src : project.patterns)
        {
            const auto pm = patternMap.find (src.index);
            if (src.controlEvents.empty() || pm == patternMap.end())
                continue;

            auto& pattern = song.patterns[(std::size_t) pm->second];
            std::vector<std::uint32_t> ids; // targets in order of first appearance
            for (const auto& e : src.controlEvents)
                if (std::find (ids.begin(), ids.end(), e.id.raw) == ids.end())
                    ids.push_back (e.id.raw);

            for (const auto raw : ids)
            {
                PatternEvents lane;
                const auto convert = convertor (flp::ControlId { raw }, lane.target);
                if (! convert)
                    continue;

                for (const auto& e : src.controlEvents)
                    if (e.id.raw == raw)
                        lane.events.push_back ({ rescale ((std::int64_t) e.tick), convert (e.value) });

                // one event per tick: the last one of a tick wins (FL applies them in order)
                std::stable_sort (lane.events.begin(), lane.events.end(),
                                  [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
                std::vector<PatternEvent> kept;
                for (const auto& e : lane.events)
                    if (! kept.empty() && kept.back().tick == e.tick)
                        kept.back() = e;
                    else
                        kept.push_back (e);
                lane.events = std::move (kept);

                // a target already driven by a lane of this pattern: the later lane wins, like FL's single target
                pattern.events.erase (std::remove_if (pattern.events.begin(), pattern.events.end(),
                                                      [&] (const PatternEvents& l) { return l.target == lane.target; }),
                                      pattern.events.end());
                pattern.events.push_back (std::move (lane));
                if (options.report != nullptr)
                    ++options.report->converted["Pattern events"];
            }
        }

        // 0xD8: values FL resets when the song starts — the targets' own values before any pattern event
        for (const auto& c : project.initControls)
        {
            AutomationTarget target;
            if (const auto convert = convertor (c.id, target))
            {
                const auto value = convert (c.value);
                if (target.kind == AutomationTargetKind::tempo)
                    initialTempo = value; // (applied at the end: the audio clips' millisecond offsets use the file's tempo)
                else
                    song.setTargetValue (target, value);
                song.setInitialValue (target, value); // kept: reset whenever the song starts (REA round 146)
            }
        }
    }

    // an FL point's curve (the segment that ends at it) onto LYNX's point where that segment starts
    const auto flCurveOnto = [] (AutomationPoint& previous, const flp::AutomationPoint& p) {
        previous.mode = p.curve < flcurve::modeCount ? p.curve : (std::uint8_t) flcurve::singleCurve;
        previous.tension = std::isfinite (p.tension) ? std::clamp (0.0f - p.tension, -1.0f, 1.0f) : 0.0f;
    };

    // Automation clips: one Automation per FL automation channel, with the target from the 0xE3 link.
    std::map<int, int> automationMap; // FL channel -> index into song.automations

    for (const auto& c : project.channels)
    {
        if (c.type != flp::ChannelType::automationClip || c.automation.empty())
            continue;

        Automation a;
        a.name = c.name.empty() ? "Automation " + std::to_string (c.index + 1) : c.name;
        std::function<float (float)> valueMap; // FL normalized value -> ours (effect parameters)

        for (const auto& link : project.automationLinks)
            if (link.channel == c.index)
            {
                a.target = resolveTarget (link.target, valueMap);
                break;
            }

        // the clip's range (round 171e): its channel levels, pan = minimum and volume = maximum of 12800 in the target's
        // whole range (FL's tempo clips: 1250…4250 = 60…180 of FL's 10…522 BPM); a file without them: the whole range,
        // and the tempo FL's usual 60…180
        const auto ranged = c.hasLevels;
        const auto lo = ranged ? std::clamp (c.pan / 12800.0, 0.0, 1.0) : 0.0;
        const auto hi = ranged ? std::clamp (c.volume / 12800.0, 0.0, 1.0) : 1.0;
        for (std::size_t i = 0; i < c.automation.size(); ++i)
        {
            const auto& p = c.automation[i];
            const auto tick = (std::int64_t) std::llround (p.position * ppq);
            auto value = (float) (lo + (hi - lo) * std::clamp (p.value, 0.0, 1.0));

            // our tempo target is FL's whole tempo range
            if (a.target.kind == AutomationTargetKind::tempo)
            {
                if (! ranged)
                    value = valueFromTempo (60.0 + 120.0 * std::clamp (p.value, 0.0, 1.0));
            }
            else if (valueMap)
                value = std::clamp (valueMap (value), 0.0f, 1.0f);

            // FL's curve mode and tension belong to the segment ending at the point; ours sit on the left point, the
            // tension with the opposite sign (round 190bw: every mode, played by FL's own law)
            if (i > 0)
                flCurveOnto (a.points.back(), p);

            a.points.push_back ({ tick, value, 0.0f });
        }

        // FL's clip LFO (round 177b, REA FUN_00fe8640 / FUN_00ba1700): kept whole, so the file writes back byte for byte
        if (const auto& f = c.automationLfo; f.present || f.level != 64 || f.mode != 0)
        {
            auto& lfo = a.lfo;
            lfo.on = f.enabled != 0;
            lfo.level = f.level;
            lfo.add = f.mode == 1;
            lfo.flags = f.flags;
            lfo.speed = f.speed;
            lfo.tension = f.tension;
            lfo.skew = f.skew;
            lfo.pulseWidth = f.pulseWidth;
            lfo.phase = f.phase;
            lfo.range = (float) (hi - lo);
            lfo.amp.clear();
            for (std::size_t i = 0; i < f.points.size(); ++i) // (the same curve rules as the clip's points)
            {
                const auto& p = f.points[i];
                const auto tick = (std::int64_t) std::llround (p.position * ppq);
                if (i > 0)
                    flCurveOnto (lfo.amp.back(), p);
                lfo.amp.push_back ({ tick, (float) std::clamp (p.value, 0.0, 1.0), 0.0f });
            }
            if (lfo.amp.empty())
                lfo.amp.push_back ({ 0, 1.0f, 0.0f });
            lfo.fl.present = f.present;
            lfo.fl.mainFlags = f.mainFlags;
            lfo.fl.mainEnabled = f.mainEnabled;
            lfo.fl.mainLoop = f.mainLoop;
            lfo.fl.adsr = f.adsr;
            lfo.fl.lfoLoop = f.lfoLoop;
            lfo.fl.rawPoints = f.rawPoints;
            lfo.fl.rawAmp = lfo.amp;
        }

        automationMap[c.index] = (int) song.automations.size();
        song.automations.push_back (std::move (a));
    }

    // Controller links (REA_rodada14 §7): Peak Controller in a mixer slot -> live automation of the
    // target (the effect output is read every block). Other controllers are still left out.
    for (const auto& link : project.controllerLinks)
    {
        // Envelope Controller on a channel: output k follows articulator k
        if (link.source.isChannel())
        {
            const auto channel = channelMap.find (link.source.channel());
            if (channel == channelMap.end()
                || (song.channels[(std::size_t) channel->second].instrument != InstrumentKind::controller
                    && song.channels[(std::size_t) channel->second].instrument != InstrumentKind::dashboard))
                continue;
            const auto& sourceChannel = song.channels[(std::size_t) channel->second];
            auto output = link.source.param() - flp::ControlId::pluginParamBase;
            if (sourceChannel.instrument == InstrumentKind::dashboard) // FL's output number → the panel's order
            {
                const auto k = surface::flDashboardOutputToLynx (sourceChannel.pluginState, output);
                if (! k)
                    continue;
                output = *k;
            }
            Automation a;
            std::function<float (float)> valueMap;
            a.target = resolveTarget (link.target, valueMap);
            if (a.target.kind == AutomationTargetKind::none || a.target.kind == AutomationTargetKind::tempo)
                continue;
            a.name = "Controller (" + song.channels[(std::size_t) channel->second].name + ")";
            a.source.channel = channel->second;
            a.source.output = output;
            if (valueMap)
                for (int i = 0; i <= 32; ++i)
                    a.curve.push_back (std::clamp (valueMap ((float) i / 32.0f), 0.0f, 1.0f));
            song.automations.push_back (std::move (a));
            if (options.report != nullptr)
                ++options.report->converted["Channel controller (link)"];
            continue;
        }

        if (! link.source.isMixer() || link.source.track() < 0 || link.source.track() >= (int) song.mixer.size())
            continue;

        // an X-Y (-Z) Controller / Control Surface imported as LYNX's own (round 164): a live link (below)
        const auto liveSlot = effectSlotMap.find ({ link.source.track(), link.source.slot() });
        const auto liveKind = liveSlot != effectSlotMap.end() ? song.mixer[(std::size_t) link.source.track()].effects[(std::size_t) liveSlot->second.first].kind
                                                              : EffectKind::eq;
        const auto newController = xy::isXyController (liveKind) || liveKind == EffectKind::controlSurface;

        // static X-Y Controller / Control Surface (not imported): the current value is applied to the target once
        if (! newController)
        {
            const flp::MixerInsert::Slot* source = nullptr;
            for (const auto& insert : project.mixer)
                if (insert.index == link.source.track())
                    for (const auto& slot : insert.slots)
                        if (slot.index == link.source.slot())
                            source = &slot;
            if (source != nullptr)
                if (const auto value = controllerConstantOutput (source->plugin, source->state, link.source.param() - flp::ControlId::pluginParamBase))
                {
                    std::function<float (float)> valueMap;
                    const auto target = resolveTarget (link.target, valueMap);
                    if (target.kind != AutomationTargetKind::none && target.kind != AutomationTargetKind::tempo
                        && song.setTargetValue (target, valueMap ? valueMap (*value) : *value) && options.report != nullptr)
                        ++options.report->converted[source->plugin + " (link)"];
                    continue;
                }
        }

        const auto found = effectSlotMap.find ({ link.source.track(), link.source.slot() });
        if (found == effectSlotMap.end()
            || (song.mixer[(std::size_t) link.source.track()].effects[(std::size_t) found->second.first].kind != EffectKind::peakController
                && song.mixer[(std::size_t) link.source.track()].effects[(std::size_t) found->second.first].kind != EffectKind::formulaController
                && ! newController))
            continue;
        const auto& sourceSlot = song.mixer[(std::size_t) link.source.track()].effects[(std::size_t) found->second.first];
        const auto formulaSource = sourceSlot.kind == EffectKind::formulaController;

        Automation a;
        std::function<float (float)> valueMap;
        a.target = resolveTarget (link.target, valueMap);
        if (a.target.kind == AutomationTargetKind::none || a.target.kind == AutomationTargetKind::tempo)
            continue;

        a.name = std::string (newController ? effectInfo (sourceSlot.kind).name : formulaSource ? "Formula Controller" : "Peak Controller") + " ("
                 + song.mixer[(std::size_t) link.source.track()].name + ")";
        a.source = { link.source.track(), found->second.first, link.source.param() - flp::ControlId::pluginParamBase };
        if (sourceSlot.kind == EffectKind::controlSurface) // FL's output numbers → the panel's order
        {
            const auto k = surface::flOutputToLynx (sourceSlot.state, a.source.output);
            if (! k)
                continue;
            a.source.output = *k;
        }
        else if (xy::isXyController (sourceSlot.kind) && (a.source.output < 0 || a.source.output >= xy::axesOf (sourceSlot.kind)))
            continue;
        if (valueMap)
            for (int i = 0; i <= 32; ++i)
                a.curve.push_back (std::clamp (valueMap ((float) i / 32.0f), 0.0f, 1.0f));
        song.automations.push_back (std::move (a));
        if (options.report != nullptr)
            ++options.report->converted[newController ? std::string (effectInfo (sourceSlot.kind).name) + " (link)"
                                        : formulaSource ? "Formula Controller (link)" : "Peak Controller (link)"];
    }

    // Playlist: all arrangements (the first stays open); pattern, audio and automation clips.
    std::vector<const std::vector<flp::PlaylistTrackInfo>*> importedTracks;

    if (! project.arrangements.empty())
        song.arrangements.clear();

    for (const auto& arrangement : project.arrangements)
    {
        std::vector<Clip> clips;

        for (const auto& item : arrangement.items)
        {
            if (item.length == 0)
                continue;

            if (item.kind == flp::PlaylistItem::Kind::channelClip && automationMap.count (item.index))
            {
                Clip clip;
                clip.kind = ClipKind::automation;
                clip.automation = automationMap[item.index];
                clip.track = item.track;
                clip.start = rescale (item.position);
                clip.length = rescale (item.length);
                clip.muted = item.muted(); // (issue #110)

                // trim in beats (-1 = no trim)
                if (std::isfinite (item.startOffset) && item.startOffset > 0.0f)
                    clip.offset = (std::int64_t) std::llround (item.startOffset * ppq);

                clips.push_back (clip);
                continue;
            }

            if (item.kind == flp::PlaylistItem::Kind::channelClip)
            {
                const auto channel = channelMap.find (item.index);

                if (channel == channelMap.end() || song.channels[(std::size_t) channel->second].instrument != InstrumentKind::audioClip)
                    continue;

                Clip clip;
                clip.kind = ClipKind::audio;
                clip.channel = channel->second;
                clip.track = item.track;
                clip.start = rescale (item.position);
                clip.length = rescale (item.length);
                clip.muted = item.muted();

                // The region start comes in beats or in milliseconds, depending on the clip;
                // we find out by the unit in which (end - start) matches the length.
                const auto beats = (double) item.length / (double) filePpq;
                const auto span = (double) item.endOffset - (double) item.startOffset;

                if (std::isfinite (item.startOffset) && item.startOffset > 0.0f)
                {
                    // offsets in ms are kept to the sample: the sub-tick rest goes to offsetFraction (REA_rodada110 §1:
                    // a 77 019.27 ms offset rounded to whole ticks played 2 samples late against FL)
                    const auto setOffset = [&clip] (double ticks)
                    {
                        clip.offset = (std::int64_t) std::llround (ticks);
                        clip.offsetFraction = (float) (ticks - (double) clip.offset);
                    };

                    if (std::abs (span - beats) < 0.02 * std::max (1.0, beats))
                        clip.offset = (std::int64_t) std::llround (item.startOffset * ppq);
                    else if (std::abs (span - beats * 60000.0 / song.bpm) < 0.02 * std::max (1.0, span))
                        setOffset (item.startOffset / 1000.0 * song.bpm / 60.0 * ppq);
                    else if (const auto& ch = song.channels[(std::size_t) channel->second]; ch.sampler.stretchBeats > 0.0f && span > 0.0 && beats > 0.0)
                    {
                        // time-stretched clip: start/end are in the SAMPLE's milliseconds, so the span is the clip
                        // length times the stretch factor (e.g. 31 219 ms over 64 beats = 29 538 ms at 130 BPM)
                        const auto factor = span / (beats * 60000.0 / song.bpm);
                        if (factor > 0.25 && factor < 4.0)
                            setOffset (item.startOffset / factor / 1000.0 * song.bpm / 60.0 * ppq);
                    }
                }

                // clip gain and fades (FL 20.99+ records); fades are in ms, at most the clip length
                clip.gain = item.gain;
                clip.pitch = item.pitch;
                clip.reversed = item.reversed;
                // FL's clip stretch (round 167l, FL render: a 6-beat item with stretch 1.5 ends 9 beats later, the song
                // with it): the item's length and start offset are the unstretched ones
                clip.stretch = item.stretch;
                if (clip.stretch != 1.0)
                {
                    clip.length = (std::int64_t) std::llround ((double) clip.length * clip.stretch);
                    const auto offset = ((double) clip.offset + (double) clip.offsetFraction) * clip.stretch;
                    clip.offset = (std::int64_t) std::llround (offset);
                    clip.offsetFraction = (float) (offset - (double) clip.offset);
                }
                const auto msToTicks = [&] (float ms)
                { return std::clamp ((std::int64_t) std::llround (ms / 1000.0 * song.bpm / 60.0 * ppq), (std::int64_t) 0, clip.length); };
                clip.fadeIn = msToTicks (item.fadeInMs);
                clip.fadeOut = msToTicks (item.fadeOutMs);
                clip.fadeInTension = item.fadeInTension;
                clip.fadeOutTension = item.fadeOutTension;
                clip.flFadeFlags = item.fadeFlags;
                clip.linearFades = (std::uint8_t) ((((item.fadeFlags >> 8) & 3) == 1 ? 1 : 0) | (((item.fadeFlags >> 12) & 3) == 1 ? 2 : 0));

                clips.push_back (clip);
                continue;
            }

            const auto it = patternMap.find (item.index);

            if (it == patternMap.end())
                continue;

            Clip clip;
            clip.pattern = it->second;
            clip.track = item.track;
            clip.start = rescale (item.position);
            clip.length = rescale (item.length);
            clip.offset = item.patternStartTick > 0 ? rescale (item.patternStartTick) : 0;
            clip.muted = item.muted();
            clips.push_back (clip);
        }

        // stable: on the same start the file's order stays, and the clip later in it wins a pattern clip takeover (issue #158)
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });

        Arrangement a;
        a.name = arrangement.name.empty() ? "Arrangement " + std::to_string (song.arrangements.size() + 1) : arrangement.name;

        // markers; type 8 changes the time signature; the other types are FL's marker actions (core/MarkerPlan.h)
        for (const auto& m : arrangement.markers)
            a.markers.push_back (importMarker (m)); // (unnamed action markers show their action's name on the ruler, as in FL)

        std::stable_sort (a.markers.begin(), a.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
        importedTracks.push_back (&arrangement.tracks);

        if (song.arrangements.empty())
            song.playlist = std::move (clips); // the first is the current one
        else
            a.clips = std::move (clips);

        song.arrangements.push_back (std::move (a));
    }

    // the arrangement open when FL saved (event 0x64, REA round 167g); files without it: the fullest is usually the song
    {
        int fullest = -1;

        for (int i = 0; i < (int) project.arrangements.size() && i < (int) song.arrangements.size(); ++i)
            if (project.currentArrangement >= 0 && project.arrangements[(std::size_t) i].index == project.currentArrangement)
                fullest = i;

        if (fullest < 0 && project.currentArrangement != -1)
            fullest = 0; // FL's loader: an index past the last arrangement opens the first

        if (fullest < 0)
        {
            fullest = 0;
            std::size_t most = song.playlist.size();

            for (int i = 1; i < (int) song.arrangements.size(); ++i)
                if (song.arrangements[(std::size_t) i].clips.size() > most)
                {
                    most = song.arrangements[(std::size_t) i].clips.size();
                    fullest = i;
                }
        }

        song.selectArrangement (fullest);

        // Playlist track names, mute and colour (from the open arrangement); FL's default grey = no colour
        if (fullest < (int) importedTracks.size())
            for (const auto& t : *importedTracks[(std::size_t) fullest])
            {
                const auto coloured = (t.colour & 0xFFFFFF) != 0x485156;

                const auto sized = std::abs (t.heightScale - 1.0f) > 0.01f;

                TrackPerformance performance;
                performance.motion = (TrackPerformance::Motion) std::clamp (t.motion, 0, 6);
                performance.press = (TrackPerformance::Press) std::clamp (t.press, 0, 3);
                performance.triggerSync = (TrackPerformance::Sync) std::clamp (t.triggerSync, 0, 6);
                performance.queue = t.queue;
                performance.tolerant = t.tolerant;
                performance.positionSync = (TrackPerformance::Sync) std::clamp (t.positionSync, 0, 6);

                if (t.index >= 0 && t.index < 500 && (! t.name.empty() || ! t.enabled || coloured || t.grouped || ! t.expanded || sized || t.mode != 0
                                                       || ! performance.isDefault() || t.lockedToContent || t.lockedHeight > 0 || t.icon != 0))
                {
                    if ((int) song.playlistTracks.size() <= t.index)
                        song.playlistTracks.resize ((std::size_t) t.index + 1);

                    auto& track = song.playlistTracks[(std::size_t) t.index];
                    track = { t.name, ! t.enabled, 0, coloured ? 0xFF000000u | t.colour : 0u };
                    // FL's size is relative to its default row; LYNX keeps the same proportion of its own default
                    if (sized)
                        track.height = polish::clampTrackHeight ((int) std::lround (polish::defaultTrackHeight * t.heightScale));
                    track.grouped = t.index > 0 && t.grouped;
                    track.performance = performance; // Performance mode's track settings (round 165m)
                    track.collapsed = ! t.expanded;
                    // FL's track locks (round 168f): a locked size is in FL's pixels, clamped to LYNX's row limits
                    track.lockedToContent = t.lockedToContent;
                    track.lockedHeight = t.lockedHeight > 0 ? polish::clampTrackHeight (t.lockedHeight) : -1;
                    track.icon = std::clamp (t.icon, 0, 0xFFFF); // (REA round 168j)
                    // track modes (REA round 146a): an audio track's mixer track, an instrument track's channel
                    const auto mixerOk = t.mixerTrack > 0 && t.mixerTrack < (int) song.mixer.size();
                    if (t.mode == 1 && mixerOk)
                    {
                        track.mode = PlaylistTrackMode::audio;
                        track.mixerTrack = t.mixerTrack;
                    }
                    else if (t.mode == 3)
                        if (const auto it = channelMap.find (t.channel); it != channelMap.end() && it->second < (int) song.channels.size())
                        {
                            track.mode = PlaylistTrackMode::instrument;
                            track.channel = it->second;
                            track.mixerTrack = mixerOk ? t.mixerTrack : song.channels[(std::size_t) it->second].mixerTrack;
                        }
                }
            }
    }

    // PAT / SONG as saved (event 0x09, REA round 167g); files without it: SONG when the Playlist has clips
    if (project.songMode >= 0)
        song.mode = project.songMode == 1 ? PlayMode::song : PlayMode::pattern;
    else if (! song.playlist.empty())
        song.mode = PlayMode::song;


    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    // The pattern selected when FL saved (event 0x43, REA round 167g); when it is missing or was empty (empty patterns
    // are not imported), the fullest is the best starting point.
    if (const auto it = patternMap.find (project.currentPattern); project.currentPattern > 0 && it != patternMap.end())
        song.currentPattern = it->second;
    else
        for (int i = 0; i < (int) song.patterns.size(); ++i)
            if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) song.currentPattern].notes.size())
                song.currentPattern = i;

    // FPC kits: one channel per pad (last to first, so indices don't shift)
    for (auto it = drumKits.rbegin(); it != drumKits.rend(); ++it)
        splitDrumChannel (song, it->first, it->second);

    numberChorusInstances (song); // the random LFO phases FL gives each Hyper Chorus instance (REA_rodada59 §2.6)
    flpexport::stampOrigins (song); // what the import made of each FL plugin (Export to FL Studio, round 171e)
    clipops::updateCrossfades (song); // the project's "Automatic crossfades" on its overlapping audio clips (round 167a)
    if (initialTempo)
        song.setTargetValue ({ AutomationTargetKind::tempo, 0 }, *initialTempo);
    return song;
}
std::optional<MixerTrack> mixerTrackFromFlPreset (std::span<const std::uint8_t> fileBytes, const FlpImportOptions& options)
{
    if (! flp::looksLikeMixerPreset (fileBytes))
        return std::nullopt;
    try
    {
        const auto project = flp::parseMixerPreset (fileBytes);
        auto song = importFlp (project, options);
        if (song.mixer.size() < 2)
            return std::nullopt;
        auto track = song.mixer[1];
        track.name = project.mixer[1].name; // (the import's "Insert 1" when the preset has no name)
        track.colour = project.mixer[1].colour != 0 && project.mixer[1].colour != 0x485156 ? 0xFF000000u | project.mixer[1].colour : 0u;
        return track;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}
} // namespace lynx
