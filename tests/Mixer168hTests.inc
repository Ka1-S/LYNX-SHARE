// --- Round 168h: FL's Mixer layout choices (issue #52), track icons, default insert names after a move (#47) ------

#include "core/MixerLayout.h"
#include "core/MixerOps.h"
#include "core/TrackIcons.h"

TEST (mixer168h_layouts_names_order_and_fl_widths)
{
    using namespace lynx::mixerlayout;
    // FL's drop-down (TFXForm.FXViewModeSelect): its items in order, FL's FXViewMode index = the position
    const char* names[] { "Compact", "Compact 2", "Compact alt", "Wide", "Wide 2", "Wide 3", "Wide alt", "Extra large" };
    for (int i = 0; i < layoutCount; ++i)
        CHECK (nameOf ((Layout) i) == names[i]);
    CHECK (separatorAfter (Layout::compactAlt) && separatorAfter (Layout::wideAlt) && ! separatorAfter (Layout::wide3));
    CHECK (defaultLayout == Layout::wide); // the global's initial value is 3

    // FUN_01280570's widths at 100 % (FL's Wide strips: 44 px apart on screen)
    const int at100[] { 20, 22, 20, 44, 42, 48, 44, 64 };
    for (int i = 0; i < layoutCount; ++i)
        CHECK (flStripWidth ((Layout) i) == at100[i]);
    // and scaled: always even bases, Wide at 125 % = 2 * 26 + 2 * Round (2.5 - 0.5) = 56
    CHECK (flStripWidth (Layout::wide, 1.25f) == 56);
    CHECK (flStripWidth (Layout::compact, 1.25f) == 26);
    CHECK (flStripWidth (Layout::extraLarge, 2.0f) == 128);

    // ui-options.txt "mixerLayout" (FL's numbering); junk and out of range -> Wide
    CHECK (parse ("0") == Layout::compact && parse ("7") == Layout::extraLarge && parse (" 4 ") == Layout::wide2);
    CHECK (parse ("8") == Layout::wide && parse ("-1") == Layout::wide && parse ("abc") == Layout::wide && parse ("") == Layout::wide);

    // LYNX's drawing of them: names up the strip except Extra large; the alt layouts put it low; widths = FL's
    for (int i = 0; i < layoutCount; ++i)
    {
        const auto l = (Layout) i;
        const auto s = specOf (l);
        CHECK (s.stripWidth == flStripWidth (l));
        CHECK (s.verticalName == (l != Layout::extraLarge));
        CHECK (s.nameLow == (l == Layout::compactAlt || l == Layout::wideAlt));
        CHECK (s.masterWidth > s.stripWidth && s.stripWidth - s.gap >= 18);
        CHECK (s.knobSize <= s.stripWidth - s.gap);
        CHECK (2 * s.meterWidth + 2 < s.stripWidth - s.gap); // room for the fader beside the meters
    }
    CHECK (! specOf (Layout::compact).stereoKnob && ! specOf (Layout::compact).dbReadout && specOf (Layout::wide).stereoKnob);
}

TEST (mixer168h_track_icons_map_fl_indices)
{
    using namespace lynx::trackicons;
    // every LYNX icon but `tag` is in the picker once, with a distinct FL index
    std::set<int> flSeen;
    std::set<int> iconSeen;
    for (const auto& e : entries)
    {
        CHECK (iconSeen.insert ((int) e.icon).second);
        CHECK (flSeen.insert (e.flIndex).second);
        CHECK (iconFor (e.flIndex) == e.icon);     // what LYNX saves reads back as the same icon
        CHECK (flIndexOf (e.icon) == e.flIndex);
        CHECK (! e.name.empty());
    }
    CHECK ((int) iconSeen.size() == iconCount - 1 && iconSeen.count ((int) Icon::tag) == 0);
    CHECK (entries.front().icon == Icon::none && entries.front().flIndex == 0);

    // FL's glyph names (U+F400 + index) against the demo projects' names
    CHECK (iconFor (5) == Icon::kick && iconFor (4) == Icon::snare && iconFor (239) == Icon::hiHat && iconFor (6) == Icon::cymbal);
    CHECK (iconFor (20) == Icon::vocals && iconFor (3) == Icon::strings && iconFor (14) == Icon::bass && iconFor (12) == Icon::piano);
    CHECK (iconFor (93) == Icon::sidechain && iconFor (116) == Icon::master && iconFor (119) == Icon::reverb && iconFor (132) == Icon::clap);
    CHECK (iconFor (59) == Icon::number1 && iconFor (67) == Icon::number9);
    // FL icons LYNX has no own one for: the closest, else a label; 0 / negative: none
    CHECK (iconFor (10) == Icon::flute && iconFor (80) == Icon::cymbal && iconFor (90) == Icon::acousticGuitar && iconFor (74) == Icon::automation);
    CHECK (iconFor (200) == Icon::tag && iconFor (0xFFFF) == Icon::tag && iconFor (0) == Icon::none && iconFor (-3) == Icon::none);
    CHECK (flIndexOf (Icon::tag) == 0);
}

TEST (mixer168h_default_insert_names_follow_the_position)
{
    // issue #47's screenshot: insert 16 moved to strip 8 kept "Insert 16". FL shows an unnamed track as "Insert <its
    // number>": the default name follows the move, a user's name stays with its track.
    using namespace lynx;
    auto song = Song::makeDefault();
    song.mixer.resize (17);
    for (int t = 1; t < 17; ++t)
        song.mixer[(std::size_t) t].name = "Insert " + std::to_string (t);
    song.mixer[3].name = "Drums";

    std::vector<int> selection { 16 };
    for (int i = 0; i < 8; ++i)
        CHECK (mixerops::moveTracks (song, selection, -1));
    CHECK (selection == std::vector<int> { 8 });
    for (int t = 1; t < 17; ++t)
        CHECK (song.mixer[(std::size_t) t].name == (t == 3 ? std::string ("Drums") : "Insert " + std::to_string (t)));

    mixerops::swapTracks (song, 3, 5); // a named track keeps its name, the default one takes its new number
    CHECK (song.mixer[5].name == "Drums" && song.mixer[3].name == "Insert 3");

    mixerops::deleteTrack (song, 2); // the tracks after it move left: still "Insert <number>"
    CHECK (song.mixer[2].name == "Insert 2" && song.mixer[4].name == "Drums" && song.mixer[16].name == "Insert 16");
}
