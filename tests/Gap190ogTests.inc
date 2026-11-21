// Round gap190og (0.1.31, coverage rows still uncovered after round 190og): Live's Duplicate Loop (live1:detail-019),
// Crop Clip (live1:detail-020) and the Velocity lane's Randomize with its amount (live1:detail-047) on a MIDI clip of the
// clip grid (core/ClipLoopOps.h); the transport switches Live maps besides Play / Stop / Record / Metronome
// (live4:mapping-0028: Arrangement Overdub, Loop, Punch-In, Punch-Out; core/RemoteMap.h); the Clip-launch layout's Edit
// menu on the Device View (live4:deviceview-0012; the UI wiring is checked in the sources and by --gapshot190og).

#include "core/ClipLoopOps.h"
#include "core/Preferences.h"
#include "core/RemoteMap.h"
#include "core/SessionKeys.h"

namespace gap190og
{
inline lynx::Note note (std::int64_t start, std::int64_t length, int key, float velocity = 0.78f)
{
    lynx::Note n;
    n.start = start;
    n.length = length;
    n.key = key;
    n.velocity = velocity;
    return n;
}
// pattern 0 with these notes (the clip below plays it)
inline lynx::Song song (std::vector<lynx::Note> notes)
{
    lynx::Song s;
    s.patterns.clear();
    s.patterns.emplace_back();
    s.patterns[0].notes = std::move (notes);
    s.patterns[0].sortNotes();
    return s;
}
inline lynx::Clip clip (std::int64_t offset, std::int64_t length)
{
    lynx::Clip c;
    c.pattern = 0;
    c.offset = offset;
    c.length = length;
    return c;
}
inline std::string starts (const lynx::Pattern& p)
{
    std::string out;
    for (const auto& n : p.notes)
        out += (out.empty() ? "" : ",") + std::to_string (n.start) + ":" + std::to_string (n.key);
    return out;
}
inline std::string fileText (const char* path)
{
    std::ifstream in (path, std::ios::binary); // (run from the source folder, as round 190fq's checks)
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
} // namespace gap190og

TEST (gap190og_duplicate_loop_copies_the_loop_and_moves_what_follows)
{
    using namespace gap190og;
    const auto bar = 4 * lynx::ppq;
    // a one-bar loop at the start: notes at 0 and beat 2 inside, one at bar 1 beat 2 right of the loop
    auto s = song ({ note (0, 240, 60), note (2 * lynx::ppq, 240, 62), note (bar + lynx::ppq, 240, 64) });
    auto c = clip (0, bar);
    CHECK (lynx::cliploop::duplicateLoop (s, c));
    // "duplicate the loop's length and its contents": copies a bar later; "any notes to the right of the loop will be
    // moved, so that they maintain their position relative to the end of the loop"
    CHECK (starts (s.patterns[0]) == "0:60,1920:62,3840:60,5760:62,8640:64");
    CHECK (c.offset == 0 && c.length == 2 * bar);
    // a loop that does not start the pattern: what lies before it stays, a note crossing the loop's end is copied whole
    auto s2 = song ({ note (0, 240, 48), note (bar, 240, 60), note (bar + 3 * lynx::ppq, 2 * lynx::ppq, 61), note (2 * bar, 240, 70) });
    auto c2 = clip (bar, bar);
    s2.patterns[0].lengthTicks = 3 * bar;
    CHECK (lynx::cliploop::duplicateLoop (s2, c2));
    CHECK (starts (s2.patterns[0]) == "0:48,3840:60,6720:61,7680:60,10560:61,11520:70");
    CHECK (s2.patterns[0].notes[2].length == 2 * lynx::ppq && s2.patterns[0].notes[4].length == 2 * lynx::ppq);
    CHECK (c2.offset == bar && c2.length == 2 * bar && s2.patterns[0].lengthTicks == 4 * bar);
    // a pattern's events go the same way (one per tick)
    auto s3 = song ({ note (0, 240, 60) });
    lynx::PatternEvents lane;
    lane.events = { { 0, 0.1f }, { lynx::ppq, 0.2f }, { bar + 10, 0.9f } };
    s3.patterns[0].events.push_back (lane);
    auto c3 = clip (0, bar);
    CHECK (lynx::cliploop::duplicateLoop (s3, c3));
    const auto& ev = s3.patterns[0].events[0].events;
    CHECK (ev.size() == 5 && ev[0].tick == 0 && ev[1].tick == lynx::ppq && ev[2].tick == bar && ev[2].value == 0.1f
           && ev[3].tick == bar + lynx::ppq && ev[4].tick == 2 * bar + 10 && ev[4].value == 0.9f);
    // twice: four bars, the right note moved on again
    CHECK (lynx::cliploop::duplicateLoop (s, c) && c.length == 4 * bar && s.patterns[0].notes.size() == 9 && s.patterns[0].notes.back().start == 4 * bar + lynx::ppq);
    // not a MIDI clip: nothing changes
    auto audio = clip (0, bar);
    audio.kind = lynx::ClipKind::audio;
    const auto before = s.patterns[0].notes;
    CHECK (! lynx::cliploop::duplicateLoop (s, audio) && s.patterns[0].notes == before && audio.length == bar);
    auto missing = clip (0, bar);
    missing.pattern = 7;
    CHECK (! lynx::cliploop::duplicateLoop (s, missing));
}

TEST (gap190og_crop_clip_keeps_only_the_loop)
{
    using namespace gap190og;
    const auto bar = 4 * lynx::ppq;
    // loop = bar 2 (ticks 3840..7680): a note before it, one inside, one running past its end, one after it
    auto s = song ({ note (0, 240, 48), note (bar - 240, 480, 49), note (bar + 480, 240, 60), note (2 * bar - 240, 960, 62), note (2 * bar, 240, 70) });
    lynx::PatternEvents lane;
    lane.events = { { 10, 0.1f }, { bar + 20, 0.5f }, { 2 * bar, 0.9f } };
    s.patterns[0].events.push_back (lane);
    auto c = clip (bar, bar);
    CHECK (lynx::cliploop::cropClip (s, c));
    // "MIDI data that is outside of the loop brace can be deleted using the Crop Clip command"
    CHECK (starts (s.patterns[0]) == "480:60,3600:62");
    CHECK (s.patterns[0].notes[1].length == 240); // (cut at the loop's end)
    CHECK (s.patterns[0].events[0].events.size() == 1 && s.patterns[0].events[0].events[0].tick == 20);
    CHECK (c.offset == 0 && c.length == bar && s.patterns[0].lengthTicks == bar);
    // cropping again changes nothing
    const auto once = s.patterns[0].notes;
    CHECK (lynx::cliploop::cropClip (s, c) && s.patterns[0].notes == once && c.offset == 0 && c.length == bar);
    auto audio = clip (0, bar);
    audio.kind = lynx::ClipKind::audio;
    CHECK (! lynx::cliploop::cropClip (s, audio));
}

TEST (gap190og_velocity_randomize_moves_by_at_most_the_amount)
{
    using namespace gap190og;
    lynx::Pattern p;
    for (int i = 0; i < 64; ++i)
        p.notes.push_back (note (i * 240, 240, 60, (float) (1 + (i * 2) % 127) / 127.0f));
    const auto before = p.notes;
    std::vector<int> all (64);
    for (int i = 0; i < 64; ++i)
        all[(std::size_t) i] = i;
    lynx::cliploop::randomizeVelocity (p, all, 10, 7u);
    auto moved = 0;
    for (int i = 0; i < 64; ++i)
    {
        const auto a = (int) std::lround (before[(std::size_t) i].velocity * 127.0f), b = (int) std::lround (p.notes[(std::size_t) i].velocity * 127.0f);
        CHECK (std::abs (b - a) <= 10 && b >= 1 && b <= 127);
        moved += a != b ? 1 : 0;
        CHECK (std::abs (p.notes[(std::size_t) i].velocity * 127.0f - (float) b) < 1e-3f); // (whole steps of Live's 1..127)
    }
    CHECK (moved > 40);
    // the same seed gives the same result; the amount 0 changes nothing; only the notes asked for change
    auto q = lynx::Pattern();
    q.notes = before;
    lynx::cliploop::randomizeVelocity (q, all, 10, 7u);
    CHECK (q.notes == p.notes);
    q.notes = before;
    lynx::cliploop::randomizeVelocity (q, all, 0, 7u);
    CHECK (q.notes == before);
    lynx::cliploop::randomizeVelocity (q, { 3, 5 }, 127, 1u);
    for (int i = 0; i < 64; ++i)
        if (i != 3 && i != 5)
            CHECK (q.notes[(std::size_t) i] == before[(std::size_t) i]);
    // the extremes stay on 1..127 with the largest amount
    lynx::Pattern edge;
    edge.notes = { note (0, 240, 60, 1.0f / 127.0f), note (240, 240, 60, 1.0f) };
    for (std::uint32_t seed = 1; seed < 50; ++seed)
    {
        lynx::cliploop::randomizeVelocity (edge, { 0, 1 }, 127, seed);
        for (const auto& n : edge.notes)
            CHECK (n.velocity >= 1.0f / 127.0f - 1e-6f && n.velocity <= 1.0f + 1e-6f);
    }
}

TEST (gap190og_transport_switches_are_mappable)
{
    using namespace lynx::remotemap;
    using lynx::RemoteAction;
    // appended after round 190kc's actions: saved projects and settings keep their numbers
    CHECK ((int) RemoteAction::launchSelectedClip == 17 && (int) RemoteAction::overdub == 18 && (int) RemoteAction::loopSwitch == 19
           && (int) RemoteAction::punchIn == 20 && (int) RemoteAction::punchOut == 21);
    for (const auto a : { RemoteAction::overdub, RemoteAction::loopSwitch, RemoteAction::punchIn, RemoteAction::punchOut })
    {
        lynx::Song s;
        CHECK (canBeGlobal (Target::of (a)));
        assignKey (s, Target::of (a), 'P');
        CHECK (s.remoteMappings.size() == 1 && s.remoteMappings[0].action == a);
        const auto fires = keyFires (s, 'P', true);
        CHECK (fires.size() == 1 && fires[0].target.action == a && fires[0].edge == Edge::press);
        s.removeDeadMidiLinks(); // (a transport switch never goes with a deleted target)
        CHECK (s.remoteMappings.size() == 1);
    }
    // the settings keep a mapping of them for every project (and round 190kc's crossfader / relative strip ones), not a
    // track's clip
    const auto kept = lynx::prefs::MidiPrefs::fromText ("globalmap=18|0|20|-1|-1\nglobalmap=21|0|21|-1|-1\nglobalmap=10|0|22|-1|-1\nglobalmap=17|0|23|-1|-1\nglobalmap=22|0|24|-1|-1\n").globalMappings;
    CHECK (kept.size() == 3 && kept[0].action == 18 && kept[1].action == 21 && kept[2].action == 10);
}

TEST (gap190og_crop_clip_key_and_live_edit_menu_wiring)
{
    using namespace lynx::sessionkeys;
    using lynx::layoutstyle::Style;
    // Live's Crop Clip key (its clip menu: "Crop Clip  Ctrl+Shift+J"), only in the Clip-launch layout
    CHECK (std::string (menuKey (Command::gridCropClip, Style::clipLaunch)) == "\tCtrl+Shift+J");
    CHECK (std::string (menuKey (Command::gridCropClip, Style::pattern)).empty() && ! defaultKeyActs (Command::gridCropClip, Style::pattern));
    const auto main = gap190og::fileText ("src/ui/MainComponent.cpp");
    const auto detail = gap190og::fileText ("src/ui/DetailView.cpp");
    const auto grid = gap190og::fileText ("src/ui/ClipGridComponent.cpp");
    const auto roll = gap190og::fileText ("src/ui/PianoRollComponent.cpp");
    // the Edit menu's Live block only in the Clip-launch layout, after Undo / Redo, in Live's order with its keys
    CHECK (main.find ("if (layoutStyle == layoutstyle::Style::clipLaunch) // (round gap190og: Live's Edit block after Undo / Redo)") != std::string::npos);
    const auto cut = main.find ("{ TR_NOOP (\"Cut\"), \"Ctrl+X\"");
    const auto copy = main.find ("{ TR_NOOP (\"Copy\"), \"Ctrl+C\"");
    const auto paste = main.find ("{ TR_NOOP (\"Paste\"), \"Ctrl+V\"");
    const auto dup = main.find ("{ TR_NOOP (\"Duplicate\"), \"Ctrl+D\"");
    const auto del = main.find ("{ TR_NOOP (\"Delete\"), \"Del\"");
    CHECK (cut != std::string::npos && cut < copy && copy < paste && paste < dup && dup < del);
    // a pasted device goes in front of the selected one (menu and key), at the end with none selected
    CHECK (detail.find ("[this, slot] { pasteDevice (slot); }") != std::string::npos && detail.find ("pasteDevice (slot + 1)") == std::string::npos);
    CHECK (detail.find ("case EditCommand::paste: pasteDevice (effect ? slot : effectCount()); break;") != std::string::npos);
    // the Clip panel's Duplicate button (Live column), the clip menu's Crop Clip, the roll's Randomize footer
    CHECK (detail.find ("put (duplicateLoop, { x0, y, inner, row });") != std::string::npos);
    CHECK (grid.find ("tr (\"Crop Clip\") + sessionkeys::menuKey (sessionkeys::Command::gridCropClip)") != std::string::npos);
    CHECK (roll.find ("liveRandomize.onClick = [this] { liveRandomizeVelocity(); };") != std::string::npos);
    // the map mode tags of the four switches
    for (const char* tag : { "mapmode::tag (blendButton, remotemap::Target::of (RemoteAction::overdub));",
                             "mapmode::tag (punchButton, remotemap::Target::of (RemoteAction::punchIn));",
                             "mapmode::tag (liveLoopButton, remotemap::Target::of (RemoteAction::loopSwitch));",
                             "mapmode::tag (punchOutButton, remotemap::Target::of (RemoteAction::punchOut));" })
        CHECK (main.find (tag) != std::string::npos);
}
