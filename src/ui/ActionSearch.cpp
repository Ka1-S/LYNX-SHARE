#include "ui/ActionSearch.h"

#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
juce::String whereName (keys::Context c)
{
    switch (c)
    {
        case keys::Context::global: return tr ("Everywhere");
        case keys::Context::channelRack: return tr ("Channel Rack");
        case keys::Context::pianoRoll: return tr ("Piano Roll");
        case keys::Context::playlist: return tr ("Playlist");
        case keys::Context::mixer: return tr ("Mixer");
        case keys::Context::browser: return tr ("Browser");
    }
    return {};
}

juce::File recentFile() { return platform::settingsFile ("command-palette-recent.txt"); }

std::string utf8 (const juce::String& s) { return s.toStdString(); }
} // namespace

std::vector<actionsearch::Entry> actionSearchEntries (const keys::KeyMap& map)
{
    std::vector<actionsearch::Entry> out;
    for (const auto& a : keys::actions())
    {
        actionsearch::Entry e;
        e.id = a.id;
        e.text = tr (a.description).toStdString();
        e.english = a.description;
        e.where = whereName (a.context).toStdString();
        for (const auto& c : map.chordsFor (a.id))
            e.keys += (e.keys.empty() ? "" : "   ") + keys::displayString (c);
        out.push_back (std::move (e));
    }
    return out;
}

void addMenuItems (std::vector<PaletteItem>& out, const juce::PopupMenu& menu, const juce::String& path, const juce::String& skipSection)
{
    bool skipping = false;
    for (juce::PopupMenu::MenuItemIterator it (menu, false); it.next();)
    {
        const auto& item = it.getItem();
        if (item.isSectionHeader)
        {
            skipping = skipSection.isNotEmpty() && item.text == skipSection;
            continue;
        }
        if (item.isSeparator)
        {
            skipping = false;
            continue;
        }
        if (skipping)
            continue;
        auto text = item.text;
        auto keysText = item.shortcutKeyDescription;
        if (const auto tab = text.indexOfChar ('\t'); tab >= 0)
        {
            keysText = text.substring (tab + 1).trim();
            text = text.substring (0, tab);
        }
        text = text.trim();
        if (text.isEmpty())
            continue;
        if (item.subMenu != nullptr)
        {
            addMenuItems (out, *item.subMenu, path + " > " + text, skipSection);
            continue;
        }
        if (! item.action || item.customComponent != nullptr)
            continue;
        PaletteItem p;
        p.entry.kind = actionsearch::Kind::menu;
        p.entry.id = utf8 ("menu:" + path + " > " + text);
        p.entry.text = utf8 (text);
        p.entry.english = p.entry.text;
        p.entry.where = utf8 (path);
        p.entry.keys = utf8 (keysText);
        p.entry.enabled = item.isEnabled;
        if (item.isTicked)
            p.entry.state = utf8 (tr ("on"));
        p.run = [action = item.action, enabled = item.isEnabled] {
            if (! enabled)
                return false;
            action();
            return true;
        };
        out.push_back (std::move (p));
    }
}

std::vector<std::string>& ActionSearchOverlay::recentIds()
{
    static std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        juce::StringArray lines;
        lines.addLines (recentFile().loadFileAsString());
        for (const auto& l : lines)
            if (l.trim().isNotEmpty() && v.size() < 20)
                v.push_back (l.trim().toStdString());
        return v;
    }();
    return ids;
}

class ActionSearchOverlay::Panel final : public juce::Component, private juce::ListBoxModel
{
public:
    Panel (ActionSearchOverlay& o, std::vector<PaletteItem> all) : owner (o), items (std::move (all))
    {
        entries.reserve (items.size());
        for (const auto& i : items)
            entries.push_back (i.entry);
        field.setTextToShowWhenEmpty (tr ("Search commands, menus, LYNX extras, settings and recent projects"), theme::colour::textDisabled);
        field.setFont (theme::font (theme::type::body));
        field.onTextChange = [this] { refilter(); };
        field.onReturnKey = [this] { owner.runSelected(); };
        field.onEscapeKey = [this] { owner.onClose ? owner.onClose() : void(); };
        field.onArrow = [this] (int delta) { step (delta); };
        addAndMakeVisible (field);
        list.setModel (this);
        list.setRowHeight (26);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg2);
        list.setWantsKeyboardFocus (false);
        addAndMakeVisible (list);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (note);
        refilter();
    }

    void refilter()
    {
        shown = actionsearch::find (entries, field.getText().toStdString(), ActionSearchOverlay::recentIds());
        list.updateContent();
        list.selectRow (0);
        list.repaint();
        note.setText (tr ("{0} item(s)   Up / Down: pick   Enter: run   Esc: close", { juce::String ((int) shown.size()) }), juce::dontSendNotification);
    }

    void step (int delta)
    {
        if (shown.empty())
            return;
        const auto row = juce::jlimit (0, (int) shown.size() - 1, list.getSelectedRow() + delta);
        list.selectRow (row);
        list.scrollToEnsureRowIsOnscreen (row);
    }

    int selectedIndex() const
    {
        const auto row = list.getSelectedRow();
        return juce::isPositiveAndBelow (row, (int) shown.size()) ? shown[(std::size_t) row] : -1;
    }

    std::string selectedId() const
    {
        const auto i = selectedIndex();
        return i >= 0 ? entries[(std::size_t) i].id : std::string();
    }

    void cannotRun (bool disabled)
    {
        note.setText (disabled ? tr ("That item can't be used now (it is greyed in its menu too).")
                               : tr ("That action can't run here: open or click its panel first."),
                      juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radius::lg);
        g.setColour (theme::colour::outline);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::radius::lg, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        field.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        note.setBounds (r.removeFromBottom (20));
        r.removeFromBottom (4);
        list.setBounds (r);
    }

    void grabField() { field.grabKeyboardFocus(); }
    void setText (const juce::String& t)
    {
        field.setText (t, false);
        refilter(); // (a TextEditor tells its listeners later, by a message)
    }
    int count() const { return (int) shown.size(); }

    struct Field final : juce::TextEditor
    {
        std::function<void (int)> onArrow;
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (onArrow && (k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey)))
            {
                onArrow (k.isKeyCode (juce::KeyPress::upKey) ? -1 : 1);
                return true;
            }
            if (onArrow && (k.isKeyCode (juce::KeyPress::pageUpKey) || k.isKeyCode (juce::KeyPress::pageDownKey)))
            {
                onArrow (k.isKeyCode (juce::KeyPress::pageUpKey) ? -10 : 10);
                return true;
            }
            return juce::TextEditor::keyPressed (k);
        }
    };

private:
    int getNumRows() override { return (int) shown.size(); }

    static juce::String kindTag (const actionsearch::Entry& e)
    {
        switch (e.kind)
        {
            case actionsearch::Kind::action: return juce::String::fromUTF8 (e.where.c_str());
            case actionsearch::Kind::menu: return juce::String::fromUTF8 (e.where.c_str());
            case actionsearch::Kind::extra: return tr ("LYNX extras") + " > " + juce::String::fromUTF8 (e.where.c_str());
            case actionsearch::Kind::setting: return tr ("Settings");
            case actionsearch::Kind::recent: return tr ("Recent projects");
        }
        return {};
    }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;
        const auto& e = entries[(std::size_t) shown[(std::size_t) row]];
        if (selected)
            g.fillAll (theme::colour::accentMuted);
        const auto keysWidth = juce::jmin (160, width / 5), whereWidth = juce::jmin (200, width / 4);
        auto text = juce::String::fromUTF8 (e.text.c_str());
        g.setFont (theme::font (theme::type::body));
        g.setColour (e.enabled ? theme::colour::textPrimary : theme::colour::textDisabled);
        const auto textWidth = width - keysWidth - whereWidth - 24;
        if (! e.state.empty())
        {
            // a switch: its state in a small pill after the name
            const auto state = juce::String::fromUTF8 (e.state.c_str());
            const auto pill = juce::jmin (48, textWidth / 4);
            g.drawText (text, 10, 0, textWidth - pill - 6, height, juce::Justification::centredLeft, true);
            const juce::Rectangle<float> box ((float) (10 + textWidth - pill), (float) height * 0.5f - 8.0f, (float) pill - 4.0f, 16.0f);
            const auto on = state == tr ("on");
            g.setColour (on ? theme::colour::accent : theme::colour::bg5);
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (on ? juce::Colours::white : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (state, box, juce::Justification::centred, true);
        }
        else
            g.drawText (text, 10, 0, textWidth, height, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textSecondary);
        g.drawText (kindTag (e), width - keysWidth - whereWidth - 8, 0, whereWidth, height, juce::Justification::centredLeft, true);
        g.setColour (e.keys.empty() ? theme::colour::textDisabled : theme::colour::textPrimary);
        g.drawText (e.keys.empty() ? juce::String::fromUTF8 ("\xe2\x80\x94") : juce::String::fromUTF8 (e.keys.c_str()), width - keysWidth, 0, keysWidth - 8, height,
                    juce::Justification::centredRight, true);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.runSelected(); }

    ActionSearchOverlay& owner;
    std::vector<PaletteItem> items;
    std::vector<actionsearch::Entry> entries;
    std::vector<int> shown;
    Field field;
    juce::ListBox list;
    juce::Label note;

    friend class ActionSearchOverlay;
};

ActionSearchOverlay::ActionSearchOverlay (std::vector<PaletteItem> items)
    : panel (std::make_unique<Panel> (*this, std::move (items)))
{
    setWantsKeyboardFocus (false);
    addAndMakeVisible (*panel);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ActionSearchOverlay> (this)] {
        if (safe != nullptr && safe->isShowing())
            safe->panel->grabField();
    });
}

ActionSearchOverlay::~ActionSearchOverlay() = default;

void ActionSearchOverlay::setQuery (const juce::String& text) { panel->setText (text); }
int ActionSearchOverlay::matchCount() const { return panel->count(); }
int ActionSearchOverlay::itemCount() const { return (int) panel->items.size(); }
std::string ActionSearchOverlay::selectedId() const { return panel->selectedId(); }

bool ActionSearchOverlay::runSelected()
{
    const auto index = panel->selectedIndex();
    if (index < 0)
        return false;
    const auto& item = panel->items[(std::size_t) index];
    if (! item.run)
        return false;
    if (! item.entry.enabled)
    {
        panel->cannotRun (true);
        return false;
    }
    // (the box closes first, so the action finds the window as it was: its panel focused, nothing over it)
    auto run = item.run;
    auto close = onClose;
    const auto id = item.entry.id;
    setVisible (false);
    if (! run())
    {
        setVisible (true);
        panel->cannotRun (false);
        panel->grabField();
        return false;
    }
    actionsearch::noteRun (recentIds(), id);
    juce::StringArray lines;
    for (const auto& r : recentIds())
        lines.add (juce::String::fromUTF8 (r.c_str()));
    if (! platform::testMode())
        recentFile().replaceWithText (lines.joinIntoString ("\n"));
    if (close)
        close();
    return true;
}

void ActionSearchOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.28f));
}

void ActionSearchOverlay::resized()
{
    const auto w = juce::jmin (760, getWidth() - 40), h = juce::jmin (460, getHeight() - 80);
    panel->setBounds ((getWidth() - w) / 2, juce::jmin (90, getHeight() / 8), w, h);
}

void ActionSearchOverlay::mouseDown (const juce::MouseEvent&)
{
    if (onClose)
        onClose();
}

void ActionSearchOverlay::parentSizeChanged()
{
    if (auto* p = getParentComponent())
        setBounds (p->getLocalBounds());
}
} // namespace lynx
