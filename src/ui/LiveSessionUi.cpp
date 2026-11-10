#include "ui/LiveSessionUi.h"

#include <memory>

#include "engine/AudioEngine.h"
#include "engine/LiveSessions.h"
#include "platform/Paths.h"
#include "ui/LiveChat.h"
#include "ui/LiveRemoteUi.h"
#include "ui/PluginWindow.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx::liveui
{
using namespace theme;

namespace
{
juce::File nameFile() { return platform::settingsFile ("session-name.txt"); }
void rememberName (const juce::String& name)
{
    nameFile().getParentDirectory().createDirectory();
    nameFile().replaceWithText (name.trim());
}

void warn (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, title, text);
}

bool quietEnd = false; // the person ended it themself: a hint is enough

// `then` once the session is live or over (polled: the relay answers in the background)
void whenSettled (LiveSessionManager& live, std::function<void (bool live)> then, int triesLeft = 600)
{
    using S = LiveSessionManager::State;
    const auto s = live.getState();
    if (s == S::live || s == S::ended || s == S::idle || triesLeft <= 0)
        return then (s == S::live);
    juce::Timer::callAfterDelay (100, [&live, then, triesLeft] { whenSettled (live, then, triesLeft - 1); });
}

LiveSessionManager::Options optionsFor (const Context& c, const juce::String& name)
{
    LiveSessionManager::Options o;
    o.name = name;
    o.projectFile = c.projectFile;
    if (const auto relay = juce::SystemStats::getEnvironmentVariable ("LYNX_RELAY_URL", {}); relay.isNotEmpty())
        o.relay = relay; // (tests: a local relay)
    return o;
}
} // namespace

juce::String myName()
{
    auto name = nameFile().loadFileAsString().trim();
    return name.isNotEmpty() ? name : juce::SystemStats::getFullUserName().trim();
}

void addMenuItems (juce::PopupMenu& menu, Context c)
{
    const bool active = c.live.isActive();
    menu.addItem (tr ("Start live session..."), ! active, false, [c] { start (c); });
    menu.addItem (tr ("Join live session..."), ! active, false, [c] { join (c); });
    menu.addItem (tr ("Show invite code..."), active && c.live.isHost() && c.live.getInviteCode().isNotEmpty(), false, [c] { showInvite (c); });
    // round 169j: chat, following the host, permissions during the session
    const auto unread = c.live.unreadChat();
    menu.addItem (tr ("Session chat...") + (unread > 0 ? " (" + juce::String (unread) + ")" : juce::String()), active, false, [c] { showChat (c.live); });
    if (! c.live.isHost())
        menu.addItem (tr ("Follow the host's transport"), active, c.live.isFollowingTransport(), [c] { c.live.setFollowTransport (! c.live.isFollowingTransport()); });
    else
    {
        menu.addItem (tr ("People and permissions..."), active, false, [c] { showPermissions (c.live); });
        // P5-A1 (round 190ai): which of the host's plugins guests may use
        const auto sharing = (int) c.live.sharedPlugins().size();
        menu.addItem (tr ("Share plugins with guests...") + (active && sharing > 0 ? " (" + juce::String (sharing) + ")" : juce::String()), active, false,
                      [c] { showSharePlugins (c.live, c.engine); });
    }
    menu.addItem (c.live.isHost() || ! active ? tr ("End live session") : tr ("Leave live session"), active, false, [c] { leave (c); });
}

void start (Context c)
{
    auto* w = new juce::AlertWindow (tr ("Start live session"),
                                     tr ("Up to {0} people join you and edit this project together over the internet, each on their own computer. "
                                         "Everything is end-to-end encrypted with the invite code: the LYNX SHARE relay only passes it on.",
                                         { juce::String (3) }),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("me", myName(), tr ("Your name"));
    w->addComboBox ("edit", { tr ("Only their own channels, patterns, inserts and rows (recommended)"), tr ("Everything") }, tr ("Guests may edit"));
    w->addComboBox ("keep", { tr ("Only their own part (recommended)"), tr ("A full copy of the project") }, tr ("When the session ends, guests keep"));
    w->addButton (tr ("Start"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([c, w] (int result) {
                            if (result != 1)
                                return;
                            const auto name = w->getTextEditorContents ("me").trim();
                            rememberName (name);
                            auto o = optionsFor (c, name);
                            o.guestsEditEverything = w->getComboBoxComponent ("edit")->getSelectedItemIndex() == 1;
                            o.guestsKeepFull = w->getComboBoxComponent ("keep")->getSelectedItemIndex() == 1;
                            c.live.start (o);
                            auto& live = c.live;
                            whenSettled (live, [c] (bool ok) {
                                if (ok)
                                    showInvite (c);
                                else if (! quietEnd)
                                    warn (tr ("Start live session"), c.live.getStatus());
                            });
                        }),
                        true);
}

void join (Context c)
{
    auto* w = new juce::AlertWindow (tr ("Join live session"),
                                     tr ("Type or paste the invite code the host sent you. The host's project opens here and every change "
                                         "anyone makes reaches everyone."),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("me", myName(), tr ("Your name"));
    w->addTextEditor ("code", juce::SystemClipboard::getTextFromClipboard().trim().startsWithIgnoreCase ("LYNX-") ? juce::SystemClipboard::getTextFromClipboard().trim() : juce::String(),
                      tr ("Invite code"));
    w->addButton (tr ("Join"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([c, w] (int result) {
                            if (result != 1)
                                return;
                            const auto name = w->getTextEditorContents ("me").trim();
                            const auto code = w->getTextEditorContents ("code").trim();
                            rememberName (name);
                            c.confirmDiscard ([c, name, code] {
                                c.live.join (code, optionsFor (c, name));
                                auto& live = c.live;
                                whenSettled (live, [c] (bool ok) {
                                    if (! ok && ! quietEnd)
                                        warn (tr ("Join live session"), c.live.getStatus());
                                });
                            });
                        }),
                        true);
}

void showInvite (Context c)
{
    const auto code = c.live.getInviteCode();
    if (code.isEmpty())
        return;
    auto* w = new juce::AlertWindow (tr ("Invite code"),
                                     tr ("Send this code to the people you want in the session (up to {0}). Anyone with it can join "
                                         "after you let them in; it stops working when you end the session.",
                                         { juce::String (3) }),
                                     juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("code", code);
    if (auto* e = w->getTextEditor ("code"))
    {
        e->setReadOnly (true);
        e->setFont (font (16.0f, true));
        e->setJustification (juce::Justification::centred);
    }
    w->addButton (tr ("Copy"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton (tr ("Close"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([code] (int result) {
                            if (result != 1)
                                return;
                            juce::SystemClipboard::copyTextToClipboard (code);
                            ui::postHint ({ tr ("Invite code copied"), {} }, 2500);
                        }),
                        true);
}

void leave (Context c)
{
    if (! c.live.isActive())
        return;
    const bool host = c.live.isHost();
    const auto text = host ? tr ("Everyone leaves the session. Your project stays as it is.")
                           : tr ("You leave the session.") + " " + tr ("Your part of the project stays here.");
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, host ? tr ("End live session") : tr ("Leave live session"), text,
                                        host ? tr ("End") : tr ("Leave"), tr ("Cancel"), nullptr,
                                        juce::ModalCallbackFunction::create ([c] (int r) {
                                            if (r != 1)
                                                return;
                                            quietEnd = true;
                                            c.live.leave();
                                            quietEnd = false;
                                        }));
}

void offerResume (Context c)
{
    const auto journal = LiveSessionManager::defaultJournal();
    const auto info = LiveSessionManager::readJournal (journal);
    if (! info)
    {
        journal.deleteFile(); // (too old for the relay, or damaged)
        return;
    }
    if (c.live.isActive())
        return;
    const auto when = info->saved.toString (true, true, false, true);
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::QuestionIcon, tr ("Resume live session"),
        tr ("LYNX SHARE closed during your live session ({0}, {1} people invited). Resume it? The people in it reconnect by themselves.",
            { when, juce::String (info->guests) }),
        tr ("Resume"), tr ("End it"), nullptr, juce::ModalCallbackFunction::create ([c, journal] (int r) {
            auto o = optionsFor (c, myName());
            if (r == 1)
            {
                o.projectFile = juce::File(); // (the journal knows the project)
                if (! c.live.resumeHost (journal, o))
                    warn (tr ("Resume live session"), tr ("The live session could not be resumed."));
                return;
            }
            LiveSessionManager::discardJournal (journal, o.relay);
        }));
}

void attach (LiveSessionManager& live)
{
    // P5-A1: what the host changes in a shared plugin's own window becomes parameter values when that window closes
    live.hostedEditorOpen = [] (juce::AudioPluginInstance& plugin) { return PluginWindows::isOpen (plugin); };
    live.onHint = [] (const juce::String& text) { ui::postHint ({ text, {} }, 4000); };
    live.onChat = [] (const LiveSessionManager::ChatLine& line) {
        if (! line.mine && ! chatVisible())
            ui::postHint ({ line.name + ": " + line.text.substring (0, 120), {} }, 5000);
    };
    live.onEnded = [] (const juce::String& text) {
        if (quietEnd)
            ui::postHint ({ text, {} }, 4000);
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Live session"), text);
    };
    live.onKnock = [] (const juce::String& name, std::function<void (bool)> answer) {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Live session"),
                                            tr ("{0} wants to join your live session.", { name }), tr ("Let them in"), tr ("Not now"), nullptr,
                                            juce::ModalCallbackFunction::create ([answer] (int r) { answer (r == 1); }));
    };
}

int paintPresence (juce::Graphics& g, juce::Rectangle<int> area, const LiveSessionManager& live)
{
    if (! live.isActive() || area.getWidth() < 40)
        return 0;
    const auto people = live.getPeople();
    auto r = area;
    const auto f = juce::Font (font (type::small, true));
    // the session badge: LIVE (connecting / knocking: its state)
    using S = LiveSessionManager::State;
    const bool on = live.getState() == S::live;
    const auto badge = on                                   ? tr ("LIVE")
                       : live.getState() == S::reconnecting ? tr ("Reconnecting...")
                       : live.getState() == S::waitingHost  ? tr ("Waiting for the host...")
                                                            : tr ("Connecting...");
    const auto bw = juce::GlyphArrangement::getStringWidthInt (f, badge) + 12;
    auto b = r.removeFromLeft (bw).reduced (0, 4).toFloat();
    g.setColour (on ? colour::accent : colour::bg5);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (colour::onAccent);
    g.setFont (f);
    g.drawText (badge, b, juce::Justification::centred, false);
    r.removeFromLeft (6);
    // names when they fit, else each person's initial in their colour
    int full = 0;
    for (const auto& p : people)
        full += 14 + juce::GlyphArrangement::getStringWidthInt (f, p.name + (p.me ? " (" + tr ("you") + ")" : juce::String())) + (p.recording ? 26 : 0) + 8;
    const bool compact = full > r.getWidth();
    for (const auto& p : people)
    {
        if (compact)
        {
            if (r.getWidth() < 20)
                break;
            auto dot = r.removeFromLeft (20).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (juce::Colour (p.colour).withMultipliedAlpha (p.online || p.me ? 1.0f : 0.35f));
            g.fillEllipse (dot);
            if (p.me || p.recording)
            {
                g.setColour (p.recording ? colour::accent : colour::textPrimary);
                g.drawEllipse (dot.expanded (1.5f), 1.5f);
            }
            g.setColour (juce::Colours::white);
            g.drawText (p.name.substring (0, 1).toUpperCase(), dot, juce::Justification::centred, false);
            r.removeFromLeft (2);
            continue;
        }
        const auto name = p.name + (p.me ? " (" + tr ("you") + ")" : juce::String());
        const auto w = 14 + juce::GlyphArrangement::getStringWidthInt (f, name) + (p.recording ? 26 : 0) + 8;
        auto cell = r.removeFromLeft (w);
        const auto dot = cell.removeFromLeft (14).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
        const bool here = p.online || p.me;
        g.setColour (juce::Colour (p.colour).withMultipliedAlpha (here ? 1.0f : 0.35f));
        if (here)
            g.fillEllipse (dot);
        else
            g.drawEllipse (dot, 1.5f); // (their connection dropped: they may come back)
        if (p.host)
        {
            g.setColour (colour::textPrimary.withAlpha (0.8f));
            g.drawEllipse (dot.expanded (1.5f), 1.0f);
        }
        g.setColour (p.me ? colour::textPrimary : here ? colour::textSecondary : colour::textDisabled);
        g.drawText (name, cell.removeFromLeft (cell.getWidth() - (p.recording ? 34 : 8)), juce::Justification::centredLeft, false);
        if (p.recording)
        {
            g.setColour (colour::accent);
            g.drawText (tr ("rec"), cell.removeFromLeft (26), juce::Justification::centredLeft, false);
        }
    }
    if (live.unreadChat() > 0 && r.getWidth() > 30)
    {
        const auto text = tr ("{0} new messages", { juce::String (live.unreadChat()) });
        const auto w = juce::GlyphArrangement::getStringWidthInt (f, text) + 12;
        auto b2 = r.removeFromLeft (w).reduced (0, 4).toFloat();
        g.setColour (colour::bg5);
        g.fillRoundedRectangle (b2, 3.0f);
        g.setColour (colour::textPrimary);
        g.drawText (text, b2, juce::Justification::centred, false);
        r.removeFromLeft (6);
    }
    if (live.standInsRendering() > 0 && r.getWidth() > 30)
    {
        g.setColour (colour::textDisabled);
        const auto text = tr ("rendering stand-ins...");
        const auto w = juce::GlyphArrangement::getStringWidthInt (f, text) + 8;
        g.drawText (text, r.removeFromLeft (w), juce::Justification::centredLeft, true);
    }
    if (live.mediaDownloading() > 0 && r.getWidth() > 30)
    {
        g.setColour (colour::textDisabled);
        g.drawText (tr ("{0} files arriving", { juce::String (live.mediaDownloading()) }), r, juce::Justification::centredLeft, true);
    }
    return area.getWidth() - r.getWidth();
}
} // namespace lynx::liveui
