// Issue #53 (round mute53f): FL's Alt+M / Shift+Alt+M in the Playlist (EEMuteAction / EEUnmuteAction → FUN_00e3cc70:
// set / clear the clip mute bit of the selected clips, or of all clips when nothing is selected; one undo step)

TEST (mute53f_alt_m_mutes_and_shift_alt_m_unmutes_clips)
{
    using namespace lynx;
    const auto bar = (std::int64_t) ppq * 4;
    auto song = pl168f::songWithPatterns (2);
    for (int i = 0; i < 4; ++i)
        song.playlist.push_back (pl168f::patternClip (i % 2, i, i * bar, bar));

    // the selection: only those clips, and it sets (a second Alt+M keeps them muted, FL has no toggle here)
    CHECK (clipops::setClipsMuted (song, { 1, 3 }, true) == 2);
    CHECK (! song.playlist[0].muted && song.playlist[1].muted && ! song.playlist[2].muted && song.playlist[3].muted);
    CHECK (clipops::setClipsMuted (song, { 1, 3 }, true) == 0);
    CHECK (song.playlist[1].muted && song.playlist[3].muted);

    // nothing selected: every clip
    CHECK (clipops::setClipsMuted (song, {}, true) == 2);
    CHECK (std::all_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.muted; }));
    CHECK (clipops::setClipsMuted (song, { 0 }, false) == 1 && ! song.playlist[0].muted && song.playlist[1].muted);
    CHECK (clipops::setClipsMuted (song, {}, false) == 3);
    CHECK (std::none_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.muted; }));
    CHECK (clipops::setClipsMuted (song, { 99, -1 }, true) == 0); // stale indices are ignored

    // a time selection: only the clips that start inside it (ends included)
    CHECK (clipops::setClipsMuted (song, {}, true, bar, 2 * bar) == 2);
    CHECK (! song.playlist[0].muted && song.playlist[1].muted && song.playlist[2].muted && ! song.playlist[3].muted);

    // FL's keys (ShortcutsModule: Alt+M Mute, Shift+Alt+M Unmute), free of conflicts in the Playlist
    const auto* mute = keys::findAction ("playlist.muteClips");
    const auto* unmute = keys::findAction ("playlist.unmuteClips");
    CHECK (mute != nullptr && mute->defaults.front() == (keys::Chord { 'M', keys::alt }));
    CHECK (unmute != nullptr && unmute->defaults.front() == (keys::Chord { 'M', (std::uint8_t) (keys::shift | keys::alt) }));
    keys::KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find (keys::Chord { 'M', keys::alt }, keys::Context::playlist, false) == "playlist.muteClips");
    CHECK (map.find (keys::Chord { 'M', (std::uint8_t) (keys::shift | keys::alt) }, keys::Context::playlist, false) == "playlist.unmuteClips");
}
