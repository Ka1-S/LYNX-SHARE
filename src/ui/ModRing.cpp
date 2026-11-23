#include "ui/ModRing.h"

#include <algorithm>
#include <cmath>

#include "engine/AudioEngine.h"
#include "ui/Theme.h"

namespace lynx::modring
{
namespace
{
AudioEngine* currentEngine = nullptr;
int armedModulator = -1;
constexpr const char* targetProperty = "lynxModTarget";

struct DepthDrag
{
    bool active = false;
    AutomationTarget target;
    int modulator = -1;
    float startDepth = 0.0f;
    float moved = 0.0f;
} drag;

juce::String packTarget (const AutomationTarget& t)
{
    return juce::String ((int) t.kind) + ":" + juce::String (t.index) + ":" + juce::String (t.slot) + ":" + juce::String (t.param);
}

std::optional<AutomationTarget> unpackTarget (const juce::String& s)
{
    juce::StringArray parts;
    parts.addTokens (s, ":", {});
    if (parts.size() != 4)
        return std::nullopt;
    return AutomationTarget { (AutomationTargetKind) parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue(), parts[3].getIntValue() };
}
} // namespace

void setEngine (AudioEngine* e) noexcept
{
    currentEngine = e;
}
AudioEngine* engine() noexcept
{
    return currentEngine;
}

juce::Colour colour() noexcept
{
    return theme::colour::automation;
}

juce::Colour colourAgainst (juce::Colour knobAccent) noexcept
{
    const auto ring = colour();
    if (knobAccent.isTransparent() || knobAccent.getSaturation() < 0.25f)
        return ring;
    auto gap = std::abs (knobAccent.getHue() - ring.getHue());
    gap = std::min (gap, 1.0f - gap);
    return gap < 0.12f ? juce::Colour (0xffc4a8ff) : ring; // (a light violet beside a teal knob)
}

bool ringFor (const AutomationTarget& target, Ring& out)
{
    out = {};
    if (currentEngine == nullptr || target.kind == AutomationTargetKind::none)
        return false;
    const auto& mods = currentEngine->getSong().modulators;
    if (mods.empty())
        return false;
    for (int m = 0; m < (int) mods.size() && out.count < Ring::maxRoutes; ++m)
        for (const auto& r : mods[(std::size_t) m].routes)
            if (r.target == target && r.depth != 0.0f)
            {
                if (m == armedModulator)
                    out.armedRoute = out.count;
                out.depth[out.count] = r.depth;
                out.bipolar[out.count] = ! mods[(std::size_t) m].unipolar;
                ++out.count;
                break;
            }
    // the armed modulator not routed yet: an empty ring is still shown while it is armed (where a drag would put it)
    return out.count > 0;
}

void draw (juce::Graphics& g, juce::Rectangle<float> circle, float value, const Ring& ring, juce::Colour knobAccent, float startAngle, float sweep)
{
    const auto base = colourAgainst (knobAccent);
    if (ring.count <= 0 || circle.isEmpty())
        return;
    const auto size = juce::jmin (circle.getWidth(), circle.getHeight());
    if (size < 14.0f)
        return;
    const auto centre = circle.getCentre();
    const auto stroke = juce::jlimit (1.75f, 3.0f, size * 0.065f);
    const auto at = [&] (float v) { return startAngle + sweep * juce::jlimit (0.0f, 1.0f, v); };
    for (int k = 0; k < ring.count; ++k)
    {
        // each route its own ring just inside the knob's value arc, the next one a stroke further in
        const auto radius = size * 0.5f - size * 0.09f - stroke * (1.4f + 1.6f * (float) k);
        if (radius < stroke * 2.0f)
            break;
        const auto armedOne = k == ring.armedRoute;
        const auto c = base.withAlpha (armedOne || ring.armedRoute < 0 ? 1.0f : 0.55f);
        const auto d = ring.depth[k];
        juce::Path main;
        main.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, at (value), at (value + d), true);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); // (a dark edge: the ring reads on light knob caps too)
        g.strokePath (main, juce::PathStrokeType (stroke + 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (main, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (ring.bipolar[k]) // (a bipolar source swings the other way as much)
        {
            juce::Path other;
            other.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, at (value), at (value - d), true);
            g.setColour (c.withMultipliedAlpha (0.45f));
            g.strokePath (other, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

void drawFor (juce::Graphics& g, juce::Rectangle<float> circle, float value, const AutomationTarget& target, juce::Colour knobAccent)
{
    Ring ring;
    if (ringFor (target, ring))
        draw (g, circle, value, ring, knobAccent);
}

int armed() noexcept
{
    if (currentEngine == nullptr || armedModulator >= (int) currentEngine->getSong().modulators.size())
        return -1;
    return armedModulator;
}

void setArmed (int modulator)
{
    armedModulator = modulator;
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
        if (auto* c = juce::Desktop::getInstance().getComponent (i))
            c->repaint(); // (every ring shows which source is armed)
}

bool assign (int modulator, const AutomationTarget& target)
{
    if (currentEngine == nullptr)
        return false;
    const auto& song = currentEngine->getSong();
    if (modulator < 0 || modulator >= (int) song.modulators.size() || ! Modulator::canModulate (song, target))
        return false;
    const auto& routes = song.modulators[(std::size_t) modulator].routes;
    if (std::any_of (routes.begin(), routes.end(), [&] (const ModRoute& r) { return r.target == target; }))
        return false;
    ModRoute route { target, 0.25f };
    if (Modulator::isHostedTarget (song, target))
        route.base = juce::jlimit (0.0f, 1.0f, currentEngine->currentTargetValue (target));
    currentEngine->editSong ([modulator, route] (Song& s) {
        if (modulator < (int) s.modulators.size())
            s.modulators[(std::size_t) modulator].routes.push_back (route);
    });
    return true;
}

bool beginDepthDrag (const AutomationTarget& target)
{
    const auto m = armed();
    if (m < 0 || currentEngine == nullptr || ! Modulator::canModulate (currentEngine->getSong(), target))
        return false;
    drag = {};
    drag.active = true;
    drag.target = target;
    drag.modulator = m;
    const auto& routes = currentEngine->getSong().modulators[(std::size_t) m].routes;
    const auto it = std::find_if (routes.begin(), routes.end(), [&] (const ModRoute& r) { return r.target == target; });
    if (it == routes.end())
    {
        // the route, at depth 0, as the drag's one undo step
        ModRoute route { target, 0.0f };
        if (Modulator::isHostedTarget (currentEngine->getSong(), target))
            route.base = juce::jlimit (0.0f, 1.0f, currentEngine->currentTargetValue (target));
        currentEngine->editSong ([m, route] (Song& s) {
            if (m < (int) s.modulators.size())
                s.modulators[(std::size_t) m].routes.push_back (route);
        });
    }
    else
    {
        drag.startDepth = it->depth;
        currentEngine->editSong ([] (Song&) {}); // (the undo step: the depth before the drag)
    }
    return true;
}

void dragDepth (float deltaPixels, bool fine)
{
    if (! drag.active || currentEngine == nullptr)
        return;
    drag.moved = deltaPixels;
    const auto depth = juce::jlimit (-1.0f, 1.0f, drag.startDepth + deltaPixels / (fine ? 600.0f : 150.0f));
    currentEngine->editSong ([d = drag, depth] (Song& s) {
        if (d.modulator >= (int) s.modulators.size())
            return;
        for (auto& r : s.modulators[(std::size_t) d.modulator].routes)
            if (r.target == d.target)
                r.depth = depth;
    }, false);
}

void endDepthDrag()
{
    if (drag.active && currentEngine != nullptr)
    {
        // a click without a move on a route made by it: nothing routed
        const auto d = drag;
        currentEngine->editSong ([d] (Song& s) {
            if (d.modulator >= (int) s.modulators.size())
                return;
            auto& routes = s.modulators[(std::size_t) d.modulator].routes;
            routes.erase (std::remove_if (routes.begin(), routes.end(), [&] (const ModRoute& r) { return r.target == d.target && r.depth == 0.0f; }),
                          routes.end());
        }, false);
    }
    drag = {};
}

bool depthDragging() noexcept
{
    return drag.active;
}

void tagSlider (juce::Slider& slider, const AutomationTarget& target)
{
    slider.getProperties().set (targetProperty, packTarget (target));
}

std::optional<AutomationTarget> taggedTarget (const juce::Component& c)
{
    if (const auto* v = c.getProperties().getVarPointer (targetProperty))
        return unpackTarget (v->toString());
    return std::nullopt;
}

std::optional<AutomationTarget> targetUnder (juce::Point<int> screenPosition)
{
    auto* c = juce::Desktop::getInstance().findComponentAt (screenPosition);
    for (; c != nullptr; c = c->getParentComponent())
    {
        if (auto* source = dynamic_cast<TargetSource*> (c))
            if (auto t = source->modTargetAt (c->getLocalPoint (nullptr, screenPosition)))
                return t;
        if (auto t = taggedTarget (*c))
            return t;
    }
    return std::nullopt;
}
} // namespace lynx::modring
