// Issue #97: several automation clips on one target. FL's rule, from FL's own renders (FL64 /R, REA_rodada177b.md): where
// clips play, the one that started last (same start: the lowest automation); between clips, the end of the clip that
// ended last; before the first clip, the control's own value. Not only one automation of a target plays.
namespace issue97
{
const auto bar = (std::int64_t) lynx::ppq * 4;

lynx::Song song()
{
    auto s = lynx::Song::makeDefault();
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    return s;
}

// a flat automation of the Master's fader
int addFlat (lynx::Song& s, float value)
{
    lynx::Automation a;
    a.target = { lynx::AutomationTargetKind::mixerVolume, 0 };
    a.points = { { 0, value }, { bar * 32, value } };
    s.automations.push_back (a);
    return (int) s.automations.size() - 1;
}

void addClip (lynx::Song& s, int automation, int startBar, int bars, int track)
{
    lynx::Clip c;
    c.kind = lynx::ClipKind::automation;
    c.automation = automation;
    c.track = track;
    c.start = bar * startBar;
    c.length = bar * bars;
    s.playlist.push_back (c);
}

// the Master's peak over a bar
float levelAt (const lynx::Song& s, int atBar)
{
    const double sr = 48000.0;
    auto state = lynx::RackState::build (s, nullptr, sr);
    lynx::RackRenderer r;
    r.prepare (sr, 512);
    std::vector<float> l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, sr);
    float peak = 0.0f;
    for (double tick = (double) bar * atBar; tick < (double) bar * (atBar + 1); tick += 512.0 * tps)
    {
        r.process (state.get(), true, tick, 512, l.data(), rr.data());
        for (const auto v : l)
            peak = std::max (peak, std::abs (v));
    }
    return peak;
}

// the pattern in every bar, so every bar has signal
lynx::Song withSound()
{
    auto s = song();
    for (int b = 0; b < 32; ++b)
        s.playlist.push_back ({ 0, 0, bar * b, bar, 0 });
    return s;
}
} // namespace issue97

TEST (issue97_one_target_shares_a_slot)
{
    auto s = issue97::song();
    const auto a = issue97::addFlat (s, 0.2f), b = issue97::addFlat (s, 0.5f), c = issue97::addFlat (s, 0.8f);
    issue97::addClip (s, a, 4, 2, 1);
    issue97::addClip (s, b, 8, 2, 2);
    auto state = lynx::RackState::build (s, nullptr, 48000.0);
    CHECK (state->mixerVolumeAuto[0] == b); // (the third has no clip: it plays nothing)
    CHECK (state->automationGroups[(std::size_t) b].size() == 2 && state->automationFollowers[(std::size_t) a] == 1);
    CHECK (state->automationFollowers[(std::size_t) c] == 0 && state->automationGroups[(std::size_t) c].empty());

    // the second automation's clip goes: the first plays alone again
    s.playlist.pop_back();
    state = lynx::RackState::build (s, nullptr, 48000.0);
    CHECK (state->mixerVolumeAuto[0] == a && state->automationFollowers[(std::size_t) a] == 0 && state->automationGroups[(std::size_t) a].empty());
}

TEST (issue97_plugin_parameter_binds_once)
{
    // a CC / plugin parameter binding was made per automation: both wrote every block and the last one stayed
    auto s = issue97::song();
    s.channels[0].instrument = lynx::InstrumentKind::midiOut;
    for (const auto value : { 0.2f, 0.5f })
    {
        lynx::Automation a;
        a.target = { lynx::AutomationTargetKind::channelParam, 0, 0, lynx::midictl::ccBase + 1 };
        a.points = { { 0, value }, { issue97::bar * 8, value } };
        s.automations.push_back (a);
    }
    issue97::addClip (s, 0, 0, 2, 1);
    issue97::addClip (s, 1, 2, 2, 2);
    const auto state = lynx::RackState::build (s, nullptr, 48000.0);
    CHECK (state->pluginParams.size() == 1 && state->pluginParams[0].automation == 1);
}

TEST (issue97_the_clip_that_plays_decides)
{
    // FL's ra / rb renders: before = own value; A, held; B, held; and the channel order does not matter
    auto s = issue97::withSound();
    const auto own = issue97::levelAt (s, 1);
    const auto low = issue97::addFlat (s, 0.2f), high = issue97::addFlat (s, 0.5f);
    issue97::addClip (s, low, 4, 2, 1);
    issue97::addClip (s, high, 8, 2, 2);
    const auto before = issue97::levelAt (s, 1), first = issue97::levelAt (s, 4), between = issue97::levelAt (s, 6);
    const auto second = issue97::levelAt (s, 8), after = issue97::levelAt (s, 12);
    CHECK (std::abs (before - own) < 1e-3f);
    CHECK (first > 0.0f && first < second);
    CHECK (std::abs (between - first) < 1e-3f);
    CHECK (std::abs (after - second) < 1e-3f);

    auto swapped = issue97::withSound();
    const auto high2 = issue97::addFlat (swapped, 0.5f), low2 = issue97::addFlat (swapped, 0.2f);
    issue97::addClip (swapped, low2, 4, 2, 1);
    issue97::addClip (swapped, high2, 8, 2, 2);
    CHECK (std::abs (issue97::levelAt (swapped, 4) - first) < 1e-3f);
    CHECK (std::abs (issue97::levelAt (swapped, 8) - second) < 1e-3f);
}

TEST (issue97_overlap_the_later_start_wins)
{
    // FL's rc / rd / re renders
    auto s = issue97::withSound();
    const auto a = issue97::addFlat (s, 0.2f), b = issue97::addFlat (s, 0.5f);
    issue97::addClip (s, a, 4, 6, 2); // A 4..10
    issue97::addClip (s, b, 6, 2, 1); // B 6..8, inside it
    const auto onlyA = issue97::levelAt (s, 4), inside = issue97::levelAt (s, 6), resumes = issue97::levelAt (s, 8);
    CHECK (onlyA < inside);
    CHECK (std::abs (resumes - onlyA) < 1e-3f);

    // the same start: the lowest automation (FL's rg / rh / ri, whichever the track or the file order)
    auto tie = issue97::withSound();
    const auto a2 = issue97::addFlat (tie, 0.2f), b2 = issue97::addFlat (tie, 0.5f);
    issue97::addClip (tie, b2, 4, 4, 1);
    issue97::addClip (tie, a2, 4, 4, 2);
    CHECK (std::abs (issue97::levelAt (tie, 5) - onlyA) < 1e-3f);

    // the same automation twice: the later clip restarts its curve (FL's rj)
    auto same = issue97::song();
    lynx::Automation ramp;
    ramp.target = { lynx::AutomationTargetKind::mixerVolume, 0 };
    ramp.points = { { 0, 0.0f }, { issue97::bar * 4, 1.0f } };
    same.automations.push_back (ramp);
    issue97::addClip (same, 0, 0, 4, 1);
    issue97::addClip (same, 0, 2, 4, 2);
    CHECK (std::abs (same.targetValueAt (ramp.target, (double) issue97::bar * 2) - 0.0f) < 1e-5f);
    CHECK (std::isnan (same.targetValueAt ({ lynx::AutomationTargetKind::mixerPan, 0 }, 0.0)));
}

TEST (issue97_two_tempo_clips)
{
    auto s = issue97::song();
    for (const auto value : { 0.2f, 0.6f })
    {
        lynx::Automation a;
        a.target.kind = lynx::AutomationTargetKind::tempo;
        a.points = { { 0, value }, { issue97::bar * 8, value } };
        s.automations.push_back (a);
    }
    issue97::addClip (s, 0, 2, 2, 1);
    issue97::addClip (s, 1, 6, 2, 2);
    // before the first clip: the first clip's value (178a's rule for the tempo, unverified against FL)
    CHECK (std::abs (s.tempoAt ((double) issue97::bar) - lynx::tempoFromValue (0.2f)) < 1.0e-3);
    const auto first = s.tempoAt ((double) issue97::bar * 3), second = s.tempoAt ((double) issue97::bar * 7);
    CHECK (first < second);
    CHECK (s.tempoAt ((double) issue97::bar * 5) == first);
    CHECK (s.tempoAt ((double) issue97::bar * 9) == second);
}
