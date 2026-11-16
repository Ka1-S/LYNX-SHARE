#pragma once

// The last note played on the typing keyboard or a MIDI input, packed in one atomic word for the hint bar's
// "Note on: C4 / velocity: 100" (FL 2026's hint bar, details190if shots/fl-hint-noteon.png; round 190ih, PLAN R3
// DC-9): serial << 16 | on << 15 | key << 8 | velocity (0 … 127); 0 = no note yet. The serial (1 … 65535) makes every
// note a change, even the same key again.

#include <algorithm>
#include <cstdint>

namespace lynx::livenote
{
constexpr std::uint32_t pack (std::uint32_t previous, int key, int velocity127, bool on) noexcept
{
    const auto serial = std::max<std::uint32_t> (((previous >> 16) + 1u) & 0xffffu, 1u);
    return (serial << 16) | (on ? 0x8000u : 0u) | ((std::uint32_t) std::clamp (key, 0, 127) << 8) | (std::uint32_t) std::clamp (velocity127, 0, 127);
}
constexpr int key (std::uint32_t packed) noexcept { return (int) (packed >> 8) & 0x7f; }
constexpr int velocity (std::uint32_t packed) noexcept { return (int) packed & 0x7f; }
constexpr bool on (std::uint32_t packed) noexcept { return (packed & 0x8000u) != 0; }
} // namespace lynx::livenote
