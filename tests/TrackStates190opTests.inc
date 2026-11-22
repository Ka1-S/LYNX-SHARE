// Round 190op (research190oe P4 / P7, LYNX extras): Hide tracks, Deactivate tracks (core/TrackStates.h) and Capture
// automation (core/CaptureAutomation.h). The renders (a hidden track plays the same, deactivated units are silent,
// Reactivate nulls), the .lynx round trip, the copies for 0.1.30 / 0.1.28, the .flp report and the engine's capture in
// one undo step are checked in the app's --selftest ("trackstates190op"): they need the engine and ProjectFile.

#include "core/AlsExport.h"
#include "core/CaptureAutomation.h"
#include "core/DawProject.h"
#include "core/Recorder.h"
#include "core/TrackStates.h"

namespace trackstates190op
{
inline lynx::Clip clipOn (int track, std::int64_t start)
{
    lynx::Clip c;
    c.pattern = 0;
    c.track = track;
    c.start = start;
    c.length = 4 * lynx::ppq;
    return c;
}

// Lead on insert 1, Bass on insert 2, Pad on insert 3; insert 4 sends only into insert 3 (no route to the Master)
inline lynx::Song mixSong()
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (6);
    const char* names[] = { "Lead", "Bass", "Pad" };
    for (int c = 0; c < 3; ++c)
    {
        ChannelDef d;
        d.name = names[c];
        d.mixerTrack = c + 1;
        s.channels.push_back (d);
    }
    s.mixer[4].toMaster = false;
    s.mixer[4].sends.push_back (MixerSend { 3, 1.0f });
    s.patterns.resize (1);
    return s;
}
} // namespace trackstates190op

// Hide: a view flag on Playlist tracks; Show all; "Hide unused tracks" hides the empty tracks above the last used one,
// never a group's tracks or a track with lanes, never the empty rows below
TEST (trackstates190op_hide_and_hide_unused)
{
    using namespace lynx;
    Song s;
    s.patterns.resize (1);
    CHECK (trackstates::hiddenCount (s) == 0 && ! trackstates::isHidden (s, 3) && ! trackstates::isHidden (s, -1));
    CHECK (trackstates::setHidden (s, { 3, 5 }, true) == 2 && trackstates::hiddenCount (s) == 2 && trackstates::isHidden (s, 5));
    CHECK (s.playlistTracks.size() == 6);
    CHECK (trackstates::setHidden (s, { 3 }, true) == 0);              // already hidden
    CHECK (trackstates::setHidden (s, { 40 }, false) == 0 && s.playlistTracks.size() == 6); // (showing adds no rows)
    CHECK (trackstates::showAll (s) == 2 && trackstates::hiddenCount (s) == 0);

    // clips on 0 and 4, a grid clip on 9; a group: header 2, member 3; lanes on 6
    s.playlist.push_back (trackstates190op::clipOn (0, 0));
    s.playlist.push_back (trackstates190op::clipOn (4, 0));
    s.clipGrid.emplace();
    s.clipGrid->clips.push_back (trackstates190op::clipOn (9, 0));
    s.playlistTracks.resize (10);
    s.playlistTracks[3].grouped = true;
    s.playlistTracks[6].lanes.push_back ({ 0, 0 });
    const auto hidden = trackstates::hideUnused (s);
    CHECK (hidden == 4); // 1, 5, 7, 8
    for (int t = 0; t < 12; ++t)
        CHECK (trackstates::isHidden (s, t) == (t == 1 || t == 5 || t == 7 || t == 8));
    CHECK (trackstates::hideUnused (s) == 0);
    // a clip in another arrangement counts as used
    Song a;
    a.patterns.resize (1);
    a.playlist.push_back (trackstates190op::clipOn (3, 0));
    a.addArrangement ("Second");
    a.playlist.push_back (trackstates190op::clipOn (0, 0));
    CHECK (trackstates::hideUnused (a) == 2); // 1, 2 (3 holds a clip in the first arrangement)
    CHECK (! trackstates::isHidden (a, 0) && trackstates::isHidden (a, 1) && trackstates::isHidden (a, 2) && ! trackstates::isHidden (a, 3));
}

// Deactivate: by hand, and what plays only into a deactivated insert (channels on it, inserts that route only into it)
TEST (trackstates190op_effective_units)
{
    using namespace lynx;
    auto s = trackstates190op::mixSong();
    CHECK (! deactivate::anyMarked (s) && ! deactivate::effective (s).any);
    CHECK (deactivate::setChannels (s, { 1 }, true) == 1);
    auto e = deactivate::effective (s);
    CHECK (e.any && e.channel (1) && ! e.channel (0) && ! e.channel (2) && ! e.insert (2));
    CHECK (deactivate::setInserts (s, { 3, 0 }, true) == 1); // (the Master never)
    e = deactivate::effective (s);
    CHECK (e.insert (3) && e.insert (4) && e.channel (2) && ! e.insert (0) && ! e.insert (1));
    // insert 4 also routed to the Master: it plays on
    s.mixer[4].toMaster = true;
    CHECK (! deactivate::effective (s).insert (4));
    // reactivated: nothing
    deactivate::setChannels (s, { 1 }, false);
    deactivate::setInserts (s, { 3 }, false);
    CHECK (! deactivate::effective (s).any);
}

// The engine's projection: a silent placeholder channel (same insert, name, id), no effects and muted on the insert,
// automation of their parameters detached, everything else untouched; the song itself keeps every setting
TEST (trackstates190op_projection)
{
    using namespace lynx;
    auto s = trackstates190op::mixSong();
    s.channels[0].instrument = InstrumentKind::plugin;
    s.channels[0].plugin.id = "vst3:Some Synth";
    s.channels[0].plugin.state = { 1, 2, 3 };
    s.channels[0].uid = 77;
    s.mixer[2].effects.emplace_back();
    s.mixer[2].effects.back().kind = EffectKind::plugin;
    s.mixer[2].effects.back().plugin.id = "vst3:Some Verb";
    s.mixer[1].effects.emplace_back();
    Automation synthParam, verbParam, leadVolume, otherFx;
    synthParam.target = { AutomationTargetKind::channelParam, 0, 0, 5 };
    verbParam.target = { AutomationTargetKind::effectParam, 2, 0, 1 };
    leadVolume.target = { AutomationTargetKind::channelVolume, 0, 0, 0 };
    otherFx.target = { AutomationTargetKind::effectParam, 1, 0, 1 };
    s.automations = { synthParam, verbParam, leadVolume, otherFx };
    s.channels[0].deactivated = true;
    s.mixer[2].deactivated = true;
    const auto before = s;
    const auto p = deactivate::projected (s);
    CHECK (s.channels == before.channels && s.mixer == before.mixer); // (the song is not touched)
    CHECK (p.channels.size() == s.channels.size() && p.mixer.size() == s.mixer.size());
    CHECK (p.channels[0].instrument == InstrumentKind::audioClip && p.channels[0].plugin.id.empty() && p.channels[0].muted);
    CHECK (p.channels[0].mixerTrack == 1 && p.channels[0].name == "Lead" && p.channels[0].uid == 77);
    CHECK (p.channels[1].instrument == InstrumentKind::audioClip && p.channels[1].muted); // (Bass plays only into insert 2)
    CHECK (p.channels[2] == s.channels[2]);
    CHECK (p.mixer[2].effects.empty() && p.mixer[2].muted && p.mixer[1].effects.size() == 1 && ! p.mixer[1].muted);
    CHECK (p.automations[0].target.kind == AutomationTargetKind::none && p.automations[1].target.kind == AutomationTargetKind::none);
    CHECK (p.automations[2].target == leadVolume.target && p.automations[3].target == otherFx.target);
    CHECK (s.channels[0].plugin.state == before.channels[0].plugin.state && ! s.channels[0].plugin.state.empty()); // the saved state stays for Reactivate
    // a project without any: the same song
    auto plain = trackstates190op::mixSong();
    const auto same = deactivate::projected (plain);
    CHECK (same.channels == plain.channels && same.mixer == plain.mixer);
}

// A track's units for "Deactivate track": its channels and its own insert (not one other channels play into)
TEST (trackstates190op_track_units)
{
    using namespace lynx;
    auto s = trackstates190op::mixSong();
    s.patterns[0].notes.push_back ({});
    s.patterns[0].notes.back().channel = 1;
    s.playlist.push_back (trackstates190op::clipOn (2, 0));
    auto u = trackstates::unitsOfTrack (s, 2);
    CHECK (u.channels == std::vector<int> ({ 1 }) && u.inserts == std::vector<int> ({ 2 }));
    CHECK (! trackstates::trackDeactivated (s, u));
    s.channels[1].deactivated = true;
    CHECK (trackstates::trackDeactivated (s, u));
    s.channels[1].deactivated = false;
    s.mixer[2].deactivated = true;
    CHECK (trackstates::trackDeactivated (s, u));
    // Lead shares insert 2 now: the insert is not the track's own
    s.mixer[2].deactivated = false;
    s.channels[0].mixerTrack = 2;
    u = trackstates::unitsOfTrack (s, 2);
    CHECK (u.channels == std::vector<int> ({ 1 }) && u.inserts.empty());
    CHECK (trackstates::unitsOfTrack (s, 7).empty());
}

// .als export: a deactivated track is written with its Track Activator off; DAWproject: "loaded" false, both ways
TEST (trackstates190op_als_and_dawproject)
{
    using namespace lynx;
    auto s = trackstates190op::mixSong();
    for (int c = 0; c < 2; ++c)
    {
        Note n;
        n.channel = c;
        s.patterns[0].notes.push_back (n);
    }
    s.playlist.push_back (trackstates190op::clipOn (0, 0));
    s.channels[1].deactivated = true;
    const auto dp = exportDawProject (s);
    CHECK (! dp.bytes.empty());
    CHECK (dp.projectXml.find ("loaded=\"false\"") != std::string::npos);
    const auto back = importDawProject (dp.bytes);
    CHECK (back.has_value());
    if (back)
        CHECK (std::count_if (back->channels.begin(), back->channels.end(), [] (const ChannelDef& c) { return c.deactivated; }) == 1);
    // .als: the Bass track's Activator (Speaker) is off, the note says why; nothing for a set without deactivated tracks
    const auto speakersOff = [] (const std::string& xml) {
        int n = 0;
        for (auto at = xml.find ("<Speaker>"); at != std::string::npos; at = xml.find ("<Speaker>", at + 1))
        {
            const auto manual = xml.find ("<Manual Value=\"", at);
            n += manual != std::string::npos && xml.compare (manual + 15, 5, "false") == 0 ? 1 : 0;
        }
        return n;
    };
    const auto als = exportAls (s);
    CHECK (speakersOff (als.xml) == 1);
    CHECK (std::any_of (als.notes.begin(), als.notes.end(), [] (const std::string& n) { return n.find ("Track Activator off") != std::string::npos; }));
    s.channels[1].deactivated = false;
    CHECK (exportDawProject (s).projectXml.find ("loaded=\"false\"") == std::string::npos);
    CHECK (speakersOff (exportAls (s).xml) == 0);
}

// Capture automation's buffer: at most one point per control per 10 ms, the window, and the last continuous gesture
// per control (a pause of more than two bars, a jump back or a new play ends one)
TEST (trackstates190op_capture_buffer)
{
    using namespace lynx;
    captureauto::Buffer b;
    const AutomationTarget vol { AutomationTargetKind::mixerVolume, 1, 0, 0 }, pan { AutomationTargetKind::mixerPan, 2, 0, 0 };
    auto entry = [] (const AutomationTarget& t, double ms, double tick, float v) {
        captureauto::Entry e;
        e.target = t;
        e.ms = ms;
        e.tick = tick;
        e.value = v;
        return e;
    };
    b.add (entry (vol, 100.0, 0.0, 0.1f));
    b.add (entry (vol, 105.0, 10.0, 0.2f)); // < 10 ms: replaces the last one
    CHECK (b.size() == 1 && b.entries().back().value == 0.2f && b.entries().back().tick == 10.0);
    b.add (entry (pan, 106.0, 10.0, 0.5f));
    b.add (entry (vol, 107.0, 12.0, 0.3f)); // (the last vol is 7 ms back, behind a pan sample: still merged)
    CHECK (b.size() == 2 && b.entries().front().value == 0.3f);
    b.add (entry (vol, 120.0, 20.0, 0.4f));
    CHECK (b.size() == 3);
    // the window: two minutes
    b.add (entry (pan, 100.0 + captureauto::defaultWindowMs + 110.0, 30.0, 0.6f));
    CHECK (b.size() == 1 && b.entries().front().target == pan);
    b.clear();

    // the brief's case: three gestures on two controls -> two automations, the last gesture of each
    const auto bar = 4 * ppq;
    b.add (entry (vol, 1000.0, 0.0, 0.2f));        // gesture 1 (vol)
    b.add (entry (vol, 1050.0, 0.5 * bar, 0.3f));
    b.add (entry (pan, 1100.0, 0.5 * bar, 0.1f));  // gesture 2 (pan)
    b.add (entry (pan, 1200.0, 1.0 * bar, 0.4f));
    b.add (entry (vol, 3000.0, 3.0 * bar, 0.6f));  // gesture 3 (vol, 2.5 bars after its last point)
    b.add (entry (vol, 3100.0, 3.25 * bar, 0.8f));
    auto g = captureauto::lastGestures (b.entries(), -1.0, 2 * bar);
    CHECK (g.size() == 2);
    if (g.size() == 2)
    {
        CHECK (g[0].target == vol && g[0].points.size() == 2 && g[0].points.front().tick == 3.0 * bar && g[0].points.back().tick == 3.25 * bar);
        CHECK (g[1].target == pan && g[1].points.size() == 2 && g[1].points.front().tick == 0.5 * bar);
    }
    // only what is newer than the last capture
    CHECK (captureauto::lastGestures (b.entries(), 3100.0, 2 * bar).empty());
    g = captureauto::lastGestures (b.entries(), 1150.0, 2 * bar);
    CHECK (g.size() == 2 && g[0].target == pan && g[0].points.size() == 1);
    // a jump back (a new play from the start) ends a gesture
    b.add (entry (vol, 4000.0, 0.25 * bar, 0.5f));
    g = captureauto::lastGestures (b.entries(), -1.0, 2 * bar);
    CHECK (g.size() == 2 && g[0].points.size() == 1 && g[0].points.front().tick == 0.25 * bar);

    // into a song: two automation clips (Pattern layout, lanes off) at the gestures' ticks
    Song s;
    s.patterns.resize (1);
    s.playlist.push_back (trackstates190op::clipOn (5, 0));
    s.playlist.back().length = 8 * bar; // (the song is eight bars long: nothing folds)
    for (const auto& gesture : captureauto::lastGestures (b.entries(), 1000.0, 2 * bar))
    {
        AutomationWrite w;
        w.target = gesture.target;
        w.name = "x";
        std::vector<RecordedValue> points;
        for (const auto& p : gesture.points)
            points.push_back ({ p.tick, p.value });
        commitAutomationPoints (s, w, points, 0);
    }
    CHECK (s.automations.size() == 2);
    int clips = 0;
    for (const auto& c : s.playlist)
        if (c.kind == ClipKind::automation)
        {
            ++clips;
            const auto& a = s.automations[(std::size_t) c.automation];
            CHECK (! a.points.empty());
            if (! a.points.empty())
                CHECK (c.start - c.offset + a.points.front().tick == (a.target == vol ? bar / 4 : bar / 2));
        }
    CHECK (clips == 2);
}
