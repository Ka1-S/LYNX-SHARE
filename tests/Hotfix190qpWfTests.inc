// Round 190qp (0.1.31.1 hotfix): a clip on an instrument track opens its instrument's notes in Track-only mode and the
// Simple layout, empty or not (Discord, 0.1.31); the Simple dock's Editor tab follows the clip that was opened.

#include "core/SimpleTracks.h"
#include "core/TrackClips.h"
#include "core/TrackOnly.h"

namespace hotfix190qpwf
{
// a song with an instrument track (channel `c`, its own empty pattern clip) and an unassigned track playing a pattern of
// several channels
inline lynx::Song instrumentTrackSong (int& channel, int& emptyClip, int& sharedClip)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    for (auto& p : s.patterns)
        p.notes.clear();
    ChannelDef synth;
    synth.name = "Lead";
    synth.instrument = InstrumentKind::synth;
    channel = trackmodes::addInstrumentTrack (s, synth);
    const auto track = trackmodes::instrumentTracksOf (s, channel).front();
    const auto p = trackmodes::instrumentTrackPattern (s, track); // (the pencil's pattern: empty, named after the channel)
    s.playlist.push_back ({ p, track, 0, s.barTicks(), 0 });
    s.playlist.back().kind = ClipKind::pattern;
    emptyClip = (int) s.playlist.size() - 1;
    Pattern shared;
    shared.name = "Shared";
    shared.notes.push_back ({ 0, ppq, 0, 60, 0.8f });
    shared.notes.push_back ({ 0, ppq, channel, 64, 0.8f });
    s.patterns.push_back (shared);
    s.playlist.push_back ({ (int) s.patterns.size() - 1, track, s.barTicks(), s.barTicks(), 0 });
    s.playlist.back().kind = ClipKind::pattern;
    sharedClip = (int) s.playlist.size() - 1;
    return s;
}
} // namespace hotfix190qpwf

TEST (hotfix190qp_track_clip_opens_its_instruments_piano_roll)
{
    using namespace lynx;
    int channel = -1, emptyClip = -1, sharedClip = -1;
    auto s = hotfix190qpwf::instrumentTrackSong (channel, emptyClip, sharedClip);
    CHECK (channel >= 0);
    if (! (channel >= 0))
        return;
    // an empty clip on the instrument track: that channel's Piano roll (FL's Auto rule would open the rack)
    CHECK (trackonly::pianoRollChannelOf (s, emptyClip) == channel);
    // notes of that channel only: still that channel
    s.patterns[(std::size_t) s.playlist[(std::size_t) emptyClip].pattern].notes.push_back ({ 0, ppq, channel, 60, 0.8f });
    CHECK (trackonly::pianoRollChannelOf (s, emptyClip) == channel);
    // a pattern with another channel's notes on the track: the usual rule decides
    CHECK (trackonly::pianoRollChannelOf (s, sharedClip) == -1);
    // a clip on an unassigned track, an automation clip, out of range: none
    auto unassigned = s;
    unassigned.playlist[(std::size_t) emptyClip].track = 30;
    CHECK (trackonly::pianoRollChannelOf (unassigned, emptyClip) == -1);
    auto automation = s;
    automation.playlist[(std::size_t) emptyClip].kind = ClipKind::automation;
    CHECK (trackonly::pianoRollChannelOf (automation, emptyClip) == -1);
    CHECK (trackonly::pianoRollChannelOf (s, -1) == -1 && trackonly::pianoRollChannelOf (s, 99) == -1);
}

TEST (hotfix190qp_simple_editor_tab_follows_the_opened_clip)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.playlist.clear();
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    q.channel = c;
    q.emptyBars = 4;
    const auto r = simpletracks::addTrack (s, q);
    CHECK (r.ok());
    if (! (r.ok()))
        return;
    const auto first = simpletracks::ensurePattern (s, r.track, 4);
    CHECK (first >= 0);
    if (! (first >= 0))
        return;
    // a second clip drawn later on the track (the pencil on a Simple instrument track: a new empty clip of its own)
    const auto second = trackclips::newClip (s, r.track, s.barTicks());
    CHECK (second >= 0 && second != first);
    if (! (second >= 0 && second != first))
        return;
    Clip clip;
    clip.kind = ClipKind::pattern;
    clip.pattern = second;
    clip.track = r.track;
    clip.start = 8 * s.barTicks();
    clip.length = s.barTicks();
    s.playlist.push_back (clip);
    // the dock showed the first clip, always; now the current pattern while it is one of the track's clips
    s.currentPattern = first;
    CHECK (simpletracks::editorPattern (s, r.track) == first);
    s.currentPattern = second;
    CHECK (simpletracks::editorPattern (s, r.track) == second);
    CHECK (simpletracks::patternOf (s, r.track) == first); // (patternOf unchanged)
    // a current pattern that plays elsewhere: the track's first clip
    Pattern other;
    s.patterns.push_back (other);
    s.currentPattern = (int) s.patterns.size() - 1;
    CHECK (simpletracks::editorPattern (s, r.track) == first);
    // both clips on the track open its channel's Piano roll, empty as they are
    for (int i = 0; i < (int) s.playlist.size(); ++i)
        if (s.playlist[(std::size_t) i].track == r.track && s.playlist[(std::size_t) i].kind == ClipKind::pattern)
            CHECK (trackonly::pianoRollChannelOf (s, i) == r.channels.front());
}
