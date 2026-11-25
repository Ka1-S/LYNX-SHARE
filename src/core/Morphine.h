#pragma once

// FL Studio's Morphine rebuilt as the engine decoded in docs/analise/REA_rodada39.md §2 (clean-room; a 1:1 port of our
// own round-39 reference model r39/py/mormodel.py + morpatch.py, morfx.py, which was checked against renders of the
// installation's Morphine_x64.dll: 7/7 demo channels within −60 dB).
//
// Per voice, in control blocks of int(0.0029·fs) samples: the modulation processor (Eg1–4 breakpoint envelopes with
// loops, the 12-slot matrix, §2.5), the master ADSR (§2.6), the four generators (keyboard zones, frame player, PWM,
// §2.7) combined through the morph line (§2.8) into a bank of up to 128 coupled-form float oscillators updated every
// third block with a Catmull-Rom amplitude envelope (§2.9), and the noise sample player with its filter (§2.10); then
// the plugin effects overdrive → chorus → delay → reverb → EQ (§2.11) and the master gain (§2.12).
//
// The channel keeps the plugin's raw saved state (record 0xD5, "SSLF" + the Mor0 chunk tree) in ChannelDef::pluginState
// and the full path of the noise sample (…\Plugins\Fruity\Generators\Morphine\Noise Samples\<name>, read at run time
// from the user's installation, never bundled) in ChannelDef::samplePath (empty: no noise).
//
// Host interface as FL's wrapper (§2.3): MIDI velocity from Init.Vol, per-note L/R levels from ComputeLRVol(Final.Pan,
// Final.Vol) and the pitch offset; FL's channel volume/pan/pitch reach Morphine only through those, so LYNX recovers
// them from its channel gains and does not apply them a second time. The max-polyphony event is taken as value 0 (→ 32
// voices; FL's value at channel creation is not confirmed). LYNX creates the instance at the rate it plays at, so the
// sample-rate-change quirks of §2.13 (chorus wet −p⁴, overdrive smoother and master gain jump) do not apply.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this Morphine state (the whole Mor0 tree parses)
bool morphineStateUsable (const std::vector<std::uint8_t>& state);

// the noise sample file name of a state ("" = none)
std::string morphineNoiseName (const std::vector<std::uint8_t>& state);
// The state for FL (the .flp export): a noise that is LYNX's own ("LYNX ...", core/NativeData.h) is taken out, FL would ask
// for it as a missing file. true = there was one
bool morphineTakeOutOwnNoise (std::vector<std::uint8_t>& state);

// byte offset of global parameter A[index] (0…39, float) inside the state, or −1 (used by the channel settings panel)
int morphineParamOffset (const std::vector<std::uint8_t>& state, int index);

std::unique_ptr<Instrument> makeMorphineInstrument();

namespace morphine
{
// One note for the reference renderer (the verification harness drives the engine exactly like the Python model).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like mormodel.Synth.run: host blocks of `block` samples split at every event, tempo `bpm`, host law A for
// the per-note levels. `noisePath` = the noise sample file (empty: none). rateChange = the host set a sample rate
// different from 44 100 Hz after loading the state (the model's default when sampleRate ≠ 44100: chorus wet −p⁴,
// overdrive smoother and master gain set to their targets). Interleaved stereo float samples (2·length); empty if
// the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const std::string& noisePath, int block = 512, double bpm = 120.0, bool rateChange = false);
} // namespace morphine
} // namespace lynx
