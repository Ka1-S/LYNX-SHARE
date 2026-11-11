// Round 190am: the Master's loudness readout (core/MasterLoudness.h; LYNX's own, off by default).

#include "core/MasterLoudness.h"
#include "core/Meters.h"

namespace extras190am
{
// `seconds` of a stereo 1 kHz sine at `dbfs`, pushed in uneven blocks as a device would; continues at sample `at`
inline void pushTone (lynx::MasterLoudness& tap, double sampleRate, double seconds, double dbfs, long long& at)
{
    const auto gain = std::pow (10.0, dbfs / 20.0);
    const int sizes[] = { 512, 37, 441, 1024, 1 };
    std::vector<float> l (1024), r (1024);
    auto left = (long long) std::llround (seconds * sampleRate);
    for (int b = 0; left > 0; ++b)
    {
        const auto n = (int) std::min<long long> (left, sizes[b % 5]);
        for (int i = 0; i < n; ++i)
            l[(std::size_t) i] = r[(std::size_t) i] = (float) (gain * std::sin (2.0 * 3.14159265358979323846 * 1000.0 * (double) (at + i) / sampleRate));
        tap.push (l.data(), r.data(), n, sampleRate);
        at += n;
        left -= n;
    }
}
} // namespace extras190am

TEST (extras190am_loudness_off_measures_nothing)
{
    lynx::MasterLoudness tap;
    CHECK (! tap.isEnabled()); // off by default
    long long at = 0;
    extras190am::pushTone (tap, 48000.0, 5.0, -10.0, at);
    const auto v = tap.read();
    CHECK (! lynx::MasterLoudness::measured (v.momentary) && ! lynx::MasterLoudness::measured (v.shortTerm));
    CHECK (! lynx::MasterLoudness::measured (v.integrated) && ! lynx::MasterLoudness::measured (v.truePeak));
    // and what was played while it was off is not in the measurement once it is switched on
    tap.setEnabled (true);
    extras190am::pushTone (tap, 48000.0, 5.0, -30.0, at);
    CHECK (std::abs (tap.read().integrated + 30.0f) < 0.1f && std::abs (tap.read().truePeak + 30.0f) < 0.1f);
    // the MeterBank carries one, off
    lynx::MeterBank bank;
    CHECK (! bank.loudness.isEnabled());
}

TEST (extras190am_loudness_matches_ebu_3341)
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // case 1: a stereo 1 kHz sine at -23 dBFS for 20 s = -23.0 LUFS (M, S and I), tolerance 0.1 LU
        {
            lynx::MasterLoudness tap;
            tap.setEnabled (true);
            long long at = 0;
            extras190am::pushTone (tap, sr, 20.0, -23.0, at);
            const auto v = tap.read();
            std::printf ("  extras190am %.0f Hz case 1: M %.2f S %.2f I %.2f TP %.2f\n", sr, (double) v.momentary, (double) v.shortTerm, (double) v.integrated, (double) v.truePeak);
            CHECK (std::abs (v.momentary + 23.0f) < 0.1f && std::abs (v.shortTerm + 23.0f) < 0.1f && std::abs (v.integrated + 23.0f) < 0.1f);
            CHECK (std::abs (v.truePeak + 23.0f) < 0.1f);
        }
        // case 3: -36 dBFS 10 s, -23 dBFS 60 s, -36 dBFS 10 s: the relative gate leaves -23.0 LUFS
        {
            lynx::MasterLoudness tap;
            tap.setEnabled (true);
            long long at = 0;
            extras190am::pushTone (tap, sr, 10.0, -36.0, at);
            extras190am::pushTone (tap, sr, 60.0, -23.0, at);
            extras190am::pushTone (tap, sr, 10.0, -36.0, at);
            CHECK (std::abs (tap.read().integrated + 23.0f) < 0.1f);
            CHECK (std::abs (tap.read().shortTerm + 36.0f) < 0.1f); // the last 3 s
        }
        // case 5: -26 dBFS 20 s, -20 dBFS 20.1 s, -26 dBFS 20 s = -23.0 LUFS
        {
            lynx::MasterLoudness tap;
            tap.setEnabled (true);
            long long at = 0;
            extras190am::pushTone (tap, sr, 20.0, -26.0, at);
            extras190am::pushTone (tap, sr, 20.1, -20.0, at);
            extras190am::pushTone (tap, sr, 20.0, -26.0, at);
            CHECK (std::abs (tap.read().integrated + 23.0f) < 0.1f);
            CHECK (std::abs (tap.read().truePeak + 20.0f) < 0.1f); // the highest since the start
        }
    }
}

TEST (extras190am_loudness_reset_and_rate_change)
{
    lynx::MasterLoudness tap;
    tap.setEnabled (true);
    long long at = 0;
    extras190am::pushTone (tap, 48000.0, 10.0, -12.0, at);
    CHECK (std::abs (tap.read().integrated + 12.0f) < 0.1f);

    // Reset: blank at once, then a measurement that knows nothing of what came before
    tap.reset();
    CHECK (tap.read() == lynx::MasterLoudness::Reading {});
    extras190am::pushTone (tap, 48000.0, 10.0, -30.0, at);
    CHECK (std::abs (tap.read().integrated + 30.0f) < 0.1f && std::abs (tap.read().truePeak + 30.0f) < 0.1f);

    // another sample rate starts again too (the filters are made for the rate)
    at = 0;
    extras190am::pushTone (tap, 44100.0, 10.0, -18.0, at);
    CHECK (std::abs (tap.read().integrated + 18.0f) < 0.1f && std::abs (tap.read().truePeak + 18.0f) < 0.1f);

    // off: blank; on again: a fresh measurement
    tap.setEnabled (false);
    CHECK (tap.read() == lynx::MasterLoudness::Reading {});
    tap.setEnabled (true);
    extras190am::pushTone (tap, 44100.0, 5.0, -40.0, at);
    CHECK (std::abs (tap.read().integrated + 40.0f) < 0.1f);

    // nothing but silence: nothing to show (the absolute gate), never a number like -120
    lynx::MasterLoudness quiet;
    quiet.setEnabled (true);
    std::vector<float> zeros (4800, 0.0f);
    for (int i = 0; i < 50; ++i)
        quiet.push (zeros.data(), zeros.data(), 4800, 48000.0);
    CHECK (! lynx::MasterLoudness::measured (quiet.read().integrated) && ! lynx::MasterLoudness::measured (quiet.read().truePeak));
}

TEST (extras190am_loudness_text)
{
    CHECK (lynx::formatLoudness ((float) lynx::loudness::silence) == "--.-");
    CHECK (lynx::formatLoudness (-14.23f) == "-14.2");
    CHECK (lynx::formatLoudness (-9.96f) == "-10.0");
    CHECK (lynx::formatLoudness (-0.04f) == "0.0"); // never "-0.0"
    CHECK (lynx::formatLoudness (1.47f) == "1.5");
}
