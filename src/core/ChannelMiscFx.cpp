#include "core/ChannelMiscFx.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace lynx::channelmisc
{
double flTimeKnobBeats (int v) noexcept
{
    if (v <= 0)
        return 0.0;
    return (std::pow (27.0, v / 1024.0) - 1.0) * 48.0 / 26.0 / 192.0;
}

int flTimeKnobValue (double beats) noexcept
{
    if (beats <= 0.0)
        return 0;
    return (int) std::lround (1024.0 * std::log (beats * 192.0 * 26.0 / 48.0 + 1.0) / std::log (27.0));
}

namespace
{
// FL's note velocity → volume (REA_rodada76 §1): x = round(velocity · 128) · 0.01; 1 at x = 1, else (11^x − 1) · 0.1
double velocityVolume (double velocity128) noexcept
{
    const auto x = std::nearbyint (velocity128) * 0.01;
    return x == 1.0 ? 1.0 : (std::exp (x * std::log (11.0)) - 1.0) * 0.1;
}
} // namespace

float trackedPan (const ChannelMisc& misc, int key, float velocity, float notePan) noexcept
{
    auto pan = (double) notePan;
    if (misc.key.pan != 0.0f)
        pan += (double) (key - (120 - misc.key.mid)) / 48.0 * misc.key.pan;
    if (misc.velocity.pan != 0.0f)
        pan += 0.275 * misc.velocity.pan * (velocityVolume (velocity * 128.0) - velocityVolume ((double) misc.velocity.mid));
    return (float) std::clamp (pan, -1.0, 1.0);
}

std::pair<float, float> trackedModulation (const ChannelMisc& misc, int key, float velocity, float modX, float modY) noexcept
{
    // the same key / velocity distances as the pan above, each scaled by its Mod X / Y knob ([model], round 151)
    const auto keyAmount = (double) (key - (120 - misc.key.mid)) / 48.0;
    const auto velocityAmount = 0.275 * (velocityVolume (velocity * 128.0) - velocityVolume ((double) misc.velocity.mid));
    auto x = (double) modX + 0.5 * (keyAmount * misc.key.modX + velocityAmount * misc.velocity.modX);
    auto y = (double) modY + 0.5 * (keyAmount * misc.key.modY + velocityAmount * misc.velocity.modY);
    return { (float) std::clamp (x, 0.0, 1.0), (float) std::clamp (y, 0.0, 1.0) };
}

std::vector<int> chordFromFlMap (const std::string& mapText, int index)
{
    if (index <= 0)
        return {};
    std::istringstream in (mapText);
    std::string line;
    for (int i = 0; std::getline (in, line); ++i)
    {
        if (i != index)
            continue;
        std::vector<int> chord { 0 };
        std::istringstream fields (line);
        std::string field;
        for (int f = 0; std::getline (fields, field, ','); ++f)
        {
            if (f < 2)
                continue; // the names
            try
            {
                chord.push_back (chord.back() + std::stoi (field));
            }
            catch (...)
            {
                return {};
            }
        }
        return chord.size() > 1 ? chord : std::vector<int> {};
    }
    return {};
}

void NoteEcho::schedule (const Pending& p) noexcept
{
    if (pendingCount < maxPending)
        pending[(std::size_t) pendingCount++] = p;
}

void NoteEcho::process (int channel, const NoteEvent* in, int inCount, double startTick, double tps, int n,
                        const EchoSettings& s, NoteEvent* out, float* gains, int& outCount) noexcept
{
    outCount = 0;
    auto emit = [&] (const NoteEvent& e, float gain) {
        if (outCount < maxEvents)
        {
            out[outCount] = e;
            gains[outCount] = gain;
            ++outCount;
        }
    };
    const auto endTick = startTick + n * tps;
    const auto delay = std::max (1.0, (double) s.timeBeats * ppq);
    const auto echoes = std::clamp (s.echoes, 0, 16);

    // the pending echoes due before `tick`, in order
    auto flush = [&] (double tick) {
        while (true)
        {
            auto best = -1;
            for (int i = 0; i < pendingCount; ++i)
                if (pending[(std::size_t) i].tick < tick && (best < 0 || pending[(std::size_t) i].tick < pending[(std::size_t) best].tick))
                    best = i;
            if (best < 0)
                return;
            auto p = pending[(std::size_t) best];
            pending[(std::size_t) best] = pending[(std::size_t) --pendingCount];
            p.event.sampleOffset = std::clamp ((int) std::floor ((std::max (p.tick, startTick) - startTick) / tps), 0, n - 1);
            emit (p.event, p.gain);
        }
    };

    for (int i = 0; i < inCount; ++i)
    {
        const auto& e = in[i];
        const auto tick = startTick + e.sampleOffset * tps;
        flush (tick + 1e-9);
        emit (e, 1.0f);

        if (e.channel != channel || ! s.enabled())
            continue;

        if (e.type == NoteEvent::Type::on)
        {
            Held h;
            h.key = e.key;
            h.onTick = tick;
            h.noteId = e.noteId;
            // FL's fat mode (REA round 149 [DECOMP FLEngine FUN_00d28c90, the channel echo generator]): a second
            // series mirrors the first (the pan and pitch steps negated) and every echo plays at feedback × the note's
            // level instead of decaying (feedback^k)
            const auto series = s.fat ? 2 : 1;
            const auto feedback = std::clamp (s.feedback, 0.0f, 2.0f);
            int count = 0;
            for (int pass = 0; pass < series; ++pass)
            {
                const auto sign = pass == 0 ? 1.0 : -1.0;
                auto gain = 1.0f;
                for (int k = 1; k <= echoes; ++k)
                {
                    gain = s.fat ? feedback : gain * feedback;
                    auto echo = e;
                    echo.echo = true;
                    const auto cents = sign * (double) s.pitch * k;
                    const auto semis = (int) std::trunc (cents / 100.0);
                    echo.key = std::clamp (e.key + semis, 0, 127);
                    echo.cents = e.cents + (float) (cents - semis * 100.0);
                    // the pan moves k steps; ping pong bounces it off the ends, else it stays there [EXEC]
                    auto pan = (double) e.pan + sign * (double) s.pan * k;
                    if (s.pingPong)
                    {
                        pan = std::fmod (pan + 1.0, 4.0);
                        if (pan < 0.0)
                            pan += 4.0;
                        pan = pan <= 2.0 ? pan - 1.0 : 3.0 - pan;
                    }
                    echo.pan = (float) std::clamp (pan, -1.0, 1.0);
                    // Mod X / Y move k steps too (round 151 [model]: FL's −1..1 knob is the step in FL's ±1 level
                    // units, half the note's 0..1 range; not mirrored by fat mode, which negates pan and pitch only)
                    echo.modX = (float) std::clamp ((double) e.modX + 0.5 * s.modX * k, 0.0, 1.0);
                    echo.modY = (float) std::clamp ((double) e.modY + 0.5 * s.modY * k, 0.0, 1.0);
                    // each repeat is a voice of its own in FL: its own identity, so the note's end (or another
                    // repeat's) does not end it (round 190ej); the k-th entry's "off" below derives the same id
                    echo.noteId = noteEchoId (e.noteId, count + 1);
                    if (count < (int) h.echoKeys.size())
                        h.echoKeys[(std::size_t) count++] = echo.key;
                    schedule ({ tick + k * delay, echo, gain });
                }
            }
            h.count = count;
            if (heldCount < (int) held.size())
                held[(std::size_t) heldCount++] = h;
        }
        else if (e.type == NoteEvent::Type::off)
        {
            // the note this "off" ends (round 190ej: by its identity), else the oldest held note of this key
            auto found = -1;
            for (int j = 0; j < heldCount && e.noteId != 0; ++j)
                if (held[(std::size_t) j].noteId == e.noteId && held[(std::size_t) j].key == e.key)
                    found = j;
            const auto byId = found >= 0;
            for (int j = 0; j < heldCount && ! byId; ++j)
                if (held[(std::size_t) j].key == e.key && (found < 0 || held[(std::size_t) j].onTick < held[(std::size_t) found].onTick))
                    found = j;
            if (found < 0)
                continue;
            const auto h = held[(std::size_t) found];
            held[(std::size_t) found] = held[(std::size_t) --heldCount];
            for (int i = 0; i < h.count; ++i)
            {
                auto off = e;
                off.echo = true;
                off.key = h.echoKeys[(std::size_t) i];
                off.noteId = noteEchoId (h.noteId, i + 1);
                const auto k = echoes > 0 ? i % echoes + 1 : 1;
                schedule ({ tick + k * delay, off, 1.0f }); // the echo keeps the note's length
            }
        }
    }
    flush (endTick);
}
} // namespace lynx::channelmisc
