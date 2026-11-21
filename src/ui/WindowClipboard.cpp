#include "ui/WindowClipboard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/LynxWindows.h"

namespace lynx::windowclip
{
namespace
{
juce::String& ownStamp()
{
    static juce::String stamp; // this window's last copy
    return stamp;
}

juce::String& projectName()
{
    static juce::String name;
    return name;
}

// the last copy any window made, for when the system clipboard is empty (X11: the window that copied is gone)
juce::File latestFile()
{
    return sideFolder().getChildFile ("latest.txt");
}

std::optional<crossclipboard::Envelope> waiting (crossclipboard::Kind kind)
{
    if (! enabled())
        return std::nullopt;
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    auto envelope = crossclipboard::envelopeOf (text);
    if (! envelope && text.isEmpty())
        envelope = crossclipboard::envelopeOf (latestFile().loadFileAsString());
    if (! envelope || envelope->kind != kind || envelope->stamp == ownStamp() || envelope->pid == windows::processId())
        return std::nullopt;
    if (envelope->file != juce::File() && ! envelope->file.existsAsFile())
        return std::nullopt;
    return envelope;
}
} // namespace

bool enabled()
{
    static const bool on = ! platform::testMode() || juce::SystemStats::getEnvironmentVariable ("LYNX_SYSTEM_CLIPBOARD", {}) == "1";
    return on;
}

juce::File sideFolder()
{
    return platform::settingsFolder().getChildFile ("Clipboard");
}

void setProjectName (const juce::String& name)
{
    projectName() = name;
}

void publish (crossclipboard::Payload payload)
{
    if (! enabled())
        return;
    payload.stamp = juce::Uuid().toString();
    payload.pid = windows::processId();
    payload.from = projectName();
    const auto text = crossclipboard::clipboardText (payload, sideFolder());
    if (text.isEmpty())
        return;
    ownStamp() = payload.stamp;
    juce::SystemClipboard::copyTextToClipboard (text);
    if (sideFolder().createDirectory())
        latestFile().replaceWithText (text);
}

std::optional<crossclipboard::Payload> incoming (crossclipboard::Kind kind)
{
    if (const auto envelope = waiting (kind))
        return crossclipboard::open (*envelope);
    return std::nullopt;
}

bool hasIncoming (crossclipboard::Kind kind)
{
    return waiting (kind).has_value();
}

void captureChannel (AudioEngine& engine, int channel, ChannelDef& into)
{
    if (into.instrument != InstrumentKind::plugin || into.plugin.playedBy != 0)
        return;
    if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
        if (auto state = p->captureState())
            into.plugin.state = std::move (*state);
}

void captureMixerTrack (AudioEngine& engine, int track, MixerTrack& into)
{
    for (std::size_t s = 0; s < into.effects.size(); ++s)
        if (into.effects[s].kind == EffectKind::plugin && into.effects[s].plugin.playedBy == 0)
            if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (track, (int) s).get()))
                if (auto state = p->captureState())
                    into.effects[s].plugin.state = std::move (*state);
}

juce::String pastedFrom (const crossclipboard::Payload& payload)
{
    return tr ("Pasted from {0}", { payload.from.isNotEmpty() ? payload.from : tr ("another LYNX SHARE window") });
}
} // namespace lynx::windowclip
