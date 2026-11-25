// Round 190re (0.1.32 regression sweep): one test per defect the sweep found and fixed. The app-side checks of the same
// defects are the self-test's `sweep190re` item and the --sweep190re route (src/Sweep190re.inc).

// Undo / redo put back a whole earlier Song, with that moment's working time: every undo took the time since that edit
// off the project's clock (Settings › Info, the .flp's work days). A history step keeps the clock it leaves.
TEST (sweep190re_undo_keeps_the_project_clock)
{
    using namespace lynx;
    ProjectInfo older;
    older.title = "Before";
    older.created = 0.0;      // (a snapshot from before the first save)
    older.workSeconds = 60.0; // (a minute in)
    ProjectInfo now;
    now.title = "After";
    now.created = 1.7e9;
    now.workSeconds = 3600.0; // (an hour in)
    auto restored = older;
    keepProjectClock (restored, now);
    CHECK (restored.workSeconds == 3600.0 && restored.created == 1.7e9);
    CHECK (restored.title == "Before"); // (the credits stay undoable: "Project info" steps)
    // both history paths keep it: the normal one and the Online Sessions' selective one
    const auto engine = i18ntest::readFile ("src/engine/AudioEngine.cpp");
    const auto normal = engine.find ("void AudioEngine::historyStep (bool redo)");
    const auto session = engine.find ("void AudioEngine::sessionStep (bool redo)");
    CHECK (normal != std::string::npos && session != std::string::npos);
    const auto keepNormal = engine.find ("keepProjectClock (song.info, clock);", normal);
    const auto keepSession = engine.find ("keepProjectClock (next.info, song.info);", session);
    CHECK (keepNormal != std::string::npos && keepNormal < engine.find ("\n}\n", normal));
    CHECK (keepSession != std::string::npos && keepSession < engine.find ("\n}\n", session));
}

// "Nothing to play in Song mode yet" (round 190np's tip) stayed up after it stopped being true: a project opened, a clip
// placed, Pattern mode, the Clip-launch or Simple layout. The empty-state refresh (every song change and layout switch)
// closes it when the signal no longer holds.
TEST (sweep190re_song_mode_tip_follows_the_song)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    s.arrangements.clear();
    CHECK (onboarding::songModeEmptySignal (true, s, layoutstyle::Style::pattern));
    CHECK (! onboarding::songModeEmptySignal (false, s, layoutstyle::Style::pattern));     // (Pattern mode)
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::clipLaunch));   // (other layouts)
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::simple));
    s.playlist.push_back ({ 0, 0, 0, 4 * ppq, 0 });
    CHECK (! onboarding::songModeEmptySignal (true, s, layoutstyle::Style::pattern));      // (a clip placed / a project opened)
    const auto ui = i18ntest::readFile ("src/ui/MainOnboarding.cpp");
    const auto refresh = ui.find ("void MainComponent::updateEmptyHint()");
    const auto close = ui.find ("tipCard->getTip() == onboarding::Tip::songModeEmpty", refresh);
    CHECK (refresh != std::string::npos && close != std::string::npos && close < ui.find ("\n}\n", refresh));
    CHECK (ui.find ("closeTip();", close) < ui.find ("\n}\n", refresh));
}
