#pragma once

// FL Studio's MiniSynth (the FL Studio Mobile synthesizer) rebuilt as the engine decoded in docs/analise/REA_rodada41.md
// (clean-room; a 1:1 port of our own round-41 reference model r41/py/msmodel.py, which renders bit-identically to the
// installation's MiniSynth_x64.dll on 11 of the 12 states of the installation's projects and on 47 of its 91 presets;
// the others stay within −107…−147 dB, the difference coming from the C runtime's sin/pow last-bit rounding).
//
// Per engine call of at most 512 samples, in 32-sample blocks: the LFO (§5), then every active voice (§3–4): control
// values (drift, glide, pitch, oscillator set-up, the filter's transition matrix), then per sample the dithered table
// oscillators + noise, the 4-pole filter with its saturating first stage, the DC blocker, the envelopes and the smoothed
// per-note levels; then the effects in fixed order distortion → delay → phaser → chorus (§6). The 1024 band-limited
// tables are built from the installation's Minisynth\oscdata.dat (read at run time, never bundled).
//
// The channel keeps the plugin's raw saved state (record 0xD5, "SSLF" + the .flms preset) in ChannelDef::pluginState and
// the path of oscdata.dat in ChannelDef::samplePath.
//
// Host interface as FL's wrapper (§2): MIDI velocity from Init.Vol, per-note L/R level tables from
// ComputeLRVol(Final.Pan, Final.Vol) and the pitch-offset table; FL's channel volume/pan/pitch reach MiniSynth only
// through those tables, so LYNX recovers them from its channel gains and does not apply them a second time. Random draws:
// MiniSynth uses one process-global generator shared by every instance (§3.6); LYNX gives every instance its own,
// started like a freshly created plugin, so noise, dither, drift and the random phases differ from any given FL render
// by design.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this MiniSynth state ("SSLF" + the .flms preset: HEAD name, DATA "0rev" floats)
bool miniSynthStateUsable (const std::vector<std::uint8_t>& state);

// the 43 parameter values (0…1) of a state as the plugin applies them (missing ones keep their defaults); empty if the
// state is unusable
std::vector<float> miniSynthParams (const std::vector<std::uint8_t>& state);

// byte offset of parameter i (0…42, float) inside the state, or −1 (used by the channel settings panel)
int miniSynthParamOffset (const std::vector<std::uint8_t>& state, int index);

std::unique_ptr<Instrument> makeMiniSynthInstrument();

// FL Studio Mobile's MiniSynth module is this engine with a few differences (REA spec of round 190n: the note's
// velocity goes to the voice as it is, linear balance pan with centre gain 1, 5 voices, LFO → OSC Mod at 1/12 of the
// desktop depth and clamped, the first note glides from its own key, the effects really stop 0.5 s after the last
// voice). A channel imported from a .flm asks for it with modelParams = { 1 } (saved with the project).
bool miniSynthIsFlm (const ChannelDef& channel) noexcept;

namespace minisynth
{
// One note for the reference renderer (the verification harness drives the engine exactly like the Python model).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like msmodel.Plugin driven by r41/py/mcmp.py: host blocks of `block` samples split at every event, engine
// calls of ≤ 512, tempo `bpm`, max-polyphony event value `maxPoly` (0 → 99), songPos ≥ 0 = the transport plays from that
// song position (beats); flm = FL Studio Mobile's build (RefNote::initVol is then the voice velocity 0…1); panLawA = the host's ComputeLRVol cos/sin law (false: L = R = Final.Vol, like the harness'
// default). The tables are built from `oscdataPath` (a missing file plays silent tables). Interleaved stereo float
// samples (2·length); empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& oscdataPath, int block = 512, double bpm = 120.0, int maxPoly = 0,
                                    double songPos = -1.0, bool panLawA = false, bool flm = false);
} // namespace minisynth
} // namespace lynx
