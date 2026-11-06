#include "ui/PluginPickerView.h"

#include "core/PickerView.h"
#include "core/PluginCatalog.h"
#include "ui/AutomationUi.h"
#include "ui/BrowserComponent.h"
#include "ui/I18n.h"
#include "ui/PluginThumbs.h"
#include "ui/QuickPick.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace lynx
{
namespace
{
constexpr const char* installedTab = "Installed"; // third-party plugins in no folder of the Plugin database (not listed: FL)

// FL's TPlugListForm measures (REA_rodada175b §1–3; all × the UI scale, 1 here)
constexpr int edgeX = 32, edgeTop = 56, edgeBottom = 96, halvesGap = 80; // the tile box inside the form
constexpr int tileGap = 8;                                              // between tiles in a row
constexpr int shadow = 3;                                               // the drop shadow around a picture
constexpr float textTileSize = 17.0f, tabSize = 19.0f, allSize = 26.0f, titleSize = 26.0f;
// (FL draws the label in Cuprum 21 / 17, a tall face: these sizes give LYNX's face the same cap heights on screen,
// measured on fl-1920-dynamics-hover: name ~15 px, category ~11 px)
constexpr float labelNameSize = 25.0f, labelCategorySize = 20.0f, labelPadX = 10.0f, labelPadY = 4.0f;

struct Tile
{
    juce::String name, detail;
    std::string key, category;
    bool generator = true, native = true;
    std::function<std::optional<ChannelDef>()> makeGenerator;
    std::function<std::optional<EffectSlot>()> makeEffect;

    juce::Point<int> png;      // the picture's size in its file (FL's budget: half the editor, ≤ 258 × 128); 0 = none
    bool matching = true;      // the search: false = dimmed (FL keeps the tile, dimmed)
    juce::Rectangle<int> bounds; // the tile (picture + shadow + 1 px), empty = not shown
    int rowHeight = 0;

    enum class Thumb { unknown, loaded, missing } thumb = Thumb::unknown;
    juce::Image scaled;        // the picture at its shown size, in physical pixels
    double scaledAt = 0.0;     // (for that display scale)
    double nextLook = 0.0;     // missing: when to look for the file again (ms)
    bool queued = false;

    bool hasPicture() const noexcept { return png.x > 0 && png.y > 0; }
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

// a picture file's size within FL's budget (an older, bigger picture is shown as if it were made for it)
juce::Point<int> budgeted (juce::Point<int> size)
{
    if (size.x <= 0 || size.y <= 0)
        return {};
    const auto k = juce::jmin (1.0, (double) pluginthumbs::maxWidth / size.x, (double) pluginthumbs::maxHeight / size.y);
    return { juce::jmax (1, juce::roundToInt (size.x * k)), juce::jmax (1, juce::roundToInt (size.y * k)) };
}

// a picture scaled to w × h: halving steps, then the last one (on the picker's worker thread)
juce::Image fitImage (const juce::Image& image, int w, int h)
{
    auto from = juce::SoftwareImageType().convert (image);
    while (from.getWidth() >= w * 2 && from.getHeight() >= h * 2)
        from = from.rescaled (from.getWidth() / 2, from.getHeight() / 2, juce::Graphics::mediumResamplingQuality);
    return from.getWidth() == w && from.getHeight() == h ? from : from.rescaled (w, h, juce::Graphics::highResamplingQuality);
}

// FL multiplies a picker bitmap by 1.1 (REA_rodada175b §2): done once, on the worker thread
void brighten (juce::Image& image)
{
    if (! image.isValid())
        return;
    juce::Image::BitmapData data (image, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < data.height; ++y)
        for (int x = 0; x < data.width; ++x)
        {
            const auto c = data.getPixelColour (x, y);
            const auto up = [] (juce::uint8 v) { return (juce::uint8) juce::jmin (255, (v * 11 + 5) / 10); };
            data.setPixelColour (x, y, juce::Colour (up (c.getRed()), up (c.getGreen()), up (c.getBlue()), c.getAlpha()));
        }
}

// What the screen showed under the picker when it opened (FL's form is see-through: the desktop and FL show through
// its veil). Windows: the screen's pixels; elsewhere: nothing (the veil alone).
juce::Image captureScreen (juce::Rectangle<int> physical)
{
#if JUCE_WINDOWS
    if (physical.isEmpty())
        return {};
    const auto w = physical.getWidth(), h = physical.getHeight();
    auto* screen = GetDC (nullptr);
    auto* dc = CreateCompatibleDC (screen);
    BITMAPINFO info {};
    info.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    auto* bitmap = CreateDIBSection (dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    juce::Image out;
    if (bitmap != nullptr && bits != nullptr)
    {
        auto* old = SelectObject (dc, bitmap);
        if (BitBlt (dc, 0, 0, w, h, screen, physical.getX(), physical.getY(), SRCCOPY))
        {
            GdiFlush();
            out = juce::Image (juce::Image::RGB, w, h, false, juce::SoftwareImageType());
            juce::Image::BitmapData data (out, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                const auto* row = static_cast<const std::uint8_t*> (bits) + (std::size_t) y * (std::size_t) w * 4;
                auto* dest = data.getLinePointer (y);
                for (int x = 0; x < w; ++x)
                {
                    // (JUCE's RGB images are B, G, R in memory on little-endian machines, as the DIB is)
                    dest[x * data.pixelStride + 0] = row[x * 4 + 0];
                    dest[x * data.pixelStride + 1] = row[x * 4 + 1];
                    dest[x * data.pixelStride + 2] = row[x * 4 + 2];
                }
            }
        }
        SelectObject (dc, old);
        DeleteObject (bitmap);
    }
    DeleteDC (dc);
    ReleaseDC (nullptr, screen);
    return out;
#else
    juce::ignoreUnused (physical);
    return {};
#endif
}

// FL Studio's Plugin picker (F8; REA_rodada169k §2.5, REA_rodada171c §2, REA_rodada175b): a see-through veil over the
// whole screen, every plugin as a picture of its editor at FL's own scale, packed in rows like FL, the categories
// along the bottom.
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

    // the screen under the picker, taken before it shows (physical pixels of `logical`, the picker's area)
    void takeBackdrop (juce::Rectangle<int> logical)
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (logical);
        const auto scale = display != nullptr ? display->scale : 1.0;
        backdrop = captureScreen ((logical.toDouble() * scale).toNearestInt());
        backdropShown = {};
    }

    void parentHierarchyChanged() override
    {
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
                if (safe == nullptr || safe->dragging || safe->hasKeyboardFocus (true))
                    return;
                auto* focused = juce::Component::getCurrentlyFocusedComponent();
                if (focused == nullptr || focused->getTopLevelComponent() != safe->getTopLevelComponent())
                    safe->close();
            });
    }

    void resized() override
    {
        tabLayoutFor = {};
        layoutTiles();
    }

    void paint (juce::Graphics& g) override
    {
        const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (pixelScale != paintScale && ! isTimerRunning())
            startTimerHz (60); // (the pictures are refitted for the new display scale)
        paintScale = pixelScale;
        paintVeil (g, pixelScale);

        // FL's titles over the halves, on the All tab only
        if (tab == tabs.all())
        {
            g.setColour (theme::colour::textSecondary.brighter (0.25f));
            g.setFont (theme::font (titleSize));
            const auto band = getLocalBounds().withHeight (juce::roundToInt (titleSize * 3.0f));
            g.drawText (tr ("Generators"), band.withWidth (getWidth() / 2), juce::Justification::centred);
            g.drawText (tr ("Effects"), band.withTrimmedLeft (getWidth() / 2), juce::Justification::centred);
        }
        paintSearch (g);

        for (int i = 0; i < (int) tiles.size(); ++i)
            if (i != hoverTile && ! tiles[(std::size_t) i].bounds.isEmpty() && g.clipRegionIntersects (tiles[(std::size_t) i].bounds.expanded (4)))
                paintTile (g, i, false);
        paintStrip (g);
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size())) // on top, a little bigger, with its label
        {
            paintTile (g, hoverTile, true);
            shownHoverArea = hoverArea (hoverTile);
        }
        else
            shownHoverArea = {};
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        const auto t = tabAt (e.getPosition());
        if (t != hoverTab)
        {
            if (hoverTab != -1)
                repaint (tabHighlight (hoverTab).getSmallestIntegerContainer().expanded (4));
            hoverTab = t;
            if (hoverTab != -1)
                repaint (tabHighlight (hoverTab).getSmallestIntegerContainer().expanded (4));
        }
        if (t >= 0 && t != tab) // pointing at a tab shows it (FL)
            showTab (t);
        auto i = tileAt (e.getPosition());
        // (FL: a dimmed tile is only pointed at with Shift held)
        if (i >= 0 && ! tiles[(std::size_t) i].matching && ! e.mods.isShiftDown())
            i = -1;
        mouseX = e.x;
        if (i >= 0 && i == hoverTile)
        {
            repaint (shownHoverArea); // (the label follows the mouse: where it was, and where it goes)
            repaint (hoverArea (i));
        }
        setHover (i);
        setMouseCursor (i >= 0 || t >= 0 || ellipsisAt (e.getPosition()) != 0 || searchBounds().contains (e.getPosition())
                            ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverTab != -1)
            repaint (tabHighlight (hoverTab).getSmallestIntegerContainer().expanded (4));
        hoverTab = -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        pressedTile = tileAt (e.getPosition());
        if (e.mods.isPopupMenu() || e.mods.isMiddleButtonDown())
        {
            // FL: a right-click on a tile loads it; a right / middle click elsewhere closes the picker
            if (pressedTile >= 0 && e.mods.isPopupMenu())
                addTile (pressedTile);
            else if (pressedTile < 0)
                close();
            pressedTile = -1;
            return;
        }
        if (pressedTile >= 0)
            repaint (hoverArea (pressedTile)); // (FL's highlight turns orange while pressed: LYNX's accent)
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
        // a tile dragged out: onto a mixer insert / slot, the Channel Rack or the Playlist; the picker gets out of the
        // way (FL closes it and starts the Browser's drag of that plugin)
        if (pressedTile >= 0 && ! dragging && e.getDistanceFromDragStart() > 6)
            startTileDrag (pressedTile);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (pressedTile >= 0 && ! dragging)
            repaint (hoverArea (pressedTile));
        pressedTile = -1;
        if (e.mouseWasDraggedSinceMouseDown() || dragging || ! ready() || ! e.mods.isLeftButtonDown())
            return;
        // FL: a click on a tab closes the picker and opens the Browser on that category of the Plugin database; the
        // "..." at the ends open Plugin database › Installed › Generators / Effects
        if (const auto t = tabAt (e.getPosition()); t >= 0)
        {
            auto open = targets.openBrowser;
            const auto category = t == tabs.all() ? std::string() : tabs.name (t);
            const auto generators = tabs.isGenerator (t);
            close();
            if (open)
                open (category, generators);
        }
        else if (const auto end = ellipsisAt (e.getPosition()); end != 0)
        {
            auto open = targets.openBrowser;
            close();
            if (open)
                open (installedTab, end < 0);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! ready())
            return;
        // Ctrl+wheel steps through the tiles (FL); else one tab per notch, down = the next one to the right (FL)
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
            setSearch (mods.isCtrlDown() ? juce::String (pickerview::dropLastWord (searchText.toStdString())) : searchText.dropLastCharacters (1));
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
    static constexpr int maxSearch = 70;

    // --- the plugins -----------------------------------------------------------------------------------------------
    void collectTiles()
    {
        auto& host = engine.getPluginHost();
        const auto genEntries = quickpick::generatorEntries (engine);
        const auto fxEntries = quickpick::effectEntries (engine);

        // FL's Plugin database folders as the categories (round 170b: the user's folder, else FL's category for FL's
        // plugins, else "LYNX SHARE"). FL lists only the Plugin database's Generators and Effects: a third-party plugin
        // in no folder of it isn't in the picker (the "..." at the ends open Installed in the Browser)
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

        auto add = [&] (Tile t) {
            if (t.category == installedTab)
                return;
            t.png = budgeted (pngSize (pluginthumbs::fileFor (t.key, t.native)));
            tiles.push_back (std::move (t));
        };
        for (const auto& g : genEntries)
        {
            if (! g.available || ! g.make)
                continue;
            Tile t;
            t.name = quickpick::shownName (engine, g.key, g.label);
            t.detail = g.detail;
            t.key = g.key;
            t.category = categoryOf (genFolders, g.key);
            t.generator = true;
            t.native = g.folder != 3;
            t.makeGenerator = g.make;
            add (std::move (t));
        }
        // FL's Plugin database also lists Generators/Misc/Audio Clip and Automation Clip (.fst with no bitmap: text
        // tiles). Audio Clip adds an audio clip channel (no sample yet); Automation Clip an automation linked to nothing,
        // its clip on a free Playlist track and its editor open (LYNX keeps automations out of the Channel Rack). A drag
        // of it does the same where it is dropped on the Channel Rack or a mixer strip.
        {
            Tile audio;
            audio.name = "Audio Clip"; // (plugin names are not translated)
            audio.key = "lynx:clip:audio";
            audio.category = "Misc";
            audio.generator = true;
            audio.native = false; // (no picture to draw: a text tile, as in FL)
            audio.makeGenerator = [name = audio.name.toStdString()]() -> std::optional<ChannelDef> {
                ChannelDef c;
                c.name = name;
                c.instrument = InstrumentKind::audioClip;
                return c;
            };
            add (std::move (audio));
            Tile automation;
            automation.name = "Automation Clip";
            automation.key = "lynx:clip:automation";
            automation.category = "Misc";
            automation.generator = true;
            automation.native = false;
            automation.makeGenerator = [&engine = engine]() -> std::optional<ChannelDef> {
                juce::MessageManager::callAsync ([&engine] { createUnlinkedAutomationClip (engine); });
                return std::nullopt; // (no channel: the clip is made instead)
            };
            add (std::move (automation));
        }
        for (const auto& f : fxEntries)
        {
            if (! f.available || ! f.make)
                continue;
            Tile t;
            t.name = quickpick::shownName (engine, f.key, f.label);
            t.detail = f.detail;
            t.key = f.key;
            t.category = categoryOf (fxFolders, f.key);
            t.generator = false;
            t.native = f.folder != 3;
            t.makeEffect = f.make;
            add (std::move (t));
        }
        // FL: each half A-Z by name (its database scan inserts every entry at its sorted place)
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

    bool inTab (const Tile& t, int which) const
    {
        return which == tabs.all() || (t.generator == tabs.isGenerator (which) && t.category == tabs.name (which));
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
        if (t.scaled.isValid())
            image = t.scaled;
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
        auto* owner = targets.dragOwner != nullptr ? targets.dragOwner() : nullptr;
        if (! BrowserComponent::startPluginDrag (*this, t.generator ? t.makeGenerator : nullptr, t.generator ? nullptr : t.makeEffect, t.name, image, owner))
            return;
        // FL: the picker closes as the drag starts. Here its window shrinks to one pixel out of the way and turns
        // invisible until the drop (the drag needs its source, which holds the mouse); an effect brings up the Mixer
        dragging = true;
        dragContainerOwner = owner;
        setAlpha (0.0f);
        setInterceptsMouseClicks (false, false);
        if (isOnDesktop())
            setBounds (getBounds().withSize (1, 1));
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
                repaint (t.bounds.expanded (4));
        }
        repaint (searchBounds().withX (0).withWidth (getWidth()));
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
            auto* container = dragContainerOwner != nullptr ? juce::DragAndDropContainer::findParentDragContainerFor (dragContainerOwner.getComponent())
                                                            : juce::DragAndDropContainer::findParentDragContainerFor (this);
            if (container == nullptr || ! container->isDragAndDropActive())
                close();
            return;
        }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (getAlpha() < 1.0f)
            setAlpha ((float) juce::jlimit (0.0, 1.0, (now - openedAt) / fadeMs));

        // the pictures: decoded and scaled to their shown size on a worker thread, a few at a time
        bool waiting = getAlpha() < 1.0f || inFlight > 0;
        bool nativeMissing = false;
        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            auto& t = tiles[(std::size_t) i];
            if (t.bounds.isEmpty() || t.queued)
                continue;
            if (t.thumb == Tile::Thumb::loaded)
            {
                if (t.scaledAt != paintScale)
                    queueLoad (i);
                waiting = waiting || t.queued;
                continue;
            }
            if (t.thumb == Tile::Thumb::missing)
            {
                if (pluginthumbs::rendering())
                    lastRenderingSeen = now;
                if (t.native && now - lastRenderingSeen < 2000.0)
                {
                    waiting = true;
                    if (now >= t.nextLook)
                    {
                        t.thumb = Tile::Thumb::unknown; // (look again: the background drawing may have made it)
                        t.nextLook = now + 400.0;
                    }
                }
                continue;
            }
            waiting = true;
            if (inFlight >= 6)
                continue;
            if (pluginthumbs::fileFor (t.key, t.native).existsAsFile())
                queueLoad (i);
            else
            {
                t.thumb = Tile::Thumb::missing;
                t.nextLook = now + 400.0;
                nativeMissing = nativeMissing || t.native;
                if (t.native)
                    lastRenderingSeen = now;
            }
        }
        if (nativeMissing && pluginthumbs::ensureRendering())
            waiting = true;
        if (relayoutPending)
        {
            relayoutPending = false;
            tabLayoutFor = {};
            layoutTiles();
            repaint();
        }
        // nothing left to load, fade or wait for: the timer stops (no idle work)
        if (! waiting)
            stopTimer();
    }

    juce::Point<int> shownPictureSize (const Tile& t, double scale) const
    {
        return { juce::jmax (1, juce::roundToInt (t.png.x * 0.5 * scale)), juce::jmax (1, juce::roundToInt (t.png.y * 0.5 * scale)) };
    }

    void queueLoad (int i)
    {
        auto& t = tiles[(std::size_t) i];
        const auto scale = paintScale;
        const auto file = pluginthumbs::fileFor (t.key, t.native);
        const auto known = t.png;
        t.queued = true;
        ++inFlight;
        juce::Component::SafePointer<PluginPickerView> safe (this);
        workers.addJob ([safe, i, file, scale, known] {
            auto source = juce::ImageFileFormat::loadFrom (file);
            juce::Image fitted;
            juce::Point<int> size;
            if (source.isValid())
            {
                size = budgeted ({ source.getWidth(), source.getHeight() });
                const auto w = juce::jmax (1, juce::roundToInt (size.x * 0.5 * scale)), h = juce::jmax (1, juce::roundToInt (size.y * 0.5 * scale));
                fitted = fitImage (source, w, h);
                brighten (fitted); // (FL draws a bitmap × 1.1)
            }
            juce::ignoreUnused (known);
            juce::MessageManager::callAsync ([safe, i, fitted, size, scale] {
                if (safe != nullptr)
                    safe->loadDone (i, fitted, size, scale);
            });
        });
    }

    void loadDone (int i, const juce::Image& fitted, juce::Point<int> size, double scale)
    {
        --inFlight;
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        auto& t = tiles[(std::size_t) i];
        t.queued = false;
        if (! fitted.isValid())
        {
            t.thumb = Tile::Thumb::missing;
            t.nextLook = juce::Time::getMillisecondCounterHiRes() + 400.0;
            return;
        }
        t.thumb = Tile::Thumb::loaded;
        if (size != t.png)
        {
            t.png = size;
            relayoutPending = true; // (a picture drawn while the picker was open: its tile takes its size)
        }
        t.scaled = juce::NativeImageType().convert (fitted);
        t.scaledAt = scale;
        if (! isTimerRunning())
            startTimerHz (60);
        repaint (t.bounds.expanded (4));
    }

    // --- painting --------------------------------------------------------------------------------------------------
    void paintVeil (juce::Graphics& g, double pixelScale)
    {
        // FL: the screen shows through a veil of 236/256, a vertical blend of two dark tones that also darkens towards
        // the top and bottom edges over 96 px (cubic). Composed once per size: a hover repaints only a small area.
        const auto w = juce::jmax (1, juce::roundToInt (getWidth() * pixelScale)), h = juce::jmax (1, juce::roundToInt (getHeight() * pixelScale));
        if (! backdropShown.isValid() || backdropShown.getWidth() != w || backdropShown.getHeight() != h)
        {
            backdropShown = juce::Image (juce::Image::RGB, w, h, false);
            juce::Graphics b (backdropShown);
            b.fillAll (juce::Colours::black);
            if (backdrop.isValid())
                b.drawImage (backdrop, backdropShown.getBounds().toFloat());
            const auto top = juce::Colour (0x23, 0x20, 0x28), bottom = juce::Colour (0x17, 0x22, 0x28); // (FL's two tones)
            const auto edge = 96.0 * pixelScale;
            for (int y = 0; y < h; ++y)
            {
                auto c = top.interpolatedWith (bottom, (float) y / (float) juce::jmax (1, h - 1));
                double k = 1.0;
                if (y < edge)
                {
                    const auto f = 1.0 - y / edge;
                    k = 1.0 - f * f * f;
                }
                else if (y > h - edge)
                {
                    const auto f = (y - (h - edge)) / edge;
                    k = 1.0 - f * f * f;
                }
                c = juce::Colour::fromFloatRGBA (c.getFloatRed() * (float) k, c.getFloatGreen() * (float) k, c.getFloatBlue() * (float) k, 236.0f / 256.0f);
                b.setColour (c);
                b.fillRect (0, y, w, 1);
            }
        }
        g.drawImage (backdropShown, getLocalBounds().toFloat());
    }

    juce::Rectangle<int> searchBounds() const { return getLocalBounds().withHeight (edgeTop).withSizeKeepingCentre (420, 44); }

    void paintSearch (juce::Graphics& g)
    {
        using namespace theme;
        if (searchText.isEmpty() && ! showPlaceholder)
            return;
        // FL: a magnifier and the text at the top centre (40 px; the hint 16 px), in its highlight colour (LYNX's accent)
        const auto area = searchBounds();
        const auto text = searchText.isEmpty() ? tr ("start to type to search plugins....") : searchText;
        const auto f = juce::Font (font (searchText.isEmpty() ? 16.0f : 34.0f));
        const auto textWidth = juce::jmin (area.getWidth() - 30, juce::GlyphArrangement::getStringWidthInt (f, text) + 4);
        const auto x = area.getCentreX() - (textWidth + 26) / 2;
        const auto glass = juce::Rectangle<float> ((float) x, (float) area.getCentreY() - 8.0f, 14.0f, 14.0f);
        g.setColour (searchText.isEmpty() ? colour::textSecondary : colour::accent);
        g.drawEllipse (glass.reduced (1.5f), 2.0f);
        g.drawLine (glass.getRight() - 3.0f, glass.getBottom() - 3.0f, glass.getRight() + 3.0f, glass.getBottom() + 3.0f, 2.2f);
        g.setFont (f);
        g.drawText (text, juce::Rectangle<int> (x + 26, area.getY(), textWidth, area.getHeight()), juce::Justification::centredLeft, true);
    }

    // FL's highlight behind the tile pointed at: the tile ± 3 px and 6 % of its width, the row's height + 14 px above and
    // below, rounded; the tab pointed at likewise (± 18 px, no widening)
    juce::Rectangle<float> highlight (juce::Rectangle<int> b, int rowHeight, bool isTab) const
    {
        const auto widen = isTab ? 3.0f : 3.0f + juce::roundToInt (b.getWidth() * 0.06f) + 1.0f;
        const auto half = rowHeight / 2.0f + (isTab ? 18.0f : 14.0f);
        return { (float) b.getX() - widen, (float) b.getCentreY() - half, (float) b.getWidth() + 2.0f * widen, 2.0f * half };
    }

    juce::Rectangle<int> hoverArea (int i) const
    {
        const auto& t = tiles[(std::size_t) i];
        return highlight (t.bounds, t.rowHeight, false).expanded (6.0f).getSmallestIntegerContainer().getUnion (labelBox (i).getSmallestIntegerContainer().expanded (4));
    }

    void paintHighlight (juce::Graphics& g, juce::Rectangle<float> box, bool pressed) const
    {
        // FL (measured on its hover): a cool tint, strongest under the top edge, a bright glow at the middle of the top
        // edge and of each side, and a thin rim. Held down, it takes the warm press colour (LYNX: the accent).
        using namespace theme;
        const auto base = pressed ? colour::accent : juce::Colour (0xff6cc4e8);
        const auto light = pressed ? colour::accent.brighter (0.7f) : juce::Colour (0xffb3ffff);
        juce::Graphics::ScopedSaveState keep (g);
        juce::Path shape;
        shape.addRoundedRectangle (box, 9.0f);
        juce::ColourGradient fill (base.withAlpha (pressed ? 0.55f : 0.30f), 0.0f, box.getY(), base.withAlpha (pressed ? 0.30f : 0.13f), 0.0f, box.getBottom(), false);
        fill.addColour (0.55, base.withAlpha (pressed ? 0.26f : 0.10f));
        g.setGradientFill (fill);
        g.fillPath (shape);
        g.reduceClipRegion (shape);
        const auto glow = [&] (float x, float y, float rx, float a) {
            g.setGradientFill (juce::ColourGradient (light.withAlpha (a), x, y, light.withAlpha (0.0f), x + rx, y, true));
            g.fillEllipse (x - rx, y - rx, rx * 2.0f, rx * 2.0f);
        };
        glow (box.getCentreX(), box.getY(), juce::jmin (box.getWidth() * 0.32f, 60.0f), 0.55f);
        glow (box.getX(), box.getCentreY(), 14.0f, 0.35f);
        glow (box.getRight(), box.getCentreY(), 14.0f, 0.35f);
        g.setColour (base.withAlpha (pressed ? 0.9f : 0.6f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));
        juce::ColourGradient line (light.withAlpha (0.0f), box.getX(), 0.0f, light.withAlpha (0.0f), box.getRight(), 0.0f, false);
        line.addColour (0.5, light);
        g.setGradientFill (line);
        g.fillRect (box.getX(), box.getY(), box.getWidth(), 1.5f);
    }

    void paintTextTile (juce::Graphics& g, const Tile& t, juce::Rectangle<float> area, float alpha) const
    {
        // FL's tile for a plugin with no picture: its name, the first letter in the highlight colour, a dark outline
        using namespace theme;
        const auto f = juce::Font (font (textTileSize, true));
        const auto first = t.name.substring (0, 1), rest = t.name.substring (1);
        juce::GlyphArrangement shape;
        shape.addLineOfText (f, t.name, 0.0f, 0.0f);
        const auto w = shape.getBoundingBox (0, -1, true).getWidth();
        const auto x = area.getCentreX() - w / 2.0f, baseline = area.getCentreY() + f.getAscent() / 2.0f - 1.0f;
        g.setFont (f);
        g.setColour (juce::Colours::black.withAlpha (0.85f * alpha));
        for (const auto [dx, dy] : { std::pair { -1.0f, 0.0f }, std::pair { 1.0f, 0.0f }, std::pair { 0.0f, -1.0f }, std::pair { 0.0f, 1.0f }, std::pair { 1.0f, 1.0f } })
            g.drawSingleLineText (t.name, juce::roundToInt (x + dx), juce::roundToInt (baseline + dy));
        g.setColour (colour::accent.withAlpha (alpha));
        g.drawSingleLineText (first, juce::roundToInt (x), juce::roundToInt (baseline));
        g.setColour (colour::textPrimary.withAlpha (alpha));
        g.drawSingleLineText (rest, juce::roundToInt (x + juce::GlyphArrangement::getStringWidth (f, first)), juce::roundToInt (baseline));
    }

    void paintTile (juce::Graphics& g, int i, bool hover)
    {
        using namespace theme;
        auto& t = tiles[(std::size_t) i];
        const auto alpha = t.matching ? 1.0f : 0.22f;
        if (hover)
            paintHighlight (g, highlight (t.bounds, t.rowHeight, false), i == pressedTile);
        // the tile pointed at is drawn a little bigger (FL eases it up to ~6 %)
        auto box = t.bounds.toFloat().reduced (1.0f);
        if (hover)
            box = box.withSizeKeepingCentre (box.getWidth() * 1.06f, box.getHeight() * 1.06f);
        if (t.hasPicture())
        {
            const auto picture = box.reduced ((float) shadow * (hover ? 1.06f : 1.0f));
            // FL's drop shadow: a soft dark border of 3 px
            for (int k = shadow; k >= 1; --k)
            {
                g.setColour (juce::Colours::black.withAlpha (0.16f * alpha));
                g.fillRoundedRectangle (picture.expanded ((float) k - 0.5f).translated (0.0f, 1.0f), (float) k);
            }
            if (t.thumb == Tile::Thumb::loaded && t.scaled.isValid())
            {
                juce::Graphics::ScopedSaveState state (g);
                if (! hover && t.scaledAt == paintScale)
                    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); // (1:1: a plain blit)
                g.setOpacity (alpha);
                g.drawImage (t.scaled, picture);
            }
            else
            {
                g.setColour (colour::bg3.withAlpha (alpha)); // (until its picture is loaded, a few frames)
                g.fillRect (picture);
            }
        }
        else
            paintTextTile (g, t, box, alpha);

        if (hover)
        {
            // FL's dark label under it: the name, then the category
            const auto lb = labelBox (i);
            g.setColour (juce::Colour (0xf0080808));
            g.fillRoundedRectangle (lb, 8.0f);
            auto lines = lb.reduced (labelPadX, labelPadY);
            g.setColour (colour::textPrimary);
            g.setFont (font (labelNameSize, true));
            g.drawText (t.name, lines.removeFromTop (labelNameSize), juce::Justification::centredLeft, false);
            g.setColour (colour::accent); // (FL: white at 0xB0; LYNX: its red)
            g.setFont (font (labelCategorySize));
            g.drawText (tr (t.category.c_str()), lines, juce::Justification::centredLeft, false);
        }
    }

    // the label under the tile pointed at: its top 3 px above the highlight's bottom; FL slides it under the mouse
    // (its centre on the pointer, nudged towards the screen's middle by half its width × how far off-centre it is)
    juce::Rectangle<float> labelBox (int i) const
    {
        const auto& t = tiles[(std::size_t) i];
        const auto nameW = juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (labelNameSize, true)), t.name);
        const auto catW = juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (labelCategorySize)), tr (t.category.c_str()));
        const auto halfW = (juce::jmax (nameW, catW) + 2.0f) * 0.5f + labelPadX;
        const auto h = labelNameSize + labelCategorySize + 2.0f * labelPadY;
        const auto hb = highlight (t.bounds, t.rowHeight, false);
        const auto c = juce::jlimit ((float) t.bounds.getX(), (float) t.bounds.getRight(), (float) mouseX);
        const auto mid = getWidth() * 0.5f;
        const auto shift = (c - mid) / juce::jmax (1.0f, mid) * halfW * 0.5f;
        return juce::Rectangle<float> (c - halfW - shift, hb.getBottom() - 3.0f, 2.0f * halfW, h)
            .constrainedWithin (getLocalBounds().toFloat());
    }

    void paintStrip (juce::Graphics& g)
    {
        using namespace theme;
        layoutTabs();
        if (hoverTab >= 0)
            paintHighlight (g, tabHighlight (hoverTab), false);
        for (int i = 0; i < tabs.count(); ++i)
        {
            const auto r = tabRects[(std::size_t) i];
            const auto shown = i == tab;
            g.setColour (shown ? colour::textPrimary : colour::textSecondary.withMultipliedAlpha (0.85f));
            g.setFont (font (tabFontOf (i)));
            g.drawText (tabLabel (i), r, juce::Justification::centred, false);
            if (shown)
            {
                // FL's mark above the shown tab
                juce::Path mark;
                const auto cx = (float) r.getCentreX(), top = (float) r.getY() - 4.0f;
                mark.addTriangle (cx - 4.5f, top, cx + 4.5f, top, cx, top - 5.0f);
                g.setColour (colour::textPrimary);
                g.fillPath (mark);
            }
        }
        g.setColour (colour::textSecondary.withMultipliedAlpha (0.85f));
        g.setFont (font ((float) genTabFont));
        for (const auto& r : { ellipsisRect (-1), ellipsisRect (1) })
            g.drawText ("...", r, juce::Justification::centred, false);
    }

    // --- geometry --------------------------------------------------------------------------------------------------
    juce::String tabLabel (int i) const { return i == tabs.all() ? tr ("All") : tr (tabs.name (i).c_str()); }

    float tabFontOf (int i) const
    {
        if (i == tabs.all())
            return allSize * (float) juce::jmax (genTabFont, fxTabFont) / tabSize;
        return (float) (tabs.isGenerator (i) ? genTabFont : fxTabFont);
    }

    // FL's tab strip (REA_rodada175b §3): each half's labels at the largest font (19…12) that fits half the form, spread
    // over its half like a row of tiles; "All" in the middle; "..." at both ends; the row just under the tiles
    void layoutTabs() const
    {
        const auto key = juce::Rectangle<int> (getWidth(), getHeight(), tabs.count(), stripReach);
        if (tabLayoutFor == key && (int) tabRects.size() == tabs.count())
            return;
        tabLayoutFor = key;
        tabRects.assign ((std::size_t) tabs.count(), {});
        const auto w = getWidth();
        const auto halves = halfBoxes();
        auto place = [&] (bool generators, juce::Range<int> span) {
            std::vector<int> ids;
            for (int i = 0; i < tabs.count(); ++i)
                if (i != tabs.all() && tabs.isGenerator (i) == generators)
                    ids.push_back (i);
            auto widthAt = [&] (int size) {
                int sum = 0;
                for (const auto i : ids)
                    sum += juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font ((float) size)), tabLabel (i));
                return sum;
            };
            const auto size = pickerview::tabFontSize (widthAt, (int) ids.size(), w / 2);
            (generators ? genTabFont : fxTabFont) = size;
            std::vector<pickerview::PackTile> row;
            for (const auto i : ids)
                row.push_back ({ juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font ((float) size)), tabLabel (i)) + 6, size + 6, true, false });
            // one row spread over the half (FL finishes the tab row as a full one: never centred)
            int x = span.getStart(), used = 0;
            for (const auto& t : row)
                used += t.w + 2;
            const auto n = (int) row.size();
            const auto dx = n > 1 ? (double) (span.getLength() - used - tileGap * (n - 1)) / (n - 1) : 0.0;
            double at = x;
            for (int k = 0; k < n; ++k)
            {
                tabRects[(std::size_t) ids[(std::size_t) k]] = { pickerview::roundFl (at), 0, row[(std::size_t) k].w + 2, row[(std::size_t) k].h + 2 };
                at += row[(std::size_t) k].w + 2 + tileGap + dx;
            }
        };
        place (true, { halves.first.left, halves.first.right });
        place (false, { halves.second.left, halves.second.right });
        const auto allFont = juce::Font (theme::font (tabFontOf (tabs.all())));
        const auto allW = juce::GlyphArrangement::getStringWidthInt (allFont, tabLabel (tabs.all())) + 8;
        tabRects[(std::size_t) tabs.all()] = juce::Rectangle<int> (allW, juce::roundToInt (allFont.getHeight()) + 6).withCentre ({ w / 2, 0 });
        tabRowHeight = 0;
        for (const auto& r : tabRects)
            tabRowHeight = juce::jmax (tabRowHeight, r.getHeight());
        const auto cy = pickerview::tabCentreY (stripReach, getHeight(), tabRowHeight);
        for (auto& r : tabRects)
            r.setCentre (r.getCentreX(), cy);
    }

    juce::Rectangle<int> ellipsisRect (int side) const
    {
        layoutTabs();
        const auto cy = tabRects.empty() ? getHeight() - 40 : tabRects.front().getCentreY();
        return juce::Rectangle<int> (16, 20).withCentre ({ side < 0 ? 8 : getWidth() - 8, cy });
    }

    int ellipsisAt (juce::Point<int> p) const
    {
        return ellipsisRect (-1).expanded (4).contains (p) ? -1 : ellipsisRect (1).expanded (4).contains (p) ? 1 : 0;
    }

    juce::Rectangle<float> tabHighlight (int i) const
    {
        layoutTabs();
        return highlight (tabRects[(std::size_t) i], tabRowHeight, true);
    }

    int tabAt (juce::Point<int> p) const
    {
        layoutTabs();
        for (int i = 0; i < tabs.count(); ++i)
            if (tabRects[(std::size_t) i].expanded (4, 8).contains (p))
                return i;
        return -1;
    }

    int tileAt (juce::Point<int> p) const
    {
        // the one pointed at first (it is drawn on top), then the rest, last drawn first (overlapping rows)
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()) && tiles[(std::size_t) hoverTile].bounds.contains (p))
            return hoverTile;
        for (int i = (int) tiles.size(); --i >= 0;)
            if (tiles[(std::size_t) i].bounds.contains (p))
                return i;
        return -1;
    }

    // the tiles shown, in the list's order (generators first, A-Z): FL's Up / Down order
    std::vector<int> shownInOrder() const
    {
        std::vector<int> order;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (! tiles[(std::size_t) i].bounds.isEmpty())
                order.push_back (i);
        return order;
    }

    void setHover (int i)
    {
        if (i == hoverTile)
            return;
        repaint (shownHoverArea); // (where the old highlight and label were painted)
        hoverTile = i;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (hoverArea (hoverTile));
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
        if (juce::isPositiveAndBelow (i, (int) tiles.size()))
            mouseX = tiles[(std::size_t) i].bounds.getCentreX();
        setHover (i);
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        // FL moves the real mouse pointer onto it (SetPhysicalCursorPos), so a click or a drag goes on from there
        if (isShowing())
            juce::Desktop::setMousePosition (localPointToGlobal (tiles[(std::size_t) i].bounds.getCentre()));
    }

    void showTab (int t)
    {
        tab = t;
        hoverTile = -1;
        layoutTiles();
        repaint();
    }

    // the generators' and effects' boxes (FL's FUN_00ec0d40): 32 px in from the sides, 56 from the top, 96 from the
    // bottom; on the All tab (or the other half's category) each gets its half, 80 px apart, else the whole width
    std::pair<pickerview::PackBox, pickerview::PackBox> halfBoxes (int which = -1) const
    {
        const pickerview::PackBox all { edgeX, edgeTop, getWidth() - edgeX, getHeight() - edgeBottom };
        const auto half = ((all.right - all.left) - halvesGap) / 2;
        auto gens = all, fx = all;
        if (which < 0 || ! tabs.isGenerator (which))
            gens.right = all.left + half;
        if (which < 0 || ! tabs.isEffect (which))
            fx.left = all.right - half;
        return { gens, fx };
    }

    void pack (int which, int& reach)
    {
        const auto boxes = halfBoxes (which);
        for (const auto generators : { true, false })
        {
            std::vector<int> ids;
            std::vector<pickerview::PackTile> in;
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (tiles[(std::size_t) i].generator == generators)
                {
                    const auto& t = tiles[(std::size_t) i];
                    pickerview::PackTile p;
                    p.shown = inTab (t, which);
                    p.generator = generators;
                    if (t.hasPicture())
                    {
                        // FL: the picture at half its file's size (192 dpi bitmaps on a 96 dpi screen) + its shadow
                        p.w = pickerview::roundFl (t.png.x * 0.5) + 2 * shadow;
                        p.h = pickerview::roundFl (t.png.y * 0.5) + 2 * shadow;
                    }
                    else
                    {
                        const auto f = juce::Font (theme::font (textTileSize, true));
                        p.w = juce::GlyphArrangement::getStringWidthInt (f, t.name) + 6;
                        p.h = juce::roundToInt (f.getHeight()) + 6;
                    }
                    ids.push_back (i);
                    in.push_back (p);
                }
            const auto r = pickerview::packRows (in, generators ? boxes.first : boxes.second, tileGap);
            reach = juce::jmax (reach, r.reach);
            for (std::size_t k = 0; k < ids.size(); ++k)
            {
                auto& t = tiles[(std::size_t) ids[k]];
                const auto& p = r.tiles[k];
                t.bounds = p.shown ? juce::Rectangle<int> (p.x, p.y, p.w, p.h) : juce::Rectangle<int>();
                t.rowHeight = p.rowHeight;
            }
        }
    }

    void layoutTiles()
    {
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        // FL places the tab strip once, from the All tab's tiles (it stays put while the tabs change)
        if (stripFor != getLocalBounds() || stripPictures != picturesKnown())
        {
            int reach = 0;
            pack (tabs.all(), reach);
            stripReach = reach;
            stripFor = getLocalBounds();
            stripPictures = picturesKnown();
            tabLayoutFor = {};
        }
        int unused = 0;
        pack (tab, unused);
        if (hoverTile >= 0 && tiles[(std::size_t) hoverTile].bounds.isEmpty())
            hoverTile = -1;
    }

    int picturesKnown() const
    {
        int n = 0;
        for (const auto& t : tiles)
            n += t.hasPicture() ? 1 : 0;
        return n;
    }

    AudioEngine& engine;
    PluginPickerTargets targets;
    std::vector<Tile> tiles;
    pickerview::Tabs tabs;
    juce::Image backdrop, backdropShown;
    mutable std::vector<juce::Rectangle<int>> tabRects;
    mutable juce::Rectangle<int> tabLayoutFor;
    mutable int genTabFont = 19, fxTabFont = 19, tabRowHeight = 0;
    juce::Rectangle<int> stripFor;
    int stripReach = 0, stripPictures = -1;
    double paintScale = 1.0;
    juce::ThreadPool workers { 2 };
    int inFlight = 0;
    bool relayoutPending = false;
    juce::String searchText;
    bool showPlaceholder = false;
    int tab = 0, hoverTab = -1, hoverTile = -1, pressedTile = -1, mouseX = 0;
    juce::Rectangle<int> shownHoverArea; // the hovered tile's highlight and label as last painted
    bool dragging = false;
    juce::Component::SafePointer<juce::Component> dragContainerOwner;
    float wheelAccum = 0.0f;
    double buildMs = 0.0, openedAt = 0.0, lastRenderingSeen = 0.0;
    static constexpr double fadeMs = 140.0;
};
} // namespace

std::unique_ptr<juce::Component> makePluginPickerView (AudioEngine& engine, PluginPickerTargets targets)
{
    return std::make_unique<PluginPickerView> (engine, std::move (targets));
}

void takePluginPickerBackdrop (juce::Component& view, juce::Rectangle<int> screenArea)
{
    if (auto* picker = dynamic_cast<PluginPickerView*> (&view))
        picker->takeBackdrop (screenArea);
}
} // namespace lynx
