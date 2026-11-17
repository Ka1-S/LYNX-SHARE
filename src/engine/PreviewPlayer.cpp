#include "engine/PreviewPlayer.h"

#include <algorithm>

#include "core/BrowserSend.h"
#include "core/PreviewTempo.h"
#include "engine/Transport.h"

namespace lynx
{
PreviewPlayer::PreviewPlayer()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    kernel.setQuality (flrs::liveQuality);
}

void PreviewPlayer::play (std::shared_ptr<const SampleData> sample, bool loop, float newGain, bool forEditor)
{
    std::shared_ptr<const SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired); // whatever the audio thread released is freed here, on the UI thread
        pending = std::move (sample);
        pendingLoop = loop;
        pendingGain = newGain;
        pendingBrowser = false;
        pendingOnBeat = false;
        pendingEditor = forEditor;
        pendingStart = pendingEnd = 0;
        pendingFade = false;
        pendingScrub = false;
        hasPending = true;
    }
}

void PreviewPlayer::playScrub (std::shared_ptr<const SampleData> sample, std::int64_t startFrame, float newGain)
{
    std::shared_ptr<const SampleData> old;
    scrubSpeed.store (0.0, std::memory_order_relaxed); // (FL: a press starts paused)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired);
        pending = std::move (sample);
        pendingLoop = false;
        pendingGain = newGain;
        pendingBrowser = false;
        pendingOnBeat = false;
        pendingEditor = true;
        pendingStart = std::max<std::int64_t> (0, startFrame);
        pendingEnd = 0;
        pendingFade = false;
        pendingScrub = true;
        hasPending = true;
    }
}

void PreviewPlayer::setBrowserRange (std::int64_t startFrame, std::int64_t endFrame, bool fadeOut) noexcept
{
    nextStart = startFrame;
    nextEnd = endFrame;
    nextFade = fadeOut;
}

void PreviewPlayer::playBrowser (std::shared_ptr<const SampleData> sample, float newGain, bool onBeat)
{
    std::shared_ptr<const SampleData> old;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        old = std::move (retired);
        pendingStart = std::exchange (nextStart, 0); // (round 190iz: a range set for this preview only)
        pendingEnd = std::exchange (nextEnd, 0);
        pendingFade = std::exchange (nextFade, false);
        pending = std::move (sample);
        pendingLoop = false;
        pendingGain = newGain;
        pendingBrowser = true;
        pendingOnBeat = onBeat;
        pendingEditor = false;
        pendingScrub = false;
        hasPending = true;
    }
}

void PreviewPlayer::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    buffer.clear();

    {
        const juce::SpinLock::ScopedTryLockType sl (lock);

        if (sl.isLocked() && hasPending)
        {
            std::swap (current, pending); // the previous one goes to `pending` and then to `retired`
            retired = std::move (pending);
            hasPending = false;
            looping = pendingLoop;
            gain = pendingGain;
            browser = pendingBrowser;
            editor = pendingEditor;
            scrubbing = pendingScrub;
            waitingForBeat = pendingOnBeat;
            position = 0.0;
            endPosition = -1.0;
            fading = false;
            if (current != nullptr && current->frames() > 1) // round 190iz: the Browser's range (FL's 5-s preview, Shift+Click)
            {
                const auto frames = (std::int64_t) current->frames();
                position = (double) std::clamp<std::int64_t> (pendingStart, 0, frames - 1);
                if (pendingEnd > 0 && pendingEnd < frames)
                {
                    endPosition = (double) pendingEnd;
                    fading = pendingFade;
                }
            }
        }
    }

    // "Preview in time" (round 190aq): while the transport plays, the sample starts where the next beat is heard —
    // inside this block, or it keeps waiting; a stopped transport (or none) starts it at once
    int startAt = 0;
    if (waitingForBeat)
    {
        const auto* t = transport;
        if (t == nullptr || current == nullptr || ! t->playing.load (std::memory_order_relaxed))
            waitingForBeat = false;
        else
        {
            const auto wait = previewtempo::samplesToNextBeat (t->positionTicks.load (std::memory_order_relaxed), t->ticksPerSample(),
                                                               syncLatency.load (std::memory_order_relaxed));
            if (wait >= (double) buffer.getNumSamples())
            {
                playPosition.store (0.0, std::memory_order_relaxed); // (about to play: the Browser shows it as playing)
                return;
            }
            startAt = juce::jlimit (0, buffer.getNumSamples() - 1, (int) std::lround (wait));
            waitingForBeat = false;
            beatStartCount.fetch_add (1, std::memory_order_relaxed);
        }
    }

    if (current == nullptr || current->frames() < 2 || buffer.getNumChannels() < 2)
    {
        playPosition.store (-1.0, std::memory_order_relaxed);
        return;
    }

    const auto* l = current->leftData();
    const auto* r = current->rightData();
    const auto fileLast = (double) (current->frames() - 1);
    const auto last = endPosition > 0.0 && endPosition < fileLast ? endPosition : fileLast; // (round 190iz: a capped preview)
    const auto rate = current->sampleRate / deviceRate;
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    const auto level = gain * (browser ? volume.load (std::memory_order_relaxed) : editor ? editorVolume.load (std::memory_order_relaxed) : 1.0f);

    if (scrubbing) // round natives190kj: Edison's Scrub button — any speed, either way, held at the ends
    {
        const auto speed = rate * scrubSpeed.load (std::memory_order_relaxed);
        kernel.setSpeed (std::max (rate, std::abs (speed)));
        const auto half = (std::int64_t) kernel.halfWidth;
        const auto lastFrame = (std::int64_t) last;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (speed == 0.0)
                break; // (paused: silent)
            const auto whole = flrs::wholeFrames (position);
            const auto frac = position - (double) whole;
            for (std::int64_t t = 0; t < 2 * half; ++t)
            {
                const auto q = std::clamp<std::int64_t> (whole - half + 1 + t, 0, lastFrame);
                tapL[(std::size_t) t] = l[q];
                tapR[(std::size_t) t] = r[q];
            }
            float yl = 0.0f, yr = 0.0f;
            flrs::interpolate (kernel, tapL.data(), tapR.data(), frac, yl, yr);
            outL[i] = level * yl;
            outR[i] = level * yr;
            position = std::clamp (position + speed, 0.0, last);
        }
        playPosition.store (position, std::memory_order_relaxed);
        return;
    }

    // FL's sampler interpolation (24-point sinc, round 160) instead of the linear read it had: a 44.1 kHz file on a
    // 48 kHz device lost 1.5 dB at 10 kHz and had an image at −21 dBc (13.9 kHz) in the Browser and editor previews
    kernel.setSpeed (rate);
    const auto half = (std::int64_t) kernel.halfWidth;
    const auto lastFrame = (std::int64_t) last;
    for (int i = startAt; i < buffer.getNumSamples() && position < last; ++i)
    {
        const auto whole = flrs::wholeFrames (position);
        const auto frac = position - (double) whole;
        float yl = 0.0f, yr = 0.0f;
        if (rate == 1.0 && frac == 0.0)
        {
            yl = l[whole];
            yr = r[whole];
        }
        else if (whole - half + 1 >= 0 && whole + half <= lastFrame && ! looping)
            flrs::interpolate (kernel, l + (whole - half + 1), r + (whole - half + 1), frac, yl, yr);
        else
        {
            for (std::int64_t t = 0; t < 2 * half; ++t)
            {
                auto q = whole - half + 1 + t;
                if (looping && lastFrame > 0)
                    q = ((q % lastFrame) + lastFrame) % lastFrame;
                q = std::clamp<std::int64_t> (q, 0, lastFrame);
                tapL[(std::size_t) t] = l[q];
                tapR[(std::size_t) t] = r[q];
            }
            flrs::interpolate (kernel, tapL.data(), tapR.data(), frac, yl, yr);
        }
        auto g = level;
        if (fading) // round 190iz: a capped preview fades out over its last 10 ms (no click at the cut)
            g *= browserpreview::fadeGain ((last - position) / rate, browserpreview::fadeSamples (deviceRate));
        outL[i] = g * yl;
        outR[i] = g * yr;
        position += rate;

        if (looping && position >= last)
            position -= last;
    }

    playPosition.store (position < last ? position : -1.0, std::memory_order_relaxed);
}
} // namespace lynx
