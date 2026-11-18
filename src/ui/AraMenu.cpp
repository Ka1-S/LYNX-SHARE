#include "ui/AraMenu.h"

#include "platform/Paths.h"

#include "core/AraRegions.h"
#include "engine/AraHost.h"
#include "engine/AudioEngine.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/PluginWindow.h"
#include "ui/ValueControl.h"

namespace lynx::aramenu
{
namespace
{
void hint (const juce::String& text, int ms = 3500)
{
    ui::Hint h;
    h.text = text;
    ui::postHint (h, ms);
}

// the plugin's binary inside a VST3 bundle (a folder), or the file itself
juce::File binaryOf (const juce::File& file)
{
    if (! file.isDirectory())
        return file;
   #if JUCE_WINDOWS
    const auto arch = juce::String ("x86_64-win");
   #elif JUCE_MAC
    const auto arch = juce::String ("MacOS");
   #else
    const auto arch = juce::String ("x86_64-linux");
   #endif
    for (const auto& f : file.getChildFile ("Contents").getChildFile (arch).findChildFiles (juce::File::findFiles, false))
        return f;
    return {};
}

// whether the binary names VST3's ARA class category (the factory's class list is plain text in the module)
bool declaresAra (const juce::File& binary)
{
    juce::MemoryMappedFile mapped (binary, juce::MemoryMappedFile::readOnly);
    if (mapped.getData() == nullptr || mapped.getSize() == 0)
        return false;
    static const char needle[] = "ARA Main Factory Class";
    const auto* begin = static_cast<const char*> (mapped.getData());
    const auto* end = begin + mapped.getSize();
    return std::search (begin, end, needle, needle + sizeof (needle) - 1) != end;
}
} // namespace

bool openEditor (AudioEngine& engine, int clipIndex)
{
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return false;
    const auto& clip = song.playlist[(std::size_t) clipIndex];
    if (! ara::isAraChannel (song, clip.channel) || engine.getAraHost() == nullptr)
        return false;
    const auto& channel = song.channels[(std::size_t) clip.channel];
    auto* host = engine.getAraHost();
    auto* plugin = host->pluginOf (channel.uid);
    if (plugin == nullptr) // (not made yet, e.g. its sample was still loading: the host is asked once more)
    {
        engine.refreshAra();
        host = engine.getAraHost();
        plugin = host != nullptr ? host->pluginOf (channel.uid) : nullptr;
    }
    if (host == nullptr)
        return false;
    auto owner = host->ownerOf (channel.uid);
    if (plugin == nullptr || owner == nullptr)
    {
        platform::settingsFile ("ara.log").appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + " no instance for "
                                                       + juce::String::fromUTF8 (channel.name.c_str()) + ": " + host->lastError() + "\n");
        hint (tr ("{0} could not be started: {1}", { juce::String::fromUTF8 (channel.ara.pluginName.c_str()), host->lastError() }));
        return false;
    }
    platform::settingsFile ("ara.log").appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + " editor of "
                                                   + juce::String::fromUTF8 (channel.name.c_str()) + ": " + plugin->getName()
                                                   + (plugin->hasEditor() ? " (has an editor)" : " (no editor)") + "\n");
    PluginWindows::open (owner, *plugin, juce::String::fromUTF8 (channel.ara.pluginName.c_str()) + " - " + juce::String::fromUTF8 (channel.name.c_str()));
    host->showInEditor (channel.uid, { clip.uid });
    return true;
}

int findAraPlugins (AudioEngine& engine)
{
    auto& plugins = engine.getPluginHost();
    if (plugins.backgroundScan().running)
    {
        hint (tr ("A plugin scan is running: try again when it is done"));
        return 0;
    }
    juce::StringArray files;
    for (const auto& d : plugins.getKnownPlugins().getTypes())
        if (d.pluginFormatName == "VST3" && ! AraHost::isAra (d) && ! files.contains (d.fileOrIdentifier))
            if (declaresAra (binaryOf (juce::File (d.fileOrIdentifier))))
                files.add (d.fileOrIdentifier);
    if (files.isEmpty())
    {
        hint (AraHost::araPlugins (plugins).empty() ? tr ("No ARA plugin found among the scanned VST3 plugins") : tr ("Every ARA plugin is listed"));
        return 0;
    }
    for (const auto& f : files)
        plugins.forgetFile (f);
    plugins.startBackgroundScanOfFiles (files);
    hint (tr ("Scanning {0} ARA plugin(s)...", { juce::String (files.size()) }));
    return files.size();
}

juce::PopupMenu forClip (AudioEngine& engine, int clipIndex)
{
    juce::PopupMenu menu;
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return menu;
    const auto& clip = song.playlist[(std::size_t) clipIndex];
    if (clip.channel < 0 || clip.channel >= (int) song.channels.size() || song.channels[(std::size_t) clip.channel].instrument != InstrumentKind::audioClip)
        return menu;
    const auto ch = clip.channel;
    const auto& channel = song.channels[(std::size_t) ch];
    if (! AraHost::available())
    {
        menu.addItem (tr ("This build has no ARA hosting"), false, false, [] {});
        return menu;
    }
    const auto plugins = AraHost::araPlugins (engine.getPluginHost());
    for (const auto& d : plugins)
    {
        const auto id = d.createIdentifierString().toStdString();
        const auto on = channel.ara.pluginId == id;
        menu.addItem (d.name, true, on, [&engine, clipIndex, ch, id, name = d.name.toStdString(), on] {
            if (! on)
                engine.setChannelAra (ch, id, name);
            openEditor (engine, clipIndex);
        });
    }
    if (plugins.empty())
        menu.addItem (tr ("No ARA plugin found"), false, false, [] {});
    menu.addSeparator();
    if (channel.ara.active())
    {
        const auto name = juce::String::fromUTF8 (channel.ara.pluginName.c_str());
        menu.addItem (tr ("Open {0} editor", { name }), [&engine, clipIndex] { openEditor (engine, clipIndex); });
        menu.addItem (tr ("Stop editing in {0}", { name }), [&engine, ch] { engine.setChannelAra (ch, {}, {}); });
    }
    menu.addItem (tr ("Find ARA plugins"), [&engine] { findAraPlugins (engine); });
    return menu;
}
} // namespace lynx::aramenu
