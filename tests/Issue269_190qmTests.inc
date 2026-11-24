// Round 190qm: issue #269 — a right-click on the hint bar crashed 0.1.30 (every state of the bar).
// HintBar::mouseDown walked its copy items with `PopupMenu::MenuItemIterator it (copyMenu())`: JUCE's iterator keeps a
// pointer to the menu it walks, the temporary menu died at the end of the declaration, and copying each Item read freed
// memory (segfault in juce::String's copy, PopupMenu::Item's copy, HintBar::mouseDown: the user's crash report and the
// local runs of 0.1.30 and rollup031 under Xvfb). lynx_tests has no UI (lynx_core only), so these tests read the sources
// (skipped when they are not next to the tests); --details190knshot builds the real menu and xdotool clicks the real bar.

#include <filesystem>
#include <fstream>
#include <sstream>

namespace issue269
{
// The repository's src/ folder: next to this file (an absolute __FILE__), or found from the working folder (the cached
// builds compile with relative paths, and the suite runs from the repository root or from the build folder)
inline std::filesystem::path sourceRoot()
{
    const auto here = std::filesystem::path (__FILE__).parent_path().parent_path() / "src";
    for (const auto& c : { here, std::filesystem::current_path() / "src", std::filesystem::current_path().parent_path() / "src" })
        if (std::filesystem::is_regular_file (c / "ui" / "HintBar.cpp"))
            return c;
    return {};
}

inline std::string readSource (const std::filesystem::path& file)
{
    std::ifstream in (file);
    if (! in)
        return {};
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// The first argument of every `MenuItemIterator <name> (<arg>...)` in `s` that is a call (holds a "(") and is not a
// dereference ("*it.getItem().subMenu" walks a menu its parent owns): such an argument is a temporary menu.
inline std::vector<std::string> iteratorsOverTemporaries (const std::string& s)
{
    std::vector<std::string> found;
    const std::string key = "MenuItemIterator";
    for (auto at = s.find (key); at != std::string::npos; at = s.find (key, at + key.size()))
    {
        auto p = at + key.size();
        while (p < s.size() && s[p] == ' ')
            ++p;
        const auto nameStart = p;
        while (p < s.size() && (std::isalnum ((unsigned char) s[p]) || s[p] == '_'))
            ++p;
        if (p == nameStart) // (the class itself: "MenuItemIterator (const PopupMenu&...", "MenuItemIterator&")
            continue;
        while (p < s.size() && s[p] == ' ')
            ++p;
        if (p >= s.size() || (s[p] != '(' && s[p] != '{'))
            continue;
        // the first argument: up to the matching close or a top-level comma
        int depth = 0;
        std::string arg;
        for (++p; p < s.size(); ++p)
        {
            const auto c = s[p];
            if ((c == ')' || c == '}' || c == ',') && depth == 0)
                break;
            if (c == '(' || c == '[' || c == '{')
                ++depth;
            if (c == ')' || c == ']' || c == '}')
                --depth;
            arg += c;
        }
        const auto first = arg.find_first_not_of (" \t\n");
        if (arg.find ('(') != std::string::npos && first != std::string::npos && arg[first] != '*')
            found.push_back (arg);
    }
    return found;
}
} // namespace issue269

TEST (issue269_no_menu_item_iterator_walks_a_temporary_menu)
{
    // the scanner itself: the bug's line is caught, the safe forms are not
    CHECK (issue269::iteratorsOverTemporaries ("for (juce::PopupMenu::MenuItemIterator it (copyMenu()); it.next();)").size() == 1);
    CHECK (issue269::iteratorsOverTemporaries ("for (juce::PopupMenu::MenuItemIterator it (cpu.makeMenu(), true); it.next();)").size() == 1);
    CHECK (issue269::iteratorsOverTemporaries ("for (juce::PopupMenu::MenuItemIterator it (copy); it.next();)").empty());
    CHECK (issue269::iteratorsOverTemporaries ("juce::PopupMenu::MenuItemIterator it (menus[2], true);").empty());
    CHECK (issue269::iteratorsOverTemporaries ("for (juce::PopupMenu::MenuItemIterator sub (*it.getItem().subMenu); sub.next();)").empty());
    CHECK (issue269::iteratorsOverTemporaries ("MenuItemIterator (const PopupMenu& menu, bool searchRecursively = false);").empty());

    // every source file of the app: none left
    const auto root = issue269::sourceRoot();
    if (root.empty())
        return;
    int files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (root))
    {
        const auto ext = entry.path().extension().string();
        if (! entry.is_regular_file() || (ext != ".cpp" && ext != ".h" && ext != ".inc"))
            continue;
        const auto s = issue269::readSource (entry.path());
        if (s.find ("MenuItemIterator") == std::string::npos)
            continue;
        ++files;
        const auto bad = issue269::iteratorsOverTemporaries (s);
        if (! bad.empty())
            std::fprintf (stderr, "  issue269: %s walks a temporary menu: %s\n", entry.path().string().c_str(), bad.front().c_str());
        CHECK (bad.empty());
    }
    CHECK (files >= 10); // (the scan saw the sources: the app walks menus in many places)
}

TEST (issue269_hint_bar_right_click_builds_its_menu_from_named_menus)
{
    const auto root = issue269::sourceRoot();
    if (root.empty())
        return;
    const auto s = issue269::readSource (root / "ui" / "HintBar.cpp");
    if (s.empty())
        return;
    // the right-click shows rightClickMenu(), which walks a named copy of copyMenu()
    const auto down = s.find ("void HintBar::mouseDown");
    const auto build = s.find ("juce::PopupMenu HintBar::rightClickMenu() const");
    CHECK (down != std::string::npos && build != std::string::npos);
    if (down == std::string::npos || build == std::string::npos)
        return;
    const auto downBody = s.substr (down, s.find ("\n}", down) - down);
    CHECK (downBody.find ("isPopupMenu()") != std::string::npos);
    CHECK (downBody.find ("rightClickMenu().showMenuAsync") != std::string::npos);
    CHECK (downBody.find ("MenuItemIterator") == std::string::npos);
    const auto buildBody = s.substr (build, s.find ("\n}", build) - build);
    CHECK (buildBody.find ("const auto copy = copyMenu();") != std::string::npos);
    CHECK (buildBody.find ("MenuItemIterator it (copy)") != std::string::npos);
    CHECK (buildBody.find ("onMenu") != std::string::npos);
    CHECK (s.find ("MenuItemIterator it (copyMenu())") == std::string::npos);
}
