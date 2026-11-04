#include "ui/PluginPickerView.h"

#include "core/PickerView.h"
#include "core/PluginCatalog.h"
#include "ui/BrowserComponent.h"
#include "ui/I18n.h"
#include "ui/PluginThumbs.h"
#include "ui/QuickPick.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
constexpr const char* installedTab = "Installed"; // third-party plugins in no folder of the Plugin database (LYNX)

struct Tile
{
    juce::String name, detail, vendor;
    std::string key, category;
    bool generator = true, native = true;
    std::function<std::optional<ChannelDef>()> makeGenerator;
    std::function<std::optional<EffectSlot>()> makeEffect;
    juce::Rectangle<int> bounds; // where it is drawn now (empty = not shown)

    // the picture: none yet / loaded / not there (a native one may still come from the background drawing)
    enum class Thumb { unknown, loaded, missing } thumb = Thumb::unknown;
    juce::Image picture;      // as kept on disk
    juce::Image scaled;       // fitted to the tile's current size (painting is a plain blit)
    juce::Rectangle<int> scaledFor;
    double scaledAt = 0.0;    // the display scale it was made for
    double nextLook = 0.0;    // missing: when to look for the file again (ms)
};

class PluginPickerView final : public juce::Component,
                               public ui::HintSource,
                               private juce::Timer
{
public:
    PluginPickerView (AudioEngine& e, PluginPickerTargets t) : engine (e), targets (std::move (t))
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        setWantsKeyboardFocus (true);
        setOpaque (true);
        collectTiles();

        search.setTextToShowWhenEmpty (tr ("Search plugins"), theme::colour::textDisabled);
        search.setJustification (juce::Justification::centredLeft);
        search.setFont (juce::Font (theme::font (15.0f)));
        search.setIndents (10, 0);
        search.setColour (juce::TextEditor::backgroundColourId, theme::colour::bg3);
        search.setColour (juce::TextEditor::outlineColourId, theme::colour::borderStrong);
        search.setColour (juce::TextEditor::focusedOutlineColourId, theme::colour::accent);
        search.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        search.onTextChange = [this] { selected = -1; scroll = 0; layoutTiles(); repaint(); };
        search.onReturnKey = [this] { addSelectedOrFirst(); };
        search.onEscapeKey = [this] { escape(); };
        search.addKeyListener (&searchKeys);
        addAndMakeVisible (search);
        startTimerHz (30);
        buildMs = juce::Time::getMillisecondCounterHiRes() - t0;
    }

    ~PluginPickerView() override { search.removeKeyListener (&searchKeys); }

    double constructionMs() const noexcept { return buildMs; }
    double backdropTakenMs() const noexcept { return backdropMs; }

    void parentHierarchyChanged() override
    {
        // the main window under a dark veil, taken once when the picker opens (the picker is opaque: a hover repaints
        // only the picker, never the mixer and playlist under it)
        if (auto* parent = getParentComponent(); parent != nullptr && ! backdrop.isValid() && parent->getWidth() > 0)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto shown = isVisible();
            setVisible (false);
            backdrop = parent->createComponentSnapshot (parent->getLocalBounds(), true, 0.5f);
            setVisible (shown);
            backdropMs = juce::Time::getMillisecondCounterHiRes() - t0;
        }
        if (isShowing())
            grabKeyboardFocus();
    }

    void visibilityChanged() override
    {
        if (isShowing())
            grabKeyboardFocus();
    }

    void resized() override
    {
        search.setBounds (getLocalBounds().withHeight (headerHeight).withSizeKeepingCentre (juce::jmin (340, getWidth() / 3), 30));
        layoutTiles();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        paintBackdrop (g);
        const auto bounds = getLocalBounds();
        const auto searching = search.getText().trim().isNotEmpty();

        if (tab == tabs.all() || searching)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (20.0f));
            const auto header = bounds.withHeight (headerHeight);
            const auto half = bounds.getWidth() / 2;
            const auto side = (half - search.getWidth() / 2 - 24);
            g.drawText (tr ("Generators"), header.withWidth (side).withTrimmedLeft (24), juce::Justification::centredLeft);
            g.drawText (tr ("Effects"), header.withTrimmedLeft (bounds.getWidth() - side).withTrimmedRight (24), juce::Justification::centredRight);
        }
        else
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (20.0f));
            const auto side = (bounds.getWidth() - search.getWidth()) / 2 - 24;
            g.drawText ((tabs.isGenerator (tab) ? tr ("Generators") : tr ("Effects")) + "  ·  " + tabLabel (tab),
                        bounds.withHeight (headerHeight).withWidth (side).withTrimmedLeft (24), juce::Justification::centredLeft);
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (tileArea());
            const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (! tiles[(std::size_t) i].bounds.isEmpty() && g.clipRegionIntersects (tiles[(std::size_t) i].bounds))
                    paintTile (g, i, scale);
        }
        if (shownCount == 0)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::title));
            g.drawText (tr ("No plugin matches \"{0}\".", { search.getText().trim() }), tileArea(), juce::Justification::centred);
        }
        paintScrollbar (g);
        paintStrip (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto t = tabAt (e.getPosition());
        if (t != hoverTab)
        {
            repaint (stripBounds());
            hoverTab = t;
        }
        if (t >= 0 && t != tab && search.getText().isEmpty()) // pointing at a tab shows it (FL)
            showTab (t);
        setHover (tileAt (e.getPosition()));
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverTab = -1;
        setHover (-1);
        repaint (stripBounds());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressedTile = tileAt (e.getPosition());
        draggingBar = scrollbarBounds().contains (e.getPosition());
        if (draggingBar)
            barGrab = e.getPosition().y - thumbBounds().getY();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (draggingBar)
        {
            const auto bar = scrollbarBounds();
            const auto room = juce::jmax (1, bar.getHeight() - thumbBounds().getHeight());
            setScroll ((int) ((double) (e.getPosition().y - barGrab - bar.getY()) / room * maxScroll()));
            return;
        }
        // a tile dragged out: onto a mixer slot / the Channel Rack / the Playlist, the picker steps aside meanwhile
        if (pressedTile >= 0 && ! dragging && e.getDistanceFromDragStart() > 6)
            startTileDrag (pressedTile);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        draggingBar = false;
        if (e.mouseWasDraggedSinceMouseDown() || dragging)
            return;
        if (tabAt (e.getPosition()) >= 0)
        {
            search.clear();
            showTab (tabAt (e.getPosition()));
            return;
        }
        if (e.mods.isPopupMenu())
            return;
        const auto i = tileAt (e.getPosition());
        if (i >= 0)
        {
            addTile (i);
            return;
        }
        if (! scrollbarBounds().contains (e.getPosition()) && ! search.getBounds().contains (e.getPosition()))
            close();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // over a scrollbar (or with Shift): scroll; else one tab per notch, down = the next one to the right (FL)
        if ((e.mods.isShiftDown() || scrollbarBounds().expanded (8).contains (e.getPosition())) && maxScroll() > 0)
        {
            setScroll (scroll - juce::roundToInt ((wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * 400.0f));
            return;
        }
        if (search.getText().isNotEmpty())
            return;
        wheelAccum += wheel.deltaY;
        const auto notches = (int) std::trunc (wheelAccum / 0.1f);
        if (notches == 0)
            return;
        wheelAccum -= (float) notches * 0.1f;
        showTab (pickerview::stepTab (tabs, tab, -notches));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        if (code == juce::KeyPress::escapeKey)
        {
            escape();
            return true;
        }
        if (code == juce::KeyPress::F8Key)
        {
            close();
            return true;
        }
        if (code == juce::KeyPress::tabKey || (key.getModifiers().isCtrlDown() && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)))
        {
            const auto right = code == juce::KeyPress::tabKey ? ! key.getModifiers().isShiftDown() : code == juce::KeyPress::rightKey;
            search.clear();
            showTab (pickerview::stepTab (tabs, tab, right ? 1 : -1));
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            moveSelection (code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0,
                           code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0);
            return true;
        }
        if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
        {
            const auto shown = shownInOrder();
            if (! shown.empty())
                select (code == juce::KeyPress::homeKey ? shown.front() : shown.back());
            return true;
        }
        if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
        {
            setScroll (scroll + (code == juce::KeyPress::pageDownKey ? 1 : -1) * tileArea().getHeight() * 3 / 4);
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            addSelectedOrFirst();
            return true;
        }
        // typing anywhere searches
        const auto c = key.getTextCharacter();
        if (c >= ' ' && ! key.getModifiers().isCtrlDown() && ! key.getModifiers().isAltDown() && ! key.getModifiers().isCommandDown())
        {
            search.grabKeyboardFocus();
            search.insertTextAtCaret (juce::String::charToString (c));
            return true;
        }
        if (code == juce::KeyPress::backspaceKey && search.getText().isNotEmpty())
        {
            search.grabKeyboardFocus();
            search.setText (search.getText().dropLastCharacters (1));
            return true;
        }
        return false;
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        if (const auto i = tileAt (p); i >= 0)
        {
            const auto& t = tiles[(std::size_t) i];
            h.text = t.name + "  ·  " + t.detail + "  —  "
                     + (t.generator ? tr ("click: add as a new channel; drag onto the Channel Rack, a channel, the Playlist or the Mixer")
                                    : tr ("click: add to the selected mixer insert; drag onto a mixer insert or effect slot"));
        }
        else if (const auto k = tabAt (p); k >= 0)
            h.text = tabLabel (k) + " — " + tr ("point at a tab or turn the mouse wheel to change the category");
        return h;
    }

    int shownTab() const noexcept { return tab; }

private:
    static constexpr int headerHeight = 56, stripHeight = 64, gap = 10, labelHeight = 16, barWidth = 8;

    struct SearchKeys final : juce::KeyListener
    {
        PluginPickerView& view;
        explicit SearchKeys (PluginPickerView& v) : view (v) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            const auto code = key.getKeyCode();
            // the search box keeps Left / Right for its caret; the rest moves through the tiles
            if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::tabKey || code == juce::KeyPress::F8Key
                || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
                return view.keyPressed (key);
            return false;
        }
    } searchKeys { *this };

    // --- the plugins -----------------------------------------------------------------------------------------------
    void collectTiles()
    {
        auto& host = engine.getPluginHost();
        const auto genEntries = quickpick::generatorEntries (engine);
        const auto fxEntries = quickpick::effectEntries (engine);

        // FL's Plugin database folders as the categories (round 170b: the user's folder, else FL's category for FL's
        // plugins, else "LYNX SHARE"); third-party plugins in no folder: "Installed"
        std::vector<plugincatalog::BuiltinPlugin> genBuiltins, fxBuiltins;
        for (const auto& g : genEntries)
            if (g.folder != 3)
                genBuiltins.push_back ({ g.key, g.key.rfind ("lynx:gen:", 0) == 0 ? g.key.substr (9) : g.key, true, g.folder == 1 });
        for (const auto& f : fxEntries)
            if (f.folder != 3)
                fxBuiltins.push_back ({ f.key, f.key.rfind ("lynx:fx:", 0) == 0 ? f.key.substr (8) : f.key, f.flPlugin, f.flPlugin });
        const auto genFolders = plugincatalog::withDefaultCategories (host.databaseFolders(), true, genBuiltins);
        const auto fxFolders = plugincatalog::withDefaultCategories (host.databaseFolders(), false, fxBuiltins);
        auto categoryOf = [] (const plugincatalog::Folders& folders, const std::string& key) {
            if (const auto it = folders.of.find (key); it != folders.of.end() && ! it->second.empty())
                return it->second.substr (0, it->second.find ('/')); // (a sub-folder belongs to its top folder's tab)
            return std::string (installedTab);
        };
        auto vendorOf = [] (const juce::String& detail) { return detail.upToFirstOccurrenceOf (" · ", false, false); };

        for (const auto& g : genEntries)
        {
            if (! g.available || ! g.make)
                continue;
            Tile t;
            t.name = quickpick::shownName (engine, g.key, g.label);
            t.detail = g.detail;
            t.vendor = g.folder == 3 ? vendorOf (g.detail) : juce::String ("LYNX SHARE");
            t.key = g.key;
            t.category = categoryOf (genFolders, g.key);
            t.generator = true;
            t.native = g.folder != 3;
            t.makeGenerator = g.make;
            tiles.push_back (std::move (t));
        }
        for (const auto& f : fxEntries)
        {
            if (! f.available || ! f.make)
                continue;
            Tile t;
            t.name = quickpick::shownName (engine, f.key, f.label);
            t.detail = f.detail;
            t.vendor = f.folder == 3 ? vendorOf (f.detail) : juce::String ("LYNX SHARE");
            t.key = f.key;
            t.category = categoryOf (fxFolders, f.key);
            t.generator = false;
            t.native = f.folder != 3;
            t.makeEffect = f.make;
            tiles.push_back (std::move (t));
        }
        // A-Z inside each side (FL lists the database A-Z)
        std::stable_sort (tiles.begin(), tiles.end(), [] (const Tile& a, const Tile& b) {
            if (a.generator != b.generator)
                return a.generator;
            return a.name.compareNatural (b.name) < 0;
        });

        for (const auto generators : { true, false })
        {
            std::vector<std::string> names;
            for (const auto& t : tiles)
                if (t.generator == generators)
                    names.push_back (t.category);
            (generators ? tabs.generators : tabs.effects) = pickerview::orderTabs (names, plugincatalog::lynxCategory, installedTab);
        }
        tab = tabs.all();
    }

    bool tileShown (const Tile& t) const
    {
        const auto query = search.getText().trim();
        if (query.isNotEmpty())
            return pickerview::matches (query.toStdString(), { t.name.toStdString(), t.detail.toStdString(), t.category, t.vendor.toStdString() });
        if (tab == tabs.all())
            return true;
        return t.generator == tabs.isGenerator (tab) && t.category == tabs.name (tab);
    }

    void addTile (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        const auto& t = tiles[(std::size_t) i];
        auto close = targets.close;
        auto addInstrument = targets.addInstrument;
        auto addEffect = targets.addEffect;
        auto makeGenerator = t.makeGenerator;
        auto makeEffect = t.makeEffect;
        if (close)
            close(); // (this view is gone after this)
        if (makeGenerator && addInstrument)
        {
            if (auto c = makeGenerator())
                addInstrument (*c);
        }
        else if (makeEffect && addEffect)
        {
            if (auto s = makeEffect())
                addEffect (*s);
        }
    }

    void addSelectedOrFirst()
    {
        if (selected >= 0 && ! tiles[(std::size_t) selected].bounds.isEmpty())
            addTile (selected);
        else if (const auto shown = shownInOrder(); ! shown.empty())
            addTile (shown.front());
    }

    void escape()
    {
        if (search.getText().isNotEmpty())
        {
            search.clear();
            grabKeyboardFocus();
            return;
        }
        close();
    }

    void close()
    {
        if (auto c = targets.close)
            c();
    }

    void startTileDrag (int i)
    {
        auto& t = tiles[(std::size_t) i];
        juce::Image image;
        if (t.picture.isValid())
        {
            const auto scale = juce::jmin (1.0, 140.0 / t.picture.getWidth(), 100.0 / t.picture.getHeight());
            image = t.picture.rescaled (juce::jmax (1, (int) (t.picture.getWidth() * scale)), juce::jmax (1, (int) (t.picture.getHeight() * scale)),
                                        juce::Graphics::mediumResamplingQuality);
        }
        else
        {
            image = juce::Image (juce::Image::ARGB, 140, 36, true);
            juce::Graphics g (image);
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (image.getBounds().toFloat(), theme::radius::md);
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::body, true));
            g.drawFittedText (t.name, image.getBounds().reduced (6, 2), juce::Justification::centred, 2);
        }
        if (! BrowserComponent::startPluginDrag (*this, t.generator ? t.makeGenerator : nullptr, t.generator ? nullptr : t.makeEffect, t.name, image))
            return;
        // FL's picker gets out of the way while a plugin is dragged: the windows under it take the drop
        dragging = true;
        setAlpha (0.0f);
        setInterceptsMouseClicks (false, false);
        search.setInterceptsMouseClicks (false, false);
    }

    // --- pictures --------------------------------------------------------------------------------------------------
    void timerCallback() override
    {
        if (dragging)
        {
            // the drag ended (dropped or cancelled): the picker closes
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container == nullptr || ! container->isDragAndDropActive())
                close();
            return;
        }
        // lazily: only the tiles shown, a few per frame (the PNG decode is ~1 ms each)
        const auto now = juce::Time::getMillisecondCounterHiRes();
        int loads = 0;
        bool nativeMissing = false;
        for (auto& t : tiles)
        {
            if (t.bounds.isEmpty() || t.thumb == Tile::Thumb::loaded || ! tileArea().intersects (t.bounds))
                continue;
            if (t.thumb == Tile::Thumb::missing && now < t.nextLook)
            {
                nativeMissing = nativeMissing || t.native;
                continue;
            }
            if (loads >= 6)
                break;
            const auto file = pluginthumbs::fileFor (t.key, t.native);
            if (file.existsAsFile())
            {
                t.picture = juce::ImageFileFormat::loadFrom (file);
                ++loads;
            }
            if (t.picture.isValid())
            {
                t.thumb = Tile::Thumb::loaded;
                repaint (t.bounds);
            }
            else
            {
                t.thumb = Tile::Thumb::missing;
                // a native one is being drawn in the background: look again soon; a captured one only on the next open
                t.nextLook = t.native ? now + 400.0 : 1.0e300;
                nativeMissing = nativeMissing || t.native;
            }
        }
        if (nativeMissing)
            pluginthumbs::ensureRendering();
    }

    const juce::Image& fittedPicture (Tile& t, juce::Rectangle<int> box, double scale)
    {
        if (t.scaled.isValid() && t.scaledFor == box && t.scaledAt == scale)
            return t.scaled;
        const auto& p = t.picture;
        const auto fit = juce::jmin ((double) box.getWidth() * scale / p.getWidth(), (double) box.getHeight() * scale / p.getHeight());
        const auto w = juce::jmax (1, (int) (p.getWidth() * fit)), h = juce::jmax (1, (int) (p.getHeight() * fit));
        t.scaled = p.rescaled (w, h, juce::Graphics::highResamplingQuality);
        t.scaledFor = box;
        t.scaledAt = scale;
        return t.scaled;
    }

    // --- painting --------------------------------------------------------------------------------------------------
    void paintBackdrop (juce::Graphics& g)
    {
        g.fillAll (theme::colour::bg0);
        if (backdrop.isValid())
        {
            g.setOpacity (0.45f);
            g.drawImage (backdrop, getLocalBounds().toFloat());
            g.setOpacity (1.0f);
        }
        g.fillAll (theme::colour::bg0.withAlpha (0.82f));
    }

    void paintTile (juce::Graphics& g, int i, double scale)
    {
        using namespace theme;
        auto& t = tiles[(std::size_t) i];
        const auto hover = i == hoverTile, chosen = i == selected;
        auto cell = t.bounds;
        const auto withLabel = cell.getWidth() >= 96;
        auto label = withLabel ? cell.removeFromBottom (labelHeight) : juce::Rectangle<int>();
        const auto box = cell.toFloat();

        if (t.thumb == Tile::Thumb::loaded)
        {
            const auto& image = fittedPicture (t, cell.reduced (2), scale);
            const auto w = (float) (image.getWidth() / scale), h = (float) (image.getHeight() / scale);
            const juce::Rectangle<float> at (box.getCentreX() - w / 2.0f, box.getCentreY() - h / 2.0f, w, h);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (at.translated (0.0f, 2.0f));
            g.drawImage (image, at);
            if (! hover && ! chosen)
            {
                g.setColour (colour::bg0.withAlpha (0.18f)); // (the one pointed at stands out)
                g.fillRect (at);
            }
            g.setColour (hover || chosen ? colour::accent : colour::border);
            g.drawRect (at.expanded (1.0f), hover || chosen ? 2.0f : 1.0f);
        }
        else
        {
            // a plain tile: LYNX's own plugins until their picture is drawn; a third-party plugin's maker and format
            const auto area = box.reduced (2.0f);
            g.setColour (hover ? colour::bg5 : colour::bg3);
            g.fillRoundedRectangle (area, radius::lg);
            g.setColour (hover || chosen ? colour::accent : colour::borderStrong);
            g.drawRoundedRectangle (area.reduced (0.5f), radius::lg, hover || chosen ? 2.0f : 1.0f);
            auto text = area.reduced (8.0f, 6.0f);
            g.setColour (colour::accent.withAlpha (0.85f));
            g.fillRect (text.removeFromTop (3.0f).withWidth (juce::jmin (28.0f, text.getWidth())));
            text.removeFromTop (4.0f);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption, true));
            g.drawFittedText (t.vendor.toUpperCase(), text.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            g.setColour (colour::textPrimary);
            g.setFont (font (text.getHeight() > 30.0f ? type::title : type::small, true));
            g.drawFittedText (t.name, text.toNearestInt(), juce::Justification::centred, 3, 0.75f);
            if (t.native && t.thumb != Tile::Thumb::loaded && pluginthumbs::rendering())
            {
                g.setColour (colour::textDisabled);
                g.setFont (font (type::caption));
                g.drawText ("...", area.removeFromBottom (14.0f), juce::Justification::centred);
            }
        }
        if (withLabel)
        {
            g.setColour (hover || chosen ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (type::small, hover || chosen));
            g.drawFittedText (t.name, label, juce::Justification::centred, 1, 0.75f);
        }
    }

    void paintStrip (juce::Graphics& g)
    {
        using namespace theme;
        // the tab strip: generator categories, "All", effect categories; the shown one white and bold with a mark above
        const auto strip = stripBounds();
        const auto searching = search.getText().trim().isNotEmpty();
        for (int i = 0; i < tabs.count(); ++i)
        {
            const auto r = tabBounds (i);
            if (r.getRight() < strip.getX() || r.getX() > strip.getRight())
                continue;
            const auto shown = i == tab && ! searching;
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
        // the generators' side and the effects' side
        if (tabs.all() > 0)
        {
            const auto left = tabBounds (0).getX(), right = tabBounds (tabs.all() - 1).getRight();
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawText (tr ("Generators"), juce::Rectangle<int> (left, strip.getY() - 6, right - left, 12), juce::Justification::centred);
        }
        if (tabs.count() > tabs.all() + 1)
        {
            const auto left = tabBounds (tabs.all() + 1).getX(), right = tabBounds (tabs.count() - 1).getRight();
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawText (tr ("Effects"), juce::Rectangle<int> (left, strip.getY() - 6, right - left, 12), juce::Justification::centred);
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (17.0f));
        if (stripOffset > 0)
            g.drawText ("...", strip.withWidth (24), juce::Justification::centred);
        if (stripWidth() - stripOffset > strip.getWidth())
            g.drawText ("...", strip.withTrimmedLeft (strip.getWidth() - 24), juce::Justification::centred);
    }

    void paintScrollbar (juce::Graphics& g)
    {
        if (maxScroll() <= 0)
            return;
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (scrollbarBounds().toFloat(), barWidth / 2.0f);
        g.setColour (draggingBar ? theme::colour::accent : theme::colour::bg6);
        g.fillRoundedRectangle (thumbBounds().toFloat(), barWidth / 2.0f);
    }

    // --- geometry --------------------------------------------------------------------------------------------------
    juce::String tabLabel (int i) const
    {
        return i == tabs.all() ? tr ("All") : tr (tabs.name (i).c_str());
    }

    juce::Rectangle<int> stripBounds() const { return getLocalBounds().removeFromBottom (stripHeight).reduced (8, 10); }
    juce::Rectangle<int> tileArea() const { return getLocalBounds().withTrimmedBottom (stripHeight).withTrimmedTop (headerHeight).reduced (24, 0); }
    juce::Rectangle<int> scrollbarBounds() const
    {
        return maxScroll() > 0 ? tileArea().removeFromRight (barWidth).translated (barWidth + 6, 0).reduced (0, 4) : juce::Rectangle<int>();
    }
    juce::Rectangle<int> thumbBounds() const
    {
        const auto bar = scrollbarBounds();
        if (bar.isEmpty())
            return {};
        const auto view = tileArea().getHeight();
        const auto h = juce::jmax (24, bar.getHeight() * view / juce::jmax (1, view + maxScroll()));
        return bar.withHeight (h).withY (bar.getY() + (bar.getHeight() - h) * scroll / juce::jmax (1, maxScroll()));
    }
    int maxScroll() const noexcept { return juce::jmax (0, contentHeight - tileArea().getHeight()); }

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
        if (! tileArea().contains (p))
            return -1;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(std::size_t) i].bounds.contains (p))
                return i;
        return -1;
    }

    std::vector<int> shownInOrder() const
    {
        std::vector<pickerview::Box> boxes;
        for (const auto& t : tiles)
            boxes.push_back ({ t.bounds.getX(), t.bounds.getY(), t.bounds.getWidth(), t.bounds.getHeight() });
        std::vector<int> order;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (! tiles[(std::size_t) i].bounds.isEmpty())
                order.push_back (i);
        // the generators' half first, then the effects' (reading order inside each)
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) {
            const auto& p = tiles[(std::size_t) a];
            const auto& q = tiles[(std::size_t) b];
            if (p.generator != q.generator)
                return p.generator;
            return p.bounds.getY() != q.bounds.getY() ? p.bounds.getY() < q.bounds.getY() : p.bounds.getX() < q.bounds.getX();
        });
        return order;
    }

    void setHover (int i)
    {
        if (i == hoverTile)
            return;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (tiles[(std::size_t) hoverTile].bounds.expanded (3));
        hoverTile = i;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (tiles[(std::size_t) hoverTile].bounds.expanded (3));
        setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void select (int i)
    {
        if (juce::isPositiveAndBelow (selected, (int) tiles.size()))
            repaint (tiles[(std::size_t) selected].bounds.expanded (3));
        selected = i;
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        // keep it in view
        const auto area = tileArea();
        const auto b = tiles[(std::size_t) i].bounds;
        if (b.getY() < area.getY())
            setScroll (scroll - (area.getY() - b.getY()) - gap);
        else if (b.getBottom() > area.getBottom())
            setScroll (scroll + (b.getBottom() - area.getBottom()) + gap);
        repaint (tiles[(std::size_t) i].bounds.expanded (3));
    }

    void moveSelection (int dx, int dy)
    {
        // across the two halves of "All": Left / Right step in each half's reading order, Up / Down stay in the half
        std::vector<pickerview::Box> boxes;
        const auto side = juce::isPositiveAndBelow (selected, (int) tiles.size()) ? tiles[(std::size_t) selected].generator : true;
        const auto order = shownInOrder();
        if (order.empty())
            return;
        if (dx != 0)
        {
            const auto it = std::find (order.begin(), order.end(), selected);
            if (it == order.end())
                select (order.front());
            else
                select (order[(std::size_t) juce::jlimit (0, (int) order.size() - 1, (int) (it - order.begin()) + dx)]);
            return;
        }
        for (const auto& t : tiles)
            boxes.push_back (t.generator == side ? pickerview::Box { t.bounds.getX(), t.bounds.getY(), t.bounds.getWidth(), t.bounds.getHeight() }
                                                 : pickerview::Box {});
        select (pickerview::moveSelection (boxes, juce::isPositiveAndBelow (selected, (int) tiles.size()) ? selected : -1, 0, dy));
    }

    void setScroll (int y)
    {
        const auto clamped = juce::jlimit (0, maxScroll(), y);
        if (clamped == scroll)
            return;
        const auto delta = clamped - scroll;
        scroll = clamped;
        for (auto& t : tiles)
            if (! t.bounds.isEmpty())
                t.bounds.translate (0, -delta);
        repaint();
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
        selected = -1;
        scroll = 0;
        layoutTiles();
        repaint();
    }

    // tiles in rows inside `area`; returns the height they take
    int flow (const std::vector<int>& items, juce::Rectangle<int> area, bool centre)
    {
        const auto f = pickerview::fitTiles ((int) items.size(), area.getWidth(), area.getHeight(), gap, 64, 220, 0.62);
        if (f.tileWidth <= 0)
            return 0;
        const auto rows = f.rows;
        const auto cellHeight = f.tileHeight + (f.tileWidth >= 96 ? labelHeight : 0);
        const auto usedHeight = rows * cellHeight + (rows - 1) * gap;
        auto y = area.getY() + (centre ? juce::jmax (0, (area.getHeight() - usedHeight) / 2) : 0);
        for (int r = 0; r < rows; ++r)
        {
            const auto first = r * f.columns;
            const auto inRow = juce::jmin (f.columns, (int) items.size() - first);
            const auto rowWidth = inRow * f.tileWidth + (inRow - 1) * gap;
            auto x = area.getX() + (centre ? (area.getWidth() - rowWidth) / 2 : 0);
            for (int c = 0; c < inRow; ++c)
            {
                tiles[(std::size_t) items[(std::size_t) (first + c)]].bounds = { x, y, f.tileWidth, cellHeight };
                x += f.tileWidth + gap;
            }
            y += cellHeight + gap;
        }
        return usedHeight;
    }

    void layoutTiles()
    {
        for (auto& t : tiles)
            t.bounds = {};
        const auto area = tileArea().withTrimmedRight (barWidth + 4);
        // (fitTiles counts the label row as part of the tile height through the aspect: leave it the room)
        std::vector<int> gens, fx;
        shownCount = 0;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tileShown (tiles[(std::size_t) i]))
            {
                (tiles[(std::size_t) i].generator ? gens : fx).push_back (i);
                ++shownCount;
            }
        if (tab == tabs.all() || search.getText().trim().isNotEmpty())
        {
            auto halves = area;
            const auto left = halves.removeFromLeft (halves.getWidth() / 2).withTrimmedRight (20);
            const auto right = halves.withTrimmedLeft (20);
            if (gens.empty() || fx.empty()) // (a search that found one kind only: the whole width)
                contentHeight = flow (gens.empty() ? fx : gens, area, false);
            else
                contentHeight = juce::jmax (flow (gens, left, false), flow (fx, right, false));
        }
        else
            contentHeight = flow (tabs.isGenerator (tab) ? gens : fx, area.withTrimmedTop (16), true) + 16; // a category's plugins centred (FL)
        // scrolled content keeps its place (a search change starts at the top)
        scroll = juce::jlimit (0, maxScroll(), scroll);
        for (auto& t : tiles)
            if (! t.bounds.isEmpty())
                t.bounds.translate (0, -scroll);
        if (selected >= 0 && tiles[(std::size_t) selected].bounds.isEmpty())
            selected = -1;
    }

    AudioEngine& engine;
    PluginPickerTargets targets;
    std::vector<Tile> tiles;
    pickerview::Tabs tabs;
    juce::TextEditor search;
    juce::Image backdrop;
    int tab = 0, hoverTab = -1, hoverTile = -1, selected = -1, pressedTile = -1, stripOffset = 0;
    int scroll = 0, contentHeight = 0, shownCount = 0, barGrab = 0;
    bool dragging = false, draggingBar = false;
    float wheelAccum = 0.0f;
    double buildMs = 0.0, backdropMs = 0.0;
};
} // namespace

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets)
{
    return std::make_unique<PluginPickerView> (engine, std::move (targets));
}
} // namespace lynx
