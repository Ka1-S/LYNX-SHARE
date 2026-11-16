#pragma once

// Native Instruments' NCW (Kontakt's lossless compressed WAV) for JUCE: read-only, decodes the whole file on open with
// core/Ncw.h (round 190ii, issue #149). NCW is compression only; encrypted Kontakt content is never read.

#include <juce_audio_formats/juce_audio_formats.h>

namespace lynx
{
class NcwAudioFormat final : public juce::AudioFormat
{
public:
    NcwAudioFormat();

    juce::Array<int> getPossibleSampleRates() override { return {}; }
    juce::Array<int> getPossibleBitDepths() override { return {}; }
    bool canDoStereo() override { return true; }
    bool canDoMono() override { return true; }

    juce::AudioFormatReader* createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails) override;
    juce::AudioFormatWriter* createWriterFor (juce::OutputStream*, double, unsigned int, int, const juce::StringPairArray&, int) override
    {
        return nullptr; // read-only
    }
    using juce::AudioFormat::createWriterFor;
};
} // namespace lynx
