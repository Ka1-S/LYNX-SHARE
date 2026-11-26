#include "ui/KeyFocus.h"

namespace lynx::ui::keyfocus
{
namespace
{
bool insidePanelWindow (const juce::Component* c)
{
    if (c == nullptr)
        return false;
    const auto* top = c->getTopLevelComponent();
    return top != nullptr && (bool) top->getProperties().getWithDefault ("lynxPanel", false);
}

// the text field that had the keyboard last (always watched, from the first install)
struct TextWatch final : juce::FocusChangeListener
{
    juce::Component::SafePointer<juce::TextEditor> current, left;
    double leftAt = 0.0;

    void globalFocusChanged (juce::Component* focused) override
    {
        auto* field = dynamic_cast<juce::TextEditor*> (focused);
        if (field == nullptr && focused != nullptr)
            field = focused->findParentComponentOfClass<juce::TextEditor>();
        if (field != nullptr)
        {
            current = field;
            left = nullptr;
            return;
        }
        if (current != nullptr && ! juce::Desktop::getInstance().getMainMouseSource().isDragging()
            && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        {
            left = current;
            leftAt = juce::Time::getMillisecondCounterHiRes();
        }
        else
            left = nullptr;
        current = nullptr;
    }
};

TextWatch& textWatch()
{
    static TextWatch w;
    return w;
}

struct Watch final : juce::FocusChangeListener
{
    juce::Component::SafePointer<juce::Component> root;
    bool pending = false;

    void globalFocusChanged (juce::Component* focused) override
    {
        if (root == nullptr || pending || ! shouldGiveBack (focused, *root))
            return;
        pending = true; // (async: the control may still be inside its own mouse or focus handling)
        juce::MessageManager::callAsync ([this, safeRoot = root] {
            pending = false;
            if (auto* r = safeRoot.getComponent())
                giveBackNow (*r);
        });
    }
};

Watch& watch()
{
    static Watch w;
    return w;
}
} // namespace

bool isMouseOnlyControl (const juce::Component* c)
{
    if (c == nullptr || dynamic_cast<const juce::TextEditor*> (c) != nullptr)
        return false;
    if (dynamic_cast<const juce::Button*> (c) != nullptr || dynamic_cast<const juce::ComboBox*> (c) != nullptr)
        return true;
    // a drop-down's own parts (its label): the label is a TextEditor only while its text is being edited
    for (auto* p = c->getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<const juce::ComboBox*> (p) != nullptr)
            return true;
    return false;
}

bool shouldGiveBack (const juce::Component* focused, const juce::Component& root)
{
    if (! isMouseOnlyControl (focused) || focused == &root)
        return false;
    const auto* box = dynamic_cast<const juce::ComboBox*> (focused);
    if (box == nullptr)
        box = focused->findParentComponentOfClass<juce::ComboBox>();
    if (box != nullptr && box->isTextEditable())
        return false; // (a drop-down one types into keeps the keys)
    return root.isParentOf (focused) || insidePanelWindow (focused);
}

void giveBackNow (juce::Component& root)
{
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (! shouldGiveBack (focused, root))
        return;
    if (root.isParentOf (focused))
    {
        if (root.isShowing())
            root.grabKeyboardFocus();
        else
            focused->giveAwayKeyboardFocus();
    }
    else
        focused->giveAwayKeyboardFocus(); // (a panel window: its keyPressed gets the keys again)
}

bool releaseOnEscape (juce::Component& root)
{
    if (! shouldGiveBack (juce::Component::getCurrentlyFocusedComponent(), root))
        return false;
    giveBackNow (root);
    return true;
}

bool typeIntoFieldJustLeft (const juce::KeyPress& key, int graceMs)
{
    auto& w = textWatch();
    auto* field = w.left.getComponent();
    if (field == nullptr || juce::Time::getMillisecondCounterHiRes() - w.leftAt > graceMs)
        return false;
    const auto mods = key.getModifiers();
    const auto c = key.getTextCharacter();
    if (mods.isCtrlDown() || mods.isAltDown() || mods.isCommandDown() || c < ' ' || c == 127)
        return false;
    if (! field->isShowing() || ! field->isEnabled() || field->isReadOnly() || dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr)
        return false;
    w.left = nullptr;
    field->grabKeyboardFocus();
    field->insertTextAtCaret (juce::String::charToString (c));
    return true;
}

void noteTextFieldLeftForTests (juce::TextEditor* field, double whenMs)
{
    textWatch().left = field;
    textWatch().leftAt = whenMs;
}

void install (juce::Component& root)
{
    watch().root = &root;
    juce::Desktop::getInstance().addFocusChangeListener (&watch());
    static const bool textWatched = [] {
        juce::Desktop::getInstance().addFocusChangeListener (&textWatch());
        return true;
    }();
    juce::ignoreUnused (textWatched);
}

void uninstall (juce::Component& root)
{
    if (watch().root.getComponent() != &root)
        return;
    juce::Desktop::getInstance().removeFocusChangeListener (&watch());
    watch().root = nullptr;
}
} // namespace lynx::ui::keyfocus
