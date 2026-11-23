// Round 190oo (design review 2, group "fit"): translated texts that pushed toolbar panels out or ran into each other.
// WG-11: at 1920 px the German / Russian menu titles set the hint column's width and the Chord panel and the output
// scope dropped; the titles now give up width first (condensed face, then a squeeze of at least 0.9).
// WG-31: knob captions keep >= 4 px between them, squeezed to 80 % at most, else the 9 px face (two lines if they can).
#include <array>
#include <cmath>

#include "core/TextFit.h"
#include "core/ToolbarLayout.h"

TEST (textfit190oo_menu_squeeze_keeps_the_panels_at_1920)
{
    using namespace lynx;
    using toolbarlayout::Group;
    // the Pattern toolbar's panels in their tight form at 1920 px (MainComponent::layoutToolbar: 31 px rows, gaps 12)
    std::array<int, toolbarlayout::groupCount> widths {};
    widths[(std::size_t) Group::transport] = 452;
    widths[(std::size_t) Group::master] = 118;
    widths[(std::size_t) Group::record] = 163;
    widths[(std::size_t) Group::snap] = 204;
    widths[(std::size_t) Group::windows] = 97;
    widths[(std::size_t) Group::file] = 97;
    widths[(std::size_t) Group::meters] = 118;
    widths[(std::size_t) Group::output] = 104;
    widths[(std::size_t) Group::chord] = 88;
    const auto layout = toolbarlayout::Layout::makeDefault();
    constexpr int room = 1904, gap = 12, items = 8, padding = 8 * items;
    int panels = 0;
    for (const auto& e : layout.entries)
        if (e.shown)
            panels += widths[(std::size_t) e.group] + gap;
    CHECK (panels == 1549);

    // German's titles with the tight padding (~392 px + 8): the Chord panel drops (the bug)
    const auto before = toolbarlayout::fit (layout, widths, gap, room, 400);
    CHECK (! before[(std::size_t) Group::chord]);

    // the condensed titles (~300 px of text) squeezed just enough: every panel stays, the squeeze is >= 0.9
    const auto scale = textfit::menuSqueeze (room, panels, 300, padding, items);
    CHECK (scale >= 0.9 && scale < 1.0);
    const auto hint = (int) std::ceil (300.0 * scale) + padding + 8;
    const auto after = toolbarlayout::fit (layout, widths, gap, room, hint);
    for (const auto& e : layout.entries)
        if (e.shown)
            CHECK (after[(std::size_t) e.group]);

    // English's titles fit as they are: no squeeze; a menu far too long stops at 0.9 (a panel may drop then)
    CHECK (textfit::menuSqueeze (room, panels, 222, padding, items) == 1.0);
    CHECK (textfit::menuSqueeze (room, panels, 500, padding, items) == 0.9);
    CHECK (textfit::menuSqueeze (room, panels, 0, padding, items) == 1.0);
}

TEST (textfit190oo_knob_captions)
{
    using lynx::textfit::CaptionFit;
    using lynx::textfit::captionFit;
    // the Misc page: 50 px pitch, 46 px for the caption
    CHECK (captionFit (40.0f, 46.0f, true, true) == CaptionFit::normal);    // en "CUT GROUP"
    CHECK (captionFit (57.0f, 46.0f, false, true) == CaptionFit::normal);   // de "WIEDERHOLEN": squeezed to ~0.81
    CHECK (captionFit (69.0f, 46.0f, true, true) == CaptionFit::twoLines);  // ru "ГРУППА ОБРЕЗКИ"
    CHECK (captionFit (69.0f, 46.0f, true, false) == CaptionFit::smallFace); // no room for a second line
    CHECK (captionFit (65.0f, 46.0f, false, true) == CaptionFit::smallFace); // one long word never breaks
}
