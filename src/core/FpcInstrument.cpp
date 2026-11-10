#include "core/FpcInstrument.h"

#include "core/FlSamplers.h"
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

class FpcInstrument final : public Instrument
{
public:
    FpcInstrument (const ChannelDef& channel, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& samples)
    {
        for (auto& source : fpcChannelPads (channel))
        {
            std::vector<LoadedZone> zones;
            const auto def = drumPadChannel (channel, source, 0);
            for (const auto& z : def.zones)
                zones.push_back ({ z, samples ? samples (z.path) : nullptr });

            Pad pad;
            pad.sampler = makeZonedSampler (std::move (zones));
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
            p.sampler->prepare (sampleRate);
    }

    // UI thread: each pad's sampler takes the channel's settings as the pad's own channel would
    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        for (auto& p : pads)
        {
            const auto def = drumPadChannel (channel, p.source, 0);
            p.sampler->configure (def, bpm);
            p.pitch.store (def.sampler.pitch, std::memory_order_relaxed);
        }
        channelPitch.store (channel.sampler.pitch, std::memory_order_relaxed);
        for (std::size_t k = 0; k < outputOffsets.size(); ++k)
            outputTracks[k].store (std::clamp (channel.mixerTrack + outputOffsets[k], 0, maxMixerTracks - 1), std::memory_order_relaxed);
    }

    void settleNew() override
    {
        for (auto& p : pads)
            p.sampler->settleNew();
    }

    void setFastDeclick (bool fast) noexcept override
    {
        for (auto& p : pads)
            p.sampler->setFastDeclick (fast);
    }

    void setPanTriangular (bool triangular) noexcept override
    {
        for (auto& p : pads)
            p.sampler->setPanTriangular (triangular);
    }

    void setSongPosition (double ticks) noexcept override
    {
        for (auto& p : pads)
            p.sampler->setSongPosition (ticks);
    }

    bool idle() const noexcept override
    {
        if (pendingCount > 0)
            return false;
        for (const auto& p : pads)
            if (! p.sampler->idle())
                return false;
        return true;
    }

    // ---- notes ----
    void setNextNoteExpression (float pan, float cents, bool porta) noexcept override { next.pan = pan, next.cents = cents, next.porta = porta; }
    void setNextNoteModulation (float modX, float modY) noexcept override { next.modX = modX, next.modY = modY; }
    void setNextNoteFinePitch (float cents) noexcept override { next.fine = cents; }
    void setNextNoteRelease (float velocity) noexcept override { nextRelease = velocity; }
    bool setNextNoteGain (float gain) noexcept override
    {
        next.gain = gain;
        return true;
    }

    void noteOn (int key, float velocity) noexcept override { noteOnAt (key, velocity, 0.0); }

    // A note: every pad on its key. The pads it cuts stop now; the pads it starts start with the next audio, after
    // every cut of this sample — the order the rack gives a channel per pad (its cut groups' "choke" events come before
    // the notes of the same sample), so two pads of one group hit together both sound.
    void noteOnAt (int key, float velocity, double startSeconds) noexcept override
    {
        for (std::size_t i = 0; i < pads.size(); ++i)
        {
            const auto& p = pads[i];
            if (p.source.key != key)
                continue;

            if (p.source.cutGroup > 0)
                for (auto& other : pads)
                    if (other.source.cutBy == p.source.cutGroup)
                        other.sampler->cut();

            if (pendingCount == (int) pending.size())
                startPending();
            auto& note = pending[(std::size_t) pendingCount++];
            note = next;
            note.pad = (int) i;
            note.velocity = velocity;
            note.startSeconds = startSeconds;
            note.glide = HostSlide::pendingGlide();
        }
        next = {};
    }

    void noteOff (int key) noexcept override
    {
        startPending();
        for (auto& p : pads)
            if (p.source.key == key)
            {
                p.sampler->setNextNoteRelease (nextRelease);
                p.sampler->noteOff (padKey);
            }
    }

    void allNotesOff() noexcept override
    {
        pendingCount = 0;
        for (auto& p : pads)
            p.sampler->allNotesOff();
    }

    void cut() noexcept override
    {
        startPending();
        for (auto& p : pads)
            p.sampler->cut();
    }

    void transportStopped() noexcept override
    {
        startPending();
        for (auto& p : pads)
            p.sampler->transportStopped();
    }

    // (FPC is a plugin: FL's "Reset plugins on transport" stops it — the base class's transportReset() is panic())
    void panic() noexcept override
    {
        pendingCount = 0;
        for (auto& p : pads)
            p.sampler->panic();
    }

    void voiceProperty (int key, int prop, float value) noexcept override
    {
        startPending();
        for (auto& p : pads)
            if (p.source.key == key)
                p.sampler->voiceProperty (padKey, prop, value);
    }

    void controlChange (int cc, float value) noexcept override
    {
        for (auto& p : pads)
            p.sampler->controlChange (cc, value);
    }

    // ---- pitch: the channel's knob moves every pad around its own tuning ----
    void pitchBend (float semitones) noexcept override
    {
        for (auto& p : pads)
            p.sampler->pitchBend (semitones);
    }

    void setChannelPitchNow (float knob, float rest) noexcept override
    {
        // the knob as configure() saw it: the pad's own pitch as its channel has it; a knob that moved since (live,
        // automated): the pad's tuning around it
        const auto configured = knob == channelPitch.load (std::memory_order_relaxed) * 100.0f;
        for (auto& p : pads)
            p.sampler->setChannelPitchNow (configured ? p.pitch.load (std::memory_order_relaxed) * 100.0f
                                                      : std::clamp (knob / 100.0f + p.source.tune, -24.0f, 24.0f) * 100.0f,
                                           rest);
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

            if (p.output < 0)
                p.sampler->render (left, right, n, gl, gr);
            else if (room)
                p.sampler->render (outputs[(std::size_t) p.output].data[0].data() + cursor, outputs[(std::size_t) p.output].data[1].data() + cursor, n, gl, gr);
        }

        cursor += n;
    }

private:
    static constexpr int padKey = 60; // a pad's sampler plays at the sample's own pitch: its root key
    static constexpr std::size_t outputCapacity = 16384;

    struct Pad
    {
        DrumPad source;
        std::unique_ptr<Instrument> sampler;
        int output = -1; // −1 = the channel's insert, else the extra output
        std::atomic<float> pitch { 0.0f }; // its sampler's PITCH (semitones): the channel's + the pad's tuning

        Pad() = default;
        Pad (Pad&& o) noexcept : source (std::move (o.source)), sampler (std::move (o.sampler)), output (o.output), pitch (o.pitch.load()) {}
    };

    struct Note
    {
        int pad = 0;
        float velocity = 0.0f;
        double startSeconds = 0.0;
        float pan = 0.0f, cents = 0.0f, fine = 0.0f;
        bool porta = false;
        float modX = 0.5f, modY = 0.5f, gain = 1.0f;
        HostSlide::GlideIn glide {};
    };

    struct Output
    {
        std::array<std::vector<float>, 2> data;
    };

    void startPending() noexcept
    {
        if (pendingCount == 0)
            return;
        const auto held = HostSlide::pendingGlide();
        for (int i = 0; i < pendingCount; ++i)
        {
            const auto& note = pending[(std::size_t) i];
            auto& sampler = *pads[(std::size_t) note.pad].sampler;
            sampler.setNextNoteExpression (note.pan, note.cents, note.porta);
            HostSlide::pendingGlide() = note.glide;
            sampler.setNextNoteModulation (note.modX, note.modY);
            if (note.gain != 1.0f)
                sampler.setNextNoteGain (note.gain);
            sampler.setNextNoteFinePitch (note.fine);
            sampler.noteOnAt (padKey, note.velocity, note.startSeconds);
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
    std::array<Note, 64> pending {};
    int pendingCount = 0;
    Note next {};
    float nextRelease = 0.5f;
    float channelPan = 0.0f, channelVolume = 1.0f;
    bool haveLevels = false; // the rack gives the levels before every chunk; other callers (a Patcher) never do
    int cursor = 0;
};
} // namespace

std::unique_ptr<Instrument> makeFpcInstrument (const ChannelDef& channel,
                                               const std::function<std::shared_ptr<const SampleData> (const std::string& path)>& samples)
{
    return std::make_unique<FpcInstrument> (channel, samples);
}
} // namespace lynx
