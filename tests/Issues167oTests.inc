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
    CHECK (map.find ({ f8 }, Context::global, false) == "view.pluginPicker"); // round 169k: FL's Plugin picker view
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
    // (FL's three "Default" presets are Ogun state version 12, which LYNX's Ogun engine doesn't read yet: the arrows
    // pass over them; "300 Ft Glass Bowl", the new channel's own default, and "Alien Queens Lair" are version 14)
    CHECK (! lynx::natives::withPreset (*channel, list[1].path, list[1].name, fl).has_value());
    const auto next = lynx::natives::withPreset (*channel, list[4].path, list[4].name, fl);
    CHECK (next.has_value());
    if (next)
    {
        CHECK (next->instrument == lynx::InstrumentKind::ogun && next->name == "Alien Queens Lair");
        CHECK (next->mixerTrack == 3 && next->misc.pitchRange == 7);
        CHECK (next->pluginState != channel->pluginState);
    }
}

// --- #22: SFZ instruments ---------------------------------------------------------------------------------------------

#include "core/Sfz.h"

namespace
{
namespace sfzt
{
// a mono (or stereo) 16-bit WAV of a sine at `hz`, with a smpl loop when loopEnd > loopStart
void writeSine (const std::string& path, double hz, double seconds, int channels = 1, std::int64_t loopStart = -1, std::int64_t loopEnd = -1)
{
    lynx::audioedit::Document d;
    d.audio.sampleRate = 44100.0;
    const auto n = (std::size_t) (seconds * 44100.0);
    d.audio.channels.assign ((std::size_t) channels, std::vector<float> (n));
    for (std::size_t i = 0; i < n; ++i)
        for (auto& ch : d.audio.channels)
            ch[i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * hz * (double) i / 44100.0);
    d.loopStart = loopStart, d.loopEnd = loopEnd;
    sft::writeFile (path, lynx::audioedit::encodeWav (d, lynx::audioedit::WavFormat::int16));
}

std::string writeText (const std::string& path, const std::string& text)
{
    std::ofstream out (path, std::ios::binary);
    out << text;
    return path;
}

// plays `key` for `seconds` (released at `release`), returns the left channel
std::vector<float> play (sft::Rig& rig, int key, double seconds, double release = 1e9, float velocity = 0.8f)
{
    const auto n = (int) (seconds * rig.rate), off = (int) (std::min (release, seconds) * rig.rate);
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    rig.inst->noteOn (key, velocity);
    bool released = false;
    for (int at = 0; at < n; at += 128)
    {
        if (! released && at >= off)
            rig.inst->noteOff (key), released = true;
        rig.inst->render (l.data() + at, r.data() + at, std::min (128, n - at), 1.0f, 1.0f);
    }
    if (! released)
        rig.inst->noteOff (key);
    return l;
}
} // namespace sfzt
} // namespace

TEST (issue22_sfz_parse_headers_defines_and_note_names)
{
    namespace sfz = lynx::sfz;
    CHECK (sfz::noteNumber ("c4") == 60 && sfz::noteNumber ("C#4") == 61 && sfz::noteNumber ("db4") == 61);
    CHECK (sfz::noteNumber ("a-1") == 9 && sfz::noteNumber ("64") == 64 && sfz::noteNumber ("x") == -1);

    const std::string text = R"(// a comment
#define $ROOT 57
<control> default_path=Samples/ note_offset=0
<global> ampeg_release=0.5 /* block
 comment */ volume=-6
<group> lokey=c3 hikey=b3 pitch_keycenter=$ROOT
<region> sample=a.wav
<region> sample=b.wav ampeg_release=1 hikey=c4
<group> key=72
#include "more.sfz"
<curve> v000=0
)";
    const auto parsed = sfz::parse (text, [] (const std::string& file) -> std::optional<std::string> {
        if (file == "more.sfz")
            return std::string ("<region> sample=c d.wav lovel=64");
        return std::nullopt;
    });
    CHECK (parsed.regions.size() == 3);
    if (parsed.regions.size() != 3)
        return;
    const auto& a = parsed.regions[0];
    CHECK (a.get ("sample") == "a.wav" && a.get ("lokey") == "c3" && a.get ("pitch_keycenter") == "57");
    CHECK (a.get ("ampeg_release") == "0.5" && a.get ("volume") == "-6");
    const auto& b = parsed.regions[1];
    CHECK (b.get ("ampeg_release") == "1" && b.get ("hikey") == "c4"); // the region's own value wins
    const auto& c = parsed.regions[2];
    CHECK (c.get ("sample") == "c d.wav"); // (a sample path runs to the next opcode: spaces kept)
    CHECK (c.get ("key") == "72" && c.get ("lokey").empty() && c.get ("lovel") == "64"); // a new group: the old one's opcodes gone
    CHECK (c.get ("__default_path") == "Samples/");
    CHECK (parsed.unknownHeaders == 1);
}

TEST (issue22_sfz_loads_and_plays_at_the_regions_pitch)
{
    const auto dir = sft::tempDir ("sfz-play");
    std::filesystem::create_directories (dir + "/Samples");
    sfzt::writeSine (dir + "/Samples/a440.wav", 440.0, 1.0);
    sfzt::writeSine (dir + "/Samples/stereo.wav", 220.0, 1.0, 2);
    const auto path = sfzt::writeText (dir + "/Test Piano.sfz", R"(<control> default_path=Samples/
<region> sample=a440.wav lokey=60 hikey=80 pitch_keycenter=a4
<region> sample=stereo.wav lokey=81 hikey=90 pitch_keycenter=57 loop_mode=one_shot
<region> sample=missing.wav key=100
)");
    lynx::sfz::Report report;
    std::string error;
    const auto font = lynx::sfz::loadFile (path, &error, &report);
    CHECK (font != nullptr);
    if (font == nullptr)
        return;
    CHECK (font->presets.size() == 1 && font->presets[0].name == "Test Piano");
    CHECK (report.regions == 3 && report.samplesLoaded == 2 && report.samplesMissing == 1); // (stereo: two regions)
    CHECK (font->presets[0].regions.size() == 3);

    // the browser's quick scan: valid, one preset named after the file
    const auto info = lynx::sf::scanFontFile (path);
    CHECK (info.valid && info.presets.size() == 1 && info.presets[0].name == "Test Piano");
    CHECK (! lynx::sf::scanFontFile (sfzt::writeText (dir + "/empty.sfz", "<region> lokey=1")).valid);

    sft::Rig rig (path);
    const auto up = sfzt::play (rig, 69, 0.5); // key 69 = the root: 440 Hz
    CHECK (std::abs (sft::frequency (up, rig.rate) - 440.0) < 6.0);
    const auto octave = sfzt::play (rig, 80, 0.5); // 11 semitones up
    CHECK (std::abs (sft::frequency (octave, rig.rate) - 440.0 * std::pow (2.0, 11.0 / 12.0)) < 10.0);
    const auto stereo = sfzt::play (rig, 81, 0.2); // root 57 → 2 octaves up: 880 Hz (the 1 s file lasts 0.25 s there)
    CHECK (std::abs (sft::frequency (stereo, rig.rate) - 880.0) < 12.0);
    rig.run (1.0); // (the one-shot above plays to its end)
    CHECK (sft::peak (sfzt::play (rig, 100, 0.2)) < 1e-6f); // its sample is missing
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

TEST (issue22_sfz_round_robin_random_and_key_switches)
{
    const auto dir = sft::tempDir ("sfz-rr");
    sfzt::writeSine (dir + "/low.wav", 220.0, 1.0);
    sfzt::writeSine (dir + "/high.wav", 660.0, 1.0);
    const auto path = sfzt::writeText (dir + "/rr.sfz", R"(<group> key=60 pitch_keycenter=60 seq_length=2
<region> sample=low.wav seq_position=1
<region> sample=high.wav seq_position=2
<global> sw_lokey=24 sw_hikey=25 sw_default=24
<region> sample=low.wav key=62 pitch_keycenter=62 sw_last=24
<region> sample=high.wav key=62 pitch_keycenter=62 sw_last=25
<region> sample=low.wav key=64 pitch_keycenter=64 lorand=0 hirand=0.5
<region> sample=high.wav key=64 pitch_keycenter=64 lorand=0.5 hirand=1
)");
    sft::Rig rig (path);
    auto freq = [&] (int key) { return sft::frequency (sfzt::play (rig, key, 0.3, 0.25), rig.rate); };
    // round robin: low, high, low
    CHECK (std::abs (freq (60) - 220.0) < 5.0);
    CHECK (std::abs (freq (60) - 660.0) < 12.0);
    CHECK (std::abs (freq (60) - 220.0) < 5.0);
    // key switches: the default (24) first, then the last switch pressed
    CHECK (std::abs (freq (62) - 220.0) < 5.0);
    sfzt::play (rig, 25, 0.05, 0.02);
    CHECK (std::abs (freq (62) - 660.0) < 12.0);
    sfzt::play (rig, 24, 0.05, 0.02);
    CHECK (std::abs (freq (62) - 220.0) < 5.0);
    // random: exactly one of the two plays each time, both are heard over many notes
    int low = 0, high = 0;
    for (int i = 0; i < 24; ++i)
    {
        const auto f = freq (64);
        low += std::abs (f - 220.0) < 5.0;
        high += std::abs (f - 660.0) < 12.0;
    }
    CHECK (low + high == 24 && low > 0 && high > 0);
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}

TEST (issue22_sfz_release_trigger_choke_groups_and_loops)
{
    const auto dir = sft::tempDir ("sfz-trig");
    sfzt::writeSine (dir + "/short.wav", 440.0, 0.2, 1, 2205, 4410); // a smpl loop over 50 ms
    sfzt::writeSine (dir + "/tone.wav", 330.0, 1.0);
    const auto path = sfzt::writeText (dir + "/trig.sfz", R"(<region> sample=tone.wav key=60 pitch_keycenter=60 ampeg_release=0.01
<region> sample=tone.wav key=60 pitch_keycenter=60 trigger=release
<region> sample=short.wav key=62 pitch_keycenter=69
<region> sample=short.wav key=63 pitch_keycenter=69 loop_mode=no_loop
<region> sample=tone.wav key=42 pitch_keycenter=42 group=1 off_by=2
<region> sample=tone.wav key=46 pitch_keycenter=46 group=2
)");
    sft::Rig rig (path);
    // trigger=release: after the note-off a second region sounds (the first one's release is 10 ms)
    const auto rel = sfzt::play (rig, 60, 0.6, 0.2);
    CHECK (sft::peak (rel, (std::size_t) (0.35 * rig.rate), (std::size_t) (0.5 * rig.rate)) > 0.1f);
    // the sample's smpl loop: still sounding after the 0.2 s file while held; without a loop it ends
    const auto looped = sfzt::play (rig, 62, 0.6);
    CHECK (sft::peak (looped, (std::size_t) (0.4 * rig.rate)) > 0.1f);
    const auto once = sfzt::play (rig, 63, 0.6);
    CHECK (sft::peak (once, (std::size_t) (0.4 * rig.rate)) < 1e-4f);
    rig.run (1.0);
    // off_by: the open hi-hat (group 1) is choked by the closed one (group 2)
    rig.inst->noteOn (42, 0.8f);
    rig.run (0.05);
    CHECK (lynx::sf::activeVoices (*rig.inst) >= 1);
    rig.inst->noteOn (46, 0.8f);
    rig.run (0.1);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->noteOff (42);
    rig.inst->noteOff (46);
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
}
