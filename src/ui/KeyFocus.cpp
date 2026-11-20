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

void install (juce::Component& root)
{
    watch().root = &root;
    juce::Desktop::getInstance().addFocusChangeListener (&watch());
}

void uninstall (juce::Component& root)
{
    if (watch().root.getComponent() != &root)
        return;
    juce::Desktop::getInstance().removeFocusChangeListener (&watch());
    watch().root = nullptr;
}
} // namespace lynx::ui::keyfocus
