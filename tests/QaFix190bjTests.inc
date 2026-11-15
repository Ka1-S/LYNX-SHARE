// Round 190bj (QA of 2026-11-11): a scaled plugin editor's window keeps the editor's aspect ratio on its CONTENT, not on
// the whole window (core/WindowFit.h aspectContent). The Apex Limiter (950 x 618 with its description line) under a
// native Windows frame (8 px sides and bottom, 31 px title bar) had its status row cut off by ~30 px.
#include "core/WindowFit.h"

TEST (qafix190bj_aspect_on_content)
{
    using lynx::windowfit::aspectContent;
    using lynx::windowfit::Box;
    const lynx::windowfit::Frame frame { 8, 31, 8, 8 };
    auto content = [&frame] (const Box& b) { return Box { 0, 0, b.w - frame.left - frame.right, b.h - frame.top - frame.bottom }; };

    // the default size: the window's content is the editor, 950 x 618 (JUCE's ratio on the whole 966 x 657 window gave
    // 950 x 589)
    const auto opened = aspectContent ({ 100, 100, 966, 657 }, frame, 950, 618, 0.7, 2.0);
    CHECK ((opened == Box { 100, 100, 966, 657 }));
    CHECK ((content (opened) == Box { 0, 0, 950, 618 }));
    // moved (same size): unchanged
    CHECK ((aspectContent ({ 400, 20, 966, 657 }, frame, 950, 618, 0.7, 2.0) == Box { 400, 20, 966, 657 }));
    // the top edge dragged down: sized by the height, the bottom edge stays
    const auto fromTop = aspectContent ({ 100, 150, 966, 607 }, frame, 950, 618, 0.7, 2.0, true, false, false, false);
    CHECK ((content (fromTop).h) == (568));
    CHECK ((content (fromTop).w) == (873));
    CHECK ((fromTop.y + fromTop.h) == (150 + 607));
    CHECK ((fromTop.x) == (100));
    // the left edge dragged out: sized by the width, the right edge stays
    const auto fromLeft = aspectContent ({ 50, 100, 1016, 657 }, frame, 950, 618, 0.7, 2.0, false, true, false, false);
    CHECK ((content (fromLeft).w) == (1000));
    CHECK ((content (fromLeft).h) == (651));
    CHECK ((fromLeft.x + fromLeft.w) == (50 + 1016));
    // the corner: sized by the width; every size keeps the ratio within a pixel
    const auto corner = aspectContent ({ 100, 100, 1300, 700 }, frame, 950, 618, 0.7, 2.0, false, false, true, true);
    CHECK (std::abs (content (corner).h - 618.0 * content (corner).w / 950.0) <= 1.0);
    // the limits: 70 % and 200 % of the editor
    CHECK ((content (aspectContent ({ 0, 0, 200, 200 }, frame, 950, 618, 0.7, 2.0)) == Box { 0, 0, 665, 433 }));
    CHECK ((content (aspectContent ({ 0, 0, 3000, 2000 }, frame, 950, 618, 0.7, 2.0)) == Box { 0, 0, 1900, 1236 }));
    // no frame (Linux without a known frame size, the snapshots): the content is the box
    CHECK ((aspectContent ({ 0, 0, 950, 600 }, {}, 950, 618, 0.7, 2.0) == Box { 0, 0, 950, 618 }));
}

// The channel's Freeze menu item has its own key: the spectrum's "Freeze" (a still picture) read "Стоп-кадр" in Russian,
// "Figer" in French and "Blocca" in Italian on the channel menu (coordinator, round 190bj).
TEST (qafix190bj_channel_freeze_has_its_own_translation)
{
    using namespace i18ntest;
    const std::pair<const char*, const char*> expected[] = { { "ru", "Заморозить" }, { "fr", "Geler" }, { "it", "Congela" } };
    for (const auto& entry : std::filesystem::directory_iterator ("resources/i18n"))
    {
        if (entry.path().extension() != ".txt")
            continue;
        std::vector<std::string> duplicates;
        const auto table = parseTable (readFile (entry.path()), duplicates);
        const auto it = table.find ("@channel: Freeze");
        CHECK (it != table.end() && ! it->second.empty());
        for (const auto& [lang, word] : expected)
            if (entry.path().stem() == lang)
                CHECK (it != table.end() && it->second == word);
    }
    const auto rack = readFile ("src/ui/ChannelRackComponent.cpp"), grid = readFile ("src/ui/ClipGridComponent.cpp");
    CHECK (rack.find ("trc (\"@channel: Freeze\")") != std::string::npos);
    // (round 190ff: the clip grid's column menu has the reference's own "Freeze Track", a key of its own)
    CHECK (grid.find ("tr (\"Freeze Track\")") != std::string::npos);
    CHECK (grid.find ("menu.addItem (tr (\"Freeze\")") == std::string::npos);
}

// Piano roll scripts live in Documents/LYNX/Piano roll scripts, as the docs say; the first folder (Documents/Piano roll
// scripts) is still read (coordinator, round 190bj)
TEST (qafix190bj_piano_roll_scripts_folder)
{
    using namespace i18ntest;
    const auto code = readFile ("src/ui/PianoRollScripts.cpp");
    CHECK (code.find ("documentsFolder().getChildFile (\"LYNX\").getChildFile (\"Piano roll scripts\")") != std::string::npos);
    CHECK (code.find ("oldScriptsFolder()") != std::string::npos);
    CHECK (readFile ("docs/PARITY.md").find ("Documents/LYNX/Piano roll scripts") != std::string::npos);
}
