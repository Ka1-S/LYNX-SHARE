#include "ui/DeleteFade.h"

#include <algorithm>
#include <cmath>

#include "ui/Motion.h"
#include "ui/PluginSkin.h"

namespace lynx::ui
{
namespace
{
constexpr float growPx = 4.0f; // FL: round(4·scale − 0.5) at 100 %, in logical pixels here (JUCE scales them)
}

bool DeleteFade::enabled()
{
    return motion::enabled() && ! skin::reducedEffects();
}

juce::Colour DeleteFade::ghostColour (juce::Colour c)
{
    // FL's FUN_00660dc0 (c, 0.55, 0.80): HSL, the lightness clamped, hue and saturation kept
    const auto r = c.getFloatRed(), g = c.getFloatGreen(), b = c.getFloatBlue();
    const auto mx = std::max ({ r, g, b }), mn = std::min ({ r, g, b });
    const auto l = (mx + mn) * 0.5f;
    const auto target = std::clamp (l, 0.55f, 0.80f);
    if (mx - mn < 1.0e-6f)
        return juce::Colour::fromFloatRGBA (target, target, target, 1.0f);
    const auto d = mx - mn;
    const auto s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    float h = mx == r ? (g - b) / d + (g < b ? 6.0f : 0.0f) : mx == g ? (b - r) / d + 2.0f : (r - g) / d + 4.0f;
    h /= 6.0f;
    const auto q = target < 0.5f ? target * (1.0f + s) : target + s - target * s;
    const auto p = 2.0f * target - q;
    const auto hue = [p, q] (float t) {
        t = t < 0.0f ? t + 1.0f : t > 1.0f ? t - 1.0f : t;
        return t < 1.0f / 6.0f ? p + (q - p) * 6.0f * t : t < 0.5f ? q : t < 2.0f / 3.0f ? p + (q - p) * (2.0f / 3.0f - t) * 6.0f : p;
    };
    return juce::Colour::fromFloatRGBA (hue (h + 1.0f / 3.0f), hue (h), hue (h - 1.0f / 3.0f), 1.0f);
}

double DeleteFade::alphaAt (double ms) const noexcept
{
    if (decay <= 0.0)
        return 0.0;
    const auto a = std::pow (0.01, std::max (0.0, ms) / decay);
    return a < dropAlpha ? 0.0 : a;
}

double DeleteFade::lifetimeMs() const noexcept { return decay * std::log (dropAlpha) / std::log (0.01); }

void DeleteFade::add (juce::Rectangle<float> area, juce::Colour colour)
{
    if (! enabled() || area.isEmpty() || (! view.isShowing() && frozen < 0.0))
        return;
    constexpr std::size_t maxGhosts = 256; // (a Delete of thousands of notes fades the first ones only)
    if (ghosts.size() >= maxGhosts)
        return;
    // FL widens a rectangle narrower than 2u by u on each side (u ≈ 2 px)
    if (area.getWidth() < 4.0f)
        area = area.withSizeKeepingCentre (area.getWidth() + 4.0f, area.getHeight());
    if (area.getHeight() < 4.0f)
        area = area.withSizeKeepingCentre (area.getWidth(), area.getHeight() + 4.0f);
    ghosts.push_back ({ area, ghostColour (colour), juce::Time::getMillisecondCounterHiRes() });
    view.repaint (dirtyArea());
    if (frozen < 0.0 && ! isTimerRunning())
        startTimerHz (60);
}

double DeleteFade::ageOf (const Ghost& g) const
{
    return frozen >= 0.0 ? frozen : juce::Time::getMillisecondCounterHiRes() - g.start;
}

void DeleteFade::paint (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    if (ghosts.empty())
        return;
    juce::Graphics::ScopedSaveState keep (g);
    if (! clip.isEmpty())
        g.reduceClipRegion (clip);
    for (const auto& ghost : ghosts)
    {
        const auto a = (float) alphaAt (ageOf (ghost));
        if (a <= 0.0f)
            continue;
        // FL: inflated by 4 px, then inset by a · 4 px: on the item at first, 4 px out at the end
        const auto area = ghost.area.expanded (growPx * (1.0f - a));
        const auto corner = std::min (0.5f * std::min (area.getWidth(), area.getHeight()), 8.0f);
        const auto fill = 224.0f * a - 96.0f;
        if (fill > 0.0f)
        {
            g.setColour (ghost.colour.withAlpha (fill / 255.0f));
            g.fillRoundedRectangle (area, corner);
        }
        const auto pen = std::min (0.5f * std::min (area.getWidth(), area.getHeight()), 3.0f * a + 0.5f);
        g.setColour (ghost.colour.withAlpha (224.0f * a / 255.0f));
        g.drawRoundedRectangle (area.reduced (pen * 0.5f), corner, pen);
    }
}

juce::Rectangle<int> DeleteFade::dirtyArea() const
{
    juce::Rectangle<float> all;
    for (const auto& ghost : ghosts)
        all = all.isEmpty() ? ghost.area : all.getUnion (ghost.area);
    return all.getSmallestIntegerContainer().expanded ((int) growPx + 2);
}

void DeleteFade::timerCallback()
{
    const auto dirty = dirtyArea();
    const auto life = lifetimeMs();
    const auto now = juce::Time::getMillisecondCounterHiRes();
    ghosts.erase (std::remove_if (ghosts.begin(), ghosts.end(), [now, life] (const Ghost& g) { return now - g.start >= life; }), ghosts.end());
    view.repaint (dirty);
    if (ghosts.empty() || ! view.isShowing() || ! enabled())
    {
        ghosts.clear();
        stopTimer();
    }
}

void DeleteFade::clear()
{
    if (ghosts.empty())
        return;
    view.repaint (dirtyArea());
    ghosts.clear();
    stopTimer();
}

void DeleteFade::freezeForSnapshot (double ms)
{
    frozen = ms;
    stopTimer();
}
} // namespace lynx::ui
