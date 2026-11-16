#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// User themes (round 162d, issues #8 / #9): the accent colour, the background and the workspace backdrop are the
// user's choice. This is the pure part (no JUCE): it derives every colour token of ui/Theme.h from the two colours the
// user picks, so the whole UI keeps its relations (elevation steps, hover/pressed shades, tinted selections) and the
// text stays readable. With LYNX's default accent and background it gives exactly the built-in palette.
namespace lynx::themes
{
constexpr std::uint32_t defaultAccent = 0xffe5383b;     // LYNX red
constexpr std::uint32_t defaultBackground = 0xff15161a; // the app background (token bg1)

// Every themeable token of ui/Theme.h (ARGB).
struct Palette
{
    std::uint32_t bg0, bg1, bg2, bg3, bg4, bg5, bg6;
    std::uint32_t border, borderStrong;
    std::uint32_t textPrimary, textSecondary, textDisabled;
    std::uint32_t accent, accentHover, accentPressed, accentMuted, onAccent, selectionFill;
    std::uint32_t knobArc, knobTrack, stepOff, stepOffAlt, stepNote;
    std::uint32_t gridBar, gridBeat, gridStep, keyBlack, rowWhite, rowBlack;
    std::uint32_t noteSoft, noteHard, noteMuted, ledOff;

    bool operator== (const Palette&) const = default;
};

// FL's number wheels ("digiwheels": channel RANGE / TRACK, the channel rack's insert box) in the LYNX skin (round
// 168s): a dark inset display with light text, red (the accent) only while hovered or dragged. C = a colour type.
template <typename C>
struct DigitDisplayColours
{
    C fill, edge, text;
};
template <typename C>
DigitDisplayColours<C> digitDisplayColours (C bg0, C border, C accent, C accentHover, C textPrimary, C textDisabled, bool hot, bool dimmed)
{
    return { bg0, hot ? accent : border, hot ? accentHover : dimmed ? textDisabled : textPrimary };
}

// The Channel Rack's every-other beat group of steps (Palette::stepOffAlt): the off step lifted towards the text colour
// (a clearly different grey) with a little of the accent (a dull reddish grey, as FL's alternate groups), never
// saturated enough to pass for a lit step (round 170a2: at 28 % accent the empty steps read as active ones).
constexpr double stepGroupLift = 0.10;
constexpr double stepGroupTint = 0.09;

// A lit Channel Rack step in its channel's colour (LYNX, round 170a2; FL keeps its steps white / pink): the colour
// lifted (towards white on a dark theme, black on a light one) until it stands clear of both empty-step greys
// (WCAG contrast >= minLitStepContrast against stepOff and stepOffAlt). 0 (no colour) = the palette's lit step, the accent.
constexpr double minLitStepContrast = 2.6;
std::uint32_t litStepColour (std::uint32_t channelColour, const Palette& palette);

// The palette for an accent and a background (alpha is ignored: both are opaque).
Palette derivePalette (std::uint32_t accent, std::uint32_t background);
// LYNX's built-in palette (= derivePalette (defaultAccent, defaultBackground)).
const Palette& defaultPalette();

// How the workspace image is laid out.
enum class ImageFit
{
    fill,    // covers the whole area, cropped (keeps the proportions)
    fit,     // whole image visible, centred (keeps the proportions)
    stretch, // exactly the area (distorts)
    tile,    // repeated at its own size from the top left
    centre   // its own size, centred
};
constexpr int imageFitCount = 5;
const char* imageFitId (ImageFit fit); // "fill", "fit", "stretch", "tile", "centre"
ImageFit imageFitFromId (const std::string& id);

// FL's colour dots on its Theme page (round 190jp, design190ia FW-20): the colours of the selection, the highlight, muted
// things, options, lit steps, meters, waveforms and the text, each the theme's own unless set (0).
enum class Dot : int { selection = 0, highlight, mute, option, steps, meters, waves, text };
constexpr int dotCount = 8;
const char* dotId (Dot dot); // "selection", "highlight", ... (the .lynxtheme keys)

// A theme: what a .lynxtheme file holds and what Options › Theme edits.
struct Spec
{
    std::string name = "LYNX";
    // FL's Adjustments (round 190jp, design190ia FW-20; Live: Brightness / Color Intensity / Color Hue): shifts of the
    // whole palette's surfaces and text, 0 = none. hue in degrees (-180..180), the others -1..1.
    float hue = 0.0f, saturation = 0.0f, brightness = 0.0f, contrast = 0.0f, text = 0.0f;
    std::array<std::uint32_t, dotCount> dots {}; // FL's colour dots (Dot order); 0 = the theme's own colour
    std::uint32_t accent = defaultAccent;
    std::uint32_t background = defaultBackground;
    std::uint32_t workspace = 0;     // the workspace backdrop's colour; 0 = automatic (the theme's deepest surface)
    std::string image;               // workspace image file (empty = none); a relative path is inside the Backgrounds
                                     // folder next to LYNX.exe (themeui::backgroundsFolder, issue #12)
    ImageFit fit = ImageFit::fill;
    float imageOpacity = 1.0f;       // 0..1
    float dim = 0.0f;                // 0..1: darkens the image towards the backdrop colour
    std::string startupSound;        // audio file played at start instead of LYNX's own sound (empty = LYNX's; issue #12)

    bool hasAdjustments() const noexcept { return hue != 0.0f || saturation != 0.0f || brightness != 0.0f || contrast != 0.0f || text != 0.0f; }
    bool hasDots() const noexcept { return std::any_of (dots.begin(), dots.end(), [] (std::uint32_t c) { return c != 0; }); }
    bool isDefaultLook() const noexcept
    {
        return accent == defaultAccent && background == defaultBackground && workspace == 0 && image.empty() && ! hasAdjustments() && ! hasDots();
    }
    bool operator== (const Spec&) const = default;
};

// The built-in themes ("LYNX" first = the default look).
const std::vector<Spec>& builtInThemes();

// The workspace backdrop colour of a theme (its own, or the palette's bg0).
std::uint32_t workspaceColour (const Spec& spec);

// --- Settings › Theme › Adjustments and colour dots (round 190jp, design190ia FW-20) ------------------------------------
// The theme's Adjustments over a palette: Hue turns the surfaces' and the text's hue; Saturation tints the grey surfaces
// (or greys them); Brightness lifts or lowers the surfaces; Contrast spreads them apart from (or towards) the raised
// surface bg3; Text moves the text away from (or towards) the panels. The accent family stays the user's colour. With no
// adjustment the palette is returned as it is (bit-identical).
Palette withAdjustments (Palette palette, const Spec& spec);
// The dots that are palette tokens (highlight = accentHover, mute = textDisabled, text = textPrimary, selection =
// selectionFill at its own alpha); the others (option, steps, meters, waves) are drawing colours ui/ThemeSettings sets.
Palette withDots (Palette palette, const Spec& spec);
// derivePalette, the Adjustments, the dots: the palette a theme draws with.
Palette adjustedPalette (const Spec& spec);

// --- colour maths ------------------------------------------------------------------------------------------------
double relativeLuminance (std::uint32_t argb);              // WCAG 2 (0..1)
double contrastRatio (std::uint32_t a, std::uint32_t b);    // WCAG 2 (1..21)
std::uint32_t readableTextOn (std::uint32_t background);    // white or near-black, whichever reads better
std::uint32_t mix (std::uint32_t a, std::uint32_t b, double amount); // a → b by amount (0..1), opaque
std::string toHex (std::uint32_t argb);                     // "#RRGGBB"
bool parseHex (const std::string& text, std::uint32_t& argb); // "#RGB", "RRGGBB", "#RRGGBB", "0xRRGGBB" (opaque)
} // namespace lynx::themes
