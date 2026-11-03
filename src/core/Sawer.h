#pragma once

// FL Studio's Sawer rebuilt as the engine decoded in docs/analise/REA_rodada65.md §3 (clean-room; checked against the
// installation's engine_x64.dll in Plugins\Fruity\Generators\Sawer).
//
// Sawer is built on the same synthesizer framework as PoiZone (REA_rodada44: the FL wrapper, voice allocation with
// unison, poly/mono and glide modes, the arpeggiator, the master gain, the chorus and the delay are instruction-for-
// instruction the same code). Its own parts: 24 voices, per voice and per control chunk (≈ 1 ms) the drift, LFO and
// envelope modulation, a saw read from the oscillator cycle `data.saw` plus a "sub-saw" (or its pulse form) at a
// harmonic ratio, hard sync, noise, a 4-pole filter with three saturating feedback passes per sample and four mixable
// outputs (low/band/high/notch), at 6× oversampling, one voice at a time or four at a time (the SSE code's own operation
// order), a 2nd-order decimator, a DC high-pass, then chorus, phaser, delay and reverb, the arpeggiator tick and the
// master gain.
//
// The channel keeps the plugin's raw saved state (record 0xD5: "SSLF" + size + "1rwS" + 96 floats + modulation data +
// name) in ChannelDef::pluginState and the path of the user's `data.saw` (the oscillator cycle, 8192 floats, read at run
// time and never bundled) in ChannelDef::samplePath. Random numbers: the DLL's process-global generators; LYNX gives
// every instance its own, started like a freshly loaded DLL.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this Sawer state ("SSLF" + "1rwS"/"DrwS" + at least the 96 parameters)
bool sawerStateUsable (const std::vector<std::uint8_t>& state);

// parameter P[index] (0…95) of a state, or nullopt; its byte offset inside the state (−1 if unusable)
std::optional<float> sawerParam (const std::vector<std::uint8_t>& state, int index);
int sawerParamOffset (const std::vector<std::uint8_t>& state, int index);

// the oscillator cycle (8192 floats) from a data.saw file; empty if unreadable
std::vector<float> sawerReadTable (const std::string& path);

std::unique_ptr<Instrument> makeSawerInstrument();

// The automatable parameters of the designed page (round 168q), by P index (sorted)
const std::vector<EngineParam>& sawerParams();
namespace modelparams
{
struct Table;
Table sawerModelTable();
} // namespace modelparams

namespace sawer
{
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1;                                                        // sample of the note-off (−1: held)
};

// Renders like the EXEC harness drives engine_x64.dll: the plugin created at 44 100 Hz, the state loaded, the rate set,
// max polyphony event `maxPoly` (0 → 24), host blocks of `block` samples split at every event, tempo `bpm`; host pan
// law A when panLaw (else L = R = Final.Vol). `table` = the 8192 floats of data.saw. Interleaved stereo float samples;
// empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<float>& table, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, int block = 512, double bpm = 120.0, int maxPoly = 0, bool panLaw = false);
} // namespace sawer
} // namespace lynx
