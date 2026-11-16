#include "ui/SamplerImportUi.h"

#include "core/NativeEngines.h"
#include "core/SamplerImport.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
juce::File fileOf (const std::string& path) { return juce::File (juce::String::fromUTF8 (path.c_str())); }

msimport::Access diskAccess (const juce::File& source)
{
    msimport::Access a;
    a.exists = [] (const std::string& p) { return juce::File::isAbsolutePath (juce::String::fromUTF8 (p.c_str())) && fileOf (p).existsAsFile(); };
    a.size = [] (const std::string& p) { const auto f = fileOf (p); return f.existsAsFile() ? (std::uint64_t) f.getSize() : (std::uint64_t) 0; };
    a.read = [] (const std::string& p, std::size_t limit) {
        const auto f = fileOf (p);
        std::vector<std::uint8_t> out;
        if (! f.existsAsFile())
            return out;
        juce::FileInputStream in (f);
        if (! in.openedOk())
            return out;
        const auto n = (std::size_t) std::min<juce::int64> (in.getTotalLength(), (juce::int64) limit);
        out.resize (n);
        if (n > 0 && in.read (out.data(), (int) n) != (int) n)
            out.clear();
        return out;
    };
    a.readAt = [] (const std::string& p, std::uint64_t at, std::size_t n) {
        std::vector<std::uint8_t> out;
        juce::FileInputStream in (fileOf (p));
        if (! in.openedOk() || ! in.setPosition ((juce::int64) at) || n > (std::size_t) std::numeric_limits<int>::max())
            return out;
        out.resize (n);
        if (in.read (out.data(), (int) n) != (int) n)
            out.clear();
        return out;
    };
    // the audio inside an imported file: %APPDATA%\LYNX\imported-samples\<file>\<name>-<hash>.wav (kept by content)
    const auto folder = PluginHost::settingsFolder().getChildFile ("imported-samples")
                            .getChildFile (juce::File::createLegalFileName (source.getFileNameWithoutExtension()));
    a.saveWav = [folder] (const std::vector<std::uint8_t>& wav, const std::string& name) -> std::string {
        folder.createDirectory();
        const auto hash = juce::String::toHexString ((juce::int64) juce::DefaultHashFunctions::generateHash (
            juce::String::createStringFromData (wav.data(), (int) std::min<std::size_t> (wav.size(), 65536)) + juce::String ((juce::int64) wav.size()),
            0x7FFFFFFF));
        const auto file = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (name.c_str())).substring (0, 80) + "-" + hash + ".wav");
        if (! file.existsAsFile() && ! file.replaceWithData (wav.data(), wav.size()))
            return {};
        return file.getFullPathName().toStdString();
    };
    a.findBelow = [] (const std::string& folder, const std::string& name) -> std::string {
        const auto dir = fileOf (folder);
        if (folder.empty() || ! dir.isDirectory())
            return {};
        const auto wanted = juce::String::fromUTF8 (name.c_str());
        int seen = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        {
            if (entry.getFile().getFileName().equalsIgnoreCase (wanted))
                return entry.getFile().getFullPathName().toStdString();
            if (++seen > 20000)
                break;
        }
        return {};
    };
    a.roots = flStudioAccess().roots;
    a.separator = (char) juce::File::getSeparatorChar();
    return a;
}

void showReport (const juce::File& file, const msimport::Report& r, bool ok)
{
    if (ok && r.skipped.empty() && r.missingSamples == 0)
        return;
    juce::String text;
    if (! ok)
        text << tr ("Nothing was imported.") << "\n\n" << juce::String::fromUTF8 (r.error.c_str());
    else
    {
        text << tr ("Imported as a DirectWave channel") << ": " << juce::String::fromUTF8 (r.name.c_str()) << " (" << r.zones << " "
             << tr ("zones") << ")";
        if (r.instruments > 1)
            text << "\n" << tr ("Instrument") << " " << (r.instrument + 1) << " / " << r.instruments;
        text << "\n\n" << tr ("Not brought over") << ":";
        for (const auto& s : r.skipped)
            text << "\n- " << juce::String::fromUTF8 (s.c_str());
    }
    juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                            tr ("Import") + " - " + file.getFileName(), text);
}

void place (AudioEngine& engine, ChannelDef def, int channel)
{
    int index = channel;
    bool into = false;
    {
        const auto& song = engine.getSong();
        into = channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].instrument == InstrumentKind::directwave;
    }
    engine.editSong ([&] (Song& s) {
        if (into)
        {
            auto& c = s.channels[(std::size_t) channel];
            c.name = def.name;
            c.zones = std::move (def.zones);
            c.modelParams = std::move (def.modelParams);
            c.pluginState = std::move (def.pluginState);
            return;
        }
        def.mixerTrack = s.claimFreeInsert();
        s.channels.push_back (std::move (def));
        index = (int) s.channels.size() - 1;
    });
    engine.setSelectedChannel (index);
}

void importInstrument (AudioEngine& engine, const juce::File& file, int instrument, int channel)
{
    const auto path = file.getFullPathName().toStdString();
    auto access = diskAccess (file);
    juce::Thread::launch ([&engine, file, path, access, instrument, channel] {
        msimport::Report report;
        auto def = msimport::importFile (path, access, report, instrument);
        juce::MessageManager::callAsync ([&engine, file, report, def = std::move (def), channel]() mutable {
            if (def)
                place (engine, std::move (*def), channel);
            showReport (file, report, def.has_value());
        });
    });
}
} // namespace

bool isSamplerInstrumentFile (const juce::File& file)
{
    return msimport::handles (file.getFileExtension().trimCharactersAtStart (".").toStdString());
}

void openSamplerInstrumentFile (AudioEngine& engine, const juce::File& file, int channel)
{
    if (! file.existsAsFile())
        return;
    const auto names = msimport::instrumentNames (file.getFullPathName().toStdString(), diskAccess (file));
    if (names.size() <= 1)
    {
        importInstrument (engine, file, 0, channel);
        return;
    }
    // a library with several instruments: which one
    juce::PopupMenu menu;
    menu.addSectionHeader (file.getFileName());
    for (std::size_t i = 0; i < names.size() && i < 500; ++i)
        menu.addItem ((int) i + 1, juce::String::fromUTF8 (names[i].c_str()));
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [&engine, file, channel] (int chosen) {
        if (chosen > 0)
            importInstrument (engine, file, chosen - 1, channel);
    });
}
} // namespace lynx
