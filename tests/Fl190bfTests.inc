// Round 190bf: FL Studio as seen live in round 190be (/mnt/e/lynx-work/fl190be/ANSWERS.md) for issues #104 and #121:
// the Plugin database entry's file (the Browser's System items), the folder watcher, FL's root entries in the plugin
// menus, Fruity Dance's sheet path / display options with FL's own bytes, and the Disk recording formats.
// Needs PluginDb190baTests.inc (plugindb190ba helpers) before it.

#include "core/FolderWatcher.h"
#include "core/FruityDance.h"
#include "core/PluginCatalog.h"
#include "core/PluginDbFolder.h"
#include "core/TakeRecording.h"

namespace fl190bf
{
namespace fs = std::filesystem;
inline std::vector<std::uint8_t> hex (const char* text)
{
    std::vector<std::uint8_t> out;
    for (const char* p = text; p[0] != 0 && p[1] != 0; p += 2)
    {
        auto nibble = [] (char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        out.push_back ((std::uint8_t) (nibble (p[0]) * 16 + nibble (p[1])));
    }
    return out;
}
// FL Studio 2026's own Fruity Dance states (event 0xD5 of fl190be/dance.flp and dance2.flp, saved by FL 2026-11-11)
// dance.flp: FL's shipped Count.png inside FL's folder (119 bytes)
inline const char* const flCount = "0200000002000000000000000100000040000000000000009c010000f401000040000000010101014e"
                                   "25464c53747564696f466163746f727944617461255c506c7567696e735c4672756974795c47656e657261746f72735c"
                                   "4672756974792044616e63655c417274776f726b5c436f756e742e706e67";
// dance2.flp: a copy at E:\lynx-work\fl190be\sheet\Dançé.png (79 bytes; 36 characters, 38 bytes)
inline const char* const flOutside = "0200000002000000000000000100000040000000000000009c010000f4010000400000000101010126"
                                     "453a5c6c796e782d776f726b5c666c31393062655c73686565745c44616ec3a7c3a92e706e67";
inline const std::string flRoot = "C:\\Program Files\\Image-Line\\FL Studio 2026";

// waits up to `ms` for the watcher to report (polls a counter, sleeping)
inline bool waitForReports (const lynx::folderwatch::Watcher& w, int atLeast, int ms)
{
    for (int t = 0; t < ms; t += 20)
    {
        if (w.reports() >= atLeast)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
    }
    return w.reports() >= atLeast;
}
} // namespace fl190bf

// A: FL's "System" items on an entry act on its file: the one under Effects / Generators, else its Installed entry
TEST (fl190bf_entry_file_of_a_plugin)
{
    using namespace plugindb190ba;
    TempRoot t;
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, plugins(), state);
    const auto maximus = lynx::plugindb::entryFile (t.root, "lynx:fx:Maximus", false);
    CHECK (! maximus.empty() && fs::path ((const char8_t*) maximus.c_str()) == t.fx ("Dynamics/Maximus.lynxplugin"));
    // a scanned plugin in no folder: its Installed entry
    const auto vital = lynx::plugindb::entryFile (t.root, "CLAP-synth-1", true);
    CHECK (! vital.empty() && fs::path ((const char8_t*) vital.c_str()) == u8 (t.root) / "Installed/Generators/CLAP/Vital.lynxplugin");
    // the kind counts; an unknown key has none
    CHECK (lynx::plugindb::entryFile (t.root, "lynx:fx:Maximus", true).empty());
    CHECK (lynx::plugindb::entryFile (t.root, "no such plugin", false).empty() && lynx::plugindb::entryFile ({}, "lynx:fx:Maximus", false).empty());
    // moved in the file manager: found at its new place
    fs::create_directories (t.fx ("Mine"));
    fs::rename (t.fx ("Dynamics/Maximus.lynxplugin"), t.fx ("Mine/M.lynxplugin"));
    CHECK (fs::path ((const char8_t*) lynx::plugindb::entryFile (t.root, "lynx:fx:Maximus", false).c_str()) == t.fx ("Mine/M.lynxplugin"));
    // deleted (FL's "Delete file..."): the plugin leaves the menus' folders at the next read, and is still installed
    fs::remove (t.fx ("Mine/M.lynxplugin"));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.hidden.count ("lynx:fx:Maximus") == 1);
    CHECK (fs::exists (u8 (t.root) / "Installed/Effects/FL engines/Maximus.lynxplugin"));
}

// B: the watcher reports a change made on disk once, about a second later at most, and nothing while all is quiet
TEST (fl190bf_folder_watcher)
{
    using namespace plugindb190ba;
    for (const bool poll : { false, true })
    {
        TempRoot t;
        fs::create_directories (t.fx ("Gain"));
        fs::create_directories (t.gen ("Synth"));
        std::atomic<int> calls { 0 };
        lynx::folderwatch::Options o;
        o.forcePoll = poll;
        o.quietMs = 300;
        o.pollMs = 250;
        lynx::folderwatch::Watcher w ({ plugindb190ba::str (t.fx ("")), plugindb190ba::str (t.gen ("")) }, [&calls] { calls.fetch_add (1); }, o);
        if (poll)
            CHECK (w.method() == "poll");
#if defined(__linux__)
        else
            CHECK (w.method() == "inotify");
#endif
        std::this_thread::sleep_for (std::chrono::milliseconds (400));
        CHECK (w.reports() == 0); // (nothing changed: nothing reported)

        // a folder and an entry made in a subfolder (FL's Q2: "Effects\LX190BE test\LX190BE Balance.fst")
        const auto started = std::chrono::steady_clock::now();
        fs::create_directories (t.fx ("Gain/LX test"));
        { std::ofstream (t.fx ("Gain/LX test/Balance.lynxplugin")) << "LYNX plugin 1\nkey=k\n"; }
        CHECK (fl190bf::waitForReports (w, 1, 3000));
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now() - started).count();
        CHECK (took < 2000);
        // several changes close together: one report (the debounce)
        std::this_thread::sleep_for (std::chrono::milliseconds (700));
        const auto before = w.reports();
        for (int k = 0; k < 3; ++k)
        {
            { std::ofstream (t.gen ("Synth/S" + std::to_string (k) + ".lynxplugin")) << "x"; }
            std::this_thread::sleep_for (std::chrono::milliseconds (60));
        }
        CHECK (fl190bf::waitForReports (w, before + 1, 3000));
        std::this_thread::sleep_for (std::chrono::milliseconds (900));
        CHECK (w.reports() == before + 1);
        // a delete and a rename in a folder made after the watch began (inotify watches it too)
        const auto mid = w.reports();
        fs::rename (t.fx ("Gain/LX test/Balance.lynxplugin"), t.fx ("Gain/LX test/B2.lynxplugin"));
        CHECK (fl190bf::waitForReports (w, mid + 1, 3000));
        std::this_thread::sleep_for (std::chrono::milliseconds (50)); // (the count goes up just before the call)
        CHECK (calls.load() == w.reports());
    }
    // the poll's view changes with a folder made deep in the tree, not when nothing happens
    {
        TempRoot t;
        fs::create_directories (t.fx ("A/B"));
        const auto a = lynx::folderwatch::treeStamp (plugindb190ba::str (t.fx ("")));
        CHECK (! a.empty() && a == lynx::folderwatch::treeStamp (plugindb190ba::str (t.fx (""))));
        fs::create_directories (t.fx ("A/B/C"));
        CHECK (a != lynx::folderwatch::treeStamp (plugindb190ba::str (t.fx (""))));
        CHECK (lynx::folderwatch::treeStamp (plugindb190ba::str (t.fx ("missing"))).empty());
    }
}

// C: FL's three views of a plugin list: root entries (in no folder) first in Categories, after the folders in Tree, A-Z
// with the rest in Simple; empty folders are left out
TEST (fl190bf_root_entries_and_menu_views)
{
    using namespace plugindb190ba;
    namespace pc = lynx::plugincatalog;
    TempRoot t;
    lynx::plugindb::State state;
    lynx::plugindb::refresh (t.root, plugins(), state);
    // in the file manager: Maximus to the top of Effects, the VST3 Pro-Q copied there from Installed, an empty folder
    fs::rename (t.fx ("Dynamics/Maximus.lynxplugin"), t.fx ("Maximus.lynxplugin"));
    fs::copy_file (u8 (t.root) / "Installed/Effects/VST3/Pro-Q 3.lynxplugin", t.fx ("Pro-Q 3.lynxplugin"));
    fs::create_directories (t.fx ("Empty"));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.top == (std::set<std::string> { "lynx:fx:Maximus", "VST3-ProQ-1234" }));
    CHECK (! state.folders.of.count ("lynx:fx:Maximus") && ! state.folders.of.count ("VST3-ProQ-1234"));

    const std::vector<std::string> keys { "lynx:fx:Fruity Limiter", "lynx:fx:Maximus", "lynx:fx:Facet EQ", "VST3-ProQ-1234", "VST-ProQ-99" };
    const std::vector<pc::BuiltinPlugin> builtins { { "lynx:fx:Fruity Limiter", "Fruity Limiter", false, true }, { "lynx:fx:Maximus", "Maximus", false, true },
                                                    { "lynx:fx:Facet EQ", "Facet EQ", false, false } };
    const auto folders = pc::withDefaultCategories (state.folders, false, builtins);
    // the root entry gets no default folder
    CHECK (! folders.of.count ("lynx:fx:Maximus"));
    CHECK (pc::rootItems (folders, false, keys) == (std::vector<int> { 1, 3 }));
    CHECK (pc::rootItems (folders, true, keys) == (std::vector<int> { 1, 3 })); // (the keys say the kind; a menu passes its own)
    const auto tree = pc::menuTree (folders, false, keys);
    std::vector<std::string> names;
    for (const auto& f : tree)
        names.push_back (f.name);
    CHECK (names == (std::vector<std::string> { "LYNX SHARE", "Dynamics" })); // "Empty" is left out (FL: hidden in menus)
    CHECK (state.folders.effects.count ("Empty") == 1); // (the Browser still has it)
    // Simple: every plugin once
    auto all = pc::allItems (tree);
    const auto root = pc::rootItems (folders, false, keys);
    all.insert (all.end(), root.begin(), root.end());
    std::sort (all.begin(), all.end());
    CHECK (all == (std::vector<int> { 0, 1, 2, 3 })); // (the unfiled VST2 Pro-Q is in no list: FL lists database entries)

    // a choice made in LYNX ("Move to folder") takes it out of the top, and the file follows
    pc::moveToFolder (state.folders, false, "lynx:fx:Maximus", "Mine");
    CHECK (! state.folders.top.count ("lynx:fx:Maximus"));
    lynx::plugindb::apply (t.root, plugins(), state);
    CHECK (fs::exists (t.fx ("Mine/Maximus.lynxplugin")) && ! fs::exists (t.fx ("Maximus.lynxplugin")));
    // a root entry LYNX writes nothing to stays where it is
    CHECK (fs::exists (t.fx ("Pro-Q 3.lynxplugin")));
    lynx::plugindb::refresh (t.root, plugins(), state);
    CHECK (state.folders.top == (std::set<std::string> { "VST3-ProQ-1234" }) && state.folders.of.at ("lynx:fx:Maximus") == "Mine");
}

// D: Fruity Dance's sheet path as FL 2026 saves it, with FL's own bytes
TEST (fl190bf_dance_sheet_path_fl_bytes)
{
    using namespace lynx::dance;
    const auto count = fl190bf::hex (fl190bf::flCount), outside = fl190bf::hex (fl190bf::flOutside);
    CHECK (count.size() == 119 && outside.size() == 79);
    // inside FL's folder: the token, expanded with FL's folder
    const auto a = fromFl (count, fl190bf::flRoot);
    CHECK (a && a->sheet == fl190bf::flRoot + "\\Plugins\\Fruity\\Generators\\Fruity Dance\\Artwork\\Count.png");
    CHECK (a->move == 2 && ! a->mirrored && a->visible && std::abs (a->blend - 0.25f) < 1.0e-6f && a->x == 412 && a->y == 500);
    CHECK (std::abs (a->reflect - 0.25f) < 1.0e-6f && a->sync && a->keepInFront && a->onDesktop && a->solid);
    // without FL: the path stays as FL wrote it (a file that cannot be found: LYNX's dancer stands in)
    CHECK (fromFl (count)->sheet == "%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Fruity Dance\\Artwork\\Count.png");
    // FL's bytes again, exactly
    CHECK (toFl (*a, fl190bf::flRoot) == count);
    // a POSIX root (macOS, Linux, a Wine prefix): '/' in LYNX, FL's form back in the export
    const std::string posixRoot = "/mnt/c/Program Files/Image-Line/FL Studio 2026/";
    const auto p = fromFl (count, posixRoot);
    CHECK (p && p->sheet == "/mnt/c/Program Files/Image-Line/FL Studio 2026/Plugins/Fruity/Generators/Fruity Dance/Artwork/Count.png");
    CHECK (toFl (*p, posixRoot) == count);
    // outside FL's folder: the full path, UTF-8, the length in bytes (36 characters, 38 bytes)
    const auto b = fromFl (outside, fl190bf::flRoot);
    CHECK (b && b->sheet == "E:\\lynx-work\\fl190be\\sheet\\Dan\xc3\xa7\xc3\xa9.png" && b->sheet.size() == 38);
    CHECK (toFl (*b, fl190bf::flRoot) == outside && toFl (*b) == outside);
    // never relative, never the token for a file outside the root (also one whose name only starts like it)
    auto c = *b;
    c.sheet = fl190bf::flRoot + " (old)\\Count.png";
    CHECK (flSheetPath (c.sheet, fl190bf::flRoot) == c.sheet);
    CHECK (flSheetPath ("c:/program files/image-line/FL STUDIO 2026//Plugins/x.png", fl190bf::flRoot) == "%FLStudioFactoryData%\\Plugins\\x.png");
    CHECK (expandFlSheetPath ("%flstudiofactorydata%\\a\\b.png", "D:\\FL\\") == "D:\\FL\\a\\b.png");
    CHECK (expandFlSheetPath ("D:\\x.png", fl190bf::flRoot) == "D:\\x.png");
    // the length escape (255, then an int) still holds a token path
    c.sheet = fl190bf::flRoot + "\\" + std::string (300, 'z') + ".png";
    const auto longOne = toFl (c, fl190bf::flRoot);
    CHECK (longOne[40] == 0xff && fromFl (longOne, fl190bf::flRoot)->sheet == c.sheet);
    CHECK (fromFl (longOne)->sheet.rfind ("%FLStudioFactoryData%\\zzz", 0) == 0);
}

// E: FL's display options: in FL's fields, in LYNX's own state, and "Sync changes" off in the animation
TEST (fl190bf_dance_display_options)
{
    using namespace lynx::dance;
    Settings s;
    CHECK (s.sync && s.solid && s.keepInFront && s.onDesktop && std::abs (s.reflect - 0.25f) < 1.0e-6f); // (FL's new Fruity Dance)
    // a dancer made before keeps its state text (nothing written while the options are FL's defaults)
    const auto plain = toState (s);
    const std::string text (plain.begin(), plain.end());
    CHECK (text.find ("reflect") == std::string::npos && text.find ("sync") == std::string::npos && text.find ("front") == std::string::npos);
    s.sync = false;
    s.solid = false;
    s.keepInFront = false;
    s.onDesktop = false;
    s.reflect = 200.0f / 256.0f;
    CHECK (read (toState (s)) == s);
    const auto fl = toFl (s);
    CHECK (fl.size() == 41 && fl[32] == 200 && fl[33] == 0 && fl[36] == 0 && fl[37] == 0 && fl[38] == 0 && fl[39] == 0);
    CHECK (fromFl (fl) && *fromFl (fl) == s);
    // one at a time, in FL's order of the four bytes (Sync changes, Keep in front, On desktop, Solid)
    Settings one;
    one.keepInFront = false;
    const auto b = toFl (one);
    CHECK (b[36] == 1 && b[37] == 0 && b[38] == 1 && b[39] == 1 && *fromFl (b) == one);
    one = {};
    one.solid = false;
    CHECK (toFl (one)[39] == 0 && fromFl (toFl (one))->solid == false && fromFl (toFl (one))->sync);
    // Sync changes off: a move other than Freestyle shows at once; on: at the next cycle's start
    {
        SheetAnimator on (7), off (7);
        CHECK (on.advance (0.1, 10, 3, false, true, 0.25f).row == 2 && off.advance (0.1, 10, 3, false, true, 0.25f, false, false).row == 2);
        CHECK (on.advance (0.4, 10, 6, false, true, 0.25f).row == 2);
        CHECK (off.advance (0.4, 10, 6, true, true, 0.25f, false, false).row == 5);
        CHECK (off.advance (0.45, 10, 6, true, true, 0.25f, false, false).mirrored);
        // Freestyle still waits for the cycle (FL)
        const auto before = off.advance (0.5, 10, 6, false, true, 0.25f, false, false).row;
        CHECK (off.advance (0.6, 10, 0, false, true, 0.25f, false, false).row == before);
    }
}

// F: Mixer › Disk recording › Format: 16Bit int, 24Bit int, 32Bit float
TEST (fl190bf_disk_recording_formats)
{
    using namespace lynx::takes;
    namespace fs = std::filesystem;
    CHECK (bytesPerSample (SampleFormat::pcm16) == 2 && bytesPerSample (SampleFormat::pcm24) == 3 && bytesPerSample (SampleFormat::float32) == 4);
    const auto dir = fs::temp_directory_path() / ("lynx-fl190bf-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories (dir);
    std::vector<float> left (4800), right (4800);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = 0.8f * (float) std::sin ((double) i * 0.05);
        right[i] = -0.5f * (float) std::sin ((double) i * 0.031);
    }
    left[10] = 1.5f; // (clipped)
    const float* planar[] { left.data(), right.data() };
    for (const auto [format, bits, tolerance] : { std::tuple { SampleFormat::pcm16, 16, 1.0f / 32767.0f }, std::tuple { SampleFormat::pcm24, 24, 1.0f / 8388607.0f },
                                                  std::tuple { SampleFormat::float32, 32, 0.0f } })
    {
        const auto file = (dir / ("take" + std::to_string (bits) + ".wav")).string();
        WavTakeWriter w;
        CHECK (w.open (file, 48000.0, 2, format));
        CHECK (w.write (planar, 4800) && w.close());
        CHECK ((std::int64_t) fs::file_size (file) == 44 + 4800 * 2 * bits / 8);
        std::ifstream in (file, std::ios::binary);
        unsigned char h[44] {};
        in.read ((char*) h, 44);
        CHECK ((h[20] | h[21] << 8) == (format == SampleFormat::float32 ? 3 : 1) && (h[34] | h[35] << 8) == bits && (h[32] | h[33] << 8) == 2 * bits / 8);
        std::vector<std::vector<float>> back;
        double rate = 0.0;
        CHECK (readWav (file, back, &rate) && rate == 48000.0 && back.size() == 2 && back[0].size() == 4800);
        float worst = 0.0f;
        for (std::size_t i = 0; i < 4800; ++i)
            if (i != 10)
                worst = std::max ({ worst, std::abs (back[0][i] - left[i]), std::abs (back[1][i] - right[i]) });
        CHECK (worst <= tolerance * 0.5f + 1.0e-7f);
        CHECK (format == SampleFormat::float32 ? back[0][10] == 1.5f : std::abs (back[0][10] - 1.0f) < 1.0e-4f);
    }
    // a 16-bit take uses two thirds of a 24-bit one's room
    CHECK (std::abs (minutesLeft (1000000000, 48000.0, 2, SampleFormat::pcm16) / minutesLeft (1000000000, 48000.0, 2, SampleFormat::pcm24) - 1.5) < 1.0e-9);
    std::error_code ec;
    fs::remove_all (dir, ec);
}
