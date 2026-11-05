#pragma once

// The LYNX plugin-editor skin (REA round 122): one design kit shared by every built-in plugin editor — the
// display-first editors (ProEditors), the FL-arranged panels (PluginEditors), Facet EQ, the generator pages and
// LYNX Tune. It only changes how things are DRAWN: layouts, control placement and gestures stay with each editor.
//
// Identity: LYNX's dark surfaces and red brand mark, with one accent per plugin family (reverbs violet, delays blue,
// modulation teal, dynamics amber, saturation orange, voice rose, EQ / filters the LYNX red) so each editor is
// recognisable at a glance. Knobs carry a lit value arc with a soft glow, machined caps and a pointer; displays sit
// in inset wells; meters are lit gradients with peak holds; lit buttons glow. Everything is plain vector drawing
// (static layers — the canvas with its blooms and grain, the knobs — are rendered once into cached images), cheap
// enough for 30 Hz displays, and nothing animates unless something changed (see Animator).
//
// Round 126 ("much bolder"): aurora blooms of the accent behind every editor, grain, translucent glass sections
// with rim light, big 3D knob caps with a lit ring, a hot tip and an LED pointer (the bigger knobs light the panel
// around them as they are turned up), illuminated buttons, LED-ladder meters, white-hot curves and a short open
// animation. A "Reduced effects" setting turns the costly glows off.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>

#include "core/Song.h"
#include "ui/Theme.h"

namespace lynx::skin
{
// --- colours -------------------------------------------------------------------------------------
namespace colour
{
inline const juce::Colour crimson { 0xffe5383b }; // = theme accent (EQ, filters, LYNX's own default)
inline const juce::Colour violet { 0xffa48bff };  // reverbs
inline const juce::Colour azure { 0xff5aa2ff };   // delays / echoes
inline const juce::Colour teal { 0xff3fd0c2 };    // modulation
inline const juce::Colour amber { 0xfff7b33c };   // dynamics
inline const juce::Colour ember { 0xffff7a3d };   // saturation / distortion
inline const juce::Colour rose { 0xffff5c93 };    // voice / pitch
inline const juce::Colour lime { 0xff8fdc4a };    // stereo / utility

inline const juce::Colour canvasTop { 0xff1a1c22 };
inline const juce::Colour canvasBottom { 0xff0f1014 };
inline const juce::Colour wellTop { 0xff0a0b0e };
inline const juce::Colour wellBottom { 0xff121318 };
} // namespace colour

// a juce::Slider carrying this property (the accent as an int64 ARGB) is drawn with the skin by theme::LookAndFeel
inline constexpr const char* sliderProperty = "lynxSkinAccent";

// the accent of a built-in effect's family
juce::Colour accentFor (EffectKind kind);
// the accent of a generator page (synths: crimson; sampler-like: azure...)
juce::Colour accentForInstrument (const juce::String& pluginName);

// --- surfaces ------------------------------------------------------------------------------------
// the editor's canvas: a deep vertical gradient with a faint glow of the accent under the header
void canvas (juce::Graphics&, juce::Rectangle<float> area, juce::Colour accent);
// the header strip: raised gradient, top highlight, hairline and an accent line fading out from the left
void headerBar (juce::Graphics&, juce::Rectangle<float> area, juce::Colour accent);
// LYNX's mark (the app logo, cached at the size asked) followed by nothing: callers draw the name next to it
void brandMark (juce::Graphics&, juce::Rectangle<float> area);
// the plugin's name in the header (bold, with a short accent underline); returns the width used
float titleText (juce::Graphics&, juce::Rectangle<float> area, const juce::String& name, juce::Colour accent, bool underline = true);
// round 170k2: the name always fits its box: the style's size, then smaller (down to 10 px), then "Fruity" on a small
// line above the rest; an ellipsis only when even that cannot fit. titleMinWidth: the narrowest box that still shows
// the whole name (in the current style, for a header this high). lastTitleFit: what the last titleText did (shots).
float titleMinWidth (const juce::String& name, float height);
struct TitleFit
{
    float need = 0.0f, have = 0.0f, size = 0.0f; // the name's width at the style's size, the box's width, the size used
    int lines = 1;
    bool clipped = false;                         // drawn with an ellipsis
};
TitleFit lastTitleFit();
void resetTitleFit();
// one line of text in the current gradient (or image) fill, drawn as its glyph outlines; the empty outlines (spaces)
// are left out (round 170c: JUCE 8.0.4's CoreGraphics renderer took an empty outline as "no clip" and flooded the
// whole component with the gradient, Facet EQ's blank pink window on macOS, issue #11). A plain colour fill draws
// the text as usual.
void drawFilledText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Justification justification);
// --gfxprobe: does a gradient-filled text or an empty gradient-filled path stay inside its shape on this platform?
juce::String rendererProbeReport();
// a power LED with a halo when on
void powerLed (juce::Graphics&, juce::Point<float> centre, float diameter, bool on);
// a raised section (group of controls) and its title strip (caps, with an accent dot)
// `tint` (the accent) lights the glass from the top and colours its rim
void section (juce::Graphics&, juce::Rectangle<float> area, float corner, bool raised = false, juce::Colour tint = juce::Colours::transparentBlack);
void sectionTitle (juce::Graphics&, juce::Rectangle<float> strip, const juce::String& text, juce::Colour accent, float fontSize = 10.5f);
// an inset display well (graphs, analysers, read-outs); `glow` (the accent) lights a soft bloom from its bottom edge
void well (juce::Graphics&, juce::Rectangle<float> area, float corner, juce::Colour glow = juce::Colours::transparentBlack);
// the deep background of a display-first editor's body (round 126: aurora blooms of the accent; cached as an image)
void backdrop (juce::Graphics&, juce::Rectangle<float> area, juce::Colour accent);
// a soft bloom of light (radial, `alpha` at the centre fading to nothing at `radius`)
void bloom (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float alpha);
// a floating panel over a display (drop shadow, glassy fill, an accent edge)
void floatingPanel (juce::Graphics&, juce::Rectangle<float> area, juce::Colour edge, float corner = 10.0f);
// the bottom status bar of the display-first editors
void bottomBar (juce::Graphics&, juce::Rectangle<float> area);

// --- controls ------------------------------------------------------------------------------------
struct KnobLook
{
    float value = 0.0f;   // 0..1
    bool bipolar = false;
    juce::Colour accent = colour::crimson;
    float hover = 0.0f;   // 0..1 (animated by the owner, or 0 / 1)
    bool active = false;  // being dragged
    bool enabled = true;  // the plugin is on
    // round tp172c: the cap's own colour (a console's colour-coded knobs: red HF, green HMF...); transparent = the
    // theme's. Only the themed style reads it, through the theme's capTint hook (see capTintFor)
    juce::Colour capTint {};
};
void knob (juce::Graphics&, juce::Rectangle<float> bounds, const KnobLook&);
// round tp172c: the cap colour the current plugin theme gives the knob of this parameter (transparent = none)
juce::Colour capTintFor (const char* param);
// round tp172c: the current plugin theme's colour for an OFF toggle's label (transparent = the global one)
juce::Colour offLabelColour();

// a slider track (vertical or horizontal) with its lit part and a thumb
void slider (juce::Graphics&, juce::Rectangle<float> trackArea, float value01, bool vertical, bool bipolar, juce::Colour accent, bool hot,
             bool enabled, float thumbLength, float thumbThickness);
// a push button / option button; lit = on (glows in the accent)
void button (juce::Graphics&, juce::Rectangle<float> area, bool lit, bool hover, juce::Colour accent, float corner = theme::radius::md);
// a pill switch
void switchPill (juce::Graphics&, juce::Rectangle<float> box, bool on, juce::Colour accent, bool enabled);
// a menu / selector face (the value is drawn by the caller)
void selector (juce::Graphics&, juce::Rectangle<float> area, bool hover, float corner = theme::radius::lg);
// a vertical (or horizontal) level meter with a lit gradient (green → amber → red) and a peak hold, 0..1 each
void meter (juce::Graphics&, juce::Rectangle<float> area, float level01, float peak01, bool vertical = true);
// a meter lit in one colour (gain reduction): `fromTop` hangs from the top
void meterSolid (juce::Graphics&, juce::Rectangle<float> area, float level01, juce::Colour c, bool fromTop);
// a stroked curve with a soft glow under it (response curves, analysers)
void glowStroke (juce::Graphics&, const juce::Path&, juce::Colour, float thickness, float glow = 1.0f);
// a value read-out plate (boxed values)
void valuePlate (juce::Graphics&, juce::Rectangle<float> area, float corner);

// --- plugin style (round 126) ---------------------------------------------------------------------
// The look every built-in editor is drawn in (Settings > General > Plugin style): Glow (the default kit above),
// Neon, Hardware or Vivid (ui/PluginStyles.inc). Only the drawing changes; layouts and gestures never do.
//
// Round 170j adds "Themed": a plugin that has its own ART (ui/PluginArt.inc) is drawn in a theme of its own — an
// illustration in its background, its own palette, materials and knobs, evoking the mood of the plugin it is
// inspired by (Sakura's cherry blossoms, Toxic Biohazard's containment lab...). All of it is LYNX's own procedural
// vector drawing. A plugin without art falls back to its automatic style.
enum class Style { glow = 0, neon, hardware, vivid, themed };
inline constexpr int styleCount = 5;

// the per-plugin art of the Themed style (none: the plugin has no theme of its own)
enum class Art // ids: generators 1..127 (ui/PluginArtGeneratorIds.inc), effects 128..255 (ui/PluginArtEffectIds.inc)
{
    none = 0,
   #define LYNX_ART(id, value) id = value,
   #include "ui/PluginArtGeneratorIds.inc"
   #include "ui/PluginArtEffectIds.inc"
   #undef LYNX_ART
};
Art art();                                 // the art the kit draws with right now (Style::themed only)
Art artForEffect (EffectKind);
Art artForInstrument (InstrumentKind);
Style baseStyleOf (Art);                   // the style the art builds on for what it does not draw itself
juce::Colour artAccent (Art);              // the art's main accent (editors use it for their own drawing)
juce::Colour artAccent2 (Art);             // its second light
const char* artName (Art);                 // "Sakura"... (tests, snapshots)
// animated art (falling petals, glows): Settings > General; also off with Reduced effects or the OS's
// "show animations" switched off
bool artMotion();
void setArtMotion (bool);
bool osReducedMotion();
void kickArtLayers(); // the open editors' art layers re-check whether they may move (after a settings change)
juce::String artSelfTest(); // "OK" or what failed: style resolution with art, every art drawn (LYNX --selftest)
// the style the kit draws in right now (editors set it for their paint with StyleScope)
Style style();
void setStyle (Style);
const char* styleName (Style);            // English; translated with tr()
Style styleFromName (const juce::String&); // "neon", "Hardware"... (unknown: glow)

// Per plugin (the user's choice, round 126): every editor has its own style, alternating across the set so it feels
// varied — LYNX Tune Vivid, Epoch Verb Hardware, Crest Compressor Glow... Settings can force one style everywhere
// (-1 = automatic, per plugin), and an editor's header menu can override its own (saved with the UI options).
int forcedStyle();
void setForcedStyle (int styleOrMinusOne);
int styleKey (EffectKind, bool soundgoodizer = false); // the key of an editor in the override table
Style defaultStyleFor (int key);                        // the built-in table
Style plainStyleFor (int key);                          // ... as it is without the plugin's own art (Themed)
Style styleFor (EffectKind, bool soundgoodizer = false); // forced > override > table
Style resolve (Style automatic);                        // forced, or `automatic`
Style resolve (Style automatic, Art);                   // the same for a page with (or without) art: Themed needs art
int overrideFor (int key);                              // −1: none
void setOverride (int key, int styleOrMinusOne);
juce::String overridesToString();
void overridesFromString (const juce::String&);
inline std::function<void()> onStyleSettingsChanged; // the main window saves its UI options
// adds "Plugin style" (Automatic / Glow / Neon / Hardware / Vivid) for one editor to a menu; `changed` repaints it
void addStyleMenu (juce::PopupMenu&, int key, std::function<void()> changed);
inline constexpr const char* styleProperty = "lynxSkinStyle"; // a generator page's knob: its page's automatic style
// the styleProperty value: the automatic style and the page's art in one int
inline int packStyle (Style s, Art a) { return (int) s | ((int) a << 8); }
inline Style unpackStyle (int packed) { return (Style) juce::jlimit (0, styleCount - 1, packed & 0xff); }
inline Art unpackArt (int packed) { return (Art) ((packed >> 8) & 0xff); }
void setArt (Art);

// draws with a style (and, for Themed, an art) until the end of the scope; Themed without art draws Glow
struct StyleScope
{
    explicit StyleScope (Style s, Art a = Art::none) : saved (style()), savedArt (art())
    {
        setArt (a);
        setStyle (s == Style::themed && a == Art::none ? Style::glow : s);
    }
    ~StyleScope()
    {
        setStyle (saved);
        setArt (savedArt);
    }
    StyleScope (const StyleScope&) = delete;
    StyleScope& operator= (const StyleScope&) = delete;
    Style saved;
    Art savedArt;
};

// The animated part of an editor's art (falling petals, a pulsing glow, twinkling stars...), a transparent layer
// over the editor that takes no mouse clicks. It repaints only the small areas that move, at ≤ 30 Hz, and only
// while the editor is on screen, the style is Themed and artMotion() is on; otherwise it shows a still frame.
class ArtLayer final : public juce::Component, private juce::Timer
{
public:
    ArtLayer();
    ~ArtLayer() override;
    // `active` tells whether the editor draws in the Themed style right now (Settings can change it at any time)
    void setArt (Art, std::function<bool()> active);
    // where the art may move (the editor's free areas; empty: everywhere)
    void setStage (juce::Rectangle<float>);
    void paint (juce::Graphics&) override;
    void visibilityChanged() override { kick(); }
    void parentHierarchyChanged() override { kick(); }
    void resized() override;
    void kick();                 // restart the motion if it may run
    bool animating() const { return isTimerRunning(); }
    double phase() const { return t; }
    void setPhase (double seconds); // a still frame at this time (snapshots)

private:
    void timerCallback() override;
    bool showing() const;
    struct Impl;
    std::unique_ptr<Impl> impl;
    Art artId = Art::none;
    std::function<bool()> isActive;
    juce::Rectangle<float> stage;
    double t = 0.0, lastMs = 0.0;
};

// --- effects level (round 126) --------------------------------------------------------------------
// "Reduced effects" (Settings > General): no glows, blooms, grain or open animation, for weak GPUs / old machines
bool reducedEffects();
void setReducedEffects (bool);
// an editor window's opening: its content fades and scales in (≤ 220 ms); nothing when effects are reduced
void playOpenAnimation (juce::Component& content, Style = Style::glow);

// --- typography ----------------------------------------------------------------------------------
// round tp172d: a themed editor may set its own caption / value colours and type size (artkit::Theme)
juce::Colour captionColour();
juce::Colour valueColour (bool hot);
float captionScale();

// --- animation -----------------------------------------------------------------------------------
// Smooth hover / press levels per control id, advanced by a 60 Hz timer that runs ONLY while something is
// still moving (an idle editor costs nothing). The owner calls setTarget on hover changes and repaints in onFrame.
class Animator final : private juce::Timer
{
public:
    std::function<void()> onFrame; // repaint (the owner decides what)

    void setHot (int id) // the control under the mouse (or −1): it fades in, the others fade out
    {
        if (id == hot) return;
        hot = id;
        if (id >= 0 && levels.find (id) == levels.end()) levels[id] = 0.0f;
        kick();
    }
    float level (int id) const
    {
        const auto it = levels.find (id);
        return it != levels.end() ? it->second : 0.0f;
    }
    bool running() const { return isTimerRunning(); }

private:
    void kick()
    {
        if (! isTimerRunning()) startTimerHz (60);
    }
    void timerCallback() override
    {
        auto moving = false;
        for (auto it = levels.begin(); it != levels.end();)
        {
            const auto target = it->first == hot ? 1.0f : 0.0f;
            auto& x = it->second;
            x += (target - x) * (target > x ? 0.35f : 0.18f);
            if (std::abs (target - x) < 0.02f) x = target;
            else moving = true;
            if (x == 0.0f && it->first != hot) it = levels.erase (it);
            else ++it;
        }
        if (onFrame) onFrame();
        if (! moving) stopTimer();
    }

    int hot = -1;
    std::map<int, float> levels;
};

// a meter level that rises at once and falls smoothly (dB per second), with a peak hold
struct Ballistics
{
    float level = 0.0f, peak = 0.0f; // 0..1
    int hold = 0;                    // frames left before the peak falls
    // returns true when anything moved
    bool update (float in01, float fallPerFrame = 0.025f)
    {
        const auto before = level + peak;
        level = in01 >= level ? in01 : juce::jmax (in01, level - fallPerFrame);
        if (in01 >= peak) { peak = in01; hold = 30; }
        else if (hold > 0) --hold;
        else peak = juce::jmax (level, peak - fallPerFrame * 0.6f);
        return std::abs (level + peak - before) > 1e-4f;
    }
};
} // namespace lynx::skin
