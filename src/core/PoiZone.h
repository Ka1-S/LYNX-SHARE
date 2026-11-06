#pragma once

// FL Studio's PoiZone rebuilt as the engine decoded in docs/analise/REA_rodada44.md (clean-room; a 1:1 port of our own
// round-44 reference model r44/py/pzmodel.py + pzfx.py, which was checked against renders of the installation's
// PoiZone_x64.dll / engine_x64.dll: bit-identical on the demo and preset batteries except where the tape delay's
// low/high-cut coefficients use the CRT's low-accuracy vector sincosf, §6).
//
// Per 64-sample chunk: eight per-chunk parameter smoothers, the voice pool (1 024 voices in a fixed list; unison,
// poly/mono, glide modes, octaver, analog drift), per voice and per chunk two envelopes (decibel-shaped curves), a
// per-voice LFO, oscillator increments/levels/pulse offsets and the 4-pole filter coefficients, then the voices at 8×
// oversampling (naive saw/pulse phases, hard sync, ring, noise, soft clip, 4 one-pole stages, DC blocker, ramped and
// smoothed gains) — one voice at a time or four lanes at a time with the SSE code's own operation order and its
// reversed write-back of the oscillator levels — the 2nd-order decimator, the 16-step gate, the two 12 kHz shelves,
// the three-line chorus, the stereo tape delay, the arpeggiator tick and the master gain.
//
// The channel keeps the plugin's raw saved state (record 0xD5, "SSLF" + "Ver3"/"Ver4" + 156 floats + name) in
// ChannelDef::pluginState. Host interface as FL's wrapper (shared with GMS): MIDI velocity from Init.Vol, per-note
// L/R level and pitch-offset tables from Final.Vol/Pan/Pitch, so LYNX's channel volume/pan/pitch reach the engine only
// through those tables. Random numbers: the DLL uses three process-global generators shared by every PoiZone of a
// project; LYNX gives every instance its own, started like a freshly loaded DLL, so noise, drift and initial phases
// are deterministic and differ from any given FL render by design.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this state ("SSLF" + "Ver3"/"Ver4"/"EDV3"/"EDV4" + parameters)
bool poizoneStateUsable (const std::vector<std::uint8_t>& state);

// parameter P[index] (0…155, float) of a state, or nullopt; and its byte offset inside the state (−1 if unusable)
std::optional<float> poizoneParam (const std::vector<std::uint8_t>& state, int index);
int poizoneParamOffset (const std::vector<std::uint8_t>& state, int index);

// The parameters the editor shows and automation clips drive (round 168n), sorted by index: P[0…80] with the raw
// ranges of the counted settings (unison voices, polyphony, speeds, bend range, LFO sync)
const std::vector<EngineParam>& poizoneParams();

// A state with the engine's constructor values ("Ver4"): a new PoiZone without FL Studio, the base of LYNX's bank
std::vector<std::uint8_t> poizoneDefaultState();

std::unique_ptr<Instrument> makePoiZoneInstrument();

namespace poizone
{
// PoiZone's own wheels on its editor (TSynthEditorForm: ModWheel, PitchBendWheel; REA_rodada44 §1.3: parameter 157 = the
// mod wheel x, 158 = the pitch bend 2x − 1). FL's state does not hold them (its 172 floats carry other data past 155);
// LYNX keeps them in ChannelDef::modelParams { mod wheel, pitch bend } (empty: 0 and the centre ½).
constexpr int modWheelParam = 157, bendWheelParam = 158;
std::pair<float, float> wheelsOf (const ChannelDef& channel) noexcept;
bool setWheel (ChannelDef& channel, int index, float raw);

// One note for the reference renderer (drives the engine like pzmodel.Plugin.run).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0, finalVol = 1.0, finalPan = 0.0, pitchCents = 0.0; // Init.Vol, Final.Vol/Pan, Final.Pitch offset
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like pzmodel.Plugin(state, rate).run: the plugin is created at 44 100 Hz, the state is loaded, then the rate
// is set (the engine resets only if it differs), host blocks of `block` samples split at every event, chunks of 64,
// host pan law A when panLaw (else L = R = Final.Vol), tempo `bpm`, max-polyphony event value `maxPoly` (0 → 64).
// Interleaved stereo float samples (2·length); empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, double bpm = 120.0, int maxPoly = 0, bool panLaw = false);
} // namespace poizone
} // namespace lynx
