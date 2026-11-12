// Round 190ef: the reference DAW's MIDI Map Mode / Key Map Mode rules (core/RemoteMap.h): mapping ranges (Min / Max,
// inverted), button edges, the lesson's two refusals, the Mapping Browser's list model, mappings following their scene
// rows, and what a MIDI event or a key fires. docs/analise/REA_rodada190ef.md.

#include "core/ClipLauncher.h"
#include "core/RemoteMap.h"

namespace live190efmap
{
inline lynx::Song gridSong()
{
    lynx::Song s;
    s.playlistTracks.resize (3);
    if (s.patterns.empty())
        s.patterns.emplace_back();
    lynx::Clip c;
    c.pattern = 0;
    c.length = 4 * lynx::ppq;
    lynx::launcher::setClip (s, 0, 0, c);
    lynx::launcher::setClip (s, 1, 1, c);
    lynx::launcher::insertScene (s, 2); // three scenes (setClip made two)
    return s;
}
inline const lynx::AutomationTarget volume { lynx::AutomationTargetKind::mixerVolume, 1, 0, 0 };
inline const lynx::AutomationTarget pan { lynx::AutomationTargetKind::mixerPan, 1, 0, 0 };
} // namespace live190efmap

TEST (live190ef_map_range_maps_the_travel_onto_min_max)
{
    using namespace lynx::remotemap;
    CHECK (applyRange (0.0f, 0.0f, 1.0f) == 0.0f && applyRange (1.0f, 0.0f, 1.0f) == 1.0f);
    CHECK (std::abs (applyRange (0.5f, 0.0f, 1.0f) - 0.5f) < 1e-6f);  // the default range keeps the position
    CHECK (std::abs (applyRange (0.0f, 0.25f, 0.75f) - 0.25f) < 1e-6f);
    CHECK (std::abs (applyRange (1.0f, 0.25f, 0.75f) - 0.75f) < 1e-6f);
    CHECK (std::abs (applyRange (0.5f, 0.25f, 0.75f) - 0.5f) < 1e-6f);
    // Min above Max inverts (the lesson: "Mappings can be inverted by setting the value of Min to be greater than Max")
    CHECK (std::abs (applyRange (0.0f, 1.0f, 0.0f) - 1.0f) < 1e-6f && std::abs (applyRange (1.0f, 1.0f, 0.0f)) < 1e-6f);
    CHECK (std::abs (applyRange (0.25f, 0.8f, 0.2f) - 0.65f) < 1e-6f);
    // out-of-range input and a range outside 0..1 stay inside 0..1
    CHECK (applyRange (2.0f, 0.0f, 1.0f) == 1.0f && applyRange (-1.0f, 0.0f, 1.0f) == 0.0f);
    CHECK (applyRange (std::nanf (""), 0.0f, 1.0f) == 0.0f);
}

TEST (live190ef_map_key_toggles_between_min_and_max)
{
    using namespace lynx::remotemap;
    CHECK (keyToggle (0.3f, 0.0f, 1.0f) == 1.0f); // anywhere but Max: to Max
    CHECK (keyToggle (1.0f, 0.0f, 1.0f) == 0.0f); // at Max: to Min
    CHECK (std::abs (keyToggle (0.75f, 0.25f, 0.75f) - 0.25f) < 1e-6f);
    CHECK (std::abs (keyToggle (0.25f, 0.25f, 0.75f) - 0.75f) < 1e-6f);
    // inverted: Max is the lower value, the toggle still alternates
    CHECK (std::abs (keyToggle (0.5f, 0.9f, 0.1f) - 0.1f) < 1e-6f && std::abs (keyToggle (0.1f, 0.9f, 0.1f) - 0.9f) < 1e-6f);
}

TEST (live190ef_map_button_edges)
{
    using namespace lynx::remotemap;
    const auto note = lynx::linkNoteBase + 36;
    CHECK (midiEdge (note, 100, -1) == Edge::press && midiEdge (note, 0, 100) == Edge::release);
    CHECK (midiEdge (note, 1, 1) == Edge::press); // every note on presses, even at the same velocity
    CHECK (midiEdge (20, 127, -1) == Edge::press && midiEdge (20, 0, 127) == Edge::release);
    CHECK (midiEdge (20, 127, 127) == Edge::none && midiEdge (20, 80, 127) == Edge::none); // still down
    CHECK (midiEdge (20, 63, -1) == Edge::none && midiEdge (20, 64, 63) == Edge::press && midiEdge (20, 63, 64) == Edge::release);
}

TEST (live190ef_map_assigning_a_parameter_makes_one_link)
{
    using namespace lynx::remotemap;
    lynx::Song s;
    CHECK (assignMidi (s, Target::parameter (live190efmap::volume), { 0, 16, -1 }) == Refusal::none);
    CHECK (s.midiLinks.size() == 1 && s.midiLinks[0].cc == 16 && s.midiLinks[0].channel == 0 && s.midiLinks[0].target == live190efmap::volume);
    CHECK (s.remoteMappings.empty()); // (a parameter's MIDI mapping is FL's link: one storage)
    // a new control for the same parameter replaces the old one; the same control may drive another parameter too
    CHECK (assignMidi (s, Target::parameter (live190efmap::volume), { 0, 17, 3 }) == Refusal::none);
    CHECK (s.midiLinks.size() == 1 && s.midiLinks[0].cc == 17 && s.midiLinks[0].port == 3);
    CHECK (assignMidi (s, Target::parameter (live190efmap::pan), { 0, 17, 3 }) == Refusal::none);
    CHECK (s.midiLinks.size() == 2);
    const auto a = midiOf (s, Target::parameter (live190efmap::pan));
    CHECK (a.found && a.channel == 0 && a.number == 17);
    CHECK (assignMidi (s, Target::parameter ({}), { 0, 1, -1 }) == Refusal::invalid);
    CHECK (assignMidi (s, Target::parameter (live190efmap::pan), { 0, 200, -1 }) == Refusal::invalid);
}

TEST (live190ef_map_slot_and_scene_rules_of_the_lesson)
{
    using namespace lynx::remotemap;
    auto s = live190efmap::gridSong();
    const MidiControl pad { 9, lynx::linkNoteBase + 36, -1 };
    CHECK (assignMidi (s, Target::slot (0, 0), pad) == Refusal::none);
    // "can be assigned to multiple clips so long as they all reside in different tracks"
    CHECK (assignMidi (s, Target::slot (1, 1), pad) == Refusal::none);
    CHECK (assignMidi (s, Target::slot (0, 2), pad) == Refusal::sameTrack);
    // "you can't assign a MIDI controller to a scene if it has already been assigned to a clip"
    CHECK (assignMidi (s, Target::sceneLaunch (1), pad) == Refusal::usedBySlot);
    CHECK (assignMidi (s, Target::sceneLaunch (1), { 9, lynx::linkNoteBase + 37, -1 }) == Refusal::none);
    CHECK (s.remoteMappings.size() == 3);
    // the slot's mapping again with another control: replaced, not added
    CHECK (assignMidi (s, Target::slot (0, 0), { 9, lynx::linkNoteBase + 40, -1 }) == Refusal::none);
    CHECK (s.remoteMappings.size() == 3 && midiOf (s, Target::slot (0, 0)).number == lynx::linkNoteBase + 40);
    // now track 0 is free for the first pad again
    CHECK (assignMidi (s, Target::slot (0, 2), pad) == Refusal::none);
    // keys follow the same rules
    CHECK (assignKey (s, Target::slot (0, 0), 'A') == Refusal::none);
    CHECK (assignKey (s, Target::slot (0, 1), 'A') == Refusal::sameTrack);
    CHECK (assignKey (s, Target::slot (2, 1), 'A') == Refusal::none);
    CHECK (assignKey (s, Target::sceneLaunch (0), 'A') == Refusal::usedBySlot);
    CHECK (assignKey (s, Target::of (lynx::RemoteAction::play), ' ') == Refusal::none);
    CHECK (assignKey (s, Target::of (lynx::RemoteAction::play), 0) == Refusal::invalid);
    CHECK (keyOf (s, Target::slot (2, 1)).key == 'A' && ! keyOf (s, Target::slot (1, 1)).found);
    // unmap: one kind only
    CHECK (unmap (s, Target::slot (0, 0), Mode::key) == 1 && midiOf (s, Target::slot (0, 0)).found && ! keyOf (s, Target::slot (0, 0)).found);
    CHECK (unmap (s, Target::slot (0, 0), Mode::midi) == 1 && ! midiOf (s, Target::slot (0, 0)).found);
}

TEST (live190ef_map_browser_rows_min_max_invert_delete)
{
    using namespace lynx::remotemap;
    auto s = live190efmap::gridSong();
    assignMidi (s, Target::parameter (live190efmap::volume), { 0, 16, -1 });
    assignMidi (s, Target::slot (0, 0), { 0, 60, -1 });
    assignKey (s, Target::parameter (live190efmap::pan), 'Q');
    assignKey (s, Target::sceneLaunch (2), '1');
    const auto midi = rows (s, Mode::midi), keys = rows (s, Mode::key);
    CHECK (midi.size() == 2 && keys.size() == 2 && rows (s, Mode::off).empty());
    CHECK (midi[0].link && midi[0].hasRange && midi[0].number == 16 && midi[0].target == Target::parameter (live190efmap::volume));
    CHECK (! midi[1].link && ! midi[1].hasRange && midi[1].target == Target::slot (0, 0)); // a slot has no range
    CHECK (keys[0].key == 'Q' && keys[0].hasRange && keys[1].key == '1' && ! keys[1].hasRange);
    // Min / Max of a link and of a key mapping
    CHECK (setRange (s, midi[0], 0.2f, 0.9f) && s.midiLinks[0].min == 0.2f && s.midiLinks[0].max == 0.9f);
    CHECK (s.midiLinks[0].hasSettings()); // (saved with the link's other settings)
    CHECK (setRange (s, keys[0], 0.1f, 0.6f));
    CHECK (! setRange (s, midi[1], 0.1f, 0.6f)); // not for a slot
    CHECK (setRange (s, midi[0], -1.0f, 3.0f) && s.midiLinks[0].min == 0.0f && s.midiLinks[0].max == 1.0f); // clamped
    setRange (s, midi[0], 0.2f, 0.9f);
    CHECK (invertRange (s, rows (s, Mode::midi)[0]) && s.midiLinks[0].min == 0.9f && s.midiLinks[0].max == 0.2f);
    // Delete
    CHECK (removeRow (s, rows (s, Mode::midi)[1]) && rows (s, Mode::midi).size() == 1 && rows (s, Mode::key).size() == 2);
    CHECK (removeRow (s, rows (s, Mode::midi)[0]) && s.midiLinks.empty());
    CHECK (! removeRow (s, Row { true, 5 }));
    // the list's texts
    CHECK (controlText (16) == "CC 16" && controlText (lynx::linkNoteBase + 60) == "C3" && controlText (lynx::linkNoteBase + 61) == "C#3");
    CHECK (noteText (0) == "C-2" && noteText (127) == "G8");
}

TEST (live190ef_map_mappings_follow_their_scene_rows)
{
    using namespace lynx::remotemap;
    auto s = live190efmap::gridSong();
    assignMidi (s, Target::slot (1, 1), { 0, 61, -1 });
    assignKey (s, Target::sceneLaunch (2), 'S');
    assignKey (s, Target::trackStop (1), 'T');
    lynx::launcher::insertScene (s, 1); // a row above: both move down
    CHECK (midiOf (s, Target::slot (1, 2)).found && ! midiOf (s, Target::slot (1, 1)).found);
    CHECK (keyOf (s, Target::sceneLaunch (3)).found);
    CHECK (keyOf (s, Target::trackStop (1)).found); // (a track's stop button has no row)
    lynx::launcher::removeScene (s, 2); // the slot's row goes: its mapping goes with it
    CHECK (! midiOf (s, Target::slot (1, 2)).found && s.remoteMappings.size() == 2);
    CHECK (keyOf (s, Target::sceneLaunch (2)).found); // the scene below moved up
}

TEST (live190ef_map_what_an_event_or_a_key_fires)
{
    using namespace lynx::remotemap;
    auto s = live190efmap::gridSong();
    assignMidi (s, Target::slot (0, 0), { 0, 20, 2 });
    assignMidi (s, Target::slot (1, 1), { 0, 20, 2 });
    assignMidi (s, Target::of (lynx::RemoteAction::stopAllClips), { 0, 21, -1 });
    assignMidi (s, Target::parameter (live190efmap::volume), { 0, 22, -1 }); // (a link: never a "fire")
    auto fires = midiFires (s, 0, 20, 127, -1, 2);
    CHECK (fires.size() == 2 && fires[0].edge == Edge::press && fires[0].target == Target::slot (0, 0) && fires[1].target == Target::slot (1, 1));
    CHECK (midiFires (s, 0, 20, 127, -1, 3).empty()); // another port
    CHECK (midiFires (s, 1, 20, 127, -1, 2).empty()); // another channel
    CHECK (midiFires (s, 0, 20, 127, 127, 2).empty()); // no edge
    fires = midiFires (s, 0, 20, 0, 127, 2);
    CHECK (fires.size() == 2 && fires[0].edge == Edge::release);
    CHECK (midiFires (s, 0, 21, 100, 0, -1).size() == 1 && midiFires (s, 0, 22, 127, 0, -1).empty());
    // a mapping made for "any channel" hears every channel
    s.remoteMappings[2].channel = -1;
    CHECK (midiFires (s, 7, 21, 100, 0, -1).size() == 1);
    // keys: parameters fire too (they toggle), with their range
    assignKey (s, Target::parameter (live190efmap::pan), 'P');
    setRange (s, rows (s, Mode::key)[0], 0.2f, 0.8f);
    const auto keyFired = keyFires (s, 'P', true);
    CHECK (keyFired.size() == 1 && keyFired[0].target.action == lynx::RemoteAction::parameter && keyFired[0].min == 0.2f && keyFired[0].max == 0.8f);
    CHECK (keyFires (s, 'P', false)[0].edge == Edge::release && keyFires (s, 'X', true).empty());
    // the MIDI numbers the mappings take from the instruments
    std::uint64_t ccs[2] {}, notes[2] {};
    assignMidi (s, Target::sceneLaunch (0), { 0, lynx::linkNoteBase + 100, -1 });
    usedNumbers (s, ccs, notes);
    CHECK ((ccs[0] >> 20 & 1u) && (ccs[0] >> 21 & 1u) && ! (ccs[0] >> 22 & 1u) && (notes[1] >> (100 - 64) & 1u));
}

TEST (live190ef_map_parameter_mappings_follow_and_leave_with_their_target)
{
    using namespace lynx::remotemap;
    lynx::Song s;
    s.channels.resize (2);
    assignKey (s, Target::parameter ({ lynx::AutomationTargetKind::channelVolume, 1, 0, 0 }), 'V');
    assignKey (s, Target::of (lynx::RemoteAction::metronome), 'M');
    auto visited = 0;
    s.forEachTarget ([&] (lynx::AutomationTarget&) { ++visited; });
    CHECK (visited >= 1);
    s.forEachTarget ([] (lynx::AutomationTarget& t) {
        if (t.kind == lynx::AutomationTargetKind::channelVolume)
            t.kind = lynx::AutomationTargetKind::none; // (as deleting the channel does)
    });
    s.removeDeadMidiLinks();
    CHECK (s.remoteMappings.size() == 1 && s.remoteMappings[0].action == lynx::RemoteAction::metronome);
    // a plain link has no settings until its range changes
    lynx::MidiLink l;
    CHECK (! l.hasSettings());
    l.max = 0.5f;
    CHECK (l.hasSettings());
}
