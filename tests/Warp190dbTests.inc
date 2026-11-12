// --- Round 190db: the audio editor's beat-time view of a warped clip (core/WarpView.h). The model the editor draws
// with: beat ↔ file time through the markers, what a held marker drag draws (plain: stretch, Shift: slide, as Live
// 12.4.16 does them, live check 190cw shot warp190cg-09), the ruler's labels, and the cost of a redraw while a marker is
// dragged on a five-minute clip.

#include <chrono>

#include "core/WarpView.h"
#include "core/WaveAnalysis.h"

namespace warp190db
{
inline bool near (double a, double b, double tolerance = 1.0e-9) { return std::abs (a - b) <= tolerance; }
// 2 s at 1000 Hz; the first beat played faster than the rest (beat 1 at 0.4 s), then 120 bpm
inline std::vector<lynx::WarpPoint> bent() { return { { 0.0, 0.0 }, { 0.4, 1.0 }, { 1.0, 2.0 }, { 2.0, 4.0 } }; }
inline std::vector<lynx::WarpPoint> even() { return { { 0.0, 0.0 }, { 0.5, 1.0 }, { 1.0, 2.0 }, { 2.0, 4.0 } }; }
} // namespace warp190db

TEST (warp190db_beat_axis_maps_file_time_through_the_markers)
{
    using namespace lynx;
    const auto w = warp190db::bent();
    const auto axis = warpview::makeAxis (w, 2000.0, 1000.0);
    CHECK (axis.valid());
    CHECK (warp190db::near (axis.beat0, 0.0));
    CHECK (warp190db::near (axis.unitsPerBeat, 500.0)); // 4 beats over 2000 units: the file spans [0, frames]
    CHECK (warp190db::near (axis.unitOfFrame (0.0), 0.0));
    CHECK (warp190db::near (axis.unitOfFrame (2000.0), 2000.0));
    CHECK (warp190db::near (axis.unitOfFrame (400.0), 500.0));  // the marker at 0.4 s is drawn on beat 1
    CHECK (warp190db::near (axis.unitOfFrame (700.0), 750.0));  // half-way through its segment: beat 1.5
    CHECK (warp190db::near (axis.unitOfFrame (200.0), 250.0));  // the fast first beat is drawn wider than file time
    CHECK (warp190db::near (axis.frameOfUnit (750.0), 700.0));
    CHECK (warp190db::near (axis.beatOfFrame (2500.0), 5.0)); // straight beyond the end marker
    // the binary search answers as warpedit's walk does, on and between the markers and outside them
    bool same = true;
    for (int k = -50; k <= 2600; ++k)
    {
        const auto seconds = k / 1000.0;
        same = same && warp190db::near (warpview::beatAt (w, seconds), warpedit::beatAt (w, seconds), 1.0e-12);
        const auto beats = k / 500.0;
        same = same && warp190db::near (warpview::secondsAt (w, beats), warpedit::secondsAt (w, beats), 1.0e-12);
        same = same && warp190db::near (axis.frameOfUnit (axis.unitOfFrame ((double) k)), (double) k, 1.0e-6);
    }
    CHECK (same);
    // Set 1.1.1 here moved beat 0 to 0.4 s: unit 0 is still the file's start, the ruler counts from the bar before
    auto shifted = w;
    for (auto& m : shifted)
        m.beats -= 1.0;
    const auto later = warpview::makeAxis (shifted, 2000.0, 1000.0);
    CHECK (warp190db::near (later.beat0, -1.0));
    CHECK (warp190db::near (later.unitOfFrame (400.0), 500.0));
    CHECK (warpview::beatLabel (later.beatOfUnit (0.0), 4) == "0.4");
    // too few markers, or none moving forward: no beat view
    CHECK (! warpview::makeAxis ({ { 0.0, 0.0 } }, 2000.0, 1000.0).valid());
    CHECK (! warpview::makeAxis ({ { 0.0, 1.0 }, { 2.0, 1.0 } }, 2000.0, 1000.0).valid());
}

TEST (warp190db_ruler_labels_and_steps_as_live)
{
    using namespace lynx;
    // Live's Clip View ruler: "1", "1.2", "1.3", "1.4", "2" … and sixteenths as "1.2.3"
    CHECK (warpview::beatLabel (0.0, 4) == "1");
    CHECK (warpview::beatLabel (1.0, 4) == "1.2");
    CHECK (warpview::beatLabel (3.0, 4) == "1.4");
    CHECK (warpview::beatLabel (4.0, 4) == "2");
    CHECK (warpview::beatLabel (1.75, 4) == "1.2.4");
    CHECK (warpview::beatLabel (5.25, 4) == "2.2.2");
    CHECK (warpview::beatLabel (3.0, 3) == "2");
    CHECK (warpview::beatLabel (-1.0, 4) == "0.4");
    CHECK (warpview::beatLabel (0.9999999, 4) == "1.2"); // (a step sum that falls a hair short)
    CHECK (warp190db::near (warpview::rulerStep (300.0, 4), 0.25));
    CHECK (warp190db::near (warpview::rulerStep (150.0, 4), 0.5));
    CHECK (warp190db::near (warpview::rulerStep (140.0, 4), 1.0)); // Live's shot 09: one label a beat
    CHECK (warp190db::near (warpview::rulerStep (60.0, 4), 4.0));
    CHECK (warp190db::near (warpview::rulerStep (20.0, 4), 4.0));
    CHECK (warp190db::near (warpview::rulerStep (5.0, 4), 16.0));
    // the view keeps its left beat and its beats per pixel when the axis changes (a committed drag of the end marker)
    const auto a = warpview::makeAxis (warp190db::even(), 2000.0, 1000.0);
    auto longer = warp190db::even();
    longer.back().beats = 5.0;
    const auto b = warpview::makeAxis (longer, 2000.0, 1000.0);
    double viewStart = 600.0, upp = 2.0;
    const auto leftBeat = a.beatOfUnit (viewStart), beatsPerPixel = upp / a.unitsPerBeat;
    warpview::keepView (a, b, viewStart, upp);
    CHECK (warp190db::near (b.beatOfUnit (viewStart), leftBeat));
    CHECK (warp190db::near (upp / b.unitsPerBeat, beatsPerPixel));
}

TEST (warp190db_drags_draw_stretch_and_slide_as_live)
{
    using namespace lynx;
    const auto before = warp190db::even();
    const auto axis = warpview::makeAxis (before, 2000.0, 1000.0); // 500 units a beat
    // a plain drag of marker 1 (0.5 s, beat 1) to beat 1.5: its audio goes with it, both segments around it stretch
    const auto stretched = warpview::withScaleOf (axis, warpview::stretchPreview (before, 1, 1.5));
    CHECK (warp190db::near (stretched.unitOfFrame (500.0), 750.0));  // the marker's attack is drawn under the pointer
    CHECK (warp190db::near (stretched.unitOfFrame (250.0), 375.0));  // the segment before: 1 beat of audio over 1.5
    CHECK (warp190db::near (stretched.unitOfFrame (750.0), 875.0));  // the segment after: squeezed into half a beat
    CHECK (warp190db::near (stretched.unitOfFrame (1000.0), 1000.0)); // the neighbours stay
    CHECK (warp190db::near (stretched.unitOfFrame (1500.0), 1500.0)); // and so does everything past them
    CHECK (warp190db::near (stretched.beatOfUnit (750.0), axis.beatOfUnit (750.0))); // the pointer's beat doesn't move
    // what the drag draws is what the release commits
    {
        SamplerSettings s;
        s.warp = before;
        s.keepPitch = true;
        warpedit::moveMarker (s, 1, 1.5, 2.0);
        CHECK (s.warp == stretched.markers);
    }
    // past a neighbour: held just before it, as the commit holds it
    CHECK (warp190db::near (warpview::stretchPreview (before, 1, 9.0)[1].beats, 2.0 - warpedit::minBeatGap));
    CHECK (warp190db::near (warpview::stretchPreview (before, 1, -3.0)[1].beats, warpedit::minBeatGap));

    // Shift + drag of marker 1 a sixteenth to the right: the marker stays on beat 1, the audio slides right under it
    const auto seconds = warpview::slideDragSeconds (before, 1, 1.0, 1.25);
    CHECK (warp190db::near (seconds, 0.375)); // the audio a sixteenth earlier now plays on beat 1
    const auto slid = warpview::withScaleOf (axis, warpview::slidePreview (before, 1, seconds));
    CHECK (warp190db::near (slid.unitOfFrame (375.0), 500.0));                          // the marker keeps its place
    CHECK (warp190db::near (slid.unitOfFrame (500.0), 500.0 + 500.0 * 0.125 / 0.625)); // the attack is drawn later (beat 1.2)
    CHECK (slid.unitOfFrame (500.0) > axis.unitOfFrame (500.0));
    CHECK (warp190db::near (slid.unitOfFrame (250.0), 500.0 * 0.25 / 0.375)); // the audio before it stretches (Live's slowed decay)
    CHECK (warp190db::near (slid.unitOfFrame (1000.0), 1000.0));
    {
        SamplerSettings s;
        s.warp = before;
        s.keepPitch = true;
        warptools::slideMarker (s, 1, seconds);
        CHECK (s.warp == slid.markers);
    }
    // to the left the audio after the marker comes under it: the attack is drawn earlier
    const auto left = warpview::withScaleOf (axis, warpview::slidePreview (before, 1, warpview::slideDragSeconds (before, 1, 1.0, 0.75)));
    CHECK (left.unitOfFrame (500.0) < axis.unitOfFrame (500.0));
    // held between the neighbours' file times
    CHECK (warp190db::near (warpview::slideDragSeconds (before, 1, 1.0, 9.0), warpedit::minSecondGap));
    // the start marker never moves
    CHECK (warpview::stretchPreview (before, 0, 3.0) == before);
    CHECK (warpview::slidePreview (before, 0, 0.3) == before);
}

TEST (warp190db_drag_redraw_on_a_five_minute_clip_is_cheap)
{
    using namespace lynx;
    // five minutes at 44.1 kHz with an attack on every beat, warped with a marker on each of its 600 beats (a little
    // off 120 bpm, as an auto-warp of a played take leaves them)
    const double rate = 44100.0;
    const auto frames = (std::size_t) (300.0 * rate);
    std::vector<float> audio (frames);
    for (std::size_t i = 0; i < frames; ++i)
    {
        const auto inBeat = (double) (i % 22050) / rate;
        audio[i] = (float) (std::sin ((double) i * 0.07) * std::exp (-inBeat * 12.0) * 0.8);
    }
    const auto summary = wave::analyse (audio.data(), nullptr, frames, rate);
    std::vector<WarpPoint> w;
    for (int beat = 0; beat <= 600; ++beat)
        w.push_back ({ beat * 0.5 + (beat % 2 == 1 ? 0.012 : 0.0), (double) beat });
    w.back().seconds = 300.0;
    const auto peak = [&] (double from, double to, float& lo, float& hi) {
        if (to <= 0.0 || from >= (double) frames)
            return false;
        if (to - from > 256.0) // the editor's choice: the summary for wide stretches, else the samples
        {
            const auto p = summary.query (from, to, -1);
            lo = p.lo;
            hi = p.hi;
            return p.valid;
        }
        auto a = std::clamp<std::int64_t> ((std::int64_t) std::floor (from), 0, (std::int64_t) frames - 1);
        const auto b = std::clamp<std::int64_t> ((std::int64_t) std::ceil (to), a + 1, (std::int64_t) frames);
        lo = hi = audio[(std::size_t) a];
        for (auto i = a + 1; i < b; ++i)
        {
            lo = std::min (lo, audio[(std::size_t) i]);
            hi = std::max (hi, audio[(std::size_t) i]);
        }
        return true;
    };
    const auto base = warpview::makeAxis (w, (double) frames, rate);
    CHECK (base.valid());
    const int width = 1600;
    struct View
    {
        const char* name;
        double viewStart, upp;
        int marker;
    };
    // the whole clip in view, and 8 beats (zoomed in: each column is measured from the samples)
    const View views[] { { "whole", 0.0, (double) frames / width, 300 }, { "8 beats", base.unitOfBeat (296.0), base.unitsPerBeat * 8.0 / width, 299 } };
    for (const auto& v : views)
    {
        for (const auto slides : { false, true })
        {
            warpview::Columns cached;
            using clock = std::chrono::steady_clock;
            auto t0 = clock::now();
            const auto cold = warpview::columnPeaks (base, v.viewStart, v.upp, width, 1, cached, peak);
            const auto coldMs = std::chrono::duration<double, std::milli> (clock::now() - t0).count();
            CHECK (cold == width);
            // 60 moves of a held drag: the pointer walks across the marker's neighbourhood
            int worst = 0;
            bool inside = true;
            const auto& m = w[(std::size_t) v.marker];
            const auto spanColumns = (base.unitOfBeat (w[(std::size_t) v.marker + 1].beats) - base.unitOfBeat (w[(std::size_t) v.marker - 1].beats)) / v.upp;
            warpview::BeatAxis shown = base;
            t0 = clock::now();
            for (int step = 0; step < 60; ++step)
            {
                const auto pointerBeat = m.beats - 0.6 + 1.2 * step / 59.0;
                const auto preview = slides ? warpview::slidePreview (w, v.marker, warpview::slideDragSeconds (w, v.marker, m.beats, pointerBeat))
                                            : warpview::stretchPreview (w, v.marker, pointerBeat);
                shown = warpview::withScaleOf (base, preview);
                const auto measured = warpview::columnPeaks (shown, v.viewStart, v.upp, width, 1, cached, peak);
                worst = std::max (worst, measured);
                inside = inside && measured <= (int) std::ceil (spanColumns) + 2;
            }
            const auto dragMs = std::chrono::duration<double, std::milli> (clock::now() - t0).count() / 60.0;
            // only the two segments around the marker are measured again, and a move costs well under a frame
            CHECK (inside);
            CHECK (dragMs < 4.0);
            CHECK (coldMs < 40.0);
            // the kept columns are what a fresh measurement gives
            warpview::Columns fresh;
            warpview::columnPeaks (shown, v.viewStart, v.upp, width, 1, fresh, peak);
            CHECK (fresh.lo == cached.lo && fresh.hi == cached.hi && fresh.valid == cached.valid);
            std::printf ("  warp190db redraw (%s, %s): cold %.2f ms, per drag move %.3f ms, at most %d of %d columns measured again\n", v.name,
                         slides ? "Shift+drag" : "drag", coldMs, dragMs, worst, width);
        }
    }
}
