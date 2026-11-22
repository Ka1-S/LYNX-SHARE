// Round 190oq: GitHub issues #243 (ARA on a mixer insert; the tempo map and time signatures for ARA plugins), #252 (the
// start page's logic). The app's parts are checked by --issuesshot190oq (src/IssuesShot190oq.inc) and --aratest.
#include "core/AraRegions.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/RackFilter.h"
#include "core/StartPage.h"

namespace issues190oq
{
// two audio clip channels routed to insert 2, one to insert 3, one with its own ARA link (also routed to 2), a synth on 2
lynx::Song araInsertSong()
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mixer = Song::defaultMixer();
    auto audio = [&s] (std::uint64_t uid, int track) -> ChannelDef& {
        ChannelDef c;
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "take" + std::to_string (uid) + ".wav";
        c.uid = uid;
        c.mixerTrack = track;
        s.channels.push_back (c);
        return s.channels.back();
    };
    audio (10, 2);
    audio (11, 2);
    audio (12, 3);
    audio (13, 2).ara = { "VST3-Other-1", "Other ARA" };
    ChannelDef synth;
    synth.instrument = InstrumentKind::sampler;
    synth.mixerTrack = 2;
    s.channels.push_back (synth);
    EffectSlot eq;
    eq.kind = EffectKind::parametricEq;
    s.mixer[2].effects.push_back (eq);
    EffectSlot mel;
    mel.kind = EffectKind::plugin;
    mel.plugin.id = "VST3-Melodyne-1234";
    mel.plugin.name = "Melodyne";
    mel.ara = true;
    s.mixer[2].effects.push_back (mel);
    for (int c = 0; c < 4; ++c)
    {
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = c;
        clip.start = c * 1920;
        clip.length = 1920;
        clip.uid = (std::uint64_t) (100 + c);
        s.playlist.push_back (clip);
    }
    return s;
}
} // namespace issues190oq

TEST (issues190oq_243_ara_insert_links_the_routed_audio_clip_channels)
{
    using namespace lynx;
    auto s = issues190oq::araInsertSong();
    CHECK (ara::araInsertSlot (s, 2) == 1 && ara::araInsertSlot (s, 3) == -1 && ara::araInsertSlot (s, 0) == -1);
    CHECK (ara::anyAraInsert (s));
    // the routed audio clip channels without a link of their own; not the channel on insert 3, not the synth
    CHECK (ara::insertLink (s, 0).pluginId == "VST3-Melodyne-1234" && ara::insertLink (s, 0).pluginName == "Melodyne");
    CHECK (ara::insertLink (s, 1).active() && ! ara::insertLink (s, 2).active() && ! ara::insertLink (s, 4).active());
    // the channel's own link (clip menu) wins over the insert's
    CHECK (! ara::insertLink (s, 3).active() && ara::effectiveLink (s, 3).pluginId == "VST3-Other-1");
    CHECK (ara::effectiveLink (s, 0).pluginId == "VST3-Melodyne-1234" && ! ara::effectiveLink (s, 2).active());
    CHECK ((ara::insertChannels (s, 2) == std::vector<int> { 0, 1 }));
    CHECK (ara::routedAudioClipChannels (s, 2) == 2 && ara::routedAudioClipChannels (s, 3) == 1 && ara::routedAudioClipChannels (s, 5) == 0);
    // the song the rack plays: the channels take the link, the slot is left empty (passes the sound), the EQ stays
    auto played = s;
    CHECK (ara::projectInserts (played) == 2);
    CHECK (played.channels[0].ara.pluginId == "VST3-Melodyne-1234" && played.channels[1].ara.active() && ! played.channels[2].ara.active());
    CHECK (played.channels[3].ara.pluginId == "VST3-Other-1");
    CHECK (played.mixer[2].effects[1].plugin.id.empty() && ! played.mixer[2].effects[1].ara);
    CHECK (played.mixer[2].effects[0].kind == EffectKind::parametricEq);
    CHECK (ara::isAraChannel (played, 0) && ara::isAraChannel (played, 1) && ! ara::isAraChannel (played, 2));
    CHECK (! ara::anyAraInsert (played));
    // the regions are the same as the channel's own link would give
    auto own = s;
    own.channels[0].ara = { "VST3-Melodyne-1234", "Melodyne" };
    CHECK (ara::regionsOf (played, 0) == ara::regionsOf (own, 0));
    // bypassed: the clips play unedited (A/B), as REAPER / Logic / Studio One bypass an ARA insert
    s.mixer[2].effects[1].enabled = false;
    CHECK (ara::araInsertSlot (s, 2) == -1 && ! ara::insertLink (s, 0).active() && ! ara::anyAraInsert (s));
    // the mode off: a plain insert effect, nothing projected
    s.mixer[2].effects[1].enabled = true;
    s.mixer[2].effects[1].ara = false;
    played = s;
    CHECK (ara::projectInserts (played) == 0 && played.mixer[2].effects[1].plugin.id == "VST3-Melodyne-1234");
    // a slot's ARA mode takes part in its equality (the rack rebuilds it) and is off by default (FL imports, old projects)
    EffectSlot a, b;
    CHECK (! a.ara && a == b);
    b.ara = true;
    CHECK (! (a == b));
}

TEST (issues190oq_243_time_map_follows_the_tempo)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    // a constant tempo: two points, the old entries exactly
    auto m = ara::timeMap (s);
    CHECK (m.constant() && m.ticks.size() == 2 && std::abs (m.secondsAt (960.0) - 0.5) < 1e-12 && std::abs (m.ticksAt (0.5) - 960.0) < 1e-9);
    const auto flat = ara::tempoEntries (s);
    CHECK (flat.size() == 2 && flat[1].quarters == 4.0 && std::abs (flat[1].seconds - 2.0) < 1e-12);
    // tempo automation: 60 BPM for a bar, then 120 (a step at bar 2)
    Automation tempo;
    tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    tempo.points = { { 0, valueFromTempo (60.0), 0.0f }, { ppq * 4 - 1, valueFromTempo (60.0), 0.0f }, { ppq * 4, valueFromTempo (120.0), 0.0f },
                     { ppq * 16, valueFromTempo (120.0), 0.0f } };
    s.automations.push_back (tempo);
    Clip at;
    at.kind = ClipKind::automation;
    at.automation = 0;
    at.track = 5;
    at.length = ppq * 16;
    s.playlist.push_back (at);
    m = ara::timeMap (s);
    CHECK (! m.constant());
    // bar 2 starts after 4 s (4 beats at 60), bar 3 two seconds later; the map agrees with Song::secondsForTicks
    CHECK (std::abs (m.secondsAt (4.0 * ppq) - s.secondsForTicks (4 * ppq)) < 1e-6);
    CHECK (std::abs (m.secondsAt (8.0 * ppq) - s.secondsForTicks (8 * ppq)) < 1e-6);
    CHECK (std::abs (m.secondsAt (12.0 * ppq) - s.secondsForTicks (12 * ppq)) < 1e-6);
    CHECK (std::abs (m.ticksAt (m.secondsAt (9.5 * ppq)) - 9.5 * ppq) < 1e-6);
    // the entries: seconds rising, quarters rising, the tempo between two entries constant
    const auto e = ara::tempoEntries (m);
    CHECK (e.size() >= 3 && e.front().seconds == 0.0 && e.front().quarters == 0.0);
    for (std::size_t i = 1; i < e.size(); ++i)
        CHECK (e[i].seconds > e[i - 1].seconds && e[i].quarters > e[i - 1].quarters);
    // a clip at bar 3: its region starts where the tempo map puts it (not at the project tempo's 4 s)
    ChannelDef audio;
    audio.instrument = InstrumentKind::audioClip;
    audio.ara = { "VST3-Melodyne-1234", "Melodyne" };
    s.channels.push_back (audio);
    Clip c;
    c.kind = ClipKind::audio;
    c.channel = 0;
    c.start = 8 * ppq;
    c.length = 4 * ppq;
    c.uid = 9;
    s.playlist.push_back (c);
    const auto r = ara::regionsOf (s, 0, 0.0, &m);
    CHECK (r.size() == 1 && std::abs (r[0].playStart - s.secondsForTicks (8 * ppq)) < 1e-6);
    CHECK (std::abs (r[0].playDuration - (s.secondsForTicks (12 * ppq) - s.secondsForTicks (8 * ppq))) < 1e-6);
    CHECK (std::abs (r[0].sourceDuration - r[0].playDuration) < 1e-9); // (unstretched: the sample plays at its own speed)
    // without the map (as before): the project tempo
    CHECK (std::abs (ara::regionsOf (s, 0)[0].playStart - 4.0) < 1e-9);
    // pattern mode: the project tempo (the tempo automation plays in song mode only)
    s.mode = PlayMode::pattern;
    CHECK (ara::timeMap (s).constant());
}

TEST (issues190oq_243_bar_signatures_follow_the_markers)
{
    using namespace lynx;
    Song s;
    s.beatsPerBar = 4;
    s.beatUnit = 4;
    auto b = ara::barSignatures (s);
    CHECK (b.size() == 1 && b[0].numerator == 4 && b[0].denominator == 4 && b[0].quarters == 0.0);
    Marker m;
    m.tick = 8 * ppq; // bar 3
    m.numerator = 3;
    m.denominator = 4;
    m.action = Marker::timeSignature;
    s.markers().push_back (m);
    Marker plain;
    plain.tick = 2 * ppq; // not a signature
    s.markers().push_back (plain);
    Marker six = m;
    six.tick = 14 * ppq; // two 3/4 bars later
    six.numerator = 6;
    six.denominator = 8;
    s.markers().push_back (six);
    b = ara::barSignatures (s);
    CHECK (b.size() == 3);
    CHECK (b[1].numerator == 3 && b[1].denominator == 4 && b[1].quarters == 8.0);
    CHECK (b[2].numerator == 6 && b[2].denominator == 8 && b[2].quarters == 14.0);
    // a marker at 0 replaces the project's meter there
    Marker first = m;
    first.tick = 0;
    first.numerator = 7;
    first.denominator = 8;
    s.markers().push_back (first);
    b = ara::barSignatures (s);
    CHECK (b.size() == 3 && b[0].numerator == 7 && b[0].denominator == 8);
}

TEST (issues190oq_252_start_page_logic)
{
    using namespace lynx::startpage;
    // on by default (FL's Welcome window shows at every start; owner decision, rollup 0.1.31 step 6); only on a normal
    // start without a file, never over the first run's card
    CHECK (enabled());
    CHECK (showAtStart (true, false, false, false));
    CHECK (! showAtStart (false, false, false, false) && ! showAtStart (true, true, false, false) && ! showAtStart (true, false, true, false)
           && ! showAtStart (true, false, false, true));
    std::vector<Recent> list {
        { "/p/Beats/Night Drive.lynx", "Night Drive", "/p/Beats", 1000, true, 0 },
        { "/p/Vocals/Demo Vocal.flp", "Demo Vocal", "/p/Vocals", 900, false, 0 },
        { "/p/Beats/Trap Idea 2.lynx", "Trap Idea 2", "/p/Beats", 800, true, 0 },
    };
    CHECK ((filter (list, "") == std::vector<int> { 0, 1, 2 }));
    CHECK ((filter (list, "  ") == std::vector<int> { 0, 1, 2 }));
    CHECK ((filter (list, "", true) == std::vector<int> { 0, 2 }));
    CHECK ((filter (list, "beats") == std::vector<int> { 0, 2 }));          // the folder counts
    CHECK ((filter (list, "DRIVE night") == std::vector<int> { 0 }));        // any order, any case
    CHECK ((filter (list, "beats -trap") == std::vector<int> { 0 }));        // the Browser's syntax
    CHECK ((filter (list, "vocal OR trap") == std::vector<int> { 1, 2 }));
    CHECK (filter (list, "nothing here").empty());
    // FL's Welcome window's "how long ago" (no date at any age)
    const std::int64_t minute = 60000, hour = 60 * minute, day = 24 * hour, now = 1000 * day + 5;
    CHECK (ago (now, now - 20000).kind == Ago::Kind::seconds);
    CHECK (ago (now, now - 7 * minute).kind == Ago::Kind::minutes && ago (now, now - 7 * minute).minutes == 7);
    const auto h = ago (now, now - 3 * hour - 25 * minute);
    CHECK (h.kind == Ago::Kind::hours && h.hours == 3 && h.minutes == 25);
    CHECK (ago (now, now - day - 1).kind == Ago::Kind::days && ago (now, now - day - 1).days == 1 && ago (now, now - 400 * day).days == 400);
    CHECK (ago (now, 0).kind == Ago::Kind::unknown && ago (now, now + day).kind == Ago::Kind::seconds);
    // FL's size text
    CHECK (sizeText (500) == "500 B" && sizeText (179 * 1024 + 300) == "179 KB" && sizeText (3 * 1024 * 1024 + 300000) == "3.3 MB"
           && sizeText (25LL * 1024 * 1024) == "25 MB");
    // the keys
    CHECK (moveRow (-1, 1, 3) == 0 && moveRow (-1, -1, 3) == 2 && moveRow (0, -1, 3) == 0 && moveRow (1, 1, 3) == 2 && moveRow (2, 8, 3) == 2);
    CHECK (moveRow (0, 1, 0) == -1);
}

TEST (issues190oq_246_rack_groups_as_fl_2026_names_them)
{
    using namespace lynx;
    // FL 2026 calls its built-in groups "Audio" / "Automation"; its loader renames an older project's "Audio clips" /
    // "Automation clips" (REA_rodada190oq §3)
    Song s;
    s.channels.resize (3);
    s.channelGroups = { "Audio", "Automation clips", "Leads" };
    s.channels[1].group = 0;
    s.channels[2].group = 2;
    CHECK (rackfilter::isBuiltinName ("Audio") && rackfilter::isBuiltinName ("Audio clips") && rackfilter::isBuiltinName ("Unsorted"));
    CHECK (! rackfilter::isBuiltinName ("Automation") && ! rackfilter::isBuiltinName ("Leads"));
    CHECK (rackfilter::shows (s, 1, rackfilter::audioClips) && ! rackfilter::shows (s, 1, rackfilter::unsorted));
    CHECK (rackfilter::shownName ("Audio clips") == "Audio" && rackfilter::shownName ("Automation clips") == "Automation"
           && rackfilter::shownName ("Leads") == "Leads");
}

// Issue #253 (round 190oq): FL's send-level automation ("To <insert>" / "To Master" knobs, control 0x2000 + destination)
// had no target on import, so its clips played nothing; plugin parameters and mixer volume / pan were mapped.
TEST (issues190oq_253_flp_send_automation_targets_the_route)
{
    using namespace lynx;
    const std::map<int, int> channels;
    const auto mixerId = [] (int track, int param) { return flp::ControlId { 0x70000000u + ((std::uint32_t) track << 22) + (std::uint32_t) param }; };
    auto t = automationTargetFor (mixerId (22, flp::ControlId::mixerSendBase + 2), channels, 30);
    CHECK (t.kind == AutomationTargetKind::mixerSend && t.index == 22 && t.slot == 2);
    t = automationTargetFor (mixerId (22, flp::ControlId::mixerSendBase), channels, 30); // "To Master"
    CHECK (t.kind == AutomationTargetKind::mixerSend && t.index == 22 && t.slot == 0);
    CHECK (automationTargetFor (mixerId (22, flp::ControlId::mixerSendBase + 40), channels, 30).kind == AutomationTargetKind::none); // (no such insert)
    // the rest as before
    CHECK (automationTargetFor (mixerId (3, flp::ControlId::mixerVolume), channels, 30).kind == AutomationTargetKind::mixerVolume);
    CHECK (automationTargetFor (mixerId (3, flp::ControlId::mixerPan), channels, 30).kind == AutomationTargetKind::mixerPan);
    CHECK (automationTargetFor (mixerId (3, flp::ControlId::pluginParamBase + 4), channels, 30).kind == AutomationTargetKind::effectParam);

    // a song with a send automated in two separate clips (as the reporter's), written as FL writes it and read back
    auto song = Song::makeDefault();
    song.setRoute (5, 2, true, 1.0f);
    for (int i = 0; i < 2; ++i)
    {
        Automation a;
        a.name = "PLUCK - To DELAY BUS";
        a.target = { AutomationTargetKind::mixerSend, 5, 2, 0 };
        a.points = { { 0, 0.2f + 0.5f * (float) i, 0.0f }, { ppq * 4, 0.6f, 0.0f } };
        song.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = (int) song.automations.size() - 1;
        c.track = 3;
        c.start = (std::int64_t) i * ppq * 4;
        c.length = ppq * 4;
        song.playlist.push_back (c);
    }
    flpexport::Options options;
    flpexport::Report report;
    const auto back = importFlp (flp::parseProject (flpexport::exportFlp (song, options, report)));
    int sends = 0;
    for (const auto& a : back.automations)
        sends += a.target.kind == AutomationTargetKind::mixerSend && a.target.index == 5 && a.target.slot == 2 ? 1 : 0;
    CHECK (sends >= 1);
    // and they play: the route's level follows the clip
    auto played = back;
    for (const auto& a : played.automations)
        if (a.target.kind == AutomationTargetKind::mixerSend)
        {
            CHECK (played.setTargetValue (a.target, 0.25f));
            CHECK (std::abs (played.routeLevel (5, 2) - faderGain (0.25f)) < 1.0e-5f);
            break;
        }
}
