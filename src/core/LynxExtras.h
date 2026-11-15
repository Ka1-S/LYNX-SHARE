#pragma once

// Settings › "LYNX extras" (round 190am, decision D7): the list of LYNX's own improvements that are landed but off by
// default, so FL Studio's look and behaviour stay untouched until someone asks for them. The page shows each one with
// a switch and one line of explanation.
//
// This is only the list: every switch reads and writes the preference the feature already has (the same variable and
// the same settings key as its menu item or Settings page), so there is no second place where the state lives. An
// extra that is off by default and has its own switch elsewhere belongs here; add it when it lands.
//
// No JUCE: tested in tests/Extras190amTests.inc. The texts are English keys (TR_NOOP), translated by the page.

#include <string_view>

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::extras
{
struct Item
{
    std::string_view id;        // what the page asks the app to read or switch
    std::string_view section;   // the page's group
    std::string_view title;     // the switch's caption: the same text as the feature's own menu item or check box
    std::string_view note;      // one line: what it does, and where else the switch is
    std::string_view settingsKey; // where the preference is kept ("file:key"), for the tests and the docs
};

inline constexpr Item items[] = {
    { "mixer.loudness", TR_NOOP ("Mixer"), TR_NOOP ("Loudness meter beside the Master (LYNX)"),
      TR_NOOP ("Short-term and integrated loudness (LUFS) and the true peak of the Master's output, with a Reset button. Also in the Mixer menu."),
      "ui-options.txt:mixerLoudness" },
    { "mixer.usedOnly", TR_NOOP ("Mixer"), TR_NOOP ("Show only used inserts"),
      TR_NOOP ("Hides the inserts nothing uses. The Master, the selected insert and any insert that receives audio always stay. Also in the Mixer menu."),
      "ui-options.txt:mixerUsedOnly" },
    { "mixer.keepLostLinks", TR_NOOP ("Mixer"), TR_NOOP ("Keep links to deleted inserts, greyed (LYNX)"),
      TR_NOOP ("Deleting an insert keeps the links that went into it, greyed, so they can be connected again. Also in the Mixer menu."),
      "ui-options.txt:mixerKeepLostLinks" },
    { "mixer.followPlaylist", TR_NOOP ("Mixer"), TR_NOOP ("Mixer groups follow the Playlist (LYNX)"),
      TR_NOOP ("The mixer's groups mirror the Playlist's track groups after every change. Also in the Mixer menu."),
      "ui-options.txt:mixerFollowPlaylist" },
    { "playlist.automationLanes", TR_NOOP ("Playlist"), TR_NOOP ("Automation lanes"),
      TR_NOOP ("Automation can sit in lanes under a Playlist track; automation clips keep working as before. Also in the Playlist menu."),
      "ui-options.txt:automationLanes" },
    { "project.linkTracks", TR_NOOP ("Playlist"), TR_NOOP ("Link Playlist tracks to mixer tracks in new projects"),
      TR_NOOP ("New projects start with every Playlist track that holds clips linked to a mixer track (Track N to insert N when that leaves the sound unchanged); linked track headers show mute, solo, volume and pan. Also on the Settings › Project page, with the switch for the open project."),
      "ui-options.txt:newLinkTracks" },
    { "pianoRoll.songBarsRuler", TR_NOOP ("Piano Roll"), TR_NOOP ("Ruler: song bars (LYNX)"),
      TR_NOOP ("The Piano roll's ruler shows the bar numbers of the place in the song where the pattern plays. Also in the Piano roll's View menu."),
      "ui-options.txt:prSongBars" },
    { "general.hoverValues", TR_NOOP ("Editing"), TR_NOOP ("Show a control's value next to the mouse when hovering over it (LYNX)"),
      TR_NOOP ("The value of the knob or fader under the mouse shows beside the cursor, not only in the hint bar. Also on the General page."),
      "ui-options.txt:hoverValues" },
    { "record.lastLoopPassPlays", TR_NOOP ("Recording"), TR_NOOP ("After loop recording, only the last pass plays"),
      TR_NOOP ("The earlier passes stay under it, muted, to pick from. Also in the recording options menu."),
      "ui-options.txt:recordLastPassPlays" },
    { "detail.windowKey", TR_NOOP ("Device view"), TR_NOOP ("W shows or hides the selected device's window"),
      TR_NOOP ("A key for the selected device's own window. Also in the Device view's options menu."),
      "ui-options.txt:detailExtraWindowKey" },
    { "detail.selectInMixer", TR_NOOP ("Device view"), TR_NOOP ("Selecting a device selects its insert in the Mixer"),
      TR_NOOP ("The Mixer follows the device picked in the Device view. Also in the Device view's options menu."),
      "ui-options.txt:detailExtraSelectInMixer" },
    { "midi.gamepad", TR_NOOP ("Controllers"), TR_NOOP ("Use a game controller"),
      TR_NOOP ("A game controller plays the transport and can be linked to any control like a MIDI controller. Also on the MIDI page."),
      "midi-settings.txt:gamepad" },
};
} // namespace lynx::extras
