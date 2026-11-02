#pragma once

// LYNX Soundfont (round 158, GitHub issue #6): LYNX's own SoundFont 2 / 3 player, close to FL Studio's Fruity
// Soundfont Player (docs/analise/REA_rodada158.md).
//
// Loading: TinySoundFont (third_party/tinysoundfont, MIT; SF3's Ogg Vorbis samples through stb_vorbis) parses the
// file into presets of fully merged regions (preset + instrument generators, global zones, key / velocity ranges);
// LYNX copies them into its own Font (samples as 16-bit) and renders its own voices: SF2 volume / modulation
// envelopes, both LFOs, the resonant low-pass, loops (continuous / until release), exclusive classes, the SF2 default
// modulators (velocity → attenuation and → cut-off, mod wheel → vibrato, CC 7 / 10 / 11 / 91 / 93, pitch wheel with
// the RPN 0 range), sustain pedal, a polyphony limit, and FL-style overrides (envelope ADSR, pitch LFO, cut-off /
// resonance, reverb and chorus sends with built-in effects). Custom SF2 modulators (pmod / imod) are not applied
// (TinySoundFont does not parse them).
//
// Real time: a font is loaded and decoded on a background thread (FontLibrary) and handed to the instrument through
// an atomic pointer; the audio thread never allocates, locks or touches the file. Settings travel from configure()
// (UI thread) through a lock-free triple buffer.
//
// The channel stores the font's path in ChannelDef::samplePath (so the missing-files search relocates it) and the
// settings in ChannelDef::modelParams (the parameter table below, in order; new parameters only go at the END).

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "core/Instruments.h"
#include "core/Song.h"

namespace lynx::sf
{
// --- the font --------------------------------------------------------------------------------------------------------

struct Envelope
{
    float delay = 0, attack = 0, hold = 0, decay = 0, sustain = 0, release = 0; // seconds; sustain: see Region
    float keynumToHold = 0, keynumToDecay = 0; // timecents per key (≠ 0: hold / decay are kept in timecents)
};

enum class LoopMode : std::uint8_t { none, continuous, untilRelease };

struct Region
{
    std::uint8_t loKey = 0, hiKey = 127, loVel = 0, hiVel = 127;
    LoopMode loop = LoopMode::none;
    std::uint32_t exclusiveClass = 0;
    std::uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0; // frames in Font::samples; loopEnd exclusive
    std::uint32_t sampleRate = 44100;
    int rootKey = 60, keyTrack = 100;  // cents per key
    int transpose = 0, tune = 0;       // semitones, cents
    float attenuation = 0;             // centibels (0…1440)
    float pan = 0;                     // −0.5…0.5
    Envelope ampEnv, modEnv;           // ampEnv.sustain: gain 0…1; modEnv.sustain: level 0…1
    int filterFc = 13500, filterQ = 0; // absolute cents, centibels
    int modEnvToPitch = 0, modEnvToFilterFc = 0, modLfoToFilterFc = 0, modLfoToVolume = 0; // cents / cB
    float delayModLfo = 0, delayVibLfo = 0; // seconds
    int freqModLfo = 0, freqVibLfo = 0;     // absolute cents (8.176 Hz · 2^(c/1200))
    int modLfoToPitch = 0, vibLfoToPitch = 0;
    float chorusSend = 0, reverbSend = 0;   // 0…1

    // SFZ regions (core/Sfz.h, round 167o): what SF2 has no generator for. `sfz` false = a SoundFont region (none of
    // these apply).
    bool sfz = false;
    bool oneShot = false;          // loop_mode=one_shot: a note-off doesn't stop it
    bool releaseTrigger = false;   // trigger=release: starts at the note-off, with the note-on's velocity
    bool offFast = true;           // off_mode: fast (a few ms) or normal (its release)
    int group = 0, offBy = 0;      // group / off_by: starting a region of group G stops the voices whose off_by is G
    int seqLength = 1, seqPosition = 1; // round robin: plays on the seqPosition-th of every seqLength matching notes
    float loRand = 0, hiRand = 1;  // random: plays when the note's random number is in [loRand, hiRand)
    int switchLast = -1;           // sw_last: plays only while that key switch is the last one pressed
    float ampVelTrack = 1.0f;      // amp_veltrack (0…1): gain = 1 − t + t · (velocity / 127)²
};

struct Preset
{
    std::string name;
    int bank = 0, program = 0;
    std::vector<Region> regions;
};

struct Font
{
    std::string path, name; // name: the file's INAM (or the file name)
    int switchLo = -1, switchHi = -1, switchDefault = -1; // an SFZ's key switch range (sw_lokey / sw_hikey / sw_default)
    std::vector<Preset> presets; // sorted by bank, then program
    std::vector<std::int16_t> samples; // all samples, mono, plus 8 frames of silence at the end (interpolation)

    // the preset with that bank and program, else the nearest usable one (same program in bank 0, then the first);
    // −1 when the font has none
    int findPreset (int bank, int program) const noexcept;
};

// Parses a whole .sf2 / .sf3 file (bytes). nullptr when it is not a SoundFont or is damaged; `error` says why.
std::shared_ptr<const Font> parseFont (const std::vector<std::uint8_t>& bytes, std::string* error = nullptr);
// Reads and parses a file (UTF-8 path). nullptr when unreadable / not a SoundFont.
std::shared_ptr<const Font> loadFontFile (const std::string& path, std::string* error = nullptr);

// --- quick listing (search index) ------------------------------------------------------------------------------------

struct PresetInfo
{
    std::string name;
    int bank = 0, program = 0;
    bool operator== (const PresetInfo&) const = default;
};

struct FontInfo
{
    std::string name;            // INAM
    std::vector<PresetInfo> presets; // in the file's order
    bool valid = false;
};

// Only the header chunks (no sample data is read): what the preset browser indexes. Validates the hydra (every
// index in range) so a damaged file is rejected before it is parsed.
FontInfo scanFont (const std::vector<std::uint8_t>& bytes);
FontInfo scanFontFile (const std::string& path);

// The General MIDI family of a preset ("Piano", "Bass", "Drums"…): bank 128 = drum kits, otherwise from the program
// number, refined by words in the name ("808", "kick", "pad"…). English; the UI translates it.
const char* category (int bank, int program, const std::string& name);
// every category name, in display order
const std::vector<const char*>& categories();

// --- settings -----------------------------------------------------------------------------------------------------------

enum Param : int
{
    pBank, pProgram,
    pReverbAmount, pReverbOverride, pReverbBuiltIn, pReverbTrack,
    pChorusAmount, pChorusOverride, pChorusBuiltIn, pChorusTrack,
    pAttack, pDecay, pSustain, pRelease,          // 0 = the preset's own (FL: "at the minimum the bank's envelope")
    pLfoDelay, pLfoAmount, pLfoSpeed,             // pitch LFO override: amount 0 = off, 0.5 = zero
    pCutoff, pCutoffMod,                          // filter override: 0 = the preset's
    pResonance,                                   // LYNX: resonance override (0 = the preset's)
    pBendRange,                                   // semitones
    pResampling,                                  // 0 linear, 1 standard (cubic), 2 HQ (8-point sinc), 3 extreme HQ (16-point)
    pProgramMode,                                 // 0 automatic, 1 instrument, 2 drums (one-shot)
    pVelocityToCutoff,                            // the SF2 default velocity → cut-off modulator
    pEmuEnvelope,                                 // EMU10K1-style volume envelope (convex attack)
    pIgnoreLinks,                                 // "fix unexpected levels": stereo-linked layers counted once
    pPolyphony,
    pUseLoops,
    pVolume,                                      // output level, dB
    pVelocityCurve,                               // 0 SF2 (concave), 1 linear, 2 soft, 3 hard, 4 fixed
    pTranspose,                                   // semitones
    pModWheel,                                    // the mod wheel's resting position 0…1 (FL: MOD knob); CC 1 moves it live
    paramCount
};

struct ParamInfo
{
    const char* id;   // stable name (state, tests)
    const char* name; // English label
    float min, max, def;
    bool integer;
};
const ParamInfo& paramInfo (int index) noexcept;

// A channel's settings: modelParams in table order, missing ones at their default, each clamped to its range
std::array<float, paramCount> readParams (const std::vector<float>& modelParams) noexcept;
std::vector<float> defaultParams();
// 0…1 (automation) ↔ a parameter's value
float normalise (int index, float value) noexcept;
float denormalise (int index, float value01) noexcept;
// the shown value ("2.0 s", "Preset", "64 voices"…)
std::string displayValue (int index, float value);

// A new LYNX Soundfont channel (default name "LYNX Soundfont"; `fontPath` may be empty: a silent channel until a
// font is loaded)
ChannelDef makeChannel (const std::string& fontPath = {}, int bank = 0, int program = 0);

// --- FL Studio's Fruity Soundfont Player ------------------------------------------------------------------------------

// Its saved state (FL event 0xD5, versions 2…6; docs/analise/REA_rodada158.md §1)
struct FlPlayerState
{
    int version = 0;
    std::array<int, 13> p {};       // P0 program 1…256 (> 128: drum bank), P1 bank, P2/P3 reverb / chorus level 0…256,
                                    // P4 mod wheel, P5…P8 A/D/S/R, P9…P11 LFO DEL/AMT/SPD, P12 CUT (minimum = the preset's)
    std::string path;               // as saved: FL path macros (%FLStudioFactoryData%…)
    int reverbTarget = -1, chorusTarget = -1;  // "send to" mixer track (internal: insert − 1), −1 = none
    bool reverbBuiltIn = false, chorusBuiltIn = false;
    bool ignoreLinks = true, velocityFilter = true, emuEnvelope = true;
    int resampling = -1;            // 1 crunchy, 4 standard, 6 HQ, 8 extreme HQ (others: standard)
    int presetIndex = -1;           // v3+: into the font's preset list sorted by bank, program
    bool reverbLevel = true, chorusLevel = true; // the LEVEL check boxes (FL reads both from the first byte)
    int programMode = 0;
};
bool parseFlPlayerState (const std::vector<std::uint8_t>& state, FlPlayerState& out);
// LYNX Soundfont settings from FL's (the bank / program from the preset index when the font's presets are given)
std::vector<float> paramsFromFl (const FlPlayerState& fl, const std::vector<PresetInfo>& fontPresets);
// FL's control range (min, max) of its parameter P0…P12
std::pair<int, int> flParamRange (int flIndex) noexcept;
// FL's parameter P2…P12 at a raw value → (LYNX parameter, its value); nothing for P0 / P1 (not automatable in FL)
std::optional<std::pair<int, float>> paramFromFl (int flIndex, int raw) noexcept;
// FL automation (P index, FL's 0…1 over the control's whole range) → (LYNX parameter, its 0…1)
std::optional<std::pair<int, float>> automationFromFl (int flIndex, float flNormalised) noexcept;

// --- background loading ---------------------------------------------------------------------------------------------

// One font file being loaded or loaded: the audio thread only reads `font()` (null until ready).
class FontHandle
{
public:
    explicit FontHandle (std::string p) : path (std::move (p)) {}
    const Font* font() const noexcept { return ready.load (std::memory_order_acquire); }
    bool finished() const noexcept { return done.load (std::memory_order_acquire); }
    const std::string& filePath() const noexcept { return path; }
    std::string error() const;
    // blocks until the load finished (export / tests), at most `timeoutMs`
    bool wait (int timeoutMs = 60000) const;

private:
    friend class FontLibrary;
    void finish (std::shared_ptr<const Font> f, std::string err);
    const std::string path;
    std::shared_ptr<const Font> owner;
    std::atomic<const Font*> ready { nullptr };
    std::atomic<bool> done { false };
    mutable std::mutex lock;
    mutable std::string errorText;
};

// Process-wide cache of loaded fonts: one load per path, shared by every channel playing it, kept while used.
class FontLibrary
{
public:
    static FontLibrary& instance();
    // the handle for a file; starts its load on a background thread the first time (or when the file changed)
    std::shared_ptr<FontHandle> get (const std::string& path);
    // the same, loading on the calling thread (tests, command-line renders)
    std::shared_ptr<FontHandle> getNow (const std::string& path);
    // called (on the loader thread) after each load finishes, e.g. to refresh an open editor
    void setListener (std::function<void (const std::string& path)> f);

private:
    std::shared_ptr<FontHandle> obtain (const std::string& path, bool now);
    std::mutex lock;
    std::vector<std::pair<std::string, std::weak_ptr<FontHandle>>> cache;
    std::function<void (const std::string&)> listener;
};

// --- the instrument -------------------------------------------------------------------------------------------------

std::unique_ptr<Instrument> makeInstrument (std::shared_ptr<FontHandle> font);

// Test hooks: the number of voices sounding now (audio thread's view) of a LYNX Soundfont instrument, −1 otherwise
int activeVoices (const Instrument& instrument) noexcept;

// Writes a tiny valid SF2 into bytes: one preset per entry (name, bank, program), each with `layers` velocity layers
// over two key zones, sampled from generated sines / noise; for tests and the editor's demo. `loop` adds loop points.
struct TestPreset
{
    std::string name;
    int bank = 0, program = 0;
    int exclusiveClass = 0;
    bool loop = true;
};
std::vector<std::uint8_t> makeTestFont (const std::vector<TestPreset>& presets, int layers = 2, int sampleRate = 22050);
} // namespace lynx::sf
