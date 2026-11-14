#include "core/RenderAhead.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "core/Denormals.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/WorkerPool.h"

namespace lynx
{
std::atomic<bool>& renderAheadOption() noexcept
{
    static std::atomic<bool> on { true }; // O22: on by default
    return on;
}

namespace renderahead
{
void Targets::publish (const int* channels, int channelCount, const int* tracks, int trackCount) noexcept
{
    channelCount = std::clamp (channelCount, 0, maxChannels);
    trackCount = std::clamp (trackCount, 0, maxTracks);
    versionCounter.fetch_add (1, std::memory_order_acq_rel); // odd: writing
    for (int i = 0; i < channelCount; ++i)
        channelList[(std::size_t) i].store (channels[i], std::memory_order_relaxed);
    for (int i = 0; i < trackCount; ++i)
        trackList[(std::size_t) i].store (tracks[i], std::memory_order_relaxed);
    channelTotal.store (channelCount, std::memory_order_relaxed);
    trackTotal.store (trackCount, std::memory_order_relaxed);
    versionCounter.fetch_add (1, std::memory_order_acq_rel); // even: ready
}

bool Targets::read (std::array<int, maxChannels>& channels, int& channelCount, std::array<int, maxTracks>& tracks, int& trackCount,
                    std::uint32_t& version) const noexcept
{
    const auto before = versionCounter.load (std::memory_order_acquire);
    if ((before & 1u) != 0 || before == version)
        return false;
    const auto nc = std::clamp (channelTotal.load (std::memory_order_relaxed), 0, maxChannels);
    const auto nt = std::clamp (trackTotal.load (std::memory_order_relaxed), 0, maxTracks);
    std::array<int, maxChannels> c {};
    std::array<int, maxTracks> t {};
    for (int i = 0; i < nc; ++i)
        c[(std::size_t) i] = channelList[(std::size_t) i].load (std::memory_order_relaxed);
    for (int i = 0; i < nt; ++i)
        t[(std::size_t) i] = trackList[(std::size_t) i].load (std::memory_order_relaxed);
    if (versionCounter.load (std::memory_order_acquire) != before)
        return false; // written meanwhile: next block
    channels = c;
    tracks = t;
    channelCount = nc;
    trackCount = nt;
    version = before;
    return true;
}

bool TransportModel::operator== (const TransportModel& o) const noexcept
{
    // (offset is where the current sub-block starts: it moves every sub-block and changes no prediction)
    return sampleRate == o.sampleRate && deviceBlock == o.deviceBlock && songMode == o.songMode && loopStart == o.loopStart && loopEnd == o.loopEnd
           && songEnd == o.songEnd && songStart == o.songStart && linear == o.linear && throughRegion == o.throughRegion
           && pendingAt == o.pendingAt && predictable == o.predictable
           && (markers == nullptr) == (o.markers == nullptr)
           && (markers == nullptr || (markers->count == o.markers->count && markers->wrapTo == o.markers->wrapTo && markers->start == o.markers->start
                                      && std::equal (markers->jumps.begin(), markers->jumps.begin() + std::clamp (markers->count, 0, MarkerPlan::maxJumps),
                                                     o.markers->jumps.begin(),
                                                     [] (const MarkerJump& a, const MarkerJump& b) { return a.at == b.at && a.to == b.to && a.kind == b.kind; })));
}

SubBlock planSubBlock (const TransportModel& m, double position, int done, double tps) noexcept
{
    // AudioEngine::audioDeviceIOCallbackWithContext, one turn of its loop while playing (not counting in): the same
    // values in the same order, so the positions come out bit for bit as the callback's
    SubBlock s;
    auto n = m.deviceBlock - done;
    const auto loop = playbackLoop (m.songMode, m.throughRegion ? 0.0 : m.loopStart, m.throughRegion ? 0.0 : m.loopEnd, m.linear ? 0.0 : m.songEnd,
                                    m.songStart);
    const auto looping = loop.active() && tps > 0.0;
    const auto regionOn = ! m.throughRegion && m.loopEnd > m.loopStart;
    const auto wrapTo = m.markers != nullptr ? (double) m.markers->wrapTo : 0.0;
    const auto songWrap = regionOn ? loop.start : std::clamp (wrapTo, loop.start, std::max (loop.start, loop.end - 1.0));
    if (looping)
    {
        if (transportReached (position, loop.end))
            position = songWrap;
        n = transportSamplesUntil (position, loop.end, tps, n);
    }
    const MarkerJump* jump = nullptr;
    auto userJump = false;
    if (tps > 0.0 && m.songMode && m.markers != nullptr)
    {
        const auto step = markerStep (*m.markers, position, n, tps, m.pendingAt);
        n = step.samples;
        jump = step.jump;
        userJump = step.userJump;
    }
    s.start = position;
    s.samples = n;
    auto after = position + n * tps;
    if (userJump && transportReached (after, m.pendingAt))
        s.last = true; // the user's quantized jump: the callback moves the position
    else if (jump != nullptr && transportReached (after, (double) jump->at))
        s.last = true; // a marker jump / pause
    else if (looping && transportReached (after, loop.end))
        after = songWrap;
    s.next = after;
    return s;
}
} // namespace renderahead

using namespace renderahead;

namespace
{
constexpr int maxEntries = 64;
constexpr int poolSamples = 4096; // per boundary slot and side, for all the ring's entries
constexpr int tweakHoldMs = 250;
constexpr int editorPollSamples = 2048; // how often the open editors' live meters are looked at
} // namespace

struct RenderAhead::Entry
{
    RackRenderer::SplitBlock block;
    double start = 0.0;
    int samples = 0;
    bool playing = false;
    std::array<float, 2 * maxMixerTracks> peaks {}; // [i * 2 + side]: the i-th insert of the part rendered ahead
};

struct RenderAhead::Partition
{
    RackRenderer::Split liveSplit, aheadSplit;
    std::array<char, RackRenderer::maxChannels> aheadChannel {};
    std::array<char, maxMixerTracks> aheadTrack {};
    std::array<std::int16_t, maxMixerTracks> aheadTrackList {};
    int aheadTrackCount = 0, aheadChannelCount = 0;
    // the automations driven by a unit of the part rendered ahead (controllers): their value moves with that part
    std::array<int, RackState::maxAutomations> aheadAutomations {};
    int aheadAutomationCount = 0;
    // scratch of the closure
    static constexpr int nodes = RackRenderer::maxChannels + maxMixerTracks;
    std::array<std::int16_t, nodes> parent {};
    std::array<char, nodes> live {}, groupLive {};
    std::uint32_t editorHash = 0;

    bool sameUnits (const Partition& o) const noexcept
    {
        return aheadChannel == o.aheadChannel && aheadTrack == o.aheadTrack && liveSplit.slots == o.liveSplit.slots
               && std::equal (liveSplit.slotKind.begin(), liveSplit.slotKind.begin() + liveSplit.slots, o.liveSplit.slotKind.begin())
               && std::equal (liveSplit.slotUnit.begin(), liveSplit.slotUnit.begin() + liveSplit.slots, o.liveSplit.slotUnit.begin());
    }
};

RenderAhead::RenderAhead (RackRenderer& liveRenderer)
    : live (liveRenderer),
      ahead (std::make_unique<RackRenderer>()),
      aheadInput (std::make_unique<LiveInput>()),
      aheadMeters (std::make_unique<MeterBank>()),
      aheadTimeline (std::make_unique<HostTimeline>()),
      current (std::make_unique<Partition>()),
      next (std::make_unique<Partition>()),
      wake (std::make_unique<OsSemaphore>()),
      forward (std::make_unique<LiveEventQueue>()),
      localEvents (std::make_unique<LiveEventQueue>())
{
    ahead->shareTimesWith (live);
    ahead->setLiveInput (aheadInput.get());
    ahead->setHostTimeline (aheadTimeline.get());
    for (int e = 0; e < maxEntries; ++e)
        entries.push_back (std::make_unique<Entry>());
}

RenderAhead::~RenderAhead()
{
    quit.store (true, std::memory_order_seq_cst);
    wake->release();
    if (worker.joinable())
        worker.join();
}

void RenderAhead::setWorkerPool (WorkerPool* p, std::function<void()> onThreadStart)
{
    pool = p;
    if (! worker.joinable())
        worker = std::thread ([this, onThreadStart] { threadLoop (onThreadStart); });
}

void RenderAhead::prepare (double rate, int maxBlockSize)
{
    // (no audio callback runs the rack now: a device (re)start or an offline render getting ready)
    running.store (false, std::memory_order_seq_cst);
    while (owner.load (std::memory_order_acquire) == 1)
        std::this_thread::yield();
    if (engagedNow.load (std::memory_order_relaxed))
        disengage (liveState, writeIndex.load() != readIndex.load());
    sampleRate = rate;
    maxBlock = std::max (64, maxBlockSize);
    halted.store (false, std::memory_order_seq_cst);
    if (windowSamples <= 0)
        windowSamples = (int) std::lround (0.0232 * rate); // about 23 ms: 1024 samples at 44.1 kHz
    ahead->forgetState();
    ahead->prepare (rate, maxBlockSize);
    audioPool.assign ((std::size_t) RackRenderer::Split::maxSlots * 2 * poolSamples, 0.0f);
    flagPool.assign ((std::size_t) maxEntries * RackRenderer::SplitBlock::maxChunks * RackRenderer::Split::maxSlots, 0);
    scratchL.assign ((std::size_t) maxBlock, 0.0f);
    scratchR.assign ((std::size_t) maxBlock, 0.0f);
    entryCount = 0;
    readIndex.store (0);
    writeIndex.store (0);
}

void RenderAhead::halt() noexcept
{
    halted.store (true, std::memory_order_seq_cst);
    running.store (false, std::memory_order_seq_cst);
    while (owner.load (std::memory_order_acquire) == 1)
        std::this_thread::yield();
}

void RenderAhead::setOverriddenSlots (const std::vector<int>& slots) noexcept
{
    live.setOverriddenSlots (slots);
    ahead->setOverriddenSlots (slots);
}

void RenderAhead::setReleasedSlots (const std::vector<int>& slots) noexcept
{
    live.setReleasedSlots (slots);
    ahead->setReleasedSlots (slots);
}

void RenderAhead::setProfiling (bool on) noexcept
{
    live.setProfiling (on);
    ahead->setProfiling (on);
}

const RackRenderer& RenderAhead::rendererOf (int channel) const noexcept
{
    if (engagedNow.load (std::memory_order_relaxed) && channel >= 0 && channel < RackRenderer::maxChannels
        && current->aheadChannel[(std::size_t) channel] != 0)
        return *ahead;
    return live;
}

// ---- the partition -------------------------------------------------------------------------------------------------

bool RenderAhead::computePartition (const RackState& state, const Block& block, Partition& p) noexcept
{
    const auto& song = state.song;
    const auto C = std::min<int> ({ (int) song.channels.size(), (int) state.instruments.size(), RackRenderer::maxChannels });
    const auto T = std::clamp ((int) song.mixer.size(), 1, maxMixerTracks);
    const auto N = C + T;
    auto track = [C] (int t) { return C + t; };
    auto okChannel = [C] (int c) { return c >= 0 && c < C; };
    auto okTrack = [T] (int t) { return t >= 0 && t < T; };
    for (int i = 0; i < N; ++i)
    {
        p.parent[(std::size_t) i] = (std::int16_t) i;
        p.live[(std::size_t) i] = 0;
        p.groupLive[(std::size_t) i] = 0;
    }
    auto find = [&p] (int x) {
        while (p.parent[(std::size_t) x] != x)
        {
            p.parent[(std::size_t) x] = p.parent[(std::size_t) p.parent[(std::size_t) x]];
            x = p.parent[(std::size_t) x];
        }
        return x;
    };
    auto unite = [&] (int a, int b) {
        a = find (a);
        b = find (b);
        if (a != b)
            p.parent[(std::size_t) a] = (std::int16_t) b;
    };
    auto unitOf = [&] (const AutomationTarget& t, int* out) -> int {
        switch (t.kind)
        {
            case AutomationTargetKind::channelVolume:
            case AutomationTargetKind::channelPan:
            case AutomationTargetKind::channelParam:
            case AutomationTargetKind::channelAdjust:
                if (okChannel (t.index))
                {
                    out[0] = t.index;
                    return 1;
                }
                return 0;
            case AutomationTargetKind::mixerVolume:
            case AutomationTargetKind::mixerPan:
            case AutomationTargetKind::effectParam:
            case AutomationTargetKind::effectMix:
                if (okTrack (t.index))
                {
                    out[0] = track (t.index);
                    return 1;
                }
                return 0;
            case AutomationTargetKind::mixerSend: // (the destination pulls the route at its level)
            {
                auto n = 0;
                if (okTrack (t.index))
                    out[n++] = track (t.index);
                if (okTrack (t.slot))
                    out[n++] = track (t.slot);
                return n;
            }
            default:
                return 0;
        }
    };

    // a controller driving the tempo: the positions depend on a unit's output, nothing can be foreseen
    if (state.tempoAutomation >= 0 && (std::size_t) state.tempoAutomation < song.automations.size()
        && song.automations[(std::size_t) state.tempoAutomation].isLive())
        return false;

    // --- the live seeds
    p.live[(std::size_t) track (0)] = 1; // the Master: its effects, its captures, the device
    for (int t = 1; t < T; ++t)
    {
        const auto& m = song.mixer[(std::size_t) t];
        if (m.inputChannel >= 0)
            p.live[(std::size_t) track (t)] = 1;
        if (liveInput != nullptr && (liveInput->armed[(std::size_t) t].load (std::memory_order_relaxed) || liveInput->tappedPoints (t) != 0))
            p.live[(std::size_t) track (t)] = 1;
    }
    for (const auto& route : state.hwRoutes)
        if (okTrack (route.track))
            p.live[(std::size_t) track (route.track)] = 1;
    for (const auto t : { block.metronomeTrack, block.sideTrack, block.captureTrack })
        if (okTrack (t))
            p.live[(std::size_t) track (t)] = 1;
    for (const auto c : state.audioThreadChannels) // ("Allow threaded processing" off, hardware I/O): the audio thread only
        if (okChannel (c))
            p.live[(std::size_t) c] = 1;
    for (const auto& layer : state.audioThreadLayers)
        for (const auto t : layer)
            if (okTrack (t))
                p.live[(std::size_t) track (t)] = 1;
    for (int i = 0; i < targetChannelCount; ++i)
        if (okChannel (targetChannels[(std::size_t) i]))
            p.live[(std::size_t) targetChannels[(std::size_t) i]] = 1;
    for (int i = 0; i < promotedCount; ++i)
        if (okChannel (promotedChannels[(std::size_t) i]))
            p.live[(std::size_t) promotedChannels[(std::size_t) i]] = 1;
    for (int i = 0; i < targetTrackCount; ++i)
        if (okTrack (targetTracks[(std::size_t) i]))
            p.live[(std::size_t) track (targetTracks[(std::size_t) i])] = 1;
    // an editor showing a slot's live level meters or its spectrum: that insert stays in time with its pictures
    {
        auto& times = live.processTimes();
        std::uint32_t hash = 2166136261u;
        for (int t = 0; t < T; ++t)
            for (int s = 0; s < maxEffectSlots; ++s)
                if (times.levelWanted[(std::size_t) (t * maxEffectSlots + s)].load (std::memory_order_relaxed) > 0)
                {
                    p.live[(std::size_t) track (t)] = 1;
                    hash = (hash ^ (std::uint32_t) (t * maxEffectSlots + s)) * 16777619u;
                }
        if (const auto key = times.analyzer.key.load (std::memory_order_relaxed); key >= 0 && okTrack (key / maxEffectSlots))
        {
            p.live[(std::size_t) track (key / maxEffectSlots)] = 1;
            hash = (hash ^ (std::uint32_t) (key + 0x10000)) * 16777619u;
        }
        p.editorHash = hash;
    }

    // --- units tied together by notes and controls: one side only
    for (int c = 0; c < C; ++c)
    {
        const auto& ch = song.channels[(std::size_t) c];
        if ((std::size_t) c < state.midiRoutesOut.size())
            for (const auto& route : state.midiRoutesOut[(std::size_t) c])
                if (okChannel (route.other))
                    unite (c, route.other);
        if ((std::size_t) c < state.midiOutTargets.size())
            for (const auto t : state.midiOutTargets[(std::size_t) c])
                if (okChannel (t))
                    unite (c, t);
        if ((std::size_t) c < state.midiTargets.size())
            for (const auto t : state.midiTargets[(std::size_t) c])
                if (okChannel (t))
                    unite (c, t);
        if ((std::size_t) c < state.cutTargets.size())
            for (const auto t : state.cutTargets[(std::size_t) c])
                if (okChannel (t))
                    unite (c, t);
        if (ch.instrument == InstrumentKind::layer)
            for (const auto child : ch.layer.children)
                if (okChannel (child))
                    unite (c, child);
        if ((std::size_t) c < state.effectMidiRoutes.size())
            for (const auto& route : state.effectMidiRoutes[(std::size_t) c])
                if (okTrack (route.track))
                    unite (c, track (route.track));
        if (const auto* rk = rack::rackOf (ch.patcher); rk != nullptr)
            for (const auto child : rk->children)
                if (okChannel (child))
                    unite (c, child);
        // a generator's extra outputs (a generator Patcher's) go to inserts it names while it plays
        if (const auto* instrument = state.instruments[(std::size_t) c].get(); instrument != nullptr)
            for (int k = 0; k < instrument->auxOutputCount(); ++k)
                if (okTrack (instrument->auxOutputTrack (k)))
                    unite (c, track (instrument->auxOutputTrack (k)));
    }
    // controllers (Peak / Formula / XY controllers, a plugin's output, a generator's): their source and what they drive
    p.aheadAutomationCount = 0;
    for (const auto a : state.liveAutomations)
    {
        if (a < 0 || (std::size_t) a >= song.automations.size())
            continue;
        const auto& automation = song.automations[(std::size_t) a];
        const auto& src = automation.source;
        const auto source = src.channel >= 0 ? (okChannel (src.channel) ? src.channel : -1) : (okTrack (src.track) ? track (src.track) : -1);
        if (source < 0)
            continue;
        int units[2] {};
        for (int k = 0, n = unitOf (automation.target, units); k < n; ++k)
            unite (source, units[k]);
        for (const auto& route : state.modRoutes) // (a modulator riding on that controller's value)
            if (route.source == a && route.slot >= 0 && (std::size_t) route.slot < song.automations.size())
                for (int k = 0, n = unitOf (song.automations[(std::size_t) route.slot].target, units); k < n; ++k)
                    unite (source, units[k]);
    }

    // --- the closure: a group with a live member is live; whatever a live unit feeds is live
    for (auto changed = true; changed;)
    {
        changed = false;
        for (int i = 0; i < N; ++i)
            if (p.live[(std::size_t) i] != 0)
                p.groupLive[(std::size_t) find (i)] = 1;
        for (int i = 0; i < N; ++i)
            if (p.live[(std::size_t) i] == 0 && p.groupLive[(std::size_t) find (i)] != 0)
            {
                p.live[(std::size_t) i] = 1;
                changed = true;
            }
        for (int t = 1; t < T; ++t)
        {
            if (p.live[(std::size_t) track (t)] != 0)
                continue;
            auto feeds = false;
            if ((std::size_t) t < state.trackChannels.size())
                for (const auto c : state.trackChannels[(std::size_t) t])
                    feeds = feeds || (okChannel (c) && p.live[(std::size_t) c] != 0);
            if ((std::size_t) t < state.mixerInputs.size())
                for (const auto& in : state.mixerInputs[(std::size_t) t])
                    feeds = feeds || (okTrack (in.source) && p.live[(std::size_t) track (in.source)] != 0);
            const auto& effects = song.mixer[(std::size_t) t].effects;
            const auto* connections = (std::size_t) t < state.slotConnections.size() ? &state.slotConnections[(std::size_t) t] : nullptr;
            for (std::size_t i = 0; i < effects.size(); ++i)
                for (const auto source : { effects[i].sidechain, effects[i].sidechain2,
                                           connections != nullptr && i < connections->size() ? (*connections)[i].mainSource : -1,
                                           connections != nullptr && i < connections->size() ? (*connections)[i].sideSource : -1 })
                    feeds = feeds || (okTrack (source) && source != t && p.live[(std::size_t) track (source)] != 0);
            if (feeds)
            {
                p.live[(std::size_t) track (t)] = 1;
                changed = true;
            }
        }
    }

    // --- the two renderers' views and the boundary
    auto& L = p.liveSplit;
    auto& A = p.aheadSplit;
    L.clear();
    A.clear();
    p.aheadChannel.fill (0);
    p.aheadTrack.fill (0);
    p.aheadChannelCount = p.aheadTrackCount = 0;
    for (int c = 0; c < RackRenderer::maxChannels; ++c)
    {
        const auto ahead = c < C && p.live[(std::size_t) c] == 0;
        p.aheadChannel[(std::size_t) c] = ahead ? 1 : 0;
        L.foreignChannel[(std::size_t) c] = ahead ? 1 : 0;
        A.foreignChannel[(std::size_t) c] = ahead ? 0 : 1;
        p.aheadChannelCount += ahead ? 1 : 0;
    }
    for (int t = 0; t < maxMixerTracks; ++t)
    {
        const auto ahead = t < T && p.live[(std::size_t) track (t)] == 0;
        p.aheadTrack[(std::size_t) t] = ahead ? 1 : 0;
        L.foreignTrack[(std::size_t) t] = ahead ? 1 : 0;
        A.foreignTrack[(std::size_t) t] = ahead ? 0 : 1;
        if (ahead)
            p.aheadTrackList[(std::size_t) p.aheadTrackCount++] = (std::int16_t) t;
    }
    // (nothing worth it: no channel and no insert with effects to render ahead)
    auto aheadWork = p.aheadChannelCount > 0;
    for (int i = 0; i < p.aheadTrackCount && ! aheadWork; ++i)
        aheadWork = ! song.mixer[(std::size_t) p.aheadTrackList[(std::size_t) i]].effects.empty();
    if (! aheadWork)
        return false;
    auto overflow = false;
    auto addSlot = [&] (RackRenderer::Split::Kind kind, int unit) {
        auto* slot = kind == RackRenderer::Split::Kind::channel ? &L.channelSlot[(std::size_t) unit]
                   : kind == RackRenderer::Split::Kind::track ? &L.trackSlot[(std::size_t) unit]
                                                              : &L.tapSlot[(std::size_t) unit];
        if (*slot >= 0)
            return;
        if (L.slots >= RackRenderer::Split::maxSlots)
        {
            overflow = true;
            return;
        }
        *slot = (std::int16_t) L.slots;
        L.slotKind[(std::size_t) L.slots] = kind;
        L.slotUnit[(std::size_t) L.slots] = (std::int16_t) unit;
        ++L.slots;
    };
    for (int t = 0; t < T; ++t)
    {
        if (p.live[(std::size_t) track (t)] == 0)
            continue;
        if ((std::size_t) t < state.trackChannels.size())
            for (const auto c : state.trackChannels[(std::size_t) t])
                if (okChannel (c) && p.live[(std::size_t) c] == 0)
                    addSlot (RackRenderer::Split::Kind::channel, c);
        if ((std::size_t) t < state.mixerInputs.size())
            for (const auto& in : state.mixerInputs[(std::size_t) t])
                if (okTrack (in.source) && p.live[(std::size_t) track (in.source)] == 0)
                {
                    if (in.tap >= 0 && in.tap < RackState::maxSendTaps)
                        addSlot (RackRenderer::Split::Kind::tap, in.tap);
                    else
                        addSlot (RackRenderer::Split::Kind::track, in.source);
                }
        const auto& effects = song.mixer[(std::size_t) t].effects;
        const auto* connections = (std::size_t) t < state.slotConnections.size() ? &state.slotConnections[(std::size_t) t] : nullptr;
        for (std::size_t i = 0; i < effects.size(); ++i)
            for (const auto source : { effects[i].sidechain, effects[i].sidechain2,
                                       connections != nullptr && i < connections->size() ? (*connections)[i].mainSource : -1,
                                       connections != nullptr && i < connections->size() ? (*connections)[i].sideSource : -1 })
                if (okTrack (source) && source != t && p.live[(std::size_t) track (source)] == 0)
                    addSlot (RackRenderer::Split::Kind::track, source);
    }
    if (overflow)
        return false;
    A.channelSlot = L.channelSlot;
    A.trackSlot = L.trackSlot;
    A.tapSlot = L.tapSlot;
    A.slotKind = L.slotKind;
    A.slotUnit = L.slotUnit;
    A.slots = L.slots;
    A.exports = true;
    L.exports = false;

    for (const auto a : state.liveAutomations)
    {
        if (a < 0 || (std::size_t) a >= song.automations.size() || p.aheadAutomationCount >= RackState::maxAutomations)
            continue;
        const auto& src = song.automations[(std::size_t) a].source;
        const auto aheadSource = src.channel >= 0 ? okChannel (src.channel) && p.live[(std::size_t) src.channel] == 0
                                                  : okTrack (src.track) && p.live[(std::size_t) track (src.track)] == 0;
        if (aheadSource)
            p.aheadAutomations[(std::size_t) p.aheadAutomationCount++] = a;
    }
    return true;
}

bool RenderAhead::partitionOf (const RackState& state, const int* liveChannels, int liveChannelCount, const int* liveTracks, int liveTrackCount,
                               const LiveInput* input, std::vector<char>& aheadChannels, std::vector<char>& aheadTracks, int& boundary)
{
    RackRenderer scratch;
    RenderAhead r (scratch);
    r.liveInput = const_cast<LiveInput*> (input);
    r.targetChannelCount = std::clamp (liveChannelCount, 0, Targets::maxChannels);
    for (int i = 0; i < r.targetChannelCount; ++i)
        r.targetChannels[(std::size_t) i] = liveChannels[i];
    r.targetTrackCount = std::clamp (liveTrackCount, 0, Targets::maxTracks);
    for (int i = 0; i < r.targetTrackCount; ++i)
        r.targetTracks[(std::size_t) i] = liveTracks[i];
    Block block;
    const auto ok = r.computePartition (state, block, *r.next);
    aheadChannels.assign (r.next->aheadChannel.begin(), r.next->aheadChannel.begin() + (std::ptrdiff_t) std::min<std::size_t> (state.song.channels.size(), RackRenderer::maxChannels));
    aheadTracks.assign (r.next->aheadTrack.begin(), r.next->aheadTrack.begin() + (std::ptrdiff_t) std::min<std::size_t> (state.song.mixer.size(), maxMixerTracks));
    boundary = r.next->liveSplit.slots;
    return ok;
}

// ---- engaging, syncing, dropping ------------------------------------------------------------------------------------

bool RenderAhead::engage (const RackState& state, const Block& b) noexcept
{
    if (! computePartition (state, b, *next))
        return false;
    stride = b.model->deviceBlock;
    entryCount = std::min (maxEntries, poolSamples / std::max (1, stride));
    if (entryCount < 2 || stride > maxBlock)
        return false;
    targetFill = std::clamp ((windowSamples + stride - 1) / stride, 1, entryCount - 1);
    for (int e = 0; e < entryCount; ++e)
    {
        auto& block = entries[(std::size_t) e]->block;
        block.audio = audioPool.data() + (std::size_t) e * RackRenderer::Split::maxSlots * 2 * (std::size_t) stride;
        block.stride = stride;
        block.flags = flagPool.data() + (std::size_t) e * RackRenderer::SplitBlock::maxChunks * RackRenderer::Split::maxSlots;
        block.chunks = 0;
    }
    readIndex.store (0, std::memory_order_relaxed);
    writeIndex.store (0, std::memory_order_relaxed);
    std::swap (current, next);
    // (the back-off rules start counting afresh; the thread is idle)
    serialLoad = 0.0;
    serialLoadSeen.store (0.0f, std::memory_order_relaxed);
    jobsSinceSerial = 0;
    heavyFor = windowSeconds = 0.0;
    windowBlocks = windowWaits = 0;

    // the part rendered ahead starts where the live renderer is: its transport, its started notes, the units' state
    ahead->adoptGlobals (live);
    ahead->adoptUnits (live, current->aheadChannel.data(), current->aheadTrack.data());
    ahead->adoptAutomationValues (live, current->aheadAutomations.data(), current->aheadAutomationCount);
    live.setSplit (&current->liveSplit);
    ahead->setSplit (&current->aheadSplit);
    live.setWorkerPool (nullptr); // (the workers are the ahead part's now; the live part is small)

    liveState = aheadState = &state;
    aheadNext.store (&state, std::memory_order_release);
    aheadAck.store (&state, std::memory_order_release);
    jobBlock = b;
    jobRecording = liveInput != nullptr && liveInput->recording.load (std::memory_order_relaxed);
    jobLinear = liveInput != nullptr && liveInput->linearSong.load (std::memory_order_relaxed);
    jobPatternOrigin = liveInput != nullptr ? liveInput->patternOrigin.load (std::memory_order_relaxed) : -1.0;
    model = *b.model;
    if (b.model->markers != nullptr)
    {
        modelMarkers = *b.model->markers;
        model.markers = &modelMarkers;
    }
    lastModel = model;
    modelStop.store (true, std::memory_order_release); // (until the first block is rendered here and the thread knows where the next one starts)
    partitionTargets = targetVersion;
    drain.store (false, std::memory_order_relaxed);
    running.store (false, std::memory_order_relaxed);
    engagedNow.store (true, std::memory_order_release);
    counters.engaged.store (true, std::memory_order_relaxed);
    counters.engagements.fetch_add (1, std::memory_order_relaxed);
    counters.aheadChannels.store (current->aheadChannelCount, std::memory_order_relaxed);
    counters.aheadTracks.store (current->aheadTrackCount, std::memory_order_relaxed);
    counters.boundary.store (current->liveSplit.slots, std::memory_order_relaxed);
    counters.window.store (targetFill * stride, std::memory_order_relaxed);
    return true;
}

void RenderAhead::disengage (const RackState* state, bool dropped) noexcept
{
    // (the thread is idle: the callback holds the renderer, or nothing runs)
    if (! engagedNow.load (std::memory_order_relaxed))
        return;
    running.store (false, std::memory_order_seq_cst);
    readIndex.store (writeIndex.load (std::memory_order_relaxed), std::memory_order_relaxed);
    live.adoptUnits (*ahead, current->aheadChannel.data(), current->aheadTrack.data());
    live.adoptAutomationValues (*ahead, current->aheadAutomations.data(), current->aheadAutomationCount);
    live.setSplit (nullptr);
    if (dropped && state != nullptr) // what the ahead part played past the dropped blocks never sounded: its notes end
        live.silenceChannels (*state, current->aheadChannel.data());
    live.setSplitBlock (nullptr);
    live.setWorkerPool (pool);
    ahead->setSplit (nullptr);
    ahead->setSplitBlock (nullptr);
    ahead->forgetState();
    // live notes still waiting for the part rendered ahead: the live renderer plays them now
    NoteEvent e;
    while (forward->pop (e))
    {
        localEvents->push (e);
        localPending = true;
    }
    aheadState = nullptr;
    aheadNext.store (nullptr, std::memory_order_release);
    aheadAck.store (nullptr, std::memory_order_release);
    pinned.store (0.0, std::memory_order_relaxed);
    drain.store (false, std::memory_order_relaxed);
    engagedNow.store (false, std::memory_order_release);
    counters.engaged.store (false, std::memory_order_relaxed);
}

void RenderAhead::dropRing (const RackState& state) noexcept
{
    // (the callback holds the renderer) the ring's blocks are not the ones the callback plays: dropped. The ahead part
    // goes on from the live renderer's transport (in step again), its own notes ended (they were played past here).
    readIndex.store (writeIndex.load (std::memory_order_relaxed), std::memory_order_relaxed);
    ahead->adoptGlobals (live);
    ahead->adoptAutomationValues (live, nullptr, 0);
    ahead->silenceChannels (state, current->aheadChannel.data());
    modelStop.store (true, std::memory_order_release);
    counters.flushes.fetch_add (1, std::memory_order_relaxed);
}

void RenderAhead::retireHeld (SnapshotExchange<RackState>& exchange) noexcept
{
    if (held == nullptr)
        return;
    if (engagedNow.load (std::memory_order_relaxed) && aheadAck.load (std::memory_order_acquire) == held)
        return; // still in use by the ahead renderer
    if (held == liveState)
        return;
    if (exchange.retire (held))
        held = nullptr;
}

// ---- the render-ahead thread ----------------------------------------------------------------------------------------

bool RenderAhead::producing() const noexcept
{
    if (! running.load (std::memory_order_acquire) || drain.load (std::memory_order_acquire) || modelStop.load (std::memory_order_acquire)
        || halted.load (std::memory_order_acquire))
        return false;
    const auto fill = writeIndex.load (std::memory_order_relaxed) - readIndex.load (std::memory_order_acquire);
    const auto target = tweakHoldSamples.load (std::memory_order_relaxed) > 0 ? 1 : targetFill;
    return fill < target && fill < entryCount - 1;
}

void RenderAhead::wakeThread() noexcept
{
    if (sleeping.exchange (false, std::memory_order_seq_cst))
        wake->release();
}

void RenderAhead::threadLoop (std::function<void()> onStart)
{
    const ScopedFlushDenormals noDenormals;
    if (onStart)
        onStart();
    while (! quit.load (std::memory_order_acquire))
    {
        auto expected = 0;
        if (! producing() || ! owner.compare_exchange_strong (expected, 1, std::memory_order_acq_rel))
        {
            sleeping.store (true, std::memory_order_seq_cst);
            if (quit.load (std::memory_order_seq_cst) || (producing() && owner.load (std::memory_order_seq_cst) == 0))
            {
                sleeping.store (false, std::memory_order_seq_cst);
                continue;
            }
            wake->acquire();
            sleeping.store (false, std::memory_order_seq_cst);
            continue;
        }
        if (! producing() || aheadState == nullptr) // (the callback asked for a drain meanwhile)
        {
            owner.store (0, std::memory_order_release);
            continue;
        }
        if (const auto* s = aheadNext.load (std::memory_order_acquire); s != nullptr && s != aheadState)
        {
            aheadState = s;
            aheadAck.store (s, std::memory_order_release);
        }
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto fill = w - readIndex.load (std::memory_order_acquire);
        auto& entry = *entries[(std::size_t) (w % entryCount)];
        const auto sb = planSubBlock (model, nextPosition, nextDone, transportTps (ahead->currentBpm(), model.sampleRate));
        // (serial while the window is comfortable: no hand-off cost; the workers when it runs thin)
        // (macOS: the workers may sit in the device's Audio Workgroup, round 190gt; render-ahead is not deadline work and
        // stays on its own thread there)
       #if defined(__APPLE__)
        const auto useWorkers = false;
        (void) fill;
       #else
        // (the workers when the window runs thin, or for good while one thread can't keep up — a serial block now and
        // then, with the window comfortable, measures that again)
        const auto heavy = serialLoad >= serialLimit && ! (jobsSinceSerial >= 256 && fill >= targetFill / 2);
        const auto useWorkers = pool != nullptr && (fill < std::max (1, targetFill / 3) || heavy);
       #endif
        const auto jobStart = ProcessTimes::now();
        const auto jobCpu = WorkerPool::threadCpuNs();
        renderJob (entry, *aheadState, sb.start, sb.samples, true, useWorkers);
        const auto jobNs = ProcessTimes::now() - jobStart;
        aheadCpuNs.fetch_add (WorkerPool::threadCpuNs() - jobCpu, std::memory_order_relaxed);
        aheadBusyNs.fetch_add (jobNs, std::memory_order_relaxed);
        if (useWorkers)
            ++jobsSinceSerial;
        else if (sb.samples > 0 && model.sampleRate > 0.0)
        {
            const auto load = (double) jobNs * 1.0e-9 * model.sampleRate / sb.samples;
            serialLoad += 0.1 * (load - serialLoad);
            serialLoadSeen.store ((float) serialLoad, std::memory_order_relaxed);
            jobsSinceSerial = 0;
        }
        nextPosition = sb.next;
        nextDone = nextDone + sb.samples >= model.deviceBlock ? 0 : nextDone + sb.samples;
        modelStop.store (sb.last, std::memory_order_release);
        if (useWorkers && (fill + 1 >= targetFill || sb.last))
            pool->park(); // (the window is full: the workers sleep instead of spinning)
        writeIndex.store (w + 1, std::memory_order_release);
        owner.store (0, std::memory_order_release);
    }
}

void RenderAhead::renderJob (Entry& entry, const RackState& state, double startTick, int samples, bool playing, bool useWorkers) noexcept
{
    ahead->setWorkerPool (useWorkers ? pool : nullptr);
    aheadInput->recording.store (jobRecording, std::memory_order_relaxed);
    aheadInput->linearSong.store (jobLinear, std::memory_order_relaxed);
    aheadInput->patternOrigin.store (jobPatternOrigin, std::memory_order_relaxed);
    ahead->setTransportOptions (jobBlock.playTruncated, jobBlock.resetPlugins, jobBlock.userJumps);
    ahead->setTempoOverride (0.0);
    entry.block.chunks = 0;
    ahead->setSplitBlock (&entry.block);
    ahead->process (&state, playing, startTick, samples, scratchL.data(), scratchR.data(), forward.get(), aheadMeters.get(), nullptr);
    harvestMeters (entry);
    entry.start = startTick;
    entry.samples = samples;
    entry.playing = playing;
}

void RenderAhead::harvestMeters (Entry& entry) noexcept
{
    auto& m = *aheadMeters;
    for (int i = 0; i < current->aheadTrackCount; ++i)
    {
        const auto t = current->aheadTrackList[(std::size_t) i];
        for (int side = 0; side < 2; ++side)
        {
            const auto a = m.takePeak (t, side), v = m.takeViewPeak (t, side), s = m.takeSessionPeak (t, side);
            entry.peaks[(std::size_t) (i * 2 + side)] = std::max ({ a, v, s });
        }
    }
}

void RenderAhead::replayMeters (const Entry& entry, MeterBank* meters) noexcept
{
    if (meters == nullptr)
        return;
    for (int i = 0; i < current->aheadTrackCount; ++i)
    {
        const auto l = entry.peaks[(std::size_t) (i * 2)], r = entry.peaks[(std::size_t) (i * 2 + 1)];
        if (l > 0.0f || r > 0.0f)
            meters->accumulate (current->aheadTrackList[(std::size_t) i], l, r);
    }
}

void RenderAhead::noteLoad (double ns, int samples, bool playing) noexcept
{
    if (samples <= 0 || sampleRate <= 0.0)
        return;
    const auto seconds = samples / sampleRate;
    const auto load = ns * 1.0e-9 / seconds;
    const auto a = std::min (1.0, seconds / 0.5); // (about half a second's average)
    loadAverage += a * (load - loadAverage);
    // one heavy block arms it until the average has had a second to settle (decays with it)
    spiked = playing && (load >= engageSpike || (spiked && loadAverage >= engageLoad * 0.5));
    idleFor = engagedNow.load (std::memory_order_relaxed) && loadAverage < idleLoad ? idleFor + seconds : 0.0;
}

bool RenderAhead::claimForCallback() noexcept
{
    for (int spins = 0;; ++spins)
    {
        auto expected = 0;
        if (owner.compare_exchange_strong (expected, 2, std::memory_order_acq_rel))
            return true;
        WorkerPool::relax(); // the thread is finishing a block
        if ((spins & 1023) == 1023)
            std::this_thread::yield(); // (as WorkerPool's wait: never starve the thread of its core)
    }
}

void RenderAhead::releaseClaim() noexcept
{
    owner.store (0, std::memory_order_release);
}

void RenderAhead::routeLiveEvents (LiveEventQueue* queue) noexcept
{
    if (queue == nullptr)
        return;
    NoteEvent e;
    while (queue->pop (e))
    {
        if (e.channel < 0) // panic, Stop sound: both parts
        {
            localEvents->push (e);
            forward->push (e);
            continue;
        }
        if (e.channel < RackRenderer::maxChannels && current->aheadChannel[(std::size_t) e.channel] != 0)
        {
            forward->push (e); // played there at the next block it renders (a window late)...
            auto known = false;
            for (int i = 0; i < promotedCount; ++i)
                known = known || promotedChannels[(std::size_t) i] == e.channel;
            if (! known && promotedCount < (int) promotedChannels.size())
            {
                promotedChannels[(std::size_t) promotedCount++] = e.channel; // ... and the channel joins the live part
                promotionPending = true;
                counters.promoted.fetch_add (1, std::memory_order_relaxed);
            }
            continue;
        }
        localEvents->push (e);
    }
}

void RenderAhead::renderLive (const RackState& state, const Block& b, Entry& entry, float* left, float* right, MeterBank* meters) noexcept
{
    live.setSplitBlock (&entry.block);
    live.process (&state, b.playing, b.startTick, b.samples, left, right, localEvents.get(), meters, nullptr);
    live.setSplitBlock (nullptr);
    replayMeters (entry, meters);
    counters.splitMismatches.store (live.splitMismatchCount(), std::memory_order_relaxed);
}

// ---- the callback --------------------------------------------------------------------------------------------------

void RenderAhead::process (SnapshotExchange<RackState>& exchange, const Block& b, float* left, float* right, LiveEventQueue* liveQueue,
                           MeterBank* meters, LiveEventQueue* moreLive) noexcept
{
    const auto wall0 = ProcessTimes::now();
    const auto cpu0 = WorkerPool::threadCpuNs();
    processBlock (exchange, b, left, right, liveQueue, meters, moreLive);
    const auto cpu = (double) (WorkerPool::threadCpuNs() - cpu0);
    account ((double) (ProcessTimes::now() - wall0), cpu, b.samples, b.playing, liveState);
}

bool RenderAhead::renderAheadAllowed() const noexcept
{
    return alwaysEngage || probe == Probe::on || probe == Probe::decidedOn;
}

void RenderAhead::account (double wallNs, double cpuNs, int samples, bool playing, const RackState* state) noexcept
{
    if (alwaysEngage || samples <= 0 || sampleRate <= 0.0)
        return;
    const auto seconds = samples / sampleRate;
    const auto over = wallNs * 1.0e-9 > seconds;
    // the rack's CPU in this block: the callback's here, the workers' and the thread's since the last block
    const auto workers = pool != nullptr ? pool->cpuNs() : 0, aheadNs = aheadCpuNs.load (std::memory_order_relaxed);
    const auto rackNs = cpuNs + (double) (workers - lastWorkersCpu) + (double) (aheadNs - lastAheadCpu);
    lastWorkersCpu = workers;
    lastAheadCpu = aheadNs;
    if (! playing)
        return;
    recentOverruns += std::min (1.0, seconds / 2.0) * ((over ? 1.0 / seconds : 0.0) - recentOverruns);
    const auto engaged = engagedNow.load (std::memory_order_relaxed);
    const auto changed = state != nullptr && state->serial != decidedState;
    switch (probe)
    {
        case Probe::none:
            if (! engaged && (recentOverruns >= probeTriggerOverruns || loadAverage >= probeTriggerLoad))
            {
                probe = Probe::off;
                probeLeft = probeSeconds;
                probeRound = 0;
                cpuOff = cpuOn = secondsOff = secondsOn = 0.0;
                overOff = overOn = 0;
                counters.probes.fetch_add (1, std::memory_order_relaxed);
            }
            break;
        case Probe::off:
            if (engaged) // (still draining from the last "on" window)
                break;
            cpuOff += rackNs;
            secondsOff += seconds;
            overOff += over ? 1 : 0;
            if ((probeLeft -= seconds) <= 0.0)
            {
                probe = Probe::on;
                probeLeft = probeSeconds;
                warmupLeft = probeWarmup;
            }
            break;
        case Probe::on:
            if (! engaged)
            {
                // it could not start (nothing to render ahead, backed off): off it is
                if ((warmupLeft -= seconds) < -1.0)
                {
                    probe = Probe::decidedOff;
                    decidedLeft = decidedSeconds;
                    decidedState = state != nullptr ? state->serial : 0;
                    counters.keptOff.fetch_add (1, std::memory_order_relaxed);
                }
                break;
            }
            if ((warmupLeft -= seconds) > 0.0) // (its first blocks fill the window)
                break;
            cpuOn += rackNs;
            secondsOn += seconds;
            overOn += over ? 1 : 0;
            if ((probeLeft -= seconds) <= 0.0)
            {
                if (++probeRound < probeRounds)
                {
                    probe = Probe::off;
                    probeLeft = probeSeconds;
                    drain.store (true, std::memory_order_release); // (off again, cleanly)
                    break;
                }
                const auto cpuShareOff = secondsOff > 0.0 ? cpuOff * 1.0e-9 / secondsOff : 0.0;
                const auto cpuShareOn = secondsOn > 0.0 ? cpuOn * 1.0e-9 / secondsOn : 0.0;
                const auto overRateOff = secondsOff > 0.0 ? overOff / secondsOff : 0.0, overRateOn = secondsOn > 0.0 ? overOn / secondsOn : 0.0;
                counters.probeCpuOff.store ((float) (100.0 * cpuShareOff), std::memory_order_relaxed);
                counters.probeCpuOn.store ((float) (100.0 * cpuShareOn), std::memory_order_relaxed);
                counters.probeOverOff.store ((float) (60.0 * overRateOff), std::memory_order_relaxed);
                counters.probeOverOn.store ((float) (60.0 * overRateOn), std::memory_order_relaxed);
                const auto fewerOverruns = overRateOn <= overRateOff * probeOverrunGain && overRateOff > 0.0;
                const auto lessCpu = cpuShareOn <= cpuShareOff * probeCpuGain;
                // (the owner's rule, no project worse: kept only for a CPU win beyond the probe's measuring error — about
                // ten points in render-ahead's favour against whole runs — with no more overruns. Fewer overruns for a
                // little more CPU (synth100, Exoplanet: 5-20x fewer for +5-9 %) would be `favourFewerOverruns`.)
                const auto keep = (lessCpu && overRateOn <= overRateOff) || (favourFewerOverruns && fewerOverruns && cpuShareOn <= cpuShareOff * probeCpuMargin);
                probe = keep ? Probe::decidedOn : Probe::decidedOff;
                decidedLeft = decidedSeconds;
                decidedState = state != nullptr ? state->serial : 0;
                (keep ? counters.keptOn : counters.keptOff).fetch_add (1, std::memory_order_relaxed);
                if (! keep)
                    drain.store (true, std::memory_order_release);
            }
            break;
        case Probe::decidedOn:
        case Probe::decidedOff:
            if ((decidedLeft -= seconds) <= 0.0 || changed)
            {
                probe = Probe::none; // (measured again when the load asks)
                if (engaged)
                    drain.store (true, std::memory_order_release);
            }
            break;
    }
}

void RenderAhead::processBlock (SnapshotExchange<RackState>& exchange, const Block& b, float* left, float* right, LiveEventQueue* liveQueue,
                                MeterBank* meters, LiveEventQueue* moreLive) noexcept
{

    counters.blocks.fetch_add (1, std::memory_order_relaxed);
    if (targets != nullptr)
        targets->read (targetChannels, targetChannelCount, targetTracks, targetTrackCount, targetVersion);
    // the tweak hold (round 190gq): while edits keep coming the window holds at one block
    if (targets != nullptr && targets->tweakCount() != lastTweaks)
    {
        lastTweaks = targets->tweakCount();
        tweakHoldSamples.store ((int) (sampleRate * tweakHoldMs / 1000), std::memory_order_relaxed);
    }
    else if (const auto hold = tweakHoldSamples.load (std::memory_order_relaxed); hold > 0)
        tweakHoldSamples.store (std::max (0, hold - b.samples), std::memory_order_relaxed);

    const auto* m = b.model;
    if (m != nullptr && m->deviceBlock != lastBlockSize)
    {
        lastBlockSize = m->deviceBlock;
        stableSamples = 0;
    }
    else if (m != nullptr)
        stableSamples = std::min (stableSamples + b.samples, 1 << 30);
    const auto want = renderAheadOption().load (std::memory_order_relaxed) && m != nullptr && m->predictable && b.metering && b.tempoOverride <= 0.0
                      && b.playing && b.samples > 0 && m->deviceBlock > 0 && m->deviceBlock <= maxBlock && ! audioPool.empty();
    // (a device that changes its block size now and then: render-ahead starts again once it has held for half a second)
    const auto steady = stableSamples >= (int) (sampleRate * 0.5) || counters.engagements.load (std::memory_order_relaxed) == 0;

    if (! engagedNow.load (std::memory_order_relaxed))
    {
        const auto* state = exchange.acquire();
        retireHeld (exchange);
        if (! b.playing)
        {
            promotedCount = 0; // (the channels played live join the live part until playback stops)
            stickyOn = false;
        }
        auto engaged = false;
        // (light projects: no deadline to ease, render-ahead would only add its thread)
        // (backed off: not again until the time is up or the project changes)
        if (backoffLeft > 0.0 && b.playing)
            backoffLeft = state != nullptr && state->serial != backoffState ? 0.0 : std::max (0.0, backoffLeft - b.samples / sampleRate);
        // (the probe's "on" window or its verdict; tests: whenever it can)
        const auto needed = renderAheadAllowed() && (alwaysEngage || backoffLeft <= 0.0);
        if (want && steady && needed && state != nullptr && ! state->launchers && held == nullptr)
        {
            // (a state with nothing to render ahead is not looked at again until it, or the live targets, change)
            const auto blocked = engageBlocked && blockedSerial == state->serial && blockedTargets == targetVersion && blockedLiveSeed == promotedCount;
            if (! blocked)
            {
                engaged = engage (*state, b);
                stickyOn = stickyOn || engaged;
                engageBlocked = ! engaged;
                blockedSerial = state->serial;
                blockedTargets = targetVersion;
                blockedLiveSeed = promotedCount;
            }
        }
        if (! engaged)
        {
            const auto start = ProcessTimes::now();
            if (localPending) // (live notes a disengage handed back: they play first, then the queues' own)
            {
                localPending = false;
                for (auto* q : { liveQueue, moreLive })
                    if (q != nullptr)
                        for (NoteEvent e; q->pop (e);)
                            localEvents->push (e);
                live.process (state, b.playing, b.startTick, b.samples, left, right, localEvents.get(), meters, nullptr);
            }
            else
                live.process (state, b.playing, b.startTick, b.samples, left, right, liveQueue, meters, moreLive);
            liveState = state;
            noteLoad ((double) (ProcessTimes::now() - start), b.samples, b.playing);
            return;
        }
    }

    // --- engaged
    routeLiveEvents (liveQueue);
    routeLiveEvents (moreLive);

    const auto hardStop = ! b.playing || m == nullptr || ! m->predictable || ! b.metering || b.tempoOverride > 0.0 || b.samples <= 0
                          || m->deviceBlock != stride || audioPool.empty();
    if (hardStop)
    {
        // Stop, an export, an external clock, a device change: what was rendered ahead is dropped and the live renderer
        // renders everything again
        claimForCallback();
        const auto dropped = writeIndex.load (std::memory_order_relaxed) != readIndex.load (std::memory_order_relaxed);
        disengage (liveState, dropped);
        releaseClaim();
        if (! b.playing)
            promotedCount = 0;
        const auto* state = exchange.acquire();
        retireHeld (exchange);
        localPending = false;
        live.process (state, b.playing, b.startTick, b.samples, left, right, localEvents.get(), meters, nullptr);
        liveState = state;
        return;
    }
    if (! want || ! renderAheadAllowed()) // the option switched off while playing, the probe's "off": drained, then off
        drain.store (true, std::memory_order_release);

    // a new snapshot: the live part takes it now when the part rendered ahead can go on as it is (same units, same
    // timeline); else both take it together once the ring has drained
    if (held == nullptr && ! pendingSync && exchange.hasIncoming())
    {
        tweakHoldSamples.store ((int) (sampleRate * tweakHoldMs / 1000), std::memory_order_relaxed);
        const RackState* previous = nullptr;
        const auto* s2 = exchange.acquireHolding (previous);
        if (s2 != liveState && previous != nullptr)
        {
            held = previous;
            const auto& s1 = *liveState;
            auto compatible = s2->song.channels.size() == s1.song.channels.size() && s2->song.mixer.size() == s1.song.mixer.size()
                              && ! s2->channelsMoved && s2->sampleRate == s1.sampleRate && ! s2->launchers && s2->song.bpm == s1.song.bpm
                              && s2->song.mode == s1.song.mode && s2->song.currentPattern == s1.song.currentPattern && s2->tempoMap == s1.tempoMap
                              && s2->tempoAutomation == s1.tempoAutomation && s2->song.beatsPerBar == s1.song.beatsPerBar
                              && s2->schedule.songLength == s1.schedule.songLength;
            if (compatible)
                compatible = computePartition (*s2, b, *next) && next->sameUnits (*current);
            if (compatible)
            {
                liveState = s2;
                aheadNext.store (s2, std::memory_order_release);
                wakeThread(); // (it takes the new state at its next block, even with the ring full)
                counters.statesSwapped.fetch_add (1, std::memory_order_relaxed);
            }
            else
            {
                pendingSync = true;
                drain.store (true, std::memory_order_release);
            }
        }
    }
    retireHeld (exchange);

    // the live targets, a channel played live, an editor's live meters: a new partition at the next sync
    {
        static_assert (editorPollSamples > 0);
        auto partitionChanged = false;
        if (promotionPending || targetVersion != partitionTargets)
        {
            promotionPending = false;
            partitionTargets = targetVersion;
            partitionChanged = true;
        }
        editorPoll += b.samples;
        if (editorPoll >= editorPollSamples)
        {
            editorPoll = 0;
            partitionChanged = true;
        }
        if (partitionChanged && ! drain.load (std::memory_order_relaxed))
        {
            const auto ok = computePartition (*liveState, b, *next);
            if (! ok || ! next->sameUnits (*current) || next->editorHash != current->editorHash)
            {
                if (! ok || ! next->sameUnits (*current))
                {
                    drain.store (true, std::memory_order_release);
                    counters.partitionChanges.fetch_add (1, std::memory_order_relaxed);
                }
                else
                    current->editorHash = next->editorHash;
            }
        }
    }
    // the transport options and the input's flags the ahead part renders with: a change waits for a sync
    {
        const auto recording = liveInput != nullptr && liveInput->recording.load (std::memory_order_relaxed);
        const auto linear = liveInput != nullptr && liveInput->linearSong.load (std::memory_order_relaxed);
        const auto origin = liveInput != nullptr ? liveInput->patternOrigin.load (std::memory_order_relaxed) : -1.0;
        if (recording != jobRecording || linear != jobLinear || origin != jobPatternOrigin || b.playTruncated != jobBlock.playTruncated
            || b.resetPlugins != jobBlock.resetPlugins || (m != nullptr && ! (*m == lastModel)))
            drain.store (true, std::memory_order_release);
    }

    // --- this block's part rendered ahead: from the ring, from the thread finishing it, or rendered here
    Entry* entry = nullptr;
    auto fromRing = false;
    auto claimed = false;
    for (;;)
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        const auto w = writeIndex.load (std::memory_order_acquire);
        if (r != w)
        {
            auto& front = *entries[(std::size_t) (r % entryCount)];
            if (front.playing == b.playing && front.start == b.startTick && front.samples == b.samples)
            {
                entry = &front;
                fromRing = true;
                break;
            }
            // not the block the callback plays (a jump, Stop, a change of the loop...): dropped
            claimForCallback();
            claimed = true;
            dropRing (*liveState);
            break;
        }
        if (owner.load (std::memory_order_acquire) == 1)
        {
            // the thread is rendering it: wait for it (the renderer can't do two blocks at once)
            counters.waited.fetch_add (1, std::memory_order_relaxed);
            if (tweakHoldSamples.load (std::memory_order_relaxed) == 0) // (a drag holds the window at one block: waits are expected)
                ++windowWaits;
            for (int spins = 0; owner.load (std::memory_order_acquire) == 1; ++spins)
            {
                WorkerPool::relax();
                if ((spins & 1023) == 1023)
                    std::this_thread::yield();
            }
            continue;
        }
        claimForCallback();
        claimed = true;
        if (readIndex.load (std::memory_order_relaxed) != writeIndex.load (std::memory_order_acquire))
        {
            releaseClaim(); // (it published one meanwhile)
            claimed = false;
            continue;
        }
        break;
    }

    if (entry == nullptr)
    {
        // the ring is empty and the callback holds the ahead renderer: a sync point
        if (drain.load (std::memory_order_acquire))
        {
            counters.drains.fetch_add (1, std::memory_order_relaxed);
            // both parts stop here together; the live renderer renders this block alone, and render-ahead starts again
            // at the next block with the new snapshot / partition / options
            disengage (liveState, false);
            releaseClaim();
            pendingSync = false;
            engageBlocked = false;
            const auto* state = exchange.acquire();
            retireHeld (exchange);
            if (std::exchange (backoffPending, false) && state != nullptr)
            {
                backoffLeft = backoffSeconds; // (backed off: off for a while, or until the project changes)
                backoffState = state->serial;
            }
            localPending = false;
            live.process (state, b.playing, b.startTick, b.samples, left, right, localEvents.get(), meters, nullptr);
            liveState = state;
            return;
        }
        // render the part rendered ahead here, now (the start, after a drop, or the thread fell behind)
        if (const auto* s = aheadNext.load (std::memory_order_acquire); s != nullptr && s != aheadState)
        {
            aheadState = s;
            aheadAck.store (s, std::memory_order_release);
        }
        auto& slot = *entries[(std::size_t) (writeIndex.load (std::memory_order_relaxed) % entryCount)];
        jobBlock = b; // (the transport options as they are now: a user's jump chases in both parts)
        renderJob (slot, *aheadState, b.startTick, b.samples, b.playing, pool != nullptr);
        entry = &slot;
        counters.inline_.fetch_add (1, std::memory_order_relaxed);
        if (tweakHoldSamples.load (std::memory_order_relaxed) == 0)
            ++windowWaits;
        // the thread goes on after this block: where the callback will put the position
        const auto sb = planSubBlock (*m, b.startTick, m->offset, b.tps);
        if (sb.start == b.startTick && sb.samples == b.samples)
        {
            nextPosition = sb.next;
            nextDone = m->offset + b.samples >= m->deviceBlock ? 0 : m->offset + b.samples;
            modelStop.store (sb.last, std::memory_order_release);
        }
        else
            modelStop.store (true, std::memory_order_release); // (the transport moved as the model does not: inline until a sync)
    }
    else
        counters.fromRing.fetch_add (1, std::memory_order_relaxed);

    const auto liveStart = ProcessTimes::now();
    renderLive (*liveState, b, *entry, left, right, meters);
    pinned.store (live.currentBpm(), std::memory_order_relaxed);
    // the whole rack's load while on (the live part + what the thread rendered since the last block): a long quiet
    // stretch hands everything back to the callback
    noteLoad ((double) (ProcessTimes::now() - liveStart + aheadBusyNs.exchange (0, std::memory_order_relaxed)), b.samples, true);
    if (! alwaysEngage && idleFor >= idleSeconds && ! drain.load (std::memory_order_relaxed))
    {
        idleFor = 0.0;
        stickyOn = false;
        drain.store (true, std::memory_order_release); // (disengaged cleanly at the next sync)
        engageBlocked = false;
    }
    // the back-off rules
    {
        const auto seconds = b.samples / sampleRate;
        heavyFor = serialLoadSeen.load (std::memory_order_relaxed) >= serialLimit ? heavyFor + seconds : 0.0;
        ++windowBlocks;
        windowSeconds += seconds;
        // (only once the probe has kept it on: the probe itself measures the "on" window)
        auto backOff = false;
        if (windowSeconds >= backoffWindowSeconds)
        {
            backOff = windowWaits > backoffWaitShare * windowBlocks;
            windowBlocks = windowWaits = 0;
            windowSeconds = 0.0;
        }
        if (backOff && probe == Probe::decidedOn && ! alwaysEngage && ! drain.load (std::memory_order_relaxed))
        {
            heavyFor = 0.0;
            stickyOn = false;
            backoffPending = true;
            drain.store (true, std::memory_order_release);
            engageBlocked = false;
            counters.backoffs.fetch_add (1, std::memory_order_relaxed);
            probe = Probe::none; // (the window stopped absorbing anything: measured again)
        }
    }

    if (fromRing)
        readIndex.store (readIndex.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    if (claimed)
    {
        if (pool != nullptr && (drain.load (std::memory_order_relaxed) || modelStop.load (std::memory_order_relaxed)))
            pool->park();
        releaseClaim();
    }
    if (! drain.load (std::memory_order_relaxed))
        running.store (true, std::memory_order_release);
    // the thread refills the ring from half the window: at small buffers one wake-up every few blocks, not one per block
    const auto fill = writeIndex.load (std::memory_order_relaxed) - readIndex.load (std::memory_order_relaxed);
    const auto target = tweakHoldSamples.load (std::memory_order_relaxed) > 0 ? 1 : targetFill;
    if (claimed || fill <= target / 2)
        wakeThread();
}
} // namespace lynx
