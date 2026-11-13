#pragma once

// FL Studio's 3x Osc rebuilt as the engine decoded in docs/analise/REA_rodada47.md (clean-room; a 1:1 port of our own
// round-47 reference model r47/py/osc3model.py, which renders bit-identically to the installation's DLL on all 239
// distinct non-HQ 3x Osc states of the installation and on synthetic batteries).
//
// Per voice (§1–§2): three oscillators reading the host's 16384-point wave tables (sine, triangle, square, saw, the
// "rounded saw" table, MT19937 noise; shape 6 = the channel's sample as a custom wave, else the saw) by the top 14 bits
// of a 32-bit phase; the increment comes from Final.Pitch + coarse·100 + fine cents (the right channel adds the stereo
// detune; noise draws a random increment every block); levels from the two mix knobs (osc 1 takes what is left, AM
// makes osc 3 a ring/amplitude modulator of osc 1 + 2), each oscillator's level split by FL's host pan law; stereo
// phase offset and random start phases. Then Image-Line's plugin voice processor (core/FlVoiceProcessor.h), which
// 3x Osc keeps itself like DX10 — including the release path the volume/cutoff/resonance/pitch envelopes take on a
// note-off, and the 10 ms end fade when the volume envelope is off.
//
// The channel keeps the plugin state in ChannelDef::pluginState as version 14 (21 parameters, invert ×3, AM, HQ, the
// 364-byte channel-settings snapshot); the importer normalises every version and puts the channel's 0xDA/0xDB records
// in the snapshot. ChannelDef::modelParams[0] (optional) is the FL project's PPQ; ChannelDef::samplePath (optional)
// the channel's sample, which FL hands 3x Osc resampled to 16384 points for the custom shape.
//
// Differences from FL by design / not modelled (REA_rodada47 §5): the "HQ" oscillators (engine_x64.dll, band-limited
// tables) play the plain tables; the custom wave is resampled with our own interpolation (FL's resampler not decoded);
// FL's host "HQ render" oversampling mode is on only while LYNX renders offline (REA_rodada80 §3.3); the random seed is per instrument (in FL all
// 3x Osc channels share one seed drawn at start-up). The processor's end-fade table uses IPP's cosine (REA_rodada74).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// FL Studio Mobile's 3x Osc module is the same machine in float (REA spec of round 190n) and carries, per parameter,
// the FL raw value it converts from; a channel imported from a .flm is a desktop state plus
// ChannelDef::modelParams = { PPQ, 1, gain }: [1] ≥ 0.5 makes the note's velocity the voice's level as it is (FLM's
// level is linear in the velocity; the desktop's Init.Vol curve is not applied), times [2] (FLM's Volume multiplier
// and volume-envelope amount; 1 when absent). The PPQ is FLM's tick rate, 384 / the song's steps per beat (96), and
// such a channel also steps its envelopes the way FLM does (flvp::Rec::mobile) on ticks of whole samples that run
// free of the song position (round 190ab). Saved with the project.
bool threeOscIsFlm (const ChannelDef& channel) noexcept;

// true when the exact engine can read this 3x Osc state (any version the plugin loads)
bool threeOscStateUsable (const std::vector<std::uint8_t>& state);

// The state normalised to version 14 (460 bytes) with `snapshot` (364 bytes) as its channel settings; when `snapshot`
// is empty a version-14 state keeps its own and older ones get the plugin's defaults. Empty if the state is unusable.
std::vector<std::uint8_t> threeOscNormalisedState (const std::vector<std::uint8_t>& state, const std::vector<std::uint8_t>& snapshot);

// Parameters of a normalised state: 0…20 the plugin's parameters (pan, shape, coarse, fine, stereo phase, stereo detune
// per oscillator; mix 2 at 6, mix 3 at 13; phase randomness at 20), 21…23 invert osc 1…3, 24 AM, 25 HQ
std::optional<int> threeOscParam (const std::vector<std::uint8_t>& state, int index);
bool threeOscSetParam (std::vector<std::uint8_t>& state, int index, int value); // clamped to the knob's range
int threeOscParamCount() noexcept;
// knob range of a parameter
std::pair<int, int> threeOscParamRange (int index) noexcept;

// A custom wave (shape 6) from an audio file: mono, 16384 points over the whole file. Empty if unreadable.
std::vector<float> threeOscCustomWave (const std::string& wavPath);

// hostHq: when it returns true at prepare(), the oscillators render as in FL's "HQ render" host mode (FL's
// exports and command-line renders; REA_rodada80 §3.3)
std::unique_ptr<Instrument> makeThreeOscInstrument (bool (*hostHq)() noexcept = nullptr);

namespace threeosc
{
// One note for the reference renderer (drives the engine exactly like the reference model's run()).
struct RefNote
{
    int start = 0, key = 60;
    double finalVol = 1.0, finalPan = 0.0, pitchOffset = 0.0; // Final.Vol/Pan, Final.Pitch − 100·(key − 60)
    int release = -1;                                         // sample of the note-off (−1: held)
};

struct RefOptions
{
    int block = 512;        // host block (split at every event and tick)
    int maxPoly = 0;        // ProcessEvent 1 (0 = unlimited)
    int tick = 0;           // > 0: the host's NewTick at every multiple of `tick` samples, before that sample's events
    int ppq = 96;           // SetTimeSig PPQ
    double tempo = 140.0;   // ProcessEvent 0
    std::uint32_t seed = 0; // the DLL's RandSeed when the first note starts
};

// Renders `notes` from a normalised `state` like osc3model.run(). Interleaved stereo float samples (2·length); empty if
// the state is unusable. `custom` (16384 floats) is the custom wave, if any.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, const RefOptions& options = {}, const std::vector<float>& custom = {});

// --- round 190fe: FL Studio Mobile's 3x Osc parameters as automation targets (an imported .flm's lanes).
// A channel from a .flm (threeOscIsFlm) takes AutomationTargetKind::channelParam `flmParamBase + k` with FLM's own
// value v (0…1) of its parameter k (flm190n spec-3xosc.md §1); the engine converts it with the import's laws
// (writeFlmParam), so a lane plays what the import writes for the same value. FLM's oscillator render reads the
// module's oscillator fields on every call (0x1807d9eb0: shape, coarse, fine, detune, gain / invert, pan), its
// modulation sums the cutoff, resonance and their offsets, its gain stage the Volume multiplier: the voices that sound
// follow a lane, as here. Three targets carry two FLM parameters at once, folded by the import: 111 = the cutoff with
// its offset (v111 + 2·v115 − 1), 112 = the resonance with its offset (v112 + 2·v116 − 1), 114 = half of FLM's gain
// (v114 · clamp(2·v106 − 1, 0, 1)). Not targets: HQ (25), Pan (113: the channel's pan), the voice settings 117…145
// (polyphony, porta, mono, tracking, gate, echo, arpeggiator) and Free Run (146…148).
constexpr int flmParamBase = 0x3000;
constexpr int flmParamCount = 149;
bool isFlmTarget (int k) noexcept; // k = 1…24, 26…105, 107…112, 114
// Writes target k at value v into a normalised (version-14, 460-byte) state as the .flm import does (111 / 112: the
// folded values). The attack's "no tick = start at the top" rule needs the song's steps per beat and tempo. False for
// 114 (the gain, not in the state) and non-targets.
bool writeFlmParam (std::uint8_t* state, int k, double v, int stepsPerBeat, double bpm) noexcept;
// --- end of round 190fe
} // namespace threeosc
} // namespace lynx
