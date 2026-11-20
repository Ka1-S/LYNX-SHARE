// Round 190ht: issue #127 phase 2 (the other Playlist tools on FL 10's Pattern Blocks: Mute, Select + group move,
// Slice) and issue #154 (the linked headers' insert effects list, core/TrackInserts.h). docs/analise/REA_rodada190ht.md

#include "core/DetailChain.h"
#include "core/History.h"
#include "core/PatternBlocks.h"
#include "core/TrackInserts.h"

namespace iss190ht
{
constexpr std::int64_t bar = 4 * lynx::ppq;

// P1: 1 bar (a note per beat), P2: 2 bars (a note on beat 1 and a long note from beat 7 to the end), P3: 1 bar
inline lynx::Song song()
{
    lynx::Song s;
    s.channels.resize (1);
    s.patterns.resize (3);
    for (int i = 0; i < 3; ++i)
        s.patterns[(std::size_t) i].name = "P" + std::to_string (i + 1);
    for (int step : { 0, 4, 8, 12 })
        s.patterns[0].toggleStep (0, step);
    s.patterns[1].toggleStep (0, 0);
    lynx::Note longNote;
    longNote.start = 6 * lynx::ppq;
    longNote.length = 2 * lynx::ppq;
    s.patterns[1].notes.push_back (longNote);
    s.patterns[1].sortNotes();
    s.patterns[2].toggleStep (0, 0);
    s.patternBlocksOn = true;
    s.mode = lynx::PlayMode::song;
    return s;
}
} // namespace iss190ht

TEST (iss190ht_mute_tool_mutes_blocks_and_their_clips)
{
    using namespace lynx;
    using iss190ht::bar;
    auto s = iss190ht::song();
    CHECK (patternblocks::place (s, 0, 0) && patternblocks::place (s, 0, bar) && patternblocks::place (s, 2, 0));
    // a sweep over bars 1-2 of every row finds the three blocks; row 0 alone, two
    CHECK (patternblocks::blocksInRange (s, -1, 0, bar + 5).size() == 3);
    CHECK (patternblocks::blocksInRange (s, 0, bar + 5, 0).size() == 2); // (either order)
    const auto i = (std::size_t) patternblocks::indexOf (s, { 0, bar });
    CHECK (patternblocks::setMuted (s, i, true));
    CHECK (! patternblocks::setMuted (s, i, true)); // (already so: no change)
    CHECK (! patternblocks::setMuted (s, 99, true));
    // the block stays where it is; its clip is muted, the others play
    CHECK (s.blocks.size() == 3 && s.blocks[i].muted);
    const auto clips = patternblocks::asClips (s);
    int muted = 0;
    for (const auto& c : clips)
        muted += c.muted ? 1 : 0;
    CHECK (clips.size() == 3 && muted == 1);
    // moving keeps the mute; a block in place of a muted one is the same block (normalize ignores the mute)
    CHECK (patternblocks::move (s, 0, bar, 3 * bar) == 3 * bar);
    CHECK (s.blocks[(std::size_t) patternblocks::indexOf (s, { 0, 3 * bar })].muted);
    auto twice = s.blocks;
    twice.push_back ({ 0, 3 * bar, false });
    patternblocks::normalize (twice, 3);
    CHECK (twice.size() == 3);
    // turned into clips, the block is a muted clip (FL's clip mute)
    auto copy = s;
    CHECK (patternblocks::toClips (copy) == 3);
    muted = 0;
    for (const auto& c : copy.playlist)
        muted += c.muted ? 1 : 0;
    CHECK (muted == 1);
    // the undo step is named
    auto before = s;
    patternblocks::setMuted (s, (std::size_t) patternblocks::indexOf (s, { 2, 0 }), true);
    CHECK (std::string (describeChange (before, s).text) == "Mute pattern blocks");
}

TEST (iss190ht_muted_block_plays_nothing)
{
    using namespace lynx;
    auto s = iss190ht::song();
    CHECK (patternblocks::place (s, 0, 0));
    auto played = s;
    patternblocks::expandSong (played);
    CHECK (played.playlist.size() == 1 && ! played.playlist[0].muted);
    patternblocks::setMuted (s, 0, true);
    auto silent = s;
    patternblocks::expandSong (silent);
    CHECK (silent.playlist.size() == 1 && silent.playlist[0].muted);
}

TEST (iss190ht_select_tool_area_and_group_move)
{
    using namespace lynx;
    using iss190ht::bar;
    auto s = iss190ht::song();
    CHECK (patternblocks::place (s, 0, 0) && patternblocks::place (s, 1, 0) && patternblocks::place (s, 2, 2 * bar) && patternblocks::place (s, 0, 4 * bar));
    // a box over rows 0-1, bars 1-2 meets P1@1 and P2@1 (2 bars long); rows given either way round
    auto area = patternblocks::inArea (s, 1, 0, bar / 2, bar + 10);
    CHECK (area.size() == 2);
    CHECK (patternblocks::contains (area, { 0, 0 }) && patternblocks::contains (area, { 1, 0 }));
    CHECK (! patternblocks::contains (area, { 2, 2 * bar }));
    // the box over every row
    CHECK (patternblocks::inArea (s, 0, 2, 0, 5 * bar).size() == 4);

    // the group moves by a bar, each block in its own row; the mute goes with a block
    patternblocks::setMuted (s, (std::size_t) patternblocks::indexOf (s, { 1, 0 }), true);
    area = patternblocks::inArea (s, 0, 1, 0, bar - 1);
    const auto moved = patternblocks::moveGroup (s, area, bar);
    CHECK (moved.has_value() && moved->size() == 2);
    CHECK (patternblocks::indexOf (s, { 0, bar }) >= 0 && patternblocks::indexOf (s, { 1, bar }) >= 0);
    CHECK (patternblocks::indexOf (s, { 0, 0 }) < 0 && patternblocks::indexOf (s, { 1, 0 }) < 0);
    CHECK (s.blocks[(std::size_t) patternblocks::indexOf (s, { 1, bar })].muted);
    CHECK (s.blocks.size() == 4);
    // a block of the group would meet one that stays (P1@5 when P1 goes to bar 5): nothing moves
    const auto before = s.blocks;
    CHECK (! patternblocks::moveGroup (s, *moved, 3 * bar).has_value());
    CHECK (s.blocks == before);
    // nothing goes before bar 1: the group keeps its shape
    CHECK (! patternblocks::moveGroup (s, *moved, -2 * bar).has_value());
    CHECK (s.blocks == before);
    // a group of blocks one bar apart in the same row can move over its own places
    auto row = iss190ht::song();
    CHECK (patternblocks::place (row, 0, 0) && patternblocks::place (row, 0, bar));
    const auto both = patternblocks::inArea (row, 0, 0, 0, 2 * bar - 1);
    CHECK (both.size() == 2 && patternblocks::moveGroup (row, both, bar).has_value());
    CHECK (patternblocks::indexOf (row, { 0, bar }) >= 0 && patternblocks::indexOf (row, { 0, 2 * bar }) >= 0 && row.blocks.size() == 2);
    // a block that is gone (undo) makes no move
    CHECK (! patternblocks::moveGroup (row, { { 2, 0 } }, bar).has_value());
}

TEST (iss190ht_slice_tool_cuts_the_pattern_into_two)
{
    using namespace lynx;
    using iss190ht::bar;
    auto s = iss190ht::song();
    s.currentPattern = 2;
    s.patterns[1].events.push_back ({});
    s.patterns[1].events[0].events = { { 0, 0.2f }, { 2 * bar, 0.9f } };
    CHECK (patternblocks::place (s, 1, 0) && patternblocks::place (s, 1, 4 * bar) && patternblocks::place (s, 2, 0));
    patternblocks::setMuted (s, (std::size_t) patternblocks::indexOf (s, { 1, 4 * bar }), true);
    Clip clip; // a clip of P3 keeps playing P3 after the patterns move down
    clip.kind = ClipKind::pattern;
    clip.pattern = 2;
    clip.length = bar;
    s.playlist.push_back (clip);
    const auto before = s;

    // cut P2 (2 bars) at bar 2 beat 4 (7 beats in): the cut is inside the pattern
    CHECK (patternblocks::slice (s, 1, 0) < 0 && patternblocks::slice (s, 1, 2 * bar) < 0 && patternblocks::slice (s, 7, ppq) < 0);
    CHECK (s.patterns.size() == 3);
    const auto cut = bar + 3 * ppq;
    CHECK (patternblocks::slice (s, 1, cut) == 2);
    CHECK (s.patterns.size() == 4);
    const auto& head = s.patterns[1];
    const auto& tail = s.patterns[2];
    // the pattern keeps the first note and the long note shortened at the cut (FL 10 trims it, no second half)
    CHECK (head.notes.size() == 2 && head.notes[0].start == 0);
    CHECK (head.notes[1].start == 6 * ppq && head.notes[1].length == cut - 6 * ppq && cut - 6 * ppq == ppq);
    // the new pattern has what started at or after the cut (nothing here: the long note started before)
    CHECK (tail.notes.empty() && tail.name == "P2 #2");
    // events: the first stays, the later one moves to the new pattern's start
    CHECK (head.events.size() == 1 && head.events[0].events.size() == 1);
    CHECK (tail.events.size() == 1 && tail.events[0].events.size() == 1 && tail.events[0].events[0].tick == 2 * bar - cut);
    // automatic lengths stay automatic: the head plays 2 bars still (7 beats round up), the tail 1 bar
    CHECK (head.lengthTicks == 0 && patternblocks::lengthOf (s, 1) == 2 * bar && patternblocks::lengthOf (s, 2) == bar);
    // every block of P2 gets a block of the new pattern where it ends, muted like it; P3's block and clip follow P3
    CHECK (patternblocks::indexOf (s, { 2, 2 * bar }) >= 0 && patternblocks::indexOf (s, { 2, 6 * bar }) >= 0);
    CHECK (s.blocks[(std::size_t) patternblocks::indexOf (s, { 2, 6 * bar })].muted);
    CHECK (! s.blocks[(std::size_t) patternblocks::indexOf (s, { 2, 2 * bar })].muted);
    CHECK (patternblocks::indexOf (s, { 3, 0 }) >= 0 && s.playlist[0].pattern == 3);
    CHECK (s.currentPattern == 3);
    CHECK (std::string (describeChange (before, s).text) == "Slice pattern");
}

TEST (iss190ht_slice_shares_a_set_length_and_moves_notes)
{
    using namespace lynx;
    using iss190ht::bar;
    auto s = iss190ht::song();
    // P1 with a set length of 1 bar and notes on every beat: cut at beat 3
    s.patterns[0].lengthTicks = bar;
    CHECK (patternblocks::place (s, 0, 0) && patternblocks::place (s, 0, 3 * bar));
    CHECK (patternblocks::slice (s, 0, 2 * ppq) == 1);
    CHECK (s.patterns[0].lengthTicks == 2 * ppq && s.patterns[1].lengthTicks == 2 * ppq);
    CHECK (s.patterns[0].notes.size() == 2 && s.patterns[1].notes.size() == 2);
    CHECK (s.patterns[1].notes[0].start == 0 && s.patterns[1].notes[1].start == ppq);
    // the new blocks start exactly where the shortened ones end (half a bar in)
    CHECK (patternblocks::indexOf (s, { 1, 2 * ppq }) >= 0 && patternblocks::indexOf (s, { 1, 3 * bar + 2 * ppq }) >= 0);
    // other arrangements follow too
    auto a = iss190ht::song();
    a.arrangements.resize (2);
    a.currentArrangement = 0;
    a.arrangements[1].blocks = { { 1, 8 * bar } };
    CHECK (patternblocks::slice (a, 1, bar) == 2);
    CHECK (a.arrangements[1].blocks.size() == 2 && a.arrangements[1].blocks[1].pattern == 2 && a.arrangements[1].blocks[1].start == 9 * bar);
}

TEST (iss190ht_track_inserts_list_layout)
{
    using namespace lynx;
    namespace ti = trackinserts;
    // rows: one per effect, then "add" while a slot is free
    CHECK (ti::itemCount (0) == 1 && ti::itemCount (3) == 4 && ti::itemCount (maxEffectSlots) == maxEffectSlots);
    CHECK (ti::rowsThatFit (12) == 0 && ti::rowsThatFit (13) == 1 && ti::rowsThatFit (43) == 3);
    // 3 effects in 3 rows: the list scrolls by one (the "add" row is the 4th); never a blank tail
    CHECK (ti::clampFirst (5, 3, 3) == 1 && ti::clampFirst (-2, 3, 3) == 0 && ti::clampFirst (1, 1, 3) == 0);
    CHECK (ti::itemAt (3, 0, 3, 0) == 0 && ti::itemAt (3, 0, 3, 14) == 1 && ti::itemAt (3, 0, 3, 38) == 2);
    CHECK (ti::itemAt (3, 1, 3, 38) == ti::addRow && ti::itemAt (3, 0, 3, 39) == -1 && ti::itemAt (3, 0, 3, -1) == -1);
    CHECK (ti::itemAt (maxEffectSlots, 9, 1, 0) == 9 && ti::itemAt (0, 0, 2, 13) == -1);
    // a drop goes before the row whose upper half it is on
    CHECK (ti::dropPosition (3, 0, 4, 2) == 0 && ti::dropPosition (3, 0, 4, 8) == 1 && ti::dropPosition (3, 0, 4, 200) == 3);
    CHECK (ti::dropPosition (3, 1, 3, 2) == 1);
    // a new effect: after the last one, else the first free slot
    std::vector<EffectSlot> fx (2);
    fx[0].row = 0;
    fx[1].row = 4;
    CHECK (ti::nextFreeRow (fx) == 5);
    fx[1].row = maxEffectSlots - 1;
    CHECK (ti::nextFreeRow (fx) == 1);
}

TEST (iss190ht_track_inserts_edits_are_the_mixers)
{
    using namespace lynx;
    // the list's gestures use the Device View's edits on the linked insert (round 183a / 190dr)
    Song s;
    s.mixer.resize (4);
    for (auto k : { EffectKind::parametricEq, EffectKind::compressor, EffectKind::reverb })
        s.mixer[1].effects.push_back (makeEffectSlot (k));
    // drag the reverb above the EQ: the list's drop position 0
    CHECK (detailchain::moveEffectBefore (s, 1, 2, 0) == 0);
    CHECK (s.mixer[1].effects[0].kind == EffectKind::reverb && s.mixer[1].effects[1].kind == EffectKind::parametricEq);
    // Ctrl + drag copies it below the compressor
    CHECK (detailchain::duplicateEffect (s, 1, 0, 3) == 3 && s.mixer[1].effects.size() == 4 && s.mixer[1].effects[3].kind == EffectKind::reverb);
    // onto another linked track: it moves to the end of that insert's chain
    CHECK (detailchain::moveEffectToInsert (s, 1, 1, 2, false) == 0);
    CHECK (s.mixer[1].effects.size() == 3 && s.mixer[2].effects.size() == 1 && s.mixer[2].effects[0].kind == EffectKind::parametricEq);
    // a Browser effect dropped between the first two rows
    CHECK (detailchain::insertEffectAt (s, 1, makeEffectSlot (EffectKind::delay), 1) == 1 && s.mixer[1].effects[1].kind == EffectKind::delay);
}
