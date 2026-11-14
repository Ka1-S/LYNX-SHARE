#include "core/RackRenderer.h"
#include "core/Rack.h"
#include "core/MixerOps.h"
#include "core/FlSettings.h"

#include "core/FpcInstrument.h"
#include "core/FruitySlicer.h"
#include "core/SlicexInstrument.h"
#include "core/TempoMap.h"
#include "core/ModelParams.h"
#include "core/FlmDwSampler.h"
#include "core/FlmSlicer.h"
#include "core/SamplePrecompute.h"
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
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace lynx
{
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
           && a.mpe == b.mpe;
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
                    s = dw::zoneSamples (*data, z < formats.size() && formats[z]);
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
                                     ? factories.stretchedSpans (channel.samplePath, precompute::variantKey (spanSettings, clip), span, ratio,
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
            if (auto stretched = factories.stretchedSamples (channel.samplePath, ratio, channel.sampler.stretchMethod, channel.sampler.stretchPitch, map,
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

    auto state = std::make_unique<RackState>();
    state->live = std::make_shared<LiveLevels> (song);
    state->instruments.reserve (song.channels.size());
    std::vector<Instrument*> keyShiftLater; // new instruments only: none is visible to the audio thread yet

    // which previous channel each channel was (round 168s, issue #32): by its instance tag, so inserting, removing or
    // moving channels keeps every other channel's running instrument (voices, loops, a plugin's live state); by position
    // only when the channel there is gone from the song (another project loaded over it, the old behaviour)
    const auto channelOrigin = matchByInstance (song.channels, previous != nullptr ? &previous->song.channels : nullptr,
                                                previous != nullptr ? previous->instruments.size() : 0);
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

        if (previous != nullptr && p < previous->song.channels.size() && p < previous->instruments.size()
            && previous->song.channels[p].instrument == kind && kind != InstrumentKind::patcher
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
    prepareKeyShifts (keyShiftLater, sampleRate);

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
                if (channels[d].cutBy == channels[c].cutGroup && flCutsIt (channels[d].instrument))
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
    if (pinned)
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
    graphOk = true;
}

std::vector<int> RackState::currentLatencies() const
{
    std::vector<int> out;

    // a hosted plugin's latency set by hand in its wrapper settings (round 149) replaces what it reports
    auto manual = [this] (const PluginRef& ref, int reported) {
        return ref.latencyOverrideMs >= 0.0f ? (int) std::lround (ref.latencyOverrideMs * 0.001 * sampleRate) : reported;
    };
    for (std::size_t c = 0; c < instruments.size(); ++c)
    {
        const auto reported = instruments[c]->latencySamples();
        out.push_back (c < song.channels.size() && song.channels[c].instrument == InstrumentKind::plugin ? manual (song.channels[c].plugin, reported) : reported);
    }

    for (std::size_t t = 0; t < effects.size(); ++t)
        for (std::size_t s = 0; s < effects[t].size(); ++s)
        {
            if (lowlatency::bypassed (lowLatencySlots, (int) t, (int) s)) // (round 190aq: bypassed, nothing to compensate)
            {
                out.push_back (0);
                continue;
            }
            const auto reported = effects[t][s]->latencySamples();
            const auto* slot = t < song.mixer.size() && s < song.mixer[t].effects.size() ? &song.mixer[t].effects[s] : nullptr;
            out.push_back (slot != nullptr && slot->kind == EffectKind::plugin ? manual (slot->plugin, reported) : reported);
        }

    return out;
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

    pdc = computePdc (song, channelLatency, slotLatency, sampleRate);

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
    }
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

    // round 177b: the lanes of unmuted tracks; a lane automation's own clips (if any) don't play as clips
    laneAutomations.clear();
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        for (const auto& lane : song.playlistTracks[(std::size_t) t].lanes)
            if (lane.automation >= 0 && lane.automation < count && ! song.isTrackMuted (t)
                && ! automations[(std::size_t) lane.automation].points.empty())
            {
                automationClips[(std::size_t) lane.automation].clear();
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
    hasAutomation = false;
    tempoAutomation = -1;

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
                                  // FL Studio Mobile's DW Sampler: its own 34 values (round 190de)
                                  || (flmdw::isChannel (song.channels[(std::size_t) t.index]) && t.param >= flmdw::automationBase
                                      && t.param < flmdw::automationBase + flmdw::paramCount)
                                  || (flmslicer::isChannel (song.channels[(std::size_t) t.index]) && t.param >= flmslicer::automationBase
                                      && t.param < flmslicer::automationBase + flmslicer::paramCount)
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

    // issue #97: the automations with clips on one target share a slot, the clip that plays decides (automationValueAt)
    automationGroups.assign ((std::size_t) count, {});
    automationFollowers.assign ((std::size_t) count, 0);
    const auto playsClips = [&] (int a) {
        const auto& automation = automations[(std::size_t) a];
        return ! automationClips[(std::size_t) a].empty() && ! automation.isLive() && zoneAutomations[(std::size_t) a] == 0
               && automation.target.kind != AutomationTargetKind::none;
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
    // otherwise the last automation of the same target wins (a live controller link, Performance mode clips)
    for (int a = 0; a < count; ++a)
    {
        if (automationFollowers[(std::size_t) a] != 0
            || (automationClips[(std::size_t) a].empty() && ! automations[(std::size_t) a].isLive() && zoneAutomations[(std::size_t) a] == 0))
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

            eventLanes.push_back ({ p, l, slot });
            bind (slot++, lane.target);
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
            case AutomationTargetKind::none: break;
        }
        return -1;
    };
    for (int m = 0; m < (int) song.modulators.size(); ++m)
        for (const auto& route : song.modulators[(std::size_t) m].routes)
        {
            if (slot >= maxAutomations || route.depth == 0.0f || ! Modulator::canModulate (song, route.target))
                continue;
            const auto source = boundSlot (route.target);
            modRoutes.push_back ({ slot, source, std::clamp (song.targetValue (route.target), 0.0f, 1.0f), m, route.depth });
            bind (slot++, route.target);
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

    for (int i = 0; i < n; ++i)
    {
        const auto a = std::abs (data[i]);
        peak = a > peak ? a : peak; // form the compiler vectorizes
    }

    return peak;
}

// No NaN / inf in the block? (x · 0 is 0 for every finite x: one vectorized pass)
bool finiteBlock (const float* l, const float* r, int n) noexcept
{
    float acc = 0.0f;
    for (int k = 0; k < n; ++k)
        acc += l[k] * 0.0f + r[k] * 0.0f;
    return acc == 0.0f;
}

std::atomic<std::uint64_t> nonFiniteBlocks { 0 };

// Any sample above `level`? Stops at the first: with sound (the common case) it costs almost nothing.
bool anyAbove (const float* l, const float* r, int n, float level) noexcept
{
    // in blocks of 32: the inner loop vectorizes; silent chunks (the ones that scan everything) stay cheap
    int i = 0;

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
    UnitTimer (bool on, std::atomic<std::uint64_t>& s, std::atomic<std::uint64_t>* m = nullptr) noexcept
        : slot (s), longest (m), start (on ? nowNs() : 0) {}
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
    }
    std::atomic<std::uint64_t>& slot;
    std::atomic<std::uint64_t>* longest;
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
    automationValues.fill (std::numeric_limits<float>::quiet_NaN());
    lastTrackGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    lastTapGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    lastMeterGain.fill ({ std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f });
    channelBuffers.assign ((std::size_t) maxChannels * 2 * chunkCapacity, 0.0f);
    smartDisable.assign ((std::size_t) maxMixerTracks * maxEffectSlots, {});
    trackEqs.assign ((std::size_t) maxMixerTracks, {});
    trackScratchBuffers.assign ((std::size_t) maxMixerTracks * 2 * chunkCapacity, 0.0f);
    tapBuffers.assign ((std::size_t) RackState::maxSendTaps * 2 * chunkCapacity, 0.0f);
    sampleRate = sr;
    blockCapacity = std::max (64, maxBlockSize);
    mixBuffers.assign ((std::size_t) maxMixerTracks * 2 * (std::size_t) blockCapacity, 0.0f);
    hwScratch.assign (2 * (std::size_t) blockCapacity, 0.0f);
    layerPick.assign ((std::size_t) maxChannels * 128, (std::int16_t) -1);
    voiceOrderScratch.assign ((std::size_t) maxChannels, {});
    heldScratch.assign ((std::size_t) maxChannels, {});
    polyLayers.assign ((std::size_t) maxChannels, {});
    polyScratch.assign ((std::size_t) maxChannels, {});

    if (lastState != nullptr)
    {
        for (auto& instrument : lastState->instruments)
            instrument->prepare (sr);

        for (auto& track : lastState->effects)
            for (auto& effect : track)
                effect->prepare (sr);
    }
}

float* RackRenderer::trackBuffer (int track, int channel) noexcept
{
    return mixBuffers.data() + ((std::size_t) track * 2 + (std::size_t) channel) * (std::size_t) blockCapacity;
}

float* RackRenderer::tapBuffer (int tap, int side) noexcept
{
    return tapBuffers.data() + ((std::size_t) tap * 2 + (std::size_t) side) * chunkCapacity;
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
float automationValueAt (const RackState& state, int slot, double tick) noexcept
{
    const auto& song = state.song;
    const auto& group = state.automationGroups[(std::size_t) slot];
    const Clip* playing = nullptr;
    const Clip* ended = nullptr;
    int playingAutomation = -1, endedAutomation = -1;

    const auto scan = [&] (int a) {
        for (const auto i : state.automationClips[(std::size_t) a]) // sorted by start
        {
            const auto& c = song.playlist[(std::size_t) i];
            if (tick < (double) c.start)
                break;
            if (tick < (double) c.end())
            {
                if (playing == nullptr || c.start > playing->start)
                    playing = &c, playingAutomation = a;
            }
            else if (ended == nullptr || c.end() > ended->end() || (c.end() == ended->end() && c.start > ended->start))
                ended = &c, endedAutomation = a;
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

void RackRenderer::updateAutomation (const RackState& state, double startTick) noexcept
{
    const auto& song = state.song;

    // controllers (live sources): the effect's output at the end of the previous block, in any mode
    for (const auto a : state.liveAutomations)
    {
        const auto& automation = song.automations[(std::size_t) a];
        const auto& src = automation.source;
        if (src.channel >= 0)
        {
            if (src.channel < (int) state.instruments.size() && state.instruments[(std::size_t) src.channel] != nullptr)
                automationValues[(std::size_t) a] = automation.mapLive (state.instruments[(std::size_t) src.channel]->controlOutput (src.output));
        }
        else if (src.track < (int) state.effects.size() && src.slot >= 0 && src.slot < (int) state.effects[(std::size_t) src.track].size()
            && state.effects[(std::size_t) src.track][(std::size_t) src.slot] != nullptr)
            automationValues[(std::size_t) a] = automation.mapLive (state.effects[(std::size_t) src.track][(std::size_t) src.slot]->controlOutput (src.output));
    }

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
                const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.beatsPerBar));
                const auto t = std::fmod (std::max (0.0, startTick), length);

                for (const auto& lane : state.eventLanes)
                    if (lane.pattern == current)
                        automationValues[(std::size_t) lane.slot] = pattern.events[(std::size_t) lane.lane].valueAt (t);
            }
        }
        else
        {
            const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
            const auto tick = std::fmod (std::max (0.0, startTick), length);

            // inside a clip of the pattern: its events; after one: the value it ended with; before the first:
            // no value (the target's own). Depends only on the position, like automation clips.
            for (const auto& lane : state.eventLanes)
            {
                const auto& events = song.patterns[(std::size_t) lane.pattern].events[(std::size_t) lane.lane];
                auto value = std::numeric_limits<float>::quiet_NaN();

                for (auto i : state.patternClips[(std::size_t) lane.pattern]) // sorted by start
                {
                    const auto& c = song.playlist[(std::size_t) i];

                    if (tick < (double) c.start)
                        break;

                    // a stretched clip loops its pattern (LYNX extra stretchedClipsLoop, issue #25), its events too
                    const auto period = (std::size_t) lane.pattern < state.schedule.patternLength.size() ? state.schedule.patternLength[(std::size_t) lane.pattern] : 0;
                    value = events.valueAt (patternTickInClip (c, tick < (double) c.end() ? tick : (double) c.end() - 1.0, period));
                }

                automationValues[(std::size_t) lane.slot] = value;
            }
        }
    }

    // in pattern mode the Playlist does not play: values stay as they were
    if (song.mode != PlayMode::song)
    {
        compensateTick = -1.0;
        clearOverriddenValues();
        // only the plugin parameters driven by pattern events and by live controllers (links: FL's work in any mode,
        // round 165)
        if (! state.eventLanes.empty() || ! state.liveAutomations.empty())
            applyPluginParams (state, std::min<int> ((int) song.automations.size(), RackState::maxAutomations), true);
        return;
    }

    const auto length = (double) std::max<std::int64_t> (1, state.schedule.songLength);
    const auto tick = std::fmod (std::max (0.0, startTick), length);

    for (std::size_t a = 0; a < state.automationClips.size(); ++a)
        if (! state.automationClips[a].empty() && state.automationFollowers[a] == 0)
            automationValues[a] = automationValueAt (state, (int) a, tick);

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
    applyPluginParams (state, 0);
}

void RackRenderer::updateModulators (const RackState& state, double tick, int numSamples) noexcept
{
    const auto& song = state.song;
    const auto beats = std::max (0.0, tick) / (double) ppq;
    for (const auto& r : state.modRoutes)
    {
        if (r.modulator < 0 || r.modulator >= (int) song.modulators.size())
            continue;
        const auto& m = song.modulators[(std::size_t) r.modulator];
        const auto cycles = m.sync ? beats / std::max (1.0e-3, m.beats) : modSeconds * std::max (0.0, m.hz);
        const auto source = r.source >= 0 ? automationValues[(std::size_t) r.source] : std::numeric_limits<float>::quiet_NaN();
        const auto base = std::isnan (source) ? r.base : source;
        automationValues[(std::size_t) r.slot] = std::clamp (base + r.depth * m.output (cycles), 0.0f, 1.0f);
    }
    if (sampleRate > 0.0)
        modSeconds += numSamples / sampleRate;
}

float RackRenderer::compensatedAutomation (const RackState& state, int automation, int track, int slot, float current) const noexcept
{
    if (compensateTicksPerSample <= 0.0 || compensateTick < 0.0 || automation < 0 || std::isnan (current)
        || (std::size_t) track >= state.pdc.slotInput.size() || (std::size_t) slot >= state.pdc.slotInput[(std::size_t) track].size()
        || (std::size_t) automation >= state.automationClips.size() || state.automationClips[(std::size_t) automation].empty()
        || automation >= RackState::maxAutomations || std::isnan (automationValues[(std::size_t) automation]))
        return current;
    const auto lag = state.pdc.slotInput[(std::size_t) track][(std::size_t) slot];
    if (lag <= 0)
        return current;
    const auto earlier = automationValueAt (state, automation, std::max (0.0, compensateTick - lag * compensateTicksPerSample));
    return std::isnan (earlier) ? current : earlier;
}

void RackRenderer::applyPluginParams (const RackState& state, int firstSlot, bool andLive) noexcept
{
    for (const auto& b : state.pluginParams)
    {
        if (b.automation < firstSlot
            && ! (andLive && (std::size_t) b.automation < state.song.automations.size() && state.song.automations[(std::size_t) b.automation].isLive()))
            continue;

        const auto v = automationValues[(std::size_t) b.automation];

        if (std::isnan (v))
            continue;

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
                continue;
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
        if ((std::size_t) route.track >= state.effects.size() || (std::size_t) route.slot >= state.effects[(std::size_t) route.track].size())
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
    return channelBuffers.data() + ((std::size_t) channel * 2 + (std::size_t) side) * chunkCapacity;
}

float* RackRenderer::trackScratch (int track, int side) noexcept
{
    return trackScratchBuffers.data() + ((std::size_t) track * 2 + (std::size_t) side) * chunkCapacity;
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

void RackRenderer::renderChannel (const RackState& state, int i, int n) noexcept
{
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
        state.instruments[(std::size_t) i]->setSongPosition (chunkTick);

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
        return;
    }
    channelSilent[(std::size_t) i] = 0;
    const UnitTimer timer (profiling.load (std::memory_order_relaxed), units->channelNs[(std::size_t) i], &units->channelMaxNs[(std::size_t) i]);
    std::memset (l, 0, sizeof (float) * (std::size_t) n);
    std::memset (r, 0, sizeof (float) * (std::size_t) n);
    int position = 0;

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
            {
                instrument.render (l + position, r + position, e.sampleOffset - position, gl, gr);
                position = e.sampleOffset;
            }
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
        if (e.port >= 0 || e.type == NoteEvent::Type::voiceProp) // (what this channel sends out on a port is for the others)
            continue;

        if (e.sampleOffset > position)
        {
            instrument.render (l + position, r + position, e.sampleOffset - position, gl, gr);
            position = e.sampleOffset;
        }

        auto& held = heldNotes[(std::size_t) i];
        const auto bit = std::uint64_t { 1 } << (e.key & 63);
        if (e.type == NoteEvent::Type::on)
        {
            if (e.key >= 0 && e.key < 128)
            {
                held[(std::size_t) (e.key >> 6)] |= bit;
                struckNotes[(std::size_t) i][(std::size_t) (e.key >> 6)] |= bit;
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
                const auto glideSamples = (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, chunkTps));
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
                instrument.noteOnAt (e.key, velocity, e.startSeconds);
                instrument.setNextNoteId (0);
                HostSlide::pendingGlide() = {};
            }
        }
        else if (e.type == NoteEvent::Type::slide)
        {
            instrument.slideTo (e.key, e.cents + c.misc.fineTune + (c.misc.mainPitch ? state.song.masterPitch : 0.0f), e.slideSamples);
        }
        else if (e.type == NoteEvent::Type::choke)
        {
            instrument.cut(); // short fade, like FL's "cut"
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
                                    (float) (channelpoly::glideBeats (mode.portaBeats) * ppq / std::max (1.0e-9, chunkTps)));
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

    if (position < n)
        instrument.render (l + position, r + position, n - position, gl, gr);

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
           || kind == EffectKind::peakController; // issue #66: its display's Peak + LFO, Peak and LFO lines
}

void RackRenderer::setSidechain (const RackState& state, const EffectSlot& slot, Effect& effect, int track) noexcept
{
    // the source was already processed in this pass (earlier layer); silent = no signal in the detector
    for (const auto second : { false, true })
    {
        const auto source = second ? slot.sidechain2 : slot.sidechain;
        if (source < 0 || source == track || source >= (int) state.song.mixer.size())
            continue;
        const auto active = (std::size_t) source < trackActive.size() && trackActive[(std::size_t) source] != 0;
        const auto* left = active ? trackBuffer (source, 0) : nullptr;
        const auto* right = active ? trackBuffer (source, 1) : nullptr;
        if (second)
            effect.setSidechain2 (left, right);
        else
            effect.setSidechain (left, right);
    }
}

void RackRenderer::connect (const RackState& state, const SlotConnections& c, Effect& effect, int track, const float* l, const float* r) noexcept
{
    // a hosted effect's Connections (round 189j [EXEC FL 2026]): input 1 from the insert its index picks (processed
    // earlier, like a sidechain source; silent when it is), input 2 the slot's own input when no source feeds it
    auto sounding = [&] (int source) {
        return source > 0 && source != track && source < (int) state.song.mixer.size() && (std::size_t) source < trackActive.size()
               && trackActive[(std::size_t) source] != 0;
    };
    if (c.mainSource == -2)
        effect.setMainInput (nullptr, nullptr);
    else if (c.mainSource > 0)
        effect.setMainInput (sounding (c.mainSource) ? trackBuffer (c.mainSource, 0) : nullptr, sounding (c.mainSource) ? trackBuffer (c.mainSource, 1) : nullptr);
    if (c.sideSource == -1)
        effect.setSidechain (l, r);
    else if (c.sideSource == -2)
        effect.setSidechain (nullptr, nullptr);
    else // (its index, when no side input source is picked: setSidechain fed the slot's own one, if any)
        effect.setSidechain (sounding (c.sideSource) ? trackBuffer (c.sideSource, 0) : nullptr, sounding (c.sideSource) ? trackBuffer (c.sideSource, 1) : nullptr);
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

    auto* l = trackBuffer (track, 0);
    auto* r = trackBuffer (track, 1);
    int audible = -1; // -1 = not measured yet; an effect's output is the next one's input
    // round 190bu: the chain's Blend (nullptr at 100 %): the insert's input is kept before the first slot
    static_assert (chunkCapacity <= RackState::ChainBlendStage::capacity);
    auto* blend = (std::size_t) track < state.chainBlends.size() ? state.chainBlends[(std::size_t) track].get() : nullptr;
    if (blend != nullptr)
    {
        std::memcpy (blend->dry.data(), l, sizeof (float) * (std::size_t) n);
        std::memcpy (blend->dry.data() + RackState::ChainBlendStage::capacity, r, sizeof (float) * (std::size_t) n);
    }
    const auto timing = times->enabled.load (std::memory_order_relaxed);
    auto timed = [this, timing] (std::size_t key, auto&& run) {
        if (! timing || key >= times->effectNs.size())
        {
            run();
            return;
        }
        const auto before = ProcessTimes::now();
        run();
        times->effectNs[key].fetch_add (ProcessTimes::now() - before, std::memory_order_relaxed);
    };

    for (std::size_t i = 0; i < effects.size() && i < slots.size(); ++i)
    {
        // slot dry/wet (automatable)
        const auto wet = state.hasAutomation && i < state.effectMixAuto[(std::size_t) track].size()
                             ? std::clamp (automated (state.effectMixAuto[(std::size_t) track][i], slots[i].mix), 0.0f, 1.0f)
                             : slots[i].mix;

        auto clearMeters = [this, track, i] {
            const auto at = ((std::size_t) track * maxEffectSlots + i) * ProcessTimes::meterOutputs;
            if (at + ProcessTimes::meterOutputs <= times->effectMeters.size())
                for (int o = 0; o < ProcessTimes::meterOutputs; ++o)
                    times->effectMeters[at + (std::size_t) o].store (0.0f, std::memory_order_relaxed);
        };

        if (! state.song.slotActive (slots[i]) || wet <= 0.0f)
        {
            if (hasLiveMeters (slots[i].kind))
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
                if (hasLiveMeters (slots[i].kind))
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
        const auto isPlugin = slots[i].kind == EffectKind::plugin;
        const auto* connections = isPlugin && (std::size_t) track < state.slotConnections.size() && i < state.slotConnections[(std::size_t) track].size()
                                      && state.slotConnections[(std::size_t) track][i].routed
                                      ? &state.slotConnections[(std::size_t) track][i]
                                      : nullptr; // (round 189j: a hosted effect's routed Connections)
        const auto hold = (int) (sampleRate * (isPlugin || slots[i].kind == EffectKind::patcher ? 4.0 : 1.0)); // (a Patcher may hold plugins)

        // (not Gross Beat: its buffer must keep recording the silence it may play back later, as in FL)
        // (nor Newtone / Newtime: in slave mode they play their sample whatever the input)
        // (nor a slot whose "Smart disable" is off, or a plugin that does not allow it — FL's per-plugin options, REA round 139)
        // (nor a hosted effect whose input 1 comes from another insert: its own input says nothing; round 189j)
        // (nor anything while FL's Settings › Audio › CPU › "Smart disable" is off: the global switch, round 190bx)
        if (key < smartDisable.size() && slots[i].smartDisable && ! (isPlugin && ! slots[i].plugin.allowSmartDisable)
            && flsettings::options().smartDisable.load (std::memory_order_relaxed)
            && ! (connections != nullptr && connections->mainSource > 0)
            && slots[i].kind != EffectKind::grossBeat && slots[i].kind != EffectKind::newtone
            && slots[i].kind != EffectKind::newtime
            && slots[i].kind != EffectKind::scratcher // (nor the Scratcher: its turntable plays whatever the input)
            && slots[i].kind != EffectKind::edison // (nor Edison: it records silence and plays whatever the input, round 190cd)
            && slots[i].kind != EffectKind::hardwareInsert // (nor the hardware insert: outboard gear has its own tails and noise)
            && ! (slots[i].kind == EffectKind::patcher && patcher::usesHardwareIo (slots[i].patcher)) // (nor a Patcher holding one, round 165c)
            // (nor a controller: its outputs move whatever the sound — a silent insert is their usual place; round 164)
            && slots[i].kind != EffectKind::formulaController
            && slots[i].kind != EffectKind::xyController && slots[i].kind != EffectKind::xyzController
            && slots[i].kind != EffectKind::controlSurface
            && slots[i].kind != EffectKind::peakController // (issue #66: its LFO runs and its peak falls in silence, as FL's)
            // round 164c: a visual plugin keeps recording silence (its display falls, the video export stays in time)
            && slots[i].kind != EffectKind::waveCandy && slots[i].kind != EffectKind::visualizer
            && slots[i].kind != EffectKind::tuner // (round 165g: the tuner's display follows the signal into silence)
            // (round 166f: nor Facet EQ playing its comparative target — the sidechain plays while this track is silent)
            && ! (slots[i].kind == EffectKind::facetEq && facet::listensToTarget (slots[i].params)))
        {
            auto& sd = smartDisable[key];

            if (sd.effect != effects[i].get())
                sd = { effects[i].get(), 0 };

            if (audible < 0)
                audible = anyAbove (l, r, n, silenceLevel) ? 1 : 0;

            if (audible == 1)
                sd.silentSamples = 0;
            else if (sd.silentSamples >= hold)
            {
                if (hasLiveMeters (slots[i].kind))
                    clearMeters();
                continue;
            }
            else
                sd.silentSamples += n;
        }

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
            const auto* sl = sounding ? trackBuffer (source, 0) : nullptr;
            const auto* sr = sounding ? trackBuffer (source, 1) : nullptr;
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
                               &units->effectMaxNs[std::min (key, units->effectNs.size() - 1)]);

        // the hardware insert (round 164d): this piece's device input and output buses (none during an export)
        if (slots[i].kind == EffectKind::hardwareInsert || slots[i].kind == EffectKind::patcher) // (a Patcher hands them to its nodes, round 165c)
            effects[i]->setHardwareIo (liveInput, hwOut != nullptr && hwOut->active.load (std::memory_order_relaxed) ? hwOut : nullptr, liveInputOffset);

        // Fruity Send / Patcher / Transmitter / Transporter: what it sends goes to its own buffer, pulled by the destination insert
        // (processed later)
        if (slots[i].kind == EffectKind::fruitySend || slots[i].kind == EffectKind::patcher || slots[i].kind == EffectKind::transmitter
            || slots[i].kind == EffectKind::flTransporter || slots[i].kind == EffectKind::equo || connections != nullptr)
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

        // round 170c: the slot's input peak for an editor's level meter (only while an editor asks)
        const bool levels = key < times->levelWanted.size() && times->levelWanted[key].load (std::memory_order_relaxed) > 0;
        if (levels)
        {
            auto& in = times->slotLevels[key * 2];
            in.store (std::max (in.load (std::memory_order_relaxed), std::max (peakOf (l, n), peakOf (r, n))), std::memory_order_relaxed);
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
            auto params = slots[i].params; // copy without allocation
            const auto& autos = state.effectParamAuto[(std::size_t) track][i];
            const auto used = std::min (params.size(), effectInfo (slots[i].kind).params.size());

            for (std::size_t p = 0; p < used; ++p)
                params[p] = compensatedAutomation (state, autos[p], track, (int) i, automated (autos[p], params[p]));

            effects[i]->setSongPosition (chunkTick);
            effects[i]->setPlaying (chunkPlaying);
            effects[i]->setHeldNotes (heldNotes.data(), maxChannels);
            setSidechain (state, slots[i], *effects[i], track);
            if (connections != nullptr)
                connect (state, *connections, *effects[i], track, l, r);
            timed (key, [&] { runGoalSlot (slots[i], *effects[i], key, l, r, n, params, bpm); });
        }
        else
        {
            effects[i]->setSongPosition (chunkTick);
            effects[i]->setPlaying (chunkPlaying);
            effects[i]->setHeldNotes (heldNotes.data(), maxChannels);
            setSidechain (state, slots[i], *effects[i], track);
            if (connections != nullptr)
                connect (state, *connections, *effects[i], track, l, r);
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
            out.store (std::max (out.load (std::memory_order_relaxed), std::max (peakOf (l, n), peakOf (r, n))), std::memory_order_relaxed);
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
        if (hasLiveMeters (slots[i].kind) && key < (std::size_t) maxMixerTracks * maxEffectSlots)
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
    const UnitTimer timer (profiling.load (std::memory_order_relaxed), units->processNs);
    if (profiling.load (std::memory_order_relaxed))
        units->processedSamples.fetch_add ((std::uint64_t) std::max (0, numSamples), std::memory_order_relaxed);
    std::memset (left, 0, sizeof (float) * (std::size_t) numSamples);
    std::memset (right, 0, sizeof (float) * (std::size_t) numSamples);
    events.clear();

    if (state == nullptr || blockCapacity == 0)
        return;

    const auto timing = times->enabled.load (std::memory_order_relaxed);
    const auto wallStart = timing ? ProcessTimes::now() : 0;

    // A snapshot built before the device opened may have another rate. The instruments'
    // prepare() does not allocate, so it can run here.
    if (state != lastState && state->sampleRate != sampleRate)
    {
        for (auto& instrument : state->instruments)
            instrument->prepare (sampleRate);

        for (auto& track : state->effects)
            for (auto& effect : track)
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
        for (auto& arp : state->arpeggiators)
            if (arp != nullptr)
                arp->reset();
        for (auto& echo : state->echoes)
            if (echo != nullptr)
                echo->reset();
    }

    // a start, a stop or a jump: every note was released (or none sounds yet)
    if (playing != wasPlaying || jumped)
        scheduledNotes->clear();

    if ((wasPlaying && ! playing) || jumped)
    {
        for (auto& instrument : state->instruments)
            if (jumped)
                instrument->allNotesOff();
            else
                instrument->transportStopped(); // (a hosted plugin: FL's "Send All Notes Off when playback stops")
        heldNotes.fill ({});
        clearPolyLayers();

        for (auto& arp : state->arpeggiators)
            if (arp != nullptr)
                arp->reset();
        for (auto& echo : state->echoes)
            if (echo != nullptr)
                echo->reset();
    }

    // FL resets its generators when playback starts: a MIDI Out sends its bank, patch and controls again
    // (REA_rodada167i §1.4)
    if (playing && ! wasPlaying)
        for (std::size_t c = 0; c < state->song.channels.size() && c < state->instruments.size(); ++c)
            if (state->song.channels[c].instrument == InstrumentKind::midiOut && state->instruments[c] != nullptr)
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
        for (auto& slots : state->effects)
            for (auto& effect : slots)
                if (effect != nullptr)
                    effect->transportReset();
        for (auto& instrument : state->instruments)
            if (instrument != nullptr)
                instrument->transportReset();
    }
    wasPlaying = playing;
    // tempo automation: the tempo holds for the whole block (evaluated at its start)
    bpm = tempoOverride > 0.0 ? tempoOverride : state->song.bpm;

    // and FL's pattern Tempo events (issue #86, core/TempoMap.h): in song mode with the tempo clips, in pattern mode the
    // current pattern's. A tempo the user is dragging over its automation holds (as before).
    if (tempoOverride <= 0.0 && state->tempoMap && ! (state->tempoAutomation >= 0 && isSlotOverridden (state->tempoAutomation)))
    {
        const auto songMode = state->song.mode == PlayMode::song;
        const auto length = (double) std::max<std::int64_t> (1, state->schedule.songLength);
        bpm = tempomap::tempoAt (state->song, songMode ? std::fmod (std::max (0.0, startTick), length) : std::max (0.0, startTick));
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
                for (auto& slots : state->effects)
                    for (auto& effect : slots)
                        if (effect != nullptr)
                            effect->transportReset(); // native effects: reset(); plugins: their wrapper's reset option
                continue;
            }
            if (e.type == NoteEvent::Type::choke && e.channel < 0) // panic: every channel stops sounding
            {
                for (auto& instrument : state->instruments)
                    if (instrument != nullptr)
                        instrument->panic();
                heldNotes.fill ({});
                for (auto& order : voiceOrders)
                    order.clear();
                clearPolyLayers();
                continue;
            }

            if (e.channel < 0 || e.channel >= (int) state->instruments.size())
                continue;

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

    for (int done = 0; done < numSamples;)
    {
        auto n = std::min (chunk, numSamples - done);

        // pattern events take effect at their own sample, as in FL (REA_rodada85 §3): a chunk ends where one falls
        if (playing && ! state->eventLanes.empty() && tps > 0.0)
        {
            const auto ahead = ticksToNextEvent (*state, startTick + done * tps);
            if (ahead < (double) n * tps)
                n = std::max (1, (int) std::ceil (ahead / tps - 1.0e-9));
        }
        liveInputOffset = done;
        processChunk (*state, playing, startTick + done * tps, n, left + done, right + done, meters);
        resumeAudio = false;
        chaseNotes = false;
        done += n;
    }
    publishSoundingKeys ((int) state->song.channels.size());

    if (timing)
    {
        times->wallNs.fetch_add (ProcessTimes::now() - wallStart, std::memory_order_relaxed);
        times->audioNs.fetch_add ((std::uint64_t) ((double) numSamples / sampleRate * 1.0e9), std::memory_order_relaxed);
    }
}

void RackRenderer::publishSoundingKeys (int channels) noexcept
{
    channels = std::clamp (channels, 0, maxChannels);
    const auto top = std::max (channels, publishedTop);
    publishedTop = 0;
    for (int i = 0; i < top; ++i)
    {
        auto& was = publishedNotes[(std::size_t) i];
        NoteMask now {};
        if (i < channels)
        {
            const auto& held = heldNotes[(std::size_t) i];
            auto& struck = struckNotes[(std::size_t) i];
            now = { held[0] | struck[0], held[1] | struck[1] };
            struck = {};
        }
        if (now[0] != 0 || now[1] != 0)
            publishedTop = i + 1;
        if (now == was)
            continue;
        was = now;
        sounding[(std::size_t) i][0].store (now[0], std::memory_order_relaxed);
        sounding[(std::size_t) i][1].store (now[1], std::memory_order_relaxed);
    }
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
        const auto length = (double) std::max<std::int64_t> (1, pattern.effectiveLength (song.beatsPerBar));
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

    instrument->midiMessage (m, size);
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
        if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr)
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

void RackRenderer::processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                                 float* left, float* right, MeterBank* meters) noexcept
{
    chunkTick = startTick;
    chunkPlaying = playing;
    // FL: playback starting puts every control the user moved back under its automation (issue #125)
    if (playing && ! releaseWasPlaying && anyReleased.load (std::memory_order_relaxed))
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
    if (state.hasAutomation) // (a hair later: a chunk that starts on a pattern event's sample takes its value)
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
        timeline.bpm.store (bpm, std::memory_order_relaxed);
        timeline.sampleRate.store (sampleRate, std::memory_order_relaxed);
        timeline.beatsPerBar.store (std::max (1, song.beatsPerBar), std::memory_order_relaxed);
        timeline.playing.store (playing, std::memory_order_relaxed);
        timeline.looping.store (song.hasLoop(), std::memory_order_relaxed);
        timeline.loopStart.store ((double) song.loopStart, std::memory_order_relaxed);
        timeline.loopEnd.store ((double) song.loopEnd, std::memory_order_relaxed);
        timeline.ticksPerSample.store (playing ? chunkTps : 0.0, std::memory_order_relaxed);
        timeline.chunk.fetch_add (1, std::memory_order_relaxed);
    }

    for (int t = 0; t < tracks; ++t)
    {
        std::memset (trackBuffer (t, 0), 0, sizeof (float) * (std::size_t) numSamples);
        std::memset (trackBuffer (t, 1), 0, sizeof (float) * (std::size_t) numSamples);
    }

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
        schedulePatternLoop (pattern, pattern.effectiveLength (song.beatsPerBar), startTick, numSamples,
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
            if (state.midiOutTargets[c].empty())
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
            if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr)
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
        if ((std::size_t) c >= state.instruments.size() || state.instruments[(std::size_t) c] == nullptr)
            continue;
        std::array<std::uint32_t, midiout::maxResetMessages + 8> sent; // (a MIDI Out's reset: every active control at once)
        const auto count = state.instruments[(std::size_t) c]->takeMidiMessages (sent.data(), (int) sent.size());
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

            if (e.type != NoteEvent::Type::on || e.channel < 0 || e.channel >= (int) state.cutTargets.size())
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

    // 1) channels, each with its own notes at the exact sample (in parallel when worth it)
    const auto channels = std::min<int> ({ (int) state.instruments.size(), (int) song.channels.size(), maxChannels });
    auto channel = [this, &state, numSamples] (int c) {
        if (! times->enabled.load (std::memory_order_relaxed) || c >= ProcessTimes::channels)
        {
            renderChannel (state, c, numSamples);
            return;
        }
        const auto before = ProcessTimes::now();
        renderChannel (state, c, numSamples);
        times->channelNs[(std::size_t) c].fetch_add (ProcessTimes::now() - before, std::memory_order_relaxed);
    };

    // the task graph (round 165i): channels and inserts by readiness; else the channel pass, then the inserts by layers
    // FL's Settings › Audio › CPU (round 190bx): "Multithreaded generator processing" and "Multithreaded mixer
    // processing". With either off that half runs on the audio thread alone (the layers then: the task graph mixes both).
    const auto threadGenerators = flsettings::options().multithreadGenerators.load (std::memory_order_relaxed);
    const auto threadMixer = flsettings::options().multithreadMixer.load (std::memory_order_relaxed);
    if (! (threadGenerators && threadMixer) || ! runTaskGraph (state, playing, channels, numSamples, meters))
    {
        if (workers != nullptr && state.parallel && threadGenerators && state.audioThreadChannels.empty())
            workers->parallelFor (channels, channel);
        else if (workers != nullptr && state.parallel && threadGenerators)
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

            // extra outputs to other inserts (a generator Patcher's, REA_rodada141)
            if ((std::size_t) c < state.instruments.size() && state.instruments[(std::size_t) c] != nullptr)
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
            auto track = [this, &state, &layer, numSamples, meters] (int i) { processTrack (state, layer[(std::size_t) i], numSamples, meters); };

            if (workers != nullptr && state.parallel && threadMixer && layer.size() > 1)
                workers->parallelFor ((int) layer.size(), track);
            else
                for (int i = 0; i < (int) layer.size(); ++i)
                    track (i);

            if (split) // inserts with a plugin whose "Allow threaded processing" is off
                for (const auto t : state.audioThreadLayers[d])
                    processTrack (state, t, numSamples, meters);
        }
    }

    // 3) Master: inputs, effects, fader. Without a configured mixer, it stays at 0 dB.
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
        const auto volume = state.hasAutomation ? automated (state.mixerVolumeAuto[0], fader) : fader;
        const auto pan = state.hasAutomation ? automated (state.mixerPanAuto[0], (knob + 1.0f) * 0.5f) * 2.0f - 1.0f : knob;
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
        meters->capture.push (0, left, right, numSamples);
        meters->logTap.push (0, left, right, numSamples);
        meters->loudness.push (left, right, numSamples, sampleRate); // (round 190am: one atomic load while it is off)
        if (chunkPlaying)
            meters->bounceTap.push (0, left, right, numSamples);
    }

    if (hwOut != nullptr && ! state.hwRoutes.empty() && hwOut->active.load (std::memory_order_relaxed))
        mixHardwareOutputs (state, numSamples);
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

        const float* l = trackActive[(std::size_t) t] ? trackBuffer (t, 0) : nullptr;
        const float* r = trackActive[(std::size_t) t] ? trackBuffer (t, 1) : nullptr;
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
    auto* dl = trackBuffer (t, 0);
    auto* dr = trackBuffer (t, 1);
    for (const auto c : state.trackChannels[(std::size_t) t])
    {
        if (c >= channels || channelSilent[(std::size_t) c])
            continue;
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
                const auto plain = std::min (state.sendDelays[t].size(), 1 + state.song.mixer[t].sends.size()); // not the Fruity Send taps
                for (std::size_t k = 0; k < plain; ++k)
                    if (state.sendDelays[t][k] != nullptr)
                        state.sendDelays[t][k]->clear();
            }
        for (const auto& route : state.hwRoutes)
            if (route.delay != nullptr && (std::size_t) route.track < state.bypassWhenIdle.size() && state.bypassWhenIdle[(std::size_t) route.track] != 0)
                route.delay->clear();
    }
    pdcBypassBefore = pdcBypassNow;
}

bool RackRenderer::runTaskGraph (const RackState& state, bool playing, int channels, int n, MeterBank* meters) noexcept
{
    // LYNX_TASKGRAPH=0: the layers only (A/B measurements)
    static const bool enabled = [] { const char* e = std::getenv ("LYNX_TASKGRAPH"); return e == nullptr || std::atoi (e) != 0; }();
    if (! enabled || ! state.graphOk || workers == nullptr || ! state.parallel || state.graphChannels != channels)
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
    auto task = [&] (int position) {
        int id;
        for (int spins = 0; (id = graphSlots[(std::size_t) position].load (std::memory_order_acquire)) == 0; ++spins)
        {
            WorkerPool::relax();
            if ((spins & 1023) == 1023)
                std::this_thread::yield();
        }
        --id;
        if (id < channels)
        {
            if (! timing || id >= ProcessTimes::channels)
                renderChannel (state, id, n);
            else
            {
                const auto before = ProcessTimes::now();
                renderChannel (state, id, n);
                times->channelNs[(std::size_t) id].fetch_add (ProcessTimes::now() - before, std::memory_order_relaxed);
            }
        }
        else
        {
            const auto t = id - channels + 1;
            sumChannelsInto (state, t, channels, n);
            if (live)
                mixLiveInputTrack (state, t, recording, playing, n);
            mixMetronome (state, t, n);
            mixSideInput (state, t, n);
            processTrack (state, t, n, meters);
        }
        for (auto k = state.graphNextStart[(std::size_t) id]; k < state.graphNextStart[(std::size_t) id + 1]; ++k)
        {
            const auto j = state.graphNext[(std::size_t) k];
            if (graphPending[(std::size_t) j].fetch_sub (1, std::memory_order_acq_rel) == 1)
                graphSlots[(std::size_t) graphPushed.fetch_add (1, std::memory_order_acq_rel)].store (j + 1, std::memory_order_release);
        }
    };
    workers->parallelFor (tasks, task);

    // the Master's own channels and input (its effects run after, as before)
    sumChannelsInto (state, 0, channels, n);
    if (live)
        mixLiveInputTrack (state, 0, recording, playing, n);
    mixMetronome (state, 0, n);
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

void RackRenderer::pullInputs (const RackState& state, int t, int n) noexcept
{
    if ((std::size_t) t >= state.mixerInputs.size())
        return;

    auto* dl = trackBuffer (t, 0);
    auto* dr = trackBuffer (t, 1);

    for (const auto& in : state.mixerInputs[(std::size_t) t])
    {
        const auto tap = in.tap >= 0 && in.tap < RackState::maxSendTaps ? in.tap : -1;
        // an automated route level (fader curve, like the knob)
        const auto automatedLevel = in.automation >= 0 ? automationValues[(std::size_t) in.automation] : std::numeric_limits<float>::quiet_NaN();
        const auto level = std::isnan (automatedLevel) ? in.level : faderGain (automatedLevel);
        const auto active = tap >= 0 ? tapActive[(std::size_t) tap] : trackActive[(std::size_t) in.source];
        const auto& delays = state.sendDelays[(std::size_t) in.source];
        auto* delay = (std::size_t) in.delay < delays.size() ? delays[(std::size_t) in.delay].get() : nullptr;
        if (pdcBypassNow && (tap < 0 || in.pre) && state.bypassWhenIdle[(std::size_t) in.source] != 0)
            delay = nullptr; // FL's "When recording or not playing": no compensation now
        auto* sl = tap >= 0 ? tapBuffer (tap, 0) : trackBuffer (in.source, 0);
        auto* sr = tap >= 0 ? tapBuffer (tap, 1) : trackBuffer (in.source, 1);

        if (delay != nullptr)
        {
            // PDC: this edge is shorter than the slowest one arriving here. A silent source
            // still drains the tail that was in the line.
            delay->mixInto (active ? sl : nullptr, active ? sr : nullptr, level, dl, dr, n);
            continue;
        }

        if (! active)
            continue;

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
    for (int i = 0; i < n; ++i)
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
}

void RackRenderer::processTrack (const RackState& state, int t, int n, MeterBank* meters) noexcept
{
    const auto& track = state.song.mixer[(std::size_t) t];
    pullInputs (state, t, n);

    // recording taps on this insert (round 168x, issue #41): FL's "Record" positions after the input
    const auto taps = liveInput != nullptr && t > 0 ? liveInput->tappedPoints (t) : 0u;
    auto tap = [&] (RecordSource point, const float* l, const float* r) {
        if ((taps & (1u << (unsigned) point)) != 0)
            liveInput->tap (t, point, l, r, liveInputOffset, n);
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
        const auto volume = state.hasAutomation ? automated (state.mixerVolumeAuto[(std::size_t) t], fader) : fader;
        const auto pan = state.hasAutomation ? automated (state.mixerPanAuto[(std::size_t) t], (knob + 1.0f) * 0.5f) * 2.0f - 1.0f : knob;
        level = mixerMatrix (track, volume, pan, state.song.panTriangular, liveLevel (state, &LiveLevels::mixerStereo, t, track.stereo));
        if ((std::size_t) t < state.vcaChains.size() && ! state.vcaChains[(std::size_t) t].empty()) // VCA faders (round 177a)
        {
            auto g = 1.0f;
            for (const auto v : state.vcaChains[(std::size_t) t])
            {
                const auto position = liveLevel (state, &LiveLevels::vcaLevel, v, state.song.vcas[(std::size_t) v].level);
                const auto automatedPosition = state.hasAutomation && (std::size_t) v < state.vcaLevelAuto.size()
                                                   ? automated (state.vcaLevelAuto[(std::size_t) v], position)
                                                   : position;
                g *= faderGain (automatedPosition);
            }
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

    auto* l = trackBuffer (t, 0);
    auto* r = trackBuffer (t, 1);

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
    if (track.effects.empty() && (state.song.cleanMixer || ! track.eq.active()) && isSilentBlock (l, r, n))
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
    applyTrackEq (track, t, state.song.cleanMixer, l, r, n);
    tap (RecordSource::postEq, l, r);
    if ((taps & (1u << (unsigned) RecordSource::postLevel)) != 0)
        if (const auto dest = liveInput->tapDestination (t, RecordSource::postLevel, liveInputOffset, n); dest.first != nullptr)
        {
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
    last = applyMatrix (l, r, l, r, n, from, target);
    tap (RecordSource::postTrack, l, r);

    trackActive[(std::size_t) t] = 1;

    if (meters != nullptr)
    {
        meters->accumulate (t, peakOf (l, n), peakOf (r, n));
        meters->capture.push (t, l, r, n);
    }
}
} // namespace lynx
