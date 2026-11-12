#pragma once

// FL Studio Mobile's GMSynth module (type 2, 75 parameters) as its own voice engine, round 190ab (clean-room; written
// from the behavioural spec of round 190u, docs/analise/REA_rodada190ab_gmsynth.md). It is the ancestor of the desktop GMS
// (core/Gms.h) and shares its wave spectra, the 512 band-limited 1024-point tables and their choice, the dithered
// 32-bit phase read, the generator, the envelope follower and the voice-steal rule, but it is another instrument:
// two oscillators + noise, four modulation kinds (AM / FM / SNC / FLT), a linear amp ADSR, two AD envelopes, one
// ADSR "EG", one tempo-synced LFO, a 4-voice choir, 4 voices, an 8-pole filter whose (g, k) pairs come from a data
// file, three effect slots and a squared-balance master pan. 32-sample control chunks.
//
// A channel imported from a .flm plays it with instrument == gms and ChannelDef::modelParams = { 1 } (saved with the
// project): ChannelDef::pluginState then holds this module's own state ("FLMG", see writeState) instead of the
// desktop plugin's, and ChannelDef::samplePath the folder of FL Studio Mobile's resources ("…/Installed"), where the
// wave spectra (GMSynthWaveshapes/<name>.spectra) and the filter table (FilterTable_Ctf_Res.data) are read at run
// time. They belong to the user's installation and are never bundled.
//
// Host side: the note's velocity (0…1) is the voice's linear gain as it is, the note pan a linear balance; the
// channel's volume and pan (a linear balance too) are applied to the module's output, the channel's pitch is added to
// every voice. Random draws: FLM uses one process-global generator; LYNX gives every instance its own, started at 0,
// so the dither, the noise and the start phases of fresh voices differ from any given FLM render by design.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
// true when the channel asks for FL Studio Mobile's GMSynth (instrument gms, modelParams[0] ≥ 0.5)
bool gmsIsFlm (const ChannelDef& channel) noexcept;

namespace gmsmobile
{
constexpr int paramCount = 75;
constexpr int waveCount = 29;
constexpr int voiceCount = 4;
constexpr int chunkSize = 32;

// parameter ids (FLM's own numbering; 0 = the module's Enable)
namespace param
{
constexpr int enable = 0, level = 1, pan = 2, ampAttack = 3, ampDecay = 4, ampSustain = 5, ampRelease = 6, oscBPitch = 11,
              oscBDetune = 12, oscBMix = 13, oscBPhase = 14, oscMod = 15, oscModKind = 16, oscModAttack = 17, oscModDecay = 18,
              oscModContour = 19, noise = 20, cutoff = 21, resonance = 22, filterKind = 23, filterKeyboard = 24, filterAttack = 25,
              filterDecay = 26, filterContour = 27, transpose = 28, slideTime = 29, singleVoice = 30, autoPortamento = 31,
              lfoRate = 32, lfoShape = 33, lfoAmount = 34, lfoDest = 35, egAttack = 36, egDecay = 37, egSustain = 38,
              egRelease = 39, egContour = 40, egDest = 41, choirVoices = 42, choirScale = 43, choirDetune = 44, choirStereo = 45,
              fxBase = 46, fxStride = 7, egVelocity = 74;
// inside an effect slot (fxBase + fxStride · slot + …)
constexpr int fxKind = 0, fxEnable = 1, fxSync = 2, fxShape = 3, fxAmount = 4, fxX = 5, fxY = 6;
} // namespace param

// effect kinds, FLM's order
namespace fx
{
constexpr int dist = 0, flanger = 1, phaser = 2, lowPass = 3, highPass = 4, delay = 5, reverb = 6, pan = 7, ring = 8, vox = 9;
// true for the kinds this engine renders (all ten since round 190dw)
bool implemented (int kind) noexcept;
const char* name (int kind) noexcept;
} // namespace fx

struct Patch
{
    std::array<float, paramCount> P {};
    int waveA = 18, waveB = 18;       // index into the built-in wave list (18 = Sine)
    float bendRange = 1.0f / 3.0f;    // CSTM: round(3v) → 1, 2, 7, 12 semitones
    float mwAmount = 0.5f, mwDest = 1.0f; // CSTM: the mod wheel's amount and destination
};

// the module's defaults (FLM's own default table)
Patch defaultPatch();

// the wave's file base name under GMSynthWaveshapes ("Devils Horn" for index 2, shown as "Double Spike"); "" when
// the index is outside the list
std::string waveFileName (int index);

// The channel's state: "FLMG", u32 version (1), the 75 parameters, then FLM's 20-byte CSTM chunk (i32 wave A, i32
// wave B, f32 bend range, f32 mod-wheel amount, f32 mod-wheel destination), little-endian.
std::vector<std::uint8_t> writeState (const Patch& patch);
std::optional<Patch> readState (const std::vector<std::uint8_t>& state);

// the effect kind a slot's Kind parameter selects (FLM truncates 9·v in float: 0.11111111 → 1)
int fxKindOf (float v) noexcept;

// --- laws exposed for the tests (each is the engine's own code path)
// envelope rate per sample: attack (v = 0 → 1), decay, amp release, EG release
float attackRate (float v, double fs) noexcept;
float decayRate (float v, double fs) noexcept;
float ampReleaseRate (float v, double fs) noexcept;
float egReleaseRate (float v, double fs) noexcept;
// the entry of FilterTable_Ctf_Res.data the run-time table's entry i (0…65535) is read from
int filterFileIndex (int i) noexcept;
// the table index (number of harmonics − 1) an oscillator of frequency f plays
int tableFor (float f, double fs) noexcept;

// The engine as the instrument drives it. All calls but the constructor / prepare / setPatch are for the audio thread.
class Player
{
public:
    Player();
    ~Player();
    // (allocates) the sample rate, a full reset
    void prepare (double sampleRate);
    double sampleRate() const noexcept;
    // (reads the data files on first use, allocates) the patch and where FLM's resources are
    void setPatch (const Patch& patch, const std::string& resourceRoot);
    // true when the wave tables and the filter table were found
    bool resourcesLoaded() const noexcept;
    void setParam (int index, float value) noexcept;
    // velocity 0…1 (0 = note off), pan 0…1 (½ = centre), pitch = the note's own offset in semitones
    void note (int key, float velocity, float pan = 0.5f, float pitch = 0.0f) noexcept;
    // semitones added to every voice (the channel's pitch)
    void setPitchOffset (float semitones) noexcept;
    // (round 190dw item 4) the mod wheel as FLM's slot 74 takes it (0x18064db80: the raw 0…1 value, read by the CSTM's
    // MW destination × MW amount); the rack hands a CC #1 lane's value to it as it is (0x18059e0c0)
    void setModWheel (float value) noexcept;
    void allNotesOff() noexcept;
    // adds n samples; songBeats ≥ 0 = the transport plays (the position at the first sample)
    void render (float* left, float* right, int n, double tempo, double songBeats) noexcept;
    // nothing sounding: no voice and no effect tail
    bool idle() const noexcept;
    int activeVoices() const noexcept;

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

// Renders the patch like FLM's module: host blocks of `block` samples split at every event, tempo `bpm`, songPos ≥ 0
// = the transport plays from that position (beats). Interleaved stereo (2·length); empty when the data files are
// missing.
std::vector<float> renderReference (const Patch& patch, const std::vector<RefNote>& notes, double sampleRate, int length,
                                    const std::string& resourceRoot, int block = 512, double bpm = 120.0, double songPos = -1.0);
} // namespace gmsmobile
} // namespace lynx
