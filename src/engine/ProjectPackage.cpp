#include "platform/Paths.h"
#include "engine/ProjectPackage.h"

#include "core/MissingFiles.h"
#include "core/ProjectSafety.h"
#include "engine/ProjectFile.h"

namespace lynx::ProjectPackage
{
namespace
{
juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::File fileFor (const std::string& path)
{
    const auto p = str (path);
    return juce::File::isAbsolutePath (p) ? juce::File (p) : juce::File();
}
} // namespace

bool exportZip (const Song& song, const juce::String& projectName, const juce::File& zip, Result& result, juce::String& error)
{
    result = {};
    // FL's zipped loop package (round 190oq, REA FUN_011dc560): the project and its files side by side, flat, by name
    // (FL warns when two have one name and may lose one; LYNX gives the second a unique name instead)
    const auto plan = safety::packagePlan (song, "");

    Song packed = song;
    std::map<std::string, std::string> relink;
    juce::ZipFile::Builder builder;

    for (const auto& [original, inside] : plan)
    {
        const auto source = fileFor (original);
        if (! source.existsAsFile())
        {
            result.missing.add (str (original));
            continue; // the project keeps the old path (the Missing files dialog can find it later)
        }
        builder.addFile (source, 0, str (inside)); // samples are already compressed audio or barely compress
        relink[original] = inside;
        ++result.files;
    }
    applyRelocations (packed, relink);

    const auto name = juce::File::createLegalFileName (projectName.isNotEmpty() ? projectName : juce::String ("untitled"));
    const auto json = ProjectFile::toJson (packed);
    builder.addEntry (new juce::MemoryInputStream (json.toRawUTF8(), json.getNumBytesAsUTF8(), true), 9, name + ProjectFile::extension,
                      juce::Time::getCurrentTime());

    juce::TemporaryFile temp (zip);
    {
        juce::FileOutputStream out (temp.getFile());
        if (! out.openedOk() || ! builder.writeToStream (out, nullptr))
        {
            error = "could not write " + zip.getFullPathName();
            return false;
        }
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "could not write " + zip.getFullPathName();
        return false;
    }
    return true;
}

bool collectSamples (const Song& song, const juce::File& folder, std::map<std::string, std::string>& relink, Result& result,
                     juce::String& error)
{
    result = {};
    relink.clear();
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return false;
    }

    for (const auto& original : safety::referencedSamples (song))
    {
        const auto source = fileFor (original);
        if (! source.existsAsFile())
        {
            result.missing.add (str (original));
            continue;
        }
        if (source.getParentDirectory() == folder) // already there
        {
            relink[original] = source.getFullPathName().toStdString();
            continue;
        }
        const auto stem = safety::uniqueName (source.getFileNameWithoutExtension().toStdString(), [&] (const std::string& s) {
            return folder.getChildFile (str (s) + source.getFileExtension()).exists();
        });
        const auto target = folder.getChildFile (str (stem) + source.getFileExtension());
        if (! source.copyFileTo (target))
        {
            error = "could not copy " + source.getFullPathName();
            return false;
        }
        relink[original] = target.getFullPathName().toStdString();
        ++result.files;
    }
    return true;
}

bool copyDataFiles (const Song& song, const juce::File& folder, Result& result, juce::String& error)
{
    // FL's Export › Project data files (round 190oq, REA FUN_011ec0f0): every file the project uses copied flat into one
    // folder (unique names, nothing overwritten); the project keeps its own paths and no project file is written
    result = {};
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return false;
    }
    for (const auto& original : safety::referencedSamples (song))
    {
        const auto source = fileFor (original);
        if (! source.existsAsFile())
        {
            result.missing.add (str (original));
            continue;
        }
        const auto stem = safety::uniqueName (source.getFileNameWithoutExtension().toStdString(), [&] (const std::string& n) {
            return folder.getChildFile (str (n) + source.getFileExtension()).exists();
        });
        if (! source.copyFileTo (folder.getChildFile (str (stem) + source.getFileExtension())))
        {
            error = "could not copy " + source.getFullPathName();
            return false;
        }
        ++result.files;
    }
    return true;
}

bool collectIntoProject (const Song& song, const juce::File& projectFolder, std::map<std::string, std::string>& relink, Result& result,
                         juce::String& error)
{
    // Live's File › Collect All and Save (round 190oq, issue #251): the files outside the project's folder are copied into
    // <project>/Samples/Imported and the project uses the copies; files already inside the folder stay where they are
    result = {};
    relink.clear();
    const auto target = projectFolder.getChildFile ("Samples").getChildFile ("Imported");
    for (const auto& original : safety::referencedSamples (song))
    {
        const auto source = fileFor (original);
        if (! source.existsAsFile())
        {
            result.missing.add (str (original));
            continue;
        }
        if (source.isAChildOf (projectFolder))
            continue;
        if (! target.createDirectory())
        {
            error = "could not create " + target.getFullPathName();
            return false;
        }
        const auto stem = safety::uniqueName (source.getFileNameWithoutExtension().toStdString(), [&] (const std::string& n) {
            return target.getChildFile (str (n) + source.getFileExtension()).exists();
        });
        const auto copy = target.getChildFile (str (stem) + source.getFileExtension());
        if (! source.copyFileTo (copy))
        {
            error = "could not copy " + source.getFullPathName();
            return false;
        }
        relink[original] = copy.getFullPathName().toStdString();
        ++result.files;
    }
    return true;
}

juce::File extractZip (const juce::File& zipFile, const juce::File& destinationRoot, juce::String& error)
{
    juce::ZipFile zip (zipFile);
    if (zip.getNumEntries() == 0)
    {
        error = "not a project package (empty or not a zip file)";
        return {};
    }

    bool hasProject = false;
    for (int i = 0; i < zip.getNumEntries(); ++i)
        if (const auto* e = zip.getEntry (i); e != nullptr && (e->filename.endsWithIgnoreCase (ProjectFile::extension) || e->filename.endsWithIgnoreCase (".flp")
                                                         || e->filename.endsWithIgnoreCase (".flm")
                                                         || e->filename.containsIgnoreCase (".band/projectData")
                                                         || e->filename.containsIgnoreCase (".band/Alternatives/000/ProjectData")
                                                         || (zipFile.hasFileExtension ("band") && (e->filename == "projectData" || e->filename == "Alternatives/000/ProjectData"))
                                                         // (round 190jr: a BandLab .blx, or a zipped .blx holding its JSON)
                                                         || e->filename.endsWithIgnoreCase (".blx")
                                                         || (zipFile.hasFileExtension ("blx") && e->filename.endsWithIgnoreCase (".json"))))
            hasProject = true;
    if (! hasProject)
    {
        error = "the package has no project file (.lynx, .flp or .flm)";
        return {};
    }

    destinationRoot.createDirectory();
    // (round 190ic: a GarageBand song that arrived as one zipped file, "Song.band", unpacks into a "Song.band" folder)
    const auto zippedBand = zipFile.hasFileExtension ("band");
    const auto folder = destinationRoot.getNonexistentChildFile (juce::File::createLegalFileName (zipFile.getFileNameWithoutExtension()),
                                                                 zippedBand ? ".band" : "", false);
    if (! folder.createDirectory())
    {
        error = "could not create " + folder.getFullPathName();
        return {};
    }
    if (const auto r = zip.uncompressTo (folder, false); r.failed()) // (JUCE refuses entries outside the folder)
    {
        error = r.getErrorMessage();
        return {};
    }

    // (an FL Studio Mobile share-zip, round 182c: the .flm with its library tree, "My Samples/…", beside it)
    for (const auto* pattern : { "*.lynx", "*.flp", "*.flm", "*.blx" })
    {
        auto found = folder.findChildFiles (juce::File::findFiles, true, pattern);
        found.sort();
        if (! found.isEmpty())
            return found.getFirst();
    }
    // (round 190jr: a .blx that was a zip of its revision JSON: the JSON becomes "<name>.blx" beside its samples)
    if (zipFile.hasFileExtension ("blx"))
    {
        auto found = folder.findChildFiles (juce::File::findFiles, true, "*.json");
        found.sort();
        if (! found.isEmpty())
        {
            const auto blx = found.getFirst().getSiblingFile (juce::File::createLegalFileName (zipFile.getFileNameWithoutExtension()) + ".blx");
            if (found.getFirst().copyFileTo (blx))
                return blx;
        }
    }
    // (round 190ic: a GarageBand song, shared from GarageBand for iOS as a zip of its .band folder, or zipped itself)
    if (zippedBand && garageBandPackage (folder) == folder)
        return folder;
    auto bands = folder.findChildFiles (juce::File::findDirectories, true, "*.band");
    bands.sort();
    for (const auto& band : bands)
        if (garageBandPackage (band) == band)
            return band;
    error = "the package has no project file (.lynx, .flp or .flm)";
    return {};
}

juce::File garageBandPackage (const juce::File& file)
{
    for (auto f = file; f != juce::File() && f.getParentDirectory() != f; f = f.getParentDirectory())
        if (f.isDirectory() && f.hasFileExtension ("band")
            && (f.getChildFile ("projectData").existsAsFile() || f.getChildFile ("Alternatives/000/ProjectData").existsAsFile()))
            return f;
    return {};
}

juce::File defaultExtractRoot()
{
    return lynx::platform::documentsFolder().getChildFile ("LYNX/Projects/Unpacked");
}
} // namespace lynx::ProjectPackage
