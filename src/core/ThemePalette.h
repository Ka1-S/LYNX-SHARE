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
    // the 1 px outline of controls whose fill alone does not read on the panels (secondary buttons, combo boxes, tick
    // boxes, text fields): at least partContrastFloor against bg4, the highest surface they sit on (round 190je, design190ia
    // G1 / FW idea 6)
    std::uint32_t outline;

    bool operator== (const Palette&) const = default;
};

// --- contrast floors (round 190je, design190ia G1; COMMUNITY.md: low contrast is the first complaint about FL, Live,
// BandLab and FL Studio Mobile alike). WCAG 2: text >= 4.5:1, UI parts (outlines, icons) >= 3:1. Disabled text is held
// at 3:1 too: still readable, clearly dimmer than the rest. Measured against bg3, the surface of menus, toolbars and
// dialogs; derivePalette lifts a user theme's tokens to them.
constexpr double textContrastFloor = 4.5;
constexpr double partContrastFloor = 3.0;
constexpr double disabledContrastFloor = 3.0;
// `colour` moved away from `background` (towards white on a dark one, black on a light one) just far enough to reach
// `ratio`; unchanged when it already does.
std::uint32_t withContrast (std::uint32_t colour, std::uint32_t background, double ratio);

// --- one state language for every button kind (round 190je, design190ia G1: FM-2, FM idea 5, FW idea 5) -------------
// FL 2024: "toolbar buttons show if they're pressed when hovered": hovering a toggle previews its "on" look. One rule
// for every kind, so the rack, the mixer and the toolbars read alike:
//   idle      secondary: bg4 + outline; flat: nothing;
//   hover     momentary: bg5 (+ outline, or borderStrong on a flat one), content textPrimary;
//             toggle that is off: its "on" look at partial strength (accentMuted 70 %, accent edge 50 %, accentHover icon);
//   pressed   bg0 (a toggle: accentMuted, accent edge); an "on" button: accentPressed;
//   on        the accent (accentHover under the mouse), content onAccent;
//   disabled  the idle fill and edge at half alpha, content textDisabled, no hover.
enum class ButtonKind
{
    secondary,     // a filled text / icon button (TextButton, a non-flat IconButton, a combo box)
    flatMomentary, // a toolbar icon that acts once (save, undo, a menu)
    flatToggle     // a toolbar icon that switches something on and off (metronome, panels, snap)
};
struct ButtonLook
{
    std::uint32_t fill, edge, content; // ARGB; alpha 0 = not drawn

    bool operator== (const ButtonLook&) const = default;
};
// `hover` 0..1 (IconButton fades it over 120 ms); `on`: toggled on (or a primary button).
ButtonLook buttonLook (const Palette& palette, ButtonKind kind, bool on, double hover, bool down, bool enabled);

// --- Settings › Theme › Grid line intensity (round 190je, design190ia FM-18; Live 12's "Grid Line Intensity") -------
// The bar, beat and step lines pushed away from (or towards) the field they sit on (rowWhite) by `intensity`; 1 = the
// theme's own lines, unchanged.
constexpr double gridIntensityMin = 0.6, gridIntensityMax = 1.6;
Palette withGridIntensity (Palette palette, double intensity);

// --- Settings › Theme › Menu style (round 190je, design190ia FW-16) ----------------------------------------------------
// LYNX's dark menus (the default), or FL Studio's classic light grey ones (measured in FL Studio 2026, ref/fl-menu-file.png).
enum class MenuStyle
{
    lynxDark,
    flClassic
};
const char* menuStyleId (MenuStyle style); // "lynx", "flClassic"
MenuStyle menuStyleFromId (const std::string& id);
struct MenuColours
{
    std::uint32_t background, border, text, shortcut, disabled, band, bandText, hover, hoverText, separator, check;
};
MenuColours menuColours (const Palette& palette, MenuStyle style);

// --- long menus in columns (round 190je, design190ia FW-13) ------------------------------------------------------------
// FL splits its long context menus into columns (the Channel rack menu: 2 columns, 305 px). A menu without column
// breaks that is taller than `maxHeight` (LYNX: 70 % of the screen) breaks once, where the two columns come out most
// even: before a section header, else after a separator, else nowhere (-1; JUCE's own split takes over when even two
// columns don't fit). Returns the row after which the second column starts.
struct MenuRow
{
    int height = 0;
    bool separator = false, header = false;
};
int tallMenuBreak (const std::vector<MenuRow>& rows, int maxHeight);

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
// things, options, lit steps, meters, waveforms and the text, each the theme's own unless set (0). Issue #238 (round 190od,
// LYNX's own dot; FL's theme files carry the same role as their channel / pattern / Playlist track default colours,
// "channels.colors." / "pattern.defaultcolor" / "forms.eventeditform.playlist.track.background"): Tracks = the colour
// drawn for Playlist tracks, mixer tracks and channels that have no colour of their own (FL's grey #485156 unless set).
// The project keeps "no colour" (0): a theme changes the look, never the project's data.
enum class Dot : int { selection = 0, highlight, mute, option, steps, meters, waves, text, tracks };
constexpr int dotCount = 9;
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
    float gridIntensity = 1.0f;      // Grid line intensity (round 190je): gridIntensityMin..Max, 1 = the theme's lines
    MenuStyle menuStyle = MenuStyle::lynxDark; // Menu style (round 190je)
    // FL's Theme › Options › "Audio and automation clips use note colors" (round 190od, issue #239; FL's ClipsOverrideBox,
    // .flstheme OverrideClips, hint "Use the note color list to find random colors for new clips"): a new audio clip
    // channel or automation clip takes a random colour of the 16 note colours instead of the default grey
    bool clipsUseNoteColours = false;

    bool hasAdjustments() const noexcept { return hue != 0.0f || saturation != 0.0f || brightness != 0.0f || contrast != 0.0f || text != 0.0f; }
    bool hasDots() const noexcept { return std::any_of (dots.begin(), dots.end(), [] (std::uint32_t c) { return c != 0; }); }
    bool isDefaultLook() const noexcept
    {
        return accent == defaultAccent && background == defaultBackground && workspace == 0 && image.empty() && ! hasAdjustments() && ! hasDots()
               && ! clipsUseNoteColours;
    }
    bool operator== (const Spec&) const = default;
};

// The palette a theme draws with: derivePalette, then its grid line intensity.
Palette paletteFor (const Spec& spec);

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
