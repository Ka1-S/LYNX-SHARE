// Round 190rc, item 9 (hotfix candidate): the .flp export writes FL's project data folder only when it is on this computer.

#include "core/FlpExport.h"

// Item 9: FL 2026 stops a 26.x project whose project data folder (0xCA) is missing at "Invalid project data folder".
// An .flp imported from another computer keeps its maker's folder; the export leaves it out when it is not on this
// computer (FL's own projects without one save it empty), and keeps one that exists.
TEST (hotfix190rc_flp_export_data_folder_only_when_it_exists)
{
    using namespace lynx;
    Song song;
    song.dataFolder = "C:\\Users\\someone\\Documents\\Image-Line\\FL Studio\\Projects\\song\\";
    flpexport::Report report;
    flpexport::Options options;
    // not set (the core default, tests): written as it is
    CHECK (flpexport::toProject (song, options, report).dataFolder == song.dataFolder);
    std::vector<std::string> asked;
    options.folderExists = [&asked] (const std::string& p) { asked.push_back (p); return false; };
    CHECK (flpexport::toProject (song, options, report).dataFolder.empty());
    CHECK (asked.size() == 1u && asked.front() == song.dataFolder);
    // the file FL reads back has no data folder
    CHECK (flp::parseProject (flpexport::exportFlp (song, options, report)).dataFolder.empty());
    options.folderExists = [] (const std::string&) { return true; };
    CHECK (flpexport::toProject (song, options, report).dataFolder == song.dataFolder);
    CHECK (flp::parseProject (flpexport::exportFlp (song, options, report)).dataFolder == song.dataFolder);
    // an empty one is not looked up
    song.dataFolder.clear();
    asked.clear();
    options.folderExists = [&asked] (const std::string& p) { asked.push_back (p); return false; };
    CHECK (flpexport::toProject (song, options, report).dataFolder.empty());
    CHECK (asked.empty());
}
