#pragma once

// Tetra: LYNX's own 4-operator FM synthesizer (round 190hs, owner decision O15). LYNX's own engine, name, look and
// code; Ableton Live's Operator sets import onto it (core/AlsDevicesFm.cpp) with Operator's own parameter values, so
// the parameters below use Operator's units and ranges as they are stored in a Live set (times in ms, levels linear,
// tuning as coarse ratio + fine per mille, filter in Hz). The laws that make those values sound like Operator were
// measured black-box on Live 12 renders (docs/analise/REA_rodada190hs.md); no Ableton code, preset or sample is used.
//
// A voice: four operators A…D (phase modulation: an operator's output is added to the phase of the operators it feeds)
// on one of eleven routings, each with its own wave (sine, quantised sines, additive saws / squares / triangle, noise,
// a 64-harmonic user wave), ratio or fixed frequency, level, self feedback, velocity / key scaling and an envelope with
// shaped segments and four loop modes; a pitch envelope; an LFO with its own envelope; a filter (low / high / band
// pass, notch, morph; 12 or 24 dB/oct; clean and driven circuits) with an envelope; a wave shaper; tone, spread,
// glide, voice count, pan and volume.

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

namespace fm
{
constexpr int opCount = 4;
constexpr int harmonics = 64; // the user wave's partials

// --- parameter numbers (the channel's modelParams[index]; Operator's units)
// an envelope: 14 values from its base
namespace env
{
constexpr int attack = 0, initial = 1, attackSlope = 2, decay = 3, peak = 4, decaySlope = 5, sustain = 6, release = 7,
              end = 8, releaseSlope = 9, loopMode = 10, loopTime = 11, repeat = 12, timeVel = 13, count = 14;
}
// an operator: from opBase(k)
namespace op
{
constexpr int on = 0, level = 1, wave = 2, coarse = 3, fine = 4, fixed = 5, fixedFreq = 6, fixedMulti = 7, feedback = 8,
              phase = 9, retrig = 10, velScale = 11, keyScale = 12, lfoOn = 13, pitchEnvOn = 14, envBase = 15,
              stride = envBase + env::count;
}
constexpr int opBase (int k) noexcept { return k * op::stride; }
constexpr int globalBase = opCount * op::stride;
namespace g
{
constexpr int algorithm = globalBase, volume = algorithm + 1, pan = algorithm + 2, transpose = algorithm + 3, tone = algorithm + 4,
              spread = algorithm + 5, voices = algorithm + 6, retrigger = algorithm + 7, glideOn = algorithm + 8,
              glideTime = algorithm + 9, envTime = algorithm + 10,
              // LFO
              lfoOn = algorithm + 11, lfoType = algorithm + 12, lfoRange = algorithm + 13, lfoRate = algorithm + 14,
              lfoSynced = algorithm + 15, lfoRetrig = algorithm + 16, lfoAmount = algorithm + 17, lfoAmountA = algorithm + 18,
              lfoFilter = algorithm + 19, lfoEnv = algorithm + 20,
              // pitch envelope
              penvOn = lfoEnv + env::count, penvAmountA = penvOn + 1, penvAmount = penvOn + 2, penv = penvOn + 3,
              // filter
              filterOn = penv + env::count, filterType = filterOn + 1, filterSlope = filterOn + 2, filterCircuit = filterOn + 3,
              filterCircuitBp = filterOn + 4, filterFreq = filterOn + 5, filterRes = filterOn + 6, filterMorph = filterOn + 7,
              filterDrive = filterOn + 8, filterVel = filterOn + 9, filterKey = filterOn + 10, filterEnvAmount = filterOn + 11,
              filterEnv = filterOn + 12,
              // shaper
              shaperType = filterEnv + env::count, shaperDrive = shaperType + 1, shaperMix = shaperType + 2;
}
constexpr int paramCount = g::shaperMix + 1;               // the automatable values
constexpr int harmonicsBase = paramCount;                  // then each operator's 64 user partials (0…1)
constexpr int stateSize = harmonicsBase + opCount * harmonics;

// waves (Operator's menu order, 190ei shot): 0 sine, 1 sine 4 bit, 2 sine 8 bit, 3…9 saw 3/4/6/8/16/32/64,
// 10 saw D, 11…17 square 3/4/6/8/16/32/64, 18 square D, 19 triangle, 20 noise looped, 21 white noise, 22 user
constexpr int waveCount = 23;
constexpr int waveUser = 22;
// filter types and circuits
constexpr int filterTypeCount = 5;     // low, high, band, notch, morph
constexpr int circuitCount = 5;        // clean, OSR, MS2, SMP, PRD (low / high pass); band / notch / morph: clean, OSR, MS2? (3)
constexpr int algorithmCount = 11;

// --- the routing of algorithm a: modulators of each operator (bit j = operator j feeds it) and the carriers
struct Routing
{
    std::array<std::uint8_t, opCount> from {}; // from[k]: the operators whose output modulates k
    std::uint8_t carriers = 0;                 // bit k: k is heard
};
const Routing& routing (int algorithm) noexcept;

// --- laws (each is the engine's own code path; exposed for the tests, the editor and the import)
double ratio (int coarse, double fine) noexcept;          // coarse 0 = ½, 1…48; fine ‰ (0…1000)
double fixedHz (double freq, int multi) noexcept;         // Operator's fixed frequency and its range switch
int voiceLimit (double index) noexcept;                   // the voice chooser's index → voices
double levelDb (double linear) noexcept;                  // −inf below Operator's floor
double lfoHz (int range, double rate) noexcept;           // the LFO's rate (low / high range); sync: lfoSyncBeats
double lfoSyncBeats (int index) noexcept;
double repeatBeats (int index) noexcept;                  // Beat / Sync loop modes' repeat time
double cutoffHz (double freq, double env, double keyOffset, double velocity, const double* p) noexcept;
// a shaped envelope segment: 0 → 1 over x = 0 → 1, slope −1…1 (0 straight)
float segment (float x, float slope) noexcept;

const std::array<double, stateSize>& defaults() noexcept;
// the wave tables (built once, any thread) and a single-cycle wave's value at phase 0…1 (tests, editor)
float waveValue (int wave, double phase) noexcept;
void buildUserWave (const float* partials, float* table) noexcept; // table: waveTableSize samples, peak-normalised
constexpr int waveTableSize = 2048;

// The engine as the instrument drives it (audio thread, except the constructor, prepare and setState)
class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    // the whole state (stateSize values); the user waves are rebuilt here (call it from the UI thread / before play)
    void setState (const double* values) noexcept;
    // the same without building the user waves (audio thread): they come ready from setUserTable
    void setValues (const double* values) noexcept;
    void setUserTable (int op, const float* table) noexcept; // waveTableSize + 1 samples
    void setParam (int index, double value) noexcept; // one automatable value (audio thread)
    double param (int index) const noexcept;
    // velocity 0…1 (0 = note off), the note's own pitch offset in semitones
    void note (int key, float velocity, float pitch = 0.0f) noexcept;
    // the held voices glide to `semitones` (key + the note's own offset) over `samples` (FL's slide notes, round 190ix)
    void slideTo (float semitones, float samples) noexcept;
    void setPitchOffset (float semitones) noexcept; // the channel's pitch, live
    void setPitchBend (float semitones) noexcept;   // the MIDI wheel in semitones
    void allNotesOff() noexcept;
    void reset() noexcept;
    // adds n samples; songBeats ≥ 0 = the transport plays (Sync loop mode, synced LFO)
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

// the parameter's display text (the editor and automation)
std::string paramText (int index, double v);
// the parameter's English name ("A Coarse", "Filter Freq"...)
std::string paramName (int index);
} // namespace fm

std::unique_ptr<Instrument> makeTetraInstrument();
// a new channel's values (the engine's defaults: one sine)
std::vector<float> tetraDefaults();
// the channel's state as doubles (missing values = defaults)
std::vector<double> tetraState (const ChannelDef& channel);
const std::vector<EngineParam>& tetraEngineParams();
namespace modelparams
{
struct Table;
Table tetraModelTable();
} // namespace modelparams
} // namespace lynx
