#include "core/MarkerPlan.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Song.h"

namespace lynx
{
namespace
{
// the markers of the current arrangement in tick order (Song::markers() is kept sorted)
const std::vector<Marker>& sorted (const Song& song) { return song.markers(); }

std::int64_t nextMarkerTick (const std::vector<Marker>& list, std::size_t i, std::int64_t songEnd)
{
    for (auto k = i + 1; k < list.size(); ++k)
        if (list[k].tick > list[i].tick)
            return list[k].tick;
    return songEnd;
}
} // namespace

MarkerPlan makeMarkerPlan (const Song& song)
{
    MarkerPlan plan;
    const auto& list = sorted (song);
    const auto songEnd = song.songLength();
    plan.songEnd = songEnd;
    auto hasLoop = false;

    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& m = list[i];
        MarkerJump jump;
        jump.at = m.tick;

        switch (m.action)
        {
            case Marker::markerLoop:
                // the section from this marker to the next one (or the song's end) repeats: at its end playback goes
                // back to the marker [SHOT fl_marker_loop_section.png]
                jump.at = nextMarkerTick (list, i, songEnd);
                jump.to = m.tick;
                break;
            case Marker::markerSkip:
                // the section from this marker to the next one is skipped [SHOT fl_skip_pause_play.png]
                jump.to = nextMarkerTick (list, i, songEnd);
                break;
            case Marker::markerPause:
                // playback stops on the marker; Play goes on from there [SHOT fl_pause_resume.png] (round 190ri: `to` is
                // the marker itself; it was 0, so after the pause Play went on from the song's start)
                jump.kind = MarkerJump::Kind::pause;
                jump.to = m.tick;
                break;
            case Marker::songLoop:
                // the song's end goes back here [SHOT fl_start_loop_play.png]
                plan.wrapTo = m.tick;
                hasLoop = true;
                continue;
            case Marker::start:
                // Play from the song's beginning starts here [SHOT fl_start_loop_play.png, fl_combo2_first.png]
                if (plan.start < 0)
                    plan.start = m.tick;
                continue;
            default:
                continue;
        }

        if (jump.kind == MarkerJump::Kind::jump && jump.to == jump.at)
            continue; // nowhere to go
        if (jump.at <= 0 && jump.kind == MarkerJump::Kind::pause)
            continue;
        if (plan.count < MarkerPlan::maxJumps)
            plan.jumps[(std::size_t) plan.count++] = jump;
    }

    // without a Loop marker the song starts over at the Start marker (FL numbers the bars from it) [HYP]
    if (! hasLoop && plan.start > 0)
        plan.wrapTo = plan.start;

    std::stable_sort (plan.jumps.begin(), plan.jumps.begin() + plan.count, [] (const MarkerJump& a, const MarkerJump& b) { return a.at < b.at; });

    // Round 190ri (kainproctor, Discord 11-25; FL 2026 live, docs/analise/REA_rodada190ri.md): a skip lands on the next marker,
    // and when that is a Marker skip too FL skips its section as well (skips at bars 3 and 5, a marker at 7: bars 1-2 then 7),
    // as its render does; landing on a Marker pause stops there. Playback only meets a jump AFTER the position, so a skip
    // that landed on another one never fired: the chain is followed here, once
    for (int i = 0; i < plan.count; ++i)
    {
        auto& j = plan.jumps[(std::size_t) i];
        if (j.kind != MarkerJump::Kind::jump || j.to <= j.at)
            continue; // (a marker loop jumps back to its own marker: nothing to chain)
        for (int guard = 0; guard < MarkerPlan::maxJumps; ++guard)
        {
            const MarkerJump* next = nullptr;
            for (int k = 0; k < plan.count && next == nullptr; ++k)
                if (const auto& o = plan.jumps[(std::size_t) k]; o.at == j.to && (o.kind == MarkerJump::Kind::pause || (o.kind == MarkerJump::Kind::jump && o.to > o.at)))
                    next = &o;
            if (next == nullptr)
                break;
            if (next->kind == MarkerJump::Kind::pause)
            {
                j.kind = MarkerJump::Kind::jumpThenPause;
                break;
            }
            j.to = next->to;
        }
    }
    return plan;
}

MarkerStep markerStep (const MarkerPlan& plan, double position, int n, double tps, double pendingAt) noexcept
{
    MarkerStep step;
    step.samples = n;
    if (n <= 0 || tps <= 0.0)
        return step;
    const auto end = position + n * tps;
    step.jump = plan.firstIn (position, end);
    if (pendingAt >= 0.0 && pendingAt <= end && (step.jump == nullptr || pendingAt <= (double) step.jump->at))
    {
        // the user's jump comes first; a marker after it is reached from the new place
        step.jump = nullptr;
        step.userJump = true;
        step.samples = transportSamplesUntil (position, pendingAt, tps, n);
    }
    else if (step.jump != nullptr)
        step.samples = transportSamplesUntil (position, (double) step.jump->at, tps, n);
    return step;
}

namespace markers
{
std::int64_t quantizedJumpTick (const Song& song, double position, int quantize)
{
    const auto at = (std::int64_t) std::floor (std::max (0.0, position));
    if (quantize <= 0)
        return at;
    if (quantize == 1) // beats
        return (at / ppq + 1) * ppq;

    // bars (1, 2, 4, 8), counted from bar 1 and following the time signature changes
    const auto every = quantize == 2 ? 1 : quantize == 3 ? 2 : quantize == 4 ? 4 : 8;
    auto bar = song.barAt (at);
    auto tick = bar.tick + bar.length;
    auto number = bar.number + 1;
    for (int guard = 0; guard < 64 && (number - 1) % every != 0; ++guard)
    {
        bar = song.barAt (tick);
        tick = bar.tick + bar.length;
        number = bar.number + 1;
    }
    return tick;
}
void setAction (const Song& song, Marker& marker, int action)
{
    if (action == Marker::timeSignature)
    {
        if (! marker.isSignature())
        {
            const auto bar = song.barAt (marker.tick);
            marker.numerator = std::clamp ((int) (bar.length / ppq), 1, 32); // in quarter notes
            marker.denominator = 4;
        }
        marker.action = 0;
        return;
    }
    marker.numerator = 0;
    marker.denominator = 4;
    marker.action = std::clamp (action, 0, (int) Marker::keyScale);
}

namespace
{
bool hasMarkerAt (const Song& song, std::int64_t tick)
{
    for (const auto& m : song.markers())
        if (m.tick == tick)
            return true;
    return false;
}
} // namespace

int addAuto (Song& song, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    for (int i = 0; i < (int) song.markers().size(); ++i)
        if (song.markers()[(std::size_t) i].tick == tick)
            return i;
    std::string name;
    for (int n = 1;; ++n)
    {
        name = n == 1 ? std::string ("Auto") : "Auto #" + std::to_string (n);
        auto taken = false;
        for (const auto& m : song.markers())
            taken = taken || m.name == name;
        if (! taken)
            break;
    }
    Marker m;
    m.tick = tick;
    m.name = name;
    song.addMarker (m);
    for (int i = 0; i < (int) song.markers().size(); ++i)
        if (song.markers()[(std::size_t) i].tick == tick && song.markers()[(std::size_t) i].name == name)
            return i;
    return -1;
}

void placeSongLoop (Song& song, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    std::int64_t old = -1;
    auto& list = song.markers();
    for (auto it = list.begin(); it != list.end();)
        if (! it->isSignature() && it->action == Marker::songLoop)
        {
            old = it->tick;
            it = list.erase (it);
        }
        else
            ++it;
    if (old == tick)
        return;
    Marker m;
    m.tick = tick;
    m.action = Marker::songLoop;
    song.addMarker (m);
}

std::pair<int, int> signatureAt (const Song& song, std::int64_t tick)
{
    std::pair<int, int> sig { std::max (1, song.beatsPerBar), meterUnit (song.beatUnit) };
    for (const auto& m : song.markers())
        if (m.isSignature() && m.tick <= tick)
            sig = { m.numerator, m.denominator };
    return sig;
}

void addSignatureChange (Song& song, std::int64_t from, std::int64_t to, int numerator, int denominator)
{
    from = std::max<std::int64_t> (0, from) / ppq * ppq;
    const auto before = signatureAt (song, from);
    auto restore = false;
    if (to > 0 && to != song.songLength())
    {
        to = to / ppq * ppq;
        restore = to > from;
        for (const auto& m : song.markers())
            restore = restore && ! (m.isSignature() && m.tick == to);
    }
    auto put = [&song] (std::int64_t at, int n, int d) {
        Marker m;
        m.tick = at;
        m.name = std::to_string (n) + "/" + std::to_string (d);
        m.numerator = n;
        m.denominator = d;
        song.addMarker (m);
    };
    put (from, std::clamp (numerator, 1, 32), std::clamp (denominator, 1, 32));
    if (restore)
        put (to, before.first, before.second);
}

void addEvery (Song& song, int bars)
{
    bars = std::max (1, bars);
    const auto from = song.hasLoop() ? song.loopStart : 0;
    const auto to = song.hasLoop() ? song.loopEnd : song.songLength();
    for (const auto& b : song.barsBetween (from, to))
    {
        if (b.tick < from || (b.number - 1) % bars != 0 || hasMarkerAt (song, b.tick))
            continue;
        Marker m;
        m.tick = b.tick;
        m.name = std::to_string (b.number);
        song.addMarker (m);
    }
}

void addJumpToNextBar (Song& song, std::int64_t tick)
{
    tick = std::max<std::int64_t> (0, tick);
    const auto length = std::max<std::int64_t> (1, song.barAt (tick).length);
    Marker skip;
    skip.tick = tick;
    skip.action = Marker::markerSkip;
    song.addMarker (skip);
    if (! hasMarkerAt (song, tick + length))
    {
        Marker next;
        next.tick = tick + length;
        song.addMarker (next);
    }
}

namespace
{
// the sections around marker `index`: [a0, a1) then [a1, b1) (the one before and this one for `direction` < 0)
bool contentSections (const Song& song, int index, int direction, std::int64_t& a0, std::int64_t& a1, std::int64_t& b1)
{
    const auto& list = song.markers();
    if (index < 0 || index >= (int) list.size() || direction == 0)
        return false;
    const auto end = std::max<std::int64_t> (song.songLength(), list.back().tick);
    const auto tick = list[(std::size_t) index].tick;
    const auto after = [&list, end] (std::int64_t t) {
        for (const auto& m : list)
            if (m.tick > t)
                return m.tick;
        return end;
    };
    if (direction > 0)
    {
        a0 = tick;
        a1 = after (tick);
        if (a1 >= end)
            return false; // (no marker after this one: nothing on the right)
        b1 = after (a1);
    }
    else
    {
        if (tick <= 0)
            return false;
        a0 = 0;
        for (const auto& m : list)
            if (m.tick < tick)
                a0 = std::max (a0, m.tick);
        a1 = tick;
        b1 = after (tick);
    }
    return a1 > a0 && b1 > a1;
}
} // namespace

bool canMoveContent (const Song& song, int index, int direction)
{
    std::int64_t a0 = 0, a1 = 0, b1 = 0;
    return contentSections (song, index, direction, a0, a1, b1);
}

bool moveContent (Song& song, int index, int direction)
{
    std::int64_t a0 = 0, a1 = 0, b1 = 0;
    if (! contentSections (song, index, direction, a0, a1, b1))
        return false;
    const auto lengthA = a1 - a0, lengthB = b1 - a1;
    const auto moved = [&] (std::int64_t t) { return t >= a0 && t < a1 ? t + lengthB : t >= a1 && t < b1 ? t - lengthA : t; };
    for (auto& c : song.playlist)
        c.start = moved (c.start);
    auto list = song.markers();
    song.markers().clear();
    for (auto m : list)
    {
        m.tick = moved (m.tick);
        song.addMarker (m);
    }
    return true;
}

int deleteInSelection (Song& song)
{
    auto& list = song.markers();
    const auto before = list.size();
    if (song.hasLoop())
    {
        const auto from = song.loopStart, to = song.loopEnd;
        std::erase_if (list, [from, to] (const Marker& m) { return m.tick >= from && m.tick < to; });
    }
    else
        list.clear();
    return (int) (before - list.size());
}

void changeToMarkerLoop (Song& song)
{
    if (! song.hasLoop())
        return;
    const auto from = song.loopStart, to = song.loopEnd;
    bool started = false;
    for (auto& m : song.markers())
        if (m.tick == from && ! m.isSignature())
        {
            m.action = Marker::markerLoop;
            started = true;
            break;
        }
    if (! started)
    {
        Marker loop;
        loop.tick = from;
        loop.action = Marker::markerLoop;
        song.addMarker (loop);
    }
    if (! hasMarkerAt (song, to))
    {
        Marker end;
        end.tick = to;
        song.addMarker (end);
    }
    song.loopStart = song.loopEnd = 0;
}

const char* actionName (int action) noexcept
{
    switch (action)
    {
        case Marker::start: return "Start";
        case Marker::songLoop: return "Loop";
        case Marker::patternLength: return "Pattern length";
        case Marker::markerLoop: return "Marker loop";
        case Marker::markerSkip: return "Marker skip";
        case Marker::markerPause: return "Marker pause";
        case Marker::barOverride: return "Bar override";
        case Marker::timeSignature: return "Time signature";
        case Marker::punchIn: return "Start recording";
        case Marker::punchOut: return "Stop recording";
        case Marker::loopLength: return "Loop length";
        case Marker::keyScale: return "Key/Scale";
        default: return "";
    }
}

const char* actionTag (int action) noexcept
{
    switch (action)
    {
        case Marker::start: return "\xe2\x96\xb6";       // ▶
        case Marker::songLoop: return "\xe2\x86\xba";    // ↺
        case Marker::markerLoop: return "\xe2\x86\xb6";  // ↶
        case Marker::markerSkip: return "\xe2\x86\xb7";  // ↷
        case Marker::markerPause: return "\xe2\x80\x96"; // ‖
        case Marker::barOverride: return "\xe2\x87\xa5"; // ⇥
        case Marker::patternLength: return "\xe2\x86\x94"; // ↔
        case Marker::keyScale: return "\xe2\x99\xaf";    // ♯
        case Marker::punchIn: case Marker::punchOut: return "\xe2\x97\x8f"; // ●
        default: return "";
    }
}
} // namespace markers
} // namespace lynx
