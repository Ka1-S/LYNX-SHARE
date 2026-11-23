#pragma once

// Offline mode (round 190nx, issue #233 follow-up): `LYNX_OFFLINE=1` in the environment, or `--offline` on the command line,
// guarantees that LYNX SHARE opens no network connection of its own: no update check or update download, no crash report
// upload, no model download (the stem model), no Online Sessions (live session relay). The places that would connect ask
// `lynx::offline::enabled()` first and say so instead (a message, never a silent failure). Files, Send project / Merge a
// part (a .lynxpack file you send yourself), the Remote Control API (127.0.0.1) and the plugin sandbox are not network
// access and keep working. No JUCE here: the engine, the core and the tests share it.

#include <atomic>
#include <cstdlib>
#include <cstring>

namespace lynx::offline
{
namespace detail
{
inline std::atomic<bool>& flag() noexcept
{
    static std::atomic<bool> set { false };
    return set;
}

// "1", "true", "yes", "on" or any other non-empty value except "0", "false", "no", "off"
inline bool envOn() noexcept
{
    const char* e = std::getenv ("LYNX_OFFLINE");
    if (e == nullptr || e[0] == '\0')
        return false;
    return std::strcmp (e, "0") != 0 && std::strcmp (e, "false") != 0 && std::strcmp (e, "no") != 0 && std::strcmp (e, "off") != 0;
}
} // namespace detail

// `--offline` (set once, early in main); tests also use it to turn the mode on and off
inline void set (bool on) noexcept { detail::flag() = on; }

inline bool enabled() noexcept { return detail::flag().load() || detail::envOn(); }

// English text of the reasons (callers pass them through tr())
inline constexpr const char* whyShort = "offline mode (LYNX_OFFLINE or --offline)";
} // namespace lynx::offline
