#pragma once

// File › Export › "Master..." (Ctrl+Alt+M), LYNX's own version (round 168e). In FL Studio 2026 this item renders the
// project and sends it to FL Cloud's online mastering (a separate download, "FL Cloud mastering v2"; REA round 168e:
// FileExportMasterActionExecute → FUN_00edf6a0 / FUN_00edf490, the file named "<project> (mastered)"). LYNX has no
// online service: it masters the render on the computer instead — the integrated loudness (ITU-R BS.1770, core/
// Loudness.h) brought to a target and the peaks held under a true-peak ceiling by LYNX's Apex Limiter.

#include <vector>

namespace lynx::mastering
{
struct Settings
{
    double targetLufs = -14.0; // integrated loudness of the result (streaming services' usual reference)
    double ceilingDb = -1.0;   // true-peak ceiling (dBTP)
};

// The targets offered (LUFS), loudest first.
const std::vector<double>& targets();

struct Result
{
    double inputLufs = 0.0, outputLufs = 0.0; // integrated loudness before / after
    double outputTruePeakDb = 0.0;
    double gainDb = 0.0;                      // the gain applied before the limiter
};

// Masters a stereo render in place (`left` / `right` the same length) at `sampleRate`. Silence is left as it is.
Result process (std::vector<float>& left, std::vector<float>& right, double sampleRate, const Settings& settings);

// The integrated loudness and true peak (dBTP) of a stereo buffer.
void measure (const std::vector<float>& left, const std::vector<float>& right, double sampleRate, double& lufs, double& truePeakDb);
} // namespace lynx::mastering
