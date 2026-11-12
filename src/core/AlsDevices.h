#pragma once

// Live's own devices in a set (.als) → LYNX's own instruments and effects, with their settings (round 190bq; the
// "N3 table" of docs/analise/ALS_FORMAT.md §8). Clean-room: only the device's saved state is read (member names,
// values and each parameter's own range, `MidiControllerRange`, as the sets on this machine hold them); the LYNX
// engines have their own names and DSP. Every conversion is a law in a unit (Hz, dB, ms, ratio, Q), written here
// so the tests can pin it; what has no counterpart is returned as a note for the import report, never dropped
// silently. Evidence tags: [DATA] read in saved sets, [DOC] Ableton's manual, [ASSUMED] not confirmed in Live (the
// handoff lists each one for an A/B in Live).
//
//   Simpler (OriginalSimpler)      → the LYNX Sampler channel (what the zones / envelope import did not read)
//   EQ Eight (Eq8)                 → Facet EQ
//   Compressor (Compressor2)       → Crest Compressor, Clean style
//   Glue Compressor                → Crest Compressor, Bus style
//   Utility (StereoGain)           → Utility
//   Reverb                         → Atrium

#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
namespace xml
{
class Element;
}

namespace alsdev
{
// What the import report shows for one device, besides the device itself
struct Notes
{
    std::vector<std::string> approximated; // kept, but not the same law / range (clamped, fitted, nearest choice)
    std::vector<std::string> dropped;      // a setting with no LYNX counterpart (the device itself is kept)
};

// ---- shared laws ------------------------------------------------------------------------------------------------
double dbOfGain (double linear) noexcept;       // 20·log10; −200 dB at or below 0
double gainOfDb (double db) noexcept;

// ---- EQ Eight → Facet EQ ----------------------------------------------------------------------------------------
// The band's filter type, in the order Live stores it [DATA: Mode 0…7; the defaults of bands 1…8 and the manual's list]
enum class Eq8Mode { lowCut48, lowCut12, lowShelf, bell, notch, highShelf, highCut12, highCut48 };
// Facet EQ's Q for a Live Q, per filter family. Facet's prototypes (docs/analise/MEASURE_fabfilter.md §1):
//   bell / notch   Qb = Qfacet / √2 in the constant-Q biquad  → Qfacet = √2 · Q
//   shelves        Qs = 0.7071 · Qfacet^0.4697                → Qfacet = (Q / 0.7071)^(1 / 0.4697)
//   12 dB cuts     section Q = 0.7071 · Qfacet                → Qfacet = Q / 0.7071
//   48 dB cuts     Live's "x4" = four equal 12 dB sections in series [ASSUMED]: 4 · 20·log10(Q) dB at the cut
//                  frequency; Facet's 48 dB cut holds 20·log10(Qfacet / √2) there → Qfacet = √2 · Q⁴
// (clamped to Facet's 0.025 … 40)
double facetQ (Eq8Mode mode, double liveQ) noexcept;
struct Eq8Band
{
    bool on = true;
    Eq8Mode mode = Eq8Mode::bell;
    double hz = 1000.0, gainDb = 0.0, q = 0.7071;
};
// Writes one band into a Facet EQ parameter set; `gainSign` −1 folds a negative Scale into the gains
void setFacetBand (EffectParams& params, int facetBand, const Eq8Band& band, double gainSign, int placement);
// The device → a Facet EQ slot (false: the element holds no EQ Eight band data)
bool eqEightToFacet (const xml::Element& device, EffectSlot& slot, Notes& notes);

// ---- Compressor / Glue Compressor → Crest Compressor ------------------------------------------------------------
// Glue's stepped controls [DOC]: attack 0.01 / 0.1 / 0.3 / 1 / 3 / 10 / 30 ms, release 0.1 / 0.2 / 0.4 / 0.6 / 0.8 /
// 1.2 s and Auto, ratio 2 / 4 / 10 [DATA: the indices' ranges 0…6, 0…6, 0…2]
double glueAttackMs (int index) noexcept;
double glueReleaseMs (int index) noexcept; // 0 for Auto (index 6)
double glueRatio (int index) noexcept;
// Compressor's look-ahead choices [DOC]: 0, 1, 10 ms
double compressorLookaheadMs (int index) noexcept;
// A side-chain input the device names: the set's track id (−1 = none or not a track), and whether Live taps it
// somewhere LYNX cannot (LYNX's side chain is the insert's post-fader output)
struct SideInput
{
    bool on = false;
    int trackId = -1;
    bool otherTap = false;
};
bool compressorToCrest (const xml::Element& device, EffectSlot& slot, Notes& notes, SideInput& side);
bool glueToCrest (const xml::Element& device, EffectSlot& slot, Notes& notes, SideInput& side);

// ---- Utility → Utility ------------------------------------------------------------------------------------------
struct LiveUtility
{
    double gain = 1.0;        // linear; 0 = −inf dB [DATA: 0 … 56.23 = +35 dB]
    bool mute = false, phaseL = false, phaseR = false, mono = false, bassMono = false, dcFilter = false;
    int channelMode = 1;      // 0 Left, 1 Stereo, 2 Right, 3 Swap [DATA: 0…3, 1 in every saved device; order ASSUMED]
    double width = 1.0;       // 0 … 4 (0 … 400 %)
    bool midSideOn = false;   // the Width control shows Mid/Side balance instead
    double midSide = 1.0;     // 0 mid only … 1 neutral … 2 side only
    double balance = 0.0;     // −1 … 1
    bool operator== (const LiveUtility&) const = default;
};
// out L = ll·L + rl·R, out R = lr·L + rr·R (before gain and balance)
struct StereoMatrix
{
    double ll = 1.0, rl = 0.0, lr = 0.0, rr = 1.0;
};
// Phase switches, then channel mode, then Mono or Width (M = (L + R) / 2, S = (L − R) / 2; out = M ± width·S)
StereoMatrix utilityMatrix (const LiveUtility& u) noexcept;
// True when LYNX's Utility does exactly what these settings say: plain gain within ±24 dB or mute, phase switches,
// channel mode, Mono, Width 0 % or 100 %, balance centred, no Bass Mono / DC filter
bool utilityIsExact (const LiveUtility& u) noexcept;
LiveUtility readUtility (const xml::Element& device);
bool utilityToUtility (const xml::Element& device, EffectSlot& slot, Notes& notes);
// The other way, for the export (decision D: only what is exact): a LYNX Utility slot as Live's settings, nullopt
// when it holds anything Live's device cannot say exactly (a balance, a free matrix)
std::optional<LiveUtility> liveUtilityOf (const EffectSlot& slot) noexcept;

// ---- Reverb → Atrium (fitted by reading, not by measurement) ----------------------------------------------------
// Live's room size (0.22 … 500, 100 by default) on Atrium's Late size (0 … 1, 0.5 by default): log on each side
float atriumSizeOf (double liveRoomSize) noexcept;
// Reflect and Diffuse levels (linear gains) as Atrium's Early / late (equal power: 0 early only … 1 late only)
float atriumEarlyLateOf (double reflect, double diffuse) noexcept;
// The input filter (centre Hz, width in octaves [ASSUMED]) as the edges Atrium's Low cut / High cut take
double reverbInputLowHz (double centreHz, double widthOct) noexcept;
double reverbInputHighHz (double centreHz, double widthOct) noexcept;
bool reverbToAtrium (const xml::Element& device, EffectSlot& slot, Notes& notes);

// ---- one entry point for the importer ---------------------------------------------------------------------------
struct Translated
{
    EffectSlot slot;
    std::string from, to; // "EQ Eight", "Facet EQ": the report's line is "<from> -> <to>"
    bool approximate = false; // the pair is fitted, not the same maths (the report lists it under "approximated")
    Notes notes;
    SideInput side;
};
// nullopt: not one of the devices above, or its element lacks the parameters (then the importer does what it did
// before: the device is dropped and listed)
std::optional<Translated> effectFromLiveDevice (const xml::Element& device);

// ---- Simpler ----------------------------------------------------------------------------------------------------
// Voices [DOC: 1 … 8, 10, 12, 14, 16, 20, 24, 32; DATA: the stored index 0 … 14, 5 in a new Simpler = 6 voices]
int simplerVoices (int index) noexcept;
// The loop of Classic mode in frames: the part's region [sampleStart, sampleEnd), played from `start` for `length`
// of what is left, the last `loopLength` of that looping [DOC: Start, Length, Loop as fractions; ASSUMED how they nest]
struct SimplerRegion
{
    double start = 0.0, end = 0.0, loopStart = 0.0;
};
SimplerRegion simplerRegion (double sampleStart, double sampleEnd, double start, double length, double loopLength) noexcept;
struct SimplerResult
{
    int keyShift = 0;     // Slice mode: Live's first slice plays on C1 (36), LYNX's on key 60 — the notes move by this
    bool loop = false;    // Classic mode with Loop on: `loopStart` / `loopEnd` (frames) go on the zone
    std::int64_t loopStart = 0, loopEnd = 0;
};
// Reads what the zone / envelope import leaves out into `channel` (already a sampler with its sample or zones):
// transpose, volume, pan, filter and its envelope, pitch envelope, LFO, voices, glide, the playback mode (one-shot
// fades, slices), the region and loop, warp. `fileSeconds` 0 = unknown (the set's own DefaultDuration is used).
SimplerResult applySimpler (const xml::Element& device, ChannelDef& channel, double bpm, double fileSeconds, Notes& notes);
} // namespace alsdev
} // namespace lynx
