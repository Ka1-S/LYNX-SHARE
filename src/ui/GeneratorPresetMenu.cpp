#include "ui/GeneratorPresetMenu.h"

#include "ui/I18n.h"
#include "ui/PluginPresetMenu.h"

namespace lynx::genpresetui
{
namespace
{
juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// The first bytes of a file (enough to tell a generator preset by its first line)
std::string startOf (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return {};
    char buffer[64] {};
    const auto n = in.read (buffer, (int) sizeof (buffer));
    return n > 0 ? std::string (buffer, (std::size_t) n) : std::string();
}
} // namespace

juce::File root()
{
    return pluginpresetui::presetsRoot().getChildFile (genpresets::generatorsFolder);
}

juce::File folderFor (InstrumentKind kind)
{
    return root().getChildFile (utf8 (genpresets::folderName (kind)));
}

bool isPresetFile (const juce::File& file)
{
    return file.hasFileExtension ("lynxpreset") && file.existsAsFile() && genpresets::looksLikePreset (startOf (file));
}

std::optional<genpresets::Preset> read (const juce::File& file)
{
    if (! file.hasFileExtension ("lynxpreset") || file.getSize() > 256 * 1024 * 1024)
        return std::nullopt;
    auto preset = genpresets::fromText (file.loadFileAsString().toStdString());
    if (preset && preset->name.empty())
        preset->name = file.getFileNameWithoutExtension().toStdString();
    return preset;
}

juce::Array<juce::File> userPresets (InstrumentKind kind)
{
    juce::Array<juce::File> out;
    for (const auto& f : folderFor (kind).findChildFiles (juce::File::findFiles, false, "*.lynxpreset"))
        if (isPresetFile (f))
            out.add (f);
    std::sort (out.begin(), out.end(), [] (const juce::File& a, const juce::File& b) {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });
    return out;
}

bool save (const ChannelDef& channel, const juce::String& name, juce::File& written, juce::String& error)
{
    const auto folder = folderFor (channel.instrument);
    if (! folder.createDirectory())
    {
        error = tr ("The file could not be written.");
        return false;
    }
    const auto file = folder.getChildFile (utf8 (genpresets::fileNameFor (name.toStdString())) + genpresets::extension);
    if (! file.replaceWithText (utf8 (genpresets::toText (name.toStdString(), channel)), false, false, "\n"))
    {
        error = tr ("The file could not be written.");
        return false;
    }
    written = file;
    return true;
}

bool loadInto (AudioEngine& engine, int channel, const juce::File& file, juce::String& error)
{
    const auto preset = read (file);
    const auto& song = engine.getSong();
    if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
        return false;
    if (! preset)
    {
        error = tr ("This preset could not be read: {0}", { file.getFileNameWithoutExtension() });
        return false;
    }
    auto changed = genpresets::withPreset (song.channels[(std::size_t) channel], *preset);
    if (! changed)
    {
        error = tr ("This preset is for {0}.", { utf8 (genpresets::engineName (preset->kind)) });
        return false;
    }
    const auto kind = preset->kind;
    engine.editSong ([channel, kind, c = std::move (*changed)] (Song& s) {
        if (channel < (int) s.channels.size() && s.channels[(std::size_t) channel].instrument == kind)
            s.channels[(std::size_t) channel] = c;
    });
    return true;
}

bool addChannel (AudioEngine& engine, const juce::File& file, juce::String& error)
{
    const auto preset = read (file);
    if (! preset)
    {
        error = tr ("This preset could not be read: {0}", { file.getFileNameWithoutExtension() });
        return false;
    }
    engine.editSong ([&preset] (Song& s) {
        auto c = genpresets::newChannel (*preset);
        c.mixerTrack = s.claimFreeInsert();
        s.channels.push_back (std::move (c));
    });
    engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
    return true;
}

std::unique_ptr<juce::AlertWindow> makeNameBox (const juce::String& initial, juce::Component* owner)
{
    auto window = std::make_unique<juce::AlertWindow> (tr ("Save preset"), tr ("Name of the preset:"), juce::MessageBoxIconType::NoIcon, owner);
    window->addTextEditor ("name", initial.isEmpty() ? tr ("My preset") : initial);
    window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return window;
}

void askName (juce::Component& owner, const juce::String& initial, std::function<void (const juce::String&)> done)
{
    // the same box as the hosted plugins' and the mixer effects' "Save preset..." (ui/PluginPresetMenu, ui/PluginHeader)
    auto* window = makeNameBox (initial, &owner).release();
    juce::Component::SafePointer<juce::Component> safe (&owner);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, done = std::move (done)] (int result) {
        const auto name = window->getTextEditorContents ("name").trim();
        if (result != 1 || name.isEmpty() || safe == nullptr || ! done)
            return;
        done (name);
    }), true);
}

void reveal (const juce::File& folder)
{
    folder.createDirectory();
    folder.revealToUser();
}
} // namespace lynx::genpresetui
