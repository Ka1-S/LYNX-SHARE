#include "core/FpcInstrument.h"

#include "core/FlSamplers.h"
#include "core/Fpc.h"
#include "core/FpcEnvelope.h"
#include "core/FlpImport.h"
#include "core/HostSlide.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace lynx
{
namespace
{
// the rack's channel gains (RackRenderer panGains): constant power, both sides 1 at the centre
void channelGains (float pan, float gain, float& left, float& right) noexcept
{
    const auto angle = (pan + 1.0f) * 0.25f * 3.14159265f;
    left = gain * std::cos (angle) * 1.41421356f;
    right = gain * std::sin (angle) * 1.41421356f;
}

// A layer's voices as FPC renders them (round 190z, REA_rodada190z §1–§3; FPC_x64.dll FUN_0086a2d0): on a tick of
// FpcEnvelope::ticksFor samples counted from the voice's start, the volume envelope's level and the pan (the pan
// envelope's level × 2 + the layer's pan, within −1…1, through the pan law) set the voice's gains, which ramp to them
// linearly over the tick (the first tick jumps). A choke, or the volume envelope's end, sends them to 0 over one tick
// and the voice ends there. Without envelopes, layer pan and choke the gains stay exactly 1.
class LayerShaper final : public VoiceShaper
{
public:
    LayerShaper (const FpcEnvelope& volume, const FpcEnvelope& pan, int layerPan)
        : vol (volume), pn (pan), volOn (volume.enabled()), panOn (pan.enabled()),
          layerPan ((float) std::clamp (layerPan, -128, 128) / 128.0f)
    {
    }

    void prepare (double rate) noexcept
    {
        tick = FpcEnvelope::ticksFor (rate);
        scale = FpcEnvelope::scaleFor (rate);
    }

    // FPC's choke: every voice playing now goes to 0 over its next tick
    void cutAll() noexcept
    {
        for (auto& s : slots)
            s.cut = true;
    }

    void voiceStarted (int voice) noexcept override
    {
        if (voice < 0 || voice >= (int) slots.size())
            return;
        auto& s = slots[(std::size_t) voice];
        s = {};
        if (volOn)
        {
            vol.start (s.vol, scale);
            s.level = s.vol.out;
        }
        if (panOn)
        {
            pn.start (s.pan, scale);
            s.panLevel = s.pan.out * 2.0f;
        }
    }

    void voiceReleased (int voice) noexcept override
    {
        if (voice < 0 || voice >= (int) slots.size())
            return;
        auto& s = slots[(std::size_t) voice];
        if (volOn)
            vol.release (s.vol);
        if (panOn)
            pn.release (s.pan);
    }

    bool voiceGains (int voice, float& left, float& right) noexcept override
    {
        if (voice < 0 || voice >= (int) slots.size())
            return true;
        auto& s = slots[(std::size_t) voice];
        if (s.left <= 0)
        {
            if (s.ending)
                return false;
            newTick (s);
        }
        left = s.gain[0];
        right = s.gain[1];
        s.gain[0] += s.step[0];
        s.gain[1] += s.step[1];
        --s.left;
        return true;
    }

private:
    struct Slot
    {
        FpcEnvelope::State vol, pan;
        float level = 1.0f, panLevel = 0.0f;
        float gain[2] { 1.0f, 1.0f }, step[2] { 0.0f, 0.0f };
        int left = 0;
        bool first = true, cut = false, finished = false, ending = false;
    };

    void newTick (Slot& s) noexcept
    {
        s.left = tick;
        if (volOn)
        {
            s.level = s.vol.out;
            vol.step (s.vol, scale, 1.0);
            if (s.vol.finished())
                s.finished = ! (panOn && ! s.pan.finished());
        }
        if (panOn)
        {
            s.panLevel = s.pan.out * 2.0f;
            pn.step (s.pan, scale, 1.0);
        }
        float target[2] { s.level, s.level };
        if (const auto p = std::clamp (s.panLevel + layerPan, -1.0f, 1.0f); p != 0.0f)
        {
            float l, r;
            channelGains (p, 1.0f, l, r);
            target[0] *= l;
            target[1] *= r;
        }
        if (s.cut || s.finished)
        {
            target[0] = target[1] = 0.0f;
            s.ending = true;
        }
        for (int k = 0; k < 2; ++k)
        {
            if (s.first)
                s.gain[k] = target[k], s.step[k] = 0.0f;
            else
                s.step[k] = (target[k] - s.gain[k]) / (float) tick;
        }
        s.first = false;
    }

    const FpcEnvelope& vol;
    const FpcEnvelope& pn;
    const bool volOn, panOn;
    const float layerPan;
    int tick = 48;
    double scale = FpcEnvelope::scaleFor (44100.0);
    std::array<Slot, 16> slots {};
};

class FpcInstrument final : public Instrument
{
public:
    FpcInstrument (const ChannelDef& channel, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& samples)
    {
        const auto kit = fpc::read (channel.pluginState);
        for (auto& source : fpcChannelPads (channel))
        {
            Pad pad;
            const fpc::Pad* stored = kit && source.sourcePad >= 0 && source.sourcePad < (int) kit->pads.size() ? &kit->pads[(std::size_t) source.sourcePad] : nullptr;
            if (stored != nullptr)
            {
                pad.oneAtATime = stored->oneLayerAtATime();
                pad.mode = stored->trigger[0];
                for (const auto& e : stored->envelopes)
                    (e.index == 1 ? pad.pan : pad.volume) = FpcEnvelope (e, e.index == 1);
                for (const auto& l : stored->layers)
                {
                    Layer layer;
                    layer.low = l.velocityLow;
                    layer.high = l.velocityHigh;
                    layer.pan = l.pan;
                    layer.scaleVolume = (l.flags & 1) != 0;
                    layer.reverse = (l.flags & 2) != 0;
                    pad.layers.push_back (std::move (layer));
                }
            }

            // the layers with a file: each its own one-zone sampler with the zone the pad's channel would have (the pad's
            // and the layer's gain on it), configured as that channel with its layer
            const auto def = drumPadChannel (channel, source, 0);
            for (std::size_t k = 0; k < source.layers.size() && k < def.zones.size(); ++k)
            {
                auto at = k < source.sourceLayers.size() ? source.sourceLayers[k] : -1;
                if (at < 0 || at >= (int) pad.layers.size())
                {
                    // (no kit to read: the layer covers its own velocities)
                    Layer layer;
                    layer.low = source.layers[k].lowVelocity;
                    layer.high = source.layers[k].highVelocity >= 127 ? 127 : source.layers[k].highVelocity + 1;
                    pad.layers.push_back (std::move (layer));
                    at = (int) pad.layers.size() - 1;
                }
                auto& layer = pad.layers[(std::size_t) at];
                layer.zone = def.zones[k];
                layer.zone.lowVelocity = 0, layer.zone.highVelocity = 127; // (FPC picks the layers)
                std::vector<LoadedZone> zones;
                zones.push_back ({ layer.zone, samples ? samples (layer.zone.path) : nullptr });
                layer.sampler = makeZonedSampler (std::move (zones));
            }
            pad.source = std::move (source);

            if (pad.source.mixerOffset != 0)
            {
                auto it = std::find (outputOffsets.begin(), outputOffsets.end(), pad.source.mixerOffset);
                if (it == outputOffsets.end())
                    it = outputOffsets.insert (outputOffsets.end(), pad.source.mixerOffset);
                pad.output = (int) (it - outputOffsets.begin());
            }

            pads.push_back (std::move (pad));
        }

        // (the shapers read their pad's envelopes: made once the pads stay where they are). A layer without envelope,
        // layer pan or a group that can cut it needs none: its gains stay 1 (the sampler's own path, nothing per sample)
        for (auto& p : pads)
            for (auto& l : p.layers)
                if (l.sampler != nullptr && (p.volume.enabled() || p.pan.enabled() || l.pan != 0 || p.source.cutBy > 0))
                {
                    l.shaper = std::make_unique<LayerShaper> (p.volume, p.pan, l.pan);
                    l.sampler->setVoiceShaper (l.shaper.get());
                }

        outputs.resize (outputOffsets.size());
        for (auto& o : outputs)
            for (auto& side : o.data)
                side.assign (outputCapacity, 0.0f);
        outputTracks = std::make_unique<std::atomic<int>[]> (std::max<std::size_t> (1, outputOffsets.size()));
        for (std::size_t k = 0; k < outputOffsets.size(); ++k)
            outputTracks[k].store (-1, std::memory_order_relaxed);
    }

    void prepare (double sampleRate) noexcept override
    {
        for (auto& p : pads)
            for (auto& l : p.layers)
                if (l.sampler != nullptr)
                {
                    l.sampler->prepare (sampleRate);
                    if (l.shaper != nullptr)
                        l.shaper->prepare (sampleRate);
                }
    }

    // UI thread: each layer's sampler takes the channel's settings as the pad's own channel would, with its layer
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        for (auto& p : pads)
        {
            const auto def = drumPadChannel (channel, p.source, 0);
            for (auto& l : p.layers)
                if (l.sampler != nullptr)
                    l.sampler->configure (layerChannel (def, l), bpm);
            p.pitch.store (def.sampler.pitch, std::memory_order_relaxed);
        }
        channelPitch.store (channel.sampler.pitch, std::memory_order_relaxed);
        for (std::size_t k = 0; k < outputOffsets.size(); ++k)
            outputTracks[k].store (std::clamp (channel.mixerTrack + outputOffsets[k], 0, maxMixerTracks - 1), std::memory_order_relaxed);
    }

    void settleNew() override
    {
        forEachSampler ([] (Instrument& s) { s.settleNew(); });
    }

    void setFastDeclick (bool fast) noexcept override
    {
        forEachSampler ([fast] (Instrument& s) { s.setFastDeclick (fast); });
    }

    void setPanTriangular (bool triangular) noexcept override
    {
        forEachSampler ([triangular] (Instrument& s) { s.setPanTriangular (triangular); });
    }

    void setSongPosition (double ticks) noexcept override
    {
        forEachSampler ([ticks] (Instrument& s) { s.setSongPosition (ticks); });
    }

    bool idle() const noexcept override
    {
        if (pendingCount > 0)
            return false;
        for (const auto& p : pads)
            for (const auto& l : p.layers)
                if (l.sampler != nullptr && ! l.sampler->idle())
                    return false;
        return true;
    }

    // ---- notes ----
    void setNextNoteExpression (float pan, float cents, bool porta) noexcept override { next.pan = pan, next.cents = cents, next.porta = porta; }
    void setNextNoteModulation (float, float) noexcept override {} // (FPC's voices take no filter: FL's Mod X / Y do nothing)
    void setNextNoteFinePitch (float cents) noexcept override { next.fine = cents; }
    void setNextNoteRelease (float velocity) noexcept override { nextRelease = velocity; }
    bool setNextNoteGain (float gain) noexcept override
    {
        next.gain = gain;
        return true;
    }

    void noteOn (int key, float velocity) noexcept override { noteOnAt (key, velocity, 0.0); }

    // A note: every pad on its key (FPC's TriggerVoice, REA_rodada9 §1.3 / REA_rodada190z §3). The pads it cuts go to 0
    // over their next tick; the layers it starts start with the next audio, after every cut of this sample, so two pads
    // of one group hit together both sound. A pad plays the layers whose velocity range holds the note's (low ≤ v < high,
    // 127 in a range that ends at 127), all of them, or one picked by the pad's mode when it plays one at a time.
    void noteOnAt (int key, float velocity, double startSeconds) noexcept override
    {
        const auto vel = std::clamp ((int) std::nearbyint (std::clamp (velocity, 0.0f, 1.0f) * 128.0f), 0, 127);
        for (std::size_t i = 0; i < pads.size(); ++i)
        {
            auto& p = pads[i];
            if (p.source.key != key)
                continue;

            if (p.source.cutGroup > 0)
                for (auto& other : pads)
                    if (other.source.cutBy == p.source.cutGroup)
                        for (auto& l : other.layers)
                            if (l.shaper != nullptr)
                                l.shaper->cutAll();

            auto first = 0, last = (int) p.layers.size() - 1;
            if (p.oneAtATime && first < last)
                if (const auto chosen = choose (p, vel); chosen >= 0)
                    first = last = chosen;

            for (auto k = first; k <= last; ++k)
            {
                const auto& l = p.layers[(std::size_t) k];
                if (l.sampler == nullptr || ! covers (l, vel))
                    continue;
                if (pendingCount == (int) pending.size())
                    startPending();
                auto& note = pending[(std::size_t) pendingCount++];
                note = next;
                note.pad = (int) i;
                note.layer = k;
                note.velocity = velocity;
                note.startSeconds = startSeconds;
                note.glide = HostSlide::pendingGlide();
                note.noteId = startingNoteId();
            }
        }
        next = {};
    }

    void noteOff (int key) noexcept override
    {
        startPending();
        for (auto& p : pads)
            if (p.source.key == key)
                for (auto& l : p.layers)
                    if (l.sampler != nullptr)
                    {
                        l.sampler->setNextNoteRelease (nextRelease);
                        // (round 190ej: the note's own layer voices, as the channel sampler ends them)
                        l.sampler->setNextNoteOffId (endingNoteId());
                        l.sampler->noteOff (padKey);
                        l.sampler->setNextNoteOffId (0);
                    }
    }

    void allNotesOff() noexcept override
    {
        pendingCount = 0;
        forEachSampler ([] (Instrument& s) { s.allNotesOff(); });
    }

    void cut() noexcept override
    {
        startPending();
        forEachSampler ([] (Instrument& s) { s.cut(); });
    }

    void transportStopped() noexcept override
    {
        startPending();
        forEachSampler ([] (Instrument& s) { s.transportStopped(); });
    }

    // (FPC is a plugin: FL's "Reset plugins on transport" stops it — the base class's transportReset() is panic())
    void panic() noexcept override
    {
        pendingCount = 0;
        forEachSampler ([] (Instrument& s) { s.panic(); });
    }

    void voiceProperty (int key, int prop, float value) noexcept override
    {
        startPending();
        for (auto& p : pads)
            if (p.source.key == key)
                for (auto& l : p.layers)
                    if (l.sampler != nullptr)
                        l.sampler->voiceProperty (padKey, prop, value);
    }

    void controlChange (int cc, float value) noexcept override
    {
        forEachSampler ([cc, value] (Instrument& s) { s.controlChange (cc, value); });
    }

    // ---- pitch: the channel's knob moves every pad around its own tuning ----
    void pitchBend (float semitones) noexcept override
    {
        forEachSampler ([semitones] (Instrument& s) { s.pitchBend (semitones); });
    }

    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        // the knob as configure() saw it: the pad's own pitch as its channel has it; a knob that moved since (live,
        // automated): the pad's tuning around it
        const auto configured = knob == channelPitch.load (std::memory_order_relaxed) * 100.0f;
        for (auto& p : pads)
        {
            const auto cents = configured ? p.pitch.load (std::memory_order_relaxed) * 100.0f
                                          : std::clamp (knob / 100.0f + p.source.tune, -24.0f, 24.0f) * 100.0f;
            for (auto& l : p.layers)
                if (l.sampler != nullptr)
                    l.sampler->setChannelPitchNow (cents, rest);
        }
    }

    // ---- audio ----
    void setChannelLevels (float pan, float volume) noexcept override
    {
        channelPan = pan;
        channelVolume = volume;
        haveLevels = true;
    }

    void beginChunk (int n) noexcept override
    {
        cursor = 0;
        for (auto& o : outputs)
            for (auto& side : o.data)
                std::fill (side.begin(), side.begin() + std::clamp<std::ptrdiff_t> (n, 0, (std::ptrdiff_t) outputCapacity), 0.0f);
    }

    int auxOutputCount() const noexcept override { return (int) outputs.size(); }
    int auxOutputTrack (int k) const noexcept override
    {
        return k >= 0 && k < (int) outputs.size() ? outputTracks[(std::size_t) k].load (std::memory_order_relaxed) : -1;
    }
    const float* auxOutputData (int k, int side) const noexcept override
    {
        return k >= 0 && k < (int) outputs.size() ? outputs[(std::size_t) k].data[(std::size_t) (side & 1)].data() : nullptr;
    }

    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override
    {
        startPending();

        // the channel's pan and volume behind the gains: the rack's (setChannelLevels), else read back from the gains
        auto pan = channelPan, volume = channelVolume;
        if (! haveLevels)
        {
            volume = std::sqrt ((gainLeft * gainLeft + gainRight * gainRight) * 0.5f);
            pan = volume > 0.0f ? std::atan2 (gainRight, gainLeft) / (0.25f * 3.14159265f) - 1.0f : 0.0f;
        }

        const auto room = cursor + n <= (int) outputCapacity;

        for (auto& p : pads)
        {
            auto gl = gainLeft, gr = gainRight;
            if (p.source.pan != 0.0f)
                channelGains (std::clamp (pan + p.source.pan, -1.0f, 1.0f), volume, gl, gr);

            for (auto& l : p.layers)
            {
                if (l.sampler == nullptr || l.sampler->idle()) // (an idle layer has nothing to add: 8-layer kits stay cheap)
                    continue;
                if (p.output < 0)
                    l.sampler->render (left, right, n, gl, gr);
                else if (room)
                    l.sampler->render (outputs[(std::size_t) p.output].data[0].data() + cursor, outputs[(std::size_t) p.output].data[1].data() + cursor, n, gl, gr);
            }
        }

        cursor += n;
    }

private:
    static constexpr int padKey = 60; // a pad's sampler plays at the sample's own pitch: its root key
    static constexpr std::size_t outputCapacity = 16384;

    struct Layer
    {
        int low = 0, high = 127; // FL's range: low ≤ velocity < high (a high of 127 includes 127)
        int pan = 0;             // −128…128
        bool scaleVolume = true; // "Scale volume with velocity" (layer flag bit 0)
        bool reverse = false;    // layer flag bit 1
        SampleZone zone;
        std::unique_ptr<Instrument> sampler; // null: the layer has no file (it can be chosen, and is silent)
        std::unique_ptr<LayerShaper> shaper;
    };

    struct Pad
    {
        DrumPad source;
        std::vector<Layer> layers; // every layer of the kit's pad, in its order
        bool oneAtATime = false;
        int mode = 1;              // LayerMode: 0 Random, 1 Random (avoid previous), 2 Cycle backward, 3 Cycle
        int previous = -1;         // the layer picked last
        FpcEnvelope volume, pan;
        int output = -1; // −1 = the channel's insert, else the extra output
        std::atomic<float> pitch { 0.0f }; // its samplers' PITCH (semitones): the channel's + the pad's tuning

        Pad() = default;
        Pad (Pad&& o) noexcept
            : source (std::move (o.source)), layers (std::move (o.layers)), oneAtATime (o.oneAtATime), mode (o.mode), previous (o.previous),
              volume (std::move (o.volume)), pan (std::move (o.pan)), output (o.output), pitch (o.pitch.load())
        {
        }
    };

    struct Note
    {
        int pad = 0, layer = 0;
        float velocity = 0.0f;
        double startSeconds = 0.0;
        float pan = 0.0f, cents = 0.0f, fine = 0.0f;
        bool porta = false;
        float gain = 1.0f;
        HostSlide::GlideIn glide {};
        std::uint32_t noteId = 0; // the scheduled note it belongs to (round 190ej)
    };

    struct Output
    {
        std::array<std::vector<float>, 2> data;
    };

    static bool covers (const Layer& l, int vel) noexcept
    {
        return l.low <= vel && (vel < l.high || (vel == 127 && l.high == 127));
    }

    // the layer's channel: the pad's, with this layer as its only zone (any velocity: FPC picks the layers), reversed
    // with the layer's flag; FL's velocity law only when the layer scales its volume with the velocity
    static ChannelDef layerChannel (const ChannelDef& pad, const Layer& l)
    {
        auto def = pad;
        def.zones.clear();
        auto z = l.zone;
        z.rootKey = 60, z.lowKey = 0, z.highKey = 127, z.lowVelocity = 0, z.highVelocity = 127;
        def.zones.push_back (z);
        def.samplePath = z.path;
        def.sampler.reverse = l.reverse;
        def.sampler.flVelocityLaw = l.scaleVolume;
        return def;
    }

    // FPC's pick among the layers that hold the velocity (FUN_00869db0): Random draws one, Random (avoid previous)
    // draws among the others (with no previous one among them, never the last), Cycle backward / Cycle step from the
    // previous one, wrapping; −1 when none holds it
    int choose (Pad& p, int vel) noexcept
    {
        std::array<int, 128> holding {};
        int count = 0, previousAt = -1;
        for (int k = 0; k < (int) p.layers.size() && count < (int) holding.size(); ++k)
            if (covers (p.layers[(std::size_t) k], vel))
            {
                if (k == p.previous)
                    previousAt = count;
                holding[(std::size_t) count++] = k;
            }
        int pick = previousAt;
        if (count == 1)
            pick = 0;
        else if (count > 0)
        {
            if (p.mode == 0)
                pick = (int) ((double) random() * 2.3283064365386963e-10 * (double) count);
            else if (p.mode == 1)
            {
                pick = (int) ((double) random() * 2.3283064365386963e-10 * (double) (count - 1));
                if (pick >= previousAt && previousAt >= 0 && previousAt < count - 1)
                    ++pick;
            }
            else if (p.mode == 2)
            {
                pick = previousAt - 1;
                if (pick < 0)
                    pick = count - 1;
            }
            else if (p.mode == 3)
            {
                pick = previousAt + 1;
                if (pick >= count)
                    pick = 0;
            }
        }
        if (pick < 0)
            return -1;
        p.previous = holding[(std::size_t) pick];
        return p.previous;
    }

    std::uint32_t random() noexcept
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    }

    template <typename Fn>
    void forEachSampler (Fn&& fn) noexcept
    {
        for (auto& p : pads)
            for (auto& l : p.layers)
                if (l.sampler != nullptr)
                    fn (*l.sampler);
    }

    void startPending() noexcept
    {
        if (pendingCount == 0)
            return;
        const auto held = HostSlide::pendingGlide();
        for (int i = 0; i < pendingCount; ++i)
        {
            const auto& note = pending[(std::size_t) i];
            auto& layer = pads[(std::size_t) note.pad].layers[(std::size_t) note.layer];
            auto& sampler = *layer.sampler;
            sampler.setNextNoteExpression (note.pan, note.cents, note.porta);
            HostSlide::pendingGlide() = note.glide;
            if (note.gain != 1.0f)
                sampler.setNextNoteGain (note.gain);
            sampler.setNextNoteFinePitch (note.fine);
            // (a layer that does not scale its volume with the velocity: FPC divides the velocity's level out)
            sampler.setNextNoteId (note.noteId);
            sampler.noteOnAt (padKey, layer.scaleVolume ? note.velocity : 1.0f, note.startSeconds);
            sampler.setNextNoteId (0);
        }
        HostSlide::pendingGlide() = held;
        pendingCount = 0;
    }

    std::vector<Pad> pads;
    std::vector<int> outputOffsets; // the distinct output offsets of the pads (not 0)
    std::vector<Output> outputs;
    std::unique_ptr<std::atomic<int>[]> outputTracks;
    std::atomic<float> channelPitch { 0.0f };

    // audio thread
    std::array<Note, 256> pending {};
    int pendingCount = 0;
    Note next {};
    float nextRelease = 0.5f;
    float channelPan = 0.0f, channelVolume = 1.0f;
    bool haveLevels = false; // the rack gives the levels before every chunk; other callers (a Patcher) never do
    int cursor = 0;
    std::uint32_t seed = 0x2545F491u;
};
} // namespace

std::unique_ptr<Instrument> makeFpcInstrument (const ChannelDef& channel,
                                               const std::function<std::shared_ptr<const SampleData> (const std::string& path)>& samples)
{
    return std::make_unique<FpcInstrument> (channel, samples);
}
} // namespace lynx
