#pragma once

// Touch mode (round 190pm, research190oe P23; FL Studio Mobile and BandLab on a tablet, interpreted): the gestures of the
// fingers on an editor, as a state machine with no JUCE in it (the editors feed it their touch events, ui/TouchInput.h
// turns its actions back into their own mouse calls; tests feed it directly).
//
// A finger that comes down is held back, because what it means is not known yet:
//  - it moves past the slop: a one-finger edit (draw, move, select). The press is given to the editor where it happened
//    and the drag goes on as a mouse drag;
//  - it lifts before that: a tap (the press and the release, a click);
//  - it stays still for the long-press time: a long press, the editor's right click (its menu) at that place; nothing
//    else of that finger reaches the editor;
//  - a second finger comes down before the first moved (two-finger mode only, the Simple layout's timeline and editor):
//    the two fingers pan (the move of their centre) and pinch (the change of their distance) until both are up; the
//    first finger's press never reaches the editor.
// A mouse never goes through this (the editors only feed it touch input).

#include <cmath>
#include <cstdint>
#include <vector>

namespace lynx::touch
{
struct Point
{
    float x = 0.0f, y = 0.0f;
};

struct Options
{
    double longPressMs = 500.0; // Windows' press-and-hold time
    float slop = 10.0f;         // px a finger may move and still be a press
    bool twoFinger = true;      // pan / pinch with two fingers (false: a second finger is ignored)
};

enum class Action
{
    press,     // give the held press to the editor (at `at`, the finger's first place), the drag / release follow as mouse events
    longPress, // the editor's right click at `at`
    pan,       // two fingers moved their centre by `delta`
    pinch,     // two fingers changed their distance by `scale` (> 1 apart), around `at`
    gestureEnd // the two-finger gesture ended (the last finger is up)
};

struct Event
{
    Action action = Action::press;
    Point at, delta;
    float scale = 1.0f;
};

class Tracker
{
public:
    enum class State
    {
        idle,
        held,      // one finger down, not yet known
        dragging,  // a one-finger edit: its events go to the editor as they come
        longPress, // the long press was given: the finger's later events are swallowed
        twoFinger  // pan / pinch until every finger is up
    };

    explicit Tracker (Options o = {}) : options (o) {}

    State state() const noexcept { return now; }
    void setTwoFinger (bool on) noexcept { options.twoFinger = on; } // (between gestures)
    // a finger's events, by its id (the touch source's index); true = swallowed (the editor must not act on it)
    bool down (int id, Point p, double ms)
    {
        if (now == State::idle)
        {
            first = { id, p, p };
            downMs = ms;
            now = State::held;
            return true;
        }
        if (now == State::held && options.twoFinger && id != first.id)
        {
            second = { id, p, p };
            now = State::twoFinger;
            return true;
        }
        if (now == State::held && id != first.id) // (no two-finger gestures here: the first finger is a press now)
        {
            press();
            now = State::dragging;
            return true; // (the second finger itself is ignored)
        }
        return now != State::dragging || id != first.id;
    }

    bool move (int id, Point p, double ms)
    {
        (void) ms;
        switch (now)
        {
            case State::held:
                if (id != first.id)
                    return true;
                if (std::hypot (p.x - first.start.x, p.y - first.start.y) > options.slop)
                {
                    press();
                    now = State::dragging;
                    first.at = p;
                    return false; // (this drag reaches the editor)
                }
                return true;
            case State::dragging: return id != first.id;
            case State::twoFinger:
            {
                Finger* f = id == first.id ? &first : id == second.id ? &second : nullptr;
                if (f == nullptr || (! first.down || ! second.down))
                    return true;
                const auto c0 = centre();
                const auto d0 = distance();
                f->at = p;
                const auto c1 = centre();
                const auto d1 = distance();
                if (c1.x != c0.x || c1.y != c0.y)
                    out.push_back ({ Action::pan, c1, { c1.x - c0.x, c1.y - c0.y }, 1.0f });
                if (d0 > 1.0f && d1 > 1.0f && std::abs (d1 - d0) > 0.0f)
                    out.push_back ({ Action::pinch, c1, {}, d1 / d0 });
                return true;
            }
            default: return now == State::longPress || id != first.id;
        }
    }

    bool up (int id, Point p, double ms)
    {
        (void) ms;
        switch (now)
        {
            case State::held:
                if (id != first.id)
                    return true;
                first.at = p;
                press(); // a tap: the press, then this release reaches the editor
                now = State::idle;
                return false;
            case State::dragging:
                if (id != first.id)
                    return true;
                now = State::idle;
                return false;
            case State::longPress:
                if (id == first.id)
                    now = State::idle;
                return true;
            case State::twoFinger:
                if (id == first.id)
                    first.down = false;
                else if (id == second.id)
                    second.down = false;
                else
                    return true;
                if (! first.down && ! second.down)
                {
                    out.push_back ({ Action::gestureEnd, p, {}, 1.0f });
                    now = State::idle;
                }
                return true;
            default: return true;
        }
    }

    // time passing (a timer while a finger is held): the long press
    void tick (double ms)
    {
        if (now == State::held && ms - downMs >= options.longPressMs)
        {
            out.push_back ({ Action::longPress, first.start, {}, 1.0f });
            now = State::longPress;
        }
    }

    // a finger's touch was cancelled (the window lost it): back to idle, nothing given
    void reset() noexcept { now = State::idle; }

    // the actions since the last call
    std::vector<Event> take()
    {
        std::vector<Event> taken;
        taken.swap (out);
        return taken;
    }

    bool waitingForLongPress() const noexcept { return now == State::held; }

private:
    struct Finger
    {
        int id = -1;
        Point start, at;
        bool down = true;
    };

    void press() { out.push_back ({ Action::press, first.start, {}, 1.0f }); }
    Point centre() const noexcept { return { (first.at.x + second.at.x) * 0.5f, (first.at.y + second.at.y) * 0.5f }; }
    float distance() const noexcept { return std::hypot (first.at.x - second.at.x, first.at.y - second.at.y); }

    Options options;
    State now = State::idle;
    Finger first, second;
    double downMs = 0.0;
    std::vector<Event> out;
};
} // namespace lynx::touch
