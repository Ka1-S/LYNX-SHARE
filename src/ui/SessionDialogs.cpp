#include "ui/SessionDialogs.h"

#include <memory>

#include "core/SessionIds.h"
#include "core/SessionParts.h"
#include "core/StandIns.h"
#include "engine/AudioEngine.h"
#include "engine/ProjectFile.h"
#include "engine/SessionPack.h"
#include "platform/Paths.h"
#include "ui/I18n.h"

namespace lynx::sessionui
{
namespace
{
// stems are rendered at one rate on every PC (their hashes do not depend on the audio device)
constexpr double stemRate = 44100.0;

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

std::unique_ptr<juce::FileChooser>& chooser()
{
    static std::unique_ptr<juce::FileChooser> c;
    return c;
}

juce::File nameFile() { return platform::settingsFile ("session-name.txt"); }
juce::String myName()
{
    auto name = nameFile().loadFileAsString().trim();
    return name.isNotEmpty() ? name : juce::SystemStats::getFullUserName().trim();
}
void rememberName (const juce::String& name)
{
    nameFile().getParentDirectory().createDirectory();
    nameFile().replaceWithText (name.trim());
}

// each guest's area colour (the host's own things keep theirs)
std::uint32_t guestColour (int guest)
{
    static constexpr std::uint32_t colours[] = { 0xFF3D9BE9, 0xFF46C27A, 0xFFE0A030, 0xFFB46EE0 };
    return colours[(std::size_t) std::max (0, guest - 1) % 4];
}

void warn (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
}
void inform (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, title, text);
}

juce::String megabytes (juce::int64 bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"; }

// Renders the stand-ins `items` of `song` and writes the package — on a background thread behind a progress window.
// `make` turns the song (with its fresh stand-ins) into what is packed; `done` runs on the UI thread with the renders.
class PackThread final : public juce::ThreadWithProgressWindow
{
public:
    using Make = std::function<Song (const Song&)>;
    using Done = std::function<void (bool ok, const std::vector<sessionpack::Rendered>&, const sessionpack::WriteResult&, const juce::String& error)>;

    PackThread (AudioEngine& e, Song s, std::vector<sessionpack::RenderItem> i, juce::File stems, juce::String who, Make m,
                sessionpack::Manifest man, juce::File f, Done d)
        : juce::ThreadWithProgressWindow (tr ("Preparing the package..."), true, true),
          engine (e), song (std::move (s)), items (std::move (i)), stemFolder (std::move (stems)), by (std::move (who)), make (std::move (m)),
          manifest (std::move (man)), file (std::move (f)), done (std::move (d))
    {
    }

    void run() override
    {
        ok = sessionpack::render (song, items, stemRate, stemFolder, by, rendered,
                                  [this] (double p, const juce::String& what) {
                                      setProgress (p * 0.9);
                                      if (what.isNotEmpty())
                                          setStatusMessage (tr ("Rendering {0}...", { what }));
                                      return ! threadShouldExit();
                                  },
                                  error);
        if (! ok || threadShouldExit())
            return;
        setStatusMessage (tr ("Writing the package..."));
        setProgress (0.92);
        auto withStandIns = song;
        sessionpack::apply (withStandIns, rendered);
        ok = sessionpack::write (make (withStandIns), manifest, engine.getPluginHost(), file, result, error);
    }

    void threadComplete (bool cancelled) override
    {
        if (cancelled && error.isEmpty())
            error = tr ("Cancelled");
        done (ok && ! cancelled, rendered, result, error);
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    AudioEngine& engine;
    Song song;
    std::vector<sessionpack::RenderItem> items;
    juce::File stemFolder;
    juce::String by;
    Make make;
    sessionpack::Manifest manifest;
    juce::File file;
    Done done;
    std::vector<sessionpack::Rendered> rendered;
    sessionpack::WriteResult result;
    juce::String error;
    bool ok = false;
};

juce::String summaryOf (const sessionpack::WriteResult& r, const std::vector<sessionpack::Rendered>& rendered)
{
    int stems = 0;
    for (const auto& x : rendered)
        stems += (int) x.standIns.size();
    auto text = tr ("{0} files ({1}), {2} plugin renders.", { juce::String (r.media), megabytes (r.bytes), juce::String (stems) });
    if (r.duplicates > 0)
        text << " " << tr ("{0} repeated files stored once.", { juce::String (r.duplicates) });
    if (! r.missing.isEmpty())
        text << "\n\n" << tr ("Not on this PC (left out):") << "\n" << r.missing.joinIntoString ("\n");
    return text;
}

juce::File defaultFolder (const Context& c)
{
    return c.projectFile != juce::File() ? c.projectFile.getParentDirectory() : platform::documentsFolder().getChildFile ("LYNX/Projects");
}

juce::String projectTitle (const Context& c) { return c.projectName.isNotEmpty() ? c.projectName : tr ("Untitled"); }

void writePackage (Context c, const juce::String& title, std::vector<sessionpack::RenderItem> items, PackThread::Make make, sessionpack::Manifest manifest,
                   const juce::File& file, std::function<void (const sessionpack::WriteResult&, const std::vector<sessionpack::Rendered>&)> after)
{
    c.engine.capturePluginStates();
    const auto& song = c.engine.getSong();
    auto* thread = new PackThread (c.engine, song, std::move (items), sessionpack::standInFolder (c.projectFile, song), myName(), std::move (make),
                                   std::move (manifest), file,
                                   [c, title, after] (bool ok, const std::vector<sessionpack::Rendered>& rendered, const sessionpack::WriteResult& result,
                                                      const juce::String& error) {
                                       if (! rendered.empty()) // the project keeps its renders (the next package reuses them)
                                           c.engine.editSong ([&rendered] (Song& s) { sessionpack::apply (s, rendered); }, false);
                                       if (! ok)
                                       {
                                           warn (title, tr ("Could not write the package:") + "\n" + error);
                                           return;
                                       }
                                       after (result, rendered);
                                   });
    thread->launchThread();
}
} // namespace

void sendProject (Context c)
{
    const auto& song = c.engine.getSong();
    if (song.session.isGuestCopy())
    {
        const auto* host = song.session.find (session::hostTag);
        warn (tr ("Send project"), tr ("This is a copy of {0}'s project. Use File › Send my part back... to send them what you made.",
                                       { host != nullptr ? str (host->name) : tr ("the host") }));
        return;
    }
    const auto guests = std::max (0, (int) song.session.participants.size() - 1);
    auto* w = new juce::AlertWindow (tr ("Send project"),
                                     tr ("Your friend opens the package in their LYNX SHARE, records their part and sends it back to you "
                                         "(File › Merge a part...). Up to {0} people in one project, you included.",
                                         { juce::String (session::maxParticipants) }),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("me", myName(), tr ("Your name"));
    w->addTextEditor ("friend", {}, tr ("Your friend's name"));
    w->addComboBox ("keep", { tr ("Only their own part (recommended)"), tr ("A full copy of the project") }, tr ("When the session ends, your friend keeps"));
    w->addTextBlock (tr ("Parts that use a third-party plugin are rendered into the package: they play on their PC even without the plugin."));
    w->addButton (tr ("Save package..."), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([c, w, guests] (int result) {
                            if (result != 1)
                                return;
                            const auto me = w->getTextEditorContents ("me").trim();
                            const auto guest = w->getTextEditorContents ("friend").trim();
                            const auto keepFull = w->getComboBoxComponent ("keep")->getSelectedItemIndex() == 1;
                            if (guest.isEmpty())
                            {
                                warn (tr ("Send project"), tr ("Type your friend's name: their part comes back under it."));
                                return;
                            }
                            rememberName (me);
                            // invite him (his tag, his inserts and Playlist rows): saved with the project
                            const auto known = [&] {
                                for (const auto& p : c.engine.getSong().session.participants)
                                    if (p.tag != session::hostTag && str (p.name).equalsIgnoreCase (guest))
                                        return true;
                                return false;
                            }();
                            std::optional<SessionParticipant> invited;
                            c.engine.editSong ([&] (Song& s) {
                                uids::normalize (s);
                                if (auto* p = parts::invite (s, me.toStdString(), guest.toStdString(), known ? 0u : guestColour (guests + 1), keepFull))
                                    invited = *p;
                            }, false);
                            c.engine.markModified();
                            if (! invited)
                            {
                                warn (tr ("Send project"), tr ("This project already has {0} people (the most in one session).",
                                                               { juce::String (session::maxParticipants) }));
                                return;
                            }
                            const auto start = defaultFolder (c).getChildFile (
                                juce::File::createLegalFileName (projectTitle (c) + " - " + tr ("for {0}", { guest })) + sessionpack::extension);
                            chooser() = std::make_unique<juce::FileChooser> (tr ("Send project"), start, juce::String ("*") + sessionpack::extension);
                            chooser()->launchAsync (
                                juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                [c, guest, invited] (const juce::FileChooser& fc) {
                                    auto file = fc.getResult();
                                    if (file == juce::File())
                                        return;
                                    file = file.withFileExtension (sessionpack::extension);
                                    const auto& song = c.engine.getSong();
                                    sessionpack::Manifest m;
                                    m.projectName = projectTitle (c);
                                    if (const auto* host = song.session.find (session::hostTag))
                                        m.from = *host;
                                    m.to = *invited;
                                    const auto tag = invited->tag;
                                    writePackage (c, tr ("Send project"), sessionpack::planRenders (song, c.engine.getPluginHost(), 0),
                                                  [tag] (const Song& s) { return parts::guestCopy (s, tag); }, m, file,
                                                  [file, guest] (const sessionpack::WriteResult& r, const std::vector<sessionpack::Rendered>& rendered) {
                                                      inform (tr ("Send project"),
                                                              tr ("{0} is ready. Send it to {1} (e-mail, a shared cloud folder, a USB stick...).",
                                                                  { file.getFileName(), guest })
                                                                  + "\n\n" + summaryOf (r, rendered));
                                                      if (! platform::testMode())
                                                          file.revealToUser();
                                                  });
                                });
                        }),
                        true);
}

void sendPartBack (Context c)
{
    const auto& song = c.engine.getSong();
    const auto* self = song.session.self();
    if (! song.session.isGuestCopy() || self == nullptr)
    {
        warn (tr ("Send my part back"), tr ("Only a project you received (a .lynxpack) has a part to send back. To invite someone, use File › Send project..."));
        return;
    }
    const auto* host = song.session.find (session::hostTag);
    const auto hostName = host != nullptr ? str (host->name) : tr ("the host");
    const auto start = platform::documentsFolder().getChildFile (
        juce::File::createLegalFileName (projectTitle (c) + " - " + tr ("{0}'s part", { str (self->name) })) + sessionpack::extension);
    chooser() = std::make_unique<juce::FileChooser> (tr ("Send my part back"), start, juce::String ("*") + sessionpack::extension);
    chooser()->launchAsync (
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [c, hostName] (const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            file = file.withFileExtension (sessionpack::extension);
            const auto& song = c.engine.getSong();
            const auto me = song.session.me;
            sessionpack::Manifest m;
            m.part = true;
            m.projectName = projectTitle (c);
            m.from = *song.session.self();
            const auto keepFull = m.from.keepFull;
            writePackage (c, tr ("Send my part back"), sessionpack::planRenders (song, c.engine.getPluginHost(), me),
                          [me] (const Song& s) { return parts::extract (s, me); }, m, file,
                          [c, file, hostName, keepFull] (const sessionpack::WriteResult& r, const std::vector<sessionpack::Rendered>& rendered) {
                              auto text = tr ("{0} is ready. Send it to {1}, who adds it with File › Merge a part...", { file.getFileName(), hostName })
                                          + "\n\n" + summaryOf (r, rendered);
                              if (! keepFull)
                              {
                                  // the host's choice (a courtesy, not a lock): this copy keeps only what he made
                                  c.engine.editSong ([] (Song& s) { s = parts::keepOnlyPart (s); }, false);
                                  c.engine.markModified();
                                  text << "\n\n" << tr ("{0} asked that you keep only your own part: the rest of the project was removed from your copy.",
                                                        { hostName });
                              }
                              inform (tr ("Send my part back"), text);
                              if (! platform::testMode())
                                  file.revealToUser();
                          });
        });
}

namespace
{
void mergeFile (Context c, const juce::File& file, const sessionpack::Manifest& manifest)
{
    const auto title = tr ("Merge a part");
    const auto& song = c.engine.getSong();
    if (song.session.isGuestCopy())
    {
        warn (title, tr ("Parts are merged by the host, in the host's project. This is a copy you received."));
        return;
    }
    const auto* guest = song.session.find (manifest.from.tag);
    if (manifest.projectId.toStdString() != song.projectId || guest == nullptr || manifest.from.tag == session::hostTag)
    {
        warn (title, tr ("This part was made in another project. Open the project you sent to {0}, then merge it.", { str (manifest.from.name) }));
        return;
    }
    juce::String error;
    auto opened = sessionpack::open (file, sessionpack::receivedRoot().getChildFile ("Parts"), error);
    if (! opened)
    {
        warn (title, error);
        return;
    }
    auto part = std::move (opened->song);
    if (! sessionpack::adoptMedia (part, sessionpack::sessionMediaFolder (c.projectFile, song, str (manifest.from.name)), error))
    {
        warn (title, error);
        return;
    }
    const auto plugins = sessionpack::resolvePlugins (part, manifest, c.engine.getPluginHost(), false);
    opened->folder.deleteRecursively(); // (its media were copied next to the project)

    parts::MergeReport report;
    c.engine.editSong ([&] (Song& s) { report = parts::merge (s, part, manifest.from.tag); });
    if (! report.ok)
    {
        warn (title, str (report.error));
        return;
    }
    auto text = tr ("{0}'s part is in the project: {1} channels, {2} patterns, {3} notes, {4} Playlist clips, {5} inserts. Undo takes it out again.",
                    { str (report.guest), juce::String (report.channels), juce::String (report.patterns), juce::String (report.notes),
                      juce::String (report.clips), juce::String (report.inserts) });
    if (report.removed > 0)
        text << "\n" << tr ("{0} things deleted since the last merge were removed.", { juce::String (report.removed) });
    if (report.rowShift > 0 || ! report.movedInserts.empty())
        text << "\n" << tr ("Some of their inserts or Playlist rows were in use here: they moved to free ones.");
    if (! plugins.missing.isEmpty() || ! plugins.otherVersion.isEmpty())
        text << "\n\n" << tr ("Plugins not installed here (their renders play):") << "\n"
             << juce::StringArray (plugins.missing).joinIntoString ("\n") << (plugins.otherVersion.isEmpty() ? "" : "\n")
             << plugins.otherVersion.joinIntoString ("\n");
    if (! opened->corrupt.isEmpty())
        text << "\n\n" << tr ("Damaged in the transfer (ask for the package again):") << "\n" << opened->corrupt.joinIntoString ("\n");
    if (platform::testMode())
        platform::testModeLog ("session part merged: " + text.replaceCharacter ('\n', ' '));
    inform (title, text);
}

void openReceived (Context c, const juce::File& file, const sessionpack::Manifest& manifest, bool useOtherVersions)
{
    const auto title = tr ("Open a received project");
    juce::String error;
    auto opened = sessionpack::open (file, sessionpack::receivedRoot(), error);
    if (! opened)
    {
        warn (title, error);
        return;
    }
    const auto plugins = sessionpack::resolvePlugins (opened->song, manifest, c.engine.getPluginHost(), useOtherVersions);
    if (! plugins.otherVersion.isEmpty() && ! useOtherVersions)
    {
        auto* w = new juce::AlertWindow (title,
                                         tr ("These plugins are another version on this PC:") + "\n" + plugins.otherVersion.joinIntoString ("\n") + "\n\n"
                                             + tr ("Play the renders that came with the project (sounds as sent), or use your version (can sound different)?"),
                                         juce::MessageBoxIconType::QuestionIcon);
        w->addButton (tr ("Play the renders"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Use my version"), 2);
        auto kept = std::make_shared<sessionpack::Opened> (std::move (*opened));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([c, file, manifest, kept] (int r) {
                                if (r == 1) // as with a missing plugin: its stand-in plays
                                {
                                    juce::String e;
                                    if (! ProjectFile::save (kept->song, kept->project, e))
                                        warn (tr ("Open a received project"), e);
                                    else
                                        c.openProject (kept->project);
                                }
                                else
                                {
                                    kept->folder.deleteRecursively();
                                    if (r == 2)
                                        openReceived (c, file, manifest, true);
                                }
                            }),
                            true);
        return;
    }
    if (! ProjectFile::save (opened->song, opened->project, error))
    {
        warn (title, error);
        return;
    }
    c.openProject (opened->project);

    juce::String text;
    if (! plugins.missing.isEmpty())
        text << tr ("Not installed here (their renders play, marked R):") << "\n" << plugins.missing.joinIntoString ("\n");
    if (! opened->corrupt.isEmpty())
        text << (text.isEmpty() ? "" : "\n\n") << tr ("Damaged in the transfer (ask for the package again):") << "\n" << opened->corrupt.joinIntoString ("\n");
    if (! manifest.missing.isEmpty())
        text << (text.isEmpty() ? "" : "\n\n") << tr ("Not on the sender's PC (left out):") << "\n" << manifest.missing.joinIntoString ("\n");
    if (platform::testMode())
        platform::testModeLog ("session project opened: " + opened->project.getFullPathName() + " " + text.replaceCharacter ('\n', ' '));
    if (text.isNotEmpty())
        inform (title, text);
}
} // namespace

void mergePart (Context c)
{
    if (c.engine.getSong().session.isGuestCopy())
    {
        warn (tr ("Merge a part"), tr ("Parts are merged by the host, in the host's project. This is a copy you received."));
        return;
    }
    chooser() = std::make_unique<juce::FileChooser> (tr ("Merge a part"), platform::documentsFolder(), juce::String ("*") + sessionpack::extension);
    chooser()->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [c] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (! file.existsAsFile())
            return;
        juce::String error;
        const auto manifest = sessionpack::readManifest (file, error);
        if (! manifest)
            warn (tr ("Merge a part"), error);
        else if (! manifest->part)
            warn (tr ("Merge a part"), tr ("This package is a whole project, not a part. Open it with File › Open..."));
        else
            mergeFile (c, file, *manifest);
    });
}

void openPackage (Context c, const juce::File& file)
{
    juce::String error;
    const auto manifest = sessionpack::readManifest (file, error);
    if (! manifest)
    {
        warn (tr ("Could not open"), error);
        return;
    }
    if (manifest->part)
    {
        mergeFile (c, file, *manifest);
        return;
    }
    auto text = tr ("{0} sent you \"{1}\".", { str (manifest->from.name.empty() ? std::string ("?") : manifest->from.name), manifest->projectName }) + "\n\n"
                + tr ("Record your part on your own channels, inserts (from {0}) and Playlist tracks (from {1}), then use File › Send my part back...",
                      { juce::String (manifest->to.insertFirst), juce::String (manifest->to.rowFirst + 1) })
                + "\n\n"
                + (manifest->to.keepFull ? tr ("When the session ends you may keep a full copy of the project.")
                                         : tr ("When the session ends you keep your own part (the host's choice)."));
    if (manifest->lynxVersion != JUCE_APPLICATION_VERSION_STRING)
        text << "\n\n" << tr ("Made with LYNX SHARE {0} (this is {1}).", { manifest->lynxVersion, JUCE_APPLICATION_VERSION_STRING });
    auto* w = new juce::AlertWindow (tr ("Open a received project"), text, juce::MessageBoxIconType::NoIcon);
    w->addButton (tr ("Open"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([c, file, m = *manifest] (int r) {
                            if (r == 1)
                                openReceived (c, file, m, false);
                        }),
                        true);
}
} // namespace lynx::sessionui
