#pragma once

// FL's channel polyphony layer (Channel settings › Misc › Polyphony: Mono, Porta, Slide; REA_rodada168l). FL applies it
// in its channel's voice code for every generator, before the generator sees a voice (FLEngine 0xd293d0 / 0xd237e0 /
// 0xd236d0 / 0xd23c00 / 0xfeaf70 / 0xd26460), so LYNX's rack does it the same way in front of every instrument that
// does not do it itself (the LYNX Synth: its own Mono / Porta; the Sampler: its own Porta).
//
// - Porta (the channel's box, inverted by a note's porta flag): a new voice starts at the pitch of the last note
//   triggered in its colour group and goes linearly (in cents) to its own over FL's glide time. Not with the
//   arpeggiator on, not with the echo delay's feedback up, not before a first note, not with a Slide of 0 ticks.
// - Mono: the notes of a colour group share one voice. A note played while others are held starts no voice: the
//   sounding voice glides (FL's glide time, Porta box or not) to it; releasing it glides back to the newest note still
//   held; the voice ends when the last held note ends. Not with the arpeggiator on (the limiter of Max polyphony does
//   not run on a Mono channel, VoiceLimit.h).
//
// JUCE-free and real-time safe (fixed storage, no allocation).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lynx::channelpoly
{
// FL's porta ticks for a Slide time (`portaBeats` = the knob's n / 192 beat, n = round((27^(v/1024) − 1)·48/26),
// REA_rodada6 §4.2): ticks = n · (PPQ / 4) / 48 at FL's 96 PPQ, integer division (FLEngine 0x1312200)
inline int portaTicks (float portaBeats) noexcept
{
    const auto n = (int) std::lround (std::clamp (portaBeats, 0.0f, 8.0f) * 192.0f);
    return n * (96 / 4) / 48;
}

// FL's glide length: ticks + 1 ticks (0xd26460: the 5 per-note values step by (to − from) / (ticks + 1) a tick), in beats
inline double glideBeats (float portaBeats) noexcept { return (portaTicks (portaBeats) + 1) / 96.0; }

struct Mode
{
    bool mono = false;       // the channel's Mono (handled here)
    bool porta = false;      // the channel's Porta (handled here)
    float portaBeats = 0.125f;
    bool arpeggiator = false; // FL: no mono list, no glide with the arpeggiator on (0xd28910, channel +0x2ac)
    bool echo = false;        // FL: no glide with the echo delay's feedback up (0xd237e0 / 0xd26460, channel +0x200)
    bool slides = true;       // the instrument can move a sounding voice's pitch (Instrument::slideTo)
    bool glidesIn = true;     // the instrument's new voices can glide from another pitch (Instrument::followsChannelPorta)
};

// What the rack does for a note on
struct On
{
    bool start = true;        // start a voice (noteOn)
    float glideFrom = 0.0f;   // start it this many cents away (from the group's last note) and glide to its pitch
    bool glide = false;
    int release = -1;         // release this key first (Mono on an instrument that cannot slide: the old voice goes)
    bool slide = false;       // (no new voice) the sounding voice glides to `key` + `cents`
};

// ... and for a note off
struct Off
{
    int release = -1;         // release this key (the sounding voice's own key)
    bool slide = false;       // the sounding voice glides back to `key` + `cents`
    int key = 0;
    float cents = 0.0f;
};

class Layer
{
public:
    static constexpr int groups = 16, maxHeld = 32;

    // `cents` = the note's own fine pitch (note cents + fine tune + main pitch), `colour` its colour group
    On noteOn (const Mode& m, int key, float cents, bool notePorta, int colour) noexcept
    {
        On on;
        const auto g = (std::size_t) std::clamp (colour, 0, groups - 1);
        const auto pitch = 100.0f * (float) key + cents;
        const auto glideOk = ! m.arpeggiator && ! m.echo;
        if (m.mono && ! m.arpeggiator)
        {
            const auto overlapping = held (g) > 0 && sounding[g] >= 0;
            push (key, cents, (int) g);
            if (overlapping && m.slides)
            {
                // a virtual voice (FL's state −10): the sounding one glides to it (0xd236d0 → 0xfeaf70)
                // (with the echo delay's feedback up FL's glide does not run: the pitch stays, 0xd26460)
                on.start = false;
                on.slide = glideOk;
                last[g] = pitch; // (0xd23db0 runs for the virtual voice too: it is the group's last note)
                return on;
            }
            if (overlapping) // (an instrument that cannot slide: the new note takes the voice over)
                on.release = sounding[g];
            sounding[g] = key;
        }
        // FL's porta (0xd237e0): channel porta XOR the note's flag, a note before, at least 1 tick
        const auto porta = (m.porta != notePorta) && glideOk && m.glidesIn;
        if (porta && ! std::isnan (last[g]) && portaTicks (m.portaBeats) >= 1 && last[g] != pitch)
        {
            on.glide = true;
            on.glideFrom = last[g] - pitch;
        }
        last[g] = pitch;
        return on;
    }

    Off noteOff (const Mode& m, int key, int colour) noexcept
    {
        Off off;
        const auto g = (std::size_t) std::clamp (colour, 0, groups - 1);
        if (! remove (key, (int) g))
        {
            off.release = key; // not one of the Mono notes: an ordinary note off
            return off;
        }
        if (held (g) == 0)
        {
            off.release = sounding[g] >= 0 ? sounding[g] : key;
            sounding[g] = -1;
            return off;
        }
        // the sounding voice glides back to the newest note still held (0xd23c00 → 0xfeaf70)
        if (const auto& back = newest (g); m.slides && ! m.echo)
        {
            off.slide = true;
            off.key = back.key;
            off.cents = back.cents;
            last[g] = 100.0f * (float) back.key + back.cents;
        }
        return off;
    }

    // the notes stop (transport, jump, panic, cut group): no voice sounds; FL keeps the last note for the next porta
    void clear() noexcept
    {
        count = 0;
        sounding.fill (-1);
    }
    // the channel changed (FL resets the groups' last notes when it sets the channel up, 0xfea880 → 0xfeac10)
    void reset() noexcept
    {
        clear();
        last.fill (NAN);
    }
    bool active() const noexcept { return count > 0; }

private:
    struct Held
    {
        std::int16_t key = 0;
        std::int8_t group = 0;
        float cents = 0.0f;
    };

    int held (std::size_t g) const noexcept
    {
        int n = 0;
        for (int i = 0; i < count; ++i)
            n += notes[(std::size_t) i].group == (int) g ? 1 : 0;
        return n;
    }
    const Held& newest (std::size_t g) const noexcept
    {
        for (int i = count - 1; i > 0; --i)
            if (notes[(std::size_t) i].group == (int) g)
                return notes[(std::size_t) i];
        return notes[0];
    }
    void push (int key, float cents, int g) noexcept
    {
        if (count == maxHeld) // (full: the oldest goes)
        {
            std::copy (notes.begin() + 1, notes.end(), notes.begin());
            --count;
        }
        notes[(std::size_t) count++] = { (std::int16_t) key, (std::int8_t) g, cents };
    }
    bool remove (int key, int g) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (notes[(std::size_t) i].key == key && notes[(std::size_t) i].group == g)
            {
                std::copy (notes.begin() + i + 1, notes.begin() + count, notes.begin() + i);
                --count;
                return true;
            }
        return false;
    }

    std::array<Held, maxHeld> notes {};
    int count = 0;
    std::array<int, groups> sounding = filled (-1);
    std::array<float, groups> last = filledF (NAN); // pitch (cents) of each group's last note

    static constexpr std::array<int, groups> filled (int v) noexcept
    {
        std::array<int, groups> a {};
        for (auto& x : a)
            x = v;
        return a;
    }
    static std::array<float, groups> filledF (float v) noexcept
    {
        std::array<float, groups> a {};
        a.fill (v);
        return a;
    }
};
} // namespace lynx::channelpoly
