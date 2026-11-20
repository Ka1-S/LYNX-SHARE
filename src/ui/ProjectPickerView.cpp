#include "ui/ProjectPickerView.h"

#include "core/PickerView.h"
#include "engine/GeneratorCatalog.h"
#include "engine/SampleLibrary.h"
#include "ui/BrowserComponent.h"
#include "ui/I18n.h"
#include "ui/PickerLook.h"
#include "ui/PlaylistComponent.h"
#include "ui/PluginThumbs.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// FL's Project picker on "Ookay - Thief" at 100 % (capture 190my A4, pp-all / fl-project-picker-tiles): the Plugin
// picker's form and box (32 px in from the sides, 56 from the top, 96 from the bottom, the halves 80 px apart), a pattern
// tile 188 × 106 (its clip: a 15 px title band over the notes), a sample channel 39 px tall (its clip: the name band over
// the waveform), as wide as its name or its sound needs up to 192 px, a generator its plugin's picture 64 px tall with a
// name tag; rows 24 px apart, spread over the half like the Plugin picker's. The tile pointed at grows ~15 %, with the
// picker's highlight and a label under it (name, then its length / its plugin); the channels a pointed-at pattern plays
// (or the patterns a pointed-at channel plays in) get a yellow tick.
constexpr int edgeX = 32, edgeTop = 56, edgeBottom = 96, halvesGap = 80, tileGap = 8, rowGap = 24;
constexpr int patternW = 186, patternH = 104;         // (+ 2 = FL's 188 × 106)
constexpr int sampleH = 37, sampleMinW = 50, sampleMaxW = 190;
constexpr int generatorH = 64, generatorMaxW = 128;
constexpr float titleBand = 15.0f, titleSize = 26.0f, hoverGrow = 1.15f;
constexpr float labelNameSize = 25.0f, labelDetailSize = 20.0f, labelPadX = 10.0f, labelPadY = 4.0f;
// a sample channel's width for its sound's length: FL's widths on Thief (SAX loops 144, crashes 125–175, the vocals at
// the 192 px cap) against the samples' lengths
constexpr double samplePixelsPerSecond = 60.0;

struct PTile
{
    enum class Kind { pattern, add, sample, generator } kind = Kind::pattern;
    int index = -1;              // pattern / channel
    juce::String name, detail;  // the label under the tile pointed at: name, then its length / plugin
    juce::Colour colour;
    std::vector<int> related;   // the channels a pattern plays / the patterns a channel plays in
    int w = 0, h = 0;            // the picture (packRows adds 2)
    juce::Rectangle<int> bounds; // shown (empty = not in the tab)
    int rowHeight = 0;
    bool matching = true;
    juce::Image picture;         // a generator's plugin picture (empty: its name only)
    std::vector<float> peaks;    // a sample's waveform (one peak per pixel column)
    bool current = false;        // the current pattern / the selected channel (FL's red mark)
    bool left() const noexcept { return kind == Kind::pattern || kind == Kind::add; }
};

class ProjectPickerView final : public juce::Component,
                                public ui::HintSource,
                                private juce::Timer
{
public:
    ProjectPickerView (AudioEngine& e, ProjectPickerTargets t) : engine (e), targets (std::move (t))
    {
        setWantsKeyboardFocus (true);
        setOpaque (true);
        collectTiles();
        setAlpha (0.0f); // (FL fades its picker form in; the mouse waits for most of it)
        openedAt = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (60);
    }

    void takeBackdrop (juce::Rectangle<int> logical)
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (logical);
        const auto scale = display != nullptr ? display->scale : 1.0;
        backdrop = pickerlook::captureScreen ((logical.toDouble() * scale).toNearestInt());
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
        // FL: the picker closes when focus goes to another window (capture 190mv A11)
        if (isShowing() && ! dragging)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ProjectPickerView> (this)] {
                if (safe == nullptr || safe->dragging || safe->hasKeyboardFocus (true))
                    return;
                auto* focused = juce::Component::getCurrentlyFocusedComponent();
                if (focused == nullptr || focused->getTopLevelComponent() != safe->getTopLevelComponent())
                    safe->close();
            });
    }

    void resized() override { layoutTiles(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto pixelScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        pickerlook::paintVeil (g, *this, backdrop, backdropShown, pixelScale);
        // the halves' titles (FL: on the All tab), the search glass at the top centre
        if (tab == 0)
        {
            g.setColour (colour::textSecondary.brighter (0.25f));
            g.setFont (font (titleSize));
            const auto band = getLocalBounds().withHeight (juce::roundToInt (titleSize * 3.0f));
            g.drawText (tr ("Patterns"), band.withWidth (getWidth() / 2), juce::Justification::centred);
            g.drawText (tr ("Channels"), band.withTrimmedLeft (getWidth() / 2), juce::Justification::centred);
        }
        paintSearch (g);
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (i != hoverTile && ! tiles[(std::size_t) i].bounds.isEmpty() && g.clipRegionIntersects (tiles[(std::size_t) i].bounds.expanded (30)))
                paintTile (g, i, false);
        paintTabs (g);
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            paintTile (g, hoverTile, true);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        const auto t = tabAt (e.getPosition());
        if (t >= 0 && t != tab) // (FL's form: pointing at a tab shows it)
            showTab (t);
        if (t != hoverTab)
        {
            hoverTab = t;
            repaint (tabStrip());
        }
        auto i = tileAt (e.getPosition());
        if (i >= 0 && ! tiles[(std::size_t) i].matching && ! e.mods.isShiftDown())
            i = -1;
        setHover (i);
        setMouseCursor (i >= 0 || t >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverTab = -1;
        repaint (tabStrip());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! ready())
            return;
        pressedTile = tileAt (e.getPosition());
        if ((e.mods.isPopupMenu() || e.mods.isMiddleButtonDown()) && pressedTile < 0)
        {
            close(); // (FL's form: a right / middle click outside the tiles closes it)
            return;
        }
        if (pressedTile >= 0)
            repaint (hoverArea (pressedTile));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // a pattern dragged out: its clip onto the Playlist (the Browser's pattern drag); the picker gets out of the way
        if (pressedTile < 0 || dragging || e.getDistanceFromDragStart() <= 6 || tiles[(std::size_t) pressedTile].kind != PTile::Kind::pattern)
            return;
        auto* owner = targets.dragOwner != nullptr ? targets.dragOwner() : nullptr;
        auto* container = dynamic_cast<juce::DragAndDropContainer*> (owner);
        if (container == nullptr)
            container = juce::DragAndDropContainer::findParentDragContainerFor (owner != nullptr ? owner : this);
        if (container == nullptr)
            return;
        const auto& t = tiles[(std::size_t) pressedTile];
        juce::Image image (juce::Image::ARGB, t.bounds.getWidth(), t.bounds.getHeight(), true);
        {
            juce::Graphics g (image);
            paintPattern (g, t, image.getBounds().toFloat().reduced (1.0f));
        }
        dragging = true;
        dragContainerOwner = owner;
        container->startDragging (juce::String (BrowserComponent::patternDragDescription) + "|" + juce::String (t.index), this,
                                  juce::ScaledImage (image), true);
        setAlpha (0.0f);
        setInterceptsMouseClicks (false, false);
        if (isOnDesktop())
            setBounds (getBounds().withSize (1, 1));
        if (! isTimerRunning())
            startTimerHz (30);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto pressed = std::exchange (pressedTile, -1);
        if (dragging || e.mouseWasDraggedSinceMouseDown() || ! ready())
            return;
        if (pressed < 0 && tabAt (e.getPosition()) >= 0 && e.mods.isLeftButtonDown())
        {
            close(); // (FL, capture 190my A4: a tab shows on pointing; a click on it closes the picker)
            return;
        }
        if (pressed < 0 || pressed != tileAt (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
        {
            // FL (capture 190my A4): a right-click on a pattern closes the picker and shows the pattern in the Browser
            const auto& t = tiles[(std::size_t) pressed];
            auto reveal = targets.revealPattern;
            const auto index = t.kind == PTile::Kind::pattern ? t.index : -1;
            close();
            if (index >= 0 && reveal)
                reveal (index);
        }
        else if (e.mods.isLeftButtonDown())
            pick (pressed);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (! ready())
            return;
        // (the Plugin picker's form: one tab per notch, down = the next one to the right)
        wheelAccum += wheel.deltaY;
        const auto notches = (int) std::trunc (wheelAccum / 0.1f);
        if (notches == 0)
            return;
        wheelAccum -= (float) notches * 0.1f;
        showTab (juce::jlimit (0, 2, tab - notches));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();
        if (code == juce::KeyPress::escapeKey || (code == juce::KeyPress::F8Key && mods.isCtrlDown()))
        {
            close();
            return true;
        }
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            showTab (juce::jlimit (0, 2, tab + (code == juce::KeyPress::rightKey ? 1 : -1)));
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
                pick (hoverTile);
            return true;
        }
        if (code == juce::KeyPress::backspaceKey)
        {
            setSearch (searchText.dropLastCharacters (1));
            return true;
        }
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
        {
            const auto& t = tiles[(std::size_t) i];
            h.text = t.kind == PTile::Kind::add ? tr ("Add a pattern")
                   : t.left() ? t.name + " - " + tr ("[LMB] make it the current pattern  drag: onto the Playlist")
                              : t.name + " - " + tr ("[LMB] select the channel");
            if (t.kind == PTile::Kind::pattern)
                h.text << "  " << tr ("[RMB] show it in the Browser");
        }
        else if (const auto k = tabAt (p); k >= 0)
            h.text = tabLabel (k);
        return h;
    }

    // (shots / tests)
    std::pair<int, int> tileKindAt (juce::Point<int> p) const
    {
        const auto i = tileAt (p);
        if (i < 0)
            return { 0, -1 };
        const auto& t = tiles[(std::size_t) i];
        return { t.kind == PTile::Kind::pattern ? 1 : t.kind == PTile::Kind::add ? 2 : 3, t.index };
    }

    juce::Rectangle<int> boundsOf (int kind, int index) const
    {
        for (const auto& t : tiles)
        {
            const auto k = t.kind == PTile::Kind::pattern ? 1 : t.kind == PTile::Kind::add ? 2 : 3;
            if (k == kind && (kind == 2 || t.index == index))
                return t.bounds;
        }
        return {};
    }

    void backdropForSnapshot (const juce::Image& screen)
    {
        backdrop = screen;
        backdropShown = {};
        setAlpha (1.0f);
    }

    void typeForSnapshot (const juce::String& text) { setSearch (text); }

    void hoverForSnapshot (juce::Point<int> p)
    {
        setAlpha (1.0f);
        setHover (tileAt (p));
    }

    void showTab (int t)
    {
        if (t == tab)
            return;
        tab = t;
        hoverTile = -1;
        layoutTiles();
        repaint();
    }

private:
    // --- the project ----------------------------------------------------------------------------------------------
    void collectTiles()
    {
        const auto& song = engine.getSong();
        for (int p = 0; p < (int) song.patterns.size(); ++p)
        {
            PTile t;
            t.kind = PTile::Kind::pattern;
            t.index = p;
            t.name = juce::String::fromUTF8 (song.patterns[(std::size_t) p].name.c_str());
            t.colour = PlaylistComponent::patternColour (song, p);
            t.w = patternW;
            t.h = patternH;
            t.current = p == song.currentPattern;
            const auto bars = (int) std::ceil ((double) song.patterns[(std::size_t) p].effectiveLength (song.meter())
                                               / (double) juce::jmax<std::int64_t> (1, song.meter().bar()));
            t.detail = bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) });
            for (const auto& n : song.patterns[(std::size_t) p].notes)
                if (std::find (t.related.begin(), t.related.end(), n.channel) == t.related.end())
                    t.related.push_back (n.channel);
            tiles.push_back (std::move (t));
        }
        {
            // FL's last tile: the pattern a click adds, named as the next one would be ("Pattern 9" on Thief)
            PTile t;
            t.kind = PTile::Kind::add;
            t.name = tr ("Pattern {0}", { juce::String ((int) song.patterns.size() + 1) });
            t.detail = tr ("Find first empty"); // (FL's label: "New pattern / Find first empty")
            t.colour = juce::Colour (0xff4b525a);
            t.w = patternW;
            t.h = patternH;
            tiles.push_back (std::move (t));
        }
        const auto entries = generators::entries (engine);
        auto& library = engine.getSampleLibrary();
        const auto nameFont = juce::Font (theme::font (theme::type::caption, true));
        for (int c = 0; c < (int) song.channels.size(); ++c)
        {
            const auto& ch = song.channels[(std::size_t) c];
            PTile t;
            t.index = c;
            t.name = juce::String::fromUTF8 (ch.name.c_str());
            t.colour = ch.colour != 0 ? juce::Colour (ch.colour) : juce::Colour (0xff5f6b74);
            t.current = c == engine.getSelectedChannel();
            for (int p = 0; p < (int) song.patterns.size(); ++p)
                for (const auto& n : song.patterns[(std::size_t) p].notes)
                    if (n.channel == c)
                    {
                        t.related.push_back (p);
                        break;
                    }
            t.detail = detailOf (ch, entries);
            // (a sample channel whose file is missing keeps its clip look, its name only)
            const auto sampled = ch.instrument == InstrumentKind::sampler || ch.instrument == InstrumentKind::audioClip;
            if (sampled)
            {
                t.kind = PTile::Kind::sample;
                double seconds = 0.0;
                if (const auto data = ch.samplePath.empty() ? nullptr : library.findLoaded (juce::String::fromUTF8 (ch.samplePath.c_str()));
                    data != nullptr && data->sampleRate > 0.0)
                {
                    seconds = (double) data->frames() / data->sampleRate;
                    t.w = juce::jlimit (sampleMinW, sampleMaxW, juce::roundToInt (seconds * samplePixelsPerSecond));
                    t.peaks = peaksOf (*data, t.w);
                }
                const auto nameW = juce::GlyphArrangement::getStringWidthInt (nameFont, t.name) + 26; // (+ its glyph)
                t.w = juce::jlimit (sampleMinW, sampleMaxW, juce::jmax (t.w, nameW));
                if (t.peaks.size() != (std::size_t) t.w && ! t.peaks.empty())
                    if (const auto data = library.findLoaded (juce::String::fromUTF8 (ch.samplePath.c_str())))
                        t.peaks = peaksOf (*data, t.w);
                t.h = sampleH;
            }
            else
            {
                t.kind = PTile::Kind::generator;
                t.picture = pictureFor (ch, entries);
                if (t.picture.isValid())
                {
                    const auto k = (double) generatorH / juce::jmax (1, t.picture.getHeight());
                    t.w = juce::jmin (generatorMaxW, juce::roundToInt (t.picture.getWidth() * k));
                    t.h = generatorH;
                }
                else
                {
                    t.w = juce::jlimit (sampleMinW, sampleMaxW, juce::GlyphArrangement::getStringWidthInt (nameFont, t.name) + 16);
                    t.h = sampleH;
                }
            }
            tiles.push_back (std::move (t));
        }
        // FL (capture 190my A4): the sample channels first, the generators in the last rows (each in the rack's order)
        std::stable_partition (tiles.begin(), tiles.end(), [] (const PTile& t) { return t.kind != PTile::Kind::generator; });
    }

    // FL's label: the channel's plugin ("Audio Clip", "Harmless", ...)
    static juce::String detailOf (const ChannelDef& ch, const std::vector<generators::Entry>& entries)
    {
        if (ch.instrument == InstrumentKind::audioClip)
            return "Audio Clip";
        if (ch.instrument == InstrumentKind::sampler)
            return tr ("Sampler");
        if (ch.instrument == InstrumentKind::plugin && ch.plugin.name.size() > 0)
            return juce::String::fromUTF8 (ch.plugin.name.c_str());
        for (const auto& e : entries)
            if (e.kind == ch.instrument && e.folder != 3)
                return e.label;
        return {};
    }

    static std::vector<float> peaksOf (const SampleData& data, int columns)
    {
        std::vector<float> out ((std::size_t) juce::jmax (1, columns), 0.0f);
        const auto frames = data.frames();
        const auto* left = data.leftData();
        if (frames == 0 || left == nullptr)
            return out;
        for (int x = 0; x < columns; ++x)
        {
            const auto from = frames * (std::size_t) x / (std::size_t) columns, to = juce::jmax (from + 1, frames * (std::size_t) (x + 1) / (std::size_t) columns);
            float peak = 0.0f;
            for (auto i = from; i < to && i < frames; i += juce::jmax ((std::size_t) 1, (to - from) / 64))
                peak = juce::jmax (peak, std::abs (left[i]));
            out[(std::size_t) x] = juce::jmin (1.0f, peak);
        }
        return out;
    }

    // a generator's plugin picture (the Plugin picker's): its catalog entry by plugin id, else by instrument kind
    static juce::Image pictureFor (const ChannelDef& ch, const std::vector<generators::Entry>& entries)
    {
        const generators::Entry* found = nullptr;
        for (const auto& e : entries)
            if (ch.instrument == InstrumentKind::plugin ? (! ch.plugin.id.empty() && e.key == ch.plugin.id) : (e.kind == ch.instrument && e.folder != 3))
            {
                found = &e;
                break;
            }
        if (found == nullptr)
            return {};
        const auto file = pluginthumbs::fileFor (found->key, found->folder != 3);
        return file.existsAsFile() ? juce::ImageFileFormat::loadFrom (file) : juce::Image();
    }

    void pick (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int) tiles.size()))
            return;
        const auto& t = tiles[(std::size_t) i];
        const auto kind = t.kind;
        const auto index = t.index;
        auto pickPattern = targets.pickPattern;
        auto pickChannel = targets.pickChannel;
        close(); // (this view is gone after this)
        if (kind == PTile::Kind::pattern && pickPattern)
            pickPattern (index);
        else if (kind == PTile::Kind::add && pickPattern)
            pickPattern (-1);
        else if ((kind == PTile::Kind::sample || kind == PTile::Kind::generator) && pickChannel)
            pickChannel (index);
    }

    void close()
    {
        if (auto c = targets.close)
            c();
    }

    bool ready() const noexcept { return getAlpha() >= 0.75f; }

    void setSearch (juce::String text)
    {
        searchText = text.substring (0, 70);
        const auto query = searchText.toStdString();
        for (auto& t : tiles)
            t.matching = query.empty() || pickerview::nameMatches (query, t.name.toStdString());
        // FL (capture 190my A4): typing filters at once — only the matching tiles stay
        hoverTile = -1;
        layoutTiles();
        if (! query.empty())
        {
            std::vector<std::string> names;
            std::vector<int> index;
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (! tiles[(std::size_t) i].bounds.isEmpty())
                {
                    names.push_back (tiles[(std::size_t) i].name.toStdString());
                    index.push_back (i);
                }
            if (const auto k = pickerview::searchJump (query, names); k >= 0)
                hoverTile = index[(std::size_t) k];
        }
        repaint();
    }

    void stepTile (int delta)
    {
        std::vector<int> order;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (! tiles[(std::size_t) i].bounds.isEmpty() && tiles[(std::size_t) i].matching)
                order.push_back (i);
        if (order.empty())
            return;
        const auto at = std::find (order.begin(), order.end(), hoverTile);
        const auto k = at == order.end() ? (delta > 0 ? 0 : (int) order.size() - 1)
                                         : ((int) (at - order.begin()) + delta + (int) order.size()) % (int) order.size();
        setHover (order[(std::size_t) k]);
    }

    // --- painting -------------------------------------------------------------------------------------------------
    juce::Rectangle<int> searchBounds() const { return getLocalBounds().withHeight (edgeTop).withSizeKeepingCentre (420, 44); }

    void paintSearch (juce::Graphics& g)
    {
        using namespace theme;
        // FL: a magnifier at the top centre; the typed text beside it in the highlight colour
        const auto area = searchBounds();
        const auto f = juce::Font (font (34.0f));
        const auto textWidth = searchText.isEmpty() ? 0 : juce::jmin (area.getWidth() - 40, juce::GlyphArrangement::getStringWidthInt (f, searchText) + 4);
        const auto x = area.getCentreX() - (textWidth + 30) / 2;
        const auto glass = juce::Rectangle<float> ((float) x, (float) area.getCentreY() - 12.0f, 24.0f, 24.0f);
        g.setColour (searchText.isEmpty() ? colour::textSecondary.withAlpha (0.6f) : colour::accent);
        g.drawEllipse (glass.reduced (2.0f), 2.0f);
        g.drawLine (glass.getRight() - 5.0f, glass.getBottom() - 5.0f, glass.getRight() + 2.0f, glass.getBottom() + 2.0f, 2.4f);
        if (searchText.isNotEmpty())
        {
            g.setFont (f);
            g.drawText (searchText, juce::Rectangle<int> (x + 30, area.getY(), textWidth, area.getHeight()), juce::Justification::centredLeft, true);
        }
    }

    juce::Rectangle<int> hoverArea (int i) const
    {
        const auto& t = tiles[(std::size_t) i];
        const auto grown = t.bounds.toFloat().withSizeKeepingCentre (t.bounds.getWidth() * hoverGrow, t.bounds.getHeight() * hoverGrow);
        auto area = pickerlook::highlight (t.bounds, t.rowHeight, false).getUnion (grown).expanded (12.0f).getUnion (labelBox (t, grown).expanded (4.0f));
        for (const auto& other : tiles) // (the ticks on the related tiles)
            if (other.left() != t.left() && std::find (t.related.begin(), t.related.end(), other.index) != t.related.end() && ! other.bounds.isEmpty())
                area = area.getUnion (other.bounds.toFloat().expanded (12.0f));
        return area.getSmallestIntegerContainer();
    }

    void paintTile (juce::Graphics& g, int i, bool hover)
    {
        using namespace theme;
        const auto& t = tiles[(std::size_t) i];
        const auto alpha = t.matching ? 1.0f : 0.22f;
        if (hover)
            pickerlook::paintHighlight (g, pickerlook::highlight (t.bounds, t.rowHeight, false), i == pressedTile);
        auto box = t.bounds.toFloat().reduced (1.0f);
        if (hover)
            box = box.withSizeKeepingCentre (box.getWidth() * hoverGrow, box.getHeight() * hoverGrow);
        juce::Graphics::ScopedSaveState keep (g);
        g.beginTransparencyLayer (alpha);
        switch (t.kind)
        {
            case PTile::Kind::pattern: paintPattern (g, t, box); break;
            case PTile::Kind::add: paintAdd (g, t, box); break;
            case PTile::Kind::sample: paintSample (g, t, box); break;
            case PTile::Kind::generator: paintGenerator (g, t, box); break;
        }
        g.endTransparencyLayer();
        if (! hover && juce::isPositiveAndBelow (hoverTile, (int) tiles.size()) && tiles[(std::size_t) hoverTile].left() != t.left())
        {
            const auto& related = tiles[(std::size_t) hoverTile].related;
            if (std::find (related.begin(), related.end(), t.index) != related.end())
                paintTick (g, box);
        }
        if (hover)
            paintLabel (g, i, box);
    }

    // FL's yellow tick on a channel the pointed-at pattern plays (and the other way round), at the tile's top right
    static void paintTick (juce::Graphics& g, juce::Rectangle<float> box)
    {
        const auto r = juce::Rectangle<float> (box.getRight() - 9.0f, box.getY() - 8.0f, 17.0f, 17.0f);
        g.setColour (juce::Colour (0xfff2d230));
        g.fillEllipse (r);
        juce::Path tick;
        tick.startNewSubPath (r.getX() + 4.5f, r.getCentreY());
        tick.lineTo (r.getCentreX() - 1.0f, r.getBottom() - 5.0f);
        tick.lineTo (r.getRight() - 4.0f, r.getY() + 5.0f);
        g.setColour (juce::Colour (0xff222222));
        g.strokePath (tick, juce::PathStrokeType (1.8f));
    }

    // the label under the tile pointed at: a dark rounded box with the name, then the detail (the Plugin picker's)
    juce::Rectangle<float> labelBox (const PTile& t, juce::Rectangle<float> box) const
    {
        const auto nameW = juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (labelNameSize, true)), t.kind == PTile::Kind::add ? tr ("New pattern") : t.name);
        const auto detailW = juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (labelDetailSize)), t.detail);
        const auto w = juce::jmax (nameW, detailW) + 2.0f * labelPadX + 2.0f;
        const auto h = labelNameSize + labelDetailSize + 2.0f * labelPadY;
        return juce::Rectangle<float> (box.getCentreX() - w * 0.5f + box.getWidth() * 0.1f, box.getBottom() - 3.0f, w, h).constrainedWithin (getLocalBounds().toFloat());
    }

    void paintLabel (juce::Graphics& g, int i, juce::Rectangle<float> box) const
    {
        using namespace theme;
        const auto& t = tiles[(std::size_t) i];
        const auto lb = labelBox (t, box);
        g.setColour (juce::Colour (0xf0080808));
        g.fillRoundedRectangle (lb, 8.0f);
        auto lines = lb.reduced (labelPadX, labelPadY);
        g.setColour (colour::textPrimary);
        g.setFont (font (labelNameSize, true));
        g.drawText (t.kind == PTile::Kind::add ? tr ("New pattern") : t.name, lines.removeFromTop (labelNameSize), juce::Justification::centredLeft, false);
        g.setColour (colour::textSecondary);
        g.setFont (font (labelDetailSize));
        g.drawText (t.detail, lines, juce::Justification::centredLeft, false);
    }

    static juce::Colour bodyOf (juce::Colour c) { return c.interpolatedWith (juce::Colour (0xff1c2126), 0.62f); }
    static juce::Colour inkOf (juce::Colour c) { return c.interpolatedWith (juce::Colours::white, 0.6f); }

    static void paintTitle (juce::Graphics& g, juce::Rectangle<float> band, juce::Colour colour, const juce::String& name, bool notes)
    {
        using namespace theme;
        g.setColour (colour);
        g.fillRect (band);
        auto text = band.reduced (3.0f, 1.0f);
        const auto glyph = text.removeFromLeft (9.0f).withSizeKeepingCentre (8.0f, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        if (notes) // (FL's pattern glyph: three bars)
            for (int k = 0; k < 3; ++k)
                g.fillRect (glyph.getX() + (k == 1 ? 2.0f : 0.0f), glyph.getY() + 1.0f + 2.5f * (float) k, 6.0f, 1.4f);
        else // (an empty pattern: FL's down arrow)
        {
            juce::Path v;
            v.addTriangle (glyph.getX(), glyph.getY() + 2.0f, glyph.getRight(), glyph.getY() + 2.0f, glyph.getCentreX(), glyph.getBottom() - 1.0f);
            g.fillPath (v);
        }
        g.setFont (font (type::caption, true));
        g.drawText (name, text.withTrimmedLeft (3.0f), juce::Justification::centredLeft, true);
    }

    void paintCurrentMark (juce::Graphics& g, juce::Rectangle<float> body) const
    {
        // FL's red mark at the left of the current pattern / the selected channel
        juce::Path mark;
        const auto cy = body.getCentreY();
        mark.addTriangle (body.getX(), cy - 6.0f, body.getX() + 8.0f, cy, body.getX(), cy + 6.0f);
        g.setColour (juce::Colour (0xffe8475a));
        g.fillPath (mark);
    }

    void paintPattern (juce::Graphics& g, const PTile& t, juce::Rectangle<float> box) const
    {
        const auto& song = engine.getSong();
        g.setColour (bodyOf (t.colour));
        g.fillRect (box);
        const auto& notes = song.patterns[(std::size_t) t.index].notes;
        paintTitle (g, box.withHeight (titleBand), t.colour, t.name, ! notes.empty());
        auto body = box.withTrimmedTop (titleBand).reduced (1.0f, 3.0f);
        if (! notes.empty())
        {
            int low = 127, high = 0;
            std::int64_t end = 1;
            for (const auto& n : notes)
            {
                low = juce::jmin (low, n.key);
                high = juce::jmax (high, n.key);
                end = juce::jmax (end, n.start + juce::jmax<std::int64_t> (1, n.length));
            }
            const auto length = (double) juce::jmax<std::int64_t> (end, song.patterns[(std::size_t) t.index].effectiveLength (song.meter()));
            const auto span = juce::jmax (6, high - low + 2);
            const auto bottomKey = (low + high) / 2.0f - span / 2.0f;
            const auto rows = (float) span + 1.0f;
            const auto h = juce::jlimit (2.0f, 6.0f, body.getHeight() / rows);
            g.setColour (inkOf (t.colour));
            for (const auto& n : notes)
            {
                const auto x = body.getX() + (float) ((double) n.start / length) * body.getWidth();
                const auto w = juce::jmax (1.5f, (float) ((double) n.length / length) * body.getWidth() - 1.0f);
                const auto y = body.getBottom() - ((float) n.key - bottomKey + 0.5f) / rows * body.getHeight() - h * 0.5f;
                g.fillRect (x, y, w, h);
            }
        }
        if (t.current)
            paintCurrentMark (g, box.withTrimmedTop (titleBand));
    }

    void paintAdd (juce::Graphics& g, const PTile& t, juce::Rectangle<float> box) const
    {
        g.setColour (bodyOf (t.colour));
        g.fillRect (box);
        paintTitle (g, box.withHeight (titleBand), t.colour, t.name, false);
        const auto plus = box.withTrimmedTop (titleBand).withSizeKeepingCentre (18.0f, 18.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (plus);
        g.setColour (bodyOf (t.colour));
        g.fillRect (plus.withSizeKeepingCentre (10.0f, 2.0f));
        g.fillRect (plus.withSizeKeepingCentre (2.0f, 10.0f));
    }

    void paintSample (juce::Graphics& g, const PTile& t, juce::Rectangle<float> box) const
    {
        g.setColour (t.colour.darker (0.35f));
        g.fillRoundedRectangle (box, 2.0f);
        auto band = box.withHeight (titleBand);
        g.setColour (t.colour);
        g.fillRoundedRectangle (band, 2.0f);
        // the name, and FL's sample-clip glyph at the band's right end
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (theme::font (theme::type::caption, true));
        auto glyph = band.removeFromRight (16.0f).withSizeKeepingCentre (9.0f, 9.0f);
        g.drawText (t.name, band.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
        g.fillRect (glyph.withSizeKeepingCentre (9.0f, 1.6f));
        g.fillRect (glyph.withSizeKeepingCentre (1.6f, 9.0f));
        // the waveform under it
        const auto wave = box.withTrimmedTop (titleBand).reduced (1.0f, 2.0f);
        if (! t.peaks.empty())
        {
            g.setColour (t.colour.brighter (0.25f).withAlpha (0.85f));
            const auto cols = (int) t.peaks.size();
            for (int x = 0; x < cols; ++x)
            {
                const auto a = t.peaks[(std::size_t) x] * wave.getHeight() * 0.5f;
                if (a > 0.3f)
                    g.fillRect (wave.getX() + wave.getWidth() * (float) x / (float) cols, wave.getCentreY() - a, juce::jmax (1.0f, wave.getWidth() / (float) cols), 2.0f * a);
            }
        }
        if (t.current)
            paintCurrentMark (g, box);
    }

    void paintGenerator (juce::Graphics& g, const PTile& t, juce::Rectangle<float> box) const
    {
        if (t.picture.isValid())
            g.drawImage (t.picture, box, juce::RectanglePlacement::stretchToFit);
        else
        {
            g.setColour (juce::Colour (0xff2b3136));
            g.fillRoundedRectangle (box, 2.0f);
        }
        // FL's name tag at the picture's top left, in the channel's colour
        g.setFont (theme::font (theme::type::caption, true));
        const auto w = juce::jmin (box.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t.name) + 10.0f);
        const auto tag = box.withWidth (w).withHeight (titleBand + 1.0f).translated (0.0f, -4.0f);
        g.setColour (t.colour);
        g.fillRoundedRectangle (tag, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.drawText (t.name, tag.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
        if (t.current)
            paintCurrentMark (g, box.withTrimmedTop (2.0f).withHeight (titleBand));
    }

    // --- the tabs (FL: All big in the middle, Patterns and Channels after it; the shown one white) ------------------
    juce::String tabLabel (int i) const { return i == 0 ? tr ("All") : i == 1 ? tr ("Patterns") : tr ("Channels"); }

    juce::Rectangle<int> tabRect (int i) const
    {
        const auto cy = pickerview::tabCentreY (stripReach, getHeight(), 32);
        const auto allW = juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font (26.0f)), tabLabel (0)) + 8;
        const auto pw = juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font (19.0f)), tabLabel (1)) + 8;
        const auto cw = juce::GlyphArrangement::getStringWidthInt (juce::Font (theme::font (19.0f)), tabLabel (2)) + 8;
        // (FL on 1920 px: "All" centred at 852, Patterns at 941, Channels at 1049 — the three as one group)
        const auto total = allW + 30 + pw + 30 + cw;
        auto x = getWidth() / 2 - total / 2;
        const int widths[] = { allW, pw, cw };
        for (int k = 0; k < i; ++k)
            x += widths[k] + 30;
        return juce::Rectangle<int> (x, cy - 16, widths[i], 32);
    }

    juce::Rectangle<int> tabStrip() const { return tabRect (0).getUnion (tabRect (2)).expanded (24, 24); }

    int tabAt (juce::Point<int> p) const
    {
        for (int i = 0; i < 3; ++i)
            if (tabRect (i).expanded (4, 8).contains (p))
                return i;
        return -1;
    }

    void paintTabs (juce::Graphics& g)
    {
        using namespace theme;
        for (int i = 0; i < 3; ++i)
        {
            const auto r = tabRect (i);
            if (i == hoverTab)
                pickerlook::paintHighlight (g, pickerlook::highlight (r, r.getHeight(), true), false);
            g.setColour (i == tab ? colour::textPrimary : colour::textSecondary.withMultipliedAlpha (0.85f));
            g.setFont (font (i == 0 ? 26.0f : 19.0f, i == tab));
            g.drawText (tabLabel (i), r, juce::Justification::centred, false);
            if (i == tab) // (FL's mark above the shown tab)
            {
                juce::Path mark;
                const auto cx = (float) r.getCentreX(), top = (float) r.getY() - 2.0f;
                mark.addTriangle (cx - 4.5f, top, cx + 4.5f, top, cx, top - 5.0f);
                g.fillPath (mark);
            }
        }
    }

    // --- layout ---------------------------------------------------------------------------------------------------
    int tileAt (juce::Point<int> p) const
    {
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()) && tiles[(std::size_t) hoverTile].bounds.contains (p))
            return hoverTile;
        for (int i = (int) tiles.size(); --i >= 0;)
            if (tiles[(std::size_t) i].bounds.contains (p))
                return i;
        return -1;
    }

    void setHover (int i)
    {
        if (i == hoverTile)
            return;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (hoverArea (hoverTile));
        hoverTile = i;
        if (juce::isPositiveAndBelow (hoverTile, (int) tiles.size()))
            repaint (hoverArea (hoverTile));
    }

    void pack (int which, int& reach)
    {
        const pickerview::PackBox all { edgeX, edgeTop, getWidth() - edgeX, getHeight() - edgeBottom };
        const auto half = ((all.right - all.left) - halvesGap) / 2;
        auto leftBox = all, rightBox = all;
        if (which != 1)
            leftBox.right = all.left + half;
        if (which != 2)
            rightBox.left = all.right - half;
        for (const auto left : { true, false })
        {
            std::vector<int> ids;
            std::vector<pickerview::PackTile> in;
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (tiles[(std::size_t) i].left() == left)
                {
                    pickerview::PackTile p;
                    p.w = tiles[(std::size_t) i].w;
                    p.h = tiles[(std::size_t) i].h;
                    p.shown = (which == 0 || (which == 1) == left) && tiles[(std::size_t) i].matching;
                    ids.push_back (i);
                    in.push_back (p);
                }
            const auto r = pickerview::packRows (in, left ? leftBox : rightBox, tileGap, rowGap);
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
        if (stripFor != getLocalBounds()) // (the tab strip stays where the All tab puts it)
        {
            int reach = 0;
            pack (0, reach);
            stripReach = reach;
            stripFor = getLocalBounds();
        }
        int unused = 0;
        pack (tab, unused);
    }

    void timerCallback() override
    {
        if (dragging)
        {
            // the drag ended (dropped or cancelled): the picker closes
            auto* container = dragContainerOwner != nullptr ? dynamic_cast<juce::DragAndDropContainer*> (dragContainerOwner.getComponent()) : nullptr;
            if (container == nullptr)
                container = juce::DragAndDropContainer::findParentDragContainerFor (dragContainerOwner != nullptr ? dragContainerOwner.getComponent() : this);
            if (container == nullptr || ! container->isDragAndDropActive())
                close();
            return;
        }
        if (getAlpha() < 1.0f)
            setAlpha ((float) juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() - openedAt) / 140.0));
        else
            stopTimer(); // (nothing else to do: no idle work)
    }

    AudioEngine& engine;
    ProjectPickerTargets targets;
    std::vector<PTile> tiles;
    juce::Image backdrop, backdropShown;
    juce::Rectangle<int> stripFor;
    int stripReach = 0;
    juce::String searchText;
    int tab = 0, hoverTab = -1, hoverTile = -1, pressedTile = -1;
    bool dragging = false;
    juce::Component::SafePointer<juce::Component> dragContainerOwner;
    float wheelAccum = 0.0f;
    double openedAt = 0.0;
};
} // namespace

std::unique_ptr<juce::Component> makeProjectPickerView (AudioEngine& engine, ProjectPickerTargets targets)
{
    return std::make_unique<ProjectPickerView> (engine, std::move (targets));
}

void takeProjectPickerBackdrop (juce::Component& view, juce::Rectangle<int> screenArea)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        picker->takeBackdrop (screenArea);
}

std::pair<int, int> projectPickerTileAt (juce::Component& view, juce::Point<int> p)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        return picker->tileKindAt (p);
    return { 0, -1 };
}

juce::Rectangle<int> projectPickerTileBounds (juce::Component& view, int kind, int index)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        return picker->boundsOf (kind, index);
    return {};
}

void projectPickerHoverForSnapshot (juce::Component& view, juce::Point<int> p)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        picker->hoverForSnapshot (p);
}

void projectPickerBackdropForSnapshot (juce::Component& view, const juce::Image& screen)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        picker->backdropForSnapshot (screen);
}

void projectPickerTypeForSnapshot (juce::Component& view, const juce::String& text)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        picker->typeForSnapshot (text);
}

void projectPickerShowTabForSnapshot (juce::Component& view, int tab)
{
    if (auto* picker = dynamic_cast<ProjectPickerView*> (&view))
        picker->showTab (tab);
}
} // namespace lynx
