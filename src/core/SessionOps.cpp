#include "core/SessionOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

std::string recordedClipName (const Song& song, const std::string& trackName)
{
    const auto taken = [&song] (const std::string& name) {
        if (std::any_of (song.patterns.begin(), song.patterns.end(), [&] (const Pattern& p) { return p.name == name; }))
            return true;
        return song.clipGrid && std::any_of (song.clipGrid->slots.begin(), song.clipGrid->slots.end(),
                                             [&] (const ClipGridSlot& slot) { return slot.name == name; });
    };
    for (int n = 1;; ++n)
        if (auto name = trackName + " " + std::to_string (n); ! taken (name))
            return name;
}

std::string lengthCounter (double ticks, Meter meter)
{
    const auto whole = (std::int64_t) std::floor (std::max (0.0, ticks) + 1.0e-6);
    const auto bar = meter.bar(), beat = meter.beat(); // (round 190fg: the meter's beats; sixteenths below them)
    const auto inBar = whole % bar;
    return std::to_string (whole / bar) + "." + std::to_string (inBar / beat) + "." + std::to_string ((inBar % beat) / (ppq / 4));
}

int placeRecordedPattern (Song& song, int track, int scene, std::vector<Note> notes, std::int64_t length, const std::string& name)
{
    if (track < 0 || track >= launcher::maxTracks || scene < 0 || scene >= launcher::maxScenes || length <= 0)
        return -1;
    Pattern p;
    p.name = name.empty() ? newPatternName (song) : name;
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
    return placeRecordedPattern (song, track, scene, {}, song.barTicks());
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
    clip.length = std::max<std::int64_t> (ppq, song.patterns[(std::size_t) pattern].effectiveLength (song.meter()));
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
// --- scenes (round 190ef) --------------------------------------------------------------------------------------------
namespace
{
// a copy of `from`'s slot on track t into row `to` (the clip, its settings, or a removed stop button)
void copySlot (Song& song, int track, int from, int to)
{
    if (const auto content = copyOf (song, track, from))
    {
        paste (song, track, to, *content);
        return;
    }
    if (song.clipGrid && ! launcher::hasStopButton (*song.clipGrid, track, from))
        launcher::setStopButton (song, track, to, false);
}

std::vector<int> tracksIn (const ClipGrid& grid)
{
    std::vector<int> tracks;
    for (const auto& c : grid.clips)
        if (std::find (tracks.begin(), tracks.end(), c.track) == tracks.end())
            tracks.push_back (c.track);
    std::sort (tracks.begin(), tracks.end());
    return tracks;
}
} // namespace

int duplicateScene (Song& song, int scene)
{
    if (scene < 0 || scene + 1 >= launcher::maxScenes)
        return -1;
    auto& grid = launcher::ensureGrid (song);
    while ((int) grid.scenes.size() <= scene) // (a row shown under the last scene)
        grid.scenes.emplace_back();
    const auto at = launcher::insertScene (song, scene + 1);
    if (at < 0)
        return -1;
    song.clipGrid->scenes[(std::size_t) at] = song.clipGrid->scenes[(std::size_t) scene];
    for (const auto t : tracksIn (*song.clipGrid))
        copySlot (song, t, scene, at);
    return at;
}

int captureScene (Song& song, int below, const std::vector<int>& playing, std::vector<int>* moved)
{
    if (moved != nullptr)
        moved->clear();
    if (below < -1 || below + 1 >= launcher::maxScenes)
        return -1;
    auto& grid = launcher::ensureGrid (song);
    std::vector<std::pair<int, int>> taken; // (track, slot playing)
    for (int t = 0; t < (int) playing.size(); ++t)
        if (const auto* clip = playing[(std::size_t) t] >= 0 ? launcher::clipAt (grid, t, playing[(std::size_t) t]) : nullptr)
            taken.push_back ({ t, clip->gridSlot });
    while ((int) grid.scenes.size() <= below)
        grid.scenes.emplace_back();
    // nothing plays: an empty row like the selected scene (name, tempo, signature; live check 190ei Q1a)
    ClipGridScene like;
    if (taken.empty() && below >= 0)
    {
        const auto& from = grid.scenes[(std::size_t) below];
        like.name = from.name;
        like.tempo = from.tempo;
        like.beatsPerBar = from.beatsPerBar;
        like.beatUnit = from.beatUnit;
    }
    const auto at = launcher::insertScene (song, below + 1);
    if (at < 0)
        return -1;
    auto& g = *song.clipGrid;
    g.scenes[(std::size_t) at] = like;
    for (const auto& [t, slot] : taken)
    {
        if (slot < 0 || slot >= (int) g.slots.size())
            continue;
        const auto row = g.slots[(std::size_t) slot].scene; // (rows from `at` on moved down one)
        const auto content = copyOf (song, t, row);
        if (! content || ! paste (song, t, at, *content))
            continue;
        // the playing slot takes the new row, its copy the old one: playback moves with no restart
        if (const auto* copy = launcher::clipAt (g, t, at); copy != nullptr && copy->gridSlot >= 0 && copy->gridSlot != slot)
        {
            g.slots[(std::size_t) copy->gridSlot].scene = row;
            g.slots[(std::size_t) slot].scene = at;
            if (moved != nullptr)
                moved->push_back (slot);
        }
    }
    return at;
}

namespace
{
ClipGridScene* sceneAt (Song& song, int scene)
{
    if (scene < 0 || scene >= launcher::maxScenes)
        return nullptr;
    auto& grid = launcher::ensureGrid (song);
    while ((int) grid.scenes.size() <= scene)
        grid.scenes.emplace_back();
    return &grid.scenes[(std::size_t) scene];
}
} // namespace

bool setSceneTempo (Song& song, int scene, double bpm)
{
    auto* s = sceneAt (song, scene);
    if (s == nullptr)
        return false;
    s->tempo = bpm > 0.0 && std::isfinite (bpm) ? std::clamp (bpm, minTempo, maxTempo) : 0.0;
    return true;
}

bool setSceneBeats (Song& song, int scene, int beats)
{
    auto* s = sceneAt (song, scene);
    if (s == nullptr)
        return false;
    s->beatsPerBar = beats > 0 ? std::clamp (beats, sceneBeatsMin, sceneBeatsMax) : 0;
    if (s->beatsPerBar == 0)
        s->beatUnit = 4;
    return true;
}

bool setSceneSignature (Song& song, int scene, int beats, int unit)
{
    if (! setSceneBeats (song, scene, beats))
        return false;
    auto& s = song.clipGrid->scenes[(std::size_t) scene];
    s.beatUnit = s.beatsPerBar > 0 ? sceneBeatUnit (unit) : 4;
    return true;
}

namespace
{
constexpr int beatUnits[] { 1, 2, 4, 8, 16 };
int unitIndex (int unit) noexcept
{
    auto best = 2;
    for (int i = 0; i < 5; ++i)
        if (std::abs (beatUnits[i] - unit) < std::abs (beatUnits[best] - unit))
            best = i;
    return best;
}
} // namespace

int sceneBeatUnit (int unit) noexcept
{
    return unit > 0 ? beatUnits[unitIndex (unit)] : 4;
}

int sceneBeatUnitStep (int unit, int steps) noexcept
{
    return beatUnits[std::clamp (unitIndex (sceneBeatUnit (unit)) + steps, 0, 4)];
}

int sceneBarBeats (int beats, int unit) noexcept
{
    const auto quarters = (double) std::max (1, beats) * 4.0 / (double) sceneBeatUnit (unit);
    return std::clamp ((int) std::lround (quarters), sceneBeatsMin, sceneBeatsMax);
}

std::string sceneTempoText (double bpm)
{
    char text[32];
    std::snprintf (text, sizeof text, "%.2f", bpm);
    return text;
}

std::string sceneSignatureText (int beats, int unit)
{
    return std::to_string (std::max (1, beats)) + " / " + std::to_string (sceneBeatUnit (unit));
}

double sceneTempoDragged (double start, double pixels) noexcept
{
    return std::clamp (std::round (start + pixels * sceneTempoPerPixel), minTempo, maxTempo);
}

int sceneBeatsDragged (int start, double pixels) noexcept
{
    return std::clamp ((int) std::lround ((double) start + pixels * sceneBeatsPerPixel), sceneBeatsMin, sceneBeatsMax);
}

int sceneUnitDragged (int start, double pixels) noexcept
{
    const auto steps = (int) std::trunc (pixels / sceneUnitPixelsPerStep);
    return sceneBeatUnitStep (start, steps);
}

int captureSlot (const Song& song, int track, int scene)
{
    scene = std::max (0, scene);
    if (! song.clipGrid)
        return scene;
    while (scene < launcher::maxScenes - 1 && launcher::clipAt (*song.clipGrid, track, scene) != nullptr)
        ++scene;
    return scene;
}

std::vector<Note> phraseNotes (const std::vector<scorelog::Entry>& phrase, double nowMs, double bpm, Meter meter, std::int64_t& length)
{
    std::vector<Note> notes;
    const auto bar = meter.bar();
    length = bar;
    if (phrase.empty())
        return notes;
    const auto ticksPerMs = std::max (1.0, bpm) * ppq / 60000.0;
    const auto played = phrase.front().startTick >= 0.0;
    double origin = 0.0;
    if (played)
    {
        auto first = phrase.front().startTick;
        for (const auto& e : phrase)
            first = std::min (first, e.startTick);
        origin = std::floor (first / (double) bar) * (double) bar;
    }
    auto firstMs = phrase.front().startMs;
    for (const auto& e : phrase)
        firstMs = std::min (firstMs, e.startMs);
    std::int64_t reach = 0;
    for (const auto& e : phrase)
    {
        Note n;
        const auto at = played && e.startTick >= 0.0 ? e.startTick - origin : (e.startMs - firstMs) * ticksPerMs;
        n.start = std::max<std::int64_t> (0, (std::int64_t) std::llround (at));
        const auto endMs = e.endMs >= 0.0 ? e.endMs : std::max (e.startMs, nowMs);
        n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround ((endMs - e.startMs) * ticksPerMs));
        n.channel = e.channel;
        n.key = std::clamp (e.key, 0, 127);
        n.velocity = std::clamp (e.velocity, 1.0f / 127.0f, 1.0f);
        reach = std::max (reach, n.start + n.length);
        notes.push_back (n);
    }
    length = std::max<std::int64_t> (1, (reach + bar - 1) / bar) * bar;
    for (auto& n : notes) // (a note held over the clip's end is cut there)
        n.length = std::clamp<std::int64_t> (n.length, 1, std::max<std::int64_t> (1, length - n.start));
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}
// --- Capture MIDI in the Clip-launch layout (round 190em) --------------------------------------------------------------
double phraseTempo (const std::vector<scorelog::Entry>& phrase, double fallback)
{
    std::vector<double> onsets;
    for (const auto& e : phrase)
        onsets.push_back (e.startMs);
    std::sort (onsets.begin(), onsets.end());
    std::vector<double> merged; // (a chord is one onset)
    for (const auto t : onsets)
        if (merged.empty() || t - merged.back() > 40.0)
            merged.push_back (t);
    if (merged.size() < 3)
        return fallback;
    std::vector<double> gaps;
    for (std::size_t i = 1; i < merged.size(); ++i)
        gaps.push_back (merged[i] - merged[i - 1]);
    std::sort (gaps.begin(), gaps.end());
    auto unit = gaps[gaps.size() / 2];
    if (! (unit > 0.0))
        return fallback;
    // the grid of that gap: each onset's whole number of units from the first, then the unit that fits them best
    double sxy = 0.0, sxx = 0.0;
    for (const auto t : merged)
    {
        const auto k = std::round ((t - merged.front()) / unit);
        sxy += (t - merged.front()) * k;
        sxx += k * k;
    }
    if (sxx > 0.0)
        unit = sxy / sxx;
    auto bpm = 60000.0 / unit;
    for (int guard = 0; guard < 16 && bpm < 80.0; ++guard)
        bpm *= 2.0;
    for (int guard = 0; guard < 16 && bpm >= 160.0; ++guard)
        bpm *= 0.5;
    if (! std::isfinite (bpm))
        return fallback;
    return std::clamp (std::round (bpm * 100.0) / 100.0, minTempo, maxTempo);
}

std::vector<Note> clipTimeNotes (const std::vector<scorelog::Entry>& phrase, double nowMs, double bpm, double shift)
{
    std::vector<Note> notes;
    const auto ticksPerMs = std::max (1.0, bpm) * ppq / 60000.0;
    for (const auto& e : phrase)
    {
        if (e.startTick < 0.0)
            continue;
        Note n;
        n.start = std::max<std::int64_t> (0, (std::int64_t) std::llround (e.startTick + shift));
        const auto endMs = e.endMs >= 0.0 ? e.endMs : std::max (e.startMs, nowMs);
        n.length = std::max<std::int64_t> (ticksPerStep / 8, (std::int64_t) std::llround ((endMs - e.startMs) * ticksPerMs));
        n.channel = e.channel;
        n.key = std::clamp (e.key, 0, 127);
        n.velocity = std::clamp (e.velocity, 1.0f / 127.0f, 1.0f);
        notes.push_back (n);
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return notes;
}

bool captureIntoClip (Song& song, int track, int scene, const std::vector<Note>& notes)
{
    if (! song.clipGrid || notes.empty())
        return false;
    const auto* found = launcher::clipAt (*song.clipGrid, track, scene);
    if (found == nullptr || found->kind != ClipKind::pattern || found->pattern < 0 || found->pattern >= (int) song.patterns.size()
        || found->length <= 0)
        return false;
    const auto clipIndex = (std::size_t) (found - song.clipGrid->clips.data());
    const auto loopStart = found->offset, loopLength = found->length;
    auto pattern = found->pattern;
    // a pattern another clip plays too is copied, so only this clip changes
    auto users = 0;
    song.forEachClipList ([&] (const std::vector<Clip>& list) {
        for (const auto& c : list)
            users += c.kind == ClipKind::pattern && c.pattern == pattern ? 1 : 0;
    });
    if (users > 1)
    {
        song.patterns.push_back (song.patterns[(std::size_t) pattern]);
        pattern = (int) song.patterns.size() - 1;
    }
    const auto bar = song.barTicks();
    auto first = notes.front().start, reach = (std::int64_t) 0;
    for (const auto& n : notes)
    {
        first = std::min (first, n.start);
        reach = std::max (reach, n.start + n.length);
    }
    const auto from = first / bar * bar;
    const auto to = std::max (from + bar, (reach + bar - 1) / bar * bar);
    // the loop's content, unrolled pass after pass up to the end of the bars played
    auto& p = song.patterns[(std::size_t) pattern];
    std::vector<Note> loop;
    for (const auto& n : p.notes)
        if (n.start >= loopStart && n.start < loopStart + loopLength)
            loop.push_back (n);
    std::vector<Note> unrolled;
    for (std::int64_t pass = 0; pass < to; pass += loopLength)
        for (auto n : loop)
        {
            n.start = pass + (n.start - loopStart);
            if (n.start >= to)
                continue;
            n.length = std::clamp<std::int64_t> (n.length, 1, std::max<std::int64_t> (1, std::min (pass + loopLength, to) - n.start));
            unrolled.push_back (n);
        }
    p.notes = std::move (unrolled);
    p.sortNotes();
    mergeNotes (p, notes);
    p.lengthTicks = to;
    auto& clip = song.clipGrid->clips[clipIndex];
    clip.pattern = pattern;
    clip.offset = from;
    clip.length = to - from;
    return true;
}

void setNotesChannel (std::vector<Note>& notes, int channel) noexcept
{
    if (channel >= 0)
        for (auto& n : notes)
            n.channel = channel;
}
} // namespace lynx::session
