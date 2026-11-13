#pragma once

// FL Studio Mobile's "Slicer" module (type 33) as LYNX plays an imported .flm (round 190de). Clean-room: written from
// the behaviour described in docs/analise/FLM_FORMAT.md §7.7 (static analysis of FLM's FL plugin, laws in LYNX's own
// words); no Image-Line code, table or sample is used.
//
// One sample cut into slices where FLM's onset detector finds hits (or at the file's markers with sensitivity 0);
// slice i plays on key firstKey + i, every note its own voice (no limit), FLM's drum-pad envelope laws, whole-semitone
// transposition, linear interpolation, a linear balance and velocity, and an optional choke (MONO).
//
// In LYNX the channel is a Sampler channel with the sample and ChannelDef::modelParams = { marker, sensitivity, first
// key, the module's 7 values }; the rack slices the decoded sample and plays the engine below on it.

#include <cstdint>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Instrument;
struct SampleData;

namespace flmslicer
{
constexpr float marker = 1902.0f;
constexpr int paramCount = 7; // 0 Enable, 1 PITCH, 2 (unused), 3 ATTACK, 4 DECAY, 5 SUST, 6 MONO
constexpr int head = 3;       // marker, sensitivity, first key
constexpr int automationBase = 0x2400; // automation of value k: channelParam automationBase + k (as the DW Sampler's)
extern const float defaults[paramCount];

bool isChannel (const ChannelDef& c) noexcept;
std::vector<float> channelParams (const std::vector<float>& values, float sensitivity, int firstKey);

// The slice starts (frames) FLM's detector finds in a sample at `sensitivity` (> 0), at most 128; the file's markers
// (or one slice at 0) at sensitivity 0
std::vector<std::int64_t> slices (const SampleData& sample, float sensitivity);

std::unique_ptr<Instrument> makeInstrument (std::shared_ptr<const SampleData> sample);
} // namespace flmslicer
} // namespace lynx
