#pragma once

// The fixed parts of FL Studio 2026's menus that round 190dx brought to LYNX (FL facts: /mnt/e/lynx-work/flpl190dx/
// FL-FACTS.md; [FORM] = FL's form resources, [SHOT] = FL screenshots). JUCE-free menu models: the UI builds its menus
// from them (texts are the English keys of the i18n tables) and the tests check them against FL.

#include <string>
#include <vector>

namespace lynx::fltrackmenus
{
struct Entry
{
    enum class Kind { item, caption, separator, submenu, list } kind = Kind::item;
    std::string text;
    bool isDefault = false; // FL's Default item (drawn bold in FL)
    bool operator== (const Entry&) const = default;
};

// Tools › One-click audio recording › (OneClickRecMenu [FORM], live 190cq)
inline std::vector<Entry> oneClickRecording()
{
    return { { Entry::Kind::caption, "I want to record audio..." },
             { Entry::Kind::item, "Into Edison audio editor/recorder", true },
             { Entry::Kind::item, "Into playlist as an audio clip" } };
}

// The audio clip menu's Automate › (EEAUAutoMenu [FORM]: Panning Tag 0, Volume Tag 1 = Default)
inline std::vector<Entry> automate()
{
    return { { Entry::Kind::item, "Panning" }, { Entry::Kind::item, "Volume", true } };
}

// The Playlist's "+" menu (AddInstrumentMenu [FORM]); `list` = FL's generator list
inline std::vector<Entry> addTrack()
{
    return { { Entry::Kind::caption, "Audio track" },
             { Entry::Kind::item, "Add audio track", true },
             { Entry::Kind::caption, "Instrument track" },
             { Entry::Kind::item, "More plugins..." },
             { Entry::Kind::separator, {} },
             { Entry::Kind::list, "generators" } };
}

// Track mode › Instrument track › (EEPLTrackModeInstrMenu [FORM])
inline std::vector<Entry> instrumentTrack()
{
    return { { Entry::Kind::submenu, "Use existing channel" },
             { Entry::Kind::separator, {} },
             { Entry::Kind::item, "More plugins...", true },
             { Entry::Kind::separator, {} },
             { Entry::Kind::list, "generators" } };
}

// The Channel rack menu's item (LineAssignInstrTrackMenu; ChannelMenuPopup: Tag 1 text while the channel has one)
inline std::string assignInstrumentTrackText (bool hasInstrumentTrack)
{
    return hasInstrumentTrack ? "Unassign from instrument track" : "Assign to new instrument track";
}

// Mixer › Disk recording › Options, FL's order (RecordingMenu [FORM]); LYNX lists the ones it has, in this order
inline std::vector<std::string> diskRecordingOptions()
{
    return { "Auto-unarm", "Auto-create audio clip", "Latency compensation", "Ignore silent takes", "Auto name playlist tracks",
             "Group loop recorded takes", "Apply track level and mute when recording" };
}
} // namespace lynx::fltrackmenus
