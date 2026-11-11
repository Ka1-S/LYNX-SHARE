#pragma once

// The form of a script's ScriptDialog (core/ScriptDialog.h), shared by the audio editor's scripts (Edison, round 167c)
// and the Piano roll's scripts (round 190as). FL uses one form for both ("Configure script"): the controls the script
// asks for, Preview (automatic, when a control changes), Regenerate, Reset, Edit script and Accept.

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ScriptDialog.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
// what differs between the hosts: the accept button's text (Edison: "Run"; the Piano roll, FL's own word: "Accept"),
// whether Preview starts ticked, and `dismissed`: called when the panel goes away without being accepted
struct ScriptPanelOptions
{
    juce::String acceptText;
    bool previewOn = false;
    std::function<void()> dismissed;
};

class ScriptPanel final : public juce::Component,
                          private juce::Timer
{
public:
    using Answer = std::function<void (const scriptform::Form&)>;

    using Options = ScriptPanelOptions;

    ScriptPanel (scriptform::Form f, Answer runAction, Answer previewAction, std::function<void()> editAction, Options panelOptions = {})
        : form (std::move (f)), defaults (form), run (std::move (runAction)), preview (std::move (previewAction)), edit (std::move (editAction)),
          options (std::move (panelOptions))
    {
        header.setText (juce::String::fromUTF8 (form.title.c_str()), juce::dontSendNotification);
        header.setFont (theme::font (theme::type::title));
        header.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (header);
        description.setText (juce::String::fromUTF8 (form.description.c_str()), juce::dontSendNotification);
        description.setFont (theme::font (theme::type::body));
        description.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        description.setJustificationType (juce::Justification::topLeft);
        description.setMinimumHorizontalScale (1.0f);
        description.setVisible (! form.description.empty());
        addChildComponent (description);

        using T = scriptform::Control::Type;
        for (std::size_t i = 0; i < form.controls.size(); ++i)
        {
            const auto& c = form.controls[i];
            auto* label = labels.add (new juce::Label ({}, juce::String::fromUTF8 (c.name.c_str())));
            label->setFont (theme::font (theme::type::body));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            label->setTooltip (juce::String::fromUTF8 (c.hint.c_str()));
            addAndMakeVisible (label);
            std::unique_ptr<juce::Component> editor;
            switch (c.type)
            {
                case T::value:
                case T::knob:
                case T::knobInt:
                {
                    auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
                    const auto lo = std::min (c.minimum, c.maximum), hi = std::max (c.minimum, c.maximum);
                    s->setRange (lo, hi > lo ? hi : lo + 1.0, c.type == T::knobInt ? 1.0 : 0.0);
                    s->setNumDecimalPlacesToDisplay (c.type == T::knobInt ? 0 : 3);
                    s->setValue (c.value, juce::dontSendNotification);
                    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 22);
                    s->onValueChange = [this, i, p = s.get()] {
                        form.controls[i].value = p->getValue();
                        changed();
                    };
                    editor = std::move (s);
                    break;
                }
                case T::combo:
                {
                    auto b = std::make_unique<juce::ComboBox>();
                    for (std::size_t o = 0; o < c.options.size(); ++o)
                        b->addItem (juce::String::fromUTF8 (c.options[o].c_str()), (int) o + 1);
                    b->setSelectedItemIndex ((int) c.value, juce::dontSendNotification);
                    b->onChange = [this, i, p = b.get()] {
                        form.controls[i].value = juce::jmax (0, p->getSelectedItemIndex());
                        changed();
                    };
                    editor = std::move (b);
                    break;
                }
                case T::checkbox:
                {
                    auto b = std::make_unique<juce::ToggleButton>();
                    b->setToggleState (c.value != 0.0, juce::dontSendNotification);
                    b->onClick = [this, i, p = b.get()] {
                        form.controls[i].value = p->getToggleState() ? 1.0 : 0.0;
                        changed();
                    };
                    editor = std::move (b);
                    break;
                }
                case T::text:
                {
                    auto t = std::make_unique<juce::TextEditor>();
                    t->setText (juce::String::fromUTF8 (c.text.c_str()), false);
                    t->onTextChange = [this, i, p = t.get()] {
                        form.controls[i].text = p->getText().toStdString();
                        changed();
                    };
                    editor = std::move (t);
                    break;
                }
                case T::surface:
                {
                    auto l = std::make_unique<juce::Label> (juce::String {}, tr ("(Control Surface presets are not available in LYNX SHARE)"));
                    l->setFont (theme::font (theme::type::caption));
                    l->setColour (juce::Label::textColourId, theme::colour::textDisabled);
                    editor = std::move (l);
                    break;
                }
            }
            addAndMakeVisible (*editor);
            editors.push_back (std::move (editor));
        }

        previewToggle.setButtonText (tr ("Preview"));
        previewToggle.setTooltip (tr ("Automatic preview when controls are modified"));
        previewToggle.onClick = [this] {
            if (previewToggle.getToggleState())
                startTimer (1);
            else
                stopTimer();
        };
        regenerateButton.setButtonText (tr ("Regenerate"));
        regenerateButton.onClick = [this] { startTimer (1); };
        resetButton.setButtonText (tr ("Reset"));
        resetButton.onClick = [this] { reset(); };
        editButton.setButtonText (tr ("Edit script"));
        editButton.onClick = [this] {
            if (edit)
                edit();
        };
        runButton.setButtonText (options.acceptText.isNotEmpty() ? options.acceptText : tr ("Run"));
        runButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        runButton.onClick = [this] {
            stopTimer();
            accepted = true;
            const auto f = form;
            auto action = run;
            closeToolWindowOf (*this); // (deletes this)
            if (action)
                action (f);
        };
        cancelButton.setButtonText (tr ("Cancel"));
        cancelButton.onClick = [this] { closeToolWindowOf (*this); };
        for (juce::Component* b : { (juce::Component*) &previewToggle, (juce::Component*) &regenerateButton, (juce::Component*) &resetButton,
                                    (juce::Component*) &editButton, (juce::Component*) &runButton, (juce::Component*) &cancelButton })
            addAndMakeVisible (*b);
        editButton.setVisible (edit != nullptr);
        previewToggle.setVisible (preview != nullptr);
        regenerateButton.setVisible (preview != nullptr);

        if (preview != nullptr && options.previewOn)
        {
            previewToggle.setToggleState (true, juce::dontSendNotification);
            startTimer (1); // (the first preview: the script with its starting values)
        }

        // (the description takes the lines it needs: FL's scripts explain themselves in a paragraph)
        if (! form.description.empty())
        {
            juce::AttributedString text;
            text.setWordWrap (juce::AttributedString::byWord);
            text.append (description.getText(), description.getFont());
            juce::TextLayout layout;
            layout.createLayout (text, (float) (500 - 2 * theme::space::lg - 8));
            descriptionHeight = juce::jlimit (22, 200, (int) std::ceil (layout.getHeight()) + 6);
        }
        setSize (500, 24 + 34 + (form.description.empty() ? 0 : descriptionHeight + 8) + (int) form.controls.size() * 36 + 52);
    }

    ~ScriptPanel() override
    {
        if (! accepted && options.dismissed)
            options.dismissed();
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (theme::space::lg);
        header.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        if (description.isVisible())
        {
            description.setBounds (area.removeFromTop (descriptionHeight));
            area.removeFromTop (8);
        }
        for (std::size_t i = 0; i < editors.size(); ++i)
        {
            auto row = area.removeFromTop (28);
            labels[(int) i]->setBounds (row.removeFromLeft (150));
            editors[i]->setBounds (row);
            area.removeFromTop (8);
        }
        auto buttons = area.removeFromBottom (30);
        previewToggle.setBounds (buttons.removeFromLeft (84));
        regenerateButton.setBounds (buttons.removeFromLeft (92));
        cancelButton.setBounds (buttons.removeFromRight (70));
        buttons.removeFromRight (6);
        runButton.setBounds (buttons.removeFromRight (70));
        buttons.removeFromRight (6);
        editButton.setBounds (buttons.removeFromRight (82));
        buttons.removeFromRight (6);
        resetButton.setBounds (buttons.removeFromRight (64));
    }

private:
    void changed()
    {
        if (previewToggle.getToggleState())
            startTimer (250); // (a drag previews once it pauses)
    }

    void timerCallback() override
    {
        stopTimer();
        if (preview)
            preview (form);
    }

    void reset()
    {
        using T = scriptform::Control::Type;
        form = defaults;
        for (std::size_t i = 0; i < form.controls.size(); ++i)
        {
            const auto& c = form.controls[i];
            auto* e = editors[i].get();
            if (auto* s = dynamic_cast<juce::Slider*> (e))
                s->setValue (c.value, juce::dontSendNotification);
            else if (auto* b = dynamic_cast<juce::ComboBox*> (e))
                b->setSelectedItemIndex ((int) c.value, juce::dontSendNotification);
            else if (auto* t = dynamic_cast<juce::ToggleButton*> (e))
                t->setToggleState (c.value != 0.0, juce::dontSendNotification);
            else if (auto* x = dynamic_cast<juce::TextEditor*> (e); x != nullptr && c.type == T::text)
                x->setText (juce::String::fromUTF8 (c.text.c_str()), false);
        }
        changed();
    }

    scriptform::Form form, defaults;
    Answer run, preview;
    std::function<void()> edit;
    Options options;
    bool accepted = false;
    juce::Label header, description;
    int descriptionHeight = 36;
    juce::OwnedArray<juce::Label> labels;
    std::vector<std::unique_ptr<juce::Component>> editors;
    juce::ToggleButton previewToggle;
    juce::TextButton regenerateButton, resetButton, editButton, runButton, cancelButton;
};
} // namespace lynx
