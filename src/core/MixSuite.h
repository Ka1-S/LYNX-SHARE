#pragma once

// LYNX's own mixing suite (REA_rodada96): five native effects built only from published DSP knowledge
// (textbooks, papers, AES / DAFx literature and public circuit analyses of classic studio hardware).
// None of them is a model of a particular product; they fill the roles a vocal chain needs:
//
//   Ridge Strip     channel strip: HPF / LPF, 4-band EQ (two curve families), compressor, gate / expander,
//                   processing order, filters to the sidechain, metering
//   Glow Leveler    optical leveler: program-dependent attack / two-stage release of an electro-optical cell,
//                   peak reduction, gain, compress / limit
//   Hush            de-esser: split-band or wideband, listen, frequency / threshold / range, lookahead
//   Aether          air band (high shelf) + harmonic exciter, oversampled
//   Ember           analog-style saturator: tube / tape / transistor / fuzz, tone, oversampled (2x / 4x / 8x), mix
//
// All parameters are 0..1 in the model (EffectParams); the mappings to real units are below. Nothing allocates
// after construction, and every effect reports a latency that does not depend on its parameters, known right
// after prepare() (REA round 87: PDC is planned before the first block).

#include <algorithm>
#include <array>
#include <utility>
#include <memory>
#include <string>

#include "core/Song.h"

namespace lynx
{
struct EffectInfo;
class Effect;

namespace mixsuite
{
// ---- shared helpers (exposed for the tests and the editor graphs) ----

// Multi-stage half-band FIR oversampler for one channel (2x, 4x or 8x). The latency in base-rate samples is the
// same for every factor (the shorter paths are padded), so switching the quality never moves the PDC.
class Oversampler
{
public:
    static constexpr int maxStages = 3;
    Oversampler();
    void setFactor (int factor) noexcept; // 2, 4 or 8
    int factor() const noexcept { return 1 << stages; }
    void reset() noexcept;
    static int latency() noexcept;        // base-rate samples (the same for every factor)

    // Runs `f` (float → float) at the oversampled rate on each sample of `x` (in place).
    template <typename F>
    void process (float* x, int n, F&& f) noexcept
    {
        for (int i = 0; i < n; ++i)
            x[i] = padOut (run<0> (x[i], f));
    }

private:
    struct Stage
    {
        int taps = 0;                       // non-zero side taps of the half-band (centre excluded)
        std::array<float, 64> coef {};      // h[2j], j = 0 .. taps − 1
        int centreDelay = 0;                // (c − 1) / 2 and (c + 1) / 2 in input samples
        std::array<float, 128> upHist {}, evenHist {}, oddHist {};
        int upPos = 0, downPos = 0;
    };

    template <int level, typename F>
    float run (float x, F& f) noexcept
    {
        if constexpr (level == maxStages)
            return f (padIn (x));
        else
        {
            if (level == stages)
                return f (padIn (x));
            float a, b;
            up (st[(std::size_t) level], x, a, b);
            const auto ya = run<level + 1> (a, f);
            const auto yb = run<level + 1> (b, f);
            return down (st[(std::size_t) level], ya, yb);
        }
    }

    static void up (Stage& s, float x, float& a, float& b) noexcept;
    static float down (Stage& s, float a, float b) noexcept;
    float padIn (float x) noexcept;  // a short delay at the highest rate (makes the latency whole)
    float padOut (float x) noexcept; // base-rate delay up to the common latency

    std::array<Stage, maxStages> st;
    int stages = 2;
    std::array<float, 8> hiPad {};
    int hiPadLen = 0, hiPadPos = 0;
    std::array<float, 64> loPad {};
    int loPadLen = 0, loPadPos = 0;
};

// ---- Ridge Strip (channel strip) ----
namespace ridge
{
enum Param
{
    input, hpf, lpf,
    lfGain, lfFreq, lfBell, lmfGain, lmfFreq, lmfQ, hmfGain, hmfFreq, hmfQ, hfGain, hfFreq, hfBell,
    curve, eqOn,
    compRatio, compThreshold, compRelease, compFastAttack,
    gateRange, gateThreshold, gateRelease, gateFastAttack, expander, dynOn,
    order, filtersToSidechain, output, phase, compMix,
    // round 123: "E" voicing (0 = Ridge's own engine, 1 = the measured E-type console channel, core/VoiceMatch.h) and
    // that console's filter split (filters before the dynamics); appended, so older projects load Ridge's own sound
    voicing, filterSplit,
    // round 145 (appended): the "Vocal strip" voicing (core/VoxStrip.h), measured on a reference vocal channel strip
    // (de-esser, de-ploser, 3-band EQ, compander, 24 orders); off = the two engines above
    vocal, vInput, vOutput, vOrder,
    vDsOn, vDsFreq, vDsThreshold, vDsSplit, vDsLookahead, vDsListen,
    vDpOn, vDpFreq, vDpThreshold, vDpSplit, vDpLookahead, vDpListen,
    vEqOn, vHpfOn, vHpfFreq, vNotchOn, vNotchFreq, vNotchGain, vHighOn, vHighFreq, vHighGain,
    vCompOn, vExpThreshold, vCompThreshold, vRatio, vAttack, vRelease, vMakeup, vKnee, vDrive,
    // round 147b (appended, off by default = the old sound): Ridge's own engine gets the reference console's automatic
    // make-up (the level its compressor gives back for the threshold and ratio, measured in round 123 — the E voicing
    // always has it) and the ANALOG switch (the console emulation's colour)
    autoMakeup, analog,
    // round 148 (appended, off by default = the old sound): the "4K" voicing — the console measured on the SSL 4K E trial
    // (core/VoiceMatch.h, echannel model 1: its EQ colours, filters, compressor, gate, mic pre and VCA). It takes the E
    // voicing's controls (and Auto make-up, Analog = the Analogue VCA, Comp. mix) plus these
    console4k, k4Colour, k4Pre, k4Mic, k4MakeupOffset,
    // round 165 (appended, off = the old sound): the reference console's "Dyn S.C." — the filters and the EQ feed only the
    // dynamics' side chain (de-essing / frequency-conscious compression); the audio keeps neither (the filters stay at the
    // input when split). Ridge's own engine and the E voicing
    eqToSidechain,
    // round tp172c (appended, off = the old sound): the "Modern" voicing — the modern console channel strip measured on
    // the SSL Native Channel Strip 2 trial (core/VoiceMatch.h, strip2): its E / G EQ types (the EQ curve: Brown = E,
    // Black = G), filters, feed-forward compressor (its peak / RMS detector) and gate (with its hold); the shared
    // controls read the reference's own ranges
    modern, compPeak, gateHold,
    // round tp172c6 (appended, off = the old sound): the DRIVE section — a measured saturation stage at the strip's end
    // (before the output trim in Ridge's own engine, after the voicings' own output): a static curve per model and
    // drive position (fitted on the trials' stairs, 4x oversampled), its tone, an input gain into it, a trim and a mix
    driveOn, driveModel, driveAmount, driveInput, driveTrim, driveMix,
    count,
    // round tp172c10, hidden (not in the info, saved only while non-zero): the compressor's largest gain reduction
    // (0 = none, else 60 dB x the value), set by the Harrison 32Classic translation (that console caps it at 27.5 dB;
    // the original has no control for it)
    compGrCap = count,
    // round tp172c11, hidden the same way: the compressor's attack time constant (0 = the Fast switch's 1 / 10 ms, else
    // 0.25 s x the value), set by the Harrison 32Classic translation (its dial is a real time constant, 2.5 ms in Limiter
    // mode; Ridge has only the switch)
    compAttackFine
};
double compGrCapDb (float v) noexcept;   // 0 = no cap (a huge value), else 0 .. 60 dB
double compAttackFineSeconds (float v) noexcept; // 0 .. 0.25 s
// round tp172c6: the drive models (DRIVE section), in the Model selector's order
enum DriveModel { driveSaturate, driveConsoleE, driveConsoleG, driveConsoleA, driveConsoleN, driveConsoleY, driveTube,
                  driveGuitarAmp, driveBassAmp, driveModels };
int driveModelIndex (float v) noexcept;
float driveModelValue (int model) noexcept;
const char* driveModelName (int model) noexcept;  // English (the selector's options are translated in the layout)
double driveAmountUnits (int model, float v) noexcept; // the model's own dial: dB (Saturate -24 .. +24, consoles / tube 0 .. 18) or 0 .. 11 (amps)
float driveAmountValue (int model, double units) noexcept;
double driveInputDb (float v) noexcept;  // -24 .. +24 dB
double driveTrimDb (float v) noexcept;   // -24 .. +24 dB
// the drive stage's static curve (no tone, no oversampling) at the current settings: output for one input sample
double driveCurve (const EffectParams& p, double x) noexcept;
std::unique_ptr<Effect> makeKiln(); // round tp172c7: Kiln Channel (its console models are the DRIVE section's)
// round 147b: the auto make-up (dB) for Ridge's own compressor: the console's for the same threshold (dBFS + 18 = the
// console's dBu scale) and ratio; 0 with the compressor off
double autoMakeupDb (const EffectParams& p) noexcept;

double hpfHz (float v) noexcept;      // 0 = off, else 16 .. 350 Hz (18 dB/oct)
double lpfHz (float v) noexcept;      // 0 = off, else 22 kHz .. 3 kHz (12 dB/oct)
double bandGainDb (float v, bool black) noexcept; // ±15 dB (brown) / ±18 dB (black)
double lfHz (float v) noexcept;       // 30 .. 450 Hz
double lmfHz (float v) noexcept;      // 200 Hz .. 2.5 kHz
double hmfHz (float v) noexcept;      // 600 Hz .. 7 kHz
double hfHz (float v) noexcept;       // 1.5 .. 16 kHz
double bandQ (float v) noexcept;      // 0.5 .. 2.5
double ratio (float v) noexcept;      // 1:1 .. 20:1, ∞ at the top (returned as 1e9)
double compThresholdDb (float v) noexcept; // −40 .. 0 dBFS
double releaseSeconds (float v) noexcept;  // 0.1 .. 4 s
double gateRangeDb (float v) noexcept;     // 0 (off) .. 60 dB
double gateThresholdDb (float v) noexcept; // −80 .. 0 dBFS
double trimDb (float v) noexcept;          // ±20 dB (input / output)

// The EQ section's magnitude (filters + 4 bands) in dB at `hz`, as processed at `sampleRate`.
double responseDb (const EffectParams& p, double sampleRate, double hz);
// Static curve of the compressor: output level for a steady input level (dB), no gate.
double compressorCurveDb (const EffectParams& p, double inputDb);
// E voicing: the console's own scales for the controls whose range differs from Ridge's (dial position 0..1)
double eQ (float v) noexcept;              // mid-band Q 0.1 .. 3.5 (the console's dial, reversed so that up = narrower)
double eCompThresholdDb (float v) noexcept; // -20 .. +10 dB (console scale)
double eGateRangeDb (float v) noexcept;     // 0 .. 40 dB
double eGateThresholdDb (float v) noexcept; // -30 .. +10 dB (console scale)
bool eVoicing (const EffectParams& p) noexcept;
std::string eParamText (int param, float v);
bool vocalVoicing (const EffectParams& p) noexcept; // round 145
// round 148: the 4K voicing and its own scales (dial position 0..1)
bool k4Voicing (const EffectParams& p) noexcept;
std::string k4ParamText (int param, float v);
double k4Q (float v) noexcept;              // mid-band Q 0.5 .. 3 (log; up = narrower)
float k4QValue (double q) noexcept;
int k4ColourIndex (float v) noexcept;       // 0 Brown, 1 Black, 2 Orange
float k4ColourValue (int colour) noexcept;
double k4MicDb (float v) noexcept;          // 0 .. 50 dB
double k4OffsetDb (float v) noexcept;       // -12 .. +12 dB
std::string vocalParamText (int param, float v);
// round tp172c: the Modern voicing and its own scales (dial position 0..1; the translator inverts them)
bool modernVoicing (const EffectParams& p) noexcept;
std::string modernParamText (int param, float v);
double mHpfHz (float v) noexcept;    // 0 = out, else 20.9 .. 500 Hz (log)
double mLpfKHz (float v) noexcept;   // 0 = out, else 27.5 .. 3 kHz (log)
double mLfHz (float v) noexcept;     // 40 .. 600 Hz
double mLmfKHz (float v) noexcept;   // 0.2 .. 2 kHz
double mHmfKHz (float v) noexcept;   // 0.6 .. 7 kHz
double mHfKHz (float v) noexcept;    // 1.5 .. 22 kHz
double mLfGainDb (float v) noexcept; // ±16.5 dB
double mGainDb (float v) noexcept;   // ±20 dB (LMF, HMF, HF)
double mQ (float v) noexcept;        // 0.5 .. 2.5
double mRatio (float v) noexcept;    // 1 (0) .. 63 : 1, infinity at the top (1e9); the reference's dial
float mRatioValue (double ratio) noexcept;
double mHoldSeconds (float v) noexcept; // 0 .. 4 s
} // namespace ridge

// ---- Glow Leveler (optical leveler) ----
namespace glow
{
// round 129: trim (±18 dB after the mix) and auto make-up appended (the reference's TRIM and AUTO MAKEUP); older
// projects load their defaults (0 dB, off) and sound the same
// round 123: voicing (appended): 0 = Glow's own engine, 1 = the measured classic opto leveler (core/VoiceMatch.h), where
// peak reduction / gain / emphasis read 0 .. 100 like the original's dials (emphasis = its Hi Freq)
// round 152 (REA_rodada152a §2): appended — the opto law (the Opto classic voicing's measured Limit and AUTO MAKEUP; a
// project saved before keeps the round-123 law) and ANALOG (Off / 50 Hz / 60 Hz: the reference's measured hum and noise)
// round 165w (REA_rodada165w §2): appended — the opto release (the Opto classic voicing's measured gain recovery; a
// project saved before keeps round 123's slower tail)
// round 165x (REA_rodada165x §1): appended — the make-up reference (the peak reduction dial where AUTO MAKEUP was
// switched on: the trial's make-up follows the dial from there; 0.46 = round 152's fixed reference, so older projects
// are unchanged); the opto release now also selects the measured attack; and the VU display (gain reduction / input /
// output: the reference saves its VU DISPLAY knob with the state, so LYNX keeps the editor's selector with the project)
// round 190dm (appended): the Opto classic voicing's cell memory (VoiceMatch.h opto::Settings::memory; off = as before)
enum Param { peakReduction, gain, mode, emphasis, mix, trim, autoMakeup, voicing, optoLaw, analog, releaseLaw, makeupRef, vuDisplay, optoMemory, count };
// round 165x: an editor edit with the reference's AUTO MAKEUP behaviour (switching it on takes the dial as the
// reference; switching it off leaves the make-up in the Gain dial, as the trial turns its own Gain knob)
std::vector<std::pair<int, float>> linkedEdit (const EffectParams& p, int param, float value);
int analogHz (float v) noexcept; // 0 (off), 50 or 60
int vuModeOf (float v) noexcept;  // round 165x: the VU display: 0 gain reduction (0), 1 input (0.5), 2 output (1)
bool classic (const EffectParams& p) noexcept;
std::string classicParamText (int param, float v);
double trimDb (float v) noexcept;   // −18 .. +18 dB
double gainDb (float v) noexcept;   // 0 .. +30 dB make-up
double sidechainDb (float v) noexcept; // peak reduction: −20 .. +40 dB into the cell
} // namespace glow

// ---- Hush (de-esser) ----
namespace hush
{
// Round 128 (parity with the reference de-esser), appended so saved projects keep their meaning, every default
// leaving the output unchanged: allround (detection mode: 0 single vocal, 1 allround = the sibilance must also stand
// out from the full-band level), lowPass (the band's upper edge 2 .. 20 kHz; 1 = off), lookaheadTime (0 .. 15 ms, the
// delay = the latency; default 2 ms), stereoLink (0 .. 100 %), linkMid (detect on the mid signal only), external
// (detect the slot's side chain), input (±12 dB), display (editor only: show the waveform).
// round 123 (vmatch123): voicing at 9 — 0 = Hush's own engine, 1 = the measured classic split de-esser
// (core/VoiceMatch.h); round 128's controls follow it.
// round 135 (appended): oversampling (off, 2x, 4x, 8x) — Hush's own voicing applies its gain at the high rate (the
// measured classic voicing is left as it is); latency + the oversampler's.
// round 148 (appended): vocalVoicing — the single-vocal de-esser measured on a reference vocal de-esser
// (core/VoiceMatch.h vocalds: band detector + "stands out" test, look-ahead within a fixed 15 ms latency); uses the
// threshold, range, frequency (the band's high-pass), low-pass, look-ahead, mode and listen controls
enum Param { frequency, threshold, range, mode, detector, listen, lookahead, release, output, voicing,
             allround, lowPass, lookaheadTime, stereoLink, linkMid, external, input, display, oversampling, vocalVoicing,
             // round 165w (appended): the reference's input / output pan (a balance: the far side × (1 − |pan|), measured)
             inputPan, outputPan,
             // round 165x (appended, REA_rodada165x §2): the reference's ranges and stereo link — Input / Output −∞ … +36 dB
             // on the reference's own curve, the high-pass 2 … 20 kHz, Stereo link as the reference links (a blend of the
             // two channels' signals, 100 % = the mid signal) and its 100 … 200 % half: `linkOnly` (0 … 100 % of the
             // processing on the mid — or with `linkSide`, the side — signal only). A project saved before keeps the old
             // law (±12 dB, 2 … 16 kHz, Hush's own link) through upgradeLoadedEffectParams; the classic voicing ignores it.
             rangeLaw, linkOnly, linkSide,
             // round 190dm (appended, every default = the sound before): Air — a high shelf on the processed signal
             // (0 … +12 dB, measured on a de-esser reference's wet path: 8.15 kHz, S 0.5) — and Mix (dry / processed;
             // the dry signal delayed by the latency), for every voicing
             air, mix, count };
double airDb (float v) noexcept;        // round 190dm: 0 … +12 dB
constexpr double airHz = 8151.0, airSlope = 0.5;
bool extended (const EffectParams& p) noexcept;         // round 165x: the extended law (and not the classic voicing)
double levelDb (float v, bool extended) noexcept;       // input / output: ±12 dB, or −∞ (−200) … +36 dB
float levelValue (double db, bool extended) noexcept;
double hpHz (float v, bool extended) noexcept;          // the high-pass (the Frequency): 2 … 16 kHz, or 2 … 20 kHz
float hpValue (double hz, bool extended) noexcept;
double panValue (float v) noexcept; // −1 … +1
bool vocal (const EffectParams& p) noexcept; // round 148
int oversamplingOf (float v) noexcept; // 1 (off), 2, 4, 8
double hz (float v) noexcept;          // 2 .. 16 kHz
double lowPassHz (float v) noexcept;   // 2 .. 20 kHz (v ≥ 0.999 = off)
double lookaheadTimeMs (float v) noexcept; // 0 .. 15 ms
float lookaheadTimeValue (double ms) noexcept;
double classicThresholdDb (float v) noexcept; // voicing 1: -80 .. 0 dB (the original's dial)
bool classic (const EffectParams& p) noexcept;
std::string classicParamText (int param, float v);
std::string oldLawText (int param, float v); // round 165x: the texts with the old ranges (±12 dB, 2 … 16 kHz)
double thresholdDb (float v) noexcept; // −60 .. 0 dBFS
double rangeDb (float v) noexcept;     // 0 .. 24 dB
double releaseMs (float v) noexcept;   // 10 .. 250 ms
double lookaheadMs() noexcept;         // the default lookahead (2 ms)
} // namespace hush

// ---- Aether (air band + exciter) ----
namespace aether
{
// `dynamic` (appended, round 124) switches to the dynamic air model: two parallel high-passed bands (Mid air from
// ~3 kHz, High air from ~9 kHz), each lifted by a compressed copy of itself, so quiet highs come up more than loud
// ones; `midAir` / `highAir` set the two amounts (0..1). On by default since issue #77 (round tp172c); off keeps the
// shelf + exciter above (projects saved before round 124 load with it off).
enum Param { air, airFreq, harmonics, exciterFreq, character, mix, output, dynamic, midAir, highAir, count };
double airDb (float v) noexcept;       // 0 .. +15 dB
double airHz (float v) noexcept;       // 5 .. 20 kHz
double exciterHz (float v) noexcept;   // 1 .. 12 kHz
} // namespace aether

// ---- Kiln Channel (round tp172c7: one console channel on the DRIVE section's console models) ----
namespace kiln
{
// Input / Output ±18 dB around the console, Console = six steps (British E, British G, American A, British N, Y, Tube =
// the drive models Console E .. Tube), Drive 0 .. 18 dB, the console's noise while Noise reduction is off, Group 0
// (none) .. 8 with a Group bypass shared by the group, Main in / out ±24 dB around everything. Link I/O (the output
// follows the input inversely) and Group I/O (the trims move with the group's) are the editor's: no DSP of their own
enum Param { input, output, console, drive, noiseReduction, group, groupBypass, mainIn, mainOut, ioLink, ioGroup, count };
double trimDb (float v) noexcept;
double mainDb (float v) noexcept;
double driveDb (float v) noexcept;
int consoleStep (float v) noexcept;   // 0 .. 5
float consoleValue (int step) noexcept;
int groupOf (float v) noexcept;       // 0 = none, 1 .. 8
const char* consoleName (int step) noexcept;
// The edits one control change makes on this channel (Link I/O: an input change moves the output the other way)
std::vector<std::pair<int, float>> linkedEdit (const EffectParams& p, int param, float value);
} // namespace kiln

// ---- Fret Channel (round tp172c8: a guitar / bass channel strip) ----
// The guitar / bass channel role (SSL Guitarstrip, measured black-box on its trial: GS_bank / GS_x / GS_y). Every control
// has the reference's own range and law, so a value reads the same on both: input / output ±24 dB, the output mix; the
// phase correction (a delay of ±20 ms around a fixed 20 ms, polarity, a first- or second-order all-pass; Align to: the
// side chain = both channels move, Left / Right = only the other channel moves); the EQ (first-order low / high shelves
// ±12 dB, a Q 0.5 mid bell 0.5 .. 7 kHz, a 24 dB/oct high-pass 20 .. 300 Hz, a steep low-pass 3 .. 20 kHz, before or
// after the compressor) on Ridge's engine; the compressor (threshold -36 .. 0 dB, Fastest .. Slowest, mix, auto gain);
// the amp (guitar / bass on the DRIVE section's measured models, the guitar pre-amp, Boost / Punch / Edge, the bass
// Punch / Shape, the drive mix); Mono listen and Side-chain listen. Latency: 20 ms + the amp's oversampler, always
namespace fret
{
enum Param { input, output, outputMix, phaseCorrection, alignMode, delayIn, delay, phaseInvert, allPassIn, allPassFreq,
             allPassQ, allPassOrder, hpf, eqIn, eqPrePost, lpf, lowGain, midFreq, midGain, highGain, compIn, compThreshold,
             compMix, compAutoGain, compMode, driveIn, driveModel, preAmp, guitarDrive, guitarBoost, guitarPunch, guitarEdge,
             bassDrive, bassPunch, bassShape, driveMix, monoListen, scListen, autoListen, count,
             // hidden (not in the info, saved only while non-zero): the EQ band Auto listen solos while its knob moves,
             // set by the editor during the drag (0 none, 1 low, 2 mid, 3 high, stored / 3)
             listenBand = count };
double gainDb (float v) noexcept;        // input / output -24 .. +24 dB
double delayMs (float v) noexcept;       // -20 .. +20 ms
double allPassHz (float v) noexcept;     // 20 .. 20000 Hz (log)
double allPassQOf (float v) noexcept;    // 0.10 .. 6.00 (log)
double hpfHz (float v) noexcept;         // 0 = off, else 20 .. 300 Hz (log)
double lpfKHz (float v) noexcept;        // 1 = off, else 3 .. 20 kHz (log)
double shelfDb (float v) noexcept;       // the low / mid / high gains -12 .. +12 dB
double midKHz (float v) noexcept;        // 0.5 .. 1 kHz on the lower half, 1 .. 7 kHz on the upper (log)
double thresholdDb (float v) noexcept;   // -36 .. 0 dB
double preAmpDb (float v) noexcept;      // -6 .. +30 dB
double ampDrive (float v) noexcept;      // 0 .. 11
double voicePct (float v) noexcept;      // Boost / Punch / bass Punch -100 .. +100 %
double voiceSteps (float v) noexcept;    // Edge / bass Shape 0 .. 10
int alignOf (float v) noexcept;          // 0 side chain, 1 left, 2 right
int modeOf (float v) noexcept;           // 0 Fastest .. 4 Slowest
float modeValue (int mode) noexcept;
const char* modeName (int mode) noexcept;
const char* alignName (int align) noexcept;
// float values of the reference's own units (the translator and the tests)
float midValue (double kHz) noexcept;
float hpfValue (double hz) noexcept;
float lpfValue (double kHz) noexcept;
// Ridge's engine settings for this strip's EQ and compressor (the editor draws the EQ curve with ridge::responseDb)
EffectParams ridgeParams (const EffectParams& p);
double autoGainDb (const EffectParams& p) noexcept; // the auto gain's make-up for the threshold and mode
int baseDelaySamples (double sampleRate) noexcept;  // the fixed 20 ms the delay moves around
} // namespace fret

// ---- Shell Channel (round tp172c8: a drum channel strip) ----
// The drum channel role (SSL Native Drumstrip, measured black-box on its trial: DS_bank / DS_y): input / output ±20 dB, a
// gate (open / close thresholds, range, attack, hold, release), a transient shaper (gain, speed, amount, invert, listen),
// the low- and high-frequency enhancers (frequency, drive, amount: a steep resonant low lift and a third-order high lift
// added to the signal), the listen-mic compressor (comp, narrow band, make-up, dry / wet), the output dry / wet and the
// order of the five modules (120 orders). Latency: 4.1 ms, always
namespace shell
{
enum Param { input, gateOn, gateAttack, gateHold, gateRelease, gateOpen, gateClose, gateRange, tsGain, tsSpeed, tsAmount,
             tsOn, tsInvert, tsListen, hfOn, hfFreq, hfDrive, hfAmount, lfOn, lfFreq, lfDrive, lfAmount, lmcOn, lmcNarrow,
             lmcComp, lmcMakeup, lmcMix, output, outputMix, processOrder, count };
enum Module { gate, shaper, lowEnhancer, highEnhancer, listenMic, modules };
double gainDb (float v) noexcept;        // input / output / shaper gain / LMC make-up -20 .. +20 dB
double attackS (float v) noexcept;       // 0 .. 0.1 s
double holdMs (float v) noexcept;        // 0 .. 4000 ms
double releaseS (float v) noexcept;      // 0.01 .. 1 s
double thresholdDb (float v) noexcept;   // open / close -30 .. 0 dB
double rangeDb (float v) noexcept;       // 0 .. 80 dB
double hfHz (float v) noexcept;          // 2 .. 20 kHz (log)
double lfHz (float v) noexcept;          // 20 .. 250 Hz (log)
double driveSteps (float v) noexcept;    // 0 .. 10
int orderIndex (float v) noexcept;       // 0 .. 119
float orderValue (int index) noexcept;
std::array<int, modules> orderOf (int index) noexcept;  // the modules in processing order
int indexOf (const std::array<int, modules>& order) noexcept;
const char* moduleName (int module) noexcept;
std::string orderText (int index);
float thresholdValue (double db) noexcept;
float releaseValue (double s) noexcept;
float hfValue (double hz) noexcept;
float lfValue (double hz) noexcept;
int latencySamples (double sampleRate) noexcept;
} // namespace shell

// ---- Ember (saturator) ----
namespace ember
{
enum Param
{
    drive, style, tone, lowCut, highCut, mix, output, autoGain, quality,
    // multiband mode (round 108): with bands > 1 the drive / style above are band 1's; the tone, cuts, mix, output,
    // auto gain and oversampling stay global
    bands, xover1, xover2, xover3, xoverSlope, band1Mix, band1Level,
    band2Style, band2Drive, band2Mix, band2Level,
    band3Style, band3Drive, band3Mix, band3Level,
    band4Style, band4Drive, band4Mix, band4Level,
    // round 128 (Saturn parity, appended): the input gain, then per band its power, state (normal / solo / mute),
    // tone (bass / mid / treble / presence), feedback (amount, frequency) and dynamics. Every default is the old sound.
    inputGain,
    band1On, band1State, band1Bass, band1Mid, band1Treble, band1Presence, band1Feedback, band1FeedbackFreq, band1Dynamics,
    band2On, band2State, band2Bass, band2Mid, band2Treble, band2Presence, band2Feedback, band2FeedbackFreq, band2Dynamics,
    band3On, band3State, band3Bass, band3Mid, band3Treble, band3Presence, band3Feedback, band3FeedbackFreq, band3Dynamics,
    band4On, band4State, band4Bass, band4Mid, band4Treble, band4Presence, band4Feedback, band4FeedbackFreq, band4Dynamics,
    // round 135 (the Decapitator role, appended; every default is the old sound): PUNISH (+20 dB before the drive),
    // THUMP (a resonant low cut), STEEP (a 30 dB/oct high cut), a one-click VOICE (0 = the Style menu, else one of five
    // voicings: a curve and its own EQ), and the reference's filter ranges ("wide cuts": a low cut 20 Hz – 1 kHz and a
    // 6 dB/oct high cut 1 – 20 kHz, read from the two parameters after it)
    punish, thump, steep, voice, wideCuts, lowCutWide, highCutWide,
    // round 136 (appended): the Classic voicing (core/EmberClassic.h), measured on the reference five-style saturator
    // its substitute role plays; its own controls with the original's laws. Voicing 0 = Ember's own engine (unchanged)
    voicing, classicStyle, classicDrive, classicPunish, classicLowCut, classicThump, classicTone, classicHighCut,
    classicSteep, classicMix, classicOutput,
    // round 147 (Saturn 2 parity, measured black-box on the trial, REA_rodada147b; appended, defaults = the old sound):
    // the channel mode (0 left / right, 1 mid / side: M = (L + R) / 2, S = (R − L) / 2 go through the bands' two
    // channels), each band's drive pan (−1 … +1: the far channel's drive × (1 − |pan|)) and each band's pan (−1 … +1:
    // the far channel's gain 1 − |pan|; in mid / side, left = mid)
    channelMode, band1DrivePan, band2DrivePan, band3DrivePan, band4DrivePan, band1Pan, band2Pan, band3Pan, band4Pan,
    // round 150 (Saturn 2 parity, REA_rodada150b; appended, every default is the old sound): bands 5 and 6 — `bandsMore`
    // 0 = the Bands parameter (1 … 4), 1 = five, 2 = six bands; crossovers 4 and 5; bands 5 / 6's blocks with the same
    // fields as bands 2 … 4 (style, drive, mix, level), round 128's nine and round 147's drive pan / pan — then the
    // global input / output pan (−1 … +1, the far side × (1 − |pan|), measured) and each band's character (0 = the
    // Style menu, else one of the 28 voicings measured black-box on the trial's styles, core/EmberCharacters.inc)
    bandsMore, xover4, xover5,
    band5Style, band5Drive, band5Mix, band5Level,
    band6Style, band6Drive, band6Mix, band6Level,
    band5On, band5State, band5Bass, band5Mid, band5Treble, band5Presence, band5Feedback, band5FeedbackFreq, band5Dynamics,
    band6On, band6State, band6Bass, band6Mid, band6Treble, band6Presence, band6Feedback, band6FeedbackFreq, band6Dynamics,
    band5DrivePan, band6DrivePan, band5Pan, band6Pan,
    inputPan, outputPan,
    band1Character, band2Character, band3Character, band4Character, band5Character, band6Character,
    // round 152 (Saturn 2 parity, measured black-box on the trial, REA_rodada152b; appended, defaults = the old sound):
    // each crossover's own slope (0 = the global Crossover slope, else 6 / 12 / 24 / 36 / 48 dB/oct: the first-order
    // pair and LR2 … LR8, exactly the reference's), the processing mode (minimum phase = the IIR tree, linear phase =
    // zero-phase FIR bands, the reference's 3072-sample latency at 48 kHz), the modulation (two step LFOs, an envelope
    // follower, four slots source → target × amount, the reference's laws) and the effect characters' measured law
    // (Collapse pitches down 24 semitones × drive; projects saved before round 152 keep the static stand-in)
    crossSlope1, crossSlope2, crossSlope3, crossSlope4, crossSlope5,
    processing,
    lfo1Rate, lfo1Shape, lfo2Rate, lfo2Shape,
    envAttack, envRelease,
    mod1Source, mod1Target, mod1Amount, mod2Source, mod2Target, mod2Amount,
    mod3Source, mod3Target, mod3Amount, mod4Source, mod4Target, mod4Amount,
    effectLaw,
    // round 165 (Decapitator parity, measured black-box on the trial, REA_rodada165ha; appended): the Classic voicing's
    // Auto (its editor moves Output −1.6 dB per Drive unit, as the reference's; no audio effect) and the Classic law
    // (1 = Punish's measured law; projects saved before round 165 keep round 136's +9.75 dB)
    classicAuto, classicLaw,
    // round 190ar (appended; on for new instances): the Classic voicing's high-drive tables with the coupling ahead of
    // the curve (core/EmberClassic.h: styles driven hard hold the reference's uneven square wave). Off = round
    // trial187b's tables: what a project saved before loads with (upgradeLoadedEffectParams). Drives up to 5 ignore it.
    classicHot,
    // round 190fj (appended; Measured for new instances, Classic for projects saved before): the Style menu's Tape on a
    // measured magnetic core (core/EmberTape.h: hysteresis between a record and a playback EQ, the level law and the
    // speed losses fitted black-box on the reference tape saturator's renders) in the single-band mode, and the tape
    // speed it runs at (30, 15, 7½, 3¾, 1⅞ ips; 15 by default). Classic = the tape curve of round 108 … 190, bit for bit.
    law, tapeSpeed,
    count
};
constexpr int tapeSpeeds = 5;
int tapeSpeedIndex (float v) noexcept;          // 0 … 4 = 30, 15, 7½, 3¾, 1⅞ ips
const char* tapeSpeedName (int k) noexcept;
// round 190fj: the single-band Tape runs the measured core (Law = Measured, no voice / character on band 1)
bool measuredTape (const EffectParams& p) noexcept;
// round 165: an editor edit of `param` with the Classic voicing's Auto on also moves Output (the reference's link);
// returns every (parameter, value) to set — just the edit itself when nothing is linked
std::vector<std::pair<int, float>> linkedEdit (const EffectParams& p, int param, float value);
// round 152: crossover k's slope (0 … 4) in dB/oct — its own, or the global Crossover slope
int crossoverSlopeDbOct (const EffectParams& p, int k) noexcept;
constexpr int ownSlopes = 5;                    // 6, 12, 24, 36, 48
int ownSlopeDbOct (float v) noexcept;           // 0 = global (returns 0), else 6 … 48
float ownSlopeValue (int dbOct) noexcept;       // 6 … 48 → its value (0 = global)
bool linearPhase (const EffectParams& p) noexcept;
int linearLatency (double sampleRate) noexcept; // 3072 samples at 48 kHz (the reference's, measured)
// the linear-phase band shape: 1 / (1 + x^m), x = f / crossover, m = slope / 6 (measured, zero phase)
double linearLow (double hz, double crossoverHz, int slopeDbOct) noexcept;
// modulation (round 152): sources and targets
constexpr int modSlots = 4;
enum class ModSource { off, lfo1, lfo2, envelope };
constexpr int modSources = 4;
ModSource modSourceOf (float v) noexcept;
// targets: 0 none, 1 input gain, 2 output gain, 3 mix, 4 … 9 band 1 … 6 drive, 10 … 15 band level, 16 … 21 band mix,
// 22 … 26 crossover 1 … 5
constexpr int modTargets = 27;
int modTargetOf (float v) noexcept;
const char* modTargetName (int t) noexcept;
int modTargetParam (int t) noexcept;            // the Ember parameter a target moves (−1 for none)
// the change of that parameter (0 … 1 units) for a modulation of `units` in the reference's units of that target
// (its full range = 1: gains ±36 dB, drive 0 … 100 % = 30 dB of Ember's, level −∞ … +36 dB, crossover 40 Hz … 18 kHz)
double modDelta (int target, double units) noexcept;
double modAmount (float v) noexcept;            // −1 … +1 (the reference's level law on |2v − 1|, signed)
float modAmountValue (double level) noexcept;   // inverse
double lfoRateHz (float v) noexcept;            // 0.02 … 500 Hz (the reference's knob law)
float lfoRateValue (double hz) noexcept;
enum class LfoShape { sine, triangle, square, easeIn, easeOut };
constexpr int lfoShapes = 5;
LfoShape lfoShapeOf (float v) noexcept;
double lfoValue (LfoShape s, double phase) noexcept; // −1 … +1 at a phase 0 … 1 (two steps +1 / −1 with a glide)
double envAttackMs (float v) noexcept;          // 1 ms … 1 s (the reference's law)
double envReleaseMs (float v) noexcept;         // 50 ms … 5 s
// the follower's source value for a level (dB of its mean-square follower): (dB + 60.7) / 30, ≥ 0 (measured)
double envSource (double followerDb) noexcept;
bool modulated (const EffectParams& p) noexcept;
bool measuredEffects (const EffectParams& p) noexcept;
constexpr int collapseCharacter = 25;           // the character that pitches down with the measured law
double collapseRatio (double referenceDrive) noexcept; // 2^(−2 · drive): −24 semitones at 100 %
constexpr int maxBands = 6;   // round 150 (four before)
constexpr int firstBands = 4; // the bands whose parameters sit in the first blocks
inline int bandDrivePanParam (int band) noexcept
{
    band = std::clamp (band, 0, maxBands - 1);
    return band < firstBands ? band1DrivePan + band : band5DrivePan + band - firstBands;
}
inline int bandPanParam (int band) noexcept
{
    band = std::clamp (band, 0, maxBands - 1);
    return band < firstBands ? band1Pan + band : band5Pan + band - firstBands;
}
// round 150: crossover i (0 … 4: between band i and i + 1), a band's character parameter, the active band count
inline int crossoverParam (int i) noexcept
{
    i = std::clamp (i, 0, maxBands - 2);
    return i < 3 ? xover1 + i : xover4 + i - 3;
}
inline int characterParam (int band) noexcept { return band1Character + std::clamp (band, 0, maxBands - 1); }
int activeBands (const EffectParams& p) noexcept; // 1 … 6 (Bands, or bandsMore when it is set)
// the two parameter values that select n bands (1 … 6): first = Bands, second = bandsMore
std::pair<float, float> bandCountValues (int n) noexcept;
constexpr int characters = 28;               // the measured voicings (1 … 28; 0 = off)
int characterIndex (float v) noexcept;       // 0 … 28
const char* characterName (int k) noexcept;  // LYNX's own names ("Off" for 0)
// the drive dB of Ember (0 … 36) on the reference's 0 … 100 % scale (its translation: % × 30 dB), clamped
inline double referenceDrive (double driveDb) noexcept { return std::clamp (driveDb / 30.0, 0.0, 1.0); }
// a voicing's static curve at a drive (0 … 1 on the reference's scale): y = A · g (D · x) / D, g with unit slope at 0
// (A = the small-signal gain)
float characterShape (int k, double drive, float x) noexcept;
double panOf (float v) noexcept;        // −1 … +1 (0.5 = centre)
// the two channels' scale for a pan of −1 … +1 (the far side's 1 − |pan|): first = channel 0 (left / mid)
std::pair<float, float> panScales (double pan) noexcept;
bool midSide (const EffectParams& p) noexcept;
// a band's round-128 parameters: field 0 power, 1 state, 2 bass, 3 mid, 4 treble, 5 presence, 6 feedback,
// 7 feedback frequency, 8 dynamics
constexpr int extraFields = 9;
inline int bandExtra (int band, int field) noexcept
{
    band = std::clamp (band, 0, maxBands - 1);
    field = std::clamp (field, 0, extraFields - 1);
    return band < firstBands ? band1On + band * extraFields + field : band5On + (band - firstBands) * extraFields + field;
}
enum class BandState { normal, solo, mute };
BandState bandState (float v) noexcept;
double inputDb (float v) noexcept;      // ±24 dB before everything
double toneDb (float v) noexcept;       // ±18 dB: bass (low shelf 120 Hz), mid (bell 800 Hz), treble (high shelf 3.5 kHz),
                                        // presence (bell 6 kHz) on the band's saturated signal
double feedbackAmount (float v) noexcept; // 0 … 1: the band-passed output fed back into the shaper (loop gain up to 0.9)
double feedbackHz (float v) noexcept;   // 10 Hz … 1 kHz (log): the feedback band's centre
double dynamicsAmount (float v) noexcept; // −1 (compress) … +1 (expand) the band's level after the drive
// console: only through a voice (round 135). tubeBiased (round 170c): the Tube curve of rounds 96 … 169 (a tanh biased
// by 0.35, the knee at the curve's unit level), kept for the Tape pre and Triode voicings and Voice Shift; the Style
// menu's Tube has 6 dB more headroom and a milder bias since round 170c (issue: "the first mode sounds garbly")
enum class Style { tube, tape, transistor, fuzz, console, tubeBiased };
constexpr int styles = 4; // the Style parameter's choices
Style styleOf (float v) noexcept;
double driveDb (float v) noexcept;     // 0 .. +36 dB
double toneTilt (float v) noexcept;    // −1 (dark) .. +1 (bright), ±6 dB tilt around 1 kHz
double lowCutHz (float v) noexcept;    // 0 = off, else 20 .. 500 Hz (before the drive)
double highCutHz (float v) noexcept;   // 1 = off, else 2 .. 20 kHz (after the drive)
int oversampling (float v) noexcept;   // 2, 4 or 8
// round 135
constexpr int voices = 5;               // Voice: 0 = off (the Style menu), 1 … 5
int voiceIndex (float v) noexcept;      // 0 … 5
const char* voiceName (int k) noexcept; // LYNX's names of the five voicings
Style voiceStyle (int k) noexcept;      // the curve of a voicing
double lowCutWideHz (float v) noexcept; // 0 = off, else 20 Hz … 1 kHz (log)
double highCutWideHz (float v) noexcept;// 1 = off, else 1 … 20 kHz (log)
constexpr double punishDb = 20.0;
constexpr double thumpQ = 1.6;          // the low cut's Q with THUMP on [EXEC trial: +3.5 dB at the cutoff]
// the drive on the reference's 0 … 10 scale (the read-out of the single-band mode): 3.6 dB per step
double driveSteps (float v) noexcept;
float shape (Style s, float x) noexcept; // the transfer curve (no drive, no DC removal)
int bandCount (float v) noexcept;      // 1 .. 4 (the Bands parameter alone; activeBands() adds bandsMore)
double crossoverHz (float v) noexcept; // 30 Hz .. 18 kHz (log)
float crossoverValue (double hz) noexcept;
int slopeDbOct (float v) noexcept;     // 12, 24 or 48 (Linkwitz-Riley)
double bandLevelDb (float v) noexcept; // ±18 dB
// a band's style / drive / mix / level parameters (band 0 = the global Style and Drive, band1Mix / band1Level)
int bandParam (int band, int field) noexcept; // field: 0 style, 1 drive, 2 mix, 3 level
} // namespace ember

const EffectInfo& ridgeInfo();
const EffectInfo& glowInfo();
const EffectInfo& hushInfo();
const EffectInfo& aetherInfo();
const EffectInfo& emberInfo();
const EffectInfo& kilnInfo();
const EffectInfo& fretInfo();
const EffectInfo& shellInfo();
const EffectInfo& strikeInfo(); // round trans189d: Strike Shaper (core/StrikeShaper.h)
const EffectInfo& anvilInfo();  // round 190fk: Anvil Bus (core/AnvilBus.h)

std::string paramText (EffectKind kind, int param, float v);
std::unique_ptr<Effect> make (EffectKind kind);
// Hush with its look-ahead (= latency) known before the first block (round 128)
std::unique_ptr<Effect> makeHush (const EffectParams* params);
std::unique_ptr<Effect> makeEmber (const EffectParams* params); // round 152: the processing mode known at once
// issue #113: the latency known before the first block (Ridge: the Vocal voicing / DRIVE; Aether: the model)
std::unique_ptr<Effect> makeRidge (const EffectParams* params);
std::unique_ptr<Effect> makeAether (const EffectParams* params);
bool isSuiteKind (EffectKind kind) noexcept;

// Meters the editor shows (written by the audio thread, read by the UI): see Effect::controlOutput.
// Output 0 = gain reduction 0..1 over 0..30 dB (Ridge: compressor), 1 = second gain reduction (Ridge: gate),
// 2 = input peak, 3 = output peak (linear, 0..1 clamped).
} // namespace mixsuite
} // namespace lynx
