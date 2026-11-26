// Round 190rs (0.1.32 quality stage Q5 fixes): the gaps of round 190rq's REA coverage that need a rule in core.

#include "core/NoteEdit.h"
#include "core/SelectionKeep.h"

namespace q5fix190rs
{
struct NoteKey // (as PianoRollComponent::NoteKey)
{
    int channel = 0, key = 0;
    std::int64_t start = 0, length = 0;
    auto operator<=> (const NoteKey&) const = default;
};
inline std::vector<NoteKey> keysOf (const lynx::Pattern& p)
{
    std::vector<NoteKey> out;
    for (const auto& n : p.notes)
        out.push_back ({ n.channel, n.key, n.start, n.length });
    return out;
}
} // namespace q5fix190rs

// G-31: select notes 3 and 4 of 8, move them past note 6 (the Piano roll re-sorts the notes and tracks the selection),
// undo: notes 3 and 4 are selected again, not whatever sits at the moved notes' indices; redo selects the moved ones
TEST (q5fix190rs_selection_kept_by_content_over_undo_of_a_move)
{
    using namespace lynx;
    Pattern original;
    for (int i = 0; i < 8; ++i)
    {
        Note n;
        n.key = 60;
        n.start = i * 96;
        n.length = 48;
        original.notes.push_back (n);
    }
    auto moved = original;
    std::vector<int> tracked { 3, 4 };
    moved.notes[3].start += 336; // 288 -> 624, 384 -> 720: past the note at 576
    moved.notes[4].start += 336;
    noteedit::sortTracking (moved, tracked);
    CHECK (tracked == std::vector<int> ({ 5, 7 })); // (the moved notes now sit at 5 and 7)

    // the old rule (by index) would have selected 5 and 7 of the restored pattern: the notes at 480 and 672
    const auto undone = selkeep::remap (q5fix190rs::keysOf (moved), tracked, q5fix190rs::keysOf (original));
    CHECK (undone == std::vector<int> ({ 3, 4 }));
    const auto redone = selkeep::remap (q5fix190rs::keysOf (original), undone, q5fix190rs::keysOf (moved));
    CHECK (redone == std::vector<int> ({ 5, 7 }));

    // an untouched selection stays; an undone paste (items removed) keeps only the items still there
    CHECK (selkeep::remap (q5fix190rs::keysOf (original), { 0, 6 }, q5fix190rs::keysOf (original)) == std::vector<int> ({ 0, 6 }));
    auto pasted = original;
    Note extra;
    extra.key = 72;
    extra.start = 1000;
    extra.length = 48;
    pasted.notes.push_back (extra);
    CHECK (selkeep::remap (q5fix190rs::keysOf (pasted), { 1, 8 }, q5fix190rs::keysOf (original)) == std::vector<int> ({ 1 }));
    // a transposition of every note undone: each keeps its own selection (paired in order)
    auto up = original;
    for (auto& n : up.notes)
        n.key += 12;
    CHECK (selkeep::remap (q5fix190rs::keysOf (up), { 2, 5 }, q5fix190rs::keysOf (original)) == std::vector<int> ({ 2, 5 }));
    // duplicates: two equal notes, one selected, one selected after
    auto twins = original;
    twins.notes.push_back (twins.notes[2]);
    const auto twinKeys = q5fix190rs::keysOf (twins);
    CHECK (selkeep::remap (twinKeys, { 8 }, twinKeys).size() == 1);
}

// G-31, the Playlist twin: clips keyed by track, kind, source, start and length
TEST (q5fix190rs_playlist_selection_kept_by_content_over_undo_of_a_move)
{
    struct ClipKey
    {
        int track = 0, kind = 0, source = 0;
        std::int64_t start = 0, length = 0;
        auto operator<=> (const ClipKey&) const = default;
    };
    std::vector<ClipKey> before { { 0, 0, 1, 0, 384 }, { 0, 0, 1, 384, 384 }, { 1, 0, 2, 0, 384 }, { 2, 0, 3, 0, 384 } };
    // clip 1 (track 0, bar 2) dragged to track 3 and re-sorted to the end; then undone
    std::vector<ClipKey> moved { before[0], before[2], before[3], { 3, 0, 1, 384, 384 } };
    CHECK (lynx::selkeep::remap (moved, { 3 }, before) == std::vector<int> ({ 1 }));
    CHECK (lynx::selkeep::remap (before, { 1 }, moved) == std::vector<int> ({ 3 }));
}

// G-01: where an unassigned Playlist track's clips are heard (the header hint's facts). The starter project: Kick,
// Clap, Hat, Snare and Synth on inserts 1-5, the beat on Playlist track 1
#include "core/PlaylistOps.h"
TEST (q5fix190rs_route_summary_of_an_unassigned_track)
{
    using namespace lynx;
    Song s;
    s.channels.resize (3);
    s.channels[0].mixerTrack = 1;
    s.channels[1].mixerTrack = 2;
    s.channels[2].mixerTrack = 0; // the Master
    s.patterns.resize (2);
    Note kick;
    kick.channel = 0;
    s.patterns[0].notes.push_back (kick);
    for (int ch : { 0, 1, 2 })
    {
        Note n;
        n.channel = ch;
        s.patterns[1].notes.push_back (n);
    }
    // nothing on track 4: a lane for clips
    auto r = trackmodes::routeSummary (s, 4);
    CHECK (! r.clips && r.channels.empty() && r.inserts.empty());
    // one pattern of the Kick only on track 0 (twice: counted once)
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.track = 0;
    s.playlist.push_back (c);
    c.start = 384;
    s.playlist.push_back (c);
    r = trackmodes::routeSummary (s, 0);
    CHECK (r.clips && r.channels == std::vector<int> ({ 0 }) && r.inserts == std::vector<int> ({ 1 }));
    // the three channels on track 1: inserts Master, 1, 2
    c.pattern = 1;
    c.track = 1;
    s.playlist.push_back (c);
    r = trackmodes::routeSummary (s, 1);
    CHECK (r.channels == std::vector<int> ({ 0, 1, 2 }) && r.inserts == std::vector<int> ({ 0, 1, 2 }));
    // an audio clip plays its own channel; an automation clip alone is not a sound clip
    Clip a;
    a.kind = ClipKind::audio;
    a.channel = 1;
    a.track = 2;
    s.playlist.push_back (a);
    CHECK (trackmodes::routeSummary (s, 2).inserts == std::vector<int> ({ 2 }));
    Clip au;
    au.kind = ClipKind::automation;
    au.track = 3;
    s.playlist.push_back (au);
    CHECK (! trackmodes::routeSummary (s, 3).clips);
}

// Owner decision D-4: in the Pattern layout Ctrl+N is FL 2026's "Save new version"; New project keeps no key (FL's File ›
// New has none); the Clip-launch and Simple layouts keep LYNX's Ctrl+N = New project. A key the user set stays.
#include "core/KeyMap.h"
TEST (q5fix190rs_ctrl_n_is_save_new_version_in_the_pattern_layout)
{
    using namespace lynx::keys;
    const Chord ctrlN { 'N', ctrl }, ctrlAltS { 'S', (std::uint8_t) (ctrl | alt) };
    KeyMap m;
    CHECK (m.find (ctrlN, Context::global, false) == "file.new");
    m.setFlFileKeys (true);
    CHECK (m.find (ctrlN, Context::global, false) == "file.saveNewVersion");
    CHECK (m.find (ctrlAltS, Context::global, false) == "file.saveNewVersion"); // (LYNX's key stays too)
    CHECK (m.chordsFor ("file.new").empty() && m.isDefault ("file.new") && m.isDefault ("file.saveNewVersion"));
    CHECK (m.overridesJson() == "{}\n" && m.conflicts().empty());
    m.setFlFileKeys (false);
    CHECK (m.find (ctrlN, Context::global, false) == "file.new" && m.chordsFor ("file.saveNewVersion") == std::vector<Chord> ({ ctrlAltS }));

    // the user gave New project keys of his own (Ctrl+N among them): they stay, and Save new version doesn't take Ctrl+N
    KeyMap own;
    CHECK (own.loadJson (R"({"file.new": ["Ctrl+N", "Ctrl+Shift+N"]})"));
    own.setFlFileKeys (true);
    CHECK (own.chordsFor ("file.new").size() == 2 && own.find (ctrlN, Context::global, false) == "file.new");
    CHECK (own.chordsFor ("file.saveNewVersion") == std::vector<Chord> ({ ctrlAltS }));

    // Ctrl+N given to another action: that one keeps it; New project loses its default key in the Pattern layout
    KeyMap other;
    CHECK (other.loadJson (R"({"file.saveAs": ["Ctrl+N"]})"));
    other.setFlFileKeys (true);
    CHECK (other.find (ctrlN, Context::global, false) == "file.saveAs" && other.chordsFor ("file.saveNewVersion") == std::vector<Chord> ({ ctrlAltS }));

    // a choice made in the Pattern layout (Save new version without Ctrl+N) survives keys.json and the next start
    KeyMap pattern;
    pattern.setFlFileKeys (true);
    pattern.setChords ("file.saveNewVersion", { ctrlAltS });
    KeyMap next;
    CHECK (next.loadJson (pattern.overridesJson()));
    next.setFlFileKeys (true);
    CHECK (next.chordsFor ("file.saveNewVersion") == std::vector<Chord> ({ ctrlAltS }) && next.find (ctrlN, Context::global, false).empty());
    // the Shortcut manager's "Reset to default" in the Pattern layout gives FL's keys back
    CHECK (next.defaultsFor ("file.saveNewVersion") == std::vector<Chord> ({ ctrlN, ctrlAltS }) && next.defaultsFor ("file.new").empty());
}

// G-30: the Clip-launch track header's Duplicate and Assign Track Color to Clips (Live's items over LYNX's operations)
#include "core/ClipLauncher.h"
#include "core/SessionOps.h"
TEST (q5fix190rs_clip_launch_duplicate_track_and_track_colour)
{
    using namespace lynx;
    Song s;
    s.channels.resize (1);
    s.channels[0].name = "Bass";
    s.patterns.resize (1);
    Note n;
    n.channel = 0;
    n.length = 96;
    s.patterns[0].notes.push_back (n);
    CHECK (trackmodes::makeInstrumentTrack (s, 0, 0));
    launcher::ensureGrid (s);
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 0;
    c.length = 384;
    CHECK (launcher::setClip (s, 0, 1, c) >= 0);

    const auto at = session::duplicateTrack (s, 0);
    CHECK (at == 1);
    const auto newChannel = s.playlistTracks[1].channel;
    CHECK (s.playlistTracks[1].mode == PlaylistTrackMode::instrument && newChannel > 0 && newChannel < (int) s.channels.size());
    const auto* copy = launcher::clipAt (*s.clipGrid, 1, 1);
    CHECK (copy != nullptr && copy->pattern != 0 && copy->pattern < (int) s.patterns.size());
    if (copy != nullptr && copy->pattern < (int) s.patterns.size())
        CHECK (! s.patterns[(std::size_t) copy->pattern].notes.empty() && s.patterns[(std::size_t) copy->pattern].notes.front().channel == newChannel);
    const auto* original = launcher::clipAt (*s.clipGrid, 0, 1);
    CHECK (original != nullptr && original->pattern == 0 && s.patterns[0].notes.front().channel == 0); // (the original plays on)

    s.playlistTracks[1].colour = 0xff336699;
    CHECK (session::assignTrackColourToClips (s, 1) == 1);
    CHECK (launcher::settingsAt (*s.clipGrid, 1, 1) != nullptr && launcher::settingsAt (*s.clipGrid, 1, 1)->colour == 0xff336699u);
    CHECK (session::assignTrackColourToClips (s, 1) == 0); // (already)
}
