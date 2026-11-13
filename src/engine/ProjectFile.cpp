#include "engine/ProjectFile.h"
#include "core/LiveInput.h"
#include "core/FlCurve.h"
#include "core/Preferences.h"

#include "core/ClipLauncher.h"
#include "core/Groove.h"
#include "core/PatternBlocks.h"
#include "core/SessionIds.h"

#include "core/MixerOps.h"
#include "core/PlaylistOps.h"

#include <algorithm>
#include <tuple>
#include <cmath>

#include "core/Effects.h"
#include "core/Rack.h"
#include "core/FruityBasicFx.h"
#include "core/PluginSubstitutes.h"
#include "core/ProjectSafety.h"
#include "core/RemotePlugins.h"
#include "core/SessionOps.h"
#include "engine/Text.h"

namespace lynx::ProjectFile
{
namespace
{
// cutoff/pitch envelopes: [amount, attack, hold, decay, sustain, release(, shape)]; only the active ones
void writeEnvelopes (juce::DynamicObject& o, const ModEnvelope& filter, const ModEnvelope& pitch)
{
    for (auto [name, env] : { std::pair { "filterEnv", &filter }, { "pitchEnv", &pitch } })
        if (env->amount != 0.0f)
        {
            juce::Array<juce::var> values { env->amount, env->attack, env->hold, env->decay, env->sustain, env->release };
            if (env->shape != 0)
                values.add (env->shape);
            o.setProperty (name, values);
        }
}

void readEnvelopes (const juce::var& o, ModEnvelope& filter, ModEnvelope& pitch)
{
    for (auto [name, env, limit] : { std::tuple { "filterEnv", &filter, 10.0f }, { "pitchEnv", &pitch, 48.0f } })
        if (const auto& e = o[name]; e.isArray() && (e.size() == 6 || e.size() == 7))
        {
            const auto shape = e.size() == 7 ? juce::jlimit (0, 1, (int) e[6]) : 0;
            auto time = [&] (int i) { return juce::jlimit (0.0f, shape == 1 ? 50.0f : 20.0f, (float) (double) e[i]); };
            *env = { juce::jlimit (-limit, limit, (float) (double) e[0]), time (1), time (2), time (3),
                     juce::jlimit (0.0f, 1.0f, (float) (double) e[4]), time (5), shape };
        }
}

// channel LFOs: only the active ones, [target, amount, period, synced, shape, delay, fade-in]
void writeLfos (juce::DynamicObject& o, const std::array<ChannelLfo, lfoTargets>& lfo)
{
    juce::Array<juce::var> list;

    for (int i = 0; i < lfoTargets; ++i)
        if (const auto& l = lfo[(std::size_t) i]; l.amount != 0.0f)
        {
            juce::Array<juce::var> values { i, l.amount, l.period, l.sync, l.shape, l.delay, l.attack, l.global };
            if (l.secondsTiming)
                values.add (true);
            list.add (values);
        }

    if (! list.isEmpty())
        o.setProperty ("lfo", list);
}

void readLfos (const juce::var& o, std::array<ChannelLfo, lfoTargets>& lfo)
{
    if (auto* list = o["lfo"].getArray())
        for (const auto& e : *list)
            if (e.isArray() && e.size() >= 7)
                if (const auto i = (int) e[0]; i >= 0 && i < lfoTargets)
                    lfo[(std::size_t) i] = { juce::jlimit (-48.0f, 48.0f, (float) (double) e[1]), juce::jlimit (0.005f, 64.0f, (float) (double) e[2]),
                                             (bool) e[3], juce::jlimit (0, 2, (int) e[4]), juce::jlimit (0.0f, 64.0f, (float) (double) e[5]),
                                             juce::jlimit (0.0f, 64.0f, (float) (double) e[6]), e.size() >= 8 && (bool) e[7],
                                             e.size() >= 9 && (bool) e[8] };
}

const char* instrumentId (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "kick";
        case InstrumentKind::snare: return "snare";
        case InstrumentKind::hat: return "hat";
        case InstrumentKind::clap: return "clap";
        case InstrumentKind::synth: return "synth";
        case InstrumentKind::sampler: return "sampler";
        case InstrumentKind::audioClip: return "audioClip";
        case InstrumentKind::plugin: return "plugin";
        case InstrumentKind::controller: return "controller";
        case InstrumentKind::sakura: return "sakura";
        case InstrumentKind::drumpad: return "drumpad";
        case InstrumentKind::boobass: return "boobass";
        case InstrumentKind::drumsynth: return "drumsynth";
        case InstrumentKind::harmless: return "harmless";
        case InstrumentKind::sytrus: return "sytrus";
        case InstrumentKind::harmor: return "harmor";
        case InstrumentKind::gms: return "gms";
        case InstrumentKind::morphine: return "morphine";
        case InstrumentKind::ogun: return "ogun";
        case InstrumentKind::minisynth: return "minisynth";
        case InstrumentKind::dx10: return "dx10";
        case InstrumentKind::kepler: return "kepler";
        case InstrumentKind::poizone: return "poizone";
        case InstrumentKind::fruitkick: return "fruitkick";
        case InstrumentKind::bassdrum: return "bassdrum";
        case InstrumentKind::toxic: return "toxic";
        case InstrumentKind::threeosc: return "threeosc";
        case InstrumentKind::autogun: return "autogun";
        case InstrumentKind::transistorbass: return "transistorbass";
        case InstrumentKind::simsynth: return "simsynth";
        case InstrumentKind::wasp: return "wasp";
        case InstrumentKind::plucked: return "plucked";
        case InstrumentKind::beepmap: return "beepmap";
        case InstrumentKind::sawer: return "sawer";
        case InstrumentKind::flkeys: return "flkeys";
        case InstrumentKind::granulizer: return "granulizer";
        case InstrumentKind::drumaxx: return "drumaxx";
        case InstrumentKind::slayer: return "slayer";
        case InstrumentKind::fruitydance: return "fruitydance";
        case InstrumentKind::directwave: return "directwave";
        case InstrumentKind::midiOut: return "midiOut";
        case InstrumentKind::patcher: return "patcher";
        case InstrumentKind::soundfont: return "soundfont";
        case InstrumentKind::layer: return "layer";
        case InstrumentKind::dashboard: return "dashboard";
        case InstrumentKind::voltage: return "voltage";
        case InstrumentKind::vfx: return "vfx";
        case InstrumentKind::videoplayer: return "videoplayer";
        case InstrumentKind::keplerExo: return "keplerexo";
        case InstrumentKind::wavetraveller: return "wavetraveller";
        case InstrumentKind::fruityslicer: return "fruityslicer";
        case InstrumentKind::slicex: return "slicex";
        case InstrumentKind::fpc: return "fpc";
        case InstrumentKind::pocketSuperSaw: return "pocketsupersaw";
    }
    return "synth";
}

InstrumentKind instrumentFromId (const juce::String& id)
{
    for (auto k : { InstrumentKind::kick, InstrumentKind::snare, InstrumentKind::hat, InstrumentKind::clap,
                    InstrumentKind::synth, InstrumentKind::sampler, InstrumentKind::audioClip,
                    InstrumentKind::plugin, InstrumentKind::controller, InstrumentKind::sakura, InstrumentKind::drumpad, InstrumentKind::boobass,
                    InstrumentKind::drumsynth, InstrumentKind::harmless, InstrumentKind::sytrus, InstrumentKind::harmor,
                    InstrumentKind::gms, InstrumentKind::morphine, InstrumentKind::ogun, InstrumentKind::minisynth, InstrumentKind::dx10, InstrumentKind::kepler, InstrumentKind::poizone,
                    InstrumentKind::fruitkick, InstrumentKind::bassdrum, InstrumentKind::toxic,
                    InstrumentKind::threeosc, InstrumentKind::autogun, InstrumentKind::transistorbass,
                    InstrumentKind::simsynth, InstrumentKind::wasp, InstrumentKind::plucked, InstrumentKind::beepmap, InstrumentKind::sawer, InstrumentKind::flkeys,
                    InstrumentKind::granulizer, InstrumentKind::drumaxx, InstrumentKind::slayer, InstrumentKind::fruitydance, InstrumentKind::directwave, InstrumentKind::midiOut, InstrumentKind::patcher, InstrumentKind::soundfont, InstrumentKind::layer, InstrumentKind::dashboard, InstrumentKind::voltage, InstrumentKind::vfx, InstrumentKind::videoplayer, InstrumentKind::keplerExo, InstrumentKind::wavetraveller, InstrumentKind::fruityslicer, InstrumentKind::slicex, InstrumentKind::fpc, InstrumentKind::pocketSuperSaw })
        if (id == instrumentId (k))
            return k;

    return InstrumentKind::synth;
}

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

const char* effectId (EffectKind k)
{
    switch (k)
    {
        case EffectKind::eq: return "eq";
        case EffectKind::compressor: return "compressor";
        case EffectKind::delay: return "delay";
        case EffectKind::reverb: return "reverb";
        case EffectKind::plugin: return "plugin";
        case EffectKind::limiter: return "limiter";
        case EffectKind::filter: return "filter";
        case EffectKind::distortion: return "distortion";
        case EffectKind::chorus: return "chorus";
        case EffectKind::parametricEq: return "peq";
        case EffectKind::utility: return "utility";
        case EffectKind::volumeGate: return "volumeGate";
        case EffectKind::peakController: return "peakController";
        case EffectKind::pitchShift: return "pitchShift";
        case EffectKind::freqShift: return "freqShift";
        case EffectKind::parallel: return "parallel";
        case EffectKind::patcher: return "patcher";
        case EffectKind::lofi: return "lofi";
        case EffectKind::effector: return "effector";
        case EffectKind::crossover: return "crossover";
        case EffectKind::rhythmGain: return "rhythmGain";
        case EffectKind::autoPan: return "autoPan";
        case EffectKind::vocoder: return "vocoder";
        case EffectKind::pitchCorrect: return "pitchCorrect";
        case EffectKind::squeeze: return "squeeze";
        case EffectKind::vintagePhaser: return "vintagePhaser";
        case EffectKind::vocodex: return "vocodex";
        case EffectKind::maximus: return "maximus";
        case EffectKind::fruityLimiter: return "fruityLimiter";
        case EffectKind::fruityCompressor: return "fruityCompressor";
        case EffectKind::fruityWaveShaper: return "fruityWaveShaper";
        case EffectKind::fruitySoftClipper: return "fruitySoftClipper";
        case EffectKind::fruityPanOMatic: return "fruityPanOMatic";
        case EffectKind::fruityBalance: return "fruityBalance";
        case EffectKind::fruityBloodOverdrive: return "fruityBloodOverdrive";
        case EffectKind::reeverb2: return "reeverb2";
        case EffectKind::reeverb1: return "reeverb1";
        case EffectKind::fruityDelay2: return "fruityDelay2";
        case EffectKind::fruityDelay3: return "fruityDelay3";
        case EffectKind::stereoShaper: return "stereoShaper";
        case EffectKind::stereoEnhancer: return "stereoEnhancer";
        case EffectKind::transientProcessor: return "transientProcessor";
        case EffectKind::spreader: return "spreader";
        case EffectKind::fruityChorus: return "fruityChorus";
        case EffectKind::hyperChorus: return "hyperChorus";
        case EffectKind::vintageChorus: return "vintageChorus";
        case EffectKind::grossBeat: return "grossBeat";
        case EffectKind::fruityDelayBank: return "fruityDelayBank";
        case EffectKind::fruityMultiband: return "fruityMultiband";
        case EffectKind::fruityFastDist: return "fruityFastDist";
        case EffectKind::fruityFlangus: return "fruityFlangus";
        case EffectKind::fruityFlanger: return "fruityFlanger";
        case EffectKind::luxeVerb: return "luxeVerb";
        case EffectKind::convolver: return "convolver";
        case EffectKind::equo: return "equo";
        case EffectKind::flPitchShifter: return "flPitchShifter";
        case EffectKind::frequencySplitter: return "frequencySplitter";
        case EffectKind::fruityFastLP: return "fruityFastLP";
        case EffectKind::fruityFreeFilter: return "fruityFreeFilter";
        case EffectKind::fruityFilter: return "fruityFilter";
        case EffectKind::hardcore: return "hardcore";
        case EffectKind::distructor: return "distructor";
        case EffectKind::multibandDelay: return "multibandDelay";
        case EffectKind::lovePhilter: return "lovePhilter";
        case EffectKind::newtone: return "newtone";
        case EffectKind::newtime: return "newtime";
        case EffectKind::facetEq: return "facetEq";
        case EffectKind::apexLimiter: return "apexLimiter";
        case EffectKind::ridgeStrip: return "ridgeStrip";
        case EffectKind::glowLeveler: return "glowLeveler";
        case EffectKind::hushDeEsser: return "hushDeEsser";
        case EffectKind::aether: return "aether";
        case EffectKind::ember: return "ember";
        case EffectKind::kilnChannel: return "kilnChannel";
        case EffectKind::fretChannel: return "fretChannel";
        case EffectKind::shellChannel: return "shellChannel";
        case EffectKind::pocketDistortion: return "pocketDistortion";
        case EffectKind::pocketLimiterClassic: return "pocketLimiterClassic";
        case EffectKind::pocketLimiter: return "pocketLimiter";
        case EffectKind::pocketLeveller: return "pocketLeveller";
        case EffectKind::pocketGate: return "pocketGate";
        case EffectKind::pocketTranceDelay: return "pocketTranceDelay";
        case EffectKind::pocketTapeDelay: return "pocketTapeDelay";
        case EffectKind::pocketMultiFx: return "pocketMultiFx";
        case EffectKind::strikeShaper: return "strikeShaper";
        case EffectKind::pocketAutoduck: return "pocketAutoduck";
        case EffectKind::pocketChorus: return "pocketChorus";
        case EffectKind::pocketSpacer: return "pocketSpacer";
        case EffectKind::pocketStereoizer: return "pocketStereoizer";
        case EffectKind::pocketWowFlutter: return "pocketWowFlutter";
        case EffectKind::pocketPhaser: return "pocketPhaser";
        case EffectKind::pocketFlanger: return "pocketFlanger";
        case EffectKind::pocketTremolo: return "pocketTremolo";
        case EffectKind::pocketWaveshaper: return "pocketWaveshaper";
        case EffectKind::pocketCompressor: return "pocketCompressor";
        case EffectKind::pocketSpreader: return "pocketSpreader";
        case EffectKind::pocketComb: return "pocketComb";
        case EffectKind::pocketMultiband: return "pocketMultiband";
        case EffectKind::pocketTapeStop: return "pocketTapeStop";
        case EffectKind::pocketEq: return "pocketEq"; // round 190dw
        case EffectKind::pocketGraphicEq: return "pocketGraphicEq"; // round 190dw
        case EffectKind::pocketFilter: return "pocketFilter"; // round 190dw
        case EffectKind::pocketReverbClassic: return "pocketReverbClassic"; // round 190dw
        case EffectKind::pocketReverb2: return "pocketReverb2"; // round 190dw
        case EffectKind::pocketReverb: return "pocketReverb"; // round 190dw
        case EffectKind::rackMixer: return "rackMixer";
        case EffectKind::edison: return "edison";
        case EffectKind::lynxTune: return "lynxTune";
        case EffectKind::fruityPhaser: return "fruityPhaser";
        case EffectKind::epochVerb: return "epochVerb";
        case EffectKind::atrium: return "atrium";
        case EffectKind::foilPlate: return "foilPlate";
        case EffectKind::nebula: return "nebula";
        case EffectKind::halo: return "halo";
        case EffectKind::horizon: return "horizon";
        case EffectKind::echoform: return "echoform";
        case EffectKind::spiralEcho: return "spiralEcho";
        case EffectKind::orbit: return "orbit";
        case EffectKind::latticeMod: return "latticeMod";
        case EffectKind::fruitySend: return "fruitySend";
        case EffectKind::crestCompressor: return "crestCompressor";
        case EffectKind::prismMultiband: return "prismMultiband";
        case EffectKind::sentryGate: return "sentryGate";
        case EffectKind::quellDenoiser: return "quellDenoiser";
        case EffectKind::geminiDoubler: return "geminiDoubler";
        case EffectKind::formulaController: return "formulaController";
        case EffectKind::voiceShift: return "voiceShift";
        case EffectKind::emphasizer: return "emphasizer";
        case EffectKind::flEmphasis: return "flEmphasis";
        case EffectKind::flFrequencyShifter: return "flFrequencyShifter";
        case EffectKind::flLowLifter: return "flLowLifter";
        case EffectKind::flPhaseInverter: return "flPhaseInverter";
        case EffectKind::flCenter: return "flCenter";
        case EffectKind::flBassBoost: return "flBassBoost";
        case EffectKind::fl7BandEq: return "fl7BandEq";
        case EffectKind::flDelay1: return "flDelay1";
        case EffectKind::hardwareInsert: return "hardwareInsert";
        case EffectKind::fruityMute2: return "fruityMute2";
        case EffectKind::bigClock: return "bigClock";
        case EffectKind::tuner: return "tuner";
        case EffectKind::htmlNotebook: return "htmlNotebook";
        case EffectKind::scratcher: return "fruityScratcher";
        case EffectKind::transmitter: return "transmitter";
        case EffectKind::flTransporter: return "flTransporter";
        case EffectKind::waveCandy: return "waveCandy";
        case EffectKind::visualizer: return "visualizer";
        case EffectKind::xyController: return "xyController";
        case EffectKind::xyzController: return "xyzController";
        case EffectKind::controlSurface: return "controlSurface";
    }
    return "eq";
}

juce::var pluginToVar (const PluginRef& p)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("id", str (p.id));
    o->setProperty ("name", str (p.name));
    o->setProperty ("state", str (p.state));
    // an .flp plugin reference not resolved here (kept by a LYNX substitute): every field, the state byte for byte
    if (p.foreign.format != ForeignPlugin::Format::none)
        o->setProperty ("foreign", str (foreignToText (p.foreign)));
    if (p.fixedBuffers)
        o->setProperty ("fixedBuffers", true);
    if (p.separateProcess)
        o->setProperty ("separateProcess", true);
    if (! p.threaded)
        o->setProperty ("threaded", false);
    if (p.maxSizeBuffers)
        o->setProperty ("maxSizeBuffers", true);
    if (! p.allowSmartDisable)
        o->setProperty ("allowSmartDisable", false);
    if (p.hostMaxBuffer)
        o->setProperty ("hostMaxBuffer", true);
    if (p.midiInPort >= 0)
        o->setProperty ("midiInPort", p.midiInPort);
    if (p.midiOutPort >= 0)
        o->setProperty ("midiOutPort", p.midiOutPort);
    if (! p.notifyRender)
        o->setProperty ("notifyRender", false);
    if (p.latencyOverrideMs >= 0.0f)
        o->setProperty ("latencyMs", p.latencyOverrideMs);
    // the round-151 wrapper options, when not FL's defaults
    if (! p.releaseVelocity)
        o->setProperty ("releaseVelocity", false);
    if (p.sendBendRange)
        o->setProperty ("sendBendRange", true);
    if (p.bendRange != 12)
        o->setProperty ("bendRange", p.bendRange);
    if (p.modXAftertouch)
        o->setProperty ("modXAftertouch", true);
    if (p.mpe) // issue #108
        o->setProperty ("mpe", true);
    if (! p.allNotesOffOnStop)
        o->setProperty ("allNotesOffOnStop", false);
    if (! p.resetOnFlush)
        o->setProperty ("resetOnFlush", false);
    if (! p.sendLoopPosition)
        o->setProperty ("sendLoopPosition", false);
    if (p.allowHostKeys)
        o->setProperty ("allowHostKeys", true);
    if (p.hostKeysFirst)
        o->setProperty ("hostKeysFirst", true);
    if (p.bitSize != 0)
        o->setProperty ("bitSize", p.bitSize);
    if (! p.processInactive)
        o->setProperty ("processInactive", false);
    // Processing › Connections (round 188d): [offset, active] per output / input, up to the last one that is not the
    // default
    for (const auto& [key, source] : { std::pair { "outputs", &p.outputs }, std::pair { "inputs", &p.inputs } })
    {
        auto routes = *source;
        while (! routes.empty() && routes.back() == PluginOutputRoute {})
            routes.pop_back();
        if (routes.empty())
            continue;
        juce::Array<juce::var> list;
        for (const auto& r : routes)
            list.add (juce::Array<juce::var> { r.offset, (int) r.active });
        o->setProperty (key, list);
    }
    if (p.detached) // issue #116 (saved only when not the default)
        o->setProperty ("detached", true);
    // Online Sessions P5-A1 (round 190ai): a plugin the session's host plays for everyone, and its parameter values
    if (p.playedBy != 0)
        o->setProperty ("playedBy", (juce::int64) p.playedBy);
    if (! p.values.empty())
    {
        juce::Array<juce::var> values; // [index, value, index, value, ...]
        for (const auto& [index, value] : p.values)
        {
            values.add (index);
            values.add ((double) value);
        }
        o->setProperty ("values", values);
    }
    if (p.pinned) // round 190ao (saved only when on)
        o->setProperty ("pinnedOnTop", true);
    if (p.velocityMode != 0) // round 190ao (saved only when the plugin has its own velocity setting)
    {
        o->setProperty ("velocityMode", p.velocityMode);
        o->setProperty ("fixedVelocity", p.fixedVelocity);
    }
    return juce::var (o.release());
}

PluginRef pluginFromVar (const juce::var& v)
{
    PluginRef p;
    p.id = v["id"].toString().toStdString();
    p.name = v["name"].toString().toStdString();
    p.state = v["state"].toString().toStdString();
    if (v.hasProperty ("foreign") && ! foreignFromText (v["foreign"].toString().toStdString(), p.foreign))
        p.foreign = {};
    p.fixedBuffers = (bool) v.getProperty ("fixedBuffers", false);
    p.separateProcess = (bool) v.getProperty ("separateProcess", false);
    p.detached = (bool) v.getProperty ("detached", false);
    p.pinned = (bool) v.getProperty ("pinnedOnTop", false);
    p.velocityMode = prefs::clampPluginVelocityMode ((int) v.getProperty ("velocityMode", 0));
    p.fixedVelocity = prefs::clampFixedVelocity ((int) v.getProperty ("fixedVelocity", prefs::defaultFixedVelocity));
    p.threaded = (bool) v.getProperty ("threaded", true);
    p.maxSizeBuffers = (bool) v.getProperty ("maxSizeBuffers", false);
    p.allowSmartDisable = (bool) v.getProperty ("allowSmartDisable", true);
    p.hostMaxBuffer = (bool) v.getProperty ("hostMaxBuffer", false);
    p.midiInPort = juce::jlimit (-1, 255, (int) v.getProperty ("midiInPort", -1));
    p.midiOutPort = juce::jlimit (-1, 255, (int) v.getProperty ("midiOutPort", -1));
    p.notifyRender = (bool) v.getProperty ("notifyRender", true);
    p.latencyOverrideMs = v.hasProperty ("latencyMs") ? juce::jlimit (0.0f, 2000.0f, (float) (double) v["latencyMs"]) : -1.0f;
    p.releaseVelocity = (bool) v.getProperty ("releaseVelocity", true);
    p.sendBendRange = (bool) v.getProperty ("sendBendRange", false);
    p.bendRange = juce::jlimit (1, 48, (int) v.getProperty ("bendRange", 12));
    p.modXAftertouch = (bool) v.getProperty ("modXAftertouch", false);
    p.mpe = (bool) v.getProperty ("mpe", false);
    p.allNotesOffOnStop = (bool) v.getProperty ("allNotesOffOnStop", true);
    p.resetOnFlush = (bool) v.getProperty ("resetOnFlush", true);
    p.sendLoopPosition = (bool) v.getProperty ("sendLoopPosition", true);
    p.allowHostKeys = (bool) v.getProperty ("allowHostKeys", false);
    p.hostKeysFirst = (bool) v.getProperty ("hostKeysFirst", false);
    p.bitSize = (int) v.getProperty ("bitSize", 0);
    if (p.bitSize != 32 && p.bitSize != 64)
        p.bitSize = 0;
    p.playedBy = (std::uint32_t) juce::jlimit<juce::int64> (0, 0xffffffffLL, (juce::int64) v.getProperty ("playedBy", 0));
    if (const auto* values = v["values"].getArray())
    {
        for (int i = 0; i + 1 < values->size() && p.values.size() < remoteplug::maxParams; i += 2)
            p.values.push_back ({ (int) (*values)[i], (float) (double) (*values)[i + 1] });
        remoteplug::cleanValues (p);
    }
    p.processInactive = (bool) v.getProperty ("processInactive", true);
    for (const auto& [key, target] : { std::pair { "outputs", &p.outputs }, std::pair { "inputs", &p.inputs } })
        if (const auto* list = v[key].getArray())
            for (const auto& r : *list)
                if (target->size() < 256)
                    target->push_back ({ juce::jlimit (-999, 999, (int) r[0]), (std::int8_t) (r.size() > 1 ? juce::jlimit (-1, 1, (int) r[1]) : -1) });
    return p;
}

std::optional<EffectKind> effectFromId (const juce::String& id);

juce::var channelToVar (const ChannelDef& c);
ChannelDef channelFromVar (const juce::var& c, const Song& song);
juce::var effectToVar (const EffectSlot& slot);
std::optional<EffectSlot> effectFromVar (const juce::var& e);

// The FL plugin an imported generator / effect came from (round 171e, Export to FL Studio): {plugin, state, print}
juce::var flOriginToVar (const FlOrigin& origin)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("plugin", juce::String::fromUTF8 (origin.plugin.c_str()));
    if (! origin.state.empty())
        o->setProperty ("state", juce::Base64::toBase64 (origin.state.data(), origin.state.size()));
    o->setProperty ("print", juce::String::toHexString ((juce::int64) origin.fingerprint));
    return juce::var (o.release());
}

FlOrigin flOriginFromVar (const juce::var& v)
{
    FlOrigin origin;
    if (! v.isObject())
        return origin;
    origin.plugin = v["plugin"].toString().toStdString();
    if (const auto encoded = v["state"].toString(); encoded.isNotEmpty())
    {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64 (raw, encoded))
            origin.state.assign ((const std::uint8_t*) raw.getData(), (const std::uint8_t*) raw.getData() + raw.getDataSize());
    }
    origin.fingerprint = (std::uint64_t) v["print"].toString().getHexValue64();
    return origin;
}


// A Patcher in rack mode (round 183c, core/Rack.h): the RackDoc. The Patcher's nodes (the rack's devices and chain
// mixers) and its compiled cables are saved as usual, so older versions open it as a plain Patcher; on load the cables
// are built again from the RackDoc. A plain Patcher writes nothing here (its file is unchanged).
juce::var rackToVar (const RackDoc& r)
{
    auto o = std::make_unique<juce::DynamicObject>();
    auto ints = [] (std::initializer_list<int> list) {
        juce::Array<juce::var> a;
        for (const auto x : list)
            a.add (x);
        return a;
    };
    auto zone = [&] (const RackZone& z) { return ints ({ z.lo, z.hi, z.fadeLo, z.fadeHi }); };
    o->setProperty ("kind", (int) r.kind);
    if (! r.name.empty())
        o->setProperty ("name", str (r.name));
    juce::Array<juce::var> chains, returns;
    for (const auto* list : { &r.chains, &r.returns })
    for (const auto& c : *list)
    {
        auto v = std::make_unique<juce::DynamicObject>();
        v->setProperty ("name", str (c.name));
        if (c.colour >= 0)
            v->setProperty ("colour", c.colour);
        v->setProperty ("active", c.active);
        if (c.solo)
            v->setProperty ("solo", true);
        v->setProperty ("volume", (double) c.volume);
        v->setProperty ("pan", (double) c.pan);
        v->setProperty ("key", zone (c.key));
        v->setProperty ("velocity", zone (c.velocity));
        v->setProperty ("select", zone (c.select));
        juce::Array<juce::var> nodes;
        for (const auto n : c.nodes)
            nodes.add (n);
        v->setProperty ("nodes", nodes);
        v->setProperty ("mixer", c.mixer);
        if (c.gate >= 0)
            v->setProperty ("gate", c.gate);
        if (r.drum && list == &r.chains) // a Drum Rack pad (round 190ec): [receive, play, choke, audio to] and its sends
        {
            v->setProperty ("pad", ints ({ c.receive, c.play, c.choke, c.audioTo }));
            juce::Array<juce::var> sends; // [level, on] per return chain
            for (const auto& x : c.sends)
                sends.add (juce::Array<juce::var> { (double) x.level, x.on });
            if (! sends.isEmpty())
                v->setProperty ("sends", sends);
        }
        (list == &r.chains ? chains : returns).add (juce::var (v.release()));
    }
    o->setProperty ("chains", chains);
    if (r.drum) // (round 190ec; a LYNX without Drum Racks reads an Instrument Rack: kind 1, its pads key-split)
    {
        o->setProperty ("drum", true);
        if (! returns.isEmpty())
            o->setProperty ("returns", returns);
        o->setProperty ("drumView", juce::Array<juce::var> { r.showPads, r.showIO, r.showSends, r.showReturns, r.padScroll });
    }
    juce::Array<juce::var> macros;
    for (const auto& m : r.macros)
    {
        auto v = std::make_unique<juce::DynamicObject>();
        if (! m.name.empty())
            v->setProperty ("name", str (m.name));
        if (m.colour >= 0)
            v->setProperty ("colour", m.colour);
        v->setProperty ("value", (double) m.value);
        if (m.defaultValue >= 0.0f)
            v->setProperty ("default", (double) m.defaultValue);
        if (m.excludeFromRandom)
            v->setProperty ("noRandom", true);
        if (m.excludeFromVariations)
            v->setProperty ("noVariations", true);
        juce::Array<juce::var> maps; // [target, node, chain, param, min, max]
        for (const auto& x : m.mappings)
            maps.add (juce::Array<juce::var> { (int) x.target, x.node, x.chain, x.param, (double) x.min, (double) x.max });
        if (! maps.isEmpty())
            v->setProperty ("maps", maps);
        macros.add (juce::var (v.release()));
    }
    o->setProperty ("macros", macros);
    o->setProperty ("shown", r.shownMacros);
    o->setProperty ("selector", (double) r.selector);
    if (r.selectorFiltersCtrl)
        o->setProperty ("selectorFiltersCtrl", true);
    juce::Array<juce::var> variations;
    for (const auto& x : r.variations)
    {
        auto v = std::make_unique<juce::DynamicObject>();
        v->setProperty ("name", str (x.name));
        juce::Array<juce::var> values; // −1 = no value for that macro
        for (int k = 0; k < rackMacroCount; ++k)
            values.add (x.has[(std::size_t) k] ? (double) x.values[(std::size_t) k] : -1.0);
        v->setProperty ("values", values);
        variations.add (juce::var (v.release()));
    }
    if (! variations.isEmpty())
        o->setProperty ("variations", variations);
    o->setProperty ("selectedVariation", r.selectedVariation);
    if (r.morphOn || r.morphA >= 0 || r.morphB >= 0 || r.morph != 0.0f || r.glideMs != 0.0f)
        o->setProperty ("morph", juce::Array<juce::var> { r.morphOn, r.morphA, r.morphB, (double) r.morph, (double) r.glideMs });
    if (! r.children.empty())
    {
        juce::Array<juce::var> children;
        for (const auto c : r.children)
            children.add (c);
        o->setProperty ("children", children);
    }
    o->setProperty ("view", juce::Array<juce::var> { r.showMacros, r.showChains, r.showDevices, r.showVariations, r.zoneEditor, r.selectedChain, r.autoSelect });
    return juce::var (o.release());
}

RackDoc rackFromVar (const juce::var& v, int nodeCount)
{
    RackDoc r;
    r.kind = (RackDoc::Kind) juce::jlimit (0, 2, (int) v.getProperty ("kind", 0));
    r.name = v.getProperty ("name", "").toString().toStdString();
    const auto node = [nodeCount] (const juce::var& x) { const auto n = (int) x; return n >= 0 && n < nodeCount ? n : -1; };
    const auto zone = [] (const juce::var& a, RackZone fallback) {
        if (! a.isArray() || a.size() != 4)
            return fallback;
        RackZone z;
        z.lo = juce::jlimit (0, 127, (int) a[0]);
        z.hi = juce::jlimit (z.lo, 127, (int) a[1]);
        z.fadeLo = juce::jlimit (z.lo, z.hi, (int) a[2]);
        z.fadeHi = juce::jlimit (z.fadeLo, z.hi, (int) a[3]);
        return z;
    };
    r.drum = (bool) v.getProperty ("drum", false) && r.kind == RackDoc::Kind::instrument;
    const auto readChains = [&] (const juce::var& list, std::vector<RackChain>& into, bool pads) {
        if (auto* chains = list.getArray())
        for (const auto& c : *chains)
        {
            RackChain chain;
            chain.name = c.getProperty ("name", "").toString().toStdString();
            chain.colour = (int) c.getProperty ("colour", -1);
            chain.active = (bool) c.getProperty ("active", true);
            chain.solo = (bool) c.getProperty ("solo", false);
            chain.volume = juce::jlimit (0.0f, 2.0f, (float) (double) c.getProperty ("volume", 1.0));
            chain.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) c.getProperty ("pan", 0.0));
            chain.key = zone (c["key"], RackZone {});
            chain.velocity = zone (c["velocity"], RackZone { 1, 127, 1, 127 });
            chain.select = zone (c["select"], RackZone { 0, 0, 0, 0 });
            if (auto* nodes = c["nodes"].getArray())
                for (const auto& n : *nodes)
                    if (const auto k = node (n); k >= 0)
                        chain.nodes.push_back (k);
            chain.mixer = node (c.getProperty ("mixer", -1));
            chain.gate = node (c.getProperty ("gate", -1));
            if (pads && r.drum)
            {
                if (const auto& pad = c["pad"]; pad.isArray() && pad.size() >= 4)
                {
                    chain.receive = juce::jlimit (-1, 127, (int) pad[0]);
                    chain.play = juce::jlimit (0, 127, (int) pad[1]);
                    chain.choke = juce::jlimit (0, rack::drumChokeGroups, (int) pad[2]);
                    chain.audioTo = juce::jmax (-1, (int) pad[3]);
                }
                if (auto* sends = c["sends"].getArray())
                    for (const auto& x : *sends)
                        if (x.isArray() && x.size() >= 2)
                            chain.sends.push_back ({ juce::jlimit (0.0f, 1.0f, (float) (double) x[0]), (bool) x[1] });
            }
            else if (! pads)
                chain.receive = -1;
            into.push_back (std::move (chain));
        }
    };
    readChains (v["chains"], r.chains, true);
    if (r.drum)
    {
        readChains (v["returns"], r.returns, false);
        for (auto& c : r.chains)
        {
            c.sends.resize (r.returns.size());
            if (c.audioTo >= (int) r.returns.size())
                c.audioTo = -1;
        }
        if (const auto& view = v["drumView"]; view.isArray() && view.size() >= 5)
        {
            r.showPads = (bool) view[0];
            r.showIO = (bool) view[1];
            r.showSends = (bool) view[2];
            r.showReturns = (bool) view[3];
            r.padScroll = juce::jlimit (0, rack::drumPadRows - 4, (int) view[4]);
        }
    }
    if (auto* macros = v["macros"].getArray())
        for (int k = 0; k < juce::jmin (rackMacroCount, macros->size()); ++k)
        {
            const auto& m = (*macros)[k];
            auto& macro = r.macros[(std::size_t) k];
            macro.name = m.getProperty ("name", "").toString().toStdString();
            macro.colour = (int) m.getProperty ("colour", -1);
            macro.value = juce::jlimit (0.0f, 127.0f, (float) (double) m.getProperty ("value", 0.0));
            macro.defaultValue = juce::jlimit (-1.0f, 127.0f, (float) (double) m.getProperty ("default", -1.0));
            macro.excludeFromRandom = (bool) m.getProperty ("noRandom", false);
            macro.excludeFromVariations = (bool) m.getProperty ("noVariations", false);
            if (auto* maps = m["maps"].getArray())
                for (const auto& x : *maps)
                    if (x.isArray() && x.size() >= 6)
                    {
                        RackMapping mapping;
                        mapping.target = (RackMapping::Target) juce::jlimit (0, 3, (int) x[0]);
                        mapping.node = node (x[1]);
                        mapping.chain = juce::jlimit (-1, (int) r.chains.size() - 1, (int) x[2]);
                        mapping.param = juce::jmax (0, (int) x[3]);
                        mapping.min = juce::jlimit (0.0f, 1.0f, (float) (double) x[4]);
                        mapping.max = juce::jlimit (0.0f, 1.0f, (float) (double) x[5]);
                        if (mapping.target != RackMapping::Target::param || mapping.node >= 0)
                            macro.mappings.push_back (mapping);
                    }
        }
    r.shownMacros = juce::jlimit (1, rackMacroCount, (int) v.getProperty ("shown", 8));
    r.selector = juce::jlimit (0.0f, 127.0f, (float) (double) v.getProperty ("selector", 0.0));
    r.selectorFiltersCtrl = (bool) v.getProperty ("selectorFiltersCtrl", false);
    if (auto* variations = v["variations"].getArray())
        for (const auto& x : *variations)
        {
            RackVariation variation;
            variation.name = x.getProperty ("name", "").toString().toStdString();
            if (auto* values = x["values"].getArray())
                for (int k = 0; k < juce::jmin (rackMacroCount, values->size()); ++k)
                {
                    const auto value = (float) (double) (*values)[k];
                    variation.has[(std::size_t) k] = value >= 0.0f;
                    variation.values[(std::size_t) k] = value >= 0.0f ? juce::jlimit (0.0f, 127.0f, value) : -1.0f;
                }
            r.variations.push_back (std::move (variation));
        }
    const auto variationIndex = [&r] (const juce::var& x) { const auto k = (int) x; return k >= 0 && k < (int) r.variations.size() ? k : -1; };
    r.selectedVariation = variationIndex (v.getProperty ("selectedVariation", -1));
    if (const auto& m = v["morph"]; m.isArray() && m.size() >= 5)
    {
        r.morphOn = (bool) m[0];
        r.morphA = variationIndex (m[1]);
        r.morphB = variationIndex (m[2]);
        r.morph = juce::jlimit (0.0f, 1.0f, (float) (double) m[3]);
        r.glideMs = juce::jlimit (0.0f, 60000.0f, (float) (double) m[4]);
    }
    if (auto* children = v["children"].getArray())
        for (const auto& c : *children)
            r.children.push_back (juce::jlimit (0, 4095, (int) c));
    if (const auto& view = v["view"]; view.isArray() && view.size() >= 7)
    {
        r.showMacros = (bool) view[0];
        r.showChains = (bool) view[1];
        r.showDevices = (bool) view[2];
        r.showVariations = (bool) view[3];
        r.zoneEditor = juce::jlimit (-1, 2, (int) view[4]);
        r.selectedChain = juce::jlimit (0, juce::jmax (0, rack::chainSlots (r) - 1), (int) view[5]);
        r.autoSelect = (bool) view[6];
    }
    return r;
}

// FL's Patcher graph (REA_rodada141): nodes (an effect or a channel each, nested Patchers too), cables, the host
// nodes' places and the extra outputs' inserts
juce::var patcherToVar (const PatcherDoc& doc)
{
    auto o = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> nodes;
    for (const auto& n : doc.nodes)
    {
        auto v = std::make_unique<juce::DynamicObject>();
        if (n.generator)
            v->setProperty ("channel", channelToVar (n.channel));
        else
            v->setProperty ("effect", effectToVar (n.effect));
        if (! n.label.empty())
            v->setProperty ("label", str (n.label));
        v->setProperty ("x", juce::roundToInt (n.x * 10.0f) / 10.0);
        v->setProperty ("y", juce::roundToInt (n.y * 10.0f) / 10.0);
        if (! n.busHeads.empty())
        {
            juce::Array<juce::var> heads;
            for (const auto& h : n.busHeads)
            {
                auto hv = std::make_unique<juce::DynamicObject>();
                hv->setProperty ("bus", h.bus);
                hv->setProperty ("gain", h.gain);
                if (h.head)
                    hv->setProperty ("head", effectToVar (*h.head));
                heads.add (juce::var (hv.release()));
            }
            v->setProperty ("busHeads", heads);
        }
        // the activated parameter ports (round 171b)
        auto list = [] (const std::vector<int>& from) {
            juce::Array<juce::var> a;
            for (const auto p : from)
                a.add (p);
            return a;
        };
        if (! n.paramIns.empty())
            v->setProperty ("paramIns", list (n.paramIns));
        if (! n.paramOuts.empty())
            v->setProperty ("paramOuts", list (n.paramOuts));
        nodes.add (juce::var (v.release()));
    }
    o->setProperty ("nodes", nodes);
    juce::Array<juce::var> wires;
    for (const auto& w : doc.wires)
    {
        // [kind (0 audio, 1 MIDI, 2 control), from, from port, to, to port, gain, on, curve…]
        juce::Array<juce::var> wire { (int) w.kind, w.from, w.fromPort, w.to, w.toPort, w.gain, w.enabled };
        for (const auto v : w.curve)
            wire.add (juce::roundToInt (v * 10000.0f) / 10000.0);
        wires.add (wire);
    }
    o->setProperty ("wires", wires);
    o->setProperty ("io", juce::Array<juce::var> { doc.inputX, doc.inputY, doc.outputX, doc.outputY });
    if (! doc.published.empty()) // [node, parameter, name, curve…] per Patcher parameter
    {
        juce::Array<juce::var> published;
        for (const auto& p : doc.published)
        {
            juce::Array<juce::var> entry { p.node, p.param, str (p.name) };
            for (const auto v : p.curve)
                entry.add (juce::roundToInt (v * 10000.0f) / 10000.0);
            published.add (entry);
        }
        o->setProperty ("published", published);
    }
    if (! doc.outputTracks.empty())
    {
        juce::Array<juce::var> tracks;
        for (const auto t : doc.outputTracks)
            tracks.add (t);
        o->setProperty ("outputTracks", tracks);
    }
    // the I/O nodes' activated MIDI pins (round 162)
    auto ports = [] (const std::vector<int>& v) {
        juce::Array<juce::var> a;
        for (const auto p : v)
            a.add (p);
        return a;
    };
    if (! doc.fromMidiPorts.empty())
        o->setProperty ("fromMidiPorts", ports (doc.fromMidiPorts));
    if (! doc.toMidiPorts.empty())
        o->setProperty ("toMidiPorts", ports (doc.toMidiPorts));
    if (doc.fromNotesHidden)
        o->setProperty ("fromNotesHidden", true);
    if (! doc.surface.empty()) // the Surface tab (round 151): [kind, name, value, value Y, x, y(, size: round 164)]
    {
        juce::Array<juce::var> controls;
        for (const auto& c : doc.surface)
        {
            juce::Array<juce::var> control { (int) c.kind, juce::String::fromUTF8 (c.name.c_str()), c.value, c.valueY, c.x, c.y };
            if (c.size != 1.0f || c.midi != SurfaceControl::Midi::none)
                control.add (c.size);
            if (c.midi != SurfaceControl::Midi::none) // a Dashboard control's MIDI (round 165): type, number, low, high
            {
                control.add ((int) c.midi);
                control.add (c.midiNumber);
                control.add (c.midiLow);
                control.add (c.midiHigh);
            }
            controls.add (control);
        }
        o->setProperty ("surface", controls);
        o->setProperty ("surfaceAt", juce::Array<juce::var> { doc.surfaceX, doc.surfaceY });
    }
    if (doc.isRack()) // (round 183c)
        o->setProperty ("rack", rackToVar (doc.rack.front()));
    return juce::var (o.release());
}

PatcherDoc patcherFromVar (const juce::var& v, const Song& song)
{
    PatcherDoc doc;
    if (auto* nodes = v["nodes"].getArray())
        for (const auto& n : *nodes)
        {
            PatchNode node;
            if (n.hasProperty ("channel"))
            {
                node.generator = true;
                node.channel = channelFromVar (n["channel"], song);
            }
            else if (auto fx = effectFromVar (n["effect"]))
                node.effect = std::move (*fx);
            else
                node.effect = makeEffectSlot (EffectKind::utility); // an effect from a future version: a pass-through
            node.label = n.getProperty ("label", "").toString().toStdString();
            node.x = (float) (double) n.getProperty ("x", 0.0);
            node.y = (float) (double) n.getProperty ("y", 0.0);
            if (auto* heads = n["busHeads"].getArray())
                for (const auto& h : *heads)
                {
                    PatchNode::BusHead head;
                    head.bus = juce::jlimit (1, 64, (int) h.getProperty ("bus", 1));
                    head.gain = juce::jlimit (0.0f, 16.0f, (float) (double) h.getProperty ("gain", 1.0));
                    if (h.hasProperty ("head"))
                        head.head = effectFromVar (h["head"]);
                    node.busHeads.push_back (std::move (head));
                }
            auto readParams = [] (const juce::var& a, std::vector<int>& out) {
                if (auto* list = a.getArray())
                    for (const auto& p : *list)
                        if (const auto x = (int) p; x >= 0 && x < patchParamOutputBase
                                                     && std::find (out.begin(), out.end(), x) == out.end())
                            out.insert (std::upper_bound (out.begin(), out.end(), x), x);
            };
            readParams (n["paramIns"], node.paramIns);
            readParams (n["paramOuts"], node.paramOuts);
            doc.nodes.push_back (std::move (node));
        }
    const auto count = (int) doc.nodes.size();
    if (auto* wires = v["wires"].getArray())
        for (const auto& w : *wires)
            if (w.isArray() && w.size() >= 7)
            {
                PatchWire wire;
                wire.kind = (PatchWire::Kind) juce::jlimit (0, 2, (int) w[0]);
                wire.from = juce::jlimit (patchSurface, count - 1, (int) w[1]); // (the Surface node, round 151)
                wire.fromPort = juce::jmax (0, (int) w[2]);
                wire.to = juce::jlimit (patchToHost, count - 1, (int) w[3]);
                wire.toPort = juce::jmax (0, (int) w[4]);
                wire.gain = juce::jlimit (-16.0f, 16.0f, (float) (double) w[5]);
                wire.enabled = (bool) w[6];
                for (int k = 7; k < w.size(); ++k)
                    wire.curve.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) w[k]));
                if (wire.from != patchToHost && wire.to != patchFromHost && (wire.from != patchSurface || wire.kind == PatchWire::Kind::control))
                    doc.wires.push_back (std::move (wire));
            }
    if (const auto& io = v["io"]; io.isArray() && io.size() == 4)
    {
        doc.inputX = (float) (double) io[0];
        doc.inputY = (float) (double) io[1];
        doc.outputX = (float) (double) io[2];
        doc.outputY = (float) (double) io[3];
    }
    if (auto* published = v["published"].getArray())
        for (const auto& p : *published)
            if (p.isArray() && p.size() >= 3)
            {
                PatchParam param;
                param.node = juce::jlimit (-1, count - 1, (int) p[0]);
                param.param = juce::jmax (0, (int) p[1]);
                param.name = p[2].toString().toStdString();
                for (int k = 3; k < p.size(); ++k)
                    param.curve.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) p[k]));
                doc.published.push_back (std::move (param));
            }
    if (auto* tracks = v["outputTracks"].getArray())
        for (const auto& t : *tracks)
            doc.outputTracks.push_back (juce::jlimit (-1, maxMixerTracks - 1, (int) t));
    auto readPorts = [] (const juce::var& a, std::vector<int>& out) {
        if (auto* list = a.getArray())
            for (const auto& p : *list)
                if (const auto port = (int) p; port >= 0 && port < patchMidiPorts && std::find (out.begin(), out.end(), port) == out.end())
                    out.push_back (port);
        std::sort (out.begin(), out.end());
    };
    readPorts (v["fromMidiPorts"], doc.fromMidiPorts);
    readPorts (v["toMidiPorts"], doc.toMidiPorts);
    doc.fromNotesHidden = (bool) v.getProperty ("fromNotesHidden", false);
    if (auto* controls = v["surface"].getArray())
        for (const auto& c : *controls)
            if (c.isArray() && c.size() >= 6)
            {
                SurfaceControl s;
                s.kind = (SurfaceControl::Kind) juce::jlimit (0, 4, (int) c[0]);
                s.name = c[1].toString().toStdString();
                s.value = juce::jlimit (0.0f, 1.0f, (float) (double) c[2]);
                s.valueY = juce::jlimit (0.0f, 1.0f, (float) (double) c[3]);
                s.x = (float) (double) c[4];
                s.y = (float) (double) c[5];
                if (c.size() >= 7)
                    s.size = juce::jlimit (0.5f, 3.0f, (float) (double) c[6]);
                if (c.size() >= 11)
                {
                    const auto type = (int) c[7];
                    if (type == 0 || type == 1 || type == 2 || type == 3 || type == 5)
                    {
                        s.midi = (SurfaceControl::Midi) type;
                        s.midiNumber = juce::jlimit (0, 16383, (int) c[8]);
                        s.midiLow = juce::jlimit (0, 16383, (int) c[9]);
                        s.midiHigh = juce::jlimit (0, 16383, (int) c[10]);
                    }
                }
                doc.surface.push_back (std::move (s));
            }
    if (const auto& at = v["surfaceAt"]; at.isArray() && at.size() == 2)
    {
        doc.surfaceX = (float) (double) at[0];
        doc.surfaceY = (float) (double) at[1];
    }
    // a Surface cable whose control is gone
    std::erase_if (doc.wires, [&doc] (const PatchWire& w) { return w.from == patchSurface && w.fromPort >= doc.surfaceOutputs(); });
    if (v.hasProperty ("rack")) // rack mode (round 183c): its cables, Surface and published parameters come from the RackDoc
    {
        doc.rack.push_back (rackFromVar (v["rack"], (int) doc.nodes.size()));
        rack::compile (doc);
    }
    return doc;
}

namespace
{
// Round 167a: Fruity Send's "Send to" is normalised over 0..500 inserts since round 166d; older LYNX versions read it
// over 0..127. A file stores every "Send to" value of a slot (its parameter, its A/B other side, the automation clips
// and event lanes that move it) over 0..127 when all of them target an insert up to 127 — older versions then read the
// same insert — and over today's span only when one of them needs more. "sendSpan" says which, on the slot and on each
// automation / lane of it.
double rounded4 (float v) { return juce::roundToInt (v * 10000.0f) / 10000.0; }

// value [1] of every [tick, value, ...] entry, written over `span`
void rewriteSendValues (juce::var& list, int span)
{
    if (auto* entries = list.getArray())
        for (auto& entry : *entries)
            if (auto* e = entry.getArray(); e != nullptr && e->size() >= 2)
                e->set (1, rounded4 (flbasic::sendTrackValueInSpan ((float) (double) (*e)[1], span)));
}

// the event lanes of a pattern that toJson writes, in its order
std::vector<const PatternEvents*> savedLanes (const Pattern& p)
{
    std::vector<const PatternEvents*> lanes;
    for (const auto& lane : p.events)
        if (! lane.events.empty() && lane.target.kind != AutomationTargetKind::none)
            lanes.push_back (&lane);
    return lanes;
}

void storeSendSpans (const Song& song, juce::DynamicObject& root)
{
    auto* mixer = root.getProperty ("mixer").getArray();
    auto* automations = root.getProperty ("automations").getArray();
    auto* patterns = root.getProperty ("patterns").getArray();
    if (mixer == nullptr)
        return;
    for (int t = 0; t < (int) song.mixer.size() && t < mixer->size(); ++t)
    {
        auto* effects = (*mixer)[t]["effects"].getArray();
        for (int s = 0; effects != nullptr && s < (int) song.mixer[(std::size_t) t].effects.size() && s < effects->size(); ++s)
        {
            const auto& slot = song.mixer[(std::size_t) t].effects[(std::size_t) s];
            if (slot.kind != EffectKind::fruitySend)
                continue;
            const AutomationTarget target { AutomationTargetKind::effectParam, t, s, 3 };
            const auto span = flbasic::saveSendSpanOf (song, t, s);

            auto* fx = (*effects)[s].getDynamicObject();
            if (fx == nullptr)
                continue;
            fx->setProperty ("sendSpan", span);
            if (span == maxMixerTracks - 1)
                continue;
            if (auto* params = fx->getProperty ("params").getArray(); params != nullptr && params->size() > 3)
                params->set (3, rounded4 (flbasic::sendTrackValueInSpan (slot.params[3], span)));
            if (auto* other = fx->getProperty ("abOther").getArray(); other != nullptr && other->size() > 3 && slot.abOther.size() > 3)
                other->set (3, rounded4 (flbasic::sendTrackValueInSpan (slot.abOther[3], span)));
            for (int i = 0; automations != nullptr && i < (int) song.automations.size() && i < automations->size(); ++i)
                if (song.automations[(std::size_t) i].target == target)
                    if (auto* o = (*automations)[i].getDynamicObject())
                    {
                        o->setProperty ("sendSpan", span);
                        auto points = o->getProperty ("points");
                        rewriteSendValues (points, span);
                    }
            for (int pi = 0; patterns != nullptr && pi < (int) song.patterns.size() && pi < patterns->size(); ++pi)
            {
                const auto lanes = savedLanes (song.patterns[(std::size_t) pi]);
                auto* saved = (*patterns)[pi]["events"].getArray();
                for (int li = 0; saved != nullptr && li < (int) lanes.size() && li < saved->size(); ++li)
                    if (lanes[(std::size_t) li]->target == target)
                        if (auto* o = (*saved)[li].getDynamicObject())
                        {
                            o->setProperty ("sendSpan", span);
                            auto events = o->getProperty ("events");
                            rewriteSendValues (events, span);
                        }
            }
        }
    }
}

// Loading: the automations and lanes of a Send's "Send to" come back to today's span. Their own "sendSpan" when they
// have one; written by 0.1.21 (none of their own), the slot's; older files: 0..127.
void readSendSpans (Song& song, const juce::var& root)
{
    const auto spanOf = [&] (const AutomationTarget& t, const juce::var& holder) {
        if (holder.hasProperty ("sendSpan"))
            return (int) holder["sendSpan"];
        const auto fx = root["mixer"][t.index]["effects"][t.slot];
        return (int) fx.getProperty ("sendSpan", flbasic::legacySendSpan);
    };
    if (auto* automations = root["automations"].getArray())
        for (int i = 0; i < (int) song.automations.size() && i < automations->size(); ++i)
        {
            auto& a = song.automations[(std::size_t) i];
            if (flbasic::sendToTargetOf (song, a.target) == nullptr)
                continue;
            const auto span = spanOf (a.target, (*automations)[i]);
            for (auto& pt : a.points)
                pt.value = flbasic::rescaleSendTrackValue (pt.value, span);
        }
    if (auto* patterns = root["patterns"].getArray())
        for (int pi = 0; pi < (int) song.patterns.size() && pi < patterns->size(); ++pi)
        {
            auto& p = song.patterns[(std::size_t) pi];
            const auto* saved = (*patterns)[pi]["events"].getArray();
            if (saved == nullptr)
                continue;
            // the lanes kept at load, in file order (see the loader: a target and at least one event)
            int li = 0;
            for (auto& lane : p.events)
            {
                while (li < saved->size())
                {
                    const auto& l = (*saved)[li];
                    const auto& t = l["target"];
                    const auto* ev = l["events"].getArray();
                    if (t.isArray() && t.size() == 4 && ev != nullptr && ! ev->isEmpty())
                        break;
                    ++li;
                }
                if (li >= saved->size())
                    break;
                if (flbasic::sendToTargetOf (song, lane.target) != nullptr)
                {
                    const auto span = spanOf (lane.target, (*saved)[li]);
                    for (auto& e : lane.events)
                        e.value = flbasic::rescaleSendTrackValue (e.value, span);
                }
                ++li;
            }
        }
}
} // namespace

juce::var effectToVar (const EffectSlot& slot)
{
    auto e = std::make_unique<juce::DynamicObject>();
    e->setProperty ("kind", effectId (slot.kind));
    e->setProperty ("on", slot.enabled);
    juce::Array<juce::var> params;
    // the named parameters and the hidden ones after them that hold a value (issue #67: the Parametric EQ's FL law,
    // processing and band mask live past its 36 named ones); each value reads back as the same float (FL's raw values,
    // the hardware insert's round trip over 65536 steps)
    const auto used = savedEffectParamCount (slot);
    for (std::size_t i = 0; i < used && i < slot.params.size(); ++i)
        params.add (savedEffectParamValue (slot.params[i]));
    e->setProperty ("params", params);
    if (slot.kind == EffectKind::fruitySend) // the span "Send to" is normalised over (round 166d: 500 inserts)
        e->setProperty ("sendSpan", maxMixerTracks - 1);

    if (slot.mix < 1.0f)
        e->setProperty ("mix", juce::roundToInt (slot.mix * 10000.0f) / 10000.0);
    if (! slot.smartDisable)
        e->setProperty ("smartDisable", false);
    if (slot.row >= 0) // the mixer row (FL's slot places, round 170b); absent = right after the previous effect
        e->setProperty ("row", slot.row);
    if (slot.look != 0) // part of a Master look (round 189k)
        e->setProperty ("look", (int) slot.look);
    if (slot.goal != 0) // a one-knob effect (round 190bu): the goal and where its knob stands
    {
        e->setProperty ("goal", (int) slot.goal);
        e->setProperty ("goalAmount", juce::roundToInt (slot.goalAmount * 10000.0f) / 10000.0);
    }
    if (slot.sidechain >= 0)
        e->setProperty ("sidechain", slot.sidechain);
    if (slot.sidechain2 >= 0)
        e->setProperty ("sidechain2", slot.sidechain2);
    if (slot.sendTracks[0] > 0) // a Transmitter's transient / sustain sends (round 167b)
        e->setProperty ("sendTransient", slot.sendTracks[0]);
    if (slot.sendTracks[1] > 0)
        e->setProperty ("sendSustain", slot.sendTracks[1]);
    if (! slot.state.empty()) // FL's raw state of an exact effect (Vocodex), base64
        e->setProperty ("state", juce::Base64::toBase64 (slot.state.data(), slot.state.size()));
    if (! slot.samplePath.empty()) // Newtone / Newtime source sample
        e->setProperty ("sample", str (slot.samplePath));
    if (! slot.flOrigin.empty()) // the FL effect it was imported from (round 171e)
        e->setProperty ("flOrigin", flOriginToVar (slot.flOrigin));
    if (! slot.presetName.empty()) // the editor's preset name and A/B compare (round 129)
        e->setProperty ("preset", str (slot.presetName));
    if (! slot.abOther.empty())
    {
        juce::Array<juce::var> other;
        for (const auto v : slot.abOther)
            other.add (savedEffectParamValue (v));
        e->setProperty ("abOther", other);
        if (slot.abShowingB)
            e->setProperty ("abB", true);
    }

    if (slot.kind == EffectKind::plugin)
        e->setProperty ("plugin", pluginToVar (slot.plugin));
    else if (slot.kind == EffectKind::grossBeat && slot.plugin.midiInPort >= 0) // round 190ee: FL's wrapper MIDI port
        e->setProperty ("midiInPort", slot.plugin.midiInPort);
    else if (slot.isSubstitute()) // a LYNX substitute: the original plugin travels with it, unchanged
        e->setProperty ("substituteFor", pluginToVar (slot.substituteFor));

    if (slot.kind == EffectKind::patcher || slot.kind == EffectKind::controlSurface) // (a Control Surface's controls: round 164)
        e->setProperty ("patcher", patcherToVar (slot.patcher));

    if (slot.kind == EffectKind::parallel) // container branches, each with its own chain
    {
        juce::Array<juce::var> branches;
        for (const auto& b : slot.branches)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("gain", b.gain);
            juce::Array<juce::var> chain;
            for (const auto& child : b.chain)
                chain.add (effectToVar (child));
            o->setProperty ("chain", chain);
            branches.add (juce::var (o.release()));
        }
        e->setProperty ("branches", branches);
        if (! slot.links.empty())
        {
            juce::Array<juce::var> links;
            for (const auto& link : slot.links)
            {
                auto o = std::make_unique<juce::DynamicObject>();
                o->setProperty ("branch", link.branch);
                o->setProperty ("source", link.source);
                o->setProperty ("target", link.target);
                o->setProperty ("param", link.param);
                juce::Array<juce::var> curve;
                for (auto v : link.curve)
                    curve.add (v);
                o->setProperty ("curve", curve);
                links.add (juce::var (o.release()));
            }
            e->setProperty ("links", links);
        }
    }
    return juce::var (e.release());
}

std::optional<EffectSlot> effectFromVar (const juce::var& e)
{
    const auto kind = effectFromId (e["kind"].toString());
    if (! kind)
        return std::nullopt;

    // start from the effect's defaults: parameters missing from the file (parameter added to the
    // effect later) stay at their default instead of 0
    auto slot = *kind == EffectKind::plugin ? EffectSlot {} : makeEffectSlot (*kind);
    slot.kind = *kind;
    slot.enabled = (bool) e.getProperty ("on", true);
    slot.mix = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("mix", 1.0));
    slot.smartDisable = (bool) e.getProperty ("smartDisable", true);
    slot.row = juce::jlimit (-1, maxEffectSlots - 1, (int) e.getProperty ("row", -1));
    slot.look = (std::uint8_t) juce::jlimit (0, 255, (int) e.getProperty ("look", 0));
    slot.goal = (std::uint8_t) juce::jlimit (0, 255, (int) e.getProperty ("goal", 0));
    slot.goalAmount = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("goalAmount", 0.5));

    if (auto* params = e["params"].getArray())
    {
        for (int i = 0; i < params->size() && i < maxEffectParams; ++i)
            slot.params[(std::size_t) i] = juce::jlimit (0.0f, 1.0f, (float) (double) (*params)[i]);
        upgradeLoadedEffectParams (slot, (std::size_t) params->size()); // engines that changed since the file was saved
    }
    const auto sendSpan = *kind == EffectKind::fruitySend ? (int) e.getProperty ("sendSpan", flbasic::legacySendSpan) : 0;
    if (sendSpan > 0) // saved over 0..127 before round 166d: the same insert over today's span
        slot.params[3] = flbasic::rescaleSendTrackValue (slot.params[3], sendSpan);

    slot.plugin = pluginFromVar (e["plugin"]);
    if (*kind == EffectKind::grossBeat) // round 190ee
        slot.plugin.midiInPort = juce::jlimit (-1, 255, (int) e.getProperty ("midiInPort", -1));
    if (*kind != EffectKind::plugin && e.hasProperty ("substituteFor"))
        slot.substituteFor = pluginFromVar (e["substituteFor"]);
    slot.sidechain = juce::jlimit (-1, maxMixerTracks - 1, (int) e.getProperty ("sidechain", -1));
    slot.sidechain2 = juce::jlimit (-1, maxMixerTracks - 1, (int) e.getProperty ("sidechain2", -1));
    slot.sendTracks[0] = juce::jlimit (0, maxMixerTracks - 1, (int) e.getProperty ("sendTransient", 0));
    slot.sendTracks[1] = juce::jlimit (0, maxMixerTracks - 1, (int) e.getProperty ("sendSustain", 0));
    slot.samplePath = e.getProperty ("sample", "").toString().toStdString();
    slot.presetName = e.getProperty ("preset", "").toString().toStdString();
    if (auto* other = e["abOther"].getArray())
    {
        for (int i = 0; i < other->size() && i < maxEffectParams; ++i)
            slot.abOther.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) (*other)[i]));
        if (sendSpan > 0 && slot.abOther.size() > 3)
            slot.abOther[3] = flbasic::rescaleSendTrackValue (slot.abOther[3], sendSpan);
        slot.abShowingB = (bool) e.getProperty ("abB", false);
    }
    if (e.hasProperty ("state"))
    {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64 (raw, e["state"].toString()))
            slot.state.assign ((const std::uint8_t*) raw.getData(), (const std::uint8_t*) raw.getData() + raw.getDataSize());
    }
    if (e.hasProperty ("flOrigin"))
        slot.flOrigin = flOriginFromVar (e["flOrigin"]);

    if (auto* branches = e["branches"].getArray())
        for (const auto& b : *branches)
        {
            EffectBranch branch;
            branch.gain = juce::jlimit (0.0f, 4.0f, (float) (double) b.getProperty ("gain", 1.0));
            if (auto* chain = b["chain"].getArray())
                for (const auto& child : *chain)
                    if (auto c = effectFromVar (child))
                        branch.chain.push_back (std::move (*c)); // containers can be nested
            slot.branches.push_back (std::move (branch));
        }
    if ((*kind == EffectKind::patcher || *kind == EffectKind::controlSurface) && e.hasProperty ("patcher"))
        slot.patcher = patcherFromVar (e["patcher"], Song {});
    if (auto* links = e["links"].getArray())
        for (const auto& l : *links)
        {
            EffectLink link;
            link.branch = (int) l.getProperty ("branch", 0);
            link.source = (int) l.getProperty ("source", 0);
            link.target = (int) l.getProperty ("target", 0);
            link.param = juce::jlimit (0, maxEffectParams - 1, (int) l.getProperty ("param", 0));
            if (auto* curve = l["curve"].getArray())
                for (const auto& v : *curve)
                    link.curve.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) v));
            if (link.branch >= 0 && link.branch < (int) slot.branches.size() && link.curve.size() >= 2)
                slot.links.push_back (std::move (link));
        }
    return slot;
}

std::optional<EffectKind> effectFromId (const juce::String& id)
{
    for (auto k : { EffectKind::eq, EffectKind::compressor, EffectKind::delay, EffectKind::reverb, EffectKind::plugin,
                    EffectKind::limiter, EffectKind::filter, EffectKind::distortion, EffectKind::chorus, EffectKind::parametricEq, EffectKind::utility, EffectKind::volumeGate, EffectKind::peakController, EffectKind::pitchShift, EffectKind::freqShift, EffectKind::parallel, EffectKind::lofi, EffectKind::effector, EffectKind::crossover, EffectKind::rhythmGain, EffectKind::autoPan, EffectKind::vocoder, EffectKind::pitchCorrect, EffectKind::squeeze, EffectKind::vintagePhaser, EffectKind::vocodex, EffectKind::maximus, EffectKind::fruityLimiter, EffectKind::fruityCompressor, EffectKind::fruityWaveShaper, EffectKind::fruitySoftClipper, EffectKind::reeverb2, EffectKind::reeverb1, EffectKind::fruityDelay2, EffectKind::fruityDelay3,
                    EffectKind::stereoShaper, EffectKind::stereoEnhancer, EffectKind::transientProcessor, EffectKind::spreader,
                    EffectKind::fruityChorus, EffectKind::hyperChorus, EffectKind::vintageChorus, EffectKind::fruityPanOMatic, EffectKind::fruityBalance, EffectKind::fruityBloodOverdrive, EffectKind::grossBeat,
                    EffectKind::fruityChorus, EffectKind::hyperChorus, EffectKind::vintageChorus,
                    EffectKind::fruityDelayBank, EffectKind::fruityMultiband,
                    EffectKind::fruityFastDist, EffectKind::fruityFlangus, EffectKind::fruityFlanger, EffectKind::luxeVerb, EffectKind::convolver,
                    EffectKind::equo, EffectKind::flPitchShifter,
                    EffectKind::frequencySplitter,
                    EffectKind::fruityFastLP, EffectKind::fruityFreeFilter, EffectKind::fruityFilter, EffectKind::hardcore, EffectKind::distructor, EffectKind::multibandDelay,
                    EffectKind::lovePhilter,
                    EffectKind::newtone, EffectKind::newtime, EffectKind::facetEq, EffectKind::apexLimiter,
                    EffectKind::ridgeStrip, EffectKind::glowLeveler, EffectKind::hushDeEsser, EffectKind::aether, EffectKind::ember, EffectKind::lynxTune, EffectKind::fruityPhaser,
                    EffectKind::epochVerb, EffectKind::atrium, EffectKind::foilPlate, EffectKind::nebula, EffectKind::halo, EffectKind::horizon,
                    EffectKind::echoform, EffectKind::spiralEcho, EffectKind::orbit, EffectKind::latticeMod,
                    EffectKind::fruitySend,
                    EffectKind::crestCompressor, EffectKind::prismMultiband, EffectKind::sentryGate,
                    EffectKind::quellDenoiser, EffectKind::geminiDoubler, EffectKind::formulaController, EffectKind::voiceShift, EffectKind::patcher, EffectKind::emphasizer, EffectKind::flFrequencyShifter, EffectKind::flLowLifter, EffectKind::waveCandy, EffectKind::visualizer,
                    EffectKind::flPhaseInverter, EffectKind::flCenter, EffectKind::flBassBoost, EffectKind::fl7BandEq, EffectKind::flDelay1,
                    EffectKind::pocketDistortion, EffectKind::pocketLimiterClassic, EffectKind::pocketLimiter, EffectKind::pocketLeveller,
                    EffectKind::pocketGate, EffectKind::pocketTranceDelay, EffectKind::pocketTapeDelay, EffectKind::pocketMultiFx,
                    EffectKind::pocketAutoduck, EffectKind::pocketChorus, EffectKind::pocketSpacer, EffectKind::pocketStereoizer,
                    EffectKind::pocketWowFlutter, EffectKind::pocketPhaser, EffectKind::pocketFlanger, EffectKind::pocketTremolo,
                    EffectKind::pocketWaveshaper, EffectKind::pocketCompressor, EffectKind::pocketSpreader,
                    EffectKind::pocketComb, // round 190by
                    EffectKind::pocketMultiband, // round 190by
                    EffectKind::pocketTapeStop, // round 190by
                    EffectKind::pocketEq, EffectKind::pocketGraphicEq, EffectKind::pocketFilter, EffectKind::pocketReverbClassic,
                    EffectKind::pocketReverb2, EffectKind::pocketReverb, // round 190dw
                    EffectKind::xyController, EffectKind::xyzController, EffectKind::controlSurface, EffectKind::hardwareInsert,
                    EffectKind::fruityMute2, EffectKind::bigClock, EffectKind::tuner, EffectKind::htmlNotebook,
                    EffectKind::scratcher, EffectKind::transmitter, EffectKind::flEmphasis, EffectKind::flTransporter,
                    EffectKind::kilnChannel, EffectKind::fretChannel, EffectKind::shellChannel, EffectKind::strikeShaper, EffectKind::rackMixer,
                    EffectKind::edison })
        if (id == effectId (k))
            return k;

    return std::nullopt; // effect from a future version: ignored
}

const char* targetKindName (AutomationTargetKind k)
{
    switch (k)
    {
        case AutomationTargetKind::channelVolume: return "channelVolume";
        case AutomationTargetKind::channelPan: return "channelPan";
        case AutomationTargetKind::channelParam: return "channelParam";
        case AutomationTargetKind::mixerVolume: return "mixerVolume";
        case AutomationTargetKind::mixerPan: return "mixerPan";
        case AutomationTargetKind::effectParam: return "effectParam";
        case AutomationTargetKind::tempo: return "tempo";
        case AutomationTargetKind::effectMix: return "effectMix";
        case AutomationTargetKind::mixerSend: return "mixerSend";
        case AutomationTargetKind::vcaLevel: return "vcaLevel";
        case AutomationTargetKind::channelAdjust: return "channelAdjust";
        case AutomationTargetKind::none: break;
    }
    return "none";
}

AutomationTargetKind targetKindFromName (const juce::String& name)
{
    for (auto k : { AutomationTargetKind::channelVolume, AutomationTargetKind::channelPan, AutomationTargetKind::channelParam,
                    AutomationTargetKind::mixerVolume, AutomationTargetKind::mixerPan, AutomationTargetKind::effectParam,
                    AutomationTargetKind::tempo, AutomationTargetKind::effectMix, AutomationTargetKind::mixerSend,
                    AutomationTargetKind::vcaLevel, AutomationTargetKind::channelAdjust })
        if (name == targetKindName (k))
            return k;

    return AutomationTargetKind::none;
}
// Online Sessions (round 168a): uids as 16 hex digits, stand-ins as small objects
juce::var uidVar (std::uint64_t uid) { return str (uids::toHex (uid)); }
std::uint64_t uidFrom (const juce::var& v) { return uids::fromHex (v.toString().toStdString()); }

// A saved automation point's curve mode (round 190bw): the fourth element when present; without it a bent segment
// comes from a project saved before FL's curve modes and keeps LYNX's older law, a straight one is a Single curve.
std::uint8_t savedPointMode (const juce::var& pt)
{
    if (pt.size() >= 4)
        return (std::uint8_t) juce::jlimit (0, (int) flcurve::modeCount - 1, (int) pt[3]);
    return pt.size() >= 3 && (double) pt[2] != 0.0 ? AutomationPoint::legacyCurve : (std::uint8_t) 0;
}

juce::var standInsToVar (const std::vector<StandIn>& list)
{
    juce::Array<juce::var> out;
    for (const auto& s : list)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        if (s.pattern != 0)
            o->setProperty ("pattern", uidVar (s.pattern));
        if (s.loop)
            o->setProperty ("loop", true);
        o->setProperty ("path", str (s.path));
        o->setProperty ("hash", str (s.sourceHash));
        o->setProperty ("length", (juce::int64) s.lengthTicks);
        if (! s.plugin.empty())
            o->setProperty ("plugin", str (s.plugin));
        if (! s.by.empty())
            o->setProperty ("by", str (s.by));
        out.add (juce::var (o.release()));
    }
    return out;
}

std::vector<StandIn> standInsFromVar (const juce::var& v)
{
    std::vector<StandIn> out;
    if (auto* list = v.getArray())
        for (const auto& o : *list)
        {
            StandIn s;
            s.pattern = o.hasProperty ("pattern") ? uidFrom (o["pattern"]) : 0;
            s.loop = (bool) o.getProperty ("loop", false);
            s.path = o["path"].toString().toStdString();
            s.sourceHash = o["hash"].toString().toStdString();
            s.lengthTicks = juce::jmax<juce::int64> (0, (juce::int64) o.getProperty ("length", 0));
            s.plugin = o["plugin"].toString().toStdString();
            s.by = o["by"].toString().toStdString();
            if (! s.path.empty())
                out.push_back (std::move (s));
        }
    return out;
}

// FL's legacy Pattern Blocks (round 170f): [[pattern, start], ...]
juce::var blocksToVar (const std::vector<PatternBlock>& list)
{
    juce::Array<juce::var> out;
    for (const auto& b : list)
        out.add (juce::Array<juce::var> { b.pattern, (juce::int64) b.start });
    return out;
}

std::vector<PatternBlock> blocksFromVar (const juce::var& v, const Song& song)
{
    std::vector<PatternBlock> out;
    if (auto* list = v.getArray())
        for (const auto& b : *list)
            if (b.isArray() && b.size() == 2)
                out.push_back ({ (int) b[0], (std::int64_t) (juce::int64) b[1] });
    patternblocks::normalize (out, (int) song.patterns.size());
    return out;
}

juce::var clipsToVar (const std::vector<Clip>& list)
{
    juce::Array<juce::var> clips;

    // [pattern, track, start, length, offset, kind (0 pattern, 1 audio, 2 automation), channel or automation]
    for (const auto& c : list)
    {
        juce::Array<juce::var> clip { c.pattern, c.track, (juce::int64) c.start, (juce::int64) c.length,
                                      (juce::int64) c.offset, (int) c.kind,
                                      c.kind == ClipKind::automation ? c.automation : c.channel };

        // [.., fade-in, fade-out, gain, muted, sub-tick offset, pan, pitch, reversed, stretch] up to the last value that
        // is not the default (round 167k: FL 2026's clip pitch and clip reverse, keys 7/8/9; 167l: the clip stretch)
        const auto fraction = juce::roundToInt (c.offsetFraction * 10000.0f) / 10000.0;
        const auto pan = juce::roundToInt (c.pan * 1000.0f) / 1000.0;
        const auto pitch = juce::roundToInt (c.pitch * 1000.0f) / 1000.0;
        const auto stretch = std::round (c.stretch * 1.0e6) / 1.0e6;
        const int used = c.followKey || c.keyShift != 0 ? 14 : c.fadeCurve != 0 ? 13 : c.linearFades != 0 || c.flFadeFlags != 0 ? 12 : c.fadeInTension != 0.0f || c.fadeOutTension != 0.0f ? 11 : c.uid != 0 ? 10 : stretch != 1.0 ? 9 : c.reversed ? 8 : pitch != 0.0 ? 7 : pan != 0.0 ? 6 : fraction != 0.0 ? 5 : c.muted ? 4
                       : (c.fadeIn > 0 || c.fadeOut > 0 || c.gain != 1.0f) ? 3 : 0;
        if (used > 0)
        {
            clip.add ((juce::int64) c.fadeIn);
            clip.add ((juce::int64) c.fadeOut);
            // (round 166e: the gain handle reaches −96 dB, so the value keeps 6 significant digits)
            clip.add ([g = (double) c.gain] {
                if (g <= 0.0)
                    return 0.0;
                const auto scale = std::pow (10.0, 5.0 - std::floor (std::log10 (g)));
                return std::round (g * scale) / scale;
            }());
            if (used >= 4)
                clip.add (c.muted);
            if (used >= 5)
                clip.add (fraction);
            if (used >= 6)
                clip.add (pan);
            if (used >= 7)
                clip.add (pitch);
            if (used >= 8)
                clip.add (c.reversed);
            if (used >= 9)
                clip.add (stretch);
            if (used >= 10)
                clip.add (uidVar (c.uid)); // Online Sessions (round 168a)
            if (used >= 11)
            {
                clip.add ((double) c.fadeInTension); // FL's fade curves, for the export (round 180d)
                clip.add ((double) c.fadeOutTension);
            }
            if (used >= 12)
            {
                clip.add ((int) c.linearFades); // straight fades: FL's shape 1, or a clip from before round 180d
                clip.add ((juce::int64) c.flFadeFlags);
            }
            if (used >= 13)
                clip.add ((int) c.fadeCurve); // comp crossfades (round 177c): index 21, after round 180d's fade fields
            if (used >= 14)
            {
                clip.add (c.followKey); // follows the project key, and the part of the pitch that is the key's (round 190ds)
                clip.add ((int) c.keyShift);
            }
        }

        clips.add (clip);
    }

    return clips;
}

// Needs channels, patterns and automations already loaded to validate the indices.
std::vector<Clip> clipsFromVar (const juce::var& v, const Song& song)
{
    std::vector<Clip> out;

    if (auto* clips = v.getArray())
        for (const auto& c : *clips)
        {
            if (! c.isArray() || c.size() < 5)
                continue;

            Clip clip;
            clip.pattern = (int) c[0];
            clip.track = juce::jmax (0, (int) c[1]);
            clip.start = juce::jmax<juce::int64> (0, (juce::int64) c[2]);
            clip.length = (juce::int64) c[3];
            clip.offset = juce::jmax<juce::int64> (0, (juce::int64) c[4]);

            if (c.size() >= 10)
            {
                clip.fadeIn = juce::jmax<juce::int64> (0, (juce::int64) c[7]);
                clip.fadeOut = juce::jmax<juce::int64> (0, (juce::int64) c[8]);
                clip.gain = juce::jlimit (0.0f, 63.1f, (float) (double) c[9]); // FL: +36 dB
                clip.muted = c.size() >= 11 && (bool) c[10];
                clip.offsetFraction = c.size() >= 12 ? juce::jlimit (-0.5f, 0.5f, (float) (double) c[11]) : 0.0f;
                clip.pan = c.size() >= 13 ? juce::jlimit (-1.0f, 1.0f, (float) (double) c[12]) : 0.0f;
                clip.pitch = c.size() >= 14 ? juce::jlimit (-12.0f, 12.0f, (float) (double) c[13]) : 0.0f;
                clip.reversed = c.size() >= 15 && (bool) c[14];
                clip.stretch = c.size() >= 16 ? juce::jlimit (0.01, 100.0, (double) c[15]) : 1.0;
                clip.uid = c.size() >= 17 ? uidFrom (c[16]) : 0;
                clip.fadeInTension = c.size() >= 19 ? (float) (double) c[17] : 0.0f;
                clip.fadeOutTension = c.size() >= 19 ? (float) (double) c[18] : 0.0f;
                clip.linearFades = c.size() >= 20 ? (std::uint8_t) ((int) c[19] & 3) : (std::uint8_t) 0;
                clip.flFadeFlags = c.size() >= 21 ? (std::uint32_t) (juce::int64) c[20] : 0u;
                clip.fadeCurve = c.size() >= 22 ? (std::uint8_t) juce::jlimit (0, 3, (int) c[21]) : 0;
                clip.followKey = c.size() >= 24 && (bool) c[22];
                clip.keyShift = c.size() >= 24 ? (std::int8_t) juce::jlimit (-12, 12, (int) c[23]) : (std::int8_t) 0;
            }

            if (c.size() >= 7 && (int) c[5] == 2)
            {
                clip.kind = ClipKind::automation;
                clip.automation = (int) c[6];

                if (clip.automation >= 0 && clip.automation < (int) song.automations.size() && clip.length > 0)
                    out.push_back (clip);
            }
            else if (c.size() >= 7 && (int) c[5] == 1) // [.., kind, channel]; old files have only 5 fields
            {
                clip.kind = ClipKind::audio;
                clip.channel = (int) c[6];

                if (clip.channel >= 0 && clip.channel < (int) song.channels.size() && clip.length > 0)
                    out.push_back (clip);
            }
            else if (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && clip.length > 0)
                out.push_back (clip);
        }

    return out;
}

// The clip grid (round 181a). One object per clip: its settings, and the Clip in the form the Playlist's clips are saved in.
juce::var clipGridToVar (const ClipGrid& grid)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("quant", (int) grid.globalQuant);
    if (grid.backOnStop)
        root->setProperty ("backOnStop", true);
    if (! grid.followActions)
        root->setProperty ("followActions", false);
    // round 190dr: the Session mixer's sections (only when not the defaults: older projects read the defaults)
    if (grid.mixerView != SessionMixerView {})
    {
        const auto& m = grid.mixerView;
        juce::Array<juce::var> view { m.shown, m.io, m.sends, m.volume, m.trackOptions };
        root->setProperty ("mixerView", view);
    }
    juce::Array<juce::var> scenes;
    for (const auto& scene : grid.scenes)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        if (! scene.name.empty())
            o->setProperty ("name", str (scene.name));
        if (scene.colour != 0)
            o->setProperty ("colour", (juce::int64) scene.colour);
        if (scene.tempo > 0.0)
            o->setProperty ("tempo", scene.tempo);
        if (scene.beatsPerBar > 0)
            o->setProperty ("beatsPerBar", scene.beatsPerBar);
        if (scene.beatsPerBar > 0 && scene.beatUnit != 4) // (round 190em)
            o->setProperty ("beatUnit", scene.beatUnit);
        scenes.add (juce::var (o.release()));
    }
    root->setProperty ("scenes", scenes);
    juce::Array<juce::var> clips;
    for (const auto& clip : grid.clips)
    {
        if (clip.gridSlot < 0 || clip.gridSlot >= (int) grid.slots.size())
            continue;
        const auto& slot = grid.slots[(std::size_t) clip.gridSlot];
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("scene", slot.scene);
        if (! slot.name.empty())
            o->setProperty ("name", str (slot.name));
        if (slot.colour != 0)
            o->setProperty ("colour", (juce::int64) slot.colour);
        if (slot.mode != LaunchMode::trigger)
            o->setProperty ("mode", (int) slot.mode);
        if (slot.quant != LaunchQuant::global)
            o->setProperty ("quant", (int) slot.quant);
        if (slot.legato)
            o->setProperty ("legato", true);
        if (! slot.loop)
            o->setProperty ("loop", false);
        if (slot.noStop)
            o->setProperty ("noStop", true);
        if (slot.blankName) // (round 190em)
            o->setProperty ("blankName", true);
        if (slot.groove != 0)
            o->setProperty ("groove", (juce::int64) slot.groove);
        if (slot.follow != ClipFollow {})
        {
            const auto& f = slot.follow;
            juce::Array<juce::var> follow { f.enabled, (int) f.a, (int) f.b, f.chanceA, f.chanceB, f.jumpA, f.jumpB, f.linked, f.iterations, (juce::int64) f.ticks };
            o->setProperty ("follow", follow);
        }
        o->setProperty ("clip", clipsToVar ({ clip }));
        clips.add (juce::var (o.release()));
    }
    root->setProperty ("clips", clips);
    return juce::var (root.release());
}

std::optional<ClipGrid> clipGridFromVar (const juce::var& v, const Song& song)
{
    if (! v.isObject())
        return std::nullopt;
    ClipGrid grid;
    grid.globalQuant = (LaunchQuant) juce::jlimit (1, (int) LaunchQuant::thirtySecond, (int) v.getProperty ("quant", (int) LaunchQuant::bar));
    grid.backOnStop = (bool) v.getProperty ("backOnStop", false);
    grid.followActions = (bool) v.getProperty ("followActions", true);
    if (const auto* view = v["mixerView"].getArray(); view != nullptr && view->size() >= 5)
        grid.mixerView = { (bool) (*view)[0], (bool) (*view)[1], (bool) (*view)[2], (bool) (*view)[3], (bool) (*view)[4] };
    if (const auto* scenes = v["scenes"].getArray())
        for (const auto& o : *scenes)
        {
            ClipGridScene scene;
            scene.name = o["name"].toString().toStdString();
            scene.colour = (std::uint32_t) (juce::int64) o.getProperty ("colour", 0);
            scene.tempo = (double) o.getProperty ("tempo", 0.0);
            scene.beatsPerBar = (int) o.getProperty ("beatsPerBar", 0);
            scene.beatUnit = scene.beatsPerBar > 0 ? session::sceneBeatUnit ((int) o.getProperty ("beatUnit", 4)) : 4;
            grid.scenes.push_back (std::move (scene));
        }
    if (const auto* clips = v["clips"].getArray())
        for (const auto& o : *clips)
        {
            auto read = clipsFromVar (o["clip"], song);
            if (read.size() != 1 || (int) grid.slots.size() >= launcher::maxSlots)
                continue;
            ClipGridSlot slot;
            slot.scene = juce::jlimit (0, launcher::maxScenes - 1, (int) o["scene"]);
            slot.name = o["name"].toString().toStdString();
            slot.colour = (std::uint32_t) (juce::int64) o.getProperty ("colour", 0);
            slot.mode = (LaunchMode) juce::jlimit (0, (int) LaunchMode::repeat, (int) o.getProperty ("mode", 0));
            slot.quant = (LaunchQuant) juce::jlimit (0, (int) LaunchQuant::thirtySecond, (int) o.getProperty ("quant", 0));
            slot.legato = (bool) o.getProperty ("legato", false);
            slot.loop = (bool) o.getProperty ("loop", true);
            slot.noStop = (bool) o.getProperty ("noStop", false);
            slot.blankName = (bool) o.getProperty ("blankName", false);
            slot.groove = (std::uint32_t) (juce::int64) o.getProperty ("groove", 0);
            if (const auto f = o["follow"]; f.isArray() && f.size() >= 10)
            {
                const auto kind = [] (const juce::var& k) { return (FollowKind) juce::jlimit (0, (int) FollowKind::jump, (int) k); };
                slot.follow = { (bool) f[0], kind (f[1]), kind (f[2]), (int) f[3], (int) f[4], (int) f[5], (int) f[6], (bool) f[7], juce::jmax (1, (int) f[8]), juce::jmax<juce::int64> (1, (juce::int64) f[9]) };
            }
            auto& clip = read[0];
            clip.start = 0;
            clip.gridSlot = (std::int16_t) grid.slots.size();
            while ((int) grid.scenes.size() <= slot.scene)
                grid.scenes.emplace_back();
            grid.slots.push_back (std::move (slot));
            grid.clips.push_back (clip);
        }
    if (grid.scenes.empty())
        grid.scenes.emplace_back();
    return grid;
}

// The groove pool (round 190bv): only in a project that has one.
juce::var groovePoolToVar (const GroovePool& pool)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("amount", pool.amount);
    juce::Array<juce::var> grooves;
    for (const auto& g : pool.grooves)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("id", (juce::int64) g.id);
        o->setProperty ("name", str (g.name));
        o->setProperty ("base", (int) g.base);
        o->setProperty ("quantize", g.quantize);
        o->setProperty ("timing", g.timing);
        o->setProperty ("random", g.random);
        o->setProperty ("velocity", g.velocity);
        o->setProperty ("length", (juce::int64) g.length);
        juce::Array<juce::var> points; // tick, velocity, tick, velocity…
        for (const auto& p : g.points)
        {
            points.add ((juce::int64) p.tick);
            points.add (p.velocity);
        }
        o->setProperty ("points", points);
        grooves.add (juce::var (o.release()));
    }
    root->setProperty ("grooves", grooves);
    return juce::var (root.release());
}

GroovePool groovePoolFromVar (const juce::var& v)
{
    GroovePool pool;
    if (! v.isObject())
        return pool;
    pool.amount = juce::jlimit (0.0f, groove::maxAmount, (float) (double) v.getProperty ("amount", 1.0));
    if (const auto* grooves = v["grooves"].getArray())
        for (const auto& o : *grooves)
        {
            Groove g;
            g.id = (std::uint32_t) (juce::int64) o.getProperty ("id", 0);
            if (g.id == 0 || groove::find (pool, g.id) != nullptr)
                continue;
            g.name = o["name"].toString().toStdString();
            g.base = (GrooveBase) juce::jlimit (0, (int) GrooveBase::thirtySecond, (int) o.getProperty ("base", (int) GrooveBase::sixteenth));
            g.quantize = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("quantize", 0.0));
            g.timing = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("timing", 1.0));
            g.random = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("random", 0.0));
            g.velocity = juce::jlimit (-1.0f, 1.0f, (float) (double) o.getProperty ("velocity", 0.0));
            g.length = juce::jmax<juce::int64> (groove::baseTicks (g.base), (juce::int64) o.getProperty ("length", 4 * ppq));
            if (const auto* points = o["points"].getArray())
                for (int i = 0; i + 1 < points->size(); i += 2)
                    g.points.push_back ({ juce::jlimit<juce::int64> (0, g.length - 1, (juce::int64) (*points)[i]),
                                          juce::jlimit (0.0f, 1.0f, (float) (double) (*points)[i + 1]) });
            std::stable_sort (g.points.begin(), g.points.end(), [] (const GroovePoint& a, const GroovePoint& b) { return a.tick < b.tick; });
            pool.grooves.push_back (std::move (g));
        }
    return pool;
}

juce::var channelToVar (const ChannelDef& c)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("name", str (c.name));
    o->setProperty ("instrument", instrumentId (c.instrument));
    o->setProperty ("volume", c.volume);
    o->setProperty ("pan", c.pan);
    o->setProperty ("muted", c.muted);
    if (c.ghostHidden)
        o->setProperty ("ghostHidden", true);
    if (c.soloLock)
        o->setProperty ("soloLock", true);

    if (! c.samplePath.empty())
        o->setProperty ("sample", str (c.samplePath));

    if (! c.controllerState.empty())
        o->setProperty ("controllerState", juce::Base64::toBase64 (c.controllerState.data(), c.controllerState.size()));
    if (! c.pluginState.empty())
        o->setProperty ("pluginState", juce::Base64::toBase64 (c.pluginState.data(), c.pluginState.size()));
    if (! c.modelParams.empty())
    {
        juce::StringArray values;
        for (auto v : c.modelParams)
            values.add (juce::String (v, 7));
        o->setProperty ("modelParams", values.joinIntoString (" "));
    }

    if (! c.zones.empty()) // [file, root, min key, max key, min velocity, max velocity, tune, gain]
    {
        juce::Array<juce::var> zones;
        for (const auto& z : c.zones)
            zones.add (juce::Array<juce::var> { str (z.path), z.rootKey, z.lowKey, z.highKey, z.lowVelocity, z.highVelocity, z.tune, z.gain,
                                                (juce::int64) z.loopStart, (juce::int64) z.loopEnd, z.loopMode });
        o->setProperty ("zones", zones);
    }

    o->setProperty ("mixer", c.mixerTrack);
    if (c.maxPolyphony > 0)
        o->setProperty ("maxPolyphony", c.maxPolyphony);

    if (c.colour != 0)
        o->setProperty ("colour", juce::String::toHexString ((juce::int64) (c.colour & 0xFFFFFF)).paddedLeft ('0', 6));
    if (c.group >= 0)
        o->setProperty ("group", c.group);
    if (c.swingMix != 1.0f)
        o->setProperty ("swingMix", c.swingMix);
    if (! c.keyLabels.empty())
    {
        auto* labels = new juce::DynamicObject();
        for (const auto& [key, label] : c.keyLabels)
            labels->setProperty (juce::String (key), juce::String::fromUTF8 (label.c_str()));
        o->setProperty ("keyLabels", juce::var (labels));
    }
    if (c.arp.enabled())
    {
        o->setProperty ("arp", juce::Array<juce::var> { c.arp.direction, c.arp.range, c.arp.stepBeats, c.arp.gate, c.arp.repeat });
        // FL's chord / slide / repeat (REA round 146b)
        juce::Array<juce::var> chord;
        for (auto i : c.arp.chord)
            chord.add (i);
        auto fl = std::make_unique<juce::DynamicObject>();
        fl->setProperty ("chord", chord);
        fl->setProperty ("chordSustain", c.arp.chordSustain);
        fl->setProperty ("slide", c.arp.slide);
        fl->setProperty ("octaveRepeat", c.arp.octaveRepeat);
        o->setProperty ("arpFl", juce::var (fl.release()));
    }
    // FL's Misc tab (REA round 146b): only what differs from the defaults
    if (! (c.misc == ChannelMisc {}))
    {
        const auto& m = c.misc;
        auto misc = std::make_unique<juce::DynamicObject>();
        misc->setProperty ("shift", m.shiftBeats);
        misc->setProperty ("gate", m.gateBeats);
        misc->setProperty ("fullPorta", m.fullPorta);
        if (m.mono) // Polyphony › Mono / Porta / Slide (round 168l)
            misc->setProperty ("mono", true);
        if (m.porta)
            misc->setProperty ("porta", true);
        if (m.portaBeats != ChannelMisc {}.portaBeats)
            misc->setProperty ("portaTime", m.portaBeats);
        misc->setProperty ("echo", juce::Array<juce::var> { m.echo.feedback, m.echo.pan, m.echo.pitch, m.echo.modX, m.echo.modY, m.echo.echoes,
                                                            m.echo.timeBeats, m.echo.pingPong, m.echo.fat });
        misc->setProperty ("velocityTracking", juce::Array<juce::var> { m.velocity.mid, m.velocity.pan, m.velocity.modX, m.velocity.modY });
        misc->setProperty ("keyTracking", juce::Array<juce::var> { m.key.mid, m.key.pan, m.key.modX, m.key.modY });
        misc->setProperty ("pitchRange", m.pitchRange);
        misc->setProperty ("mainPitch", m.mainPitch);
        misc->setProperty ("rootNote", m.rootNote);
        misc->setProperty ("addToKey", m.addToKey);
        if (m.fineTune != 0.0f)
            misc->setProperty ("fineTune", m.fineTune);
        if (! m.levels.neutral()) // FL's Levels adjustment (round 190dz): pan, volume multiplier, Mod X, Mod Y
            misc->setProperty ("levels", juce::Array<juce::var> { m.levels.pan, m.levels.volume, m.levels.modX, m.levels.modY });
        o->setProperty ("misc", juce::var (misc.release()));
    }
    if (c.cutGroup != 0 || c.cutBy != 0)
        o->setProperty ("cut", juce::Array<juce::var> { c.cutGroup, c.cutBy });
    // FL's key region and Layer channels (round 164)
    if (c.hasKeyRegion())
        o->setProperty ("keyRegion", juce::Array<juce::var> { c.keyLow, c.keyHigh });
    if (! c.receiveFrom.empty()) // FL's "Receive notes from" (round 165f-rack)
        o->setProperty ("receiveFrom", juce::Array<juce::var> { str (c.receiveFrom), c.receiveMidiChannel });
    if (c.icon != 0) // FL's channel icon index (round 168j)
        o->setProperty ("icon", c.icon);
        if (c.flWrapperFlags >= 0)
            o->setProperty ("flWrapperFlags", c.flWrapperFlags);
    if (! c.flOrigin.empty()) // the FL generator it was imported from (round 171e)
        o->setProperty ("flOrigin", flOriginToVar (c.flOrigin));
    if (c.loopStarterPattern >= 0) // FL's Loop Starter (round 168b): [pattern, slot, category, locked]
        o->setProperty ("loopStarter", juce::Array<juce::var> { c.loopStarterPattern, str (c.loopStarterSlot), str (c.loopStarterCategory), c.loopStarterLocked });
    if (c.instrument == InstrumentKind::layer)
    {
        auto l = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> kids;
        for (const auto k : c.layer.children)
            kids.add (k);
        l->setProperty ("children", kids);
        l->setProperty ("random", c.layer.random);
        l->setProperty ("crossfade", c.layer.crossfade);
        l->setProperty ("sequential", c.layer.sequential);
        l->setProperty ("fade", c.layer.fade);
        o->setProperty ("layer", juce::var (l.release()));
    }

    if (! (c.synth == SynthSettings {}))
    {
        auto synth = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> oscs;

        for (const auto& osc : c.synth.osc)
            oscs.add (juce::Array<juce::var> { (int) osc.wave, osc.octave, osc.detune, osc.level, osc.semitones, osc.pan });

        synth->setProperty ("osc", oscs);
        if (std::any_of (c.synth.spectrum.begin(), c.synth.spectrum.end(), [] (float a) { return a != 0.0f; }))
        {
            juce::StringArray amplitudes, phases;
            for (int h = 0; h < SynthSettings::spectrumSize; ++h)
            {
                amplitudes.add (juce::String (c.synth.spectrum[(std::size_t) h], 6));
                phases.add (juce::String (c.synth.spectrumPhase[(std::size_t) h], 6));
            }
            synth->setProperty ("spectrum", amplitudes.joinIntoString (" "));
            synth->setProperty ("spectrumPhase", phases.joinIntoString (" "));
            if (std::any_of (c.synth.spectrumDecay.begin(), c.synth.spectrumDecay.end(), [] (float t) { return t != 0.0f; }))
            {
                juce::StringArray decays;
                for (auto t : c.synth.spectrumDecay)
                    decays.add (juce::String (t, 4));
                synth->setProperty ("spectrumDecay", decays.joinIntoString (" "));
                if (std::any_of (c.synth.spectrumTarget.begin(), c.synth.spectrumTarget.end(), [] (float a) { return a != 0.0f; }))
                {
                    juce::StringArray targets;
                    for (auto a : c.synth.spectrumTarget)
                        targets.add (juce::String (a, 6));
                    synth->setProperty ("spectrumTarget", targets.joinIntoString (" "));
                }
            }
        }
        synth->setProperty ("adsr", juce::Array<juce::var> { c.synth.attack, c.synth.decay, c.synth.sustain, c.synth.release });
        synth->setProperty ("filter", juce::Array<juce::var> { c.synth.cutoff, c.synth.resonance, c.synth.filterType });
        if (c.synth.ladder)
            synth->setProperty ("ladder", true);
        if (c.synth.chorus > 0)
            synth->setProperty ("chorus", c.synth.chorus);
        if (c.synth.stereoTremolo > 0.0f)
            synth->setProperty ("stereoTremolo", c.synth.stereoTremolo);
        if (c.synth.vibratoCents > 0.0f)
            synth->setProperty ("vibratoCents", c.synth.vibratoCents);
        if (c.synth.mono)
            synth->setProperty ("mono", true);
        if (c.synth.porta)
            synth->setProperty ("porta", true);
        if (c.synth.portaBeats != SynthSettings {}.portaBeats)
            synth->setProperty ("portaTime", c.synth.portaBeats);
        if (c.synth.am)
            synth->setProperty ("am", true);
        if (c.synth.ring)
            synth->setProperty ("ring", true);
        if (c.synth.keyTrack != 0.0f)
            synth->setProperty ("keyTrack", c.synth.keyTrack);
        if (c.synth.bandGain != std::array<float, 3> { 1.0f, 1.0f, 1.0f })
            synth->setProperty ("bands", juce::String (c.synth.bandGain[0]) + " " + juce::String (c.synth.bandGain[1]) + " " + juce::String (c.synth.bandGain[2]));
        if (c.synth.sweep > 0.0f)
        {
            synth->setProperty ("sweep", c.synth.sweep);
            synth->setProperty ("sweepTime", c.synth.sweepTime);
        }
        if (c.synth.fm > 0.0f)
            synth->setProperty ("fm", juce::Array<juce::var> { c.synth.fm, c.synth.fmDecay, c.synth.fmSustain });

        writeEnvelopes (*synth, c.synth.filterEnv, c.synth.pitchEnv);
        writeLfos (*synth, c.synth.lfo);
        o->setProperty ("synthSettings", juce::var (synth.release()));
    }

    if (! (c.sampler == SamplerSettings {}))
    {
        auto sampler = std::make_unique<juce::DynamicObject>();
        sampler->setProperty ("pitch", c.sampler.pitch);
        sampler->setProperty ("attack", c.sampler.attack);
        sampler->setProperty ("decay", c.sampler.decay);
        sampler->setProperty ("sustain", c.sampler.sustain);
        sampler->setProperty ("release", c.sampler.release);
        sampler->setProperty ("stretchBeats", c.sampler.stretchBeats);
        sampler->setProperty ("keepPitch", c.sampler.keepPitch);
        if (c.sampler.stretchMethod != StretchMethod::general)
            sampler->setProperty ("stretchMethod", (int) c.sampler.stretchMethod);
        if (c.sampler.flStretchMode != 0)
            sampler->setProperty ("flStretchMode", c.sampler.flStretchMode);
        if (c.sampler.flStretchPitch != 0)
            sampler->setProperty ("flStretchPitch", c.sampler.flStretchPitch);
        if (c.sampler.loopKeyRoot >= 0) // round 190ds: the audio's key (core/KeyFollow.h)
            sampler->setProperty ("loopKey", juce::Array<juce::var> { c.sampler.loopKeyRoot, c.sampler.loopKeyMinor ? 1 : 0, (int) c.sampler.loopKeySource });
        if (c.sampler.stretchPitch != 0.0f)
            sampler->setProperty ("stretchPitch", c.sampler.stretchPitch);
        if (! c.sampler.warp.empty()) // warp markers (round 175a, M1): [seconds, beats] pairs
        {
            juce::Array<juce::var> warp;
            for (const auto& w : c.sampler.warp)
                warp.add (juce::Array<juce::var> { w.seconds, w.beats });
            sampler->setProperty ("warp", warp);
        }
        sampler->setProperty ("region", juce::Array<juce::var> { c.sampler.regionStart, c.sampler.regionEnd });
        if (c.sampler.startOffset > 0.0f)
            sampler->setProperty ("startOffset", c.sampler.startOffset);

        if (! c.sampler.slices.empty())
        {
            juce::Array<juce::var> slices;
            for (auto v : c.sampler.slices)
                slices.add (juce::roundToInt (v * 100000.0f) / 100000.0);
            sampler->setProperty ("slices", slices);
        }
        sampler->setProperty ("reverse", c.sampler.reverse);
        sampler->setProperty ("cut", c.sampler.cut);
        if (c.sampler.porta)
            sampler->setProperty ("porta", true);
        if (c.sampler.portaBeats != SamplerSettings {}.portaBeats)
            sampler->setProperty ("portaTime", c.sampler.portaBeats);
        sampler->setProperty ("filter", juce::Array<juce::var> { c.sampler.cutoff, c.sampler.resonance, c.sampler.filterType });
        if (c.sampler.gainDb != 0.0f)
            sampler->setProperty ("gainDb", c.sampler.gainDb);
        // FL's precomputed effects, loop flag and declicking mode (round 167h)
        if (const auto& p = c.sampler.precompute; p.any())
        {
            auto pre = std::make_unique<juce::DynamicObject>();
            for (auto [name, on] : { std::pair { "removeDc", p.removeDc }, { "normalize", p.normalize }, { "reversePolarity", p.reversePolarity },
                                     { "fadeStereo", p.fadeStereo }, { "swapStereo", p.swapStereo }, { "pingPong", p.pingPong } })
                if (on)
                    pre->setProperty (name, true);
            for (auto [name, v] : { std::pair { "fadeIn", p.fadeIn }, { "fadeOut", p.fadeOut }, { "crossfade", p.crossfade }, { "trim", p.trim } })
                if (v != 0)
                    pre->setProperty (name, v);
            // the legacy effects (round 167l), only when not FL's default
            const SamplePrecompute none;
            for (auto [name, v, d] : { std::tuple { "boost", p.boost, none.boost }, { "eq", p.eq, none.eq }, { "ringMix", p.ringMix, none.ringMix },
                                       { "ringFreq", p.ringFreq, none.ringFreq }, { "stereoDelay", p.stereoDelay, none.stereoDelay },
                                       { "cutoff", p.cutoff, none.cutoff }, { "resonance", p.resonance, none.resonance },
                                       { "reverb", p.reverb, none.reverb }, { "reverbType", p.reverbType, none.reverbType }, { "pogo", p.pogo, none.pogo } })
                if (v != d)
                    pre->setProperty (name, v);
            if (p.clip)
                pre->setProperty ("clip", true);
            sampler->setProperty ("precomputed", juce::var (pre.release()));
        }
        if (! c.sampler.useLoopPoints)
            sampler->setProperty ("useLoopPoints", false);
        if (c.sampler.flVelocityLaw)
            sampler->setProperty ("flVelocityLaw", true);
        if (c.sampler.declickMode != 0)
            sampler->setProperty ("declickMode", c.sampler.declickMode);
        // round 190dh (FL's Sampler page): MUL, the loop knobs' points and the Content options, each only when set
        if (c.sampler.stretchMul != 0)
            sampler->setProperty ("stretchMul", c.sampler.stretchMul);
        if (c.sampler.hasLoopOverride())
        {
            sampler->setProperty ("loopStart", (juce::int64) c.sampler.loopStart);
            sampler->setProperty ("loopEnd", (juce::int64) c.sampler.loopEnd);
        }
        if (c.sampler.keepOnDisk)
            sampler->setProperty ("keepOnDisk", true);
        if (c.sampler.resampleOnLoad)
            sampler->setProperty ("resample", true);
        if (! c.sampler.loadRegions)
            sampler->setProperty ("loadRegions", false);
        if (c.sampler.loadSliceMarkers)
            sampler->setProperty ("loadSliceMarkers", true);
        if (! c.sampler.flVolumeEnvelope.empty())
        {
            juce::Array<juce::var> env;
            for (auto v : c.sampler.flVolumeEnvelope)
                env.add ((int) v);
            sampler->setProperty ("flVolumeEnvelope", env);
        }
        if (! c.sampler.flmPad.empty())
        {
            juce::Array<juce::var> pad;
            for (auto v : c.sampler.flmPad)
                pad.add ((double) v);
            sampler->setProperty ("flmPad", pad);
        }
        if (! c.sampler.flChannel.empty())
        {
            juce::Array<juce::var> fl;
            for (auto v : c.sampler.flChannel)
                fl.add ((int) v);
            sampler->setProperty ("flChannel", fl);
        }
        writeEnvelopes (*sampler, c.sampler.filterEnv, c.sampler.pitchEnv);
        writeLfos (*sampler, c.sampler.lfo);
        o->setProperty ("samplerSettings", juce::var (sampler.release()));
    }

    if (c.instrument == InstrumentKind::plugin)
        o->setProperty ("plugin", pluginToVar (c.plugin));
    if (c.instrument == InstrumentKind::patcher || c.instrument == InstrumentKind::dashboard) // (a Dashboard's controls: round 164)
        o->setProperty ("patcher", patcherToVar (c.patcher));
    if (c.uid != 0)
        o->setProperty ("uid", uidVar (c.uid));
    if (! c.standIns.empty())
        o->setProperty ("standIns", standInsToVar (c.standIns));
    // Freeze (round 190ay): written only while frozen, so every other project's file is what it was
    if (c.freeze.on())
    {
        auto f = std::make_unique<juce::DynamicObject>();
        f->setProperty ("stems", standInsToVar (c.freeze.stems));
        f->setProperty ("source", str (c.freeze.source));
        o->setProperty ("freeze", juce::var (f.release()));
    }

    return juce::var (o.release());
}

ChannelDef channelFromVar (const juce::var& c, const Song& song)
{
    ChannelDef def;
    def.name = c["name"].toString().toStdString();
    def.instrument = instrumentFromId (c["instrument"].toString());
    def.volume = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("volume", 0.8));
    def.pan = juce::jlimit (-1.0f, 1.0f, (float) c.getProperty ("pan", 0.0));
    def.muted = (bool) c.getProperty ("muted", false);
    def.ghostHidden = (bool) c.getProperty ("ghostHidden", false);
    def.soloLock = (bool) c.getProperty ("soloLock", false);
    def.samplePath = c["sample"].toString().toStdString();

    {
        const auto values = juce::StringArray::fromTokens (c.getProperty ("modelParams", "").toString(), " ", "");
        for (int k = 0; k < values.size() && k < 8192; ++k) // (DrumSynth Live: 4562)
            def.modelParams.push_back (values[k].getFloatValue());
    }
    if (const auto encoded = c["pluginState"].toString(); encoded.isNotEmpty())
    {
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64 (decoded, encoded))
        {
            const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
            def.pluginState.assign (bytes, bytes + decoded.getDataSize());
        }
    }
    if (const auto encoded = c["controllerState"].toString(); encoded.isNotEmpty())
    {
        juce::MemoryOutputStream decoded;
        if (juce::Base64::convertFromBase64 (decoded, encoded))
        {
            const auto* bytes = static_cast<const std::uint8_t*> (decoded.getData());
            def.controllerState.assign (bytes, bytes + decoded.getDataSize());
        }
    }

    if (auto* zones = c["zones"].getArray())
        for (const auto& z : *zones)
            if (z.isArray() && z.size() >= 8)
            {
                def.zones.push_back ({ z[0].toString().toStdString(), juce::jlimit (0, 127, (int) z[1]), juce::jlimit (0, 127, (int) z[2]),
                                       juce::jlimit (0, 127, (int) z[3]), juce::jlimit (0, 127, (int) z[4]), juce::jlimit (0, 127, (int) z[5]),
                                       juce::jlimit (-48.0f, 48.0f, (float) (double) z[6]), juce::jlimit (0.0f, 4.0f, (float) (double) z[7]) });

                if (z.size() >= 11) // loop
                {
                    def.zones.back().loopStart = juce::jmax<juce::int64> (0, (juce::int64) z[8]);
                    def.zones.back().loopEnd = juce::jmax<juce::int64> (0, (juce::int64) z[9]);
                    def.zones.back().loopMode = juce::jlimit (0, 2, (int) z[10]);
                }
            }
    def.mixerTrack = juce::jlimit (0, maxMixerTracks - 1, (int) c.getProperty ("mixer", 0));
    def.maxPolyphony = juce::jlimit (0, 128, (int) c.getProperty ("maxPolyphony", 0));
    def.plugin = pluginFromVar (c["plugin"]);

    if (c.hasProperty ("colour"))
        def.colour = 0xFF000000u | (std::uint32_t) c["colour"].toString().getHexValue32();
    if (const int group = c.getProperty ("group", -1); group >= 0 && group < (int) song.channelGroups.size())
        def.group = group;

    def.swingMix = juce::jlimit (0.0f, 1.0f, (float) (double) c.getProperty ("swingMix", 1.0));
    if (auto* labels = c.getProperty ("keyLabels", {}).getDynamicObject())
        for (const auto& p : labels->getProperties())
        {
            const auto key = p.name.toString().getIntValue();
            const auto label = p.value.toString().trim();
            if (key >= 0 && key < 128 && label.isNotEmpty())
                def.keyLabels[key] = label.toStdString();
        }

    if (const auto& arp = c["arp"]; arp.isArray() && arp.size() == 5)
    {
        def.arp.direction = juce::jlimit (0, 5, (int) arp[0]);
        def.arp.range = juce::jlimit (1, 4, (int) arp[1]);
        def.arp.stepBeats = juce::jlimit (1.0f / 192.0f, 4.0f, (float) (double) arp[2]);
        def.arp.gate = juce::jlimit (0.05f, 128.0f / 48.0f, (float) (double) arp[3]);
        def.arp.repeat = juce::jlimit (1, 16, (int) arp[4]);
        if (const auto& fl = c["arpFl"]; fl.isObject())
        {
            if (const auto* chord = fl["chord"].getArray())
                for (const auto& i : *chord)
                    def.arp.chord.push_back (juce::jlimit (-48, 48, (int) i));
            def.arp.chordSustain = (bool) fl.getProperty ("chordSustain", true);
            def.arp.slide = (bool) fl.getProperty ("slide", false);
            def.arp.octaveRepeat = juce::jlimit (0, 16, (int) fl.getProperty ("octaveRepeat", 1));
        }
    }
    def.icon = juce::jlimit (0, 0xFFFF, (int) c.getProperty ("icon", 0));
    def.flWrapperFlags = (int) c.getProperty ("flWrapperFlags", -1);
    if (c.hasProperty ("flOrigin"))
        def.flOrigin = flOriginFromVar (c["flOrigin"]);
    if (const auto& from = c["receiveFrom"]; from.isArray() && from.size() >= 2)
    {
        def.receiveFrom = from[0].toString().toStdString();
        def.receiveMidiChannel = juce::jlimit (0, 16, (int) from[1]);
    }
    if (const auto& ls = c["loopStarter"]; ls.isArray() && ls.size() >= 4 && (int) ls[0] >= 0)
    {
        def.loopStarterPattern = (int) ls[0];
        def.loopStarterSlot = ls[1].toString().toStdString();
        def.loopStarterCategory = ls[2].toString().toStdString();
        def.loopStarterLocked = (bool) ls[3];
    }
    if (const auto& region = c["keyRegion"]; region.isArray() && region.size() >= 2)
    {
        def.keyLow = juce::jlimit (0, 127, (int) region[0]);
        def.keyHigh = juce::jlimit (def.keyLow, 127, (int) region[1]);
    }
    if (const auto& l = c["layer"]; l.isObject())
    {
        if (const auto* kids = l["children"].getArray())
            for (const auto& k : *kids)
                if ((int) k >= 0)
                    def.layer.children.push_back ((int) k);
        def.layer.random = (bool) l.getProperty ("random", false);
        def.layer.crossfade = (bool) l.getProperty ("crossfade", false);
        def.layer.sequential = (bool) l.getProperty ("sequential", false);
        def.layer.fade = juce::jlimit (0.0f, 1.0f, (float) (double) l.getProperty ("fade", 0.0));
    }
    if (const auto& misc = c["misc"]; misc.isObject())
    {
        auto& m = def.misc;
        m.shiftBeats = juce::jlimit (0.0f, 1.0f, (float) (double) misc.getProperty ("shift", 0.0));
        m.gateBeats = juce::jlimit (0.0f, 4.0f, (float) (double) misc.getProperty ("gate", 0.0));
        m.fullPorta = (bool) misc.getProperty ("fullPorta", true);
        m.mono = (bool) misc.getProperty ("mono", false);
        m.porta = (bool) misc.getProperty ("porta", false);
        m.portaBeats = juce::jlimit (0.0f, 8.0f, (float) (double) misc.getProperty ("portaTime", (double) ChannelMisc {}.portaBeats));
        if (const auto& e = misc["echo"]; e.isArray() && e.size() >= 9)
        {
            m.echo.feedback = juce::jlimit (0.0f, 2.0f, (float) (double) e[0]);
            m.echo.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) e[1]);
            m.echo.pitch = juce::jlimit (-4800.0f, 4800.0f, (float) (double) e[2]);
            m.echo.modX = juce::jlimit (-1.0f, 1.0f, (float) (double) e[3]);
            m.echo.modY = juce::jlimit (-1.0f, 1.0f, (float) (double) e[4]);
            m.echo.echoes = juce::jlimit (0, 16, (int) e[5]);
            m.echo.timeBeats = juce::jlimit (1.0f / 192.0f, 16.0f, (float) (double) e[6]);
            m.echo.pingPong = (bool) e[7];
            m.echo.fat = (bool) e[8];
        }
        auto tracking = [] (const juce::var& t, NoteTracking& to) {
            if (! t.isArray() || t.size() < 4)
                return;
            to.mid = juce::jlimit (0, 131, (int) t[0]);
            to.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) t[1]);
            to.modX = juce::jlimit (-1.0f, 1.0f, (float) (double) t[2]);
            to.modY = juce::jlimit (-1.0f, 1.0f, (float) (double) t[3]);
        };
        tracking (misc["velocityTracking"], m.velocity);
        tracking (misc["keyTracking"], m.key);
        m.pitchRange = juce::jlimit (0, 48, (int) misc.getProperty ("pitchRange", 2));
        m.mainPitch = (bool) misc.getProperty ("mainPitch", true);
        m.rootNote = juce::jlimit (0, 127, (int) misc.getProperty ("rootNote", 60));
        m.addToKey = (bool) misc.getProperty ("addToKey", false);
        m.fineTune = juce::jlimit (-100.0f, 100.0f, (float) (double) misc.getProperty ("fineTune", 0.0));
        if (const auto& l = misc["levels"]; l.isArray() && l.size() >= 4)
        {
            m.levels.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) l[0]);
            m.levels.volume = juce::jlimit (0.0f, 2.0f, (float) (double) l[1]);
            m.levels.modX = juce::jlimit (-1.0f, 1.0f, (float) (double) l[2]);
            m.levels.modY = juce::jlimit (-1.0f, 1.0f, (float) (double) l[3]);
        }
    }

    if (const auto& cut = c["cut"]; cut.isArray() && cut.size() == 2)
    {
        def.cutGroup = (int) cut[0];
        def.cutBy = (int) cut[1];
    }

    if (const auto& s = c["synthSettings"]; s.isObject())
    {
        if (auto* oscs = s["osc"].getArray())
            for (int i = 0; i < oscs->size() && i < 3; ++i)
                if (const auto& o = (*oscs)[i]; o.isArray() && o.size() >= 4)
                {
                    auto& osc = def.synth.osc[(std::size_t) i];
                    osc = { (Waveform) juce::jlimit (0, 5, (int) o[0]), juce::jlimit (-3, 3, (int) o[1]),
                            juce::jlimit (-100.0f, 100.0f, (float) (double) o[2]), juce::jlimit (0.0f, 4.0f, (float) (double) o[3]) }; // > 1: Harmor with volume above 0 dB

                    if (o.size() >= 6) // semitones and pan (since the 3x Osc import)
                    {
                        osc.semitones = juce::jlimit (-11, 11, (int) o[4]);
                        osc.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) o[5]);
                    }
                }

        if (const auto& a = s["adsr"]; a.isArray() && a.size() == 4)
        {
            def.synth.attack = juce::jlimit (0.0f, 10.0f, (float) (double) a[0]);
            def.synth.decay = juce::jlimit (0.0f, 10.0f, (float) (double) a[1]);
            def.synth.sustain = juce::jlimit (0.0f, 1.0f, (float) (double) a[2]);
            def.synth.release = juce::jlimit (0.0f, 10.0f, (float) (double) a[3]);
        }

        if (const auto& f = s["filter"]; f.isArray() && f.size() >= 2)
        {
            def.synth.cutoff = juce::jlimit (20.0f, 20000.0f, (float) (double) f[0]);
            def.synth.resonance = juce::jlimit (0.0f, 1.0f, (float) (double) f[1]);
            def.synth.filterType = f.size() >= 3 ? juce::jlimit (0, 3, (int) f[2]) : 0;
            def.synth.ladder = (bool) s.getProperty ("ladder", false);
            def.synth.chorus = juce::jlimit (0, 3, (int) s.getProperty ("chorus", 0));
            def.synth.stereoTremolo = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperty ("stereoTremolo", 0.0));
            def.synth.vibratoCents = juce::jlimit (0.0f, 100.0f, (float) (double) s.getProperty ("vibratoCents", 0.0));
        }

        def.synth.mono = (bool) s.getProperty ("mono", false);
        def.synth.porta = (bool) s.getProperty ("porta", false);
        def.synth.portaBeats = juce::jlimit (0.0f, 8.0f, (float) (double) s.getProperty ("portaTime", (double) SynthSettings {}.portaBeats));
        def.synth.am = (bool) s.getProperty ("am", false);
        def.synth.ring = (bool) s.getProperty ("ring", false);
        for (const auto& [key, target] : { std::pair { "spectrum", &def.synth.spectrum }, std::pair { "spectrumPhase", &def.synth.spectrumPhase } })
        {
            const auto values = juce::StringArray::fromTokens (s.getProperty (key, "").toString(), " ", "");
            for (int h = 0; h < values.size() && h < SynthSettings::spectrumSize; ++h)
                (*target)[(std::size_t) h] = juce::jlimit (-16.0f, 16.0f, values[h].getFloatValue());
        }
        {
            const auto values = juce::StringArray::fromTokens (s.getProperty ("spectrumDecay", "").toString(), " ", "");
            for (int h = 0; h < values.size() && h < SynthSettings::spectrumSize; ++h)
                def.synth.spectrumDecay[(std::size_t) h] = juce::jlimit (0.0f, 60.0f, values[h].getFloatValue());
            const auto targets = juce::StringArray::fromTokens (s.getProperty ("spectrumTarget", "").toString(), " ", "");
            for (int h = 0; h < targets.size() && h < SynthSettings::spectrumSize; ++h)
                def.synth.spectrumTarget[(std::size_t) h] = juce::jlimit (0.0f, 16.0f, targets[h].getFloatValue());
        }
        def.synth.keyTrack = juce::jlimit (-2.0f, 2.0f, (float) (double) s.getProperty ("keyTrack", 0.0));
        if (const auto bands = juce::StringArray::fromTokens (s.getProperty ("bands", "").toString(), " ", ""); bands.size() == 3)
            for (int b = 0; b < 3; ++b)
                def.synth.bandGain[(std::size_t) b] = juce::jlimit (0.0f, 4.0f, bands[b].getFloatValue());
        def.synth.sweep = juce::jlimit (0.0f, 200.0f, (float) (double) s.getProperty ("sweep", 0.0));
        def.synth.sweepTime = juce::jlimit (0.0005f, 30.0f, (float) (double) s.getProperty ("sweepTime", 0.1));
        if (const auto* fm = s.getProperty ("fm", {}).getArray(); fm != nullptr && fm->size() >= 3)
        {
            def.synth.fm = juce::jlimit (0.0f, 16.0f, (float) (double) (*fm)[0]);
            def.synth.fmDecay = juce::jlimit (0.0f, 30.0f, (float) (double) (*fm)[1]);
            def.synth.fmSustain = juce::jlimit (0.0f, 1.0f, (float) (double) (*fm)[2]);
        }

        readEnvelopes (s, def.synth.filterEnv, def.synth.pitchEnv);
        readLfos (s, def.synth.lfo);
    }

    if (const auto& s = c["samplerSettings"]; s.isObject())
    {
        def.sampler.pitch = juce::jlimit (-24.0f, 24.0f, (float) (double) s.getProperty ("pitch", 0.0));
        def.sampler.attack = juce::jlimit (0.0f, 2.0f, (float) (double) s.getProperty ("attack", 0.0));
        def.sampler.release = juce::jlimit (0.0f, 4.0f, (float) (double) s.getProperty ("release", 0.0));
        def.sampler.decay = juce::jlimit (0.0f, 10.0f, (float) (double) s.getProperty ("decay", 0.0));
        def.sampler.sustain = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperty ("sustain", 1.0));
        def.sampler.stretchBeats = juce::jmax (0.0f, (float) (double) s.getProperty ("stretchBeats", 0.0));
        def.sampler.keepPitch = (bool) s.getProperty ("keepPitch", false);
        def.sampler.stretchMethod = (StretchMethod) juce::jlimit (0, stretchMethodCount - 1, (int) s.getProperty ("stretchMethod", 0));
        def.sampler.flStretchMode = (int) s.getProperty ("flStretchMode", 0);
        def.sampler.flStretchPitch = (int) s.getProperty ("flStretchPitch", 0);
        if (const auto& k = s.getProperty ("loopKey", {}); k.isArray() && k.size() >= 3)
        {
            def.sampler.loopKeyRoot = juce::jlimit (0, 11, (int) k[0]);
            def.sampler.loopKeyMinor = (int) k[1] != 0;
            def.sampler.loopKeySource = (std::uint8_t) juce::jlimit (0, 3, (int) k[2]);
        }
        def.sampler.stretchPitch = juce::jlimit (-48.0f, 48.0f, (float) (double) s.getProperty ("stretchPitch", 0.0));
        const auto warpVar = s.getProperty ("warp", {});
        if (const auto* warp = warpVar.getArray())
            for (const auto& w : *warp)
                if (const auto* pair = w.getArray(); pair != nullptr && pair->size() >= 2)
                    def.sampler.warp.push_back ({ (double) (*pair)[0], (double) (*pair)[1] });

        if (auto* slices = s["slices"].getArray())
            for (const auto& v : *slices)
                def.sampler.slices.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) v));

        if (const auto& region = s["region"]; region.isArray() && region.size() == 2)
        {
            def.sampler.regionStart = juce::jlimit (0.0f, 1.0f, (float) (double) region[0]);
            def.sampler.regionEnd = juce::jlimit (0.0f, 1.0f, (float) (double) region[1]);
        }
        def.sampler.startOffset = juce::jlimit (0.0f, 1.0f, (float) (double) s.getProperty ("startOffset", 0.0));
        def.sampler.reverse = (bool) s.getProperty ("reverse", false);
        def.sampler.cut = (bool) s.getProperty ("cut", false);
        def.sampler.porta = (bool) s.getProperty ("porta", false);
        def.sampler.portaBeats = juce::jlimit (0.0f, 8.0f, (float) (double) s.getProperty ("portaTime", (double) SamplerSettings {}.portaBeats));
        def.sampler.gainDb = juce::jlimit (-24.0f, 24.0f, (float) (double) s.getProperty ("gainDb", 0.0));
        if (const auto& pre = s["precomputed"]; pre.isObject())
        {
            auto& p = def.sampler.precompute;
            p.removeDc = (bool) pre.getProperty ("removeDc", false);
            p.normalize = (bool) pre.getProperty ("normalize", false);
            p.reversePolarity = (bool) pre.getProperty ("reversePolarity", false);
            p.fadeStereo = (bool) pre.getProperty ("fadeStereo", false);
            p.swapStereo = (bool) pre.getProperty ("swapStereo", false);
            p.pingPong = (bool) pre.getProperty ("pingPong", false);
            p.fadeIn = juce::jlimit (0, 1024, (int) pre.getProperty ("fadeIn", 0));
            p.fadeOut = juce::jlimit (0, 1024, (int) pre.getProperty ("fadeOut", 0));
            p.crossfade = juce::jlimit (0, 2048, (int) pre.getProperty ("crossfade", 0));
            p.trim = juce::jlimit (0, 32768, (int) pre.getProperty ("trim", 0));
            p.boost = juce::jlimit (0, 256, (int) pre.getProperty ("boost", 0));
            p.eq = juce::jlimit (0, 256, (int) pre.getProperty ("eq", 128));
            p.ringMix = juce::jlimit (0, 256, (int) pre.getProperty ("ringMix", 0));
            p.ringFreq = juce::jlimit (0, 256, (int) pre.getProperty ("ringFreq", 128));
            p.stereoDelay = juce::jlimit (0, 4096, (int) pre.getProperty ("stereoDelay", 2048));
            p.cutoff = juce::jlimit (16, 1024, (int) pre.getProperty ("cutoff", 1024));
            p.resonance = juce::jlimit (0, 640, (int) pre.getProperty ("resonance", 0));
            p.reverb = juce::jlimit (0, 256, (int) pre.getProperty ("reverb", 0));
            p.reverbType = (int) pre.getProperty ("reverbType", 1) == 0 ? 0 : 1;
            p.pogo = juce::jlimit (0, 512, (int) pre.getProperty ("pogo", 256));
            p.clip = (bool) pre.getProperty ("clip", false);
        }
        def.sampler.useLoopPoints = (bool) s.getProperty ("useLoopPoints", true);
        def.sampler.flVelocityLaw = (bool) s.getProperty ("flVelocityLaw", false);
        def.sampler.declickMode = juce::jlimit (0, 5, (int) s.getProperty ("declickMode", 0));
        def.sampler.stretchMul = juce::jlimit (-20000, 20000, (int) s.getProperty ("stretchMul", 0));
        if (s.hasProperty ("loopStart") && s.hasProperty ("loopEnd"))
        {
            const auto a = (juce::int64) s.getProperty ("loopStart", -1), b = (juce::int64) s.getProperty ("loopEnd", -1);
            if (a >= 0 && b >= a)
                def.sampler.loopStart = a, def.sampler.loopEnd = b;
        }
        def.sampler.keepOnDisk = (bool) s.getProperty ("keepOnDisk", false);
        def.sampler.resampleOnLoad = (bool) s.getProperty ("resample", false);
        def.sampler.loadRegions = (bool) s.getProperty ("loadRegions", true);
        def.sampler.loadSliceMarkers = (bool) s.getProperty ("loadSliceMarkers", false);

        if (auto* env = s["flVolumeEnvelope"].getArray(); env != nullptr && env->size() == 18)
            for (const auto& v : *env)
                def.sampler.flVolumeEnvelope.push_back ((std::int32_t) (int) v);

        if (auto* pad = s["flmPad"].getArray(); pad != nullptr && pad->size() == 7)
            for (const auto& v : *pad)
                def.sampler.flmPad.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) v));

        if (auto* fl = s["flChannel"].getArray(); fl != nullptr && (fl->size() == 89 || fl->size() == 90))
            for (const auto& v : *fl)
                def.sampler.flChannel.push_back ((std::int32_t) (int) v);

        if (const auto& filter = s["filter"]; filter.isArray() && filter.size() >= 2)
        {
            def.sampler.cutoff = juce::jlimit (20.0f, 20000.0f, (float) (double) filter[0]);
            def.sampler.resonance = juce::jlimit (0.0f, 1.0f, (float) (double) filter[1]);
            def.sampler.filterType = filter.size() >= 3 ? juce::jlimit (0, 3, (int) filter[2]) : 0;
        }

        readEnvelopes (s, def.sampler.filterEnv, def.sampler.pitchEnv);
        readLfos (s, def.sampler.lfo);
    }

    if ((def.instrument == InstrumentKind::patcher || def.instrument == InstrumentKind::dashboard) && c.hasProperty ("patcher"))
        def.patcher = patcherFromVar (c["patcher"], song);
    def.uid = uidFrom (c.getProperty ("uid", {}));
    def.standIns = standInsFromVar (c["standIns"]);
    if (const auto& f = c["freeze"]; f.isObject())
    {
        def.freeze.stems = standInsFromVar (f["stems"]);
        def.freeze.source = f["source"].toString().toStdString();
    }
    // projects saved before round 168l: an FL engine's (or another generator's) Mono / Porta, imported from FL, was
    // only in the LYNX Synth's settings (the Mono box came from FL's Mono flag or a Max of 1; a Max of 1 is not Mono)
    if (const auto& misc = c["misc"]; def.instrument != InstrumentKind::synth && ! misc.hasProperty ("mono") && ! misc.hasProperty ("porta"))
    {
        def.misc.mono = def.synth.mono && def.maxPolyphony != 1;
        def.misc.porta = def.instrument == InstrumentKind::sampler ? def.sampler.porta : def.synth.porta;
        def.misc.portaBeats = def.instrument == InstrumentKind::sampler ? def.sampler.portaBeats : def.synth.portaBeats;
    }
    return def;
}

} // namespace

juce::var mixerTrackToVar (const MixerTrack& t)
{
    auto o = std::make_unique<juce::DynamicObject>();
    o->setProperty ("name", str (t.name));
    o->setProperty ("volume", t.volume);
    o->setProperty ("pan", t.pan);
    o->setProperty ("muted", t.muted);
    o->setProperty ("solo", t.solo);
    if (! t.soloSources)
        o->setProperty ("soloSources", false);
    if (! t.threaded)
        o->setProperty ("threaded", false);
    if (t.separator)
        o->setProperty ("separator", true);
    if (t.folded)
        o->setProperty ("folded", true);
    if (t.dock != MixerDock::middle)
        o->setProperty ("dock", t.dock == MixerDock::left ? "left" : "right");

    if (! t.toMaster)
        o->setProperty ("toMaster", false);
    if (t.masterLevel != 1.0f)
        o->setProperty ("masterLevel", juce::roundToInt (t.masterLevel * 10000.0f) / 10000.0);
    if (t.colour != 0)
        o->setProperty ("colour", juce::String::toHexString ((juce::int64) (t.colour & 0xFFFFFF)).paddedLeft ('0', 6));
    if (t.stereo != 0.0f)
        o->setProperty ("stereo", juce::roundToInt (t.stereo * 1000.0f) / 1000.0);
    if (t.invertPhase)
        o->setProperty ("invertPhase", true);
    if (t.swapChannels)
        o->setProperty ("swap", true);
    if (t.inputChannel >= 0) // external audio input: device channel (0-based), stereo pair or mono
    {
        o->setProperty ("input", t.inputChannel);
        if (! t.inputStereo)
            o->setProperty ("inputMono", true);
    }
    if (monitorKeyWritten (t.monitor)) // (no key = "Auto": LYNX's default before round 190ev)
        o->setProperty ("monitor", (int) t.monitor);
    if (t.recordSafe)
        o->setProperty ("recordSafe", true);
    if (! t.recordTo.empty())
        o->setProperty ("recordTo", juce::String::fromUTF8 (t.recordTo.c_str()));
    if (t.recordSource != RecordSource::postEq) // round 168x: FL's "Record" position (Post EQ, FL's default, not written)
        o->setProperty ("recordSource", (int) t.recordSource);
    if (t.recordDryToo)
        o->setProperty ("recordDryToo", true);
    if (t.latencyMs != 0.0f) // FL's manual track latency, ms (REA round 165f-mixer)
        o->setProperty ("latencyMs", juce::roundToInt (t.latencyMs * 1000.0f) / 1000.0);
    if (t.latencyBypass != LatencyBypass::never)
        o->setProperty ("latencyBypass", t.latencyBypass == LatencyBypass::always ? "always" : "whenIdle");
    if (! t.compensateMasterLatency)
        o->setProperty ("compensateMaster", false);
    if (t.icon != 0) // FL's mixer track icon index (round 168j)
        o->setProperty ("icon", t.icon);
    if (t.outputChannel != outputDefault) // hardware output target (REA round 156): -1 = none, else device channel
    {
        o->setProperty ("output", t.outputChannel);
        if (t.outputMode != OutputMode::stereo)
            o->setProperty ("outputMode", t.outputMode == OutputMode::monoSum ? "mono" : t.outputMode == OutputMode::monoLeft ? "left" : "right");
    }
    if (! (t.eq == TrackEq {})) // [gains dB, frequencies Hz, widths 0..1] of the 3 bands
        o->setProperty ("eq", juce::Array<juce::var> { t.eq.gainDb[0], t.eq.gainDb[1], t.eq.gainDb[2], t.eq.hz[0], t.eq.hz[1], t.eq.hz[2],
                                                       t.eq.width[0], t.eq.width[1], t.eq.width[2] });
    if (t.eq.bypassed)
        o->setProperty ("eqBypassed", true);

    if (! t.sends.empty())
    {
        juce::Array<juce::var> sends;

        for (const auto& send : t.sends)
            sends.add (juce::Array<juce::var> { send.target, juce::roundToInt (send.level * 10000.0f) / 10000.0 });

        o->setProperty ("sends", sends);
        // LYNX's pre-fader sends (round 190aq): their targets, written only when one is used. A version from before
        // ignores the list and plays those sends post-fader (its loader takes "sends" entries of two values only).
        juce::Array<juce::var> pre;
        for (const auto& send : t.sends)
            if (send.pre)
                pre.add (send.target);
        if (! pre.isEmpty())
            o->setProperty ("preSends", pre);
    }
    if (! t.lostSends.empty()) // round 186c: [name, level] of each send to a deleted insert
    {
        juce::Array<juce::var> lost;
        for (const auto& l : t.lostSends)
            lost.add (juce::Array<juce::var> { juce::String::fromUTF8 (l.to.c_str()), juce::roundToInt (l.level * 10000.0f) / 10000.0 });
        o->setProperty ("lostSends", lost);
    }

    juce::Array<juce::var> effects;

    for (const auto& slot : t.effects)
        effects.add (effectToVar (slot));

    if (! effects.isEmpty())
        o->setProperty ("effects", effects);
    if (t.chainBlend < 1.0f) // the Simple workflow's Blend (round 190bu)
        o->setProperty ("chainBlend", juce::roundToInt (t.chainBlend * 10000.0f) / 10000.0);
    if (! t.chainPreset.empty())
        o->setProperty ("chainPreset", str (t.chainPreset));
    if (! t.standIns.empty())
        o->setProperty ("standIns", standInsToVar (t.standIns));

    return juce::var (o.release());
}

MixerTrack mixerTrackFromVar (const juce::var& t)
{
    MixerTrack track;
    track.name = t["name"].toString().toStdString();
    track.volume = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("volume", unityFader));
    track.pan = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("pan", 0.0));
    track.muted = (bool) t.getProperty ("muted", false);
    track.solo = (bool) t.getProperty ("solo", false);
    track.soloSources = (bool) t.getProperty ("soloSources", true);
    track.threaded = (bool) t.getProperty ("threaded", true);
    track.separator = (bool) t.getProperty ("separator", false);
    track.folded = (bool) t.getProperty ("folded", false);
    track.dock = t["dock"].toString() == "left" ? MixerDock::left : t["dock"].toString() == "right" ? MixerDock::right : MixerDock::middle;
    track.toMaster = (bool) t.getProperty ("toMaster", true);
    track.masterLevel = juce::jlimit (0.0f, maxFaderGain, (float) (double) t.getProperty ("masterLevel", 1.0));
    track.stereo = juce::jlimit (-1.0f, 1.0f, (float) (double) t.getProperty ("stereo", 0.0));

    if (t.hasProperty ("colour"))
        track.colour = 0xFF000000u | (std::uint32_t) t["colour"].toString().getHexValue32();
    track.invertPhase = (bool) t.getProperty ("invertPhase", false);
    track.swapChannels = (bool) t.getProperty ("swap", false);
    track.inputChannel = juce::jlimit (-1, 255, (int) t.getProperty ("input", -1));
    track.inputStereo = ! (bool) t.getProperty ("inputMono", false);
    track.monitor = monitorFromKey ((int) t.getProperty ("monitor", 0), t.hasProperty ("monitor"));
    track.recordSafe = (bool) t.getProperty ("recordSafe", false);
    track.recordTo = t.getProperty ("recordTo", "").toString().toStdString();
    track.recordSource = (RecordSource) juce::jlimit (0, 5, (int) t.getProperty ("recordSource", (int) RecordSource::postEq));
    track.recordDryToo = (bool) t.getProperty ("recordDryToo", false);
    track.latencyMs = juce::jlimit (-maxTrackLatencyMs, maxTrackLatencyMs, (float) (double) t.getProperty ("latencyMs", 0.0));
    if (const auto bypass = t["latencyBypass"].toString(); bypass == "always")
        track.latencyBypass = LatencyBypass::always;
    else if (bypass == "whenIdle")
        track.latencyBypass = LatencyBypass::whenRecordingOrStopped;
    track.compensateMasterLatency = (bool) t.getProperty ("compensateMaster", true);
    track.icon = juce::jlimit (0, 0xFFFF, (int) t.getProperty ("icon", 0));
    track.chainBlend = juce::jlimit (0.0f, 1.0f, (float) (double) t.getProperty ("chainBlend", 1.0));
    track.chainPreset = t.getProperty ("chainPreset", "").toString().toStdString();
    track.outputChannel = juce::jlimit (outputDefault, 255, (int) t.getProperty ("output", outputDefault));
    if (const auto mode = t["outputMode"].toString(); mode == "mono")
        track.outputMode = OutputMode::monoSum;
    else if (mode == "left")
        track.outputMode = OutputMode::monoLeft;
    else if (mode == "right")
        track.outputMode = OutputMode::monoRight;

    if (const auto& eq = t["eq"]; eq.isArray() && eq.size() == 9)
        for (std::size_t b = 0; b < 3; ++b)
        {
            track.eq.gainDb[b] = juce::jlimit (-18.0f, 18.0f, (float) (double) eq[(int) b]);
            track.eq.hz[b] = juce::jlimit (10.0f, 16000.0f, (float) (double) eq[(int) b + 3]);
            track.eq.width[b] = juce::jlimit (0.0f, 1.0f, (float) (double) eq[(int) b + 6]);
        }
    track.eq.bypassed = (bool) t.getProperty ("eqBypassed", false);

    if (auto* sends = t["sends"].getArray())
        for (const auto& send : *sends)
            if (send.isArray() && send.size() == 2)
                track.sends.push_back ({ (int) send[0], juce::jlimit (0.0f, maxFaderGain, (float) (double) send[1]) });
    if (auto* pre = t["preSends"].getArray())
        for (const auto& target : *pre)
            for (auto& send : track.sends)
                if (send.target == (int) target)
                    send.pre = true;
    if (auto* lost = t["lostSends"].getArray())
        for (const auto& l : *lost)
            if (l.isArray() && l.size() == 2)
                track.lostSends.push_back ({ l[0].toString().toStdString(), juce::jlimit (0.0f, maxFaderGain, (float) (double) l[1]) });

    if (auto* effects = t["effects"].getArray())
        for (const auto& e : *effects)
        {
            if ((int) track.effects.size() >= maxEffectSlots)
                continue;
            if (auto slot = effectFromVar (e))
                track.effects.push_back (std::move (*slot));
        }
    track.standIns = standInsFromVar (t["standIns"]);

    return track;
}

juce::String toJson (const Song& song, bool stampVersion)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "lynx-project");
    root->setProperty ("version", currentVersion);
    if (stampVersion)
        root->setProperty ("savedBy", JUCE_APPLICATION_VERSION_STRING); // the build that wrote it (round 177d, pain point 37)
    root->setProperty ("bpm", song.bpm);
    root->setProperty ("beatsPerBar", song.beatsPerBar);
    if (song.beatUnit != 4) // (round 190fg: the meter's denominator; older files read n/4)
        root->setProperty ("beatUnit", meterUnit (song.beatUnit));

    if (song.swing > 0.0f)
        root->setProperty ("swing", juce::roundToInt (song.swing * 1000.0f) / 1000.0);
    if (song.chanceSeed != 0)
        root->setProperty ("chanceSeed", (juce::int64) song.chanceSeed);
    if (! song.channelGroups.empty())
    {
        juce::Array<juce::var> groups;
        for (const auto& name : song.channelGroups)
            groups.add (str (name));
        root->setProperty ("channelGroups", groups);
    }
    if (song.masterPitch != 0.0f)
        root->setProperty ("masterPitch", (double) song.masterPitch);
    if (song.masterLook.id != 0) // the Finish dock's Master look (round 189k)
    {
        const auto& l = song.masterLook;
        root->setProperty ("masterLook", juce::String (l.id) + " " + juce::String (l.intensity, 4) + " " + juce::String (l.tone[0], 2) + " "
                                             + juce::String (l.tone[1], 2) + " " + juce::String (l.tone[2], 2) + " " + juce::String (l.inputDb, 2));
    }
    if (song.substitutesBypassed)
        root->setProperty ("substitutesBypassed", true);
    if (song.cleanMixer)
        root->setProperty ("cleanMixer", true);
    if (song.performanceMode)
        root->setProperty ("performanceMode", true);
    if (song.autoCrossfades)
        root->setProperty ("autoCrossfades", true);
    if (song.compCrossfadeMs != Song::defaultCompCrossfadeMs)
        root->setProperty ("compCrossfadeMs", (double) song.compCrossfadeMs);
    if (song.playTruncatedNotes)
        root->setProperty ("playTruncatedNotes", true);
    if (song.muteRunsEffects)
        root->setProperty ("muteRunsEffects", true);
    if (song.patternClipTakeover)
        root->setProperty ("patternClipTakeover", true);
    if (song.mutedInsertsSkip)
        root->setProperty ("mutedInsertsSkip", true);
    if (song.fastDeclick)
        root->setProperty ("fastDeclick", true);
    if (song.panTriangular)
        root->setProperty ("panLaw", "triangular");
    if (song.flPluginLevels) // (round 190ad: absent = LYNX's own law, as every project saved before)
        root->setProperty ("pluginLevels", "fl");
    // FL's mixer › Plugin delay compensation, Time settings, Piano Roll scale highlighting, data folder (round 168j)
    if (! song.autoPdc)
        root->setProperty ("autoPdc", false);
    if (! song.compensateAutomations)
        root->setProperty ("compensateAutomations", false);
    if (song.timeSignatureMode)
        root->setProperty ("timeSettings", "signature");
    if (song.scaleHighlight != Song::ScaleHighlight {})
        root->setProperty ("scaleHighlight", juce::Array<juce::var> { song.scaleHighlight.scale, song.scaleHighlight.root });
    if (! song.dataFolder.empty())
        root->setProperty ("dataFolder", juce::String::fromUTF8 (song.dataFolder.c_str()));
    if (! song.keyboardChords.empty()) // round 190ds: the Chord pad's chord set, [root, quality, octave, [extensions]]
    {
        juce::Array<juce::var> chords;
        for (const auto& k : song.keyboardChords)
        {
            juce::Array<juce::var> ext;
            for (auto e : k.extensions)
                ext.add (e);
            chords.add (juce::Array<juce::var> { k.root, k.quality, k.octave, ext });
        }
        root->setProperty ("keyboardChords", chords);
    }
    if (! song.info.empty())
    {
        auto info = std::make_unique<juce::DynamicObject>();
        auto set = [&info] (const char* key, const std::string& value) {
            if (! value.empty())
                info->setProperty (key, juce::String::fromUTF8 (value.c_str()));
        };
        set ("title", song.info.title);
        set ("author", song.info.author);
        set ("genre", song.info.genre);
        set ("url", song.info.url);
        set ("comments", song.info.comments);
        if (song.info.showOnOpen)
            info->setProperty ("showOnOpen", true);
        if (song.info.created > 0.0)
            info->setProperty ("created", song.info.created);
        if (song.info.workSeconds > 0.0)
            info->setProperty ("workSeconds", song.info.workSeconds);
        root->setProperty ("info", juce::var (info.release()));
    }
    root->setProperty ("currentPattern", song.currentPattern);

    juce::Array<juce::var> channels;

    for (const auto& c : song.channels)
        channels.add (channelToVar (c));

    root->setProperty ("channels", channels);

    juce::Array<juce::var> patterns;

    for (const auto& p : song.patterns)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (p.name));
        if (p.uid != 0)
            o->setProperty ("uid", uidVar (p.uid));

        if (p.lengthTicks > 0)
            o->setProperty ("length", (juce::int64) p.lengthTicks);

        if (p.colour != 0)
            o->setProperty ("colour", juce::String::toHexString ((juce::int64) (p.colour & 0xFFFFFF)).paddedLeft ('0', 6));

        // the Channel Rack's looping (round 165f-rack): on, type, and the loop points [channel, setting]
        if (p.loopOn)
            o->setProperty ("loopOn", true);
        if (! p.loopStarterGenre.empty()) // FL's Loop Starter genre (round 168b)
            o->setProperty ("loopStarterGenre", str (p.loopStarterGenre));
        if (p.loopMode != RackLoopMode::steps)
            o->setProperty ("loopMode", (int) p.loopMode);
        if (! p.channelLoops.empty())
        {
            juce::Array<juce::var> loops;
            for (const auto& l : p.channelLoops)
                loops.add (juce::Array<juce::var> { l.channel, l.setting });
            o->setProperty ("channelLoops", loops);
        }

        // the Piano roll's pattern markers (round 168g): [tick, name, numerator, denominator(, action)] like the Playlist's
        if (! p.markers.empty())
        {
            juce::Array<juce::var> markers;
            for (const auto& m : p.markers)
            {
                juce::Array<juce::var> one { (juce::int64) m.tick, str (m.name), m.numerator, m.denominator };
                if (m.action != 0)
                    one.add (m.action);
                markers.add (one);
            }
            o->setProperty ("markers", markers);
        }

        juce::Array<juce::var> notes;

        for (const auto& n : p.notes)
        {
            juce::Array<juce::var> note { (juce::int64) n.start, (juce::int64) n.length, n.channel, n.key,
                                          juce::roundToInt (n.velocity * 1000.0f) / 1000.0 };

            // [.., pan, fine pitch, muted] only when not default
            // [.., release, mod X, mod Y (, colour)]
            // [.., colour, group, chance, velocity range, chance group, play one] (round 187c)
            const auto chanced = n.hasChance();
            const auto extra = n.release != 0.5f || n.modX != 0.5f || n.modY != 0.5f || n.colour != 0 || n.group != 0 || chanced;
            if (n.pan != 0.0f || n.finePitch != 0.0f || n.muted || n.slide || n.porta || extra)
            {
                note.add (juce::roundToInt (n.pan * 1000.0f) / 1000.0);
                note.add (juce::roundToInt (n.finePitch * 10.0f) / 10.0);
                note.add (n.muted);
                note.add (n.slide);
                if (n.porta || extra)
                    note.add (n.porta);
                if (extra)
                    for (auto v : { n.release, n.modX, n.modY })
                        note.add (juce::roundToInt (v * 1000.0f) / 1000.0);
                if (n.colour != 0 || n.group != 0 || chanced)
                    note.add (n.colour);
                if (n.group != 0 || chanced)
                    note.add (n.group); // [.., colour, group]
                if (chanced)
                {
                    note.add (juce::roundToInt (n.chance * 10000.0f) / 10000.0);
                    note.add (juce::roundToInt (n.velocityRange * 10000.0f) / 10000.0);
                    note.add (n.chanceGroup);
                    note.add (n.chanceOne);
                }
            }

            notes.add (note);
        }

        o->setProperty ("notes", notes);

        // event editor lanes: [kind, index, slot, param] + [[tick, value], ...]
        juce::Array<juce::var> lanes;
        for (const auto& lane : p.events)
        {
            if (lane.events.empty() || lane.target.kind == AutomationTargetKind::none)
                continue;

            auto l = std::make_unique<juce::DynamicObject>();
            l->setProperty ("target", juce::Array<juce::var> { targetKindName (lane.target.kind), lane.target.index, lane.target.slot, lane.target.param });
            juce::Array<juce::var> events;
            for (const auto& e : lane.events)
                events.add (juce::Array<juce::var> { (juce::int64) e.tick, juce::roundToInt (e.value * 10000.0f) / 10000.0 });
            l->setProperty ("events", events);
            lanes.add (juce::var (l.release()));
        }
        if (! lanes.isEmpty())
            o->setProperty ("events", lanes);

        patterns.add (juce::var (o.release()));
    }

    root->setProperty ("patterns", patterns);

    juce::Array<juce::var> automations;

    for (const auto& a : song.automations)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("name", str (a.name));
        if (a.uid != 0)
            o->setProperty ("uid", uidVar (a.uid));
        o->setProperty ("target", juce::Array<juce::var> { targetKindName (a.target.kind), a.target.index, a.target.slot, a.target.param });
        juce::Array<juce::var> points;

        // [tick, value, tension] is a straight segment or an older project's bend (LYNX's own law); a fourth element is
        // FL's curve mode of the segment (round 190bw), with the tension then kept whole for the .flp round trip
        for (const auto& pt : a.points)
        {
            juce::Array<juce::var> p { (juce::int64) pt.tick, juce::roundToInt (pt.value * 10000.0f) / 10000.0 };
            if (pt.mode == AutomationPoint::legacyCurve || (pt.mode == 0 && pt.tension == 0.0f))
                p.add (juce::roundToInt (pt.tension * 1000.0f) / 1000.0);
            else
            {
                p.add ((double) pt.tension);
                p.add ((int) pt.mode);
            }
            points.add (p);
        }

        o->setProperty ("points", points);
        if (a.isLive())
        {
            o->setProperty ("source", juce::Array<juce::var> { a.source.track, a.source.slot, a.source.output, a.source.channel });
            juce::Array<juce::var> curve;
            for (auto v : a.curve)
                curve.add (juce::roundToInt (v * 10000.0f) / 10000.0);
            o->setProperty ("curve", curve);
            if (a.recordLive) // "Record automation" (round 190cs)
                o->setProperty ("recordLive", true);
        }
        if (const auto& l = a.lfo; ! l.isDefault()) // round 177b: FL's clip LFO
        {
            juce::Array<juce::var> amp;
            for (const auto& pt : l.amp)
            {
                juce::Array<juce::var> p { (juce::int64) pt.tick, (double) pt.value, (double) pt.tension };
                if (pt.mode != AutomationPoint::legacyCurve && ! (pt.mode == 0 && pt.tension == 0.0f))
                    p.add ((int) pt.mode);
                amp.add (p);
            }
            o->setProperty ("lfo", juce::Array<juce::var> { l.on, l.level, l.add, l.flags, l.speed, l.tension, l.skew, l.pulseWidth,
                                                            (juce::int64) l.phase, (double) l.range, amp });
            if (l.fl.present)
            {
                juce::Array<juce::var> fl { l.fl.mainFlags, l.fl.mainEnabled };
                for (const auto v : l.fl.mainLoop)
                    fl.add (v);
                for (const auto v : l.fl.adsr)
                    fl.add (v);
                for (const auto v : l.fl.lfoLoop)
                    fl.add (v);
                // the FL records of the LFO's points, while they still match `amp`
                fl.add (l.amp == l.fl.rawAmp ? juce::Base64::toBase64 (l.fl.rawPoints.data(), l.fl.rawPoints.size()) : juce::String());
                o->setProperty ("lfoFl", fl);
            }
        }
        automations.add (juce::var (o.release()));
    }

    if (! automations.isEmpty())
        root->setProperty ("automations", automations);

    // round 177b: LYNX's modulators
    if (! song.modulators.empty())
    {
        juce::Array<juce::var> mods;
        for (const auto& m : song.modulators)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("name", str (m.name));
            o->setProperty ("shape", (int) m.shape);
            o->setProperty ("sync", m.sync);
            o->setProperty ("beats", m.beats);
            o->setProperty ("hz", m.hz);
            o->setProperty ("phase", (double) m.phase);
            o->setProperty ("unipolar", m.unipolar);
            o->setProperty ("seed", (juce::int64) m.seed);
            juce::Array<juce::var> routes;
            for (const auto& r : m.routes)
                routes.add (juce::Array<juce::var> { targetKindName (r.target.kind), r.target.index, r.target.slot, r.target.param, (double) r.depth });
            o->setProperty ("routes", routes);
            mods.add (juce::var (o.release()));
        }
        root->setProperty ("modulators", mods);
    }

    if (! song.vcas.empty()) // VCA faders (round 177a)
    {
        juce::Array<juce::var> vcas;
        for (const auto& v : song.vcas)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("name", juce::String::fromUTF8 (v.name.c_str()));
            if (v.colour != 0)
                o->setProperty ("colour", juce::String::toHexString ((juce::int64) (v.colour & 0xFFFFFF)).paddedLeft ('0', 6));
            o->setProperty ("level", (double) v.level);
            if (v.muted)
                o->setProperty ("muted", true);
            juce::Array<juce::var> members;
            for (const auto m : v.members)
                members.add (m);
            o->setProperty ("members", members);
            if (v.parent >= 0)
                o->setProperty ("parent", v.parent);
            vcas.add (juce::var (o.release()));
        }
        root->setProperty ("vcas", vcas);
    }

    if (! song.midiLinks.empty())
    {
        juce::Array<juce::var> links, settings, noteLinks;
        // a link's Remote control settings (round 190bt): "midiLinkSettings", beside the plain list (older versions read
        // "midiLinks" as before and skip it); only the links that have any
        const auto settingsOf = [] (const MidiLink& l, juce::DynamicObject& o) {
            if (l.port >= 0)
                o.setProperty ("port", l.port);
            if (! l.formula.empty())
                o.setProperty ("formula", juce::String::fromUTF8 (l.formula.c_str()));
            if (l.smooth)
                o.setProperty ("smooth", true);
            if (l.smoothing != 469)
                o.setProperty ("smoothing", l.smoothing);
            if (l.omni)
                o.setProperty ("omni", true);
            if (l.mode != 0) // FL's link Mode (round 190cs)
                o.setProperty ("mode", l.mode);
            if (l.min != 0.0f || l.max != 1.0f) // the mapping range (round 190ef, Live's Min / Max)
            {
                o.setProperty ("min", l.min);
                o.setProperty ("max", l.max);
            }
        };

        for (const auto& l : song.midiLinks)
        {
            if (isNoteLink (l.cc))
            {
                // round 190cs: links to keys go to their own list, which older versions skip (in "midiLinks" they would
                // read as controller links)
                auto o = std::make_unique<juce::DynamicObject>();
                o->setProperty ("note", l.cc - linkNoteBase);
                o->setProperty ("channel", l.channel);
                o->setProperty ("target", juce::Array<juce::var> { targetKindName (l.target.kind), l.target.index, l.target.slot, l.target.param });
                settingsOf (l, *o);
                noteLinks.add (juce::var (o.release()));
                continue;
            }
            if (l.hasSettings())
            {
                auto o = std::make_unique<juce::DynamicObject>();
                o->setProperty ("link", links.size());
                settingsOf (l, *o);
                settings.add (juce::var (o.release()));
            }
            links.add (juce::Array<juce::var> { l.cc, l.channel, targetKindName (l.target.kind), l.target.index, l.target.slot, l.target.param });
        }

        if (! links.isEmpty())
            root->setProperty ("midiLinks", links);
        if (! settings.isEmpty())
            root->setProperty ("midiLinkSettings", settings);
        if (! noteLinks.isEmpty())
            root->setProperty ("midiNoteLinks", noteLinks);
    }

    if (! song.remoteMappings.empty()) // round 190ef: key mappings, MIDI mappings of slots / scenes / transport
    {
        juce::Array<juce::var> list;
        for (const auto& m : song.remoteMappings)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            if (m.source == RemoteMapping::Source::key)
                o->setProperty ("key", m.key);
            else
            {
                o->setProperty ("number", m.number);
                o->setProperty ("channel", m.channel);
                if (m.port >= 0)
                    o->setProperty ("port", m.port);
            }
            o->setProperty ("action", (int) m.action);
            if (m.action == RemoteAction::parameter)
                o->setProperty ("target", juce::Array<juce::var> { targetKindName (m.target.kind), m.target.index, m.target.slot, m.target.param });
            if (m.track >= 0)
                o->setProperty ("track", m.track);
            if (m.scene >= 0)
                o->setProperty ("scene", m.scene);
            if (m.min != 0.0f || m.max != 1.0f)
            {
                o->setProperty ("min", m.min);
                o->setProperty ("max", m.max);
            }
            list.add (juce::var (o.release()));
        }
        root->setProperty ("remoteMappings", list);
    }

    if (! song.initialValues.empty()) // FL's initialized controls (REA round 146)
    {
        juce::Array<juce::var> values;
        for (const auto& v : song.initialValues)
            values.add (juce::Array<juce::var> { targetKindName (v.target.kind), v.target.index, v.target.slot, v.target.param, v.value });
        root->setProperty ("initialValues", values);
    }

    // video tracks and the project's video settings (round 174d, core/VideoTrack.h)
    if (! song.videoTracks.empty() || ! (song.video == VideoSettings {}))
    {
        auto* v = new juce::DynamicObject();
        v->setProperty ("rate", str (vtime::rateId (song.video.rate)));
        v->setProperty ("startTimecode", song.video.startTimecode);
        v->setProperty ("rateFromVideo", song.video.rateFromVideo);
        v->setProperty ("timecodeRuler", song.video.showTimecodeRuler);
        v->setProperty ("burnIn", song.video.burnIn);
        juce::Array<juce::var> tracks;
        for (const auto& t : song.videoTracks)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", str (t.name));
            if (t.muted)
                o->setProperty ("muted", true);
            if (t.height > 0)
                o->setProperty ("height", t.height);
            if (t.colour != 0)
                o->setProperty ("colour", (int) (t.colour & 0xFFFFFF));
            juce::Array<juce::var> clips;
            for (const auto& c : t.clips)
            {
                auto* k = new juce::DynamicObject();
                k->setProperty ("path", str (c.path));
                k->setProperty ("start", c.start);
                k->setProperty ("offset", c.offset);
                k->setProperty ("length", c.length);
                k->setProperty ("duration", c.duration);
                k->setProperty ("uid", uidVar (c.uid));
                clips.add (juce::var (k));
            }
            o->setProperty ("clips", clips);
            tracks.add (juce::var (o));
        }
        v->setProperty ("tracks", tracks);
        root->setProperty ("video", juce::var (v));
    }

    // the ADR session (round 174d, core/Adr.h)
    if (! (song.adr == AdrSession {}))
    {
        const auto& a = song.adr;
        auto* o = new juce::DynamicObject();
        auto* st = new juce::DynamicObject();
        st->setProperty ("preRoll", a.settings.preRoll);
        st->setProperty ("postRoll", a.settings.postRoll);
        st->setProperty ("beeps", a.settings.beeps);
        st->setProperty ("beepFrequency", a.settings.beepFrequency);
        st->setProperty ("streamer", a.settings.streamer);
        st->setProperty ("streamerLength", a.settings.streamerLength);
        st->setProperty ("streamerColour", (int) (a.settings.streamerColour & 0xFFFFFF));
        st->setProperty ("punch", a.settings.punch);
        st->setProperty ("cycle", a.settings.cycle);
        st->setProperty ("guideInsert", a.settings.guideInsert);
        st->setProperty ("headphoneOutput", a.settings.headphoneOutput);
        st->setProperty ("recordInsert", a.settings.recordInsert);
        st->setProperty ("takeTrack", a.settings.takeTrack);
        o->setProperty ("settings", juce::var (st));
        o->setProperty ("current", a.current);
        juce::Array<juce::var> cues;
        for (const auto& c : a.cues)
        {
            auto* k = new juce::DynamicObject();
            k->setProperty ("id", str (c.id));
            k->setProperty ("in", c.in);
            k->setProperty ("out", c.out);
            k->setProperty ("character", str (c.character));
            k->setProperty ("line", str (c.line));
            k->setProperty ("notes", str (c.notes));
            k->setProperty ("status", str (adr::statusName (c.status)));
            k->setProperty ("selected", c.selected);
            juce::Array<juce::var> takes;
            for (const auto& t : c.takes)
            {
                auto* tk = new juce::DynamicObject();
                tk->setProperty ("number", t.number);
                tk->setProperty ("name", str (t.name));
                tk->setProperty ("clip", uidVar (t.clip));
                tk->setProperty ("rating", t.rating);
                tk->setProperty ("circled", t.circled);
                tk->setProperty ("note", str (t.note));
                tk->setProperty ("aligned", t.aligned);
                takes.add (juce::var (tk));
            }
            k->setProperty ("takes", takes);
            juce::Array<juce::var> comp;
            for (const auto& p : c.comp)
                comp.add (juce::Array<juce::var> { p.from, p.to, p.take });
            k->setProperty ("comp", comp);
            juce::Array<juce::var> compClips;
            for (const auto u : c.compClips)
                compClips.add (uidVar (u));
            k->setProperty ("compClips", compClips);
            cues.add (juce::var (k));
        }
        o->setProperty ("cues", cues);
        root->setProperty ("adr", juce::var (o));
    }

    root->setProperty ("playlist", clipsToVar (song.playlist));
    root->setProperty ("clipFadesPerClip", true); // round 180d: a project without it has linear clip fades
    if (song.clipGrid) // the Clip-launch workflow's grid (round 181a): only in a project that has one
        root->setProperty ("clipGrid", clipGridToVar (*song.clipGrid));
    if (song.grooves != GroovePool {}) // the groove pool (round 190bv)
        root->setProperty ("grooves", groovePoolToVar (song.grooves));
    if (song.patternBlocksOn)
        root->setProperty ("patternBlocks", true);
    if (! song.blocks.empty())
        root->setProperty ("blocks", blocksToVar (song.blocks));

    {
        juce::Array<juce::var> tracks;

        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& t = song.playlistTracks[i]; ! t.name.empty() || t.muted)
                tracks.add (juce::Array<juce::var> { (int) i, str (t.name), t.muted });

        if (! tracks.isEmpty())
            root->setProperty ("playlistTracks", tracks);

        // row heights in a separate list, so older versions still read the names and mutes
        juce::Array<juce::var> heights;

        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].height > 0)
                heights.add (juce::Array<juce::var> { (int) i, song.playlistTracks[i].height });

        if (! heights.isEmpty())
            root->setProperty ("playlistTrackHeights", heights);

        juce::Array<juce::var> colours; // [index, 0xRRGGBB]

        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].colour != 0)
                colours.add (juce::Array<juce::var> { (int) i, (int) (song.playlistTracks[i].colour & 0xFFFFFF) });

        if (! colours.isEmpty())
            root->setProperty ("playlistTrackColours", colours);

        // track groups: [index, grouped, collapsed] for the tracks that have either (older versions ignore it)
        juce::Array<juce::var> groups;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].grouped || song.playlistTracks[i].collapsed)
                groups.add (juce::Array<juce::var> { (int) i, song.playlistTracks[i].grouped ? 1 : 0, song.playlistTracks[i].collapsed ? 1 : 0 });

        if (! groups.isEmpty())
            root->setProperty ("playlistTrackGroups", groups);

        // comp tracks (round 177c): their indexes; a project without any has no such property
        juce::Array<juce::var> compTracks;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].compTrack)
                compTracks.add ((int) i);

        if (! compTracks.isEmpty())
            root->setProperty ("playlistCompTracks", compTracks);

        // FL's track locks (round 168f): [index, lock to content 0/1, locked height (px; <= 0 = not locked)]
        juce::Array<juce::var> locks;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& t = song.playlistTracks[i]; t.lockedToContent || t.lockedHeight != -1)
                locks.add (juce::Array<juce::var> { (int) i, t.lockedToContent ? 1 : 0, t.lockedHeight });

        if (! locks.isEmpty())
            root->setProperty ("playlistTrackLocks", locks);

        // automation lanes (round 177b): [track, folded 0/1, [automation, height], ...]
        juce::Array<juce::var> lanes;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& t = song.playlistTracks[i]; ! t.lanes.empty())
            {
                juce::Array<juce::var> row { (int) i, t.lanesFolded ? 1 : 0 };
                for (const auto& l : t.lanes)
                    row.add (juce::Array<juce::var> { l.automation, l.height });
                lanes.add (row);
            }
        if (! lanes.isEmpty())
            root->setProperty ("playlistTrackLanes", lanes);

        // track modes (round 146a): [index, "audio" | "instrument", mixer track, channel]
        juce::Array<juce::var> modes;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& t = song.playlistTracks[i]; t.mode != PlaylistTrackMode::unassigned)
                modes.add (juce::Array<juce::var> { (int) i, t.mode == PlaylistTrackMode::audio ? "audio" : "instrument", t.mixerTrack, t.channel });

        if (! modes.isEmpty())
            root->setProperty ("playlistTrackModes", modes);

        // Performance settings (round 165m): [index, press, motion, trigger sync, queue, tolerant, position sync] (FL's
        // numbers) for the tracks off FL's defaults
        juce::Array<juce::var> performance;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (const auto& p = song.playlistTracks[i].performance; ! p.isDefault())
                performance.add (juce::Array<juce::var> { (int) i, (int) p.press, (int) p.motion, (int) p.triggerSync, p.queue ? 1 : 0,
                                                          p.tolerant ? 1 : 0, (int) p.positionSync });
        if (! performance.isEmpty())
            root->setProperty ("playlistTrackPerformance", performance);

        // FL's playlist track icons (round 168j): [index, FL icon index]
        juce::Array<juce::var> icons;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].icon != 0)
                icons.add (juce::Array<juce::var> { (int) i, song.playlistTracks[i].icon });
        if (! icons.isEmpty())
            root->setProperty ("playlistTrackIcons", icons);

        // folded tracks (round 190cg): the indices
        juce::Array<juce::var> foldedTracks;
        for (std::size_t i = 0; i < song.playlistTracks.size(); ++i)
            if (song.playlistTracks[i].folded)
                foldedTracks.add ((int) i);
        if (! foldedTracks.isEmpty())
            root->setProperty ("playlistTracksFolded", foldedTracks);
    }

    const auto anyMarkers = std::any_of (song.arrangements.begin(), song.arrangements.end(),
                                         [] (const Arrangement& a) { return ! a.markers.empty() || ! a.blocks.empty(); });

    if (song.arrangements.size() > 1 || anyMarkers)
    {
        juce::Array<juce::var> arrangements;

        for (std::size_t i = 0; i < song.arrangements.size(); ++i)
        {
            const auto& a = song.arrangements[i];
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("name", str (a.name));

            if ((int) i != song.currentArrangement) // the current one is in "playlist"
            {
                o->setProperty ("clips", clipsToVar (a.clips));
                if (! a.blocks.empty())
                    o->setProperty ("blocks", blocksToVar (a.blocks));

                if (a.loopEnd > a.loopStart)
                    o->setProperty ("loop", juce::Array<juce::var> { (juce::int64) a.loopStart, (juce::int64) a.loopEnd });
            }

            // markers: [tick, name, numerator, denominator] (numerator 0 = plain marker)
            if (! a.markers.empty())
            {
                juce::Array<juce::var> markers;
                for (const auto& m : a.markers)
                {
                    juce::Array<juce::var> one { (juce::int64) m.tick, str (m.name), m.numerator, m.denominator };
                    if (m.action != 0) // FL's marker action (Start / Stop recording...)
                        one.add (m.action);
                    markers.add (one);
                }
                o->setProperty ("markers", markers);
            }

            arrangements.add (juce::var (o.release()));
        }

        root->setProperty ("arrangements", arrangements);
        root->setProperty ("currentArrangement", song.currentArrangement);
    }

    juce::Array<juce::var> mixer;

    for (const auto& t : song.mixer)
        mixer.add (mixerTrackToVar (t));

    root->setProperty ("mixer", mixer);
    storeSendSpans (song, *root); // round 167a: "Send to" over 0..127 when it fits (older LYNX versions)
    root->setProperty ("mode", song.mode == PlayMode::song ? "song" : "pattern");

    if (song.hasLoop())
        root->setProperty ("loop", juce::Array<juce::var> { (juce::int64) song.loopStart, (juce::int64) song.loopEnd });
    if (song.loopOff) // round 190cg: the loop switch (only when off: older versions loop the range, as before)
        root->setProperty ("loopOff", true);
    if (song.tempoViewHigh > song.tempoViewLow) // round 190cg: the tempo lane's shown range (BPM)
        root->setProperty ("tempoView", juce::Array<juce::var> { song.tempoViewLow, song.tempoViewHigh });

    // Online Sessions (round 168a): the lineage, the uid counter, the arrangements' uids (the arrangements list itself
    // is only written when there is more than one) and who works on the project
    if (! song.projectId.empty())
        root->setProperty ("projectId", str (song.projectId));
    if (song.nextUid != 0)
        root->setProperty ("nextUid", uidVar (song.nextUid));
    {
        juce::Array<juce::var> ids;
        for (const auto& a : song.arrangements)
            ids.add (uidVar (a.uid));
        if (std::any_of (song.arrangements.begin(), song.arrangements.end(), [] (const Arrangement& a) { return a.uid != 0; }))
            root->setProperty ("arrangementUids", ids);
    }
    if (! song.session.empty())
    {
        auto block = std::make_unique<juce::DynamicObject>();
        block->setProperty ("me", (int) song.session.me);
        juce::Array<juce::var> people;
        for (const auto& p : song.session.participants)
        {
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("tag", (int) p.tag);
            o->setProperty ("name", str (p.name));
            o->setProperty ("colour", juce::String::toHexString ((juce::int64) (p.colour & 0xFFFFFF)).paddedLeft ('0', 6));
            if (p.insertCount > 0)
                o->setProperty ("inserts", juce::Array<juce::var> { p.insertFirst, p.insertCount });
            if (p.rowCount > 0)
                o->setProperty ("rows", juce::Array<juce::var> { p.rowFirst, p.rowCount });
            if (p.keepFull)
                o->setProperty ("keepFull", true);
            people.add (juce::var (o.release()));
        }
        block->setProperty ("participants", people);
        root->setProperty ("session", juce::var (block.release()));
    }
    return juce::JSON::toString (juce::var (root.release()), false);
}

std::optional<Song> fromJson (const juce::String& json, juce::String& error, juce::String* savedBy)
{
    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);

    if (parsed.failed())
    {
        error = utf8 ("invalid JSON: ") + parsed.getErrorMessage();
        return std::nullopt;
    }

    // "daw-project" = projects saved before the project was named LYNX
    if (root["format"].toString() != "lynx-project" && root["format"].toString() != "daw-project")
    {
        error = utf8 ("not a project from this DAW");
        return std::nullopt;
    }

    if ((int) root["version"] > currentVersion)
    {
        error = utf8 ("project saved by a newer version of LYNX SHARE");
        return std::nullopt;
    }
    if (savedBy != nullptr)
        *savedBy = root["savedBy"].toString();

    Song song;
    song.bpm = juce::jlimit (10.0, 999.0, (double) root.getProperty ("bpm", 130.0));
    song.beatsPerBar = juce::jlimit (1, 32, (int) root.getProperty ("beatsPerBar", 4));
    song.beatUnit = meterUnit ((int) root.getProperty ("beatUnit", 4));
    song.swing = juce::jlimit (0.0f, 1.0f, (float) (double) root.getProperty ("swing", 0.0));
    song.chanceSeed = (std::uint32_t) (juce::int64) root.getProperty ("chanceSeed", 0);
    if (auto* groups = root["channelGroups"].getArray())
        for (const auto& name : *groups)
            song.channelGroups.push_back (name.toString().toStdString());
    song.masterPitch = juce::jlimit (-1200.0f, 1200.0f, (float) (double) root.getProperty ("masterPitch", 0.0));
    {
        const auto t = juce::StringArray::fromTokens (root.getProperty ("masterLook", "").toString(), " ", "");
        song.masterLook = {};
        if (t.size() == 6)
        {
            auto& l = song.masterLook;
            l.id = juce::jlimit (0, 255, t[0].getIntValue());
            l.intensity = juce::jlimit (0.0f, 1.0f, t[1].getFloatValue());
            for (int i = 0; i < 3; ++i)
                l.tone[(std::size_t) i] = juce::jlimit (-6.0f, 6.0f, t[2 + i].getFloatValue());
            l.inputDb = juce::jlimit (-18.0f, 18.0f, t[5].getFloatValue());
        }
    }
    song.substitutesBypassed = (bool) root.getProperty ("substitutesBypassed", false);
    song.cleanMixer = (bool) root.getProperty ("cleanMixer", false);
    song.performanceMode = (bool) root.getProperty ("performanceMode", false);
    song.autoCrossfades = (bool) root.getProperty ("autoCrossfades", false);
    song.compCrossfadeMs = juce::jlimit (0.0f, 100.0f, (float) (double) root.getProperty ("compCrossfadeMs", (double) Song::defaultCompCrossfadeMs));
    song.playTruncatedNotes = (bool) root.getProperty ("playTruncatedNotes", false);
    song.muteRunsEffects = (bool) root.getProperty ("muteRunsEffects", false); // (round 190y; older projects: off)
    song.patternClipTakeover = (bool) root.getProperty ("patternClipTakeover", false); // (issue #158; older projects: off)
    song.mutedInsertsSkip = (bool) root.getProperty ("mutedInsertsSkip", false); // (LYNX option, round 190y)
    song.fastDeclick = (bool) root.getProperty ("fastDeclick", false);
    song.panTriangular = root.getProperty ("panLaw", "circular").toString() == "triangular";
    song.flPluginLevels = root.getProperty ("pluginLevels", "lynx").toString() == "fl";
    song.autoPdc = (bool) root.getProperty ("autoPdc", true);
    song.compensateAutomations = (bool) root.getProperty ("compensateAutomations", true);
    song.timeSignatureMode = root.getProperty ("timeSettings", "division").toString() == "signature";
    if (const auto& sh = root["scaleHighlight"]; sh.isArray() && sh.size() >= 2)
        song.scaleHighlight = { juce::jlimit (-3, 255, (int) sh[0]), juce::jlimit (0, 11, (int) sh[1]) };
    song.dataFolder = root.getProperty ("dataFolder", {}).toString().toStdString();
    song.keyboardChords.clear();
    if (const auto* chords = root.getProperty ("keyboardChords", {}).getArray())
        for (const auto& c : *chords)
            if (c.isArray() && c.size() >= 3)
            {
                KeyboardChord k;
                k.root = juce::jlimit (0, 11, (int) c[0]);
                k.quality = juce::jlimit (0, 5, (int) c[1]);
                k.octave = juce::jlimit (0, 9, (int) c[2]);
                if (c.size() >= 4)
                    if (const auto* ext = c[3].getArray())
                        for (const auto& e : *ext)
                            k.extensions.push_back (juce::jlimit (1, 35, (int) e));
                if (song.keyboardChords.size() < 24)
                    song.keyboardChords.push_back (std::move (k));
            }
    if (const auto info = root.getProperty ("info", {}); info.isObject())
    {
        auto get = [&info] (const char* key) { return info.getProperty (key, {}).toString().toStdString(); };
        song.info.title = get ("title");
        song.info.author = get ("author");
        song.info.genre = get ("genre");
        song.info.url = get ("url");
        song.info.comments = get ("comments");
        song.info.showOnOpen = (bool) info.getProperty ("showOnOpen", false);
        song.info.created = std::max (0.0, (double) info.getProperty ("created", 0.0));
        song.info.workSeconds = std::max (0.0, (double) info.getProperty ("workSeconds", 0.0));
    }
    song.currentPattern = (int) root.getProperty ("currentPattern", 0);

    if (auto* channels = root["channels"].getArray())
        for (const auto& c : *channels)
            song.channels.push_back (channelFromVar (c, song));
    // projects imported from FL before round 167j kept the triangular law only in their Sampler channels' snapshot
    if (! root.hasProperty ("panLaw"))
        for (const auto& c : song.channels)
            if (c.sampler.flChannel.size() == 90 && c.sampler.flChannel[89] == 2)
                song.panTriangular = true;

    if (auto* patterns = root["patterns"].getArray())
        for (const auto& p : *patterns)
        {
            Pattern pattern;
            pattern.name = p["name"].toString().toStdString();
            pattern.uid = uidFrom (p.getProperty ("uid", {}));
            pattern.lengthTicks = (juce::int64) p.getProperty ("length", 0);

            if (p.hasProperty ("colour"))
                pattern.colour = 0xFF000000u | (std::uint32_t) p["colour"].toString().getHexValue32();

            pattern.loopOn = (bool) p.getProperty ("loopOn", false);
            pattern.loopStarterGenre = p.getProperty ("loopStarterGenre", "").toString().toStdString();
            pattern.loopMode = (RackLoopMode) juce::jlimit (1, 3, (int) p.getProperty ("loopMode", 1));
            if (auto* loops = p["channelLoops"].getArray())
                for (const auto& l : *loops)
                    if (l.isArray() && l.size() >= 2 && (int) l[0] >= 0)
                        pattern.channelLoops.push_back ({ (int) l[0], juce::jmax (-3, (int) l[1]) });

            if (auto* markers = p["markers"].getArray())
                for (const auto& m : *markers)
                    if (m.isArray() && (m.size() == 4 || m.size() == 5))
                        pattern.markers.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) m[0]), m[1].toString().toStdString(),
                                                     juce::jlimit (0, 32, (int) m[2]), juce::jlimit (1, 32, (int) m[3]),
                                                     m.size() == 5 ? juce::jlimit (0, 255, (int) m[4]) : 0 });
            std::stable_sort (pattern.markers.begin(), pattern.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });

            if (auto* notes = p["notes"].getArray())
                for (const auto& n : *notes)
                {
                    if (! n.isArray() || n.size() < 5)
                        continue;

                    Note note;
                    note.start = (juce::int64) n[0];
                    note.length = juce::jmax<juce::int64> (1, (juce::int64) n[1]);
                    note.channel = (int) n[2];
                    note.key = juce::jlimit (0, 127, (int) n[3]);
                    note.velocity = juce::jlimit (0.0f, 1.0f, (float) (double) n[4]);

                    if (n.size() >= 8)
                    {
                        note.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) n[5]);
                        note.finePitch = juce::jlimit (-120.0f, 120.0f, (float) (double) n[6]);
                        note.muted = (bool) n[7];
                        note.slide = n.size() >= 9 && (bool) n[8];
                        note.porta = n.size() >= 10 && (bool) n[9];

                        if (n.size() >= 13)
                        {
                            note.release = juce::jlimit (0.0f, 1.0f, (float) (double) n[10]);
                            note.modX = juce::jlimit (0.0f, 1.0f, (float) (double) n[11]);
                            note.modY = juce::jlimit (0.0f, 1.0f, (float) (double) n[12]);
                        }
                        if (n.size() >= 14)
                            note.colour = juce::jlimit (0, noteColourCount - 1, (int) n[13]);
                        if (n.size() >= 15)
                            note.group = juce::jmax (0, (int) n[14]);
                        if (n.size() >= 19)
                        {
                            note.chance = juce::jlimit (0.0f, 1.0f, (float) (double) n[15]);
                            note.velocityRange = juce::jlimit (-1.0f, 1.0f, (float) (double) n[16]);
                            note.chanceGroup = juce::jmax (0, (int) n[17]);
                            note.chanceOne = (bool) n[18];
                        }
                    }

                    if (note.channel >= 0 && note.channel < (int) song.channels.size())
                        pattern.notes.push_back (note);
                }

            pattern.sortNotes();

            if (auto* lanes = p["events"].getArray())
                for (const auto& l : *lanes)
                {
                    PatternEvents lane;
                    if (const auto& t = l["target"]; t.isArray() && t.size() == 4)
                        lane.target = { targetKindFromName (t[0].toString()), (int) t[1], (int) t[2], (int) t[3] };
                    if (auto* events = l["events"].getArray())
                        for (const auto& e : *events)
                            if (e.isArray() && e.size() >= 2)
                                lane.events.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) e[0]), juce::jlimit (0.0f, 1.0f, (float) (double) e[1]) });
                    std::stable_sort (lane.events.begin(), lane.events.end(), [] (const PatternEvent& a, const PatternEvent& b) { return a.tick < b.tick; });
                    if (lane.target.kind != AutomationTargetKind::none && ! lane.events.empty())
                        pattern.events.push_back (std::move (lane));
                }

            song.patterns.push_back (std::move (pattern));
        }

    if (song.patterns.empty())
        song.patterns.emplace_back().name = "Pattern 1";

    if (auto* automations = root["automations"].getArray())
        for (const auto& a : *automations)
        {
            Automation automation;
            automation.name = a["name"].toString().toStdString();
            automation.uid = uidFrom (a.getProperty ("uid", {}));

            if (const auto& t = a["target"]; t.isArray() && t.size() == 4)
                automation.target = { targetKindFromName (t[0].toString()), (int) t[1], (int) t[2], (int) t[3] };

            if (auto* points = a["points"].getArray())
                for (const auto& pt : *points)
                    if (pt.isArray() && pt.size() >= 2)
                        automation.points.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) pt[0]),
                                                       juce::jlimit (0.0f, 1.0f, (float) (double) pt[1]),
                                                       pt.size() >= 3 ? juce::jlimit (-1.0f, 1.0f, (float) (double) pt[2]) : 0.0f,
                                                       savedPointMode (pt) });

            if (const auto& source = a["source"]; source.isArray() && source.size() >= 3)
                automation.source = { (int) source[0], (int) source[1], (int) source[2], source.size() >= 4 ? (int) source[3] : -1 };
            automation.recordLive = (bool) a["recordLive"];
            if (auto* curve = a["curve"].getArray())
                for (const auto& v : *curve)
                    automation.curve.push_back (juce::jlimit (0.0f, 1.0f, (float) (double) v));

            std::stable_sort (automation.points.begin(), automation.points.end(),
                              [] (const AutomationPoint& x, const AutomationPoint& y) { return x.tick < y.tick; });
            if (const auto& l = a["lfo"]; l.isArray() && l.size() >= 11) // round 177b: FL's clip LFO
            {
                auto& lfo = automation.lfo;
                lfo.on = (bool) l[0];
                lfo.level = juce::jlimit (-128, 128, (int) l[1]);
                lfo.add = (bool) l[2];
                lfo.flags = (int) l[3];
                lfo.speed = juce::jlimit (512, 65536, (int) l[4]);
                lfo.tension = juce::jlimit (-128, 128, (int) l[5]);
                lfo.skew = juce::jlimit (-128, 128, (int) l[6]);
                lfo.pulseWidth = juce::jlimit (-112, 112, (int) l[7]);
                lfo.phase = (std::uint32_t) (juce::int64) l[8];
                lfo.range = juce::jlimit (0.0f, 1.0f, (float) (double) l[9]);
                lfo.amp.clear();
                if (auto* amp = l[10].getArray())
                    for (const auto& pt : *amp)
                        if (pt.isArray() && pt.size() >= 3)
                            lfo.amp.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) pt[0]), juce::jlimit (0.0f, 1.0f, (float) (double) pt[1]),
                                                 juce::jlimit (-1.0f, 1.0f, (float) (double) pt[2]), savedPointMode (pt) });
                if (lfo.amp.empty())
                    lfo.amp.push_back ({ 0, 1.0f, 0.0f });
                if (const auto& f = a["lfoFl"]; f.isArray() && f.size() >= 13)
                {
                    lfo.fl.present = true;
                    lfo.fl.mainFlags = (int) f[0];
                    lfo.fl.mainEnabled = (int) f[1];
                    for (int k = 0; k < 3; ++k)
                        lfo.fl.mainLoop[(std::size_t) k] = (int) f[2 + k];
                    for (int k = 0; k < 4; ++k)
                        lfo.fl.adsr[(std::size_t) k] = (int) f[5 + k];
                    for (int k = 0; k < 3; ++k)
                        lfo.fl.lfoLoop[(std::size_t) k] = (int) f[9 + k];
                    juce::MemoryOutputStream raw;
                    if (f[12].toString().isNotEmpty() && juce::Base64::convertFromBase64 (raw, f[12].toString()))
                    {
                        lfo.fl.rawPoints.assign ((const std::uint8_t*) raw.getData(), (const std::uint8_t*) raw.getData() + raw.getDataSize());
                        lfo.fl.rawAmp = lfo.amp;
                    }
                }
            }
            song.automations.push_back (std::move (automation));
        }

    song.playlist = clipsFromVar (root["playlist"], song);
    // saved before FL's S curve for clip fades (round 180d): those clips keep the straight fades the mix was made with
    const bool oldClipFades = ! root.hasProperty ("clipFadesPerClip");
    const auto keepOldClipFades = [oldClipFades] (std::vector<Clip>& clips)
    {
        if (oldClipFades)
            for (auto& clip : clips)
                clip.linearFades = (std::uint8_t) ((clip.fadeIn > 0 ? 1 : 0) | (clip.fadeOut > 0 ? 2 : 0));
    };
    keepOldClipFades (song.playlist);
    song.clipGrid = clipGridFromVar (root["clipGrid"], song);
    song.grooves = groovePoolFromVar (root["grooves"]);
    if (song.clipGrid) // (a clip never points at a groove the pool does not have)
        for (auto& slot : song.clipGrid->slots)
            if (slot.groove != 0 && groove::find (song.grooves, slot.groove) == nullptr)
                slot.groove = 0;
    song.patternBlocksOn = (bool) root.getProperty ("patternBlocks", false);
    song.blocks = blocksFromVar (root["blocks"], song);

    if (auto* vcas = root["vcas"].getArray())
        for (const auto& v : *vcas)
        {
            Vca vca;
            vca.name = v["name"].toString().toStdString();
            if (v.hasProperty ("colour"))
                vca.colour = 0xFF000000u | (std::uint32_t) v["colour"].toString().getHexValue32();
            vca.level = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("level", (double) unityFader));
            vca.muted = (bool) v.getProperty ("muted", false);
            if (auto* members = v["members"].getArray())
                for (const auto& m : *members)
                    if ((int) m > 0 && (int) m < maxMixerTracks)
                        vca.members.push_back ((int) m);
            std::sort (vca.members.begin(), vca.members.end());
            vca.members.erase (std::unique (vca.members.begin(), vca.members.end()), vca.members.end());
            vca.parent = (int) v.getProperty ("parent", -1);
            song.vcas.push_back (std::move (vca));
        }
    for (auto& v : song.vcas)
        if (v.parent < -1 || v.parent >= (int) song.vcas.size() || &song.vcas[(std::size_t) std::max (0, v.parent)] == &v)
            v.parent = -1;

    if (auto* links = root["midiLinks"].getArray())
        for (const auto& l : *links)
            if (l.isArray() && l.size() == 6)
                song.midiLinks.push_back ({ juce::jlimit (0, 127, (int) l[0]), juce::jlimit (-1, 15, (int) l[1]),
                                            { targetKindFromName (l[2].toString()), (int) l[3], (int) l[4], (int) l[5] } });
            else
                song.midiLinks.push_back ({ -1, -1, {} }); // (keeps the places "midiLinkSettings" counts; dropped below)

    const auto readSettings = [] (const juce::var& o, MidiLink& l) {
        if (o.hasProperty ("port"))
            l.port = juce::jlimit (-1, 255, (int) o["port"]);
        l.formula = o["formula"].toString().toStdString();
        l.smooth = (bool) o["smooth"];
        if (o.hasProperty ("smoothing"))
            l.smoothing = juce::jlimit (-1024, 1024, (int) o["smoothing"]);
        l.omni = (bool) o["omni"];
        l.mode = juce::jlimit (0, 4, (int) o["mode"]); // (round 190cs)
        if (o.hasProperty ("min") || o.hasProperty ("max")) // (round 190ef)
        {
            l.min = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("min", 0.0));
            l.max = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("max", 1.0));
        }
    };
    if (auto* settings = root["midiLinkSettings"].getArray()) // round 190bt
        for (const auto& o : *settings)
        {
            const auto i = o.isObject() && o.hasProperty ("link") ? (int) o["link"] : -1;
            if (i < 0 || i >= (int) song.midiLinks.size())
                continue;
            readSettings (o, song.midiLinks[(std::size_t) i]);
        }
    if (auto* notes = root["midiNoteLinks"].getArray()) // round 190cs: links to keys
        for (const auto& o : *notes)
        {
            const auto* target = o["target"].getArray();
            if (! o.isObject() || target == nullptr || target->size() != 4)
                continue;
            MidiLink l { linkNoteBase + juce::jlimit (0, 127, (int) o["note"]), juce::jlimit (-1, 15, (int) o["channel"]),
                         { targetKindFromName ((*target)[0].toString()), (int) (*target)[1], (int) (*target)[2], (int) (*target)[3] } };
            readSettings (o, l);
            song.midiLinks.push_back (std::move (l));
        }
    song.midiLinks.erase (std::remove_if (song.midiLinks.begin(), song.midiLinks.end(), [] (const MidiLink& l) { return l.cc < 0; }),
                          song.midiLinks.end());

    if (auto* mappings = root["remoteMappings"].getArray()) // round 190ef
        for (const auto& o : *mappings)
        {
            if (! o.isObject())
                continue;
            RemoteMapping m;
            const auto action = (int) o.getProperty ("action", -1);
            if (action < 0 || action > (int) RemoteAction::metronome)
                continue; // (an action a newer version added)
            m.action = (RemoteAction) action;
            if (o.hasProperty ("key"))
            {
                m.source = RemoteMapping::Source::key;
                m.key = (int) o["key"];
            }
            else
            {
                m.number = (int) o.getProperty ("number", -1);
                m.channel = juce::jlimit (-1, 15, (int) o.getProperty ("channel", -1));
                m.port = juce::jlimit (-1, 255, (int) o.getProperty ("port", -1));
                if (! (m.number >= 0 && m.number < 128) && ! isNoteLink (m.number))
                    continue;
            }
            if (const auto* target = o["target"].getArray(); target != nullptr && target->size() == 4)
                m.target = { targetKindFromName ((*target)[0].toString()), (int) (*target)[1], (int) (*target)[2], (int) (*target)[3] };
            m.track = (int) o.getProperty ("track", -1);
            m.scene = (int) o.getProperty ("scene", -1);
            m.min = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("min", 0.0));
            m.max = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("max", 1.0));
            if ((m.source == RemoteMapping::Source::key && m.key == 0)
                || (m.action == RemoteAction::parameter && m.target.kind == AutomationTargetKind::none))
                continue;
            song.remoteMappings.push_back (m);
        }

    if (auto* values = root["initialValues"].getArray())
        for (const auto& v : *values)
            if (v.isArray() && v.size() == 5)
                song.setInitialValue ({ targetKindFromName (v[0].toString()), (int) v[1], (int) v[2], (int) v[3] }, (float) (double) v[4]);

    if (const auto& v = root["video"]; v.isObject())
    {
        if (const auto r = vtime::rateFromId (v["rate"].toString().toStdString()))
            song.video.rate = *r;
        song.video.startTimecode = juce::jlimit (0.0, 86400.0, (double) v.getProperty ("startTimecode", 0.0));
        song.video.rateFromVideo = (bool) v.getProperty ("rateFromVideo", true);
        song.video.showTimecodeRuler = (bool) v.getProperty ("timecodeRuler", true);
        song.video.burnIn = (bool) v.getProperty ("burnIn", false);
        if (auto* tracks = v["tracks"].getArray())
            for (const auto& t : *tracks)
            {
                if (! t.isObject() || song.videoTracks.size() >= 64)
                    continue;
                VideoTrack track;
                track.name = t["name"].toString().toStdString();
                track.muted = (bool) t.getProperty ("muted", false);
                track.height = juce::jlimit (0, 2000, (int) t.getProperty ("height", 0));
                if (const int c = (int) t.getProperty ("colour", 0); c != 0)
                    track.colour = 0xFF000000u | ((std::uint32_t) c & 0xFFFFFFu);
                if (auto* clips = t["clips"].getArray())
                    for (const auto& k : *clips)
                    {
                        if (! k.isObject())
                            continue;
                        VideoClip c;
                        c.path = k["path"].toString().toStdString();
                        c.start = juce::jmax (0.0, (double) k.getProperty ("start", 0.0));
                        c.offset = juce::jmax (0.0, (double) k.getProperty ("offset", 0.0));
                        c.length = juce::jmax (0.001, (double) k.getProperty ("length", 1.0));
                        c.duration = juce::jmax (0.0, (double) k.getProperty ("duration", 0.0));
                        c.uid = uids::fromHex (k["uid"].toString().toStdString());
                        if (! c.path.empty())
                            track.clips.push_back (c);
                    }
                std::stable_sort (track.clips.begin(), track.clips.end(), [] (const VideoClip& a, const VideoClip& b) { return a.start < b.start; });
                song.videoTracks.push_back (std::move (track));
            }
    }

    if (const auto& a = root["adr"]; a.isObject())
    {
        auto& d = song.adr;
        if (const auto& st = a["settings"]; st.isObject())
        {
            d.settings.preRoll = juce::jlimit (0.0, 30.0, (double) st.getProperty ("preRoll", 3.0));
            d.settings.postRoll = juce::jlimit (0.0, 30.0, (double) st.getProperty ("postRoll", 1.0));
            d.settings.beeps = (bool) st.getProperty ("beeps", true);
            d.settings.beepFrequency = juce::jlimit (100.0, 8000.0, (double) st.getProperty ("beepFrequency", 1000.0));
            d.settings.streamer = (bool) st.getProperty ("streamer", true);
            d.settings.streamerLength = juce::jlimit (0.25, 10.0, (double) st.getProperty ("streamerLength", 2.0));
            d.settings.streamerColour = 0xFF000000u | ((std::uint32_t) (int) st.getProperty ("streamerColour", 0xFFFFFF) & 0xFFFFFFu);
            d.settings.punch = (bool) st.getProperty ("punch", true);
            d.settings.cycle = (bool) st.getProperty ("cycle", false);
            d.settings.guideInsert = (int) st.getProperty ("guideInsert", -1);
            d.settings.headphoneOutput = (int) st.getProperty ("headphoneOutput", -1);
            d.settings.recordInsert = (int) st.getProperty ("recordInsert", -1);
            d.settings.takeTrack = (int) st.getProperty ("takeTrack", -1);
        }
        if (auto* cues = a["cues"].getArray())
            for (const auto& k : *cues)
            {
                if (! k.isObject() || d.cues.size() >= 10000)
                    continue;
                AdrCue c;
                c.id = k["id"].toString().toStdString();
                c.in = juce::jmax (0.0, (double) k.getProperty ("in", 0.0));
                c.out = juce::jmax (c.in, (double) k.getProperty ("out", c.in + 2.0));
                c.character = k["character"].toString().toStdString();
                c.line = k["line"].toString().toStdString();
                c.notes = k["notes"].toString().toStdString();
                c.status = adr::statusFromName (k["status"].toString().toStdString()).value_or (AdrCue::Status::todo);
                c.selected = (int) k.getProperty ("selected", 0);
                if (auto* takes = k["takes"].getArray())
                    for (const auto& t : *takes)
                        if (t.isObject())
                        {
                            AdrTake take;
                            take.number = juce::jmax (1, (int) t.getProperty ("number", 1));
                            take.name = t["name"].toString().toStdString();
                            take.clip = uids::fromHex (t["clip"].toString().toStdString());
                            take.rating = juce::jlimit (0, 5, (int) t.getProperty ("rating", 0));
                            take.circled = (bool) t.getProperty ("circled", false);
                            take.note = t["note"].toString().toStdString();
                            take.aligned = (bool) t.getProperty ("aligned", false);
                            c.takes.push_back (take);
                        }
                if (auto* comp = k["comp"].getArray())
                    for (const auto& p : *comp)
                        if (p.isArray() && p.size() == 3)
                            c.comp.push_back ({ (double) p[0], (double) p[1], (int) p[2] });
                if (auto* cc = k["compClips"].getArray())
                    for (const auto& u : *cc)
                        c.compClips.push_back (uids::fromHex (u.toString().toStdString()));
                d.cues.push_back (std::move (c));
            }
        d.current = juce::jlimit (0, juce::jmax (0, (int) d.cues.size() - 1), (int) a.getProperty ("current", 0));
    }

    if (auto* tracks = root["playlistTracks"].getArray())
        for (const auto& t : *tracks)
            if (t.isArray() && t.size() == 3 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                song.playlistTracks[index] = { t[1].toString().toStdString(), (bool) t[2] };
            }

    if (auto* heights = root["playlistTrackHeights"].getArray())
        for (const auto& t : *heights)
            if (t.isArray() && t.size() == 2 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                song.playlistTracks[index].height = juce::jlimit (0, 400, (int) t[1]);
            }

    if (auto* colours = root["playlistTrackColours"].getArray())
        for (const auto& t : *colours)
            if (t.isArray() && t.size() == 2 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                song.playlistTracks[index].colour = 0xFF000000u | ((std::uint32_t) (int) t[1] & 0xFFFFFFu);
            }

    if (auto* groups = root["playlistTrackGroups"].getArray())
        for (const auto& t : *groups)
            if (t.isArray() && t.size() == 3 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                song.playlistTracks[index].grouped = index > 0 && (int) t[1] != 0;
                song.playlistTracks[index].collapsed = (int) t[2] != 0;
            }

    if (auto* compTracks = root["playlistCompTracks"].getArray())
        for (const auto& t : *compTracks)
            if ((int) t >= 0 && (int) t < 500)
            {
                const auto index = (std::size_t) (int) t;
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                song.playlistTracks[index].compTrack = true;
            }

    if (auto* mods = root["modulators"].getArray()) // round 177b: LYNX's modulators
        for (const auto& v : *mods)
        {
            Modulator m;
            m.name = v["name"].toString().toStdString();
            m.shape = (ModShape) juce::jlimit (0, (int) ModShape::smoothRandom, (int) v.getProperty ("shape", 0));
            m.sync = (bool) v.getProperty ("sync", true);
            m.beats = juce::jlimit (1.0 / 64.0, 1024.0, (double) v.getProperty ("beats", 4.0));
            m.hz = juce::jlimit (0.001, 100.0, (double) v.getProperty ("hz", 1.0));
            m.phase = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("phase", 0.0));
            m.unipolar = (bool) v.getProperty ("unipolar", false);
            m.seed = (std::uint32_t) (juce::int64) v.getProperty ("seed", 1);
            if (auto* routes = v["routes"].getArray())
                for (const auto& r : *routes)
                    if (r.isArray() && r.size() >= 5)
                        m.routes.push_back ({ { targetKindFromName (r[0].toString()), (int) r[1], (int) r[2], (int) r[3] },
                                              juce::jlimit (-1.0f, 1.0f, (float) (double) r[4]) });
            song.modulators.push_back (std::move (m));
        }

    if (auto* lanes = root["playlistTrackLanes"].getArray()) // round 177b (after the automations: indices checked)
        for (const auto& t : *lanes)
            if (t.isArray() && t.size() >= 2 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                auto& track = song.playlistTracks[index];
                track.lanesFolded = (int) t[1] != 0;
                for (int k = 2; k < t.size(); ++k)
                    if (const auto& l = t[k]; l.isArray() && l.size() >= 1 && (int) l[0] >= 0 && (int) l[0] < (int) song.automations.size())
                        track.lanes.push_back ({ (int) l[0], l.size() >= 2 ? juce::jlimit (0, 2000, (int) l[1]) : 0 });
            }

    if (auto* locks = root["playlistTrackLocks"].getArray())
        for (const auto& t : *locks)
            if (t.isArray() && t.size() >= 3 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                song.playlistTracks[index].lockedToContent = (int) t[1] != 0;
                song.playlistTracks[index].lockedHeight = juce::jlimit (-1000, 1000, (int) t[2]);
            }

    if (auto* performance = root["playlistTrackPerformance"].getArray())
        for (const auto& t : *performance)
            if (t.isArray() && t.size() >= 7 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                auto& p = song.playlistTracks[index].performance;
                p.press = (TrackPerformance::Press) juce::jlimit (0, 3, (int) t[1]);
                p.motion = (TrackPerformance::Motion) juce::jlimit (0, 6, (int) t[2]);
                p.triggerSync = (TrackPerformance::Sync) juce::jlimit (0, 6, (int) t[3]);
                p.queue = (int) t[4] != 0;
                p.tolerant = (int) t[5] != 0;
                p.positionSync = (TrackPerformance::Sync) juce::jlimit (0, 6, (int) t[6]);
            }

    if (auto* icons = root["playlistTrackIcons"].getArray())
        for (const auto& t : *icons)
            if (t.isArray() && t.size() >= 2 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                song.playlistTracks[index].icon = juce::jlimit (0, 0xFFFF, (int) t[1]);
            }

    if (auto* foldedTracks = root["playlistTracksFolded"].getArray())
        for (const auto& t : *foldedTracks)
            if ((int) t >= 0 && (int) t < 500)
            {
                const auto index = (std::size_t) (int) t;
                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);
                song.playlistTracks[index].folded = true;
            }

    if (auto* arrangements = root["arrangements"].getArray(); arrangements != nullptr && ! arrangements->isEmpty())
    {
        song.arrangements.clear();

        for (const auto& a : *arrangements)
        {
            Arrangement arrangement;
            arrangement.name = a["name"].toString().toStdString();
            arrangement.clips = clipsFromVar (a["clips"], song);
            keepOldClipFades (arrangement.clips);
            arrangement.blocks = blocksFromVar (a["blocks"], song);

            if (const auto& loop = a["loop"]; loop.isArray() && loop.size() == 2)
            {
                arrangement.loopStart = juce::jmax<juce::int64> (0, (juce::int64) loop[0]);
                arrangement.loopEnd = juce::jmax<juce::int64> (arrangement.loopStart, (juce::int64) loop[1]);
            }

            if (auto* markers = a["markers"].getArray())
                for (const auto& m : *markers)
                    if (m.isArray() && (m.size() == 4 || m.size() == 5))
                        arrangement.markers.push_back ({ juce::jmax<juce::int64> (0, (juce::int64) m[0]), m[1].toString().toStdString(),
                                                         juce::jlimit (0, 32, (int) m[2]), juce::jlimit (1, 32, (int) m[3]),
                                                         m.size() == 5 ? juce::jlimit (0, 255, (int) m[4]) : 0 });

            std::stable_sort (arrangement.markers.begin(), arrangement.markers.end(), [] (const Marker& x, const Marker& y) { return x.tick < y.tick; });
            song.arrangements.push_back (std::move (arrangement));
        }

        song.currentArrangement = juce::jlimit (0, (int) song.arrangements.size() - 1, (int) root.getProperty ("currentArrangement", 0));
        song.arrangements[(std::size_t) song.currentArrangement].clips.clear();
        song.arrangements[(std::size_t) song.currentArrangement].blocks.clear();
    }

    song.mode = root["mode"].toString() == "song" ? PlayMode::song : PlayMode::pattern;

    if (const auto& loop = root["loop"]; loop.isArray() && loop.size() == 2)
    {
        song.loopStart = juce::jmax<juce::int64> (0, (juce::int64) loop[0]);
        song.loopEnd = juce::jmax<juce::int64> (song.loopStart, (juce::int64) loop[1]);
    }
    song.loopOff = (bool) root.getProperty ("loopOff", false);
    if (const auto& view = root["tempoView"]; view.isArray() && view.size() == 2 && (double) view[1] > (double) view[0])
    {
        song.tempoViewLow = juce::jlimit (minTempo, maxTempo, (double) view[0]);
        song.tempoViewHigh = juce::jlimit (song.tempoViewLow + 1.0, maxTempo, (double) view[1]);
    }

    if (auto* mixer = root["mixer"].getArray())
        for (const auto& t : *mixer)
        {
            if ((int) song.mixer.size() >= maxMixerTracks)
                break;

            auto track = mixerTrackFromVar (t);
            song.mixer.push_back (std::move (track));
        }

    if (song.mixer.empty()) // projects from before the mixer existed
        song.mixer = Song::defaultMixer();

    // a channel routed past the saved mixer's last insert (round 162): the inserts up to it exist, so it is not mixed
    // into the last one
    for (const auto& c : song.channels)
        if (c.mixerTrack >= (int) song.mixer.size())
            mixerops::ensureInsert (song, c.mixerTrack);

    // track modes (round 146a), after the mixer: an audio track's insert is checked against it (issue #122: read
    // before it, every audio track fell back to Unassigned on reopen)
    if (auto* modes = root["playlistTrackModes"].getArray())
        for (const auto& t : *modes)
            if (t.isArray() && t.size() == 4 && (int) t[0] >= 0 && (int) t[0] < 500)
            {
                const auto index = (std::size_t) (int) t[0];
                const auto mixer = (int) t[2], channel = (int) t[3];
                const auto audio = t[1].toString() == "audio";
                if (audio ? (mixer <= 0 || mixer >= (int) song.mixer.size()) : (channel < 0 || channel >= (int) song.channels.size()))
                    continue;

                if (song.playlistTracks.size() <= index)
                    song.playlistTracks.resize (index + 1);

                auto& track = song.playlistTracks[index];
                track.mode = audio ? PlaylistTrackMode::audio : PlaylistTrackMode::instrument;
                track.mixerTrack = mixer > 0 && mixer < (int) song.mixer.size() ? mixer : -1;
                track.channel = audio ? -1 : channel;
            }

    // audio clips that point at a Sampler channel (a file dropped on an audio clip channel's row made one before round
    // 190ax; hand-written projects): the clip showed its waveform and played nothing
    audioclips::repairChannels (song);

    // sends to tracks that don't exist are discarded
    for (auto& t : song.mixer)
        t.sends.erase (std::remove_if (t.sends.begin(), t.sends.end(),
                                       [n = (int) song.mixer.size()] (const MixerSend& s) { return s.target <= 0 || s.target >= n; }),
                       t.sends.end());

    readSendSpans (song, root); // round 167a: automations / lanes of Fruity Send's "Send to"
    song.currentPattern = juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern);

    // Online Sessions (round 168a)
    song.projectId = root["projectId"].toString().toStdString();
    song.nextUid = uidFrom (root.getProperty ("nextUid", {}));
    if (auto* ids = root["arrangementUids"].getArray(); ids != nullptr && ids->size() == (int) song.arrangements.size())
        for (int i = 0; i < ids->size(); ++i)
            song.arrangements[(std::size_t) i].uid = uidFrom ((*ids)[i]);
    if (const auto& block = root["session"]; block.isObject())
    {
        song.session.me = (std::uint32_t) juce::jlimit (1, 0xFFFFFF, (int) block.getProperty ("me", 1));
        if (auto* people = block["participants"].getArray())
            for (const auto& o : *people)
            {
                if ((int) song.session.participants.size() >= session::maxParticipants)
                    break;
                SessionParticipant p;
                p.tag = (std::uint32_t) juce::jlimit (1, 0xFFFFFF, (int) o.getProperty ("tag", 1));
                p.name = o["name"].toString().toStdString();
                p.colour = 0xFF000000u | (std::uint32_t) o["colour"].toString().getHexValue32();
                if (const auto& r = o["inserts"]; r.isArray() && r.size() == 2)
                    p.insertFirst = juce::jlimit (0, maxMixerTracks, (int) r[0]), p.insertCount = juce::jlimit (0, maxMixerTracks, (int) r[1]);
                if (const auto& r = o["rows"]; r.isArray() && r.size() == 2)
                    p.rowFirst = juce::jmax (0, (int) r[0]), p.rowCount = juce::jlimit (0, 100000, (int) r[1]);
                p.keepFull = (bool) o.getProperty ("keepFull", false);
                if (song.session.find (p.tag) == nullptr)
                    song.session.participants.push_back (std::move (p));
            }
    }
    return song;
}

bool save (const Song& song, const juce::File& file, juce::String& error)
{
    // Write to a temp file and swap, so a mid-write error doesn't destroy the previous project.
    juce::TemporaryFile temp (file);
    const auto json = toJson (song);

    if (! temp.getFile().replaceWithText (json, false, false, nullptr) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = utf8 ("could not write ") + file.getFullPathName();
        return false;
    }

    // a save that "worked" on a full disk, a sync folder or a failing drive must not look like a save (D#92, round 180a)
    if (file.loadFileAsString() != json)
    {
        error = utf8 ("the saved file could not be read back correctly: ") + file.getFullPathName();
        return false;
    }

    return true;
}

juce::File withProjectExtension (const juce::File& file)
{
    return file.getSiblingFile (juce::String::fromUTF8 (safety::withProjectExtension (file.getFileName().toStdString(), extension).c_str()));
}

std::optional<Song> load (const juce::File& file, juce::String& error, juce::String* savedBy)
{
    if (! file.existsAsFile())
    {
        error = utf8 ("file not found");
        return std::nullopt;
    }

    auto song = fromJson (file.loadFileAsString(), error, savedBy);

    // samples saved relative to the project (a package) or moved along with it are found next to it (REA round 118)
    if (song)
        safety::resolveProjectSamples (*song, file.getParentDirectory().getFullPathName().toStdString(), [] (const std::string& path) {
            return juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())) && juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile();
        });

    return song;
}

juce::String mixerTrackStateToJson (const MixerTrack& track)
{
    // the track's own settings and effects; routing, audio input, mute and solo belong to the project
    auto state = track;
    state.sends.clear();
    state.lostSends.clear();
    state.toMaster = true;
    state.masterLevel = 1.0f;
    state.inputChannel = -1;
    state.outputChannel = outputDefault;
    state.outputMode = OutputMode::stereo;
    state.monitor = InputMonitor::automatic;
    state.muted = state.solo = false;
    state.standIns.clear(); // (renders of this project's parts)
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "lynx-mixer-track");
    root->setProperty ("version", 1);
    root->setProperty ("track", mixerTrackToVar (state));
    return juce::JSON::toString (juce::var (root.release()));
}

std::optional<MixerTrack> mixerTrackStateFromJson (const juce::String& json, juce::String& error)
{
    const auto root = juce::JSON::parse (json);
    if (root["format"].toString() != "lynx-mixer-track" || ! root["track"].isObject())
    {
        error = utf8 ("not a LYNX SHARE mixer track state");
        return std::nullopt;
    }
    return mixerTrackFromVar (root["track"]);
}

void applyMixerTrackState (MixerTrack& target, const MixerTrack& state, bool takeName)
{
    // FL's "Open mixer track state": the effects and the track's settings; the routing stays
    if (takeName && ! state.name.empty())
        target.name = state.name;
    if (takeName || state.colour != 0)
        target.colour = state.colour;
    target.volume = state.volume;
    target.pan = state.pan;
    target.stereo = state.stereo;
    target.invertPhase = state.invertPhase;
    target.swapChannels = state.swapChannels;
    target.eq = state.eq;
    target.threaded = state.threaded;
    target.effects = state.effects;
    target.chainBlend = state.chainBlend; // (round 190bu: a state is the whole chain, its Blend and its preset name too)
    target.chainPreset = state.chainPreset;
}
} // namespace lynx::ProjectFile
