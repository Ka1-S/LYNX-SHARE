#include "ui/ExternalDrag.h"

#include "core/Song.h"
#include "engine/PluginSandbox.h"
#include "ui/BrowserComponent.h"
#include "ui/ClipGridComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace juce
{
// cmake/LynxPatchJuce.cmake fix 35 (juce_DragAndDropContainer.cpp)
extern int (*lynxExternalDropAtHook) (Point<int> screenPos);
} // namespace juce

namespace lynx::externaldrag
{
#if JUCE_LINUX || JUCE_BSD
// ui/ExternalDragX11.cpp (a file of its own: X11's macros, Status, None, Bool..., clash with LYNX's names)
bool otherProgramAt (juce::Point<int> screenPos);
#endif

namespace
{
// LYNX's component at a screen point. On X11 JUCE's own lookup (Desktop::findComponentAt) asks each window whether a
// window of ours above it covers the point, and the drag image under the mouse (a window that lets the mouse through)
// counts: over LYNX's own windows it found nothing. Here windows that let the mouse through are passed over; other
// programs' windows were already ruled out (otherProgramAt). Elsewhere JUCE's lookup is right.
juce::Component* componentAt (juce::Point<int> screenPos)
{
#if JUCE_LINUX || JUCE_BSD
    auto& desktop = juce::Desktop::getInstance();
    for (int i = desktop.getNumComponents(); --i >= 0;)
    {
        auto* c = desktop.getComponent (i);
        if (c == nullptr || ! c->isVisible() || ! c->getScreenBounds().contains (screenPos))
            continue;
        if (auto* peer = c->getPeer(); peer != nullptr && (peer->getStyleFlags() & juce::ComponentPeer::windowIgnoresMouseClicks) != 0)
            continue;
        bool self = true, children = true;
        c->getInterceptsMouseClicks (self, children);
        if (! self && ! children)
            continue;
        if (auto* hit = c->getComponentAt (c->getLocalPoint (nullptr, screenPos)))
            return hit;
    }
    return nullptr;
#else
    return juce::Desktop::getInstance().findComponentAt (screenPos);
#endif
}

// (fix 35's answer: 0 LYNX, 1 outside the app, 2 a plugin's editor)
int hookAt (juce::Point<int> screenPos)
{
    switch (placeAt (screenPos))
    {
        case Place::pluginEditor: return 2;
        case Place::otherProgram: return 1;
        case Place::lynx: break;
    }
    return 0;
}
} // namespace

bool isHostedPluginEditor (const juce::Component* component)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
        if (auto* editor = dynamic_cast<const juce::AudioProcessorEditor*> (c))
        {
            if (dynamic_cast<const juce::GenericAudioProcessorEditor*> (editor) != nullptr)
                return false; // (LYNX's knobs for a plugin without an editor of its own)
            auto* plugin = dynamic_cast<juce::AudioPluginInstance*> (editor->getAudioProcessor());
            // a plugin in a separate process (or the 32-bit bridge) shows only LYNX's stand-in here: its window is the
            // other process's, which placeAt() sees as another program's window
            return plugin != nullptr && ! isSandboxedPlugin (*plugin);
        }
    return false;
}

Place placeAt (juce::Point<int> screenPos)
{
#if JUCE_LINUX || JUCE_BSD
    if (otherProgramAt (screenPos))
        return Place::otherProgram;
#endif
    // (on Windows and macOS JUCE's own hit test already leaves out a point where another program's window is in front;
    // MainComponent puts JUCE's list of LYNX's windows in screen order when a drag starts)
    auto* c = componentAt (screenPos);
    if (c == nullptr)
        return Place::otherProgram;
    return isHostedPluginEditor (c) ? Place::pluginEditor : Place::lynx;
}

const char* placeName (Place place)
{
    switch (place)
    {
        case Place::pluginEditor: return "plugin";
        case Place::otherProgram: return "other";
        case Place::lynx: break;
    }
    return "lynx";
}

juce::StringArray filesOf (const juce::var& description, const Song* song)
{
    // a Browser file (the tree, its search results) or Edison's drag button: "lynx-arquivo|<path>"
    const auto text = description.toString();
    const auto filePrefix = juce::String (BrowserComponent::dragDescription) + "|";
    if (text.startsWith (filePrefix))
    {
        const juce::File file (text.fromFirstOccurrenceOf ("|", false, false));
        return file.existsAsFile() ? juce::StringArray (file.getFullPathName()) : juce::StringArray();
    }

    // a Playlist audio clip: its sample
    if (int index = -1; song != nullptr && playlistClipDrag (description, index) && index >= 0 && index < (int) song->playlist.size())
    {
        const auto& clip = song->playlist[(std::size_t) index];
        if (clip.kind == ClipKind::audio && clip.channel >= 0 && clip.channel < (int) song->channels.size())
        {
            const auto& path = song->channels[(std::size_t) clip.channel].samplePath;
            if (! path.empty() && juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())))
                if (const juce::File file (juce::String::fromUTF8 (path.c_str())); file.existsAsFile())
                    return juce::StringArray (file.getFullPathName());
        }
    }
    return {};
}

bool shouldDropFiles (const juce::DragAndDropTarget::SourceDetails& details, const Song* song, juce::StringArray& files, bool& canMoveFiles)
{
    files = filesOf (details.description, song);
    canMoveFiles = false; // (a drop elsewhere copies: LYNX's files stay where they are)
    return ! files.isEmpty();
}

void install()
{
    juce::lynxExternalDropAtHook = hookAt;
}
} // namespace lynx::externaldrag
