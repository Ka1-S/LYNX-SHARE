#pragma once

// Effector (FL Studio's 12-algorithm multi-effect), reimplemented from the behavior
// described in docs/analise/REA_rodada18.md §3 and REA_rodada19.md §1. The 12 parameters are kept as in FL
// (raw values 0..1): 0 type, 1 bypass, 2 wet mix, 3 X, 4 Y, 5 mod rate, 6 tempo reduce, 7 mod X,
// 8 mod Y, 9 LFO shape, 10 input level, 11 output gain.

#include <memory>
#include <string>

#include "core/Effects.h"

namespace lynx::effector
{
constexpr int typeCount = 12;
enum Param { type, bypass, wetMix, paramX, paramY, modRate, tempoReduce, modX, modY, modShape, inputLevel, outputGain };

int typeOf (float v) noexcept;                // trunc(11v + 0.5)
const char* typeName (int type) noexcept;     // "Distort", "Lo-fi", …
double rateBeats (float modRate, float tempoReduce) noexcept; // LFO period in beats
std::string formatParam (int param, float value, double bpm);

// kiloheartsLaw (round 190nb): the slot holds a Kilohearts snapin law (core/KiloheartsDsp.h, hidden [12]): its buffers
// are allocated at prepare()
std::unique_ptr<Effect> make (bool kiloheartsLaw = false);
} // namespace lynx::effector
