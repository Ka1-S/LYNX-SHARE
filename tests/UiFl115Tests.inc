// Round 115 (docs/analise/REA_rodada115.md): the Browser's "Current project" contents, the toolbar's edit mode.

#include "core/ProjectContents.h"

TEST (flui115_project_samples_once_with_colour_and_uses)
{
    lynx::Song song;
    lynx::ChannelDef kick;
    kick.name = "Kick";
    kick.instrument = lynx::InstrumentKind::sampler;
    kick.samplePath = "C:\\Samples\\Kick 01.wav";
    kick.colour = 0xFFAA2233u;
    song.channels.push_back (kick);

    lynx::ChannelDef clip;
    clip.name = "Vox";
    clip.instrument = lynx::InstrumentKind::audioClip;
    clip.samplePath = "/home/u/vox.take2.wav";
    song.channels.push_back (clip);

    lynx::ChannelDef again = kick; // the same file on another channel
    again.colour = 0xFF00FF00u;
    song.channels.push_back (again);

    lynx::ChannelDef engineFile; // a path that is an engine file, not audio
    engineFile.instrument = lynx::InstrumentKind::slayer;
    engineFile.samplePath = "C:\\FL\\Slayer_Engine.dll";
    song.channels.push_back (engineFile);

    lynx::ChannelDef zones;
    zones.instrument = lynx::InstrumentKind::sampler;
    zones.zones.push_back ({ "/z/a.wav" });
    zones.zones.push_back ({ "/z/b.wav" });
    zones.samplePath = "/ignored-when-zones.wav";
    song.channels.push_back (zones);

    song.mixer.resize (2);
    song.mixer[1].colour = 0xFF0000FFu;
    lynx::EffectSlot newtone;
    newtone.samplePath = "/fx/source.wav";
    song.mixer[1].effects.push_back (newtone);

    const auto list = lynx::projectcontents::samples (song);
    CHECK (list.size() == 5);
    CHECK (list[0].name == "Kick 01" && list[0].uses == 2 && list[0].colour == 0xFFAA2233u && list[0].index == 0);
    CHECK (list[0].detail == "2x");
    CHECK (list[1].name == "vox.take2" && list[1].uses == 1 && list[1].detail.empty() && list[1].index == 1);
    CHECK (list[2].path == "/z/a.wav" && list[3].path == "/z/b.wav");
    CHECK (list[4].path == "/fx/source.wav" && list[4].colour == 0xFF0000FFu && list[4].index == -1);
}

TEST (flui115_project_generators_effects_patterns_automation)
{
    lynx::Song song;
    song.mixer.resize (3);
    song.mixer[2].name = "VOCALS";
    song.mixer[2].colour = 0xFF112233u;
    song.mixer[2].effects.push_back (lynx::makeEffectSlot (lynx::EffectKind::reverb));
    lynx::EffectSlot plugin;
    plugin.kind = lynx::EffectKind::plugin;
    plugin.plugin.name = "Some EQ";
    song.mixer[0].effects.push_back (plugin);

    lynx::ChannelDef a;
    a.name = "Lead";
    a.mixerTrack = 2;
    a.colour = 0xFF445566u;
    song.channels.push_back (a);
    lynx::ChannelDef b;
    b.mixerTrack = 1; // unnamed insert
    song.channels.push_back (b);

    const auto gens = lynx::projectcontents::generators (song);
    CHECK (gens.size() == 2);
    CHECK (gens[0].name == "Lead" && gens[0].detail == "VOCALS" && gens[0].colour == 0xFF445566u);
    CHECK (gens[1].name == "Channel 2" && gens[1].detail == "Insert 1");

    const auto fx = lynx::projectcontents::effects (song);
    CHECK (fx.size() == 2);
    CHECK (fx[0].index == 0 && fx[0].detail == "Master" && fx[0].name == "Some EQ" && fx[0].plugin);
    CHECK (fx[1].index == 2 && fx[1].slot == 0 && fx[1].detail == "VOCALS" && fx[1].colour == 0xFF112233u && ! fx[1].plugin);
    CHECK (! fx[1].name.empty());

    song.patterns.resize (2);
    song.patterns[0].name = "Drums";
    song.patterns[1].colour = 0xFF778899u;
    lynx::Clip c;
    c.pattern = 0;
    song.playlist.push_back (c);
    song.playlist.push_back (c);
    const auto pats = lynx::projectcontents::patterns (song);
    CHECK (pats.size() == 2 && pats[0].name == "Drums" && pats[0].uses == 2);
    CHECK (pats[1].name == "Pattern 2" && pats[1].uses == 0 && pats[1].colour == 0xFF778899u);

    lynx::Automation au;
    au.name = "Cutoff";
    au.target.kind = lynx::AutomationTargetKind::mixerVolume;
    au.target.index = 2;
    song.automations.push_back (au);
    lynx::Clip ac;
    ac.kind = lynx::ClipKind::automation;
    ac.automation = 0;
    song.playlist.push_back (ac);
    const auto autos = lynx::projectcontents::automations (song);
    CHECK (autos.size() == 1 && autos[0].uses == 1 && autos[0].colour == 0xFF112233u);
}

TEST (flui115_toolbar_drop_and_place)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    // drop "file" before "master": transport, file, master, ...
    const auto master = Group::master;
    CHECK (l.placeBefore (Group::file, &master));
    CHECK (l.orderToString() == "-link,transport,file,master,record,snap,windows,-multilink,-key,chord,meters,output");
    CHECK (! l.placeBefore (Group::file, &master)); // already there
    CHECK (l.placeBefore (Group::transport, nullptr)); // to the end
    CHECK (l.entries.back().group == Group::transport);

    // the panel lands before the first other panel whose centre is right of the drop point
    const std::vector<std::pair<Group, int>> shown { { Group::transport, 100 }, { Group::master, 300 }, { Group::snap, 500 } };
    CHECK (dropBefore (shown, Group::snap, 50) == (int) Group::transport);
    CHECK (dropBefore (shown, Group::snap, 250) == (int) Group::master);
    CHECK (dropBefore (shown, Group::master, 250) == (int) Group::snap); // itself does not count
    CHECK (dropBefore (shown, Group::transport, 600) == groupCount);   // the end
}

TEST (flui115_toolbar_presets_and_flags)
{
    using namespace lynx::toolbarlayout;
    auto l = Layout::makeDefault();
    CHECK (matchingPreset (l) == (int) Preset::standard);
    l.iconSize = IconSize::large;
    auto c = applyPreset (l, Preset::compact);
    CHECK (c.iconSize == IconSize::large); // presets keep the other options
    CHECK (c.isShown (Group::transport) && c.isShown (Group::snap) && ! c.isShown (Group::meters) && ! c.isShown (Group::file));
    CHECK (matchingPreset (c) == (int) Preset::compact);
    c.setShown (Group::file, true);
    CHECK (matchingPreset (c) == presetCount); // custom
    CHECK (applyPreset (l, Preset::minimal).orderToString() == "-link,transport,-master,-record,-snap,windows,-multilink,-file,-key,-chord,-meters,-output");

    auto d = Layout::makeDefault();
    CHECK (d.separators && d.visible && d.isDefault());
    d.visible = false;
    CHECK (! d.isDefault());
}

TEST (flui115_mixer_insert_one_and_delete)
{
    auto song = lynx::Song::makeDefault();
    song.mixer = lynx::Song::defaultMixer (4); // Master + Insert 1..4
    song.mixer[2].name = "bass";
    song.channels.resize (2);
    song.channels[0].mixerTrack = 2;
    song.channels[1].mixerTrack = 3;
    song.setRoute (2, 4, true, 0.5f);
    lynx::EffectSlot comp;
    comp.kind = lynx::EffectKind::compressor;
    comp.sidechain = 2;
    song.mixer[4].effects = { comp };

    CHECK (lynx::mixerops::insertTrack (song, 2) && song.mixer.size() == 6);
    CHECK (song.mixer[2].name == "Insert 2" && song.mixer[3].name == "bass" && song.mixer[4].name == "Insert 4"); // old Insert 3 renumbered
    CHECK (song.mixer[1].name == "Insert 1" && song.mixer[5].name == "Insert 5");
    CHECK (song.channels[0].mixerTrack == 3 && song.channels[1].mixerTrack == 4);
    CHECK (song.routes (3, 5) && song.mixer[5].effects[0].sidechain == 3);
    CHECK (! lynx::mixerops::insertTrack (song, 0) && ! lynx::mixerops::insertTrack (song, 7));

    CHECK (lynx::mixerops::deleteTrack (song, 3) && song.mixer.size() == 6); // "bass" goes, a blank takes the end
    CHECK (song.channels[0].mixerTrack == 0 && song.channels[1].mixerTrack == 3);
    CHECK (song.mixer[3].name == "Insert 3" && song.mixer[5].name == "Insert 5" && song.mixer[5].effects.empty());
    CHECK (song.mixer[4].effects.size() == 1 && song.mixer[4].effects[0].sidechain == -1); // (round 177a: none, not the Master)
    CHECK (! lynx::mixerops::deleteTrack (song, 0) && ! lynx::mixerops::deleteTrack (song, 6));
}
