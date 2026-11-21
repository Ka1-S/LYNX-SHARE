#pragma once

// Helix: LYNX's own wavetable synthesizer (round 190ja, build list 17b, owner decision O13). LYNX's own engine, name,
// look, code and wave tables; Ableton Live's Wavetable device imports onto it (core/AlsDevicesWt.cpp) with its own
// parameter values, so the parameters below use Wavetable's units and ranges as a Live set stores them (times in
// seconds, gains linear, transpose in semitones, filter in Hz, the modulation matrix's amounts −1…1). The laws that make
// those values sound like Live were measured black-box on Live 12 renders (docs/analise/REA_rodada190ja.md); no Ableton
// code, preset, sample or wave table is used. Helix's tables are generated here, additively, from LYNX's own recipes
// (core/WavetableTables.cpp); Live's table names map onto the closest of them on import.
//
// A voice: two wavetable oscillators (table, position, an effect: FM / Classic (pulse width, sync) / Modern (warp,
// fold), transpose, detune, pan, gain), a sub oscillator, two filters (low / high / band pass, notch, morph; 12 or 24
// dB/oct; Live's circuits) routed serial / parallel / split, an amp envelope and two more envelopes, two LFOs, a
// modulation matrix (13 sources × 52 targets), unison (six modes, 2…8 voices), glide, mono / poly, and MPE (per-note
// pitch, slide, pressure as matrix sources). Band-limited: every table is kept as mipmaps (one per octave, no partial
// above Nyquist at the note played); the phase effects pick a lower mip by their stretch; sync is corrected at its
// resets (polyBLEP) and fold is antiderivative anti-aliased. No allocation on the audio thread.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
struct ChannelDef;

namespace wt
{
// --- parameter numbers (the channel's modelParams[index]; Wavetable's units)
namespace osc
{
constexpr int on = 0, transpose = 1, detune = 2, position = 3, effectMode = 4, effect1 = 5, effect2 = 6, pan = 7, gain = 8, stride = 9;
}
constexpr int oscBase (int k) noexcept { return k * osc::stride; }
constexpr int subOn = 2 * osc::stride, subTone = subOn + 1, subGain = subOn + 2, subTranspose = subOn + 3;
namespace flt
{
constexpr int on = 0, type = 1, circuitLpHp = 2, circuitBpNoMo = 3, slope = 4, freq = 5, res = 6, drive = 7, morph = 8, stride = 9;
}
constexpr int filterBase (int k) noexcept { return subTranspose + 1 + k * flt::stride; }
constexpr int routing = filterBase (2);
// envelopes: the amp envelope has no values (it goes 0 → 1 → sustain → 0); envelopes 2 and 3 have four
namespace env
{
constexpr int attack = 0, decay = 1, release = 2, attackSlope = 3, decaySlope = 4, releaseSlope = 5, sustain = 6, loopMode = 7,
              initial = 8, peak = 9, final = 10, ampCount = 8, count = 11;
}
constexpr int ampEnv = routing + 1;
constexpr int envBase (int k) noexcept { return k == 0 ? ampEnv : ampEnv + env::ampCount + (k - 1) * env::count; } // k 0…2
namespace lfo
{
constexpr int retrigger = 0, type = 1, amount = 2, shaping = 3, phase = 4, sync = 5, rate = 6, syncedRate = 7, attack = 8, stride = 9;
}
constexpr int lfoBase (int k) noexcept { return envBase (2) + env::count + k * lfo::stride; }
constexpr int timeScale = lfoBase (2), modAmount = timeScale + 1, unisonMode = timeScale + 2, unisonVoices = timeScale + 3,
              unisonAmount = timeScale + 4, transpose = timeScale + 5, glide = timeScale + 6, volume = timeScale + 7,
              monoPoly = timeScale + 8, polyVoices = timeScale + 9, hiQ = timeScale + 10;
constexpr int paramCount = hiQ + 1; // the automatable values
// then (not automatable): each oscillator's table, then the modulation matrix [target][source]
constexpr int tableBase = paramCount;
constexpr int sourceCount = 13, targetCount = 52;
constexpr int matrixBase = tableBase + 2;
constexpr int matrixIndex (int target, int source) noexcept { return matrixBase + target * sourceCount + source; }
constexpr int liveStateSize = matrixBase + targetCount * sourceCount; // Live Wavetable's values (778): a project before 190mi

// --- Helix+ (round 190mi): LYNX's own extension block after Live's values (Serum-class features, clean-room). Live's
// import / export never touch it. `revision` 0 (a channel saved before 190mi: the block is missing and reads as its
// defaults) ignores every value below, so old projects render bit for bit as before; 1 (new channels, the factory
// presets, any edit of a Helix+ control) makes it live. Every value defaults to "off": a revision 1 channel that uses no
// Helix+ control sounds exactly as revision 0.
constexpr int extBase = liveStateSize;
namespace ext
{
constexpr int revision = 0;                                   // (state, not automatable)
constexpr int warpMode = 1, warpAmount = 2, warpStride = 2;   // + 2 k: oscillator k's second warp (menu, 0…1)
constexpr int noiseOn = 5, noiseLevel = 6, noiseColour = 7;   // the noise oscillator
constexpr int unisonCount = 8, unisonBlend = 9, unisonWidth = 10, unisonStack = 11; // 0 = Live's count; 2…16
constexpr int filterType = 12;                                // + k: filter k's Helix+ type (0 = Live's own type)
constexpr int macro = 14;                                     // + j: macros 1…4 (0…1)
constexpr int lfoDraw = 18;                                   // + k: LFO k plays its drawn shape
constexpr int distOn = 20, distMode = 21, distDrive = 22, distMix = 23;
constexpr int chorusOn = 24, chorusRate = 25, chorusDepth = 26, chorusMix = 27;
constexpr int delayOn = 28, delayTime = 29, delayFeedback = 30, delayPingPong = 31, delayMix = 32;
constexpr int reverbOn = 33, reverbSize = 34, reverbDamp = 35, reverbMix = 36;
constexpr int autoEnd = 37;                                   // the automatable values are 1 … autoEnd − 1
constexpr int maxPoints = 16, pointStride = 3;                // a drawn LFO: count, then maxPoints × (x 0…1, y −1…1, curve −1…1)
constexpr int points = autoEnd;                               // + k × lfoPointsSize
constexpr int lfoPointsSize = 1 + maxPoints * pointStride;
constexpr int macroCells = points + 2 * lfoPointsSize;        // Live's 52 targets × the 4 macros
constexpr int extCells = macroCells + 52 * 4;                 // Helix+'s targets × every source (13 Live + 4 macros)
constexpr int count = extCells + 4 * 17;
} // namespace ext
constexpr int plusMacroCount = 4;                                  // 190mi's macros (their cells' layout above)
constexpr int plusSourceCount = sourceCount + plusMacroCount;      // 17: Live's 13 + Macro 1…4
constexpr int extTargetCount = 4, plusTargetCount = targetCount + extTargetCount; // 56: Live's 52 + Helix+'s 4

// --- Helix+ 2 (round 190ml): a second block after Helix+'s, gated by the same revision value (2 = this block live; 0 and 1
// render exactly as before). Oscillator 3 (Live's oscillator set + a second warp), the sample oscillator, user tables
// (their data in ChannelDef::pluginState, see HelixUserData), macros 5…8, two chaos sources, the frequency shifter, the
// EQ and the multiband compressor in the rack, anti-aliased hard-edged warps. Every value defaults to "off".
constexpr int revision190mi = 1, revision190ml = 2, revision190mt = 3, revision190oc = 4, currentRevision = revision190oc;
constexpr int newSourceCount = 6;  // Macro 5…8, Chaos 1, Chaos 2
constexpr int newTargetCount = 12; // oscillator 3's six rows and Warp 2, the sample's pitch / gain / pan, Shift, Multiband
constexpr int allSourceCount = plusSourceCount + newSourceCount;   // 23
constexpr int rev2TargetCount = plusTargetCount + newTargetCount;  // 68 (Helix+ 2's rows end here)
constexpr int newTargetCount3 = 12; // Helix round 3 (190mt): the granular and spectral oscillators' rows
constexpr int allTargetCount = rev2TargetCount + newTargetCount3;  // 80
constexpr int macroCount = 8;
constexpr int chaosCount = 2;
constexpr int ext2Base = extBase + ext::count;
namespace ext2
{
constexpr int reserved = 0;                                         // (state, not automatable)
constexpr int osc3 = 1;                                             // + osc::on … osc::gain: oscillator 3 (Live's set)
constexpr int osc3WarpMode = osc3 + osc::stride, osc3WarpAmount = osc3WarpMode + 1; // 10, 11
constexpr int sampleOn = 12, sampleTranspose = 13, sampleDetune = 14, sampleGain = 15, samplePan = 16, sampleStart = 17,
              sampleLoop = 18, sampleWarpMode = 19, sampleWarpAmount = 20;
constexpr int macro5 = 21;                                          // + j: macros 5…8 (0…1)
constexpr int chaosMode = 25, chaosRate = 26, chaosAmount = 27, chaosStride = 3; // + 3 k: chaos k + 1
constexpr int shiftOn = 31, shift = 32, shiftMix = 33;              // the frequency shifter (shift −1…1)
constexpr int eqOn = 34, eqLow = 35, eqMidFreq = 36, eqMid = 37, eqHigh = 38;
constexpr int mbOn = 39, mbDepth = 40, mbTime = 41, mbUp = 42, mbDown = 43, mbGain = 44;
constexpr int autoEnd = 45;                                         // the automatable values are 1 … autoEnd − 1
constexpr int osc3Table = autoEnd;                                  // oscillator 3's factory table (state)
constexpr int userTable = 46;                                       // + k: oscillator k plays its user table (0 / 1)
constexpr int sampleRoot = 49;                                      // the sample's root key
constexpr int cellsA = 50;                                          // 190mi's 56 targets × the new sources
constexpr int cellsB = cellsA + plusTargetCount * newSourceCount;   // the new targets × every source
constexpr int count = cellsB + newTargetCount * allSourceCount;
} // namespace ext2

// --- Helix round 3 (round 190mt): a third block after Helix+ 2's, gated by the same revision value (3 = this block live; 0,
// 1 and 2 render exactly as before). The granular oscillator (grains of the channel's sample), the spectral oscillator
// (the sample's partials resynthesised, warped and filtered) and the sample oscillator's band limit ("Clean"). Every value
// in defaults() is "off" (Clean included: a revision 2 channel taken to 3 keeps its sound); new channels and presets
// take helixDefaults() (Clean on).
constexpr int ext3Base = ext2Base + ext2::count;
namespace ext3
{
constexpr int reserved = 0;                                          // (state, not automatable)
constexpr int grainOn = 1, grainPosition = 2, grainSize = 3, grainDensity = 4, grainSpray = 5, grainPitch = 6, grainSpread = 7,
              grainGain = 8, grainPan = 9;
constexpr int specOn = 10, specPosition = 11, specStretch = 12, specShift = 13, specTilt = 14, specBand = 15, specWidth = 16,
              specPartials = 17, specTranspose = 18, specGain = 19, specPan = 20;
constexpr int sampleClean = 21;                                      // the sample oscillator reads band-limited levels
constexpr int autoEnd = 22;                                          // the automatable values are 1 … autoEnd − 1
constexpr int cells = autoEnd;                                       // the new rows × every source
constexpr int count = cells + newTargetCount3 * allSourceCount;
} // namespace ext3

// --- Helix round 4 (round 190oc): a fourth block after round 3's, gated by the same revision value (4 = this block live;
// 0 … 3 render exactly as before): unison for the granular and the spectral oscillators (voices, detune, width) and reverse
// grains. Every value in defaults() leaves the sound as it was (one voice, no reverse); no new matrix rows.
constexpr int ext4Base = ext3Base + ext3::count;
namespace ext4
{
constexpr int reserved = 0;                                          // (state, not automatable)
constexpr int grainUnison = 1, grainDetune = 2, grainWidth = 3, grainReverse = 4;
constexpr int specUnison = 5, specDetune = 6, specWidth = 7;
constexpr int autoEnd = 8;                                           // the automatable values are 1 … autoEnd − 1
constexpr int count = autoEnd;
} // namespace ext4
constexpr int stateSize = ext4Base + ext4::count;
constexpr int maxGrains = 16;        // grains sounding at once per voice
constexpr int maxGrainsUnison = 24;  // (round 4: with grain unison)
constexpr int maxSpectralPartials = 64;
constexpr int spectralFrames = 64;   // analysis frames over the sample
constexpr int maxOscUnison = 8;      // (round 4: the granular and spectral oscillators' unison voices)
constexpr int maxSpectralBank = 128; // (round 4: partials sounding per voice over all unison voices: two full banks)
constexpr double unisonDetuneCents = 50.0; // (round 4: Detune 1 = the outer voices ± 50 cents, as Classic unison)

// a matrix cell's value index for any target (Live's 52, Helix+'s 4, Helix+ 2's 12) and any source (Live's 13, the 8
// macros, the chaos sources)
constexpr int cellIndex (int target, int source) noexcept
{
    if (target >= rev2TargetCount) // (round 3's rows)
        return ext3Base + ext3::cells + (target - rev2TargetCount) * allSourceCount + source;
    if (source >= plusSourceCount && target < plusTargetCount)
        return ext2Base + ext2::cellsA + target * newSourceCount + (source - plusSourceCount);
    if (target >= plusTargetCount)
        return ext2Base + ext2::cellsB + (target - plusTargetCount) * allSourceCount + source;
    if (target < targetCount && source < sourceCount)
        return matrixIndex (target, source);
    if (target < targetCount)
        return extBase + ext::macroCells + target * plusMacroCount + (source - sourceCount);
    return extBase + ext::extCells + (target - targetCount) * plusSourceCount + source;
}
// an automatable value (Live's 0 … paramCount − 1, Helix+'s and Helix+ 2's)
constexpr bool automatable (int index) noexcept
{
    return (index >= 0 && index < paramCount) || (index > extBase + ext::revision && index < extBase + ext::autoEnd)
           || (index > ext2Base + ext2::reserved && index < ext2Base + ext2::autoEnd)
           || (index > ext3Base + ext3::reserved && index < ext3Base + ext3::autoEnd)
           || (index > ext4Base + ext4::reserved && index < ext4Base + ext4::autoEnd);
}
// oscillator k's (0…2) value of Live's oscillator set (osc::on … osc::gain), its table value and its second warp's values
constexpr int oscIndex (int k, int field) noexcept { return k < 2 ? oscBase (k) + field : ext2Base + ext2::osc3 + field; }
constexpr int oscTableIndex (int k) noexcept { return k < 2 ? tableBase + k : ext2Base + ext2::osc3Table; }
constexpr int warpModeIndex (int k) noexcept { return k < 2 ? extBase + ext::warpMode + k * ext::warpStride : ext2Base + ext2::osc3WarpMode; }
constexpr int warpAmountIndex (int k) noexcept { return k < 2 ? extBase + ext::warpAmount + k * ext::warpStride : ext2Base + ext2::osc3WarpAmount; }
constexpr int macroIndex (int j) noexcept { return j < plusMacroCount ? extBase + ext::macro + j : ext2Base + ext2::macro5 + j - plusMacroCount; }
constexpr int oscCount = 3;
// the warp modes of Helix+'s second warp
namespace warp
{
constexpr int off = 0, sync = 1, windowSync = 2, bendPlus = 3, bendMinus = 4, bendBoth = 5, pwm = 6, asymPlus = 7, asymMinus = 8,
              asymBoth = 9, mirror = 10, flip = 11, remap = 12, crush = 13, fm = 14, am = 15, rm = 16, count = 17;
}
// Helix+'s filter types (on top of Live's)
namespace xfilter
{
constexpr int live = 0, combPlus = 1, combMinus = 2, formant = 3, phaser = 4, count = 5;
}
constexpr int maxPolyVoices = 32;

// modulation sources (Live's matrix columns, in the set's order [DATA 190ja: 271 Core Library presets])
namespace src
{
constexpr int amp = 0, env2 = 1, env3 = 2, lfo1 = 3, lfo2 = 4, velocity = 5, note = 6, pitchBend = 7, pressure = 8, modWheel = 9,
              random = 10, notePitchBend = 11, slide = 12;
constexpr int macro1 = 13; // + j (Helix+, round 190mi: Macro 1…4; 190ml: 5…8 follow at 17…20)
constexpr int chaos1 = 21; // + k (Helix+ 2, round 190ml)
}
// modulation targets (Live's rows, in the set's order; the effect rows are named by the oscillator's effect mode)
namespace tgt
{
constexpr int oscPitch = 0, oscPos = 1, oscFx1 = 2, oscFx2 = 3, oscPan = 4, oscGain = 5, oscStride = 6; // + 6 k
constexpr int subTone = 12, subGain = 13;
constexpr int filterFreq = 14, filterRes = 15, filterDrive = 16, filterMorph = 17, filterStride = 4; // + 4 k
constexpr int ampAttack = 22, ampDecay = 23, ampRelease = 24, ampSustain = 25;
constexpr int env2 = 26, env3 = 33; // attack, decay, release, initial, peak, sustain, final
constexpr int lfo1 = 40, lfo2 = 43; // amount, shaping, rate
constexpr int time = 46, globalMod = 47, unison = 48, amp = 49, pitch = 50, glide = 51;
constexpr int warpAmount = 52, noiseLevel = 54, unisonBlend = 55; // Helix+ (round 190mi): warp amount + k
// Helix+ 2 (round 190ml): oscillator 3's rows (+ oscPitch … oscGain), its Warp 2, the sample's, the shifter, the multiband
constexpr int osc3 = 56, osc3Warp = 62, samplePitch = 63, sampleGain = 64, samplePan = 65, shift = 66, mbDepth = 67;
// Helix round 3 (round 190mt): the granular and spectral oscillators
constexpr int grainPos = 68, grainSize = 69, grainDensity = 70, grainSpray = 71, grainPitch = 72, grainGain = 73, specPos = 74,
              specStretch = 75, specShift = 76, specTilt = 77, specBand = 78, specGain = 79;
// oscillator k's (0…2) row of Live's oscillator rows (oscPitch … oscGain)
constexpr int oscRow (int k, int row) noexcept { return k < 2 ? k * oscStride + row : osc3 + row; }
constexpr int warpRow (int k) noexcept { return k < 2 ? warpAmount + k : osc3Warp; }
}

constexpr int effectNone = 0, effectFm = 1, effectClassic = 2, effectModern = 3;
constexpr int routeSerial = 0, routeParallel = 1, routeSplit = 2;
constexpr int unisonNone = 0, unisonClassic = 1, unisonShimmer = 2, unisonNoise = 3, unisonPhaseSync = 4, unisonPositionSpread = 5,
              unisonRandomNote = 6;
constexpr int maxUnison = 16;    // (Live's Unison Voices reach 8; Helix+'s count 16)
constexpr int liveMaxUnison = 8;

// --- wave tables (core/WavetableTables.cpp): LYNX's own, generated from recipes on first use, then kept
constexpr int frameCount = 64;                 // frames per table (the position moves through them)
constexpr int mipCount = 19;                   // half an octave apart: highest partial 1023, 723, 511 … 1
int mipLength (int mip) noexcept;              // samples per frame at mip m (4096 … 512; a guard sample follows)
int mipPartials (int mip) noexcept;            // the highest partial kept at mip m
struct Table
{
    std::string category, name;
    // the distinct frames, each mipLength (its mip) + 1 floats (the guard sample last): a frame that is bit for bit the same
    // as another of the same length (a static table's frames, the mips of a table with few partials) is kept once (190js)
    std::vector<float> pool;
    std::array<std::array<const float*, frameCount>, mipCount> frames {}; // [mip][frame] → into pool
    const float* frame (int mip, int f) const noexcept { return frames[(std::size_t) mip][(std::size_t) f]; }
    std::size_t bytes() const noexcept { return pool.size() * sizeof (float); } // the table's frame data
    Table() = default;
    Table (const Table&) = delete; // (frames point into pool)
    Table& operator= (const Table&) = delete;
};
int tableCount() noexcept;
const char* tableName (int index) noexcept;     // "Shapes"
const char* tableCategory (int index) noexcept; // "Basics"
std::vector<std::string> tableCategories();     // the menu's categories, in order
// the table (built on the first call: any thread but the audio thread; later calls return the same object, never freed)
const Table& table (int index);
// a built table or nullptr (any thread, the audio thread included: never builds)
const Table* tableIfBuilt (int index) noexcept;
// a frame's partial amplitudes and phases before the mipmaps (tests, the editor's drawing): `partials` values each
void tableSpectrum (int index, int frame, float* amplitude, float* phase, int partials);
// Live's table name ("Basic Shapes") → the closest LYNX table (−1: unknown name) and what differs, for the import report
struct TableMatch
{
    int table = 0;
    std::string note;
};
TableMatch matchLiveTable (const std::string& liveName);
// the Live table name an exported Helix table goes out as (the first Live table that maps onto it; "Basic Shapes" if none)
std::string liveTableFor (int index);
// a LYNX table by its own name (−1: none)
int tableByName (const std::string& name) noexcept;

// --- user tables and the sample oscillator's sample (Helix+ 2, round 190ml): the channel's own data, kept in
// ChannelDef::pluginState (encodeUserData), saved with the project and its presets, undoable as any channel edit
constexpr int maxUserPartials = 1023;
constexpr int userFrameLength = 2048; // a frame's samples when a table is drawn, imported, exported
struct UserTable
{
    std::string name;
    int partials = 0;              // per frame (0 = none)
    std::vector<float> amp, phase; // frameCount × partials: partial h + 1 of frame f at [f × partials + h]; sin (2π h x + φ)
    bool empty() const noexcept { return partials <= 0 || (int) amp.size() < frameCount * partials; }
};
struct SampleBuffer
{
    std::vector<float> l, r; // r empty: mono
    double rate = 44100.0;
    int frames() const noexcept { return (int) l.size(); }
};
// Helix round 3 (round 190mt): the sample's partials for the spectral oscillator, analysed once (analyseSpectrum) and kept
// with the channel's data: `frames` analysis frames spread over the sample, each `partials` tracks (a track keeps its
// slot from frame to frame; the loudest tracks first); hz / amp at [frame × partials + track], amp 0 = silent there
struct SpectralData
{
    int frames = 0, partials = 0;
    std::vector<float> hz, amp;
    bool empty() const noexcept { return frames <= 0 || partials <= 0 || (int) amp.size() < frames * partials || hz.size() != amp.size(); }
};
// the sample read band-limited (round 190mt): level k (1 …) holds the sample low-passed below 0.5 × 2^(−k/2) of its rate,
// every 2^shift-th sample kept (shift = max (0, k / 2 − 1): twice the rate its band needs); a level's samples start at
// index `guard`, with zeros before and after them
struct SampleLevels
{
    static constexpr int guard = 4;
    struct Level
    {
        std::vector<float> l, r; // (r empty: mono)
        int shift = 0;
        int frames = 0;          // its samples (the guards not counted)
    };
    std::vector<Level> levels;
};
struct HelixUserData
{
    std::array<UserTable, oscCount> tables; // per oscillator
    std::string sampleName;
    SampleBuffer sample; // (no frames: none)
    SpectralData spectral; // (190mt: the sample's partials; empty: not analysed)
    bool empty() const noexcept;
};
std::vector<std::uint8_t> encodeUserData (const HelixUserData& data); // (empty bytes when the data is empty)
bool decodeUserData (const std::vector<std::uint8_t>& bytes, HelixUserData& out); // false: not Helix data (out = empty)
// a user table's mipmaps (UI thread, never the audio thread): the same object while anyone holds it for the same data
std::shared_ptr<const Table> userTableBuilt (const UserTable& table);
// --- making user tables (the editor, the import; UI thread)
UserTable userTableOf (int factoryIndex);                         // a factory table's spectra (to edit it)
// cycles of `length` samples each (frames of them, spread over the 64 frames) → a table; DC dropped
UserTable userTableFromCycles (const std::vector<float>& cycles, int length, int frames, const std::string& name);
std::vector<float> userTableCycle (const UserTable& table, int frame, int length); // a frame's cycle (the editor's drawing)
void setUserTableCycle (UserTable& table, int frame, const std::vector<float>& cycle); // a frame from its cycle
enum class Slicing { pitch, fixed, spread };
struct ImportOptions
{
    Slicing slicing = Slicing::pitch;
    int frameSize = 2048;     // samples per frame (fixed; the window of spread)
    bool normalise = true;    // every frame at the same peak
    bool phaseAlign = true;   // every frame's fundamental starts at the same phase (no phasing as the position moves)
};
// audio (mono, at `rate`) → a table; `note` says what was found (the period, the frames used)
UserTable userTableFromAudio (const float* audio, int samples, double rate, const ImportOptions& options, const std::string& name,
                              std::string* note = nullptr);
double detectPeriod (const float* audio, int samples, double rate); // the strongest period in samples (YIN), 0: none
// --- Helix round 3 (round 190mt; any thread but the audio thread): the sample's partials (deterministic), and its
// band-limited levels (shared by content while anyone holds them)
SpectralData analyseSpectrum (const SampleBuffer& sample);
std::shared_ptr<const SampleLevels> sampleLevels (const SampleBuffer& sample);
// a formula → a table: x the phase (−1…1 over the cycle), t the position (0…1 over the frames); `error` says why not
bool userTableFromFormula (const std::string& formula, UserTable& out, std::string& error);
// the cycle as a WAV (float, 64 frames of userFrameLength) and back (a WAV of whole frames of `frameSize`, or any audio)
std::vector<std::uint8_t> userTableWav (const UserTable& table);

// --- laws (each is the engine's own code path; exposed for the tests, the editor and the import)
double filterHz (double normalised) noexcept;   // 0…1 → 20 Hz … 20.48 kHz (Live's frequency knob)
double filterNorm (double hz) noexcept;
double lfoSyncBeats (int index) noexcept;       // the synced rate's beats
float segment (float x, float slope) noexcept;  // a shaped envelope segment 0 → 1 (slope −1…1, 0 straight)
int polyVoiceCount (int index) noexcept;        // the voice chooser's index → voices
double fmRatio (double fmPitch) noexcept;       // FM Pitch −1…1 → the modulator's frequency ratio
double syncRatio (double sync) noexcept;        // Sync 0…1 → the synced wave's frequency ratio
double unisonCents (int mode, double amount) noexcept; // the widest unison voice's detune

const std::array<double, stateSize>& defaults() noexcept;
// Helix+ (round 190mi) laws, exposed for the tests and the editor
double warpStretch (int mode, double amount, double otherRatio) noexcept; // how much faster the warped phase can run (mip)
// a chaos source (Helix+ 2): mode 0 Smooth Random, 1 Lorenz, 2 Rössler; `cycles` = rate × time since the last step;
// returns the next value −1…1
struct ChaosState
{
    float x = 1.0f, y = 1.0f, z = 1.0f;   // the attractor's point
    float from = 0.0f, to = 0.0f, phase = 1.0f; // smooth random: the values it glides between, 0…1 between them
    std::uint32_t seed = 1u;
    void start (std::uint32_t s) noexcept;      // a voice's own start (no engine random drawn)
};
float chaosStep (int mode, ChaosState& state, double cycles) noexcept;
double shiftHz (double shift) noexcept;                                     // the frequency shifter's −1…1 → Hz
// Helix round 3 (round 190mt) laws, exposed for the tests and the editor
double spectralShiftHz (double shift) noexcept;   // the spectral oscillator's Shift −1…1 → Hz added to every partial
double spectralExponent (double stretch) noexcept; // Stretch −1…1 → the partials' exponent around the root (1: none)
int sampleLevelFor (double ratio) noexcept;        // the band-limited level a read at `ratio` source samples per sample takes
// Helix round 4 (round 190oc) laws: unison voice u of n's place −1 … 1 (0 for one voice), and the spectral oscillator's
// partials per unison voice (n voices share maxSpectralBank, at most maxSpectralPartials each)
double oscUnisonSpread (int u, int n) noexcept;
int spectralUnisonStride (int n) noexcept;
float grainWindow (float x) noexcept;              // the grains' window at 0…1
double warpPhase (int mode, double amount, double phase) noexcept;        // the phase warps (0…1 → 0…1)
float drawnLfo (const double* points, double x) noexcept;                  // a drawn LFO at phase x (points: count, x/y/curve…)
int unisonStackSemis (int stack, int voice) noexcept;                       // the stacked voice's offset in semitones
double delayBeats (int index) noexcept;                                     // the FX delay's synced time
std::array<double, 3> formantHz (double vowel) noexcept;                    // the formant filter's three bands at Morph 0…1 (A E I O U)
// the matrix target a parameter's knob modulates (drag and drop; −1: none)
int targetOfParam (int index) noexcept;

// The engine as the instrument drives it (audio thread, except the constructor, prepare and setState)
class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    // the whole state (stateSize values); builds the tables it names (call it from the UI thread / before play)
    void setState (const double* values);
    // the same without building tables (audio thread): a table not built yet keeps the oscillator's previous one
    void setValues (const double* values) noexcept;
    void setParam (int index, double value) noexcept; // one automatable value (audio thread)
    // Helix+ 2 (round 190ml, audio thread): oscillator k's user table and the sample oscillator's sample (nullptr: none);
    // the caller keeps them alive while the engine may play them
    void setUserTable (int osc, const Table* table) noexcept;
    void setSample (const SampleBuffer* sample) noexcept;
    // Helix round 3 (round 190mt, audio thread): the sample's band-limited levels (for Clean and the grains) and its partials
    // (the spectral oscillator); nullptr: none (the grains then read the sample itself; the spectral oscillator is silent)
    void setSampleLevels (const SampleLevels* levels) noexcept;
    void setSpectral (const SpectralData* spectral) noexcept;
    double param (int index) const noexcept;
    // velocity 0…1 (0 = note off), the note's own pitch offset in semitones, its slide (0…1, −1 = none) and pressure
    void note (int key, float velocity, float pitch = 0.0f, float slide = -1.0f, float pressure = 0.0f) noexcept;
    // MPE: a sounding note's own pitch (semitones), slide (0…1) or pressure (0…1) changed
    void notePitch (int key, float semitones) noexcept;
    // the held voices glide to `semitones` (a key, with its cents) over `samples` (FL's slide notes; rollup 0.1.30)
    void slideTo (float semitones, float samples) noexcept;
    void noteSlide (int key, float slide) noexcept;
    void notePressure (int key, float pressure) noexcept;
    void setPitchOffset (float semitones) noexcept;  // the channel's pitch, live
    void setPitchBend (float semitones, float range) noexcept; // the wheel in semitones and its range (the source's ±1)
    void setModWheel (float value) noexcept;         // 0…1
    void setPressure (float value) noexcept;         // channel pressure 0…1
    void allNotesOff() noexcept;
    void reset() noexcept;
    // adds n samples; songBeats ≥ 0 = the transport plays (synced LFOs follow the song)
    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept;
    bool idle() const noexcept;
    int activeVoices() const noexcept;
    void seed (std::uint32_t s) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One note for the reference renderer
struct RefNote
{
    int start = 0, key = 60;
    float velocity = 1.0f;
    int release = -1; // sample of the note-off (−1: held)
};
// Renders a state (stateSize values) with notes in blocks of `block` samples, at tempo `bpm`; interleaved stereo
std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 256, double bpm = 120.0, const HelixUserData* data = nullptr);

// the parameter's display text (the editor and automation) and English name ("Osc 1 Pos", "Filter 1 Freq"...)
std::string paramText (int index, double v);
std::string paramName (int index);
// a modulation source's / target's English name (a target's effect rows follow the oscillator's effect mode)
std::string sourceName (int source);
std::string targetName (int target, int effectMode1 = effectNone, int effectMode2 = effectNone);
} // namespace wt

std::unique_ptr<Instrument> makeHelixInstrument();
// a new channel's values (the engine's defaults)
std::vector<float> helixDefaults();
// the channel's state as doubles (missing values = defaults)
std::vector<double> helixState (const ChannelDef& channel);
// grows a channel's values to the whole state (missing ones as they read: a missing Helix+ block stays revision 0);
// `helixPlus`: a Helix+ value is about to be set, so the blocks go live (revision 4, round 190oc; 190mt: 3; 190ml: 2; 190mi: 1)
void padHelixState (ChannelDef& channel, bool helixPlus);
const std::vector<EngineParam>& helixEngineParams();
namespace modelparams
{
struct Table;
Table helixModelTable();
} // namespace modelparams
} // namespace lynx
