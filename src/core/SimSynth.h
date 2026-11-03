#pragma once

// FL Studio's SimSynth rebuilt as the engine decoded in docs/analise/REA_rodada64.md (clean-room). In FL 2026 the
// plugin is a Delphi front-end (SimSynth_x64.dll) that forwards every call to a C++ engine (SimSynth_Eng_x64.dll); this
// is that engine plus the front-end's state handling:
//
// 12 voices, each with 3 table oscillators (4096-point tables built from 5 wave kinds with optional +1/+2 octave layers,
// read twice per output sample and averaged, optional detuned unison copy) or a filtered noise source, a ring
// modulator (osc 1 × osc 2), an LFO (square, saw, triangle, sample & hold, with fade-in), linear 4-stage amp and filter
// envelopes (the amp level squared twice and smoothed), a double-precision 2-stage state-variable filter with LP/BP/HP
// mix, the host's integer-pan ComputeLRVol per voice, and the plugin-level "chorus" (the left channel through a 20 ms
// modulated delay line). The voices' oscillator start phases and the noise come from one 65536-entry random table
// (the C runtime's rand() after srand(666)), read through a 16-bit index shared by everything; LYNX keeps one per
// instance, started like a freshly created plugin.
//
// The channel keeps the plugin's raw saved state (record 0xD5, 564 bytes: 45 doubles, 19 switches, version) in
// ChannelDef::pluginState.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
// true when the engine can read this SimSynth state (at least the 564 bytes the plugin reads)
bool simSynthStateUsable (const std::vector<std::uint8_t>& state);

// the 58 host parameters (0…65535 for knobs, 0/1 for switches, 0…4 for the wave selectors) as the plugin reports them
// after loading the state; empty if unusable
std::vector<int> simSynthHostParams (const std::vector<std::uint8_t>& state);

std::unique_ptr<Instrument> makeSimSynthInstrument();

namespace simsynth
{
// FL's host parameters of SimSynth (the editor's Tags, round 168o): 0…25 and 26…35 knobs (0…65535; 27, 30, 33 the
// coarse pitch in 48 semitone steps), 36…53 switches (0 / 1), 54…56 the oscillator waves and 57 the LFO shape
// (0 pulse / square, 1 half cosine / saw, 2 triangle, 3 noise / sample & hold, 4 sine–sine⁴ mix)
constexpr int hostParamCount = 58;
// the value of host parameter h in a state as FL's editor shows it (nullopt: unusable state, bad index)
std::optional<int> hostParam (const std::vector<std::uint8_t>& state, int h);
// writes host parameter h into a state (clamped to its range); false when the state can't be read
bool setHostParam (std::vector<std::uint8_t>& state, int h, int value);
// a complete state for a new LYNX instance: the plugin as FL creates it (the editor's default patch), version 14
std::vector<std::uint8_t> defaultState();

// One note for the reference renderer (the verification harness drives the engine exactly like run/ssx.exe drives
// the DLL). SimSynth takes FL's old voice-parameter record: Final.Pan is an integer −64…64, Final.Pitch an integer in
// cents (100·(key − 60) + pitchCents); Final.Vol, Final.FCut and Final.FRes are floats.
struct RefNote
{
    int start = 0, key = 60;
    float finalVol = 1.0f;
    int finalPan = 0, pitchCents = 0;
    float fcut = 0.0f, fres = 0.0f;
    int release = -1; // sample of the note-off (−1: held)
};

// Renders like the harness: plugin created, state restored, block size and sample rate set, then host blocks of
// `block` samples split at every event. Interleaved stereo float samples (2·length); empty if the state is unusable.
// counters (optional): the random-table index and the block counter after the set-up, for the EXEC comparisons.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, std::vector<std::int64_t>* counters = nullptr);
} // namespace simsynth
} // namespace lynx
