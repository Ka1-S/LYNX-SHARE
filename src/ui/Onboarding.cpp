#include "ui/Onboarding.h"

#include "ui/DialogFrame.h"
#include "ui/I18n.h"
#include "ui/NativeChild.h"
#include "ui/Theme.h"

namespace lynx::onboardui
{
using onboarding::Choice;
namespace
{
constexpr int titleRow = ui::dialog::titleRowHeight;

juce::Colour cardText() { return theme::colour::textPrimary; }
juce::Colour cardGrey() { return theme::colour::textSecondary; }

void styleSecondary (juce::Button& b) { ui::dialog::styleButton (b, false); }
void stylePrimary (juce::Button& b) { ui::dialog::styleButton (b, true); }

// a flat text button (the "No more tips" / "Hide" links): grey text, no body
class LinkButton final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override {}
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool) override
    {
        g.setFont (theme::font (theme::type::small));
        g.setColour (over ? cardText() : cardGrey());
        g.drawFittedText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centredRight, 1, 0.9f);
    }
};

LinkButton& linkLook()
{
    static LinkButton look;
    return look;
}

int wrappedHeight (const juce::String& text, const juce::Font& font, int width, juce::Justification just = juce::Justification::topLeft)
{
    juce::AttributedString s;
    s.append (text, font);
    s.setJustification (just);
    juce::TextLayout layout;
    layout.createLayout (s, (float) juce::jmax (1, width));
    return (int) std::ceil (layout.getHeight());
}

void drawWrapped (juce::Graphics& g, const juce::String& text, const juce::Font& font, juce::Colour colour, juce::Rectangle<int> area,
                  juce::Justification just = juce::Justification::topLeft)
{
    juce::AttributedString s;
    s.append (text, font, colour);
    s.setJustification (just);
    juce::TextLayout layout;
    layout.createLayout (s, (float) area.getWidth());
    layout.draw (g, area.toFloat());
}

juce::String trademarkLine()
{
    // the names stay as they are in every language (only the sentence around them is translated)
    return tr ("{0} are trademarks of their owners; LYNX SHARE is not affiliated with them.",
               { "BandLab, FL Studio, GarageBand, Ableton Live, Bitwig, Logic, Cubase, Reaper, Pro Tools, Studio One" });
}

// --- the icons (LYNX's own vectors on a 64-unit square) -----------------------------------------------------------------
void glyph (juce::Graphics& g, juce::Rectangle<float> r, Choice c, juce::Colour ink, juce::Colour back)
{
    const auto s = r.getWidth() / 64.0f;
    const auto P = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * s, r.getY() + y * s); };
    const auto R = [&] (float x0, float y0, float x1, float y1) { return juce::Rectangle<float> (P (x0, y0), P (x1, y1)); };
    const auto stroke = 2.6f * s;
    g.setColour (ink);
    switch (c)
    {
        case Choice::mobile: // a phone with a beat on its screen
            g.drawRoundedRectangle (R (22, 12, 42, 52), 4.0f * s, stroke);
            g.drawLine ({ P (29, 46), P (35, 46) }, stroke);
            for (int i = 0; i < 3; ++i)
            {
                const float h = i == 0 ? 10.0f : i == 1 ? 16.0f : 8.0f;
                g.fillRect (R (26.0f + (float) i * 5.0f, 38.0f - h, 28.0f + (float) i * 5.0f, 38.0f));
            }
            break;
        case Choice::fl: // a step grid
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                {
                    const auto box = R (13.0f + (float) col * 10.0f, 17.0f + (float) row * 11.0f, 20.0f + (float) col * 10.0f, 25.0f + (float) row * 11.0f);
                    const bool on = (row == 0 && (col == 0 || col == 2)) || (row == 1 && (col == 1 || col == 3)) || row == 2;
                    if (on)
                        g.fillRoundedRectangle (box, 1.5f * s);
                    else
                        g.drawRoundedRectangle (box.reduced (0.8f * s), 1.5f * s, 1.6f * s);
                }
            break;
        case Choice::live: // a clip grid, one clip playing
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                {
                    const auto box = R (14.0f + (float) col * 12.0f, 14.0f + (float) row * 12.0f, 23.0f + (float) col * 12.0f, 23.0f + (float) row * 12.0f);
                    const bool on = (row == 0 && col == 0) || (row == 1 && col <= 1) || (row == 2 && col == 2);
                    if (on)
                        g.fillRoundedRectangle (box, 2.0f * s);
                    else
                        g.drawRoundedRectangle (box.reduced (0.8f * s), 2.0f * s, 1.6f * s);
                }
            {
                juce::Path play;
                play.addTriangle (P (16.5f, 16.0f), P (16.5f, 21.0f), P (20.5f, 18.5f));
                g.setColour (back);
                g.fillPath (play);
            }
            break;
        case Choice::timeline: // clips on a timeline with a playhead
        {
            const float spans[4][2] { { 12, 34 }, { 20, 50 }, { 10, 28 }, { 30, 52 } };
            for (int i = 0; i < 4; ++i)
                g.fillRoundedRectangle (R (spans[i][0], 16.0f + (float) i * 9.0f, spans[i][1], 22.0f + (float) i * 9.0f), 2.0f * s);
            g.drawLine ({ P (40, 11), P (40, 53) }, 1.6f * s);
            break;
        }
        case Choice::newcomer: // two notes and sparkles
        {
            g.drawLine ({ P (26, 44), P (26, 18) }, stroke);
            g.drawLine ({ P (26, 18), P (40, 14) }, stroke);
            g.drawLine ({ P (40, 14), P (40, 38) }, stroke);
            g.fillEllipse (R (17, 39, 27, 48));
            g.fillEllipse (R (31, 33, 41, 42));
            const float sparks[2][3] { { 48, 16, 4 }, { 14, 20, 3 } };
            for (const auto& k : sparks)
            {
                g.drawLine ({ P (k[0] - k[2], k[1]), P (k[0] + k[2], k[1]) }, 1.8f * s);
                g.drawLine ({ P (k[0], k[1] - k[2]), P (k[0], k[1] + k[2]) }, 1.8f * s);
            }
            break;
        }
        default:
            break;
    }
}

// a Playlist (the empty-state hint's icon): lines of clips under a ruler
void playlistGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
{
    glyph (g, r, Choice::timeline, ink, ink);
}

// --- one choice of the welcome card -------------------------------------------------------------------------------------
class ChoiceTile final : public juce::Button
{
public:
    explicit ChoiceTile (Choice c) : juce::Button (tr (onboarding::choiceTitle (c))), choice (c)
    {
        setComponentID (juce::String ("welcome.choice.") + onboarding::choiceId (c));
        setTitle (tr (onboarding::choiceTitle (c)));
        setDescription (tr (onboarding::choiceText (c)));
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    bool forceHover = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme::colour;
        const auto lit = over || down || forceHover || hasKeyboardFocus (false);
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (lit ? bg3.overlaidWith (accent.withAlpha (0.12f)) : bg3);
        g.fillRoundedRectangle (box, theme::radius::lg);
        g.setColour (lit ? accent : border);
        g.drawRoundedRectangle (box, theme::radius::lg, lit ? 1.6f : 1.0f);
        const auto h = (float) getHeight();
        const auto iconSize = juce::jmin (64.0f, h - 16.0f);
        paintChoiceIcon (g, { 10.0f, (h - iconSize) * 0.5f, iconSize, iconSize }, choice);
        const auto textX = juce::roundToInt (iconSize + 26.0f);
        auto text = getLocalBounds().withTrimmedLeft (textX).withTrimmedRight (44);
        const auto titleFont = theme::font (theme::type::title, true);
        const auto bodyFont = theme::font (theme::type::body);
        const auto descHeight = wrappedHeight (getDescription(), bodyFont, text.getWidth());
        const auto block = juce::roundToInt (titleFont.getHeight()) + 4 + descHeight;
        text = text.withSizeKeepingCentre (text.getWidth(), juce::jmin (text.getHeight() - 4, block));
        g.setFont (titleFont);
        g.setColour (cardText());
        g.drawFittedText (getButtonText(), text.removeFromTop (juce::roundToInt (titleFont.getHeight())), juce::Justification::centredLeft, 1, 0.9f);
        text.removeFromTop (4);
        drawWrapped (g, getDescription(), bodyFont, cardGrey(), text);
        // the chevron
        const auto cx = (float) getWidth() - 26.0f, cy = h * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.0f, cy - 6.0f);
        chevron.lineTo (cx + 3.0f, cy);
        chevron.lineTo (cx - 3.0f, cy + 6.0f);
        g.setColour (lit ? cardText() : textDisabled);
        g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const Choice choice;
};

// --- the welcome card ---------------------------------------------------------------------------------------------------
class WelcomeCard final : public juce::Component,
                          private juce::Timer
{
public:
    explicit WelcomeCard (WelcomeHost h) : host (std::move (h))
    {
        setComponentID ("welcome.card");
        for (int i = 0; i < onboarding::choiceCount; ++i)
        {
            auto* tile = tiles.add (new ChoiceTile (onboarding::choiceAt (i)));
            tile->onClick = [this, c = tile->choice] { pick (c); };
            addAndMakeVisible (tile);
        }
        closeButton.setComponentID ("welcome.close");
        closeButton.setTooltip (tr ("Skip: keep the FL-style Pattern layout"));
        styleSecondary (closeButton);
        closeButton.onClick = [this] { pick (Choice::none); };
        addAndMakeVisible (closeButton);

        languageLabel.setText (tr ("Language"), juce::dontSendNotification);
        soundLabel.setText (tr ("Sound:"), juce::dontSendNotification);
        for (auto* l : { &languageLabel, &soundLabel, &deviceLabel, &scanLabel })
        {
            l->setFont (theme::font (theme::type::body));
            l->setColour (juce::Label::textColourId, l == &deviceLabel ? cardText() : cardGrey());
            l->setMinimumHorizontalScale (0.8f);
            addAndMakeVisible (*l);
        }
        int id = 1;
        for (const auto& l : i18n::languages())
        {
            languageBox.addItem (juce::String::fromUTF8 (l.nativeName), id);
            if (i18n::current() == l.id)
                languageBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
        languageBox.setComponentID ("welcome.language");
        languageBox.onChange = [this] {
            const auto chosen = languageBox.getSelectedId() - 1;
            const auto& all = i18n::languages();
            if (chosen >= 0 && chosen < (int) all.size() && host.language && juce::String (all[(std::size_t) chosen].id) != i18n::current())
                host.language (all[(std::size_t) chosen].id);
        };
        addAndMakeVisible (languageBox);
        testButton.setButtonText (tr ("Test"));
        testButton.setComponentID ("welcome.test");
        styleSecondary (testButton);
        testButton.onClick = [this] {
            if (host.testSound)
                host.testSound();
        };
        addAndMakeVisible (testButton);
        moreButton.setButtonText (tr ("More setup (audio driver, plugin folders)..."));
        moreButton.setComponentID ("welcome.more");
        styleSecondary (moreButton);
        moreButton.onClick = [this] {
            applyUpdates();
            if (host.moreSetup)
                host.moreSetup();
        };
        addAndMakeVisible (moreButton);
        skipButton.setButtonText (tr ("Skip: keep the FL-style Pattern layout"));
        skipButton.setComponentID ("welcome.skip");
        styleSecondary (skipButton);
        skipButton.onClick = [this] { pick (Choice::none); };
        addAndMakeVisible (skipButton);
        // issue #233 (rollup 0.1.31 step 2, round 190ns's choice): the one connection LYNX SHARE makes by itself, chosen
        // here before it ever runs; ticked by default, and every answer (Skip / × / Esc too) applies what it shows
        updatesToggle.setButtonText (tr ("Check for updates automatically"));
        updatesToggle.setComponentID ("welcome.updates");
        updatesToggle.setToggleState (host.updatesOn, juce::dontSendNotification);
        updatesToggle.setTooltip (tr ("A few seconds after start-up LYNX SHARE asks GitHub for the newest version number; nothing about you is sent. "
                                      "It is the only connection LYNX SHARE makes by itself: plugin scans and everything else stay on this computer."));
        updatesToggle.setColour (juce::ToggleButton::textColourId, cardText());
        updatesNote.setText (tr ("Only the newest version number is asked for; nothing about you is sent."), juce::dontSendNotification);
        updatesNote.setFont (theme::font (theme::type::caption));
        updatesNote.setColour (juce::Label::textColourId, cardGrey());
        updatesNote.setJustificationType (juce::Justification::centredLeft);
        updatesNote.setMinimumHorizontalScale (0.75f);
        updatesNote.setBorderSize ({});
        if (host.updatesShown)
        {
            addAndMakeVisible (updatesToggle);
            addAndMakeVisible (updatesNote);
        }
        refreshStatus();
        setSize (welcomeWidth, idealHeight (welcomeWidth, 100000));
    }

    void visibilityChanged() override
    {
        if (isShowing() && host.scanStatus)
            startTimer (1000); // (only while shown)
        else
            stopTimer();
    }

    void timerCallback() override
    {
        if (! isShowing())
        {
            stopTimer();
            return;
        }
        refreshStatus();
    }

    void refreshStatus()
    {
        deviceLabel.setText (host.deviceName ? host.deviceName() : tr ("Speakers (system default)"), juce::dontSendNotification);
        scanLabel.setText (host.scanStatus ? host.scanStatus() : tr ("Plugins: scanning in the background"), juce::dontSendNotification);
    }

    // the card's height for `width`, with the tiles squeezed into `maxHeight` when the window is small
    int idealHeight (int width, int maxHeight) const
    {
        const auto fixed = fixedHeight (width);
        tileHeight = juce::jlimit (52, 82, (maxHeight - fixed) / onboarding::choiceCount - tileGap);
        return fixed + onboarding::choiceCount * (tileHeight + tileGap);
    }

    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds().toFloat();
        ui::dialog::paintFrame (g, card, ui::dialog::Kind::overlay, true);
        ui::dialog::paintTitle (g, getLocalBounds(), tr ("Welcome to LYNX SHARE"));
        auto body = getLocalBounds().withTrimmedTop (titleRow + 1).reduced (pad, 0);
        body.removeFromTop (14);
        g.setColour (cardText());
        const auto heading = theme::font (theme::type::display, true);
        g.setFont (heading);
        g.drawText (tr ("Where are you coming from?"), body.removeFromTop (headingHeight), juce::Justification::centred);
        drawWrapped (g, subtitle(), theme::font (theme::type::body), cardGrey(), body.removeFromTop (subtitleHeight (getWidth())),
                     juce::Justification::centredTop);
        // the line over the setup row and the trademark line
        g.setColour (theme::colour::border);
        g.fillRect (pad, dividerY, getWidth() - 2 * pad, 1);
        drawWrapped (g, trademarkLine(), theme::font (theme::type::caption), theme::colour::textDisabled,
                     { pad, getHeight() - footerTextHeight (getWidth()) - 10, getWidth() - 2 * pad, footerTextHeight (getWidth()) },
                     juce::Justification::centredTop);
    }

    void resized() override
    {
        const auto w = getWidth();
        idealHeight (w, getHeight());
        closeButton.setBounds (w - 14 - 30, ui::dialog::accentStrip + 7, 30, 30);
        auto r = getLocalBounds().withTrimmedTop (titleRow + 1).reduced (pad, 0);
        r.removeFromTop (14 + headingHeight + subtitleHeight (w) + 14);
        for (auto* tile : tiles)
        {
            tile->setBounds (r.removeFromTop (tileHeight));
            r.removeFromTop (tileGap);
        }
        r.removeFromTop (4);
        dividerY = r.getY();
        r.removeFromTop (12);
        auto row = r.removeFromTop (28);
        const auto fit = [] (juce::Label& l) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText())) + 10; };
        languageLabel.setBounds (row.removeFromLeft (fit (languageLabel)));
        languageBox.setBounds (row.removeFromLeft (150).reduced (0, 1));
        row.removeFromLeft (18);
        soundLabel.setBounds (row.removeFromLeft (fit (soundLabel)));
        deviceLabel.setBounds (row.removeFromLeft (juce::jmin (fit (deviceLabel), juce::jmax (80, row.getWidth() - 300))));
        testButton.setBounds (row.removeFromLeft (56).reduced (0, 1));
        row.removeFromLeft (14);
        scanLabel.setBounds (row);
        r.removeFromTop (12);
        auto buttons = r.removeFromTop (30);
        const auto textWidth = [] (juce::Button& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), b.getButtonText())) + 30; };
        moreButton.setBounds (buttons.removeFromLeft (juce::jmin (buttons.getWidth() / 2 - 6, textWidth (moreButton))));
        skipButton.setBounds (buttons.removeFromRight (juce::jmin (buttons.getWidth() - 12, textWidth (skipButton))));
        if (host.updatesShown) // the bottom row (issue #233): the toggle, its note beside it (two lines where it is long)
        {
            r.removeFromTop (updatesGap);
            auto row2 = r.removeFromTop (updatesRow);
            const auto toggleWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), updatesToggle.getButtonText())) + 34;
            updatesToggle.setBounds (row2.removeFromLeft (juce::jmin (toggleWidth, row2.getWidth() / 2)));
            row2.removeFromLeft (10);
            updatesNote.setBounds (row2);
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            pick (Choice::none);
            return true;
        }
        // 1…5 pick a choice (the keys of the card's order)
        const auto c = key.getTextCharacter();
        if (! key.getModifiers().isAnyModifierKeyDown() && c >= '1' && c < '1' + onboarding::choiceCount)
        {
            pick (onboarding::choiceAt ((int) (c - '1')));
            return true;
        }
        return false;
    }

    void setHover (int tile)
    {
        for (int i = 0; i < tiles.size(); ++i)
        {
            tiles[i]->forceHover = i == tile;
            tiles[i]->repaint();
        }
    }

private:
    static constexpr int pad = 24, headingHeight = 30, tileGap = 10, updatesGap = 8, updatesRow = 26;

    juce::String subtitle() const
    {
        return tr ("LYNX SHARE sets itself up to feel familiar. One click and you're in; change it any time in View > Layout style.");
    }

    int subtitleHeight (int width) const
    {
        return wrappedHeight (subtitle(), theme::font (theme::type::body), juce::jmax (100, width - 2 * pad - 60), juce::Justification::centredTop) + 4;
    }
    int footerTextHeight (int width) const
    {
        return wrappedHeight (trademarkLine(), theme::font (theme::type::caption), width - 2 * pad, juce::Justification::centredTop) + 2;
    }

    int fixedHeight (int width) const
    {
        // title row, heading, subtitle, (tiles), divider + setup row + buttons, trademark line
        return titleRow + 1 + 14 + headingHeight + subtitleHeight (width) + 14 + 4 + 12 + 28 + 12 + 30 + (host.updatesShown ? updatesGap + updatesRow : 0) + 14
               + footerTextHeight (width) + 10;
    }

    void pick (Choice c)
    {
        if (picked)
            return;
        picked = true;
        stopTimer();
        applyUpdates();
        if (host.choose)
            host.choose (c); // (may delete this)
    }

    void applyUpdates()
    {
        if (host.updatesShown && host.updates)
            host.updates (updatesToggle.getToggleState());
    }

    WelcomeHost host;
    juce::OwnedArray<ChoiceTile> tiles;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::Label languageLabel, soundLabel, deviceLabel, scanLabel;
    juce::ComboBox languageBox;
    juce::TextButton testButton, moreButton, skipButton;
    juce::ToggleButton updatesToggle;
    juce::Label updatesNote;
    mutable int tileHeight = 82;
    int dividerY = 0;
    bool picked = false;
};

// The card over the dimmed window (the Setup wizard's overlay, round 140): modal for the window's keys
class WelcomeOverlay final : public juce::Component
{
public:
    explicit WelcomeOverlay (WelcomeHost host) : card (std::move (host))
    {
        setComponentID ("welcome.overlay");
        setWantsKeyboardFocus (true);
        addAndMakeVisible (card);
    }

    void paint (juce::Graphics& g) override { ui::dialog::paintDim (g, getLocalBounds()); }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (16);
        const auto width = juce::jmin (welcomeWidth, area.getWidth());
        const auto height = juce::jmin (card.idealHeight (width, area.getHeight()), area.getHeight());
        card.setBounds (area.withSizeKeepingCentre (width, height));
    }

    void visibilityChanged() override
    {
        if (isShowing())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (this)] {
                if (safe != nullptr && safe->isShowing())
                    safe->grabKeyboardFocus();
            });
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        card.keyPressed (key);
        return true; // (the window's keys stay with the card while it shows)
    }
    bool keyStateChanged (bool) override { return true; }

    WelcomeCard card;
};
} // namespace

bool& profileExistedAtStart()
{
    static bool existed = false;
    return existed;
}

juce::Colour choiceColour (Choice c)
{
    switch (c)
    {
        case Choice::mobile: return juce::Colour (0xffe9a026);
        case Choice::fl: return theme::colour::accent;
        case Choice::live: return juce::Colour (0xff2abec8);
        case Choice::timeline: return juce::Colour (0xff7c5cf5);
        case Choice::newcomer: return juce::Colour (0xff34b25a);
        default: return theme::colour::bg5;
    }
}

void paintChoiceIcon (juce::Graphics& g, juce::Rectangle<float> area, Choice c)
{
    const auto colour = choiceColour (c);
    g.setColour (colour);
    g.fillRoundedRectangle (area, area.getWidth() * 9.0f / 64.0f);
    glyph (g, area, c, juce::Colours::white, colour);
}

std::unique_ptr<juce::Component> makeWelcomeOverlay (WelcomeHost host) { return std::make_unique<WelcomeOverlay> (std::move (host)); }
std::unique_ptr<juce::Component> makeWelcomeCard (WelcomeHost host) { return std::make_unique<WelcomeCard> (std::move (host)); }

int welcomeIdealHeight (const juce::Component& c)
{
    if (auto* card = dynamic_cast<const WelcomeCard*> (&c))
        return card->idealHeight (c.getWidth() > 0 ? c.getWidth() : welcomeWidth, 100000);
    return c.getHeight();
}

void setWelcomeHoverForSnapshot (juce::Component& c, int tile)
{
    if (auto* card = dynamic_cast<WelcomeCard*> (&c))
        card->setHover (tile);
    else if (auto* overlay = dynamic_cast<WelcomeOverlay*> (&c))
        overlay->card.setHover (tile);
}

// --- Next: 3 things to try ----------------------------------------------------------------------------------------------
struct Next3Card::Row final : public juce::Component
{
    Row (Next3Card& o, onboarding::Item i) : owner (o), item (i)
    {
        setComponentID (juce::String ("next3.") + onboarding::itemId (i));
        if (const auto* action = onboarding::itemAction (i))
        {
            button.setButtonText (tr (action));
            button.setComponentID (juce::String ("next3.do.") + onboarding::itemId (i));
            styleSecondary (button);
            button.onClick = [this] {
                if (owner.act)
                    owner.act (item);
            };
            addAndMakeVisible (button);
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        const auto box = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (bg3);
        g.fillRoundedRectangle (box, theme::radius::lg);
        g.setColour (border);
        g.drawRoundedRectangle (box, theme::radius::lg, 1.0f);
        const auto cy = (float) getHeight() * 0.5f;
        const juce::Rectangle<float> dot (12.0f, cy - 9.0f, 18.0f, 18.0f);
        if (done)
        {
            g.setColour (juce::Colour (0xff3dc95c));
            g.fillEllipse (dot);
            juce::Path tick;
            tick.startNewSubPath (dot.getCentreX() - 4.5f, cy);
            tick.lineTo (dot.getCentreX() - 1.0f, cy + 3.5f);
            tick.lineTo (dot.getCentreX() + 5.0f, cy - 4.0f);
            g.setColour (bg1);
            g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setColour (outline);
            g.drawEllipse (dot.reduced (1.0f), 2.0f);
        }
        auto text = getLocalBounds().withTrimmedLeft (42).withTrimmedRight (button.isVisible() ? button.getWidth() + 20 : 12);
        g.setFont (theme::font (theme::type::body, ! done));
        g.setColour (done ? cardGrey() : cardText());
        g.drawFittedText (tr (onboarding::itemText (item)), text, juce::Justification::centredLeft, 2, 0.85f);
    }

    void resized() override
    {
        if (button.isVisible())
        {
            const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), button.getButtonText())) + 26;
            button.setBounds (getLocalBounds().removeFromRight (w + 10).withTrimmedRight (10).withSizeKeepingCentre (w, 28));
        }
    }

    Next3Card& owner;
    onboarding::Item item;
    bool done = false;
    juce::TextButton button;
};

Next3Card::Next3Card()
{
    setComponentID ("next3.card");
    closeButton.setComponentID ("next3.close");
    closeButton.setTooltip (tr ("Hide (Help > Getting started shows it again)"));
    styleSecondary (closeButton);
    closeButton.onClick = [this] {
        if (close)
            close();
    };
    addAndMakeVisible (closeButton);
    hideButton.setButtonText (tr ("Hide"));
    hideButton.setComponentID ("next3.hide");
    hideButton.setLookAndFeel (&linkLook());
    hideButton.onClick = closeButton.onClick;
    addAndMakeVisible (hideButton);
    tutorialsButton.setButtonText (tr ("Learn more..."));
    tutorialsButton.setComponentID ("next3.tutorials");
    styleSecondary (tutorialsButton);
    tutorialsButton.onClick = [this] {
        if (tutorials)
            tutorials();
    };
    addChildComponent (tutorialsButton);
}

Next3Card::~Next3Card() { hideButton.setLookAndFeel (nullptr); }

void Next3Card::show (Choice c, std::uint32_t ticked, bool allDone)
{
    if (c != choice)
    {
        choice = c;
        rows.clear();
        for (const auto item : onboarding::next3 (c))
        {
            rows.push_back (std::make_unique<Row> (*this, item));
            addAndMakeVisible (*rows.back());
        }
    }
    for (auto& row : rows)
    {
        const auto now = (ticked & (1u << (std::uint32_t) row->item)) != 0;
        if (now != row->done)
        {
            row->done = now;
            row->button.setVisible (! now && row->button.getButtonText().isNotEmpty());
            row->resized();
            row->repaint();
        }
    }
    done = ticked;
    if (finished != allDone)
    {
        finished = allDone;
        tutorialsButton.setVisible (allDone);
    }
    setSize (width, idealHeight());
    resized();
    repaint();
}

int Next3Card::idealHeight() const
{
    return titleRow - 8 + 22 + 10 + (int) rows.size() * (52 + 8) + 34 + (finished ? 40 : 0);
}

void Next3Card::paint (juce::Graphics& g)
{
    ui::dialog::paintFrame (g, getLocalBounds().toFloat(), ui::dialog::Kind::overlay, false);
    auto r = getLocalBounds().reduced (18, 0).withTrimmedTop (ui::dialog::accentStrip + 10);
    g.setFont (theme::font (theme::type::title + 2.0f, true));
    g.setColour (cardText());
    g.drawText (tr ("Next: 3 things to try"), r.removeFromTop (24).withTrimmedRight (40), juce::Justification::centredLeft);
    g.setFont (theme::font (theme::type::body));
    g.setColour (cardGrey());
    g.drawFittedText (tr (onboarding::next3Subtitle (choice)), r.removeFromTop (20), juce::Justification::centredLeft, 1, 0.85f);
    if (finished)
    {
        const auto y = getHeight() - 34 - 40;
        g.setFont (theme::font (theme::type::body, true));
        g.setColour (juce::Colour (0xff3dc95c));
        g.drawText (tr ("Nice, that's the basics."), juce::Rectangle<int> (18, y, getWidth() - 36 - 120, 34), juce::Justification::centredLeft);
    }
    g.setFont (theme::font (theme::type::small));
    g.setColour (theme::colour::textDisabled);
    g.drawText (tr ("Help > Getting started shows this again"), juce::Rectangle<int> (18, getHeight() - 32, getWidth() - 36 - 60, 26),
                juce::Justification::centredLeft);
}

void Next3Card::resized()
{
    closeButton.setBounds (getWidth() - 14 - 28, ui::dialog::accentStrip + 8, 28, 28);
    auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (titleRow - 8 + 22 + 10);
    for (auto& row : rows)
    {
        row->setBounds (r.removeFromTop (52));
        r.removeFromTop (8);
    }
    if (finished)
        tutorialsButton.setBounds (getWidth() - 18 - 120, getHeight() - 34 - 40 + 3, 120, 28);
    hideButton.setBounds (getWidth() - 18 - 60, getHeight() - 32, 60, 26);
}

// --- a contextual tip ---------------------------------------------------------------------------------------------------
TipCard::TipCard (onboarding::Tip t) : tip (t)
{
    setComponentID (juce::String ("tip.") + onboarding::tipId (t));
    setWantsKeyboardFocus (false);
    actionButton.setButtonText (tr (onboarding::tipAction (t)));
    actionButton.setComponentID ("tip.action");
    stylePrimary (actionButton);
    actionButton.onClick = [this] {
        if (action)
            action();
    };
    gotItButton.setButtonText (tr ("Got it"));
    gotItButton.setComponentID ("tip.gotIt");
    styleSecondary (gotItButton);
    gotItButton.onClick = [this] {
        if (gotIt)
            gotIt();
    };
    noMoreButton.setButtonText (tr ("No more tips"));
    noMoreButton.setComponentID ("tip.noMore");
    noMoreButton.setLookAndFeel (&linkLook());
    noMoreButton.onClick = [this] {
        if (noMore)
            noMore();
    };
    for (auto* b : { &actionButton, &gotItButton, &noMoreButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    setSize (width, idealHeight());
}

int TipCard::idealHeight() const
{
    return 16 + 24 + 6 + wrappedHeight (tr (onboarding::tipText (tip)), theme::font (theme::type::body), width - 32) + 16 + 30 + 16;
}

void TipCard::paint (juce::Graphics& g)
{
    ui::dialog::paintFrame (g, getLocalBounds().toFloat(), ui::dialog::Kind::coach, false);
    auto r = getLocalBounds().reduced (16);
    g.setFont (theme::font (theme::type::title, true));
    g.setColour (cardText());
    g.drawText (tr (onboarding::tipTitle (tip)), r.removeFromTop (24), juce::Justification::centredLeft);
    r.removeFromTop (6);
    drawWrapped (g, tr (onboarding::tipText (tip)), theme::font (theme::type::body), cardGrey(), r.withTrimmedBottom (46));
}

void TipCard::resized()
{
    auto row = getLocalBounds().reduced (16).removeFromBottom (30);
    const auto w = [] (juce::Button& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), b.getButtonText())) + 28; };
    actionButton.setBounds (row.removeFromLeft (w (actionButton)));
    row.removeFromLeft (8);
    gotItButton.setBounds (row.removeFromLeft (w (gotItButton)));
    noMoreButton.setBounds (row.removeFromRight (juce::jmin (row.getWidth() - 8, w (noMoreButton))));
}

bool TipCard::keyPressed (const juce::KeyPress&) { return false; }

// --- an empty-state hint ------------------------------------------------------------------------------------------------
EmptyHintCard::EmptyHintCard (onboarding::EmptyHint k, std::vector<Button> list, std::function<juce::Rectangle<int>()> w)
    : kind (k), where (std::move (w))
{
    setComponentID (juce::String ("empty.") + (k == onboarding::EmptyHint::playlist ? "playlist" : k == onboarding::EmptyHint::clipGrid ? "clipGrid" : "simple"));
    setInterceptsMouseClicks (false, true); // (clicks on the card's body reach the view under it)
    setWantsKeyboardFocus (false);
    for (auto& b : list)
    {
        auto* button = buttons.add (new juce::TextButton (b.text));
        button->setComponentID (b.id);
        button->setWantsKeyboardFocus (false);
        styleSecondary (*button);
        button->onClick = std::move (b.run);
        addAndMakeVisible (button);
    }
    setBufferedToImage (true); // (static: drawn once, then a copy whenever the view under it repaints)
    setSize (width, idealHeight());
}

EmptyHintCard::~EmptyHintCard()
{
    if (watched != nullptr)
        watched->removeComponentListener (this);
}

int EmptyHintCard::idealHeight() const
{
    return 24 + 56 + 14 + 22 + 6 + wrappedHeight (tr (onboarding::emptyText (kind)), theme::font (theme::type::body), width - 64, juce::Justification::centredTop) + 18
         + (buttons.isEmpty() ? 0 : 30) + 22;
}

void EmptyHintCard::attachTo (juce::Component* parent)
{
    if (watched != nullptr && watched != parent)
        watched->removeComponentListener (this);
    if (parent == nullptr)
    {
        if (auto* p = getParentComponent())
            p->removeChildComponent (this);
        watched = nullptr;
        return;
    }
    if (getParentComponent() != parent)
    {
        parent->addAndMakeVisible (*this);
        parent->addComponentListener (this);
        watched = parent;
    }
    toFront (false);
    place();
}

void EmptyHintCard::place()
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;
    auto area = where ? where() : parent->getLocalBounds();
    area = area.getIntersection (parent->getLocalBounds()).reduced (12);
    const auto w = juce::jmin (width, area.getWidth()), h = juce::jmin (idealHeight(), area.getHeight());
    setVisible (w >= 260 && h >= 120); // (a tiny view: no hint)
    setBounds (area.withSizeKeepingCentre (w, h));
}

void EmptyHintCard::paint (juce::Graphics& g)
{
    using namespace theme::colour;
    const auto box = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (bg2.withAlpha (0.96f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (borderStrong);
    g.drawRoundedRectangle (box, 8.0f, 1.0f);
    auto r = getLocalBounds().reduced (32, 0).withTrimmedTop (24);
    const auto icon = r.removeFromTop (56).withSizeKeepingCentre (56, 56).toFloat();
    if (kind == onboarding::EmptyHint::playlist)
    {
        g.setColour (bg5);
        g.fillRoundedRectangle (icon, 8.0f);
        playlistGlyph (g, icon, textPrimary);
    }
    else
        paintChoiceIcon (g, icon, kind == onboarding::EmptyHint::clipGrid ? Choice::live : Choice::mobile);
    r.removeFromTop (14);
    g.setFont (theme::font (theme::type::title + 1.0f, true));
    g.setColour (textPrimary);
    g.drawText (tr (onboarding::emptyTitle (kind)), r.removeFromTop (22), juce::Justification::centred);
    r.removeFromTop (6);
    drawWrapped (g, tr (onboarding::emptyText (kind)), theme::font (theme::type::body), textSecondary,
                 r.removeFromTop (wrappedHeight (tr (onboarding::emptyText (kind)), theme::font (theme::type::body), r.getWidth(), juce::Justification::centredTop) + 2),
                 juce::Justification::centredTop);
}

void EmptyHintCard::resized()
{
    if (buttons.isEmpty())
        return;
    const auto w = [] (juce::Button& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body), b.getButtonText())) + 28; };
    int total = (buttons.size() - 1) * 8;
    for (auto* b : buttons)
        total += w (*b);
    auto row = juce::Rectangle<int> (0, getHeight() - 22 - 30, getWidth(), 30).withSizeKeepingCentre (juce::jmin (total, getWidth() - 24), 30);
    for (auto* b : buttons)
    {
        b->setBounds (row.removeFromLeft (w (*b)));
        row.removeFromLeft (8);
    }
}

bool EmptyHintCard::hitTest (int x, int y)
{
    for (auto* b : buttons)
        if (b->isVisible() && b->getBounds().contains (x, y))
            return true;
    return false;
}

// --- hosting ------------------------------------------------------------------------------------------------------------
void hostOnTop (juce::Component& card, juce::Component& window, juce::Rectangle<int> bounds)
{
    if (nativechild::dockingEnabled() && window.getPeer() != nullptr)
    {
        if (! nativechild::isAttached (card))
        {
            if (auto* p = card.getParentComponent())
                p->removeChildComponent (&card);
            card.setAlwaysOnTop (false);
            card.setBounds (bounds);
            if (nativechild::attach (card, window, 0))
            {
                nativechild::addFloater (card);
                card.setVisible (true);
                nativechild::raiseOverlays();
                return;
            }
        }
        else
        {
            nativechild::reattachIfNeeded (card, window);
            nativechild::place (card, window, bounds);
            card.setVisible (true);
            nativechild::raiseOverlays();
            return;
        }
    }
    if (card.getParentComponent() != &window)
        window.addAndMakeVisible (card);
    card.setAlwaysOnTop (true);
    card.setBounds (bounds);
    card.setVisible (true);
    card.toFront (false);
}

void removeHosted (juce::Component& card)
{
    if (nativechild::isAttached (card))
    {
        nativechild::removeFloater (card);
        nativechild::detach (card);
    }
    if (auto* p = card.getParentComponent())
        p->removeChildComponent (&card);
}

juce::Rectangle<int> areaIn (juce::Component& window, juce::Component& c)
{
    auto* top = c.getTopLevelComponent();
    auto* main = window.getTopLevelComponent();
    if (top == nullptr || main == nullptr || top == main || ! nativechild::isAttached (*top))
        return window.getLocalArea (&c, c.getLocalBounds());
    return window.getLocalArea (main, top->getLocalArea (&c, c.getLocalBounds()) + top->getPosition());
}

namespace
{
bool matches (juce::Component& c, const juce::String& key)
{
    if (c.getComponentID() == key)
        return true;
    if (c.getName().isNotEmpty() && c.getName() == key)
        return true;
    if (auto* b = dynamic_cast<juce::Button*> (&c))
        return b->getButtonText().isNotEmpty() && b->getButtonText() == key;
    return false;
}

juce::Component* findIn (juce::Component& root, const juce::String& key, bool byIdOnly)
{
    for (auto* c : root.getChildren())
    {
        if (! c->isVisible())
            continue;
        if ((byIdOnly ? c->getComponentID() == key : matches (*c, key)) && ! c->getLocalBounds().isEmpty())
            return c;
        if (auto* inner = findIn (*c, key, byIdOnly))
            return inner;
    }
    return nullptr;
}

juce::Component* findPass (juce::Component& window, const juce::String& key, bool byIdOnly)
{
    if (auto* c = findIn (window, key, byIdOnly))
        return c;
    if (auto* peer = window.getPeer())
        for (auto* attached : nativechild::attachedChildren (peer->getNativeHandle()))
            if (attached != nullptr && attached->isVisible())
                if (auto* c = (byIdOnly ? attached->getComponentID() == key : matches (*attached, key)) ? attached : findIn (*attached, key, byIdOnly))
                    return c;
    return nullptr;
}
} // namespace

juce::Component* findShowing (juce::Component& window, const juce::String& key)
{
    if (key.isEmpty())
        return nullptr;
    if (auto* c = findPass (window, key, true)) // (component IDs first: they never change with the language)
        return c;
    return findPass (window, key, false);
}
} // namespace lynx::onboardui
