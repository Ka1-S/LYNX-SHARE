#pragma once

// Round 190qi (DETAILS-2 D2-BR4, LYNX-own; FL users ask for it, IL forum p=2059228): a MIDI file selected in the Browser
// is heard on the selected channel's instrument at the project's tempo, like a sample's preview. The file's notes
// (tracks merged, engine/MidiFileIo loadNotes) become timed note-ons / note-offs from the first note on (the silence
// before it is skipped), at most `maxSeconds` long (the notes still sounding then end there). The Browser plays them
// on the live note queue as the time comes, and stops them all at the next preview, a transport start or Stop.
// No JUCE (tests/Picker190qiTests.inc).

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "core/Song.h"

namespace lynx::midipreview
{
// Rollup 0.1.32 step 3 (the owner's choice): a LYNX extra with a switch on Settings › LYNX extras ("browser.midiPreview"),
// on by default; off, a MIDI file clicked in the Browser plays nothing, as in FL Studio (message thread)
inline bool& enabled() noexcept
{
    static bool on = true;
    return on;
}

struct Event
{
    double ms = 0.0; // from the preview's start
    int key = 60;
    float velocity = 0.0f;
    bool on = true;
    bool operator== (const Event&) const = default;
};

inline std::vector<Event> schedule (const std::vector<Note>& notes, double bpm, double maxSeconds = 30.0)
{
    std::vector<Event> out;
    if (notes.empty() || bpm <= 0.0)
        return out;
    auto first = std::numeric_limits<std::int64_t>::max();
    for (const auto& n : notes)
        if (! n.muted)
            first = std::min (first, n.start);
    if (first == std::numeric_limits<std::int64_t>::max())
        return out; // (every note muted)
    const auto tickMs = [bpm] (std::int64_t ticks) { return (double) ticks * 60000.0 / (bpm * (double) ppq); }; // (whole ms stay whole)
    const auto limit = maxSeconds * 1000.0;
    for (const auto& n : notes)
    {
        if (n.muted || n.key < 0 || n.key > 127)
            continue;
        const auto on = tickMs (n.start - first);
        if (on >= limit)
            continue;
        const auto off = std::min (limit, tickMs (n.start - first + std::max<std::int64_t> (1, n.length)));
        out.push_back ({ on, n.key, std::clamp (n.velocity, 0.01f, 1.0f), true });
        out.push_back ({ off, n.key, 0.0f, false });
    }
    // in time order; at the same time an off first (a repeated key restarts cleanly)
    std::stable_sort (out.begin(), out.end(), [] (const Event& a, const Event& b) {
        if (a.ms != b.ms)
            return a.ms < b.ms;
        return ! a.on && b.on;
    });
    return out;
}

inline double lengthMs (const std::vector<Event>& events) { return events.empty() ? 0.0 : events.back().ms; }

// Plays a schedule: advance (now) hands over every event due by `now` (ms since the preview started); stop() ends the
// notes still sounding (one off per sounding note-on of each key).
class Player
{
public:
    explicit Player (std::vector<Event> e = {}) : events (std::move (e)) {}

    template <typename Send>
    void advance (double nowMs, Send&& send)
    {
        while (next < events.size() && events[next].ms <= nowMs)
        {
            const auto& e = events[next++];
            auto& count = sounding[e.key];
            if (e.on)
                ++count;
            else if (count > 0)
                --count;
            else
                continue; // (an off whose on was never sent)
            send (e);
        }
    }

    template <typename Send>
    void stop (Send&& send)
    {
        for (auto& [key, count] : sounding)
            for (; count > 0; --count)
                send (Event { 0.0, key, 0.0f, false });
        sounding.clear();
        next = events.size();
    }

    bool done() const noexcept { return next >= events.size(); }
    double lengthMs() const noexcept { return midipreview::lengthMs (events); }
    double nextMs() const noexcept { return next < events.size() ? events[next].ms : -1.0; }
    const std::vector<Event>& all() const noexcept { return events; }

private:
    std::vector<Event> events;
    std::size_t next = 0;
    std::map<int, int> sounding;
};
} // namespace lynx::midipreview
