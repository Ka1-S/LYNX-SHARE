// Round 190nz: issue #232 stage 2, copy in one LYNX SHARE window and paste in another (core/CrossPaste.h).
#include "core/CrossPaste.h"
#include "core/Groove.h"

namespace crosspaste190nz
{
// a song with: Kick (0), Clap (1), "Lead" plugin channel (2), "Vox" audio clip channel (3); pattern 0 with Kick and Lead
// notes, pattern 1 with Clap notes; an automation clip on Lead's volume, one on mixer insert 4's pan
inline lynx::Song source()
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.resize (2); // Kick, Clap
    ChannelDef lead;
    lead.name = "Lead";
    lead.instrument = InstrumentKind::plugin;
    lead.plugin.id = "VST3-Lead-1234";
    lead.plugin.name = "Lead";
    lead.plugin.state = "c3RhdGU=";
    lead.mixerTrack = 7;
    song.channels.push_back (lead);
    ChannelDef vox;
    vox.name = "Vox";
    vox.instrument = InstrumentKind::audioClip;
    vox.samplePath = "/music/vox take 3.wav";
    song.channels.push_back (vox);
    song.channelGroups = { "Drums", "Synths" };
    song.channels[0].group = 0;
    song.channels[2].group = 1;

    song.patterns.clear();
    Pattern a;
    a.name = "Beat";
    a.notes.push_back ({ 0, 24, 0, 60, 0.8f });
    a.notes.push_back ({ 96, 48, 2, 64, 0.6f });
    a.channelLoops.push_back ({ 2, ChannelLoop::nextBar });
    PatternEvents lane;
    lane.target = { AutomationTargetKind::channelPan, 2, 0, 0 };
    lane.events = { { 0, 0.25f }, { 96, 0.75f } };
    a.events.push_back (lane);
    song.patterns.push_back (a);
    Pattern b;
    b.name = "Claps";
    b.notes.push_back ({ 24, 24, 1, 60, 0.7f });
    song.patterns.push_back (b);

    Automation leadVolume;
    leadVolume.name = "Lead volume";
    leadVolume.target = { AutomationTargetKind::channelVolume, 2, 0, 0 };
    leadVolume.points = { { 0, 0.2f, 0.0f }, { 384, 0.9f, 0.0f } };
    song.automations.push_back (leadVolume);
    Automation pan;
    pan.name = "Insert 4 pan";
    pan.target = { AutomationTargetKind::mixerPan, 4, 0, 0 };
    pan.points = { { 0, 0.5f, 0.0f } };
    song.automations.push_back (pan);

    Groove swing;
    swing.name = "Swing 16";
    swing.timing = 0.6f;
    const auto grooveId = groove::add (song.grooves, swing);

    song.clipEnvelopes.push_back ({});
    song.clipEnvelopes.back().volume = { { 0, 1.0f, 0.0f }, { 192, 0.5f, 0.0f } };

    song.playlist.clear();
    Clip p;
    p.kind = ClipKind::pattern;
    p.pattern = 0;
    p.track = 1;
    p.start = 384;
    p.length = 384;
    p.groove = grooveId;
    song.playlist.push_back (p);
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 3;
    audio.track = 2;
    audio.start = 768;
    audio.length = 768;
    audio.envelope = 0;
    song.playlist.push_back (audio);
    Clip automation;
    automation.kind = ClipKind::automation;
    automation.automation = 0;
    automation.track = 3;
    automation.start = 0;
    automation.length = 384;
    song.playlist.push_back (automation);
    Clip insertPan = automation;
    insertPan.automation = 1;
    insertPan.track = 4;
    song.playlist.push_back (insertPan);
    return song;
}
} // namespace crosspaste190nz

// the fragment holds only what the clips need, with every index into the fragment
TEST (crosspaste190nz_clips_fragment_holds_what_the_clips_play)
{
    using namespace lynx;
    const auto song = crosspaste190nz::source();
    const auto f = crosspaste::clipsFragment (song, song.playlist);
    // channels: Kick and Lead (pattern 0's notes, Lead's loop point and pan lane, the volume automation) and Vox (audio)
    CHECK (f.channels.size() == 3u);
    CHECK (f.channels[0].name == "Kick" && f.channels[1].name == "Lead" && f.channels[2].name == "Vox");
    CHECK (f.channels[1].plugin.state == "c3RhdGU=");
    CHECK (f.patterns.size() == 1u && f.patterns[0].name == "Beat"); // (pattern 1 is not played by a copied clip)
    CHECK (f.patterns[0].notes.size() == 2u && f.patterns[0].notes[1].channel == 1);
    CHECK (f.patterns[0].channelLoops.size() == 1u && f.patterns[0].channelLoops[0].channel == 1);
    CHECK (f.patterns[0].events.size() == 1u && f.patterns[0].events[0].target.index == 1);
    CHECK (f.automations.size() == 2u && f.automations[0].target.index == 1);
    CHECK (f.automations[1].target.kind == AutomationTargetKind::mixerPan && f.automations[1].target.index == 4);
    CHECK (f.clipEnvelopes.size() == 1u && f.grooves.grooves.size() == 1u);
    CHECK (f.playlist.size() == 4u);
    CHECK (f.playlist[0].pattern == 0 && f.playlist[0].groove == f.grooves.grooves[0].id);
    CHECK (f.playlist[1].channel == 2 && f.playlist[1].envelope == 0);
    CHECK (f.playlist[2].automation == 0 && f.playlist[3].automation == 1);
    CHECK (f.channelGroups.size() == 2u && f.channels[1].group == 1);
    // only the second clip: its audio channel, nothing else
    const auto one = crosspaste::clipsFragment (song, { song.playlist[1] });
    CHECK (one.channels.size() == 1u && one.patterns.empty() && one.automations.empty() && one.playlist.size() == 1u);
    CHECK (one.playlist[0].channel == 0);
}

// into another project: its own channels where it has the same ones, the rest added; a second paste adds nothing new
TEST (crosspaste190nz_import_clips_matches_channels_and_reuses_what_it_added)
{
    using namespace lynx;
    const auto song = crosspaste190nz::source();
    const auto f = crosspaste::clipsFragment (song, song.playlist);

    auto other = Song::makeDefault(); // Kick, Clap, Hat, Snare, Synth (Kick: same name and kind)
    const auto channelsBefore = other.channels.size(), patternsBefore = other.patterns.size();
    other.vcas.clear();
    auto clips = crosspaste::importClips (other, f);
    CHECK (clips.size() == 4u);
    // Kick is the song's own; Lead and Vox were added at the end
    CHECK (other.channels.size() == channelsBefore + 2);
    const auto lead = (int) channelsBefore, vox = (int) channelsBefore + 1;
    CHECK (other.channels[(std::size_t) lead].name == "Lead" && other.channels[(std::size_t) vox].name == "Vox");
    CHECK (other.channels[(std::size_t) lead].plugin.state == "c3RhdGU=");
    CHECK (other.channels[(std::size_t) lead].mixerTrack == 7); // (the song has insert 7)
    CHECK (other.channels[(std::size_t) lead].uid == 0); // (uids::normalize gives it one)
    // the rack group came along by name
    CHECK (other.channels[(std::size_t) lead].group >= 0 && other.channelGroups[(std::size_t) other.channels[(std::size_t) lead].group] == "Synths");
    // the pattern: Kick's notes on the song's Kick (0), Lead's on the added Lead
    CHECK (other.patterns.size() == patternsBefore + 1);
    const auto& beat = other.patterns[(std::size_t) clips[0].pattern];
    CHECK (beat.name == "Beat" && beat.notes.size() == 2u);
    CHECK (beat.notes[0].channel == 0 && beat.notes[1].channel == lead);
    CHECK (beat.channelLoops.size() == 1u && beat.channelLoops[0].channel == lead);
    CHECK (beat.events.size() == 1u && beat.events[0].target.index == lead);
    CHECK (clips[1].channel == vox && clips[1].envelope >= 0 && other.clipEnvelopes[(std::size_t) clips[1].envelope].volume.size() == 2u);
    CHECK (other.automations[(std::size_t) clips[2].automation].target.index == lead);
    CHECK (other.automations[(std::size_t) clips[3].automation].target.kind == AutomationTargetKind::mixerPan
           && other.automations[(std::size_t) clips[3].automation].target.index == 4);
    CHECK (clips[0].groove != 0 && groove::find (other.grooves, clips[0].groove) != nullptr);
    CHECK (clips[0].start == 384 && clips[0].track == 1 && clips[0].uid == 0);

    // pasted again: the same channels, pattern, automation clips and groove
    const auto channels = other.channels.size(), patterns = other.patterns.size(), automations = other.automations.size();
    const auto grooves = other.grooves.grooves.size();
    const auto again = crosspaste::importClips (other, f);
    CHECK (again.size() == 4u);
    CHECK (other.channels.size() == channels && other.patterns.size() == patterns && other.automations.size() == automations);
    CHECK (other.grooves.grooves.size() == grooves);
    CHECK (again[0].pattern == clips[0].pattern && again[2].automation == clips[2].automation && again[1].channel == clips[1].channel);
}

// a copied track clip comes back as a pattern of its own every time (as every copy of one); a channel whose sample
// differs is not the same channel
TEST (crosspaste190nz_track_clips_and_channel_identity)
{
    using namespace lynx;
    auto song = crosspaste190nz::source();
    song.patterns[1].trackClip = true;
    Clip c;
    c.kind = ClipKind::pattern;
    c.pattern = 1;
    c.track = 5;
    c.length = 192;
    trackclips::Carried carried;
    carried.patterns.emplace_back (1, song.patterns[1]);
    const auto f = crosspaste::clipsFragment (song, { c }, carried);
    CHECK (f.patterns.size() == 1u && f.patterns[0].trackClip);
    auto other = Song::makeDefault();
    const auto before = other.patterns.size();
    crosspaste::importClips (other, f);
    crosspaste::importClips (other, f);
    CHECK (other.patterns.size() == before + 2); // (two pastes, two track clips)
    // Clap is the song's own Clap (same name, same kind): no channel added
    CHECK (other.channels.size() == 5u);

    ChannelDef a, b;
    a.name = b.name = "Vox";
    a.instrument = b.instrument = InstrumentKind::audioClip;
    a.samplePath = "/a.wav";
    b.samplePath = "/b.wav";
    CHECK (! crosspaste::sameChannel (a, b));
    b.samplePath = a.samplePath;
    b.volume = 0.1f; // (its settings may differ)
    CHECK (crosspaste::sameChannel (a, b));
    a.instrument = b.instrument = InstrumentKind::plugin;
    a.plugin.id = "x";
    b.plugin.id = "y";
    CHECK (! crosspaste::sameChannel (a, b));
}

// Channel Rack › Copy channels / Paste channels: copies at the place asked, the rest of the song follows
TEST (crosspaste190nz_channels_paste_as_copies_and_keep_the_song_consistent)
{
    using namespace lynx;
    auto song = crosspaste190nz::source();
    song.channels[2].instrument = InstrumentKind::layer;
    song.channels[2].layer.children = { 0, 3 };
    song.channels[2].simplePad.index = 4;
    song.channels[2].freeze.source = "frozen";
    const auto f = crosspaste::channelsFragment (song, { 3, 2, 2 });
    CHECK (f.channels.size() == 2u && f.channels[0].name == "Lead" && f.channels[1].name == "Vox");
    CHECK (f.channels[0].layer.children == std::vector<int> { 1 }); // (Kick did not come along)

    auto other = Song::makeDefault();
    other.mixer.resize (5); // (no insert 7 here)
    const auto kickNotes = other.patterns[0].notes.size();
    const auto added = crosspaste::importChannels (other, f, 1);
    CHECK (added == (std::vector<int> { 1, 2 }));
    CHECK (other.channels.size() == 7u && other.channels[0].name == "Kick" && other.channels[3].name == "Clap");
    CHECK (other.channels[1].name == "Lead" && other.channels[1].layer.children == std::vector<int> { 2 });
    CHECK (other.channels[1].mixerTrack == 0 && other.channels[1].simplePad.index == -1 && other.channels[1].freeze.source.empty());
    CHECK (other.channels[1].plugin.state == "c3RhdGU=");
    CHECK (other.channelGroups.size() == 1u && other.channelGroups[0] == "Synths" && other.channels[1].group == 0);
    // the pattern's notes moved with their channels (Clap was 1, is 3 now)
    CHECK (other.patterns[0].notes.size() == kickNotes);
    bool clapMoved = false;
    for (const auto& n : other.patterns[0].notes)
        clapMoved = clapMoved || n.channel == 3;
    CHECK (clapMoved);
    // pasting again in the same project: more copies (Paste channels always adds)
    const auto again = crosspaste::importChannels (other, f, 99);
    CHECK (again == (std::vector<int> { 7, 8 }));
    CHECK (other.channels[7].layer.children == std::vector<int> { 8 });
}
