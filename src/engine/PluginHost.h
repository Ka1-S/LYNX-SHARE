#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Effects.h"
#include "core/PluginCatalog.h"
#include "core/FixedBlock.h"
#include "core/FlPluginDb.h"
#include "core/HostNotePitch.h"
#include "core/PluginSubstitutes.h"
#include "core/RemotePlugins.h"
#include "core/PluginHold.h"
#include "core/Instruments.h"
#include "core/MidiControls.h"
#include "core/Mpe.h"
#include "core/Vfx.h"
#include "core/ModuleWarmup.h"

namespace lynx
{
// FL's "Notify about rendering mode" (wrapper option, on by default): tells the plugin an export is running
// (VST3 kOffline process mode, VST2 offline process level) so it can use its render quality. Re-prepares the plugin
// when the mode changes; call only while it is not processing (offline renders, before/after the render loop).
void setPluginRenderMode (juce::AudioPluginInstance& plugin, bool offline, int blockSize);

// Block length of "Use fixed size buffers" (PluginRef::fixedBuffers): 2 ms as FL's wrapper, or the audio device's
// buffer with "Process maximum size buffers" (flFixedBlockSize; set when the device starts, 512 / 44.1 kHz until
// then). Plugins created afterwards use it.
int fixedPluginBlock (bool maximumSize) noexcept;
void setFixedPluginBlock (int deviceBlock, double sampleRate) noexcept;

// Adapters: a JUCE plugin seen by the core as an Instrument/Effect.
// Notes become MIDI at the start of the next rendered chunk; since the core
// renders in chunks that start exactly at each event, timing is sample-accurate.
// Hosted plugins that crashed during this session (newest last; read from the UI thread): the plugin's name and the
// exception (platform/CrashGuard.h). `count` only grows, so the UI notices a new one by comparing it.
struct PluginCrash
{
    std::string plugin;
    std::uint32_t code = 0;
    // round 190dt: it crashed while its saved settings were being loaded into it (a project opening, an undo, a
    // reload): it was left out (the slot keeps those settings for the save) and reloading it would do the same again
    bool onLoad = false;
};
int pluginCrashCount() noexcept;
std::vector<PluginCrash> pluginCrashes();

// A hosted plugin's audio outputs as FL's wrapper lists them in Processing › Connections (round 188d, issue #114):
// one per output bus (bus 0 = the main output); a plugin with one bus of more than two channels (a bridged plugin)
// in stereo pairs. `channel` = its first channel in the plugin's buffer.
struct PluginOutputInfo
{
    juce::String name;
    int channel = 0, channels = 2;
    bool activeByDefault = true; // what the plugin declares (VST3 bus kDefaultActive, VST2 pin kVstPinIsActive)
};
std::vector<PluginOutputInfo> pluginOutputs (const juce::AudioPluginInstance& plugin);
// The same for the inputs (an effect's: input 1 = its main input, input 2 = the side input), every bus listed whether
// enabled or not (LYNX turns a side bus on only while it is fed)
std::vector<PluginOutputInfo> pluginInputs (const juce::AudioPluginInstance& plugin);

// FL's wrapper options a hosted plugin follows while it runs (REA round 151; PluginRef has them, saved per plugin)
struct HostedOptions
{
    bool releaseVelocity = true, sendBendRange = false, modXAftertouch = false, allNotesOffOnStop = true, resetOnFlush = true,
         sendLoopPosition = true;
    int bendRange = 12;
    bool mpe = false; // issue #108: one member channel per note (core/Mpe.h)
    static HostedOptions of (const PluginRef& ref) noexcept
    {
        return { ref.releaseVelocity, ref.sendBendRange, ref.modXAftertouch, ref.allNotesOffOnStop, ref.resetOnFlush, ref.sendLoopPosition, ref.bendRange,
                 ref.mpe };
    }
};

class PluginInstrument final : public Instrument
{
public:
    // fixedBlock > 0: FL's "Use fixed size buffers" — the plugin always gets blocks of that length (+ that much latency)
    PluginInstrument (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize, int fixedBlock = 0, HostedOptions options = {});
    ~PluginInstrument() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {} // prepared at creation (off the audio thread)
    void noteOn (int key, float velocity) noexcept override;
    void noteOff (int key) noexcept override;
    void allNotesOff() noexcept override;
    void setNextNoteModulation (float modX, float modY) noexcept override { nextModX = modX, nextModY = modY; }
    // issue #108 (MPE on): the note's bend / timbre / pressure on its member channel
    void noteExpression (int key, int prop, float value, float baseCents) noexcept override;
    void setNextNoteRelease (float velocity) noexcept override { nextRelease = velocity; }
    // issue #63: the note's own fine pitch, sent as FL's wrapper sends it (core/HostNotePitch.h)
    void setNextNoteFinePitch (float cents) noexcept override { nextFine = cents; }
    // Song::flPluginLevels (round 190ad): note velocities go out as FL's wrapper sends them
    void setFlWrapperVelocity (bool on) noexcept override { flVelocity.store (on, std::memory_order_relaxed); }
    void transportStopped() noexcept override;
    void panic() noexcept override;
    void transportReset() noexcept override { panic(); }
    void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept override;
    void setParameter (int index, float value) noexcept override;
    float parameterValue (int index) const noexcept override;
    int latencySamples() const noexcept override
    {
        return crashed.load (std::memory_order_relaxed) ? 0 : plugin->getLatencySamples() + (fixed != nullptr ? fixed->latency() : 0);
    }

    // The plugin crashed inside a call (caught by platform/CrashGuard.h): it is never called again, the slot plays
    // through (effect) or stays silent (instrument) and the UI is told (pluginCrashes()).
    bool hasCrashed() const noexcept { return crashed.load (std::memory_order_relaxed); }
    // The plugin's state for saving (base64, as capturePluginState), or nullopt after a crash (keep the saved one).
    std::optional<std::string> captureState();
    // Sets a parameter from the UI / controllers (setValueNotifyingHost), guarded like the audio calls.
    void setParameterFromUi (int index, float value);
    void setRenderMode (bool offline);
    void resetForExport(); // UI thread, before an export (the audio is not running through it)
    bool usesFixedBlocks() const noexcept { return fixed != nullptr; }
    // The MIDI "parameters" (core/MidiControls.h): CC / aftertouch / pitch bend values last asked for.
    float midiControlValue (int index) const noexcept { return midiControls.value (index); }
    void noteOnMidi (int key, float velocity, int midiChannel) noexcept override;
    void noteOffMidi (int key, int midiChannel) noexcept override;
    void midiMessage (const std::uint8_t* bytes, int size) noexcept override { midi.addEvent (bytes, size, 0); }
    void pitchBend (float) noexcept override {} // (the plugin takes the raw MIDI pitch bend, midiMessage)
    int takeMidiOutput (MidiOutNote* out, int max) noexcept override;
    // Issue #14: the channel's PITCH knob, fine tune and main pitch reach the plugin as MIDI pitch bend (scaled by the
    // wrapper's pitch bend range), and a slide note bends the sounding notes to its key over its length
    void setLivePitch (float cents) noexcept override { livePitch = cents; }
    void slideTo (int key, float cents, float samples) noexcept override;
    // FL's channel Porta / Mono (round 168l): a new note starts bent from the previous note's pitch and bends back
    bool followsChannelPorta() const noexcept override { return true; }
    int sentPitchBend() const noexcept { return sentBend; } // the last 14-bit bend sent (8192 = centre), for the tests

    // FL's wrapper Processing › Connections (round 188d, issue #114): each output of the plugin plays on the channel's
    // insert + its offset (offset 0: the channel's own audio), an inactive one is silent; the channel's volume and
    // pan apply to all of them, as FL [EXEC]. configure() takes the routes (UI thread; the audio thread reads atomics).
    const std::vector<PluginOutputInfo>& outputs() const noexcept { return outs; }
    void configure (const ChannelDef& channel, double bpm) noexcept override;
    void beginChunk (int n) noexcept override;
    int auxOutputCount() const noexcept override { return anyAux.load (std::memory_order_relaxed) ? (int) outs.size() : 0; }
    int auxOutputTrack (int k) const noexcept override;
    const float* auxOutputData (int k, int side) const noexcept override;

private:
    void mixOutputs (const float* const* channels, int numChannels, int at, int count, float gl, float gr, float* left, float* right) noexcept;
    std::vector<PluginOutputInfo> outs;
    std::unique_ptr<std::atomic<int>[]> routeOffset;   // [output]
    std::unique_ptr<std::atomic<bool>[]> routeActive;  // [output]
    std::atomic<int> channelTrack { 0 };
    std::atomic<bool> anyAux { false };                // an output plays somewhere else than the channel's insert
    static constexpr int auxCapacity = 16384;
    std::vector<std::array<std::vector<float>, 2>> aux; // [output] this chunk's audio for its insert
    int auxCursor = 0, chunkBase = 0;

    void failed (std::uint32_t code) noexcept;
    void addBend (int offset) noexcept;         // the bend the pitch asks for now, if it changed
    void bendBlock (juce::MidiBuffer& into, int base, int count) noexcept; // advances a slide over `count` samples
    float livePitch = 0.0f;                     // cents
    float slideNow = 0.0f, slideTarget = 0.0f;  // semitones away from the last note's key
    float slideLeft = 0.0f;                     // samples
    int lastKey = -1, sentBend = 8192;
    juce::MidiBuffer* bendInto = nullptr;
    void flushMidiControls() noexcept;
    void collectMidiOutput (const juce::MidiBuffer& buffer) noexcept; // the plugin's MIDI output (wrapper Output port)
    bool producesMidi = false;
    std::array<MidiOutNote, 256> midiOut {};
    int midiOutCount = 0;
    HostedOptions options;
    std::atomic<bool> flVelocity { false };
    juce::uint8 midiVelocity (float velocity) const noexcept;
    float nextModX = 0.5f, nextModY = 0.5f, nextRelease = 0.5f, nextFine = 0.0f;
    mpe::Allocator mpeNotes;      // issue #108 (the wrapper's MPE switch): each note's member channel
    void sendExpression (int channel, mpe::Expr expr, float value) noexcept; // on a member channel (0-based)
    hostpitch::SentKeys sentKeys; // issue #63: the key each note went out on; the VST3 tunings waiting for the host
    void tagSent (bool noteOn, int channel, int key, std::uint32_t note) noexcept; // round 190fg (SentKeys::tag)
    hostpitch::Format noteFormat = hostpitch::Format::midi; // how FL's wrapper sends this format's notes
    bool takesTuning = false;     // a VST3 plugin (its note ons carry the tuning)
    bool bendRangeSent = false;
    std::atomic<bool> resetPending { false };     // panic with "Reset plugin": reset at the next render (audio thread)
    HostedKeys heldKeys; // per MIDI channel: the notes on (their note offs on stop / jump, round 169c)
    void hold (int channel, int key, bool on) noexcept { heldKeys.set (channel, key, on); }
    void releaseHeldKeys() noexcept; // a note off for each held key
    HostBlockClock blockClock; // where each plugin block starts in the renderer's chunk (the play head's position)

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    midictl::State midiControls;
    int blockSize;
    std::string name;
    std::atomic<bool> crashed { false };
    std::unique_ptr<FixedBlock> fixed;
    std::vector<float> scratch; // fixed blocks: the delayed output before it is mixed in
    juce::MidiBuffer pending; // fixed blocks: MIDI waiting for the block being collected, at its offsets in it
};

class PluginEffect final : public Effect
{
public:
    // fixedBlock > 0: FL's "Use fixed size buffers" — the plugin always gets blocks of that length (+ that much latency)
    PluginEffect (std::unique_ptr<juce::AudioPluginInstance> instance, int maxBlockSize, int fixedBlock = 0, HostedOptions options = {});
    ~PluginEffect() override;

    juce::AudioPluginInstance& getInstance() noexcept { return *plugin; }

    void prepare (double) noexcept override {}
    void reset() noexcept override {}
    // "Reset plugins on transport" (round 167k): with the wrapper's "Reset plugin when FL Studio resets", the plugin is
    // reset before its next block
    void transportReset() noexcept override
    {
        if (options.resetOnFlush)
            resetPending.store (true, std::memory_order_relaxed);
    }
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override;
    void setParameter (int index, float value) noexcept override;
    float parameterValue (int index) const noexcept override;
    int latencySamples() const noexcept override
    {
        return crashed.load (std::memory_order_relaxed) ? 0 : plugin->getLatencySamples() + (fixed != nullptr ? fixed->latency() : 0);
    }

    // The plugin crashed inside a call (caught by platform/CrashGuard.h): it is never called again, the slot plays
    // through (effect) or stays silent (instrument) and the UI is told (pluginCrashes()).
    bool hasCrashed() const noexcept { return crashed.load (std::memory_order_relaxed); }
    // The plugin's state for saving (base64, as capturePluginState), or nullopt after a crash (keep the saved one).
    std::optional<std::string> captureState();
    // Sets a parameter from the UI / controllers (setValueNotifyingHost), guarded like the audio calls.
    void setParameterFromUi (int index, float value);
    void setRenderMode (bool offline);
    void resetForExport(); // UI thread, before an export (the audio is not running through it)
    bool usesFixedBlocks() const noexcept { return fixed != nullptr; }
    // Round 177b: a plugin with a second input bus (its sidechain input) gets the slot's sidechain there; a slot without
    // one (or a silent source) feeds it silence, as before.
    void setSidechain (const float* left, const float* right) noexcept override
    {
        sideL = left;
        sideR = right;
    }
    bool hasSideInput() const noexcept { return sideChannel >= 0; }
    // Issue #109: the plugin has a side input bus. It is on only while the slot has a sidechain source
    // (setSideInputActive, called by the rack build, never on the audio thread; the block that meets the change plays dry).
    bool canTakeSideInput() const noexcept { return sideBus; }
    void setSideInputActive (bool on) override;
    float midiControlValue (int index) const noexcept { return midiControls.value (index); }
    // Processing › Connections (round 189j, core/Song.h SlotConnections [EXEC FL 2026]): with them set, every output
    // pair at index 0 is summed into the insert, one at another index goes to its extra output (the renderer's tap
    // for the track it names), an inactive one is silent; input 1 may come from another insert (setMainInput).
    void setConnections (const SlotConnections& c) override;
    void setMainInput (const float* left, const float* right) noexcept override
    {
        mainSet = true;
        mainL = left;
        mainR = right;
    }
    void setExtraOutput (int output, float* left, float* right) noexcept override
    {
        if (output >= 0 && output < maxPluginConnections)
        {
            extraL[(std::size_t) output] = left;
            extraR[(std::size_t) output] = right;
        }
    }
    const std::vector<PluginOutputInfo>& outputs() const noexcept { return outs; }

private:
    void failed (std::uint32_t code) noexcept;
    void flushMidiControls() noexcept;
    void findSideChannel() noexcept;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    midictl::State midiControls;
    int blockSize;
    bool sideBus = false;   // the plugin has a second input bus (issue #109)
    std::mutex layoutLock;  // held by process() (try_lock) and by setSideInputActive while it lays the buses out again
    std::string name;
    std::atomic<bool> crashed { false };
    std::unique_ptr<FixedBlock> fixed;
    HostedOptions options;
    std::atomic<bool> resetPending { false }; // "Reset plugins on transport": reset before the next block (audio thread)
    int sideChannel = -1, sideCount = 0;      // the side input bus in the process buffer (round 177b); -1 = none
    const float* sideL = nullptr;             // this block's sidechain (set before process(), cleared after it)
    const float* sideR = nullptr;
    std::vector<float> sideScratch;           // fixed blocks: the sidechain copied (FixedBlock writes its io in place);
                                              // routed, output pair 2 comes back there
    // Connections (round 189j)
    void routeOutputs (const float* const* ch, int numChannels, int at, int count, float* left, float* right, bool taps) noexcept;
    std::vector<PluginOutputInfo> outs;       // the output pairs (pluginOutputs), fixed at construction
    std::atomic<bool> routed { false };
    std::array<std::atomic<int>, maxPluginConnections> outputMode {}; // −1 into the insert, −2 silent, ≥ 0 elsewhere
    std::array<float*, maxPluginConnections> extraL {}, extraR {};
    bool mainSet = false;                     // this block's input 1 from another insert (set before process())
    const float* mainL = nullptr;
    const float* mainR = nullptr;
};

// Round 177b: the channels of a plugin's second input bus (its sidechain input), 0 when it has none (or it is off).
int sideInputChannels (const juce::AudioPluginInstance& plugin);
// The plugin has a second input bus, on or off (issue #109: what decides whether the "Sidechain from" picker shows).
bool hasSideBus (const juce::AudioPluginInstance& plugin);
// --selftest: a stand-in plugin with a stereo sidechain bus (it outputs its side input) inside PluginEffect, plain and
// with fixed blocks: the slot's sidechain reaches the bus, and silence once the slot has none. "OK" or what failed.
juce::String pluginSidechainSelfTest();

// Plugin formats, known-plugin list (saved in AppData) and instance creation.
// UI thread, except the scan functions (any thread; one scan at a time) and the background scan.
class PluginHost
{
public:
    // persistent = read and write the list in %APPDATA%\LYNX (false in tests).
    explicit PluginHost (bool persistent = true);
    ~PluginHost();

    juce::KnownPluginList& getKnownPlugins() noexcept { return known; }
    juce::AudioPluginFormatManager& getFormats() noexcept { return formats; }

    static juce::File settingsFolder();          // %APPDATA%\LYNX
    // Did this process load any third-party plugin module (instance or in-process scan)? (then it ends without
    // unloading them, platform/CrashGuard.h exitNow)
    static bool pluginsWereLoaded() noexcept;
    static juce::FileSearchPath defaultSearchPath(); // default VST3 folders

    // The folders LYNX scans: the user's list (setup wizard / %APPDATA%\LYNX\plugin-folders.txt), else suggestedFolders().
    juce::FileSearchPath pluginFolders() const;
    void setPluginFolders (const juce::FileSearchPath& folders);
    // The standard VST3/CLAP folders plus the folders FL Studio uses on this PC (its configured VST folders and
    // the folders of the plugins in its plugin database), existing ones only.
    juce::FileSearchPath suggestedFolders();

    // FL Studio's plugin database on this PC (read once, on first use; empty without FL Studio). Thread-safe.
    const FlPluginDb& flPluginDatabase();
    int flPluginDatabaseFiles() { flPluginDatabase(); return flDbFiles; }

    // Scans the folders for plugins. `progress(0..1, file)` returns false to cancel.
    // Each file is examined in a separate process (LYNX.exe --scan-one): a plugin that
    // hangs or crashes the process doesn't take LYNX down; it is blacklisted and skipped
    // in later scans. Incremental: files already listed and unchanged since are skipped.
    int scan (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress);
    // The same for a given list of plugin files (targeted scan).
    int scanFiles (const juce::StringArray& files, const std::function<bool (float, const juce::String&)>& progress);

    // Background scan of pluginFolders() (or `paths`): never blocks the caller; poll backgroundScan() for progress.
    struct ScanState
    {
        bool running = false;
        float progress = 0.0f;
        juce::String file;  // being examined
        int found = 0;      // new plugins of the last (or current) background scan
        int finished = 0;   // how many background scans finished since start (changes when one ends)
    };
    void startBackgroundScan (const juce::FileSearchPath& paths);
    void startBackgroundScan() { startBackgroundScan (pluginFolders()); }
    ScanState backgroundScan() const;
    void cancelBackgroundScan();

    // Background scan of these files only (Plugin manager: "Try again", "Add plugin file").
    void startBackgroundScanOfFiles (const juce::StringArray& files);

    // Plugin manager: forgets a plugin file (its plugins and its failed-scan mark, so a later scan examines it
    // again), or everything (before a full rescan). UI thread, not while a scan runs.
    void forgetFile (const juce::String& file);
    void forgetAll();

    // Favourite plugins (identifier strings), listed first by the plugin menus and the quick-add palette.
    // Saved in %APPDATA%\LYNX\plugin-favourites.txt. UI thread.
    const std::set<std::string>& favourites() const noexcept { return favouriteIds; }
    void setFavourite (const std::string& id, bool favourite);

    // Names the user gave plugins (FL's renamed Plugin database entries, issue #11; core/Preferences.h): the
    // Browser's Plugin database shows them and the slot / channel menus list them first, picked by their first
    // character. Saved in %APPDATA%\LYNX\plugin-names.txt. UI thread.
    const std::map<std::string, std::string>& pluginNames() const noexcept { return userNames; }
    void setPluginName (const std::string& key, const std::string& name); // "" = the original name again

    // The user's Plugin database folders (round 166f, issue #16; core/PluginCatalog.h): the slot / channel menus
    // list the plugins of each folder under its heading. Saved in %APPDATA%\LYNX\plugin-folders.txt. UI thread.
    // Since round 190ba (issue #104) the folders, the places and the names live in the Plugin database folder on disk
    // (core/PluginDbFolder.h, Documents/LYNX/Plugin database): a change made here (setDatabaseFolders, setPluginName)
    // is written to that folder by `onDatabaseEdited` (set by the UI, which knows the plugins), and what the folder
    // says is taken in with adoptDatabase. The settings keep a copy (plugin-database.txt, plugin-names.txt); until
    // 0.1.25 the folders were saved in plugin-folders.txt, the file of the scan folders, and each wrote over the other.
    const plugincatalog::Folders& databaseFolders() const noexcept { return userFolders; }
    void setDatabaseFolders (const plugincatalog::Folders& folders);
    void adoptDatabase (const plugincatalog::Folders& folders, const std::map<std::string, std::string>& names);
    std::function<void()> onDatabaseEdited;
    bool isPersistent() const noexcept { return persistent; }

    // How many scan child processes this host has started (--lv2scantest).
    int scanChildrenStarted() const noexcept { return scanChildren; }

    // Scan child mode: describes a file's plugins into `output` (XML). Returns the number found.
    static int scanOneFile (const juce::String& formatName, const juce::String& file, const juce::File& output);

    // (the parent waits core/PluginScanPolicy.h's time for each file; the child first reports a VST3's class count)

    // Adds a specific file (e.g. a .vst3 outside the default folders; an .lv2 bundle adds the plugins its manifest lists).
    int addPluginFile (const juce::File& file);

    // Lets the LV2 format find plugins by URI in every LV2 folder LYNX scans (a host that is not `keep` skips this at
    // construction: the sandbox child calls it before opening an LV2 plugin; round 161).
    void findLv2Plugins();

    std::unique_ptr<juce::PluginDescription> find (const juce::String& id) const;

    // FL's "Combine 32 and 64 bit versions of a plugin" (round 176a): what the 32-bit and the 64-bit build of one plugin
    // share (format, ID, name), and the other build of `description` when both are installed (null: there is none).
    static std::string bitnessKey (const juce::PluginDescription& description);
    std::unique_ptr<juce::PluginDescription> otherBuild (const juce::PluginDescription& description) const;
    // The build `ref` loads: its "Bit size" (Native = the 64-bit build when there is one, else the one there is).
    std::unique_ptr<juce::PluginDescription> buildFor (const PluginRef& ref) const;

    // Online Sessions (round 168a). The plugin of `ref` is installed here (scanned, its file still there); when it is
    // not, a part that has a stand-in render plays that instead (core/StandIns.h).
    bool isInstalled (const PluginRef& ref);
    // A plugin as another computer can find it (JUCE's identifier string holds a hash of the file's path, which differs
    // from PC to PC): format, unique ids, name, vendor, version, role and file name.
    struct Identity
    {
        juce::String ref, name, format, manufacturer, version, file;
        int uniqueId = 0, deprecatedUid = 0;
        bool instrument = false;
    };
    std::optional<Identity> portableIdentity (const PluginRef& ref) const;

    // Online Sessions P5-A1 (round 190ai, core/RemotePlugins.h). In a live session this computer is participant
    // `tag` (0: no session): a plugin reference played by another participant is never loaded here, even when the
    // same plugin is installed — it counts as not installed and its stand-ins play.
    void setSessionTag (std::uint32_t tag) noexcept { sessionTag.store (tag, std::memory_order_relaxed); }
    bool playedElsewhere (const PluginRef& ref) const noexcept
    {
        const auto me = sessionTag.load (std::memory_order_relaxed);
        return me != 0 && ref.playedBy != 0 && ref.playedBy != me;
    }
    // What a host tells its guests about a plugin it shares: the name, the vendor and the parameter list of a fresh
    // instance (created and released here; message thread). nullopt: not installed or it did not load (`error`).
    std::optional<remoteplug::Shared> describeForSharing (const std::string& id, juce::String* error = nullptr);
    // A hosted plugin's parameter values (PluginRef::values) put on a running instance (indices the plugin does not
    // have are ignored), and read back: every automatable parameter that is not at its default.
    static void applyValues (juce::AudioPluginInstance& plugin, const std::vector<std::pair<int, float>>& values);
    // (`fresh`: the plugin as it was described for sharing — a value counts when it differs from a fresh instance's)
    static std::vector<std::pair<int, float>> readValues (juce::AudioPluginInstance& plugin, const remoteplug::Shared* fresh = nullptr);
    // The installed plugin of an identity: the same format and unique id, else the same name and vendor in the same
    // role (another format: its state may not carry over).
    std::optional<juce::PluginDescription> findByIdentity (const Identity& identity, bool* sameFormat = nullptr);

    // Third-party plugins from an imported .flp (ForeignPlugin): finds the same plugin among the
    // scanned ones (VST3 by class ID, CLAP by id; then the file FL Studio's plugin database lists for it; then
    // VST2 by its ID; by name as a fallback) and carries the state over (VST3: JUCE's format; VST2: FL's layout as is).
    // Matched channels become plugin channels; unmatched effect slots are dropped. Returns how many were matched.
    // `matched` gets one "name → file (how)" line per plugin found.
    // `withoutState` gets the plugins found whose saved settings couldn't be carried over (VST2 <-> VST3 swap).
    // Effect slots whose plugin is missing become their LYNX substitute (core/PluginSubstitutes.h) when
    // pluginSubstitutesEnabled(); `substituted` gets one "name -> LYNX effect" line each (they are not in `missing`).
    int adoptFlPlugins (Song& song, juce::StringArray* missing = nullptr, juce::StringArray* matched = nullptr,
                        juce::StringArray* withoutState = nullptr, juce::StringArray* substituted = nullptr);
    // The state in JUCE's format that `plugin` (FL's wrapper record of a project or an .fst preset) gives the plugin
    // `d` (VST3: component + controller; VST2: FL's layout; CLAP: the plugin's bytes); empty when it can't be carried
    // over (VST2 <-> VST3). Base64, as PluginRef::state.
    static std::string stateFromFlWrapper (const ForeignPlugin& plugin, const juce::PluginDescription& d);

    // Export to FL Studio (round 171e): a plugin of this PC as FL's Fruity Wrapper keeps it — the format, its identifier
    // (VST2 id, VST3 class TUID from FL's plugin database or the module's factory, CLAP id), name, path and the state
    // in FL's layout (VST3: component + controller records; VST2: FL's layout, which LYNX's VST2 host already keeps;
    // CLAP: u32 0 + the plugin's bytes). nullopt = not in the plugin list, or its identifier can't be read.
    std::optional<ForeignPlugin> flWrapperRecord (const PluginRef& ref, bool instrument);

    // A .lynx song just loaded: substitute slots whose original plugin is now installed go back to it (always: the
    // original is preferred); plugin effect slots whose plugin isn't installed get their LYNX substitute (when
    // pluginSubstitutesEnabled()). Returns how many slots changed; `restored` / `substituted` count each kind.
    int applySubstitutes (Song& song, int* restored = nullptr, int* substituted = nullptr);

    // What a substitute is matched on for a saved reference: its .flp record when it has one, else the format, name
    // and (decoded) state of the LYNX reference.
    static PluginIdentity identityOf (const PluginRef& ref);
    std::optional<juce::PluginDescription> findForeign (const ForeignPlugin& plugin, bool instrument, juce::String* how = nullptr);

    // Plugin files worth a targeted scan so a project's plugins can be found: for each plugin not known yet, the
    // file FL's database or the project names, and same-named VST3/CLAP files in pluginFolders(). Cheap (no loading).
    juce::StringArray filesToScanFor (const std::vector<FlpPluginUse>& uses);

    // Plugins of the song that are known but failed to load ("name: reason"; they play silence / are bypassed).
    // Kept until clearLoadFailures (a new project). Any thread.
    juce::StringArray getLoadFailures() const;
    void clearLoadFailures();

    std::shared_ptr<Instrument> makeInstrument (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);
    std::shared_ptr<Effect> makeEffect (const PluginRef& ref, double sampleRate, juce::String* error = nullptr);

    static constexpr int maxBlockSize = 4096;

    // Opening a project (round 112): its hosted plugins are created one at a time before the rack is built, so a
    // progress window can say which one is loading; makeInstrument/makeEffect then take the ready instance (same id,
    // state and rate) instead of creating another. A failure is kept too, so a broken plugin is tried once. The rate
    // is the one the rack will be built at (also used by the VST3 automation remap, which reuses the same instance).
    // Message thread.
    void setPreloadRate (double sampleRate) noexcept { preloadRate = sampleRate; }
    // `wanted`: how many ready instances of this exact plugin+state the rack needs so far (one the VST3 automation
    // remap already created counts, so it is not created twice)
    void preload (const PluginRef& ref, int wanted = 1);
    void clearPreloaded();
    int preloadedCount() const;
    // Called on the message thread just before a hosted plugin is created (the progress panel names it and repaints).
    std::function<void (const juce::String& name)> onInstantiate;

    // Slow modules started ahead, on a thread of their own (round 121, core/ModuleWarmup.h): a Waves shell's first
    // factory call waits ~10 s for the vendor's service. Call as soon as an open knows its plugins (any thread);
    // the panel then waits for `modulesStarting()` without blocking. Not with the plugin sandbox (the child loads it).
    void startModulesFor (const std::vector<FlpPluginUse>& uses);
    void startModulesFor (const std::vector<PluginRef>& refs);
    static modulewarm::Waiting modulesStarting();
    // "Continue without it": the modules still starting are left out of this open (their slots get substitutes)
    static void skipStartingModules();
    static void newOpen(); // a new project open: forgets the skips
    // (the modules started ahead stay loaded for the session: a process that loaded plugins ends without their
    // teardown, round 111)

    // Targeted scans on open (round 112): a project plugin that one targeted scan could not find is not searched
    // again on every open (it walked the plugin folders and rescanned files each time); a full scan, or a week,
    // forgets this. Files a scan described with no plugin at all are skipped until they change on disk.
    bool recentlyMissed (const FlpPluginUse& use) const;
    void rememberMisses (const std::vector<FlpPluginUse>& uses);
    void forgetMisses();

private:
    std::unique_ptr<juce::AudioPluginInstance> instantiate (const PluginRef& ref, double sampleRate, juce::String* error);
    std::unique_ptr<juce::AudioPluginInstance> instantiateOnce (const PluginRef& ref, double sampleRate, juce::String* error); // (preloaded or new)
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const PluginRef& ref, double sampleRate, juce::String* error);
    static std::string preloadKey (const PluginRef& ref, double sampleRate);
    struct Preloaded
    {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::String error;
    };
    std::map<std::string, std::vector<Preloaded>> preloaded;
    mutable std::mutex preloadLock;
    double preloadRate = 44100.0;
    bool emptyScanIsCurrent (const juce::String& file);
    // a plugin described from this file: the same path, or a module inside this bundle folder (round 165i)
    static bool sameModule (const juce::PluginDescription& d, const juce::String& file);
    bool fileStillThere (const juce::String& file); // cached until clearPreloaded (one check per file per open)
    std::map<juce::String, bool> existsCache;
    std::mutex existsLock;
    void rememberEmptyScan (const juce::String& file);
    bool describeLv2 (const juce::FileSearchPath& paths, const std::function<bool (float, const juce::String&)>& progress);
    void save();
    bool scanOne (juce::AudioPluginFormat& format, const juce::String& file, const std::function<bool()>& keepGoing = {});

    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;
    std::atomic<std::uint32_t> sessionTag { 0 };
    bool persistent = true;

    std::mutex scanLock, saveLock, flDbLock;
    std::atomic<bool> stopScan { false };
    std::atomic<int> scanChildren { 0 };
    FlPluginDb flDb;
    int flDbFiles = 0;
    bool flDbLoaded = false;

    std::set<std::string> favouriteIds;
    std::map<std::string, std::string> userNames;
    plugincatalog::Folders userFolders;

    class BackgroundScan;
    std::unique_ptr<BackgroundScan> background;
    // reads FL's plugin database a few seconds after start, off the message thread: it took 2 s (hundreds of small
    // .fst / .nfo files) the first time Settings opened, and froze the window (issue #13 investigation)
    std::atomic<bool> warmupQuit { false };
    std::thread flDbWarmup;
    mutable std::mutex failuresLock;
    juce::StringArray loadFailures;
    mutable std::mutex stateLock;
    ScanState state;
};

// "Open without third-party plugins" (round 190ap, core/PluginHold.h): this session's switched-off plugins. While it is
// on, no hosted plugin that is off is created by any PluginHost of this process (the project's engine, an export's
// engine, a thumbnail's): createInstance answers "switched off" before it touches the plugin's file, nothing is
// preloaded or started ahead, and no substitute takes a slot's place. Any thread.
bool pluginsHeld();                              // the mode is on
bool pluginHeld (const std::string& id);         // this plugin is off now
pluginhold::State pluginHoldState();             // a copy, for the lists
void holdPlugins();                              // all off
void releasePlugin (const std::string& id);      // this one back on (the caller rebuilds its slots)
void endPluginHold();                            // the mode off: everything loads again

// Settings → "Substitute missing plugins with LYNX equivalents" (default on; %APPDATA%\LYNX\substitutes.txt).
bool pluginSubstitutesEnabled();
void setPluginSubstitutesEnabled (bool enabled);

// createPluginInstance, checked: a VST3 shell that hands out another of its plugins than the one asked for (a Waves
// shell's first instance in a process, round 121) is created once more.
std::unique_ptr<juce::AudioPluginInstance> createRightInstance (juce::AudioPluginFormatManager& formats, const juce::PluginDescription& description,
                                                                double sampleRate, int blockSize, juce::String& error);

// Stereo layout (if the plugin accepts it), rate and block size; calls prepareToPlay.
// `sideInput`: the second input bus (a sidechain input) on; off by default (issue #109).
void preparePlugin (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize, bool sideInput = false);

// Current plugin state as base64 (for saving in the project).
std::string capturePluginState (juce::AudioPluginInstance& plugin);
} // namespace lynx
