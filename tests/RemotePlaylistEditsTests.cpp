#include "engine/RemotePlaylistEdits.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace lynx;
using namespace lynx::remote_playlist;

void check (bool value, const char* message)
{
    if (! value) throw std::runtime_error (message);
}
template <typename Fn> void refuses (Fn&& fn)
{
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    check (rejected, "invalid Playlist transaction was accepted");
}
Song fixture()
{
    Song song;
    song.patterns.resize (1);
    song.channels.resize (1);
    song.channels[0].instrument = InstrumentKind::audioClip;
    song.automations.resize (1);
    song.playlistTracks.resize (3);
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 0;
    audio.track = 1;
    audio.start = 960;
    audio.length = 1920;
    audio.offset = 77;
    audio.offsetFraction = 0.2f;
    audio.fadeIn = 30;
    audio.fadeOut = 80;
    audio.gain = 1.3f;
    audio.pan = -0.3f;
    audio.pitch = 4.0f;
    audio.reversed = true;
    audio.stretch = 1.25;
    audio.muted = true;
    audio.crossfadeIn = 23;
    audio.crossfadeOut = 45;
    song.playlist.push_back (audio);
    uids::normalize (song);
    return song;
}
int main()
{
    try
    {
        auto song = fixture();
        const auto original = song;
        Edit move;
        move.clipId = song.playlist[0].uid;
        move.patch.start = 3840;
        auto moved = remote_playlist::apply (song, { move });
        auto expected = song.playlist[0];
        expected.start = 3840;
        check (moved.song.playlist[0] == expected, "move lost existing audio clip metadata");
        check ((song.playlist == original.playlist && song.nextUid == original.nextUid), "copy transaction mutated its original model");

        Edit duplicate;
        duplicate.action = Edit::Action::duplicate;
        duplicate.clipId = song.playlist[0].uid;
        duplicate.patch.track = 2;
        auto copied = remote_playlist::apply (song, { duplicate });
        expected = song.playlist[0];
        expected.track = 2;
        expected.uid = copied.song.playlist[1].uid;
        check (copied.song.playlist[1] == expected, "duplicate lost clip metadata");
        check (expected.uid != song.playlist[0].uid && copied.created.size() == 1, "duplicate reused source identity");

        Edit add;
        add.action = Edit::Action::add;
        add.kind = ClipKind::pattern;
        add.source = 0;
        add.patch.track = 0;
        add.patch.start = 0;
        add.patch.length = 960;
        Edit remove;
        remove.action = Edit::Action::remove;
        remove.clipId = song.playlist[0].uid;
        auto replaced = remote_playlist::apply (song, { add, remove });
        check (replaced.song.playlist.size() == 1 && replaced.song.playlist[0].kind == ClipKind::pattern,
               "add/remove transaction targeted unstable indices");
        check (replaced.created.size() == 1 && replaced.removed == 1, "transaction result counts are incorrect");

        auto invalid = move;
        invalid.patch.start = maxTick;
        refuses ([&] { remote_playlist::apply (song, { move, invalid }); });
        check ((song.playlist == original.playlist && song.nextUid == original.nextUid), "late invalid edit changed source model");
        invalid = add;
        invalid.source = 5;
        refuses ([&] { remote_playlist::apply (song, { invalid }); });
        invalid = move;
        invalid.clipId = 0;
        refuses ([&] { remote_playlist::apply (song, { invalid }); });
        invalid = move;
        invalid.patch.stretch = std::numeric_limits<double>::infinity();
        refuses ([&] { remote_playlist::apply (song, { invalid }); });
        invalid = move;
        invalid.patch.length = 1;
        refuses ([&] { remote_playlist::apply (song, { invalid }); }); // existing fades no longer fit
        invalid = add;
        invalid.patch.gain = 2.0f;
        refuses ([&] { remote_playlist::apply (song, { invalid }); }); // audio fields cannot silently affect pattern clips
        auto locked = song;
        locked.playlistTracks[1].lockedToContent = true;
        refuses ([&] { remote_playlist::apply (locked, { move }); });
        locked = song;
        locked.playlistTracks[2].lockedToContent = true;
        refuses ([&] { remote_playlist::apply (locked, { duplicate }); });
        refuses ([&] { remote_playlist::apply (song, {}); });
        refuses ([&] { remote_playlist::apply (song, std::vector<Edit> (129, move)); });
        std::puts ("Remote Playlist transaction checks passed");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf (stderr, "Remote Playlist transaction check failed: %s\n", error.what());
        return 1;
    }
}
