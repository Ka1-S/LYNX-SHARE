#pragma once

// The Session mixer under the clip grid (round 190dr, Clip-launch workflow; docs/analise/REA_rodada190dr.md §1).
// Live's Session View has a mixer strip under each track's slots: In/Out (Audio From, Monitor, Audio To), Sends,
// Track Options (Track Delay) and Volume (volume, pan, Track Activator, Solo, Arm, meter) [SHOT ableton/probe/p3/
// session1.png; strings View>Mixer Controls>In/Out | Sends | Volume | Track Options]. A LYNX grid column is a Playlist
// track: its strip shows the mixer insert the track plays through (the Device View's rule, core/DetailChain.h), so the
// Mixer, the Device View and this strip are one insert seen three ways. Header only, no JUCE.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/DetailChain.h"
#include "core/Song.h"

namespace lynx::sessionmixer
{
// What a column's strip shows.
struct Strip
{
    int track = -1;        // the grid column (Playlist track)
    int insert = -1;       // the mixer insert it plays through (-1: none, or several and none chosen)
    int insertCount = 0;   // how many inserts the track's channels play through
    bool notes = false;    // its chain starts with an instrument channel (Live's MIDI track: "MIDI From")
};

inline bool validInsert (const Song& song, int insert) noexcept
{
    return insert > 0 && insert < (int) song.mixer.size();
}

inline Strip stripOf (const Song& song, int track)
{
    Strip s;
    s.track = track;
    const auto chain = detailchain::forTrack (song, track);
    s.insert = validInsert (song, chain.insert) ? chain.insert : -1;
    s.insertCount = (int) chain.inserts.size();
    if (s.insertCount == 0 && s.insert > 0)
        s.insertCount = 1;
    s.notes = chain.channel >= 0 && chain.channel < (int) song.channels.size()
              && song.channels[(std::size_t) chain.channel].instrument != InstrumentKind::audioClip;
    return s;
}

// --- Volume section -----------------------------------------------------------------------------------------------
// The strip's volume box reads in dB with two decimals, as Live's "-3.00"; silence is "-inf".
inline double volumeDb (float position) noexcept
{
    const auto gain = faderGain (position);
    return gain <= 1.0e-6f ? -INFINITY : 20.0 * std::log10 ((double) gain);
}
inline float positionForDb (double db) noexcept
{
    if (! std::isfinite (db) || db <= -96.0)
        return 0.0f;
    return faderPosition ((float) std::pow (10.0, db / 20.0));
}
inline std::string volumeText (float position)
{
    const auto db = volumeDb (position);
    if (! std::isfinite (db) || db < -95.0)
        return "-inf";
    char text[24];
    std::snprintf (text, sizeof (text), "%.2f", std::abs (db) < 0.005 ? 0.0 : db);
    return text;
}

// The Track Activator: on = the insert plays (LYNX's insert mute, off = muted).
inline bool isActive (const Song& song, int insert) noexcept
{
    return validInsert (song, insert) && ! song.mixer[(std::size_t) insert].muted;
}
inline void setActive (Song& song, int insert, bool on)
{
    if (validInsert (song, insert))
        song.mixer[(std::size_t) insert].muted = ! on;
}

// Solo. Live's default is exclusive solo (a click solos this track alone; again clears it), Ctrl-click adds or removes
// one track [Live manual: Exclusive Solo, on by default].
inline void solo (Song& song, int insert, bool exclusive)
{
    if (! validInsert (song, insert))
        return;
    auto& mixer = song.mixer;
    if (! exclusive)
    {
        mixer[(std::size_t) insert].solo = ! mixer[(std::size_t) insert].solo;
        return;
    }
    const auto alone = mixer[(std::size_t) insert].solo
                       && std::none_of (mixer.begin() + 1, mixer.end(), [&] (const MixerTrack& m) { return &m != &mixer[(std::size_t) insert] && m.solo; });
    for (std::size_t i = 1; i < mixer.size(); ++i)
        mixer[i].solo = ! alone && (int) i == insert;
}

// --- In/Out section -----------------------------------------------------------------------------------------------
// Live's three Monitor buttons. LYNX's "When armed" has no button of its own: it lights Auto (both follow the arm).
enum class Monitor { in, autoMode, off };
inline Monitor monitorOf (InputMonitor m) noexcept
{
    return m == InputMonitor::on ? Monitor::in : m == InputMonitor::off ? Monitor::off : Monitor::autoMode;
}
inline InputMonitor inputMonitorOf (Monitor m) noexcept
{
    return m == Monitor::in ? InputMonitor::on : m == Monitor::off ? InputMonitor::off : InputMonitor::automatic;
}

// The input's channel box: "1" for a mono input, "1/2" for a stereo pair (0-based `channel`); "" for none.
inline std::string inputChannelText (int channel, bool stereo)
{
    if (channel < 0)
        return {};
    return stereo ? std::to_string (channel + 1) + "/" + std::to_string (channel + 2) : std::to_string (channel + 1);
}

// Audio To: 0 = Main (the Master), > 0 = the insert the track goes to instead (its first send while it does not go to
// the Master), -1 = nowhere but its sends.
inline int outputOf (const Song& song, int insert) noexcept
{
    if (! validInsert (song, insert))
        return 0;
    const auto& m = song.mixer[(std::size_t) insert];
    if (m.toMaster)
        return 0;
    return m.sends.empty() ? -1 : m.sends.front().target;
}
// True when insert `from` already receives (through any chain of sends) the audio of `to`: a route from -> to would
// then make a loop.
inline bool feeds (const Song& song, int to, int from)
{
    if (to == from)
        return true;
    std::vector<int> todo { to };
    std::vector<bool> seen (song.mixer.size(), false);
    while (! todo.empty())
    {
        const auto t = todo.back();
        todo.pop_back();
        if (t < 0 || t >= (int) song.mixer.size() || seen[(std::size_t) t])
            continue;
        seen[(std::size_t) t] = true;
        for (const auto& s : song.mixer[(std::size_t) t].sends)
        {
            if (s.target == from)
                return true;
            todo.push_back (s.target);
        }
    }
    return false;
}
// The destinations Audio To offers: Main, then every other insert that would not make a loop.
inline std::vector<int> outputChoices (const Song& song, int insert)
{
    std::vector<int> out { 0 };
    if (! validInsert (song, insert))
        return out;
    for (int t = 1; t < (int) song.mixer.size(); ++t)
        if (t != insert && ! feeds (song, t, insert))
            out.push_back (t);
    return out;
}
// Sets Audio To. Main: the insert goes to the Master again and the route that was its output goes. Another insert: the
// insert stops going to the Master and goes there at unity, as its first route (its other sends stay, as Live's sends
// stay). False when nothing changed (the same place, out of range, or a loop).
inline bool setOutput (Song& song, int insert, int to)
{
    if (! validInsert (song, insert) || to < 0 || to >= (int) song.mixer.size() || to == insert)
        return false;
    const auto was = outputOf (song, insert);
    if (was == to || (to > 0 && feeds (song, to, insert)))
        return false;
    auto& m = song.mixer[(std::size_t) insert];
    const auto dropRoute = [&m] (int target) {
        m.sends.erase (std::remove_if (m.sends.begin(), m.sends.end(), [target] (const MixerSend& s) { return s.target == target; }), m.sends.end());
    };
    if (was > 0)
        dropRoute (was);
    if (to == 0)
    {
        m.toMaster = true;
        return true;
    }
    dropRoute (to);
    MixerSend route;
    route.target = to;
    route.level = 1.0f;
    m.sends.insert (m.sends.begin(), route);
    m.toMaster = false;
    return true;
}

// --- Sends section --------------------------------------------------------------------------------------------------
// The rows: every insert one of the strips' inserts sends to (Live has a row per return track; any LYNX insert can
// take a send), ascending, at most `maxRows`.
inline std::vector<int> sendRows (const Song& song, const std::vector<int>& inserts, int maxRows)
{
    std::vector<int> rows;
    for (const auto i : inserts)
        if (validInsert (song, i))
            for (const auto& s : song.mixer[(std::size_t) i].sends)
                if (s.target > 0 && s.target < (int) song.mixer.size())
                    rows.push_back (s.target);
    std::sort (rows.begin(), rows.end());
    rows.erase (std::unique (rows.begin(), rows.end()), rows.end());
    if ((int) rows.size() > maxRows)
        rows.resize ((std::size_t) std::max (0, maxRows));
    return rows;
}
// A send knob's level as a fader position (0..1, the send's own curve: FL's, 0 dB at 80 %); -1 = no such send.
inline float sendPosition (const Song& song, int from, int to) noexcept
{
    if (! validInsert (song, from))
        return -1.0f;
    for (const auto& s : song.mixer[(std::size_t) from].sends)
        if (s.target == to)
            return faderPosition (s.level);
    return -1.0f;
}
// Turning a send knob: the send's level; a knob without a send makes one when turned up (unless it would make a
// loop). True when the song changed.
inline bool setSendPosition (Song& song, int from, int to, float position)
{
    if (! validInsert (song, from) || to <= 0 || to >= (int) song.mixer.size() || to == from)
        return false;
    position = std::clamp (position, 0.0f, 1.0f);
    auto& sends = song.mixer[(std::size_t) from].sends;
    auto it = std::find_if (sends.begin(), sends.end(), [to] (const MixerSend& s) { return s.target == to; });
    if (it == sends.end())
    {
        if (position <= 0.0f || feeds (song, to, from))
            return false;
        MixerSend send;
        send.target = to;
        sends.push_back (send);
        it = sends.end() - 1;
    }
    it->level = faderGain (position);
    return true;
}

// --- Track Options --------------------------------------------------------------------------------------------------
// Live's Track Delay: positive delays the track, negative plays it earlier [Live manual, Track Delay]. LYNX keeps FL's
// manual latency, whose sign is the other way round (positive = the other paths wait, the track plays earlier).
inline double trackDelayMs (const Song& song, int insert) noexcept
{
    return validInsert (song, insert) ? -(double) song.mixer[(std::size_t) insert].latencyMs : 0.0;
}
inline void setTrackDelayMs (Song& song, int insert, double ms)
{
    if (validInsert (song, insert))
        song.mixer[(std::size_t) insert].latencyMs = (float) -std::clamp (ms, -(double) maxTrackLatencyMs, (double) maxTrackLatencyMs);
}

// --- Layout -----------------------------------------------------------------------------------------------------------
// The sections' heights in px, top to bottom as Live stacks them: In/Out, Sends, Track Options, Volume (Live's default
// set: ViewStateSessionMixerVolumeSectionHeight 120). Sections that do not fit in `available` are left out from the
// top, the Volume section last (the slots keep at least their room).
struct Sections
{
    int io = 0, sends = 0, trackOptions = 0, volume = 0;
    int total() const noexcept { return io + sends + trackOptions + volume; }
};
constexpr int ioHeight = 112, sendRowHeight = 26, trackOptionsHeight = 40, volumeHeight = 120;
inline Sections sectionHeights (const SessionMixerView& view, int sendRowCount, int available)
{
    Sections s;
    if (! view.shown)
        return s;
    s.volume = view.volume ? volumeHeight : 0;
    s.trackOptions = view.trackOptions ? trackOptionsHeight : 0;
    s.sends = view.sends && sendRowCount > 0 ? sendRowCount * sendRowHeight + 6 : 0;
    s.io = view.io ? ioHeight : 0;
    for (auto* part : { &s.io, &s.sends, &s.trackOptions, &s.volume })
        if (s.total() > std::max (0, available))
            *part = 0;
    return s;
}
} // namespace lynx::sessionmixer
