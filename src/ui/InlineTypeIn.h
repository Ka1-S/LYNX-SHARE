#pragma once

// FL's in-place type-in (TNameEditForm): a text box with a hint row "✓ ✕ <caption>" under it, over the window it was
// asked in. Round 174d made it for the video player's times; round 190ri shares it for the values asked from a menu
// (Transpose...).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/Theme.h"

namespace lynx::ui
{
// ---------------------------------------------------------------------------------------------------------------
// FL's in-place type-in (round 174d, FL 2026 on screen): a right click on a time opens a text box over the panel's
// left edge, prefilled with the label's text (selected), and under it a hint row "✓ ✕ <caption>". Enter or ✓ accepts;
// Esc, ✕ or a click elsewhere cancels.
class InlineTypeIn final : public juce::Component, private juce::FocusChangeListener
{
public:
    InlineTypeIn (const juce::String& text, const juce::String& caption) : hint (caption)
    {
        editor.setFont (theme::font (theme::type::display));
        editor.setText (text, false);
        editor.setColour (juce::TextEditor::backgroundColourId, theme::colour::bg0);
        editor.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        editor.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff1e6fd9));
        editor.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        editor.setIndents (4, 2);
        editor.onReturnKey = [this] { finish (true); };
        editor.onEscapeKey = [this] { finish (false); };
        addAndMakeVisible (editor);
        setMouseClickGrabsKeyboardFocus (false); // (a click on ✓ / ✕ must not take the focus from the box first)
        juce::Desktop::getInstance().addFocusChangeListener (this);
    }
    ~InlineTypeIn() override { juce::Desktop::getInstance().removeFocusChangeListener (this); }

    static constexpr int boxHeight = 28, hintHeight = 22;

    // (round 190ri) what may be typed: at most `maxLength` characters of `allowed` (empty: any)
    void restrict (int maxLength, const juce::String& allowed) { editor.setInputRestrictions (maxLength, allowed); }

    void start()
    {
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

    void resized() override { editor.setBounds (0, 0, getWidth(), boxHeight); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (boxHeight).toFloat();
        g.setColour (theme::colour::bg0.withAlpha (0.92f));
        g.fillRect (r);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textSecondary);
        g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93"), tickArea().toFloat(), juce::Justification::centred);
        g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x95"), crossArea().toFloat(), juce::Justification::centred);
        g.drawText (hint, r.withTrimmedLeft ((float) crossArea().getRight() + 8.0f), juce::Justification::centredLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (tickArea().contains (e.getPosition()))
            finish (true);
        else if (crossArea().contains (e.getPosition()))
            finish (false);
    }

private:
    juce::Rectangle<int> tickArea() const { return { 4, boxHeight, 18, hintHeight }; }
    juce::Rectangle<int> crossArea() const { return { 22, boxHeight, 18, hintHeight }; }

    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused != &editor && focused != this)
            finish (false);
    }

    void finish (bool ok)
    {
        if (done)
            return;
        done = true;
        if (onDone)
            onDone (ok, editor.getText()); // (the owner removes this box asynchronously)
    }

public:
    std::function<void (bool ok, const juce::String& text)> onDone;

private:
    juce::TextEditor editor;
    juce::String hint;
    bool done = false;
};

// Round 190ri (FL 2026 live, Patterns › Transpose...: FL's TNameEditForm "Semitones" at 39,190-241,241 with the mouse at
// 231,231; the F2 name box the same way): the box over the window under the mouse, its bottom-right corner 10 px past the
// mouse, kept inside the window. `accept` gets the text on Enter or ✓; Esc, ✕ or a click elsewhere cancel. One at a time.
struct InlineTypeInHolder final : juce::DeletedAtShutdown
{
    std::unique_ptr<InlineTypeIn> box;
    static InlineTypeInHolder& get()
    {
        static InlineTypeInHolder* holder = nullptr;
        if (holder == nullptr)
            holder = new InlineTypeInHolder();
        return *holder;
    }
};

inline InlineTypeIn* openInlineTypeIn (const juce::String& text, const juce::String& caption, std::function<void (const juce::String&)> accept,
                                       int width = 202, juce::Component* host = nullptr)
{
    auto& holder = InlineTypeInHolder::get();
    if (holder.box != nullptr && holder.box->isShowing())
        return holder.box.get();
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    if (host == nullptr)
        if (auto* under = juce::Desktop::getInstance().findComponentAt (mouse))
            host = under->getTopLevelComponent();
    if (host == nullptr && juce::TopLevelWindow::getNumTopLevelWindows() > 0)
        host = juce::TopLevelWindow::getTopLevelWindow (0);
    if (host == nullptr)
        return nullptr;
    holder.box = std::make_unique<InlineTypeIn> (text, caption);
    auto* box = holder.box.get();
    box->onDone = [box, accept = std::move (accept)] (bool ok, const juce::String& typed) {
        juce::MessageManager::callAsync ([box, ok, typed, accept] {
            auto& h = InlineTypeInHolder::get();
            if (h.box.get() == box)
                h.box.reset();
            if (ok && accept)
                accept (typed);
        });
    };
    const auto at = host->getLocalPoint (nullptr, mouse);
    const auto height = InlineTypeIn::boxHeight + InlineTypeIn::hintHeight;
    box->setBounds (juce::Rectangle<int> (at.x + 10 - width, at.y + 10 - height, width, height).constrainedWithin (host->getLocalBounds()));
    host->addAndMakeVisible (*box);
    box->toFront (false);
    box->start();
    return box;
}
} // namespace lynx::ui
