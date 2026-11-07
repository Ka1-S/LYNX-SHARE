// Round 180c: FL's Merge similar pattern clips (Ctrl+Shift+G, FUN_012dc060 (1, 1)) and Merge automation clips
// (Ctrl+Alt+G, FUN_013009f0 / FUN_012ff0d0) (docs/analise/REA_rodada180c.md)

namespace merge180c
{
lynx::Clip automationClip (int automation, int track, std::int64_t start, std::int64_t length, std::int64_t offset = 0)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::automation;
    c.automation = automation;
    c.track = track;
    c.start = start;
    c.length = length;
    c.offset = offset;
    return c;
}

int addAutomation (lynx::Song& song, const std::string& name, int targetIndex, std::vector<lynx::AutomationPoint> points)
{
    lynx::Automation a;
    a.name = name;
    a.target.kind = lynx::AutomationTargetKind::channelVolume;
    a.target.index = targetIndex;
    a.points = std::move (points);
    song.automations.push_back (a);
    return (int) song.automations.size() - 1;
}

int countClips (const lynx::Song& song, int pattern)
{
    return (int) std::count_if (song.playlist.begin(), song.playlist.end(), [pattern] (const lynx::Clip& c) {
        return c.kind == lynx::ClipKind::pattern && c.pattern == pattern;
    });
}
} // namespace merge180c

TEST (merge180c_similar_replaces_every_group)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (3);
    const std::int64_t bar = ppq * 4;
    Note n;
    n.length = ppq;
    n.key = 60;
    song.patterns[0].notes.push_back (n);
    n.key = 72;
    song.patterns[1].notes.push_back (n);

    // the group: P1 + P2 stacked at bar 0 (tracks 2, 3); a copy at bar 8 on other tracks (the tracks don't count)
    song.playlist.push_back (pl168f::patternClip (0, 2, 0, bar));
    song.playlist.push_back (pl168f::patternClip (1, 3, 0, bar));
    song.playlist.push_back (pl168f::patternClip (0, 6, 8 * bar, bar));
    song.playlist.push_back (pl168f::patternClip (1, 5, 8 * bar, bar));
    // not similar: P2 is one bar late / P2's window differs / P1 alone / P2 muted
    song.playlist.push_back (pl168f::patternClip (0, 0, 16 * bar, bar));
    song.playlist.push_back (pl168f::patternClip (1, 1, 17 * bar, bar));
    song.playlist.push_back (pl168f::patternClip (0, 0, 24 * bar, bar));
    song.playlist.push_back (pl168f::patternClip (1, 1, 24 * bar, bar));
    song.playlist.back().offset = ppq;
    song.playlist.push_back (pl168f::patternClip (0, 0, 32 * bar, bar));
    song.playlist.push_back (pl168f::patternClip (1, 1, 32 * bar, bar));
    song.playlist.back().muted = true;

    int groups = -1;
    CHECK ((pltracks::mergeSimilarPatternClips (song, { 0, 1 }, &groups) == pltracks::MergeResult::merged));
    CHECK (groups == 1);
    // new pattern after P1 (index 1), P2 moved to 2: 2 clips of it, and the six left-alone clips
    CHECK (song.patterns.size() == 4 && song.patterns[1].notes.size() == 2);
    CHECK (merge180c::countClips (song, 1) == 2);
    CHECK (song.playlist.size() == 8);
    bool first = false, second = false;
    for (const auto& c : song.playlist)
        if (c.pattern == 1)
        {
            CHECK (c.length == bar);
            first = first || (c.start == 0 && c.track == 2);     // the topmost of the group's tracks
            second = second || (c.start == 8 * bar && c.track == 5);
        }
    CHECK (first && second);
    CHECK (merge180c::countClips (song, 0) == 3);
    CHECK (merge180c::countClips (song, 2) == 3);
}

TEST (merge180c_similar_restarts_and_refuses)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (2);
    const std::int64_t bar = ppq * 4;
    // A B, A B, A B in a row on one track: the selection is the first A B
    for (int i = 0; i < 3; ++i)
    {
        song.playlist.push_back (pl168f::patternClip (0, 0, (i * 2) * bar, bar));
        song.playlist.push_back (pl168f::patternClip (1, 0, (i * 2 + 1) * bar, bar));
    }
    int groups = -1;
    CHECK ((pltracks::mergeSimilarPatternClips (song, { 0, 1 }, &groups) == pltracks::MergeResult::merged));
    CHECK (groups == 2);
    CHECK (song.playlist.size() == 3);
    for (const auto& c : song.playlist)
    {
        CHECK (c.pattern == 1 && c.length == 2 * bar);
    }

    // other kinds in the list are skipped (not "different clip types"), but two pattern clips are still needed
    auto other = pl168f::songWithPatterns (2);
    const auto ch = pl168f::addAudioChannel (other, "Loop");
    other.playlist.push_back (pl168f::patternClip (0, 0, 0, bar));
    other.playlist.push_back (pl168f::audioClip (ch, 1, 0, bar));
    CHECK ((pltracks::mergeSimilarPatternClips (other, { 0, 1 }) == pltracks::MergeResult::notEnough));
    other.playlist.push_back (pl168f::patternClip (1, 0, bar, bar));
    CHECK ((pltracks::mergeSimilarPatternClips (other, { 0, 1, 2 }) == pltracks::MergeResult::merged));
    CHECK (other.playlist.size() == 2);
}

TEST (merge180c_pattern_merge_skips_muted_and_uses_top_track)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (2);
    const std::int64_t bar = ppq * 4;
    song.playlist.push_back (pl168f::patternClip (0, 5, 0, bar));
    song.playlist.push_back (pl168f::patternClip (1, 2, bar, bar));
    int added = -1;
    CHECK ((pltracks::mergePatternClips (song, { 0, 1 }, &added) == pltracks::MergeResult::merged));
    CHECK (song.playlist[(std::size_t) added].track == 2 && song.playlist[(std::size_t) added].start == 0);

    auto muted = pl168f::songWithPatterns (2);
    muted.playlist.push_back (pl168f::patternClip (0, 0, 0, bar));
    muted.playlist.push_back (pl168f::patternClip (1, 1, bar, bar));
    muted.playlist.back().muted = true;
    CHECK ((pltracks::mergePatternClips (muted, { 0, 1 }) == pltracks::MergeResult::notEnough));
}

TEST (merge180c_automation_checks)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    const std::int64_t bar = ppq * 4;
    const auto a = merge180c::addAutomation (song, "Cut", 0, { { 0, 0.0f, 0.0f }, { bar, 1.0f, 0.0f } });
    const auto b = merge180c::addAutomation (song, "Res", 1, { { 0, 0.5f, 0.0f }, { bar, 0.25f, 0.0f } });
    song.playlist.push_back (merge180c::automationClip (a, 1, 0, bar));
    song.playlist.push_back (merge180c::automationClip (a, 1, bar, bar));
    song.playlist.push_back (pl168f::patternClip (0, 0, 0, bar));
    song.playlist.push_back (merge180c::automationClip (b, 2, 2 * bar, bar));

    using Check = pltracks::AutomationMergeCheck;
    CHECK ((pltracks::checkAutomationMerge (song, { 0 }) == Check::tooFew));
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 2 }) == Check::notAutomation));
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 1 }) == Check::ok));
    CHECK (! pltracks::automationLinksConflict (song, { 0, 1 }));
    CHECK (pltracks::automationLinksConflict (song, { 0, 3 }));

    song.playlist[1].start = bar / 2;
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 1 }) == Check::overlapping));
    song.playlist[1].start = bar;
    // a window with no control point at its start
    song.playlist[1].offset = ppq;
    song.playlist[1].length = bar - ppq;
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 1 }) == Check::missingEnds));
    // a clip that runs on past the last point only holds: still whole
    song.playlist[1].offset = 0;
    song.playlist[1].length = 3 * bar;
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 1 }) == Check::ok));
}

TEST (merge180c_automation_lossless_keeps_points_and_holds_gaps)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    const std::int64_t bar = ppq * 4;
    const auto a = merge180c::addAutomation (song, "Cut", 3, { { 0, 0.0f, 0.5f }, { bar, 1.0f, 0.0f } });
    const auto b = merge180c::addAutomation (song, "Res", 4, { { 0, 0.5f, 0.0f }, { ppq, 0.75f, 0.0f }, { bar, 0.25f, 0.0f } });
    song.playlist.push_back (merge180c::automationClip (b, 2, 3 * bar, ppq));  // a window with both points, later in time
    song.playlist.push_back (merge180c::automationClip (a, 1, bar, bar));

    const auto before = (int) song.automations.size();
    const auto index = pltracks::mergeAutomationClips (song, { 0, 1 }, pltracks::AutomationLinks::keepFirst, false);
    CHECK (index >= 0);
    if (index < 0)
        return;
    CHECK ((int) song.automations.size() == before + 1);
    CHECK (song.playlist.size() == 1);
    const auto& clip = song.playlist[(std::size_t) index];
    CHECK (clip.kind == ClipKind::automation && clip.automation == before && clip.start == bar && clip.track == 1 && clip.length == 2 * bar + ppq);
    const auto& m = song.automations[(std::size_t) clip.automation];
    CHECK (m.name == "Cut - merged");
    CHECK (m.target.index == 3);
    CHECK (m.points.size() == 5);
    if (m.points.size() != 5)
        return;
    CHECK (m.points[0].tick == 0 && m.points[0].value == 0.0f && m.points[0].tension == 0.5f);
    CHECK (m.points[1].tick == bar && m.points[1].value == 1.0f);
    CHECK (m.points[2].tick == 2 * bar && m.points[2].value == 1.0f); // the gap holds
    CHECK (m.points[3].tick == 2 * bar && m.points[3].value == 0.5f);
    CHECK (m.points[4].tick == 2 * bar + ppq && m.points[4].value == 0.75f);
    // the old automations stay, as FL's channels do
    CHECK (song.automations[(std::size_t) a].points.size() == 2 && song.automations[(std::size_t) b].points.size() == 3);
}

TEST (merge180c_automation_lossy_cuts_overlap_and_follows_the_curve)
{
    using namespace lynx;
    auto song = pl168f::songWithPatterns (1);
    const std::int64_t bar = ppq * 4;
    const auto a = merge180c::addAutomation (song, "Cut", 0, { { 0, 0.0f, 0.0f }, { bar, 1.0f, 0.0f } });
    const auto b = merge180c::addAutomation (song, "Res", 0, { { 0, 0.25f, 0.0f }, { bar, 0.25f, 0.0f } });
    song.playlist.push_back (merge180c::automationClip (a, 0, 0, bar));
    song.playlist.push_back (merge180c::automationClip (b, 0, bar / 2, bar));
    CHECK ((pltracks::checkAutomationMerge (song, { 0, 1 }) == pltracks::AutomationMergeCheck::overlapping));
    const auto index = pltracks::mergeAutomationClips (song, { 0, 1 }, pltracks::AutomationLinks::keepFirst, true);
    CHECK (index >= 0);
    if (index < 0)
        return;
    const auto& m = song.automations[(std::size_t) song.playlist[(std::size_t) index].automation];
    CHECK (std::abs (m.valueAt ((double) bar / 4) - 0.25f) < 0.01f); // first clip's ramp
    CHECK (std::abs (m.valueAt ((double) bar / 2 - 2.0) - 0.5f) < 0.01f);
    CHECK (std::abs (m.valueAt ((double) bar) - 0.25f) < 0.001f);    // the second clip took over
    CHECK (m.points.size() < 12);                                    // lines, not one point per tick
}

TEST (merge180c_automation_link_answers)
{
    using namespace lynx;
    const std::int64_t bar = ppq * 4;
    const auto make = [] {
        auto song = pl168f::songWithPatterns (1);
        const auto a = merge180c::addAutomation (song, "Cut", 3, { { 0, 0.0f, 0.0f }, { bar, 1.0f, 0.0f } });
        const auto b = merge180c::addAutomation (song, "Res", 4, { { 0, 0.5f, 0.0f }, { bar, 0.25f, 0.0f } });
        song.playlist.push_back (merge180c::automationClip (a, 0, 0, bar));
        song.playlist.push_back (merge180c::automationClip (b, 0, bar, bar));
        return song;
    };

    auto first = make();
    pltracks::mergeAutomationClips (first, { 0, 1 }, pltracks::AutomationLinks::keepFirst, false);
    CHECK (first.playlist.size() == 1 && first.automations.back().target.index == 3);

    auto none = make();
    pltracks::mergeAutomationClips (none, { 0, 1 }, pltracks::AutomationLinks::deleteAll, false);
    CHECK (none.playlist.size() == 1 && none.automations.back().target.kind == AutomationTargetKind::none);

    // one LYNX automation holds one target: the other target gets its own clip with the same curve
    auto all = make();
    const auto index = pltracks::mergeAutomationClips (all, { 0, 1 }, pltracks::AutomationLinks::mergeAll, false);
    CHECK (all.playlist.size() == 2 && all.automations.size() == 4);
    CHECK (all.automations[2].target.index == 3 && all.automations[3].target.index == 4);
    CHECK (all.automations[2].points == all.automations[3].points);
    CHECK (all.automations[3].name == "Cut - merged #2");
    CHECK (all.playlist[0].start == all.playlist[1].start && all.playlist[(std::size_t) index].automation == 2);
}
