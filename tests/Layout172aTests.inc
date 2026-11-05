// Round 172a: the FL engines' designed pages — Harmor's page in FL's arrangement with both parts and the global strip,
// Sytrus' FM / RM matrix, and no page with an empty titled panel or a note about what LYNX doesn't edit.

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "core/HarmorControls.h"
#include "core/PluginLayoutSynths.h"

namespace layout172a
{
bool inside (const lynx::plugui::LayoutItem& outer, const lynx::plugui::LayoutItem& inner)
{
    const auto cx = inner.x + inner.w * 0.5f, cy = inner.y + inner.h * 0.5f;
    return cx >= outer.x && cx <= outer.x + outer.w && cy >= outer.y && cy <= outer.y + outer.h;
}

bool samePage (const lynx::plugui::LayoutItem& a, const lynx::plugui::LayoutItem& b)
{
    return a.page < 0 || b.page < 0 || a.page == b.page;
}

std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return s;
}
} // namespace layout172a

TEST (layout172a_engine_pages_have_no_empty_panels_or_notes)
{
    using namespace lynx;
    using plugui::Item;
    int pages = 0;
    for (const auto kind : plugui::instrumentLayoutKinds())
    {
        const auto* layout = plugui::instrumentLayout (kind);
        if (layout == nullptr)
            continue;
        ++pages;
        for (const auto& s : layout->items)
        {
            // no text about what LYNX leaves as saved / doesn't play / has no presets for
            if (s.label != nullptr)
            {
                const auto text = layout172a::lower (s.label);
                for (const char* bad : { "saved patch", "not played", "no lynx presets", "not decoded", "not edited", "not editable" })
                {
                    CHECK (text.find (bad) == std::string::npos);
                    if (text.find (bad) != std::string::npos)
                        std::printf ("  %s: note on the page: \"%s\"\n", layout->fl, s.label);
                }
            }
            // a titled panel holds a control (or a display) of its own page
            if (s.type != Item::section || s.label == nullptr || *s.label == 0)
                continue;
            const auto filled = std::any_of (layout->items.begin(), layout->items.end(), [&] (const plugui::LayoutItem& i) {
                return &i != &s && i.type != Item::section && i.type != Item::label && layout172a::samePage (s, i) && layout172a::inside (s, i);
            });
            CHECK (filled);
            if (! filled)
                std::printf ("  %s: empty panel \"%s\"\n", layout->fl, s.label);
        }
    }
    CHECK (pages >= 30);
}

TEST (layout172a_harmor_page_edits_both_parts_and_the_global_strip)
{
    using namespace lynx;
    const auto* layout = plugui::instrumentLayout (InstrumentKind::harmor);
    CHECK (layout != nullptr);
    if (layout == nullptr)
        return;
    CHECK (layout->pages.size() == 2u); // part A / part B
    std::set<std::string> known, shown;
    for (const auto& c : harmorui::controls())
        CHECK (known.insert (c.name).second); // unique names (the knobs' names)
    for (const auto& item : layout->items)
    {
        for (const char* p : { item.param, item.param2 })
            if (p != nullptr)
            {
                CHECK (known.count (p) == 1);
                shown.insert (p);
            }
        if (item.param != nullptr && plugui::isValueControl (item.type))
        {
            // part controls sit on their part's page, global ones on both
            const auto part = std::string (item.param).rfind ("A: ", 0) == 0 ? 0 : std::string (item.param).rfind ("B: ", 0) == 0 ? 1 : -1;
            CHECK (item.page == part);
        }
        CHECK (item.x >= 0.0f && item.y >= 0.0f && item.x + item.w <= (float) layout->width + 0.5f && item.y + item.h <= (float) layout->height + 0.5f);
    }
    CHECK (shown.size() == known.size()); // every control is on the page
    CHECK (harmorui::controls().size() > 120);

    // every control reads a value of a real state within (or at) FL's range, and writes back into it
    // a two-part state: the test patch's part A block (length-prefixed) repeated as part B
    auto state = harmorTestState (false);
    {
        std::uint32_t size = 0;
        std::memcpy (&size, state.data() + 8, 4);
        const auto partEnd = (std::ptrdiff_t) (8 + 4 + size);
        state.insert (state.begin() + partEnd, state.begin() + 8, state.begin() + partEnd);
        state[4] = 1 | 2 | 4;
    }
    for (const auto& c : harmorui::controls())
    {
        const auto at = harmorParamOffset (state, c.part, c.index);
        CHECK (at >= 0);
        if (at < 0)
            std::printf ("  no Harmor parameter for \"%s\"\n", c.name.c_str());
    }
    const auto cutoffB = harmorParamOffset (state, 1, 52);
    CHECK (cutoffB >= 0 && cutoffB != harmorParamOffset (state, 0, 52));
    CHECK (harmorui::name (1, 52) != nullptr && std::string (harmorui::name (1, 52)) == "B: Filter 1 frequency");
    CHECK (harmorui::name (-1, 12) != nullptr && harmorui::name (0, 3) == nullptr);
}

TEST (layout172a_sytrus_page_shows_the_matrix)
{
    using namespace lynx;
    const auto* layout = plugui::instrumentLayout (InstrumentKind::sytrus);
    CHECK (layout != nullptr);
    if (layout == nullptr)
        return;
    std::set<std::string> settings;
    for (const auto& s : sytrus::settings())
        settings.insert (s.name);
    int cells = 0, fm = 0, rm = 0;
    for (const auto& item : layout->items)
        if (item.param != nullptr && plugui::isValueControl (item.type))
        {
            CHECK (settings.count (item.param) == 1); // every control is one of the engine's settings
            const std::string p = item.param;
            const auto endsWith = [&p] (const char* tail) { const std::string t = tail; return p.size() > t.size() && p.compare (p.size() - t.size(), t.size(), t) == 0; };
            const auto opOrFilter = p.rfind ("Op ", 0) == 0 || p.rfind ("Filter ", 0) == 0;
            if (p.rfind ("FM ", 0) == 0 || p.rfind ("RM ", 0) == 0 || (opOrFilter && (endsWith (" pan") || endsWith (" FX") || endsWith (" out"))))
            {
                ++cells;
                fm += p.rfind ("FM ", 0) == 0 && item.page == 0 ? 1 : 0;
                rm += p.rfind ("RM ", 0) == 0 && item.page == 1 ? 1 : 0;
            }
        }
    CHECK (cells == 81 + 54 && fm == 54 && rm == 54);
    CHECK (layout->pages.size() == 2u);
}
