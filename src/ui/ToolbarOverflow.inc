// Round 190qg (Discord #feedback 11-23): the main toolbar never hides anything silently, FL's "Single line" toolbar, and the
// Grouped look.
//
// - Overflow: in the Pattern layout, whatever the window has no room for (whole panels, and the switches a tighter panel
//   form leaves out) goes to a "»" button at the toolbar's right end; its menu lists every such item with its icon and
//   name, and works like the item (a switch toggles, a window button opens the window, the master knobs, the tempo and the
//   time are the real controls inside the menu). FL 2026 has the same principle for its buttons (a ▸ at the right end
//   opens "More actions" with the hidden buttons, captured in raw/fs-1280-arrow.png), but drops its panels silently; LYNX
//   lists the panels too. The Simple and Clip-launch bars keep their "⋯" menus, which now list every part they left out.
// - One row: FL's "Single line" preset (core/ToolbarLayout.h flClassicOrder): the menus, the hint bar, then the panels on
//   one 41 px row in FL's order; what does not fit drops from the right end into the overflow, as FL's does.
// - Grouped (LYNX's own, Layout::grouped): raised frames, steady spacing (MainComponent::resized / paint).
//
// Performance: no timer of its own on the toolbar. The overflow menu's live controls refresh at 15 Hz only while the menu
// is open (WidgetItem); nothing is added to the idle paint.

#include "ui/MainComponent.h" // (included at the end of MainComponent.cpp)

#include "core/DrawMode.h"
#include "ui/I18n.h"
#include "ui/SimpleWorkflow.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
using toolbarlayout::Item;

// A LYNX vector icon as a menu item's picture (drawn on its 24-unit grid, scaled into the menu's icon box)
class IconDrawable final : public juce::Drawable
{
public:
    IconDrawable (Icon i, juce::Colour c) : icon (i), colour (c) { setBounds (0, 0, 24, 24); }
    void paint (juce::Graphics& g) override { icons::draw (g, icon, { 0.0f, 0.0f, 24.0f, 24.0f }, colour); }
    std::unique_ptr<juce::Drawable> createCopy() const override { return std::make_unique<IconDrawable> (icon, colour); }
    juce::Rectangle<float> getDrawableBounds() const override { return { 0.0f, 0.0f, 24.0f, 24.0f }; }
    juce::Path getOutlineAsPath() const override
    {
        juce::Path p;
        p.addRectangle (0.0f, 0.0f, 24.0f, 24.0f);
        return p;
    }

private:
    Icon icon;
    juce::Colour colour;
};

std::unique_ptr<juce::Drawable> iconPicture (Icon icon)
{
    return icon == Icon::none ? nullptr : std::make_unique<IconDrawable> (icon, theme::colour::textSecondary);
}

// A row of the overflow menu that holds a real toolbar control (a fresh one of the same kind): its icon and name at the
// left, the control at the right. Clicking the control does not close the menu. While the menu is open, `tick` refreshes
// the control 15 times a second (the toolbar's own copy is refreshed by the main window's timer).
class WidgetItem final : public juce::PopupMenu::CustomComponent,
                         private juce::Timer
{
public:
    WidgetItem (Icon i, const juce::String& n, std::unique_ptr<juce::Component> w, int ww, int wh, std::function<void (juce::Component&)> tickFn = {})
        : juce::PopupMenu::CustomComponent (false), icon (i), name (n), widget (std::move (w)), widgetWidth (ww), widgetHeight (wh), tick (std::move (tickFn))
    {
        addAndMakeVisible (*widget);
        if (tick != nullptr)
        {
            tick (*widget);
            startTimerHz (15);
        }
    }

    void getIdealSize (int& w, int& h) override
    {
        const auto text = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), name));
        w = 28 + text + 16 + widgetWidth + 8;
        h = juce::jmax (theme::menus::itemHeight(), widgetHeight + 6);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedRight (8);
        widget->setBounds (r.removeFromRight (widgetWidth).withSizeKeepingCentre (widgetWidth, widgetHeight));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        auto r = getLocalBounds();
        const auto iconBox = r.removeFromLeft (juce::roundToInt ((float) juce::jmin (getHeight(), menus::itemHeight()) * 0.8f) + 4).toFloat();
        icons::draw (g, icon, iconBox.withSizeKeepingCentre (14.0f, 14.0f), colour::textSecondary);
        g.setFont (font (type::body));
        g.setColour (colour::textPrimary);
        g.drawText (name, r.withTrimmedRight (widgetWidth + 16), juce::Justification::centredLeft, true);
    }

private:
    void timerCallback() override
    {
        if (isShowing())
            tick (*widget);
    }

    Icon icon;
    juce::String name;
    std::unique_ptr<juce::Component> widget;
    int widgetWidth, widgetHeight;
    std::function<void (juce::Component&)> tick;
};

void addWidget (juce::PopupMenu& menu, Icon icon, const juce::String& name, std::unique_ptr<juce::Component> widget, int w, int h,
                std::function<void (juce::Component&)> tick = {})
{
    juce::PopupMenu::Item item (name);
    item.customComponent = new WidgetItem (icon, name, std::move (widget), w, h, std::move (tick));
    menu.addItem (std::move (item));
}

void addIconItem (juce::PopupMenu& menu, Icon icon, const juce::String& text, bool enabled, bool ticked, std::function<void()> action)
{
    juce::PopupMenu::Item item (text);
    item.isEnabled = enabled;
    item.isTicked = ticked;
    item.image = iconPicture (icon);
    item.action = std::move (action);
    menu.addItem (std::move (item));
}

void addIconSubMenu (juce::PopupMenu& menu, Icon icon, const juce::String& text, juce::PopupMenu sub)
{
    juce::PopupMenu::Item item (text);
    item.subMenu = std::make_unique<juce::PopupMenu> (std::move (sub));
    item.image = iconPicture (icon);
    menu.addItem (std::move (item));
}

// Every text of a menu, its submenus' too (the check below looks names up in it)
void collectTexts (const juce::PopupMenu& menu, juce::StringArray& out)
{
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.text.isNotEmpty())
            out.add (item.text);
        if (item.subMenu != nullptr)
            collectTexts (*item.subMenu, out);
    }
}

bool listed (const juce::StringArray& texts, const juce::String& name)
{
    for (const auto& t : texts)
        if (t.startsWith (name))
            return true;
    return false;
}
} // namespace

// --- the overflow button --------------------------------------------------------------------------------------------------

void MainComponent::placeOverflowButton (juce::Rectangle<int> area)
{
    if (area.isEmpty())
    {
        if (overflowButton.isVisible())
            overflowButton.setVisible (false);
        overflowButton.setBounds ({});
        return;
    }
    if (overflowButton.getParentComponent() != this)
    {
        addChildComponent (overflowButton);
        overflowButton.setMouseClickGrabsKeyboardFocus (false);
        overflowButton.setWantsKeyboardFocus (false);
        overflowButton.setFlat (true);
        overflowButton.setIconSizing (0.5f, 14.0f);
        overflowButton.onClick = [this] {
            makeToolbarOverflowMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&overflowButton));
        };
        overflowButton.onRightClick = overflowButton.onClick;
    }
    // the hint names what is in it, so a glance at the hint bar says where the missing panels went
    juce::StringArray names;
    const auto count = [] (const std::vector<Item>& items, toolbarlayout::Group g) {
        return (int) std::count_if (items.begin(), items.end(), [g] (Item i) { return toolbarlayout::groupOf (i) == g; });
    };
    std::vector<Item> all;
    for (int i = 0; i < toolbarlayout::itemCount; ++i)
        all.push_back ((Item) i);
    for (auto item : overflowList)
    {
        const auto group = toolbarlayout::groupOf (item);
        const auto text = count (overflowList, group) == count (all, group) ? tr (toolbarlayout::groupName (group)) // a whole panel
                                                                              : tr (toolbarlayout::itemName (item)); // a single switch
        names.addIfNotAlreadyThere (text);
    }
    const auto text = tr ("More toolbar items: what this window size has no room for") + " (" + names.joinIntoString (", ") + ")";
    if (overflowButton.getTooltip() != text)
    {
        overflowButton.setTooltip (text);
        ui::setHint (overflowButton, text, true);
    }
    overflowButton.setBounds (area);
    if (! overflowButton.isVisible())
        overflowButton.setVisible (true);
}

juce::Component* MainComponent::toolbarItemComponent (Item item)
{
    switch (item)
    {
        case Item::patternSong: return &patternModeButton;
        case Item::play: return &playButton;
        case Item::stop: return &stopButton;
        case Item::record: return &recordButton;
        case Item::tempo: return &tempoDisplay;
        case Item::tapTempo: return &tapTempoButton;
        case Item::time: return &timeDisplay;
        case Item::songPosition: return &songPosition;
        case Item::masterVolume: return &masterVolume;
        case Item::masterPitch: return &masterPitch;
        case Item::metronome: return &metronomeButton;
        case Item::waitInput: return &waitInputButton;
        case Item::countIn: return &countInButton;
        case Item::blend: return &blendButton;
        case Item::typingPiano: return &typingPianoButton;
        case Item::loopRecord: return &loopRecordButton;
        case Item::stepEdit: return &stepEditButton;
        case Item::punch: return &punchButton;
        case Item::recordMenu: return &recordMenuButton;
        case Item::midiLight: return &midiLight;
        case Item::snap: return &gridBox;
        case Item::pattern: return &patternBox;
        case Item::patternMenu: return &patternMenuButton;
        case Item::playlist: return &panelButtons[(std::size_t) Panel::playlist];
        case Item::channelRack: return &panelButtons[(std::size_t) Panel::channelRack];
        case Item::pianoRoll: return &panelButtons[(std::size_t) Panel::pianoRoll];
        case Item::mixer: return &panelButtons[(std::size_t) Panel::mixer];
        case Item::browser: return &panelButtons[(std::size_t) Panel::browser];
        case Item::pluginPicker: return &pluginPickerButton;
        case Item::newFile: return &newButton;
        case Item::open: return &openButton;
        case Item::save: return &saveButton;
        case Item::exportFile: return &exportButton;
        case Item::undo: return &undoButton;
        case Item::redo: return &redoButton;
        case Item::audioEditor: return &audioEditorButton;
        case Item::meters: return &perfMeter;
        case Item::output: return &output;
        case Item::chord: return &chordPanel;
        case Item::multilink: return &multilinkButton;
        case Item::key: return &keyPanel;
        case Item::link: return &linkToggle;
        case Item::count_: break;
    }
    return nullptr;
}

// One overflow entry: the item's icon (a button's own glyph) and name, working as the item does
void MainComponent::addOverflowItem (juce::PopupMenu& menu, Item item)
{
    auto* component = toolbarItemComponent (item);
    const auto name = juce::String (item >= Item::playlist && item <= Item::browser ? toolbarlayout::itemName (item) : tr (toolbarlayout::itemName (item)));
    auto* button = dynamic_cast<IconButton*> (component);
    const auto glyph = button != nullptr ? button->getIcon() : Icon::none;
    auto click = [this, item] {
        if (auto* b = dynamic_cast<juce::Button*> (toolbarItemComponent (item)))
            b->triggerClick();
    };
    auto withKey = [this] (const juce::String& text, const char* action) { return text + shortcutText (action); };
    auto tick = [] (auto& c) { c.update(); };

    switch (item)
    {
        case Item::patternSong:
        {
            const auto song = engine.getSong().mode == PlayMode::song;
            addIconItem (menu, Icon::pattern, name + ": " + tr ("Pattern"), true, ! song, [this] { patternModeButton.triggerClick(); });
            addIconItem (menu, Icon::song, name + ": " + tr ("Song"), true, song, [this] { songModeButton.triggerClick(); });
            return;
        }
        case Item::tempo:
            addWidget (menu, Icon::metronome, name, std::make_unique<TempoDisplay> (engine), 96, 24, [] (juce::Component& c) { c.repaint(); });
            return;
        case Item::time:
            addWidget (menu, Icon::song, name, std::make_unique<toolbar::TimeDisplay> (engine), 132, 26,
                       [tick] (juce::Component& c) { tick (static_cast<toolbar::TimeDisplay&> (c)); });
            return;
        case Item::songPosition:
            addWidget (menu, Icon::arrangementView, name, std::make_unique<toolbar::SongPositionBar> (engine), 160, 12,
                       [tick] (juce::Component& c) { tick (static_cast<toolbar::SongPositionBar&> (c)); });
            return;
        case Item::masterVolume:
        case Item::masterPitch:
        {
            const auto kind = item == Item::masterVolume ? toolbar::MasterKnob::Kind::volume : toolbar::MasterKnob::Kind::pitch;
            addWidget (menu, item == Item::masterVolume ? Icon::mixer : Icon::tuningFork, name, std::make_unique<toolbar::MasterKnob> (engine, kind), 96, 28,
                       [tick] (juce::Component& c) { tick (static_cast<toolbar::MasterKnob&> (c)); });
            return;
        }
        case Item::link:
        {
            auto toggle = std::make_unique<toolbar::LinkToggle> (engine);
            toggle->onConfigure = linkToggle.onConfigure;
            addWidget (menu, Icon::link, name, std::move (toggle), toolbar::LinkToggle::idealWidth (24), 24,
                       [tick] (juce::Component& c) { tick (static_cast<toolbar::LinkToggle&> (c)); });
            return;
        }
        case Item::recordMenu: addIconSubMenu (menu, Icon::mic, name, buildRecordMenu()); return;
        case Item::midiLight:
            addIconItem (menu, Icon::keyboard, name + ": " + tr ("reconnect the MIDI inputs"), midiLight.onReconnect != nullptr, false, [this] {
                if (midiLight.onReconnect != nullptr)
                    midiLight.onReconnect();
            });
            return;
        case Item::snap:
        {
            juce::PopupMenu snap;
            for (int i = 0; i < gridBox.getNumItems(); ++i)
                snap.addItem (gridBox.getItemText (i), true, gridBox.getSelectedItemIndex() == i, [this, i] { gridBox.setSelectedItemIndex (i); });
            addIconSubMenu (menu, Icon::magnet, name + ": " + gridBox.getText(), std::move (snap));
            return;
        }
        case Item::pattern:
        {
            juce::PopupMenu patterns;
            const auto& song = engine.getSong();
            for (int p = 0; p < (int) song.patterns.size() && p < 200; ++p)
                patterns.addItem (juce::String::fromUTF8 (song.patterns[(std::size_t) p].name.c_str()), true, song.currentPattern == p, [this, p] { selectPattern (p); });
            patterns.addSeparator();
            patterns.addItem (tr ("New pattern"), [this] { newPatternButton.triggerClick(); });
            patterns.addSubMenu (tr ("Pattern"), makePatternMenu());
            addIconSubMenu (menu, Icon::pattern, name + ": " + patternBox.getText(), std::move (patterns));
            return;
        }
        case Item::patternMenu: // round 190sc: the "…" beside the pattern box, its own entry like every item of the overflow
            addIconSubMenu (menu, Icon::pattern, name, makePatternMenu());
            return;
        case Item::playlist:
        case Item::channelRack:
        case Item::pianoRoll:
        case Item::mixer:
        case Item::browser:
        {
            const auto panel = (Panel) ((int) item - (int) Item::playlist);
            static constexpr const char* actions[] { "panel.playlist", "panel.channelRack", "panel.pianoRoll", "panel.mixer", "panel.browser" };
            const auto* action = actions[(int) panel];
            addIconItem (menu, glyph, withKey (name, action), true, workspace.isPanelVisible (panel), [this, action] { runAction (action); });
            return;
        }
        case Item::pluginPicker: addIconItem (menu, glyph, withKey (name, "view.pluginPicker"), true, false, [this] { togglePluginPicker(); }); return;
        case Item::newFile: addIconItem (menu, glyph, withKey (name, "file.new"), true, false, [this] { runAction ("file.new"); }); return;
        case Item::open: addIconItem (menu, glyph, withKey (name, "file.open"), true, false, [this] { runAction ("file.open"); }); return;
        case Item::save: addIconItem (menu, glyph, withKey (name, "file.save"), true, false, [this] { runAction ("file.save"); }); return;
        case Item::exportFile: addIconItem (menu, glyph, withKey (name, "file.export"), true, false, [this] { runAction ("file.export"); }); return;
        case Item::undo: addIconItem (menu, glyph, withKey (name, "edit.undo"), true, false, [this] { runAction ("edit.undo"); }); return;
        case Item::redo: addIconItem (menu, glyph, withKey (name, "edit.redo"), true, false, [this] { runAction ("edit.redo"); }); return;
        case Item::meters:
            addIconItem (menu, Icon::graph, name, perfMeter.onOpenMonitor != nullptr, false, [this] {
                if (perfMeter.onOpenMonitor != nullptr)
                    perfMeter.onOpenMonitor();
            });
            return;
        case Item::output: addIconItem (menu, Icon::equalizer, name + " (" + juce::String ("Mixer") + ")", true, false, [this] { runAction ("panel.mixer"); }); return;
        case Item::chord: addIconItem (menu, Icon::chord, name, true, false, [this] { showChordPanelMenu(); }); return;
        case Item::key: addIconItem (menu, Icon::scaleSnap, name, true, false, [this] { showKeyMenu(); }); return;
        case Item::play:
        case Item::stop:
        case Item::record:
        case Item::tapTempo:
        case Item::metronome:
        case Item::waitInput:
        case Item::countIn:
        case Item::blend:
        case Item::typingPiano:
        case Item::loopRecord:
        case Item::stepEdit:
        case Item::punch:
        case Item::audioEditor:
        case Item::multilink:
        default:
            addIconItem (menu, glyph, name, component != nullptr && component->isEnabled(), button != nullptr && button->getToggleState(), click);
            return;
    }
}

juce::PopupMenu MainComponent::makeToolbarOverflowMenu()
{
    juce::PopupMenu menu;
    std::optional<toolbarlayout::Group> section;
    for (auto item : overflowList)
    {
        if (const auto group = toolbarlayout::groupOf (item); section != group)
        {
            menu.addSectionHeader (tr (toolbarlayout::groupName (group)));
            section = group;
        }
        addOverflowItem (menu, item);
    }
    menu.addSeparator();
    menu.addItem (tr ("Customize toolbar..."), [this] { showCustomizeToolbar(); });
    return menu;
}

// --- FL's "Single line" toolbar --------------------------------------------------------------------------------------------

void MainComponent::layoutOneRowToolbar (juce::Rectangle<int> bar, int menuWidth)
{
    using toolbarlayout::Group;
    hideToolbarParts();
    dividers.clear();
    panelBounds = {};
    snapLabelArea = patternLabelArea = transportArea = modeArea = {};
    compactRecord = narrowSnap = false;
    fileWide = true;
    const auto h = rowHeight, grow = h - toolbarlayout::rowHeight (toolbarlayout::IconSize::medium);
    const auto put = [] (juce::Component& c, juce::Rectangle<int> r) {
        c.setVisible (true);
        c.setBounds (r);
    };
    auto toolbar = bar.reduced (6, 0);
    // the row: FL's single line keeps its panels in the band's middle (raw/fs-1920.png: 41 px band, controls ~32 px)
    const auto rowY = bar.getY() + (bar.getHeight() - h) / 2;
    const auto rowAt = [&] (int x, int w, int height) { return juce::Rectangle<int> (x, bar.getY() + (bar.getHeight() - height) / 2, w, height); };

    // widths (FL's single line: PAT/SONG and the transport with the tempo, then the time, monitor, pattern and snap, the
    // window buttons and the CPU panel)
    const auto modeHeight = (h - 2) / 2;
    const auto modeWidth = juce::jmax (patternModeButton.getIdealWidth (modeHeight), songModeButton.getIdealWidth (modeHeight), 34);
    const int tempoWidth = 84 + grow, tapWidth = juce::roundToInt (h * 0.8f), timeWidth = 136 + grow; // (FL's time panel: 136)
    const int patternBoxWidth = 112, gridWidth = 128, step = h + 2;
    std::array<int, toolbarlayout::groupCount> widths {};
    widths[(std::size_t) Group::transport] = modeWidth + 6 + 3 * step + 4 + tempoWidth + 2 + tapWidth + 12 + timeWidth;
    widths[(std::size_t) Group::master] = 2 * 92 + 4;
    widths[(std::size_t) Group::record] = 10 * step - 2;
    widths[(std::size_t) Group::snap] = step + patternBoxWidth + 2 + step + 8 + step + gridWidth;
    widths[(std::size_t) Group::windows] = 6 * step - 2;
    widths[(std::size_t) Group::file] = 7 * step - 2;
    widths[(std::size_t) Group::meters] = 118;
    widths[(std::size_t) Group::output] = 120 + grow * 2;
    widths[(std::size_t) Group::chord] = 96;
    widths[(std::size_t) Group::multilink] = h;
    widths[(std::size_t) Group::key] = 112;
    widths[(std::size_t) Group::link] = toolbar::LinkToggle::idealWidth (h);

    // the menus and the hint bar first (FL: menu 300, hint panel 248); the hint keeps 200 px when it can
    const auto menuColumn = menuWidth + 8;
    int minHint = 200;
    const int fullHint = 248;
    const int gap = 12;
    const auto ow = toolbarlayout::overflowWidth (h) + 6;
    auto room = toolbar.getWidth();
    // FL's single line drops its panels from the right end (core/ToolbarLayout.h fitFromTheRight)
    const auto fitRow = [&] (const std::array<int, toolbarlayout::groupCount>& w) { return toolbarlayout::fitFromTheRight (toolbarLayout, w, gap, room, menuColumn + 6 + minHint); };
    auto visible = fitRow (widths);
    // a narrower window: the recording panel without the typing-piano switch and the MIDI light, the File panel without the
    // audio editor, if that keeps more panels; then the overflow button's room
    const auto shownCount = [] (const std::array<bool, toolbarlayout::groupCount>& v) { return (int) std::count (v.begin(), v.end(), true); };
    auto tighter = widths;
    tighter[(std::size_t) Group::record] = 8 * step - 2;
    tighter[(std::size_t) Group::file] = 6 * step - 2;
    const auto anyDropped = [&] (const std::array<bool, toolbarlayout::groupCount>& v) {
        return std::any_of (toolbarLayout.entries.begin(), toolbarLayout.entries.end(), [&v] (const auto& e) { return e.shown && ! v[(std::size_t) e.group]; });
    };
    if (anyDropped (visible))
    {
        room -= ow; // (something goes to the overflow in any case)
        visible = fitRow (widths);
        if (const auto t = fitRow (tighter); shownCount (t) > shownCount (visible))
        {
            visible = t;
            widths = tighter;
            compactRecord = true;
            fileWide = false;
        }
    }
    // the narrowest windows (800 px at a large Interface size; FL's single line then cuts its time panel, raw/fs-800.png):
    // the transport never goes, so the hint gets narrower, then the time (with the position bar) and the tempo tapper move
    // to the overflow; the transport buttons and the tempo stay
    const auto usedBy = [&] (const std::array<bool, toolbarlayout::groupCount>& v) {
        int u = 0;
        for (int i = 0; i < toolbarlayout::groupCount; ++i)
            u += v[(std::size_t) i] ? widths[(std::size_t) i] + gap : 0;
        return u;
    };
    bool noTime = false, noTap = false;
    const auto tooWide = [&] { return usedBy (visible) + menuColumn + 6 + minHint > room; };
    if (tooWide())
    {
        if (room == toolbar.getWidth())
            room -= ow;
        minHint = 120;
        if (tooWide() && visible[(std::size_t) Group::transport])
        {
            noTime = true;
            widths[(std::size_t) Group::transport] -= 12 + timeWidth;
        }
        if (tooWide() && visible[(std::size_t) Group::transport])
        {
            noTap = true;
            widths[(std::size_t) Group::transport] -= 2 + tapWidth;
        }
        if (tooWide())
            minHint = 60; // (the hint bar's last pixels before anything covers the overflow button)
    }
    {
        std::vector<Item> inside;
        if (noTap)
            inside.push_back (Item::tapTempo);
        if (noTime)
            inside.insert (inside.end(), { Item::time, Item::songPosition });
        if (compactRecord && visible[(std::size_t) Group::record])
            inside.insert (inside.end(), { Item::typingPiano, Item::midiLight });
        if (! fileWide && visible[(std::size_t) Group::file])
            inside.push_back (Item::audioEditor);
        overflowList = toolbarlayout::overflowItems (toolbarLayout, visible, inside);
    }
    if (! overflowList.empty() && room == toolbar.getWidth())
        room -= ow;
    juce::Rectangle<int> overflowArea;
    if (! overflowList.empty())
        overflowArea = toolbar.removeFromRight (ow).withTrimmedLeft (6);

    int used = 0, shownGroups = 0;
    for (int i = 0; i < toolbarlayout::groupCount; ++i)
        if (visible[(std::size_t) i])
        {
            used += widths[(std::size_t) i] + gap;
            ++shownGroups;
        }
    auto hintWidth = juce::jlimit (juce::jmin (minHint, juce::jmax (60, room - used - menuColumn - 6)), fullHint, room - used - menuColumn - 6);
    auto spare = juce::jmax (0, room - used - menuColumn - 6 - hintWidth);
    // FL's spacers: a little air between the panels (at most 14 px more each; none in the Grouped look), the rest before
    // the panels at the right end
    const auto extra = toolbarLayout.grouped ? 0 : toolbarlayout::extraGap (spare, shownGroups, 14);
    spare -= extra * shownGroups;

    // 1. the menus and the hint bar, on the row
    {
        const auto menu = rowAt (toolbar.getX(), juce::jmin (menuColumn, toolbar.getWidth()), h);
        put (menuBar, menu);
        titleArea = {};
        auto hintRow = rowAt (menu.getRight() + 6, juce::jmax (0, hintWidth), h).reduced (0, 1);
        placePaletteButton (hintRow, true);
        placeHintBar (hintRow);
        fixedToolbarArea = juce::Rectangle<int> (toolbar.getX() - 4, 0, menuColumn + 6 + hintWidth + 8, toolbarHeight);
        toolbar.removeFromLeft (menuColumn + 6 + hintWidth);
    }
    // round 190oo (WG-11): the menu bar places its titles in its own resized(); a new look at the same bounds needs one
    if (auto& props = menuBar.getProperties(); props.getWithDefault ("lynxMenuLaidOut", juce::var()).toString() != "1row")
    {
        props.set ("lynxMenuLaidOut", "1row");
        menuBar.resized();
        menuBar.repaint();
    }

    // 2. the panels, in the user's order; the spare room before the run of right-end panels (FL: before its CPU panel)
    // (counted on the panels the user shows, so a panel that went to the overflow does not pull another one to the right
    // end: FL at 1280 px keeps its monitor next to the time, raw/fs-1280.png)
    std::array<bool, toolbarlayout::groupCount> userShown {};
    for (const auto& e : toolbarLayout.entries)
        userShown[(std::size_t) e.group] = e.shown;
    const auto rightRun = toolbarlayout::firstOfRightRun (toolbarLayout, userShown);
    for (int index = 0; index < (int) toolbarLayout.entries.size(); ++index)
    {
        const auto group = toolbarLayout.entries[(std::size_t) index].group;
        if (! visible[(std::size_t) group])
            continue;
        if (rightRun >= 0 && index >= rightRun && spare > 0)
        {
            toolbar.removeFromLeft (spare);
            spare = 0;
        }
        toolbar.removeFromLeft (gap / 2 + extra / 2);
        dividers.push_back (toolbar.getX());
        toolbar.removeFromLeft (gap - gap / 2 + extra - extra / 2);
        auto panel = toolbar.removeFromLeft (widths[(std::size_t) group]);
        panelBounds[(std::size_t) group] = juce::Rectangle<int> (panel.getX() - 5, 0, panel.getWidth() + 10, toolbarHeight);
        auto x = panel.getX();
        const auto button = [&] (juce::Component& c) {
            put (c, juce::Rectangle<int> (x, rowY, h, h));
            x += step;
        };
        switch (group)
        {
            case Group::transport:
            {
                // FL's PAT / SONG: two small halves, one over the other
                const auto mode = rowAt (x, modeWidth, 2 * modeHeight + 2);
                modeArea = mode.expanded (2, 1);
                put (patternModeButton, mode.withHeight (modeHeight));
                put (songModeButton, mode.withTrimmedTop (modeHeight + 2));
                x += modeWidth + 6;
                transportArea = juce::Rectangle<int> (x, rowY, 3 * step - 2, h);
                for (auto* b : { &playButton, &stopButton, &recordButton })
                    button (*b);
                x += 2;
                put (tempoDisplay, rowAt (x, tempoWidth, h - 6));
                x += tempoWidth + 2;
                if (noTap)
                    break;
                put (tapTempoButton, rowAt (x, tapWidth, h - 6));
                x += tapWidth + 12;
                if (noTime)
                    break;
                // the time, with LYNX's song position as a thin bar under it (FL's single line has no position bar)
                const auto time = rowAt (x, timeWidth, h + 4);
                put (timeDisplay, time.withTrimmedBottom (6));
                put (songPosition, time.withTop (time.getBottom() - 5));
                break;
            }
            case Group::master:
                put (masterVolume, rowAt (x, 92, h));
                put (masterPitch, rowAt (x + 96, 92, h));
                break;
            case Group::record:
                for (auto* b : { &metronomeButton, &waitInputButton, &countInButton, &blendButton })
                    button (*b);
                if (! compactRecord)
                    button (typingPianoButton);
                for (auto* b : { &loopRecordButton, &stepEditButton, &punchButton, &recordMenuButton })
                    button (*b);
                if (! compactRecord)
                    put (midiLight, juce::Rectangle<int> (x, rowY, h, h).reduced (h / 4));
                break;
            case Group::snap: // FL's order: the pattern selector, then the snap
                button (patternMenuButton);
                put (patternBox, rowAt (x, patternBoxWidth, h - 6));
                x += patternBoxWidth + 2;
                button (newPatternButton);
                x += 8;
                button (snapToggleButton);
                put (gridBox, rowAt (x, gridWidth, h - 6));
                break;
            case Group::windows: // FL's single line: Playlist, Piano roll, Channel rack, Mixer, Browser (then LYNX's plugin picker)
                for (auto p : { Panel::playlist, Panel::pianoRoll, Panel::channelRack, Panel::mixer, Panel::browser })
                    button (panelButtons[(std::size_t) p]);
                button (pluginPickerButton);
                break;
            case Group::file:
                for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton })
                    button (*b);
                if (fileWide)
                    button (audioEditorButton);
                break;
            case Group::meters: put (perfMeter, rowAt (x, panel.getWidth(), h + 6)); break;
            case Group::output: put (output, rowAt (x, panel.getWidth(), h + 6)); break;
            case Group::chord: put (chordPanel, rowAt (x, panel.getWidth(), h + 6)); break;
            case Group::multilink: button (multilinkButton); break;
            case Group::key: put (keyPanel, rowAt (x, panel.getWidth(), h + 6)); break;
            case Group::link: put (linkToggle, rowAt (x, panel.getWidth(), h - 2)); break;
            case Group::count_: break;
        }
    }
    placeOverflowButton (overflowArea.isEmpty() ? overflowArea : overflowArea.withY (rowY).withHeight (h));
}

// --- the Simple and Clip-launch bars: their "⋯" menus list every part the window had no room for --------------------------

namespace
{
juce::String liveItemText (toolbarlayout::livebar::Item i)
{
    using I = toolbarlayout::livebar::Item;
    switch (i)
    {
        case I::link: return tr ("Ableton Link");
        case I::browser: return "Browser";
        case I::tap: return tr ("Tap tempo");
        case I::nudgeDown: return tr ("Nudge the tempo down");
        case I::nudgeUp: return tr ("Nudge the tempo up");
        case I::signature: return tr ("Time signature");
        case I::metronome: return tr ("Metronome");
        case I::quantisation: return tr ("Launch quantization");
        case I::key: return tr ("Project key");
        case I::backToArrangement: return tr ("Back to Playlist");
        case I::overdub: return tr ("Blend recorded notes");
        case I::automationArm: return tr ("Automation Arm");
        case I::reEnableAutomation: return tr ("Re-Enable Automation");
        case I::capture: return tr ("Capture MIDI: the notes just played without recording into the pattern");
        case I::sessionRecord: return tr ("Session Record");
        case I::punch: return tr ("Punch-In");
        case I::loopSwitch: return tr ("Loop");
        case I::punchOut: return tr ("Punch-Out");
        case I::draw: return tr ("Draw Mode");
        case I::computerKeys: return tr ("Typing keyboard to piano");
        case I::keyMap: return tr ("Key Map Mode: map computer keys to clips, scenes, buttons and controls (Ctrl+K)");
        case I::midiMap: return tr ("MIDI Map Mode: map MIDI controls to clips, scenes, buttons and controls (Ctrl+M)");
        case I::cpu: return tr ("CPU, memory and polyphony");
        case I::tempo:
        case I::position:
        case I::play:
        case I::stop:
        case I::record:
        case I::more:
        case I::sessionView:
        case I::arrangementView:
        case I::count_: break; // (never left out: priority 100)
    }
    return {};
}

juce::String simplePartText (toolbarlayout::simplebar::Part p)
{
    using P = toolbarlayout::simplebar::Part;
    switch (p)
    {
        case P::tempo: return tr ("Tempo");
        case P::key: return tr ("Project key");
        case P::master: return tr ("Master volume");
        case P::file: return tr (toolbarlayout::groupName (toolbarlayout::Group::file));
        case P::link: return tr ("Ableton Link");
        case P::status: return tr ("Save");
        case P::tuner: return tr ("Tuner");
        case P::transport:
        case P::more:
        case P::finish:
        case P::count_: break; // (never left out: priority 100)
    }
    return {};
}
} // namespace

juce::StringArray MainComponent::lookDroppedNames()
{
    juce::StringArray names;
    if (toolbarLook == Look::live)
    {
        for (int i = 0; i < lb::itemCount; ++i)
            if (liveWanted[(std::size_t) i] && ! liveShown[(std::size_t) i])
                names.add (liveItemText ((lb::Item) i));
    }
    else if (toolbarLook == Look::simple)
    {
        for (int i = 0; i < sb::partCount; ++i)
            if (simpleWanted[(std::size_t) i] && ! simpleShown[(std::size_t) i])
            {
                names.add (simplePartText ((sb::Part) i));
                if ((sb::Part) i == sb::Part::master)
                    names.add (tr ("Master pitch"));
            }
    }
    return names;
}

void MainComponent::addLookDroppedItems (juce::PopupMenu& menu)
{
    bool any = false;
    const auto header = [&] {
        if (! any)
            menu.addSectionHeader (tr (toolbarLook == Look::live ? "Control bar" : "Toolbar"));
        any = true;
    };
    const auto tick = [] (auto& c) { c.update(); };
    if (toolbarLook == Look::live)
    {
        using I = lb::Item;
        for (int index = 0; index < lb::itemCount; ++index)
        {
            const auto i = (I) index;
            if (! liveWanted[(std::size_t) index] || liveShown[(std::size_t) index])
                continue;
            header();
            const auto text = liveItemText (i);
            switch (i)
            {
                case I::tap: menu.addItem (text, [this] { tapTempoButton.triggerClick(); }); break;
                case I::overdub: menu.addItem (text, true, blendButton.getToggleState(), [this] { runAction ("record.blend"); }); break;
                case I::automationArm: menu.addItem (text, true, engine.getRecordOptions().recordAutomation, [this] { automationArmButton.triggerClick(); }); break;
                case I::reEnableAutomation: menu.addItem (text, true, engine.automationOverridden(), [this] { engine.reEnableAutomation(); }); break;
                case I::capture: menu.addItem (text, [this] { runAction ("record.captureMidi"); }); break;
                case I::punch: menu.addItem (text, true, punchButton.getToggleState(), [this] { runAction ("record.punchIn"); }); break;
                case I::loopSwitch: menu.addItem (text, true, liveLoopButton.getToggleState(), [this] { toggleLiveLoop(); }); break;
                case I::punchOut: menu.addItem (text, true, punchOutButton.getToggleState(), [this] { runAction ("record.punchOut"); }); break;
                case I::computerKeys: menu.addItem (text, true, typingPianoButton.getToggleState(), [this] { runAction ("view.typingPiano"); }); break;
                case I::draw: menu.addItem (text, true, drawmode::isOn(), [] { drawmode::toggle(); }); break;
                case I::keyMap: menu.addItem (text, true, getMapMode() == remotemap::Mode::key, [this] { keyMapButton.triggerClick(); }); break;
                case I::midiMap: menu.addItem (text, true, getMapMode() == remotemap::Mode::midi, [this] { midiMapButton.triggerClick(); }); break;
                case I::browser: menu.addItem (text, true, workspace.isPanelVisible (Panel::browser), [this] { runAction ("panel.browser"); }); break;
                // round 190qg: the rest of the bar (they dropped without a trace before)
                case I::link:
                {
                    auto toggle = std::make_unique<toolbar::LinkToggle> (engine);
                    toggle->onConfigure = linkToggle.onConfigure;
                    addWidget (menu, Icon::link, text, std::move (toggle), toolbar::LinkToggle::idealWidth (24), 24,
                               [tick] (juce::Component& c) { tick (static_cast<toolbar::LinkToggle&> (c)); });
                    break;
                }
                case I::nudgeDown:
                case I::nudgeUp:
                    addWidget (menu, Icon::tapTempo, text, std::make_unique<toolbar::PhaseNudge> (engine, i == I::nudgeUp), 20, 20);
                    break;
                case I::signature: addWidget (menu, Icon::metronome, text, std::make_unique<toolbar::SignatureBox> (engine), 48, 20, [] (juce::Component& c) { c.repaint(); }); break;
                case I::metronome: menu.addItem (text, true, metronomeButton.getToggleState(), [this] { metronomeButton.triggerClick(); }); break;
                case I::quantisation: addWidget (menu, Icon::magnet, text, std::make_unique<toolbar::QuantBox> (engine), 56, 20, [] (juce::Component& c) { c.repaint(); }); break;
                case I::key: menu.addItem (text, [this] { showKeyMenu(); }); break;
                case I::backToArrangement: menu.addItem (text, backToArrangementButton.isEnabled(), false, [this] { backToArrangementButton.triggerClick(); }); break;
                case I::sessionRecord: menu.addItem (text, true, sessionRecordButton.getToggleState(), [this] { sessionRecordButton.triggerClick(); }); break;
                case I::cpu:
                    menu.addItem (text, perfMeter.onOpenMonitor != nullptr, false, [this] {
                        if (perfMeter.onOpenMonitor != nullptr)
                            perfMeter.onOpenMonitor();
                    });
                    break;
                case I::tempo:
                case I::position:
                case I::play:
                case I::stop:
                case I::record:
                case I::more:
                case I::sessionView:
                case I::arrangementView:
                case I::count_: break;
            }
        }
    }
    else if (toolbarLook == Look::simple)
    {
        using P = sb::Part;
        for (int index = 0; index < sb::partCount; ++index)
        {
            const auto p = (P) index;
            if (! simpleWanted[(std::size_t) index] || simpleShown[(std::size_t) index])
                continue;
            if (p == P::file)
                continue; // (the File and undo submenu is always in "⋯", below)
            header();
            const auto text = simplePartText (p);
            switch (p)
            {
                case P::tempo:
                    addWidget (menu, Icon::metronome, text, std::make_unique<TempoDisplay> (engine), 96, 24, [] (juce::Component& c) { c.repaint(); });
                    menu.addItem (tr ("Metronome"), true, metronomeButton.getToggleState(), [this] { metronomeButton.triggerClick(); });
                    break;
                case P::key: menu.addItem (text, [this] { showKeyMenu(); }); break;
                case P::master:
                    for (const auto kind : { toolbar::MasterKnob::Kind::volume, toolbar::MasterKnob::Kind::pitch })
                        addWidget (menu, kind == toolbar::MasterKnob::Kind::volume ? Icon::mixer : Icon::tuningFork,
                                   kind == toolbar::MasterKnob::Kind::volume ? text : tr ("Master pitch"), std::make_unique<toolbar::MasterKnob> (engine, kind), 96, 28,
                                   [tick] (juce::Component& c) { tick (static_cast<toolbar::MasterKnob&> (c)); });
                    break;
                case P::link:
                {
                    auto toggle = std::make_unique<toolbar::LinkToggle> (engine);
                    toggle->onConfigure = linkToggle.onConfigure;
                    addWidget (menu, Icon::link, text, std::move (toggle), toolbar::LinkToggle::idealWidth (24), 24,
                               [tick] (juce::Component& c) { tick (static_cast<toolbar::LinkToggle&> (c)); });
                    break;
                }
                case P::status: menu.addItem (text + " (" + saveStatus.getText() + ")", [this] { save (false); }); break;
                case P::tuner:
                    menu.addItem (text, [this] {
                        if (simpleWorkflow != nullptr)
                            if (auto* b = dynamic_cast<juce::Button*> (simpleWorkflow->tunerComponent()))
                                b->triggerClick();
                    });
                    break;
                case P::transport:
                case P::file:
                case P::more:
                case P::finish:
                case P::count_: break;
            }
        }
    }
    if (any)
        menu.addSeparator();
}

// --- the check (--selftest, --toolbarshot190qg): every item on the toolbar or in its overflow, never lost ------------------

juce::String MainComponent::checkToolbarItemsForSnapshot()
{
    juce::StringArray problems;
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    const auto onBar = [&] (juce::Component* c) {
        return c != nullptr && c->isVisible() && c->getParentComponent() == this && ! c->getBounds().isEmpty() && bar.contains (c->getBounds());
    };
    if (toolbarLook == Look::fl)
    {
        std::vector<std::pair<Item, juce::Rectangle<int>>> placed;
        for (const auto& e : toolbarLayout.entries)
        {
            if (! e.shown || ! toolbarLayout.visible)
                continue;
            for (int index = 0; index < toolbarlayout::itemCount; ++index)
            {
                const auto item = (Item) index;
                if (toolbarlayout::groupOf (item) != e.group)
                    continue;
                const auto inOverflow = std::find (overflowList.begin(), overflowList.end(), item) != overflowList.end();
                auto* c = toolbarItemComponent (item);
                if (onBar (c))
                {
                    if (inOverflow)
                        problems.add (juce::String ("both on the bar and in the overflow: ") + toolbarlayout::itemId (item));
                    placed.push_back ({ item, c->getBounds() });
                }
                else if (! inOverflow)
                    problems.add (juce::String ("lost: ") + toolbarlayout::itemId (item) + " (" + (c != nullptr && c->isVisible() ? c->getBounds().toString() : juce::String ("hidden")) + ")");
            }
        }
        // the items on the bar do not cover each other
        for (std::size_t a = 0; a < placed.size(); ++a)
            for (std::size_t b = a + 1; b < placed.size(); ++b)
                if (placed[a].second.intersects (placed[b].second))
                    problems.add (juce::String ("overlap: ") + toolbarlayout::itemId (placed[a].first) + " " + placed[a].second.toString() + " / "
                                  + toolbarlayout::itemId (placed[b].first) + " " + placed[b].second.toString());
        // the menus are not cut: their bar is as wide as its titles
        {
            int needed = 0;
            for (const auto& name : getMenuBarNames())
                needed += getLookAndFeel().getMenuBarItemWidth (menuBar, 0, name);
            if (menuBar.getWidth() < needed)
                problems.add ("menu bar cut: " + juce::String (menuBar.getWidth()) + " of " + juce::String (needed) + " px");
        }
        // the menus and the hint bar are not covered either
        for (const auto& [item, r] : placed)
            if (r.intersects (menuBar.getBounds()) || (hintBar.isVisible() && r.intersects (hintBar.getBounds())))
                problems.add (juce::String ("covers the menus / hint bar: ") + toolbarlayout::itemId (item));
        // the overflow button: shown exactly when something is in it, on the bar, clear of everything else; its menu
        // names every item
        if (overflowList.empty() == onBar (&overflowButton))
            problems.add (overflowList.empty() ? "overflow button shown with nothing in it" : "overflow items but no overflow button on the bar");
        for (const auto& [item, r] : placed)
            if (overflowButton.isVisible() && r.intersects (overflowButton.getBounds()))
                problems.add (juce::String ("covers the overflow button: ") + toolbarlayout::itemId (item));
        if (! overflowList.empty())
        {
            juce::StringArray texts;
            collectTexts (makeToolbarOverflowMenu(), texts);
            for (auto item : overflowList)
            {
                const auto name = juce::String (item >= Item::playlist && item <= Item::browser ? toolbarlayout::itemName (item) : tr (toolbarlayout::itemName (item)));
                if (! listed (texts, name))
                    problems.add (juce::String ("not in the overflow menu: ") + toolbarlayout::itemId (item));
            }
        }
    }
    else
    {
        // the Simple / Clip-launch bars: every part they wanted and left out is in "⋯", and "⋯" is on the bar
        if (toolbarLayout.visible && ! onBar (&moreButton))
            problems.add ("the More button is not on the bar");
        const auto names = lookDroppedNames();
        if (! names.isEmpty())
        {
            juce::StringArray texts;
            collectTexts (makeToolbarMoreMenu(), texts);
            for (const auto& name : names)
                if (name.isEmpty() || ! listed (texts, name))
                    problems.add ("not in the More menu: " + name);
        }
    }
    return problems.joinIntoString ("\n");
}
} // namespace lynx
