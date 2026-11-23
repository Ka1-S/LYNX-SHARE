#include "ui/FlmExportDialog.h"

#include <map>

#include "engine/Mp3Format.h"
#include "ui/FileChoosers.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
juce::String fromUtf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

FlmExportOptions optionsFor (const juce::File& target, std::shared_ptr<juce::AudioFormatManager> formats)
{
    FlmExportOptions o;
    o.title = target.getFileNameWithoutExtension().toStdString();
    o.targetFolder = target.getParentDirectory().getFullPathName().toStdString();
    o.nowSeconds = (double) juce::Time::currentTimeMillis() / 1000.0;
    o.resourceRoots = flmFactoryRoots();
    auto seen = std::make_shared<std::map<std::string, double>>();
    o.sampleSeconds = [formats, seen] (const std::string& path) {
        if (const auto it = seen->find (path); it != seen->end())
            return it->second;
        double seconds = 0.0;
        if (std::unique_ptr<juce::AudioFormatReader> reader { formats->createReaderFor (juce::File (fromUtf8 (path))) }; reader != nullptr && reader->sampleRate > 0)
            seconds = (double) reader->lengthInSamples / reader->sampleRate;
        seen->emplace (path, seconds);
        return seconds;
    };
    return o;
}

std::shared_ptr<juce::AudioFormatManager> audioFormats()
{
    auto formats = std::make_shared<juce::AudioFormatManager>();
    registerAudioFormats (*formats);
    return formats;
}
} // namespace

juce::String flmExportLossText (const FlmExportResult& result)
{
    juce::String sound, other;
    for (const auto& l : result.lost)
        (l.sound ? sound : other) << "- " << tr (l.text.c_str(), { fromUtf8 (l.detail) }).trimEnd() << "\n";
    juce::String text;
    if (sound.isNotEmpty())
        text << tr ("May sound different there:") << "\n" << sound << "\n";
    if (other.isNotEmpty())
        text << tr ("Left out (the sound stays):") << "\n" << other;
    if (text.isEmpty())
        text = tr ("Nothing is lost: the copy holds the whole project.");
    return text.trimEnd();
}

juce::AlertWindow* buildFlmExportWindow (const FlmExportResult& result, std::shared_ptr<juce::TextEditor> list)
{
    auto* w = new juce::AlertWindow (tr ("Save a copy as FL Studio Mobile project"),
                                     tr ("The copy opens in FL Studio Mobile, with its samples in a folder beside it. The open project stays as it is."),
                                     juce::MessageBoxIconType::NoIcon);
    list->setMultiLine (true);
    list->setReadOnly (true);
    list->setScrollbarsShown (true);
    list->setCaretVisible (false);
    list->setSize (560, 240);
    list->setText (flmExportLossText (result), false);
    w->addCustomComponent (list.get());
    w->addButton (tr ("Save copy..."), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    return w;
}

bool writeFlmCopy (const Song& song, const juce::File& target, FlmExportResult& result, juce::String& error)
{
    result = exportFlm (song, optionsFor (target, audioFormats()));
    if (! result.ok)
    {
        error = tr ("The FL Studio Mobile project could not be written");
        return false;
    }
    // the samples first (a copy that already holds the same file is left as it is), then the project
    int missing = 0;
    for (const auto& s : result.samples)
    {
        const juce::File from (fromUtf8 (s.source));
        const auto to = target.getParentDirectory().getChildFile (fromUtf8 (s.relative));
        if (! from.existsAsFile())
        {
            ++missing;
            continue;
        }
        if (to.existsAsFile() && to.getSize() == from.getSize() && to.hasIdenticalContentTo (from))
            continue;
        to.getParentDirectory().createDirectory();
        if (! from.copyFileTo (to))
        {
            error = tr ("Could not write {0}", { to.getFullPathName() });
            return false;
        }
    }
    if (missing > 0)
        result.lost.push_back ({ "Samples not found on this computer ({0}): their notes stay, FL Studio Mobile plays nothing for them", std::to_string (missing), true });
    juce::TemporaryFile temp (target);
    if (! temp.getFile().replaceWithData (result.bytes.data(), result.bytes.size()) || ! temp.overwriteTargetFileWithTemporary())
    {
        error = tr ("Could not write {0}", { target.getFullPathName() });
        return false;
    }
    return true;
}

void showFlmExportDialog (AudioEngine& engine, juce::Component* parent, const juce::File& suggestedFolder, const juce::String& projectName)
{
    engine.capturePluginStates();
    const auto stem = juce::File::createLegalFileName (projectName.isNotEmpty() ? projectName : juce::String ("LYNX SHARE"));
    // what the copy loses, worked out before anything is saved (the names of the copy's samples don't change it)
    const auto dryRun = exportFlm (engine.getSong(), optionsFor (suggestedFolder.getChildFile (stem + ".flm"), audioFormats()));
    auto list = std::make_shared<juce::TextEditor>();
    auto* w = buildFlmExportWindow (dryRun, list);
    juce::Component::SafePointer<juce::Component> safe (parent);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, list, &engine, safe, suggestedFolder, stem] (int choice) {
        juce::ignoreUnused (w, list);
        if (choice != 1)
            return;
        auto chooser = std::shared_ptr<juce::FileChooser> (
            ui::makeFileChooser (tr ("Save a copy as FL Studio Mobile project"), suggestedFolder.getChildFile (stem + ".flm"), "*.flm").release());
        ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                           [&engine, safe] (const juce::FileChooser& c) {
                               auto target = c.getResult();
                               if (target == juce::File())
                                   return;
                               if (! target.hasFileExtension ("flm"))
                                   target = target.withFileExtension ("flm");
                               engine.capturePluginStates();
                               FlmExportResult result;
                               juce::String error;
                               if (! writeFlmCopy (engine.getSong(), target, result, error))
                               {
                                   juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not save"), error, {},
                                                                           safe.getComponent());
                                   return;
                               }
                               ui::postHint ({ tr ("Saved a copy for FL Studio Mobile"), target.getFileName() }, 6000);
                           });
    }), true);
}
} // namespace lynx
