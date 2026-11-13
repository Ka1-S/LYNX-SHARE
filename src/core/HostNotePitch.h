#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Issue #63: a note's own fine pitch on a hosted plugin, as FL's wrapper sends it (docs/analise/REA_rodada188g.md §2).
// Fruity Wrapper's voice (FruityWrapper_x64.dll 0x879a00) makes the note's pitch a float, (Final.Pitch + 6000) · 0.01,
// i.e. key + fine pitch / 100, and hands it to the plugin's note on (the wrapper's VMT slot 0x108):
// - VST2 / MIDI (the base, 0x745f70): the key is Round(pitch) (cvtsd2si: ties to even), the rest is lost;
// - VST3 (0x84d0e0): the same key, and the rest as the note on's tuning, (pitch − key) · 100 cents — a note at +70
//   cents goes out one key up, 30 cents flat;
// - CLAP (0x85e160): the key is Trunc(pitch) (cvttsd2si), no tuning and no note expression event. (FL's CLAP note off,
//   0x85e280, rounds instead, so a note at +50…+99 cents ends on another key than it started; LYNX ends it on its own.)
// Pan is never sent: no VST3 note expression, no CLAP expression, no MIDI pan per note (FL 2026).
namespace lynx::hostpitch
{
struct Sent
{
    int key = 60;        // the MIDI key the note goes out on (0…127)
    float tuning = 0.0f; // cents on top of it (VST3 NoteOnEvent::tuning; MIDI-only formats drop it)
};

enum class Format
{
    midi, // VST2, AU, anything that takes MIDI notes
    vst3,
    clap
};

// `key` the note's key, `cents` its own fine pitch (the piano roll's, not the channel's)
inline Sent of (int key, float cents, Format format = Format::vst3) noexcept
{
    if (! std::isfinite (cents))
        cents = 0.0f;
    // (single precision, as FL: (Final.Pitch + 6000) · 0.01f with Final.Pitch in cents from C5)
    const auto pitch = ((float) (key - 60) * 100.0f + cents + 6000.0f) * 0.01f;
    // (Round: the default rounding mode, ties to even, as cvtsd2si; Trunc: towards zero)
    const auto whole = format == Format::clap ? std::trunc (pitch) : std::nearbyint (pitch);
    Sent s;
    s.key = (int) std::fmin (127.0f, std::fmax (0.0f, whole));
    s.tuning = format == Format::vst3 ? (pitch - whole) * 100.0f : 0.0f;
    return s;
}

// The key each sounding note went out on, so its note off goes to the same one (FL keeps it in the voice); per played
// key, oldest first. And the tunings waiting for the VST3 host to pick up with their note ons (same order).
struct SentKeys
{
    struct Slot
    {
        std::array<std::int8_t, 4> keys {};
        std::array<float, 4> tunings {};
        int count = 0, tuned = 0; // notes sounding; tunings not yet taken
    };
    std::array<Slot, 128> played {};
    std::array<Slot, 128> sent {}; // by the key sent: the tunings, for the VST3 host

    void on (int key, Sent s) noexcept
    {
        auto& p = played[(std::size_t) (key & 127)];
        if (p.count == (int) p.keys.size())
            drop (p.keys, p.count);
        p.keys[(std::size_t) p.count++] = (std::int8_t) s.key;
        auto& t = sent[(std::size_t) s.key];
        if (t.tuned == (int) t.tunings.size())
            drop (t.tunings, t.tuned);
        t.tunings[(std::size_t) t.tuned++] = s.tuning;
    }
    // the key the oldest sounding note of `key` went out on (`key` itself if none)
    int off (int key) noexcept
    {
        auto& p = played[(std::size_t) (key & 127)];
        if (p.count == 0)
            return key;
        const auto k = (int) p.keys[0];
        drop (p.keys, p.count);
        return k;
    }
    // the key the newest sounding note of `key` went out on (`key` itself if none); issue #108
    int sentFor (int key) const noexcept
    {
        const auto& p = played[(std::size_t) (key & 127)];
        return p.count == 0 ? key : (int) p.keys[(std::size_t) (p.count - 1)];
    }
    // the VST3 host's note on for `sentKey`: its tuning (0 when none waits)
    float takeTuning (int sentKey) noexcept
    {
        auto& t = sent[(std::size_t) (sentKey & 127)];
        if (t.tuned == 0)
            return 0.0f;
        const auto v = t.tunings[0];
        drop (t.tunings, t.tuned);
        return v;
    }
    void clearTunings() noexcept
    {
        for (auto& t : sent)
            t.tuned = 0;
    }

    // Round 190fg: which LYNX note (NoteEvent::noteId, 0 = none) each note on and note off that went out on MIDI
    // channel 1 belongs to, per key sent, in the order they went out. The patched VST3 host (cmake/LynxPatchJuce.cmake
    // fix 26) converts the block's MIDI in the same order and takes one tag per note on / off, so a note off ends the
    // VST3 note id of its own note on: of a long note with a short one of the same key inside it, the short note's end
    // ends the short note (fix 14 alone gave it the long note's id). Every note on / off written on channel 1 queues
    // one (0 when it has no note: a live key, a held key's release), so the order cannot slip.
    struct Tags
    {
        std::array<std::uint32_t, 8> ids {};
        int count = 0;
    };
    std::array<Tags, 128> onTags {}, offTags {};

    void tag (bool noteOn, int sentKey, std::uint32_t id) noexcept
    {
        auto& t = (noteOn ? onTags : offTags)[(std::size_t) (sentKey & 127)];
        if (t.count == (int) t.ids.size()) // (never taken: no VST3 host read the block)
            drop (t.ids, t.count);
        t.ids[(std::size_t) t.count++] = id;
    }
    std::uint32_t takeTag (bool noteOn, int sentKey) noexcept
    {
        auto& t = (noteOn ? onTags : offTags)[(std::size_t) (sentKey & 127)];
        if (t.count == 0)
            return 0;
        const auto id = t.ids[0];
        drop (t.ids, t.count);
        return id;
    }
    void clearTags() noexcept
    {
        onTags = {};
        offTags = {};
    }

    void clear() noexcept
    {
        played = {};
        sent = {};
        clearTags();
    }

private:
    template <typename A>
    static void drop (A& a, int& count) noexcept
    {
        for (int i = 1; i < count; ++i)
            a[(std::size_t) i - 1] = a[(std::size_t) i];
        --count;
    }
};

// Round 190ad: the MIDI velocity of a note on in a project with FL's levels (Song::flPluginLevels). Fruity Wrapper's
// voice (0x879a00, REA_rodada190ad.md §2) sends Round(velocity · 128) (single precision product, ties to even),
// clamped to 0…127: an FL note at velocity 100 (100 / 128) goes out as 100; LYNX's own law, JUCE's round(127 v), gives
// 99. FL lets it reach 0; LYNX keeps a note on at 1 or more, as a velocity 0 note on is a note off on the wire (no
// .flp note gets there: its lowest velocity byte is 1).
inline int flWrapperVelocity (float velocity) noexcept
{
    if (! (velocity > 0.0f))
        return 1;
    const auto v = (int) std::nearbyint ((double) (std::min (velocity, 1.0f) * 128.0f));
    return v < 1 ? 1 : (v > 127 ? 127 : v);
}
} // namespace lynx::hostpitch
