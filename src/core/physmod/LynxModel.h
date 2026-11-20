#pragma once

// LYNX Model (round 190ni, 0.1.31): LYNX's own physical-modelling instrument. First family: mallets on a shared modal
// core (owner decisions, design study 190nd physmod/DESIGN-physmod.md): marimba, vibraphone, xylophone, glockenspiel,
// tubular bells and kalimba, each a model type of one instrument (and a preset).
//
// A note is a bank of damped modes (core/physmod/ModalBank.h) struck by a synthesised mallet: a contact force
// t e^(-t / tau) (critically damped, -12 dB / octave above 1 / (2 pi tau), as a Hertz contact) whose time constant
// sets the brightness (hardness, velocity), plus a short band-pass noise burst for the strike. No
// recorded audio is played or shipped: each instrument is a parameter table (.lpm, a few KB; resources/physmod/) fitted
// offline from CC0 recordings (tools/physmod/calibrate.py; provenance per file in resources/physmod/*.provenance.json):
// per recorded key and velocity layer, each mode's frequency ratio, level and T60, its bending-mode number for the
// strike-position law, the contact width that reproduces the recorded attack, and the strike noise. A note between
// recorded keys / layers interpolates the tables: modes of the same family (round 190nl: the same physical mode in a
// layer's neighbouring anchors, however far its ratio drifts) blend as one mode; untagged modes pair by ratio within 4 %.
// Round 190nl: up to 48 modes per voice (the bells' and the kalimba's long, quiet upper partials), and the strike noise
// through two band-pass sections where the table asks for it (-12 dB / octave skirts; the recordings' strikes are not
// as broad as one section's -6 dB).
// Round 190nm (tubular bells and kalimba): a partial may be two modes - a two-stage decay (a tine's fast start and slow
// tail) or a beating pair (a tube's near-degenerate doublet), the second one possibly struck in antiphase (the mode's
// invert flag); a table may ask for the whole note's onset 1 - e^(-t / rise) (a bell's, a tine's slow start) instead of
// the resonated modes' build-up twins, and for an equal-power fade of the modes only one of two neighbouring keys has
// (the other tines' and the body's lines). A note keeps the strongest modes its voice can play (with build-up twins,
// 44; before, it lost its highest-frequency ones).
//
// The knobs act on that physics: Hardness scales the contact width (a harder mallet excites the high modes more and
// clicks more), Position moves the strike point along the bar (each mode weighted by its shape there: the free-free
// beam, or the clamped-free tine for the kalimba), Damping scales every T60, Resonator scales the resonated
// fundamental (marimba / vibraphone tubes: louder and shorter), Motor turns the vibraphone's rotating discs (an
// amplitude modulation of the resonated part, one shaft for all voices), Release damps the bars at note-off, Tone
// tilts the spectrum, Velocity sets the velocity response, Noise the strike noise.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"
#include "core/physmod/ModalBank.h"

namespace lynx
{
struct ChannelDef;

namespace lm
{
constexpr int modelCount = 6;
constexpr int maxVoices = 32;
enum Model : int { marimba = 0, vibraphone = 1, xylophone = 2, glockenspiel = 3, tubularBells = 4, kalimba = 5 };

// --- parameters (ChannelDef::modelParams[index], in these units)
namespace p
{
constexpr int model = 0,     // 0…5 (Model)
              hardness = 1,  // 0…1 (0.5 = the recordings' mallet)
              position = 2,  // 0…1 (the strike point: centre → end; kalimba: tip → middle; bells: cap → down)
              damping = 3,   // 0…1 (0.5 = as recorded; every T60 × 2^((0.5 − x)·4))
              resonator = 4, // 0…1 (0.5 = as recorded; the resonated fundamental ±12 dB)
              motor = 5,     // 0…10 Hz (0 = off; the vibraphone's discs)
              release = 6,   // 0…1 (note-off damping: 30 ms … 18 s; 1 = the bar rings on)
              tone = 7,      // −1…1 (spectral tilt, ±4.5 dB per octave)
              velocity = 8,  // 0…1 (velocity response: 0 = none, 0.5 = as recorded, 1 = steep)
              volume = 9,    // −40…+6 dB
              noise = 10,    // −40…+12 dB (the strike noise re the fitted level; −40 = off)
              count = 11;
}
constexpr int paramCount = p::count;
constexpr int stateSize = paramCount;

// --- the parameter tables (.lpm v1, tools/physmod/lpm.py)
struct TableMode
{
    float ratio = 1.0f, ampDb = 0.0f, t60 = 1.0f;
    std::uint8_t shape = 0;  // (the LPM byte's low 7 bits)
    bool invert = false;     // (round 190nm: the LPM shape byte's bit 7) the mode starts in antiphase: the second component
                             // of a doublet (a tube's or a tine's near-degenerate pair) struck so
    std::uint8_t family = 0; // (round 190nl) the same mode in a layer's neighbouring anchors; 0 = none (pair by ratio);
                             // absoluteFamily: a line at its own pitch (another tine ringing along), kept in Hz
};
constexpr std::uint8_t absoluteFamily = 255;
struct TableAnchor
{
    float key = 60.0f, velocity = 1.0f, gainDb = 0.0f, contact = 0.0005f; // contact: the force pulse's time constant (s)
    float noiseDb = -60.0f, noiseHz = 2000.0f, noiseQ = 1.0f, noiseMs = 10.0f;
    float riseMs = 0.0f; // the resonated modes' build-up (a tube re-radiating the fundamental), or with Table::onset the
                         // whole note's onset time constant (ms); 0 = none
    std::uint16_t first = 0, count = 0;
};
struct Table
{
    int model = 0, beam = 0; // beam 0: free-free bar / tube, 1: clamped-free tine
    int noiseOrder = 1;      // (round 190nl) strike-noise band-pass sections: 1 = -6 dB / octave skirts, 2 = -12 dB / octave
    int fade = 0;            // (round 190nm, LPM header byte 15) between two recorded keys, a mode only one of them has
                             // fades with 0: its amplitude (1 - t, t), 1: its power (equal power: the other tines' and the
                             // body's lines of two neighbours keep their summed energy in the middle of a gap)
    int onset = 0;           // (round 190nm, LPM header byte 14) 1: an anchor's riseMs is the whole note's onset - the voice's
                             // output rises as 1 - e^(-t / rise) (a bell's, a tine's slow start) - instead of the resonated
                             // modes' build-up twins; 0: the twins (tables before 190nm); 2 (round 190nn): the S-shaped onset
                             // (1 - e^(-t / rise))^2 - a pluck's or a strike's quiet first moment, then the rise
    float posRef = 0.25f, posU0 = 0.5f, posU1 = 0.95f, gainOffsetDb = 0.0f;
    std::array<std::uint8_t, 8> provenance {}; // the first bytes of the provenance record's SHA-256
    std::vector<TableAnchor> anchors;
    std::vector<TableMode> modes;
    std::size_t bytes = 0;
};
std::optional<Table> parseTable (const std::uint8_t* data, std::size_t size, std::string* error = nullptr);
// The tables compiled in (one per model; resources/physmod/<name>.lpm)
const Table& builtInTable (int model) noexcept;
std::size_t builtInTableBytes (int model) noexcept;   // the file's size
std::size_t tableMemoryBytes (const Table& table) noexcept; // parsed, in memory

// What a note plays: the table interpolated at its key and velocity, before the knobs
struct NoteModes
{
    int count = 0;
    std::array<float, maxModes> hz {}, amp {}, t60 {};
    std::array<std::uint8_t, maxModes> shape {};
    std::array<bool, maxModes> invert {}; // (round 190nm) the mode starts in antiphase
    float mainHz = 440.0f, gain = 1.0f, contact = 0.0005f, noiseAmp = 0.0f, noiseHz = 2000.0f, noiseQ = 1.0f, noiseMs = 10.0f;
    float riseMs = 0.0f;
};
NoteModes interpolate (const Table& table, float key, float velocity);

// The strike-position law: the shape of bending mode `shape` (1, 2, …) of the beam at u (0…1 along it)
float modeShape (int beam, int shape, float u) noexcept;

const char* modelName (int model) noexcept;   // "Marimba", "Vibraphone", …
const char* modelId (int model) noexcept;     // "marimba", …, the table's file name
const std::array<double, stateSize>& defaults() noexcept; // the marimba
std::array<double, stateSize> presetValues (int model) noexcept; // a model's own starting values
std::string paramText (int index, double v);
std::string paramName (int index);

// The engine as the instrument drives it (audio thread, except the constructor and prepare)
class Engine
{
public:
    Engine();
    ~Engine();
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    void setValues (const double* values) noexcept; // stateSize values
    void setParam (int index, double value) noexcept;
    double param (int index) const noexcept;
    // plays this table whatever the model (the calibration's held-out tables); nullptr = the built-in ones
    void setTable (const Table* table) noexcept;
    void note (int key, float velocity, float cents = 0.0f) noexcept; // velocity 0 = note off
    void setPitchOffset (float semitones) noexcept; // the channel's pitch and the wheel
    void allNotesOff() noexcept;
    void reset() noexcept;
    void render (float* left, float* right, int n) noexcept; // adds n samples
    bool idle() const noexcept;
    int activeVoices() const noexcept;
    int modesOfVoice (int voice) const noexcept; // (tests, perf)
    static std::size_t footprintBytes() noexcept; // the engine's own memory (its voices); the tables are shared

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

struct RefNote
{
    int start = 0, key = 60;
    float velocity = 0.8f;
    int release = -1;   // sample of the note-off (−1: held)
    float cents = 0.0f; // the note's own detune
};
// Renders a state with notes in blocks of `block` samples; interleaved stereo
std::vector<float> renderReference (const std::vector<double>& state, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 256, const Table* table = nullptr);
} // namespace lm

std::unique_ptr<Instrument> makeLynxModelInstrument();
std::vector<float> lynxModelDefaults();                 // a new channel's values (the marimba)
std::vector<float> lynxModelPreset (int model);         // a model's starting values
std::vector<double> lynxModelState (const ChannelDef& channel);
const std::vector<EngineParam>& lynxModelEngineParams();
namespace modelparams
{
struct Table;
Table lynxModelModelTable();
} // namespace modelparams
} // namespace lynx
