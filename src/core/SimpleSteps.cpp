#include "core/SimpleSteps.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/SimpleTracks.h"

namespace lynx::simplesteps
{
namespace
{
constexpr char keys[keyedRows + 1] = "AOFIEJKL"; // BandLab's row keys, bottom row first

bool drumsTrack (const Song& song, int track)
{
    return simpletracks::kindOf (song, track) == simpletracks::Kind::drums;
}

std::string letterName (const Song& song, int track, int letter)
{
    const auto& base = song.playlistTracks[(std::size_t) track].name;
    return letter == 0 ? base : base + " " + std::string (1, (char) ('A' + letter));
}

bool validPattern (const Song& song, int pattern) { return pattern >= 0 && pattern < (int) song.patterns.size(); }
} // namespace

std::vector<int> kitRows (const Song& song, int track)
{
    std::vector<int> out;
    if (! drumsTrack (song, track))
        return out;
    const auto insert = simpletracks::insertOf (song, track);
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (const auto& c = song.channels[(std::size_t) i];
            c.mixerTrack == insert && c.instrument != InstrumentKind::audioClip && c.instrument != InstrumentKind::controller && c.simplePad.index < 0)
            out.push_back (i);
    return out;
}

char rowKey (int row) noexcept { return row >= 0 && row < keyedRows ? keys[row] : 0; }

int rowForKey (int keyCode) noexcept
{
    if (keyCode >= 'a' && keyCode <= 'z')
        keyCode -= 'a' - 'A';
    for (int i = 0; i < keyedRows; ++i)
        if (keys[i] == keyCode)
            return i;
    return -1;
}

int letterPattern (const Song& song, int track, int letter)
{
    if (letter < 0 || letter >= letterCount || ! drumsTrack (song, track))
        return -1;
    const auto name = letterName (song, track, letter);
    if (! song.playlistTracks[(std::size_t) track].name.empty())
        for (int p = 0; p < (int) song.patterns.size(); ++p)
            if (song.patterns[(std::size_t) p].name == name)
                return p;
    if (letter != 0)
        return -1;
    // A: the track's first pattern clip (a pattern renamed by hand, or a track without a name)
    const Clip* first = nullptr;
    for (const auto& c : song.playlist)
        if (c.kind == ClipKind::pattern && c.track == track && validPattern (song, c.pattern) && (first == nullptr || c.start < first->start))
            first = &c;
    return first != nullptr ? first->pattern : -1;
}

unsigned lettersWithPattern (const Song& song, int track)
{
    unsigned bits = 0;
    for (int l = 0; l < letterCount; ++l)
        if (letterPattern (song, track, l) >= 0)
            bits |= 1u << l;
    return bits;
}

int letterOf (const Song& song, int track, int pattern)
{
    for (int l = 0; l < letterCount; ++l)
        if (pattern >= 0 && letterPattern (song, track, l) == pattern)
            return l;
    return -1;
}

int ensureLetter (Song& song, int track, int letter)
{
    if (letter < 0 || letter >= letterCount || ! drumsTrack (song, track))
        return -1;
    if (const auto have = letterPattern (song, track, letter); have >= 0)
        return have;
    const auto a = letterPattern (song, track, 0);
    Pattern p;
    p.name = letterName (song, track, letter);
    const auto& t = song.playlistTracks[(std::size_t) track];
    p.colour = validPattern (song, a) ? song.patterns[(std::size_t) a].colour : t.colour;
    p.lengthTicks = validPattern (song, a) ? song.patterns[(std::size_t) a].effectiveLength (song.meter()) : song.barTicks();
    song.patterns.push_back (std::move (p));
    return (int) song.patterns.size() - 1;
}

int addLetterClip (Song& song, int track, int letter, std::int64_t at)
{
    const auto p = letterPattern (song, track, letter);
    if (p < 0)
        return -1;
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = p;
    clip.track = track;
    clip.start = std::max<std::int64_t> (0, at);
    clip.length = song.patterns[(std::size_t) p].effectiveLength (song.meter());
    // BandLab (capture C f): the new region overwrites what lies under it on the track — a region it starts inside keeps
    // its left part, one it ends inside goes on from its end (its own content, same end), one it covers goes
    const auto from = clip.start, to = clip.start + clip.length;
    std::vector<Clip> rights;
    for (auto it = song.playlist.begin(); it != song.playlist.end();)
    {
        auto& c = *it;
        if (c.track != track || c.start >= to || c.end() <= from)
        {
            ++it;
            continue;
        }
        if (c.end() > to)
        {
            auto right = c;
            right.offset += to - c.start;
            right.start = to;
            right.length = c.end() - to;
            right.uid = 0;
            rights.push_back (right);
        }
        if (c.start < from)
        {
            c.length = from - c.start;
            ++it;
        }
        else
            it = song.playlist.erase (it);
    }
    for (auto& r : rights)
        song.playlist.push_back (r);
    song.playlist.push_back (clip);
    return (int) song.playlist.size() - 1;
}

int stepCount (const Song& song, int pattern)
{
    if (! validPattern (song, pattern))
        return 0;
    const auto len = song.patterns[(std::size_t) pattern].effectiveLength (song.meter());
    return (int) std::max<std::int64_t> (1, (len + ticksPerStep - 1) / ticksPerStep);
}

int lengthBars (const Song& song, int pattern)
{
    if (! validPattern (song, pattern))
        return 1;
    const auto bar = std::max<std::int64_t> (1, song.barTicks());
    return (int) std::max<std::int64_t> (1, (song.patterns[(std::size_t) pattern].effectiveLength (song.meter()) + bar - 1) / bar);
}

void setLengthBars (Song& song, int pattern, int bars)
{
    if (validPattern (song, pattern))
        song.patterns[(std::size_t) pattern].lengthTicks = (std::int64_t) std::clamp (bars, 1, 8) * song.barTicks();
}

float shade (float velocity) noexcept
{
    constexpr float standard = 0.78f; // Note's default velocity (FL's 100 of 128)
    return std::clamp (0.45f + 0.55f * velocity / standard, 0.45f, 1.0f);
}

int playingStep (const Song& song, int track, int pattern, double pos, bool playing)
{
    if (! playing || ! validPattern (song, pattern) || pos < 0.0)
        return -1;
    const auto period = (double) song.patterns[(std::size_t) pattern].effectiveLength (song.meter());
    if (period <= 0.0)
        return -1;
    double at = -1.0;
    if (song.mode == PlayMode::pattern)
    {
        if (song.currentPattern != pattern)
            return -1;
        at = std::fmod (pos, period);
    }
    else
        for (const auto& c : song.playlist)
            if (c.kind == ClipKind::pattern && c.track == track && c.pattern == pattern && ! c.muted && pos >= (double) c.start
                && pos < (double) (c.start + c.length))
            {
                at = std::fmod (pos - (double) c.start + (double) c.offset, period);
                break;
            }
    return at < 0.0 ? -1 : (int) (at / ticksPerStep);
}
} // namespace lynx::simplesteps
