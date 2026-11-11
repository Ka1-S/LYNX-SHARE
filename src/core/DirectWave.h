#pragma once

// FL Studio's DirectWave sampler rebuilt as decoded in docs/analise/REA_rodada72.md (clean-room; our own code from the
// behaviour read in the installation's DirectWave_x64.dll and checked by running it unmodified).
//
// A DirectWave program is a set of zones (a sample with key/velocity ranges and its own voice settings). Per note the
// plugin starts one voice per matching zone; per block of at most 256 samples a voice evaluates its 16-slot
// modulation matrix (note, velocity, wheels, two envelopes, two LFOs, envelope follower, random, sample position…
// onto pitch, gain, pan, sample points, two filters, the envelopes, the zone effects, the sends and the other slots'
// amounts), reads the sample with 32.32 fixed-point linear interpolation (forward, sustain or bounce loops), runs the
// two filters (serial or parallel; RBJ biquads applied twice), a per-voice limiter, the squared linear amplitude
// envelope and the smoothed square-root pan law, and adds the result to the program's dry, delay, chorus and reverb
// buses. The program then runs its delay, chorus and reverb and two waveshapers and adds the buses to the output.
//
// The channel keeps the plugin's saved state (0xD5) in ChannelDef::pluginState and the zones' samples in
// ChannelDef::zones (one per zone of the played program, in order).

#include <array>
#include <functional>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Instruments.h"

namespace lynx
{
namespace dw
{
// --------------------------------------------------------------------------------------------- saved state
struct Filter
{
    std::uint8_t type = 0;                    // 0 off, 1 low-pass, 2 high-pass, 3 band-pass, 4 notch, 5 all-pass, 6/7 others
    float cutoff = 0.0f, res = 0.0f, shape = 0.0f, extra = 0.0f; // 0…1
};

struct Lfo
{
    std::uint8_t type = 0, sync = 0;
    float rate = 0.0f, phase = 0.0f, fade = 0.0f, extra = 0.0f;
};

struct ModSlot
{
    std::uint16_t src = 0, dst = 0;
    float amount = 0.5f;                      // 0…1, effective ((a − ½)·2)³
};

struct Zone
{
    // header 0x1F4
    int root = 60, lowKey = 0, highKey = 127, lowVel = 0, highVel = 127;
    int exclusiveGroup = 0, output = 0;
    bool parallelFilters = false;             // +0x2C4
    float gain = 1.0f, pan = 0.5f;
    int altGroup = 0;
    // format 0x1F7
    std::int64_t frames = 0;
    int channels = 1, beats = 0, bits = 16, loopMode = 0, loopA = 0, loopB = 0, offset = 0;
    float rate = 44100.0f;
    // tuning 0x1F8
    float pitch = 0.5f;
    int coarse = 0, fine = 0, keyTrack = 100;
    // stretch 0x1F9
    int stretchMode = 0, stretchSync = 0;
    float stretch[3] {};
    float sends[4] { 0.0f, 0.0f, 0.0f, 1.0f }; // delay, chorus, reverb, dry
    Filter filter[2];
    bool syncTempo = false, sliceKeys = false;
    float amp[4] { 0.0f, 0.0f, 1.0f, 0.0f }; // A, D, S, R
    std::uint8_t fxOn[4] {};
    float fx[4][2] {};
    float modEnv[2][4] {};
    Lfo lfo[2];
    std::array<ModSlot, 16> mods {};
    std::string path;
    std::vector<std::uint8_t> embedded;       // raw planar audio (0x205), (frames + 512)·bits/8 per channel
};

struct Program
{
    std::string name;
    int index = 0;
    int voiceMode = 0;                        // 0 poly, 1 mono, 2 legato
    bool glideLegatoOnly = false;
    float gain = 1.0f, glide = 0.0f;
    bool shape1 = false, shape2 = false;      // 0x68 waveshapers
    float shape1Amount = 0.0f, shape2Amount = 0.0f;
    bool delayOn = false, delayCross = false;
    float delay[4] {};
    bool reverbOn = false;
    float reverb[4] {};
    bool chorusOn = false;
    float chorus[4] {};
    Lfo lfo[2];
    float modValue[4] {};
    struct AltGroup { int mode = 0; float probability = 1.0f, velTolerance = 0.0f; };
    std::array<AltGroup, 100> groups {};
    std::vector<Zone> zones;
};

struct State
{
    std::uint32_t flags = 0;                  // chunk 6
    std::vector<Program> programs;            // programs with zones (index = Program::index)
};
std::optional<State> parseState (const std::vector<std::uint8_t>& state);

// --------------------------------------------------------------------------------------------- sample data
// A zone's samples as DirectWave holds them: per channel 256 frames of silence, the frames, 256 frames of silence.
// 16-bit sources stay integers (read × 1/32767), others are floats.
struct ZoneSamples
{
    int bits = 32;
    std::int64_t frames = 0;                  // as loaded (the zone record's values are replaced by these)
    int channels = 0;
    std::vector<std::vector<std::int16_t>> i16;
    std::vector<std::vector<float>> f32;
};

// --------------------------------------------------------------------------------------------- engine
struct EngineOptions
{
    bool fineBlocks = false;                  // 8-sample control blocks instead of 256 (plugin setting)
    bool velocityAsInitVol = false;           // flag bit 1 (0xaaa739): velocity from Init.Vol, other gain law
    bool noGainClamp = false;                 // flag bit 0 (0xaaa738)
};

class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    // `samples[i]` belongs to program.zones[i] (empty = silent zone)
    void setProgram (const Program& program, std::vector<std::shared_ptr<const ZoneSamples>> samples, const EngineOptions& o);
    void setSampleRate (double sampleRate);
    void setTempo (double bpm);
    void reset();

    // note on: host velocity 0…1 (FL's), Final.Vol, Final.Pan (−1…1), cents added to the note (Final.Pitch − key)
    void noteOn (std::uint64_t id, int key, double velocity, double finalVol, double finalPan, double cents);
    void noteOff (std::uint64_t id);
    void setLevels (std::uint64_t id, double finalVol, double finalPan, double cents);
    // adds `n` frames to interleaved stereo `out`
    void render (float* out, int n);
    bool active() const;
    // the program's delay / chorus / reverb still sound after the last voice (round 190aj): the channel is not idle yet
    bool ringing() const;

    std::uint32_t randState = 1;              // the C run-time's rand() state the plugin draws from

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// --------------------------------------------------------------------------------------------- LYNX channel
// The program LYNX plays: the first one with zones (the one the demos' first MIDI slot selects).
const Program* playedProgram (const State& state);

// modelParams of an imported channel: { 1, zone count, then per zone 1 when DirectWave holds it as 16-bit
// integers (16-bit PCM file or 16-bit embedded audio), else 0 }
std::vector<float> encodeFormats (const std::vector<bool>& int16);
std::vector<bool> decodeFormats (const std::vector<float>& params);

// a decoded sample as the plugin holds it (16-bit sources back to their integers)
std::shared_ptr<const ZoneSamples> zoneSamples (const SampleData& sample, bool int16);

// Fills an imported channel (FlpImport): the state, the zones' files (resolved with `existing`, or the audio
// the project embeds, stored with `saveEmbedded`) and their formats (read with `readFile`). False: not usable.
bool importChannel (ChannelDef& def, const std::vector<std::uint8_t>& state,
                    const std::function<std::string (const std::string&)>& existing,
                    const std::function<std::vector<std::uint8_t> (const std::string&)>& readFile,
                    const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveEmbedded);

// `samples[i]`: program zone i (null = silent)
std::unique_ptr<Instrument> makeDirectWaveInstrument (std::vector<std::shared_ptr<const ZoneSamples>> samples);

// --------------------------------------------------------------------------------------------- reference
struct RefNote
{
    int start = 0, key = 60;
    double velocity = 0.78, finalVol = 1.0, finalPan = 0.0, cents = 0.0;
    int release = -1;
};

// Renders like r72's harness drives the DLL: host blocks of `block` samples split at every event. Interleaved stereo.
std::vector<float> renderReference (const Program& program, const std::vector<std::shared_ptr<const ZoneSamples>>& samples,
                                    const EngineOptions& options, const std::vector<RefNote>& notes, double sampleRate,
                                    int length, int block = 512, std::uint32_t randSeed = 1);

// CRT math as the plugin's run-time computes it (exposed for tests)
float sinfCrt (float x);
float cosfCrt (float x);
float powfCrt (float x, float y);
float expfCrt (float x);
} // namespace dw
} // namespace lynx
