// Round 190ba (issue #104): the Plugin database as a folder the user can arrange in the file manager
// (core/PluginDbFolder.h).

#include "core/PluginDbFolder.h"

namespace plugindb190ba
{
namespace fs = std::filesystem;
inline fs::path u8 (const std::string& s) { return fs::path ((const char8_t*) s.c_str()); }
inline std::string str (const fs::path& p)
{
    const auto s = p.generic_u8string();
    return std::string ((const char*) s.data(), s.size());
}
struct TempRoot
{
    fs::path dir;
    std::string root;
    TempRoot()
    {
        static int n = 0;
        dir = fs::temp_directory_path() / ("lynx-plugindb190ba-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string (++n));
        fs::create_directories (dir);
        root = str (dir / "Plugin database");
    }
    ~TempRoot()
    {
        std::error_code ec;
        fs::remove_all (dir, ec);
    }
    fs::path fx (const std::string& rel) const { return u8 (root) / "Effects" / u8 (rel); }
    fs::path gen (const std::string& rel) const { return u8 (root) / "Generators" / u8 (rel); }
    bool has (const fs::path& p) const { return fs::exists (p); }
};
inline std::vector<lynx::plugindb::Plugin> plugins()
{
    using P = lynx::plugindb::Plugin;
    return {
        P { "lynx:fx:Fruity Limiter", "Fruity Limiter", "FL engines", false, true, "Dynamics" },
        P { "lynx:fx:Maximus", "Maximus", "FL engines", false, true, "Dynamics" },
        P { "lynx:fx:Facet EQ", "Facet EQ", "LYNX", false, true, "LYNX SHARE" },
        P { "lynx:gen:Sytrus", "Sytrus", "FL engines", true, true, "Synth classic" },
        P { "VST3-ProQ-1234", "Pro-Q 3", "VST3", false, false, "" },
        P { "VST-ProQ-99", "Pro-Q 3", "VST", false, false, "" },
        P { "CLAP-synth-1", "Vital", "CLAP", true, false, "" },
    };
}
inline int entryFiles (const fs::path& dir)
{
    int n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it (dir, ec), end; it != end && ! ec; it.increment (ec))
        n += it->path().extension() == lynx::plugindb::entryExtension ? 1 : 0;
    return n;
}
} // namespace plugindb190ba

TEST (plugindb190ba_safe_file_names)
{
    using lynx::plugindb::safeFileName;
    CHECK (safeFileName ("Pro-Q 3") == "Pro-Q 3");
    CHECK (safeFileName ("A/B\\C:D*E?F\"G<H>I|J") == "A_B_C_D_E_F_G_H_I_J");
    CHECK (safeFileName ("  name. . ") == "name");
    CHECK (safeFileName ("") == "Plugin" && safeFileName (" .. ") == "Plugin");
    CHECK (safeFileName ("CON") == "_CON" && safeFileName ("nul.txt") == "_nul.txt" && safeFileName ("COM1") == "_COM1" && safeFileName ("Console") == "Console");
    CHECK (safeFileName ("tab\there") == "tab_here");
    CHECK (safeFileName ("Ré\xe2\x80\xa2verb \xe6\x97\xa5\xe6\x9c\xac") == "Ré\xe2\x80\xa2verb \xe6\x97\xa5\xe6\x9c\xac"); // UTF-8 kept
    std::string longName;
    for (int i = 0; i < 100; ++i)
        longName += "\xc3\xa9"; // 200 bytes of "é"
    const auto cut = safeFileName (longName);
    CHECK (cut.size() == 120 && ((unsigned char) cut.back() & 0xc0) == 0x80); // cut between characters
}

TEST (plugindb190ba_entry_round_trip)
{
    lynx::plugindb::Entry e;
    e.key = "VST3-Some=Plugin-1";
    e.name = "Some = Plugin";
    e.format = "VST3";
    e.file = "Some = Plugin";
    e.shown = "1_EQ";
    e.instrument = true;
    lynx::plugindb::Entry back;
    CHECK (lynx::plugindb::parseEntry (lynx::plugindb::formatEntry (e), back));
    CHECK (back.key == e.key && back.name == e.name && back.format == e.format && back.file == e.file && back.shown == e.shown && back.instrument);
    CHECK (lynx::plugindb::parseEntry ("\xef\xbb\xbfLYNX plugin 1\r\nkey=k\r\nkind=effect\r\n", back) && back.key == "k" && ! back.instrument);
    CHECK (! lynx::plugindb::parseEntry ("something else\nkey=k\n", back));
    CHECK (! lynx::plugindb::parseEntry ("LYNX plugin 1\nname=no key\n", back));
}

TEST (plugindb190ba_first_run_makes_the_tree_from_the_settings)
{
    using namespace plugindb190ba;
    TempRoot t;
    lynx::plugindb::State state;
    // what LYNX kept until now: a folder with a subfolder, a built-in and a scanned plugin filed, a renamed plugin
    lynx::plugincatalog::moveToFolder (state.folders, false, "lynx:fx:Maximus", "Stock/Dynamics");
    lynx::plugincatalog::moveToFolder (state.folders, false, "VST3-ProQ-1234", "Third party");
    lynx::plugincatalog::addFolder (state.folders, true, "Empty one");
    state.names["lynx:fx:Fruity Limiter"] = "1_Limiter";
    state.folders.view = 2;
    const auto wanted = state;

    const auto r = lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (r.ok && r.migrated && r.created == 5); // 4 built-in ones + the filed VST3
    CHECK (t.has (t.fx ("Stock/Dynamics/Maximus.lynxplugin")));
    CHECK (t.has (t.fx ("Third party/Pro-Q 3.lynxplugin")));
    CHECK (t.has (t.fx ("Dynamics/1_Limiter.lynxplugin"))); // the renamed one under its name, in its default folder
    CHECK (t.has (t.fx ("LYNX SHARE/Facet EQ.lynxplugin")));
    CHECK (t.has (t.gen ("Synth classic/Sytrus.lynxplugin")));
    CHECK (fs::is_directory (t.gen ("Empty one")));
    CHECK (t.has (u8 (t.root) / "database.index") && t.has (u8 (t.root) / "Read me.txt"));
    // unfiled scanned plugins have no entry in the arrangement, but every plugin has one under Installed
    CHECK (entryFiles (u8 (t.root) / "Effects") == 4 && entryFiles (u8 (t.root) / "Generators") == 1);
    CHECK (t.has (u8 (t.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin") && t.has (u8 (t.root) / "Installed/Effects/VST/Pro-Q 3.lynxplugin"));
    CHECK (t.has (u8 (t.root) / "Installed/Generators/CLAP/Vital.lynxplugin") && t.has (u8 (t.root) / "Installed/Effects/LYNX/Facet EQ.lynxplugin"));
    CHECK (entryFiles (u8 (t.root) / "Installed") == 7);
    // the model is what it was (default folders are not the user's)
    CHECK (state == wanted && state.folders.view == 2);
    CHECK (state.folders.effects == (std::set<std::string> { "Stock", "Stock/Dynamics", "Third party" }));

    // again: nothing changes
    const auto again = lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (again.ok && ! again.migrated && again.created == 0 && again.moved == 0 && again.removed == 0 && state == wanted);
    CHECK (entryFiles (u8 (t.root)) == 12);
}

TEST (plugindb190ba_changes_made_in_the_file_manager_are_respected)
{
    using namespace plugindb190ba;
    TempRoot t;
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.of.empty() && state.folders.effects.empty() && state.names.empty());

    // a new folder with a subfolder, a built-in plugin moved into it
    fs::create_directories (t.fx ("Image-Line/Dynamics"));
    fs::rename (t.fx ("Dynamics/Maximus.lynxplugin"), t.fx ("Image-Line/Dynamics/Maximus.lynxplugin"));
    // a scanned plugin copied from Installed into a new folder (the way to file one), in a folder named with Unicode
    fs::create_directories (t.fx ("3rd Party/\xe6\x97\xa5\xe6\x9c\xac EQ"));
    fs::copy_file (u8 (t.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin", t.fx ("3rd Party/\xe6\x97\xa5\xe6\x9c\xac EQ/Pro-Q 3.lynxplugin"));
    // a rename, a delete, an empty folder
    fs::rename (t.fx ("Dynamics/Fruity Limiter.lynxplugin"), t.fx ("Dynamics/1_Limiter.lynxplugin"));
    fs::remove (t.fx ("LYNX SHARE/Facet EQ.lynxplugin"));
    fs::create_directories (t.gen ("Later"));

    auto r = lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (r.ok && r.created == 0); // the deleted one is not made again
    CHECK (state.folders.of.at ("lynx:fx:Maximus") == "Image-Line/Dynamics");
    CHECK (state.folders.of.at ("VST3-ProQ-1234") == "3rd Party/\xe6\x97\xa5\xe6\x9c\xac EQ");
    CHECK (state.folders.effects == (std::set<std::string> { "3rd Party", "3rd Party/\xe6\x97\xa5\xe6\x9c\xac EQ", "Image-Line", "Image-Line/Dynamics" }));
    CHECK (state.folders.generators == (std::set<std::string> { "Later" }));
    CHECK (state.names.at ("lynx:fx:Fruity Limiter") == "1_Limiter" && state.names.size() == 1);
    CHECK (state.folders.hidden == (std::set<std::string> { "lynx:fx:Facet EQ" }));
    CHECK (! state.folders.of.count ("lynx:fx:Fruity Limiter")); // still in its default folder

    // the menus: the deleted one has no folder, the others sit where the tree says
    std::vector<lynx::plugincatalog::BuiltinPlugin> builtins { { "lynx:fx:Fruity Limiter", "Fruity Limiter", false, true }, { "lynx:fx:Maximus", "Maximus", false, true },
                                                               { "lynx:fx:Facet EQ", "Facet EQ", false, false } };
    const auto menus = lynx::plugincatalog::withDefaultCategories (state.folders, false, builtins);
    CHECK (menus.of.at ("lynx:fx:Fruity Limiter") == "Dynamics" && menus.of.at ("lynx:fx:Maximus") == "Image-Line/Dynamics" && ! menus.of.count ("lynx:fx:Facet EQ"));
    const auto tree = lynx::plugincatalog::menuTree (menus, false, { "lynx:fx:Fruity Limiter", "lynx:fx:Maximus", "lynx:fx:Facet EQ", "VST3-ProQ-1234" });
    CHECK (tree.size() == 3 && tree[0].name == "3rd Party" && tree[1].name == "Dynamics" && tree[2].name == "Image-Line");
    CHECK (tree[2].sub.size() == 1 && tree[2].sub[0].items == std::vector<int> { 1 });

    // the entry copied back from Installed brings the plugin back; a folder renamed in the file manager is followed
    fs::copy_file (u8 (t.root) / "Installed/Effects/LYNX/Facet EQ.lynxplugin", t.fx ("Image-Line/Facet EQ.lynxplugin"));
    fs::rename (t.fx ("3rd Party"), t.fx ("Third"));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.hidden.empty() && state.folders.of.at ("lynx:fx:Facet EQ") == "Image-Line");
    CHECK (state.folders.of.at ("VST3-ProQ-1234") == "Third/\xe6\x97\xa5\xe6\x9c\xac EQ" && ! state.folders.effects.count ("3rd Party"));

    // a whole folder deleted: its built-in plugins leave the menus, the scanned one is simply unfiled
    fs::remove_all (t.fx ("Third"));
    fs::remove_all (t.fx ("Image-Line"));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.effects.empty() && ! state.folders.of.count ("VST3-ProQ-1234"));
    CHECK (state.folders.hidden == (std::set<std::string> { "lynx:fx:Facet EQ", "lynx:fx:Maximus" }));

    // a plugin that is new to LYNX gets its entry in its default folder; one that left keeps its file
    auto more = plugins();
    more.push_back ({ "lynx:fx:New One", "New One", "LYNX", false, true, "LYNX SHARE" });
    more.erase (more.begin()); // Fruity Limiter (e.g. the FL engines' data is not there now)
    r = lynx::plugindb::refresh (t.root, more, state);
    CHECK (r.created == 1 && t.has (t.fx ("LYNX SHARE/New One.lynxplugin")) && t.has (t.fx ("Dynamics/1_Limiter.lynxplugin")));
    CHECK (! t.has (u8 (t.root) / "Installed/Effects/FL engines/Fruity Limiter.lynxplugin") && t.has (u8 (t.root) / "Installed/Effects/LYNX/New One.lynxplugin"));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.names.at ("lynx:fx:Fruity Limiter") == "1_Limiter");
}

TEST (plugindb190ba_changes_made_in_lynx_go_to_the_tree)
{
    using namespace plugindb190ba;
    TempRoot t;
    const auto all = plugins();
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, all, state);
    auto edit = [&] (std::function<void (lynx::plugindb::State&)> change) {
        change (state);
        const auto r = lynx::plugindb::apply (t.root, all, state);
        lynx::plugindb::refresh (t.root, all, state);
        return r;
    };

    // Move to folder (a new one), for a built-in and for both builds of a scanned plugin with one name
    auto r = edit ([] (lynx::plugindb::State& s) {
        lynx::plugincatalog::moveToFolder (s.folders, false, "lynx:fx:Maximus", "Mix/Bus");
        lynx::plugincatalog::moveToFolder (s.folders, false, "VST3-ProQ-1234", "Mix/Bus");
        lynx::plugincatalog::moveToFolder (s.folders, false, "VST-ProQ-99", "Mix/Bus");
    });
    CHECK (r.ok && r.moved == 1 && r.created == 2);
    CHECK (t.has (t.fx ("Mix/Bus/Maximus.lynxplugin")) && ! t.has (t.fx ("Dynamics/Maximus.lynxplugin")));
    // two plugins of one name in one folder: the second one's file carries its format, and that is not a rename
    CHECK (t.has (t.fx ("Mix/Bus/Pro-Q 3.lynxplugin")) && t.has (t.fx ("Mix/Bus/Pro-Q 3 (VST).lynxplugin")));
    CHECK (state.names.empty() && state.folders.of.at ("VST3-ProQ-1234") == "Mix/Bus" && state.folders.of.at ("VST-ProQ-99") == "Mix/Bus");

    // Rename... and Original name
    r = edit ([] (lynx::plugindb::State& s) { s.names["lynx:fx:Maximus"] = "2_Max: bus?"; });
    CHECK (r.renamed == 1 && t.has (t.fx ("Mix/Bus/2_Max_ bus_.lynxplugin")) && ! t.has (t.fx ("Mix/Bus/Maximus.lynxplugin")));
    CHECK (state.names.at ("lynx:fx:Maximus") == "2_Max: bus?"); // the name itself, not the file's
    r = edit ([] (lynx::plugindb::State& s) { s.names.erase ("lynx:fx:Maximus"); });
    CHECK (r.renamed == 1 && t.has (t.fx ("Mix/Bus/Maximus.lynxplugin")) && state.names.empty());

    // Rename folder: its entries follow, the old folder goes
    r = edit ([] (lynx::plugindb::State& s) { CHECK (lynx::plugincatalog::renameFolder (s.folders, false, "Mix", "Mixing")); });
    CHECK (r.moved == 3 && t.has (t.fx ("Mixing/Bus/Maximus.lynxplugin")) && ! t.has (t.fx ("Mix")));
    CHECK (state.folders.effects == (std::set<std::string> { "Mixing", "Mixing/Bus" }));

    // a folder made in the file manager since the last refresh is not touched by a change made in LYNX
    fs::create_directories (t.fx ("Made outside"));
    r = edit ([] (lynx::plugindb::State& s) { lynx::plugincatalog::addFolder (s.folders, false, "Made in LYNX"); });
    CHECK (r.foldersMade == 1 && fs::is_directory (t.fx ("Made outside")) && fs::is_directory (t.fx ("Made in LYNX")));
    CHECK (state.folders.effects.count ("Made outside") && state.folders.effects.count ("Made in LYNX"));

    // No folder: a built-in one goes back to its default folder, a scanned one loses its entry
    r = edit ([] (lynx::plugindb::State& s) {
        lynx::plugincatalog::moveToFolder (s.folders, false, "lynx:fx:Maximus", "");
        lynx::plugincatalog::moveToFolder (s.folders, false, "VST-ProQ-99", "");
    });
    CHECK (r.moved == 1 && r.removed == 1 && t.has (t.fx ("Dynamics/Maximus.lynxplugin")) && state.folders.hidden.empty());
    CHECK (entryFiles (t.fx ("Mixing/Bus")) == 1);

    // Delete folder (its plugins go back): the folder and its subfolder are removed, another file in it keeps it
    {
        std::ofstream note (t.fx ("Made outside/notes.txt"));
        note << "mine";
    }
    r = edit ([&] (lynx::plugindb::State& s) {
        std::vector<std::string> keys;
        for (const auto& p : all) keys.push_back (p.key);
        lynx::plugincatalog::removeFolder (s.folders, false, "Mixing", keys);
        lynx::plugincatalog::removeFolder (s.folders, false, "Made outside", keys);
    });
    CHECK (r.foldersRemoved == 2 && ! t.has (t.fx ("Mixing")) && t.has (t.fx ("Made outside/notes.txt")));
    CHECK (! state.folders.of.count ("VST3-ProQ-1234") && ! state.folders.effects.count ("Mixing"));

    // a plugin taken out in the file manager comes back when it is filed in LYNX
    fs::remove (t.fx ("LYNX SHARE/Facet EQ.lynxplugin"));
    lynx::plugindb::refresh (t.root, all, state);
    CHECK (state.folders.hidden.count ("lynx:fx:Facet EQ"));
    edit ([] (lynx::plugindb::State& s) { lynx::plugincatalog::moveToFolder (s.folders, false, "lynx:fx:Facet EQ", "Back"); });
    CHECK (t.has (t.fx ("Back/Facet EQ.lynxplugin")) && state.folders.hidden.empty() && state.folders.of.at ("lynx:fx:Facet EQ") == "Back");

    // a folder name a file system cannot hold is stored under a safe one, and the model follows the tree
    edit ([] (lynx::plugindb::State& s) { lynx::plugincatalog::moveToFolder (s.folders, true, "CLAP-synth-1", "What?"); });
    CHECK (t.has (t.gen ("What_/Vital.lynxplugin")) && state.folders.of.at ("CLAP-synth-1") == "What_");
}

TEST (plugindb190ba_odd_trees)
{
    using namespace plugindb190ba;
    TempRoot t;
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, plugins(), state);
    // the same plugin in two folders: the first path counts, the other file is left alone
    fs::create_directories (t.fx ("A"));
    fs::create_directories (t.fx ("B"));
    fs::copy_file (t.fx ("Dynamics/Maximus.lynxplugin"), t.fx ("A/Maximus.lynxplugin"));
    fs::copy_file (t.fx ("Dynamics/Maximus.lynxplugin"), t.fx ("B/Maximus.lynxplugin"));
    // an effect's entry dropped under Generators, a file that is not an entry, an entry at the top, a folder too deep
    fs::copy_file (t.fx ("Dynamics/Maximus.lynxplugin"), t.gen ("Maximus.lynxplugin"));
    {
        std::ofstream junk (t.fx ("A/broken.lynxplugin"));
        junk << "not an entry";
    }
    fs::rename (t.fx ("LYNX SHARE/Facet EQ.lynxplugin"), t.fx ("Facet EQ.lynxplugin"));
    fs::create_directories (t.fx ("1/2/3/4/5/6"));
    fs::copy_file (u8 (t.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin", t.fx ("1/2/3/4/5/6/Pro-Q 3.lynxplugin"));
    const auto r = lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (r.ok && r.created == 0);
    CHECK (state.folders.of.at ("lynx:fx:Maximus") == "A" && t.has (t.fx ("B/Maximus.lynxplugin")) && t.has (t.fx ("Dynamics/Maximus.lynxplugin")));
    CHECK (! state.folders.of.count ("lynx:fx:Facet EQ") && state.folders.hidden.empty()); // at the top: its usual place in the menus
    CHECK (state.folders.of.at ("VST3-ProQ-1234") == "1/2/3/4" && state.folders.effects.count ("1/2/3/4") && ! state.folders.effects.count ("1/2/3/4/5"));
    CHECK (state.folders.generators.empty());
    // a change in LYNX leaves all of that where it is
    const auto a = lynx::plugindb::apply (t.root, plugins(), state);
    CHECK (a.ok && a.moved == 0 && a.removed == 0 && a.created == 0 && a.renamed == 0 && a.foldersRemoved == 0);
    // no folder to write to: nothing happens, the model stays
    auto keep = state;
    CHECK (! lynx::plugindb::refresh ("", plugins(), keep).ok && keep == state);
    {
        std::ofstream file (t.dir / "a file");
        file << "x";
    }
    CHECK (! lynx::plugindb::refresh (str (t.dir / "a file" / "db"), plugins(), keep).ok && keep == state);
    // an index lost with the tree still there: the tree is read, not made again from the settings
    fs::remove (u8 (t.root) / "database.index");
    lynx::plugindb::State stale;
    lynx::plugincatalog::moveToFolder (stale.folders, false, "lynx:fx:Maximus", "Old place");
    const auto lost = lynx::plugindb::refresh (t.root, plugins(), stale);
    CHECK (lost.ok && ! lost.migrated && stale.folders.of.at ("lynx:fx:Maximus") == "A" && ! t.has (t.fx ("Old place")));
}
