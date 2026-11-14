// Round 190am: the list behind Settings > LYNX extras (core/LynxExtras.h).

#include "core/LynxExtras.h"
#include "core/Preferences.h"

TEST (extras190am_extras_list)
{
    using namespace lynx;
    std::set<std::string_view> ids, keys;
    int count = 0;
    for (const auto& item : extras::items)
    {
        ++count;
        CHECK (! item.id.empty() && ! item.section.empty() && ! item.title.empty() && ! item.note.empty());
        CHECK (ids.insert (item.id).second);            // one line per extra
        CHECK (keys.insert (item.settingsKey).second);  // one preference per line: nothing is stored twice
        // "file:key" of the preference the feature already has
        const auto colon = item.settingsKey.find (':');
        CHECK (colon != std::string_view::npos && colon > 0 && colon + 1 < item.settingsKey.size());
        CHECK (item.settingsKey.substr (0, colon) == "ui-options.txt" || item.settingsKey.substr (0, colon) == "midi-settings.txt");
        CHECK (item.note.back() == '.'); // one line of explanation, a sentence
        CHECK (item.note.find ("Also ") != std::string_view::npos); // ... that names the switch's own place
    }
    CHECK (count == 12); // (round 190gz: project.linkTracks)
    for (const auto* id : { "mixer.loudness", "mixer.usedOnly", "mixer.keepLostLinks", "mixer.followPlaylist", "playlist.automationLanes",
                            "pianoRoll.songBarsRuler", "general.hoverValues" })
        CHECK (ids.count (id) == 1);
    // the one kept in the MIDI settings is off in a fresh profile and round-trips under the key the list names
    prefs::MidiPrefs midi;
    CHECK (! midi.gamepad);
    midi.gamepad = true;
    CHECK (midi.toText().find ("gamepad=1") != std::string::npos && prefs::MidiPrefs::fromText (midi.toText()).gamepad);
}
