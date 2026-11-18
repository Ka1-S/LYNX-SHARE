// Round 190li: Newtone's natural engine (Track::engine 1), the pitch modulation / drift split, the sung pitch centre,
// the analysis octave guard and native layout 4. The quality scores against Melodyne 5 are in tools/vocallab (the
// round's lab); these tests pin the behaviour the scores rest on.

#include "core/NewtoneFx.h"
#include "core/VocalPitch.h"

namespace vocal190li
{
// a sung tone: harmonics of f0 (optionally with vibrato, cents) through a vowel resonance, 20 ms fades; `spec` =
// (MIDI note or 0 for a rest, seconds); `formant` = the resonance's centre (Hz)
lynx::SampleData line (const std::vector<std::pair<double, double>>& spec, double vibratoCents = 0.0, double formant = 700.0,
                       double sampleRate = 48000.0)
{
    lynx::SampleData s;
    s.sampleRate = sampleRate;
    double ph = 0.0;
    for (const auto& [midi, seconds] : spec)
    {
        const auto n = (int) (seconds * sampleRate);
        const auto f0 = midi > 0.0 ? 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) : 0.0;
        const auto ramp = (int) (0.02 * sampleRate);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            if (f0 > 0.0)
            {
                const auto f = f0 * std::pow (2.0, vibratoCents * std::sin (2.0 * M_PI * 5.5 * i / sampleRate) / 1200.0);
                ph += 2.0 * M_PI * f / sampleRate;
                for (int k = 1; k * f < 8000.0; ++k)
                {
                    const auto fk = k * f;
                    const auto res = 1.0 / std::sqrt (1.0 + std::pow ((fk - formant) / 120.0, 2.0)) + 0.1;
                    v += (float) (0.2 * res / (1.0 + fk / 400.0) * std::sin (k * ph));
                }
                v *= (float) std::min ({ 1.0, i / (double) ramp, (n - i) / (double) ramp });
            }
            s.left.push_back (v);
        }
    }
    return s;
}

// the median pitch (MIDI) of x over [a, b) seconds, and the spread (cents, standard deviation)
std::pair<double, double> pitchOver (const lynx::SampleData& x, double a, double b)
{
    const auto fs = x.sampleRate;
    const auto frames = lynx::vocal::analysePitch (x.leftData(), (int) x.frames(), fs, 128);
    std::vector<double> v;
    for (std::size_t k = 0; k < frames.size(); ++k)
        if (const auto t = (double) k * 128 / fs; t >= a && t < b && frames[k].hz > 0.0)
            v.push_back (lynx::vocal::hzToNote (frames[k].hz));
    if (v.empty())
        return { 0.0, 0.0 };
    auto sorted = v;
    std::sort (sorted.begin(), sorted.end());
    double mean = 0.0, var = 0.0;
    for (const auto p : v)
        mean += p / (double) v.size();
    for (const auto p : v)
        var += (p - mean) * (p - mean) / (double) v.size();
    return { sorted[sorted.size() / 2], 100.0 * std::sqrt (var) };
}

double rmsOver (const lynx::SampleData& x, double a, double b)
{
    double s = 0.0;
    int n = 0;
    for (auto i = (std::int64_t) (a * x.sampleRate); i < (std::int64_t) (b * x.sampleRate) && i < (std::int64_t) x.frames(); ++i, ++n)
        s += (double) x.leftData()[i] * x.leftData()[i];
    return std::sqrt (s / std::max (1, n));
}

// the voiced note that holds time t (seconds)
int noteAt (const lynx::newtonefx::Track& t, double seconds, double fs)
{
    const auto at = (std::int64_t) (seconds * fs);
    for (int k = 0; k < (int) t.pitches.size(); ++k)
        if (t.pitches[(std::size_t) k].voiced() && at >= t.pitches[(std::size_t) k].origStart && at < t.pitches[(std::size_t) k].voicedEnd())
            return k;
    return -1;
}
} // namespace vocal190li

TEST (vocal190li_native_layout_4_only_when_used)
{
    using namespace lynx;
    const auto s = vocal190li::line ({ { 0, 0.15 }, { 57, 0.4 }, { 60, 0.4 }, { 0, 0.1 } });
    auto t = newtonefx::analyseNotes (s);
    CHECK (t.engine == 0); // (an analysis alone: FL's engine; the editor gives a new slot the natural one)
    CHECK (t.pitches.size() >= 3u);
    if (t.pitches.size() < 3u)
        return;
    t.pitches[2].shift = 1.0;
    // nothing of layout 4 in use: version 3, as before round 190li
    const auto v3 = newtonefx::writeNative (t);
    CHECK (v3.size() > 8 && v3[4] == 3);
    const auto back3 = newtonefx::parseNative (v3);
    CHECK (back3 && back3->engine == 0 && back3->pitches.size() == t.pitches.size());
    if (back3)
        for (const auto& p : back3->pitches)
            CHECK (p.modulation == 1.0 && p.pitchDrift == 1.0 && p.sungPitch == 0.0);
    // the engine, modulation, drift and sung centre round-trip in version 4
    t.engine = newtonefx::latestEngine;
    newtonefx::applyModulation (t, 0.5, { 2 });
    newtonefx::applyPitchDrift (t, 0.0);
    const auto v4 = newtonefx::writeNative (t);
    CHECK (v4.size() > 8 && v4[4] == 4);
    const auto back = newtonefx::parseNative (v4);
    CHECK (back && back->engine == newtonefx::latestEngine && back->pitches.size() == t.pitches.size());
    if (back && back->pitches.size() == t.pitches.size())
        for (std::size_t k = 0; k < t.pitches.size(); ++k)
        {
            CHECK (back->pitches[k].modulation == t.pitches[k].modulation && back->pitches[k].pitchDrift == 0.0);
            CHECK (back->pitches[k].sungPitch == t.pitches[k].sungPitch);
        }
    CHECK (back && back->pitches[2].modulation == 0.5 && back->pitches[1].modulation == 1.0);
    // a 0.1.28 copy (layout 2) drops them: the old engine plays
    const auto v2 = newtonefx::writeNative (t, 2);
    const auto old = newtonefx::parseNative (v2);
    CHECK (v2[4] == 2 && old && old->engine == 0 && old->pitches[2].modulation == 1.0);
}

TEST (vocal190li_sung_pitch_and_octave_guard)
{
    using namespace lynx;
    // a low voice with a strong first formant: YIN's loose threshold (FL's voicing) can take a formant's sub-period;
    // the guard keeps the strict estimate's octave, so the notes read where they were sung
    const auto s = vocal190li::line ({ { 0, 0.2 }, { 45, 0.6 }, { 0, 0.15 }, { 50, 0.6 }, { 0, 0.2 } }, 0.0, 730.0);
    const auto t = newtonefx::analyseNotes (s);
    const int a = vocal190li::noteAt (t, 0.5, s.sampleRate), b = vocal190li::noteAt (t, 1.25, s.sampleRate);
    CHECK (a >= 0 && b >= 0);
    if (a < 0 || b < 0)
        return;
    CHECK (std::abs (t.pitches[(std::size_t) a].origPitch - 45.0) < 0.2);
    CHECK (std::abs (t.pitches[(std::size_t) b].origPitch - 50.0) < 0.2);
    // the sung centre: exact periods (FL's origPitch reads sharp from whole-sample periods)
    CHECK (std::abs (t.pitches[(std::size_t) a].sungPitch - 45.0) < 0.05);
    CHECK (std::abs (t.pitches[(std::size_t) b].sungPitch - 50.0) < 0.05);
    newtonefx::AnalyseOptions off;
    off.octaveGuard = false;
    const auto plain = newtonefx::analyseNotes (s, off);
    CHECK (! plain.pitches.empty()); // (FL's analysis path unchanged apart from the guard)
}

TEST (vocal190li_natural_engine_reproduces_and_shifts)
{
    using namespace lynx;
    const auto s = vocal190li::line ({ { 0, 0.15 }, { 57, 0.5 }, { 0, 0.15 }, { 62, 0.5 }, { 0, 0.15 } });
    auto t = newtonefx::analyseNotes (s);
    t.engine = 1;
    const int a = vocal190li::noteAt (t, 0.4, s.sampleRate), b = vocal190li::noteAt (t, 1.05, s.sampleRate);
    CHECK (a >= 0 && b >= 0);
    if (a < 0 || b < 0)
        return;
    // the second note +3 semitones, the first one only quieter: unshifted, the natural engine's grains add up to the
    // source itself (times the volume)
    t.pitches[(std::size_t) b].shift = 3.0;
    t.pitches[(std::size_t) a].volume = 0.5;
    t.edited = true;
    const auto out = newtonefx::renderEdits (s, t);
    CHECK (out != nullptr);
    if (out == nullptr)
        return;
    double worst = 0.0;
    const auto& p = t.pitches[(std::size_t) a];
    for (auto i = p.origStart + (std::int64_t) (0.05 * s.sampleRate); i < p.voicedEnd() - (std::int64_t) (0.05 * s.sampleRate); ++i)
        if (i < p.origStart + (std::int64_t) p.fadeInTime || i >= p.origEnd - (std::int64_t) p.fadeOutTime)
            continue;
        else
            worst = std::max (worst, (double) std::abs (out->left[(std::size_t) i] - 0.5f * s.left[(std::size_t) i]));
    CHECK (worst < 2e-4);
    // the shifted note: on its new pitch (within 3 cents), its loudness kept (within 0.5 dB)
    const auto [pitch, spread] = vocal190li::pitchOver (*out, 0.9, 1.2);
    CHECK (std::abs (pitch - 65.0) < 0.03);
    const auto lv = 20.0 * std::log10 (vocal190li::rmsOver (*out, 0.9, 1.2) / vocal190li::rmsOver (s, 0.9, 1.2));
    CHECK (std::abs (lv) < 0.5);
    // the same edits with engine 0 render too (FL's engine, unchanged by this round)
    auto t0 = t;
    t0.engine = 0;
    const auto out0 = newtonefx::renderEdits (s, t0);
    CHECK (out0 != nullptr && out0->frames() == out->frames());
}

TEST (vocal190li_modulation_and_drift_split)
{
    using namespace lynx;
    // a held note with a 40-cent vibrato
    const auto s = vocal190li::line ({ { 0, 0.15 }, { 60, 1.6 }, { 0, 0.15 } }, 40.0);
    auto t = newtonefx::analyseNotes (s);
    t.engine = 1;
    const auto [p0, sung] = vocal190li::pitchOver (s, 0.5, 1.4);
    CHECK (sung > 20.0); // (the vibrato is there)
    // modulation 0: the vibrato is gone, the note stays where it was
    auto flat = t;
    newtonefx::applyModulation (flat, 0.0);
    const auto outFlat = newtonefx::renderEdits (s, flat);
    CHECK (outFlat != nullptr);
    if (outFlat != nullptr)
    {
        const auto [p1, spread1] = vocal190li::pitchOver (*outFlat, 0.5, 1.4);
        CHECK (spread1 < 0.3 * sung);
        CHECK (std::abs (p1 - p0) < 0.05);
    }
    // drift 0 leaves the vibrato (it is the slow part that goes)
    auto still = t;
    newtonefx::applyPitchDrift (still, 0.0);
    const auto outStill = newtonefx::renderEdits (s, still);
    CHECK (outStill != nullptr);
    if (outStill != nullptr)
    {
        const auto [p2, spread2] = vocal190li::pitchOver (*outStill, 0.5, 1.4);
        CHECK (spread2 > 0.8 * sung);
    }
    // Correct pitch: centre + drift amounts, the vibrato kept
    auto corrected = t;
    newtonefx::correctPitch (corrected, 1.0, 1.0);
    for (const auto& p : corrected.pitches)
        CHECK (p.pitchDrift == 0.0 && p.modulation == 1.0);
}
