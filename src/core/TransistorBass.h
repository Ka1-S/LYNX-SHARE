#pragma once

// FL Studio's Transistor Bass rebuilt from its engine (docs/analise/REA_rodada57.md; clean-room: behaviour, layouts and
// formulas in our own words, checked against renders of the installation's own engine DLL). A TB-303-style monophonic
// bass: band-limited saw/square oscillator (or the "303 Pulse" recorded oscillator cycles), 4-pole diode-ladder low-pass,
// accent/decay envelopes, VCA and high-pass at 4× the host rate with two half-band decimators, the Rat-style distortion;
// then the stereo delay and the plate reverb; the 128 programs (patch + 16-step sequence + transpose) with the step
// sequencer driven by host notes and tempo. The three engines of the plugin's Edit menu: "V1 legacy" (state version < 5
// or engine 0) and "V2" with the extended or the TB303 cutoff range (engines 1 and 2).
//
// Installation data read at run time from the user's own engine DLL (never shipped): the recorded oscillator cycles of
// the "303 Pulse" mode (57 for V1, 55 for V2). The DLL must be the analysed build (SHA-256 checked); otherwise the
// importer keeps the channel unsupported.
//
// Host interface as FL's wrapper: MIDI velocity from Init.Vol (127·ln(10·Init.Vol + 1)/ln 21), velocity ≥ 110 is an
// accent, overlapping notes slide; per-note pan (√(1 − p), √p), pitch (cents) and X/Y (Final.FCut adds to the cutoff
// control, 128/12 octaves per unit; Final.FRes adds to the resonance in V1); Final.Vol is not read. Differences from FL / not
// modelled: the library's pow/powf/exp results are not bit-identical to the DLL's (single-ulp differences, −108 dB worst
// case on the factory programs, median −131 dB); parameter automation (the plugin's 16-sample smoother), MIDI CC7
// volume and pitch-bend events, the "MIDI channel 13" sequence transpose (no note channel in LYNX); the order of the
// host's sample-rate call and the state load is FL's own (the reference follows the EXEC harness: state, then rate).

#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/Instruments.h"

namespace lynx
{
// true for a Transistor Bass state the engine can read (wrapper version 1–2, engine stream version 1…5)
bool transistorBassStateUsable (const std::vector<std::uint8_t>& state);

// true when `path` is the analysed build of Transistor Bass's engine_x64.dll
bool transistorBassDllUsable (const std::string& path);

std::unique_ptr<Instrument> makeTransistorBassInstrument();

// FL Studio Mobile's "Transistor Bass" module is the "V1 legacy" engine at 1× oversampling inside its own note layer
// (REA spec of round 190n: monophonic stack of 16 keys, a note over a held one slides, releasing the top key slides
// back to the one below, velocity ≥ 99/127 is an accent and has no level effect, balance pan, no sequencer, delay or
// reverb, and none of the installation's data). A channel imported from a .flm asks for it with modelParams = { 1 }
// (saved with the project).
bool transistorBassIsFlm (const ChannelDef& channel) noexcept;
// A state as the desktop plugin saves it (engine stream version 5, sequencer off, every program the given patch of
// parameters 0…41), with the oversampling field (1, 2 or 4) and the engine (0 = "V1 legacy").
std::vector<std::uint8_t> transistorBassWriteState (const std::array<float, 42>& patch, int oversampling = 4, int engine = 0);
// The state for FLM's module from its 14 parameters (PRMS, 0 = Enable): the equivalent desktop patch, oversampling 1.
// Empty when there are fewer than 14 values.
std::vector<std::uint8_t> transistorBassFlmState (const std::vector<float>& flmParams);
// the oversampling field of a usable state (0 otherwise)
int transistorBassOversampling (const std::vector<std::uint8_t>& state);

// The designed page (round 168q): parameter `index` (0…41) of the edit program's working patch — its byte offset
// in a usable state (−1 otherwise) — and the program's sequence for the sequence display.
int transistorBassParamOffset (const std::vector<std::uint8_t>& state, int index);
struct TransistorBassProgram
{
    struct Step
    {
        int note = 0;
        bool gate = false, slide = false, accent = false;
    };
    int program = 0, transpose = 0, length = 16;
    bool sequencer = false;
    std::array<Step, 32> steps {};
};
std::optional<TransistorBassProgram> transistorBassProgram (const std::vector<std::uint8_t>& state);
// FL's editor shows and edits its "edit program"; with the sequencer on and the state's follow flag set, a note makes
// both the playing and the edit program the note's key (engine_x64 FUN_18000d5a0, REA_rodada169g2): the state's
// program and edit program become `program` (0…127), in place; false when the state is not usable (round 169g2)
bool transistorBassSelectProgram (std::vector<std::uint8_t>& state, int program);
// The page's sequence rows (round 168q2, FL's 16-step editor): parameters after the patch's 0…41 — the saved
// program's sequence length (1…16), the sequencer switch, then per step i (0…15) firstStep + 5i + {0 note 0…131,
// 1 octave 0…10 (the note's octave: note / 12), 2 gate, 3 slide, 4 accent}.
namespace tbseq
{
constexpr int length = 100, sequencer = 101, firstStep = 110, steps = 16;
}
// Clear (gates, slides and accents off, notes C5) or move every step an octave (±1) — FL's CLEAR and octave buttons;
// false when the state can't be read
bool transistorBassClearSequence (std::vector<std::uint8_t>& state);
bool transistorBassShiftSequence (std::vector<std::uint8_t>& state, int octaves);
const std::vector<EngineParam>& transistorBassParams();
namespace modelparams
{
struct Table;
Table transistorBassModelTable();
} // namespace modelparams

namespace tbass
{
// One note for the reference renderer (drives the engine like the EXEC harness of REA_rodada57).
struct RefNote
{
    int start = 0, key = 60;
    double initVol = 1.0;  // Init.Vol (FL: (11^(1.28·velocity) − 1)/10)
    double finalPan = 0.0; // −1…1
    double pitchCents = 0.0;
    double fcut = 0.0, fres = 0.0; // Final.FCut / Final.FRes (the X/Y expressions)
    int release = -1;      // sample of the note-off (−1: held)
    int channel = 0;       // host "colour" answer (MIDI channel; 12 = sequence transpose)
};

struct RefOptions
{
    double tempo = 0.0; // ProcessEvent 0 (≤ 0: never sent)
    int block = 512;    // host block (split at every event)
    bool flm = false;   // FL Studio Mobile's module: RefNote::initVol is the note's velocity 0…1, no DLL needed
    int flmPreroll = 0; // flm: samples the engine runs before the first note
    int flmTail = -1;   // flm: samples the engine runs after the last note-off (−1: a quarter second)
    int flmPanBlock = 0; // flm: samples per process call of FLM for the note-pan smoothing (0: 512)
};

// Renders `notes` from `state` with the recorded cycles of the DLL at `dll`. Interleaved stereo (2·length); empty if
// unusable.
std::vector<float> renderReference (const std::vector<std::uint8_t>& state, const std::string& dll, const std::vector<RefNote>& notes,
                                    double sampleRate, int length, const RefOptions& options = {});
} // namespace tbass
} // namespace lynx
