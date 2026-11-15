// Round 190go (start-up faster than FL Studio): the app writes the Plugin database's Installed folder on a thread of its
// own (refresh with installedLater); the folder ends up exactly as a synchronous refresh leaves it, a reader of
// Installed (entryFile) waits for the write, and a later change of the plugin list is written the same way.
// Needs PluginDb190baTests.inc (plugindb190ba helpers) and PerfWin190gdTests.inc (perfwin190gd helpers) before it.

TEST (startup190go_installed_written_in_the_background_matches_a_synchronous_refresh)
{
    using namespace plugindb190ba;
    TempRoot sync, later;
    lynx::plugindb::State a, b;
    CHECK (lynx::plugindb::refresh (sync.root, plugins(), a).ok);
    CHECK (lynx::plugindb::refresh (later.root, plugins(), b, true).ok);
    // the state (the menus' folders and names) is there at once: it comes from the user's tree, not from Installed
    CHECK (a.folders.of == b.folders.of && a.folders.effects == b.folders.effects && a.folders.generators == b.folders.generators && a.names == b.names);
    // a reader of Installed waits for the write in flight
    const auto entry = lynx::plugindb::entryFile (later.root, "VST3-ProQ-1234", false);
    CHECK (! entry.empty());
    lynx::plugindb::finishInstalledWrites();
    CHECK (entryFiles (u8 (later.root) / "Installed") == 7 && entryFiles (u8 (sync.root) / "Installed") == 7);
    CHECK (perfwin190gd::readText (u8 (later.root) / "installed.index") == perfwin190gd::readText (u8 (sync.root) / "installed.index"));
    CHECK (perfwin190gd::readText (u8 (later.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin")
           == perfwin190gd::readText (u8 (sync.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin"));

    // a scan adds a plugin: written in the background too; two refreshes in a row leave the newest list
    auto more = plugins();
    more.push_back ({ "VST3-Saturn-7", "Saturn 2", "VST3", false, false, "" });
    CHECK (lynx::plugindb::refresh (later.root, more, b, true).ok);
    CHECK (lynx::plugindb::refresh (later.root, plugins(), b, true).ok);
    CHECK (lynx::plugindb::refresh (later.root, more, b, true).ok);
    lynx::plugindb::finishInstalledWrites();
    CHECK (later.has (u8 (later.root) / "Installed/Effects/VST3/Saturn 2.lynxplugin") && entryFiles (u8 (later.root) / "Installed") == 8);

    // a new start of the same list: nothing to write, the folder stays as it is
    lynx::plugindb::forgetSession (later.root);
    CHECK (lynx::plugindb::refresh (later.root, more, b, true).ok);
    lynx::plugindb::finishInstalledWrites();
    CHECK (entryFiles (u8 (later.root) / "Installed") == 8);
    CHECK (lynx::plugindb::groupFolder (later.root, false, "") == (u8 (later.root) / "Installed" / "Effects").generic_string());
}
