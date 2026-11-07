#pragma once

// FL's Fruity Slicer as a LYNX generator (round 180c, docs/analise/REA_rodada180c.md §Fruity Slicer): its saved state
// (FL's 0xD5 stream, read and written byte for byte), the slice edits FL makes to the sample when it loads (reverse,
// declick, fades) and what a note plays. The voices are the sampler's: each key from the first slice's note plays one
// slice at the sample's own pitch.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    std::int32_t extraA = -1;   // two fields FL always saves as −1 / −1.0
    float extraB = -1.0f;
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

// FL's edits to the loaded sample, in FL's order (FUN_007686d0): reversed slices, declick, fades. The stretch and pitch
// shift (TS / PS) are not applied: FL runs them through a licensed engine (method ≥ 2) — see the REA doc.
std::shared_ptr<SampleData> prepare (const SampleData& sample, const State& state);

// The single steps (exposed for the tests)
void reverseRegion (SampleData& data, std::int64_t first, std::int64_t last);
void fadeRegion (SampleData& data, std::int64_t first, std::int64_t last, bool fadeIn);
void declickEnd (SampleData& data, std::int64_t first, std::int64_t last);

// What the sampler voice needs: the slice starts (fractions of the prepared sample), the first note and "play to end"
void configureSampler (ChannelDef& def, const State& state, std::int64_t frames);

// A new Fruity Slicer state for a sample: one slice at the start, FL's defaults
State fresh (const std::string& path, int sampleRate);
} // namespace fslicer
} // namespace lynx
