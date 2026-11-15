#include "core/FlControlMenus.h"
#include "core/PlaylistOps.h"
#include "core/QuickRender.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace lynx::clipprops
{
namespace
{
bool validSource (const Song& song, ClipKind kind, int source)
{
    switch (kind)
    {
        case ClipKind::pattern: return source >= 0 && source < (int) song.patterns.size();
        case ClipKind::audio:
            return source >= 0 && source < (int) song.channels.size() && song.channels[(std::size_t) source].instrument == InstrumentKind::audioClip;
        case ClipKind::automation: return source >= 0 && source < (int) song.automations.size();
    }
    return false;
}
} // namespace

bool read (const Song& song, int clip, Props& out)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    const auto& c = song.playlist[(std::size_t) clip];
    Props p;
    p.kind = c.kind;
    p.track = c.track;
    p.start = c.start;
    p.length = c.length;
    p.offset = c.offset;
    p.gain = c.gain;
    p.pan = c.pan;
    p.fadeIn = c.fadeIn;
    p.fadeOut = c.fadeOut;
    p.muted = c.muted;

    if (c.kind == ClipKind::pattern && validSource (song, c.kind, c.pattern))
    {
        const auto& pat = song.patterns[(std::size_t) c.pattern];
        p.source = c.pattern;
        p.name = pat.name;
        p.colour = pat.colour;
    }
    else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
    {
        const auto& ch = song.channels[(std::size_t) c.channel];
        p.source = c.channel;
        p.name = ch.name;
        p.colour = ch.colour;
        p.pitch = ch.sampler.pitch;
        p.stretchBeats = ch.sampler.stretchBeats;
        p.keepPitch = ch.sampler.keepPitch;
    }
    else if (c.kind == ClipKind::automation && validSource (song, c.kind, c.automation))
    {
        p.source = c.automation;
        p.name = song.automations[(std::size_t) c.automation].name;
    }
    out = p;
    return true;
}

bool apply (Song& song, int clip, const Props& p)
{
    if (clip < 0 || clip >= (int) song.playlist.size())
        return false;
    auto& c = song.playlist[(std::size_t) clip];

    if (p.kind == c.kind && validSource (song, c.kind, p.source))
    {
        if (c.kind == ClipKind::pattern)
            c.pattern = p.source;
        else if (c.kind == ClipKind::audio)
            c.channel = p.source;
        else
            c.automation = p.source;
    }

    c.track = std::clamp (p.track, 0, 499);
    c.start = std::max<std::int64_t> (0, p.start);
    c.length = std::max<std::int64_t> (1, p.length);
    if (c.offset != p.offset)
        c.offsetFraction = 0.0f; // an offset typed in ticks drops the imported sub-tick rest
    c.offset = std::max<std::int64_t> (0, p.offset);
    c.muted = p.muted;

    if (c.kind == ClipKind::audio)
    {
        c.gain = std::clamp (p.gain, 0.0f, 4.0f);
        c.pan = std::clamp (p.pan, -1.0f, 1.0f);
        c.fadeIn = std::clamp<std::int64_t> (p.fadeIn, 0, c.length);
        c.fadeOut = std::clamp<std::int64_t> (p.fadeOut, 0, c.length - c.fadeIn);
    }

    // the source's name and colour (and the audio channel's pitch / stretch)
    if (c.kind == ClipKind::pattern && validSource (song, c.kind, c.pattern))
    {
        auto& pat = song.patterns[(std::size_t) c.pattern];
        if (! p.name.empty())
            pat.name = p.name;
        pat.colour = p.colour;
    }
    else if (c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size())
    {
        auto& ch = song.channels[(std::size_t) c.channel];
        if (! p.name.empty())
            ch.name = p.name;
        ch.colour = p.colour;
        ch.sampler.pitch = std::clamp (p.pitch, -24.0f, 24.0f);
        ch.sampler.stretchBeats = std::max (0.0f, p.stretchBeats);
        ch.sampler.keepPitch = p.keepPitch; // (round 163: also "keep length" for a pitched clip without stretch)
    }
    else if (c.kind == ClipKind::automation && validSource (song, c.kind, c.automation) && ! p.name.empty())
        song.automations[(std::size_t) c.automation].name = p.name;

    return true;
}

void changeMute (Song& song, const std::vector<int>& clips, MuteChange change)
{
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size())
        {
            auto& m = song.playlist[(std::size_t) i].muted;
            switch (change)
            {
                case MuteChange::muteAll: m = true; break;
                case MuteChange::unmuteAll: m = false; break;
                case MuteChange::flip: m = ! m; break;
                case MuteChange::keep: break;
            }
        }
}

void changeGain (Song& song, const std::vector<int>& clips, float value, bool relative)
{
    for (auto i : clips)
        if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == ClipKind::audio)
        {
            auto& g = song.playlist[(std::size_t) i].gain;
            g = std::clamp (relative ? g * value : value, 0.0f, 4.0f);
        }
}

namespace
{
std::string bbt (std::int64_t ticks, Meter meter, int base)
{
    const auto beat = meter.beat(); // (round 190fg: the meter's beats, eighths in 6/8)
    const auto bar = meter.bar();
    const auto t = std::max<std::int64_t> (0, ticks);
    char text[48];
    std::snprintf (text, sizeof (text), "%lld:%lld:%03lld", (long long) (t / bar + base), (long long) ((t % bar) / beat + base), (long long) (t % beat));
    return text;
}

std::int64_t parseBbt (const std::string& text, Meter meter, int base)
{
    long long parts[3] = { 0, base, 0 };
    int count = 0;
    std::string token;
    auto flush = [&] {
        if (token.empty() || count >= 3)
            return false;
        for (auto c : token)
            if (c < '0' || c > '9')
                return false;
        parts[count++] = std::stoll (token);
        token.clear();
        return true;
    };
    for (auto c : text)
    {
        if (c == ' ')
            continue;
        if (c == ':' || c == '.')
        {
            if (! flush())
                return -1;
        }
        else
            token += c;
    }
    if (! flush())
        return -1;
    const auto beat = meter.beat();
    if (parts[0] < base || parts[1] < base || parts[1] - base >= std::max (1, meter.beats) || parts[2] >= beat)
        return -1;
    return (parts[0] - base) * meter.bar() + (parts[1] - base) * beat + parts[2];
}
} // namespace

std::string formatPosition (std::int64_t ticks, Meter meter) { return bbt (ticks, meter, 1); }
std::string formatLength (std::int64_t ticks, Meter meter) { return bbt (ticks, meter, 0); }
std::int64_t parsePosition (const std::string& text, Meter meter) { return parseBbt (text, meter, 1); }
std::int64_t parseLength (const std::string& text, Meter meter) { return parseBbt (text, meter, 0); }

std::vector<std::pair<int, std::string>> sourcesFor (const Song& song, ClipKind kind)
{
    std::vector<std::pair<int, std::string>> out;
    if (kind == ClipKind::pattern)
        for (int i = 0; i < (int) song.patterns.size(); ++i)
        {
            if (song.patterns[(std::size_t) i].name == "(tail)")
                continue;
            out.emplace_back (i, song.patterns[(std::size_t) i].name);
        }
    else if (kind == ClipKind::audio)
    {
        for (int i = 0; i < (int) song.channels.size(); ++i)
            if (song.channels[(std::size_t) i].instrument == InstrumentKind::audioClip)
                out.emplace_back (i, song.channels[(std::size_t) i].name);
    }
    else
        for (int i = 0; i < (int) song.automations.size(); ++i)
            out.emplace_back (i, song.automations[(std::size_t) i].name);
    return out;
}
} // namespace lynx::clipprops

namespace lynx::arrangeops
{
const std::vector<Clip>& clipsOf (const Song& song, int index)
{
    static const std::vector<Clip> none;
    if (index == song.currentArrangement)
        return song.playlist;
    if (index < 0 || index >= (int) song.arrangements.size())
        return none;
    return song.arrangements[(std::size_t) index].clips;
}

std::int64_t lengthOf (const Song& song, int index)
{
    std::int64_t end = 0;
    for (const auto& c : clipsOf (song, index))
        end = std::max (end, c.end());
    return end;
}

std::int64_t positionTick (const Song& song, MergePosition position, std::int64_t currentTick)
{
    switch (position)
    {
        case MergePosition::start: return 0;
        case MergePosition::current: return std::max<std::int64_t> (0, currentTick);
        case MergePosition::end:
        {
            const auto end = lengthOf (song, song.currentArrangement);
            const auto bar = song.barTicks();
            return (end + bar - 1) / bar * bar; // the next bar line
        }
    }
    return 0;
}

void clearRange (std::vector<Clip>& clips, std::int64_t from, std::int64_t to)
{
    if (to <= from)
        return;
    std::vector<Clip> out;
    for (auto c : clips)
    {
        if (c.end() <= from || c.start >= to)
        {
            out.push_back (c);
            continue;
        }
        if (c.start < from) // keep the head
        {
            auto head = c;
            splitClip (head, from);
            out.push_back (head);
        }
        if (c.end() > to) // keep the end
        {
            auto copy = c;
            out.push_back (splitClip (copy, to));
        }
    }
    clips = std::move (out);
}

bool merge (Song& song, int source, MergeMode mode, std::int64_t at)
{
    if (source == song.currentArrangement || source < 0 || source >= (int) song.arrangements.size())
        return false;

    const auto incoming = song.arrangements[(std::size_t) source].clips; // copy: the playlist may grow
    const auto length = lengthOf (song, source);
    at = std::max<std::int64_t> (0, at);

    if (mode == MergeMode::insert && length > 0)
    {
        // what crosses the position is split; everything after it moves later
        std::vector<Clip> moved;
        for (auto c : song.playlist)
        {
            if (c.start < at && c.end() > at)
            {
                auto rest = splitClip (c, at);
                rest.start += length;
                moved.push_back (c);
                moved.push_back (rest);
            }
            else
            {
                if (c.start >= at)
                    c.start += length;
                moved.push_back (c);
            }
        }
        song.playlist = std::move (moved);
        for (auto& m : song.markers())
            if (m.tick >= at)
                m.tick += length;
    }
    else if (mode == MergeMode::replace)
        clearRange (song.playlist, at, at + length);

    for (auto c : incoming)
    {
        c.start += at;
        song.playlist.push_back (c);
    }

    std::stable_sort (song.playlist.begin(), song.playlist.end(), [] (const Clip& a, const Clip& b) {
        return a.start != b.start ? a.start < b.start : a.track < b.track;
    });
    return true;
}

void move (Song& song, int from, int to)
{
    const auto n = (int) song.arrangements.size();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to)
        return;
    auto item = std::move (song.arrangements[(std::size_t) from]);
    song.arrangements.erase (song.arrangements.begin() + from);
    song.arrangements.insert (song.arrangements.begin() + to, std::move (item));

    auto& cur = song.currentArrangement;
    if (cur == from)
        cur = to;
    else if (from < cur && to >= cur)
        --cur;
    else if (from > cur && to <= cur)
        ++cur;
}

void rename (Song& song, int index, const std::string& name)
{
    if (index >= 0 && index < (int) song.arrangements.size() && ! name.empty())
        song.arrangements[(std::size_t) index].name = name;
}

int duplicate (Song& song, int index)
{
    if (index < 0 || index >= (int) song.arrangements.size())
        return -1;
    Arrangement copy = song.arrangements[(std::size_t) index];
    if (index == song.currentArrangement)
    {
        copy.clips = song.playlist;
        copy.loopStart = song.loopStart;
        copy.loopEnd = song.loopEnd;
    }
    copy.name += " (copy)";
    song.arrangements.insert (song.arrangements.begin() + index + 1, std::move (copy));
    if (song.currentArrangement > index)
        ++song.currentArrangement;
    return index + 1;
}
} // namespace lynx::arrangeops

namespace lynx::trackmodes
{
namespace
{
PlaylistTrack* trackAt (Song& song, int track)
{
    if (track < 0 || track >= 500)
        return nullptr;
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    return &song.playlistTracks[(std::size_t) track];
}

bool linkedElsewhere (const Song& song, int mixer, int exceptTrack)
{
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
        if (i != exceptTrack && song.playlistTracks[(std::size_t) i].mode != PlaylistTrackMode::unassigned
            && song.playlistTracks[(std::size_t) i].mixerTrack == mixer)
            return true;
    return false;
}

std::string trackName (const Song& song, int track)
{
    if (track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return song.playlistTracks[(std::size_t) track].name;
    return "Track " + std::to_string (track + 1);
}
} // namespace

int freeMixerTrack (Song& song)
{
    const auto isFree = [&song] (int t) {
        const auto& m = song.mixer[(std::size_t) t];
        if (! m.effects.empty() || linkedElsewhere (song, t, -1) || song.session.reservedForOther (true, t))
            return false;
        for (const auto& c : song.channels)
            if (c.mixerTrack == t)
                return false;
        for (int from = 1; from < (int) song.mixer.size(); ++from)
            if (from != t)
                for (const auto& s : song.mixer[(std::size_t) from].sends)
                    if (s.target == t)
                        return false;
        return true;
    };
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (isFree (t))
            return t;
    return addMixerTrack (song);
}

bool makeAudioTrack (Song& song, int track)
{
    if (track < 0 || track >= 500)
        return false;
    auto mixer = track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::audio
                     ? song.playlistTracks[(std::size_t) track].mixerTrack : -1;
    if (mixer <= 0)
        mixer = freeMixerTrack (song);
    if (mixer <= 0)
        return false;
    auto* t = trackAt (song, track);
    t->mode = PlaylistTrackMode::audio;
    t->mixerTrack = mixer;
    t->channel = -1;
    auto& m = song.mixer[(std::size_t) mixer];
    m.name = trackName (song, track);
    if (t->colour != 0)
        m.colour = t->colour;
    // the audio clips already on the track play through it from now on
    std::vector<int> onTrack;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].track == track)
            onTrack.push_back (i);
    followAudioTracks (song, onTrack);
    return true;
}

bool makeInstrumentTrack (Song& song, int track, int channel)
{
    if (track < 0 || track >= 500 || channel < 0 || channel >= (int) song.channels.size())
        return false;
    const auto kind = song.channels[(std::size_t) channel].instrument;
    if (kind == InstrumentKind::audioClip || kind == InstrumentKind::controller)
        return false; // FL: "Only generators can be used as instrument tracks."
    // a channel belongs to one instrument track
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
        if (i != track && song.playlistTracks[(std::size_t) i].mode == PlaylistTrackMode::instrument
            && song.playlistTracks[(std::size_t) i].channel == channel)
            unassign (song, i);
    auto mixer = song.channels[(std::size_t) channel].mixerTrack;
    if (mixer <= 0 || mixer >= (int) song.mixer.size() || linkedElsewhere (song, mixer, track))
        mixer = freeMixerTrack (song);
    if (mixer <= 0)
        return false;
    auto* t = trackAt (song, track);
    t->mode = PlaylistTrackMode::instrument;
    t->channel = channel;
    t->mixerTrack = mixer;
    song.channels[(std::size_t) channel].mixerTrack = mixer;
    if (t->name.empty())
        t->name = song.channels[(std::size_t) channel].name;
    return true;
}

bool canLinkToMixerTrack (const Song& song, int track, int mixer)
{
    return track >= 0 && track < 500 && mixer > 0 && mixer < (int) song.mixer.size() && ! linkedElsewhere (song, mixer, track)
           && ! song.session.reservedForOther (true, mixer);
}

bool hasOwnName (const Song& song, int mixer)
{
    if (mixer <= 0 || mixer >= (int) song.mixer.size())
        return false;
    const auto& name = song.mixer[(std::size_t) mixer].name;
    return ! name.empty() && name != "Insert " + std::to_string (mixer);
}

bool canAddMixerTrack (const Song& song)
{
    if (song.mixer.empty())
        return false;
    for (int t = (int) song.mixer.size(); t < maxMixerTracks; ++t)
        if (! song.session.reservedForOther (true, t))
            return true;
    return false;
}

int addMixerTrack (Song& song)
{
    if (! canAddMixerTrack (song))
        return -1;
    // inserts reserved for another participant are added as plain inserts and skipped (Song::claimFreeInsert's rule)
    while (true)
    {
        song.mixer.emplace_back().name = "Insert " + std::to_string (song.mixer.size());
        if (const auto t = (int) song.mixer.size() - 1; ! song.session.reservedForOther (true, t))
            return t;
    }
}

bool linkToMixerTrack (Song& song, int track, int mixer)
{
    if (! canLinkToMixerTrack (song, track, mixer))
        return false;
    auto* t = trackAt (song, track);
    if (t->mode == PlaylistTrackMode::audio && t->mixerTrack == mixer)
        return true;
    auto& m = song.mixer[(std::size_t) mixer];
    const auto insertName = hasOwnName (song, mixer) ? m.name : "Insert " + std::to_string (mixer);
    if (t->mode == PlaylistTrackMode::unassigned)
    {
        // FL (FUN_012f38f0): the insert's colour when the track has the default one; the insert's own name, else the
        // track's, else "Insert N"
        if (t->colour == 0)
            t->colour = m.colour;
        if (hasOwnName (song, mixer) || t->name.empty())
            t->name = insertName;
    }
    else if (t->mode == PlaylistTrackMode::audio && t->mixerTrack > 0 && t->mixerTrack < (int) song.mixer.size())
    {
        // FL: the previous insert is reset (name, colour; its effects and routing stay)
        auto& old = song.mixer[(std::size_t) t->mixerTrack];
        old.name = "Insert " + std::to_string (t->mixerTrack);
        old.colour = 0;
    }
    t->mode = PlaylistTrackMode::audio;
    t->mixerTrack = mixer;
    t->channel = -1;
    // FL: the track and the insert end with one name and colour
    if (t->name.empty())
    {
        t->name = insertName;
        t->colour = m.colour;
    }
    else
    {
        m.name = t->name;
        m.colour = t->colour;
    }
    std::vector<int> onTrack;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].track == track)
            onTrack.push_back (i);
    followAudioTracks (song, onTrack);
    return true;
}

int assignAudioTracks (Song& song, const std::vector<int>& tracks, int mixer)
{
    if (mixer != newMixerTrack && (mixer <= 0 || mixer >= (int) song.mixer.size()))
        return 0;
    int linked = 0;
    auto next = mixer;
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (next == newMixerTrack || next >= (int) song.mixer.size())
        {
            next = addMixerTrack (song);
            if (next < 0)
                break;
        }
        // a refused track (LYNX: the pick is linked to another selected track, e.g. the menu's own) leaves the insert
        // to the next one
        if (! linkToMixerTrack (song, tracks[i], next))
            continue;
        ++linked;
        // FL: the next insert no Playlist track is linked to (LYNX: nor reserved for another participant)
        do
            ++next;
        while (next < (int) song.mixer.size() && (linkedElsewhere (song, next, -1) || song.session.reservedForOther (true, next)));
        if (next >= maxMixerTracks)
            break;
    }
    return linked;
}

AudioTrackMenu audioTrackMenu (const Song& song, int track)
{
    AudioTrackMenu menu;
    menu.canAddMixerTrack = canAddMixerTrack (song);
    const auto* t = track >= 0 && track < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) track] : nullptr;
    for (int m = 1; m < (int) song.mixer.size(); ++m)
    {
        auto& item = menu.inserts.emplace_back();
        item.mixer = m;
        item.name = song.mixer[(std::size_t) m].name;
        // FL ticks the insert the track is stored as linked to, whatever its mode
        item.ticked = t != nullptr && t->mode != PlaylistTrackMode::unassigned && t->mixerTrack == m;
        item.enabled = item.ticked || canLinkToMixerTrack (song, track, m);
    }
    return menu;
}

int instrumentTrackPattern (Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.mode != PlaylistTrackMode::instrument || t.channel < 0 || t.channel >= (int) song.channels.size())
        return -1;
    const auto channel = t.channel;
    const auto& name = song.channels[(std::size_t) channel].name;
    for (int p = 0; p < (int) song.patterns.size(); ++p)
    {
        const auto& pattern = song.patterns[(std::size_t) p];
        if (pattern.name == name && std::all_of (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }))
            return p;
    }
    Pattern made;
    made.name = name;
    made.colour = song.channels[(std::size_t) channel].colour;
    song.patterns.push_back (std::move (made));
    return (int) song.patterns.size() - 1;
}

void unassign (Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return;
    auto& t = song.playlistTracks[(std::size_t) track];
    t.mode = PlaylistTrackMode::unassigned;
    t.mixerTrack = -1;
    t.channel = -1;
}

int firstFreeTrack (const Song& song)
{
    std::vector<bool> used (500, false);
    for (const auto& c : song.playlist)
        if (c.track >= 0 && c.track < 500)
            used[(std::size_t) c.track] = true;
    for (int t = 0; t < 500; ++t)
        if (! used[(std::size_t) t] && (t >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::unassigned))
            return t;
    return -1;
}

int addAudioTrack (Song& song)
{
    const auto track = firstFreeTrack (song);
    if (track < 0)
        return -1;
    const auto mixerBefore = song.mixer.size();
    const auto mixer = freeMixerTrack (song);
    if (mixer <= 0 || assignAudioTracks (song, { track }, mixer) != 1)
    {
        if (song.mixer.size() > mixerBefore) // (an insert freeMixerTrack added for nothing goes again)
            song.mixer.resize (mixerBefore);
        return -1;
    }
    return track;
}

bool canBeInstrumentTrack (const Song& song, int channel)
{
    if (channel < 0 || channel >= (int) song.channels.size())
        return false;
    const auto kind = song.channels[(std::size_t) channel].instrument;
    return kind != InstrumentKind::audioClip && kind != InstrumentKind::controller;
}

std::vector<int> instrumentTracksOf (const Song& song, int channel)
{
    std::vector<int> tracks;
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
        if (song.playlistTracks[(std::size_t) i].mode == PlaylistTrackMode::instrument && song.playlistTracks[(std::size_t) i].channel == channel)
            tracks.push_back (i);
    return tracks;
}

int assignToNewInstrumentTrack (Song& song, int channel)
{
    if (! canBeInstrumentTrack (song, channel))
        return -1;
    const auto track = firstFreeTrack (song);
    if (track < 0)
        return -1;
    const auto before = song;
    if (! makeInstrumentTrack (song, track, channel))
    {
        song = before;
        return -1;
    }
    return track;
}

int unassignFromInstrumentTracks (Song& song, int channel)
{
    const auto tracks = instrumentTracksOf (song, channel);
    for (const auto t : tracks)
        unassign (song, t);
    return (int) tracks.size();
}

int addInstrumentTrack (Song& song, ChannelDef generator, int track)
{
    if (generator.instrument == InstrumentKind::audioClip || generator.instrument == InstrumentKind::controller)
        return -1;
    if (track < 0)
        track = firstFreeTrack (song);
    if (track < 0 || track >= 500 || (track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::instrument))
        return -1;
    const auto before = song;
    generator.mixerTrack = 0; // (makeInstrumentTrack gives it a free mixer track)
    song.channels.push_back (std::move (generator));
    const auto channel = (int) song.channels.size() - 1;
    if (! makeInstrumentTrack (song, track, channel))
    {
        song = before;
        return -1;
    }
    return channel;
}

std::string describe (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return {};
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.mode == PlaylistTrackMode::instrument)
        return "(Instrument track)";
    if (t.mode == PlaylistTrackMode::audio)
        return "(Audio track, linked to mixer track " + std::to_string (t.mixerTrack) + ")";
    return {};
}

int armableInsert (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    return t.mode == PlaylistTrackMode::audio && t.mixerTrack > 0 && t.mixerTrack < (int) song.mixer.size() ? t.mixerTrack : -1;
}

int meteredInsert (const Song& song, int track, const HeaderOptions& options)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    if (t.mode == PlaylistTrackMode::audio)
        return options.audioLevels ? armableInsert (song, track) : -1;
    if (t.mode != PlaylistTrackMode::instrument || ! options.instrumentLevels)
        return -1;
    // the channel's own route (it may have moved since the track was made); the linked mixer track without a channel
    if (t.channel >= 0 && t.channel < (int) song.channels.size())
    {
        const auto m = song.channels[(std::size_t) t.channel].mixerTrack;
        return m >= 0 && m < (int) song.mixer.size() ? m : -1;
    }
    return t.mixerTrack >= 0 && t.mixerTrack < (int) song.mixer.size() ? t.mixerTrack : -1;
}

namespace
{
int audioMixerOf (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    const auto& t = song.playlistTracks[(std::size_t) track];
    return t.mode == PlaylistTrackMode::audio && t.mixerTrack > 0 && t.mixerTrack < (int) song.mixer.size() ? t.mixerTrack : -1;
}

// the mixer track a touched clip asks for its channel, or -1 when it needs nothing (not an audio clip on an audio
// track, or its channel already plays there)
int wantedMixer (const Song& song, const Clip& clip)
{
    const auto mixer = audioMixerOf (song, clip.track);
    if (clip.kind != ClipKind::audio || mixer <= 0 || clip.channel < 0 || clip.channel >= (int) song.channels.size()
        || song.channels[(std::size_t) clip.channel].mixerTrack == mixer)
        return -1;
    return mixer;
}

// FL's test: another clip (any arrangement) of the same channel on an audio track linked to another mixer track
bool sharedWithOtherAudioTrack (const Song& song, const Clip& clip, int mixer)
{
    bool shared = false;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& other : clips)
            if (&other != &clip && other.kind == ClipKind::audio && other.channel == clip.channel)
                if (const auto m = audioMixerOf (song, other.track); m > 0 && m != mixer)
                    shared = true;
    });
    return shared;
}
} // namespace

std::vector<int> audioTrackConflicts (const Song& song, const std::vector<int>& touched)
{
    std::vector<int> channels;
    for (const auto i : touched)
    {
        if (i < 0 || i >= (int) song.playlist.size())
            continue;
        const auto& clip = song.playlist[(std::size_t) i];
        const auto mixer = wantedMixer (song, clip);
        if (mixer > 0 && std::find (channels.begin(), channels.end(), clip.channel) == channels.end()
            && sharedWithOtherAudioTrack (song, clip, mixer))
            channels.push_back (clip.channel);
    }
    return channels;
}

int followAudioTracks (Song& song, const std::vector<int>& touched, bool makeUnique)
{
    int changed = 0;
    std::vector<int> done; // (FL: the touched clips of a channel it asked about are handled together)
    for (const auto i : touched)
    {
        if (i < 0 || i >= (int) song.playlist.size() || std::find (done.begin(), done.end(), i) != done.end())
            continue;
        const auto clip = song.playlist[(std::size_t) i];
        const auto mixer = wantedMixer (song, clip);
        if (mixer <= 0)
            continue;
        if (! sharedWithOtherAudioTrack (song, song.playlist[(std::size_t) i], mixer))
        {
            song.channels[(std::size_t) clip.channel].mixerTrack = mixer; // every clip of the channel follows (FL)
            ++changed;
            continue;
        }
        // "Make clip unique?": every touched clip of this channel gets the copy (Yes) or stays as it is (No)
        auto copy = -1;
        if (makeUnique)
        {
            auto def = song.channels[(std::size_t) clip.channel];
            def.mixerTrack = mixer;
            song.channels.push_back (def);
            copy = (int) song.channels.size() - 1;
        }
        for (const auto j : touched)
            if (j >= 0 && j < (int) song.playlist.size() && song.playlist[(std::size_t) j].channel == clip.channel
                && song.playlist[(std::size_t) j].kind == ClipKind::audio && std::find (done.begin(), done.end(), j) == done.end())
            {
                done.push_back (j);
                if (copy >= 0)
                {
                    song.playlist[(std::size_t) j].channel = copy;
                    ++changed;
                }
            }
    }
    return changed;
}

HeaderDropOptions& headerDropOptions() noexcept
{
    static HeaderDropOptions options;
    return options;
}

bool dropOnHeader (Song& song, int track, const std::string& path, const std::string& name, std::int64_t lengthTicks, HeaderDrop how, bool lock)
{
    if (track < 0 || track >= 500 || how == HeaderDrop::ask || path.empty())
        return false;
    if (track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode != PlaylistTrackMode::unassigned)
        return false; // (FUN_00e71980: only an unassigned track takes a header drop)
    const auto before = song; // (all or nothing)
    auto fail = [&song, &before] {
        song = before;
        return false;
    };

    auto newChannel = [&song, &name, &path] (InstrumentKind kind) {
        ChannelDef c;
        c.name = quickrender::uniqueChannelName (song, name);
        c.instrument = kind;
        c.samplePath = path;
        if (kind == InstrumentKind::audioClip)
            c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
        song.channels.push_back (c);
        return (int) song.channels.size() - 1;
    };
    auto placeAudio = [&song, track, lengthTicks] (int channel) {
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = channel;
        clip.track = track;
        clip.start = 0;
        clip.length = std::max<std::int64_t> (1, lengthTicks);
        song.playlist.push_back (clip);
        followAudioTracks (song, { (int) song.playlist.size() - 1 });
    };

    if (how == HeaderDrop::audioClips)
    {
        int channel = -1;
        for (int c = 0; c < (int) song.channels.size(); ++c)
            if (song.channels[(std::size_t) c].instrument == InstrumentKind::audioClip && song.channels[(std::size_t) c].samplePath == path)
                channel = c;
        if (channel < 0)
        {
            channel = newChannel (InstrumentKind::audioClip);
            song.channels[(std::size_t) channel].mixerTrack = song.claimFreeInsert();
        }
        auto* t = trackAt (song, track);
        t->name = song.channels[(std::size_t) channel].name;
        if (const auto colour = song.channels[(std::size_t) channel].colour; colour != 0)
            t->colour = colour;
        placeAudio (channel);
    }
    else if (how == HeaderDrop::audioTracks)
    {
        const auto channel = newChannel (InstrumentKind::audioClip);
        auto* t = trackAt (song, track);
        t->name = song.channels[(std::size_t) channel].name;
        if (! makeAudioTrack (song, track))
            return fail();
        placeAudio (channel); // (routed to the track's mixer track by followAudioTracks)
    }
    else
    {
        const auto channel = newChannel (InstrumentKind::sampler);
        trackAt (song, track)->name.clear(); // (named after its channel)
        if (! makeInstrumentTrack (song, track, channel))
            return fail();
        const auto pattern = instrumentTrackPattern (song, track);
        if (pattern < 0)
            return fail();
        Clip clip;
        clip.kind = ClipKind::pattern;
        clip.pattern = pattern;
        clip.track = track;
        clip.start = 0;
        clip.length = song.patterns[(std::size_t) pattern].effectiveLength (song.meter());
        song.playlist.push_back (clip);
    }
    if (lock)
        trackAt (song, track)->lockedToContent = true;
    song.mode = PlayMode::song;
    return true;
}

} // namespace lynx::trackmodes

namespace lynx::audioclips
{
int repairChannels (Song& song)
{
    int changed = 0;
    const auto channels = (int) song.channels.size(); // (copies added below are audio clip channels already)
    for (int index = 0; index < channels; ++index)
    {
        if (song.channels[(std::size_t) index].instrument != InstrumentKind::sampler || song.channels[(std::size_t) index].samplePath.empty())
            continue;

        auto used = false;
        song.forEachClipList ([&] (const std::vector<Clip>& clips) {
            for (const auto& c : clips)
                used = used || (c.kind == ClipKind::audio && c.channel == index);
        });
        if (! used)
            continue;

        const auto playsNotes = std::any_of (song.patterns.begin(), song.patterns.end(), [index] (const Pattern& p) {
            return std::any_of (p.notes.begin(), p.notes.end(), [index] (const Note& n) { return n.channel == index; });
        });
        ++changed;
        if (! playsNotes)
        {
            song.channels[(std::size_t) index].instrument = InstrumentKind::audioClip;
            continue;
        }

        auto copy = song.channels[(std::size_t) index];
        copy.instrument = InstrumentKind::audioClip;
        copy.instance.renew(); // (a new channel: the renderer must not take it for the Sampler it was copied from)
        song.channels.push_back (std::move (copy));
        const auto target = (int) song.channels.size() - 1;
        song.forEachClipList ([&] (std::vector<Clip>& clips) {
            for (auto& c : clips)
                if (c.kind == ClipKind::audio && c.channel == index)
                    c.channel = target;
        });
    }
    return changed;
}

void loadSampleInto (ChannelDef& channel, const std::string& path, const std::string& name, bool autoName)
{
    channel.samplePath = path;
    samplehistory::add (path); // FL's Sample history (round 190df): every sample a channel loads
    if (channel.instrument == InstrumentKind::granulizer || channel.instrument == InstrumentKind::wavetraveller)
        return; // FL's Granulizer keeps its engine and plays the file (round 142); so does Wave Traveller
    if (channel.instrument != InstrumentKind::audioClip)
        channel.instrument = InstrumentKind::sampler;
    // FL's General › "Auto name channels" (round 190bx): off = a channel that has a name keeps it
    if (autoName || channel.name.empty())
        channel.name = name;
}
} // namespace lynx::audioclips

namespace lynx::autoclipplace
{
Place forNewClip (const Song& song, bool atPlayback, std::int64_t playTick)
{
    const auto bar = song.barTicks();
    const auto songEnd = song.playlist.empty() ? (std::int64_t) 0 : song.songLength();
    Place place;
    if (song.hasLoop())
    {
        place.start = song.loopStart;
        place.length = song.loopEnd - song.loopStart;
    }
    else
    {
        // FL (checked live, round 177b): the clip starts at the song position even past the last clip, and is one bar
        // long when nothing lies after it.
        place.start = atPlayback ? std::max ((std::int64_t) 0, playTick) : 0;
        place.length = std::max (bar, songEnd - place.start);
    }
    // FL (checked live, round 177b): the first track with no clips at all; a free gap on a used track is not taken
    auto busy = [&] (int track) {
        if (track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::instrument)
            return true;
        for (const auto& c : song.playlist)
            if (c.track == track)
                return true;
        return false;
    };
    while (busy (place.track))
        ++place.track;
    return place;
}
} // namespace lynx::autoclipplace

namespace lynx::autolanes
{
int ownerTrack (const Song& song, int automation)
{
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        for (const auto& l : song.playlistTracks[(std::size_t) t].lanes)
            if (l.automation == automation)
                return t;
    return -1;
}

namespace
{
std::int64_t songEnd (const Song& song)
{
    const auto bar = song.barTicks();
    return std::max (bar * 4, song.playlist.empty() ? 0 : song.songLength());
}

int laneFor (const Song& song, int track, const AutomationTarget& target)
{
    if (track < 0 || track >= (int) song.playlistTracks.size())
        return -1;
    for (const auto& l : song.playlistTracks[(std::size_t) track].lanes)
        if (l.automation >= 0 && l.automation < (int) song.automations.size() && song.automations[(std::size_t) l.automation].target == target)
            return l.automation;
    return -1;
}
} // namespace

int trackFor (const Song& song, const AutomationTarget& target)
{
    const auto& tracks = song.playlistTracks;
    for (int t = 0; t < (int) tracks.size(); ++t)
        if (laneFor (song, t, target) >= 0)
            return t;

    int channel = -1, insert = -1;
    switch (target.kind)
    {
        case AutomationTargetKind::channelVolume:
        case AutomationTargetKind::channelPan:
        case AutomationTargetKind::channelParam:
        case AutomationTargetKind::channelAdjust:
            channel = target.index;
            if (channel >= 0 && channel < (int) song.channels.size())
                insert = song.channels[(std::size_t) channel].mixerTrack;
            break;
        case AutomationTargetKind::mixerVolume:
        case AutomationTargetKind::mixerPan:
        case AutomationTargetKind::effectParam:
        case AutomationTargetKind::effectMix:
        case AutomationTargetKind::mixerSend:
            insert = target.index;
            break;
        case AutomationTargetKind::none:
        case AutomationTargetKind::tempo:
            break;
    }
    if (channel >= 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode == PlaylistTrackMode::instrument && tracks[(std::size_t) t].channel == channel)
                return t;
    if (insert > 0)
        for (int t = 0; t < (int) tracks.size(); ++t)
            if (tracks[(std::size_t) t].mode != PlaylistTrackMode::unassigned && tracks[(std::size_t) t].mixerTrack == insert)
                return t;

    for (int t = 0; t < 500; ++t)
    {
        if (t < (int) tracks.size() && ! tracks[(std::size_t) t].lanes.empty())
            continue;
        if (std::none_of (song.playlist.begin(), song.playlist.end(), [t] (const Clip& c) { return c.track == t; }))
            return t;
    }
    return 0;
}

int addLane (Song& song, int track, const AutomationTarget& target, const std::string& name, float value)
{
    if (track < 0 || track >= 500)
        return -1;
    if (const auto existing = laneFor (song, track, target); existing >= 0)
        return existing;
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);
    Automation a;
    a.name = name;
    a.target = target;
    a.points = { { 0, value, 0.0f }, { songEnd (song), value, 0.0f } };
    song.automations.push_back (std::move (a));
    const auto index = (int) song.automations.size() - 1;
    song.playlistTracks[(std::size_t) track].lanes.push_back ({ index, 0 });
    song.playlistTracks[(std::size_t) track].lanesFolded = false;
    return index;
}

bool removeLane (Song& song, int track, int lane)
{
    if (track < 0 || track >= (int) song.playlistTracks.size() || lane < 0 || lane >= (int) song.playlistTracks[(std::size_t) track].lanes.size())
        return false;
    song.removeAutomation (song.playlistTracks[(std::size_t) track].lanes[(std::size_t) lane].automation); // (drops the lane too)
    return true;
}

int clipToLane (Song& song, int clipIndex, int track)
{
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return -1;
    const auto clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::automation || clip.automation < 0 || clip.automation >= (int) song.automations.size() || clip.length <= 0)
        return -1;
    const auto source = song.automations[(std::size_t) clip.automation]; // (a copy: addLane may grow the list)
    auto lane = laneFor (song, track, source.target);
    if (lane < 0)
        lane = addLane (song, track, source.target, source.name, source.valueAt ((double) clip.offset));
    if (lane < 0)
        return -1;

    // the clip's curve in song time: its edges exact, the points inside it shifted by start − offset
    std::vector<AutomationPoint> inside;
    inside.push_back ({ clip.start, source.valueAt ((double) clip.offset), 0.0f });
    for (const auto& p : source.points)
        if (p.tick > clip.offset && p.tick < clip.offset + clip.length)
            inside.push_back ({ clip.start + p.tick - clip.offset, p.value, p.tension, p.mode });
    inside.push_back ({ clip.end(), source.valueAt ((double) (clip.offset + clip.length)), 0.0f });

    auto& points = song.automations[(std::size_t) lane].points;
    // what the lane had before and after the clip stays its own value up to the clip's edges
    const auto before = song.automations[(std::size_t) lane].valueAt ((double) clip.start - 1.0);
    const auto after = song.automations[(std::size_t) lane].valueAt ((double) clip.end() + 1.0);
    points.erase (std::remove_if (points.begin(), points.end(), [&] (const AutomationPoint& p) { return p.tick >= clip.start && p.tick <= clip.end(); }),
                  points.end());
    points.insert (points.end(), inside.begin(), inside.end());
    if (clip.start > 0 && std::none_of (points.begin(), points.end(), [&] (const AutomationPoint& p) { return p.tick == clip.start - 1; }))
        points.push_back ({ clip.start - 1, before, 0.0f });
    if (std::none_of (points.begin(), points.end(), [&] (const AutomationPoint& p) { return p.tick == clip.end() + 1; }))
        points.push_back ({ clip.end() + 1, after, 0.0f });
    std::sort (points.begin(), points.end(), [] (const AutomationPoint& a, const AutomationPoint& b) { return a.tick < b.tick; });

    // the clip goes (its automation stays: other clips may use it, as FL keeps it)
    song.playlist.erase (song.playlist.begin() + clipIndex);
    return lane;
}

int laneToClip (Song& song, int track, int lane)
{
    if (track < 0 || track >= (int) song.playlistTracks.size() || lane < 0 || lane >= (int) song.playlistTracks[(std::size_t) track].lanes.size())
        return -1;
    const auto automation = song.playlistTracks[(std::size_t) track].lanes[(std::size_t) lane].automation;
    auto& lanes = song.playlistTracks[(std::size_t) track].lanes;
    lanes.erase (lanes.begin() + lane);
    if (automation < 0 || automation >= (int) song.automations.size())
        return -1;
    const auto& points = song.automations[(std::size_t) automation].points;
    const auto end = std::max (songEnd (song), points.empty() ? 0 : points.back().tick);

    Clip clip;
    clip.kind = ClipKind::automation;
    clip.automation = automation;
    clip.start = 0;
    clip.length = std::max<std::int64_t> (1, end);
    clip.track = 0;
    // the first track whose [0, end) is free (instrument tracks take no automation clips)
    auto busy = [&] (int t) {
        if (t < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) t].mode == PlaylistTrackMode::instrument)
            return true;
        for (const auto& c : song.playlist)
            if (c.track == t && c.start < clip.length && c.end() > 0)
                return true;
        return false;
    };
    while (busy (clip.track))
        ++clip.track;
    song.playlist.push_back (clip);
    return (int) song.playlist.size() - 1;
}

int lanesToClips (Song& song)
{
    int n = 0;
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        while (! song.playlistTracks[(std::size_t) t].lanes.empty())
            if (laneToClip (song, t, 0) >= 0)
                ++n;
    return n;
}
} // namespace lynx::autolanes

namespace lynx::autolink
{
namespace
{
bool linkedToAnotherTrack (const Song& song, int mixer, int track)
{
    for (int i = 0; i < (int) song.playlistTracks.size(); ++i)
        if (i != track && song.playlistTracks[(std::size_t) i].mode != PlaylistTrackMode::unassigned
            && song.playlistTracks[(std::size_t) i].mixerTrack == mixer)
            return true;
    return false;
}

bool unassigned (const Song& song, int track)
{
    return track >= (int) song.playlistTracks.size() || song.playlistTracks[(std::size_t) track].mode == PlaylistTrackMode::unassigned;
}

// What a track's clips hold (automation clips don't count)
struct Content
{
    bool audio = false, patterns = false, invalid = false;
    std::vector<int> audioChannels; // each once
    std::vector<int> noteChannels;  // the channels the patterns' notes play, each once
    Plan::Kind kind() const
    {
        if (invalid || (audio && patterns))
            return Plan::Kind::none;
        if (audio && ! audioChannels.empty())
            return Plan::Kind::audio;
        if (patterns && noteChannels.size() == 1)
            return Plan::Kind::instrument;
        return Plan::Kind::none;
    }
};

void addOnce (std::vector<int>& v, int x)
{
    if (std::find (v.begin(), v.end(), x) == v.end())
        v.push_back (x);
}

// every track's content, in one pass (patterns' channels are gathered once per pattern)
std::vector<Content> contents (const Song& song)
{
    std::vector<Content> out (500);
    std::vector<std::vector<int>> patternChannels (song.patterns.size());
    std::vector<bool> gathered (song.patterns.size(), false);
    const auto channels = (int) song.channels.size();
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        for (const auto& c : clips)
        {
            if (c.track < 0 || c.track >= 500)
                continue;
            auto& t = out[(std::size_t) c.track];
            if (c.kind == ClipKind::audio)
            {
                t.audio = true;
                if (c.channel >= 0 && c.channel < channels)
                    addOnce (t.audioChannels, c.channel);
                else
                    t.invalid = true;
            }
            else if (c.kind == ClipKind::pattern)
            {
                t.patterns = true;
                if (c.pattern < 0 || c.pattern >= (int) song.patterns.size())
                {
                    t.invalid = true;
                    continue;
                }
                const auto p = (std::size_t) c.pattern;
                if (! gathered[p])
                {
                    gathered[p] = true;
                    for (const auto& n : song.patterns[p].notes)
                        if (n.channel >= 0 && n.channel < channels)
                            addOnce (patternChannels[p], n.channel);
                }
                for (const auto ch : patternChannels[p])
                    addOnce (t.noteChannels, ch);
            }
        }
    });
    return out;
}

Plan planOf (const Song& song, int track, const Content& content)
{
    Plan plan;
    if (track < 0 || track >= 500 || ! unassigned (song, track))
        return plan;
    const auto inserts = (int) song.mixer.size();
    const auto usable = [&] (int mixer) {
        return mixer == 0 || (mixer > 0 && mixer < inserts && ! linkedToAnotherTrack (song, mixer, track) && ! song.session.reservedForOther (true, mixer));
    };
    switch (content.kind())
    {
        case Plan::Kind::none: return plan;
        case Plan::Kind::audio:
        {
            // every channel through one insert
            const auto route = song.channels[(std::size_t) content.audioChannels.front()].mixerTrack;
            for (const auto ch : content.audioChannels)
                if (song.channels[(std::size_t) ch].mixerTrack != route)
                    return plan;
            if (inserts < 2 || ! usable (route))
                return plan;
            plan.kind = Plan::Kind::audio;
            plan.mixer = route;
            return plan;
        }
        case Plan::Kind::instrument:
        {
            const auto ch = content.noteChannels.front();
            if (! trackmodes::canBeInstrumentTrack (song, ch) || ! trackmodes::instrumentTracksOf (song, ch).empty())
                return plan;
            const auto route = song.channels[(std::size_t) ch].mixerTrack;
            if (inserts < 2 || ! usable (route))
                return plan;
            plan.kind = Plan::Kind::instrument;
            plan.mixer = route;
            plan.channel = ch;
            return plan;
        }
    }
    return plan;
}

bool link (Song& song, int track, const Content& content)
{
    const auto plan = planOf (song, track, content);
    if (plan.kind == Plan::Kind::none)
        return false;
    const auto fromMaster = plan.mixer == 0;
    const auto mixerBefore = song.mixer.size();
    const auto mixer = fromMaster ? insertForTrack (song, track) : plan.mixer;
    if (mixer <= 0 || ! trackmodes::canLinkToMixerTrack (song, track, mixer))
    {
        if (song.mixer.size() > mixerBefore) // (inserts added for nothing go again)
            song.mixer.resize (mixerBefore);
        return false;
    }
    if ((int) song.playlistTracks.size() <= track)
        song.playlistTracks.resize ((std::size_t) track + 1);

    if (plan.kind == Plan::Kind::audio)
    {
        // a track of one sample is named after it (before the link gives the track and the insert one name)
        auto& t = song.playlistTracks[(std::size_t) track];
        if (t.name.empty() && content.audioChannels.size() == 1)
            t.name = song.channels[(std::size_t) content.audioChannels.front()].name;
        if (fromMaster)
            for (const auto ch : content.audioChannels)
                song.channels[(std::size_t) ch].mixerTrack = mixer;
        return trackmodes::linkToMixerTrack (song, track, mixer);
    }

    auto& channel = song.channels[(std::size_t) plan.channel];
    if (fromMaster)
    {
        channel.mixerTrack = mixer;
        if (! trackmodes::hasOwnName (song, mixer)) // a new insert carries the instrument's name and colour
        {
            song.mixer[(std::size_t) mixer].name = channel.name;
            song.mixer[(std::size_t) mixer].colour = channel.colour;
        }
    }
    return trackmodes::makeInstrumentTrack (song, track, plan.channel);
}
} // namespace

bool isNeutralFree (const Song& song, int mixer)
{
    if (mixer <= 0 || mixer >= (int) song.mixer.size() || song.session.reservedForOther (true, mixer) || linkedToAnotherTrack (song, mixer, -1))
        return false;
    for (const auto& c : song.channels)
        if (c.mixerTrack == mixer)
            return false;
    for (int from = 0; from < (int) song.mixer.size(); ++from)
        if (from != mixer)
            for (const auto& s : song.mixer[(std::size_t) from].sends)
                if (s.target == mixer)
                    return false;
    const auto& m = song.mixer[(std::size_t) mixer];
    const MixerTrack plain;
    return m.effects.empty() && m.volume == plain.volume && m.pan == plain.pan && ! m.muted && ! m.solo && m.toMaster
           && m.masterLevel == plain.masterLevel && m.stereo == plain.stereo && ! m.invertPhase && ! m.swapChannels && m.sends.empty()
           && m.eq == plain.eq && m.latencyMs == plain.latencyMs && m.outputChannel == plain.outputChannel && m.inputChannel < 0;
}

int insertForTrack (Song& song, int track)
{
    const auto paired = track + 1;
    if (paired > 0 && paired < maxMixerTracks)
    {
        // the track's own number: inserts up to it are added when the mixer is shorter (empty inserts change nothing)
        const auto before = song.mixer.size();
        while ((int) song.mixer.size() <= paired && trackmodes::canAddMixerTrack (song))
            if (trackmodes::addMixerTrack (song) < 0)
                break;
        if (isNeutralFree (song, paired))
            return paired;
        if (song.mixer.size() > before) // (added for nothing: they go again)
            song.mixer.resize (before);
    }
    for (int m = 1; m < (int) song.mixer.size(); ++m)
        if (isNeutralFree (song, m))
            return m;
    return trackmodes::addMixerTrack (song);
}

Plan planFor (const Song& song, int track)
{
    if (track < 0 || track >= 500)
        return {};
    return planOf (song, track, contents (song)[(std::size_t) track]);
}

bool linkTrack (Song& song, int track)
{
    if (track < 0 || track >= 500)
        return false;
    return link (song, track, contents (song)[(std::size_t) track]);
}

std::vector<Plan::Kind> contentKinds (const Song& song)
{
    const auto all = contents (song);
    std::vector<Plan::Kind> kinds (all.size());
    for (std::size_t t = 0; t < all.size(); ++t)
        kinds[t] = all[t].kind();
    return kinds;
}

int setOn (Song& song, bool on)
{
    song.linkTracksToMixer = on;
    if (! on)
        return 0;
    const auto all = contents (song);
    int linked = 0;
    for (int t = 0; t < (int) all.size(); ++t)
        if (all[(std::size_t) t].kind() != Plan::Kind::none && link (song, t, all[(std::size_t) t]))
            ++linked;
    return linked;
}

int followNewContent (Song& song, const std::vector<Plan::Kind>& before)
{
    if (! song.linkTracksToMixer)
        return 0;
    const auto all = contents (song);
    int linked = 0;
    for (int t = 0; t < (int) all.size(); ++t)
    {
        const auto was = t < (int) before.size() ? before[(std::size_t) t] : Plan::Kind::none;
        if (was == Plan::Kind::none && all[(std::size_t) t].kind() != Plan::Kind::none && link (song, t, all[(std::size_t) t]))
            ++linked;
    }
    return linked;
}

int pairedTakeTrack (const Song& song, int insert)
{
    const auto track = insert - 1;
    if (! song.linkTracksToMixer || insert <= 0 || insert >= (int) song.mixer.size() || track >= 500)
        return -1;
    if (track < (int) song.playlistTracks.size())
    {
        const auto& t = song.playlistTracks[(std::size_t) track];
        if (t.mode != PlaylistTrackMode::unassigned || t.grouped)
            return -1;
    }
    bool used = false;
    song.forEachClipList ([&] (const std::vector<Clip>& clips) {
        used = used || std::any_of (clips.begin(), clips.end(), [track] (const Clip& c) { return c.track == track; });
    });
    return used ? -1 : track;
}
} // namespace lynx::autolink
