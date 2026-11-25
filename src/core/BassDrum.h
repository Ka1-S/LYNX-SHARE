#pragma once

// FL Studio's BassDrum rebuilt as decoded in docs/analise/REA_rodada43.md §2 (clean-room; a 1:1 port of our own
// round-43 reference model r43/py/bdmodel.py, which reproduces renders of the installation's BassDrum engine bit for
// bit on the demo channels and factory presets).
//
// Per voice, at an internal 44.1 kHz: two oscillators reading the installation's 1024-point table (`sounddata`) with a
// falling pitch sweep (Base, Peak, Slide; the second one detuned by the Var. knobs and delayed by Var. Phase), a click
// added to their phase (Click Freq/Level/Cut), an optional noise-sample layer (16-bit WAV from `Noise Samples`, four
// one-pole lowpasses, A/D/S envelope, Noise Start delays either layer), the Duration ramp and the Attack smoother; the
// voice is resampled (linear, Sampling Ratio × the note's pitch offset) to the output rate, then the Drive saturator,
// the "Butt" 42 Hz high-pass, Output Level × velocity, and the host's per-note L/R levels. Note-offs do not shorten a
// note; a voice ends after ~23 ms of silence.
//
// The channel keeps the plugin's raw saved state (record 0xD5, "SSLF" + 1kiK/2kiK) in ChannelDef::pluginState and the
// BassDrum plugin folder of the user's installation in ChannelDef::samplePath (the engine reads `sounddata` and
// `Noise Samples/<name>` from it at run time; nothing of the installation is bundled).
//
// Host side as FL's wrapper (same CFLWrapper as GMS, REA_rodada39 §1.11): velocity from Init.Vol, per-note L/R levels
// from FL's ComputeLRVol(Final.Pan, Final.Vol) (x87 sin/cos law), Final.Vol = channel volume (link velocity off);
// LYNX's channel gains are turned back into those values and applied once, inside the engine.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the engine can read this BassDrum state ("SSLF", tag 1kiK/2kiK, 23 parameters)
bool bassDrumStateUsable (const std::vector<std::uint8_t>& state);

// true when `path` is BassDrum's folder with its table (here, or in this computer's FL installation: core/NativeData.h)
bool bassDrumDataUsable (const std::string& path);

// the noise sample's file name in a state ("" = none)
std::string bassDrumNoiseName (const std::vector<std::uint8_t>& state);

// byte offset of parameter index (0…22, float 0…1) inside the state, or −1
int bassDrumParamOffset (const std::vector<std::uint8_t>& state, int index);

std::unique_ptr<Instrument> makeBassDrumInstrument();

namespace bassdrum
{
// FL's CRT powf as the engine computes it (FMA path; bit-exact with the DLL on the tested range), exposed for tests
float powfMsvc (float x, float y);

struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1;
};

// Renders like bdmodel.run: host blocks of `block` samples split at every event, wrapper chunks of 512 samples.
// `pluginDir` = the BassDrum folder (sounddata + Noise Samples); noise = false skips the sample layer.
// Interleaved stereo (2·length floats); empty if the state or the table is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& pluginDir, int block = 512, bool noise = true);
} // namespace bassdrum
} // namespace lynx
