// Round 190iz: the Browser's tags (core/BrowserTags.h), FL's six tabs and Live's sidebar (core/BrowserPlaces.h), FL's
// "Send to selected channel or focused plugin" and FL's 5-second preview (core/BrowserSend.h).
// docs/analise/REA_rodada190iz.md.

#include "core/BrowserPlaces.h"
#include "core/BrowserSend.h"
#include "core/BrowserTags.h"

TEST (browser190iz_add_a_tag_splits_commas_as_fl)
{
    using namespace lynx::browsertags;
    // FL manual: "typing - Kick,808,C# will give the content 3 tags"
    const auto tags = splitTags ("Kick,808,C#");
    CHECK ((tags == std::vector<std::string> { "Kick", "808", "C#" }));
    // spaces trimmed, empty parts and repeats (case ignored) dropped, the format's own characters removed
    CHECK ((splitTags (" Kick , ,kick,Dark|Pad , ") == std::vector<std::string> { "Kick", "DarkPad" }));
    CHECK (splitTags (",,  ,").empty());

    Store s;
    CHECK (s.add ({ "/s/kick.wav", "/s/kick2.wav" }, "Kick,808,C#") == 6);
    CHECK (s.has ("/s/kick.wav", "kick")); // (case ignored)
    CHECK (s.has ("/s/kick2.wav", "C#"));
    CHECK ((s.tagsOf ("/s/kick.wav") == std::vector<std::string> { "808", "C#", "Kick" }));
    // adding again changes nothing; the first spelling stays
    CHECK (s.add ({ "/s/kick.wav" }, "KICK") == 0);
    CHECK (s.nameOf ("kick") == "Kick");
}

TEST (browser190iz_tags_toggle_rename_delete)
{
    using namespace lynx::browsertags;
    Store s;
    s.add ({ "/a.wav" }, "Dark,Pad");
    s.add ({ "/b.wav" }, "Pad");
    // Tags ▸ ticks a tag off
    CHECK (s.set ("/a.wav", "Dark", false));
    CHECK (! s.has ("/a.wav", "Dark"));
    CHECK (! s.set ("/a.wav", "Dark", false));
    // a tag stays in the list with no item left (FL's tag list); its count is 0
    CHECK ((s.all() == std::vector<TagCount> { { "Dark", 0 }, { "Pad", 2 } }));
    // Edit tags: rename everywhere, and into an existing tag the two become one
    CHECK (s.rename ("Pad", "Pads"));
    CHECK (s.has ("/a.wav", "Pads") && s.has ("/b.wav", "pads"));
    s.add ({ "/c.wav" }, "Keys");
    CHECK (s.rename ("Keys", "pads"));
    CHECK (s.has ("/c.wav", "Pads"));
    CHECK ((s.all() == std::vector<TagCount> { { "Dark", 0 }, { "Pads", 3 } })); // (merged: the existing tag's name stays)
    CHECK (! s.rename ("pads", "Pads"));
    CHECK (s.rename ("pads", "PADS")); // only the case
    CHECK (s.all().back().name == "PADS");
    CHECK (! s.rename ("nothing", "x"));
    CHECK (! s.rename ("Pads", " , "));
    // delete: from the list and every item; items with no tag left go
    CHECK (s.remove ("Pads") == 3);
    CHECK (s.items.empty());
    CHECK ((s.all() == std::vector<TagCount> { { "Dark", 0 } }));
    // only real items and names
    CHECK (! s.set ("", "x", true));
    CHECK (! s.set ("/a.wav", " ", true));
    CHECK (! s.set ("/a\nb.wav", "x", true));
}

TEST (browser190iz_tag_filter_any_all)
{
    using namespace lynx::browsertags;
    Store s;
    s.add ({ "/kick.wav" }, "Drum,808");
    s.add ({ "/snare.wav" }, "Drum");
    s.add ({ "/bass.wav" }, "808,Bass");
    // FL: "Will show content with 'Any' of the selected tags, or only that with 'All' the selected tags"
    CHECK ((s.filter ({ "Drum", "808" }, Match::any) == std::vector<std::string> { "/bass.wav", "/kick.wav", "/snare.wav" }));
    CHECK ((s.filter ({ "Drum", "808" }, Match::all) == std::vector<std::string> { "/kick.wav" }));
    CHECK ((s.filter ({ "bass" }, Match::all) == std::vector<std::string> { "/bass.wav" }));
    CHECK (s.filter ({}, Match::any).empty());
    CHECK (s.matches ("/anything.wav", {}, Match::all)); // nothing picked: everything passes
    CHECK (! s.matches ("/untagged.wav", { "Drum" }, Match::any));
}

TEST (browser190iz_tags_move_with_files_and_round_trip)
{
    using namespace lynx::browsertags;
    Store s;
    s.add ({ "/lib/Drums", "/lib/Drums/kick.wav", "/lib/Drumsy.wav", "plugin:lynx:gen:Synth" }, "Mine");
    s.add ({ "/lib/Drums/kick.wav" }, "Kick");
    s.create ("Unused");
    // a folder renamed: it and everything below keep their tags; a sibling that only starts the same does not move
    CHECK (s.moved ("/lib/Drums", "/lib/Perc") == 2);
    CHECK (s.has ("/lib/Perc/kick.wav", "Kick") && s.has ("/lib/Perc", "Mine"));
    CHECK (s.has ("/lib/Drumsy.wav", "Mine"));
    CHECK (s.moved ("/x", "/x") == 0);

    const auto text = s.toText();
    CHECK (text.rfind ("lynx-browser-tags 1\n", 0) == 0);
    const auto back = Store::fromText (text);
    CHECK (back == s);
    CHECK (back.exists ("unused"));
    // a path with a '|' (Linux) survives; junk lines are skipped
    Store odd;
    odd.set ("/a|b.wav", "T", true);
    CHECK (Store::fromText (odd.toText() + "junk\nitem|\nitem|x|\n") == odd);
}

TEST (browser190iz_hash_tag_query)
{
    using namespace lynx::browsertags;
    const auto q = parseQuery ("#808 kick #Dark  hard");
    CHECK ((q.tags == std::vector<std::string> { "808", "Dark" }));
    CHECK (q.rest == "kick hard");
    CHECK (parseQuery ("#").tags.empty()); // a lone '#' is a word
    CHECK (parseQuery ("#").rest == "#");
    CHECK (nameHasWords ("Kick Hard 01.wav", "hard KICK"));
    CHECK (! nameHasWords ("Kick.wav", "snare"));
    CHECK (nameHasWords ("x", ""));
}

TEST (browser190iz_fl_six_tabs_in_fl_order)
{
    using namespace lynx::browserplaces;
    using lynx::browserview::RootKind;
    // FL 2026: ALL, PROJECT, PLUGINS, PRESETS, SOUNDS, STARRED; the ids 0..2 stay what they were
    CHECK ((int) Tab::all == 0 && (int) Tab::project == 1 && (int) Tab::starred == 2);
    const char* names[tabCount] = { "ALL", "PROJECT", "PLUGINS", "PRESETS", "SOUNDS", "STARRED" };
    for (int i = 0; i < tabCount; ++i)
    {
        CHECK (std::string (tabName (tabOrder[(std::size_t) i])) == names[i]);
        CHECK (slotOf (tabOrder[(std::size_t) i]) == i);
    }
    CHECK (tabShowsRoot (Tab::presets, RootKind::flPresets) && tabShowsRoot (Tab::presets, RootKind::pluginPresets));
    CHECK (! tabShowsRoot (Tab::presets, RootKind::flSamples));
    CHECK (tabShowsRoot (Tab::sounds, RootKind::flSamples) && tabShowsRoot (Tab::sounds, RootKind::user));
    CHECK (! tabShowsRoot (Tab::sounds, RootKind::flPresets) && ! tabShowsRoot (Tab::sounds, RootKind::flDemos));
    CHECK (! tabShowsRoot (Tab::plugins, RootKind::user) && ! tabShowsRoot (Tab::project, RootKind::user));
    CHECK (tabShowsRoot (Tab::all, RootKind::flDemos));
    CHECK (tabFilter (Tab::sounds, lynx::browserview::ContentFilter::all) == lynx::browserview::ContentFilter::audio);
    CHECK (tabFilter (Tab::presets, lynx::browserview::ContentFilter::audio) == lynx::browserview::ContentFilter::presets);
    CHECK (tabFilter (Tab::all, lynx::browserview::ContentFilter::samples) == lynx::browserview::ContentFilter::samples);
}

TEST (browser190iz_live_sidebar_groups)
{
    using namespace lynx::browserplaces;
    using lynx::browserview::RootKind;
    const auto rows = sidebar ({ { 0, "Favorites" }, { 4, "Blue" } },
                               { { "FL samples (Packs)", "/fl/Packs", RootKind::flSamples },
                                 { "Scores", "/fl/Scores", RootKind::flMidi },
                                 { "Music", "/home/u/Music", RootKind::music },
                                 { "My kits", "/data/kits", RootKind::user } });
    // Collections first, then Live's 14 Library entries, then Places
    CHECK (rows.size() == 2 + 14 + 6);
    CHECK (rows[0].group == Group::collections && rows[0].collection == 0 && rows[0].label == "Favorites");
    CHECK (rows[1].collection == 4);
    CHECK (rows[2].group == Group::library && rows[2].label == "All");
    CHECK (rows[2 + 7].label == "Max for Live" && ! rows[2 + 7].available);
    CHECK (rows[2 + 11].label == "Grooves" && ! rows[2 + 11].available);
    CHECK (rows[2 + 13].label == "Templates" && rows[2 + 13].available);
    const auto* places = &rows[16];
    CHECK (places[0].category == Category::packs);
    CHECK (places[1].category == Category::userLibrary);
    CHECK (places[2].category == Category::currentProject && places[2].label == "Current Project");
    CHECK (places[3].category == Category::folder && places[3].label == "Music" && places[3].path == "/home/u/Music");
    CHECK (places[4].label == "My kits");
    CHECK (places[5].category == Category::addFolder);
    // no FL packs: no Packs place
    CHECK (sidebar ({}, {}).front().category == Category::all);
    CHECK (sidebar ({}, {})[14].category == Category::userLibrary);
}

TEST (browser190iz_live_categories_list_the_right_places)
{
    using namespace lynx::browserplaces;
    using lynx::browserview::ContentFilter;
    using lynx::browserview::RootKind;
    CHECK (categoryShowsRoot (Category::samples, RootKind::flSamples) && categoryShowsRoot (Category::samples, RootKind::user));
    CHECK (! categoryShowsRoot (Category::samples, RootKind::flPresets));
    CHECK (categoryShowsRoot (Category::sounds, RootKind::pluginPresets) && ! categoryShowsRoot (Category::sounds, RootKind::music));
    CHECK (categoryShowsRoot (Category::clips, RootKind::flMidi) && ! categoryShowsRoot (Category::clips, RootKind::flSamples));
    CHECK (categoryShowsRoot (Category::packs, RootKind::flSamples) && ! categoryShowsRoot (Category::packs, RootKind::user));
    CHECK (categoryFilter (Category::samples, ContentFilter::all) == ContentFilter::audio);
    CHECK (categoryFilter (Category::clips, ContentFilter::all) == ContentFilter::projects);
    CHECK (categoryFilter (Category::packs, ContentFilter::samples) == ContentFilter::samples);
    CHECK (wantsPlugins (Category::instruments) == 1 && wantsPlugins (Category::audioEffects) == 0 && wantsPlugins (Category::samples) == -1);
    CHECK (keepsPlugin (Category::instruments, true, "LYNX", "Synth") && ! keepsPlugin (Category::instruments, true, "VFX", "VFX Arp"));
    CHECK (keepsPlugin (Category::midiEffects, true, "VFX", "VFX Arp") && ! keepsPlugin (Category::midiEffects, false, "", "EQ"));
    CHECK (keepsPlugin (Category::audioEffects, false, "LYNX", "Facet EQ") && ! keepsPlugin (Category::audioEffects, true, "LYNX", "Synth"));
    CHECK (keepsPlugin (Category::plugIns, false, "", "Vital") && ! keepsPlugin (Category::plugIns, false, "LYNX", "Facet EQ"));
    CHECK (keepsPlugin (Category::modulators, false, "FL engines", "Fruity Peak Controller"));
    CHECK (! keepsPlugin (Category::modulators, false, "FL engines", "Fruity Delay"));
    CHECK (wantsProject (Category::currentProject) && ! wantsProject (Category::samples));
}

TEST (browser190iz_send_to_selected_channel_or_focused_plugin)
{
    using namespace lynx::browsersend;
    CHECK (decide (Item::sample, Channel::playsSample, false) == Action::sampleIntoSelected);
    CHECK (decide (Item::sample, Channel::plugin, true) == Action::sampleNewChannel); // (as Enter: a new Sampler channel)
    CHECK (decide (Item::sample, Channel::none, false) == Action::sampleNewChannel);
    CHECK (decide (Item::soundFont, Channel::soundFont, false) == Action::fontIntoSelected);
    CHECK (decide (Item::soundFont, Channel::playsSample, false) == Action::fontNewChannel);
    // a plugin preset goes to the focused plugin window, else to the selected channel's plugin
    CHECK (decide (Item::pluginPreset, Channel::other, true) == Action::presetToFocusedPlugin);
    CHECK (decide (Item::pluginPreset, Channel::plugin, false) == Action::presetToFocusedPlugin);
    CHECK (decide (Item::pluginPreset, Channel::playsSample, false) == Action::none);
    CHECK (decide (Item::generatorPreset, Channel::none, false) == Action::generatorPresetChannel);
    // FL's Enter on a generator: it replaces the selected channel
    CHECK (decide (Item::generator, Channel::playsSample, false) == Action::replaceSelected);
    CHECK (decide (Item::generator, Channel::none, false) == Action::newChannel);
    CHECK (decide (Item::effect, Channel::none, true) == Action::effectToInsert);
    CHECK (decide (Item::other, Channel::playsSample, true) == Action::none);
}

TEST (browser190iz_preview_plays_five_seconds_unless_full)
{
    using namespace lynx::browserpreview;
    // a 12 s file at 48 kHz: 5 s from the start, capped (faded)
    auto r = range (12 * 48000, 48000.0, 0.0, false);
    CHECK (r.start == 0 && r.end == 5 * 48000 && r.capped);
    // full: to the end
    r = range (12 * 48000, 48000.0, 0.0, true);
    CHECK (r.end == 12 * 48000 && ! r.capped);
    // a 3 s one-shot plays whole
    r = range (3 * 44100, 44100.0, 0.0, false);
    CHECK (r.end == 3 * 44100 && ! r.capped);
    // Shift+Click at the middle: 5 s from there; near the end: to the end
    r = range (12 * 48000, 48000.0, 0.5, false);
    CHECK (r.start == 6 * 48000 && r.end == 11 * 48000 && r.capped);
    r = range (12 * 48000, 48000.0, 0.9, false);
    CHECK (r.end == 12 * 48000 && ! r.capped);
    // out-of-range fractions and empty files
    CHECK (range (100, 48000.0, 2.0, false).start == 99);
    CHECK (range (100, 48000.0, -1.0, false).start == 0);
    CHECK (range (0, 48000.0, 0.0, false).end == 0);
    // the fade-out: 10 ms
    CHECK (fadeSamples (48000.0) == 480);
    CHECK (fadeGain (1000.0, 480) == 1.0f);
    CHECK (fadeGain (240.0, 480) == 0.5f);
    CHECK (fadeGain (-1.0, 480) == 0.0f);
}
