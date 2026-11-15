// Round 190hf (issue #152 point 6): plugins dropped from the Browser on the Playlist's track headers (FL) and on the
// Clip-launch workflow's tracks (Live). Facts: /mnt/e/lynx-work/dnd190hf/DROP-FACTS.md.
namespace plugindrop190hf
{
inline lynx::ChannelDef generator (const char* name)
{
    lynx::ChannelDef c;
    c.name = name;
    c.instrument = lynx::InstrumentKind::synth;
    return c;
}

inline lynx::EffectSlot effect()
{
    lynx::EffectSlot fx;
    fx.kind = lynx::EffectKind::eq;
    return fx;
}
} // namespace plugindrop190hf

TEST (plugindrop190hf_fl_targets_follow_the_selection)
{
    using namespace lynx::plugindrop;
    // a header outside the selection: that track alone (FL selects it first)
    CHECK (generatorTargets (5, { 1, 2 }) == std::vector<int> { 5 });
    // inside it: every selected track, in order
    CHECK (generatorTargets (2, { 4, 2, 1 }) == std::vector<int> ({ 1, 2, 4 }));
    CHECK (generatorTargets (-1, { 1 }).empty());
}

TEST (plugindrop190hf_fl_generator_on_headers)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    const auto channels = song.channels.size();
    // an unassigned track becomes, in place, the instrument track of a new channel on a free mixer track
    auto r = plugindrop::generatorOnTracks (song, plugindrop190hf::generator ("Lead"), { 3 });
    CHECK (r.ok() && ! r.replaced && r.tracks == std::vector<int> { 3 } && song.channels.size() == channels + 1);
    if (r.ok() && r.channels.size() == 1)
    {
        const auto ch = r.channels[0];
        CHECK (ch == (int) channels && song.channels[(std::size_t) ch].name == "Lead");
        CHECK (song.playlistTracks[3].mode == PlaylistTrackMode::instrument && song.playlistTracks[3].channel == ch);
        CHECK (song.channels[(std::size_t) ch].mixerTrack > 0);
        // the same track again: its channel's generator is replaced (channel, insert and track stay)
        const auto insert = song.channels[(std::size_t) ch].mixerTrack;
        auto pad = plugindrop190hf::generator ("Pad");
        pad.instrument = InstrumentKind::sampler;
        auto again = plugindrop::generatorOnTracks (song, pad, { 3 });
        CHECK (again.ok() && again.replaced && again.channels == std::vector<int> { ch } && song.channels.size() == channels + 1);
        CHECK (song.channels[(std::size_t) ch].instrument == InstrumentKind::sampler && song.channels[(std::size_t) ch].mixerTrack == insert);
        CHECK (song.playlistTracks[3].channel == ch);
    }
    // several selected tracks: each one (here two new instrument tracks)
    r = plugindrop::generatorOnTracks (song, plugindrop190hf::generator ("Keys"), { 5, 6 });
    CHECK (r.ok() && r.tracks == std::vector<int> ({ 5, 6 }) && r.channels.size() == 2 && song.channels.size() == channels + 3);
    // more than 20: refused, nothing changes
    std::vector<int> many;
    for (int t = 10; t < 31; ++t)
        many.push_back (t);
    const auto before = song.channels.size();
    CHECK (plugindrop::generatorOnTracks (song, plugindrop190hf::generator ("X"), many).error == plugindrop::Error::tooMany);
    CHECK (song.channels.size() == before);
    // a channel kind that can't be an instrument track (FL's error)
    ChannelDef clip;
    clip.instrument = InstrumentKind::audioClip;
    CHECK (plugindrop::generatorOnTracks (song, clip, { 8 }).error == plugindrop::Error::notInstrumentTrack);
    CHECK (song.channels.size() == before);
}

TEST (plugindrop190hf_fl_effect_on_headers)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    // an unassigned track: FL refuses ("Only generators can be used as instrument tracks.")
    CHECK (plugindrop::effectOnTrack (song, plugindrop190hf::effect(), 2).error == plugindrop::Error::onlyGenerators);
    // an instrument track: its channel's mixer track, first free slot
    auto r = plugindrop::generatorOnTracks (song, plugindrop190hf::generator ("Lead"), { 2 });
    CHECK (r.ok());
    if (! r.ok())
        return;
    const auto insert = song.channels[(std::size_t) r.channels[0]].mixerTrack;
    auto e = plugindrop::effectOnTrack (song, plugindrop190hf::effect(), 2);
    CHECK (e.ok() && e.insert == insert && e.effect == 0 && song.mixer[(std::size_t) insert].effects.size() == 1);
    // an audio track: its own mixer track
    CHECK (trackmodes::makeAudioTrack (song, 4));
    const auto audioInsert = song.playlistTracks[4].mixerTrack;
    e = plugindrop::effectOnTrack (song, plugindrop190hf::effect(), 4);
    CHECK (e.ok() && e.insert == audioInsert && song.mixer[(std::size_t) audioInsert].effects.size() == 1);
    // ten slots taken: refused
    for (int i = 1; i < maxEffectSlots; ++i)
        CHECK (plugindrop::effectOnTrack (song, plugindrop190hf::effect(), 4).ok());
    CHECK (plugindrop::effectOnTrack (song, plugindrop190hf::effect(), 4).error == plugindrop::Error::noEffectSlot);
    CHECK ((int) song.mixer[(std::size_t) audioInsert].effects.size() == maxEffectSlots);
}

TEST (plugindrop190hf_live_devices_on_session_tracks)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    const auto channels = song.channels.size();
    // an empty track: an instrument makes it a MIDI track of a new channel (Live's drop area: a new MIDI track)
    auto r = plugindrop::instrumentOnSessionTrack (song, plugindrop190hf::generator ("Lead"), 6);
    CHECK (r.ok() && ! r.replaced && song.channels.size() == channels + 1 && song.playlistTracks[6].mode == PlaylistTrackMode::instrument);
    if (! r.ok())
        return;
    const auto ch = r.channels[0];
    // an audio effect on it: at the end of its chain
    auto e = plugindrop::effectOnSessionTrack (song, plugindrop190hf::effect(), 6);
    const auto insert = song.channels[(std::size_t) ch].mixerTrack;
    CHECK (e.ok() && e.insert == insert && e.effect == 0);
    e = plugindrop::effectOnSessionTrack (song, plugindrop190hf::effect(), 6);
    CHECK (e.ok() && e.effect == 1 && song.mixer[(std::size_t) insert].effects.size() == 2);
    // another instrument on it: the instrument is replaced, the effects stay
    auto pad = plugindrop190hf::generator ("Pad");
    pad.instrument = InstrumentKind::sampler;
    r = plugindrop::instrumentOnSessionTrack (song, pad, 6);
    CHECK (r.ok() && r.replaced && r.channels == std::vector<int> { ch } && song.channels[(std::size_t) ch].instrument == InstrumentKind::sampler);
    CHECK (song.mixer[(std::size_t) insert].effects.size() == 2 && song.channels.size() == channels + 1);
    // an empty track and an audio effect: an audio track on a free mixer track holding it
    e = plugindrop::effectOnSessionTrack (song, plugindrop190hf::effect(), 8);
    CHECK (e.ok() && song.playlistTracks[8].mode == PlaylistTrackMode::audio && e.insert == song.playlistTracks[8].mixerTrack);
    // an instrument on that audio track: refused (Live: only audio effects on an audio track)
    CHECK (plugindrop::instrumentOnSessionTrack (song, plugindrop190hf::generator ("X"), 8).error == plugindrop::Error::onlyAudioEffects);
    CHECK (song.channels.size() == channels + 1);
}
