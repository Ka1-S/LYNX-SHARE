#pragma once

// Round 190qi (the paint gate of the plugin lists): a small picture that many rows draw the same way — a format chip
// ("CLAP"), a ticked / empty checkbox — is drawn once per key, size and display scale, then only copied into place
// (a blit instead of a rounded path and a text layout per row). Message thread only. LYNX_PAINT_CACHE=0 draws it every
// time, as before (ui/PaintAB.h).

#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>

#include "ui/PaintAB.h"

namespace lynx::paintstamp
{
// Draws `draw` (given a Graphics and the stamp's own bounds at 0, 0) at `area`'s top left. With `background` (the
// colour under the stamp, known to the caller) the picture is opaque, drawn over it: a plain copy, no blending.
inline void stamp (juce::Graphics& g, const juce::String& key, juce::Rectangle<int> area,
                   const std::function<void (juce::Graphics&, juce::Rectangle<float>)>& draw, std::optional<juce::Colour> background = {})
{
    if (area.isEmpty())
        return;
    if (! ui::paintCache())
    {
        juce::Graphics::ScopedSaveState keep (g);
        g.setOrigin (area.getPosition());
        draw (g, area.withZeroOrigin().toFloat());
        return;
    }
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto full = key + "|" + juce::String (area.getWidth()) + "x" + juce::String (area.getHeight()) + "@" + juce::String (scale, 3)
                      + (background.has_value() ? "|" + juce::String::toHexString ((juce::int64) background->getARGB()) : juce::String());
    static std::unordered_map<juce::String, juce::Image> cache;
    auto it = cache.find (full);
    if (it == cache.end())
    {
        if (cache.size() > 256) // (a long session's keys: start again rather than grow)
            cache.clear();
        juce::Image image (background.has_value() ? juce::Image::RGB : juce::Image::ARGB, juce::jmax (1, (int) std::ceil ((float) area.getWidth() * scale)),
                           juce::jmax (1, (int) std::ceil ((float) area.getHeight() * scale)), true);
        {
            juce::Graphics ig (image);
            if (background.has_value())
                ig.fillAll (background->withAlpha (1.0f));
            ig.addTransform (juce::AffineTransform::scale (scale));
            draw (ig, area.withZeroOrigin().toFloat());
        }
        it = cache.emplace (full, image).first;
    }
    if (scale == 1.0f)
        g.drawImageAt (it->second, area.getX(), area.getY()); // (the plain copy: no resampling path)
    else
        g.drawImageTransformed (it->second, juce::AffineTransform::scale (1.0f / scale).translated ((float) area.getX(), (float) area.getY()));
}
} // namespace lynx::paintstamp
