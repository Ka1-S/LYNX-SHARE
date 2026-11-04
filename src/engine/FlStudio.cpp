#include "engine/FlStudio.h"

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

FlStudioInstall findFlStudio()
{
    FlStudioInstall fl;
    // test hook (round 169g): LYNX_NO_FL=1 = a computer without FL Studio, even where it is installed
    if (juce::SystemStats::getEnvironmentVariable ("LYNX_NO_FL", {}).isNotEmpty())
        return fl;

    // installation: where FL says it is, else the newest "FL Studio*" under Program Files\Image-Line
    // LYNX_FL_INSTALL=<folder> points at an installation anywhere (another Wine prefix, a copy, tests)
    fl.installDir = existingFolder (juce::SystemStats::getEnvironmentVariable ("LYNX_FL_INSTALL", {}));
    if (! fl.installDir.getChildFile ("Data").isDirectory())
        fl.installDir = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Install path"));
    if (! fl.installDir.getChildFile ("Data").isDirectory())
    {
        fl.installDir = juce::File();
        for (const auto& dir : flStudioCandidates())
            if (dir.getChildFile ("Data").isDirectory() && dir.getFileName() > fl.installDir.getFileName())
                fl.installDir = dir;
    }

    // test hook: LYNX_FL_USER_DATA=<folder> stands in for FL's user data folder (synthetic plugin databases)
    if (const auto testFolder = existingFolder (juce::SystemStats::getEnvironmentVariable ("LYNX_FL_USER_DATA", {})); testFolder.exists())
        fl.userData = testFolder;

    // user data: "Shared data" (e.g. ...\Documents\Image-Line\, possibly under OneDrive) + "FL Studio"
    if (! fl.userData.isDirectory())
        if (const auto shared = existingFolder (registryValue ("Software\\Image-Line\\Shared\\Paths\\Shared data")); shared.exists())
            if (const auto user = shared.getChildFile ("FL Studio"); user.isDirectory())
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

    // plugin folders FL was told about
    for (const auto* key : { "Software\\Image-Line\\Shared\\Paths\\VST plugins", "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir64",
                             "Software\\Image-Line\\Shared\\Plugins\\FL Studio\\VstDir" })
        for (const auto& part : juce::StringArray::fromTokens (registryValue (key), ";", "\""))
            if (const auto folder = existingFolder (part.trim()); folder.exists())
                fl.vstFolders.addIfNotAlreadyThere (folder.getFullPathName(), true);

    return fl;
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
