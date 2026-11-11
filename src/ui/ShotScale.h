#pragma once

// Round 190bi: the drawing side of core/ShotScale.h — one pixel scale for every headless picture route.
//   LYNX.exe --snapshot out\s Demo.lynx --shot-scale 2      (or LYNX_SHOT_SCALE=2; LYNX_PARITYSHOT_SCALE=2 too)
//   --shot-suffix @2x / LYNX_SHOT_SUFFIX=@2x: "s-mixer.png" is written as "s-mixer@2x.png" (default: names unchanged)
// Without any of them every route draws exactly what it drew before (scale 1, same calls, same pixels).

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ShotScale.h"

namespace lynx::shot
{
namespace detail
{
inline juce::String& argumentScale()
{
    static juce::String text;
    return text;
}
inline juce::String& argumentSuffix()
{
    static juce::String text;
    static bool read = false;
    if (! read)
        read = true, text = juce::SystemStats::getEnvironmentVariable ("LYNX_SHOT_SUFFIX", {});
    return text;
}
inline shotscale::Choice& choice()
{
    static shotscale::Choice c;
    static bool read = false;
    if (! read)
    {
        read = true;
        c = shotscale::choose (argumentScale().toStdString(), juce::SystemStats::getEnvironmentVariable ("LYNX_SHOT_SCALE", {}).toStdString(),
                               juce::SystemStats::getEnvironmentVariable ("LYNX_PARITYSHOT_SCALE", {}).toStdString());
    }
    return c;
}
} // namespace detail

// Takes "--shot-scale <v>" and "--shot-suffix <text>" out of the command line (so the routes' positional arguments stay
// where they were) and keeps them. Call once, before any picture is drawn.
inline juce::StringArray takeArguments (juce::StringArray args)
{
    for (const auto* flag : { "--shot-scale", "--shot-suffix" })
        if (const auto i = args.indexOf (flag); i >= 0)
        {
            const auto value = i + 1 < args.size() ? args[i + 1].unquoted() : juce::String();
            (juce::String (flag) == "--shot-scale" ? detail::argumentScale() : detail::argumentSuffix()) = value;
            args.removeRange (i, i + 1 < args.size() ? 2 : 1);
        }
    return args;
}

// pixels per logical pixel of the pictures (1 unless asked)
inline float scale()
{
    return detail::choice().scale;
}
// whether a scale was asked for at all (even "1")
inline bool given()
{
    return detail::choice().given;
}

// the file a picture route writes: `path` with the --shot-suffix before its extension (unchanged without one)
inline juce::String fileName (const juce::String& path)
{
    const auto& suffix = detail::argumentSuffix();
    return suffix.isEmpty() ? path : juce::String::fromUTF8 (shotscale::withSuffix (path.toStdString(), suffix.toStdString()).c_str());
}
inline juce::File file (const juce::File& f)
{
    return detail::argumentSuffix().isEmpty() ? f : juce::File (fileName (f.getFullPathName()));
}

// `area` of `c` (default: all of it) drawn at the shot scale — createComponentSnapshot as the routes called it
inline juce::Image snapshot (juce::Component& c, std::optional<juce::Rectangle<int>> area = {})
{
    const auto r = area.value_or (c.getLocalBounds());
    return scale() == 1.0f ? c.createComponentSnapshot (r) : c.createComponentSnapshot (r, true, scale());
}

// The routes' other way: a transparent ARGB picture of the component's size, optionally filled with `fill`, then
// paintEntireComponent (g, false) into it — the same steps, with the scale as a transform when one was asked.
inline juce::Image paint (juce::Component& c, std::optional<juce::Colour> fill = {}, int width = -1, int height = -1, bool ignoreAlpha = false)
{
    const auto w = width >= 0 ? width : c.getWidth(), h = height >= 0 ? height : c.getHeight();
    const auto s = scale();
    juce::Image image (juce::Image::ARGB, shotscale::pixels (w, s), shotscale::pixels (h, s), true);
    {
        juce::Graphics g (image);
        if (fill.has_value())
            g.fillAll (*fill);
        if (s != 1.0f)
            g.addTransform (juce::AffineTransform::scale (s));
        c.paintEntireComponent (g, ignoreAlpha);
    }
    return image;
}

// A picture the route composes itself (sheets, strips): an image of `width` × `height` logical pixels at the scale.
// Draw into it with a Graphics passed through logical() first, in a scope that ends before the image is saved (the
// drawing of a Graphics may only be flushed when it goes away).
inline juce::Image canvas (int width, int height)
{
    return juce::Image (juce::Image::ARGB, shotscale::pixels (width, scale()), shotscale::pixels (height, scale()), true);
}
inline juce::Graphics& logical (juce::Graphics& g)
{
    if (scale() != 1.0f)
        g.addTransform (juce::AffineTransform::scale (scale()));
    return g;
}
} // namespace lynx::shot
