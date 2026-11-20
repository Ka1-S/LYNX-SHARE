#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ToolbarLayout.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"
#include "ui/MotionOverlay.h"
#include "ui/ValueControl.h"

// Widgets of the main toolbar (docs/WORKFLOW_FL.md section 5.1), each with its own hint: the song position
// slider, the time display, master volume and pitch, the CPU / memory / polyphony meter and the output
// monitor. Laid out by MainComponent::resized().
namespace lynx::toolbar
{
// A thin slider under the time display: where the playhead is in the song (Song mode) or in the
// current pattern (Pattern mode). Click or drag to move the playhead; the wheel moves it by a bar.
class SongPositionBar final : public juce::Component,
                              public ui::HintSource
{
public:
    explicit SongPositionBar (AudioEngine& e);
    void update(); // from the main window's timer
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    double length() const;
    double position() const;
    void seekTo (int x);
    juce::Rectangle<float> track() const;
    AudioEngine& engine;
    double shownFraction = -1.0;
    bool dragging = false;
};

// The time display: bar : beat : step, minutes : seconds : hundredths or SMPTE time code (a click switches; the
// right-click menu picks the mode and the SMPTE frame rate, as FL's time display menu).
class TimeDisplay final : public juce::Component,
                          public ui::HintSource
{
public:
    explicit TimeDisplay (AudioEngine& e) : engine (e)
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void update();
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    bool showsTime() const noexcept { return mode == Mode::clock || mode == Mode::smpte; }
    juce::String textForSnapshot() const { return text(); } // round 190kn (--details190knshot)
    // round 190iv: the Simple capsule's time segment, or Live's 17 px position box ("1. 1. 1"); FL's look is unchanged
    void setLook (toolbarlayout::Look l);

    enum class Mode
    {
        bars,  // the Pattern toolbar: FL's B:S:T ("1:01:00", round 190jo FM-6); the other looks: bar : beat : step
        clock,
        smpte,
        beats  // round 190jo: LYNX's "1 : 1 : 1" (bar : beat : step) kept in the Pattern toolbar's menu
    };

private:
    juce::String text() const;
    void showMenu();
    void paintLook (juce::Graphics& g);
    toolbarlayout::Look look = toolbarlayout::Look::fl;
    AudioEngine& engine;
    juce::String shown;
    juce::uint32 shownAt = 0; // (round 190jo) when `shown` last changed: the B:S:T ticks repaint at most every 100 ms
    bool red = false;           // (round 190jj) the Simple capsule's recording / count-in time
    double linkProgress = -1.0; // (round 190gl) the Link session's progress bar while the transport waits / is stopped
    // (round 190kz) the FL look's fitted digit height and caption width, worked out again only when the readout's length,
    // the box or the mode change (the digits are a monospaced font): shaping the text twice per paint was ~4 % of the
    // message thread while playing
    struct Fit
    {
        int length = -1, width = -1, height = -1, mode = -1;
        float scale = 0.0f, digits = 0.0f, caption = 0.0f;
    } fit;
    Mode mode = Mode::bars;
    int smpteRate = 5; // timecode::rates index (30 fps)
};

// Master volume (the Master insert's fader) or master pitch (cents, Song::masterPitch): a small knob with
// its name and value beside it.
class MasterKnob final : public juce::Component,
                         public ui::HintSource
{
public:
    enum class Kind
    {
        volume,
        pitch
    };

    MasterKnob (AudioEngine& e, Kind k);
    void update(); // repaints when the song's value changed
    // Round 190jj (capture190ji §1c, shots/bl-c-headers-and-master-meter.png): in the Simple top bar the master volume is
    // BandLab's: a speaker, a slider whose rail is the master meter (two thin bars, left over right, green to yellow,
    // falling in ~0.6 s with no peak hold, a short stub at rest) and the value in dB. Other looks keep the knob.
    void setLook (toolbarlayout::Look l);
    bool isSlider() const noexcept { return look == toolbarlayout::Look::simple && kind == Kind::volume; }
    // one frame of the meter (the main window's 30 Hz timer, Simple only): repaints the rail when a bar moved a pixel
    void tickMeter (double nowMs);
    static constexpr int sliderWidth = 196; // speaker 20 + rail 112 + value 64
    juce::Rectangle<float> sliderRail() const;
    std::array<float, 2> meterForSnapshot (std::array<float, 2> levels) { meter = levels; repaint(); return meter; }
    void paint (juce::Graphics& g) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    ui::ValueSpec spec() const;
    double value() const;
    void setValue (double v);
    bool available() const;
    AudioEngine& engine;
    Kind kind;
    ui::ValueDrag drag;
    double shown = -1.0;
    toolbarlayout::Look look = toolbarlayout::Look::fl;
    std::array<float, 2> meter {};        // the bars shown, as rail fractions (the fader law: 0 dBFS at the 0 dB mark)
    std::array<int, 2> meterPixels { -1, -1 };
    double meterMs = -1.0;
    bool sliderDrag = false;
    void paintSlider (juce::Graphics& g);
    void setFromRail (float x);
};

// A small light that blinks while MIDI arrives from the enabled input devices (like FL's MIDI activity light).
// A click reconnects the MIDI inputs (closes and opens them again).
class MidiActivityLight final : public juce::Component,
                                public juce::SettableTooltipClient,
                                public ui::HintSource
{
public:
    // `count` = the engine's running count of received messages; call ~30 times a second. Round 190ld (FL's Sync
    // indicator colours, manual "Toolbar panels"): `projectLinked` / `globalLinked` = the engine's counts of messages a
    // project link / a global link took: orange = handled by a project link, blue = by a global link, green = used by
    // no link, grey = nothing.
    void update (std::uint32_t count, const juce::StringArray& activeInputs, std::uint32_t projectLinked = 0, std::uint32_t globalLinked = 0);
    // FL's MIDI Sync light (round 190ld): "Blinks at the beginning of each beat and signals blue at the beginning of each
    // bar" while master sync is sent (FL: "works only when the Enable MIDI Output option ... is checked")
    void beat (bool barStart);
    enum class Kind : std::uint8_t { unhandled, project, global, beat, bar };
    Kind litKind() const noexcept { return kind; }
    bool isLit() const noexcept { return litFrames > 0; }
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    std::function<void()> onReconnect;

private:
    std::uint32_t lastCount = 0, lastProject = 0, lastGlobal = 0;
    bool seenCount = false;
    int litFrames = 0;
    Kind kind = Kind::unhandled, paintedKind = Kind::unhandled;
    juce::StringArray inputs;
};

// CPU load, memory used by LYNX and the polyphony (notes sounding now), in two rows.
class PerformanceMeter final : public juce::Component,
                               public ui::HintSource
{
public:
    void setValues (double cpu01, std::int64_t memoryBytes, int polyphony);
    // Real-time health (REA round 87): callbacks that missed their deadline (the crackles), the highest load since
    // the last update and the heaviest channels / effects ("name 12%").
    // `deviceUnderruns`: the driver's own count (−1: it does not report one), shown like FL's "N underruns".
    void setRealtime (std::uint64_t overDeadline, std::uint64_t lateCalls, double peakLoad, const juce::StringArray& heaviest,
                      int deviceUnderruns = -1);
    // After dropouts on Windows shared mode: the ASIO driver to suggest (round 160; empty = none)
    void setDriverTip (const juce::String& asioDriver) { driverTip = asioDriver; }
    // Round 190gg (B5): the dropouts since the start, the last one's time and cause (ui/PluginMonitorWindow.h
    // describeDropouts), at the end of the CPU hint
    void setDropouts (const juce::String& text) { dropoutText = text; }
    // round 190iv: Live's CPU box ("26 %" over an orange load bar, 54 x 17); FL's look is unchanged
    void setLook (toolbarlayout::Look l);
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    // round 190kn (FL's CPU panel, DF-26 / 27 / 28): a double-click opens the Plugin performance monitor; right-click: the
    // refresh rate (Slow / Medium / Fast, FL's names) and the monitor; the bar's box carries FL's running CPU graph.
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    juce::PopupMenu makeMenu(); // the right-click menu (shots)
    std::function<void()> onOpenMonitor, onRefreshChanged;
    enum Refresh { slow, medium, fast };
    static inline int refresh = medium;   // saved in the UI options ("cpuRefresh")
    // round 190ld: FL's "Misc" radio pair, what the panel's second number shows: the voice count (FL's default) or the
    // frames per second of the display (LYNX: frames in which something on screen moved). Saved ("cpuView").
    enum SecondView { voiceCount, framesPerSecond };
    static inline int secondView = voiceCount;
    void setFramesPerSecond (int fps);
    // FL's info lines under Misc ("Handles: 753 (GDI) / 569 (USER)", "Threads: 41"): this process's own counts
    static int threadCount();
    static std::pair<int, int> guiHandles(); // {GDI, USER}; {-1, -1} where the OS has no such count
    static int refreshTicks() noexcept { return refresh == slow ? 16 : refresh == fast ? 4 : 8; } // of the main 30 Hz timer
    static constexpr int historySize = 96; // the running graph's points (one per update; 25 s at Medium)
    juce::Rectangle<float> graphArea() const; // (tests / shots; empty in the Live look)
    int historyCount() const noexcept { return cpuHistoryCount; }

    static std::int64_t processMemoryBytes(); // working set of this process (0 if unknown)
    static std::int64_t processPrivateBytes(); // memory only this process holds (Windows: private bytes; else the working set)

private:
    std::uint64_t overDeadline = 0, lateCalls = 0, underruns = 0;
    int deviceUnderruns = -1;
    juce::String driverTip, dropoutText;
    double peakLoad = 0.0;
    juce::StringArray heaviest;
    juce::uint32 lastGlitchAt = 0; // ms counter of the last new over-deadline callback (0 = none)
    double cpu = 0.0, cpuSmoothed = 0.0;
    toolbarlayout::Look look = toolbarlayout::Look::fl;
    std::int64_t memory = 0;
    int voices = 0, peakVoices = 0, fps = 0;
    std::array<float, historySize> cpuHistory {}; // oldest first (round 190kn)
    int cpuHistoryCount = 0;
    juce::int64 paintedSignature = -1;
    bool glitchShown = false;
    juce::Rectangle<float> barBox() const;
};

// The output: a display of what is heard and a peak meter (red after clipping, for a while). Like FL's
// output panel, the display has modes (right-click menu, or a click steps through them): the oscilloscope,
// a long scrolling view of the level, and a spectrum (low frequencies on the left).
// FL's Chord detection panel (round 181b, REA_rodada181b.md): names the chord being played on MIDI and the typing
// keyboard, and the notes selected in the Piano Roll. Right-click: chord style and display delay.
class ChordPanel final : public juce::Component,
                         public ui::HintSource
{
public:
    void setText (const juce::String& newText);
    const juce::String& getText() const { return text; }
    std::function<void()> onMenu;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    juce::String text { "---" };
};

// The project key (round 184a S11, design §3.1; LYNX's own panel): "Key" over the key's name ("A minor", "none"). A click
// opens the Key menu (root, scale, None, Guess from notes). Hidden by default; the Simple and Clip-launch workflows show it.
class KeyPanel final : public juce::Component,
                       public ui::HintSource
{
public:
    KeyPanel() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setText (const juce::String& keyName); // "" = no key
    const juce::String& getText() const { return text; }
    std::function<void()> onClick;
    // round 190iv: BandLab's Key capsule ("D min"; "Key" while none) in Simple, Live's key box in Clip-launch
    void setLook (toolbarlayout::Look l);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    juce::String text;
    toolbarlayout::Look look = toolbarlayout::Look::fl;
};

// Ableton Link's switch (round 190gl), as the reference DAW's control bar has it (its manual's "The Link Toggle in the
// Control Bar"): "Link" while off; on, the number of other peers ("0 Links", "1 Link", "2 Links"), lit; the arrow beside
// it opens the drop-down (Link on / off, Configure Link...). In FL's Pattern layout it is LYNX's own small switch,
// hidden until the user shows it (Settings › Link › Show Link Toggle, or the toolbar's edit mode).
// Live's phase-nudge buttons (round 190mv, DL-51; capture 190mv B2, live-phase-nudge.png): a 16 px box with four upright
// bars, no box at rest, grey under the mouse, orange (255,194,129) with black bars while held. Held, the song runs a
// little slower (down) or faster (up) — Live's "Temporarily decrease / increase song tempo to synchronize to external
// music"; let go, the tempo is back. Clip-launch layout only.
class PhaseNudge final : public juce::Component,
                         public ui::HintSource
{
public:
    PhaseNudge (AudioEngine& e, bool up) : engine (e), faster (up) { setRepaintsOnMouseActivity (true); }
    ~PhaseNudge() override { release(); }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { release(); }
    ui::Hint hintAt (juce::Point<int>) override;
    bool isHeldForTests() const noexcept { return held; }

private:
    void release();
    AudioEngine& engine;
    bool faster;
    bool held = false;
    double base = 0.0;
};

class LinkToggle final : public juce::Component,
                         public ui::HintSource
{
public:
    explicit LinkToggle (AudioEngine& e) : engine (e) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onConfigure; // "Configure Link...": Settings › Link
    void update();                     // the main window's timer: peers, on / off
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    ui::Hint hintAt (juce::Point<int> position) override;
    static int idealWidth (int height);
    juce::String text() const;         // what the switch shows now (tests)
    juce::PopupMenu makeMenu();        // the drop-down (shots)
    void setLook (toolbarlayout::Look l); // round 190iv: Live's 17 px box in the Clip-launch control bar

private:
    static constexpr int arrowWidth = 12;
    // the switch's box: a short row in the middle of the toolbar's height (as small as the reference's toggle)
    juce::Rectangle<int> boxArea() const { return getLocalBounds().withSizeKeepingCentre (getWidth(), juce::jmin (getHeight(), 20)); }
    juce::Rectangle<int> arrowArea() const { return boxArea().removeFromRight (arrowWidth); }
    void showMenu();
    AudioEngine& engine;
    bool on = false;
    int peers = 0;
    toolbarlayout::Look look = toolbarlayout::Look::fl;
};

// The chord near the hint bar (View › Toolbars): outlined text with no box, over whatever is below the bar, as in FL.
class ChordHint final : public juce::Component
{
public:
    ChordHint() { setInterceptsMouseClicks (false, false); }
    void setText (const juce::String& newText);
    void paint (juce::Graphics& g) override;

private:
    juce::String text;
};

class OutputMonitor final : public juce::Component,
                            public ui::HintSource
{
public:
    explicit OutputMonitor (AudioEngine& e) : engine (e)
    {
        gpuDisplay.setLive (true);
        gpuDisplay.onHidden = [this] { repaint(); }; // (the overlay went by itself: the view paints the display again)
    }
    // one display frame (round 174e: at the display's refresh; the falloffs and the long view's scroll go by time, as
    // they did at 30 Hz). Returns true while the display moves (false once a silent output has fallen to zero).
    bool update (double nowMs);
    void paint (juce::Graphics& g) override;
    ui::Hint hintAt (juce::Point<int> position) override;
    void mouseDown (const juce::MouseEvent& e) override;
    // round 190kn (FL's output panel, DF-23 / DF-25): the left / right peak bars beside the display, and the level at
    // the pointer when it is over them (FL: "hovering your cursor over most peak meters will show the value at the
    // pointer position"). Public for the shot route and tests.
    juce::Rectangle<float> barsArea() const;
    juce::Rectangle<float> barArea (int side) const;
    static float levelAt (float x01) noexcept;        // the bar's position 0..1 -> dB (-60..0)
    void setSidesForShot (float left, float right, bool clipLeft, bool clipRight);

    enum Mode
    {
        oscilloscope,
        longView,
        spectrum,
        modeCount_
    };
    static constexpr int modeCount = (int) modeCount_;
    static inline int mode = oscilloscope; // shared by every instance; saved in the UI options
    // FL's Oscilloscope › "Stereo" (round 190mv, DF-24; capture 190mv A8: off = one line, on = the left side above the right,
    // each its own line): saved in the UI options
    static inline bool stereoScope = false;
    void setScopeSidesForShot (float leftPhase, float rightPhase); // (--details190mvshot: two sines)
    static juce::String modeName (int m);
    std::function<void()> onModeChanged;   // the main window saves the choice

    // diagnostics (LYNX_OVERLAY_STATS / --selftest): frames this display showed on the GPU overlay instead of a repaint
    static std::atomic<int>& overlayFrames();

private:
    void setMode (int newMode);
    // Round 190kz: while it moves, the whole display (background, meter, scope) is a picture on the GPU overlay
    // (ui/MotionOverlay: a DirectComposition visual over the main window) painted in software each frame, so the main
    // window runs no Direct2D frame and no present for it. Profiled (Windows, Thief playing, toolbar at the display's
    // rate): the main window's frames were ~58 % of the message thread's cycles (EndDraw 28 %, the display's paint 11 %,
    // present 11 %), nearly all for this display. The repaint stays the fallback whenever the overlay cannot show it
    // (another platform, drawing with the CPU, something in front of it). LYNX_SCOPE_OVERLAY=0: always the repaint.
    void frameChanged();      // the display's picture changed this frame: on the overlay, else a repaint
    void stopOverlay();       // it stopped moving: the view paints itself again
    void paintDisplay (juce::Graphics& g, bool software);
    ui::MotionOverlay gpuDisplay { *this, [this] (juce::Graphics& g, float x) {
        g.addTransform (juce::AffineTransform::translation (x, 0.0f));
        paintDisplay (g, true);
    } };
    std::uint64_t overlayKey = 0; // a new value each frame: the overlay paints its picture again
    juce::Path wave;              // the scope's path, its storage kept from frame to frame
    AudioEngine& engine;
    std::array<float, 1024> samples {};
    std::array<float, 1024> sideSamples {}; // (round 190mv: Stereo)
    juce::Image scopeImage, scopeNative; // the oscilloscope's stroke, drawn in software, and its copy for the window (round 174e2)
    std::array<float, 256> history {};     // long view: one peak per 1/30 s, oldest first
    std::array<float, 128> bands {};       // spectrum: smoothed level per band, 0..1
    float peak = 0.0f, held = 0.0f;
    std::array<float, 2> sidePeak {}, sideHeld {};   // (round 190kn) the stereo bars: falling peak and hold line per side
    std::array<double, 2> sideHoldMs {}, sideClipMs {};
    float column = 0.0f;                   // the long view's next column: the loudest frame since the last one
    double holdMs = 0.0, clipMs = 0.0, columnMs = 0.0, lastMs = 0.0;
    double silentMs = 0.0; // how long the output has been silent: once everything has decayed, no more repaints
};

// --- round 190iv: the Simple top bar's and the Clip-launch control bar's own fields ------------------------------------
// The song's time signature ("4 / 4"): a segment of the Simple tempo capsule (BandLab) or Live's 17 px box. A click opens
// the signatures (the Pattern menu's "Song time signature" list). update() from the main window's timer repaints only
// when the signature changed.
class SignatureBox final : public juce::Component,
                           public ui::HintSource
{
public:
    explicit SignatureBox (AudioEngine& e) : engine (e) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void setLook (toolbarlayout::Look l);
    void update();
    juce::String text() const;
    juce::PopupMenu makeMenu();
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    AudioEngine& engine;
    juce::String shown;
    int shownSignature = -1;
    toolbarlayout::Look look = toolbarlayout::Look::live;
};

// Live's global launch quantisation ("1 Bar ▾") in the control bar (it was the clip grid's footer box, design190ia CL-25).
class QuantBox final : public juce::Component,
                       public ui::HintSource
{
public:
    explicit QuantBox (AudioEngine& e) : engine (e) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void update();
    juce::String text() const;
    juce::PopupMenu makeMenu();
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    AudioEngine& engine;
    juce::String shown;
    int shownQuant = -1;
};

// The Simple top bar's "Last saved" status (design190ia SG-17; BandLab's "Last Saved" at its top right): a 14 px icon
// before the text, the whole item a 24 px pill that saves (Ctrl+S). Four states (toolbarlayout::SaveState).
class SaveStatus final : public juce::Component,
                         public ui::HintSource
{
public:
    SaveStatus() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void set (toolbarlayout::SaveState state, const juce::String& text); // repaints only on a change
    toolbarlayout::SaveState getState() const noexcept { return state; }
    const juce::String& getText() const noexcept { return text; }
    std::function<void()> onSave;
    void paint (juce::Graphics& g) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    ui::Hint hintAt (juce::Point<int> position) override;

private:
    toolbarlayout::SaveState state = toolbarlayout::SaveState::fresh;
    juce::String text;
    bool known = false;
};

// "Customize toolbar": the groups in toolbar order, each with a check box (shown / hidden), Move up / Move
// down for the selected group (also Alt+Up / Alt+Down), the icon size, the UI scale and Reset to default.
// Every change applies at once through onChange.
class CustomizePanel final : public juce::Component
{
public:
    explicit CustomizePanel (const toolbarlayout::Layout& layout);
    std::function<void (const toolbarlayout::Layout&)> onChange;
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void select (toolbarlayout::Group g);

private:
    void rebuild();
    void changed();
    void move (int delta);
    juce::Rectangle<int> listArea() const;
    static constexpr int rowHeight = 30;
    toolbarlayout::Layout layout;
    toolbarlayout::Group selected = toolbarlayout::Group::transport;
    juce::OwnedArray<juce::ToggleButton> checks;
    juce::TextButton upButton, downButton, resetButton;
    juce::ComboBox iconSizeBox, scaleBox;
};
// The toolbar's edit mode (FL's toolbar "Edit", interpreted; REA_rodada115): while it is on, the toolbar's panels
// show their outlines and names and do not react to clicks; a tray under the toolbar lists every panel.
//  - drag a panel sideways to move it (an accent line shows where it lands)
//  - drag a panel down into the tray to remove it; drag a panel from the tray onto the toolbar to add it there
//  - double-click a panel in the tray: add / remove it; right-click a panel: remove, move left / right
//  - presets (LYNX's own), "Reset" and "Done" (Esc or Enter) on the tray
class EditOverlay final : public juce::Component,
                          public ui::HintSource
{
public:
    using Bounds = std::array<juce::Rectangle<int>, toolbarlayout::groupCount>;
    struct Host
    {
        std::function<toolbarlayout::Layout()> layout;
        std::function<void (const toolbarlayout::Layout&)> setLayout;
        std::function<Bounds()> panels;                       // each shown panel's area, in this component's coordinates
        std::function<juce::Rectangle<int>()> fixedArea;      // the menu and the hint bar (cannot move)
        std::function<juce::Image (juce::Rectangle<int>)> snapshot; // how a panel looks (for the drag image)
        std::function<void()> done;
    };

    static constexpr int trayHeight = 92;

    EditOverlay (Host host, int toolbarHeight);
    void setToolbarHeight (int height);
    void layoutChanged(); // the layout changed elsewhere: the preset box follows

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    static Icon groupIcon (toolbarlayout::Group g);

private:
    juce::Rectangle<int> trayArea() const { return { 0, toolbarHeight, getWidth(), trayHeight }; }
    juce::Rectangle<int> chipArea() const;
    juce::Rectangle<int> chipBounds (int index) const; // index in the default order
    int panelAt (juce::Point<int> p) const;            // group or -1
    int chipAt (juce::Point<int> p) const;             // group or -1
    int dropX() const;                                 // where the dragged panel would land (x of the accent line), -1 none
    void apply (toolbarlayout::Layout layout);
    void showPanelMenu (toolbarlayout::Group g, bool inTray);
    void move (toolbarlayout::Group g, int delta);

    Host host;
    int toolbarHeight = 0;
    int hoverPanel = -1, hoverChip = -1;
    int pressedGroup = -1;
    bool pressedInTray = false, dragging = false;
    juce::Point<int> dragPos, grabOffset;
    juce::Image dragImage;
    juce::ComboBox presetBox;
    juce::TextButton resetButton, doneButton;
};
} // namespace lynx::toolbar
