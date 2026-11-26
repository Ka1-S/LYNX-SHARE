// Round 190qi (TOP-NEXT R6): the plugin lists' Recent / Hidden / one row per plugin across formats, the Browser's MIDI
// preview schedule, file types and hidden folder names, the search box's Tab completion and the "a/b" tag tree.

#include "core/BrowserTags.h"
#include "core/BrowserView.h"
#include "core/MidiPreview.h"
#include "core/PluginPicks.h"

TEST (picker190qi_recent_list)
{
    using namespace lynx::pluginpicks;
    std::vector<std::string> r;
    r = pushRecent (r, "a");
    r = pushRecent (r, "b");
    r = pushRecent (r, "a"); // a pick again goes to the top, no repeat
    CHECK ((r == std::vector<std::string> { "a", "b" }));
    for (int i = 0; i < 10; ++i)
        r = pushRecent (r, "k" + std::to_string (i));
    CHECK ((int) r.size() == maxRecent);
    CHECK (r.front() == "k9");
    CHECK (r.back() == "k2");
    CHECK (pushRecent (r, "") == r);        // nothing to note
    CHECK (pushRecent (r, "x\ny") == r);    // never a line break in the file
    // the file: one key per line, newest first; repeats and empty lines skipped, at most 8
    CHECK (parseRecent (formatRecent (r)) == r);
    CHECK ((parseRecent ("a\r\n\nb\na\n") == std::vector<std::string> { "a", "b" }));
    std::string many;
    for (int i = 0; i < 12; ++i)
        many += "p" + std::to_string (i) + "\n";
    CHECK ((int) parseRecent (many).size() == maxRecent);
}

TEST (picker190qi_one_row_per_plugin_across_formats)
{
    using namespace lynx::pluginpicks;
    // the preference: VST3 › CLAP › VST2 (JUCE's "VST") › anything else (round 190rk: VST3 first)
    CHECK (formatRank ("VST3") < formatRank ("CLAP"));
    CHECK (formatRank ("CLAP") < formatRank ("VST"));
    CHECK (formatRank ("vst") == formatRank ("VST2"));
    CHECK (formatRank ("LV2") > formatRank ("VST"));
    // the same plugin: name and vendor without case, spaces or punctuation; a VST2 build's "x64" suffix dropped
    CHECK (groupKey ("Pro-Q 3", "FabFilter", false) == groupKey ("Pro Q3", "fabfilter", false));
    CHECK (groupKey ("Serum x64", "Xfer Records", true) == groupKey ("Serum", "Xfer Records", true));
    CHECK (groupKey ("Serum", "Xfer Records", true) != groupKey ("Serum", "Xfer Records", false)); // (the kind counts)
    CHECK (groupKey ("Pro-Q 3", "FabFilter", false) != groupKey ("Pro-Q 3", "Other", false));

    const std::vector<Item> items {
        { "Pro-Q 3", "FabFilter", "VST", false },      // 0
        { "Pro-Q 3", "FabFilter", "VST3", false },     // 1
        { "Vital", "Vital Audio", "VST3", true },      // 2
        { "Pro-Q 3", "FabFilter", "CLAP", false },     // 3
        { "Surge XT", "Surge Synth Team", "LV2", true }, // 4 (not VST / VST3 / CLAP: its own row)
        { "Surge XT", "Surge Synth Team", "CLAP", true }, // 5
        { "Pro-Q 3", "FabFilter", "VST3", false },     // 6 (a second VST3 of it, e.g. in another folder: its own row)
        { "", "", "VST3", false },                     // 7 (no name: its own row)
    };
    const auto groups = groupByFormat (items);
    CHECK ((int) groups.size() == 6);
    CHECK ((groups[0].members == std::vector<int> { 1, 3, 0 })); // VST3 first, then CLAP, VST2 (round 190rk)
    CHECK (groups[0].preferred() == 1);
    CHECK ((groups[1].members == std::vector<int> { 2 }));
    CHECK ((groups[2].members == std::vector<int> { 4 }));
    CHECK ((groups[3].members == std::vector<int> { 5 }));
    CHECK ((groups[4].members == std::vector<int> { 6 }));
    CHECK ((groups[5].members == std::vector<int> { 7 }));
    // every item in exactly one row
    std::vector<int> seen;
    for (const auto& g : groups)
        seen.insert (seen.end(), g.members.begin(), g.members.end());
    std::sort (seen.begin(), seen.end());
    CHECK ((seen == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7 }));
    CHECK (groupByFormat ({}).empty());
}

TEST (picker190qi_plugin_manager_hidden_filter)
{
    using namespace lynx::prefs;
    std::vector<PluginRow> rows (3);
    rows[0].name = "Alpha";
    rows[1].name = "Beta";
    rows[1].hidden = true;
    rows[2].name = "Gamma";
    rows[2].hidden = true;
    rows[2].status = PluginStatus::failed;
    CHECK ((visiblePlugins (rows, "", PluginFilter::hidden, PluginSort::name) == std::vector<int> { 1, 2 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::all, PluginSort::name) == std::vector<int> { 0, 1, 2 })); // (All still lists them)
    CHECK ((visiblePlugins (rows, "gam", PluginFilter::hidden, PluginSort::name) == std::vector<int> { 2 }));
    // the hidden list is saved in the favourites' format
    CHECK ((parseFavourites (formatFavourites ({ "VST3-a", "CLAP-b" })) == std::set<std::string> { "CLAP-b", "VST3-a" }));
}

TEST (picker190qi_midi_preview_schedule)
{
    using namespace lynx;
    std::vector<Note> notes (3);
    notes[0].start = 960; // one beat of silence first: skipped
    notes[0].length = 480;
    notes[0].key = 60;
    notes[0].velocity = 0.5f;
    notes[1].start = 1920;
    notes[1].length = 960;
    notes[1].key = 64;
    notes[2].start = 1920;
    notes[2].length = 480;
    notes[2].key = 67;
    notes[2].muted = true; // a muted note doesn't play
    const auto events = midipreview::schedule (notes, 120.0);
    // 120 BPM: a beat = 500 ms
    CHECK (events.size() == 4u);
    CHECK ((events[0] == midipreview::Event { 0.0, 60, 0.5f, true }));
    CHECK ((events[1] == midipreview::Event { 250.0, 60, 0.0f, false }));
    CHECK ((events[2] == midipreview::Event { 500.0, 64, 0.78f, true }));
    CHECK ((events[3] == midipreview::Event { 1000.0, 64, 0.0f, false }));
    CHECK (midipreview::lengthMs (events) == 1000.0);
    // at 60 BPM twice as long; capped at maxSeconds (a note sounding then ends there)
    CHECK (midipreview::lengthMs (midipreview::schedule (notes, 60.0)) == 2000.0);
    CHECK (midipreview::lengthMs (midipreview::schedule (notes, 120.0, 0.75)) == 750.0);
    CHECK (midipreview::schedule (notes, 120.0, 0.4).size() == 2u); // (the second note starts after the cap)
    CHECK (midipreview::schedule ({}, 120.0).empty());
    // same time: the off before the on (a repeated key restarts cleanly)
    std::vector<Note> again (2);
    again[0].start = 0;
    again[0].length = 960;
    again[1].start = 960;
    again[1].length = 960;
    const auto rep = midipreview::schedule (again, 120.0);
    CHECK (rep.size() == 4u && ! rep[1].on && rep[2].on && rep[1].ms == rep[2].ms);

    // the player: what is due, and Stop ends what still sounds
    midipreview::Player player (events);
    std::vector<midipreview::Event> sent;
    auto send = [&sent] (const midipreview::Event& e) { sent.push_back (e); };
    player.advance (0.0, send);
    CHECK (sent.size() == 1u && sent[0].on);
    player.advance (600.0, send);
    CHECK (sent.size() == 3u); // the off at 250, the on at 500
    CHECK (! player.done());
    player.stop (send);
    CHECK (sent.size() == 4u && ! sent[3].on && sent[3].key == 64);
    CHECK (player.done());
    player.stop (send);
    CHECK (sent.size() == 4u); // (nothing left sounding)
}

TEST (picker190qi_browser_file_types_and_hidden_folders)
{
    using namespace lynx::browserview;
    // the user's types: none chosen = everything
    CHECK (typeChosen ("kick.wav", {}));
    CHECK (typeChosen ("Kick.WAV", { "wav" }));
    CHECK (! typeChosen ("loop.mid", { "wav", "flac" }));
    // every type the menu offers is one the Browser lists
    for (const auto& g : typeGroups())
        for (const auto& e : g.extensions)
            CHECK (listed ("x." + e, ContentFilter::all));
    // folder names: trimmed, commas, no repeats (ASCII case), no "*" alone, no slashes
    CHECK ((parseFolderNames (" Backup, old* ,, backup, *, a/b ") == std::vector<std::string> { "Backup", "old*", "ab" }));
    CHECK (joinFolderNames ({ "Backup", "old*" }) == "Backup, old*");
    CHECK (folderNameHidden ("backup", { "Backup" }));
    CHECK (folderNameHidden ("Old Stuff", { "old*" }));
    CHECK (folderNameHidden ("drums_bak", { "*_bak" }));
    CHECK (folderNameHidden ("x_y_bak", { "*_*_bak" }));
    CHECK (! folderNameHidden ("Backups", { "Backup" }));
    CHECK (! folderNameHidden ("Drums", {}));
    // a path through a hidden folder below its root (the root's own path is not judged)
    CHECK (pathHidden ("C:\\Samples\\Backup\\kick.wav", "C:\\Samples", { "backup" }));
    CHECK (! pathHidden ("C:\\Backup\\Samples\\kick.wav", "C:\\Backup", { "backup" }));
    CHECK (pathHidden ("/home/u/Samples/old drums/kick.wav", "", { "old*" }));
    CHECK (! pathHidden ("/home/u/Samples/kick.wav", "/home/u/Samples", { "kick*" })); // (the file's own name isn't a folder)
    // saved with the view
    View v;
    v.types = { "wav", "mid" };
    v.hiddenFolderNames = { "Backup", "old*" };
    const auto back = View::fromText (v.toText());
    CHECK (back.types == v.types);
    CHECK (back.hiddenFolderNames == v.hiddenFolderNames);
    CHECK (View::fromText ("type=WAV\nhideFolder=a, b\n").types == std::set<std::string> { "wav" });
    CHECK ((View::fromText ("hideFolder=a, b\n").hiddenFolderNames == std::vector<std::string> { "a", "b" }));
    CHECK (View::fromText ("filter=audio\n").types.empty()); // (an older view: every type)
}

TEST (picker190qi_search_tab_completion)
{
    using namespace lynx::browsertags;
    const std::vector<std::string> tags { "808", "Drums", "Drums/Kick", "Lo-fi keys" };
    const std::vector<std::string> names { "Drum Loops", "Drums", "Vocals" };
    // a "#" word: the shortest tag (equal lengths A-Z), never one with spaces
    CHECK (completeQuery ("#dr", tags, names) == "#Drums");
    CHECK (completeQuery ("#Drums", tags, names) == "#Drums/Kick"); // (then the tree goes on)
    CHECK (completeQuery ("#lo", tags, names).empty());
    CHECK (completeQuery ("#", tags, names) == "#808");
    // other words: a folder name, else a tag; the words before stay
    CHECK (completeQuery ("dr", tags, names) == "Drums");
    CHECK (completeQuery ("kick vo", tags, names) == "kick Vocals");
    CHECK (completeQuery ("80", tags, names) == "808");
    CHECK (completeQuery ("zz", tags, names).empty());
    CHECK (completeQuery ("dr ", tags, names).empty()); // (nothing after the last space: nothing to complete)
    CHECK (completeQuery ("", tags, names).empty());
    CHECK (completeQuery ("Vocals", tags, names).empty()); // (already whole)
}

TEST (picker190qi_tag_tree_and_batch)
{
    using namespace lynx::browsertags;
    // "a / b" is stored as "a/b"
    CHECK (cleanTag (" Drums / Kick ") == "Drums/Kick");
    CHECK (cleanTag ("Drums//Kick/") == "Drums/Kick");
    CHECK (cleanTag ("/") == "");
    CHECK (tagParent ("Drums/Kick") == "Drums");
    CHECK (tagParent ("Drums") == "");
    CHECK (tagLeaf ("Drums/Acoustic/Kick") == "Kick");

    Store s;
    // the batch "Add tag...": every name onto every item
    CHECK (s.add ({ "a.wav", "b.wav", "c.wav" }, "Drums/Kick, 808") == 6);
    CHECK (s.add ({ "d.wav" }, "Drums/Snare") == 1);
    CHECK (s.add ({ "e.wav" }, "Drums") == 1);
    CHECK (s.has ("b.wav", "drums/kick"));
    // picking a parent takes everything under it, a child only its own
    CHECK ((s.filter ({ "Drums" }, Match::any) == std::vector<std::string> { "a.wav", "b.wav", "c.wav", "d.wav", "e.wav" }));
    CHECK ((s.filter ({ "Drums/Kick" }, Match::any) == std::vector<std::string> { "a.wav", "b.wav", "c.wav" }));
    CHECK ((s.filter ({ "Drums", "808" }, Match::all) == std::vector<std::string> { "a.wav", "b.wav", "c.wav" }));
    CHECK (s.filter ({ "Dru" }, Match::any).empty()); // (a parent is a whole name, not a prefix of letters)
    // the tree: groups made only of children's names exist = false
    s.add ({ "f.wav" }, "Synth/Lead/Saw");
    const auto tree = tagTree (s.all());
    CHECK (tree.size() == 3u); // 808, Drums, Synth
    CHECK (tree[0].name == "808" && tree[0].exists && tree[0].items == 3);
    CHECK (tree[1].name == "Drums" && tree[1].exists && tree[1].items == 1);
    CHECK (tree[1].children.size() == 2u);
    CHECK (tree[1].children[0].name == "Kick" && tree[1].children[0].path == "Drums/Kick" && tree[1].children[0].items == 3);
    CHECK (tree[1].children[1].name == "Snare");
    CHECK (tree[2].name == "Synth" && ! tree[2].exists && tree[2].children.size() == 1u);
    CHECK (tree[2].children[0].name == "Lead" && ! tree[2].children[0].exists);
    CHECK (tree[2].children[0].children[0].path == "Synth/Lead/Saw" && tree[2].children[0].children[0].exists);
    // saved and read back the same
    CHECK (Store::fromText (s.toText()) == s);
}
