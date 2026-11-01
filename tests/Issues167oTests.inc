// Round 167o: issues #23 (F-keys) and #24 (preset arrows), #22 (SFZ) further below.

TEST (issue23_f10_f11_toggle_settings_like_fl)
{
    using namespace lynx::keys;
    // FLEngine 0x13545f0: open on the asked page → close; on another page → switch; closed → open
    CHECK (settingsKeyPress (false, -1, 5) == SettingsKey::open);
    CHECK (settingsKeyPress (true, 5, 5) == SettingsKey::close);
    CHECK (settingsKeyPress (true, 1, 5) == SettingsKey::switchPage);
}

TEST (issue23_fl_function_keys_and_the_sheet_list)
{
    using namespace lynx::keys;
    KeyMap map;
    CHECK (map.conflicts().empty());
    CHECK (map.find ({ f12, ctrl }, Context::global, false) == "view.closeUnfocusedWindows");
    CHECK (map.find ({ f7, alt }, Context::global, false) == "view.touchKeyboard");
    CHECK (map.find ({ f8 }, Context::global, false) == "panel.quickAdd");
    CHECK (map.find ({ f1 }, Context::global, false) == "help.shortcuts");
    // the sheet's column: every F-key binding, F1 first, F12 last, plain before Shift / Ctrl / Alt
    const auto list = map.functionKeyBindings();
    CHECK (list.size() >= 20);
    CHECK (list.front().chord == (Chord { f1 }) && list.front().actionId == "help.shortcuts");
    CHECK (list.back().chord.key == f12);
    for (std::size_t i = 1; i < list.size(); ++i)
        CHECK (list[i - 1].chord.key <= list[i].chord.key);
    auto at = [&] (Chord c) {
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].chord == c)
                return (int) i;
        return -1;
    };
    CHECK (at ({ f4 }) >= 0 && at ({ f4 }) < at ({ f4, shift }) && at ({ f4, shift }) < at ({ f4, ctrl }));
    CHECK (at ({ f8 }) < at ({ f8, shift }) && at ({ f8, shift }) < at ({ f8, alt }));
    CHECK (at ({ f2 }) >= 0); // the pattern's rename (global) and the Browser's / Mixer's own F2
    // a key the user chose shows up in place of the default
    map.setChords ("help.shortcuts", { { f3 } });
    const auto changed = map.functionKeyBindings();
    CHECK (changed.front().chord == (Chord { f2 }));
    bool hasF3 = false;
    for (const auto& b : changed)
        hasF3 = hasF3 || (b.chord == Chord { f3 } && b.actionId == "help.shortcuts");
    CHECK (hasF3);
}

#include "core/PresetCycle.h"
#include "core/MidiOut.h"

namespace issues167o
{
std::filesystem::path scratch (const std::string& name)
{
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path (ec) / ("lynx_issues167o_" + processTempTag() + "_" + name);
    std::filesystem::remove_all (dir, ec);
    std::filesystem::create_directories (dir, ec);
    return dir;
}

void touch (const std::filesystem::path& file, const std::vector<std::uint8_t>& bytes = { 1 })
{
    std::filesystem::create_directories (file.parent_path());
    std::ofstream (file, std::ios::binary).write ((const char*) bytes.data(), (std::streamsize) bytes.size());
}
} // namespace issues167o

TEST (issue24_fl_preset_order)
{
    using namespace lynx::presetcycle;
    // FLEngine 0x662a40 as measured on FL's Ogun (shots pn1–pn5): the folder's own files, then the subfolders
    CHECK (flLess ("Default.fst", "Default (pad).fst"));
    CHECK (flLess ("Default (pad).fst", "Default (tonal).fst"));
    CHECK (flLess ("Default (tonal).fst", "Atmospheric/300 Ft Glass Bowl.fst"));
    CHECK (flLess ("Atmospheric/300 Ft Glass Bowl.fst", "Atmospheric/Alien Queens Lair.fst"));
    CHECK (flLess ("Atmospheric/Zz.fst", "Bass/Aa.fst"));
    CHECK (! flLess ("SFX/Waterphone.fst", "Percussion/Waterphone Smack.fst"));
    CHECK (flLess ("Bass/Sub.fst", "Bass/Deep/A.fst"));   // a folder's files before its subfolders
    CHECK (flLess ("Pad 2.fst", "Pad 10.fst"));            // numbers as numbers
    CHECK (flLess ("alpha.fst", "Beta.fst"));              // letter case doesn't order
    CHECK (compareNatural ("abc", "ABC") != 0 && compareNatural ("x1", "x01") != 0);
    CHECK (! flLess ("Default.fst", "Default.fst"));
}

TEST (issue24_fl_preset_arrows_step_and_wrap)
{
    using namespace lynx::presetcycle;
    CHECK (step (-1, 1, 229) == 0);      // ▶ from no preset: the first (FL: 1 / 229)
    CHECK (step (-1, -1, 229) == 228);   // ◀ from no preset: the last
    CHECK (step (0, -1, 229) == 228);    // wraps (FL: "Default" 1/229 ◀ → "Waterphone" 229/229)
    CHECK (step (228, 1, 229) == 0);     // and back (▶ → 1/229)
    CHECK (step (4, 1, 229) == 5);
    CHECK (step (0, 1, 0) == -1);
    CHECK (positionText (3, 229) == "4 / 229");
}

TEST (issue24_preset_list_merges_fl_and_user_folders)
{
    using namespace lynx::presetcycle;
    const auto fl = issues167o::scratch ("fl"), user = issues167o::scratch ("user");
    for (const auto* f : { "Default.fst", "Default (pad).fst", "SFX/Waterphone.fst", "Atmospheric/300 Ft Glass Bowl.fst", "Atmospheric/readme.txt",
                           "Pad/Pad 10.fst", "Pad/Pad 2.fst" })
        issues167o::touch (fl / f);
    issues167o::touch (user / "Mine.fst");
    issues167o::touch (user / "SFX" / "Waterphone.fst", { 2, 2 }); // the user's own copy wins
    const auto list = listPresets ({ fl.string(), user.string(), (fl / "missing").string() });
    std::vector<std::string> rel;
    for (const auto& p : list)
        rel.push_back (p.relative);
    const std::vector<std::string> expected { "Default.fst", "Default (pad).fst", "Mine.fst", "Atmospheric/300 Ft Glass Bowl.fst",
                                              "Pad/Pad 2.fst", "Pad/Pad 10.fst", "SFX/Waterphone.fst" };
    CHECK (rel == expected);
    CHECK (list.back().name == "Waterphone" && std::filesystem::file_size (list.back().path) == 2);
    CHECK (indexOfName (list, "default (PAD)") == 1 && indexOfName (list, "nothing") == -1);
    lynx::FlPathRoots roots;
    roots.data = "/fl/Data";
    roots.userData = "/docs/FL Studio";
    const auto folders = pluginPresetFolders (roots, true, "Ogun");
    CHECK (folders.size() == 2 && folders[0] == "/fl/Data/Patches/Plugin presets/Generators/Ogun"
           && folders[1] == "/docs/FL Studio/Presets/Plugin presets/Generators/Ogun");
    CHECK (pluginPresetFolders (roots, false, "Fruity Reeverb 2").front() == "/fl/Data/Patches/Plugin presets/Effects/Fruity Reeverb 2");
    std::error_code ec;
    std::filesystem::remove_all (fl, ec);
    std::filesystem::remove_all (user, ec);
}

TEST (issue24_midi_out_preset_keeps_channel_and_port)
{
    using namespace lynx;
    const auto dir = issues167o::scratch ("midiout");
    midiout::State preset;
    preset.channel = 9;
    preset.port = 7;
    preset.program = 33;
    issues167o::touch (dir / "Bass.fst", midiout::writeFst (preset));
    natives::FlAccess fl;
    fl.readFile = [] (const std::string& p) {
        std::ifstream f (p, std::ios::binary);
        return std::vector<std::uint8_t> ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    };
    ChannelDef c;
    c.name = "MIDI Out";
    c.instrument = InstrumentKind::midiOut;
    c.mixerTrack = 4;
    midiout::State mine;
    mine.channel = 2;
    mine.port = 1;
    c.pluginState = midiout::write (mine);
    CHECK (natives::presetFolderOf (InstrumentKind::midiOut) == "MIDI Out");
    CHECK (natives::presetFolderOf (InstrumentKind::ogun) == "Ogun");
    CHECK (natives::presetFolderOf (InstrumentKind::sampler).empty());
    const auto loaded = natives::withPreset (c, (dir / "Bass.fst").string(), "Bass", fl);
    CHECK (loaded.has_value());
    if (loaded)
    {
        const auto s = midiout::stateOf (loaded->pluginState);
        CHECK (s.program == 33 && s.channel == 2 && s.port == 1);
        CHECK (loaded->name == "Bass" && loaded->mixerTrack == 4);
    }
    CHECK (! natives::withPreset (c, (dir / "missing.fst").string(), "x", fl).has_value());
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

TEST (issue24_fl_ogun_presets_in_fls_order)
{
    // FL's own Ogun folder (shots pn1–pn5, pq1–pq6): 229 presets, Default / Default (pad) / Default (tonal) /
    // Atmospheric's "300 Ft Glass Bowl" / "Alien Queens Lair" first, SFX's "Waterphone" last
    if (realFlpDir.empty())
        return;
    lynx::FlPathRoots roots;
    roots.data = realFlpDir;
    const auto list = lynx::presetcycle::listPresets (lynx::presetcycle::pluginPresetFolders (roots, true, "Ogun"));
    if (list.size() != 229)
        return; // (another FL version)
    CHECK (list[0].name == "Default" && list[1].name == "Default (pad)" && list[2].name == "Default (tonal)");
    CHECK (list[3].name == "300 Ft Glass Bowl" && list[4].name == "Alien Queens Lair");
    CHECK (list.back().relative == "SFX/Waterphone.fst");
    // the arrows load them into an Ogun channel, keeping the channel's own settings
    const auto fl = natives142::flAccess();
    auto channel = lynx::natives::makeChannel (lynx::InstrumentKind::ogun, fl);
    CHECK (channel.has_value());
    if (! channel)
        return;
    channel->mixerTrack = 3;
    channel->misc.pitchRange = 7;
    const auto next = lynx::natives::withPreset (*channel, list[1].path, list[1].name, fl);
    CHECK (next.has_value());
    if (next)
    {
        CHECK (next->instrument == lynx::InstrumentKind::ogun && next->name == "Default (pad)");
        CHECK (next->mixerTrack == 3 && next->misc.pitchRange == 7);
        CHECK (next->pluginState != channel->pluginState);
    }
}
