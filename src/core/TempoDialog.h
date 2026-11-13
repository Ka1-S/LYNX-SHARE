#pragma once

// FL's "Tempo detection" dialog (live check 190ew PL-8, FL 2026 shot PL-8-detect-tempo-dialog.png): the audio clip menu's
// Detect tempo item (and the Sampler waveform menu's) opens one dialog titled "Tempo detection - <clip>": the estimate,
// then one button per choice, top to bottom: "<estimate> (estimated)", the four ranges, "Type in (BPM)", "Type in
// (beats)", "<project tempo> (project)", "No tempo". A tempo chosen there becomes the sample's tempo: the sample is
// synchronised to the project (LYNX: fitted to that many beats, pitch kept); "No tempo" leaves it unsynchronised.
// Header-only, no JUCE (tested in tests/FlFix190eyTests.inc).

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::tempodlg
{
enum class Choice { estimated, range, typeBpm, typeBeats, project, noTempo };

struct Button
{
    Choice choice = Choice::estimated;
    double low = 0.0, high = 0.0; // a range's BPM bounds
    double bpm = 0.0;             // the estimate / the project's tempo
};

// FL's ranges, in its order
inline constexpr double ranges[4][2] = { { 50.0, 100.0 }, { 75.0, 150.0 }, { 100.0, 200.0 }, { 150.0, 300.0 } };

// A tempo as FL writes it on the buttons: "117.5", "100" (at most two decimals, no trailing zeros)
inline std::string bpmText (double bpm)
{
    char text[32];
    std::snprintf (text, sizeof (text), "%.2f", std::round (bpm * 100.0) / 100.0);
    std::string s = text;
    while (! s.empty() && s.back() == '0')
        s.pop_back();
    if (! s.empty() && s.back() == '.')
        s.pop_back();
    return s;
}

// The buttons, top to bottom. `estimatedBpm` <= 0 (nothing found): no estimate button.
inline std::vector<Button> buttons (double estimatedBpm, double projectBpm)
{
    std::vector<Button> out;
    if (estimatedBpm > 0.0)
        out.push_back ({ Choice::estimated, 0.0, 0.0, estimatedBpm });
    for (const auto& r : ranges)
        out.push_back ({ Choice::range, r[0], r[1], 0.0 });
    out.push_back ({ Choice::typeBpm });
    out.push_back ({ Choice::typeBeats });
    out.push_back ({ Choice::project, 0.0, 0.0, projectBpm });
    out.push_back ({ Choice::noTempo });
    return out;
}

// The sample's length in beats at `bpm` (0 for no tempo)
inline double beatsAt (double seconds, double bpm) noexcept
{
    return seconds > 0.0 && bpm > 0.0 ? seconds * bpm / 60.0 : 0.0;
}
} // namespace lynx::tempodlg
