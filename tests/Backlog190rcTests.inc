// Round 190rc (0.1.32 quality release): #285, #286, the ghost notes of Track-only / Simple (Discord 11-24).

#include "core/EditorPolish.h"
#include "core/KeyMap.h"
#include "core/PlaylistOps.h"

// #285: "Cut clips at the song position" — the selected clips that cross it, else every clip that crosses it
TEST (backlog190rc_285_split_at_playhead)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 4 * ppq;
    std::vector<Clip> clips;
    for (int t = 0; t < 3; ++t)
    {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = t;
        c.track = t;
        c.start = t == 2 ? 2 * bar : 0; // (track 3's clip starts at the position: not cut)
        c.length = 4 * bar;
        clips.push_back (c);
    }
    // the selection: clip 1 alone
    CHECK ((splitat::clipsAt (clips, { 1 }, 2 * bar) == std::vector<int> { 1 }));
    // a selection that doesn't cross the position (clip 2 starts there): every clip that crosses it
    CHECK ((splitat::clipsAt (clips, { 2 }, 2 * bar) == std::vector<int> { 0, 1 }));
    CHECK ((splitat::clipsAt (clips, {}, 2 * bar) == std::vector<int> { 0, 1 }));
    CHECK (splitat::clipsAt (clips, {}, 0).empty()); // (at a clip's start nothing is cut)
    CHECK (splitat::clipsAt (clips, {}, 9 * bar).empty());
    auto cut = clips;
    const auto pieces = splitat::split (cut, { 0, 1 }, 2 * bar);
    CHECK ((pieces == std::vector<int> { 0, 3, 1, 4 }));
    CHECK (cut.size() == 5u);
    CHECK (cut[0].start == 0 && cut[0].length == 2 * bar && cut[3].start == 2 * bar && cut[3].length == 2 * bar && cut[3].offset == 2 * bar);
    CHECK (cut[3].track == 0 && cut[4].track == 1 && cut[2].length == 4 * bar);
    // the key map: an action with no default key (FL has none; Live's Ctrl+E stays the Clip-launch layout's)
    const auto* a = keys::findAction ("playlist.splitAtPlayhead");
    CHECK (a != nullptr && a->defaults.empty() && a->context == keys::Context::playlist);
}

// #286: "Open the project's folder" — an action with no default key
TEST (backlog190rc_286_open_project_folder_action)
{
    using namespace lynx;
    const auto* a = keys::findAction ("file.openProjectFolder");
    CHECK (a != nullptr && a->defaults.empty() && a->context == keys::Context::global);
}

// Discord 11-24 (sidthenugget): in Track-only mode / Simple a clip holds one instrument, so the ghost notes are the other
// tracks' notes that play while the edited clip does
TEST (backlog190rc_ghosts_from_other_tracks)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 4 * ppq;
    Song s;
    s.channels.resize (3);
    s.patterns.resize (3);
    for (int p = 0; p < 3; ++p)
    {
        Note n;
        n.channel = p;
        n.key = 60 + p;
        n.start = p * ppq;
        n.length = ppq;
        s.patterns[(std::size_t) p].notes.push_back (n);
        Note late = n;
        late.start = 3 * bar; // (beyond the edited clip's end: not a ghost)
        s.patterns[(std::size_t) p].notes.push_back (late);
        s.patterns[(std::size_t) p].lengthTicks = 4 * bar;
    }
    // pattern 0 (channel 0) at bar 2, two bars long; pattern 1 (channel 1) from bar 1; pattern 2 (channel 2) muted
    const auto addClip = [&s] (int pattern, int track, std::int64_t start, std::int64_t length, bool muted) {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = pattern;
        c.track = track;
        c.start = start;
        c.length = length;
        c.muted = muted;
        s.playlist.push_back (c);
    };
    addClip (0, 0, bar, 2 * bar, false);
    addClip (1, 1, 0, 4 * bar, false);
    addClip (2, 2, 0, 4 * bar, true);
    const auto others = polish::clipGhostNotes (s, 0, 0, 2000, true);
    // channel 1's note at beat 2 of pattern 1 plays at song tick ppq (before the clip): not there; its bar-4 note neither
    // (outside); the pattern plays from tick 0, so within bar 2-3 nothing... add one that does
    CHECK (others.empty());
    Note mid;
    mid.channel = 1;
    mid.key = 70;
    mid.start = bar + ppq; // song tick bar + ppq = the edited pattern's tick ppq
    mid.length = ppq;
    s.patterns[1].notes.push_back (mid);
    const auto again = polish::clipGhostNotes (s, 0, 0, 2000, true);
    CHECK (again.size() == 1u && again[0].channel == 1 && again[0].key == 70 && again[0].start == ppq);
    // FL's own (the same channel in other clips) leaves the other channels out
    for (const auto& n : polish::clipGhostNotes (s, 0, 0))
        CHECK (n.channel == 0);
}
