#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
struct Song;

// Issue #174 (FL: a sample dragged from the Browser onto Serum's or Chan Sampler's own editor reaches the plugin): a
// drag that starts inside LYNX (the Browser, Edison's drag button) is LYNX's own drag (juce::DragAndDropContainer) while
// it stays over LYNX's panels, so their drop targets keep working. Over a hosted plugin's own editor (a native window of
// the plugin inside a LYNX window: Windows HWND, X11 window through XEmbed, NSView) or over another program's window
// (a plugin running in a separate process or in the 32-bit bridge, a file manager), it goes on as the system's file
// drag (OLE on Windows, XDND on X11, NSDragging on macOS), which the plugin's own drop target receives.
// The JUCE side is cmake/LynxPatchJuce.cmake fixes 35 (the hook below) and 36 (XDND reaches an embedded plugin window).
namespace externaldrag
{
enum class Place
{
    lynx,         // one of LYNX's own components (a drop target of LYNX, or nothing that takes drops)
    pluginEditor, // the editor of a plugin hosted inside LYNX (VST3, VST2, CLAP, LV2, AU)
    otherProgram  // not a LYNX window: another program, a plugin's window in a separate process, the desktop
};

// What is at a screen point (LYNX's logical screen coordinates) during a drag.
Place placeAt (juce::Point<int> screenPos);
const char* placeName (Place place); // "lynx", "plugin", "other" (test reports)

// Whether `component` is (inside) the editor of a plugin hosted in this process: a juce::AudioProcessorEditor of a
// hosted juce::AudioPluginInstance, not the generic editor of a plugin without one and not the stand-in of a plugin
// running in another process.
bool isHostedPluginEditor (const juce::Component* component);

// The files a LYNX drag takes along when it leaves LYNX: a Browser / Edison sample file, a Playlist audio clip's sample
// (`song` may be null: then no clip). Empty: the drag has no file (a plugin, a pattern, a clip of the grid) and stays
// LYNX's own.
juce::StringArray filesOf (const juce::var& description, const Song* song);

// For juce::DragAndDropContainer::shouldDropFilesWhenDraggedExternally (the main window, the Sample browser window):
// the drag's files, copied (never moved).
bool shouldDropFiles (const juce::DragAndDropTarget::SourceDetails& details, const Song* song, juce::StringArray& files, bool& canMoveFiles);

// Sets JUCE's `lynxExternalDropAtHook` (fix 35) to placeAt(): outside LYNX the drag becomes the system's file drag,
// over a plugin's editor after 300 ms, elsewhere after JUCE's 700 ms. Message thread; safe to call again.
void install();
} // namespace externaldrag
} // namespace lynx
