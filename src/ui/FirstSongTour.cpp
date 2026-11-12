#include "ui/FirstSongTour.h"

#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr const char* layerId = "tour.layer";
constexpr const char* cardId = "tour.card";

// The first component with this ID that is on screen inside `root` (the tour's own parts left out)
juce::Component* findShowing (juce::Component& root, const juce::String& id)
{
    for (auto* c : root.getChildren())
    {
        if (c->getComponentID() == layerId || c->getComponentID() == cardId || ! c->isVisible())
            continue;
        if (c->getComponentID() == id && ! c->getLocalBounds().isEmpty()) // (its parents up to the window are visible: checked on the way down)
            return c;
        if (auto* inner = findShowing (*c, id))
            return inner;
    }
    return nullptr;
}

void styleButton (juce::TextButton& b, bool primary)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? theme::colour::accent : theme::colour::bg4);
    b.setColour (juce::TextButton::buttonOnColourId, primary ? theme::colour::accentPressed : theme::colour::bg6);
    b.setColour (juce::TextButton::textColourOffId, primary ? theme::colour::onAccent : theme::colour::textPrimary);
    b.setWantsKeyboardFocus (false);
}
} // namespace

// --- the dimmed window with a ring around the control to use -------------------------------------------------------------
class FirstSongTour::Layer final : public juce::Component
{
public:
    Layer()
    {
        setComponentID (layerId);
        setInterceptsMouseClicks (false, false); // (never in the way: every click reaches the window)
        setWantsKeyboardFocus (false);
        setAlwaysOnTop (true);
    }

    void setRing (juce::Rectangle<int> r)
    {
        if (r == ring)
            return;
        ring = r;
        repaint();
    }
    juce::Rectangle<int> getRing() const noexcept { return ring; }

    void paint (juce::Graphics& g) override
    {
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        if (! ring.isEmpty())
        {
            const auto hole = ring.toFloat().expanded (6.0f);
            dim.addRoundedRectangle (hole, theme::radius::lg + 2.0f);
            dim.setUsingNonZeroWinding (false);
        }
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillPath (dim);
        if (ring.isEmpty())
            return;
        const auto r = ring.toFloat().expanded (6.0f);
        for (int i = 3; i >= 1; --i) // a soft glow, then the ring
        {
            g.setColour (theme::colour::accent.withAlpha (0.10f * (float) (4 - i)));
            g.drawRoundedRectangle (r.expanded (2.0f * (float) i), theme::radius::lg + 2.0f + 2.0f * (float) i, 2.0f);
        }
        g.setColour (theme::colour::accent);
        g.drawRoundedRectangle (r, theme::radius::lg + 2.0f, 2.5f);
    }

private:
    juce::Rectangle<int> ring;
};

// --- the step card ---------------------------------------------------------------------------------------------------------
class FirstSongTour::Card final : public juce::Component,
                                  private juce::Timer
{
public:
    static constexpr int width = 380, height = 184;

    Card (FirstSongTour& t) : owner (t)
    {
        setComponentID (cardId);
        setAlwaysOnTop (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        close.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        close.setTooltip (tr ("Close: continue the tour later from Help"));
        skipButton.setButtonText (tr ("Skip tour"));
        skipButton.setTooltip (tr ("End the tour. Help › First-song tour shows it again."));
        backButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9 ") + tr ("Back"));
        nextButton.setButtonText (tr ("Next") + juce::String::fromUTF8 (" \xe2\x80\xba"));
        for (auto* b : { &close, &skipButton, &backButton, &doButton, &nextButton, &saveButton, &demoButton })
        {
            styleButton (*b, b == &nextButton || b == &saveButton);
            addChildComponent (*b);
        }
        close.setVisible (true);
        close.onClick = [this] { owner.clicked ("close"); };
        skipButton.onClick = [this] { owner.clicked ("skip"); };
        backButton.onClick = [this] { owner.clicked ("back"); };
        doButton.onClick = [this] { owner.clicked ("do"); };
        nextButton.onClick = [this] { owner.clicked ("next"); };
        saveButton.onClick = [this] { owner.clicked ("save"); };
        demoButton.onClick = [this] { owner.clicked ("demo"); };
    }

    void show (int stepIndex, bool done)
    {
        step = stepIndex;
        stepDone = done;
        const auto end = step >= tour::stepCount;
        skipButton.setVisible (! end);
        backButton.setVisible (true); // (the end card too: back to the last step)
        backButton.setEnabled (step > 0);
        nextButton.setVisible (! end);
        doButton.setVisible (! end && ! done && (tour::canDoForUser (step) || step == tour::loop));
        doButton.setButtonText (step == tour::loop ? tr ("Show the loops") : tr ("Do it for me"));
        doButton.setTooltip (step == tour::loop ? tr ("Opens the loops: drag one onto the timeline")
                                                : tr ("The tour does this step for you, as the control would"));
        saveButton.setVisible (end);
        saveButton.setButtonText (tr ("Save as a file..."));
        demoButton.setVisible (end);
        demoButton.setButtonText (tr ("Open a demo song"));
        close.setTooltip (end ? tr ("Close") : tr ("Close: continue the tour later from Help"));
        resized();
        repaint();
    }

    void startFollowing() { startTimer (250); }
    void stopFollowing() { stopTimer(); }
    std::function<void()> follow; // where the ring and the card go (the window's layout may change at any time)

    juce::TextButton* button (const juce::String& name)
    {
        if (name == "next") return &nextButton;
        if (name == "back") return &backButton;
        if (name == "do") return &doButton;
        if (name == "skip") return &skipButton;
        if (name == "close") return &close;
        if (name == "save") return &saveButton;
        if (name == "demo") return &demoButton;
        return nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0.0f, 3.0f), theme::radius::lg + 1.0f);
        g.setColour (theme::colour::bg2);
        g.fillRoundedRectangle (r, theme::radius::lg);
        g.setColour (theme::colour::accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::lg, 1.2f);

        auto area = getLocalBounds().reduced (18, 14);
        auto top = area.removeFromTop (18);
        top.removeFromRight (26);
        const auto end = step >= tour::stepCount;
        g.setFont (theme::font (theme::type::small, true));
        g.setColour (theme::colour::accent);
        g.drawText (end ? tr ("First song") : tr ("Step {0} of {1}", { juce::String (step + 1), juce::String (tour::stepCount) }), top,
                    juce::Justification::centredLeft, true);
        // progress: one segment per step
        auto bar = area.removeFromTop (10).withTrimmedTop (5).toFloat();
        const auto seg = (bar.getWidth() - 4.0f * (tour::stepCount - 1)) / (float) tour::stepCount;
        for (int i = 0; i < tour::stepCount; ++i)
        {
            const auto s = juce::Rectangle<float> (bar.getX() + (float) i * (seg + 4.0f), bar.getY(), seg, 3.0f);
            g.setColour (i < step || end ? theme::colour::accent : i == step ? theme::colour::accent.withAlpha (0.55f) : theme::colour::bg5);
            g.fillRoundedRectangle (s, 1.5f);
        }
        area.removeFromTop (8);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        auto titleRow = area.removeFromTop (24);
        g.drawFittedText (tr (tour::title (step)), titleRow, juce::Justification::centredLeft, 1, 0.85f);
        if (stepDone && ! end)
        {
            g.setColour (theme::colour::ledOn);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93 ") + tr ("Step done"), titleRow, juce::Justification::centredRight, false);
        }
        area.removeFromTop (4);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body));
        g.drawFittedText (tr (tour::text (step)), area.removeFromTop (58), juce::Justification::topLeft, 3, 0.9f);
    }

    void resized() override
    {
        close.setBounds (getWidth() - 34, 10, 24, 24);
        auto row = getLocalBounds().reduced (16, 14).removeFromBottom (30);
        if (step >= tour::stepCount)
        {
            backButton.setBounds (row.removeFromLeft (66));
            saveButton.setBounds (row.removeFromRight (130));
            row.removeFromRight (6);
            demoButton.setBounds (row.removeFromRight (140));
            return;
        }
        skipButton.setBounds (row.removeFromLeft (78));
        nextButton.setBounds (row.removeFromRight (74));
        row.removeFromRight (6);
        if (doButton.isVisible())
        {
            doButton.setBounds (row.removeFromRight (112));
            row.removeFromRight (6);
        }
        backButton.setBounds (row.removeFromRight (66));
    }

private:
    void timerCallback() override
    {
        if (follow)
            follow();
    }

    FirstSongTour& owner;
    int step = 0;
    bool stepDone = false;
    juce::TextButton close, skipButton, backButton, doButton, nextButton, saveButton, demoButton;
};

// --- the Welcome screen ------------------------------------------------------------------------------------------------------
class FirstSongTour::Welcome final : public juce::Component
{
public:
    struct Option final : public juce::Component
    {
        Option (juce::String n, juce::String h, bool p) : name (std::move (n)), help (std::move (h)), primary (p)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
            setTitle (name);
            setDescription (help);
        }
        std::function<void()> onClick;
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const auto hover = isMouseOver (true);
            g.setColour (primary ? (hover ? theme::colour::accentHover : theme::colour::accent) : hover ? theme::colour::bg4 : theme::colour::bg3);
            g.fillRoundedRectangle (r, theme::radius::lg);
            g.setColour (primary ? theme::colour::accent : hasKeyboardFocus (false) ? theme::colour::accent : hover ? theme::colour::borderStrong : theme::colour::border);
            g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::lg, 1.0f);
            auto text = getLocalBounds().reduced (18, 10);
            g.setColour (primary ? theme::colour::onAccent : theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::title, true));
            g.drawFittedText (name, text.removeFromTop (22), juce::Justification::centredLeft, 1, 0.85f);
            g.setColour (primary ? theme::colour::onAccent.withAlpha (0.85f) : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (help, text, juce::Justification::topLeft, 2, 0.9f);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
                onClick();
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if ((k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) && onClick)
            {
                onClick();
                return true;
            }
            return false;
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        juce::String name, help;
        bool primary;
    };

    Welcome (FirstSongTour& t)
        : owner (t),
          tourOption (tr ("Make my first song"), tr ("A short guided tour, about five minutes: a beat, chords, a voice or a loop, an effect, done."), true),
          demoOption (tr ("Open a demo song"), tr ("Three short songs made with LYNX's own instruments: open one and take it apart."), false),
          emptyOption (tr ("Start empty"), tr ("Go straight to the tracks."), false)
    {
        setComponentID ("tour.welcome");
        setWantsKeyboardFocus (true);
        title.setText (tr ("Welcome to LYNX"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::display, true));
        title.setJustificationType (juce::Justification::centred);
        subtitle.setText (tr ("What would you like to do?"), juce::dontSendNotification);
        subtitle.setFont (theme::font (theme::type::body));
        subtitle.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        subtitle.setJustificationType (juce::Justification::centred);
        footer.setText (tr ("You can open this screen again from Help."), juce::dontSendNotification);
        footer.setFont (theme::font (theme::type::small));
        footer.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        footer.setJustificationType (juce::Justification::centred);
        close.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        close.setTooltip (tr ("Close"));
        styleButton (close, false);
        tourOption.setComponentID ("tour.welcome.start");
        tourOption.onClick = [this] { owner.clicked ("welcome.tour"); };
        demoOption.onClick = [this] { owner.clicked ("welcome.demo"); };
        emptyOption.onClick = [this] { owner.clicked ("welcome.empty"); };
        close.onClick = [this] { owner.clicked ("welcome.empty"); };
        for (auto* c : std::initializer_list<juce::Component*> { &title, &subtitle, &footer, &close, &tourOption, &demoOption, &emptyOption })
            addAndMakeVisible (*c);
    }

    juce::Rectangle<int> card() const
    {
        const auto w = juce::jmin (580, getWidth() - 24), h = juce::jmin (420, getHeight() - 24);
        return getLocalBounds().withSizeKeepingCentre (juce::jmax (320, w), juce::jmax (300, h));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0.withAlpha (0.72f));
        const auto c = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (c.translated (0.0f, 4.0f).expanded (2.0f), theme::radius::lg + 2.0f);
        g.setColour (theme::colour::bg2);
        g.fillRoundedRectangle (c, theme::radius::lg);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (c.reduced (0.5f), theme::radius::lg, 1.0f);
        g.setColour (theme::colour::accent);
        g.fillRect (c.getX() + 24.0f, c.getY() + 52.0f, c.getWidth() - 48.0f, 1.0f);
    }

    void resized() override
    {
        auto c = card().reduced (24, 14);
        auto top = c.removeFromTop (34);
        close.setBounds (top.removeFromRight (28).withSizeKeepingCentre (28, 28));
        top.removeFromLeft (28);
        title.setBounds (top);
        c.removeFromTop (12);
        subtitle.setBounds (c.removeFromTop (22));
        c.removeFromTop (10);
        footer.setBounds (c.removeFromBottom (20));
        c.removeFromBottom (8);
        const auto gap = 10, h = juce::jlimit (56, 84, (c.getHeight() - 2 * gap) / 3);
        tourOption.setBounds (c.removeFromTop (h));
        c.removeFromTop (gap);
        demoOption.setBounds (c.removeFromTop (h));
        c.removeFromTop (gap);
        emptyOption.setBounds (c.removeFromTop (h));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey)
        {
            owner.clicked ("welcome.empty");
            return true;
        }
        return false;
    }

private:
    FirstSongTour& owner;
    juce::Label title, subtitle, footer;
    juce::TextButton close;
    Option tourOption, demoOption, emptyOption;
};

// --- the tour ----------------------------------------------------------------------------------------------------------------
FirstSongTour::FirstSongTour (AudioEngine& e, Host h, bool p) : engine (e), host (std::move (h)), persistent (p)
{
    if (persistent)
        if (const auto f = stateFile(); f.existsAsFile())
            machine = tour::Machine (tour::parse (f.loadFileAsString().toStdString()));
}

FirstSongTour::~FirstSongTour()
{
    hideCard();
    hideWelcome();
}

juce::File FirstSongTour::stateFile() { return platform::settingsFile ("simple-tour.txt"); }

void FirstSongTour::save()
{
    if (persistent)
        stateFile().replaceWithText (tour::serialize (machine.state()));
}

bool FirstSongTour::shouldOfferWelcome() const { return ! machine.state().welcomeSeen && ! machine.state().done && ! machine.canContinue(); }

void FirstSongTour::showWelcome()
{
    if (welcome == nullptr)
        welcome = std::make_unique<Welcome> (*this);
    machine.welcomeShown();
    save();
    if (host.setOverlay)
        host.setOverlay (welcome.get());
    if (welcome->isShowing())
        welcome->grabKeyboardFocus();
}

void FirstSongTour::hideWelcome()
{
    if (isWelcomeShowing() && host.setOverlay)
        host.setOverlay (nullptr);
}

bool FirstSongTour::isWelcomeShowing() const noexcept { return welcome != nullptr && welcome->getParentComponent() != nullptr; }

juce::Component* FirstSongTour::welcomeForSnapshot()
{
    if (welcome == nullptr)
        welcome = std::make_unique<Welcome> (*this);
    return welcome.get();
}

void FirstSongTour::start()
{
    hideWelcome();
    machine.start (engine.getSong());
    save();
    if (host.tourStarting && machine.step() == tour::beat)
        host.tourStarting();
    showCard();
}

void FirstSongTour::startOrContinue()
{
    if (machine.canContinue())
    {
        hideWelcome();
        machine.resume (engine.getSong());
        save();
        if (host.tourStarting && machine.step() == tour::beat)
            host.tourStarting();
        showCard();
        return;
    }
    // a new tour on a project with work in it: a new project first (asked), or this one
    if (host.songIsEmpty && ! host.songIsEmpty() && host.newProjectFirst)
    {
        host.newProjectFirst ([this] { start(); });
        return;
    }
    start();
}

void FirstSongTour::pause()
{
    machine.pause();
    save();
    hideCard();
}

void FirstSongTour::skip()
{
    machine.skip();
    save();
    hideCard();
}

bool FirstSongTour::isRunning() const noexcept { return machine.running(); }
bool FirstSongTour::canContinue() const noexcept { return machine.canContinue(); }
int FirstSongTour::step() const noexcept { return machine.step(); }

void FirstSongTour::songChanged()
{
    if (! machine.running())
        return;
    if (machine.observe (engine.getSong()))
        save();
    update();
}

void FirstSongTour::showCard()
{
    auto* window = host.window ? host.window() : nullptr;
    if (window == nullptr)
        return;
    if (card == nullptr)
    {
        card = std::make_unique<Card> (*this);
        layer = std::make_unique<Layer>();
        card->follow = [this] { update(); };
    }
    if (layer->getParentComponent() != window)
        window->addAndMakeVisible (*layer);
    if (card->getParentComponent() != window)
        window->addAndMakeVisible (*card);
    layer->setBounds (window->getLocalBounds());
    layer->toFront (false);
    card->toFront (false);
    card->startFollowing();
    update();
}

void FirstSongTour::hideCard()
{
    if (card != nullptr)
    {
        card->stopFollowing();
        if (auto* p = card->getParentComponent())
            p->removeChildComponent (card.get());
    }
    if (layer != nullptr)
        if (auto* p = layer->getParentComponent())
            p->removeChildComponent (layer.get());
}

void FirstSongTour::update()
{
    if (card == nullptr || card->getParentComponent() == nullptr)
        return;
    auto* window = card->getParentComponent();
    const auto step = machine.step();
    card->show (step, machine.shownDone (engine.getSong()));

    // the ring: the first target on screen
    juce::Rectangle<int> ring;
    for (const auto& id : tour::targets (step))
        if (auto* c = findShowing (*window, id))
        {
            ring = window->getLocalArea (c, c->getLocalBounds());
            break;
        }
    layer->setBounds (window->getLocalBounds());
    layer->setRing (ring);

    // the card: in a corner the ring is not in (top right first, under the toolbar)
    const auto area = window->getLocalBounds().reduced (16);
    const auto w = Card::width, h = Card::height, top = area.getY() + 96;
    const juce::Rectangle<int> spots[] { { area.getRight() - w, top, w, h },
                                         { area.getRight() - w, area.getBottom() - h, w, h },
                                         { area.getX(), area.getBottom() - h, w, h },
                                         { area.getX(), top, w, h },
                                         { area.getCentreX() - w / 2, area.getBottom() - h, w, h } };
    // (and not over the Finish dock while it is open: the end card would hide its looks)
    juce::Rectangle<int> keepClear;
    if (auto* finishDock = findShowing (*window, "simple.finishDock"))
        keepClear = window->getLocalArea (finishDock, finishDock->getLocalBounds());
    auto place = spots[0];
    for (const auto& s : spots)
        if ((ring.isEmpty() || ! s.intersects (ring.expanded (24))) && ! s.intersects (keepClear))
        {
            place = s;
            break;
        }
    if (card->getBounds() != place)
        card->setBounds (place);
}

void FirstSongTour::clicked (const juce::String& what)
{
    const auto& song = engine.getSong();
    if (what == "welcome.tour")
    {
        start();
        return;
    }
    if (what == "welcome.demo")
    {
        hideWelcome();
        if (host.openDemos)
            host.openDemos();
        return;
    }
    if (what == "welcome.empty")
    {
        hideWelcome();
        if (host.startEmpty)
            host.startEmpty();
        return;
    }
    if (what == "close")
    {
        if (machine.atEnd())
            skip();
        else
            pause();
        return;
    }
    if (what == "skip")
    {
        skip();
        return;
    }
    if (what == "back")
        machine.back();
    else if (what == "next")
        machine.next (song);
    else if (what == "do")
    {
        const auto step = machine.step();
        if (step == tour::loop)
        {
            if (host.showLoops)
                host.showLoops();
        }
        else if (host.doStep && tour::canDoForUser (step))
            host.doStep (step); // (the song changes: songChanged() moves the tour on)
        if (! machine.running())
            return;
        machine.observe (engine.getSong()); // (in case the edit didn't come back through the change callback yet)
    }
    else if (what == "save")
    {
        if (host.saveSong)
            host.saveSong();
        return;
    }
    else if (what == "demo")
    {
        skip();
        if (host.openDemos)
            host.openDemos();
        return;
    }
    save();
    update();
}

juce::Component* FirstSongTour::cardForSnapshot() { return card.get(); }
juce::Rectangle<int> FirstSongTour::ringForSnapshot()
{
    update();
    return layer != nullptr ? layer->getRing() : juce::Rectangle<int>();
}

juce::String FirstSongTour::targetIdForSnapshot() const
{
    if (card == nullptr || card->getParentComponent() == nullptr)
        return {};
    for (const auto& id : tour::targets (machine.step()))
        if (findShowing (*card->getParentComponent(), id) != nullptr)
            return id;
    return {};
}

void FirstSongTour::clickForSnapshot (const juce::String& button)
{
    if (button.startsWith ("welcome."))
    {
        clicked (button);
        return;
    }
    if (card != nullptr)
        if (auto* b = card->button (button); b != nullptr && b->isVisible() && b->isEnabled())
            clicked (button);
}
} // namespace lynx
