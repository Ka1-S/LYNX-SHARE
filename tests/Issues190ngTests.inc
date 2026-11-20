// Round 190ng: issues #217 (Esc closes the focused window), #219 (snap key), #220 (icon search, F3), #222 (icons that grow
// with the track), #215 (export file name wildcards).

#include "core/EscClose.h"
#include "core/ExportNaming.h"
#include "core/ExportOptions.h"
#include "core/KeyMap.h"
#include "core/LynxExtras.h"
#include "core/TrackIconScale.h"
#include "core/TrackIcons.h"

TEST (issues190ng_217_esc_closes_the_focused_window)
{
    using namespace lynx;
    CHECK (escclose::enabled()); // FL 2026: on by default
    CHECK (escclose::closesPanel (true, true, true));
    CHECK (! escclose::closesPanel (false, true, true));  // the switch off: windows stay
    CHECK (! escclose::closesPanel (true, false, true));  // Clip-launch / Simple: their views stay
    CHECK (! escclose::closesPanel (true, true, false));  // a hidden panel
    bool listed = false;
    for (const auto& item : extras::items)
        if (item.id == "general.escCloses")
            listed = item.settingsKey == "ui-options.txt:escClosesWindows";
    CHECK (listed);
}

TEST (issues190ng_219_fl_snap_key_in_the_key_map)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    // FL's Backspace (the main snap's hint "BkSp") in the Playlist and the Piano roll
    CHECK (map.find ({ backspace, none }, Context::playlist, false) == "playlist.snapToggle");
    CHECK (map.find ({ backspace, none }, Context::pianoRoll, false) == "pianoRoll.snapToggle");
    CHECK (map.find ({ backspace, none }, Context::browser, false) == "browser.historyBack"); // (the Browser keeps its own)
    // the same from any window, without a key until one is given (Alt+S stays FL's Strum / Alt solo there)
    CHECK (map.find ({ 'S', alt }, Context::pianoRoll, false) == "pianoRoll.strum");
    CHECK (map.find ({ 'S', alt }, Context::mixer, false) == "mixer.altSoloSelected");
    CHECK (map.isDefault ("snap.toggle"));
}

TEST (issues190ng_220_icon_search_and_f3)
{
    using namespace lynx::trackicons;
    const auto english = [] (std::string_view s) { return std::string (s); };
    CHECK (search ("", english).size() == entries.size());
    const auto hat = search ("hat", english);
    CHECK (hat.size() == 1 && entries[(std::size_t) hat.front()].icon == Icon::hiHat);
    const auto guitar = search ("el gu", english);
    CHECK (guitar.size() == 1 && entries[(std::size_t) guitar.front()].icon == Icon::electricGuitar);
    CHECK (search ("GUITAR", english).size() == 2); // (acoustic and electric; case ignored)
    CHECK (search ("zzz", english).empty());
    CHECK (! matches ("tar", "Guitar")); // (word starts only: "tar" is not a word start)
    // the user's language counts too
    const auto portuguese = [] (std::string_view s) { return s == "Kick" ? std::string ("Bumbo") : std::string (s); };
    const auto bumbo = search ("bum", portuguese);
    CHECK (bumbo.size() == 1 && entries[(std::size_t) bumbo.front()].icon == Icon::kick);

    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.find ({ f3, none }, Context::playlist, false) == "playlist.changeIcon");
    CHECK (map.find ({ f3, none }, Context::mixer, false) == "mixer.changeIcon");
    CHECK (map.find ({ f3, none }, Context::channelRack, false) == "rack.changeIcon");
    CHECK (map.find ({ f3, none }, Context::browser, false) == "browser.findNext");
    CHECK (map.find ({ f2, none }, Context::mixer, false) == "mixer.rename");
}

TEST (issues190ng_222_track_icons_grow_with_the_track)
{
    using namespace lynx::trackiconscale;
    // FL 2026, measured: rows 56 / 68 / 80 px no big icon, 92 / 104 / 116 px half the row
    for (const auto h : { 14, 26, 56, 68, 80 })
        CHECK (bigIconSize (h, 100) == 0);
    CHECK (bigIconSize (92, 100) == 46);
    CHECK (bigIconSize (104, 100) == 52);
    CHECK (bigIconSize (116, 100) == 58);
    CHECK (bigIconSize (300, 100) == 84); // (never wider than the plate)
    CHECK (bigIconSize (300, 30) == 0);   // (a plate too narrow for it)
    // the plate's colour x 0.85 (FL: 212,169,21 -> 180,144,18)
    CHECK (shadeArgb (0xffd4a915u) == 0xffb49012u);
    CHECK (shadeArgb (0x80000000u) == 0x80000000u);
}

TEST (issues190ng_215_export_name_wildcards)
{
    using namespace lynx::exportnaming;
    Context c;
    c.project = "MySong";
    c.year = 2026;
    c.month = 11;
    c.day = 9;
    c.hour = 7;
    c.minute = 5;
    c.second = 3;
    c.bpm = 128.0;
    c.key = "A minor";
    c.sampleRate = 48000;
    c.bitDepth = "24bit";
    c.format = "WAV";
    c.track = "Master";
    c.pattern = "Verse";
    // REAPER's examples of the issue
    CHECK (resolve ("$project_$year-$month-$day", c) == "MySong_2026-11-09");
    CHECK (resolve ("$project_$format_$samplerate", c) == "MySong_WAV_48000");
    CHECK (resolve ("$date $time", c) == "2026-11-09 07-05");
    CHECK (resolve ("$year2$month$day", c) == "261109");
    CHECK (resolve ("$monthname", c) == "November");
    CHECK (resolve ("$bpm BPM $key", c) == "128 BPM A minor");
    c.bpm = 87.5;
    CHECK (resolve ("$bpm", c) == "87.5");
    c.bpm = 140.25;
    CHECK (resolve ("$bpm", c) == "140.25");
    CHECK (resolve ("$pattern - $bitdepth", c) == "Verse - 24bit");
    CHECK (resolve ("$PROJECT", c) == "MySong");          // case ignored
    CHECK (resolve ("$projectX $unknown $$5", c) == "MySongX $unknown $5"); // longest match, unknown kept, $$ = $
    CHECK (resolve ("", c) == "MySong");                  // empty: the default "$project"
    // the take: the first number whose file isn't there yet
    c.exists = [] (const std::string& n) { return n == "MySong_Take1" || n == "MySong_Take2"; };
    CHECK (resolve ("$project_Take$take", c) == "MySong_Take3");
    // stems: $track stays for each stem's file, which fills it
    CHECK (resolve ("$project_$track", c, true) == "MySong_$track");
    CHECK (resolve ("$project_$track", c, false) == "MySong_Master");
    CHECK (hasStemWildcard ("x_$track") && hasStemWildcard ("$Insert") && ! hasStemWildcard ("$$track") && ! hasStemWildcard ("$project"));
    CHECK (lynx::exportopts::stemFileName ("MySong_$track", 3, "Kick: 808", ".wav") == "MySong_Kick_ 808.wav");
    CHECK (lynx::exportopts::stemFileName ("MySong", 3, "Kick", ".wav") == "MySong - 3 Kick.wav"); // (no wildcard: as before)
    // safe names on every system
    CHECK (sanitise ("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK (sanitise ("  name. . ") == "name");
    CHECK (sanitise ("CON") == "_CON" && sanitise ("nul.mix") == "_nul.mix" && sanitise ("Concert") == "Concert");
    CHECK (sanitise ("") == "Untitled" && sanitise ("...") == "Untitled");
    CHECK (sanitise (std::string (300, 'x')).size() == 180);
    CHECK (sanitise (std::string (179, 'x') + "\xc3\xa9") .size() == 179); // (a 2-byte character is not cut in half)
    // every listed wildcard gives its example's kind of value (none is left as typed)
    for (const auto& w : wildcards)
        CHECK (resolve (w.token, c).find ('$') == std::string::npos);
}

TEST (issues190ng_215_template_is_kept_with_the_choices)
{
    using namespace lynx::exportopts;
    Options o;
    CHECK (o.nameTemplate == "$project");
    o.nameTemplate = "$project_$date=v2";
    const auto back = fromText (toText (o));
    CHECK (back.nameTemplate == "$project_$date=v2");
    CHECK (back == o);
    CHECK (fromText ("format=0\n").nameTemplate == "$project"); // (old settings files)
    Options blank;
    blank.nameTemplate = "   ";
    CHECK (clamped (blank).nameTemplate == "$project");
}
