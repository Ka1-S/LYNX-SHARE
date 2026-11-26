// --- Settings (General / Audio / MIDI pages), Plugin manager and Shortcut manager logic (core/Preferences.h,
// core/KeyMap.h) - round 82 ----------------------------------------------------------------------------------------

TEST (prefs_key_values)
{
    const auto kv = lynx::prefs::parseKeyValues ("a=1\n  b = two words \r\nnoequals\n=x\na=3\nc=\n");
    CHECK (kv.size() == 3);
    CHECK (kv.at ("a") == "3" && kv.at ("b") == "two words" && kv.at ("c").empty());
}

// A folder whose name ends in a space (".../lunx ") is a different folder than ".../lunx": the New project window saved
// the path in new-project.txt, parseKeyValues trimmed the space, and the next project went to another folder (a second
// "lunx" in Finder). encodeSettingsPath/decodeSettingsPath keep the path exactly as it is.
TEST (prefs_paths_keep_a_trailing_space)
{
    using namespace lynx::prefs;
    const std::string spaced = "/Users/me/Desktop/all in mac ssd/lunx ";
    CHECK (encodeSettingsPath (spaced) == spaced + "/");
    CHECK (decodeSettingsPath (encodeSettingsPath (spaced)) == spaced);
    // through a settings line, as the New project window writes it
    const auto kv = parseKeyValues ("show=1\nfolder=" + encodeSettingsPath (spaced) + "\nownFolder=1\n");
    CHECK (decodeSettingsPath (kv.at ("folder")) == spaced);
    // every other path is written and read as it is (a drive root included)
    CHECK (encodeSettingsPath ("/Users/me/Music") == "/Users/me/Music");
    CHECK (decodeSettingsPath ("/Users/me/Music") == "/Users/me/Music");
    CHECK (encodeSettingsPath ("C:\\") == "C:\\" && decodeSettingsPath ("C:\\") == "C:\\");
}

TEST (prefs_autosave_choices_and_backup_rotation)
{
    using namespace lynx::prefs;
    CHECK (clampAutosaveMinutes (0) == 0 && clampAutosaveMinutes (-5) == 0);
    CHECK (clampAutosaveMinutes (2) == 2 && clampAutosaveMinutes (4) == 5 && clampAutosaveMinutes (999) == 30 && clampAutosaveMinutes (3) == 2);
    CHECK (clampAutosaveKeep (0) == 1 && clampAutosaveKeep (500) == 100 && clampAutosaveKeep (7) == 7);

    const auto a = backupName ("Night Drive", 2026, 10, 16, 9, 5, 3);
    CHECK (a == "Night Drive (2026-10-16 09.05.03)");
    CHECK (isBackupOf (a, "Night Drive"));
    CHECK (isBackupOf ("Night Drive", "Night Drive"));             // the single backup of older versions
    CHECK (! isBackupOf ("Night Drive 2 (2026-10-16 09.05.03)", "Night Drive"));
    CHECK (! isBackupOf ("Night Drive (2026-10-16 09.05.0x)", "Night Drive"));
    CHECK (! isBackupOf ("Night (2026-10-16 09.05.03)", "Night Drive"));

    const std::vector<std::string> names { backupName ("Song", 2026, 10, 16, 10, 0, 0), backupName ("Song", 2026, 10, 15, 23, 59, 0), "Song",
                                           backupName ("Other", 2026, 1, 1, 0, 0, 0), backupName ("Song", 2026, 10, 16, 11, 0, 0) };
    const auto gone = backupsToRemove (names, "Song", 2);
    CHECK (gone.size() == 2);
    CHECK (gone.size() == 2 && gone[0] == "Song" && gone[1] == backupName ("Song", 2026, 10, 15, 23, 59, 0));
    CHECK (backupsToRemove (names, "Song", 10).empty());
    CHECK (backupsToRemove (names, "Other", 1).empty());
    CHECK (clampUndoLevels (1) == 10 && clampUndoLevels (5000) == 1000 && clampUndoLevels (250) == 250);
    CHECK (std::abs (bufferLatencyMs (480, 48000.0) - 10.0) < 1e-9 && bufferLatencyMs (256, 0.0) == 0.0);
}

TEST (prefs_midi_velocity_curves_and_device_settings)
{
    using namespace lynx::prefs;
    CHECK (mapVelocity (0.5f, VelocityCurve::linear) == 0.5f);
    CHECK (mapVelocity (0.5f, VelocityCurve::soft) > 0.6f && mapVelocity (0.5f, VelocityCurve::hard) < 0.35f);
    CHECK (mapVelocity (0.2f, VelocityCurve::fixed) == 1.0f && mapVelocity (0.0f, VelocityCurve::fixed) == 0.0f);
    CHECK (mapVelocity (1.0f, VelocityCurve::soft) == 1.0f && mapVelocity (1.0f, VelocityCurve::hard) == 1.0f);
    CHECK (mapVelocity (2.0f, VelocityCurve::linear) == 1.0f);

    MidiPrefs p;
    CHECK (p.accepts ("Keystation 49", 3)); // unknown devices: on, any channel
    MidiInputPrefs k;
    k.name = "Keystation 49";
    k.midiChannel = 2;
    k.target = 4;
    p.setInput (k);
    MidiInputPrefs pads;
    pads.name = "Pads | MPD=218";
    pads.enabled = false;
    p.setInput (pads);
    p.velocity = VelocityCurve::soft;
    p.pickup = true;
    CHECK (p.inputs.size() == 2);
    CHECK (p.accepts ("Keystation 49", 2) && ! p.accepts ("Keystation 49", 3) && ! p.accepts ("Pads | MPD=218", 1));
    CHECK (p.input ("Keystation 49").target == 4 && p.input ("Other").target == -1);

    const auto back = MidiPrefs::fromText (p.toText());
    CHECK (back == p);
    CHECK (back.input ("Pads | MPD=218").enabled == false);

    // setting a device back to the defaults drops it from the list
    k.midiChannel = 0;
    k.target = -1;
    p.setInput (k);
    CHECK (p.inputs.size() == 1);
    CHECK (MidiPrefs::fromText ("input=1|99|-7|X\ninput=junk\nvelocity=hard\n").input ("X").midiChannel == 16);
    CHECK (MidiPrefs::fromText ("velocity=hard").velocity == VelocityCurve::hard);
}

TEST (prefs_midi_pickup)
{
    using namespace lynx::prefs;
    CHECK (! pickupReached (0.5f, -1.0f, 0.1f));    // first value, far away
    CHECK (pickupReached (0.5f, -1.0f, 0.5f));      // lands on it
    CHECK (! pickupReached (0.5f, 0.1f, 0.3f));     // still below
    CHECK (pickupReached (0.5f, 0.3f, 0.7f));       // moved past it (up)
    CHECK (pickupReached (0.5f, 0.9f, 0.2f));       // moved past it (down)
    CHECK (pickupReached (0.5f, 0.2f, 0.505f));     // within the tolerance

    PickupState st;
    CHECK (! pickupUpdate (st, 0.5f, 0.1f));        // the knob is elsewhere: nothing changes
    CHECK (! pickupUpdate (st, 0.5f, 0.3f));
    CHECK (pickupUpdate (st, 0.5f, 0.6f));          // crossed the value: caught
    CHECK (pickupUpdate (st, 0.6f, 0.9f));          // then follows even in big steps
    CHECK (pickupUpdate (st, 0.9f, 0.2f));
    CHECK (! pickupUpdate (st, 0.8f, 0.25f));       // the parameter was moved by the mouse: lost again
    CHECK (pickupUpdate (st, 0.8f, 0.85f));
}

TEST (prefs_plugin_manager_filter_and_favourites)
{
    using namespace lynx::prefs;
    std::vector<PluginRow> rows (4);
    rows[0] = { "Serum", "Xfer Records", "VST3", "Synth", "C:\\VST3\\Serum.vst3", "s", true, PluginStatus::ok, true };
    rows[1] = { "Pro-Q 3", "FabFilter", "VST3", "EQ", "C:\\VST3\\Pro-Q 3.vst3", "q", false, PluginStatus::ok, false };
    rows[2] = { "Broken", "", "CLAP", "", "C:\\CLAP\\Broken.clap", "", false, PluginStatus::failed, false };
    rows[3] = { "Analog Lab", "Arturia", "CLAP", "Synth", "C:\\CLAP\\Analog Lab.clap", "a", true, PluginStatus::missingFile, false };

    CHECK ((visiblePlugins (rows, "", PluginFilter::all, PluginSort::name) == std::vector<int> { 3, 2, 1, 0 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::instruments, PluginSort::name) == std::vector<int> { 3, 0 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::effects, PluginSort::name) == std::vector<int> { 1 })); // failed ones are not effects
    CHECK ((visiblePlugins (rows, "", PluginFilter::favourites, PluginSort::name) == std::vector<int> { 0 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::failed, PluginSort::name) == std::vector<int> { 3, 2 }));
    CHECK ((visiblePlugins (rows, "fab vst3", PluginFilter::all, PluginSort::name) == std::vector<int> { 1 }));
    CHECK ((visiblePlugins (rows, "SYNTH", PluginFilter::all, PluginSort::vendor) == std::vector<int> { 3, 0 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::all, PluginSort::format) == std::vector<int> { 3, 2, 1, 0 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::all, PluginSort::name, true) == std::vector<int> { 0, 1, 2, 3 }));
    CHECK ((visiblePlugins (rows, "", PluginFilter::all, PluginSort::status) == std::vector<int> { 1, 0, 2, 3 }));

    const std::set<std::string> fav { "VST3-Serum-1", "CLAP-x" };
    CHECK (parseFavourites (formatFavourites (fav)) == fav);
    CHECK (parseFavourites ("\n  a \r\n\nb\n").size() == 2);
}

TEST (keymap_shortcut_manager_helpers)
{
    lynx::keys::KeyMap map;
    CHECK (map.overridesJson() == "{\n}\n"); // nothing changed
    CHECK (map.isDefault ("pattern.next"));

    // the chord of an existing global action, asked for another global action: reported as taken
    const auto& play = map.chordsFor ("transport.playStop");
    CHECK (! play.empty());
    const auto users = map.actionsWith (play.front(), lynx::keys::Context::global, "transport.pause");
    CHECK (users.size() == 1 && users[0] == "transport.playStop");
    CHECK (map.actionsWith (play.front(), lynx::keys::Context::global, "transport.playStop").empty());

    lynx::keys::Chord f9 { lynx::keys::f9, lynx::keys::ctrl | lynx::keys::shift };
    CHECK (map.actionsWith (f9, lynx::keys::Context::global).empty());
    map.setChords ("pattern.next", { f9 });
    CHECK (! map.isDefault ("pattern.next"));
    const auto json = map.overridesJson();
    CHECK (json == "{\n  \"pattern.next\": [\"Ctrl+Shift+F9\"]\n}\n");
    CHECK (map.actionsWith (f9, lynx::keys::Context::global).size() == 1);

    // the overrides read back give the same map
    lynx::keys::KeyMap back;
    std::vector<std::string> errors;
    CHECK (back.loadJson (json, &errors) && errors.empty());
    CHECK (back.chordsFor ("pattern.next") == map.chordsFor ("pattern.next"));
    CHECK (back.toJson() == map.toJson());

    map.setChords ("transport.playStop", {});
    CHECK (map.overridesJson().find ("\"transport.playStop\": []") != std::string::npos);
}
