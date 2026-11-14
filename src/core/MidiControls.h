#pragma once

// MIDI messages as automatable "parameters" of a hosted plugin, like FL's plugin wrapper (REA_rodada22 §2.1): past
// the plugin's own parameters, index 0x1000 + n is MIDI CC n (0…127) and 0x1080 + c is the channel pressure
// (aftertouch) of MIDI channel c + 1. LYNX adds 0x1090 + c, the pitch bend of MIDI channel c + 1 (FL bends through
// the channel's pitch knob instead). An automation clip, a pattern event lane or a MIDI link on such an index sends
// the message to the plugin whenever its 7-bit (14-bit for the bend) value changes; a .flp's automation of the
// wrapper's index arrives here unchanged.

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx::midictl
{
constexpr int ccBase = 0x1000;       // + CC number 0…127
constexpr int pressureBase = 0x1080; // + MIDI channel 0…15
constexpr int pitchBendBase = 0x1090; // + MIDI channel 0…15 (LYNX)
constexpr int end = 0x10A0;
constexpr int count = end - ccBase;

constexpr bool isMidiParam (int param) noexcept { return param >= ccBase && param < end; }
constexpr bool isCc (int param) noexcept { return param >= ccBase && param < pressureBase; }
constexpr bool isPressure (int param) noexcept { return param >= pressureBase && param < pitchBendBase; }
constexpr bool isPitchBend (int param) noexcept { return param >= pitchBendBase && param < end; }

// The value a controller rests at before anything moved it (0..1): the GM reset values — volume 100, pan and
// balance centred, expression full, the bend centred, everything else 0.
inline float defaultValue (int param) noexcept
{
    if (isPitchBend (param))
        return 0.5f;
    if (isCc (param))
        switch (param - ccBase)
        {
            case 7: return 100.0f / 127.0f;
            case 8: case 10: return 64.0f / 127.0f;
            case 11: return 1.0f;
            default: break;
        }
    return 0.0f;
}

// The 7-bit (pitch bend: 14-bit) value sent for an automation value 0..1.
inline int quantise (int param, float v) noexcept
{
    v = std::isfinite (v) ? std::fmin (1.0f, std::fmax (0.0f, v)) : 0.0f;
    return isPitchBend (param) ? (int) std::lround (v * 16383.0f) : (int) std::lround (v * 127.0f);
}

// The short MIDI message for a value (status, data 1, data 2); `midiChannel` 0…15 is used for the CCs (pressure
// and bend carry their own channel). Returns the number of bytes (2 or 3), 0 if `param` is not a MIDI index.
inline int message (int param, int value, int midiChannel, std::uint8_t out[3]) noexcept
{
    if (isCc (param))
    {
        out[0] = (std::uint8_t) (0xB0 | (midiChannel & 15));
        out[1] = (std::uint8_t) (param - ccBase);
        out[2] = (std::uint8_t) (value & 127);
        return 3;
    }
    if (isPressure (param))
    {
        out[0] = (std::uint8_t) (0xD0 | (param - pressureBase));
        out[1] = (std::uint8_t) (value & 127);
        return 2;
    }
    if (isPitchBend (param))
    {
        out[0] = (std::uint8_t) (0xE0 | (param - pitchBendBase));
        out[1] = (std::uint8_t) (value & 127);
        out[2] = (std::uint8_t) ((value >> 7) & 127);
        return 3;
    }
    return 0;
}

// FL's MIDI Out state (REA_rodada16 §1.1): what LYNX reads and edits of it. The rest of FL's bytes (control
// definitions, page names) are kept as they are.
struct MidiOutSettings
{
    int channel = 0;   // MIDI channel 0…15
    int port = 0;      // output port 0…255
    int program = 0;   // 1…128, 0 = don't send
    int bankMsb = -1, bankLsb = -1; // −1 = don't send
};
MidiOutSettings parseMidiOut (const std::vector<std::uint8_t>& state) noexcept;
// Writes the settings into `state` (FL's layout; a new state when it is too short to hold them).
void writeMidiOut (std::vector<std::uint8_t>& state, const MidiOutSettings& settings);

// The standard name of a CC number ("" when it has none), as FL lists them: "Modulation wheel", "Damper pedal
// (sustain)"…
const char* ccName (int cc) noexcept;

// "MIDI CC #64 (Damper pedal (sustain))", "MIDI Channel 3 Aftertouch", "MIDI Channel 1 Pitch Bend".
std::string paramName (int param);

// The MIDI side of one hosted plugin: values wanted by automation / links / the UI (any thread) and those already
// sent (audio thread). The audio thread turns each change into one message before the next block.
class State
{
public:
    State() noexcept
    {
        for (auto& w : wanted)
            w.store (-1, std::memory_order_relaxed);
        sent.fill (-1);
    }

    // Any thread. Returns false if `param` is not a MIDI index.
    bool set (int param, float value) noexcept
    {
        if (! isMidiParam (param))
            return false;
        const auto q = quantise (param, value);
        if (wanted[(std::size_t) (param - ccBase)].exchange (q, std::memory_order_relaxed) != q)
            dirty.store (true, std::memory_order_release);
        return true;
    }

    // The value last asked for (0..1), or the controller's default.
    float value (int param) const noexcept
    {
        if (! isMidiParam (param))
            return 0.0f;
        const auto q = wanted[(std::size_t) (param - ccBase)].load (std::memory_order_relaxed);
        if (q < 0)
            return defaultValue (param);
        return isPitchBend (param) ? (float) q / 16383.0f : (float) q / 127.0f;
    }

    // Audio thread: a host queue dropped a block; replay requested controls on the next flush (not defaults for
    // controls that were never touched). This also preserves a sustain-off lost in that discarded block.
    void resend() noexcept
    {
        sent.fill (-1);
        dirty.store (true, std::memory_order_release);
    }

    // Audio thread: calls emit(bytes, size) for each controller whose value changed since the last call.
    template <typename Emit>
    void flush (int midiChannel, Emit&& emit) noexcept
    {
        if (! dirty.exchange (false, std::memory_order_acquire))
            return;
        for (int k = 0; k < count; ++k)
        {
            const auto q = wanted[(std::size_t) k].load (std::memory_order_relaxed);
            if (q < 0 || q == sent[(std::size_t) k])
                continue;
            sent[(std::size_t) k] = q;
            std::uint8_t bytes[3] {};
            if (const auto size = message (ccBase + k, q, midiChannel, bytes); size > 0)
                emit (bytes, size);
        }
    }

private:
    std::array<std::atomic<int>, count> wanted;
    std::array<int, count> sent {};
    std::atomic<bool> dirty { false };
};
} // namespace lynx::midictl
