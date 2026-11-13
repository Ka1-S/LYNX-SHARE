#include <juce_audio_formats/juce_audio_formats.h>

#include "engine/Metronome.h"

#include "core/Song.h"
#include "core/ValueMath.h"

namespace lynx
{
std::array<metronome::Sample, metronome::soundCount> Metronome::loadSounds (const juce::File& flDataFolder)
{
    std::array<metronome::Sample, metronome::soundCount> result;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    for (int i = 0; i < metronome::soundCount; ++i)
    {
        auto& s = result[(std::size_t) i];
        const auto file = flDataFolder == juce::File() ? juce::File()
                                                       : flDataFolder.getChildFile ("System").getChildFile (metronome::soundFileName (i));
        if (file.existsAsFile())
            if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr)
            {
                const auto frames = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) reader->sampleRate * 4);
                if (frames > 0 && reader->sampleRate > 0.0)
                {
                    juce::AudioBuffer<float> buffer ((int) juce::jmin (2u, juce::jmax (1u, reader->numChannels)), frames);
                    if (reader->read (&buffer, 0, frames, 0, true, buffer.getNumChannels() > 1))
                    {
                        s.rate = reader->sampleRate;
                        s.left.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + frames);
                        const auto* r = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0); // mono: the same on both sides
                        s.right.assign (r, r + frames);
                        s.fromFl = true;
                        continue;
                    }
                }
            }
        s = metronome::fallbackSound (i);
    }
    return result;
}

Metronome::Metronome (const Transport& t, const juce::File& flDataFolder)
    : transport (t), sounds (loadSounds (flDataFolder))
{
}

void Metronome::prepare (double sampleRate, int maximumBlockSize)
{
    voice.setOutputRate (sampleRate);
    voice.stop();
    lastBeat = -1;
    hasLastBeat = false;
    const auto size = (std::size_t) juce::jmax (maximumBlockSize, 8192);
    left.assign (size, 0.0f);
    right.assign (size, 0.0f);
}

bool Metronome::render (float* outLeft, float* outRight, int numSamples) noexcept
{
    std::fill (outLeft, outLeft + numSamples, 0.0f);
    std::fill (outRight, outRight + numSamples, 0.0f);

    const auto countIn = transport.countIn.load (std::memory_order_relaxed);
    const auto counting = countIn > 0.0 && ! transport.playing.load (std::memory_order_relaxed);

    if (! counting && (! transport.playing.load (std::memory_order_relaxed) || ! transport.metronome.load (std::memory_order_relaxed)))
    {
        lastBeat = -1;
        hasLastBeat = false;
        voice.stop();
        return false;
    }

    const auto beatsPerBar = juce::jmax (1, transport.beatsPerBar.load (std::memory_order_relaxed));
    const Meter meter (beatsPerBar, meterUnit (transport.beatUnit.load (std::memory_order_relaxed)));
    const auto songMode = transport.songMode.load (std::memory_order_relaxed);

    // in song mode, bars follow the Playlist time-signature changes (consistent copy
    // of the table; if the UI is writing, the previous block's copy is kept)
    if (songMode)
    {
        const auto before = transport.signatureVersion.load (std::memory_order_acquire);

        if ((before & 1u) == 0 && before != signatureVersion)
        {
            const auto count = juce::jlimit (0, Transport::maxSignatures, transport.signatureCount.load (std::memory_order_relaxed));
            decltype (signatures) copy {};

            for (int s = 0; s < count; ++s)
                copy[(std::size_t) s] = { transport.signatureTick[(std::size_t) s].load (std::memory_order_relaxed),
                                          transport.signatureLength[(std::size_t) s].load (std::memory_order_relaxed),
                                          transport.signatureBeat[(std::size_t) s].load (std::memory_order_relaxed) };

            if (transport.signatureVersion.load (std::memory_order_acquire) == before)
            {
                signatures = copy;
                signatureCount = count;
                signatureVersion = before;
            }
        }
    }

    // the song's meter from 0; in song mode the Playlist's signatures (their own beats); a count-in's ticks are
    // negative and run on the grid of the first one
    const metronome::Span base { 0, meter.bar(), meter.beat() };
    // during the count-in the clicks run on a position before the start (whole bars, so the beats line up)
    const auto startTick = transport.positionTicks.load (std::memory_order_relaxed) - (counting ? countIn : 0.0);
    const auto tps = transport.ticksPerSample();
    const auto level = controls::metronomeGain (transport.metronomeVolume.load (std::memory_order_relaxed));
    auto any = false;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto beat = metronome::beatAt (startTick + i * tps, signatures.data(), songMode ? signatureCount : 0, base);

        if (! hasLastBeat || beat.start != lastBeat)
        {
            lastBeat = beat.start;
            hasLastBeat = true;
            voice.trigger (&sound (transport.metronomeSound.load (std::memory_order_relaxed)), beat.downbeat);
        }

        if (voice.next (level, outLeft[i], outRight[i]))
            any = true;
    }
    return any;
}
} // namespace lynx
