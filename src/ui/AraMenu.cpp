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
   #elif defined(__aarch64__)
    const auto arch = juce::String ("aarch64-linux");
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
    // (round 190oq: the channel's own link, or its ARA insert's)
    const auto link = ara::effectiveLink (song, clip.channel);
    if (! link.active() || engine.getAraHost() == nullptr)
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
        hint (tr ("{0} could not be started: {1}", { juce::String::fromUTF8 (link.pluginName.c_str()), host->lastError() }));
        return false;
    }
    platform::settingsFile ("ara.log").appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + " editor of "
                                                   + juce::String::fromUTF8 (channel.name.c_str()) + ": " + plugin->getName()
                                                   + (plugin->hasEditor() ? " (has an editor)" : " (no editor)") + "\n");
    PluginWindows::open (owner, *plugin, juce::String::fromUTF8 (link.pluginName.c_str()) + " - " + juce::String::fromUTF8 (channel.name.c_str()));
    host->showInEditor (channel.uid, { clip.uid });
    return true;
}

bool isAraSlot (AudioEngine& engine, int track, int slot)
{
    const auto& song = engine.getSong();
    return AraHost::available() && slot >= 0 && ara::araInsertSlot (song, track) == slot;
}

bool openInsertEditor (AudioEngine& engine, int track)
{
    const auto& song = engine.getSong();
    const auto slot = ara::araInsertSlot (song, track);
    if (slot < 0 || ! AraHost::available())
        return false;
    const auto& ref = song.mixer[(std::size_t) track].effects[(std::size_t) slot].plugin;
    const auto name = juce::String::fromUTF8 ((ref.name.empty() ? ref.id : ref.name).c_str());
    const auto channels = ara::insertChannels (song, track);
    if (channels.empty())
    {
        hint (tr ("{0} (ARA): no audio clip channel is routed to this mixer track. Route one here and its clips play and are edited in {0}.", { name }), 6000);
        return true;
    }
    std::vector<std::uint64_t> uids;
    for (const auto c : channels)
        uids.push_back (song.channels[(std::size_t) c].uid);
    auto* host = engine.getAraHost();
    auto* plugin = host != nullptr ? host->pluginOf (uids.front()) : nullptr;
    if (plugin == nullptr)
    {
        engine.refreshAra();
        host = engine.getAraHost();
        plugin = host != nullptr ? host->pluginOf (uids.front()) : nullptr;
    }
    auto owner = host != nullptr ? host->ownerOf (uids.front()) : nullptr;
    if (plugin == nullptr || owner == nullptr)
    {
        hint (tr ("{0} could not be started: {1}", { name, host != nullptr ? host->lastError() : juce::String() }));
        return true;
    }
    const auto& insert = song.mixer[(std::size_t) track];
    const auto insertName = track == 0 ? tr ("Master") : (insert.name.empty() ? tr ("Insert {0}", { juce::String (track) }) : juce::String::fromUTF8 (insert.name.c_str()));
    PluginWindows::open (owner, *plugin, name + " - " + insertName);
    host->showChannelsInEditor (uids.front(), uids);
    return true;
}

void addSlotItems (juce::PopupMenu& menu, AudioEngine& engine, int track, int slot)
{
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
        return;
    const auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
    if (fx.kind != EffectKind::plugin || ! AraHost::available())
        return;
    const auto description = engine.getPluginHost().find (juce::String::fromUTF8 (fx.plugin.id.c_str()));
    if (description == nullptr || ! AraHost::isAra (*description))
        return;
    const auto routed = ara::routedAudioClipChannels (song, track);
    menu.addItem (tr ("Edit the audio clips routed here (ARA)") + (routed > 0 ? " (" + juce::String (routed) + ")" : juce::String()), true, fx.ara,
                  [&engine, track, slot, on = ! fx.ara] { setSlotAra (engine, track, slot, on); });
}

void setSlotAra (AudioEngine& engine, int track, int slot, bool on)
{
    engine.editSong ([track, slot, on] (Song& s) {
        if (track >= 0 && track < (int) s.mixer.size() && slot >= 0 && slot < (int) s.mixer[(std::size_t) track].effects.size())
            s.mixer[(std::size_t) track].effects[(std::size_t) slot].ara = on;
    });
    hintSlotAra (engine, track, slot, on);
}

void hintSlotAra (AudioEngine& engine, int track, int slot, bool on)
{
    const auto& song = engine.getSong();
    if (track < 0 || track >= (int) song.mixer.size() || slot >= (int) song.mixer[(std::size_t) track].effects.size())
        return;
    const auto& ref = song.mixer[(std::size_t) track].effects[(std::size_t) slot].plugin;
    const auto name = juce::String::fromUTF8 ((ref.name.empty() ? ref.id : ref.name).c_str());
    const auto routed = ara::routedAudioClipChannels (song, track);
    if (! on)
        hint (tr ("{0} is a plain insert effect again: the routed audio clips play unedited", { name }));
    else if (routed == 0)
        hint (tr ("{0} (ARA): no audio clip channel is routed to this mixer track. Route one here and its clips play and are edited in {0}.", { name }), 6000);
    else
        hint (tr ("{0} (ARA) plays and edits the audio clips of {1} channel(s) routed here", { name, juce::String (routed) }));
}

bool araByDefault (AudioEngine& engine, int track, const PluginRef& plugin)
{
    if (! AraHost::available() || track <= 0 || plugin.id.empty())
        return false;
    const auto description = engine.getPluginHost().find (juce::String::fromUTF8 (plugin.id.c_str()));
    const auto& song = engine.getSong();
    return description != nullptr && AraHost::isAra (*description) && ara::routedAudioClipChannels (song, track) > 0
           && ara::araInsertSlot (song, track) < 0;
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
    // round 190oq (issue #243): edited through an ARA insert on its mixer track
    if (const auto viaInsert = ara::insertLink (song, ch); viaInsert.active())
    {
        const auto name = juce::String::fromUTF8 (viaInsert.pluginName.c_str());
        menu.addItem (tr ("Edited in {0} on mixer track {1} (ARA insert)", { name, juce::String (channel.mixerTrack) }), false, true, [] {});
        menu.addItem (tr ("Open {0} editor", { name }), [&engine, clipIndex] { openEditor (engine, clipIndex); });
    }
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
