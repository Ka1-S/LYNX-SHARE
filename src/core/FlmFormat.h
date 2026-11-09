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
    std::vector<float> params; // PRMS
};

// One EVN2 record (20 bytes): FLM_FORMAT §4
struct Event
{
    std::uint32_t tick = 0;  // inside the clip, ticksPerBeat per beat
    double length = 0.0;     // beats
    int key = 0;             // MIDI key, kit sample index on drum lanes; < 0 on automation lanes
    std::uint8_t velocity = 200; // byte 14 (INFERRED: velocity 0…255)
    std::uint8_t pan = 127;      // byte 15 (INFERRED: 127 = centre)
    std::uint16_t value = 0x7FFF; // bytes 16–17: automation value 0…65535 (notes: INFERRED fine pitch, 0x7FFF = centre)
    std::uint8_t extra0 = 0, extra1 = 0; // bytes 18–19
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
    std::vector<Clip> clips;

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
    std::vector<std::uint8_t> custom; // CSTM body (the engine's own state)
};

struct Rack
{
    std::uint32_t kind = kindNormal;
    std::vector<float> params; // RPRM: the rack's own parameters, see rackparam (FLM_FORMAT §3)
    std::vector<std::uint8_t> pads; // RSMP's PADS
    std::vector<SampleRef> kit;     // RSMP's samples (drum lanes play kit[key])
    std::vector<Module> modules;    // in slot order
};

struct Project
{
    std::string title;
    double tempo = 120.0;
    int numerator = 4, denominator = 4;
    std::uint32_t scale = 0, root = 0; // the "key locker" (FLM_FORMAT §2)
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
