#include "ui/JsfxPanel.h"

#include "engine/JsfxFormat.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr int panelWidth = 480, rowHeight = 30, headerHeight = 54, noteHeight = 22, pad = 12, maxVisibleRows = 18;

// One slider's row: its name, and a bar (a number) or a menu (an enum, a file slider)
class Row final : public juce::Component
{
public:
    Row (jsfx::Instance& i, juce::AudioProcessorParameter& p, const jsfx::Slider& s) : instance (i), parameter (p), slider (s)
    {
        using namespace theme::colour;
        if (slider.isEnum || slider.isFile)
        {
            menu = std::make_unique<juce::ComboBox>();
            for (int k = 0; k < slider.choices.size(); ++k)
                menu->addItem (slider.choices[k].isNotEmpty() ? slider.choices[k] : juce::String (k), k + 1);
            if (slider.choices.isEmpty())
                menu->setTextWhenNoChoicesAvailable (slider.isFile ? tr ("(no files)") : juce::String());
            menu->onChange = [this] {
                if (updating) return;
                const auto k = menu->getSelectedItemIndex();
                if (k >= 0)
                {
                    parameter.beginChangeGesture();
                    jsfx::setSliderValue (instance, slider.index, slider.minimum + k);
                    parameter.endChangeGesture();
                }
            };
            addAndMakeVisible (*menu);
        }
        else
        {
            // (a bar with its value written inside it, editable by a double-click on the text... as the generic panel)
            bar = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
            bar->setRange (0.0, 1.0, 0.0);
            bar->setColour (juce::Slider::trackColourId, accent.withAlpha (0.45f));
            bar->setColour (juce::Slider::backgroundColourId, bg0);
            bar->setColour (juce::Slider::textBoxTextColourId, textPrimary);
            bar->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            bar->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            bar->setColour (juce::Slider::textBoxHighlightColourId, accent.withAlpha (0.4f));
            bar->setTextValueSuffix ({});
            bar->textFromValueFunction = [this] (double v) { return parameter.getText ((float) v, 32); };
            bar->valueFromTextFunction = [this] (const juce::String& t) { return (double) parameter.getValueForText (t); };
            bar->setDoubleClickReturnValue (true, (double) parameter.getDefaultValue());
            bar->setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::ctrlModifier); // (Ctrl: fine steps)
            bar->onDragStart = [this] { parameter.beginChangeGesture(); };
            bar->onDragEnd = [this] { parameter.endChangeGesture(); };
            bar->onValueChange = [this] {
                if (! updating)
                    parameter.setValueNotifyingHost ((float) bar->getValue());
            };
            addAndMakeVisible (*bar);
        }
        setTooltip();
        refresh();
        if (bar != nullptr)
            bar->updateText(); // (a value that did not move keeps JUCE's own number format otherwise)
    }

    // the script's value now (it may change it itself); true when it moved
    bool refresh()
    {
        const auto v = parameter.getValue();
        if (std::abs (v - shown) < 1.0e-7f)
            return false;
        shown = v;
        const juce::ScopedValueSetter<bool> quiet (updating, true);
        if (menu != nullptr)
            menu->setSelectedItemIndex ((int) std::lround (jsfx::sliderValue (instance, slider.index) - slider.minimum), juce::dontSendNotification);
        if (bar != nullptr)
            bar->setValue (v, juce::dontSendNotification);
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body));
        g.drawFittedText (slider.name, getLocalBounds().withWidth (labelWidth()).withTrimmedLeft (pad), juce::Justification::centredLeft, 1, 0.85f);
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedLeft (labelWidth()).withTrimmedRight (pad).reduced (0, 4);
        if (menu != nullptr) menu->setBounds (area);
        if (bar != nullptr) bar->setBounds (area);
    }

    const jsfx::Slider slider;

private:
    jsfx::Instance& instance;
    juce::AudioProcessorParameter& parameter;
    std::unique_ptr<juce::Slider> bar;
    std::unique_ptr<juce::ComboBox> menu;
    float shown = -1.0f;
    bool updating = false;

    int labelWidth() const { return juce::jmin (200, getWidth() * 2 / 5); }
    void setTooltip()
    {
        const auto tip = tr ("slider{0} of the script", { juce::String (slider.index + 1) });
        if (menu != nullptr) menu->setTooltip (tip);
        if (bar != nullptr) bar->setTooltip (tip + " · " + tr ("[double-click] default, [Ctrl] drag: fine"));
    }
};

class Panel final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    Panel (juce::AudioProcessor& p, jsfx::Instance& i) : AudioProcessorEditor (p), instance (i)
    {
        setOpaque (true);
        const auto& params = p.getParameters();
        for (const auto& s : jsfx::sliders (instance))
            if (juce::isPositiveAndBelow (s.parameter, params.size()))
                rows.push_back (std::make_unique<Row> (instance, *params[s.parameter], s));
        for (auto& r : rows)
            content.addChildComponent (*r);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        layoutRows (true);
    }

    ~Panel() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg2);
        auto header = getLocalBounds().removeFromTop (headerHeight);
        g.setColour (bg3);
        g.fillRect (header);
        g.setColour (border);
        g.fillRect (header.removeFromBottom (1));
        auto text = header.reduced (pad, 8);
        // the script's delay (pdc_delay), when it has one
        if (const auto pdc = jsfx::latency (instance); pdc > 0)
        {
            const auto badge = tr ("PDC {0} smp", { juce::String (pdc) });
            const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::small), badge)) + 14;
            auto b = text.removeFromRight (w).withSizeKeepingCentre (w, 20).toFloat();
            g.setColour (bg4);
            g.fillRoundedRectangle (b, theme::radius::md);
            g.setColour (textSecondary);
            g.setFont (theme::font (theme::type::small));
            g.drawText (badge, b, juce::Justification::centred);
        }
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawFittedText (jsfx::scriptName (instance), text.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (textSecondary);
        g.setFont (theme::font (theme::type::small));
        const auto author = jsfx::scriptAuthor (instance);
        g.drawFittedText ((author.isNotEmpty() ? author + "  ·  " : juce::String()) + "JSFX  ·  " + jsfx::relativeId (instance), text,
                          juce::Justification::centredLeft, 1, 0.8f);
        // one line under the header: the script changed since the project was saved / it has a display LYNX does not draw yet
        if (const auto note = noteText(); note.isNotEmpty())
        {
            auto line = getLocalBounds().withTrimmedTop (headerHeight).removeFromTop (noteHeight).reduced (pad, 0);
            g.setColour (jsfx::scriptChanged (instance) ? warning : textSecondary);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (note, line, juce::Justification::centredLeft, 1, 0.8f);
        }
        if (rows.empty())
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::body));
            g.drawText (tr ("This script has no sliders."), getLocalBounds().withTrimmedTop (top()), juce::Justification::centred);
        }
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds().withTrimmedTop (top()));
        content.setSize (viewport.getMaximumVisibleWidth(), content.getHeight());
        for (auto& r : rows)
            r->setSize (content.getWidth(), rowHeight);
        layoutRows (false);
    }

private:
    jsfx::Instance& instance;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<bool> visible;
    int pdcShown = -1;

    juce::String noteText() const
    {
        if (jsfx::scriptChanged (instance))
            return tr ("The script changed since this project was saved: its settings may not fit.");
        if (jsfx::hasGfx (instance))
            return tr ("This script draws its own display; LYNX SHARE shows its sliders.");
        return {};
    }
    int top() const { return headerHeight + (noteText().isNotEmpty() ? noteHeight : 0) + 4; }

    // the visible sliders one under the other; the window is as tall as they are (up to maxVisibleRows, then it scrolls)
    void layoutRows (bool sizeWindow)
    {
        visible.resize (rows.size());
        auto y = 0, shownRows = 0;
        for (std::size_t k = 0; k < rows.size(); ++k)
        {
            const auto on = jsfx::sliderVisible (instance, rows[k]->slider.index);
            visible[k] = on;
            rows[k]->setVisible (on);
            if (on)
            {
                rows[k]->setBounds (0, y, content.getWidth(), rowHeight);
                y += rowHeight;
                ++shownRows;
            }
        }
        content.setSize (juce::jmax (1, content.getWidth()), juce::jmax (1, y + 6));
        if (sizeWindow)
            setSize (panelWidth, top() + juce::jmax (2, juce::jmin (shownRows, maxVisibleRows)) * rowHeight + 10);
    }

    void visibilityChanged() override { isShowing() ? startTimerHz (15) : stopTimer(); }
    void parentHierarchyChanged() override { isShowing() ? startTimerHz (15) : stopTimer(); }

    // (only while the window is on screen) the values the script or the automation moved, sliders it showed or hid,
    // its delay
    void timerCallback() override
    {
        for (auto& r : rows)
            r->refresh();
        auto layout = false;
        for (std::size_t k = 0; k < rows.size(); ++k)
            layout = layout || jsfx::sliderVisible (instance, rows[k]->slider.index) != visible[k];
        if (layout)
            layoutRows (false);
        if (const auto pdc = jsfx::latency (instance); pdc != pdcShown)
        {
            pdcShown = pdc;
            repaint (0, 0, getWidth(), headerHeight);
        }
    }
};
} // namespace

juce::AudioProcessorEditor* makeJsfxPanel (juce::AudioProcessor& processor)
{
    if (auto* instance = jsfx::asJsfx (&processor))
        return new Panel (processor, *instance);
    return nullptr;
}
} // namespace lynx
