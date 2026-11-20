// Round 190ne (LYNX 0.1.31): track clips, "Clips own their notes" on FL's instrument tracks (core/TrackClips.h; design A
// of round 190nd). The .lynx round trip, the engine's automatic deletion in one undo step and the copy for 0.1.28 are
// checked in the app's --selftest ("trackclips190ne"): they need the engine and ProjectFile.

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/EditorPolish.h"
#include "core/FlpExport.h"
#include "core/LayoutStyle.h"
#include "core/RackOps.h"
#include "core/SessionOps.h"
#include "core/TrackClips.h"

namespace trackclips190ne
{
// Patterns 0 ("Drums", ordinary) and 1 ("Chords", ordinary); channel 0 "Lead" on instrument track 1 with its clips owning
// their notes; no clip yet
inline lynx::Song song()
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (8);
    s.patterns.resize (2);
    s.patterns[0].name = "Drums";
    s.patterns[1].name = "Chords";
    ChannelDef lead;
    lead.name = "Lead";
    lead.colour = 0xFFE5383Bu;
    s.channels.push_back (lead);
    trackmodes::makeInstrumentTrack (s, 1, 0);
    trackclips::setOwnClips (s, 1, true);
    return s;
}

inline lynx::Clip clipOf (int pattern, int track, std::int64_t start, std::int64_t length = 4 * lynx::ppq)
{
    lynx::Clip c;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}

inline lynx::Note noteOf (int channel, int key, std::int64_t start)
{
    lynx::Note n;
    n.channel = channel;
    n.key = key;
    n.start = start;
    n.length = lynx::ppq / 2;
    return n;
}

inline int patternsNamed (const lynx::Song& s, const std::string& name)
{
    return (int) std::count_if (s.patterns.begin(), s.patterns.end(), [&name] (const lynx::Pattern& p) { return p.name == name; });
}
} // namespace trackclips190ne

// The switch's default follows the layout a new instrument track is made in (owner decision: off in the Pattern layout,
// FL's own instrument track; on in Clip-launch and Simple); an existing instrument track keeps its switch
TEST (trackclips190ne_switch_default_per_layout)
{
    using namespace lynx;
    const auto before = layoutstyle::current();
    Song s;
    s.mixer = Song::defaultMixer (8);
    for (int c = 0; c < 4; ++c)
        s.channels.emplace_back().name = "Synth " + std::to_string (c + 1);
    layoutstyle::setCurrent (layoutstyle::Style::pattern);
    CHECK (trackmodes::makeInstrumentTrack (s, 0, 0));
    CHECK (! s.playlistTracks[0].ownClips && ! trackclips::owns (s, 0));
    layoutstyle::setCurrent (layoutstyle::Style::clipLaunch);
    CHECK (trackmodes::makeInstrumentTrack (s, 1, 1));
    CHECK (s.playlistTracks[1].ownClips && trackclips::owns (s, 1));
    layoutstyle::setCurrent (layoutstyle::Style::simple);
    CHECK (trackmodes::makeInstrumentTrack (s, 2, 2));
    CHECK (trackclips::owns (s, 2));
    // another channel on an instrument track: the track keeps its switch (Pattern layout or not)
    layoutstyle::setCurrent (layoutstyle::Style::pattern);
    CHECK (trackmodes::makeInstrumentTrack (s, 2, 3));
    CHECK (trackclips::owns (s, 2));
    // only an instrument track owns clips: unassigned, the switch means nothing
    trackmodes::unassign (s, 2);
    CHECK (! trackclips::owns (s, 2));
    layoutstyle::setCurrent (before);
}

// A new clip: an empty track clip named and coloured after the track, as long as asked; out of the pattern selector
// unless shown; F4 / Numpad +/- / the selector's wheel never land on one
TEST (trackclips190ne_new_clip_and_the_pattern_selector)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    const auto a = trackclips::newClip (s, 1, 2 * s.barTicks());
    const auto b = trackclips::newClip (s, 1, s.barTicks());
    CHECK (a == 2 && b == 3);
    CHECK (s.patterns[2].name == "Lead \xC2\xB7 clip 1" && s.patterns[3].name == "Lead \xC2\xB7 clip 2");
    CHECK (s.patterns[2].trackClip && s.patterns[2].colour == 0xFFE5383Bu && s.patterns[2].lengthTicks == 2 * s.barTicks());
    CHECK (s.patterns[2].notes.empty() && s.patterns[2].uid == 0);
    CHECK (trackclips::count (s) == 2 && trackclips::isTrackClip (s, 3) && ! trackclips::isTrackClip (s, 1) && ! trackclips::isTrackClip (s, 9));
    // a track with its own name names its clips
    s.playlistTracks[1].name = "Hook";
    CHECK (trackclips::newClipName (s, 1) == "Hook \xC2\xB7 clip 1");
    CHECK (trackclips::trackLabel (s, 5) == "Track 6");

    // the selector lists the ordinary patterns only, unless "shown"
    CHECK (trackclips::listedInSelector (s, 0) && trackclips::listedInSelector (s, 1) && ! trackclips::listedInSelector (s, 2));
    s.currentPattern = 1;
    CHECK (stepPattern (s, 1) == 1 && stepPattern (s, -1) == 0);
    trackclips::setShownInSelector (true);
    CHECK (trackclips::listedInSelector (s, 2) && stepPattern (s, 1) == 2 && stepPattern (s, 2) == 3);
    trackclips::setShownInSelector (false);
    // a track clip in the middle of the list is stepped over
    s.patterns.push_back ({});
    s.patterns.back().name = "Bass";
    s.currentPattern = 1;
    CHECK (stepPattern (s, 1) == 4);
    s.currentPattern = 4;
    CHECK (stepPattern (s, -1) == 1);
    // the selector's step past the last listed pattern: a new one only when "Bass" has notes (track clips don't count)
    CHECK (! stepCreatesPattern (s, 1)); // ("Bass" is empty: it stays)
    s.patterns[4].notes.push_back (trackclips190ne::noteOf (0, 60, 0));
    CHECK (stepCreatesPattern (s, 1));
    // F4 / Shift+F4: empty patterns, never a track's clip
    s.currentPattern = 4;
    CHECK (findNextEmptyPattern (s, 4) == 0);
    s.patterns[0].notes.push_back (trackclips190ne::noteOf (0, 36, 0));
    s.patterns[1].notes.push_back (trackclips190ne::noteOf (0, 48, 0));
    CHECK (findNextEmptyPattern (s, 4) == -1);
    const auto patterns = s.patterns.size();
    CHECK (gotoFirstEmptyPattern (s) == (int) patterns && s.patterns.back().name == "Pattern 1" && ! s.patterns.back().trackClip);
}

// Copies of a track clip are unique by default: one new pattern per source (two linked copies stay linked to each other,
// not to the original); ordinary patterns keep FL's sharing; a linked copy is a plain copy of the clip
TEST (trackclips190ne_unique_and_linked_copies)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    const auto a = trackclips::newClip (s, 1, 4 * ppq);
    s.patterns[(std::size_t) a].notes.push_back (trackclips190ne::noteOf (0, 60, 0));
    s.playlist = { trackclips190ne::clipOf (a, 1, 0), trackclips190ne::clipOf (a, 1, 4 * ppq), trackclips190ne::clipOf (0, 0, 0) };
    // copies of all three (Shift+drag / Ctrl+B / paint: appended, then made unique)
    for (int i = 0; i < 3; ++i)
    {
        auto c = s.playlist[(std::size_t) i];
        c.start += 8 * ppq;
        s.playlist.push_back (c);
    }
    CHECK (trackclips::makeCopiesUnique (s, std::vector<int> { 3, 4, 5 }) == 1);
    CHECK (s.patterns.size() == 4);
    CHECK (s.playlist[3].pattern == 3 && s.playlist[4].pattern == 3 && s.playlist[5].pattern == 0); // (the copies of a share one)
    CHECK (s.playlist[0].pattern == a && s.playlist[1].pattern == a);
    CHECK (s.patterns[3].trackClip && s.patterns[3].notes == s.patterns[(std::size_t) a].notes && s.patterns[3].name == "Lead \xC2\xB7 clip 2");
    // editing the copy leaves the original alone
    s.patterns[3].notes.front().key = 72;
    CHECK (s.patterns[(std::size_t) a].notes.front().key == 60);
    // a linked copy: the same pattern (Ctrl+Alt+B, Shift+L+drag); the uses count it
    auto linked = s.playlist[0];
    linked.start = 16 * ppq;
    s.playlist.push_back (linked);
    CHECK (trackclips::uses (s)[(std::size_t) a] == 3 && trackclips::uses (s)[3] == 2 && trackclips::uses (s)[0] == 2);
    // FL's Make unique on a track clip: a new clip of its track
    polish::makeClipsUnique (s, { (int) s.playlist.size() - 1 });
    CHECK (s.patterns.back().trackClip && s.patterns.back().name == "Lead \xC2\xB7 clip 3" && s.patterns.back().uid == 0);
    // clips not in a list yet (a paste onto the grid): each track clip gets a copy
    std::vector<Clip> loose { trackclips190ne::clipOf (a, 1, 0), trackclips190ne::clipOf (1, 1, 0) };
    const auto count = s.patterns.size();
    CHECK (trackclips::makeCopiesUnique (s, loose) == 1);
    CHECK (s.patterns.size() == count + 1 && loose[0].pattern == (int) count && loose[1].pattern == 1);
}

// The last clip of a track clip goes: the track clip goes with it (in AudioEngine::editSong's step, --selftest); ordinary
// patterns stay (FL); any clip list, a block or a Loop Starter channel counts as a use; the current pattern follows
TEST (trackclips190ne_unused_track_clips_are_deleted)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    const auto a = trackclips::newClip (s, 1, 4 * ppq); // 2
    const auto b = trackclips::newClip (s, 1, 4 * ppq); // 3
    const auto c = trackclips::newClip (s, 1, 4 * ppq); // 4
    s.patterns.push_back ({});
    s.patterns.back().name = "Late"; // 5, ordinary
    s.playlist = { trackclips190ne::clipOf (a, 1, 0), trackclips190ne::clipOf (c, 1, 4 * ppq), trackclips190ne::clipOf (5, 2, 0) };
    s.currentPattern = 4;
    // nothing unused but b (patterns 0 / 1 are ordinary: they stay without clips)
    std::vector<int> removed;
    CHECK (trackclips::collect (s, &removed) == 1 && removed == std::vector<int> { b });
    CHECK (s.patterns.size() == 5 && s.patterns[3].name == "Lead \xC2\xB7 clip 3" && s.patterns[4].name == "Late");
    CHECK (s.playlist[1].pattern == 3 && s.playlist[2].pattern == 4 && s.currentPattern == 3); // (indices follow)
    // a clip on the grid counts as a use
    launcher::setClip (s, 1, 0, trackclips190ne::clipOf (3, 1, 0));
    s.playlist.erase (s.playlist.begin() + 1);
    CHECK (trackclips::collect (s) == 0);
    launcher::clearClip (s, 1, 0);
    CHECK (trackclips::collect (s) == 1 && s.patterns.size() == 4);
    // the current pattern was the one deleted: it stays in range
    CHECK (s.currentPattern >= 0 && s.currentPattern < (int) s.patterns.size());
    // a project without track clips: nothing happens
    auto plain = trackclips190ne::song();
    plain.playlistTracks[1].ownClips = false;
    const auto copy = plain;
    CHECK (trackclips::collect (plain) == 0 && plain.patterns == copy.patterns && plain.playlist == copy.playlist);
}

// Cut + paste: the clipboard carries the track clip (cutting its last clip deletes it), and a paste brings a unique copy
TEST (trackclips190ne_clipboard_carries_track_clips)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    const auto a = trackclips::newClip (s, 1, 4 * ppq);
    s.patterns[(std::size_t) a].notes.push_back (trackclips190ne::noteOf (0, 64, ppq));
    s.playlist = { trackclips190ne::clipOf (a, 1, 0), trackclips190ne::clipOf (0, 0, 0) };
    const auto clipboard = s.playlist; // Ctrl+X
    const auto carried = trackclips::carry (s, clipboard);
    CHECK (carried.patterns.size() == 1 && carried.patterns[0].first == a);
    s.playlist.clear();
    CHECK (trackclips::collect (s) == 1 && s.patterns.size() == 2);
    auto pasted = clipboard; // Ctrl+V
    trackclips::materialize (s, pasted, carried);
    CHECK (s.patterns.size() == 3 && pasted[0].pattern == 2 && pasted[1].pattern == 0);
    CHECK (s.patterns[2].trackClip && s.patterns[2].notes.size() == 1 && s.patterns[2].notes[0].key == 64);
    // two clips of one source pasted together share one copy
    std::vector<Clip> two { trackclips190ne::clipOf (a, 1, 0), trackclips190ne::clipOf (a, 1, 4 * ppq) };
    trackclips::materialize (s, two, carried);
    CHECK (two[0].pattern == 3 && two[1].pattern == 3 && s.patterns.size() == 4);
}

// The switch on: the track's patterns that only it plays (all notes its channel's) become its clips; off: they come back
// into the pattern list. A pattern shared with another track, a block, or several instruments stays ordinary.
TEST (trackclips190ne_switch_adopts_and_releases)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    trackclips::setOwnClips (s, 1, false);
    s.channels.emplace_back().name = "Pad";
    s.patterns.resize (5);
    s.patterns[2].notes = { trackclips190ne::noteOf (0, 60, 0) };                                   // Lead only, track 1 only
    s.patterns[3].notes = { trackclips190ne::noteOf (0, 60, 0), trackclips190ne::noteOf (1, 50, 0) }; // two instruments
    s.patterns[4].notes = { trackclips190ne::noteOf (0, 62, 0) };                                   // also on track 3
    s.playlist = { trackclips190ne::clipOf (2, 1, 0), trackclips190ne::clipOf (2, 1, 4 * ppq), trackclips190ne::clipOf (3, 1, 0),
                   trackclips190ne::clipOf (4, 1, 0), trackclips190ne::clipOf (4, 3, 0), trackclips190ne::clipOf (0, 0, 0) };
    trackclips::setOwnClips (s, 1, true);
    CHECK (s.playlistTracks[1].ownClips && trackclips::owns (s, 1));
    CHECK (s.patterns[2].trackClip && ! s.patterns[3].trackClip && ! s.patterns[4].trackClip && ! s.patterns[0].trackClip);
    CHECK (trackclips::ownerTrack (s, 2) == 1 && trackclips::ownerTracks (s)[4] == 1 && trackclips::ownerTracks (s)[1] == -1);
    // a pattern block makes a pattern shared
    s.patterns[2].trackClip = false;
    s.blocks.push_back ({ 2, 0, false });
    CHECK (trackclips::adopt (s, 1) == 0);
    s.blocks.clear();
    CHECK (trackclips::adopt (s, 1) == 1);
    trackclips::setOwnClips (s, 1, false);
    CHECK (! s.patterns[2].trackClip && trackclips::count (s) == 0);
}

// Export to FL Studio: identical track clips are merged (3 identical + 2 different -> 3 patterns), every clip following;
// ordinary patterns are never merged; more than 999 patterns are reported
TEST (trackclips190ne_flp_export_merges_identical_track_clips)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    s.patterns[0].notes = { trackclips190ne::noteOf (0, 36, 0) };
    s.patterns[1].notes = s.patterns[0].notes; // (ordinary twins: FL keeps both)
    std::vector<int> made;
    for (int k = 0; k < 5; ++k)
    {
        made.push_back (trackclips::newClip (s, 1, 4 * ppq));
        s.patterns.back().notes = { trackclips190ne::noteOf (0, k < 3 ? 60 : 60 + k, 0) };
        s.playlist.push_back (trackclips190ne::clipOf (made.back(), 1, k * 4 * ppq));
    }
    auto merged = s;
    CHECK (trackclips::mergeIdentical (merged) == 2);
    CHECK (merged.patterns.size() == 5); // Drums, Chords, clip 1 (x3), clip 4, clip 5
    CHECK (merged.playlist[0].pattern == 2 && merged.playlist[1].pattern == 2 && merged.playlist[2].pattern == 2);
    CHECK (merged.playlist[3].pattern == 3 && merged.playlist[4].pattern == 4);
    CHECK (merged.patterns[0].notes == merged.patterns[1].notes); // (both ordinary twins stay)
    // the export writes the merged song: two patterns fewer than without the flags
    flpexport::Report report, plainReport;
    const auto flp = flp::parseProject (flpexport::exportFlp (s, {}, report));
    auto plain = s;
    for (auto& p : plain.patterns)
        p.trackClip = false;
    const auto flpPlain = flp::parseProject (flpexport::exportFlp (plain, {}, plainReport));
    CHECK (flp.patterns.size() + 2 == flpPlain.patterns.size());
    CHECK (std::none_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.find ("999") != std::string::npos; }));
    // over FL's limit: a warning
    auto big = trackclips190ne::song();
    for (int k = 0; k < 1000; ++k)
        big.patterns.emplace_back().name = "P" + std::to_string (k);
    flpexport::Report bigReport;
    (void) flpexport::toProject (big, {}, bigReport);
    CHECK (std::any_of (bigReport.notes.begin(), bigReport.notes.end(), [] (const std::string& n) { return n.find ("999 FL Studio can hold") != std::string::npos; }));
}

// A Live set: its MIDI tracks become instrument tracks whose clips own their notes, every MIDI clip a track clip, so the
// pattern list stays clean; the export to Live and back keeps it so
TEST (trackclips190ne_als_import_marks_clips_and_round_trips)
{
    using namespace lynx;
    FlpImportReport report;
    std::string error;
    const auto song = importAls (als175a::bytesOf (als175a::richSet()), als175a::options (&report), &error);
    CHECK (song.has_value());
    if (! song)
        return;
    int keys = -1;
    for (int t = 0; t < (int) song->playlistTracks.size(); ++t)
        if (song->playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::instrument && ! song->playlistTracks[(std::size_t) t].takeLane)
            keys = t;
    CHECK (keys >= 0 && trackclips::owns (*song, keys));
    int clips = 0;
    for (const auto& c : song->playlist)
        if (c.kind == ClipKind::pattern)
        {
            ++clips;
            CHECK (trackclips::isTrackClip (*song, c.pattern) && c.track == keys);
        }
    CHECK (clips >= 2 && trackclips::count (*song) >= 2);
    const auto out = exportAls (*song);
    const auto back = importAls (out.bytes, als175a::options (&report), &error);
    CHECK (back.has_value());
    if (! back)
        return;
    CHECK (trackclips::count (*back) == trackclips::count (*song));
    int backKeys = 0;
    for (int t = 0; t < (int) back->playlistTracks.size(); ++t)
        backKeys += trackclips::owns (*back, t) ? 1 : 0;
    CHECK (backKeys >= 1);
}

// The clip grid: a slot's own clip (recorded, empty) is a track clip; a copy of it (paste, duplicate, a drag with the copy
// modifier, to the Playlist) is unique; a drag moves it with its pattern; a cut + paste works after the pattern is gone
TEST (trackclips190ne_grid_slots_own_their_clips)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    const auto p = session::insertEmptyClip (s, 1, 0);
    CHECK (p == 2 && s.patterns[2].trackClip);
    s.patterns[2].notes = { trackclips190ne::noteOf (0, 60, 0) };
    CHECK (session::duplicate (s, 1, 0) == 1);
    const auto* dup = launcher::clipAt (*s.clipGrid, 1, 1);
    CHECK (dup != nullptr && dup->pattern == 3 && s.patterns[3].notes == s.patterns[2].notes && s.patterns[3].trackClip);
    // a drag moves the clip with its pattern; with the copy modifier the copy is unique
    CHECK (session::moveClip (s, 1, 1, 2, 1));
    CHECK (launcher::clipAt (*s.clipGrid, 2, 1)->pattern == 3 && s.patterns.size() == 4);
    CHECK (session::moveClip (s, 2, 1, 2, 2, true));
    CHECK (launcher::clipAt (*s.clipGrid, 2, 2)->pattern == 4 && s.patterns.size() == 5);
    // cut: the pattern goes with the slot's clip (collect), the paste brings a copy back
    auto cut = session::cut (s, 2, 2);
    CHECK (cut.has_value() && cut->trackClip.has_value());
    CHECK (trackclips::collect (s) == 1 && s.patterns.size() == 4);
    CHECK (session::paste (s, 3, 0, *cut));
    const auto* pasted = launcher::clipAt (*s.clipGrid, 3, 0);
    CHECK (pasted != nullptr && pasted->pattern == 4 && s.patterns[4].notes == s.patterns[2].notes);
    // to the Playlist: a copy of its own; an ordinary pattern placed in a slot stays shared (FL's pattern)
    CHECK (session::toPlaylist (s, 1, 0, 1, 0));
    CHECK (s.playlist.back().pattern == 5 && s.patterns.size() == 6);
    CHECK (session::putPattern (s, 4, 0, 0) && launcher::clipAt (*s.clipGrid, 4, 0)->pattern == 0);
    CHECK (session::duplicate (s, 4, 0) == 1 && launcher::clipAt (*s.clipGrid, 4, 1)->pattern == 0);
}

// Bus tracks: an audio track whose insert other inserts send into (the header's bus mark and hint)
TEST (trackclips190ne_bus_tracks)
{
    using namespace lynx;
    auto s = trackclips190ne::song();
    CHECK (trackmodes::linkToMixerTrack (s, 4, 6));
    CHECK (! trackclips::isBusTrack (s, 4) && trackclips::busSources (s, 4) == 0);
    s.setRoute (2, 6, true);
    s.setRoute (3, 6, true);
    CHECK (trackclips::isBusTrack (s, 4) && trackclips::busSources (s, 4) == 2);
    CHECK (! trackclips::isBusTrack (s, 1)); // (an instrument track is no bus track)
}
