#include "core/QuickRender.h"

#include <algorithm>
#include <limits>

#include "core/Effects.h"
#include "core/NoteEdit.h"

namespace lynx::quickrender
{
namespace
{
std::string trackName (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return song.playlistTracks[(std::size_t) track].name;
    return "Track " + std::to_string (track + 1);
}

bool isAutomation (const Clip& c) noexcept { return c.kind == ClipKind::automation; }

// The name and colour of what a clip plays (FUN_00e50820 / FUN_00e50890): the channel of an audio clip, the pattern of
// a pattern clip.
std::string sourceName (const Song& song, const Clip& c)
{
    if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
        return song.channels[(std::size_t) c.channel].name;
    if (c.kind == ClipKind::pattern)
        return patternName (song, c.pattern);
    return {};
}

std::uint32_t sourceColour (const Song& song, const Clip& c)
{
    if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
        return song.channels[(std::size_t) c.channel].colour;
    if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) song.patterns.size())
        return song.patterns[(std::size_t) c.pattern].colour;
    return 0;
}

// FL refuses pattern clips whose pattern has tempo events (item 0x40000005 in the pattern's events).
bool hasTempoEvents (const Song& song, const Clip& c)
{
    if (c.kind != ClipKind::pattern || c.pattern < 0 || c.pattern >= (int) song.patterns.size())
        return false;
    for (const auto& lane : song.patterns[(std::size_t) c.pattern].events)
        if (lane.target.kind == AutomationTargetKind::tempo && ! lane.events.empty())
            return true;
    return false;
}

bool plays (const Song& song, const Clip& c) noexcept
{
    return ! c.muted && c.length > 0 && ! song.isTrackMuted (c.track);
}

Result finish (const Song& song, Plan& plan, bool fromSongStart, Result emptyResult)
{
    if (plan.clips.empty())
        return emptyResult;

    std::int64_t first = std::numeric_limits<std::int64_t>::max(), last = 0;
    plan.topTrack = std::numeric_limits<int>::max();
    for (auto i : plan.clips)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (hasTempoEvents (song, c))
            return Result::tempoEvents;
        first = std::min (first, c.start);
        last = std::max (last, c.end());
        plan.topTrack = std::min (plan.topTrack, c.track);
    }

    const auto& c0 = song.playlist[(std::size_t) plan.clips.front()];
    if (plan.name.empty())
        plan.name = sourceName (song, c0) + " (consolidated)";
    plan.colour = sourceColour (song, c0);

    // FL: a time selection is the range; otherwise the clips' extent, from 0 with "From song start"
    if (song.hasLoop())
    {
        plan.timeSelection = true;
        plan.start = song.loopStart;
        plan.end = song.loopEnd;
    }
    else
    {
        plan.start = fromSongStart ? 0 : first;
        plan.end = last;
    }
    if (fromSongStart)
        plan.start = 0;
    return plan.end > plan.start ? Result::ok : emptyResult;
}
} // namespace

bool patternRenderable (const Song& song, int pattern) noexcept
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return false;
    const auto& p = song.patterns[(std::size_t) pattern];
    if (! p.notes.empty())
        return true;
    for (const auto& lane : p.events)
        if (! lane.events.empty())
            return true;
    return false;
}

std::string patternName (const Song& song, int pattern)
{
    if (pattern >= 0 && pattern < (int) song.patterns.size() && ! song.patterns[(std::size_t) pattern].name.empty())
        return song.patterns[(std::size_t) pattern].name;
    return "Pattern " + std::to_string (pattern + 1);
}

std::string uniqueChannelName (const Song& song, const std::string& name)
{
    auto taken = [&song] (const std::string& n) {
        return std::any_of (song.channels.begin(), song.channels.end(), [&n] (const ChannelDef& c) { return c.name == n; });
    };
    if (name.empty() || ! taken (name))
        return name;
    for (int k = 2;; ++k)
    {
        auto candidate = name + " #" + std::to_string (k);
        if (! taken (candidate))
            return candidate;
    }
}

Song makePatternRenderSong (const Song& song, int pattern)
{
    Song out = song;
    out.currentPattern = std::clamp (pattern, 0, std::max (0, (int) song.patterns.size() - 1));
    out.mode = PlayMode::pattern;
    out.loopStart = out.loopEnd = 0;
    return out;
}

int applyPatternRender (Song& song, int pattern, const std::string& samplePath, bool replace)
{
    ChannelDef c;
    c.name = uniqueChannelName (song, patternName (song, pattern));
    c.instrument = InstrumentKind::audioClip;
    c.samplePath = samplePath;
    c.volume = 1.0f;
    c.mixerTrack = 0;
    if (pattern >= 0 && pattern < (int) song.patterns.size())
        c.colour = song.patterns[(std::size_t) pattern].colour;
    song.channels.push_back (c);
    const auto channel = (int) song.channels.size() - 1;

    if (replace)
        for (auto& clip : song.playlist) // FL walks the Playlist's items (the arrangement shown)
            if (clip.kind == ClipKind::pattern && clip.pattern == pattern)
            {
                clip.kind = ClipKind::audio;
                clip.channel = channel;
                // (the place, length, offset and mute stay; FL clears the offsets of an untrimmed clip: LYNX's are 0)
            }
    return channel;
}

Result planSelection (const Song& song, const std::vector<int>& selected, bool fromSongStart, Plan& plan)
{
    plan = {};
    for (auto i : selected)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            const auto& c = song.playlist[(std::size_t) i];
            if (plays (song, c) && ! isAutomation (c))
                plan.clips.push_back (i);
        }
    std::sort (plan.clips.begin(), plan.clips.end());
    plan.clips.erase (std::unique (plan.clips.begin(), plan.clips.end()), plan.clips.end());
    return finish (song, plan, fromSongStart, Result::nothingSelected);
}

Result planTrack (const Song& song, int track, bool withGroup, Start start, Plan& plan)
{
    plan = {};
    if (track < 0)
        return Result::empty;
    if (start == Start::timeSelection && ! song.hasLoop())
        return Result::empty;

    std::vector<int> tracks { track };
    if (withGroup)
        for (auto t : noteedit::groupMembers (song, track))
            tracks.push_back (t);

    for (int i = 0; i < (int) song.playlist.size(); ++i)
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (std::find (tracks.begin(), tracks.end(), c.track) != tracks.end() && plays (song, c) && ! isAutomation (c))
            plan.clips.push_back (i);
    }
    plan.name = trackName (song, track) + " (consolidated)";
    const auto r = finish (song, plan, start == Start::songStart, Result::empty);
    plan.topTrack = track;
    return r;
}

Song makePlanRenderSong (const Song& song, const Plan& plan)
{
    Song out = song;
    out.playlist.clear();
    for (auto i : plan.clips)
        out.playlist.push_back (song.playlist[(std::size_t) i]);
    // FL mutes every other clip but the automation channels' (FUN_012fc040: channel type 5)
    for (const auto& c : song.playlist)
        if (isAutomation (c) && ! c.muted && ! song.isTrackMuted (c.track))
            out.playlist.push_back (c);
    std::stable_sort (out.playlist.begin(), out.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    for (auto& t : out.playlistTracks)
        t.muted = false; // (the clips of muted tracks were left out above)
    out.mode = PlayMode::song;
    out.loopStart = plan.start;
    out.loopEnd = std::max (plan.end, plan.start + 1);
    return out;
}

int applyConsolidation (Song& song, const Plan& plan, const std::string& samplePath, std::int64_t lengthTicks)
{
    // the rendered clips are muted (FL keeps them, FUN_010646c0); with a time selection they are cut at its edges
    // first (FUN_00e913e0 at both ends) and only the parts inside it are muted
    std::vector<Clip> pieces;
    for (auto i : plan.clips)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        auto& c = song.playlist[(std::size_t) i];
        if (! plan.timeSelection)
        {
            c.muted = true;
            continue;
        }
        if (c.end() <= plan.start || c.start >= plan.end)
            continue;
        if (c.start < plan.start)
            pieces.push_back (splitClip (c, plan.start)); // c keeps the part before; the rest is a new piece
        auto& inside = c.start >= plan.start ? c : pieces.back();
        if (inside.end() > plan.end)
        {
            auto after = splitClip (inside, plan.end);
            inside.muted = true;
            pieces.push_back (after);
        }
        else
            inside.muted = true;
    }
    song.playlist.insert (song.playlist.end(), pieces.begin(), pieces.end());

    // the new track above the consolidated one, which joins its group (FUN_012e62e0 then +0x3e of the track below)
    const auto top = std::max (0, plan.topTrack);
    const bool wasGrouped = top < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) top].grouped;
    noteedit::insertTrack (song, top);
    if ((int) song.playlistTracks.size() <= top + 1)
        song.playlistTracks.resize ((std::size_t) top + 2);
    song.playlistTracks[(std::size_t) top].grouped = wasGrouped;
    song.playlistTracks[(std::size_t) top + 1].grouped = true;

    ChannelDef ch;
    ch.name = uniqueChannelName (song, plan.name);
    ch.instrument = InstrumentKind::audioClip;
    ch.samplePath = samplePath;
    ch.volume = 1.0f;
    ch.mixerTrack = 0;
    ch.colour = plan.colour;
    song.channels.push_back (ch);

    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = (int) song.channels.size() - 1;
    clip.track = top;
    clip.start = plan.start;
    clip.length = std::max<std::int64_t> (1, lengthTicks);
    song.playlist.push_back (clip);
    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    return clip.channel;
}

std::vector<int> exportTracks (const Song& song, bool groups)
{
    int last = -1;
    for (const auto& c : song.playlist)
        last = std::max (last, c.track);
    std::vector<int> out;
    for (int t = 0; t <= last; ++t)
        if (! groups || t >= (int) song.playlistTracks.size() || ! song.playlistTracks[(std::size_t) t].grouped)
            out.push_back (t);
    return out;
}

std::string trackExportName (const Song& song, const std::string& projectName, int track)
{
    auto name = projectName + " - " + trackName (song, track);
    for (auto& ch : name)
        if (std::string ("\\/:*?\"<>|").find (ch) != std::string::npos || (unsigned char) ch < 32)
            ch = '_';
    return name;
}

std::string safeFileName (const std::string& name, const std::string& fallback)
{
    auto out = name;
    for (auto& ch : out)
        if (std::string ("\\/:*?\"<>|").find (ch) != std::string::npos || (unsigned char) ch < 32)
            ch = '_';
    const auto first = out.find_first_not_of (" .");
    const auto last = out.find_last_not_of (" .");
    out = first == std::string::npos ? std::string() : out.substr (first, last - first + 1);
    return out.empty() ? fallback : out;
}

std::vector<Bone> projectBones (const Song& song)
{
    std::vector<Bone> out;
    std::vector<std::string> taken; // "<folder>/<name>", case kept (the tests and the file system decide the rest)
    auto add = [&out, &taken] (Bone b) {
        auto name = b.name;
        for (int k = 2; std::find (taken.begin(), taken.end(), b.folder + "/" + name) != taken.end(); ++k)
            name = b.name + " (" + std::to_string (k) + ")";
        b.name = name;
        taken.push_back (b.folder + "/" + name);
        out.push_back (std::move (b));
    };

    for (int t = 0; t < (int) song.mixer.size(); ++t)
    {
        const auto& track = song.mixer[(std::size_t) t];
        const auto defaultName = t == 0 ? std::string ("Master") : "Insert " + std::to_string (t);
        if (track.effects.empty() && (track.name.empty() || track.name == defaultName))
            continue; // (an untouched insert has nothing to keep)
        const auto trackName = ! track.name.empty() ? track.name : defaultName;
        Bone b;
        b.kind = Bone::Kind::mixer;
        b.folder = "Mixer";
        b.name = safeFileName (trackName, "Insert");
        b.track = t;
        add (b);
        for (int s = 0; s < (int) track.effects.size(); ++s)
        {
            const auto& slot = track.effects[(std::size_t) s];
            if (slot.kind == EffectKind::parallel)
                continue; // (a container has no preset of its own: the insert's state keeps it)
            Bone e;
            e.kind = Bone::Kind::effect;
            e.folder = "Effects";
            e.name = safeFileName (slot.kind == EffectKind::plugin ? slot.plugin.name : std::string (effectInfo (slot.kind).name), "Effect");
            e.track = t;
            e.slot = s;
            add (e);
        }
    }

    for (int p = 0; p < (int) song.patterns.size(); ++p)
        for (int c = 0; c < (int) song.channels.size(); ++c)
        {
            const auto& notes = song.patterns[(std::size_t) p].notes;
            if (std::none_of (notes.begin(), notes.end(), [c] (const Note& n) { return n.channel == c; }))
                continue;
            Bone b;
            b.kind = Bone::Kind::score;
            b.folder = "Scores";
            b.name = safeFileName (patternName (song, p) + " - " + song.channels[(std::size_t) c].name, "Score");
            b.pattern = p;
            b.channel = c;
            add (b);
        }
    return out;
}

std::int64_t ticksForSeconds (const Song& song, std::int64_t startTick, double seconds)
{
    if (seconds <= 0.0)
        return 0;
    const std::int64_t step = ppq / 16;
    double t = 0.0;
    std::int64_t tick = startTick;
    for (int guard = 0; guard < 100000000; ++guard)
    {
        const auto bpm = std::max (1.0, song.tempoAt ((double) tick));
        const auto stepSeconds = (double) step * 60.0 / (bpm * (double) ppq);
        if (t + stepSeconds >= seconds)
            return tick - startTick + (std::int64_t) ((seconds - t) / stepSeconds * (double) step + 0.5);
        t += stepSeconds;
        tick += step;
    }
    return tick - startTick;
}
} // namespace lynx::quickrender
