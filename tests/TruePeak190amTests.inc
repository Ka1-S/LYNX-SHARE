// Round 190am: the true-peak filter of core/Loudness.h read without a wrap test.
// Round 190co replaced that filter (4 x 12 taps, Hann, its points between the samples) by a half-band cascade that
// agrees with a 16x reference meter; the history is still kept twice and read in one run. The check against the plain
// form moved with it: tests/ApexFix190coTests.inc, apexfix190co_true_peak_estimator_is_the_plain_cascade.

#include "core/Loudness.h"

// What this test still holds: a cleared estimator is silent, and silence in gives (next to) silence out, whatever was
// measured before.
TEST (extras190am_true_peak_clears_to_silence)
{
    lynx::loudness::TruePeak tp;
    unsigned seed = 190;
    double loud = 0.0;
    for (int i = 0; i < 2000; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        loud = std::max (loud, tp.push (((seed >> 8) / 8388608.0 - 1.0) * 0.9));
    }
    CHECK (loud > 0.5);
    tp.clear();
    double quiet = 0.0;
    for (int i = 0; i < 500; ++i) quiet = std::max (quiet, tp.push (0.0));
    CHECK (quiet < 1e-6);
}
