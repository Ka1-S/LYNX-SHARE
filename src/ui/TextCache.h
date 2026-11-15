#pragma once

// Text drawn again and again with the same words, font and box (mixer strip names, dB readouts, clip names): JUCE 8
// shapes the text (HarfBuzz) on every drawText / drawFittedText, which costs ~0.05-0.1 ms a call on Windows. A busy
// Mixer or Playlist draws hundreds of them per frame (REA round 151: a full Mixer repaint took 12-30 ms). These
// helpers keep the laid-out glyphs, keyed by the text, the font and the box size, and only move them into place.
// Message thread only (the UI's paint calls). Same results as Graphics::drawText / drawFittedText.

#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <unordered_map>

#include "ui/PaintAB.h"

namespace lynx::textcache
{
namespace detail
{
inline std::unordered_map<juce::String, juce::GlyphArrangement>& cache()
{
    static std::unordered_map<juce::String, juce::GlyphArrangement> map;
    return map;
}

inline const juce::GlyphArrangement& get (const juce::String& key, const std::function<void (juce::GlyphArrangement&)>& make)
{
    auto& map = cache();
    if (const auto it = map.find (key); it != map.end())
        return it->second;
    if (map.size() > 6000) // a project switch or a long session: start again rather than grow
        map.clear();
    juce::GlyphArrangement glyphs;
    make (glyphs);
    return map.emplace (key, std::move (glyphs)).first->second;
}

inline juce::String fontKey (const juce::Font& f)
{
    return f.getTypefaceName() + "|" + f.getTypefaceStyle() + "|" + juce::String (f.getHeight(), 3) + "|" + juce::String (f.getHorizontalScale(), 3)
           + (f.isUnderlined() ? "u" : "");
}

// Round 190gr: the cache found by a 64-bit hash of the call instead of a key string built at every drawText (decimal
// floats, the font's names: allocations and locale-aware number formatting on each call). The call is held the way the
// key string held it (box sizes to 2 decimals for drawText, the minimum scale and the font's height and scale to 3), so
// the same calls share an entry as before and draw the same glyphs; an entry is used only when all of it matches.
// LYNX_PAINT_CACHE=0: the key strings, as before.
struct Call
{
    char kind; // 't' drawText, 'f' drawFittedText
    std::int64_t width, height, minScale; // (drawText: hundredths; drawFittedText: whole pixels; minScale: thousandths)
    int flags, lines;
    bool ellipsis;
};

inline std::int64_t hundredths (float v) noexcept { return std::llround ((double) v * 100.0); }
inline std::int64_t thousandths (float v) noexcept { return std::llround ((double) v * 1000.0); }

struct FontId
{
    juce::String name, style;
    std::int64_t height, scale;
    bool underlined;
    static FontId of (const juce::Font& f)
    {
        return { f.getTypefaceName(), f.getTypefaceStyle(), thousandths (f.getHeight()), thousandths (f.getHorizontalScale()), f.isUnderlined() };
    }
};

struct Entry
{
    Call call;
    juce::Font font; // (kept: equal fonts are found by a pointer compare)
    FontId fontId;
    juce::String text;
    juce::GlyphArrangement glyphs;
};

inline std::uint64_t mixHash (std::uint64_t h, std::uint64_t v) noexcept { return (h ^ v) * 1099511628211ull; }

inline bool sameCall (const Call& a, const Call& b) noexcept
{
    return a.kind == b.kind && a.width == b.width && a.height == b.height && a.minScale == b.minScale && a.flags == b.flags && a.lines == b.lines
           && a.ellipsis == b.ellipsis;
}

inline std::unordered_multimap<std::uint64_t, Entry>& hashed()
{
    static std::unordered_multimap<std::uint64_t, Entry> map;
    return map;
}

inline const juce::GlyphArrangement& getHashed (const Call& call, const juce::Font& font, const juce::String& text,
                                                const std::function<void (juce::GlyphArrangement&)>& make)
{
    const auto height = thousandths (font.getHeight()), scale = thousandths (font.getHorizontalScale());
    auto h = mixHash (mixHash (mixHash (1469598103934665603ull, (std::uint64_t) (unsigned char) call.kind), (std::uint64_t) call.width), (std::uint64_t) call.height);
    h = mixHash (mixHash (mixHash (mixHash (h, (std::uint64_t) call.minScale), (std::uint64_t) (std::uint32_t) call.flags), (std::uint64_t) (std::uint32_t) call.lines),
                 call.ellipsis ? 1u : 0u);
    h = mixHash (mixHash (mixHash (h, (std::uint64_t) height), (std::uint64_t) scale), font.isUnderlined() ? 1u : 0u);
    h = mixHash (h, (std::uint64_t) text.hashCode64());
    auto& map = hashed();
    for (auto [it, end] = map.equal_range (h); it != end; ++it)
    {
        const auto& e = it->second;
        if (! sameCall (e.call, call) || e.text != text)
            continue;
        if (e.font == font // (the same font object, or equal options)
            || (e.fontId.height == height && e.fontId.scale == scale && e.fontId.underlined == font.isUnderlined()
                && e.fontId.style == font.getTypefaceStyle() && e.fontId.name == font.getTypefaceName()))
            return e.glyphs;
    }
    if (map.size() > 6000) // a project switch or a long session: start again rather than grow
        map.clear();
    Entry entry { call, font, FontId::of (font), text, {} };
    make (entry.glyphs);
    return map.emplace (h, std::move (entry))->second.glyphs;
}
} // namespace detail

// Graphics::drawText (text, area, justification, ellipsis) with the current font and colour.
inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    if (text.isEmpty() || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f || ! g.clipRegionIntersects (area.getSmallestIntegerContainer()))
        return;
    const auto font = g.getCurrentFont();
    const auto make = [&] (juce::GlyphArrangement& arr) {
        // what Graphics::drawText does, in a box at the origin
        arr.addCurtailedLineOfText (font, text, 0.0f, 0.0f, area.getWidth(), useEllipsesIfTooBig);
        arr.justifyGlyphs (0, arr.getNumGlyphs(), 0.0f, 0.0f, area.getWidth(), area.getHeight(), justification);
    };
    if (ui::paintCache())
    {
        detail::getHashed ({ 't', detail::hundredths (area.getWidth()), detail::hundredths (area.getHeight()), 0, justification.getFlags(), 0,
                             useEllipsesIfTooBig }, font, text, make)
            .draw (g, juce::AffineTransform::translation (area.getX(), area.getY()));
        return;
    }
    const auto key = "t" + juce::String (area.getWidth(), 2) + "x" + juce::String (area.getHeight(), 2) + "|" + juce::String (justification.getFlags())
                     + (useEllipsesIfTooBig ? "e|" : "|") + detail::fontKey (font) + "|" + text;
    const auto& glyphs = detail::get (key, make);
    glyphs.draw (g, juce::AffineTransform::translation (area.getX(), area.getY()));
}

inline void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    drawText (g, text, area.toFloat(), justification, useEllipsesIfTooBig);
}

inline void drawText (juce::Graphics& g, const juce::String& text, int x, int y, int w, int h, juce::Justification justification,
                      bool useEllipsesIfTooBig = true)
{
    drawText (g, text, juce::Rectangle<int> { x, y, w, h }, justification, useEllipsesIfTooBig);
}

// Graphics::drawFittedText (text, area, justification, maximumLines, minimumHorizontalScale).
inline void drawFittedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Justification justification,
                            int maximumLines, float minimumHorizontalScale = 0.0f)
{
    if (text.isEmpty() || area.getWidth() <= 0 || area.getHeight() <= 0 || ! g.clipRegionIntersects (area))
        return;
    const auto font = g.getCurrentFont();
    const auto make = [&] (juce::GlyphArrangement& arr) {
        arr.addFittedText (font, text, 0.0f, 0.0f, (float) area.getWidth(), (float) area.getHeight(), justification, maximumLines,
                           minimumHorizontalScale);
    };
    if (ui::paintCache())
    {
        detail::getHashed ({ 'f', area.getWidth(), area.getHeight(), detail::thousandths (minimumHorizontalScale), justification.getFlags(), maximumLines,
                             false }, font, text, make)
            .draw (g, juce::AffineTransform::translation ((float) area.getX(), (float) area.getY()));
        return;
    }
    const auto key = "f" + juce::String (area.getWidth()) + "x" + juce::String (area.getHeight()) + "|" + juce::String (justification.getFlags()) + "|"
                     + juce::String (maximumLines) + "|" + juce::String (minimumHorizontalScale, 3) + "|" + detail::fontKey (font) + "|" + text;
    const auto& glyphs = detail::get (key, make);
    glyphs.draw (g, juce::AffineTransform::translation ((float) area.getX(), (float) area.getY()));
}

inline void drawFittedText (juce::Graphics& g, const juce::String& text, int x, int y, int w, int h, juce::Justification justification,
                            int maximumLines, float minimumHorizontalScale = 0.0f)
{
    drawFittedText (g, text, juce::Rectangle<int> { x, y, w, h }, justification, maximumLines, minimumHorizontalScale);
}
} // namespace lynx::textcache
