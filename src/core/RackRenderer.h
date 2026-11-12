#pragma once

// Ties Song + instruments + scheduler together: given the transport state, renders
// a block of stereo audio with notes triggered at the exact sample.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/Arpeggiator.h"
#include "core/ChannelMiscFx.h"
#include "core/ChannelPoly.h"
#include "core/VoiceLimit.h"
#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/HardwareOutput.h"
#include "core/Instruments.h"
#include "core/TimeStretch.h"
#include "core/LiveEventQueue.h"
#include "core/LiveInput.h"
#include "core/LowLatency.h"
#include "core/Meters.h"
#include "core/Pdc.h"
#include "core/ClipLauncher.h"
#include "core/Performance.h"
#include "core/Scheduler.h"
#include "core/Vfx.h"
#include "core/Song.h"

namespace lynx
{
// The levels a drag moves (round 171c, discussion #10): mixer volume / pan / separation and channel volume / pan.
// A snapshot carries its own copy as atomics; while a fader or knob is dragged the UI writes the new value here
// (AudioEngine::editLevels) instead of rebuilding the whole snapshot for every mouse move, and the renderer reads them
// at each block. The next full build starts again from the Song, which holds the same values.
struct LiveLevels
{
    explicit LiveLevels (const Song& song);
    // false = the song's mixer or channel count differs from this snapshot's (a structural edit: rebuild instead)
    bool matches (const Song& song) const noexcept;
    // the song's values into the atomics (UI thread; the layout must match)
    void store (const Song& song) noexcept;

    std::vector<std::atomic<float>> mixerVolume, mixerPan, mixerStereo;
    std::vector<std::atomic<float>> channelVolume, channelPan;
    std::vector<std::atomic<float>> vcaLevel; // VCA faders (round 177a)
};

// Immutable snapshot published by the UI. Instruments are shared between
// consecutive snapshots so voices are not cut off when the song is edited.
struct RackState
{
    Song song;
    std::vector<std::shared_ptr<Instrument>> instruments; // one per channel
    std::vector<std::vector<std::shared_ptr<Effect>>> effects; // [mixer track][slot]
    double sampleRate = 44100.0; // rate the new instruments were prepared with
    // Round 168s (issue #32): this snapshot's number, the one it was built from, and for each channel the channel of
    // that snapshot it is (−1 = new): the renderer moves its per-channel note state along when channels moved
    std::uint64_t serial = 0, builtFrom = 0;
    std::vector<int> channelFrom;
    bool channelsMoved = false;

    // Automation, preprocessed so the audio thread only does table lookups.
    // Indices into song.automations, or -1 = no automation.
    static constexpr int maxAutomations = 1024;
    std::vector<std::vector<int>> automationClips; // per automation: clips in the Playlist
    std::vector<char> zoneAutomations;             // per automation: it has clips in the performance zone
    // round 177b: automation lanes (PlaylistTrack::lanes): the lane automations that play, and per lane the automations
    // of the same target that have clips (where one of their clips plays, it wins over the lane)
    struct LanePlay
    {
        int automation = -1;
        std::vector<int> clipPeers;
        std::vector<std::pair<int, int>> clipSlots; // peer -> the slot holding its value, for peers that share one (issue #97)
    };
    std::vector<LanePlay> laneAutomations;
    // round 177b: modulator routings — each drives its own slot (bound last: it wins) from the target's value, or the
    // value of the automation that drives it, plus depth × the modulator
    struct ModPlay
    {
        int slot = -1, source = -1; // its slot; the slot that drove the target before (-1: none)
        float base = 0.5f;          // the target's own value
        int modulator = 0;
        float depth = 0.0f;
    };
    std::vector<ModPlay> modRoutes;
    // Several automations with clips on one target play as one (issue #97, FL's rule: docs/analise/REA_rodada177b.md):
    // the last of them holds the slot and lists them all here (ascending; empty = it plays alone); the others are
    // marked as followers: they bind nothing and compute nothing.
    std::vector<std::vector<int>> automationGroups;
    std::vector<char> automationFollowers;
    std::vector<int> channelVolumeAuto, channelPanAuto;
    // FL's Levels adjustment (round 190dz): the automation slot of each channel's pan / volume offset ([channel][param],
    // leveladjust::pan / volume; −1 = none). Mod X / Y offsets apply when the instrument is configured.
    std::vector<std::array<int, 2>> channelAdjustAuto;
    std::vector<int> mixerVolumeAuto, mixerPanAuto;
    std::vector<std::vector<std::array<int, maxEffectParams>>> effectParamAuto; // [track][slot][param] (built-in)
    std::vector<std::vector<int>> effectMixAuto;                                 // [track][slot]: dry/wet
    struct RouteAuto
    {
        int from = 0, to = 0, slot = -1;
    };
    std::vector<RouteAuto> routeAutos; // route level knobs (mixerSend), bound to mixerInputs by buildMixerGraph
    std::vector<int> liveAutomations;                                            // automations with a live source (controllers)

    // pattern events (event editor): each lane uses an automation slot after the song's automations
    struct EventLane
    {
        int pattern = 0, lane = 0, slot = 0;
    };
    std::vector<EventLane> eventLanes;
    std::vector<std::vector<int>> patternClips; // per pattern: its clips in the Playlist, by start

    struct PluginParamBinding
    {
        int automation = 0;
        int channel = -1;       // channel instrument, or
        int track = 0, slot = 0; // effect in the slot
        int param = 0;
    };
    std::vector<PluginParamBinding> pluginParams;
    bool hasAutomation = false;
    // the dragged levels (round 171c); never nullptr in a built snapshot
    std::shared_ptr<LiveLevels> live;
    ScheduleIndex schedule; // index to schedule the Playlist without scanning every note
    std::vector<int> performanceZone; // Performance mode: the zone's clips (perf::zoneClips), empty when off
    // The Clip-launch workflow's grid (core/ClipLauncher.h): the engine's runtime while the song has a grid, else nullptr
    // (always, in an export). `launchers`: Performance mode has a zone, or there is a grid.
    launcher::Runtime* clipLauncher = nullptr;
    launcher::GridIndex gridIndex;
    bool launchers = false;
    int tempoAutomation = -1; // tempo automation, if any
    bool tempoMap = false;    // the tempo moves while playing (tempo automation or pattern Tempo events, core/TempoMap.h)

    // PDC: per-edge delays (nullptr = no delay) and the latencies used to build the plan.
    PdcPlan pdc;
    std::vector<std::shared_ptr<DelayLine>> channelDelays;
    std::vector<std::vector<std::shared_ptr<DelayLine>>> sendDelays; // [track][k] like PdcPlan::sendDelay
    std::vector<int> latencies; // channels, then slots of all tracks

    // The Simple workflow's Blend (round 190bu, MixerTrack::chainBlend): the dry/wet of an insert's whole effect chain.
    // One stage per insert whose Blend is under 100 % (nullptr = 100 %: nothing is copied or mixed): the insert's
    // input before its first slot, kept in `dry` and delayed by the chain's latency (`delay`, nullptr = none) so the
    // two paths line up. Built with the PDC plan; a stage of the same latency is kept across rebuilds.
    struct ChainBlendStage
    {
        static constexpr int capacity = 512; // (the renderer's chunk)
        std::shared_ptr<DelayLine> delay;
        int latency = 0;
        std::array<float, 2 * capacity> dry {};
        // round 190dg: the Blend the audio last ran (a new value ramps from it over one chunk instead of stepping; a
        // stage kept for its insert's next state carries it). Written by the audio thread, read by the next build.
        std::atomic<float> lastWet { 1.0f };
        // samples still to come before a new stage's delayed dry path holds the input (until then the mix stays where it
        // was: a delay line starts empty)
        int warmup = 0;
    };
    std::vector<std::shared_ptr<ChainBlendStage>> chainBlends; // [track]

    // Inserts with a hardware output target (REA round 156), each delayed by what it lacks to line up with the
    // Master output (pdc.total − outLat[track]; nullptr = no delay). Built with the PDC plan.
    struct HwRoute
    {
        int track = 0;
        int channel = 0;
        OutputMode mode = OutputMode::stereo;
        std::shared_ptr<DelayLine> delay;
    };
    std::vector<HwRoute> hwRoutes;
    // FL's "Bypass track latency compensation › When recording or not playing" (REA round 165f-mixer): these tracks'
    // output delays (sendDelays[t], their hardware route) are skipped while the transport is stopped or recording.
    std::vector<char> bypassWhenIdle; // [track]
    bool anyBypassWhenIdle = false;

    // "Low latency while recording" (LYNX extra, round 190aq, core/LowLatency.h): the inserts whose latency-adding
    // slots are bypassed in this snapshot (the engine's choice; empty = none, as in every export) and, made with the PDC
    // plan, those slots: active slots of these inserts whose latency is above 0. They are skipped by the renderer and
    // count as 0 in the plan. The song itself is not changed.
    std::vector<char> lowLatencyTracks;                 // [track]
    std::vector<lowlatency::SlotMask> lowLatencySlots;  // [track] bit s = slot s is bypassed
    // The latency of each bypassed slot when the plan was made ([track][slot], 0 elsewhere), for the UI.
    std::vector<std::vector<int>> lowLatencySaved;

    // Current latencies of instruments and effects, in the same layout as `latencies` (0 for the slots bypassed for
    // low latency).
    std::vector<int> currentLatencies() const;
    void buildPdc (const RackState* previous);

    // Arpeggiators of channels with arpeggio on (nullptr for the others); reused across snapshots.
    std::vector<std::shared_ptr<Arpeggiator>> arpeggiators;
    // Echo delays of channels with FL's Misc › Echo delay on (nullptr for the others; REA round 146b).
    std::vector<std::shared_ptr<channelmisc::NoteEcho>> echoes;

    // Cut groups: channels each channel cuts when it plays (cutBy == its cutGroup).
    std::vector<std::vector<int>> cutTargets;
    bool hasCutGroups = false;
    // Layer channels (round 164): their notes play their children (RackRenderer::expandLayers)
    bool hasLayers = false;

    // Mixer: insert processing order and which ones sound with the current solo.
    std::vector<int> mixerOrder;
    std::vector<char> mixerAudible;
    // VCA faders (round 177a slice 3): per insert, the VCAs acting on it (members and parents, empty = none: the whole
    // table is empty while the song has no VCA); per VCA, its automation slot (-1 = none). Built with the mixer graph.
    std::vector<std::vector<int>> vcaChains;
    std::vector<int> vcaLevelAuto;

    // "Pull" model: each track sums the (post-fader) outputs of the tracks that send to it.
    struct MixerInput
    {
        int source = 0;
        int delay = 0; // index into sendDelays[source]
        float level = 1.0f;
        int tap = -1;  // ≥ 0: a Fruity Send slot's signal (sendTaps[tap]) instead of the source's output
        int automation = -1; // the route's level automation (mixerSend, issue #12): its slot; -1 = none
        bool pre = false;    // a pre-fader send (MixerSend::pre): `tap` is the source's pre-fader signal (preTap)
    };
    std::vector<std::vector<MixerInput>> mixerInputs; // [track]; [0] = Master

    // Fruity Send slots (FL's mid-chain sends to another insert): what each one sends is collected in its own
    // buffer while its insert runs, and pulled by the destination like a route.
    static constexpr int maxSendTaps = 64;
    struct SendTap
    {
        int track = 0, slot = 0, target = 0;
    };
    std::vector<SendTap> sendTaps;
    std::vector<std::vector<int>> slotTap; // [track][slot · maxSlotSends + output]: index into sendTaps, or −1
    // the hosted effects' Connections (round 189j): [track][slot], only for tracks with a routed one (else empty)
    std::vector<std::vector<SlotConnections>> slotConnections;
    // Pre-fader sends (LYNX extra, round 190aq): [track] the tap holding the insert's signal before its fader (after
    // its effects and EQ, silenced by its mute), or −1 — one per insert with a pre-fader send, in sendTaps with slot −1.
    // No tap left (maxSendTaps): that insert's pre-fader sends play post-fader.
    std::vector<int> preTap;
    std::vector<std::vector<int>> mixerLayers;        // inserts with no dependencies among them, in order
    // "Allow threaded processing" off (PluginRef::threaded, REA round 134): those channels / inserts always run on the
    // calling (audio) thread, the others may go to the workers. Same layers as mixerLayers.
    std::vector<int> threadedChannels, audioThreadChannels;
    std::vector<std::vector<int>> threadedLayers, audioThreadLayers;
    bool parallel = false;                            // worth splitting across threads
    // Task graph (round 165i): the channels and the inserts as one dependency graph, so an insert starts as soon as
    // the channels and inserts feeding it are done instead of waiting for every channel and for the whole previous
    // layer (FL hands its mixer tracks to its threads by readiness too, REA_rodada165i). Tasks: channel c = c, insert
    // t = graphChannels + t − 1. Each insert sums its own channels (in channel order) before its effects, so the
    // arithmetic is the layered path's, bit for bit. Off (layers) when an insert or channel is pinned to the audio
    // thread or the mixer routing has a cycle.
    bool graphOk = false;
    int graphChannels = 0;                            // channels in the graph (the first ones of song.channels)
    std::vector<int> graphIndegree;                   // [task] tasks that must finish first
    std::vector<int> graphNextStart, graphNext;       // [task] → successors (CSR)
    std::vector<int> graphRoots;                      // tasks with nothing before them, heaviest kind first
    std::vector<std::vector<int>> trackChannels;      // [track] the channels playing into it, in channel order
    void buildMixerGraph();
    void applyVcas(); // VCA mutes into mixerAudible, the VCA chains (round 177a)
    void buildTaskGraph (const std::vector<int>& depth);

    void buildAutomationTables();
    // The automation slots that drive `target` in a snapshot of `song` (its automations, then its pattern event
    // lanes, numbered as buildAutomationTables numbers them). REA round 146: a touched control overrides them.
    static void slotsOfTarget (const Song& song, const AutomationTarget& target, std::vector<int>& out);

    // MIDI Out channels (REA_rodada133 §4): which channels each one's notes and controls reach (the plugin channels
    // whose MIDI input port is its port), and the MIDI channel it sends on. Indexed by channel; empty / −1 otherwise.
    std::vector<std::vector<int>> midiTargets;  // [MIDI Out channel] → plugin channels
    std::vector<std::vector<int>> midiSources;  // [plugin channel] → MIDI Out channels
    std::vector<int> midiOutChannel;            // [channel] MIDI channel 0…15 of a MIDI Out, −1 = not one
    bool hasMidiRoutes = false;
    // The same routes with the port and MIDI channel of each (round 162: Patchers take MIDI ports through their "From
    // LYNX" MIDI port pins, and a generator Patcher sends on several ports / channels through MIDI Out nodes)
    struct MidiRoute
    {
        int other = 0;        // [source] → the target channel; [target] → the source channel
        int port = 0, midiChannel = 0;
        bool portOnly = false; // only what the source sends on that port (a Patcher's VFX output, round 165c)
    };
    std::vector<std::vector<MidiRoute>> midiRoutesOut; // [source channel] → targets
    std::vector<std::vector<MidiRoute>> midiRoutesIn;  // [target channel] → sources
    struct EffectMidiRoute
    {
        int track = 0, slot = 0, port = 0, midiChannel = 0;
        bool portOnly = false;
    };
    std::vector<std::vector<EffectMidiRoute>> effectMidiRoutes; // [source channel] → mixer-slot Patchers on its port
    bool hasEffectMidiRoutes = false;
    // FL's wrapper "Output port" (round 149): [plugin channel] → the plugin channels whose input port is its output port
    std::vector<std::vector<int>> midiOutTargets;
    bool hasMidiOutputs = false;
    // VFX channels (round 164b): their notes go through their processor; output k goes out on MIDI port vfxPort + k
    std::vector<int> vfxChannels;
    std::vector<int> vfxPort; // [channel] (−1 = not a VFX channel)
    // generator Patchers whose VFX nodes send to "To LYNX" MIDI ports (round 165c): taken at each chunk's start
    std::vector<int> portEventChannels;
    // Dashboard and MIDI Out channels (rounds 165, 167i): what their controls send in MIDI goes to the plugins on their
    // port (midiTargets)
    std::vector<int> dashboardChannels;
    // [channel] a MIDI Out with "Map note color to MIDI channel" (round 167i): its notes go out on their colour's channel
    std::vector<char> mapNoteColour;
    void buildMidiRoutes();

    // Loads (or fetches from cache) a file's audio; nullptr on failure.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // The app creates plugin instruments/effects (the core does not know JUCE). nullptr = failed
    // (missing plugin): the channel goes silent / the slot becomes bypass.
    using PluginInstrumentFactory = std::function<std::shared_ptr<Instrument> (const PluginRef&, double sampleRate)>;
    using PluginEffectFactory = std::function<std::shared_ptr<Effect> (const PluginRef&, double sampleRate)>;

    // Sample stretched without changing pitch (ratio = new / original duration); cached in the app.
    // `map`: the channel's warp map (core/TimeStretch warpStretchMap; empty = one ratio)
    using StretchedSampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, double ratio, StretchMethod method,
                                                                                     double semitones, const StretchMap& map)>;

    // A channel's precomputed span (its region after its precomputed effects and Trim) stretched by `ratio`, cached in the
    // app by (path, variantKey, ratio, method): FL stretches that span, not the whole file (round 180d). Unset: stretched
    // here, uncached (tests).
    using StretchedSpanProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, const std::string& variantKey,
                                                                                  const std::shared_ptr<const SampleData>& span, double ratio,
                                                                                  StretchMethod method, double semitones)>;

    struct Factories
    {
        SampleProvider samples;
        StretchedSampleProvider stretchedSamples;
        StretchedSpanProvider stretchedSpans;
        PluginInstrumentFactory pluginInstruments;
        PluginEffectFactory pluginEffects;
        // Round 190ap ("Open without third-party plugins"): true for a plugin whose slots must be created anew in this
        // build although the slot did not change (a plugin switched back on: its slot holds the silent placeholder of
        // when it was off). A Patcher that holds such a plugin is built anew as a whole. Unset: nothing is forced.
        std::function<bool (const PluginRef&)> rebuildPlugin;
        launcher::Runtime* clipLauncher = nullptr; // (round 181a) only the engine's own state, and only with a grid
        // round 190aq: the inserts whose latency-adding slots are bypassed (lowlatency::bypassTracks); empty = none
        std::vector<char> lowLatencyTracks;
    };

    // Builds the next state reusing the instruments from `previous` when the
    // channel changed neither instrument nor sample, and the effects when the slot has the
    // same type. Runs on the UI thread (allocates).
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const SampleProvider& samples = {});
    static std::unique_ptr<const RackState> build (Song song,
                                                   const RackState* previous,
                                                   double sampleRate,
                                                   const Factories& factories);
};

// An audio clip's own audio as its channel's instrument plays it (round 189l, FL's Make unique as sample): the clip's
// pitch, stretch and direction are rendered in, at unity (no clip or channel volume, pan, fades or declick), for the
// clip's length. The channel's own processing (precomputed effects, its pitch and time stretch, envelopes) is in it
// too, and the channel copy that plays the result applies it again. Empty when the clip isn't an audio clip or its
// channel's instrument can't be built. UI thread (allocates).
struct ClipAudio
{
    double sampleRate = 44100.0;
    std::vector<float> left, right;
};
ClipAudio renderClipAudio (const Song& song, std::size_t clipIndex, double sampleRate, const RackState::Factories& factories);

// A channel's instrument heard alone before it is in the song (round 190bu: the Simple workflow's sound picker): the
// notes `keys` (MIDI keys, 60 = C5) one after the other, `noteSeconds` each, then `tailSeconds` of release, at unity
// through no mixer; then the whole is brought to `peak` (0 = left as rendered) so every sound previews at one level.
// Empty when the instrument can't be built or stays silent. UI thread (allocates).
ClipAudio renderAudition (const ChannelDef& channel, const std::vector<int>& keys, double noteSeconds, double tailSeconds, double bpm,
                          double sampleRate, const RackState::Factories& factories, float peak = 0.5f);

class WorkerPool;

// Round 190ao (reload a crashed plugin): the next RackState::build from `shape` makes this channel's / mixer slot's
// hosted plugin again instead of keeping its running instance; nothing else in the shape changes and the song is not
// touched. False when that channel / slot is not a hosted plugin of the shape.
bool forgetHostedPlugin (RackState& shape, int channel);
bool forgetHostedPlugin (RackState& shape, int track, int slot);

// Whether a parallel container slot (FL's Patcher) can keep its built effect: same branches, children and links.
bool sameContainer (const EffectSlot& a, const EffectSlot& b);
// Time spent in each instrument and effect slot (the Plugin performance monitor, round 84). Counted only while
// `enabled` is on; nanoseconds of processing and of audio rendered, accumulated (the reader takes differences).
struct ProcessTimes
{
    static constexpr int channels = 1024;
    std::atomic<bool> enabled { false };
    std::array<std::atomic<std::uint64_t>, channels> channelNs {};
    std::array<std::atomic<std::uint64_t>, maxMixerTracks * maxEffectSlots> effectNs {}; // [track * maxEffectSlots + slot]
    std::atomic<std::uint64_t> audioNs { 0 };  // duration of the audio rendered while enabled
    std::atomic<std::uint64_t> wallNs { 0 };   // time the whole rack took for it
    // Live meters of the effects that have them (the mixing suite's gain reduction, REA round 96; LYNX Tune's pitch and
    // Facet EQ's dynamic gains, round 103; the dynamics suite's band gains and levels, round 108), always published:
    // [(track * maxEffectSlots + slot) * meterOutputs + output] = Effect::controlOutput (output) after the last block.
    static constexpr int meterOutputs = 14; // (round tp172b6: 8 ... 13 = Prism's band detector levels)
    std::array<std::atomic<float>, (std::size_t) maxMixerTracks * maxEffectSlots * meterOutputs> effectMeters {};
    // Round 170c (issue #14, "everything is a shell"): any slot's input and output peaks for an editor's level meter
    // (FL's live meters in Soft Clipper, Multiband Compressor, Transient Processor, Balance...). An open editor counts
    // itself in `levelWanted[key]`; while it is above 0 the audio thread raises slotLevels[key * 2 + 0 / 1] to the
    // block's peak (linear), and the editor takes them with exchange (0) (the highest peak since its last frame).
    std::array<std::atomic<std::uint16_t>, (std::size_t) maxMixerTracks * maxEffectSlots> levelWanted {};
    std::array<std::atomic<float>, (std::size_t) maxMixerTracks * maxEffectSlots * 2> slotLevels {};

    // Audio of one effect slot for an editor's spectrum analyser (Facet EQ, round 103): the editor sets `key`
    // (track * maxEffectSlots + slot, −1 = none); the audio thread then writes the slot's input (pre) and output
    // (post), as mono mid samples, into the rings and advances `written`. Reads may tear; the analyser tolerates it.
    struct AnalyzerTap
    {
        static constexpr int size = 16384; // power of two
        std::atomic<int> key { -1 };
        std::atomic<std::uint32_t> written { 0 };
        std::atomic<double> sampleRate { 48000.0 };
        std::array<std::atomic<float>, size> pre {}, post {};
        // round 166f: the slot's sidechain (Facet EQ's comparative target), written while one is routed (sideOn)
        std::array<std::atomic<float>, size> side {};
        std::atomic<bool> sideOn { false };
    };
    AnalyzerTap analyzer;

    static std::uint64_t now() noexcept
    {
        return (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// FL's mixer level matrix (FLEngine FUN_0127b050, REA_rodada2 §2.2, REA_rodada167j §1): fader, polarity, pan with
// the project's panning law, stereo separation and L/R swap in one 2×2 matrix, outL = ll·inL + rl·inR and
// outR = rr·inR + lr·inL. The side a pan turns down is not lost: half of what it loses folds into the other side
// (hard right = both inputs on the right). NaN in `ll` = not set yet.
struct MixerMatrix
{
    float ll = 1.0f, rl = 0.0f, lr = 0.0f, rr = 1.0f;
    bool operator== (const MixerMatrix&) const = default;
};
// `volume` = the fader position (0..1, MixerTrack::volume), `pan` −1..1 (both possibly automated)
MixerMatrix mixerMatrix (const MixerTrack& track, float volume, float pan, bool triangular) noexcept;
// the same with the separation given (a live value, round 171c) instead of track.stereo
MixerMatrix mixerMatrix (const MixerTrack& track, float volume, float pan, bool triangular, float stereo) noexcept;

class RackRenderer
{
public:
    // A live choke event on this channel = FL's Stop sound (round 170h2): every effect and plugin is reset (the
    // all-notes-off is the panic event, channel −1, sent just before it)
    static constexpr int stopSoundChannel = -2;

    ProcessTimes& processTimes() noexcept { return *times; }

    // Extra threads for channels and mixer layers (nullptr = everything on the audio thread).
    void setWorkerPool (WorkerPool* pool) noexcept { workers = pool; }

    // External audio input (REA_rodada88): inserts with an input source hear it (per their monitor mode) and
    // meter it. The caller fills `input` before each process() call on the same thread. Set before playback.
    void setLiveInput (LiveInput* input) noexcept { liveInput = input; }
    // Hardware output targets of the inserts (REA round 156): their post-fader output is added into `out`'s buses
    // while `out->active`. Set before playback.
    void setHardwareOutput (HardwareOutput* out) noexcept { hwOut = out; }
    // Performance mode (round 165m): the launch commands / what plays, shared with the UI. Set before playback.
    void setPerformance (perf::Live* live) noexcept { perfLive = live; }
    // The metronome's click for the next process() block (round 165z-a2, FL's Settings › Audio › "Metronome track"):
    // added to the input of mixer track `track` (0 = the Master, FL's "---"; an insert this song lacks = the Master),
    // so it plays through that track's effects and fader. nullptr = silent. Same thread as process().
    // FL's Settings › Audio › Mixer: "Play truncated notes on transport" (on by default) and "Reset plugins on
    // transport" (off by default), REA_rodada167j §2–§3. Read by the audio thread at each start of playback.
    // userJumps: a count the transport raises each time the user moves the position (seek, rewind, a quantized jump,
    // a marker jump), so a jump of the position that is the loop's wrap can be told apart (round 167k).
    void setTransportOptions (bool playTruncated, bool resetPlugins, unsigned userJumps = 0) noexcept
    {
        playTruncatedOnTransport = playTruncated;
        resetPluginsOnTransport = resetPlugins;
        userJumpSerial = userJumps;
    }
    // Round 180b (LYNX's effects as VST3 / CLAP): this slot takes its parameters from `params` instead of the snapshot, so
    // a host's automation reaches it at every block without a rebuild. nullptr = the snapshot's. Same thread as process().
    void setSlotParamsOverride (const EffectParams* params, int track, int slot) noexcept
    {
        overrideParams = params;
        overrideTrack = track;
        overrideSlot = slot;
    }
    void setMetronomeInput (const float* left, const float* right, int track) noexcept
    {
        clickLeft = left;
        clickRight = right != nullptr ? right : left;
        clickTrack = track;
    }
    // Round 188b (the plugins' side-chain bus): this audio goes into `track`'s input, as the metronome input does into its
    // own track. nullptr = none. Same thread as process(); the pointers cover the block.
    void setSideInput (const float* left, const float* right, int track) noexcept
    {
        sideLeft = left;
        sideRight = right != nullptr ? right : left;
        sideTrack = track;
    }

    // Allocates the mixer buffers: call off the audio thread.
    // Blocks larger than maxBlockSize are processed in pieces.
    void prepare (double sampleRate, int maxBlockSize = 2048);

    // Audio thread. left/right are overwritten. `live` (optional) carries notes
    // played live, applied at the start of the block.
    void process (const RackState* state,
                  bool playing,
                  double startTick,
                  int numSamples,
                  float* left,
                  float* right,
                  LiveEventQueue* live = nullptr,
                  MeterBank* meters = nullptr,
                  LiveEventQueue* moreLive = nullptr) noexcept;

    // Per-unit CPU profile (REA round 87): the time spent in each channel (instrument, notes, PDC) and in each insert
    // effect slot, accumulated while profiling is on. Off by default (it costs two clock reads per unit and chunk).
    // Written by the audio thread and the workers, read by any thread (relaxed counters, nanoseconds).
    static constexpr int maxChannels = 1024;
    struct UnitTimes
    {
        std::array<std::atomic<std::uint64_t>, maxChannels> channelNs {};
        std::array<std::atomic<std::uint64_t>, (std::size_t) maxMixerTracks * maxEffectSlots> effectNs {}; // [track * maxEffectSlots + slot]
        std::array<std::atomic<std::uint64_t>, maxChannels> channelMaxNs {}; // longest single call (spikes)
        std::array<std::atomic<std::uint64_t>, (std::size_t) maxMixerTracks * maxEffectSlots> effectMaxNs {};
        std::atomic<std::uint64_t> processNs { 0 }, processedSamples { 0 };
    };
    // The keys sounding on a channel (played by the song / pattern, the keyboard, MIDI inputs), for the Piano roll's
    // keyboard (issue #14: FL lights them); published after each block, safe to read from any thread.
    NoteMask soundingKeys (int channel) const noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return {};
        return { sounding[(std::size_t) channel][0].load (std::memory_order_relaxed), sounding[(std::size_t) channel][1].load (std::memory_order_relaxed) };
    }
    void setProfiling (bool on) noexcept { profiling.store (on, std::memory_order_relaxed); }
    bool isProfiling() const noexcept { return profiling.load (std::memory_order_relaxed); }
    const UnitTimes& unitTimes() const noexcept { return *units; }
    // Blocks in which an insert effect output NaN / inf and was muted and reset (all renderers, since start; round 111)
    static std::uint64_t nonFiniteEffectBlocks() noexcept;
    void resetUnitTimes() noexcept;

    // FL's touched control wins over its playing automation while recording (REA round 146): the slots set here
    // (RackState::slotsOfTarget) are not driven by automation clips / pattern events; the control's own value
    // plays. UI thread writes, audio thread reads (relaxed: a block late at worst).
    void setOverriddenSlots (const std::vector<int>& slots) noexcept;
    // FL (issue #125, [EXEC] round 185b): a control the user moves (its knob, a linked controller) is no longer driven by
    // its automation until playback starts again, stopped or playing. The slots set here are released that way; the
    // renderer forgets them itself when the transport starts (and the engine its list of moved targets).
    void setReleasedSlots (const std::vector<int>& slots) noexcept;
    bool isSlotOverridden (int slot) const noexcept
    {
        if (slot < 0 || slot >= RackState::maxAutomations)
            return false;
        const auto bit = 1u << ((unsigned) slot % 32);
        return ((overridden[(std::size_t) slot / 32].load (std::memory_order_relaxed) | released[(std::size_t) slot / 32].load (std::memory_order_relaxed)) & bit) != 0;
    }

    // Tempo used in the last block (changes with tempo automation). The transport advances with it.
    double currentBpm() const noexcept { return bpm; }
    // > 0: the tempo of an external MIDI clock LYNX follows (round 165f), over the song's tempo and its automation
    void setTempoOverride (double b) noexcept { tempoOverride = b; }

    // Events of the last block (for tests and diagnostics).
    const EventBuffer& lastEvents() const noexcept { return events; }
    // Song notes started and not ended yet (round 169d; tests and diagnostics)
    int scheduledNotesSounding() const noexcept { return scheduledNotes->sounding(); }

    // FL's "Compensate automations" (REA round 168j): a mixer effect whose input the delay compensation delays gets its
    // automation clips' value at its own, delayed, position (`current` = the value at the song position)
    float compensatedAutomation (const RackState& state, int automation, int track, int slot, float current) const noexcept;

private:
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters) noexcept;
    void renderChannel (const RackState& state, int channel, int n) noexcept;
    void processTrack (const RackState& state, int track, int n, MeterBank* meters) noexcept;
    void runSilencedTrack (const RackState& state, int track, int n, const MixerMatrix& level, MeterBank* meters, bool cutEffectSends) noexcept;
    void pullInputs (const RackState& state, int track, int n) noexcept;
    void updatePdcBypass (const RackState& state, bool playing) noexcept;
    void deliverToEffects (const RackState& state, const NoteEvent& e) noexcept; // (round 162)
    float* channelBuffer (int channel, int side) noexcept;
    float* trackScratch (int track, int side) noexcept;
    void runEffects (const RackState& state, int track, int n) noexcept;
    // one slot's process, with a moved one-knob slot's glide (round 190dg)
    void runGoalSlot (const EffectSlot& slot, Effect& effect, std::size_t key, float* l, float* r, int n, const EffectParams& params,
                      double bpm) noexcept;
    void setSidechain (const RackState& state, const EffectSlot& slot, Effect& effect, int track) noexcept;
    void connect (const RackState& state, const SlotConnections& c, Effect& effect, int track, const float* l, const float* r) noexcept;
    void updateAutomation (const RackState& state, double startTick) noexcept;
    void updateModulators (const RackState& state, double tick, int numSamples) noexcept; // round 177b
    double modSeconds = 0.0; // the free-running modulators' clock
    // ticks from `tick` to the next pattern event (or pattern clip edge) that changes a lane's value; +inf if none
    static double ticksToNextEvent (const RackState& state, double tick) noexcept;
    // bindings from this automation slot on (andLive: and those of live controllers, the links)
    void applyPluginParams (const RackState& state, int firstSlot, bool andLive = false) noexcept;
    double compensateTick = -1.0;          // the song position of this block's automation (song mode), −1 = none
    double compensateTicksPerSample = 0.0; // > 0: compensation on in this block
    float automated (int automation, float base) const noexcept;
    float* trackBuffer (int track, int channel) noexcept;

    double sampleRate = 44100.0;
    double bpm = 120.0;
    double tempoOverride = 0.0;
    bool wasPlaying = false;
    double expectedTick = -1.0; // where the next block should start, to detect jumps
    bool resumeAudio = false;
    bool chaseNotes = false; // this block starts playback: notes crossing the position play (setTransportOptions)
    bool playTruncatedOnTransport = true, resetPluginsOnTransport = false;
    unsigned userJumpSerial = 0, seenUserJumpSerial = 0;
    const RackState* lastState = nullptr;
    std::uint64_t lastSerial = 0;
    std::vector<VoiceOrder> voiceOrderScratch;   // (round 168s: moving the note state with moved channels)
    std::vector<NoteMask> heldScratch;
    EventBuffer events;
    std::vector<float> mixBuffers; // maxMixerTracks * 2 * blockCapacity
    std::array<float, RackState::maxAutomations> automationValues {}; // NaN = not evaluated yet
    // the pitch bend an automation / event lane last gave each LYNX instrument (0..1; NaN = none yet; issue #12)
    std::array<float, maxChannels> automatedBend {};
    bool automatedBendReset = false;
    std::array<std::atomic<std::uint32_t>, RackState::maxAutomations / 32> overridden {}; // touched while recording
    std::atomic<bool> anyOverridden { false };
    std::array<std::atomic<std::uint32_t>, RackState::maxAutomations / 32> released {}; // moved by the user (issue #125)
    std::atomic<bool> anyReleased { false };
    bool releaseWasPlaying = false; // (audio thread) the last chunk played: a start clears `released`
    void clearOverriddenValues() noexcept;
    std::array<MixerMatrix, maxMixerTracks> lastTrackGain {}; // the level matrix of the previous block, for the ramp
    // round 168x: the "Post level and panning" recording tap's own ramp (the insert's level matrix without the mute)
    std::array<MixerMatrix, maxMixerTracks> lastTapGain {};
    // issue #144: the fader ramp of a silenced insert's meter (NaN: none yet)
    std::array<MixerMatrix, maxMixerTracks> lastMeterGain {};
    std::array<char, maxMixerTracks> trackActive {};        // the track output has sound in this block
    std::vector<float> tapBuffers;                          // RackState::maxSendTaps * 2 * chunkCapacity
    std::array<char, RackState::maxSendTaps> tapActive {};  // the Fruity Send slot sent something this block
    // round 190aq: the mute ramp of each insert's pre-fader send signal (1 = heard, 0 = muted; NaN: none yet)
    std::array<float, maxMixerTracks> lastPreGain {};
    // round 190aq: slots skipped for low latency last block; back in the chain they start from a reset state
    // (their look-ahead buffers hold what played before the bypass)
    std::vector<char> lowLatencySkipped; // maxMixerTracks * maxEffectSlots
    // round 190dg: each one-knob slot's knob (core/GoalKnobs.h) as this renderer last ran it, and the effect it ran it
    // on. A knob turned since glides there over the block in steps of `goalGlideStep` samples, so a dragged knob doesn't
    // step its effect's parameters once per edit (a click on a held tone). NaN / nullptr: not run yet.
    std::vector<float> goalGlideAmount;           // maxMixerTracks * maxEffectSlots
    std::vector<const Effect*> goalGlideEffect;   // maxMixerTracks * maxEffectSlots
    static constexpr int goalGlideStep = 8;
    // the fastest a knob or a Blend moves on the audio side: the whole range in this many seconds (a jump, e.g. a double
    // click back to the default, becomes a short glide)
    static constexpr double fullTurnSeconds = 0.25;
    float* tapBuffer (int tap, int side) noexcept;
    std::array<char, maxChannels> channelSilent {};          // channel skipped in this chunk (idle, no notes)
    std::array<NoteMask, maxChannels> heldNotes {};          // keys held on each channel (effects that follow notes: LYNX Tune)
    std::array<std::array<std::atomic<std::uint64_t>, 2>, maxChannels> sounding {};
    std::array<NoteMask, maxChannels> struckNotes {};        // keys started in this block (a note shorter than a block still shows)
    std::array<NoteMask, maxChannels> publishedNotes {};     // what soundingKeys holds (stored only on a change)
    int publishedTop = 0;                                   // channels past it have nothing published
    void publishSoundingKeys (int channels) noexcept;
    std::array<VoiceOrder, maxChannels> voiceOrders {};      // sounding notes in order, for the channels' Max polyphony
    // the song's started notes and their ends (round 169d): an edit while playing can't leave one without its "off"
    std::unique_ptr<ScheduledNotes> scheduledNotes = std::make_unique<ScheduledNotes>();
    // FL's channel Mono / Porta in front of every instrument (round 168l, core/ChannelPoly.h): one per channel (prepare)
    std::vector<channelpoly::Layer> polyLayers;
    std::vector<channelpoly::Layer> polyScratch; // (round 168s: channels moved, see process)
    channelpoly::Mode polyMode (const ChannelDef& c, const Instrument& instrument) const noexcept;
    void clearPolyLayers() noexcept;
    static constexpr int chunkCapacity = 512;               // chunks processed at once
    std::vector<float> channelBuffers;                      // maxChannels * 2 * chunkCapacity
    std::vector<float> trackScratchBuffers;                 // maxMixerTracks * 2 * chunkCapacity
    double chunkStartTick = 0.0, chunkTps = 1.0;            // of the chunk being rendered (for the arpeggiator)
    // live-played notes of channels with an arpeggiator, echo delay or key / velocity tracking go through the
    // channel's note path like the scheduled ones (FL, round 149); while stopped the arpeggiator runs on its own clock
    std::array<NoteEvent, 256> liveRouted {};
    // the VFX channels' processors run on the chunk's events before the channels render (round 164b)
    void runVfxChannels (const RackState& state, bool playing, int numSamples) noexcept;
    std::unique_ptr<vfx::EventList> vfxIn = std::make_unique<vfx::EventList>(), vfxOut = std::make_unique<vfx::EventList>();
    int liveRoutedCount = 0;
    double liveTick = 0.0;
    bool routesLiveNotes (const RackState& state, int channel) const noexcept;
    // Layer channels (round 164): each note event of a Layer is copied to the children it plays (core/Layer.h). The
    // child a Random / Sequential note picked is kept per Layer and key so its "off" reaches the same child.
    void expandLayers (const RackState& state) noexcept;
    std::vector<std::int16_t> layerPick; // [channel * 128 + key]: the child, −1 none (maxChannels × 128)
    std::array<int, maxChannels> layerTurn {};
    std::uint32_t layerRandom = 0x2545F491u;
    void applyLiveControl (const RackState& state, const NoteEvent& e) noexcept;
    WorkerPool* workers = nullptr;
    LiveInput* liveInput = nullptr;
    HardwareOutput* hwOut = nullptr;
    perf::Live* perfLive = nullptr;
    perf::Player perfPlayer;
    double perfClock = 0.0;      // the Player's steady clock (ticks): the transport's position, never wrapping
    bool perfRunning = false;
    bool launchersRunning = false;                 // the launchers' branch of the block is on (Performance mode, the grid)
    launcher::Runtime* runningLauncher = nullptr;  // the grid's runtime while it plays
    std::vector<float> hwScratch; // 2 × blockCapacity
    void mixHardwareOutputs (const RackState& state, int n) noexcept;
    int liveInputOffset = 0; // of the chunk within the process() block
    const float* clickLeft = nullptr;
    const float* clickRight = nullptr;
    int clickTrack = 0;
    const float* sideLeft = nullptr;
    const float* sideRight = nullptr;
    int sideTrack = -1;
    const EffectParams* overrideParams = nullptr;
    int overrideTrack = -1, overrideSlot = -1;
    int metronomeTarget (const RackState& state) const noexcept;
    void mixMetronome (const RackState& state, int track, int n) noexcept;
    void mixSideInput (const RackState& state, int track, int n) noexcept;
    void mixLiveInput (const RackState& state, bool playing, int n) noexcept;
    void mixLiveInputTrack (const RackState& state, int track, bool recording, bool playing, int n) noexcept;
    // the task graph run (RackState::graphOk): per chunk, each task's unfinished predecessors and the ready queue
    // (positions are taken in order by the pool's indices; a task is written to the next free position when its last
    // predecessor finishes)
    static constexpr int graphCapacity = maxChannels + maxMixerTracks;
    std::unique_ptr<std::atomic<int>[]> graphPending = std::make_unique<std::atomic<int>[]> (graphCapacity);
    std::unique_ptr<std::atomic<int>[]> graphSlots = std::make_unique<std::atomic<int>[]> (graphCapacity);
    std::atomic<int> graphPushed { 0 };
    bool runTaskGraph (const RackState& state, bool playing, int channels, int numSamples, MeterBank* meters) noexcept;
    void sumChannelsInto (const RackState& state, int track, int channels, int n) noexcept;
    std::atomic<bool> profiling { false };
    std::unique_ptr<UnitTimes> units = std::make_unique<UnitTimes>();
    std::unique_ptr<ProcessTimes> times = std::make_unique<ProcessTimes>();

    struct SmartDisableState
    {
        const Effect* effect = nullptr;
        int silentSamples = 0;
    };
    std::vector<SmartDisableState> smartDisable; // [track * maxEffectSlots + slot]

    struct TrackEqState
    {
        TrackEq settings;
        std::array<Biquad, 3> bands;
        bool running = false;
    };
    std::vector<TrackEqState> trackEqs; // built-in EQ of each track
    double chunkTick = 0.0;             // start of the block (for the global LFOs)
    bool chunkPlaying = false;          // the song plays during the block (effects that follow the transport)
    bool pdcBypassNow = false;          // the "When recording or not playing" tracks skip their PDC delays this chunk
    bool pdcBypassBefore = false;       // ... and did in the previous chunk (back to compensated: their lines are emptied)
    void applyTrackEq (const MixerTrack& track, int t, bool bypassed, float* l, float* r, int n) noexcept;
    static constexpr float silenceLevel = 1.5849e-5f;  // -96 dBFS
    int blockCapacity = 0;
};
} // namespace lynx
