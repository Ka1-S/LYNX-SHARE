// Round 190pj (issue #241, docs/design/JSFX.md stages 1–2): JSFX scripts' identity, file policy helpers and saved state
// (core/Jsfx.h), the bundled pack embedded in the executable, and the ysfx runtime itself against upstream ysfx's
// reference render of a bundled script (src/engine/JsfxReference.h, tools/jsfx_ref.cpp). The format end to end (slots,
// MIDI, PDC, the file policy at run time) is in the app's --selftest (engine/JsfxFormat.cpp, jsfx::selfTest).

#include <ysfx.h>

#include "core/Inflate.h"
#include "core/Jsfx.h"
#include "engine/JsfxPackData.h"
#include "engine/JsfxReference.h"

TEST (jsfx190pj_paths_and_ids)
{
    using namespace lynx::jsfx;
    CHECK (normalise ("C:\\Users\\me\\JSFX\\..\\JSFX\\a.jsfx") == "C:/Users/me/JSFX/a.jsfx");
    CHECK (normalise ("/home/me//JSFX/./Utility/") == "/home/me/JSFX/Utility");
    CHECK (normalise ("//server/share/x") == "//server/share/x");
    CHECK (isInside ("/home/me/JSFX/Utility/v.jsfx", "/home/me/JSFX", false));
    CHECK (! isInside ("/home/me/JSFXother/v.jsfx", "/home/me/JSFX", false));
    CHECK (! isInside ("/home/me/JSFX/../secret.txt", "/home/me/JSFX", false)); // (the ".." is resolved first)
    CHECK (isInside ("c:/users/ME/jsfx/a.jsfx", "C:\\Users\\me\\JSFX", true));
    CHECK (! isInside ("c:/users/ME/jsfx/a.jsfx", "C:\\Users\\me\\JSFX", false));
    const std::vector<std::string> roots { "/home/me/Documents/LYNX/JSFX", "/home/me/.config/LYNX/JSFX pack", "/home/me/.config/REAPER/Effects",
                                           "/home/me/.config/REAPER/Effects/Saike" };
    auto at = locate ("/home/me/.config/LYNX/JSFX pack/chokehold/Clipper/soft_clipper.jsfx", roots, false);
    CHECK (at.root == 1 && at.relative == "chokehold/Clipper/soft_clipper.jsfx");
    at = locate ("/home/me/.config/REAPER/Effects/Saike/Tight_Compressor.jsfx", roots, false); // (the deepest root)
    CHECK (at.root == 3 && at.relative == "Tight_Compressor.jsfx");
    CHECK (locate ("/tmp/x.jsfx", roots, false).root == -1);
    // the id is the relative path's: the same on every computer, case-folded, and another one for the generator listing
    CHECK (uniqueIdOf ("Utility/volume.jsfx", false) == uniqueIdOf ("utility\\VOLUME.jsfx", false));
    CHECK (uniqueIdOf ("Utility/volume.jsfx", false) != uniqueIdOf ("Utility/volume.jsfx", true));
    CHECK (uniqueIdOf ("Utility/volume.jsfx", false) != uniqueIdOf ("Utility/volume2.jsfx", false));
    CHECK (sourceHash ("abc") != sourceHash ("abd") && sourceHash ("") == 14695981039346656037ull);
}

TEST (jsfx190pj_headers_and_roles)
{
    using namespace lynx::jsfx;
    CHECK (looksLikeJsfx ("desc:Volume\nslider1:v=0<-12,12>Gain\n"));
    CHECK (looksLikeJsfx ("// This effect Copyright (C) 2004 and later Cockos Incorporated\n// License: LGPL\n\ndesc: 3-Band EQ\n"));
    CHECK (looksLikeJsfx ("\xef\xbb\xbf" "desc:BOM first\n"));
    CHECK (looksLikeJsfx ("version: 1.0\nauthor: me\ndesc:Late desc\n"));
    CHECK (! looksLikeJsfx ("hello world\ndesc:not a header\n"));
    CHECK (! looksLikeJsfx ("@init\ndesc:\n"));
    CHECK (! looksLikeJsfx (""));
    CHECK (listAsGenerator (0, {}, "x.jsfx"));
    CHECK (listAsGenerator (2, { "utility", "synth" }, "x.jsfx"));
    CHECK (listAsGenerator (2, { "MIDI" }, "x.jsfx"));
    CHECK (listAsGenerator (2, {}, "midi/midi_transpose"));
    CHECK (! listAsGenerator (2, { "processing", "gain" }, "Utility/volume.jsfx"));
    CHECK (listAsEffect (2, 2) && listAsEffect (0, 0) && ! listAsEffect (0, 2));
}

TEST (jsfx190pj_saved_state)
{
    using namespace lynx::jsfx;
    SavedState s;
    s.relative = "chokehold/Clipper/soft_clipper.jsfx";
    s.source = 0x0123456789abcdefull;
    s.sliders = { { 0, -3.5 }, { 1, 12.0 }, { 255, 1.0e-300 } };
    s.serialized = std::string ("\x00\x01\x02\xff", 4);
    const auto bytes = encodeState (s);
    SavedState back;
    CHECK (decodeState (bytes.data(), bytes.size(), back));
    CHECK (back.relative == s.relative && back.source == s.source && back.sliders == s.sliders && back.serialized == s.serialized);
    // not ours / cut short: refused, the target left as it was
    SavedState untouched;
    untouched.relative = "keep";
    CHECK (! decodeState ("VC2!xxxxxxxx", 12, untouched) && untouched.relative == "keep");
    for (std::size_t cut = 0; cut < bytes.size(); ++cut)
        CHECK (! decodeState (bytes.data(), cut, untouched));
    CHECK (untouched.relative == "keep");
    // a slider count that cannot fit is refused before anything is reserved
    auto big = bytes;
    const auto countAt = 4 + 4 + 4 + s.relative.size() + 8;
    big[countAt + 3] = (char) 0x7f;
    CHECK (! decodeState (big.data(), big.size(), untouched));
}

TEST (jsfx190pj_bundled_pack)
{
    // every embedded file inflates to the bytes of resources/jsfx (the pack is MIT only: owner decision 2)
    int scripts = 0;
    bool same = true, licence = false;
    for (const auto& f : lynx::jsfxpack::files)
    {
        const auto data = lynx::inflateZlib (f.zlib, f.zlibSize);
        CHECK (data.has_value() && data->size() == f.size);
        const auto disk = i18ntest::readFile (std::string ("resources/jsfx/") + f.path);
        same = same && data.has_value() && disk.size() == data->size() && std::equal (disk.begin(), disk.end(), (const char*) data->data());
        const std::string path (f.path);
        scripts += path.size() > 5 && path.substr (path.size() - 5) == ".jsfx" ? 1 : 0;
        licence = licence || path == "chokehold/LICENSE.txt";
        // no @gfx scripts before LYNX draws @gfx (stage 3), and no REAPER stock effects
        if (data && path.find (".jsfx") != std::string::npos)
        {
            const std::string text ((const char*) data->data(), data->size());
            CHECK (text.find ("\n@gfx") == std::string::npos);
            CHECK (text.find ("Cockos Incorporated") == std::string::npos);
        }
    }
    CHECK (same);
    CHECK (licence);
    CHECK (scripts == 31);
}

// the runtime LYNX links (third_party/ysfx, with LYNX's file filter unset here) renders a bundled script as upstream
// ysfx does: tools/jsfx_ref.cpp's input and slider schedule, every 61st frame against its output
TEST (jsfx190pj_runtime_matches_upstream)
{
    ysfx_config_t* config = ysfx_config_new();
    ysfx_set_log_reporter (config, [] (intptr_t, ysfx_log_level, const char*) {});
    ysfx_t* fx = ysfx_new (config);
    ysfx_config_free (config);
    const auto ok = ysfx_load_file (fx, "resources/jsfx/chokehold/Clipper/soft_clipper.jsfx", 0) && ysfx_compile (fx, ysfx_compile_no_gfx);
    CHECK (ok);
    if (! ok)
    {
        ysfx_free (fx);
        return;
    }
    ysfx_set_sample_rate (fx, 48000.0);
    ysfx_set_block_size (fx, 4096);
    ysfx_init (fx);
    ysfx_time_info_t info {};
    info.tempo = 120.0;
    info.time_signature[0] = info.time_signature[1] = 4;
    std::vector<float> l (512), r (512);
    std::uint32_t lcg = 12345u;
    long frame = 0;
    std::size_t point = 0;
    float worst = 0.0f;
    for (int b = 0; b < 64; ++b)
    {
        if (b == 32)
            ysfx_slider_set_value (fx, 1, 12.0, true);
        for (int i = 0; i < 512; ++i)
        {
            const double n = (double) (frame + i);
            lcg = lcg * 1664525u + 1013904223u;
            const double noise = (double) (lcg >> 8) / 16777216.0 * 0.2 - 0.1;
            l[(std::size_t) i] = (float) (0.6 * std::sin (2.0 * 3.14159265358979323846 * 220.0 * n / 48000.0) + 0.3 * std::sin (2.0 * 3.14159265358979323846 * 3317.0 * n / 48000.0));
            r[(std::size_t) i] = (float) (0.5 * std::sin (2.0 * 3.14159265358979323846 * 440.0 * n / 48000.0 + 0.3) + noise);
        }
        ysfx_set_time_info (fx, &info);
        const float* ins[2] = { l.data(), r.data() };
        float* outs[2] = { l.data(), r.data() };
        ysfx_process_float (fx, ins, outs, 2, 2, 512);
        for (int i = 0; i < 512; ++i, ++frame)
            if (frame % 61 == 0 && point < std::size (lynx::jsfxref::softClipper))
            {
                worst = std::max ({ worst, std::abs (l[(std::size_t) i] - lynx::jsfxref::softClipper[point][0]),
                                    std::abs (r[(std::size_t) i] - lynx::jsfxref::softClipper[point][1]) });
                ++point;
            }
    }
    ysfx_free (fx);
    CHECK (point == std::size (lynx::jsfxref::softClipper));
    CHECK (worst <= 1.0e-5f);
    std::printf ("  jsfx190pj: soft_clipper against upstream ysfx, %d points, max |diff| %.3g\n", (int) point, (double) worst);
}
