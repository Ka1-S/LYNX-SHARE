#include "core/Crossfader.h"
#include "core/RackRenderer.h"
#include "core/Vocodex.h"
#include "core/SytrusEngine.h"
#include "core/Harmor.h"
#include "core/TrackStates.h"
#include "core/LynxVerbs.h"
#include "core/PolyEdit.h"
#include "core/Dropouts.h"
#include "core/HostTimelineScope.h"
#include "core/Rack.h"
#include "core/SessionMixer.h"
#include "core/MixerOps.h"
#include "core/FlSettings.h"

#include "core/FpcInstrument.h"
#include "core/FruitySlicer.h"
#include "core/SlicexInstrument.h"
#include "core/TempoMap.h"
#include "core/ModelParams.h"
#include "core/SynthParams.h"
#include "core/FlmDwSampler.h"
#include "core/FlmSlicer.h"
#include "core/SamplePrecompute.h"
#include "core/ThreeOsc.h" // round 190fe
#include "core/TimeStretch.h"
#include "core/MetronomeVoice.h"

#include "core/ChannelLoops.h"
#include "core/PatternBlocks.h"

#include "core/ControlSurface.h"
#include "core/VoltageController.h"
#include "core/FlSynths.h"
#include "core/HardwareInsert.h"
#include "core/Layer.h"
#include "core/RackOps.h"
#include "core/HostSlide.h"
#include "core/Vfx.h"
#include "core/FacetEq.h"
#include "core/GoalKnobs.h"
#include "core/MidiControls.h"
#include "core/MidiOut.h"
#include "core/Patcher.h"

#include <optional>
#include "core/LoadTrace.h"
#include "core/DynamicsSuite.h"
#include "core/MixSuite.h"
#include "core/VoiceTools.h"

#include "core/WorkerPool.h"
#include "core/Denormals.h"
#include "core/FlKeys.h"
#include "core/FlVoiceProcessor.h"
#include "core/DirectWave.h"
#include "core/SoundFont.h"
#include "core/NewtoneFx.h"
#include "core/EdisonFx.h"
#include "core/Dvs.h"
#include "core/Scratcher.h"

#include <algorithm>
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <tuple>
#include <mutex>
#include <thread>

namespace lynx
{
namespace tempofollow
{
namespace
{
std::atomic<bool> on { true };
}
bool enabled() noexcept { return on.load (std::memory_order_relaxed); }
void setEnabled (bool yes) noexcept { on.store (yes, std::memory_order_relaxed); }
} // namespace tempofollow

namespace
{
// a hosted plugin is created again when its processing options change (fixed blocks, own process) or the options it
// gets at creation (HostedOptions: the wrapper's MIDI and reset options, which did nothing until a reload otherwise)
bool samePluginProcessing (const PluginRef& a, const PluginRef& b) noexcept
{
    return a.fixedBuffers == b.fixedBuffers && a.separateProcess == b.separateProcess && a.maxSizeBuffers == b.maxSizeBuffers
           && a.hostMaxBuffer == b.hostMaxBuffer && a.bitSize == b.bitSize && a.releaseVelocity == b.releaseVelocity
           && a.sendBendRange == b.sendBendRange && a.bendRange == b.bendRange && a.modXAftertouch == b.modXAftertouch
           && a.allNotesOffOnStop == b.allNotesOffOnStop && a.resetOnFlush == b.resetOnFlush && a.sendLoopPosition == b.sendLoopPosition
           && a.mpe == b.mpe && a.dpiAware == b.dpiAware; // (FL: "DPI aware (requires reloading the plugin)", round 190oh)
}

// A hosted plugin's settings live in its running instance: the song only holds what was saved, and the instance is
// given that once, when it is created. So the running instance stays with its own channel / slot whatever the song
// says (an edit, an undo back to an older save: the instance tag is the same), but a channel / slot that merely sits
// where another one was (another project opened over this one, a session joined, a channel replaced) keeps the
// plugin running there only when it was saved with the same settings. Without this, a project opened over one with
// the same plugin in the same place played, and then saved, the first project's settings (round 190az).
template <typename T>
bool sameHostedPlugin (const T& before, const T& now) noexcept
{
    return before.instance.id == now.instance.id || before.plugin.state == now.plugin.state;
}

// Silent instrument: plugin channel that could not be loaded.
class SilentInstrument final : public Instrument
{
public:
    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int, float, float) noexcept override {}
};

// FL Keys: the data FL builds from a wavetable's files, shared by every channel that plays the same wavetable
// (Grand Piano holds 40 MB of 16-bit samples)
std::shared_ptr<const flkeys::Wavetable> flKeysWavetable (const ChannelDef& channel, const RackState::SampleProvider& samples)
{
    const auto table = flkeys::FlKeysTable::decode (channel.modelParams);
    if (! table || ! samples || table->files.size() != channel.zones.size())
        return nullptr;
    std::string key;
    for (const auto& z : channel.zones)
        key += z.path + '\n';
    for (auto v : channel.modelParams)
        key += std::to_string (v) + ' ';
    static std::mutex lock;
    static std::map<std::string, std::weak_ptr<const flkeys::Wavetable>> cache;
    {
        std::lock_guard<std::mutex> guard (lock);
        if (auto found = cache[key].lock())
            return found;
    }
    std::vector<flkeys::FileSamples> files;
    for (std::size_t i = 0; i < channel.zones.size(); ++i)
    {
        const auto sample = samples (channel.zones[i].path);
        if (sample == nullptr || sample->frames() == 0)
            return nullptr;
        files.push_back (flkeys::toFileSamples (*sample, table->files[i].scale));
    }
    auto built = flkeys::buildWavetable (*table, files);
    std::lock_guard<std::mutex> guard (lock);
    cache[key] = built;
    return built;
}

// Effect that does nothing: missing plugin slot.
class BypassEffect final : public Effect
{
public:
    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    void process (float*, float*, int, const EffectParams&, double) noexcept override {}
};
} // namespace

LiveLevels::LiveLevels (const Song& song)
    : mixerVolume (song.mixer.size()), mixerPan (song.mixer.size()), mixerStereo (song.mixer.size()),
      channelVolume (song.channels.size()), channelPan (song.channels.size()), vcaLevel (song.vcas.size())
{
    store (song);
}

bool LiveLevels::matches (const Song& song) const noexcept
{
    return mixerVolume.size() == song.mixer.size() && channelVolume.size() == song.channels.size() && vcaLevel.size() == song.vcas.size();
}

void LiveLevels::store (const Song& song) noexcept
{
    if (! matches (song))
        return;
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        mixerVolume[t].store (song.mixer[t].volume, std::memory_order_relaxed);
        mixerPan[t].store (song.mixer[t].pan, std::memory_order_relaxed);
        mixerStereo[t].store (song.mixer[t].stereo, std::memory_order_relaxed);
    }
    for (std::size_t c = 0; c < song.channels.size(); ++c)
    {
        channelVolume[c].store (song.channels[c].volume, std::memory_order_relaxed);
        channelPan[c].store (song.channels[c].pan, std::memory_order_relaxed);
    }
    for (std::size_t v = 0; v < song.vcas.size(); ++v)
        vcaLevel[v].store (song.vcas[v].level, std::memory_order_relaxed);
    crossfader.store (song.crossfader, std::memory_order_relaxed);
}

std::unique_ptr<const RackState> RackState::build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples)
{
    Factories factories;
    factories.samples = samples;
    return build (std::move (song), previous, sampleRate, factories);
}

namespace
{
// A channel's instrument, built new (UI thread)
// deferKeyShift (round 165i): the pitch shifters of a keep-pitch sampler are made later by the caller, all channels at
// once in parallel (Thief: 29 such channels × 8 shifters took most of the 860 ms of rack_instruments, one by one)
std::shared_ptr<Instrument> buildInstrument (const ChannelDef& channel, double sampleRate, double bpm, const RackState::Factories& factories,
                                             std::vector<Instrument*>* deferKeyShift = nullptr)
{
    const auto& samples = factories.samples;
    const auto kind = channel.instrument;

    if (kind == InstrumentKind::plugin)
    {
        std::shared_ptr<Instrument> instrument;

        if (factories.pluginInstruments && ! channel.plugin.id.empty())
            instrument = factories.pluginInstruments (channel.plugin, sampleRate);
        if (instrument != nullptr)
            instrument->configure (channel, bpm); // (its outputs' routes, wrapper Processing › Connections, round 188d)

        return instrument != nullptr ? instrument : std::make_shared<SilentInstrument>();
    }

    // FL Keys: its wavetable built from the channel's files
    if (kind == InstrumentKind::flkeys)
    {
        std::shared_ptr<Instrument> instrument = flkeys::makeFlKeysInstrument (flKeysWavetable (channel, samples));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    // DirectWave (REA_rodada72): the zones' samples as the plugin holds them
    if (kind == InstrumentKind::directwave)
    {
        const auto formats = dw::decodeFormats (channel.modelParams);
        std::vector<std::shared_ptr<const dw::ZoneSamples>> zs;
        for (std::size_t z = 0; z < channel.zones.size(); ++z)
        {
            std::shared_ptr<const dw::ZoneSamples> s;
            if (samples && ! channel.zones[z].path.empty())
                if (const auto data = samples (channel.zones[z].path))
                    s = dw::zoneSamplesShared (channel.zones[z].path, data, z < formats.size() && formats[z]); // (round qafix190kq: shared between rack builds)
            zs.push_back (std::move (s));
        }
        std::shared_ptr<Instrument> instrument = dw::makeDirectWaveInstrument (std::move (zs));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    // LYNX Soundfont (REA_rodada158): the font is loaded on a background thread and shared by the channels playing it
    if (kind == InstrumentKind::soundfont)
    {
        std::shared_ptr<Instrument> instrument = sf::makeInstrument (channel.samplePath.empty() ? nullptr : sf::FontLibrary::instance().get (channel.samplePath));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    // Slicex (round 190l): FL's engine on the two decks' samples (deck A = the channel's sample, deck B = its one zone)
    if (kind == InstrumentKind::slicex)
    {
        std::shared_ptr<const SampleData> a, b;
        if (samples && ! channel.samplePath.empty())
            a = samples (channel.samplePath);
        if (samples && ! slicex::deckBPath (channel).empty())
            b = samples (slicex::deckBPath (channel));
        std::shared_ptr<Instrument> instrument = makeSlicexInstrument (std::move (a), std::move (b));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        instrument->settleNew();
        return instrument;
    }

    // FPC (round 190q): a zoned sampler per pad, as the channel-per-pad import builds them (no settleNew(), like those)
    if (kind == InstrumentKind::fpc)
    {
        const loadtrace::Scope timing ("rack_zoned");
        std::shared_ptr<Instrument> instrument = makeFpcInstrument (channel, samples);
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    // FL Studio Mobile's Slicer (round 190de, core/FlmSlicer.h): FLM's slices of the decoded sample, FLM's voice
    if (kind == InstrumentKind::sampler && flmslicer::isChannel (channel))
    {
        std::shared_ptr<Instrument> instrument = flmslicer::makeInstrument (samples && ! channel.samplePath.empty() ? samples (channel.samplePath) : nullptr);
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        instrument->settleNew();
        return instrument;
    }

    // FL Studio Mobile's DW Sampler (round 190de, core/FlmDwSampler.h): FLM's own voice on the channel's zones
    if (kind == InstrumentKind::sampler && flmdw::isChannel (channel))
    {
        std::vector<flmdw::EngineZone> zones;
        for (const auto& z : channel.zones)
            zones.push_back ({ z, true, samples && ! z.path.empty() ? samples (z.path) : nullptr });
        std::shared_ptr<Instrument> instrument = flmdw::makeInstrument (std::move (zones));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    // multi-sample sampler: one zone per sample (DirectWave, FPC layers...)
    if (kind == InstrumentKind::sampler && ! channel.zones.empty())
    {
        const loadtrace::Scope timing ("rack_zoned");
        std::vector<LoadedZone> zones;

        for (const auto& z : channel.zones)
            zones.push_back ({ z, samples ? samples (z.path) : nullptr });

        std::shared_ptr<Instrument> instrument = makeZonedSampler (std::move (zones));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        return instrument;
    }

    std::shared_ptr<const SampleData> sample;

    if ((kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip || kind == InstrumentKind::granulizer
         || kind == InstrumentKind::wavetraveller || kind == InstrumentKind::fruityslicer) && samples
        && ! channel.samplePath.empty())
        sample = samples (channel.samplePath);

    // round 190lk (core/PolyEdit.h): an audio clip whose notes were edited in the polyphonic editor plays the edited
    // render of its sample (made once per edit state, cached; a channel without edits takes none of this)
    const auto polyKey = kind == InstrumentKind::audioClip && sample != nullptr ? polyedit::stateKey (channel.polyEdit) : std::string();
    if (! polyKey.empty())
        sample = polyedit::renderedFor (channel.samplePath, sample, channel.polyEdit);

    // FL's Sampler › Precomputed effects (REA_rodada167h §2): the played region, processed once
    // Audio clips too (round 167k, FL render r167k/fl/clip/p_*: an audio clip channel's Normalize, Reverse polarity,
    // Fade in / out play in FL); a clip plays the whole file, so the region stays whole there
    const auto clip = kind == InstrumentKind::audioClip;
    auto spanSettings = channel.sampler;
    if (clip)
        spanSettings.regionStart = 0.0f, spanSettings.regionEnd = 1.0f;
    const auto precomputes = sample != nullptr
                             && ((kind == InstrumentKind::sampler && precompute::needed (channel.sampler))
                                 || (clip && channel.sampler.precompute.any()));
    auto precomputed = false;

    // pitch-preserving stretch: the sample comes already stretched to the current tempo
    if (sample != nullptr && kind != InstrumentKind::granulizer && kind != InstrumentKind::wavetraveller
        && kind != InstrumentKind::fruityslicer && channel.sampler.keepPitch && channel.sampler.stretchBeats > 0.0f
        && (factories.stretchedSamples || factories.stretchedSpans)
        && sample->frames() > 0)
    {
        const auto target = channel.sampler.stretchBeats * 60.0 / std::max (1.0, bpm);
        if (precomputes && channel.sampler.warp.empty())
        {
            // FL stretches what plays: the region after its precomputed effects and Trim, not the whole file (round 180d:
            // Thief's Importer Ride, 1.30 s of file faded out to 0.38 s, plays 2x slower in FL at TIME 367). The span is cut
            // first and only it is stretched, so the stretcher's smear of the silence around it is not kept by the trim.
            if (std::shared_ptr<const SampleData> span = precompute::apply (*sample, spanSettings); span != nullptr && span->frames() > 0)
            {
                const auto ratio = target / ((double) span->frames() / span->sampleRate);
                auto stretched = factories.stretchedSpans
                                     ? factories.stretchedSpans (channel.samplePath, precompute::variantKey (spanSettings, clip) + polyKey, span, ratio,
                                                                 channel.sampler.stretchMethod, channel.sampler.stretchPitch, stretchOptionsOf (channel.sampler))
                                     : std::shared_ptr<const SampleData> (timeStretchSpan (*span, ratio, channel.sampler.stretchMethod,
                                                                                           channel.sampler.stretchPitch, stretchOptionsOf (channel.sampler)));
                sample = stretched != nullptr ? std::move (stretched) : std::move (span);
                precomputed = true;
            }
        }
        else if (factories.stretchedSamples)
        {
            // the whole file (no precomputed effects; or a warp map, whose markers are in the file's frames)
            const auto ratio = target / ((double) precompute::stretchedFrames (sample, channel.sampler, clip) / sample->sampleRate);
            const auto map = warpStretchMap (channel.sampler.warp, (double) sample->frames(), sample->sampleRate,
                                             (double) std::llround ((double) sample->frames() * ratio));
            // round 190lk: the polyphonic editor's render (not the file) is what stretches (a warp map's clip plays the
            // file's stretch without the note edits: the span cache takes no warp map)
            if (! polyKey.empty() && channel.sampler.warp.empty() && factories.stretchedSpans)
            {
                if (auto stretched = factories.stretchedSpans (channel.samplePath, polyKey, sample, ratio, channel.sampler.stretchMethod,
                                                               channel.sampler.stretchPitch, stretchOptionsOf (channel.sampler)))
                    sample = std::move (stretched);
            }
            else if (auto stretched = factories.stretchedSamples (channel.samplePath, ratio, channel.sampler.stretchMethod, channel.sampler.stretchPitch, map,
                                                         stretchOptionsOf (channel.sampler)))
                sample = std::move (stretched);
        }
    }

    if (precomputes && ! precomputed)
        if (auto processed = precompute::apply (*sample, spanSettings))
            sample = std::move (processed);

    // Fruity Slicer (round 180c): FL's edits to the sample (reverse, declick, fades), then the sampler on the slices
    if (kind == InstrumentKind::fruityslicer)
    {
        if (const auto state = fslicer::parse (channel.pluginState); state && sample != nullptr)
            sample = fslicer::prepare (*sample, *state);
        // (the instrument takes its slices, key table and voice from the channel's state, at every configure)
        std::shared_ptr<Instrument> instrument = makeInstrument (kind, std::move (sample));
        instrument->prepare (sampleRate);
        instrument->configure (channel, bpm);
        instrument->settleNew();
        return instrument;
    }

    const loadtrace::Scope timing ("rack_instruments");
    std::shared_ptr<Instrument> instrument = makeInstrument (kind, std::move (sample));
    instrument->prepare (sampleRate);
    instrument->configure (channel, bpm);
    instrument->settleNew();
    if ((kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip) && channel.sampler.keepPitch)
    {
        if (deferKeyShift != nullptr)
            deferKeyShift->push_back (instrument.get());
        else
            instrument->prepareKeyShift (sampleRate); // transpositions keep the length (rounds 162, 163)
    }
    return instrument;
}

// A slot's effect, built new (UI thread)
std::shared_ptr<Effect> buildEffect (const EffectSlot& slot, double sampleRate, const RackState::Factories& factories)
{
    const auto kind = slot.kind;
    std::shared_ptr<Effect> effect;

    if (kind == EffectKind::plugin)
    {
        if (factories.pluginEffects && ! slot.plugin.id.empty())
            effect = factories.pluginEffects (slot.plugin, sampleRate);

        if (effect == nullptr)
            effect = std::make_shared<BypassEffect>();
    }
    else if ((kind == EffectKind::newtone || kind == EffectKind::newtime))
    {
        // Newtone / Newtime: their source sample (loaded off the audio thread), REA_rodada73
        // (Newtone: its note edits rendered by LYNX's engine, REA_rodada95)
        effect = newtonefx::make (factories.samples && ! slot.samplePath.empty() ? factories.samples (slot.samplePath) : nullptr,
                                  slot.state);
        effect->prepare (sampleRate);
    }
    else if (kind == EffectKind::edison)
    {
        // Edison (round 190cd): the slot's own audio, found again by the slot's instance (core/EdisonFx.h)
        effect = edisonfx::make (slot, sampleRate, factories.samples);
    }
    else if (kind == EffectKind::scratcher)
    {
        // Fruity Scratcher: its turntable's sample, loaded off the audio thread (REA_rodada165g-flscratch165)
        effect = scratcher::make (factories.samples && ! slot.samplePath.empty() ? factories.samples (slot.samplePath) : nullptr);
        effect->prepare (sampleRate);
        // its DVS position table (REA_rodada167c §1), built here so the audio thread finds it ready
        dvs::warm (scratcher::timecodeOf (slot.params[scratcher::timecodeType]), true);
    }
    else
    {
        const loadtrace::Scope timing ("rack_builtin_fx"); // (round 112: what the open spends on LYNX's own effects)
        effect = makeEffect (slot);
        effect->prepare (sampleRate);
    }

    return effect;
}

// Issue #134: hosted plugins are prepared when they are made, for the rate they are made at. A Patcher built again
// for another sample rate (the audio device changed) keeps the running nodes of its old graph, and LYNX's own units
// follow the new rate, but a hosted plugin kept that way went on running at the old one (out of tune, its delays and
// envelopes off). A Patcher that holds one, nested ones included, is built from nothing at a new rate.
bool hostsPlugins (const PatcherDoc& doc) noexcept
{
    for (const auto& n : doc.nodes)
        if (n.generator ? n.channel.instrument == InstrumentKind::plugin
                              || (n.channel.instrument == InstrumentKind::patcher && hostsPlugins (n.channel.patcher))
                        : n.effect.kind == EffectKind::plugin || (n.effect.kind == EffectKind::patcher && hostsPlugins (n.effect.patcher)))
            return true;
    return false;
}

// round 190ap: a plugin the build must create anew (RackState::Factories::rebuildPlugin), alone or inside a Patcher
bool rebuilds (const RackState::Factories& factories, const PluginRef& ref) { return factories.rebuildPlugin && factories.rebuildPlugin (ref); }
bool rebuildsInside (const RackState::Factories& factories, const PatcherDoc& doc)
{
    if (! factories.rebuildPlugin)
        return false;
    for (const auto& n : doc.nodes)
        if (n.generator ? (n.channel.instrument == InstrumentKind::plugin && rebuilds (factories, n.channel.plugin))
                              || (n.channel.instrument == InstrumentKind::patcher && rebuildsInside (factories, n.channel.patcher))
                        : (n.effect.kind == EffectKind::plugin && rebuilds (factories, n.effect.plugin))
                              || (n.effect.kind == EffectKind::patcher && rebuildsInside (factories, n.effect.patcher)))
            return true;
    return false;
}

// What a Patcher builds its nodes with: the same builders as the rack's channels and slots
patcher::Factories patcherFactories (double sampleRate, double bpm, const RackState::Factories& factories)
{
    patcher::Factories f;
    f.effect = [sampleRate, bpm, factories] (const EffectSlot& slot) -> std::shared_ptr<Effect> {
        if (slot.kind == EffectKind::patcher)
        {
            std::shared_ptr<Effect> e = patcher::makeEffect (slot, sampleRate, patcherFactories (sampleRate, bpm, factories));
            e->prepare (sampleRate);
            return e;
        }
        return buildEffect (slot, sampleRate, factories);
    };
    f.instrument = [sampleRate, bpm, factories] (const ChannelDef& channel) -> std::shared_ptr<Instrument> {
        if (channel.instrument == InstrumentKind::patcher)
        {
            std::shared_ptr<Instrument> i = patcher::makeInstrument (channel, sampleRate, patcherFactories (sampleRate, bpm, factories));
            i->configure (channel, bpm);
            return i;
        }
        return buildInstrument (channel, sampleRate, bpm, factories);
    };
    return f;
}

// an audio clip channel that resamples but has a clip of its own pitch (FL 2026's keys 7/8): it needs the shifters
bool clipShiftWanted (const Song& song, std::size_t channel)
{
    if (channel >= song.channels.size() || song.channels[channel].instrument != InstrumentKind::audioClip
        || song.channels[channel].sampler.keepPitch)
        return false;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::audio && c.channel == (int) channel && c.pitch != 0.0f)
            return true;
    return false;
}

// Round 190gn (A1, memory): how many voices of channel `c` can sound at once — a bound for the pitch shifters it can
// use (at most 8, ShifterStock). Every note of the channel the song can play (each pattern clip's notes along the clip,
// repeated or not; muted clips, tracks and notes too, as they can be switched on while playing; each pattern looped on
// its own, as in pattern mode; the notes of a Layer that has the channel as a child; the channel's echo repeats; with a
// VFX note processor in the song, all 8) lasts
// at most `voiceSeconds` (the instrument's longestVoiceSeconds: its sample played once to the end), an audio clip of
// the channel at most its length and the end declick; counted with a quarter beat of slack each side (time shift,
// swing; plus the channel's Time › Shift) at the fastest tempo of the song. The most of these intervals that overlap is the bound; a channel whose notes
// can come from elsewhere (an arpeggiator) or whose voices have no end (loops, zones: +inf) keeps all 8. What a live
// keyboard adds comes from the stock's live reserve.
// The song as the analysis reads it (once per build): Pattern Blocks expanded to their clips, and the fastest tempo
struct VoicePlan
{
    std::optional<Song> expanded; // (empty: the song itself)
    double ticksPerSecond = 0.0; // at the fastest tempo; 0 = unknown (a pattern's own tempo lane)
    // (round perfgate190id) each pattern's notes by channel: (channel, note index) sorted, for the played song's
    // patterns and, when Pattern Blocks expanded it, the song's own. Each keep-pitch channel then reads only its own
    // notes instead of every note of every clip's pattern (Exoplanet: 99 of an edit snapshot's 161 ms)
    std::vector<std::vector<std::pair<int, std::uint32_t>>> playedNotes, songNotes;
};

std::vector<std::vector<std::pair<int, std::uint32_t>>> notesByChannel (const std::vector<Pattern>& patterns)
{
    std::vector<std::vector<std::pair<int, std::uint32_t>>> index (patterns.size());
    for (std::size_t p = 0; p < patterns.size(); ++p)
    {
        auto& list = index[p];
        list.reserve (patterns[p].notes.size());
        for (std::size_t k = 0; k < patterns[p].notes.size(); ++k)
            list.push_back ({ patterns[p].notes[k].channel, (std::uint32_t) k });
        std::sort (list.begin(), list.end());
    }
    return index;
}

VoicePlan voicePlan (const Song& song)
{
    VoicePlan plan;
    if (patternblocks::active (song))
        patternblocks::expandSong (plan.expanded.emplace (song));
    auto bpm = std::max (1.0, song.bpm);
    if (tempomap::active (song))
    {
        if (song.mode != PlayMode::song)
            return plan;
        const auto length = (double) std::max<std::int64_t> (ppq, song.songLength());
        const auto clips = tempomap::tempoClips (song); // (round perfgate190id: Exoplanet's 2,578 clips read 7,000 times)
        for (double t = 0.0; t <= length; t += ppq / 16.0)
            bpm = std::max (bpm, tempomap::tempoAtIn (song, clips, t));
        bpm *= 1.01;
    }
    plan.ticksPerSecond = bpm * ppq / 60.0;
    plan.songNotes = notesByChannel (song.patterns);
    if (plan.expanded)
        plan.playedNotes = notesByChannel (plan.expanded->patterns);
    return plan;
}

int voicesAtOnce (const Song& song, const VoicePlan& plan, std::size_t c, double voiceSeconds)
{
    constexpr int all = 8;
    if (c >= song.channels.size() || ! std::isfinite (voiceSeconds) || song.channels[c].arp.enabled() || plan.ticksPerSecond <= 0.0)
        return all;
    const auto& channel = song.channels[c];
    const auto ticksPerSecond = plan.ticksPerSecond;
    // (+ the channel's Time › Shift, which moves every note later)
    const auto slack = (double) ppq / 4.0 + std::max (0.0, (double) song.channels[c].misc.shiftBeats * ppq);
    const auto voiceTicks = voiceSeconds * ticksPerSecond;

    // VFX note processors send notes to other channels by port (core/Vfx.h): not followed here, 8
    for (const auto& other : song.channels)
        if (other.instrument == InstrumentKind::vfx)
            return all;

    // the channels whose notes play this one: itself, and every Layer (in turn) that has it as a child
    std::vector<char> plays (song.channels.size(), 0);
    plays[c] = 1;
    for (bool grew = true; grew;)
    {
        grew = false;
        for (std::size_t l = 0; l < song.channels.size(); ++l)
            if (! plays[l] && song.channels[l].instrument == InstrumentKind::layer)
                for (const auto child : song.channels[l].layer.children)
                    if (child >= 0 && (std::size_t) child < plays.size() && plays[(std::size_t) child])
                    {
                        if (song.channels[l].arp.enabled())
                            return all;
                        plays[l] = 1, grew = true;
                        break;
                    }
    }
    const auto ofChannel = [&plays] (const Note& n) { return n.channel >= 0 && (std::size_t) n.channel < plays.size() && plays[(std::size_t) n.channel] && ! n.slide; };
    const auto echo = channel.misc.echo.enabled() ? std::clamp (channel.misc.echo.echoes, 0, 64) * (channel.misc.echo.fat ? 2 : 1) : 0;
    const auto echoTicks = (double) channel.misc.echo.timeBeats * ppq;

    std::vector<std::pair<double, int>> edges; // (tick, +1 start / −1 end) of one timeline
    auto best = 0;
    const auto addVoice = [&] (double start, double ticks) {
        edges.push_back ({ start - slack, +1 });
        edges.push_back ({ start + ticks + slack, -1 });
    };
    const auto addNote = [&] (double start) {
        addVoice (start, voiceTicks);
        for (int k = 1; k <= echo; ++k)
            addVoice (start + (double) (channel.misc.echo.fat ? (k + 1) / 2 : k) * echoTicks, voiceTicks);
    };
    const auto count = [&] {
        std::sort (edges.begin(), edges.end()); // (an end at the same tick as a start comes first)
        auto now = 0;
        for (const auto& e : edges)
            best = std::max (best, now += e.second);
        edges.clear();
    };

    // the notes of the channels in `plays`, of pattern `p` (through the plan's index: the order differs from the
    // pattern's, which the sort of the edges makes irrelevant)
    std::vector<int> players;
    for (std::size_t k = 0; k < plays.size(); ++k)
        if (plays[k])
            players.push_back ((int) k);
    const auto forNotes = [&players] (const std::vector<std::pair<int, std::uint32_t>>& byChannel, const Pattern& pattern, auto&& fn) {
        for (const auto ch : players)
            for (auto it = std::lower_bound (byChannel.begin(), byChannel.end(), std::pair<int, std::uint32_t> { ch, 0u });
                 it != byChannel.end() && it->first == ch; ++it)
                fn (pattern.notes[it->second]);
    };

    // the song: pattern clips (with their repeats), audio clips of the channel
    const Song& played = plan.expanded ? *plan.expanded : song;
    const auto& playedNotes = plan.expanded ? plan.playedNotes : plan.songNotes;
    for (const auto& clip : played.playlist)
    {
        if (clip.kind == ClipKind::audio && clip.channel >= 0 && (std::size_t) clip.channel < plays.size() && plays[(std::size_t) clip.channel])
            addVoice ((double) clip.start, (double) clip.length + 0.3 * ticksPerSecond);
        if (clip.kind != ClipKind::pattern || clip.pattern < 0 || clip.pattern >= (int) played.patterns.size() || clip.length <= 0)
            continue;
        const auto& pattern = played.patterns[(std::size_t) clip.pattern];
        const auto period = std::max<std::int64_t> (1, pattern.effectiveLength (played.meter()));
        const auto lastPass = (clip.offset + clip.length - 1) / period;
        forNotes (playedNotes[(std::size_t) clip.pattern], pattern, [&] (const Note& n) {
            if (! ofChannel (n))
                return;
            for (std::int64_t pass = 0; pass <= lastPass; ++pass)
            {
                const auto at = n.start + pass * period;
                if (pass > 0 && n.start >= period)
                    break;
                if (at >= clip.offset && at < clip.offset + clip.length)
                    addNote ((double) (clip.start + at - clip.offset));
            }
        });
    }
    count();

    // pattern mode: each pattern on its own, looping
    for (std::size_t p = 0; p < song.patterns.size(); ++p)
    {
        const auto& pattern = song.patterns[p];
        const auto period = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
        const auto loops = std::max (2.0, std::ceil ((voiceTicks + 2.0 * slack) / period) + 1.0);
        auto any = false;
        forNotes (plan.songNotes[p], pattern, [&] (const Note& n) {
            if (ofChannel (n))
            {
                any = true;
                for (double k = 0.0; k < loops; ++k)
                    addNote ((double) n.start + k * period);
            }
        });
        if (any)
            count();
        if (best >= all)
            break;
    }

    // (not capped by the channel's Max polyphony: a note past it releases the oldest one, which still sounds)
    return std::clamp (best, 0, all);
}

// The keep-pitch samplers' shifters, in parallel: each instrument only touches its own pool, and each shifter's
// set-up is deterministic (a fixed seed and a silent priming run), so the result is the same as one by one
void prepareKeyShifts (const std::vector<Instrument*>& list, double sampleRate)
{
    if (list.empty())
        return;
    const loadtrace::Scope timing ("rack_keyshift");
    const auto threads = (std::size_t) std::clamp ((int) std::thread::hardware_concurrency() - 1, 1, 6);
    const char* off = std::getenv ("LYNX_PARALLEL_KEYSHIFT"); // "0": one by one (A/B and the null test)
    if (list.size() < 2 || threads < 2 || (off != nullptr && off[0] == '0'))
    {
        for (auto* i : list)
            i->prepareKeyShift (sampleRate);
        return;
    }
    std::atomic<std::size_t> next { 0 };
    const auto mode = FloatMode::current(); // the helpers round and flush like this thread
    auto work = [&] {
        mode.applyHere();
        for (auto k = next.fetch_add (1); k < list.size(); k = next.fetch_add (1))
            list[k]->prepareKeyShift (sampleRate);
    };
    std::vector<std::thread> helpers;
    for (std::size_t t = 1; t < std::min (threads, list.size()); ++t)
    {
        try
        {
            helpers.emplace_back (work);
        }
        catch (...) // no thread: the others (and this one) do the rest
        {
            break;
        }
    }
    work();
    for (auto& h : helpers)
        h.join();
}

// Round 168s (issue #32): for each item of `now`, the index of the item of `before` it was (its instance tag), or the
// same position when the item that was there is gone from `now`; −1 = a new item. Each old item goes to one new item
// only (the first with its tag: a copy that kept the tag gets a new instance). Only indexes below `usable` are given.
template <typename T>
std::vector<long> matchByInstance (const std::vector<T>& now, const std::vector<T>* before, std::size_t usable)
{
    std::vector<long> origin (now.size(), -1);
    if (before == nullptr)
        return origin;
    usable = std::min (usable, before->size());
    std::map<std::uint64_t, std::size_t> wasAt;
    for (std::size_t k = 0; k < usable; ++k)
        wasAt.emplace ((*before)[k].instance.id, k);
    std::map<std::uint64_t, int> stillThere;
    for (const auto& item : now)
        ++stillThere[item.instance.id];
    std::vector<bool> taken (usable, false);

    for (std::size_t i = 0; i < now.size(); ++i)
        if (const auto it = wasAt.find (now[i].instance.id); it != wasAt.end() && ! taken[it->second])
        {
            origin[i] = (long) it->second;
            taken[it->second] = true;
        }

    for (std::size_t i = 0; i < now.size() && i < usable; ++i)
        if (origin[i] < 0 && ! taken[i] && stillThere.count ((*before)[i].instance.id) == 0)
        {
            origin[i] = (long) i;
            taken[i] = true;
        }
    return origin;
}

} // namespace

ClipAudio renderClipAudio (const Song& song, std::size_t clipIndex, double sampleRate, const RackState::Factories& factories)
{
    ClipAudio out;
    out.sampleRate = sampleRate;
    if (clipIndex >= song.playlist.size())
        return out;
    const auto& clip = song.playlist[clipIndex];
    if (clip.kind != ClipKind::audio || clip.length <= 0 || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return out;
    const auto channel = (std::size_t) clip.channel;
    const auto& def = song.channels[channel];
    if (def.instrument != InstrumentKind::audioClip && def.instrument != InstrumentKind::sampler)
        return out;
    auto instrument = buildInstrument (def, sampleRate, song.bpm, factories);
    if (instrument == nullptr)
        return out;
    if (clipShiftWanted (song, channel))
    {
        instrument->setClipShift (true);
        instrument->prepareKeyShift (sampleRate);
    }

    // the note the Scheduler plays for the clip (Scheduler.cpp, audio clips), without its gain, pan, fades and declick
    // (fadeShape bits 5 / 6: no declicker at either edge)
    const auto secondsPerTick = 60.0 / (std::max (song.bpm, 1.0) * ppq);
    const auto key = audioClipKey (clipIndex);
    if (clip.pitch != 0.0f)
        instrument->setNextClipPitch (clip.pitch);
    if (clip.stretch != 1.0)
        instrument->setNextClipStretch ((float) clip.stretch);
    instrument->setNextClipFades (0.0f, (float) ((double) clip.length * secondsPerTick), 0.0f, 0.0f,
                                  (std::uint16_t) ((clip.reversed ? 4 : 0) | 32 | 64), 0.0f, 0.0f);
    instrument->noteOnAt (key, 1.0f, std::max (0.0, ((double) clip.offset + (double) clip.offsetFraction) * secondsPerTick));

    const auto frames = (std::size_t) std::ceil ((double) clip.length * secondsPerTick * sampleRate);
    out.left.assign (frames, 0.0f);
    out.right.assign (frames, 0.0f);
    constexpr std::size_t block = 512;
    for (std::size_t at = 0; at < frames; at += block)
        instrument->render (out.left.data() + at, out.right.data() + at, (int) std::min (block, frames - at), 1.0f, 1.0f);
    instrument->noteOff (key);

    // an audio clip voice plays its file at unity at FL's default channel volume (Instruments.cpp audioClipUnity):
    // undo that so the file holds the clip's audio as it is
    if (def.instrument == InstrumentKind::audioClip)
    {
        const auto unity = channelGain (audioClipUnityVolume);
        for (std::size_t i = 0; i < frames; ++i)
        {
            out.left[i] *= unity;
            out.right[i] *= unity;
        }
    }
    return out;
}

ClipAudio renderAudition (const ChannelDef& channel, const std::vector<int>& keys, double noteSeconds, double tailSeconds, double bpm,
                          double sampleRate, const RackState::Factories& factories, float peak)
{
    ClipAudio out;
    out.sampleRate = sampleRate;
    if (keys.empty() || noteSeconds <= 0.0 || sampleRate <= 0.0)
        return out;
    auto instrument = buildInstrument (channel, sampleRate, bpm, factories);
    if (instrument == nullptr)
        return out;
    const auto noteFrames = (std::size_t) (noteSeconds * sampleRate);
    const auto frames = noteFrames * keys.size() + (std::size_t) (std::max (0.0, tailSeconds) * sampleRate);
    out.left.assign (frames, 0.0f);
    out.right.assign (frames, 0.0f);
    const auto gain = channelGain (channel.volume);
    constexpr std::size_t block = 256;
    std::size_t note = 0;
    int held = -1;
    for (std::size_t at = 0; at < frames; at += block)
    {
        // (notes change on block edges: an audition needs no sample-exact timing)
        while (note <= keys.size() && at >= note * noteFrames)
        {
            if (held >= 0)
                instrument->noteOff (held);
            held = note < keys.size() ? keys[note] : -1;
            if (held >= 0)
                instrument->noteOn (held, 0.8f);
            ++note;
        }
        instrument->render (out.left.data() + at, out.right.data() + at, (int) std::min (block, frames - at), gain, gain);
    }
    instrument->allNotesOff();

    float top = 0.0f;
    for (std::size_t i = 0; i < frames; ++i)
    {
        if (! std::isfinite (out.left[i]) || ! std::isfinite (out.right[i]))
            out.left[i] = out.right[i] = 0.0f;
        top = std::max (top, std::max (std::abs (out.left[i]), std::abs (out.right[i])));
    }
    if (top < 1.0e-5f) // nothing sounded (an engine whose data files are not on this computer)
    {
        out.left.clear();
        out.right.clear();
        return out;
    }
    if (peak > 0.0f)
    {
        const auto scale = peak / top;
        for (std::size_t i = 0; i < frames; ++i)
        {
            out.left[i] *= scale;
            out.right[i] *= scale;
        }
    }
    // a short fade at the end: a long release is cut without a click
    const auto fade = std::min (frames, (std::size_t) (0.02 * sampleRate));
    for (std::size_t i = 0; i < fade; ++i)
    {
        const auto g = (float) i / (float) fade;
        out.left[frames - 1 - i] *= g;
        out.right[frames - 1 - i] *= g;
    }
    return out;
}

// Round 190ao: the build keeps a hosted plugin's instance while the slot's plugin id is the same; an id no plugin has
// makes it build that one again.
namespace
{
constexpr const char* forgottenPluginId = "\x01reload";
}

bool forgetHostedPlugin (RackState& shape, int channel)
{
    if (channel < 0 || channel >= (int) shape.song.channels.size() || shape.song.channels[(std::size_t) channel].instrument != InstrumentKind::plugin)
        return false;
    shape.song.channels[(std::size_t) channel].plugin.id = forgottenPluginId;
    return true;
}

bool forgetHostedPlugin (RackState& shape, int track, int slot)
{
    if (track < 0 || track >= (int) shape.song.mixer.size() || slot < 0 || slot >= (int) shape.song.mixer[(std::size_t) track].effects.size()
        || shape.song.mixer[(std::size_t) track].effects[(std::size_t) slot].kind != EffectKind::plugin)
        return false;
    shape.song.mixer[(std::size_t) track].effects[(std::size_t) slot].plugin.id = forgottenPluginId;
    return true;
}

std::unique_ptr<const RackState> RackState::build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const Factories& factories)
{
    const auto& samples = factories.samples;
    // round 190op (P4): deactivated channels and inserts are never built (the app's engine passes its projection
    // already; the export plugin and the tools come through here). Nothing changes for a song without any.
    if (deactivate::anyMarked (song))
        deactivate::project (song, deactivate::effective (song));

    auto state = std::make_unique<RackState>();
    state->live = std::make_shared<LiveLevels> (song);
    state->instruments.reserve (song.channels.size());
    std::vector<Instrument*> keyShiftLater; // new instruments only: none is visible to the audio thread yet

    // which previous channel each channel was (round 168s, issue #32): by its instance tag, so inserting, removing or
    // moving channels keeps every other channel's running instrument (voices, loops, a plugin's live state); by position
    // only when the channel there is gone from the song (another project loaded over it, the old behaviour)
    auto channelOrigin = matchByInstance (song.channels, previous != nullptr ? &previous->song.channels : nullptr,
                                         previous != nullptr ? previous->instruments.size() : 0);
    for (std::size_t i = 0; i < channelOrigin.size(); ++i)
        if (factories.freshInstances.contains (song.channels[i].instance.id)) channelOrigin[i] = -1;
    {
        static std::atomic<std::uint64_t> serials { 1 };
        state->serial = serials.fetch_add (1, std::memory_order_relaxed);
        state->builtFrom = previous != nullptr ? previous->serial : 0;
        state->channelFrom.assign (channelOrigin.begin(), channelOrigin.end());
        for (std::size_t c = 0; c < channelOrigin.size(); ++c)
            state->channelsMoved = state->channelsMoved || channelOrigin[c] != (long) c;
        state->channelsMoved = state->channelsMoved || (previous != nullptr && previous->song.channels.size() != channelOrigin.size());
    }

    for (std::size_t i = 0; i < song.channels.size(); ++i)
    {
        const auto& channel = song.channels[i];
        const auto kind = channel.instrument;
        const auto p = channelOrigin[i] >= 0 ? (std::size_t) channelOrigin[i] : std::numeric_limits<std::size_t>::max();

        // round 190kx: an audio clip channel edited in an ARA plugin plays through that plugin (the ARA host keeps one
        // instance per channel: asking again returns the same instance)
        if (kind == InstrumentKind::audioClip && channel.ara.active())
        {
            std::shared_ptr<Instrument> unit;
            if (factories.araInstrument)
                unit = factories.araInstrument (channel, sampleRate);
            else if (factories.liveInstrument)
                unit = factories.liveInstrument (channel, sampleRate);
            if (unit != nullptr)
            {
                state->instruments.push_back (std::move (unit));
                continue;
            }
        }

        if (previous != nullptr && p < previous->song.channels.size() && p < previous->instruments.size()
            && previous->song.channels[p].instrument == kind && kind != InstrumentKind::patcher
            && previous->song.channels[p].ara == channel.ara // (round 190kx: into / out of an ARA plugin: another instrument)
            && ((kind != InstrumentKind::granulizer && kind != InstrumentKind::wavetraveller)
                || previous->song.channels[p].samplePath == channel.samplePath)
            && (kind != InstrumentKind::fruityslicer
                || (previous->song.channels[p].samplePath == channel.samplePath
                    && previous->song.channels[p].pluginState == channel.pluginState))
            && (kind != InstrumentKind::slicex // (its state is taken live; its samples are the instrument's)
                || (previous->song.channels[p].samplePath == channel.samplePath && previous->song.channels[p].zones == channel.zones
                    && previous->sampleRate == sampleRate))
            && (kind != InstrumentKind::soundfont
                || (previous->song.channels[p].samplePath == channel.samplePath && previous->sampleRate == sampleRate))
            && (kind != InstrumentKind::fpc // (the kit and its files: a new instrument)
                || (previous->song.channels[p].zones == channel.zones && previous->song.channels[p].pluginState == channel.pluginState
                    && previous->sampleRate == sampleRate))
            && (kind != InstrumentKind::directwave
                || (previous->song.channels[p].zones == channel.zones && previous->song.channels[p].modelParams == channel.modelParams
                    && previous->song.channels[p].pluginState == channel.pluginState && previous->sampleRate == sampleRate))
            && ((kind != InstrumentKind::sampler && kind != InstrumentKind::audioClip && kind != InstrumentKind::flkeys)
                || (previous->song.channels[p].samplePath == channel.samplePath && previous->song.channels[p].zones == channel.zones
                    && previous->song.channels[p].polyEdit == channel.polyEdit // (round 190lk: other note edits, another render)
                    && previous->song.channels[p].modelParams == channel.modelParams
                    && previous->song.channels[p].sampler.keepPitch == channel.sampler.keepPitch
                    && clipShiftWanted (previous->song, p) == clipShiftWanted (song, i)
                    // FL's precomputed effects (round 167h): the sample is processed when the channel is built
                    && previous->song.channels[p].sampler.precompute == channel.sampler.precompute
                    && previous->song.channels[p].sampler.useLoopPoints == channel.sampler.useLoopPoints
                    && (! precompute::needed (channel.sampler)
                        || (previous->song.channels[p].sampler.regionStart == channel.sampler.regionStart
                            && previous->song.channels[p].sampler.regionEnd == channel.sampler.regionEnd
                            && previous->song.channels[p].sampler.reverse == channel.sampler.reverse
                            && precompute::needed (previous->song.channels[p].sampler)))
                    && (! channel.sampler.keepPitch
                        || (previous->song.channels[p].sampler.stretchBeats == channel.sampler.stretchBeats
                            && previous->song.channels[p].sampler.stretchMethod == channel.sampler.stretchMethod
                            && previous->song.channels[p].sampler.stretchPitch == channel.sampler.stretchPitch
                            && previous->song.channels[p].sampler.warp == channel.sampler.warp
                            && previous->song.channels[p].sampler.warpModes == channel.sampler.warpModes
                            && previous->song.bpm == song.bpm))))
            && (kind != InstrumentKind::plugin
                || (previous->song.channels[p].plugin.id == channel.plugin.id && previous->sampleRate == sampleRate
                    && samePluginProcessing (previous->song.channels[p].plugin, channel.plugin) && ! rebuilds (factories, channel.plugin)
                    && sameHostedPlugin (previous->song.channels[p], channel))))
        {
            state->instruments.push_back (previous->instruments[p]);
            state->instruments.back()->configure (channel, song.bpm); // (a Slicer takes its slices from its state: issue #160)
            continue;
        }

        // round 190jb: an export renders with the live engine's running plugin (or the Patcher that hosts plugins)
        if (factories.liveInstrument && (kind == InstrumentKind::plugin || kind == InstrumentKind::patcher))
            if (auto lent = factories.liveInstrument (channel, sampleRate); lent != nullptr)
            {
                state->instruments.push_back (std::move (lent));
                state->instruments.back()->configure (channel, song.bpm);
                continue;
            }

        // FL's Patcher as a generator (REA_rodada141): same structure keeps the running graph (it takes the values)
        if (kind == InstrumentKind::patcher)
        {
            const auto* old = previous != nullptr && p < previous->instruments.size() ? previous->instruments[p].get() : nullptr;
            if (old != nullptr && previous->song.channels[p].instrument == kind && previous->sampleRate == sampleRate
                && patcher::sameStructure (previous->song.channels[p].patcher, channel.patcher) && ! rebuildsInside (factories, channel.patcher)
                // a node's latency moved since its delay compensation was planned: build it again (issue #113)
                && (patcher::graphOf (previous->instruments[p].get()) == nullptr
                    || ! patcher::graphOf (previous->instruments[p].get())->latencyStale (&channel.patcher)))
            {
                state->instruments.push_back (previous->instruments[p]);
                state->instruments.back()->configure (channel, song.bpm);
                continue;
            }
            const auto* kept = (old != nullptr && previous->sampleRate != sampleRate && hostsPlugins (channel.patcher)) || rebuildsInside (factories, channel.patcher)
                                   ? nullptr
                                   : old;
            state->instruments.push_back (patcher::makeInstrument (channel, sampleRate, patcherFactories (sampleRate, song.bpm, factories), kept));
            state->instruments.back()->configure (channel, song.bpm);
            continue;
        }

        state->instruments.push_back (buildInstrument (channel, sampleRate, song.bpm, factories, &keyShiftLater));
        // FL 2026's clip pitch (round 167k): a channel with a pitched clip gets its pitch shifters even when it resamples
        if (clipShiftWanted (song, i) && state->instruments.back() != nullptr)
        {
            state->instruments.back()->setClipShift (true);
            keyShiftLater.push_back (state->instruments.back().get());
        }
    }
    // Options › Project › "Fast declick for cut groups" (REA round 167h §3)
    for (auto& instrument : state->instruments)
        if (instrument != nullptr)
        {
            instrument->setFastDeclick (song.fastDeclick);
            instrument->setPanTriangular (song.panTriangular);
            instrument->setFlWrapperVelocity (song.flPluginLevels);
        }
    // round 190gn (A1): the shifters each keep-pitch channel's voices can use at once are made now; any further one (a
    // live keyboard, an edit) comes from the rack's stock, refilled off the audio thread
    {
        const loadtrace::Scope timing ("rack_shifter_plan");
        const auto offline = offlineRendering() || renderInProgress();
        state->shifterStock = previous != nullptr && previous->shifterStock != nullptr && previous->sampleRate == sampleRate
                                      && previous->shifterStock->offline() == (offline && ! shifterstock::strict())
                                  ? previous->shifterStock
                                  : std::make_shared<ShifterStock> (sampleRate, offline);
        auto& stock = *state->shifterStock;
        std::array<int, ShifterStock::kinds> reserve {};
        std::array<bool, ShifterStock::kinds> used {};
        std::optional<VoicePlan> plan; // (made for the first keep-pitch channel)
        for (std::size_t i = 0; i < song.channels.size() && i < state->instruments.size(); ++i)
        {
            auto* instrument = state->instruments[i].get();
            const auto& channel = song.channels[i];
            const auto fresh = std::find (keyShiftLater.begin(), keyShiftLater.end(), instrument) != keyShiftLater.end();
            const auto shifts = (channel.instrument == InstrumentKind::sampler || channel.instrument == InstrumentKind::audioClip)
                                && (channel.sampler.keepPitch || clipShiftWanted (song, i));
            if (instrument == nullptr || ! shifts || ! shifterstock::planned())
                continue;
            const auto kind = ShifterStock::kindOf (channel.sampler.stretchMethod);
            if (! plan)
                plan = voicePlan (song);
            const auto need = voicesAtOnce (song, *plan, i, instrument->longestVoiceSeconds());
            if (fresh)
                instrument->useShifterStock (state->shifterStock, need);
            else // (a running instrument: what the song now wants beyond what it holds waits in the stock)
                reserve[(std::size_t) kind] += std::max (0, need - instrument->pitchShiftersHeld());
            used[(std::size_t) kind] = true;
        }
        // + a live reserve (a chord) for each kind in use: a keyboard played into a keep-pitch channel
        for (int k = 0; k < ShifterStock::kinds; ++k)
            stock.setReserve (k, used[(std::size_t) k] ? ShifterStock::liveReserve + reserve[(std::size_t) k] : 0);
    }
    prepareKeyShifts (keyShiftLater, sampleRate);
    state->shifterStock->topUp();
    shifterstock::enlist (state->shifterStock);

    state->effects.resize (song.mixer.size());

    // which previous slot each effect slot was (round 168s, issue #32): by its instance tag, so moving an effect up /
    // down its insert (or to another insert) keeps its running effect — a hosted plugin keeps every setting made since
    // the project was saved; by position only when the slot there is gone (the old behaviour)
    std::vector<std::vector<std::pair<long, long>>> slotOrigin (song.mixer.size());
    {
        std::map<std::uint64_t, std::pair<std::size_t, std::size_t>> wasAt;
        std::map<std::uint64_t, int> stillThere;
        std::map<std::pair<std::size_t, std::size_t>, bool> taken;
        const auto usable = [previous] (std::size_t t) {
            return previous == nullptr || t >= previous->song.mixer.size() || t >= previous->effects.size()
                       ? std::size_t { 0 }
                       : std::min (previous->song.mixer[t].effects.size(), previous->effects[t].size());
        };
        if (previous != nullptr)
            for (std::size_t t = 0; t < previous->song.mixer.size(); ++t)
                for (std::size_t i = 0; i < usable (t); ++i)
                    wasAt.emplace (previous->song.mixer[t].effects[i].instance.id, std::make_pair (t, i));
        for (const auto& track : song.mixer)
            for (const auto& slot : track.effects)
                ++stillThere[slot.instance.id];
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
        {
            const auto& slots = song.mixer[t].effects;
            slotOrigin[t].assign (slots.size(), { -1, -1 });
            for (std::size_t i = 0; i < slots.size(); ++i)
                if (const auto it = wasAt.find (slots[i].instance.id); it != wasAt.end() && ! taken[it->second])
                {
                    slotOrigin[t][i] = { (long) it->second.first, (long) it->second.second };
                    taken[it->second] = true;
                }
        }
        for (std::size_t t = 0; t < song.mixer.size(); ++t)
            for (std::size_t i = 0; i < song.mixer[t].effects.size() && i < usable (t); ++i)
                if (slotOrigin[t][i].first < 0 && ! taken[{ t, i }] && stillThere.count (previous->song.mixer[t].effects[i].instance.id) == 0)
                {
                    slotOrigin[t][i] = { (long) t, (long) i };
                    taken[{ t, i }] = true;
                }
    }

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        const auto& slots = song.mixer[t].effects;

        for (std::size_t i = 0; i < slots.size() && i < (std::size_t) maxEffectSlots; ++i)
        {
            if (factories.freshInstances.contains (slots[i].instance.id)) slotOrigin[t][i] = { -1, -1 };
            const auto kind = slots[i].kind;
            const auto none = std::numeric_limits<std::size_t>::max();
            const auto pt = slotOrigin[t][i].first >= 0 ? (std::size_t) slotOrigin[t][i].first : none;
            const auto pi = slotOrigin[t][i].second >= 0 ? (std::size_t) slotOrigin[t][i].second : none;

            if (previous != nullptr && pt < previous->song.mixer.size() && pt < previous->effects.size()
                && pi < previous->song.mixer[pt].effects.size() && pi < previous->effects[pt].size()
                && previous->song.mixer[pt].effects[pi].kind == kind && kind != EffectKind::patcher
                && (kind != EffectKind::parallel || sameContainer (previous->song.mixer[pt].effects[pi], slots[i])) // children are built in
                // (Edison keeps running when its audio gets a new file: the audio is already in it, round 190cd)
                && (previous->song.mixer[pt].effects[pi].samplePath == slots[i].samplePath || kind == EffectKind::edison)
                // round 144: Facet EQ's buffers are sized by its linear-phase resolution: a new one, a new instance
                && (kind != EffectKind::facetEq
                    || facet::resolution (previous->song.mixer[pt].effects[pi].params[(std::size_t) facet::resolutionParam])
                           == facet::resolution (slots[i].params[(std::size_t) facet::resolutionParam]))
                // round 190nr: Atrium's dense late tank is allocated only for the slots that run it: a new instance when that differs
                && (kind != EffectKind::atrium || verbs::atriumDense (previous->song.mixer[pt].effects[pi].params) == verbs::atriumDense (slots[i].params))
                && (kind != EffectKind::plugin
                    || (previous->song.mixer[pt].effects[pi].plugin.id == slots[i].plugin.id && previous->sampleRate == sampleRate
                        && samePluginProcessing (previous->song.mixer[pt].effects[pi].plugin, slots[i].plugin) && ! rebuilds (factories, slots[i].plugin)
                        && sameHostedPlugin (previous->song.mixer[pt].effects[pi], slots[i])))
                && (previous->song.mixer[pt].effects[pi].state == slots[i].state
                    || (previous->sampleRate == sampleRate && previous->effects[pt][pi] != nullptr && previous->effects[pt][pi]->acceptState (slots[i].state))))
            {
                if (kind == EffectKind::edison && previous->song.mixer[pt].effects[pi].samplePath != slots[i].samplePath)
                    edisonfx::followPath (slots[i], factories.samples); // (a relocated or undone file)
                state->effects[t].push_back (previous->effects[pt][pi]);
                continue;
            }

            // round 190jb: an export renders with the live engine's running plugin (or the Patcher that hosts plugins)
            if (factories.liveEffect && (kind == EffectKind::plugin || kind == EffectKind::patcher))
                if (auto lent = factories.liveEffect (slots[i], sampleRate); lent != nullptr)
                {
                    state->effects[t].push_back (std::move (lent));
                    continue;
                }

            // FL's Patcher (REA_rodada141): same structure keeps the running graph (it takes the values)
            if (kind == EffectKind::patcher)
            {
                const auto* old = previous != nullptr && pt < previous->effects.size() && pi < previous->effects[pt].size()
                                          && pi < previous->song.mixer[pt].effects.size() && previous->song.mixer[pt].effects[pi].kind == kind
                                      ? previous->effects[pt][pi].get()
                                      : nullptr;
                if (old != nullptr && previous->sampleRate == sampleRate
                    && patcher::sameStructure (previous->song.mixer[pt].effects[pi].patcher, slots[i].patcher) && ! rebuildsInside (factories, slots[i].patcher)
                    && patcher::acceptValues (previous->effects[pt][pi].get(), slots[i]))
                {
                    state->effects[t].push_back (previous->effects[pt][pi]);
                    continue;
                }
                const auto* kept = (old != nullptr && previous->sampleRate != sampleRate && hostsPlugins (slots[i].patcher)) || rebuildsInside (factories, slots[i].patcher)
                                       ? nullptr
                                       : old;
                std::shared_ptr<Effect> effect = patcher::makeEffect (slots[i], sampleRate, patcherFactories (sampleRate, song.bpm, factories), kept);
                effect->prepare (sampleRate);
                state->effects[t].push_back (std::move (effect));
                continue;
            }

            state->effects[t].push_back (buildEffect (slots[i], sampleRate, factories));
        }
    }

    // Issue #109: a hosted plugin's side input is on only while its slot has a sidechain source (the same test as
    // setSidechain below); a running plugin keeps its instance and state
    for (std::size_t t = 0; t < state->effects.size() && t < song.mixer.size(); ++t)
        for (std::size_t i = 0; i < state->effects[t].size() && i < song.mixer[t].effects.size(); ++i)
            if (song.mixer[t].effects[i].kind == EffectKind::plugin && state->effects[t][i] != nullptr)
            {
                const auto source = song.mixer[t].effects[i].sidechain;
                // (round 189j: with its Connections set, input 2 is fed unless inactive — its own input by default)
                const auto connections = lynx::slotConnections (song, (int) t, song.mixer[t].effects[i]);
                state->effects[t][i]->setConnections (connections);
                state->effects[t][i]->setSideInputActive ((source >= 0 && source != (int) t && source < (int) song.mixer.size())
                                                          || (connections.routed && connections.sideSource != -2));
            }

    // FL's Channel Rack looping (round 165f-rack): the looping channels' repeats are written out as notes here, so
    // playback, the Playlist's pattern clips and export all play them without any cost on the audio thread
    // FL's legacy Pattern Blocks (round 170f): plain pattern clips from here on (playback, export, song length)
    patternblocks::expandSong (song);
    channelloops::expandSong (song);
    state->song = std::move (song);
    const auto tailStarted = std::chrono::steady_clock::now();

    // scheduling: notes sorted by start (order among notes on the same tick is preserved)
    for (auto& p : state->song.patterns)
        std::stable_sort (p.notes.begin(), p.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    state->schedule = makeScheduleIndex (state->song);
    state->performanceZone = perf::zoneClips (state->song);
    if (factories.clipLauncher != nullptr && state->song.clipGrid)
    {
        state->clipLauncher = factories.clipLauncher;
        state->gridIndex = launcher::makeIndex (*state->song.clipGrid);
    }
    state->launchers = ! state->performanceZone.empty() || state->clipLauncher != nullptr;
    state->sampleRate = sampleRate;
    state->buildMidiRoutes();
    state->buildAutomationTables();
    {
        const loadtrace::Scope timing ("pdc");
        state->lowLatencyTracks = factories.lowLatencyTracks; // (round 190aq; empty in exports)
        state->pdcCurrentSource = factories.currentSource; // (round 190my)
        state->buildPdc (previous);
    }
    state->buildMixerGraph();

    const auto& channels = state->song.channels;
    state->cutTargets.assign (channels.size(), {});
    state->arpeggiators.assign (channels.size(), nullptr);

    for (std::size_t c = 0; c < channels.size(); ++c)
        if (channels[c].arp.enabled())
            state->arpeggiators[c] = previous != nullptr && channelOrigin[c] >= 0 && (std::size_t) channelOrigin[c] < previous->arpeggiators.size()
                                             && previous->arpeggiators[(std::size_t) channelOrigin[c]] != nullptr
                                         ? previous->arpeggiators[(std::size_t) channelOrigin[c]]
                                         : std::make_shared<Arpeggiator>();

    state->echoes.assign (channels.size(), nullptr);
    for (std::size_t c = 0; c < channels.size(); ++c)
        if (channels[c].misc.echo.enabled())
            state->echoes[c] = previous != nullptr && channelOrigin[c] >= 0 && (std::size_t) channelOrigin[c] < previous->echoes.size()
                                       && previous->echoes[(std::size_t) channelOrigin[c]] != nullptr
                                   ? previous->echoes[(std::size_t) channelOrigin[c]]
                                   : std::make_shared<channelmisc::NoteEcho>();

    // FL's cut groups cut its Sampler / audio clip channels (REA_rodada167h) but not its plugin generators: Kepler
    // keeps sounding when its group cuts it (REA_rodada165g-flbeta165k §3), and so do the 26 FL generators of
    // REA_rodada170i3 §2 (FL's renders with and without "cut itself" are the same; Ogun's tail in 169l's project).
    // Nor its wrapped third-party plugins (issue #143, REA_rodada190x): Surge XT (VST3) renders the same in FL with
    // and without "cut itself" — the group FL's default template gives every new channel (0x84 = 0x10001) — while
    // LYNX's choke, landing after the note on at the same sample, ended every note as it started: silent channels.
    const auto flCutsIt = [] (InstrumentKind k) {
        switch (k)
        {
            case InstrumentKind::sakura: case InstrumentKind::drumpad: case InstrumentKind::boobass:
            case InstrumentKind::drumsynth: case InstrumentKind::harmless: case InstrumentKind::sytrus:
            case InstrumentKind::harmor: case InstrumentKind::gms: case InstrumentKind::morphine:
            case InstrumentKind::ogun: case InstrumentKind::minisynth: case InstrumentKind::dx10:
            case InstrumentKind::kepler: case InstrumentKind::keplerExo: case InstrumentKind::poizone:
            case InstrumentKind::fruitkick: case InstrumentKind::bassdrum: case InstrumentKind::toxic:
            case InstrumentKind::threeosc: case InstrumentKind::autogun: case InstrumentKind::transistorbass:
            case InstrumentKind::simsynth: case InstrumentKind::plucked: case InstrumentKind::beepmap:
            case InstrumentKind::sawer: case InstrumentKind::flkeys: case InstrumentKind::granulizer:
            case InstrumentKind::drumaxx: case InstrumentKind::slayer: case InstrumentKind::directwave:
            case InstrumentKind::wavetraveller: case InstrumentKind::slicex:
            case InstrumentKind::fpc: // (its pads have their own cut groups, inside)
            case InstrumentKind::plugin:
                return false;
            default:
                return true;
        }
    };
    for (std::size_t c = 0; c < channels.size(); ++c)
        if (channels[c].cutGroup > 0)
            for (std::size_t d = 0; d < channels.size(); ++d)
                if (channels[d].cutBy == channels[c].cutGroup && flCutsIt (channels[d].instrument)
                    && ! (d == c && channels[c].cutOthersOnly)) // (round 190fe: FLM's groups spare the pad that plays)
                {
                    state->cutTargets[c].push_back ((int) d);
                    state->hasCutGroups = true;
                }
    state->hasLayers = std::any_of (channels.begin(), channels.end(), [] (const ChannelDef& c) {
        return c.instrument == InstrumentKind::layer && ! c.layer.children.empty();
    });
    const auto audible = state->song.soloAudible();
    state->mixerAudible.assign (audible.begin(), audible.end());
    state->applyVcas();
    state->buildSlotPlans(); // (round 190gp: last, from the song as it now is)
    loadtrace::add ("rack_schedule_graph", loadtrace::msSince (tailStarted));
    return state;
}

void RackState::applyVcas()
{
    // VCA faders (round 177a): a muted VCA silences its members; the chains let the audio thread multiply the gains
    vcaChains.clear();
    if (song.vcas.empty())
        return;
    vcaChains.assign (song.mixer.size(), {});
    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        vcaChains[(std::size_t) t] = mixerops::vcasOf (song, t);
        if (mixerops::vcaMuted (song, t) && (std::size_t) t < mixerAudible.size())
            mixerAudible[(std::size_t) t] = 0;
    }
}

void RackState::buildMixerGraph()
{
    mixerOrder = song.mixerOrder();
    currentTrack = mixerops::currentTrack (song); // (round 190mv: FL's Current track runs inside its source's processing)
    if (currentTrack > 0)
        mixerOrder.erase (std::remove (mixerOrder.begin(), mixerOrder.end(), currentTrack), mixerOrder.end());
    const auto audible = song.soloAudible();
    mixerAudible.assign (audible.begin(), audible.end());
    applyVcas();

    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    mixerInputs.assign ((std::size_t) tracks, {});
    sendTaps.clear();
    slotTap.assign ((std::size_t) tracks, {});
    preTap.assign ((std::size_t) tracks, -1);
    std::vector<int> depth ((std::size_t) tracks, 0);

    // the hosted effects' Connections (round 189j): an input 1 taken from another insert orders it like a sidechain
    slotConnections.assign ((std::size_t) tracks, {});
    for (int t = 1; t < tracks; ++t)
    {
        const auto& effects = song.mixer[(std::size_t) t].effects;
        for (std::size_t i = 0; i < effects.size(); ++i)
            if (auto c = lynx::slotConnections (song, t, effects[i]); c.routed)
            {
                slotConnections[(std::size_t) t].resize (effects.size());
                slotConnections[(std::size_t) t][i] = c;
            }
    }
    auto sourceOf = [this, tracks] (int t, std::size_t i, bool side) {
        const auto& list = slotConnections[(std::size_t) t];
        const auto source = i < list.size() ? (side ? list[i].sideSource : list[i].mainSource) : -1;
        return source > 0 && source < tracks && source != t ? source : -1;
    };

    // who listens to each track via sidechain (the source goes in an earlier layer)
    std::vector<std::vector<int>> sidechainListeners ((std::size_t) tracks);
    for (int t = 1; t < tracks; ++t)
    {
        const auto& effects = song.mixer[(std::size_t) t].effects;
        for (std::size_t i = 0; i < effects.size(); ++i)
            for (const auto source : { effects[i].sidechain, effects[i].sidechain2, sourceOf (t, i, false), sourceOf (t, i, true) })
                if (source > 0 && source < tracks && source != t)
                    sidechainListeners[(std::size_t) source].push_back (t);
    }

    for (auto t : mixerOrder) // sources before destinations: the depth is already computed
    {
        const auto& track = song.mixer[(std::size_t) t];

        for (const auto listener : sidechainListeners[(std::size_t) t])
            depth[(std::size_t) listener] = std::max (depth[(std::size_t) listener], depth[(std::size_t) t] + 1);

        // a route whose level knob is automated (issue #12)
        auto routeAuto = [this, t] (int to) {
            for (const auto& r : routeAutos)
                if (r.from == t && r.to == to)
                    return r.slot;
            return -1;
        };

        if (track.toMaster)
            mixerInputs[0].push_back ({ t, 0, track.masterLevel, -1, routeAuto (0) });

        for (std::size_t k = 0; k < track.sends.size(); ++k)
        {
            const auto& send = track.sends[k];
            const auto automation = send.target > 0 ? routeAuto (send.target) : -1;

            // (a send at 0 is left out, unless an automation brings it up)
            if (send.target > 0 && send.target < tracks && send.target != t && (send.level > 0.0f || automation >= 0))
            {
                // a pre-fader send (round 190aq) pulls the insert's pre-fader tap, made on its first such send
                auto tap = -1;
                if (send.pre)
                {
                    if (preTap[(std::size_t) t] < 0 && (int) sendTaps.size() < maxSendTaps)
                    {
                        preTap[(std::size_t) t] = (int) sendTaps.size();
                        sendTaps.push_back ({ t, -1, 0 });
                    }
                    tap = preTap[(std::size_t) t];
                }
                mixerInputs[(std::size_t) send.target].push_back ({ t, (int) k + 1, send.level, tap, automation, tap >= 0 });
                depth[(std::size_t) send.target] = std::max (depth[(std::size_t) send.target], depth[(std::size_t) t] + 1);
            }
        }

        // Fruity Send slots: their own signal, delay lines after the sends' (PdcPlan::sendDelay)
        const auto taps = fruitySendsOf (song, t);
        for (std::size_t j = 0; j < taps.size(); ++j)
        {
            const auto slot = taps[j].slot, target = taps[j].target;
            if ((int) sendTaps.size() >= maxSendTaps)
                break;
            if ((std::size_t) t >= slotTap.size())
                slotTap.resize ((std::size_t) t + 1);
            // (two entries per slot: its outputs 0 and 1, the Transmitter's transient / sustain sends)
            const auto key = (std::size_t) slot * maxSlotSends + (std::size_t) taps[j].output;
            if (key >= slotTap[(std::size_t) t].size())
                slotTap[(std::size_t) t].resize (key + 1, -1);
            slotTap[(std::size_t) t][key] = (int) sendTaps.size();
            mixerInputs[(std::size_t) target].push_back ({ t, (int) (1 + track.sends.size() + j), 1.0f, (int) sendTaps.size() });
            sendTaps.push_back ({ t, slot, target });
            depth[(std::size_t) target] = std::max (depth[(std::size_t) target], depth[(std::size_t) t] + 1);
        }
    }

    mixerLayers.clear();

    for (auto t : mixerOrder)
    {
        const auto d = (std::size_t) depth[(std::size_t) t];

        if (mixerLayers.size() <= d)
            mixerLayers.resize (d + 1);

        mixerLayers[d].push_back (t);
    }

    // splitting across threads costs a few microseconds per block; only worth it with plugins or many channels
    const auto plugins = std::count_if (song.channels.begin(), song.channels.end(),
                                        [] (const ChannelDef& c) { return c.instrument == InstrumentKind::plugin; });
    auto pluginEffects = 0;
    for (const auto& t : song.mixer)
        pluginEffects += (int) std::count_if (t.effects.begin(), t.effects.end(), [] (const EffectSlot& e) { return e.kind == EffectKind::plugin; });

    parallel = plugins + pluginEffects >= 2 || song.channels.size() >= 24;

    // plugins whose "Allow threaded processing" is off pin their channel / insert to the audio thread
    threadedChannels.clear();
    audioThreadChannels.clear();
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto& ch = song.channels[(std::size_t) c];
        // (and a generator Patcher holding a Hardware Insert, round 165c: the device-output buses are not shared between threads)
        const auto pinned = (ch.instrument == InstrumentKind::plugin && ! ch.plugin.threaded)
                            || (ch.instrument == InstrumentKind::patcher && patcher::usesHardwareIo (ch.patcher));
        (pinned ? audioThreadChannels : threadedChannels).push_back (c);
    }
    threadedLayers.assign (mixerLayers.size(), {});
    audioThreadLayers.assign (mixerLayers.size(), {});
    for (std::size_t d = 0; d < mixerLayers.size(); ++d)
        for (const auto t : mixerLayers[d])
        {
            const auto& effects = song.mixer[(std::size_t) t].effects;
            // (and the hardware inserts, round 164d: the device-output buses they write are not shared between threads)
            const auto pinned = ! song.mixer[(std::size_t) t].threaded || std::any_of (effects.begin(), effects.end(),
                                             [] (const EffectSlot& e) {
                                                 return (e.kind == EffectKind::plugin && ! e.plugin.threaded) || e.kind == EffectKind::hardwareInsert
                                                        || (e.kind == EffectKind::patcher && patcher::usesHardwareIo (e.patcher)); // (round 165c)
                                             });
            (pinned ? audioThreadLayers : threadedLayers)[d].push_back (t);
        }

    buildTaskGraph (depth);
}

void RackState::buildTaskGraph (const std::vector<int>& depth)
{
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    graphChannels = std::min<int> ((int) song.channels.size(), RackRenderer::maxChannels);
    trackChannels.assign ((std::size_t) tracks, {});
    for (int c = 0; c < graphChannels; ++c)
        trackChannels[(std::size_t) std::clamp (song.channels[(std::size_t) c].mixerTrack, 0, tracks - 1)].push_back (c);

    graphOk = false;
    graphIndegree.clear();
    graphNextStart.clear();
    graphNext.clear();
    graphRoots.clear();

    const auto pinned = ! audioThreadChannels.empty()
                     || std::any_of (audioThreadLayers.begin(), audioThreadLayers.end(), [] (const std::vector<int>& l) { return ! l.empty(); });
    if (pinned || currentTrack > 0) // (round 190mv: with FL's Current track, the layers run the mixer)
        return;

    const auto tasks = graphChannels + tracks - 1;
    const auto trackTask = [this] (int t) { return graphChannels + t - 1; };
    std::vector<std::vector<int>> next ((std::size_t) tasks);
    auto ok = true;
    auto edge = [&] (int from, int to) {
        // the layers run a source in an earlier layer than its destination; a cycle breaks that (the layered path
        // then reads the source before it ran): keep the layers for such a mixer
        if (depth[(std::size_t) to] <= depth[(std::size_t) from])
            ok = false;
        next[(std::size_t) trackTask (from)].push_back (trackTask (to));
    };

    for (int c = 0; c < graphChannels; ++c)
        if (const auto t = std::clamp (song.channels[(std::size_t) c].mixerTrack, 0, tracks - 1); t > 0)
            next[(std::size_t) c].push_back (trackTask (t));
    for (int t = 1; t < tracks; ++t)
    {
        for (const auto& in : mixerInputs[(std::size_t) t])
            if (in.source > 0 && in.source < tracks && in.source != t)
                edge (in.source, t);
        const auto& effects = song.mixer[(std::size_t) t].effects;
        const auto& connections = slotConnections[(std::size_t) t];
        for (std::size_t i = 0; i < effects.size(); ++i)
            for (const auto source : { effects[i].sidechain, effects[i].sidechain2, i < connections.size() ? connections[i].mainSource : -1,
                                       i < connections.size() ? connections[i].sideSource : -1 })
                if (source > 0 && source < tracks && source != t)
                    edge (source, t);
    }
    if (! ok)
        return;

    graphIndegree.assign ((std::size_t) tasks, 0);
    graphNextStart.assign ((std::size_t) tasks + 1, 0);
    for (int i = 0; i < tasks; ++i)
    {
        auto& n = next[(std::size_t) i];
        std::sort (n.begin(), n.end());
        n.erase (std::unique (n.begin(), n.end()), n.end());
        graphNextStart[(std::size_t) i + 1] = graphNextStart[(std::size_t) i] + (int) n.size();
        for (const auto j : n)
        {
            graphNext.push_back (j);
            ++graphIndegree[(std::size_t) j];
        }
    }
    // the channels first (they are most of the work and every insert waits for some of them), then the inserts
    // nothing feeds
    for (int i = 0; i < tasks; ++i)
        if (graphIndegree[(std::size_t) i] == 0)
            graphRoots.push_back (i);
    // serial chains (round 190gq): an insert whose only successor is an insert fed by nothing else runs that insert
    // itself. Only insert → insert links: a channel fused with its insert made the graph run depth-first, so the other
    // channels (the roots, most of the work) started later (measured: Exoplanet's overruns doubled)
    graphFused.assign ((std::size_t) tasks, -1);
    graphFusedCount = 0;
    if (std::getenv ("LYNX_CHAINS") == nullptr || std::atoi (std::getenv ("LYNX_CHAINS")) != 0)
        for (int i = graphChannels; i < tasks; ++i)
            if (graphNextStart[(std::size_t) i + 1] - graphNextStart[(std::size_t) i] == 1)
                if (const auto j = graphNext[(std::size_t) graphNextStart[(std::size_t) i]]; graphIndegree[(std::size_t) j] == 1)
                {
                    graphFused[(std::size_t) i] = j;
                    ++graphFusedCount;
                }
    graphOk = true;
}

namespace
{
// Every unit's latency in the order of RackState::latencies (instruments, then each insert's slots); `take (value)`
// returns false to stop early (round 190of: shared by currentLatencies and latenciesDiffer)
template <typename Take>
void eachLatency (const RackState& state, Take&& take)
{
    // a hosted plugin's latency set by hand in its wrapper settings (round 149) replaces what it reports
    auto manual = [&state] (const PluginRef& ref, int reported) {
        return ref.latencyOverrideMs >= 0.0f ? (int) std::lround (ref.latencyOverrideMs * 0.001 * state.sampleRate) : reported;
    };
    const auto& song = state.song;
    for (std::size_t c = 0; c < state.instruments.size(); ++c)
    {
        const auto reported = state.instruments[c]->latencySamples();
        if (! take (c < song.channels.size() && song.channels[c].instrument == InstrumentKind::plugin ? manual (song.channels[c].plugin, reported) : reported))
            return;
    }

    for (std::size_t t = 0; t < state.effects.size(); ++t)
        for (std::size_t s = 0; s < state.effects[t].size(); ++s)
        {
            if (lowlatency::bypassed (state.lowLatencySlots, (int) t, (int) s)) // (round 190aq: bypassed, nothing to compensate)
            {
                if (! take (0))
                    return;
                continue;
            }
            const auto reported = state.effects[t][s]->latencySamples();
            const auto* slot = t < song.mixer.size() && s < song.mixer[t].effects.size() ? &song.mixer[t].effects[s] : nullptr;
            if (! take (slot != nullptr && slot->kind == EffectKind::plugin ? manual (slot->plugin, reported) : reported))
                return;
        }
}
} // namespace

std::vector<int> RackState::currentLatencies() const
{
    std::vector<int> out;
    eachLatency (*this, [&out] (int latency) {
        out.push_back (latency);
        return true;
    });
    return out;
}

bool RackState::latenciesDiffer (const std::vector<int>& known) const
{
    std::size_t at = 0;
    auto differ = false;
    eachLatency (*this, [&] (int latency) {
        differ = at >= known.size() || known[at] != latency;
        ++at;
        return ! differ;
    });
    return differ || at != known.size();
}

void RackState::buildPdc (const RackState* previous)
{
    // "Low latency while recording" (round 190aq): on the engine's live inserts, the active slots with a latency are
    // bypassed; the plan below is made without them
    lowLatencySlots.clear();
    lowLatencySaved.clear();
    if (! lowLatencyTracks.empty())
    {
        const auto all = currentLatencies(); // (no slot bypassed yet: the real latencies)
        std::vector<lowlatency::SlotMask> masks (effects.size(), 0);
        std::vector<std::vector<int>> saved (effects.size());
        auto next = all.begin() + (std::ptrdiff_t) instruments.size();
        auto any = false;
        for (std::size_t t = 0; t < effects.size(); ++t)
        {
            saved[t].assign (effects[t].size(), 0);
            for (std::size_t s = 0; s < effects[t].size(); ++s)
            {
                const auto latency = *next++;
                if (t < lowLatencyTracks.size() && lowLatencyTracks[t] != 0 && s < 16 && latency > 0 && t < song.mixer.size()
                    && s < song.mixer[t].effects.size() && song.slotActive (song.mixer[t].effects[s]))
                {
                    masks[t] = (lowlatency::SlotMask) (masks[t] | (1u << s));
                    saved[t][s] = latency;
                    any = true;
                }
            }
        }
        if (any)
        {
            lowLatencySlots = std::move (masks);
            lowLatencySaved = std::move (saved);
        }
    }

    latencies = currentLatencies();

    std::vector<int> channelLatency (latencies.begin(), latencies.begin() + (std::ptrdiff_t) instruments.size());
    std::vector<std::vector<int>> slotLatency (effects.size());
    auto next = latencies.begin() + (std::ptrdiff_t) instruments.size();

    for (std::size_t t = 0; t < effects.size(); ++t)
        for (std::size_t s = 0; s < effects[t].size(); ++s)
            slotLatency[t].push_back (*next++);

    pdc = computePdc (song, channelLatency, slotLatency, sampleRate, pdcCurrentSource);

    // reuses lines of the same length: editing the song must not click the audio
    auto line = [] (int length, const std::shared_ptr<DelayLine>* old) -> std::shared_ptr<DelayLine> {
        if (length <= 0)
            return nullptr;
        if (old != nullptr && *old != nullptr && (*old)->length() == length)
            return *old;
        return std::make_shared<DelayLine> (length);
    };

    // round 190bu: the inserts with a chain Blend under 100 % get their dry path, as late as their active slots make
    // the chain (the slots' own latencies, whatever the PDC setting: both paths are inside the insert)
    chainBlends.assign (song.mixer.size(), nullptr);
    for (std::size_t t = 0; t < song.mixer.size() && t < slotLatency.size(); ++t)
    {
        const auto& track = song.mixer[t];
        // round 190dg: a one-knob slot's table is made here, on this thread, not first by the renderer's glide
        for (const auto& slot : track.effects)
            if (slot.goal != 0)
                goals::defaultAmount (slot.goal);
        const auto* before = previous != nullptr && t < previous->chainBlends.size() ? previous->chainBlends[t].get() : nullptr;
        // (a first build — an export, a render, a test — starts at its Blend; a live one from where the audio was: no
        // stage before = 100 %)
        const auto lastWet = before != nullptr ? before->lastWet.load (std::memory_order_relaxed)
                             : previous == nullptr ? std::clamp (track.chainBlend, 0.0f, 1.0f) : 1.0f;
        // (at 100 % no stage, unless the audio is still on its way back from a lower Blend: one more chunk ramps it up)
        if (track.effects.empty() || (track.chainBlend >= 1.0f && lastWet >= 1.0f))
            continue;
        int latency = 0;
        for (std::size_t s = 0; s < track.effects.size() && s < slotLatency[t].size(); ++s)
            if (song.slotActive (track.effects[s]) && track.effects[s].mix > 0.0f)
                latency += std::max (0, slotLatency[t][s]);
        if (previous != nullptr && t < previous->chainBlends.size() && previous->chainBlends[t] != nullptr && previous->chainBlends[t]->latency == latency)
        {
            chainBlends[t] = previous->chainBlends[t];
            continue;
        }
        auto stage = std::make_shared<ChainBlendStage>();
        stage->latency = latency;
        stage->lastWet.store (lastWet, std::memory_order_relaxed);
        stage->warmup = previous != nullptr ? latency : 0;
        if (latency > 0)
            stage->delay = std::make_shared<DelayLine> (latency);
        chainBlends[t] = std::move (stage);
    }

    channelDelays.resize (pdc.channelDelay.size());

    for (std::size_t c = 0; c < pdc.channelDelay.size(); ++c)
        channelDelays[c] = line (pdc.channelDelay[c],
                                 previous != nullptr && c < previous->channelDelays.size() ? &previous->channelDelays[c] : nullptr);

    sendDelays.resize (pdc.sendDelay.size());

    for (std::size_t t = 0; t < pdc.sendDelay.size(); ++t)
    {
        sendDelays[t].resize (pdc.sendDelay[t].size());

        for (std::size_t k = 0; k < pdc.sendDelay[t].size(); ++k)
        {
            const std::shared_ptr<DelayLine>* old = nullptr;

            if (previous != nullptr && t < previous->sendDelays.size() && k < previous->sendDelays[t].size())
                old = &previous->sendDelays[t][k];

            sendDelays[t][k] = line (pdc.sendDelay[t][k], old);
        }
    }

    // round 190hq (B13): the slots' side inputs, delayed to their own input's lateness
    sideLines.assign (pdc.sideDelay.size(), {});
    for (std::size_t t = 0; t < pdc.sideDelay.size(); ++t)
    {
        sideLines[t].resize (pdc.sideDelay[t].size());
        for (std::size_t s = 0; s < pdc.sideDelay[t].size(); ++s)
            for (std::size_t k = 0; k < 4; ++k)
            {
                const auto length = pdc.sideDelay[t][s][k];
                if (length <= 0)
                    continue;
                const RackState::SideLine* old = nullptr;
                if (previous != nullptr && t < previous->sideLines.size() && s < previous->sideLines[t].size())
                    old = previous->sideLines[t][s][k].get();
                if (old != nullptr && old->delay != nullptr && old->delay->length() == length)
                    sideLines[t][s][k] = previous->sideLines[t][s][k];
                else
                {
                    auto line = std::make_shared<SideLine>();
                    line->delay = std::make_shared<DelayLine> (length);
                    sideLines[t][s][k] = std::move (line);
                }
            }
    }

    // inserts with a hardware output target: aligned with the Master's output (which waits for the longest path)
    hwRoutes.clear();
    for (std::size_t t = 1; t < song.mixer.size() && t < (std::size_t) maxMixerTracks; ++t)
    {
        const auto& track = song.mixer[t];
        if (track.outputChannel < 0 || track.outputChannel >= HardwareOutput::maxChannels)
            continue;
        // FL (FUN_010ffca0): only with "Compensate for master latency" on and the track not bypassed
        const auto compensated = track.compensateMasterLatency && track.latencyBypass != LatencyBypass::always;
        const auto length = compensated && t < pdc.outLat.size() ? std::max (0, pdc.total - pdc.outLat[t]) : 0;
        const std::shared_ptr<DelayLine>* old = nullptr;
        if (previous != nullptr)
            for (const auto& r : previous->hwRoutes)
                if (r.track == (int) t)
                    old = &r.delay;
        hwRoutes.push_back ({ (int) t, track.outputChannel, track.outputMode, line (length, old) });
    }
    // round 190nc (DL-110, Live's Cue mode): each cued track's output also to the Cue Out, lined up with Main (the routes
    // exist only while the Cue Out is separate from the Main Out; channels the device lacks are dropped by the callback)
    if (sessionmixer::cueActive (song, HardwareOutput::maxChannels))
    {
        const auto& cue = sessionmixer::cueOf (song);
        for (std::size_t t = 1; t < song.mixer.size() && t < (std::size_t) maxMixerTracks; ++t)
        {
            if (! song.mixer[t].cue)
                continue;
            const auto length = t < pdc.outLat.size() ? std::max (0, pdc.total - pdc.outLat[t]) : 0;
            const std::shared_ptr<DelayLine>* old = nullptr;
            if (previous != nullptr)
                for (const auto& r : previous->hwRoutes)
                    if (r.track == (int) t && r.cue)
                        old = &r.delay;
            hwRoutes.push_back ({ (int) t, cue.out, cue.outMode, line (length, old), true });
        }
    }

    bypassWhenIdle.assign (song.mixer.size(), 0);
    anyBypassWhenIdle = false;
    for (std::size_t t = 1; t < song.mixer.size(); ++t)
        if (song.mixer[t].latencyBypass == LatencyBypass::whenRecordingOrStopped)
            anyBypassWhenIdle = bypassWhenIdle[t] = 1;
}

void RackState::buildMidiRoutes()
{
    const auto count = song.channels.size();
    midiTargets.assign (count, {});
    midiSources.assign (count, {});
    midiOutChannel.assign (count, -1);
    vfxChannels.clear();
    portEventChannels.clear();
    dashboardChannels.clear();
    mapNoteColour.assign (count, 0);
    vfxPort.assign (count, -1);
    hasMidiRoutes = false;
    midiOutTargets.assign (count, {});
    hasMidiOutputs = false;
    hardwareMidiPort.assign (count, -1);
    hardwareMidiChannels.clear();
    for (std::size_t c = 0; c < count && c < (std::size_t) RackRenderer::maxChannels; ++c) // (round 190jl)
        if (song.channels[c].instrument == InstrumentKind::plugin && song.channels[c].plugin.midiOutPort >= 0)
        {
            hardwareMidiPort[c] = std::min (song.channels[c].plugin.midiOutPort, 255);
            hardwareMidiChannels.push_back ((int) c);
        }
    for (std::size_t c = 0; c < count; ++c)
        if (song.channels[c].instrument == InstrumentKind::plugin && song.channels[c].plugin.midiOutPort >= 0)
            for (std::size_t t = 0; t < count; ++t)
                if (t != c && song.channels[t].instrument == InstrumentKind::plugin
                    && song.channels[t].plugin.midiInPort == song.channels[c].plugin.midiOutPort)
                {
                    midiOutTargets[c].push_back ((int) t);
                    hasMidiOutputs = true;
                }
    midiRoutesOut.assign (count, {});
    midiRoutesIn.assign (count, {});
    effectMidiRoutes.assign (count, {});
    hasEffectMidiRoutes = false;

    // the Patchers that take MIDI ports through their "From LYNX" pins (round 162)
    std::vector<std::vector<int>> channelPorts (count);
    for (std::size_t t = 0; t < count; ++t)
        if (song.channels[t].instrument == InstrumentKind::patcher)
            channelPorts[t] = patcher::midiInputPorts (song.channels[t].patcher);
    struct SlotPorts
    {
        int track, slot;
        std::vector<int> ports;
    };
    std::vector<SlotPorts> slotPorts;
    for (std::size_t t = 0; t < song.mixer.size(); ++t)
        for (std::size_t k = 0; k < song.mixer[t].effects.size(); ++k)
            if (const auto& fx = song.mixer[t].effects[k]; fx.kind == EffectKind::patcher)
            {
                if (auto ports = patcher::midiInputPorts (fx.patcher); ! ports.empty())
                    slotPorts.push_back ({ (int) t, (int) k, std::move (ports) });
            }
            else if (const auto port = effectMidiInPort (fx); port >= 0) // round 190ee: Gross Beat, Pitcher
                slotPorts.push_back ({ (int) t, (int) k, { port } });

    // source c sends on `port` with MIDI channel `midiChannel`: the plugins whose input port it is, the Patchers that
    // take that port
    // (a Patcher that takes the port may pass it on to "To LYNX" MIDI ports: those are sent on too, a few levels deep)
    bool portOnly = false; // (the routes sent next carry only what goes out on the port: a Patcher's VFX outputs)
    std::function<void (std::size_t, int, int, int)> send = [&] (std::size_t c, int port, int midiChannel, int depth) {
        auto passOn = [&] (const PatcherDoc& doc) {
            if (depth < 4)
                for (const auto& r : patcher::midiOutputsOf (doc, patchMidiPortPin (port)))
                    if (r.port != port)
                        send (c, r.port, r.midiChannel >= 0 ? r.midiChannel : midiChannel, depth + 1);
        };
        if (midiOutChannel[c] < 0)
            midiOutChannel[c] = midiChannel;
        for (std::size_t t = 0; t < count; ++t)
        {
            if (t == c)
                continue;
            const auto plugin = song.channels[t].instrument == InstrumentKind::plugin && song.channels[t].plugin.midiInPort == port;
            const auto patch = std::find (channelPorts[t].begin(), channelPorts[t].end(), port) != channelPorts[t].end();
            if (! plugin && ! patch)
                continue;
            if (std::any_of (midiRoutesOut[c].begin(), midiRoutesOut[c].end(), [&] (const MidiRoute& r) {
                    return r.other == (int) t && r.port == port && r.midiChannel == midiChannel;
                }))
                continue;
            midiRoutesOut[c].push_back ({ (int) t, port, midiChannel, portOnly });
            midiRoutesIn[t].push_back ({ (int) c, port, midiChannel, portOnly });
            if (std::find (midiSources[t].begin(), midiSources[t].end(), (int) c) == midiSources[t].end())
                midiSources[t].push_back ((int) c);
            if (plugin && ! portOnly && std::find (midiTargets[c].begin(), midiTargets[c].end(), (int) t) == midiTargets[c].end())
                midiTargets[c].push_back ((int) t); // (controls and program changes: the plugins)
            hasMidiRoutes = true;
            if (patch)
                passOn (song.channels[t].patcher);
        }
        for (const auto& s : slotPorts)
            if (std::find (s.ports.begin(), s.ports.end(), port) != s.ports.end()
                && std::none_of (effectMidiRoutes[c].begin(), effectMidiRoutes[c].end(), [&] (const EffectMidiRoute& r) {
                       return r.track == s.track && r.slot == s.slot && r.port == port && r.midiChannel == midiChannel;
                   }))
            {
                effectMidiRoutes[c].push_back ({ s.track, s.slot, port, midiChannel, portOnly });
                hasEffectMidiRoutes = true;
                if (const auto& fx = song.mixer[(std::size_t) s.track].effects[(std::size_t) s.slot]; fx.kind == EffectKind::patcher)
                    passOn (fx.patcher);
            }
    };

    for (std::size_t c = 0; c < count; ++c)
    {
        // a generator Patcher's "MIDI port N" outputs (REA_rodada141): its notes reach the plugins on those ports, as a
        // MIDI Out on MIDI channel 1 would send them (or on the channel of a MIDI Out node on the way, round 162)
        if (song.channels[c].instrument == InstrumentKind::patcher)
        {
            // a standalone MIDI Effect Rack (round 183c, decision R1): what its chains play goes to its children only
            if (const auto* rk = rack::rackOf (song.channels[c].patcher); rk != nullptr && rk->kind == RackDoc::Kind::midiEffect)
            {
                if (! rk->children.empty() && ! patcher::vfxHostPorts (song.channels[c].patcher).empty())
                    portEventChannels.push_back ((int) c);
                continue;
            }
            for (const auto& r : patcher::midiOutputsOf (song.channels[c].patcher, 0))
                send (c, r.port, std::max (0, r.midiChannel), 0);
            // its VFX nodes' outputs cabled to "To LYNX" MIDI ports (round 165c): what they play goes out there
            if (const auto ports = patcher::vfxHostPorts (song.channels[c].patcher); ! ports.empty())
            {
                portEventChannels.push_back ((int) c);
                portOnly = true;
                for (const auto port : ports)
                    send (c, port, 0, 0);
                portOnly = false;
            }
            continue;
        }
        if (song.channels[c].instrument == InstrumentKind::vfx) // a VFX channel: output k on port + k (round 164b)
        {
            if (const auto vs = vfx::readState (song.channels[c].pluginState))
            {
                vfxChannels.push_back ((int) c);
                vfxPort[c] = vs->port;
                for (int k = 0; k < vfx::outputCount (vs->kind) && vs->port + k < patchMidiPorts; ++k)
                    send (c, vs->port + k, 0, 0);
            }
            continue;
        }
        if (song.channels[c].instrument == InstrumentKind::dashboard) // its notes and controls' MIDI (round 165)
        {
            const auto settings = surface::dashboardMidi (song.channels[c].pluginState);
            midiOutChannel[c] = settings.channel;
            dashboardChannels.push_back ((int) c);
            send (c, settings.port, settings.channel, 0);
            continue;
        }
        if (song.channels[c].instrument != InstrumentKind::midiOut)
            continue;
        // its knobs' MIDI (round 167i) goes out like a Dashboard's
        const auto settings = midiout::stateOf (song.channels[c].pluginState);
        midiOutChannel[c] = settings.channel;
        mapNoteColour[c] = settings.mapNoteColour ? 1 : 0;
        dashboardChannels.push_back ((int) c);
        send (c, settings.port, settings.channel, 0);
        if (c < (std::size_t) RackRenderer::maxChannels) // the hardware outputs on its port (round 190jl)
        {
            hardwareMidiPort[c] = std::clamp (settings.port, 0, 255);
            hardwareMidiChannels.push_back ((int) c);
        }
    }
}

// round 190ow (details DE-123, core/RackAutomation.h): FL's triggered automation clips. A step / note on an automation
// clip row starts the clip from its beginning and plays it while the note is held (FL's manual: "Triggering"); the clip
// then holds the value it reached. In Song mode each pattern clip of the Playlist plays its pattern's steps / notes
// where its notes play (the clip's offset and its passes when it loops); a note the clip's end cuts plays on to its own
// end, as FL does (round 190ow's FL /R probes p13_cut, p15_cutnote).
static std::int64_t triggerLength (const Automation& a, const Note& n) noexcept
{
    return n.length > 0 ? n.length : a.length(); // (FL's step of no length: the clip once, to its last point)
}

void RackState::buildTriggerClips (int count)
{
    triggerClips.clear();
    triggered.clear();
    patternTriggers = false;
    for (const auto& p : song.patterns)
        patternTriggers = patternTriggers || ! p.autoNotes.empty();
    if (! patternTriggers)
        return;
    triggerClips.assign ((std::size_t) count, {});
    triggered.assign ((std::size_t) count, 0);
    for (const auto& p : song.patterns)
        for (const auto& n : p.autoNotes)
            if (n.channel >= 0 && n.channel < count && ! n.muted)
                triggered[(std::size_t) n.channel] = 1;
    const auto meter = song.meter();
    const auto zoneEnd = song.performanceZoneEnd();
    for (const auto& c : song.playlist)
    {
        if (c.kind != ClipKind::pattern || c.pattern < 0 || c.pattern >= (int) song.patterns.size() || c.length <= 0 || c.muted
            || song.isTrackMuted (c.track) || c.start < zoneEnd)
            continue;
        const auto& pattern = song.patterns[(std::size_t) c.pattern];
        if (pattern.autoNotes.empty())
            continue;
        const auto passes = clipPasses (c, pattern.effectiveLength (meter));
        for (auto k = passes.first; k <= passes.last; ++k)
            for (const auto& n : pattern.autoNotes)
            {
                if (n.muted || n.channel < 0 || n.channel >= count || (passes.loops && n.start >= passes.length))
                    continue;
                const auto at = c.start - c.offset + (passes.loops ? k * passes.length : 0) + n.start;
                if (at < c.start || at >= c.end())
                    continue;
                Clip t;
                t.kind = ClipKind::automation;
                t.automation = n.channel;
                t.start = at;
                // (not cut at the pattern clip's end: FL plays the note on, FL 2026 /R probes p13_cut / p15_cutnote)
                t.length = std::max<std::int64_t> (1, triggerLength (song.automations[(std::size_t) n.channel], n));
                triggerClips[(std::size_t) n.channel].push_back (t);
            }
    }
    for (auto& clips : triggerClips)
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

void RackState::buildAutomationTables()
{
    const auto& automations = song.automations;
    const auto count = std::min<int> ((int) automations.size(), maxAutomations);

    automationClips.assign ((std::size_t) count, {});
    zoneAutomations.assign ((std::size_t) count, 0);
    const auto zoneEnd = song.performanceZoneEnd();

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];

        if (c.kind == ClipKind::automation && c.automation >= 0 && c.automation < count && c.length > 0 && ! c.muted && ! song.isTrackMuted (c.track))
        {
            if (c.start < zoneEnd) // Performance mode: plays only when launched
                zoneAutomations[(std::size_t) c.automation] = 1;
            else
                automationClips[(std::size_t) c.automation].push_back (i);
        }
    }

    for (auto& clips : automationClips)
        std::sort (clips.begin(), clips.end(),
                   [this] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });

    // round 190ow (DE-123): the clips the automation clip rows' steps / notes start, and FL's mute light of a clip
    buildTriggerClips (count);
    for (int a = 0; a < count; ++a)
        if (automations[(std::size_t) a].muted)
        {
            automationClips[(std::size_t) a].clear();
            if ((std::size_t) a < triggerClips.size())
                triggerClips[(std::size_t) a].clear();
            if ((std::size_t) a < triggered.size())
                triggered[(std::size_t) a] = 0;
        }

    // round 177b: the lanes of unmuted tracks; a lane automation's own clips (if any) don't play as clips
    laneAutomations.clear();
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        for (const auto& lane : song.playlistTracks[(std::size_t) t].lanes)
            if (lane.automation >= 0 && lane.automation < count && ! song.isTrackMuted (t)
                && ! automations[(std::size_t) lane.automation].points.empty())
            {
                automationClips[(std::size_t) lane.automation].clear();
                if ((std::size_t) lane.automation < triggerClips.size()) // (round 190ow)
                    triggerClips[(std::size_t) lane.automation].clear(), triggered[(std::size_t) lane.automation] = 0;
                laneAutomations.push_back ({ lane.automation, {} });
            }
    for (auto& lane : laneAutomations)
        for (int a = 0; a < count; ++a)
            if (a != lane.automation && ! automationClips[(std::size_t) a].empty()
                && automations[(std::size_t) a].target == automations[(std::size_t) lane.automation].target)
                lane.clipPeers.push_back (a);

    liveAutomations.clear();
    for (int a = 0; a < count; ++a)
        if (automations[(std::size_t) a].isLive())
            liveAutomations.push_back (a);

    channelVolumeAuto.assign (song.channels.size(), -1);
    channelPanAuto.assign (song.channels.size(), -1);
    channelAdjustAuto.assign (song.channels.size(), { -1, -1 });
    mixerVolumeAuto.assign (song.mixer.size(), -1);
    mixerPanAuto.assign (song.mixer.size(), -1);
    vcaLevelAuto.assign (song.vcas.size(), -1);
    effectParamAuto.assign (song.mixer.size(), {});
    effectMixAuto.assign (song.mixer.size(), {});

    for (std::size_t t = 0; t < song.mixer.size(); ++t)
    {
        std::array<int, maxEffectParams> none;
        none.fill (-1);
        effectParamAuto[t].assign (song.mixer[t].effects.size(), none);
        effectMixAuto[t].assign (song.mixer[t].effects.size(), -1);
    }

    pluginParams.clear();
    routeAutos.clear();
    modFeeds.assign (song.modulators.size(), {}); // (round 190ox: before the binds, which note a Macro's automation)
    followedTracks.clear();
    modStateful = false;
    hasAutomation = false;
    tempoAutomation = -1;
    crossfaderAutomation = -1;

    // binds automation slot `a` (an automation, or a pattern event lane after them) to its target
    const auto bind = [this] (int a, const AutomationTarget& t) {
        const auto channelOk = t.index >= 0 && t.index < (int) song.channels.size();
        const auto trackOk = t.index >= 0 && t.index < (int) song.mixer.size();
        auto used = true;

        switch (t.kind)
        {
            case AutomationTargetKind::channelVolume: if (channelOk) channelVolumeAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::channelPan: if (channelOk) channelPanAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::channelAdjust: // (pan and volume move live; Mod X / Y offsets are not automatable yet)
                if (channelOk && (t.param == leveladjust::pan || t.param == leveladjust::volume))
                    channelAdjustAuto[(std::size_t) t.index][(std::size_t) t.param] = a;
                else
                    used = false;
                break;
            case AutomationTargetKind::mixerVolume: if (trackOk) mixerVolumeAuto[(std::size_t) t.index] = a; else used = false; break;
            case AutomationTargetKind::mixerPan: if (trackOk) mixerPanAuto[(std::size_t) t.index] = a; else used = false; break;

            case AutomationTargetKind::channelParam:
                // VST plugins and the exact engines that take FL's parameter automation (FL Slayer: REA_rodada79)
                if (channelOk && (song.channels[(std::size_t) t.index].instrument == InstrumentKind::plugin
                                  || song.channels[(std::size_t) t.index].instrument == InstrumentKind::patcher // (its published parameters)
                                  || song.channels[(std::size_t) t.index].instrument == InstrumentKind::slayer
                                  || song.channels[(std::size_t) t.index].instrument == InstrumentKind::soundfont // (LYNX Soundfont's knobs)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::midiOut
                                      && (midictl::isMidiParam (t.param) || (t.param >= 0 && t.param <= midiout::controlCount)))
                                  // a recorded / drawn pitch bend plays on any instrument (issue #12)
                                  || midictl::isPitchBend (t.param)
                                  // an Envelope Controller's global Mod X / Y (round 164f)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::controller
                                      && (t.param == envelopeControllerModXParam || t.param == envelopeControllerModYParam))
                                  // a Dashboard's outputs, the Voltage Controller's offsets / scales (round 165)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::dashboard && t.param >= 0
                                      && t.param < surface::maxOutputs)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::voltage && t.param >= 0
                                      && t.param < voltage::paramCount)
                                  // the Sampler's knobs (issue #12)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::sampler && t.param >= 0
                                      && t.param < samplerparam::count)
                                  // the knobs of an FL engine kept in the channel's state (round 168n)
                                  || modelparams::find (song.channels[(std::size_t) t.index].instrument, t.param) != nullptr
                                  // Sytrus' main page (round 190pi: master level / LFO / pitch, Mod X / Y)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::sytrus && sytrus::automatable (t.param))
                                  // Harmor's filter frequencies and Mod X / Y / Z (round 190pr; engine revision 3 on)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::harmor
                                      && song.channels[(std::size_t) t.index].engineRev >= 3 && harmor::automatable (t.param))
                                  // LYNX Synth's knobs (issue #152, round 190he)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::synth && synthparam::valid (t.param))
                                  // FL Studio Mobile's DW Sampler: its own 34 values (round 190de)
                                  || (flmdw::isChannel (song.channels[(std::size_t) t.index]) && t.param >= flmdw::automationBase
                                      && t.param < flmdw::automationBase + flmdw::paramCount)
                                  || (flmslicer::isChannel (song.channels[(std::size_t) t.index]) && t.param >= flmslicer::automationBase
                                      && t.param < flmslicer::automationBase + flmslicer::paramCount)
                                  // FL Studio Mobile's 3x Osc: FLM's own values of its parameters (an imported .flm's lanes, round 190fe)
                                  || (threeOscIsFlm (song.channels[(std::size_t) t.index]) && t.param > threeosc::flmParamBase
                                      && threeosc::isFlmTarget (t.param - threeosc::flmParamBase))
                                  // FL Studio Mobile's GMSynth: its own 75 values P[k] (an imported .flm's lanes, round 190de)
                                  || (song.channels[(std::size_t) t.index].instrument == InstrumentKind::gms
                                      && song.channels[(std::size_t) t.index].modelParams.size() == 1
                                      && song.channels[(std::size_t) t.index].modelParams[0] == 1.0f && t.param >= 0 && t.param < 75)))
                    pluginParams.push_back ({ a, t.index, 0, 0, t.param });
                else
                    used = false;
                break;

            case AutomationTargetKind::effectParam:
            {
                const auto slotOk = trackOk && t.slot >= 0 && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size();

                if (! slotOk)
                    used = false;
                else if (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind == EffectKind::plugin
                         || song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].kind == EffectKind::patcher) // (its published parameters)
                    pluginParams.push_back ({ a, -1, t.index, t.slot, t.param });
                else if (song.mixer[(std::size_t) t.index].effects[(std::size_t) t.slot].isSubstitute())
                    used = false; // the missing plugin's parameters: kept for when it is back, they don't drive the substitute
                else if (t.param >= 0 && t.param < maxEffectParams)
                    effectParamAuto[(std::size_t) t.index][(std::size_t) t.slot][(std::size_t) t.param] = a;
                else
                    used = false;
                break;
            }

            case AutomationTargetKind::effectMix:
                if (trackOk && t.slot >= 0 && t.slot < (int) song.mixer[(std::size_t) t.index].effects.size())
                    effectMixAuto[(std::size_t) t.index][(std::size_t) t.slot] = a;
                else
                    used = false;
                break;

            case AutomationTargetKind::tempo: tempoAutomation = a; break;
            case AutomationTargetKind::crossfader: crossfaderAutomation = a; break; // (round 190kc)
            case AutomationTargetKind::modulatorMacro: // (round 190ox: a Macro modulator's knob, read by updateModulators)
                if (t.index >= 0 && t.index < (int) modFeeds.size() && song.modulators[(std::size_t) t.index].kind == ModKind::macro)
                    modFeeds[(std::size_t) t.index].macroSlot = a;
                else
                    used = false;
                break;
            case AutomationTargetKind::vcaLevel:
                if (t.index >= 0 && t.index < (int) song.vcas.size())
                    vcaLevelAuto[(std::size_t) t.index] = a;
                else
                    used = false;
                break;
            case AutomationTargetKind::mixerSend:
                if (trackOk && song.routes (t.index, t.slot))
                {
                    // the last one of the same route wins (as the loop's order makes it for the other targets)
                    auto it = std::find_if (routeAutos.begin(), routeAutos.end(), [&t] (const RouteAuto& r) { return r.from == t.index && r.to == t.slot; });
                    if (it == routeAutos.end())
                        routeAutos.push_back ({ t.index, t.slot, a });
                    else
                        it->slot = a;
                }
                else
                    used = false;
                break;
            case AutomationTargetKind::none: used = false; break;
        }

        hasAutomation = hasAutomation || used;
    };
    hasAutomation = song.automationChunks; // (round 190ff: a render song standing in for one with automation)

    // issue #97: the automations with clips on one target share a slot, the clip that plays decides (automationValueAt)
    automationGroups.assign ((std::size_t) count, {});
    automationFollowers.assign ((std::size_t) count, 0);
    const auto playsClips = [&] (int a) {
        const auto& automation = automations[(std::size_t) a];
        return driven (a) && ! automation.isLive() && zoneAutomations[(std::size_t) a] == 0
               && automation.target.kind != AutomationTargetKind::none && automation.links.empty(); // (190gw: links bind below)
    };
    for (int a = count - 1; a > 0; --a)
    {
        if (automationFollowers[(std::size_t) a] != 0 || ! playsClips (a))
            continue;
        auto& group = automationGroups[(std::size_t) a];
        for (int b = 0; b < a; ++b)
            if (playsClips (b) && automations[(std::size_t) b].target == automations[(std::size_t) a].target)
            {
                group.push_back (b);
                automationFollowers[(std::size_t) b] = 1;
            }
        if (! group.empty())
            group.push_back (a);
    }

    // round 190ow: where a triggered automation's value goes (Pattern mode writes it there; Song mode scans the group)
    triggerSlot.assign ((std::size_t) count, -1);
    if (! triggerClips.empty() || patternTriggers)
        for (int a = 0; a < count; ++a)
        {
            triggerSlot[(std::size_t) a] = a;
            if (automationFollowers[(std::size_t) a] != 0)
                for (int holder = a + 1; holder < count; ++holder)
                    if (std::find (automationGroups[(std::size_t) holder].begin(), automationGroups[(std::size_t) holder].end(), a)
                        != automationGroups[(std::size_t) holder].end())
                    {
                        triggerSlot[(std::size_t) a] = holder;
                        break;
                    }
        }

    // a lane takes the value of its peers' group: a follower computes none, the automation that holds the slot does
    for (auto& lane : laneAutomations)
        for (auto& peer : lane.clipPeers)
            if (automationFollowers[(std::size_t) peer] != 0)
                for (int a = 0; a < count; ++a)
                    if (std::find (automationGroups[(std::size_t) a].begin(), automationGroups[(std::size_t) a].end(), peer)
                        != automationGroups[(std::size_t) a].end())
                    {
                        lane.clipSlots.push_back ({ peer, a });
                        break;
                    }
    // round 190ov: the targets of target links (FL's clips linked to several controls), each with every clip that drives
    // it — the links on it and the automations of that one target. A target with more than one plays as one slot (below);
    // those automations of one target bind nothing themselves.
    struct SharedTarget
    {
        AutomationTarget target;
        std::vector<std::pair<int, int>> members; // (automation, link; −1 = the automation's own target)
    };
    std::vector<SharedTarget> linkTargets;
    for (int a = 0; a < count; ++a)
        if (const auto& automation = automations[(std::size_t) a]; ! automation.links.empty() && ! automation.isLive())
            for (int l = 0; l < (int) automation.links.size(); ++l)
            {
                const auto& t = automation.links[(std::size_t) l].target;
                auto it = std::find_if (linkTargets.begin(), linkTargets.end(), [&t] (const SharedTarget& s) { return s.target == t; });
                if (it == linkTargets.end())
                    it = linkTargets.insert (linkTargets.end(), SharedTarget { t, {} });
                it->members.push_back ({ a, l });
            }
    for (auto& shared : linkTargets)
    {
        if (shared.target.kind == AutomationTargetKind::none)
            continue;
        for (int b = 0; b < count; ++b)
            if (playsClips (b) && automations[(std::size_t) b].target == shared.target)
            {
                shared.members.push_back ({ b, -1 });
                automationFollowers[(std::size_t) b] = 1;
                automationGroups[(std::size_t) b].clear();
            }
        std::sort (shared.members.begin(), shared.members.end()); // (ascending: the lowest automation keeps a tie)
    }

    // otherwise the last automation of the same target wins (a live controller link, Performance mode clips)
    for (int a = 0; a < count; ++a)
    {
        if (automationFollowers[(std::size_t) a] != 0
            || (! driven (a) && ! automations[(std::size_t) a].isLive() && zoneAutomations[(std::size_t) a] == 0)
            || ! automations[(std::size_t) a].links.empty()) // (round 190gw: through its link slots, below)
            continue;

        bind (a, automations[(std::size_t) a].target);
    }
    // a lane binds its target last: it carries the clips of the same target where they play (render)
    for (const auto& lane : laneAutomations)
        bind (lane.automation, automations[(std::size_t) lane.automation].target);

    // pattern events (event editor): slots after the automations; they win over automation clips of the same
    // target. The tempo is not driven by pattern events.
    eventLanes.clear();
    patternClips.assign (song.patterns.size(), {});

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size() && c.length > 0 && ! c.muted
            && ! song.isTrackMuted (c.track))
            patternClips[(std::size_t) c.pattern].push_back (i);
    }

    for (auto& clips : patternClips)
        std::sort (clips.begin(), clips.end(),
                   [this] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });

    auto slot = count;

    for (int p = 0; p < (int) song.patterns.size(); ++p)
        for (int l = 0; l < (int) song.patterns[(std::size_t) p].events.size(); ++l)
        {
            const auto& lane = song.patterns[(std::size_t) p].events[(std::size_t) l];

            if (lane.events.empty() || lane.target.kind == AutomationTargetKind::tempo || slot >= maxAutomations)
                continue;

            eventLanes.push_back ({ p, l, slot, -1 });
            bind (slot++, lane.target);
        }
    // round 190om: lanes of several patterns on one target (the last one is bound to it): grouped, the bound slot first
    sharedLanes.clear();
    {
        std::map<std::tuple<int, int, int, int>, std::vector<int>> byTarget;
        for (int i = (int) eventLanes.size() - 1; i >= 0; --i)
        {
            const auto& t = song.patterns[(std::size_t) eventLanes[(std::size_t) i].pattern].events[(std::size_t) eventLanes[(std::size_t) i].lane].target;
            byTarget[{ (int) t.kind, t.index, t.slot, t.param }].push_back (i);
        }
        for (auto& [target, group] : byTarget)
        {
            if (group.size() < 2)
                continue;
            for (const auto k : group)
                eventLanes[(std::size_t) k].shared = eventLanes[(std::size_t) group.front()].slot;
            sharedLanes.push_back (std::move (group));
        }
    }

    // round 190gw: the target links of an automation clip with several (FL's "Crossfade with" clip): one slot each;
    // round 190ov: one per target, shared with the other clips that drive it (linkTargets above)
    linkSlots.clear();
    for (const auto& shared : linkTargets)
    {
        if (slot >= maxAutomations)
            break;
        const auto& [first, firstLink] = shared.members.front();
        LinkSlot made;
        made.slot = slot;
        if (shared.members.size() == 1)
        {
            const auto& link = automations[(std::size_t) first].links[(std::size_t) firstLink];
            made.automation = first;
            made.formula = linkformula::Formula::compile (link.formula);
            made.scale = link.scale;
        }
        else
        {
            made.automation = -1;
            for (const auto& [a, l] : shared.members)
            {
                LinkMember member;
                member.automation = a;
                member.link = l;
                if (l >= 0)
                {
                    const auto& link = automations[(std::size_t) a].links[(std::size_t) l];
                    member.formula = linkformula::Formula::compile (link.formula);
                    member.scale = link.scale;
                }
                made.members.push_back (std::move (member));
            }
        }
        linkSlots.push_back (std::move (made));
        bind (slot++, shared.target);
    }

    // round 177b: modulators, after everything else (a modulated target follows its automation or its own value)
    modRoutes.clear();
    const auto boundSlot = [this] (const AutomationTarget& t) {
        const auto channelOk = t.index >= 0 && t.index < (int) song.channels.size();
        const auto trackOk = t.index >= 0 && t.index < (int) song.mixer.size();
        switch (t.kind)
        {
            case AutomationTargetKind::channelVolume: return channelOk ? channelVolumeAuto[(std::size_t) t.index] : -1;
            case AutomationTargetKind::channelPan: return channelOk ? channelPanAuto[(std::size_t) t.index] : -1;
            case AutomationTargetKind::channelAdjust:
                return channelOk && (t.param == leveladjust::pan || t.param == leveladjust::volume)
                           ? channelAdjustAuto[(std::size_t) t.index][(std::size_t) t.param]
                           : -1;
            case AutomationTargetKind::mixerVolume: return trackOk ? mixerVolumeAuto[(std::size_t) t.index] : -1;
            case AutomationTargetKind::mixerPan: return trackOk ? mixerPanAuto[(std::size_t) t.index] : -1;
            case AutomationTargetKind::effectParam:
                return trackOk && t.slot >= 0 && t.slot < (int) effectParamAuto[(std::size_t) t.index].size() && t.param >= 0 && t.param < maxEffectParams
                           ? effectParamAuto[(std::size_t) t.index][(std::size_t) t.slot][(std::size_t) t.param]
                           : -1;
            case AutomationTargetKind::effectMix:
                return trackOk && t.slot >= 0 && t.slot < (int) effectMixAuto[(std::size_t) t.index].size() ? effectMixAuto[(std::size_t) t.index][(std::size_t) t.slot] : -1;
            case AutomationTargetKind::mixerSend:
                for (const auto& r : routeAutos)
                    if (r.from == t.index && r.to == t.slot)
                        return r.slot;
                return -1;
            case AutomationTargetKind::channelParam:
            {
                auto found = -1;
                for (const auto& p : pluginParams)
                    if (p.channel == t.index && p.param == t.param)
                        found = p.automation;
                return found;
            }
            case AutomationTargetKind::tempo:
            case AutomationTargetKind::crossfader:
            case AutomationTargetKind::modulatorMacro:
            case AutomationTargetKind::none: break;
        }
        return -1;
    };
    // round 190ox: a hosted plugin's parameter is bound in pluginParams (an effect slot's too)
    const auto hostedSlot = [this] (const AutomationTarget& t) {
        auto found = -1;
        for (const auto& p : pluginParams)
            if (t.kind == AutomationTargetKind::channelParam ? (p.channel == t.index && p.param == t.param)
                                                             : (p.channel < 0 && p.track == t.index && p.slot == t.slot && p.param == t.param))
                found = p.automation;
        return found;
    };
    for (int m = 0; m < (int) song.modulators.size(); ++m)
        for (const auto& route : song.modulators[(std::size_t) m].routes)
        {
            if (slot >= maxAutomations || route.depth == 0.0f || ! Modulator::canModulate (song, route.target))
                continue;
            const auto hosted = Modulator::isHostedTarget (song, route.target);
            const auto source = hosted ? hostedSlot (route.target) : boundSlot (route.target);
            if (hosted)
            {
                if (source < 0 && route.base < 0.0f)
                    continue; // (no base known: the plugin keeps its own value)
                // the modulated value is the one the plugin gets: the automation's own binding sends nothing any more
                // (its slot still computes the base), so the two never take turns within a block
                const auto& t = route.target;
                pluginParams.erase (std::remove_if (pluginParams.begin(), pluginParams.end(),
                                                    [&t] (const PluginParamBinding& p) {
                                                        return t.kind == AutomationTargetKind::channelParam
                                                                   ? (p.channel == t.index && p.param == t.param)
                                                                   : (p.channel < 0 && p.track == t.index && p.slot == t.slot && p.param == t.param);
                                                    }),
                                    pluginParams.end());
            }
            const auto base = hosted ? std::clamp (route.base, 0.0f, 1.0f) : std::clamp (song.modulationBase (route.target), 0.0f, 1.0f);
            modRoutes.push_back ({ slot, source, base, m, route.depth, hosted });
            bind (slot++, route.target);
        }
    // round 190ox: what feeds each routed modulator (resolved here, so the audio thread only reads)
    for (const auto& r : modRoutes)
    {
        const auto& mod = song.modulators[(std::size_t) r.modulator];
        auto& feed = modFeeds[(std::size_t) r.modulator];
        const auto hostChannel = mod.host.kind == AutomationTargetKind::channelVolume && mod.host.index >= 0 && mod.host.index < (int) song.channels.size()
                                     ? mod.host.index : -1;
        switch (mod.kind)
        {
            case ModKind::follower:
            {
                auto t = -1;
                if (mod.input.kind == AutomationTargetKind::mixerVolume)
                    t = mod.input.index;
                else if (mod.host.kind == AutomationTargetKind::mixerVolume)
                    t = mod.host.index;
                else if (hostChannel >= 0)
                    t = song.channels[(std::size_t) hostChannel].mixerTrack;
                if (t >= 0 && t < (int) song.mixer.size() && t < maxMixerTracks)
                {
                    feed.track = t;
                    if (followedTracks.size() < song.mixer.size())
                        followedTracks.assign (song.mixer.size(), 0);
                    followedTracks[(std::size_t) t] = 1;
                }
                modStateful = true;
                break;
            }
            case ModKind::envelope:
            case ModKind::keytrack:
            case ModKind::velocity:
            {
                const auto c = mod.input.kind == AutomationTargetKind::channelVolume ? mod.input.index : hostChannel;
                feed.channel = c >= 0 && c < (int) song.channels.size() ? c : -1;
                modStateful = true;
                break;
            }
            case ModKind::lfo:
            case ModKind::steps:
            case ModKind::random:
            case ModKind::macro: break;
        }
    }

    // the tempo can move while playing: tempo automation or FL's pattern Tempo events (issue #86)
    tempoMap = tempoAutomation >= 0 || tempomap::hasTempoEvents (song);
}

void RackState::slotsOfTarget (const Song& song, const AutomationTarget& target, std::vector<int>& out)
{
    out.clear();
    if (target.kind == AutomationTargetKind::none)
        return;
    const auto count = std::min<int> ((int) song.automations.size(), maxAutomations);
    for (int a = 0; a < count; ++a)
        if (song.automations[(std::size_t) a].target == target)
            out.push_back (a);

    auto slot = count;
    for (const auto& pattern : song.patterns)
        for (const auto& lane : pattern.events)
        {
            if (lane.events.empty() || lane.target.kind == AutomationTargetKind::tempo || slot >= maxAutomations)
                continue;
            if (lane.target == target)
                out.push_back (slot);
            ++slot;
        }
}

namespace
{
// a level as the UI last set it (RackState::live, round 171c); the snapshot's own value without one
float liveLevel (const RackState& state, std::vector<std::atomic<float>> LiveLevels::*values, int index, float fallback) noexcept
{
    if (state.live == nullptr || index < 0)
        return fallback;
    const auto& v = (*state.live).*values;
    return (std::size_t) index < v.size() ? v[(std::size_t) index].load (std::memory_order_relaxed) : fallback;
}
} // namespace

MixerMatrix mixerMatrix (const MixerTrack& track, float volume, float pan, bool triangular) noexcept
{
    return mixerMatrix (track, volume, pan, triangular, track.stereo);
}

MixerMatrix mixerMatrix (const MixerTrack& track, float volume, float pan, bool triangular, float stereo) noexcept
{
    // FL keeps the fader as an int (12800 = 0 dB), the pan ±6400 and the separation ±64; LYNX keeps them /16000,
    // /6400 and /64. Float arithmetic as FL's, the circular kernel in x87 extended precision (flvp::panLaw).
    const auto x = std::clamp (volume, 0.0f, 1.0f) * 1.25f;
    const auto p = std::clamp (pan, -1.0f, 1.0f);
    const auto sep = std::clamp (stereo, -1.0f, 1.0f);
    MixerMatrix m;
    if (x == 1.0f && p == 0.0f && sep == 0.0f) // FL's fast path: exactly unity
    {
        m.ll = m.rr = track.invertPhase ? -1.0f : 1.0f;
        m.rl = m.lr = 0.0f;
    }
    else if (x == 0.0f)
        m = { 0.0f, 0.0f, 0.0f, 0.0f };
    else
    {
        constexpr float ln11 = 2.3978953f, sqrt2 = 1.4142135f, sin45 = 0.70710677f;
        auto g = x == 1.0f ? 1.0f : (float) ((std::exp ((double) (x * ln11)) - 1.0) * (double) 0.1f);
        if (track.invertPhase)
            g = -g;
        float gl, gr;
        if (triangular) // FUN_006a39c0: the louder side keeps the level
        {
            const auto v = g * sqrt2 * sin45;
            if (std::signbit (p))
                gl = v, gr = (p + 1.0f) * v;
            else
                gr = v, gl = (1.0f - p) * v;
        }
        else
            flvp::panLaw (p, g * sqrt2, gl, gr);
        float fl = 0.0f, fr = 0.0f; // the share of each side that folds into the other
        if (p != 0.0f)
        {
            if (p < 0.0f)
                fl = 1.0f - gr / g;
            else
                fr = 1.0f - gl / g;
        }
        if (sep != 0.0f)
        {
            // narrowing (5^s − 1)/4 (up to mono), widening linear (−1: side ×2)
            const auto s = sep < 0.0f ? sep : (float) (std::exp ((double) sep * std::log (5.0)) - 1.0) * 0.25f;
            fl = std::min (fl + s, 1.0f);
            fr = std::min (fr + s, 1.0f);
        }
        m.rl = gl * fl * 0.5f;
        m.ll = gl * (1.0f - fl * 0.5f);
        m.lr = gr * fr * 0.5f;
        m.rr = gr * (1.0f - fr * 0.5f);
    }
    if (track.swapChannels) // the outputs trade places
        m = { m.lr, m.rr, m.ll, m.rl };
    return m;
}

namespace
{
// The matrix over a block, each coefficient gliding toward its target by at most 0.001 per sample (FL's fader / pan
// glide, REA_rodada85 §3; the folded terms glide the same way). `from` = the previous block's matrix (NaN: none).
// The output may be the input. Returns the matrix reached.
MixerMatrix applyMatrix (const float* inL, const float* inR, float* outL, float* outR, int n, MixerMatrix from,
                         const MixerMatrix& to) noexcept
{
    if (std::isnan (from.ll))
        from = to;
    constexpr float step = 0.001f;
    auto toward = [] (float g, float target) { return target > g ? std::min (target, g + step) : std::max (target, g - step); };
    auto m = from;
    int i = 0;
    for (; i < n && ! (m == to); ++i)
    {
        m = { toward (m.ll, to.ll), toward (m.rl, to.rl), toward (m.lr, to.lr), toward (m.rr, to.rr) };
        const auto a = inL[i], b = inR[i];
        outL[i] = m.ll * a + m.rl * b;
        outR[i] = m.rr * b + m.lr * a;
    }
    if (i < n)
    {
        if (m.rl == 0.0f && m.lr == 0.0f) // no fold: two gains (the common case)
            for (int k = i; k < n; ++k)
            {
                outL[k] = inL[k] * m.ll;
                outR[k] = inR[k] * m.rr;
            }
        else
            for (int k = i; k < n; ++k)
            {
                const auto a = inL[k], b = inR[k];
                outL[k] = m.ll * a + m.rl * b;
                outR[k] = m.rr * b + m.lr * a;
            }
    }
    return m;
}
} // namespace

namespace
{
// Constant-power pan: at center both sides are 1 (0 dB).
void panGains (float pan, float gain, float& left, float& right) noexcept
{
    const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    left = gain * std::cos (angle) * 1.41421356f;
    right = gain * std::sin (angle) * 1.41421356f;
}

// A hosted plugin channel in a project with FL's levels (Song::flPluginLevels, REA_rodada190ad.md): Fruity Wrapper's
// output gains are the host's ComputeLRVol of (the channel's pan, 2 × its volume curve) — FL's pan kernel (circular
// cos/sin, or the project's triangular law: the louder side keeps v/√2, the other falls linearly), so the centre is
// √2 × the curve and the side panned to 2 ×. The wrapper gets the pan as an integer, −64…64 (FL's renders: a channel
// pan of 1.5 / 64 and 2.5 / 64 play as 1 / 64 and 2 / 64, 0.4 / 64 as the centre), so the pan moves in steps of 1 / 64.
void flWrapperGains (float pan, float gain, bool triangular, float& left, float& right) noexcept
{
    const auto p = std::trunc (std::clamp (pan, -1.0f, 1.0f) * 64.0f) / 64.0f;
    const auto vol = gain * 2.0f;
    if (triangular)
    {
        const auto v = vol * 0.70710677f;
        if (std::signbit (p))
            left = v, right = (p + 1.0f) * v;
        else
            right = v, left = (1.0f - p) * v;
    }
    else
        flvp::panLaw (p, vol, left, right);
}

float peakOf (const float* data, int n) noexcept
{
    float peak = 0.0f;
    int i = 0;
#if defined(LYNX_X86)
    // round 190ge: four lanes at a time. The scalar form below stayed one serial chain of maxss on every compiler (it
    // runs for each insert and meter, every block). The same peak: a maximum is exact in any order, and a NaN is
    // skipped in both forms (max (NaN, m) keeps m, as `a > peak` is false for a NaN)
    if (n >= 8)
    {
        const auto mask = _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff));
        auto m0 = _mm_setzero_ps(), m1 = _mm_setzero_ps();
        for (; i + 8 <= n; i += 8)
        {
            m0 = _mm_max_ps (_mm_and_ps (_mm_loadu_ps (data + i), mask), m0);
            m1 = _mm_max_ps (_mm_and_ps (_mm_loadu_ps (data + i + 4), mask), m1);
        }
        alignas (16) float lanes[4];
        _mm_store_ps (lanes, _mm_max_ps (m0, m1));
        for (const auto a : lanes)
            peak = a > peak ? a : peak;
    }
#endif
    for (; i < n; ++i)
    {
        const auto a = std::abs (data[i]);
        peak = a > peak ? a : peak; // form the compiler vectorizes
    }

    return peak;
}

// Round 190lr (DL-108): the block's sum of squares, for the Session mixer's RMS bars (only while they show); four
// partial sums the compiler can keep in lanes
float squaresOf (const float* data, int n) noexcept
{
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        s0 += data[i] * data[i];
        s1 += data[i + 1] * data[i + 1];
        s2 += data[i + 2] * data[i + 2];
        s3 += data[i + 3] * data[i + 3];
    }
    for (; i < n; ++i)
        s0 += data[i] * data[i];
    const auto sum = (s0 + s1) + (s2 + s3);
    return std::isfinite (sum) ? sum : 0.0f;
}

void meterPower (MeterBank* meters, int track, const float* l, const float* r, int n) noexcept
{
    if (meters != nullptr && meters->powerWanted.load (std::memory_order_relaxed))
        meters->accumulatePower (track, squaresOf (l, n), squaresOf (r, n), n);
}

// No NaN / inf in the block? Round 190ge: by the exponent bits (all ones only for an inf or a NaN), four lanes at a
// time. (The form before, x · 0 summed — 0 for every finite x — was one serial chain of float additions on every
// compiler: the order of a float sum may not change. It ran after every effect slot, every block.)
bool finiteBlock (const float* l, const float* r, int n) noexcept
{
    int k = 0;
    auto bad = false;
#if defined(LYNX_X86)
    {
        const auto exponent = _mm_set1_epi32 (0x7f800000);
        auto acc = _mm_setzero_si128();
        for (; k + 4 <= n; k += 4)
        {
            const auto a = _mm_and_si128 (_mm_castps_si128 (_mm_loadu_ps (l + k)), exponent);
            const auto b = _mm_and_si128 (_mm_castps_si128 (_mm_loadu_ps (r + k)), exponent);
            acc = _mm_or_si128 (acc, _mm_or_si128 (_mm_cmpeq_epi32 (a, exponent), _mm_cmpeq_epi32 (b, exponent)));
        }
        bad = _mm_movemask_epi8 (acc) != 0;
    }
#endif
    for (; k < n; ++k)
        bad = bad || ! std::isfinite (l[k]) || ! std::isfinite (r[k]);
    return ! bad;
}

std::atomic<std::uint64_t> nonFiniteBlocks { 0 };

// Any sample above `level`? Stops at the first: with sound (the common case) it costs almost nothing.
bool anyAbove (const float* l, const float* r, int n, float level) noexcept
{
    // in blocks of 32: the inner loop vectorizes; silent chunks (the ones that scan everything) stay cheap
    int i = 0;

#if defined(LYNX_X86)
    // round 190ge: explicitly four lanes at a time (the loop below stayed scalar on every compiler); the same answer:
    // true as soon as a 32-sample chunk holds a sample above `level` (a NaN is never above it, in both forms)
    {
        const auto mask = _mm_castsi128_ps (_mm_set1_epi32 (0x7fffffff));
        const auto threshold = _mm_set1_ps (level);
        for (; i + 32 <= n; i += 32)
        {
            auto m = _mm_setzero_ps();
            for (int k = i; k < i + 32; k += 4)
                m = _mm_max_ps (_mm_max_ps (_mm_and_ps (_mm_loadu_ps (l + k), mask), _mm_and_ps (_mm_loadu_ps (r + k), mask)), m);
            if (_mm_movemask_ps (_mm_cmpgt_ps (m, threshold)) != 0)
                return true;
        }
    }
#endif
    for (; i + 32 <= n; i += 32)
    {
        float m = 0.0f;

        for (int k = i; k < i + 32; ++k)
        {
            const auto a = std::abs (l[k]), b = std::abs (r[k]);
            m = a > m ? a : m;
            m = b > m ? b : m;
        }

        if (m > level)
            return true;
    }

    for (; i < n; ++i)
        if (std::abs (l[i]) > level || std::abs (r[i]) > level)
            return true;

    return false;
}
} // namespace

namespace
{
// A Patcher-style container bakes its children (kinds, states, parameters, routing) in when it is built, so it can be
// reused only if all of that is unchanged. (It used to be rebuilt on every edit: its children's tails and delay lines
// restarted, and a fresh linear-phase child reporting its latency only after the first block made the engine re-plan
// PDC and rebuild it again, over and over — REA round 87.)
bool sameSlotSetup (const EffectSlot& a, const EffectSlot& b);

bool sameBranches (const EffectSlot& a, const EffectSlot& b)
{
    if (a.branches.size() != b.branches.size() || a.links.size() != b.links.size())
        return false;
    for (std::size_t i = 0; i < a.branches.size(); ++i)
    {
        const auto& x = a.branches[i];
        const auto& y = b.branches[i];
        if (x.gain != y.gain || x.chain.size() != y.chain.size())
            return false;
        for (std::size_t k = 0; k < x.chain.size(); ++k)
            if (! sameSlotSetup (x.chain[k], y.chain[k]))
                return false;
    }
    for (std::size_t i = 0; i < a.links.size(); ++i)
    {
        const auto& x = a.links[i];
        const auto& y = b.links[i];
        if (x.branch != y.branch || x.source != y.source || x.target != y.target || x.param != y.param || x.curve != y.curve)
            return false;
    }
    return true;
}

bool sameSlotSetup (const EffectSlot& a, const EffectSlot& b)
{
    return a.kind == b.kind && a.enabled == b.enabled && a.params == b.params && a.mix == b.mix && a.plugin.id == b.plugin.id
           && a.sidechain == b.sidechain && a.sidechain2 == b.sidechain2 && a.sendTracks == b.sendTracks && a.state == b.state && a.samplePath == b.samplePath
           && sameBranches (a, b);
}
} // namespace

bool sameContainer (const EffectSlot& a, const EffectSlot& b) { return sameBranches (a, b); }

namespace
{
std::uint64_t nowNs() noexcept
{
#if defined(__linux__)
    // diagnostics on a busy machine: LYNX_PROFILE_CPU=1 measures each unit in its thread's CPU time (renderwav)
    static const bool threadCpu = std::getenv ("LYNX_PROFILE_CPU") != nullptr;
    if (threadCpu)
    {
        timespec ts {};
        clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return (std::uint64_t) ts.tv_sec * 1000000000ull + (std::uint64_t) ts.tv_nsec;
    }
#endif
    return (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Adds the time since `start` to `slot` when profiling (start = 0: profiling was off when the unit began)
struct UnitTimer
{
    // `blockSlowest` / `unit` (round 190gg): the block's slowest call, for the dropout log (core/Dropouts.h)
    // (rollup 0.1.28: only the live renderer reports it; the part rendered ahead (190gq) runs outside the callback, so
    // its calls are no cause of a late callback)
    UnitTimer (bool on, std::atomic<std::uint64_t>& s, std::atomic<std::uint64_t>* m = nullptr, std::atomic<std::uint64_t>* blockSlowest = nullptr,
               std::uint32_t unitId = 0) noexcept
        : slot (s), longest (m), slowest (blockSlowest), unit (unitId), start (on ? nowNs() : 0) {}
    ~UnitTimer()
    {
        if (start == 0)
            return;
        const auto used = nowNs() - start;
        slot.fetch_add (used, std::memory_order_relaxed);
        if (longest != nullptr)
        {
            auto current = longest->load (std::memory_order_relaxed);
            while (used > current && ! longest->compare_exchange_weak (current, used, std::memory_order_relaxed))
            {
            }
        }
        if (slowest != nullptr)
            dropouts::raiseSlowest (*slowest, used, unit);
    }
    std::atomic<std::uint64_t>& slot;
    std::atomic<std::uint64_t>* longest;
    std::atomic<std::uint64_t>* slowest;
    std::uint32_t unit;
    std::uint64_t start;
};
} // namespace

void RackRenderer::resetUnitTimes() noexcept
{
    for (auto& t : units->channelNs)
        t.store (0, std::memory_order_relaxed);
    for (auto& t : units->effectNs)
        t.store (0, std::memory_order_relaxed);
    for (auto& t : units->channelMaxNs)
        t.store (0, std::memory_order_relaxed);
    for (auto& t : units->effectMaxNs)
        t.store (0, std::memory_order_relaxed);
    units->processNs.store (0, std::memory_order_relaxed);
    units->processedSamples.store (0, std::memory_order_relaxed);
}

void RackRenderer::prepare (double sr, int maxBlockSize)
{
    lastPreGain.fill (std::numeric_limits<float>::quiet_NaN());
    lowLatencySkipped.assign ((std::size_t) maxMixerTracks * maxEffectSlots, 0);
    goalGlideAmount.assign ((std::size_t) maxMixerTracks * maxEffectSlots, std::numeric_limits<float>::quiet_NaN());
    goalGlideEffect.assign ((std::size_t) maxMixerTracks * maxEffectSlots, nullptr);
    for (auto& slot : automationValues.v) // (round 190kz: every piece's slot)
        slot.fill (std::numeric_limits<float>::quiet_NaN());
    lastTrackGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    lastTapGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    lastMeterGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    smartDisable.assign ((std::size_t) maxMixerTracks * maxEffectSlots, {});
    trackEqs.assign ((std::size_t) maxMixerTracks, {});
    tapBuffers.assign ((std::size_t) RackState::maxSendTaps * 2 * chunkCapacity, 0.0f);
    sampleRate = sr;
    blockCapacity = std::max (64, maxBlockSize);
    {
        // (round 190gn: as many channels / inserts as reserved, the state last played or the minimum; a mixer buffer
        // holds one chunk, the most process() mixes at once)
        const std::scoped_lock l (bufferLock);
        auto channels = std::max (reservedChannels.load(), minBufferChannels);
        auto tracks = std::max (reservedTracks.load(), minBufferTracks);
        if (lastState != nullptr)
        {
            channels = std::max (channels, (int) std::min<std::size_t> (lastState->instruments.size(), (std::size_t) maxChannels));
            tracks = std::max (tracks, (int) std::min<std::size_t> (lastState->song.mixer.size(), (std::size_t) maxMixerTracks));
        }
        const auto frames = std::min (blockCapacity, chunkCapacity);
        reservedChannels.store (channels);
        reservedTracks.store (tracks);
        preparedFrames.store (frames);
        bufferExchange.collectGarbage();
        bufferExchange.publish (makeBuffers (channels, tracks, frames));
        // (issue #236's note properties: every channel's back to "none yet", as the full tables were made afresh here)
        for (auto& block : ownedProps)
            if (block != nullptr)
                *block = {};
        ensureProps (channels);
    }
    // (round 190gp: the lazy-zero flags of the inserts and the shared silent block a reader of an unwritten insert gets)
    for (auto& slot : trackUnwritten.v)
        slot.fill (0);
    // round 190kz: the units' plugin parameter bindings (built on the audio thread for each state: no allocation there)
    unitParamStart.assign ((std::size_t) maxChannels + maxMixerTracks + 1, 0);
    unitParams.assign (4096, 0);
    unitParamsState = nullptr;
    // round 190lv: the live controllers' two rounds (built with the bindings, same rule)
    liveInA.assign ((std::size_t) graphCapacity, 0);
    liveSourceStart.assign ((std::size_t) graphCapacity + 1, 0);
    liveSources.assign ((std::size_t) RackState::maxAutomations, 0);
    liveRootsA.assign ((std::size_t) graphCapacity, 0);
    liveRootsB.assign ((std::size_t) graphCapacity, 0);
    livePendingB.assign ((std::size_t) graphCapacity, 0);
    zeroBlock.assign ((std::size_t) blockCapacity, 0.0f);
    hwScratch.assign (2 * (std::size_t) blockCapacity, 0.0f);
    layerPick.assign ((std::size_t) maxChannels * 128, (std::int16_t) -1);
    voiceOrderScratch.assign ((std::size_t) maxChannels, {});
    heldScratch.assign ((std::size_t) maxChannels, {});
    polyLayers.assign ((std::size_t) maxChannels, {});
    polyScratch.assign ((std::size_t) maxChannels, {});

    if (lastState != nullptr)
    {
        for (std::size_t c = 0; c < lastState->instruments.size(); ++c)
            if (! foreignChannel ((int) c))
                lastState->instruments[c]->prepare (sr);

        for (std::size_t t = 0; t < lastState->effects.size(); ++t)
            if (! foreignTrack ((int) t))
                for (auto& effect : lastState->effects[t])
                    effect->prepare (sr);
    }
}

float* RackRenderer::rawTrackBuffer (int track, int channel) noexcept
{
    return buffers->mix.data() + ((std::size_t) track * 2 + (std::size_t) channel) * (std::size_t) buffers->frames + (std::size_t) pieceOffset.get();
}

std::unique_ptr<const RackRenderer::Buffers> RackRenderer::makeBuffers (int channels, int tracks, int frames)
{
    auto b = std::make_unique<Buffers>();
    b->channels = std::clamp (channels, 1, maxChannels);
    b->tracks = std::clamp (tracks, 1, maxMixerTracks);
    b->frames = std::clamp (frames, 1, chunkCapacity);
    b->channel.assign ((std::size_t) b->channels * 2 * chunkCapacity, 0.0f);
    b->scratch.assign ((std::size_t) b->tracks * 2 * chunkCapacity, 0.0f);
    b->mix.assign ((std::size_t) b->tracks * 2 * (std::size_t) b->frames, 0.0f);
    return b;
}

// Round 190ql (perf 8): the note-property blocks of channels 0 … channels − 1 that are missing are made (each published
// before it can be looked up: release here, acquire in propsOf), and the mirror (the render-ahead's renderer) gets the
// same. Blocks stay until the renderer goes away. Caller holds bufferLock.
void RackRenderer::ensureProps (int channels)
{
    channels = std::clamp (channels, 0, maxChannels);
    if (channels > propsCount.load (std::memory_order_relaxed))
    {
        if (ownedProps.size() < (std::size_t) channels)
            ownedProps.resize ((std::size_t) channels);
        for (auto c = (std::size_t) propsCount.load (std::memory_order_relaxed); c < (std::size_t) channels; ++c)
        {
            if (ownedProps[c] == nullptr)
                ownedProps[c] = std::make_unique<ChannelProps>();
            channelProps[c].store (ownedProps[c].get(), std::memory_order_release);
        }
        propsCount.store (channels, std::memory_order_release);
    }
    if (propsMirror != nullptr)
    {
        const std::scoped_lock l (propsMirror->bufferLock);
        propsMirror->ensureProps (channels);
    }
}

void RackRenderer::setPropsMirror (RackRenderer* mirror)
{
    const std::scoped_lock l (bufferLock);
    propsMirror = mirror;
    if (mirror != nullptr)
        ensureProps (propsCount.load());
}

namespace
{
// what a state's blocks index the buffers by (process: channels < min (instruments, channels, maxChannels); inserts
// < max (1, mixer size), at most maxMixerTracks)
std::pair<int, int> bufferNeeds (const RackState& state) noexcept
{
    const auto channels = (int) std::min ({ state.instruments.size(), state.song.channels.size(), (std::size_t) RackRenderer::maxChannels });
    const auto tracks = std::clamp ((int) state.song.mixer.size(), 1, maxMixerTracks);
    return { std::max (1, channels), tracks };
}
} // namespace

void RackRenderer::reserveFor (const RackState& state)
{
    const auto [channels, tracks] = bufferNeeds (state);
    const std::scoped_lock l (bufferLock);
    reserving.store (true);
    bufferExchange.collectGarbage();
    if (channels <= reservedChannels.load() && tracks <= reservedTracks.load())
        return;
    const auto up = [] (int n, int limit) { return std::min (limit, (n + 31) / 32 * 32); };
    reservedChannels.store (std::max (reservedChannels.load(), up (channels, maxChannels)));
    reservedTracks.store (std::max (reservedTracks.load(), up (tracks, maxMixerTracks)));
    ensureProps (reservedChannels.load()); // (round 190ql: before the state that uses them is published)
    if (preparedFrames.load() > 0) // (not prepared yet: prepare() makes them at this size)
        bufferExchange.publish (makeBuffers (reservedChannels.load(), reservedTracks.load(), preparedFrames.load()));
}

bool RackRenderer::buffersFor (const RackState& state) noexcept
{
    const auto [channels, tracks] = bufferNeeds (state);
    buffers = bufferExchange.acquire();
    const auto fits = [&] { return buffers != nullptr && buffers->channels >= channels && buffers->tracks >= tracks; };
    if (fits())
        return true;
    if (reserving.load (std::memory_order_relaxed)) // (never: reserveFor ran before the state was published)
        return false;
    // a renderer without reserveFor (offline, tools, tests): grown here, on the rendering thread
    try
    {
        const std::scoped_lock l (bufferLock);
        reservedChannels.store (std::max (reservedChannels.load(), channels));
        reservedTracks.store (std::max (reservedTracks.load(), tracks));
        ensureProps (reservedChannels.load());
        bufferExchange.collectGarbage();
        bufferExchange.publish (makeBuffers (reservedChannels.load(), reservedTracks.load(), std::max (1, preparedFrames.load())));
    }
    catch (...)
    {
        return false;
    }
    buffers = bufferExchange.acquire();
    return fits();
}

float* RackRenderer::trackBuffer (int track, int channel) noexcept
{
    if (trackZeros (track)) // (round 190gp: the zeros of a buffer nobody wrote this chunk, stored now)
    {
        trackUnwritten[(std::size_t) track] = 0;
        std::memset (rawTrackBuffer (track, 0), 0, sizeof (float) * (std::size_t) trackSamples);
        std::memset (rawTrackBuffer (track, 1), 0, sizeof (float) * (std::size_t) trackSamples);
    }
    return rawTrackBuffer (track, channel);
}

const float* RackRenderer::trackInput (int track, int channel) noexcept
{
    return trackZeros (track) ? zeroBlock.data() : rawTrackBuffer (track, channel);
}

float* RackRenderer::tapBuffer (int tap, int side) noexcept
{
    return tapBuffers.data() + ((std::size_t) tap * 2 + (std::size_t) side) * chunkCapacity + (std::size_t) pieceOffset.get();
}

float RackRenderer::automated (int automation, float base) const noexcept
{
    if (automation < 0)
        return base;

    const auto v = automationValues[(std::size_t) automation];
    return std::isnan (v) ? base : v;
}

namespace
{
// Value of an automation slot at a song position, as FL plays it (FL renders, issue #97, REA_rodada177b.md): where clips
// of the target play, the one that started last (same start: the lowest automation); between clips, the end of the clip
// that ended last, whichever automation it belongs to; before the first clip, NaN: the control's own value. Depends only
// on position, so playing from any point (or exporting) gives the same result.
// round 190ov: a link's value through its scale (Automation::Link::scaled)
float linkScaled (const std::vector<float>& scale, float v) noexcept
{
    v = std::clamp (v, 0.0f, 1.0f);
    if (scale.size() < 2)
        return v;
    const auto x = v * (float) (scale.size() - 1);
    const auto i = std::min ((std::size_t) x, scale.size() - 2);
    const auto f = x - (float) i;
    return std::clamp (scale[i] + (scale[i + 1] - scale[i]) * f, 0.0f, 1.0f);
}

// round 190ov: the value of a target several clips drive (RackState::LinkSlot::members), as automationValueAt picks the
// clip of a group, then through that clip's link (1:1 for an automation of that one target)
float linkGroupValueAt (const RackState& state, const RackState::LinkSlot& link, double tick) noexcept
{
    const auto& song = state.song;
    const Clip* playing = nullptr;
    const Clip* ended = nullptr;
    const RackState::LinkMember* playingMember = nullptr;
    const RackState::LinkMember* endedMember = nullptr;
    for (const auto& member : link.members)
        for (const auto i : state.automationClips[(std::size_t) member.automation]) // sorted by start
        {
            const auto& c = song.playlist[(std::size_t) i];
            if (tick < (double) c.start)
                break;
            if (tick < (double) c.end())
            {
                if (playing == nullptr || c.start > playing->start)
                    playing = &c, playingMember = &member;
            }
            else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
                ended = &c, endedMember = &member;
        }
    const auto through = [] (const RackState::LinkMember& m, float v) noexcept {
        return m.link < 0 ? v : linkScaled (m.scale, m.formula.map (v));
    };
    if (playing != nullptr)
        return through (*playingMember, song.automations[(std::size_t) playingMember->automation].playAt (tick - (double) playing->start + (double) playing->offset, tick));
    if (ended != nullptr)
        return through (*endedMember, song.automations[(std::size_t) endedMember->automation].playAt ((double) (ended->length + ended->offset), (double) ended->end()));
    return std::numeric_limits<float>::quiet_NaN();
}

float automationValueAt (const RackState& state, int slot, double tick) noexcept
{
    const auto& song = state.song;
    const auto& group = state.automationGroups[(std::size_t) slot];
    const Clip* playing = nullptr;
    const Clip* ended = nullptr;
    int playingAutomation = -1, endedAutomation = -1;

    const auto visit = [&] (const Clip& c, int a) {
        if (tick < (double) c.end())
        {
            if (playing == nullptr || c.start > playing->start)
                playing = &c, playingAutomation = a;
        }
        else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
            ended = &c, endedAutomation = a;
    };
    const auto scan = [&] (int a) {
        for (const auto i : state.automationClips[(std::size_t) a]) // sorted by start
        {
            const auto& c = song.playlist[(std::size_t) i];
            if (tick < (double) c.start)
                break;
            visit (c, a);
        }
        // round 190ow (DE-123): the clips its steps / notes start play by the same rule (the one started last wins)
        if ((std::size_t) a < state.triggerClips.size())
            for (const auto& c : state.triggerClips[(std::size_t) a]) // sorted by start
            {
                if (tick < (double) c.start)
                    break;
                visit (c, a);
            }
    };

    if (group.empty())
        scan (slot);
    else
        for (const auto a : group) // ascending: the lowest automation keeps a tie
            scan (a);

    if (playing != nullptr)
        return song.automations[(std::size_t) playingAutomation].playAt (tick - (double) playing->start + (double) playing->offset, tick); // (+ its LFO: round 177b)
    if (ended != nullptr)
        return song.automations[(std::size_t) endedAutomation].playAt ((double) (ended->length + ended->offset), (double) ended->end());
    return std::numeric_limits<float>::quiet_NaN();
}
} // namespace

void RackRenderer::setOverriddenSlots (const std::vector<int>& slots) noexcept
{
    std::array<std::uint32_t, RackState::maxAutomations / 32> bits {};
    for (const auto s : slots)
        if (s >= 0 && s < RackState::maxAutomations)
            bits[(std::size_t) s / 32] |= 1u << ((unsigned) s % 32);
    for (std::size_t i = 0; i < bits.size(); ++i)
        overridden[i].store (bits[i], std::memory_order_relaxed);
    anyOverridden.store (! slots.empty(), std::memory_order_release);
}

void RackRenderer::setReleasedSlots (const std::vector<int>& slots) noexcept
{
    std::array<std::uint32_t, RackState::maxAutomations / 32> bits {};
    for (const auto s : slots)
        if (s >= 0 && s < RackState::maxAutomations)
            bits[(std::size_t) s / 32] |= 1u << ((unsigned) s % 32);
    for (std::size_t i = 0; i < bits.size(); ++i)
        released[i].store (bits[i], std::memory_order_relaxed);
    anyReleased.store (! slots.empty(), std::memory_order_release);
}

void RackRenderer::clearOverriddenValues() noexcept
{
    const auto touched = anyOverridden.load (std::memory_order_acquire), moved = anyReleased.load (std::memory_order_acquire);
    if (! touched && ! moved)
        return;
    for (std::size_t w = 0; w < overridden.size(); ++w)
        if (auto bits = (touched ? overridden[w].load (std::memory_order_relaxed) : 0u) | (moved ? released[w].load (std::memory_order_relaxed) : 0u); bits != 0)
            for (unsigned b = 0; b < 32; ++b)
                if ((bits & (1u << b)) != 0)
                    automationValues[w * 32 + b] = std::numeric_limits<float>::quiet_NaN(); // the control's own value
}

// Round 190gh (B7): where the song is for its automation and tempo: within the song (the transport wraps at its end),
// except while it plays on past its end (a real-time bounce, a linear recording: liveInput's linearSong), where the
// values hold as the song ended them — as an export's tail does — instead of starting over from the song's start.
namespace
{
double linearSongTick (const LiveInput* liveInput, double startTick, double length) noexcept
{
    if (liveInput != nullptr && liveInput->linearSong.load (std::memory_order_relaxed))
        return std::max (0.0, startTick);
    return std::fmod (std::max (0.0, startTick), length);
}
} // namespace

float RackRenderer::automationValueForCheck (int slot) const noexcept
{
    return slot >= 0 && slot < RackState::maxAutomations ? automationValues.v[0][(std::size_t) slot] : std::numeric_limits<float>::quiet_NaN();
}

void RackRenderer::updateAutomation (const RackState& state, double startTick) noexcept
{
    const auto& song = state.song;

    // controllers (live sources): the effect's output at the end of the previous block, in any mode
    for (const auto a : state.liveAutomations)
    {
        const auto& automation = song.automations[(std::size_t) a];
        const auto& src = automation.source;
        if (src.channel >= 0 ? foreignChannel (src.channel) : foreignTrack (src.track))
            continue; // (render-ahead: that unit's renderer works it out; what it drives is that renderer's too)
        // (round 190lv: a batched block's piece >= 1 holds a placeholder here, the source writes the value into the piece's
        // slot right after it rendered the piece before: runGraphRound / processBatched)
        if (src.channel >= 0)
        {
            if (src.channel < (int) state.instruments.size() && state.instruments[(std::size_t) src.channel] != nullptr)
                automationValues[(std::size_t) a] = deferLive ? 0.0f : automation.mapLive (state.instruments[(std::size_t) src.channel]->controlOutput (src.output));
        }
        else if (src.track < (int) state.effects.size() && src.slot >= 0 && src.slot < (int) state.effects[(std::size_t) src.track].size()
            && state.effects[(std::size_t) src.track][(std::size_t) src.slot] != nullptr)
            automationValues[(std::size_t) a] = deferLive ? 0.0f : automation.mapLive (state.effects[(std::size_t) src.track][(std::size_t) src.slot]->controlOutput (src.output));
    }
    if (deferLive)
        pieces[(std::size_t) pieceSlot()].liveDeferred = true;

    // pattern events: in pattern mode the current pattern's (looped); in song mode those of the pattern clip
    // that covers the position. Elsewhere the value holds.
    if (! state.eventLanes.empty())
    {
        if (song.mode != PlayMode::song)
        {
            const auto current = song.currentPattern;

            if (current >= 0 && current < (int) song.patterns.size())
            {
                const auto& pattern = song.patterns[(std::size_t) current];
                const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
                const auto t = std::fmod (std::max (0.0, startTick), length);

                for (const auto& lane : state.eventLanes)
                    if (lane.pattern == current)
                        automationValues[(std::size_t) (lane.shared >= 0 ? lane.shared : lane.slot)] = pattern.events[(std::size_t) lane.lane].valueAt (t);
            }
        }
        else
        {
            const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
            const auto tick = linearSongTick (liveInput, startTick, length);

            // inside a clip of the pattern: its events; after one: the value it ended with; before the first:
            // no value (the target's own). Depends only on the position, like automation clips.
            // (the lane's value and the start of its pattern's clip that gave it; −∞ before the first)
            const auto laneValue = [&] (const RackState::EventLane& lane, double& from) noexcept {
                const auto& events = song.patterns[(std::size_t) lane.pattern].events[(std::size_t) lane.lane];
                auto value = std::numeric_limits<float>::quiet_NaN();
                from = -std::numeric_limits<double>::infinity();

                for (auto i : state.patternClips[(std::size_t) lane.pattern]) // sorted by start
                {
                    const auto& c = song.playlist[(std::size_t) i];

                    if (tick < (double) c.start)
                        break;

                    // a stretched clip loops its pattern (LYNX extra stretchedClipsLoop, issue #25), its events too
                    const auto period = (std::size_t) lane.pattern < state.schedule.patternLength.size() ? state.schedule.patternLength[(std::size_t) lane.pattern] : 0;
                    value = events.valueAt (patternTickInClip (c, tick < (double) c.end() ? tick : (double) c.end() - 1.0, period));
                    from = (double) c.start;
                }
                return value;
            };
            for (const auto& lane : state.eventLanes)
                if (lane.shared < 0)
                {
                    double from = 0.0;
                    automationValues[(std::size_t) lane.slot] = laneValue (lane, from);
                }
            // round 190om: a target several patterns' lanes drive takes the value of the pattern whose clip started last
            for (const auto& group : state.sharedLanes)
            {
                auto best = -std::numeric_limits<double>::infinity();
                auto value = std::numeric_limits<float>::quiet_NaN();
                for (const auto k : group) // (the bound lane first: it wins a tie, as it did alone)
                {
                    double from = 0.0;
                    const auto v = laneValue (state.eventLanes[(std::size_t) k], from);
                    if (from > best)
                        best = from, value = v;
                }
                automationValues[(std::size_t) state.eventLanes[(std::size_t) group.front()].shared] = value;
            }

            // round 190mx (GitHub #198, Live's clip envelopes): a pattern clip the clip grid plays drives its pattern's
            // events at its own position (position 0 at anchor + k·length on the launcher's clock), as Session Record
            // writes them; the grid's clip takes the track over, so it wins over the Playlist's value
            if (state.clipLauncher != nullptr && song.clipGrid && state.clipLauncher->anyPlaying.load (std::memory_order_relaxed))
            {
                const auto& rt = *state.clipLauncher;
                const auto& grid = *song.clipGrid;
                for (const auto& c : grid.clips)
                {
                    if (c.kind != ClipKind::pattern || c.length <= 0 || c.pattern < 0 || c.pattern >= (int) song.patterns.size()
                        || song.patterns[(std::size_t) c.pattern].events.empty() || c.track < 0 || c.track >= launcher::maxTracks
                        || c.gridSlot < 0 || c.gridSlot >= (int) grid.slots.size())
                        continue;
                    const auto& view = rt.tracks[(std::size_t) c.track];
                    if (view.playing.load (std::memory_order_relaxed) != grid.slots[(std::size_t) c.gridSlot].scene)
                        continue;
                    auto at = std::fmod (rt.clock - view.anchor.load (std::memory_order_relaxed), (double) c.length);
                    if (at < 0.0)
                        at += (double) c.length;
                    const auto local = (double) c.offset + at;
                    for (const auto& lane : state.eventLanes)
                        if (lane.pattern == c.pattern)
                            automationValues[(std::size_t) (lane.shared >= 0 ? lane.shared : lane.slot)] = song.patterns[(std::size_t) c.pattern].events[(std::size_t) lane.lane].valueAt (local);
                }
            }
        }
    }

    // in pattern mode the Playlist does not play: values stay as they were
    if (song.mode != PlayMode::song)
    {
        // round 190ow (DE-123): except the automation clips the current pattern's steps / notes start (FL plays them in
        // Pattern mode as in Song mode)
        const auto triggered = state.patternTriggers && updatePatternTriggers (state, startTick);
        compensateTick = -1.0;
        clearOverriddenValues();
        if (triggered)
        {
            applyOrDeferParams (state, 0, true);
            return;
        }
        // only the plugin parameters driven by pattern events and by live controllers (links: FL's work in any mode,
        // round 165)
        if (! state.eventLanes.empty() || ! state.liveAutomations.empty())
            applyOrDeferParams (state, std::min<int> ((int) song.automations.size(), RackState::maxAutomations), true);
        return;
    }

    const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
    const auto tick = linearSongTick (liveInput, startTick, length);

    for (std::size_t a = 0; a < state.automationClips.size(); ++a)
        if (state.positional ((int) a) && state.automationFollowers[a] == 0) // (round 190ow: triggered clips too)
            automationValues[a] = automationValueAt (state, (int) a, tick);
    // round 190gw: an automation's target links take its value through their formulas (NaN: the targets' own values);
    // round 190ov: then their scales, and a target several clips drive takes the one that plays
    for (const auto& link : state.linkSlots)
    {
        if (! link.members.empty())
        {
            automationValues[(std::size_t) link.slot] = linkGroupValueAt (state, link, tick);
            continue;
        }
        const auto v = automationValues[(std::size_t) link.automation];
        automationValues[(std::size_t) link.slot] = std::isnan (v) ? v : linkScaled (link.scale, link.formula.map (v));
    }

    // automation lanes (round 177b): the lane's curve in song time, except where a clip of the same target plays
    for (const auto& lane : state.laneAutomations)
    {
        auto value = std::numeric_limits<float>::quiet_NaN();
        for (const auto peer : lane.clipPeers)
            for (const auto i : state.automationClips[(std::size_t) peer])
                if (const auto& c = song.playlist[(std::size_t) i]; tick >= (double) c.start && tick < (double) c.end())
                {
                    auto slot = peer;
                    for (const auto& held : lane.clipSlots) // (a follower of a group: the slot holder has the value)
                        if (held.first == peer)
                            slot = held.second;
                    value = automationValues[(std::size_t) slot];
                    break;
                }
        automationValues[(std::size_t) lane.automation] = std::isnan (value) ? song.automations[(std::size_t) lane.automation].valueAt (tick) : value;
    }

    // Performance mode: the launched automation clips (the last block's passes) drive their automation
    if (perfRunning)
        for (int i = 0; i < perfPlayer.virtualCount(); ++i)
        {
            const auto& v = perfPlayer.virtualClip (i).clip;
            if (v.kind != ClipKind::automation || v.automation < 0 || v.automation >= (int) song.automations.size()
                || v.automation >= RackState::maxAutomations)
                continue;
            const auto local = std::clamp (perfClock - (double) v.start, 0.0, (double) v.length);
            automationValues[(std::size_t) v.automation] = song.automations[(std::size_t) v.automation].playAt (local + (double) v.offset, perfClock);
        }

    compensateTick = tick;
    compensateTicksPerSample = song.autoPdc && song.compensateAutomations && ! perfRunning && ! state.pdc.slotInput.empty() && state.sampleRate > 0.0
                                   ? bpm / 60.0 * (double) ppq / state.sampleRate
                                   : 0.0;
    clearOverriddenValues();
    applyOrDeferParams (state, 0, false);
}

// Round 190kz: in a batched block's serial part (processBatched) the bindings are applied later, by each unit for its
// piece; elsewhere now, as before
// Round 190ow (DE-123): Pattern mode plays the current pattern in a loop, and its steps / notes on the automation clip rows
// start their clips as in Song mode: at position t of the pass, the note started last at or before t (else the last one
// of the pass before, from the second pass on) plays its clip from its start; before the first one the target keeps its
// own value. False when the current pattern has none (the values stay as they were).
bool RackRenderer::updatePatternTriggers (const RackState& state, double startTick) noexcept
{
    const auto& song = state.song;
    const auto current = song.currentPattern;
    if (current < 0 || current >= (int) song.patterns.size() || song.patterns[(std::size_t) current].autoNotes.empty())
        return false;
    const auto& pattern = song.patterns[(std::size_t) current];
    const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
    const auto position = std::max (0.0, startTick);
    const auto t = std::fmod (position, length);
    const auto count = std::min<int> ((int) song.automations.size(), RackState::maxAutomations);
    bool any = false;
    for (int a = 0; a < count; ++a)
    {
        const auto slot = (std::size_t) a < state.triggerSlot.size() ? state.triggerSlot[(std::size_t) a] : -1;
        const auto& automation = song.automations[(std::size_t) a];
        if (slot < 0 || automation.muted)
            continue;
        const Note* last = nullptr;     // started last at or before t
        const Note* previous = nullptr; // the pass's last one (for the start of the next pass)
        for (const auto& n : pattern.autoNotes)
        {
            if (n.channel != a || n.muted)
                continue;
            if ((double) n.start <= t)
                last = &n;
            previous = &n;
        }
        if (last == nullptr && previous == nullptr)
            continue;
        any = true;
        double since = 0.0;
        const Note* playing = last;
        if (playing != nullptr)
            since = t - (double) playing->start;
        else if (position >= length) // (the first pass has nothing before its first note)
        {
            playing = previous;
            since = t + length - (double) playing->start;
        }
        if (playing == nullptr)
        {
            automationValues[(std::size_t) slot] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }
        const auto held = (double) std::max<std::int64_t> (1, triggerLength (automation, *playing));
        automationValues[(std::size_t) slot] = automation.playAt (std::min (since, held), position);
    }
    return any;
}

void RackRenderer::applyOrDeferParams (const RackState& state, int firstSlot, bool andLive) noexcept
{
    if (! deferParams)
    {
        applyPluginParams (state, firstSlot, andLive);
        return;
    }
    auto& piece = pieces[(std::size_t) pieceSlot()];
    piece.params = true;
    piece.paramsFirst = firstSlot;
    piece.paramsLive = andLive;
}

void RackRenderer::updateModulators (const RackState& state, double tick, int numSamples) noexcept
{
    const auto& song = state.song;
    const auto beats = std::max (0.0, tick) / (double) ppq;
    const auto seconds = sampleRate > 0.0 ? numSamples / sampleRate : 0.0;

    // round 190ox: the stateful sources move once per chunk (control rate), each modulator once however many targets
    // it drives; a source with jumps (steps, random, macro, the last note) is smoothed over ~5 ms so nothing clicks
    if (state.modStateful)
    {
        int last = -1;
        for (const auto& r : state.modRoutes)
        {
            if (r.modulator == last || r.modulator < 0 || r.modulator >= (int) song.modulators.size() || r.modulator >= maxStatefulModulators)
                continue;
            last = r.modulator;
            const auto& m = song.modulators[(std::size_t) r.modulator];
            const auto& feed = state.modFeeds[(std::size_t) r.modulator];
            auto& st = modStates[(std::size_t) r.modulator];
            switch (m.kind)
            {
                case ModKind::follower:
                    modenv::follow (m, st, feed.track >= 0 ? followPeak[(std::size_t) feed.track] : 0.0f, seconds);
                    break;
                case ModKind::envelope:
                {
                    const auto c = feed.channel;
                    const auto struck = c >= 0 && c < maxChannels ? notesStruck[(std::size_t) c] : st.notesSeen;
                    auto gate = false;
                    if (c >= 0 && c < maxChannels)
                        for (const auto word : heldNotes[(std::size_t) c])
                            gate = gate || word != 0;
                    const auto trigger = st.primed && struck != st.notesSeen;
                    st.notesSeen = struck;
                    st.primed = true;
                    modenv::envelope (m, st, gate || trigger, trigger, seconds);
                    break;
                }
                case ModKind::keytrack:
                case ModKind::velocity:
                    if (feed.channel >= 0 && feed.channel < maxChannels && notesStruck[(std::size_t) feed.channel] != 0)
                    {
                        st.lastKey = (float) lastNoteKey[(std::size_t) feed.channel];
                        st.lastVelocity = lastNoteVelocity[(std::size_t) feed.channel];
                    }
                    break;
                case ModKind::lfo:
                case ModKind::steps:
                case ModKind::random:
                case ModKind::macro: break;
            }
        }
    }

    int lastMod = -1;
    float value = 0.0f;
    for (const auto& r : state.modRoutes)
    {
        if (r.modulator < 0 || r.modulator >= (int) song.modulators.size())
            continue;
        const auto& m = song.modulators[(std::size_t) r.modulator];
        if (r.modulator != lastMod) // (a modulator's routes are next to each other: its output once)
        {
            lastMod = r.modulator;
            const auto cycles = m.sync ? beats / std::max (1.0e-3, m.beats) : modSeconds * std::max (0.0, m.hz);
            if (m.kind == ModKind::lfo)
                value = m.output (cycles); // (stage 1's modulators exactly as before)
            else
            {
                auto* st = r.modulator < maxStatefulModulators ? &modStates[(std::size_t) r.modulator] : nullptr;
                auto y = 0.0f;
                auto smoothed = false;
                switch (m.kind)
                {
                    case ModKind::steps:
                    case ModKind::random: y = m.stepOutput (cycles); smoothed = m.kind == ModKind::random ? m.smooth <= 0.0f : m.glide <= 0.0f; break;
                    case ModKind::macro:
                    {
                        const auto slot = (std::size_t) r.modulator < state.modFeeds.size() ? state.modFeeds[(std::size_t) r.modulator].macroSlot : -1;
                        const auto automated = slot >= 0 ? automationValues[(std::size_t) slot] : std::numeric_limits<float>::quiet_NaN();
                        const auto v = std::isnan (automated) ? m.macro : automated;
                        y = m.unipolar ? v : v * 2.0f - 1.0f;
                        smoothed = true;
                        break;
                    }
                    case ModKind::follower:
                    case ModKind::envelope:
                    {
                        const auto level = st != nullptr ? st->level : 0.0f; // 0..1
                        y = m.unipolar ? level : level * 2.0f - 1.0f;
                        break;
                    }
                    case ModKind::keytrack:
                        y = st != nullptr ? std::clamp ((st->lastKey - 60.0f) / 60.0f, -1.0f, 1.0f) : 0.0f; // (C5 = 0, ±5 octaves)
                        if (m.unipolar)
                            y = (y + 1.0f) * 0.5f;
                        smoothed = true;
                        break;
                    case ModKind::velocity:
                    {
                        const auto v = st != nullptr ? std::clamp (st->lastVelocity, 0.0f, 1.0f) : 0.0f;
                        y = m.unipolar ? v : v * 2.0f - 1.0f;
                        smoothed = true;
                        break;
                    }
                    case ModKind::lfo: break;
                }
                if (smoothed && st != nullptr)
                {
                    constexpr double tauMs = 5.0;
                    const auto a = (float) (1.0 - std::exp (-seconds * 1000.0 / tauMs));
                    st->smoothed = std::isnan (st->smoothed) || ! st->primedSmooth ? y : st->smoothed + (y - st->smoothed) * a;
                    st->primedSmooth = true;
                    y = st->smoothed;
                }
                value = y;
            }
        }
        const auto source = r.source >= 0 ? automationValues[(std::size_t) r.source] : std::numeric_limits<float>::quiet_NaN();
        const auto base = std::isnan (source) ? r.base : source;
        automationValues[(std::size_t) r.slot] = std::clamp (base + r.depth * value, 0.0f, 1.0f);
    }
    if (sampleRate > 0.0)
        modSeconds += numSamples / sampleRate;
}

float RackRenderer::compensatedAutomation (const RackState& state, int automation, int track, int slot, float current) const noexcept
{
    if (compensateTicksPerSample <= 0.0 || compensateTick < 0.0 || automation < 0 || std::isnan (current)
        || (std::size_t) track >= state.pdc.slotInput.size() || (std::size_t) slot >= state.pdc.slotInput[(std::size_t) track].size()
        || ! state.positional (automation)
        || automation >= RackState::maxAutomations || std::isnan (automationValues[(std::size_t) automation]))
        return current;
    const auto lag = state.pdc.slotInput[(std::size_t) track][(std::size_t) slot];
    if (lag <= 0)
        return current;
    const auto earlier = automationValueAt (state, automation, std::max (0.0, compensateTick - lag * compensateTicksPerSample));
    return std::isnan (earlier) ? current : earlier;
}

float RackRenderer::automatedLevel (const RackState& state, int automation, float base, int track) const noexcept
{
    const auto current = automated (automation, base);
    if (! state.song.compensateMixerLevels || compensateTicksPerSample <= 0.0 || compensateTick < 0.0 || automation < 0
        || automation >= RackState::maxAutomations || ! state.positional (automation) || std::isnan (automationValues[(std::size_t) automation])
        || (std::size_t) track >= state.pdc.levelLag.size())
        return current;
    const auto lag = state.pdc.levelLag[(std::size_t) track];
    if (lag <= 0)
        return current;
    const auto earlier = automationValueAt (state, automation, std::max (0.0, compensateTick - lag * compensateTicksPerSample));
    return std::isnan (earlier) ? current : earlier;
}

void RackRenderer::applyPluginParams (const RackState& state, int firstSlot, bool andLive) noexcept
{
    for (std::size_t k = 0; k < state.pluginParams.size(); ++k)
        applyPluginParam (state, k, firstSlot, andLive);
}

// one binding of applyPluginParams (round 190kz: processBatched applies each unit's own inside its task)
void RackRenderer::applyPluginParam (const RackState& state, std::size_t index, int firstSlot, bool andLive) noexcept
{
    const auto& b = state.pluginParams[index];
    if (b.automation < firstSlot
        && ! (andLive && (std::size_t) b.automation < state.song.automations.size() && state.song.automations[(std::size_t) b.automation].isLive()))
        return;

    const auto v = automationValues[(std::size_t) b.automation];

    if (std::isnan (v) || (b.channel >= 0 ? foreignChannel (b.channel) : foreignTrack (b.track)))
        return;

    if (b.channel >= 0)
    {
        const auto kind = (std::size_t) b.channel < state.song.channels.size() ? state.song.channels[(std::size_t) b.channel].instrument
                                                                                : InstrumentKind::plugin;
        if (midictl::isPitchBend (b.param) && kind != InstrumentKind::plugin && kind != InstrumentKind::midiOut
            && b.channel < (int) state.instruments.size() && b.channel < maxChannels)
        {
            // LYNX's own instruments: the bend as the controller's wheel plays it (applyLiveControl), when it changes
            if (! automatedBendReset)
            {
                automatedBend.fill (std::numeric_limits<float>::quiet_NaN());
                automatedBendReset = true;
            }
            auto& last = automatedBend[(std::size_t) b.channel];
            if (! (last == v) && state.instruments[(std::size_t) b.channel] != nullptr)
            {
                last = v;
                NoteEvent e;
                e.type = NoteEvent::Type::control;
                e.channel = b.channel;
                e.key = NoteEvent::controlPitchBend;
                e.velocity = std::clamp (v, 0.0f, 1.0f) * 2.0f - 1.0f;
                applyLiveControl (state, e);
            }
            return;
        }
        if (b.channel < (int) state.instruments.size())
            state.instruments[(std::size_t) b.channel]->setParameter (b.param, v);
        // a MIDI Out's controls reach the plugins on its port
        if ((std::size_t) b.channel < state.midiTargets.size() && midictl::isMidiParam (b.param)) // (a Dashboard's outputs stay its own)
            for (const auto t : state.midiTargets[(std::size_t) b.channel])
                if (t < (int) state.instruments.size())
                    state.instruments[(std::size_t) t]->setParameter (b.param, v);
    }
    else if (b.track < (int) state.effects.size() && b.slot < (int) state.effects[(std::size_t) b.track].size())
    {
        state.effects[(std::size_t) b.track][(std::size_t) b.slot]->setParameter (b.param, compensatedAutomation (state, b.automation, b.track, b.slot, v));
    }
}

void RackRenderer::queueTimedPluginParams (const RackState& state, double startTick, int numSamples, bool continuing) noexcept
{
    const auto& song = state.song;
    if (! sampleAccurateAutomation() || timedGridPos < 0 || ! chunkPlaying || song.mode != PlayMode::song || perfRunning
        || state.pluginParams.empty() || numSamples < 2 || chunkTps <= 0.0)
        return;
    const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
    const auto tps = chunkTps;

    for (const auto& b : state.pluginParams)
    {
        const auto a = b.automation;
        if (a < 0 || a >= RackState::maxAutomations || midictl::isMidiParam (b.param) || std::isnan (automationValues[(std::size_t) a]))
            continue;
        // the sources whose value follows the song position: an automation clip, or a lane without clips over it
        const auto clip = state.positional (a) && state.automationFollowers[(std::size_t) a] == 0;
        const RackState::LanePlay* lane = nullptr;
        if (! clip)
            for (const auto& l : state.laneAutomations)
                if (l.automation == a && l.clipPeers.empty())
                    lane = &l;
        if (! clip && lane == nullptr)
            continue;

        Instrument* instrument = nullptr;
        Effect* effect = nullptr;
        auto lagTicks = 0.0;
        if (b.channel >= 0)
        {
            if (foreignChannel (b.channel) || b.channel >= (int) state.instruments.size() || state.instruments[(std::size_t) b.channel] == nullptr)
                continue;
            instrument = state.instruments[(std::size_t) b.channel].get();
            if (! instrument->takesTimedParameters())
                continue;
        }
        else
        {
            if (foreignTrack (b.track) || b.track < 0 || b.track >= (int) state.effects.size() || b.slot < 0
                || b.slot >= (int) state.effects[(std::size_t) b.track].size() || state.effects[(std::size_t) b.track][(std::size_t) b.slot] == nullptr)
                continue;
            effect = state.effects[(std::size_t) b.track][(std::size_t) b.slot].get();
            if (! effect->takesTimedParameters())
                continue;
            // (PDC of automation, compensatedAutomation: the value from as many samples earlier as the slot is delayed)
            if (clip && compensateTicksPerSample > 0.0 && (std::size_t) b.track < state.pdc.slotInput.size()
                && (std::size_t) b.slot < state.pdc.slotInput[(std::size_t) b.track].size())
                lagTicks = std::max (0, state.pdc.slotInput[(std::size_t) b.track][(std::size_t) b.slot]) * compensateTicksPerSample;
        }

        const auto valueAt = [&] (int sample) noexcept {
            const auto tick = linearSongTick (liveInput, startTick + sample * tps + 1.0e-6, length);
            if (lane != nullptr)
                return song.automations[(std::size_t) a].valueAt (tick);
            const auto v = automationValueAt (state, a, std::max (0.0, tick - lagTicks));
            return std::isnan (v) ? automationValues[(std::size_t) a] : v;
        };
        const auto send = [&] (int offset, float v) noexcept {
            if (instrument != nullptr)
                instrument->setParameterAt (b.param, v, offset);
            else
                effect->setParameterAt (b.param, v, offset);
        };

        // the value the plugin has: the chunk's own (applyPluginParams), or the last one sent in the piece's first part
        auto previous = continuing ? timedLast[(std::size_t) a] : automationValues[(std::size_t) a];
        if (std::isnan (previous))
            previous = automationValues[(std::size_t) a];
        // checkpoints: the export grid's timedStep samples, then the chunk's end (only a jump is placed before it)
        auto from = continuing ? -1 : 0; // (the last sample whose value the plugin has)
        const auto firstGrid = (int) ((timedStep - (timedGridPos % timedStep)) % timedStep);
        for (auto k = firstGrid == 0 && ! continuing ? timedStep : firstGrid;; k += timedStep)
        {
            const auto atEnd = k >= numSamples;
            const auto at = atEnd ? numSamples : k;
            const auto v = valueAt (at);
            if (v != previous)
            {
                // a jump: half way the value is still the old or already the new one. Its sample: the first that has
                // the new side's value (a bisection between the last checkpoint and this one)
                const auto lo0 = std::max (from, 0);
                const auto mid = valueAt ((lo0 + at) / 2);
                const auto jump = at - lo0 > 1 && (mid == previous || mid == v);
                if (jump)
                {
                    auto lo = from, hi = at; // value(lo) is the old one, value(hi) the new one
                    while (hi - lo > 1)
                    {
                        const auto m = lo + (hi - lo) / 2;
                        (std::abs (valueAt (m) - previous) < std::abs (valueAt (m) - v) ? lo : hi) = m;
                    }
                    if (hi < numSamples && hi > 0)
                    {
                        const auto there = valueAt (hi);
                        send (hi, there);
                        previous = there;
                    }
                }
                if (! atEnd && v != previous) // (a ramp: its value at the checkpoint)
                {
                    send (at, v);
                    previous = v;
                }
            }
            if (atEnd)
                break;
            from = at;
        }
        timedLast[(std::size_t) a] = previous;
    }
}

void RackRenderer::deliverToEffects (const RackState& state, const NoteEvent& e) noexcept
{
    if (e.channel < 0 || (std::size_t) e.channel >= state.effectMidiRoutes.size()
        || (e.type != NoteEvent::Type::on && e.type != NoteEvent::Type::off && e.type != NoteEvent::Type::voiceProp) || e.port == -2)
        return;
    if (e.type == NoteEvent::Type::on && state.song.channels[(std::size_t) e.channel].muted)
        return;
    for (const auto& route : state.effectMidiRoutes[(std::size_t) e.channel])
    {
        if ((std::size_t) route.track >= state.effects.size() || (std::size_t) route.slot >= state.effects[(std::size_t) route.track].size()
            || foreignTrack (route.track))
            continue;
        if ((e.port >= 0 && route.port != e.port) || (e.port < 0 && route.portOnly))
            continue;
        if (auto* graph = patcher::graphOf (state.effects[(std::size_t) route.track][(std::size_t) route.slot].get()))
        {
            if (e.type == NoteEvent::Type::voiceProp) // (a VFX channel's voice property, round 165c: at once)
            {
                graph->voicePropertyPort (route.port, e.key, e.prop, e.velocity, e.port >= 0 ? (int) e.colour : route.midiChannel);
                continue;
            }
            // at the note's sample in the chunk the slot renders next (round 164f; it was the chunk's start); a slot
            // that is off renders nothing and gets nothing
            const auto& slot = state.song.mixer[(std::size_t) route.track].effects[(std::size_t) route.slot];
            if (! state.song.slotActive (slot))
                continue;
            if (e.type == NoteEvent::Type::on)
            {
                graph->queueNoteOnPort (route.port, e.key, e.velocity, route.midiChannel, e.sampleOffset);
                // a note wakes a smart-disabled slot: the instruments inside play it (a silent insert is their usual place)
                if (const auto key = (std::size_t) route.track * maxEffectSlots + (std::size_t) route.slot; key < smartDisable.size())
                    smartDisable[key].silentSamples = 0;
            }
            else
                graph->queueNoteOffPort (route.port, e.key, route.midiChannel, e.sampleOffset);
        }
        else if (e.type != NoteEvent::Type::voiceProp && ! route.portOnly) // round 190ee: Gross Beat, Pitcher
        {
            const auto& slot = state.song.mixer[(std::size_t) route.track].effects[(std::size_t) route.slot];
            if (! state.song.slotActive (slot))
                continue;
            const auto midiChannel = e.port >= 0 ? (int) (e.colour & 15) : route.midiChannel;
            const auto velocity = e.type == NoteEvent::Type::on ? std::clamp ((int) std::lround (e.velocity * 127.0f), 1, 127) : 0;
            if (auto* fx = state.effects[(std::size_t) route.track][(std::size_t) route.slot].get())
                fx->midiNote (e.key & 127, velocity, midiChannel);
            if (e.type == NoteEvent::Type::on)
                if (const auto key = (std::size_t) route.track * maxEffectSlots + (std::size_t) route.slot; key < smartDisable.size())
                    smartDisable[key].silentSamples = 0;
        }
    }
}

float* RackRenderer::channelBuffer (int channel, int side) noexcept
{
    return buffers->channel.data() + ((std::size_t) channel * 2 + (std::size_t) side) * chunkCapacity + (std::size_t) pieceOffset.get();
}

float* RackRenderer::trackScratch (int track, int side) noexcept
{
    return buffers->scratch.data() + ((std::size_t) track * 2 + (std::size_t) side) * chunkCapacity;
}

// A whole channel over the chunk: notes at the exact sample (only its own events), volume/pan and the
// PDC delay. Each channel only touches its own instrument and buffer: it can run in parallel.
channelpoly::Mode RackRenderer::polyMode (const ChannelDef& c, const Instrument& instrument) const noexcept
{
    channelpoly::Mode m;
    // (the LYNX Synth plays its own Mono / Porta, the Sampler its own Porta: the layer leaves those to them)
    m.mono = c.instrument != InstrumentKind::synth && channelMono (c);
    m.porta = channelPortamento (c);
    m.portaBeats = channelPortamentoBeats (c);
    m.arpeggiator = c.arp.direction != 0;
    m.echo = c.misc.echo.feedback > 0.0f;
    m.slides = instrument.followsSlides();
    m.glidesIn = instrument.followsChannelPorta();
    return m;
}

void RackRenderer::clearPolyLayers() noexcept
{
    for (auto& layer : polyLayers)
        layer.clear();
}

namespace
{
constexpr double flTickTicks = ppq / 96.0; // one tick of FL's default PPQ, in song ticks
} // namespace

// FL's native generators: the host steps their voices' pitch for a slide note (round 190jv, REA_rodada190jv §3). LYNX's
// own generators (the drum and synth kinds, Tetra, Helix), FL Studio Mobile's SuperSaw and hosted plugins keep the glide.
bool RackRenderer::stepsSlides (InstrumentKind k) noexcept
{
    switch (k)
    {
        case InstrumentKind::sampler: case InstrumentKind::sakura: case InstrumentKind::drumpad: case InstrumentKind::boobass:
        case InstrumentKind::drumsynth: case InstrumentKind::harmless: case InstrumentKind::sytrus: case InstrumentKind::harmor:
        case InstrumentKind::gms: case InstrumentKind::morphine: case InstrumentKind::ogun: case InstrumentKind::minisynth:
        case InstrumentKind::dx10: case InstrumentKind::kepler: case InstrumentKind::poizone: case InstrumentKind::fruitkick:
        case InstrumentKind::bassdrum: case InstrumentKind::toxic: case InstrumentKind::threeosc: case InstrumentKind::autogun:
        case InstrumentKind::transistorbass: case InstrumentKind::simsynth: case InstrumentKind::plucked: case InstrumentKind::beepmap:
        case InstrumentKind::sawer: case InstrumentKind::flkeys: case InstrumentKind::granulizer: case InstrumentKind::drumaxx:
        case InstrumentKind::slayer: case InstrumentKind::directwave: case InstrumentKind::patcher: case InstrumentKind::soundfont:
        case InstrumentKind::keplerExo: case InstrumentKind::wavetraveller: case InstrumentKind::fruityslicer: case InstrumentKind::slicex:
        case InstrumentKind::fpc:
            return true;
        default:
            return false;
    }
}

// Issue #236 (round 190od, Song::slideNoteProps): FL's slide note moves every sounding note of its channel toward its own
// velocity, pan and Mod X / Y (FL manual: "During slides all note properties move toward those set in the slide note";
// FL 2026's renders, docs/analise/REA_rodada190od.md): from where each note is now to the slide note's values in n + 1
// equal steps, one per FL tick, the first a sample after the slide's start and the last at its end — the law round
// 190hg measured for the pitch. The voices take the steps as voice properties (Instrument::voiceProperty); an engine
// without them keeps its level, as before.
void RackRenderer::startPropSlide (int channel, const NoteEvent& slide, int flTicks, bool flLaw, const NoteMask* only) noexcept
{
    auto* block = channel >= 0 ? propsOf ((std::size_t) channel) : nullptr;
    if (block == nullptr)
        return;
    auto& p = block->slide;
    p.count = 0;
    p.steps = std::max (0, flTicks);
    p.flLaw = flLaw;
    p.to = { std::clamp (slide.velocity, 0.0f, 1.0f), std::clamp (slide.pan, -1.0f, 1.0f), std::clamp (slide.modX, 0.0f, 1.0f),
             std::clamp (slide.modY, 0.0f, 1.0f) };
    const auto& held = only != nullptr ? *only : heldNotes[(std::size_t) channel];
    const auto* props = block->notes.data();
    bool moves = false;
    for (int key = 0; key < 128 && p.count < PropSlide::maxKeys; ++key)
        if ((held[(std::size_t) (key >> 6)] >> (key & 63)) & 1u)
        {
            const auto& now = props[key];
            p.keys[(std::size_t) p.count] = (std::uint8_t) key;
            p.from[(std::size_t) p.count] = now;
            ++p.count;
            moves = moves || now.velocity != p.to.velocity || now.pan != p.to.pan || now.modX != p.to.modX || now.modY != p.to.modY;
        }
    if (! moves) // (nothing differs: the slide moves the pitch only, as before)
        p.count = 0;
}

void RackRenderer::stepPropSlide (Instrument& instrument, int channel, int step) noexcept
{
    auto* block = channel >= 0 ? propsOf ((std::size_t) channel) : nullptr;
    if (block == nullptr)
        return;
    auto& p = block->slide;
    if (p.count <= 0 || step < 0)
        return;
    const auto last = step >= p.steps;
    const auto f = last ? 1.0f : (float) (step + 1) / (float) (p.steps + 1);
    auto* props = block->notes.data();
    for (int k = 0; k < p.count; ++k)
    {
        const auto key = (int) p.keys[(std::size_t) k];
        const auto& a = p.from[(std::size_t) k];
        const auto& b = p.to;
        const auto at = [f] (float x, float y) { return x + (y - x) * f; };
        NoteProps now { at (a.velocity, b.velocity), at (a.pan, b.pan), at (a.modX, b.modX), at (a.modY, b.modY) };
        if (p.flLaw && a.velocity != b.velocity)
        {
            // FL's host moves Final.Vol in a straight line (FL 2026's renders: half way through a fade the level is
            // the mean of the two levels), i.e. linear in FL's volume law (11^(1.28·v) − 1) / 10, not in velocity
            const auto vol = [] (float v) { return (std::pow (11.0f, 1.28f * std::clamp (v, 0.0f, 1.0f)) - 1.0f) * 0.1f; };
            const auto g = at (vol (a.velocity), vol (b.velocity));
            now.velocity = last ? b.velocity : std::clamp (std::log (10.0f * g + 1.0f) / (1.28f * std::log (11.0f)), 0.0f, 1.0f);
        }
        auto& was = props[key];
        now.cents = was.cents, now.colour = was.colour;
        if (now.velocity != was.velocity)
            instrument.voiceProperty (key, (int) vfx::Prop::velocity, now.velocity);
        if (now.pan != was.pan)
            instrument.voiceProperty (key, (int) vfx::Prop::pan, now.pan);
        if (now.modX != was.modX)
            instrument.voiceProperty (key, (int) vfx::Prop::modX, now.modX);
        if (now.modY != was.modY)
            instrument.voiceProperty (key, (int) vfx::Prop::modY, now.modY);
        was = now;
    }
    if (last)
        p.count = 0;
}

void RackRenderer::renderChannel (const RackState& state, int i, int n) noexcept
{
    if (foreignChannel (i)) // (render-ahead: the other renderer renders it; the boundary brings it here)
    {
        releasingVoices[(std::size_t) i] = 0; // (round 190oa: its tail isn't heard here)
        importChannel (i, n);
        return;
    }
    const auto& c = state.song.channels[(std::size_t) i];
    const auto automation = state.hasAutomation;
    const auto knobVolume = liveLevel (state, &LiveLevels::channelVolume, i, c.volume);
    const auto knobPan = liveLevel (state, &LiveLevels::channelPan, i, c.pan);
    auto volume = channelGain (automation ? automated (state.channelVolumeAuto[(std::size_t) i], knobVolume) : knobVolume);
    auto pan = automation ? automated (state.channelPanAuto[(std::size_t) i], (knobPan + 1.0f) * 0.5f) * 2.0f - 1.0f : knobPan;
    // FL's Levels adjustment (round 190dz, REA_rodada3 §2 FUN_00fe9d20 / FUN_00fe9bf0): gain f(volume)·f(multiplier),
    // pan + offset. Neutral (every project before it) costs one comparison and changes nothing.
    if (const auto& adjust = c.misc.levels; automation || adjust.pan != 0.0f || adjust.volume != 1.0f)
    {
        const auto bound = automation && (std::size_t) i < state.channelAdjustAuto.size();
        const auto multiplier = bound ? automated (state.channelAdjustAuto[(std::size_t) i][leveladjust::volume], adjust.volume * 0.5f) * 2.0f : adjust.volume;
        const auto offset = bound ? automated (state.channelAdjustAuto[(std::size_t) i][leveladjust::pan], (adjust.pan + 1.0f) * 0.5f) * 2.0f - 1.0f
                                  : adjust.pan;
        if (multiplier != 1.0f)
            volume *= levelAdjustGain (multiplier);
        if (offset != 0.0f)
            pan = adjustedPan (pan, offset);
    }
    float gl = 0.0f, gr = 0.0f;
    if (c.instrument == InstrumentKind::plugin && state.song.flPluginLevels)
        flWrapperGains (pan, volume, state.song.panTriangular, gl, gr);
    else
        panGains (pan, volume, gl, gr);

    auto* l = channelBuffer (i, 0);
    auto* r = channelBuffer (i, 1);

    if ((std::size_t) i < state.instruments.size() && state.instruments[(std::size_t) i] != nullptr)
    {
        // (round 190hq: while the tempo can move, the tempo this chunk plays at; tempo-synced Resample samples follow it)
        state.instruments[(std::size_t) i]->setSongTempo (state.tempoMap || tempoOverride > 0.0 || playSpeed != 1.0 ? bpm : 0.0);
        state.instruments[(std::size_t) i]->setSongPosition (chunkTick);
    }

    auto& instrument = *state.instruments[(std::size_t) i];
    instrument.beginChunk (n); // (extra outputs: a generator Patcher's, REA_rodada141)
    instrument.setChannelLevels (pan, volume); // (FPC's pads pan around the channel's pan, round 190q)
    // audio clip envelopes (round 190gi): this snapshot's table, every block while the song has one (and once more when it
    // has none left, so no instrument keeps a table that is going away); a song without any skips the call
    if (! state.song.clipEnvelopes.empty() || clipEnvTableShown)
        instrument.setClipEnvelopes (state.song.clipEnvelopes.empty() ? nullptr : &state.song.clipEnvelopes, chunkTps);
    // issue #14: the channel's pitch as it is now (a hosted plugin bends to it; native engines took it at note on)
    // (round 167a: the native generators move their sounding notes with it, as FL's do)
    // (round 173d: + the controller's pitch wheel, which FL's generators follow through the channel's pitch)
    instrument.setChannelPitchNow (c.sampler.pitch * 100.0f, c.misc.fineTune + (c.misc.mainPitch ? state.song.masterPitch : 0.0f) + instrument.wheelCents());
    if (c.instrument == InstrumentKind::patcher) // its Hardware Insert nodes: this chunk's device buses (round 165c)
        instrument.setHardwareIo (liveInput, hwOut != nullptr && hwOut->active.load (std::memory_order_relaxed) ? hwOut : nullptr, liveInputOffset);

    // idle channel, no notes in this chunk and no PDC: nothing to do (neither clear nor sum into the mixer)
    const auto hasDelay = (std::size_t) i < state.channelDelays.size() && state.channelDelays[(std::size_t) i] != nullptr;
    const auto hasArp = ((std::size_t) i < state.arpeggiators.size() && state.arpeggiators[(std::size_t) i] != nullptr)
                        || ((std::size_t) i < state.echoes.size() && state.echoes[(std::size_t) i] != nullptr);
    const auto* sources = state.hasMidiRoutes && (std::size_t) i < state.midiSources.size() && ! state.midiSources[(std::size_t) i].empty()
                              ? &state.midiSources[(std::size_t) i] : nullptr;
    const auto routed = [sources] (int channel) {
        return sources != nullptr && std::find (sources->begin(), sources->end(), channel) != sources->end();
    };
    if (! hasDelay && ! hasArp && instrument.idle()
        && std::none_of (events.begin(), events.end(), [&] (const NoteEvent& e) { return e.channel == i || routed (e.channel); }))
    {
        channelSilent[(std::size_t) i] = 1;
        releasingVoices[(std::size_t) i] = 0; // (round 190oa: no tail left)
        return;
    }
    channelSilent[(std::size_t) i] = 0;
    const UnitTimer timer (profiling.load (std::memory_order_relaxed), units->channelNs[(std::size_t) i], &units->channelMaxNs[(std::size_t) i],
                           (countsAudio ? &units->blockSlowest : nullptr), dropouts::channelUnit (i));
    std::memset (l, 0, sizeof (float) * (std::size_t) n);
    std::memset (r, 0, sizeof (float) * (std::size_t) n);
    int position = 0;
    // the render calls up to `end`, split on the ticks of a stepped slide note (round 190jv): an engine moves a voice's
    // pitch between two calls, so each step lands on its tick, and the first one a sample after the slide's start
    auto& ticks = slideTicks[(std::size_t) i];
    auto* noteBlock = propsOf ((std::size_t) i); // (round 190ql: null = none, as past the old tables' end)
    auto* props = noteBlock != nullptr ? &noteBlock->slide : nullptr;
    const auto renderUntil = [&] (int end) noexcept {
        while (true)
        {
            // the next tick of the pitch slide and of the note properties' slide (issue #236; round 190pi: each on its
            // own schedule — the same ticks when one slide note moves both)
            const auto tickAt = [&] (double start, int k) {
                return k == 0 ? (start - chunkStartTick) / chunkTps + 1.0 : (start + k * flTickTicks - chunkStartTick) / chunkTps;
            };
            const auto pitchAt = ticks.k <= ticks.n && ticks.n > 0 ? tickAt (ticks.start, ticks.k) : HUGE_VAL;
            const auto propAt = props != nullptr && props->count > 0 && props->k <= props->steps && props->steps > 0
                                    ? tickAt (props->start, props->k) : HUGE_VAL;
            const auto at = std::min (pitchAt, propAt);
            if (! std::isfinite (at) || at >= (double) end)
                break;
            const auto split = (int) std::lround (at);
            if (split > position && split < end)
            {
                instrument.render (l + position, r + position, split - position, gl, gr);
                position = split;
            }
            if (pitchAt == at)
                ++ticks.k;
            if (propAt == at)
                stepPropSlide (instrument, i, props->k++);
        }
        if (end > position)
        {
            instrument.render (l + position, r + position, end - position, gl, gr);
            position = end;
        }
    };

    // arpeggiator: the channel's notes become the arpeggio sequence
    const NoteEvent* list = events.begin();
    int listCount = events.size();

    if ((std::size_t) i < state.arpeggiators.size() && state.arpeggiators[(std::size_t) i] != nullptr)
    {
        auto& arp = *state.arpeggiators[(std::size_t) i];
        int inCount = 0;

        for (const auto& e : events)
            if (e.channel == i && inCount < Arpeggiator::maxEvents)
                arp.input[(std::size_t) inCount++] = e;

        int outCount = 0;
        arp.flSlideLaw = state.song.slideNoteProps; // (round 190pi)
        arp.process (i, arp.input.data(), inCount, chunkStartTick, chunkTps, n, c.arp, arp.output.data(), outCount);
        for (int k = 0; k < outCount; ++k) // (the arpeggio's notes are its own: no scheduled note's identity, round 190ej)
            arp.output[(std::size_t) k].noteId = 0;
        // FL cuts an arpeggio note at its gate even on a sample that plays out (REA round 146b [EXEC: FL's renders of
        // its Sampler render test]): the arpeggio is one voice, so its "off" stops the channel's sound (not with the
        // echo delay, whose echoes keep ringing)
        if (! c.misc.echo.enabled())
            for (int k = 0; k < outCount; ++k)
                if (arp.output[(std::size_t) k].type == NoteEvent::Type::off)
                    arp.output[(std::size_t) k].type = NoteEvent::Type::choke;
        list = arp.output.data();
        listCount = outCount;
    }

    // FL's echo delay (Misc tab): the channel's notes (after the arpeggio) and their echoes
    const float* gains = nullptr;
    if ((std::size_t) i < state.echoes.size() && state.echoes[(std::size_t) i] != nullptr)
    {
        auto& echo = *state.echoes[(std::size_t) i];
        int inCount = 0;
        for (int k = 0; k < listCount; ++k)
            if (list[k].channel == i && inCount < channelmisc::NoteEcho::maxEvents)
                echo.input[(std::size_t) inCount++] = list[k];
        int outCount = 0;
        echo.process (i, echo.input.data(), inCount, chunkStartTick, chunkTps, n, c.misc.echo, echo.output.data(), echo.outputGains.data(), outCount);
        list = echo.output.data();
        listCount = outCount;
        gains = echo.outputGains.data();
    }
    const auto tracking = c.misc.key.pan != 0.0f || c.misc.velocity.pan != 0.0f;
    const auto modTracking = c.misc.key.modX != 0.0f || c.misc.key.modY != 0.0f || c.misc.velocity.modX != 0.0f || c.misc.velocity.modY != 0.0f;
    // FL's Add to key (round 149): the root note moves the notes of instruments that are not samplers
    const auto keyShift = c.misc.addToKey && c.instrument != InstrumentKind::sampler ? 60 - c.misc.rootNote : 0;

    auto sourceSet = false;
    for (int index = 0; index < listCount; ++index)
    {
        auto shifted = list[index];
        if (keyShift != 0 && shifted.channel == i)
            shifted.key = std::clamp (shifted.key + keyShift, 0, 127);
        const auto& e = shifted;

        if (e.channel != i)
        {
            // a note of a MIDI Out channel on this plugin's MIDI input port, with that MIDI Out's MIDI channel
            if (sources == nullptr || ! routed (e.channel)
                || (e.type != NoteEvent::Type::on && e.type != NoteEvent::Type::off && e.type != NoteEvent::Type::voiceProp))
                continue;
            if (e.port == -2) // (a VFX channel's own note: its processor's output goes out instead)
                continue;
            if (e.sampleOffset > position)
                renderUntil (e.sampleOffset);
            // (each route from that channel: its port and MIDI channel, round 162)
            for (const auto& route : state.midiRoutesIn[(std::size_t) i])
            {
                if (route.other != e.channel || (e.port >= 0 && route.port != e.port) || (e.port < 0 && route.portOnly))
                    continue;
                // (a VFX channel's voice keeps its colour as the MIDI channel; so does a MIDI Out's note with "Map note
                // color to MIDI channel", round 167i)
                const auto midiChannel = e.port >= 0 || ((std::size_t) e.channel < state.mapNoteColour.size() && state.mapNoteColour[(std::size_t) e.channel] != 0)
                                             ? (int) (e.colour & 15)
                                             : route.midiChannel;
                if (e.type == NoteEvent::Type::voiceProp) // (round 165c: outside a Patcher too)
                    instrument.voicePropertyPort (route.port, e.key, e.prop, e.velocity, midiChannel);
                else if (e.type == NoteEvent::Type::on)
                {
                    if (! c.muted && ! state.song.channels[(std::size_t) e.channel].muted)
                    {
                        if (e.port >= 0)
                        {
                            instrument.setNextNoteExpression (e.pan, e.cents, e.porta);
                            instrument.setNextNoteModulation (e.modX, e.modY);
                            instrument.setNextNoteColour (e.colour);
                        }
                        instrument.noteOnPort (route.port, e.key, e.velocity, midiChannel);
                    }
                }
                else
                {
                    if (e.port >= 0)
                        instrument.setNextNoteRelease (e.release);
                    instrument.noteOffPort (route.port, e.key, midiChannel);
                }
            }
            continue;
        }
        if (e.port < 0 && e.type == NoteEvent::Type::voiceProp && e.expression)
        {
            // MPE stage 2 (issue #108, core/NoteCurves.h): a song note's pitch / timbre / pressure curve moving while it
            // sounds, as a played MPE note's (the live queue): the channel's tuning on top of its own cents
            if (e.sampleOffset > position)
                renderUntil (e.sampleOffset);
            if (! c.muted)
                instrument.noteExpression (e.key, e.prop, e.velocity,
                                           c.misc.fineTune + instrument.wheelCents()
                                               + (c.misc.mainPitch && c.instrument != InstrumentKind::audioClip ? state.song.masterPitch : 0.0f));
            continue;
        }
        if (e.port >= 0 || e.type == NoteEvent::Type::voiceProp) // (what this channel sends out on a port is for the others)
            continue;

        if (e.sampleOffset > position)
            renderUntil (e.sampleOffset);
        // (round 190jl: a live note routed through the channel's note path keeps its MIDI input, so a MIDI Out does
        // not send it back to that device)
        if (e.source != 0 || sourceSet)
        {
            instrument.setNoteSource (e.source);
            sourceSet = e.source != 0;
        }

        auto& held = heldNotes[(std::size_t) i];
        const auto bit = std::uint64_t { 1 } << (e.key & 63);
        if (e.type == NoteEvent::Type::on)
        {
            if (e.key >= 0 && e.key < 128)
            {
                held[(std::size_t) (e.key >> 6)] |= bit;
                struckNotes[(std::size_t) i][(std::size_t) (e.key >> 6)] |= bit;
                if (state.modStateful) // (round 190ox: an envelope / keytrack / velocity modulator's notes)
                    noteStruck (i, e.key, e.velocity);
            }
            // FL's key region (round 164): the channel plays only the notes inside it (audio clips: the key is the clip)
            const auto inRegion = c.instrument == InstrumentKind::audioClip || c.inKeyRegion (list[index].key);
            if (! c.muted && inRegion)
            {
                // (an audio clip follows its fine tune, not the main pitch: LYNX never moved clips with it)
                const auto noteCents = e.cents + c.misc.fineTune + instrument.wheelCents()
                                       + (c.misc.mainPitch && c.instrument != InstrumentKind::audioClip ? state.song.masterPitch : 0.0f);
                // FL's channel Mono / Porta (round 168l): a Mono note over a held one moves the sounding voice instead
                const auto mode = polyMode (c, instrument);
                const auto glideSamples = (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, chunkTps.get()));
                auto& poly = polyLayers[(std::size_t) i];
                // (an echo delay's repeats are FL's generated voices: they don't enter the Mono list)
                const auto on = e.echo ? channelpoly::On {} : poly.noteOn (mode, e.key, noteCents, e.porta, e.colour);
                if (on.release >= 0)
                {
                    instrument.noteOff (on.release);
                    voiceOrders[(std::size_t) i].noteOff (on.release);
                }
                if (! on.start)
                {
                    if (on.slide)
                        instrument.slideTo (e.key, noteCents, glideSamples);
                    continue;
                }
                // Max polyphony: the oldest note is released first
                if (const auto limit = voiceLimitFor (c.maxPolyphony, channelMono (c)); limit > 0)
                    if (const auto victim = voiceOrders[(std::size_t) i].noteOn (e.key, limit); victim >= 0)
                        instrument.noteOff (victim);
                const auto pan = tracking ? channelmisc::trackedPan (c.misc, e.key, e.velocity, e.pan) : e.pan;
                // (porta: the voice starts at the group's last note and glides to its own pitch, HostSlide::anchor)
                instrument.setNextNoteExpression (pan, noteCents + (on.glide ? on.glideFrom : 0.0f), e.porta);
                HostSlide::pendingGlide() = { on.glide ? on.glideFrom : 0.0f, on.glide ? glideSamples : 0.0f, false };
                {
                    const auto [modX, modY] = modTracking ? channelmisc::trackedModulation (c.misc, e.key, e.velocity, e.modX, e.modY)
                                                          : std::pair<float, float> { e.modX, e.modY };
                    instrument.setNextNoteModulation (modX, modY);
                }
                instrument.setNextNoteColour (e.colour);

                if (e.clipPitch != 0.0f)
                    instrument.setNextClipPitch (e.clipPitch);
                if (e.clipStretch != 1.0f)
                    instrument.setNextClipStretch (e.clipStretch);
                if (e.fadeIn > 0.0f || e.fadeOut > 0.0f || e.fadeShape != 0)
                    instrument.setNextClipFades (e.clipElapsed, e.clipRemaining, e.fadeIn, e.fadeOut, e.fadeShape, e.fadeInTension, e.fadeOutTension);
                if (e.clipEnvelope >= 0) // (round 190gi)
                    instrument.setNextClipEnvelope (e.clipEnvelope, e.clipEnvTick);

                // an echo's volume: the instrument scales the voice, else (engines without it) the velocity
                auto velocity = e.velocity;
                const auto gain = e.gain * (gains != nullptr ? gains[index] : 1.0f);
                if (gain != 1.0f && ! instrument.setNextNoteGain (gain))
                    velocity *= gain;
                instrument.setNextNoteFinePitch (e.cents); // (issue #63: a hosted plugin's key and VST3 tuning)
                instrument.setNextNoteId (e.noteId);     // (round 190ej: the voice is this note's)
                if (e.key >= 0 && e.key < 128 && noteBlock != nullptr) // (issue #236: what a slide moves from)
                {
                    const auto [modX, modY] = modTracking ? channelmisc::trackedModulation (c.misc, e.key, e.velocity, e.modX, e.modY)
                                                          : std::pair<float, float> { e.modX, e.modY };
                    noteBlock->notes[(std::size_t) e.key] = { velocity, pan, modX, modY, noteCents, e.colour };
                }
                instrument.noteOnAt (e.key, velocity, e.startSeconds);
                instrument.setNextNoteId (0);
                HostSlide::pendingGlide() = {};
            }
        }
        else if (e.type == NoteEvent::Type::slide)
        {
            // FL's native generators: the slide moves in steps on FL's ticks (round 190jv, core/HostSlide.h Steps)
            const auto flTicks = stepsSlides (c.instrument) ? (int) std::lround ((double) e.slideSamples * chunkTps / flTickTicks) : 0;
            const auto ticksBefore = ticks; // (round 190pi: a slide that moves nothing leaves them)
            if (flTicks > 0)
            {
                HostSlide::pendingSteps() = { flTicks, (float) (flTickTicks / chunkTps / std::max (1.0, (double) e.slideSamples)) };
                ticks = { chunkStartTick + (double) e.sampleOffset * chunkTps, 0, flTicks };
            }
            const auto slideCents = e.cents + c.misc.fineTune + (c.misc.mainPitch ? state.song.masterPitch : 0.0f);
            // FL's slide note (round 190pi, REA_rodada190pi §2): it moves the held notes of its own colour only, each by the
            // interval from the highest of them to its key (HostSlide::pendingShift) — not every voice to its key; with
            // none of its colour held it moves nothing. One held key of its colour and nothing else: the plain slide
            NoteMask group {};
            bool plain = true, none = false;
            if (state.song.slideNoteProps && ! e.arpGlide && ! channelMono (c) && noteBlock != nullptr)
            {
                const auto& keys = heldNotes[(std::size_t) i];
                const auto* props = noteBlock->notes.data();
                int top = -1, inGroup = 0, all = 0;
                for (int k = 0; k < 128; ++k)
                    if ((keys[(std::size_t) (k >> 6)] >> (k & 63)) & 1u)
                    {
                        ++all;
                        if (props[k].colour == e.colour)
                            group[(std::size_t) (k >> 6)] |= std::uint64_t { 1 } << (k & 63), ++inGroup, top = k;
                    }
                none = all > 0 && inGroup == 0;
                plain = all == 0 || (inGroup == 1 && all == 1);
                if (! plain && ! none)
                {
                    auto& shift = HostSlide::pendingShift();
                    shift = { true, 100.0f * (float) (e.key - top) + slideCents - props[top].cents, e.key, group };
                }
            }
            if (! none)
                instrument.slideTo (e.key, slideCents, e.slideSamples);
            HostSlide::pendingShift() = {};
            HostSlide::pendingSteps() = {};
            if (none)
            {
                ticks = ticksBefore; // (FL 2026 [EXEC]: a slide note of a colour no held note has moves nothing)
                continue;
            }
            // issue #236 (round 190od): FL's slide note moves the sounding notes' velocity, pan and Mod X / Y to its own
            // as well, in the same n + 1 steps on FL's ticks (every engine: the property steps are the host's)
            // (an arpeggiator's own Slide step glides the pitch only, round 190pi)
            if (state.song.slideNoteProps && ! c.muted && ! e.arpGlide)
            {
                const auto n = std::max (0, (int) std::lround ((double) e.slideSamples * chunkTps / flTickTicks));
                startPropSlide (i, e, n, stepsSlides (c.instrument), plain ? nullptr : &group);
                if (noteBlock != nullptr && noteBlock->slide.count > 0)
                {
                    auto& p = noteBlock->slide;
                    p.start = chunkStartTick + (double) e.sampleOffset * chunkTps; // (its own ticks, round 190pi)
                    p.k = 0;
                    if (n == 0) // (shorter than a tick: at once)
                        stepPropSlide (instrument, i, 0);
                }
            }
        }
        else if (e.type == NoteEvent::Type::choke)
        {
            instrument.cut(); // short fade, like FL's "cut"
            if (noteBlock != nullptr)
                noteBlock->slide.count = 0; // (issue #236)
            held = {};
            voiceOrders[(std::size_t) i].clear();
            polyLayers[(std::size_t) i].clear();
        }
        else
        {
            instrument.setNextNoteRelease (e.release);
            // a Mono channel (round 168l): the voice ends with the last held note, else it glides back to the newest
            const auto mode = polyMode (c, instrument);
            const auto off = e.echo ? channelpoly::Off { e.key } : polyLayers[(std::size_t) i].noteOff (mode, e.key, e.colour);
            if (off.slide)
                instrument.slideTo (off.key, off.cents,
                                    (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, chunkTps.get())));
            if (off.release >= 0)
            {
                // FL ends the note's own voice (round 190ej); a Mono channel's shared voice ends with its last note
                instrument.setNextNoteOffId (mode.mono ? 0u : e.noteId);
                instrument.noteOff (off.release);
                instrument.setNextNoteOffId (0);
                voiceOrders[(std::size_t) i].noteOff (off.release);
            }
            if (e.key >= 0 && e.key < 128)
                held[(std::size_t) (e.key >> 6)] &= ~bit;
        }
    }

    if (sourceSet)
        instrument.setNoteSource (0);
    renderUntil (n);

    // round 190oa (DF-29): released voices count until the channel's tail has died away (−100 dB); looked at only
    // while there are some
    if (auto& releasing = releasingVoices[(std::size_t) i]; releasing != 0)
    {
        const auto quiet = [n] (const float* x) {
            float peak = 0.0f;
            for (int k = 0; k < n; ++k)
                peak = std::max (peak, std::abs (x[k]));
            return peak < 1.0e-5f;
        };
        if (instrument.idle() || (quiet (l) && quiet (r)))
            releasing = 0;
    }

    // PDC: this channel arrives before the track's others; delay until aligned
    if ((std::size_t) i < state.channelDelays.size() && state.channelDelays[(std::size_t) i] != nullptr)
        state.channelDelays[(std::size_t) i]->process (l, r, n);
}

void RackRenderer::applyTrackEq (const MixerTrack& track, int t, bool bypassed, float* l, float* r, int n) noexcept
{
    if ((std::size_t) t >= trackEqs.size())
        return;

    auto& eq = trackEqs[(std::size_t) t];

    if (bypassed || ! track.eq.active()) // clean mixer (round 163): no built-in processing, the samples pass untouched
    {
        if (eq.running)
            eq = {}; // off: reset the filters so they don't come back with a stale tail
        return;
    }

    if (! eq.running || ! (eq.settings == track.eq))
    {
        // new coefficients, keeping the filter state (no click when adjusting)
        const auto& s = track.eq;
        eq.bands[0].shelf (sampleRate, std::min ((double) s.hz[0], sampleRate * 0.45), s.gainDb[0], false, TrackEq::shelfSlope (s.width[0]));
        eq.bands[1].peaking (sampleRate, std::min ((double) s.hz[1], sampleRate * 0.45), TrackEq::peakQ (s.width[1]), s.gainDb[1]);
        eq.bands[2].shelf (sampleRate, std::min ((double) s.hz[2], sampleRate * 0.45), s.gainDb[2], true, TrackEq::shelfSlope (s.width[2]));

        for (std::size_t b = 0; b < 3; ++b)
            if (s.gainDb[b] == 0.0f)
                eq.bands[b].reset();

        eq.settings = s;
        eq.running = true;
    }

    for (std::size_t b = 0; b < 3; ++b)
    {
        if (eq.settings.gainDb[b] == 0.0f)
            continue; // band at 0 dB: not processed (like FL)

        eq.bands[b].processBlock (l, r, n);
    }
}

// effects whose controlOutput() an editor shows live
static bool hasLiveMeters (EffectKind kind) noexcept
{
    // round 129: the voice tools (Quell's reduction meter was never published) and the basic compressor's meters
    return mixsuite::isSuiteKind (kind) || dyn::isDynKind (kind) || kind == EffectKind::lynxTune || kind == EffectKind::facetEq
           || kind == EffectKind::apexLimiter || voicetools::isVoiceKind (kind) || kind == EffectKind::compressor
           || kind == EffectKind::echoform // round 135w: Echoform's output meter
           || kind == EffectKind::autoPan || kind == EffectKind::voiceShift // round 135: the pan position, the shifter's notes
           || kind == EffectKind::hardwareInsert // round 164d: send / return levels, the ping
           || kind == EffectKind::xyController || kind == EffectKind::xyzController // round 164: the pad's target and outputs
           || kind == EffectKind::peakController // issue #66: its display's Peak + LFO, Peak and LFO lines
           || kind == EffectKind::grossBeat // round 190ih: its playback locator and the playing / queued slots
           || kind == EffectKind::vocoder // round 190ld: Fruity Vocoder's live band levels (only while its editor shows them)
           || kind == EffectKind::fruityMultiband // round 190mf: Fruity Multiband Compressor's band meters (the same)
           || kind == EffectKind::maximus // round 190oa: Maximus' Monitor band levels (the same)
           || kind == EffectKind::pitchCorrect; // round 190oa: Pitcher's input / output pitch strips
}

RackState::SlotPlan RackState::planSlot (const RackState& state, int track, std::size_t i) noexcept
{
    SlotPlan p;
    const auto& slot = state.song.mixer[(std::size_t) track].effects[i];
    p.kind = slot.kind;
    p.active = state.song.slotActive (slot);
    p.mix = slot.mix;
    p.hasMixAuto = (std::size_t) track < state.effectMixAuto.size() && i < state.effectMixAuto[(std::size_t) track].size();
    p.mixAuto = p.hasMixAuto ? state.effectMixAuto[(std::size_t) track][i] : -1;
    p.isPlugin = slot.kind == EffectKind::plugin;
    p.routed = p.isPlugin && (std::size_t) track < state.slotConnections.size() && i < state.slotConnections[(std::size_t) track].size()
               && state.slotConnections[(std::size_t) track][i].routed;
    p.longHold = p.isPlugin || slot.kind == EffectKind::patcher;
    p.liveMeters = hasLiveMeters (slot.kind);
    p.sends = slot.kind == EffectKind::fruitySend || slot.kind == EffectKind::patcher || slot.kind == EffectKind::transmitter
              || slot.kind == EffectKind::flTransporter || slot.kind == EffectKind::equo || p.routed;
    // smart disable (runEffects; FL's global switch is read there, live):
    // (not Gross Beat: its buffer must keep recording the silence it may play back later, as in FL)
    // (nor Newtone / Newtime: in slave mode they play their sample whatever the input)
    // (nor a slot whose "Smart disable" is off, or a plugin that does not allow it — FL's per-plugin options, REA round 139)
    // (nor a hosted effect whose input 1 comes from another insert: its own input says nothing; round 189j)
    p.smartEligible = slot.smartDisable && ! (p.isPlugin && ! slot.plugin.allowSmartDisable)
                      && ! (p.routed && state.slotConnections[(std::size_t) track][i].mainSource > 0)
                      && slot.kind != EffectKind::grossBeat && slot.kind != EffectKind::newtone
                      && slot.kind != EffectKind::newtime
                      && slot.kind != EffectKind::scratcher // (nor the Scratcher: its turntable plays whatever the input)
                      && slot.kind != EffectKind::edison // (nor Edison: it records silence and plays whatever the input, round 190cd)
                      && slot.kind != EffectKind::hardwareInsert // (nor the hardware insert: outboard gear has its own tails and noise)
                      && ! (slot.kind == EffectKind::patcher && patcher::usesHardwareIo (slot.patcher)) // (nor a Patcher holding one, round 165c)
                      // (nor a controller: its outputs move whatever the sound — a silent insert is their usual place; round 164)
                      && slot.kind != EffectKind::formulaController
                      && slot.kind != EffectKind::xyController && slot.kind != EffectKind::xyzController
                      && slot.kind != EffectKind::controlSurface
                      && slot.kind != EffectKind::peakController // (issue #66: its LFO runs and its peak falls in silence, as FL's)
                      // round 164c: a visual plugin keeps recording silence (its display falls, the video export stays in time)
                      && slot.kind != EffectKind::waveCandy && slot.kind != EffectKind::visualizer
                      && slot.kind != EffectKind::tuner // (round 165g: the tuner's display follows the signal into silence)
                      // (round 166f: nor Facet EQ playing its comparative target — the sidechain plays while this track is silent)
                      && ! (slot.kind == EffectKind::facetEq && facet::listensToTarget (slot.params));
    // round 190gp: a built-in's declared tail (Effect::tailSamples) may end its smart-disable hold early; not with a
    // side input (what it hears there moves its state while its own input is silent)
    // (round 190jg: a hosted plugin too, when it declares its tail — CLAP's clap.tail, PluginEffect::tailSamples)
    p.tailHold = (! p.longHold || p.isPlugin) && slot.sidechain < 0 && slot.sidechain2 < 0;
    p.used = (int) std::min (slot.params.size(), effectInfo (slot.kind).params.size());
    // round 190pt: a Vocodex with liveControls has FL's 20 controls as hidden parameters that automation moves
    if (slot.kind == EffectKind::vocodex && slot.params[(std::size_t) vocodex::liveControls] >= 0.5f)
        p.used = vocodex::paramCount;
    p.firstAutomated = p.used;
    if ((std::size_t) track < state.effectParamAuto.size() && i < state.effectParamAuto[(std::size_t) track].size())
    {
        const auto& autos = state.effectParamAuto[(std::size_t) track][i];
        p.firstAutomated = 0;
        while (p.firstAutomated < p.used && autos[(std::size_t) p.firstAutomated] < 0)
            ++p.firstAutomated;
    }
    return p;
}

void RackState::buildSlotPlans()
{
    slotPlans.assign (effects.size(), {});
    for (std::size_t t = 0; t < effects.size() && t < song.mixer.size(); ++t)
    {
        const auto count = std::min (effects[t].size(), song.mixer[t].effects.size());
        slotPlans[t].reserve (count);
        for (std::size_t i = 0; i < count; ++i)
            slotPlans[t].push_back (planSlot (*this, (int) t, i));
    }
}

std::pair<const float*, const float*> RackRenderer::sideInput (const RackState& state, int track, int index, int k, int source,
                                                              bool sounding, int n) noexcept
{
    const RackState::SideLine* line = nullptr;
    if ((std::size_t) track < state.sideLines.size() && (std::size_t) index < state.sideLines[(std::size_t) track].size())
        line = state.sideLines[(std::size_t) track][(std::size_t) index][(std::size_t) k].get();
    // FL's "When recording or not playing" source: no compensation now (as its routes)
    if (line != nullptr && pdcBypassNow && (std::size_t) source < state.bypassWhenIdle.size() && state.bypassWhenIdle[(std::size_t) source] != 0)
        line = nullptr;
    if (line == nullptr || line->delay == nullptr)
        return sounding ? std::pair { trackInput (source, 0), trackInput (source, 1) } : std::pair<const float*, const float*> { nullptr, nullptr };
    // round 190hq (B13): the source, as late as the slot's own input. A silent source still drains what the line holds.
    auto& delay = *line->delay;
    if (! sounding && delay.silent())
        return { nullptr, nullptr };
    n = std::min (n, RackState::SideLine::capacity);
    auto* out = const_cast<float*> (line->out.data()); // (the slot's own buffer: only this slot's renderer writes it)
    auto* outR = out + RackState::SideLine::capacity;
    std::memset (out, 0, sizeof (float) * (std::size_t) n);
    std::memset (outR, 0, sizeof (float) * (std::size_t) n);
    delay.mixInto (sounding ? trackInput (source, 0) : nullptr, sounding ? trackInput (source, 1) : nullptr, 1.0f, out, outR, n);
    return { out, outR };
}

void RackRenderer::setSidechain (const RackState& state, const EffectSlot& slot, Effect& effect, int track, int index, int n) noexcept
{
    // the source was already processed in this pass (earlier layer); silent = no signal in the detector
    for (const auto second : { false, true })
    {
        const auto source = second ? slot.sidechain2 : slot.sidechain;
        if (source < 0 || source == track || source >= (int) state.song.mixer.size())
            continue;
        const auto active = (std::size_t) source < trackActive.size() && trackActive[(std::size_t) source] != 0;
        const auto [left, right] = sideInput (state, track, index, second ? 1 : 0, source, active, n);
        if (second)
            effect.setSidechain2 (left, right);
        else
            effect.setSidechain (left, right);
    }
}

void RackRenderer::connect (const RackState& state, const SlotConnections& c, Effect& effect, int track, int index, const float* l, const float* r,
                            int n) noexcept
{
    // a hosted effect's Connections (round 189j [EXEC FL 2026]): input 1 from the insert its index picks (processed
    // earlier, like a sidechain source; silent when it is), input 2 the slot's own input when no source feeds it
    auto sounding = [&] (int source) {
        return source > 0 && source != track && source < (int) state.song.mixer.size() && (std::size_t) source < trackActive.size()
               && trackActive[(std::size_t) source] != 0;
    };
    auto valid = [&] (int source) { return source > 0 && source != track && source < (int) state.song.mixer.size(); };
    if (c.mainSource == -2)
        effect.setMainInput (nullptr, nullptr);
    else if (c.mainSource > 0)
    {
        const auto [ml, mr] = valid (c.mainSource) ? sideInput (state, track, index, 2, c.mainSource, sounding (c.mainSource), n)
                                                   : std::pair<const float*, const float*> { nullptr, nullptr };
        effect.setMainInput (ml, mr);
    }
    if (c.sideSource == -1)
        effect.setSidechain (l, r);
    else if (c.sideSource == -2)
        effect.setSidechain (nullptr, nullptr);
    else // (its index, when no side input source is picked: setSidechain fed the slot's own one, if any)
    {
        const auto [sl, sr] = valid (c.sideSource) ? sideInput (state, track, index, 3, c.sideSource, sounding (c.sideSource), n)
                                                   : std::pair<const float*, const float*> { nullptr, nullptr };
        effect.setSidechain (sl, sr);
    }
}

void RackRenderer::runGoalSlot (const EffectSlot& slot, Effect& effect, std::size_t key, float* l, float* r, int n, const EffectParams& params,
                                double bpm) noexcept
{
    if (slot.goal == 0 || key >= goalGlideAmount.size())
    {
        effect.process (l, r, n, params, bpm);
        return;
    }
    // round 190dg: a one-knob slot whose knob moved since the last block, on the same effect: the knob glides from where
    // it was to where it is now over this block, in short steps (every parameter the knob drives follows it; the rest
    // are the slot's as they are)
    const auto from = goalGlideAmount[key];
    const bool same = goalGlideEffect[key] == &effect;
    goalGlideEffect[key] = &effect;
    if (! same || ! std::isfinite (from) || from == slot.goalAmount || ! goals::active (slot))
    {
        goalGlideAmount[key] = slot.goalAmount;
        effect.process (l, r, n, params, bpm);
        return;
    }
    // (at most the whole range in fullTurnSeconds: a jump becomes a glide over the next blocks)
    const auto most = (float) (n / (fullTurnSeconds * sampleRate));
    const auto to = std::abs (slot.goalAmount - from) <= most ? slot.goalAmount : from + std::copysign (most, slot.goalAmount - from);
    goalGlideAmount[key] = to;
    auto moving = params; // (a copy on the stack, no allocation)
    for (int at = 0; at < n; at += goalGlideStep)
    {
        const auto m = std::min (goalGlideStep, n - at);
        goals::writeParams (slot.goal, from + (to - from) * (float) (at + m) / (float) n, moving);
        effect.process (l + at, r + at, m, moving, bpm);
    }
}

void RackRenderer::runEffects (const RackState& state, int track, int n) noexcept
{
    if (track >= (int) state.effects.size())
        return;

    const auto& slots = state.song.mixer[(std::size_t) track].effects;
    const auto& effects = state.effects[(std::size_t) track];

    // (round 190gp: the insert's zeros, when nothing came in, are stored only once a slot runs: trackBuffer)
    auto* l = rawTrackBuffer (track, 0);
    auto* r = rawTrackBuffer (track, 1);
    int audible = -1; // -1 = not measured yet; an effect's output is the next one's input
    // round 190bu: the chain's Blend (nullptr at 100 %): the insert's input is kept before the first slot
    static_assert (chunkCapacity <= RackState::ChainBlendStage::capacity);
    auto* blend = (std::size_t) track < state.chainBlends.size() ? state.chainBlends[(std::size_t) track].get() : nullptr;
    if (blend != nullptr)
    {
        (void) trackBuffer (track, 0);
        std::memcpy (blend->dry.data(), l, sizeof (float) * (std::size_t) n);
        std::memcpy (blend->dry.data() + RackState::ChainBlendStage::capacity, r, sizeof (float) * (std::size_t) n);
    }
    const auto timing = times->enabled.load (std::memory_order_relaxed);
    auto timed = [this, timing, n] (std::size_t key, auto&& run) {
        if (! timing || key >= times->effectNs.size())
        {
            run();
            return;
        }
        const auto before = ProcessTimes::now();
        run();
        times->addCall (false, key, ProcessTimes::now() - before, audioNsOf (n)); // (round 190of: + longest call, spikes)
    };

    // round 190gp: the slots' plan (RackState::SlotPlan), made with the snapshot; worked out here only for a state that
    // has none
    const auto* plans = (std::size_t) track < state.slotPlans.size() ? &state.slotPlans[(std::size_t) track] : nullptr;
    for (std::size_t i = 0; i < effects.size() && i < slots.size(); ++i)
    {
        const auto plan = plans != nullptr && i < plans->size() ? (*plans)[i] : RackState::planSlot (state, track, i);
        // slot dry/wet (automatable)
        const auto wet = state.hasAutomation && plan.hasMixAuto ? std::clamp (automated (plan.mixAuto, plan.mix), 0.0f, 1.0f) : plan.mix;

        auto clearMeters = [this, track, i] {
            const auto at = ((std::size_t) track * maxEffectSlots + i) * ProcessTimes::meterOutputs;
            if (at + ProcessTimes::meterOutputs <= times->effectMeters.size())
                for (int o = 0; o < ProcessTimes::meterOutputs; ++o)
                    times->effectMeters[at + (std::size_t) o].store (0.0f, std::memory_order_relaxed);
        };

        if (! plan.active || wet <= 0.0f)
        {
            if (plan.liveMeters)
                clearMeters();
            continue;
        }

        // "Low latency while recording" (round 190aq): a latency-adding slot on a live insert's path is skipped; when
        // it comes back it starts from a reset state
        if (const auto at = (std::size_t) track * maxEffectSlots + i; at < lowLatencySkipped.size())
        {
            if (lowlatency::bypassed (state.lowLatencySlots, track, (int) i))
            {
                lowLatencySkipped[at] = 1;
                if (plan.liveMeters)
                    clearMeters();
                continue;
            }
            if (lowLatencySkipped[at] != 0)
            {
                lowLatencySkipped[at] = 0;
                effects[i]->reset();
            }
        }

        // smart disable (like FL): an effect with silent input and output is skipped (plugins
        // after 4 s; built-ins after 1 s, which is well beyond their tail)
        const auto key = (std::size_t) track * maxEffectSlots + i;
        const auto isPlugin = plan.isPlugin;
        const auto* connections = plan.routed ? &state.slotConnections[(std::size_t) track][i] : nullptr; // (round 189j: a hosted effect's routed Connections)
        const auto hold = (int) (sampleRate * (plan.longHold ? 4.0 : 1.0)); // (a Patcher may hold plugins)

        // (not the slots RackState::planSlot lists, made with the snapshot; round 190gp — nor anything while FL's
        // Settings › Audio › CPU › "Smart disable" is off: the global switch, round 190bx)
        if (key < smartDisable.size() && plan.smartEligible && flsettings::options().smartDisable.load (std::memory_order_relaxed))
        {
            auto& sd = smartDisable[key];

            if (sd.effect != effects[i].get())
                sd = { effects[i].get(), 0 };

            if (audible < 0)
                audible = ! trackZeros (track) && anyAbove (l, r, n, silenceLevel) ? 1 : 0;

            // round 190gp: a built-in whose declared tail is shorter than the hold is taken out once it has settled
            // (round 190kr: asked only when it can decide: past the hold the slot is out whatever its tail, and an audible
            // input keeps it on. silent ≥ min (hold, t) ⇔ silent ≥ hold or silent ≥ t; the call was a cache miss on the
            // effect object of every waiting slot at every block)
            auto pastTail = [&] {
                const auto tail = plan.tailHold && declaredTails() ? effects[i]->tailSamples() : -1;
                return tail >= 0 && sd.silentSamples >= std::max (tail, 1);
            };
            if (audible == 1)
                sd.silentSamples = 0;
            // (at least one silent block runs: its output is measured, and one the effect makes audible keeps it on)
            else if (sd.silentSamples >= hold || pastTail())
            {
                if (plan.liveMeters)
                    clearMeters();
                continue;
            }
            else
                sd.silentSamples += n;
        }

        (void) trackBuffer (track, 0); // (round 190gp: this slot runs: the insert's zeros are stored, if it has only those)

        // the editor's spectrum analyser listens to this slot: its input
        const auto tapped = times->analyzer.key.load (std::memory_order_relaxed) == (int) key;
        if (tapped)
        {
            auto& tap = times->analyzer;
            const auto w = tap.written.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k)
                tap.pre[(std::size_t) ((w + (std::uint32_t) k) & (ProcessTimes::AnalyzerTap::size - 1))].store (0.5f * (l[k] + r[k]), std::memory_order_relaxed);
            // round 166f: and its sidechain (Facet EQ draws its comparative target)
            const auto source = slots[i].sidechain;
            const bool side = source >= 0 && source != track && source < (int) state.song.mixer.size();
            const bool sounding = side && (std::size_t) source < trackActive.size() && trackActive[(std::size_t) source] != 0;
            const auto* sl = sounding ? trackInput (source, 0) : nullptr;
            const auto* sr = sounding ? trackInput (source, 1) : nullptr;
            for (int k = 0; k < n; ++k)
                tap.side[(std::size_t) ((w + (std::uint32_t) k) & (ProcessTimes::AnalyzerTap::size - 1))].store (sl != nullptr ? 0.5f * (sl[k] + sr[k]) : 0.0f, std::memory_order_relaxed);
            tap.sideOn.store (side, std::memory_order_relaxed);
        }

        // dry/wet: keep the dry signal to mix later
        // (not the hardware insert: its dry path is delayed by its round trip inside it, so the slot's mix scales its own
        // dry/wet instead — round 164d)
        const auto partial = wet < 0.999f && slots[i].kind != EffectKind::hardwareInsert;

        if (partial)
        {
            std::memcpy (trackScratch (track, 0), l, sizeof (float) * (std::size_t) n);
            std::memcpy (trackScratch (track, 1), r, sizeof (float) * (std::size_t) n);
        }

        const UnitTimer timer (profiling.load (std::memory_order_relaxed), units->effectNs[std::min (key, units->effectNs.size() - 1)],
                               &units->effectMaxNs[std::min (key, units->effectNs.size() - 1)], (countsAudio ? &units->blockSlowest : nullptr),
                               dropouts::effectUnit (std::min (key, units->effectNs.size() - 1)));

        // the hardware insert (round 164d): this piece's device input and output buses (none during an export)
        if (slots[i].kind == EffectKind::hardwareInsert || slots[i].kind == EffectKind::patcher) // (a Patcher hands them to its nodes, round 165c)
            effects[i]->setHardwareIo (liveInput, hwOut != nullptr && hwOut->active.load (std::memory_order_relaxed) ? hwOut : nullptr, liveInputOffset);

        // Fruity Send / Patcher / Transmitter / Transporter: what it sends goes to its own buffer, pulled by the destination insert
        // (processed later)
        if (plan.sends)
        {
            for (int o = 0; o < maxSlotSends; ++o)
            {
                const auto key = i * (std::size_t) maxSlotSends + (std::size_t) o;
                const auto tap = (std::size_t) track < state.slotTap.size() && key < state.slotTap[(std::size_t) track].size()
                                     ? state.slotTap[(std::size_t) track][key]
                                     : -1;
                if (tap >= 0 && tap < RackState::maxSendTaps)
                {
                    std::memset (tapBuffer (tap, 0), 0, sizeof (float) * (std::size_t) n);
                    std::memset (tapBuffer (tap, 1), 0, sizeof (float) * (std::size_t) n);
                    effects[i]->setExtraOutput (o, tapBuffer (tap, 0), tapBuffer (tap, 1));
                    tapActive[(std::size_t) tap] = 1;
                }
                else
                    effects[i]->setExtraOutput (o, nullptr, nullptr);
            }
        }

        // round 190gp: whether an editor shows the slot's live meters (Apex Limiter's loudness meter runs only then)
        if (plan.liveMeters && key < times->metersWanted.size())
            effects[i]->setMetersShown (times->metersWanted[key].load (std::memory_order_relaxed) > 0);

        // round 170c: the slot's input peak for an editor's level meter (only while an editor asks)
        const bool levels = key < times->levelWanted.size() && times->levelWanted[key].load (std::memory_order_relaxed) > 0;
        if (levels)
        {
            auto& in = times->slotLevels[key * 2];
            const auto pl = peakOf (l, n), pr = peakOf (r, n);
            in.store (std::max (in.load (std::memory_order_relaxed), std::max (pl, pr)), std::memory_order_relaxed);
            auto& il = times->slotInputSides[key * 2];
            auto& ir = times->slotInputSides[key * 2 + 1];
            il.store (std::max (il.load (std::memory_order_relaxed), pl), std::memory_order_relaxed);
            ir.store (std::max (ir.load (std::memory_order_relaxed), pr), std::memory_order_relaxed);
        }

        if (slots[i].kind == EffectKind::hardwareInsert && wet < 0.999f)
        {
            auto params = slots[i].params; // copy without allocation
            if (state.hasAutomation)
            {
                const auto& autos = state.effectParamAuto[(std::size_t) track][i];
                const auto used = std::min (params.size(), effectInfo (slots[i].kind).params.size());
                for (std::size_t p = 0; p < used; ++p)
                    params[p] = automated (autos[p], params[p]);
            }
            params[(std::size_t) hwinsert::dryWet] *= wet;
            timed (key, [&] { effects[i]->process (l, r, n, params, bpm); });
        }
        else if (state.hasAutomation && ! isPlugin)
        {
            // round 190ge: only the automated parameters are worked out, into a copy only when there is one. Most slots of
            // a song with automation have none, and the copy (768 bytes) and a lookup per parameter, at every block,
            // were most of this function's own time in a big project (an unautomated parameter keeps its value either way)
            // (round 190gp: the first automated one comes from the slot's plan)
            const auto& autos = state.effectParamAuto[(std::size_t) track][i];
            const auto used = (std::size_t) plan.used;
            const auto firstAutomated = (std::size_t) plan.firstAutomated;
            EffectParams automatedParams; // (filled only when used)
            if (firstAutomated < used)
            {
                automatedParams = slots[i].params;
                for (std::size_t p = firstAutomated; p < used; ++p)
                    if (autos[p] >= 0)
                        automatedParams[p] = compensatedAutomation (state, autos[p], track, (int) i, automated (autos[p], automatedParams[p]));
            }
            const auto& params = firstAutomated < used ? automatedParams : slots[i].params;

            effects[i]->setSongPosition (chunkTick);
            effects[i]->setPlaying (chunkPlaying);
            effects[i]->setHeldNotes (heldNotes.data(), maxChannels);
            setSidechain (state, slots[i], *effects[i], track, (int) i, n);
            if (connections != nullptr)
                connect (state, *connections, *effects[i], track, (int) i, l, r, n);
            timed (key, [&] { runGoalSlot (slots[i], *effects[i], key, l, r, n, params, bpm); });
        }
        else
        {
            effects[i]->setSongPosition (chunkTick);
            effects[i]->setPlaying (chunkPlaying);
            effects[i]->setHeldNotes (heldNotes.data(), maxChannels);
            setSidechain (state, slots[i], *effects[i], track, (int) i, n);
            if (connections != nullptr)
                connect (state, *connections, *effects[i], track, (int) i, l, r, n);
            const auto& shown = overrideParams != nullptr && track == overrideTrack && (int) i == overrideSlot ? *overrideParams : slots[i].params;
            timed (key, [&] { runGoalSlot (slots[i], *effects[i], key, l, r, n, shown, bpm); });
        }

        // an effect that output NaN / inf (an unstable setting): this block of it is muted and its state reset, so the
        // broken values never reach the rest of the mix (they would stay in every filter and feedback path after it)
        if (! finiteBlock (l, r, n))
        {
            std::memset (l, 0, sizeof (float) * (std::size_t) n);
            std::memset (r, 0, sizeof (float) * (std::size_t) n);
            effects[i]->reset();
            nonFiniteBlocks.fetch_add (1, std::memory_order_relaxed);
        }

        if (partial)
        {
            const auto* dl = trackScratch (track, 0);
            const auto* dr = trackScratch (track, 1);

            for (int k = 0; k < n; ++k)
            {
                l[k] = dl[k] + (l[k] - dl[k]) * wet;
                r[k] = dr[k] + (r[k] - dr[k]) * wet;
            }
        }

        if (levels) // round 170c: and its output peak
        {
            auto& out = times->slotLevels[key * 2 + 1];
            const auto pl = peakOf (l, n), pr = peakOf (r, n);
            out.store (std::max (out.load (std::memory_order_relaxed), std::max (pl, pr)), std::memory_order_relaxed);
            auto& ol = times->slotOutputSides[key * 2];
            auto& orr = times->slotOutputSides[key * 2 + 1];
            ol.store (std::max (ol.load (std::memory_order_relaxed), pl), std::memory_order_relaxed);
            orr.store (std::max (orr.load (std::memory_order_relaxed), pr), std::memory_order_relaxed);
        }

        // ... and its output
        if (tapped)
        {
            auto& tap = times->analyzer;
            const auto w = tap.written.load (std::memory_order_relaxed);
            for (int k = 0; k < n; ++k)
                tap.post[(std::size_t) ((w + (std::uint32_t) k) & (ProcessTimes::AnalyzerTap::size - 1))].store (0.5f * (l[k] + r[k]), std::memory_order_relaxed);
            tap.sampleRate.store (sampleRate, std::memory_order_relaxed);
            tap.written.store (w + (std::uint32_t) n, std::memory_order_release);
        }

        // live meters for the editor (the mixing suite's gain reduction, LYNX Tune's pitch, Facet EQ's dynamic gains)
        if (plan.liveMeters && key < (std::size_t) maxMixerTracks * maxEffectSlots)
            for (int o = 0; o < ProcessTimes::meterOutputs; ++o)
                times->effectMeters[key * ProcessTimes::meterOutputs + (std::size_t) o].store (effects[i]->controlOutput (o), std::memory_order_relaxed);

        // the effect changed the signal: measure again (it is the next slot's input)
        audible = anyAbove (l, r, n, silenceLevel) ? 1 : 0;

        if (audible == 1 && key < smartDisable.size())
            smartDisable[key].silentSamples = 0; // tail (reverb, delay): keep processing
    }

    // round 190bu: the chain's Blend: the kept input, as late as the chain, mixed with the chain's output
    if (blend != nullptr)
    {
        auto* dl = blend->dry.data();
        auto* dr = blend->dry.data() + RackState::ChainBlendStage::capacity;
        if (blend->delay != nullptr)
            blend->delay->process (dl, dr, n);
        const auto wet = std::clamp (state.song.mixer[(std::size_t) track].chainBlend, 0.0f, 1.0f);
        const auto from = blend->lastWet.load (std::memory_order_relaxed);
        // round 190dg: a moved Blend ramps (a step of a dry / wet mix clicks), at most the whole range in
        // fullTurnSeconds, and only once a new stage's dry path is filled
        auto to = from;
        if (blend->warmup > 0)
            blend->warmup -= n;
        else
            to = from + std::clamp (wet - from, -(float) (n / (fullTurnSeconds * sampleRate)), (float) (n / (fullTurnSeconds * sampleRate)));
        if (from == to || n <= 0)
            for (int k = 0; k < n; ++k)
            {
                l[k] = dl[k] + (l[k] - dl[k]) * to;
                r[k] = dr[k] + (r[k] - dr[k]) * to;
            }
        else
        {
            const auto step = (to - from) / (float) n;
            for (int k = 0; k < n; ++k)
            {
                const auto w = from + step * (float) (k + 1);
                l[k] = dl[k] + (l[k] - dl[k]) * w;
                r[k] = dr[k] + (r[k] - dr[k]) * w;
            }
            blend->lastWet.store (std::abs (to - wet) < 1.0e-6f ? wet : to, std::memory_order_relaxed);
        }
    }
}

std::uint64_t RackRenderer::nonFiniteEffectBlocks() noexcept
{
    return nonFiniteBlocks.load (std::memory_order_relaxed);
}

void RackRenderer::process (const RackState* state,
                            bool playing,
                            double startTick,
                            int numSamples,
                            float* left,
                            float* right,
                            LiveEventQueue* live,
                            MeterBank* meters,
                            LiveEventQueue* moreLive) noexcept
{
    const ScopedFlushDenormals noDenormals;
    const ScopedHostTimeline timelineScope (ownTimeline); // (round 190gq: a renderer ahead of the song has its own play head)
    const UnitTimer timer (countsAudio && profiling.load (std::memory_order_relaxed), units->processNs);
    if (countsAudio && profiling.load (std::memory_order_relaxed))
        units->processedSamples.fetch_add ((std::uint64_t) std::max (0, numSamples), std::memory_order_relaxed);
    std::memset (left, 0, sizeof (float) * (std::size_t) numSamples);
    std::memset (right, 0, sizeof (float) * (std::size_t) numSamples);
    events.clear();
    splitChunk = 0;
    if (splitBlock != nullptr && split != nullptr && split->exports)
        splitBlock->chunks = 0;

    if (state == nullptr || blockCapacity == 0 || ! buffersFor (*state))
        return;

    const auto timing = times->enabled.load (std::memory_order_relaxed);
    const auto wallStart = timing ? ProcessTimes::now() : 0;

    // A snapshot built before the device opened may have another rate. The instruments'
    // prepare() does not allocate, so it can run here.
    if (state != lastState && state->sampleRate != sampleRate)
    {
        for (std::size_t c = 0; c < state->instruments.size(); ++c)
            if (! foreignChannel ((int) c))
                state->instruments[c]->prepare (sampleRate);

        for (std::size_t t = 0; t < state->effects.size(); ++t)
            if (! foreignTrack ((int) t))
                for (auto& effect : state->effects[t])
                    effect->prepare (sampleRate);
    }

    // channels inserted, removed or moved (round 168s): their held keys and voice order go with them
    if (state != lastState && lastState != nullptr && state->builtFrom == lastSerial && state->channelsMoved
        && voiceOrderScratch.size() == (std::size_t) maxChannels && heldScratch.size() == (std::size_t) maxChannels)
    {
        for (std::size_t c = 0; c < (std::size_t) maxChannels; ++c)
        {
            voiceOrderScratch[c] = voiceOrders[c];
            heldScratch[c] = heldNotes[c];
            if (c < polyLayers.size() && c < polyScratch.size())
                polyScratch[c] = polyLayers[c];
        }
        for (std::size_t c = 0; c < (std::size_t) maxChannels; ++c)
        {
            const auto from = c < state->channelFrom.size() ? state->channelFrom[c] : -1;
            voiceOrders[c] = from >= 0 && from < maxChannels ? voiceOrderScratch[(std::size_t) from] : VoiceOrder {};
            heldNotes[c] = from >= 0 && from < maxChannels ? heldScratch[(std::size_t) from] : NoteMask {};
            if (c < polyLayers.size() && c < polyScratch.size()) // (round 168l's mono / porta layers move too)
                polyLayers[c] = from >= 0 && (std::size_t) from < polyScratch.size() ? polyScratch[(std::size_t) from] : channelpoly::Layer {};
        }
        scheduledNotes->moveChannels (state->channelFrom); // (round 169d)
        for (int ch = 0, end = propsCount.load (std::memory_order_acquire); ch < end; ++ch) // (issue #236: a slide in progress stops where channels moved)
            if (auto* block = propsOf ((std::size_t) ch))
                block->slide.count = 0;
    }
    lastState = state; // so prepare() knows which instruments to reconfigure
    lastSerial = state->serial;

    // Stopping, or jumping to another position (seek/rewind), releases held notes:
    // their "off" events will no longer happen.
    const auto jumped = playing && wasPlaying && std::abs (startTick - expectedTick) > 1.0;

    if ((wasPlaying && ! playing) || (playing && ! wasPlaying))
    {
        // the arpeggiators / echoes ran on the live clock while stopped (round 149): they restart on the song's
        liveRoutedCount = 0;
        resetNoteTools (*state);
    }

    // a start, a stop or a jump: every note was released (or none sounds yet)
    if (playing != wasPlaying || jumped)
        scheduledNotes->clear();

    if ((wasPlaying && ! playing) || jumped)
    {
        for (std::size_t c = 0; c < state->instruments.size(); ++c)
            if (! foreignChannel ((int) c))
            {
                if (jumped)
                    state->instruments[c]->allNotesOff();
                else
                    state->instruments[c]->transportStopped(); // (a hosted plugin: FL's "Send All Notes Off when playback stops")
            }
        heldNotes.fill ({});
        clearPolyLayers();
        resetNoteTools (*state);
    }

    // FL resets its generators when playback starts: a MIDI Out sends its bank, patch and controls again
    // (REA_rodada167i §1.4)
    if (playing && ! wasPlaying)
        for (std::size_t c = 0; c < state->song.channels.size() && c < state->instruments.size(); ++c)
            if (state->song.channels[c].instrument == InstrumentKind::midiOut && state->instruments[c] != nullptr && ! foreignChannel ((int) c))
                state->instruments[c]->transportStarted();

    // started playing or jumped: audio clips in progress must start mid-way
    resumeAudio = playing && (! wasPlaying || jumped);
    // FL's "Play truncated notes on transport": at the start of playback, and after the user moves the position while
    // playing (FL's set-position FUN_011e7a10 → FUN_011e78a0 → FUN_011e7470 with its catch-up flag set, round 167k),
    // the notes already sounding there play. The loop's wrap is not a set-position and chases nothing.
    const auto userJump = jumped && userJumpSerial != seenUserJumpSerial;
    seenUserJumpSerial = userJumpSerial;
    chaseNotes = playing && (! wasPlaying || userJump) && playTruncatedOnTransport;
    // FL's "Reset plugins on transport" (FUN_011e7a10 → FUN_011c8a40 mode 2 → FUN_012b5210, every registered plugin's
    // reset, generators included): the effects' tails and buffers are cleared and the plugin generators stop when
    // playback starts or jumps (round 167k: instruments too, hosted plugins with their wrapper's reset option)
    if (resetPluginsOnTransport && playing && (! wasPlaying || jumped))
    {
        for (std::size_t t = 0; t < state->effects.size(); ++t)
            if (! foreignTrack ((int) t))
                for (auto& effect : state->effects[t])
                    if (effect != nullptr)
                        effect->transportReset();
        for (std::size_t c = 0; c < state->instruments.size(); ++c)
            if (state->instruments[c] != nullptr && ! foreignChannel ((int) c))
                state->instruments[c]->transportReset();
    }
    wasPlaying = playing;
    // tempo automation: the tempo holds for the whole block (evaluated at its start)
    ownBpm = state->song.bpm;

    // and FL's pattern Tempo events (issue #86, core/TempoMap.h): in song mode with the tempo clips, in pattern mode the
    // current pattern's. A tempo the user is dragging over its automation holds (as before).
    // (round 190gl: evaluated under an override too: the Link session proposes the song's own tempo to its peers)
    // (round 190hq: a launched scene's tempo holds the tempo map, as Live's scene tempo overrides its tempo automation)
    if (state->tempoMap && ! state->song.sceneTempoHold && ! (state->tempoAutomation >= 0 && isSlotOverridden (state->tempoAutomation)))
    {
        const auto songMode = state->song.mode == PlayMode::song;
        const auto length = (double) std::max<std::int64_t> (1, state->schedule.songLength);
        ownBpm = tempomap::tempoAt (state->song, songMode ? linearSongTick (liveInput, startTick, length) : std::max (0.0, startTick));
    }
    bpm = (tempoOverride > 0.0 ? tempoOverride : ownBpm) * playSpeed; // (round 190mp: FL's Half speed)
    // issue #187 (round 190ik): warped samples follow the playing tempo (automation, Tempo events, a dragged or Link
    // tempo); at the song's own fixed tempo the ratio is exactly 1 and nothing changes
    if (tempofollow::enabled())
    {
        const auto moves = state->tempoMap || tempoOverride > 0.0 || playSpeed != 1.0;
        const auto ratio = bpm / std::max (1.0, state->song.bpm);
        for (auto& instrument : state->instruments)
            if (instrument != nullptr)
                instrument->setTempoRatio (ratio, moves);
    }

    const auto tps = ticksPerSample (bpm, sampleRate);
    expectedTick = startTick + numSamples * tps;

    for (auto* queue : { live, moreLive })
    {
        if (queue == nullptr)
            continue;

        NoteEvent e;

        while (queue->pop (e))
        {
            if (e.type == NoteEvent::Type::choke && e.channel == stopSoundChannel) // FL's Stop sound (round 170h2)
            {
                for (std::size_t t = 0; t < state->effects.size(); ++t)
                    if (! foreignTrack ((int) t))
                        for (auto& effect : state->effects[t])
                            if (effect != nullptr)
                                effect->transportReset(); // native effects: reset(); plugins: their wrapper's reset option
                continue;
            }
            if (e.type == NoteEvent::Type::choke && e.channel < 0) // panic: every channel stops sounding
            {
                for (std::size_t c = 0; c < state->instruments.size(); ++c)
                    if (state->instruments[c] != nullptr && ! foreignChannel ((int) c))
                        state->instruments[c]->panic();
                heldNotes.fill ({});
                for (auto& order : voiceOrders)
                    order.clear();
                clearPolyLayers();
                continue;
            }

            if (e.channel < 0 || e.channel >= (int) state->instruments.size() || foreignChannel (e.channel))
                continue; // (render-ahead routes a channel's live notes to the renderer that renders it)

            if (e.type == NoteEvent::Type::control)
            {
                applyLiveControl (*state, e);
                continue;
            }
            // Freeze (round 190ay): a frozen channel plays its rendered audio as a clip. A key played on it (the typing
            // keyboard, a MIDI input, a preview) has no instrument to play, and must not stop or restart that audio
            if ((e.type == NoteEvent::Type::on || e.type == NoteEvent::Type::off) && (std::size_t) e.channel < state->song.channels.size()
                && state->song.channels[(std::size_t) e.channel].freeze.on())
                continue;

            auto& instrument = *state->instruments[(std::size_t) e.channel];
            // (round 190jl: a MIDI Out channel keeps a MIDI input's own notes from going back to that device)
            struct SourceScope
            {
                Instrument& instrument;
                bool set;
                ~SourceScope()
                {
                    if (set)
                        instrument.setNoteSource (0);
                }
            } sourceScope { instrument, e.source != 0 };
            if (e.source != 0)
                instrument.setNoteSource (e.source);

            // Round 190ey (live check 190ew PL-3): a Playlist audio clip previewed through its channel, so through the
            // channel's mixer insert as FL plays it: its key is a clip key (above the MIDI range, which nothing played live
            // uses) and it carries the clip's note as the Scheduler makes it (core audioClipNote)
            if (e.key >= audioClipKeyBase && (e.type == NoteEvent::Type::on || e.type == NoteEvent::Type::off))
            {
                if (e.type == NoteEvent::Type::off)
                {
                    instrument.noteOff (e.key);
                    continue;
                }
                const auto* c = (std::size_t) e.channel < state->song.channels.size() ? &state->song.channels[(std::size_t) e.channel] : nullptr;
                const auto cents = e.cents + (c != nullptr ? c->misc.fineTune : 0.0f) + instrument.wheelCents();
                instrument.setNextNoteExpression (e.pan, cents, false);
                instrument.setNextNoteModulation (e.modX, e.modY);
                if (e.clipPitch != 0.0f)
                    instrument.setNextClipPitch (e.clipPitch);
                if (e.clipStretch != 1.0f)
                    instrument.setNextClipStretch (e.clipStretch);
                if (e.fadeIn > 0.0f || e.fadeOut > 0.0f || e.fadeShape != 0)
                    instrument.setNextClipFades (e.clipElapsed, e.clipRemaining, e.fadeIn, e.fadeOut, e.fadeShape, e.fadeInTension, e.fadeOutTension);
                if (e.clipEnvelope >= 0) // (round 190gi)
                    instrument.setNextClipEnvelope (e.clipEnvelope, e.clipEnvTick);
                auto velocity = e.velocity;
                if (e.gain != 1.0f && ! instrument.setNextNoteGain (e.gain))
                    velocity *= e.gain;
                instrument.noteOnAt (e.key, velocity, e.startSeconds);
                continue;
            }

            if (e.type == NoteEvent::Type::voiceProp) // issue #108: a played MPE note's bend / timbre / pressure
            {
                auto base = instrument.wheelCents();
                if ((std::size_t) e.channel < state->song.channels.size())
                {
                    const auto& c = state->song.channels[(std::size_t) e.channel];
                    base += c.misc.fineTune + (c.misc.mainPitch ? state->song.masterPitch : 0.0f);
                }
                instrument.noteExpression (e.key, e.prop, e.velocity, base);
                continue;
            }

            auto& held = heldNotes[(std::size_t) std::min (e.channel, maxChannels - 1)];
            const auto bit = std::uint64_t { 1 } << (e.key & 63);
            const auto inRange = e.key >= 0 && e.key < 128;
            // played on a MIDI Out channel: the plugins on its port get the note with its MIDI channel
            if (state->hasMidiRoutes && (std::size_t) e.channel < state->midiRoutesOut.size()
                && ((std::size_t) e.channel >= state->vfxPort.size() || state->vfxPort[(std::size_t) e.channel] < 0))
                for (const auto& route : state->midiRoutesOut[(std::size_t) e.channel])
                    if (! route.portOnly && route.other < (int) state->instruments.size() && state->instruments[(std::size_t) route.other] != nullptr)
                    {
                        auto& target = *state->instruments[(std::size_t) route.other];
                        const auto midiChannel = state->mapNoteColour[(std::size_t) e.channel] != 0 ? (int) (e.colour & 15) : route.midiChannel;
                        if (e.type == NoteEvent::Type::on)
                            target.noteOnPort (route.port, e.key, e.velocity, midiChannel);
                        else if (e.type == NoteEvent::Type::off)
                            target.noteOffPort (route.port, e.key, midiChannel);
                    }
            // and the mixer-slot Patchers on its port (round 162)
            // (notes that go through the channel's note path reach them in processChunk)
            if (state->hasEffectMidiRoutes && ! routesLiveNotes (*state, e.channel))
                deliverToEffects (*state, e);
            // the channel's arpeggiator / echo delay / tracking play it (in processChunk's first chunk)
            if ((e.type == NoteEvent::Type::on || e.type == NoteEvent::Type::off) && routesLiveNotes (*state, e.channel))
            {
                if (liveRoutedCount < (int) liveRouted.size())
                {
                    e.sampleOffset = 0;
                    liveRouted[(std::size_t) liveRoutedCount++] = e;
                }
                continue;
            }
            auto& order = voiceOrders[(std::size_t) std::min (e.channel, maxChannels - 1)];
            if (e.type == NoteEvent::Type::on && (std::size_t) e.channel < state->song.channels.size()
                && ! state->song.channels[(std::size_t) e.channel].inKeyRegion (e.key))
                continue; // outside the channel's key region (round 164)
            const auto& def = state->song.channels;
            auto& poly = polyLayers[(std::size_t) std::min (e.channel, maxChannels - 1)];
            if (e.type == NoteEvent::Type::on)
            {
                if (inRange)
                {
                    held[(std::size_t) (e.key >> 6)] |= bit;
                    struckNotes[(std::size_t) std::min (e.channel, maxChannels - 1)][(std::size_t) (e.key >> 6)] |= bit;
                    if (state->modStateful) // (round 190ox)
                        noteStruck (e.channel, e.key, e.velocity);
                }
                if ((std::size_t) e.channel >= def.size())
                {
                    instrument.noteOn (e.key, e.velocity);
                    continue;
                }
                const auto& c = def[(std::size_t) e.channel];
                // a played note is tuned like the song's notes: Misc › Fine tune and the main pitch (round 162, issue
                // #9: the fine tune did nothing to the keys played live)
                // (issue #108: an MPE note starts at its own bend, with its timbre and pressure as Mod X / Mod Y)
                const auto cents = c.misc.fineTune + (c.misc.mainPitch ? state->song.masterPitch : 0.0f) + instrument.wheelCents()
                                   + (e.expression ? e.cents : 0.0f);
                // FL's channel Mono / Porta (round 168l), as for the song's notes
                const auto mode = polyMode (c, instrument);
                const auto glideSamples = (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, tps));
                const auto on = poly.noteOn (mode, e.key, cents, false, e.colour);
                if (on.release >= 0)
                {
                    order.noteOff (on.release);
                    instrument.noteOff (on.release);
                }
                if (! on.start)
                {
                    if (on.slide)
                        instrument.slideTo (e.key, cents, glideSamples);
                    continue;
                }
                if (const auto limit = voiceLimitFor (c.maxPolyphony, channelMono (c)); limit > 0)
                    if (const auto victim = order.noteOn (e.key, limit); victim >= 0)
                        instrument.noteOff (victim);
                if (const auto from = on.glide ? on.glideFrom : 0.0f; cents != 0.0f || from != 0.0f)
                    instrument.setNextNoteExpression (0.0f, cents + from, false);
                HostSlide::pendingGlide() = { on.glide ? on.glideFrom : 0.0f, on.glide ? glideSamples : 0.0f, false };
                if (e.expression)
                {
                    instrument.setNextNoteModulation (e.modX, e.modY);
                    instrument.setNextNoteFinePitch (e.cents);
                }
                if (auto* block = e.channel >= 0 ? propsOf ((std::size_t) e.channel) : nullptr; e.key >= 0 && e.key < 128 && block != nullptr) // (issue #236)
                    block->notes[(std::size_t) e.key] = { e.velocity, 0.0f, e.expression ? e.modX : 0.5f, e.expression ? e.modY : 0.5f };
                instrument.noteOn (e.key, e.velocity);
                HostSlide::pendingGlide() = {};
            }
            else
            {
                if (inRange)
                    held[(std::size_t) (e.key >> 6)] &= ~bit;
                if ((std::size_t) e.channel >= def.size())
                {
                    order.noteOff (e.key);
                    instrument.noteOff (e.key);
                    continue;
                }
                const auto mode = polyMode (def[(std::size_t) e.channel], instrument);
                const auto off = poly.noteOff (mode, e.key, e.colour);
                if (off.slide)
                    instrument.slideTo (off.key, off.cents, (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, tps)));
                if (off.release >= 0)
                {
                    order.noteOff (off.release);
                    instrument.noteOff (off.release);
                }
            }
        }
    }

    // Blocks larger than the mixer buffers are done in pieces. With automation, small
    // pieces so values move smoothly.
    const auto chunk = std::min (blockCapacity, state->hasAutomation ? 128 : chunkCapacity);
    // (round 190gh, B7: while playing with automation, the pieces are those an export makes — blocks of 512 from the
    // start, cut in pieces of 128 and where a pattern event falls — whatever the audio device's block size; a piece the
    // block's end cuts goes on in the next block with the automation values it began with)
    constexpr int piece = 128, exportBlock = 512;
    const auto grid = playing && tps > 0.0 && state->hasAutomation && chunk >= std::min (piece, blockCapacity);
    if (! playing || tps <= 0.0)
        gridNextTick = -1.0;
    else if (gridNextTick < 0.0 || std::abs (startTick - gridNextTick) > 1.0e-3)
        gridSample = pieceStart = 0; // started, or jumped (a seek, the loop)

    // round 190kz (sched): the same pieces with one task-graph round for the whole block, where that is exact
    const auto batch = batchable (*state, numSamples);
    const auto batched = batch && planPieces (*state, playing, startTick, numSamples, tps, chunk, grid);
    // (round 190lv: a refused plan is counted, and the block then plays piece by piece below — before, it was not rendered
    // at all: a block of more than maxPieces pieces came out as whatever the output buffers held)
    if (batch && ! batched && pieceCount == -1) // (more than maxPieces: pattern events close together)
        ++batchRefused[(std::size_t) BatchRefusal::manyPieces];
    else if (batch && ! batched && pieceCount == -2) // (round 190lv: a later piece goes on without its live values)
        ++batchRefused[(std::size_t) BatchRefusal::liveControllers];
    if (batched)
        processBatched (*state, playing, numSamples, left, right, meters);
    else
    for (int done = 0; done < numSamples;)
    {
        auto n = std::min (chunk, numSamples - done);
        auto continuing = false;
        const auto pos = gridSample + done;
        if (grid)
        {
            const auto blockEnd = (pos / exportBlock + 1) * exportBlock;
            auto pieceEnd = std::min<std::int64_t> (pieceStart + piece, blockEnd);
            if (pos >= pieceEnd)
            {
                pieceStart = pos;
                pieceEnd = std::min<std::int64_t> (pos + piece, blockEnd);
            }
            continuing = pos > pieceStart;
            n = (int) std::min<std::int64_t> (std::min (blockCapacity, numSamples - done), pieceEnd - pos);
        }

        // pattern events take effect at their own sample, as in FL (REA_rodada85 §3): a chunk ends where one falls
        auto atEvent = false;
        if (playing && ! state->eventLanes.empty() && tps > 0.0)
        {
            const auto ahead = ticksToNextEvent (*state, startTick + done * tps);
            if (ahead < (double) n * tps)
            {
                n = std::max (1, (int) std::ceil (ahead / tps - 1.0e-9));
                atEvent = true;
            }
        }
        liveInputOffset = done;
        if (! state->hardwareMidiChannels.empty()) // (round 190jl)
            hardwareMidiBeforeChunk (*state);
        timedGridPos = grid ? pos : -1; // (round 190jg: sample-accurate automation keeps to the export's grid)
        const auto ownNotes = ownAnyOn || ownWantedAny.load (std::memory_order_acquire); // (round 190ks)
        if (ownNotes)
            ownNotesBeforeChunk (*state);
        processChunk (*state, playing, startTick + done * tps, n, left + done, right + done, meters, continuing);
        if (! state->hardwareMidiChannels.empty())
            hardwareMidiAfterChunk (*state, n);
        if (ownNotes)
            ownNotesAfterChunk (*state, n);
        resumeAudio = false;
        chaseNotes = false;
        done += n;
        if (grid && atEvent)
            pieceStart = pos + n; // (the next piece starts at the event, as in an export)
    }
    if (playing && tps > 0.0)
    {
        gridSample += numSamples;
        gridNextTick = startTick + numSamples * tps;
    }
    publishSoundingKeys ((int) state->song.channels.size());

    if (timing)
    {
        times->wallNs.fetch_add (ProcessTimes::now() - wallStart, std::memory_order_relaxed);
        if (countsAudio)
            times->audioNs.fetch_add ((std::uint64_t) ((double) numSamples / sampleRate * 1.0e9), std::memory_order_relaxed);
    }
}

// ---- Hardware MIDI outputs (round 190jl) ----------------------------------------------------------------------------

void RackRenderer::setHardwareMidiPorts (const std::array<std::uint64_t, 4>& ports) noexcept
{
    for (std::size_t w = 0; w < ports.size(); ++w)
        hwPorts[w].store (ports[w], std::memory_order_relaxed);
    hwPortsAny.store ((ports[0] | ports[1] | ports[2] | ports[3]) != 0, std::memory_order_release);
}

int RackRenderer::takeHardwareMidi (HardwareMidiEvent* out, int max) noexcept
{
    const auto count = std::min (max, hwMidiCount);
    std::copy (hwMidi.begin(), hwMidi.begin() + count, out);
    hwMidiCount = 0;
    return count;
}

void RackRenderer::addHardwareMidi (int sample, int port, const std::uint8_t* bytes, int size, int source) noexcept
{
    if (hwMidiCount >= hardwareMidiCapacity || port < 0 || port > 255 || size < 1 || size > 3)
        return; // (nobody took the last blocks: dropped, never allocated)
    auto& m = hwMidi[(std::size_t) hwMidiCount++];
    m.sample = sample;
    m.port = (std::uint8_t) port;
    m.size = (std::uint8_t) size;
    m.source = (std::uint8_t) std::clamp (source, 0, 255);
    m.bytes = { bytes[0], size > 1 ? bytes[1] : std::uint8_t { 0 }, size > 2 ? bytes[2] : std::uint8_t { 0 } };
}

// ---- The plugins' own notes, for a take (GitHub #198, round 190ks) ---------------------------------------------------

void RackRenderer::setOwnNoteChannels (const std::array<std::uint64_t, ownNoteWords>& channels) noexcept
{
    auto any = false;
    for (std::size_t w = 0; w < channels.size(); ++w)
    {
        ownWanted[w].store (channels[w], std::memory_order_relaxed);
        any = any || channels[w] != 0;
    }
    ownWantedAny.store (any, std::memory_order_release);
}

int RackRenderer::takeOwnNotes (OwnNoteEvent* out, int max) noexcept
{
    const auto count = std::min (max, ownEventCount);
    std::copy (ownEvents.begin(), ownEvents.begin() + count, out);
    ownEventCount = 0;
    return count;
}

// Before a chunk: each marked channel's capture is on (asked every chunk: an instrument made again starts off); a channel
// no longer marked is switched off once
void RackRenderer::ownNotesBeforeChunk (const RackState& state) noexcept
{
    const auto count = std::min<std::size_t> (state.instruments.size(), (std::size_t) maxChannels);
    auto anyOn = false;
    for (std::size_t w = 0; w < (std::size_t) ownNoteWords; ++w)
    {
        const auto wanted = ownWanted[w].load (std::memory_order_relaxed);
        auto bits = wanted | ownOn[w];
        std::uint64_t on = 0;
        while (bits != 0)
        {
            const auto b = (std::size_t) std::countr_zero (bits);
            bits &= bits - 1;
            const auto c = w * 64 + b;
            if (c >= count || state.instruments[c] == nullptr)
                continue;
            const auto want = (wanted >> b & 1u) != 0 && ! foreignChannel ((int) c);
            if (want || (ownOn[w] >> b & 1u) != 0)
                state.instruments[c]->setRecordOwnNotes (want);
            if (want)
                on |= std::uint64_t { 1 } << b;
        }
        ownOn[w] = on;
        anyOn = anyOn || on != 0;
    }
    ownAnyOn = anyOn;
}

// After a chunk: the notes each capture kept, at their sample in the process() block, in time order
void RackRenderer::ownNotesAfterChunk (const RackState& state, int n) noexcept
{
    const auto first = ownEventCount;
    const auto count = std::min<std::size_t> (state.instruments.size(), (std::size_t) maxChannels);
    for (std::size_t w = 0; w < (std::size_t) ownNoteWords; ++w)
        for (auto bits = ownOn[w]; bits != 0; bits &= bits - 1)
        {
            const auto c = w * 64 + (std::size_t) std::countr_zero (bits);
            if (c >= count || state.instruments[c] == nullptr)
                continue;
            const auto taken = state.instruments[c]->takeOwnNotes (ownScratch.data(), (int) ownScratch.size());
            for (int k = 0; k < taken && ownEventCount < ownNoteCapacity; ++k)
            {
                const auto& o = ownScratch[(std::size_t) k];
                auto& e = ownEvents[(std::size_t) ownEventCount++];
                e.sample = liveInputOffset + std::clamp (o.sample, 0, std::max (0, n - 1));
                e.channel = (std::int32_t) c;
                e.key = o.key;
                e.midiChannel = o.midiChannel;
                e.velocity = o.velocity;
            }
        }
    for (int i = first + 1; i < ownEventCount; ++i) // (stable insertion sort: a few notes)
    {
        const auto m = ownEvents[(std::size_t) i];
        auto j = i;
        while (j > first && ownEvents[(std::size_t) j - 1].sample > m.sample)
        {
            ownEvents[(std::size_t) j] = ownEvents[(std::size_t) j - 1];
            --j;
        }
        ownEvents[(std::size_t) j] = m;
    }
}

// Before a chunk: the capture of each MIDI Out channel / plugin whose port an output device has is on (bit 0); a
// capture switched off this chunk is still taken once after it (bit 1: the notes it ended)
void RackRenderer::hardwareMidiBeforeChunk (const RackState& state) noexcept
{
    const auto any = hwPortsAny.load (std::memory_order_acquire);
    for (const auto c : state.hardwareMidiChannels)
    {
        if (c < 0 || c >= maxChannels || (std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr)
            continue;
        auto& flags = hwMidiOn[(std::size_t) c];
        const auto on = any && ! foreignChannel (c) && hardwareMidiPort (state.hardwareMidiPort[(std::size_t) c]);
        if (on || (flags & 1) != 0)
            state.instruments[(std::size_t) c]->setHardwareMidi (on);
        flags = (char) ((on ? 1 : 0) | (on || (flags & 1) != 0 ? 2 : 0));
    }
}

// After a chunk: what each capture kept, at its sample in the chunk, in time order
void RackRenderer::hardwareMidiAfterChunk (const RackState& state, int n) noexcept
{
    const auto first = hwMidiCount;
    for (const auto c : state.hardwareMidiChannels)
    {
        if (c < 0 || c >= maxChannels || (hwMidiOn[(std::size_t) c] & 2) == 0 || (std::size_t) c >= state.instruments.size()
            || state.instruments[(std::size_t) c] == nullptr)
            continue;
        const auto count = state.instruments[(std::size_t) c]->takeHardwareMidi (hwMidiScratch.data(), (int) hwMidiScratch.size());
        for (int k = 0; k < count; ++k)
        {
            const auto& h = hwMidiScratch[(std::size_t) k];
            addHardwareMidi (liveInputOffset + std::clamp (h.sample, 0, std::max (0, n - 1)), state.hardwareMidiPort[(std::size_t) c], h.bytes.data(), h.size,
                             h.source);
        }
    }
    // (stable: each channel's own order stays; a few events, insertion sort)
    for (int i = first + 1; i < hwMidiCount; ++i)
    {
        const auto m = hwMidi[(std::size_t) i];
        auto j = i;
        while (j > first && hwMidi[(std::size_t) j - 1].sample > m.sample)
        {
            hwMidi[(std::size_t) j] = hwMidi[(std::size_t) j - 1];
            --j;
        }
        hwMidi[(std::size_t) j] = m;
    }
}

void RackRenderer::publishSoundingKeys (int channels) noexcept
{
    channels = std::clamp (channels, 0, maxChannels);
    const auto top = std::max (channels, publishedTop);
    publishedTop = 0;
    const auto bits = [] (const NoteMask& m) { return std::popcount (m[0]) + std::popcount (m[1]); };
    int voices = 0; // (round 190oa, DF-29)
    for (int i = 0; i < top; ++i)
    {
        auto& was = publishedNotes[(std::size_t) i];
        NoteMask now {};
        if (i < channels)
        {
            const auto& held = heldNotes[(std::size_t) i];
            auto& struck = struckNotes[(std::size_t) i];
            now = { held[0] | struck[0], held[1] | struck[1] };
            if ((struck[0] | struck[1]) != 0) // round 190ih: a note started in this block (the rack's activity light)
                strikes[(std::size_t) i].store (strikes[(std::size_t) i].load (std::memory_order_relaxed) + 1u, std::memory_order_relaxed);
            struck = {};
        }
        auto& releasing = releasingVoices[(std::size_t) i];
        if (now != was) // the keys let go of this block start releasing
            releasing = (std::uint16_t) std::min (999, (int) releasing + bits ({ was[0] & ~now[0], was[1] & ~now[1] }));
        if (i >= channels)
            releasing = 0;
        voices += bits (now) + releasing;
        if (now[0] != 0 || now[1] != 0 || releasing != 0)
            publishedTop = i + 1;
        if (now == was)
            continue;
        was = now;
        sounding[(std::size_t) i][0].store (now[0], std::memory_order_relaxed);
        sounding[(std::size_t) i][1].store (now[1], std::memory_order_relaxed);
    }
    voicesNow.store (voices, std::memory_order_relaxed);
}

double RackRenderer::ticksToNextEvent (const RackState& state, double tick) noexcept
{
    const auto& song = state.song;
    auto best = std::numeric_limits<double>::infinity();
    // first event tick of `events` strictly after `t` (events sorted by tick)
    auto after = [] (const PatternEvents& events, double t) {
        auto it = std::upper_bound (events.events.begin(), events.events.end(), t,
                                    [] (double x, const PatternEvent& e) { return x < (double) e.tick; });
        return it == events.events.end() ? std::numeric_limits<double>::infinity() : (double) it->tick;
    };

    if (song.mode != PlayMode::song)
    {
        const auto current = song.currentPattern;
        if (current < 0 || current >= (int) song.patterns.size())
            return best;
        const auto& pattern = song.patterns[(std::size_t) current];
        const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.meter()));
        const auto t = std::fmod (std::max (0.0, tick), length);
        for (const auto& lane : state.eventLanes)
            if (lane.pattern == current)
            {
                const auto& events = pattern.events[(std::size_t) lane.lane];
                best = std::min (best, after (events, t) - t);
                if (! events.events.empty()) // the loop restarts at the first event's value
                    best = std::min (best, length - t);
            }
        return best;
    }

    const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
    const auto t = std::fmod (std::max (0.0, tick), length);

    for (const auto& lane : state.eventLanes)
    {
        const auto& events = song.patterns[(std::size_t) lane.pattern].events[(std::size_t) lane.lane];
        for (auto i : state.patternClips[(std::size_t) lane.pattern])
        {
            const auto& c = song.playlist[(std::size_t) i];
            if ((double) c.end() <= t)
                continue;
            if ((double) c.start > t)
            {
                best = std::min (best, (double) c.start - t);
                break;
            }
            const auto period = (std::size_t) lane.pattern < state.schedule.patternLength.size() ? state.schedule.patternLength[(std::size_t) lane.pattern] : 0;
            const auto local = patternTickInClip (c, t, period);
            auto ahead = after (events, local) - local;
            if (stretchedClipsLoop() && period > 0 && c.offset + c.length > period) // the next pass restarts at the first event's value
                ahead = std::min (ahead, (double) period - local);
            best = std::min (best, std::min (t + ahead, (double) c.end()) - t);
        }
    }
    return best;
}

void RackRenderer::applyLiveControl (const RackState& state, const NoteEvent& e) noexcept
{
    // a controller's pitch wheel, mod wheel (CC 1), other CCs and channel pressure played into a channel (round 162,
    // issue #9): hosted plugins (and the SoundFont player) get the MIDI message; LYNX's own instruments bend by the
    // channel's pitch range (FL's Misc › Pitch range, default ±2 semitones) and take the CCs they know
    // A Layer makes no sound: what is played into it goes to its children, as its notes do (issue #141: the sustain
    // pedal, the wheels and the other controllers stopped at the Layer)
    if (const auto& channels = state.song.channels; (std::size_t) e.channel < channels.size() && channels[(std::size_t) e.channel].instrument == InstrumentKind::layer)
    {
        for (const auto c : channels[(std::size_t) e.channel].layer.children)
            if (c >= 0 && c < (int) channels.size() && c < (int) state.instruments.size() && c != e.channel
                && channels[(std::size_t) c].instrument != InstrumentKind::layer)
            {
                auto child = e;
                child.channel = c;
                applyLiveControl (state, child);
            }
        return;
    }
    auto* instrument = state.instruments[(std::size_t) e.channel].get();
    if (instrument == nullptr)
        return;

    std::uint8_t m[3] {};
    auto size = 3;
    const auto value = std::isfinite (e.velocity) ? e.velocity : 0.0f;
    if (e.key == NoteEvent::controlPitchBend)
    {
        const auto v = (int) std::clamp (std::lround ((double) (value + 1.0f) * 8192.0), 0L, 16383L);
        m[0] = 0xE0, m[1] = (std::uint8_t) (v & 127), m[2] = (std::uint8_t) (v >> 7);
    }
    else if (e.key == NoteEvent::controlPressure)
    {
        m[0] = 0xD0, m[1] = (std::uint8_t) std::clamp (std::lround (value * 127.0f), 0L, 127L);
        size = 2;
    }
    else if (e.key == NoteEvent::controlProgram) // (round 165c: a VFX Sequencer selects its sequence, plugins their preset)
    {
        m[0] = 0xC0, m[1] = (std::uint8_t) std::clamp (std::lround (value * 127.0f), 0L, 127L);
        size = 2;
    }
    else if (e.key >= 0 && e.key < 128)
        m[0] = 0xB0, m[1] = (std::uint8_t) e.key, m[2] = (std::uint8_t) std::clamp (std::lround (value * 127.0f), 0L, 127L);
    else
        return;

    if (e.source != 0) // (round 190jl: a MIDI Out channel does not send a device's own knobs back to it)
        instrument->setNoteSource (e.source);
    instrument->midiMessage (m, size);
    if (e.source != 0)
        instrument->setNoteSource (0);
    if (e.key == NoteEvent::controlPitchBend)
    {
        const auto range = (std::size_t) e.channel < state.song.channels.size() ? state.song.channels[(std::size_t) e.channel].misc.pitchRange : 2;
        instrument->pitchBend (std::clamp (value, -1.0f, 1.0f) * (float) std::max (0, range));
        // (round 173d: an FL-native engine follows the wheel through the channel's pitch; tell it now, so that a note
        // started before the next chunk anchors the same pitch it is tuned to)
        if (const auto& ch = state.song.channels; (std::size_t) e.channel < ch.size())
        {
            const auto& c = ch[(std::size_t) e.channel];
            instrument->setChannelPitchNow (c.sampler.pitch * 100.0f, c.misc.fineTune + (c.misc.mainPitch ? state.song.masterPitch : 0.0f) + instrument->wheelCents());
        }
    }
    else if (e.key < 128)
        instrument->controlChange (e.key, std::clamp (value, 0.0f, 1.0f));

    // a MIDI Out channel: the plugins on its port get it on its MIDI channel
    if (state.hasMidiRoutes && (std::size_t) e.channel < state.midiTargets.size())
        for (const auto t : state.midiTargets[(std::size_t) e.channel])
            if (t >= 0 && t < (int) state.instruments.size() && state.instruments[(std::size_t) t] != nullptr)
            {
                std::uint8_t routed[3] { (std::uint8_t) ((m[0] & 0xF0) | (state.midiOutChannel[(std::size_t) e.channel] & 15)), m[1], m[2] };
                state.instruments[(std::size_t) t]->midiMessage (routed, size);
            }
}

void RackRenderer::expandLayers (const RackState& state) noexcept
{
    const auto& channels = state.song.channels;
    const auto count = events.size();
    auto added = false;
    for (int i = 0; i < count; ++i)
    {
        const auto e = events[i];
        if (e.channel < 0 || e.channel >= (int) channels.size() || e.channel >= maxChannels)
            continue;
        const auto& l = channels[(std::size_t) e.channel];
        if (l.instrument != InstrumentKind::layer || l.layer.children.empty())
            continue;
        const auto key = std::clamp (e.key, 0, 127);
        auto* picked = layerPick.empty() ? nullptr : &layerPick[(std::size_t) e.channel * 128 + (std::size_t) key];
        auto child = e;
        child.pan = std::clamp (e.pan + l.pan, -1.0f, 1.0f);
        child.cents = e.cents + layer::pitchCents (l);
        const auto one = l.layer.random || l.layer.sequential;

        if (e.type == NoteEvent::Type::on)
        {
            if (l.muted)
                continue;
            std::array<layer::Pick, 64> picks;
            const auto n = layer::pick (l, channels, key, layerRandom, layerTurn[(std::size_t) e.channel], picks.data(), (int) picks.size());
            if (one && picked != nullptr)
                *picked = (std::int16_t) (n > 0 ? picks[0].child : -1);
            const auto level = layer::levelGain (l);
            for (int k = 0; k < n; ++k)
            {
                child.channel = picks[(std::size_t) k].child;
                child.gain = e.gain * level * picks[(std::size_t) k].gain;
                events.add (child);
                added = true;
            }
        }
        else if (e.type == NoteEvent::Type::off && one)
        {
            if (picked != nullptr && *picked >= 0)
            {
                child.channel = *picked;
                *picked = -1;
                events.add (child);
                added = true;
            }
        }
        else if (e.type == NoteEvent::Type::off || e.type == NoteEvent::Type::slide || e.type == NoteEvent::Type::choke)
            for (const auto c : l.layer.children)
                if (c >= 0 && c < (int) channels.size() && c != e.channel && channels[(std::size_t) c].instrument != InstrumentKind::layer)
                {
                    child.channel = c;
                    events.add (child);
                    added = true;
                }
    }
    if (added)
        events.sortByOffset();
}

bool RackRenderer::routesLiveNotes (const RackState& state, int channel) const noexcept
{
    const auto c = (std::size_t) channel;
    if (channel < 0 || c >= state.song.channels.size())
        return false;
    if (state.song.channels[c].instrument == InstrumentKind::layer) // (its children play the note: round 164)
        return true;
    const auto& misc = state.song.channels[c].misc;
    return (c < state.arpeggiators.size() && state.arpeggiators[c] != nullptr) || (c < state.echoes.size() && state.echoes[c] != nullptr)
           || misc.key.pan != 0.0f || misc.velocity.pan != 0.0f
           || (misc.addToKey && misc.rootNote != 60 && state.song.channels[c].instrument != InstrumentKind::sampler)
           || (c < state.vfxPort.size() && state.vfxPort[c] >= 0); // (a VFX channel's processor takes them in the chunk)
}

void RackRenderer::runVfxChannels (const RackState& state, bool playing, int numSamples) noexcept
{
    vfx::Transport transport;
    transport.sampleRate = sampleRate;
    transport.bpm = chunkTps * sampleRate * 60.0 / (double) ppq; // (the tempo at this chunk)
    transport.ticks = chunkStartTick; // (stopped: the live clock, as the arpeggiator)
    transport.playing = playing;
    for (const auto c : state.vfxChannels)
    {
        if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr || foreignChannel (c))
            continue;
        auto* processor = state.instruments[(std::size_t) c]->noteProcessor();
        if (processor == nullptr)
            continue;
        vfxIn->clear();
        vfxOut->clear();
        const auto count = events.size();
        for (int k = 0; k < count; ++k)
        {
            auto& e = events.at (k);
            if (e.channel != c || e.port != -1)
                continue;
            if (e.type == NoteEvent::Type::on || e.type == NoteEvent::Type::off)
            {
                vfx::Event in;
                in.type = e.type == NoteEvent::Type::on ? vfx::Event::Type::on : vfx::Event::Type::off;
                in.offset = e.sampleOffset;
                in.voice.key = e.key;
                in.voice.velocity = e.velocity;
                in.voice.pan = e.pan;
                in.voice.cents = e.cents;
                in.voice.modX = e.modX;
                in.voice.modY = e.modY;
                in.voice.release = e.release;
                in.voice.colour = e.colour & 15;
                in.voice.porta = e.porta;
                in.id = vfx::inputId (e.key, in.voice.colour);
                vfxIn->add (in);
            }
            e.port = -2; // (taken by the processor)
        }
        processor->process (vfxIn->size() > 0 ? &(*vfxIn)[0] : nullptr, vfxIn->size(), numSamples, transport, *vfxOut);
        const auto port = state.vfxPort[(std::size_t) c];
        for (int k = 0; k < vfxOut->size(); ++k)
        {
            const auto& o = (*vfxOut)[k];
            if (o.type == vfx::Event::Type::program) // (a VFX Sequencer passing a program change on: the plugins on its port)
            {
                if ((std::size_t) c < state.midiTargets.size())
                    for (const auto t : state.midiTargets[(std::size_t) c])
                        if (t >= 0 && t < (int) state.instruments.size() && state.instruments[(std::size_t) t] != nullptr)
                        {
                            const std::uint8_t m[2] { 0xC0, (std::uint8_t) std::clamp ((int) o.value, 0, 127) };
                            state.instruments[(std::size_t) t]->midiMessage (m, 2);
                        }
                continue;
            }
            if (port + o.output >= patchMidiPorts)
                continue;
            NoteEvent e;
            e.type = o.type == vfx::Event::Type::on ? NoteEvent::Type::on : o.type == vfx::Event::Type::off ? NoteEvent::Type::off : NoteEvent::Type::voiceProp;
            e.prop = (std::uint8_t) o.prop;
            if (e.type == NoteEvent::Type::voiceProp) // (round 165c: the voice's property over time, on its port too)
            {
                e.velocity = o.value;
                e.channel = c;
                e.sampleOffset = std::clamp (o.offset, 0, std::max (0, numSamples - 1));
                e.key = o.voice.key;
                e.colour = (std::int8_t) (o.voice.colour & 15);
                e.port = (std::int16_t) (port + o.output);
                events.add (e);
                continue;
            }
            e.channel = c;
            e.sampleOffset = std::clamp (o.offset, 0, std::max (0, numSamples - 1));
            e.key = o.voice.key;
            e.velocity = o.voice.velocity;
            e.pan = o.voice.pan;
            e.cents = o.voice.cents;
            e.modX = o.voice.modX;
            e.modY = o.voice.modY;
            e.release = o.voice.release;
            e.colour = (std::int8_t) (o.voice.colour & 15);
            e.porta = o.voice.porta;
            e.port = (std::int16_t) (port + o.output);
            events.add (e);
        }
    }
    events.sortByOffset();
}

namespace
{
// Round 190ge: a chunk with almost nothing to do runs on the audio thread alone (see processChunk). The previous chunk's
// wall time on this thread (one renderer per audio thread; only a heuristic: the samples are the same either way)
thread_local std::uint64_t lastChunkNs = ~std::uint64_t (0);
constexpr std::uint64_t lightChunkNs = 60000; // 60 µs (about 1 % of a 256-sample block at 44.1 kHz)
// A7 (round 190gq): below this much work a chunk runs on the calling thread alone (190ge measured the hand-offs of 3
// workers at 64 samples at ~110 µs of CPU per block in a light project), a mixer layer below layerSerialNs too, and the
// channel pass hands out clusters of about clusterWorkNs
constexpr std::uint64_t serialWorkNs = 150000, layerSerialNs = 30000, clusterWorkNs = 20000;
constexpr double parallelShare = 0.6; // (round 190gq, REA 190hc: a snapshot that guessed "serial" goes parallel past this)
// LYNX_A7=0: the round 190ge dispatch (A/B measurements with one build, as LYNX_TASKGRAPH)
bool adaptiveThreads() noexcept
{
    static const bool on = [] { const char* e = std::getenv ("LYNX_A7"); return e == nullptr || std::atoi (e) != 0; }();
    return on;
}
} // namespace

void RackRenderer::processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                                 float* left, float* right, MeterBank* meters, bool continuing) noexcept
{
    const auto chunkStartNs = workers != nullptr ? ProcessTimes::now() : 0;
    chunkPrelude (state, playing, startTick, numSamples, continuing);
    auto* const pool = chunkSchedule (state, numSamples, events.size());
    chunkUnits (state, playing, numSamples, meters);
    chunkMaster (state, numSamples, left, right, meters, pool, chunkStartNs);
}

// (round 190kz: processChunk's parts, in its order; processBatched runs them piece by piece around one round)
void RackRenderer::chunkPrelude (const RackState& state, bool playing, double startTick, int numSamples, bool continuing) noexcept
{
    chunkTick = startTick;
    chunkPlaying = playing;
    // FL: playback starting puts every control the user moved back under its automation (issue #125)
    if (playing && ! releaseWasPlaying && anyReleased.load (std::memory_order_relaxed) && ! keepReleasedOnPlay.load (std::memory_order_relaxed))
    {
        for (auto& w : released)
            w.store (0, std::memory_order_relaxed);
        anyReleased.store (false, std::memory_order_relaxed);
    }
    releaseWasPlaying = playing;
    updatePdcBypass (state, playing);
    const auto& song = state.song;
    const auto tracks = std::max<int> (1, (int) song.mixer.size());

    if (! state.modRoutes.empty()) // round 177b (before the automation applies the plugin-side parameters)
        updateModulators (state, playing ? startTick : liveTick, numSamples);
    // (a hair later: a chunk that starts on a pattern event's sample takes its value; round 190gh: a piece that goes on
    // from the previous block keeps the values it began with, as in an export)
    if (state.hasAutomation && ! continuing)
        updateAutomation (state, startTick + 1.0e-6);

    chunkTps = ticksPerSample (bpm, sampleRate);
    // (round 190gi: renderChannel hands the clip envelope table on while the song has one, and once after it is gone)
    clipEnvTableShown = clipEnvTableWas;
    clipEnvTableWas = ! song.clipEnvelopes.empty();
    chunkStartTick = playing ? startTick : liveTick; // stopped: live notes' arpeggios run on their own clock
    if (! playing)
        liveTick += numSamples * chunkTps;

    // hosted plugins read the transport from here (PluginHost's play head)
    {
        auto& timeline = hostTimeline();
        timeline.tick.store (hostPositionTick (song, startTick, state.schedule.songLength), std::memory_order_relaxed);
        const auto wrap = playing ? hostPositionWrap (song, state.schedule.songLength) : HostPositionWrap {}; // (issue #199)
        timeline.wrapAt.store (wrap.at, std::memory_order_relaxed);
        timeline.wrapTo.store (wrap.to, std::memory_order_relaxed);
        timeline.bpm.store (bpm, std::memory_order_relaxed);
        timeline.sampleRate.store (sampleRate, std::memory_order_relaxed);
        timeline.beatsPerBar.store (std::max (1, song.beatsPerBar), std::memory_order_relaxed);
        timeline.beatUnit.store (meterUnit (song.beatUnit), std::memory_order_relaxed);
        timeline.playing.store (playing, std::memory_order_relaxed);
        timeline.looping.store (song.hasLoop(), std::memory_order_relaxed);
        timeline.loopStart.store ((double) song.loopStart, std::memory_order_relaxed);
        timeline.loopEnd.store ((double) song.loopEnd, std::memory_order_relaxed);
        timeline.ticksPerSample.store (playing ? chunkTps : 0.0, std::memory_order_relaxed);
        timeline.chunk.fetch_add (1, std::memory_order_relaxed);
    }
    // round 190jg: the automation's changes inside this chunk for hosted CLAP / VST3 plugins (after the chunk count moved:
    // they belong to the blocks of this chunk)
    if (state.hasAutomation && playing)
        queueTimedPluginParams (state, startTick, numSamples, continuing);

    // every insert's buffer starts the chunk as zeros (round 190gp: marked, not stored: trackBuffer stores them when the
    // insert is first written or worked on; an insert nobody feeds, whose effects are smart-disabled, never is)
    trackSamples = numSamples;
    std::memset (trackUnwritten.data(), 1, (std::size_t) std::min (tracks, (int) trackUnwritten.size()));

    events.clear();

    const auto songPlaying = playing && song.mode == PlayMode::song;
    // The launchers: Performance mode's clips (round 165m) and the clip grid (round 181a). Without either, this is the
    // one test a block makes for them, the one Performance mode always made.
    if (songPlaying && (state.launchers || launchersRunning))
    {
        launchersRunning = true;
        const auto tps = ticksPerSample (bpm, sampleRate);
        const auto wrap = liveInput == nullptr || ! liveInput->linearSong.load (std::memory_order_relaxed);
        if (state.clipLauncher != nullptr)
        {
            if (runningLauncher == nullptr)
                state.clipLauncher->clock = startTick;
            runningLauncher = state.clipLauncher;
            launcher::playBlock (*state.clipLauncher, song, *song.clipGrid, state.gridIndex, startTick, numSamples, tps, events, resumeAudio,
                                 &state.schedule, wrap, chaseNotes, *scheduledNotes);
        }
        else
        {
            if (runningLauncher != nullptr)
            {
                runningLauncher->player.stopAll (runningLauncher);
                runningLauncher = nullptr;
            }
            scheduleSong (song, startTick, numSamples, tps, events, resumeAudio, &state.schedule, wrap, chaseNotes);
        }
        // Performance mode (round 165m): the launched clips, on the Player's steady clock (the transport's ticks from
        // where playback started, never wrapping with the song loop)
        if (! state.performanceZone.empty() || perfRunning)
        {
            if (! perfRunning)
                perfClock = startTick;
            perfRunning = true;
            perfPlayer.process (song, state.performanceZone, perfLive, perfClock, startTick, numSamples * tps, true);
            schedulePerformance (song, perfPlayer, perfClock, numSamples, tps, events, &state.schedule);
            perfClock += numSamples * tps;
        }
        else if (perfLive != nullptr)
        {
            perf::Command c;
            while (perfLive->commands.pop (c)) {} // nothing launches without a zone
        }
    }
    else
    {
        if (songPlaying)
            scheduleSong (song, startTick, numSamples, ticksPerSample (bpm, sampleRate), events, resumeAudio, &state.schedule,
                          liveInput == nullptr || ! liveInput->linearSong.load (std::memory_order_relaxed), chaseNotes);
        if (launchersRunning || (perfLive != nullptr && perfLive->anyPlaying.load (std::memory_order_relaxed)))
        {
            perfPlayer.process (song, state.performanceZone, perfLive, perfClock, startTick, 0.0, false);
            perfRunning = false;
            launchersRunning = false;
            if (runningLauncher != nullptr)
            {
                if (song.clipGrid)
                    runningLauncher->player.recordStop (*song.clipGrid, state.gridIndex, runningLauncher);
                runningLauncher->player.stopAll (runningLauncher, song.clipGrid && song.clipGrid->backOnStop);
                runningLauncher = nullptr;
            }
        }
        else if (perfLive != nullptr)
        {
            perf::Command c;
            while (perfLive->commands.pop (c)) {} // nothing launches while stopped or in Pattern mode
        }
    }

    if (! songPlaying && playing && ! song.patterns.empty())
    {
        const auto patternIndex = std::clamp (song.currentPattern, 0, (int) song.patterns.size() - 1);
        const auto& pattern = song.patterns[(std::size_t) patternIndex];
        schedulePatternLoop (pattern, pattern.effectiveLength (song.meter()), startTick, numSamples,
                             ticksPerSample (bpm, sampleRate), events, song.swing, &song.channels, chaseNotes,
                             liveInput != nullptr ? liveInput->patternOrigin.load (std::memory_order_relaxed) : -1.0, song.chanceSeed);
    }

    // each started note ends where it was going to, whatever was edited meanwhile (FL, round 169d)
    if (playing)
        scheduledNotes->process (events, numSamples, ticksPerSample (bpm, sampleRate));

    // FL's wrapper "Output port" (round 149): the notes a plugin sent out in the previous chunk play the plugins on
    // that port, at this chunk's start
    if (state.hasMidiOutputs)
    {
        std::array<Instrument::MidiOutNote, 128> notes;
        for (std::size_t c = 0; c < state.midiOutTargets.size() && c < state.instruments.size(); ++c)
        {
            if (state.midiOutTargets[c].empty() || foreignChannel ((int) c))
                continue;
            const auto count = state.instruments[c]->takeMidiOutput (notes.data(), (int) notes.size());
            for (int k = 0; k < count; ++k)
                for (const auto t : state.midiOutTargets[c])
                {
                    NoteEvent e;
                    e.type = notes[(std::size_t) k].velocity > 0.0f ? NoteEvent::Type::on : NoteEvent::Type::off;
                    e.channel = t;
                    e.key = notes[(std::size_t) k].key;
                    e.velocity = notes[(std::size_t) k].velocity;
                    e.sampleOffset = 0;
                    events.add (e);
                }
        }
        events.sortByOffset();
    }

    // generator Patchers' VFX nodes cabled to "To LYNX" MIDI ports (round 165c): what they played in the previous
    // chunk goes out on those ports at this chunk's start (as a VFX channel's outputs do)
    if (! state.portEventChannels.empty())
    {
        std::array<Instrument::PortEvent, 128> sent;
        auto added = false;
        for (const auto c : state.portEventChannels)
        {
            if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr || foreignChannel (c))
                continue;
            const auto count = state.instruments[(std::size_t) c]->takePortEvents (sent.data(), (int) sent.size());
            for (int k = 0; k < count; ++k)
            {
                const auto& p = sent[(std::size_t) k];
                NoteEvent e;
                e.type = p.prop >= 0 ? NoteEvent::Type::voiceProp : p.value > 0.0f ? NoteEvent::Type::on : NoteEvent::Type::off;
                e.channel = c;
                e.key = p.key;
                e.velocity = p.value;
                e.prop = (std::uint8_t) std::max (0, p.prop);
                e.pan = p.pan;
                e.cents = p.cents;
                e.modX = p.modX;
                e.modY = p.modY;
                e.release = p.release;
                e.colour = (std::int8_t) (p.channel & 15);
                e.port = (std::int16_t) p.port;
                // a standalone MIDI Effect Rack (round 183c): its children play the notes, as a Layer's do
                if (const auto* rk = (std::size_t) c < state.song.channels.size() ? rack::rackOf (state.song.channels[(std::size_t) c].patcher) : nullptr;
                    rk != nullptr && rk->kind == RackDoc::Kind::midiEffect)
                {
                    for (const auto child : rk->children)
                        if (const auto* childRack = child >= 0 && child < (int) state.song.channels.size() ? rack::rackOf (state.song.channels[(std::size_t) child].patcher) : nullptr;
                            child >= 0 && child < (int) state.song.channels.size() && child != c
                            && (childRack == nullptr || childRack->kind != RackDoc::Kind::midiEffect)) // (no MIDI rack to MIDI rack loops)
                        {
                            auto toChild = e;
                            toChild.channel = child;
                            toChild.port = -1;
                            events.add (toChild);
                            added = true;
                        }
                    continue;
                }
                events.add (e);
                added = true;
            }
        }
        if (added)
            events.sortByOffset();
    }

    // Dashboards (round 165, FL's Dashboard): the MIDI their controls and patch send reaches the plugins on their port
    for (const auto c : state.dashboardChannels)
    {
        if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr || foreignChannel (c))
            continue;
        std::array<std::uint32_t, midiout::maxResetMessages + 8> sent; // (a MIDI Out's reset: every active control at once)
        const auto count = state.instruments[(std::size_t) c]->takeMidiMessages (sent.data(), (int) sent.size());
        // round 190jl: a MIDI Out's knobs, patch, bank, pan / volume / pitch to the hardware outputs on its port too
        if (count > 0 && c < maxChannels && (hwMidiOn[(std::size_t) c] & 1) != 0 && (std::size_t) c < state.hardwareMidiPort.size())
            for (int k = 0; k < count; ++k)
            {
                const auto m = sent[(std::size_t) k];
                const std::uint8_t bytes[3] { (std::uint8_t) (m & 0xff), (std::uint8_t) ((m >> 8) & 127), (std::uint8_t) ((m >> 16) & 127) };
                addHardwareMidi (liveInputOffset, state.hardwareMidiPort[(std::size_t) c], bytes, (bytes[0] & 0xE0) == 0xC0 ? 2 : 3, 0);
            }
        if (count <= 0 || (std::size_t) c >= state.midiTargets.size())
            continue;
        for (int k = 0; k < count; ++k)
        {
            const auto m = sent[(std::size_t) k];
            const std::uint8_t bytes[3] { (std::uint8_t) (m & 0xff), (std::uint8_t) ((m >> 8) & 127), (std::uint8_t) ((m >> 16) & 127) };
            const auto size = (bytes[0] & 0xE0) == 0xC0 ? 2 : 3; // (program change, channel pressure: one data byte)
            for (const auto t : state.midiTargets[(std::size_t) c])
                if (t >= 0 && t < (int) state.instruments.size() && state.instruments[(std::size_t) t] != nullptr)
                    state.instruments[(std::size_t) t]->midiMessage (bytes, size);
        }
    }

    // live-played notes routed through their channel's note path (round 149)
    if (liveRoutedCount > 0)
    {
        for (int k = 0; k < liveRoutedCount; ++k)
            events.add (liveRouted[(std::size_t) k]);
        liveRoutedCount = 0;
        events.sortByOffset();
    }

    // Layer channels play their children (round 164)
    if (state.hasLayers)
        expandLayers (state);
    // VFX channels (round 164b): their processors turn their notes into what they send on their ports
    if (! state.vfxChannels.empty())
        runVfxChannels (state, playing, numSamples);

    // cut groups: each note of a channel "cuts" the channels linked to it, at the same sample
    if (state.hasCutGroups)
    {
        const auto count = events.size();

        for (int i = 0; i < count; ++i)
        {
            const auto e = events[i];

            if (e.channel < 0 || e.channel >= (int) state.cutTargets.size())
                continue;
            // (round 190fe: an FL Studio Mobile pad of a lane that holds its notes cuts at the note's end as well)
            if (e.type != NoteEvent::Type::on
                && ! (e.type == NoteEvent::Type::off && (std::size_t) e.channel < song.channels.size() && song.channels[(std::size_t) e.channel].cutAtNoteEnd))
                continue;

            for (auto target : state.cutTargets[(std::size_t) e.channel])
            {
                NoteEvent choke;
                choke.type = NoteEvent::Type::choke;
                choke.channel = target;
                choke.sampleOffset = e.sampleOffset;
                events.add (choke);
            }
        }

        events.sortByOffset();
    }

    // MIDI to the mixer-slot Patchers that take a MIDI port (round 162): at its sample (round 164f)
    if (state.hasEffectMidiRoutes)
        for (int k = 0; k < events.size(); ++k)
            deliverToEffects (state, events[k]);
}

WorkerPool* RackRenderer::chunkSchedule (const RackState& state, int numSamples, int eventCount) noexcept
{
    const auto& song = state.song;
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    // Round 190ge: no note or event in this chunk, no live input, and the last one took under 60 µs (an idle project:
    // the channels idle, the effects smart-disabled): the workers stay asleep. Waking them per chunk cost an idle
    // project with 3 workers ~15 % of a core at 64 samples, ten times the work itself. Serial and parallel give the
    // same samples (the task graph and the layers only change who computes what); the next note runs in parallel.
    auto* const pool = workers;
    if (pool != nullptr && eventCount == 0 && lastChunkNs < lightChunkNs && (liveInput == nullptr || liveInput->numChannels == 0))
        workers = nullptr;
    // A7 (round 190gq): the same when the work the chunk's units did last time comes to less than handing it out costs
    // (waking the workers, their spinning, the last one's wait), and nothing starts in this chunk: a chunk where notes
    // or events start can start voices the last one did not have (measured: Exoplanet's bursts after a quiet chunk ran
    // past their block when this rule took them too). The units' times are measured whenever workers are there, serial
    // or not, so the next chunk decides on fresh numbers.
    // Round 190gq (REA 190hc action 1): whether the work goes to the workers at all is measured too. The snapshot's
    // guess (2+ hosted plugins or 24+ channels) left a set of LYNX's own devices (Live's "Patience", 15 tracks) on one
    // thread at 104-110 % of its deadline with every worker idle: a chunk whose units' work last time would fill most of
    // its real time runs in parallel whatever its plugin and channel counts.
    measureUnits = pool != nullptr && adaptiveThreads();
    parallelChunk = state.parallel;
    if (adaptiveThreads() && workers != nullptr)
    {
        std::uint64_t work = 0;
        const auto channelCount = std::min<int> ({ (int) state.instruments.size(), (int) song.channels.size(), maxChannels });
        for (int c = 0; c < channelCount; ++c)
            work += channelCostNs[(std::size_t) c];
        for (int t = 1; t < tracks && t < maxMixerTracks; ++t)
            work += trackCostNs[(std::size_t) t];
        // (the snapshot's guess, with A7's serial rule below; or, whatever the counts, when the chunk's measured work
        // would take more than parallelShare of its real time on one thread: parallelism pays only near the deadline —
        // measured: Patience at 64 samples, 53 % of a core in small units, cost more CPU and more overruns in parallel)
        const auto chunkNs = sampleRate > 0.0 ? 1.0e9 * numSamples / sampleRate : 0.0;
        parallelChunk = state.parallel || (double) work >= parallelShare * chunkNs;
        lastWorkShareValue = chunkNs > 0.0 ? (double) work / chunkNs : 0.0;
        if (eventCount == 0 && work < serialWorkNs && (liveInput == nullptr || liveInput->numChannels == 0))
            workers = nullptr;
        // round 190kr: as many workers woken as the work needs (WorkerPool::Tuning::helpPercent): ~1 ms of Thief's work in a
        // 2.9 ms chunk woke all five
        if (const auto help = WorkerPool::tuning().helpPercent.load (std::memory_order_relaxed); help > 0 && chunkNs > 0.0)
            pool->setWakeLimit (std::clamp ((int) std::ceil ((double) work / (help * 0.01 * chunkNs)) - 1, 0, pool->workers()));
        else
            pool->setWakeLimit (-1);
    }
    return pool;
}

void RackRenderer::chunkUnits (const RackState& state, bool playing, int numSamples, MeterBank* meters) noexcept
{
    const auto& song = state.song;
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    // 1) channels, each with its own notes at the exact sample (in parallel when worth it)
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) song.channels.size(), maxChannels });
    auto channel = [this, &state, numSamples] (int c) {
        const ScopedHostTimeline timelineScope (ownTimeline); // (a worker runs it: round 190gq)
        const auto timed = times->enabled.load (std::memory_order_relaxed) && c < ProcessTimes::channels;
        if (! timed && ! measureUnits)
        {
            renderChannel (state, c, numSamples);
            return;
        }
        const auto before = ProcessTimes::now();
        renderChannel (state, c, numSamples);
        const auto spent = ProcessTimes::now() - before;
        if (timed)
            times->addCall (true, (std::size_t) c, spent, audioNsOf (numSamples));
        if (measureUnits)
            channelCostNs[(std::size_t) c] = (std::uint32_t) std::min<std::uint64_t> (spent, 0xffffffffu);
    };

    // the task graph (round 165i): channels and inserts by readiness; else the channel pass, then the inserts by layers
    // FL's Settings › Audio › CPU (round 190bx): "Multithreaded generator processing" and "Multithreaded mixer
    // processing". With either off that half runs on the audio thread alone (the layers then: the task graph mixes both).
    const auto threadGenerators = flsettings::options().multithreadGenerators.load (std::memory_order_relaxed);
    const auto threadMixer = flsettings::options().multithreadMixer.load (std::memory_order_relaxed);
    if (! (threadGenerators && threadMixer) || ! runTaskGraph (state, playing, channels, numSamples, meters))
    {
        if (workers != nullptr && parallelChunk && threadGenerators && state.audioThreadChannels.empty())
        {
            // A7: neighbouring light channels go out together, a cluster about worth one hand-off
            auto clusters = 0;
            const auto target = adaptiveThreads() ? clusterWorkNs : 0;
            std::uint64_t sum = target;
            for (int c = 0; c < channels; ++c)
            {
                if (sum >= target)
                {
                    clusterStart[(std::size_t) clusters++] = (std::int16_t) c;
                    sum = 0;
                }
                sum += channelCostNs[(std::size_t) c];
            }
            clusterStart[(std::size_t) clusters] = (std::int16_t) channels;
            auto cluster = [this, &channel] (int k) {
                for (int c = clusterStart[(std::size_t) k]; c < clusterStart[(std::size_t) k + 1]; ++c)
                    channel (c);
            };
            workers->parallelFor (clusters, cluster);
        }
        else if (workers != nullptr && parallelChunk && threadGenerators)
        {
            // "Allow threaded processing" off: those channels on this thread, the rest in parallel
            auto threaded = [&state, &channel, channels] (int i) {
                if (const auto c = state.threadedChannels[(std::size_t) i]; c < channels)
                    channel (c);
            };
            workers->parallelFor ((int) state.threadedChannels.size(), threaded);
            for (const auto c : state.audioThreadChannels)
                if (c < channels)
                    channel (c);
        }
        else
            for (int c = 0; c < channels; ++c)
                channel (c);

        for (int c = 0; c < channels; ++c)
        {
            if (channelSilent[(std::size_t) c])
                continue;
            const auto t = std::clamp (song.channels[(std::size_t) c].mixerTrack, 0, tracks - 1);
            auto* dl = trackBuffer (t, 0);
            auto* dr = trackBuffer (t, 1);
            const auto* sl = channelBuffer (c, 0);
            const auto* sr = channelBuffer (c, 1);

            for (int k = 0; k < numSamples; ++k)
            {
                dl[k] += sl[k];
                dr[k] += sr[k];
            }

            // extra outputs to other inserts (a generator Patcher's, REA_rodada141) (render-ahead: only the renderer that
            // rendered the channel reads them; its targets are on its side, core/RenderAhead.h)
            if ((std::size_t) c < state.instruments.size() && state.instruments[(std::size_t) c] != nullptr && ! foreignChannel (c))
            {
                const auto& instrument = *state.instruments[(std::size_t) c];
                for (int k = 0; k < instrument.auxOutputCount(); ++k)
                {
                    const auto target = instrument.auxOutputTrack (k);
                    const auto* al = instrument.auxOutputData (k, 0);
                    const auto* ar = instrument.auxOutputData (k, 1);
                    if (target < 0 || target >= tracks || al == nullptr || ar == nullptr)
                        continue;
                    auto* tl = trackBuffer (target, 0);
                    auto* tr = trackBuffer (target, 1);
                    for (int x = 0; x < numSamples; ++x)
                    {
                        tl[x] += al[x];
                        tr[x] += ar[x];
                    }
                }
            }
        }

        if (liveInput != nullptr && liveInput->numChannels > 0)
            mixLiveInput (state, playing, numSamples);
        mixMetronome (state, metronomeTarget (state), numSamples);
        mixSideInput (state, sideTrack, numSamples);

        // 2) inserts by layers: within a layer nothing depends on anything else; each track pulls the
        //    outputs of the tracks that send to it (without writing into others' buffers)
        trackActive.fill (0);
        tapActive.fill (0);

        for (std::size_t d = 0; d < state.mixerLayers.size(); ++d)
        {
            const auto split = d < state.audioThreadLayers.size() && ! state.audioThreadLayers[d].empty();
            const auto& layer = split ? state.threadedLayers[d] : state.mixerLayers[d];
            auto track = [this, &state, &layer, numSamples, meters] (int i) {
                const ScopedHostTimeline timelineScope (ownTimeline);
                const auto t = layer[(std::size_t) i];
                if (! measureUnits)
                {
                    processTrack (state, t, numSamples, meters);
                    return;
                }
                const auto before = ProcessTimes::now();
                processTrack (state, t, numSamples, meters);
                if (t >= 0 && t < maxMixerTracks)
                    trackCostNs[(std::size_t) t] = (std::uint32_t) std::min<std::uint64_t> (ProcessTimes::now() - before, 0xffffffffu);
            };
            // A7: a layer whose inserts did less work last time than a hand-off costs runs here
            std::uint64_t layerWork = 0;
            for (const auto t : layer)
                if (t >= 0 && t < maxMixerTracks)
                    layerWork += trackCostNs[(std::size_t) t];

            if (workers != nullptr && parallelChunk && threadMixer && layer.size() > 1 && (layerWork >= layerSerialNs || ! adaptiveThreads()))
                workers->parallelFor ((int) layer.size(), track);
            else
                for (int i = 0; i < (int) layer.size(); ++i)
                    track (i);

            if (split) // inserts with a plugin whose "Allow threaded processing" is off
                for (const auto t : state.audioThreadLayers[d])
                    processTrack (state, t, numSamples, meters);
        }
    }

}

void RackRenderer::chunkMaster (const RackState& state, int numSamples, float* left, float* right, MeterBank* meters, WorkerPool* pool,
                                std::uint64_t chunkStartNs) noexcept
{
    const auto& song = state.song;
    workers = pool;

    // render-ahead (round 190gq): what the live part takes from this one goes out now; the Master is the live part's
    if (split != nullptr)
    {
        if (split->exports && splitBlock != nullptr)
            exportSplit (numSamples);
        ++splitChunk;
        if (foreignTrack (0))
        {
            if (workers != nullptr)
                lastChunkNs = ProcessTimes::now() - chunkStartNs;
            return;
        }
    }

    // 3) Master: inputs, effects, fader. Without a configured mixer, it stays at 0 dB.
    if (renderTaps != nullptr) // round 190gj: what the Master holds before its routes (its own channels)
        copyToRenderTap (0, trackBuffer (0, 0), trackBuffer (0, 1), numSamples);
    pullInputs (state, 0, numSamples);
    if (meters != nullptr)
        meters->rollingLog.push (RollingAudioLog::Position::preEffects, trackBuffer (0, 0), trackBuffer (0, 1), numSamples);
    runEffects (state, 0, numSamples);

    if (! song.mixer.empty())
    {
        applyTrackEq (song.mixer[0], 0, song.cleanMixer, trackBuffer (0, 0), trackBuffer (0, 1), numSamples);
    }
    if (meters != nullptr)
        meters->rollingLog.push (RollingAudioLog::Position::postEffects, trackBuffer (0, 0), trackBuffer (0, 1), numSamples);

    MixerMatrix target; // without a mixer: unity
    std::optional<MixerMatrix> mutedLevel; // issue #144: a muted Master's meter shows its level without the mute (FL)

    if (! song.mixer.empty())
    {
        const auto& master = song.mixer[0];
        const auto fader = liveLevel (state, &LiveLevels::mixerVolume, 0, master.volume);
        const auto knob = liveLevel (state, &LiveLevels::mixerPan, 0, master.pan);
        const auto volume = state.hasAutomation ? automatedLevel (state, state.mixerVolumeAuto[0], fader, 0) : fader;
        const auto pan = state.hasAutomation ? automatedLevel (state, state.mixerPanAuto[0], (knob + 1.0f) * 0.5f, 0) * 2.0f - 1.0f : knob;
        const auto level = mixerMatrix (master, volume, pan, song.panTriangular, liveLevel (state, &LiveLevels::mixerStereo, 0, master.stereo));
        target = master.muted ? MixerMatrix { 0.0f, 0.0f, 0.0f, 0.0f } : level;
        if (master.muted && meters != nullptr && ! song.mutedInsertsSkip)
            mutedLevel = level;
    }

    if (mutedLevel) // (read before the buffers are scaled; nothing is written)
    {
        const auto& m = *mutedLevel;
        const auto* a = trackBuffer (0, 0);
        const auto* b = trackBuffer (0, 1);
        float pl = 0.0f, pr = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            pl = std::max (pl, std::abs (m.ll * a[i] + m.rl * b[i]));
            pr = std::max (pr, std::abs (m.rr * b[i] + m.lr * a[i]));
        }
        meters->accumulate (0, pl, pr);
    }

    lastTrackGain[0] = applyMatrix (trackBuffer (0, 0), trackBuffer (0, 1), left, right, numSamples, lastTrackGain[0], target);

    if (meters != nullptr)
    {
        meters->accumulate (0, peakOf (left, numSamples), peakOf (right, numSamples));
        meterPower (meters, 0, left, right, numSamples);
        meters->capture.push (0, left, right, numSamples);
        meters->logTap.push (0, left, right, numSamples);
        meters->loudness.push (left, right, numSamples, sampleRate); // (round 190am: one atomic load while it is off)
        if (chunkPlaying)
            meters->bounceTap.push (0, left, right, numSamples);
    }

    // FL's Current track on the Master (round 190mv: REA 190mv R, the Master selected — or the Current track itself — is its
    // source): what leaves the Master, after its meter
    if (state.currentTrack > 0 && currentSource.load (std::memory_order_relaxed) == 0)
        runCurrentTrack (state, left, right, numSamples, meters);

    if (hwOut != nullptr && ! state.hwRoutes.empty() && hwOut->active.load (std::memory_order_relaxed))
        mixHardwareOutputs (state, numSamples);
    if (workers != nullptr)
        lastChunkNs = ProcessTimes::now() - chunkStartNs;
}

// 4) inserts with a hardware output target: their post-fader output (the same signal the Master gets) into the
//    device-channel buses, delayed like the Master's path
void RackRenderer::mixHardwareOutputs (const RackState& state, int n) noexcept
{
    auto* sl = hwScratch.data();
    auto* sr = hwScratch.data() + blockCapacity;

    for (const auto& route : state.hwRoutes)
    {
        const auto t = route.track;
        if (t <= 0 || t >= maxMixerTracks)
            continue;

        const float* l = trackActive[(std::size_t) t] ? trackInput (t, 0) : nullptr;
        const float* r = trackActive[(std::size_t) t] ? trackInput (t, 1) : nullptr;
        const auto bypassed = pdcBypassNow && (std::size_t) t < state.bypassWhenIdle.size() && state.bypassWhenIdle[(std::size_t) t] != 0;

        if (route.delay != nullptr && ! bypassed)
        {
            if (l != nullptr)
            {
                std::copy (l, l + n, sl);
                std::copy (r, r + n, sr);
            }
            else
            {
                std::fill (sl, sl + n, 0.0f);
                std::fill (sr, sr + n, 0.0f);
            }
            route.delay->process (sl, sr, n);
            l = sl;
            r = sr;
        }

        if (l == nullptr)
            continue;
        if (route.cue) // round 190nc: the Cue Volume
            if (const auto g = hwOut->cueGain.load (std::memory_order_relaxed); g != 1.0f)
            {
                for (int i = 0; i < n; ++i) // (in place when the delay already wrote them)
                {
                    sl[i] = l[i] * g;
                    sr[i] = r[i] * g;
                }
                l = sl;
                r = sr;
            }

        const auto [first, count] = outputChannelsOf (route.channel, route.mode);
        auto* a = hwOut->bus (first, liveInputOffset, n);
        auto* b = count > 1 ? hwOut->bus (first + 1, liveInputOffset, n) : nullptr;
        addToOutput (l, r, route.mode, a, b, n);
    }
}

void RackRenderer::mixLiveInput (const RackState& state, bool playing, int n) noexcept
{
    const auto recording = liveInput->recording.load (std::memory_order_relaxed);

    // the Master too (FL's slot panel has its audio input selector; REA round 143): it plays whenever its
    // monitoring is not off (it has no record arm)
    for (int t = 0; t < (int) state.song.mixer.size() && t < maxMixerTracks; ++t)
        mixLiveInputTrack (state, t, recording, playing, n);
}

void RackRenderer::mixLiveInputTrack (const RackState& state, int t, bool recording, bool playing, int n) noexcept
{
    if (t >= (int) state.song.mixer.size() || t >= maxMixerTracks)
        return;
    const auto& track = state.song.mixer[(std::size_t) t];
    const auto [first, count] = inputChannelsOf (track, liveInput->numChannels);

    if (count == 0)
        return;

    const auto* a = liveInput->channel (first, liveInputOffset);
    const auto* b = count > 1 ? liveInput->channel (first + 1, liveInputOffset) : a;

    if (a == nullptr || b == nullptr)
        return;

    liveInput->meters.accumulate (t, peakOf (a, n), peakOf (b, n));

    if (t == 0 ? track.monitor == InputMonitor::off
               : ! inputMonitored (track.monitor, liveInput->armed[(std::size_t) t].load (std::memory_order_relaxed), playing, recording))
        return;

    auto* dl = trackBuffer (t, 0);
    auto* dr = trackBuffer (t, 1);

    for (int i = 0; i < n; ++i)
    {
        dl[i] += a[i];
        dr[i] += b[i];
    }
}

void RackRenderer::sumChannelsInto (const RackState& state, int t, int channels, int n) noexcept
{
    if ((std::size_t) t >= state.trackChannels.size())
        return;
    float* dl = nullptr;
    float* dr = nullptr;
    for (const auto c : state.trackChannels[(std::size_t) t])
    {
        if (c >= channels || channelSilent[(std::size_t) c])
            continue;
        if (dl == nullptr) // (round 190gp: the insert's buffer only once a channel sounds into it)
        {
            dl = trackBuffer (t, 0);
            dr = trackBuffer (t, 1);
        }
        const auto* sl = channelBuffer (c, 0);
        const auto* sr = channelBuffer (c, 1);
        for (int k = 0; k < n; ++k)
        {
            dl[k] += sl[k];
            dr[k] += sr[k];
        }
    }
}

// FL's FUN_012c19f0: "When recording or not playing" bypasses while the transport is stopped or recording. Back to
// compensated, those tracks' lines are emptied: they start from silence instead of replaying what they held.
void RackRenderer::updatePdcBypass (const RackState& state, bool playing) noexcept
{
    const auto recording = liveInput != nullptr && liveInput->recording.load (std::memory_order_relaxed);
    pdcBypassNow = state.anyBypassWhenIdle && (! playing || recording);

    if (pdcBypassBefore && ! pdcBypassNow)
    {
        for (std::size_t t = 0; t < state.bypassWhenIdle.size() && t < state.sendDelays.size(); ++t)
            if (state.bypassWhenIdle[t] != 0)
            {
                const auto& sends = state.song.mixer[t].sends;
                const auto plain = std::min (state.sendDelays[t].size(), 1 + sends.size()); // not the Fruity Send taps
                for (std::size_t k = 0; k < plain; ++k)
                    // (render-ahead: a line is run, so emptied, by the renderer of the insert it arrives at)
                    if (state.sendDelays[t][k] != nullptr && ! foreignTrack (k == 0 ? 0 : sends[k - 1].target))
                        state.sendDelays[t][k]->clear();
            }
        for (const auto& route : state.hwRoutes)
            if (route.delay != nullptr && (std::size_t) route.track < state.bypassWhenIdle.size() && state.bypassWhenIdle[(std::size_t) route.track] != 0
                && ! foreignTrack (route.track))
                route.delay->clear();
        // round 190hq: the side inputs from such a source (run by the listening insert's renderer)
        for (std::size_t t = 0; t < state.sideLines.size() && t < state.song.mixer.size(); ++t)
        {
            if (foreignTrack ((int) t))
                continue;
            const auto& slots = state.song.mixer[t].effects;
            for (std::size_t s = 0; s < state.sideLines[t].size() && s < slots.size(); ++s)
            {
                // (no allocation here: the Connections as the state holds them)
                const auto* c = t < state.slotConnections.size() && s < state.slotConnections[t].size() && state.slotConnections[t][s].routed
                                    ? &state.slotConnections[t][s]
                                    : nullptr;
                const std::array<int, 4> sources { slots[s].sidechain, slots[s].sidechain2, c != nullptr ? c->mainSource : -1, c != nullptr ? c->sideSource : -1 };
                for (std::size_t k = 0; k < 4; ++k)
                    if (const auto& line = state.sideLines[t][s][k]; line != nullptr && line->delay != nullptr && sources[k] > 0
                        && (std::size_t) sources[k] < state.bypassWhenIdle.size() && state.bypassWhenIdle[(std::size_t) sources[k]] != 0)
                        line->delay->clear();
            }
        }
    }
    pdcBypassBefore = pdcBypassNow;
}

bool RackRenderer::runTaskGraph (const RackState& state, bool playing, int channels, int n, MeterBank* meters) noexcept
{
    // LYNX_TASKGRAPH=0: the layers only (A/B measurements)
    static const bool enabled = [] { const char* e = std::getenv ("LYNX_TASKGRAPH"); return e == nullptr || std::atoi (e) != 0; }();
    if (! enabled || ! state.graphOk || workers == nullptr || ! parallelChunk || state.graphChannels != channels)
        return false;
    const auto tracks = std::max<int> (1, (int) state.song.mixer.size());
    const auto tasks = channels + tracks - 1;
    if (tasks <= 0 || tasks > graphCapacity || (int) state.graphIndegree.size() != tasks)
        return false;
    // extra outputs of an instrument (a generator Patcher's) go to inserts known only now: the layers then
    for (int c = 0; c < channels; ++c)
        if (state.instruments[(std::size_t) c] != nullptr && state.instruments[(std::size_t) c]->auxOutputCount() > 0)
            return false;

    trackActive.fill (0);
    tapActive.fill (0);
    const auto live = liveInput != nullptr && liveInput->numChannels > 0;
    const auto recording = live && liveInput->recording.load (std::memory_order_relaxed);

    for (int i = 0; i < tasks; ++i)
    {
        graphPending[(std::size_t) i].store (state.graphIndegree[(std::size_t) i], std::memory_order_relaxed);
        graphSlots[(std::size_t) i].store (0, std::memory_order_relaxed);
    }
    auto pushed = 0;
    for (const auto r : state.graphRoots)
        graphSlots[(std::size_t) pushed++].store (r + 1, std::memory_order_relaxed);
    graphPushed.store (pushed, std::memory_order_release); // (the pool publishes its work after this: seq_cst)

    const auto timing = times->enabled.load (std::memory_order_relaxed);
    // round 190kb: the wait for a position's task (WorkerPool::Tuning::waitMode): 0 spin with a yield every 1024
    // pauses (before), 1 bounded spin then sleep until notified, 2 bounded spin then hand the position over — the thread
    // that pushes its task runs it at once itself (no wake-up on the critical path) and this one goes back to the pool
    constexpr int handedOver = -1;
    const auto waitMode = WorkerPool::tuning().waitMode.load (std::memory_order_relaxed);
    const auto handOffSpin = std::chrono::microseconds (WorkerPool::tuning().waitSpinMicros.load (std::memory_order_relaxed));
    auto task = [&] (int position) {
        const ScopedHostTimeline timelineScope (ownTimeline);
        int id = 0;
        auto& slot = graphSlots[(std::size_t) position];
        if (waitMode == 2)
        {
            if ((id = slot.load (std::memory_order_acquire)) == 0)
            {
                const auto start = std::chrono::steady_clock::now();
                auto handed = false;
                for (int spins = 0; (id = slot.load (std::memory_order_acquire)) == 0; ++spins)
                {
                    WorkerPool::relax();
                    if ((spins & 63) == 63 && std::chrono::steady_clock::now() - start > handOffSpin)
                    {
                        auto expected = 0;
                        if (slot.compare_exchange_strong (expected, handedOver, std::memory_order_acq_rel))
                        {
                            handed = true;
                            break;
                        }
                        id = expected; // (its task came at that moment)
                        break;
                    }
                }
                workers->noteWait ((std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - start).count(), handed);
                if (handed)
                    return;
            }
        }
        else
            // (round 190kb: mode 1, a bounded spin, then sleep until the task pushed into this position wakes it)
            workers->await (position, [&] { return (id = slot.load (std::memory_order_acquire)) != 0; });
        --id;
        // the tasks of handed-over positions this thread runs (pushed by it); more than fit: run in place, recursively
        std::array<int, 32> taken;
        int takenCount = 0;
        auto chain = [&] (int first, auto& self) -> void {
            auto at = first;
        for (;;) // (a serial chain runs here link after link: round 190gq)
        {
        const auto before = timing || measureUnits ? ProcessTimes::now() : 0; // (A7: every task's time, for the next chunk's choice)
        if (at < channels)
        {
            renderChannel (state, at, n);
            const auto spent = ProcessTimes::now() - before;
            if (timing && at < ProcessTimes::channels)
                times->addCall (true, (std::size_t) at, spent, audioNsOf (n));
            if (measureUnits)
                channelCostNs[(std::size_t) at] = (std::uint32_t) std::min<std::uint64_t> (spent, 0xffffffffu);
        }
        else
        {
            const auto t = at - channels + 1;
            sumChannelsInto (state, t, channels, n);
            if (live)
                mixLiveInputTrack (state, t, recording, playing, n);
            mixMetronome (state, t, n);
            mixSideInput (state, t, n);
            processTrack (state, t, n, meters);
            if (measureUnits && t < maxMixerTracks)
                trackCostNs[(std::size_t) t] = (std::uint32_t) std::min<std::uint64_t> (ProcessTimes::now() - before, 0xffffffffu);
        }
        const auto fused = (std::size_t) at < state.graphFused.size() ? state.graphFused[(std::size_t) at] : -1;
        if (fused < 0)
            break;
        at = fused;
        }
        for (auto k = state.graphNextStart[(std::size_t) at]; k < state.graphNextStart[(std::size_t) at + 1]; ++k)
        {
            const auto j = state.graphNext[(std::size_t) k];
            if (graphPending[(std::size_t) j].fetch_sub (1, std::memory_order_acq_rel) == 1)
            {
                const auto pos = graphPushed.fetch_add (1, std::memory_order_acq_rel);
                auto expected = 0;
                if (graphSlots[(std::size_t) pos].compare_exchange_strong (expected, j + 1, std::memory_order_acq_rel))
                    workers->notify (pos);
                else if (takenCount < (int) taken.size()) // (its thread handed the position over: this one runs it)
                    taken[(std::size_t) takenCount++] = j;
                else
                    self (j, self);
            }
        }
        };
        chain (id, chain);
        while (takenCount > 0)
            chain (taken[(std::size_t) --takenCount], chain);
    };
    workers->parallelFor (tasks - state.graphFusedCount, task);

    // the Master's own channels and input (its effects run after, as before)
    sumChannelsInto (state, 0, channels, n);
    if (live)
        mixLiveInputTrack (state, 0, recording, playing, n);
    mixMetronome (state, 0, n);
    return true;
}

// ---- Round 190kz (sched): one task-graph round per device callback ----------------------------------------------------
// A block the loop in process() cuts in pieces (with automation: pieces of 128 on the export's grid of 512, and wherever
// a pattern event falls) ran one worker-pool round per piece: at a device block of 480, about four wake-ups, spins and
// waits for the last task per block, half of the audio CPU on Windows. A batched block runs
//   1. every piece's serial part (chunkPrelude: the automation's values, the transport, the notes and events), in order,
//      each into its own slot of the per-chunk state (PieceValue / PieceArray / PieceEvents, pieceTimelines);
//   2. ONE round of the task graph in which each channel and insert renders its pieces one after another, piece k with
//      slot k's state and its own plugin parameter bindings for that piece (applyUnitParams: what updateAutomation's
//      applyPluginParams did for that unit at that piece), into its buffers at the piece's offset;
//   3. the Master's pieces, in order (chunkMaster), and the last piece's state becomes the renderer's again.
// Every unit gets exactly the calls it got piece by piece before — the same sizes, values and order, its inputs' same
// samples — so the output is the same bit for bit; only what runs at the same time changes (as with the task graph
// itself). It is used only where nothing a piece's serial part does depends on the previous piece's units, or reaches
// a unit outside its own task (batchable): no MIDI a unit sends to the next chunk (wrapper output ports, VFX, Dashboards,
// hardware MIDI, mixer-slot MIDI routes), no live controllers (they read a unit's output), no effect that follows the
// Channel rack's held notes, no sample-accurate plugin parameters, no launchers, no render-ahead split, no PDC "when
// recording or not playing" switch, no generator extra outputs, no binding that reaches another unit; at most maxPieces
// pieces and one mixer buffer of samples. Anything else plays piece by piece as before.
// LYNX_POOL="rounds=chunk" (or rounds=0): one round per piece, as before (A/B); "rounds=block" (1, the default): batched.
namespace
{
bool taskGraphEnabled() noexcept
{
    static const bool enabled = [] { const char* e = std::getenv ("LYNX_TASKGRAPH"); return e == nullptr || std::atoi (e) != 0; }();
    return enabled;
}

void copyTimeline (const HostTimeline& from, HostTimeline& to) noexcept
{
    constexpr auto r = std::memory_order_relaxed;
    to.tick.store (from.tick.load (r), r);
    to.loopStart.store (from.loopStart.load (r), r);
    to.loopEnd.store (from.loopEnd.load (r), r);
    to.looping.store (from.looping.load (r), r);
    to.bpm.store (from.bpm.load (r), r);
    to.sampleRate.store (from.sampleRate.load (r), r);
    to.playing.store (from.playing.load (r), r);
    to.beatsPerBar.store (from.beatsPerBar.load (r), r);
    to.beatUnit.store (from.beatUnit.load (r), r);
    to.ticksPerSample.store (from.ticksPerSample.load (r), r);
    to.wrapAt.store (from.wrapAt.load (r), r);
    to.wrapTo.store (from.wrapTo.load (r), r);
    to.chunk.store (from.chunk.load (r), r);
}
} // namespace

bool RackRenderer::buildUnitParams (const RackState& state) noexcept
{
    const auto& song = state.song;
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) song.channels.size(), maxChannels });
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    const auto units = (std::size_t) maxChannels + maxMixerTracks;
    unitParamsBend = unitParamsTimed = unitParamsHeld = false;
    if (unitParamStart.size() < units + 1 || state.pluginParams.size() > unitParams.size())
        return false;
    // each binding's unit, as applyPluginParam reaches it: -1 = it sets nothing, -2 = it reaches another unit (or one
    // that is not rendered as a task): not batched
    auto unitOf = [&] (const RackState::PluginParamBinding& b) noexcept -> int {
        if (b.channel >= 0)
        {
            const auto c = (std::size_t) b.channel;
            if (c < state.midiTargets.size() && midictl::isMidiParam (b.param) && ! state.midiTargets[c].empty())
                return -2; // (a MIDI Out's controls reach the plugins on its port)
            const auto kind = c < song.channels.size() ? song.channels[c].instrument : InstrumentKind::plugin;
            if (midictl::isPitchBend (b.param) && kind != InstrumentKind::plugin && kind != InstrumentKind::midiOut
                && b.channel < (int) state.instruments.size() && b.channel < maxChannels)
            {
                // (applyLiveControl: a Layer's children, a MIDI Out's port)
                if (kind == InstrumentKind::layer || (state.hasMidiRoutes && c < state.midiTargets.size() && ! state.midiTargets[c].empty()))
                    return -2;
                unitParamsBend = true;
                return b.channel < channels ? b.channel : -2;
            }
            if (b.channel >= (int) state.instruments.size())
                return -1;
            if (const auto* instrument = state.instruments[c].get(); instrument != nullptr && instrument->takesTimedParameters())
                unitParamsTimed = true;
            return b.channel < channels ? b.channel : -2;
        }
        if (b.track < 0)
            return -2;
        if (b.track >= (int) state.effects.size() || b.slot < 0 || b.slot >= (int) state.effects[(std::size_t) b.track].size())
            return b.slot < 0 ? -2 : -1;
        if (const auto* effect = state.effects[(std::size_t) b.track][(std::size_t) b.slot].get(); effect != nullptr && effect->takesTimedParameters())
            unitParamsTimed = true;
        return b.track < tracks && b.track < maxMixerTracks ? maxChannels + b.track : -2;
    };
    std::fill (unitParamStart.begin(), unitParamStart.begin() + (std::ptrdiff_t) units + 1, 0);
    for (const auto& b : state.pluginParams)
    {
        const auto u = unitOf (b);
        if (u == -2)
            return false;
        if (u >= 0)
            ++unitParamStart[(std::size_t) u + 1];
    }
    for (std::size_t u = 0; u < units; ++u)
        unitParamStart[u + 1] += unitParamStart[u];
    // (a second pass, in the bindings' order: each unit keeps applyPluginParams' order; unitParamStart[u] moves on to its
    // end as it fills, then back)
    for (std::size_t k = 0; k < state.pluginParams.size(); ++k)
        if (const auto u = unitOf (state.pluginParams[k]); u >= 0)
            unitParams[(std::size_t) unitParamStart[(std::size_t) u]++] = (int) k;
    for (std::size_t u = units; u > 0; --u)
        unitParamStart[u] = unitParamStart[u - 1];
    unitParamStart[0] = 0;
    // the effects that follow the Channel rack's held notes read every channel's at their piece: not batched — a slot
    // whose settings read them, or one that can and has automated parameters (its settings may turn that on mid-block)
    for (std::size_t t = 0; t < state.effects.size() && t < song.mixer.size(); ++t)
        for (std::size_t s = 0; s < state.effects[t].size() && s < song.mixer[t].effects.size(); ++s)
        {
            const auto* effect = state.effects[t][s].get();
            if (effect == nullptr || ! effect->followsHeldNotes())
                continue;
            auto automated = false;
            if (t < state.effectParamAuto.size() && s < state.effectParamAuto[t].size())
                for (const auto a : state.effectParamAuto[t][s])
                    automated = automated || a >= 0;
            for (const auto& b : state.pluginParams)
                automated = automated || (b.channel < 0 && b.track == (int) t && b.slot == (int) s);
            if (automated || effect->followsHeldNotesWith (song.mixer[t].effects[s].params))
                unitParamsHeld = true;
        }
    return true;
}

void RackRenderer::applyUnitParams (const RackState& state, int unit, const Piece& piece) noexcept
{
    if (! piece.params || unit < 0 || (std::size_t) unit + 1 >= unitParamStart.size())
        return;
    for (auto k = unitParamStart[(std::size_t) unit]; k < unitParamStart[(std::size_t) unit + 1]; ++k)
        applyPluginParam (state, (std::size_t) unitParams[(std::size_t) k], piece.paramsFirst, piece.paramsLive);
}

// Round 190lv (sched2): can this state's live controllers be batched (two rounds per block, processBatched)? In the
// per-piece path piece k+1's updateAutomation reads each controller's output after every unit rendered piece k. Batched,
// the source writes it into piece k+1's slot right after it rendered piece k, in round A; round B and the Master read it
// later. That is the same value read at the same point of the source's own calls. It is exact only when nothing else
// reads or writes the slot in a later piece's serial part (the placeholder must stay unseen), and when a round-A unit reads
// only the live values of its own source or of one upstream of it (any other source's pieces can run after the unit's).
// Anything else: piece by piece.
bool RackRenderer::buildLivePlan (const RackState& state) noexcept
{
    const auto& song = state.song;
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) song.channels.size(), maxChannels });
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    const auto tasks = channels + tracks - 1;
    const auto no = [this] (const char* why) noexcept {
        livePlanWhy = why;
        return false;
    };
    if (state.liveAutomations.empty() || tasks <= 0 || tasks > graphCapacity || (int) liveInA.size() < tasks
        || (int) liveSourceStart.size() < tasks + 1 || state.liveAutomations.size() > liveSources.size()
        || (int) state.graphNextStart.size() < tasks + 1 || (int) state.graphFused.size() < tasks)
        return no ("no graph");
    std::array<char, RackState::maxAutomations> live {};
    for (const auto a : state.liveAutomations)
    {
        if (a < 0 || a >= RackState::maxAutomations || (std::size_t) a >= song.automations.size())
            return no ("automation index");
        live[(std::size_t) a] = 1;
    }
    const auto isLive = [&live] (int slot) noexcept { return slot >= 0 && slot < RackState::maxAutomations && live[(std::size_t) slot] != 0; };

    // a slot a later piece's serial part reads or writes besides the live loop: clips, Performance zone, lanes, links,
    // modulators, pattern events; the tempo, VCAs, the crossfader (their readers are not one unit)
    for (const auto a : state.liveAutomations)
    {
        if (state.positional (a)
            || ((std::size_t) a < state.zoneAutomations.size() && state.zoneAutomations[(std::size_t) a] != 0)
            || a == state.tempoAutomation || a == state.crossfaderAutomation)
            return no ("clips, zone, tempo or crossfader on a live automation");
        if (std::find (state.vcaLevelAuto.begin(), state.vcaLevelAuto.end(), a) != state.vcaLevelAuto.end())
            return no ("a live VCA level");
    }
    for (const auto& lane : state.laneAutomations)
    {
        if (isLive (lane.automation))
            return no ("a live automation's lane");
        for (const auto peer : lane.clipPeers)
            if (isLive (peer))
                return no ("a live lane peer");
        for (const auto& held : lane.clipSlots)
            if (isLive (held.first) || isLive (held.second))
                return no ("a live lane peer");
    }
    for (const auto& link : state.linkSlots)
        if (isLive (link.automation) || isLive (link.slot))
            return no ("a link on a live slot");
    for (const auto& r : state.modRoutes)
        if (isLive (r.source) || isLive (r.slot))
            return no ("a modulator on a live slot");
    for (const auto& lane : state.eventLanes)
        if (isLive (lane.slot))
            return no ("pattern events on a live slot");

    const auto trackTask = [channels] (int t) noexcept { return channels + t - 1; };
    // the sources: a channel's controller instrument or an insert's effect (not the Master: it runs after the rounds)
    std::fill (liveInA.begin(), liveInA.begin() + tasks, 0);
    std::fill (liveSourceStart.begin(), liveSourceStart.begin() + tasks + 1, 0);
    const auto sourceOf = [&] (int a) noexcept -> int {
        const auto& src = song.automations[(std::size_t) a].source;
        if (src.channel >= 0)
            return src.channel < channels && state.instruments[(std::size_t) src.channel] != nullptr ? src.channel : -1;
        if (src.track <= 0 || src.track >= tracks || src.track >= maxMixerTracks || (std::size_t) src.track >= state.effects.size()
            || src.slot < 0 || (std::size_t) src.slot >= state.effects[(std::size_t) src.track].size()
            || state.effects[(std::size_t) src.track][(std::size_t) src.slot] == nullptr)
            return -1;
        return trackTask (src.track);
    };
    for (const auto a : state.liveAutomations)
    {
        const auto node = sourceOf (a);
        if (node < 0)
            return no ("a source that is not a task (the Master, an empty slot, a channel past the graph)");
        liveInA[(std::size_t) node] = 1;
        ++liveSourceStart[(std::size_t) node + 1];
    }
    for (int i = 0; i < tasks; ++i)
        liveSourceStart[(std::size_t) i + 1] += liveSourceStart[(std::size_t) i];
    for (const auto a : state.liveAutomations) // (in liveAutomations' order per source; the cursor moves to its end, then back)
        liveSources[(std::size_t) liveSourceStart[(std::size_t) sourceOf (a)]++] = a;
    for (int i = tasks; i > 0; --i)
        liveSourceStart[(std::size_t) i] = liveSourceStart[(std::size_t) i - 1];
    liveSourceStart[0] = 0;

    // round A: every unit that reaches a source (the upstream closure: its units wait only for each other)
    for (auto changed = true; changed;)
    {
        changed = false;
        for (int i = 0; i < tasks; ++i)
            if (liveInA[(std::size_t) i] == 0)
                for (auto k = state.graphNextStart[(std::size_t) i]; k < state.graphNextStart[(std::size_t) i + 1]; ++k)
                    if (liveInA[(std::size_t) state.graphNext[(std::size_t) k]] != 0)
                    {
                        liveInA[(std::size_t) i] = 1;
                        changed = true;
                        break;
                    }
    }
    // the units that read a live value (automated / automatedLevel / applyPluginParam): a channel's volume, pan and
    // levels adjustment; an insert's fader, pan, effect parameters and mix, the routes into it; plugin bindings. Round B
    // reads them after round A. A round-A unit reads them right: its own (it writes piece k+1's value after its piece k,
    // then works on piece k+1) and an upstream source's (that one rendered every piece before the unit starts); any
    // other source may not have written the value yet.
    std::array<char, graphCapacity> seen {};
    std::array<int, graphCapacity> stack {};
    const auto reaches = [&] (int from, int to) noexcept {
        std::fill (seen.begin(), seen.begin() + tasks, 0);
        int depth = 0;
        stack[(std::size_t) depth++] = from;
        seen[(std::size_t) from] = 1;
        while (depth > 0)
        {
            const auto at = stack[(std::size_t) --depth];
            if (at == to)
                return true;
            for (auto k = state.graphNextStart[(std::size_t) at]; k < state.graphNextStart[(std::size_t) at + 1]; ++k)
                if (const auto j = state.graphNext[(std::size_t) k]; seen[(std::size_t) j] == 0)
                {
                    seen[(std::size_t) j] = 1;
                    stack[(std::size_t) depth++] = j;
                }
        }
        return false;
    };
    const auto readOk = [&] (int node, int a) noexcept {
        if (! isLive (a) || liveInA[(std::size_t) node] == 0)
            return true;
        const auto source = sourceOf (a);
        return source == node || reaches (source, node);
    };
    const auto lateRead = [&] (int node) noexcept {
        return no (node < channels ? "a round-A channel reads a live value its source may not have written yet"
                                   : "a round-A insert reads a live value its source may not have written yet");
    };
    for (int c = 0; c < channels; ++c)
    {
        const auto at = (std::size_t) c;
        if ((at < state.channelVolumeAuto.size() && ! readOk (c, state.channelVolumeAuto[at]))
            || (at < state.channelPanAuto.size() && ! readOk (c, state.channelPanAuto[at]))
            || (at < state.channelAdjustAuto.size() && (! readOk (c, state.channelAdjustAuto[at][0]) || ! readOk (c, state.channelAdjustAuto[at][1]))))
            return lateRead (c);
    }
    for (int t = 1; t < tracks; ++t)
    {
        const auto at = (std::size_t) t;
        const auto node = trackTask (t);
        auto ok = (at >= state.mixerVolumeAuto.size() || readOk (node, state.mixerVolumeAuto[at]))
                  && (at >= state.mixerPanAuto.size() || readOk (node, state.mixerPanAuto[at]));
        if (at < state.effectParamAuto.size())
            for (const auto& slot : state.effectParamAuto[at])
                for (const auto a : slot)
                    ok = ok && readOk (node, a);
        if (at < state.effectMixAuto.size())
            for (const auto a : state.effectMixAuto[at])
                ok = ok && readOk (node, a);
        if (at < state.mixerInputs.size())
            for (const auto& in : state.mixerInputs[at])
                ok = ok && readOk (node, in.automation);
        if (! ok)
            return lateRead (node);
    }
    for (const auto& b : state.pluginParams)
    {
        if (! isLive (b.automation))
            continue;
        if (b.channel >= 0)
        {
            if (midictl::isPitchBend (b.param)) // (applyLiveControl's bend: kept piece by piece)
                return no ("a live pitch bend");
            if (b.channel < channels && ! readOk (b.channel, b.automation))
                return lateRead (b.channel);
        }
        else if (b.track > 0 && b.track < tracks && ! readOk (trackTask (b.track), b.automation))
            return lateRead (trackTask (b.track));
    }

    // the rounds' roots, pending counts and positions (round B's units count only their round-B predecessors; a fused
    // chain from round A into round B starts round B)
    liveRootCountA = liveRootCountB = liveCountA = liveCountB = 0;
    for (int i = 0; i < tasks; ++i)
        livePendingB[(std::size_t) i] = state.graphIndegree[(std::size_t) i];
    for (int i = 0; i < tasks; ++i)
        if (liveInA[(std::size_t) i] != 0)
            for (auto k = state.graphNextStart[(std::size_t) i]; k < state.graphNextStart[(std::size_t) i + 1]; ++k)
                if (const auto j = state.graphNext[(std::size_t) k]; liveInA[(std::size_t) j] == 0)
                    --livePendingB[(std::size_t) j];
    for (int i = 0; i < tasks; ++i)
    {
        const auto inA = liveInA[(std::size_t) i] != 0;
        const auto fused = state.graphFused[(std::size_t) i];
        const auto fusedSameRound = fused >= 0 && (liveInA[(std::size_t) fused] != 0) == inA;
        if (inA)
        {
            if (state.graphIndegree[(std::size_t) i] == 0)
                liveRootsA[(std::size_t) liveRootCountA++] = i;
            liveCountA += fusedSameRound ? 0 : 1;
        }
        else
        {
            if (livePendingB[(std::size_t) i] == 0)
                liveRootsB[(std::size_t) liveRootCountB++] = i;
            liveCountB += fusedSameRound ? 0 : 1;
        }
    }
    // (each position is one unit handed out: the nodes of the round minus those run inline by their fused predecessor)
    livePlanWhy = liveCountA > 0 ? "" : "no round A";
    return liveCountA > 0;
}

// a controller's value for piece `slot`, as that piece's updateAutomation would have read it (unless the control's own
// value plays there: clearOverriddenValues left NaN)
void RackRenderer::writeLiveValue (const RackState& state, int automation, int slot) noexcept
{
    auto& value = automationValues.v[(std::size_t) slot][(std::size_t) automation];
    if (std::isnan (value))
        return;
    const auto& a = state.song.automations[(std::size_t) automation];
    const auto& src = a.source;
    value = src.channel >= 0 ? a.mapLive (state.instruments[(std::size_t) src.channel]->controlOutput (src.output))
                             : a.mapLive (state.effects[(std::size_t) src.track][(std::size_t) src.slot]->controlOutput (src.output));
}

bool RackRenderer::batchable (const RackState& state, int numSamples) noexcept
{
    const auto refuse = [this] (BatchRefusal why) noexcept {
        ++batchRefused[(std::size_t) why];
        return false;
    };
    liveBatch = false;
    if (WorkerPool::tuning().blockRounds.load (std::memory_order_relaxed) == 0)
        return refuse (BatchRefusal::switchedOff);
    if (workers == nullptr || workers->workers() == 0 || split != nullptr || ownTimeline != nullptr || buffers == nullptr || ! taskGraphEnabled() || ! state.graphOk)
        return refuse (BatchRefusal::noGraph);
    if (! flsettings::options().multithreadGenerators.load (std::memory_order_relaxed) || ! flsettings::options().multithreadMixer.load (std::memory_order_relaxed))
        return refuse (BatchRefusal::noGraph);
    if (numSamples > chunkCapacity || numSamples > buffers->frames || numSamples > (int) zeroBlock.size())
        return refuse (BatchRefusal::bigBlock);
    if (! state.liveAutomations.empty() && WorkerPool::tuning().liveRounds.load (std::memory_order_relaxed) == 0)
        return refuse (BatchRefusal::liveControllers); // (round 190lv: LYNX_POOL live=0, piece by piece as before)
    if (state.hasMidiOutputs || ! state.portEventChannels.empty() || ! state.dashboardChannels.empty() || ! state.vfxChannels.empty()
        || state.hasEffectMidiRoutes || ! state.hardwareMidiChannels.empty())
        return refuse (BatchRefusal::midiBetweenUnits);
    // (rollup 0.1.30 step 9: a take records a plugin's own notes per chunk (round 190ks, GitHub #198): ownNotesBefore /
    // AfterChunk run in process()'s per-piece loop only)
    if (ownAnyOn || ownWantedAny.load (std::memory_order_acquire))
        return refuse (BatchRefusal::midiBetweenUnits);
    if (state.anyBypassWhenIdle)
        return refuse (BatchRefusal::other);
    if (state.launchers || launchersRunning || runningLauncher != nullptr || ! state.performanceZone.empty() || perfRunning
        || (perfLive != nullptr && perfLive->anyPlaying.load (std::memory_order_relaxed)))
        return refuse (BatchRefusal::launchers);
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) state.song.channels.size(), maxChannels });
    const auto tracks = std::max<int> (1, (int) state.song.mixer.size());
    const auto tasks = channels + tracks - 1;
    if (state.graphChannels != channels || tasks <= 0 || tasks > graphCapacity || (int) state.graphIndegree.size() != tasks)
        return refuse (BatchRefusal::noGraph);
    for (int c = 0; c < channels; ++c) // (runTaskGraph's check: a generator's extra outputs go to inserts known only now)
        if (state.instruments[(std::size_t) c] != nullptr && state.instruments[(std::size_t) c]->auxOutputCount() > 0)
            return refuse (BatchRefusal::noGraph);
    if (&state != unitParamsState || state.serial != unitParamsSerial)
    {
        unitParamsOk = buildUnitParams (state);
        livePlanOk = unitParamsOk && ! state.liveAutomations.empty() && buildLivePlan (state); // (round 190lv)
        unitParamsState = &state;
        unitParamsSerial = state.serial;
    }
    if (! unitParamsOk)
        return refuse (BatchRefusal::bindings);
    if (unitParamsHeld)
        return refuse (BatchRefusal::heldNotes);
    // (an editor previewing a slot's settings: they are not the slot's own, which buildUnitParams judged)
    if (overrideParams != nullptr && overrideTrack >= 0 && (std::size_t) overrideTrack < state.effects.size() && overrideSlot >= 0
        && (std::size_t) overrideSlot < state.effects[(std::size_t) overrideTrack].size()
        && state.effects[(std::size_t) overrideTrack][(std::size_t) overrideSlot] != nullptr
        && state.effects[(std::size_t) overrideTrack][(std::size_t) overrideSlot]->followsHeldNotes())
        return refuse (BatchRefusal::heldNotes);
    if (unitParamsTimed && sampleAccurateAutomation())
        return refuse (BatchRefusal::timedParams);
    // round 190lv: live controllers, in two rounds where buildLivePlan proved that exact
    if (! state.liveAutomations.empty())
    {
        if (! livePlanOk)
            return refuse (BatchRefusal::liveControllers);
        liveBatch = true;
    }
    return true;
}

bool RackRenderer::planPieces (const RackState& state, bool playing, double startTick, int numSamples, double tps, int chunk, bool grid) noexcept
{
    // (process()'s loop, without rendering: the same pieces, the same moves of pieceStart; undone when not batched)
    constexpr int piece = 128, exportBlock = 512;
    const auto pieceStartBefore = pieceStart;
    pieceCount = 0;
    for (int done = 0; done < numSamples;)
    {
        if (pieceCount == maxPieces)
        {
            pieceStart = pieceStartBefore;
            pieceCount = -1; // (counted as a refusal by process())
            return false;
        }
        auto n = std::min (chunk, numSamples - done);
        auto continuing = false;
        const auto pos = gridSample + done;
        if (grid)
        {
            const auto blockEnd = (pos / exportBlock + 1) * exportBlock;
            auto pieceEnd = std::min<std::int64_t> (pieceStart + piece, blockEnd);
            if (pos >= pieceEnd)
            {
                pieceStart = pos;
                pieceEnd = std::min<std::int64_t> (pos + piece, blockEnd);
            }
            continuing = pos > pieceStart;
            n = (int) std::min<std::int64_t> (std::min (blockCapacity, numSamples - done), pieceEnd - pos);
        }
        auto atEvent = false;
        if (playing && ! state.eventLanes.empty() && tps > 0.0)
        {
            const auto ahead = ticksToNextEvent (state, startTick + done * tps);
            if (ahead < (double) n * tps)
            {
                n = std::max (1, (int) std::ceil (ahead / tps - 1.0e-9));
                atEvent = true;
            }
        }
        auto& p = pieces[(std::size_t) pieceCount++];
        p = Piece {};
        p.samples = n;
        p.offset = done;
        p.startTick = startTick + done * tps;
        p.continuing = continuing;
        p.gridPos = grid ? pos : -1;
        done += n;
        if (grid && atEvent)
            pieceStart = pos + n;
    }
    if (pieceCount < 2) // (one piece: one round either way)
    {
        pieceStart = pieceStartBefore;
        pieceCount = 0;
        return false;
    }
    // (round 190lv: with live controllers a block costs two rounds: two pieces cost no more piece by piece, where A7 can
    // still keep a light piece on this thread — measured at 256 samples, 1.26 rounds per block against 1.59)
    if (liveBatch && pieceCount < 3)
    {
        pieceStart = pieceStartBefore;
        pieceCount = 0;
        return false;
    }
    // (round 190lv: a later piece that goes on from the one before skips updateAutomation, it would keep the placeholder)
    if (liveBatch)
        for (int k = 1; k < pieceCount; ++k)
            if (pieces[(std::size_t) k].continuing)
            {
                pieceStart = pieceStartBefore;
                pieceCount = -2; // (counted as a refusal by process())
                return false;
            }
    return true;
}

void RackRenderer::carryPieceState (int from, int to) noexcept
{
    trackUnwritten.carry (from, to);
    trackSamples.carry (from, to);
    automationValues.carry (from, to);
    trackActive.carry (from, to);
    tapActive.carry (from, to);
    channelSilent.carry (from, to);
    chunkStartTick.carry (from, to);
    chunkTps.carry (from, to);
    clipEnvTableShown.carry (from, to);
    liveInputOffset.carry (from, to);
    chunkTick.carry (from, to);
    chunkPlaying.carry (from, to);
    pdcBypassNow.carry (from, to);
    compensateTick.carry (from, to);
    compensateTicksPerSample.carry (from, to);
}

void RackRenderer::processBatched (const RackState& state, bool playing, int numSamples, float* left, float* right, MeterBank* meters) noexcept
{
    const auto chunkStartNs = ProcessTimes::now();
    auto& shared = hostTimeline(); // (this thread's play head: the shared one, batchable wants no own one)
    ++batchedBlocks;

    // 1) the pieces' serial parts, in order, each in its slot
    int eventCount = 0;
    for (int k = 0; k < pieceCount; ++k)
    {
        auto& piece = pieces[(std::size_t) k];
        const ScopedPiece slot (k);
        if (k > 0)
            carryPieceState (k - 1, k);
        copyTimeline (k == 0 ? shared : pieceTimelines[(std::size_t) k - 1], pieceTimelines[(std::size_t) k]);
        const ScopedHostTimeline timelineScope (&pieceTimelines[(std::size_t) k]);
        pieceOffset = piece.offset;
        liveInputOffset = piece.offset;
        timedGridPos = piece.gridPos;
        deferParams = true;
        deferLive = liveBatch && k > 0;
        chunkPrelude (state, playing, piece.startTick, piece.samples, piece.continuing);
        deferLive = false;
        deferParams = false;
        eventCount += events.size();
        piece.live = liveInput != nullptr && liveInput->numChannels > 0;
        piece.recording = piece.live && liveInput->recording.load (std::memory_order_relaxed);
        resumeAudio = false;
        chaseNotes = false;
    }

    // 2) the units: A7's choice for the block (its pieces' average size, all their events), then one round, or each
    // piece on this thread as before
    auto* const pool = chunkSchedule (state, std::max (1, numSamples / pieceCount), eventCount);
    auto* const chosen = workers;
    if (unitParamsBend && ! automatedBendReset) // (applyPluginParam's first bend, made here: tasks run it at once)
    {
        automatedBend.fill (std::numeric_limits<float>::quiet_NaN());
        automatedBendReset = true;
    }
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) state.song.channels.size(), maxChannels });
    if (runBatchedGraph (state, playing, channels, meters))
    {
        ++batchedRounds;
        if (liveBatch)
            ++batchedLiveRounds;
        for (int k = 0; k < pieceCount; ++k)
        {
            const auto& piece = pieces[(std::size_t) k];
            const ScopedPiece slot (k);
            const ScopedHostTimeline timelineScope (&pieceTimelines[(std::size_t) k]);
            applyUnitParams (state, maxChannels, piece); // (the Master's own bindings)
            // (runTaskGraph's end: the Master's own channels and input)
            sumChannelsInto (state, 0, channels, piece.samples);
            if (piece.live)
                mixLiveInputTrack (state, 0, piece.recording, playing, piece.samples);
            mixMetronome (state, 0, piece.samples);
            chunkMaster (state, piece.samples, left + piece.offset, right + piece.offset, meters, pool, chunkStartNs);
        }
    }
    else
        for (int k = 0; k < pieceCount; ++k)
        {
            const auto& piece = pieces[(std::size_t) k];
            const ScopedPiece slot (k);
            const ScopedHostTimeline timelineScope (&pieceTimelines[(std::size_t) k]);
            if (piece.params)
                applyPluginParams (state, piece.paramsFirst, piece.paramsLive);
            workers = chosen;
            chunkUnits (state, playing, piece.samples, meters);
            chunkMaster (state, piece.samples, left + piece.offset, right + piece.offset, meters, pool, chunkStartNs);
            // (round 190lv: the controllers' values after this piece, read as the next piece's updateAutomation read them)
            if (liveBatch && k + 1 < pieceCount && pieces[(std::size_t) k + 1].liveDeferred)
                for (const auto a : state.liveAutomations)
                    writeLiveValue (state, a, k + 1);
        }
    workers = pool;

    // 3) the last piece's state is the renderer's (slot 0) again, as after the last chunk of the loop
    carryPieceState (pieceCount - 1, 0);
    pieceOffset.v[0] = 0;
    {
        const auto& last = events.more[(std::size_t) pieceCount - 2];
        events.first.clear();
        for (const auto& e : last)
            events.first.add (e);
    }
    copyTimeline (pieceTimelines[(std::size_t) pieceCount - 1], shared);
    // (A7's light-chunk rule compares one chunk's time)
    lastChunkNs = (ProcessTimes::now() - chunkStartNs) / (std::uint64_t) pieceCount;
}

bool RackRenderer::runBatchedGraph (const RackState& state, bool playing, int channels, MeterBank* meters) noexcept
{
    // (runTaskGraph's conditions; batchable checked the rest for the whole block)
    if (workers == nullptr || ! parallelChunk)
        return false;

    for (int k = 0; k < pieceCount; ++k)
    {
        const ScopedPiece slot (k);
        trackActive.fill (0);
        tapActive.fill (0);
    }
    if (! liveBatch)
        return runGraphRound (state, playing, channels, meters, 0);
    // round 190lv: the controllers' sources and what feeds them, then the rest (each unit still renders all its pieces
    // after every unit it depends on: the graph's order, cut in two)
    runGraphRound (state, playing, channels, meters, 1);
    if (liveCountB > 0)
        runGraphRound (state, playing, channels, meters, 2);
    return true;
}

// one round of the batched task graph: round 0 = every unit, 1 = round A of a live block (liveInA), 2 = its round B
bool RackRenderer::runGraphRound (const RackState& state, bool playing, int channels, MeterBank* meters, int round) noexcept
{
    const auto tracks = std::max<int> (1, (int) state.song.mixer.size());
    const auto tasks = channels + tracks - 1;
    const auto liveA = round == 1;
    for (int i = 0; i < tasks; ++i)
    {
        graphPending[(std::size_t) i].store (round == 2 ? livePendingB[(std::size_t) i] : state.graphIndegree[(std::size_t) i], std::memory_order_relaxed);
        graphSlots[(std::size_t) i].store (0, std::memory_order_relaxed);
    }
    auto pushed = 0;
    if (round == 0)
        for (const auto r : state.graphRoots)
            graphSlots[(std::size_t) pushed++].store (r + 1, std::memory_order_relaxed);
    else
        for (int k = 0, n = round == 1 ? liveRootCountA : liveRootCountB; k < n; ++k)
            graphSlots[(std::size_t) pushed++].store ((round == 1 ? liveRootsA : liveRootsB)[(std::size_t) k] + 1, std::memory_order_relaxed);
    graphPushed.store (pushed, std::memory_order_release);

    const auto timing = times->enabled.load (std::memory_order_relaxed);
    constexpr int handedOver = -1;
    const auto waitMode = WorkerPool::tuning().waitMode.load (std::memory_order_relaxed);
    const auto handOffSpin = std::chrono::microseconds (WorkerPool::tuning().waitSpinMicros.load (std::memory_order_relaxed));
    const auto count = (std::uint64_t) pieceCount;
    auto roundSamples = 0; // (round 190of: the audio all the pieces of a unit cover, for the monitor's spikes)
    for (int k = 0; k < pieceCount; ++k)
        roundSamples += pieces[(std::size_t) k].samples;
    auto task = [&] (int position) {
        int id = 0;
        auto& slot = graphSlots[(std::size_t) position];
        if (waitMode == 2)
        {
            if ((id = slot.load (std::memory_order_acquire)) == 0)
            {
                const auto start = std::chrono::steady_clock::now();
                auto handed = false;
                for (int spins = 0; (id = slot.load (std::memory_order_acquire)) == 0; ++spins)
                {
                    WorkerPool::relax();
                    if ((spins & 63) == 63 && std::chrono::steady_clock::now() - start > handOffSpin)
                    {
                        auto expected = 0;
                        if (slot.compare_exchange_strong (expected, handedOver, std::memory_order_acq_rel))
                        {
                            handed = true;
                            break;
                        }
                        id = expected;
                        break;
                    }
                }
                workers->noteWait ((std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - start).count(), handed);
                if (handed)
                    return;
            }
        }
        else
            workers->await (position, [&] { return (id = slot.load (std::memory_order_acquire)) != 0; });
        --id;
        std::array<int, 32> taken;
        int takenCount = 0;
        auto chain = [&] (int first, auto& self) -> void {
            auto at = first;
            for (;;)
            {
                const auto before = timing || measureUnits ? ProcessTimes::now() : 0;
                // the unit's pieces, one after another: piece k with slot k's state, its own bindings for that piece
                for (int k = 0; k < pieceCount; ++k)
                {
                    const auto& piece = pieces[(std::size_t) k];
                    const ScopedPiece pieceScope (k);
                    const ScopedHostTimeline timelineScope (&pieceTimelines[(std::size_t) k]);
                    const auto n = piece.samples;
                    if (at < channels)
                    {
                        applyUnitParams (state, at, piece);
                        renderChannel (state, at, n);
                    }
                    else
                    {
                        const auto t = at - channels + 1;
                        applyUnitParams (state, maxChannels + t, piece);
                        sumChannelsInto (state, t, channels, n);
                        if (piece.live)
                            mixLiveInputTrack (state, t, piece.recording, playing, n);
                        mixMetronome (state, t, n);
                        mixSideInput (state, t, n);
                        processTrack (state, t, n, meters);
                    }
                    // (round 190lv: a controller's source: its values for the next piece, as that piece's
                    // updateAutomation read them after this one)
                    if (liveA && k + 1 < pieceCount && pieces[(std::size_t) k + 1].liveDeferred)
                        for (auto s = liveSourceStart[(std::size_t) at]; s < liveSourceStart[(std::size_t) at + 1]; ++s)
                            writeLiveValue (state, liveSources[(std::size_t) s], k + 1);
                }
                const auto spent = ProcessTimes::now() - before;
                // (A7 keeps one chunk's cost: the pieces' average)
                if (at < channels)
                {
                    if (timing && at < ProcessTimes::channels)
                        times->addCall (true, (std::size_t) at, spent, audioNsOf (roundSamples));
                    if (measureUnits)
                        channelCostNs[(std::size_t) at] = (std::uint32_t) std::min<std::uint64_t> (spent / count, 0xffffffffu);
                }
                else if (const auto t = at - channels + 1; measureUnits && t < maxMixerTracks)
                    trackCostNs[(std::size_t) t] = (std::uint32_t) std::min<std::uint64_t> (spent / count, 0xffffffffu);
                const auto fused = (std::size_t) at < state.graphFused.size() ? state.graphFused[(std::size_t) at] : -1;
                if (fused < 0 || (liveA && liveInA[(std::size_t) fused] == 0)) // (round A: a chain into round B is cut there)
                    break;
                at = fused;
            }
            for (auto k = state.graphNextStart[(std::size_t) at]; k < state.graphNextStart[(std::size_t) at + 1]; ++k)
            {
                const auto j = state.graphNext[(std::size_t) k];
                if (liveA && liveInA[(std::size_t) j] == 0) // (round B's: its pending count left out round A's units)
                    continue;
                if (graphPending[(std::size_t) j].fetch_sub (1, std::memory_order_acq_rel) == 1)
                {
                    const auto pos = graphPushed.fetch_add (1, std::memory_order_acq_rel);
                    auto expected = 0;
                    if (graphSlots[(std::size_t) pos].compare_exchange_strong (expected, j + 1, std::memory_order_acq_rel))
                        workers->notify (pos);
                    else if (takenCount < (int) taken.size())
                        taken[(std::size_t) takenCount++] = j;
                    else
                        self (j, self);
                }
            }
        };
        chain (id, chain);
        while (takenCount > 0)
            chain (taken[(std::size_t) --takenCount], chain);
    };
    workers->parallelFor (round == 0 ? tasks - state.graphFusedCount : round == 1 ? liveCountA : liveCountB, task);
    return true;
}

int RackRenderer::metronomeTarget (const RackState& state) const noexcept
{
    return metronome::targetTrack (clickTrack, std::min ((int) state.song.mixer.size(), maxMixerTracks));
}

// the metronome's click into its track's input (round 165z-a2); `track` is the one being filled, the click goes in
// only when it is the metronome's
void RackRenderer::mixMetronome (const RackState& state, int track, int n) noexcept
{
    if (clickLeft == nullptr || state.song.mixer.empty() || track != metronomeTarget (state))
        return;
    auto* dl = trackBuffer (track, 0);
    auto* dr = trackBuffer (track, 1);
    const auto* a = clickLeft + liveInputOffset;
    const auto* b = clickRight + liveInputOffset;
    for (int i = 0; i < n; ++i)
    {
        dl[i] += a[i];
        dr[i] += b[i];
    }
}

// round 188b: a plugin's side-chain bus into its side track's input (setSideInput); `track` is the one being filled
void RackRenderer::mixSideInput (const RackState& state, int track, int n) noexcept
{
    if (sideLeft == nullptr || track <= 0 || track != sideTrack || track >= std::min ((int) state.song.mixer.size(), maxMixerTracks))
        return;
    auto* dl = trackBuffer (track, 0);
    auto* dr = trackBuffer (track, 1);
    const auto* a = sideLeft + liveInputOffset;
    const auto* b = sideRight + liveInputOffset;
    for (int i = 0; i < n; ++i)
    {
        dl[i] += a[i];
        dr[i] += b[i];
    }
}

void RackRenderer::copyToRenderTap (int t, const float* l, const float* r, int n) noexcept
{
    if (! renderTaps->wants (t) || liveInputOffset < 0 || liveInputOffset + n > renderTaps->capacity)
        return;
    std::memcpy (renderTaps->left (t) + liveInputOffset, l, sizeof (float) * (std::size_t) n);
    std::memcpy (renderTaps->right (t) + liveInputOffset, r, sizeof (float) * (std::size_t) n);
}

void RackRenderer::pullInputs (const RackState& state, int t, int n) noexcept
{
    if ((std::size_t) t >= state.mixerInputs.size())
        return;

    float* dl = nullptr; // (round 190gp: the insert's buffer only once something comes in)
    float* dr = nullptr;
    auto target = [&] {
        if (dl == nullptr)
        {
            dl = trackBuffer (t, 0);
            dr = trackBuffer (t, 1);
        }
    };

    for (const auto& in : state.mixerInputs[(std::size_t) t])
    {
        const auto tap = in.tap >= 0 && in.tap < RackState::maxSendTaps ? in.tap : -1;
        // an automated route level (fader curve, like the knob)
        // (round 190hq: taken as late as the source's audio, Song::compensateMixerLevels)
        const auto routeAutomation = in.automation >= 0 ? automatedLevel (state, in.automation, std::numeric_limits<float>::quiet_NaN(), in.source)
                                                        : std::numeric_limits<float>::quiet_NaN();
        const auto level = std::isnan (routeAutomation) ? in.level : faderGain (routeAutomation);
        // (round 190ge: a source whose block came out all zeros, trackActive 2, adds nothing: its zeros would not change
        // a sum that starts from +0, and its PDC line drains as if it had been fed them)
        const auto active = tap >= 0 ? tapActive[(std::size_t) tap] != 0 : trackActive[(std::size_t) in.source] == 1;
        const auto& delays = state.sendDelays[(std::size_t) in.source];
        auto* delay = (std::size_t) in.delay < delays.size() ? delays[(std::size_t) in.delay].get() : nullptr;
        if (pdcBypassNow && (tap < 0 || in.pre) && state.bypassWhenIdle[(std::size_t) in.source] != 0)
            delay = nullptr; // FL's "When recording or not playing": no compensation now
        const auto* sl = tap >= 0 ? tapBuffer (tap, 0) : trackInput (in.source, 0);
        const auto* sr = tap >= 0 ? tapBuffer (tap, 1) : trackInput (in.source, 1);

        if (delay != nullptr)
        {
            // PDC: this edge is shorter than the slowest one arriving here. A silent source
            // still drains the tail that was in the line.
            target();
            delay->mixInto (active ? sl : nullptr, active ? sr : nullptr, level, dl, dr, n);
            continue;
        }

        if (! active)
            continue;
        target();

        for (int i = 0; i < n; ++i)
        {
            dl[i] += sl[i] * level;
            dr[i] += sr[i] * level;
        }
    }
}

namespace
{
bool isSilentBlock (const float* l, const float* r, int n) noexcept
{
    int i = 0;
#if defined(LYNX_X86)
    // round 190ge: four lanes at a time (each idle insert scans its whole block); != 0 is true for a NaN, as below
    {
        const auto zero = _mm_setzero_ps();
        for (; i + 4 <= n; i += 4)
            if (_mm_movemask_ps (_mm_or_ps (_mm_cmpneq_ps (_mm_loadu_ps (l + i), zero), _mm_cmpneq_ps (_mm_loadu_ps (r + i), zero))) != 0)
                return false;
    }
#endif
    for (; i < n; ++i)
        if (l[i] != 0.0f || r[i] != 0.0f)
            return false;
    return true;
}

// Issue #144: a silenced insert (its mute, a solo elsewhere, a muted VCA) still shows its level, as in FL. Its effects
// run for that only when nothing else can hear them: no slot reaches outside the insert (hardware I/O, controllers that
// move other controls, sends into other inserts are cut below), so what comes out of LYNX stays the same.
bool canMeterSilenced (const MixerTrack& track) noexcept
{
    for (const auto& slot : track.effects)
        switch (slot.kind)
        {
            case EffectKind::hardwareInsert:
            case EffectKind::formulaController:
            case EffectKind::xyController:
            case EffectKind::xyzController:
            case EffectKind::controlSurface:
            case EffectKind::peakController:
                return false;
            case EffectKind::patcher:
                if (patcher::usesHardwareIo (slot.patcher))
                    return false;
                break;
            default:
                break;
        }
    return true;
}
} // namespace

// Issue #144 (FL: a muted insert's meter keeps moving): the silenced insert's effects, EQ and fader without the mute,
// into its meter. The insert stays inactive, so its routes (Master, sends) carry nothing. `cutEffectSends` (projects
// from before round 190y): the sends its effects make (Fruity Send, Patcher, Transmitter...) are cut again and only
// effects that stay inside the insert run (canMeterSilenced). Without it (Song::muteRunsEffects, FL): they are heard.
void RackRenderer::runSilencedTrack (const RackState& state, int t, int n, const MixerMatrix& level, MeterBank* meters, bool cutEffectSends) noexcept
{
    const auto& track = state.song.mixer[(std::size_t) t];
    auto* l = trackBuffer (t, 0);
    auto* r = trackBuffer (t, 1);
    auto& last = lastMeterGain[(std::size_t) t];
    if (meters == nullptr && track.effects.empty()) // (nothing to run, nobody looks)
        return;
    if (track.effects.empty() && (state.song.cleanMixer || ! track.eq.active()) && isSilentBlock (l, r, n))
    {
        last = level;
        return;
    }

    runEffects (state, t, n);
    if (cutEffectSends && (std::size_t) t < state.slotTap.size())
        for (const auto tap : state.slotTap[(std::size_t) t])
            if (tap >= 0 && tap < RackState::maxSendTaps)
                tapActive[(std::size_t) tap] = 0;
    applyTrackEq (track, t, state.song.cleanMixer, l, r, n);
    if (meters == nullptr)
        return;
    last = applyMatrix (l, r, l, r, n, std::isnan (last.ll) ? level : last, level);
    meters->accumulate (t, peakOf (l, n), peakOf (r, n));
    meterPower (meters, t, l, r, n);
}

void RackRenderer::processTrack (const RackState& state, int t, int n, MeterBank* meters) noexcept
{
    // round 190ox: an envelope follower's insert: its level after the fader (silence unless set below)
    const auto followed = (std::size_t) t < state.followedTracks.size() && state.followedTracks[(std::size_t) t] != 0 && t < maxMixerTracks;
    if (followed)
        followPeak[(std::size_t) t] = 0.0f;
    if (foreignTrack (t)) // (render-ahead: the other renderer runs it, and its inputs; the boundary brings its output here)
    {
        importTrack (state, t, n);
        return;
    }
    const auto& track = state.song.mixer[(std::size_t) t];
    pullInputs (state, t, n);

    // recording taps on this insert (round 168x, issue #41): FL's "Record" positions after the input
    const auto taps = liveInput != nullptr && t > 0 ? liveInput->tappedPoints (t) : 0u;
    auto tap = [&] (RecordSource point, const float* l, const float* r) {
        if ((taps & (1u << (unsigned) point)) != 0)
        {
            (void) trackBuffer (t, 0); // (round 190gp: its zeros stored, if it has only those)
            liveInput->tap (t, point, l, r, liveInputOffset, n);
        }
    };

    const auto silent = track.muted || ! state.mixerAudible[(std::size_t) t];
    // issue #144: a silenced insert keeps its meter (FL): metered without being heard, while the meters are on
    // (the LYNX option "Muted inserts don't process": the old cheap way, no effects, no meter)
    const auto skip = silent && state.song.mutedInsertsSkip;
    const auto meterSilenced = ! skip && silent && meters != nullptr && canMeterSilenced (track);
    // FL (round 190y renders): a muted insert keeps running its effects, live and in exports; only its routes are cut
    const auto runSilenced = ! skip && silent && state.song.muteRunsEffects;

    // target matrix; the ramp from the previous block avoids clicks on mute and fader
    MixerMatrix target { 0.0f, 0.0f, 0.0f, 0.0f };
    MixerMatrix level { 0.0f, 0.0f, 0.0f, 0.0f }; // the same without the mute (the "Post level and panning" tap)

    if (! silent || meterSilenced || runSilenced || (taps & (1u << (unsigned) RecordSource::postLevel)) != 0)
    {
        const auto fader = liveLevel (state, &LiveLevels::mixerVolume, t, track.volume);
        const auto knob = liveLevel (state, &LiveLevels::mixerPan, t, track.pan);
        const auto volume = state.hasAutomation ? automatedLevel (state, state.mixerVolumeAuto[(std::size_t) t], fader, t) : fader;
        const auto pan = state.hasAutomation ? automatedLevel (state, state.mixerPanAuto[(std::size_t) t], (knob + 1.0f) * 0.5f, t) * 2.0f - 1.0f : knob;
        level = mixerMatrix (track, volume, pan, state.song.panTriangular, liveLevel (state, &LiveLevels::mixerStereo, t, track.stereo));
        if ((std::size_t) t < state.vcaChains.size() && ! state.vcaChains[(std::size_t) t].empty()) // VCA faders (round 177a)
        {
            auto g = 1.0f;
            for (const auto v : state.vcaChains[(std::size_t) t])
            {
                const auto position = liveLevel (state, &LiveLevels::vcaLevel, v, state.song.vcas[(std::size_t) v].level);
                const auto automatedPosition = state.hasAutomation && (std::size_t) v < state.vcaLevelAuto.size()
                                                   ? automatedLevel (state, state.vcaLevelAuto[(std::size_t) v], position, t)
                                                   : position;
                g *= faderGain (automatedPosition);
            }
            level.ll *= g;
            level.lr *= g;
            level.rl *= g;
            level.rr *= g;
        }
        if (track.crossfade != CrossfadeAssign::none) // round 190kc: Live's crossfader, at the gain stage (a VCA)
        {
            auto position = state.live != nullptr ? state.live->crossfader.load (std::memory_order_relaxed) : state.song.crossfader;
            if (state.hasAutomation && state.crossfaderAutomation >= 0)
                position = crossfader::positionOf (automatedLevel (state, state.crossfaderAutomation, crossfader::valueOf (position), t));
            const auto g = crossfader::gain (state.song.crossfadeCurve, track.crossfade, position);
            level.ll *= g;
            level.lr *= g;
            level.rl *= g;
            level.rr *= g;
        }
        if (! silent)
            target = level;
    }

    auto& last = lastTrackGain[(std::size_t) t];
    const auto from = std::isnan (last.ll) ? target : last;
    constexpr MixerMatrix zero { 0.0f, 0.0f, 0.0f, 0.0f };

    // round 190gp: an insert nothing came into holds zeros it has not stored (trackZeros): what only reads them knows
    // they are zeros, and they are stored (trackBuffer) only before something works on them
    auto* l = rawTrackBuffer (t, 0);
    auto* r = rawTrackBuffer (t, 1);
    auto store = [&] { (void) trackBuffer (t, 0); };

    // pre-fader sends (round 190aq): the insert's signal before its fader goes to its tap, with the mute as a ramp
    const auto preTap = (std::size_t) t < state.preTap.size() ? state.preTap[(std::size_t) t] : -1;
    auto& lastPre = lastPreGain[(std::size_t) t];
    const auto preTarget = silent ? 0.0f : 1.0f;
    const auto preFading = preTap >= 0 && ! std::isnan (lastPre) && lastPre > 0.0f; // (still heard: its mute ramps out)

    if (silent && from == zero && taps == 0 && ! preFading) // (a muted insert being recorded still runs its effects for the take)
    {
        if (runSilenced)
            runSilencedTrack (state, t, n, level, meters, false);
        else if (meterSilenced)
            runSilencedTrack (state, t, n, level, meters, true);
        else
            lastMeterGain[(std::size_t) t] = { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f };
        lastPre = preTarget;
        if (meters != nullptr)
            meters->capture.pushSilence (t, n);
        return; // stays inactive: nobody pulls anything from it
    }
    lastMeterGain[(std::size_t) t] = { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f };

    tap (RecordSource::externalAndMixer, l, r);

    // an idle insert (round 166d, up to 500 of them): no effects, no built-in EQ and nothing came in this block. Its
    // output is silence whatever the fader does, so it stays inactive and the tracks it feeds skip it (the same
    // samples: adding zeros changes nothing; a tap stays silent too)
    if (track.effects.empty() && (state.song.cleanMixer || ! track.eq.active()) && (trackZeros (t) || isSilentBlock (l, r, n)))
    {
        if (t < (int) trackEqs.size() && trackEqs[(std::size_t) t].running)
            trackEqs[(std::size_t) t] = {};
        last = target;
        lastPre = preTarget;
        if (meters != nullptr)
            meters->capture.pushSilence (t, n);
        return;
    }

    runEffects (state, t, n);
    tap (RecordSource::postEffects, l, r);
    if (! state.song.cleanMixer && track.eq.active())
        store();
    applyTrackEq (track, t, state.song.cleanMixer, l, r, n);
    tap (RecordSource::postEq, l, r);
    if ((taps & (1u << (unsigned) RecordSource::postLevel)) != 0)
        if (const auto dest = liveInput->tapDestination (t, RecordSource::postLevel, liveInputOffset, n); dest.first != nullptr)
        {
            store();
            // the fader, pan and separation without the mute, with their own ramp
            auto& lastLevel = lastTapGain[(std::size_t) t];
            const auto fromLevel = std::isnan (lastLevel.ll) ? level : lastLevel;
            lastLevel = applyMatrix (l, r, dest.first, dest.second, n, fromLevel, level);
        }
    if (preTap >= 0 && preTap < RackState::maxSendTaps)
    {
        auto g = std::isnan (lastPre) ? preTarget : lastPre;
        if (g > 0.0f || preTarget > 0.0f)
        {
            store();
            auto* pl = tapBuffer (preTap, 0);
            auto* pr = tapBuffer (preTap, 1);
            constexpr float step = 0.001f; // (the fader ramp's step, applyMatrix)
            for (int i = 0; i < n; ++i)
            {
                g = preTarget > g ? std::min (preTarget, g + step) : std::max (preTarget, g - step);
                pl[i] = l[i] * g;
                pr[i] = r[i] * g;
            }
            tapActive[(std::size_t) preTap] = 1;
        }
        lastPre = g;
    }
    else
        lastPre = preTarget;
    // round 190ge: an insert whose effects left only zeros (smart-disabled after the stop, a gate closed...): its fader
    // on silence, with no ramp running and gains that cannot flip a zero's sign, would write the same zeros back, so it
    // is not run; and the inserts it feeds skip it (trackActive 2). An idle project's mixer used to scale, delay and
    // sum blocks of zeros on every insert with effects (most of its CPU once the effects were smart-disabled)
    const auto silentOut = trackZeros (t) || isSilentBlock (l, r, n);
    if (silentOut && from == target && target.rl == 0.0f && target.lr == 0.0f && ! std::signbit (target.ll) && ! std::signbit (target.rr))
        last = target;
    else
    {
        store();
        last = applyMatrix (l, r, l, r, n, from, target);
    }
    tap (RecordSource::postTrack, l, r);
    if (followed && ! trackZeros (t))
        followPeak[(std::size_t) t] = std::max (peakOf (l, n), peakOf (r, n));

    trackActive[(std::size_t) t] = silentOut ? 2 : 1;
    // round 190gj: the single-pass stems' tap (what this insert sends on). Rollup 0.1.28: through trackInput, as 190gp's
    // readers do: a silent insert may end here with zeros it never stored (trackZeros), its buffer then still holding
    // an older block
    if (renderTaps != nullptr)
        copyToRenderTap (t, trackInput (t, 0), trackInput (t, 1), n);

    if (meters != nullptr)
    {
        if (trackZeros (t)) // (round 190gp: its zeros, never stored: the readers read them as zeros)
        {
            meters->accumulate (t, 0.0f, 0.0f);
            meters->capture.push (t, trackInput (t, 0), trackInput (t, 1), n);
        }
        else
        {
            meters->accumulate (t, peakOf (l, n), peakOf (r, n));
            meterPower (meters, t, l, r, n);
            meters->capture.push (t, l, r, n);
        }
    }

    // FL's Current (C) track (round 190mv, DF-82; capture 190mv A1): the selected track's output, after its own meter,
    // goes through the Current track's effects and EQ before the tracks it feeds pull it
    if (state.currentTrack > 0 && t == currentSource.load (std::memory_order_relaxed) && t != state.currentTrack)
    {
        auto* cl = trackBuffer (t, 0);
        auto* cr = trackBuffer (t, 1);
        runCurrentTrack (state, cl, cr, n, meters);
        trackActive[(std::size_t) t] = isSilentBlock (cl, cr, n) ? 2 : 1;
    }
}

void RackRenderer::runCurrentTrack (const RackState& state, float* l, float* r, int n, MeterBank* meters) noexcept
{
    const auto c = state.currentTrack;
    if (c <= 0 || c >= (int) state.song.mixer.size() || foreignTrack (c))
        return;
    const auto& track = state.song.mixer[(std::size_t) c];
    if (track.effects.empty() && (state.song.cleanMixer || ! track.eq.active()))
    {
        if (meters != nullptr) // (nothing on it: its meter shows the selected track's output)
        {
            meters->accumulate (c, peakOf (l, n), peakOf (r, n));
            meterPower (meters, c, l, r, n);
        }
        return;
    }
    auto* cl = trackBuffer (c, 0);
    auto* cr = trackBuffer (c, 1);
    std::memcpy (cl, l, sizeof (float) * (std::size_t) n);
    std::memcpy (cr, r, sizeof (float) * (std::size_t) n);
    runEffects (state, c, n);
    applyTrackEq (track, c, state.song.cleanMixer, cl, cr, n);
    std::memcpy (l, cl, sizeof (float) * (std::size_t) n);
    std::memcpy (r, cr, sizeof (float) * (std::size_t) n);
    if (meters != nullptr)
    {
        meters->accumulate (c, peakOf (cl, n), peakOf (cr, n));
        meterPower (meters, c, cl, cr, n);
    }
}
// ---- render-ahead (round 190gq, core/RenderAhead.h) ----------------------------------------------------------------

void RackRenderer::resetNoteTools (const RackState& state) noexcept
{
    for (std::size_t c = 0; c < state.arpeggiators.size(); ++c)
        if (state.arpeggiators[c] != nullptr && ! foreignChannel ((int) c))
            state.arpeggiators[c]->reset();
    for (std::size_t c = 0; c < state.echoes.size(); ++c)
        if (state.echoes[c] != nullptr && ! foreignChannel ((int) c))
            state.echoes[c]->reset();
}

int RackRenderer::splitRow() noexcept
{
    if (splitBlock == nullptr || splitChunk >= SplitBlock::maxChunks || splitChunk >= splitBlock->chunks
        || splitBlock->chunkOffset[(std::size_t) splitChunk] != liveInputOffset)
        return -1;
    return splitChunk;
}

void RackRenderer::exportSplit (int n) noexcept
{
    auto& b = *splitBlock;
    if (splitChunk < SplitBlock::maxChunks)
    {
        b.chunkOffset[(std::size_t) splitChunk] = liveInputOffset;
        b.chunks = splitChunk + 1;
    }
    const auto row = std::min (splitChunk, SplitBlock::maxChunks - 1);
    for (int s = 0; s < split->slots; ++s)
    {
        const auto u = split->slotUnit[(std::size_t) s];
        std::uint8_t flag = 0;
        const float* l = nullptr;
        const float* r = nullptr;
        switch (split->slotKind[(std::size_t) s])
        {
            case Split::Kind::channel:
                flag = (std::uint8_t) channelSilent[(std::size_t) u];
                if (flag == 0)
                    l = channelBuffer (u, 0), r = channelBuffer (u, 1);
                break;
            case Split::Kind::track:
                flag = (std::uint8_t) trackActive[(std::size_t) u];
                if (flag != 0)
                    l = trackBuffer (u, 0), r = trackBuffer (u, 1);
                break;
            case Split::Kind::tap:
                flag = (std::uint8_t) tapActive[(std::size_t) u];
                if (flag != 0)
                    l = tapBuffer (u, 0), r = tapBuffer (u, 1);
                break;
        }
        b.flags[(std::size_t) row * Split::maxSlots + (std::size_t) s] = flag;
        // (the samples are always written, zeros for a silent unit: a chunk that does not line up reads them as they are)
        auto* dl = b.audio + ((std::size_t) s * 2) * (std::size_t) b.stride + (std::size_t) liveInputOffset;
        auto* dr = dl + b.stride;
        if (l != nullptr)
        {
            std::memcpy (dl, l, sizeof (float) * (std::size_t) n);
            std::memcpy (dr, r, sizeof (float) * (std::size_t) n);
        }
        else
        {
            std::memset (dl, 0, sizeof (float) * (std::size_t) n);
            std::memset (dr, 0, sizeof (float) * (std::size_t) n);
        }
    }
}

void RackRenderer::importChannel (int c, int n) noexcept
{
    const auto slot = split->channelSlot[(std::size_t) c];
    if (split->exports || slot < 0 || splitBlock == nullptr)
    {
        channelSilent[(std::size_t) c] = 1;
        return;
    }
    const auto row = splitRow();
    if (row < 0)
        ++splitMismatches;
    const auto flag = row >= 0 ? splitBlock->flags[(std::size_t) row * Split::maxSlots + (std::size_t) slot] : (std::uint8_t) 0;
    channelSilent[(std::size_t) c] = (char) flag;
    if (flag != 0)
        return;
    const auto* sl = splitBlock->audio + ((std::size_t) slot * 2) * (std::size_t) splitBlock->stride + (std::size_t) liveInputOffset;
    std::memcpy (channelBuffer (c, 0), sl, sizeof (float) * (std::size_t) n);
    std::memcpy (channelBuffer (c, 1), sl + splitBlock->stride, sizeof (float) * (std::size_t) n);
}

void RackRenderer::importTrack (const RackState& state, int t, int n) noexcept
{
    const auto row = split->exports || splitBlock == nullptr ? -2 : splitRow();
    if (row == -1)
        ++splitMismatches;
    auto take = [&] (int slot, float* l, float* r) -> std::uint8_t {
        if (slot < 0 || row == -2)
            return 0;
        const auto flag = row >= 0 ? splitBlock->flags[(std::size_t) row * Split::maxSlots + (std::size_t) slot] : (std::uint8_t) 1;
        if (flag != 0)
        {
            const auto* sl = splitBlock->audio + ((std::size_t) slot * 2) * (std::size_t) splitBlock->stride + (std::size_t) liveInputOffset;
            std::memcpy (l, sl, sizeof (float) * (std::size_t) n);
            std::memcpy (r, sl + splitBlock->stride, sizeof (float) * (std::size_t) n);
        }
        return flag;
    };
    trackActive[(std::size_t) t] = (char) take (split->trackSlot[(std::size_t) t], trackBuffer (t, 0), trackBuffer (t, 1));
    // its send taps (Fruity Send and the other slot sends, the pre-fader send)
    if ((std::size_t) t < state.slotTap.size())
        for (const auto tap : state.slotTap[(std::size_t) t])
            if (tap >= 0 && tap < RackState::maxSendTaps)
                tapActive[(std::size_t) tap] = (char) take (split->tapSlot[(std::size_t) tap], tapBuffer (tap, 0), tapBuffer (tap, 1));
    if (const auto pre = (std::size_t) t < state.preTap.size() ? state.preTap[(std::size_t) t] : -1; pre >= 0 && pre < RackState::maxSendTaps)
        tapActive[(std::size_t) pre] = (char) take (split->tapSlot[(std::size_t) pre], tapBuffer (pre, 0), tapBuffer (pre, 1));
}

void RackRenderer::shareTimesWith (RackRenderer& other) noexcept
{
    units = other.units;
    times = other.times;
    countsAudio = false;
}

void RackRenderer::adoptGlobals (const RackRenderer& from) noexcept
{
    bpm = from.bpm;
    wasPlaying = from.wasPlaying;
    expectedTick = from.expectedTick;
    resumeAudio = from.resumeAudio;
    chaseNotes = from.chaseNotes;
    playTruncatedOnTransport = from.playTruncatedOnTransport;
    resetPluginsOnTransport = from.resetPluginsOnTransport;
    userJumpSerial = from.userJumpSerial;
    seenUserJumpSerial = from.seenUserJumpSerial;
    lastState = from.lastState;
    lastSerial = from.lastSerial;
    automationValues = from.automationValues;
    automatedBend = from.automatedBend;
    automatedBendReset = from.automatedBendReset;
    for (std::size_t w = 0; w < overridden.size(); ++w)
    {
        overridden[w].store (from.overridden[w].load (std::memory_order_relaxed), std::memory_order_relaxed);
        released[w].store (from.released[w].load (std::memory_order_relaxed), std::memory_order_relaxed);
    }
    anyOverridden.store (from.anyOverridden.load (std::memory_order_relaxed), std::memory_order_relaxed);
    anyReleased.store (from.anyReleased.load (std::memory_order_relaxed), std::memory_order_relaxed);
    keepReleasedOnPlay.store (from.keepReleasedOnPlay.load (std::memory_order_relaxed), std::memory_order_relaxed);
    releaseWasPlaying = from.releaseWasPlaying;
    modSeconds = from.modSeconds;
    compensateTick = from.compensateTick;
    compensateTicksPerSample = from.compensateTicksPerSample;
    chunkStartTick = from.chunkStartTick;
    chunkTps = from.chunkTps;
    liveTick = from.liveTick;
    layerTurn = from.layerTurn;
    layerRandom = from.layerRandom;
    chunkTick = from.chunkTick;
    chunkPlaying = from.chunkPlaying;
    pdcBypassNow = from.pdcBypassNow;
    pdcBypassBefore = from.pdcBypassBefore;
    *scheduledNotes = *from.scheduledNotes;
    liveRoutedCount = 0;
    // Round 190jb: the automation's grid (round 190gh: pieces of 128 inside blocks of 512 counted from where playback
    // started). Without it the part rendered ahead took it as a jump when it engaged in the middle of a song (its load
    // probe under load) and started a grid of its own there: an automated fader then moved at other samples than in the
    // export (the rare export_parity miss "first apart at 3.56 s", -70 dBFS, in 190hn / 190ip)
    gridSample = from.gridSample;
    pieceStart = from.pieceStart;
    gridNextTick = from.gridNextTick;
}

void RackRenderer::adoptAutomationValues (const RackRenderer& from, const int* automations, int count) noexcept
{
    if (automations == nullptr)
    {
        automationValues = from.automationValues;
        return;
    }
    for (int i = 0; i < count; ++i)
        if (const auto a = automations[i]; a >= 0 && a < RackState::maxAutomations)
            automationValues[(std::size_t) a] = from.automationValues[(std::size_t) a];
}

void RackRenderer::adoptUnits (const RackRenderer& from, const char* channels, const char* tracks) noexcept
{
    if (channels != nullptr)
        for (std::size_t c = 0; c < (std::size_t) maxChannels; ++c)
        {
            if (channels[c] == 0)
                continue;
            heldNotes[c] = from.heldNotes[c];
            struckNotes[c] = from.struckNotes[c];
            voiceOrders[c] = from.voiceOrders[c];
            automatedBend[c] = from.automatedBend[c];
            layerTurn[c] = from.layerTurn[c];
            if (c < polyLayers.size() && c < from.polyLayers.size())
                polyLayers[c] = from.polyLayers[c];
            slideTicks[c] = from.slideTicks[c]; // (issue #236: a slide in progress goes on where the channel renders now)
            if (auto* mine = propsOf (c), * theirs = from.propsOf (c); mine != nullptr && theirs != nullptr)
                *mine = *theirs; // (the slide in progress and the keys' properties)
            if ((c + 1) * 128 <= layerPick.size() && (c + 1) * 128 <= from.layerPick.size())
                std::copy (from.layerPick.begin() + (std::ptrdiff_t) (c * 128), from.layerPick.begin() + (std::ptrdiff_t) ((c + 1) * 128),
                           layerPick.begin() + (std::ptrdiff_t) (c * 128));
        }
    if (tracks != nullptr)
        for (std::size_t t = 0; t < (std::size_t) maxMixerTracks; ++t)
        {
            if (tracks[t] == 0)
                continue;
            lastTrackGain[t] = from.lastTrackGain[t];
            lastTapGain[t] = from.lastTapGain[t];
            lastMeterGain[t] = from.lastMeterGain[t];
            lastPreGain[t] = from.lastPreGain[t];
            if (t < trackEqs.size() && t < from.trackEqs.size())
                trackEqs[t] = from.trackEqs[t];
            for (std::size_t s = 0; s < (std::size_t) maxEffectSlots; ++s)
            {
                const auto key = t * maxEffectSlots + s;
                if (key < smartDisable.size() && key < from.smartDisable.size())
                    smartDisable[key] = from.smartDisable[key];
                if (key < goalGlideAmount.size() && key < from.goalGlideAmount.size())
                    goalGlideAmount[key] = from.goalGlideAmount[key];
                if (key < goalGlideEffect.size() && key < from.goalGlideEffect.size())
                    goalGlideEffect[key] = from.goalGlideEffect[key];
                if (key < lowLatencySkipped.size() && key < from.lowLatencySkipped.size())
                    lowLatencySkipped[key] = from.lowLatencySkipped[key];
            }
        }
}

void RackRenderer::silenceChannels (const RackState& state, const char* channels) noexcept
{
    for (std::size_t c = 0; c < state.instruments.size() && c < (std::size_t) maxChannels; ++c)
    {
        if (channels[c] == 0 || foreignChannel ((int) c))
            continue;
        if (state.instruments[c] != nullptr)
            state.instruments[c]->allNotesOff();
        heldNotes[c] = {};
        struckNotes[c] = {};
        voiceOrders[c].clear();
        if (c < polyLayers.size())
            polyLayers[c].clear();
        if (auto* block = propsOf (c))
            block->slide.count = 0; // (issue #236)
        if ((c + 1) * 128 <= layerPick.size())
            std::fill (layerPick.begin() + (std::ptrdiff_t) (c * 128), layerPick.begin() + (std::ptrdiff_t) ((c + 1) * 128), (std::int16_t) -1);
        if (c < state.arpeggiators.size() && state.arpeggiators[c] != nullptr)
            state.arpeggiators[c]->reset();
        if (c < state.echoes.size() && state.echoes[c] != nullptr)
            state.echoes[c]->reset();
    }
}
} // namespace lynx
