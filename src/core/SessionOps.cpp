#include "core/SessionOps.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/ClipLauncher.h"
#include "core/RackOps.h"

namespace lynx::session
{
// --- when ------------------------------------------------------------------------------------------------------------
double nextPoint (double now, double quant) noexcept
{
    return quant > 0.0 ? std::ceil (now / quant - 1.0e-9) * quant : std::ceil (now - 1.0e-6);
}

double recordEnd (double from, double now, double quant, double fixedLength) noexcept
{
    if (fixedLength > 0.0)
        return from + fixedLength;
    const auto at = nextPoint (std::max (now, from), quant);
    const auto least = from + (quant > 0.0 ? quant : 1.0);
    return at < least - 1.0e-9 ? least : at;
}

// --- what ------------------------------------------------------------------------------------------------------------
namespace
{
std::int64_t toGrid (std::int64_t tick, std::int64_t grid) noexcept
{
    if (grid <= 1)
        return tick;
    return (tick + grid / 2) / grid * grid;
}

Note noteOf (const RecordedEvent& on)
{
    Note n;
    n.channel = on.channel;
    n.key = on.key;
    n.velocity = std::clamp (on.velocity, 1.0f / 127.0f, 1.0f);
    if (on.expression)
    {
        n.finePitch = on.finePitch;
        n.modX = on.modX;
    }
    return n;
}

// Pairs each "on" with the next event of its channel and key: its "off", or the next strike of the same key (the first
// note ends there); fn (on, end or nullptr while it is still held).
template <typename Fn>
void forEachPair (const std::vector<RecordedEvent>& events, Fn fn)
{
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (! events[i].on)
            continue;
        const RecordedEvent* end = nullptr;
        for (std::size_t k = i + 1; k < events.size() && end == nullptr; ++k)
            if (events[k].channel == events[i].channel && events[k].key == events[i].key)
                end = &events[k];
        fn (events[i], end);
    }
}
} // namespace

std::vector<Note> takeNotes (const std::vector<RecordedEvent>& events, double from, double until, std::int64_t grid, std::int64_t early)
{
    std::vector<Note> notes;
    const auto length = (std::int64_t) std::llround (until - from);
    if (length <= 0)
        return notes;
    forEachPair (events, [&] (const RecordedEvent& on, const RecordedEvent* off) {
        if (on.tick < from - (double) early || on.tick >= until)
            return;
        const auto played = std::max<std::int64_t> (0, (std::int64_t) std::llround (on.tick - from));
        auto start = toGrid (played, grid);
        if (start >= length)
            start = 0; // (rounded up onto the loop's end: the downbeat of the next pass)
        const auto end = off != nullptr ? std::min ((double) length, off->tick - from) : (double) length;
        auto n = noteOf (on);
        n.start = start;
        n.length = std::clamp<std::int64_t> ((std::int64_t) std::llround (end) - played, 1, length - start);
        if (off != nullptr && ! off->on && off->expression)
            n.modY = off->modY;
        notes.push_back (n);
    });
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

std::vector<Note> overdubNotes (const std::vector<RecordedEvent>& events, double anchor, std::int64_t length, std::int64_t grid)
{
    std::vector<Note> notes;
    if (length <= 0)
        return notes;
    forEachPair (events, [&] (const RecordedEvent& on, const RecordedEvent* off) {
        if (off == nullptr || on.tick < anchor)
            return;
        const auto played = (std::int64_t) std::llround (std::fmod (on.tick - anchor, (double) length));
        auto start = toGrid (played % length, grid);
        if (start >= length)
            start = 0;
        auto n = noteOf (on);
        n.start = start;
        n.length = std::clamp<std::int64_t> ((std::int64_t) std::llround (off->tick - on.tick), 1, length - start);
        if (! off->on && off->expression)
            n.modY = off->modY;
        notes.push_back (n);
    });
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

void mergeNotes (Pattern& pattern, const std::vector<Note>& notes)
{
    for (const auto& n : notes)
    {
        const auto same = std::find_if (pattern.notes.begin(), pattern.notes.end(),
                                        [&] (const Note& o) { return o.channel == n.channel && o.key == n.key && o.start == n.start; });
        if (same != pattern.notes.end())
            *same = n;
        else
            pattern.notes.push_back (n);
    }
    pattern.sortNotes();
}

namespace
{
std::uint32_t trackColour (const Song& song, int track) noexcept
{
    return track >= 0 && track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].colour : 0u;
}

bool slotFree (const Song& song, int track, int scene) noexcept
{
    return ! song.clipGrid || launcher::clipAt (*song.clipGrid, track, scene) == nullptr;
}
} // namespace

int placeRecordedPattern (Song& song, int track, int scene, std::vector<Note> notes, std::int64_t length)
{
    if (track < 0 || track >= launcher::maxTracks || scene < 0 || scene >= launcher::maxScenes || length <= 0)
        return -1;
    Pattern p;
    p.name = newPatternName (song);
    p.notes = std::move (notes);
    p.lengthTicks = length;
    p.sortNotes();
    song.patterns.push_back (std::move (p));
    const auto pattern = (int) song.patterns.size() - 1;
    Clip clip;
    clip.pattern = pattern;
    clip.length = length;
    const auto slot = launcher::setClip (song, track, scene, clip);
    if (slot < 0)
    {
        song.patterns.pop_back();
        return -1;
    }
    song.clipGrid->slots[(std::size_t) slot].colour = trackColour (song, track);
    return pattern;
}

int placeRecordedAudio (Song& song, int track, int scene, const std::string& path, const std::string& name, int insert,
                        std::int64_t length, std::int64_t offset, float offsetFraction)
{
    if (track < 0 || track >= launcher::maxTracks || scene < 0 || scene >= launcher::maxScenes || length <= 0 || path.empty())
        return -1;
    ChannelDef c;
    c.name = name;
    c.instrument = InstrumentKind::audioClip;
    c.sampler.declickMode = audioClipDeclickMode;
    c.samplePath = path;
    c.mixerTrack = insert > 0 && insert < (int) song.mixer.size() ? insert : song.claimFreeInsert();
    song.channels.push_back (c);
    const auto channel = (int) song.channels.size() - 1;
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = channel;
    clip.length = length;
    clip.offset = std::max<std::int64_t> (0, offset);
    clip.offsetFraction = offsetFraction;
    const auto slot = launcher::setClip (song, track, scene, clip);
    if (slot < 0)
    {
        song.channels.pop_back();
        return -1;
    }
    auto& settings = song.clipGrid->slots[(std::size_t) slot];
    settings.name = name;
    settings.colour = trackColour (song, track);
    return channel;
}

int insertEmptyClip (Song& song, int track, int scene)
{
    if (! slotFree (song, track, scene))
        return -1;
    return placeRecordedPattern (song, track, scene, {}, (std::int64_t) std::max (1, song.beatsPerBar) * ppq);
}

// --- clips between slots ---------------------------------------------------------------------------------------------
std::optional<SlotClip> copyOf (const Song& song, int track, int scene)
{
    if (! song.clipGrid)
        return std::nullopt;
    const auto* clip = launcher::clipAt (*song.clipGrid, track, scene);
    if (clip == nullptr)
        return std::nullopt;
    return SlotClip { *clip, song.clipGrid->slots[(std::size_t) clip->gridSlot] };
}

bool paste (Song& song, int track, int scene, const SlotClip& content)
{
    auto clip = content.clip;
    clip.uid = 0; // (a copy is a clip of its own)
    const auto slot = launcher::setClip (song, track, scene, clip);
    if (slot < 0)
        return false;
    auto settings = content.settings;
    settings.scene = scene;
    settings.noStop = false;
    if (settings.groove != 0 && std::none_of (song.grooves.grooves.begin(), song.grooves.grooves.end(),
                                              [&] (const Groove& g) { return g.id == settings.groove; }))
        settings.groove = 0; // (copied from a project state whose groove is gone)
    song.clipGrid->slots[(std::size_t) slot] = std::move (settings);
    return true;
}

std::optional<SlotClip> cut (Song& song, int track, int scene)
{
    auto content = copyOf (song, track, scene);
    if (content)
        launcher::clearClip (song, track, scene);
    return content;
}

bool moveClip (Song& song, int fromTrack, int fromScene, int toTrack, int toScene, bool copy)
{
    if (fromTrack == toTrack && fromScene == toScene)
        return false;
    const auto content = copyOf (song, fromTrack, fromScene);
    if (! content)
        return false;
    const auto uid = content->clip.uid;
    if (! paste (song, toTrack, toScene, *content))
        return false;
    if (! copy)
    {
        launcher::clearClip (song, fromTrack, fromScene);
        // (a moved clip is still the same clip)
        for (auto& c : song.clipGrid->clips)
            if (c.track == toTrack && c.length > 0 && song.clipGrid->slots[(std::size_t) c.gridSlot].scene == toScene)
                c.uid = uid;
    }
    return true;
}

int duplicate (Song& song, int track, int scene)
{
    const auto content = copyOf (song, track, scene);
    if (! content || scene + 1 >= launcher::maxScenes)
        return -1;
    return paste (song, track, scene + 1, *content) ? scene + 1 : -1;
}

bool toPlaylist (Song& song, int track, int scene, int playlistTrack, std::int64_t tick)
{
    const auto content = copyOf (song, track, scene);
    if (! content || playlistTrack < 0 || playlistTrack >= launcher::maxTracks)
        return false;
    auto clip = content->clip;
    clip.gridSlot = -1;
    clip.uid = 0;
    clip.track = playlistTrack;
    clip.start = std::max<std::int64_t> (0, tick);
    song.playlist.push_back (clip);
    return true;
}

bool putClip (Song& song, Clip clip, int track, int scene)
{
    if (clip.length <= 0)
        return false;
    if (clip.kind == ClipKind::pattern ? (clip.pattern < 0 || clip.pattern >= (int) song.patterns.size())
        : clip.kind == ClipKind::audio ? (clip.channel < 0 || clip.channel >= (int) song.channels.size()) : true)
        return false;
    clip.uid = 0;
    clip.muted = false;
    return launcher::setClip (song, track, scene, clip) >= 0;
}

bool fromPlaylist (Song& song, std::size_t playlistIndex, int track, int scene, bool move)
{
    if (playlistIndex >= song.playlist.size())
        return false;
    if (! putClip (song, song.playlist[playlistIndex], track, scene))
        return false;
    if (move)
        song.playlist.erase (song.playlist.begin() + (std::ptrdiff_t) playlistIndex);
    return true;
}

bool putPattern (Song& song, int track, int scene, int pattern)
{
    if (pattern < 0 || pattern >= (int) song.patterns.size())
        return false;
    Clip clip;
    clip.pattern = pattern;
    clip.length = std::max<std::int64_t> (ppq, song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar));
    return launcher::setClip (song, track, scene, clip) >= 0;
}

bool putAudio (Song& song, int track, int scene, int channel, std::int64_t length)
{
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].instrument != InstrumentKind::audioClip || length <= 0)
        return false;
    Clip clip;
    clip.kind = ClipKind::audio;
    clip.channel = channel;
    clip.length = length;
    return launcher::setClip (song, track, scene, clip) >= 0;
}
} // namespace lynx::session
