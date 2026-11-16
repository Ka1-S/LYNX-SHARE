#pragma once

// Native Instruments' NCW (Kontakt's lossless compressed WAV) decoder (round 190ii, issue #149). LYNX's own code,
// written from the format description in the "ncw" project's FORMAT.md (github.com/monomadic/ncw, MIT OR
// Apache-2.0; see THIRD_PARTY.md). NCW is a compression, not a protection: the samples of encrypted Kontakt
// libraries live in .nkx / .nkc containers, which LYNX does not open.
//
// Layout: a 120-byte header (signature 01 A8 9E D6 31|30 01 00 00, channels, bits, rate, frames, block table and data
// offsets), a table of block offsets (one per 512 frames, plus a sentinel), then per block one sub-block per channel
// (16-byte header: signature 16 0C 9A 3E, base value, signed width, flags) holding 512 values packed LSB first:
// deltas (width > 0, the first sample = the base value) or raw samples (width < 0). Flag bit 0: mid / side (left =
// mid + side, right = mid − side; channel 0's flag decides), bit 1: IEEE floats (deltas on their bit patterns).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx::ncw
{
struct Info
{
    int channels = 0, bits = 0;   // bits 8 / 16 / 24 / 32
    bool isFloat = false;          // (32-bit blocks flagged as floats)
    double sampleRate = 0.0;
    std::int64_t frames = 0;
};

bool isNcw (const std::uint8_t* data, std::size_t size);

struct Decoded
{
    Info info;
    std::vector<std::vector<float>> channels; // planar, −1 … 1
    std::string error;
};

// Decodes a whole file; `maxFrames` bounds the allocation (a malformed header cannot ask for more)
bool decode (const std::uint8_t* data, std::size_t size, Decoded& out, std::int64_t maxFrames = (std::int64_t) 1 << 28);

// The file as a WAV (16 / 24-bit PCM as in the file, 32-bit floats for float files): what the importer stores
std::vector<std::uint8_t> toWav (const std::uint8_t* data, std::size_t size, std::string* error = nullptr);
} // namespace lynx::ncw
