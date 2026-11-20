#include "ui/ActionSearch.h"

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

class ActionSearchOverlay::Panel final : public juce::Component, private juce::ListBoxModel
{
public:
    Panel (ActionSearchOverlay& o, const keys::KeyMap& map, std::function<bool (const std::string&)> r)
        : owner (o), entries (actionSearchEntries (map)), run (std::move (r))
    {
        field.setTextToShowWhenEmpty (tr ("Search every LYNX action: what it does, its panel or its key"), theme::colour::textDisabled);
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
        shown = actionsearch::find (entries, field.getText().toStdString());
        list.updateContent();
        list.selectRow (0);
        list.repaint();
        note.setText (tr ("{0} action(s)   Up / Down: pick   Enter: run   Esc: close", { juce::String ((int) shown.size()) }), juce::dontSendNotification);
    }

    void step (int delta)
    {
        if (shown.empty())
            return;
        const auto row = juce::jlimit (0, (int) shown.size() - 1, list.getSelectedRow() + delta);
        list.selectRow (row);
        list.scrollToEnsureRowIsOnscreen (row);
    }

    std::string selectedId() const
    {
        const auto row = list.getSelectedRow();
        return juce::isPositiveAndBelow (row, (int) shown.size()) ? entries[(std::size_t) shown[(std::size_t) row]].id : std::string();
    }

    void cannotRun()
    {
        note.setText (tr ("That action can't run here: open or click its panel first."), juce::dontSendNotification);
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

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, (int) shown.size()))
            return;
        const auto& e = entries[(std::size_t) shown[(std::size_t) row]];
        if (selected)
            g.fillAll (theme::colour::accentMuted);
        const auto keysWidth = juce::jmin (180, width / 4), whereWidth = juce::jmin (110, width / 6);
        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::textPrimary);
        g.drawText (juce::String::fromUTF8 (e.text.c_str()), 10, 0, width - keysWidth - whereWidth - 24, height, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textSecondary);
        g.drawText (juce::String::fromUTF8 (e.where.c_str()), width - keysWidth - whereWidth - 8, 0, whereWidth, height, juce::Justification::centredLeft, true);
        g.setColour (e.keys.empty() ? theme::colour::textDisabled : theme::colour::textPrimary);
        g.drawText (e.keys.empty() ? juce::String::fromUTF8 ("\xe2\x80\x94") : juce::String::fromUTF8 (e.keys.c_str()), width - keysWidth, 0, keysWidth - 8, height,
                    juce::Justification::centredRight, true);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { owner.runSelected(); }

    ActionSearchOverlay& owner;
    std::vector<actionsearch::Entry> entries;
    std::vector<int> shown;
    std::function<bool (const std::string&)> run;
    Field field;
    juce::ListBox list;
    juce::Label note;

    friend class ActionSearchOverlay;
};

ActionSearchOverlay::ActionSearchOverlay (const keys::KeyMap& map, std::function<bool (const std::string&)> run)
    : panel (std::make_unique<Panel> (*this, map, std::move (run)))
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
std::string ActionSearchOverlay::selectedId() const { return panel->selectedId(); }

bool ActionSearchOverlay::runSelected()
{
    const auto id = panel->selectedId();
    if (id.empty() || ! panel->run)
        return false;
    // (the box closes first, so the action finds the window as it was: its panel focused, nothing over it)
    auto run = panel->run;
    auto close = onClose;
    setVisible (false);
    if (! run (id))
    {
        setVisible (true);
        panel->cannotRun();
        panel->grabField();
        return false;
    }
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
    const auto w = juce::jmin (680, getWidth() - 40), h = juce::jmin (440, getHeight() - 80);
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
