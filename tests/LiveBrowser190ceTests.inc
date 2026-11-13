// Round 190ce: the Browser's collections (core/BrowserCollections.h), hot-swap (core/HotSwap.h) and the Device View's
// device copies (core/DetailChain.h). docs/analise/REA_rodada190ce.md.

#include "core/BrowserCollections.h"
#include "core/DetailChain.h"
#include "core/HotSwap.h"
#include "core/KeyMap.h"

namespace live190ce
{
inline lynx::EffectSlot fx (lynx::EffectKind kind)
{
    lynx::EffectSlot s;
    s.kind = kind;
    return s;
}
// two channels (a synth on insert 1, a sampler on insert 2) and insert 1 with an EQ and a compressor (off, half mix, row 3)
inline lynx::Song song()
{
    lynx::Song s;
    s.channels.resize (2);
    s.channels[0].name = "Lead";
    s.channels[0].instrument = lynx::InstrumentKind::synth;
    s.channels[0].mixerTrack = 1;
    s.channels[0].volume = 0.5f;
    s.channels[0].pan = -0.25f;
    s.channels[1].name = "Kick";
    s.channels[1].instrument = lynx::InstrumentKind::sampler;
    s.channels[1].samplePath = "/samples/kick.wav";
    s.channels[1].mixerTrack = 2;
    if (s.mixer.size() < 3)
        s.mixer.resize (3);
    s.mixer[1].effects.clear();
    s.mixer[1].effects.push_back (fx (lynx::EffectKind::eq));
    s.mixer[1].effects[0].row = 0;
    s.mixer[1].effects.push_back (fx (lynx::EffectKind::compressor));
    s.mixer[1].effects[1].row = 3;
    s.mixer[1].effects[1].enabled = false;
    s.mixer[1].effects[1].mix = 0.5f;
    s.mixer[1].effects[1].sidechain = 2;
    if (s.patterns.empty())
        s.patterns.emplace_back();
    lynx::Note n;
    n.channel = 0;
    n.key = 60;
    s.patterns[0].notes = { n };
    return s;
}
} // namespace live190ce

// --- collections -------------------------------------------------------------------------------------------------------

TEST (live190ce_collections_start_as_the_one_starred_flag)
{
    using namespace lynx::collections;
    Store s;
    CHECK (count == 7);
    CHECK (s.shown() == std::vector<int> { 0 }); // only Starred is listed at first: the tab looks as it always did
    CHECK (s.name (0) == "Starred" && s.name (1) == "Orange" && s.name (6) == "Gray");
    CHECK (std::string (colourName (0)) == "Red" && std::string (colourName (4)) == "Blue");
    CHECK (s.items.empty() && s.mask ("/a.wav") == 0);
    for (int c = 0; c < count; ++c)
        CHECK ((colourArgb (c) >> 24) == 0xFF);
    CHECK (colourArgb (-1) == 0 && colourArgb (7) == 0 && s.name (7).empty());
}

TEST (live190ce_an_item_carries_several_collections_at_once)
{
    using namespace lynx::collections;
    Store s;
    CHECK (s.set ("/a.wav", 0, true));
    CHECK (! s.set ("/a.wav", 0, true)); // nothing changed
    CHECK (s.toggle ("/a.wav", 4));      // the number key 5: in
    CHECK (s.has ("/a.wav", 0) && s.has ("/a.wav", 4) && ! s.has ("/a.wav", 1));
    CHECK (s.mask ("/a.wav") == 0b10001);
    CHECK (! s.toggle ("/a.wav", 4)); // the key again: out
    CHECK (s.mask ("/a.wav") == 0b00001);
    CHECK (s.toggle ("/b", 2) && s.toggle ("/b", 6));
    CHECK (s.itemsOf (2) == std::vector<std::string> { "/b" } && s.itemsOf (0) == std::vector<std::string> { "/a.wav" });
    CHECK (s.countOf (6) == 1 && s.countOf (5) == 0);
    // key 0: out of all
    CHECK (s.clear ("/b") && ! s.clear ("/b"));
    CHECK (s.items.size() == 1);
    // the last collection taken away: the item is forgotten
    CHECK (s.set ("/a.wav", 0, false) && s.items.empty());
    // nothing odd gets in
    CHECK (! s.set ("", 0, true) && ! s.set ("/x", 7, true) && ! s.set ("/x", -1, true) && ! s.set ("a\nb", 0, true));
    // a Plugin database entry is an item too
    CHECK (s.set (pluginItem ("lynx:fx:Reverb"), 1, true));
    CHECK (isPluginItem (pluginItem ("lynx:fx:Reverb")) && ! isPluginItem ("/plugin:x") && pluginKeyOf (pluginItem ("lynx:fx:Reverb")) == "lynx:fx:Reverb");
}

TEST (live190cy_labelling_with_a_hidden_collection_shows_it)
{
    // Live 12.4.16 (live check 190cw): the key of a hidden colour labels the item and the collection appears
    using namespace lynx::collections;
    Store s;
    CHECK (s.shown() == std::vector<int> { 0 });
    CHECK (s.toggle ("/a.wav", 4)); // key 5: in, and Blue is listed from now on
    CHECK (s.isShown (4) && s.shown() == (std::vector<int> { 0, 4 }));
    CHECK (! s.toggle ("/a.wav", 4)); // out again: the collection stays listed (hiding is the user's own choice)
    CHECK (s.isShown (4));
    s.setShown (4, false);
    CHECK (s.label ("/b", 4, true) && s.isShown (4) && s.has ("/b", 4)); // the item menu's label does the same
    s.setShown (2, false);
    CHECK (! s.label ("/b", 2, false) && ! s.isShown (2)); // taking a label off shows nothing
    CHECK (s.label ("/b", 0, true) && s.isShown (0) && s.shown().size() == 2);
    CHECK (! s.label ("", 3, true) && ! s.isShown (3)); // nothing odd gets in, nothing is shown for it
    // the text round trip keeps it shown
    const auto back = Store::fromText (s.toText());
    CHECK (back.isShown (4) && back.has ("/b", 4));
}

TEST (live190ce_collections_round_trip_through_the_settings_file)
{
    using namespace lynx::collections;
    Store s;
    s.set ("C:\\Samples\\Kick 01.wav", 0, true);
    s.set ("C:\\Samples\\Kick 01.wav", 3, true);
    s.set ("/home/u/Loops", 5, true);
    s.set (pluginItem ("vst3:ABCD"), 6, true);
    s.rename (3, "  Drums | fat\n ");
    s.setShown (3, true);
    s.setShown (0, false);
    CHECK (s.name (3) == "Drums   fat" && s.hasOwnName (3) && ! s.hasOwnName (0));
    const auto text = s.toText();
    const auto back = Store::fromText (text);
    CHECK (back == s);
    CHECK (back.shown() == std::vector<int> { 3 });
    CHECK (back.toText() == text);
    // a damaged file loads what it can
    const auto odd = Store::fromText ("nonsense\nitem|0|/zero\nitem|999|/big\nitem|x|/nan\nitem|5|\nlabel|9|1|Nine\nlabel|2|1|Warm\r\nitem|2|/ok.wav\r\n");
    CHECK (odd.items.size() == 1 && odd.mask ("/ok.wav") == 2);
    CHECK (odd.name (1) == "Warm" && odd.isShown (1) && odd.isShown (0));
    // an empty name goes back to the default
    Store r = back;
    r.rename (3, "   ");
    CHECK (! r.hasOwnName (3) && r.name (3) == "Green");
}

TEST (live190ce_the_old_starred_file_becomes_collection_one)
{
    using namespace lynx::collections;
    const auto s = Store::fromStarred ({ "/a/kick.wav", "/a/Loops", "/a/kick.wav" });
    CHECK (s.items.size() == 2 && s.has ("/a/kick.wav", 0) && s.has ("/a/Loops", 0));
    CHECK (s.shown() == std::vector<int> { 0 });
    // what an older LYNX still reads: the starred paths, without Plugin database entries
    auto t = s;
    t.set (pluginItem ("lynx:gen:Synth"), 0, true);
    t.set ("/b/only-blue.wav", 4, true);
    CHECK (t.starredPaths() == (std::vector<std::string> { "/a/Loops", "/a/kick.wav" }));
}

TEST (live190ce_labels_follow_a_moved_file_or_folder)
{
    using namespace lynx::collections;
    Store s;
    s.set ("/lib/Drums", 1, true);
    s.set ("/lib/Drums/kick.wav", 0, true);
    s.set ("/lib/Drums2/other.wav", 0, true); // (not below /lib/Drums)
    s.set ("/lib/new/kick.wav", 2, true);
    CHECK (s.moved ("/lib/Drums", "/lib/new") == 2);
    CHECK (s.has ("/lib/new", 1) && s.mask ("/lib/new/kick.wav") == 0b101 && s.has ("/lib/Drums2/other.wav", 0));
    CHECK (s.mask ("/lib/Drums") == 0 && s.mask ("/lib/Drums/kick.wav") == 0);
    CHECK (s.moved ("/lib/new", "/lib/new") == 0 && s.moved ("", "/x") == 0);
}

TEST (live190ce_search_finds_items_by_the_name_of_their_collection)
{
    using namespace lynx::collections;
    Store s;
    s.set ("/s/Kick Hard.wav", 0, true);
    s.set ("/s/Snare.wav", 0, true);
    s.set ("/s/Kick Soft.wav", 3, true);
    s.set ("/s/Pad.wav", 3, true);
    s.set (pluginItem ("lynx:fx:Reverb"), 3, true);
    s.rename (3, "Favourite Textures");
    // a collection's name alone: everything in it
    CHECK (s.search ("starred") == (std::vector<std::string> { "/s/Kick Hard.wav", "/s/Snare.wav" }));
    CHECK (s.search ("TEXTURES").size() == 3);
    // a collection's name and a word of the item's own name: both must match
    CHECK (s.search ("starred kick") == std::vector<std::string> { "/s/Kick Hard.wav" });
    CHECK (s.search ("textures kick") == std::vector<std::string> { "/s/Kick Soft.wav" });
    // words that only match the item's own name are the file search's business
    CHECK (s.search ("kick").empty() && s.search ("").empty() && s.search ("   ").empty());
    CHECK (s.search ("starred pad").empty());
    // the names as the user sees them (a translated default)
    std::array<std::string, count> names;
    for (int c = 0; c < count; ++c)
        names[(std::size_t) c] = s.name (c);
    names[0] = "Favoritos";
    CHECK (s.search ("favoritos", &names).size() == 2 && s.search ("starred", &names).empty());
}

// --- hot-swap ----------------------------------------------------------------------------------------------------------

TEST (live190ce_hot_swap_replaces_the_device_in_place)
{
    using namespace lynx;
    auto s = live190ce::song();
    const auto eqInstance = s.mixer[1].effects[0].instance.id;
    const auto target = hotswap::targetOfEffect (s, 1, 1);
    CHECK (target.active() && target.kind == hotswap::Kind::effect && hotswap::exists (s, target));
    CHECK (! hotswap::targetOfEffect (s, 1, 2).active() && ! hotswap::targetOfEffect (s, 9, 0).active());

    auto fresh = live190ce::fx (EffectKind::reverb);
    const auto freshInstance = fresh.instance.id;
    const auto now = hotswap::swapEffect (s, target, fresh);
    CHECK (now.active() && now.instance == freshInstance && now.instance != target.instance);
    const auto& slot = s.mixer[1].effects[1];
    CHECK (slot.kind == EffectKind::reverb);
    CHECK (slot.row == 3 && slot.mix == 0.5f); // its place and Mix stay
    CHECK (slot.enabled); // round 190cy: it comes in with its own on / off state (Live 12.4.16), not the old one's (off)
    CHECK (s.mixer[1].effects.size() == 2 && s.mixer[1].effects[0].kind == EffectKind::eq && s.mixer[1].effects[0].instance.id == eqInstance);
    CHECK (! hotswap::exists (s, target) && hotswap::exists (s, now));
    // the old link is dead: nothing is replaced through it
    CHECK (! hotswap::swapEffect (s, target, live190ce::fx (EffectKind::delay)).active());
    CHECK (s.mixer[1].effects[1].kind == EffectKind::reverb);
    // a sidechain source stays only where the new device takes one
    auto s2 = live190ce::song();
    const auto t2 = hotswap::targetOfEffect (s2, 1, 1);
    const auto keeps = mixerops::acceptsSidechain (EffectKind::reverb);
    hotswap::swapEffect (s2, t2, live190ce::fx (EffectKind::reverb));
    CHECK (s2.mixer[1].effects[1].sidechain == (keeps ? 2 : -1));
    auto s3 = live190ce::song();
    hotswap::swapEffect (s3, hotswap::targetOfEffect (s3, 1, 1), live190ce::fx (EffectKind::compressor));
    CHECK (s3.mixer[1].effects[1].sidechain == 2);
}

TEST (live190ce_hot_swap_follows_the_device_through_the_chain)
{
    using namespace lynx;
    auto s = live190ce::song();
    const auto target = hotswap::targetOfEffect (s, 1, 1); // the compressor
    CHECK (mixerops::moveEffectSlot (s, 1, 1, 0));         // dragged before the EQ
    const auto at = hotswap::findEffect (s, target.instance);
    CHECK (at.track == 1 && at.slot == 0 && s.mixer[1].effects[0].kind == EffectKind::compressor);
    const auto now = hotswap::swapEffect (s, target, live190ce::fx (EffectKind::delay));
    CHECK (now.active() && s.mixer[1].effects[0].kind == EffectKind::delay && s.mixer[1].effects[1].kind == EffectKind::eq);
    // deleted: the link is gone
    s.mixer[1].effects.erase (s.mixer[1].effects.begin());
    CHECK (! hotswap::exists (s, now) && ! hotswap::findEffect (s, now.instance).found());
    CHECK (! hotswap::exists (s, {}) && ! hotswap::findEffect (s, 0).found() && hotswap::findChannel (s, 0) < 0);
}

TEST (live190ce_hot_swap_preset_changes_only_the_settings)
{
    using namespace lynx;
    auto s = live190ce::song();
    const auto target = hotswap::targetOfEffect (s, 1, 0); // the EQ
    EffectParams params {};
    params[0] = 0.25f;
    params[1] = 0.75f;
    const std::vector<std::uint8_t> state { 1, 2, 3 };
    CHECK (! hotswap::swapEffectPreset (s, target, EffectKind::reverb, params, nullptr, "Hall")); // another effect's preset
    CHECK (s.mixer[1].effects[0].presetName.empty());
    CHECK (hotswap::swapEffectPreset (s, target, EffectKind::eq, params, &state, "Bright"));
    const auto& slot = s.mixer[1].effects[0];
    CHECK (slot.kind == EffectKind::eq && slot.params[0] == 0.25f && slot.params[1] == 0.75f && slot.state == state && slot.presetName == "Bright");
    CHECK (slot.instance.id == target.instance && hotswap::exists (s, target)); // the device, and so the link, stay
    CHECK (! hotswap::swapEffectPreset (s, hotswap::targetOfChannel (s, 0), EffectKind::eq, params, nullptr, "x"));
    // round 190cy: a preset swapped onto a device that was off comes in on, as the preset loads it (Live 12.4.16: an
    // Utility that was off, swapped for "Wide Stereo", came in on); a plugin's own preset file the same
    const auto off = hotswap::targetOfEffect (s, 1, 1); // the compressor, off
    CHECK (! s.mixer[1].effects[1].enabled);
    CHECK (hotswap::swapEffectPreset (s, off, EffectKind::compressor, params, nullptr, "Glue"));
    CHECK (s.mixer[1].effects[1].enabled && s.mixer[1].effects[1].mix == 0.5f && s.mixer[1].effects[1].row == 3);
    s.mixer[1].effects[1].enabled = false;
    CHECK (hotswap::presetLoadedInto (s, off) && s.mixer[1].effects[1].enabled);
    CHECK (! hotswap::presetLoadedInto (s, hotswap::targetOfChannel (s, 0)));
    // a fresh device that is itself off (a preset saved off) stays off
    auto offFresh = live190ce::fx (EffectKind::delay);
    offFresh.enabled = false;
    const auto now = hotswap::swapEffect (s, off, offFresh);
    CHECK (now.active() && ! s.mixer[1].effects[1].enabled);
}

TEST (live190ce_hot_swap_of_a_channel_keeps_its_place_notes_and_levels)
{
    using namespace lynx;
    auto s = live190ce::song();
    const auto target = hotswap::targetOfChannel (s, 0);
    CHECK (target.kind == hotswap::Kind::channel && hotswap::findChannel (s, target.instance) == 0);
    ChannelDef fresh;
    fresh.name = "Bass";
    fresh.instrument = InstrumentKind::sampler;
    fresh.samplePath = "/samples/bass.wav";
    const auto now = hotswap::swapInstrument (s, target, fresh);
    CHECK (now.active() && hotswap::findChannel (s, now.instance) == 0);
    const auto& c = s.channels[0];
    CHECK (c.name == "Bass" && c.instrument == InstrumentKind::sampler && c.samplePath == "/samples/bass.wav");
    CHECK (c.mixerTrack == 1 && c.volume == 0.5f && c.pan == -0.25f);
    CHECK (s.channels.size() == 2 && s.channels[1].name == "Kick");
    CHECK (s.patterns[0].notes.size() == 1 && s.patterns[0].notes[0].channel == 0);
    CHECK (! hotswap::swapInstrument (s, hotswap::targetOfEffect (s, 1, 0), fresh).active()); // an effect link takes no instrument

    // a sample into a channel that plays one; not into a synth
    const auto kick = hotswap::targetOfChannel (s, 1);
    CHECK (hotswap::playsSample (s.channels[1]));
    CHECK (hotswap::swapSample (s, kick, "/samples/snare.wav", "snare"));
    CHECK (s.channels[1].samplePath == "/samples/snare.wav" && s.channels[1].instance.id == kick.instance && s.channels[1].mixerTrack == 2);
    auto synth = live190ce::song();
    CHECK (! hotswap::playsSample (synth.channels[0]));
    CHECK (! hotswap::swapSample (synth, hotswap::targetOfChannel (synth, 0), "/samples/snare.wav", "snare"));
    CHECK (synth.channels[0].instrument == InstrumentKind::synth);
}

TEST (live190ce_hot_swap_candidates_by_file_type)
{
    using namespace lynx;
    using hotswap::Candidate;
    const auto s = live190ce::song();
    const auto effect = hotswap::targetOfEffect (s, 1, 0), synth = hotswap::targetOfChannel (s, 0), sampler = hotswap::targetOfChannel (s, 1);
    CHECK (hotswap::candidateFor (s, effect, "Big Hall.FST") == Candidate::effectPreset);
    CHECK (hotswap::candidateFor (s, effect, "/p/My EQ.lynxpreset") == Candidate::effectPreset);
    CHECK (hotswap::candidateFor (s, effect, "kick.wav") == Candidate::none);
    CHECK (hotswap::candidateFor (s, synth, "Lead.fst") == Candidate::synthPreset && hotswap::candidateFor (s, synth, "x.gmsynth") == Candidate::synthPreset);
    CHECK (hotswap::candidateFor (s, synth, "kick.wav") == Candidate::none); // a synth takes no sample
    CHECK (hotswap::candidateFor (s, sampler, "C:\\s\\Kick.WAV") == Candidate::sample && hotswap::candidateFor (s, sampler, "a.flac") == Candidate::sample);
    CHECK (hotswap::candidateFor (s, sampler, "song.lynx") == Candidate::none && hotswap::candidateFor (s, sampler, "noext") == Candidate::none);
    CHECK (hotswap::candidateFor (s, sampler, "/folder.wav/noext") == Candidate::none);
    CHECK (hotswap::candidateFor (s, {}, "kick.wav") == Candidate::none);
    CHECK (hotswap::candidateFor (s, { hotswap::Kind::effect, 123456789 }, "a.fst") == Candidate::none); // the device is gone
}

TEST (live190ce_hot_swap_session_restores_the_browser_and_follows_undo)
{
    using namespace lynx;
    auto s = live190ce::song();
    hotswap::Session session;
    CHECK (! session.active() && session.stillValid (s));
    const auto first = hotswap::targetOfEffect (s, 1, 1);
    session.begin (first, 2, "kick");
    CHECK (session.active() && session.target == first && session.swaps == 0);

    // two swaps, each an undo step of the song (the engine keeps a copy of the song per step)
    const Song before1 = s;
    const auto second = hotswap::swapEffect (s, session.target, live190ce::fx (EffectKind::reverb));
    session.retarget (second);
    const Song before2 = s;
    const auto third = hotswap::swapEffect (s, session.target, live190ce::fx (EffectKind::delay));
    session.retarget (third);
    CHECK (session.swaps == 2 && session.target == third && session.earlier.size() == 2);
    CHECK (session.stillValid (s) && session.target == third);

    // Undo: the reverb is back with its instance, and the link moves to it
    s = before2;
    CHECK (s.mixer[1].effects[1].kind == EffectKind::reverb);
    CHECK (session.stillValid (s) && session.target == second && session.earlier.size() == 1);
    // Undo again: the original compressor, exactly as it was (off, half mix, row 3, its sidechain)
    s = before1;
    CHECK (session.stillValid (s) && session.target == first && session.earlier.empty());
    const auto& slot = s.mixer[1].effects[1];
    CHECK (slot.kind == EffectKind::compressor && ! slot.enabled && slot.mix == 0.5f && slot.row == 3 && slot.sidechain == 2);
    // a swap from here goes on from the restored device
    const auto again = hotswap::swapEffect (s, session.target, live190ce::fx (EffectKind::eq));
    session.retarget (again);
    CHECK (session.target == again && s.mixer[1].effects[1].kind == EffectKind::eq);

    // linking another device while linked keeps what the Browser showed before the first link
    session.begin (hotswap::targetOfChannel (s, 1), 0, "");
    CHECK (session.previousTab == 2 && session.previousSearch == "kick" && session.earlier.empty() && session.swaps == 0);
    session.loaded();
    CHECK (session.swaps == 1);
    // the device is deleted: the mode has to end
    s.channels.pop_back();
    CHECK (! session.stillValid (s));
    const auto back = session.end();
    CHECK (back.tab == 2 && back.search == "kick" && ! session.active() && session.swaps == 0);
    CHECK (session.stillValid (s)); // (nothing linked: nothing to end)
    // a dead retarget changes nothing
    session.begin (first, 1, "");
    session.retarget ({});
    CHECK (session.target == first && session.swaps == 0);
}

// --- the Device View's copies --------------------------------------------------------------------------------------------

TEST (live190ce_duplicate_and_paste_make_new_devices)
{
    using namespace lynx;
    auto s = live190ce::song();
    const auto original = s.mixer[1].effects[1].instance.id;
    // Duplicate: right after the device, the same settings, a new running instance
    const auto copy = detailchain::duplicateEffect (s, 1, 1);
    CHECK (copy == 2 && s.mixer[1].effects.size() == 3);
    CHECK (s.mixer[1].effects[2].kind == EffectKind::compressor && s.mixer[1].effects[2].mix == 0.5f && ! s.mixer[1].effects[2].enabled);
    CHECK (s.mixer[1].effects[2].instance.id != original && s.mixer[1].effects[1].instance.id == original);
    CHECK (s.mixer[1].effects[2].sidechain == 2);
    {
        const auto rows = mixerops::effectRows (s.mixer[1].effects); // every device in a row of its own, in chain order
        CHECK (rows.size() == 3 && rows[0] < rows[1] && rows[1] < rows[2] && rows[2] < maxEffectSlots);
    }
    // Ctrl + drag: the copy where it is dropped (before the first device)
    const auto dragged = detailchain::duplicateEffect (s, 1, 0, 0);
    CHECK (dragged == 0 && s.mixer[1].effects.size() == 4 && s.mixer[1].effects[0].kind == EffectKind::eq && s.mixer[1].effects[1].kind == EffectKind::eq);
    CHECK (s.mixer[1].effects[0].instance.id != s.mixer[1].effects[1].instance.id);
    // Paste into another insert: at the end
    const auto clip = s.mixer[1].effects[2];
    const auto pasted = detailchain::pasteEffect (s, 2, clip, 0);
    CHECK (pasted == 0 && s.mixer[2].effects.size() == 1 && s.mixer[2].effects[0].kind == clip.kind && s.mixer[2].effects[0].instance.id != clip.instance.id);
    // no room, no such insert or device: nothing
    while ((int) s.mixer[2].effects.size() < maxEffectSlots)
        CHECK (detailchain::pasteEffect (s, 2, clip, (int) s.mixer[2].effects.size()) >= 0);
    CHECK (detailchain::pasteEffect (s, 2, clip, 0) == -1 && detailchain::duplicateEffect (s, 2, 0) == -1);
    CHECK ((int) s.mixer[2].effects.size() == maxEffectSlots);
    CHECK (detailchain::duplicateEffect (s, 1, 9) == -1 && detailchain::duplicateEffect (s, 99, 0) == -1 && detailchain::pasteEffect (s, -1, clip, 0) == -1);
}

// --- the keys --------------------------------------------------------------------------------------------------------------

TEST (live190ce_collection_and_hot_swap_keys)
{
    using namespace lynx::keys;
    for (int c = 1; c <= 7; ++c)
    {
        const auto* a = findAction ("browser.collection" + std::to_string (c));
        CHECK (a != nullptr);
        if (a != nullptr)
        {
            CHECK (a->context == Context::browser && a->defaults.size() == 1 && ! a->whilePiano); // (the typing keyboard keeps the digits)
            if (a->defaults.size() == 1)
                CHECK (a->defaults[0].key == '0' + c && a->defaults[0].mods == 0);
        }
    }
    const auto* clear = findAction ("browser.clearCollections");
    CHECK (clear != nullptr && clear->context == Context::browser && clear->defaults.size() == 1 && clear->defaults[0].key == '0');
    const auto* swap = findAction ("view.hotSwap");
    CHECK (swap != nullptr && swap->context == Context::global && swap->defaults.size() == 1 && swap->defaults[0].key == 'Q' && swap->defaults[0].mods == 0 && ! swap->whilePiano);
    // the digits are the Browser's only while it has the focus, and nothing else clashes with the new keys
    KeyMap map;
    CHECK (map.find ({ '0' }, Context::browser, false) == "browser.clearCollections");
    CHECK (map.find ({ '0' }, Context::playlist, false).empty() && map.find ({ '3' }, Context::mixer, false).empty());
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ '3' }, Context::browser, false) == "browser.collection3");
    CHECK (map.find ({ 'Q' }, Context::mixer, false) == "view.hotSwap");
    CHECK (map.find ({ 'Q' }, Context::mixer, true).empty() && map.find ({ '3' }, Context::browser, true).empty()); // typing keyboard on
}
