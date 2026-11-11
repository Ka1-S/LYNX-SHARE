#pragma once

// State (0xD5) of FL's sample-based generators: FPC (pad drum kit) and DirectWave
// (multi-sample). Formats in docs/analise/REA_rodada9.md (clean-room).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/FlpImport.h"

namespace lynx
{
// FPC pads that have a sample (paths still with FL's variables, e.g. %FLStudioData%).
std::optional<std::vector<DrumPad>> fpcPads (const std::vector<std::uint8_t>& state);

// DirectWave program: the zones (paths with FL's variables) and the amplitude envelope of the
// first zone.
struct DirectWaveProgram
{
    std::vector<SampleZone> zones;
    std::vector<std::vector<std::uint8_t>> embeddedWav; // per zone: WAV of the audio embedded in the project (empty = none)
    float attack = 0.0f, decay = 0.0f, sustain = 1.0f, release = 0.0f; // seconds / 0..1
};
std::optional<DirectWaveProgram> directWaveProgram (const std::vector<std::uint8_t>& state);

// Sliced sample from Fruity Slicer / Slicex: the file (or the WAV embedded in the project) and, in Slicer,
// the slice positions in frames. In Slicex the slices come from the WAV's own "cue" points.
struct SlicedSample
{
    std::string path;
    std::vector<std::uint8_t> embeddedWav;
    std::vector<std::int64_t> positions; // Slicer: start of each slice (frames)
    float pitchCents = 0.0f;
    float gain = 1.0f; // Slicex master volume: (11^(v/12800) − 1)/10 (REA_rodada69 §5)
};
std::optional<SlicedSample> fruitySlicer (const std::vector<std::uint8_t>& state);
std::optional<SlicedSample> slicexSample (const std::vector<std::uint8_t>& state); // (the whole state: core/Slicex.h)

// FL Keys: the name of the sample set (the .wti in Plugins\Fruity\Generators\FL Keys\Wavtables).
std::optional<std::string> flKeysSet (const std::vector<std::uint8_t>& state);

// Zones of an FL Keys .wti (INI). `folder` is the .wti's folder; "raw" samples (a single .dat) are
// cut out of `rawData` and handed to `saveWav` (which returns the path of the written WAV).
std::vector<SampleZone> flKeysZones (const std::string& wti, const std::string& folder, const std::vector<std::uint8_t>& rawData,
                                     const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveWav);

// 16-bit mono PCM WAV with these samples (for the "raw" samples).
std::vector<std::uint8_t> makeWav16Mono (const std::int16_t* samples, std::size_t count, int sampleRate);

// Interleaved 32-bit float WAV from separate (planar) channels.
std::vector<std::uint8_t> makeWavFloat (const std::vector<std::vector<float>>& channels, int sampleRate);

// "cue" points (frames) of a WAV, in order; `frames` receives the length when the audio is PCM/float.
std::vector<std::int64_t> wavCuePoints (const std::vector<std::uint8_t>& wav, std::int64_t& frames);
} // namespace lynx
