// Round 177d, FL pain points (research-web-2.md §3; docs/analise/REA_rodada177d.md §2).
// Slice 177d-2: 22 solo action, 20 ultra-fine, 15 backups, 37 savedBy.

TEST (pain177d_ultra_fine_drag_and_wheel)
{
    using namespace lynx::controls;
    CHECK (fineScale (false, false) == 1.0);
    CHECK (fineScale (false, true) == 1.0); // Shift alone changes nothing (FL)
    CHECK (fineScale (true, false) == fineFactor);
    CHECK (fineScale (true, true) == ultraFineFactor);
    // 100 px over a 0..1 range swept by 200 px: 0.5 plain, 0.05 fine, 0.005 ultra-fine
    CHECK (std::abs (dragValueScaled (0.0, 100.0, 0.0, 1.0, 200.0, 1.0) - 0.5) < 1e-12);
    CHECK (std::abs (dragValueScaled (0.0, 100.0, 0.0, 1.0, 200.0, fineScale (true, false)) - 0.05) < 1e-12);
    CHECK (std::abs (dragValueScaled (0.0, 100.0, 0.0, 1.0, 200.0, fineScale (true, true)) - 0.005) < 1e-12);
    // the old entry points are unchanged
    CHECK (dragValue (0.0, 100.0, 0.0, 1.0, 200.0, true) == dragValueScaled (0.0, 100.0, 0.0, 1.0, 200.0, fineFactor));
    CHECK (wheelValue (0.5, 1.0, 0.0, 1.0, false) == wheelValueScaled (0.5, 1.0, 0.0, 1.0, 1.0));
    CHECK (std::abs (wheelValueScaled (0.5, 1.0, 0.0, 1.0, ultraFineFactor) - (0.5 + wheelStep * 0.01)) < 1e-12);
}

TEST (pain177d_solo_selected_channel_action_is_bound_nowhere_by_default)
{
    const auto& actions = lynx::keys::actions();
    const auto it = std::find_if (actions.begin(), actions.end(), [] (const auto& a) { return std::string (a.id) == "channel.soloSelected"; });
    CHECK (it != actions.end());
    if (it != actions.end())
        CHECK (it->defaults.empty());
}

TEST (pain177d_backups_wait_for_a_quiet_moment)
{
    using lynx::prefs::backupMayRun;
    constexpr double minute = 60000.0, longest = 10 * minute;
    // recording: never (FL), not even past the longest wait
    CHECK (! backupMayRun (true, false, 1e9, false, 0.0, longest));
    CHECK (! backupMayRun (true, false, 1e9, true, 0.0, longest));
    CHECK (! backupMayRun (true, true, 1e9, true, 1e12, longest));
    // the option off: only recording holds it (FL)
    CHECK (backupMayRun (false, true, 0.0, false, 0.0, longest));
    // on (the default): not while playing, not right after MIDI input
    CHECK (! backupMayRun (false, true, 1e9, true, 0.0, longest));
    CHECK (! backupMayRun (false, false, 1000.0, true, 0.0, longest));
    CHECK (backupMayRun (false, false, lynx::prefs::backupQuietMs, true, 0.0, longest));
    // the user's decision: 10 minutes without a backup and it is written anyway, playing or just after MIDI input
    CHECK (lynx::prefs::defaultBackupLongestWaitMinutes == 10);
    CHECK (! backupMayRun (false, true, 0.0, true, longest - 1.0, longest));
    CHECK (backupMayRun (false, true, 0.0, true, longest, longest));
    CHECK (lynx::prefs::clampBackupLongestWait (10) == 10);
    CHECK (lynx::prefs::clampBackupLongestWait (0) == 5);
    CHECK (lynx::prefs::clampBackupLongestWait (1000) == 60);
    // clock and active sensing are not someone playing
    using lynx::prefs::midiMeansPlaying;
    CHECK (midiMeansPlaying (0x90) && midiMeansPlaying (0xB3) && midiMeansPlaying (0xE0));
    CHECK (! midiMeansPlaying (0xF8) && ! midiMeansPlaying (0xFE) && ! midiMeansPlaying (0xF0) && ! midiMeansPlaying (0x40));
    // 15 minutes stopped with a device sending clock and active sensing: only played messages reset the 5 s, so the
    // first due backup (2 minutes) runs
    {
        double lastPlayed = -1e9, firstBackup = -1.0;
        for (double now = 0.0; now < 15 * minute && firstBackup < 0.0; now += 100.0)
        {
            if (midiMeansPlaying (0xF8) || midiMeansPlaying (0xFE))
                lastPlayed = now;
            if (now >= 2 * minute && backupMayRun (false, false, now - lastPlayed, true, now, longest))
                firstBackup = now;
        }
        CHECK (firstBackup == 2 * minute);
    }
}

TEST (pain177d_saved_by_newer_version)
{
    using lynx::safety::compareVersions;
    using lynx::safety::savedByNewerVersion;
    CHECK (compareVersions ("0.1.10", "0.1.9") > 0);
    CHECK (compareVersions ("0.1.9", "0.1.10") < 0);
    CHECK (compareVersions ("0.1.25-alpha", "0.1.25") == 0);
    CHECK (compareVersions ("0.2", "0.1.99") > 0);
    CHECK (compareVersions ("1.0", "1.0.0") == 0);
    CHECK (compareVersions ("", "0.0.0") == 0);
    // only a newer build warns; projects from before "savedBy" never do
    CHECK (savedByNewerVersion ("0.1.26-alpha", "0.1.25-alpha"));
    CHECK (! savedByNewerVersion ("0.1.25-alpha", "0.1.25-alpha"));
    CHECK (! savedByNewerVersion ("0.1.20", "0.1.25-alpha"));
    CHECK (! savedByNewerVersion ("", "0.1.25-alpha"));
}
