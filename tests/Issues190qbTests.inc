// Round 190qb: issue follow-ups — #215 (folders in the export file name template).

#include "core/ExportNaming.h"

TEST (issues190qb_215_folders_in_the_export_name)
{
    using namespace lynx::exportnaming;
    Context c;
    c.project = "LYNX SHARE";
    c.year = 2026;
    c.month = 11;
    c.day = 20;
    c.bpm = 130.0;
    c.sampleRate = 48000;
    c.bitDepth = "24bit";
    c.format = "WAV";
    c.track = "Master";
    // the issue's two templates: without and with a folder (a leading "/" = inside the export folder)
    CHECK (resolve ("$project_D$day-M$month-Y$year_$bpm-BPM_$samplerateHz-$bitdepth", c) == "LYNX SHARE_D20-M11-Y2026_130-BPM_48000Hz-24bit");
    CHECK (resolve ("/BOUNCE/$project_D$day-M$month-Y$year_$bpm-BPM_$samplerateHz-$bitdepth", c) == "BOUNCE/LYNX SHARE_D20-M11-Y2026_130-BPM_48000Hz-24bit");
    CHECK (resolve ("BOUNCE/$project", c) == "BOUNCE/LYNX SHARE");
    CHECK (resolve ("BOUNCE\\$project", c) == "BOUNCE/LYNX SHARE"); // (a backslash too, always "/" out)
    // wildcards in folders, nested
    CHECK (resolve ("Bounces/$year/$month/$project", c) == "Bounces/2026/11/LYNX SHARE");
    // a "/" inside a value stays a character ("_"): the template is split before the wildcards are filled
    c.project = "AC/DC";
    CHECK (resolve ("$project", c) == "AC_DC");
    CHECK (resolve ("$project/$project", c) == "AC_DC/AC_DC");
    c.project = "Song";
    // never out of the export folder, no empty folders
    CHECK (resolve ("../../$project", c) == "Song");
    CHECK (resolve ("./x//$project", c) == "x/Song");
    CHECK (resolve ("a/ ../b/$project", c) == "a/b/Song");
    // unsafe characters in a folder become "_" as in a file name; a Windows device name gets "_"
    CHECK (resolve ("Mix:es?/$project", c) == "Mix_es_/Song");
    CHECK (resolve ("CON/$project", c) == "_CON/Song");
    // a folder alone: the project's name in it
    CHECK (resolve ("BOUNCE/", c) == "BOUNCE/Song");
    CHECK (resolve ("BOUNCE/   ", c) == "BOUNCE/Song");
    CHECK (hasFolders ("a/b") && hasFolders ("a\\b") && ! hasFolders ("$project_$date"));
    // stems: $track stays in the file's name; a folder takes the one-file value
    CHECK (resolve ("Stems/$project_$track", c, true) == "Stems/Song_$track");
    CHECK (resolve ("$track/$project", c, true) == "Master/Song");
    // the take counts with the folder: the first name (folder included) not there yet
    c.exists = [] (const std::string& n) { return n == "BOUNCE/Song_Take1"; };
    CHECK (resolve ("BOUNCE/$project_Take$take", c) == "BOUNCE/Song_Take2");
    c.exists = [] (const std::string& n) { return n == "Take1/Song" || n == "Take2/Song"; };
    CHECK (resolve ("Take$take/$project", c) == "Take3/Song");
    // without a "/" nothing changes (the 0.1.30 names)
    CHECK (resolve ("$project_$date", c) == "Song_2026-11-20");
    CHECK (resolve ("", c) == "Song");
}
