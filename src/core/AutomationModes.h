#pragma once

// Automation write modes (round 190pa, research190oe P21; Pro Tools', Logic's and Cubase's Touch / Latch / Write). A
// LYNX extra for recording automation from the controls: FL's Record automation (the recording filter's Automation,
// core/Recorder.h) stays the default and is the mode called Read here, bit for bit.
//
//  - Read (default): FL's way. A moved control writes while it is held or keeps moving (350 ms), then the curve that was
//    there plays again from the next tick.
//  - Touch: the same gesture rule, and on release the value glides back to the curve that was there over Touch's
//    return time (Pro Tools' AutoMatch / Logic's Ramp time, 250 ms), instead of jumping.
//  - Latch: from the first move on, the control keeps writing its last value after it is let go, until recording stops
//    (or the take ends); after that point the old curve plays again.
//  - Write: as Latch, and the moved parameter is overwritten from where recording started: before the first move the
//    curve holds the first value written.
//
// One mode per project (Song::automationWriteMode, saved in .lynx as "automationWriteMode" only when it is not Read);
// it applies to a recording take with the Automation filter on, and to Capture automation (round 190op,
// core/CaptureAutomation.h: Latch holds a gesture's last value to where that play pass stopped, Write also from where
// it started). JUCE-free; tested in tests/AutoModes190paTests.inc.

#include <cstdint>
#include <string>

namespace lynx::automodes
{
enum class Mode
{
    read = 0,
    touch = 1,
    latch = 2,
    write = 3
};

inline const char* idOf (Mode m) noexcept
{
    switch (m)
    {
        case Mode::touch: return "touch";
        case Mode::latch: return "latch";
        case Mode::write: return "write";
        case Mode::read: break;
    }
    return "read";
}

inline Mode fromId (const std::string& id) noexcept
{
    if (id == "touch")
        return Mode::touch;
    if (id == "latch")
        return Mode::latch;
    if (id == "write")
        return Mode::write;
    return Mode::read;
}

inline Mode fromInt (int v) noexcept { return v >= 0 && v <= 3 ? (Mode) v : Mode::read; }

// FL's gesture: a control is touched while held or while it moved less than this long ago
constexpr double gestureTimeoutMs = 350.0;
// Touch's glide back to the curve (Pro Tools' AutoMatch default is 250 ms; Logic's Ramp time 200 ms)
constexpr double touchReturnMs = 250.0;

// Whether a touched control still writes: Read / Touch while held or moving; Latch / Write until recording stops.
inline bool stillWriting (Mode m, bool held, double msSinceMove) noexcept
{
    if (m == Mode::latch || m == Mode::write)
        return true;
    return held || msSinceMove < gestureTimeoutMs;
}

// Whether a gesture's end glides back to the curve (Touch) rather than jumping (Read).
inline bool glidesBack (Mode m) noexcept { return m == Mode::touch; }
// Whether a gesture holds its last value to the end of the pass (Latch, Write).
inline bool holdsToEnd (Mode m) noexcept { return m == Mode::latch || m == Mode::write; }
// Whether a gesture's parameter is written from the start of the pass (Write).
inline bool writesFromStart (Mode m) noexcept { return m == Mode::write; }

// Touch's return time in ticks at `bpm` (`ppq` ticks per quarter note), at least one tick.
inline std::int64_t returnTicks (double bpm, std::int64_t ppq, double ms = touchReturnMs) noexcept
{
    const auto t = (std::int64_t) (ms / 60000.0 * (bpm > 0.0 ? bpm : 120.0) * (double) ppq + 0.5);
    return t < 1 ? 1 : t;
}
} // namespace lynx::automodes
