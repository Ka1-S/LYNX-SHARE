// Issue #87: an audio clip follows a tempo change (its file keeps its speed) unless its channel is fitted to beats
TEST (issue87_audio_clips_follow_the_tempo)
{
    lynx::Song song;
    song.bpm = 120.0;
    song.channels.resize (2);
    song.channels[0].instrument = song.channels[1].instrument = lynx::InstrumentKind::audioClip;
    song.channels[1].sampler.stretchBeats = 8.0f;
    lynx::Clip a;
    a.kind = lynx::ClipKind::audio;
    a.channel = 0;
    a.length = 960;
    a.offset = 96;
    auto b = a;
    b.channel = 1;
    song.playlist = { a, b };
    song.bpm = 60.0;
    lynx::clipops::followTempo (song, 120.0);
    CHECK (song.playlist[0].length == 480 && song.playlist[0].offset == 48);
    CHECK (song.playlist[1].length == 960 && song.playlist[1].offset == 96);
}
