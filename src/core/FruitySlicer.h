#pragma once

// FL's Fruity Slicer as a LYNX generator (round 180c, docs/analise/REA_rodada180c.md §Fruity Slicer): its saved state
// (FL's 0xD5 stream, read and written byte for byte), the slice edits FL makes to the sample when it loads (reverse,
// declick, fades) and what a note plays. The voices are the sampler's: each key from the first slice's note plays one
// slice at the sample's own pitch.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/FlVoiceSettings.h"

namespace lynx
{
struct SampleData;
struct ChannelDef;

namespace fslicer
{
struct Slice
{
    std::string name;
    std::int32_t position = 0;  // first frame of the slice in the sample
    std::int32_t key = -1;      // the slice's own key (SliceKeyMenu); −1 = the first note + its index
    float beat = -1.0f;         // its place in beats when it came with one (a .zgr groove); −1 = from its position
    std::uint8_t flags = 0;     // bit 0: reversed ("REV")

    bool reversed() const noexcept { return (flags & 1) != 0; }
    bool operator== (const Slice&) const = default;
};

struct State
{
    std::uint32_t version = 16;
    float lengthBeats = 4.0f;   // "BEATS" (original sample length)
    float bpm = 140.0f;         // "BPM" (original sample tempo)
    std::int32_t pitchCents = 0;    // PS wheel, −1200…1200
    std::int32_t stretch = 0;       // TS wheel, −20000…20000: length factor 2^(v/10000)
    std::int32_t method = 0;        // stretch method (0 Fill gaps, 1 Alt fill gaps, 2…7 the licensed engine's modes)
    std::int32_t fadeInMs = 0;      // ATT wheel, 0…100 ms
    std::int32_t fadeOutMs = 0;     // DEC wheel, 0…100 ms
    std::string path;               // the sample (FL's path variables kept, e.g. %FLStudioFactoryData%)
    std::vector<Slice> slices;
    std::uint8_t animate = 0;       // "ANIMATE" (P+0x141)
    std::int32_t firstNote = 60;    // the note of slice 1
    std::uint8_t playToEnd = 0;     // "PLAY TO END": a slice plays on to the sample's end
    std::int32_t sampleRate = 44100; // the sample's rate when saved
    std::uint8_t autoDump = 1;      // "AUTO-DUMP"
    std::uint8_t declick = 0;       // "DECLICK"
    std::uint8_t autoFit = 1;       // "AUTO-FIT"
    std::int32_t display = 0;       // the editor's display mode
    std::vector<std::uint8_t> voiceBlock; // v16: FL's 364-byte voice settings block, kept as saved

    bool operator== (const State&) const = default;
};

// FL's 0xD5 state (versions 13…16; FUN_00766c40). nullopt when it is not a Fruity Slicer state LYNX can read.
std::optional<State> parse (const std::vector<std::uint8_t>& bytes);
// The state as FL writes it (version 16)
std::vector<std::uint8_t> write (const State& state);

// Slice i's length in frames, as FL computes it (FUN_00769900): up to the next slice (at least 1), the last to the end
std::vector<std::int64_t> sliceLengths (const State& state, std::int64_t frames);

// FL's edits to the loaded sample, in FL's order (FUN_007686d0): reversed slices, declick, the TS stretch with the
// Fill gaps methods (0, 1: FUN_0075b5d0), fades. FL runs the other methods and PS through a licensed engine: LYNX
// leaves the length as it is there (see the REA doc).
std::shared_ptr<SampleData> prepare (const SampleData& sample, const State& state);
// Where the slices start in the prepared sample (moved by a Fill gaps stretch)
std::vector<std::int32_t> playedStarts (const State& state, std::int64_t frames);
// TS's length factor, FL's Exp(Ln 2 · v / 10000) in single precision (FUN_00766300)
float stretchFactor (std::int32_t stretch);
// FUN_0075b5d0 on one slice (16-bit values, interleaved): `from` (oldFrames) grows into `to` (newFrames)
void fillGaps (const std::int16_t* from, std::int16_t* to, int oldFrames, int newFrames, int channels, int sampleRate, int method);

// The single steps (exposed for the tests)
void reverseRegion (SampleData& data, std::int64_t first, std::int64_t last);
void fadeRegion (SampleData& data, std::int64_t first, std::int64_t last, bool fadeIn);
void declickEnd (SampleData& data, std::int64_t first, std::int64_t last);

// What the sampler voice needs: the slice starts (fractions of the prepared sample), the first note and "play to end"
void configureSampler (ChannelDef& def, const State& state, std::int64_t frames);

// The voice settings (FL's Envelope / instrument settings page: the envelopes, LFOs and filter of the state's 364-byte
// block). A state without the block reads as the plugin's defaults; setting them gives it one, as FL saves it.
flvoice::Settings voiceSettings (const State& state);
void setVoiceSettings (State& state, const flvoice::Settings& settings);

// A new Fruity Slicer state for a sample: one slice at the start, FL's defaults
State fresh (const std::string& path, int sampleRate);

// "Dump beat to piano roll" (FUN_0076b640): FL's menu tags — 0 Normal, 1 Reverse, 2 Quantize, 3 Swing, 4 Random,
// 5 Flatten, 6 Accentuate beat, 7 Pitch up beat, 8 Widen stereo, 9 Original length, 10 Shift up, 11 Shift down,
// 12 Crazy, 13 Stutter (half), 14 Stutter (fourth)
struct DumpNote
{
    std::int64_t start = 0, length = 1; // ticks at `ppq`
    int key = 60;
    float velocity = 0.78125f;          // FL's 100/128
    int pan = 0;                        // FL's −64…64
    int pitchCents = 0;
};
// `frames` / `sampleRate`: the sample; `bpm`: the project's tempo (Original length places the slices in real time).
// `random` (0 ≤ r < n) stands in for Delphi's Random, whose seed LYNX does not share.
std::vector<DumpNote> dumpNotes (const State& state, int tag, std::int64_t frames, double sampleRate, double bpm, int ppq,
                                 const std::function<int (int)>& random);

// Slice starts every `beats` beats (FL's 1/6 … 1 beat slicing: tags 106, 104, 103, 102, 101) of a sample that lasts
// state.lengthBeats
std::vector<std::int32_t> gridSlices (const State& state, std::int64_t frames, double beats);
// "Zero-cross check slices": each start moved to the nearest zero crossing of the left channel
std::vector<std::int32_t> zeroCrossSlices (const std::vector<std::int32_t>& starts, const SampleData& sample);
// The key of slice i (its own key, else the first note + i)
int sliceKey (const State& state, int index);
// The first note FL plays from: FUN_00769900 sets it to 60 whenever it measures the slices (every load), an octave
// lower while the last slice would pass key 124 — the saved one is not used (FL render, round 180c)
int firstNote (const State& state);
// FL's key table (FUN_0076c200, issue #160): 125 keys, each the index of the slice it plays or −1. Every entry starts at
// −1, then each slice in order writes its index at its key (sliceKey), so a later slice on the same key wins. A note
// below the first slice's key or past the last one plays nothing (TriggerVoice FUN_00768230 starts no voice).
std::vector<int> keyTable (const State& state);
} // namespace fslicer
} // namespace lynx
