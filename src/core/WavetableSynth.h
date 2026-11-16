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
constexpr int stateSize = matrixBase + targetCount * sourceCount;

// modulation sources (Live's matrix columns, in the set's order [DATA 190ja: 271 Core Library presets])
namespace src
{
constexpr int amp = 0, env2 = 1, env3 = 2, lfo1 = 3, lfo2 = 4, velocity = 5, note = 6, pitchBend = 7, pressure = 8, modWheel = 9,
              random = 10, notePitchBend = 11, slide = 12;
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
}

constexpr int effectNone = 0, effectFm = 1, effectClassic = 2, effectModern = 3;
constexpr int routeSerial = 0, routeParallel = 1, routeSplit = 2;
constexpr int unisonNone = 0, unisonClassic = 1, unisonShimmer = 2, unisonNoise = 3, unisonPhaseSync = 4, unisonPositionSpread = 5,
              unisonRandomNote = 6;
constexpr int maxUnison = 8;

// --- wave tables (core/WavetableTables.cpp): LYNX's own, generated from recipes on first use, then kept
constexpr int frameCount = 64;                 // frames per table (the position moves through them)
constexpr int mipCount = 19;                   // half an octave apart: highest partial 1023, 723, 511 … 1
int mipLength (int mip) noexcept;              // samples per frame at mip m (4096 … 512; a guard sample follows)
int mipPartials (int mip) noexcept;            // the highest partial kept at mip m
struct Table
{
    std::string category, name;
    // [mip][frame][mipLength (mip) + 1]
    std::array<std::vector<float>, mipCount> data;
    const float* frame (int mip, int f) const noexcept { return data[(std::size_t) mip].data() + (std::size_t) f * (std::size_t) (mipLength (mip) + 1); }
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
                                    int length, int block = 256, double bpm = 120.0);

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
const std::vector<EngineParam>& helixEngineParams();
namespace modelparams
{
struct Table;
Table helixModelTable();
} // namespace modelparams
} // namespace lynx
