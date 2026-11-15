#include "ui/SupportLynx.h"

#include <cmath>
#include <tuple>

#include "core/Links.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx::support
{
namespace
{
juce::File stateFile()
{
    return PluginHost::settingsFolder().getChildFile ("support-note.txt");
}

// The Support window's text: who makes LYNX SHARE, what donations pay for, and that nothing is ever locked
class SupportText final : public juce::Component
{
public:
    static constexpr int textWidth = 460;

    SupportText()
    {
        paragraphs = { tr ("LYNX SHARE is free and open source, made by an independent developer."),
                       tr ("Donations keep the project going. They pay for the server (VPS) behind sharing, the relay and the tests; the AI tools "
                           "and tokens used in development; the website; the Apple developer account for signed Mac builds; and the reference "
                           "plugins used to check the sound quality."),
                       tr ("Donating is optional and never unlocks anything. Everything in LYNX SHARE stays free for everyone.") };
        int h = 6;
        for (int i = 0; i < paragraphs.size(); ++i)
            h += (int) std::ceil (layout (i).getHeight()) + gap;
        setSize (textWidth, h);
    }

    void paint (juce::Graphics& g) override
    {
        // a thin accent rule on the left, as LYNX's cards
        g.setColour (theme::colour::accent);
        g.fillRoundedRectangle (0.0f, 4.0f, 3.0f, (float) getHeight() - 12.0f, 1.5f);
        float y = 4.0f;
        for (int i = 0; i < paragraphs.size(); ++i)
        {
            const auto l = layout (i);
            l.draw (g, { indent, y, (float) getWidth() - indent, l.getHeight() });
            y += l.getHeight() + (float) gap;
        }
    }

private:
    static constexpr int gap = 10;
    static constexpr float indent = 14.0f;

    juce::TextLayout layout (int i) const
    {
        juce::AttributedString s;
        const auto last = i == paragraphs.size() - 1;
        // (the middle paragraph, the list of costs, a step quieter)
        s.append (paragraphs[i], theme::font (theme::type::body + 0.5f, last), i == 1 ? theme::colour::textSecondary : theme::colour::textPrimary);
        s.setWordWrap (juce::AttributedString::byWord);
        s.setLineSpacing (2.0f);
        juce::TextLayout l;
        l.createLayout (s, (float) textWidth - indent);
        return l;
    }

    juce::StringArray paragraphs;
};

class SupportWindow final : public juce::AlertWindow
{
public:
    SupportWindow() : juce::AlertWindow (tr ("Support LYNX SHARE"), {}, juce::MessageBoxIconType::NoIcon)
    {
        setComponentID ("support.window");
        addCustomComponent (&text);
        addButton (tr ("Donate on Ko-fi"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Close"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        if (auto* donate = getButton (0))
        {
            donate->setColour (juce::TextButton::buttonColourId, theme::colour::accent);
            donate->setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
            donate->setComponentID ("support.donate");
            donate->setTooltip (juce::String (links::donate));
        }
    }

private:
    SupportText text;
};

void styleButton (juce::TextButton& b, bool primary)
{
    b.setColour (juce::TextButton::buttonColourId, primary ? theme::colour::accent : theme::colour::bg4);
    b.setColour (juce::TextButton::textColourOffId, primary ? theme::colour::onAccent : theme::colour::textPrimary);
    b.setWantsKeyboardFocus (false);
    b.setMouseClickGrabsKeyboardFocus (false);
}
} // namespace

void openDonatePage()
{
    juce::URL (links::donate).launchInDefaultBrowser();
}

std::unique_ptr<juce::AlertWindow> makeSupportWindow()
{
    return std::make_unique<SupportWindow>();
}

void showSupportWindow()
{
    auto* window = makeSupportWindow().release();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([] (int result) {
                                 if (result == 1)
                                     openDonatePage();
                             }),
                             true);
    keepAboveMainWindow (*window);
}

supportnote::State loadState()
{
    return supportnote::fromText (stateFile().loadFileAsString().toStdString());
}

void saveState (const supportnote::State& state)
{
    stateFile().replaceWithText (juce::String (supportnote::toText (state)));
}

bool noteEnabled()
{
    return loadState().show;
}

void setNoteEnabled (bool on)
{
    auto s = loadState();
    if (s.show == on)
        return;
    supportnote::setShow (s, on);
    saveState (s);
}

// --- the card ----------------------------------------------------------------------------------------------------------
NoteCard::NoteCard (std::function<void (supportnote::Choice)> onChoice) : choice (std::move (onChoice))
{
    setComponentID ("support.note");
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    supportButton.setButtonText (tr ("Support"));
    supportButton.setTooltip (tr ("Opens the Support window: why donations help, and the Ko-fi page"));
    laterButton.setButtonText (tr ("Not now"));
    laterButton.setTooltip (tr ("Shows this note again in a while"));
    neverButton.setButtonText (tr ("Don't show again"));
    neverButton.setTooltip (tr ("Settings › General › Show the support note turns it back on"));
    for (auto [b, c, id] : { std::tuple { &supportButton, supportnote::Choice::support, "support.note.support" },
                             std::tuple { &laterButton, supportnote::Choice::notNow, "support.note.later" },
                             std::tuple { &neverButton, supportnote::Choice::never, "support.note.never" } })
    {
        styleButton (*b, b == &supportButton);
        b->setComponentID (id);
        b->onClick = [this, c = c] {
            if (choice != nullptr)
                choice (c);
        };
        addAndMakeVisible (*b);
    }
    setSize (width, height);
}

juce::TextButton* NoteCard::buttonForSnapshot (supportnote::Choice c)
{
    return c == supportnote::Choice::support ? &supportButton : c == supportnote::Choice::notNow ? &laterButton : &neverButton;
}

void NoteCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f), theme::radius::lg + 1.0f);
    g.setColour (theme::colour::bg2);
    g.fillRoundedRectangle (r, theme::radius::lg);
    g.setColour (theme::colour::borderStrong);
    g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::lg, 1.0f);
    // the accent rule on the left
    g.setColour (theme::colour::accent);
    g.fillRoundedRectangle (r.getX() + 8.0f, r.getY() + 12.0f, 3.0f, 46.0f, 1.5f);

    auto area = getLocalBounds().reduced (22, 12).withTrimmedLeft (2);
    g.setColour (theme::colour::textPrimary);
    g.setFont (theme::font (theme::type::body));
    g.drawFittedText (tr ("LYNX SHARE is free and open source. If it helps your music, you can support it on Ko-fi."),
                      area.removeFromTop (50), juce::Justification::centredLeft, 3, 0.9f);
}

void NoteCard::resized()
{
    auto row = getLocalBounds().reduced (14, 12).removeFromBottom (28);
    row.removeFromLeft (8);
    const auto w = [] (juce::TextButton& b) { return b.getBestWidthForHeight (28) + 18; };
    supportButton.setBounds (row.removeFromLeft (juce::jmax (84, w (supportButton))));
    neverButton.setBounds (row.removeFromRight (juce::jmax (120, w (neverButton))));
    row.removeFromRight (6);
    laterButton.setBounds (row.removeFromRight (juce::jmax (84, w (laterButton))));
}

// --- the start-up note -------------------------------------------------------------------------------------------------
StartupNote::StartupNote (juce::Component& p, std::function<supportnote::Moment()> m, std::function<int()> inset)
    : parent (p), moment (std::move (m)), bottomInset (std::move (inset))
{
    parent.addComponentListener (this);
}

StartupNote::~StartupNote()
{
    stopTimer();
    parent.removeComponentListener (this);
    closeCard();
}

void StartupNote::begin (bool quietRun)
{
    auto s = loadState();
    const auto due = supportnote::countStart (s, quietRun);
    if (quietRun)
        return; // (nothing counted, nothing written)
    saveState (s);
    if (! due)
        return;
    // the start settles first (the project, the plugin scan's first moments); then the first quiet moment
    waiting = true;
    waitStarted = juce::Time::getMillisecondCounterHiRes();
    startTimer (4000);
}

void StartupNote::timerCallback()
{
    const auto m = moment != nullptr ? moment() : supportnote::Moment {};
    if (card != nullptr)
    {
        if (! supportnote::canShowNow (m)) // playback, recording or a dialog started: the card steps aside
            closeCard();
        return;
    }
    if (! waiting)
    {
        stopTimer();
        return;
    }
    if (supportnote::canShowNow (m))
    {
        waiting = false;
        showCard();
        auto s = loadState();
        supportnote::noteShown (s); // (left without an answer, it comes back as after "Not now")
        saveState (s);
        startTimer (500); // (watches for playback / dialogs while it shows)
        return;
    }
    if (juce::Time::getMillisecondCounterHiRes() - waitStarted > 120000.0) // no quiet moment: next start
    {
        waiting = false;
        stopTimer();
        return;
    }
    startTimer (1000);
}

void StartupNote::showCard()
{
    card = std::make_unique<NoteCard> ([this] (supportnote::Choice c) { chosen (c); });
    parent.addAndMakeVisible (*card);
    card->toFront (false);
    place();
}

void StartupNote::showCardForSnapshot()
{
    if (card == nullptr)
        showCard();
}

void StartupNote::closeCard()
{
    if (card != nullptr && card->getParentComponent() != nullptr)
        card->getParentComponent()->removeChildComponent (card.get());
    card.reset();
    if (! waiting)
        stopTimer();
}

void StartupNote::place()
{
    if (card == nullptr)
        return;
    const auto inset = bottomInset != nullptr ? bottomInset() : 0;
    card->setTopLeftPosition (parent.getWidth() - NoteCard::width - 16, parent.getHeight() - inset - NoteCard::height - 12);
}

void StartupNote::chosen (supportnote::Choice c)
{
    auto s = loadState();
    supportnote::choose (s, c);
    saveState (s);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&parent), this, c] {
        if (safe == nullptr)
            return; // (the main window is gone, and this note with it)
        closeCard();
        if (c == supportnote::Choice::support)
            showSupportWindow();
    });
}
} // namespace lynx::support
