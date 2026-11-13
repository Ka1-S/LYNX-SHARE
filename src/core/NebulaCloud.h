#pragma once

// Nebula's cloud networks (round 190ca): six delay / reverb networks built from long modulated delay lines —
// feedback delay networks with rotation scattering and cascades of matrix all-passes.
//
// This file and NebulaCloud.cpp are a C++ port of the Faust library function `re.valhallaSupermassive`
// (https://github.com/grame-cncm/faustlibraries, reverbs.lib 1.6.2, declared there as
// `author "David Braun"`, `license "MIT"`). The network structures, the delay / gain / modulation / filter laws and
// the fixed 33- and 32-sample offsets are that model's. LYNX's changes: block processing, glided controls (line
// lengths at 0.25 samples per sample, coefficients over 5 ms; the model applies its parameters directly), a
// single-precision signal path, the modulator's sine / cosine by rotation between blocks, and the looped network's
// global gain capped at 0.9995 like the other loops (the model lets it reach 1). Its licence:
//
//   MIT License
//
//   Copyright (c) 2026 David Braun
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
//   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
//   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
//   to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
//   the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
//   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//   IN THE SOFTWARE.
//
// (THIRD_PARTY.md carries the same notice. The structures themselves were published by their designer: S. Costello,
// keynote, 29th Int. Conf. on Digital Audio Effects, 2026; M. Gerzon, "Unitary (energy-preserving) multichannel
// networks with feedback", Electronics Letters 12 (11), 1976.)
//
// Nothing allocates after prepare(); no latency is reported (the wet path carries the model's own fixed offsets).

#include <memory>

namespace lynx::verbs::cloud
{
// the six networks, named by structure
enum class Topology
{
    none = -1,
    fdn16,        // sixteen modulated lines, scattered by a butterfly of rotations
    serialQuad,   // four four-channel matrix all-passes in series
    serialStereo, // eight stereo matrix all-passes in series
    loopedQuad,   // a plain four-channel stage and three matrix all-passes inside one feedback loop
    fdn4,         // four modulated lines with a four-channel scattering matrix
    fdn4Filtered, // fdn4 with the low and high cuts inside the loop
};
constexpr int topologyCount = 6;

// One block's controls, in units (the network glides them itself).
struct Controls
{
    Topology topology = Topology::fdn16;
    double delayMs = 300.0;     // 0 … 2000
    double warp = 0.0;          // 0 … 1: spreads the line lengths below the delay
    double feedback = 0.5;      // 0 … 1
    double density = 0.0;       // 0 … 1: the scattering angle (0 = no mixing between the lines)
    double width = 1.0;         // −1 … 1: 1 = full width, 0 = mono, −1 = sides swapped
    double lowCutHz = 10.0;     // 10 … 2000
    double highCutHz = 20000.0; // 200 … 20000
    double modRateHz = 0.5;     // 0.01 … 10
    double modDepth = 0.5;      // 0 … 1
};

class Network
{
public:
    Network();
    ~Network();
    void prepare (double sampleRate);
    void reset() noexcept;
    // Writes the wet signal only (the dry / wet mix is the caller's). The outputs may be the inputs.
    void process (const float* inLeft, const float* inRight, float* outLeft, float* outRight, int n, const Controls& c) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace lynx::verbs::cloud
