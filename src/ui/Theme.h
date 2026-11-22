#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ThemePalette.h"

#include <cstdint>

namespace lynx
{
enum class ClipKind : std::uint8_t; // (core/Song.h)
}

// LYNX design tokens: the single source of colours, type sizes, spacing and radii for the whole UI,
// plus the LookAndFeel that draws every stock JUCE widget with them.
//
// Identity: dark, flat, high contrast, with one red accent used sparingly (selection, active
// states, playhead/record, primary actions). Surfaces get lighter as they rise ("elevation").
namespace lynx::theme
{
// --- colours -----------------------------------------------------------------------------------
// (aliases repeat the hex value: juce::Colour is not constexpr, and copying another inline
// variable during static initialisation is not reliably ordered across translation units)
namespace colour
{
// backgrounds by elevation (0 = deepest)
inline juce::Colour bg0 { 0xff0e0f12 };   // editor canvases, insets, meter wells
inline juce::Colour bg1 { 0xff15161a };   // app background, menu bar, status bar
inline juce::Colour bg2 { 0xff1b1d22 };   // panels (browser, rack, mixer strips)
inline juce::Colour bg3 { 0xff23262c };   // raised surfaces: toolbar, headers, popups
inline juce::Colour bg4 { 0xff2c2f37 };   // controls at rest
inline juce::Colour bg5 { 0xff363a43 };   // controls hovered
inline juce::Colour bg6 { 0xff434852 };   // controls pressed / neutral "on"

inline juce::Colour border { 0xff2a2d34 };       // hairlines between areas
inline juce::Colour borderStrong { 0xff3b3f48 }; // control outlines
inline juce::Colour outline { 0xff747983 };      // outlines that must read at 3:1 on the panels, bg4 included (round 190je: buttons, fields)

inline juce::Colour textPrimary { 0xffeceef2 };
inline juce::Colour textSecondary { 0xffa1a6b0 };
inline juce::Colour textDisabled { 0xff767b85 }; // round 190je: 3.6:1 on bg3 (was #60656F, 2.6:1)

// the red accent
inline juce::Colour accent { 0xffe5383b };
inline juce::Colour accentHover { 0xfff2545a };
inline juce::Colour accentPressed { 0xffc12a30 };
inline juce::Colour accentMuted { 0xff5c1f24 };  // tinted backgrounds (armed, selected rows)
inline juce::Colour onAccent { 0xffffffff };     // text/icons drawn on the accent

// semantic
inline juce::Colour selection { 0xffe5383b }; // = accent: selected notes/clips outline
inline juce::Colour selectionFill { 0x2ee5383b };               // selection rectangles
inline juce::Colour playhead { 0xffe5383b }; // = accent
inline juce::Colour record { 0xffff3b3f };
inline juce::Colour automation { 0xff4cc9c0 };                   // automation curves and clips
inline juce::Colour info { 0xff6ea8fe };                         // secondary values (pan, links)
inline juce::Colour ledOn { 0xff3ddc84 };                        // "active/unmuted" LEDs
inline juce::Colour ledOff { 0xff3a3e47 };
inline juce::Colour warning { 0xfff5b942 };                      // markers, missing items
inline juce::Colour meterLow { 0xff3ddc84 };
inline juce::Colour meterMid { 0xfff5c542 };
inline juce::Colour meterClip { 0xffff3b3f };

// step sequencer / editors
inline juce::Colour knobArc { 0xffc5c9d1 };     // rotary value arcs at rest (accent while touched)
inline juce::Colour knobTrack { 0xff444955 };   // the unlit part of a knob's ring (reads on every panel surface)
inline juce::Colour stepOff { 0xff2a2d34 };
inline juce::Colour stepOffAlt { 0xff4c3f46 };   // every other group of 4 steps (a dull reddish grey, round 170a2)
inline juce::Colour stepOn { 0xffe5383b }; // = accent
inline juce::Colour stepNote { 0xffe8a3a6 };     // a step that only has a Piano Roll note
inline juce::Colour gridBar { 0xff3a3e47 };      // bar lines
inline juce::Colour gridBeat { 0xff262930 };     // beat lines
inline juce::Colour gridStep { 0xff1d1f24 };     // step lines
inline juce::Colour keyWhite { 0xffd9dbe0 };
inline juce::Colour keyBlack { 0xff17181c };
inline juce::Colour rowWhite { 0xff1c1e23 };     // Piano Roll rows of white keys
inline juce::Colour rowBlack { 0xff16171b };     // rows of black keys
inline juce::Colour noteSoft { 0xffb3545a };     // note colour at velocity 0
inline juce::Colour noteHard { 0xffff6b6e };     // note colour at velocity 1
inline juce::Colour noteMuted { 0xff4a4e57 };
inline juce::Colour audioClip { 0xff5a87b8 };
// issue #238 (round 190od): Playlist tracks, mixer tracks and channels without a colour of their own (FL's grey; Settings ›
// Theme › Tracks)
inline juce::Colour defaultTrack { 0xff485156 };
} // namespace colour

// User themes (round 162d): the tokens above hold LYNX's built-in look until a theme replaces them (ui/ThemeSettings.h
// applies one live). Code reads the tokens when it paints, so never copy one into a member or a static.
void setPalette (const themes::Palette& palette); // the tokens only (ThemeSettings also refreshes the windows)
themes::Palette currentPalette();

// --- one state language for every button kind (round 190je, design190ia G1; the rule: core/ThemePalette.h buttonLook) ---
// idle / hover (0..1, faded by IconButton) / pressed / on / disabled for a secondary button, a flat momentary icon or a
// flat switch (whose hover previews its "on" look, FL 2024). Text buttons, icon buttons, combo boxes and tick boxes use it.
struct ButtonColours
{
    juce::Colour fill, edge, content; // transparent: not drawn
};
ButtonColours buttonColours (themes::ButtonKind kind, bool on, float hover, bool down, bool enabled);

// --- typography ----------------------------------------------------------------------------------
// Round 190lx (DL-59, capture 190lx B1 live-led-send.png / live-led-device-knob.png): Live's automation LED on a knob,
// a red (255,77,71) 4 px dot with a dark ring (about 6 px in all) at the knob's top-left, just outside its ring.
inline void drawLiveKnobAutomationLed (juce::Graphics& g, juce::Rectangle<float> knob)
{
    const auto r = juce::jmin (knob.getWidth(), knob.getHeight()) * 0.5f;
    const juce::Point<float> at { knob.getCentreX() - 0.82f * r, knob.getCentreY() - 0.88f * r };
    g.setColour (juce::Colour (0xff060606));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (at));
    g.setColour (juce::Colour (0xffff4d47));
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (at));
}

namespace type
{
// The text roles at 100 %. Settings › General › Text size (issue #189, round 190ir) scales them per role at start-up
// (setTextSize): the sizes below are what the code reads, so they are variables, set once before any window exists.
namespace base
{
inline constexpr float caption = 11.0f, small = 12.0f, body = 13.0f, title = 15.0f, display = 20.0f;
}
inline float caption = base::caption;  // grid numbers, small captions
inline float small = base::small;      // secondary labels, status bar, tooltips
inline float body = base::body;        // default control text
inline float title = base::title;      // panel titles
inline float display = base::display;  // the transport position
} // namespace type

// Settings › General › Text size (issue #189): the roles scaled per role (core/TextSize.h), saved in the profile
// (ui-text-size.txt) and applied at start-up, as the interface font is (Labels and layouts take their sizes when they
// are made).
void setTextSize (int percent); // sets type::caption..display
int textSize();                 // the percent in use
int savedTextSize();            // 100 when none was saved
void saveTextSize (int percent);
// Settings › General › Bold text (round 190jp): textsize::Bold, set once at start-up (ui-bold-text.txt); theme::font
// then gives the bold face for the chosen roles.
void setBoldText (int setting);
int boldText();
int savedBoldText(); // 0 (off) when none was saved
void saveBoldText (int setting);

// The UI font at `size` (plain or bold). Round 190gr (U1): the fonts come from a small cache, so a paint no longer makes a
// new juce::Font whose typeface JUCE then looks up by name (its typeface cache compares the family and style strings),
// and the fonts a paint sets share one JUCE font object, so JUCE's glyph and context checks see the same font at once.
// The cache empties when the interface font changes (clearFontCache) and with the image caches (ui/ImageCaches.h).
// LYNX_PAINT_CACHE=0 (diagnostics) makes a new font on every call, as before.
juce::Font font (float size, bool bold = false);
void clearFontCache();


// The width a column of labels needs: `minWidth` when every text fits in it (the English layout), else the widest
// text (+ the label borders), up to `maxWidth` (round 165o: translated labels are often longer).
int labelColumnWidth (std::initializer_list<const juce::Label*> labels, int minWidth, int maxWidth);

// A menu item's text with FL's accelerator letter `key` (core/MenuMnemonic.h, round 170a): the letter is underlined in
// the menu and typing it picks the item, e.g. theme::mnemonic (tr ("Delete..."), 'D').
juce::String mnemonic (const juce::String& text, char key);
// Japanese / Korean / Chinese: the installed UI font family LYNX uses for that language (empty: none installed, or a
// language that needs no font of its own — see needsOwnFont).
juce::String uiFontForLanguage (const juce::String& languageId);
bool needsOwnFont (const juce::String& languageId);

// Settings › General › Interface font (round 179a). The platform's default family (empty on macOS: JUCE's own pick), the
// installed families for the list, and the choice saved in the profile (empty: the default).
juce::String defaultUiFont();
juce::StringArray installedUiFonts();
juce::String savedUiFont();
void saveUiFont (const juce::String& family);

// Same for a text button (its padding included).
int buttonWidth (const juce::TextButton& button, int minWidth, int maxWidth);

// Round 190oo (design review 2, WG-13 / WG-31): for boxes laid out for the English texts (fixed widths, not sized yet):
// how much wider than its English text the translation of any of `english` is in `font` (the largest difference; 0 in
// English and when no translation is longer). A fixed English width + this keeps the English layout to the pixel and
// gives a long translation (de "Übernehmen", ru "Насыщенность") the room it needs.
int translationExtra (const juce::Font& font, std::initializer_list<const char*> english);

// --- menus -------------------------------------------------------------------------------------------
// Options › General › Menu density (round 163): compact rows close to native Windows / FL menus (the default),
// or the roomier rows of LYNX 0.1.17 and older.
namespace menus
{
void setCompact (bool compact);
bool compact();
int itemHeight();    // a normal row
int headerHeight();  // a section header: FL's full-width band (round 190je: 17 px compact, 22 comfortable)
int separatorHeight();
// The menu text, one step larger than the body text (round 190je, design190ia FW-15: FL's cap 8 px, LYNX's was 7): 14 at
// 100 %, scaled by Settings › General › Text size like the body role. The shortcut column uses type::small, italic as FL's.
float textSize();
// Settings › Theme › Menu style (round 190je, FW-16): LYNX's dark menus (the default) or FL's classic light grey ones.
void setStyle (themes::MenuStyle style);
themes::MenuStyle style();
struct Colours
{
    juce::Colour background, border, text, shortcut, disabled, band, bandText, hover, hoverText, separator, check;
};
Colours colours(); // the palette in use, in the style in use
} // namespace menus

juce::Font monoFont (float size); // (cached as font() is)

// --- spacing and radii ---------------------------------------------------------------------------
namespace space
{
inline constexpr int xs = 2, sm = 4, md = 8, lg = 12, xl = 16;
}

namespace radius
{
inline constexpr float sm = 3.0f, md = 4.0f, lg = 6.0f;
}

// --- helpers -------------------------------------------------------------------------------------
inline juce::Colour stepOff (int step)
{
    return (step / 4) % 2 == 0 ? colour::stepOff : colour::stepOffAlt; // alternating groups of 4
}

inline juce::Colour muteLed (bool muted)
{
    return muted ? colour::ledOff : colour::ledOn;
}

// Piano Roll note colour by velocity (soft = dark rose, hard = bright red).
inline juce::Colour noteColour (float velocity)
{
    return colour::noteSoft.interpolatedWith (colour::noteHard, juce::jlimit (0.0f, 1.0f, velocity));
}

// Note colour groups (REA round 153): group 0 is LYNX's note colour, groups 1..15 LYNX's own hue wheel; the velocity
// sets the brightness, as for group 0.
inline juce::Colour noteGroupHue (int group)
{
    if (group <= 0)
        return colour::noteHard;
    return juce::Colour::fromHSV (std::fmod (0.02f + (float) group / 16.0f, 1.0f), 0.62f, 0.95f, 1.0f);
}

inline juce::Colour noteGroupColour (int group, float velocity)
{
    if (group <= 0)
        return noteColour (velocity);
    const auto hue = noteGroupHue (group);
    return hue.withMultipliedBrightness (0.68f + 0.32f * juce::jlimit (0.0f, 1.0f, velocity));
}

// A user/channel colour toned down to sit on the dark surfaces without shouting.
inline juce::Colour tame (juce::Colour c)
{
    return c.withMultipliedSaturation (0.8f).withBrightness (juce::jmin (c.getBrightness(), 0.72f));
}

// --- the Live look of the Clip-launch layout (round 190ig; tokens measured by round 190ia in Ableton Live 12.4's Dark
// theme at 100 %, /mnt/e/lynx-work/design190ia/findings/cliplaunch.md §0). Used only while the Clip-launch layout shows,
// so the Pattern layout never changes. Fixed values (the reference look), not user-theme tokens.
namespace livelook
{
inline const juce::Colour window { 0xff2a2a2a };     // control bar, around the views
inline const juce::Colour surface { 0xff363636 };    // slots, strips, scene cells
inline const juce::Colour raised { 0xff464646 };     // selected track column / scene row, title bars
inline const juce::Colour separator { 0xff242424 };  // 2 px between columns, 1 px between rows
inline const juce::Colour well { 0xff1e1e1e };       // stop squares, record circles, fields
inline const juce::Colour mainHeader { 0xff3c3c3c }; // the Main column's header
inline const juce::Colour text { 0xffb5b5b5 };       // text on the greys
inline const juce::Colour ink { 0xff070707 };        // text on a light selection
inline const juce::Colour selectionFocused { 0xffb0ddeb }; // a selected scene; a focused slot's edge
inline const juce::Colour selectionFocusedFill { 0xff95bbc7 }; // a focused selected slot's launch zone
inline const juce::Colour selectionUnfocused { 0xff637e86 };
inline const juce::Colour playing { 0xff00d38d };    // a playing clip's launch triangle
inline const juce::Colour queuedOn { 0xff008a5c };   // a queued launch's blink, lit phase
inline const juce::Colour queuedOff { 0xff002217 };  // and its dark phase
inline const juce::Colour stateBox { 0xff181818 };   // the launch zone of a playing / queued / recording clip
inline const juce::Colour arm { 0xffff5559 };
inline const juce::Colour on { 0xffffad56 };         // Live's "on" orange: activators, power, Warp, Detail toggles
inline const juce::Colour meterWell { 0xff181818 };  // the wells of the level meters
inline const juce::Colour knobRing { 0xff111111 };   // a knob's open ring and its needle (#181818)
inline const juce::Colour focusOutline { 0xff757575 }; // the focused view's 2 px outline
// Round 190jn: whether the Clip-launch layout shows Live's look now (the Clip-launch layout without View › "Classic LYNX
// look", round 190ig's switch). Every Live-look painter (grid, Session mixer, Arrangement, Clip / Device View, Info View,
// status bar) asks this one switch, so the Pattern and Simple layouts never change.
bool active() noexcept;
// round gaps190kf: the Clip View's Sample Editor (Live 12.4 Dark, 100 %: live190gu/warp/live-clipview-beats-defaults-full.png)
inline const juce::Colour clipMarker { 0xff00bfaf }; // the clip's Start / End markers
inline const juce::Colour braceOff { 0xff919191 };   // the loop brace while Loop is off
inline const juce::Colour braceOn { 0xff808080 };    // its ">" / "<" over the teal bar, and the Start marker while Loop is on
} // namespace livelook

// Live derives every shade of a clip from its colour by multiplying the RGB channels (round 190ia: name ×0.24, idle
// launch triangle ×0.40, launch-zone divider ×0.66, waveform ×0.15, grid in a clip ×0.88).
inline juce::Colour liveShade (juce::Colour c, float k)
{
    const auto m = [k] (juce::uint8 v) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt ((float) v * k)); };
    return juce::Colour (m (c.getRed()), m (c.getGreen()), m (c.getBlue()), c.getAlpha());
}

// --- one colour system for clips (round 190ig, design190ia PLAN §2) ---------------------------------------------------
// A clip's colours from its own colour (FL: its channel's or pattern's; Live: its slot's, else its track's) and the
// colour of the track it sits on, in the look of one layout: FL Studio's tint (Pattern layout), Live's pastel blocks
// (Clip-launch) or BandLab's track hue (Simple). Each layout's painter picks its style; the slices that restyle the other
// layouts only choose theirs here.
enum class ClipStyle { flTint, livePastel, bandlabHue };

struct ClipLook
{
    juce::Colour headerTop, headerBottom; // the header band, a vertical gradient (one colour: no gradient)
    juce::Colour edge;                    // 1 px rim of the header band
    juce::Colour body;                    // the body, filled at bodyAlpha over the lane (FL: the grid shows through)
    float bodyAlpha = 1.0f;
    juce::Colour content;                 // waveform, automation curve, note marks
    juce::Colour contentFill;             // under an automation curve (transparent: none)
    juce::Colour compactContent;          // FL: the waveform when the whole clip is one band (a short track)
    juce::Colour ink;                     // the name and kind glyph on the header
};

// FL (measured in FL Studio 2026 on "Ookay - Thief", round 190ig, fl-playlist-zoom-audioclips.png / fl-playlist.png):
// header = the clip colour, a gradient from colour − 13 to colour − 28 per channel, its rim colour × 0.66; body = the
// TRACK's colour at 50 % over the lane (the Riser Noise clip: an orange header on the yellow RISER SWEEP track has a
// yellow body); waveform = colour + 72 per channel, automation curve = colour + 84 with colour + 84 at 13 % under it;
// name = colour + 124; on a short track the clip is one header-coloured band with its waveform in the clip colour.
// `trackColour` 0 / `clipColour` 0 = FL's default grey #485156 (a channel or track saved without a colour).
ClipLook clipBody (juce::Colour clipColour, juce::Colour trackColour, ClipKind kind, ClipStyle style);

// --- FL's colour-filled buttons and tinted names (round 190io, design190ia FM-10 / FM-30 / FM-31) ------------------------
// WCAG contrast ratio of two colours (1 … 21).
float contrast (juce::Colour a, juce::Colour b);

// Live's marks on a clip or track colour (round 190oj, design190oj CS-4/5/6): the colour × k (Live: name ×0.24, waveform
// ×0.15; 7–10:1 on Live's pastels) when that reaches `ratio` on the fill, else `light` — on LYNX's and FL's darker colours
// Live's dark ink read 1.5–3:1. Cached per colour (message thread; paint calls it per clip).
juce::Colour liveMarkOn (juce::Colour fill, float k, float ratio, juce::Colour light);

// A user colour as FL fills a button or a header band with it: the colour itself (FL keeps its saturation: Synth Bass
// #0E4498, Grv Kick #8A211A measured in fl-channelrack.png), its brightness held in [lo, hi] so white text on it keeps
// its contrast (LYNX: FL's lighter fills such as Titey Tom #B65227 read 4.2:1 with white).
juce::Colour colourFill (juce::Colour c, float lo, float hi);

// FL's light tint of a track colour for its name ("KICK" red-pink on the red group): saturation × 0.45, brightness 0.95;
// `fallback` when the tint reads below 4.5:1 on `background`.
juce::Colour lightTint (juce::Colour c, juce::Colour background, juce::Colour fallback);

// FL's raised step keycap (round 190io, FM-11) in `fill`; `lit`: a lit step's light edges and slot line. From a cached tile.
void drawKeycap (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, bool lit);
// Eight step keycaps in a row (the Channel rack's idle steps, two groups of four: `a` then `b`) at `pitch` from `firstCell`,
// one tile per row instead of eight; `ghost`: the faint outlined steps past a pattern's end (round 190io).
void drawStepStrip (juce::Graphics& g, juce::Rectangle<int> firstCell, int pitch, juce::Colour a, juce::Colour b, bool ghost);
// Round 190mh (perf): drawStepStrip's strip where the frame shows the opaque `background` under it (a Channel rack row):
// on JUCE's software renderer the strip's translucent tile is composed over `background` once, into an opaque image
// that is copied afterwards (the same pixel blends: the same pixels); elsewhere drawStepStrip as before.
void drawStepStripOver (juce::Graphics& g, juce::Rectangle<int> firstCell, int pitch, juce::Colour a, juce::Colour b, bool ghost, juce::Colour background);
// A plate filled top-down from `top` to `bottom` (one colour: equal), rounded `corner` (only on top with `squareBottom`):
// FL's colour-filled channel buttons and Mixer header bands (round 190io). From a cached tile.
void fillPlate (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour top, juce::Colour bottom, bool squareBottom = false);

// FL's middle ellipsis (core/MiddleEllipsis.h): `text` as it fits in `width` with `font`, start + ".." + end when it is
// too long. Cached by text, font height and width, so a paint measures each name once.
juce::String fitMiddle (const juce::String& text, const juce::Font& font, float width);

// Round 190pb: the palette's change count (setPalette), for keys of pictures drawn in the theme's colours.
std::uint64_t paletteStampNow() noexcept;

// Upper case for small caption headers, including accented Latin letters (ã -> Ã), which the C
// runtime's towupper leaves alone on Windows.
inline juce::String caps (const juce::String& text)
{
    juce::String out;
    for (auto c : text)
    {
        if ((c >= 0xe0 && c <= 0xfe && c != 0xf7))
            c -= 0x20;
        else
            c = juce::CharacterFunctions::toUpperCase (c);
        out += juce::String::charToString (c);
    }
    return out;
}

// g.setColour (colour) + g.fillRoundedRectangle, faster where the frame is drawn on the CPU (Linux): nothing outside the
// clip, a plain rectangle when the clip misses the corners, the shape (or a big one's corners) from a cached sprite.
void fillRoundRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour colour);
// The launch triangle of a clip slot (a triangle pointing right, filling `a`, in `colour`; g's colour is `colour` after),
// from cached tiles (round 190gr, U2): Linux's software renderer within one level per channel of drawing it straight
// (--selftest paint_caches); Direct2D (where each triangle drawn straight is a new path geometry) see paintCacheSelfTest.
void fillLaunchTriangle (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour colour);
// The frozen mark (round 190ay, Freeze): a small tag with a six-armed snowflake, in the shape of the stand-in tag. `tag`
// is the tag's fill (the info colour while the frozen audio is up to date, the warning colour when it needs attention).
void drawFrozenTag (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tag, juce::Colour flake);

// FL's number wheel ("digiwheel": channel RANGE / TRACK, the rack's insert box) in the LYNX skin: a dark inset display,
// red only while hovered or dragged. Draws the box (with FL's small up / down marks at its right when `marks`); returns
// the colour for its value text, which goes in box.withTrimmedRight (digitMarksWidth) when marked (round 168s).
constexpr float digitMarksWidth = 9.0f;
juce::Colour drawDigitDisplay (juce::Graphics& g, juce::Rectangle<float> box, bool hot, bool dimmed = false, bool marks = false);

// Draws a rotary value arc in the LYNX style (used by custom-painted knobs too).
void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value01, bool bipolar, juce::Colour arc, bool hover = false);

// --- legacy names (kept so code written against the old palette keeps compiling) ---------------
inline juce::Colour window { 0xff15161a }; // = colour::bg1
inline juce::Colour panel { 0xff1b1d22 }; // = colour::bg2
inline juce::Colour toolbar { 0xff23262c }; // = colour::bg3
inline juce::Colour channelButton { 0xff2c2f37 }; // = colour::bg4
inline juce::Colour channelButtonSelected { 0xff434852 }; // = colour::bg6
inline juce::Colour knobBackground { 0xff0e0f12 }; // = colour::bg0
inline juce::Colour stepOn { 0xffe5383b }; // = colour::stepOn
inline juce::Colour stepOnOffGrid { 0xffe8a3a6 }; // = colour::stepNote
inline juce::Colour accentOrange { 0xffe5383b }; // = colour::accent
inline juce::Colour accentGreen { 0xff3ddc84 }; // = colour::ledOn
inline juce::Colour accentBlue { 0xff6ea8fe }; // = colour::info
inline juce::Colour playhead { 0xffe5383b }; // = colour::playhead
inline juce::Colour text { 0xffeceef2 }; // = colour::textPrimary

// --- fonts of the system (issue #142) ------------------------------------------------------------------
// The folders JUCE scans for fonts, from the ones fontconfig knows (Linux): only absolute, existing folders JUCE's
// list can carry, none of them inside another one of the list (JUCE reads each with everything below it).
juce::StringArray fontFoldersToScan (const juce::StringArray& fontconfigFolders, const std::function<bool (const juce::String&)>& exists);
// --selftest "fonts=": every font LYNX draws with has a typeface and glyphs, and a font without one does not crash.
juce::String fontSelfTest();
// --selftest "paint_caches=" (round 190gr): the font cache and the shape tiles against shapes drawn straight.
juce::String paintCacheSelfTest();
// Round 190ms: what JUCE fix 45 rests on (edge tables over a path's own rows; RGB rows into ARGB pixels): "OK" or an error.
juce::String softwareRendererSelfTest();

// --- LookAndFeel ---------------------------------------------------------------------------------
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    // re-reads the colour tokens into the stock widgets' colours (after a theme change)
    void refreshColours();

    // Japanese, Korean and Chinese: the OS's UI font for that script becomes the default sans-serif (Segoe UI and the
    // other Latin UI fonts have no CJK glyphs; the font fallback would otherwise pick a CJK face per run, which may be
    // a Chinese face for Japanese text). Returns the chosen family, or empty when the default is kept.
    juce::String applyLanguageFont (const juce::String& languageId);

    // At start-up: the font saved in Settings (runOverride: --uifont, for this run only) when it is installed, else
    // the default for this language (applyLanguageFont). Returns the family chosen, empty when the default stays.
    juce::String applyInterfaceFont (const juce::String& languageId, const juce::String& runOverride = {});

    // Every font gets a typeface (issue #142): one whose family is not installed draws with LYNX's own font.
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // property set on a Button / TabBarButton to draw one of lynx::Icon next to (or instead of) its text
    static constexpr const char* iconProperty = "lynxIcon";

    // buttons
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled, bool highlighted, bool down) override;

    // combo boxes and menus
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 2; } // (round 190je: 4 → 2, FL's rows reach the frame)
    void drawPopupMenuColumnSeparatorWithOptions (juce::Graphics&, const juce::Rectangle<int>&, const juce::PopupMenu::Options&) override;
    int getPopupMenuColumnSeparatorWidthWithOptions (const juce::PopupMenu::Options&) override;
    // keeps a submenu from opening over its own multi-column parent menu (round 162d: JUCE then misses the click on
    // the submenu's items — the mixer track menu's "Change color" did nothing, issue #9)
    void preparePopupMenuWindow (juce::Component& newWindow) override;
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar, juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex, const juce::String& itemText, bool isMouseOverItem,
                          bool isMenuOpen, bool isMouseOverBar, juce::MenuBarComponent&) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    int getMenuBarItemWidth (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

    // sliders
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    // scrollbars
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical, int thumbStartPosition,
                        int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 10; }
    bool areScrollbarButtonsVisible() override { return false; }

    // tabs
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton (juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

    // text
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // tooltips, alerts, windows
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    // JUCE's message boxes (showMessageBoxAsync, showOkCancelBox, showAsync...) on LYNX's one dialog frame (round 190oo, WG-6)
    juce::AlertWindow* createAlertWindow (const juce::String& title, const juce::String& message, const juce::String& button1,
                                          const juce::String& button2, const juce::String& button3, juce::MessageBoxIconType iconType,
                                          int numButtons, juce::Component* associatedComponent) override;
    int getAlertWindowButtonHeight() override { return 30; }
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;
    juce::Button* createDocumentWindowButton (int buttonType) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String& textToShow) override;
    void drawGroupComponentOutline (juce::Graphics&, int w, int h, const juce::String& text, const juce::Justification&, juce::GroupComponent&) override;

    // file browser (the LYNX browser panel is a FileTreeComponent)
    void drawFileBrowserRow (juce::Graphics&, int width, int height, const juce::File& file, const juce::String& filename, juce::Image* icon,
                             const juce::String& fileSizeDescription, const juce::String& fileTimeDescription, bool isDirectory,
                             bool isItemSelected, int itemIndex, juce::DirectoryContentsDisplayComponent&) override;
    void drawTreeviewPlusMinusBox (juce::Graphics&, const juce::Rectangle<float>& area, juce::Colour backgroundColour, bool isOpen, bool isMouseOver) override;
};
} // namespace lynx::theme
