// Round 168f: FL's Playlist track tools — Lock to content / to this size, Auto name, Send pattern instances here, the
// Tools macros, Merge pattern clips (Ctrl+G), Clone tracks (Ctrl+Shift+B), Make unique (Ctrl+Shift+V), stretching with
// "Create variants" (docs/analise/REA_rodada168f.md)

namespace pl168f
{
lynx::Song songWithPatterns (int count)
{
    auto song = lynx::Song::makeDefault();
    song.playlist.clear();
    song.playlistTracks.clear();
    song.patterns.resize ((std::size_t) count);
    for (int i = 0; i < count; ++i)
    {
        song.patterns[(std::size_t) i].name = "P" + std::to_string (i + 1);
        song.patterns[(std::size_t) i].notes.clear();
        song.patterns[(std::size_t) i].colour = 0xFF000000u + (std::uint32_t) i;
    }
    return song;
}

lynx::Clip patternClip (int pattern, int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::pattern;
    c.pattern = pattern;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}

int addAudioChannel (lynx::Song& song, const std::string& name)
{
    lynx::ChannelDef ch;
    ch.name = name;
    ch.instrument = lynx::InstrumentKind::audioClip;
    song.channels.push_back (ch);
    return (int) song.channels.size() - 1;
}

lynx::Clip audioClip (int channel, int track, std::int64_t start, std::int64_t length)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::audio;
    c.channel = channel;
    c.pattern = 0;
    c.track = track;
    c.start = start;
    c.length = length;
    return c;
}
} // namespace pl168f

TEST (pl168f_lock_to_content_paints_the_dominant_clip)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (3);
    // track 2 holds pattern 1 twice (one bar) and pattern 2 once
    song.playlist.push_back (pl168f::patternClip (1, 2, 0, bar));
    song.playlist.push_back (pl168f::patternClip (1, 2, bar, bar));
    song.playlist.push_back (pl168f::patternClip (2, 2, 2 * bar, 2 * bar));
    CHECK (pltracks::dominantClip (song, 2) == 0);
    CHECK (pltracks::dominantClip (song, 5) == -1);

    // not locked: painting pattern 0 stays pattern 0
    auto c = pl168f::patternClip (0, 2, 5 * bar, bar / 2);
    CHECK (! pltracks::applyLockToContent (song, c) && c.pattern == 0);

    // locked: an unnamed track is auto-named after what it plays most (FL's FUN_012e7a90 → FUN_012ef490)
    pltracks::toggleLockToContent (song, { 2 });
    CHECK (song.playlistTracks.size() > 2 && song.playlistTracks[2].lockedToContent);
    CHECK (song.playlistTracks[2].name == "P2" && song.playlistTracks[2].colour == song.patterns[1].colour);
    // a source the track doesn't hold becomes the dominant clip (length too), at the click
    CHECK (pltracks::applyLockToContent (song, c));
    CHECK (c.pattern == 1 && c.length == bar && c.start == 5 * bar && c.track == 2);
    // a source it holds stays as painted
    auto other = pl168f::patternClip (2, 2, 7 * bar, bar);
    CHECK (! pltracks::applyLockToContent (song, other) && other.pattern == 2);
    // an empty locked track paints what was chosen
    pltracks::toggleLockToContent (song, { 6 });
    auto free = pl168f::patternClip (0, 6, 0, bar);
    CHECK (! pltracks::applyLockToContent (song, free) && free.pattern == 0);

    // the menu's track decides the new state for the other selected tracks (FL's mask 4)
    pltracks::toggleLockToContent (song, { 2, 6 });
    CHECK (! song.playlistTracks[2].lockedToContent && ! song.playlistTracks[6].lockedToContent);
    // a named track keeps its name
    song.playlistTracks[2].name = "Drums";
    pltracks::toggleLockToContent (song, { 2 });
    CHECK (song.playlistTracks[2].name == "Drums");
}

TEST (pl168f_lock_all_and_the_resize_answer)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (2);
    song.playlist.push_back (pl168f::patternClip (1, 0, 0, ppq));
    song.playlistTracks.resize (3);
    song.playlistTracks[1].height = 60;
    // "No": only the flags
    pltracks::lockAll (song, 4, true, false);
    CHECK (song.playlistTracks.size() >= 4);
    CHECK (song.playlistTracks[0].lockedToContent && song.playlistTracks[3].lockedToContent);
    CHECK (song.playlistTracks[0].name == "P2"); // auto-named
    CHECK (song.playlistTracks[1].height == 60 && song.playlistTracks[1].lockedHeight == -1);
    // "Yes": locked at the minimum size (FL sets +0x14 = 1, size 1.0) / unlocked at 100 %
    pltracks::lockAll (song, 4, true, true);
    CHECK (song.playlistTracks[1].lockedHeight == 1 && song.playlistTracks[1].height == 0);
    pltracks::lockAll (song, 4, false, true);
    CHECK (! song.playlistTracks[1].lockedToContent && song.playlistTracks[1].lockedHeight == -1);
}

TEST (pl168f_lock_to_this_size)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    CHECK (! pltracks::isSizeLocked (song, 3));
    // FUN_01228cb0: not locked → the row's height as shown; locked → negated (kept)
    pltracks::toggleSizeLock (song, { 3, 4 }, 57);
    CHECK (pltracks::isSizeLocked (song, 3) && song.playlistTracks[3].lockedHeight == 57 && song.playlistTracks[4].lockedHeight == 57);
    pltracks::toggleSizeLock (song, { 3 }, 99);
    CHECK (! pltracks::isSizeLocked (song, 3) && song.playlistTracks[3].lockedHeight == -57);
    // relocking takes the height shown now, not the old one
    pltracks::toggleSizeLock (song, { 3 }, 40);
    CHECK (song.playlistTracks[3].lockedHeight == 40);
    // the Size menu unlocks
    pltracks::unlockSize (song.playlistTracks[3]);
    CHECK (song.playlistTracks[3].lockedHeight == -40);
    PlaylistTrack fresh;
    pltracks::unlockSize (fresh);
    CHECK (fresh.lockedHeight == -1);
}

TEST (pl168f_send_pattern_instances_here)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (3);
    song.playlist.push_back (pl168f::patternClip (1, 0, 0, bar));
    song.playlist.push_back (pl168f::patternClip (1, 3, bar, bar));
    song.playlist.push_back (pl168f::patternClip (2, 1, 0, bar));
    const auto ch = pl168f::addAudioChannel (song, "Loop");
    song.playlist.push_back (pl168f::audioClip (ch, 2, 0, bar));
    // no selection: every clip of the current pattern, positions kept, the unnamed track auto-named
    song.currentPattern = 1;
    CHECK ((pltracks::sendPatternInstancesHere (song, 5, {}) == 2));
    CHECK (song.playlist[0].track == 5 && song.playlist[1].track == 5 && song.playlist[1].start == bar);
    CHECK (song.playlist[2].track == 1);
    CHECK (song.playlistTracks.size() > 5 && song.playlistTracks[5].name == "P2");
    // a selection: its pattern clips (any pattern), not the audio clip
    CHECK ((pltracks::sendPatternInstancesHere (song, 7, { 2, 3 }) == 1));
    CHECK (song.playlist[2].track == 7 && song.playlist[3].track == 2);
}

TEST (pl168f_reset_empty_tracks_and_groups)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    song.playlist.push_back (pl168f::patternClip (0, 1, 0, ppq));
    song.playlistTracks.resize (6);
    song.playlistTracks[0].name = "Empty but named";
    song.playlistTracks[1].name = "Has clips";
    song.playlistTracks[2].colour = 0xFF112233;
    song.playlistTracks[3].height = 70; // only a size: FL leaves it
    song.playlistTracks[4].name = "Audio";
    song.playlistTracks[4].mode = PlaylistTrackMode::audio;
    song.playlistTracks[4].mixerTrack = 2;
    CHECK (pltracks::resetEmptyTracks (song) == 2);
    CHECK (song.playlistTracks[0].name.empty() && song.playlistTracks[2].colour == 0);
    CHECK (song.playlistTracks[1].name == "Has clips" && song.playlistTracks[3].height == 70 && song.playlistTracks[4].name == "Audio");

    // Collapse / Expand all grouped tracks: every header
    song.playlistTracks[1].grouped = true;  // 0 = header
    song.playlistTracks[4].grouped = true;  // 3 = header
    pltracks::setAllGroupsCollapsed (song, true);
    CHECK (song.playlistTracks[0].collapsed && song.playlistTracks[3].collapsed && ! song.playlistTracks[2].collapsed);
    pltracks::setAllGroupsCollapsed (song, false);
    CHECK (! song.playlistTracks[0].collapsed && ! song.playlistTracks[3].collapsed);
}

TEST (pl168f_purge_unused_audio_clips)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    const auto before = song.channels.size();
    const auto used = pl168f::addAudioChannel (song, "Used");
    const auto unused = pl168f::addAudioChannel (song, "Unused");
    const auto stored = pl168f::addAudioChannel (song, "In arrangement 2");
    song.playlist.push_back (pl168f::audioClip (used, 0, 0, ppq));
    song.arrangements.push_back ({ "Arrangement 2", { pl168f::audioClip (stored, 0, 0, ppq) }, 0, 0, {} });
    CHECK (pltracks::unusedAudioClipChannels (song) == std::vector<int> { unused });
    CHECK (pltracks::purgeUnusedAudioClips (song) == 1);
    CHECK (song.channels.size() == before + 2 && song.channels[(std::size_t) used].name == "Used");
    CHECK (song.channels.back().name == "In arrangement 2");
    CHECK (song.playlist.size() == 1 && song.playlist[0].channel == used);
}

TEST (pl168f_merge_pattern_clips)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (3);
    Note n;
    n.length = ppq / 2;
    n.channel = 0;
    n.key = 60;
    n.start = 0;
    song.patterns[0].notes.push_back (n);
    n.start = ppq * 3;
    n.key = 62;
    song.patterns[0].notes.push_back (n);
    n.start = ppq;
    n.key = 70;
    song.patterns[2].notes.push_back (n);
    // clip A: pattern 0 at bar 2 on track 1, shows beats 0-4; clip B: pattern 2 at bar 3, offset one beat (skips nothing
    // before its note at beat 1 → the note lands at the clip start)
    song.playlist.push_back (pl168f::patternClip (2, 4, 3 * bar, bar));
    song.playlist.back().offset = ppq;
    song.playlist.push_back (pl168f::patternClip (0, 1, 2 * bar, bar));
    const auto ch = pl168f::addAudioChannel (song, "Loop");
    song.playlist.push_back (pl168f::audioClip (ch, 0, 0, bar));

    CHECK ((pltracks::mergePatternClips (song, { 0 }) == pltracks::MergeResult::notEnough));
    CHECK ((pltracks::mergePatternClips (song, { 0, 2 }) == pltracks::MergeResult::mixedKinds));
    int added = -1;
    CHECK ((pltracks::mergePatternClips (song, { 0, 1 }, &added) == pltracks::MergeResult::merged));
    // the new pattern sits after the lowest one involved (pattern 0) → index 1; old pattern 1 moved to 2
    CHECK (song.patterns.size() == 4 && song.patterns[1].name == "P1 #2" && song.patterns[1].colour == song.patterns[0].colour);
    CHECK (song.patterns[2].name == "P2");
    CHECK (song.playlist.size() == 2 && added == 1);
    const auto& m = song.playlist[(std::size_t) added];
    CHECK (m.kind == ClipKind::pattern && m.pattern == 1 && m.track == 1 && m.start == 2 * bar && m.length == 2 * bar);
    const auto& notes = song.patterns[1].notes;
    CHECK (notes.size() == 3);
    if (notes.size() == 3)
    {
        CHECK (notes[0].start == 0 && notes[0].key == 60);
        CHECK (notes[1].start == ppq * 3 && notes[1].key == 62);
        CHECK (notes[2].start == bar && notes[2].key == 70);
    }
    // the audio clip kept its channel (index shift does not touch it)
    CHECK (song.playlist[0].kind == ClipKind::audio && song.playlist[0].channel == ch);
    // no selection = every clip (FL's selection mask), so the audio clip makes it mixed
    CHECK ((pltracks::mergeCandidates (song, {}).size() == 2));
}

TEST (pl168f_clone_tracks)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (2);
    song.playlistTracks.resize (5);
    song.playlistTracks[1].name = "Lead";
    song.playlistTracks[1].colour = 0xFF445566;
    song.playlistTracks[1].lockedToContent = true;
    song.playlistTracks[3].name = "Lead 2"; // taken: the clone becomes "Lead 3"
    song.playlist.push_back (pl168f::patternClip (1, 1, 0, bar));
    const auto ch = pl168f::addAudioChannel (song, "Vox");
    song.playlist.push_back (pl168f::audioClip (ch, 1, bar, bar));
    song.playlist.push_back (pl168f::patternClip (0, 2, 0, bar));

    // every track empty → nothing to clone
    CHECK ((pltracks::cloneTracks (song, { 4 }, {}) == -1));

    pltracks::CloneOptions o;
    o.audioClips = false;
    const auto first = pltracks::cloneTracks (song, { 1 }, o);
    CHECK (first == 2);
    CHECK (song.playlistTracks[2].name == "Lead 3" && song.playlistTracks[2].colour == 0xFF445566 && song.playlistTracks[2].lockedToContent);
    int patterns = 0, audio = 0;
    for (const auto& c : song.playlist)
        if (c.track == 2)
            (c.kind == ClipKind::audio ? audio : patterns) += 1;
    CHECK (patterns == 1 && audio == 0);
    // the old track 2 moved down
    CHECK ((std::any_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.track == 3 && c.pattern == 0; })));
    CHECK (pltracks::uniqueTrackName (song, "Solo", -1) == "Solo");

    // a group header with Grouped tracks: its members come along, after the group
    auto g = pl168f::songWithPatterns (1);
    g.playlistTracks.resize (3);
    g.playlistTracks[1].grouped = true;
    g.playlist.push_back (pl168f::patternClip (0, 0, 0, bar));
    g.playlist.push_back (pl168f::patternClip (0, 1, 0, bar));
    CHECK ((pltracks::tracksToClone (g, { 0 }, true) == std::vector<int> { 0, 1 }));
    CHECK ((pltracks::tracksToClone (g, { 0 }, false) == std::vector<int> { 0 }));
    CHECK ((pltracks::cloneTracks (g, { 0, 1 }, {}) == 2));
    CHECK (! g.playlistTracks[2].grouped && g.playlistTracks[3].grouped);
}

TEST (pl168f_stretch_with_and_without_variants)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (1);
    const auto ch = pl168f::addAudioChannel (song, "Loop");
    song.playlist.push_back (pl168f::audioClip (ch, 0, 0, bar));
    song.playlist.push_back (pl168f::audioClip (ch, 1, 0, bar));
    song.playlist[1].offset = ppq;
    CHECK (pltracks::clipsOfChannel (song, ch) == 2);

    // Create variants on, two clips of the sample: only the stretched clip changes (its own stretch)
    auto before = song.playlist;
    pltracks::stretchAudioClip (song, before, 0, 0, 2 * bar, true);
    CHECK (song.playlist[0].length == 2 * bar && std::abs (song.playlist[0].stretch - 2.0) < 1e-9);
    CHECK (song.playlist[1].length == bar && song.playlist[1].stretch == 1.0);
    CHECK (pltracks::isVariant (song.playlist[0]) && ! pltracks::isVariant (song.playlist[1]));

    // off: the channel's timing changes, every clip of it follows (offsets scale too)
    song.playlist = before;
    pltracks::stretchAudioClip (song, before, 0, 0, 2 * bar, false);
    CHECK (std::abs (song.playlist[1].stretch - 2.0) < 1e-9 && song.playlist[1].length == 2 * bar && song.playlist[1].offset == 2 * ppq);

    // a clip that already is a variant stays one even with the option off
    song.playlist = before;
    song.playlist[0].pitch = 3.0f;
    before = song.playlist;
    pltracks::stretchAudioClip (song, before, 0, 0, bar / 2, false);
    CHECK (std::abs (song.playlist[0].stretch - 0.5) < 1e-9 && song.playlist[1].stretch == 1.0);

    // from the start: the end stays
    song.playlist = before;
    pltracks::stretchAudioClip (song, before, 1, bar / 2, bar / 2, true);
    CHECK (song.playlist[1].start == bar / 2 && song.playlist[1].end() == bar && std::abs (song.playlist[1].stretch - 0.5) < 1e-9);
}

TEST (pl168f_keys_and_flp_import)
{
    using namespace lynx;
    // FL's ShortcutsModule [DFM]: EEMergeAction Ctrl+G, EEClonePLTracksAction Ctrl+Shift+B, EEMakeUniqueAction
    // Ctrl+Shift+V, EEZoomOnPerformanceClipsAction Shift+6
    const std::pair<const char*, keys::Chord> expected[] = {
        { "playlist.mergePatternClips", keys::Chord { 'G', keys::ctrl } },
        { "playlist.cloneTracks", keys::Chord { 'B', (std::uint8_t) (keys::ctrl | keys::shift) } },
        { "playlist.makeUnique", keys::Chord { 'V', (std::uint8_t) (keys::ctrl | keys::shift) } },
        { "playlist.zoomPerformance", keys::Chord { '6', keys::shift } },
        { "playlist.stretchMode", keys::Chord { 'M', keys::shift } },
    };
    for (const auto& [id, chord] : expected)
    {
        const auto* a = keys::findAction (id);
        CHECK (a != nullptr && ! a->defaults.empty() && a->defaults.front() == chord);
    }
    for (const auto* id : { "playlist.createVariants", "playlist.lockAllToContent", "playlist.unlockAllFromContent", "playlist.resetEmptyTracks",
                            "playlist.collapseAllGroups", "playlist.expandAllGroups", "playlist.purgeUnusedAudio" })
        CHECK (keys::findAction (id) != nullptr);
    keys::KeyMap map;
    CHECK (map.conflicts().empty());

    // .flp 0xEE: i32 locked size at 17, u8 lock to content at 21 (FL's Data: byte 21 = 1 goes with the lock icon)
    FlpBuilder b;
    b.ascii (0xC7, "25.1.0.4000");
    b.u8 (0x63); b.u16 (0);
    auto track = [&b] (std::uint32_t index, std::int32_t lockedSize, std::uint8_t lockContent) {
        std::vector<std::uint8_t> r (70, 0);
        std::memcpy (&r[0], &index, 4);
        const std::uint32_t grey = 0x565148;
        std::memcpy (&r[4], &grey, 4);
        r[12] = 1;
        const float size = 1.0f;
        std::memcpy (&r[13], &size, 4);
        std::memcpy (&r[17], &lockedSize, 4);
        r[21] = lockContent;
        r[61] = 1;
        b.data (0xEE, r);
    };
    track (1, -1, 1);
    track (2, 50, 0);
    track (3, -16, 0);
    const auto song = importFlp (flp::parseProject (b.bytes));
    CHECK (song.playlistTracks.size() >= 2);
    if (song.playlistTracks.size() >= 2)
    {
        CHECK (song.playlistTracks[0].lockedToContent && song.playlistTracks[0].lockedHeight == -1);
        CHECK (! song.playlistTracks[1].lockedToContent && song.playlistTracks[1].lockedHeight == 50);
    }
    CHECK (song.playlistTracks.size() < 3 || song.playlistTracks[2].lockedHeight == -1);
}
