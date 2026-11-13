#pragma once

// Built-in effects for the mixer slots. Written from scratch, no dependencies.
// Buffers are allocated in the constructor already sized for 192 kHz, so prepare() and
// process() never allocate and can run on the audio thread.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct LiveInput;
struct HardwareOutput;

// Parameter description for the UI (all values are 0..1 in the model).
struct EffectParamInfo
{
    const char* name;
    float defaultValue;
};

struct EffectInfo
{
    const char* name;
    std::vector<EffectParamInfo> params;
};

const EffectInfo& effectInfo (EffectKind kind);

// Text for a parameter value, converted to its real unit (dB, ms, %...).
std::string formatEffectParam (EffectKind kind, int param, float value01, double bpm);
// The same with the slot's other parameters as context (the parametric EQ's FL modes show FL's own units)
std::string formatEffectParam (EffectKind kind, int param, float value01, double bpm, const EffectParams& context);

EffectSlot makeEffectSlot (EffectKind kind);
// The basic Filter's second stage at 24 dB/oct (round 190da, its appended Slope): the same type and cutoff, Q2 =
// 0.5 + x / (1 + x) for the first stage's Q = 0.5 + 2x / (1 − x) — the pair Ableton Live 12.4's Auto Filter runs
// (measured in round 190cz: both stages cookbook biquads, 0.003 dB); at Q 0.5 both are 0.5
double filterSecondStageQ (double q) noexcept;
// A slot loaded from a project that stored `savedCount` parameters: parameters added later that switch an engine
// to a newer construction get the value that keeps the old sound (round 147: Prism's measured Dynamic phase).
void upgradeLoadedEffectParams (EffectSlot& slot, std::size_t savedCount) noexcept;
// Round 190ee: the effects that listen to a MIDI input port (FL: the notes of the MIDI Out channels on that port):
// Gross Beat (its wrapper's port, kept in EffectSlot::plugin.midiInPort) and Pitcher (its own MIDI port parameter,
// FL's raw parameter 33). −1 = none; a Pitcher of an older project (Port routing off) listens to no port.
bool effectHasMidiPort (EffectKind kind) noexcept;
int effectMidiInPort (const EffectSlot& slot) noexcept;
void setEffectMidiInPort (EffectSlot& slot, int port) noexcept; // −1…255
// How many of a slot's parameters a project / preset saves (issue #67): the named ones plus any hidden ones after them
// that hold a value (the Parametric EQ's FL law, processing, band mask and FL's own values: ParametricEqFl.h [36…47]).
std::size_t savedEffectParamCount (const EffectSlot& slot) noexcept;
// A parameter value as written to a file: rounded to 4 decimals when that reads back as the same float, else the
// shortest decimal that does (FL's raw values, e.g. a frequency of raw/65536, survive a save and reload bit for bit)
double savedEffectParamValue (float v) noexcept;

// 7-band parametric EQ. Parameters: band b uses [4b .. 4b+3] = type, frequency, gain, Q;
// [28] = output; [29 + b] = cut slope (1 to 4 biquads: 12/24/36/48 dB per octave).
// For shelves the "Q" is the cookbook shelf slope S (1 = default).
// Slots imported from FL's Parametric EQ 2 / Parametric EQ run FL's exact engine instead and give some of these
// parameters FL's own meaning (core/ParametricEqFl.h: width instead of Q, FL's order instead of the cut slope).
namespace peq
{
constexpr int bands = 7;
constexpr int outputParam = 28;
constexpr int slopeParam = 29;

enum class BandType { off, peak, lowShelf, highShelf, lowCut, highCut, bandPass, notch };
constexpr int bandTypes = 8;

inline int param (int band, int field) noexcept { return band * 4 + field; } // field: 0 type, 1 freq, 2 gain, 3 Q
BandType type (float v) noexcept;
float typeValue (BandType t) noexcept;
double hz (float v) noexcept;      // 20 Hz .. 20 kHz (log)
float hzValue (double hz) noexcept;
double gainDb (float v) noexcept;  // ±18 dB
float gainValue (double db) noexcept;
double q (float v) noexcept;       // 0.1 .. 18 (log)
float qValue (double q) noexcept;
int slopeStages (float v) noexcept; // 1 to 4 biquads (12/24/36/48 dB/oct)
float slopeValue (int stages) noexcept;
const char* typeName (BandType t) noexcept;
bool flLaw (const EffectParams& params) noexcept;          // runs FL's Parametric EQ 2 / Parametric EQ engine
std::string orderText (const EffectParams& params, int band); // slope / order of a band, as the editor shows it
std::string widthText (const EffectParams& params, int band); // Q (LYNX) or bandwidth (FL)

// Response of the whole EQ (all bands and the output) in dB, for the editor graph.
double responseDb (const EffectParams& params, double sampleRate, double hz);
} // namespace peq

// Notes held on a Channel Rack channel: bit k of word k / 64 = MIDI key k
using NoteMask = std::array<std::uint64_t, 2>;

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void reset() noexcept = 0;
    // FL's Settings › Audio › "Reset plugins on transport" (REA_rodada167j §3, round 167k): playback started or jumped.
    // A built-in effect clears its state; a hosted plugin is reset only with its wrapper's "Reset plugin when FL Studio
    // resets" (on by default), at the next block.
    virtual void transportReset() noexcept { reset(); }

    // Processes in place. Parameters arrive every block (they may change at any time).
    virtual void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept = 0;
    // Automated plugin parameter (built-in effects receive their parameters in process()). 0..1.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // A hosted plugin's parameter value now (0..1; −1 = unknown), for a Patcher parameter output (round 171b). Audio thread.
    virtual float parameterValue (int /*index*/) const noexcept { return -1.0f; }
    // Control output ("controller" effects such as the Peak Controller), 0..1. Read every block.
    virtual float controlOutput (int /*output*/) const noexcept { return 0.0f; }
    // Song position at the start of the block, in ticks (for tempo-synced effects).
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    // Whether the song is playing during the next process() (effects that follow the host transport, like Newtone).
    virtual void setPlaying (bool /*playing*/) noexcept {}
    // Sidechain input for the next process(): another track's output, or nullptr if that track is
    // silent. Only called when the slot has a sidechain.
    virtual void setSidechain (const float* /*left*/, const float* /*right*/) noexcept {}
    // Whether the slot has a sidechain source, after every rack build (UI thread, never the audio thread): a hosted
    // plugin turns its side input bus on only then (issue #109).
    virtual void setSideInputActive (bool /*on*/) {}
    // Second side input (Vocodex carrier), same rules; only called when the slot has `sidechain2`.
    virtual void setSidechain2 (const float* /*left*/, const float* /*right*/) noexcept {}
    // A hosted effect's Processing › Connections (round 189j, core/Song.h SlotConnections), after every rack build
    // (UI thread, never the audio thread): which output pairs go into the insert, elsewhere (setExtraOutput's
    // buffers) or nowhere.
    virtual void setConnections (const SlotConnections& /*connections*/) {}
    // Input 1 taken from another insert for the next process() (its Connections): that insert's audio, nullptr =
    // silence; the slot's own input still reaches the effect as `left` / `right` (input 2 may take it). Not called =
    // input 1 is the slot's own input.
    virtual void setMainInput (const float* /*left*/, const float* /*right*/) noexcept {}
    // The notes held on every Channel Rack channel (`count` masks, by channel index) during the next process(); the
    // pointer stays valid until the next call. Effects that follow notes (LYNX Tune's MIDI targets) keep it.
    virtual void setHeldNotes (const NoteMask* /*channels*/, int /*count*/) noexcept {}
    // Round 190ee: a note reaching the effect through its MIDI input port (FL: a MIDI Out channel on the port the
    // effect listens to), before the process() that follows. `velocity` 1…127, 0 = note off; `midiChannel` 0…15.
    // Audio thread. Effects that take notes: Gross Beat (slot selection), Pitcher (MIDI mode / Harmonize).
    virtual void midiNote (int /*key*/, int /*velocity*/, int /*midiChannel*/) noexcept {}
    // Only the slot's state changed (an editor edited a control stored there, core/StateParams.h). Called off the audio
    // thread on the running effect: true = it takes the new state itself, applied at the start of a later process(),
    // so its delay lines and filters keep running; false (the default) = the slot's effect is built again.
    virtual bool acceptState (const std::vector<std::uint8_t>& /*state*/) noexcept { return false; }
    // Effects that send audio to another insert (FL's Fruity Send): the buffers the next process() adds its send
    // into (cleared by the renderer), or nullptr = nothing is sent this block.
    virtual void setSendOutput (float* /*left*/, float* /*right*/) noexcept {}
    // The same for an effect with several sent outputs (round 167b, the Transmitter's transient / sustain sends):
    // output 0 is setSendOutput's.
    virtual void setExtraOutput (int output, float* left, float* right) noexcept
    {
        if (output == 0)
            setSendOutput (left, right);
    }
    // Latency the effect introduces (plugins), in samples. Used by PDC.
    virtual int latencySamples() const noexcept { return 0; }
    // The hardware insert (round 164d): the device's input of the current callback piece and the device-output buses,
    // with where the next process() starts within that piece. `out` is null when no device plays live (an export).
    virtual void setHardwareIo (const LiveInput* /*in*/, HardwareOutput* /*out*/, int /*offset*/) noexcept {}
};

// Auto-pan's rhythmic-panner sources (round 135, the PanMan role; appended after FL's Love Philter LFO parameters 0–6,
// which stay the default source): the source, a free rate in Hz, the direction, smoothing, rhythm steps on a note
// grid, input / output gains and an analog input stage.
namespace autopan
{
enum Param : int { source = 7, rate, direction, smoothing, stepsParam, stepNote, inputGain, outputGain, analogStage, count };
enum class Source { fl, lfo, steps };
enum class Direction { both, right, left }; // back and forth, left → right, right → left
Source sourceOf (float v) noexcept;
Direction directionOf (float v) noexcept;
double rateHz (float v) noexcept;   // 0.01 … 10 Hz (0.01 · 1000^v, the reference's law)
float rateValue (double hz) noexcept;
int stepCount (float v) noexcept;   // 2 … 8
double stepBeats (float v) noexcept; // 1/1 … 1/32 note, in beats (4 … 0.125)
double inputDb (float v) noexcept;  // ±24 dB (input and output)
std::string paramText (int param, float v);
} // namespace autopan

std::unique_ptr<Effect> makeEffect (EffectKind kind);
// The effect of a slot: like makeEffect (kind), but also builds the effects that need more than the kind
// (`parallel` containers, the Vocodex from its raw state).
std::unique_ptr<Effect> makeEffect (const EffectSlot& slot);

// FL's mode-0 pan law as a 2×2 matrix (outL = ll·inL + rl·inR, outR = lr·inL + rr·inR), unit volume (REA_rodada27 §3.1)
void flPanMatrix (double pan, float& ll, float& rl, float& lr, float& rr) noexcept;
// Fruity Squeeze: maximum of each raw control (the effect's parameters are raw / max)
extern const std::array<float, 12> squeezeMax;
// `parallel` container holding the effects of the slot's branches.
std::unique_ptr<Effect> makeParallelEffect (const EffectSlot& slot);
} // namespace lynx
