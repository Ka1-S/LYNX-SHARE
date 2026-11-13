#pragma once

// Pocket SuperSaw: LYNX's engine for FL Studio Mobile's "SuperSaw" module (FLM module type 23, 60 parameters, no
// desktop FL twin and no data file), round 190by. Clean-room: the module's laws were read from the FL-hosted FL
// Studio Mobile plugin and are restated here as formulas in our own words (docs/analise/REA_rodada190by.md); no
// Image-Line code, preset or sample is in this repository.
//
// A virtual-analog synthesizer: up to 8 voices, each a stack of 1…12 detuned band-limited saws (wave tables with
// exactly the harmonics below Nyquist, built at start-up) with a sub-octave saw and a pulse mode (the difference of
// two saws), or the "SuperSync" oscillator (a train of 1…6 pulses per period that sweeps like hard sync); a 4-pole
// ladder low-pass (the continuous ladder discretised exactly per sample, recomputed every 24 samples); three linear
// ADSR envelopes (level, filter, sync) on one time law; one LFO per voice (sine, ramp, square, random; free, one-shot
// or tempo-synced) with eight destinations; a mod-wheel router; a five-mode arpeggiator; a tanh distortion with an
// output trim and a tempo-synced stereo delay with a one-pole low / high-pass in its loop.
//
// Parameters: the channel keeps FLM's own 0…1 values in ChannelDef::modelParams, LYNX parameter k = FLM parameter
// k + 1 (FLM's parameter 0, Enable, is the channel's own mute), like the Pocket effects (core/PocketFx.h). So a
// .flm import copies the values as they are. The engine runs in single precision like FLM's.
//
// Differences from FLM, by design: one random generator per instance (FLM shares one between every SuperSaw of the
// process), so start phases, table dither and the random LFO differ from any given FLM render; the channel's pitch
// (knob, fine tune, main pitch) is added to every voice, live; the MIDI wheel bends by the module's own Pitch bend
// range; and LYNX's channel Mono / Porta and piano roll slide notes move the voices as they do FL's generators
// (core/HostSlide.h), on top of the module's own Monophonic mode and Slide time.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
namespace pocketsaw
{
// FLM's parameter numbers (index 0 = Enable). LYNX's parameter k is FLM's k + 1.
constexpr int flmParamCount = 60;
constexpr int paramCount = flmParamCount - 1;
namespace flm
{
constexpr int enable = 0, pulseWidth = 1, pulseMode = 2, subLevel = 3, unison = 4, detune = 5, unisonPan = 6, detuneCurve = 7,
              retrigPhases = 8, phasesOffset = 9, superSync = 10, sync = 11, syncPulseWidth = 12, syncAttack = 13, syncDecay = 14,
              syncSustain = 15, syncRelease = 16, syncEgAmount = 17, syncKbdDir = 18, cutoff = 19, resonance = 20, filterKbd = 21,
              filterAttack = 22, filterDecay = 23, filterSustain = 24, filterRelease = 25, filterEgAmount = 26, filterVel = 27,
              levelAttack = 28, levelDecay = 29, levelSustain = 30, levelRelease = 31, level = 32, pan = 33, slideHeld = 34,
              slideTime = 35, transpose = 36, bendRange = 37, slideStatic = 38, lfoShape = 39, lfoRate = 40, lfoMode = 41,
              lfoBeat = 42, unused43 = 43, lfoAmount = 44, lfoDest = 45, lfoFade = 46, arpRate = 47, arpRange = 48, arpMode = 49,
              mono = 50, voices = 51, distortion = 52, trim = 53, delayTime = 54, delayFeedback = 55, delayMix = 56,
              delayFilter = 57, mwAmount = 58, mwDest = 59;
}

// FLM's defaults, by FLM's parameter number (60 values, [0] = 1)
const std::array<float, flmParamCount>& defaults() noexcept;

// --- laws (each is the engine's own code path; exposed for the tests and the parameter texts)
// the envelope time law in seconds: T(v) = 2^(5·h⁷(v)) − 1 with h(x) = 2^x − 1 (0 … 31 s)
float envelopeSeconds (float v) noexcept;
// the step per tick of an envelope segment of T(v) seconds at `ticksPerSecond` ticks: 1 / (⌊T·ticks⌋ + 1)
float envelopeStep (float v, float ticksPerSecond) noexcept;
int unisonCount (float v) noexcept;                       // ⌊11v + 1⌋ in single precision, 1…12
// oscillator u of n: its frequency ratio for Detune d (0…1) and Detune Curve c (0…1)
float detuneRatio (int u, int n, float detuneKnob, float curveKnob) noexcept;
float voiceNormaliser (int n) noexcept;                   // 10^(−n/20): −1 dB per oscillator
double cutoffHz (float c, float egAmount, float env, float kbd, float note) noexcept; // min(2^(10.3c + 4 + …), 20000)
double lfoPeriodSeconds (float v) noexcept;               // 30(1 − v)³ + 1/30
double lfoBeatLength (float v) noexcept;                  // ⌊9v⌋ → 1/8 … 32 beats
double arpStepBeats (float v) noexcept;                   // ⌊5v⌋ → 1/8 … 1 beat
double delayBeats (float v) noexcept;                     // ⌊4v⌋ → 1/4 … 1 beat
int transposeSemitones (float v) noexcept;                // ⌊24v⌋ − 12
int bendRangeSemitones (float v) noexcept;                // ⌊11v + 1⌋
int voiceLimit (float v) noexcept;                        // ⌊7v + 1⌋, 1…8
// the wave table (2048 samples) an oscillator of frequency f reads at sample rate fs, and its number of harmonics
const float* waveTable (double f, double fs) noexcept;
int waveTableHarmonics (double f, double fs) noexcept;

// The engine as the instrument drives it (audio thread, except the constructor and prepare).
class Engine
{
public:
    Engine();
    ~Engine();
    // (allocates the delay lines, builds the shared wave tables on first use) the sample rate, a full reset
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    // FLM's parameter `index` (1…59; 0 = Enable), value 0…1
    void setParam (int index, float value) noexcept;
    float param (int index) const noexcept;
    // velocity 0…1 (0 = note off), pan 0…1 (½ = centre), pitch = the note's own offset in semitones
    void note (int key, float velocity, float pan = 0.5f, float pitch = 0.0f) noexcept;
    void setPitchOffset (float semitones) noexcept; // added to every voice (the channel's pitch)
    // a slide note of the host (LYNX's piano roll / channel Mono): the sounding voices glide to `key` + `cents` over
    // `samples`, keeping their keys; a note started inside HostSlide::pendingGlide() glides in (channel Porta)
    void slideTo (int key, float cents, float samples) noexcept;
    void setModWheel (float value) noexcept;        // 0…1
    void setPitchBend (float value) noexcept;       // −1…1 of the module's Pitch Bend Range
    void allNotesOff() noexcept;
    void reset() noexcept;
    // adds n samples; songBeats ≥ 0 = the transport plays (the position at the first sample)
    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept;
    bool idle() const noexcept; // no voice and no delay tail
    int activeVoices() const noexcept;
    void seed (std::uint32_t s) noexcept; // the instance's random generator (tests)

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// One note for the reference renderer
struct RefNote
{
    int start = 0, key = 60;
    float velocity = 1.0f, pan = 0.5f;
    int release = -1; // sample of the note-off (−1: held)
};
// Renders FLM's parameters (60 values, [0] = Enable) like the module: host blocks of `block` samples split at every
// event, tempo `bpm`, songPos ≥ 0 = the transport plays from that position (beats). Interleaved stereo (2·length).
std::vector<float> renderReference (const std::vector<float>& flmParams, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, double bpm = 120.0, double songPos = -1.0, std::uint32_t seed = 1);

// the text of LYNX parameter `param` (0…58) at value v, as FLM shows it where FLM shows a unit
std::string paramText (int param, float v);
} // namespace pocketsaw

std::unique_ptr<Instrument> makePocketSuperSawInstrument();
// the channel's 59 values from FLM's 60 (PRMS); empty when there are fewer than 60
std::vector<float> pocketSuperSawParams (const std::vector<float>& flmParams);
// a new channel's values (FLM's defaults)
std::vector<float> pocketSuperSawDefaults();
const std::vector<EngineParam>& pocketSuperSawEngineParams();
namespace modelparams
{
struct Table;
Table pocketSuperSawModelTable();
} // namespace modelparams
} // namespace lynx
