#pragma once

// Reader for .flp (FL Studio) projects, written from scratch based on the behavioral
// analysis in docs/ANALISE_FL_STUDIO.md and tests with real files.
// Does not depend on JUCE so it can be tested in isolation.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace flp
{
struct Error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------------------
// Level 1: raw events

struct Event
{
    std::uint32_t id = 0;        // 1-byte ID, or 32-bit extended ID (escape 0xAC)
    std::uint32_t value = 0;     // u8/u16/u32 payload (empty for data events)
    std::span<const std::uint8_t> data; // variable-size payload (view over the buffer)

    // Size class: 0 = u8, 1 = u16, 2 = u32, 3 = data
    int sizeClass() const noexcept { return (int) ((id > 0xFF ? id >> 24 : id) >> 6); }
    bool isData() const noexcept { return sizeClass() == 3; }
};

struct Header
{
    std::uint16_t format = 0;    // 0 = project
    std::uint16_t channelCount = 0;
    std::uint16_t ppq = 96;
};

// Iterates over the events of a .flp buffer. The buffer must outlive the reader.
class EventReader
{
public:
    explicit EventReader (std::span<const std::uint8_t> file);

    const Header& header() const noexcept { return hdr; }

    // Returns false at the end of the data. Throws flp::Error if the file is truncated.
    bool next (Event& out);

private:
    std::span<const std::uint8_t> bytes;
    std::size_t pos = 0;
    Header hdr;
};

// ---------------------------------------------------------------------------
// Level 2: project model

enum class ChannelType : std::uint8_t
{
    sampler = 0,
    native = 1,
    instrument = 2,
    layer = 3,
    audioClip = 4,
    automationClip = 5,
    unknown = 255
};

// Point of an automation clip (event 0xEA). Positions in beats (quarter notes).
struct AutomationPoint
{
    double position = 0.0;  // absolute, with the file's deltas already summed
    double value = 0.0;     // 0..1
    float tension = 0.0f;   // -1..1
    std::uint8_t curve = 0; // 0 = single curve, 1 = double, 2 = hold, 10 = smooth...
};

// The rest of event 0xEA after the points (REA round 177b, FL's TACArticulator stream FUN_00fe8640): the header's LFO
// level and mode, the main envelope's tail and FL's automation clip LFO ("LFO mode"). The defaults are what FL writes
// for a clip whose LFO was never touched.
struct AutomationLfo
{
    bool present = false;     // the event had the whole layout (else the defaults)
    std::int32_t level = 64;  // header +4: LFO level -128..128
    std::uint8_t mode = 0;    // header +8: 0 = multiply ("Multiply" checked), 1 = add
    std::int32_t mainFlags = 1, mainEnabled = -1;
    std::array<std::int32_t, 3> mainLoop { -1, -1, -1 };     // decay / loop start / sustain points
    std::array<std::int32_t, 4> adsr { 128, 128, 0, 128 };   // the articulator's A / D / S / R scale knobs
    std::vector<AutomationPoint> points { AutomationPoint { 0.0, 1.0, 0.0f, 0 } }; // the LFO's amplitude points (≥ 1)
    std::vector<std::uint8_t> rawPoints; // the points' records as read (24 B each); written back when its size matches
    std::int32_t flags = 1;   // 1 tempo, 2 global phase, 4 unipolar tension, 8 frozen
    std::int32_t enabled = 0; // "LFO mode": 0 off, -1 on
    std::array<std::int32_t, 3> lfoLoop { -1, -1, -1 };
    std::int32_t speed = 45819, tension = 0, skew = 0, pulseWidth = 0;
    std::uint32_t phase = 0;
};

struct Channel
{
    int index = 0;
    ChannelType type = ChannelType::unknown;
    std::string name;            // name shown in the Channel Rack
    std::string pluginName;      // internal name of the generator (e.g. "FLEX")
    std::string samplePath;
    int mixerTrack = -1;
    bool enabled = true;         // 0x00: the channel's enabled switch in the Channel Rack (false: its notes don't play)
    std::vector<AutomationPoint> automation; // channels of type automationClip
    AutomationLfo automationLfo;             // the rest of their 0xEA (round 177b)

    // event 0xDB (channel "levels"): pan 0..12800 (6400 = center), volume 0..12800
    // (10000 = 78%, the default), pitch in cents
    std::uint32_t colour = 0; // 0xRRGGBB (0 = no stored colour)
    int filterGroup = -1;     // 0x91: index into Project::filterGroups (-1 = none)
    bool hasLevels = false;
    int pan = 6400, volume = 10000, pitchCents = 0;
    int modX = 256, modY = 0, filterType = 0; // channel filter (0xDB with 24 B; docs/analise/REA_rodada4.md §3)
    // event 0xE5 (channel "level adjustments", REA_rodada3 §2): pan adjustment ±6400 (0 = neutral) added to the pan,
    // volume adjustment 0..12800 (12800 = neutral) that scales the volume with the same curve: f(volume)·f(volAdj)
    bool hasLevelAdjust = false;
    int panAdj = 0, volAdj = 12800;

    std::vector<std::uint8_t> pluginState; // the generator's 0xD5 (e.g. 3x Osc state)
    int wrapperFlags = -1;                 // the generator's 0xD4 u16 at +16 (0x10 generator, 0x20 smart disable; −1 = none)

    // Layer channels (type layer, REA_rodada164a §1): 0x5E = a child channel's index (one per child), 0x90 = options
    std::vector<int> layerChildren;
    int layerFlags = 0;
    int layerFade = 0;                         // 0…256 (0xD7 +0x4C)
    int keyRegionLow = 0, keyRegionHigh = 256; // inclusive keys (0xD7 +0x44 / +0x48; 0…256 = every key)

    int swingMix = 128;          // 0x61: 0..128
    int cutGroup = 0, cutBy = 0; // 0x84
    int rootNote = -1;           // 0x87 (MIDI), -1 = default (C5 = 60)
    int fineTune = 0;            // 0x8E: Misc › Root note › Fine tune, cents (round 151)
    int maxVoices = 0;           // 0xDD: 0 = unlimited (with Mono off, the excess releases the oldest voices)
    int slideTime = 820;         // 0xDD +4: porta/slide time (0..1660; REA_rodada6 §4)
    bool monoFlag = false, portaFlag = false; // 0xDD +8: bit 0 Mono, bit 1 Porta

    // 0xDA: the 5 records, in order pan, volume, cutoff, resonance, pitch (68 B, i32)
    struct Envelope
    {
        bool present = false, enabled = false;
        int flags = 0, delay = 100, attack = 20000, hold = 20000, decay = 30000, sustain = 128, release = 20000;
        int amount = 0; // target: ±128 (cutoff/resonance/volume), ±64 (pan), ±2400 cents (pitch)
        // LFO (REA_rodada6 §2): flags 0x02 = tempo-synced, 0x20 = global
        int lfoDelay = 100, lfoAttack = 20000, lfoAmount = 0, lfoSpeed = 32950, lfoShape = 0;
        // the record as FL wrote it (first 68 bytes; rawSize = how many were present), for engines that read it whole
        std::array<std::uint8_t, 68> raw {};
        int rawSize = 0;
    };
    std::array<Envelope, 5> envelopes {};
    int envelopeCount = 0;

    // Misc tab (REA round 146b): 0x59 time shift (FL's time-knob value), 0xD1 echo delay, 0x8A echo mod X / Y,
    // 0xE4 velocity / key tracking, and the 0xD7 fields below
    int timeShift = 0;
    int echoFeedback = 0, echoPan = 6400, echoPitch = 0, echoes = 4, echoTime = 144; // time in 1/192 beat
    int echoModX = 128, echoModY = 128;
    struct Tracking
    {
        bool present = false;
        int mid = 0, pan = 0, modX = 0, modY = 0;
    };
    std::array<Tracking, 2> tracking {}; // [0] velocity (mid 100), [1] key (mid 60)
    int trackingCount = 0;
    int delayFlags = 0;            // 2 ping pong, 4 fat mode
    bool mainPitch = true, addToKey = false, arpSlide = false, fullPorta = true;
    int arpDirection = 0, arpRange = 1, arpChord = -1, arpTime = 1024, arpGate = 48, arpRepeat = 1;
    int timeGate = 1447;           // time-knob value; 1446 and above = off
    int pitchRange = 2;

    // 0xD7: time-stretch
    std::uint32_t stretchTimeRaw = 0; // in 1/192 of a beat: int or float, depending on Project::flagsA9
    int stretchPitch = 0;             // Time stretching › PITCH (cents); FL ignores it in Resample mode (REA_rodada164e-clippitch)
    int stretchMul = 0;               // factor = 2^(v/10000)
    int stretchMode = 0;              // 0 resample, < 0 real-time stretch, >= 1 offline
    float stretchTime = 0.0f;         // already decoded (1/192 of a beat)
    double sampleStart = 0.0, sampleEnd = 1.0, startOffset = 0.0; // fractions of the sample

    // Sampler › Precomputed effects and sample flags (REA round 167h §1): 0x46 bits (0 fade stereo, 1 reverse, 2 clip,
    // 8 swap stereo), 0x4C fade in / 0x4B fade out (0…1024), 0xD7 remove DC (0x09), normalize (0x50), reverse polarity
    // (0x51), declicking mode (0x53), crossfade (i32 0x54, 0…2048) and trim (i32 0x58, 0…32768); 0x8F sample flags
    // (bit 3 = use loop points; −1 = not saved), 0x13 (older files: use loop points), 0x14 ping pong loop
    int fxFlags = 0, fadeIn = 0, fadeOut = 0;
    bool removeDc = false, normalize = false, reversePolarity = false;
    int declickMode = 0, crossfade = 0, trim = 0;
    int sampleFlags = -1;
    bool loopPointsOld = false, pingPong = false;
    // 0xFB (round 190dh): the sampler's loop record, saved by FL only when its loop START / END knobs moved the loop
    // (FUN_01006f00 / FUN_01006d20): i32 version 2, i64 loop start, i64 loop end (frames, inclusive; −1 / INT64_MAX =
    // none), 21 bytes FL keeps unset (double −1, i32 −1, u8 0, i32 / i32 INT32_MAX in FL's own files), u8 use loop points,
    // u8 0. −1 here: no record.
    std::int64_t loopStart = -1, loopEnd = -1;
    // the legacy precomputed effects (REA round 167l §1): 0x4A boost, 0x45 EQ ("FX"), 0x83 ring mod mix | freq << 16,
    // 0x55 stereo delay, 0x47 / 0x53 low-pass cutoff / resonance, 0x8B reverb | type << 16, 0x56 pogo ("FX3"); FL's
    // defaults when absent
    int boost = 0, eq = 128, ringMix = 0, ringFreq = 128, stereoDelay = 2048, cutoff = 1024, resonance = 0;
    int reverb = 0, reverbType = 1, pogo = 256;
    // 0x20: "Lock active/mute state" (the channel keeps its state when another one is soloed; REA round 167l §5)
    bool soloLock = false;
    // 0x32: the channel's notes are shown as ghosts in the Piano Roll (FL's ghost channels menu; 1 = shown, FL's default)
    bool ghostShown = true;
    // 0x9B: the channel's icon (FL's icon index, 0 = none; the loader stores it in the channel's +0x7c, REA round 168j)
    int icon = 0;
    bool useLoopPoints() const noexcept { return sampleFlags >= 0 ? (sampleFlags & 8) != 0 : loopPointsOld; }

    // Seconds for an envelope time value (docs/analise/REA_rodada3.md §1.4). With sync (flags & 1), in beats.
    static double envelopeTime (int v) noexcept;
};

// Address of an automatable parameter (FL's controlID), already in the current format:
//  < 0x10000000: channel (id >> 16), parameter (id & 0xFFFF)
//  0x70000000 + (track << 22) + (slot << 16) + p: mixer
struct ControlId
{
    std::uint32_t raw = 0;

    bool isChannel() const noexcept { return raw < 0x10000000u; }
    bool isMixer() const noexcept { return raw >= 0x70000000u; }
    int channel() const noexcept { return (int) (raw >> 16); }
    int track() const noexcept { return (int) ((raw - 0x70000000u) >> 22); }
    int slot() const noexcept { return (int) ((raw >> 16) & 0x3F); }
    int param() const noexcept { return (int) (raw & 0xFFFF); }

    static constexpr int mixerVolume = 0x1FC0;
    static constexpr int mixerPan = 0x1FC1;
    static constexpr int mixerStereo = 0x1FC2; // separation ±64
    static constexpr int mixerEqBase = 0x1FD0;  // track EQ: + group·8 + band (REA_rodada6 §1)
    static constexpr int mixerSendBase = 0x2000; // + destination track
    static constexpr int pluginParamBase = 0x8000;
    static constexpr std::uint32_t tempo = 0x40000005; // tempo automation: 0..1 = 60..180 BPM (checked against the demos)

    bool isTempo() const noexcept { return raw == tempo; }
};

// Stored value of a parameter (event 0xE1).
struct ControlValue
{
    ControlId id;
    std::int32_t value = 0;
};

// A pattern event (event 0xDF, 12 B records {u32 tick, u32 controlID, i32 value}): the target's raw value
// (what FL's parameter setter stores, e.g. 0..12800 for a channel volume), held from `tick` (file PPQ) on.
// Same record for the "initialized controls" of event 0xD8 (reset when the song starts). REA_rodada85 §1.
struct ControlEvent
{
    std::uint32_t tick = 0;
    ControlId id;
    std::int32_t value = 0;
};

// Links an automation clip (channel) to a parameter (event 0xE3).
struct AutomationLink
{
    int channel = 0;
    ControlId target;
};

// Links a controller's output (Peak Controller in a mixer slot, Envelope Controller in a channel...)
// to a parameter (same event 0xE3, with the source pointing to plugin "parameter" >= 0x8000;
// REA_rodada14 §7). `source.param() - 0x8000` = output number.
struct ControllerLink
{
    ControlId source;
    ControlId target;
};

struct Note
{
    std::uint32_t position = 0;  // in ticks of the file's PPQ
    std::uint32_t length = 0;    // 0 = step sequencer note (no length of its own)
    std::uint16_t channel = 0;   // index in the Channel Rack
    std::uint16_t key = 60;      // 60 = C5 in FL
    std::uint16_t flags = 0;
    std::uint16_t group = 0;     // note group (FL's Group, Shift+G); 0 = none
    std::uint8_t finePitch = 120; // 120 = center
    std::uint8_t release = 64;
    std::uint8_t pan = 64;       // 0..128, 64 = center
    std::uint8_t velocity = 100; // 0..128
    std::uint8_t modX = 128;
    std::uint8_t modY = 128;
    std::uint8_t extra = 0;      // bits 0-3 colour/MIDI channel; 0x10 porta; 0x20 muted; 0x80 selected
    std::uint8_t repeats = 0;

    bool muted() const noexcept { return (extra & 0x20) != 0; }
    bool porta() const noexcept { return (extra & 0x10) != 0; }
    bool slide() const noexcept { return (flags & 0xF) == 8; }
};

// Playlist time marker: 0x94 (u32: type << 24 | position in ticks), followed by
// 0x21/0x22 (numerator/denominator) and 0xCD (name). Type 8 = time signature change (seen in the demos:
// 7/4, 6/4, 3/4); 0 = ordinary marker; the others are actions (loop, pause...).
struct TimeMarker
{
    std::uint32_t position = 0;
    int type = 0;
    int numerator = 4, denominator = 4;
    std::string name;
};

struct Pattern
{
    int index = 0;
    std::string name;
    std::optional<std::uint32_t> colour; // 0xRRGGBB (stored in the file as 0x00BBGGRR)
    std::vector<Note> notes;
    std::vector<ControlEvent> controlEvents; // event 0xDF: pattern events (event editor), file order
    // FL's Channel Rack looping (FL 20.5+, REA_rodada165f-rack §1): 0x1A u8 on, 0xA2 u32 type (1 step channels, 2 all
    // channels, 3 advanced), then 0xA0 u32 channel / 0xA1 i32 loop point pairs (0 none, -1 step, -2 beat, -3 bar, N steps)
    bool loopOn = false;
    int loopMode = 1;
    std::vector<std::pair<int, int>> channelLoops; // (FL channel index, setting)
    // the Piano roll's pattern time markers (round 168g): the same 0x94 / 0x21 / 0x22 / 0xCD events as the
    // Playlist's, written between the pattern's 0x41 and the next pattern / channel / playlist section
    std::vector<TimeMarker> markers;
    // 0xA4: the Channel Rack's pattern length selector (pattern +0x44, FL's PatternLengthSelectChange; REA round 168j):
    // the pattern's length in ticks of the file's PPQ, 0 = automatic
    std::uint32_t fixedLength = 0;
};

struct PlaylistItem
{
    enum class Kind { pattern, channelClip };

    std::uint32_t position = 0;  // ticks
    std::uint32_t length = 0;    // ticks
    int track = 0;               // 0 = first playlist track
    Kind kind = Kind::pattern;
    int index = 0;               // pattern index, or channel index (audio/automation clip)
    std::uint16_t flags = 0;     // +0x12: 0x40 by default; see mutedFlag
    // +0x12 bit 13: the clip is muted (the Mute tool / clip menu › Mute). FL keeps the record in memory as it is in the
    // file and tests bit 0x20 of byte +0x13 (REA round 182e)
    static constexpr std::uint16_t mutedFlag = 0x2000;
    static constexpr std::uint16_t defaultFlags = 0x40;
    bool muted() const noexcept { return (flags & mutedFlag) != 0; }

    // The same 8 bytes mean different things per clip type:
    //  - pattern: two int32 in ticks (start/end of the pattern section); -1 = no trimming
    //  - channel clip (audio/automation): two floats
    std::int32_t patternStartTick = -1;
    std::int32_t patternEndTick = -1;
    float startOffset = 0.0f;
    float endOffset = 0.0f;

    // Audio clips, records of 60 B and more (FL 20.99+): fades in milliseconds and the clip gain (linear).
    float fadeInMs = 0.0f;
    float fadeOutMs = 0.0f;
    float fadeInTension = 0.0f, fadeOutTension = 0.0f; // FL's raw fade curve values (+0x28, +0x30; 0 = its S curve)
    // +0x38 (FL's own bits, REA round 180d): bit 0 / 1 the fade-in / -out is manual, >> 2 & 7 / >> 14 & 7 their declick
    // choice, >> 8 & 3 / >> 12 & 3 their shape (1 = linear, else the S curve). The writer sets bit 0 / 1 for a fade it writes.
    std::uint32_t fadeFlags = 0;
    float gain = 1.0f;
    // Audio clips, records of 80 B and more: FL 2026's clip pitch (semitones) and clip reverse (round 167k).
    float pitch = 0.0f;
    bool reversed = false;
    double stretch = 1.0; // FL 2026's per-clip stretch (+0x40 double; round 167l)
};

struct PlaylistTrackInfo
{
    int index = 0;        // 0 = first track
    std::string name;
    bool enabled = true;  // false = track muted
    std::uint32_t colour = 0x485156; // 0xRRGGBB (stored as 0x00BBGGRR); 0x485156 = FL's default grey
    float heightScale = 1.0f; // the track's size (FL's Size menu: 0.33 .. 2.0; 1 = 100 %)
    bool grouped = false;     // "Group with above track"
    bool expanded = true;     // a group header's members are shown (false = "Collapse grouped tracks")
    // FL's track locks (REA round 168f; track record +0x14 / +0x19): i32 at 17 = "Lock to this size" (> 0 locked at that
    // many px, else not locked — FL keeps −the last size), u8 at 21 = "Lock to content"
    int lockedHeight = -1;
    bool lockedToContent = false;
    // track mode (REA round 146a): u32 at 49 (0 none, 1 audio track, 3 instrument track), i32 mixer track at 53, i32
    // FL channel index at 57 (-1 = none)
    int mode = 0, mixerTrack = -1, channel = -1;
    int icon = 0;          // u32 at 8: FL's icon index (0 = none)
    // Performance settings (round 165m, bytes 22…45, FL's menu numbers): u32 motion, press, trigger sync, queue,
    // tolerant, position sync. FL's defaults: Stay, Retrigger, 4 beats, no queue, tolerant, position sync off.
    int motion = 0, press = 0, triggerSync = 5, positionSync = 0;
    bool queue = false, tolerant = true;
};

struct Arrangement
{
    int index = 0;
    std::string name;
    std::vector<PlaylistItem> items;
    std::vector<PlaylistTrackInfo> tracks; // only the ones the file describes
    std::vector<TimeMarker> markers;
};

struct MixerInsert
{
    int index = 0;
    std::string name;
    std::uint32_t flags = 0x4C;  // 0x08 = enabled (cleared = muted), 0x1000 = solo
    std::uint32_t colour = 0;    // 0xRRGGBB (0 = no stored colour)
    std::vector<int> routes;      // destination tracks (0 = Master)
    bool hasRoutes = false;
    int output = -2;              // 0x93 "Audio output target": -1 = none, 0 = the device's first pair; -2 = not in the file
    int input = -1;               // 0x9A "Audio input source": -1 = none; k = entry k of FL's list for its device (stereo
                                  // pairs first, then every channel alone: REA round 157c)
    float latencyMs = 0.0f;       // 0xEC f32 at +0: the manual track latency in ms (REA round 165f-mixer)
    int latencyBypass = 0;        // 0x31: Bypass track latency compensation 0 Never / 1 Always / 2 When recording or not playing
    int recordSource = -1;        // −1 = no 0xA5 event; 0xA5: 0 External input only, 1 + mixer input, 2 Post effects, 3 Post EQ, 4 Post level/pan, 5 Post track
    // 0xA6 "Monitor external input" (track +0x30, FL's ExtInputMonitorOffMenuClick tags; REA round 168j): 0 Off, 1 When armed,
    // 2 Auto, 3 On; -1 = not in the file
    int monitor = -1;
    int icon = 0;                 // 0x5F: FL's icon index (track +0x14; 0 = none)
    // 0x2A: the track's "own colour" flag (track +0x00; the loader's 0x95 sets it to colour != default, 0x2A then
    // overrides it; FL draws the default colour when it is off). -1 = not in the file
    int colourSet = -1;

    struct Slot
    {
        int index = 0;      // 0..9 (0x62; files without it, FL <= 11: the slot field of the plugin's 0xD4)
        std::string plugin; // internal name (0xC9), e.g. "Fruity Limiter"
        std::vector<std::uint8_t> state; // the plugin's 0xD5 (e.g. Fruity Wrapper blob)
        // the plugin's 0xD4 u16 at +16 (wrapper flags, REA round 139): 0x20 = "Smart disable" (−1 = not in the file)
        int wrapperFlags = -1;
    };
    std::vector<Slot> slots;
};

struct Project
{
    Header header;
    std::string version;
    std::uint32_t versionNumber = 0; // major*1e6 + minor*1e3 + patch
    double tempo = 140.0;
    int beatsPerBar = 4; // event 0x11 (the first one, the project's)
    int swing = 0;       // event 0x0B: 0..128
    int currentPattern = 0;     // event 0x43: FL's pattern number selected when saved (1…999; 0 = not in the file)
    int songMode = -1;          // event 0x09: 1 = SONG, 0 = PAT (−1 = not in the file)
    bool showInfo = false;      // event 0x0A: Project info › Show on opening
    int currentArrangement = -1; // event 0x64: index (0x63) of the arrangement open when saved (−1 = not in the file)
    int mainPitch = 0;
    bool playTruncatedNotes = false; // event 0x1E (FL writes it only when on)
    // events 0x1D / 0x27: the mixer menu's Plugin delay compensation › "Automatic" (FL's AutoPDCMenuClick; off = the PDC
    // planner FUN_010ff3b0 ignores the plugins' latencies, the manual track latencies stay) and "Compensate automations"
    // (CompensateAutomationsMenuClick; automation sent to a delayed plugin is delayed with its audio). REA round 168j
    bool autoPdc = true, compensateAutomations = true;
    bool timeSignatureMode = false; // event 0x23 = 1: Options › Project › Time settings "Set as time signature" (TimeSettingComboChange)
    int mixerTrackCount = 0;        // event 0x67: the mixer track records saved (Master + inserts + FL's "current"); 0 = not in the file
    // event 0xF3 (2 × i32, the Piano Roll's View › scale highlighting, FL's EEMenuScaleClick): the scale (≥ 0 = FL's
    // "Reference scales" list in alphabetical order; −1 Default, −2 Black notes, −3 Automatic) and its root (0 = C … 11 = B)
    int scaleHighlight = -1, scaleRoot = 0;
    bool hasScaleHighlight = false;
    std::string dataFolder; // event 0xCA: Options › Project › "Data folder" (ProjDataPathBtnClick)
    bool fastDeclick = false;        // event 0x28: Options › Project › "Fast declick for cut groups" (REA round 167h §3)
    // event 0xED (2 × f64 Delphi TDateTime, days since 1899-12-30): the creation date and the working time in days
    double createdDays = 0.0, workDays = 0.0;   // event 0x50 (i16): the main pitch knob in cents (FL saves it in FUN_00f40840, REA round 167g)
    bool performanceMode = false; // event 0x1F (round 165m)
    int panLaw = 0;      // event 0x17: 0 = FL's circular (sin/cos) pan law, 2 = triangular (REA_rodada76 §9)
    std::uint32_t flagsA9 = 0;
    bool autoCrossfades = false; // event 0x2C (FL 21+); files without it load with it off, as in FL
    std::string title, artist, genre, url;
    std::string comments; // event 0xC3 (the Info page's comments; line breaks saved as CR)

    std::vector<Channel> channels;
    std::vector<std::string> filterGroups; // 0xE7: the Channel Rack's filter groups, in order
    std::vector<Pattern> patterns;
    std::vector<Arrangement> arrangements;
    std::vector<MixerInsert> mixer;
    std::vector<ControlValue> controls;       // volumes, pans, send levels...
    std::vector<ControlEvent> initControls;   // event 0xD8: values reset when the song starts
    std::vector<AutomationLink> automationLinks;
    std::vector<ControllerLink> controllerLinks;

    std::vector<std::string> warnings; // ignored or suspicious things, for diagnostics

    const Pattern* findPattern (int index) const noexcept;
    const Channel* findChannel (int index) const noexcept;
};

// `channelPreset`: FL channel preset file (.fst): the events belong to a single channel (index 0),
// without the new-channel event.
Project parseProject (std::span<const std::uint8_t> file, bool channelPreset = false);

// FL mixer track preset (.fst of Data\Patches\Mixer presets and Documents\...\Presets\Mixer presets, REA round
// 167q): one insert, as `mixer[1]` (mixer[0] is an empty Master), its controls moved to track 1.
Project parseMixerPreset (std::span<const std::uint8_t> file);
// The events of an FL mixer track preset (an insert's events, no channel or pattern); false for other .fst / bad data.
bool looksLikeMixerPreset (std::span<const std::uint8_t> file);

// The FL version that saved a project ("21.2.3.4060"), read from the start of the file only (event 0xC7 comes among
// the first events; the Browser's details box, REA round 139). "" when the bytes are not an FL project / no version.
std::string readSavedVersion (std::span<const std::uint8_t> head);

// Utilities exposed for tests
std::uint32_t parseVersionNumber (const std::string& version);
// `flagsA9`: the file flags event 0xA9 (0 when absent); it decides between the 60, 80 and 88 B layouts.
std::size_t playlistRecordSize (std::uint32_t versionNumber, std::size_t payloadSize, std::uint32_t flagsA9 = 0);
// Converts a controlID from old files (class 0x2..., sends at 0x1F40+d) to the current format.
ControlId normalizeControlId (std::uint32_t raw);
} // namespace flp
