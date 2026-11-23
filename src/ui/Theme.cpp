#include "ui/Theme.h"
#include "ui/ModRing.h"
#include "ui/LiveLook.h"
#include "ui/TextCache.h"

#include "core/MenuMnemonic.h"

#if LYNX_BUNDLED_FONT
 #include <LynxFontData.h>
#endif

#if (JUCE_LINUX || JUCE_BSD) && __has_include(<fontconfig/fontconfig.h>)
 #include <fontconfig/fontconfig.h>
 #define LYNX_FONTCONFIG 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "core/FontMetrics.h"
#include "core/MenuPlacement.h"
#include "core/Song.h"
#include "core/MiddleEllipsis.h"
#include "core/TextSize.h"
#include "core/UiFontChoice.h"
#include "platform/Paths.h"
#include "ui/CondensedFontData.h"
#include "ui/DialogFrame.h"
#include "ui/I18n.h"
#include "ui/LabelFont.h"
#include "ui/Icons.h"
#include "ui/ImageCaches.h"
#include "ui/PaintAB.h"
#include "ui/PluginSkin.h"
#include "ui/WindowOwner.h"

// cmake/LynxPatchJuce.cmake fix 40 (round 190je): where a tall menu breaks into a second column (set by the LookAndFeel)
namespace juce
{
extern int (*lynxTallMenuBreakHook) (const int* heights, const int* kinds, int count, int screenHeight);
}

namespace lynx::theme
{
using namespace colour;

// --- the font cache (round 190gr, U1) -----------------------------------------------------------------
namespace
{
struct FontCache
{
    juce::SpinLock lock;
    std::unordered_map<std::uint64_t, juce::Font> fonts;
};

FontCache& fontCache()
{
    // (never destroyed: its fonts hold typefaces JUCE frees itself at shutdown; a plugin empties it through the image caches)
    static auto* cache = [] {
        auto* c = new FontCache();
        lynx::ui::registerImageCache ([c] {
            const juce::SpinLock::ScopedLockType lock (c->lock);
            c->fonts.clear();
        });
        return c;
    }();
    return *cache;
}

juce::Typeface::Ptr condensedFace (bool bold); // (below)

juce::FontOptions fontOptions (float size, int kind)
{
    if (kind == 2)
        return juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain);
    if (kind >= 3)
        if (auto face = condensedFace (kind == 4))
            return juce::FontOptions (face).withHeight (size);
    return juce::FontOptions (size, kind == 1 || kind == 4 ? juce::Font::bold : juce::Font::plain);
}

// kind 0 plain, 1 bold, 2 monospaced, 3 condensed, 4 condensed bold
juce::Font cachedFont (float size, int kind)
{
    if (! lynx::ui::paintCache())
        return juce::Font (fontOptions (size, kind));
    std::uint32_t bits = 0;
    std::memcpy (&bits, &size, sizeof (bits));
    const auto key = ((std::uint64_t) bits << 3) | (std::uint64_t) kind;
    auto& cache = fontCache();
    const juce::SpinLock::ScopedLockType lock (cache.lock);
    if (const auto it = cache.fonts.find (key); it != cache.fonts.end())
        return it->second;
    if (cache.fonts.size() >= 512) // (the sizes a session uses stay far below this)
        cache.fonts.clear();
    return cache.fonts.emplace (key, juce::Font (fontOptions (size, kind))).first->second;
}
} // namespace

namespace
{
int boldTextInUse = 0; // textsize::Bold (round 190jp)
} // namespace

juce::Font font (float size, bool bold)
{
    // Bold text (round 190jp): the roles the user chose take the bold face (the role from the size, as the roles are now)
    if (! bold && boldTextInUse != 0)
        bold = textsize::boldFor (boldTextInUse, textsize::roleOf (size, { type::caption, type::small, type::body, type::title, type::display }));
    return cachedFont (size, bold ? 1 : 0);
}

juce::Font monoFont (float size)
{
    return cachedFont (size, 2);
}

// --- Settings › General › "Condensed labels (FL style)" (round 190je, FM-5 / FM-S8) -----------------------------------
namespace
{
bool condensedOn = false;
constexpr float condensedScale = 1.15f; // a step larger: Open Sans Condensed is about 80 % as wide, so the text keeps its width
juce::File condensedFile() { return platform::settingsFile ("ui-condensed-labels.txt"); }
} // namespace

juce::Font labelFont (float size, bool bold)
{
    if (! condensedOn)
        return font (size, bold);
    return cachedFont (size * condensedScale, bold ? 4 : 3);
}

void setCondensedLabels (bool on)
{
    if (on != condensedOn)
    {
        condensedOn = on;
        clearFontCache();
    }
}

bool condensedLabels() { return condensedOn; }
bool savedCondensedLabels() { return condensedFile().loadFileAsString().trim() == "1"; }

void saveCondensedLabels (bool on)
{
    if (on)
        condensedFile().replaceWithText ("1");
    else
        condensedFile().deleteFile();
}

void clearFontCache()
{
    auto& cache = fontCache();
    const juce::SpinLock::ScopedLockType lock (cache.lock);
    cache.fonts.clear();
}

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (font (size, bold));
}

// A text that drawFittedText would cut (ellipsis) or squeeze below its minimum scale shows whole in the tooltip
// (round 165o: translated labels and buttons). Only sets a tooltip the component doesn't have one of its own.
void tooltipIfCut (juce::Component& c, juce::SettableTooltipClient& tip, const juce::Font& f, const juce::String& text,
                   juce::Rectangle<int> area, int lines, float minScale)
{
    static const juce::Identifier autoTip ("lynxAutoTooltip");
    auto& props = c.getProperties();
    const auto own = tip.getTooltip();
    if (own.isNotEmpty() && own != props[autoTip].toString())
        return;
    bool cut = false;
    if (text.isNotEmpty() && area.getWidth() > 4 && juce::GlyphArrangement::getStringWidth (f, text) > (float) area.getWidth())
    {
        juce::GlyphArrangement fitted, full;
        fitted.addFittedText (f, text, 0.0f, 0.0f, (float) area.getWidth(), (float) juce::jmax (1, area.getHeight()),
                              juce::Justification::centredLeft, juce::jmax (1, lines), minScale);
        full.addLineOfText (f, text, 0.0f, 0.0f);
        auto visible = [] (juce::GlyphArrangement& ga) {
            int n = 0;
            for (int i = 0; i < ga.getNumGlyphs(); ++i)
                n += ga.getGlyph (i).isWhitespace() ? 0 : 1;
            return n;
        };
        cut = visible (fitted) < visible (full);
    }
    const auto wanted = cut ? text : juce::String();
    if (wanted != own)
    {
        tip.setTooltip (wanted);
        props.set (autoTip, wanted);
    }
}

void fillRounded (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c)
{
    fillRoundRect (g, r, corner, c);
}

void outlineRounded (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour c, float thickness = 1.0f)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (thickness * 0.5f), corner, thickness);
}

// Window caption buttons (only used for non-native title bars).
class CaptionButton final : public juce::Button
{
public:
    CaptionButton (const juce::String& name, Icon i, bool isClose) : juce::Button (name), icon (i), close (isClose) {}

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down)
            g.fillAll (close ? (down ? accentPressed : accent) : (down ? bg6 : bg5));

        icons::draw (g, icon, getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 16.0f),
                     highlighted || down ? textPrimary : textSecondary);
    }

private:
    Icon icon;
    bool close;
};
} // namespace

namespace
{
// Software rendering (the Linux build draws every frame on the CPU; round 161): filling a path rasterises it into an
// edge table each time, which made the rounded plates, step buttons and knobs most of a Mixer or Channel rack frame.
// There, small shapes are drawn once into a sprite (a rounded rectangle in its colour, a knob) and blitted afterwards; the result is the same pixels, at most 1/16 pixel apart. Windows (Direct2D) and macOS
// (Core Graphics) keep drawing the paths directly.
#if JUCE_LINUX || JUCE_BSD
constexpr bool useSprites = true;
#else
constexpr bool useSprites = false;
#endif

// The context's physical pixel scale when it is a whole number (1 or 2 on HiDPI); 0 = draw directly
int spriteScale (juce::Graphics& g)
{
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto whole = std::round (s);
    return whole >= 1.0f && whole <= 3.0f && std::abs (s - whole) < 1.0e-4f ? (int) whole : 0;
}

struct SpriteCache
{
    std::unordered_map<std::uint64_t, juce::Image> images;

    juce::Image* find (std::uint64_t key)
    {
        const auto it = images.find (key);
        return it != images.end() ? &it->second : nullptr;
    }

    juce::Image& add (std::uint64_t key, juce::Image image)
    {
        if (images.size() >= 3000) // (sizes, values and colours seen in a session stay far below this)
            images.clear();
        return images[key] = std::move (image);
    }
};

std::uint64_t mix (std::uint64_t h, std::uint64_t v) { return (h ^ v) * 0x100000001b3ull; }
int eighth (float v) { return juce::roundToInt (v * 8.0f); }

// Where a sprite goes in physical pixels: its whole-pixel origin and the fractional rest (in eighths of a pixel)
struct SpritePlace
{
    int x, y, fx, fy;
    SpritePlace (juce::Rectangle<float> r, int scale)
    {
        const auto px = r.getX() * (float) scale, py = r.getY() * (float) scale;
        x = (int) std::floor (px);
        y = (int) std::floor (py);
        fx = eighth (px - (float) x);
        fy = eighth (py - (float) y);
        if (fx == 8) // (0.9375 and up: the next pixel)
            ++x, fx = 0;
        if (fy == 8)
            ++y, fy = 0;
    }
};

// (round 190gr: JUCE draws an image with the current colour's alpha as its opacity; a tile already holds its colour's
// alpha, so it is drawn with an opaque fill and `after` is the colour set afterwards — the one the shape's own drawing
// leaves. Before, a translucent shape drawn from a tile got its alpha twice: fainter, its big plates' corners fainter
// than their middle.)
void blit (juce::Graphics& g, const juce::Image& sprite, const SpritePlace& at, int scale, bool mask, juce::Colour after)
{
    g.setColour (juce::Colours::black);
    if (scale == 1)
        g.drawImageAt (sprite, at.x, at.y, mask);
    else
        g.drawImage (sprite, juce::Rectangle<float> ((float) at.x / (float) scale, (float) at.y / (float) scale, (float) sprite.getWidth() / (float) scale,
                                                     (float) sprite.getHeight() / (float) scale), juce::RectanglePlacement::stretchToFit, mask);
    g.setColour (after);
}

void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arcColour, bool hover);

// Round 190gr (U2): a small shape drawn from a cached tile. `draw` paints the shape into a tile at (pad, pad) with the
// size of `bounds` (quantised to eighths of a physical pixel, as the tile's key holds it); the tile is blitted at
// bounds' place, its sub-pixel offset (eighths) part of the key, so the shape keeps its anti-aliasing. One colour, one
// filled primitive: the pixels of drawing it straight within one level per channel (--selftest paint_caches).
// `native`: the tile is an image of the context's own kind (Windows: a Direct2D bitmap, which Direct2D blits on the GPU;
// a software image there would be uploaded at every blit). Returns false when the context's scale is not a whole
// number (or the shape is too big): the caller then draws it directly.
template <typename Draw>
bool drawTile (juce::Graphics& g, SpriteCache& cache, juce::Rectangle<float> bounds, float pad, std::uint64_t shapeKey, bool native, juce::Colour after, Draw&& draw)
{
    const auto scale = spriteScale (g);
    if (scale == 0 || bounds.getWidth() * (float) scale > 256.0f || bounds.getHeight() * (float) scale > 256.0f || bounds.isEmpty())
        return false;
    const auto s = (float) scale;
    const auto outer = bounds.expanded (pad);
    const SpritePlace at (outer, scale);
    const auto qw = eighth (bounds.getWidth() * s), qh = eighth (bounds.getHeight() * s), qp = eighth (pad * s);
    auto key = mix (mix (mix (shapeKey, (std::uint64_t) qw), (std::uint64_t) qh), (std::uint64_t) qp);
    key = mix (key, (std::uint64_t) ((at.fx * 16 + at.fy) * 8 + scale));
    auto* sprite = cache.find (key);
    if (sprite == nullptr)
    {
        const auto w = (float) at.fx * 0.125f + (float) (qw + 2 * qp) * 0.125f, h = (float) at.fy * 0.125f + (float) (qh + 2 * qp) * 0.125f;
        juce::Image image (juce::Image::ARGB, (int) std::ceil (w) + 1, (int) std::ceil (h) + 1, true,
                           native ? (const juce::ImageType&) juce::NativeImageType() : (const juce::ImageType&) juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale (s).translated ((float) at.fx * 0.125f, (float) at.fy * 0.125f));
            const auto p = (float) qp * 0.125f / s;
            draw (ig, juce::Rectangle<float> (p, p, (float) qw * 0.125f / s, (float) qh * 0.125f / s));
        }
        sprite = &cache.add (key, std::move (image));
    }
    blit (g, *sprite, at, scale, false, after);
    return true;
}

// Which shapes come from tiles. Linux (software renderer): round 161's rounded rectangles and knobs, and since round
// 190gr the launch triangles (within one level of drawing them straight; ellipses and outlines were tried and left out:
// a curve's tile made at another place came out up to 14 levels apart, see paintCacheSelfTest). Windows (Direct2D): the
// shapes drawn as paths — a knob's arcs, a triangle — for which Direct2D makes a new path geometry at every call (it
// draws rounded rectangles and ellipses as GPU primitives). LYNX_PAINT_CACHE=0: no tiles at all, every shape drawn
// straight; LYNX_PAINT_TILES=0: only round 161's tiles (Linux).
bool tilesForPrimitives() noexcept // (round 161's rounded rectangles: LYNX_PAINT_CACHE=0 only)
{
    return useSprites && lynx::ui::paintCache();
}

bool tilesForPaths() noexcept
{
   #if JUCE_WINDOWS
    return lynx::ui::paintTiles(); // (new on Windows: LYNX_PAINT_TILES=0 draws them straight)
   #else
    return useSprites && lynx::ui::paintCache(); // (round 161's knobs; the triangles follow LYNX_PAINT_TILES below)
   #endif
}

constexpr bool nativeTiles =
   #if JUCE_WINDOWS
    true;
   #else
    false;
   #endif
std::uint64_t paletteStamp = 0; // (setPalette: +1; part of the knob tiles' keys)

// the palette of the tokens in use (setPalette), for the state rules that read several tokens at once (round 190je)
themes::Palette& paletteInUse()
{
    static themes::Palette palette = themes::defaultPalette();
    return palette;
}
// (the largest channel difference a tile may show against its shape drawn straight, --selftest; see paintCacheSelfTest)
constexpr int nativeTolerance = 1, nativeKnobTolerance = 3;
} // namespace

ClipLook clipBody (juce::Colour clipColour, juce::Colour trackColour, ClipKind kind, ClipStyle style)
{
    const auto flGrey = colour::defaultTrack; // FL's default channel / track colour (issue #238: Settings › Theme › Tracks)
    if (clipColour.getAlpha() == 0)
        clipColour = flGrey;
    if (trackColour.getAlpha() == 0)
        trackColour = style == ClipStyle::flTint ? flGrey : clipColour;
    const auto add = [] (juce::Colour c, int d) {
        const auto f = [d] (juce::uint8 v) { return (juce::uint8) juce::jlimit (0, 255, (int) v + d); };
        return juce::Colour (f (c.getRed()), f (c.getGreen()), f (c.getBlue()));
    };
    ClipLook look;
    switch (style)
    {
        case ClipStyle::flTint:
        {
            look.headerTop = add (clipColour, -13);
            look.headerBottom = add (clipColour, -28);
            look.edge = liveShade (clipColour, 0.66f).withAlpha (1.0f);
            look.body = trackColour.withAlpha (1.0f);
            look.bodyAlpha = 0.5f;
            look.content = add (clipColour, kind == ClipKind::automation ? 84 : 72);
            look.contentFill = kind == ClipKind::automation ? look.content.withAlpha (0.13f) : juce::Colours::transparentBlack;
            look.compactContent = clipColour.withAlpha (1.0f);
            look.ink = add (clipColour, 124);
            // a light theme (issue #95: the lanes near white): the body then is pale, so the content takes a dark shade
            if (colour::bg2.getPerceivedBrightness() > 0.6f)
            {
                look.content = liveShade (clipColour, 0.45f).withAlpha (1.0f);
                look.contentFill = kind == ClipKind::automation ? look.content.withAlpha (0.13f) : juce::Colours::transparentBlack;
            }
            break;
        }
        case ClipStyle::livePastel: // Live 12.4 (round 190ia CL-5, CL-17, CL-18): one block in the colour, dark marks
            look.headerTop = look.headerBottom = look.body = clipColour.withAlpha (1.0f);
            look.edge = liveShade (clipColour, 0.66f).withAlpha (1.0f);
            // (round 190oj: Live's dark marks only where they read on the colour; on a dark colour the light ones)
            look.content = look.compactContent = liveMarkOn (clipColour, 0.15f, 3.0f, clipColour.withAlpha (1.0f).brighter (0.9f));
            look.contentFill = juce::Colours::transparentBlack;
            look.ink = liveMarkOn (clipColour, 0.24f, 4.5f, colour::textPrimary);
            break;
        case ClipStyle::bandlabHue: // BandLab web Studio (round 190ia SG-5: header ×0.45, body ×0.57, waveform the hue)
            look.headerTop = look.headerBottom = liveShade (trackColour, 0.45f).withAlpha (1.0f);
            look.body = liveShade (trackColour, 0.57f).withAlpha (1.0f);
            look.edge = liveShade (trackColour, 0.35f).withAlpha (1.0f);
            look.content = look.compactContent = trackColour.withAlpha (1.0f);
            look.contentFill = juce::Colours::transparentBlack;
            look.ink = juce::Colours::white.withAlpha (0.92f);
            break;
    }
    return look;
}

juce::Colour liveMarkOn (juce::Colour fill, float k, float ratio, juce::Colour light)
{
    // a direct-mapped cache (256 entries, message thread): paint asks once per clip / slot, from a handful of colours
    struct Entry
    {
        juce::uint32 fill = 0, light = 0, chosen = 0;
        std::int32_t k = -1, ratio = -1;
    };
    static std::array<Entry, 256> cache;
    const auto kKey = (std::int32_t) (k * 1000.0f + 0.5f), ratioKey = (std::int32_t) (ratio * 100.0f + 0.5f);
    const auto argb = fill.getARGB(), lightArgb = light.getARGB();
    auto& e = cache[(argb ^ (argb >> 11) ^ (lightArgb >> 3) ^ (juce::uint32) kKey ^ ((juce::uint32) ratioKey << 4)) & 255u];
    if (e.fill == argb && e.light == lightArgb && e.k == kKey && e.ratio == ratioKey)
        return juce::Colour (e.chosen);
    const auto opaque = fill.withAlpha (1.0f);
    const auto dark = liveShade (opaque, k);
    const auto chosen = contrast (dark, opaque) >= ratio ? dark : light;
    e = { argb, lightArgb, chosen.getARGB(), kKey, ratioKey };
    return chosen;
}

float contrast (juce::Colour a, juce::Colour b)
{
    const auto lum = [] (juce::Colour c) {
        const auto lin = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
        return 0.2126f * lin (c.getFloatRed()) + 0.7152f * lin (c.getFloatGreen()) + 0.0722f * lin (c.getFloatBlue());
    };
    const auto la = lum (a), lb = lum (b);
    return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
}

juce::Colour colourFill (juce::Colour c, float lo, float hi)
{
    return c.withBrightness (juce::jlimit (lo, hi, c.getBrightness()));
}

juce::Colour lightTint (juce::Colour c, juce::Colour background, juce::Colour fallback)
{
    const auto tint = c.withMultipliedSaturation (0.45f).withBrightness (0.95f);
    return contrast (tint, background) >= 4.5f ? tint : fallback;
}

juce::String fitMiddle (const juce::String& text, const juce::Font& font, float width)
{
    // (message thread only, as every paint; a project switch or a long session starts the cache again)
    // Round 190mh (perf): found by a hash of the call instead of a key string built at every call (a float formatted and
    // five strings joined per clip / channel name and frame); the same fields as the key string held (the font's height
    // to 2 decimals, its names, the width in quarter pixels), so the same calls share an entry and give the same text.
    struct Entry
    {
        juce::String text, name, style;
        std::int64_t height;
        int quarters;
        juce::String fitted;
    };
    static std::unordered_multimap<std::uint64_t, Entry> cache;
    const auto height = std::llround ((double) font.getHeight() * 100.0);
    const auto quarters = juce::roundToInt (width * 4.0f);
    const auto name = font.getTypefaceName(), style = font.getTypefaceStyle();
    const auto h = mix (mix (mix (mix (mix (0x9b05688c2b3e6c1full, (std::uint64_t) text.hashCode64()), (std::uint64_t) height), (std::uint64_t) (std::uint32_t) quarters),
                             (std::uint64_t) name.hashCode64()),
                        (std::uint64_t) style.hashCode64());
    for (auto [it, end] = cache.equal_range (h); it != end; ++it)
        if (it->second.quarters == quarters && it->second.height == height && it->second.text == text && it->second.name == name && it->second.style == style)
            return it->second.fitted;
    if (cache.size() > 4000)
        cache.clear();
    const auto n = text.length();
    const auto widthOf = [&] (int first, int count) { return juce::GlyphArrangement::getStringWidth (font, text.substring (first, first + count)); };
    const auto [head, tail] = middleellipsis::split (n, width, juce::GlyphArrangement::getStringWidth (font, ".."), widthOf);
    auto fitted = tail == 0 && head == n ? text
                  : head == 0               ? text.substring (0, 1) // (nothing fits: the drawText that follows clips it)
                                            : text.substring (0, head).trimEnd() + ".." + text.substring (n - tail).trimStart();
    cache.emplace (h, Entry { text, name, style, height, quarters, fitted });
    return fitted;
}

void fillLaunchTriangle (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour colour)
{
    g.setColour (colour);
    if (a.isEmpty() || ! g.clipRegionIntersects (a.getSmallestIntegerContainer().expanded (1)))
        return;
    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const auto direct = [colour] (juce::Graphics& ig, juce::Rectangle<float> b) {
        juce::Path triangle;
        triangle.addTriangle (b.getX(), b.getY(), b.getRight(), b.getCentreY(), b.getX(), b.getBottom());
        ig.setColour (colour);
        ig.fillPath (triangle);
    };
    if (! (tilesForPaths() && lynx::ui::paintTiles()) || ! drawTile (g, cache, a, 0.0f, mix (0x3c6ef372fe94f82bull, (std::uint64_t) colour.getARGB()), nativeTiles, colour, direct))
        direct (g, a);
}

// Round 190io: FL's raised step keycap and the colour-filled plates (rack buttons, Mixer header bands), drawn from cached
// tiles where the context's scale is whole (as the rounded rectangles): one blit per keycap instead of five fills
// (paintbench: the rack's whole frame went from 16 to 35 ms with the fills).
// Rollup 0.1.30 (round 190iy): tilesForPaths(), not tilesForPrimitives(): on Windows the latter is off (Direct2D draws a
// rounded rectangle as one GPU primitive), so these drew five fills per keycap and a new path geometry per plate there
// — the rack's whole frame on Thief 10 → 22 ms (--paintbench, Windows); as the knobs and triangles, they are Direct2D
// bitmaps there now (Linux unchanged: both switches are the same there).
void drawKeycap (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, bool lit)
{
    if (r.isEmpty() || ! g.clipRegionIntersects (r))
        return;
    const auto direct = [fill, lit] (juce::Graphics& k, juce::Rectangle<float> b) {
        using namespace colour;
        const auto x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
        const auto slotY = y + std::round (h * 0.25f);
        k.setColour (fill);
        k.fillRoundedRectangle (b, 2.0f);
        // the bevel: the cap's upper part a step lighter down to the slot (FL: #767C80 over a #63696D body)
        k.setColour (fill.brighter (lit ? 0.12f : 0.18f));
        k.fillRect (juce::Rectangle<float> (x + 1.0f, y + 1.0f, w - 2.0f, slotY - y - 1.0f));
        k.setColour (lit ? fill.brighter (0.4f) : borderStrong.brighter (0.25f));
        k.fillRect (juce::Rectangle<float> (x + 1.0f, y, w - 2.0f, 1.0f));
        k.setColour (lit ? fill.darker (0.45f) : bg0);
        k.fillRect (juce::Rectangle<float> (x + 1.0f, y + h - 1.0f, w - 2.0f, 1.0f));
        k.setColour (lit ? onAccent.withAlpha (0.6f) : bg0.withAlpha (0.85f));
        k.fillRect (juce::Rectangle<float> (x + 3.0f, slotY, w - 6.0f, 1.0f));
    };
    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const auto key = mix (mix (mix (0x6a09e667f3bcc908ull, (std::uint64_t) fill.getARGB()), lit ? 1u : 0u), paletteStamp);
    if (! tilesForPaths() || ! drawTile (g, cache, r.toFloat(), 0.0f, key, nativeTiles, fill, direct))
        direct (g, r.toFloat());
}

void drawStepStrip (juce::Graphics& g, juce::Rectangle<int> firstCell, int pitch, juce::Colour a, juce::Colour b, bool ghost)
{
    const auto bounds = firstCell.withWidth (7 * pitch + firstCell.getWidth());
    if (! g.clipRegionIntersects (bounds))
        return;
    const auto w = (float) firstCell.getWidth();
    const auto cell = [ghost, w] (juce::Graphics& k, juce::Rectangle<float> c, juce::Colour fill) {
        if (! ghost)
        {
            using namespace colour; // (drawKeycap's keycap, unlit)
            const auto slotY = c.getY() + std::round (c.getHeight() * 0.25f);
            k.setColour (fill);
            k.fillRoundedRectangle (c, 2.0f);
            k.setColour (fill.brighter (0.18f));
            k.fillRect (juce::Rectangle<float> (c.getX() + 1.0f, c.getY() + 1.0f, w - 2.0f, slotY - c.getY() - 1.0f));
            k.setColour (borderStrong.brighter (0.25f));
            k.fillRect (juce::Rectangle<float> (c.getX() + 1.0f, c.getY(), w - 2.0f, 1.0f));
            k.setColour (bg0);
            k.fillRect (juce::Rectangle<float> (c.getX() + 1.0f, c.getBottom() - 1.0f, w - 2.0f, 1.0f));
            k.setColour (bg0.withAlpha (0.85f));
            k.fillRect (juce::Rectangle<float> (c.getX() + 3.0f, slotY, w - 6.0f, 1.0f));
            return;
        }
        // a step past the pattern's end: faint, outlined
        k.setColour (fill.withMultipliedAlpha (0.35f));
        k.fillRoundedRectangle (c, 2.0f);
        k.setColour (fill.brighter (0.25f).withMultipliedAlpha (0.55f));
        k.drawRoundedRectangle (c.reduced (0.5f), 2.0f, 1.0f);
    };
    const auto direct = [&cell, pitch, w, a, b] (juce::Graphics& k, juce::Rectangle<float> r) {
        for (int i = 0; i < 8; ++i)
            cell (k, juce::Rectangle<float> (r.getX() + (float) (i * pitch), r.getY(), w, r.getHeight()), i < 4 ? a : b);
    };
    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const auto key = mix (mix (mix (mix (mix (0x3c6ef372fe94f82bull, (std::uint64_t) a.getARGB()), (std::uint64_t) b.getARGB()), (std::uint64_t) pitch),
                               ghost ? 1u : 0u), paletteStamp);
    if (! tilesForPaths() || ! drawTile (g, cache, bounds.toFloat(), 0.0f, key, nativeTiles, a, direct))
        direct (g, bounds.toFloat());
}

void drawStepStripOver (juce::Graphics& g, juce::Rectangle<int> firstCell, int pitch, juce::Colour a, juce::Colour b, bool ghost, juce::Colour background)
{
    const auto bounds = firstCell.withWidth (7 * pitch + firstCell.getWidth());
    if (! g.clipRegionIntersects (bounds))
        return;
    // only where drawStepStrip blits its tile (tilesForPaths, a whole scale, a tile of at most 256 px) with JUCE's software
    // renderer into the frame: the tile's pixels blended over `background` here are the frame's (PixelARGB and PixelRGB
    // blend an opaque pixel alike), and an opaque RGB image is then copied (PixelARGB::blend (PixelRGB) = set)
    const auto scale = spriteScale (g);
    if (! tilesForPaths() || scale == 0 || ! background.isOpaque() || bounds.getWidth() * scale > 256 || bounds.getHeight() * scale > 256
        || dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) == nullptr)
    {
        drawStepStrip (g, firstCell, pitch, a, b, ghost);
        return;
    }
    static std::unordered_map<std::uint64_t, juce::Image> cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    auto key = mix (mix (mix (mix (mix (0x510e527fade682d1ull, (std::uint64_t) a.getARGB()), (std::uint64_t) b.getARGB()), (std::uint64_t) pitch),
                         ghost ? 1u : 0u), paletteStamp);
    key = mix (mix (mix (mix (key, (std::uint64_t) background.getARGB()), (std::uint64_t) firstCell.getWidth()), (std::uint64_t) firstCell.getHeight()),
               (std::uint64_t) scale);
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 200)
            cache.clear();
        juce::Image image (juce::Image::RGB, bounds.getWidth() * scale, bounds.getHeight() * scale, false, juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale));
            ig.setColour (background);
            ig.fillRect (juce::Rectangle<int> (bounds.getWidth(), bounds.getHeight()));
            drawStepStrip (ig, firstCell.withPosition (0, 0), pitch, a, b, ghost);
        }
        it = cache.emplace (key, std::move (image)).first;
    }
    g.setColour (juce::Colours::black); // (an image is drawn with the current colour's alpha as its opacity)
    if (scale == 1)
        g.drawImageAt (it->second, bounds.getX(), bounds.getY());
    else
        g.drawImage (it->second, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
    g.setColour (a); // (the colour drawStepStrip leaves set)
}

void fillPlate (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour top, juce::Colour bottom, bool squareBottom)
{
    if (r.isEmpty() || ! g.clipRegionIntersects (r.getSmallestIntegerContainer()))
        return;
    const auto direct = [corner, top, bottom, squareBottom] (juce::Graphics& k, juce::Rectangle<float> b) {
        if (top == bottom)
            k.setColour (top);
        else
            k.setGradientFill (juce::ColourGradient::vertical (top, b.getY(), bottom, b.getBottom()));
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), corner, corner, true, true, ! squareBottom, ! squareBottom);
        k.fillPath (p);
    };
    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const auto key = mix (mix (mix (mix (0xbb67ae8584caa73bull, (std::uint64_t) top.getARGB()), (std::uint64_t) bottom.getARGB()),
                               (std::uint64_t) eighth (corner)), squareBottom ? 1u : 0u);
    if (! tilesForPaths() || ! drawTile (g, cache, r, 0.0f, key, nativeTiles, bottom, direct))
        direct (g, r);
}

void fillRoundRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour colour)
{
    g.setColour (colour);
    if (r.isEmpty() || ! g.clipRegionIntersects (r.getSmallestIntegerContainer()))
        return;
    if (! tilesForPrimitives() || corner <= 0.0f)
    {
        g.fillRoundedRectangle (r, corner);
        return;
    }

    // the clip doesn't reach a corner (a meter or a playhead repainted over a big plate): the visible part is a rectangle
    const auto c = juce::jmin (corner, r.getWidth() * 0.5f, r.getHeight() * 0.5f) + 1.0f;
    const auto corners = { r.withSize (c, c), r.withLeft (r.getRight() - c).withHeight (c), r.withTop (r.getBottom() - c).withWidth (c),
                           r.withLeft (r.getRight() - c).withTop (r.getBottom() - c) };
    if (std::none_of (corners.begin(), corners.end(), [&g] (juce::Rectangle<float> k) { return g.clipRegionIntersects (k.getSmallestIntegerContainer()); }))
    {
        g.fillRect (r);
        return;
    }

    const auto scale = spriteScale (g);
    if (scale == 0)
    {
        g.fillRoundedRectangle (r, corner);
        return;
    }

    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const auto s = (float) scale;
    const auto rad = juce::jmin (corner, r.getWidth() * 0.5f, r.getHeight() * 0.5f) * s; // (the path clamps it the same way)
    const auto xl = r.getX() * s, xr = r.getRight() * s, yt = r.getY() * s, yb = r.getBottom() * s;
    const auto k = (int) std::ceil (rad) + 2; // a corner's square, in physical pixels
    const auto x0 = (int) std::floor (xl), y0 = (int) std::floor (yt), xa = x0 + k, ya = y0 + k;
    const auto xb = (int) std::ceil (xr) - k, yc = (int) std::ceil (yb) - k;

    // small: the whole shape as one mask
    if (xb < xa || yc < ya || (xr - xl) * (yb - yt) <= 48.0f * 48.0f)
    {
        if ((xr - xl) > 1024.0f || (yb - yt) > 1024.0f)
        {
            g.fillRoundedRectangle (r, corner);
            return;
        }
        const SpritePlace at (r, scale);
        auto key = mix (mix (mix (0xcbf29ce484222325ull, (std::uint64_t) eighth (xr - xl)), (std::uint64_t) eighth (yb - yt)), (std::uint64_t) eighth (corner * s));
        key = mix (mix (mix (key, (std::uint64_t) (at.fx * 16 + at.fy)), (std::uint64_t) scale), (std::uint64_t) colour.getARGB());
        auto* sprite = cache.find (key);
        if (sprite == nullptr)
        {
            const auto sw = (float) eighth (xr - xl) * 0.125f, sh = (float) eighth (yb - yt) * 0.125f; // (the size the key holds)
            const auto w = (float) at.fx * 0.125f + sw, h = (float) at.fy * 0.125f + sh;
            juce::Image mask (juce::Image::ARGB, (int) std::ceil (w) + 1, (int) std::ceil (h) + 1, true, juce::SoftwareImageType());
            {
                juce::Graphics mg (mask);
                mg.setColour (colour);
                mg.fillRoundedRectangle ({ (float) at.fx * 0.125f, (float) at.fy * 0.125f, sw, sh }, (float) eighth (corner * s) * 0.125f);
            }
            sprite = &cache.add (key, std::move (mask));
        }
        blit (g, *sprite, at, scale, false, colour);
        return;
    }

    // large (a strip's plate): nine pieces — the four corners from small masks, the rest plain rectangles whose inner
    // edges fall on whole pixels, so nothing overlaps or leaves a seam
    // pa / pb: where the corner's vertical / horizontal edge lies in its square, in eighths of a pixel
    auto cornerMask = [&] (int which, int pa, int pb) -> const juce::Image& {
        const auto key = mix (mix (mix (mix (0x9e3779b97f4a7c15ull, (std::uint64_t) (which * 1000000 + pa * 1000 + pb)), (std::uint64_t) eighth (rad)), (std::uint64_t) k),
                              (std::uint64_t) colour.getARGB());
        if (auto* found = cache.find (key))
            return *found;
        juce::Image mask (juce::Image::ARGB, k, k, true, juce::SoftwareImageType());
        {
            // a big rounded rectangle whose corner `which` (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right) lies in
            // this square, its two edges at the same sub-pixel places as the shape's own
            const auto big = (float) (4 * k);
            const auto ex = (float) pa * 0.125f - ((which & 1) == 0 ? 0.0f : big);
            const auto ey = (float) pb * 0.125f - ((which & 2) == 0 ? 0.0f : big);
            juce::Graphics mg (mask);
            mg.setColour (colour);
            mg.fillRoundedRectangle ({ ex, ey, big, big }, rad);
        }
        return cache.add (key, std::move (mask));
    };
    const auto put = [&] (const juce::Image& mask, int x, int y) {
        if (scale == 1)
            g.drawImageAt (mask, x, y);
        else
            g.drawImage (mask, juce::Rectangle<float> ((float) x / s, (float) y / s, (float) k / s, (float) k / s), juce::RectanglePlacement::stretchToFit);
    };
    const auto rect = [&] (float x1, float y1, float x2, float y2) {
        if (x2 > x1 && y2 > y1)
            g.fillRect (juce::Rectangle<float> (x1 / s, y1 / s, (x2 - x1) / s, (y2 - y1) / s));
    };
    const auto pl = eighth (xl - (float) x0), pr = eighth (xr - (float) xb), pt = eighth (yt - (float) y0), pbot = eighth (yb - (float) yc);
    g.setColour (juce::Colours::black); // (the corners with an opaque fill: each mask holds the colour's alpha already; see blit)
    put (cornerMask (0, pl, pt), x0, y0);
    put (cornerMask (1, pr, pt), xb, y0);
    put (cornerMask (2, pl, pbot), x0, yc);
    put (cornerMask (3, pr, pbot), xb, yc);
    g.setColour (colour);
    rect ((float) xa, yt, (float) xb, (float) ya);  // top band
    rect ((float) xa, (float) yc, (float) xb, yb);  // bottom band
    rect (xl, (float) ya, xr, (float) yc);          // middle, full width
}

void drawFrozenTag (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tag, juce::Colour flake)
{
    fillRoundRect (g, r, 3.0f, tag);
    const auto centre = r.getCentre();
    const auto arm = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f - 2.0f;
    juce::Path p;
    for (int i = 0; i < 6; ++i)
    {
        const auto angle = juce::MathConstants<float>::pi / 3.0f * (float) i;
        const auto dx = std::sin (angle), dy = -std::cos (angle);
        p.startNewSubPath (centre);
        p.lineTo (centre.x + dx * arm, centre.y + dy * arm);
        // the two short barbs near each tip
        const auto at = juce::Point<float> (centre.x + dx * arm * 0.6f, centre.y + dy * arm * 0.6f);
        for (const auto side : { -1.0f, 1.0f })
        {
            const auto barb = angle + side * juce::MathConstants<float>::pi / 4.0f;
            p.startNewSubPath (at);
            p.lineTo (at.x + std::sin (barb) * arm * 0.38f, at.y - std::cos (barb) * arm * 0.38f);
        }
    }
    g.setColour (flake);
    g.strokePath (p, juce::PathStrokeType (1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
}

juce::Colour drawDigitDisplay (juce::Graphics& g, juce::Rectangle<float> box, bool hot, bool dimmed, bool marks)
{
    const auto c = lynx::themes::digitDisplayColours (colour::bg0, colour::border, colour::accent, colour::accentHover,
                                                      colour::textPrimary, colour::textDisabled, hot, dimmed);
    fillRoundRect (g, box, radius::sm, c.fill);
    // the inset: a shade under the top edge, a hairline outline (red while touched)
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (box.reduced (radius::sm, 0.0f).withHeight (1.0f).translated (0.0f, 1.0f));
    g.setColour (c.edge);
    g.drawRoundedRectangle (box.reduced (0.5f), radius::sm, 1.0f);
    if (marks) // FL's up / down marks: the value turns with the wheel or a vertical drag
    {
        const auto m = box.removeFromRight (digitMarksWidth).reduced (2.0f, 0.0f).withSizeKeepingCentre (5.0f, 11.0f);
        juce::Path p;
        p.startNewSubPath (m.getX(), m.getY() + 2.5f);
        p.lineTo (m.getCentreX(), m.getY());
        p.lineTo (m.getRight(), m.getY() + 2.5f);
        p.startNewSubPath (m.getX(), m.getBottom() - 2.5f);
        p.lineTo (m.getCentreX(), m.getBottom());
        p.lineTo (m.getRight(), m.getBottom() - 2.5f);
        g.setColour ((hot ? c.edge : colour::textSecondary).withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    return c.text;
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arcColour, bool hover)
{
    if (! g.clipRegionIntersects (bounds.getSmallestIntegerContainer().expanded (1)))
        return;
    const auto scale = tilesForPaths() ? spriteScale (g) : 0;
    if (scale == 0 || bounds.getWidth() * (float) scale > 256.0f || bounds.getHeight() * (float) scale > 256.0f)
    {
        drawKnobDirect (g, bounds, value01, bipolar, arcColour, hover);
        return;
    }

    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); }); (void) registered;
    const SpritePlace at (bounds, scale);
    const auto value = juce::roundToInt (juce::jlimit (0.0f, 1.0f, value01) * 1440.0f); // (a sixteenth of a degree of the ring)
    auto key = mix (mix (0x84222325cbf29ce4ull, (std::uint64_t) eighth (bounds.getWidth() * (float) scale)), (std::uint64_t) eighth (bounds.getHeight() * (float) scale));
    key = mix (mix (mix (key, (std::uint64_t) value), (std::uint64_t) arcColour.getARGB()), (std::uint64_t) ((at.fx * 16 + at.fy) * 64 + scale * 4 + (bipolar ? 2 : 0) + (hover ? 1 : 0)));
    key = mix (key, paletteStamp); // (a knob is drawn in the theme's colours too: a new palette, new tiles)
    auto* sprite = cache.find (key);
    if (sprite == nullptr)
    {
        const auto w = (float) at.fx * 0.125f + bounds.getWidth() * (float) scale, h = (float) at.fy * 0.125f + bounds.getHeight() * (float) scale;
        // (Windows, round 190gr: a Direct2D bitmap; drawn straight, each of the knob's arcs is a new path geometry)
        juce::Image image (juce::Image::ARGB, (int) std::ceil (w) + 1, (int) std::ceil (h) + 1, true,
                           nativeTiles ? (const juce::ImageType&) juce::NativeImageType() : (const juce::ImageType&) juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale).translated ((float) at.fx * 0.125f, (float) at.fy * 0.125f));
            drawKnobDirect (ig, { (float) eighth (bounds.getWidth() * (float) scale) * 0.125f / (float) scale,
                                  (float) eighth (bounds.getHeight() * (float) scale) * 0.125f / (float) scale }, (float) value / 1440.0f, bipolar, arcColour, hover);
        }
        sprite = &cache.add (key, std::move (image));
    }
    blit (g, *sprite, at, scale, false, textPrimary); // (the colour drawKnobDirect leaves set: its pointer's)
}

// Round 190jn: Live's knobs (ui/LiveLook.h) from tiles, as drawKnob above. Values and defaults in sixteenths of a degree.
void drawLiveKnobTile (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, float default01, int style,
                       void (*direct) (juce::Graphics&, juce::Rectangle<float>, float, float))
{
    if (! g.clipRegionIntersects (bounds.getSmallestIntegerContainer().expanded (5)))
        return;
    const auto value = juce::roundToInt (juce::jlimit (0.0f, 1.0f, value01) * 1440.0f), home = juce::roundToInt (juce::jlimit (0.0f, 1.0f, default01) * 1440.0f);
    static SpriteCache cache;
    static const bool registered = lynx::ui::registerImageCache ([] { cache.images.clear(); });
    (void) registered;
    const auto key = mix (mix (mix (0x9e3779b97f4a7c15ull, (std::uint64_t) value), (std::uint64_t) home), (std::uint64_t) style);
    const auto drawn = tilesForPaths() && drawTile (g, cache, bounds, 5.0f, key, nativeTiles, juce::Colour (0xff181818), [&] (juce::Graphics& ig, juce::Rectangle<float> r) {
        direct (ig, r, (float) value / 1440.0f, (float) home / 1440.0f);
    });
    if (! drawn)
        direct (g, bounds, value01, default01);
}

namespace
{
void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arcColour, bool hover)
{
    const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    if (size <= 4.0f)
        return;

    const auto area = bounds.withSizeKeepingCentre (size, size);
    const auto centre = area.getCentre();
    const auto thickness = juce::jlimit (2.0f, 4.0f, size * 0.09f);
    const auto arcRadius = size / 2.0f - thickness / 2.0f;
    constexpr auto start = -pi * 0.75f;
    constexpr auto range = pi * 1.5f;
    const auto value = juce::jlimit (0.0f, 1.0f, value01);

    // track: lighter than every panel surface, so a knob at rest (a centred pan has no value arc) is
    // still visible on the dark rows
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, start + range, true);
    g.setColour (hover ? knobTrack.brighter (0.25f) : knobTrack);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    const auto from = bipolar ? start + range * 0.5f : start;
    const auto to = start + range * value;

    if (bipolar) // the centre (default) mark at the top of the ring
    {
        g.setColour (textSecondary);
        g.fillEllipse (juce::Rectangle<float> (thickness, thickness).withCentre ({ centre.x, centre.y - arcRadius }));
    }

    if (std::abs (to - from) > 0.001f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setColour (hover && arcColour == knobArc ? accent : arcColour);
        g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body: a flat cap with a hairline rim, and a short pointer near the edge
    const auto bodyRadius = juce::jmin (arcRadius - thickness * 0.5f - juce::jmax (1.5f, size * 0.07f), 22.0f);
    if (bodyRadius > 2.0f)
    {
        const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
        g.setColour (hover ? bg5 : bg4);
        g.fillEllipse (body);
        g.setColour (bg6.withAlpha (0.7f));
        g.drawEllipse (body.reduced (0.5f), 1.0f);

        const auto inner = bodyRadius * (bodyRadius > 8.0f ? 0.42f : 0.15f);
        const auto outer = bodyRadius * 0.86f;
        g.setColour (textPrimary);
        g.drawLine (centre.x + std::sin (to) * inner, centre.y - std::cos (to) * inner, centre.x + std::sin (to) * outer,
                    centre.y - std::cos (to) * outer, juce::jlimit (1.5f, 2.5f, size * 0.05f));
    }
}
} // namespace

// --selftest "paint_caches=" (round 190gr): the font cache gives one font object per size and weight, and a shape drawn
// from a tile has the pixels of the shape drawn straight: on the software renderer at most 1 level per channel (a fill
// at a fractional position; outlines are not drawn from tiles: JUCE works a stroke's outline out from the shape's own
// coordinates, so the same outline made elsewhere came out up to 11 levels apart at its corners), on the native renderer
// (Direct2D) at most nativeTolerance / nativeKnobTolerance.
juce::String paintCacheSelfTest()
{
    {
        const auto a = font (type::body), b = font (type::body), bold = font (type::body, true);
        if (lynx::ui::paintCache() && (! (a == b) || a.getTypefacePtr() != b.getTypefacePtr()))
            return "ERROR the font cache made two fonts for one size";
        if (! bold.isBold() || a.isBold() || std::abs (a.getHeight() - type::body) > 1.0e-4f)
            return "ERROR a cached font has the wrong size or weight";
        if (monoFont (type::small).getTypefaceName() != juce::Font::getDefaultMonospacedFontName())
            return "ERROR the monospaced font";
        clearFontCache();
        if (font (type::body).getTypefacePtr() == nullptr)
            return "ERROR no typeface after the cache was emptied";
    }
    struct Shape
    {
        const char* name;
        float pad;
        bool single; // one primitive in one colour
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> draw;
    };
    const auto red = juce::Colour (0xffe5383b), grey = juce::Colour (0xb0a1a6b0);
    const Shape shapes[] = {
        { "ellipse", 0.0f, true, [red] (juce::Graphics& g, juce::Rectangle<float> r) { g.setColour (red); g.fillEllipse (r); } },
        { "translucent ellipse", 0.0f, true, [grey] (juce::Graphics& g, juce::Rectangle<float> r) { g.setColour (grey); g.fillEllipse (r); } },
        { "triangle", 0.0f, true, [red] (juce::Graphics& g, juce::Rectangle<float> r) {
              juce::Path t;
              t.addTriangle (r.getX(), r.getY(), r.getRight(), r.getCentreY(), r.getX(), r.getBottom());
              g.setColour (red);
              g.fillPath (t);
          } },
        { "knob", 0.0f, false, [] (juce::Graphics& g, juce::Rectangle<float> r) { drawKnobDirect (g, r, 0.37f, false, knobArc, false); } },
    };
    const juce::Rectangle<float> places[] = { { 10.0f, 6.0f, 9.0f, 11.0f }, { 30.5f, 6.25f, 22.0f, 22.0f }, { 61.125f, 5.75f, 26.0f, 18.0f } };
    juce::String detail;
    for (const auto native : { false, true })
    {
        int worstSingle = 0, worstKnob = 0;
        for (const auto& shape : shapes)
            for (const auto& place : places)
            {
                SpriteCache cache;
                juce::Image direct (juce::Image::ARGB, 100, 36, true, native ? (const juce::ImageType&) juce::NativeImageType() : (const juce::ImageType&) juce::SoftwareImageType());
                auto tiled = direct.createCopy();
                for (auto* image : { &direct, &tiled })
                {
                    juce::Graphics g (*image);
                    g.fillAll (bg2);
                    if (image == &direct || ! drawTile (g, cache, place, shape.pad, 0x1234567ull, native, juce::Colours::white, shape.draw))
                        shape.draw (g, place);
                }
                const juce::Image::BitmapData a (direct, juce::Image::BitmapData::readOnly), b (tiled, juce::Image::BitmapData::readOnly);
                int worst = 0;
                juce::String where;
                for (int y = 0; y < a.height; ++y)
                    for (int x = 0; x < a.width; ++x)
                    {
                        const auto p = a.getPixelColour (x, y), q = b.getPixelColour (x, y);
                        const auto d = juce::jmax (std::abs ((int) p.getRed() - (int) q.getRed()), std::abs ((int) p.getGreen() - (int) q.getGreen()),
                                                   juce::jmax (std::abs ((int) p.getBlue() - (int) q.getBlue()), std::abs ((int) p.getAlpha() - (int) q.getAlpha())));
                        if (d > worst)
                            where = juce::String (x) + "," + juce::String (y) + " " + p.toDisplayString (true) + "/" + q.toDisplayString (true);
                        worst = juce::jmax (worst, d);
                    }
                if (worst > 0 && std::getenv ("LYNX_TILE_DEBUG") != nullptr)
                    std::fprintf (stderr, "tile %s %s at %.3f,%.3f: %d (%s)\n", native ? "native" : "software", shape.name, place.getX(), place.getY(), worst,
                                  where.toRawUTF8());
                (shape.single ? worstSingle : worstKnob) = juce::jmax (shape.single ? worstSingle : worstKnob, worst);
            }
        detail << (native ? ", native" : "software") << " shapes " << worstSingle << " / knob " << worstKnob;
        if (! native && (worstSingle > 1 || worstKnob > 1))
            return "ERROR a tile differs from its shape on the software renderer by more than 1 level (" + detail + ")";
        if (worstSingle > nativeTolerance || worstKnob > nativeKnobTolerance)
            return "ERROR a tile differs from its shape (" + detail + ")";
    }
    // round 161's rounded rectangles (theme::fillRoundRect: a small one from one tile, a big one from four corner tiles
    // and plain rectangles), translucent: the alpha once (round 190gr), as fillRoundedRectangle draws it
    {
        int worst = 0;
        for (const auto& r : { juce::Rectangle<float> (4.0f, 3.0f, 20.0f, 14.0f), juce::Rectangle<float> (30.5f, 2.0f, 140.0f, 90.0f) })
        {
            juce::Image direct (juce::Image::ARGB, 180, 100, true, juce::SoftwareImageType());
            auto tiled = direct.createCopy();
            const auto colour = juce::Colour (0x8ce5383b);
            {
                juce::Graphics g (direct);
                g.fillAll (bg2);
                g.setColour (colour);
                g.fillRoundedRectangle (r, radius::md);
            }
            {
                juce::Graphics g (tiled);
                g.fillAll (bg2);
                fillRoundRect (g, r, radius::md, colour);
            }
            const juce::Image::BitmapData a (direct, juce::Image::BitmapData::readOnly), b (tiled, juce::Image::BitmapData::readOnly);
            int here = 0;
            juce::String where;
            for (int y = 0; y < a.height; ++y)
                for (int x = 0; x < a.width; ++x)
                {
                    const auto p = a.getPixelColour (x, y), q = b.getPixelColour (x, y);
                    const auto d = juce::jmax (std::abs ((int) p.getRed() - (int) q.getRed()), std::abs ((int) p.getGreen() - (int) q.getGreen()),
                                               std::abs ((int) p.getBlue() - (int) q.getBlue()));
                    if (d > here)
                        where = juce::String (x) + "," + juce::String (y) + " " + p.toDisplayString (false) + "/" + q.toDisplayString (false);
                    here = juce::jmax (here, d);
                }
            if (here > 0 && std::getenv ("LYNX_TILE_DEBUG") != nullptr)
                std::fprintf (stderr, "plate %.1fx%.1f: %d (%s)\n", r.getWidth(), r.getHeight(), here, where.toRawUTF8());
            worst = juce::jmax (worst, here);
            const auto mid = r.getCentre().toInt();
            if (a.getPixelColour (mid.x, mid.y) != b.getPixelColour (mid.x, mid.y))
                return "ERROR a translucent rounded rectangle from tiles is not the colour fillRoundedRectangle gives it (" + detail + ")";
        }
        // (their corners: up to 8 levels apart since round 161 — the corner curve flattened at the tile's place, not the
        // plate's; reported, not a failure. The alpha must be applied once: the plates' middles are the same.)
        detail << ", translucent plates " << worst << " at a corner";
    }
    // round 190ms (JUCE fix 45): the software renderer builds a path's edge table over the clip cut to the path's
    // bounds + 1 px (not the whole clip), and copies an RGB image's rows into ARGB pixels four bytes at a time
    if (const auto renderer = softwareRendererSelfTest(); renderer != "OK")
        return renderer;
    detail << ", edge tables over the path's rows and RGB rows exact";
    return "OK (" + detail + ")";
}

// Round 190ms: the two claims JUCE fix 45 rests on (cmake/LynxPatchJuce.cmake), checked with JUCE's own classes:
// (a) an edge table built over the clip's rows from one above the path's bounds to one below (every column of the clip)
//     covers every pixel as the one built over the whole clip does (shapes at sub-pixel places, strokes, ellipses, curves
//     of many nearly flat segments, scaled; clips that cut through them or not). Not its columns: EdgeTable extrapolates
//     a nearly flat segment from its rounded start row, so an edge point can land pixels beyond the path's bounds, and
//     it is clamped to the table's columns (round 190ms: a 2-D cut changed 3 pixels of Helix+ 2's filter curve by 1 level).
// (b) an opaque RGB image drawn into an ARGB one gives every pixel 0xff000000 | its RGB (rows of any width and offset).
juce::String softwareRendererSelfTest()
{
    struct Coverage // EdgeTable::iterate's callback: the coverage of each pixel in a W x H map
    {
        std::vector<int>& map;
        int width, height, y = 0;
        void setEdgeTableYPos (int row) noexcept { y = row; }
        void put (int x, int alpha) noexcept
        {
            if (x >= 0 && x < width && y >= 0 && y < height)
                map[(std::size_t) (y * width + x)] += alpha + 1; // (+1: a pixel set at alpha 0 still counts as visited)
        }
        void handleEdgeTablePixel (int x, int alpha) noexcept { put (x, alpha); }
        void handleEdgeTablePixelFull (int x) noexcept { put (x, 255); }
        void handleEdgeTableLine (int x, int w, int alpha) noexcept { for (int i = 0; i < w; ++i) put (x + i, alpha); }
        void handleEdgeTableLineFull (int x, int w) noexcept { for (int i = 0; i < w; ++i) put (x + i, 255); }
        void handleEdgeTableRectangle (int x, int y0, int w, int h, int alpha) noexcept
        {
            for (int row = y0; row < y0 + h; ++row) { y = row; handleEdgeTableLine (x, w, alpha); }
        }
        void handleEdgeTableRectangleFull (int x, int y0, int w, int h) noexcept { handleEdgeTableRectangle (x, y0, w, h, 255); }
    };
    constexpr int W = 160, H = 120;
    juce::Random random (190);
    int cases = 0;
    for (int i = 0; i < 1200; ++i)
    {
        const auto x = random.nextFloat() * 150.0f - 10.0f, y = random.nextFloat() * 110.0f - 10.0f;
        const auto w = 1.0f + random.nextFloat() * 60.0f, h = 1.0f + random.nextFloat() * 40.0f;
        juce::Path path;
        // a curve as many short, nearly flat segments (a response or envelope curve): its edge points can land well
        // outside its own bounds (EdgeTable extrapolates a flat segment's slope from a rounded start row)
        const auto curve = [&] {
            juce::Path c;
            for (int k = 0; k <= 120; ++k)
            {
                const auto px = x + w * (float) k / 120.0f, py = y + h * 0.5f * (1.0f + std::sin ((float) k * 0.05f + w)) * (k < 60 ? 0.02f : 1.0f);
                k == 0 ? c.startNewSubPath (px, py) : c.lineTo (px, py);
            }
            return c;
        };
        switch (i % 6)
        {
            case 0: path.addRoundedRectangle (x, y, w, h, random.nextFloat() * 6.0f); break;
            case 1: juce::PathStrokeType (0.5f + random.nextFloat() * 2.0f).createStrokedPath (path, [&] { juce::Path p; p.addRoundedRectangle (x, y, w, h, 2.0f); return p; }()); break;
            case 2: path.addEllipse (x, y, w, h); break;
            case 3: juce::PathStrokeType (0.75f + random.nextFloat() * 1.5f).createStrokedPath (path, curve()); break;
            case 4: path = curve(); path.lineTo (x + w, y + h); path.lineTo (x, y + h); path.closeSubPath(); break;
            default: path.addTriangle (x, y, x + w, y + h * 0.5f, x, y + h); break;
        }
        const auto transform = i % 5 == 4 ? juce::AffineTransform::scale (1.5f) : i % 7 == 6 ? juce::AffineTransform::scale (2.0f).translated (-3.0f, 1.0f)
                                                                                           : juce::AffineTransform();
        // the clip: the whole picture, or a part that cuts through the shape
        const auto clip = i % 3 == 0 ? juce::Rectangle<int> (W, H)
                                     : juce::Rectangle<int> (random.nextInt (W / 2), random.nextInt (H / 2), 10 + random.nextInt (W / 2), 10 + random.nextInt (H / 2));
        const auto pathArea = path.getBoundsTransformed (transform).getSmallestIntegerContainer();
        if (! pathArea.intersects (clip))
            continue;
        // (fix 45: the clip's rows from one above the path's to one below; every column of the clip)
        const auto rows = juce::Rectangle<int>::leftTopRightBottom (clip.getX(), juce::jmax (clip.getY(), pathArea.getY() - 1), clip.getRight(),
                                                                   juce::jmin (clip.getBottom(), pathArea.getBottom() + 1));
        std::vector<int> whole ((std::size_t) (W * H), 0), cut ((std::size_t) (W * H), 0);
        Coverage a { whole, W, H }, b { cut, W, H };
        juce::EdgeTable (clip, path, transform).iterate (a);
        juce::EdgeTable (rows, path, transform).iterate (b);
        if (whole != cut)
            return "ERROR a path's edge table over its own rows covers other pixels than over the whole clip (case " + juce::String (i) + ")";
        ++cases;
    }
    // (b) RGB rows into ARGB pixels, the image placed at odd offsets and clipped
    juce::Image rgb (juce::Image::RGB, 37, 9, false, juce::SoftwareImageType());
    {
        juce::Image::BitmapData data (rgb, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x)
                data.setPixelColour (x, y, juce::Colour ((juce::uint8) random.nextInt (256), (juce::uint8) random.nextInt (256), (juce::uint8) random.nextInt (256)));
    }
    const std::tuple<int, int, int> placesAndClips[] { { 0, 0, 64 }, { 3, 2, 64 }, { -5, 1, 64 }, { 7, -2, 20 }, { 30, 4, 64 } };
    for (const auto& [dx, dy, clipWidth] : placesAndClips)
    {
        juce::Image argb (juce::Image::ARGB, 64, 16, true, juce::SoftwareImageType());
        {
            juce::Graphics g (argb);
            g.reduceClipRegion (0, 0, clipWidth, 16);
            g.setColour (juce::Colours::black);
            g.drawImageAt (rgb, dx, dy);
        }
        const juce::Image::BitmapData from (rgb, juce::Image::BitmapData::readOnly), to (argb, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const auto sx = x - dx, sy = y - dy;
                const auto inside = x < clipWidth && sx >= 0 && sy >= 0 && sx < rgb.getWidth() && sy < rgb.getHeight();
                const auto want = inside ? from.getPixelColour (sx, sy).withAlpha ((juce::uint8) 255) : juce::Colour();
                if (to.getPixelColour (x, y) != want)
                    return "ERROR an RGB image drawn into an ARGB one: pixel " + juce::String (x) + ", " + juce::String (y);
            }
    }
    return cases > 300 ? "OK" : "ERROR too few edge table cases (" + juce::String (cases) + ")";
}

std::uint64_t paletteStampNow() noexcept
{
    return paletteStamp;
}

void setPalette (const themes::Palette& p)
{
    ++paletteStamp;
    paletteInUse() = p;
    auto c = [] (std::uint32_t argb) { return juce::Colour (argb); };
    bg0 = c (p.bg0); bg1 = c (p.bg1); bg2 = c (p.bg2); bg3 = c (p.bg3); bg4 = c (p.bg4); bg5 = c (p.bg5); bg6 = c (p.bg6);
    border = c (p.border); borderStrong = c (p.borderStrong);
    textPrimary = c (p.textPrimary); textSecondary = c (p.textSecondary); textDisabled = c (p.textDisabled);
    accent = c (p.accent); accentHover = c (p.accentHover); accentPressed = c (p.accentPressed); accentMuted = c (p.accentMuted);
    onAccent = c (p.onAccent); selectionFill = c (p.selectionFill);
    colour::selection = accent; colour::playhead = accent; colour::stepOn = accent;
    knobArc = c (p.knobArc); knobTrack = c (p.knobTrack); colour::stepOff = c (p.stepOff); stepOffAlt = c (p.stepOffAlt); stepNote = c (p.stepNote);
    gridBar = c (p.gridBar); gridBeat = c (p.gridBeat); gridStep = c (p.gridStep); keyBlack = c (p.keyBlack);
    rowWhite = c (p.rowWhite); rowBlack = c (p.rowBlack);
    noteSoft = c (p.noteSoft); noteHard = c (p.noteHard); noteMuted = c (p.noteMuted); ledOff = c (p.ledOff);
    colour::outline = c (p.outline);

    // the legacy names
    theme::window = bg1; theme::panel = bg2; theme::toolbar = bg3; theme::channelButton = bg4; theme::channelButtonSelected = bg6;
    theme::knobBackground = bg0; theme::stepOn = accent; theme::stepOnOffGrid = stepNote; theme::accentOrange = accent;
    theme::playhead = accent; theme::text = textPrimary;
}

ButtonColours buttonColours (themes::ButtonKind kind, bool on, float hover, bool down, bool enabled)
{
    // (round 190mh, perf: the state rule mixes colours and checks contrast at every button paint; its answer is kept per
    // palette and state, per thread: some self-tests and renders paint off the message thread)
    std::uint32_t hoverBits = 0;
    static_assert (sizeof (hoverBits) == sizeof (hover));
    std::memcpy (&hoverBits, &hover, sizeof (hover));
    const auto key = mix (mix (mix (0x2545f4914f6cdd1dull, paletteStamp), (std::uint64_t) hoverBits),
                          (std::uint64_t) ((int) kind * 8 + (on ? 4 : 0) + (down ? 2 : 0) + (enabled ? 1 : 0)));
    struct Entry
    {
        std::uint64_t stamp;
        std::uint32_t hover;
        int state;
        ButtonColours colours;
    };
    static thread_local std::unordered_map<std::uint64_t, Entry> cache;
    const auto state = (int) kind * 8 + (on ? 4 : 0) + (down ? 2 : 0) + (enabled ? 1 : 0);
    if (const auto it = cache.find (key); it != cache.end() && it->second.stamp == paletteStamp && it->second.hover == hoverBits && it->second.state == state)
        return it->second.colours;
    const auto look = themes::buttonLook (paletteInUse(), kind, on, (double) hover, down, enabled);
    const ButtonColours colours { juce::Colour (look.fill), juce::Colour (look.edge), juce::Colour (look.content) };
    if (cache.size() > 1024) // (fades step through hover levels)
        cache.clear();
    cache[key] = { paletteStamp, hoverBits, state, colours };
    return colours;
}

juce::String mnemonic (const juce::String& text, char key)
{
    return juce::String::fromUTF8 (menumnemonic::apply (text.toStdString(), key).c_str());
}

int labelColumnWidth (std::initializer_list<const juce::Label*> labels, int minWidth, int maxWidth)
{
    float widest = 0.0f;
    for (const auto* l : labels)
        if (l != nullptr && l->getText().isNotEmpty())
        {
            const auto font = const_cast<juce::Label*> (l)->getLookAndFeel().getLabelFont (*const_cast<juce::Label*> (l));
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, l->getText()) + (float) l->getBorderSize().getLeftAndRight() + 2.0f);
        }
    return juce::jlimit (minWidth, juce::jmax (minWidth, maxWidth), (int) std::ceil (widest));
}

int buttonWidth (const juce::TextButton& button, int minWidth, int maxWidth)
{
    auto& b = const_cast<juce::TextButton&> (button);
    const auto font = b.getLookAndFeel().getTextButtonFont (b, juce::jmax (20, b.getHeight()));
    const auto needed = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, b.getButtonText()) + 18.0f);
    return juce::jlimit (minWidth, juce::jmax (minWidth, maxWidth), needed);
}

int translationExtra (const juce::Font& font, std::initializer_list<const char*> english)
{
    float extra = 0.0f;
    for (const auto* text : english)
    {
        const auto source = juce::String::fromUTF8 (text);
        const auto shown = tr (text);
        if (shown != source)
            extra = juce::jmax (extra, juce::GlyphArrangement::getStringWidth (font, shown) - juce::GlyphArrangement::getStringWidth (font, source));
    }
    return juce::jmax (0, (int) std::ceil (extra));
}

themes::Palette currentPalette()
{
    auto c = [] (juce::Colour x) { return x.getARGB(); };
    return { c (bg0), c (bg1), c (bg2), c (bg3), c (bg4), c (bg5), c (bg6), c (border), c (borderStrong), c (textPrimary), c (textSecondary),
             c (textDisabled), c (accent), c (accentHover), c (accentPressed), c (accentMuted), c (onAccent), c (selectionFill), c (knobArc),
             c (knobTrack), c (colour::stepOff), c (stepOffAlt), c (stepNote), c (gridBar), c (gridBeat), c (gridStep), c (keyBlack), c (rowWhite),
             c (rowBlack), c (noteSoft), c (noteHard), c (noteMuted), c (ledOff), c (colour::outline) };
}

namespace
{
// Linux (issue #142, round 190d): JUCE 8.0.4 makes its own list of the installed fonts from the <dir> entries of
// /etc/fonts/fonts.conf alone; it does not follow that file's <include>s. NixOS names one folder there (DejaVu Sans
// only) and adds every font package, each in its own /nix/store folder, through an included file: JUCE then knew no
// monospaced family at all, the monospaced fonts LYNX asks for had no typeface, and drawing a text with one crashed
// at start-up (cmake/LynxPatchJuce.cmake, fix 19). The folders now come from fontconfig itself, through JUCE's own
// JUCE_FONT_PATH variable (one the user set is left alone). Must run before JUCE's first look at the fonts.
[[maybe_unused]] void useFontconfigFontFolders()
{
   #if LYNX_FONTCONFIG
    if (const auto* own = std::getenv ("JUCE_FONT_PATH"); own != nullptr && *own != 0)
        return;
    juce::StringArray folders;
    if (auto* config = FcInitLoadConfig()) // (the configuration files only: no font is opened here)
    {
        if (auto* dirs = FcConfigGetFontDirs (config))
        {
            while (const auto* dir = FcStrListNext (dirs))
                folders.add (juce::String::fromUTF8 (reinterpret_cast<const char*> (dir)));
            FcStrListDone (dirs);
        }
        FcConfigDestroy (config);
    }
    folders = fontFoldersToScan (folders, [] (const juce::String& folder) { return juce::File (folder).isDirectory(); });
    if (! folders.isEmpty())
        ::setenv ("JUCE_FONT_PATH", folders.joinIntoString (";").toRawUTF8(), 1);
   #endif
}

#if LYNX_BUNDLED_FONT
// Open Sans, regular and bold (resources/fonts, SIL OFL). (The name finds a memory face only while it lives.)
// Issue #189 (round 190ir): with its own vertical metrics (ascent + descent 1.362 em) Open Sans drew its letters 15 %
// smaller than DejaVu Sans did at the same JUCE height, the font Linux showed up to 0.1.23, and narrower: every text
// looked much smaller after 0.1.25 at the same UI size. Its ascent + descent are set to DejaVu Sans's in memory
// (core/FontMetrics.h), so the letters are as large as 0.1.23's again; Open Sans's own widths (8 % narrower than
// DejaVu's at that size) leave the layouts more room than 0.1.23 had. The font file itself is unchanged (OFL: the
// glyphs and names are the same; only the line metrics JUCE sizes by are read differently).
juce::Typeface::Ptr bundledFace (const unsigned char* data, int size)
{
    // LYNX_FONT_METRICS=0 (diagnostics, A/B): Open Sans with its own metrics, as 0.1.25-0.1.29 drew it
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_FONT_METRICS", {}) == "0")
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    const auto scaled = fontmetrics::withVerticalExtent (data, (std::size_t) size, fontmetrics::dejaVuSansExtent);
    if (! scaled.empty())
        if (auto face = juce::Typeface::createSystemTypefaceFor (scaled.data(), scaled.size()))
            return face; // (JUCE keeps its own copy of the data)
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

const std::array<juce::Typeface::Ptr, 2>& bundledFaces()
{
    static const std::array<juce::Typeface::Ptr, 2> faces {
        bundledFace ((const unsigned char*) lynxfontdata::OpenSansRegular_ttf, lynxfontdata::OpenSansRegular_ttfSize),
        bundledFace ((const unsigned char*) lynxfontdata::OpenSansBold_ttf, lynxfontdata::OpenSansBold_ttfSize)
    };
    return faces;
}
#endif

bool typefaceFallbackOff = false; // (fontSelfTest only)
} // namespace

namespace
{
// Open Sans Condensed (round 190je), decoded from ui/CondensedFontData.h on first use: only when the option is on, so the
// default look never meets these faces. On Linux they get Open Sans's adjusted line metrics (issue #189), as the
// regular face does, so both draw their letters at the same scale.
juce::Typeface::Ptr condensedFace (bool bold)
{
    static const std::array<juce::Typeface::Ptr, 2> faces = [] {
        const auto decode = [] (const char* const* pieces, int count) {
            juce::String text;
            for (int i = 0; i < count; ++i)
                text += pieces[i];
            juce::MemoryOutputStream out;
            juce::Base64::convertFromBase64 (out, text);
            return out.getMemoryBlock();
        };
        const auto make = [] (const juce::MemoryBlock& data) -> juce::Typeface::Ptr {
            if (data.getSize() == 0)
                return {};
           #if LYNX_BUNDLED_FONT
            return bundledFace ((const unsigned char*) data.getData(), (int) data.getSize());
           #else
            return juce::Typeface::createSystemTypefaceFor (data.getData(), data.getSize());
           #endif
        };
        return std::array<juce::Typeface::Ptr, 2> { make (decode (condensedfont::semiBold, condensedfont::semiBoldPieces)),
                                                    make (decode (condensedfont::bold, condensedfont::boldPieces)) };
    }();
    return faces[bold ? 1 : 0] != nullptr ? faces[bold ? 1 : 0] : faces[0];
}
} // namespace

juce::StringArray fontFoldersToScan (const juce::StringArray& fontconfigFolders, const std::function<bool (const juce::String&)>& exists)
{
    // JUCE splits its list at ';' and ',' and scans each folder with everything below it: a folder inside another
    // one of the list would be read twice (every face twice in the font lists)
    juce::StringArray kept;
    for (const auto& raw : fontconfigFolders)
    {
        auto folder = raw.trim();
        while (folder.length() > 1 && folder.endsWithChar ('/'))
            folder = folder.dropLastCharacters (1);
        if (folder.isEmpty() || folder.containsAnyOf (";,") || ! juce::File::isAbsolutePath (folder) || ! exists (folder))
            continue;
        const auto inside = [] (const juce::String& child, const juce::String& parent) {
            return child == parent || parent == "/" || child.startsWith (parent + "/");
        };
        bool covered = false;
        for (int i = kept.size(); --i >= 0;)
        {
            if (inside (folder, kept[i]))
                covered = true;
            else if (inside (kept[i], folder))
                kept.remove (i);
        }
        if (! covered)
            kept.add (folder);
    }
    return kept;
}

LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { bg1, bg2, bg3, borderStrong, textPrimary, bg4, onAccent, accent, textPrimary })
{
    // Segoe UI on Windows (the JUCE default there is Verdana, wider and dated)
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #elif LYNX_BUNDLED_FONT
    // Linux: JUCE would take the first of Verdana, Bitstream Vera, Luxi, Liberation, DejaVu, 8-32 % wider than the font
    // the layouts are made with. Open Sans comes with LYNX SHARE (resources/fonts, SIL OFL), so it looks the same everywhere.
    useFontconfigFontFolders(); // (first: the memory faces below make JUCE read the font folders)
    if (const auto& faces = bundledFaces(); faces[0] != nullptr)
        setDefaultSansSerifTypefaceName (faces[0]->getName());
   #else
    useFontconfigFontFolders();
   #endif
    clearFontCache(); // (the cached fonts may hold the typeface of the family before)
    refreshColours();

    // FL's columns for long menus (round 190je, FW-13): a menu taller than 70 % of the screen breaks once at a group
    juce::lynxTallMenuBreakHook = [] (const int* heights, const int* kinds, int count, int screenHeight) {
        std::vector<themes::MenuRow> rows ((std::size_t) juce::jmax (0, count));
        for (int i = 0; i < count; ++i)
            rows[(std::size_t) i] = { heights[i], kinds[i] == 1, kinds[i] == 2 };
        return themes::tallMenuBreak (rows, screenHeight * 7 / 10);
    };
}

// JUCE expects a typeface for every font ("the look and feel must return a typeface!") but, on Linux, has none for a
// family that is not installed — nor for its own "monospaced" or "serif" placeholder on a system where it found no
// such font (issue #142: text drawn with such a font crashed LYNX at start-up). Such a font now draws with the font
// LYNX brings (Linux), else with the default sans-serif: plain, but there.
juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (auto face = juce::LookAndFeel_V4::getTypefaceForFont (font); face != nullptr || typefaceFallbackOff)
        return face;
    const auto bold = font.isBold();
   #if LYNX_BUNDLED_FONT
    if (const auto& faces = bundledFaces(); faces[bold ? 1 : 0] != nullptr)
        return faces[bold ? 1 : 0];
    else if (faces[0] != nullptr)
        return faces[0];
   #endif
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return {}; // (no font at all: nothing left to try)
    return juce::LookAndFeel_V4::getTypefaceForFont (juce::Font (juce::FontOptions (font.getHeight(), bold ? juce::Font::bold : juce::Font::plain)));
}

juce::String fontSelfTest()
{
    // every font gets a typeface and glyphs: the default, the monospaced one, a family nobody has (issue #142). The
    // text has places where a line may break: those took JUCE's crashing path
    const juce::String text ("0 : 00 : 00 one-two three/four");
    const std::pair<const char*, juce::Font> fonts[] = {
        { "default", font (type::body) },
        { "bold", font (type::body, true) },
        { "monospaced", monoFont (type::body) },
        { "serif", juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), type::body, juce::Font::plain)) },
        { "unknown family", juce::Font (juce::FontOptions ("LYNX no such family 142", type::body, juce::Font::bold)) },
    };
    for (const auto& [name, options] : fonts)
    {
        const juce::Font f (options);
        if (f.getTypefacePtr() == nullptr)
            return juce::String ("ERROR no typeface: ") + name;
        if (! (juce::GlyphArrangement::getStringWidth (f, text) > 1.0f))
            return juce::String ("ERROR no glyphs: ") + name;
    }
    // without the fallback (what Linux gave LYNX 0.1.25 for such a family: no typeface): nothing drawn, no crash
    // (cmake/LynxPatchJuce.cmake, fix 19; Windows and macOS substitute a font themselves)
   #if JUCE_LINUX || JUCE_BSD
    {
        const juce::ScopedValueSetter<bool> off (typefaceFallbackOff, true);
        const juce::Font none (juce::FontOptions ("LYNX no such family 142 b", type::body, juce::Font::plain));
        juce::GlyphArrangement line, fitted;
        line.addLineOfText (none, text, 0.0f, 0.0f);
        fitted.addFittedText (none, text, 0.0f, 0.0f, 60.0f, 40.0f, juce::Justification::centredLeft, 3, 0.7f);
        juce::AttributedString attributed;
        attributed.append (text, none, juce::Colours::white);
        juce::TextLayout layout;
        layout.createLayout (attributed, 60.0f);
        juce::Image image (juce::Image::ARGB, 80, 40, true);
        juce::Graphics g (image);
        g.setFont (none);
        g.drawText (text, 0, 0, 80, 20, juce::Justification::centredLeft, true);
        g.drawFittedText (text, 0, 20, 80, 20, juce::Justification::centredLeft, 2);
        if (! std::isfinite (juce::GlyphArrangement::getStringWidth (none, text)) || ! std::isfinite (layout.getHeight()))
            return "ERROR a font without a typeface";
    }
   #endif
    // the font folders handed to JUCE: fontconfig's, without the missing ones and the ones inside another
    // (POSIX paths: on Windows "/usr/..." is not an absolute path, and fontconfig is not used there)
   #if ! JUCE_WINDOWS
    {
        const auto all = [] (const juce::String&) { return true; };
        const auto notOpt = [] (const juce::String& folder) { return ! folder.startsWith ("/opt"); };
        const auto joined = [] (const juce::StringArray& folders) { return folders.joinIntoString (";"); };
        if (joined (fontFoldersToScan ({ "/usr/share/fonts/truetype", "/usr/share/fonts/", "/nix/store/a-dejavu/share/fonts", "/usr/share/fonts",
                                         "/usr/share/fontsx", "relative/fonts", "/a;b", "/opt/fonts", "" }, notOpt))
                != "/usr/share/fonts;/nix/store/a-dejavu/share/fonts;/usr/share/fontsx"
            || joined (fontFoldersToScan ({ "/a/b", "/", "/c" }, all)) != "/"
            || ! fontFoldersToScan ({}, all).isEmpty())
            return "ERROR font folders";
    }
   #endif
    return "OK";
}

juce::String uiFontForLanguage (const juce::String& languageId)
{
    // first installed family wins (Windows 10/11 and macOS ship these; Linux: Noto CJK / Source Han / WenQuanYi...)
    // (a std::vector: assigning a braced list to a std::initializer_list variable leaves it dangling — it crashed
    // LYNX at start-up on Windows in Japanese / Chinese)
    std::vector<const char*> families;
    if (languageId == "ja")
        families = { "Yu Gothic UI", "Meiryo UI", "Hiragino Sans", "Hiragino Kaku Gothic ProN", "Noto Sans CJK JP", "Noto Sans JP",
                     "Source Han Sans JP", "IPAexGothic", "IPAPGothic", "TakaoPGothic", "VL PGothic" };
    else if (languageId == "ko")
        families = { "Malgun Gothic", "Apple SD Gothic Neo", "Noto Sans CJK KR", "Noto Sans KR", "Source Han Sans KR", "NanumGothic",
                     "UnDotum" };
    else if (languageId == "zh-Hans")
        families = { "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Hiragino Sans GB", "Noto Sans CJK SC", "Noto Sans SC",
                     "Source Han Sans SC", "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Droid Sans Fallback" };
    else if (languageId == "zh-Hant")
        families = { "Microsoft JhengHei UI", "Microsoft JhengHei", "PingFang TC", "PingFang HK", "Noto Sans CJK TC", "Noto Sans TC",
                     "Source Han Sans TC", "WenQuanYi Micro Hei", "AR PL UMing TW", "Droid Sans Fallback" };
    else
        return {};

    static const auto installed = juce::Font::findAllTypefaceNames();
    for (const auto* family : families)
        if (installed.contains (family))
            return family;
    // Linux lists only the first face of a font collection (NotoSansCJK.ttc -> "Noto Sans CJK JP"): any of
    // them covers Chinese, Japanese and Korean
    for (const auto& family : installed)
        for (const auto* prefix : { "Noto Sans CJK", "Noto Sans Mono CJK", "Source Han Sans", "WenQuanYi", "Droid Sans Fallback" })
            if (family.startsWith (prefix))
                return family;
    return {};
}

bool needsOwnFont (const juce::String& languageId)
{
    return languageId == "ja" || languageId == "ko" || languageId == "zh-Hans" || languageId == "zh-Hant";
}

juce::String LookAndFeel::applyLanguageFont (const juce::String& languageId)
{
    const auto family = uiFontForLanguage (languageId);
    if (family.isNotEmpty())
        setDefaultSansSerifTypefaceName (family);
    clearFontCache(); // (the cached fonts hold the typeface of the family before)
    return family; // empty: JUCE's per-glyph font fallback still finds whatever CJK font the OS has
}

juce::String defaultUiFont()
{
   #if JUCE_WINDOWS
    return "Segoe UI";
   #elif LYNX_BUNDLED_FONT
    return "Open Sans";
   #else
    return {}; // macOS: what JUCE picks there, unchanged
   #endif
}

juce::StringArray installedUiFonts()
{
    auto names = juce::Font::findAllTypefaceNames();
    names.removeDuplicates (false);
    names.removeEmptyStrings();
    names.sortNatural();
    return names;
}

static juce::File uiFontFile() { return platform::settingsFile ("ui-font.txt"); }

// --- Settings › General › Text size (issue #189) --------------------------------------------------------
namespace
{
int textSizeInUse = textsize::standard;
juce::File textSizeFile() { return platform::settingsFile ("ui-text-size.txt"); }
} // namespace

void setTextSize (int percent)
{
    using textsize::Role;
    textSizeInUse = textsize::clamp (percent);
    type::caption = textsize::scaled (type::base::caption, textSizeInUse, Role::caption);
    type::small = textsize::scaled (type::base::small, textSizeInUse, Role::small);
    type::body = textsize::scaled (type::base::body, textSizeInUse, Role::body);
    type::title = textsize::scaled (type::base::title, textSizeInUse, Role::title);
    type::display = textsize::scaled (type::base::display, textSizeInUse, Role::display);
    clearFontCache();
}

int textSize() { return textSizeInUse; }

int savedTextSize() { return textsize::parse (textSizeFile().loadFileAsString().toStdString()); }

void saveTextSize (int percent)
{
    if (textsize::clamp (percent) == textsize::standard)
        textSizeFile().deleteFile();
    else
        textSizeFile().replaceWithText (juce::String (textsize::clamp (percent)));
}

// --- Settings › General › Bold text (round 190jp) ------------------------------------------------------------
namespace
{
juce::File boldTextFile() { return platform::settingsFile ("ui-bold-text.txt"); }
} // namespace

void setBoldText (int setting)
{
    boldTextInUse = juce::jlimit (0, 2, setting);
    clearFontCache();
}

int boldText() { return boldTextInUse; }

int savedBoldText() { return textsize::parseBold (boldTextFile().loadFileAsString().toStdString()); }

void saveBoldText (int setting)
{
    if (juce::jlimit (0, 2, setting) == 0)
        boldTextFile().deleteFile();
    else
        boldTextFile().replaceWithText (juce::String (juce::jlimit (0, 2, setting)));
}

juce::String savedUiFont() { return uiFontFile().loadFileAsString().trim(); }

void saveUiFont (const juce::String& family)
{
    if (family.isEmpty())
        uiFontFile().deleteFile();
    else
        uiFontFile().replaceWithText (family);
}

juce::String LookAndFeel::applyInterfaceFont (const juce::String& languageId, const juce::String& runOverride)
{
    const auto saved = runOverride.isNotEmpty() ? runOverride : savedUiFont();
    if (saved.isNotEmpty())
    {
        const auto installed = juce::Font::findAllTypefaceNames(); // (only when a font was chosen: it costs a scan)
        const auto family = chooseUiFont (saved.toStdString(), [&installed] (const std::string& name) {
            return installed.contains (juce::String::fromUTF8 (name.c_str()));
        });
        if (! family.empty())
        {
            // the user's font also in Chinese, Japanese and Korean: what it lacks comes from JUCE's per-glyph fallback
            setDefaultSansSerifTypefaceName (juce::String::fromUTF8 (family.c_str()));
            clearFontCache();
            return juce::String::fromUTF8 (family.c_str());
        }
    }
    return applyLanguageFont (languageId);
}

void LookAndFeel::refreshColours()
{
    setColourScheme (juce::LookAndFeel_V4::ColourScheme { bg1, bg2, bg3, borderStrong, textPrimary, bg4, onAccent, accent, textPrimary });
    setColour (juce::ResizableWindow::backgroundColourId, bg1);
    setColour (juce::DocumentWindow::textColourId, textPrimary);

    setColour (juce::TextButton::buttonColourId, bg4);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, textPrimary);
    setColour (juce::TextButton::textColourOnId, onAccent);
    setColour (juce::ToggleButton::textColourId, textPrimary);
    setColour (juce::ToggleButton::tickColourId, onAccent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDisabled);

    setColour (juce::ComboBox::backgroundColourId, bg4);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, textPrimary);
    setColour (juce::ComboBox::arrowColourId, textSecondary);
    setColour (juce::ComboBox::buttonColourId, bg4);
    setColour (juce::ComboBox::focusedOutlineColourId, accent);

    // (round 190je: the menu style's colours, LYNX dark or FL classic)
    const auto menuLook = menus::colours();
    setColour (juce::PopupMenu::backgroundColourId, menuLook.background);
    setColour (juce::PopupMenu::textColourId, menuLook.text);
    setColour (juce::PopupMenu::headerTextColourId, menuLook.bandText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, menuLook.hover);
    setColour (juce::PopupMenu::highlightedTextColourId, menuLook.hoverText);

    setColour (juce::Label::textColourId, textPrimary);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textWhenEditingColourId, textPrimary);
    setColour (juce::Label::backgroundWhenEditingColourId, bg0);
    setColour (juce::Label::outlineWhenEditingColourId, accent);

    setColour (juce::TextEditor::backgroundColourId, bg0);
    setColour (juce::TextEditor::textColourId, textPrimary);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, textPrimary);
    setColour (juce::TextEditor::outlineColourId, colour::outline); // (round 190je: 3:1, was border 1.1:1)
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    setColour (juce::CaretComponent::caretColourId, accent);

    setColour (juce::Slider::backgroundColourId, bg0);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::thumbColourId, textPrimary);
    setColour (juce::Slider::rotarySliderFillColourId, knobArc);
    setColour (juce::Slider::rotarySliderOutlineColourId, bg0);
    setColour (juce::Slider::textBoxTextColourId, textPrimary);
    setColour (juce::Slider::textBoxBackgroundColourId, bg0);
    setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.35f));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour (juce::ScrollBar::thumbColourId, bg6);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);

    setColour (juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TabbedComponent::backgroundColourId, bg2);
    setColour (juce::TabbedButtonBar::tabTextColourId, textSecondary);
    setColour (juce::TabbedButtonBar::frontTextColourId, textPrimary);

    setColour (juce::TooltipWindow::backgroundColourId, bg3);
    setColour (juce::TooltipWindow::textColourId, textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, borderStrong);

    setColour (juce::AlertWindow::backgroundColourId, bg3);
    setColour (juce::AlertWindow::textColourId, textPrimary);
    setColour (juce::AlertWindow::outlineColourId, borderStrong);

    setColour (juce::TreeView::backgroundColourId, bg2);
    setColour (juce::TreeView::linesColourId, border);
    setColour (juce::TreeView::selectedItemBackgroundColourId, accentMuted);
    setColour (juce::TreeView::oddItemsColourId, juce::Colours::transparentBlack);
    setColour (juce::TreeView::evenItemsColourId, juce::Colours::transparentBlack);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, accentMuted);
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, textPrimary);
    setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId, textPrimary);
    setColour (juce::ListBox::backgroundColourId, bg2);
    setColour (juce::ListBox::outlineColourId, border);
    setColour (juce::ListBox::textColourId, textPrimary);

    setColour (juce::GroupComponent::outlineColourId, border);
    setColour (juce::GroupComponent::textColourId, textSecondary);
    setColour (juce::ProgressBar::backgroundColourId, bg0);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::HyperlinkButton::textColourId, info);
    setColour (juce::BubbleComponent::backgroundColourId, bg3);
    setColour (juce::BubbleComponent::outlineColourId, borderStrong);
}

// --- buttons ---------------------------------------------------------------------------------------

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour, bool highlighted,
                                        bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();
    auto base = backgroundColour; // already buttonOnColour when toggled
    juce::Colour edge;

    // LYNX's own colours (a secondary button: bg4; on / primary: the accent) follow the one state rule (round 190je);
    // a button given colours of its own keeps its shading
    const auto standard = base == bg4 || base == accent;
    if (standard)
    {
        // (round 190oo, WG-23: a disabled primary takes the secondary disabled look, bg4 + textDisabled as its neighbours;
        // the dim red with grey text read 2.2:1. A toggled-on button keeps its on look when disabled.)
        const auto look = buttonColours (themes::ButtonKind::secondary, on || (base == accent && button.isEnabled()), highlighted ? 1.0f : 0.0f, down,
                                         button.isEnabled());
        base = look.fill;
        edge = look.edge;
    }
    else
    {
        if (down)
            base = on ? accentPressed : base.brighter (0.18f);
        else if (highlighted)
            base = on ? accentHover : base.brighter (0.1f);

        if (! button.isEnabled())
            base = base.withMultipliedAlpha (0.5f);
    }

    const auto flatLeft = button.isConnectedOnLeft(), flatRight = button.isConnectedOnRight();
    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md, ! flatLeft,
                           ! flatRight, ! flatLeft, ! flatRight);
    g.setColour (base);
    g.fillPath (p);
    if (! edge.isTransparent())
    {
        // 1 px outline (round 190je, FW idea 6): the fill alone reads 1.1:1 on the panels
        // (on the pixel centres: `bounds` is the button inset by half a pixel, so the line covers exactly its edge pixels)
        juce::Path rim;
        rim.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md, ! flatLeft, ! flatRight,
                                 ! flatLeft, ! flatRight);
        g.setColour (edge);
        g.strokePath (rim, juce::PathStrokeType (1.0f));
    }

    if ((button.hasKeyboardFocus (false) || button.getProperties().contains ("lynxSnapshotFocus")) // (--snapshot: see saveWindow)
        && button.getWantsKeyboardFocus())
    {
        g.setColour (accent.withAlpha (0.6f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (juce::jmin ((float) type::body, (float) buttonHeight * 0.6f));
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto font = getTextButtonFont (button, button.getHeight());
    g.setFont (font);
    const auto own = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    // disabled: the disabled text token (3.6:1, round 190je) for LYNX's own text colours, else the colour dimmed as before
    const auto fg = button.isEnabled() ? own : (own == textPrimary || own == onAccent) ? textDisabled : own.withMultipliedAlpha (0.45f);
    g.setColour (fg);

    auto area = button.getLocalBounds().reduced (juce::jmin (8, button.getWidth() / 6), 0);
    const auto iconId = (int) button.getProperties().getWithDefault (iconProperty, 0);

    if (iconId != 0)
    {
        const auto iconSize = juce::jmin (16.0f, (float) button.getHeight() * 0.6f);
        const auto textWidth = button.getButtonText().isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (font, button.getButtonText()) + 6.0f;
        auto content = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), iconSize + textWidth), (float) area.getHeight());
        icons::draw (g, (Icon) iconId, content.removeFromLeft (iconSize), fg);
        g.setColour (fg);
        content.removeFromLeft (6.0f);
        g.drawFittedText (button.getButtonText(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        tooltipIfCut (button, button, font, button.getButtonText(), content.toNearestInt(), 1, 0.85f);
        return;
    }

    g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, 2, 0.85f);
    tooltipIfCut (button, button, font, button.getButtonText(), area, 2, 0.85f);
}

void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component, float x, float y, float w, float h, bool ticked, bool enabled,
                               bool highlighted, bool)
{
    const auto box = juce::Rectangle<float> (x, y, w, h).reduced (0.5f);

    // one of several exclusive choices (opt-in, round 184a S17: the setup wizard's workflows): a round radio dot
    if (component.getProperties()["lynxRadio"])
    {
        const auto circle = box.withSizeKeepingCentre (juce::jmin (box.getWidth(), box.getHeight()), juce::jmin (box.getWidth(), box.getHeight()));
        g.setColour (bg0);
        g.fillEllipse (circle);
        g.setColour (ticked ? (enabled ? (highlighted ? accentHover : accent) : accentMuted)
                            : (highlighted ? textSecondary : enabled ? colour::outline : colour::outline.withMultipliedAlpha (0.5f)));
        g.drawEllipse (circle.reduced (0.5f), 1.2f);
        if (ticked)
            g.fillEllipse (circle.reduced (circle.getWidth() * 0.28f));
        return;
    }

    if (ticked)
        fillRounded (g, box, radius::sm, enabled ? (highlighted ? accentHover : accent) : accentMuted);
    else
    {
        fillRounded (g, box, radius::sm, bg0);
        // (round 190je: the outline at 3:1, was borderStrong 1.4:1)
        outlineRounded (g, box.expanded (0.5f), radius::sm, highlighted ? textSecondary : enabled ? colour::outline : colour::outline.withMultipliedAlpha (0.5f));
    }

    if (ticked)
        icons::draw (g, Icon::check, box.reduced (box.getWidth() * 0.05f), enabled ? onAccent : textDisabled);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto fontSize = juce::jmin ((float) type::body, (float) button.getHeight() * 0.75f);
    const auto tickSize = juce::jmin (16.0f, (float) button.getHeight() - 4.0f);

    drawTickBox (g, button, 2.0f, ((float) button.getHeight() - tickSize) * 0.5f, tickSize, tickSize, button.getToggleState(),
                 button.isEnabled(), highlighted, down);

    const auto own = button.findColour (juce::ToggleButton::textColourId);
    g.setColour (button.isEnabled() ? own : own == textPrimary ? textDisabled : own.withMultipliedAlpha (0.5f));
    g.setFont (uiFont (fontSize));
    const auto textArea = button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tickSize) + 8).withTrimmedRight (2);
    g.drawFittedText (button.getButtonText(), textArea, juce::Justification::centredLeft, 2, 0.85f);
    tooltipIfCut (button, button, uiFont (fontSize), button.getButtonText(), textArea, 2, 0.85f);
}

// --- combo boxes and menus -------------------------------------------------------------------------

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    const auto hover = box.isMouseOver (true) && box.isEnabled();
    const auto own = box.findColour (juce::ComboBox::backgroundColourId);
    // LYNX's own box (bg4) follows the button rule (round 190je): bg5 under the mouse, a 1 px outline at 3:1
    const auto look = buttonColours (themes::ButtonKind::secondary, false, hover ? 1.0f : 0.0f, false, box.isEnabled());
    fillRounded (g, bounds, radius::md, own == bg4 ? look.fill : hover ? bg5 : own);

    if ((box.hasKeyboardFocus (true) || box.getProperties().contains ("lynxSnapshotFocus")) && box.getWantsKeyboardFocus())
        outlineRounded (g, bounds, radius::md, accent.withAlpha (0.7f));
    else if (const auto rim = box.findColour (juce::ComboBox::outlineColourId); ! rim.isTransparent() && rim != borderStrong)
        outlineRounded (g, bounds, radius::md, rim);
    else if (own == bg4 || rim == borderStrong) // (Settings' boxes asked for borderStrong, 1.4:1: the 3:1 outline)
        outlineRounded (g, bounds.expanded (0.5f), radius::md, look.edge); // (expanded: the helper insets half a pixel, crisp)

    const auto arrowSize = juce::jmin (14.0f, (float) height * 0.6f);
    const auto arrowArea = juce::Rectangle<float> ((float) width - arrowSize - 7.0f, ((float) height - arrowSize) * 0.5f, arrowSize, arrowSize);
    icons::draw (g, Icon::chevronDown, arrowArea, (hover ? textPrimary : textSecondary).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return uiFont (juce::jmin ((float) type::body, (float) box.getHeight() * 0.62f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto arrowSize = juce::jmin (14, juce::roundToInt ((float) box.getHeight() * 0.6f));
    label.setBounds (2, 1, box.getWidth() - arrowSize - 12, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    // round 190oo (PAT-18): a box that asks for it keeps its font size and ends a long text in "..." (one line)
    static const juce::Identifier ellipsisId ("lynxComboEllipsis"), oneLineId ("lynxOneLine");
    if ((bool) box.getProperties().getWithDefault (ellipsisId, false))
    {
        label.setMinimumHorizontalScale (1.0f);
        label.getProperties().set (oneLineId, true);
    }
    const auto own = box.findColour (juce::ComboBox::textColourId);
    label.setColour (juce::Label::textColourId, box.isEnabled() ? own : own == textPrimary ? textDisabled : own.withMultipliedAlpha (0.5f));
}

void LookAndFeel::preparePopupMenuWindow (juce::Component& window)
{
    juce::LookAndFeel_V4::preparePopupMenuWindow (window);

    // Issue #18 (round 167p): a submenu opens beside its parent menu's OUTER edge, its first item level with the item
    // that opened it, growing downwards (core/MenuPlacement.h). JUCE put it beside the item, so in a two-column menu
    // (Channel Rack, Playlist, Mixer, Piano Roll) it covered the other column, and grew upwards whenever the item was
    // in the lower half of the screen. The mouse crossing that other column keeps the submenu open (the JUCE patch
    // "submenu corridor" in cmake/LynxPatchJuce.cmake).
    static std::vector<juce::Component::SafePointer<juce::Component>> menus;
    menus.erase (std::remove_if (menus.begin(), menus.end(), [] (const auto& m) { return m == nullptr || ! m->isVisible(); }), menus.end());
    const auto remember = [&window] { menus.push_back (&window); };
    if (! window.isOnDesktop() || menus.empty())
        return remember();

    // the parent: the latest menu window still open (opening another item's submenu closes the previous one first)
    juce::Component* parent = nullptr;
    for (auto it = menus.rbegin(); it != menus.rend() && parent == nullptr; ++it)
        if (auto* m = it->getComponent(); m != &window && m->isOnDesktop()
                                          && std::abs (m->getDesktopScaleFactor() - window.getDesktopScaleFactor()) < 0.001f)
            parent = m;
    if (parent == nullptr)
        return remember();

    // the item that opened it: the item under the mouse (a hover), else the one JUCE put the submenu right beside
    // (its right edge = the submenu's left, or the other way round) level with its top or bottom (the keyboard's
    // right arrow). Neither: JUCE's own place stays (kept on the root menu's display by the JUCE patch, issue #45).
    const auto border = getPopupMenuBorderSize();
    const auto sub = window.getBounds(), parentBox = parent->getBounds();
    const auto beside = [&sub] (juce::Rectangle<int> r) { return std::abs (r.getRight() - sub.getX()) <= 2 || std::abs (r.getX() - sub.getRight()) <= 2; };
    const auto mouse = juce::Desktop::getInstance().getMousePosition();
    juce::Rectangle<int> item;
    int best = std::numeric_limits<int>::max();
    for (auto* c : parent->getChildren())
    {
        const auto r = c->getBounds() + parentBox.getPosition();
        if (! c->isVisible() || r.getHeight() <= 0)
            continue;
        if (r.contains (mouse))
        {
            item = r;
            break;
        }
        const auto miss = juce::jmin (std::abs (r.getY() - (sub.getY() + border)), std::abs (r.getBottom() - (sub.getBottom() - border)));
        if (beside (r) && miss < best)
        {
            best = miss;
            item = r;
        }
    }
    if (item.isEmpty())
        return remember();

    // the screen: the display of the ROOT menu, i.e. of the window the menu came from (issue #45, core/MenuPlacement.h)
    juce::Component* root = parent;
    for (const auto& m : menus)
        if (m != nullptr && m.getComponent() != &window && m->isOnDesktop())
        {
            root = m.getComponent();
            break;
        }
    const auto scale = juce::jmax (0.01f, window.getDesktopScaleFactor() / juce::Desktop::getInstance().getGlobalScaleFactor());
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    std::vector<menuplace::Box> screens;
    for (const auto& d : displays)
    {
        // JUCE 8.0.4 on Linux leaves a display it could not place beside the others (Displays::updateToLogical) with
        // an empty totalArea and a userArea at 0,0 over the main display (seen with Xvfb + Xinerama): not a screen
        if (d.totalArea.isEmpty())
            continue;
        const auto a = (d.userArea.toFloat() / scale).toNearestInt();
        screens.push_back ({ a.getX(), a.getY(), a.getWidth(), a.getHeight() });
    }
    const auto rootBox = root->getBounds();
    const auto at = menuplace::displayFor (screens, { rootBox.getX(), rootBox.getY(), rootBox.getWidth(), rootBox.getHeight() });
    const auto area = at >= 0 ? juce::Rectangle<int> (screens[(size_t) at].x, screens[(size_t) at].y, screens[(size_t) at].w, screens[(size_t) at].h)
                              : parentBox.expanded (10000);
    using menuplace::Box;
    const auto box = [] (juce::Rectangle<int> r) { return Box { r.getX(), r.getY(), r.getWidth(), r.getHeight() }; };
    const auto placed = menuplace::placeSubmenu (box (parentBox), box (item), sub.getWidth(), sub.getHeight(), border, box (area));
    if (placed != box (sub))
        window.setBounds (placed.x, placed.y, placed.w, placed.h);
    remember();
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const auto look = menus::colours();
    g.fillAll (look.background);
    g.setColour (look.border);
    g.drawRect (bounds, 1.0f);
}

// FL's 1 px line between the columns of a long menu (round 190je, FW-13)
void LookAndFeel::drawPopupMenuColumnSeparatorWithOptions (juce::Graphics& g, const juce::Rectangle<int>& bounds, const juce::PopupMenu::Options&)
{
    g.setColour (menus::colours().separator);
    g.fillRect (bounds.withSizeKeepingCentre (1, bounds.getHeight()));
}

int LookAndFeel::getPopupMenuColumnSeparatorWidthWithOptions (const juce::PopupMenu::Options&)
{
    return 1;
}

namespace menus
{
namespace
{
bool compactMenus = true;
themes::MenuStyle menuStyle = themes::MenuStyle::lynxDark;
}
void setCompact (bool c) { compactMenus = c; }
bool compact() { return compactMenus; }
int itemHeight() { return compactMenus ? 21 : 26; }
int headerHeight() { return compactMenus ? 17 : 22; }
int separatorHeight() { return compactMenus ? 5 : 9; }
float textSize() { return textsize::scaled (14.0f, theme::textSize(), textsize::Role::body); }
void setStyle (themes::MenuStyle s) { menuStyle = s; }
themes::MenuStyle style() { return menuStyle; }

Colours colours()
{
    const auto m = themes::menuColours (paletteInUse(), menuStyle);
    const auto c = [] (std::uint32_t argb) { return juce::Colour (argb); };
    return { c (m.background), c (m.border), c (m.text), c (m.shortcut), c (m.disabled), c (m.band), c (m.bandText), c (m.hover), c (m.hoverText),
             c (m.separator), c (m.check) };
}
} // namespace menus

juce::Font LookAndFeel::getPopupMenuFont()
{
    return labelFont (menus::textSize());
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth,
                                             int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = menus::separatorHeight();
        return;
    }

    const auto font = getPopupMenuFont();
    if (standardMenuItemHeight < 0)
    {
        // JUCE asks with -1 for a section header and then adds half of the answer: hand back 2/3 of the wanted height
        idealHeight = (menus::headerHeight() * 2 + 2) / 3;
        idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (labelFont (type::small), text)) + 24;
        return;
    }
    idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : menus::itemHeight();
    const auto iconWidth = juce::roundToInt ((float) idealHeight * 0.8f);
    const auto shown = text.removeCharacters (juce::String::charToString ((juce::juce_wchar) menumnemonic::mark)); // (an accelerator mark)
    idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, shown)) + iconWidth + (menus::compact() ? 36 : idealHeight + 30);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                     bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                     const juce::Drawable* icon, const juce::Colour* textColourToUse)
{
    const auto look = menus::colours();
    if (isSeparator)
    {
        g.setColour (look.separator);
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (0, 0);
    const auto hot = isHighlighted && isActive;

    // FL's hover: the whole row, square, no accent bar (round 190je, FW-14: the accent stays for selected states)
    if (hot)
    {
        g.setColour (look.hover);
        g.fillRect (r);
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse : look.text;
    if (! isActive)
        textColour = look.disabled;
    else if (hot && textColourToUse == nullptr)
        textColour = look.hoverText;

    auto content = r.reduced (12, 0);
    auto iconArea = content.removeFromLeft (juce::roundToInt ((float) r.getHeight() * 0.8f)).toFloat();

    if (icon != nullptr)
        icon->drawWithin (g, iconArea.withSizeKeepingCentre (14.0f, 14.0f), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.4f);
    else if (isTicked)
        icons::draw (g, Icon::check, iconArea.withSizeKeepingCentre (16.0f, 16.0f), ! isActive ? look.disabled : hot ? look.hoverText : look.check);

    if (hasSubMenu)
    {
        const auto arrow = content.removeFromRight (14).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-pi * 0.5f, arrow.getCentreX(), arrow.getCentreY()));
        icons::draw (g, Icon::chevronDown, arrow, ! isActive ? look.disabled : hot ? look.hoverText : look.shortcut);
    }

    content.removeFromLeft (4);
    const auto font = getPopupMenuFont();
    g.setFont (font);

    if (shortcutKeyText.isNotEmpty())
    {
        // FL's shortcut column: right-aligned, italic, a step smaller; white under the mouse as FL's
        const auto keyFont = labelFont (type::small).italicised();
        g.setColour (! isActive ? look.disabled : hot ? look.hoverText : look.shortcut);
        g.setFont (keyFont);
        g.drawText (shortcutKeyText, content.removeFromRight (juce::roundToInt (juce::GlyphArrangement::getStringWidth (keyFont, shortcutKeyText)) + 4),
                    juce::Justification::centredRight, false);
        g.setFont (font);
    }

    // FL's accelerator letter (core/MenuMnemonic.h, round 170a): the mark is hidden, its letter underlined
    const auto markAt = text.indexOfChar ((juce::juce_wchar) menumnemonic::mark);
    const auto shown = markAt >= 0 ? text.removeCharacters (juce::String::charToString ((juce::juce_wchar) menumnemonic::mark)) : text;
    g.setColour (textColour);
    g.drawFittedText (shown, content, juce::Justification::centredLeft, 1, 0.9f);

    if (markAt >= 0 && markAt < shown.length() && isActive)
    {
        const auto full = juce::GlyphArrangement::getStringWidth (font, shown);
        const auto squeeze = full > (float) content.getWidth() && full > 0.0f ? juce::jmax (0.9f, (float) content.getWidth() / full) : 1.0f;
        const auto x = (float) content.getX() + juce::GlyphArrangement::getStringWidth (font, shown.substring (0, markAt)) * squeeze;
        const auto w = juce::GlyphArrangement::getStringWidth (font, shown.substring (markAt, markAt + 1)) * squeeze;
        const auto baseline = (float) content.getCentreY() - font.getHeight() * 0.5f + font.getAscent();
        g.fillRect (juce::Rectangle<float> (x, baseline + 1.5f, juce::jmax (1.0f, w), 1.0f));
    }
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    // FL's section band (round 190je, FW-12): the full row filled, the title in sentence case ("Recent projects")
    const auto look = menus::colours();
    const auto band = area.withTrimmedTop (juce::jmax (0, area.getHeight() - menus::headerHeight()));
    g.setColour (look.band);
    g.fillRect (band);
    g.setFont (labelFont (type::small));
    g.setColour (look.bandText);
    g.drawFittedText (sectionName, band.reduced (8, 0), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawMenuBarBackground (juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
    // transparent: the menu sits inside the main toolbar, which paints the band behind it
}

juce::Font LookAndFeel::getMenuBarFont (juce::MenuBarComponent& bar, int, const juce::String&)
{
    // round 190jo (design190ia FM-4): the Pattern toolbar's menu one step larger, FL's landmark (FL cap 12, LYNX's 13 gave
    // cap 7; 15 gives cap 8 in about 335 px at 1920); the Simple and Live menu rows keep theirs. (Rollup: both through
    // round 190je's labelFont, so Condensed labels still applies to the menu bar.)
    if ((bool) bar.getProperties().getWithDefault ("lynxFlMenuBar", false))
    {
        // round 190oo (WG-11): a long translated menu that would push toolbar panels out at 1920 px takes the condensed
        // face at the same size (MainComponent's toolbar layout sets this only when it keeps more panels)
        static const juce::Identifier condensedId ("lynxCondensedMenuBar");
        if ((bool) bar.getProperties().getWithDefault (condensedId, false))
            return cachedFont (type::title, 3);
        return labelFont (type::title);
    }
    return labelFont (type::body);
}

namespace
{
int menuBarPadding (juce::MenuBarComponent& bar)
{
    return (bool) bar.getProperties().getWithDefault ("lynxTightMenuBar", false)  ? 8
           : (bool) bar.getProperties().getWithDefault ("lynxFlMenuBar", false) ? 12
                                                                                 : 16;
}

// round 190oo (WG-11): the horizontal squeeze of the menu titles (1 = none; the toolbar layout keeps it >= 0.9)
float menuBarScale (juce::MenuBarComponent& bar)
{
    static const juce::Identifier scaleId ("lynxMenuBarScale");
    return juce::jlimit (0.5f, 1.0f, (float) (double) bar.getProperties().getWithDefault (scaleId, 1.0));
}
} // namespace

int LookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText)
{
    // a long (translated) menu bar takes tighter item padding so the toolbar keeps its panels (round 165o)
    // (round 190jo: the Pattern toolbar's larger menu text with FL's closer items, 12 px: the bar keeps its ~292 px, as FL's
    // eight menus take, so no toolbar panel drops out)
    const auto padding = menuBarPadding (bar);
    const auto text = juce::GlyphArrangement::getStringWidth (getMenuBarFont (bar, itemIndex, itemText), itemText);
    if (const auto scale = menuBarScale (bar); scale < 0.999f) // (round 190oo WG-11: squeezed titles, see drawMenuBarItem)
        return (int) std::ceil (text * scale) + padding;
    return juce::roundToInt (text) + padding;
}

void LookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int itemIndex, const juce::String& itemText, bool isMouseOverItem,
                                   bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent& bar)
{
    juce::ignoreUnused (isMouseOverBar);
    const auto active = isMenuOpen || isMouseOverItem;

    if (active)
        fillRounded (g, juce::Rectangle<float> (0.0f, 2.0f, (float) width, (float) height - 4.0f).reduced (1.0f, 0.0f), radius::sm,
                     isMenuOpen ? bg4 : bg3);

    // (round 190jo FM-4: the Pattern toolbar's menu titles in textPrimary at rest, as FL's bright caps)
    g.setColour (active || (bool) bar.getProperties().getWithDefault ("lynxFlMenuBar", false) ? textPrimary : textSecondary.brighter (0.2f));
    g.setFont (getMenuBarFont (bar, itemIndex, itemText));
    // (round 190mh, perf: the laid-out title from the text cache, the same glyphs at the item's origin; round 190jo's larger
    // Pattern-toolbar titles were shaped again on every paint)
    if (const auto scale = menuBarScale (bar); scale < 0.999f)
    {
        // round 190oo (WG-11): the title squeezed into the item less its padding, so the gaps between titles stay
        const auto padding = menuBarPadding (bar);
        textcache::drawFittedText (g, itemText, padding / 2, 0, width - padding, height, juce::Justification::centred, 1, juce::jmax (0.5f, scale - 0.05f));
        return;
    }
    textcache::drawFittedText (g, itemText, 0, 0, width, height, juce::Justification::centred, 1);
}

// --- sliders ---------------------------------------------------------------------------------------

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float, juce::Slider& slider)
{
    const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                         && std::abs (slider.getMinimum() + slider.getMaximum()) < 1e-6 * (slider.getMaximum() - slider.getMinimum());
    if (const auto* skinned = slider.getProperties().getVarPointer (skin::sliderProperty)) // a plugin page's knob (ui/PluginSkin.h)
    {
        const auto packed = (int) slider.getProperties().getWithDefault (skin::styleProperty, 0);
        const skin::StyleScope styleScope (skin::resolve (skin::unpackStyle (packed), skin::unpackArt (packed)), skin::unpackArt (packed));
        skin::KnobLook look;
        look.value = pos;
        look.bipolar = bipolar;
        look.accent = juce::Colour ((juce::uint32) (juce::int64) *skinned);
        look.hover = slider.isMouseOverOrDragging() ? 1.0f : 0.0f;
        look.active = slider.isMouseButtonDown();
        look.enabled = slider.isEnabled();
        skin::knob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), look);
        if (const auto target = modring::taggedTarget (slider)) // round 190ox: the modulation rings of a tagged knob
        {
            const auto b = juce::Rectangle<int> (x, y, width, height).toFloat();
            const auto size = juce::jmin (b.getWidth(), b.getHeight()) - 2.0f;
            modring::drawFor (g, b.withSizeKeepingCentre (size, size), pos, *target, look.accent);
        }
        return;
    }
    auto arc = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled())
        arc = textDisabled;
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    drawKnob (g, bounds, pos, bipolar, arc, slider.isMouseOverOrDragging());
    if (const auto target = modring::taggedTarget (slider)) // round 190ox: the modulation rings of a tagged knob
    {
        const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
        modring::drawFor (g, bounds.withSizeKeepingCentre (size, size), pos, *target, arc);
    }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                    float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.isBar())
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
        fillRounded (g, bounds, radius::sm, bg0);
        auto fill = slider.isHorizontal() ? bounds.withRight (sliderPos) : bounds.withTop (sliderPos);
        fillRounded (g, fill, radius::sm, slider.findColour (juce::Slider::trackColourId).withAlpha (0.7f));
        return;
    }

    if (style == juce::Slider::TwoValueHorizontal || style == juce::Slider::TwoValueVertical || style == juce::Slider::ThreeValueHorizontal
        || style == juce::Slider::ThreeValueVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto vertical = slider.isVertical();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    if (const auto* skinned = slider.getProperties().getVarPointer (skin::sliderProperty)) // a plugin page's fader
    {
        const auto packed = (int) slider.getProperties().getWithDefault (skin::styleProperty, 0);
        const skin::StyleScope styleScope (skin::resolve (skin::unpackStyle (packed), skin::unpackArt (packed)), skin::unpackArt (packed));
        const auto v = vertical ? (bounds.getBottom() - sliderPos) / juce::jmax (1.0f, bounds.getHeight()) : (sliderPos - bounds.getX()) / juce::jmax (1.0f, bounds.getWidth());
        const auto track = vertical ? bounds.withSizeKeepingCentre (4.0f, bounds.getHeight()) : bounds.withSizeKeepingCentre (bounds.getWidth(), 4.0f);
        const auto bip = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        skin::slider (g, track, v, vertical, bip, juce::Colour ((juce::uint32) (juce::int64) *skinned), slider.isMouseOverOrDragging(), slider.isEnabled(),
                      juce::jmin (vertical ? bounds.getWidth() - 2.0f : bounds.getHeight() - 2.0f, 26.0f), 12.0f);
        return;
    }
    const auto trackWidth = 4.0f;
    const auto active = slider.isMouseOverOrDragging();
    const auto trackColour = slider.isEnabled() ? slider.findColour (juce::Slider::trackColourId) : textDisabled;

    if (vertical)
    {
        const auto track = juce::Rectangle<float> (bounds.getCentreX() - trackWidth / 2, bounds.getY(), trackWidth, bounds.getHeight());
        fillRounded (g, track, trackWidth / 2, bg0);
        fillRounded (g, track.withTop (sliderPos), trackWidth / 2, trackColour.withAlpha (0.85f));

        // fader cap
        const auto capWidth = juce::jmin (bounds.getWidth() - 2.0f, 26.0f);
        const auto cap = juce::Rectangle<float> (capWidth, 12.0f).withCentre ({ bounds.getCentreX(), sliderPos });
        fillRounded (g, cap.translated (0.0f, 1.5f), radius::sm, juce::Colours::black.withAlpha (0.35f));
        fillRounded (g, cap, radius::sm, active ? juce::Colour (0xfff4f5f7) : juce::Colour (0xffd4d7dd));
        g.setColour (bg0.withAlpha (0.8f));
        g.fillRect (cap.withSizeKeepingCentre (capWidth - 8.0f, 1.5f));
    }
    else
    {
        const auto track = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - trackWidth / 2, bounds.getWidth(), trackWidth);
        fillRounded (g, track, trackWidth / 2, bg0);
        fillRounded (g, track.withRight (sliderPos), trackWidth / 2, trackColour.withAlpha (0.85f));

        const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, bounds.getCentreY() });
        g.setColour (active ? juce::Colour (0xfff4f5f7) : juce::Colour (0xffd4d7dd));
        g.fillEllipse (thumb);
    }
}

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (uiFont (type::small));
    label->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    label->setColour (juce::TextEditor::focusedOutlineColourId, accent);
    label->setColour (juce::Label::backgroundColourId,
                      slider.isBar() ? juce::Colours::transparentBlack : slider.findColour (juce::Slider::textBoxBackgroundColourId));
    return label;
}

juce::Button* LookAndFeel::createSliderButton (juce::Slider&, bool isIncrement)
{
    auto* b = new IconButton (isIncrement ? "+" : "-", isIncrement ? Icon::plus : Icon::minimise);
    return b;
}

juce::Slider::SliderLayout LookAndFeel::getSliderLayout (juce::Slider& slider)
{
    auto layout = juce::LookAndFeel_V4::getSliderLayout (slider);

    // rotary with a text box below: leave a small gap between the knob and the value
    if (slider.isRotary() && slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        layout.sliderBounds = layout.sliderBounds.withTrimmedBottom (2);

    return layout;
}

// --- scrollbars ------------------------------------------------------------------------------------

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical, int thumbStart,
                                 int thumbSize, bool isMouseOver, bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, width, thumbSize) : juce::Rectangle<int> (thumbStart, y, thumbSize, height);
    const auto thickness = isMouseOver || isMouseDown ? 6.0f : 4.0f;
    auto r = thumb.toFloat().reduced (vertical ? 0.0f : 2.0f, vertical ? 2.0f : 0.0f);
    r = vertical ? r.withSizeKeepingCentre (thickness, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), thickness);
    fillRounded (g, r, thickness / 2, isMouseDown ? textSecondary : isMouseOver ? bg6.brighter (0.25f) : bg6);
}

// --- tabs ------------------------------------------------------------------------------------------

int LookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int tabDepth)
{
    const auto hasIcon = (int) button.getProperties().getWithDefault (iconProperty, 0) != 0;
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (type::body), button.getButtonText())) + (hasIcon ? tabDepth : 0) + 24;
}

void LookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar& bar, juce::Graphics& g)
{
    g.fillAll (bg1);
    g.setColour (border);
    g.fillRect (0, bar.getHeight() - 1, bar.getWidth(), 1);
}

void LookAndFeel::drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

void LookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const auto front = button.isFrontTab();
    auto area = button.getActiveArea().toFloat();

    if (front)
        fillRounded (g, area.reduced (2.0f, 4.0f), radius::md, bg3);
    else if (isMouseOver || isMouseDown)
        fillRounded (g, area.reduced (2.0f, 4.0f), radius::md, bg2);

    if (front)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (area.removeFromBottom (2.0f).reduced (10.0f, 0.0f), 1.0f);
    }

    const auto fg = front ? textPrimary : isMouseOver ? textPrimary.withAlpha (0.85f) : textSecondary;
    const auto iconId = (int) button.getProperties().getWithDefault (iconProperty, 0);
    const auto font = uiFont (type::body, false);
    const auto textWidth = juce::GlyphArrangement::getStringWidth (font, button.getButtonText());
    const auto iconSize = 16.0f;
    auto content = button.getActiveArea().toFloat();
    content = content.withSizeKeepingCentre (juce::jmin (content.getWidth() - 8.0f, textWidth + (iconId != 0 ? iconSize + 7.0f : 0.0f)),
                                             content.getHeight());

    if (iconId != 0)
        icons::draw (g, (Icon) iconId, content.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize), front ? accent : fg);

    content.removeFromLeft (iconId != 0 ? 7.0f : 0.0f);
    g.setColour (fg);
    g.setFont (font);
    g.drawFittedText (button.getButtonText(), content.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
}

// --- text ------------------------------------------------------------------------------------------

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (dynamic_cast<juce::AlertWindow*> (editor.getParentComponent()) != nullptr)
    {
        fillRounded (g, juce::Rectangle<int> (width, height).toFloat(), radius::md, bg0);
        return;
    }

    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (width, height).toFloat(), radius::sm);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (! editor.isEnabled())
        return;

    const auto r = juce::Rectangle<int> (width, height).toFloat();

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
        outlineRounded (g, r, radius::sm, editor.findColour (juce::TextEditor::focusedOutlineColourId));
    else if (! editor.findColour (juce::TextEditor::outlineColourId).isTransparent())
        outlineRounded (g, r, radius::sm, editor.findColour (juce::TextEditor::outlineColourId));
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    const auto background = label.findColour (juce::Label::backgroundColourId);
    if (! background.isTransparent())
        fillRounded (g, label.getLocalBounds().toFloat(), radius::sm, background);

    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = getLabelFont (label);
        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);
        const auto textArea = getLabelBorderSize (label).subtractedFrom (label.getLocalBounds());
        static const juce::Identifier oneLineId ("lynxOneLine"); // (round 190oo PAT-18, positionComboBoxText)
        const auto lines = (bool) label.getProperties().getWithDefault (oneLineId, false) ? 1 : juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight()));
        g.drawFittedText (label.getText(), textArea, label.getJustificationType(), lines, label.getMinimumHorizontalScale());
        tooltipIfCut (label, label, font, label.getText(), textArea, lines, label.getMinimumHorizontalScale());
    }

    const auto outline = label.findColour (juce::Label::outlineColourId);
    if (! outline.isTransparent())
        outlineRounded (g, label.getLocalBounds().toFloat(), radius::sm, outline);
}

// --- tooltips, alerts, windows ---------------------------------------------------------------------

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    juce::TextLayout layout;
    juce::AttributedString s;
    s.append (tipText, uiFont (type::small), textPrimary);
    layout.createLayoutWithBalancedLineLengths (s, 360.0f);
    const auto w = (int) (layout.getWidth() + 18.0f), h = (int) (layout.getHeight() + 12.0f);
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);

    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, uiFont (type::small), textPrimary);
    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 360.0f);
    layout.draw (g, bounds.reduced (9.0f, 6.0f));
}

juce::Font LookAndFeel::getAlertWindowTitleFont() { return uiFont (type::title, true); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return uiFont (type::body); }
juce::Font LookAndFeel::getAlertWindowFont() { return uiFont (type::body); }

juce::AlertWindow* LookAndFeel::createAlertWindow (const juce::String& title, const juce::String& message, const juce::String& button1,
                                                   const juce::String& button2, const juce::String& button3, juce::MessageBoxIconType iconType,
                                                   int numButtons, juce::Component* associatedComponent)
{
    // round 190oo (design review 2, WG-6): JUCE's message boxes (the plugin crash prompt, the OK / Cancel questions, the
    // notices) are made on ui::dialog::Alert, the one dialog frame; the buttons, results and shortcuts are JUCE's own
    // (LookAndFeel_V2::createAlertWindow). A box without a message is a progress window (ThreadWithProgressWindow adds its
    // bar and status text through the AlertWindow base afterwards): that one stays JUCE's.
    if (message.trim().isEmpty())
        return juce::LookAndFeel_V4::createAlertWindow (title, message, button1, button2, button3, iconType, numButtons, associatedComponent);

    auto* alert = new lynx::ui::dialog::Alert (title, message, iconType, associatedComponent);
    if (numButtons == 1)
        alert->addButton (button1, 0, juce::KeyPress (juce::KeyPress::escapeKey), juce::KeyPress (juce::KeyPress::returnKey));
    else if (numButtons == 2 || numButtons == 3)
    {
        const juce::KeyPress key1 ((int) juce::CharacterFunctions::toLowerCase (button1[0]), 0, 0);
        auto key2 = juce::KeyPress ((int) juce::CharacterFunctions::toLowerCase (button2[0]), 0, 0);
        if (key1 == key2)
            key2 = juce::KeyPress();
        if (numButtons == 2)
        {
            alert->addButton (button1, 1, juce::KeyPress (juce::KeyPress::returnKey), key1);
            alert->addButton (button2, 0, juce::KeyPress (juce::KeyPress::escapeKey), key2);
        }
        else
        {
            alert->addButton (button1, 1, key1);
            alert->addButton (button2, 2, key2);
            alert->addButton (button3, 0, juce::KeyPress (juce::KeyPress::escapeKey));
        }
    }
    return alert;
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds().toFloat();
    g.fillAll (bg2); // (round 190jp, design190ia SG-20: the one dialog body colour, as ui/DialogFrame.h)
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);
    g.setColour (accent);
    lynx::ui::dialog::fillBand (g, bounds.getX(), bounds.getY(), bounds.getWidth(), 3.0f); // (round 190oo, WG-22: whole device rows)

    auto iconSpace = 0;
    const auto type = alert.getAlertType();

    if (type != juce::MessageBoxIconType::NoIcon)
    {
        iconSpace = 46;
        const auto iconArea = juce::Rectangle<float> ((float) textArea.getX() - 2.0f, (float) textArea.getY() + 2.0f, 30.0f, 30.0f);
        const auto tint = type == juce::MessageBoxIconType::WarningIcon ? warning : type == juce::MessageBoxIconType::InfoIcon ? info : accent;
        g.setColour (tint.withAlpha (0.16f));
        g.fillEllipse (iconArea);
        g.setColour (tint);
        g.setFont (uiFont (18.0f, true));
        g.drawText (type == juce::MessageBoxIconType::QuestionIcon ? "?" : type == juce::MessageBoxIconType::InfoIcon ? "i" : "!",
                    iconArea, juce::Justification::centred);
    }

    g.setColour (alert.findColour (juce::AlertWindow::textColourId));
    textLayout.draw (g, textArea.withTrimmedLeft (iconSpace).toFloat());
}

void LookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX, int titleSpaceW,
                                              const juce::Image* icon, bool drawTitleTextOnLeft)
{
    if (w * h == 0)
        return;

    g.fillAll (bg1);
    g.setColour (border);
    g.fillRect (0, h - 1, w, 1);

    const auto font = uiFont (type::body);
    g.setFont (font);
    // round 190in (design190ia FW-5): a plugin / channel window's title bar holds FL's ▸ at the left and the presets at
    // the right (the window says how much room they take), and its "Name (target)" in FL's two tones
    const auto& props = window.getProperties();
    const auto insetLeft = (int) props.getWithDefault ("lynxTitleLeft", 0), insetRight = (int) props.getWithDefault ("lynxTitleRight", 0);
    titleSpaceX += insetLeft;
    titleSpaceW = juce::jmax (0, titleSpaceW - insetLeft - insetRight);
    auto textW = juce::GlyphArrangement::getStringWidthInt (font, window.getName());
    auto iconW = 0;

    if (icon != nullptr)
        iconW = h - 10 + 6;

    textW = juce::jmin (titleSpaceW, textW + iconW);
    auto textX = drawTitleTextOnLeft ? titleSpaceX + 4 : juce::jmax (titleSpaceX, (w - textW) / 2);

    if (icon != nullptr)
    {
        g.setOpacity (window.isActiveWindow() ? 1.0f : 0.6f);
        // LYNX's own logo (by w3teal): pre-scaled at the physical size, the small-size form up to 32 px
        const auto px = juce::roundToInt ((float) (h - 10) * juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor()));
        const auto shown = (*icon == logoImage()) ? logoAt (px) : *icon;
        g.drawImageWithin (shown, textX, 5, h - 10, h - 10, juce::RectanglePlacement::centred, false);
        textX += iconW;
        textW -= iconW;
    }

    g.setColour (window.isActiveWindow() ? textPrimary : textSecondary);
    const auto name = window.getName();
    if (const auto open = name.lastIndexOf (" ("); (bool) props.getWithDefault ("lynxTitleTwoTone", false) && open > 0 && name.endsWithChar (')'))
    {
        const auto head = name.substring (0, open);
        const auto room = juce::jmax (0, titleSpaceX + titleSpaceW - textX); // (the bold name is wider than textW measured)
        g.setFont (uiFont (type::body, true));
        const auto headW = juce::jmin (room, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), head) + 1);
        g.drawText (head, textX, 0, headW, h, juce::Justification::centredLeft, true);
        g.setFont (font);
        g.setColour (textSecondary);
        g.drawText (name.substring (open), textX + headW, 0, juce::jmax (0, room - headW), h, juce::Justification::centredLeft, true);
        return;
    }
    g.drawText (name, textX, 0, textW, h, juce::Justification::centredLeft, true);
}

juce::Button* LookAndFeel::createDocumentWindowButton (int buttonType)
{
    if (buttonType == juce::DocumentWindow::closeButton)
        return new CaptionButton ("close", Icon::close, true);
    if (buttonType == juce::DocumentWindow::minimiseButton)
        return new CaptionButton ("minimise", Icon::minimise, false);
    return new CaptionButton ("maximise", Icon::maximise, false);
}

void LookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (isMouseOver || isMouseDragging ? textSecondary : textDisabled);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w * i, (float) h + 1.0f, (float) w + 1.0f, (float) h * i, 1.0f);
}

void LookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar& bar, int width, int height, double progress, const juce::String& textToShow)
{
    juce::ignoreUnused (bar);
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    fillRounded (g, r, r.getHeight() / 2, bg0);

    if (progress >= 0.0 && progress <= 1.0)
        fillRounded (g, r.withWidth (juce::jmax (r.getHeight(), (float) (r.getWidth() * progress))), r.getHeight() / 2, accent);
    else // indeterminate: a sliding segment
    {
        const auto t = (float) std::fmod (juce::Time::getMillisecondCounterHiRes() / 1200.0, 1.0);
        fillRounded (g, r.withWidth (r.getWidth() * 0.3f).withX (r.getX() + (r.getWidth() * 1.3f) * t - r.getWidth() * 0.3f), r.getHeight() / 2,
                     accent.withAlpha (0.8f));
    }

    if (textToShow.isNotEmpty())
    {
        g.setColour (textPrimary);
        g.setFont (uiFont (type::small));
        g.drawText (textToShow, r, juce::Justification::centred, false);
    }
}

void LookAndFeel::drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text, const juce::Justification&,
                                             juce::GroupComponent&)
{
    const auto r = juce::Rectangle<float> (0.5f, 8.5f, (float) w - 1.0f, (float) h - 9.0f);
    outlineRounded (g, r, radius::lg, border);

    if (text.isNotEmpty())
    {
        const auto font = uiFont (type::small, true);
        const auto tw = juce::GlyphArrangement::getStringWidth (font, text) + 10.0f;
        g.setColour (bg2);
        g.fillRect (12.0f, 0.0f, tw, 16.0f);
        g.setColour (textSecondary);
        g.setFont (font);
        g.drawText (text, juce::Rectangle<float> (12.0f, 0.0f, tw, 16.0f), juce::Justification::centred, false);
    }
}

// --- file browser ----------------------------------------------------------------------------------

void LookAndFeel::drawFileBrowserRow (juce::Graphics& g, int width, int height, const juce::File& file, const juce::String& filename,
                                      juce::Image*, const juce::String&, const juce::String&, bool isDirectory, bool isItemSelected, int,
                                      juce::DirectoryContentsDisplayComponent&)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();

    if (isItemSelected)
    {
        fillRounded (g, r.reduced (1.0f, 0.5f), radius::sm, accentMuted);
        g.setColour (accent);
        g.fillRoundedRectangle (r.withWidth (2.5f).reduced (0.0f, 3.0f), 1.0f);
    }

    auto iconKind = Icon::file;
    if (isDirectory)
        iconKind = Icon::folder;
    else if (file.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;wv"))
        iconKind = Icon::audioFile;
    else if (file.hasFileExtension ("lynx;flp;als;flm;dawproj;dawproject;blx;mod;xm;s3m;it"))
        iconKind = Icon::project;
    else if (file.hasFileExtension ("mid;midi"))
        iconKind = Icon::pianoRoll;
    else if (file.hasFileExtension ("fst;lynxpreset"))
        iconKind = Icon::preset;

    const auto iconSize = juce::jmin (16.0f, (float) height - 4.0f);
    icons::draw (g, iconKind, juce::Rectangle<float> (4.0f, ((float) height - iconSize) / 2.0f, iconSize, iconSize),
                 isDirectory ? textSecondary.brighter (0.15f) : (iconKind == Icon::audioFile ? info : textSecondary));

    g.setColour (isItemSelected ? textPrimary : (isDirectory ? textPrimary : textPrimary.withAlpha (0.88f)));
    g.setFont (uiFont (type::body, false));
    g.drawFittedText (filename, juce::Rectangle<int> (juce::roundToInt (iconSize) + 10, 0, width - juce::roundToInt (iconSize) - 12, height),
                      juce::Justification::centredLeft, 1, 0.9f);
}

void LookAndFeel::drawTreeviewPlusMinusBox (juce::Graphics& g, const juce::Rectangle<float>& area, juce::Colour, bool isOpen, bool isMouseOver)
{
    const auto r = area.withSizeKeepingCentre (12.0f, 12.0f);
    juce::Graphics::ScopedSaveState state (g);
    if (! isOpen)
        g.addTransform (juce::AffineTransform::rotation (-pi * 0.5f, r.getCentreX(), r.getCentreY()));
    icons::draw (g, Icon::chevronDown, r, isMouseOver ? textPrimary : textSecondary);
}
} // namespace lynx::theme
