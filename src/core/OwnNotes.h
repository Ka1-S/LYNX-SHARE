#pragma once

// GitHub #198 (round 190ks): a hosted plugin's own notes — what it plays from its own window (pads or keys clicked in it)
// and sends on its MIDI output — told apart from the echo of the notes it was sent (a "MIDI thru" plugin passes its
// input on), so a take records each note once. Audio thread, per plugin instance; no allocation.

#include <array>
#include <cstdint>

namespace lynx::ownnotes
{
// how long after a note on went in the same key coming out is its echo (50 ms)
constexpr double echoSeconds = 0.05;

struct EchoGuard
{
    // a note on of `key` went into the plugin at sample `now` (the instance's running sample count)
    void noteIn (int key, std::int64_t now) noexcept
    {
        if (key >= 0 && key < 128)
            inAt[(std::size_t) key] = now + 1;
    }
    // a note on / off of `key` came out at sample `now`: true = the plugin's own (record it). A note on is an echo when
    // the same key went in within `window` samples; a note off is the plugin's own when its key's last note on was.
    bool noteOut (int key, bool on, std::int64_t now, std::int64_t window) noexcept
    {
        if (key < 0 || key >= 128)
            return false;
        const auto k = (std::size_t) key;
        if (! on)
        {
            const auto own = ownOn[k];
            ownOn[k] = false;
            return own;
        }
        const auto own = inAt[k] == 0 || now - (inAt[k] - 1) > window;
        ownOn[k] = own;
        return own;
    }

private:
    std::array<std::int64_t, 128> inAt {}; // 1 + the sample the key's last note on went in (0: never)
    std::array<bool, 128> ownOn {};        // the key's last note on out was the plugin's own (its note off is too)
};
} // namespace lynx::ownnotes
