#pragma once

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

// One dialog system for LYNX's own dialogs (round 190jp, design190ia slice 20 X-DIALOG = SG-20/21/22 + FW-17).
//
// Before it there were four dialog frames (the Setup wizard's red outline, About / Support's 3 px top strip, the Simple
// cards' red divider, the tour's red outline), three title sizes and a primary button that was sometimes the accent and
// sometimes not. Now:
//  - every LYNX-own modal is a bg2 body in a 1 px borderStrong frame with the brand's 3 px accent strip on top, a 44 px
//    title row (the title in type::title bold, centred) over a 1 px divider; a card inside the main window (Kind::overlay)
//    also gets rounded corners and a soft shadow, a top-level dialog window (Kind::window) is square;
//  - the tour's coachmark (Kind::coach) keeps its accent outline: it must stand out on the dimmed window;
//  - exactly one accent button per dialog, the Return default (SG-21);
//  - FL's confirm habits (FW-17): the file name in the sentence, in the highlight colour; the default button stands out;
//    every button has an underlined letter that presses it (FL's Y / N also answer Save / Discard).
// Header-only: Dialog is an AlertWindow (so the shot routes and modal helpers that take one keep working) that draws and
// lays itself out. Static paints only: no timer, nothing animates.
namespace lynx::ui::dialog
{
inline constexpr int titleRowHeight = 44; // the strip, the title, the divider
inline constexpr int accentStrip = 3;
inline constexpr int padding = 24;
inline constexpr int buttonHeight = 28, minButtonWidth = 76, buttonGap = 8;

enum class Kind
{
    window,  // a top-level dialog window: square corners
    overlay, // a card inside the main window: rounded, with a shadow
    coach    // the tour's coachmark: rounded, with a shadow and the accent outline (no strip)
};

// The dim behind an overlay card (the window stays faintly visible).
inline void paintDim (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::colour::bg0.withAlpha (0.72f));
    g.fillRect (area);
}

// The frame of a dialog card at `card` (body, border, accent strip; the title row's divider when `withTitleRow`).
inline void paintFrame (juce::Graphics& g, juce::Rectangle<float> card, Kind kind, bool withTitleRow = true)
{
    using namespace theme::colour;
    const auto corner = kind == Kind::window ? 0.0f : theme::radius::lg;
    if (kind != Kind::window)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (card.translated (0.0f, 4.0f).expanded (2.0f), corner + 2.0f);
    }
    juce::Path shape;
    shape.addRoundedRectangle (card, corner);
    g.setColour (bg2);
    g.fillPath (shape);
    if (kind == Kind::coach)
    {
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (card.reduced (0.5f), corner, 1.2f);
        return;
    }
    g.setColour (borderStrong);
    if (corner > 0.0f)
        g.drawRoundedRectangle (card.reduced (0.5f), corner, 1.0f);
    else
        g.drawRect (card, 1.0f);
    juce::Graphics::ScopedSaveState keep (g);
    g.reduceClipRegion (shape);
    if (withTitleRow) // the divider under the title row
    {
        g.setColour (border);
        g.fillRect (card.getX() + 1.0f, std::round (card.getY()) + (float) titleRowHeight, card.getWidth() - 2.0f, 1.0f);
    }
    g.setColour (accent); // the brand's strip (Support's, now on every LYNX-own modal), over the frame's top line
    g.fillRect (card.withHeight ((float) accentStrip));
}

// The title row of a card (below the strip, above the divider) and the area under it.
inline juce::Rectangle<int> titleRow (juce::Rectangle<int> card) { return card.withHeight (titleRowHeight).withTrimmedTop (accentStrip); }
inline juce::Rectangle<int> bodyOf (juce::Rectangle<int> card) { return card.withTrimmedTop (titleRowHeight + 1); }

inline juce::Font titleFont() { return theme::font (theme::type::title, true); }

// A title label in the title row's style.
inline void styleTitle (juce::Label& title)
{
    title.setFont (titleFont());
    title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    title.setJustificationType (juce::Justification::centred);
    title.setMinimumHorizontalScale (0.8f);
}

inline void paintTitle (juce::Graphics& g, juce::Rectangle<int> card, const juce::String& title)
{
    g.setColour (theme::colour::textPrimary);
    g.setFont (titleFont());
    g.drawFittedText (title, titleRow (card).reduced (48, 0), juce::Justification::centred, 1, 0.8f);
}

// SG-21: one accent button per dialog, the Return default; the others are secondary (bg4).
inline void styleButton (juce::Button& b, bool primary)
{
    using namespace theme::colour;
    b.setColour (juce::TextButton::buttonColourId, primary ? accent : bg4);
    b.setColour (juce::TextButton::textColourOffId, primary ? onAccent : textPrimary);
}

// "Save changes to {0}?" with {0} in the accent (FL's confirm names the file in its highlight colour).
inline juce::AttributedString sentence (const juce::String& text, const juce::String& name, const juce::Font& font)
{
    juce::AttributedString s;
    const auto at = text.indexOf ("{0}");
    const auto plain = theme::colour::textPrimary;
    if (at < 0)
        s.append (text, font, plain);
    else
    {
        if (at > 0)
            s.append (text.substring (0, at), font, plain);
        s.append (name, font, theme::colour::accent);
        if (at + 3 < text.length())
            s.append (text.substring (at + 3), font, plain);
    }
    s.setWordWrap (juce::AttributedString::byWord);
    s.setLineSpacing (2.0f);
    return s;
}

// A dialog button with FL's underlined access letter (the key that presses it).
class Button final : public juce::TextButton
{
public:
    Button (const juce::String& text, int resultValue) : juce::TextButton (text), result (resultValue) {}

    int result;
    int letterIndex = -1; // the underlined character of the text (-1: none)
    bool primary = false, cancels = false;
    juce::String aliases; // more letters that press it (lower case), e.g. FL's "y" for Save

    juce::juce_wchar letter() const
    {
        const auto t = getButtonText();
        return juce::isPositiveAndBelow (letterIndex, t.length()) ? juce::CharacterFunctions::toLowerCase (t[letterIndex]) : 0;
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto& lf = getLookAndFeel();
        lf.drawButtonBackground (g, *this, findColour (getToggleState() ? buttonOnColourId : buttonColourId), over, down);
        const auto font = theme::font (theme::type::body, primary);
        const auto text = getButtonText();
        const auto fg = findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f);
        g.setColour (fg);
        g.setFont (font);
        const auto area = getLocalBounds().reduced (8, 0);
        g.drawFittedText (text, area, juce::Justification::centred, 1, 0.85f);
        if (juce::isPositiveAndBelow (letterIndex, text.length()))
        {
            // the access letter, underlined 1 px under the baseline (whole pixels)
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (font, text, 0.0f, 0.0f);
            const auto width = glyphs.getBoundingBox (0, -1, true).getWidth();
            const auto x0 = (float) area.getX() + ((float) area.getWidth() - width) / 2.0f;
            const auto glyph = glyphs.getBoundingBox (letterIndex, 1, true);
            const auto baseline = std::round ((float) getHeight() / 2.0f + font.getAscent() / 2.0f - font.getDescent() / 4.0f) + 1.0f;
            g.fillRect (std::round (x0 + glyph.getX()), baseline, std::round (juce::jmax (4.0f, glyph.getWidth())), 1.0f);
        }
    }
};

// A LYNX dialog window. Add the buttons (and any content) then call `layout()`. `enterModalState` as for any
// AlertWindow; the modal result is the pressed button's result.
class Dialog : public juce::AlertWindow
{
public:
    Dialog (const juce::String& title, int width = 400, juce::MessageBoxIconType iconType = juce::MessageBoxIconType::NoIcon)
        : juce::AlertWindow (title, {}, iconType), dialogWidth (width), icon (iconType)
    {
    }

    void setSentence (juce::AttributedString text)
    {
        message = std::move (text);
        layout();
    }
    void setSentence (const juce::String& text)
    {
        auto s = sentence (text, {}, theme::font (theme::type::body));
        setSentence (s);
    }

    // `primary`: the accent button and the Return key; `cancels`: Escape. The access letter is the first letter of the
    // text no other button took (FL underlines one per button); `aliases` are more letters that press it.
    Button& addDialogButton (const juce::String& text, int result, bool primary = false, bool cancels = false, const juce::String& aliases = {})
    {
        auto* b = buttons.add (new Button (text, result));
        b->primary = primary;
        b->cancels = cancels;
        b->aliases = aliases.toLowerCase();
        styleButton (*b, primary);
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, b] { exitModalState (b->result); };
        for (int i = 0; i < text.length(); ++i)
        {
            const auto c = juce::CharacterFunctions::toLowerCase (text[i]);
            if (! juce::CharacterFunctions::isLetterOrDigit (c))
                continue;
            auto taken = false;
            for (auto* other : buttons)
                taken = taken || (other != b && (other->letter() == c || other->aliases.containsChar (c)));
            if (! taken)
            {
                b->letterIndex = i;
                break;
            }
        }
        addAndMakeVisible (b);
        layout();
        return *b;
    }

    // a component shown under the sentence, centred (not owned); its width and height are kept
    void addContent (juce::Component& c)
    {
        contents.push_back (&c);
        addAndMakeVisible (c);
        layout();
    }

    void setButtonsCentred (bool centred)
    {
        buttonsCentred = centred;
        layout();
    }

    int numDialogButtons() const { return buttons.size(); }
    Button* dialogButton (int index) const { return buttons[index]; }
    juce::StringArray dialogButtonTexts() const
    {
        juce::StringArray texts;
        for (auto* b : buttons)
            texts.add (b->getButtonText());
        return texts;
    }
    const juce::AttributedString& sentenceText() const { return message; }

    // Sizes the window from its parts (it keeps its centre once shown).
    void layout()
    {
        const auto textWidth = (float) (dialogWidth - 2 * padding - (hasIcon() ? 46 : 0));
        textLayout.createLayout (message, textWidth);
        auto h = titleRowHeight + 1 + 20;
        if (message.getNumAttributes() > 0)
            h += juce::jmax (hasIcon() ? 32 : 0, (int) std::ceil (textLayout.getHeight())) + 16;
        for (auto* c : contents)
            h += c->getHeight() + 12;
        if (! buttons.isEmpty())
            h += 8 + buttonHeight;
        h += 20;
        if (isVisible())
            setBounds (getBounds().withSizeKeepingCentre (dialogWidth, h));
        else
            centreAroundComponent (nullptr, dialogWidth, h);
        resized();
    }

    void resized() override
    {
        auto r = bodyOf (getLocalBounds()).reduced (padding, 0);
        r.removeFromTop (20);
        textArea = {};
        if (message.getNumAttributes() > 0)
        {
            const auto h = juce::jmax (hasIcon() ? 32 : 0, (int) std::ceil (textLayout.getHeight()));
            textArea = r.removeFromTop (h);
            r.removeFromTop (16);
        }
        for (auto* c : contents)
        {
            c->setTopLeftPosition ((getWidth() - c->getWidth()) / 2, r.getY());
            r.removeFromTop (c->getHeight() + 12);
        }
        // the buttons: FL's row at the bottom right (centred when asked), in the order they were added
        auto row = getLocalBounds().removeFromBottom (20 + buttonHeight).withTrimmedBottom (20).reduced (padding, 0);
        auto total = -buttonGap;
        for (auto* b : buttons)
        {
            b->setSize (buttonWidthFor (*b), buttonHeight);
            total += b->getWidth() + buttonGap;
        }
        auto x = buttonsCentred ? row.getX() + (row.getWidth() - total) / 2 : row.getRight() - total;
        for (auto* b : buttons)
        {
            b->setTopLeftPosition (x, row.getY());
            x += b->getWidth() + buttonGap;
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintFrame (g, getLocalBounds().toFloat(), Kind::window);
        paintTitle (g, getLocalBounds(), getName());
        if (textArea.isEmpty())
            return;
        auto text = textArea.toFloat();
        if (hasIcon())
        {
            // LYNX's round sign (as every alert of the app): ? i !
            const auto box = text.removeFromLeft (32.0f).withHeight (32.0f);
            text.removeFromLeft (14.0f);
            const auto tint = icon == juce::MessageBoxIconType::WarningIcon ? theme::colour::warning
                              : icon == juce::MessageBoxIconType::InfoIcon  ? theme::colour::info
                                                                             : theme::colour::accent;
            g.setColour (tint.withAlpha (0.16f));
            g.fillEllipse (box);
            g.setColour (tint);
            g.setFont (theme::font (18.0f, true));
            g.drawText (icon == juce::MessageBoxIconType::QuestionIcon ? "?" : icon == juce::MessageBoxIconType::InfoIcon ? "i" : "!", box,
                        juce::Justification::centred);
            // a one-line sentence sits on the sign's middle
            if (textLayout.getHeight() < 32.0f)
                text = text.withTrimmedTop ((32.0f - textLayout.getHeight()) / 2.0f);
        }
        textLayout.draw (g, text);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto press = [this] (Button* b) {
            if (b == nullptr || ! b->isEnabled())
                return false;
            b->triggerClick();
            return true;
        };
        if (key == juce::KeyPress::returnKey)
            for (auto* b : buttons)
                if (b->primary)
                    return press (b);
        if (key == juce::KeyPress::escapeKey)
        {
            for (auto* b : buttons)
                if (b->cancels)
                    return press (b);
            exitModalState (0);
            return true;
        }
        if (! key.getModifiers().isAnyModifierKeyDown() || key.getModifiers().isShiftDown())
            if (const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter()); c != 0)
                for (auto* b : buttons)
                    if (b->letter() == c || b->aliases.containsChar (c))
                        return press (b);
        return juce::AlertWindow::keyPressed (key);
    }

    void lookAndFeelChanged() override
    {
        juce::AlertWindow::lookAndFeelChanged(); // (JUCE's own layout sizes the window: ours follows)
        if (dialogWidth > 0)
            layout();
    }

private:
    bool hasIcon() const { return icon != juce::MessageBoxIconType::NoIcon; }

    int buttonWidthFor (Button& b) const
    {
        const auto text = juce::GlyphArrangement::getStringWidth (theme::font (theme::type::body, b.primary), b.getButtonText());
        return juce::jmax (minButtonWidth, (int) std::ceil (text) + 32);
    }

    int dialogWidth = 0;
    juce::MessageBoxIconType icon;
    juce::AttributedString message;
    juce::TextLayout textLayout;
    juce::Rectangle<int> textArea;
    juce::OwnedArray<Button> buttons;
    std::vector<juce::Component*> contents;
    bool buttonsCentred = false;
};
} // namespace lynx::ui::dialog
