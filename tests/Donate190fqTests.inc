// Round 190fq: an optional, low-key way to donate (LYNX's own; FL has nothing like it). The rules of the start-up
// support note (core/SupportNote.h): the 10th start shows it, "Not now" waits 30 more starts, "Don't show again" and
// the Settings box off mean never, test / CI runs never count or show it. The donation page is one constant
// (core/Links.h) used everywhere; Help › Support LYNX SHARE... is in the Help menu of every layout. The app side
// (the card in the window, the menus of the three layouts, the windows) is checked by LYNX --donateshot.
#include "core/Links.h"
#include "core/SupportNote.h"

namespace donate190fq
{
using namespace lynx;

// n starts of a person's app; true if the note was due at any of them (each due start shows it: noteShown)
inline int startsUntilShown (supportnote::State& s, int maxStarts)
{
    for (int k = 1; k <= maxStarts; ++k)
        if (supportnote::countStart (s, false))
        {
            supportnote::noteShown (s);
            return k;
        }
    return -1;
}

inline std::string fileText (const char* path)
{
    std::ifstream in (path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

inline int count (const std::string& text, const std::string& what)
{
    int n = 0;
    for (auto at = text.find (what); at != std::string::npos; at = text.find (what, at + what.size()))
        ++n;
    return n;
}
} // namespace donate190fq

TEST (donate190fq_the_tenth_start_shows_the_note_once)
{
    using namespace lynx::supportnote;
    State s;
    CHECK (s.launches == 0 && s.nextAt == 10 && s.show);
    for (int k = 1; k <= 9; ++k)
        CHECK (! countStart (s, false));
    CHECK (s.launches == 9);
    CHECK (countStart (s, false)); // the 10th
    noteShown (s);
    CHECK (s.nextAt == 40);
    // the card left without an answer: like Not now, starts 11..39 are quiet, the 40th shows it again
    for (int k = 11; k <= 39; ++k)
        CHECK (! countStart (s, false));
    CHECK (countStart (s, false));
    CHECK (s.launches == 40);
}

TEST (donate190fq_not_now_waits_thirty_more_starts)
{
    using namespace lynx::supportnote;
    State s;
    CHECK (donate190fq::startsUntilShown (s, 100) == 10);
    choose (s, Choice::notNow);
    CHECK (s.nextAt == 40 && s.show);
    CHECK (donate190fq::startsUntilShown (s, 100) == 30); // start 40
    choose (s, Choice::notNow);
    CHECK (donate190fq::startsUntilShown (s, 100) == 30); // start 70
    CHECK (s.launches == 70);
}

TEST (donate190fq_dont_show_again_and_support_mean_never)
{
    using namespace lynx::supportnote;
    for (auto c : { Choice::never, Choice::support })
    {
        State s;
        CHECK (donate190fq::startsUntilShown (s, 100) == 10);
        choose (s, c);
        CHECK (! s.show);
        CHECK (donate190fq::startsUntilShown (s, 5000) == -1);
        CHECK (s.launches == 5010); // (still counted)
    }
}

TEST (donate190fq_the_settings_box_off_means_never_and_on_counts_from_now)
{
    using namespace lynx::supportnote;
    State s;
    setShow (s, false);
    CHECK (donate190fq::startsUntilShown (s, 1000) == -1);
    CHECK (s.launches == 1000);
    setShow (s, true); // on again: not at the very next start, 30 starts later
    CHECK (s.show && s.nextAt == 1030);
    CHECK (donate190fq::startsUntilShown (s, 100) == 30);
    // on again before the first mark was passed: the mark stays
    State fresh;
    setShow (fresh, false);
    for (int k = 0; k < 3; ++k)
        countStart (fresh, false);
    setShow (fresh, true);
    CHECK (fresh.nextAt == 10);
    CHECK (donate190fq::startsUntilShown (fresh, 100) == 7);
}

TEST (donate190fq_test_and_ci_runs_never_count_or_show)
{
    using namespace lynx::supportnote;
    State s;
    for (int k = 0; k < 1000; ++k)
        CHECK (! countStart (s, true));
    CHECK (s == State {});
    CHECK (quietEnvironment (true, "", "", ""));        // LYNX_TEST_MODE
    CHECK (quietEnvironment (false, "true", "", ""));   // CI=true (GitHub, most CI services)
    CHECK (quietEnvironment (false, "1", "", ""));
    CHECK (quietEnvironment (false, "", "true", ""));   // GITHUB_ACTIONS
    CHECK (quietEnvironment (false, "", "", "1"));      // LYNX_NO_SUPPORT_NOTE
    CHECK (! quietEnvironment (false, "", "", ""));     // a person's start
    CHECK (! quietEnvironment (false, "0", "false", "0"));
    // the start path: only after the command-line modes (they return before the window), and only through the quiet check
    const auto main = donate190fq::fileText ("src/Main.cpp");
    CHECK (donate190fq::count (main, "startSupportNote();") == 2); // both start paths (with and without the splash)
    CHECK (main.find ("lynx::supportnote::quietEnvironment (lynx::platform::testMode(), env (\"CI\"), env (\"GITHUB_ACTIONS\"), env (\"LYNX_NO_SUPPORT_NOTE\"))")
           != std::string::npos);
}

TEST (donate190fq_the_card_waits_for_a_quiet_moment)
{
    using namespace lynx::supportnote;
    CHECK (canShowNow ({}));
    CHECK (! canShowNow ({ true, false, false, true }));  // a dialog / overlay in front
    CHECK (! canShowNow ({ false, true, false, true }));  // playing
    CHECK (! canShowNow ({ false, false, true, true }));  // recording
    CHECK (! canShowNow ({ false, false, false, false })); // minimised
}

TEST (donate190fq_state_round_trips_and_survives_a_broken_file)
{
    using namespace lynx::supportnote;
    const State s { 57, 70, false };
    CHECK (toText (s) == "launches=57\nnext=70\nshow=0\n");
    CHECK (fromText (toText (s)) == s);
    CHECK (fromText ("launches=12\r\nnext=40\r\nshow=1\r\n") == (State { 12, 40, true }));
    CHECK (fromText ("") == State {});
    CHECK (fromText ("garbage\nlaunches=x\nnext=-4\nother=1") == State {});
    CHECK (fromText ("show=0") == (State { 0, 10, false }));
}

TEST (donate190fq_one_donation_url_everywhere)
{
    using donate190fq::fileText;
    const std::string url = lynx::links::donate;
    CHECK (url.rfind ("https://ko-fi.com/", 0) == 0);
    const auto name = url.substr (std::string ("https://ko-fi.com/").size());
    CHECK (! name.empty() && name.find ('/') == std::string::npos);
    // the app: only core/Links.h names the page; the Support window and the card go through links::donate
    for (const auto& entry : std::filesystem::recursive_directory_iterator ("src"))
    {
        if (! entry.is_regular_file())
            continue;
        const auto path = entry.path().generic_string();
        if (path == "src/core/Links.h" || path.find ("/i18n/") != std::string::npos || path.find ("I18nData") != std::string::npos)
            continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".inc" && ext != ".mm")
            continue;
        const auto text = fileText (path.c_str());
        if (text.find ("ko-fi.com") != std::string::npos)
        {
            std::printf ("  %s names the Ko-fi page itself (use lynx::links::donate)\n", path.c_str());
            CHECK (false);
        }
    }
    CHECK (fileText ("src/ui/SupportLynx.cpp").find ("juce::URL (links::donate).launchInDefaultBrowser()") != std::string::npos);
    // the repository's texts carry the same page
    CHECK (fileText (".github/FUNDING.yml").find ("ko_fi: " + name + "\n") != std::string::npos);
    CHECK (fileText ("README.md").find (url) != std::string::npos);
    CHECK (fileText ("website/_src/build.py").find ("KOFI = \"" + url + "\"") != std::string::npos);
}

TEST (donate190fq_help_has_support_next_to_about_in_every_layout)
{
    // the Help menu is one branch for the three layouts (Pattern, Clip-launch, Simple): the item sits right above
    // About, outside the Simple-only block at the menu's top (LYNX --donateshot checks the built menus of all three)
    const auto ui = donate190fq::fileText ("src/ui/MainComponent.cpp");
    const auto item = ui.find ("menu.addItem (tr (\"Support LYNX SHARE...\"), [] { support::showSupportWindow(); });\n        menu.addItem (tr (\"About LYNX SHARE\")");
    CHECK (item != std::string::npos);
    CHECK (donate190fq::count (ui, "tr (\"Support LYNX SHARE...\")") == 1);
    const auto simpleOnly = ui.rfind ("if (layoutStyle == layoutstyle::Style::simple && simpleWorkflow != nullptr)", item);
    const auto blockEnd = ui.find ("menu.addItem (tr (\"Shortcuts\")", simpleOnly);
    CHECK (simpleOnly != std::string::npos && blockEnd != std::string::npos && blockEnd < item);
    CHECK (ui.find ("layoutStyle", blockEnd) > item); // (no layout condition between the common items and it)
    // the About window's button opens the same window
    CHECK (ui.find ("if (result == 2) // \"Support LYNX SHARE\"\n                                             support::showSupportWindow();") != std::string::npos);
}
