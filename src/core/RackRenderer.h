#pragma once

// Ties Song + instruments + scheduler together: given the transport state, renders
// a block of stereo audio with notes triggered at the exact sample.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>

#include "core/Arpeggiator.h"
#include "core/LinkFormula.h"
#include "core/ChannelMiscFx.h"
#include "core/ChannelPoly.h"
#include "core/VoiceLimit.h"
#include "core/Biquad.h"
#include "core/Effects.h"
#include "core/HardwareOutput.h"
#include "core/Instruments.h"
#include "core/TimeStretch.h"
#include "core/LiveEventQueue.h"
#include "core/SnapshotExchange.h"
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
// Issue #187 (round 190ik): warped samples follow the playing tempo (Instrument::setTempoRatio, every block). The switch
// exists for the tests' A/B (off = the renderer before 190ik); it is always on in the app.
namespace tempofollow
{
bool enabled() noexcept;
void setEnabled (bool yes) noexcept;
} // namespace tempofollow

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
    std::atomic<float> crossfader { 0.0f };   // round 190kc: Live's crossfader position (-1..1)
};

// Immutable snapshot published by the UI. Instruments are shared between
// consecutive snapshots so voices are not cut off when the song is edited.
struct RackState
{
    Song song;
    std::vector<std::shared_ptr<Instrument>> instruments; // one per channel
    std::vector<std::vector<std::shared_ptr<Effect>>> effects; // [mixer track][slot]
    double sampleRate = 44100.0; // rate the new instruments were prepared with
    // Round 190gn (A1): the keep-pitch samplers' stock of fresh pitch shifters (shared with the racks built from this one)
    std::shared_ptr<ShifterStock> shifterStock;
    // Round 168s (issue #32): this snapshot's number, the one it was built from, and for each channel the channel of
    // that snapshot it is (−1 = new): the renderer moves its per-channel note state along when channels moved
    std::uint64_t serial = 0, builtFrom = 0;
    std::vector<int> channelFrom;
    bool channelsMoved = false;

    // Automation, preprocessed so the audio thread only does table lookups.
    // Indices into song.automations, or -1 = no automation.
    static constexpr int maxAutomations = 1024;
    std::vector<std::vector<int>> automationClips; // per automation: clips in the Playlist
    // round 190ow (details DE-123): FL's triggered automation clips. Per automation, the clips its steps / notes on its
    // Channel rack row start in Song mode (Pattern::autoNotes inside the Playlist's pattern clips), as clips of its own in
    // song time: start = the note's song position, length = how long the note holds the clip, offset 0. Empty = none
    // (every song without such steps: no work anywhere). `triggerSlot[a]` = the slot that carries automation a's value
    // (its own, or the group's when it shares its target, issue #97); `patternTriggers` = some pattern has such steps
    // (Pattern mode plays the current pattern's).
    std::vector<std::vector<Clip>> triggerClips;
    std::vector<int> triggerSlot;
    std::vector<char> triggered; // per automation: steps / notes in some pattern (bound for Pattern mode too)
    bool patternTriggers = false;
    // the automation's value follows the song position: clips in the Playlist or triggered ones
    bool positional (int a) const noexcept
    {
        return a >= 0 && (std::size_t) a < automationClips.size()
               && (! automationClips[(std::size_t) a].empty() || ((std::size_t) a < triggerClips.size() && ! triggerClips[(std::size_t) a].empty()));
    }
    bool driven (int a) const noexcept { return positional (a) || (a >= 0 && (std::size_t) a < triggered.size() && triggered[(std::size_t) a] != 0); }
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
        bool hosted = false;        // round 190ox: a hosted plugin's parameter (base = ModRoute::base)
    };
    std::vector<ModPlay> modRoutes;
    // Round 190ox (modulators stage 2): per modulator, what feeds it — the follower's insert, the notes' channel (−1:
    // none) — and the slot of an automation that drives a Macro's knob (−1: its own value). Empty without modulators.
    struct ModFeed
    {
        int track = -1, channel = -1, macroSlot = -1;
    };
    std::vector<ModFeed> modFeeds;
    std::vector<char> followedTracks; // per insert: a follower reads its level (empty: none)
    bool modStateful = false;         // a follower, envelope, keytrack or velocity modulator routes somewhere
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
        int shared = -1; // round 190om: several patterns' lanes of one target — the slot bound to it (else −1)
    };
    std::vector<EventLane> eventLanes;
    // round 190om: the lanes (indices into eventLanes) of each target that several patterns' lanes drive, the bound slot
    // first; the pattern whose clip started last drives the target, as in FL (before, only the last lane did)
    std::vector<std::vector<int>> sharedLanes;
    // round 190gw: automation clips with several target links (FL's "Target links", Automation::links): each link takes
    // a slot after the event lanes whose value is the clip's own through the link's mapping formula
    // Round 190ov (FL's demos: clips linked to several controls): a link's target that other clips drive too plays them as
    // one, as issue #97's groups do — the clip that plays (the last one started) or else the last one that ended decides,
    // through its own link (`link` ≥ 0) or 1:1 (−1: an automation of that one target, which then binds nothing itself).
    struct LinkMember
    {
        int automation = 0, link = -1;
        linkformula::Formula formula;
        std::vector<float> scale;
    };
    struct LinkSlot
    {
        int automation = 0, slot = 0;
        linkformula::Formula formula;
        std::vector<float> scale;          // round 190ov: Automation::Link::scale
        std::vector<LinkMember> members;   // round 190ov: empty = this link alone, from its automation's value
    };
    std::vector<LinkSlot> linkSlots;
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
    int crossfaderAutomation = -1; // round 190kc: Live's crossfader automation, if any
    bool tempoMap = false;    // the tempo moves while playing (tempo automation or pattern Tempo events, core/TempoMap.h)

    // PDC: per-edge delays (nullptr = no delay) and the latencies used to build the plan.
    PdcPlan pdc;
    std::vector<std::shared_ptr<DelayLine>> channelDelays;
    std::vector<std::vector<std::shared_ptr<DelayLine>>> sendDelays; // [track][k] like PdcPlan::sendDelay
    std::vector<int> latencies; // channels, then slots of all tracks
    // Round 190hq (B13): a slot's side input (PdcPlan::sideDelay, k = 0…3) delayed to line up with the slot's own input,
    // into its own chunk buffer (the slot's effect reads it while it processes). nullptr = no delay.
    struct SideLine
    {
        static constexpr int capacity = 512; // (the renderer's chunk)
        std::shared_ptr<DelayLine> delay;
        std::array<float, 2 * capacity> out {};
    };
    std::vector<std::vector<std::array<std::shared_ptr<SideLine>, 4>>> sideLines; // [track][slot][k]

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
        bool cue = false; // round 190nc (DL-110): a cued track to the Cue Out, at HardwareOutput::cueGain
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
    int pdcCurrentSource = -1;                          // (round 190my: Factories::currentSource, for buildPdc)
    std::vector<lowlatency::SlotMask> lowLatencySlots;  // [track] bit s = slot s is bypassed
    // The latency of each bypassed slot when the plan was made ([track][slot], 0 elsewhere), for the UI.
    std::vector<std::vector<int>> lowLatencySaved;

    // Current latencies of instruments and effects, in the same layout as `latencies` (0 for the slots bypassed for
    // low latency).
    std::vector<int> currentLatencies() const;
    // round 190of: currentLatencies() != known, without making the list (the engine asks 30 times a second)
    bool latenciesDiffer (const std::vector<int>& known) const;
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
    // FL's Current (C) track (round 190mv, DF-82; MixerTrack::current): its index, -1 = none. It is not in mixerOrder:
    // the renderer runs its effects on the selected track's output (RackRenderer::setCurrentSource).
    int currentTrack = -1;
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
    // Round 190gq (REA 190hb, FL's mixer threading runs each serial chain of tracks as one unit): [task] the task run
    // right after it on the same thread (its only successor, which nothing else feeds), −1 none; those tasks are not
    // handed out on their own (graphFusedCount of them), so a chain costs no hand-off per link
    std::vector<int> graphFused;
    int graphFusedCount = 0;
    std::vector<std::vector<int>> trackChannels;      // [track] the channels playing into it, in channel order
    void buildMixerGraph();
    void applyVcas(); // VCA mutes into mixerAudible, the VCA chains (round 177a)
    void buildTaskGraph (const std::vector<int>& depth);

    void buildAutomationTables();
    void buildTriggerClips (int count); // (round 190ow, DE-123)
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
    // Hardware MIDI outputs (round 190jl, FL's Options › MIDI › Output "Port"): [channel] the port a MIDI Out channel
    // sends on, or a plugin's wrapper output port (−1 = none), and the channels that have one
    std::vector<int> hardwareMidiPort;
    std::vector<int> hardwareMidiChannels;
    void buildMidiRoutes();

    // Loads (or fetches from cache) a file's audio; nullptr on failure.
    using SampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path)>;

    // The app creates plugin instruments/effects (the core does not know JUCE). nullptr = failed
    // (missing plugin): the channel goes silent / the slot becomes bypass.
    using PluginInstrumentFactory = std::function<std::shared_ptr<Instrument> (const PluginRef&, double sampleRate)>;
    using PluginEffectFactory = std::function<std::shared_ptr<Effect> (const PluginRef&, double sampleRate)>;

    // Sample stretched without changing pitch (ratio = new / original duration); cached in the app.
    // `map`: the channel's warp map (core/TimeStretch warpStretchMap; empty = one ratio)
    // `options` (round 190fm): the warp mode's own settings (core/TimeStretch StretchOptions)
    using StretchedSampleProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, double ratio, StretchMethod method,
                                                                                     double semitones, const StretchMap& map, const StretchOptions& options)>;

    // A channel's precomputed span (its region after its precomputed effects and Trim) stretched by `ratio`, cached in the
    // app by (path, variantKey, ratio, method): FL stretches that span, not the whole file (round 180d). Unset: stretched
    // here, uncached (tests).
    using StretchedSpanProvider = std::function<std::shared_ptr<const SampleData> (const std::string& path, const std::string& variantKey,
                                                                                  const std::shared_ptr<const SampleData>& span, double ratio,
                                                                                  StretchMethod method, double semitones, const StretchOptions& options)>;

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
        // One publish may deliberately replace selected runtime identities (remote reset/replacement).
        // An empty set preserves the established positional fallback for ordinary project/UI edits.
        std::unordered_set<std::uint64_t> freshInstances;
        // round 190aq: the inserts whose latency-adding slots are bypassed (lowlatency::bypassTracks); empty = none
        std::vector<char> lowLatencyTracks;
        // Round 190my (DF-82): the mixer track FL's Current track runs on (the renderer's setCurrentSource), for the PDC
        // plan: its latency is added to that track's (−1 = none)
        int currentSource = -1;
        // Round 190jb (FL renders with the instances that play): an export's hosted plugin channels / slots and its
        // Patchers that host plugins take the unit these return (the live engine's running one); nullptr or unset: built
        // new as usual
        std::function<std::shared_ptr<Instrument> (const ChannelDef&, double sampleRate)> liveInstrument;
        std::function<std::shared_ptr<Effect> (const EffectSlot&, double sampleRate)> liveEffect;
        // Round 190kx (core/AraLink.h): an audio clip channel edited in an ARA plugin takes the instrument this returns (the
        // plugin's playback renderer, engine/AraHost); nullptr or unset: its clips play through its sampler as usual
        std::function<std::shared_ptr<Instrument> (const ChannelDef&, double sampleRate)> araInstrument;
    };

    // Round 190gp: what the renderer's effect loop needs of each slot, worked out once per snapshot ([track][slot], one
    // per slot with an effect): a few bytes in a row instead of each EffectSlot (about 1 KB) and its automation table
    // (768 bytes) read at every block, most of the effect loop's own cost in a big project (cache misses).
    struct SlotPlan
    {
        EffectKind kind {};
        bool active = false;        // Song::slotActive
        bool smartEligible = false; // smart disable may skip it (everything but FL's global switch, which is read live)
        bool longHold = false;      // a plugin or a Patcher: 4 s of silence before smart disable, else 1 s
        bool liveMeters = false;    // an editor shows its controlOutput()
        bool isPlugin = false;
        bool routed = false;        // a hosted effect with routed Connections (slotConnections)
        bool sends = false;         // it sends into its own buffers (Fruity Send, Patcher, Transmitter, Transporter, Equo, routed)
        bool hasMixAuto = false;    // effectMixAuto has its entry (mixAuto)
        bool tailHold = false;      // smart disable may take the effect's declared tail (a built-in with no side input)
        int mixAuto = -1;
        float mix = 1.0f;
        int used = 0;               // the parameters its kind has (and the slot holds)
        int firstAutomated = 0;     // its first automated parameter (used = none)
    };
    std::vector<std::vector<SlotPlan>> slotPlans;
    static SlotPlan planSlot (const RackState& state, int track, std::size_t slot) noexcept;
    void buildSlotPlans();

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
    // Round 190of (the monitor's Longest and Spikes columns): per instrument / slot, the longest single call since the
    // monitor last took it (exchange (0)), and the calls that took more than half of the time their own audio lasts
    // (one plugin alone using half of the real-time budget: what makes the sound stutter), counted up
    std::array<std::atomic<std::uint32_t>, channels> channelLongestNs {};
    std::array<std::atomic<std::uint32_t>, maxMixerTracks * maxEffectSlots> effectLongestNs {};
    std::array<std::atomic<std::uint32_t>, channels> channelSpikes {};
    std::array<std::atomic<std::uint32_t>, maxMixerTracks * maxEffectSlots> effectSpikes {};
    // one timed call of an instrument (channel = true, key = its channel) or effect slot (key = track * slots + slot):
    // `spent` ns for `audio` ns of sound
    void addCall (bool channel, std::size_t key, std::uint64_t spent, std::uint64_t audio) noexcept
    {
        if (channel ? key >= channelNs.size() : key >= effectNs.size())
            return;
        (channel ? channelNs[key] : effectNs[key]).fetch_add (spent, std::memory_order_relaxed);
        auto& longest = channel ? channelLongestNs[key] : effectLongestNs[key];
        const auto value = (std::uint32_t) std::min<std::uint64_t> (spent, 0xffffffffu);
        auto seen = longest.load (std::memory_order_relaxed);
        while (value > seen && ! longest.compare_exchange_weak (seen, value, std::memory_order_relaxed))
        {
        }
        if (audio > 0 && spent * 2 > audio)
            (channel ? channelSpikes[key] : effectSpikes[key]).fetch_add (1, std::memory_order_relaxed);
    }
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
    // Round 190gp: the editors showing a slot's live meters (effectMeters) count themselves here; the renderer tells the
    // slot's effect before each block (Effect::setMetersShown), so a costly meter runs only while one is open.
    std::array<std::atomic<std::uint16_t>, (std::size_t) maxMixerTracks * maxEffectSlots> metersWanted {};
    std::array<std::atomic<float>, (std::size_t) maxMixerTracks * maxEffectSlots * 2> slotLevels {};
    // round 190lr (DL-87, Live's stereo meter after each device): the same peaks per side, [key * 2 + side], input and output
    std::array<std::atomic<float>, (std::size_t) maxMixerTracks * maxEffectSlots * 2> slotInputSides {}, slotOutputSides {};

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
    // tests (round 190mx): the value automation slot `slot` had in the last block (NaN: none; the first piece's)
    float automationValueForCheck (int slot) const noexcept;

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

    // Round 190gj (P9, FL's "Split mixer tracks" in one render): while set, every process() block copies the post-fader
    // output of each `wanted` insert, and the Master's own input before its routes arrive (its channels: index 0), into
    // `taps` at its place in the block. A part where an insert stays inactive (silent, idle) is left as it was: the
    // export clears the taps before each block. Same thread as process(); set off playback (an offline render).
    struct RenderTaps
    {
        int capacity = 0;                     // samples per block
        std::vector<char> wanted;             // [track]
        std::vector<std::vector<float>> data; // [track]: left (capacity samples) then right
        bool wants (int t) const noexcept { return t >= 0 && (std::size_t) t < wanted.size() && wanted[(std::size_t) t] != 0; }
        float* left (int t) noexcept { return data[(std::size_t) t].data(); }
        float* right (int t) noexcept { return data[(std::size_t) t].data() + capacity; }
    };
    void setRenderTaps (RenderTaps* taps) noexcept { renderTaps = taps; }

    // Allocates the mixer buffers: call off the audio thread.
    // Blocks larger than maxBlockSize are processed in pieces.
    void prepare (double sampleRate, int maxBlockSize = 2048);
    // FL's native generators, whose slide notes the host moves in steps on FL's ticks (round 190jv, core/HostSlide.h Steps)
    static bool stepsSlides (InstrumentKind kind) noexcept;

    // Round 190gn (A6, memory): the channel and mixer buffers are sized by the songs played, not for 1 024 channels and
    // 501 inserts (~8 MB, all touched). Off the audio thread, before `state` reaches process(): grows them (in steps of
    // 32) when the state has more channels or inserts than they hold; the audio thread swaps to the new ones at its
    // next block (never smaller than the state it plays). A renderer nobody calls this on (tools, tests, offline
    // renders) grows them on the rendering thread instead.
    void reserveFor (const RackState& state);
    // the channels / inserts the buffers hold now (as last reserved or prepared)
    int bufferChannels() const noexcept { return reservedChannels.load (std::memory_order_relaxed); }
    int bufferTracks() const noexcept { return reservedTracks.load (std::memory_order_relaxed); }
    static constexpr int minBufferChannels = 64, minBufferTracks = 128;

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
        // round 190gg: the slowest channel / effect call since the audio callback last took it (core/Dropouts.h pack())
        std::atomic<std::uint64_t> blockSlowest { 0 };
    };
    // The keys sounding on a channel (played by the song / pattern, the keyboard, MIDI inputs), for the Piano roll's
    // keyboard (issue #14: FL lights them); published after each block, safe to read from any thread.
    NoteMask soundingKeys (int channel) const noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return {};
        return { sounding[(std::size_t) channel][0].load (std::memory_order_relaxed), sounding[(std::size_t) channel][1].load (std::memory_order_relaxed) };
    }
    // Round 190ih (FL's channel activity light, PLAN R4): how many blocks struck a note on the channel (any source: song,
    // pattern, typing keyboard, MIDI, Piano roll preview, arpeggiator); a change since the last look = a new note-on,
    // even one shorter than the UI's frame. Safe to read from any thread.
    // Round 190oa (DF-29, owner decision F5; REA 190oa R1: FL's polyphony readout is the count of its voice pool's lists —
    // generator voices kept until their release ends, audio clips, automation clips, plugin notes): the notes sounding on
    // every channel plus the ones released whose channel still sounds (its release tail), as of the last block. Any thread.
    int voicesSounding() const noexcept { return voicesNow.load (std::memory_order_relaxed); }
    std::uint32_t noteStrikes (int channel) const noexcept
    {
        return channel >= 0 && channel < maxChannels ? strikes[(std::size_t) channel].load (std::memory_order_relaxed) : 0u;
    }
    void setProfiling (bool on) noexcept { profiling.store (on, std::memory_order_relaxed); }
    bool isProfiling() const noexcept { return profiling.load (std::memory_order_relaxed); }
    const UnitTimes& unitTimes() const noexcept { return *units; }
    // Round 190gg: the slowest channel / effect call since the last take (core/Dropouts.h pack(); 0 = none). Audio thread,
    // once per device callback.
    std::uint64_t takeBlockSlowest() noexcept { return units->blockSlowest.exchange (0, std::memory_order_relaxed); }

    // ---- Render-ahead (round 190gq, core/RenderAhead.h) ----------------------------------------------------------
    // Two renderers can share one rack, each channel and insert rendered by exactly one of them: the live one at the
    // device block, the other a few blocks ahead on its own thread. A Split names the other renderer's units and the
    // boundary: the units of the part rendered ahead that a live insert takes (a channel or an insert routed into it,
    // a send tap, a side-chain or Connections source). The renderer that renders ahead writes them into a SplitBlock
    // chunk by chunk; the live one reads them where the unit would have run. The same samples either way.
    struct Split
    {
        static constexpr int maxSlots = 256;
        enum class Kind : std::uint8_t { channel, track, tap };
        std::array<char, maxChannels> foreignChannel {};       // 1 = the other renderer renders this channel
        std::array<char, maxMixerTracks> foreignTrack {};      // ... this insert (0 = the Master)
        std::array<std::int16_t, maxChannels> channelSlot {};  // its boundary slot, −1 = none
        std::array<std::int16_t, maxMixerTracks> trackSlot {};
        std::array<std::int16_t, RackState::maxSendTaps> tapSlot {};
        std::array<Kind, maxSlots> slotKind {};
        std::array<std::int16_t, maxSlots> slotUnit {};
        int slots = 0;
        bool exports = false; // true: this renderer writes the boundary (it renders ahead); false: it reads it
        void clear() noexcept
        {
            foreignChannel.fill (0);
            foreignTrack.fill (0);
            channelSlot.fill (-1);
            trackSlot.fill (-1);
            tapSlot.fill (-1);
            slots = 0;
            exports = false;
        }
    };
    struct SplitBlock
    {
        static constexpr int maxChunks = 64;
        float* audio = nullptr;        // slot s, side d: audio + (s * 2 + d) * stride, each chunk at its offset in the block
        int stride = 0;
        std::uint8_t* flags = nullptr; // [chunk * Split::maxSlots + slot]: the channel's channelSilent, the insert's or tap's activity
        int chunks = 0;
        std::array<int, maxChunks> chunkOffset {};
    };
    // Same thread as process(); nullptr = this renderer renders everything (the default)
    void setSplit (const Split* s) noexcept { split = s; }
    void setSplitBlock (SplitBlock* block) noexcept { splitBlock = block; }
    // the play head this renderer's hosted plugins see (core/Instruments.h), nullptr = the shared one (the song position)
    void setHostTimeline (HostTimeline* timeline) noexcept { ownTimeline = timeline; }
    // the Plugin performance monitor's and the profiler's counters are `other`'s: both renderers of one rack add their
    // units into them, and only `other` counts the audio's duration
    void shareTimesWith (RackRenderer& other) noexcept;
    // Moving units between the two renderers (at a block boundary, both idle): the global state — transport, the
    // started notes, automation values, the live clocks — and each flagged unit's own state (fader ramps, the built-in
    // EQ, smart disable, one-knob glides, held keys, voice order, Mono layers, Layer picks). Any per-unit state added to
    // the renderer must be copied in adoptUnits too.
    void adoptGlobals (const RackRenderer& from) noexcept;
    void adoptUnits (const RackRenderer& from, const char* channels, const char* tracks) noexcept;
    // (round 190ql) the render-ahead's renderer gets the per-channel note blocks this one makes (off the audio thread),
    // so adoptUnits finds them on both sides; null: none. Set while neither renders (the render-ahead's lifetime).
    void setPropsMirror (RackRenderer* mirror);
    // these automations' values (controllers: their value comes from a unit's output), or all of them (nullptr)
    void adoptAutomationValues (const RackRenderer& from, const int* automations, int count) noexcept;
    // the flagged channels' voices end now and their note state is cleared (what render-ahead played for them was dropped)
    void silenceChannels (const RackState& state, const char* channels) noexcept;
    // the snapshot it last rendered is gone: not looked at again (prepare)
    void forgetState() noexcept { lastState = nullptr; }
    // chunks whose boundary did not line up with the part rendered ahead (the live renderer; diagnostics)
    std::uint64_t splitMismatchCount() const noexcept { return splitMismatches; }
    // Round 190kz (sched): blocks rendered batched (every piece's serial part first, the units' pieces in one task-graph
    // round: processBatched), and how many of them did go to the workers in that one round (diagnostics, A/B)
    std::uint64_t batchedBlockCount() const noexcept { return batchedBlocks; }
    std::uint64_t batchedRoundCount() const noexcept { return batchedRounds; }
    // Round 190lv (sched2): batched blocks of a project with live controllers, run as two rounds (the controllers' sources
    // and what feeds them, then the rest; processBatched's liveBatch)
    std::uint64_t batchedLiveRoundCount() const noexcept { return batchedLiveRounds; }
    // ... and why the last state's live controllers could not be ("" = they can, or none were looked at)
    const char* livePlanRefusal() const noexcept { return livePlanWhy; }
    // ... and the blocks that could not be, by the first reason found (every block is looked at, one cut in a single
    // piece too: that one runs one round anyway). Audio thread counters, read them between blocks (diagnostics).
    enum class BatchRefusal : int
    {
        switchedOff,      // LYNX_POOL rounds=chunk
        noGraph,          // no worker pool or no task graph (render-ahead's split, FL's CPU options, extra outputs...)
        bigBlock,         // more samples than one mixer buffer holds
        liveControllers,  // a controller's output (Peak / Formula / envelope controllers...) drives the next piece's values
        midiBetweenUnits, // MIDI from one unit to another between pieces (wrapper output ports, VFX, Dashboards, MIDI routes
                          // to mixer slots, hardware MIDI)
        launchers,        // Performance mode or the clip grid
        bindings,         // a plugin parameter binding reaching another unit
        heldNotes,        // an effect that follows the Channel rack's held notes (its settings, or automated ones)
        timedParams,      // sample-accurate plugin parameters
        manyPieces,       // more than maxPieces pieces (pattern events close together)
        other,            // FL's "When recording or not playing" PDC switch
        count
    };
    std::uint64_t batchRefusedCount (BatchRefusal why) const noexcept { return batchRefused[(std::size_t) why]; }
    // A7's measure of the last chunk: its units' summed work as a share of the chunk's real time (0 when not measured,
    // e.g. without workers). Render-ahead skips its probe when one thread could not carry the ahead part anyway.
    double lastWorkShare() const noexcept { return lastWorkShareValue; }
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
    // round 190oa (DL-58): Live's override — the released slots stay released when playback starts (FL: they go back)
    void setKeepReleasedOnPlay (bool keep) noexcept { keepReleasedOnPlay.store (keep, std::memory_order_relaxed); }
    bool isSlotOverridden (int slot) const noexcept
    {
        if (slot < 0 || slot >= RackState::maxAutomations)
            return false;
        const auto bit = 1u << ((unsigned) slot % 32);
        return ((overridden[(std::size_t) slot / 32].load (std::memory_order_relaxed) | released[(std::size_t) slot / 32].load (std::memory_order_relaxed)) & bit) != 0;
    }

    // Tempo used in the last block (changes with tempo automation). The transport advances with it.
    double currentBpm() const noexcept { return bpm; }
    // The song's own tempo in the last block (its tempo, automation and tempo events), with or without an override
    // (round 190gl: the Link session proposes it to its peers)
    double songBpm() const noexcept { return ownBpm; }
    // > 0: the tempo of an external MIDI clock LYNX follows (round 165f), over the song's tempo and its automation
    void setTempoOverride (double b) noexcept { tempoOverride = b; }
    // FL's tempo menu "Half speed" (round 190mp, DF-16; capture 190mp A4): 0.5 plays everything at half the tempo it has
    // (automation and an override included) while the tempo shown stays; 1 = normal. Live playback only.
    void setPlaySpeed (double s) noexcept { playSpeed = s > 0.0 ? s : 1.0; }

    // ---- Hardware MIDI outputs (round 190jl) ------------------------------------------------------------------------
    // The ports the output devices have (FL's Options › MIDI › Output "Port"; a 256-bit set, any thread; none = off). A
    // MIDI Out channel on one of them (or a plugin whose wrapper output port it is) sends what it plays: its notes, the
    // MIDI its knobs, patch, bank, pan / volume / pitch send, and what a MIDI input plays into it. Only the renderer the
    // engine plays live sets it (not an export's, not the one rendering ahead: core/RenderAhead.h keeps those channels live).
    struct HardwareMidiEvent
    {
        std::int32_t sample = 0; // in the last process() block
        std::uint8_t port = 0, size = 0, source = 0; // source: Instrument::HardwareMidi::source
        std::array<std::uint8_t, 3> bytes {};
    };
    void setHardwareMidiPorts (const std::array<std::uint64_t, 4>& ports) noexcept;
    // FL's Current (C) track (round 190mv, DF-82): the mixer track whose output it takes in line (the selected one; −1 =
    // none). The live engine sets it from the Mixer's selection; an export engine sets the Master (round 190my: FL's
    // render window selects the Master, live check 190my A2).
    void setCurrentSource (int track) noexcept { currentSource.store (track, std::memory_order_relaxed); }
    bool hardwareMidiPort (int port) const noexcept
    {
        return port >= 0 && port < 256 && (hwPorts[(std::size_t) port >> 6].load (std::memory_order_relaxed) >> (port & 63) & 1u) != 0;
    }
    // audio thread, after process(): the messages of the block, in time order (they are then gone)
    int takeHardwareMidi (HardwareMidiEvent* out, int max) noexcept;
    static constexpr int hardwareMidiCapacity = 2048;

    // GitHub #198 (round 190ks): the notes hosted plugins play from their own window (Instrument::takeOwnNotes) on the
    // channels the engine marks (UI thread: a bit per channel, those whose plugin window is open while recording is armed).
    // Only the renderer the engine plays live gets the marks (the one rendering ahead never does: open editors' channels
    // render live). After process(): the notes of the block in time order (then gone), `sample` in the process() block.
    struct OwnNoteEvent
    {
        std::int32_t sample = 0;
        std::int32_t channel = 0;
        std::uint8_t key = 60, midiChannel = 0;
        float velocity = 0.0f; // 0 = note off
    };
    static constexpr int ownNoteWords = maxChannels / 64;
    void setOwnNoteChannels (const std::array<std::uint64_t, ownNoteWords>& channels) noexcept;
    int takeOwnNotes (OwnNoteEvent* out, int max) noexcept;
    static constexpr int ownNoteCapacity = 512;

    // Events of the last block (for tests and diagnostics).
    const EventBuffer& lastEvents() const noexcept { return events; }
    // Song notes started and not ended yet (round 169d; tests and diagnostics)
    int scheduledNotesSounding() const noexcept { return scheduledNotes->sounding(); }

    // FL's "Compensate automations" (REA round 168j): a mixer effect whose input the delay compensation delays gets its
    // automation clips' value at its own, delayed, position (`current` = the value at the song position)
    float compensatedAutomation (const RackState& state, int automation, int track, int slot, float current) const noexcept;
    // Round 190hq (Song::compensateMixerLevels): `automated (automation, base)` for a level that acts on insert
    // `track`'s audio after its effects (fader, pan, a route's level, a VCA fader over it), taken as late as that audio.
    float automatedLevel (const RackState& state, int automation, float base, int track) const noexcept;

private:
    // ---- Round 190kz (sched): one task-graph round per device callback ----------------------------------------------
    // A block cut in pieces (the automation's 128-sample grid, pattern events) used to run one worker-pool round per
    // piece. A batched block (processBatched) runs every piece's serial part first, then ONE round in which each channel
    // and insert renders its pieces one after another, then the Master's pieces: each unit gets exactly the calls, sizes,
    // values and order it got before (the same samples), but the workers are woken and joined once per block.
    // What a piece's units read of the renderer's per-chunk state lives in one slot per piece: the thread's pieceSlot()
    // (0 everywhere except inside a batched block) picks it. PieceValue / PieceArray / PieceEvents behave as the plain
    // member they replace for code that never sets the slot.
    static constexpr int maxPieces = 8;
    static int& pieceSlot() noexcept
    {
        thread_local int slot = 0;
        return slot;
    }
    template <typename T>
    struct PieceValue
    {
        explicit PieceValue (T init) noexcept { v.fill (init); }
        std::array<T, maxPieces> v;
        T& get() noexcept { return v[(std::size_t) pieceSlot()]; }
        const T& get() const noexcept { return v[(std::size_t) pieceSlot()]; }
        operator T() const noexcept { return get(); }
        PieceValue& operator= (const T& x) noexcept
        {
            get() = x;
            return *this;
        }
        PieceValue& operator+= (const T& x) noexcept
        {
            get() += x;
            return *this;
        }
        void carry (int from, int to) noexcept { v[(std::size_t) to] = v[(std::size_t) from]; }
    };
    template <typename T, std::size_t N>
    struct PieceArray
    {
        std::array<std::array<T, N>, maxPieces> v {};
        std::array<T, N>& get() noexcept { return v[(std::size_t) pieceSlot()]; }
        const std::array<T, N>& get() const noexcept { return v[(std::size_t) pieceSlot()]; }
        T& operator[] (std::size_t i) noexcept { return get()[i]; }
        const T& operator[] (std::size_t i) const noexcept { return get()[i]; }
        T* data() noexcept { return get().data(); }
        static constexpr std::size_t size() noexcept { return N; }
        void fill (const T& x) noexcept { get().fill (x); }
        // (the plain array's whole-value copy, as adoptGlobals makes it: this slot from the other's same slot)
        PieceArray& operator= (const PieceArray& o) noexcept
        {
            get() = o.get();
            return *this;
        }
        void carry (int from, int to) noexcept { v[(std::size_t) to] = v[(std::size_t) from]; }
    };
    struct PieceEvents
    {
        EventBuffer first;
        std::unique_ptr<EventBuffer[]> more = std::make_unique<EventBuffer[]> ((std::size_t) maxPieces - 1);
        EventBuffer& get() noexcept { const auto s = pieceSlot(); return s == 0 ? first : more[(std::size_t) s - 1]; }
        const EventBuffer& get() const noexcept { const auto s = pieceSlot(); return s == 0 ? first : more[(std::size_t) s - 1]; }
        operator EventBuffer&() noexcept { return get(); }
        operator const EventBuffer&() const noexcept { return get(); }
        void clear() noexcept { get().clear(); }
        void add (const NoteEvent& e) noexcept { get().add (e); }
        void sortByOffset() noexcept { get().sortByOffset(); }
        int size() const noexcept { return get().size(); }
        void resize (int n) noexcept { get().resize (n); }
        int droppedCount() const noexcept { return get().droppedCount(); }
        const NoteEvent& operator[] (int i) const noexcept { return get()[i]; }
        NoteEvent& at (int i) noexcept { return get().at (i); }
        const NoteEvent* begin() const noexcept { return get().begin(); }
        const NoteEvent* end() const noexcept { return get().end(); }
    };
    // a scope in which this thread works on piece `slot` of a batched block
    struct ScopedPiece
    {
        explicit ScopedPiece (int slot) noexcept : previous (pieceSlot()) { pieceSlot() = slot; }
        ~ScopedPiece() { pieceSlot() = previous; }
        ScopedPiece (const ScopedPiece&) = delete;
        ScopedPiece& operator= (const ScopedPiece&) = delete;
        const int previous;
    };
    // a piece of a batched block: its samples, where it starts in the block, its tick, whether it goes on from the
    // previous block (automation grid), its place on the export grid
    struct Piece
    {
        int samples = 0, offset = 0;
        double startTick = 0.0;
        bool continuing = false;
        std::int64_t gridPos = -1;
        bool live = false, recording = false;     // (the live input, as runTaskGraph reads it per chunk)
        bool params = false, paramsLive = false;  // updateAutomation's applyPluginParams, deferred to the units
        int paramsFirst = 0;
        bool liveDeferred = false; // (round 190lv) updateAutomation's live loop left a placeholder: the source writes it
    };
    std::array<Piece, maxPieces> pieces {};
    int pieceCount = 0;
    bool deferParams = false; // (processBatched's serial parts: applyPluginParams is recorded in the piece, not run)
    PieceValue<int> pieceOffset { 0 }; // where the slot's piece starts in the units' buffers
    std::array<HostTimeline, maxPieces> pieceTimelines {};
    // the plugin parameter bindings of each unit (channel c: c; insert t: maxChannels + t), in RackState::pluginParams'
    // order, for a state (processBatched applies each unit's own, per piece, inside its task)
    std::vector<int> unitParamStart, unitParams;
    const RackState* unitParamsState = nullptr;
    std::uint64_t unitParamsSerial = 0;
    bool unitParamsOk = false; // the state can be batched (see batchable)
    std::uint64_t batchedBlocks = 0, batchedRounds = 0; // (diagnostics: blocks planned as batched, and run as one round)
    std::array<std::uint64_t, (std::size_t) BatchRefusal::count> batchRefused {};
    bool batchable (const RackState& state, int numSamples) noexcept;
    bool buildUnitParams (const RackState& state) noexcept;
    void applyUnitParams (const RackState& state, int unit, const Piece& piece) noexcept;
    // plans the block's pieces as process()'s loop cuts them (false: more than maxPieces, or one piece)
    bool planPieces (const RackState& state, bool playing, double startTick, int numSamples, double tps, int chunk, bool grid) noexcept;
    void processBatched (const RackState& state, bool playing, int numSamples, float* left, float* right, MeterBank* meters) noexcept;
    void carryPieceState (int from, int to) noexcept;
    bool unitParamsBend = false, unitParamsTimed = false, unitParamsHeld = false;
    bool runBatchedGraph (const RackState& state, bool playing, int channels, MeterBank* meters) noexcept;
    // ---- Round 190lv (sched2): batched blocks with live controllers --------------------------------------------------
    // A controller's output after piece k is what piece k+1's serial part reads (updateAutomation's live loop). Batched,
    // those parts leave a placeholder for pieces k >= 1 (deferLive), and the block runs two rounds: round A = the
    // controllers' sources and every unit that feeds them (liveInA: the upstream closure, so nothing in it waits for a
    // unit outside), each source writing its live values into the next piece's slot right after rendering a piece; round
    // B = the rest. Exact only where no later serial part touches a live value and a round-A unit reads only its own source's
    // or an upstream source's (buildLivePlan).
    bool deferLive = false;           // (processBatched's serial parts of pieces >= 1)
    bool liveBatch = false;           // this block runs the two rounds
    bool livePlanOk = false;          // the state's live controllers can be batched (for unitParamsState)
    std::vector<char> liveInA;        // [task] in round A
    std::vector<int> liveSourceStart, liveSources; // [task] → its live automations (CSR, in liveAutomations' order)
    std::vector<int> liveRootsA, liveRootsB, livePendingB;
    int liveRootCountA = 0, liveRootCountB = 0;
    int liveCountA = 0, liveCountB = 0; // positions of each round (its nodes minus the fused links inside it)
    std::uint64_t batchedLiveRounds = 0;
    const char* livePlanWhy = "";
    bool buildLivePlan (const RackState& state) noexcept;
    void writeLiveValue (const RackState& state, int automation, int slot) noexcept;
    bool runGraphRound (const RackState& state, bool playing, int channels, MeterBank* meters, int round) noexcept;
    // processChunk in its three parts (round 190kz): the serial part before the units, the units, the Master's part.
    // chunkPrelude returns the pool to restore after the units (chunkSchedule may set `workers` to nullptr)
    void chunkPrelude (const RackState& state, bool playing, double startTick, int numSamples, bool continuing) noexcept;
    WorkerPool* chunkSchedule (const RackState& state, int numSamples, int eventCount) noexcept;
    void chunkUnits (const RackState& state, bool playing, int numSamples, MeterBank* meters) noexcept;
    void chunkMaster (const RackState& state, int numSamples, float* left, float* right, MeterBank* meters, WorkerPool* pool,
                      std::uint64_t chunkStartNs) noexcept;
    void processChunk (const RackState& state, bool playing, double startTick, int numSamples,
                       float* left, float* right, MeterBank* meters, bool continuing = false) noexcept;
    // render-ahead (round 190gq): this renderer's part of the rack, the boundary in and out
    const Split* split = nullptr;
    SplitBlock* splitBlock = nullptr;
    int splitChunk = 0; // the chunk of the current process() call
    std::uint64_t splitMismatches = 0;
    HostTimeline* ownTimeline = nullptr;
    bool countsAudio = true; // (false: a renderer that shares another's times)
    bool foreignChannel (int c) const noexcept { return split != nullptr && c >= 0 && c < maxChannels && split->foreignChannel[(std::size_t) c] != 0; }
    bool foreignTrack (int t) const noexcept { return split != nullptr && t >= 0 && t < maxMixerTracks && split->foreignTrack[(std::size_t) t] != 0; }
    void importChannel (int channel, int n) noexcept;
    void importTrack (const RackState& state, int track, int n) noexcept;
    void exportSplit (int n) noexcept;
    // A7 (round 190gq): each unit's time in the last chunk it ran in, measured while workers are available (the
    // channel and insert tasks; nanoseconds, saturated), and what the chunk's work comes to from them
    std::array<std::uint32_t, maxChannels> channelCostNs {};
    std::array<std::uint32_t, maxMixerTracks> trackCostNs {};
    bool measureUnits = false;
    bool parallelChunk = false; // this chunk may go to the workers (the snapshot's guess, or its units' measured work)
    double lastWorkShareValue = 0.0;
    // the channel pass in clusters of neighbouring channels worth one hand-off each: [k] first channel of cluster k
    std::array<std::int16_t, maxChannels + 1> clusterStart {};
    // the boundary row of the current chunk (−1: the chunks do not line up: the slow path)
    int splitRow() noexcept;
    void resetNoteTools (const RackState& state) noexcept;
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
    void setSidechain (const RackState& state, const EffectSlot& slot, Effect& effect, int track, int index, int n) noexcept;
    // a side input of slot `index` of `track` (k as PdcPlan::sideDelay) from insert `source`: its buffer, or the slot's
    // delayed copy of it when the PDC plan delays it (round 190hq); {nullptr, nullptr} when it is silent
    std::pair<const float*, const float*> sideInput (const RackState& state, int track, int index, int k, int source, bool sounding, int n) noexcept;
    void connect (const RackState& state, const SlotConnections& c, Effect& effect, int track, int index, const float* l, const float* r, int n) noexcept;
    void updateAutomation (const RackState& state, double startTick) noexcept;
    void updateModulators (const RackState& state, double tick, int numSamples) noexcept; // round 177b
    double modSeconds = 0.0; // the free-running modulators' clock
    // Round 190ox: the stateful modulators' state (envelopes, followers, the last note; audio thread), the followed
    // inserts' last peak, and per channel the notes struck so far with the last one's key and velocity
    static constexpr int maxStatefulModulators = 256;
    std::array<modenv::State, maxStatefulModulators> modStates {};
    std::array<float, maxMixerTracks> followPeak {};        // [insert]
    std::array<std::uint32_t, maxChannels> notesStruck {};
    std::array<std::uint8_t, maxChannels> lastNoteKey {};
    std::array<float, maxChannels> lastNoteVelocity {};
    void noteStruck (int channel, int key, float velocity) noexcept
    {
        if (channel >= 0 && channel < maxChannels)
        {
            ++notesStruck[(std::size_t) channel];
            lastNoteKey[(std::size_t) channel] = (std::uint8_t) std::clamp (key, 0, 127);
            lastNoteVelocity[(std::size_t) channel] = velocity;
        }
    }
    // ticks from `tick` to the next pattern event (or pattern clip edge) that changes a lane's value; +inf if none
    static double ticksToNextEvent (const RackState& state, double tick) noexcept;
    // bindings from this automation slot on (andLive: and those of live controllers, the links)
    void applyPluginParams (const RackState& state, int firstSlot, bool andLive = false) noexcept;
    void applyPluginParam (const RackState& state, std::size_t index, int firstSlot, bool andLive) noexcept; // (one binding)
    void applyOrDeferParams (const RackState& state, int firstSlot, bool andLive) noexcept;               // (round 190kz)
    bool updatePatternTriggers (const RackState& state, double startTick) noexcept; // (round 190ow, DE-123)
    // Round 190jg (sample-accurate automation): the automation's changes inside this chunk for hosted CLAP / VST3
    // plugins (Instrument / Effect::setParameterAt), after the chunk's values. Clip automation and lanes (not pattern
    // events: a chunk already ends at each of those). Checked every timedStep samples of the export grid (a ramp moves
    // in steps that size instead of the chunk's 128), and a jump (Hold, Stairs, a step drawn) at its own sample.
    void queueTimedPluginParams (const RackState& state, double startTick, int numSamples, bool continuing) noexcept;
    static constexpr int timedStep = 16;
    std::int64_t timedGridPos = -1;                                  // the chunk's place on the export grid (-1: no grid)
    std::array<float, RackState::maxAutomations> timedLast {};       // the last value sent per automation slot
    PieceValue<double> compensateTick { -1.0 };          // the song position of this block's automation (song mode), −1 = none
    PieceValue<double> compensateTicksPerSample { 0.0 }; // > 0: compensation on in this block
    float automated (int automation, float base) const noexcept;
    float* trackBuffer (int track, int channel) noexcept;
    // Round 190gp (lazy-zero track buffers): an insert's buffer nobody has written in this chunk holds zeros without
    // storing them (trackUnwritten). trackBuffer — for the insert's own work and what is added into it — stores the
    // zeros on first use; trackInput — another insert reading it (a sidechain, a route, a hardware output) — reads
    // `zeroBlock` instead, so nothing is written by a reader. rawTrackBuffer: the memory, as it is.
    const float* trackInput (int track, int channel) noexcept;
    float* rawTrackBuffer (int track, int channel) noexcept;
    bool trackZeros (int track) const noexcept { return (std::size_t) track < trackUnwritten.size() && trackUnwritten[(std::size_t) track] != 0; }
    PieceArray<std::uint8_t, maxMixerTracks> trackUnwritten; // (round 190kz: per piece)
    std::vector<float> zeroBlock;
    PieceValue<int> trackSamples { 0 }; // the chunk's samples (what trackBuffer zeroes)

    double sampleRate = 44100.0;
    // round 190of: how long `samples` of audio last, in ns (the Plugin performance monitor's spikes)
    std::uint64_t audioNsOf (int samples) const noexcept
    {
        return samples > 0 && sampleRate > 0.0 ? (std::uint64_t) ((double) samples / sampleRate * 1.0e9) : 0;
    }
    double bpm = 120.0;
    double ownBpm = 120.0;
    double tempoOverride = 0.0;
    double playSpeed = 1.0;
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
    PieceEvents events; // (round 190kz: per piece)
    // round 190gn (A6): the per-chunk channel and insert buffers, sized by the songs (reserveFor) and handed to the
    // audio thread like the snapshots; `buffers` is the one process() uses (audio thread)
    struct Buffers
    {
        int channels = 0, tracks = 0, frames = 0;  // frames: of each mixer buffer, min (blockCapacity, chunkCapacity)
        mutable std::vector<float> channel;      // channels * 2 * chunkCapacity
        mutable std::vector<float> scratch;      // tracks * 2 * chunkCapacity
        mutable std::vector<float> mix;          // tracks * 2 * frames
    };
    static std::unique_ptr<const Buffers> makeBuffers (int channels, int tracks, int frames);
    bool buffersFor (const RackState& state) noexcept; // (audio thread) false: no buffers for it (the block is silent)
    SnapshotExchange<Buffers> bufferExchange;
    const Buffers* buffers = nullptr;
    std::mutex bufferLock; // the producers of bufferExchange: reserveFor (message thread) and prepare
    std::atomic<int> reservedChannels { 0 }, reservedTracks { 0 }, preparedFrames { 0 };
    std::atomic<bool> reserving { false }; // reserveFor is used: the audio thread never grows them itself
    PieceArray<float, RackState::maxAutomations> automationValues; // NaN = not evaluated yet (round 190kz: per piece)
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
    PieceArray<char, maxMixerTracks> trackActive;           // the track output has sound in this block
    std::vector<float> tapBuffers;                          // RackState::maxSendTaps * 2 * chunkCapacity
    PieceArray<char, RackState::maxSendTaps> tapActive;     // the Fruity Send slot sent something this block
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
    PieceArray<char, maxChannels> channelSilent;             // channel skipped in this chunk (idle, no notes)
    std::array<NoteMask, maxChannels> heldNotes {};          // keys held on each channel (effects that follow notes: LYNX Tune)
    std::array<std::array<std::atomic<std::uint64_t>, 2>, maxChannels> sounding {};
    std::array<std::atomic<std::uint32_t>, maxChannels> strikes {}; // (noteStrikes; written only by publishSoundingKeys)
    std::array<NoteMask, maxChannels> struckNotes {};        // keys started in this block (a note shorter than a block still shows)
    // FL's stepped slide notes (round 190jv, core/HostSlide.h Steps): a channel's stepped slide on the song's ticks; the
    // render calls split there (a sample after its start, then on each of FL's ticks up to its end)
    struct SlideTicks
    {
        double start = 0.0; // the slide's start (song ticks)
        int k = 0, n = 0;   // the next step, the slide's length (FL ticks)
    };
    std::array<SlideTicks, maxChannels> slideTicks {};
    // FL's slide notes move the note properties too (issue #236, round 190od; Song::slideNoteProps): each sounding key's
    // velocity, pan and Mod X / Y as the channel's voices have them now (set at note on, moved by slides), and a slide in
    // progress — the keys it moves, where they started and where they go — stepped on the slide's FL ticks
    struct NoteProps
    {
        float velocity = 0.78125f, pan = 0.0f, modX = 0.5f, modY = 0.5f;
        float cents = 0.0f; // (round 190pi) the note's own cents as its voice started (a chord slide's interval)
        std::int8_t colour = 0; // (round 190pi) its colour: a slide note moves the notes of its own colour only
    };
    struct PropSlide
    {
        static constexpr int maxKeys = 16;
        int count = 0; // keys it moves (0: none in progress)
        std::array<std::uint8_t, maxKeys> keys {};
        std::array<NoteProps, maxKeys> from {};
        NoteProps to;
        int steps = 0; // FL ticks (n): steps 0…n, the last one at the slide's end
        // (round 190pi) its own ticks: where it started (song ticks) and the next step — a pitch slide that starts while
        // it runs (an arpeggiator's Slide step) has its own schedule (SlideTicks) and no longer stops this one
        double start = 0.0;
        int k = 0;
        bool flLaw = false; // an FL native generator: the level moves linearly in FL's volume (InitLevels.Vol), not in velocity
    };
    // Round 190ql (perf 8): one block per channel the songs use (the keys' properties and the slide in progress), made
    // where the channel buffers are made (prepare, reserveFor, buffersFor's own growth: never in an audio callback) — no
    // longer maxChannels × 128 at every prepare (3.6 MB per renderer, twice with the render-ahead's). A channel without
    // its block has none, as a channel past the old vectors' end; process() renders only channels its buffers cover.
    struct ChannelProps
    {
        std::array<NoteProps, 128> notes {};
        PropSlide slide {};
    };
    std::array<std::atomic<ChannelProps*>, maxChannels> channelProps {};
    std::vector<std::unique_ptr<ChannelProps>> ownedProps; // [channel], channels 0 … propsCount − 1 (under bufferLock)
    std::atomic<int> propsCount { 0 };
    RackRenderer* propsMirror = nullptr; // the render-ahead's renderer: gets each block this one makes (adoptUnits copies)
    void ensureProps (int channels);     // (under bufferLock)
    ChannelProps* propsOf (std::size_t channel) const noexcept
    {
        return channel < (std::size_t) maxChannels ? channelProps[channel].load (std::memory_order_acquire) : nullptr;
    }
    // (`only`: the keys it moves — round 190pi, the slide note's colour —; null: every held key)
    void startPropSlide (int channel, const NoteEvent& slide, int flTicks, bool flLaw, const NoteMask* only = nullptr) noexcept;
    void stepPropSlide (Instrument& instrument, int channel, int step) noexcept;
    std::array<NoteMask, maxChannels> publishedNotes {};     // what soundingKeys holds (stored only on a change)
    std::atomic<bool> keepReleasedOnPlay { false };         // (round 190oa, DL-58)
    // round 190oa (DF-29): per channel, the notes released since the channel last fell silent (their voices still
    // release; renderChannel zeroes it once the channel is idle / below −100 dB), and the block's total
    std::array<std::uint16_t, maxChannels> releasingVoices {};
    std::atomic<int> voicesNow { 0 };
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
    PieceValue<double> chunkStartTick { 0.0 }, chunkTps { 1.0 }; // of the chunk being rendered (for the arpeggiator)
    PieceValue<bool> clipEnvTableShown { false };          // round 190gi: the last chunk's song had clip envelopes
    bool clipEnvTableWas = false;
    // live-played notes of channels with an arpeggiator, echo delay or key / velocity tracking go through the
    // channel's note path like the scheduled ones (FL, round 149); while stopped the arpeggiator runs on its own clock
    std::array<NoteEvent, 256> liveRouted {};
    // the VFX channels' processors run on the chunk's events before the channels render (round 164b)
    void runVfxChannels (const RackState& state, bool playing, int numSamples) noexcept;
    std::unique_ptr<vfx::EventList> vfxIn = std::make_unique<vfx::EventList>(), vfxOut = std::make_unique<vfx::EventList>();
    int liveRoutedCount = 0;
    double liveTick = 0.0;
    // round 190gh (B7, render = playback): while playing, the automation's pieces end on a grid counted in samples from
    // where playback started (or jumped: a seek, the loop), not from each block's start, so the values change at the
    // same samples whatever the audio device's block size; an export (blocks of 512 from its start) is unchanged
    std::int64_t gridSample = 0;   // (audio thread) samples played since the start or the last jump
    std::int64_t pieceStart = 0;   // where the current piece began (on that count): a block's end may cut it
    double gridNextTick = -1.0;    // the tick the next block starts at when nothing jumps; < 0: not playing
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
    RenderTaps* renderTaps = nullptr; // round 190gj (single-pass stems)
    void copyToRenderTap (int t, const float* l, const float* r, int n) noexcept;
    perf::Live* perfLive = nullptr;
    perf::Player perfPlayer;
    double perfClock = 0.0;      // the Player's steady clock (ticks): the transport's position, never wrapping
    bool perfRunning = false;
    bool launchersRunning = false;                 // the launchers' branch of the block is on (Performance mode, the grid)
    launcher::Runtime* runningLauncher = nullptr;  // the grid's runtime while it plays
    std::vector<float> hwScratch; // 2 × blockCapacity
    void mixHardwareOutputs (const RackState& state, int n) noexcept;
    PieceValue<int> liveInputOffset { 0 }; // of the chunk within the process() block
    // hardware MIDI outputs (round 190jl)
    std::atomic<int> currentSource { -1 }; // (round 190mv)
    void runCurrentTrack (const RackState& state, float* l, float* r, int n, MeterBank* meters) noexcept;
    std::array<std::atomic<std::uint64_t>, 4> hwPorts {};
    std::atomic<bool> hwPortsAny { false };
    std::array<HardwareMidiEvent, hardwareMidiCapacity> hwMidi {};
    int hwMidiCount = 0;
    std::array<Instrument::HardwareMidi, 512> hwMidiScratch {};
    std::array<char, maxChannels> hwMidiOn {}; // [channel] its capture was on in the last chunk
    void hardwareMidiBeforeChunk (const RackState& state) noexcept;
    void hardwareMidiAfterChunk (const RackState& state, int n) noexcept;
    void addHardwareMidi (int sample, int port, const std::uint8_t* bytes, int size, int source) noexcept;
    // the plugins' own notes (round 190ks)
    std::array<std::atomic<std::uint64_t>, ownNoteWords> ownWanted {};
    std::atomic<bool> ownWantedAny { false };
    std::array<std::uint64_t, ownNoteWords> ownOn {}; // the channels whose capture is on for the current chunk
    bool ownAnyOn = false;
    std::array<OwnNoteEvent, ownNoteCapacity> ownEvents {};
    int ownEventCount = 0;
    std::array<Instrument::OwnNote, 256> ownScratch {};
    void ownNotesBeforeChunk (const RackState& state) noexcept;
    void ownNotesAfterChunk (const RackState& state, int n) noexcept;
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
    std::unique_ptr<UnitTimes> ownUnits = std::make_unique<UnitTimes>();
    std::unique_ptr<ProcessTimes> ownTimes = std::make_unique<ProcessTimes>();
    UnitTimes* units = ownUnits.get();
    ProcessTimes* times = ownTimes.get();

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
    PieceValue<double> chunkTick { 0.0 };   // start of the block (for the global LFOs)
    PieceValue<bool> chunkPlaying { false }; // the song plays during the block (effects that follow the transport)
    PieceValue<bool> pdcBypassNow { false }; // the "When recording or not playing" tracks skip their PDC delays this chunk
    bool pdcBypassBefore = false;       // ... and did in the previous chunk (back to compensated: their lines are emptied)
    void applyTrackEq (const MixerTrack& track, int t, bool bypassed, float* l, float* r, int n) noexcept;
    static constexpr float silenceLevel = 1.5849e-5f;  // -96 dBFS
    int blockCapacity = 0;
};
} // namespace lynx
