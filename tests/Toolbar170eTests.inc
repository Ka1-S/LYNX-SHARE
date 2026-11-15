// Round 170e (Busy Works review #17): trackpad zoom and scrolling — Windows touchpad pieces of a wheel notch become
// smooth deltas of the same size, a pinch zooms by its own factor (limited per event).

#include <limits>

#include "core/EditorGestures.h"
#include "core/ToolbarLayout.h"
#include "core/RecordLoop.h"
#include "core/StepEdit.h"

TEST (toolbar170e_windows_touchpad_wheel_pieces_scale_with_the_fingers)
{
    using namespace lynx::gesture;
    const auto notch = windowsWheelNotch; // 120 wheel units in JUCE's units on Windows

    // a notched wheel is unchanged (one notch = one step), on Windows and elsewhere
    for (const auto windows : { true, false })
    {
        const auto w = normaliseWheel (0.0, notch, false, windows);
        CHECK (! w.smooth && w.y == notch);
        CHECK (wheelZoomFactor (w.y, w.smooth) == 1.2);
    }
    // macOS / Linux small notched deltas (a classic mouse reports ~0.04 per notch on a Mac) stay one notch each
    CHECK (! normaliseWheel (0.0, 0.04, false, false).smooth);
    CHECK (wheelZoomFactor (normaliseWheel (0.0, 0.04, false, false).y, false) == 1.2);

    // Windows: a quarter notch (a precision touchpad piece, also a pinch sent as Ctrl+wheel) zooms a quarter step
    {
        const auto w = normaliseWheel (0.0, notch / 4.0, false, true);
        CHECK (w.smooth);
        CHECK (std::abs (wheelZoomFactor (w.y, w.smooth) - std::pow (1.2, 0.25)) < 1e-9);
        CHECK (std::abs (wheelScrollPixels (w.y, w.smooth, 80.0) + 20.0) < 1e-9); // a quarter of 80 px, upwards
    }
    // four quarter pieces zoom exactly as one notch
    {
        auto factor = 1.0;
        for (int i = 0; i < 4; ++i)
        {
            const auto w = normaliseWheel (0.0, -notch / 4.0, false, true);
            factor *= wheelZoomFactor (w.y, w.smooth);
        }
        CHECK (std::abs (factor - 1.0 / 1.2) < 1e-9);
    }
    // a sideways touchpad piece scrolls sideways in proportion
    {
        const auto w = normaliseWheel (notch / 2.0, 0.0, false, true);
        CHECK (w.smooth && w.y == 0.0);
        CHECK (std::abs (wheelScrollPixels (w.x, w.smooth, 80.0) + 40.0) < 1e-9);
    }
    // smooth events (macOS trackpads) pass through untouched
    {
        const auto w = normaliseWheel (0.01, -0.02, true, true);
        CHECK (w.smooth && w.x == 0.01 && w.y == -0.02);
    }
    CHECK (normaliseWheel (0.0, 0.0, false, true).smooth == false);
}

TEST (toolbar170e_pinch_zoom_factor)
{
    using namespace lynx::gesture;
    CHECK (pinchZoomFactor (1.0) == 1.0);
    CHECK (pinchZoomFactor (1.05) == 1.05);
    CHECK (pinchZoomFactor (0.9) == 0.9);
    CHECK (pinchZoomFactor (10.0) == 2.0);   // one event never jumps more than x2
    CHECK (pinchZoomFactor (0.01) == 0.5);
    CHECK (pinchZoomFactor (0.0) == 1.0);    // a broken event does nothing
    CHECK (pinchZoomFactor (-1.0) == 1.0);
    CHECK (pinchZoomFactor (std::numeric_limits<double>::infinity()) == 1.0);
    // a pinch out and back in returns to the same zoom, around the same anchor
    const auto scroll = 960.0, ppt = 0.25, anchor = 300.0;
    const auto zoomed = ppt * pinchZoomFactor (1.25);
    const auto s1 = zoomAnchored (scroll, ppt, zoomed, anchor);
    const auto s2 = zoomAnchored (s1, zoomed, zoomed * pinchZoomFactor (0.8), anchor);
    CHECK (std::abs (s2 - scroll) < 1e-9);
}

TEST (toolbar170e2_recording_options_outlast_the_file_buttons)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    std::array<int, groupCount> widths;
    widths.fill (100);
    // room for five of the eight groups: output, meters and then the file buttons drop; the recording options stay
    const auto v = fit (l, widths, 10, 5 * 110 + 300, 300);
    CHECK (v[(std::size_t) Group::record] && v[(std::size_t) Group::snap] && v[(std::size_t) Group::windows]);
    CHECK (! v[(std::size_t) Group::file] && ! v[(std::size_t) Group::meters] && ! v[(std::size_t) Group::output]);
    CHECK (groupPriority (Group::record) > groupPriority (Group::file));
}

// --- round 170e3: a real toolbar pass (loop record, step edit, bigger icons, wide windows) ---

TEST (toolbar170e3_loop_record_decides_whether_song_takes_fold)
{
    using lynx::PlayMode;
    using namespace lynx::recordloop;
    // on (the default): a loop region loops; no region: the song plays on (round 168k)
    CHECK (! linear (PlayMode::song, true, true));
    CHECK (linear (PlayMode::song, false, true));
    // off: straight through the region too (FL: the play end becomes "never" while recording)
    CHECK (linear (PlayMode::song, true, false));
    CHECK (notesLinear (PlayMode::song, true, false));
    // punch in/out plays through the region for audio, the notes' rule stays
    CHECK (linear (PlayMode::song, true, true, true));
    CHECK (! notesLinear (PlayMode::song, true, true));
    // Pattern mode loops the pattern either way
    CHECK (! linear (PlayMode::pattern, true, false) && ! linear (PlayMode::pattern, false, true));
}

TEST (toolbar170e3_loop_record_off_keeps_a_second_pass_where_it_was_played)
{
    // Song mode, loop region over bars 3-4, Loop record off: the take runs on to bar 5 and the note stays there
    auto s = lynx::Song::makeDefault();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    s.mode = lynx::PlayMode::song;
    s.loopStart = 2 * bar;
    s.loopEnd = 4 * bar;
    s.patterns[0].notes.clear();
    s.playlist.clear();
    lynx::NoteRecorder recorder;
    recorder.begin (s.patterns[0].effectiveLength (s.meter()));
    if (lynx::recordloop::notesLinear (s.mode, s.hasLoop(), false))
        recorder.setLinear (true);
    std::vector<int> cleared;
    lynx::TakeQuantize q {};
    q.grid = 0;
    recorder.add (s, { (double) (2 * bar + lynx::ppq), 0, 60, 0.8f, true });
    recorder.add (s, { (double) (2 * bar + 2 * lynx::ppq), 0, 60, 0.0f, false });
    recorder.add (s, { (double) (4 * bar + lynx::ppq), 0, 62, 0.8f, true });
    recorder.add (s, { (double) (4 * bar + 2 * lynx::ppq), 0, 62, 0.0f, false });
    recorder.end (s, (double) (5 * bar));
    auto notes = recorder.takeFinished();
    bool first = true;
    lynx::commitNoteBatch (s, recorder, std::move (notes), lynx::RecordMode::overdub, q, cleared, true, first);
    const auto& p = s.patterns[(std::size_t) s.currentPattern];
    CHECK (p.notes.size() == 2);
    // a bar apart and two bars apart: the second pass did not fold back onto the first
    CHECK (p.notes.size() == 2 && p.notes[1].start - p.notes[0].start == 2 * bar);
}

TEST (toolbar170e3_step_edit_writes_steps_and_chords)
{
    using namespace lynx::stepedit;
    lynx::Pattern p;
    Writer w;
    const std::int64_t step = lynx::ticksPerStep;
    w.reset (0);
    // a chord: three keys down, then up: one step, then the position moves on once
    CHECK (! w.play ({ 0, 60, 0.8f, true }, p, step));
    CHECK (! w.play ({ 0, 64, 0.8f, true }, p, step));
    CHECK (! w.play ({ 0, 67, 0.8f, true }, p, step));
    CHECK (! w.play ({ 0, 60, 0.0f, false }, p, step) && ! w.play ({ 0, 64, 0.0f, false }, p, step));
    CHECK (w.play ({ 0, 67, 0.0f, false }, p, step));
    CHECK (w.getPosition() == step);
    CHECK (p.notes.size() == 3);
    for (const auto& n : p.notes)
        CHECK (n.start == 0 && n.length == step);
    // the next note goes on the next step, with its velocity
    w.play ({ 0, 62, 0.5f, true }, p, step);
    w.play ({ 0, 62, 0.0f, false }, p, step);
    CHECK (p.notes.size() == 4 && p.notes.back().start == step && p.notes.back().key == 62 && p.notes.back().velocity == 0.5f);
    CHECK (w.getPosition() == 2 * step);
    // the same key on the same step replaces instead of stacking; notes stay sorted by start
    w.reset (step);
    w.play ({ 0, 62, 0.9f, true }, p, step);
    CHECK (p.notes.size() == 4);
    w.play ({ 0, 62, 0.0f, false }, p, step);
    CHECK (std::is_sorted (p.notes.begin(), p.notes.end(), [] (const lynx::Note& a, const lynx::Note& b) { return a.start < b.start; }));
    // a key released that was held before step edit started moves nothing
    Writer fresh;
    fresh.reset (4 * step);
    CHECK (! fresh.play ({ 0, 70, 0.0f, false }, p, step) && fresh.getPosition() == 4 * step);
}

TEST (toolbar170e3_step_edit_and_blend_keys_as_fl)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ 'E', ctrl }, Context::global, false) == "record.stepEdit"); // FL: Options › Step edit Ctrl+E
    CHECK (map.find ({ 'B', ctrl }, Context::global, false) == "record.blend");    // FL: Blend recorded notes Ctrl+B
    CHECK (map.chordsFor ("record.loop").empty() && map.chordsFor ("record.punch").empty());
}

TEST (toolbar170e3_bigger_icons_and_wide_windows)
{
    using namespace lynx::toolbarlayout;
    CHECK (iconMaxPx (IconSize::medium) >= 19.0f);
    CHECK (toolIconPx (IconSize::medium) >= 18.0f);
    // a 4K window at 100 %: the default size grows a step; a size the user picked stays
    CHECK (effectiveIconSize (IconSize::medium, 3840) == IconSize::large);
    CHECK (effectiveIconSize (IconSize::medium, 1920) == IconSize::medium);
    CHECK (effectiveIconSize (IconSize::small, 3840) == IconSize::small);
    CHECK (effectiveIconSize (IconSize::large, 1366) == IconSize::large);
    // spare room becomes air between the panels, at most 28 px each
    CHECK (extraGap (0, 8) == 0 && extraGap (100, 0) == 0);
    CHECK (extraGap (80, 8) == 10);
    CHECK (extraGap (2000, 8) == 28);
}

// --- Round 170e5: Loop record off in Pattern mode (FL's "never" play end, DECOMP 0x11d4800) ---------------------------

#include "core/TransportEdit.h"

TEST (toolbar170e5_loop_record_off_in_pattern_mode_rule)
{
    using lynx::PlayMode;
    CHECK (lynx::recordloop::patternLinear (PlayMode::pattern, false));
    CHECK (! lynx::recordloop::patternLinear (PlayMode::pattern, true)); // on (the default): the pattern loops
    CHECK (! lynx::recordloop::patternLinear (PlayMode::song, false));   // Song mode keeps its own rule
    // the take's pattern starts at the start of the pass it begins in
    CHECK (lynx::recordloop::patternOrigin (0.0, 3840) == 0.0);
    CHECK (lynx::recordloop::patternOrigin (3839.0, 3840) == 0.0);
    CHECK (lynx::recordloop::patternOrigin (3840.0, 3840) == 3840.0);
    CHECK (lynx::recordloop::patternOrigin (9000.0, 3840) == 7680.0);
    CHECK (lynx::recordloop::patternOrigin (-50.0, 3840) == 0.0);
    CHECK (lynx::recordloop::patternOrigin (100.0, 0) == 100.0); // (a zero length is one tick)
}

TEST (toolbar170e5_pattern_plays_once_from_the_take_origin)
{
    lynx::Pattern p;
    p.toggleStep (0, 0); // a note on the pattern's first step (1 bar = 3840 ticks)
    const auto len = p.effectiveLength (lynx::Meter (4, 4));
    const double sr = 48000.0, bpm = 120.0;
    const auto tps = lynx::ticksPerSample (bpm, sr);
    auto ons = [&] (double start, int samples, double origin) {
        lynx::EventBuffer ev;
        lynx::schedulePatternLoop (p, len, start, samples, tps, ev, 0.0f, nullptr, false, origin);
        int n = 0;
        for (auto& e : ev)
            n += e.type == lynx::NoteEvent::Type::on ? 1 : 0;
        return n;
    };
    const auto bar = (double) len;
    // looping (no origin): the first step sounds again at bar 2, and bar 3
    CHECK (ons (bar - 10.0 * tps, 64, -1.0) == 1);
    CHECK (ons (2.0 * bar - 10.0 * tps, 64, -1.0) == 1);
    // Loop record off from bar 1: it plays once, then the position runs on past the end in silence
    CHECK (ons (-10.0 * tps, 64, 0.0) == 1);
    CHECK (ons (bar - 10.0 * tps, 64, 0.0) == 0);
    CHECK (ons (2.0 * bar - 10.0 * tps, 64, 0.0) == 0);
    // ... from bar 2 (the take began in the second pass): the first step sounds at bar 2, not before, not after
    CHECK (ons (bar - 10.0 * tps, 64, bar) == 1);
    CHECK (ons (2.0 * bar - 10.0 * tps, 64, bar) == 0);
    // the same sample as the looping schedule
    {
        lynx::EventBuffer a, b;
        lynx::schedulePatternLoop (p, len, bar - 10.0 * tps, 64, tps, a);
        lynx::schedulePatternLoop (p, len, bar - 10.0 * tps, 64, tps, b, 0.0f, nullptr, false, bar);
        CHECK (a.size() > 0 && b.size() > 0 && a[0].sampleOffset == b[0].sampleOffset);
    }
    // a note recorded past the pattern's (old) end plays too, where it was played
    lynx::Note late;
    late.start = (std::int64_t) bar + 960;
    late.length = 240;
    p.notes.push_back (late);
    CHECK (ons (bar + 960.0 - 10.0 * tps, 64, 0.0) == 1);
}

TEST (toolbar170e5_pattern_take_with_loop_record_off_grows_the_pattern)
{
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::pattern;
    s.patterns[0].notes.clear();
    s.patterns[0].lengthTicks = 0;
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    CHECK (s.patterns[0].effectiveLength (s.meter()) == bar);

    lynx::NoteRecorder recorder;
    recorder.begin (s.patterns[0].effectiveLength (s.meter()));
    recorder.setPatternOrigin (lynx::recordloop::patternOrigin (0.0, bar));
    // one note in bar 1, one in bar 3: the take ran on past the 1-bar pattern's end
    recorder.add (s, { (double) lynx::ppq, 0, 60, 0.8f, true });
    recorder.add (s, { (double) (2 * lynx::ppq), 0, 60, 0.0f, false });
    recorder.add (s, { (double) (2 * bar + lynx::ppq), 0, 62, 0.8f, true });
    recorder.add (s, { (double) (2 * bar + 2 * lynx::ppq), 0, 62, 0.0f, false });
    recorder.end (s, (double) (3 * bar));
    auto notes = recorder.takeFinished();
    CHECK (notes.size() == 2 && notes[1].start == 2 * bar + lynx::ppq); // not folded onto bar 1
    std::vector<int> cleared;
    lynx::TakeQuantize q {};
    q.grid = 0;
    bool first = true;
    lynx::commitNoteBatch (s, recorder, std::move (notes), lynx::RecordMode::overdub, q, cleared, false, first);
    // the pattern is now 3 bars long (its length follows its notes)
    CHECK (s.patterns[0].notes.size() == 2);
    CHECK (s.patterns[0].effectiveLength (s.meter()) == 3 * bar);

    // Loop record on (the default): the same notes fold into the 1-bar loop
    lynx::NoteRecorder folded;
    folded.begin (bar);
    folded.add (s, { (double) (2 * bar + lynx::ppq), 0, 62, 0.8f, true });
    folded.add (s, { (double) (2 * bar + 2 * lynx::ppq), 0, 62, 0.0f, false });
    folded.end (s, (double) (3 * bar));
    const auto f = folded.takeFinished();
    CHECK (f.size() == 1 && f[0].start == lynx::ppq);
    // a take that began in pass 2 counts from bar 2
    lynx::NoteRecorder later;
    later.begin (bar);
    later.setPatternOrigin (lynx::recordloop::patternOrigin ((double) bar + 100.0, bar));
    later.add (s, { (double) (bar + lynx::ppq), 0, 64, 0.8f, true });
    later.end (s, (double) (bar + 2 * lynx::ppq));
    const auto l = later.takeFinished();
    CHECK (l.size() == 1 && l[0].start == lynx::ppq);
    // begin() resets it
    later.begin (bar);
    CHECK (later.getPatternOrigin() < 0.0);
}

TEST (toolbar170e5_playhead_runs_on_past_the_pattern_end)
{
    auto s = lynx::Song::makeDefault();
    s.mode = lynx::PlayMode::pattern;
    s.patterns[0].lengthTicks = 0;
    s.patterns[0].notes.clear();
    const std::int64_t bar = (std::int64_t) lynx::ppq * s.beatsPerBar;
    CHECK (lynx::transportedit::patternPlayhead (s, 0, (double) bar + 10.0) == 10.0);            // looping
    CHECK (lynx::transportedit::patternPlayhead (s, 0, (double) bar + 10.0, 0.0) == (double) bar + 10.0); // take
    CHECK (lynx::transportedit::patternPlayhead (s, 0, (double) bar * 3 + 5.0, (double) bar) == 2.0 * bar + 5.0);
    CHECK (lynx::transportedit::patternPlayhead (s, 0, 10.0, (double) bar) == 10.0); // (before the origin: as before)
}

TEST (toolbar170e5_step_edit_step_is_the_main_snap)
{
    using lynx::stepedit::stepLength;
    CHECK (stepLength (lynx::ppq, 120) == lynx::ppq);                // main snap "Beat": a beat, whatever the editor's
    CHECK (stepLength (lynx::ticksPerStep / 2, 960) == lynx::ticksPerStep / 2);
    CHECK (stepLength (0, 120) == 120);                                // main "Line" / "Events": the Piano roll's grid
    CHECK (stepLength (0, 1) == lynx::ticksPerStep);                   // free on both: a step, not single ticks
    CHECK (stepLength (0, 0) == lynx::ticksPerStep);
}
