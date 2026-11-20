#include "engine/NcwFormat.h"

#include "core/Ncw.h"

namespace lynx
{
namespace
{
class NcwReader final : public juce::AudioFormatReader
{
public:
    explicit NcwReader (ncw::Decoded&& decoded) : juce::AudioFormatReader (nullptr, "NCW"), audio (std::move (decoded))
    {
        sampleRate = audio.info.sampleRate;
        bitsPerSample = audio.info.bits;
        usesFloatingPointData = true;
        numChannels = (unsigned int) audio.channels.size();
        lengthInSamples = audio.channels.empty() ? 0 : (juce::int64) audio.channels.front().size();
    }

    bool readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile,
                      int numSamples) override
    {
        for (int c = 0; c < numDestChannels; ++c)
        {
            if (destChannels[c] == nullptr)
                continue;
            auto* dest = reinterpret_cast<float*> (destChannels[c]) + startOffsetInDestBuffer;
            const auto& source = audio.channels[(std::size_t) juce::jmin (c, (int) audio.channels.size() - 1)];
            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = startSampleInFile + i;
                dest[i] = index >= 0 && index < (juce::int64) source.size() ? source[(std::size_t) index] : 0.0f;
            }
        }
        return true;
    }

private:
    ncw::Decoded audio;
};
} // namespace

NcwAudioFormat::NcwAudioFormat() : juce::AudioFormat ("NCW", ".ncw")
{
}

juce::AudioFormatReader* NcwAudioFormat::createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails)
{
    std::unique_ptr<juce::InputStream> owned (source);
    auto fail = [&]() -> juce::AudioFormatReader* {
        if (! deleteStreamIfOpeningFails && source != nullptr)
        {
            source->setPosition (0); // the caller will try another format with the same stream
            owned.release();
        }
        return nullptr;
    };
    if (source == nullptr)
        return fail();
    // the signature first, so other files are not read whole (a sample of a few hundred MB at most)
    const auto start = source->getPosition();
    std::uint8_t head[120] {};
    if (source->read (head, (int) sizeof (head)) != (int) sizeof (head) || ! ncw::isNcw (head, sizeof (head))
        || source->getTotalLength() > ((juce::int64) 1 << 31))
        return fail();
    source->setPosition (start);
    juce::MemoryBlock bytes;
    source->readIntoMemoryBlock (bytes);
    ncw::Decoded decoded;
    if (! ncw::decode (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize(), decoded) || decoded.channels.empty())
        return fail();
    return new NcwReader (std::move (decoded)); // the stream has been fully read: it can go
}
} // namespace lynx
