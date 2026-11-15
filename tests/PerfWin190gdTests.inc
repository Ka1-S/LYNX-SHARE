// Round 190gd (performance vs FL Studio on Windows): a start of LYNX whose plugin list and Installed folder match what it
// wrote last time reads none of the Installed entries (core/PluginDbFolder.h, installed.index), and an entry deleted,
// added or renamed in the file manager is still put right. Needs PluginDb190baTests.inc (plugindb190ba helpers) before it.

namespace perfwin190gd
{
inline std::string readText (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}
inline void writeText (const std::filesystem::path& p, const std::string& text)
{
    std::ofstream out (p, std::ios::binary | std::ios::trunc);
    out << text;
}
} // namespace perfwin190gd

TEST (perfwin190gd_installed_folder_is_not_read_again_when_nothing_changed)
{
    using namespace plugindb190ba;
    namespace fs = std::filesystem;
    TempRoot t;
    lynx::plugindb::State state;
    CHECK (lynx::plugindb::refresh (t.root, plugins(), state).ok);
    const auto index = u8 (t.root) / "installed.index";
    const auto proQ = u8 (t.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin";
    CHECK (t.has (index) && entryFiles (u8 (t.root) / "Installed") == 7);
    const auto written = perfwin190gd::readText (proQ);
    CHECK (written.find ("key=VST3-ProQ-1234") != std::string::npos);

    // a new start, same list, every entry in place: the entries are not read (a text edited by hand stays as it is)
    perfwin190gd::writeText (proQ, written + "note=edited\n");
    lynx::plugindb::forgetSession (t.root);
    CHECK (lynx::plugindb::refresh (t.root, plugins(), state).ok);
    CHECK (perfwin190gd::readText (proQ) == written + "note=edited\n");

    // a deleted entry is written again, a stray file is removed (by their names, on the next start)
    fs::remove (proQ);
    perfwin190gd::writeText (u8 (t.root) / "Installed/Effects/VST3/Stray.lynxplugin", "LYNX plugin 1\nkey=gone\n");
    lynx::plugindb::forgetSession (t.root);
    CHECK (lynx::plugindb::refresh (t.root, plugins(), state).ok);
    CHECK (perfwin190gd::readText (proQ) == written);
    CHECK (! t.has (u8 (t.root) / "Installed/Effects/VST3/Stray.lynxplugin") && entryFiles (u8 (t.root) / "Installed") == 7);

    // a changed plugin list (a new scan): its entry is written and the index follows; the hand-edited text of an entry
    // that is in place is brought back to what LYNX writes
    perfwin190gd::writeText (proQ, written + "note=edited\n");
    const auto before = perfwin190gd::readText (index);
    auto more = plugins();
    more.push_back ({ "VST3-Saturn-7", "Saturn 2", "VST3", false, false, "" });
    lynx::plugindb::forgetSession (t.root);
    CHECK (lynx::plugindb::refresh (t.root, more, state).ok);
    CHECK (t.has (u8 (t.root) / "Installed/Effects/VST3/Saturn 2.lynxplugin") && entryFiles (u8 (t.root) / "Installed") == 8);
    CHECK (perfwin190gd::readText (proQ) == written);
    CHECK (perfwin190gd::readText (index) != before && perfwin190gd::readText (index).rfind ("LYNX installed 1\n", 0) == 0);

    // and back to the first list: the scanned plugin that went away loses its Installed entry
    lynx::plugindb::forgetSession (t.root);
    CHECK (lynx::plugindb::refresh (t.root, plugins(), state).ok);
    CHECK (! t.has (u8 (t.root) / "Installed/Effects/VST3/Saturn 2.lynxplugin") && entryFiles (u8 (t.root) / "Installed") == 7);
    CHECK (perfwin190gd::readText (index) == before);
}
