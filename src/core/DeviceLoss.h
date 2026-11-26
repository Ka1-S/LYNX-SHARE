#pragma once

// Round 190rp (0.1.32 Q4, FIX-LIST item 7, owner decision O-4 (a)): the audio device in use went away (unplugged, its
// driver or PipeWire node removed). JUCE closes it and opens what is left (the system default), or nothing; LYNX kept
// "recording" into a take that no longer grew, said nothing, and stayed on the other device when the first came back.
// This is the bookkeeping, without JUCE so it can be tested alone: AudioEngine feeds it the device open now and asks it
// what happened; the engine then stops a recording (the take is kept), posts the notice and reopens the device.

#include <functional>
#include <string>

namespace lynx::deviceloss
{
struct Device
{
    std::string type, name; // AudioIODevice::getTypeName / getName ("" = none open)
    bool empty() const noexcept { return name.empty(); }
    bool operator== (const Device& o) const noexcept { return type == o.type && name == o.name; }
    bool operator!= (const Device& o) const noexcept { return ! (*this == o); }
};

enum class Event
{
    none,
    lost,  // `lost()` went away; `fallback()` (maybe none) was opened in its place
    reopen // `lost()` is listed again and nothing else was chosen meanwhile: open it again
};

class Watch
{
public:
    // `now`: the device open now; `listed`: is this device (type, name) among the devices the system lists now?
    Event update (const Device& now, const std::function<bool (const Device&)>& listed)
    {
        if (! gone.empty())
        {
            if (! now.empty() && now != fallbackDevice && now != gone)
            {
                gone = {}; // the user chose another device meanwhile (Settings › Audio): that choice stays
                running = now;
                return Event::none;
            }
            if (now == gone)
            {
                gone = {}; // (it came back by itself: JUCE or the driver reopened it)
                running = now;
                return Event::none;
            }
            return listed (gone) ? Event::reopen : Event::none;
        }
        if (running.empty() || now == running)
        {
            if (! now.empty())
                running = now;
            return Event::none;
        }
        if (listed (running))
        {
            running = now; // another device was chosen while this one is still there: a switch, not a loss
            return Event::none;
        }
        gone = running;
        fallbackDevice = now;
        running = now;
        return Event::lost;
    }
    // the reopen worked (`now` is the device again) or was given up
    void reopened (const Device& now)
    {
        gone = {};
        running = now;
    }
    const Device& lost() const noexcept { return gone; }
    const Device& fallback() const noexcept { return fallbackDevice; }
    const Device& current() const noexcept { return running; }

private:
    Device running, gone, fallbackDevice;
};
} // namespace lynx::deviceloss
