#pragma once

// DVS (timecode vinyl / CD control) for the Fruity Scratcher, rebuilt clean-room from the behaviour decoded from FL
// Studio 2026's Fruity Scratcher_x64.dll and its engine_x64.dll (docs/analise/REA_rodada167c.md §1). No Image-Line code.
//
// A control record (vinyl or CD) plays a "timecode": a sine carrier on both channels, the right one 90° from the
// left, whose peaks are loud or quiet to spell a pseudo-random bit sequence (a linear feedback shift register). The
// carrier's phase gives the speed and direction; once enough bits agree with the register's rule, the register's value
// names the absolute place on the record. The decoder turns the input into a position on the record, in seconds,
// which the Scratcher uses to play its sample (1 s of record = 1 s of sample, looping).
//
// FL's Timecode type list and the decoder each entry creates (the state's timecode index in brackets):
//   Off [-1] · Serato Mk2, Side A [1] · Serato Mk2, Side B [2] · Serato CD [3] · Traktor Skratch Mk1, Side A [4] ·
//   Traktor Skratch Mk1, Side B [5] · Traktor Skratch Mk2, Side A [6] · Traktor Skratch Mk2, Side B [7] ·
//   Timecode CD [0].
// LYNX decodes the five sine-and-amplitude formats (Serato Mk2 A / B, Serato CD, Traktor Mk1 A / B). Traktor Mk2 and
// FL's "Timecode CD" use two other decoders in FL's engine that LYNX does not have yet: `supported()` says so and the
// decoder then reports no signal.
//
// Real-time safe after warm(): the position tables (one per format, up to 2.1 M entries) are built off the audio
// thread by warm() and shared; until a table is ready the decoder still follows speed and direction, it just cannot
// find the absolute place yet. No JUCE here (tests/Dvs167Tests.inc).

#include <atomic>
#include <cstdint>
#include <vector>

namespace lynx::dvs
{
constexpr int comboCount = 9; // FL's Timecode type list, "Off" included

// FL's list entry (0 = Off … 8 = Timecode CD) ↔ the state's timecode index (−1 = off)
int stateOfCombo (int combo) noexcept;
int comboOfState (int state) noexcept;
const char* comboName (int combo) noexcept;

struct Format
{
    int bits = 0;              // register length
    std::uint32_t seed = 0;    // the register at the record's first cycle
    std::uint32_t taps = 0;    // the feedback taps
    std::uint32_t length = 0;  // cycles on the record
    int resolution = 0;        // carrier frequency (cycles per second of record)
    double offset = 0.0;       // seconds subtracted from the place the register names
    std::uint8_t flags = 0;    // 0x10 reversed direction, 0x20 bits on the negative peaks, 0x40 left = primary
};
// The format of a state's timecode index; nullptr when LYNX has no decoder for it (off, Traktor Mk2, Timecode CD)
const Format* format (int state) noexcept;
bool supported (int state) noexcept;

// The register's next value (forwards), as the record spells it
std::uint32_t nextState (std::uint32_t state, const Format& f) noexcept;

// Builds the format's position table in the background (any thread but the audio thread; returns at once). With
// `wait` it blocks until the table is ready (tests, offline renders).
void warm (int state, bool wait = false);

class Decoder
{
public:
    // configure: the state's timecode index and the sample rate (no allocation: callable on the audio thread)
    void configure (int state, double sampleRate) noexcept;
    void reset() noexcept;
    // one stereo frame of the input
    void process (float left, float right) noexcept;

    bool active() const noexcept { return fmt != nullptr; }
    double position() const noexcept { return pos; }    // seconds on the record
    double pitch() const noexcept { return velocity; }  // record seconds per second (1 = normal speed, < 0 backwards)
    bool forwards() const noexcept { return dirForward; }
    bool locked() const noexcept { return lockCount > 0; } // the register named a place at least once

private:
    struct Crossing
    {
        bool positive = false, crossed = false;
        float zero = 0.0f;
        void feed (float x, float k) noexcept;
    };
    void shiftIn (bool bit) noexcept;

    const Format* fmt = nullptr;
    int stateIndex = -1;
    double rate = 44100.0, dt = 1.0 / 44100.0;
    float zeroK = 0.02f;
    Crossing primary, secondary;
    bool dirForward = true;
    std::uint32_t reg = 0;
    int valid = 0;
    float refLevel = 1.0f;
    double filtX = 0.0, velocity = 0.0; // the speed tracker (position error and speed)
    double pos = 0.0;
    int lockCount = 0;
};
} // namespace lynx::dvs
