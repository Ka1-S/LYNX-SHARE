#pragma once

#include <array>
#include <optional>
#include <deque>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <functional>

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/LinkFormula.h"
#include "core/Multilink.h"
#include "core/RemoteMap.h"
#include "core/AutomationDisplay.h"
#include "core/HeldNotes.h"
#include "core/Mpe.h"
#include "core/RecordLoop.h"
#include "core/StepEdit.h"
#include "core/Preferences.h"
#include "core/LiveEventQueue.h"
#include "core/LiveInput.h"
#include "core/HardwareOutput.h"
#include "core/LiveRecordView.h"
#include "core/Recorder.h"
#include "core/PedalHold.h"
#include "core/TakeRecording.h"
#include "core/WorkerPool.h"
#include "core/SerialJobs.h"
#include "core/PreviewTempo.h"
#include "engine/MidiSyncIo.h"
#include "engine/ControllerIo.h"
#include "core/RackRenderer.h"
#include "core/RtStats.h"
#include "core/ScoreLog.h"
#include "core/FlpImport.h"
#include "core/History.h"
#include "core/NativeEngines.h"
#include "core/PluginWarmup.h"
#include "core/SnapshotExchange.h"
#include "core/Song.h"
#include "engine/PluginHost.h"
#include "engine/RenderStream.h"
#include "engine/SampleLibrary.h"
#include "engine/StreamPrefetcher.h"
#include "engine/Transport.h"

namespace lynx
{
class AudioLogger;

// Owns the audio device, the processing graph and the document (Song).
// Sits between the driver and the AudioProcessorPlayer to advance the transport
// after each block, so every node sees the same start position.
//
// The Song is touched only by the UI thread; each edit publishes an immutable
// snapshot (RackState) to the audio thread.
// Folders of the most recent FL Studio installation (empty if none).
FlPathRoots findFlStudioRoots();
// The user's FL installation as the native FL engines read it (round 142): its folders and this PC's file system
const natives::FlAccess& flStudioAccess(); // (found once per session)

// Start-up trace hook (round 165i): Main's LYNX_TRACE_STARTUP log receives the engine's own steps (null: off)
inline void (*startupMark) (const char* step) = nullptr;

class AudioEngine final : private juce::AudioIODeviceCallback,
                          private juce::MidiInputCallback,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    // workerThreads: extra audio threads (< 0 = by the CPU's cores). An engine that only serves an editor needs none.
    // userPluginList = false: the plugin host neither reads nor writes LYNX's plugin list and settings, nor reads FL's
    // plugin database in the background (round 180b: an editor inside another host must not, and its close must not
    // wait for that reader)
    explicit AudioEngine (bool openAudioDevice = true, int workerThreads = -1, bool userPluginList = true);
    // Round 180b (LYNX's plugins in another host): this engine plays nothing, it serves an editor whose audio runs in the
    // plugin's own renderer. The editor's displays read that renderer's times and tap, and its running effect.
    void setExternalRuntime (ProcessTimes* times, std::function<std::shared_ptr<Effect>()> effect)
    {
        externalTimes = times;
        externalEffect = std::move (effect);
    }
    ~AudioEngine() override;

    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
    Transport& getTransport() noexcept { return transport; }
    const Transport& getTransport() const noexcept { return transport; }
    // The Song-mode playhead for the UI: the position folded into the song (its end goes back to the start), except
    // while the song plays on past its end (a Song-mode recording without a loop region, FL [EXEC] round 168k: the
    // counter reads 8:08 after recording a 1-bar song from bar 1; a bounce) or with a loop region
    double songPlayheadTicks() const noexcept;
    // Round 174e: how far the playhead has moved since the last audio block, for screens drawing faster than the blocks
    // come (144 Hz against 256-sample blocks, or larger buffers): the time since that block at its tempo, never more
    // than one block (so the next block's position is never passed: the playhead only moves forward). 0 when stopped.
    double playheadAdvanceTicks() const noexcept;
    SampleLibrary& getSampleLibrary() noexcept { return samples; }
    // An audio clip's own audio through its channel's instrument (core renderClipAudio), at the engine's sample rate, with
    // the samples, stretches and plugins the rack uses (round 189l, Make unique as sample). UI thread.
    ClipAudio renderClipAudio (std::size_t clipIndex);
    // Round 190ey (live check 190ew PL-3): the Playlist clip menu's Preview as FL plays it — the clip through its own
    // channel, so its mixer insert's effects and meter, from the clip's start to its end (core audioClipNote on the live
    // note queue). Transport start / stop end it, as does the next preview. False: no audio clip there. UI thread.
    bool previewClipThroughChannel (std::size_t clipIndex);
    void stopClipPreview() noexcept;
    bool isClipPreviewing() const noexcept { return clipPreviewKey >= 0; }
    MeterBank& getMeters() noexcept { return meters; }
    // Tools > Audio logger (REA round 153): records everything the Master plays to a WAV in the background.
    AudioLogger& getAudioLogger();
    PluginHost& getPluginHost() noexcept { return plugins; }
    // Online Sessions (round 168a): the channels / inserts whose third-party plugin is missing here and that play their
    // stand-in render instead (core/StandIns.h), as of the last publish — the Channel Rack and the Mixer badge them.
    bool playsStandIn (int channel) const noexcept
    {
        return std::find (standInChannels.begin(), standInChannels.end(), channel) != standInChannels.end();
    }
    bool insertPlaysStandIn (int track) const noexcept
    {
        return std::find (standInInserts.begin(), standInInserts.end(), track) != standInInserts.end();
    }

    // Freeze (round 190ay, core/StandIns.h lynx::freeze): a frozen channel plays its rendered audio and its instrument is
    // not loaded. frozenAudioMissing: its audio is not on disk (as of the last publish), so it plays its instrument
    // instead — never silence. freezeOutOfDate: the notes, tempo or arrangement it was rendered from changed since
    // (worked out on demand, once per edit of the song). Where the audio goes: next to the project's recordings. UI thread.
    bool frozenAudioMissing (int channel) const noexcept
    {
        return std::find (frozenMissing.begin(), frozenMissing.end(), channel) != frozenMissing.end();
    }
    bool freezeOutOfDate (int channel);
    // Frees now what the engine retired (the racks replaced by an edit, with the instruments and hosted plugin
    // instances only they still held) instead of on the next message: for the command-line modes, which run without
    // a message loop, before they measure memory or count plugin instances. Message thread.
    void freeRetiredRacks()
    {
        rackExchange.collectGarbage();
        rackDisposer.flush();
    }
    juce::File getFreezeFolder() const { return recordingFolder.getSiblingFile ("Frozen"); }

    // Third-party plugins from the last imported .flp that were not found among the scanned ones.
    const juce::StringArray& getMissingPlugins() const noexcept { return missingPlugins; }
    // "plugin → file (how it was found)" for the plugins of the last imported .flp
    const juce::StringArray& getMatchedPlugins() const noexcept { return matchedPlugins; }
    // plugins found whose saved settings could not be carried over (VST2 <-> VST3): "name (format)"
    const juce::StringArray& getPluginsWithoutState() const noexcept { return pluginsWithoutState; }
    // What the last imported .flp converted, approximated or left out.
    const FlpImportReport& getImportReport() const noexcept { return importReport; }
    // The last imported .flp's plugins that were not found, with what uses them and what LYNX put in their place.
    const std::vector<MissingPlugin>& getMissingPluginDetails() const noexcept { return missingPluginDetails; }

    // Live instances (to open the plugin window). nullptr if not a plugin.
    std::shared_ptr<Instrument> getChannelInstrument (int channel) const;
    std::shared_ptr<Effect> getEffect (int track, int slot) const;

    // Copies the plugins' current state into the Song (before saving).
    void capturePluginStates(); // (before every save: also adds the working time so far, see stampWorkTime)
    // Edison slots (round 190cd, core/EdisonFx.h): audio that changed since it was last written goes to a new WAV in
    // the recording folder and the slot points at it (not an undo step). Called after a take, when an Edison window
    // closes and before every save. UI thread.
    void flushEdisonAudio();
    // Recordings into Edison slots go on while their windows are closed (as FL's plugin does): this moves what the
    // audio thread recorded into the takes (the engine's timer calls it) and flushes a take that ended by itself.
    void pumpEdisonRecordings();
    // Round 190ao (Bitwig's "Reload plug-in"): a hosted plugin of a channel (track < 0) or a mixer slot that is down:
    // it crashed inside a call and was switched off, or its separate process died or stopped responding.
    struct HostedSlot
    {
        int channel = -1, track = -1, slot = -1;
        bool operator== (const HostedSlot& o) const noexcept { return channel == o.channel && track == o.track && slot == o.slot; }
    };
    bool hostedPluginDown (HostedSlot where) const;
    // The plugins that are down (channels first); `named`: only the ones with that PluginRef name.
    std::vector<HostedSlot> downHostedPlugins (const std::string& named = {}) const;
    // Starts those plugins again, each from its last captured state (a running plugin's is captured now; one that is
    // down keeps what the last save, backup or capture took), without reopening the project. Only ever on request:
    // a plugin that goes down again stays down until this is called again. The song and the undo history are not
    // changed (close the plugins' windows first: a window keeps the old instance alive). Returns how many run again.
    // Plugins inside a Patcher are not covered.
    int reloadHostedPlugins (const std::vector<HostedSlot>& which);
    // Round 190ao (pain point 13): a short C5 on the channel that hosts `plugin`, a moment from now (a preset was just
    // loaded into it; the preset menu's "Play a note when a preset loads"). Only while the song is stopped; a note
    // still sounding from the last call ends first. False when nothing will play (an effect, a plugin outside the
    // Channel rack, the song playing or recording). auditionState: 0 idle, 1 waiting to start, 2 sounding (tests).
    bool auditionPreset (const juce::AudioPluginInstance& plugin);
    // Round 190dg (the Simple sound picker's installed plugins): the same short note on channel `channel`, whatever it
    // plays (a plugin just loaded there is heard as it is; it can't be rendered aside the way auditionSound does). The
    // same conditions: only while the song is stopped and not recording.
    bool auditionChannelSound (int channel);
    int auditionState() const noexcept { return auditionChannel < 0 ? 0 : auditionSounding ? 2 : 1; }
    // Round 190bu (the Simple workflow's sound picker): `channel`'s instrument heard alone through the preview output,
    // without touching the song — the notes `keys` in turn (core/RackRenderer.h renderAudition). False when the
    // instrument can't be built or makes no sound on this computer. `seconds` (optional) = how long the audition is.
    bool auditionSound (const ChannelDef& channel, const std::vector<int>& keys, double* seconds = nullptr);
    // FL's Project info working time (REA round 167g): wall-clock time since the project was opened, added to the
    // saved total at each save. workSecondsNow() = the saved total + this session so far.
    void stampWorkTime();
    double workSecondsNow() const;
    void resetWorkTime();  // Project info › "Reset working time": created = now, working time 0, the clock restarts
    void markCreatedNow(); // a new project: Project info › created = now (when it has no date yet)
    // Offline render mode for the hosted plugins (setNonRealtime), around renderOffline.
    void setPluginsRenderMode (bool offline);
    // Every hosted plugin of the song that has not crashed (channels, inserts, inside Patchers); `notifyRender` is the
    // plugin's "Notify about rendering mode" option. Message thread or the render thread, while nothing else edits.
    void forEachHostedPlugin (const std::function<void (juce::AudioPluginInstance&, bool notifyRender)>& visit) const;
    // The render's plugin warm-up (renderOffline, round 155): silent discarded blocks until the hosted plugins settle,
    // then a new PDC plan if a latency changed and LYNX's own units reset. Returns blocks == 0 when the song has no
    // hosted plugin. `latency` receives the delay compensation's total afterwards.
    warmup::Result warmUpPlugins (const warmup::Settings& settings, double sampleRate, int blockSize,
                                  const std::function<bool (double)>& progress, int& latency);

    void play() noexcept;
    // --cliplooptest (round 167q): the device callback, driven by hand with a stand-in device
    juce::AudioIODeviceCallback& deviceCallback() noexcept { return *this; }

    // Performance mode (round 165m, core/Performance.h): launch / release / stop zone clips (message thread). The
    // clips start on the next block, quantized by their track's Performance settings; nothing launches while stopped.
    void performanceCommand (const perf::Command& command) noexcept { performanceLive->commands.push (command); }
    // FL sends the typing keyboard's notes as MIDI channel 1: they launch performance clips when that is the "MIDI
    // channel used for playlist live mode" and Performance mode is on (the same test as the MIDI input's)
    bool typingNotesLaunchClips() const noexcept
    {
        return performanceMidiChannel.load (std::memory_order_relaxed) == 1 && performanceMidiOn.load (std::memory_order_relaxed);
    }
    const perf::Live& performanceState() const noexcept { return *performanceLive; }
    // The clip grid (round 181a, core/ClipLauncher.h; message thread). A launch while stopped starts the transport, as the
    // first thing a launcher does; a stop while stopped does nothing. Without a grid in the project: nothing at all.
    void launcherCommand (const launcher::Command& command);
    // What plays per track, or nullptr while no project has had a grid (never with Pattern workflow).
    const launcher::Runtime* launcherState() const noexcept { return clipLauncher.get(); }

    // --- recording into the clip grid's slots (round 190bv; core/SessionOps.h, docs/analise/REA_rodada190bv.md) ---
    // A column of the grid is a Playlist track. Armed, it records what is played on its instrument's channel (a track
    // without one: on any channel), or, a track that plays through a mixer insert with an input, that input's audio
    // (the insert is armed with it). Arming is not saved. `exclusive`: the other tracks are disarmed.
    enum class GridInput : std::uint8_t { midi, audio };
    GridInput gridTrackInput (int track) const;
    void setGridTrackArmed (int track, bool armed, bool exclusive = true);
    bool isGridTrackArmed (int track) const noexcept;
    // The record button of an empty slot on an armed track: the recording starts on the next point of the global launch
    // quantisation (the transport starts if it was stopped, after the count-in of the record options), stops what the
    // track plays there, and runs until endSlotRecording, or for the fixed length. False: the slot has a clip, or the
    // track is not armed.
    bool recordSlot (int track, int scene);
    // Ends the track's recording on the next quantisation point. `thenPlay`: the new clip plays on in a loop from
    // there (the recording clip's launch button); otherwise the track stops (a stop button).
    void endSlotRecording (int track, bool thenPlay);
    // The name the next MIDI clip recorded on a track gets (round 190cy, Live's "<track> <n>"; the grid shows it on the
    // recording slot before the clip exists).
    std::string slotRecordingName (int track) const;
    // The Session Record button. Nothing recording: every armed track records into `scene` where its slot is empty,
    // and armed tracks whose MIDI clip is playing take what is played into that clip as it loops (overdub).
    // Recording or overdubbing: the recordings go on into loop playback and overdub ends.
    void toggleSessionRecord (int scene);
    bool isSessionRecording() const noexcept { return ! slotTakes.empty() || sessionOverdub; }
    bool isSessionOverdubbing() const noexcept { return sessionOverdub; }
    // A fixed recording length in bars (0 = until it is ended). Not saved.
    void setSlotRecordBars (int bars) { slotRecordBars = juce::jlimit (0, 32, bars); }
    int getSlotRecordBars() const noexcept { return slotRecordBars; }
    launcher::Runtime* launcherStateForSnapshot() noexcept { return clipLauncher.get(); } // --gridshot: a sample of what plays
    perf::Live& performanceStateForSnapshot() noexcept { return *performanceLive; } // --perfshot: a sample of what plays
    void stop() noexcept; // also cancels a count-in or a wait for the first note

    // Recording options (docs/WORKFLOW_FL.md P2: recording set). UI thread.
    struct RecordOptions
    {
        int countInBars = 0;            // 0 = off
        // FL's "Precount duration" (core/CountIn.h, issue #152): the length the count-in keeps while it is off, so
        // Ctrl+P turns a 2-bar count-in off and back on at 2 bars. A count-in that is on sets it.
        int countInLength = 1;
        bool waitForInput = false;      // playback starts at the first played note
        RecordMode mode = RecordMode::replace; // "Blend recorded notes" off, FL's factory default (round 177c)
        std::int64_t quantise = 0;      // record quantise in ticks (<= 1: as played); follows the global grid
        bool placeSongClip = true;      // song mode: a take puts a clip of the pattern where recording started
        // recording filter (FL's record button right-click, REA_rodada88): what a take records
        bool recordNotes = true;        // notes played (MIDI and PC keyboard)
        bool recordAudio = true;        // audio of the armed inserts' inputs
        bool askWhatToRecord = true;    // the record button asks first ("What would you like to record?")
        // FL's record menu "Options" (REA round 134): arming recording also starts playback
        bool startsPlayback = false;
        // LYNX's own "Disarm the inserts when recording stops" (record menu and Mixer › Disk recording; issue #16): the
        // armed inserts are disarmed when a take stops. FL has no such option (round 166e measured: neither its
        // "Disarm on stop" nor the mixer's "Auto-unarm" disarms the inserts after a live take).
        bool disarmOnStop = false;
        // LYNX's "Low latency while recording" (round 190aq, core/LowLatency.h; FL has no such switch): while an insert
        // with an audio input is armed or monitored "On", the latency-adding effects on its path to the Master are
        // bypassed and the PDC plan is made without them; they return when it is disarmed. What is recorded and the
        // saved project never change. Off by default.
        bool lowLatencyRecording = false;
        // FL's record menu "Disarm on stop" (RecDisableOnStopMenu, round 166e measured): the record switch turns off when
        // playback stops after recording
        bool recordOffOnStop = false;
        // FL's Mixer › Disk recording › "Auto-unarm" (round 166e measured): "Render armed tracks to wave file(s)"
        // disarms the tracks it rendered
        bool unarmAfterRender = false;
        // FL's Mixer › Disk recording › "Auto-create audio clip" (on by default, round 167a [SHOT s28–s37]): the files
        // "Render armed tracks to wave file(s)" writes go on the Playlist (mixerops::placeRenderedFiles)
        bool autoCreateClip = true;
        // FL's Mixer › Disk recording › "Latency compensation" (round 190dx; checked by default in FL's form): off, takes
        // are placed with no latency compensation at all (takes::compensation)
        bool latencyCompensation = true;
        // FL's record menu "Quantize" section (REA round 139); `quantise` is the grid. quantiseStarts = "Note start
        // time", noteEnd = "Note end time" (quantize) / "Leave note duration" (keepLength) / neither (keepEnd),
        // toStepSequencer = "Record to step sequencer"
        bool quantiseStarts = true;
        NoteEnd noteEnd = NoteEnd::keepLength;
        bool toStepSequencer = false;
        // FL's "Enable recording markers": the Playlist's "Start recording" / "Stop recording" markers limit what a
        // take keeps in song mode (notes started outside are dropped, audio clips are cut to the ranges)
        bool recordingMarkers = false;
        // FL's recording filter "Automation" (REA round 143): moved parameters become automation (song mode: automation
        // clips; pattern mode: the pattern's events); Quantize › "Automations" puts their points on the snap
        bool recordAutomation = true;
        // FL's recording filter "Clips" (REA round 146 B5, round 165m): Performance mode's launched clips are written
        // into the arrangement where they played
        bool recordClips = true;
        bool quantiseAutomation = false;
        // round 163 (Pro Tools / Nuendo): punch in/out on the Playlist's loop region (instead of loop recording; the
        // song plays straight through it), pre-roll before the punch-in (or before the position recording starts at)
        // and post-roll after the punch-out (then playback stops). Bars; 0 = off.
        bool punchLoopRegion = false;
        int preRollBars = 0;
        int postRollBars = 0;
        // round 174d (ADR): pre / post-roll in seconds instead of bars while ≥ 0 (the ADR window sets them for its
        // record cycle; not saved)
        double preRollSeconds = -1.0;
        double postRollSeconds = -1.0;
        // FL's "Loop record" (toolbar LoopRecordBtn / Options, round 170e3 [DECOMP 0xf1e2a0 toggles a global that
        // FUN_011d4800 reads: off while recording, the play end becomes "never", so playback runs on]): on, Song mode
        // recording loops the loop region (one take per pass); off, it plays straight through the region and the notes
        // and audio stay where they were played. Pattern mode always loops the pattern in LYNX.
        bool loopRecord = true;
        // LYNX's own option (off = FL's result, REA round 177c): after loop recording only the last pass plays, on top
        // of the other passes (muted, newest first, folded). Round 163's behaviour.
        bool lastLoopPassPlays = false;
        // round 170d (the Busy Works video: the chord on the downbeat after the count-in was lost; Cubase's "MIDI Record
        // Catch Range", Logic keeps count-in notes): notes played during the count-in are recorded, and a note-on at most
        // this many ticks before the take's start goes onto the start (0 = off: they keep their real time)
        std::int64_t catchWindow = ppq / 4;
        // round 170d (Pro Tools / Cubase / REAPER): played notes are recorded where they were heard, moved back by the
        // output buffer + the driver's output latency
        bool compensateOutput = true;
    };
    // The range this recording keeps (pre-roll or punch), while one is active. UI thread.
    std::optional<PunchRange> getActivePunch() const noexcept { return activePunch; }
    const RecordOptions& getRecordOptions() const noexcept { return recordOptions; }
    void setRecordOptions (const RecordOptions& options);

    // Play pressed: with recording on, honours wait-for-input and the count-in; otherwise play().
    void startPlayback();
    // The song's initial values (FL's initialized controls) back on their targets, in song mode; play() and a
    // seek while playing do it (REA round 146). UI thread.
    void resetToInitialValues() noexcept;
    bool isWaitingForInput() const noexcept { return waitingForInput.load(); }
    bool isCountingIn() const noexcept { return transport.countIn.load() > 0.0; }
    // The audio clock the last callback left (core/Recorder.h LiveClock) and where an event played at `eventMs`
    // (juce::Time::getMillisecondCounterHiRes) is recorded: sample-accurate against the block, minus the output
    // latency when compensateOutput is on (round 170d). Any thread.
    LiveClock liveClock() const noexcept;
    double recordTickFor (double eventMs) const noexcept;
    // The count-in catch of the take being counted in / recorded (round 170d; UI thread).
    const CountInCatch& getCountInCatch() const noexcept { return takeCatch; }
    // Record › Capture MIDI (round 170d, LYNX's own): the last phrase played without recording into the current pattern
    // (core/Recorder.h captureMidi), one undo step. UI thread.
    CaptureResult captureMidi();
    // Round 190ef / 190em (the Clip-launch layout with the clip grid shown, as Live 12.4.16 captures into its Session
    // view, live check 190ei): the same phrase on `track` (the caller passes the armed track), one undo step.
    //   - `track` plays a MIDI clip while the transport runs: the notes go INTO that clip; its loop content is unrolled up
    //     to the bars just played and its start / loop move there (session::captureIntoClip). `into` = true.
    //   - else a new, unnamed clip in the slot session::captureSlot picks from `scene` (the selected scene), launched at
    //     once (a stopped transport starts). Played while stopped, the song tempo is set from the phrase first
    //     (session::phraseTempo).
    // `placed`: the row written (−1: nothing captured). The notes play on the track's instrument channel.
    CaptureResult captureMidiToSlot (int track, int scene, int& placed, bool& into);

    // Per-insert record arm: each armed insert records its own audio input (Mixer › insert › input source) into
    // its own take (arming one turns audio recording on; disarming the last one turns it off). Not saved.
    void setInsertArmed (int track, bool armed);
    bool isInsertArmed (int track) const noexcept;
    const std::vector<int>& getArmedInserts() const noexcept { return armedInserts; }
    void rewind() noexcept;
    void seek (double ticks) noexcept;

    // Preview of an audio file (browser). An invalid/empty file stops the preview.
    void previewFile (const juce::File& file);
    // The Browser's "Preview in time" (LYNX extra, round 190aq, core/PreviewTempo.h; off by default): a previewed loop
    // plays stretched to the project's tempo and, while the transport plays, starts on the next beat. A file with no
    // usable tempo plays as it always did. The stretch is made on a worker thread: previewFile returns at once and the
    // sound starts when it is ready. UI thread.
    void setPreviewInTime (bool on) noexcept { previewInTime = on; }
    bool isPreviewInTime() const noexcept { return previewInTime; }
    struct PreviewTiming
    {
        juce::String path;          // the previewed file ("" = none since the last stop)
        bool pending = false;       // its tempo is being read or its stretch made: nothing sounds yet
        bool inTime = false;        // it has a tempo: it plays stretched and / or on the beat
        double fileBpm = 0.0, projectBpm = 0.0, ratio = 1.0; // ratio: played length / file length (1 = not stretched)
        previewtempo::Source source = previewtempo::Source::none;
        double prepareMs = 0.0;     // from previewFile to the sample handed to the player (tempo + stretch; 0 = cached)
        std::size_t frames = 0;     // the played sample's length
    };
    const PreviewTiming& getPreviewTiming() const noexcept { return previewTiming; }
    // An in-time preview's progress 0..1 (0 while it is prepared or waits for its beat), −1 when none plays.
    double getPreviewInTimeProgress() const noexcept;
    // Waits for the preview being prepared and starts it (tests and tools: no message loop needed). UI thread.
    void finishPreviewJobs();
    // How long the last prepared preview took (tempo detection + stretch), in ms (diagnostics).
    double getLastPreviewPrepareMs() const noexcept { return lastPreviewPrepareMs; }
    // How many previews started on a beat (diagnostics, tests).
    int getPreviewBeatStarts() const noexcept;
    // The Browser's preview volume (0..1.25, 1 = default; core/ValueMath.h previewGain). Applies to the preview
    // playing now too. UI thread.
    void setPreviewVolume (double volume) noexcept;
    double getPreviewVolume() const noexcept { return previewVolume; }
    // Plays a sound made in memory through the same preview output (the start-up sound). UI thread.
    void previewSample (std::shared_ptr<const SampleData> sample, bool loop = false, float gain = 0.8f, bool audioEditor = false);
    // The Audio Editor's playback volume (its toolbar knob, round 162): 0..1.25 like the Browser's, 1 = unity.
    void setEditorVolume (double volume) noexcept;
    double getEditorVolume() const noexcept { return editorVolume; }
    // The frame of the previewed sample being played, -1 when the preview is silent.
    double getPreviewPosition() const noexcept;

    // Note played live (PC keyboard, preview). UI thread.
    // `record` = goes into the recording if recording is on (Piano Roll preview doesn't).
    void sendLiveNote (int channel, int key, float velocity, bool on, bool record = false);
    // The keys held right now on MIDI inputs and the typing piano (as played, before the scale snap), ascending; for
    // the Piano Roll's chord readout (any thread).
    std::vector<int> heldInputKeys() const;
    // Round 190ee: the key played last on a MIDI input or the typing piano (C5 = 60 before any): Edison's "Last hit"
    int lastPlayedKey() const noexcept { return lastHitKey.load (std::memory_order_relaxed); }
    // The same for one MIDI channel (0-15; the typing keyboard plays on 0), as two 64-bit masks: the chord panel (round
    // 181b) counts each channel's keys on its own, as FL does.
    std::array<std::uint64_t, 2> heldInputKeyBits (int midiChannel) const noexcept;
    // The Audio logger's memory (Tools › Audio logger, round 181b). The audio thread writes it from the Master.
    RollingAudioLog& getRollingLog() noexcept;
    // The keys sounding on a channel right now (song / pattern notes, keyboard, MIDI): the Piano roll's keyboard lights
    // them (issue #14). Any thread.
    NoteMask soundingKeys (int channel) const noexcept;

    // FL's score logger (REA round 165f): every note played on a MIDI input or the typing keyboard, recording or
    // not, for Tools › Dump score log to selected pattern. Any thread (a short spin lock; never the audio thread).
    std::vector<scorelog::Entry> scoreLogSnapshot() const;
    bool scoreLogEmpty() const;
    void clearScoreLog();
    // FL's Step edit (round 170e3, core/StepEdit.h): while on, every note played on a MIDI input or the typing keyboard
    // is also queued for the UI, which writes it into the pattern when the transport is stopped. Any thread.
    void setStepEdit (bool on) noexcept;
    bool isStepEdit() const noexcept { return stepEditOn.load (std::memory_order_relaxed); }
    std::vector<stepedit::Played> takeStepEditNotes();

    // Scale snap for played notes (MIDI input and the PC keyboard): scale index into notetools::scales()
    // (0 = off) and its root (0..11). Any thread.
    void setInputScale (int scale, int root) noexcept;
    int snapInputKey (int key) const noexcept;

    // All notes off on every channel (double-click on Stop). UI thread.
    void panic();
    // FL's Tools › Macros › Stop sound (Ctrl+H, round 170h2; FL64 MacroStopSoundActionExecute → FUN_011c8d10 →
    // FUN_011c8a40 (1, 1, 1) → FUN_012b5210): all notes off on every channel and MIDI out, and every effect and
    // plugin reset — the tails of delays and reverbs stop too. UI thread.
    void stopSound();

    // Note recording (MIDI and PC keyboard) into the current pattern while playing. UI thread.
    void setRecording (bool shouldRecord);
    // FL's Tools › Macros › Cancel recording (MacroCancelRecordingAction, REA round 169k §2.3 [DECOMP 0xf1d7e0]): what this
    // recording pass wrote is thrown away (its notes, automation and audio takes; the take files are deleted), recording
    // turns off, the armed inserts stay armed and playback goes on. false: not recording. UI thread.
    bool cancelRecording();
    bool isRecording() const noexcept { return transport.recording.load(); }
    // Pattern mode, a take with Loop record off (round 170e5): the transport tick the pattern plays once from, < 0: none
    double patternLinearOrigin() const noexcept { return liveInput.patternOrigin.load (std::memory_order_relaxed); }

    // Round 177d: the recording in progress as the editors draw it live (core/LiveRecordView.h). UI thread.
    struct LiveRecording
    {
        // the note take: the notes still held (positions in `pattern`, as they would be committed now) and, in Song
        // mode, the Playlist range the take covers so far: [from, to) on `track` (from = its clip's start or the origin)
        bool notes = false;
        int pattern = -1;
        std::vector<Note> held;
        int track = -1;
        std::int64_t from = -1, to = -1;
        std::int64_t origin = -1; // the song position of the pattern's start for `held`
        // the audio takes: each on the Playlist track it will land on, its current pass and its waveform
        struct Audio
        {
            int track = -1;
            liverec::AudioSpan span;
            std::shared_ptr<const liverec::LivePeaks> peaks;
            juce::String name;
        };
        std::vector<Audio> audio;
        std::optional<PunchRange> punch; // only this range is kept (pre-roll / punch in-out)
        bool any() const noexcept { return notes || ! audio.empty(); }
    };
    // cheap: a take records (notes held or recorded, or audio)
    bool isLiveRecording() const noexcept { return recorder.isActive() || takeCount > 0; }
    bool isRecordingAudioTakes() const noexcept { return takeCount > 0; } // audio takes are open (issue #152)
    LiveRecording liveRecording() const;

    // Audio recording from the interface input: with this on, recording (Rec + Play) writes the
    // input to a WAV and, on stop, the take becomes a Playlist clip (latency-compensated).
    // Enabling it opens the device input (only then does Windows show the mic as in use).
    void setAudioRecordingArmed (bool armed);
    bool isAudioRecordingArmed() const noexcept { return audioArmed; }
    juce::String getLastRecordingError() const { return recordingError; }
    // Where takes are written and how they are named (FL's project data folder, REA round 134): `folder` is
    // "<project folder>/Audio/Recorded" for a saved project (the global recordings folder otherwise), files are
    // "<projectStem>_<yyyy-mm-dd hh-mm-ss>_<insert name>.wav". UI thread.
    void setRecordingTarget (const juce::File& folder, const juce::String& projectStem);
    juce::File getRecordingFolder() const { return recordingFolder; }
    juce::String getRecordingStem() const { return recordingStem; }
    // A MIDI message as if a controller had sent it (the --bendrectest of round 167a). Message thread.
    void injectMidiInput (const juce::MidiMessage& message) { handleIncomingMidiMessage (nullptr, message); }
    static juce::File defaultRecordingFolder();
    // The take files written since LYNX started (or since forgetRecordings), oldest first. UI thread.
    const std::vector<juce::File>& getSessionRecordings() const noexcept { return sessionRecordings; }
    void forgetRecordings (const std::vector<juce::File>& files);

    // --- hardware recording (round 163, docs/analise/REA_rodada163c.md) ---
    // Options › Audio › Recording, saved in <settings>/recording.txt.
    struct RecordingSettings
    {
        takes::SampleFormat format = takes::SampleFormat::pcm24; // 24-bit or 32-bit float WAV; the device's rate
        bool trackTakeNames = true;  // "<Insert>_Take<N>.wav" (off: FL's "<project>_<date time>_<insert>.wav")
        int manualOffset = 0;        // samples added to the compensation (negative: earlier)
        bool useMeasured = true;     // a ping measured for this device setup replaces the driver's figures
        std::map<std::string, int> measured; // setupKey -> round trip (samples)
        int pingOutput = 0, pingInput = 0;   // device channels the ping uses (output pair's first, input)
    };
    const RecordingSettings& getRecordingSettings() const noexcept { return recordingSettings; }
    void setRecordingSettings (const RecordingSettings& settings); // saves
    // The current device setup's key (takes::setupKey) and its latency figures (pdc = the Master's PDC now).
    std::string currentSetupKey() const;
    takes::Latency currentLatency() const;
    // The latency ping: clicks on output channel pair `pingOutput` (and the next), listened for on input channel
    // `pingInput` through a loopback. Needs the transport stopped and the input open. Returns an error (empty: started).
    juce::String startLatencyPing();
    bool isPinging() const noexcept { return ping.isRunning(); }
    void cancelLatencyPing() noexcept { ping.cancel(); }
    // After isPinging() turned false: the result (stored for this setup when ok).
    takes::LatencyPing::Result finishLatencyPing();
    // Record safe (round 163): a safe insert can't be armed (undoable, saved with the project).
    void setInsertRecordSafe (int track, bool safe);
    bool isInsertRecordSafe (int track) const noexcept
    {
        return track > 0 && track < (int) song.mixer.size() && song.mixer[(std::size_t) track].recordSafe;
    }
    // Frames lost while recording (the disk could not keep up; they became silence, the timing kept) since the
    // last take started.
    juce::int64 getLostRecordingFrames() const noexcept { return lostFrames; }
    // Interrupted takes (crash while recording): repaired WAVs and where they belonged. Scans the journals in the
    // given folders (the default recordings folder always). UI thread, at start-up.
    std::vector<takes::Journal> recoverInterruptedTakes (const std::vector<juce::File>& folders);
    // Puts recovered takes on the Playlist at their recorded positions (undoable). Returns how many were placed.
    int placeRecoveredTakes (const std::vector<takes::Journal>& recovered);
    // Leaves them as files only (their journals are removed).
    void forgetInterruptedTakes (const std::vector<takes::Journal>& recovered);

    // --- external audio input through the mixer (REA_rodada88) ---
    // Names of the open device's input channels (all of them, open or not); empty without an input device.
    std::vector<std::string> getInputChannelNames() const;
    // Problems the UI explains to the user (translated there).
    enum class InputIssue
    {
        none,
        noDevice,          // no audio device open at all
        noInputDevice,     // the driver has no input device
        noInputOpen,       // recording with no input open
        armedWithoutInput, // armed inserts, none with an input source
        insertWithoutInput,// one armed insert (track) has no input source
        lowDisk,           // little room left on the recordings drive (detail = minutes)
        recordSafe,        // the insert (track) is record safe: not armed
        framesLost,        // the disk could not keep up: frames became silence (detail = how many)
        failed             // the driver refused (detail = its message) or a file could not be written
    };
    struct RecordingNotice
    {
        InputIssue issue = InputIssue::none;
        int track = 0;
        juce::String detail;
    };
    // Opens the input of the current driver: `deviceName` (separate-input drivers; empty = the one chosen in
    // Settings, else the default), all of its channels. UI thread.
    RecordingNotice openAudioInput (const juce::String& deviceName = {});
    bool hasAudioInput() const;
    // Closes the device input again when nothing needs it (no insert takes an input, no armed recording, no hardware
    // insert returning from it): a capture the Audio editor opened for one recording no longer keeps the input — and
    // Windows' microphone-in-use icon — on until LYNX quits, as in FL (round 165q). The input device's name is kept
    // for the next openAudioInput(). UI thread.
    void releaseAudioInputIfUnused();
    bool audioInputNeeded() const;
    // Sets an insert's input source (undoable; -1 = none) and opens the device input when needed.
    void setInsertInput (int track, int channel, bool stereo);
    void setInsertMonitor (int track, InputMonitor mode);
    // FL's "Record" position of an insert and LYNX's "also record the dry input" (round 168x, issue #41); undoable
    void setInsertRecordSource (int track, RecordSource source);
    void setInsertRecordDryToo (int track, bool on);

    // --- hardware output targets (REA round 156) ---
    // Names of the open device's output channels (all of them, open or not); empty without a device.
    std::vector<std::string> getOutputChannelNames() const;
    // Whether device output channel `c` is open (gets sound) right now.
    bool isOutputChannelOpen (int channel) const;
    // Sets a track's hardware output target (undoable; outputNone / outputDefault / a device channel) and opens
    // the device's output channels it needs (no trip to Audio settings). The Master's default is its first pair.
    void setTrackOutput (int track, int channel, OutputMode mode);
    // Opens the device output channels the song's targets need (that the device has). UI thread.
    void openTargetedOutputs();
    // The hardware inserts' channels (round 164d): opens their outputs and, when one returns from an input, the device
    // input. UI thread.
    void openHardwareInsertIo();
    // Every hardware insert of the song (round 165c): a mixer slot's, or a node inside a Patcher (mixer-slot or generator
    // Patcher; `nodes` = the node index at each Patcher level). UI thread.
    struct HardwareInsertRef
    {
        bool channel = false; // a Channel Rack generator Patcher (index = channel), else a mixer slot (index = track)
        int index = 0, slot = 0;
        std::vector<int> nodes;
        bool operator== (const HardwareInsertRef&) const = default;
    };
    std::vector<HardwareInsertRef> getHardwareInserts() const;
    static EffectSlot* locateHardwareInsert (Song& song, const HardwareInsertRef& ref);
    std::shared_ptr<Effect> runningHardwareInsert (const HardwareInsertRef& ref) const;
    // The routed, active inserts whose round trip was measured for another device setup (device, rate or buffer size)
    std::vector<HardwareInsertRef> staleHardwareInserts() const;
    // Presses their Ping buttons (the clicks go out on their sends; the results are applied as they come back)
    void pingHardwareInserts (const std::vector<HardwareInsertRef>& refs);
    // After the device setup changed with stale inserts in the song: those inserts, once (the UI offers "Ping now")
    std::vector<HardwareInsertRef> takeHardwareInsertNotice() { return std::exchange (hwNotice, {}); }
    // Input level of an insert's source since the last call (0..1, per side). UI thread.
    // (the Mixer's strips take it 30 times a second; other views peek at the last value taken)
    float takeInputPeak (int track, int side) noexcept
    {
        const auto v = liveInput.meters.takePeak (track, side);
        if (track >= 0 && track < maxMixerTracks && (side == 0 || side == 1))
            lastInputPeaks[(std::size_t) (track * 2 + side)] = v;
        return v;
    }
    float peekInputPeak (int track, int side) const noexcept
    {
        return track >= 0 && track < maxMixerTracks && (side == 0 || side == 1) ? lastInputPeaks[(std::size_t) (track * 2 + side)] : 0.0f;
    }
    // A message for the user about recording (e.g. an armed insert without an input), once. UI thread.
    RecordingNotice takeRecordingNotice() { return std::exchange (recordingNotice, {}); }

    // MIDI learn: the next CC received becomes a controller linked to `target`.
    void learnMidiController (const AutomationTarget& target);
    bool isLearningMidi() const noexcept { return learning.kind != AutomationTargetKind::none; }
    // Global links (FL's "Override global link...", REA round 146): links in the MIDI settings that every project
    // follows unless it links the same target or controller itself. The next CC received becomes `target`'s.
    void learnGlobalLink (const AutomationTarget& target);
    // FL's "Multilink to controllers" (Options › Switches, Ctrl+J; round 177b-4, core/Multilink.h): the controls moved
    // while it is on link, in that order, to the controllers moved next; it switches itself off when all are linked
    void setMultilink (bool on);
    bool isMultilinkOn() const noexcept { return multilink.has_value(); }
    int multilinkQueued() const noexcept { return multilink ? (int) multilink->targets.size() : 0; }
    // the controls queued so far, in the order they were moved (round 190bt: "Link to controllers...")
    std::vector<AutomationTarget> multilinkTargets() const { return multilink ? multilink->targets : std::vector<AutomationTarget>(); }
    // the switch's menu: "Link to controller" (false) / "Override global links" (true): no more controls are queued,
    // the controllers moved next link to the queued ones (as project links / as global links)
    void multilinkToControllers (bool global);
    void removeGlobalLink (const AutomationTarget& target);
    // FL's "Remote control settings" / "Global link settings" windows (round 190bt). setTargetLinks: the control's
    // project links become `links` (one undo step; core setTargetLinks). setGlobalLink: the control's global link
    // becomes `link` (none: it is removed).
    void setTargetLinks (const AutomationTarget& target, std::vector<MidiLink> links, bool removeConflicts);
    void setGlobalLink (const AutomationTarget& target, const std::optional<MidiLink>& link);
    // The controller message that arrived last (the windows' "Auto detect"); `serial` grows with every message.
    struct ControllerSeen
    {
        int port = -1, channel = 0, cc = -1, value = 0;
        std::uint64_t serial = 0;
    };
    ControllerSeen lastControllerSeen() const noexcept { return controllerSeen; }
    // A controller message as if a device had sent it (tests, --snapshot). Message thread.
    void injectControllerForTest (int cc, int channel, int value, int port = -1);
    // Round 190em: a played note in the score log (Capture MIDI's source) at a given time (--selftest).
    void logPlayedNoteForTest (double ms, int channel, int key, float velocity, bool on, double tick = -1.0);
    // Round 190ef, the reference's MIDI Map Mode / Key Map Mode (core/RemoteMap.h). In MIDI Map Mode the controllers and
    // keys received pick the control the map overlay selected instead of moving anything (no mapping acts, no key plays);
    // takeMapCapture gives the last one (once). Message thread.
    void setMapMode (remotemap::Mode mode);
    remotemap::Mode getMapMode() const noexcept { return mapMode; }
    std::optional<remotemap::MidiControl> takeMapCapture() { return std::exchange (mapCaptured, std::nullopt); }
    // A MIDI mapping of a clip slot, a scene, a track's stop button, Stop all clips or a transport switch fired (its
    // press or release); the main window acts on it. Message thread (from applyControllers).
    std::function<void (const remotemap::Fire&)> onRemoteFire;
    // "Omni" links follow the mixer's selected track: the mixer reports it here (-1 = unknown).
    std::function<int()> selectedMixerTrackForLinks;
    std::optional<MidiLink> globalLinkOf (const AutomationTarget& target) const;
    std::vector<MidiLink> getGlobalLinks() const;

    // FL's "Last tweaked" (REA round 143: FLEngine keeps the last two moved parameters, Tools › Last tweaked /
    // Next to last tweaked, Add › Automation for last tweaked parameter). Every control that can be automated reports
    // its moves here (UI thread); `held` = the touch lasts until tweakReleased (a plugin's own editor's gesture),
    // otherwise while the control keeps moving. While recording with the "Automation" filter, touched parameters are
    // written as automation.
    void noteTweaked (const AutomationTarget& target, bool held = false);
    void tweakReleased (const AutomationTarget& target);
    // 0 = the last tweaked parameter, 1 = the one before it.
    std::optional<AutomationTarget> getLastTweaked (int which = 0) const;
    // The target's current value 0..1 (a hosted plugin's parameter and MIDI controls included); < 0 when it is gone.
    float currentTargetValue (const AutomationTarget& target) const;
    // Parameter `param` of a hosted plugin (a channel's instrument or a slot's effect) as an automation target.
    std::optional<AutomationTarget> targetOfPlugin (const juce::AudioPluginInstance& plugin, int param) const;
    // Number of points recorded as automation since recording started (tests, hint bar).
    int getRecordedAutomationPoints() const noexcept { return recordedAutomationPoints; }
    // Names a new recorded automation (the UI sets it to its readable target names).
    std::function<std::string (const AutomationTarget&)> nameAutomation;

    // MIDI inputs: every device is enabled and plays the selected channels unless the MIDI settings say otherwise
    // (Settings › MIDI: device off, one MIDI channel only, a fixed Channel Rack channel; velocity curve; pickup).
    juce::StringArray getActiveMidiInputs() const;
    // Counts the messages received from the enabled MIDI inputs (the toolbar's MIDI activity light).
    std::uint32_t getMidiActivity() const noexcept { return midiActivity.load (std::memory_order_relaxed); }
    std::uint32_t getMidiPlayed() const noexcept { return midiPlayed.load (std::memory_order_relaxed); }
    // Closes and opens again every enabled MIDI input (the MIDI light's click; Settings › MIDI › Refresh).
    void reconnectMidiInputs();
    // Round 190gh (B8): MIDI device hot-plug. The device lists are compared on the system's notification and every 2 s
    // (FL: "MIDI devices have changed... refreshing..."): enabled inputs that appear are opened by name, an input that
    // was unplugged while open is closed at once, the sync outputs and the controllers' feedback ports are opened
    // again by name, the version below counts up (Settings › MIDI rebuilds its lists) and onMidiDevicesChanged gets
    // the names plugged in and out (the hint bar). UI thread.
    int getMidiDeviceListVersion() const noexcept { return midiDeviceListVersion; }
    std::function<void (const juce::StringArray& added, const juce::StringArray& removed)> onMidiDevicesChanged;
    // --selftest "midi_hotplug" / --midihotplugtest: stand-in device lists in place of the system's (empty = the
    // system's), then "OK (...)" or "ERROR ..." (engine/MidiHotplugTest.cpp)
    static std::function<juce::Array<juce::MidiDeviceInfo> (bool inputs)>& midiDeviceListOverride();
    static juce::String midiHotplugSelfTest();
    const prefs::MidiPrefs& getMidiPrefs() const noexcept { return midiPrefs; }
    // Applies them (and saves them in %APPDATA%\LYNX\midi-settings.txt when `save`).
    void setMidiPrefs (const prefs::MidiPrefs& prefs, bool save = true);
    // master sync out / external clock in / all notes off (round 165f)
    MidiSyncIo& getMidiSync() noexcept { return midiSync; }
    // MIDI controller profiles (round 190aw, core/ControllerSession.h): the devices that run one, and LYNX as they see
    // it (the main window's bridge; null when it closes).
    ControllerIo& getControllers() noexcept { return controllerIo; }
    const ControllerIo& getControllers() const noexcept { return controllerIo; }
    void setControllerSurface (controller::Surface* surface);
    // The user's controller profiles (*.lynxctl).
    static juce::File controllerProfilesFolder();
    // Reads the profiles again (Settings › MIDI "Refresh device list") and moves the devices to them.
    void reloadControllerProfiles();
    // Values for parameters as a MIDI link sets them: one edit outside the undo history; plugin parameters go to the
    // plugin.
    void applyTargetValues (const std::vector<std::pair<AutomationTarget, float>>& changes);

    // How many undo steps are kept (Settings › General).
    void setUndoLevels (int levels);
    int getUndoLevels() const noexcept { return (int) maxUndoSteps; }

    // Document (UI thread)
    const Song& getSong() const noexcept { return song; }
    // The values automation gives the controls at the playhead while the song plays (issue #12: knobs and faders move
    // with their automation, as in FL). Refreshed at most once per UI frame, on the UI thread only; the audio thread
    // does no extra work for it. An editor shows `shown (target, its Song value)` and repaints when the generation
    // changes.
    const AutomationDisplay& getAutomationDisplay();
    // the time the last refresh took (ms), for --perf style measurements
    double getAutomationDisplayMs() const noexcept { return automationDisplayCostMs; }
    void setSong (Song newSong); // new/opened project: clears the history
    // "Open without third-party plugins" (round 190ap, core/PluginHold.h): the project opens with every hosted plugin
    // switched off (none is created; each slot keeps its saved settings untouched, also through a save). The next
    // project this engine opens the plain way (setSong, an import) ends the mode.
    void setSongWithPluginsOff (Song newSong);
    bool pluginsOff() const;                              // the mode is on and this engine's project is the one it is for
    std::vector<pluginhold::Use> hostedPluginUses() const; // the song's hosted plugins, each marked off or on
    // Switches the plugin `id` back on ("" = all of them, which ends the mode): its slots are created from the settings
    // in the project. Returns how many slots were built again.
    int switchPluginOn (const std::string& id);
    bool channelPluginOff (int channel) const noexcept { return std::find (channelsOff.begin(), channelsOff.end(), channel) != channelsOff.end(); }
    bool insertPluginOff (int track) const noexcept { return std::find (insertsOff.begin(), insertsOff.end(), track) != insertsOff.end(); }

    // Edits the song and publishes it to audio. `undoable` edits go into the history;
    // all edits made while the same mouse click is held down
    // (dragging a note, turning a knob) become a single step.
    void editSong (const std::function<void (Song&)>& edit, bool undoable = true);
    // FL's General › "Detach all new plugins" (issue #116; a UI setting, off by default as in FL): the plugins an edit
    // adds start Detached (detachNewPlugins in core/Song.h)
    void setDetachNewPlugins (bool on) noexcept { detachNewPluginsOn = on; }
    bool detachesNewPlugins() const noexcept { return detachNewPluginsOn; }

    // For an edit that moves or removes channels, mixer tracks or effect slots (issue #32, PR #37): the live plugin
    // settings are copied into the Song first, so the undo step has them; the moved plugins keep their instances (the
    // rack matches them by instance tag, round 168s).
    void editSongStructural (const std::function<void (Song&)>& edit);
    // An edit that gives the patterns other indices (sort, move): one undo step like editSong, but without
    // followPatternLengths, which compares by index and would resize clips that were sized by hand (round 177d).
    void editSongPatternOrder (const std::function<void (Song&)>& edit);
    // An edit that changes only levels (mixer volume / pan / separation, channel volume / pan), e.g. a fader drag: one
    // undo step per press like editSong, but the playing snapshot takes the values at once without a rebuild (round
    // 171c); the full publish follows when the mouse is released. Anything else falls back to editSong.
    void editLevels (const std::function<void (Song&)>& edit, bool undoable = true);
    // the publish editLevels deferred, now (tests; the timer does it after a drag)
    void flushLevels();
    std::uint64_t getLevelEditCount() const noexcept { return levelEdits; }
    // editLevels notifies this one only (the engine's own change message follows with the full publish after the drag):
    // the ~45 views listening to the engine would otherwise all repaint at every mouse move. The views that show
    // levels (Mixer strips, Channel Rack) listen to it too.
    juce::ChangeBroadcaster& getLevelsBroadcaster() noexcept { return levelsChanged; }
    // + 1 at every full publish (not at a level-only edit): listeners can tell a drag's level change from the rest
    std::uint64_t getPublishCount() const noexcept { return publishCount; }

    // Changes since the last save/open (for the "*" in the title and the "save changes?" prompt).
    bool isModified() const noexcept { return modified; }
    void markSaved() noexcept { modified = false; sendChangeMessage(); }
    void markModified() noexcept { modified = true; sendChangeMessage(); } // e.g. a backup was opened in place of the project

    bool canUndo() const noexcept { return sessionHistory ? ! sessionUndo.empty() : ! undoStack.empty(); }
    bool canRedo() const noexcept { return sessionHistory ? ! sessionRedo.empty() : ! redoStack.empty(); }
    void undo();
    void redo();

    // Online Sessions P2 (round 169b): a live session's copy of the song changed by someone else's edit — like setSong,
    // but the history stays, the project stays modified and nothing becomes an undo step.
    void applyRemote (Song remote);
    // Undo per user (ONLINE_SESSIONS.md §1.4): while on, undo / redo bring back only this person's own entities (each one
    // still as his step left it), never other people's edits made since. `onSkipped` names what had to stay.
    void setSessionHistory (bool on);
    bool hasSessionHistory() const noexcept { return sessionHistory; }
    std::function<void (const juce::StringArray& skipped)> onSessionUndoSkipped;
    std::uint64_t getSongStamp() const noexcept { return songStamp; }

    // Undo history as a list of states, oldest first: the undo steps, the current state, then the redo
    // steps. Each entry is named after what changed from the previous one (core/History.h).
    int getHistorySize() const noexcept
    {
        return sessionHistory ? (int) (sessionUndo.size() + 1 + sessionRedo.size()) : (int) (undoStack.size() + 1 + redoStack.size());
    }
    int getHistoryPosition() const noexcept { return sessionHistory ? (int) sessionUndo.size() : (int) undoStack.size(); }
    ChangeLabel getHistoryLabel (int index) const; // index 0 = the oldest state kept
    void jumpToHistory (int index);

    // Channel selected in the Channel Rack (target of the Piano Roll and keyboard). UI thread.
    int getSelectedChannel() const noexcept { return selectedChannel; }
    void setSelectedChannel (int channel); // selects only this channel

    // Multi-selection in the Channel Rack (sorted). The selected channels all receive live MIDI input;
    // `primary` (one of them) is the Piano Roll / keyboard target above.
    const std::vector<int>& getSelectedChannels() const noexcept { return selectedChannels; }
    void setChannelSelection (std::vector<int> channels, int primary);

    // Imports a .flp, locating the FL installation to find the samples.
    // Returns false and fills `error` on failure. `relocations` (saved sample path → file to use) are the samples
    // the user located; with `asEdit` the result replaces the song as one undoable step instead of a new project.
    bool importFlpFile (const juce::File& file, juce::String& error, const std::map<std::string, std::string>& relocations = {},
                        bool asEdit = false);

    // The same import in steps, so a window can show progress while it runs (round 112): `prepareFlpImport` reads,
    // parses and imports the project and decodes its samples in parallel — no plugin is touched, so it may run on any
    // thread; `preloadPlugins` creates the project's hosted plugins one at a time (message thread; call it until it
    // returns false); `finishFlpImport` adopts the result as the song (message thread). Phase times go to core/LoadTrace.
    struct PreparedFlp
    {
        bool ok = false;
        juce::String error;
        Song song;
        FlpImportReport report;
        std::vector<std::shared_ptr<const SampleData>> decoded; // kept alive until the rack is built
        std::vector<PluginRef> pluginQueue;                     // hosted plugins to create, in rack order
        std::size_t pluginsDone = 0;
        bool adopted = false;
        juce::StringArray missing, matched, withoutState, substituted;
        struct Use
        {
            std::string name, user;
            bool instrument = false;
        };
        std::vector<Use> uses;
    };
    // Settings › General: a Live set's Session clips go to the clip grid (false, the default) or the Playlist (round 181c)
    static inline std::atomic<bool> alsSessionOnPlaylist { false };
    static void prepareFlpImport (const juce::File& file, const std::map<std::string, std::string>& relocations, SampleLibrary& library,
                                  PreparedFlp& out);
    // one hosted plugin of the queue (message thread); false when the queue is done
    bool preloadNextPlugin (PreparedFlp& prepared);
    // Before importing the open project again: leaves out of `prepared`'s plugin queue those the song already runs
    // with the same settings (the rack keeps them).
    void keepRunningPlugins (PreparedFlp& prepared);
    bool finishFlpImport (PreparedFlp&& prepared, bool asEdit);

    // --- real-time bounce (round 165c: Export › "Render through the hardware in real time") ---
    // The song plays through the audio device from `startTick` (song mode; past its end and through the loop region,
    // without starting over) while the Master's output is recorded: hardware inserts send to and return from their gear
    // as they do while playing. Message thread; returns an error (empty: started).
    juce::String startRealtimeBounce (double startTick);
    // What the Master played since the last call, from the first sample the song played (one consumer thread).
    int pullRealtimeBounce (std::vector<float>& left, std::vector<float>& right) { return meters.bounceTap.pull (left, right); }
    bool isBouncing() const noexcept { return bouncing; }
    // While bouncing: the transport still plays (false: the user stopped it), the delay compensation's total at the
    // start, the device's dropouts and the frames the tap lost since the start.
    bool bounceStillPlaying() const noexcept { return transport.playing.load(); }
    int bounceLatency() const noexcept { return bounceStartLatency; }
    int bounceDropouts() const;
    // Stops playback and puts the transport back as it was (message thread).
    void finishRealtimeBounce();
    // Renders without an audio device. Writes to `output` if not nullptr.
    // After `seconds`, continues for up to `tailSeconds` while there is still sound.
    // `progress(0..1)` returns false to cancel. Returns the peak (used by the self-test),
    // or -1 if cancelled.
    struct RenderOptions
    {
        double seconds = 10.0;
        double sampleRate = 44100.0;
        juce::AudioFormatWriter* output = nullptr;
        std::function<bool (double)> progress; // 0..1; false cancels
        double tailSeconds = 0.0;
        int trimSamples = 0;  // PDC: discards this much from the start and renders the same amount extra at the end
        int ditherBits = 0;   // > 0: TPDF dither (with noise shaping unless ditherShaping is false) for this bit depth
        bool ditherShaping = true;
        // FL's "HQ for all plugins": render qualities and HQ switches on (off: the playback quality)
        bool hqAllPlugins = true;
        // receives every rendered block after the PDC trim (left, right, frames), like `output` does
        std::function<void (const float*, const float*, int)> capture;
        // trim the delay compensation's latency as the plugins report it after the warm-up (adds to trimSamples)
        bool trimPdc = false;
        // Export "Plugin warm-up" (round 155, core/PluginWarmup.h): with hosted plugins in the song, silent blocks are
        // processed and thrown away before the render until the plugins have settled
        bool pluginWarmUp = true;
        warmup::Settings warmUp;
        // false: samples above 0 dBFS are clamped to ±1 before the dither / the file (integer formats, Ogg)
        bool keepOvers = false;
        // FL never puts the metronome in an export (round 165z-a2, measured): off, the click is left out even with
        // the metronome on. (The selftest turns it on to measure the click.)
        bool includeMetronome = false;
        // Round 190gj (several files from one render: StemPass, engine/Exporter.h). With `shared`, nothing goes to
        // `output` / `capture` and the render's own end does not apply: after the warm-up shared->start (latency) is
        // called (the latency the plugins report then), each block's size comes from shared->nextBlock (done) (0 = the
        // end) and its output, as the graph made it (before the overs are clamped), goes to shared->block.
        struct Shared
        {
            virtual ~Shared() = default;
            virtual void start (int latency) = 0;
            virtual int nextBlock (std::int64_t done) = 0;
            virtual void block (const juce::AudioBuffer<float>& raw, std::int64_t done, int n) = 0;
        };
        Shared* shared = nullptr;
        // Round 190gh (B7): > 0 renders exactly this many samples after the PDC trim (music, then the tail, without
        // stopping at the tail's silence): the stems of one export all get the mix's length, as FL's split render
        std::int64_t exactSamples = 0;
    };
    static constexpr int renderBlockSize = 512; // renderOffline's blocks
    // What renderOffline does after the graph with these options when the plugins report `latency` (round 190gj: the
    // single-pass stems make each file with the same setup).
    static RenderStream::Setup streamSetup (const RenderOptions& options, int latency);

    float renderOffline (const RenderOptions& options);
    // How many offline renders run now, in any engine (round 168s, issue #32). While one runs, the engine that plays
    // through the audio device stops and outputs silence without processing (FL stops playback while it renders): the
    // project's live plugins never run next to their export copies (plugins that share state between instances cut
    // notes in the file) and no CPU goes to them (the glitches heard until the progress bar ended).
    static std::atomic<int>& offlineRendersRunning() noexcept;

    // timings of the last renderOffline (diagnostics): the audio graph and the file writing
    struct RenderStats
    {
        double graphMs = 0.0, writeMs = 0.0;
        warmup::Result warmUp; // the plugin warm-up before the render (blocks == 0: none ran)
        int trimmedSamples = 0; // the PDC trim used
    };
    RenderStats getLastRenderStats() const noexcept { return lastRenderStats; }
    float renderOffline (double seconds, double sampleRate, juce::AudioFormatWriter* output = nullptr)
    {
        RenderOptions o;
        o.seconds = seconds;
        o.sampleRate = sampleRate;
        o.output = output;
        return renderOffline (o);
    }

    int getNumGraphConnections() const { return (int) graph.getConnections().size(); }

    // Time spent in each instrument and effect (the Plugin performance monitor); nullptr without the rack node.
    ProcessTimes* getProcessTimes() noexcept;

    // Latest output samples (mono), for the oscilloscope. UI thread.
    void copyScope (float* destination, int count) const noexcept;
    static constexpr int scopeSize = 2048;

    // Real-time health of the audio callback (REA round 87): callbacks over their deadline, late device calls, load.
    const RtStats& getRtStats() const noexcept { return rtStats; }
    RtStats& getRtStats() noexcept { return rtStats; }

    // What takes the most audio CPU: channels and insert effects, as a share of real time over the last ~2 s of
    // playback, heaviest first. UI thread.
    struct UnitLoad
    {
        juce::String name;
        double load = 0.0;      // CPU time / audio time (summed over the worker threads)
        double longestMs = 0.0; // the longest single call in the window
    };
    const std::vector<UnitLoad>& getHeaviestUnits() const noexcept { return heaviestUnits; }

    // How regularly the message thread runs the engine's 30 Hz timer (a saturated UI delays it). UI thread.
    struct UiPulse
    {
        int calls = 0;
        double maxGapMs = 0.0;
    };
    UiPulse takeUiPulse() noexcept { return std::exchange (uiPulse, {}); }

    // Writes `state` (a createStateXml() taken earlier; nullptr = none was saved) back as the saved device and stops
    // saving later changes: a test run (--rtstress --type ...) leaves the user's device as it found it.
    void restoreAudioDeviceState (const juce::XmlElement* state);
    // Device, buffer, deadline and the callback statistics in a few lines (CPU meter hint, glitch log, --rtstress).
    juce::String describeRealtime (bool withUnits = true) const;

    // Settings › Audio "Driver settings…": shows the device's own panel (ASIO) and, when the driver says its settings
    // changed, reopens it with them. The device object is replaced (its driver re-read), so the old pointer is never
    // used after the close (0.1.17 / 0.1.18 called the deleted device: issues #12 and #13), and the buffer size the
    // panel chose is kept (the reopen asks for the driver's preferred size instead of the size LYNX had before, which
    // silently undid an ASIO4ALL / FL ASIO buffer change). Returns true when the device was reopened. Message thread.
    static bool showDriverPanel (juce::AudioDeviceManager& devices);
    // --selftest "driver_panel": showDriverPanel with a fake device whose panel changes its buffer (and one whose panel
    // changes nothing). "OK" or what went wrong.
    static juce::String driverPanelSelfTest();
    // --selftest "device_change" / --devicechangetest (issue #134, engine/DeviceChangeTest.cpp): a playing project while
    // the output device, its sample rate and its buffer size change, on stand-in devices with real callback threads.
    // "OK (...)" or "ERROR ...". `everyUnit`: every instrument and effect LYNX can make here (else a handful);
    // `pluginFiles`: hosted plugins to add; `log` receives a line per change.
    static juce::String deviceChangeSelfTest (int switches, bool everyUnit, const juce::StringArray& pluginFiles = {},
                                              const std::function<void (const juce::String&)>& log = {});
    // --selftest "export_parity" / --exportparitytest (round 190gh, B7; engine/ExportParityTest.cpp): the export against
    // playback through a stand-in audio device (null below −90 dBFS), stems of every mode starting together with one
    // length and adding up to the mix, group effects and multi-output inserts in "one per mixer insert", the delay
    // compensation's trim on and off. "OK (...)" or "ERROR ...". `log` receives a line per check.
    static juce::String exportParitySelfTest (const std::function<void (const juce::String&)>& log = {});
    // %APPDATA%\LYNX\audio-glitches.log: a line each time callbacks run over their deadline (at most every 2 s).
    static juce::File glitchLogFile();

    // Total latency compensated by PDC (at the Master output), in samples.
    int getLatencySamples() const noexcept { return lastPublishedShape.pdc.total; }
    // Round 190gj (single-pass stems): the latencies the published PDC plan was made from (the channels' instruments,
    // then each insert's slots), for computing the plan of a changed copy of the song.
    void getPublishedLatencies (std::vector<int>& channels, std::vector<std::vector<int>>& slots) const;
    // The render taps of the rack (RackRenderer::RenderTaps; nullptr = none). Set around renderOffline only.
    void setRenderTaps (RackRenderer::RenderTaps* taps) noexcept;
    // The last published PDC plan (per-track latencies: outLat, inAlign). Message thread.
    const PdcPlan& getPdcPlan() const noexcept { return lastPublishedShape.pdc; }
    // "Low latency while recording" (round 190aq): whether slot `slot` of insert `track` is bypassed for it now, the
    // latency it would add (samples, 0 = not bypassed), and how many slots are bypassed in all. Message thread.
    bool isSlotLowLatencyBypassed (int track, int slot) const noexcept { return lowlatency::bypassed (lastPublishedShape.lowLatencySlots, track, slot); }
    int lowLatencyBypassedLatency (int track, int slot) const noexcept;
    int lowLatencyBypassedCount() const noexcept;

private:
    bool detachNewPluginsOn = false;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& message) override;
    void enableAllMidiInputs();
    void saveAudioDeviceState();
    void updateRecording();
    void updateAudioRecording();
    void updateAutomationRecording (bool active);
    void startAudioTake();
    void finishAudioTake();
    double estimatedPositionNow() const noexcept;
    AutomationDisplay automationDisplay;
    std::uint64_t songStamp = 1;            // + 1 at every publishSong (the display's tables follow the Song)
    double automationDisplayAtMs = -1.0e9;  // when it was last refreshed
    double automationDisplayCostMs = 0.0;

    void buildInitialGraph();
    void choosePreferredDriver(); // a first start on Linux: PipeWire, then JACK, then ALSA (round 161)
    void chooseSafeBufferSize();
    void updateRealtimeStats(); // timer: unit loads over a window, glitch log
    RtStats rtStats;
    std::vector<UnitLoad> heaviestUnits;
    std::vector<std::uint64_t> lastChannelNs, lastEffectNs;
    std::uint64_t lastProcessedSamples = 0;
    double lastUnitsWindowMs = 0.0, lastUiPulseMs = 0.0;
    UiPulse uiPulse;
    int latencyRepublishes = 0;   // PDC re-plans because a unit's latency changed (diagnostics)
    juce::String lastLatencyChange;
    // hardware inserts (round 165c): ping results applied to the slots (whether an editor is open or not), and the device
    // setup they were measured for
    void applyHardwareInsertPings();
    std::map<const void*, int> hwPingSerials; // running insert -> the last ping result seen
    std::string lastSetupKey;
    std::vector<HardwareInsertRef> hwNotice;
    int hwTicks = 0;
    bool bouncing = false;
    int bounceStartLatency = 0, bounceStartXruns = 0;
    double bounceReturnTick = 0.0;
    bool bounceRestoreMode = false;
    bool checkHardwareInserts = false;
    std::uint64_t loggedOver = 0, loggedLate = 0;
    double lastLogMs = 0.0;
    class PreviewPlayer* preview = nullptr; // graph node (owned by the graph)
    int clipPreviewChannel = -1, clipPreviewKey = -1; // the clip previewed through its channel (round 190ey), -1 = none
    std::uint64_t clipPreviewSerial = 0;
    double previewVolume = 1.0;
    // "Preview in time" (round 190aq)
    bool previewInTime = false;
    PreviewTiming previewTiming;
    std::uint64_t previewSerial = 0;                              // raised by every previewFile: a stale job's result is dropped
    double previewHandedAtMs = 0.0;                               // when the sample went to the player
    std::shared_ptr<std::atomic<bool>> previewAlive = std::make_shared<std::atomic<bool>> (true);
    std::map<juce::String, previewtempo::Found> previewTempos;    // by path (the file's tempo, found once)
    struct PreviewStretch
    {
        juce::String path;
        double ratio = 1.0;
        std::shared_ptr<const SampleData> sample;
    };
    std::vector<PreviewStretch> previewStretches;                 // the last few stretched previews
    void startPreviewInTime (const juce::String& path, std::shared_ptr<const SampleData> sample, const PreviewTiming& timing);
    struct PreviewResult
    {
        std::uint64_t serial = 0;
        juce::String path;
        std::shared_ptr<const SampleData> sample, out; // the file, and what plays (the same when not stretched)
        PreviewTiming timing;
        previewtempo::Found found;
        bool detected = false; // the tempo was read from the audio now: kept for the next click
    };
    std::mutex previewResultLock;
    std::vector<PreviewResult> previewResults; // worker → message thread
    double lastPreviewPrepareMs = 0.0;
    void takePreviewResults();
    double editorVolume = 1.0;
    void publishSong();
    RackState::Factories rackFactories();
    void adoptPrepared (PreparedFlp& prepared); // the plugins of a prepared import found / substituted (message thread)

    std::unique_ptr<WorkerPool> workers; // extra renderer threads (before the graph, which uses them)
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    Transport transport;
    MidiSyncIo midiSync; // (before the MIDI callbacks and the audio callback use it)
    ControllerIo controllerIo; // (before the MIDI callbacks use it)
    SampleLibrary samples;
    LiveEventQueue liveEvents;   // UI -> audio
    LiveEventQueue midiEvents;   // MIDI thread -> audio
    InputHeldNotes midiHeld;     // MIDI thread: what each held MIDI key started
    // issue #108 (MIDI thread): the MPE controller's zone — its member channels' bend / CC 74 / pressure per note
    // (one decoder: two MPE controllers played at once would share it)
    mpe::Input mpeInput;
    mpe::Zone mpeZone;           // the zone of the device's settings mpeInput was last set to
    PedalHold pedalHold;         // MIDI thread: FL's foot pedal hold per channel (round 187a)
    std::atomic<bool> footPedalNoteOff { true }; // FL's Settings › MIDI "Foot pedal controls note off" (on)
    std::atomic<std::uint64_t> heldKeyBits[16][2] {}; // keys held per MIDI channel on inputs / the typing piano (chord readout)
    std::atomic<std::uint8_t> heldKeyCount[16][128] {};
    void markHeldKey (int midiChannel, int key, bool on) noexcept;
    std::atomic<int> lastHitKey { 60 };
    KeyLedger playedKeys;        // UI thread: the keys the typing piano started (after the scale snap)
    SpscQueue<RecordedEvent, 1024> midiRecordQueue; // MIDI thread -> UI (recording)
    // a controller's pitch wheel while recording (MIDI thread -> UI): key = its MIDI channel 0…15, velocity = 0..1;
    // written as the channel's pitch bend automation like a moved knob (issue #12: the bend was heard, not recorded)
    SpscQueue<RecordedEvent, 2048> bendRecordQueue;
    std::map<int, float> liveBends; // UI thread: the last recorded bend of each channel (its "current value")
    void takeRecordedBends();

    struct ControllerEvent
    {
        int cc = 0, channel = 0, value = 0;
        int port = -1; // the input device's port number (Settings > MIDI; round 190bt), -1 = none
        // round 190cs: `cc` 256 + n is key n (a note link; value = the velocity, 0 for its release). seenOnly: a key
        // nothing links, offered to Auto detect only
        bool seenOnly = false;
    };
    SpscQueue<ControllerEvent, 1024> controllerQueue; // MIDI thread -> UI (controllers)
    // the CCs a MIDI link (project or global) uses: they move their parameter and are not also played into the
    // selected channel's instrument (round 162); written by applyControllers, read by the MIDI thread
    std::array<std::atomic<std::uint64_t>, 2> linkedCcs {};
    // the keys a link uses (round 190cs: FL's note links, controller number 256 + key): their presses and releases go
    // to the link instead of the instrument. Written by applyControllers, read by the MIDI thread.
    std::array<std::atomic<std::uint64_t>, 2> linkedNotes {};
    AutomationTarget learning;
    ControllerSeen controllerSeen;
    // round 190ef: the map mode, the control it caught, and each controller's last value (a button's press / release)
    remotemap::Mode mapMode = remotemap::Mode::off;
    std::optional<remotemap::MidiControl> mapCaptured;
    std::map<std::tuple<int, int, int>, int> remoteValues; // (number, channel, port) -> value
public:
    std::atomic<bool> mapCaptureNotes { false }; // (MIDI thread: in MIDI Map Mode every key goes to applyControllers)
private:
    // round 190bt: a link's compiled mapping formula, and the smoothing filters of the links being moved
    std::map<std::string, linkformula::Formula> linkFormulas;
    struct LinkSmoothing
    {
        MidiLink link;
        float target = 0.0f;
        linkformula::Smoother filter;
    };
    std::vector<LinkSmoothing> linkSmoothing;
    double linkSmoothingClock = 0.0;
    void stepLinkSmoothing (std::vector<std::pair<AutomationTarget, float>>& changes);
    std::optional<MultilinkSession> multilink;
    bool learningGlobal = false;
    bool controllerLearnTick = false; // a MIDI learn was waiting at this tick's applyControllers (round 190aw)
    bool saveGlobalLinks = false; // only the engine with an audio device (the app) writes the settings file
    void applyControllers();
    std::atomic<int> pendingMarkerNote { -1 }; // MIDI thread -> timer: a "Song marker jump" note
    std::vector<RecordedEvent> uiRecorded;           // PC keyboard -> recording
    NoteRecorder recorder;
    // last tweaked parameters and automation recording (REA round 143)
    std::array<AutomationTarget, 2> lastTweaked {};
    struct Touch
    {
        AutomationTarget target;
        double lastMs = 0.0;
        bool held = false;
        std::vector<RecordedValue> pending; // sampled, not yet written
        float liveValue = -1.0f; // an internal controller's value to write instead of the control's (round 190cs)
    };
    std::vector<Touch> touches;
    int automationCommitCountdown = 0;
    bool automationTakeHasUndoStep = false;
    std::int64_t automationLoopLength = 0;
    int automationPattern = 0;
    int recordedAutomationPoints = 0;
    bool automationOverrideActive = false; // touched targets override their automation in the renderer
    // FL (issue #125): the targets the user moved since playback last started; their automation does not drive them
    // until it starts again (RackRenderer::setReleasedSlots). Not while recording automation (the touch rule above).
    std::vector<AutomationTarget> releasedTargets;
    bool releasedWasPlaying = false;
    void publishReleasedTargets();
    // Song mode, no loop region: once a parameter moved while recording, the song plays on past its end (no wrap to
    // the start) until recording stops, and the take's clip grows with the playhead (issue #12: it cut itself)
    bool automationLinear = false;
    // the same for the whole of a take (FL: Song mode plays on while recording; round 168k)
    bool noteTakeLinear = false;
    std::int64_t lastAutomationTick = -1; // the song position of the take's last update (song mode)
    std::vector<AutomationWrite> takeWrites; // per target: where this take writes (its own clip grows)
    bool takeHasUndoStep = false;
    // Cancel recording (round 187a): the undo depth when this recording pass started, and the steps trimmed off the
    // stack's bottom since then (the depth then still counts them)
    bool recordPassActive = false, recordPassMarked = false, discardAudioTakes = false;
    std::size_t recordPassUndoBase = 0, recordPassTrimmed = 0;
    bool perfTakeHasUndoStep = false; // the first clip a performance recording writes is the take's undo step
    void updatePerformanceRecording();
    void updateLauncherRecording();
    // recording into slots (round 190bv): UI thread
    struct SlotTake
    {
        int track = 0, scene = 0;
        bool audio = false;
        int channel = -1;   // MIDI: the channel whose notes it takes (−1: any)
        int insert = -1;    // audio: the armed insert
        bool known = false; // its end is known: [from, until) on the launcher's clock
        double from = 0.0, until = 0.0;
        bool thenPlay = false, placed = false;
        std::size_t placedEvents = 0;
        std::vector<RecordedEvent> events;
    };
    std::vector<SlotTake> slotTakes;
    std::vector<int> gridArmed;
    bool sessionOverdub = false, overdubHasUndoStep = false;
    std::vector<std::pair<int, RecordedEvent>> overdubEvents; // (track, event) not yet written
    int slotRecordBars = 0;
    SpscQueue<RecordedEvent, 1024> slotRecordQueue; // MIDI thread -> UI: notes while a slot records (tick: song position)
    std::vector<RecordedEvent> uiSlotRecorded;
    std::atomic<bool> slotTaking { false };       // a slot records or overdubs MIDI: the played notes are queued
    bool slotAudioWanted = false;                 // a slot records audio: the armed inserts' takes run
    std::atomic<bool> slotAudioCapture { false }; // (the audio thread's view of it)
    std::atomic<double> takeStartClock { -1.0 };  // the launcher's clock at the take's first sample
    void updateSlotRecording();
    void readSlotTakeMessages();
    bool placeSlotAudio (const std::vector<std::pair<juce::File, int>>& files, double ticksPerSample, int latency);
    double slotClockNow() const noexcept;
    bool launcherTakeHasUndoStep = false;
    RecordOptions recordOptions;
    std::vector<int> takeClearedChannels; // replace mode: channels already cleared in this take
    std::atomic<bool> waitingForInput { false };
    void startOnInput() noexcept; // MIDI or UI thread: the first note while waiting starts playback
    std::vector<int> armedInserts;
    // round 190aq: the inserts "Low latency while recording" bypasses latency-adding slots on, as last published
    std::vector<char> publishedLowLatency;
    std::vector<char> lowLatencyTracksNow() const; // what it should be for the song, the armed inserts and the option
    void refreshLowLatency();                      // republishes when that changed (an insert armed or disarmed, the option)
    juce::String releasedInputDevice; // the input device releaseAudioInputIfUnused closed (reopened by name)
    double lastPlayingTick = 0.0;
    RecordOffOnStop recordSwitch; // FL's "Disarm on stop"
    std::atomic<int> inputScale { 0 }, inputScaleRoot { 0 };
    std::atomic<int> liveChannel { 0 };              // selected channel, read by the MIDI thread
    static constexpr int maxLiveChannels = 16;       // other selected channels that also get MIDI input
    std::array<std::atomic<int>, maxLiveChannels> extraLiveChannels {};
    std::atomic<int> extraLiveCount { 0 };
    void publishSelection();
    std::atomic<double> blockTimeMs { 0.0 };         // when positionTicks was last updated
    std::atomic<double> blockDurationMs { 0.0 };     // the last block's length (round 174e: playheadAdvanceTicks)
    // round 174e: the device's callback thread (Windows): its priority when first seen and after LYNX raised it to the
    // workers' MMCSS "Pro Audio" class (a driver's own thread at normal priority competed with the UI; LYNX_DEVICE_BOOST=0:
    // left as the driver made it). Shown in the real-time report.
    std::atomic<unsigned long> deviceThreadId { 0 };
    std::atomic<int> deviceThreadPriorityBefore { -100 }, deviceThreadPriorityAfter { -100 };
    bool boostDeviceThread = true;
    // the live clock (round 170d): the musical position of the next block (count-in included), its tempo, and whether
    // it runs; the output latency the played notes are moved back by
    std::atomic<double> clockTick { 0.0 }, clockTicksPerMs { 0.0 }, outputCompensationMs { 0.0 };
    std::atomic<bool> clockRunning { false }, compensatePlayed { true };
    CountInCatch takeCatch; // UI thread
    double lastCaptureMs = -1.0; // UI thread: the newest note Capture MIDI took
    std::array<std::atomic<float>, scopeSize> scope {}; // ring buffer of the output (written by audio)
    std::atomic<int> scopeWrite { 0 };
    juce::MidiDeviceListConnection midiDevicesChanged;
    std::vector<std::string> lastMidiInputIds;       // the MIDI inputs available at the last check (UI thread)
    std::vector<prefs::MidiPort> lastMidiPorts;       // inputs and outputs at the last check (ids "in:" / "out:"; UI thread)
    bool midiPortsKnown = false;                     // lastMidiPorts was filled once (no hint for the start-up list)
    int midiDeviceListVersion = 0;
    int midiCheckTicks = 0;
    std::atomic<std::uint32_t> midiActivity { 0 }, midiPlayed { 0 };
    prefs::MidiPrefs midiPrefs;                      // UI thread
    juce::SpinLock midiPrefsLock;                    // guards midiPrefsShared (UI writes, MIDI thread reads)
    prefs::MidiPrefs midiPrefsShared;
    // FL's "Receive notes from" (round 165f-rack): the channels locked to a MIDI input device (and MIDI channel, 0 = any),
    // rebuilt from the song on every publish; guarded by midiPrefsLock too
    struct NoteLock
    {
        std::string device;
        int midiChannel = 0;
        int channel = 0;
    };
    std::vector<NoteLock> noteLocks;
    // Round 190ao: each channel's own velocity setting for played notes (a hosted instrument's PluginRef::velocityMode,
    // low byte, and fixedVelocity, high byte; 0 = follow the MIDI settings), rebuilt on every publish; guarded by
    // midiPrefsLock too
    std::vector<std::uint16_t> channelVelocity;
    // round 190ao: the preset audition's note (message thread: auditionPreset and the timer)
    int auditionChannel = -1;
    bool auditionSounding = false;
    double auditionAtMs = 0.0;
    void updateAudition();
    void logPlayedNote (int channel, int key, float velocity, bool on, double tick = -1.0, bool recorded = false);
    mutable juce::SpinLock scoreLogLock;             // MIDI thread and UI thread write, the UI reads
    scorelog::Log scoreLog;
    std::atomic<bool> stepEditOn { false };
    std::vector<stepedit::Played> stepEditQueue;     // under scoreLogLock
    std::map<std::tuple<int, int, int, int, int, int>, prefs::PickupState> pickups; // (cc, channel, target) -> takeover
    // settings / log files written off the message thread (issue #13's freezes); declared before the saver so it
    // outlives it, and its destructor writes what is still queued
    SerialJobs fileJobs;
    SerialJobs previewJobs; // round 190aq: "Preview in time" reads tempos and stretches here
    struct DeviceStateSaver;
    std::unique_ptr<DeviceStateSaver> deviceStateSaver;

    // audio recording
    bool audioArmed = false;
    juce::String recordingError;
    juce::File recordingFolder = defaultRecordingFolder();
    juce::String recordingStem;
    std::vector<juce::File> sessionRecordings;
    juce::TimeSliceThread recordThread { "LYNX recording" };
    RecordingNotice recordingNotice;
    void notify (InputIssue issue, int track = 0, const juce::String& detail = {});
    // One take being recorded (round 163): the audio thread pushes into `stream`, the recording thread drains it
    // into `writer` (crash-safe WAV). Shared by the UI (start/finish), the audio thread (raw pointer under
    // takeLock) and the recording thread (drainer list under drainLock).
    struct Take
    {
        int track = 0;      // the insert that records it (0 = none: Settings' "Record input audio" without armed inserts)
        int first = 0;      // device input channel
        int channels = 0;   // 1 or 2
        // round 168x (issue #41): a take tapped inside its insert (FL's "Record" positions after the input) reads
        // liveInput.taps[tap] after the block was rendered (−1 = the device input itself). Its first `skip` frames are
        // dropped (the latency of the insert's effects up to the tap) and as many frames of silence close it, so it
        // lines up with the input to the sample.
        int tap = -1;
        int skip = 0;
        int skipped = 0;    // audio thread
        int playThrough = -1; // the insert the take's clip plays through (Master = 0; -1 = a new free insert)
        juce::File file;
        std::unique_ptr<takes::TakeStream> stream;
        takes::WavTakeWriter writer;
        std::atomic<bool> failed { false };
        std::vector<float> scratch;
        std::shared_ptr<liverec::LivePeaks> peaks = std::make_shared<liverec::LivePeaks>(); // the live waveform (round 177d)
        juce::CriticalSection drainMutex; // the recording thread vs the UI's final drain
        // recording thread: writes what arrived; false when the file could not be written
        bool drain();
    };
    static constexpr int maxTakes = 16;
    std::array<Take*, maxTakes> takes {};          // guarded by takeLock (the audio thread's view)
    int takeCount = 0;
    std::vector<std::shared_ptr<Take>> openTakes;  // UI thread (owner)
    juce::CriticalSection drainLock;               // the recording thread's list
    std::vector<std::shared_ptr<Take>> draining;
    struct Drainer;
    std::unique_ptr<Drainer> drainer;
    juce::SpinLock takeLock;                       // the audio thread only tries (never waits)
    LiveInput liveInput;
    std::unique_ptr<perf::Live> performanceLive = std::make_unique<perf::Live>(); // Performance mode (round 165m)
    std::unique_ptr<launcher::Runtime> clipLauncher; // made when a project first has a clip grid (round 181a)
    std::atomic<int> performanceMidiChannel { 0 }; // MidiPrefs::performanceChannel (UI writes, MIDI thread reads)
    std::atomic<bool> performanceMidiOn { false }; // Performance mode is on with a zone (UI writes, MIDI thread reads)
    perf::MidiMapping performanceMidi;             // MIDI thread only
    HardwareOutput hwOutput;                              // inserts' hardware outputs (renderer -> callback)
    std::array<int, HardwareOutput::maxChannels> outputSlot {}; // device output channel -> callback output index (-1: closed)
    std::atomic<int> masterOutChannel { outputDefault };  // the Master's target (UI -> audio)
    std::atomic<int> masterOutMode { 0 };
    std::vector<float> masterScratch = std::vector<float> (2 * HardwareOutput::capacity, 0.0f);
    void routeOutputs (float* const* outputs, int numOutputs, int n) noexcept; // audio thread
    std::array<float, maxMixerTracks * 2> lastInputPeaks {};
    std::array<int, LiveInput::maxChannels> inputSlot {}; // device input channel -> callback input index (-1: closed)
    // audio thread: its copy of the markers' actions (Transport::readMarkers) and how many marker jumps it made
    MarkerPlan rtMarkers;
    std::uint32_t rtMarkerVersion = 0;
public:
    // marker jumps / pauses the audio thread made since start (tests, the hint bar); +1 each
    std::atomic<int> markerEvents { 0 };
    // Clicking the ruler / a marker while playing, FL's "Song marker jump": jump to `tick`, while playing at the next
    // "Quantize song jumps" step (FL's Playlist menu: 0 Off, 1 Beats, 2 Bars, 3 2 bars, 4 4 bars, 5 8 bars)
    void jumpTo (double tick) noexcept;
    void jumpToMarker (int index) noexcept;   // the index-th marker of the arrangement (by position)
    void jumpToNextMarker (bool forward) noexcept;
    std::atomic<int> quantizeSongJumps { 0 };
private:
    std::atomic<double> takeStartTick { -1.0 };    // position of the first recorded sample
    std::atomic<juce::int64> takeFrames { 0 };
    std::atomic<double> takeTicks { 0.0 };         // ticks the transport moved while recording (not wrapped)
    std::array<std::atomic<juce::int64>, 256> takeWraps {}; // take frames at which the loop jumped back (loop recording)
    std::atomic<int> takeWrapCount { 0 };
    mutable std::vector<int> liveTakeTracks;         // the Playlist tracks the open takes will land on (round 177d)
    double takeSampleRate = 44100.0;
    double takeTicksPerSample = 0.0;
    int takeLatency = 0;                           // takes::Latency::total() at the take's start, in samples
    std::optional<PunchRange> activePunch;         // pre-roll / punch range of the recording in progress (UI thread)
    std::int64_t punchStopAt = -1;                 // post-roll end: playback stops there (-1: none)
    bool linearRecording() const noexcept
    {
        return recordloop::linear (song.mode, song.loopPlays(), recordOptions.loopRecord, recordOptions.punchLoopRegion);
    }
    // the audio thread's view of the above while takes record (false when none is open)
    void updateLinearSong() noexcept
    {
        const auto on = (takeCount > 0 || automationLinear || noteTakeLinear) && linearRecording();
        liveInput.linearSong.store (on || bouncing);
        liveInput.noLoopRegion.store ((on && song.loopPlays()) || bouncing);
    }
    takes::LatencyPing ping;                       // audio thread while running (UI starts it, reads the result)
    juce::int64 lostFrames = 0;
    std::array<int, 2> pingOutputs { -1, -1 };     // callback output slots the click goes to
    int pingInput = -1;                            // callback input slot listened to
    RecordingSettings recordingSettings;
    juce::File journalFor (const juce::File& wav) const { return wav.withFileExtension ("wav.lynxrec"); }
    void writeJournals();
    bool journalsWritten = false;
    MeterBank meters;
    std::unique_ptr<AudioLogger> audioLogger; // after `meters` (it reads meters.logTap): destroyed first
    PluginHost plugins;
    juce::StringArray missingPlugins, matchedPlugins, pluginsWithoutState;
    FlpImportReport importReport;
    std::vector<MissingPlugin> missingPluginDetails;
    RenderStats lastRenderStats;
    class ChannelRackProcessor* rackProcessor = nullptr; // graph node (owner: the graph)

    Song song;
    int selectedChannel = 0;
    std::vector<int> selectedChannels { 0 };
    bool modified = false;
    double workSince = 0.0; // wall clock (ms since 1970) when the working time was last added
    std::unique_ptr<StreamPrefetcher> prefetcher; // only with an audio device (playing live)

    std::size_t maxUndoSteps = prefs::defaultUndoLevels;
    std::deque<Song> undoStack, redoStack;
    // undo per user (round 169b): each local step as (before, after); `sessionAfterOpen` = the last step's "after" is
    // still the song (it is taken when the next step starts, a remote change comes in, or undo is used)
    bool sessionHistory = false, sessionAfterOpen = false;
    std::deque<std::pair<Song, Song>> sessionUndo, sessionRedo;
    void closeSessionStep();
    void sessionStep (bool redo);
    juce::Time lastUndoPress; // mouse click that already produced an undo step
    SnapshotExchange<RackState> rackExchange;
    std::shared_ptr<LiveLevels> publishedLevels; // the last snapshot's live levels (editLevels writes them)
    bool levelsPending = false;                  // editLevels changed the Song since the last publish
    juce::ChangeBroadcaster levelsChanged;
    std::uint64_t levelEdits = 0;                // (diagnostics / tests)
    std::uint64_t publishCount = 0;
    // Issue #134: a publish asked for while one is still building its rack (a plugin being created ran the message
    // loop: the engine's timer, an edit) waits until that one is done; see publishSong
    bool publishing = false, publishAgain = false;
    int nestedPublishes = 0; // (diagnostics / tests)
    // tests: called for every hosted instrument / effect the rack asks for, in place of a plugin that runs the
    // message loop while it is created (DeviceChangeTest)
    std::function<void()> duringPluginCreation;
    void saveUndoStep();
    ProcessTimes* externalTimes = nullptr;
    std::function<std::shared_ptr<Effect>()> externalEffect;
    RackState lastPublishedShape; // channels + instruments of the last snapshot, to reuse voices
    std::vector<int> standInChannels, standInInserts; // (playsStandIn)
    std::vector<int> frozenMissing;                    // (frozenAudioMissing)
    struct FreezeCheck
    {
        std::uint64_t stamp = 0;
        bool outOfDate = false;
    };
    std::vector<FreezeCheck> freezeChecks;             // [channel] (freezeOutOfDate)
    std::vector<int> channelsOff, insertsOff;         // (channelPluginOff / insertPluginOff, round 190ap), as of the last publish
    bool rebuildPlugins = false;                      // switchPluginOn's publish: the slots of rebuildPluginId ("" = all) are created anew
    std::string rebuildPluginId;
    // Issue #36 (u-he Presswerk crashed in its VST3 teardown, freed from the engine's timer callback): the racks the
    // audio thread let go of are freed from a message of their own, not inside the timer callback that collected them.
    // A plugin whose editor is open dies when its window goes (the window closes the editor first). Declared after the
    // rack members: freed first, while the plugin hosts still exist. In the app only (tests have no message loop).
    struct RackDisposer final : private juce::AsyncUpdater
    {
        ~RackDisposer() override
        {
            cancelPendingUpdate();
            flush();
        }
        void add (const RackState* rack)
        {
            {
                const juce::ScopedLock sl (lock);
                items.push_back (rack);
            }
            triggerAsyncUpdate();
        }
        void flush()
        {
            std::vector<const RackState*> now;
            {
                const juce::ScopedLock sl (lock);
                now.swap (items);
            }
            for (const auto* rack : now)
                delete rack;
        }

    private:
        void handleAsyncUpdate() override { flush(); }
        juce::CriticalSection lock;
        std::vector<const RackState*> items;
    } rackDisposer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace lynx
