// Round 190nq: issues #225 (Mixer Left / Right select), #227 (ruler click with every tool, LYNX extra), #228 (the action
// search, LYNX extra), #230 (FL's "Show fade previews").

#include "core/ActionSearch.h"
#include "core/ClipOps.h"
#include "core/KeyMap.h"
#include "core/LynxExtras.h"
#include "core/MixerOps.h"
#include "core/RulerGestures.h"

TEST (issues190nq_225_mixer_left_right_select_the_neighbour)
{
    using namespace lynx;
    auto noneHidden = [] (int) { return false; };
    CHECK (mixerops::neighbourTrack (10, 3, 1, noneHidden) == 4);
    CHECK (mixerops::neighbourTrack (10, 3, -1, noneHidden) == 2);
    CHECK (mixerops::neighbourTrack (10, 0, -1, noneHidden) == 0);  // Master: stays
    CHECK (mixerops::neighbourTrack (10, 9, 1, noneHidden) == 9);   // the last: stays
    CHECK (mixerops::neighbourTrack (10, 3, -2, noneHidden) == 1);
    // "Show only used inserts": hidden ones are skipped; none shown that way: stays
    auto hidden = [] (int t) { return t == 4 || t == 5 || t == 9; };
    CHECK (mixerops::neighbourTrack (10, 3, 1, hidden) == 6);
    CHECK (mixerops::neighbourTrack (10, 6, -1, hidden) == 3);
    CHECK (mixerops::neighbourTrack (10, 8, 1, hidden) == 8);

    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    // FL 2026: Left / Right, also with Ctrl or Shift, select; Alt moves (unchanged)
    for (const auto mods : { (std::uint8_t) keys::none, (std::uint8_t) keys::ctrl, (std::uint8_t) keys::shift })
    {
        CHECK (map.find (keys::Chord { keys::left, mods }, Context::mixer, false) == "mixer.selectPrevious");
        CHECK (map.find (keys::Chord { keys::right, mods }, Context::mixer, false) == "mixer.selectNext");
    }
    CHECK (map.find (keys::Chord { keys::left, keys::alt }, Context::mixer, false) == "mixer.moveLeft");
    CHECK (map.find (keys::Chord { keys::right, keys::alt }, Context::mixer, false) == "mixer.moveRight");
    // the other panels keep their arrows
    CHECK (map.find (keys::Chord { keys::left, keys::ctrl }, Context::playlist, false) == "playlist.timePrevious");
    CHECK (map.find (keys::Chord { keys::left, keys::none }, Context::browser, false) == "browser.closeFolder");
}

TEST (issues190nq_227_ruler_click_moves_the_song_position_with_every_tool)
{
    using namespace lynx::ruler;
    using lynx::EditTool;
    Press p;
    p.mods.left = true;
    p.tool = EditTool::lasso;
    CHECK (press (p) == Action::select); // FL 2026 (and the default): the Select tool keeps the song position
    p.tool = EditTool::zoom;
    CHECK (press (p) == Action::zoomRange);
    p.anyToolSeeks = true; // the LYNX extra
    CHECK (press (p) == Action::seek);
    p.tool = EditTool::lasso;
    CHECK (press (p) == Action::seek);
    p.mods.ctrl = true; // Ctrl+drag still selects a range
    CHECK (press (p) == Action::select);
    p.mods.ctrl = false;
    p.clicks = 2; // and a double-click drag
    CHECK (press (p) == Action::select);
    p.clicks = 1;
    p.onMarkerTag = true;
    CHECK (press (p) == Action::marker);
    Press r;
    r.mods.right = true;
    r.tool = EditTool::lasso;
    r.anyToolSeeks = true; // the right button keeps its range
    CHECK (press (r) == Action::select);
    CHECK (! seekWithAnyTool()); // off by default
    bool listed = false;
    for (const auto& item : lynx::extras::items)
        if (item.id == "playlist.rulerSeeksAnyTool")
            listed = item.settingsKey == "ui-options.txt:rulerSeeksAnyTool";
    CHECK (listed);
}

TEST (issues190nq_228_action_search_finds_and_ranks)
{
    using namespace lynx;
    std::vector<actionsearch::Entry> e {
        { "a", "Toggle the metronome", "Toggle the metronome", "Everywhere", "Ctrl+M" },
        { "b", "Metronome sound", "Metronome sound", "Everywhere", "" },
        { "c", "Select the next mixer track", "Select the next mixer track", "Mixer", "Right" },
        { "d", "Metronome-Klang", "Metronome sound (de)", "Überall", "" },
    };
    auto ids = [&] (const std::string& q) {
        std::string s;
        for (auto i : actionsearch::find (e, q))
            s += e[(std::size_t) i].id;
        return s;
    };
    CHECK (ids ("") == "abcd");          // everything, in the key map's order
    CHECK (ids ("metro") == "bda");      // names that start with it first, then names with a word that starts with it
    CHECK (ids ("mixer next") == "c");   // every word
    CHECK (ids ("ctrl+m") == "a");       // by its key
    CHECK (ids ("Mixer") == "c");        // by its panel
    CHECK (ids ("?") == "abcd");         // ("?" is no wildcard here)
    CHECK (ids ("zzz").empty());

    // every action of the key map is listed once, with a description
    std::set<std::string> seen;
    for (const auto& a : keys::actions())
    {
        CHECK (seen.insert (a.id).second);
        CHECK (a.description != nullptr && *a.description != 0);
    }
    CHECK (keys::findAction ("help.searchActions") != nullptr);
}

TEST (issues190nq_230_fade_preview_gain_follows_the_drawn_curve)
{
    using namespace lynx;
    Clip c;
    c.kind = ClipKind::audio;
    c.length = 1000;
    CHECK (clipops::fadePreviewGain (c, 500.0) == 1.0f); // no fades: untouched
    c.fadeIn = 100;
    c.fadeOut = 200;
    CHECK (clipops::fadePreviewGain (c, 0.0) == 0.0f);
    CHECK (std::abs (clipops::fadePreviewGain (c, 50.0) - clipops::fadeLevel (0.5f, true, false, 0.0f)) < 1.0e-6f);
    CHECK (clipops::fadePreviewGain (c, 100.0) == 1.0f);
    CHECK (clipops::fadePreviewGain (c, 500.0) == 1.0f);
    CHECK (std::abs (clipops::fadePreviewGain (c, 900.0) - clipops::fadeLevel (0.5f, false, false, 0.0f)) < 1.0e-6f);
    CHECK (clipops::fadePreviewGain (c, 1000.0) < 1.0e-6f);
    c.linearFades = 2; // a straight fade out
    CHECK (std::abs (clipops::fadePreviewGain (c, 950.0) - 0.25f) < 1.0e-5f);
    c.crossfadeOut = 300; // an automatic crossfade replaces the fade: FL's S over the overlap
    CHECK (std::abs (clipops::fadePreviewGain (c, 850.0) - clipops::crossfadeCurve (0.5f)) < 1.0e-6f);
    // every value stays in 0..1, and the curve falls along a fade out
    float last = 2.0f;
    for (double t = 700.0; t <= 1000.0; t += 10.0)
    {
        const auto g = clipops::fadePreviewGain (c, t);
        CHECK (g >= 0.0f && g <= 1.0f && g <= last + 1.0e-6f);
        last = g;
    }
}
