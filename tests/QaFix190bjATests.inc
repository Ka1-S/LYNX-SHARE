// Round 190bj: fixes for QA 190bh (/mnt/e/lynx-work/qa190bh/BUGS.md) in round 190bf's areas: B3 (Disk recording format
// one take late), B4 ("Open" on LYNX SHARE), B8 (stage and preview rows), B9 (the held row in the key help).
// Needs PluginDb190baTests.inc (plugindb190ba helpers) before it.

#include "core/FruityDance.h"
#include "core/PluginDbFolder.h"
#include "core/TakeRecording.h"

// B3: a take's file made before its first sample (the next take, made when the previous one ended) takes a new format
TEST (qafix190bj_take_format_changes_before_the_first_sample)
{
    using namespace lynx::takes;
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("lynx-qafix190bj-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories (dir);
    std::vector<float> left (1000, 0.25f), right (1000, -0.5f);
    const float* planar[] { left.data(), right.data() };
    const auto bitsOf = [] (const std::string& file) {
        std::ifstream in (file, std::ios::binary);
        unsigned char h[44] {};
        in.read ((char*) h, 44);
        return std::pair { (int) (h[20] | h[21] << 8), (int) (h[34] | h[35] << 8) };
    };
    // the user's sequence: 16 -> 24 -> 32f, each change made while the next take's file is already open (QA: 16, 16, 24)
    const std::pair<SampleFormat, int> steps[] { { SampleFormat::pcm16, 16 }, { SampleFormat::pcm24, 24 }, { SampleFormat::float32, 32 } };
    auto open = SampleFormat::pcm16; // (the format of the moment the file was made)
    for (int k = 0; k < 3; ++k)
    {
        const auto file = (dir / ("take" + std::to_string (k) + ".wav")).string();
        WavTakeWriter w;
        CHECK (w.open (file, 48000.0, 2, open));
        CHECK (fs::file_size (file) == 44);
        CHECK (w.changeFormat (steps[k].first) && w.sampleFormat() == steps[k].first); // (the menu, before Play)
        CHECK (w.write (planar, 1000) && w.close());
        const auto [tag, bits] = bitsOf (file);
        CHECK (bits == steps[k].second && tag == (steps[k].first == SampleFormat::float32 ? 3 : 1));
        CHECK ((std::int64_t) fs::file_size (file) == 44 + 1000 * 2 * bits / 8);
        std::vector<std::vector<float>> back;
        CHECK (lynx::takes::readWav (file, back) && back.size() == 2 && back[0].size() == 1000 && std::abs (back[1][500] + 0.5f) < 1.0e-3f);
        open = steps[k].first;
    }
    // once samples are in, the format stays (a take is never mixed)
    {
        const auto file = (dir / "mixed.wav").string();
        WavTakeWriter w;
        CHECK (w.open (file, 48000.0, 1, SampleFormat::pcm24) && w.write (planar, 10));
        CHECK (! w.changeFormat (SampleFormat::pcm16) && w.sampleFormat() == SampleFormat::pcm24);
        CHECK (w.changeFormat (SampleFormat::pcm24)); // (the same format: nothing to change)
        w.close();
        CHECK (bitsOf (file).second == 24);
        WavTakeWriter closed;
        CHECK (! closed.changeFormat (SampleFormat::pcm16));
    }
    std::error_code ec;
    fs::remove_all (dir, ec);
}

// B4: "Open" on the Browser's own groups opens their folder: LYNX SHARE = Effects/LYNX SHARE (not Installed/Effects)
TEST (qafix190bj_group_folders)
{
    using namespace plugindb190ba;
    TempRoot t;
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, plugins(), state);
    auto same = [] (const std::string& a, const fs::path& b) { return fs::path ((const char8_t*) a.c_str()) == b; };
    CHECK (same (lynx::plugindb::groupFolder (t.root, false, "LYNX"), t.fx ("LYNX SHARE")));
    CHECK (same (lynx::plugindb::groupFolder (t.root, false, "FL engines"), u8 (t.root) / "Installed/Effects/FL engines"));
    CHECK (same (lynx::plugindb::groupFolder (t.root, false, ""), u8 (t.root) / "Installed/Effects"));
    CHECK (same (lynx::plugindb::groupFolder (t.root, true, "FL engines"), u8 (t.root) / "Installed/Generators/FL engines"));
    // no LYNX SHARE folder of generators here (Sytrus is FL's): their Installed entries; a format folder that is not there:
    // Installed of the kind
    CHECK (same (lynx::plugindb::groupFolder (t.root, true, "LYNX"), u8 (t.root) / "Installed/Generators"));
    CHECK (same (lynx::plugindb::groupFolder (t.root, true, "VFX"), u8 (t.root) / "Installed/Generators"));
    // the user moved every LYNX SHARE effect away and removed the folder: the Installed LYNX entries
    fs::rename (t.fx ("LYNX SHARE/Facet EQ.lynxplugin"), t.fx ("Facet EQ.lynxplugin"));
    fs::remove (t.fx ("LYNX SHARE"));
    CHECK (same (lynx::plugindb::groupFolder (t.root, false, "LYNX"), u8 (t.root) / "Installed/Effects/LYNX"));
}

// B8: the preview and the stage draw the same frame (FL: the first row while the song is stopped, the move's row while
// it plays); B9: the key help names every move and says the last row is the held one
TEST (qafix190bj_dance_preview_stage_and_keys)
{
    using namespace lynx::dance;
    const std::vector<std::string> names { "Bounce", "Sway", "Spin" }; // QA's 3-row sheet
    Settings s;
    s.move = 2; // "Sway" (Freestyle, Bounce, Sway)
    CHECK (sheetMoveNames (names) == (std::vector<std::string> { "Freestyle", "Bounce", "Sway" }));
    SheetAnimator preview (11), stage (11);
    for (const bool playing : { false, true })
        for (double c = 0.05; c < 3.0; c += 0.25)
        {
            const auto a = danceFrame (preview, c, 3, s, playing), b = danceFrame (stage, c, 3, s, playing);
            CHECK (a == b);
            CHECK (a.row == (playing ? 1 : 0)); // (stopped: the first row; playing: Sway's row)
        }
    {
        SheetAnimator a (3);
        CHECK (danceFrame (a, 0.1, 3, s, true).row == 1 && danceFrame (a, 1.05, 3, s, false).row == 0);
    }
    const auto keys = sheetKeys (names);
    CHECK (keys.size() == 3 && keys[0].key == 59 && keys[0].name == "Freestyle" && keys[1].key == 60 && keys[1].name == "Bounce"
           && keys[2].key == 61 && keys[2].name == "Sway");
    CHECK (sheetHeldName (names) == "Spin" && sheetHeldName ({}).empty());
    // a long sheet stops at key 127
    std::vector<std::string> many (100, "m");
    CHECK (sheetKeys (many).size() == 69 && sheetKeys (many).back().key == 127);
}
