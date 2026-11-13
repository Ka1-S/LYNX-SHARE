#pragma once

// FL Studio Mobile project files (.flm, also .songtmpl templates): the file's structure as data, round 182c (format:
// docs/analise/FLM_FORMAT.md). Clean-room: written from that page's description of the layout; no Image-Line code
// was used. Nothing here knows LYNX's Song; core/FlmImport turns a Project into one.
//
// A file is "10LF" followed by little-endian chunks (4-byte tag, u32 body size, body). Containers inside start with a
// versioned 4-byte magic ("10KR" rack, "20HC" channel, "20LC" clip, "20LS" sample) and hold chunks of their own. Fields
// added by newer app versions sit at the end of a chunk: every reader stops at the first field the chunk is too short
// for, so older and newer files both load.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lynx::flm
{
constexpr int ticksPerBeat = 128; // clip starts and event times (FLM_FORMAT §4)

// A sample reference (a drum-kit sample, an audio clip's sample): FLM_FORMAT §5–6
struct SampleRef
{
    std::string name;      // MAIN: the sample's display name
    std::string root;      // PTH1: "@L@" (the user's library) or "@R@" (the app's resources); empty = no PTH1
    std::string path;      // PTH1: relative to the root, '/' separated
    std::string savedPath; // PRST: the absolute path on the device that saved the file
    std::string relName;   // PRST: the relative name
    int stretchMode = 0;   // STR1
    double stretch = 1.0;  // STR1: time-stretch ratio
    double pitch = 0.0;    // STR1: semitones
    std::vector<float> params; // PRMS: the slot's eleven parameters, see samplepad (core/FlmPad.h has their laws)
    // MAIN's tail (PROVEN-REA, round 190bs: the sample writer 0x18074a1c0 and the drum sampler's trigger 0x1807503a0)
    bool reverse = false;     // the slot plays its sample backwards
    double timeShift = 0.0;   // beats added to every drum note of the slot (never before the clip's start)
    int cutGroup = 0;         // 0 = none, 1…3 = "Cut group A / B / C": a hit silences the group's other slots
    bool polyphonic = true;   // false: a new hit silences the slot's own voices
    // LINk: the slot's id in its drum set and the channel it plays into (−1 = its own rack)
    int slotId = -1, outputChannel = -1;
    bool legacyStretch = false; // the stretch and pitch came from STRC (no STR1)

    float param (int index, float fallback) const noexcept { return index >= 0 && index < (int) params.size() ? params[(std::size_t) index] : fallback; }
};

// A sample slot's parameters (sample PRMS; the names PROVEN-REA 0x180748680, the defaults 0x180748120)
namespace samplepad
{
constexpr int level = 0, pan = 1, speed = 2, mute = 3, solo = 4, length = 5, egAttack = 6, egDecay = 7, egSustain = 8, egRelease = 9, start = 10, count = 11;
constexpr float defaults[count] = { 0.853125f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.5f };
} // namespace samplepad

// One EVN2 record (20 bytes): FLM_FORMAT §4
struct Event
{
    std::uint32_t tick = 0;  // inside the clip, ticksPerBeat per beat
    double length = 0.0;     // beats
    int key = 0;             // MIDI key, kit sample index on drum lanes; < 0 on automation lanes
    std::uint8_t velocity = 200; // byte 14: the module's note-on velocity = byte / 255 (PROVEN-REA, round 190u)
    std::uint8_t pan = 127;      // byte 15: pan = byte / 255 (PROVEN-REA; 127 is the editors' centre)
    // (automation lanes and audio clips: |key| − 1 is the parameter the point moves, a negative key marks a point FLM
    // computed between two control points; PROVEN-REA, round 190bs: the song scheduler 0x180770330)
    // bytes 16–17: automation value 0…65535; notes: pitch p = u16 / 65534, 0x7FFF = centre — a drum pad plays at
    // 2^(2p − 1) of its speed, note lanes do not hand it to the module (PROVEN-REA, round 190u)
    std::uint16_t value = 0x7FFF;
    std::int8_t repeat = 0;      // byte 18: n > 0 = n more hits of the note inside its 1/16 step (PROVEN-REA, round 190ab)
    std::uint8_t slide = 0;      // byte 19: ≠ 0 = a slide note (note lanes): glides the held note, starts none
};

struct Clip
{
    std::uint32_t startTick = 0;
    double length = 0.0;       // CLHd: length on the timeline, beats (0 = one loop)
    double loopLength = 0.0;   // CLHd: the content repeats every this many beats
    double offset = 0.0;       // CLHd: start offset into the content / sample, beats
    std::uint32_t contentId = 0; // CLHd: clips of one lane with the same id share their content
    double sourceLength = 0.0; // CLHd: the whole source in beats (audio: the sample at the project tempo)
    std::uint8_t flag = 0;
    std::vector<Event> events;
    std::optional<SampleRef> sample; // CLSm (audio clips)

    // the clip's length on the timeline (a 0 length = one loop)
    double timelineLength() const noexcept { return length > 0.0 ? length : loopLength; }
};

enum class LaneKind : int { notes = 0, automation = 1, audio = 2, drums = 3, other = -1 };

struct Lane
{
    std::uint32_t rawKind = 0;
    int targetModule = -1; // automation: the module instance id (0 = the rack itself)
    int targetParam = -1;  // automation: the parameter index
    std::string name;
    double swing = 0.0;    // DESc f64 at 12: the step track's swing 0…1 (odd 1/16 steps start swing / 8 of a beat late)
    std::vector<Clip> clips;

    // a drum lane whose notes are held (PROVEN-REA, round 190bs: the scheduler passes a drum note's end to the pad
    // only with this flag, the lane's second kind byte; without it every hit is a one-shot)
    bool holdsNotes() const noexcept { return ((rawKind >> 8) & 1u) != 0; }

    LaneKind kind() const noexcept
    {
        switch (rawKind & 0xFF) // (one file has 0x103: a drum lane with a flag in the high byte)
        {
            case 0: return LaneKind::notes;
            case 1: return LaneKind::automation;
            case 2: return LaneKind::audio;
            case 3: return LaneKind::drums;
            default: return LaneKind::other;
        }
    }
};

// Rack and channel kinds (FLM_FORMAT §3–4)
constexpr std::uint32_t kindMaster = 0x000, kindBus = 0x080, kindNormal = 0x100;

struct Channel
{
    std::uint32_t kind = kindNormal;
    std::string name;      // CHHD's display name
    std::string typeName;  // "DW Sampler 3", "Audio 4"
    std::string userName;
    double order = 0.0;    // display order
    int channelId = -1;
    std::vector<std::vector<std::uint32_t>> sends; // SEND chunks, raw 32-bit words (F2)
    std::vector<Lane> lanes;
};

struct Module
{
    std::uint32_t type = 0;
    std::uint32_t instanceId = 0;
    bool bypassFlag = false; // ADD1 byte 0 (INFERRED: bypass)
    int keyLow = 0, keyHigh = 127;
    std::string descName, descFolder;
    std::string presetFile; // PRST: the preset or sample file (absolute, as saved)
    std::string presetName; // PRST: its display / relative name
    std::vector<float> params;    // PRMS
    std::vector<float> reserved;  // SMPR: Post Gain, Mix, Wet Pan (FLM's parameter ids 0x7FC0…0x7FC2, also −3, −2)
    std::vector<std::uint8_t> custom; // CSTM body (the engine's own state)
};

struct Rack
{
    std::uint32_t kind = kindNormal;
    std::vector<float> params; // RPRM: the rack's own parameters, see rackparam (FLM_FORMAT §3)
    std::vector<std::uint8_t> pads; // RSMP's PADS
    std::vector<SampleRef> kit;     // RSMP's samples (drum lanes play kit[key])
    std::vector<Module> modules;    // in slot order
    // --- round 190dw item 4: CCLK, the rack's controller links (FLM_FORMAT §3: writer 0x18059af42, lookup 0x18059fa60,
    // the base CC handler 0x180799210): a MIDI CC n sets parameter `param` of module instance `module` to its value
    struct CcLink
    {
        int cc = 0, module = 0, param = 0;
    };
    std::vector<CcLink> ccLinks;
    // --- end of round 190dw item 4
};

struct Project
{
    std::string title;
    double tempo = 120.0;
    int numerator = 4, denominator = 4; // TDIV: beats per bar, the grid's steps per beat (also the modules' tick rate, 384 / it)
    std::uint32_t root = 0, scale = 0; // the "key locker" (FLM_FORMAT §2): HEAD +344 root 0..11, +348 FLM's scale index
    std::string appVersion; // "5.0.4"
    std::string created, saved, author, originalName;
    std::string chordSetJson; // KEYB
    std::vector<Rack> racks;
    std::vector<Channel> channels; // channels[i] belongs to racks[i]
    int unknownChunks = 0;         // top-level chunks this reader skipped
};

// True when the bytes start like an FLM project ("10LF" and a first chunk that fits).
bool isFlmData (const std::uint8_t* data, std::size_t size) noexcept;

// The whole project, or nullopt with `error` (not an FLM file, or a chunk that runs past the end of the file).
std::optional<Project> parse (std::span<const std::uint8_t> bytes, std::string* error = nullptr);

// Module registry (FLM_FORMAT §7)
enum class ModuleCategory : int { unknown = 0, generator = 1, delayReverb = 2, dynamics = 3, eqFilter = 4, modulation = 5, noteEffect = 6 };
const char* moduleName (std::uint32_t type) noexcept; // nullptr for an unregistered id
ModuleCategory moduleCategory (std::uint32_t type) noexcept;
inline bool isGenerator (std::uint32_t type) noexcept { return moduleCategory (type) == ModuleCategory::generator; }

// The rack's own parameters (module instance 0): RPRM holds the first six; automation lanes on module 0 use the same
// indices, plus MIDI CCs and the per-destination send switches/levels (FLM_FORMAT §3, PROVEN-REA 0x18059eb20)
namespace rackparam
{
constexpr int level = 0, pan = 1, mute = 2, solo = 3, pitchBend = 4, aftertouch = 5;
constexpr int firstMidiCc = 7, lastMidiCc = 7 + 126;
constexpr int sendEnableBase = 0x1000, sendLevelBase = 0x2000; // + the destination's channel id
} // namespace rackparam

// Module type ids the importer refers to by name
namespace module
{
constexpr std::uint32_t miniSynth = 0, dwSampler = 1, gmSynth = 2, transistorBass = 19, superSaw = 23, slicer = 33, threeOsc = 39,
                        soundFont = 52, tuner = 24, analyzer = 26;
}
} // namespace lynx::flm
