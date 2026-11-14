#pragma once

// A sample's spectrogram, as FL Studio 2026's Sampler draws it with the waveform menu's "Spectrum view" on (round 190gw;
// live check 190gu B3, shot fltails/fl-sampler-spectrum-view-off-top_on-bottom.png): time to the right, frequency up
// (linear, 0 Hz at the bottom), the level as a heat colour from black through dark purple and crimson to yellow.
// JUCE-free: the display (ui/WaveformView) turns the cells into pixels.

#include <cstdint>
#include <vector>

namespace lynx::spectrogram
{
struct Cells
{
    int columns = 0, bins = 0;
    std::vector<float> level; // columns × bins, column-major (column c, bin b at c * bins + b): 0 = the floor … 1 = the loudest
};

// `columns` analysis frames over frames [from, to) of the mono mix of `left` / `right` (right may be null), each a
// Hann-windowed FFT of `fftSize` points (a power of two, 64…8192) centred on the column; `fftSize / 2` bins. The level
// is the magnitude in dB relative to the loudest cell, `rangeDb` below it giving 0.
Cells compute (const float* left, const float* right, std::int64_t frames, double from, double to, int columns, int fftSize = 1024,
               float rangeDb = 90.0f);

// The heat colour (0xAARRGGBB) of a level 0…1.
std::uint32_t colourOf (float level) noexcept;
} // namespace lynx::spectrogram
