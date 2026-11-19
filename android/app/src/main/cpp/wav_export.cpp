#include "wav_export.h"

#include <cstring>
#include <fstream>
#include <cmath>

namespace {

constexpr char kRiff[4] = {'R', 'I', 'F', 'F'};
constexpr char kWave[4] = {'W', 'A', 'V', 'E'};
constexpr char kFmt[4]  = {'f', 'm', 't', ' '};
constexpr char kData[4] = {'d', 'a', 't', 'a'};

void writeBytes(std::ofstream& out, const void* data, std::size_t size) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

template <typename T>
void writeLE(std::ofstream& out, T value) {
    writeBytes(out, &value, sizeof(T));
}

std::int16_t floatToPcm16(float sample) {
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;
    return static_cast<std::int16_t>(sample * 32767.0f);
}

} // namespace

WavExportResult writeWav(
    const std::string& path,
    int sampleRate,
    const std::vector<float>& interleavedStereo) {

    WavExportResult result;
    result.path = path;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        result.error = "cannot open file for writing";
        return result;
    }

    const std::uint32_t sampleRateU32 = static_cast<std::uint32_t>(sampleRate);
    const std::uint32_t byteRate = sampleRateU32 * 2 * 2; // 2 channels * 2 bytes
    const std::uint32_t blockAlign = 4; // 2 channels * 2 bytes
    const std::uint32_t dataSize = static_cast<std::uint32_t>(
        interleavedStereo.size() * sizeof(float));
    const std::uint32_t pcmDataSize = interleavedStereo.size() * sizeof(std::int16_t);
    const std::uint32_t fileSize = 36 + pcmDataSize;

    // RIFF header
    writeBytes(out, kRiff, 4);
    writeLE(out, fileSize);
    writeBytes(out, kWave, 4);

    // fmt chunk
    writeBytes(out, kFmt, 4);
    std::uint32_t fmtChunkSize = 16;
    writeLE(out, fmtChunkSize);
    std::uint16_t audioFormat = 1; // PCM
    writeLE(out, audioFormat);
    std::uint16_t numChannels = 2;
    writeLE(out, numChannels);
    writeLE(out, sampleRateU32);
    writeLE(out, byteRate);
    writeLE(out, blockAlign);
    std::uint16_t bitsPerSample = 16;
    writeLE(out, bitsPerSample);

    // data chunk
    writeBytes(out, kData, 4);
    writeLE(out, pcmDataSize);

    // Convert float stereo to 16-bit PCM
    std::vector<std::int16_t> pcm;
    pcm.reserve(interleavedStereo.size());
    for (float s : interleavedStereo) {
        pcm.push_back(floatToPcm16(s));
    }
    writeBytes(out, pcm.data(), pcm.size() * sizeof(std::int16_t));

    if (!out.good()) {
        result.error = "write failed";
        return result;
    }

    result.ok = true;
    return result;
}
