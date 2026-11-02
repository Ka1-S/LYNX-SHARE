#pragma once

// FL Studio's BeepMap rebuilt as the engine decoded in docs/analise/REA_rodada65.md §2 (clean-room; checked against the
// installation's BeepMap_x64.dll).
//
// The plugin plays a bitmap as a bank of sine oscillators: one oscillator per pixel row (the top row is the highest),
// the columns scanned left to right, each column lasting "Length" (64…8256 samples at 44.1 kHz, scaled with the rate).
// A pixel's red is the oscillator's left level, green its right level (/255/√height); with "Use blue" the blue makes the
// row's frequency step smaller. "Scale" picks how rows map to pitch: cents around the note (span = "Frequency"), Hz
// around the note's frequency, or harmonics of the note two octaves down (with a high-frequency roll-off). Levels ramp
// linearly across each column; "Grainy" multiplies every column with one sine cycle; "Loop" restarts the image,
// otherwise the voice ends after the last column; "Widen" gives the right channel random phases. The oscillators read
// the host's 16384-point sine table and run on the x87 (64-bit precision) like the DLL; then Image-Line's plugin voice
// processor (core/FlVoiceProcessor.h: channel articulators, filter, pan law, gain ramps) — the same library as 3x Osc.
//
// The channel keeps the plugin's raw saved state (record 0xD5: version, 15 int32 settings [+ the 364-byte channel
// snapshot in version 1], the bitmap as a BMP file) in ChannelDef::pluginState. ChannelDef::modelParams[0] (optional)
// is the FL project's PPQ.
//
// Not modelled / not confirmed (REA_rodada65 §2.8): bitmaps taller than "Max height" (FL resamples them with its own
// image scaler; LYNX picks rows by nearest neighbour), image formats other than uncompressed 24/32-bit BMP, FL's
// process-global random seed (LYNX: one per instrument, so phases differ from any given FL render).

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
// true when the exact engine can read this BeepMap state (version 0/1 and a decodable bitmap at least 3 rows high)
bool beepMapStateUsable (const std::vector<std::uint8_t>& state);

// the 15 int32 settings (0 Frequency, 1 Length, 2 Scale, 3 Use blue, 4 Grainy, 5 Loop, 6 Widen, …, 14 Max height)
std::optional<std::vector<int>> beepMapParams (const std::vector<std::uint8_t>& state);
// byte offset of setting i inside the state (for the channel settings panel), or −1
int beepMapParamOffset (const std::vector<std::uint8_t>& state, int index);

std::unique_ptr<Instrument> makeBeepMapInstrument();

// The automatable settings of the designed page (round 168q), by setting number (sorted)
const std::vector<EngineParam>& beepMapEngineParams();
namespace modelparams
{
struct Table;
Table beepMapModelTable();
} // namespace modelparams

namespace beepmap
{
struct RefNote
{
    int start = 0, key = 60;
    double finalVol = 1.0, finalPan = 0.0, pitchOffset = 0.0; // Final.Vol/Pan, Final.Pitch − 100·(key − 60)
    int release = -1;                                         // sample of the note-off (−1: held)
};

// Renders like the EXEC harness drives the DLL (host blocks of `block` samples split at every event, no ticks, max
// polyphony event `maxPoly`, Delphi RandSeed `seed` when the first note starts). Interleaved stereo float samples;
// empty if the state is unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, std::uint32_t seed = 0, int maxPoly = 0);
} // namespace beepmap
} // namespace lynx
