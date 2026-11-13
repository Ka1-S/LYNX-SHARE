#include "ui/Theme.h"

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
#include <limits>
#include <unordered_map>
#include <vector>

#include "core/MenuPlacement.h"
#include "core/UiFontChoice.h"
#include "platform/Paths.h"
#include "ui/Icons.h"
#include "ui/ImageCaches.h"
#include "ui/PluginSkin.h"
#include "ui/WindowOwner.h"

namespace lynx::theme
{
using namespace colour;

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

void blit (juce::Graphics& g, const juce::Image& sprite, const SpritePlace& at, int scale, bool mask)
{
    if (scale == 1)
        g.drawImageAt (sprite, at.x, at.y, mask);
    else
        g.drawImage (sprite, juce::Rectangle<float> ((float) at.x / (float) scale, (float) at.y / (float) scale, (float) sprite.getWidth() / (float) scale,
                                                     (float) sprite.getHeight() / (float) scale), juce::RectanglePlacement::stretchToFit, mask);
}

void drawKnobDirect (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arcColour, bool hover);
} // namespace

void fillRoundRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour colour)
{
    g.setColour (colour);
    if (r.isEmpty() || ! g.clipRegionIntersects (r.getSmallestIntegerContainer()))
        return;
    if (! useSprites || corner <= 0.0f)
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
        blit (g, *sprite, at, scale, false);
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
    put (cornerMask (0, pl, pt), x0, y0);
    put (cornerMask (1, pr, pt), xb, y0);
    put (cornerMask (2, pl, pbot), x0, yc);
    put (cornerMask (3, pr, pbot), xb, yc);
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
    const auto scale = useSprites ? spriteScale (g) : 0;
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
    auto* sprite = cache.find (key);
    if (sprite == nullptr)
    {
        const auto w = (float) at.fx * 0.125f + bounds.getWidth() * (float) scale, h = (float) at.fy * 0.125f + bounds.getHeight() * (float) scale;
        juce::Image image (juce::Image::ARGB, (int) std::ceil (w) + 1, (int) std::ceil (h) + 1, true, juce::SoftwareImageType());
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale).translated ((float) at.fx * 0.125f, (float) at.fy * 0.125f));
            drawKnobDirect (ig, { (float) eighth (bounds.getWidth() * (float) scale) * 0.125f / (float) scale,
                                  (float) eighth (bounds.getHeight() * (float) scale) * 0.125f / (float) scale }, (float) value / 1440.0f, bipolar, arcColour, hover);
        }
        sprite = &cache.add (key, std::move (image));
    }
    blit (g, *sprite, at, scale, false);
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

void setPalette (const themes::Palette& p)
{
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

    // the legacy names
    theme::window = bg1; theme::panel = bg2; theme::toolbar = bg3; theme::channelButton = bg4; theme::channelButtonSelected = bg6;
    theme::knobBackground = bg0; theme::stepOn = accent; theme::stepOnOffGrid = stepNote; theme::accentOrange = accent;
    theme::playhead = accent; theme::text = textPrimary;
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

themes::Palette currentPalette()
{
    auto c = [] (juce::Colour x) { return x.getARGB(); };
    return { c (bg0), c (bg1), c (bg2), c (bg3), c (bg4), c (bg5), c (bg6), c (border), c (borderStrong), c (textPrimary), c (textSecondary),
             c (textDisabled), c (accent), c (accentHover), c (accentPressed), c (accentMuted), c (onAccent), c (selectionFill), c (knobArc),
             c (knobTrack), c (colour::stepOff), c (stepOffAlt), c (stepNote), c (gridBar), c (gridBeat), c (gridStep), c (keyBlack), c (rowWhite),
             c (rowBlack), c (noteSoft), c (noteHard), c (noteMuted), c (ledOff) };
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
const std::array<juce::Typeface::Ptr, 2>& bundledFaces()
{
    static const std::array<juce::Typeface::Ptr, 2> faces {
        juce::Typeface::createSystemTypefaceFor (lynxfontdata::OpenSansRegular_ttf, (size_t) lynxfontdata::OpenSansRegular_ttfSize),
        juce::Typeface::createSystemTypefaceFor (lynxfontdata::OpenSansBold_ttf, (size_t) lynxfontdata::OpenSansBold_ttfSize)
    };
    return faces;
}
#endif

bool typefaceFallbackOff = false; // (fontSelfTest only)
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
    refreshColours();
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
    const std::pair<const char*, juce::FontOptions> fonts[] = {
        { "default", font (type::body) },
        { "bold", font (type::body, true) },
        { "monospaced", monoFont (type::body) },
        { "serif", juce::FontOptions (juce::Font::getDefaultSerifFontName(), type::body, juce::Font::plain) },
        { "unknown family", juce::FontOptions ("LYNX no such family 142", type::body, juce::Font::bold) },
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

    setColour (juce::PopupMenu::backgroundColourId, bg3);
    setColour (juce::PopupMenu::textColourId, textPrimary);
    setColour (juce::PopupMenu::headerTextColourId, textSecondary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, bg5);
    setColour (juce::PopupMenu::highlightedTextColourId, textPrimary);

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
    setColour (juce::TextEditor::outlineColourId, border);
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

    if (down)
        base = on ? accentPressed : (base == bg4 ? bg6 : base.brighter (0.18f));
    else if (highlighted)
        base = on ? accentHover : (base == bg4 ? bg5 : base.brighter (0.1f));

    if (! button.isEnabled())
        base = base.withMultipliedAlpha (0.5f);

    const auto flatLeft = button.isConnectedOnLeft(), flatRight = button.isConnectedOnRight();
    juce::Path p;
    p.addRoundedRectangle (bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), radius::md, radius::md, ! flatLeft,
                           ! flatRight, ! flatLeft, ! flatRight);
    g.setColour (base);
    g.fillPath (p);

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
    const auto fg = button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);
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
        g.setColour (ticked ? (enabled ? (highlighted ? accentHover : accent) : accentMuted) : (highlighted ? textSecondary : borderStrong));
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
        outlineRounded (g, box, radius::sm, highlighted ? textSecondary : borderStrong);
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

    g.setColour (button.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
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
    fillRounded (g, bounds, radius::md, hover ? bg5 : box.findColour (juce::ComboBox::backgroundColourId));

    if ((box.hasKeyboardFocus (true) || box.getProperties().contains ("lynxSnapshotFocus")) && box.getWantsKeyboardFocus())
        outlineRounded (g, bounds, radius::md, accent.withAlpha (0.7f));
    else if (! box.findColour (juce::ComboBox::outlineColourId).isTransparent())
        outlineRounded (g, bounds, radius::md, box.findColour (juce::ComboBox::outlineColourId));

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
    label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
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
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);
}

namespace menus
{
namespace
{
bool compactMenus = true;
}
void setCompact (bool c) { compactMenus = c; }
bool compact() { return compactMenus; }
int itemHeight() { return compactMenus ? 21 : 26; }
int headerHeight() { return compactMenus ? 19 : 39; }
int separatorHeight() { return compactMenus ? 5 : 9; }
} // namespace menus

juce::Font LookAndFeel::getPopupMenuFont()
{
    return uiFont (type::body);
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
        idealWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (type::small, true), caps (text)) * 0.8f) + 24;
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
    if (isSeparator)
    {
        g.setColour (border.brighter (0.1f));
        g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (4, 1);

    if (isHighlighted && isActive)
    {
        fillRounded (g, r.toFloat(), radius::sm, bg5);
        g.setColour (accent);
        g.fillRoundedRectangle (r.toFloat().withWidth (3.0f).reduced (0.0f, 4.0f), 1.5f);
    }

    auto textColour = textColourToUse != nullptr ? *textColourToUse : textPrimary;
    if (! isActive)
        textColour = textDisabled;

    auto content = r.reduced (8, 0);
    auto iconArea = content.removeFromLeft (juce::roundToInt ((float) r.getHeight() * 0.8f)).toFloat();

    if (icon != nullptr)
        icon->drawWithin (g, iconArea.withSizeKeepingCentre (14.0f, 14.0f), juce::RectanglePlacement::centred, isActive ? 1.0f : 0.4f);
    else if (isTicked)
        icons::draw (g, Icon::check, iconArea.withSizeKeepingCentre (16.0f, 16.0f), isActive ? accent : textDisabled);

    if (hasSubMenu)
    {
        const auto arrow = content.removeFromRight (14).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::rotation (-pi * 0.5f, arrow.getCentreX(), arrow.getCentreY()));
        icons::draw (g, Icon::chevronDown, arrow, isHighlighted ? textPrimary : textSecondary);
    }

    content.removeFromLeft (4);
    const auto font = getPopupMenuFont();
    g.setFont (font);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (textSecondary.withMultipliedAlpha (isActive ? 1.0f : 0.6f));
        g.setFont (uiFont (type::small));
        g.drawText (shortcutKeyText, content.removeFromRight (juce::roundToInt (juce::GlyphArrangement::getStringWidth (uiFont (type::small), shortcutKeyText)) + 4),
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
    g.setFont (uiFont (type::small, true));
    g.setColour (textSecondary);
    g.drawFittedText (caps (sectionName), area.reduced (14, 0).withTrimmedTop (menus::compact() ? 2 : 4), juce::Justification::centredLeft, 1);
}

void LookAndFeel::drawMenuBarBackground (juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
    // transparent: the menu sits inside the main toolbar, which paints the band behind it
}

juce::Font LookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return uiFont (type::body);
}

int LookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent& bar, int itemIndex, const juce::String& itemText)
{
    // a long (translated) menu bar takes tighter item padding so the toolbar keeps its panels (round 165o)
    const auto padding = (bool) bar.getProperties().getWithDefault ("lynxTightMenuBar", false) ? 8 : 16;
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (getMenuBarFont (bar, itemIndex, itemText), itemText)) + padding;
}

void LookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int itemIndex, const juce::String& itemText, bool isMouseOverItem,
                                   bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent& bar)
{
    juce::ignoreUnused (isMouseOverBar);
    const auto active = isMenuOpen || isMouseOverItem;

    if (active)
        fillRounded (g, juce::Rectangle<float> (0.0f, 2.0f, (float) width, (float) height - 4.0f).reduced (1.0f, 0.0f), radius::sm,
                     isMenuOpen ? bg4 : bg3);

    g.setColour (active ? textPrimary : textSecondary.brighter (0.2f));
    g.setFont (getMenuBarFont (bar, itemIndex, itemText));
    g.drawFittedText (itemText, 0, 0, width, height, juce::Justification::centred, 1);
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
        return;
    }
    auto arc = slider.findColour (juce::Slider::rotarySliderFillColourId);
    if (! slider.isEnabled())
        arc = textDisabled;
    drawKnob (g, juce::Rectangle<int> (x, y, width, height).toFloat(), pos, bipolar, arc, slider.isMouseOverOrDragging());
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
        const auto lines = juce::jmax (1, (int) ((float) textArea.getHeight() / font.getHeight()));
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

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& textLayout)
{
    auto bounds = alert.getLocalBounds().toFloat();
    g.fillAll (bg3);
    g.setColour (borderStrong);
    g.drawRect (bounds, 1.0f);
    g.setColour (accent);
    g.fillRect (bounds.removeFromTop (3.0f));

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
    g.drawText (window.getName(), textX, 0, textW, h, juce::Justification::centredLeft, true);
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
    else if (file.hasFileExtension ("lynx;flp;als;flm;dawproj"))
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
