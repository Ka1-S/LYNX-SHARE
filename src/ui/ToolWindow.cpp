#include "core/EscClose.h"
#include "ui/ToolWindow.h"

#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
class ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, const juce::String& k)
        : juce::DocumentWindow (title, theme::colour::bg2, juce::DocumentWindow::closeButton), key (k)
    {
        setUsingNativeTitleBar (true);
        all().add (this);
    }

    ~ToolWindow() override { all().removeFirstMatchingValue (this); }

    void closeButtonPressed() override { closeSoon(); }

    // #244 follow-up (hotfix 0.1.31.2, round 190ra; Linux COSMIC / Wayland): a window manager that activates the new window
    // only after openToolWindow returned (XWayland under a Wayland compositor) left the caret out of the search box: the
    // early requests found the window not focused yet, and on its activation JUCE gave the focus to the window itself
    // (nothing inside had had it), so typing went nowhere. When the window itself gets the focus, or becomes the active
    // window with nothing of it focused, the marked box takes it.
    void focusGained (FocusChangeType) override { focusMarkedSoon(); }
    void activeWindowStatusChanged() override
    {
        juce::DocumentWindow::activeWindowStatusChanged();
        if (! isActiveWindow())
            return;
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || focused == this || ! isParentOf (focused))
            focusMarkedSoon();
    }
    void focusMarkedSoon()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ToolWindow> (this)] {
            if (safe == nullptr || ! safe->isShowing())
                return;
            auto* focused = juce::Component::getCurrentlyFocusedComponent();
            if (focused != nullptr && focused != safe.getComponent() && safe->isParentOf (focused))
                return; // (something of the window has it: the user's choice)
            if (auto* target = focusOnOpenTarget (*safe); target != nullptr && target->isShowing())
                target->grabKeyboardFocus();
        });
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey && escclose::enabled()) // (issue #217: the switch keeps it open)
        {
            closeSoon();
            return true;
        }
        return juce::DocumentWindow::keyPressed (k);
    }

    void closeSoon()
    {
        // round 170h: hiding the active window let Windows activate another program (seen: the terminal), so after
        // File › New › Create, Esc or a note tool's Accept the shortcuts and Space did nothing until a click in LYNX.
        // The main window takes the activation first, as a closing plugin window does (round 165q).
        if (isActiveWindow())
            activateMainWindow();
        setVisible (false);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (this)] { delete safe.getComponent(); });
    }

    static juce::Array<ToolWindow*>& all()
    {
        static juce::Array<ToolWindow*> windows;
        return windows;
    }

    juce::String key;
};
constexpr const char* focusMark = "lynxFocusOnOpen";

// gives the marked component the focus unless it has it already (false: nothing marked or not showing)
bool focusMarked (juce::Component& root)
{
    auto* target = focusOnOpenTarget (root);
    if (target == nullptr || ! target->isShowing())
        return false;
    if (! target->hasKeyboardFocus (false))
        target->grabKeyboardFocus();
    return true;
}
} // namespace

void focusOnOpen (juce::Component& c)
{
    c.getProperties().set (focusMark, true);
    c.setWantsKeyboardFocus (true);
}

juce::Component* focusOnOpenTarget (juce::Component& root)
{
    if (root.getProperties().contains (focusMark))
        return &root;
    for (auto* child : root.getChildren())
        if (auto* found = focusOnOpenTarget (*child))
            return found;
    return nullptr;
}

juce::DocumentWindow* openToolWindow (std::unique_ptr<juce::Component> content, const juce::String& title, const juce::String& key, bool resizable)
{
    // (issue #179 audit: the old window goes away after this call, not during it. Its shortcut can arrive from inside the
    // old window itself (the Tempo tapper's key pressed in the Tempo tapper), and deleting it there freed the window
    // whose key handler was still running.)
    if (key.isNotEmpty())
        if (auto* old = dynamic_cast<ToolWindow*> (findToolWindow (key)))
            old->closeSoon();

    auto* window = new ToolWindow (title, key);
    window->setContentOwned (fitContentToScreen (std::move (content)).release(), true); // (scrolls when taller than the screen)
    window->setResizable (resizable, false);
    if (resizable)
        window->setResizeLimits (juce::jmax (320, window->getWidth() / 3), juce::jmax (200, window->getHeight() / 3), 8192, 8192);

    if (auto* main = juce::TopLevelWindow::getActiveTopLevelWindow())
        window->setCentrePosition (main->getScreenBounds().getCentre());
    else
        window->centreWithSize (window->getWidth(), window->getHeight());

    window->setVisible (true);
    keepAboveMainWindow (*window);
    window->toFront (true);
    // issue #244: a window with a search box opens with the caret in it (the "Find an effect" and "Manage shortcuts"
    // windows did not: the content was added before the window showed, so its own focus request found it hidden)
    if (focusMarked (*window))
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (window)] {
            if (safe != nullptr && safe->isShowing())
                focusMarked (*safe);
        });
    return window;
}

void closeToolWindowOf (juce::Component& content)
{
    if (auto* window = content.findParentComponentOfClass<ToolWindow>())
        window->closeSoon();
}

void closeAllToolWindows()
{
    while (! ToolWindow::all().isEmpty())
        delete ToolWindow::all().getLast();
}

juce::DocumentWindow* findToolWindow (const juce::String& key)
{
    for (auto* w : ToolWindow::all())
        if (w->key == key && w->isVisible())
            return w;
    return nullptr;
}
} // namespace lynx
