// --- Round 190og (FLM coverage app-0003, QUEUE-0.1.31 #31 (e)): "Save a copy as FL Studio Mobile project (.flm)".
// The writer (core/FlmFormat.h write) is the reader's inverse; the export (core/FlmExport.h) read back by the .flm import
// keeps the notes, the patterns (one content per pattern and channel), the tempo, the mixer levels and the instruments
// and effects FLM has; the rest is listed. Every project here is built by the test. With LYNX_FLM_DIR set (FL Studio
// Mobile's demo songs / templates, e.g. FL's "Plugins/Fruity/Generators/FL Studio Mobile/Installed"), every shipped song
// also goes .flm → LYNX → .flm → LYNX and must keep its notes.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstring>

#include "core/Effects.h"
#include "core/FlmDwSampler.h"
#include "core/FlmExport.h"
#include "core/FlmFormat.h"
#include "core/FlmImport.h"
#include "core/GmsMobile.h"
#include "core/PocketSuperSaw.h"
#include "core/ProjectKey.h"

namespace flm190og
{
namespace flm = lynx::flm;
using namespace lynx;

inline FlmImportOptions importOptions()
{
    FlmImportOptions o;
    o.common.projectFolder = "/flm190og";
    o.common.fileExists = [] (const std::string&) { return true; };
    o.userResourceRoots = { "/flm190og-res" };
    return o;
}

// every note the Playlist plays: (start in 1/128 beat, channel name, key, velocity byte, length in 1/128 beat)
inline std::multiset<std::tuple<std::int64_t, std::string, int, int, std::int64_t>> playedNotes (const Song& s)
{
    std::multiset<std::tuple<std::int64_t, std::string, int, int, std::int64_t>> out;
    const auto q = [] (std::int64_t t) { return (std::int64_t) std::llround ((double) t * 128.0 / (double) ppq); };
    for (const auto& c : s.playlist)
    {
        if (c.kind != ClipKind::pattern || c.pattern < 0 || c.pattern >= (int) s.patterns.size())
            continue;
        for (const auto& n : s.patterns[(std::size_t) c.pattern].notes)
        {
            if (n.muted || n.start < c.offset || n.start >= c.offset + c.length)
                continue;
            out.insert ({ q (c.start + n.start - c.offset), s.channels[(std::size_t) n.channel].name, n.key,
                          (int) std::lround (255.0f * n.velocity), q (n.length) });
        }
    }
    return out;
}

inline int channelNamed (const Song& s, const std::string& name)
{
    for (int i = 0; i < (int) s.channels.size(); ++i)
        if (s.channels[(std::size_t) i].name == name)
            return i;
    return -1;
}

inline Note note (int channel, int step, int key, float velocity = 0.8f, int steps = 1)
{
    Note n;
    n.channel = channel;
    n.start = (std::int64_t) step * ticksPerStep;
    n.length = (std::int64_t) steps * ticksPerStep;
    n.key = key;
    n.velocity = velocity;
    return n;
}

// A song made in LYNX: Pocket SuperSaw on its own insert with Pocket effects, three drum samplers sharing the Master, a
// LYNX Synth (no FLM module), a bus two inserts send to, an audio clip, two patterns placed three times
inline Song lynxSong()
{
    Song s;
    s.bpm = 97.0;
    s.beatsPerBar = 3;
    s.mixer = Song::defaultMixer (6);
    s.info.title = "Round trip";
    s.info.author = "LYNX";
    s.loopStart = 2 * ppq;
    s.loopEnd = 8 * ppq;
    s.keyboardChords = { KeyboardChord { 9, 1, 4, { 10 } }, KeyboardChord { 5, 0, 4, {} } };
    projectkey::set (s, projectkey::Key { 9, projectkey::commonFirst()[1] });

    ChannelDef saw;
    saw.name = "Saw";
    saw.instrument = InstrumentKind::pocketSuperSaw;
    saw.modelParams = pocketSuperSawParams (std::vector<float> (60, 0.5f));
    saw.volume = 0.6f;
    saw.pan = -0.25f;
    saw.mixerTrack = 1;
    s.channels.push_back (saw);
    for (const auto* name : { "Kick", "Snare", "Hat" })
    {
        ChannelDef pad;
        pad.name = name;
        pad.instrument = InstrumentKind::sampler;
        pad.samplePath = std::string ("/samples/") + name + ".wav";
        pad.mixerTrack = 0;
        s.channels.push_back (pad);
    }
    s.channels[2].volume = 0.5f;
    s.channels[3].pan = 0.5f;
    s.channels[3].sampler.pitch = 3.0f;
    ChannelDef synth;
    synth.name = "Lead";
    synth.instrument = InstrumentKind::synth;
    synth.mixerTrack = 2;
    s.channels.push_back (synth);
    ChannelDef pad2;
    pad2.name = "Pad";
    pad2.instrument = InstrumentKind::pocketSuperSaw;
    pad2.modelParams = pocketSuperSawParams (std::vector<float> (60, 0.25f));
    pad2.mixerTrack = 2; // (shares insert 2 with Lead: a bus)
    s.channels.push_back (pad2);
    ChannelDef audio;
    audio.name = "Vox";
    audio.instrument = InstrumentKind::audioClip;
    audio.samplePath = "/samples/vox.wav";
    audio.volume = audioClipUnityVolume;
    audio.mixerTrack = 3;
    s.channels.push_back (audio);

    s.mixer[1].volume = 0.7f;
    s.mixer[1].pan = 0.2f;
    auto eq = makeEffectSlot (EffectKind::pocketEq);
    eq.params[0] = 0.7f; // (Gain 1)
    s.mixer[1].effects.push_back (eq);
    auto verb = makeEffectSlot (EffectKind::pocketReverb2);
    verb.mix = 0.4f;
    s.mixer[1].effects.push_back (verb);
    s.mixer[1].sends.push_back ({ 4, 0.5f });
    s.mixer[2].volume = 0.9f;
    s.mixer[3].volume = 0.75f;
    s.mixer[3].muted = true;
    s.mixer[4].name = "Space";
    s.mixer[4].effects.push_back (makeEffectSlot (EffectKind::pocketChorus));
    s.mixer[0].volume = 0.78f;

    Pattern a;
    a.name = "Beat";
    a.lengthTicks = 3 * ppq;
    a.notes = { note (1, 0, 60), note (2, 4, 60, 0.6f), note (3, 2, 60, 0.9f), note (3, 6, 62), note (0, 0, 57, 0.7f, 4), note (0, 8, 60, 0.5f, 2) };
    a.sortNotes();
    Pattern b;
    b.name = "Melody";
    b.lengthTicks = 3 * ppq;
    b.notes = { note (4, 0, 72), note (4, 4, 74, 1.0f, 2), note (5, 0, 48, 0.4f, 12) };
    b.sortNotes();
    s.patterns = { a, b };
    for (int k = 0; k < 3; ++k)
    {
        Clip c;
        c.kind = ClipKind::pattern;
        c.pattern = k == 1 ? 1 : 0;
        c.track = k == 1 ? 1 : 0;
        c.start = (std::int64_t) k * 3 * ppq;
        c.length = 3 * ppq;
        s.playlist.push_back (c);
    }
    Clip v;
    v.kind = ClipKind::audio;
    v.channel = 6;
    v.track = 2;
    v.start = 3 * ppq;
    v.length = 6 * ppq;
    v.gain = 0.5f;
    v.pitch = 2.0f;
    s.playlist.push_back (v);
    s.playlistTracks.resize (4);
    s.playlistTracks[2].name = "Vocals";
    // automation: the Saw's insert level (a ramp) and the Equalizer's Gain 1 (a curved segment)
    Automation level;
    level.target = { AutomationTargetKind::mixerVolume, 1, 0, 0 };
    level.points = { { 0, 0.4f }, { 2 * ppq, 0.8f } };
    Automation gain;
    gain.target = { AutomationTargetKind::effectParam, 1, 0, 0 };
    gain.points = { { 0, 0.2f, 0.6f }, { 2 * ppq, 0.9f } };
    s.automations = { level, gain };
    for (int k = 0; k < 2; ++k)
    {
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = k;
        c.track = 3;
        c.start = 3 * ppq;
        c.length = 2 * ppq;
        s.playlist.push_back (c);
    }
    return s;
}
} // namespace flm190og

TEST (flm190og_writer_is_the_readers_inverse)
{
    namespace flm = lynx::flm;
    flm::Project p;
    p.title = "Writer";
    p.tempo = 123.5;
    p.numerator = 7;
    p.denominator = 4;
    p.root = 9;
    p.scale = 10;
    p.author = "me";
    p.notes = "notes";
    p.created = "2026-11-21 10-00-00";
    p.editSeconds = 42.5;
    p.chordSetJson = "{\"chords\": []}";
    p.loopStart = 4.0;
    p.loopEnd = 8.0;
    flm::Rack master;
    master.kind = flm::kindMaster;
    master.params = { 0.7f, 0.5f, 0.0f, 0.0f, 0.5f, 0.0f };
    flm::Rack rack;
    rack.params = { 0.6f, 0.25f, 1.0f, 0.0f, 0.5f, 0.0f };
    flm::SampleRef pad;
    pad.name = "Kick";
    pad.root = "@L@";
    pad.path = "S/kick.wav";
    pad.relName = "S/kick.wav";
    pad.cutGroup = 2;
    pad.polyphonic = false;
    pad.reverse = true;
    pad.params = { 0.5f, 0.3f, 0.6f };
    rack.kit.push_back (pad);
    flm::Module m;
    m.type = 23;
    m.instanceId = 3;
    m.keyLow = 10;
    m.keyHigh = 90;
    m.params = { 1.0f, 0.25f, 0.75f };
    m.reserved = { 0.4f, 0.9f, 0.6f };
    m.custom = { 1, 2, 3 };
    rack.modules.push_back (m);
    rack.ccLinks.push_back ({ 7, 3, 2 });
    p.racks = { master, rack };
    flm::Channel mc;
    mc.kind = flm::kindMaster;
    mc.name = "MASTER";
    flm::Channel ch;
    ch.name = "Saw";
    ch.typeName = "Instrument 1";
    ch.userName = "My saw";
    ch.channelId = 1;
    ch.order = 1.0;
    ch.hue = 0.25f;
    ch.sends.push_back ({ 0, 0x3F800000u, 0x3F800000u, 0x3F800000u, 0x3F5A6666u, 0x3F5A6666u });
    flm::Lane lane;
    lane.rawKind = 0x103;
    lane.swing = 0.25;
    lane.name = "Steps";
    flm::Clip clip;
    clip.startTick = 512;
    clip.length = 8.0;
    clip.loopLength = 4.0;
    clip.offset = 1.0;
    clip.contentId = 5;
    clip.sourceLength = 4.0;
    flm::Event e;
    e.tick = 64;
    e.length = 0.25;
    e.key = 0;
    e.velocity = 180;
    e.pan = 40;
    e.value = 1000;
    e.repeat = 2;
    clip.events = { e };
    lane.clips = { clip };
    flm::Clip audio = clip;
    audio.events.clear();
    audio.sample = pad;
    lane.clips.push_back (audio);
    ch.lanes = { lane };
    p.channels = { mc, ch };

    const auto bytes = flm::write (p);
    CHECK (bytes.size() > 4 && std::memcmp (bytes.data(), "10LF", 4) == 0);
    CHECK (flm::write (p) == bytes); // deterministic
    const auto q = flm::parse (bytes);
    CHECK (q.has_value());
    if (! q)
        return;
    CHECK (q->title == "Writer" && q->tempo == 123.5 && q->numerator == 7 && q->denominator == 4);
    CHECK (q->root == 9 && q->scale == 10 && q->author == "me" && q->notes == "notes" && q->created == p.created && q->editSeconds == 42.5);
    CHECK (q->chordSetJson == p.chordSetJson && q->appVersion == "5.0.4" && q->loopStart == 4.0 && q->loopEnd == 8.0);
    CHECK (q->racks.size() == 2 && q->channels.size() == 2);
    const auto& r = q->racks[1];
    CHECK (r.params == rack.params);
    CHECK (r.kit.size() == 1 && r.kit[0].name == "Kick" && r.kit[0].root == "@L@" && r.kit[0].path == "S/kick.wav");
    CHECK (r.kit[0].cutGroup == 2 && ! r.kit[0].polyphonic && r.kit[0].reverse && r.kit[0].params.size() == 11 && r.kit[0].params[1] == 0.3f);
    CHECK (r.modules.size() == 1 && r.modules[0].type == 23 && r.modules[0].instanceId == 3 && r.modules[0].keyLow == 10 && r.modules[0].keyHigh == 90);
    CHECK (r.modules[0].params == m.params && r.modules[0].reserved == m.reserved && r.modules[0].custom == m.custom);
    CHECK (r.ccLinks.size() == 1 && r.ccLinks[0].cc == 7 && r.ccLinks[0].module == 3 && r.ccLinks[0].param == 2);
    const auto& c = q->channels[1];
    CHECK (c.name == "Saw" && c.typeName == "Instrument 1" && c.userName == "My saw" && c.channelId == 1 && c.order == 1.0 && c.hue == 0.25f);
    CHECK (c.sends.size() == 1 && c.sends[0] == ch.sends[0]);
    CHECK (c.lanes.size() == 1 && c.lanes[0].rawKind == 0x103 && c.lanes[0].holdsNotes() && c.lanes[0].swing == 0.25 && c.lanes[0].name == "Steps");
    CHECK (c.lanes[0].clips.size() == 2);
    const auto& k = c.lanes[0].clips[0];
    CHECK (k.startTick == 512 && k.length == 8.0 && k.loopLength == 4.0 && k.offset == 1.0 && k.contentId == 5 && k.sourceLength == 4.0);
    CHECK (k.events.size() == 1 && k.events[0].tick == 64 && k.events[0].length == 0.25 && k.events[0].key == 0 && k.events[0].velocity == 180);
    CHECK (k.events[0].pan == 40 && k.events[0].value == 1000 && k.events[0].repeat == 2 && k.events[0].slide == 0);
    CHECK (c.lanes[0].clips[1].sample.has_value() && c.lanes[0].clips[1].sample->path == "S/kick.wav");
    CHECK (q->channels[0].kind == flm::kindMaster && q->racks[0].kind == flm::kindMaster && q->racks[0].params[0] == 0.7f);
    // a hue round trip through FLM's colour law
    CHECK (std::abs (flm::hueOfColour (flm::channelColour (0.4f)) - 0.4f) < 0.002f);
}

TEST (flm190og_export_laws_invert_the_import)
{
    using namespace lynx;
    for (const auto db : { -40.0, -12.0, -5.2, 0.0, 3.0, 5.6 })
    {
        const auto g = std::pow (10.0, db / 20.0);
        const auto v = flmFaderValue (g);
        const auto back = std::pow (10.0, 3.3 * std::pow ((double) v, 0.6) - 3.0);
        CHECK (std::abs (20.0 * std::log10 (back) - db) < 0.01);
    }
    CHECK (std::abs (flmFaderValue (1.0) - 0.853f) < 0.001f);
    for (int st = -24; st <= 24; ++st)
        CHECK (std::clamp ((int) (flmSpeedValue (st) * 48.0f - 24.0f), -24, 24) == st);
    for (const auto db : { -20.0, -6.0, 0.0, 12.0 })
        CHECK (std::abs ((40.0 * flmPostGainValue (std::pow (10.0, db / 20.0)) - 20.0) - db) < 1e-4);
}

TEST (flm190og_export_round_trip_keeps_notes_tempo_levels_instruments_effects)
{
    using namespace lynx;
    const auto song = flm190og::lynxSong();
    FlmExportOptions o;
    o.title = "Round trip";
    o.sampleSeconds = [] (const std::string&) { return 4.0; };
    const auto out = exportFlm (song, o);
    CHECK (out.ok);
    const auto back = importFlm (out.bytes, flm190og::importOptions());
    CHECK (back.has_value());
    if (! back)
        return;
    const auto& b = *back;
    // the song
    CHECK (b.bpm == 97.0 && b.beatsPerBar == 3 && b.info.title == "Round trip" && b.info.author == "LYNX");
    const auto key = projectkey::get (b);
    CHECK (key.has_value() && key->root == 9 && projectkey::intervals (*key) == projectkey::intervals (*projectkey::get (song)));
    CHECK (b.keyboardChords == song.keyboardChords);
    // the loop range: FLM's play loop, which FLM starts switched off (LYNX: the range kept, looping off)
    CHECK (b.loopStart == song.loopStart && b.loopEnd == song.loopEnd && b.loopOff);
    // the notes, every one at its place, on its channel, with its key, velocity and length
    CHECK (flm190og::playedNotes (b) == flm190og::playedNotes (song));
    // the patterns: the LYNX chunk regroups FLM's one-per-track contents into "Beat" and "Melody", placed three times
    CHECK (b.patterns.size() == 2 && b.patterns[0].name == "Beat" && b.patterns[1].name == "Melody");
    CHECK (std::count_if (b.playlist.begin(), b.playlist.end(), [] (const Clip& c) { return c.kind == ClipKind::pattern; }) == 3);
    {
        // FLM saving the song again drops the chunk; a changed file makes the chunk count for nothing (one pattern per track)
        auto edited = out.bytes;
        edited[20] ^= 1; // (a byte of the title)
        const auto plain = importFlm (edited, flm190og::importOptions());
        CHECK (plain.has_value() && plain->patterns.size() > 2 && flm190og::playedNotes (*plain) == flm190og::playedNotes (song));
    }
    // the instruments: SuperSaw stays SuperSaw with its values; the LYNX Synth plays FLM's MiniSynth (listed)
    const auto saw = flm190og::channelNamed (b, "Saw"), pad = flm190og::channelNamed (b, "Pad"), kick = flm190og::channelNamed (b, "Kick");
    CHECK (saw >= 0 && pad >= 0 && kick >= 0);
    if (saw < 0 || pad < 0 || kick < 0)
        return;
    CHECK (b.channels[(std::size_t) saw].instrument == InstrumentKind::pocketSuperSaw && b.channels[(std::size_t) saw].modelParams == song.channels[0].modelParams);
    CHECK (std::abs (b.channels[(std::size_t) saw].volume - song.channels[0].volume) < 1e-4f && std::abs (b.channels[(std::size_t) saw].pan + 0.25f) < 1e-4f);
    CHECK (b.channels[(std::size_t) pad].modelParams == song.channels[5].modelParams);
    // drum pads: sampler channels at their level, pan and pitch
    const auto snare = flm190og::channelNamed (b, "Snare"), hat = flm190og::channelNamed (b, "Hat");
    CHECK (b.channels[(std::size_t) kick].instrument == InstrumentKind::sampler && b.channels[(std::size_t) kick].samplePath.find ("Round trip Samples/Kick.wav") != std::string::npos);
    CHECK (std::abs (b.channels[(std::size_t) snare].volume - 0.5f) < 1e-3f);
    CHECK (std::abs (b.channels[(std::size_t) hat].pan - 0.5f) < 0.005f && b.channels[(std::size_t) hat].sampler.pitch == 3.0f);
    // the mixer: the Saw's insert keeps its level, pan and effects (Equalizer, Reverb 2 at 40 %), its send to the bus
    const auto& sawInsert = b.mixer[(std::size_t) b.channels[(std::size_t) saw].mixerTrack];
    CHECK (std::abs (sawInsert.volume - 0.7f) < 1e-3f && std::abs (sawInsert.pan - 0.2f) < 0.005f);
    CHECK (sawInsert.effects.size() == 2 && sawInsert.effects[0].kind == EffectKind::pocketEq && std::abs (sawInsert.effects[0].params[0] - 0.7f) < 1e-6f);
    CHECK (sawInsert.effects.size() == 2 && sawInsert.effects[1].kind == EffectKind::pocketReverb2 && std::abs (sawInsert.effects[1].mix - 0.4f) < 1e-6f);
    bool sendToSpace = false;
    for (const auto& snd : sawInsert.sends)
        if (snd.target > 0 && b.mixer[(std::size_t) snd.target].name == "Space" && std::abs (snd.level - 0.5f) < 2e-3f)
            sendToSpace = true;
    CHECK (sendToSpace);
    // the bus (insert 2, shared by Lead and Pad) keeps its level; both send into it
    const auto lead = flm190og::channelNamed (b, "Lead");
    CHECK (lead >= 0);
    if (lead >= 0)
    {
        const auto& li = b.mixer[(std::size_t) b.channels[(std::size_t) lead].mixerTrack];
        CHECK (li.sends.size() == 1 && ! li.toMaster);
        if (li.sends.size() == 1)
            CHECK (std::abs (b.mixer[(std::size_t) li.sends[0].target].volume - 0.9f) < 1e-3f);
    }
    CHECK (std::abs (b.mixer[0].volume - 0.78f) < 1e-3f);
    // the audio clip: its gain and pitch, on the Vocals track's insert (muted, 0.75)
    int audioClips = 0;
    for (const auto& c : b.playlist)
        if (c.kind == ClipKind::audio)
        {
            ++audioClips;
            CHECK (c.start == 3 * ppq && c.length == 6 * ppq && std::abs (c.gain - 0.5f) < 2e-3f && c.pitch == 2.0f);
            const auto& ins = b.mixer[(std::size_t) b.channels[(std::size_t) c.channel].mixerTrack];
            CHECK (ins.muted && std::abs (ins.volume - 0.75f) < 1e-3f);
        }
    CHECK (audioClips == 1);
    // an audio track's empty slot makes no channel (FLM's own audio tracks keep one)
    CHECK (flm190og::channelNamed (b, "<empty>") < 0);
    // the automation: the Level lane on the insert's volume, the Equalizer's Gain 1 on its slot, values kept
    {
        const auto insert = b.channels[(std::size_t) saw].mixerTrack;
        int levels = 0, gains = 0;
        for (const auto& c : b.playlist)
        {
            if (c.kind != ClipKind::automation)
                continue;
            const auto& a = b.automations[(std::size_t) c.automation];
            CHECK (c.start == 3 * ppq);
            if (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == insert)
            {
                ++levels;
                CHECK (a.points.size() == 2 && std::abs (a.points[0].value - 0.4f) < 2e-3f && std::abs (a.points[1].value - 0.8f) < 2e-3f);
            }
            if (a.target.kind == AutomationTargetKind::effectParam && a.target.index == insert && a.target.param == 0)
            {
                ++gains;
                CHECK (a.points.size() > 10); // (the curved segment, sampled)
                CHECK (std::abs (a.points.front().value - 0.2f) < 2e-3f && std::abs (a.points.back().value - 0.9f) < 2e-3f);
            }
        }
        CHECK (levels == 1 && gains == 1);
    }
    // the samples the copy needs, once each
    CHECK (out.samples.size() == 4);
    // what was lost: the LYNX Synth's sound
    bool synthListed = false;
    for (const auto& l : out.lost)
        if (l.sound && l.text.find ("MiniSynth") != std::string::npos && l.detail.find ("Lead") != std::string::npos)
            synthListed = true;
    CHECK (synthListed);
    // and an export of the import is the same file but for the names FLM gives (the content is stable)
    const auto again = exportFlm (*back, o);
    const auto p1 = flm::parse (out.bytes), p2 = flm::parse (again.bytes);
    CHECK (p1 && p2 && p1->channels.size() == p2->channels.size());
}

TEST (flm190og_flm_engines_go_back_to_their_modules)
{
    // FLM → LYNX → FLM: the modules of FLM's own engines come back with their values
    using namespace lynx;
    namespace flm = lynx::flm;
    flm::Project p;
    p.title = "Engines";
    p.tempo = 120.0;
    flm::Rack master;
    master.kind = flm::kindMaster;
    flm::Channel mc;
    mc.kind = flm::kindMaster;
    mc.name = "MASTER";
    p.racks.push_back (master);
    p.channels.push_back (mc);
    const auto add = [&p] (std::uint32_t type, std::vector<float> params, std::vector<std::uint8_t> custom, const char* name) {
        flm::Rack r;
        flm::Module m;
        m.type = type;
        m.instanceId = 1;
        m.params = std::move (params);
        m.custom = std::move (custom);
        r.modules.push_back (m);
        p.racks.push_back (r);
        flm::Channel ch;
        ch.name = name;
        ch.channelId = (int) p.channels.size();
        ch.order = ch.channelId;
        flm::Lane lane;
        flm::Clip clip;
        clip.length = 4.0;
        clip.loopLength = 4.0;
        clip.contentId = 1;
        flm::Event e;
        e.key = 60;
        e.length = 1.0;
        clip.events = { e };
        lane.clips = { clip };
        ch.lanes = { lane };
        p.channels.push_back (ch);
    };
    std::vector<float> saw (60), mini (44), tb (14), gm (75);
    for (std::size_t k = 0; k < saw.size(); ++k)
        saw[k] = (float) ((k * 37) % 100) / 100.0f;
    saw[0] = 1.0f;
    for (std::size_t k = 0; k < mini.size(); ++k)
        mini[k] = (float) ((k * 13) % 100) / 100.0f;
    mini[0] = 1.0f;
    mini[4] = 0.0f; // MW Dest: the filter
    for (std::size_t k = 0; k < tb.size(); ++k)
        tb[k] = (float) ((k * 29) % 100) / 100.0f;
    tb[0] = 1.0f;
    const auto g = gmsmobile::defaultPatch();
    gm.assign (g.P.begin(), g.P.end());
    std::vector<std::uint8_t> gcustom (20);
    const std::int32_t wa = 3, wb = 7;
    const float bend = 2.0f / 3.0f, amount = 0.25f, dest = 1.0f;
    std::memcpy (gcustom.data(), &wa, 4);
    std::memcpy (gcustom.data() + 4, &wb, 4);
    std::memcpy (gcustom.data() + 8, &bend, 4);
    std::memcpy (gcustom.data() + 12, &amount, 4);
    std::memcpy (gcustom.data() + 16, &dest, 4);
    // a DW Sampler with two zones of FLM's factory content (CSTM "10WD": ZONE records, then MIDI)
    std::vector<float> dw (34);
    for (std::size_t k = 0; k < dw.size(); ++k)
        dw[k] = (float) ((k * 17) % 100) / 100.0f;
    dw[0] = 1.0f;
    std::vector<std::uint8_t> dcustom { '1', '0', 'W', 'D' };
    for (int z = 0; z < 2; ++z)
    {
        const char* tag = "ZONE";
        dcustom.insert (dcustom.end(), tag, tag + 4);
        const std::uint32_t size = 1065;
        dcustom.insert (dcustom.end(), (const std::uint8_t*) &size, (const std::uint8_t*) &size + 4);
        std::string where = std::string ("@R@") + '\0' + (z == 0 ? "DirectWave Samples\\Bass\\low.wav" : "DirectWave Samples\\Bass\\high.wav");
        where.resize (1024, '\0');
        dcustom.insert (dcustom.end(), where.begin(), where.end());
        for (const int b : { z == 0 ? 36 : 60, z == 0 ? 0 : 49, z == 0 ? 48 : 127, 0, 127 })
            dcustom.push_back ((std::uint8_t) b);
        for (const float f : { 1.0f, 0.0f })
            dcustom.insert (dcustom.end(), (const std::uint8_t*) &f, (const std::uint8_t*) &f + 4);
        for (const std::uint32_t u : { 0u, 0u, 0u })
            dcustom.insert (dcustom.end(), (const std::uint8_t*) &u, (const std::uint8_t*) &u + 4);
        for (const float f : { 0.0f, 1.0f, 1.0f, 0.1f })
            dcustom.insert (dcustom.end(), (const std::uint8_t*) &f, (const std::uint8_t*) &f + 4);
    }
    for (const std::uint8_t b : std::initializer_list<std::uint8_t> { 'M', 'I', 'D', 'I', 2, 0, 0, 0, 7, 1 })
        dcustom.push_back (b);
    add (1, dw, dcustom, "DW");
    // a 3x Osc (149 values; the import turns them into the desktop engine's raw state and FL's channel settings)
    std::vector<float> osc (149);
    for (std::size_t k = 0; k < osc.size(); ++k)
        osc[k] = (float) ((k * 41 + 7) % 97) / 96.0f;
    osc[0] = 1.0f;
    osc[106] = 1.0f;
    osc[115] = osc[116] = 0.5f;
    osc[146] = osc[147] = osc[148] = 0.0f;
    for (std::size_t k = 1; k <= 3; ++k)
        osc[k] = 0.0f; // (sine, not the custom shape)
    osc[139] = 0.0f; // (no arpeggiator)
    add (39, osc, {}, "Osc");
    add (23, saw, {}, "SuperSaw");
    add (0, mini, {}, "MiniSynth");
    add (19, tb, {}, "Bass");
    add (2, gm, gcustom, "GM");
    const auto lynxSong = importFlmProject (p, flm190og::importOptions());
    CHECK (lynxSong.has_value());
    if (! lynxSong)
        return;
    FlmExportOptions eo;
    eo.resourceRoots = { "/flm190og-res" }; // (the import's resource folder: factory files stay "@R@")
    const auto out = exportFlm (*lynxSong, eo);
    const auto q = flm::parse (out.bytes);
    CHECK (q.has_value());
    if (! q)
        return;
    const auto moduleOf = [&q] (const char* name) -> const flm::Module* {
        for (std::size_t i = 0; i < q->channels.size(); ++i)
            if (q->channels[i].name == name && i < q->racks.size() && ! q->racks[i].modules.empty())
                return &q->racks[i].modules.front();
        return nullptr;
    };
    const auto close = [] (const std::vector<float>& a, const std::vector<float>& b, std::size_t n) {
        if (a.size() < n || b.size() < n)
            return false;
        for (std::size_t k = 0; k < n; ++k)
            if (std::abs (a[k] - b[k]) > 1e-6f)
                return false;
        return true;
    };
    const auto* s = moduleOf ("SuperSaw");
    CHECK (s != nullptr && s->type == 23 && close (s->params, saw, 60));
    const auto* m = moduleOf ("MiniSynth");
    CHECK (m != nullptr && m->type == 0 && close (m->params, mini, 44));
    const auto* t = moduleOf ("Bass");
    CHECK (t != nullptr && t->type == 19 && close (t->params, tb, 14));
    const auto* gms = moduleOf ("GM");
    CHECK (gms != nullptr && gms->type == 2 && close (gms->params, gm, 75) && gms->custom == gcustom);
    const auto* d = moduleOf ("DW");
    CHECK (d != nullptr && d->type == 1 && close (d->params, dw, 34));
    if (d != nullptr)
    {
        const auto zones = flmdw::parseCustom (d->custom);
        CHECK (zones && zones->zones.size() == 2 && zones->pbRange == 7);
        if (zones && zones->zones.size() == 2)
        {
            CHECK (zones->zones[0].root == "@R@" && zones->zones[0].path == "DirectWave Samples/Bass/low.wav");
            CHECK (zones->zones[1].rootKey == 60 && zones->zones[1].keyLow == 49 && zones->zones[1].keyHigh == 127);
        }
    }
    CHECK (out.samples.empty()); // (factory content is referenced, not copied)
    // the 3x Osc: its module again, and imported again it is the same channel (the same raw state and settings)
    const auto* o3 = moduleOf ("Osc");
    CHECK (o3 != nullptr && o3->type == 39 && o3->params.size() == 149);
    {
        const auto again = importFlm (out.bytes, flm190og::importOptions());
        const auto a = flm190og::channelNamed (*lynxSong, "Osc"), b2 = again ? flm190og::channelNamed (*again, "Osc") : -1;
        CHECK (a >= 0 && b2 >= 0);
        if (a >= 0 && b2 >= 0)
        {
            const auto& x = lynxSong->channels[(std::size_t) a];
            const auto& y = again->channels[(std::size_t) b2];
            CHECK (x.instrument == InstrumentKind::threeosc && y.instrument == InstrumentKind::threeosc);
            CHECK (x.pluginState == y.pluginState);
            CHECK (x.misc.echo == y.misc.echo && x.misc.key == y.misc.key && x.misc.velocity == y.misc.velocity && x.maxPolyphony == y.maxPolyphony);
            CHECK (std::abs (x.misc.portaBeats - y.misc.portaBeats) < 1e-6f && std::abs (x.misc.gateBeats - y.misc.gateBeats) < 1e-6f);
            CHECK (x.sampler.pitch == y.sampler.pitch && std::abs (x.pan - y.pan) < 0.01f);
            CHECK (x.modelParams.size() >= 3 && y.modelParams.size() >= 3 && std::abs (x.modelParams[2] - y.modelParams[2]) < 1e-3f);
        }
    }
    // nothing of these six was listed as lost
    for (const auto& l : out.lost)
        CHECK (l.text.find ("MiniSynth (") == std::string::npos);
}

TEST (flm190og_shipped_songs_survive_export_and_import)
{
    // FL Studio Mobile's own songs (LYNX_FLM_DIR): .flm → LYNX → .flm → LYNX keeps every note, the tempo and the levels
    using namespace lynx;
    const char* dir = std::getenv ("LYNX_FLM_DIR");
    if (dir == nullptr || ! std::filesystem::is_directory (dir))
        return;
    int songs = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (dir))
    {
        const auto ext = entry.path().extension().string();
        if (ext != ".flm" && ext != ".songtmpl")
            continue;
        std::ifstream in (entry.path(), std::ios::binary);
        const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto first = importFlm (bytes, flm190og::importOptions());
        if (! first)
            continue;
        ++songs;
        const auto out = exportFlm (*first, {});
        const auto second = importFlm (out.bytes, flm190og::importOptions());
        CHECK (second.has_value());
        if (! second)
            continue;
        const auto a = flm190og::playedNotes (*first), b = flm190og::playedNotes (*second);
        if (a != b)
            std::printf ("  notes differ in %s: %zu vs %zu\n", entry.path().filename().string().c_str(), a.size(), b.size());
        CHECK (a == b);
        CHECK (second->bpm == first->bpm && second->beatsPerBar == first->beatsPerBar);
        CHECK (std::abs (second->mixer[0].volume - first->mixer[0].volume) < 1e-3f);
    }
    std::printf ("  FL Studio Mobile songs exported and read back: %d\n", songs);
}

TEST (flm190og_switched_off_send_stays_off)
{
    // SEND [1] is the route's Enable (FLM's channel writer; REA_rodada190og §2): off = no sound through that route
    using namespace lynx;
    namespace flm = lynx::flm;
    flm::Project p;
    p.title = "Sends";
    flm::Rack master, a, bus;
    master.kind = flm::kindMaster;
    bus.kind = flm::kindBus;
    p.racks = { master, a, bus };
    flm::Channel mc, ca, cb;
    mc.kind = flm::kindMaster;
    mc.name = "MASTER";
    mc.channelId = 0;
    ca.name = "A";
    ca.channelId = 1;
    ca.order = 1;
    cb.kind = flm::kindBus;
    cb.name = "Bus";
    cb.channelId = 2;
    cb.order = 2;
    const auto f = [] (float x) { std::uint32_t u; std::memcpy (&u, &x, 4); return u; };
    ca.sends.push_back ({ 0, f (1.0f), f (1.0f), f (1.0f), f (0.853125f), f (0.853125f) });
    ca.sends.push_back ({ 2, f (0.0f), f (1.0f), f (1.0f), f (0.853125f), f (0.853125f) }); // switched off
    p.channels = { mc, ca, cb };
    const auto song = importFlm (flm::write (p), flm190og::importOptions());
    CHECK (song.has_value());
    if (! song)
        return;
    const auto& m = song->mixer[1];
    CHECK (m.name == "A" && m.toMaster && m.sends.empty());
}

