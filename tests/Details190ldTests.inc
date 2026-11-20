// Round 190ld (details pass 2, details190if rows): FL's slot lock and Playlist track lock, Live's Shift+Space, the
// Toolbar's tool names (owner decision E1).
#include "core/LiveKeys.h"
#include "core/MixerOps.h"
#include "core/NoteEdit.h"

// FL's slot LED (manual "Mute/Disable LED - The same as for the Mixer Track mute LEDs"): Shift+click locks a slot's
// state; a Ctrl/Alt+click solo of another slot leaves the locked one as it is, and doesn't count against "only this on"
TEST (details190ld_a_locked_slot_keeps_its_state_under_a_slot_solo)
{
    using namespace lynx;
    Song song;
    song.mixer.resize (2);
    song.mixer[1].effects.resize (4);
    mixerops::toggleSlotLock (song, 1, 2);
    CHECK (song.mixer[1].effects[2].lockState);
    mixerops::soloSlot (song, 1, 0);
    CHECK (song.mixer[1].effects[0].enabled);
    CHECK (! song.mixer[1].effects[1].enabled);
    CHECK (song.mixer[1].effects[2].enabled); // (locked: still on)
    CHECK (! song.mixer[1].effects[3].enabled);
    // again on the soloed slot: every slot on (the locked one too, it was on)
    mixerops::soloSlot (song, 1, 0);
    for (const auto& fx : song.mixer[1].effects)
        CHECK (fx.enabled);
    // a locked slot that is off stays off under a solo elsewhere
    song.mixer[1].effects[2].enabled = false;
    mixerops::soloSlot (song, 1, 1);
    CHECK (! song.mixer[1].effects[2].enabled && song.mixer[1].effects[1].enabled && ! song.mixer[1].effects[0].enabled);
    mixerops::toggleSlotLock (song, 1, 2);
    CHECK (! song.mixer[1].effects[2].lockState);
    mixerops::toggleSlotLock (song, 1, 9); // (out of range: nothing)
    mixerops::toggleSlotLock (song, 7, 0);
    // the lock is part of the slot's identity (undo sees it)
    auto other = song.mixer[1].effects[0];
    other.lockState = true;
    CHECK (! (other == song.mixer[1].effects[0]));
}

// FL's Playlist track mute switch: Shift+click locks its state, a solo elsewhere leaves it alone
TEST (details190ld_a_locked_playlist_track_is_not_muted_by_a_solo)
{
    using namespace lynx;
    Song song;
    song.playlistTracks.resize (4);
    song.playlistTracks[2].soloLock = true;
    noteedit::soloTrack (song, 0, 4);
    CHECK (! song.playlistTracks[0].muted);
    CHECK (song.playlistTracks[1].muted);
    CHECK (! song.playlistTracks[2].muted); // (locked)
    CHECK (song.playlistTracks[3].muted);
    PlaylistTrack a, b;
    b.soloLock = true;
    CHECK (! (a == b));
}

// Live's Shift+Space ("Continue Play") acts only in the Clip-launch layout; FL's Shift+Space stays in the others
TEST (details190ld_live_continue_play_only_in_clip_launch)
{
    using namespace lynx::keys;
    using lynx::livekeys::Command;
    using lynx::layoutstyle::Style;
    CHECK (lynx::livekeys::commandFor ({ space, shift }, Style::clipLaunch) == Command::continuePlay);
    CHECK (lynx::livekeys::commandFor ({ space, shift }, Style::pattern) == Command::none);
    CHECK (lynx::livekeys::commandFor ({ space, shift }, Style::simple) == Command::none);
    CHECK (lynx::livekeys::commandFor ({ space, none }, Style::clipLaunch) == Command::none); // (plain Space: play / stop)
    CHECK (std::string (lynx::livekeys::menuKey (Command::continuePlay, Style::clipLaunch)) == "\tShift+Space");
    CHECK (findAction ("transport.playFromStart") != nullptr && findAction ("transport.playFromStart")->defaults[0] == (Chord { space, shift }));
}

// Owner decision E1 (details DE-58): the editor tools carry FL's names; Live's Shift+Enter preview is a Browser key
TEST (details190ld_tool_names_are_fl_names_and_the_browser_preview_key)
{
    using namespace lynx::keys;
    CHECK (findAction ("pianoRoll.tool.pencil") != nullptr && std::string (findAction ("pianoRoll.tool.pencil")->description) == "Tool: Draw");
    CHECK (std::string (findAction ("playlist.tool.knife")->description) == "Tool: Slice");
    CHECK (std::string (findAction ("playlist.tool.listen")->description) == "Tool: Playback");
    const auto* preview = findAction ("browser.previewLive");
    CHECK (preview != nullptr && preview->defaults[0] == (Chord { enter, shift }));
}
