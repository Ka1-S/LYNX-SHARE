#include "engine/FlStudio.h"

#include "core/FlInstall.h"
#include "core/FlpImport.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace lynx
{
namespace
{
juce::String registryValue (const juce::String& key)
{
   #if JUCE_WINDOWS
    for (const auto* root : { "HKEY_CURRENT_USER\\", "HKEY_LOCAL_MACHINE\\" })
        if (auto value = juce::WindowsRegistry::getValue (root + key).trim(); value.isNotEmpty())
            return value;
   #else
    juce::ignoreUnused (key);
   #endif
    return {};
}

#if JUCE_LINUX || JUCE_BSD
// drive C: of the Wine prefix ($WINEPREFIX, else ~/.wine), where a Windows FL Studio would be installed on Linux
juce::File wineDriveC()
{
    const auto prefix = juce::SystemStats::getEnvironmentVariable ("WINEPREFIX", {});
    const auto root = prefix.isNotEmpty() && juce::File::isAbsolutePath (prefix) ? juce::File (prefix)
                                                                                 : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".wine");
    return root.getChildFile ("drive_c");
}
#endif

// The "FL Studio*" folders that may hold an installation (each must contain Data to count). FL data is only
// looked for where it can exist: Program Files on Windows, the app bundles on macOS (layout not confirmed), a Wine
// prefix on Linux.
juce::Array<juce::File> flStudioCandidates()
{
    juce::Array<juce::File> dirs;
   #if JUCE_WINDOWS
    const auto imageLine = juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Image-Line");
    dirs.addArray (imageLine.findChildFiles (juce::File::findDirectories, false, "FL Studio*"));
   #elif JUCE_MAC
    for (const auto& app : juce::File ("/Applications").findChildFiles (juce::File::findDirectories, false, "FL Studio*.app"))
        dirs.add (app.getChildFile ("Contents/Resources/FL"));
   #elif JUCE_LINUX || JUCE_BSD
    for (const auto* programs : { "Program Files/Image-Line", "Program Files (x86)/Image-Line" })
        dirs.addArray (wineDriveC().getChildFile (programs).findChildFiles (juce::File::findDirectories, false, "FL Studio*"));
   #endif
    return dirs;
}

juce::File existingFolder (const juce::String& path)
{
    if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
        return {};
    const juce::File folder (path);
    return folder.isDirectory() ? folder : juce::File();
}
} // namespace

namespace
{
// Data next to the installation's program (its name ignoring case: a Windows drive mounted on Linux)
bool hasData (const juce::File& install)
{
    if (install == juce::File())
        return false;
    if (install.getChildFile ("Data").isDirectory())
        return true;
   #if JUCE_WINDOWS
    return false;
   #else
    return juce::File (juce::String::fromUTF8 (existingPathIgnoringCase (install.getChildFile ("Data").getFullPathName().toStdString()).c_str())).isDirectory();
   #endif
}

juce::File settingFile() { return platform::settingsFile ("fl-studio-folder.txt"); }
} // namespace

juce::String flFolderSetting() { return settingFile().loadFileAsString().trim(); }

bool setFlFolderSetting (const juce::File& folder)
{
    const auto file = settingFile();
    const auto saved = folder == juce::File() ? (! file.existsAsFile() || file.deleteFile())
                                              : (file.getParentDirectory().createDirectory().wasOk() && file.replaceWithText (folder.getFullPathName()));
    refreshFlStudioAccess();
    return saved;
}

juce::String flFolderVariable() { return juce::SystemStats::getEnvironmentVariable ("LYNX_FL_INSTALL", {}).trim(); }

juce::StringArray detectFlFolders()
{
    juce::StringArray out;
    if (const auto folder = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Install path")); hasData (folder))
        out.add (folder.getFullPathName());
    for (const auto& dir : flStudioCandidates())
        if (hasData (dir))
            out.addIfNotAlreadyThere (dir.getFullPathName());
    for (const auto& dir : flinstall::onMountedDrives())
        out.addIfNotAlreadyThere (juce::String::fromUTF8 (dir.c_str()));
    return out;
}

namespace
{
// looks at the disk and the registry: may take as long as the slowest drive
FlStudioInstall probeFlStudio()
{
    // test hook (round 190rh): a drive that answers late
    if (const auto slow = juce::SystemStats::getEnvironmentVariable ("LYNX_TEST_SLOW_FL_MS", {}).getIntValue(); slow > 0)
        juce::Thread::sleep (slow);
    FlStudioInstall fl;
    // test hook (round 169g): LYNX_NO_FL=1 = a computer without FL Studio, even where it is installed (0 / empty = FL
    // as found; round 169g2)
    if (const auto noFl = juce::SystemStats::getEnvironmentVariable ("LYNX_NO_FL", {}).trim(); noFl.isNotEmpty() && noFl != "0")
        return fl;

    // installation: where FL says it is, else the newest "FL Studio*" under Program Files\Image-Line
    // LYNX_FL_INSTALL=<folder> points at an installation anywhere (another Wine prefix, a copy, tests)
    fl.installDir = existingFolder (flFolderVariable());
    fl.source = FlStudioInstall::Source::variable;
    if (! hasData (fl.installDir)) // Settings › Files › FL Studio folder (round 190rh, issue #291)
    {
        fl.installDir = existingFolder (flFolderSetting());
        fl.source = FlStudioInstall::Source::setting;
    }
    if (! hasData (fl.installDir))
    {
        fl.installDir = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Install path"));
        fl.source = FlStudioInstall::Source::detected;
    }
    if (! hasData (fl.installDir))
    {
        fl.installDir = juce::File();
        for (const auto& dir : flStudioCandidates())
            if (hasData (dir) && dir.getFileName() > fl.installDir.getFileName())
                fl.installDir = dir;
        if (fl.installDir == juce::File())
            fl.source = FlStudioInstall::Source::none;
    }

    // test hook: LYNX_FL_USER_DATA=<folder> stands in for FL's user data folder (synthetic plugin databases)
    if (const auto testFolder = existingFolder (juce::SystemStats::getEnvironmentVariable ("LYNX_FL_USER_DATA", {})); testFolder.exists())
        fl.userData = testFolder;

    // user data: "Shared data" (e.g. ...\Documents\Image-Line\, possibly under OneDrive) + "FL Studio"
    fl.sharedData = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Shared data"));
    if (! fl.userData.isDirectory())
        if (const auto user = fl.sharedData.getChildFile ("FL Studio"); fl.sharedData.isDirectory() && user.isDirectory())
            fl.userData = user;
    if (! fl.userData.isDirectory())
        if (const auto user = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line/FL Studio"); user.isDirectory())
            fl.userData = user;
   #if JUCE_LINUX || JUCE_BSD
    // FL Studio under Wine keeps its user data in the prefix's Documents
    if (! fl.userData.isDirectory())
        for (const auto& users : { wineDriveC().getChildFile ("users/" + juce::SystemStats::getLogonName()), wineDriveC().getChildFile ("users/steamuser") })
            if (const auto user = users.getChildFile ("Documents/Image-Line/FL Studio"); user.isDirectory())
            {
                fl.userData = user;
                break;
            }
   #endif

    // Shared data without FL's key (a Wine prefix, a test folder): the folder that holds the user data folder
    if (! fl.sharedData.isDirectory() && fl.userData.isDirectory() && fl.userData.getFileName().equalsIgnoreCase ("FL Studio"))
        fl.sharedData = fl.userData.getParentDirectory();
    if (! fl.sharedData.isDirectory())
        if (const auto shared = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Image-Line"); shared.isDirectory())
            fl.sharedData = shared;

    // plugin folders FL was told about
    for (const auto* key : { "Software\\Image-Line\\Shared\\Paths\\VST plugins", "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir64",
                             "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir" })
        for (const auto& part : juce::StringArray::fromTokens (registryValue (key), ";", "\""))
            if (const auto folder = existingFolder (part.trim()); folder.exists())
                fl.vstFolders.addIfNotAlreadyThere (folder.getFullPathName(), true);

    return fl;
}

// what the answer depends on besides the disk: another value = another question
juce::String flQuestion()
{
    juce::StringArray q;
    for (const auto* name : { "LYNX_NO_FL", "LYNX_FL_INSTALL", "LYNX_FL_USER_DATA", "WINEPREFIX" })
        q.add (juce::SystemStats::getEnvironmentVariable (name, {}));
    q.add (flFolderSetting()); // (LYNX's own settings folder)
    return q.joinIntoString ("\n");
}

bool same (const FlStudioInstall& a, const FlStudioInstall& b)
{
    return a.installDir == b.installDir && a.userData == b.userData && a.sharedData == b.sharedData && a.vstFolders == b.vstFolders && a.source == b.source;
}

struct FlFinder
{
    std::mutex lock;
    std::condition_variable done;
    juce::String question;       // of `answer` / of the worker that is looking
    FlStudioInstall answer;
    bool known = false, looking = false, waitedOut = false;
    int turn = 0;                // the newest worker's (an older one that comes back late is ignored)
    juce::uint32 answeredMs = 0;
};

// Shared by the workers: one that hangs on a dead drive may outlive everything else.
std::shared_ptr<FlFinder> flFinder()
{
    static const auto f = std::make_shared<FlFinder>();
    return f;
}

// (f.lock held)
void startLooking (const std::shared_ptr<FlFinder>& f)
{
    f->looking = true;
    const auto turn = ++f->turn;
    std::thread ([f, turn] {
        const auto found = probeFlStudio();
        bool changed = false;
        {
            const std::lock_guard<std::mutex> g (f->lock);
            if (turn != f->turn)
                return;
            changed = f->waitedOut || (f->known && ! same (f->answer, found));
            f->answer = found;
            f->known = true;
            f->looking = f->waitedOut = false;
            f->answeredMs = juce::Time::getMillisecondCounter();
        }
        f->done.notify_all();
        if (changed)
            juce::MessageManager::callAsync ([] {
                refreshFlStudioAccess();
                if (flStudioChanged)
                    flStudioChanged();
            });
    }).detach();
}
} // namespace

FlStudioInstall findFlStudio (int waitMs)
{
    const auto f = flFinder();
    const auto question = flQuestion();
    std::unique_lock<std::mutex> g (f->lock);
    if (question != f->question)
    {
        f->question = question;
        f->known = f->waitedOut = false;
        f->answer = {};
        startLooking (f);
        f->done.notify_all(); // (whoever waits for the old question)
    }
    if (f->known)
    {
        if (! f->looking && juce::Time::getMillisecondCounter() - f->answeredMs > 5000)
            startLooking (f); // (the next caller gets what it finds)
        return f->answer;
    }
    // nobody knows yet: wait, but only the first time round (a drive that did not answer is not waited for again)
    if (f->waitedOut && waitMs <= flStudioWaitMs)
        return {};
    const auto turn = f->turn;
    if (! f->done.wait_for (g, std::chrono::milliseconds (waitMs), [&f, turn] { return f->known || f->turn != turn; }) || ! f->known)
    {
        if (f->turn == turn)
            f->waitedOut = true;
        return {};
    }
    return f->answer;
}

FlPluginDb readFlPluginDatabase (const FlStudioInstall& fl, int* files)
{
    FlPluginDb db;
    int count = 0;
    const auto folder = fl.pluginDatabase();

    if (folder.isDirectory())
    {
        // .fst first (they carry the IDs), then the .nfo text (vendor, role) fills in the rest
        for (const auto* pattern : { "*.fst", "*.nfo" })
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, pattern, juce::File::findFiles))
            {
                const auto& file = entry.getFile();
                if (file.getFileName().equalsIgnoreCase ("VerifiedIDs.nfo") || entry.getFileSize() > 8 * 1024 * 1024)
                    continue;
                ++count;
                if (file.hasFileExtension ("fst"))
                {
                    juce::MemoryBlock data;
                    FlPluginDbEntry e;
                    e.source = file.getFullPathName().toStdString();
                    const auto path = e.source;
                    const auto under = [&path] (const char* folder) { // either separator (Windows path or a Wine prefix on Linux)
                        return path.find (std::string ("\\") + folder + "\\") != std::string::npos || path.find (std::string ("/") + folder + "/") != std::string::npos;
                    };
                    e.role = under ("Generators") ? 2 : under ("Effects") ? 1 : 0;
                    if (file.loadFileAsData (data)
                        && parseFlPluginFst ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() }, e))
                        db.add (e);
                }
                else
                {
                    for (const auto& e : parseFlPluginNfo (file.loadFileAsString().toStdString(), file.getFullPathName().toStdString()))
                        db.add (e);
                }
            }
    }

    if (files != nullptr)
        *files = count;
    return db;
}
} // namespace lynx
