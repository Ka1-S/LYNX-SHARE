// Round 190by: Pocket SuperSaw (core/PocketSuperSaw.h), LYNX's engine for FL Studio Mobile's SuperSaw module (type 23).
// Uses the .flm builders of FlmImport182cTests.inc. No data file is needed: every test runs everywhere.

#include "core/PocketSuperSaw.h"

namespace saw190by
{
namespace ps = lynx::pocketsaw;

inline std::vector<float> defaultsVector()
{
    const auto& d = ps::defaults();
    return std::vector<float> (d.begin(), d.end());
}

// FLM's parameters with changes { index, value }
inline std::vector<float> patch (std::initializer_list<std::pair<int, float>> changes)
{
    auto p = defaultsVector();
    for (const auto& [k, v] : changes)
        p[(std::size_t) k] = v;
    return p;
}

inline float unisonValue (int n) { return ((float) n - 0.5f) / 11.0f; }

inline std::vector<float> mono (const std::vector<float>& stereo, int channel = 0)
{
    std::vector<float> m (stereo.size() / 2);
    for (std::size_t i = 0; i < m.size(); ++i)
        m[i] = stereo[2 * i + (std::size_t) channel];
    return m;
}

inline double rms (const std::vector<float>& x, int from, int to)
{
    double a = 0.0;
    from = std::max (from, 0);
    to = std::min (to, (int) x.size());
    for (int i = from; i < to; ++i)
        a += (double) x[(std::size_t) i] * (double) x[(std::size_t) i];
    return std::sqrt (a / (double) std::max (1, to - from));
}

inline double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// amplitude of the component at f (Hz) over [from, to), Hann window
inline double tone (const std::vector<float>& x, int from, int to, double f, double fs)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const int n = to - from;
    for (int i = 0; i < n; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (6.283185307179586 * (double) i / (double) (n - 1));
        const auto a = 6.283185307179586 * f * (double) i / fs;
        re += w * (double) x[(std::size_t) (from + i)] * std::cos (a);
        im += w * (double) x[(std::size_t) (from + i)] * std::sin (a);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// the strongest component near `guess` (± 3 %), in Hz, to about 0.01 %
inline double pitchNear (const std::vector<float>& x, int from, int to, double guess, double fs)
{
    double best = guess, bestA = -1.0;
    for (double span = 0.03; span > 1e-5; span *= 0.25)
    {
        const auto centre = best;
        for (int k = -8; k <= 8; ++k)
        {
            const auto f = centre * (1.0 + span * (double) k / 8.0);
            const auto a = tone (x, from, to, f, fs);
            if (a > bestA)
                bestA = a, best = f;
        }
    }
    return best;
}

inline std::vector<float> render (const std::vector<float>& p, const std::vector<ps::RefNote>& notes, double fs, double seconds, double bpm = 120.0,
                                  double songPos = -1.0)
{
    return ps::renderReference (p, notes, fs, (int) (seconds * fs), 512, bpm, songPos, 12345u);
}

// a SuperSaw module (type 23) with the given PRMS values
inline flm182c::Bytes sawModule (const std::vector<float>& values)
{
    namespace t = flm182c;
    t::Bytes b, h, desc, prst, prms, smpr;
    t::u32 (b, 23);
    t::u32 (b, 2);
    t::u32 (h, 23);
    t::u32 (h, 0);
    t::u8 (h, 1);
    const t::Bytes add1 { 0, 0, 127 };
    t::fixed (desc, "Default", 256);
    t::fixed (desc, "", 256);
    t::lp32 (prst, "");
    t::lp32 (prst, "Default/Default");
    for (const auto v : values)
        t::f32 (prms, v);
    for (const auto v : { 0.5f, 1.0f, 0.5f })
        t::f32 (smpr, v);
    return t::chunk ("RMOd", t::cat ({ b, t::chunk ("HEAD", h), t::chunk ("DESC", desc), t::chunk ("PRST", prst), t::chunk ("ADD1", add1),
                                       t::chunk ("PRMS", prms), t::chunk ("SMPR", smpr) }));
}

inline flm182c::Bytes sawProject (const flm182c::Bytes& module)
{
    namespace t = flm182c;
    t::Bytes f, m;
    t::tag (f, "10LF");
    t::fixed (m, "2025-07-06 11-39-10", 256);
    t::f64 (m, 0);
    t::fixed (m, "", 256);
    t::fixed (m, "SuperSaw", 256);
    t::u8 (m, 0);
    t::fixed (m, "", 2048);
    t::fixed (m, "2026-11-11 15:00:00", 256);
    const t::Bytes tdiv { 4, 4 };
    return t::cat ({ f, t::head ("SuperSaw", 120.0), t::chunk ("META", m), t::chunk ("TDIV", tdiv), t::rack (lynx::flm::kindMaster, {}),
                     t::rack (lynx::flm::kindNormal, { module }), t::channel (lynx::flm::kindMaster, "MASTER", 0, {}),
                     t::channel (lynx::flm::kindNormal, "Lead", 1, { t::lane (0, "Lead", { t::clip (0, 4, 4, 0, 1, { { 0, 2, 60 } }) }) }) });
}
} // namespace saw190by

TEST (pocket190by_supersaw_laws)
{
    namespace ps = lynx::pocketsaw;
    // the envelope time law T(v) = 2^(5·h⁷(v)) − 1
    CHECK (ps::envelopeSeconds (0.0f) == 0.0f);
    CHECK (std::fabs (ps::envelopeSeconds (0.01f) - 0.00270f) < 0.00005f);
    CHECK (std::fabs (ps::envelopeSeconds (0.1f) - 0.0302f) < 0.0003f);
    CHECK (std::fabs (ps::envelopeSeconds (0.5f) - 0.305f) < 0.003f);
    CHECK (std::fabs (ps::envelopeSeconds (0.9f) - 3.93f) < 0.03f);
    CHECK (std::fabs (ps::envelopeSeconds (1.0f) - 31.0f) < 0.01f);
    CHECK (ps::envelopeSeconds (2.0f) == ps::envelopeSeconds (1.0f)); // clamped
    CHECK (ps::envelopeStep (0.0f, 44100.0f) == 1.0f);
    CHECK (std::fabs (1.0f / ps::envelopeStep (0.5f, 44100.0f) - 0.305f * 44100.0f) < 200.0f);
    // unison: ⌊11v + 1⌋ in single precision — FLM's presets store k / 11, and 1/11 gives 2 oscillators
    CHECK (ps::unisonCount (0.0f) == 1);
    CHECK (ps::unisonCount (0.09090909f) == 2);
    CHECK (ps::unisonCount (0.5f) == 6);
    CHECK (ps::unisonCount (1.0f) == 12);
    for (int n = 1; n <= 12; ++n)
        CHECK (ps::unisonCount (saw190by::unisonValue (n)) == n);
    // detune: ±d octave with d = knob² / 12, symmetric at the curve's middle, one oscillator untouched
    CHECK (ps::detuneRatio (0, 1, 1.0f, 0.5f) == 1.0f);
    CHECK (std::fabs (ps::detuneRatio (0, 7, 1.0f, 0.5f) - std::pow (2.0f, -1.0f / 12.0f)) < 1e-6f);
    CHECK (std::fabs (ps::detuneRatio (6, 7, 1.0f, 0.5f) - std::pow (2.0f, 1.0f / 12.0f)) < 1e-6f);
    CHECK (std::fabs (ps::detuneRatio (3, 7, 1.0f, 0.5f) - 1.0f) < 1e-6f);
    CHECK (std::fabs (ps::detuneRatio (6, 7, 0.5f, 0.5f) - std::pow (2.0f, 0.25f / 12.0f)) < 1e-6f);
    for (int u = 1; u < 7; ++u) // the ratios rise with the oscillator number at any curve
        for (const auto curve : { 0.0f, 0.2f, 0.5f, 0.8f, 1.0f })
            CHECK (ps::detuneRatio (u, 7, 0.7f, curve) > ps::detuneRatio (u - 1, 7, 0.7f, curve));
    CHECK (ps::detuneRatio (3, 7, 1.0f, 1.0f) < ps::detuneRatio (3, 7, 1.0f, 0.5f)); // curve > ½ bunches the stack low
    CHECK (ps::detuneRatio (3, 7, 1.0f, 0.0f) > ps::detuneRatio (3, 7, 1.0f, 0.5f));
    // −1 dB per oscillator
    CHECK (std::fabs (20.0 * std::log10 ((double) ps::voiceNormaliser (1)) + 1.0) < 1e-4);
    CHECK (std::fabs (20.0 * std::log10 ((double) ps::voiceNormaliser (12)) + 12.0) < 1e-4);
    // cutoff: 16 Hz … 20 kHz, ±8 octaves of envelope, 1 octave per octave of key tracking around key 48
    CHECK (std::fabs (ps::cutoffHz (0.0f, 0.5f, 0.0f, 0.0f, 60.0f) - 16.0) < 1e-3);
    CHECK (ps::cutoffHz (1.0f, 0.5f, 0.0f, 0.0f, 60.0f) == 20000.0);
    CHECK (std::fabs (ps::cutoffHz (0.5f, 0.5f, 0.0f, 0.0f, 60.0f) - std::pow (2.0, 9.15)) < 0.1);
    CHECK (std::fabs (ps::cutoffHz (0.2f, 1.0f, 0.25f, 0.0f, 60.0f) / ps::cutoffHz (0.2f, 0.5f, 0.0f, 0.0f, 60.0f) - 4.0) < 1e-3);
    CHECK (std::fabs (ps::cutoffHz (0.2f, 0.5f, 0.0f, 1.0f, 60.0f) / ps::cutoffHz (0.2f, 0.5f, 0.0f, 1.0f, 48.0f) - 2.0) < 1e-3);
    // steps
    CHECK (std::fabs (ps::lfoPeriodSeconds (0.0f) - (30.0 + 1.0 / 30.0)) < 1e-4);
    CHECK (std::fabs (ps::lfoPeriodSeconds (1.0f) - 1.0 / 30.0) < 1e-6);
    CHECK (ps::lfoBeatLength (0.0f) == 0.125 && ps::lfoBeatLength (2.5f / 9.0f) == 0.75 && ps::lfoBeatLength (3.5f / 9.0f) == 0.5 && ps::lfoBeatLength (1.0f) == 32.0);
    CHECK (ps::arpStepBeats (0.2f) == 0.25 && ps::arpStepBeats (1.0f) == 1.0 && ps::arpStepBeats (0.0f) == 0.125);
    CHECK (ps::delayBeats (0.775f) == 0.75 && ps::delayBeats (0.0f) == 0.25 && ps::delayBeats (1.0f) == 1.0);
    CHECK (ps::transposeSemitones (0.5f) == 0 && ps::transposeSemitones (0.0f) == -12 && ps::transposeSemitones (1.0f) == 12);
    CHECK (ps::bendRangeSemitones (0.1f) == 2 && ps::bendRangeSemitones (1.0f) == 12 && ps::bendRangeSemitones (0.0f) == 1);
    CHECK (ps::voiceLimit (0.5f) == 4 && ps::voiceLimit (0.0f) == 1 && ps::voiceLimit (1.0f) == 8);
    // the wave tables: only harmonics below Nyquist (one at most on it), a full-scale saw at the bottom
    CHECK (ps::waveTableHarmonics (440.0, 44100.0) == 51);
    CHECK (ps::waveTableHarmonics (440.0, 96000.0) == 110);
    CHECK (ps::waveTableHarmonics (20.0, 44100.0) == 1024);
    CHECK (ps::waveTableHarmonics (15000.0, 44100.0) == 2);
    for (const auto f : { 55.0, 440.0, 3520.0, 9000.0 })
        for (const auto fs : { 44100.0, 48000.0, 96000.0 })
            CHECK ((ps::waveTableHarmonics (f, fs) - 1) * f <= fs * 0.5 + 1e-6);
    {
        const auto* t = ps::waveTable (20.0, 44100.0);
        float peak = 0.0f;
        double mean = 0.0;
        for (int i = 0; i < 2048; ++i)
            peak = std::max (peak, std::fabs (t[i])), mean += t[i];
        CHECK (peak > 0.95f && peak <= 1.0f);
        CHECK (std::fabs (mean / 2048.0) < 1e-3);
        CHECK (std::fabs (t[512] - 0.849f * 0.5f) < 0.01f); // the falling ramp: ±0.849 at the ends, half of it a quarter in
        const auto* one = ps::waveTable (15000.0, 44100.0); // two harmonics
        CHECK (std::fabs (one[100] - 0.540423f * (std::sin (6.283185307f * 100.0f / 2048.0f) + 0.5f * std::sin (6.283185307f * 200.0f / 2048.0f))) < 1e-4f);
    }
    // defaults (FLM's own table)
    const auto& d = ps::defaults();
    CHECK (d[0] == 1.0f && d[1] == 1.0f && d[4] == 0.0f && d[5] == 0.2f && d[19] == 1.0f && d[32] == 1.0f && d[33] == 0.5f && d[36] == 0.5f);
    CHECK (d[51] == 0.5f && d[53] == 1.0f && d[54] == 0.775f && d[55] == 0.4f && d[56] == 0.0f && d[57] == 0.5f && d[58] == 0.5f && d[43] == 0.0f);
}

TEST (pocket190by_supersaw_tuning_and_sample_rates)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    const auto p = patch ({ { ps::flm::retrigPhases, 1.0f } });
    double level[3] {}, attackAt[3] {};
    int r = 0;
    for (const auto fs : { 44100.0, 48000.0, 96000.0 })
    {
        // key 69 = 440 Hz, key 57 = 220 Hz, key 81 = 880 Hz at every sample rate
        for (const auto [key, hz] : { std::pair<int, double> { 69, 440.0 }, { 57, 220.0 }, { 81, 880.0 }, { 36, 65.40639 } })
        {
            const auto x = mono (render (p, { { 0, key, 1.0f, 0.5f, -1 } }, fs, 1.0));
            const auto f = pitchNear (x, (int) (0.2 * fs), (int) (0.9 * fs), hz, fs);
            CHECK (std::fabs (1200.0 * std::log2 (f / hz)) < 1.0); // within a cent
        }
        // transpose and the channel's pitch offset
        {
            const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::transpose, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6));
            CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, (int) (0.1 * fs), (int) (0.55 * fs), 440.0, fs) / 440.0)) < 1.0);
        }
        // level of the default patch (one saw, −1 dB normaliser): the same at every rate
        const auto y = render (p, { { 0, 60, 1.0f, 0.5f, (int) (0.5 * fs) } }, fs, 1.0);
        const auto x = mono (y);
        level[r] = db (rms (x, (int) (0.1 * fs), (int) (0.4 * fs)));
        CHECK (std::fabs (level[r] - db (0.849 / std::sqrt (3.0) * std::pow (10.0, -0.05))) < 0.35); // a ±0.849 ramp
        // left = right at the centre
        CHECK (std::fabs (rms (mono (y, 1), (int) (0.1 * fs), (int) (0.4 * fs)) - rms (x, (int) (0.1 * fs), (int) (0.4 * fs))) < 1e-6);
        // the default release (T(0.05) = 14.2 ms): silent 50 ms after the note-off
        CHECK (rms (x, (int) (0.55 * fs), (int) (0.6 * fs)) == 0.0);
        // a 0.25 s attack (Level EG Attack 0.5 → T(0.45)) reaches half amplitude (level = value²) at 70.7 % of it
        const auto slow = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::levelAttack, 0.5f } }), { { 0, 60, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto full = rms (slow, (int) (0.4 * fs), (int) (0.55 * fs));
        for (int i = 0; i + (int) (0.01 * fs) < (int) slow.size(); i += (int) (0.001 * fs))
            if (rms (slow, i, i + (int) (0.01 * fs)) >= 0.5 * full)
            {
                attackAt[r] = ((double) i + 0.005 * fs) / fs;
                break;
            }
        const auto t = (double) ps::envelopeSeconds (0.45f);
        CHECK (std::fabs (attackAt[r] - std::sqrt (0.5) * t) < 0.012);
        ++r;
    }
    CHECK (std::fabs (level[0] - level[1]) < 0.1 && std::fabs (level[0] - level[2]) < 0.1);
    CHECK (std::fabs (attackAt[0] - attackAt[1]) < 0.003 && std::fabs (attackAt[0] - attackAt[2]) < 0.003);
}

TEST (pocket190by_supersaw_silence_and_idle)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    for (const auto fs : { 44100.0, 96000.0 })
    {
        // no note: nothing, whatever the settings
        for (const auto& p : { defaultsVector(), patch ({ { ps::flm::delayMix, 1.0f }, { ps::flm::distortion, 1.0f }, { ps::flm::resonance, 1.0f },
                                                           { ps::flm::unison, 1.0f }, { ps::flm::arpMode, 0.3f }, { ps::flm::superSync, 1.0f } }) })
        {
            const auto y = render (p, {}, fs, 0.5);
            CHECK (std::all_of (y.begin(), y.end(), [] (float v) { return v == 0.0f; }));
        }
        // Enable off: nothing even with a note
        {
            const auto y = render (patch ({ { ps::flm::enable, 0.0f } }), { { 0, 60, 1.0f, 0.5f, -1 } }, fs, 0.2);
            CHECK (std::all_of (y.begin(), y.end(), [] (float v) { return v == 0.0f; }));
        }
        // a released voice ends, the engine goes idle and its output is exactly zero; no NaN on the way
        ps::Engine e;
        e.prepare (fs);
        CHECK (e.idle() && e.activeVoices() == 0);
        e.setParam (ps::flm::resonance, 1.0f);
        e.setParam (ps::flm::cutoff, 0.6f);
        e.setParam (ps::flm::unison, 1.0f);
        e.setParam (ps::flm::distortion, 0.7f);
        std::vector<float> l (4096), r (4096);
        e.note (60, 1.0f);
        e.note (64, 0.5f, 0.2f);
        bool finite = true, sounded = false;
        // (the engine stays awake for the delay's tail, 4 s after the last voice, whatever the delay's mix)
        const int blocks = 6 + (int) (4.5 * fs / 4096.0);
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.render (l.data(), r.data(), 4096, 120.0, -1.0);
            for (int i = 0; i < 4096; ++i)
                finite = finite && std::isfinite (l[(std::size_t) i]) && std::isfinite (r[(std::size_t) i]) && std::fabs (l[(std::size_t) i]) < 8.0f,
                sounded = sounded || l[(std::size_t) i] != 0.0f;
            if (b == 4)
            {
                CHECK (e.activeVoices() == 2 && ! e.idle());
                e.note (60, 0.0f);
                e.note (64, 0.0f);
            }
        }
        CHECK (finite && sounded);
        CHECK (e.activeVoices() == 0 && e.idle());
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), 4096, 120.0, -1.0);
        CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }) && std::all_of (r.begin(), r.end(), [] (float v) { return v == 0.0f; }));
    }
}

TEST (pocket190by_supersaw_unison_detune_and_stereo)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    const double fs = 48000.0;
    // two oscillators at Detune 1: one semitone below and above the note (beating at their difference)
    {
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::unison, unisonValue (2) }, { ps::flm::detune, 1.0f } }),
                                     { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 1.5));
        const auto lo = 440.0 * std::pow (2.0, -1.0 / 12.0), hi = 440.0 * std::pow (2.0, 1.0 / 12.0);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, 4800, 67200, lo, fs) / lo)) < 1.5);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, 4800, 67200, hi, fs) / hi)) < 1.5);
        CHECK (tone (x, 4800, 67200, 440.0, fs) < 0.05 * tone (x, 4800, 67200, lo, fs)); // nothing at the centre
        CHECK (std::fabs (db (tone (x, 4800, 67200, lo, fs) / tone (x, 4800, 67200, hi, fs))) < 0.5);
    }
    // Detune 0.5 → ±25 cents; the beat of the two fundamentals is their difference frequency
    {
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::unison, unisonValue (2) }, { ps::flm::detune, 0.5f },
                                              { ps::flm::cutoff, 0.52f } }),
                                     { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 2.0));
        const auto beat = 440.0 * (std::pow (2.0, 0.25 / 12.0) - std::pow (2.0, -0.25 / 12.0)); // 12.7 Hz
        // the envelope of the (low-passed, nearly sinusoidal) sum: count its minima
        int minima = 0;
        const int w = (int) (fs / 440.0 * 2.0);
        std::vector<double> env;
        for (int i = 4800; i + w < (int) x.size(); i += w / 2)
            env.push_back (rms (x, i, i + w));
        for (std::size_t i = 1; i + 1 < env.size(); ++i)
            if (env[i] < env[i - 1] && env[i] <= env[i + 1] && env[i] < 0.3 * *std::max_element (env.begin(), env.end()))
                ++minima;
        const auto seconds = (double) (x.size() - 4800) / fs;
        CHECK (std::abs (minima - (int) std::lround (beat * seconds)) <= 1);
    }
    // level: n in-phase oscillators without detune add up to n × the normaliser (−1 dB each)
    {
        const auto one = rms (mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.4)), 4800, 19200);
        for (const int n : { 2, 7, 12 })
        {
            const auto many = rms (mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::unison, unisonValue (n) }, { ps::flm::detune, 0.0f } }),
                                                 { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.4)), 4800, 19200);
            CHECK (std::fabs (db (many / one) - (db ((double) n) - (double) (n - 1))) < 0.2);
        }
    }
    // Unison Pan: mono at 0, the low oscillator right and the high one left... each side keeps its own half at 1
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::unison, unisonValue (2) }, { ps::flm::detune, 1.0f }, { ps::flm::unisonPan, 1.0f } });
        const auto y = render (p, { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 1.0);
        const auto l = mono (y, 0), r = mono (y, 1);
        const auto lo = 440.0 * std::pow (2.0, -1.0 / 12.0), hi = 440.0 * std::pow (2.0, 1.0 / 12.0);
        // oscillator 0 (low): s = −1 → left 1, right 0; oscillator 1 (high): s = +1 → left 0, right 1
        CHECK (tone (l, 4800, 43200, hi, fs) < 0.02 * tone (l, 4800, 43200, lo, fs));
        CHECK (tone (r, 4800, 43200, lo, fs) < 0.02 * tone (r, 4800, 43200, hi, fs));
        const auto centre = render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::unison, unisonValue (2) }, { ps::flm::detune, 1.0f } }),
                                    { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 0.3);
        CHECK (mono (centre, 0) == mono (centre, 1));
    }
    // sub: an octave below, level = knob²
    {
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::subLevel, 0.5f } }), { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto ratio = tone (x, 4800, 24000, 220.0, fs) / tone (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6).size() ? mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6)) : x, 4800, 24000, 220.0, fs);
        CHECK (std::fabs (ratio - 0.25) < 0.01);
        const auto none = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 0.6));
        CHECK (tone (none, 4800, 24000, 220.0, fs) < 1e-3);
    }
    // pulse mode: Pulse Width 1 = a square (no even harmonics), the fundamental twice the saw's
    {
        const auto saw = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto sq = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::pulseMode, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6));
        CHECK (std::fabs (tone (sq, 4800, 24000, 220.0, fs) / tone (saw, 4800, 24000, 220.0, fs) - 2.0) < 0.03);
        CHECK (tone (sq, 4800, 24000, 440.0, fs) < 0.02 * tone (sq, 4800, 24000, 220.0, fs));
        CHECK (std::fabs (tone (sq, 4800, 24000, 660.0, fs) / tone (sq, 4800, 24000, 220.0, fs) - 1.0 / 3.0) < 0.02);
        const auto off = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::pulseMode, 1.0f }, { ps::flm::pulseWidth, 0.0f } }),
                                       { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.3));
        CHECK (rms (off, 4800, 14000) < 0.01); // width 0: the two saws cancel (the dither's noise is left)
    }
}

TEST (pocket190by_supersaw_is_band_limited)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    // a high note: nothing folds back below the fundamental except the table read's dither noise. Key 100 = 2637 Hz:
    // a naive saw at 44.1 kHz would put its 9th…16th harmonics back between 0 and 2.6 kHz at −20…−30 dB.
    for (const auto fs : { 44100.0, 48000.0 })
    {
        const auto hz = 440.0 * std::pow (2.0, (100.0 - 69.0) / 12.0);
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 100, 1.0f, 0.5f, -1 } }, fs, 1.0));
        const int a = (int) (0.2 * fs), b = (int) (0.9 * fs);
        const auto fundamental = tone (x, a, b, hz, fs);
        CHECK (fundamental > 0.2);
        for (int h = 9; h <= 16; ++h)
        {
            auto alias = std::fabs (std::fmod ((double) h * hz + fs * 0.5, fs) - fs * 0.5);
            if (alias < 30.0 || std::fabs (alias / hz - std::round (alias / hz)) < 0.02)
                continue; // (lands on DC or a harmonic)
            CHECK (db (tone (x, a, b, alias, fs) / fundamental) < -60.0);
        }
    }
}

TEST (pocket190by_supersaw_filter_and_envelopes)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    const double fs = 48000.0;
    const auto open = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6)); // 110 Hz
    // cutoff 0.5 = 568 Hz: the 24 dB / octave slope well above it, −3·4/… at the corner region
    {
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.5f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto gain = [&] (double f) { return db (tone (x, 9600, 28000, f, fs) / tone (open, 9600, 28000, f, fs)); };
        // four equal one-pole stages at fc' = 6.25 fc / 2π: |H|² = (1 + (f / fc')²)⁻⁴
        const auto fc = std::pow (2.0, 9.15) * 6.25 / 6.283185307179586;
        CHECK (gain (110.0) > -1.0 && gain (110.0) < 0.0);
        for (const auto f : { 110.0, 550.0, 1100.0, 2200.0, 4400.0 })
            CHECK (std::fabs (gain (f) - (-40.0 * std::log10 (1.0 + (f / fc) * (f / fc)))) < 1.0);
        CHECK (gain (4400.0) - gain (2200.0) < -21.0); // → 24 dB per octave
    }
    // resonance: a peak near the cutoff
    {
        const auto x = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.5f }, { ps::flm::resonance, 0.7f } }),
                                     { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto plain = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.5f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        CHECK (db (tone (x, 9600, 28000, 550.0, fs) / tone (plain, 9600, 28000, 550.0, fs)) > 6.0);
        CHECK (db (tone (x, 9600, 28000, 110.0, fs) / tone (plain, 9600, 28000, 110.0, fs)) < -6.0); // the feedback lowers the pass band
    }
    // the filter envelope opens the filter by ±8 octaves × amount; key tracking and velocity move the cutoff
    {
        const auto closed = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.2f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto env = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.2f }, { ps::flm::filterEgAmount, 1.0f } }),
                                       { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto vel = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.2f }, { ps::flm::filterVel, 0.5f } }),
                                       { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const auto h = [&] (const std::vector<float>& x) { return tone (x, 9600, 28000, 1100.0, fs); };
        CHECK (db (h (env) / h (closed)) > 40.0);
        CHECK (db (h (vel) / h (closed)) > 30.0);
        CHECK (db (h (env) / h (open)) > -1.0);
    }
    // level envelope: decay to the sustain level (output = value²), then the release
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::levelDecay, 0.4f }, { ps::flm::levelSustain, 0.5f }, { ps::flm::levelRelease, 0.5f } });
        const auto x = mono (render (p, { { 0, 57, 1.0f, 0.5f, (int) (0.6 * fs) } }, fs, 1.4));
        const auto plain = rms (mono (render (patch ({ { ps::flm::retrigPhases, 1.0f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.6)), (int) (0.4 * fs), (int) (0.55 * fs));
        const auto sustain = rms (x, (int) (0.4 * fs), (int) (0.55 * fs));
        CHECK (std::fabs (db (sustain / plain) - db (0.25)) < 0.3); // decay (1 → 0.5) lasts 0.5 × T(0.4) = 98 ms
        CHECK (rms (x, 480, 1440) > 2.5 * sustain); // (still near the top 20 ms in)
        // release: T(0.85·0.5 + 0.05) = T(0.475) from 0.5 → half of it; gone after it
        const auto t = 0.5 * (double) ps::envelopeSeconds (0.475f);
        CHECK (rms (x, (int) ((0.6 + 0.5 * t) * fs), (int) ((0.6 + 0.5 * t) * fs) + 960) < 0.4 * sustain);
        CHECK (rms (x, (int) ((0.6 + 0.5 * t) * fs), (int) ((0.6 + 0.5 * t) * fs) + 960) > 0.1 * sustain);
        CHECK (rms (x, (int) ((0.6 + t) * fs) + 480, (int) ((0.6 + t) * fs) + 4800) == 0.0);
    }
    // velocity is linear; Level is squared; Pan is a balance (the louder side stays at 1)
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f } });
        const auto full = rms (mono (render (p, { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.4)), 4800, 19000);
        CHECK (std::fabs (rms (mono (render (p, { { 0, 57, 0.5f, 0.5f, -1 } }, fs, 0.4)), 4800, 19000) / full - 0.5) < 0.01);
        CHECK (std::fabs (rms (mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::level, 0.5f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.4)), 4800, 19000) / full - 0.25) < 0.01);
        const auto y = render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::pan, 0.75f } }), { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 0.4);
        CHECK (std::fabs (rms (mono (y, 1), 4800, 19000) / full - 1.0) < 0.01);
        CHECK (std::fabs (rms (mono (y, 0), 4800, 19000) / full - 0.5) < 0.01);
        const auto z = render (p, { { 0, 57, 1.0f, 0.0f, -1 } }, fs, 0.4); // the note's own pan, hard left
        CHECK (rms (mono (z, 1), 4800, 19000) < 1e-4 && std::fabs (rms (mono (z, 0), 4800, 19000) / full - 1.0) < 0.01);
    }
}

TEST (pocket190by_supersaw_supersync_lfo_and_effects)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    const double fs = 48000.0;
    // SuperSync: Sync 0 = one pulse per period of half the cycle at width 1 (a square: odd harmonics); Sync 1 = six
    // pulses per period (the sixth harmonic dominates)
    {
        const auto s0 = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::superSync, 1.0f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        CHECK (tone (s0, 4800, 24000, 110.0, fs) > 0.3);
        CHECK (tone (s0, 4800, 24000, 220.0, fs) < 0.03 * tone (s0, 4800, 24000, 110.0, fs));
        const auto s1 = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::superSync, 1.0f }, { ps::flm::sync, 1.0f } }),
                                      { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.6));
        CHECK (tone (s1, 4800, 24000, 660.0, fs) > 5.0 * tone (s1, 4800, 24000, 110.0, fs));
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (s0, 4800, 24000, 110.0, fs) / 110.0)) < 1.0);
    }
    // LFO → pitch, sine, free: the pitch swings by ± amount octaves (amount (2·0.75 − 1)² = 0.25 octave)
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::lfoDest, 2.0f / 7.0f }, { ps::flm::lfoAmount, 0.75f }, { ps::flm::lfoRate, 0.6f },
                                { ps::flm::cutoff, 0.45f } });
        const auto x = mono (render (p, { { 0, 69, 1.0f, 0.5f, -1 } }, fs, 3.0));
        double lo = 1e9, hi = 0.0;
        int last = -1;
        for (int i = 4800; i < (int) x.size(); ++i) // upward zero crossings → period
            if (x[(std::size_t) i - 1] < 0.0f && x[(std::size_t) i] >= 0.0f)
            {
                if (last > 0 && i - last > 20)
                {
                    const auto f = fs / (double) (i - last);
                    lo = std::min (lo, f), hi = std::max (hi, f);
                }
                if (last < 0 || i - last > 20)
                    last = i;
            }
        CHECK (std::fabs (std::log2 (hi / 440.0) - 0.25) < 0.03);
        CHECK (std::fabs (std::log2 (lo / 440.0) + 0.25) < 0.03);
    }
    // LFO tempo-synced square → level: the level gates on the beat (phase = song position / beat length)
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::lfoDest, 6.0f / 7.0f }, { ps::flm::lfoAmount, 0.0f }, { ps::flm::lfoMode, 1.0f },
                                { ps::flm::lfoBeat, 4.5f / 9.0f }, { ps::flm::lfoShape, 2.0f / 3.0f } });
        // amount (2·0 − 1)² negated = −1: level 1 − 1 = 0 in the first half of each beat, 1 in the second
        const auto x = mono (render (p, { { 0, 57, 1.0f, 0.5f, -1 } }, fs, 2.0, 120.0, 0.0));
        const int beat = (int) (0.5 * fs);
        for (int b = 0; b < 3; ++b)
        {
            CHECK (rms (x, b * beat + beat / 8, b * beat + beat * 3 / 8) < 0.02);
            CHECK (rms (x, b * beat + beat * 5 / 8, b * beat + beat * 7 / 8) > 0.3);
        }
    }
    // distortion: tanh(k·x) / tanh(k) keeps a full-scale peak at 1 and squares the wave; the trim is a plain gain
    {
        const auto clean = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.45f } }), { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.5));
        const auto dist = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.45f }, { ps::flm::distortion, 1.0f } }),
                                        { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.5));
        float peak = 0.0f;
        for (int i = 4800; i < 20000; ++i)
            peak = std::max (peak, std::fabs (dist[(std::size_t) i]));
        CHECK (peak > 0.98f && peak <= 1.0001f);
        CHECK (rms (dist, 4800, 20000) > 0.9); // nearly a square wave
        const auto trimmed = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::cutoff, 0.45f }, { ps::flm::trim, 0.5f } }),
                                           { { 0, 45, 1.0f, 0.5f, -1 } }, fs, 0.5));
        CHECK (std::fabs (db (rms (trimmed, 4800, 20000) / rms (clean, 4800, 20000)) + 9.0) < 0.05); // 10^(−0.9·0.5) = −9 dB
    }
    // delay: the echo comes one delay time later (¾ beat at 120 BPM = 0.375 s), scaled by feedback × mix; feedback 0 = no echo
    {
        const auto note = std::vector<ps::RefNote> { { 0, 57, 1.0f, 0.5f, (int) (0.1 * fs) } };
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::delayMix, 1.0f }, { ps::flm::delayFeedback, 0.5f } });
        const auto x = mono (render (p, note, fs, 1.4));
        const auto direct = rms (x, 480, (int) (0.09 * fs));
        const int d = (int) (0.375 * fs);
        CHECK (rms (x, (int) (0.2 * fs), (int) (0.36 * fs)) < 1e-4); // nothing between
        CHECK (std::fabs (rms (x, d + 480, d + (int) (0.09 * fs)) / direct - 0.495) < 0.01);
        CHECK (std::fabs (rms (x, 2 * d + 480, 2 * d + (int) (0.09 * fs)) / direct - 0.495 * 0.495) < 0.01);
        const auto none = mono (render (patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::delayMix, 1.0f }, { ps::flm::delayFeedback, 0.0f } }), note, fs, 1.0));
        CHECK (rms (none, d, d + (int) (0.1 * fs)) < 1e-20); // (the loop's anti-denormal offset only)
        // the tempo sets the time: 0.25 s at 180 BPM
        const auto fast = mono (render (p, note, fs, 1.0, 180.0));
        CHECK (std::fabs (rms (fast, (int) (0.25 * fs) + 480, (int) (0.34 * fs)) / direct - 0.495) < 0.01);
    }
}

TEST (pocket190by_supersaw_voices_glide_and_arpeggiator)
{
    namespace ps = lynx::pocketsaw;
    using namespace saw190by;
    const double fs = 48000.0;
    std::vector<float> l (480), r (480);
    const auto run = [&] (ps::Engine& e, int blocks) {
        for (int b = 0; b < blocks; ++b)
            e.render (l.data(), r.data(), 480, 120.0, -1.0);
    };
    // polyphony: Number Of Voices (default 4), the oldest voice is taken
    {
        ps::Engine e;
        e.prepare (fs);
        for (int k = 0; k < 6; ++k)
        {
            e.note (60 + k, 1.0f);
            run (e, 1);
        }
        CHECK (e.activeVoices() == 4);
        e.setParam (ps::flm::voices, 1.0f);
        for (int k = 6; k < 12; ++k)
            e.note (60 + k, 1.0f);
        run (e, 1);
        CHECK (e.activeVoices() == 8);
        e.allNotesOff();
        run (e, 20);
        CHECK (e.activeVoices() == 0);
        // mono: one voice whatever is held; releasing the top key goes back to the key below
        e.setParam (ps::flm::mono, 1.0f);
        e.note (48, 1.0f);
        e.note (60, 1.0f);
        run (e, 4);
        CHECK (e.activeVoices() == 1);
        e.note (60, 0.0f);
        run (e, 10);
        CHECK (e.activeVoices() == 1);
        e.note (48, 0.0f);
        run (e, 20);
        CHECK (e.activeVoices() == 0);
    }
    // mono glide: Slide Time 0.6 → (0.3)² = 90 ms from the last note to the new one (static time)
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::mono, 1.0f }, { ps::flm::slideTime, 0.6f }, { ps::flm::cutoff, 0.5f } });
        const auto x = mono (render (p, { { 0, 57, 1.0f, 0.5f, -1 }, { (int) (0.3 * fs), 69, 1.0f, 0.5f, -1 } }, fs, 0.8));
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, (int) (0.1 * fs), (int) (0.29 * fs), 220.0, fs) / 220.0)) < 2.0);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, (int) (0.45 * fs), (int) (0.75 * fs), 440.0, fs) / 440.0)) < 2.0);
        // half-way through the glide the pitch is half-way (in semitones)
        const auto mid = 220.0 * std::pow (2.0, 0.5);
        const auto f = pitchNear (x, (int) (0.335 * fs), (int) (0.355 * fs), mid, fs);
        CHECK (std::fabs (12.0 * std::log2 (f / mid)) < 1.0);
    }
    // arpeggiator: up over a held chord at 1/16 (0.125 s at 120 BPM), each step retriggers one note
    {
        const auto p = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::arpMode, 0.3f }, { ps::flm::arpRange, 0.0f }, { ps::flm::cutoff, 0.6f } });
        const auto x = mono (render (p, { { 0, 57, 1.0f, 0.5f, -1 }, { 0, 64, 1.0f, 0.5f, -1 }, { 0, 69, 1.0f, 0.5f, -1 } }, fs, 1.0));
        const int step = (int) (0.125 * fs);
        const double want[] = { 220.0, 329.6276, 440.0, 220.0, 329.6276, 440.0 };
        for (int s = 0; s < 6; ++s)
            CHECK (std::fabs (1200.0 * std::log2 (pitchNear (x, s * step + step / 4, s * step + step * 7 / 8, want[s], fs) / want[s])) < 3.0);
        // down, two octaves
        const auto q = patch ({ { ps::flm::retrigPhases, 1.0f }, { ps::flm::arpMode, 0.5f }, { ps::flm::arpRange, 0.4f }, { ps::flm::cutoff, 0.6f } });
        const auto y = mono (render (q, { { 0, 57, 1.0f, 0.5f, -1 }, { 0, 64, 1.0f, 0.5f, -1 } }, fs, 0.6));
        const double down[] = { 659.2551, 440.0, 329.6276, 220.0 };
        for (int s = 0; s < 4; ++s)
            CHECK (std::fabs (1200.0 * std::log2 (pitchNear (y, s * step + step / 4, s * step + step * 7 / 8, down[s], fs) / down[s])) < 3.0);
        // releasing every key stops it
        const auto z = mono (render (p, { { 0, 57, 1.0f, 0.5f, (int) (0.3 * fs) } }, fs, 0.8));
        CHECK (rms (z, (int) (0.5 * fs), (int) (0.8 * fs)) == 0.0);
    }
}

TEST (pocket190by_supersaw_channel_parameters_round_trip)
{
    using namespace lynx;
    namespace ps = pocketsaw;
    // FLM's 60 values → the channel's 59 (FLM k + 1 at k), clamped; fewer than 60 = not a SuperSaw state
    std::vector<float> flm (60);
    for (int i = 0; i < 60; ++i)
        flm[(std::size_t) i] = (float) i / 64.0f;
    const auto values = pocketSuperSawParams (flm);
    CHECK ((int) values.size() == ps::paramCount);
    for (int i = 0; i < ps::paramCount && i < (int) values.size(); ++i)
        CHECK (values[(std::size_t) i] == flm[(std::size_t) i + 1]);
    CHECK (pocketSuperSawParams (std::vector<float> (59, 0.5f)).empty());
    flm[5] = 7.0f;
    flm[6] = -2.0f;
    flm[7] = std::numeric_limits<float>::quiet_NaN();
    const auto clamped = pocketSuperSawParams (flm);
    CHECK (clamped[4] == 1.0f && clamped[5] == 0.0f && clamped[6] == 0.0f);
    const auto d = pocketSuperSawDefaults();
    CHECK ((int) d.size() == ps::paramCount && d[0] == 1.0f && d[4] == 0.2f && d[18] == 1.0f && d[53] == 0.775f);

    // the automatable table: every FLM parameter but the unused 43, by its own index; values round-trip in the channel
    const auto* table = modelparams::tableOf (InstrumentKind::pocketSuperSaw);
    CHECK (table != nullptr);
    if (table == nullptr)
        return;
    CHECK ((int) table->params->size() == 58);
    CHECK (modelparams::find (InstrumentKind::pocketSuperSaw, 42) == nullptr);
    const auto* cutoff = modelparams::findByName (InstrumentKind::pocketSuperSaw, "Filter cutoff");
    CHECK (cutoff != nullptr && cutoff->index == 18);
    ChannelDef c;
    c.instrument = InstrumentKind::pocketSuperSaw;
    // an empty channel reads the defaults
    CHECK (modelparams::rawValue (c, 18) == std::optional<float> (1.0f));
    CHECK (modelparams::rawValue (c, 4) == std::optional<float> (0.2f));
    for (const auto& p : *table->params)
    {
        const auto v = 0.01f * (float) (p.index + 1);
        CHECK (modelparams::setValue01 (c, p.index, v));
        const auto back = modelparams::value01 (c, p.index);
        CHECK (back.has_value() && std::fabs (*back - v) < 1e-6f);
        CHECK (! ps::paramText (p.index, v).empty());
    }
    CHECK ((int) c.modelParams.size() == ps::paramCount);
    CHECK (c.modelParams[42] == 0.0f); // (FLM's unused 43 keeps its default)
    ChannelDef other;
    other.instrument = InstrumentKind::synth;
    CHECK (! modelparams::rawValue (other, 18).has_value());
    // texts as FLM shows them
    CHECK (ps::paramText (ps::flm::unison - 1, 0.09090909f) == "2");
    CHECK (ps::paramText (ps::flm::transpose - 1, 1.0f) == "+12 st");
    CHECK (ps::paramText (ps::flm::pan - 1, 0.5f) == "0.0");
    CHECK (ps::paramText (ps::flm::trim - 1, 0.5f) == "-9.0 dB");
    CHECK (ps::paramText (ps::flm::lfoRate - 1, 1.0f) == "30.00 Hz");
    CHECK (ps::paramText (ps::flm::delayTime - 1, 0.775f) == "1/8.");

    // the engine keeps what it is given
    ps::Engine e;
    for (int i = 1; i < ps::flmParamCount; ++i)
    {
        e.setParam (i, 0.013f * (float) i);
        CHECK (e.param (i) == 0.013f * (float) i);
    }
    e.setParam (3, std::numeric_limits<float>::quiet_NaN());
    CHECK (e.param (3) == 0.013f * 3.0f);

    // in LYNX's plugin list under its LYNX name
    const auto* engine = natives::instrumentEngine (InstrumentKind::pocketSuperSaw);
    CHECK (engine != nullptr && std::string (engine->name) == "Pocket SuperSaw");
}

TEST (pocket190by_supersaw_instrument_plays_the_channel)
{
    using namespace lynx;
    namespace ps = pocketsaw;
    using namespace saw190by;
    for (const auto fs : { 44100.0, 96000.0 })
    {
        auto inst = makeInstrument (InstrumentKind::pocketSuperSaw);
        CHECK (inst != nullptr);
        if (inst == nullptr)
            return;
        ChannelDef c;
        c.instrument = InstrumentKind::pocketSuperSaw;
        c.modelParams = pocketSuperSawDefaults();
        c.modelParams[ps::flm::retrigPhases - 1] = 1.0f;
        inst->prepare (fs);
        inst->configure (c, 120.0);
        const int n = (int) (0.5 * fs);
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        CHECK (inst->idle());
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }));
        inst->noteOn (69, 1.0f);
        CHECK (! inst->idle());
        inst->render (l.data(), r.data(), n, 0.5f, 0.25f); // adds, with the gains
        const auto want = db (0.849 / std::sqrt (3.0) * std::pow (10.0, -0.05));
        CHECK (std::fabs (db (rms (l, n / 4, n)) - (want - 6.02)) < 0.4);
        CHECK (std::fabs (db (rms (r, n / 4, n)) - (want - 12.04)) < 0.4);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (l, n / 4, n, 440.0, fs) / 440.0)) < 1.0);
        // the channel's pitch (semitones) and the MIDI wheel move the sounding voice
        c.sampler.pitch = 12.0f;
        inst->configure (c, 120.0);
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (l, n / 4, n, 880.0, fs) / 880.0)) < 1.0);
        inst->pitchBend (-12.0f);
        CHECK (inst->wheelCents() == 0.0f); // (the engine bends by itself)
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (std::fabs (1200.0 * std::log2 (pitchNear (l, n / 4, n, 440.0, fs) / 440.0)) < 1.0);
        // automation: Level (LYNX parameter 31) to 0.5 → −12 dB, without restarting the voice
        const auto before = rms (l, n / 2, n);
        inst->setParameter (ps::flm::level - 1, 0.5f);
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (std::fabs (db (rms (l, n / 2, n) / before) + 12.04) < 0.3);
        // a new channel state reaches the engine (Level back to 1 in the channel)
        inst->noteOff (69);
        for (int k = 0; k < 10; ++k) // (awake for the delay's 4 s tail)
            inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (inst->idle());
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }));
        // the mod wheel (CC 1) through the module's router: destination Level, amount −100 % → silence at full wheel
        c.modelParams[ps::flm::mwDest - 1] = 5.5f / 8.0f;
        c.modelParams[ps::flm::mwAmount - 1] = 0.0f;
        c.sampler.pitch = 0.0f;
        inst->configure (c, 120.0);
        inst->pitchBend (0.0f);
        inst->controlChange (1, 1.0f);
        inst->noteOn (57, 1.0f);
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (rms (l, n / 4, n) < 1e-4);
        inst->controlChange (1, 0.0f);
        std::fill (l.begin(), l.end(), 0.0f);
        inst->render (l.data(), r.data(), n, 1.0f, 1.0f);
        CHECK (rms (l, n / 4, n) > 0.2);
        inst->panic();
        CHECK (inst->idle());
    }
}

TEST (pocket190by_supersaw_flm_import)
{
    using namespace lynx;
    namespace ps = pocketsaw;
    using namespace saw190by;
    // a SuperSaw module with its own values on every parameter → a Pocket SuperSaw channel holding them as they are
    {
        std::vector<float> values (60);
        for (int i = 0; i < 60; ++i)
            values[(std::size_t) i] = 0.25f + 0.0125f * (float) i;
        values[0] = 1.0f;
        FlpImportReport report;
        std::string error;
        const auto song = importFlm (sawProject (sawModule (values)), flm182c::options (&report), &error);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto it = std::find_if (song->channels.begin(), song->channels.end(), [] (const ChannelDef& c) { return c.name == "Lead"; });
        CHECK (it != song->channels.end());
        if (it == song->channels.end())
            return;
        CHECK (it->instrument == InstrumentKind::pocketSuperSaw);
        CHECK ((int) it->modelParams.size() == ps::paramCount);
        for (int i = 0; i < ps::paramCount && i < (int) it->modelParams.size(); ++i)
            CHECK (it->modelParams[(std::size_t) i] == values[(std::size_t) i + 1]);
        CHECK (! it->muted);
        CHECK (it->pluginState.empty() && it->samplePath.empty());
        CHECK (report.converted.count ("FL Studio Mobile: SuperSaw") == 1);
        CHECK (report.unsupported.count ("FL Studio Mobile: SuperSaw") == 0);
        // FLM's own knob reaches the same LYNX parameter: Filter Cutoff is FLM 19 = LYNX 18
        CHECK (modelparams::rawValue (*it, ps::flm::cutoff - 1) == std::optional<float> (values[19]));
        // the imported channel plays through the instrument
        auto inst = makeInstrument (it->instrument);
        inst->prepare (48000.0);
        inst->configure (*it, song->bpm);
        std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
        inst->noteOn (60, 0.8f);
        inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
        CHECK (rms (l, 2400, 24000) > 1e-3);
    }
    // the module's Enable off (FLM renders nothing): the channel is muted
    {
        auto values = defaultsVector();
        values[0] = 0.0f;
        FlpImportReport report;
        const auto song = importFlm (sawProject (sawModule (values)), flm182c::options (&report), nullptr);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto it = std::find_if (song->channels.begin(), song->channels.end(), [] (const ChannelDef& c) { return c.name == "Lead"; });
        CHECK (it != song->channels.end() && it->instrument == InstrumentKind::pocketSuperSaw && it->muted);
    }
    // a module with fewer than 60 values is not a SuperSaw state: the built-in synth, listed
    {
        FlpImportReport report;
        const auto song = importFlm (sawProject (sawModule (std::vector<float> (40, 0.5f))), flm182c::options (&report), nullptr);
        CHECK (song.has_value());
        if (! song)
            return;
        const auto it = std::find_if (song->channels.begin(), song->channels.end(), [] (const ChannelDef& c) { return c.name == "Lead"; });
        CHECK (it != song->channels.end() && it->instrument != InstrumentKind::pocketSuperSaw);
        CHECK (report.unsupported.count ("FL Studio Mobile: SuperSaw") == 1);
    }
}

TEST (pocket190by_supersaw_new_channel_from_the_plugin_list)
{
    using namespace lynx;
    using namespace saw190by;
    // needs nothing of FL: a new channel is made without an FL installation and plays from FLM's defaults
    natives::FlAccess none;
    const auto* engine = natives::instrumentEngine (InstrumentKind::pocketSuperSaw);
    CHECK (engine != nullptr);
    if (engine == nullptr)
        return;
    CHECK (natives::available (*engine, none));
    const auto c = natives::makeChannel (InstrumentKind::pocketSuperSaw, none);
    CHECK (c.has_value());
    if (! c)
        return;
    CHECK (c->instrument == InstrumentKind::pocketSuperSaw && c->name == "Pocket SuperSaw");
    auto inst = makeInstrument (c->instrument);
    inst->prepare (48000.0);
    inst->configure (*c, 120.0);
    std::vector<float> l (24000, 0.0f), r (24000, 0.0f);
    inst->noteOn (60, 0.8f);
    inst->render (l.data(), r.data(), 24000, 1.0f, 1.0f);
    CHECK (rms (l, 2400, 24000) > 0.05);
    // the project file's name for the kind round-trips
    CHECK (pocketSuperSawParams (std::vector<float> (pocketsaw::defaults().begin(), pocketsaw::defaults().end())) == pocketSuperSawDefaults());
}
