#include "core/Pdc.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lynx
{
bool PdcPlan::any() const noexcept
{
    if (std::any_of (channelDelay.begin(), channelDelay.end(), [] (int d) { return d > 0; }))
        return true;

    for (const auto& t : sideDelay)
        for (const auto& s : t)
            if (std::any_of (s.begin(), s.end(), [] (int d) { return d > 0; }))
                return true;

    for (const auto& t : sendDelay)
        if (std::any_of (t.begin(), t.end(), [] (int d) { return d > 0; }))
            return true;

    return false;
}

int manualLatencySamples (const MixerTrack& track, double sampleRate) noexcept
{
    const auto ms = std::clamp (track.latencyMs, -maxTrackLatencyMs, maxTrackLatencyMs);
    return ms == 0.0f || ! (sampleRate > 0.0) ? 0 : (int) std::lround ((double) ms * sampleRate * 0.001);
}

int insertEffectsLatency (const PdcPlan& plan, const Song& song, int track, double sampleRate) noexcept
{
    if (track < 0 || (std::size_t) track >= plan.outLat.size() || (std::size_t) track >= plan.inAlign.size() || (std::size_t) track >= song.mixer.size())
        return 0;
    // (round 190my: levelLag leaves out FL's Current track run after the insert's fader; the take is tapped before it)
    if ((std::size_t) track < plan.levelLag.size())
        return std::max (0, plan.levelLag[(std::size_t) track] - plan.inAlign[(std::size_t) track]);
    return std::max (0, plan.outLat[(std::size_t) track] - plan.inAlign[(std::size_t) track]
                            - manualLatencySamples (song.mixer[(std::size_t) track], sampleRate));
}

std::array<int, 4> sideSources (const Song& song, int track, const EffectSlot& slot)
{
    const auto n = (int) song.mixer.size();
    auto valid = [&] (int source) { return source > 0 && source < n && source != track ? source : -1; };
    std::array<int, 4> s { valid (slot.sidechain), valid (slot.sidechain2), -1, -1 };
    if (slot.kind == EffectKind::plugin)
        if (const auto c = slotConnections (song, track, slot); c.routed)
        {
            s[2] = valid (c.mainSource);
            s[3] = valid (c.sideSource);
        }
    return s;
}

PdcPlan computePdc (const Song& song, const std::vector<int>& channelLatency, const std::vector<std::vector<int>>& slotLatency,
                    double sampleRate, int currentSource)
{
    const auto tracks = std::max<int> (1, (int) song.mixer.size());
    PdcPlan plan;
    plan.inAlign.assign ((std::size_t) tracks, 0);
    plan.outLat.assign ((std::size_t) tracks, 0);
    plan.channelDelay.assign (song.channels.size(), 0);
    plan.sendDelay.assign ((std::size_t) tracks, {});

    // FL's Plugin delay compensation › Automatic off (REA round 168j, FUN_010ff3b0 behind the 0x1D global): the plan leaves
    // out the instruments' and the effects' latencies; the manual track latencies and the routing still count
    const auto automatic = song.autoPdc;
    auto channelLat = [&] (std::size_t c) { return automatic && c < channelLatency.size() ? std::max (0, channelLatency[c]) : 0; };
    auto slotLat = [&] (int t, std::size_t s) { return automatic ? std::max (0, slotLatency[(std::size_t) t][s]) : 0; };
    auto trackOf = [&] (const ChannelDef& c) { return std::clamp (c.mixerTrack, 0, tracks - 1); };

    // own input latency: the slowest instrument feeding the track
    for (std::size_t c = 0; c < song.channels.size(); ++c)
    {
        auto& in = plan.inAlign[(std::size_t) trackOf (song.channels[c])];
        in = std::max (in, channelLat (c));
    }

    auto effectsLatency = [&] (int t) {
        int sum = 0;

        if (t < (int) song.mixer.size() && t < (int) slotLatency.size())
        {
            const auto& slots = song.mixer[(std::size_t) t].effects;

            for (std::size_t s = 0; s < slots.size() && s < slotLatency[(std::size_t) t].size(); ++s)
                if (song.slotActive (slots[s]))
                    sum += slotLat (t, s);
        }

        return sum;
    };

    // the latency of a Fruity Send slot's signal: the insert's input plus the enabled slots before it
    auto tapLatency = [&] (int t, int slot, bool afterSlot = false) {
        int sum = plan.inAlign[(std::size_t) t];
        if (t < (int) slotLatency.size())
        {
            const auto& slots = song.mixer[(std::size_t) t].effects;
            const auto end = afterSlot ? slot + 1 : slot; // (a Transmitter's sends leave after its own latency)
            for (int s = 0; s < end && s < (int) slots.size() && s < (int) slotLatency[(std::size_t) t].size(); ++s)
                if (song.slotActive (slots[(std::size_t) s]))
                    sum += slotLat (t, (std::size_t) s);
        }
        return sum;
    };

    // inserts in order: when T is processed, all its sources already have been
    const auto order = song.mixerOrder();

    auto manual = [&] (int t) { return t < (int) song.mixer.size() ? manualLatencySamples (song.mixer[(std::size_t) t], sampleRate) : 0; };

    // round 190hq (B13, complaint "side-chain out of sync"): a slot's side inputs (side-chain, Connections) are the
    // other inserts' outputs, as late as their outLat. FL's per-edge plan left them out, so a latent effect on the
    // source (a kick through a limiter) or before the listening slot made the ducking early or late. The listening
    // insert is aligned so that each source arrives no later than the slot's own input (its inAlign plus the active
    // slots before it); sideDelay then holds what is missing. Sources come first in the mixer order (Song::mixerOrder
    // counts side-chains; Connections sources are routed into the insert).
    std::vector<char> planned ((std::size_t) tracks, 0);
    std::vector<std::vector<std::array<int, 4>>> sources ((std::size_t) tracks);
    auto alignSides = [&] (int t) {
        if (t >= (int) song.mixer.size())
            return;
        const auto& slots = song.mixer[(std::size_t) t].effects;
        auto& list = sources[(std::size_t) t];
        list.assign (slots.size(), { -1, -1, -1, -1 });
        int before = 0; // the active slots before this one
        for (std::size_t s = 0; s < slots.size(); ++s)
        {
            list[s] = sideSources (song, t, slots[s]);
            for (const auto source : list[s])
                if (source > 0 && planned[(std::size_t) source] != 0)
                    plan.inAlign[(std::size_t) t] = std::max (plan.inAlign[(std::size_t) t], plan.outLat[(std::size_t) source] - before);
            if (song.slotActive (slots[s]) && t < (int) slotLatency.size() && s < slotLatency[(std::size_t) t].size())
                before += slotLat (t, s);
        }
    };

    // FL's Current (C) track (round 190my, DF-82): its slots run in line on its source's output, after the source's
    // fader, so its latency makes the source's output that much later (REA 190mv R: FUN_010ff3b0 / FUN_010ffca0)
    int currentIndex = -1;
    for (int t = (int) song.mixer.size() - 1; t > 0 && currentIndex < 0; --t)
        if (song.mixer[(std::size_t) t].current)
            currentIndex = t;
    const auto currentOn = currentIndex > 0 && currentSource >= 0 && currentSource < tracks && currentSource != currentIndex;
    // (REA 190my R2, FUN_010ff3b0: C's latency +0xc4 = its plugins' latencies plus its manual track latency; it has no
    // input of its own)
    plan.currentLatency = currentIndex > 0 ? effectsLatency (currentIndex) + manual (currentIndex) : 0;
    const auto currentLatency = currentOn ? plan.currentLatency : 0;
    auto currentAdds = [&] (int t) { return currentOn && t == currentSource ? currentLatency : 0; };

    for (auto t : order)
    {
        if (t == currentIndex)
            continue; // (no input, no route: done after its source below)
        alignSides (t);
        plan.outLat[(std::size_t) t] = plan.inAlign[(std::size_t) t] + effectsLatency (t) + manual (t) + currentAdds (t);
        planned[(std::size_t) t] = 1;
        const auto& track = song.mixer[(std::size_t) t];

        if (track.toMaster)
            plan.inAlign[0] = std::max (plan.inAlign[0], plan.outLat[(std::size_t) t]);

        for (const auto& s : track.sends)
            if (s.target > 0 && s.target < tracks)
                plan.inAlign[(std::size_t) s.target] = std::max (plan.inAlign[(std::size_t) s.target], plan.outLat[(std::size_t) t]);

        // Fruity Send slots leave the chain after the slots before them
        for (const auto& tap : fruitySendsOf (song, t))
            plan.inAlign[(std::size_t) tap.target] = std::max (plan.inAlign[(std::size_t) tap.target], tapLatency (t, tap.slot, tap.afterSlot));
    }

    alignSides (0);
    plan.outLat[0] = plan.inAlign[0] + effectsLatency (0) + manual (0) + currentAdds (0);
    plan.total = std::max (0, plan.outLat[0]);

    // the Current track's own slots see its source's output as it leaves the source's fader (their automation lags)
    if (currentIndex > 0)
    {
        planned[(std::size_t) currentIndex] = 1;
        if (currentOn)
        {
            plan.inAlign[(std::size_t) currentIndex] = plan.outLat[(std::size_t) currentSource] - currentLatency;
            plan.outLat[(std::size_t) currentIndex] = plan.outLat[(std::size_t) currentSource];
        }
    }

    plan.slotInput.assign ((std::size_t) tracks, {});
    for (int t = 0; t < (int) song.mixer.size(); ++t)
    {
        const auto& slots = song.mixer[(std::size_t) t].effects;
        auto& lags = plan.slotInput[(std::size_t) t];
        lags.assign (slots.size(), 0);
        auto run = plan.inAlign[(std::size_t) t];
        for (std::size_t s = 0; s < slots.size(); ++s)
        {
            lags[s] = run;
            if (song.slotActive (slots[s]) && t < (int) slotLatency.size() && s < slotLatency[(std::size_t) t].size())
                run += slotLat (t, s);
        }
    }

    plan.levelLag.assign ((std::size_t) tracks, 0);
    for (int t = 0; t < tracks; ++t)
        plan.levelLag[(std::size_t) t] = std::max (0, plan.outLat[(std::size_t) t] - manual (t) - currentAdds (t));

    // the side inputs' delays (sources bypassed "Always" are not delayed, as their routes)
    plan.sideDelay.assign ((std::size_t) tracks, {});
    for (int t = 0; t < (int) song.mixer.size(); ++t)
    {
        const auto& list = sources[(std::size_t) t];
        auto& delays = plan.sideDelay[(std::size_t) t];
        delays.assign (list.size(), { 0, 0, 0, 0 });
        for (std::size_t s = 0; s < list.size() && s < plan.slotInput[(std::size_t) t].size(); ++s)
            for (std::size_t k = 0; k < 4; ++k)
                if (const auto source = list[s][k];
                    source > 0 && planned[(std::size_t) source] != 0 && song.mixer[(std::size_t) source].latencyBypass != LatencyBypass::always)
                    delays[s][k] = std::max (0, plan.slotInput[(std::size_t) t][s] - plan.outLat[(std::size_t) source]);
    }

    // per-edge delays (what's missing to reach the destination's alignment)
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        plan.channelDelay[c] = plan.inAlign[(std::size_t) trackOf (song.channels[c])] - channelLat (c);

    for (int t = 1; t < (int) song.mixer.size(); ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];
        auto& delays = plan.sendDelay[(std::size_t) t];
        const auto taps = fruitySendsOf (song, t);
        delays.assign (1 + track.sends.size() + taps.size(), 0);
        // bypassed "Always": its routes are not delayed (FL's Fruity Send slots still are)
        const auto bypassed = track.latencyBypass == LatencyBypass::always;
        delays[0] = track.toMaster && ! bypassed ? std::max (0, plan.inAlign[0] - plan.outLat[(std::size_t) t]) : 0;

        for (std::size_t i = 0; i < track.sends.size(); ++i)
        {
            const auto target = track.sends[i].target;

            if (target > 0 && target < tracks && ! bypassed)
                delays[1 + i] = std::max (0, plan.inAlign[(std::size_t) target] - plan.outLat[(std::size_t) t]);
        }

        for (std::size_t j = 0; j < taps.size(); ++j)
            delays[1 + track.sends.size() + j] = std::max (0, plan.inAlign[(std::size_t) taps[j].target] - tapLatency (t, taps[j].slot, taps[j].afterSlot));
    }

    return plan;
}

DelayLine::DelayLine (int delaySamples)
    : delay (std::max (1, delaySamples)),
      left ((std::size_t) delay, 0.0f),
      right ((std::size_t) delay, 0.0f),
      quiet (delay)
{
}

void DelayLine::process (float* l, float* r, int n) noexcept
{
    // in contiguous ring segments: swap what comes in for what goes out
    for (int done = 0; done < n;)
    {
        const auto run = std::min (n - done, delay - position);
        auto* bl = left.data() + position;
        auto* br = right.data() + position;

        for (int i = 0; i < run; ++i)
        {
            std::swap (bl[i], l[done + i]);
            std::swap (br[i], r[done + i]);
        }

        done += run;
        position = (position + run) % delay;
    }

    quiet = 0;
}

void DelayLine::clear() noexcept
{
    if (quiet >= delay)
        return;
    std::memset (left.data(), 0, sizeof (float) * (std::size_t) delay);
    std::memset (right.data(), 0, sizeof (float) * (std::size_t) delay);
    quiet = delay;
}

void DelayLine::mixInto (const float* sl, const float* sr, float level, float* dl, float* dr, int n) noexcept
{
    if (sl == nullptr)
    {
        if (quiet >= delay)
            return; // all zeros inside: the output would be silence and the position doesn't matter

        quiet += n;
    }
    else
    {
        quiet = 0;
    }

    for (int done = 0; done < n;)
    {
        const auto run = std::min (n - done, delay - position);
        auto* bl = left.data() + position;
        auto* br = right.data() + position;
        auto* ol = dl + done;
        auto* orr = dr + done;

        if (sl != nullptr)
        {
            const auto* il = sl + done;
            const auto* ir = sr + done;

            for (int i = 0; i < run; ++i)
            {
                ol[i] += bl[i];
                orr[i] += br[i];
                bl[i] = il[i] * level;
                br[i] = ir[i] * level;
            }
        }
        else
        {
            for (int i = 0; i < run; ++i)
            {
                ol[i] += bl[i];
                orr[i] += br[i];
                bl[i] = 0.0f;
                br[i] = 0.0f;
            }
        }

        done += run;
        position = (position + run) % delay;
    }
}
} // namespace lynx
