#include "ui/TouchInput.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx::ui::touchmode
{
bool& enabled()
{
    static bool on = false;
    return on;
}
bool& asked()
{
    static bool on = false;
    return on;
}
bool& simpleLayout()
{
    static bool on = false;
    return on;
}
bool& forceTouchForTests()
{
    static bool on = false;
    return on;
}

int touchScreenPoints()
{
#if JUCE_WINDOWS
    return GetSystemMetrics (95 /* SM_MAXIMUMTOUCHES, Windows 7+ */);
#else
    return 0; // (Linux / macOS: the first finger seen on the window suggests it)
#endif
}

double nowMs()
{
    return testClockMs() >= 0.0 ? testClockMs() : juce::Time::getMillisecondCounterHiRes();
}

juce::MouseEvent withButtons (const juce::MouseEvent& like, juce::Point<float> position, juce::ModifierKeys mods)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (like.source, position, mods, like.pressure, like.orientation, like.rotation, like.tiltX, like.tiltY,
                             like.eventComponent, like.originalComponent, now, position, now, 1, false);
}

bool Deferral::mouseDown (const juce::MouseEvent& e)
{
    if (inReplay || ! active (e) || e.mods.isPopupMenu() || e.mods.isMiddleButtonDown())
        return false;
    if (tracker.state() == touch::Tracker::State::idle)
    {
        tracker.setTwoFinger (hooks.twoFinger && simpleLayout() && hooks.pan != nullptr);
        held.reset();
        held.emplace (e);
    }
    const auto swallowed = tracker.down (finger (e), at (e), nowMs());
    run (tracker.take());
    if (tracker.waitingForLongPress() && testClockMs() < 0.0)
        startTimer (50);
    return swallowed;
}

bool Deferral::mouseDrag (const juce::MouseEvent& e)
{
    if (inReplay || ! active (e) || tracker.state() == touch::Tracker::State::idle)
        return false;
    const auto swallowed = tracker.move (finger (e), at (e), nowMs());
    run (tracker.take());
    if (! tracker.waitingForLongPress())
        stopTimer();
    return swallowed;
}

bool Deferral::mouseUp (const juce::MouseEvent& e)
{
    if (inReplay || ! active (e) || tracker.state() == touch::Tracker::State::idle)
        return false;
    const auto swallowed = tracker.up (finger (e), at (e), nowMs());
    run (tracker.take());
    if (! tracker.waitingForLongPress())
        stopTimer();
    if (tracker.state() == touch::Tracker::State::idle)
        held.reset();
    return swallowed;
}

void Deferral::timerCallback()
{
    tracker.tick (nowMs());
    if (! tracker.waitingForLongPress())
        stopTimer();
    run (tracker.take());
}

void Deferral::run (const std::vector<touch::Event>& events)
{
    auto* owner = hooks.owner;
    for (const auto& ev : events)
    {
        if (owner == nullptr)
            return;
        const juce::Component::SafePointer<juce::Component> safe (owner);
        switch (ev.action)
        {
            case touch::Action::press:
                if (held)
                {
                    const juce::ScopedValueSetter<bool> replay (inReplay, true);
                    owner->mouseDown (*held);
                }
                break;
            case touch::Action::longPress:
                if (held)
                {
                    const juce::ScopedValueSetter<bool> replay (inReplay, true);
                    const auto right = withButtons (*held, juce::Point<float> (ev.at.x, ev.at.y),
                                                    juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier));
                    if (hooks.longPress)
                        hooks.longPress (right);
                    else
                    {
                        owner->mouseDown (right);
                        if (safe != nullptr)
                            owner->mouseUp (right);
                    }
                }
                break;
            case touch::Action::pan:
                if (hooks.pan)
                    hooks.pan (ev.delta.x, ev.delta.y);
                break;
            case touch::Action::pinch:
                if (hooks.pinch)
                    hooks.pinch (ev.scale, { ev.at.x, ev.at.y });
                break;
            case touch::Action::gestureEnd: break;
        }
        if (safe == nullptr)
            return;
    }
}

namespace
{
// − value + at touch size (48 px buttons, BandLab / FL Studio Mobile size)
class ValuePopup final : public juce::Component
{
public:
    ValuePopup (std::function<float()> g, std::function<void (float)> s, std::function<juce::String (float)> t, float st)
        : get (std::move (g)), set (std::move (s)), text (std::move (t)), step (st)
    {
        for (auto* b : { &minus, &plus })
        {
            b->setColour (juce::TextButton::buttonColourId, theme::colour::bg4);
            b->setColour (juce::TextButton::textColourOffId, theme::colour::textPrimary);
            b->setRepeatSpeed (400, 80); // held: it keeps stepping
            addAndMakeVisible (*b);
        }
        minus.onClick = [this] { nudge (-1.0f); };
        plus.onClick = [this] { nudge (1.0f); };
        minus.setTitle (tr ("Less"));
        plus.setTitle (tr ("More"));
        setSize (220, 64);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg3);
        const auto v = get ? get() : 0.0f;
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawText (text ? text (v) : juce::String (juce::roundToInt (v * 100.0f)) + " %", getLocalBounds().reduced (60, 0),
                    juce::Justification::centred, false);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // the glyphs on the buttons: LYNX's own strokes
        g.setColour (theme::colour::textPrimary);
        for (auto* b : { &minus, &plus })
        {
            const auto c = b->getBounds().toFloat().getCentre();
            g.fillRect (juce::Rectangle<float> (16.0f, 2.0f).withCentre (c));
            if (b == &plus)
                g.fillRect (juce::Rectangle<float> (2.0f, 16.0f).withCentre (c));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        minus.setBounds (r.removeFromLeft (48));
        plus.setBounds (r.removeFromRight (48));
    }

private:
    void nudge (float direction)
    {
        if (! get || ! set)
            return;
        set (juce::jlimit (0.0f, 1.0f, get() + direction * step));
        repaint();
    }

    std::function<float()> get;
    std::function<void (float)> set;
    std::function<juce::String (float)> text;
    float step = 0.01f;
    juce::TextButton minus, plus;
};

class Suggestion final : public juce::Component
{
public:
    explicit Suggestion (std::function<void (bool)> a) : answered (std::move (a))
    {
        title.setText (tr ("Touch screen in use"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        body.setText (tr ("Touch mode makes the Simple layout's controls finger-sized: a long press opens the menu, two fingers scroll and "
                          "a pinch zooms. Also in View and Settings › LYNX extras."),
                      juce::dontSendNotification);
        body.setFont (theme::font (theme::type::body));
        body.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        body.setJustificationType (juce::Justification::topLeft);
        turnOn.setButtonText (tr ("Turn on Touch mode"));
        notNow.setButtonText (tr ("Not now"));
        turnOn.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        turnOn.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        notNow.setColour (juce::TextButton::buttonColourId, theme::colour::bg4);
        turnOn.onClick = [this] { answer (true); };
        notNow.onClick = [this] { answer (false); };
        for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &body, (juce::Component*) &turnOn, (juce::Component*) &notNow })
            addAndMakeVisible (c);
        setSize (380, 150);
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (r, theme::radius::lg);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r, theme::radius::lg, 1.0f);
        g.setColour (theme::colour::accent); // a hand's touch: LYNX's own mark, a ring with a dot
        const auto mark = juce::Rectangle<float> (18.0f, 18.0f).withPosition (16.0f, 15.0f);
        g.drawEllipse (mark, 1.5f);
        g.fillEllipse (mark.reduced (5.5f));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        title.setBounds (r.removeFromTop (24).withTrimmedLeft (26));
        auto buttons = r.removeFromBottom (44);
        notNow.setBounds (buttons.removeFromRight (100).reduced (0, 2));
        buttons.removeFromRight (8);
        turnOn.setBounds (buttons.removeFromRight (170).reduced (0, 2));
        body.setBounds (r.reduced (0, 2));
    }

private:
    void answer (bool on)
    {
        auto callback = answered; // (the answer may delete this card)
        if (callback)
            callback (on);
    }

    std::function<void (bool)> answered;
    juce::Label title, body;
    juce::TextButton turnOn, notNow;
};
} // namespace

void showValuePopup (juce::Component& knob, std::function<float()> get, std::function<void (float)> set,
                     std::function<juce::String (float)> text, float step)
{
    auto content = std::make_unique<ValuePopup> (std::move (get), std::move (set), std::move (text), step);
    auto* top = knob.getTopLevelComponent();
    juce::CallOutBox::launchAsynchronously (std::move (content), top != nullptr ? top->getLocalArea (&knob, knob.getLocalBounds()) : knob.getScreenBounds(),
                                            top);
}

std::unique_ptr<juce::Component> makeValuePopupForSnapshot (float value, std::function<juce::String (float)> text)
{
    return std::make_unique<ValuePopup> ([value] { return value; }, [] (float) {}, std::move (text), 0.01f);
}

std::unique_ptr<juce::Component> makeSuggestion (std::function<void (bool)> answered)
{
    return std::make_unique<Suggestion> (std::move (answered));
}
} // namespace lynx::ui::touchmode
