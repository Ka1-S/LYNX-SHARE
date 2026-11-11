// --- The Versions panel (core/ProjectSafety.h, namespace versions) - round 190ap ---

TEST (versions190ap_family_follows_save_new_version)
{
    using namespace lynx::safety;
    int n = 0;
    CHECK (versions::familyOf ("Song", &n) == "Song" && n == 1);
    CHECK (versions::familyOf ("Song_2", &n) == "Song" && n == 2);
    CHECK (versions::familyOf ("Song_17", &n) == "Song" && n == 17);
    CHECK (versions::familyOf ("Mix_v2", &n) == "Mix_v2" && n == 1); // "_v2" is not a version number
    CHECK (versions::familyOf ("_5", &n) == "_5" && n == 1);         // nothing before the underscore
    CHECK (versions::familyOf ("Song_", &n) == "Song_" && n == 1);
    // what "Save new version" writes belongs to the family it was saved from
    const auto next = nextVersionName ("Song_2", {});
    CHECK (versions::familyOf (next, &n) == "Song" && n == 3);
    CHECK (versions::familyOf (nextVersionName ("Mix_v2", {}), &n) == "Mix_v2" && n == 2);
}

TEST (versions190ap_list_has_versions_and_backups_newest_first)
{
    using namespace lynx::safety;
    const std::vector<versions::FileInfo> project { { "Song", 100, 10 }, { "Song_2", 300, 20 }, { "Song_3", 500, 30 }, { "Songs", 900, 1 },
                                                    { "Song_v2", 901, 1 }, { "Other_2", 902, 1 } };
    const std::vector<versions::FileInfo> backups { { "Song_3 (autosaved on 2026-10-20 at 14h19)", 600, 31 },
                                                    { "Song_2 (overwritten on 2026-10-20 at 13h00)", 400, 21 },
                                                    { "Song (2026-10-16 19.50.03)", 50, 9 },
                                                    { "Other (autosaved on 2026-10-20 at 14h19)", 700, 5 },
                                                    { "Song_3 (autosaved on 2026-10-20 at 14h19)_2", 610, 32 },
                                                    { "Song_4 (autosaved on 2026-10-20 at 15h00)", 800, 5 } }; // no Song_4 next to the project
    versions::Labels vl { { "Song_2", "before the new drop" } }, bl { { "Song_3 (autosaved on 2026-10-20 at 14h19)", "good bass" } };
    const auto all = versions::list (project, backups, "Song_3", true, vl, bl);
    CHECK (all.size() == 7);
    if (all.size() == 7)
    {
        CHECK (all[0].name == "Song_3 (autosaved on 2026-10-20 at 14h19)_2" && all[0].kind == versions::Kind::autosaved);
        CHECK (all[1].name == "Song_3 (autosaved on 2026-10-20 at 14h19)" && all[1].label == "good bass" && all[1].size == 31);
        CHECK (all[2].name == "Song_3" && all[2].kind == versions::Kind::version && all[2].current && all[2].number == 3);
        CHECK (all[3].kind == versions::Kind::overwritten && ! all[3].current);
        CHECK (all[4].name == "Song_2" && all[4].label == "before the new drop" && all[4].number == 2 && ! all[4].current);
        CHECK (all[5].name == "Song" && all[5].number == 1);
        CHECK (all[6].name == "Song (2026-10-16 19.50.03)" && all[6].kind == versions::Kind::autosaved);
    }
    // a project never saved: no versions (the folder is not its own), only the backups filed under its name
    const auto fresh = versions::list (project, { { "untitled (autosaved on 2026-10-20 at 14h19)", 5, 1 }, { "Song (autosaved on 2026-10-20 at 14h19)", 6, 1 } },
                                       "untitled", false);
    CHECK (fresh.size() == 1 && fresh[0].name == "untitled (autosaved on 2026-10-20 at 14h19)");
    CHECK (versions::list ({}, {}, "Song", true).empty());
}

TEST (versions190ap_labels_live_beside_the_files)
{
    using namespace lynx::safety;
    CHECK (versions::labelsFileName ("Song") == "Song.version-labels.txt");
    CHECK (versions::cleanLabel ("  two\tlines\nhere \r\n") == "two lines here");
    CHECK (versions::cleanLabel (" \t ").empty());
    CHECK (versions::cleanLabel (std::string (200, 'x')).size() == 80);
    // (cut between characters, never inside one: "é" is two bytes)
    std::string accents;
    for (int i = 0; i < 60; ++i)
        accents += "\xC3\xA9";
    const auto cut = versions::cleanLabel (accents);
    CHECK (cut.size() == 80 && (cut.size() % 2) == 0);
    std::string odd = "a" + accents;
    CHECK (versions::cleanLabel (odd).size() == 79);

    versions::Labels labels { { "Song_2", "before the drop" }, { "Song (autosaved on 2026-10-20 at 14h19)", " mix  A " }, { "Song_3", "   " }, { "bad\tname", "x" } };
    const auto text = versions::labelsToText (labels);
    CHECK (text == "Song (autosaved on 2026-10-20 at 14h19)\tmix  A\nSong_2\tbefore the drop\n");
    const auto back = versions::labelsFromText (text);
    CHECK (back.size() == 2 && back.at ("Song_2") == "before the drop" && back.at ("Song (autosaved on 2026-10-20 at 14h19)") == "mix  A");
    CHECK (versions::labelsFromText (versions::labelsToText (back)) == back);
    // Windows line ends, junk lines and an empty label are tolerated
    const auto loose = versions::labelsFromText ("Song_2\tone\r\nno tab here\r\n\tno name\r\nSong_3\t\r\nSong_4\tfour");
    CHECK (loose.size() == 2 && loose.at ("Song_2") == "one" && loose.at ("Song_4") == "four");
    CHECK (versions::labelsFromText ("").empty());

    const auto kept = versions::pruned (back, [] (const std::string& name) { return name == "Song_2"; });
    CHECK (kept.size() == 1 && kept.count ("Song_2") == 1);
}

TEST (versions190ap_a_labelled_backup_is_not_rotated_away)
{
    using namespace lynx::safety;
    const std::vector<BackupFile> files { { "Song (autosaved on 2026-10-20 at 10h00)", 100 },
                                          { "Song (autosaved on 2026-10-20 at 09h00)", 90 },
                                          { "Song (autosaved on 2026-10-20 at 08h00)", 80 },
                                          { "Song (autosaved on 2026-10-20 at 07h00)", 70 } };
    // without labels: exactly the rotation as it was
    CHECK (backupsToRemove (versions::withoutLabelled (files, {}), "Song", 2) == backupsToRemove (files, "Song", 2));
    const versions::Labels labels { { "Song (autosaved on 2026-10-20 at 07h00)", "keep this" }, { "Song (autosaved on 2026-10-20 at 08h00)", "  " } };
    const auto gone = backupsToRemove (versions::withoutLabelled (files, labels), "Song", 2);
    CHECK (gone.size() == 1 && gone[0] == "Song (autosaved on 2026-10-20 at 08h00)"); // the blank label does not count
}

TEST (versions190ap_copy_names_and_ages)
{
    using namespace lynx::safety;
    versions::Entry e;
    e.name = "Song_2";
    CHECK (versions::copyName (e, "Song_3") == "Song_2 (copy)");
    e.kind = versions::Kind::autosaved;
    e.name = "Song_3 (autosaved on 2026-10-20 at 14h19)";
    CHECK (versions::copyName (e, "Song_3") == "Song_3 (copy)");
    e.name = "Song_3 (autosaved on 2026-10-20 at 14h19)_2";
    CHECK (versions::copyName (e, "Song_3") == "Song_3 (copy)");
    e.name = "My (demo) (overwritten on 2026-10-20 at 14h19)";
    e.kind = versions::Kind::overwritten;
    CHECK (versions::copyName (e, "x") == "My (demo) (copy)");
    e.name = "Song (2026-10-16 19.50.03)";
    CHECK (versions::copyName (e, "x") == "Song (copy)");
    e.name = "My (demo)"; // the plain older backup name keeps its own brackets
    CHECK (versions::copyName (e, "x") == "My (demo) (copy)");

    int unit = -1;
    CHECK (versions::agoValue (0, unit) == 0 && unit == 0);
    CHECK (versions::agoValue (-50, unit) == 0 && unit == 0);
    CHECK (versions::agoValue (59, unit) == 0 && unit == 0);
    CHECK (versions::agoValue (125, unit) == 2 && unit == 0);
    CHECK (versions::agoValue (3599, unit) == 59 && unit == 0);
    CHECK (versions::agoValue (3600, unit) == 1 && unit == 1);
    CHECK (versions::agoValue (47 * 3600, unit) == 47 && unit == 1);
    CHECK (versions::agoValue (48 * 3600, unit) == 2 && unit == 2);
}
