#pragma once
#include <bitset>
#include <cstddef>

// Who owns a key pressed with the focus in a hosted plugin's view (round 181c / 187a, issue #96). A key the host takes
// on its first key-down stays the host's until it is released: the plugin never sees the repeats or the key-up of a
// press it didn't get, and it still gets the whole of any key the host left alone. The host's repeats run again, as
// a held key repeats over LYNX's main window (FL's Space, arrows, +/-: one keyboard, whichever window has the focus).
// Used for LYNX's shortcuts over a plugin and, with FL's per-plugin "Typing keyboard priority" button off, for the
// keys the host uses first (FL manual, Wrapper: "FL Studio uses keyboard shortcuts first then any unused shortcuts are
// passed to the plugin").
namespace lynx
{
class HostKeyGate
{
public:
    // true: the plugin does not get this keyboard message. `wanted` runs once per press, on its first key-down;
    // `again` on each repeat of a press the host took.
    template <typename Wanted, typename Again>
    bool message (std::size_t key, bool down, bool repeat, Wanted&& wanted, Again&& again)
    {
        if (key >= taken.size())
            return false;
        if (! down)
        {
            const bool mine = taken[key];
            taken[key] = false;
            return mine;
        }
        if (! repeat)
            taken[key] = wanted();
        else if (taken[key])
            again();
        return taken[key];
    }

    template <typename Wanted>
    bool message (std::size_t key, bool down, bool repeat, Wanted&& wanted)
    {
        return message (key, down, repeat, wanted, [] {});
    }

    bool holds (std::size_t key) const noexcept { return key < taken.size() && taken[key]; }

private:
    std::bitset<256> taken;
};
} // namespace lynx
