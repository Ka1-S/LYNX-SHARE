#include "core/ThemePalette.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace lynx::themes
{
namespace
{
int red (std::uint32_t c) { return (int) ((c >> 16) & 0xff); }
int green (std::uint32_t c) { return (int) ((c >> 8) & 0xff); }
int blue (std::uint32_t c) { return (int) (c & 0xff); }

std::uint32_t rgb (int r, int g, int b)
{
    auto clamp = [] (int v) { return (std::uint32_t) std::clamp (v, 0, 255); };
    return 0xff000000u | (clamp (r) << 16) | (clamp (g) << 8) | clamp (b);
}

// The built-in palette (the values ui/Theme.h shipped with before themes existed).
constexpr Palette builtIn {
    0xff0e0f12, 0xff15161a, 0xff1b1d22, 0xff23262c, 0xff2c2f37, 0xff363a43, 0xff434852, // bg0..bg6
    0xff2a2d34, 0xff3b3f48,                                                             // border, borderStrong
    0xffeceef2, 0xffa1a6b0, 0xff767b85,                                                 // text (disabled: round 190je, 3.6:1 on bg3)
    0xffe5383b, 0xfff2545a, 0xffc12a30, 0xff5c1f24, 0xffffffff, 0x2ee5383b,             // accent family
    0xffc5c9d1, 0xff444955, 0xff2a2d34, 0xff4c3f46, 0xffe8a3a6,                         // knobs, steps (alt: stepOffAlt below)
    0xff3a3e47, 0xff262930, 0xff1d1f24, 0xff17181c, 0xff1c1e23, 0xff16171b,             // grids, keys, rows
    0xffb3545a, 0xffff6b6e, 0xff4a4e57, 0xff3a3e47,                                     // notes, LED
    0xff747983                                                                          // outline (round 190je: 3.1:1 on bg4, 3.5:1 on bg3)
};

// A surface token for another background: the built-in step from bg1 to that token, applied to the new background
// (mirrored for a light background, whose raised surfaces get darker instead of lighter).
std::uint32_t surface (std::uint32_t token, std::uint32_t background, bool light)
{
    const auto base = builtIn.bg1;
    const auto sign = light ? -1 : 1;
    return rgb (red (background) + sign * (red (token) - red (base)), green (background) + sign * (green (token) - green (base)),
                blue (background) + sign * (blue (token) - blue (base)));
}
} // namespace

std::uint32_t mix (std::uint32_t a, std::uint32_t b, double amount)
{
    const auto t = std::clamp (amount, 0.0, 1.0);
    auto channel = [t] (int x, int y) { return (int) std::lround (x + (y - x) * t); };
    return rgb (channel (red (a), red (b)), channel (green (a), green (b)), channel (blue (a), blue (b)));
}

double relativeLuminance (std::uint32_t argb)
{
    auto linear = [] (int v) {
        const auto c = v / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear (red (argb)) + 0.7152 * linear (green (argb)) + 0.0722 * linear (blue (argb));
}

double contrastRatio (std::uint32_t a, std::uint32_t b)
{
    const auto la = relativeLuminance (a), lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

std::uint32_t readableTextOn (std::uint32_t background)
{
    constexpr std::uint32_t white = 0xffffffff, dark = 0xff111214;
    // white wins ties: LYNX's accent keeps its white text unless the colour is clearly light
    return contrastRatio (white, background) * 1.7 >= contrastRatio (dark, background) ? white : dark;
}

Palette derivePalette (std::uint32_t accentIn, std::uint32_t backgroundIn)
{
    const auto accent = accentIn | 0xff000000u;
    const auto background = backgroundIn | 0xff000000u;
    if (accent == builtIn.accent && background == builtIn.bg1)
        return builtIn;

    Palette p = builtIn;
    const auto light = relativeLuminance (background) > 0.4;

    // surfaces, hairlines, grids: the built-in steps from the background
    for (auto* token : { &p.bg0, &p.bg1, &p.bg2, &p.bg3, &p.bg4, &p.bg5, &p.bg6, &p.border, &p.borderStrong, &p.knobTrack, &p.stepOff,
                         &p.gridBar, &p.gridBeat, &p.gridStep, &p.keyBlack, &p.rowWhite, &p.rowBlack, &p.noteMuted, &p.ledOff, &p.outline })
        *token = surface (*token, background, light);

    if (light)
    {
        p.textPrimary = 0xff16181c;
        p.textSecondary = 0xff4c515b;
        p.textDisabled = 0xff8d929b;
        p.knobArc = 0xff4c515b;
    }
    // a mid-grey background where the dark theme's text would not read: the readable one
    if (contrastRatio (p.textPrimary, p.bg2) < 4.5)
        p.textPrimary = readableTextOn (p.bg2);

    // the accent family
    if (accent == builtIn.accent)
    {
        p.accentMuted = mix (background, accent, 0.34);
    }
    else
    {
        p.accent = accent;
        p.accentHover = mix (accent, 0xffffffff, 0.14);
        p.accentPressed = mix (accent, 0xff000000, 0.18);
        p.accentMuted = mix (background, accent, 0.34);
        p.onAccent = readableTextOn (accent);
        p.selectionFill = (accent & 0x00ffffffu) | 0x2e000000u;
        p.stepNote = mix (accent, light ? 0xff000000 : 0xffffffff, 0.55);
        p.noteHard = mix (accent, 0xffffffff, 0.26);
        p.noteSoft = mix (accent, 0xff808080, 0.42);
    }
    // the Channel Rack's every-other beat group: clearly apart at a glance (Busy Works Beats' video, round 170a: "too
    // close of a color") yet a dull reddish grey like FL's, far from a lit step (round 170a2)
    p.stepOffAlt = mix (mix (p.stepOff, p.textPrimary, stepGroupLift), p.accent, stepGroupTint);

    // the contrast floors (round 190je): secondary text, disabled text and control outlines stay readable on every
    // background (the built-in look meets them as it is)
    p.textSecondary = withContrast (p.textSecondary, p.bg3, textContrastFloor);
    p.textDisabled = withContrast (p.textDisabled, p.bg3, disabledContrastFloor);
    p.outline = withContrast (p.outline, p.bg4, partContrastFloor); // (controls also sit on bg4 toolbars)
    return p;
}

std::uint32_t withContrast (std::uint32_t colour, std::uint32_t background, double ratio)
{
    colour |= 0xff000000u;
    if (contrastRatio (colour, background) >= ratio)
        return colour;
    // away from the background: towards white when the background is the darker one, else towards black (if that side
    // can't reach the ratio, the other one is tried)
    const auto darkBackground = relativeLuminance (background) < relativeLuminance (colour)
                                || (relativeLuminance (background) == relativeLuminance (colour) && relativeLuminance (background) < 0.18);
    for (const auto target : { darkBackground ? 0xffffffffu : 0xff000000u, darkBackground ? 0xff000000u : 0xffffffffu })
    {
        if (contrastRatio (target, background) < ratio)
            continue;
        double lo = 0.0, hi = 1.0; // the least amount of `target` that reaches the ratio
        for (int i = 0; i < 24; ++i)
        {
            const auto m = 0.5 * (lo + hi);
            (contrastRatio (mix (colour, target, m), background) >= ratio ? hi : lo) = m;
        }
        for (auto amount = hi; amount <= 1.0; amount += 1.0 / 512.0) // (rounding to 8 bits may land just short)
            if (const auto c = mix (colour, target, amount); contrastRatio (c, background) >= ratio)
                return c;
        return target;
    }
    return contrastRatio (0xffffffffu, background) >= contrastRatio (0xff000000u, background) ? 0xffffffffu : 0xff000000u;
}

namespace
{
std::uint32_t withAlpha (std::uint32_t c, double alpha)
{
    const auto a = (std::uint32_t) std::lround (std::clamp (alpha, 0.0, 1.0) * (double) ((c >> 24) & 0xffu));
    return (c & 0x00ffffffu) | (a << 24);
}
} // namespace

ButtonLook buttonLook (const Palette& p, ButtonKind kind, bool on, double hoverIn, bool down, bool enabled)
{
    const auto hover = enabled ? std::clamp (hoverIn, 0.0, 1.0) : 0.0;
    const auto flat = kind != ButtonKind::secondary;
    // text drawn on the accent keeps 4.5:1 (LYNX red with white text is 4.2:1: a hair deeper red where text sits on it)
    const auto accentFill = flat ? p.accent : withContrast (p.accent, p.onAccent, textContrastFloor);
    ButtonLook look {};

    if (! enabled)
    {
        look.fill = on ? withAlpha (accentFill, 0.5) : flat ? 0u : withAlpha (p.bg4, 0.5);
        look.edge = flat ? 0u : withAlpha (p.outline, 0.5);
        look.content = p.textDisabled;
        return look;
    }

    if (on)
    {
        look.fill = down ? p.accentPressed : flat ? mix (accentFill, p.accentHover, hover) : accentFill;
        look.edge = ! flat && hover > 0.0 && ! down ? withAlpha (p.accentHover, hover) : 0u; // (a lighter fill would lose the text's contrast)
        look.content = p.onAccent;
        return look;
    }

    switch (kind)
    {
        case ButtonKind::secondary:
            look.fill = down ? p.bg0 : mix (p.bg4, p.bg5, hover);
            look.edge = p.outline;
            look.content = p.textPrimary;
            break;
        case ButtonKind::flatMomentary:
            look.fill = down ? p.bg0 : withAlpha (p.bg5, hover);
            look.edge = withAlpha (p.borderStrong, down ? 1.0 : hover);
            look.content = mix (p.textSecondary, p.textPrimary, down ? 1.0 : hover);
            break;
        case ButtonKind::flatToggle: // FL: the "on" look shows under the mouse
            look.fill = down ? p.accentMuted : withAlpha (p.accentMuted, 0.7 * hover);
            look.edge = withAlpha (p.accent, down ? 1.0 : 0.5 * hover);
            look.content = mix (p.textSecondary, p.accentHover, down ? 1.0 : hover);
            break;
    }
    return look;
}

Palette withGridIntensity (Palette p, double intensity)
{
    const auto k = std::clamp (intensity, gridIntensityMin, gridIntensityMax);
    if (std::abs (k - 1.0) < 1.0e-6)
        return p;
    const auto field = p.rowWhite;
    const auto scale = [field, k] (std::uint32_t line) {
        const auto channel = [k] (int f, int l) { return (int) std::lround (f + (l - f) * k); };
        return rgb (channel (red (field), red (line)), channel (green (field), green (line)), channel (blue (field), blue (line)));
    };
    p.gridBar = scale (p.gridBar);
    p.gridBeat = scale (p.gridBeat);
    p.gridStep = scale (p.gridStep);
    return p;
}

Palette paletteFor (const Spec& spec)
{
    return withGridIntensity (derivePalette (spec.accent, spec.background), spec.gridIntensity);
}

const char* menuStyleId (MenuStyle style)
{
    return style == MenuStyle::flClassic ? "flClassic" : "lynx";
}

MenuStyle menuStyleFromId (const std::string& id)
{
    return id == "flClassic" ? MenuStyle::flClassic : MenuStyle::lynxDark;
}

MenuColours menuColours (const Palette& p, MenuStyle style)
{
    if (style == MenuStyle::flClassic)
        // FL Studio 2026's menus (ref/fl-menu-file.png, fl-channel-rack-menu-flex.png): light grey, dark text, a darker
        // section band, a slate hover with white text. FL's band text is light (#E6EEF2, 2.4:1) and its shortcuts 4.5:1:
        // here dark band text (6.0:1) and a slightly darker shortcut grey (4.7:1)
        return { 0xffbbc4c9, 0xff5f686d, 0xff0f1f29, 0xff4c4e57, 0xff5e6870, 0xff919ca4, 0xff0f1f29, 0xff5e6c75, 0xffffffff, 0xff949ca2, 0xff0f1f29 };
    // LYNX's dark menus: the band between bg3 and bg5 (FW-12)
    return { p.bg3, p.borderStrong, p.textPrimary, p.textSecondary, p.textDisabled, mix (p.bg3, p.bg5, 0.55), p.textSecondary, p.bg5, p.textPrimary,
             mix (p.border, p.borderStrong, 0.5), p.accent };
}

int tallMenuBreak (const std::vector<MenuRow>& rows, int maxHeight)
{
    if (rows.size() < 4 || maxHeight <= 0)
        return -1;
    int total = 0;
    for (const auto& r : rows)
        total += r.height;
    if (total <= maxHeight)
        return -1;

    // the boundaries: before a section header (best: the second column starts with its band, as FL's Add menu), after a
    // separator (the line closes the first column)
    int best = -1, bestTallest = 0;
    bool bestIsHeader = false;
    int above = 0;
    for (std::size_t i = 0; i + 1 < rows.size(); ++i)
    {
        above += rows[i].height;
        const auto header = rows[i + 1].header;
        if (! header && ! rows[i].separator)
            continue;
        if (rows[i].header) // (never a header alone at the foot of a column)
            continue;
        const auto tallest = std::max (above, total - above);
        // a header boundary wins over a separator one unless the separator's columns are clearly more even (3 rows)
        const auto better = best < 0 || (header == bestIsHeader ? tallest < bestTallest
                                                                : header ? tallest < bestTallest + 63 : tallest + 63 < bestTallest);
        if (better)
        {
            best = (int) i;
            bestTallest = tallest;
            bestIsHeader = header;
        }
    }
    if (best < 0 || bestTallest > maxHeight * 10 / 7) // (two columns still taller than the screen: JUCE's own split)
        return -1;
    return best;
}

std::uint32_t litStepColour (std::uint32_t channelColour, const Palette& palette)
{
    if ((channelColour & 0x00ffffffu) == 0)
        return palette.accent;
    const auto colour = channelColour | 0xff000000u;
    const auto lightTheme = relativeLuminance (palette.stepOff) > 0.4;
    const auto towards = lightTheme ? 0xff000000u : 0xffffffffu;
    for (int i = 0; i <= 20; ++i)
    {
        const auto lit = mix (colour, towards, i * 0.05);
        if (contrastRatio (lit, palette.stepOff) >= minLitStepContrast && contrastRatio (lit, palette.stepOffAlt) >= minLitStepContrast)
            return lit;
    }
    return palette.accent;
}

const Palette& defaultPalette()
{
    return builtIn;
}

// --- Adjustments and colour dots (round 190jp, design190ia FW-20) --------------------------------------------------------
namespace
{
struct Hsv
{
    double h, s, v; // h in degrees 0..360, s and v 0..1
};

Hsv toHsv (std::uint32_t c)
{
    const auto r = red (c) / 255.0, g = green (c) / 255.0, b = blue (c) / 255.0;
    const auto hi = std::max ({ r, g, b }), lo = std::min ({ r, g, b }), d = hi - lo;
    auto h = 0.0;
    if (d > 0.0)
    {
        if (hi == r)
            h = 60.0 * std::fmod ((g - b) / d, 6.0);
        else if (hi == g)
            h = 60.0 * ((b - r) / d + 2.0);
        else
            h = 60.0 * ((r - g) / d + 4.0);
    }
    if (h < 0.0)
        h += 360.0;
    return { h, hi > 0.0 ? d / hi : 0.0, hi };
}

std::uint32_t fromHsv (Hsv x, std::uint32_t alphaOf)
{
    auto h = std::fmod (x.h, 360.0);
    if (h < 0.0)
        h += 360.0;
    const auto s = std::clamp (x.s, 0.0, 1.0), v = std::clamp (x.v, 0.0, 1.0);
    const auto c = v * s, hp = h / 60.0, xx = c * (1.0 - std::abs (std::fmod (hp, 2.0) - 1.0)), m = v - c;
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = xx; }
    else if (hp < 2) { r = xx; g = c; }
    else if (hp < 3) { g = c; b = xx; }
    else if (hp < 4) { g = xx; b = c; }
    else if (hp < 5) { r = xx; b = c; }
    else { r = c; b = xx; }
    const auto out = rgb ((int) std::lround ((r + m) * 255.0), (int) std::lround ((g + m) * 255.0), (int) std::lround ((b + m) * 255.0));
    return (out & 0x00ffffffu) | (alphaOf & 0xff000000u);
}
} // namespace

const char* dotId (Dot dot)
{
    static const char* ids[] { "selection", "highlight", "mute", "option", "steps", "meters", "waves", "text", "tracks" };
    const auto i = (int) dot;
    return i >= 0 && i < dotCount ? ids[i] : "selection";
}

Palette withAdjustments (Palette p, const Spec& spec)
{
    if (! spec.hasAdjustments())
        return p;
    const auto hue = std::clamp ((double) spec.hue, -180.0, 180.0);
    const auto sat = std::clamp ((double) spec.saturation, -1.0, 1.0);
    const auto bright = std::clamp ((double) spec.brightness, -1.0, 1.0);
    const auto contrast = std::clamp ((double) spec.contrast, -1.0, 1.0);
    const auto text = std::clamp ((double) spec.text, -1.0, 1.0);
    const auto light = relativeLuminance (p.bg1) > 0.4;
    const auto pivot = toHsv (p.bg3).v; // Contrast spreads the surfaces around the raised one

    // hue and saturation: every grey and text token keeps its value, its tint turns / grows / fades
    const auto tint = [&] (Hsv x) {
        x.h += hue;
        x.s = sat >= 0.0 ? x.s + sat * std::max (0.0, 0.6 - x.s) : x.s * (1.0 + sat);
        return x;
    };
    for (auto* token : { &p.bg0, &p.bg1, &p.bg2, &p.bg3, &p.bg4, &p.bg5, &p.bg6, &p.border, &p.borderStrong, &p.knobTrack, &p.stepOff, &p.stepOffAlt,
                         &p.gridBar, &p.gridBeat, &p.gridStep, &p.keyBlack, &p.rowWhite, &p.rowBlack, &p.noteMuted, &p.ledOff, &p.outline })
    {
        auto x = tint (toHsv (*token));
        x.v = bright >= 0.0 ? x.v + bright * (1.0 - x.v) * 0.35 : x.v * (1.0 + bright * 0.6);
        x.v = pivot + (x.v - pivot) * (1.0 + contrast * 0.8);
        *token = fromHsv (x, *token);
    }
    // the text: away from the panels (towards white on a dark theme, black on a light one) or towards them
    const auto away = light ? 0xff000000u : 0xffffffffu;
    for (auto* token : { &p.textPrimary, &p.textSecondary, &p.textDisabled, &p.knobArc })
    {
        const auto tinted = fromHsv (tint (toHsv (*token)), *token);
        *token = text >= 0.0 ? mix (tinted, away, text * 0.6) : mix (tinted, p.bg2, -text * 0.5);
    }
    return p;
}

Palette withDots (Palette p, const Spec& spec)
{
    const auto dot = [&spec] (Dot d) { return spec.dots[(std::size_t) d]; };
    if (const auto c = dot (Dot::highlight); c != 0)
        p.accentHover = c | 0xff000000u;
    if (const auto c = dot (Dot::mute); c != 0)
        p.textDisabled = c | 0xff000000u;
    if (const auto c = dot (Dot::text); c != 0)
        p.textPrimary = c | 0xff000000u;
    if (const auto c = dot (Dot::selection); c != 0)
        p.selectionFill = (c & 0x00ffffffu) | (p.selectionFill & 0xff000000u);
    return p;
}

Palette adjustedPalette (const Spec& spec)
{
    // (merge 190jp + 190je: from paletteFor, so the grid line intensity holds; after Adjustments the contrast floors are
    // held again, as derivePalette holds them)
    auto p = withAdjustments (paletteFor (spec), spec);
    if (spec.hasAdjustments())
    {
        p.textSecondary = withContrast (p.textSecondary, p.bg3, textContrastFloor);
        p.textDisabled = withContrast (p.textDisabled, p.bg3, disabledContrastFloor);
        p.outline = withContrast (p.outline, p.bg4, partContrastFloor);
    }
    return withDots (p, spec);
}

const char* imageFitId (ImageFit fit)
{
    switch (fit)
    {
        case ImageFit::fill: return "fill";
        case ImageFit::fit: return "fit";
        case ImageFit::stretch: return "stretch";
        case ImageFit::tile: return "tile";
        case ImageFit::centre: return "centre";
    }
    return "fill";
}

ImageFit imageFitFromId (const std::string& id)
{
    for (int i = 0; i < imageFitCount; ++i)
        if (id == imageFitId ((ImageFit) i))
            return (ImageFit) i;
    if (id == "center")
        return ImageFit::centre;
    return ImageFit::fill;
}

const std::vector<Spec>& builtInThemes()
{
    static const std::vector<Spec> all = [] {
        auto make = [] (const char* name, std::uint32_t accent, std::uint32_t background) {
            Spec s;
            s.name = name;
            s.accent = accent;
            s.background = background;
            return s;
        };
        return std::vector<Spec> {
            make ("LYNX", defaultAccent, defaultBackground), make ("Ocean", 0xff3d8bfd, 0xff12161d), make ("Emerald", 0xff2fbf71, 0xff131715),
            make ("Amber", 0xfff0a020, 0xff17150f),          make ("Violet", 0xff9d6bff, 0xff16141c), make ("Graphite", 0xffd5d8de, 0xff141414),
        };
    }();
    return all;
}

std::uint32_t workspaceColour (const Spec& spec)
{
    return spec.workspace != 0 ? (spec.workspace | 0xff000000u) : derivePalette (spec.accent, spec.background).bg0;
}

std::string toHex (std::uint32_t argb)
{
    static const char* digits = "0123456789ABCDEF";
    std::string s = "#";
    for (int shift = 20; shift >= 0; shift -= 4)
        s += digits[(argb >> shift) & 0xf];
    return s;
}

bool parseHex (const std::string& textIn, std::uint32_t& argb)
{
    std::string text;
    for (auto c : textIn)
        if (! std::isspace ((unsigned char) c))
            text += c;
    if (text.rfind ("0x", 0) == 0 || text.rfind ("0X", 0) == 0)
        text = text.substr (2);
    else if (! text.empty() && text[0] == '#')
        text = text.substr (1);
    if (text.size() == 3)
        text = std::string { text[0], text[0], text[1], text[1], text[2], text[2] };
    if (text.size() == 8) // AARRGGBB: the alpha is dropped (themes are opaque)
        text = text.substr (2);
    if (text.size() != 6)
        return false;
    std::uint32_t value = 0;
    for (auto c : text)
    {
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= (std::uint32_t) (c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= (std::uint32_t) (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= (std::uint32_t) (c - 'A' + 10);
        else
            return false;
    }
    argb = 0xff000000u | value;
    return true;
}
} // namespace lynx::themes
