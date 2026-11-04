#include "engine/MediaPack.h"

#include <cstring>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace lynx::mediapack
{
namespace
{
constexpr char magic[8] = { 'L', 'Y', 'N', 'X', 'F', 'L', 'C', '1' };
constexpr juce::int64 maxSize = (juce::int64) 4 << 30;

std::uint32_t le32 (const std::uint8_t* p) { return (std::uint32_t) p[0] | (std::uint32_t) p[1] << 8 | (std::uint32_t) p[2] << 16 | (std::uint32_t) p[3] << 24; }
std::uint16_t le16 (const std::uint8_t* p) { return (std::uint16_t) (p[0] | p[1] << 8); }

struct Layout
{
    juce::int64 dataStart = 0, dataSize = 0;
    int channels = 0, bits = 0;
    double rate = 0.0;
};

// Where a PCM WAV's samples are (only integer 16 / 24-bit, 1..8 channels: what FLAC holds exactly)
bool layoutOf (const juce::MemoryBlock& bytes, Layout& out)
{
    const auto* d = (const std::uint8_t*) bytes.getData();
    const auto size = (juce::int64) bytes.getSize();
    if (size < 44 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
        return false;
    bool haveFormat = false;
    juce::int64 at = 12;
    while (at + 8 <= size)
    {
        const auto chunkSize = (juce::int64) le32 (d + at + 4);
        const auto body = at + 8;
        if (body + chunkSize > size)
            return false;
        if (std::memcmp (d + at, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            auto tag = le16 (d + body);
            out.channels = le16 (d + body + 2);
            out.rate = (double) le32 (d + body + 4);
            out.bits = le16 (d + body + 14);
            if (tag == 0xFFFE && chunkSize >= 40)
                tag = le16 (d + body + 24); // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes
            if (tag != 1 || (out.bits != 16 && out.bits != 24) || out.channels < 1 || out.channels > 8 || out.rate < 1000.0 || out.rate > 655350.0)
                return false;
            haveFormat = true;
        }
        else if (std::memcmp (d + at, "data", 4) == 0)
        {
            if (! haveFormat)
                return false;
            out.dataStart = body;
            out.dataSize = chunkSize;
            return chunkSize > 0 && chunkSize % (out.channels * (out.bits / 8)) == 0;
        }
        at = body + chunkSize + (chunkSize & 1);
    }
    return false;
}

// the samples as ints with their value in the top bits (JUCE's integer convention)
void readSamples (const std::uint8_t* data, juce::int64 frames, const Layout& l, std::vector<std::vector<int>>& out)
{
    const int bytes = l.bits / 8;
    out.assign ((std::size_t) l.channels, std::vector<int> ((std::size_t) frames));
    for (juce::int64 f = 0; f < frames; ++f)
        for (int c = 0; c < l.channels; ++c)
        {
            const auto* p = data + (f * l.channels + c) * bytes;
            const auto v = bytes == 2 ? (std::uint32_t) p[0] << 16 | (std::uint32_t) p[1] << 24
                                      : (std::uint32_t) p[0] << 8 | (std::uint32_t) p[1] << 16 | (std::uint32_t) p[2] << 24;
            out[(std::size_t) c][(std::size_t) f] = (int) v;
        }
}

void writeSamples (std::uint8_t* data, juce::int64 frames, const Layout& l, int* const* channels, juce::int64 from)
{
    const int bytes = l.bits / 8;
    for (juce::int64 f = 0; f < frames; ++f)
        for (int c = 0; c < l.channels; ++c)
        {
            const auto v = (std::uint32_t) channels[c][f];
            auto* p = data + ((from + f) * l.channels + c) * bytes;
            if (bytes == 2)
                p[0] = (std::uint8_t) (v >> 16), p[1] = (std::uint8_t) (v >> 24);
            else
                p[0] = (std::uint8_t) (v >> 8), p[1] = (std::uint8_t) (v >> 16), p[2] = (std::uint8_t) (v >> 24);
        }
}

bool decode (const void* packData, size_t packSize, juce::MemoryBlock& out)
{
    juce::MemoryInputStream in (packData, packSize, false);
    char m[8];
    if (in.read (m, 8) != 8 || std::memcmp (m, magic, 8) != 0)
        return false;
    const auto total = in.readInt64();
    const auto headerSize = in.readInt64(), trailerSize = in.readInt64();
    Layout l;
    l.channels = in.readInt();
    l.bits = in.readInt();
    const auto frames = in.readInt64();
    if (total <= 0 || total > maxSize || headerSize < 0 || trailerSize < 0 || frames <= 0 || (l.bits != 16 && l.bits != 24) || l.channels < 1 || l.channels > 8)
        return false;
    const auto dataSize = frames * l.channels * (l.bits / 8);
    if (headerSize + dataSize + trailerSize != total || headerSize + trailerSize > (juce::int64) packSize)
        return false;
    out.setSize ((size_t) total, false);
    auto* d = (std::uint8_t*) out.getData();
    if (in.read (d, (int) headerSize) != (int) headerSize || in.read (d + headerSize + dataSize, (int) trailerSize) != (int) trailerSize)
        return false;
    const auto flacAt = in.getPosition();
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (
        flac.createReaderFor (new juce::MemoryInputStream ((const char*) packData + flacAt, packSize - (size_t) flacAt, false), true));
    if (reader == nullptr || reader->lengthInSamples != frames || (int) reader->numChannels != l.channels || (int) reader->bitsPerSample != l.bits)
        return false;
    constexpr int block = 65536;
    std::vector<std::vector<int>> buffers ((std::size_t) l.channels, std::vector<int> (block));
    std::vector<int*> ptrs;
    for (auto& b : buffers)
        ptrs.push_back (b.data());
    for (juce::int64 at = 0; at < frames; at += block)
    {
        const auto n = (int) std::min<juce::int64> (block, frames - at);
        if (! reader->read (ptrs.data(), l.channels, at, n, false))
            return false;
        writeSamples (d + headerSize, n, l, ptrs.data(), at);
    }
    return true;
}
} // namespace

bool pack (const juce::File& wav, const juce::File& packFile)
{
    const auto size = wav.getSize();
    if (size < minimumSize || size > maxSize || wav.getFileExtension().toLowerCase() != ".wav")
        return false;
    juce::MemoryBlock bytes;
    if (! wav.loadFileAsData (bytes) || (juce::int64) bytes.getSize() != size)
        return false;
    Layout l;
    if (! layoutOf (bytes, l))
        return false;
    const auto* d = (const std::uint8_t*) bytes.getData();
    const auto frames = l.dataSize / (l.channels * (l.bits / 8));
    std::vector<std::vector<int>> samples;
    readSamples (d + l.dataStart, frames, l, samples);

    juce::MemoryBlock flacBytes;
    {
        juce::FlacAudioFormat flac;
        auto stream = std::make_unique<juce::MemoryOutputStream> (flacBytes, false);
        std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (stream.get(), l.rate, (unsigned int) l.channels, l.bits, {}, 5));
        if (writer == nullptr)
            return false;
        stream.release(); // (the writer owns it now)
        std::vector<const int*> ptrs;
        for (const auto& c : samples)
            ptrs.push_back (c.data());
        constexpr int block = 65536;
        for (juce::int64 at = 0; at < frames; at += block)
        {
            std::vector<const int*> slice;
            for (const auto* p : ptrs)
                slice.push_back (p + at);
            slice.push_back (nullptr);
            if (! writer->write (slice.data(), (int) std::min<juce::int64> (block, frames - at)))
                return false;
        }
    }
    const auto trailerStart = l.dataStart + l.dataSize;
    juce::MemoryOutputStream out;
    out.write (magic, 8);
    out.writeInt64 (size);
    out.writeInt64 (l.dataStart);
    out.writeInt64 (size - trailerStart);
    out.writeInt (l.channels);
    out.writeInt (l.bits);
    out.writeInt64 (frames);
    out.write (d, (size_t) l.dataStart);
    out.write (d + trailerStart, (size_t) (size - trailerStart));
    out.write (flacBytes.getData(), flacBytes.getSize());
    if ((double) out.getDataSize() > 0.9 * (double) size)
        return false; // (not worth it: noise, or already small)
    // the receiver must end with exactly these bytes: check it here first
    juce::MemoryBlock back;
    if (! decode (out.getData(), out.getDataSize(), back) || back != bytes)
        return false;
    packFile.getParentDirectory().createDirectory();
    return packFile.replaceWithData (out.getData(), out.getDataSize());
}

bool unpack (const juce::File& packFile, const juce::File& outFile)
{
    juce::MemoryBlock packed, original;
    if (packFile.getSize() > maxSize || ! packFile.loadFileAsData (packed) || ! decode (packed.getData(), packed.getSize(), original))
        return false;
    return outFile.replaceWithData (original.getData(), original.getSize());
}

bool isPack (const juce::File& file)
{
    juce::FileInputStream in (file);
    char m[8];
    return in.openedOk() && in.read (m, 8) == 8 && std::memcmp (m, magic, 8) == 0;
}
} // namespace lynx::mediapack
