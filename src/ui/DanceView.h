#pragma once

// LYNX Dance on screen (round 165e): the dancers of the project's dance channels drawn over the workspace (drag one to
// move it, right-click for its moves), and the channel's editor. The character is LYNX's own, drawn with vector shapes
// (core/FruityDance.h has the moves and FL's key map).

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/FruityDance.h"

namespace lynx
{
class AudioEngine;
class Instrument;

namespace dancer
{
// The character in `pose`, standing at `feet` (between its feet), `height` pixels tall at rest
void draw (juce::Graphics& g, juce::Point<float> feet, float height, const dance::Pose& pose, bool mirrored, juce::Colour outfit);
// The box the character can reach (arms up, jumps), for repaints and the mouse
juce::Rectangle<float> reach (juce::Point<float> feet, float height);

// A sprite sheet in FL's Fruity Dance format (issue #121, core/FruityDance.h): the picture, its frames and its names
struct Sheet
{
    juce::Image image;              // ARGB
    dance::SheetLayout layout;
    std::vector<std::string> names; // one per row (FL's ten when the sheet has no names file)
    int moves() const noexcept { return dance::sheetMoveCount (layout.rows); }
};
enum class SheetError { none, missing, unreadable, size };
// The sheet of that picture file, or nullptr (`error` says why). Kept while used; read again when its files change.
std::shared_ptr<const Sheet> sheetFor (const std::string& path, SheetError* error = nullptr);
// The frame standing at `feet` (its bottom centre), `scale` screen pixels per picture pixel
void drawSheet (juce::Graphics& g, const Sheet& sheet, juce::Point<float> feet, float scale, const dance::SheetFrame& frame);
juce::Rectangle<float> sheetBox (const Sheet& sheet, juce::Point<float> feet, float scale);
// Is the frame's picture solid at that point? (the clicks go through its transparent parts, as through FL's dancer)
bool sheetSolidAt (const Sheet& sheet, juce::Point<float> feet, float scale, const dance::SheetFrame& frame, juce::Point<float> point);
} // namespace dancer

// Over the workspace: one dancer per visible dance channel. Only the dancers take the mouse.
// Issue #116 slice 4 (round 188j): with docked plugin windows the panels and plugin editors are native child windows,
// which hide anything the main window's components draw. There (Windows, macOS: nativechild::supportsFloaters) each
// shown dancer gets a window of its own: a small transparent native child of the main window over its reach, kept in
// front of the panels and docked plugins, whose transparent pixels let the clicks through to what is under it. None
// exists without a shown dancer. Without a native main window (pictures), or with docking off, the stage draws them.
class DanceStage final : public juce::Component,
                         private juce::Timer
{
public:
    explicit DanceStage (AudioEngine& engine);
    ~DanceStage() override;

    // --plugwintest: the dancer's own window (nullptr: none, the stage draws it) and its body in stage coordinates
    juce::Component* windowOf (int channel) const;
    juce::Rectangle<int> bodyOf (int channel) const;
    int windowCount() const;

    void paint (juce::Graphics& g) override;
    bool hitTest (int x, int y) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

    int dancerCount() const noexcept { return (int) dancers.size(); }
    void update(); // scan the song and move the dancers on now (snapshots; the timer does it live)

private:
    struct Dancer
    {
        int channel = -1;
        std::vector<std::uint8_t> savedState;
        dance::Settings saved, now; // as saved; as the notes left it
        dance::Pose from, shown;
        double blendStart = -1.0, cycles = 0.0;
        int lastMove = -1;
        std::uint32_t seen = 0;
        std::shared_ptr<Instrument> instrument;
        juce::Rectangle<float> bounds; // painted last
        bool drawn = false;
        std::unique_ptr<juce::Component> window; // its own native window (see above), or nullptr
        // a sprite sheet (issue #121): its frames are drawn instead of LYNX's character
        std::shared_ptr<const dancer::Sheet> sheet;
        dance::SheetAnimator animator;
        dance::SheetFrame frame;
    };
    class Window;

    void timerCallback() override;
    void scan();
    juce::Rectangle<float> boxOf (const Dancer& d) const;
    Dancer* dancerAt (juce::Point<float> p, bool withWindows = false);
    static juce::Rectangle<float> bodyBox (const Dancer& d);
    static bool hits (const Dancer& d, juce::Point<float> p); // the body (a sheet: a solid pixel of its frame)
    static void drawDancer (juce::Graphics& g, const Dancer& d);
    bool windowed() const;                                // the dancers have windows of their own now
    void syncWindow (Dancer& d, bool windows);            // made, placed, shown / hidden as the dancer is
    void invalidate (Dancer& d, juce::Rectangle<float> r); // its window, or the stage's area r
    void showMenu (Dancer& d);
    void save (int channel, const dance::Settings& s);

    AudioEngine& engine;
    std::map<int, Dancer> dancers;
    double lastMs = 0.0, freeBeats = 0.0;
    bool wasPlaying = false;
    int scanCountdown = 0;
    int dragging = -1;
    bool hadWindows = false;
    juce::Point<int> dragOffset;
};

// The dance channel's settings: a live preview, the move, mirrored / shown, size, speed, the outfit colour, the keys,
// and "Load sprite sheet..." (FL's button of that name in Fruity Dance's editor)
std::unique_ptr<juce::Component> makeDanceEditor (AudioEngine& engine, int channel);
} // namespace lynx
