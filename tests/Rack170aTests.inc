// Round 170a (Busy Works Beats' video): Channel Rack Cut / Copy / Paste, menu accelerator letters, the pattern
// selector's drag and wheel, every key named in the Piano roll (docs/analise/REA_rodada170a.md)

#include "core/MenuMnemonic.h"
#include "core/NoteNames.h"
#include "core/TransportEdit.h"

TEST (rack170a_cut_copy_paste_selected_channels)
{
    using namespace lynx;
    Pattern p;
    auto add = [&p] (int channel, std::int64_t start, int key = 60) {
        Note n;
        n.channel = channel;
        n.start = start;
        n.key = key;
        p.notes.push_back (n);
    };
    add (0, 0); add (1, 96); add (1, 192, 62); add (2, 48); add (3, 0);
    p.channelLoops.push_back ({ 1, ChannelLoop::nextBeat });
    p.sortNotes();

    // FL (REA §1): the clipboard runs from the first selected channel; channel 2 was not selected: a gap
    const auto clip = copyChannelSteps (p, { 3, 1 }, 5);
    CHECK (clip.slots.size() == 4);
    CHECK (clip.slots[0].copied && clip.slots[0].notes.size() == 2 && clip.slots[0].loops && clip.slots[0].loopSetting == ChannelLoop::nextBeat);
    CHECK (! clip.slots[1].copied && clip.slots[1].notes.empty());
    CHECK (clip.slots[2].copied && clip.slots[2].notes.size() == 1);
    CHECK (! clip.slots[3].copied);
    CHECK (copyChannelSteps (p, {}, 5).empty());

    // a cut takes the notes away (the loop points stay)
    auto cut = p;
    CHECK (clearChannelSteps (cut, { 1, 3 }));
    CHECK (cut.notes.size() == 2);
    CHECK (cut.channelLoops.size() == 1);
    CHECK (! clearChannelSteps (cut, { 4 }));

    // paste at channel 2: slot 0 → channel 2 (its own note replaced, loop added), slot 1 (gap) leaves channel 3,
    // slot 2 → channel 4; nothing past the last channel
    auto pasted = p;
    CHECK (pasteChannelSteps (pasted, clip, { 2 }, 5));
    int c2 = 0, c3 = 0, c4 = 0;
    for (const auto& n : pasted.notes)
    {
        c2 += n.channel == 2;
        c3 += n.channel == 3;
        c4 += n.channel == 4;
    }
    CHECK (c2 == 2 && c3 == 1 && c4 == 1);
    CHECK (std::any_of (pasted.channelLoops.begin(), pasted.channelLoops.end(), [] (const ChannelLoop& l) { return l.channel == 2 && l.setting == ChannelLoop::nextBeat; }));
    CHECK (std::is_sorted (pasted.notes.begin(), pasted.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; }));
    // an empty copied slot clears the target; no selection or no clipboard: nothing happens
    auto cleared = p;
    CHECK (pasteChannelSteps (cleared, copyChannelSteps (p, { 4 }, 5), { 0 }, 5));
    CHECK (std::none_of (cleared.notes.begin(), cleared.notes.end(), [] (const Note& n) { return n.channel == 0; }));
    auto same = p;
    CHECK (! pasteChannelSteps (same, clip, {}, 5));
    CHECK (! pasteChannelSteps (same, {}, { 0 }, 5));
    CHECK (same.notes.size() == p.notes.size());

    // Ctrl+X / C / V with the rack focused
    keys::KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'X', keys::ctrl }, keys::Context::channelRack, false) == "rack.cut");
    CHECK (map.find ({ 'C', keys::ctrl }, keys::Context::channelRack, false) == "rack.copy");
    CHECK (map.find ({ 'V', keys::ctrl }, keys::Context::channelRack, false) == "rack.paste");
}

TEST (rack170a_menu_mnemonics)
{
    namespace mm = lynx::menumnemonic;
    CHECK (mm::apply ("Delete...", 'D') == std::string ("\x01") + "Delete...");
    CHECK (mm::apply ("Cut\tCtrl+X", 't') == std::string ("Cu\x01t\tCtrl+X"));
    CHECK (mm::apply ("Copy", 'o') == std::string ("C\x01opy"));
    // a word's first letter is preferred ("Receive &notes from")
    CHECK (mm::apply ("Receive notes from", 'n') == std::string ("Receive \x01notes from"));
    CHECK (mm::apply ("Fill each 4 steps", '4') == std::string ("Fill each \x01" "4 steps"));
    // a translation without the letter: FL's key in brackets
    CHECK (mm::apply ("Excluir...", 'D') == std::string ("Excluir... (\x01" "D)"));
    CHECK (mm::apply ("Excluir...\tAlt+Del", 'D') == std::string ("Excluir... (\x01" "D)\tAlt+Del"));
    // UTF-8 text is never split
    CHECK (mm::apply ("Löschen", 'l') == std::string ("\x01Löschen"));
    // already marked / no key: unchanged
    CHECK (mm::apply (mm::apply ("Paste", 'p'), 'a') == mm::apply ("Paste", 'p'));
    CHECK (mm::apply ("Paste", ' ') == "Paste");
    int at = 0;
    CHECK (mm::strip (mm::apply ("Cut", 't'), &at) == "Cut" && at == 2);
    CHECK (mm::strip ("Cut", &at) == "Cut" && at == -1);
    CHECK (mm::keyOf (mm::apply ("Delete", 'D')) == 'd');
    CHECK (mm::keyOf ("Delete") == 0);
}

TEST (rack170a_pattern_selector_drag_and_wheel)
{
    using namespace lynx::patterndrag;
    CHECK (steps (0.0f) == 0);
    CHECK (steps (-(pixelsPerPattern - 1.0f)) == 0);
    CHECK (steps (-pixelsPerPattern) == 1);   // up: the next pattern
    CHECK (steps (-3.0f * pixelsPerPattern) == 3);
    CHECK (steps (2.0f * pixelsPerPattern) == -2); // down: the previous ones
    Wheel w;
    CHECK (w.take (0.23f) == 1);  // a mouse notch up: the next pattern (FL)
    CHECK (w.take (-0.23f) == -1);
    CHECK (w.take (0.04f) == 0 && w.take (0.04f) == 0 && w.take (0.04f) == 1); // trackpad deltas add up
    CHECK (w.take (0.05f) == 0 && w.take (-0.05f) == 0); // the other way starts over
}

TEST (rack170a_pattern_selector_past_the_last)
{
    using namespace lynx;
    Song s;
    s.patterns.resize (2);
    s.patterns[0].name = "Pattern 1";
    s.patterns[1].name = "Pattern 2";
    s.currentPattern = 1;
    // the last pattern is empty: no new one
    CHECK (stepPatternCreating (s, 1) == 1 && s.patterns.size() == 2);
    Note n;
    s.patterns[1].notes.push_back (n);
    // with notes: one new empty pattern, named after the others
    CHECK (stepPatternCreating (s, 3) == 2 && s.patterns.size() == 3 && s.patterns[2].name == "Pattern 3");
    s.currentPattern = 2;
    CHECK (stepPatternCreating (s, 1) == 2 && s.patterns.size() == 3);
    CHECK (stepPatternCreating (s, -5) == 0);
}

TEST (rack170a_playhead_in_song_mode_and_note_names)
{
    using namespace lynx;
    // the rack's playhead uses the Piano Roll's rule: Song mode shows it while a clip of the shown pattern plays
    Song s;
    s.patterns.resize (2);
    s.currentPattern = 1;
    s.mode = PlayMode::song;
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 1;
    c.start = 4 * ppq;
    c.length = 4 * ppq;
    s.playlist.push_back (c);
    CHECK (transportedit::patternPlayhead (s, 1, 5.0 * ppq) == 1.0 * ppq);
    CHECK (transportedit::patternPlayhead (s, 1, 1.0 * ppq) < 0.0);
    CHECK (transportedit::patternPlayhead (s, 0, 5.0 * ppq) < 0.0);
    // every Piano roll key is named by default (LYNX)
    CHECK (notenames::everyKey.load());
}

TEST (rack170a_step_groups_clearly_apart)
{
    using namespace lynx::themes;
    // Busy Works Beats: "too close of a color ... dramatically different" — FL's groups are grey / red-tinted
    const auto& d = defaultPalette();
    CHECK (d.stepOffAlt != d.stepOff);
    const auto r = [] (std::uint32_t c) { return (int) ((c >> 16) & 255); };
    const auto gch = [] (std::uint32_t c) { return (int) ((c >> 8) & 255); };
    CHECK (r (d.stepOffAlt) - r (d.stepOff) >= 25);              // clearly apart (lighter, warmer)
    CHECK (r (d.stepOffAlt) > gch (d.stepOffAlt));               // a reddish grey, as FL's
    // every theme (light ones too, and other accents) keeps the groups apart
    for (const auto bg : { 0xff1c1e23u, 0xfff2f3f5u, 0xff3a3d44u })
        for (const auto accent : { 0xffe5383bu, 0xff3a8ee5u })
        {
            const auto p = derivePalette (accent, bg);
            int diff = 0;
            for (const int shift : { 16, 8, 0 })
                diff += std::abs ((int) ((p.stepOffAlt >> shift) & 255) - (int) ((p.stepOff >> shift) & 255));
            CHECK (diff >= 45);
            // round 170a2: an empty step can't pass for a lit one — a dull grey (low saturation), far from the lit step
            const auto ch = [&p] (int shift) { return (int) ((p.stepOffAlt >> shift) & 255); };
            const auto spread = std::max ({ ch (16), ch (8), ch (0) }) - std::min ({ ch (16), ch (8), ch (0) });
            CHECK (spread <= 30);
            int fromLit = 0;
            for (const int shift : { 16, 8, 0 })
                fromLit += std::abs ((int) ((p.stepOffAlt >> shift) & 255) - (int) ((p.accent >> shift) & 255));
            CHECK (fromLit >= 150);
        }
}

TEST (rack170a2_lit_steps_in_the_channel_colour)
{
    using namespace lynx::themes;
    // LYNX: a lit step takes its channel's colour, lifted until it stands clear of both empty-step greys
    for (const auto bg : { 0xff1c1e23u, 0xfff2f3f5u })
    {
        const auto p = derivePalette (0xffe5383bu, bg);
        CHECK (litStepColour (0, p) == p.accent); // no channel colour: the theme's lit step (the accent)
        for (const auto colour : { 0xffe5383bu, 0xff2fbf71u, 0xff3d8bfdu, 0xff202020u, 0xffeeeeeeu, 0xff5c1f24u, 0xfff0a020u })
        {
            const auto lit = litStepColour (colour, p);
            CHECK (contrastRatio (lit, p.stepOff) >= minLitStepContrast);
            CHECK (contrastRatio (lit, p.stepOffAlt) >= minLitStepContrast);
        }
    }
    // a colour that already reads stays exactly the channel's
    CHECK (litStepColour (0xff2fbf71u, defaultPalette()) == 0xff2fbf71u);
}
