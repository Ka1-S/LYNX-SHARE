#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include <juce_events/juce_events.h>

#include "engine/AudioEngine.h"

namespace lynx::ui
{
// Watches a mixer slot for a native effect window: when the slot is removed or now holds another kind of effect (the
// effect was deleted or replaced, the track removed), `onGone` runs once and the window closes itself (issue #9: an
// editor stayed on screen after its effect was removed).
class SlotGoneWatcher final : private juce::Timer
{
public:
    SlotGoneWatcher (AudioEngine& e, int t, int s, std::function<void()> gone) : engine (e), track (t), slot (s), onGone (std::move (gone))
    {
        if (const auto* fx = find())
        {
            kind = fx->kind;
            startTimerHz (4);
        }
        all().push_back (this);
    }

    ~SlotGoneWatcher() override
    {
        stopTimer();
        auto& list = all();
        list.erase (std::remove (list.begin(), list.end(), this), list.end());
    }

    // Round 170b (FL: a click on a slot's name shows or hides its editor): whether the window of the effect in `slot`
    // is open, and closing it (the window's own close). Message thread only.
    static bool isWindowOpen (const AudioEngine& e, int t, int s)
    {
        return std::any_of (all().begin(), all().end(), [&] (const SlotGoneWatcher* w) { return &w->engine == &e && w->track == t && w->slot == s; });
    }
    static bool closeWindow (const AudioEngine& e, int t, int s)
    {
        std::function<void()> close;
        for (auto* w : all())
            if (&w->engine == &e && w->track == t && w->slot == s && w->onGone)
                close = w->onGone;
        if (! close)
            return false;
        close(); // (destroys the watcher with its window)
        return true;
    }

private:
    static std::vector<SlotGoneWatcher*>& all()
    {
        static std::vector<SlotGoneWatcher*> list;
        return list;
    }

    const EffectSlot* find() const
    {
        const auto& mixer = engine.getSong().mixer;
        if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
            return nullptr;
        return &mixer[(std::size_t) track].effects[(std::size_t) slot];
    }

    void timerCallback() override
    {
        const auto* fx = find();
        if (fx != nullptr && fx->kind == kind)
            return;
        stopTimer();
        if (onGone)
            onGone();
    }

    AudioEngine& engine;
    const int track, slot;
    EffectKind kind {};
    std::function<void()> onGone;
};
} // namespace lynx::ui
