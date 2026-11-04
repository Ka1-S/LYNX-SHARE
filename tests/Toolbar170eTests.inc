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
    recorder.begin (s.patterns[0].effectiveLength (s.beatsPerBar));
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
