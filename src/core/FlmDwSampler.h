#pragma once

// FL Studio Mobile's "DW Sampler" module (type 1) as LYNX plays an imported .flm (round 190de). Clean-room: written
// from the behaviour described in docs/analise/FLM_FORMAT.md §7.5 (static analysis of FLM's FL plugin, laws in our own
// words); no Image-Line code, table or sample is used.
//
// The module is a 128-voice multisample player: zones (key × velocity range, root key, gain, loop points) read at
// the file's own rate with linear interpolation, a linear amp envelope heard squared, one of three filters (a 4-pole
// ladder low-pass, a band-pass, a high-pass), one-pole filter and pitch envelopes, one LFO per voice, mono / legato
// with a glide. Velocity sets the level linearly.
//
// In LYNX the channel is a Sampler channel (InstrumentKind::sampler) whose ChannelDef::zones hold the zones (path,
// root, key and velocity ranges, gain, loop points; loopMode 1 = FLM's mode 2) and whose ChannelDef::modelParams start
// with `marker`: { marker, PB Range, smooth attack, the 34 module values, then per zone 1 when its decay is used }.
// The rack builds the engine below for such a channel instead of LYNX's own sampler voice. Editing the Sampler's
// envelope by hand turns it back into a plain LYNX sampler (the module's values are dropped), as FL's imported voices.

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
class Instrument;
struct SampleData;
struct EngineParam;

namespace flmdw
{
constexpr float marker = 1901.0f; // modelParams[0] of a channel playing this engine (engine version 1)
constexpr int paramCount = 34;
constexpr int head = 3;           // modelParams before the module's values: marker, PB Range, smooth attack
// automation of the module's value k: AutomationTarget { channelParam, channel, 0, automationBase + k } (clear of the
// Sampler's own knobs and of the MIDI indices)
constexpr int automationBase = 0x2400;

namespace param
{
enum : int
{
    enable, attack, release, pitch, fineTune, decay, sustain, slide, reverse, loop, legato, mono, cutoff, resonance, kbdTrack,
    velTrack, filterType, filterAttack, filterDecay, filterSustain, filterRelease, filterAmount, pitchAttack, pitchDecay,
    pitchSustain, pitchRelease, pitchAmount, lfoShape, lfoRate, lfoMode, lfoSync, lfoAmount, lfoDest, lfoAttack
};
}
extern const float defaults[paramCount];
const char* paramName (int k) noexcept; // FLM's own names ("Attack", "Filter Env. Amount"…)

// --- the module's saved state (CSTM "10WD") and DirectWave programs (.dwp) as FLM reads them
struct ZoneRecord
{
    std::string root;          // "@R@" (the app's resources), "@L@" (the user's library) or empty
    std::string path;          // relative, '/' separated (or a full path as a program saved it)
    int rootKey = 48, keyLow = 0, keyHigh = 128, velLow = 0, velHigh = 127;
    float gain = 1.0f, pan = 0.0f;
    int loopMode = 0;          // FLM loops only from mode 2 (DirectWave's forward loop)
    std::int64_t loopStart = 0, loopEnd = 0; // frames, end exclusive
    float attack = 0.0f, decay = 1.0f, sustain = 1.0f, release = 0.1f;
};
struct Custom
{
    std::vector<ZoneRecord> zones;
    bool hasPath = false;      // a PATh / PATH chunk: the zones come from that preset file
    std::string pathRoot, path;
    bool pathFloats = false;   // PATh (not PATH): Attack and Release follow the path and replace parameters 1 and 2
    float pathAttack = 0.0f, pathRelease = 0.1f;
    int pbRange = 4;           // MIDI chunk byte 0 (semitones of a full bend)
    bool smoothAttack = true;  // MIDI chunk byte 1: an Attack of 0 still takes 1 ms
};
// nullopt: not a "10WD" container
std::optional<Custom> parseCustom (std::span<const std::uint8_t> bytes);
// The zones of a DirectWave program file ("DwPr", version > 35): empty when it is not one FLM reads
std::vector<ZoneRecord> parseProgram (std::span<const std::uint8_t> bytes);

// --- the channel
bool isChannel (const ChannelDef& c) noexcept;
// the module's value k of a channel (its default when absent)
float value (const ChannelDef& c, int k) noexcept;
// modelParams for a channel: the module's values (fewer = the defaults), its MIDI settings, per zone its decay flag
std::vector<float> channelParams (const std::vector<float>& values, int pbRange, bool smoothAttack, const std::vector<bool>& zoneDecays);
// the zone for ChannelDef::zones (its file already resolved)
SampleZone sampleZone (const ZoneRecord& z, const std::string& resolvedPath);

// --- laws (exposed for tests; v = the stored 0…1 value)
double envelopeSeconds (float v) noexcept;   // Attack / Decay / Release: 10·v⁴ + 0.001
double onePoleRate (float v, double k) noexcept; // filter / pitch envelopes, LFO fade: per sample, 1 below 0.001
int pitchSemitones (float v) noexcept;       // int(48v − 24), float32
int filterKind (float v) noexcept;           // int(3v), float32: 0 off, 1 ladder low-pass, 2 band-pass, 3 high-pass
double cutoffHz (float cutoff, double keyTerm, double envTerm) noexcept; // 2^(10.4·c + 4 + …) capped at 22 kHz
double lfoHz (float v) noexcept;             // free rate

// --- the engine
struct EngineZone
{
    SampleZone zone;
    bool decay = true;                        // the zone's decay field was above 0
    std::shared_ptr<const SampleData> sample; // null = a zone whose file is missing (silent, as in FLM)
};
std::unique_ptr<Instrument> makeInstrument (std::vector<EngineZone> zones);
} // namespace flmdw
} // namespace lynx
