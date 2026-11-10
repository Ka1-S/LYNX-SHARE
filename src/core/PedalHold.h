#pragma once
#include <array>
#include <cstdint>
#include <unordered_map>

// FL's Settings › MIDI › "Foot pedal controls note off" (KbHoldSupportBox, on by default; round 187a [DECOMP FLEngine
// 2026: FUN_00fdb260 takes CC 64 and CC 66 off the MIDI stream when the option is on, FUN_01182540 hands the pedal to
// the channels the input plays, FUN_01001070 / FUN_010002e0 are the channel's hold]). Per channel:
// - CC 64 at 64 or more: sustain (mode 1); CC 66 at 64 or more: sostenuto (mode 2); either below 64: off (mode 0);
// - pedal down: every key's flag becomes 2 ("not played since the pedal went down");
// - sustain: a note-off is held back (flag 1); a key struck again while held is released first, then played;
// - sostenuto: only the keys that were down when the pedal went down (flag still 2) and are released are held back;
//   a key struck after it (flag 0) plays and stops as usual;
// - pedal up: every held-back key gets its note-off.
// The held-back note-off is what gets recorded too, so a recorded note lasts until the pedal comes up, as in FL.
namespace lynx
{
class PedalHold
{
public:
    static constexpr int keys = 132; // (FL's 0x84)

    // Returns false when nothing changed. `release (key)` runs for each held-back key when the pedal comes up.
    template <typename Release>
    bool setMode (int channel, int mode, Release&& release)
    {
        auto& s = channels[channel];
        if ((s.mode == 0) == (mode == 0))
            return false;
        s.mode = mode;
        if (mode <= 0)
        {
            for (int k = 0; k < keys; ++k)
                if ((s.flags[(std::size_t) k] & 1) != 0)
                {
                    s.flags[(std::size_t) k] = 0;
                    release (k);
                }
        }
        else
            s.flags.fill (2);
        return true;
    }

    // A note-on: true when the same key's held-back note must be released before it plays again.
    bool noteOn (int channel, int key)
    {
        auto* s = find (channel, key);
        if (s == nullptr)
            return false;
        auto& flag = s->flags[(std::size_t) key];
        const auto releaseFirst = s->mode == 1 && flag != 2; // (FL: also a second note-on of a key still down)
        flag = 0;
        return releaseFirst;
    }

    // A note-off: true when the pedal holds it back (it is sent when the pedal comes up).
    bool noteOff (int channel, int key)
    {
        auto* s = find (channel, key);
        if (s == nullptr)
            return false;
        auto& flag = s->flags[(std::size_t) key];
        if (s->mode == 1 || flag != 0)
        {
            flag = 1;
            return true;
        }
        return false;
    }

    int mode (int channel) const
    {
        const auto it = channels.find (channel);
        return it == channels.end() ? 0 : it->second.mode;
    }

    void clear() { channels.clear(); }

    // The mode a pedal message sets: CC 64 (sustain) / CC 66 (sostenuto); -1: not a pedal FL takes
    static int modeFor (int controller, int value) noexcept
    {
        if (controller != 64 && controller != 66)
            return -1;
        return value < 64 ? 0 : controller == 66 ? 2 : 1;
    }

private:
    struct State
    {
        int mode = 0;
        std::array<std::uint8_t, keys> flags {};
    };
    std::unordered_map<int, State> channels;

    State* find (int channel, int key)
    {
        if (key < 0 || key >= keys)
            return nullptr;
        const auto it = channels.find (channel);
        return it == channels.end() || it->second.mode <= 0 ? nullptr : &it->second;
    }
};
} // namespace lynx
