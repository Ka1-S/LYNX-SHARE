#include "engine/RemotePatternEdits.h"
#include "core/SessionIds.h"
#include <cstdio>
#include <stdexcept>

using namespace lynx;
namespace
{
void check (bool condition, const char* message)
{
    if (! condition) throw std::runtime_error (message);
}
template <typename Fn> void refuses (Fn&& fn)
{
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    check (rejected, "invalid pattern transaction was accepted");
}
Song fixture()
{
    Song song;
    song.chanceSeed = 0xfedcba98u;
    song.channels.resize (2);
    song.patterns.resize (2);
    auto& pattern = song.patterns[0];
    pattern.name = "Lead";
    pattern.lengthTicks = 1920;
    pattern.colour = 0xff123456;
    pattern.loopOn = true;
    pattern.loopMode = RackLoopMode::advanced;
    pattern.channelLoops.push_back ({ 1, 8 });
    pattern.loopStarterGenre = "Ambient";
    Note note;
    note.start = 120;
    note.length = 840;
    note.channel = 1;
    note.key = 73;
    note.velocity = 0.23f;
    note.pan = -0.44f;
    note.finePitch = 4.5f;
    note.muted = true;
    note.slide = true;
    note.porta = true;
    note.release = 0.18f;
    note.modX = 0.11f;
    note.modY = 0.29f;
    note.colour = 7;
    note.group = 42;
    note.chance = 0.37123457f;
    note.velocityRange = -0.12345679f;
    note.chanceGroup = 17;
    note.chanceOne = true;
    pattern.notes.push_back (note);
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelVolume, 1 };
    lane.events.push_back ({ 240, 0.8f });
    pattern.events.push_back (lane);
    Marker marker;
    marker.tick = 480;
    marker.name = "Phrase";
    pattern.markers.push_back (marker);
    song.patterns[1].name = "Pattern 1";
    song.currentPattern = 1;
    Clip clip;
    clip.pattern = 0;
    clip.length = 1920;
    song.playlist.push_back (clip);
    clip.pattern = 1;
    clip.start = 1920;
    song.playlist.push_back (clip);
    song.blocks = { { 0, 0 }, { 1, 1920 } };
    song.arrangements.push_back ({ "Other", { clip } });
    song.arrangements[1].blocks.push_back ({ 1, 0 });
    uids::normalize (song);
    return song;
}
void createAndClone()
{
    const auto original = fixture();
    auto created = remote_patterns::create (original, 0, "New", 3840);
    check (created.index == 0 && created.song.currentPattern == 0 && created.song.patterns.size() == 3, "creation did not insert/select");
    check (created.song.patterns[0].name == "New" && created.song.patterns[0].lengthTicks == 3840
           && created.song.patterns[0].notes.empty(), "creation did not retain requested empty pattern metadata");
    check (created.song.patterns[1] == original.patterns[0] && created.song.patterns[2] == original.patterns[1], "creation changed original patterns");
    check (created.song.chanceSeed == original.chanceSeed, "creation changed the project's note probability seed");
    check (created.song.playlist[0].pattern == 1 && created.song.playlist[1].pattern == 2
           && created.song.arrangements[1].clips[0].pattern == 2, "creation lost clip references across arrangements");
    check (created.song.blocks[0].pattern == 1 && created.song.blocks[1].pattern == 2
           && created.song.arrangements[1].blocks[0].pattern == 2, "creation lost Pattern Block references");
    uids::normalize (created.song);
    check (created.song.patterns[0].uid != 0 && created.song.patterns[0].uid != original.patterns[0].uid
           && created.song.patterns[1].uid == original.patterns[0].uid, "creation reused or changed an identity");
    auto appended = remote_patterns::create (original);
    check (appended.index == 2 && appended.song.patterns[2].name == "Pattern 2" && appended.song.patterns[2].lengthTicks == 0,
           "default creation did not append with the next automatic name/length");
    check (appended.song.playlist == original.playlist && appended.song.blocks == original.blocks, "append remapped existing placements");

    auto cloned = remote_patterns::clone (original, 0);
    check (cloned.index == 1 && cloned.song.currentPattern == 1 && cloned.song.patterns.size() == 3, "clone did not insert/select after source");
    auto expected = original.patterns[0];
    expected.name = "Lead #2";
    expected.uid = 0;
    check (cloned.song.patterns[1] == expected, "clone lost note expression/probability, events, markers, loops, colour or other pattern data");
    check (cloned.song.chanceSeed == original.chanceSeed, "clone changed the project's note probability seed");
    check (cloned.song.patterns[0] == original.patterns[0] && cloned.song.patterns[2] == original.patterns[1], "clone changed source patterns");
    check (cloned.song.playlist[0] == original.playlist[0] && cloned.song.playlist[1].pattern == 2
           && cloned.song.arrangements[1].clips[0].pattern == 2, "clone changed existing clip sources");
    check (cloned.song.blocks[0] == original.blocks[0] && cloned.song.blocks[1].pattern == 2
           && cloned.song.arrangements[1].blocks[0].pattern == 2, "clone changed Pattern Block sources");
    uids::normalize (cloned.song);
    check (cloned.song.patterns[1].uid != 0 && cloned.song.patterns[1].uid != original.patterns[0].uid
           && cloned.song.patterns[0].uid == original.patterns[0].uid, "clone identity was not fresh");
    auto named = remote_patterns::clone (original, 1, "Verse");
    check (named.index == 2 && named.song.patterns[2].name == "Verse", "clone ignored supplied name");
    check (original.patterns.size() == 2 && original.playlist[0].pattern == 0 && original.playlist[1].pattern == 1,
           "staging mutated the original project");
}
void loopStarterReferences()
{
    auto original = fixture();
    original.patterns.resize (4);
    original.channels.resize (5);
    for (int i = 0; i < 4; ++i)
        original.channels[(std::size_t) i].loopStarterPattern = i;
    original.channels[4].loopStarterPattern = -1;
    uids::normalize (original);
    auto expected = original.channels;
    for (int i = 1; i < 4; ++i)
        ++expected[(std::size_t) i].loopStarterPattern;

    const auto created = remote_patterns::create (original, 1);
    check (created.song.channels == expected,
           "creation did not preserve Loop Starter references before/at/after insertion and its sentinel");
    const auto cloned = remote_patterns::clone (original, 0);
    check (cloned.song.channels == expected,
           "clone did not preserve Loop Starter references before/at/after insertion and its sentinel");
    const auto appended = remote_patterns::create (original);
    const auto clonedLast = remote_patterns::clone (original, 3);
    check (appended.song.channels == original.channels && clonedLast.song.channels == original.channels,
           "appending a pattern changed Loop Starter ownership");
    check (original.channels[0].loopStarterPattern == 0 && original.channels[1].loopStarterPattern == 1
           && original.channels[2].loopStarterPattern == 2 && original.channels[3].loopStarterPattern == 3
           && original.channels[4].loopStarterPattern == -1, "staging changed the source Loop Starter ownership");
}
void clipGridReferencesAndLengths()
{
    auto original = fixture();
    original.patterns.resize (3);
    original.clipGrid.emplace();
    auto& grid = *original.clipGrid;
    grid.scenes.push_back ({ "Scene", 0xff345678, 102.0, 3 });
    grid.globalQuant = LaunchQuant::bars2;
    grid.followActions = false;
    grid.backOnStop = true;
    for (int pattern = 0; pattern < 3; ++pattern)
    {
        Clip clip;
        clip.pattern = pattern;
        clip.length = 1920;
        clip.track = pattern;
        clip.gridSlot = (std::int16_t) grid.slots.size();
        grid.clips.push_back (clip);
        ClipGridSlot slot;
        slot.name = "Launch " + std::to_string (pattern);
        slot.legato = true;
        slot.mode = LaunchMode::gate;
        grid.slots.push_back (slot);
    }
    auto trimmed = grid.clips[0];
    trimmed.length = 1500;
    trimmed.track = 3;
    trimmed.gridSlot = 3;
    grid.clips.push_back (trimmed);
    grid.slots.emplace_back();
    auto offset = grid.clips[0];
    offset.offset = 120;
    offset.track = 4;
    offset.gridSlot = 4;
    grid.clips.push_back (offset);
    grid.slots.emplace_back();
    // A removed stop button is a zero-length marker, not a pattern placement.
    Clip noStop;
    noStop.pattern = -1;
    noStop.length = 0;
    noStop.track = 5;
    noStop.gridSlot = 5;
    grid.clips.push_back (noStop);
    grid.slots.emplace_back();
    grid.slots.back().noStop = true;
    uids::normalize (original);

    auto shifted = *original.clipGrid;
    shifted.clips[1].pattern = 2;
    shifted.clips[2].pattern = 3;
    check (remote_patterns::create (original, 1).song.clipGrid == shifted,
           "creation lost grid references before/at/after insertion, launch settings or the no-stop marker");
    check (remote_patterns::clone (original, 0).song.clipGrid == shifted,
           "clone lost grid references before/at/after insertion, launch settings or the no-stop marker");
    check (remote_patterns::create (original).song.clipGrid == original.clipGrid
           && remote_patterns::clone (original, 2).song.clipGrid == original.clipGrid,
           "appending a pattern changed the clip grid");

    auto resized = remote_patterns::set (original, 0, {}, 3840);
    followPatternLengths (patternLengths (original), resized.song);
    auto followed = *original.clipGrid;
    followed.clips[0].length = 3840;
    check (resized.song.clipGrid == followed,
           "length following did not update the full grid clip while preserving trimmed/offset clips and the no-stop marker");
    check (remote_patterns::set (original, 0, "Grid source").song.clipGrid == original.clipGrid,
           "renaming a grid source changed its placements or launch settings");
    check (original.clipGrid->clips[0].length == 1920 && original.clipGrid->clips[1].pattern == 1
           && original.clipGrid->clips[2].pattern == 2 && original.clipGrid->clips.back().pattern == -1
           && original.clipGrid->slots.back().noStop, "staging changed the source clip grid");
}
void metadataAndValidation()
{
    const auto original = fixture();
    check (! remote_patterns::metadataChanged (original, 0, "Lead", 1920), "no-op metadata edit was reported as a change");
    auto renamed = remote_patterns::set (original, 0, "Verse");
    auto expected = original.patterns[0];
    expected.name = "Verse";
    check (renamed.song.patterns[0] == expected && renamed.song.currentPattern == original.currentPattern,
           "rename changed unrequested pattern data or selection");
    check (renamed.song.playlist == original.playlist && renamed.song.blocks == original.blocks, "rename changed placements");
    auto lengthened = remote_patterns::set (original, 0, {}, 3840);
    followPatternLengths (patternLengths (original), lengthened.song);
    check (lengthened.song.playlist[0].length == 3840 && lengthened.song.playlist[1] == original.playlist[1],
           "native full-pattern clip length following changed unrelated placements");
    check (lengthened.song.patterns[0].notes == original.patterns[0].notes
           && lengthened.song.chanceSeed == original.chanceSeed && renamed.song.chanceSeed == original.chanceSeed,
           "metadata edit changed note probability, velocity range, grouping or the project's seed");
    auto trimmed = original;
    trimmed.playlist[0].length = 1500;
    trimmed.arrangements[1].clips[0].pattern = 0;
    trimmed.arrangements[1].clips[0].length = 1920;
    auto resized = remote_patterns::set (trimmed, 0, {}, 3840);
    followPatternLengths (patternLengths (trimmed), resized.song);
    check (resized.song.playlist[0].length == 1500 && resized.song.arrangements[1].clips[0].length == 3840,
           "length following did not preserve trimmed clips or update stored arrangements");
    check (remote_patterns::set (original, 0, {}, 0).song.patterns[0].lengthTicks == 0, "automatic length was refused");
    // Round 190ha: only what LYNX's pattern length box can set, 0 (automatic) or 1..999 whole bars of the song's meter
    check (remote_patterns::set (original, 0, {}, 999 * 3840).song.patterns[0].lengthTicks == 999 * 3840, "999 bars were refused");
    for (const auto ticks : { std::int64_t (960), std::int64_t (5760), std::int64_t (3841), std::int64_t (1000) * 3840 })
        refuses ([&] { remote_patterns::set (original, 0, {}, ticks); });
    // (the fixture's own 1920, half a bar, is its current length: restating it is the no-op checked above)
    check (! remote_patterns::metadataChanged (original, 0, {}, 1920), "restating a non-bar current length was refused");
    refuses ([&] { remote_patterns::create (original, {}, {}, 1920); });
    auto compound = original; // 6/8: a bar is six eighths, 2880 ticks
    compound.beatsPerBar = 6;
    compound.beatUnit = 8;
    check (remote_patterns::set (compound, 0, {}, 2880).song.patterns[0].lengthTicks == 2880
           && remote_patterns::create (compound, {}, {}, 5760).song.patterns.back().lengthTicks == 5760, "whole 6/8 bars were refused");
    refuses ([&] { remote_patterns::set (compound, 0, {}, 3840); });
    auto longNote = original; // a whole-bar length still cannot end before a note
    longNote.patterns[0].notes[0].length = 4000;
    refuses ([&] { remote_patterns::set (longNote, 0, {}, 3840); });
    check (remote_patterns::set (longNote, 0, {}, 2 * 3840).song.patterns[0].lengthTicks == 7680, "a length past the last note was refused");

    refuses ([&] { remote_patterns::create (original, -1); });
    refuses ([&] { remote_patterns::create (original, 3); });
    refuses ([&] { remote_patterns::create (original, {}, ""); });
    refuses ([&] { remote_patterns::create (original, {}, "Bad\nname"); });
    refuses ([&] { remote_patterns::create (original, {}, " "); });
    refuses ([&] { remote_patterns::create (original, {}, {}, -1); });
    refuses ([&] { remote_patterns::create (original, {}, {}, remote_patterns::maxLength + 1); });
    refuses ([&] { remote_patterns::clone (original, -1); });
    refuses ([&] { remote_patterns::clone (original, 2); });
    refuses ([&] { remote_patterns::set (original, 0); });
    refuses ([&] { remote_patterns::set (original, 0, {}, 959); });
    refuses ([&] { remote_patterns::set (original, 0, "Rejected rename", 959); });
    refuses ([&] { remote_patterns::set (original, 0, "Rejected rename", 1921); });
    refuses ([&] { remote_patterns::set (original, 2, "Missing"); });
    check (original.patterns[0].name == "Lead" && original.patterns[0].lengthTicks == 1920
           && original.playlist[0].length == 1920, "rejected metadata edit changed the original model");
}
}
int main()
{
    try
    {
        createAndClone();
        loopStarterReferences();
        clipGridReferencesAndLengths();
        metadataAndValidation();
        std::puts ("Remote pattern creation, cloning, metadata and reference tests passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "%s\n", error.what());
        return 1;
    }
}
