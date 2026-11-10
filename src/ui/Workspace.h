#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Workspace.h"
#include "ui/Icons.h"

namespace lynx
{
class PanelFrame;
class DetachedPanelWindow;

// The workspace desktop (docs/WORKFLOW_FL.md, P1 item 1): the Playlist, Channel Rack, Piano Roll,
// Mixer and Browser are floating panels inside the main window. Each has a title strip with detach,
// maximize and close; it is dragged by the strip (snapping to the other panels and the edges; Alt
// turns snapping off), resized by its edges, and double-clicking the strip maximizes it. A detached
// panel lives in its own native window (for a second monitor) until it is docked again.
//
// The layout model is core/Workspace.h; it is saved in %APPDATA%\LYNX\workspace.txt, and named
// layouts in %APPDATA%\LYNX\layouts\*.txt.
class Workspace final : public juce::Component
{
public:
    using Panel = workspace::Panel;

    Workspace();
    ~Workspace() override;

    // `content` is not owned (it must outlive the workspace).
    void setContent (Panel panel, juce::Component& content, const juce::String& title, Icon icon);
    void setTitle (Panel panel, const juce::String& title);
    void setTitleTooltip (Panel panel, const juce::String& tooltip);
    // A control shown in the title bar after the title (not owned), e.g. the Channel Rack's swing knob.
    void setTitleAccessory (Panel panel, juce::Component* accessory, int width);

    // F-key behaviour (see Layout::toggle). Returns true if the panel is visible afterwards.
    bool toggle (Panel panel);
    void show (Panel panel); // shows, brings to front and focuses
    void hide (Panel panel);
    void toggleMaximised (Panel panel);
    void setDetached (Panel panel, bool detached);

    bool isPanelVisible (Panel panel) const;
    bool isMaximised (Panel panel) const;
    bool isDetached (Panel panel) const;
    Panel getFocused() const noexcept { return layout.focused; }

    // Layouts
    void resetLayout();
    int bringBackOffscreen();
    juce::String getLayoutText() const;
    bool applyLayoutText (const juce::String& text); // false if unreadable
    static juce::File layoutsFolder();
    bool saveLayoutAs (const juce::String& name);
    bool loadLayout (const juce::File& file);

    // Persistence of the current layout in the user settings (off for --snapshot).
    void setPersistent (bool shouldPersist);
    void saveState();

    // True while one of the detached windows is the active window.
    bool isDetachedWindowActive() const;
    // Issue #62: after the main window came up (it was built hidden, behind the splash), the restored detached windows go
    // back to their saved place (window managers place new windows themselves) and above the main window.
    void restoreDetachedWindows();

    std::function<void (Panel)> onFocusChanged;
    // Round 183a: a panel is about to show (its content may first have to come back from a dock); nullptr = nothing.
    std::function<void (Panel)> onWillShow;
    // Round 183a: the panel's frame lets go of its content (it is docked elsewhere for a while); setContent gives it back.
    void releaseContent (Panel panel);
    std::function<bool (const juce::KeyPress&)> onKeyInDetachedWindow;
    std::function<void (Panel, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onTitleWheel;
    std::function<void (Panel, const juce::MouseEvent&)> onTitleRightClick;
    std::function<void()> onDefaultLayout;
    // Adds the panel's own items to its options menu (the button at the left of its title bar).
    std::function<void (Panel, juce::PopupMenu&)> onPanelMenu; // after the default layout was (re)built: fit panels to their content

    // Resizes a floating panel's content area to `contentWidth` x `contentHeight` (0 = keep that side),
    // keeping its top-left corner and staying inside the desktop. Ignored while maximized or detached.
    void fitPanelContent (Panel panel, int contentWidth, int contentHeight);
    // Where a panel is in the desktop (also while hidden).
    juce::Rectangle<int> getPanelBounds (Panel panel) const;

    // The Browser is the background of the desktop (docked, as in FL): the other panels always stay above it. The
    // Playlist comes to front when clicked, like FL's (round 168s, core/Workspace.h isBackground).
    static bool isBackgroundPanel (Panel panel) noexcept { return workspace::isBackground (panel); }

    void paint (juce::Graphics& g) override;
    void resized() override;

    // used by the frames
    void activate (Panel panel);
    void frameMoved (Panel panel, juce::Rectangle<int> bounds);
    std::vector<juce::Rectangle<int>> otherPanelBounds (Panel panel) const;
    void detachedWindowMoved (Panel panel, juce::Rectangle<int> screenBounds);
    // Screen bounds a detached window snaps to: the main window and the other detached panels.
    std::vector<juce::Rectangle<int>> otherWindowScreenBounds (Panel panel) const;

    static constexpr int titleHeight = 32; // round 170e3: room for 20 px tool icons in the editors (was 28)

    // Issue #116 (FL's docked plugin windows, developer switch ui/NativeChild.h): a panel that is a native child window
    // of the main window, so it can come in front of a docked plugin window. Slice 1: the Channel rack only.
    bool isNativePanel (Panel panel) const;
    juce::Component& getFrame (Panel panel) const; // (tests: the panel's frame)
    // (tests) a native panel (issue #116) fading in (+1) or out (−1) right now, 0 otherwise; and a fade's length
    // scaled (1 = ui::motion::panelFadeMs), for a look at a fade's frames
    int nativeFadeDirection (Panel panel) const;
    void setNativeFadeScale (double scale);
    // A floating panel's frame is placed in the desktop's coordinates whether it is a native child or a child component.
    void placeFrame (juce::Component& frame, juce::Rectangle<int> bounds);
    juce::Rectangle<int> frameBounds (const juce::Component& frame) const;

    // Docked plugin windows (issue #116): frames that float in the desktop as native child windows, above the background
    // panels; a click on a native panel brings that panel in front of them. Not owned; the frame calls removeDockedFrame
    // before it goes.
    void addDockedFrame (juce::Component& frame);
    void removeDockedFrame (juce::Component& frame);
    // A place for a new docked frame of that size: cascading from the top-left of the desktop's free area.
    juce::Rectangle<int> placeForDockedFrame (int width, int height) const;

private:
    struct PeerWatch;
    std::unique_ptr<PeerWatch> peerWatch;
    std::vector<juce::Component::SafePointer<juce::Component>> dockedFrames;
    void nativeToFront (Panel panel);
    void rehostNativeFrames(); // the main window's native window was (re)created or the desktop moved in it
    // the open / close fade of the panels that are native child windows (issue #116): the window's own alpha
    struct NativeFader;
    std::unique_ptr<NativeFader> nativeFader;
    void applyLayout (bool fadePanels = false); // fadePanels: a panel the user opened or closed fades (round 179a)
    void setFocusedPanel (Panel panel);

    workspace::Layout layout;
    bool layoutReady = false, persistent = false;
    // The layout is still LYNX's default (no panel moved or resized by hand, nothing detached or loaded):
    // when the window changes size it takes the default rects for the new size instead of scaling them,
    // so a window that opens small and is then maximised gets a sensible desktop.
    bool pristine = false;
    // The layout is still exactly the one restored from the settings (or loaded): when the window changes size it is
    // read again from that text for the new size instead of scaling the rects (the window is built at its design
    // size and then sized to the screen, and scaling twice let minimum sizes pile up, round 167v).
    juce::String restoredState;
    juce::String pendingState;
    std::array<std::unique_ptr<PanelFrame>, workspace::panelCount> frames;
    std::array<std::unique_ptr<DetachedPanelWindow>, workspace::panelCount> windows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Workspace)
};
} // namespace lynx
