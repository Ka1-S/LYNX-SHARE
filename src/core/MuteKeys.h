#pragma once

// Issue #249 (round 190oq), a LYNX extra: "Alt+M toggles". FL's Alt+M (EEMuteAction) only ever mutes the selection and
// Shift+Alt+M (EEUnmuteAction) unmutes it, in the Playlist and the Piano roll alike (REA round 190oq: the action sets the
// mute flag to its Tag; no key toggles a selection's mute in FL). With this on, Alt+M on clips / notes that are all
// muted already unmutes them, so the same key switches both ways; Shift+Alt+M still only unmutes. Off by default (FL).
// An app setting (ui-options.txt "muteKeyToggles"). FL's toggles stay as they are: the Mixer's S solos / unsolos, the
// Channel rack's 1…0 mute / unmute and Ctrl+1…0 solo / unsolo.

#include <atomic>

namespace lynx::mutekeys
{
inline std::atomic<bool>& togglesFlag() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
inline bool toggles() noexcept { return togglesFlag().load (std::memory_order_relaxed); }
inline void setToggles (bool on) noexcept { togglesFlag().store (on, std::memory_order_relaxed); }
} // namespace lynx::mutekeys
