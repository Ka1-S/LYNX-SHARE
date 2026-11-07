#pragma once

// Video time for the video tracks and ADR (round 174d, docs/analise/REA_rodada174d.md): exact frame rates (rationals:
// 24000/1001 …), SMPTE timecode with and without drop frame, typed-timecode parsing, and the frame a time falls on.
// Times are seconds (double) on the video's own time line; no JUCE here (tests/Video174dTests.inc).

#include <cstdint>
#include <optional>
#include <string>

namespace lynx::vtime
{
// A frame rate as a fraction (frames per second = num / den) and whether its timecode drops frame numbers.
struct Rate
{
    int num = 25, den = 1;
    bool drop = false;

    double fps() const noexcept { return (double) num / (double) den; }
    int nominal() const noexcept { return (num + den / 2) / den; } // 30 for 29.97, 24 for 23.976
    bool operator== (const Rate&) const = default;
};

// The project / track rates offered in the menus, in order (23.976, 24, 25, 29.97 DF, 29.97, 30, 48, 50, 59.94 DF,
// 59.94, 60)
constexpr int rateCount = 11;
Rate rateAt (int index) noexcept;
int indexOf (const Rate& r) noexcept;      // −1 when not a listed rate
std::string rateName (const Rate& r);       // "29.97 fps drop frame", "25 fps"
std::string rateId (const Rate& r);         // "2997df", "25" (project files)
std::optional<Rate> rateFromId (const std::string& id);
// The listed rate nearest a measured fps (a file's rate, 29.97002997… → 29.97 non-drop); nullopt below 1 fps
std::optional<Rate> nearestRate (double fps) noexcept;

// Frame number ↔ timecode fields. Drop frame (29.97 / 59.94): the labels 0 and 1 (0…3 at 59.94) are skipped at the
// start of every minute except each tenth; no frames are dropped, only labels.
struct Timecode
{
    int hours = 0, minutes = 0, seconds = 0, frames = 0;
    bool negative = false;
    bool operator== (const Timecode&) const = default;
};
Timecode toTimecode (std::int64_t frame, const Rate& r) noexcept;
std::int64_t toFrame (const Timecode& tc, const Rate& r) noexcept;

// The frame showing at `seconds` (floor, with a tiny tolerance so that k / fps is frame k) and the start of frame k
std::int64_t frameAt (double seconds, const Rate& r) noexcept;
double frameStart (std::int64_t frame, const Rate& r) noexcept;

// "01:00:00:00" / "01:00:00;00" (drop frame); a negative time gets "-"
std::string format (double seconds, const Rate& r);
std::string format (const Timecode& tc, const Rate& r);
// Typed timecode: "1:02:03:04", "01:02:03;04", "010203:04"-style digits ("1020304" = 1:02:03:04, right-aligned as on
// a console keypad), "+00:00:01:00" is not special here. Separators : ; . or space. nullopt when it does not parse or
// a field is out of range.
std::optional<Timecode> parse (const std::string& typed, const Rate& r);
// Typed timecode → seconds
std::optional<double> parseSeconds (const std::string& typed, const Rate& r);
} // namespace lynx::vtime
