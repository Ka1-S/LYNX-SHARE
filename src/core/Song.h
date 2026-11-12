#pragma once

// Song data model. Does not depend on JUCE.
// The UI edits a copy and publishes an immutable snapshot to the audio thread
// (see SnapshotExchange.h), so nothing here needs to be thread-safe.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <map>
#include <vector>
#include <utility>

#include "core/Adr.h"
#include "core/VideoTrack.h"

namespace lynx
{
constexpr int ppq = 960;              // ticks per quarter note
constexpr int stepsPerBeat = 4;       // step sequencer in sixteenth notes
constexpr int ticksPerStep = ppq / stepsPerBeat;
constexpr int maxMixerTracks = 501;   // 0 = Master, 1..500 inserts: FL 2026's dynamic mixer ("You can add a maximum of N tracks" up to 500;
                                      // round 166d, issue #14). Was 128 (FL's old 125 + Master)
constexpr float unityFader = 0.8f;    // fader position that gives 0 dB, as in FL

// A press on a mixer fader's rail, away from the cap (FL, measured, REA_rodada177a section 4.2): the cap steps towards the
// mouse, one step at once and one every ~64 ms while the button is held, and stops under the mouse. It never jumps.
constexpr float faderRailStep = 0.0056f; // of the fader's travel (100 clicks: 100 % -> 30 % in FL's hint)
constexpr int faderRailRepeatMs = 64;
// FL measured (REA_rodada177a section 4.2): held 50 / 100 / 160 / 250 / 300 / 400 ms the cap took 1 / 2 / 3 / 4 / 5 / 7
// steps, so the first repeat comes one period after the press, with no longer first delay. LYNX counts the steps from
// the press time, not from when its timer happens to fire (a JUCE timer's first callback can come early, which turned
// one click into two steps).
constexpr int faderRailFirstRepeatMs = faderRailRepeatMs;
// how many steps a press held `heldMs` has taken: one at once, then one per period
constexpr int faderRailStepsForHold (int heldMs) noexcept
{
    return heldMs < faderRailFirstRepeatMs ? 1 : 2 + (heldMs - faderRailFirstRepeatMs) / faderRailRepeatMs;
}
constexpr float faderRailStepTowards (float value, float target) noexcept
{
    const auto distance = target - value;
    return distance > faderRailStep ? value + faderRailStep : distance < -faderRailStep ? value - faderRailStep : target;
}

// FL mixer fader curve: g = (11^v - 1) / 10 with v = position * 1.25 (0 dB at 0.8;
// +5.6 dB at maximum). docs/analise/REA_rodada2.md §2.2.
float faderGain (float position) noexcept;
// Its inverse: the fader position (0..1) that gives `gain` (0 for gain <= 0, 1 for maxFaderGain and above).
float faderPosition (float gain) noexcept;
// Gain at the top of the fader: +5.6 dB. FL's insert-to-insert and insert-to-Master route levels use the same curve
// and range (0..16000, 12800 = 0 dB; REA_rodada107 §3), so a send can reach it too.
constexpr float maxFaderGain = 1.9031f;

// Channel volume with the same curve, but 1.0 = 0 dB (FL's default, 0.78, gives -5.2 dB).
// docs/analise/REA_rodada3.md §2.
float channelGain (float volume) noexcept;

// The channel volume at which a Playlist audio clip plays its file at unity, in FL and in LYNX: FL's default, 10000 of
// 12800 (Instruments.cpp audioClipUnity; FL's AudioTrack.fst template holds it). Channels made from a render get it, so
// the render sounds like what it replaces: Render to audio, FL's pattern render and consolidation (which load
// AudioTrack.fst and never set the volume, FUN_01188230 / FUN_012fbda0), the FL Studio export's option A (round 182b/186b).
inline constexpr float audioClipUnityVolume = 10000.0f / 12800.0f;

// FL's audio clip channel template (Data/System/AudioTrack.fst, 0xD7 byte 0x53 = 1, round 189l/189n): a NEW audio clip
// channel declicks in mode 1 (4 ms transient attack at a clip start, sharp end). Every place that makes a new audio
// clip channel sets it. SamplerSettings keeps 0 as its default: .lynx files store declickMode only when it is not 0, so
// saved projects load with the mode they were saved with.
inline constexpr int audioClipDeclickMode = 1;

// Which running instance a channel or mixer effect slot is (round 168s, issue #32): a number given when the channel /
// slot is made, kept by every copy (snapshots, undo, moving it in its list) and never saved. The renderer keeps an
// instrument / effect with its channel / slot when the list order changes (inserting a channel, moving an effect up),
// so a moved plugin keeps its live state and sounding notes. It never makes two channels or slots compare different.
struct InstanceTag
{
    std::uint64_t id = next();

    static std::uint64_t next() noexcept
    {
        static std::atomic<std::uint64_t> counter { 1 };
        return counter.fetch_add (1, std::memory_order_relaxed);
    }
    void renew() noexcept { id = next(); } // a copy that is a new channel / slot (clone, duplicate)
    bool operator== (const InstanceTag&) const noexcept { return true; }
};

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth,
    sampler,
    audioClip, // sampler without transposition that stops at the end of the clip (audio clip channel, as in FL)
    plugin,    // external instrument (VST3), created by the app
    controller, // makes no sound: notes trigger envelopes that modulate other parameters (FL's Envelope Controller)
    sakura,     // physical string model (FL's Sakura engine, REA_rodada28 §1); parameters in ChannelDef::modelParams
    drumpad,    // physical drum model (FL's Drumpad engine, REA_rodada28 §2); parameters in ChannelDef::modelParams
    boobass,    // FL's BooBass engine (REA_rodada29 §4); multisample read from the user's DLL (samplePath), 3 knobs
    drumsynth,  // FL's Fruity DrumSynth Live (REA_rodada30 §1): 120 patches in modelParams, recorded waves from the user's DLL
    harmless,   // FL's Harmless additive engine (REA_rodada32): raw state in modelParams (layout in core/Harmless.h)
    sytrus,     // FL's Sytrus engine (REA_rodada31): the plugin's raw saved state in ChannelDef::pluginState
    harmor,     // FL's Harmor additive engine (REA_rodada34): the plugin's raw saved state in ChannelDef::pluginState
    gms,        // FL's GMS engine (REA_rodada39 §1): raw saved state in pluginState, wave folder in samplePath
    morphine,   // FL's Morphine engine (REA_rodada39 §2): raw saved state in pluginState, noise sample in samplePath
    ogun,       // FL's Ogun inverse-FFT engine (REA_rodada37): the plugin's raw saved state in ChannelDef::pluginState
    minisynth,  // FL's MiniSynth engine (REA_rodada41): raw saved state in pluginState, path of oscdata.dat in samplePath
    dx10,       // FL's Fruity DX10 FM engine (REA_rodada42): the plugin's raw saved state in ChannelDef::pluginState
    kepler,     // FL's Kepler engine (REA_rodada46): raw saved state in pluginState, the user's engine DLL in samplePath
    poizone,    // FL's PoiZone engine (REA_rodada44): the plugin's raw saved state in ChannelDef::pluginState
    fruitkick,  // FL's Fruit Kick (REA_rodada43 §1): raw saved state (with its envelope snapshot) in pluginState
    bassdrum,   // FL's BassDrum (REA_rodada43 §2): raw saved state in pluginState, the BassDrum folder in samplePath
    toxic,      // FL's Toxic Biohazard engine (REA_rodada45): raw saved state in pluginState, the user's DLL in samplePath
    threeosc,   // FL's 3x Osc engine (REA_rodada47): normalised state in pluginState, custom-wave sample in samplePath
    autogun,    // FL's Autogun (REA_rodada57 §1): Ogun's engine on the raw saved state (pluginState), SG.dat path in samplePath
    transistorbass, // FL's Transistor Bass (REA_rodada57 §2): raw saved state in pluginState, the user's engine_x64.dll in samplePath
    simsynth,   // FL's SimSynth engine (REA_rodada64 §1–4): the plugin's raw saved state (564 bytes) in ChannelDef::pluginState
    wasp,       // FL 2026's Wasp / Wasp XT (REA_rodada64 §5): a 64-bit placeholder that keeps its state and makes no sound
    plucked,    // FL's Plucked! (REA_rodada65 §1): the plugin's raw saved state (5 knobs) in ChannelDef::pluginState
    beepmap,    // FL's BeepMap (REA_rodada65 §2): the plugin's raw saved state (settings + bitmap) in ChannelDef::pluginState
    sawer,      // FL's Sawer (REA_rodada65 §3): raw saved state in pluginState, the user's Sawer\data.saw in samplePath
    flkeys,     // FL Keys (REA_rodada69 §8): raw saved state in pluginState, wavetable files in zones, .wti table in modelParams
    granulizer, // FL's Fruity Granulizer engine (REA_rodada67): normalised state in pluginState, the channel's sample in samplePath
    drumaxx,    // FL's Drumaxx engine (REA_rodada67 §6–§9): the plugin's raw saved state ("SSLF" stream) in ChannelDef::pluginState
    slayer,     // FL Slayer (REA_rodada68 §2–6): raw saved state in pluginState, the user's Slayer_Engine.dll in samplePath
    fruitydance, // Fruity Dance (REA_rodada68 §8): a visual plugin (dancing character) that keeps its state and makes no sound
    directwave, // DirectWave (REA_rodada72): raw saved state in pluginState, one zone per zone of the played program, sample formats in modelParams
    midiOut,    // FL's MIDI Out (REA_rodada16 §1, REA_rodada133 §4): no sound; its notes and MIDI controls go to the plugins whose
                // MIDI input port is its port (PluginRef::midiInPort). FL's state (channel @4, port @24) in pluginState
    patcher,    // FL's Patcher as a generator (REA_rodada141): a graph of instruments and effects in ChannelDef::patcher
    soundfont,  // LYNX Soundfont (REA_rodada158, core/SoundFont.h): the .sf2 / .sf3 in samplePath, settings in modelParams
    layer,      // FL's Layer channel (REA_rodada164a, core/Layer.h): no sound; its notes play its children (ChannelDef::layer)
    dashboard,  // FL's Dashboard (REA_rodada164a, core/ControlSurface.h): no sound; a panel of controls in patcher.surface
    voltage,    // FL's Voltage Controller (REA_rodada164a, core/VoltageController.h): its notes as control voltages; FL's
                // state in pluginState
    vfx,        // FL's VFX note processors (round 164b, core/Vfx.h): kind, output port and FL's state in pluginState; no sound
    videoplayer, // FL's Fruity Video Player (round 165g, core/VideoPlayer.h): FL's state (with the video's path) in
                // pluginState; plays the video's sound in sync with the song, the video is shown by its editor
    keplerExo,  // FL 2026's Kepler Exo (REA_rodada167d): Kepler's engine with the "Kepler-2" state in pluginState, the user's
                // Kepler Exo engine_x64.dll in samplePath
    wavetraveller, // FL's Wave Traveller (REA_rodada167f, core/WaveTraveller.h): FL's state (per-key curves) in pluginState,
                   // the channel's sample in samplePath
    fruityslicer,  // FL's Fruity Slicer (round 180c, core/FruitySlicer.h): FL's state (slices, fades, declick...) in pluginState,
                   // the sample in samplePath; the sampler's voices play the slices
    slicex,        // FL's Slicex (rounds 190h / 190l, core/SlicexInstrument.h): FL's state in pluginState, deck A's sample in
                   // samplePath, deck B's in zones[0].path
    fpc,           // FL's FPC (round 190q, core/Fpc.h, core/FpcInstrument.h): the kit's FL state in pluginState, the files of its
                   // layers in `zones` (one per layer with a sample, in the state's order; only `path` is used)
    pocketSuperSaw // Pocket SuperSaw (round 190by, core/PocketSuperSaw.h): FL Studio Mobile's SuperSaw module; its 59
                   // values 0…1 in modelParams (FLM's parameter k + 1 at index k)
};

// External plugin: format identifier (the app knows how to find the file) + saved state.
// Third-party plugin coming from an FL project ("Fruity Wrapper"), before finding the same
// installed plugin (docs/analise/REA_rodada5.md). Not saved in .lynx: the app resolves it on import.
// One output of a hosted generator in FL's wrapper Processing › Connections (round 188d, issue #114 [UI and audio
// checked in FL 2026: docs/analise/REA_rodada188d.md]): it plays on the insert `offset` tracks after the channel's
// own ("---" = 0: the channel's insert, where every active output goes by default); an inactive output is silent.
struct PluginOutputRoute
{
    int offset = 0;          // −999…999, FL's wheel range
    std::int8_t active = -1; // the row's LED: 1 on, 0 off, −1 as the plugin declares the output (VST2 pin, VST3 bus)

    bool isActive (bool pluginDefault) const noexcept { return active < 0 ? pluginDefault : active != 0; }
    bool operator== (const PluginOutputRoute&) const = default;
};

struct ForeignPlugin
{
    enum class Format : std::uint8_t { none, vst2, vst3, clap, other };

    Format format = Format::none;
    std::uint32_t vst2Id = 0;                 // AEffect::uniqueID
    std::array<std::uint8_t, 16> classId {};  // VST3 class TUID, as laid out in memory
    bool hasClassId = false;
    std::string clapId, name, path;           // path: binary on the machine that saved it
    std::vector<std::uint8_t> state;          // raw 0x35 record (format depends on `format`)
    int midiInPort = -1, midiOutPort = -1;     // wrapper MIDI ports (record 0x01; −1 = none)
    // wrapper options (record 0x02, REA round 134): processing/troubleshooting flags and the fixed-buffer options
    std::uint32_t wrapperVersion = 0, wrapperFlags = 0, fixedOptions = 0;
    bool hasWrapperOptions = false;
    // round 151 [DATA: FL 2026's options toggled one by one, saved and compared]: the second flags word of record 0x02
    // (+21) and the pitch bend range of record 0x01 (+8, semitones; −1 = not saved)
    std::uint32_t wrapperFlags2 = 0;
    bool hasWrapperFlags2 = false;
    int bendRange = -1;
    // round 188d [DATA: FL 2026 saves, docs/analise/REA_rodada188d.md]: records 0x1E (input / output counts), 0x1F
    // and 0x20 (per input / output: i32 offset or sidechain index, u32 active, u32 0) — Processing › Connections
    std::vector<PluginOutputRoute> inputRoutes, outputRoutes;
    bool hasRoutes = false;

    bool operator== (const ForeignPlugin&) const = default;
};

// FL's Settings › General › "Disable typing keyboard priority by default" (DisablePlugFocusDefaultBox, off; round
// 187a): on, a plugin added from now on starts with its window's Typing keyboard priority off (LYNX first)
namespace pluginkeys
{
inline std::atomic<bool>& newPluginsHostFirst() noexcept
{
    static std::atomic<bool> on { false };
    return on;
}
} // namespace pluginkeys

struct PluginRef
{
    std::string id;    // unique plugin identifier (JUCE's format-name-hash)
    std::string name;  // to display even when the plugin is not installed
    std::string state; // plugin state in base64 (what the plugin returns when saving)
    ForeignPlugin foreign; // FL import not yet resolved (format == none: nothing pending)
    // FL wrapper processing options (REA_rodada118 Part B), saved with the project:
    bool fixedBuffers = false;    // "Use fixed size buffers": constant 2 ms blocks (flFixedBlockSize), + that latency
    bool separateProcess = false; // "Make bridged": this plugin runs in its own process even with the global option off
    bool maxSizeBuffers = false;  // with fixedBuffers: "Process maximum size buffers" (the device buffer, not 2 ms)
    bool threaded = true;         // "Allow threaded processing" (REA round 134): off = always on the audio thread itself
    // "Allow smart disable" (REA round 139, wrapper flag 0x20000000): off = this plugin is never smart-disabled, whatever
    // its slot's "Smart disable" says (for plugins that misbehave when they are not called)
    bool allowSmartDisable = true;
    // with fixedBuffers: "Use maximum buffer size from host" (fixed-buffer options bit 1) — the plugin is prepared for
    // the host's maximum block (4096) instead of the fixed block it is always called with
    bool hostMaxBuffer = false;
    // The wrapper's "MIDI input port" (0…255; −1 = none): the notes / controls of the MIDI Out channels on that port
    // reach this plugin (FL: Plugin wrapper settings › MIDI › Input port)
    int midiInPort = -1;
    // FL's wrapper settings, round 149 (Fruity Wrapper's DFM, REA_rodada149 §C):
    // "Output port" (0…255; −1 = none): the notes the plugin sends out play the plugins whose input port it is
    int midiOutPort = -1;
    // "Notify about rendering mode" (wrapper flag 0x4, on by default): the plugin is told when an export renders
    bool notifyRender = true;
    // the Info section's Latency: the delay compensation uses this (ms) instead of what the plugin reports (−1)
    float latencyOverrideMs = -1.0f;
    // FL's wrapper settings with LYNX behaviour, round 151 (defaults as FL's page shows them [EXEC]):
    bool releaseVelocity = true;    // Settings › MIDI › "Send note release velocity": note offs carry the note's release
    bool sendBendRange = false;     // "Send pitch bend range (semitones)": a pitch-bend-sensitivity RPN when it starts
    int bendRange = 12;             //   the range sent, 1…48 semitones
    bool modXAftertouch = false;    // "Send MOD X as polyphonic aftertouch": each note's Mod X follows its note on
    bool allNotesOffOnStop = true;  // "Send All Notes Off when playback stops" (off: only the held notes are released)
    // MPE (issue #108, core/Mpe.h; a LYNX addition, FL's wrapper has no such switch): each note goes out on its own
    // member channel (2 … 16) with its fine pitch as that channel's pitch bend (range 48), Mod X as CC 74 and Mod Y as
    // channel pressure; a CLAP plugin that takes CLAP notes gets them as note expressions
    bool mpe = false;
    bool resetOnFlush = true;       // Troubleshooting › "Reset plugin when FL Studio resets": before an export, on panic
    // FL's Troubleshooting box "Allow FL Studio to process keyboard presses when the keyboard focus option is on"
    // (wrapper flags2 bit 0x800, off for a new plugin): kept from and written back to FL projects (round 181c/187a);
    // what it changes in LYNX comes with the Typing keyboard priority button (issue #96 slice 2)
    bool allowHostKeys = false;
    // FL's plugin-window "Typing keyboard priority" button turned off: LYNX takes the keys it uses (typing piano,
    // shortcuts) before this plugin, which gets the rest (FL manual, Wrapper > Typing keyboard priority; issue #96).
    // .flp: wrapper flags bit 0x01000000 set [DATA round 187a: one project saved with the button on and off]
    bool hostKeysFirst = pluginkeys::newPluginsHostFirst().load (std::memory_order_relaxed);
    bool sendLoopPosition = true;   // "Send loop position": the play head reports the loop's start and end
    // Settings › Options › "Bit size" (round 176a, FL's wrapper): which build of a plugin installed as both 32-bit and
    // 64-bit loads — 0 = Native (the 64-bit one when there is one), 32 (in the 32-bit bridge) or 64
    int bitSize = 0;
    // Processing › Connections (round 188d): [plugin output] its route; outputs past the end keep FL's defaults
    std::vector<PluginOutputRoute> outputs;
    // Processing › Options › "Process inactive inputs and outputs" (on by default; wrapper flag 0x8 [DATA]): an
    // inactive output still plays on its insert [EXEC fl188d k33]; off: it is silent
    bool processInactive = true;
    // an effect's inputs in Processing › Connections (round 188d): `offset` = the sidechain index (0 = the insert's
    // own audio, k = the k-th insert sending to this one as a sidechain)
    std::vector<PluginOutputRoute> inputs;
    PluginOutputRoute outputRoute (int index) const noexcept
    {
        return index >= 0 && index < (int) outputs.size() ? outputs[(std::size_t) index] : PluginOutputRoute {};
    }
    // FL's plugin menu › "Detached" (issue #116): the editor opens in a window of its own above the main window instead
    // of docked in the workspace. LYNX's own field until the .flp bit is found (docking is behind a developer switch)
    bool detached = false;
    // Online Sessions P5-A1 (round 190ai, core/RemotePlugins.h): a plugin of the session's host that the host shared.
    // `playedBy` is the participant whose computer plays it (0: everyone plays their own copy); everyone else hears
    // that computer's stand-in renders. Such a plugin has no state on the wire: its sound is its default state plus
    // `values` — (parameter index, value 0..1) pairs, sorted by index — which the guest's parameter editor writes and
    // the host applies. Both are cleared when the session ends.
    std::uint32_t playedBy = 0;
    std::vector<std::pair<int, float>> values;
    // LYNX's own "Pin on top" of the plugin window (round 190ao, pain point 11; FL has none): the detached window stays
    // above LYNX's other windows. Off for a new plugin and for projects saved before
    bool pinned = false;
    // LYNX's own per-plugin velocity (round 190ao, pain point 12; FL's wrapper has none): how notes played into this
    // instrument from MIDI devices reach it. 0 = follow the MIDI settings' curve (the default, and projects saved
    // before), 1 as played, 2 soft, 3 hard, 4 fixed at `fixedVelocity` (1…127). core/Preferences.h mapPluginVelocity
    int velocityMode = 0;
    int fixedVelocity = 100;

    bool operator== (const PluginRef& o) const
    {
        return id == o.id && state == o.state && foreign == o.foreign && fixedBuffers == o.fixedBuffers && separateProcess == o.separateProcess
               && threaded == o.threaded && maxSizeBuffers == o.maxSizeBuffers && allowSmartDisable == o.allowSmartDisable
               && hostMaxBuffer == o.hostMaxBuffer
               && midiInPort == o.midiInPort && midiOutPort == o.midiOutPort && notifyRender == o.notifyRender
               && latencyOverrideMs == o.latencyOverrideMs && releaseVelocity == o.releaseVelocity && sendBendRange == o.sendBendRange
               && bendRange == o.bendRange && modXAftertouch == o.modXAftertouch && allNotesOffOnStop == o.allNotesOffOnStop && mpe == o.mpe
               && resetOnFlush == o.resetOnFlush && sendLoopPosition == o.sendLoopPosition && bitSize == o.bitSize && outputs == o.outputs
               && processInactive == o.processInactive && inputs == o.inputs
               && allowHostKeys == o.allowHostKeys && hostKeysFirst == o.hostKeysFirst
               && detached == o.detached && playedBy == o.playedBy && values == o.values
               && pinned == o.pinned && velocityMode == o.velocityMode && fixedVelocity == o.fixedVelocity;
    }
};

// Channel sampler settings (FL's "Channel settings" window).
// Modulation envelope (cutoff or pitch), like the channel envelopes in FL.
struct ModEnvelope
{
    float amount = 0.0f;  // filter: octaves (±); pitch: semitones (±). 0 = off
    float attack = 0.0f;  // seconds
    float hold = 0.0f;
    float decay = 0.3f;
    float sustain = 0.0f; // 0..1
    float release = 0.2f;
    // 0 = LYNX's linear AHDSR; 1 = Toxic Biohazard's filter envelope (REA_rodada33 §2: exponential stages chosen per
    // 128-sample block, attack towards 5 clipped at 1, elapsed time counted in 1/44100 s)
    int shape = 0;

    bool operator== (const ModEnvelope&) const = default;
};

// Channel LFO (like FL's, REA_rodada6 §2): shape, period, delay and fade-in; the amount
// is in the target's unit. Restarts on every note.
struct ChannelLfo
{
    float amount = 0.0f;  // pan: −1..1; volume: depth 0..1; cutoff: octaves; pitch: semitones. 0 = off
    float period = 0.5f;  // seconds, or beats with `sync`
    bool sync = false;
    int shape = 0;        // 0 sine, 1 triangle, 2 pulse
    float delay = 0.0f;   // seconds (or beats with `sync`) before starting
    float attack = 0.0f;  // fade-in, in the same unit
    bool global = false;  // phase locked to the song position (FL's "Global" button) instead of restarting per note
    bool secondsTiming = false; // delay and fade-in stay in seconds even with `sync` (Kepler's LFO1)

    bool operator== (const ChannelLfo&) const = default;
};

enum LfoTarget { lfoPan, lfoVolume, lfoCutoff, lfoPitch, lfoTargets };

// How a pitch-preserving stretch is computed (core/TimeStretch, REA_rodada160): general = polyphonic material and full
// mixes (FL's élastique Pro / E3 generic / Auto), transients = drums and loops (FL's E2 transient, slice modes), mono =
// a single voice or instrument (FL's E3 mono, E2 mono / speech).
// Round 175a (N4) adds LYNX's warp modes modelled on Ableton Live's (measured against Live 12 renders, see
// docs/analise/ALS_FORMAT.md §12): beats = the audio cut at its attacks, each piece played at its own speed and its
// gap filled by playing it back and forth (Live's Beats); tones = a single pitched voice (Live's Tones); texture =
// dense material without a clear pitch (Live's Texture); complex = full mixes (Live's Complex / Complex Pro).
enum class StretchMethod : int { general = 0, transients = 1, mono = 2, beats = 3, tones = 4, texture = 5, complex = 6 };
constexpr int stretchMethodCount = 7;

// FL's Sampler › "Precomputed effects" (REA_rodada167h §1–§2): computed once on the sample, in FL's order, before it
// plays (core/SamplePrecompute.h). Raw FL values: fadeIn / fadeOut 0…1024 (256 = a ramp over the whole sample),
// crossfade 0…2048 (a fraction of the loop), trim 0…32768 (the silence threshold).
struct SamplePrecompute
{
    bool removeDc = false, normalize = false, reversePolarity = false, fadeStereo = false, swapStereo = false;
    int fadeIn = 0, fadeOut = 0, crossfade = 0, trim = 0;
    bool pingPong = false; // Ping pong loop: the loop plays forward then backward (with loop points)
    // FL's legacy precomputed effects (the Sampler's second effects page, REA_rodada167l §1), raw FL values with FL's
    // defaults: boost 0…256, EQ (FL's "FX": the frequency balance) 0…256 (128 = flat), ring modulation mix 0…256 and
    // frequency 0…256 (128), stereo delay 0…4096 (2048 = none), low-pass cutoff 16…1024 (1024 = off) and resonance
    // 0…640, reverb 0…256 with its type 0 / 1, pogo (FL's "FX3") 0…512 (256 = none), clip at 0 dB.
    int boost = 0, eq = 128, ringMix = 0, ringFreq = 128, stereoDelay = 2048, cutoff = 1024, resonance = 0;
    int reverb = 0, reverbType = 1, pogo = 256;
    bool clip = false;

    bool legacy() const noexcept
    {
        return boost > 0 || eq != 128 || ringMix != 0 || stereoDelay != 2048 || cutoff < 1024 || resonance > 0 || reverb != 0
               || pogo != 256 || clip;
    }
    bool any() const noexcept
    {
        return removeDc || normalize || reversePolarity || fadeStereo || swapStereo || fadeIn > 0 || fadeOut > 0 || crossfade > 0
               || trim > 0 || pingPong || legacy();
    }
    bool operator== (const SamplePrecompute&) const = default;
};

// A warp marker (round 175a, M1; Ableton Live's WarpMarker SecTime / BeatTime): a time in the sample file and the
// content beat it plays on. Between markers the sample is stretched evenly.
struct WarpPoint
{
    double seconds = 0.0, beats = 0.0;
    bool operator== (const WarpPoint&) const = default;
};

struct SamplerSettings
{
    float pitch = 0.0f;   // semitones, -24..24
    float attack = 0.0f;  // seconds, 0..2
    float decay = 0.0f;   // seconds until sustain (0 = no decay)
    float sustain = 1.0f; // 0..1
    float release = 0.0f; // seconds, 0..4; 0 = plays the whole sample (one-shot)
    bool reverse = false;
    bool cut = false;     // a new note cuts the previous one ("cut itself")
    float stretchBeats = 0.0f; // > 0: the whole sample lasts this long (in beats), following the BPM
    bool keepPitch = false;    // with stretch: stretches without changing pitch (core/TimeStretch); otherwise resamples
    StretchMethod stretchMethod = StretchMethod::general; // with keepPitch: the stretcher's preset
    int flStretchMode = 0;     // FL's own mode from an imported .flp (0 = none known): the export writes it back
    int flStretchPitch = 0;    // the part of `pitch` that came from FL's stretch PITCH knob (cents): it goes back there
    float stretchPitch = 0.0f; // Beats: semitones each piece is resampled by, offline (a Live clip's transpose; round 175a)
    // audio clips, round 190ds (core/KeyFollow.h): the musical key of the audio (a loop's key read from its name or
    // detected), −1 = not known; minor = the key is minor; how it was found (0 none, 1 the file's name, 2 detected from
    // the audio, 3 set by the user)
    int loopKeyRoot = -1;
    bool loopKeyMinor = false;
    std::uint8_t loopKeySource = 0;
    // with keepPitch and stretchBeats: the file's warp markers (seconds → beats, increasing). The file still lasts
    // stretchBeats; inside it each stretch of audio between two markers gets its own speed (rendered offline, like the
    // rest of the stretch). Fewer than 2 markers, or markers in a straight line: one speed (round 175a, M1)
    std::vector<WarpPoint> warp;
    // outside Beats nothing renders stretchPitch offline: it moves to the Pitch knob (-24..24), so the sample stays at its
    // pitch. True if it moved.
    bool moveBakedPitchToKnob() noexcept
    {
        if (stretchPitch == 0.0f || (stretchMethod == StretchMethod::beats && keepPitch))
            return false;
        pitch = std::clamp (pitch + stretchPitch, -24.0f, 24.0f);
        stretchPitch = 0.0f;
        return true;
    }
    float regionStart = 0.0f;  // played region of the sample (fractions 0..1)
    float regionEnd = 1.0f;
    float startOffset = 0.0f;  // FL's start offset (0xD7 f64 at 0x90): where notes start, a fraction of the region (after
                               // the precompute crops it, REA_rodada167h §4)
    std::vector<float> slices; // slices (starts as fractions): key 60 + i plays slice i, at the original pitch
    // Fruity Slicer (round 180c, set from its state when the rack is built, not saved): the key of slice 1, and "play to
    // end" (a slice plays on to the sample's end instead of stopping at the next slice)
    int sliceFirstKey = 60;
    bool slicesToEnd = false;
    bool slicesCutAtEnd = false; // Fruity Slicer: a slice's voice stops at its end (no 10 ms end fade; FL render)
    bool porta = false;        // portamento (with "cut", the 808 glide): the note starts from the previous note's pitch
    float portaBeats = 0.125f; // glide time, in beats
    float cutoff = 20000.0f;   // channel filter (Hz); low-pass at 20 kHz with no resonance = off
    float resonance = 0.0f;    // 0..1
    int filterType = 0;        // 0 low-pass, 1 band-pass, 2 high-pass, 3 band-stop
    float gainDb = 0.0f;       // the Sampler's Gain knob (dB, -24..+24): a level boost/cut of the sample before the
                               // channel volume (FL's Sampler has none: its closest is the "Normalize" precomputed effect)
    ModEnvelope filterEnv;     // shifts the cutoff (octaves)
    ModEnvelope pitchEnv;      // shifts the pitch (semitones)
    std::array<ChannelLfo, lfoTargets> lfo {}; // per LfoTarget
    // FL's volume envelope as the channel stored it (REA_rodada69 §3): the 17 values of its 0xDA volume record followed
    // by the project's PPQ. When present it replaces attack/decay/sustain/release (delay, hold, tensions, FL's tick
    // stepping); editing the ADSR by hand clears it.
    std::vector<std::int32_t> flVolumeEnvelope;
    // FL's native sampler channel as the project stored it (REA_rodada76): the five 0xDA articulator records (pan,
    // volume, cutoff, resonance, pitch; 17 values each), Mod X, Mod Y and the filter type (0xDB), then the project's
    // PPQ, the project's pan law (FLP event 0x17; 2 = triangular) — 90 values (89: circular). When present the sampler runs FL's voice: the velocity and pan laws with the channel's volume and
    // pan inside the voice, the articulators stepped on the song's tick grid with FL's gain ramps, and the channel filter
    // (core/FlVoiceProcessor.h); ADSR, cutoff/resonance/type, the modulation envelopes and LFOs above are then unused.
    // Editing the envelope or the filter by hand clears it.
    std::vector<std::int32_t> flChannel;
    // FL Studio Mobile's sample slot as the .flm stored it (round 190bs, core/FlmPad.h): EG A, EG D, EG S, EG R, Length
    // and Sample Start (0…1 each), then 1 when the lane holds its notes — 7 values. When present the sampler runs FLM's
    // pad envelope (one-pole segments, a hit is a one-shot unless the lane holds notes) instead of
    // attack / decay / sustain / release; editing the ADSR by hand clears it.
    std::vector<float> flmPad;

    SamplePrecompute precompute;
    bool useLoopPoints = true; // FL's "Use loop points": the file's own loop (WAV smpl) plays (.flp 0x8F bit 3)
    // an FL plugin generator played by the sampler (Slicex): the note velocity reaches the level through
    // FL's Init.Vol law (11^(v·0.01) − 1)/10, unity at velocity 100, instead of LYNX's linear velocity (REA_rodada76 §1,
    // REA_rodada170i §6)
    bool flVelocityLaw = false;
    // FL's "Declicking mode" (.flp 0xD7 byte 0x53, REA_rodada167h §3): 0 out only, 1 transient (no bleeding),
    // 2 transient, 3 generic, 4 smooth, 5 crossfade — how fast a cut voice fades out (10 / 10 / 20 / 20 / 100 / 200 ms)
    int declickMode = 0;
    // Round 190dh, FL's Sampler page:
    // Time stretching › MUL (FL's StretchMulWheel, -20000…20000, a factor 2^(v/10000)). stretchBeats stays the length the
    // sample plays (TIME × the factor, as the .flp import computes it); the page shows TIME = stretchBeats / factor.
    int stretchMul = 0;
    // Playback › loop START / END (FL's LoopStartWheel / LoopEndWheel, REA round 190dh: frames of the sample, inclusive,
    // kept in FL's .flp event 0xFB). -1: the file's own loop (WAV smpl). Played only with "Use loop points".
    std::int64_t loopStart = -1, loopEnd = -1;
    bool hasLoopOverride() const noexcept { return loopStart >= 0 && loopEnd >= loopStart; }
    // Content (FL's .flp 0x8F bits 8 / 0 / 1 / 2): FL's load options of the sample, kept for FL projects. LYNX keeps
    // every sample in memory (it streams files over 30 s from its disk cache by itself), resamples in real time, and its
    // sampler does not play the file's regions or ACID markers itself, so they change nothing audible in LYNX.
    bool keepOnDisk = false, resampleOnLoad = false, loadRegions = true, loadSliceMarkers = false;

    bool operator== (const SamplerSettings&) const = default;
};

// The Sampler's knobs as automation targets (issue #12): a sampler channel's AutomationTarget { channelParam, channel, 0, k }
// with k one of these. Values 0..1 follow the knobs' own curves (the Sampler settings page), so a clip drawn at 50% is
// the knob at mid travel.
namespace samplerparam
{
enum : int { attack, decay, sustain, release, cutoff, resonance, start, end, count };
const char* name (int param) noexcept;              // "Attack", "Cutoff"... (TR_NOOP-marked in Song.cpp)
float toValue (int param, float natural) noexcept;  // seconds / Hz / fraction -> 0..1
float toNatural (int param, float value) noexcept;  // 0..1 -> seconds / Hz / fraction
float get (const SamplerSettings& s, int param) noexcept;          // 0..1
void set (SamplerSettings& s, int param, float value) noexcept;     // 0..1
} // namespace samplerparam

// Channel arpeggiator (FL's Channel settings).
struct ArpSettings
{
    // 0 off, 1 up, 2 down, 3 up and down (bounce: the ends once), 4 random, 5 up and down "sticky" (the ends twice;
    // FL's 4th direction, REA round 146b [EXEC])
    int direction = 0;
    int range = 1;           // octaves, 1..4
    float stepBeats = 0.25f; // duration of each step in beats (0.25 = sixteenth note)
    // the note's length as a fraction of the step; above 1 (FL's gate past 100 %, up to 128/48) the next step waits
    // for the note to end (REA round 146b [EXEC]: FL's step = time · max(1, gate))
    float gate = 0.5f;
    int repeat = 1;          // LYNX: times each note plays before the next one
    // FL's Repeat (REA round 146b [EXEC]): the sequence plays this many times in an octave before the next octave of
    // the range (1 = once)
    int octaveRepeat = 1;
    // FL's Chord (REA round 146b [EXEC]): empty = "Auto" (the held notes); else the chord the arpeggio plays, as
    // semitones above the lowest held note (FL's index k = line k of its Chords.map: 1 Major, 5 minor, 27 Maj7…)
    std::vector<int> chord;
    bool chordSustain = true; // "Auto (sustain)": the arpeggio keeps the notes of the chord while one is held
    bool slide = false;       // FL's Slide: each arpeggio note glides from the previous one (no retrigger)

    bool enabled() const noexcept { return direction > 0; }
    bool operator== (const ArpSettings&) const = default;
};

// Built-in 3-oscillator synth (in the spirit of FL's 3xOsc).
enum class Waveform : std::uint8_t
{
    sine,
    triangle,
    saw,
    square,
    noise,
    spectral // channel harmonic table (SynthSettings::spectrum): imported additive timbres (Harmless)
};

struct SynthOscillator
{
    Waveform wave = Waveform::saw;
    int octave = 0;      // -3..3
    float detune = 0.0f; // cents, -100..100
    float level = 0.0f;  // 0..1
    int semitones = 0;   // -11..11, added to the octave (3x Osc's "coarse" goes up to ±24)
    float pan = 0.0f;    // -1..1, linear balance as in FL

    bool operator== (const SynthOscillator&) const = default;
};

struct SynthSettings
{
    std::array<SynthOscillator, 3> osc { SynthOscillator { Waveform::saw, 0, 0.0f, 1.0f }, SynthOscillator {}, SynthOscillator {} };
    float attack = 0.005f; // seconds
    float decay = 0.3f;
    float sustain = 1.0f;  // 0..1
    float release = 0.25f;
    float cutoff = 2500.0f;  // Hz
    float resonance = 0.0f;  // 0..1
    int filterType = 0;      // 0 low-pass, 1 band-pass, 2 high-pass, 3 band-stop
    bool ladder = false;     // 4-pole ladder (Toxic Biohazard's filter, REA_rodada26 §2) instead of the 2-pole SVF
    int chorus = 0;          // Kepler's chorus on the channel output: 0 off, 1 I, 2 II, 3 I+II (REA_rodada26 §3)
    // Harmless (REA_rodada27 §1.1/§1.2): stereo tremolo depth d (2.5 Hz, |1.25·cos|/|1.25·sin| per side) and vibrato depth
    // in cents (6 Hz), both on free-running global phases; a voice fades into them over 0.1 s / 1/3 s (r²)
    float stereoTremolo = 0.0f;
    float vibratoCents = 0.0f;
    float keyTrack = 0.0f;   // cutoff follows the note: octaves per octave from key 48 (C3, 130.81 Hz)
    // kick pitch sweep: f = f_note·(1 + sweep·s⁴), s = e^(−t/sweepTime) (BassDrum's "Peak"/"Slide")
    float sweep = 0.0f;      // 0 = off
    float sweepTime = 0.1f;  // seconds (time constant of s)
    // 3-band tone control after the filter (BooBass's EQ): low < ~178 Hz < mid < ~1.18 kHz < high,
    // split by 1-pole low-passes; linear gains (1, 1, 1 = off)
    std::array<float, 3> bandGain { 1.0f, 1.0f, 1.0f };
    // "spectral" shape: amplitude and phase (in cycles) of harmonics 1…256, as a sum of sines
    static constexpr int spectrumSize = 256;
    std::array<float, spectrumSize> spectrum {};
    std::array<float, spectrumSize> spectrumPhase {};
    // per-harmonic decay: seconds for harmonic h to fall 60 dB from the note start (0 = holds); lets the
    // spectrum evolve during the note (Ogun's per-partial decay)
    std::array<float, spectrumSize> spectrumDecay {};
    // level each harmonic decays towards (0 = silence): A(t) = target + (A₀ − target)·10^(−3t/decay) — Ogun's ALT DECAY
    std::array<float, spectrumSize> spectrumTarget {};
    ModEnvelope filterEnv;   // shifts the cutoff (octaves)
    ModEnvelope pitchEnv;    // shifts the pitch (semitones)
    std::array<ChannelLfo, lfoTargets> lfo {}; // per LfoTarget
    bool mono = false;       // single voice: the new note takes over the sounding voice (no click)
    bool porta = false;       // portamento: the new note glides from the previous one
    float portaBeats = 0.125f; // glide time, in beats (FL's default: half a step)
    bool am = false;
    bool ring = false;       // with `am`: Sytrus ring modulation, (osc 1 + 2)·(1 − g3 + g3·osc 3) instead of 1 − g3·osc 3         // osc 3 modulates the amplitude of osc 1 + osc 2 (3x Osc's "AM")
    float fm = 0.0f;         // FM index: osc 3 (muted) shifts the phase of osc 1 + 2 by fm·output cycles; 0 = off
    float fmDecay = 0.0f;    // seconds for the index to fall to fmSustain (0 = constant index)
    float fmSustain = 1.0f;  // fraction of the index that remains after the decay

    bool operator== (const SynthSettings&) const = default;
};

// Zone of a multi-sample sampler (like FL's DirectWave/FPC zones): the sample plays on the keys and
// velocities in range, tuned relative to the root note.
struct SampleZone
{
    std::string path;
    int rootKey = 60;
    int lowKey = 0, highKey = 127;
    int lowVelocity = 0, highVelocity = 127; // 0..127
    float tune = 0.0f;  // semitones
    float gain = 1.0f;
    std::int64_t loopStart = 0, loopEnd = 0; // in sample frames; loopEnd <= loopStart = no loop
    int loopMode = 0;   // 0 no loop, 1 continuous, 2 while the note is held

    bool operator== (const SampleZone&) const = default;
};

// FL's Patcher (REA_rodada141): a user-editable graph of plugins, in a mixer slot (EffectKind::patcher) or as a
// Channel Rack generator (InstrumentKind::patcher). Nodes are effects or instruments; wires carry audio (summed with
// a linear gain, FL's IPP multiply-add), notes (MIDI) or control values (a controller's output moving a parameter).
// The two host nodes are "From LYNX" (patchFromHost: the slot's audio input, its side-chain, the channel's notes)
// and "To LYNX" (patchToHost: the main audio output and the extra outputs to other mixer inserts).
struct PatchNode; // defined after EffectSlot
struct RackDoc;   // defined after PatchNode (round 183c)
constexpr int patchFromHost = -1, patchToHost = -2;
// FL's Patcher "Surface" node (REA round 151): its control outputs are the Surface tab's controls
constexpr int patchSurface = -3;

// A control on the Patcher's Surface tab (FL's Patcher › Surface, REA round 151 [EXEC FL 2026: Add control › Bevel,
// Button, CheckBox, Digits, Label, Keyboard, Knob, Slider, XY; each one becomes a control output of the Map's
// "Surface" node, cabled to any node parameter]). LYNX's own drawing; knob, slider, button, check box and XY pad.
struct SurfaceControl
{
    enum class Kind : std::uint8_t { knob, slider, button, checkBox, xy };
    Kind kind = Kind::knob;
    std::string name;
    float value = 0.0f, valueY = 0.0f; // 0..1 (an XY pad has two)
    float x = 40.0f, y = 40.0f;        // place on the Surface tab
    float size = 1.0f;                 // a Control Surface / Dashboard control's scale (round 164; 0.5…3)
    // A Dashboard control that sends MIDI on the Dashboard's port (round 165, FL's record types): none = a controller
    // output only; cc, rpn, nrpn (parameter number midiNumber), channel pressure, note (key midiNumber). The value
    // 0..1 maps to midiLow…midiHigh (high > 127: 14-bit).
    enum class Midi : std::int8_t { none = -1, cc = 0, rpn = 1, nrpn = 2, pressure = 3, note = 5 };
    Midi midi = Midi::none;
    int midiNumber = 0;
    int midiLow = 0, midiHigh = 127;
    // control outputs: one each, an XY pad two (X then Y)
    int outputs() const noexcept { return kind == Kind::xy ? 2 : 1; }
    bool operator== (const SurfaceControl&) const = default;
};

// A rack zone (round 183c, docs/analise/REA_rodada183c.md §3; Live's KeyRange / VelocityRange / BranchSelectorRange):
// the chain is open on [lo, hi], at full level on [fadeLo, fadeHi], fading in over [lo, fadeLo] and out over
// [fadeHi, hi] (lo ≤ fadeLo ≤ fadeHi ≤ hi, values 0…127).
struct RackZone
{
    int lo = 0, hi = 127, fadeLo = 0, fadeHi = 127;
    bool operator== (const RackZone&) const = default;
};

// A rack chain's note filter on a MIDI cable (round 183c): the notes a cable passes and their velocity in the fades.
// `selector` = the Surface output that holds the chain selector (−1: no chain-select zone); `selectorScalesVelocity`:
// a MIDI Effect Rack's chain-select fade attenuates velocity (Instrument Racks fade the chain's output instead).
struct PatchZone
{
    RackZone key, velocity { 1, 127, 1, 127 }, select;
    int selector = -1;
    float selectorMin = 0.0f, selectorMax = 1.0f; // the selector's range over the Surface value (a macro-mapped selector)
    bool selectorScalesVelocity = false;
    bool selectorFiltersCtrl = false; // Live's "Chain Selector Filters MIDI Ctrl"
    bool operator== (const PatchZone&) const = default;
};

struct PatchWire
{
    enum class Kind : std::uint8_t { audio, midi, control };
    Kind kind = Kind::audio;
    int from = patchFromHost; // source node index (or patchFromHost)
    int fromPort = 0;         // audio: output bus (host: 0 = input, 1 = side-chain); control: the source's control output
    int to = patchToHost;     // destination node index (or patchToHost)
    int toPort = 0;           // audio: input bus (0 = main, 1 = side-chain; host: 0 = main output, k = extra output k);
                              // control: the destination's parameter index
    float gain = 1.0f;        // audio: linear cable gain
    bool enabled = true;      // FL's disabled cable: ignored in the sum
    std::vector<float> curve; // control: value map over 0..1 (≥ 2 points; empty = straight); audio with gainSource: the
                              // gain map over the source's value
    // Rack chains (round 183c, built by core/Rack.h only, never saved with the cable): a MIDI cable's note filter
    // (PatcherDoc::zones index, −1 none); an audio cable's pan (−1..1) and the Surface output that scales its gain
    // through `curve` (the chain-select fade; −1 none)
    int zone = -1;
    float pan = 0.0f;
    int gainSource = -1;
    // MIDI (round 162): "From LYNX" output pins: fromPort 0 = Notes (the channel's notes), patchMidiPortPin (N) = what
    // reaches the Patcher on MIDI port N; a node's MIDI output (MIDI Out) = fromPort 0; a node's notes input = toPort 0;
    // "To LYNX": toPort N = MIDI port N.

    bool operator== (const PatchWire&) const = default;
};
// The "From LYNX" pin of MIDI port N (0…255), and back (−1 = the Notes pin)
constexpr int patchMidiPortPin (int port) noexcept { return 1 + port; }
constexpr int patchPinMidiPort (int pin) noexcept { return pin - 1; }
constexpr int patchMidiPorts = 256;
// A node's parameter output (round 171b, FL's node menu › Outputs › Parameters): a control cable from a node with
// fromPort = patchParamOutput (p) carries that node's parameter p (its current value, 0..1) to the destination.
// (FL numbers it the other way round: parameter p = p, controller output k = 0x10000000 + k, REA_rodada14 §1.3.1.)
constexpr int patchParamOutputBase = 0x100000;
constexpr int patchParamOutput (int param) noexcept { return patchParamOutputBase + param; }
constexpr bool isPatchParamOutput (int port) noexcept { return port >= patchParamOutputBase; }
constexpr int patchOutputParam (int port) noexcept { return port - patchParamOutputBase; }

// A parameter the Patcher publishes (FL's record 1100): automating the Patcher's parameter k moves this node parameter
// (through `curve`, as a control cable). node −1 = an unused index.
struct PatchParam
{
    int node = -1, param = 0;
    std::vector<float> curve;
    std::string name;

    bool operator== (const PatchParam&) const = default;
};

struct PatcherDoc
{
    std::vector<PatchNode> nodes;
    std::vector<PatchWire> wires;
    std::vector<PatchParam> published; // [Patcher parameter]
    float inputX = 30.0f, inputY = 150.0f, outputX = 560.0f, outputY = 150.0f; // the host nodes in the editor
    // "To LYNX" extra audio outputs (bus 1…): the mixer insert each one feeds (−1 = the k-th insert after the
    // channel's own); generator Patcher only
    std::vector<int> outputTracks;
    // The I/O nodes' MIDI pins the user activated (FL's node menu › Events: Notes, MIDI port 0…255; round 162). A pin
    // with a cable is always shown. fromNotesHidden: "Deactivate" on the Notes pin of "From LYNX".
    std::vector<int> fromMidiPorts, toMidiPorts; // sorted port numbers
    bool fromNotesHidden = false;
    // the Surface tab's controls and the Surface node's place on the Map (round 151)
    std::vector<SurfaceControl> surface;
    float surfaceX = 30.0f, surfaceY = 30.0f;
    // the value of the Surface node's control output k (the controls' outputs in order); 0 past the end
    float surfaceOutput (int k) const noexcept
    {
        for (const auto& c : surface)
        {
            if (k < c.outputs())
                return k == 0 ? c.value : c.valueY;
            k -= c.outputs();
        }
        return 0.0f;
    }
    int surfaceOutputs() const noexcept
    {
        int n = 0;
        for (const auto& c : surface)
            n += c.outputs();
        return n;
    }

    // Rack mode (round 183c, core/Rack.h): the Rack this Patcher shows instead of its Map (0 or 1 element; empty = FL's
    // Patcher). The nodes are the rack's devices; the cables, Surface and published parameters are built from it.
    std::vector<RackDoc> rack;
    std::vector<PatchZone> zones; // the cables' note filters (rack mode)

    bool isRack() const noexcept { return ! rack.empty(); }
    bool empty() const noexcept { return nodes.empty() && wires.empty() && rack.empty(); }
    bool operator== (const PatcherDoc& o) const; // (Song.cpp: needs PatchNode)
};

// FL's "Echo delay / fat mode" (REA round 146b [EXEC: FL renders of the Sampler render test with the 0xD1 record
// changed]): each note repeats `echoes` times, `timeBeats` apart; echo k plays at feedback^k of the note's volume,
// with k·pan added to the note's pan (clamped) and k·pitch cents.
struct EchoSettings
{
    float feedback = 0.0f;   // 0..1 (FL 0..12800): the volume factor from one echo to the next; 0 = off
    float pan = 0.0f;        // −1..1 per echo (FL 0..12800, 6400 = centre)
    float pitch = 0.0f;      // cents per echo (FL ±1200)
    float modX = 0.0f, modY = 0.0f; // −1..1 per echo (FL 0..256, 128 = none): stored; LYNX's notes do not route mod X / Y
    int echoes = 4;          // FL 1..10
    float timeBeats = 0.75f; // the delay between echoes (FL: 1/192 beats; 144 = 3 steps)
    bool pingPong = false;   // echoes alternate their pan side
    bool fat = false;        // FL's fat mode (REA round 149): a mirrored second series, every echo at feedback × the level

    bool enabled() const noexcept { return feedback > 0.0f && echoes > 0; }
    bool operator== (const EchoSettings&) const = default;
};

// FL's Vol / Key tracking (record 0xE4, REA round 146b): the note's velocity (or key) away from `mid` moves its pan
// (and FL's mod X / Y, stored only).
struct NoteTracking
{
    int mid = 0;            // velocity 0..128 (FL default 100) or key 0..131 (FL default 60 = C5)
    float pan = 0.0f;       // −1..1: the pan at the far end of the range
    float modX = 0.0f, modY = 0.0f;

    bool operator== (const NoteTracking&) const = default;
};

// The rest of FL's Misc tab (REA round 146b)
struct ChannelMisc
{
    float shiftBeats = 0.0f; // Time › Shift: every note starts (and ends) this much later
    float gateBeats = 0.0f;  // Time › Gate: notes last at most this long (0 = off)
    bool fullPorta = true;   // Time › Full porta: a porta note glides the whole way (off: FL's partial slide)
    // Polyphony › Mono / Porta / Slide for every generator (round 168l, core/ChannelPoly.h; .flp 0xDD): the rack's
    // channel layer applies them. The LYNX Synth keeps them in SynthSettings, the Sampler its porta in SamplerSettings
    // (RackOps' setChannelMono / setChannelPortamento write every copy)
    bool mono = false;
    bool porta = false;
    float portaBeats = 0.125f; // FL's Slide knob (default 820 = half a step)
    EchoSettings echo;
    NoteTracking velocity { 100 }, key { 60 };
    int pitchRange = 2;      // the header's PITCH knob range, semitones (FL default 2)
    bool mainPitch = true;   // follows the song's main (master) pitch
    // FL's Root note keyboard (REA round 149): the note at which the channel plays at its own pitch (C5 = 60). A
    // sampler's root is in its pitch (SamplerSettings::pitch moves with it); other instruments only follow it with
    // "Add to key" (0xD7 byte 63): their notes are played (60 − root) semitones away
    int rootNote = 60;
    bool addToKey = false;
    // FL's Root note › Fine tune (REA round 151 [DATA: .flp event 0x8E, signed cents]): every note this many cents away
    float fineTune = 0.0f;

    bool operator== (const ChannelMisc&) const = default;
};

// FL's Layer channel (REA_rodada164a §1, FL's manual "Layer channel settings"): it makes no sound; every note played on
// it (Piano roll, steps, keyboard) plays its children channels. A child played from its own notes is not affected.
struct LayerSettings
{
    std::vector<int> children; // channel indices ("Set children": the selected channels, in Channel Rack order)
    bool random = false;     // one child at random per note (else every child)
    bool crossfade = false;  // the Fade knob crossfades the children, top to bottom
    bool sequential = false; // the children in turn (round robin), one per note
    float fade = 0.0f;       // the Fade knob 0..1 (with crossfade)
    // (the Layer's VOL / PAN are the channel's volume / pan, its PITCH the header's pitch knob: sampler.pitch)

    bool operator== (const LayerSettings&) const = default;
};

// Online Sessions (docs/design/ONLINE_SESSIONS.md §4.2, round 168a): a render of a third-party plugin part, made by the
// computer that has the plugin, for the computers that don't. A channel's stand-ins are its instrument's output before
// its mixer insert (the channel's own volume and pan are left out, they still apply live); an insert's is its output
// after its effect chain, before the fader. Saved with the project, so a project sent to a friend and the host's final
// project keep them as the fallback; they only play where the plugin is missing (standins::project).
// The FL plugin an imported channel generator or mixer effect came from (round 171e, Export to FL Studio): its internal
// name (.flp 0xC9) and saved state (0xD5; empty = the same as the channel's pluginState), and a fingerprint of what the
// import made of it (flpexport::fingerprint). While the LYNX side is unchanged (the same fingerprint), Export to FL
// Studio writes the original plugin back byte for byte. Saved with the project.
struct FlOrigin
{
    std::string plugin;
    std::vector<std::uint8_t> state;
    std::uint64_t fingerprint = 0;

    bool empty() const noexcept { return plugin.empty(); }
    bool operator== (const FlOrigin&) const = default;
};

struct StandIn
{
    std::uint64_t pattern = 0; // pattern uid; 0 = the whole song (a timeline stem, Song mode)
    // pattern stems: false = the pattern played once with its tail (Song mode, placed at its clips), true = one loop
    // pass of the pattern looping (the previous pass's tail sounds over its start: Pattern mode)
    bool loop = false;
    std::string path;               // the WAV
    std::string sourceHash;         // what it was rendered from (standins' source hash): differs = out of date
    std::int64_t lengthTicks = 0;   // the stem's length (the music and its tail)
    std::string plugin;             // the plugin it stands in for (for the badge)
    std::string by;                 // who rendered it (the participant's name)
    bool operator== (const StandIn&) const = default;
};

// Freeze (round 190ay): the channel's own output — after its instrument / plugin, before its volume, pan and insert —
// rendered to audio that plays in its place while the instrument is not loaded at all (its RAM and CPU are free). The
// channel itself is untouched: its plugin and settings stay in the project as they were, so unfreezing brings back the
// same sound. `stems`: pattern 0 = the whole song (what Song mode plays), and one loop pass per pattern the channel
// plays in (what Pattern mode plays). `source` = what they were rendered from (sessionpack::freezeSource): another
// value now = the notes, tempo or arrangement changed since, the audio is out of date. Saved only while frozen; a
// LYNX SHARE that does not know the field plays the instrument live.
struct FreezeState
{
    std::vector<StandIn> stems;
    std::string source;
    bool on() const noexcept { return ! stems.empty(); }
    bool operator== (const FreezeState&) const = default;
};

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.78f;  // 0..1 (channelGain curve; 0.78 = FL's default)
    float pan = 0.0f;      // -1..1
    bool muted = false;
    std::string samplePath; // used when instrument == sampler
    std::vector<SampleZone> zones; // multi-sample sampler (empty: samplePath only)
    int mixerTrack = 0;     // 0 = straight to Master
    // FL's Polyphony › Max (REA round 139): at most this many notes sound at once, a new one releases the oldest
    // (0 = unlimited). The Export window's "Disable maximum polyphony" lifts it for the render.
    int maxPolyphony = 0;
    PluginRef plugin;       // used when instrument == plugin
    std::vector<std::uint8_t> controllerState; // instrument == controller: FL Envelope Controller state
    std::vector<float> modelParams; // instrument == sakura / drumpad / boobass / drumsynth / harmless: FL's raw state parameters
                                    // instrument == minisynth: { 1 } = FL Studio Mobile's build of the engine (core/MiniSynth.h)
    std::vector<std::uint8_t> pluginState; // instrument == sytrus / harmor / ogun / gms / morphine / fruitkick / bassdrum: the FL plugin's saved state (record 0xD5)
    SamplerSettings sampler; // used when instrument == sampler
    SynthSettings synth;     // used when instrument == synth
    float swingMix = 1.0f;   // how much of the global swing applies to this channel (0..1), as in FL
    ArpSettings arp;
    int cutGroup = 0;        // playing this channel cuts channels with an equal cutBy (0 = none)
    int cutBy = 0;
    std::uint32_t colour = 0; // 0xFFRRGGBB; 0 = theme default
    int group = -1;           // the Channel Rack filter group (index into Song::channelGroups); -1 = none
    PatcherDoc patcher;       // instrument == patcher: its graph (REA_rodada141)
    ChannelMisc misc;         // FL's Misc tab: time shift / gate, echo delay, velocity / key tracking (REA round 146b)
    // Piano Roll key labels (FL 2026's custom key labels, REA round 153): MIDI key -> the name shown on that key
    std::map<int, std::string> keyLabels;
    // FL's ghost channels (the Piano Roll's ghost notes button menu, round 166d): this channel's notes are not shown as
    // ghosts in the other channels' Piano Roll
    bool ghostHidden = false;
    // FL's "Lock active/mute state" (the mute switch's menu, .flp 0x20, REA_rodada167l §5): soloing another channel
    // leaves this one as it is
    bool soloLock = false;
    // FL's key region (the Channel settings keyboard's ruler, REA_rodada164a §2): the channel plays only the notes
    // from keyLow to keyHigh (MIDI keys); 0…127 = every note. Layer's "Split children" sets one key per child.
    int keyLow = 0, keyHigh = 127;
    LayerSettings layer; // instrument == layer: its children and options
    // FL's "Receive notes from" (the channel menu, round 165f-rack): the notes of this MIDI input device (and MIDI channel,
    // 0 = any) play only this channel, whatever is selected in the Channel Rack. Empty = not locked.
    std::string receiveFrom;
    int receiveMidiChannel = 0;
    // Online Sessions (round 168a): the channel's stable id (uids::normalize; its participant tag in the top bits says
    // whose it is) and the renders of its third-party plugin for computers without it
    std::uint64_t uid = 0;
    std::vector<StandIn> standIns;
    FreezeState freeze; // (round 190ay) frozen: its stems play, its instrument is not loaded
    // FL's channel icon (.flp 0x9B, REA round 168j): FL's icon index, 0 = none. Kept and saved; drawn by the icon UI.
    int icon = 0;
    int flWrapperFlags = -1; // an imported .flp channel's plugin record flags (0xD4 +16, 0x20 = Smart disable): back in the export
    // FL's Loop Starter (round 168b, core/LoopStarter.h): the channel belongs to the Loop Starter of pattern
    // `loopStarterPattern` (-1 = not a Loop Starter channel), plays the one-shot category `loopStarterCategory` in kit slot
    // `loopStarterSlot`; locked = "Replace all samples" leaves its sample (FL's channel menu "Locked")
    int loopStarterPattern = -1;
    std::string loopStarterSlot, loopStarterCategory;
    bool loopStarterLocked = false;
    FlOrigin flOrigin;    // imported from FL: the original generator (round 171e)
    InstanceTag instance; // runtime identity of the channel (never saved)

    bool hasKeyRegion() const noexcept { return keyLow > 0 || keyHigh < 127; }
    bool inKeyRegion (int key) const noexcept { return key >= keyLow && key <= keyHigh; }
    bool operator== (const ChannelDef&) const = default;
};

enum class EffectKind : std::uint8_t
{
    eq,
    compressor,
    delay,
    reverb,
    plugin, // external effect (VST3/CLAP), created by the app
    limiter,
    filter,
    distortion,
    chorus,
    parametricEq, // 7 bands (like FL's Parametric EQ 2)
    utility,      // gain, pan and stereo matrix (FL's Balance, PanOMatic, Stereo Shaper)
    volumeGate,   // per-step gain synced to the bar (Gross Beat's volume)
    peakController, // does not change the sound: measures the peak and serves as a source for live automation (FL's Peak Controller)
    pitchShift,   // changes pitch without changing duration (two read heads crossfaded)
    freqShift,    // shifts all frequencies by Hz (single sideband, does not preserve harmonics)
    parallel,     // container: parallel effect branches summed (FL's Patcher); see EffectSlot::branches
    lofi,         // sample-rate reduction (sample and hold) and bit reduction (Lo-fi from FL's Effector)
    effector,     // FL's Effector: 12 algorithms with X/Y, LFO and Mod Rate (core/Effector.h)
    crossover,    // one band of a frequency splitter (FL's Frequency Splitter): Linkwitz-Riley high-pass and/or low-pass
    rhythmGain,   // gain following a curve of points in beats, looped, locked to the song (Love Philter envelopes)
    autoPan,      // pan moved by a triangle LFO locked to the song (Love Philter pan LFO)
    vocoder,      // Fruity Vocoder channel vocoder: L modulates, R is the carrier (mono output)
    pitchCorrect, // pitch correction (FL's Pitcher): detects the pitch and pulls it to the nearest allowed note
    squeeze,      // FL's Fruity Squeeze, exact (REA_rodada29 §2): quantiser, periodic gate, filter; raw values / max
    vintagePhaser, // FL's Vintage Phaser (REA_rodada29 §3, core/VintagePhaser.h): 36 normalised engine values
    vocodex,      // FL's Vocodex (REA_rodada35, core/Vocodex.h): built from FL's raw state (EffectSlot::state)
    maximus,      // FL's Maximus / Soundgoodizer engine (REA_rodada36/38, core/MaximusFx.h): built from EffectSlot::state
    fruityLimiter,   // FL's Fruity Limiter, exact (REA_rodada48, core/FruityDynamics.h): FL's raw controls / range
    fruityCompressor, // FL's Fruity Compressor, exact (REA_rodada48, core/FruityDynamics.h): FL's raw controls / range
    fruityWaveShaper, // FL's Fruity WaveShaper, exact (REA_rodada51, core/FruityShapers.h): curve in EffectSlot::state, controls / range
    fruitySoftClipper, // FL's Fruity Soft Clipper, exact (REA_rodada51, core/FruityShapers.h): threshold, post / range
    reeverb2,     // FL's Fruity Reeverb 2, exact (REA_rodada49, core/Reeverb2.h): FL's 15 controls + 2 options, (raw − min) / range
    reeverb1,     // FL's Fruity Reeverb (first version), exact (REA_rodada49 §6, core/Reeverb2.h): FL's 10 controls, raw / max
    fruityDelay2, // FL's Fruity Delay 2, exact (REA_rodada52, core/FruityDelay.h): FL's 8 raw controls / range
    fruityDelay3, // FL's Fruity Delay 3, exact (REA_rodada52, core/FruityDelay.h): FL's 26 raw controls / range
    stereoShaper, // FL's Fruity Stereo Shaper, exact (REA_rodada54, core/StereoFx.h): FL's 8 controls / range + difference flag
    stereoEnhancer, // FL's Fruity Stereo Enhancer, exact (REA_rodada54, core/StereoFx.h): FL's 6 controls / range
    transientProcessor, // FL's Transient Processor, exact (REA_rodada54, core/StereoFx.h): FL's 9 controls / range
    spreader,     // FL's Spreader, exact (REA_rodada54, core/StereoFx.h): FL's 5 controls / range
    fruityChorus, // FL's Fruity Chorus, exact (REA_rodada59 §1, core/ChorusFx.h): controls / range
    hyperChorus,  // FL's Hyper Chorus, exact (REA_rodada59 §2, core/ChorusFx.h): (raw − min) / range; instance in EffectSlot::state
    vintageChorus, // FL's Vintage Chorus, exact (REA_rodada59 §3, core/ChorusFx.h): (raw − min) / range; instance in EffectSlot::state
    fruityPanOMatic, // FL's Fruity PanOMatic, exact (REA_rodada58, core/FruityBasicFx.h): pan, volume, LFO; controls / range
    fruityBalance,   // FL's Fruity Balance, exact (REA_rodada58, core/FruityBasicFx.h): balance, volume; controls / range
    fruityBloodOverdrive, // FL's Fruity Blood Overdrive, exact (REA_rodada58, core/FruityBasicFx.h): 6 controls / range
    grossBeat,    // FL's Gross Beat engine (REA_rodada53, core/GrossBeat.h): slots in EffectSlot::state, parameters in params
    fruityDelayBank, // FL's Fruity Delay Bank, exact (REA_rodada56, core/FruityDelayBank.h): FL's state in EffectSlot::state, params = its 4 global controls / 128
    fruityMultiband, // FL's Fruity Multiband Compressor, exact (REA_rodada56, core/FruityMultiband.h): FL's raw controls / range (+ FL's state)
    fruityFastDist, // FL's Fruity Fast Dist, exact (REA_rodada55, core/FlangusFastDist.h): controls / range
    fruityFlangus,  // FL's Fruity Flangus, exact (REA_rodada55, core/FlangusFastDist.h): controls / range
    fruityFlanger,  // FL's Fruity Flanger, exact (REA_rodada55, core/FlangusFastDist.h): controls / range
    luxeVerb,     // FL's LuxeVerb, exact (REA_rodada60, core/LuxeVerb.h): FL's 49 controls (raw − min) / range + 4 sync switches
    convolver,    // FL's Fruity Convolver, exact (REA_rodada60, core/ConvolverFx.h): FL's 4 controls as (raw − min) / range; impulse response, knobs and envelopes in EffectSlot::state
    equo,         // FL's EQUO, exact (REA_rodada61, core/EquoFx.h): FL's state in EffectSlot::state, knobs (raw − min) / range
    flPitchShifter, // FL's Pitch Shifter, exact (REA_rodada61, core/PitchShifterFl.h): FL's 13 knobs (raw − min) / range + 3 options
    frequencySplitter, // FL's Frequency Splitter, exact (REA_rodada62, core/FrequencySplitter.h): 26 engine values + output bands
    fruityFastLP, // FL's Fruity Fast LP, exact (REA_rodada63 §1, core/FruityFilters.h): cutoff, resonance; controls / range
    fruityFreeFilter, // FL's Fruity Free Filter, exact (REA_rodada63 §2, core/FruityFilters.h): 4 controls / range + CENTER
    fruityFilter, // FL's Fruity Filter, exact (REA_rodada63 §3, core/FruityFilters.h): 6 controls / range + CENTER
    hardcore,     // FL's Hardcore, exact (REA_rodada71, core/HardcoreFx.h): FL's 50 parameters (cabinet / 20, types / 11); impulses in EffectSlot::state
    distructor,   // FL's Distructor, exact (REA_rodada71, core/DistructorFx.h): FL's state (+ cabinet impulses) in EffectSlot::state, module gains / 10000 + clipper
    multibandDelay, // FL's Multiband Delay, exact (REA_rodada71, core/MultibandDelayFx.h): FL's state in EffectSlot::state, 12 globals (raw − min) / range
    lovePhilter,   // FL's Fruity Love Philter, exact (REA_rodada66, core/LovePhilter.h): FL's state in EffectSlot::state, 48 controls
    newtone,      // FL's Newtone (REA_rodada73, core/NewtoneFx.h): pass-through; in slave mode adds its source sample at the song time
    newtime,      // FL's Newtime (REA_rodada73, core/NewtoneFx.h): the same contract (its time warping is not reproduced)
    facetEq,      // LYNX's Facet EQ (MEASURE_fabfilter §9, core/FacetEq.h): 8 matched analog-prototype bands, 42 controls
    apexLimiter,  // LYNX's Apex Limiter (MEASURE_fabfilter §9, core/ApexLimiter.h): look-ahead true-peak limiter, 7 controls
    ridgeStrip,   // LYNX's channel strip (REA_rodada96, core/MixSuite.h): filters, 4-band EQ, compressor, gate / expander
    glowLeveler,  // LYNX's optical leveler (REA_rodada96, core/MixSuite.h)
    hushDeEsser,  // LYNX's de-esser (REA_rodada96, core/MixSuite.h)
    aether,       // LYNX's air band + exciter (REA_rodada96, core/MixSuite.h)
    ember,        // LYNX's saturator (REA_rodada96, core/MixSuite.h)
    lynxTune,     // LYNX Tune, LYNX's own real-time vocal pitch corrector (REA_rodada95, core/LynxTune.h)
    fruityPhaser, // FL's Fruity Phaser, exact (REA_rodada105_phaser, core/FruityPhaser.h): FL's 9 controls / max
    epochVerb,    // LYNX's Epoch Verb (REA_rodada100, core/LynxVerbs.h): multi-mode vintage-digital reverb, 17 controls
    atrium,       // LYNX's Atrium (REA_rodada100, core/LynxVerbs.h): room reverb, early + late sections, 22 controls
    foilPlate,    // LYNX's Foil Plate (REA_rodada100, core/LynxVerbs.h): plate reverb, 12 controls
    nebula,       // LYNX's Nebula (REA_rodada100, core/LynxVerbs.h): delay-reverb network, 13 controls
    halo,         // LYNX's Halo (REA_rodada100, core/LynxVerbs.h): shimmer reverb, 12 controls
    horizon,      // LYNX's Horizon (REA_rodada100, core/LynxVerbs.h): echo + reverb, 24 controls
    echoform,     // LYNX's multi-mode echo (REA_rodada101, core/EchoSuite.h), 29 controls
    spiralEcho,   // LYNX's frequency-shifting echo (REA_rodada101, core/EchoSuite.h), 8 controls
    orbit,        // LYNX's modulation machine: flangers / choruses / doubler (REA_rodada101, core/EchoSuite.h), 6 controls
    latticeMod,   // LYNX's multitap modulated delay (REA_rodada101, core/EchoSuite.h), 34 controls
    fruitySend,   // FL's Fruity Send, exact (REA_rodada105_send, core/FruityBasicFx.h): pan, volume, dry / range + the insert it sends to
    crestCompressor, // LYNX's Crest Compressor (REA_rodada108, core/DynamicsSuite.h): styles, side-chain EQ, character, oversampling
    prismMultiband,  // LYNX's Prism Multiband (REA_rodada108, core/DynamicsSuite.h): 4-band dynamics
    sentryGate,      // LYNX's Sentry Gate (REA_rodada108, core/DynamicsSuite.h): gate / expander
    quellDenoiser,   // LYNX's Quell Denoiser (REA_rodada119, core/VoiceTools.h): one-knob multiband noise suppressor
    geminiDoubler,   // LYNX's Gemini Doubler (REA_rodada119, core/VoiceTools.h): two pitch-shifted delay voices
    formulaController, // FL's Formula Controller (REA_rodada133, core/FormulaController.h): no sound; output = formula of a, b, c
    patcher,         // FL's Patcher (REA_rodada141, core/Patcher.h): a graph of effects / instruments in EffectSlot::patcher
    voiceShift,      // LYNX's Voice Shift (REA_rodada135s, core/VoiceShift.h): pitch / formant / quantize / robot + drive
    emphasizer,      // FL's Emphasizer, exact (REA_rodada154, core/EmphasizerFx.h): FL's 6 controls / max
    flFrequencyShifter, // FL's Frequency Shifter, exact (REA_rodada154, core/FreqShifterFl.h): FL's 10 controls (raw − min) / range
    flLowLifter,     // FL's Low Lifter, exact (REA_rodada154, core/LowLifterFl.h): FL's 15 controls / max
    flPhaseInverter, // FL's Fruity Phase Inverter, exact (REA_rodada165k, core/FlLegacyFx.h): its control / 1024
    flCenter,        // FL's Fruity Center, exact (REA_rodada165k, core/FlLegacyFx.h): on / off
    flBassBoost,     // FL's Fruity Bass Boost, exact (REA_rodada165k, core/FlLegacyFx.h): frequency, amount / 1024
    fl7BandEq,       // FL's Fruity 7 Band EQ, exact (REA_rodada165k, core/FlLegacyFx.h): 7 gains (raw + 1800) / 3600
    flDelay1,        // FL's Fruity Delay (v1), exact (REA_rodada165k, core/FlLegacyFx.h): 6 controls (raw − min) / range
    waveCandy,       // LYNX's analyser in FL's Wave Candy role (REA_rodada164c, core/VisualFx.h): no sound; scope, spectrum, meters
    visualizer,      // LYNX's Visualizer in FL's ZGameEditor Visualizer role (REA_rodada164c): no sound; scene in EffectSlot::state
    xyController,    // FL's X-Y Controller (REA_rodada164a, core/XyController.h): no sound; outputs X, Y follow a pad
    xyzController,   // FL's X-Y-Z Controller (REA_rodada164a, core/XyController.h): the same with a Z axis (mouse wheel)
    controlSurface,  // FL's Control Surface (REA_rodada164a, core/ControlSurface.h): a user panel; output k = params[k]
    hardwareInsert,  // LYNX's hardware insert (REA_rodada164d, core/HardwareInsert.h): send to device outputs, return from inputs, pinged round trip
    fruityMute2,     // FL's Fruity Mute 2, exact (REA_rodada165g §1, core/FlExtraFx.h): mute, channel (L, L + R, R)
    bigClock,        // FL's Fruity Big Clock (REA_rodada165g §2, core/FlExtraFx.h): no sound; the song position in big digits
    tuner,           // FL's Tuner (REA_rodada165g §3, core/FlExtraFx.h): no sound; chromatic tuner fed by the slot's tap
    htmlNotebook,    // FL's Fruity HTML NoteBook (REA_rodada165g §4, core/FlExtraFx.h): no sound; its page's address in EffectSlot::state
    scratcher,       // FL's Fruity Scratcher (REA_rodada165g-flscratch165, core/Scratcher.h): turntable over EffectSlot::samplePath, added to the input
    transmitter,     // FL's Transmitter (REA_rodada165g-fltrans165, core/Transmitter.h): transient / sustain splitter, a section effect on each part
    flEmphasis,      // FL's Emphasis, exact (REA_rodada167f, core/EmphasisFx.h): FL's 13 controls + 3 options, (raw − min) / range
    flTransporter,   // FL's Transporter, exact (REA_rodada167f-transporter, core/TransporterFl.h): FL's 22 controls (raw − min) / range + source, stereo, send
    kilnChannel,     // LYNX's console channel (round tp172c7, core/MixSuite.h kiln): six measured console models, drive, noise, group bypass
    fretChannel,     // LYNX's guitar / bass channel strip (round tp172c8, core/MixSuite.h fret): phase correction, EQ, compressor, amp
    shellChannel,    // LYNX's drum channel strip (round tp172c8, core/MixSuite.h shell): gate, transient shaper, enhancers, listen-mic compressor
    pocketDistortion,     // Pocket Distortion, FL Studio Mobile's Distortion, exact (round 188h, core/PocketFx.h): FLM's 6 values
    pocketLimiterClassic, // Pocket Limiter Classic, FL Studio Mobile's Limiter (legacy), exact (round 188h, core/PocketFx.h): FLM's 3 values
    pocketLimiter,         // Pocket Limiter, FL Studio Mobile's Limiter, exact (round 188h, core/PocketFx.h)
    pocketLeveller,        // Pocket Leveller, FL Studio Mobile's Leveller, exact (round 188h, core/PocketFx.h)
    pocketGate,            // Pocket Gate, FL Studio Mobile's Gate, exact (round 188h, core/PocketFx.h)
    pocketTranceDelay,     // Pocket Trance Delay, FL Studio Mobile's Trance Delay, exact (round 188h, core/PocketFx.h)
    pocketTapeDelay,       // Pocket Tape Delay, FL Studio Mobile's Tape Delay, exact (round 188h, core/PocketFx.h)
    pocketMultiFx,         // Pocket MultiFX, FL Studio Mobile's MultiFX, exact (round 188h, core/PocketMultiFx.cpp): an X/Y pad over 8 effects
    strikeShaper,     // LYNX's level-independent transient designer (round trans189d, issue #94, core/StrikeShaper.h): attack / sustain ±15 / ±24 dB
    pocketAutoduck,        // Pocket Autoduck, FL Studio Mobile's Autoduck, exact (round 189h, core/PocketFx.h)
    pocketChorus,          // Pocket Chorus, FL Studio Mobile's Chorus, exact (round 189h, core/PocketFx.h)
    pocketSpacer,          // Pocket Spacer, FL Studio Mobile's Spacer, exact (round 189h, core/PocketFx.h)
    pocketStereoizer,       // Pocket Stereoizer, FL Studio Mobile's Stereoizer, exact (round 189h, core/PocketFx.h)
    pocketWowFlutter,      // Pocket Wow & Flutter, FL Studio Mobile's Wow & Flutter (round 189h, core/PocketFxB.cpp)
    pocketPhaser,          // Pocket Phaser, FL Studio Mobile's Phaser (round 189h, core/PocketFxB.cpp)
    pocketFlanger,         // Pocket Flanger, FL Studio Mobile's Flanger (round 189h, core/PocketFxB.cpp)
    pocketTremolo,          // Pocket Tremolo, FL Studio Mobile's Tremolo (round 189h, core/PocketFxB.cpp)
    pocketWaveshaper,      // Pocket Waveshaper, FL Studio Mobile's Waveshaper, exact (round 189h, core/PocketFxC.cpp): FLM's 20 values
    pocketCompressor,      // Pocket Compressor, FL Studio Mobile's Compressor, exact (round 189h, core/PocketFxC.cpp): FLM's 9 values
    pocketSpreader,        // Pocket Spreader, FL Studio Mobile's Spreader, exact (round 189h, core/PocketFxC.cpp): FL's Spreader engine, FLM's 4 values
    pocketComb, // Pocket Comb Filter, FL Studio Mobile's Comb Filter (round 190by, core/PocketFxD.cpp): FLM's 11 values
    pocketMultiband, // Pocket Multiband Compressor, FL Studio Mobile's Multiband Compressor (round 190by, core/PocketFxD.cpp): FLM's 30 values
    pocketTapeStop, // Pocket Tape Stop, FL Studio Mobile's Tape Stop (round 190by, core/PocketFxD.cpp): FLM's 7 values
    rackMixer,       // a rack chain's mixer (round 183c, core/Rack.h): volume (Live's chain taper), pan, chain-select gain; inside racks only
    edison           // FL's Edison (round 190cd, core/EdisonFx.h): an audio editor in the slot — records the slot's input, plays into it; its audio in samplePath
};
// the last EffectKind (loops over every kind; update when one is appended)
constexpr EffectKind lastEffectKind = EffectKind::edison;

// round 165x: 160 → 192 (Ember had reached 160; a slot's parameters are 768 bytes, project files store only an engine's
// own count, so older projects load unchanged). Round 150: 128 → 160 (Prism Multiband 155, Ember 133).
constexpr int maxEffectParams = 192; // LuxeVerb uses 53 (FL's 49 controls + 4 sync switches); Facet EQ 88 (round 128); Echoform 102 (round 144)
constexpr int maxEffectSlots = 10; // as in FL
using EffectParams = std::array<float, maxEffectParams>;

struct EffectSlot;

// A branch of the `parallel` container: a chain of inner effects and the gain it enters the sum with
// (empty chain = the dry signal).
struct EffectBranch
{
    std::vector<EffectSlot> chain;
    float gain = 1.0f;
};

// Live modulation inside a `parallel` container: the control output of child `source` of branch `branch`
// drives parameter `param` of child `target` of the same branch, through `curve` (33 points over 0..1).
struct EffectLink
{
    int branch = 0, source = 0, target = 0, param = 0;
    std::vector<float> curve;
};

struct EffectSlot
{
    EffectKind kind = EffectKind::eq;
    bool enabled = true;
    EffectParams params {}; // 0..1; meaning depends on the effect
    float mix = 1.0f;       // slot dry/wet (as in FL): 0 = dry signal only
    // FL's per-plugin "Smart disable" (REA round 139): on = the slot is skipped while its input and output are silent
    // (built-ins after 1 s, plugins after 4 s); off = always processed. FL's "Switch smart disable for all plugins"
    // turns it on everywhere.
    bool smartDisable = true;
    PluginRef plugin;                             // used when kind == plugin
    int sidechain = -1;   // mixer track whose (post-fader) output feeds the effect's detector (compressor); −1 = none
    int sidechain2 = -1;  // second side input (vocoder carrier); −1 = none
    // Extra stereo outputs sent to other inserts (round 167b, FL's Transmitter: [0] the transient part, [1] the
    // sustain part, after their tilt and section effect and before their level / on switch); 0 = not sent
    std::array<int, 2> sendTracks { 0, 0 };
    std::vector<EffectBranch> branches;           // used when kind == parallel
    std::vector<EffectLink> links;                // used when kind == parallel
    std::vector<std::uint8_t> state;              // FL's raw plugin state, for exact effects built from it (vocodex, maximus)
    std::string samplePath;                       // audio file the effect plays (Newtone/Newtime source sample); "" = none
    // When set (id or foreign), this built-in effect is a LYNX substitute for that missing third-party plugin
    // (core/PluginSubstitutes.h): the original reference is kept byte for byte, saved with the project, and the slot
    // goes back to it once the plugin is installed.
    PluginRef substituteFor;
    // The editor's header (REA round 129, the originals' preset bar and A/B compare): the name of the preset loaded
    // into the slot ("" = none), and the other one of the two settings A / B (its parameters; empty = B was never
    // used) with which of them is playing. Saved with the project; the audio only ever sees `params`.
    std::string presetName;
    std::vector<float> abOther;
    bool abShowingB = false;
    PatcherDoc patcher; // kind == patcher: its graph (REA_rodada141)
    // The mixer row (0..maxEffectSlots-1) the effect sits in (round 170b): FL's slots are places, an effect can sit in
    // slot 5 with slots 1-4 empty. -1 = right after the previous effect (every effect of an old project). The chain
    // order is the vector's order; the rows only grow along it (core/MixerOps.h effectRows).
    int row = -1;
    // Part of a Master look (round 189k S8, core/MasterLooks.h): the look's id, 0 = an ordinary slot. Saved in .lynx;
    // FL export writes such a slot as FL's own plugin with its settings (core/FlStateWrite.h).
    std::uint8_t look = 0;
    // A one-knob effect of the Simple workflow (round 190bu, core/GoalKnobs.h): the goal's id (0 = an ordinary slot) and
    // where its knob stands (0..1). The slot is the goal's ordinary LYNX effect with the parameters the knob wrote, so
    // every layout, the Mixer and the exports see a normal effect. Saved in .lynx.
    std::uint8_t goal = 0;
    float goalAmount = 0.5f;
    FlOrigin flOrigin;    // imported from FL: the original effect plugin (round 171e)
    InstanceTag instance; // runtime identity of the slot (never saved, not compared)

    bool operator== (const EffectSlot& o) const; // every field (Song.cpp)

    bool isSubstitute() const noexcept { return ! substituteFor.id.empty() || substituteFor.foreign.format != ForeignPlugin::Format::none; }
};

// A Patcher node (REA_rodada141): an effect or an instrument, with its place in the editor.
struct PatchNode
{
    bool generator = false;
    EffectSlot effect;  // !generator: the effect (any kind: built-in, plugin, another Patcher)
    ChannelDef channel; // generator: the instrument and its settings (name, plugin, state…)
    std::string label;  // the user's name for the node ("" = the plugin's)
    float x = 0.0f, y = 0.0f;
    // Extra audio outputs of a splitting node (FL's Frequency Splitter bands, Stereo Shaper difference): output bus
    // `bus` is the node's input through `head` (none = direct) × gain. Built on import from FL's state.
    struct BusHead
    {
        int bus = 1;
        std::optional<EffectSlot> head;
        float gain = 1.0f;
        bool operator== (const BusHead& o) const;
    };
    std::vector<BusHead> busHeads;
    // The parameter (event) ports the user activated (round 171b, FL's node menu › Inputs / Outputs › Parameters, the
    // node's class-2 ports with the "visible" flag): sorted parameter indices (LYNX's own for built-in effects, the
    // plugin's for hosted plugins). A port with a cable is always shown. An active input is also one of the Patcher's
    // published parameters (FL gives every active parameter input a Patcher parameter, record 1100).
    std::vector<int> paramIns, paramOuts;

    bool operator== (const PatchNode& o) const;
};

// ---------------------------------------------------------------------------------------------------------------
// Racks (round 183c, docs/analise/REA_rodada183c.md; Live's Instrument / Audio Effect / MIDI Effect Racks): a Patcher in
// rack mode. Chains run in parallel on the same input; each has zones, a volume, a pan, an activator and a solo. Up to
// 16 macros map onto any parameter of the rack's devices; variations store macro values. core/Rack.h builds the
// Patcher's cables from this.
constexpr int rackMacroCount = 16;

// What a macro moves: a device's parameter (a node of the Patcher: its parameter index; a nested rack's macro k is its
// published parameter k), a chain's volume or pan, or the rack's chain selector. Live's law (REA §4): linear from
// `min` to `max` in the target's 0..1 range (min > max inverts).
struct RackMapping
{
    enum class Target : std::uint8_t { param, chainVolume, chainPan, selector };
    Target target = Target::param;
    int node = -1;  // param: the Patcher node
    int chain = -1; // chainVolume / chainPan
    int param = 0;  // param: the node's parameter
    float min = 0.0f, max = 1.0f;
    bool operator== (const RackMapping&) const = default;
};

struct RackMacro
{
    std::string name;          // "" = "Macro N", or the mapped parameter's name when it has one mapping
    int colour = -1;           // −1 = the theme's
    float value = 0.0f;        // 0…127 (Live's scale)
    float defaultValue = -1.0f; // −1 = none (Live's MacroDefaults)
    bool excludeFromRandom = false, excludeFromVariations = false;
    std::vector<RackMapping> mappings;
    bool operator== (const RackMacro&) const = default;
};

struct RackVariation
{
    std::string name;
    std::array<float, rackMacroCount> values {}; // 0…127
    std::array<bool, rackMacroCount> has {};     // false = the macro is left alone on launch
    bool operator== (const RackVariation&) const = default;
};

struct RackChain
{
    std::string name;
    int colour = -1;
    bool active = true, solo = false;
    float volume = 1.0f; // linear gain (Live: −70 dB…+6 dB)
    float pan = 0.0f;    // −1..1
    RackZone key, velocity { 1, 127, 1, 127 }, select { 0, 0, 0, 0 };
    std::vector<int> nodes; // the Patcher's nodes in signal order: MIDI effects (VFX), the instrument, audio effects
    int mixer = -1;         // the chain's "rack chain mixer" node (volume, pan, chain-select gain)
    int gate = -1;          // an Audio Effect Rack chain's input gate (a zone without a fade closes the input)
    bool operator== (const RackChain&) const = default;
};

struct RackDoc
{
    enum class Kind : std::uint8_t { audioEffect, instrument, midiEffect };
    Kind kind = Kind::audioEffect;
    std::string name; // the user's name for the rack ("" = its kind's), shown in its title bar
    std::vector<RackChain> chains;
    std::array<RackMacro, rackMacroCount> macros {};
    int shownMacros = 8;        // 1, 2, 4 … 16
    float selector = 0.0f;      // the chain selector, 0…127
    bool selectorFiltersCtrl = false;
    std::vector<RackVariation> variations;
    int selectedVariation = -1;
    // LYNX's own (decision D6, off by default): a Morph knob between variations A and B, and a glide on launch
    bool morphOn = false;
    int morphA = -1, morphB = -1;
    float morph = 0.0f;   // 0..1
    float glideMs = 0.0f; // 0 = Live's instant jump
    // a standalone MIDI Effect Rack (decision R1): the channels its notes play, like a Layer's children
    std::vector<int> children;
    // the views (saved with the project, as Live saves them)
    bool showMacros = true, showChains = false, showDevices = false, showVariations = false;
    int zoneEditor = -1; // −1 hidden, 0 key, 1 velocity, 2 chain select
    int selectedChain = 0;
    bool autoSelect = false;
    bool operator== (const RackDoc&) const = default;
};

// The insert a Fruity Send slot sends its mid-chain signal to (1…127), or −1 (none, or not a Fruity Send).
// Such a slot is a route too: the destination is processed after the slot's insert (Song::mixerOrder).
int fruitySendTarget (const EffectSlot& slot) noexcept;
// Every insert a slot sends to: output `output` (0…maxSlotSends − 1) → the destination insert, or −1. Output 0 is the
// Fruity Send / Patcher target above; a Transmitter has two (its transient and sustain sends, EffectSlot::sendTracks);
// a hosted effect's routed output pairs are routes of its insert (fruitySendsOf / SlotConnections, round 189j).
constexpr int maxSlotSends = 8;
int slotSendTarget (const EffectSlot& slot, int output) noexcept;

// Send from one track to another (post-fader), like FL's routing between inserts.
struct MixerSend
{
    int target = 0;      // destination track (> 0; Master is `toMaster`)
    float level = 1.0f;  // linear gain, 0..maxFaderGain (FL's send knob: the fader curve, 0 dB at 80%, +5.6 dB at 100%)
    // LYNX extra (round 190aq; FL's sends are post-fader only): the send leaves before the insert's fader, pan,
    // separation, polarity and swap (after its effects and EQ), so the fader no longer moves it: headphone mixes,
    // effects fed at a fixed level. The insert's mute and solo still silence it. Off = FL. Saved only when on; an
    // .flp export writes the send as FL's post-fader route. Live's per-return Pre / Post toggle maps to it (.als).
    bool pre = false;

    bool operator== (const MixerSend& o) const noexcept { return target == o.target && level == o.level && pre == o.pre; }
};

// LYNX's "Keep links to deleted inserts" (round 186c, pain point 1, off by default; FL drops them): a send whose insert
// was deleted, kept by the deleted insert's name and the send's level. It carries no sound; the mixer draws it greyed
// until it is reconnected to an insert or removed.
struct LostSend
{
    std::string to;      // the deleted insert's name
    float level = 1.0f;  // the send's level (0 = a side-chain link)

    bool operator== (const LostSend&) const = default;
};

// Built-in EQ of each mixer track (FL's 3 bands: low shelf, peak, high shelf),
// after the effect slots and before the fader (REA_rodada6 §1).
struct TrackEq
{
    std::array<float, 3> gainDb { 0.0f, 0.0f, 0.0f };          // ±18
    std::array<float, 3> hz { 90.0f, 1500.0f, 8000.0f };       // 10 Hz .. 16 kHz
    std::array<float, 3> width { 0.267f, 0.267f, 0.267f };     // 0..1: peak Q / shelf slope
    // round 177d (an FL wishlist item; LYNX's own): the EQ is skipped and its settings kept (FL has no bypass for it)
    bool bypassed = false;

    bool hasBoostOrCut() const noexcept { return gainDb[0] != 0.0f || gainDb[1] != 0.0f || gainDb[2] != 0.0f; }
    bool active() const noexcept { return ! bypassed && hasBoostOrCut(); }
    bool operator== (const TrackEq&) const = default;

    // FL conversions: peak Q = 0.0999·(1001^w − 1) + 0.1; shelves S = 1.9·w + 0.1
    static double peakQ (float w) noexcept;
    static double shelfSlope (float w) noexcept;
};

// When an insert's external audio input is heard through it (FL's "Monitor external input": Off / When armed /
// Auto / On, REA_rodada88 §1). Auto [model]: like "When armed", except during playback without recording, so a
// take plays back without the live input on top.
enum class InputMonitor : std::uint8_t
{
    off,
    whenArmed,
    automatic,
    on
};

enum class MixerDock : std::uint8_t { left, middle, right };

// What an armed insert records (FL's "Record" menu of the insert's input box, AudioRecorderRoutingMenu; track byte
// +0x2F, .flp 0xA5; REA_rodada2 §2.1, REA_rodada165f-mixer §4, REA_rodada168x §2). FL 2026's new inserts record Post EQ
// (the FL 2026 demo projects save 3 on every insert; the Playlist hint of a fresh insert: "Recording: Post EQ").
// The taps sit in FL's insert order: input → effect slots → EQ → level / pan / separation → mute → routes.
enum class RecordSource : std::uint8_t
{
    externalInput,    // "External input only": the device input as it comes in (no effects)
    externalAndMixer, // "External and mixer input": the insert's input (device input + what is routed to it), before the slots
    postEffects,      // "Post effects": after the effect slots
    postEq,           // "Post EQ": after the insert's built-in EQ (FL's default)
    postLevel,        // "Post level and panning": after the fader, pan, separation, polarity and swap (not muted)
    postTrack         // "Post track": the insert's output as it leaves it (muted too)
};

// FL's mixer track latency menu "Bypass track latency compensation" (REA round 165f-mixer; .flp event 0x31): a bypassed
// track's outputs get no PDC delay (its own latency still makes the other paths wait), so a live input monitored
// through it is not delayed to line up with slow plugins elsewhere (FL 24.2's per-track low-latency monitoring).
enum class LatencyBypass : std::uint8_t
{
    never,
    always,
    whenRecordingOrStopped // FL's "When recording or not playing"
};

// How an insert's stereo output reaches its hardware output target (LYNX extra, REA round 156): a stereo pair of
// device channels, or one channel alone fed with the sum (L+R)/2, the left side only or the right side only.
enum class OutputMode : std::uint8_t
{
    stereo,
    monoSum,
    monoLeft,
    monoRight
};

// MixerTrack::outputChannel values besides a device channel (0-based)
constexpr int outputDefault = -2; // Master: the device's first output pair; inserts: none (they reach it via the Master)
constexpr int outputNone = -1;    // no hardware output (the Master too)

struct MixerTrack
{
    std::string name;
    float volume = unityFader; // 0..1; gain = volume / unityFader
    float pan = 0.0f;          // -1..1
    bool muted = false;
    bool solo = false;
    // with solo: the tracks that feed this one are heard too (LYNX's solo switch, FL's "Alt solo"); off = FL's "Solo
    // selected track(s)": only this track and its path to Master (REA_rodada167l §4)
    bool soloSources = true;
    std::vector<EffectSlot> effects; // up to maxEffectSlots, processed in order
    bool toMaster = true;            // inserts: output to Master (besides the sends)
    float masterLevel = 1.0f;        // level of that route to Master (linear gain, 0..maxFaderGain; FL's route knob)
    float stereo = 0.0f;             // separation: -1 = widest, 0 = normal, +1 = mono (as in FL)
    std::uint32_t colour = 0;        // 0xFFRRGGBB; 0 = theme default
    bool invertPhase = false;
    bool swapChannels = false;
    std::vector<MixerSend> sends;    // to other inserts
    std::vector<LostSend> lostSends; // sends to deleted inserts, kept greyed (round 186c, an option; saved)
    TrackEq eq;
    // external audio input (FL's "Audio input source"): the device's input channel `inputChannel` (0-based;
    // -1 = none) and, when `inputStereo`, the next one too
    int inputChannel = -1;
    bool inputStereo = true;
    InputMonitor monitor = InputMonitor::automatic;
    // record safe (round 163, as Pro Tools / Nuendo): the insert can't be armed by accident (saved)
    bool recordSafe = false;
    // FL's "Record <insert> to" (the arm button's right-click, round 167a [SHOT s20–s27]): the file this insert records
    // and renders to (empty = an automatic name). Saved.
    std::string recordTo;
    // FL's "Record" position (round 168x, issue #41): where the take is tapped. Saved.
    RecordSource recordSource = RecordSource::postEq;
    // LYNX extra (issue #41, "(LYNX)" in the menu): with a position after the slots, also record the dry device input as
    // a second take at the same time (DI + processed, for re-amping). Saved.
    bool recordDryToo = false;
    // an FL project's input (.flp 0x9A, REA round 157c): the index in FL's list of its own device's inputs, which only
    // the open device can turn into a channel — AudioEngine::setSong resolves it (flInputSource) when the project opens,
    // if inputChannel is still -1. -1 = none. Not saved.
    int flInput = -1;
    // hardware output target (FL's "Audio output target" .flp 0x93; mono channels are a LYNX extra, REA round 156):
    // outputDefault, outputNone or the first device output channel; an insert with a target still feeds the
    // Master (and its sends) as in FL — the hardware output is a parallel path
    int outputChannel = outputDefault;
    OutputMode outputMode = OutputMode::stereo;
    // FL's mixer track menu "CPU › Allow threaded processing" (REA round 134): off = this insert always runs on the
    // audio thread itself (as do inserts holding a plugin whose own option is off)
    bool threaded = true;
    // FL's mixer track menu "Group › Separator" (REA round 146a; .flp insert flag 0x400): a gap is drawn on the left of
    // this track; a run of tracks between two separators is a group ("Create group", "Select › Group", "Auto color group")
    bool separator = false;
    // LYNX's folded mixer group (round 177a, an addition to FL's groups): set on a group's first strip, the group is shown
    // folded — only its bus strip when the group is a summing group, else a slim tab (mixerops::foldView). View only.
    bool folded = false;
    // FL's "Layout › Dock to" (.flp insert flags bits 6-7 = dock 0 left / 1 middle / 2 right): the strip is shown in the
    // left block, the middle or the right block of the mixer
    MixerDock dock = MixerDock::middle;
    // FL's track latency menu (REA round 165f-mixer, .flp 0xEC float at +0): a manual latency in ms (−5000..5000) added
    // to the track's output latency in the PDC plan, as if its effects had it: positive = the other paths wait for this
    // one (it plays earlier), negative = this track is delayed
    float latencyMs = 0.0f;
    LatencyBypass latencyBypass = LatencyBypass::never;
    // "Compensate for master latency" (.flp 0xEC flag 0x4000 = off): with a hardware output target, that output is
    // delayed to line up with the Master's (PDC total)
    bool compensateMasterLatency = true;
    // Online Sessions (round 168a): the render of this insert's output (after its effects, before the fader) for
    // computers without one of its third-party plugins — a timeline stem (StandIn::pattern 0)
    std::vector<StandIn> standIns;
    // FL's mixer track icon ("Change icon…", .flp 0x5F, REA round 168j): FL's icon index, 0 = none. Kept and saved.
    int icon = 0;
    // The Simple workflow's Blend (round 190bu): the dry/wet of the whole effect chain, 1 = the chain alone (nothing is
    // computed), 0 = the insert's input before its first slot; the dry path waits for the chain's latency. Saved in
    // .lynx when under 1.
    float chainBlend = 1.0f;
    // The chain preset ("sound") last put on this insert (core/ChainPresets.h; "" = none). A name only: the slots are
    // the chain. Saved.
    std::string chainPreset;
    bool operator== (const MixerTrack&) const = default; // (Online Sessions P2: entity comparison, round 169b)
};

constexpr float maxTrackLatencyMs = 5000.0f; // FL's clamp (FUN_012a7230)

// A VCA fader (round 177a slice 3, REA_rodada177a §3; LYNX's addition, FL Studio has none). It carries no audio: its
// level multiplies the fader gain of each member insert (the levels add in dB), so post-fader sends follow and pre-fader
// ones don't. An insert can be in several VCAs, and a VCA can follow a parent VCA. Muting a VCA mutes its members.
// The members keep their own fader values and automation.
struct Vca
{
    std::string name;
    std::uint32_t colour = 0;     // 0xFFRRGGBB; 0 = theme default
    float level = unityFader;     // fader position (unityFader = 0 dB)
    bool muted = false;
    std::vector<int> members;     // mixer inserts (1…), sorted; kept by mixerops' remap
    int parent = -1;              // the VCA this one follows (index into Song::vcas), -1 = none
    bool operator== (const Vca&) const = default;
};

struct Note
{
    std::int64_t start = 0;   // ticks
    std::int64_t length = ticksPerStep;
    int channel = 0;
    int key = 60;             // MIDI; 60 = C4 (FL's C5)
    float velocity = 0.78f;   // 0..1
    float pan = 0.0f;         // -1..1 (per note, as in FL)
    float finePitch = 0.0f;   // cents of a semitone, -120..120
    bool muted = false;       // disabled note (does not play, stays in the pattern)
    bool slide = false;       // glides the sounding note to this pitch (without retriggering), as in FL
    bool porta = false;       // inverts the channel's porta on this note (the note glides from the previous one; as in FL)
    // release velocity 0..1 (FL's "Note release"): the note's "off" carries it to the instruments that take one — hosted
    // plugins (FL's wrapper "Send note release velocity"), a Patcher's and a VFX's voices
    float release = 0.5f;
    float modX = 0.5f, modY = 0.5f; // two free per-note modulation values 0..1 (stored, not yet routed)
    int colour = 0;           // note colour group 0..15 (FL's note colours; .flp note byte 'extra' bits 0-3)
    int group = 0;            // note group (FL's Group, Shift+G: clicking one note selects the group); 0 = none
    // Live 12's note Chance and Velocity Range (round 187c, LYNX additions; FL has neither; core/NoteChance.h): each time
    // the note comes round it plays with this probability (0..1, .als Probability), at a velocity drawn evenly between
    // `velocity` and `velocity + velocityRange` (-1..1 = Live's VelocityDeviation / 127). Drawn from Song::chanceSeed,
    // so a project plays and renders the same way every time; each loop pass draws again.
    float chance = 1.0f;
    float velocityRange = 0.0f;
    // Live 12's probability groups (.als NoteProbabilityGroups): the notes of a pattern with the same chanceGroup (> 0)
    // draw together — "Play all" (chanceOne false): all of them or none; "Play one": one member picked at random.
    int chanceGroup = 0;
    bool chanceOne = false;
    bool hasChance() const noexcept { return chance < 1.0f || velocityRange != 0.0f || chanceGroup != 0; }
    bool operator== (const Note&) const = default; // (Online Sessions P2: entity comparison, round 169b)
};

constexpr int noteColourCount = 16;

// Swing delay (FL's formula, docs/analise/REA_rodada3.md §3): only notes whose nearest step
// is odd move, with triangular weight by distance to the grid; half a step at swing 1.
std::int64_t swingDelay (std::int64_t start, float swing, float channelMix = 1.0f) noexcept;

// What an automation clip controls. Values always 0..1 (like the model's knobs).
enum class AutomationTargetKind : std::uint8_t
{
    none,          // target removed (the clip goes silent)
    channelVolume, // index = channel
    channelPan,    // index = channel (0..1 -> -1..1)
    channelParam,  // index = channel with plugin; param = plugin parameter index
    mixerVolume,   // index = mixer track (0 = Master)
    mixerPan,      // index = track
    effectParam,   // index = track, slot, param (built-in effect or plugin)
    tempo,         // song tempo: 0..1 = minTempo..maxTempo BPM
    effectMix,     // index = track, slot: slot dry/wet (0..1)
    mixerSend,     // index = source insert, slot = destination (0 = Master): the route's level knob (fader curve; issue #12)
    vcaLevel       // index = VCA (Song::vcas): its fader (round 177a; LYNX's own, FL has no VCA)
};

constexpr double minTempo = 10.0, maxTempo = 522.0; // FL's range
inline double tempoFromValue (float v) noexcept { return minTempo + (maxTempo - minTempo) * (double) v; }
inline float valueFromTempo (double bpm) noexcept { return (float) ((bpm - minTempo) / (maxTempo - minTempo)); }

struct AutomationTarget
{
    AutomationTargetKind kind = AutomationTargetKind::none;
    int index = 0;
    int slot = 0;
    int param = 0;

    bool operator== (const AutomationTarget& o) const noexcept
    {
        return kind == o.kind && index == o.index && slot == o.slot && param == o.param;
    }
};

// Event editor (round 83): a pattern's own parameter events, like FL's pattern events (.flp 0xDF). Each event
// holds its value (0..1, like automation) until the next one; while the pattern plays they drive the target.
struct PatternEvent
{
    std::int64_t tick = 0; // from the start of the pattern
    float value = 0.5f;    // 0..1
    bool operator== (const PatternEvent&) const = default;
};

struct PatternEvents
{
    AutomationTarget target;
    std::vector<PatternEvent> events; // sorted by tick, one per tick
    bool operator== (const PatternEvents&) const = default;

    // Value at `tick` (held from the previous event; before the first = the first value).
    float valueAt (double tick) const noexcept;
};

// FL's Channel Rack loop types (the Loop button's menu, REA_rodada165f-rack §1): per pattern.
enum class RackLoopMode : std::uint8_t
{
    steps = 1,   // "Loop step channels": the channels with only steps loop (at the next bar unless set)
    all = 2,     // "Loop all channels": every channel loops (at the next bar unless set)
    advanced = 3 // "Advanced looping": each channel's own loop point (none unless set)
};

// A channel's loop point in a pattern (FL .flp 0xA0 channel / 0xA1 setting). setting: 0 = don't loop, −1 = at the next
// step after its last note, −2 = next beat, −3 = next bar, N > 0 = every N steps.
struct ChannelLoop
{
    static constexpr int dontLoop = 0, nextStep = -1, nextBeat = -2, nextBar = -3;
    int channel = 0;
    int setting = dontLoop;
    bool operator== (const ChannelLoop&) const = default;
};

// Playlist marker (like FL's "time markers"). With numerator > 0 it is a time signature
// change: a new bar starts there, with numerator/denominator.
struct Marker
{
    std::int64_t tick = 0;
    std::string name;
    int numerator = 0, denominator = 4;
    // FL's marker action = the .flp's marker type (REA_rodada6 §5, REA_rodada165f-markers §1): 0 plain marker,
    // 1 marker loop, 2 marker skip, 3 marker pause, 4 (song) loop, 5 start, 6 pattern length, 7 bar override,
    // 8 time signature (also numerator > 0), 9 start recording (punch in), 10 stop recording (punch out),
    // 11 loop length (pattern markers), 12 key/scale. Playback follows them: core/MarkerPlan.h.
    int action = 0;
    static constexpr int none = 0, markerLoop = 1, markerSkip = 2, markerPause = 3, songLoop = 4, start = 5,
                         patternLength = 6, barOverride = 7, timeSignature = 8, punchIn = 9, punchOut = 10,
                         loopLength = 11, keyScale = 12;
    static constexpr int songStart = start; // (round 165m's name) Performance mode's zone ends here

    bool isSignature() const noexcept { return numerator > 0; }
    bool operator== (const Marker&) const = default;
};

struct Pattern
{
    std::string name;
    std::vector<Note> notes;  // sorted by start (kept by sortNotes)
    std::int64_t lengthTicks = 0; // 0 = automatic (rounds up to bars)
    std::uint32_t colour = 0;     // 0xFFRRGGBB; 0 = automatic (by index)
    std::vector<PatternEvents> events; // event editor lanes (one per target)
    // FL's Channel Rack looping (FL 20.5+, .flp 0x1A on / 0xA2 type): channels shorter than the pattern repeat to its
    // end (polyrhythms). The loop points stay stored while it is off.
    bool loopOn = false;
    RackLoopMode loopMode = RackLoopMode::steps;
    std::vector<ChannelLoop> channelLoops; // at most one per channel
    std::uint64_t uid = 0; // stable id (Online Sessions, round 168a)
    // FL's pattern time markers (the Piano roll's ruler; .flp 0x94 / 0x21 / 0x22 / 0xCD inside the pattern's events,
    // round 168g): plain markers, time signature changes, Loop length (11), Pattern length (6)... sorted by tick
    std::vector<Marker> markers;
    std::string loopStarterGenre; // FL's Loop Starter genre of this pattern (round 168b); "" = no Loop Starter

    void sortNotes();
    std::int64_t effectiveLength (int beatsPerBar) const noexcept;

    bool hasStep (int channel, int step) const noexcept;
    // A muted note on the step counts as an empty step. Turning a step off deletes its notes, or with `muteRemoved` (FL's
    // Channel Rack option "Mute removed steps") mutes them; turning it on with `muteRemoved` unmutes them, data and all,
    // without it adds a new note (as FL's SetStep, REA round 169f: a muted note there is left alone).
    void toggleStep (int channel, int step, bool muteRemoved = false);
    bool operator== (const Pattern&) const = default;
};

enum class ClipKind : std::uint8_t
{
    pattern,
    audio,
    automation
};


struct AutomationPoint
{
    // LYNX's bend law before round 190bw (x^(2^(3·tension))): the mode of a bent segment loaded from an older project,
    // so that it plays as it did. Never offered in a menu; bending the segment again makes it a Single curve.
    static constexpr std::uint8_t legacyCurve = 255;

    std::int64_t tick = 0; // from the start of the automation
    float value = 0.5f;    // 0..1
    float tension = 0.0f;  // curve to the next point: -1..1 (0 = straight); the opposite sign of FL's tension
    // FL's curve mode of the segment to the next point (flcurve::Mode: Single curve, Hold, Stairs…; round 190bw). FL
    // keeps a segment's mode and tension on the point where it ENDS, LYNX on the point where it starts: the .flp
    // import / export and the point menu shift by one point.
    std::uint8_t mode = 0;
    bool operator== (const AutomationPoint&) const = default;
};

// Value of a point list at `tick` (before the first point = first value; after the last = last; each segment follows
// its mode and tension, FL's point curve law in core/FlCurve).
float automationCurveAt (const std::vector<AutomationPoint>& points, double tick) noexcept;
// The fraction 0..1 of the way from point `segment` to the next one at `x` 0..1 of the segment's time (its mode and
// tension; Smooth looks at the two neighbouring segments).
double automationSegmentShape (const std::vector<AutomationPoint>& points, std::size_t segment, double x) noexcept;
// FL's point menu acts on the segment that ends at a point: the mode shown for point `point` (−1 for the first point,
// which ends no segment) and its change (the tension stays, as in FL; a legacy bend becomes the new mode's).
int automationPointMode (const std::vector<AutomationPoint>& points, int point) noexcept;
void setAutomationPointMode (std::vector<AutomationPoint>& points, int point, int mode) noexcept;
// Adding and deleting a point the way FL's envelope editor does [DECOMP FLEngine 0xcce3b0, 0xcd3180; round 190bw]:
// the segment that ENDS at the new point takes `mode` and `tension` (the editor's last used ones; LYNX's tension
// sign), the rest of the split segment keeps its curve; deleting a point hands the curve of the segment that started
// at it to the joined segment. Returns the new point's index.
int insertAutomationPoint (std::vector<AutomationPoint>& points, AutomationPoint p, int mode = 0, float tension = 0.0f);
void eraseAutomationPoint (std::vector<AutomationPoint>& points, int point);
// Bending segment `segment` by hand: its tension; an older project's bend becomes a Single curve.
void setAutomationSegmentTension (std::vector<AutomationPoint>& points, int segment, float tension) noexcept;

// FL's automation clip LFO ("LFO mode", REA round 177b: TACArticulator's TMLFO, .flp event 0xEA after the points;
// docs/analise/REA_rodada177b.md §LFO). Off: the clip plays its points only. On: a waveform (triangle bent by tension,
// skew and pulse width) whose amplitude follows the LFO's own points over the clip's time, added to or multiplying
// the clip's value.
struct AutomationLfo
{
    bool on = false;        // FL's LFO enabled LongBool (the "LFO" box / the editor's LFO button)
    int level = 64;         // -128..128 (FL's "LFO level")
    bool add = false;       // FL's "Multiply" box unchecked: the LFO is added (checked: multiplies the envelope level)
    int flags = 1;          // FL's bits: 1 tempo, 2 global phase, 4 unipolar tension, 8 frozen
    int speed = 45819;      // 512..65536 (45819 = one cycle per beat, FL's default for a new clip)
    int tension = 0, skew = 0; // -128..128
    int pulseWidth = 0;     // -112..112
    std::uint32_t phase = 0; // start phase, a fraction of a cycle × 2^32
    float range = 1.0f;     // the clip's range in the target's 0..1 (an imported FL clip's levels)
    std::vector<AutomationPoint> amp { { 0, 1.0f, 0.0f } }; // the LFO's amplitude over the clip's time (≥ 1 point)

    // FL's articulator fields LYNX does not use, kept so an imported .flp clip writes back byte for byte
    struct Fl
    {
        bool present = false;
        std::int32_t mainFlags = 1, mainEnabled = -1;
        std::array<std::int32_t, 3> mainLoop { -1, -1, -1 };
        std::array<std::int32_t, 4> adsr { 128, 128, 0, 128 };
        std::array<std::int32_t, 3> lfoLoop { -1, -1, -1 };
        std::vector<std::uint8_t> rawPoints;    // the LFO's point records as read (24 B each)
        std::vector<AutomationPoint> rawAmp;    // `amp` as decoded from them (unchanged = write rawPoints)
        bool operator== (const Fl&) const = default;
    } fl;

    bool isDefault() const noexcept;        // what FL writes for a clip whose LFO was never touched
    // The value played: `value` (the clip's own curve) with the LFO at `clipTick` (ticks from the clip's start, its
    // offset included) and `songTick` (the global phase).
    float apply (float value, double clipTick, double songTick) const noexcept;
    float output (double clipTick, double songTick) const noexcept; // the LFO alone: amplitude × waveform, -1..1
    static double cyclesPerBeat (int speed) noexcept;               // FL's speed → rate (FUN_00ba03d0)
    static int speedForBeats (double beatsPerCycle) noexcept;       // the inverse (FUN_00ba0490)
    static float shape (std::uint32_t phase, int tension, int skew, int pulseWidth, bool unipolar) noexcept;
    bool operator== (const AutomationLfo&) const = default;
};

struct Automation
{
    std::string name;
    AutomationTarget target;
    std::vector<AutomationPoint> points; // sorted by tick (kept by the editor)
    AutomationLfo lfo;                   // round 177b: FL's clip LFO (off by default: no cost)

    // Live source (FL controller link, REA_rodada14 §7): the value comes from output `output` of the
    // controller effect in slot (track, slot), passed through the curve (points evenly spaced over 0..1; empty =
    // direct). Uses neither clips nor points.
    struct LiveSource
    {
        int track = -1, slot = 0, output = 0;
        int channel = -1; // instead of an effect: this channel's "controller" instrument
        bool operator== (const LiveSource&) const = default;
    } source;
    std::vector<float> curve;
    // FL's "Record automation" of an internal-controller link (CanAutomateBox "Allow recording of automation", off by
    // default; round 190cs): while automation is recorded, the controller's moves are written into the target's clip
    bool recordLive = false;
    bool isLive() const noexcept { return source.track >= 0 || source.channel >= 0; }
    std::uint64_t uid = 0; // stable id (Online Sessions, round 168a)
    float mapLive (float v) const noexcept; // applies the curve

    // Value at `tick` (before the first point = first value; after the last = last).
    float valueAt (double tick) const noexcept;
    // What plays at `tick` of the automation (clip time, offset included) at song position `songTick`: valueAt plus
    // the LFO when it is on.
    float playAt (double tick, double songTick) const noexcept { return lfo.on ? lfo.apply (valueAt (tick), tick, songTick) : valueAt (tick); }
    std::int64_t length() const noexcept; // tick of the last point (minimum 1)
    bool operator== (const Automation&) const = default;
};

// Round 177b: LYNX's modulators (not FL: FL's controllers stay as they are). A modulator moves its targets around their
// own value (relative, as Bitwig's and Live's "Mod"): the played value = clamp (the knob's value, or its automation, +
// depth × the modulator's output). Off the audio thread nothing is made until the first modulator exists.
struct Song;
enum class ModShape : std::uint8_t { sine, triangle, sawUp, sawDown, square, random, smoothRandom };

struct ModRoute
{
    AutomationTarget target;
    float depth = 0.25f; // -1..1 of the target's whole range
    bool operator== (const ModRoute&) const = default;
};

struct Modulator
{
    std::string name;            // "LFO 1"
    ModShape shape = ModShape::sine;
    bool sync = true;            // tempo-synced: `beats` per cycle; free: `hz`
    double beats = 4.0;          // cycle length in quarter notes (synced)
    double hz = 1.0;             // cycles per second (free)
    float phase = 0.0f;          // start phase, 0..1 of a cycle
    bool unipolar = false;       // 0..1 instead of -1..1
    std::uint32_t seed = 1;      // the random shapes' sequence
    std::vector<ModRoute> routes;
    bool operator== (const Modulator&) const = default;

    // The output at `cycles` (cycles since the song start, or since the modulator started when free).
    float output (double cycles) const noexcept;
    static bool canModulate (const Song& song, const AutomationTarget& target) noexcept; // LYNX's own controls
};

// One chord of a keyboard chord set (round 190ds; FL Studio Mobile's KEYB entries {baseNote, extensions, octave,
// quality}, docs/analise/FLM_FORMAT.md §2): root pitch class, quality (0 major, 1 minor, 2 augmented, 3 diminished,
// 4 sus2, 5 sus4), octave (relative: the set's lowest octave plays where the pad keyboard is) and added notes (semitones
// above the root).
struct KeyboardChord
{
    int root = 0, quality = 0, octave = 4;
    std::vector<int> extensions;
    bool operator== (const KeyboardChord&) const = default;
};

// A block in the Playlist: a section of a pattern, or a section of an audioClip channel's audio.
struct Clip
{
    int pattern = 0;
    int track = 0;
    std::int64_t start = 0;   // ticks in the song
    std::int64_t length = 0;  // ticks
    std::int64_t offset = 0;  // how much of the start of the pattern/audio is skipped (ticks)
    float offsetFraction = 0.0f; // audio clips: sub-tick part of `offset` (-0.5..0.5 ticks; imported FL offsets in ms)
    ClipKind kind = ClipKind::pattern;
    int channel = -1;         // audio clips: channel (audioClip) that holds the sample
    int automation = -1;      // automation clips: index into Song::automations
    std::int64_t fadeIn = 0, fadeOut = 0; // audio clips: fades (ticks)
    float fadeInTension = 0.0f, fadeOutTension = 0.0f; // audio clips: FL's raw fade curve values, kept for the export
    // audio clips: straight fades instead of FL's S curve, bit 0 the fade-in, bit 1 the fade-out (FL's shape 1, round 180d).
    // Also every clip of a project saved before that round: a finished mix must not change when it is reopened.
    std::uint8_t linearFades = 0;
    std::uint32_t flFadeFlags = 0; // FL's raw fade flags of an imported clip (flp::PlaylistItem::fadeFlags), back in the export
    float gain = 1.0f;        // audio clips: clip volume (linear; FL's gain handle: −96.1 … +36 dB, round 166e)
    float pan = 0.0f;         // audio clips: clip pan -1..1 (FL 2026's clip pan; REA round 153)
    bool muted = false;       // clip-level mute (the Mute tool); independent of the track mute
    // audio clips: FL 2026's clip pitch (semitones, -12..+12; keys 7/8 of the Playlist) and clip reverse (key 9) —
    // per clip, on top of the channel's own pitch and reverse (FL: the playlist item's +0x3C / +0x48 bit 0, round 167k)
    float pitch = 0.0f;
    bool reversed = false;
    // A slot of the clip grid (Clip-launch workflow, core/ClipLauncher.h): this clip's entry in ClipGrid::slots, −1 for
    // every clip of the Playlist. It fits the padding after `reversed`: Clip stays the same size.
    std::int16_t gridSlot = -1;
    // audio clips: FL 2026's per-clip stretch (the playlist item's +0x40 double, REA_rodada167l §2): the clip plays its
    // audio `stretch` times longer (2 = half speed). Its channel's time-stretch mode decides how: Resample changes the
    // speed and the pitch together, the other modes keep the pitch. `length` is already the stretched length.
    double stretch = 1.0;
    // FL's automatic crossfades (round 166e): where this audio clip overlaps the previous / next audio clip on its
    // track, the overlap in ticks — derived from the arrangement by clipops::updateCrossfades while
    // Song::autoCrossfades is on (never saved). A crossfade replaces that side's fade, with an S-shaped curve.
    std::int64_t crossfadeIn = 0, crossfadeOut = 0;
    std::uint64_t uid = 0; // stable id (Online Sessions, round 168a): whose clip it is
    // audio clips, LYNX comping (round 177c, core/Comping.h): bit 0 = the fade-in is an equal-power crossfade (sin), bit 1 =
    // the fade-out (cos). Set where a comp switches takes: the two clips overlap by the fade and their gains sum to
    // constant power. Saved with the project.
    std::uint8_t fadeCurve = 0;
    // audio clips, round 190ds (core/KeyFollow.h): the clip follows the project key — its channel's loop key is moved to
    // the project's by `keyShift` semitones, which are part of `pitch` (so every export plays it as heard). Both in the
    // padding after fadeCurve: Clip stays the same size.
    bool followKey = false;
    std::int8_t keyShift = 0;

    std::int64_t end() const noexcept { return start + length; }
    std::int64_t effectiveFadeIn() const noexcept { return crossfadeIn > 0 ? crossfadeIn : fadeIn; }
    std::int64_t effectiveFadeOut() const noexcept { return crossfadeOut > 0 ? crossfadeOut : fadeOut; }
    bool operator== (const Clip&) const = default;
};

static_assert (offsetof (Clip, gridSlot) + sizeof (std::int16_t) <= offsetof (Clip, stretch)); // (in the padding)

// LYNX extra (issue #25; the user's choice, after round 168k showed FL plays such a clip once and is silent after its
// pattern): a pattern clip stretched past its pattern loops the pattern until the clip ends. An app setting (Options ›
// General, "Stretched pattern clips loop their pattern"), default on; off = FL's behaviour. Not saved in projects.
inline std::atomic<bool>& stretchedClipsLoopFlag() noexcept
{
    static std::atomic<bool> on { true };
    return on;
}
inline bool stretchedClipsLoop() noexcept { return stretchedClipsLoopFlag().load (std::memory_order_relaxed); }
inline void setStretchedClipsLoop (bool on) noexcept { stretchedClipsLoopFlag().store (on, std::memory_order_relaxed); }

// The pattern tick that song tick `tick` inside clip `c` plays — tick − start + offset, wrapped around `patternLength`
// (the pattern's effectiveLength) when the clip reaches past its pattern and stretchedClipsLoop() is on.
inline double patternTickInClip (const Clip& c, double tick, std::int64_t patternLength) noexcept
{
    const auto local = tick - (double) c.start + (double) c.offset;
    if (! stretchedClipsLoop() || patternLength <= 0 || c.offset + c.length <= patternLength || local < (double) patternLength)
        return local;
    return std::fmod (local, (double) patternLength);
}

// The passes of a pattern clip's pattern: pattern tick 0 of pass k sits at song tick c.start − c.offset + k·length,
// k in [first, last]; `loops` = the clip reaches past its pattern and stretchedClipsLoop() is on (then a pass plays the
// notes before `length` only), else one pass (k = 0) of every note in [offset, offset + length).
struct ClipPasses
{
    std::int64_t first = 0, last = 0, length = 1;
    bool loops = false;
};
inline ClipPasses clipPasses (const Clip& c, std::int64_t patternLength) noexcept
{
    ClipPasses p;
    p.length = patternLength > 0 ? patternLength : 1;
    p.loops = stretchedClipsLoop() && c.length > 0 && c.offset + c.length > p.length;
    if (p.loops)
    {
        p.first = c.offset / p.length;
        p.last = (c.offset + c.length - 1) / p.length;
    }
    return p;
}

enum class PlayMode : std::uint8_t
{
    pattern, // loops the current pattern (like FL's "PAT")
    song     // plays the Playlist (like FL's "SONG")
};

// MIDI controller (CC) linked to a parameter (FL's "Link to controller").
struct MidiLink
{
    int cc = 0;       // 0..127
    int channel = -1; // MIDI channel 0..15, -1 = any
    AutomationTarget target;
    // FL's "Remote control settings" (form TMIDIInputForm; round 190bt, docs/analise/REA_rodada190bt.md):
    int port = -1;        // the input device's port number (Settings > MIDI), -1 = none; a link follows the devices
                          // with exactly its port
    std::string formula;  // mapping formula over `Input` (core/LinkFormula.h); "" = 1:1
    bool smooth = false;  // "Smoothing" on
    int smoothing = 469;  // the smoothing slider, -1024..1024 (FL's default 469: 3 poles, about 200 ms)
    bool omni = false;    // "Omni": the link moves this control of the selected channel / mixer track
    int mode = 0;         // FL's "Mode" (round 190cs): linkformula::LinkMode, 0 = Default

    MidiLink() = default;
    MidiLink (int controller, int midiChannel, AutomationTarget control) : cc (controller), channel (midiChannel), target (control) {}

    // Has the link anything a plain learned link lacks?
    bool hasSettings() const noexcept { return port >= 0 || ! formula.empty() || smooth || smoothing != 469 || omni || mode != 0; }
    bool operator== (const MidiLink&) const = default;
};

// FL's controller numbers (CtrlMenu "Special" [FORM]; round 190cs): 0...127 are the continuous controllers, 256 + n is
// note n ("Note C0" = 256, FL's C0 being MIDI note 0). A link with a note number follows that key's presses and releases.
constexpr int linkNoteBase = 256;
constexpr int linkNoteLast = linkNoteBase + 127;
inline bool isNoteLink (int number) noexcept { return number >= linkNoteBase && number <= linkNoteLast; }

// The same hardware control (FL compares port, channel and controller number as one id).
inline bool sameController (const MidiLink& a, const MidiLink& b) noexcept
{
    return a.port == b.port && a.channel == b.channel && a.cc == b.cc;
}
// "Omni" exists for a channel's controls (the selected channel) and a mixer track's controls: its volume and pan and its
// slots' controls (the selected track; FL live check 190cq: enabled on "Insert 1 - Volume", greyed on Tempo).
bool linkOmniAvailable (AutomationTargetKind kind) noexcept;
// What an Omni link moves now: the same control on the selected channel / mixer track.
AutomationTarget linkOmniTarget (const MidiLink& link, int selectedChannel, int selectedMixerTrack) noexcept;
// The other links (of other controls) that use the controller of `link`: FL's "conflicting assignments".
int countLinkConflicts (const std::vector<MidiLink>& links, const MidiLink& link);
// "Accept" of the Remote control settings window: the control's links become `edited` (entries without a controller,
// cc < 0, are dropped: accepting a link with no controller removes it); with `removeConflicts`, the links of other
// controls that use one of these controllers go. Returns how many conflicting links were removed.
int setTargetLinks (std::vector<MidiLink>& links, const AutomationTarget& target, std::vector<MidiLink> edited, bool removeConflicts);

// The links a controller move follows: the project's, then the global ones (settings, every project) whose target
// and controller the project does not link itself (REA round 146: a project link overrides a global one).
std::vector<MidiLink> linksWithGlobal (const std::vector<MidiLink>& project, const std::vector<MidiLink>& global);

enum class PlaylistTrackMode : std::uint8_t { unassigned, audio, instrument };

// FL's Performance mode track settings (round 165m: the track menu's "Performance settings" [DFM], stored in the
// .flp Playlist track record 0xEE at bytes 22…45 [DATA]; the numbers are FL's menu Tags = the stored values).
// How a clip launched in the performance zone (the Playlist before the "Start" marker) plays on its track.
struct TrackPerformance
{
    enum class Press : std::uint8_t { retrigger, holdStop, holdMotion, latch };
    enum class Motion : std::uint8_t { stay, oneShot, marchWrap, marchStay, marchStop, random, exclusiveRandom };
    // Trigger / position sync: off, 1/4, 1/2, 1, 2 or 4 beats, or auto (the clip's own length)
    enum class Sync : std::uint8_t { off, quarterBeat, halfBeat, beat, twoBeats, fourBeats, autoLength };

    Press press = Press::retrigger;
    Motion motion = Motion::stay;
    Sync triggerSync = Sync::fourBeats; // a launch waits for the next multiple of this (FL's default 4 beats)
    bool queue = false;                 // a launch waits for the playing clip to end
    bool tolerant = true;               // a launch a little after the grid point still starts on it (FL's default on)
    Sync positionSync = Sync::off;      // the launched clip starts at the song position modulo this, not at its start

    bool isDefault() const noexcept { return *this == TrackPerformance {}; }
    bool operator== (const TrackPerformance&) const = default;
};

// Round 177b: an optional automation lane under a Playlist track (Live / Bitwig style, next to FL's clips): the
// automation's points are in song time and play over the whole song; where an automation clip of the same target
// plays, the clip wins (Bitwig's rule). Display only: `height` (px, 0 = default).
struct AutomationLane
{
    int automation = -1; // index into Song::automations
    int height = 0;
    bool operator== (const AutomationLane&) const = default;
};

// Header of a Playlist track.
struct PlaylistTrack
{
    std::string name;  // empty = "Track N"
    bool muted = false; // clips on this track don't play
    int height = 0;     // Playlist row height in px (0 = the default)
    std::uint32_t colour = 0; // header colour 0xFFRRGGBB (from the .flp or "Rename and color"); 0 = default
    // Track groups (FL's "Group with above track"): a grouped track belongs to the group of the nearest
    // ungrouped track above it (the group's header). `collapsed` on a header folds its members into thin rows.
    bool grouped = false;
    bool collapsed = false;
    // A comp track (round 177c, core/Comping.h): this group header is what plays, its grouped tracks are take lanes. Set
    // only by "Comp these takes"; a group of takes is an ordinary track group until then.
    bool compTrack = false;
    // FL's "Track mode" (REA round 146a; .flp record 0xEE: u32 mode at byte 49 = 0 / 1 audio / 3 instrument, i32 mixer
    // track at 53, i32 channel at 57). An audio track is linked to a mixer track: audio clips placed on it play through
    // that track (their channel is routed there). An instrument track belongs to a channel (and its mixer track).
    PlaylistTrackMode mode = PlaylistTrackMode::unassigned;
    int mixerTrack = -1; // audio / instrument tracks: the linked mixer track (-1 = none)
    int channel = -1;    // instrument tracks: the channel
    TrackPerformance performance {}; // FL's Performance settings (round 165m)
    // FL's "Lock to content" (track record +0x19, .flp 0xEE byte 21; REA round 168f): painting on the track places its
    // own content (the clip the track holds most) instead of a source the track doesn't hold yet.
    bool lockedToContent = false;
    // FL's "Lock to this size" (track record +0x14 i32, .flp 0xEE bytes 17…20; REA round 168f): > 0 = the row keeps this
    // height in px whatever the vertical zoom and the Size menu; ≤ 0 = not locked (FL keeps −the last locked height).
    int lockedHeight = -1;
    int icon = 0; // FL's playlist track icon (.flp 0xEE u32 at 8, REA round 168j): FL's icon index, 0 = none. Kept and saved.
    std::vector<AutomationLane> lanes {}; // round 177b: optional automation lanes (shared by every arrangement)
    bool lanesFolded = false;          // the lanes are hidden under the track
    // round 190cg (Live's track fold switch): the row is folded to a thin strip that still shows its clips as bars; its
    // lanes hide with it. View only; saved with the project.
    bool folded = false;
    bool operator== (const PlaylistTrack&) const = default;
};

// Splits a clip at `tick` (absolute): the original keeps the start and the return value is the rest.
// `tick` must be inside the clip.
Clip splitClip (Clip& clip, std::int64_t tick);

// A Playlist arrangement (FL allows several per project).
// A bar on the ruler: where it starts, how many beats (quarter notes) it has and its number (1, 2, ...).
struct Bar
{
    std::int64_t tick = 0, length = 0;
    int number = 1;
};

// FL's legacy Pattern Blocks (FL ≤ 9, FL 10's "Enable legacy pattern blocks"; issue #60, docs/analise/REA_rodada170f.md):
// one row per pattern under the clip tracks; a block is a whole instance of its pattern starting on a bar. Its length
// is never stored: always the pattern's effectiveLength (core/PatternBlocks.h).
struct PatternBlock
{
    int pattern = 0;
    std::int64_t start = 0; // ticks: the start of a bar
    bool operator== (const PatternBlock&) const = default;
};

struct Arrangement
{
    std::string name;
    std::vector<Clip> clips;               // empty for the current arrangement: its clips live in Song::playlist
    std::int64_t loopStart = 0, loopEnd = 0;
    std::vector<Marker> markers;           // sorted by tick (stay here even for the current arrangement)
    std::uint64_t uid = 0;                 // stable id (Online Sessions, round 168a)
    std::vector<PatternBlock> blocks {};   // Pattern Blocks (round 170f); empty for the current one: Song::blocks
    bool operator== (const Arrangement&) const = default;
};

// The groove pool (round 190bv; logic in core/Groove.h): timing and accent templates a clip of the grid plays through
// without its notes changing. The numbers of GrooveBase are those a groove file stores as its grid (0..5).
enum class GrooveBase : std::uint8_t { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond };

struct GroovePoint
{
    std::int64_t tick = 0;  // where the template's note is, from the template's start
    float velocity = 1.0f;  // 0..1
    bool operator== (const GroovePoint&) const = default;
};

struct Groove
{
    std::uint32_t id = 0;   // > 0, unique in the pool: what a clip refers to
    std::string name;
    GrooveBase base = GrooveBase::sixteenth;
    float quantize = 0.0f;  // 0..1: straight quantisation to the base, before the template
    float timing = 1.0f;    // 0..1: how far the template moves the notes
    float random = 0.0f;    // 0..1: random timing, drawn again on every pass
    float velocity = 0.0f;  // -1..1: how far the template's accents change the notes' velocity (negative: reversed)
    std::int64_t length = 4 * ppq; // the template repeats every `length` ticks
    std::vector<GroovePoint> points; // sorted by tick
    bool operator== (const Groove&) const = default;
};

struct GroovePool
{
    std::vector<Groove> grooves;
    float amount = 1.0f;    // 0..1.3: scales Timing, Random and Velocity of every groove
    bool operator== (const GroovePool&) const = default;
};

// The clip grid of the Clip-launch workflow (round 181a; logic in core/ClipLauncher.h). Absent (Song::clipGrid empty) until
// the workflow or the grid is first used: a project without one carries nothing but the empty optional.
// LaunchQuant, LaunchMode and FollowKind keep the numbers a Live set stores (docs/ALS_FORMAT.md §5), read in Live 12's own
// GUI (docs/analise/REA_rodada181a.md, 181b): clip LaunchQuantisation 0..14, LaunchMode 0..3, FollowActionA 0..9 map
// directly. A set-wide GlobalQuantisation has no Global entry: its number is LaunchQuant's minus 1 (9 = 1/8).
enum class LaunchQuant : std::uint8_t
{
    global, none, bars8, bars4, bars2, bar, half, halfTriplet, quarter, quarterTriplet, eighth, eighthTriplet, sixteenth,
    sixteenthTriplet, thirtySecond
};
enum class LaunchMode : std::uint8_t { trigger, gate, toggle, repeat };
// A follow action, in the order of the manual's list, which is also the numbers a set stores (FollowActionA, seen in Live)
enum class FollowKind : std::uint8_t { none, stop, playAgain, previous, next, first, last, any, other, jump };

struct ClipFollow
{
    bool enabled = false;
    FollowKind a = FollowKind::next, b = FollowKind::none;
    int chanceA = 100, chanceB = 0;
    int jumpA = 0, jumpB = 0;       // jump: the target row
    bool linked = true;             // after `iterations` passes of the clip; off: after `ticks`
    int iterations = 1;
    std::int64_t ticks = 4 * 960;
    bool operator== (const ClipFollow&) const = default;
};

// What a clip of the grid keeps besides the Clip itself. A Clip finds its entry through Clip::gridSlot.
struct ClipGridSlot
{
    int scene = 0;
    std::string name;
    std::uint32_t colour = 0;       // 0xFFRRGGBB, 0 = the source's colour
    LaunchMode mode = LaunchMode::trigger;
    LaunchQuant quant = LaunchQuant::global;
    bool legato = false;            // takes over the position of the clip it replaces
    bool loop = true;               // off: plays once, then the track stops
    bool noStop = false;            // an empty slot without its stop button (a scene leaves that track alone): its Clip has length 0
    ClipFollow follow;
    std::uint32_t groove = 0;       // the groove of the project's pool this clip plays through (Groove::id; 0 = none; core/Groove.h)
    bool operator== (const ClipGridSlot&) const = default;
};

struct ClipGridScene
{
    std::string name;
    std::uint32_t colour = 0;
    double tempo = 0.0;   // > 0: the song takes this tempo when the scene is launched
    int beatsPerBar = 0;  // > 0: and this bar length
    bool operator== (const ClipGridScene&) const = default;
};

// The Session mixer under the clip grid (round 190dr): Live's View > Mixer (the set's MixerInSession) and View > Mixer
// Controls > In/Out, Sends, Volume, Track Options (SessionIO, SessionSends, SessionVolume, SessionTrackOptions). The
// defaults are those of Live's default set. View only: the strips show and edit the mixer inserts the tracks play
// through (core/SessionMixer.h). Saved in .lynx only when it differs from the defaults.
struct SessionMixerView
{
    bool shown = true, io = true, sends = true, volume = true, trackOptions = false;
    bool operator== (const SessionMixerView&) const = default;
};

struct ClipGrid
{
    // Columns are Playlist tracks (Clip::track), rows are scenes (ClipGridSlot::scene). Every Clip has start 0: it has no
    // place on a timeline. Song::forEachClipList visits this list, so every reference to a pattern, channel or track
    // follows edits as the Playlist's clips do.
    std::vector<Clip> clips;
    std::vector<ClipGridSlot> slots; // an entry no clip refers to is free
    std::vector<ClipGridScene> scenes;
    LaunchQuant globalQuant = LaunchQuant::bar;
    bool followActions = true; // follow actions on the whole grid (off: edit playing clips with no jumps)
    bool backOnStop = false; // "Give tracks back to the Playlist when the transport stops" (off: they stay taken until asked)
    SessionMixerView mixerView; // round 190dr: the Session mixer's sections
    bool operator== (const ClipGrid&) const = default;
};

// The project's credits (FL's Settings › Info page): shown and edited in Settings › Info, kept in .lynx, imported from .flp.
struct ProjectInfo
{
    std::string title, author, genre, url, comments;
    bool showOnOpen = false; // open Settings › Info when the project is opened
    // FL's Project info › created / working time (event 0xED, REA round 167g): when the project was created (seconds
    // since 1970, UTC; 0 = unknown) and the wall-clock time it has been open, summed over every session up to the last
    // save (FL: Now − the time it was opened + the saved time)
    double created = 0.0;
    double workSeconds = 0.0;

    bool empty() const noexcept
    {
        return title.empty() && author.empty() && genre.empty() && url.empty() && comments.empty() && ! showOnOpen && created == 0.0
               && workSeconds == 0.0;
    }
    bool operator== (const ProjectInfo&) const = default;
};

// FL's Project info › "Reset working time" (round 190bx): the project starts counting now (`nowSeconds` since 1970, UTC).
inline void resetWorkTime (ProjectInfo& info, double nowSeconds) noexcept
{
    info.created = nowSeconds;
    info.workSeconds = 0.0;
}

// Online Sessions (docs/design/ONLINE_SESSIONS.md §2, round 168a): who works on a project. The host (the project's
// owner) has tag 1; each friend the project is sent to gets the next tag, and the entities he creates carry it in
// their uid (uids::tagOf), so "his area" needs no table. Inserts and Playlist rows are positional: each guest gets a
// reserved range of them.
namespace session
{
constexpr int maxParticipants = 4;     // the host included (the user's decision 3, 2026-11-01)
constexpr std::uint32_t hostTag = 1;
constexpr int insertsPerGuest = 20;    // reserved inserts per guest (grown on demand past them)
constexpr int rowsPerGuest = 16;       // reserved Playlist rows per guest
} // namespace session

struct SessionParticipant
{
    std::uint32_t tag = 0;
    std::string name;
    std::uint32_t colour = 0;          // 0xFFRRGGBB: his area's stripe / header colour
    int insertFirst = 0, insertCount = 0; // reserved inserts [first, first + count)
    int rowFirst = 0, rowCount = 0;       // reserved Playlist rows
    bool keepFull = false;             // the host lets him keep a full copy of the project at the end (decision 1)
    bool ownsInsert (int t) const noexcept { return insertCount > 0 && t >= insertFirst && t < insertFirst + insertCount; }
    bool ownsRow (int r) const noexcept { return rowCount > 0 && r >= rowFirst && r < rowFirst + rowCount; }
    bool operator== (const SessionParticipant&) const = default;
};

struct SessionInfo
{
    std::uint32_t me = session::hostTag; // who edits this copy of the project (the host, or the guest it was sent to)
    std::vector<SessionParticipant> participants; // the host first, then the guests
    const SessionParticipant* find (std::uint32_t tag) const noexcept
    {
        for (const auto& p : participants)
            if (p.tag == tag)
                return &p;
        return nullptr;
    }
    SessionParticipant* find (std::uint32_t tag) noexcept
    {
        for (auto& p : participants)
            if (p.tag == tag)
                return &p;
        return nullptr;
    }
    const SessionParticipant* self() const noexcept { return find (me); }
    bool isGuestCopy() const noexcept { return me != session::hostTag; }
    // insert / row `i` is in another participant's reserved range (this copy's own new things don't go there)
    bool reservedForOther (bool insert, int i) const noexcept
    {
        for (const auto& p : participants)
            if (p.tag != me && (insert ? p.ownsInsert (i) : p.ownsRow (i)))
                return true;
        return false;
    }
    bool empty() const noexcept { return me == session::hostTag && participants.empty(); }
    bool operator== (const SessionInfo&) const = default;
};

// The Master look the Finish dock last applied (round 189k S8, core/MasterLooks.h); id 0 = none. Saved in .lynx.
struct MasterLookSettings
{
    int id = 0;
    float intensity = 0.5f;           // 0…1 (Light 0.25, Normal 0.5, Heavy 0.85)
    std::array<float, 3> tone {};     // Low, Mid, High in dB, −6…+6
    float inputDb = 0.0f;             // Auto level's input gain (Parametric EQ 2's main level), dB
    bool operator== (const MasterLookSettings&) const = default;
};

struct Song
{
    double bpm = 130.0;
    int beatsPerBar = 4;
    float swing = 0.0f; // 0..1: delays off-beat sixteenth notes (like the swing of FL's Channel Rack)
    // the seed of the notes' Chance and Velocity Range draws (round 187c, core/NoteChance.h); saved, "New chance seed"
    std::uint32_t chanceSeed = 0;
    std::vector<std::string> channelGroups; // the Channel Rack's filter groups (FL's "All" dropdown), by name
    float masterPitch = 0.0f; // cents, -1200..1200: shifts every note of the built-in instruments (the toolbar's master pitch)
    ProjectInfo info;
    MasterLookSettings masterLook;
    // Online Sessions (round 168a): the project's lineage (random 128-bit hex, kept by copies and packages), the last
    // uid counter used and who works on it (uids::normalize fills the first two)
    std::string projectId;
    std::uint64_t nextUid = 0;
    SessionInfo session;
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;

    std::vector<Clip> playlist;
    // FL's legacy Pattern Blocks (round 170f, issue #60): the per-project option (Playlist menu "Pattern blocks") and the
    // current arrangement's blocks, sorted by pattern then start (core/PatternBlocks.h). They play only while it is on.
    bool patternBlocksOn = false;
    std::vector<PatternBlock> blocks;
    PlayMode mode = PlayMode::pattern;
    std::int64_t loopStart = 0, loopEnd = 0; // looped section in the Playlist (ticks); loopEnd <= loopStart = off
    // round 190cg (Live's Arrangement Loop switch; FL has none): the range above stays (the time selection, the export
    // range, the punch region) but playback does not loop it. False = FL's behaviour: a range loops. Saved with the project.
    bool loopOff = false;
    // The tempo lane's shown range in BPM (round 190cg; Live's range fields beside the tempo envelope): an automation
    // lane of the tempo spans tempoViewLow…tempoViewHigh instead of the whole 10…522. View only: the values stay.
    // 0 / 0 = the whole range (every project before this round). Saved with the project.
    double tempoViewLow = 0.0, tempoViewHigh = 0.0;

    // Arrangements: `playlist` and the loop above belong to the current one; the others are stored here.
    std::vector<Arrangement> arrangements { Arrangement { "Arrangement 1", {}, 0, 0, {} } };
    int currentArrangement = 0;

    void selectArrangement (int index);
    int addArrangement (const std::string& name, bool copyCurrent = false); // returns the index (already selected)
    void removeArrangement (int index);
    // Applies `fn` to all targets (automations and MIDI controllers).
    template <typename Fn>
    void forEachTarget (Fn&& fn)
    {
        for (auto& a : automations)
            fn (a.target);
        for (auto& l : midiLinks)
            fn (l.target);
        for (auto& v : initialValues)
            fn (v.target);
        for (auto& p : patterns)
            for (auto& lane : p.events)
                fn (lane.target);
        for (auto& m : modulators)
            for (auto& r : m.routes)
                fn (r.target);
    }
    void removeDeadMidiLinks();

    // Applies `fn` to all clip lists (the current one, the stored ones and the clip grid's).
    template <typename Fn>
    void forEachClipList (Fn&& fn)
    {
        fn (playlist);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].clips);
        if (clipGrid)
            fn (clipGrid->clips);
    }
    template <typename Fn>
    void forEachClipList (Fn&& fn) const
    {
        fn (playlist);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].clips);
        if (clipGrid)
            fn (clipGrid->clips);
    }

    // Applies `fn` to all Pattern Block lists (the current arrangement's and the stored ones).
    template <typename Fn>
    void forEachBlockList (Fn&& fn)
    {
        fn (blocks);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].blocks);
    }
    template <typename Fn>
    void forEachBlockList (Fn&& fn) const
    {
        fn (blocks);
        for (std::size_t i = 0; i < arrangements.size(); ++i)
            if ((int) i != currentArrangement)
                fn (arrangements[i].blocks);
    }

    bool hasLoop() const noexcept { return loopEnd > loopStart; }
    bool loopPlays() const noexcept { return hasLoop() && ! loopOff; } // the range is looped by playback

    // FL's Performance mode (Playlist › Options › Performance mode, Ctrl+P; round 165m): the clips before the first
    // "Start" marker (the performance zone) don't play with the song; they are launched live (click, controller pads)
    // and loop on their track by its Performance settings, while the song plays from the Start marker on.
    bool performanceMode = false;
    std::optional<ClipGrid> clipGrid; // the Clip-launch workflow's grid (round 181a); empty until it is first used
    GroovePool grooves; // the groove pool (round 190bv); empty in every project that never used one
    // The end of the performance zone: the first Start marker's tick, or 0 (no zone) without one / when off.
    std::int64_t performanceZoneEnd() const noexcept;
    bool inPerformanceZone (const Clip& clip) const noexcept { return performanceMode && clip.start < performanceZoneEnd(); }

    // Markers of the current arrangement.
    std::vector<Marker>& markers();
    const std::vector<Marker>& markers() const;
    void addMarker (Marker marker); // keeps the order

    // Bars that play in [from, to), taking the markers' time signature changes into account
    // (before the first one, beatsPerBar/4 applies).
    std::vector<Bar> barsBetween (std::int64_t from, std::int64_t to) const;
    Bar barAt (std::int64_t tick) const;

    std::vector<MixerTrack> mixer; // [0] = Master; empty = everything straight to the output
    std::vector<Vca> vcas;         // VCA faders (round 177a); empty = none (no cost anywhere)
    // A/B of the LYNX substitutes (round 113): when set, every substitute slot plays as bypassed (the project's own
    // effects and the hosted plugins still play). Saved with the project.
    bool substitutesBypassed = false;
    // Clean mixer (round 163, Options › Mixer): the inserts have no built-in processing — the per-track EQ is bypassed
    // (its settings are kept, nothing is computed) and hidden from the mixer. Saved with the project.
    bool cleanMixer = false;
    // FL's Options › Project settings › Playlist › "Automatic crossfades" (round 166e): overlapping audio clips on a
    // track crossfade over the overlap (Clip::crossfadeIn / crossfadeOut). Saved with the project; a project saved
    // without it loads with it off (FL: "old projects now load with automatic crossfades turned off"); new projects
    // take Settings' "Enable automatic crossfades in new projects".
    bool autoCrossfades = false;
    // LYNX comping (round 177c, core/Comping.h): the length of the equal-power crossfade where a comp switches takes (ms,
    // 0 = a hard cut). Saved with the project when it is not the default.
    static constexpr float defaultCompCrossfadeMs = 5.0f;
    float compCrossfadeMs = defaultCompCrossfadeMs;
    // Comping: the take lane being auditioned (a Playlist track, -1 = none) and its comp track. While set, the lane
    // plays (its muted take clips too) instead of the comp track. Not saved; comp::setAudition sets both.
    int auditionLane = -1, auditionComp = -1;
    // FL's Project › "Play truncated notes in clips" (REA round 167g, .flp 0x1E): a note that starts before a pattern
    // clip's start (the clip trimmed or sliced into it) plays from the clip's start with what is left of its length
    // (FL FUN_010608a0); off = such notes are dropped (LYNX's behaviour before; projects saved before keep it off).
    bool playTruncatedNotes = false;
    // FL 2026 (round 190y renders, issue #144): a muted insert keeps running its effects; only its mixer routes (to the
    // Master and its sends) are cut, so a Fruity Send in it is still heard and its controllers still move. Off = LYNX's
    // behaviour before (the insert skipped its effects); projects saved before keep it off, .flp imports and new
    // projects turn it on. Saved with the project.
    bool muteRunsEffects = false;
    // LYNX option (round 190y, off): "Muted inserts don't process" — a muted insert skips its effects, EQ and meter, as
    // LYNX did before (saves CPU; unlike FL, a Fruity Send or controller in it goes quiet and its effects start fresh at
    // unmute). Saved with the project only when on; an .flp export keeps FL's behaviour.
    bool mutedInsertsSkip = false;
    // FL's Options › Project › "Fast declick for cut groups" (REA round 167h §3, .flp 0x28): a voice cut by a cut group or
    // by "cut itself" fades out in 2 ms instead of its channel's declicking time. Saved with the project.
    bool fastDeclick = false;
    // FL's Options › Project › "Panning law" (REA round 167j §1, .flp 0x17 = 2 × the mode): circular = cos/sin (the
    // mixer's centre 0 dB, +3 dB on the side panned to), triangular = linear (the louder side stays at the level, the
    // other falls to 0). It sets the mixer inserts' pan (FLEngine FUN_0127b050) and the FL Sampler voices' pan.
    bool panTriangular = false;
    // FL's levels for hosted plugins (round 190ad, REA_rodada190ad.md): Fruity Wrapper scales a plugin's output by the
    // host's pan kernel of (channel pan, 2 × the channel volume curve), i.e. √2 (+3.01 dB) at the centre where LYNX's
    // own channel law is 1, and sends a note's velocity as round(128 v) where LYNX sends round(127 v). On: this
    // project's hosted plugin channels follow FL (set by the .flp import, saved as "pluginLevels": "fl"). Off: LYNX's
    // own law, which every project saved before keeps (same sound as when it was made).
    bool flPluginLevels = false;
    // FL's mixer menu › Plugin delay compensation (REA round 168j, .flp 0x1D / 0x27): "Automatic" off = the delay
    // compensation ignores the plugins' latencies (FL's planner FUN_010ff3b0 skips the generators' and the effects'
    // latencies; the tracks' manual latencies still apply); "Compensate automations" = automation of a mixer effect
    // whose input the compensation delays is delayed with it (the value it gets is the one at its delayed position).
    bool autoPdc = true;
    bool compensateAutomations = true;
    // FL's Options › Project › Time settings (REA round 168j, .flp 0x23, TimeSettingComboChange): show the project's time
    // settings as a time signature (beats per bar / beat unit) instead of FL's time division (beats per bar, steps per beat)
    bool timeSignatureMode = false;
    // FL's Piano Roll scale highlighting (View › scale menu, .flp 0xF3, REA round 168j): `scale` ≥ 0 = FL's reference
    // scale (notetools::flReferenceScales(), FL's alphabetical list), −1 Default, −2 Black notes, −3 Automatic;
    // `root` 0 = C … 11 = B. Saved with the project.
    struct ScaleHighlight
    {
        int scale = -2, root = 0;
        bool operator== (const ScaleHighlight&) const = default;
    };
    ScaleHighlight scaleHighlight;
    // The project's own chord set for the keyboard's Chord pad (round 190ds, core/ScaleKeys.h): from an FL Studio Mobile
    // project's KEYB chunk, or edited on a pad. Empty = the pads play the project key's diatonic chords.
    std::vector<KeyboardChord> keyboardChords;
    // FL's Options › Project › Data folder (.flp 0xCA, ProjDataPathBtnClick "Select the directory where your project data
    // is to be stored..."): kept and saved (empty = FL's default next to the project)
    std::string dataFolder;
    // whether an effect slot processes: switched on, and not a substitute while the substitutes are bypassed
    bool slotActive (const EffectSlot& slot) const noexcept { return slot.enabled && ! (substitutesBypassed && slot.isSubstitute()); }
    std::vector<Automation> automations;
    std::vector<Modulator> modulators; // round 177b: LYNX's modulators (empty: no cost)
    std::vector<PlaylistTrack> playlistTracks; // index = track; tracks past the end use the default
    // Video tracks and the project's video settings (round 174d, core/VideoTrack.h): LYNX's own, above the clip tracks
    std::vector<VideoTrack> videoTracks;
    VideoSettings video;
    AdrSession adr; // the ADR cue list, its takes and its settings (round 174d, core/Adr.h)
    std::vector<MidiLink> midiLinks;
    // FL's "initialized controls" (.flp 0xD8; the parameter menu's "Init song with this position", REA round 146):
    // values the targets are reset to whenever the song starts playing in song mode (from any position), before
    // the automation clips take over at their own position.
    struct InitialValue
    {
        AutomationTarget target;
        float value = 0.0f; // 0..1
        bool operator== (const InitialValue&) const = default;
    };
    std::vector<InitialValue> initialValues;
    // Sets (replaces) / removes the target's initial value; the index into initialValues or -1.
    void setInitialValue (const AutomationTarget& target, float value);
    void removeInitialValue (const AutomationTarget& target);
    int findInitialValue (const AutomationTarget& target) const noexcept;
    // Resets the model's targets to their initial values; the plugin parameters among them (which don't live in
    // the model) are passed to `plugin` when given.
    void applyInitialValues (const std::function<void (const AutomationTarget&, float)>& plugin = {});

    bool isTrackMuted (int track) const noexcept
    {
        return track >= 0 && track < (int) playlistTracks.size() && playlistTracks[(std::size_t) track].muted;
    }

    static std::vector<MixerTrack> defaultMixer (int inserts = 16);
    // Whether anything uses insert `t`: a channel, a send to or from it, effects, an input, a Playlist link, a custom
    // name, its fader / pan / mute / colour / EQ / output moved from the defaults or automation on it. Master: true.
    bool insertInUse (int t) const;
    // First insert nothing uses (insertInUse false); 0 (Master) if none.
    int nextFreeInsert() const;
    // nextFreeInsert, but when every insert is in use a new one is added (up to maxMixerTracks), like FL where a new
    // channel takes the next of its 125 inserts (round 162); 0 (Master) only when the mixer is full.
    int claimFreeInsert();

    // End of the last clip, exactly where FL loops Song mode (no clip: the current pattern's length, minimum 1 bar).
    std::int64_t songLength() const noexcept;

    // Removes a pattern and fixes the clips and the current index.
    void removePattern (int index);

    // Removes a channel along with its notes and audio clips, and reindexes the others.
    // Automations of that channel lose their target.
    void removeChannel (int index);

    // Routing between inserts. canRoute = valid destination that doesn't create a cycle.
    bool canRoute (int from, int to) const;
    // Enables/disables the send from -> to (to == 0 = Master). Does nothing if it would create a cycle.
    void setRoute (int from, int to, bool enabled, float level = 1.0f);
    bool routes (int from, int to) const noexcept;
    // Level of the route from -> to (to == 0 = Master: `masterLevel`), a linear gain 0..maxFaderGain; 0 without a route.
    float routeLevel (int from, int to) const noexcept;
    // Sets the level of an existing route (FL's route knob); does nothing if there is no such route.
    void setRouteLevel (int from, int to, float level);
    // LYNX's pre-fader switch of the send from -> to (MixerSend::pre); false / nothing without such a send (the route
    // to Master is always post-fader).
    bool sendPre (int from, int to) const noexcept;
    void setSendPre (int from, int to, bool pre);
    // Inserts (1..N) in processing order: senders come before receivers.
    std::vector<int> mixerOrder() const;
    // Tracks audible with the current solo: the soloed ones, those that feed them and the path to Master.
    // Without solo, all of them. Index = track.
    std::vector<bool> soloAudible() const;

    // Swaps/removes effect slots keeping automations pointing at the right effect.
    void swapEffectSlots (int track, int a, int b);
    // Round 168s: gives a new instance tag to every channel / top-level effect slot whose tag an earlier one already has
    // (a pasted or duplicated copy), so each running instrument / effect follows one channel / slot only.
    void uniqueInstances();
    void removeEffectSlot (int track, int slot);

    // Removes an automation and its clips.
    void removeAutomation (int index);

    // Current value (from the model, without automation) of the target, 0..1; -1 if the target doesn't exist.
    float targetValue (const AutomationTarget& target) const noexcept;
    // Changes the target's value in the model (0..1). Plugin parameters don't live here: returns false.
    bool setTargetValue (const AutomationTarget& target, float value) noexcept;

    // Value of an automation at a song position (the same one playback uses); NaN without clips.
    float targetValueAt (const AutomationTarget& target, double tick) const; // the clips of that target, as playback plays them
    // Tempo at `tick`, taking tempo automation into account (in song mode).
    double tempoAt (double tick) const;
    // Duration in seconds of the first `ticks` of the song, integrating the tempo automation.
    double secondsForTicks (std::int64_t ticks) const;

    static Song makeDefault();
};

// The Fruity Send slots of insert `track` that send to another existing insert, in slot order (only the first
// maxEffectSlots slots run): {slot index, destination insert}. The renderer, the PDC plan and the routing order
// all enumerate them this way. Round 189j: also a hosted effect's output pairs routed elsewhere by its Connections
// (SlotConnections below; the destination may be the Master, 0; always one of the insert's routes).
struct SlotSendTap
{
    int slot = 0, target = 0, output = 0;
    bool afterSlot = false; // the send leaves after the slot's own processing (Transmitter), not at its input
};
std::vector<SlotSendTap> fruitySendsOf (const Song& song, int track);

// A hosted effect's Processing › Connections as the engine plays them (FL's wrapper, rounds 188d / 189j [EXEC FL 2026
// renders, docs/analise/REA_rodada189j.md]). `routed`: the slot's Connections were set (PluginRef inputs / outputs
// not empty, as every effect of an FL project has them); then FL's routing plays: each input takes entry k of the
// insert's inputs list (k = 0: the slot's own input), the outputs at index 0 are summed into the insert and an output
// with index k goes to entry k of the insert's outputs list. Not routed: the input in, output 1 out, input 2 fed only
// by the slot's sidechain source (LYNX as before).
constexpr int maxPluginConnections = maxSlotSends; // output pairs that may go elsewhere (the slot taps)
struct SlotConnections
{
    bool routed = false;
    int mainSource = -1; // input 1: −1 the slot's own input, −2 silence, ≥ 1 the insert whose audio replaces it
    int sideSource = -1; // input 2 (routed): −1 the slot's own input, −2 silence, ≥ 1 that insert (slot.sidechain, else its index)
    std::array<int, maxPluginConnections> outputs; // per output pair: −1 into the insert, −2 silent, ≥ 0 that track
    SlotConnections() { outputs.fill (-1); }
};
SlotConnections slotConnections (const Song& song, int track, const EffectSlot& slot);
// FL's inputs list of an insert (REA_rodada22 §1): [the insert, then every insert routed to it at any level,
// ascending]; its outputs list [EXEC fxconn189j]: [the insert, then every track it is routed to, the Master first]
std::vector<int> insertInputList (const Song& song, int track);
std::vector<int> insertOutputList (const Song& song, int track);

// Pattern clips follow their pattern's length, like FL's: after an edit, the clips of a pattern whose length
// changed get the new length when they had the old one and start at the pattern's start (an auto-sized clip;
// a clip resized or cut by hand keeps its length). `before` = patternLengths (song) before the edit.
// (Issue #4: a pattern set to 2 bars still looped 1 bar in Song mode.)
std::vector<std::int64_t> patternLengths (const Song& song);

// FL's General › "Detach all new plugins" (issue #116): how many fresh hosted plugins (Channel rack instruments and
// mixer effects with no saved state yet that are still docked: just added, not loaded or cloned) of each id the song
// has, and, after an edit, marking the ones the edit added (an id the song has more fresh ones of than before; the
// newest of them) as Detached. Returns how many. A clone or a loaded plugin is never counted, so adding one leaves
// the fresh plugins that were already there (docked again by the user) alone.
std::map<std::string, int> freshPluginCounts (const Song& song);
int detachNewPlugins (const std::map<std::string, int>& before, Song& song);
// FL's Tools › Macros › "Switch all plugin editors to" › Attached / Detached (round 190i, issue #116 [STR: FLEngine's
// main menu, SwitchAllPluginsToMenu with MacroSwitchEditorsToAttachedAction / …DetachedAction; EXEC:
// docs/analise/REA_rodada190i.md]): every hosted plugin's Detached flag, instruments and effects. Returns how many changed.
int setAllPluginsDetached (Song& song, bool detached);
void followPatternLengths (const std::vector<std::int64_t>& before, Song& song);

// Copy of the song ready for export: always in song mode (the current pattern becomes a clip
// if `wholeSong` is false or the Playlist is empty), with `tailTicks` of silence at the end
// so releases and reverbs ring out without the song restarting. `musicLength` receives the end of the
// musical part (without the tail), in ticks.
Song makeExportSong (const Song& song, bool wholeSong, std::int64_t tailTicks, std::int64_t& musicLength);

// Copy of the song in which only the (post-fader) output of insert `track` is heard, straight to Master
// without effects or fader: this is what gets rendered to "turn the insert into audio".
Song makeBounceSong (const Song& song, int track);

// After rendering the insert to `samplePath` (`length` ticks from the start): creates an
// audio clip channel on a new insert with the same routing, puts the clip in the Playlist and mutes
// the original insert. Returns the new insert.
int applyBounce (Song& song, int track, const std::string& samplePath, std::int64_t length);
// The first channel (rack order) with notes in pattern `pattern`, −1 if none: FL's "Select first channel with notes
// when the pattern changes" (FLEngine FUN_00e67f20 over the channels' note counts, REA_rodada167j §5).
int firstChannelWithNotes (const Song& song, int pattern) noexcept;

} // namespace lynx
