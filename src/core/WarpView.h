#pragma once

// The audio editor's beat-time view of a warped clip (round 190db; Ableton Live 12.4.16's Clip View draws a warped
// clip in beats: ruler "1, 1.2, 1.3 …", a plain marker drag stretches the audio on both sides, Shift + drag slides the
// audio under the marker — live check 190cw shot warp190cg-09). JUCE-free and header-only, so the tests check the
// model rather than pixels.
//
// The view's horizontal axis is in "units": content beats, scaled so the whole file spans [0, frames] units. The
// editor's view position and zoom (viewStart, frames-per-pixel) then keep their meaning and range, and file time
// becomes one more mapping: a frame → its beat through the warp markers (SamplerSettings::warp, file seconds →
// content beats, straight beyond the end markers like warpedit::beatAt) → its unit. Everything the editor keeps in
// file frames (the selection, markers, loop, envelopes, the cursor) is drawn through that mapping, so the edits keep
// acting on the audio they show.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"
#include "core/WarpEdit.h"

namespace lynx::warpview
{
// --- beat ↔ file time (binary search; the warpedit versions walk the markers) ----------------------------------------
inline double beatAt (const std::vector<WarpPoint>& w, double seconds) noexcept
{
    if (w.size() < 2)
        return seconds;
    // the segment [i-1, i]: the first marker past `seconds` (the last segment beyond the end, the first before the start)
    auto i = (std::size_t) (std::upper_bound (w.begin() + 1, w.end() - 1, seconds, [] (double t, const WarpPoint& p) { return t <= p.seconds; }) - w.begin());
    const auto& a = w[i - 1];
    const auto& b = w[i];
    return a.beats + (seconds - a.seconds) * (b.beats - a.beats) / std::max (1.0e-12, b.seconds - a.seconds);
}
inline double secondsAt (const std::vector<WarpPoint>& w, double beats) noexcept
{
    if (w.size() < 2)
        return beats;
    auto i = (std::size_t) (std::upper_bound (w.begin() + 1, w.end() - 1, beats, [] (double b, const WarpPoint& p) { return b <= p.beats; }) - w.begin());
    const auto& a = w[i - 1];
    const auto& b = w[i];
    return a.seconds + (beats - a.beats) * (b.seconds - a.seconds) / std::max (1.0e-12, b.beats - a.beats);
}

// The axis of the view: markers, the file (frames at rate), where unit 0 is (beat0) and units per beat.
struct BeatAxis
{
    std::vector<WarpPoint> markers;
    double rate = 44100.0, frames = 0.0;
    double beat0 = 0.0, unitsPerBeat = 1.0;

    bool valid() const noexcept { return markers.size() >= 2 && frames > 0.0 && rate > 0.0 && unitsPerBeat > 0.0; }
    double beatOfFrame (double frame) const noexcept { return beatAt (markers, frame / rate); }
    double frameOfBeat (double beat) const noexcept { return secondsAt (markers, beat) * rate; }
    double unitOfBeat (double beat) const noexcept { return (beat - beat0) * unitsPerBeat; }
    double beatOfUnit (double unit) const noexcept { return beat0 + unit / unitsPerBeat; }
    double unitOfFrame (double frame) const noexcept { return unitOfBeat (beatOfFrame (frame)); }
    double frameOfUnit (double unit) const noexcept { return frameOfBeat (beatOfUnit (unit)); }
};

// The axis of markers `w` over a file of `frames` at `rate`: the file's start is unit 0 and its end unit `frames`.
// Invalid (valid() false) for fewer than two markers or markers that don't move forward in beats over the file.
inline BeatAxis makeAxis (const std::vector<WarpPoint>& w, double frames, double rate)
{
    BeatAxis a;
    a.markers = w;
    a.frames = frames;
    a.rate = rate;
    if (w.size() < 2 || ! (frames > 0.0) || ! (rate > 0.0))
    {
        a.unitsPerBeat = 0.0;
        return a;
    }
    a.beat0 = beatAt (w, 0.0);
    const auto span = beatAt (w, frames / rate) - a.beat0;
    a.unitsPerBeat = span > 1.0e-9 ? frames / span : 0.0;
    return a;
}

// The same scale as `fixed` (where unit 0 is, units per beat) with other markers: what a drag draws with, so the
// pointer's beat and every marker it does not touch stay where they are on screen while the markers change.
inline BeatAxis withScaleOf (const BeatAxis& fixed, const std::vector<WarpPoint>& w)
{
    auto a = fixed;
    a.markers = w;
    return a;
}

// Where the view stands after the axis changes from `from` to `to` (markers edited, the view switched on or off):
// the beat at the left edge and the beats per pixel are kept. viewStart / unitsPerPixel are in `from`'s units.
inline void keepView (const BeatAxis& from, const BeatAxis& to, double& viewStart, double& unitsPerPixel) noexcept
{
    if (! from.valid() || ! to.valid())
        return;
    const auto leftBeat = from.beatOfUnit (viewStart);
    const auto beatsPerPixel = unitsPerPixel / from.unitsPerBeat;
    viewStart = to.unitOfBeat (leftBeat);
    unitsPerPixel = beatsPerPixel * to.unitsPerBeat;
}

// --- marker drags (the model of what the view draws while a drag is held) ------------------------------------------
// A plain drag: marker `index` on beat `beat` (between its neighbours, as warpedit::moveMarker keeps it); its audio
// goes with it, so both segments around it stretch.
inline std::vector<WarpPoint> stretchPreview (const std::vector<WarpPoint>& before, int index, double beat)
{
    auto w = before;
    if (index <= 0 || index >= (int) w.size())
        return w;
    const auto lo = w[(std::size_t) index - 1].beats + warpedit::minBeatGap;
    const auto hi = index + 1 < (int) w.size() ? w[(std::size_t) index + 1].beats - warpedit::minBeatGap : 1.0e6;
    w[(std::size_t) index].beats = std::clamp (beat, lo, std::max (lo, hi));
    return w;
}
// Shift + drag: marker `index` keeps its beat and is tied to file time `seconds` (between its neighbours, as
// warptools::slideMarker keeps it): the audio slides under it.
inline std::vector<WarpPoint> slidePreview (const std::vector<WarpPoint>& before, int index, double seconds)
{
    auto w = before;
    if (index <= 0 || index >= (int) w.size())
        return w;
    const auto lo = w[(std::size_t) index - 1].seconds + warpedit::minSecondGap;
    const auto hi = index + 1 < (int) w.size() ? w[(std::size_t) index + 1].seconds - warpedit::minSecondGap : 1.0e9;
    w[(std::size_t) index].seconds = std::clamp (seconds, lo, std::max (lo, hi));
    return w;
}
// The file time a Shift + drag ties the marker to: the audio follows the pointer. The pointer went from beat `downBeat`
// to `pointerBeat` on the screen (fixed scale); the audio under the marker moves by the file time that stretch of the
// screen held before the drag (to the right: earlier audio comes under the marker, so its attack is drawn later).
inline double slideDragSeconds (const std::vector<WarpPoint>& before, int index, double downBeat, double pointerBeat)
{
    if (index <= 0 || index >= (int) before.size())
        return 0.0;
    const auto moved = secondsAt (before, pointerBeat) - secondsAt (before, downBeat);
    return slidePreview (before, index, before[(std::size_t) index].seconds - moved)[(std::size_t) index].seconds;
}

// --- the ruler ----------------------------------------------------------------------------------------------------
// Live's labels: "1" on a bar, "1.2" on a beat, "1.2.3" on a sixteenth (content beat 0 = 1.1.1; bars before it count
// down from 0).
inline std::string beatLabel (double beat, int beatsPerBar)
{
    const auto perBar = std::max (1, beatsPerBar);
    const auto sixteenths = (long long) std::floor (beat * 4.0 + 1.0e-6);
    const auto perBarSixteenths = (long long) perBar * 4;
    const auto bar = (long long) std::floor ((double) sixteenths / (double) perBarSixteenths);
    const auto inBar = sixteenths - bar * perBarSixteenths;
    const auto beatInBar = inBar / 4, sixteenth = inBar % 4;
    auto text = std::to_string (bar + 1);
    if (beatInBar != 0 || sixteenth != 0)
        text += "." + std::to_string (beatInBar + 1);
    if (sixteenth != 0)
        text += "." + std::to_string (sixteenth + 1);
    return text;
}
// The labelled step in beats for `pixelsPerBeat`: the smallest of 1/4 beat, 1/2, 1 beat, a bar, 2, 4, 8 … bars that
// leaves at least `minPixels` between labels (Live's shot 09: a label on each beat at about 140 px a beat, none between).
inline double rulerStep (double pixelsPerBeat, int beatsPerBar, double minPixels = 72.0) noexcept
{
    const auto perBar = (double) std::max (1, beatsPerBar);
    if (! (pixelsPerBeat > 0.0))
        return perBar;
    for (const auto step : { 0.25, 0.5, 1.0 })
        if (step * pixelsPerBeat >= minPixels)
            return step;
    auto step = perBar;
    while (step * pixelsPerBeat < minPixels && step < 1.0e6)
        step *= 2.0;
    return step;
}

// --- the waveform's columns ----------------------------------------------------------------------------------------
// Each pixel column of the view covers a stretch of the file: [frameOfUnit(left edge), frameOfUnit(right edge)). Its
// min / max are kept with that stretch, and a column whose stretch did not change since the last paint keeps them: a
// marker drag only changes the two segments around the marker, so only their columns are measured again.
struct Columns
{
    double viewStart = 0.0, unitsPerPixel = 0.0;
    int width = 0;
    std::uint64_t version = 0; // the audio's version (an edit measures everything again)
    std::vector<double> from, to;
    std::vector<float> lo, hi;
    std::vector<char> valid;
    void clear() noexcept { width = 0; }
};

// Fills `c` for the view (viewStart, unitsPerPixel, width) of audio version `version`. peak (fromFrame, toFrame,
// float& lo, float& hi) -> bool measures one stretch (false: past the audio). Returns how many columns were measured.
template <class PeakFn>
int columnPeaks (const BeatAxis& axis, double viewStart, double unitsPerPixel, int width, std::uint64_t version, Columns& c, PeakFn&& peak)
{
    width = std::max (0, width);
    const auto same = c.width == width && c.viewStart == viewStart && c.unitsPerPixel == unitsPerPixel && c.version == version
                      && (int) c.from.size() == width;
    if (! same)
    {
        c.viewStart = viewStart;
        c.unitsPerPixel = unitsPerPixel;
        c.width = width;
        c.version = version;
        c.from.assign ((std::size_t) width, -1.0);
        c.to.assign ((std::size_t) width, -1.0);
        c.lo.assign ((std::size_t) width, 0.0f);
        c.hi.assign ((std::size_t) width, 0.0f);
        c.valid.assign ((std::size_t) width, 0);
    }
    int measured = 0;
    auto left = axis.frameOfUnit (viewStart);
    for (int x = 0; x < width; ++x)
    {
        const auto right = axis.frameOfUnit (viewStart + (double) (x + 1) * unitsPerPixel);
        const auto k = (std::size_t) x;
        if (c.from[k] != left || c.to[k] != right)
        {
            float lo = 0.0f, hi = 0.0f;
            c.valid[k] = peak (left, right, lo, hi) ? 1 : 0;
            c.lo[k] = lo;
            c.hi[k] = hi;
            c.from[k] = left;
            c.to[k] = right;
            ++measured;
        }
        left = right;
    }
    return measured;
}
} // namespace lynx::warpview
