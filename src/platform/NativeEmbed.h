#pragma once

// A native child view for a plugin's own editor (CLAP, VST2): the one place that knows what a "parent window" is on
// each OS (docs/PLATFORMS.md).
//   Windows  a child HWND (STATIC) inside the JUCE window, shown through juce::HWNDComponent
//   macOS    a plain NSView, shown through juce::NSViewComponent
//   Linux    an X11 window created by juce::XEmbedComponent (the plugin reparents its own window into it)

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace lynx::platform
{
class NativeEmbed final : public juce::Component
{
public:
    NativeEmbed();
    ~NativeEmbed() override;

    // Creates the native view once the component is inside a window on screen; false until then (call again from a
    // timer). Safe to call again after it succeeded.
    bool create();
    bool isCreated() const noexcept;

    // The native parent for the plugin: HWND (Windows), NSView* (macOS), X11 Window id (Linux).
    void* nativeHandle() const noexcept;
    unsigned long x11Window() const noexcept;

    // The CLAP window API this platform uses ("win32", "cocoa", "x11"; matches CLAP_WINDOW_API_*).
    static const char* clapWindowApi() noexcept;

    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Round 190oh (P3, FL's wrapper Settings › GUI): how a hosted editor's reported size maps onto LYNX's window. The UI
// calls setEditorScaling right after making an editor (with either option on): it marks the editor and calls its
// setScaleFactor (1.0f), so that it sizes itself again —
//   dpiUnaware: the editor and its window were made DPI-unaware (Windows draws them at 96 dpi and stretches them): the
//               plugin is told scale 1 and its size is in the window's own pixels;
//   scaleDims:  "Scale editor dimensions": the size the plugin reports is taken in logical pixels.
// JUCE's VST3 window reads "lynxScaleEditorDimensions" (cmake/LynxPatchJuce.cmake, fix 47); LYNX's CLAP and VST2
// editors use the two functions below. An editor that was never marked is sized as before.
void setEditorScaling (juce::Component& editor, bool dpiUnaware, bool scaleDims);
// the scale a reported editor size is divided by (the primary display's; 1 for a marked editor)
double editorSizeScale (const juce::Component& editor);
// the scale the plugin is told (the primary display's; 1 for a DPI-unaware editor)
double editorContentScale (const juce::Component& editor);
// Tests (--wrappershot190oh): how many of the window `nativeWindow` and the windows inside it are DPI-unaware, and how
// many there are (`windows`); -1 where there is no such thing (not Windows, no window)
int dpiUnawareWindowCount (void* nativeWindow, int& windows);
} // namespace lynx::platform
