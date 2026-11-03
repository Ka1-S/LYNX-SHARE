#include "ui/PluginPickerView.h"

#include "core/PickerView.h"
#include "core/PluginCatalog.h"
#include "ui/I18n.h"
#include "ui/QuickPick.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
struct Tile
{
    juce::String name, detail;
    std::string category;
    bool generator = true;
    std::function<void()> add;
    juce::Rectangle<int> bounds; // where it is drawn now (empty = not shown)
};

// a calm colour per category (LYNX's own tiles; FL shows the plugins' screenshots)
juce::Colour categoryColour (const std::string& category)
{
    const auto h = (float) (std::hash<std::string> {} (category) % 360) / 360.0f;
    return juce::Colour::fromHSV (h, 0.35f, 0.42f, 1.0f);
}

class PluginPickerView final : public juce::Component
{
public:
    PluginPickerView (AudioEngine& e, PluginPickerTargets t) : engine (e), targets (std::move (t))
    {
        setWantsKeyboardFocus (true);
        for (auto& g : quickpick::generatorEntries (engine))
        {
            if (! g.available || ! g.make)
                continue;
            const auto name = quickpick::shownName (engine, g.key, g.label);
            plugincatalog::Entry probe { g.key, g.label.toStdString(), "", "", g.detail.toStdString(), true };
            tiles.push_back ({ name, g.detail, plugincatalog::categoryOf (probe), true, [add = targets.addInstrument, make = g.make] {
                                  if (auto c = make(); c.has_value() && add)
                                      add (*c);
                              } });
        }
        for (auto& f : quickpick::effectEntries (engine))
        {
            if (! f.make)
                continue;
            const auto name = quickpick::shownName (engine, f.key, f.label);
            plugincatalog::Entry probe { f.key, f.label.toStdString(), "", "", f.detail.toStdString(), false };
            tiles.push_back ({ name, f.detail, plugincatalog::categoryOf (probe), false, [add = targets.addEffect, make = f.make] {
                                  if (auto s = make(); s.has_value() && add)
                                      add (*s);
                              } });
        }
        // the tabs: only the categories that hold something, in the catalog's order
        for (const auto instruments : { true, false })
            for (const auto& c : plugincatalog::categories (instruments))
                if (std::any_of (tiles.begin(), tiles.end(), [&] (const Tile& x) { return x.generator == instruments && x.category == c; }))
                    (instruments ? tabs.generators : tabs.effects).push_back (c);
        tab = tabs.all();
    }

    void parentHierarchyChanged() override
    {
        if (isShowing())
            grabKeyboardFocus();
    }

    void resized() override { layoutTiles(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg0.withAlpha (0.88f)); // the veil over the main window
        const auto bounds = getLocalBounds();

        if (tab == tabs.all())
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (22.0f));
            const auto header = bounds.withHeight (headerHeight);
            g.drawText (tr ("Generators"), header.withWidth (bounds.getWidth() / 2), juce::Justification::centred);
            g.drawText (tr ("Effects"), header.withTrimmedLeft (bounds.getWidth() / 2), juce::Justification::centred);
        }

        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const auto& t = tiles[(std::size_t) i];
            if (t.bounds.isEmpty())
                continue;
            const auto r = t.bounds.toFloat();
            const auto base = categoryColour (t.category);
            g.setColour (i == hoverTile ? base.brighter (0.35f) : base);
            g.fillRoundedRectangle (r, radius::sm);
            g.setColour (i == hoverTile ? colour::accent : colour::borderStrong);
            g.drawRoundedRectangle (r.reduced (0.5f), radius::sm, i == hoverTile ? 2.0f : 1.0f);
            auto text = t.bounds.reduced (6, 4);
            const auto small = text.getHeight() < 34;
            g.setColour (colour::textPrimary);
            g.setFont (font (small ? type::small : type::body, true));
            g.drawFittedText (t.name, small ? text : text.removeFromTop (text.getHeight() * 2 / 3), juce::Justification::centred, 2, 0.8f);
            if (! small)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (type::caption));
                g.drawFittedText (t.detail, text, juce::Justification::centredTop, 1, 0.8f);
            }
        }

        // the tab strip: generator categories, "All", effect categories; the shown one white and bold with a mark above
        const auto strip = stripBounds();
        for (int i = 0; i < tabs.count(); ++i)
        {
            const auto r = tabBounds (i);
            if (r.getRight() < strip.getX() || r.getX() > strip.getRight())
                continue;
            const auto shown = i == tab;
            g.setColour (shown ? colour::textPrimary : i == hoverTab ? colour::textSecondary.brighter (0.3f) : colour::textSecondary);
            g.setFont (font (i == tabs.all() ? 26.0f : 17.0f, shown));
            g.drawText (tabLabel (i), r, juce::Justification::centred);
            if (shown)
            {
                juce::Path mark;
                const auto cx = (float) r.getCentreX(), top = (float) r.getY() - 2.0f;
                mark.addTriangle (cx - 5.0f, top, cx + 5.0f, top, cx, top - 6.0f);
                g.setColour (colour::accent);
                g.fillPath (mark);
            }
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (17.0f));
        if (stripOffset > 0)
            g.drawText ("...", strip.withWidth (24), juce::Justification::centred);
        if (stripWidth() - stripOffset > strip.getWidth())
            g.drawText ("...", strip.withTrimmedLeft (strip.getWidth() - 24), juce::Justification::centred);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto t = tabAt (e.getPosition());
        hoverTab = t;
        if (t >= 0 && t != tab) // pointing at a tab shows it (FL)
            showTab (t);
        const auto i = tileAt (e.getPosition());
        if (i != hoverTile)
        {
            hoverTile = i;
            setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverTile = hoverTab = -1;
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown())
            return;
        if (tabAt (e.getPosition()) >= 0)
            return;
        const auto i = tileAt (e.getPosition());
        auto close = targets.close;
        if (i >= 0)
        {
            auto add = tiles[(std::size_t) i].add;
            if (close)
                close(); // (this view is gone after this)
            if (add)
                add();
            return;
        }
        if (close)
            close();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        // one tab per notch, down = the next one to the right (FL)
        wheelAccum += wheel.deltaY;
        const auto notches = (int) std::trunc (wheelAccum / 0.1f);
        if (notches == 0)
            return;
        wheelAccum -= (float) notches * 0.1f;
        showTab (pickerview::stepTab (tabs, tab, -notches));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey || key.getKeyCode() == juce::KeyPress::F8Key)
        {
            if (auto close = targets.close)
                close();
            return true;
        }
        if (key.getKeyCode() == juce::KeyPress::leftKey || key.getKeyCode() == juce::KeyPress::rightKey)
        {
            showTab (pickerview::stepTab (tabs, tab, key.getKeyCode() == juce::KeyPress::rightKey ? 1 : -1));
            return true;
        }
        return false;
    }

    int shownTab() const noexcept { return tab; }

private:
    static constexpr int headerHeight = 56, stripHeight = 64, gap = 10;

    juce::String tabLabel (int i) const
    {
        return i == tabs.all() ? tr ("All") : tr (tabs.name (i).c_str());
    }

    juce::Rectangle<int> stripBounds() const { return getLocalBounds().removeFromBottom (stripHeight).reduced (8, 10); }

    int tabWidth (int i) const
    {
        const auto f = juce::Font (theme::font (i == tabs.all() ? 26.0f : 17.0f, true));
        return juce::GlyphArrangement::getStringWidthInt (f, tabLabel (i)) + 34;
    }

    int stripWidth() const
    {
        int w = 0;
        for (int i = 0; i < tabs.count(); ++i)
            w += tabWidth (i);
        return w;
    }

    juce::Rectangle<int> tabBounds (int i) const
    {
        const auto strip = stripBounds();
        // the strip is centred on "All" when it fits, else scrolled so the shown tab stays in view
        int x = strip.getX() + juce::jmax (0, (strip.getWidth() - stripWidth()) / 2) - stripOffset;
        for (int k = 0; k < i; ++k)
            x += tabWidth (k);
        return { x, strip.getY() + 10, tabWidth (i), strip.getHeight() - 10 };
    }

    int tabAt (juce::Point<int> p) const
    {
        if (! stripBounds().contains (p))
            return -1;
        for (int i = 0; i < tabs.count(); ++i)
            if (tabBounds (i).contains (p))
                return i;
        return -1;
    }

    int tileAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(std::size_t) i].bounds.contains (p))
                return i;
        return -1;
    }

    void showTab (int t)
    {
        tab = t;
        // keep the shown tab inside the strip
        const auto strip = stripBounds();
        const auto over = stripWidth() - strip.getWidth();
        if (over > 0)
        {
            int left = 0;
            for (int k = 0; k < tab; ++k)
                left += tabWidth (k);
            stripOffset = juce::jlimit (0, over, left + tabWidth (tab) / 2 - strip.getWidth() / 2);
        }
        else
            stripOffset = 0;
        hoverTile = -1;
        layoutTiles();
        repaint();
    }

    void flow (std::vector<int> items, juce::Rectangle<int> area, bool centreRow)
    {
        const auto f = pickerview::fitTiles ((int) items.size(), area.getWidth(), area.getHeight(), gap, 70, 190, 0.55);
        if (f.tileWidth <= 0)
            return;
        const auto rows = f.rows;
        const auto usedHeight = rows * f.tileHeight + (rows - 1) * gap;
        auto y = area.getY() + (centreRow ? juce::jmax (0, (area.getHeight() - usedHeight) / 2) : 0);
        for (int r = 0; r < rows; ++r)
        {
            const auto first = r * f.columns;
            const auto inRow = juce::jmin (f.columns, (int) items.size() - first);
            const auto rowWidth = inRow * f.tileWidth + (inRow - 1) * gap;
            auto x = area.getX() + (area.getWidth() - rowWidth) / 2;
            for (int c = 0; c < inRow; ++c)
            {
                if (y + f.tileHeight <= area.getBottom() + 1)
                    tiles[(std::size_t) items[(std::size_t) (first + c)]].bounds = { x, y, f.tileWidth, f.tileHeight };
                x += f.tileWidth + gap;
            }
            y += f.tileHeight + gap;
        }
    }

    void layoutTiles()
    {
        for (auto& t : tiles)
            t.bounds = {};
        auto area = getLocalBounds().withTrimmedBottom (stripHeight).reduced (24, 0);
        if (tab == tabs.all())
        {
            area.removeFromTop (headerHeight);
            area.removeFromBottom (8);
            const auto left = area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (20);
            const auto right = area.withTrimmedLeft (20);
            std::vector<int> gens, fx;
            for (int i = 0; i < (int) tiles.size(); ++i)
                (tiles[(std::size_t) i].generator ? gens : fx).push_back (i);
            flow (gens, left, false);
            flow (fx, right, false);
            return;
        }
        area.removeFromTop (24);
        std::vector<int> shown;
        const auto generators = tabs.isGenerator (tab);
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(std::size_t) i].generator == generators && tiles[(std::size_t) i].category == tabs.name (tab))
                shown.push_back (i);
        flow (shown, area, true); // a category's plugins centred (FL)
    }

    AudioEngine& engine;
    PluginPickerTargets targets;
    std::vector<Tile> tiles;
    pickerview::Tabs tabs;
    int tab = 0, hoverTab = -1, hoverTile = -1, stripOffset = 0;
    float wheelAccum = 0.0f;
};
} // namespace

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets)
{
    return std::make_unique<PluginPickerView> (engine, std::move (targets));
}
} // namespace lynx
