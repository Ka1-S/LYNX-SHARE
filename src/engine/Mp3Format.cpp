#include "engine/Mp3Format.h"

#include "core/Mp3Decoder.h"
#include "engine/NcwFormat.h"

namespace lynx
{
namespace
{
class Mp3Reader final : public juce::AudioFormatReader
{
public:
    explicit Mp3Reader (mp3::Decoded&& decoded)
        : juce::AudioFormatReader (nullptr, Mp3AudioFormat::formatName),
          audio (std::move (decoded))
    {
        sampleRate = audio.info.sampleRate;
        bitsPerSample = 32;
        usesFloatingPointData = true;
        numChannels = audio.right.empty() ? 1u : 2u;
        lengthInSamples = (juce::int64) audio.left.size();
        metadataValues.set ("Mp3Decoder", mp3::decoderId);
        metadataValues.set ("Mp3Layer", juce::String (audio.info.layer));
        metadataValues.set ("Mp3BitrateKbps", juce::String (audio.info.firstBitrateKbps));
        metadataValues.set ("Mp3HeaderFrameSamples", juce::String (audio.info.headerFrameSamples));
        metadataValues.set ("Mp3EncoderDelay", juce::String (audio.info.encoderDelay));
        metadataValues.set ("Mp3EncoderPadding", juce::String (audio.info.encoderPadding));
    }

    bool readSamples (int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer, juce::int64 startSampleInFile,
                      int numSamples) override
    {
        for (int c = 0; c < numDestChannels; ++c)
        {
            if (destChannels[c] == nullptr)
                continue;
            auto* dest = reinterpret_cast<float*> (destChannels[c]) + startOffsetInDestBuffer;
            const auto& source = c > 0 && ! audio.right.empty() ? audio.right : audio.left;
            const auto size = (juce::int64) source.size();
            for (int i = 0; i < numSamples; ++i)
            {
                const auto index = startSampleInFile + i;
                dest[i] = index >= 0 && index < size ? source[(std::size_t) index] : 0.0f;
            }
        }
        return true;
    }

private:
    mp3::Decoded audio;
};
} // namespace

Mp3AudioFormat::Mp3AudioFormat()
    : juce::AudioFormat (formatName, ".mp3 .mp2 .mpga")
{
}

juce::AudioFormatReader* Mp3AudioFormat::createReaderFor (juce::InputStream* source, bool deleteStreamIfOpeningFails)
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

    // a quick look first: another format's file tried by content is not read whole
    const auto start = source->getPosition();
    juce::MemoryBlock head;
    source->readIntoMemoryBlock (head, 1 << 16);
    if (! mp3::looksLikeMp3 (static_cast<const std::uint8_t*> (head.getData()), head.getSize()))
        return fail();

    juce::MemoryBlock bytes;
    source->setPosition (start);
    source->readIntoMemoryBlock (bytes);
    mp3::Decoded decoded;
    if (! mp3::decode (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize(), decoded) || decoded.left.empty())
        return fail();
    return new Mp3Reader (std::move (decoded)); // the stream has been fully read: it can go
}

void registerAudioFormats (juce::AudioFormatManager& formats)
{
    formats.registerFormat (new Mp3AudioFormat(), false);
    formats.registerBasicFormats();
    formats.registerFormat (new NcwAudioFormat(), false); // Kontakt's NCW samples (round 190ii, issue #149)
}
} // namespace lynx
