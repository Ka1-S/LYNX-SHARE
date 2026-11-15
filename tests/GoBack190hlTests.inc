// Round 190hl (hotfix of 0.1.28): Help › "Go back to <version>..." (round 190gh) in the released 0.1.28
// (/mnt/e/lynx-work/upd0128: shots s01–s15, update.log):
// (a) the running "0.1.28" (CMake, no suffix) compared newer than its own tag "v0.1.28-alpha", so going back picked
//     0.1.28 itself: a version without a prerelease now equals every prerelease of the same numbers;
// (b) 0.1.27 read the new 3-line installing.txt as version "0.0.0" ("updated to 0.0.0"): "version=" is written last;
// (c) the skip of the version left was saved only after the restart, by the version installed, and 0.1.27 does not
//     know that step: the skip is written into updates.txt ("skipped", which every version reads) before the installer.
#include "core/UpdateCheck.h"

namespace hl190
{
// the release list of 0.1.29 time: the hotfix as a four-number tag too, and a draft
const char* releasesJson = R"json([
  {"tag_name":"v0.1.30-alpha","draft":true,"prerelease":true,"assets":[]},
  {"tag_name":"v0.1.29-alpha","draft":false,"prerelease":true,"assets":[]},
  {"tag_name":"v0.1.28.1-alpha","draft":false,"prerelease":true,"assets":[]},
  {"tag_name":"v0.1.28-alpha","draft":false,"prerelease":true,"assets":[]},
  {"tag_name":"v0.1.27-alpha","draft":false,"prerelease":true,"assets":[]}
])json";

// LYNX SHARE 0.1.27's reader of installing.txt (src/ui/Updater.cpp at v0.1.27-alpha, afterStart): everything after
// the first "version=", trimmed
std::string versionAsRead0127 (const std::string& installing)
{
    const auto at = installing.find ("version=");
    if (at == std::string::npos)
        return {};
    auto rest = installing.substr (at + 8);
    const auto a = rest.find_first_not_of (" \t\r\n"), b = rest.find_last_not_of (" \t\r\n");
    return a == std::string::npos ? std::string() : rest.substr (a, b - a + 1);
}

// LYNX SHARE 0.1.28's reader (round 190gh): line by line, "version=" / "from=" / "rollback=" at the line start
lynx::update::InstallingNote asRead0128 (const std::string& installing)
{
    lynx::update::InstallingNote note;
    std::size_t start = 0;
    while (start <= installing.size())
    {
        auto end = installing.find ('\n', start);
        if (end == std::string::npos)
            end = installing.size();
        const auto line = installing.substr (start, end - start);
        auto value = [&line] { return line.substr (line.find ('=') + 1); };
        if (line.rfind ("version=", 0) == 0 && note.version.empty()) note.version = value();
        if (line.rfind ("from=", 0) == 0) note.from = value();
        if (line.rfind ("rollback=", 0) == 0) note.rollback = value() == "1";
        start = end + 1;
    }
    return note;
}
} // namespace hl190

TEST (goback190hl_a_the_running_build_equals_its_release_tag)
{
    using namespace lynx::update;
    auto cmp = [] (const char* a, const char* b) { return compare (parseVersion (a), parseVersion (b)); };
    // the build of 0.1.28 (CMake "0.1.28") is the release v0.1.28-alpha
    CHECK (cmp ("0.1.28", "0.1.28-alpha") == 0 && cmp ("0.1.28-alpha", "0.1.28") == 0);
    CHECK (cmp ("0.1.27-alpha", "0.1.28") < 0 && cmp ("0.1.28", "0.1.27-alpha") > 0);
    CHECK (cmp ("0.1.29-alpha", "0.1.28") > 0 && cmp ("0.1.28", "0.1.29-alpha") < 0);
    CHECK (cmp ("0.1.27-alpha", "0.1.29-alpha") < 0);
    // two prereleases still order by semver
    CHECK (cmp ("0.1.28-alpha", "0.1.28-beta") < 0 && cmp ("0.1.28-alpha.2", "0.1.28-alpha.10") < 0);
    // four numbers (a hotfix "0.1.28.1"): above 0.1.28 in every form, below 0.1.29
    const auto hot = parseVersion ("v0.1.28.1-alpha");
    CHECK (hot.valid && hot.revision == 1 && hot.text() == "0.1.28.1-alpha" && parseVersion ("0.1.28.1").text() == "0.1.28.1");
    CHECK (parseVersion ("0.1.28.0").text() == "0.1.28" && cmp ("0.1.28.0", "0.1.28") == 0);
    CHECK (cmp ("0.1.28.1", "0.1.28-alpha") > 0 && cmp ("0.1.28.1", "0.1.28") > 0 && cmp ("0.1.28.1", "0.1.28.1-alpha") == 0);
    CHECK (cmp ("0.1.28.1-alpha", "0.1.29-alpha") < 0 && cmp ("0.1.28.2", "0.1.28.10") < 0);

    const auto releases = parseReleases (hl190::releasesJson);
    CHECK (releases.size() == 5);
    if (releases.size() != 5)
        return;
    auto at = [&] (int i) { return i >= 0 ? releases[(std::size_t) i].version.text() : std::string ("none"); };
    auto atIn = [] (const std::vector<Release>& list, int i) { return i >= 0 ? list[(std::size_t) i].version.text() : std::string ("none"); };
    // update checks: the running build is up to date with its own tag, the next one is offered
    CHECK (at (newestIndex (releases, parseVersion ("0.1.29"))) == "none");
    CHECK (at (newestIndex (releases, parseVersion ("0.1.28.1"))) == "0.1.29-alpha");
    CHECK (at (newestIndex (releases, parseVersion ("0.1.28"))) == "0.1.29-alpha");
    CHECK (at (newestIndex (releases, parseVersion ("0.1.27"))) == "0.1.29-alpha");
    const std::vector<Release> upTo0128 (releases.begin() + 2, releases.end()); // 0.1.28.1, 0.1.28, 0.1.27
    CHECK (atIn (upTo0128, newestIndex (upTo0128, parseVersion ("0.1.28"))) == "0.1.28.1-alpha");
    CHECK (newestIndex (upTo0128, parseVersion ("0.1.28.1")) < 0);
    const std::vector<Release> upTo0128only (releases.begin() + 3, releases.end()); // 0.1.28, 0.1.27
    CHECK (newestIndex (upTo0128only, parseVersion ("0.1.28")) < 0); // "You're up to date" (as 0.1.28 said)

    // going back: never the running release itself (0.1.28 picked 0.1.28-alpha), never a newer one or the draft
    CHECK (atIn (upTo0128only, rollbackIndex (upTo0128only, parseVersion ("0.1.28"), "")) == "0.1.27-alpha");
    CHECK (atIn (upTo0128only, rollbackIndex (upTo0128only, parseVersion ("0.1.28"), "0.1.27")) == "0.1.27-alpha");
    // (s08–s10: after 0.1.28 reinstalled itself the recorded previous was "0.1.28-alpha", its own tag)
    CHECK (atIn (upTo0128only, rollbackIndex (upTo0128only, parseVersion ("0.1.28"), "0.1.28-alpha")) == "0.1.27-alpha");
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.29"), "")) == "0.1.28.1-alpha");
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.29"), "0.1.28")) == "0.1.28-alpha"); // recorded bare by 0.1.28
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.28.1"), "0.1.28")) == "0.1.28-alpha");
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.28.1"), "")) == "0.1.28-alpha");
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.28.1"), "0.1.27")) == "0.1.27-alpha");
    CHECK (at (rollbackIndex (releases, parseVersion ("0.1.28.1"), "0.1.29-alpha")) == "0.1.28-alpha"); // (newer: ignored)
    CHECK (rollbackIndex (releases, parseVersion ("0.1.27"), "") < 0);

    // the skip: "0.1.28" (left by going back) hides its tag on the automatic check, not the next release
    Settings s;
    s = afterRollback (s, parseVersion ("0.1.28"));
    Release r;
    r.version = parseVersion ("v0.1.28-alpha");
    CHECK (! shouldShow (r, s, false) && shouldShow (r, s, true));
    r.version = parseVersion ("v0.1.28.1-alpha");
    CHECK (shouldShow (r, s, false));
    // a skipped tag and the bare version of the same release are the same: no change
    Settings tagged;
    tagged.skipped = "0.1.28-alpha";
    CHECK (skipLeft (tagged, parseVersion ("0.1.28")).skipped == "0.1.28-alpha");
}

TEST (goback190hl_b_installing_txt_reads_right_in_0127_0128_and_now)
{
    using namespace lynx::update;
    InstallingNote note;
    note.version = "0.1.27-alpha";
    note.from = "0.1.29";
    note.rollback = true;
    const auto text = formatInstalling (note);
    CHECK (text.size() >= 21 && text.compare (text.size() - 21, 21, "version=0.1.27-alpha\n") == 0); // (the last line)
    // 0.1.27: the version, not "0.0.0"
    CHECK (hl190::versionAsRead0127 (text) == "0.1.27-alpha" && parseVersion (hl190::versionAsRead0127 (text)).valid);
    // round 190gh's layout (version first) is what 0.1.27 misread
    const std::string old190gh = "version=0.1.27-alpha\nfrom=0.1.28\nrollback=1\n";
    CHECK (! parseVersion (hl190::versionAsRead0127 (old190gh)).valid);
    // 0.1.28 and this version read all three, in either layout
    const auto by0128 = hl190::asRead0128 (text);
    CHECK (by0128.version == "0.1.27-alpha" && by0128.from == "0.1.29" && by0128.rollback);
    const auto now = parseInstalling (text);
    CHECK (now.version == "0.1.27-alpha" && now.from == "0.1.29" && now.rollback);
    const auto legacy = parseInstalling (old190gh);
    CHECK (legacy.version == "0.1.27-alpha" && legacy.from == "0.1.28" && legacy.rollback);
    const auto plain = parseInstalling ("version=0.1.28-alpha\n"); // 0.1.27's own note
    CHECK (plain.version == "0.1.28-alpha" && plain.from.empty() && ! plain.rollback);
    CHECK (parseInstalling ("version= 0.1.28-alpha \r\nfrom=0.1.27\r\nrollback=0\r\n").from == "0.1.27");
    // an update (not going back) from an unknown version: still readable by 0.1.27
    InstallingNote up;
    up.version = "0.1.29-alpha";
    const auto upText = formatInstalling (up);
    CHECK (hl190::versionAsRead0127 (upText) == "0.1.29-alpha" && ! parseInstalling (upText).rollback && parseInstalling (upText).from.empty());
}

TEST (goback190hl_c_the_skip_is_in_the_settings_older_versions_read)
{
    using namespace lynx::update;
    Settings s;
    s.previous = "0.1.28";
    s.lastCheckMs = 1763200000000LL;
    // before the installer of the older version runs: only the skip, the previous stays until the restart
    const auto before = skipLeft (s, parseVersion ("0.1.29"));
    CHECK (before.skipped == "0.1.29" && before.previous == "0.1.28" && before.lastCheckMs == s.lastCheckMs);
    const auto text = formatSettings (before);
    CHECK (text.find ("\nskipped=0.1.29\n") != std::string::npos); // the key and value 0.1.21+ read (three numbers)
    // what 0.1.27 does with it: parse, then hide v0.1.29-alpha on the automatic check, offer it on a manual one
    const auto read = parseSettings (text);
    Release next;
    next.version = parseVersion ("v0.1.29-alpha");
    CHECK (read.skipped == "0.1.29" && ! shouldShow (next, read, false) && shouldShow (next, read, true));
    next.version = parseVersion ("v0.1.30-alpha");
    CHECK (shouldShow (next, read, false)); // the release after it is offered
    // after the restart a version that knows going back uses the previous up; the skip stays the same
    const auto after = afterRollback (before, parseVersion ("0.1.29"));
    CHECK (after.skipped == "0.1.29" && after.previous.empty());
    // an already skipped newer version stays; a version that is no version changes nothing
    Settings later;
    later.skipped = "0.1.31-alpha";
    CHECK (skipLeft (later, parseVersion ("0.1.29")).skipped == "0.1.31-alpha");
    CHECK (skipLeft (s, Version {}).skipped.empty());
    // a hotfix with four numbers is skipped as written (0.1.28 and older can't read it, nor see its release: consistent)
    CHECK (skipLeft (Settings {}, parseVersion ("0.1.28.1")).skipped == "0.1.28.1" && parseSettings ("skipped=0.1.28.1\n").skipped == "0.1.28.1");
}
