#pragma once

// MPE (MIDI Polyphonic Expression), stage 1 (issue #108, round 190bb; docs/analise/REA_rodada190bb-mpe.md).
// A LYNX addition: FL Studio 2026 has no MPE switch (its wrapper's MIDI section ends at "Send MOD X as polyphonic
// aftertouch", read from the settings form), so the per-note data follows what FL's piano roll already has:
//
//   per-note pitch bend  ->  the note's fine pitch (cents)
//   timbre (CC 74)       ->  the note's Mod X (0..1, 0.5 = the controller's centre, 64)
//   pressure             ->  the note's Mod Y (0.5 = no pressure, 1 = full)
//
// Written from the MPE specification (MIDI Association, RP-053): the lower zone, master channel 1, member channels
// 2 … 1 + n; a member channel's pitch bend, CC 74 and channel pressure belong to the notes sounding on it. No JUCE
// here: the engine feeds raw MIDI bytes in and gets plain events out.
//
//   Input      a controller's MIDI -> note ons / offs with their expression, and expression changes per sounding note
//   Allocator  notes going out to a hosted plugin: one member channel per note (channel rotation)
//   the laws   bend <-> cents, CC 74 <-> Mod X, pressure <-> Mod Y, and a member channel's MIDI as a note expression
//              (CLAP's tuning / brightness / pressure)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lynx::mpe
{
constexpr int maxMembers = 15;
constexpr float defaultBendRange = 48.0f; // semitones, the specification's default for member channels

struct Zone
{
    bool on = false;
    int members = maxMembers;             // member channels 2 … 1 + members
    float bendRange = defaultBendRange;   // per-note pitch bend range (semitones)
    bool operator== (const Zone&) const = default;
};

enum class Expr : std::uint8_t { pitch, timbre, pressure };

// ---- the laws ---------------------------------------------------------------------------------------------------
inline float bendCents (int bend14, float rangeSemitones) noexcept
{
    const auto v = std::clamp (bend14, 0, 16383) - 8192;
    // (the top of the wheel is 8191: it reaches the full range, as the bottom does)
    return (v >= 0 ? (float) v / 8191.0f : (float) v / 8192.0f) * rangeSemitones * 100.0f;
}
inline int bendValue (float cents, float rangeSemitones) noexcept
{
    if (! std::isfinite (cents) || rangeSemitones <= 0.0f)
        return 8192;
    const auto x = std::clamp (cents / (rangeSemitones * 100.0f), -1.0f, 1.0f);
    return 8192 + (int) std::lround (x >= 0.0f ? x * 8191.0f : x * 8192.0f);
}
// CC 74: 64 is the centre, Mod X 0.5
inline float modXOfTimbre (int cc) noexcept
{
    const auto v = std::clamp (cc, 0, 127);
    return v >= 64 ? 0.5f + (float) (v - 64) / 126.0f : (float) v / 128.0f;
}
inline int timbreOfModX (float modX) noexcept
{
    const auto x = std::isfinite (modX) ? std::clamp (modX, 0.0f, 1.0f) : 0.5f;
    return std::clamp ((int) std::lround (x >= 0.5f ? 64.0f + (x - 0.5f) * 126.0f : x * 128.0f), 0, 127);
}
// pressure 0..1 <-> Mod Y 0.5..1 (a note nobody pressed keeps the neutral Mod Y; Mod Y under 0.5 sends no pressure)
inline float modYOfPressure (float pressure) noexcept { return 0.5f + 0.5f * std::clamp (pressure, 0.0f, 1.0f); }
inline float pressureOfModY (float modY) noexcept
{
    return std::isfinite (modY) ? std::clamp ((modY - 0.5f) * 2.0f, 0.0f, 1.0f) : 0.0f;
}
// the piano roll's fine pitch holds −120 … +120 cents (core/Song.h)
inline float finePitchOfCents (float cents) noexcept { return std::isfinite (cents) ? std::clamp (cents, -120.0f, 120.0f) : 0.0f; }

// ---- input ------------------------------------------------------------------------------------------------------
struct InEvent
{
    enum class Type : std::uint8_t
    {
        plain,   // not the zone's per-note data: handle the message as an ordinary one (master channel, other CCs)
        noteOn,  // `cents` / `timbre` / `pressure` = the note's expression as it starts
        noteOff, // `velocity` = the release velocity; `cents` / `timbre` = as the note started, `pressure` = its peak
        expr     // a sounding note's expression changed: `expr`, `value` (pitch: cents; timbre / pressure: 0..1)
    };
    Type type = Type::plain;
    int channel = 0; // the MIDI channel, 0-based
    int key = 60;
    float velocity = 0.0f;
    Expr expr = Expr::pitch;
    float value = 0.0f;
    float cents = 0.0f, timbre = 0.5f, pressure = 0.0f;
};

class Input
{
public:
    static constexpr int maxPerChannel = 8;                 // notes sounding on one member channel
    static constexpr int maxEvents = maxPerChannel;         // the most one message produces

    void setZone (const Zone& z) noexcept
    {
        zone = z;
        zone.members = std::clamp (zone.members, 1, maxMembers);
        zone.bendRange = std::clamp (zone.bendRange, 1.0f, 96.0f);
    }
    const Zone& currentZone() const noexcept { return zone; }
    void reset() noexcept { channels = {}; }

    // One channel voice message (status + data). Returns the events written to `out` (room for maxEvents); 0 = the
    // message was the zone's own configuration (taken) or changed nothing that sounds.
    int feed (const std::uint8_t* bytes, int size, InEvent* out) noexcept
    {
        if (size < 2 || bytes[0] < 0x80 || bytes[0] >= 0xf0)
            return plain (out);
        const auto status = bytes[0] & 0xf0;
        const auto ch = bytes[0] & 0x0f;
        const auto d1 = bytes[1] & 0x7f;
        const auto d2 = size > 2 ? bytes[2] & 0x7f : 0;
        if (! zone.on)
            return plain (out);

        auto& c = channels[(std::size_t) ch];
        if (status == 0xb0 && (d1 == 101 || d1 == 100 || d1 == 6 || d1 == 38))
        {
            // registered parameters: 6 on the master channel = the zone's member count (MPE Configuration Message),
            // 0 on a member channel = the per-note bend range (it applies to every member channel)
            if (d1 == 101) c.rpnMsb = (std::uint8_t) d2;
            else if (d1 == 100) c.rpnLsb = (std::uint8_t) d2;
            else if (d1 == 6 && c.rpnMsb == 0 && c.rpnLsb == 6 && ch == 0)
            {
                if (d2 >= 1 && d2 <= maxMembers)
                    zone.members = d2;
                return 0;
            }
            else if (d1 == 6 && c.rpnMsb == 0 && c.rpnLsb == 0 && member (ch))
            {
                if (d2 >= 1)
                    zone.bendRange = (float) std::min (96, d2);
                return 0;
            }
            if (member (ch) || (ch == 0 && c.rpnMsb == 0 && c.rpnLsb == 6))
                return 0;
            return plain (out);
        }
        if (! member (ch))
            return plain (out);

        switch (status)
        {
            case 0x90:
                if (d2 > 0)
                {
                    remove (c, d1); // (a note struck again without its off)
                    if (c.count == maxPerChannel)
                        remove (c, c.notes[0].key);
                    auto& n = c.notes[(std::size_t) c.count++];
                    n = { (std::uint8_t) d1, cents (c), c.timbre, c.pressure };
                    out[0] = {};
                    out[0].type = InEvent::Type::noteOn;
                    out[0].channel = ch;
                    out[0].key = d1;
                    out[0].velocity = (float) d2 / 127.0f;
                    out[0].cents = n.cents;
                    out[0].timbre = n.timbre;
                    out[0].pressure = n.peak;
                    return 1;
                }
                [[fallthrough]];
            case 0x80:
            {
                out[0] = {};
                out[0].type = InEvent::Type::noteOff;
                out[0].channel = ch;
                out[0].key = d1;
                out[0].velocity = status == 0x80 ? (float) d2 / 127.0f : 0.0f;
                for (int i = 0; i < c.count; ++i)
                    if (c.notes[(std::size_t) i].key == d1)
                    {
                        out[0].cents = c.notes[(std::size_t) i].cents;
                        out[0].timbre = c.notes[(std::size_t) i].timbre;
                        out[0].pressure = c.notes[(std::size_t) i].peak;
                    }
                remove (c, d1);
                return 1;
            }
            case 0xe0:
                c.bend = (std::uint16_t) (d1 | (d2 << 7));
                return each (c, ch, Expr::pitch, cents (c), -1, out);
            case 0xd0:
                c.pressure = (float) d1 / 127.0f;
                return each (c, ch, Expr::pressure, c.pressure, -1, out);
            case 0xa0: // (polyphonic key pressure: that key alone)
                return each (c, ch, Expr::pressure, (float) d2 / 127.0f, d1, out);
            case 0xb0:
                if (d1 == 74)
                {
                    c.timbre = modXOfTimbre (d2);
                    return each (c, ch, Expr::timbre, c.timbre, -1, out);
                }
                break;
            default:
                break;
        }
        return plain (out);
    }

private:
    struct Sounding
    {
        std::uint8_t key = 0;
        float cents = 0.0f, timbre = 0.5f, peak = 0.0f;
    };
    struct Channel
    {
        std::uint16_t bend = 8192;
        float timbre = 0.5f, pressure = 0.0f;
        std::uint8_t rpnMsb = 127, rpnLsb = 127;
        int count = 0;
        std::array<Sounding, maxPerChannel> notes {};
    };

    bool member (int ch) const noexcept { return ch >= 1 && ch <= zone.members; }
    float cents (const Channel& c) const noexcept { return bendCents (c.bend, zone.bendRange); }
    static int plain (InEvent* out) noexcept
    {
        out[0] = {};
        return 1;
    }
    static void remove (Channel& c, int key) noexcept
    {
        for (int i = 0; i < c.count; ++i)
            if (c.notes[(std::size_t) i].key == key)
            {
                for (int k = i + 1; k < c.count; ++k)
                    c.notes[(std::size_t) (k - 1)] = c.notes[(std::size_t) k];
                --c.count;
                return;
            }
    }
    static int each (Channel& c, int ch, Expr expr, float value, int onlyKey, InEvent* out) noexcept
    {
        auto n = 0;
        for (int i = 0; i < c.count; ++i)
        {
            auto& s = c.notes[(std::size_t) i];
            if (onlyKey >= 0 && s.key != onlyKey)
                continue;
            if (expr == Expr::pressure)
                s.peak = std::max (s.peak, value);
            out[n] = {};
            out[n].type = InEvent::Type::expr;
            out[n].channel = ch;
            out[n].key = s.key;
            out[n].expr = expr;
            out[n].value = value;
            ++n;
        }
        return n;
    }

    Zone zone;
    std::array<Channel, 16> channels {};
};

// ---- output: one member channel per note ------------------------------------------------------------------------
class Allocator
{
public:
    void setMembers (int n) noexcept { members = std::clamp (n, 1, maxMembers); }
    void reset() noexcept
    {
        counts = {};
        released = {};
        keys = {};
        clock = 0;
    }
    // The member channel (1 … members, 0-based MIDI channel) the note goes out on: a free one, the longest released
    // first (its release tail keeps its own bend); with every channel busy, the one with the fewest notes.
    int noteOn (int key) noexcept
    {
        auto best = 1;
        for (int ch = 2; ch <= members; ++ch)
        {
            const auto a = counts[(std::size_t) ch], b = counts[(std::size_t) best];
            if (a < b || (a == b && released[(std::size_t) ch] < released[(std::size_t) best]))
                best = ch;
        }
        ++counts[(std::size_t) best];
        released[(std::size_t) best] = ++clock; // (a busy channel counts as just used)
        auto& k = keys[(std::size_t) std::clamp (key, 0, 127)];
        if (k.count < (int) k.channels.size())
            k.channels[(std::size_t) k.count++] = (std::uint8_t) best;
        return best;
    }
    // The channel the key's oldest note went out on (−1 = none sounds): its note off goes there
    int noteOff (int key) noexcept
    {
        auto& k = keys[(std::size_t) std::clamp (key, 0, 127)];
        if (k.count == 0)
            return -1;
        const auto ch = (int) k.channels[0];
        for (int i = 1; i < k.count; ++i)
            k.channels[(std::size_t) (i - 1)] = k.channels[(std::size_t) i];
        --k.count;
        counts[(std::size_t) ch] = std::max (0, counts[(std::size_t) ch] - 1);
        released[(std::size_t) ch] = ++clock;
        return ch;
    }
    // The channel of the key's newest sounding note (−1 = none)
    int channelOf (int key) const noexcept
    {
        const auto& k = keys[(std::size_t) std::clamp (key, 0, 127)];
        return k.count > 0 ? (int) k.channels[(std::size_t) (k.count - 1)] : -1;
    }
    int sounding (int channel) const noexcept { return counts[(std::size_t) std::clamp (channel, 0, 15)]; }

private:
    struct Key
    {
        std::array<std::uint8_t, 4> channels {};
        int count = 0;
    };
    int members = maxMembers;
    std::array<int, 16> counts {};
    std::array<std::uint32_t, 16> released {};
    std::array<Key, 128> keys {};
    std::uint32_t clock = 0;
};

// The messages that tell a plugin about the zone, before its first note: the MPE Configuration Message on the master
// channel, then the per-note bend range (RPN 0) on each member channel. fn (status, data1, data2).
template <typename Fn>
void zoneSetup (int members, int bendRange, Fn&& fn)
{
    members = std::clamp (members, 1, maxMembers);
    bendRange = std::clamp (bendRange, 1, 96);
    fn (0xb0, 101, 0);
    fn (0xb0, 100, 6);
    fn (0xb0, 6, members);
    for (int ch = 1; ch <= members; ++ch)
    {
        fn (0xb0 | ch, 101, 0);
        fn (0xb0 | ch, 100, 0);
        fn (0xb0 | ch, 6, bendRange);
        fn (0xb0 | ch, 38, 0);
    }
}

// A member channel's pitch bend / CC 74 / channel pressure as a note expression for every note on that channel
// (CLAP: tuning in semitones, brightness 0..1, pressure 0..1; key and note id are wildcards). False = not one.
struct NoteExpression
{
    Expr expr = Expr::pitch;
    int channel = 0;
    double value = 0.0; // pitch: semitones
};
inline bool toNoteExpression (const std::uint8_t* bytes, int size, float bendRange, NoteExpression& out) noexcept
{
    if (size < 2 || (bytes[0] & 0x0f) == 0)
        return false;
    out.channel = bytes[0] & 0x0f;
    switch (bytes[0] & 0xf0)
    {
        case 0xe0:
            if (size < 3)
                return false;
            out.expr = Expr::pitch;
            out.value = (double) bendCents ((bytes[1] & 0x7f) | ((bytes[2] & 0x7f) << 7), bendRange) / 100.0;
            return true;
        case 0xd0:
            out.expr = Expr::pressure;
            out.value = (double) (bytes[1] & 0x7f) / 127.0;
            return true;
        case 0xb0:
            if (size < 3 || (bytes[1] & 0x7f) != 74)
                return false;
            out.expr = Expr::timbre;
            out.value = (double) (bytes[2] & 0x7f) / 127.0;
            return true;
        default:
            return false;
    }
}
} // namespace lynx::mpe
