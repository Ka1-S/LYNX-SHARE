#pragma once

#include <cstdint>
#include <string>
#include <vector>

// WavExport — render interleaved stereo float buffer to a 16-bit WAV file.
// JUCE-free, C++20.

struct WavExportResult {
    bool ok = false;
    std::string error;
    std::string path;
};

// Write interleaved stereo float samples [-1, 1] to a 16-bit PCM WAV file.
WavExportResult writeWav(
    const std::string& path,
    int sampleRate,
    const std::vector<float>& interleavedStereo);
