// --- Round 190mx: GitHub #198 follow-up — knobs turned in a plugin's own window while recording -------------------------
// FL 2026.1.7 (live check 190mx, Surge XT, wrapper "Notify about parameter changes" on = its default; saves dumped in
// /mnt/e/lynx-work/issues191_190mx/s198): Recording › "Notes and automation" writes a generator's own parameter into the
// current pattern as event data in Song mode as in Pattern mode (event id 0x813f = channel 0's parameter 0x13f), one event
// per change; no automation clip. Live: Arrangement Record → the track's automation (LYNX's lanes), Session Record → the
// recorded clip's envelopes. Recorded hand moves are thinned (Ramer-Douglas-Peucker within 1/512 of the range).

#include "core/ClipLauncher.h"
#include "core/DetailChain.h"
#include "core/LayoutStyle.h"
#include "core/PlaylistOps.h"
#include "core/Recorder.h"

namespace i198
{
// a 30 Hz sampling (at 120 BPM, 960 PPQ: 64 ticks per sample) of `f` over [from, to)
inline std::vector<lynx::RecordedValue> sampled (double from, double to, const std::function<float (double)>& f)
{
    std::vector<lynx::RecordedValue> out;
    for (double t = from; t < to; t += 64.0)
        out.push_back ({ t, f (t) });
    return out;
}
} // namespace i198

TEST (i198_thin_keeps_the_shape_with_few_points)
{
    using namespace lynx;
    // a straight ramp: its two ends
    std::vector<AutomationPoint> ramp;
    for (int i = 0; i <= 60; ++i)
        ramp.push_back ({ (std::int64_t) i * 64, (float) i / 60.0f, 0.0f });
    thinRecordedCurve (ramp);
    CHECK (ramp.size() == 2 && ramp.front().tick == 0 && ramp.back().tick == 60 * 64 && ramp.back().value == 1.0f);
    // up then down: the turn stays
    std::vector<AutomationPoint> tri;
    for (int i = 0; i <= 40; ++i)
        tri.push_back ({ (std::int64_t) i * 64, i <= 20 ? (float) i / 20.0f : (float) (40 - i) / 20.0f, 0.0f });
    thinRecordedCurve (tri);
    CHECK (tri.size() == 3 && tri[1].tick == 20 * 64 && tri[1].value == 1.0f);
    // jitter smaller than the tolerance goes; a jump bigger than it stays
    std::vector<AutomationPoint> flat;
    for (int i = 0; i <= 30; ++i)
        flat.push_back ({ (std::int64_t) i * 64, 0.5f + (i % 2 == 0 ? 0.0005f : -0.0005f), 0.0f });
    thinRecordedCurve (flat);
    CHECK (flat.size() == 2);
    std::vector<AutomationPoint> step { { 0, 0.2f, 0.0f }, { 64, 0.2f, 0.0f }, { 128, 0.8f, 0.0f }, { 192, 0.8f, 0.0f } };
    thinRecordedCurve (step);
    CHECK (step.size() == 4);
    // a smooth curve: every original sample is within the tolerance of the kept line
    std::vector<AutomationPoint> sine;
    for (int i = 0; i <= 90; ++i)
        sine.push_back ({ (std::int64_t) i * 64, 0.5f + 0.4f * (float) std::sin (i * 0.07), 0.0f });
    const auto original = sine;
    thinRecordedCurve (sine);
    CHECK (sine.size() < original.size() / 2 && sine.size() > 4);
    Automation a;
    a.points = sine;
    bool within = true;
    for (const auto& p : original)
        within = within && std::abs (a.valueAt ((double) p.tick) - p.value) <= recordThinTolerance + 1.0e-5f;
    CHECK (within);
}

TEST (i198_recorded_clip_is_thinned_and_follows_the_move)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.playlist.clear();
    song.automations.clear();
    AutomationWrite w;
    w.target = { AutomationTargetKind::effectParam, 1, 0, 3 };
    // a knob swept up over two beats, in batches as the engine writes them (about five a second)
    const auto all = i198::sampled (0.0, 2.0 * ppq, [] (double t) { return (float) (t / (2.0 * ppq)); });
    for (std::size_t i = 0; i < all.size(); i += 6)
        commitAutomationPoints (song, w, std::vector<RecordedValue> (all.begin() + (std::ptrdiff_t) i, all.begin() + (std::ptrdiff_t) std::min (all.size(), i + 6)), 0);
    CHECK (song.automations.size() == 1);
    if (! (song.automations.size() == 1))
        return;
    const auto& points = song.automations[0].points;
    CHECK (points.size() < all.size() / 2); // (30 samples: a straight line needs about one point per batch)
    bool follows = true;
    for (const auto& p : all)
        follows = follows && std::abs (song.automations[0].valueAt (p.tick) - p.value) < 0.01f;
    CHECK (follows);
}

TEST (i198_where_a_take_writes_by_layout)
{
    using namespace lynx;
    using D = AutomationDestination;
    const AutomationTarget pluginKnob { AutomationTargetKind::channelParam, 0, 0, 319 }; // (Surge's A Filter 1 Cutoff)
    const AutomationTarget pitchWheel { AutomationTargetKind::channelParam, 0, 0, midictl::pitchBendBase };
    const AutomationTarget effectKnob { AutomationTargetKind::effectParam, 1, 0, 2 };
    const AutomationTarget fader { AutomationTargetKind::mixerVolume, 1 };
    const auto pattern = (int) layoutstyle::Style::pattern, clipLaunch = (int) layoutstyle::Style::clipLaunch,
               simple = (int) layoutstyle::Style::simple;
    CHECK (recordsAsPatternEvents (pluginKnob) && ! recordsAsPatternEvents (pitchWheel) && ! recordsAsPatternEvents (effectKnob));
    // FL (Pattern layout): a generator's knob goes into the pattern in both modes; the others make clips in Song mode
    CHECK (recordedAutomationDestination (true, pattern, pluginKnob) == D::patternEvents);
    CHECK (recordedAutomationDestination (false, pattern, pluginKnob) == D::patternEvents);
    CHECK (recordedAutomationDestination (true, pattern, effectKnob) == D::automationClip);
    CHECK (recordedAutomationDestination (true, pattern, fader) == D::automationClip);
    CHECK (recordedAutomationDestination (true, pattern, pitchWheel) == D::automationClip);
    // Live (Clip-launch, Arrangement Record): every parameter into its track's automation lane
    CHECK (recordedAutomationDestination (true, clipLaunch, pluginKnob) == D::lane);
    CHECK (recordedAutomationDestination (true, clipLaunch, fader) == D::lane);
    CHECK (recordedAutomationDestination (false, clipLaunch, pluginKnob) == D::patternEvents);
    // Simple: as before (clips in Song mode)
    CHECK (recordedAutomationDestination (true, simple, pluginKnob) == D::automationClip);
}

TEST (i198_song_mode_plugin_knob_into_the_pattern)
{
    using namespace lynx;
    // FL: the take's moves into the current pattern, where its clip plays them (inside a clip: the clip's pattern position)
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.currentPattern = 0;
    song.patterns[0].events.clear();
    song.patterns[0].lengthTicks = 4 * ppq;
    song.playlist.clear();
    Clip c;
    c.pattern = 0;
    c.start = 4 * ppq; // bar 2
    c.length = 4 * ppq;
    song.playlist.push_back (c);
    AutomationWrite w;
    w.target = { AutomationTargetKind::channelParam, 0, 0, 319 };
    commitEventPoints (song, 0, w, { { 4.0 * ppq + 100, 0.25f }, { 4.0 * ppq + 300, 0.75f } }, 4 * ppq, 0);
    CHECK (song.patterns[0].events.size() == 1);
    if (! (song.patterns[0].events.size() == 1))
        return;
    const auto& ev = song.patterns[0].events[0].events;
    CHECK (ev.size() == 2 && ev[0].tick == 100 && ev[1].tick == 300 && ev[1].value == 0.75f);
    CHECK (song.automations.empty()); // (no automation clip)
}

TEST (i198_arrangement_record_writes_the_track_lane)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mode = PlayMode::song;
    song.automations.clear();
    song.playlist.clear();
    song.channels.resize (2);
    song.channels[1].mixerTrack = 2;
    while (song.playlistTracks.size() < 4)
        song.playlistTracks.emplace_back();
    for (auto& t : song.playlistTracks)
        t.lanes.clear();
    song.playlistTracks[3].mode = PlaylistTrackMode::instrument;
    song.playlistTracks[3].channel = 1;
    AutomationWrite w;
    w.target = { AutomationTargetKind::channelParam, 1, 0, 12 };
    w.name = "Cutoff";
    // a move from bar 3 on: a lane under the channel's track, points in song time, no flat line before or after (`linear`:
    // the song plays on past its end while recording, as the engine has it without a loop region)
    commitLanePoints (song, w, i198::sampled (8.0 * ppq, 9.0 * ppq, [] (double t) { return (float) ((t - 8.0 * ppq) / ppq); }), 0, true);
    CHECK (song.playlistTracks[3].lanes.size() == 1);
    if (! (song.playlistTracks[3].lanes.size() == 1))
        return;
    const auto a = song.playlistTracks[3].lanes[0].automation;
    CHECK (a >= 0 && a < (int) song.automations.size());
    if (! (a >= 0 && a < (int) song.automations.size()))
        return;
    CHECK (song.automations[(std::size_t) a].target == w.target && song.automations[(std::size_t) a].name == "Cutoff");
    CHECK (song.playlist.empty()); // (no automation clip)
    const auto& lane = song.automations[(std::size_t) a];
    CHECK (lane.points.front().tick == 8 * ppq && lane.points.size() <= 4);
    CHECK (std::abs (lane.valueAt (0.0) - 0.0f) < 1.0e-6f); // before: the first value
    CHECK (std::abs (lane.valueAt (8.5 * ppq) - 0.5f) < 0.01f);
    // a later gesture replaces only its stretch; the curve before it stays
    AutomationWrite w2 = w;
    w2.lastLocal = -1;
    commitLanePoints (song, w2, { { 20.0 * ppq, 0.1f }, { 20.0 * ppq + 64, 0.1f } }, 0, true);
    CHECK (song.playlistTracks[3].lanes.size() == 1 && w2.automation == a);
    CHECK (std::abs (song.automations[(std::size_t) a].valueAt (8.5 * ppq) - 0.5f) < 0.01f);
    CHECK (std::abs (song.automations[(std::size_t) a].valueAt (20.0 * ppq + 32) - 0.1f) < 1.0e-6f);
}

TEST (i198_session_record_writes_the_clip_envelope)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.patterns.resize (2);
    song.patterns[1].events.clear();
    AutomationWrite w;
    w.target = { AutomationTargetKind::effectParam, 2, 0, 1 };
    // the clip's own positions, folded into its length (a pass that went on past the end wraps)
    commitClipEnvelopePoints (song, 1, w, { { 10.0, 0.2f }, { 500.0, 0.4f }, { 4.0 * ppq + 20.0, 0.9f } }, 4 * ppq, 0);
    CHECK (song.patterns[1].events.size() == 1);
    if (! (song.patterns[1].events.size() == 1))
        return;
    const auto& lane = song.patterns[1].events[0];
    CHECK (lane.target == w.target);
    CHECK (std::abs (lane.valueAt (20.0) - 0.9f) < 1.0e-6f); // (the wrapped pass replaced its stretch)
    CHECK (std::abs (lane.valueAt (600.0) - 0.4f) < 1.0e-6f);
    // which track a moved control belongs to (Session Record writes the recording clip of that track)
    detailchain::Chain chain;
    chain.channel = 3;
    chain.channels = { 3 };
    chain.insert = 2;
    chain.inserts = { 2 };
    CHECK (detailchain::holds (chain, { AutomationTargetKind::channelParam, 3, 0, 7 }));
    CHECK (detailchain::holds (chain, { AutomationTargetKind::effectParam, 2, 1, 7 }));
    CHECK (! detailchain::holds (chain, { AutomationTargetKind::effectParam, 4, 1, 7 }));
    CHECK (! detailchain::holds (chain, { AutomationTargetKind::mixerVolume, 0 })); // (the Master is no track's)
    CHECK (! detailchain::holds (chain, { AutomationTargetKind::tempo, 0 }));
}

TEST (i198_grid_clip_plays_its_envelope)
{
    using namespace lynx;
    // a pattern clip launched in the grid drives its pattern's event lane at its own position (Live's clip envelope)
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.channels.resize (1);
    s.channels[0].instrument = InstrumentKind::synth;
    auto& p = s.patterns.emplace_back();
    p.lengthTicks = 4 * ppq;
    Note n;
    n.key = 60;
    n.length = ppq / 2;
    p.notes.push_back (n);
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelVolume, 0 };
    lane.events = { { 0, 0.1f }, { 2 * ppq, 0.9f } };
    p.events.push_back (lane);
    s.playlistTracks.resize (2);
    Clip c;
    c.pattern = 0;
    c.length = 4 * ppq;
    launcher::setClip (s, 0, 0, c);
    launcher::Runtime runtime;
    RackState::Factories factories;
    factories.clipLauncher = &runtime;
    auto state = RackState::build (s, nullptr, 48000.0, factories);
    CHECK (! state->eventLanes.empty());
    if (! (! state->eventLanes.empty()))
        return;
    const auto slot = state->eventLanes[0].slot;
    RackRenderer r;
    r.prepare (48000.0, 512);
    LiveInput input;
    r.setLiveInput (&input);
    std::vector<float> left (512), right (512);
    const auto tps = ticksPerSample (s.bpm, 48000.0);
    double tick = 0.0;
    bool sawLow = false, sawHigh = false, beforeLaunch = true;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 1)
            runtime.commands.push ({ launcher::Command::Type::launch, 0, 0 });
        r.process (state.get(), true, tick, 512, left.data(), right.data(), nullptr);
        tick += 512 * tps;
        const auto v = r.automationValueForCheck (slot);
        if (runtime.tracks[0].playing.load() != 0)
        {
            beforeLaunch = beforeLaunch && std::isnan (v); // (no clip of the pattern on the Playlist: nothing yet)
            continue;
        }
        sawLow = sawLow || std::abs (v - 0.1f) < 1.0e-6f;
        sawHigh = sawHigh || std::abs (v - 0.9f) < 1.0e-6f;
    }
    CHECK (beforeLaunch);
    CHECK (sawLow && sawHigh);
}
