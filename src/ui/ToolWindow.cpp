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

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
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
} // namespace

juce::DocumentWindow* openToolWindow (std::unique_ptr<juce::Component> content, const juce::String& title, const juce::String& key)
{
    if (key.isNotEmpty())
        if (auto* old = dynamic_cast<ToolWindow*> (findToolWindow (key)))
            delete old;

    auto* window = new ToolWindow (title, key);
    window->setContentOwned (fitContentToScreen (std::move (content)).release(), true); // (scrolls when taller than the screen)
    window->setResizable (false, false);

    if (auto* main = juce::TopLevelWindow::getActiveTopLevelWindow())
        window->setCentrePosition (main->getScreenBounds().getCentre());
    else
        window->centreWithSize (window->getWidth(), window->getHeight());

    window->setVisible (true);
    keepAboveMainWindow (*window);
    window->toFront (true);
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
