#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>
#include <unordered_map>

// LYNX's own vector icons, drawn with juce::Path on a 24x24 grid. Original designs, one family:
// - live area 3..21 (a 3-unit margin), coordinates on half units so 24 px and 16 px stay crisp;
// - line icons share one fine stroke (strokeWidth(): about 1.25-1.5 px at toolbar sizes), round caps and joins;
// - transport symbols and small accents (a lit step, a note, a dot) are solid fills.
namespace lynx
{
enum class Icon
{
    none,
    play, pause, stop, record, chevronDown, more, plus, check, close, minimise, maximise,
    pattern, song, metronome,
    newFile, open, save, exportAudio, settings, help, undo, redo,
    channelRack, pianoRoll, playlist, mixer, browser,
    folder, file, audioFile, plugin, preset, project,
    draw, erase, select, slice, mute, link, magnet, scissors,
    // workspace and control contract
    detach, dock, restore, keyboard, automate, rightClick, tap,
    brush, knife, zoom, listen, slip, ghost, // editor tools (Piano Roll / Playlist)
    panelMenu, pluginPicker, mic, // window options menu, plugin picker, recording options
    countIn, waitInput, // recording: count-in and wait for input
    mouseLeft, mouseMiddle, mouseWheel, // hint bar gestures (with rightClick)
    tuningFork, // master pitch
    drumBrush,  // Piano Roll: the drum paint tool
    graph,      // Channel Rack: the step graph editor (velocity, pan... bars)
    follow,     // Playlist / Piano Roll: the view follows the playhead (auto scrolling)
    envelope,   // channel window: the Envelope / instrument tab, an ADSR line (round 190in, design190ia FW-1)
    wrench,     // channel / plugin window: the Misc and wrapper settings tabs, FL's wrench (round 190in, FW-1)
    loop,       // Channel Rack: channel looping (round 165f-rack)
    dice,       // Channel Rack: Loop Starter (round 168b)
    stretch,    // Playlist: stretch audio when resizing audio clips (round 168f)
    scaleSnap,  // Piano Roll: snap to the scale (a small magnet over keys; round 170e)
    tweak,      // the toolbar's last tweaked parameter: a knob with a turning arrow (round 170e)
    slide,      // Piano Roll: new notes slide (a note with a glide arrow; round 170e)
    chord,      // Piano Roll: the chord stamp (three stacked notes; round 170e)
    span,       // Channel Rack: the pattern length (an arrow between two bar lines; round 170e)
    blend,      // recording: blend recorded notes into the pattern (two lines merging into one; round 170e2)
    filter,     // a funnel: filters a list (Channel Rack's channel filter group; round 170e2)
    tapTempo,   // the tempo tapper: a fingertip tapping a pad (round 170e2)
    loopRecord, // recording: loop record (a loop arrow around a record dot; round 170e3)
    stepEdit,   // recording: step edit (steps typed one after another, an arrow to the next; round 170e3)
    punch,      // recording: punch in/out on the loop region (a record dot between two brackets; round 170e3)
    note,       // the Browser's Scores folder: an eighth note (round 178a)
    deleteTool, // Playlist (Pattern layout): FL's delete tool, a circle with a slash (round 190ig, FM-26)
    slipArrows, // Playlist (Pattern layout): FL's slip tool, two arrowheads back to back (round 190ig, FM-26)
    // round 190iv: the Simple top bar's save status and the Clip-launch layout's control bar (LYNX vectors drawn to read
    // like BandLab's and Live's)
    cloud,             // saved by LYNX's backup
    cloudCheck,        // all changes saved
    sessionView,       // Live's view selector: three columns (the clip grid)
    arrangementView,   // three rows (the Playlist)
    backToArrangement, // an arrow into a dashed line
    capture,           // a dashed square (Capture MIDI)
    sessionRecord,     // a ring
    browserPanel,      // a left panel beside a page (show / hide the Browser)
    metronomeDots,     // Live's metronome: a ring and a dot
    toStart,           // round 190jj: BandLab's Back-to-start (a bar and a triangle pointing left), the Simple capsule's idle Stop
    // round 190jo (design190ia FW-22): the audio editor's tool grid and small icon row, LYNX vectors with Edison's metaphors
    copy, paste, trash, trim, silence, fadeIn, fadeOut, fadeCurve, normalize, amp, reverse, invert, dcCenter, resample,
    marker, region, setLoop, equalizer, denoise, reverb, spectrogram,
    pointer,           // round 190jq: the Playlist's pointer-click switch, a mouse pointer (issue #191)
    scrub,             // round natives190kj: Edison's Scrub button (a film reel: a ring with four holes around a hub)
    cycle,             // round 190ld: BandLab's Cycle (loop) switch, two arrows round a rounded box (B07 at 966,74)
    // round 190lx (DL-61, capture190ji live-loop-punch-z3): Live's Punch-In, Loop and Punch-Out switch glyphs
    punchIn,           // a high run, a slope down, a low run
    liveLoop,          // an open box with an arrow head pointing left in its bottom gap
    punchOut,          // a low run, a slope up, a high run
    // round 190oa (DL-58, capture 190oa L1 live-auto-arm / live-reenable-lit): Live's Automation Arm and Re-Enable
    // Automation glyphs
    automationArm,     // two small rings joined by a rising line
    reEnableAutomation, // an arrow pointing left
    search             // round 190on (P2): the command palette, a magnifier (no plus: Icon::zoom is the zoom tool's)
};
constexpr int iconCount = (int) Icon::search + 1;

namespace icons
{
struct Shape
{
    juce::Path stroke; // drawn with a line (stroke width scales with the size)
    juce::Path fill;   // solid parts
};

const Shape& get (Icon icon);

// The line width of the icons drawn `size` pixels square.
float strokeWidth (float size);

// Draws `icon` fitted (centred, square) into `area`.
void draw (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour);
} // namespace icons

// A flat button that shows an icon, optionally followed by text. Uses the theme colours:
// neutral at rest, red accent when toggled on (or when `primary`).
//
// Hover and press states fade in and out (about 120 ms) instead of switching at once. The states follow LYNX's one rule
// for every button kind (core/ThemePalette.h buttonLook, round 190je): a switch previews its "on" look under the mouse
// (FL 2024), a button that acts once lights a tile.
class IconButton : public juce::Button,
                   private juce::Timer
{
public:
    IconButton (const juce::String& name, Icon icon, const juce::String& text = {});

    void setIcon (Icon newIcon);
    Icon getIcon() const noexcept { return icon; } // (round 190jj: tests and shots)
    void setText (const juce::String& newText);
    void setPrimary (bool shouldBePrimary);              // filled with the accent even when off
    void setOnColour (juce::Colour colour);              // background when toggled on (default: accent)
    void setIconColourWhenOn (juce::Colour colour);      // icon/text colour when on (default: white)
    void setIconColour (juce::Colour colour);            // icon colour at rest (default: the theme's secondary text)
    void setFlat (bool shouldBeFlat);                    // no background at rest (toolbar style)
    void setCorners (bool roundLeft, bool roundRight);   // for segmented groups
    // Whether the button is a switch (its hover previews the "on" look) or acts once. By default a switch is a button that
    // toggles on click, sits in a radio group or was given its own "on" colours.
    void setToggleLook (bool isSwitch);
    int getIdealWidth (int height) const;
    // The icon's box: `fraction` of the button's height, at most `maxPx` (default 0.66 and 20 px). Icons are
    // drawn on whole pixels, so 16, 20 and 24 px boxes stay crisp.
    void setIconSizing (float fraction, float maxPx);

    // Round 190iv: how the button draws itself per toolbar look (core/ToolbarLayout.h Look). `standard` is every LYNX
    // button as before; the others are used only by the Simple and Clip-launch toolbars:
    //  - capsule: a segment of a BandLab capsule (the capsule's fill is drawn behind it): a hover / pressed fill only,
    //    the glyph in `iconBoxPx`; on = the accent glyph (Record: a muted-red fill)
    //  - liveBox: one of Live's 17 px control-bar boxes (#363636; hover #464646, pressed #1e1e1e, on #ffad56 with a
    //    #070707 glyph)
    //  - liveGlyph: Live's bare transport glyphs (#b5b5b5 on the band; on = playing green / recording red)
    //  - liveSelector: Live's view selector glyphs (on = #ffad56, off = a grey that keeps 3:1 on the band)
    //  - flTile (round 190jo, design190ia FM-1/FM-2/FM-3; the Pattern toolbar only): FL's button tile, 30x30 at 100 % with
    //    radius 4, fill bg3 and a 1 px `border` outline; hover on a switch previews its "on" look (accentMuted at 70 %,
    //    accentHover glyph, accent outline at 50 %; FL 2024: "toolbar buttons show if they're pressed when hovered"), on a
    //    plain button bg5 with a borderStrong outline; pressed bg0; on = the button's own on colours over the tile;
    //    disabled = the glyph at 35 %. The glyph in `iconBoxPx` (0: the button's icon sizing)
    enum class Look { standard, capsule, liveBox, liveGlyph, liveSelector, flTile };
    void setLook (Look newLook, float iconBoxPx = 0.0f);
    Look getLook() const noexcept { return look; }
    // flTile: hover previews the "on" look (a switch) instead of the plain hover (a momentary button)
    void setHoverShowsOn (bool shouldShowOn) { hoverShowsOn = shouldShowOn; }
    // Round 190ki (perf): flTile's body (fill and edge) from the parent's tile atlas. The parent paints an opaque backdrop
    // under the button (its own paint(), no sibling under the button) and `stamp` names everything that backdrop shows
    // within the button's bounds (in the parent; 0: not cacheable). The atlas is an image of the parent's band at the
    // parent's coordinates; each button keeps its region of it drawn for its current state (the backdrop and the body,
    // by the same calls, pixels and clip as the frame) and blits it; the glyph is drawn straight over it. Hover fades,
    // fractional scales, translucent components and LYNX_PAINT_CACHE=0 paint straight. On by default on JUCE's software
    // renderer (round 190mh: pixel-exact there), opt-in on the native ones with LYNX_TILE_MODE=2 (Icons.cpp
    // tileModeSetting: Direct2D then draws a later path of the frame a little differently); LYNX_TILE_MODE=0: off.
    // Empty: no cache.
    // `parentClip` (round 190mh; JUCE's software renderer, where the atlas is on by default, paints straight without it):
    // the parent's clip bounds when it last painted into a frame (in the parent; it paints just before its children),
    // recorded by the parent's paint() unless paintingTileAtlas() (its paint into the atlas), so the atlas paints the
    // backdrop with the frame's clip edge (Icons.cpp tileCacheOnSoftwareSetting)
    void setFlTileBackdrop (std::function<std::uint64_t (juce::Rectangle<int>)> stamp, std::function<juce::Rectangle<int>()> parentClip = nullptr);
    static bool paintingTileAtlas() noexcept { return paintingTileAtlasFlag(); }

    // Optional extra gestures: when set, a right-click calls onRightClick instead of clicking the button,
    // and a double-click calls onDoubleClick (after the two single clicks).
    std::function<void()> onRightClick, onDoubleClick;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    void buttonStateChanged() override;

    // Round 190pa (P21): a short tag in the button's top-right corner, e.g. the armed automation write mode ("L" for
    // Latch) on the Record / Automation Arm button; empty = none (nothing is drawn, as before)
    void setBadge (const juce::String& newBadge, juce::Colour colour);
    const juce::String& getBadge() const noexcept { return badge; }

private:
    void timerCallback() override;
    void paintButtonBody (juce::Graphics&, bool highlighted, bool down);
    void paintBadge (juce::Graphics&);
    juce::String badge;
    juce::Colour badgeColour;
    void paintLook (juce::Graphics&, bool highlighted, bool down);
    void paintFlTile (juce::Graphics&, bool highlighted, bool down);
    bool tileCacheUsable (juce::Graphics&, int& scale) const;
public:
    struct TileAtlas
    {
        juce::Image image;
        int scale = 0;
        bool software = false;
        std::uint64_t generation = 0;
    };
private:
    static int tileCacheMode();
    static bool tileCacheOnSoftware();
    static TileAtlas& tileAtlasFor (juce::Component& parent, int bottom, int scale, bool software);
    std::uint64_t atlasKey = 0, atlasGeneration = 0;
    friend juce::String flTileCacheSelfTest();
    Look look = Look::standard;
    std::function<std::uint64_t (juce::Rectangle<int>)> tileBackdrop;
    std::function<juce::Rectangle<int>()> tileParentClip;
    static bool& paintingTileAtlasFlag();
    float lookIconBox = 0.0f;
    bool hoverShowsOn = false;
    float hover = 0.0f; // 0 = at rest, 1 = under the mouse (animated)
    Icon icon;
    juce::String text;
    bool primary = false, flat = false, roundLeft = true, roundRight = true;
    float iconFraction = 0.66f, iconMax = 20.0f;
    juce::Colour onColour, iconOnColour, iconColour;
    int toggleLook = -1; // -1 automatic, 0 acts once, 1 a switch
    bool onColoursSet = false;
    bool actsAsSwitch() const;
};

// --selftest "paint_caches=" (round 190ki): IconButton's FL tile bodies from the atlas (opt-in) against the bodies painted
// straight, over a backdrop with a translucent rounded well, at scales 1 and 2 on the native renderer (Direct2D on
// Windows; the software renderer paints them straight and is only reported on stderr): "OK" when every pixel is the
// same, else an error naming the case.
juce::String flTileCacheSelfTest();
} // namespace lynx
