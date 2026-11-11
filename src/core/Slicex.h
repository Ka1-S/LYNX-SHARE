#pragma once

// FL's Slicex (rounds 188c / 190h, docs/analise/REA_rodada190h.md): its saved state (FL's 0xD5 stream, read and
// written byte for byte), the regions FL reads from a deck's WAV (cue points with their labels, lengths and keys), the
// key map FL builds from them, and the laws that turn the stored integers into what a voice plays. Clean-room: layouts,
// formulas and constants in our own words; no Image-Line code or data.
//
// The stream: version, 8 masters, the options block, 3 editor ints, the two decks (A, B: sample, view settings, four
// editor curves, one 17-int record per region), then 8 articulator groups (a filter header and six articulators:
// pan, volume, cutoff, resonance, speed, sample start; each an envelope, an LFO and four maps).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lynx::slicex
{
// A point of FL's curve record (envelopes, LFO amplitude, maps, the decks' editor curves)
struct Point
{
    double dt = 0.0, y = 0.0;   // time since the previous point (0.5 s units, beats when tempo-based; a map's x) and level 0…1
    float tension = 0.0f;       // −1…1
    std::uint8_t mode = 0;      // FL's curve mode (0 single curve, 2 hold, 6 wave…)
    std::uint8_t b1 = 0, b2 = 0, b3 = 0; // kept as saved (b3: the direction FL recomputes when it prepares the curve)

    bool operator== (const Point&) const = default;
};

struct Curve
{
    std::int32_t kind = 1;      // 1 envelope, 2 LFO, 3 map, 6 a deck's editor curve
    std::int32_t version = 3;
    std::vector<Point> points;
    std::int32_t flags = 0;     // bit 0 tempo-based time, bit 1 global LFO phase, bit 2 unipolar LFO shape, bit 3 frozen LFO
    std::int32_t enabled = 0;   // 0 = off (FL writes −1 for on)
    std::int32_t decay = -1, loop = -1, sustain = -1; // point indices, −1 = none

    bool on() const noexcept { return enabled != 0; }
    bool tempoBased() const noexcept { return (flags & 1) != 0; }
    bool operator== (const Curve&) const = default;
};

// The articulator targets, in the order FL stores them in a group
enum Target { pan = 0, volume, cutoff, resonance, speed, sampleStart, targetCount };
// The four maps of an articulator, in stored order
enum Map { mapModX = 0, mapModY, mapVelocity, mapRandom, mapCount };

struct Articulator
{
    std::int32_t version = 3;
    float base = 0.0f, envDepth = 1.0f, lfoDepth = 1.0f; // (the depths are overwritten by the group's ENV wheel and the master LFO)
    Curve envelope;
    std::array<std::int32_t, 4> envKnobs { 128, 128, 0, 128 };    // ATT, DEC, SUS, REL (0…256; 128 = ×1)
    Curve lfo;                                                    // the LFO's amplitude envelope (enabled = the LFO is on)
    std::array<std::int32_t, 5> lfoKnobs { 31102, 0, 0, 0, 0 };   // speed, tension, skew, pulse width, phase offset
    std::array<Curve, mapCount> maps;

    bool operator== (const Articulator&) const = default;
};

struct Group
{
    // the filter header: ENV (cutoff envelope / LFO amount 0…256), CUT and RES offsets (0…256, 128 = none), filter type
    // (0 low-pass, 1 band-pass, 2 notch, 3 high-pass) and order (0 off, 1…3 stages)
    std::array<std::int32_t, 5> header { 128, 128, 128, 0, 0 };
    std::array<Articulator, targetCount> art;

    int filterEnv() const noexcept { return header[0]; }
    int filterCut() const noexcept { return header[1]; }
    int filterRes() const noexcept { return header[2]; }
    int filterType() const noexcept { return header[3]; }
    int filterOrder() const noexcept { return header[4]; }
    bool operator== (const Group&) const = default;
};

// A region's 17 stored ints (FL's region panel)
enum RegionField
{
    rCutGroup = 0,   // 0…99, 0 = none
    rOutput,         // output track offset −99…99
    rPan,            // −6400…6400
    rVolume,         // 0…16000, 8000 = unity
    rAmpGroup,       // articulator group −1…7 (−1 = none) for pan and volume
    rAmpAmount,      // 0…12800
    rCutoff,         // 0…256
    rResonance,      // 0…256
    rFilterGroup,
    rFilterAmount,
    rPitch,          // cents −1200…1200
    rSpeedGroup,
    rSpeedAmount,
    rDelay,          // 0…12800 = up to one beat
    rStart,          // 0…12800: sample start (100 ms, or the whole region with the option)
    rStartGroup,
    rStartAmount,
    regionFieldCount
};
using RegionRecord = std::array<std::int32_t, regionFieldCount>;
RegionRecord defaultRegion() noexcept;

// What a voice uses of a region record (FL's conversion, single precision)
struct RegionValues
{
    int cutGroup = 0, output = 0;
    int ampGroup = 0, filterGroup = 0, speedGroup = 0, startGroup = 0; // −1 = none
    float pan = 0.0f, volume = 1.0f, ampAmount = 1.0f, cutoff = 0.5f, resonance = 0.5f, filterAmount = 1.0f;
    float pitchCents = 0.0f, speedAmount = 8.0f, delayBeats = 0.0f, start = 0.0f, startAmount = 2.0f;
};
RegionValues regionValues (const RegionRecord& r) noexcept;

struct Deck
{
    std::uint8_t referenceOnly = 1;     // 1 = the sample is the file at `path`; 0 = its WAV is embedded
    bool hasPath = true;                // (always, from version 3 on)
    bool longPath = false;              // the path was saved in the long form (kept for a byte-exact rewrite)
    std::string path;                   // FL's path variables kept (%FLStudioFactoryData%…)
    std::vector<std::uint8_t> wav;      // the embedded WAV (FL's own writer output: cue points, labels, keys)
    std::array<std::uint8_t, 76> params {}; // the editor's view settings (selection, shown range in beats…), as saved
    std::array<Curve, 4> curves;        // the editor's curves (kind 6)
    std::vector<RegionRecord> regions;  // one per region of the sample, in sample order (an empty deck keeps one)

    bool empty() const noexcept { return wav.empty() && path.empty(); }
    // the beat grid of the deck's sample (beats): start and end; the tempo FL shows is beats · 60 / duration
    // the part of the sample the editor shows (beats), as saved: start and end. A deck saved zoomed out has the
    // sample's whole beat count here; one saved zoomed in has less (round 190ac: FL showed 60BPM for 8 beats and saved
    // 0..6.845 with the view zoomed), so the tempo is NOT taken from here when the sample says its own (beats())
    double beatStart() const noexcept;
    double beatEnd() const noexcept;
    // the sample's beat count: the embedded WAV's own (FL writes it there, ACID chunk; also for a sample that came
    // without one), else the saved view range (a deck by reference, as LYNX writes one: its whole length)
    double beats() const noexcept;
    // the editor's selection, in beats of the sample's grid like the shown range (start, end), −1 / −1 = none
    // (round 190aj, FL's own saves: a drag over 160 px of a deck showing 0.576…7.421 beats saved 1.78309…3.31693)
    double selectionStart() const noexcept;
    double selectionEnd() const noexcept;
    bool hasSelection() const noexcept;
    // FL's View ▸ Loop (the loop points are drawn; "Set loop" switches it on)
    bool showsLoop() const noexcept { return params[0x36] != 0; }
    bool operator== (const Deck&) const = default;
};

// Offsets in the options block
enum Option
{
    oLayering = 0x00,     // i32: 0 all, 1 all (gain-compensated), 2 velocity, 3 Mod X, 4 Mod Y, 5 random, 6 cycle, 7 song time
    oCrossfade = 0x04,    // i32: 0 none, 1 linear, 2 balanced curve, 3 unbalanced curve, 4 adding
    oDisplayMode = 0x10,  // i32: 0 full, 1 no audio editor, 2 no region settings, 3 compact
    oArtGroup = 0x14,     // i32: the articulator group shown (0…7)
    oArtPart = 0x18,      // i32: the articulator part shown (0 envelope, 1 LFO, 2 velocity, 3 Mod X, 4 Mod Y, 5 random)
    oArtTarget = 0x1c,    // i32: the articulator target shown
    oLoop = 0x21,         // u8: the deck's loop button
    oNoAutoScroll = 0x22,
    oModSmooth = 0x25,    // u8: "Smooth up modulation"
    oAutoDump = 0x26,     // u8: "Dump deck A to piano roll after editing"
    oDualDeck = 0x27,
    oStartWholeLength = 0x28, // u8: "Sample start covers whole length"
    oHqEnvelopes = 0x29,
    oSameDeckOnly = 0x2a, // u8: "Layer regions of same deck only"
    oVelocityToVolume = 0x2b, // u8: "Link velocity to volume"
    oKeyboard = 0x2c,
    oLoopHalf = 0x2d,     // u8: "Loop half of regions"
    oNoEmbed = 0x2e       // u8: "Leave unedited audio data on disk"
};

// The masters
enum Master { mVolume = 2, mRandom = 3, mLfo = 4, mPitch = 5, mModX = 6, mModY = 7 };

struct State
{
    std::uint32_t version = 0x00060003u;
    std::array<std::int32_t, 8> masters { 0, 0, 12800, 256, 128, 0, 256, 256 };
    std::vector<std::uint8_t> options;  // 47 bytes (46 before version 2)
    std::array<std::int32_t, 3> ui { 0, 0, 3 }; // the deck shown, the region shown (deck << 16 | index), the keyboard's base octave
    std::array<Deck, 2> decks;
    std::array<Group, 8> groups;
    std::vector<std::uint8_t> trailer;  // anything after the groups (none in FL's own states)

    std::int32_t optionInt (int offset) const noexcept;
    void setOptionInt (int offset, std::int32_t v) noexcept;
    std::uint8_t optionByte (int offset) const noexcept;
    void setOptionByte (int offset, std::uint8_t v) noexcept;
    bool operator== (const State&) const = default;
};

// FL's 0xD5 state (versions 1…6, as 0x10000…0x60003 or plain); nullopt when it is not a Slicex state LYNX can read
std::optional<State> parse (const std::vector<std::uint8_t>& bytes);
// The state as FL writes it (the same bytes for a state FL wrote)
std::vector<std::uint8_t> write (const State& state);
// A new Slicex: FL's Default preset values, both decks empty
State fresh();
// Puts a sample file on a deck (by reference; its regions come from the file's cue points): one default record per region
void setDeckFile (State& state, int deck, const std::string& path, int regions);
// the deck's shown range in beats (Deck::beatStart / beatEnd); for a deck without an embedded WAV also its beat count
void setDeckBeats (State& state, int deck, double start, double end);
// The beat count a deck plays and shows its tempo with: the embedded WAV's own; for a deck by reference the sample
// file's (`fileBeats`: SlicexInstrument's sampleBeats(), 0 = not known); else the saved range (round 190aj: the saved
// range is the editor's zoom, so it only stands in when nothing else says the beat count)
double deckBeats (const Deck& deck, double fileBeats) noexcept;
// the editor's selection of a deck in beats, as FL keeps it: sorted; an empty one (from == to) is none, −1 / −1
void setDeckSelection (State& state, int deck, double from, double to);
// The frames a range in beats covers, as FL turns a selection into a region or a loop: [first, last + 1), each end
// rounded to the nearest frame (round 190aj: 1.4117647…2.1960784 beats of a 44 100 frames-a-beat sample became the
// region 62259…96846, and 2.5322129…3.6526611 the loop 111671…161081)
struct FrameRange { std::int64_t first = 0, end = 0; };
FrameRange framesOf (double fromBeats, double toBeats, double framesPerBeat, std::int64_t frames) noexcept;

// One step of FL's "Zoom in" / "Zoom out": the shown length × 2^∓0.225 about its centre (round 190aj: two steps from
// 0…8 beats saved 1.0718286…6.9281714)
inline constexpr double zoomStep = 0.8555950256826;
// "Zoom on left / right of selection": FL shows half a frame a pixel (357 frames across its 714 pixels) centred on
// that edge of the selection
inline constexpr double zoomEdgeFramesPerPixel = 0.5;

// The beat count FL gives a loaded sample that carries no tempo (no ACID beat count): the power of two that puts its
// tempo in 80 < BPM ≤ 160 (round 190ac, nine files loaded in FL: 3.000 s → 8 beats = 160 BPM, 2.981 s → 4 = 80.5,
// 1.370 s → 2 = 87.6; the project's tempo, 120, plays no part)
double guessBeats (double seconds) noexcept;
// The beat count a WAV carries (ACID chunk), 0 without one. The tempo FL shows is beats · 60 / duration
double wavBeats (const std::vector<std::uint8_t>& wav) noexcept;

// ---- the laws (FL's single-precision arithmetic)
float masterVolume (std::int32_t raw) noexcept;      // 0…16000, 12800 = 1: (11^x − 1)/10
float masterBipolar (std::int32_t raw, int unit) noexcept; // randomness (unit 256) and LFO (unit 128): the same law on |x|, sign kept
float regionVolume (std::int32_t raw) noexcept;      // 0…16000, 8000 = 1, 16000 = 4: (3^x − 1)/2
// cutoff and resonance offsets of a group, −0.5…0.5
inline float groupOffset (std::int32_t raw) noexcept { return (float) (raw - 128) * 0.00390625f; }

// ---- regions of a deck's WAV, as FL's sample object builds them
struct WavRegion
{
    std::int64_t start = 0, end = 0;    // frames, end inclusive (a marker runs to the next region's start − 1)
    std::string name;
    int key = -1;                       // the region's own MIDI key (FL's trigger list), −1 = automatic
    bool marker = true;                 // a cue point without a length
    std::string info;                   // FL's "Set info" text (a `note` entry)
    std::string purpose;                // the length entry's four-letter purpose as saved ("beat", "rgn "…), "" = none
};
// `frames` receives the audio length (0 when the data chunk is missing)
std::vector<WavRegion> wavRegions (const std::vector<std::uint8_t>& wav, std::int64_t& frames);
// The WAV with its regions' keys set (FL's trigger list chunk, rewritten): keys[i] is region i's key in the order
// wavRegions() returns them (−1 = automatic). An empty result: `wav` is not a WAV with cue points.
std::vector<std::uint8_t> withRegionKeys (const std::vector<std::uint8_t>& wav, const std::vector<int>& keys);
// The WAV with its region list replaced by `regions` (cue points, names, lengths, keys — the chunks FL's writer
// lays out as cue, LIST/adtl, tlst): what the editor's rename / delete / clone / move / add do. The regions are
// written in the order given (sample order, as wavRegions() returns them). An empty result: `wav` is not a WAV.
std::vector<std::uint8_t> withRegions (const std::vector<std::uint8_t>& wav, const std::vector<WavRegion>& regions);
// The loop points of a WAV (its sampler chunk's first loop): first and last frame; false without one
bool wavLoop (const std::vector<std::uint8_t>& wav, std::int64_t& first, std::int64_t& last);
// The WAV with its loop set (FL's "Set loop"): the sampler chunk's loop rewritten, or a sampler chunk with one loop
// added after the audio when the file has none. An empty result: `wav` is not a WAV with audio.
std::vector<std::uint8_t> withLoop (const std::vector<std::uint8_t>& wav, std::int64_t first, std::int64_t last);
// FL's name for a region without one: "Marker #n" / "Region #n"
std::string regionName (const WavRegion& r, int index);

// ---- the key map: per key, the regions a note on a deck plays (deck << 16 | region index), in FL's order
using KeyMap = std::array<std::vector<std::int32_t>, 128>;
// the key of the first automatically mapped region for a deck of n regions (60, an octave lower per 12 regions past 65)
int autoBaseKey (int regions) noexcept;
KeyMap keyMap (int deck, const std::vector<WavRegion>& deckA, const std::vector<WavRegion>& deckB, bool sameDeckOnly);
} // namespace lynx::slicex
