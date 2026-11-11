#pragma once

// Live notes that something started and must stop again: UI previews (step clicks, the Piano Roll keyboard,
// the typing piano, auditions) and keys held on a MIDI keyboard. Every note-on is matched by exactly one
// note-off for the same channel and key, however the gesture ends (mouse released elsewhere, focus lost,
// window hidden, the selected channel or the scale snap changed while the key was held).

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace lynx
{
class HeldNotes
{
public:
    using Sender = std::function<void (int channel, int key, float velocity, bool on)>;

    HeldNotes() = default;
    explicit HeldNotes (Sender sender) : send (std::move (sender)) {}
    ~HeldNotes() { releaseAll(); }
    HeldNotes (const HeldNotes&) = delete;
    HeldNotes& operator= (const HeldNotes&) = delete;

    void setSender (Sender sender) { send = std::move (sender); }

    // Starts a note. `tag` groups notes started together (the typing piano: the key code) so they can be
    // released together. A note already held (same channel and key) is stopped first, never doubled.
    void press (int channel, int key, float velocity, int tag = 0)
    {
        if (channel < 0 || key < 0 || key > 127)
            return;

        release (channel, key);
        held.push_back ({ channel, key, tag });
        if (send)
            send (channel, key, velocity, true);
    }

    // Stops one note (nothing happens if it is not held).
    void release (int channel, int key)
    {
        for (std::size_t i = 0; i < held.size(); ++i)
            if (held[i].channel == channel && held[i].key == key)
            {
                held.erase (held.begin() + (std::ptrdiff_t) i);
                if (send)
                    send (channel, key, 0.0f, false);
                return;
            }
    }

    // Stops every note started with this tag.
    void releaseTag (int tag)
    {
        for (std::size_t i = 0; i < held.size();)
            if (held[i].tag == tag)
            {
                const auto h = held[i];
                held.erase (held.begin() + (std::ptrdiff_t) i);
                if (send)
                    send (h.channel, h.key, 0.0f, false);
            }
            else
                ++i;
    }

    // Stops everything (mouse up, focus lost, hidden, destroyed).
    void releaseAll()
    {
        auto doomed = std::move (held);
        held.clear();
        if (send)
            for (const auto& h : doomed)
                send (h.channel, h.key, 0.0f, false);
    }

    bool isHeld (int channel, int key) const noexcept
    {
        for (const auto& h : held)
            if (h.channel == channel && h.key == key)
                return true;
        return false;
    }

    bool hasTag (int tag) const noexcept
    {
        for (const auto& h : held)
            if (h.tag == tag)
                return true;
        return false;
    }

    template <typename Fn>
    void forEachTag (Fn&& fn) const
    {
        std::vector<int> tags;
        for (const auto& h : held)
            if (std::find (tags.begin(), tags.end(), h.tag) == tags.end())
                tags.push_back (h.tag);
        for (auto t : tags)
            fn (t);
    }

    bool empty() const noexcept { return held.empty(); }
    std::size_t size() const noexcept { return held.size(); }

private:
    struct Held
    {
        int channel, key, tag;
    };
    std::vector<Held> held;
    Sender send;
};

// The key a played note really started (after the scale snap), so its note-off stops that same key even when
// the snap changed while the key was held. UI thread.
class KeyLedger
{
public:
    void on (int channel, int key, int resolved)
    {
        entries.push_back ({ channel, key, resolved });
    }

    // the key the matching note-on started (the oldest one still open), or `fallback` if none
    int off (int channel, int key, int fallback)
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (entries[i][0] == channel && entries[i][1] == key)
            {
                const auto r = entries[i][2];
                entries.erase (entries.begin() + (std::ptrdiff_t) i);
                return r;
            }
        return fallback;
    }

    std::size_t size() const noexcept { return entries.size(); }

private:
    std::vector<std::array<int, 3>> entries;
};

// MIDI input: which channels and key each incoming note number started, so the note-off reaches them even if
// the selected channels or the scale snap changed while the key was down. Fixed size, no allocation (the MIDI
// thread uses it).
class InputHeldNotes
{
public:
    static constexpr int maxTargets = 17;

    struct Target
    {
        int channel = -1, key = -1;
    };

    // Remembers the targets of `note`. If the note was already down (no note-off came in between) the previous
    // targets are returned in `stale` (they must be stopped first); returns how many.
    int noteOn (int note, const Target* targets, int count, Target* stale)
    {
        if (note < 0 || note > 127)
            return 0;

        auto& e = notes[(std::size_t) note];
        auto staleCount = 0;
        for (int i = 0; i < e.count; ++i)
            stale[staleCount++] = e.targets[(std::size_t) i];

        e.count = count < maxTargets ? count : maxTargets;
        for (int i = 0; i < e.count; ++i)
            e.targets[(std::size_t) i] = targets[i];
        return staleCount;
    }

    // The targets `note` started, kept (issue #108: an MPE note's expression goes where its note on went)
    int targetsOf (int note, Target* out) const
    {
        if (note < 0 || note > 127)
            return 0;

        const auto& e = notes[(std::size_t) note];
        for (int i = 0; i < e.count; ++i)
            out[i] = e.targets[(std::size_t) i];
        return e.count;
    }

    // The targets `note` started (forgotten); returns how many. 0: the note was not down.
    int noteOff (int note, Target* out)
    {
        if (note < 0 || note > 127)
            return 0;

        auto& e = notes[(std::size_t) note];
        const auto count = e.count;
        for (int i = 0; i < count; ++i)
            out[i] = e.targets[(std::size_t) i];
        e.count = 0;
        return count;
    }

private:
    struct Entry
    {
        std::array<Target, maxTargets> targets {};
        int count = 0;
    };
    std::array<Entry, 128> notes {};
};
} // namespace lynx
