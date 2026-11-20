// Round 190nr (leftovers of the 0.1.30 release): going back to an older version left the newer version's install-folder
// entries (0.1.29's installer kept 0.1.30's licenses\, round 190nk's updater test). The --apply-update helper of the
// newer version removes what arrived after the version installed (core/UpdateCheck.h entriesNewerThan).
#include "core/UpdateCheck.h"

TEST (leftovers190nr_entries_newer_than_the_version_installed)
{
    using namespace lynx::update;
    auto names = [] (const char* v) {
        std::string out;
        for (const auto& n : entriesNewerThan (parseVersion (v)))
            out += (out.empty() ? "" : ",") + n;
        return out;
    };
    CHECK (names ("0.1.29-alpha") == "licenses,installed-files.txt");
    CHECK (names ("0.1.28.1-alpha") == "licenses,installed-files.txt");
    CHECK (names ("0.1.30-alpha") == "installed-files.txt"); // 0.1.30 ships licenses\ itself
    CHECK (names ("0.1.30") == "installed-files.txt");       // CMake's bare version equals its release tag
    CHECK (names ("0.1.31-alpha").empty() && names ("0.1.31").empty() && names ("0.1.32-alpha").empty());
    CHECK (names ("").empty() && names ("garbage").empty()); // unreadable note: nothing is removed
    // every entry names one top-level item and a valid version (the helper deletes folder.getChildFile (name))
    for (const auto& e : installEntries())
    {
        const std::string n = e.name;
        CHECK (! n.empty() && n != "." && n != ".." && n.find_first_of ("/\\:") == std::string::npos);
        CHECK (parseVersion (e.since).valid);
        CHECK (n != "LYNX.exe" && n != "Uninstall.exe"); // never the program itself
    }
}
