// --- Export to FL Studio (round 171e, idea by Tookolld): the .flp writer (flp/FlpWrite.h) and the Song → .flp conversion
// (core/FlpExport.h), checked by reading the file back with LYNX's own .flp reader and importer

#include "core/FlpExport.h"
#include "core/FlWrapper.h"
#include "flp/FlpWrite.h"

namespace fx171e
{
bool near (double a, double b, double tolerance) { return std::abs (a - b) <= tolerance; }

lynx::FlpImportOptions importOptions()
{
    lynx::FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    return o;
}

// a song with something of everything the export maps
lynx::Song richSong()
{
    using namespace lynx;
    Song s = Song::makeDefault();
    s.bpm = 128.0;
    s.beatsPerBar = 3;
    s.swing = 0.25f;
    s.info.title = "Export test";
    s.info.author = "LYNX";

    ChannelDef sampler;
    sampler.name = "Loop";
    sampler.instrument = InstrumentKind::sampler;
    sampler.samplePath = "C:/samples/loop.wav";
    sampler.volume = 0.6f;
    sampler.pan = -0.5f;
    sampler.mixerTrack = 2;
    sampler.sampler.pitch = 3.0f;
    sampler.sampler.regionStart = 0.25f;
    sampler.sampler.regionEnd = 0.75f;
    sampler.sampler.reverse = true;
    sampler.sampler.stretchBeats = 8.0f;
    sampler.sampler.keepPitch = true;
    sampler.colour = 0xFF336699u;
    s.channels.push_back (sampler);

    ChannelDef osc;
    osc.name = "Lead";
    osc.instrument = InstrumentKind::threeosc; // LYNX's own default state = a new FL 3x Osc
    osc.mixerTrack = 3;
    osc.misc.echo.feedback = 0.5f;
    osc.misc.echo.echoes = 3;
    osc.misc.echo.timeBeats = 0.5f;
    osc.arp.direction = 5; // LYNX's "sticky" = FL's 4
    osc.arp.range = 2;
    osc.misc.mono = true;
    osc.misc.porta = true;
    s.channels.push_back (osc);

    ChannelDef vst;
    vst.name = "Pad";
    vst.instrument = InstrumentKind::plugin;
    vst.plugin.name = "Some Synth";
    vst.plugin.foreign.format = ForeignPlugin::Format::vst3;
    vst.plugin.foreign.name = "Some Synth";
    vst.plugin.foreign.path = "C:/Program Files/Common Files/VST3/Some Synth.vst3";
    vst.plugin.foreign.hasClassId = true;
    for (int i = 0; i < 16; ++i)
        vst.plugin.foreign.classId[(std::size_t) i] = (std::uint8_t) (0x10 + i);
    vst.plugin.foreign.state = flpexport::flVst3State ({ 1, 2, 3, 4, 5 }, { 9, 8 });
    vst.mixerTrack = 4;
    s.channels.push_back (vst);

    ChannelDef clipChannel;
    clipChannel.name = "Vocal";
    clipChannel.instrument = InstrumentKind::audioClip;
    clipChannel.samplePath = "C:/samples/vocal.wav";
    clipChannel.mixerTrack = 2;
    s.channels.push_back (clipChannel);

    auto& p = s.patterns[0];
    p.name = "Beat";
    p.colour = 0xFF884422u;
    Note n;
    n.start = ppq * 2;
    n.length = ppq / 2 + 373;
    n.channel = 6;
    n.key = 67;
    n.velocity = 0.5f;
    n.pan = 0.25f;
    n.finePitch = -30.0f;
    n.slide = true;
    n.colour = 5;
    n.group = 2;
    p.notes.push_back (n);
    Note muted = n;
    muted.start = ppq * 3;
    muted.muted = true;
    muted.slide = false;
    muted.porta = true;
    p.notes.push_back (muted);
    p.sortNotes();

    s.mixer[2].name = "Drums";
    s.mixer[2].volume = 0.7f;
    s.mixer[2].pan = 0.3f;
    s.mixer[2].stereo = -0.5f;
    s.mixer[2].eq.gainDb = { 3.0f, -2.0f, 1.5f };
    s.mixer[2].eq.hz = { 120.0f, 2000.0f, 9000.0f };
    s.mixer[2].colour = 0xFF00AA00u;
    s.setRoute (2, 5, true, 0.5f);
    s.mixer[2].masterLevel = 0.8f;

    // an FL effect imported earlier (its origin kept): written back byte for byte
    const auto limiterState = flpexport::fruityLimiterDefaultState();
    EffectSlot fl;
    fl.kind = EffectKind::fruityLimiter;
    fl.flOrigin = { "Fruity Limiter", limiterState, 0 };
    fl.mix = 0.5f;
    s.mixer[2].effects.push_back (fl);
    // a LYNX-only effect: left out (option B) or bounced (option A)
    EffectSlot own;
    own.kind = EffectKind::eq;
    s.mixer[3].effects.push_back (own);
    flpexport::stampOrigins (s);

    Clip pc;
    pc.pattern = 0;
    pc.track = 1;
    pc.start = ppq * 4;
    pc.length = ppq * 4;
    s.playlist.push_back (pc);
    Clip ac;
    ac.kind = ClipKind::audio;
    ac.channel = 8;
    ac.track = 2;
    ac.start = ppq * 8;
    ac.length = ppq * 2;
    ac.gain = 0.5f;
    s.playlist.push_back (ac);

    Automation a;
    a.name = "Drums volume";
    a.target = { AutomationTargetKind::mixerVolume, 2, 0, 0 };
    a.points = { { 0, 0.2f, 0.0f }, { ppq * 4, 0.9f, 0.5f }, { ppq * 8, 0.4f, 0.0f } };
    s.automations.push_back (a);
    Automation tempo;
    tempo.name = "Tempo";
    tempo.target = { AutomationTargetKind::tempo, 0, 0, 0 };
    tempo.points = { { 0, valueFromTempo (120.0), 0.0f }, { ppq * 8, valueFromTempo (150.0), 0.0f } };
    s.automations.push_back (tempo);
    Clip au;
    au.kind = ClipKind::automation;
    au.automation = 0;
    au.track = 3;
    au.start = 0;
    au.length = ppq * 8;
    s.playlist.push_back (au);
    Clip at = au;
    at.automation = 1;
    at.track = 4;
    s.playlist.push_back (at);

    s.playlistTracks.resize (3);
    s.playlistTracks[1].name = "Beats";
    s.playlistTracks[1].colour = 0xFFAA2200u;
    s.playlistTracks[2].muted = true;
    s.markers().push_back ({ ppq * 4, "Drop" });
    s.mode = PlayMode::song;
    return s;
}
} // namespace fx171e

TEST (flpexport171e_writer_reads_back)
{
    flp::Project p;
    p.tempo = 97.5;
    p.title = "Title ç";
    flp::Channel c;
    c.index = 0;
    c.type = flp::ChannelType::sampler;
    c.name = "Kick";
    c.samplePath = "C:/k.wav";
    c.hasLevels = true;
    c.volume = 9000;
    c.pan = 3200;
    p.channels.push_back (c);
    flp::Pattern pat;
    pat.index = 1;
    pat.name = "P";
    flp::Note n;
    n.position = 96;
    n.length = 0;
    n.key = 64;
    n.velocity = 90;
    pat.notes.push_back (n);
    p.patterns.push_back (pat);
    flp::Arrangement a;
    a.name = "Main";
    flp::PlaylistItem item;
    item.position = 384;
    item.length = 384;
    item.index = 1;
    item.track = 2;
    a.items.push_back (item);
    p.arrangements.push_back (a);
    flp::MixerInsert m;
    m.index = 1;
    m.name = "Bus";
    m.routes = { 0, 3 };
    m.hasRoutes = true;
    p.mixer.push_back (flp::MixerInsert {});
    p.mixer.push_back (m);

    const auto bytes = flp::writeProject (p);
    const auto back = flp::parseProject (bytes);
    CHECK (back.version == "26.1.7.5654" && back.tempo == 97.5 && back.title == "Title ç");
    CHECK (back.channels.size() == 1 && back.channels[0].name == "Kick" && back.channels[0].samplePath == "C:/k.wav");
    CHECK (back.channels[0].volume == 9000 && back.channels[0].pan == 3200);
    CHECK (back.patterns.size() == 1 && back.patterns[0].notes.size() == 1 && back.patterns[0].notes[0].key == 64);
    CHECK (back.patterns[0].notes[0].position == 96 && back.patterns[0].notes[0].velocity == 90);
    CHECK (back.arrangements.size() == 1 && back.arrangements[0].items.size() == 1 && back.arrangements[0].items[0].track == 2);
    CHECK (back.arrangements[0].items[0].position == 384 && back.arrangements[0].name == "Main");
    CHECK (back.mixerTrackCount == 127 && back.mixer.size() == 127);
    CHECK (back.mixer.size() > 1 && back.mixer[1].name == "Bus" && back.mixer[1].routes == std::vector<int> ({ 0, 3 }));
    // undescribed inserts route to the Master, as FL's defaults
    CHECK (back.mixer.size() > 7 && back.mixer[7].routes == std::vector<int> ({ 0 }));
    CHECK (back.warnings.empty());
}

TEST (flpexport171e_value_helpers_invert_fl)
{
    using namespace lynx::flpexport;
    for (const double t : { 0.001, 0.05, 0.3, 1.0, 4.0 })
        CHECK (fx171e::near (flp::Channel::envelopeTime (envelopeTimeValue (t)), t, t * 0.001 + 1e-5));
    CHECK (envelopeTimeValue (0.0) <= 100);
    for (const float b : { 0.125f, 0.25f, 0.5f })
    {
        const auto v = slideTimeValue (b);
        const auto n = std::round ((std::pow (27.0, v / 1024.0) - 1.0) * 48.0 / 26.0) / 192.0;
        CHECK (fx171e::near (n, b, 1.0 / 192.0));
    }
}

TEST (flpexport171e_wrapper_round_trips)
{
    using namespace lynx;
    ForeignPlugin p;
    p.format = ForeignPlugin::Format::vst3;
    p.name = "Synth";
    p.path = "C:/x/Synth.vst3";
    p.hasClassId = true;
    p.classId[0] = 0xAB;
    p.state = flpexport::flVst3State ({ 1, 2, 3 }, { 4 });
    PluginRef ref;
    ref.fixedBuffers = true;
    ref.threaded = false;
    ref.midiInPort = 3;
    ForeignPlugin back;
    CHECK (parseFlWrapper (flpexport::writeWrapper (p, true, &ref), back));
    CHECK (back.format == ForeignPlugin::Format::vst3 && back.name == "Synth" && back.path == p.path && back.classId == p.classId);
    CHECK (back.state == p.state && back.midiInPort == 3 && back.wrapperVersion == 12);
    std::vector<std::uint8_t> component, controller;
    CHECK (splitFlVst3State (back.state, component, controller) && component == std::vector<std::uint8_t> ({ 1, 2, 3 }) && controller.size() == 1);
    PluginRef options;
    applyFlWrapperOptions (back, options);
    CHECK (options.fixedBuffers && ! options.threaded && options.notifyRender && options.allowSmartDisable);

    ForeignPlugin v2;
    v2.format = ForeignPlugin::Format::vst2;
    v2.vst2Id = 0x41424344;
    v2.name = "Old";
    ForeignPlugin v2back;
    CHECK (parseFlWrapper (flpexport::writeWrapper (v2, false), v2back) && v2back.format == ForeignPlugin::Format::vst2 && v2back.vst2Id == 0x41424344);
    ForeignPlugin clap;
    clap.format = ForeignPlugin::Format::clap;
    clap.clapId = "com.example.x";
    ForeignPlugin clapBack;
    CHECK (parseFlWrapper (flpexport::writeWrapper (clap, true), clapBack) && clapBack.clapId == "com.example.x");
}

TEST (flpexport171e_song_round_trips_through_the_importer)
{
    using namespace lynx;
    const auto song = fx171e::richSong();
    flpexport::Options options;
    options.lynxOnly = flpexport::LynxOnly::substitute;
    flpexport::Report report;
    const auto bytes = flpexport::exportFlp (song, options, report);
    const auto project = flp::parseProject (bytes);
    CHECK (project.warnings.empty());
    CHECK (project.header.ppq == 960); // a note 373 ticks longer than half a beat needs LYNX's PPQ
    const auto back = importFlp (project, fx171e::importOptions());

    CHECK (back.bpm == 128.0 && back.beatsPerBar == 3 && fx171e::near (back.swing, 0.25, 0.01));
    CHECK (back.info.title == "Export test" && back.info.author == "LYNX");
    CHECK (back.channels.size() == song.channels.size());
    if (back.channels.size() == song.channels.size())
    {
        for (std::size_t i = 0; i < song.channels.size(); ++i)
        {
            CHECK (back.channels[i].name == song.channels[i].name);
            CHECK (fx171e::near (back.channels[i].volume, song.channels[i].volume, 0.001));
            CHECK (fx171e::near (back.channels[i].pan, song.channels[i].pan, 0.001));
            CHECK (back.channels[i].mixerTrack == song.channels[i].mixerTrack);
        }
        const auto& smp = back.channels[5];
        CHECK (smp.instrument == InstrumentKind::sampler && smp.samplePath == "C:/samples/loop.wav");
        CHECK (fx171e::near (smp.sampler.pitch, 3.0, 0.01) && smp.sampler.reverse && smp.colour == 0xFF336699u);
        CHECK (fx171e::near (smp.sampler.regionStart, 0.25, 1e-6) && fx171e::near (smp.sampler.regionEnd, 0.75, 1e-6));
        CHECK (fx171e::near (smp.sampler.stretchBeats, 8.0, 1e-4) && smp.sampler.keepPitch);
        const auto& osc = back.channels[6];
        CHECK (osc.misc.echo.echoes == 3 && fx171e::near (osc.misc.echo.feedback, 0.5, 0.001) && fx171e::near (osc.misc.echo.timeBeats, 0.5, 0.01));
        CHECK (osc.arp.direction == 5 && osc.arp.range == 2 && osc.misc.mono && osc.misc.porta);
        const auto& pad = back.channels[7];
        CHECK (pad.plugin.foreign.format == ForeignPlugin::Format::vst3 && pad.plugin.foreign.classId == song.channels[7].plugin.foreign.classId);
        CHECK (pad.plugin.foreign.state == song.channels[7].plugin.foreign.state);
    }

    CHECK (! back.patterns.empty() && back.patterns[0].name == "Beat" && back.patterns[0].colour == 0xFF884422u);
    if (! back.patterns.empty())
    {
        const auto& a = song.patterns[0].notes;
        const auto& b = back.patterns[0].notes;
        CHECK (a.size() == b.size());
        for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        {
            CHECK (a[i].start == b[i].start && a[i].length == b[i].length && a[i].channel == b[i].channel && a[i].key == b[i].key);
            CHECK (fx171e::near (a[i].velocity, b[i].velocity, 0.005) && fx171e::near (a[i].pan, b[i].pan, 0.01));
            CHECK (a[i].finePitch == b[i].finePitch && a[i].muted == b[i].muted && a[i].slide == b[i].slide && a[i].porta == b[i].porta);
            CHECK (a[i].colour == b[i].colour && a[i].group == b[i].group);
        }
    }

    CHECK (back.mixer.size() >= 6);
    if (back.mixer.size() >= 6)
    {
        const auto& d = back.mixer[2];
        CHECK (d.name == "Drums" && d.colour == 0xFF00AA00u);
        CHECK (fx171e::near (d.volume, 0.7, 0.001) && fx171e::near (d.pan, 0.3, 0.001) && fx171e::near (d.stereo, -0.5, 0.02));
        CHECK (fx171e::near (d.eq.gainDb[0], 3.0, 0.01) && fx171e::near (d.eq.gainDb[1], -2.0, 0.01));
        CHECK (fx171e::near (d.eq.hz[1], 2000.0, 2.0) && fx171e::near (d.eq.hz[2], 9000.0, 5.0));
        CHECK (back.routes (2, 5) && fx171e::near (back.routeLevel (2, 5), 0.5, 0.01) && fx171e::near (d.masterLevel, 0.8, 0.01));
        CHECK (! d.effects.empty() && d.effects[0].kind == EffectKind::fruityLimiter && fx171e::near (d.effects[0].mix, 0.5, 0.001));
        if (! d.effects.empty())
            CHECK (d.effects[0].flOrigin.plugin == "Fruity Limiter" && d.effects[0].flOrigin.state == song.mixer[2].effects[0].flOrigin.state);
        CHECK (back.mixer[3].effects.empty()); // the LYNX-only EQ: left out with option B
    }
    CHECK (std::any_of (report.dropped.begin(), report.dropped.end(), [] (const std::string& l) { return l.find ("slot 1") != std::string::npos; }));
    // the Master's LYNX Limiter: FL's Fruity Limiter in its place
    CHECK (! back.mixer.empty() && ! back.mixer[0].effects.empty() && back.mixer[0].effects[0].flOrigin.plugin == "Fruity Limiter");
    CHECK (! report.substituted.empty());

    // the Playlist
    std::vector<Clip> patternClips, audioClips, autoClips;
    for (const auto& c : back.playlist)
        (c.kind == ClipKind::pattern ? patternClips : c.kind == ClipKind::audio ? audioClips : autoClips).push_back (c);
    CHECK (patternClips.size() == 1 && patternClips[0].start == ppq * 4 && patternClips[0].length == ppq * 4 && patternClips[0].track == 1);
    CHECK (audioClips.size() == 1 && audioClips[0].start == ppq * 8 && audioClips[0].length == ppq * 2 && fx171e::near (audioClips[0].gain, 0.5, 1e-6));
    CHECK (autoClips.size() == 2);
    CHECK (back.playlistTracks.size() >= 3 && back.playlistTracks[1].name == "Beats" && back.playlistTracks[2].muted);
    CHECK (! back.markers().empty() && back.markers()[0].name == "Drop" && back.markers()[0].tick == ppq * 4);

    // the automation clips, their targets and values
    CHECK (back.automations.size() == 2);
    if (back.automations.size() == 2)
    {
        const auto& a = back.automations[0];
        CHECK (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == 2 && a.points.size() == 3);
        if (a.points.size() == 3)
            CHECK (fx171e::near (a.points[1].value, 0.9, 1e-5) && a.points[1].tick == ppq * 4 && fx171e::near (a.points[1].tension, 0.5, 1e-5));
        const auto& t = back.automations[1];
        CHECK (t.target.kind == AutomationTargetKind::tempo && t.points.size() == 2);
        if (t.points.size() == 2)
            CHECK (fx171e::near (tempoFromValue (t.points[1].value), 150.0, 0.01));
    }
    CHECK (back.mode == PlayMode::song);
}

TEST (flpexport171e_reexport_keeps_fl_states)
{
    // an imported FL project goes back with the very states FL wrote (generator and effect origins)
    using namespace lynx;
    auto song = fx171e::richSong();
    flpexport::Options options;
    flpexport::Report r1, r2;
    const auto first = flp::parseProject (flpexport::exportFlp (song, options, r1));
    auto imported = importFlp (first, fx171e::importOptions());
    const auto second = flp::parseProject (flpexport::exportFlp (imported, options, r2));
    CHECK (first.channels.size() == second.channels.size());
    for (std::size_t i = 0; i < std::min (first.channels.size(), second.channels.size()); ++i)
    {
        if (first.channels[i].pluginName != second.channels[i].pluginName || first.channels[i].pluginState != second.channels[i].pluginState)
            std::printf ("  channel %d: '%s' %d B vs '%s' %d B\n", (int) i, first.channels[i].pluginName.c_str(), (int) first.channels[i].pluginState.size(),
                         second.channels[i].pluginName.c_str(), (int) second.channels[i].pluginState.size());
        CHECK (first.channels[i].pluginName == second.channels[i].pluginName);
        CHECK (first.channels[i].pluginState == second.channels[i].pluginState);
    }
    auto slot = [] (const flp::Project& p, int t) -> const flp::MixerInsert::Slot* {
        for (const auto& m : p.mixer)
            if (m.index == t && ! m.slots.empty())
                return &m.slots[0];
        return nullptr;
    };
    const auto* a = slot (first, 2);
    const auto* b = slot (second, 2);
    CHECK (a != nullptr && b != nullptr && a->plugin == b->plugin && a->state == b->state);
    // the import's fingerprint matches what the import made; a change in LYNX breaks it
    CHECK (! imported.mixer[2].effects.empty() && imported.mixer[2].effects[0].flOrigin.fingerprint == flpexport::fingerprint (imported.mixer[2].effects[0]));
    if (! imported.mixer[2].effects.empty())
    {
        imported.mixer[2].effects[0].params[0] += 0.25f;
        CHECK (! flpexport::effectExportable (imported.mixer[2].effects[0], options));
    }
}

TEST (flpexport171e_render_plan_and_freeze)
{
    using namespace lynx;
    auto song = fx171e::richSong();
    flpexport::Options options; // option A
    const auto plan = flpexport::planRenders (song, options);
    // the default song's LYNX drums and synth (channels 0…4) are frozen; the sampler, 3x Osc and the VST3 are not
    CHECK (plan.channels == std::vector<int> ({ 0, 1, 2, 3, 4 }));
    // insert 3 holds the LYNX-only EQ; insert 2's effect is FL's own
    CHECK (std::find (plan.inserts.begin(), plan.inserts.end(), 3) != plan.inserts.end());
    CHECK (std::find (plan.inserts.begin(), plan.inserts.end(), 2) == plan.inserts.end());

    const auto freeze = flpexport::makeFreezeSong (song, 0);
    CHECK (! freeze.channels[0].muted && freeze.channels[1].muted && freeze.channels[0].mixerTrack == 0 && freeze.mixer[0].effects.empty());

    const auto notesBefore = std::count_if (song.patterns[0].notes.begin(), song.patterns[0].notes.end(), [] (const Note& n) { return n.channel == 0; });
    CHECK (notesBefore > 0);
    flpexport::applyFreeze (song, 0, "C:/out/kick.wav", ppq * 16);
    CHECK (song.channels[0].instrument == InstrumentKind::audioClip && song.channels[0].samplePath == "C:/out/kick.wav");
    CHECK (std::none_of (song.patterns[0].notes.begin(), song.patterns[0].notes.end(), [] (const Note& n) { return n.channel == 0; }));
    CHECK (std::any_of (song.playlist.begin(), song.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::audio && c.channel == 0 && c.length == ppq * 16; }));
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.channels[0].type == flp::ChannelType::audioClip && project.channels[0].samplePath == "C:/out/kick.wav");
}

TEST (flpexport171e_insert_render_switches_off_the_lynx_slots)
{
    using namespace lynx;
    auto song = fx171e::richSong();
    flpexport::Options options;
    Automation a;
    a.name = "EQ band";
    a.target = { AutomationTargetKind::effectParam, 3, 0, 0 };
    song.automations.push_back (a);
    const auto mixerBefore = song.mixer.size();
    flpexport::Report report;
    flpexport::applyChannelRender (song, 1, "C:/out/snare.wav", ppq * 8, report);
    const auto added = flpexport::applyInsertRender (song, 3, "C:/out/insert3.wav", ppq * 16, options, report);
    CHECK (added == (int) mixerBefore && song.mixer.size() == mixerBefore + 1);
    CHECK (song.mixer[3].muted && ! song.mixer[3].effects.empty() && ! song.mixer[3].effects[0].enabled);
    CHECK (song.automations.back().target.kind == AutomationTargetKind::none);
    CHECK (report.rendered.size() == 2);
    CHECK (song.channels.back().instrument == InstrumentKind::audioClip && song.channels.back().mixerTrack == added);
    // after the render nothing on insert 3 is reported as left out
    flpexport::Report after;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, after));
    CHECK (std::none_of (after.dropped.begin(), after.dropped.end(), [] (const std::string& d) { return d.find ("Insert 3") != std::string::npos; }));
    CHECK (project.channels.size() >= song.channels.size()); // (+ FL's automation clip channels)
    CHECK (flpexport::planRenders (song, options).inserts.empty() || std::find (flpexport::planRenders (song, options).inserts.begin(),
                                                                                    flpexport::planRenders (song, options).inserts.end(), 3)
                                                                         == flpexport::planRenders (song, options).inserts.end());
}

TEST (flpexport171e_sample_paths_rewritten_for_the_package)
{
    using namespace lynx;
    auto song = fx171e::richSong();
    flpexport::Options options;
    options.samplePath = [] (const std::string& p) { return "Samples/" + p.substr (p.find_last_of ("/\\") + 1); };
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.channels.size() > 5 && project.channels[5].samplePath == "Samples/loop.wav");
}

TEST (flpexport171e_fl_round_171e_fixes)
{
    // FL 2026 renders of the first exports (REA_rodada171e §5): a 3x Osc with no state, a plugin not on this PC, the PPQ
    using namespace lynx;
    auto song = fx171e::richSong();
    flpexport::Options options;
    flpexport::Report report;
    // 1. LYNX's 3x Osc with no state goes as FL's own version-14 state (an empty one made FL's notes never end)
    CHECK (song.channels[6].pluginState.empty());
    auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.channels[6].pluginName == "3x Osc" && project.channels[6].pluginState == flpexport::defaultThreeOscState());
    CHECK (threeOscStateUsable (flpexport::defaultThreeOscState()) && flpexport::defaultThreeOscState().size() == 460);
    // the other engines LYNX plays need their own state (else they are rendered)
    ChannelDef sim;
    sim.instrument = InstrumentKind::simsynth;
    CHECK (flpexport::needsFreeze (sim));
    // 2. a stand-in whose FL plugin is not on this PC keeps its wrapper
    auto standIn = song.channels[7];
    standIn.instrument = InstrumentKind::synth;
    song.channels[7] = standIn;
    CHECK (! flpexport::needsFreeze (standIn));
    project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.channels[7].pluginName == "Fruity Wrapper");
    // 3. the PPQ: notes and clips decide, the rest is rounded; a forced PPQ is kept
    CHECK (project.header.ppq == 960); // (the 373-tick note)
    auto& notes = song.patterns[0].notes;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [] (const Note& n) { return n.length % 10 != 0 || n.start % 10 != 0; }), notes.end());
    song.patterns[0].lengthTicks = ppq * 4 + 3; // (not on 96's grid: rounded)
    project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (project.header.ppq == 96);
    options.ppq = 960;
    CHECK (flp::parseProject (flpexport::exportFlp (song, options, report)).header.ppq == 960);
}
