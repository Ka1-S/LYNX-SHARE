#pragma once

// FL Edison's view laws (round natives190kj), from a static study of FL Studio 2026's Edison_x64.dll (sha256
// 928d4487…fe89bb) with Ghidra; behaviour in our own words, JUCE-free (tests in tests/Natives190kjEdisonTests.inc):
// - Dual view (View menu, ViewDualMenuClick 0x87d7d0; the editor's paint 0x854030): the spectrogram and the waveform
//   drawn over each other with strengths that follow the zoom.
// - The time grid (0x84c470 sets its three base steps, 0x84c890 picks the drawn / snapped step with the spacing test
//   0x84c7e0; the background lines 0x84d0b0): which step the Snap to grid rounds to at a given zoom.
// - Snap to pitch period (Snap menu, key 5; the snap function 0x855d20 case 4, the period 0x62c1e0): multiples of the
//   period of the sample's own pitch (Sample properties' middle note and fine tune).
// - Grid-slicing (Regions › Auto-slice › Small / Medium / Large grid-slicing, 0x8636d0).
// - The Scrub button (ScrubBtnMouseDown / Move / Up 0x88c8d0 / 0x88c9d0 / 0x88cab0; the speed setter 0x8827c0): a
//   shuttle — drag right to play forward faster, left to play backward.
// - Detect pitch regions (Regions menu, 0x869100 → the pitch analysis 0x7a9a10 with its region option): one region per
//   detected note, named after the note.

#include <cstdint>
#include <string>
#include <vector>

#include "core/AudioEdit.h"

namespace lynx::edisonview
{
// --- Dual view --------------------------------------------------------------------------------------------------------
// The strength (0 … 256, 256 = opaque) the spectrogram and the waveform are drawn with. Without Dual view the chosen
// view is drawn alone. With it, f = 2 / samples-per-pixel held in [0, 1]: in the spectrogram view the spectrogram gets
// 256 − round(256·f) and the waveform round(164·f) + 92 over it (zoomed in to 2 samples per pixel only the waveform is
// left); in the waveform view the spectrogram gets 92 − round(92·f) under the full waveform.
struct Layers
{
    int spectrum = 0, wave = 256;
};
Layers layers (bool spectrumView, bool dualView, double samplesPerPixel) noexcept;

// --- the time grid ------------------------------------------------------------------------------------------------
// Edison's time axis runs in units: half seconds (a beat at 120 BPM), or beats of the sample's own tempo when the
// sample is tempo-synced with a tempo of 10 … 999 BPM (0x85a9b0).
bool beatMode (const audioedit::SampleProps& props) noexcept;
double framesPerUnit (bool beats, double sampleRate, double tempo) noexcept;

// The three base steps in units (0x84c470): seconds — small 0.2 (0.1 s), medium 2 (1 s), no large; beats — small
// 1 / steps-per-beat, medium 1 beat, large one bar (steps per bar / steps per beat, a whole number of beats).
struct GridBase
{
    double small = 0.2, medium = 2.0, large = 0.0;
};
GridBase secondsBase() noexcept;
GridBase beatsBase (int stepsPerBar, int stepsPerBeat) noexcept;

// The step the grid is drawn and snapped with at `pixelsPerUnit` (0x84c890): the base small step times the smallest
// whole m (1 … the larger of the medium and large counts) that divides, or is a multiple of, the medium and the large
// counts and leaves at least `minPixels` (12) between lines; when even the largest count is too dense, it doubles until
// it is not. mediumEvery / largeEvery: every how many lines a medium / large line is drawn (largeEvery 0 = none).
struct Grid
{
    double step = 0.0;
    int mediumEvery = 1, largeEvery = 0;
};
Grid grid (const GridBase& base, double pixelsPerUnit, double minPixels = 12.0) noexcept;
// 2 large, 1 medium, 0 small for the line at index `i` (position offset + i·step), as 0x84d0b0 draws them
int lineClass (std::int64_t i, const Grid& g) noexcept;

// Snap to grid (0x855d20 case 0): round((pos − offset) / step) · step + offset
double snapToStep (double pos, double step, double offset) noexcept;

// --- Snap to pitch period ----------------------------------------------------------------------------------------
// The period in frames of the sample's pitch: 440 · 2^((middle note + fine / 100 − 57) / 12) Hz (Edison's note 57 =
// "A4" = 440 Hz; FL's note names, C5 = 60).
double pitchPeriodFrames (double sampleRate, int middleNote, double fineCents) noexcept;
// round(frame / period) · period (from the sample's start; no offset)
std::int64_t snapToPeriod (std::int64_t frame, double periodFrames) noexcept;

// --- Grid-slicing ------------------------------------------------------------------------------------------------
// size 0 small, 1 medium, 2 large (a missing large step uses the medium one): the base step, not the zoomed one
double sliceStep (const GridBase& base, int size) noexcept;
// FL's markers: start, start + step, … while ≤ end (rounded to whole frames)
std::vector<std::int64_t> gridSlicePositions (std::int64_t start, std::int64_t end, double stepFrames);
// Grid-slicing on a document: the plain markers inside [start, end] go, then one marker per position. Returns how
// many were put.
int gridSlice (audioedit::Document& doc, std::int64_t start, std::int64_t end, double stepFrames);

// --- Scrub (the toolbar's Scrub button) ---------------------------------------------------------------------------
// Press: the speed starts at 0 (paused). Each move: + 0.025 × the pixels moved right / the UI scale, held in ±16.
// Release: back to normal playback (and the playback stops).
constexpr double scrubMaxSpeed = 16.0;
double scrubSpeed (double speed, double pixelsRight, double uiScale = 1.0) noexcept;
// FL's hint: "Paused" at 0, else the direction, |speed| with one decimal and " x" (LYNX: ▶ / ◀ for FL's glyphs)
std::string scrubText (double speed);

// --- Detect pitch regions -----------------------------------------------------------------------------------------
// FL's note name of a pitch region: the note letter and floor(n / 12) (C5 = 60)
std::string regionNoteName (int note);
struct PitchSegment
{
    std::int64_t start = 0, end = 0;
    double pitch = 0.0; // MIDI semitones
};
// Every marker goes, then one region per segment whose rounded pitch lies in 0 … 127, named after it (0x7a9a10 with
// option 1). Returns the number of regions.
int pitchRegions (audioedit::Document& doc, const std::vector<PitchSegment>& segments);
} // namespace lynx::edisonview
