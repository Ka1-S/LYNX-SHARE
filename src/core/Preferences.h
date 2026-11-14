#pragma once

// The logic behind the Settings window's General, Audio and MIDI pages and the Plugin manager (FL Studio's
// Settings pages and plugin manager, interpreted). No JUCE, so it can be tested in isolation: the UI reads and
// writes the files and devices, these functions decide what the settings mean.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace lynx::prefs
{
// --- "key=value" settings files (ui-options.txt, midi-settings.txt) -------------------------------------------------
// Lines "key=value" (spaces around both trimmed); blank lines and lines without '=' are ignored, the last of a
// repeated key wins.
std::map<std::string, std::string> parseKeyValues (const std::string& text);

// --- autosave (General page) ------------------------------------------------------------------------------------
// The choices for "Save a backup copy every": 0 = never, else minutes.
const std::vector<int>& autosaveIntervals();
int clampAutosaveMinutes (int minutes); // the nearest choice
constexpr int defaultAutosaveMinutes = 2;
constexpr int defaultAutosaveKeep = 10;
int clampAutosaveKeep (int keep); // 1..100
// Round 177d (an FL wishlist item: a backup written while playing made the sound stutter; the user's decision): with
// "waitForQuiet" on (the default) a due backup also waits while the song plays and until `quietMs` after the last
// MIDI input, not only while recording (FL postpones it for recording only).
constexpr double backupQuietMs = 5000.0;
// The user's decision: after `longestWaitMs` without a backup one is written anyway, playing or not (Settings › General,
// 10 minutes by default). Never during a recording.
constexpr int defaultBackupLongestWaitMinutes = 10;
constexpr int backupLongestWaitChoices[] = { 5, 10, 15, 20, 30, 60 };
constexpr int clampBackupLongestWait (int minutes) noexcept
{
    auto best = defaultBackupLongestWaitMinutes;
    auto bestGap = 1 << 30;
    for (const auto c : backupLongestWaitChoices)
        if (const auto gap = c > minutes ? c - minutes : minutes - c; gap < bestGap)
        {
            best = c;
            bestGap = gap;
        }
    return best;
}
inline bool backupMayRun (bool recording, bool playing, double msSinceMidi, bool waitForQuiet, double msSinceBackup,
                          double longestWaitMs) noexcept
{
    if (recording)
        return false;
    if (msSinceBackup >= longestWaitMs)
        return true;
    return ! waitForQuiet || (! playing && msSinceMidi >= backupQuietMs);
}
// A MIDI message that means someone is playing: channel messages only. Clock, active sensing and the rest of the
// system messages (0xF0 and up) run by themselves whenever a device is plugged in.
constexpr bool midiMeansPlaying (unsigned char status) noexcept { return status >= 0x80 && status < 0xF0; }

// Backup file name (without extension) of project `stem` saved at a date/time: "Night Drive (2026-10-16 19.50.03)".
// Sorting these names sorts them by time.
std::string backupName (const std::string& stem, int year, int month, int day, int hour, int minute, int second);
// True when `name` (without extension) is a backup of `stem` made by backupName, or the plain "stem" of older versions.
bool isBackupOf (const std::string& name, const std::string& stem);
// The backups of `stem` among `names` that go when only the `keep` newest are kept (oldest first).
std::vector<std::string> backupsToRemove (const std::vector<std::string>& names, const std::string& stem, int keep);

// --- undo levels (General page) ---------------------------------------------------------------------------------
constexpr int defaultUndoLevels = 100;
int clampUndoLevels (int levels); // 10..1000

// --- audio (Audio page) -----------------------------------------------------------------------------------------
// The latency one buffer adds, in milliseconds.
double bufferLatencyMs (int bufferSamples, double sampleRate);

// The buffer a fresh install starts with (REA round 87): about 10.7 ms (512 samples at 44.1/48 kHz, 1024 at 88.2/96 kHz)
// so heavy projects do not crackle out of the box. The smallest offered size at or above that; the current size if it
// is already larger; the largest offered one if none reaches it. 0 when nothing is offered.
int safeBufferSize (const std::vector<int>& available, int current, double sampleRate);

// What kind of driver a JUCE device type name is (REA round 97). Windows offers three WASAPI modes:
//  shared        - other programs keep their sound; Windows mixes every stream at its engine period (10 ms on most
//                  devices), so a smaller buffer cannot lower the latency, it only adds bursts;
//  lowLatency    - shared, but asks Windows for a shorter engine period (IAudioClient3); only drivers that support it
//                  go below 10 ms (many USB class-driver devices stay at 480 samples);
//  exclusive     - LYNX alone on the device (other programs go silent); the device's own minimum period (often 3 ms).
enum class DriverKind
{
    wasapiShared,
    wasapiLowLatency,
    wasapiExclusive,
    directSound,
    asio,
    lynxAsio, // LYNX ASIO loaded by LYNX itself, without registration (REA round 106)
    coreAudio, // macOS
    alsa,      // Linux
    jack,      // Linux (JACK, or PipeWire through pipewire-jack)
    pipewire,  // Linux: LYNX's native PipeWire client (round 161)
    other
};
DriverKind driverKind (const std::string& typeName);

// The buffer sizes worth offering for a device of that kind. In classic shared mode the sizes below the engine period
// `devicePeriod` (the device's default buffer) are dropped: Windows still mixes in whole periods, so they would not
// lower the latency and only risk clicks. Other kinds (low-latency mode lists real engine periods): `offered` unchanged.
std::vector<int> usableBufferSizes (DriverKind kind, const std::vector<int>& offered, int devicePeriod);

// After dropouts on a Windows shared-mode or DirectSound device (round 160): the ASIO driver worth suggesting, from the
// ASIO type's device names. The interface's own driver first (e.g. "SSL ASIO Driver 1"), then LYNX ASIO, then any;
// wrappers (ASIO4ALL, FL Studio ASIO, Generic Low Latency) only when nothing else is there. Empty when the current
// type already is ASIO / exclusive / not Windows, or no ASIO driver exists.
std::string suggestedAsioDriver (const std::string& currentTypeName, const std::vector<std::string>& asioDevices);

// --- Linux audio (round 161) ------------------------------------------------------------------------------------
// The driver type a first start opens on Linux, from the types with whether they found devices: PipeWire (LYNX's own
// client), then JACK (a running server), then ALSA, then whatever has devices. Empty when nothing has devices.
std::string preferredLinuxDriver (const std::vector<std::pair<std::string, bool>>& typesWithDevices);

// PipeWire's node.latency request for a buffer: "256/48000".
std::string pipewireLatency (int frames, int sampleRate);

// A readable channel name for a PipeWire audio position: "FL" -> "Left", "FR" -> "Right", "MONO" -> "Mono",
// "AUX3" -> "Aux 4", "RL" -> "Rear left"...; unknown positions stay as they are; empty -> "Channel <index + 1>".
std::string pipewireChannelName (const std::string& position, int index);

// The node name in a "default.audio.sink"-style metadata value ({"name":"alsa_output.pci-0000_00_1f.3.analog-stereo"}).
// Empty when there is none.
std::string pipewireDefaultName (const std::string& json);

// Device names as the device list shows them: each description once; a repeated one gets " (2)", " (3)"...
std::vector<std::string> uniqueDeviceNames (const std::vector<std::string>& descriptions);

// --- MIDI (MIDI page) -------------------------------------------------------------------------------------------
// How a played note's velocity reaches the instrument.
enum class VelocityCurve
{
    linear, // as played
    soft,   // light playing sounds louder
    hard,   // needs harder playing
    fixed   // every note at full velocity
};
float mapVelocity (float velocity, VelocityCurve curve) noexcept; // 0..1 -> 0..1 (0 stays 0: a note-off)

// Round 190ao (pain point 12; LYNX's own: FL's wrapper has no velocity setting, its MIDI group is the ports, release
// velocity, pitch bend range, MOD X aftertouch and All Notes Off [DATA Fruity Wrapper DFM, REA round 149 C7]): a hosted
// instrument's own answer to the notes played into it from MIDI devices (PluginRef::velocityMode / fixedVelocity).
// Mode 0 follows the MIDI settings' curve (the default: nothing changes); 1…4 is the plugin's own curve
// (VelocityCurve + 1), where Fixed plays every note at `fixedVelocity` (1…127), not at full velocity.
constexpr int pluginVelocityFollow = 0;
constexpr int pluginVelocityFixed = (int) VelocityCurve::fixed + 1;
constexpr int defaultFixedVelocity = 100; // (FL's default note velocity, 100 of 127)
constexpr int clampPluginVelocityMode (int mode) noexcept { return mode < 0 || mode > pluginVelocityFixed ? pluginVelocityFollow : mode; }
constexpr int clampFixedVelocity (int velocity) noexcept { return velocity < 1 ? 1 : velocity > 127 ? 127 : velocity; }
float mapPluginVelocity (float velocity, int mode, int fixedVelocity, VelocityCurve global) noexcept; // (0 stays 0)

struct MidiInputPrefs
{
    std::string name;     // the device name (as the system shows it)
    bool enabled = true;  // new devices are on
    int midiChannel = 0;  // 0 = any (omni), 1..16 = only that channel
    int target = -1;      // -1 = the selected channels of the Channel Rack, else that Channel Rack channel (0-based)
    // "Controller type" (round 190aw, core/ControllerProfile.h): "" = detect a profile by the port name,
    // "generic" = a generic controller (no profile), else a profile's id
    std::string controller;
    // MPE (issue #108, core/Mpe.h; a LYNX addition): the device is an MPE controller — lower zone, master channel 1,
    // `mpeMembers` member channels from 2 up, per-note pitch bend over `mpeBendRange` semitones. The MIDI channel
    // filter above does not apply then.
    bool mpe = false;
    int mpeMembers = 15;
    int mpeBendRange = 48;
    // FL's input "Port" (Options > MIDI, InPortSel "Unique / feedback port number", -1...255, none by default; round
    // 190bt): a controller link made with a port number follows only the devices with that port
    int port = -1;

    bool operator== (const MidiInputPrefs&) const = default;
};

// A MIDI controller link kept in the settings, for every project (FL's "global" links of the Mapping\Generic
// folder, made with a parameter's "Override global link...", form TMIDIInputForm_Generic "Global link settings";
// REA round 146). A project's own link of the same target or controller wins.
struct GlobalLink
{
    int cc = 0, channel = -1;                // channel -1 = any
    int kind = 0, index = 0, slot = 0, param = 0; // the AutomationTarget (kind = its enum value)
    // FL's "Global link settings" (form TMIDIInputForm_Generic; round 190bt), as MidiLink has them
    int port = -1;
    std::string formula;
    bool smooth = false;
    int smoothing = 469;
    int mode = 0; // FL's "Mode" (round 190cs; linkformula::LinkMode)
    bool hasSettings() const noexcept { return port >= 0 || ! formula.empty() || smooth || smoothing != 469 || mode != 0; }
    bool operator== (const GlobalLink&) const = default;
};

// An output device (FL's Options › MIDI "Output" panel, REA round 165f): master sync and all-notes-off.
struct MidiOutputPrefs
{
    std::string name;          // the device name
    bool masterSync = false;   // "Send master sync"
    std::string syncType = "clock"; // midisync::syncTypeKey: MIDI clock + SPP, or an MTC rate
    int offsetMs = 0;          // "Master sync offset (milliseconds)", -1000…1000 (later = positive)
    bool allNotesOff = false;  // "Send all notes off" on init and after an export

    bool operator== (const MidiOutputPrefs&) const = default;
};

struct MidiPrefs
{
    std::vector<MidiInputPrefs> inputs; // the devices the user changed (others use the defaults)
    std::vector<MidiOutputPrefs> outputs; // the output devices the user changed
    std::string clockInput;             // "External clock sync": the input LYNX follows ("" = off)
    int clockOffsetMs = 0;              // "External sync offset (milliseconds)", -1000…1000
    std::vector<GlobalLink> globalLinks;
    VelocityCurve velocity = VelocityCurve::linear;
    bool pickup = false;                // a controller takes over a parameter only once it reaches its value
    // FL's "Foot pedal controls note off" (KbHoldSupportBox, on; round 187a): LYNX holds the notes on CC 64 (sustain)
    // and CC 66 (sostenuto); off, the pedal goes to the plugins as a controller
    bool footPedalNoteOff = true;
    // FL's Options › MIDI "Song marker jump (MIDI channel)": a note on this channel (1..16; 0 = off) jumps the song to
    // a marker (note 0 = the first marker...) instead of playing
    int songMarkerJumpChannel = 0;
    // FL's "MIDI channel used for playlist live mode" (round 165m): notes on this channel (1…16, 0 = off) launch the
    // Playlist's performance clips while Performance mode is on (core/Performance.h, perf::MidiMapping)
    int performanceChannel = 0;
    // "Use a game controller" (round 184a, S16): the first XInput pad plays the transport and is linkable like a
    // MIDI controller (core/GamepadMap.h); off by default
    bool gamepad = false;

    // The settings of a device (the defaults when it was never changed).
    MidiInputPrefs input (const std::string& name) const;
    void setInput (const MidiInputPrefs& input);
    MidiOutputPrefs output (const std::string& name) const;
    void setOutput (const MidiOutputPrefs& output);

    // Should a message on MIDI channel `channel` (1..16) from `device` be played?
    bool accepts (const std::string& device, int channel) const;

    std::string toText() const;
    static MidiPrefs fromText (const std::string& text);

    bool operator== (const MidiPrefs&) const = default;
};

// What to do with the MIDI input devices at a periodic check (issue #4: a controller that was not ready when LYNX
// started, or that came back after a reconnection, stayed silent until it was turned off and on in Settings).
struct MidiPort
{
    std::string id;   // the system's identifier
    std::string name; // the name MidiPrefs uses
};

struct MidiInputPlan
{
    std::vector<std::string> open;   // wanted and not open (it appeared, or opening failed before): open it
    std::vector<std::string> reopen; // open, but it disappeared since the last check and came back: close and open again
    std::vector<std::string> close;  // open but turned off in the settings
    // Round 190gh (B8, hot-plug): open but no longer listed (unplugged while open): its handle is closed at once, so
    // nothing waits on a device that is gone; it opens again (`open`) when it is plugged back in
    std::vector<std::string> gone;
    bool operator== (const MidiInputPlan&) const = default;
};

// Round 190gh (B8, hot-plug): what changed between two device lists (inputs and outputs together). `added` / `removed`
// are names (a device with an input and an output is named once); a device that came back under another identifier
// (plugged into another USB port) is in neither, but `changed` is set, so its ports are opened again.
struct MidiDeviceChange
{
    std::vector<std::string> added, removed;
    bool changed = false; // any identifier or name differs
    bool operator== (const MidiDeviceChange&) const = default;
};
MidiDeviceChange diffMidiDevices (const std::vector<MidiPort>& before, const std::vector<MidiPort>& after);

// `available` = the devices now; `previous` = the identifiers available at the last check; `openIds` = the ones open
// (including ones that are no longer available: they go to `gone`).
MidiInputPlan planMidiInputs (const MidiPrefs& prefs, const std::vector<MidiPort>& available,
                              const std::vector<std::string>& previous, const std::vector<std::string>& openIds);

// Takeover ("pickup") of a parameter by a hardware controller: the controller changes the parameter only once its
// value reaches the parameter's (moving past it in either direction, or landing within `tolerance`), so a knob that
// sits elsewhere doesn't make the parameter jump. `previous` is the controller's last value (< 0 = none yet).
bool pickupReached (float parameter, float previous, float incoming, float tolerance = 1.5f / 127.0f) noexcept;

// One controller-to-parameter link under takeover: once caught, the controller keeps the parameter until something
// else moves the parameter away from what the controller set (then it has to reach it again).
struct PickupState
{
    bool caught = false;
    float lastController = -1.0f; // the controller's last value
    float lastSet = -1.0f;        // what the controller last set the parameter to
};
// Returns whether `incoming` should be applied to a parameter now at `parameter`.
bool pickupUpdate (PickupState& state, float parameter, float incoming, float tolerance = 1.5f / 127.0f) noexcept;

// --- plugin manager ---------------------------------------------------------------------------------------------
enum class PluginStatus
{
    ok,
    failed,     // the scan couldn't load it (it crashed, hung or isn't a plugin): skipped by later scans
    missingFile // listed, but its file is gone
};

struct PluginRow
{
    std::string name, vendor, format, category, file, id;
    bool instrument = false;
    PluginStatus status = PluginStatus::ok;
    bool favourite = false;
    std::string bits = {}; // FL's Bits column (Windows, round 176a): "64", "32" or "32+64"
};

enum class PluginFilter { all, instruments, effects, favourites, failed };
enum class PluginSort { name, vendor, format, type, status };

// The rows to show (indices into `rows`): those passing `filter` whose name, vendor, format, category or file
// contain every word of `query` (case-insensitive), sorted by `sort` (then by name).
std::vector<int> visiblePlugins (const std::vector<PluginRow>& rows, const std::string& query, PluginFilter filter,
                                 PluginSort sort, bool descending = false);

// Favourite plugins: one identifier per line.
std::set<std::string> parseFavourites (const std::string& text);
std::string formatFavourites (const std::set<std::string>& ids);

// Plugin names the user gave (FL's renamed Plugin database entries, issue #11): "key<TAB>name" per line. The key is
// the plugin's identifier string, or "lynx:fx:<name>" / "lynx:gen:<name>" for LYNX's built-in ones. A name that
// starts with a character no other entry starts with ("1_Limiter") is picked by typing it in the slot menus.
std::map<std::string, std::string> parsePluginNames (const std::string& text);
std::string formatPluginNames (const std::map<std::string, std::string>& names);

// Type-ahead in a menu of `labels` (FL's menus): the one label starting with `typed` (case-insensitive, a renamed
// plugin's "1_" digit or letter); -1 when none or several do.
int uniqueTypeAhead (const std::vector<std::string>& labels, char typed);
// FL's letter keys in an open plugin menu (issue #64): the next item after `current` (-1 = none highlighted) whose
// label starts with `typed` (case-insensitive, leading spaces skipped), wrapping around past the end; only items
// that can be highlighted (`selectable`, e.g. not headings or separators) count. -1 when none starts with it.
// Pressing "e" again moves on to the next one (EQ, then Effector...); nothing is picked until Enter.
int nextTypeAhead (const std::vector<std::string>& labels, const std::vector<bool>& selectable, int current, char typed);
} // namespace lynx::prefs
