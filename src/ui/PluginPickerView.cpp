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
constexpr int defaultEditorWidth = 420, defaultEditorHeight = 260; // a tile with no picture (yet)

struct Tile
{
    juce::String name, detail, vendor;
    std::string key, category;
    bool generator = true, native = true;
    std::function<std::optional<ChannelDef>()> makeGenerator;
    std::function<std::optional<EffectSlot>()> makeEffect;

    int editorWidth = defaultEditorWidth, editorHeight = defaultEditorHeight; // its editor's size (logical px)
    bool hasPicture = false;   // a picture file exists (its size is known from the PNG header)
    bool matching = true;      // the search: false = dimmed (FL keeps the tile, dimmed)
    juce::Rectangle<int> bounds; // where it is drawn now (empty = not shown)

    // the picture: none yet / loaded / not there (a native one may still come from the background drawing)
    enum class Thumb { unknown, loaded, missing } thumb = Thumb::unknown;
    juce::Image picture;      // as kept on disk
    juce::Image scaled;       // fitted to the tile's current size (painting is a plain blit)
    juce::Point<int> scaledFor; // (its size: a scroll moves a tile without refitting it)
    double scaledAt = 0.0;    // the display scale it was made for
    double nextLook = 0.0;    // missing: when to look for the file again (ms)
    bool queued = false;      // a load / fit is on the worker thread
};

// the size of a PNG from its header (no decoding): {0, 0} when it is not one
juce::Point<int> pngSize (const juce::File& file)
{
    juce::FileInputStream in (file);
    std::uint8_t h[24] {};
    if (! in.openedOk() || in.read (h, 24) != 24 || h[1] != 'P' || h[2] != 'N' || h[3] != 'G')
        return {};
    auto be = [&] (int at) { return (int) ((std::uint32_t) h[at] << 24 | (std::uint32_t) h[at + 1] << 16 | (std::uint32_t) h[at + 2] << 8 | h[at + 3]); };
    return { be (16), be (20) };
}

// a picture scaled to w × h: halving steps, then the last one, all bilinear, in a software image (it runs on the
// picker's worker thread; JUCE's high-quality rescale took ~20 ms per picture)
juce::Image fitImage (const juce::Image& image, int w, int h)
{
    auto from = juce::SoftwareImageType().convert (image);
    while (from.getWidth() >= w * 2 && from.getHeight() >= h * 2)
        from = from.rescaled (from.getWidth() / 2, from.getHeight() / 2, juce::Graphics::mediumResamplingQuality);
    return from.rescaled (w, h, juce::Graphics::mediumResamplingQuality);
}

// FL Studio's Plugin picker (F8; REA_rodada169k §2.5 and REA_rodada171c §2, FL's TPlugListForm): a veil over the main
// window, every plugin as a picture of its editor, all at one scale (a big editor is a big tile), the categories along
// the bottom.
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
        setAlpha (0.0f); // FL fades the picker in; the mouse waits until it is mostly there
        openedAt = t0;
        startTimerHz (60);
        buildMs = juce::Time::getMillisecondCounterHiRes() - t0;
    }

    ~PluginPickerView() override
    {
        stopTimer();
        workers.removeAllJobs (true, 4000);
    }

    double constructionMs() const noexcept { return buildMs; }

    void parentHierarchyChanged() override
    {
        // the main window under a dark veil, taken once when the picker opens (the picker is opaque: a hover repaints
        // only the picker, never the mixer and playlist under it)
        if (auto* parent = getParentComponent(); parent != nullptr && ! backdrop.isValid() && parent->getWidth() > 0)
        {
            const auto shown = isVisible();
            setVisible (false);
            backdrop = parent->createComponentSnapshot (parent->getLocalBounds(), true, 0.5f);
            setVisible (shown);
        }
        if (isShowing())
            grabKeyboardFocus();
    }

    void visibilityChanged() override
    {
        if (isShowing())
            grabKeyboardFocus();
    }

    void focusLost (FocusChangeType) override
    {
        // FL's FormDeactivate: clicking another window closes the picker (not while a tile is being dragged)
        if (! dragging && isShowing())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginPickerView> (this)] {
                if (safe != nullptr && ! safe->dragging && ! safe->hasKeyboardFocus (true) && ! safe->isParentOf (juce::Component::getCurrentlyFocusedComponent()))
                    if (auto* focused = juce::Component::getCurrentlyFocusedComponent(); focused == nullptr || focused->getTopLevelComponent() != safe->getTopLevelComponent())
                        safe->close();
            });
    }

    void resized() override { layoutTiles(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg0);
        // the window behind, dimmed: composed once at this size (a full-window scaled draw per frame was most of a frame)
        const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (backdrop.isValid() && (backdropShown.getWidth() != juce::roundToInt (getWidth() * pixelScale)
                                   || backdropShown.getHeight() != juce::roundToInt (getHeight() * pixelScale)))
        {
            backdropShown = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt (getWidth() * pixelScale)),
                                         juce::jmax (1, juce::roundToInt (getHeight() * pixelScale)), false);
            juce::Graphics b (backdropShown);
            b.fillAll (colour::bg0);
            b.setOpacity (0.45f);
            b.drawImage (backdrop, backdropShown.getBounds().toFloat());
            b.setOpacity (1.0f);
            b.fillAll (colour::bg0.withAlpha (0.82f));
        }
        if (backdropShown.isValid())
            g.drawImage (backdropShown, getLocalBounds().toFloat());
        else
            g.fillAll (colour::bg0.withAlpha (0.82f));

        const auto bounds = getLocalBounds();
        const auto header = bounds.withHeight (headerHeight);
        g.setColour (colour::textSecondary);
        g.setFont (font (22.0f));
        if (tab == tabs.all())
        {
            g.drawText (tr ("Generators"), header.withWidth (bounds.getWidth() / 2), juce::Justification::centred);
            g.drawText (tr ("Effects"), header.withTrimmedLeft (bounds.getWidth() / 2), juce::Justification::centred);
        }
        paintSearch (g, header);

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (tileArea());
            const auto scale = pixelScale;
            paintScale = scale;
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (i != hoverTile && ! tiles[(std::size_t) i].bounds.isEmpty() && g.clipRegionIntersects (tiles[(std::size_t) i].bounds.expanded (12)))
                    paintTile (g, i, scale, false);
            if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size())) // on top, a little bigger, with its label
                paintTile (g, hoverTile, scale, true);
        }
        paintScrollbar (g);
        paintStrip (g);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        const auto t = tabAt (e.getPosition());
        if (t != hoverTab)
        {
            repaint (stripBounds());
            hoverTab = t;
        }
        if (t >= 0 && t != tab) // pointing at a tab shows it (FL)
            showTab (t);
        auto i = tileAt (e.getPosition());
        // (FL: a dimmed tile is only pointed at with Shift held)
        if (i >= 0 && ! tiles[(std::size_t) i].matching && ! e.mods.isShiftDown())
            i = -1;
        setHover (i);
        setMouseCursor (i >= 0 || searchBounds().contains (e.getPosition()) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverTab = -1;
        repaint (stripBounds());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        pressedTile = tileAt (e.getPosition());
        draggingBar = scrollbarBounds().contains (e.getPosition());
        if (draggingBar)
            barGrab = e.getPosition().y - thumbBounds().getY();
        if (e.mods.isPopupMenu() || e.mods.isMiddleButtonDown())
        {
            // FL: a right-click on a tile loads it; a right / middle click elsewhere closes the picker
            if (pressedTile >= 0 && e.mods.isPopupMenu())
                addTile (pressedTile);
            else if (pressedTile < 0 && ! draggingBar)
                close();
            pressedTile = -1;
            return;
        }
        if (pressedTile < 0 && searchBounds().contains (e.getPosition()) && searchText.isEmpty())
        {
            showPlaceholder = true; // FL: "start to type to search plugins...."
            repaint (searchBounds());
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // FL: a double-click loads the plugin (a single click does nothing)
        if (const auto i = tileAt (e.getPosition()); i >= 0 && ready() && ! dragging)
            addTile (i);
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
        // a tile dragged out: onto a mixer insert / slot, the Channel Rack or the Playlist; the picker gets out of the
        // way (FL closes it and starts the Browser's drag of that plugin)
        if (pressedTile >= 0 && ! dragging && e.getDistanceFromDragStart() > 6)
            startTileDrag (pressedTile);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        draggingBar = false;
        if (e.mouseWasDraggedSinceMouseDown() || dragging || ! ready() || ! e.mods.isLeftButtonDown())
            return;
        // FL: a click on a tab closes the picker and opens the Browser on that category of the Plugin database
        if (const auto t = tabAt (e.getPosition()); t >= 0)
        {
            auto open = targets.openBrowser;
            const auto category = t == tabs.all() ? std::string() : tabs.name (t);
            const auto generators = tabs.isGenerator (t);
            close();
            if (open)
                open (category, generators);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! ready())
            return;
        // over the scrollbar (or with Shift) it scrolls; Ctrl+wheel steps through the tiles (FL); else one tab per notch,
        // down = the next one to the right (FL)
        if ((e.mods.isShiftDown() || scrollbarBounds().expanded (8).contains (e.getPosition())) && maxScroll() > 0)
        {
            setScroll (scroll - juce::roundToInt ((wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX) * 400.0f));
            return;
        }
        wheelAccum += wheel.deltaY;
        const auto notches = (int) std::trunc (wheelAccum / 0.1f);
        if (notches == 0)
            return;
        wheelAccum -= (float) notches * 0.1f;
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            stepTile (-notches);
        else
            showTab (pickerview::stepTab (tabs, tab, -notches));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();
        if (code == juce::KeyPress::escapeKey || code == juce::KeyPress::F8Key) // (FL: F8 is let through, the picker closes)
        {
            close();
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::pageUpKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey)
        {
            showTab (pickerview::stepTab (tabs, tab, code == juce::KeyPress::rightKey || code == juce::KeyPress::pageDownKey ? 1 : -1));
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            stepTile (code == juce::KeyPress::downKey ? 1 : -1);
            return true;
        }
        if (code == juce::KeyPress::returnKey)
        {
            if (hoverTile >= 0)
                addTile (hoverTile);
            return true;
        }
        if (code == juce::KeyPress::backspaceKey)
        {
            setSearch (mods.isCtrlDown() ? pickerview::dropLastWord (searchText.toStdString()) : searchText.dropLastCharacters (1));
            return true;
        }
        if ((mods.isCtrlDown() || mods.isCommandDown()) && (code == 'V' || code == 'v'))
        {
            setSearch (searchText + juce::SystemClipboard::getTextFromClipboard().upToFirstOccurrenceOf ("\n", false, false).trim());
            return true;
        }
        // typing searches (FL: no Ctrl, a leading space ignored, at most 70 characters)
        const auto c = key.getTextCharacter();
        if (c >= ' ' && ! mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isCommandDown())
        {
            if (c == ' ' && searchText.isEmpty())
                return true;
            setSearch (searchText + juce::String::charToString (c));
            return true;
        }
        return false;
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        if (const auto i = tileAt (p); i >= 0)
            h.text = tiles[(std::size_t) i].name + " — "
                     + (tiles[(std::size_t) i].generator ? tr ("double-click, right-click or Enter: a new channel; drag it onto the Channel Rack (onto a channel: replaces its instrument)")
                                                         : tr ("double-click, right-click or Enter: into the selected mixer insert; drag it onto a mixer insert or one of its slots"));
        else if (const auto k = tabAt (p); k >= 0)
            h.text = tabLabel (k) + " — " + tr ("point at a category to show it; click it to open it in the Browser");
        return h;
    }

    int shownTab() const noexcept { return tab; }

private:
    static constexpr int headerHeight = 56, stripHeight = 64, gap = 10, barWidth = 8;
    static constexpr int maxSearch = 70;

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

        auto add = [&] (Tile t) {
            const auto file = pluginthumbs::fileFor (t.key, t.native);
            if (const auto size = pngSize (file); size.x > 0 && size.y > 0)
            {
                t.hasPicture = true;
                t.editorWidth = juce::roundToInt ((float) size.x / pluginthumbs::pictureScale);
                t.editorHeight = juce::roundToInt ((float) size.y / pluginthumbs::pictureScale);
            }
            tiles.push_back (std::move (t));
        };
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
            add (std::move (t));
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
            add (std::move (t));
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
        tabLefts.clear();
    }

    bool inTab (const Tile& t) const
    {
        return tab == tabs.all() || (t.generator == tabs.isGenerator (tab) && t.category == tabs.name (tab));
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

    void close()
    {
        if (auto c = targets.close)
            c();
    }

    bool ready() const noexcept { return getAlpha() >= 0.75f || dragging; } // (FL: the mouse waits for 75 % of the fade)

    void startTileDrag (int i)
    {
        auto& t = tiles[(std::size_t) i];
        juce::Image image;
        if (t.picture.isValid())
        {
            const auto scale = juce::jmin (1.0, 160.0 / t.picture.getWidth(), 120.0 / t.picture.getHeight());
            image = t.picture.rescaled (juce::jmax (1, (int) (t.picture.getWidth() * scale)), juce::jmax (1, (int) (t.picture.getHeight() * scale)),
                                        juce::Graphics::mediumResamplingQuality);
        }
        else
        {
            image = juce::Image (juce::Image::ARGB, 150, 40, true);
            juce::Graphics g (image);
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (image.getBounds().toFloat(), theme::radius::md);
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::body, true));
            g.drawFittedText (t.name, image.getBounds().reduced (6, 2), juce::Justification::centred, 2);
        }
        if (! BrowserComponent::startPluginDrag (*this, t.generator ? t.makeGenerator : nullptr, t.generator ? nullptr : t.makeEffect, t.name, image))
            return;
        // FL: the picker closes as the drag starts (here it turns invisible and lets the mouse through until the drop:
        // the drag needs its source) and an effect brings up the Mixer
        dragging = true;
        setAlpha (0.0f);
        setInterceptsMouseClicks (false, false);
        if (! t.generator && targets.showMixer)
            targets.showMixer();
    }

    // --- search ----------------------------------------------------------------------------------------------------
    void setSearch (juce::String text)
    {
        searchText = text.substring (0, maxSearch);
        showPlaceholder = false;
        const auto query = searchText.toStdString();
        for (auto& t : tiles)
        {
            const auto was = t.matching;
            t.matching = query.empty() || pickerview::nameMatches (query, t.name.toStdString());
            if (was != t.matching && ! t.bounds.isEmpty())
                repaint (t.bounds.expanded (12));
        }
        repaint (searchBounds());
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()) && ! tiles[(std::size_t) hoverTile].matching)
            setHover (-1); // (a dimmed tile isn't pointed at)
        // FL: the hover jumps to the first name that starts with the text, else to the last one that contains it
        if (! query.empty())
        {
            std::vector<std::string> names;
            std::vector<int> index;
            for (const auto i : shownInOrder())
            {
                names.push_back (tiles[(std::size_t) i].name.toStdString());
                index.push_back (i);
            }
            if (const auto k = pickerview::searchJump (query, names); k >= 0)
                focusTile (index[(std::size_t) k]);
        }
    }

    // --- pictures and the fade-in ----------------------------------------------------------------------------------
    void timerCallback() override
    {
        if (dragging)
        {
            // the drag ended (dropped or cancelled): the picker closes
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container == nullptr || ! container->isDragAndDropActive())
                close();
            return;
        }
        if (getAlpha() < 1.0f)
            setAlpha ((float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - openedAt) / fadeMs));

        // lazily: only the tiles shown. The PNG decode and the fit to the tile's size run on a worker thread (on the
        // message thread they took the whole frame: Windows paints only when no other message waits)
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now < nextLoadAt)
            return;
        nextLoadAt = now + 33.0;
        bool nativeMissing = false;
        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            auto& t = tiles[(std::size_t) i];
            if (t.bounds.isEmpty() || t.queued || ! tileArea().intersects (t.bounds))
                continue;
            if (t.thumb == Tile::Thumb::loaded)
            {
                if (! fittedNow (t))
                    queueFit (i, false);
                continue;
            }
            if (t.thumb == Tile::Thumb::missing && now < t.nextLook)
            {
                nativeMissing = nativeMissing || t.native;
                continue;
            }
            if (inFlight >= 8)
                break;
            const auto file = pluginthumbs::fileFor (t.key, t.native);
            if (file.existsAsFile())
                queueFit (i, true);
            else
            {
                t.thumb = Tile::Thumb::missing;
                // a native one is being drawn in the background: look again soon; a captured one only on the next open
                t.nextLook = t.native ? now + 400.0 : 1.0e300;
                nativeMissing = nativeMissing || t.native;
            }
        }
        if (relayoutPending && now - lastRelayout > 400.0)
        {
            relayoutPending = false;
            lastRelayout = now;
            layoutTiles();
            repaint();
        }
        if (nativeMissing)
            pluginthumbs::ensureRendering();
    }

    bool fittedNow (const Tile& t) const
    {
        return t.scaled.isValid() && t.scaledFor == juce::Point<int> (t.bounds.getWidth(), t.bounds.getHeight()) && t.scaledAt == paintScale;
    }

    // a job for the worker: load the picture (`load`) and / or fit it to the tile's size now
    void queueFit (int i, bool load)
    {
        auto& t = tiles[(std::size_t) i];
        if (inFlight >= 8 && ! load)
            return;
        const juce::Point<int> size (t.bounds.getWidth(), t.bounds.getHeight());
        const auto scale = paintScale;
        const auto file = load ? pluginthumbs::fileFor (t.key, t.native) : juce::File();
        const auto picture = t.picture;
        t.queued = true;
        ++inFlight;
        juce::Component::SafePointer<PluginPickerView> safe (this);
        workers.addJob ([safe, i, file, picture, size, scale] {
            auto source = file != juce::File() ? juce::ImageFileFormat::loadFrom (file) : picture;
            if (source.isValid())
                source = juce::SoftwareImageType().convert (source);
            const auto w = juce::jmax (1, juce::roundToInt (size.x * scale)), h = juce::jmax (1, juce::roundToInt (size.y * scale));
            auto fitted = source.isValid() ? fitImage (source, w, h) : juce::Image();
            juce::MessageManager::callAsync ([safe, i, source, fitted, size, scale] {
                if (safe != nullptr)
                    safe->fitDone (i, source, fitted, size, scale);
            });
        });
    }

    void fitDone (int i, const juce::Image& source, const juce::Image& fitted, juce::Point<int> size, double scale)
    {
        --inFlight;
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        auto& t = tiles[(std::size_t) i];
        t.queued = false;
        if (! source.isValid())
        {
            t.thumb = Tile::Thumb::missing;
            t.nextLook = t.native ? juce::Time::getMillisecondCounterHiRes() + 400.0 : 1.0e300;
            return;
        }
        if (t.thumb != Tile::Thumb::loaded)
        {
            // (drawn as the window's own kind of image: converted once here, not at every paint)
            t.picture = juce::NativeImageType().convert (source);
            t.thumb = Tile::Thumb::loaded;
            const auto w = juce::roundToInt ((float) source.getWidth() / pluginthumbs::pictureScale);
            const auto h = juce::roundToInt ((float) source.getHeight() / pluginthumbs::pictureScale);
            if (! t.hasPicture || std::abs (w - t.editorWidth) > 2 || std::abs (h - t.editorHeight) > 2)
                relayoutPending = true; // (a picture drawn while the picker was open: its tile takes its size)
            t.hasPicture = true;
            t.editorWidth = w;
            t.editorHeight = h;
        }
        if (fitted.isValid() && size == juce::Point<int> (t.bounds.getWidth(), t.bounds.getHeight()))
        {
            t.scaled = juce::NativeImageType().convert (fitted);
            t.scaledFor = size;
            t.scaledAt = scale;
        }
        repaint (t.bounds.expanded (12));
    }

    // --- painting --------------------------------------------------------------------------------------------------
    juce::Rectangle<int> searchBounds() const { return getLocalBounds().withHeight (headerHeight).withSizeKeepingCentre (360, 36); }

    void paintSearch (juce::Graphics& g, juce::Rectangle<int>)
    {
        using namespace theme;
        const auto area = searchBounds();
        if (searchText.isEmpty() && ! showPlaceholder)
            return;
        // FL: a magnifier and the text at the top centre, in its highlight colour (LYNX's red accent)
        const auto text = searchText.isEmpty() ? tr ("start to type to search plugins....") : searchText;
        const auto f = juce::Font (font (searchText.isEmpty() ? type::title : 22.0f));
        const auto textWidth = juce::jmin (area.getWidth() - 30, juce::GlyphArrangement::getStringWidthInt (f, text) + 4);
        const auto x = area.getCentreX() - (textWidth + 26) / 2;
        const auto glass = juce::Rectangle<float> ((float) x, (float) area.getCentreY() - 8.0f, 14.0f, 14.0f);
        g.setColour (searchText.isEmpty() ? colour::textSecondary : colour::accent);
        g.drawEllipse (glass.reduced (1.5f), 2.0f);
        g.drawLine (glass.getRight() - 3.0f, glass.getBottom() - 3.0f, glass.getRight() + 3.0f, glass.getBottom() + 3.0f, 2.2f);
        g.setFont (f);
        g.drawText (text, juce::Rectangle<int> (x + 26, area.getY(), textWidth, area.getHeight()), juce::Justification::centredLeft, true);
    }

    void paintTile (juce::Graphics& g, int i, double scale, bool hover)
    {
        using namespace theme;
        auto& t = tiles[(std::size_t) i];
        const auto dim = ! t.matching;
        // the tile pointed at: a little bigger, a light frame and a glow (FL), in LYNX's accent
        const auto cell = hover ? t.bounds.toFloat().withSizeKeepingCentre (t.bounds.getWidth() * 1.08f, t.bounds.getHeight() * 1.08f) : t.bounds.toFloat();
        if (hover)
        {
            for (int k = 3; k >= 1; --k)
            {
                g.setColour (colour::accent.withAlpha (0.07f * (float) k));
                g.fillRoundedRectangle (cell.expanded ((float) (4 - k) * 3.0f), radius::lg + (float) (4 - k) * 2.0f);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (cell.translated (0.0f, 2.0f));

        if (t.thumb == Tile::Thumb::loaded)
        {
            // the picture fitted to the tile once (the one pointed at is that one drawn 8 % bigger); until it is fitted
            // (the timer fits a few per frame) the kept picture is drawn scaled on the fly
            const auto fitted = fittedNow (t);
            juce::Graphics::ScopedSaveState quality (g);
            if (! fitted)
                g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.setOpacity (dim ? 0.22f : 1.0f);
            g.drawImage (fitted ? t.scaled : t.picture, cell);
            g.setOpacity (1.0f);
            if (! hover && ! dim)
            {
                g.setColour (colour::bg0.withAlpha (0.12f)); // (the one pointed at stands out)
                g.fillRect (cell);
            }
        }
        else
        {
            // a plain tile: LYNX's own plugins until their picture is drawn; a third-party plugin's maker and name
            g.setColour (colour::bg3.withMultipliedAlpha (dim ? 0.35f : 1.0f));
            g.fillRect (cell);
            auto text = cell.reduced (juce::jmin (8.0f, cell.getWidth() * 0.06f), juce::jmin (6.0f, cell.getHeight() * 0.08f));
            const auto alpha = dim ? 0.3f : 1.0f;
            g.setColour (colour::accent.withAlpha (0.85f * alpha));
            g.fillRect (text.removeFromTop (3.0f).withWidth (juce::jmin (28.0f, text.getWidth())));
            text.removeFromTop (3.0f);
            if (text.getHeight() > 40.0f)
            {
                g.setColour (colour::textSecondary.withAlpha (alpha));
                g.setFont (font (type::caption, true));
                g.drawFittedText (t.vendor.toUpperCase(), text.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
            }
            g.setColour (colour::textPrimary.withAlpha (alpha));
            g.setFont (font (juce::jlimit (9.0f, 16.0f, text.getHeight() * 0.3f), true));
            g.drawFittedText (t.name, text.toNearestInt(), juce::Justification::centred, 3, 0.7f);
        }
        g.setColour (hover ? colour::textPrimary.withAlpha (0.8f) : colour::border.withMultipliedAlpha (dim ? 0.4f : 1.0f));
        g.drawRect (cell.expanded (1.0f), hover ? 1.5f : 1.0f);

        if (hover)
        {
            // FL's dark label under it: the name, then the category
            const auto category = categoryText (t);
            const auto nameFont = juce::Font (font (17.0f, true)), catFont = juce::Font (font (type::body));
            const auto box = labelBox (t, cell);
            g.setColour (colour::bg0.withAlpha (0.94f));
            g.fillRoundedRectangle (box, radius::md);
            g.setColour (colour::border);
            g.drawRoundedRectangle (box, radius::md, 1.0f);
            auto lines = box.reduced (8.0f, 4.0f);
            g.setColour (colour::textPrimary);
            g.setFont (nameFont);
            g.drawText (t.name, lines.removeFromTop (20.0f), juce::Justification::centredLeft);
            g.setColour (colour::textSecondary);
            g.setFont (catFont);
            g.drawText (category, lines, juce::Justification::centredLeft);
        }
    }

    juce::String categoryText (const Tile& t) const
    {
        return (t.generator ? tr ("Generators") : tr ("Effects")) + " / " + tr (t.category.c_str());
    }

    // the label under the tile pointed at (`cell` = the tile as drawn, 8 % bigger), kept inside the tile area
    juce::Rectangle<float> labelBox (const Tile& t, juce::Rectangle<float> cell) const
    {
        const auto nameFont = juce::Font (theme::font (17.0f, true)), catFont = juce::Font (theme::font (theme::type::body));
        const auto w = juce::jmax (juce::GlyphArrangement::getStringWidthInt (nameFont, t.name), juce::GlyphArrangement::getStringWidthInt (catFont, categoryText (t))) + 16;
        return juce::Rectangle<float> ((float) w, 44.0f).withCentre ({ cell.getCentreX(), cell.getBottom() + 26.0f })
            .constrainedWithin (tileArea().toFloat().withTrimmedBottom (-headerHeight));
    }

    void paintStrip (juce::Graphics& g)
    {
        using namespace theme;
        // the tab strip: generator categories, "All", effect categories; the shown one white and bold with a mark above
        const auto strip = stripBounds();
        for (int i = 0; i < tabs.count(); ++i)
        {
            const auto r = tabBounds (i);
            if (! tabVisible (r))
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

    // the tabs' widths and left edges, measured once (a paint measured every label ~n² times: ~35 ms per frame)
    void measureTabs() const
    {
        if ((int) tabLefts.size() == tabs.count() + 1)
            return;
        tabLefts.assign (1, 0);
        for (int i = 0; i < tabs.count(); ++i)
        {
            const auto f = juce::Font (theme::font (i == tabs.all() ? 26.0f : 17.0f, true));
            tabLefts.push_back (tabLefts.back() + juce::GlyphArrangement::getStringWidthInt (f, tabLabel (i)) + 34);
        }
    }

    int tabWidth (int i) const
    {
        measureTabs();
        return tabLefts[(std::size_t) i + 1] - tabLefts[(std::size_t) i];
    }

    int stripWidth() const
    {
        measureTabs();
        return tabLefts.back();
    }

    juce::Rectangle<int> tabBounds (int i) const
    {
        const auto strip = stripBounds();
        // the strip is centred on "All" when it fits, else scrolled so the shown tab stays in view
        measureTabs();
        const auto x = strip.getX() + juce::jmax (0, (strip.getWidth() - stripWidth()) / 2) - stripOffset + tabLefts[(std::size_t) i];
        return { x, strip.getY() + 10, tabWidth (i), strip.getHeight() - 10 };
    }

    // a tab is drawn (and clickable) only when it is wholly inside the strip, clear of the "..." at a cut end (FL's ends)
    bool tabVisible (juce::Rectangle<int> r) const
    {
        auto inner = stripBounds();
        if (stripOffset > 0)
            inner.removeFromLeft (24);
        if (stripWidth() - stripOffset > stripBounds().getWidth())
            inner.removeFromRight (24);
        return r.getX() >= inner.getX() && r.getRight() <= inner.getRight();
    }

    int tabAt (juce::Point<int> p) const
    {
        if (! stripBounds().contains (p))
            return -1;
        for (int i = 0; i < tabs.count(); ++i)
            if (tabVisible (tabBounds (i)) && tabBounds (i).contains (p))
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

    // the tiles shown, generators first, each half in reading order (FL's tile list order)
    std::vector<int> shownInOrder() const
    {
        std::vector<int> order;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (! tiles[(std::size_t) i].bounds.isEmpty())
                order.push_back (i);
        std::stable_sort (order.begin(), order.end(), [&] (int a, int b) {
            const auto& p = tiles[(std::size_t) a];
            const auto& q = tiles[(std::size_t) b];
            // the list order = the layout order (rows are packed in list order; a row's tiles are centred on its height,
            // so their tops differ and can't give the order)
            return p.generator != q.generator && p.generator;
        });
        return order;
    }

    void setHover (int i)
    {
        if (i == hoverTile)
            return;
        auto area = [this] (int k) {
            const auto& t = tiles[(std::size_t) k];
            const auto cell = t.bounds.toFloat().withSizeKeepingCentre (t.bounds.getWidth() * 1.08f, t.bounds.getHeight() * 1.08f);
            return t.bounds.expanded (t.bounds.getWidth() / 10 + 14, 14).getUnion (labelBox (t, cell).expanded (4.0f).getSmallestIntegerContainer());
        };
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (area (hoverTile));
        hoverTile = i;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (area (hoverTile));
    }

    // Up / Down, Ctrl+wheel (FL): the next / previous tile in the list, wrapping, skipping dimmed ones; it is shown
    // as pointed at and the mouse pointer moves onto it (FL)
    void stepTile (int delta)
    {
        const auto order = shownInOrder();
        std::vector<char> usable;
        for (const auto i : order)
            usable.push_back (tiles[(std::size_t) i].matching ? 1 : 0);
        const auto at = std::find (order.begin(), order.end(), hoverTile);
        const auto k = pickerview::stepWrapping (usable, at == order.end() ? -1 : (int) (at - order.begin()), delta);
        if (k >= 0)
            focusTile (order[(std::size_t) k]);
    }

    void focusTile (int i)
    {
        setHover (i);
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        // keep it in view
        const auto area = tileArea();
        const auto b = tiles[(std::size_t) i].bounds;
        if (b.getY() < area.getY())
            setScroll (scroll - (area.getY() - b.getY()) - gap);
        else if (b.getBottom() > area.getBottom())
            setScroll (scroll + (b.getBottom() - area.getBottom()) + gap);
        // FL moves the real mouse pointer onto it (SetPhysicalCursorPos), so a click or a drag goes on from there
        if (isShowing())
            juce::Desktop::setMousePosition (localPointToGlobal (tiles[(std::size_t) i].bounds.getCentre()));
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
            measureTabs();
            const auto left = tabLefts[(std::size_t) tab];
            stripOffset = juce::jlimit (0, over, left + tabWidth (tab) / 2 - strip.getWidth() / 2);
        }
        else
            stripOffset = 0;
        hoverTile = -1;
        scroll = 0;
        layoutTiles();
        repaint();
    }

    // the tiles of one half at one scale: every picture at the same scale (FL: a big editor is a big tile), rows
    // packed left to right; `centre`: each row centred (a category alone)
    std::vector<pickerview::Box> sizesOf (const std::vector<int>& items) const
    {
        std::vector<pickerview::Box> sizes;
        for (const auto i : items)
            sizes.push_back ({ 0, 0, tiles[(std::size_t) i].editorWidth, tiles[(std::size_t) i].editorHeight });
        return sizes;
    }

    // the scale at which `items` fill `area` (FL: the largest one at which they all fit)
    double fitScale (const std::vector<int>& items, juce::Rectangle<int> area) const
    {
        return items.empty() ? 1.0 : pickerview::packRows (sizesOf (items), area.getWidth(), area.getHeight(), gap, 0.06, 0.45).scale;
    }

    // `scale` > 0: at that scale (the All tab: both halves at one scale, as FL draws them), else the largest that fits
    int flow (const std::vector<int>& items, juce::Rectangle<int> area, bool centre, double scale = 0.0)
    {
        if (items.empty())
            return 0;
        const auto sizes = sizesOf (items);
        const auto packed = scale > 0.0 ? pickerview::packAt (sizes, area.getWidth(), gap, scale)
                                        : pickerview::packRows (sizes, area.getWidth(), area.getHeight(), gap, 0.06, 0.45);
        for (std::size_t k = 0; k < items.size(); ++k)
        {
            auto b = packed.boxes[k];
            if (centre)
                b.x += packed.rowSlack[(std::size_t) packed.rowOf[k]] / 2;
            tiles[(std::size_t) items[k]].bounds = { area.getX() + b.x, area.getY() + b.y, b.w, b.h };
        }
        return packed.height;
    }

    void layoutTiles()
    {
        for (auto& t : tiles)
            t.bounds = {};
        const auto area = tileArea().withTrimmedRight (barWidth + 4);
        std::vector<int> gens, fx;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (inTab (tiles[(std::size_t) i]))
                (tiles[(std::size_t) i].generator ? gens : fx).push_back (i);
        if (tab == tabs.all())
        {
            auto halves = area;
            const auto left = halves.removeFromLeft (halves.getWidth() / 2).withTrimmedRight (20);
            const auto right = halves.withTrimmedLeft (20);
            const auto scale = juce::jmin (fitScale (gens, left), fitScale (fx, right));
            contentHeight = juce::jmax (flow (gens, left, false, scale), flow (fx, right, false, scale));
        }
        else
            contentHeight = flow (tabs.isGenerator (tab) ? gens : fx, area.withTrimmedTop (16), true) + 16; // a category's plugins centred (FL)
        scroll = juce::jlimit (0, maxScroll(), scroll);
        for (auto& t : tiles)
            if (! t.bounds.isEmpty())
                t.bounds.translate (0, -scroll);
        if (hoverTile >= 0 && tiles[(std::size_t) hoverTile].bounds.isEmpty())
            hoverTile = -1;
    }

    AudioEngine& engine;
    PluginPickerTargets targets;
    std::vector<Tile> tiles;
    pickerview::Tabs tabs;
    juce::Image backdrop, backdropShown;
    mutable std::vector<int> tabLefts; // (measureTabs)
    double paintScale = 1.0;
    juce::ThreadPool workers { 2 };
    int inFlight = 0;
    bool relayoutPending = false;
    juce::String searchText;
    bool showPlaceholder = false;
    int tab = 0, hoverTab = -1, hoverTile = -1, pressedTile = -1, stripOffset = 0;
    int scroll = 0, contentHeight = 0, barGrab = 0;
    bool dragging = false, draggingBar = false;
    float wheelAccum = 0.0f;
    double buildMs = 0.0, openedAt = 0.0, nextLoadAt = 0.0, lastRelayout = 0.0;
    static constexpr double fadeMs = 140.0;
};
} // namespace

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets)
{
    return std::make_unique<PluginPickerView> (engine, std::move (targets));
}
} // namespace lynx
