// Round natives190kj (Edison): FL Edison's view laws from Edison_x64.dll (core/EdisonView.h) — Dual view strengths
// (0x854030), the time grid step (0x84c470 / 0x84c890 / 0x84c7e0), Snap to grid / to pitch period (0x855d20, period
// 0x62c1e0), grid-slicing (0x8636d0), the Scrub button's speed (0x88c9d0 / 0x8827c0), Detect pitch regions (0x869100 /
// 0x7a9a10) on LYNX's note analysis.

#include "core/EdisonView.h"
#include "core/NewtoneFx.h"

namespace natives190kjedison
{
namespace ev = lynx::edisonview;
namespace ae = lynx::audioedit;

inline ae::Document tone (double seconds, double rate, double hz)
{
    ae::Document d;
    d.audio.sampleRate = rate;
    const auto n = (std::size_t) std::llround (seconds * rate);
    d.audio.channels.assign (1, std::vector<float> (n));
    for (std::size_t i = 0; i < n; ++i)
        d.audio.channels[0][i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / rate);
    return d;
}
} // namespace natives190kjedison

TEST (natives190kj_edison_dual_view_strengths)
{
    namespace ev = lynx::edisonview;
    // no Dual view: one view alone
    CHECK (ev::layers (false, false, 100.0).spectrum == 0 && ev::layers (false, false, 100.0).wave == 256);
    CHECK (ev::layers (true, false, 100.0).spectrum == 256 && ev::layers (true, false, 100.0).wave == 0);
    // Dual view zoomed out far (f → 0): spectrogram view = full spectrogram + waveform at 92; waveform view = 92 under it
    CHECK (ev::layers (true, true, 1.0e9).spectrum == 256 && ev::layers (true, true, 1.0e9).wave == 92);
    CHECK (ev::layers (false, true, 1.0e9).spectrum == 92 && ev::layers (false, true, 1.0e9).wave == 256);
    // zoomed in to 2 samples per pixel or closer (f = 1): only the waveform
    CHECK (ev::layers (true, true, 2.0).spectrum == 0 && ev::layers (true, true, 2.0).wave == 256);
    CHECK (ev::layers (false, true, 1.0).spectrum == 0);
    // in between: f = 2 / 8 = 0.25 → 256 − 64 = 192 and 41 + 92 = 133; waveform view 92 − 23 = 69
    CHECK (ev::layers (true, true, 8.0).spectrum == 192 && ev::layers (true, true, 8.0).wave == 133);
    CHECK (ev::layers (false, true, 8.0).spectrum == 69);
}

TEST (natives190kj_edison_grid_step_law)
{
    namespace ev = lynx::edisonview;
    // seconds: units of half a second; small 0.1 s, medium 1 s, no large
    const auto sec = ev::secondsBase();
    CHECK (std::abs (sec.small - 0.2) < 1e-12 && sec.medium == 2.0 && sec.large == 0.0);
    CHECK (ev::framesPerUnit (false, 44100.0, 0.0) == 22050.0 && ev::framesPerUnit (true, 44100.0, 120.0) == 22050.0);
    // 100 px per unit (0.1 s = 20 px ≥ 12): every small line, a medium one every 10
    auto g = ev::grid (sec, 100.0);
    CHECK (std::abs (g.step - 0.2) < 1e-12 && g.mediumEvery == 10 && g.largeEvery == 0);
    // 40 px per unit: 0.1 s = 8 px is too dense; m = 2 (0.2 s = 16 px; 2 divides 10)
    g = ev::grid (sec, 40.0);
    CHECK (std::abs (g.step - 0.4) < 1e-12 && g.mediumEvery == 5);
    // 15 px per unit: m = 3, 4 do not divide 10; m = 5 (0.5 s = 37.5 px)
    g = ev::grid (sec, 15.0);
    CHECK (std::abs (g.step - 1.0) < 1e-12 && g.mediumEvery == 2);
    // 1 px per unit: even 10 small steps (2 px) are too dense → doubled: 20, 40, 80 → 16 units = 16 px
    g = ev::grid (sec, 1.0);
    CHECK (std::abs (g.step - 16.0) < 1e-9 && g.mediumEvery == 1);
    // beats (4 steps per beat, 16 per bar): small 1/4, medium 1, large 4
    const auto beats = ev::beatsBase (16, 4);
    CHECK (beats.small == 0.25 && beats.medium == 1.0 && beats.large == 4.0);
    g = ev::grid (beats, 60.0); // a step = 15 px
    CHECK (g.step == 0.25 && g.mediumEvery == 4 && g.largeEvery == 16);
    CHECK (ev::lineClass (0, g) == 2 && ev::lineClass (4, g) == 1 && ev::lineClass (5, g) == 0 && ev::lineClass (32, g) == 2);
    g = ev::grid (beats, 30.0); // 1/4 = 7.5 px → m = 2 (half beats, 15 px)
    CHECK (g.step == 0.5 && g.mediumEvery == 2 && g.largeEvery == 8);
    g = ev::grid (beats, 2.0); // a bar = 8 px → doubled to 2 bars (16 px)
    CHECK (g.step == 8.0 && g.largeEvery == 1);
    // a tempo-synced sample of 10 … 999 BPM runs in beats
    lynx::audioedit::SampleProps p;
    CHECK (! ev::beatMode (p));
    p.tempoSync = true;
    p.tempo = 128.0;
    CHECK (ev::beatMode (p));
    p.tempo = 5.0;
    CHECK (! ev::beatMode (p));
    // Snap to grid: round((x − offset) / step) · step + offset (halves to even, as Delphi's Round)
    CHECK (ev::snapToStep (0.31, 0.2, 0.0) == 0.4 && ev::snapToStep (0.29, 0.2, 0.0) == 0.2);
    CHECK (ev::snapToStep (1.3, 1.0, 0.25) == 1.25 && ev::snapToStep (0.5, 1.0, 0.0) == 0.0 && ev::snapToStep (1.5, 1.0, 0.0) == 2.0);
}

TEST (natives190kj_edison_snap_to_pitch_period)
{
    namespace ev = lynx::edisonview;
    // Edison's note 57 ("A4") = 440 Hz: 44100 / 440 = 100.227 frames
    CHECK (std::abs (ev::pitchPeriodFrames (44100.0, 57, 0.0) - 44100.0 / 440.0) < 1e-9);
    // the default middle note C5 (60) = 523.25 Hz; +100 cents = one semitone higher
    CHECK (std::abs (ev::pitchPeriodFrames (48000.0, 60, 0.0) - 48000.0 / (440.0 * std::exp2 (3.0 / 12.0))) < 1e-9);
    CHECK (std::abs (ev::pitchPeriodFrames (48000.0, 60, 100.0) - ev::pitchPeriodFrames (48000.0, 61, 0.0)) < 1e-9);
    const auto period = ev::pitchPeriodFrames (44100.0, 57, 0.0);
    CHECK (ev::snapToPeriod (1000, period) == (std::int64_t) std::nearbyint (10.0 * period)); // 1002
    CHECK (ev::snapToPeriod (40, period) == 0 && ev::snapToPeriod (60, period) == 100);
}

TEST (natives190kj_edison_grid_slicing)
{
    namespace ev = lynx::edisonview;
    namespace ae = lynx::audioedit;
    const auto sec = ev::secondsBase();
    CHECK (std::abs (ev::sliceStep (sec, 0) - 0.2) < 1e-12 && ev::sliceStep (sec, 1) == 2.0 && ev::sliceStep (sec, 2) == 2.0); // (no large: medium)
    const auto beats = ev::beatsBase (16, 4);
    CHECK (ev::sliceStep (beats, 2) == 4.0);
    // markers from the start, every step, while ≤ the end (inclusive)
    const auto pos = ev::gridSlicePositions (100, 400, 100.0);
    CHECK (pos.size() == 4 && pos.front() == 100 && pos.back() == 400);
    CHECK (ev::gridSlicePositions (0, 10, 0.0).empty());
    // a 2-second sample, medium grid-slicing over the whole: markers at 0 s, 1 s (and the old plain markers in it go;
    // regions stay)
    auto d = natives190kjedison::tone (2.0, 1000.0, 50.0);
    ae::addMarker (d, 500, "old");
    ae::addRegion (d, { 100, 300 }, "keep");
    const auto n = ev::gridSlice (d, 0, d.audio.frames() - 1, ev::sliceStep (sec, 1) * ev::framesPerUnit (false, 1000.0, 0.0));
    CHECK (n == 2);
    int plain = 0, regions = 0;
    for (const auto& m : d.markers)
        m.isRegion() ? ++regions : ++plain;
    CHECK (plain == 2 && regions == 1);
    CHECK (std::none_of (d.markers.begin(), d.markers.end(), [] (const ae::Marker& m) { return m.name == "old"; }));
}

TEST (natives190kj_edison_scrub_speed)
{
    namespace ev = lynx::edisonview;
    // a press starts paused; 40 px right = normal speed forward; left goes backward; held in ±16
    auto s = ev::scrubSpeed (0.0, 40.0);
    CHECK (std::abs (s - 1.0) < 1e-12);
    s = ev::scrubSpeed (s, -80.0);
    CHECK (std::abs (s + 1.0) < 1e-12);
    CHECK (ev::scrubSpeed (0.0, 10000.0) == 16.0 && ev::scrubSpeed (0.0, -10000.0) == -16.0);
    CHECK (std::abs (ev::scrubSpeed (0.0, 40.0, 2.0) - 0.5) < 1e-12); // (FL divides by its UI scale)
    CHECK (ev::scrubText (0.0) == "Paused");
    CHECK (ev::scrubText (1.25).find ("1.2 x") != std::string::npos || ev::scrubText (1.25).find ("1.3 x") != std::string::npos);
    CHECK (ev::scrubText (-2.0).find ("2.0 x") != std::string::npos && ev::scrubText (-2.0) != ev::scrubText (2.0));
}

TEST (natives190kj_edison_pitch_regions)
{
    namespace ev = lynx::edisonview;
    namespace ae = lynx::audioedit;
    CHECK (ev::regionNoteName (60) == "C5" && ev::regionNoteName (69) == "A5" && ev::regionNoteName (0) == "C0" && ev::regionNoteName (61) == "C#5");
    // segments → named regions; out-of-range pitches skipped; every old marker goes
    ae::Document d = natives190kjedison::tone (1.0, 1000.0, 50.0);
    ae::addMarker (d, 10);
    const auto n = ev::pitchRegions (d, { { 0, 400, 69.2 }, { 400, 900, 71.6 }, { 900, 950, -3.0 } });
    CHECK (n == 2 && d.markers.size() == 2);
    CHECK (d.markers[0].name == "A5" && d.markers[0].start == 0 && d.markers[0].end == 400);
    CHECK (d.markers[1].name == "C6" && d.markers[1].start == 400);
    // on LYNX's note analysis: two sung-like notes (A3 then C4, 0.6 s each) give two regions named after them
    lynx::SampleData s;
    s.sampleRate = 44100.0;
    const auto len = (std::size_t) (1.2 * 44100.0);
    s.left.resize (len);
    double phase = 0.0;
    for (std::size_t i = 0; i < len; ++i)
    {
        const auto hz = i < len / 2 ? 220.0 : 261.6256;
        phase += 2.0 * 3.141592653589793 * hz / 44100.0;
        s.left[i] = (float) (0.4 * std::sin (phase) + 0.2 * std::sin (2.0 * phase) + 0.1 * std::sin (3.0 * phase));
    }
    const auto track = lynx::newtonefx::analyseNotes (s);
    std::vector<ev::PitchSegment> seg;
    for (const auto& note : track.pitches)
        seg.push_back ({ note.origStart, note.origEnd, note.origPitch });
    ae::Document doc;
    doc.audio.sampleRate = 44100.0;
    doc.audio.channels = { s.left };
    const auto found = ev::pitchRegions (doc, seg);
    CHECK (found >= 2);
    CHECK (std::any_of (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.name == "A4"; }));
    CHECK (std::any_of (doc.markers.begin(), doc.markers.end(), [] (const ae::Marker& m) { return m.name == "C5"; }));
}
