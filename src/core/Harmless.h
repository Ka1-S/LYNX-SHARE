#pragma once

// FL Studio's Harmless rebuilt as the additive engine decoded in docs/analise/REA_rodada32.md (clean-room; a port of our
// own reference model, which was checked against renders of the installation's DLL). Each voice record is a bank of
// up to 528 sine partials (self-oscillation, Sub 1/3/4, 524 harmonics) whose target amplitudes are rebuilt every
// control block (88/44/8 samples at 44.1 kHz for quality 0/1/2) from colour × timbre EQ → mask → filter → resonance
// → phaser → pluck → low-harmonics protection → subs → band limit, with FL's envelopes, LFO, vibrato, tremolo,
// unison, portamento/mono/legato and voice stealing, followed by the effects rack of docs/analise/REA_rodada36.md
// (chorus → delay → reverb → compressor; core/HarmlessFx).
//
// ChannelDef::modelParams layout (harmlessParamCount floats): the 100 raw parameters p0…p99 (integers as floats; p99
// is 0 for states older than version 9), then the 7 combo integers (filter type, resonance type, phaser type,
// compressor type, colour, threaded flag, quality), then 1 when a Custom colour table is present (0 otherwise), then
// the Custom table: 524 harmonic amplitudes followed by 524 phases (cycles), zero-padded. Optionally followed by the
// rack compressor's preset (read by the importer from the user's FL installation, Harmless/Data/Compression/<n>.fst):
// its byte count, then the Maximus state, one byte per float.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
constexpr int harmlessKnobCount = 100;
constexpr int harmlessComboIndex = 100;        // 7 combos: filter, resonance, phaser, compressor, colour, threaded, quality
constexpr int harmlessCustomFlagIndex = 107;
constexpr int harmlessCustomIndex = 108;
constexpr int harmlessCustomSize = 1048;       // 524 amplitudes + 524 phases
constexpr int harmlessParamCount = harmlessCustomIndex + harmlessCustomSize;
constexpr int harmlessCompIndex = harmlessParamCount; // optional: byte count, then the bytes of the Maximus state

// FL project state (plugin record 0xD5, versions 4…12) → modelParams (layout above); nullopt if it can't be parsed
std::optional<std::vector<float>> harmlessParams (const std::vector<std::uint8_t>& state);

std::unique_ptr<Instrument> makeHarmlessInstrument();

// modelParams of a new LYNX instance (round 168o): FL's untouched patch (Saw, open filter, no modulation, effects off)
std::vector<float> harmlessDefaultModelParams();
} // namespace lynx
