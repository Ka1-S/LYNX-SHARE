#pragma once

// FL's FPC (pad drum sampler): its plugin state (the 0xD5 record of an .flp channel or an .fst kit), read and
// written. Format in docs/analise/REA_rodada9.md §1 (clean-room). A state that is read and written back without
// an edit is the same bytes: every chunk is kept, the ones LYNX does not know as raw bytes in their place.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx::fpc
{
// A chunk LYNX does not interpret. `position` is its index in the chunk list of its container, so the writer puts
// it back where it was.
struct RawChunk
{
    int position = 0;
    std::uint32_t id = 0;
    std::vector<std::uint8_t> body;

    bool operator== (const RawChunk&) const = default;
};

struct EnvelopePoint
{
    double time = 0.0;  // seconds after the previous point
    double value = 0.0; // 0…1
    float tension = 0.0f;
    std::uint32_t type = 0;

    bool operator== (const EnvelopePoint&) const = default;
};

// A pad's volume (index 0) or pan (index 1) envelope
struct Envelope
{
    std::int32_t index = 0, indexReserved = 0;        // chunk 0x67
    std::uint32_t headA = 1, headB = 3;               // chunk 0x68: two constants, then the point count
    std::vector<EnvelopePoint> points;
    std::array<std::int32_t, 9> tail { 0, 0, -1, -1, -1, 128, 128, 0, 128 }; // sustain / loop points and four constants
    std::vector<std::uint8_t> unparsed;               // the 0x68 body when it does not have that layout (then the rest is unused)

    bool operator== (const Envelope&) const = default;
};

struct Layer
{
    std::u16string path; // with FL's variables, e.g. %FLStudioFactoryData%
    // chunk 0x12D (7 × i32 in every state seen)
    std::int32_t volume = 127;       // 0…127
    std::int32_t pan = 0;            // −128…128
    std::int32_t velocityLow = 0;    // inclusive
    std::int32_t velocityHigh = 127; // exclusive, except 127
    std::int32_t flags = 1;          // bit 0 always set; bit 1 = reverse (hypothesis)
    std::int32_t tune = 0;           // −128…128, × 9.375 cents
    std::int32_t region = -1;        // −1 = the whole sample, else the index of a region of the WAV
    std::vector<std::int32_t> moreParams; // values after the seventh, if a later version adds some
    std::vector<RawChunk> extra;

    bool operator== (const Layer&) const = default;
};

enum class LayerMode : std::int32_t
{
    random = 0,
    randomAvoidPrevious = 1,
    cycleBackward = 2,
    cycle = 3
};

struct Pad
{
    // chunk 0x64 (16 × i32)
    std::int32_t volume = 99; // 0…128
    std::int32_t pan = 0;     // −128…128
    std::int32_t flags = 0;   // bit 0 mute, bit 1 solo, bit 2 (not decoded), bit 3 one layer at a time (`mode`)
    std::int32_t note = 60;   // the MIDI note that triggers the pad
    std::int32_t output = 0;  // mixer track offset, −99…103
    std::int32_t cut = -1;    // group this pad cuts, −1 = none
    std::int32_t cutBy = -1;  // group that cuts this pad, −1 = none
    std::int32_t tune = 0;    // −128…128, × 9.375 cents
    std::int32_t colour = 0;
    std::int32_t icon = 0;
    std::array<std::int32_t, 6> reserved {};
    std::vector<std::int32_t> moreParams;

    std::u16string name;                                       // chunk 0x65
    std::array<std::int32_t, 8> trigger { 1, 0, 0, 0, 0, 0, 0, 0 }; // chunk 0x66; [0] = LayerMode
    std::vector<Layer> layers;                                 // chunks 0x69
    std::vector<Envelope> envelopes;                           // chunk pairs 0x67 + 0x68
    std::vector<RawChunk> extra;

    bool muted() const noexcept { return (flags & 1) != 0; }
    bool soloed() const noexcept { return (flags & 2) != 0; }
    bool oneLayerAtATime() const noexcept { return (flags & 8) != 0; }
    LayerMode mode() const noexcept { return (LayerMode) trigger[0]; }

    bool operator== (const Pad&) const = default;
};

struct Kit
{
    std::uint32_t version = 0x1E84E403;
    std::vector<Pad> pads;                           // chunks 2: bank A = pads 0…15, bank B = 16…31
    std::optional<std::u16string> midiLoop;          // chunk 1: the last MIDI loop chosen in the loop browser
    std::optional<std::vector<std::uint8_t>> option3; // chunk 3 (one byte, an editor option)
    std::optional<std::vector<std::uint8_t>> option5; // chunk 5 (one byte, an editor option)
    std::optional<std::vector<std::uint8_t>> uiState; // chunk 4 (32 bytes, editor state)
    std::vector<RawChunk> extra;

    bool operator== (const Kit&) const = default;
};

constexpr std::uint32_t oldestVersion = 0xF424B;   // 1,000,011: the first version with chunks (FL Mobile's kits; text of one byte per character)
constexpr std::uint32_t newestVersion = 0x1E84E403; // what FL 2026 writes

// The state as a kit; nothing when it is not an FPC state LYNX can read (too short, or a version outside
// oldestVersion…newestVersion). A chunk that runs past the end ends its list there (a cut-off state keeps the
// pads before the cut).
std::optional<Kit> read (const std::vector<std::uint8_t>& state);

// The kit as a state. write (*read (s)) == s for every state FL writes.
std::vector<std::uint8_t> write (const Kit& kit);

// FL's "Empty" kit: 32 pads without a sample on FL's default notes (bank A the General MIDI drum notes, bank B
// C5 upwards), each with one empty layer and FL's default envelopes.
Kit emptyKit();

std::string toUtf8 (const std::u16string& text);
std::u16string fromUtf8 (const std::string& text);
} // namespace lynx::fpc
