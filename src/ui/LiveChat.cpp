#include "ui/LiveChat.h"

#include <memory>

#include "engine/LiveSessions.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx::liveui
{
using namespace theme;

namespace
{
// The chat: the lines (name in the person's colour), a text box (Enter sends) and, for guests, "Follow the host's
// transport".
class ChatPanel final : public juce::Component, private juce::Timer
{
public:
    explicit ChatPanel (LiveSessionManager& l) : live (l)
    {
        addAndMakeVisible (log);
        log.setMultiLine (true, true);
        log.setReadOnly (true);
        log.setCaretVisible (false);
        log.setScrollbarsShown (true);
        log.setColour (juce::TextEditor::backgroundColourId, colour::bg0);
        log.setColour (juce::TextEditor::outlineColourId, colour::border);
        addAndMakeVisible (input);
        input.setTextToShowWhenEmpty (tr ("Write a message and press Enter"), colour::textDisabled);
        input.setColour (juce::TextEditor::backgroundColourId, colour::bg1);
        input.onReturnKey = [this] { send(); };
        addAndMakeVisible (sendButton);
        sendButton.setButtonText (tr ("Send"));
        sendButton.onClick = [this] { send(); };
        addAndMakeVisible (follow);
        follow.setButtonText (tr ("Follow the host's transport (play, stop and position)"));
        follow.setToggleState (live.isFollowingTransport(), juce::dontSendNotification);
        follow.onClick = [this] { live.setFollowTransport (follow.getToggleState()); };
        follow.setVisible (! live.isHost());
        setSize (420, 380);
        refresh();
        startTimer (250);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        if (follow.isVisible())
            follow.setBounds (r.removeFromBottom (24));
        auto bottom = r.removeFromBottom (30);
        r.removeFromBottom (6);
        sendButton.setBounds (bottom.removeFromRight (72));
        bottom.removeFromRight (6);
        input.setBounds (bottom);
        log.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (colour::bg2); }

private:
    LiveSessionManager& live;
    juce::TextEditor log, input;
    juce::TextButton sendButton;
    juce::ToggleButton follow;
    std::size_t shown = 0;
    juce::int64 lastAt = -1;
    juce::String lastText;

    void send()
    {
        const auto text = input.getText().trim();
        if (text.isEmpty())
            return;
        if (live.getState() != LiveSessionManager::State::live)
        {
            log.moveCaretToEnd();
            log.insertTextAtCaret (tr ("(Not connected: the message was not sent.)") + "\n");
            return;
        }
        live.sendChat (text);
        input.clear();
    }

    void timerCallback() override { refresh(); }

    void refresh()
    {
        const auto& lines = live.getChat();
        const bool changed = lines.size() != shown || (! lines.empty() && (lines.back().at != lastAt || lines.back().text != lastText));
        if (changed)
        {
            log.clear();
            for (const auto& line : lines)
            {
                log.setColour (juce::TextEditor::textColourId, juce::Colour (line.colour).withAlpha (1.0f));
                log.setFont (juce::Font (font (13.0f, true)));
                log.insertTextAtCaret (line.name + (line.mine ? " (" + tr ("you") + ")" : juce::String()) + "  "
                                       + juce::Time (line.at).toString (false, true, false, true) + "\n");
                log.setColour (juce::TextEditor::textColourId, colour::textPrimary);
                log.setFont (juce::Font (font (13.0f)));
                log.insertTextAtCaret (line.text + "\n\n");
            }
            shown = lines.size();
            lastAt = lines.empty() ? -1 : lines.back().at;
            lastText = lines.empty() ? juce::String() : lines.back().text;
            log.moveCaretToEnd();
        }
        if (follow.getToggleState() != live.isFollowingTransport())
            follow.setToggleState (live.isFollowingTransport(), juce::dontSendNotification);
        if (isShowing())
            live.markChatRead();
    }
};

class ChatWindow final : public juce::DocumentWindow
{
public:
    explicit ChatWindow (LiveSessionManager& live) : juce::DocumentWindow (tr ("Session chat"), colour::bg2, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ChatPanel (live), true);
        setResizable (true, false);
        setResizeLimits (300, 220, 1200, 1400);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }
    void closeButtonPressed() override { setVisible (false); }
};

std::unique_ptr<ChatWindow> window;
LiveSessionManager* windowFor = nullptr;
} // namespace

void showChat (LiveSessionManager& live)
{
    if (window == nullptr || windowFor != &live)
    {
        window = std::make_unique<ChatWindow> (live);
        windowFor = &live;
    }
    window->setVisible (true);
    window->toFront (true);
    if (auto* panel = window->getContentComponent())
        panel->grabKeyboardFocus();
    live.markChatRead();
}

void closeChat()
{
    window.reset();
    windowFor = nullptr;
}

bool chatVisible()
{
    return window != nullptr && window->isVisible();
}

void showPermissions (LiveSessionManager& live)
{
    if (! live.isHost() || ! live.isActive())
        return;
    std::vector<LiveSessionManager::Person> guests;
    for (const auto& p : live.getPeople())
        if (! p.host)
            guests.push_back (p);
    if (guests.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("People and permissions"),
                                                tr ("Nobody has joined yet. Send the invite code to the people you want in the session."));
        return;
    }
    auto* w = new juce::AlertWindow (tr ("People and permissions"),
                                     tr ("What each person may change, from now on. Turning a permission off never deletes anything."),
                                     juce::MessageBoxIconType::NoIcon);
    const juce::StringArray roles { tr ("Their own channels, patterns, inserts and rows"), tr ("Their own parts + tempo and project settings"),
                                    tr ("Everything"), tr ("Listen only (no edits)") };
    const juce::StringArray keeps { tr ("Only their own part"), tr ("A full copy of the project") };
    for (const auto& p : guests)
    {
        const auto id = juce::String ((int) p.tag);
        const auto label = p.name + (p.online ? juce::String() : " (" + tr ("offline") + ")");
        w->addComboBox ("role" + id, roles, label);
        const int role = p.permissions.listener ? 3 : p.permissions.editEverything ? 2 : p.permissions.settings ? 1 : 0;
        w->getComboBoxComponent ("role" + id)->setSelectedItemIndex (role, juce::dontSendNotification);
        w->addComboBox ("keep" + id, keeps, tr ("{0} keeps at the end", { p.name }));
        w->getComboBoxComponent ("keep" + id)->setSelectedItemIndex (p.keepFull ? 1 : 0, juce::dontSendNotification);
    }
    w->addButton (tr ("Apply"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, guests, &live] (int result) {
                            if (result != 1 || ! live.isActive())
                                return;
                            for (const auto& p : guests)
                            {
                                const auto id = juce::String ((int) p.tag);
                                const int role = w->getComboBoxComponent ("role" + id)->getSelectedItemIndex();
                                live::Permissions perms;
                                perms.settings = role == 1;
                                perms.editEverything = role == 2;
                                perms.listener = role == 3;
                                const bool full = w->getComboBoxComponent ("keep" + id)->getSelectedItemIndex() == 1;
                                if (! (perms == p.permissions) || full != p.keepFull)
                                    live.setPermissions (p.tag, perms, full);
                            }
                        }),
                        true);
}
} // namespace lynx::liveui
