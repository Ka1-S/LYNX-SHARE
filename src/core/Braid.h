#pragma once

// Braid: LYNX's own two-engine macro synthesizer (round 190ko). LYNX's own engine, name, code and sounds; Ableton
// Live's Meld imports onto it (core/AlsDevicesMeld.cpp) with its own values, so the parameters below keep Meld's units
// and ranges as a Live set stores them (times in seconds, volumes in dB, filter frequency in Hz, macros 0…1, the
// modulation matrix's amounts −1…1). The laws that make those values sound like Live were measured black-box on Live
// 12.4 renders (docs/analise/REA_rodada190ko.md); no Ableton code, preset or sample is used.
//
// A voice holds two engines, A and B. Each engine: a macro oscillator (25 algorithms, each with two macros: shapes,
// swarms, FM, noises, textures, chords), a filter (17 types, each with two macros), an amp envelope and a modulation
// envelope (loop modes), LFO 1 (six generators with two macros, then two transformers) and LFO 2 (six waves), a tone
// filter, pan and volume, glide. The two engines are mixed, driven and limited per voice. A modulation matrix (19
// sources × 50 targets: each engine's envelopes and LFOs, velocity, key, random, pitch bend, pressure, mod wheel, MPE
// note pitch / slide / pressure, voice spread) moves the knobs per voice. Mono / poly (2 … 12 voices), stacked voices
// with spread. Band-limited where it matters (polyBLEP shapes, oversampled-free FM with modest indices). No allocation
// on the audio thread.

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

namespace braid
{
// --- parameter numbers (the channel's modelParams[index]; Meld's units). Per engine (A = 0, B = 1):
namespace eng
{
enum : int
{
    on,
    oscType, oscMacro1, oscMacro2, transpose, octaves, degrees, detune, keytrack, oscReset,
    filterOn, filterType, filterFreq, filterMacro1, filterMacro2,
    lfo1Gen, lfo1Sync, lfo1Retrig, lfo1Rate, lfo1Synced, lfo1Phase, lfo1Macro1, lfo1Macro2, lfo1Fx1, lfo1Fx1Macro, lfo1Fx2, lfo1Fx2Macro,
    lfo2Wave, lfo2Sync, lfo2Rate, lfo2Synced, lfo2Retrig, lfo2Phase,
    ampAttack, ampDecay, ampRelease, ampSlopeA, ampSlopeD, ampSlopeR, ampSustain, ampLoop,
    modAttack, modDecay, modRelease, modInitial, modPeak, modSustain, modFinal, modSlopeA, modSlopeD, modSlopeR, modLoop,
    tone, pan, volume, glideMode, glideTime,
    stride
};
}
constexpr int engineBase (int k) noexcept { return k * eng::stride; }
constexpr int bDelay = 2 * eng::stride, drive = bDelay + 1, limiter = bDelay + 2, linkAmp = bDelay + 3, spread = bDelay + 4,
              volume = bDelay + 5, monoPoly = bDelay + 6, legato = bDelay + 7, polyVoices = bDelay + 8, stacked = bDelay + 9;
constexpr int paramCount = stacked + 1; // the automatable values
// then the modulation matrix [target][source] (not automatable)
constexpr int sourceCount = 19, targetCount = 50;
constexpr int matrixBase = paramCount;
constexpr int matrixIndex (int target, int source) noexcept { return matrixBase + target * sourceCount + source; }
constexpr int stateSize = matrixBase + targetCount * sourceCount;

// modulation targets (Live's rows, in the set's order): 25 per engine, B = A + 25
namespace tgt
{
enum : int
{
    detune, pitch, pitchQuant, oscMacro1, oscMacro2, filterFreq, filterMacro1, filterMacro2, lfo1Rate, lfo1Macro1, lfo1Macro2,
    lfo1Fx1, lfo1Fx2, lfo2Rate, ampAttack, ampDecay, ampRelease, ampSustain, modAttack, modDecay, modRelease, modSustain, tone, pan,
    volume, perEngine
};
}
// modulation sources (Live's matrix columns in the set's order: each engine's A Env, M Env, LFO 1, LFO 1 FX, LFO 2, then Vel,
// Pitch, PB, Note PB, Press, Mod W, Rand, Slide, Spread [SHOT 190ko mx1…mx3: the Matrix / MIDI / MPE tabs; MEASURED m1 q00…q18])
namespace src
{
enum : int
{
    aAmpEnv, aModEnv, aLfo1, aLfo1Fx, aLfo2, bAmpEnv, bModEnv, bLfo1, bLfo1Fx, bLfo2, velocity, key, pitchBend, notePitchBend, pressure,
    modWheel, random, slide, spread
};
}

constexpr int oscTypeCount = 25, filterTypeCount = 17, lfoGenCount = 6, lfoFxCount = 19, lfo2WaveCount = 6;
const char* oscTypeName (int t) noexcept;
const char* oscMacroName (int t, int macro) noexcept;
const char* filterTypeName (int t) noexcept;
const char* filterMacroName (int t, int macro) noexcept;
const char* lfoGenName (int t) noexcept;
const char* lfoFxName (int t) noexcept;
const char* lfo2WaveName (int t) noexcept;
int polyVoiceCount (int index) noexcept; // the voice chooser's index → voices
double lfoSyncBeats (int index) noexcept;

const std::array<double, stateSize>& defaults() noexcept;

// The engine as the instrument drives it (audio thread, except the constructor, prepare and setState)
class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    void setValues (const double* values) noexcept; // the whole state (stateSize values)
    void setParam (int index, double value) noexcept;
    double param (int index) const noexcept;
    // velocity 0…1 (0 = note off), the note's own pitch offset (semitones), slide (0…1, −1 = none), pressure (0…1)
    void note (int key, float velocity, float pitch = 0.0f, float slide = -1.0f, float pressure = 0.0f) noexcept;
    void notePitch (int key, float semitones) noexcept;
    void noteSlide (int key, float slide) noexcept;
    void notePressure (int key, float pressure) noexcept;
    void slideTo (float semitones, float samples) noexcept;
    void setPitchOffset (float semitones) noexcept;
    void setPitchBend (float semitones, float range) noexcept;
    void setModWheel (float value) noexcept;
    void setPressure (float value) noexcept;
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

struct RefNote
{
    int start = 0, key = 60;
    float velocity = 1.0f;
    int release = -1; // sample of the note-off (−1: held)
};
// Renders a state with notes in blocks of `block` samples at `bpm`; interleaved stereo (tests, the CPU bench)
std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    int block = 256, double bpm = 120.0);

std::string paramText (int index, double v);
std::string paramName (int index); // "A Osc Macro 1", "B Filter Freq", "Drive"…
std::string sourceName (int source);
std::string targetName (int target);
// Live's element name of an automatable value ("MeldVoice_EngineA_Oscillator_Macro1", "Volume"…)
std::string liveName (int index);
} // namespace braid

std::unique_ptr<Instrument> makeBraidInstrument();
std::vector<float> braidDefaults();
std::vector<double> braidState (const ChannelDef& channel);
const std::vector<EngineParam>& braidEngineParams();
namespace modelparams
{
struct Table;
Table braidModelTable();
} // namespace modelparams
} // namespace lynx
