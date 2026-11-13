#include "ui/HistoryPanel.h"

#include "core/FlSettings.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class HistoryPanel final : public juce::Component,
                           private juce::ListBoxModel,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    explicit HistoryPanel (AudioEngine& e) : engine (e)
    {
        title.setText (tr ("Undo history"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (note);

        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);
        engine.addChangeListener (this);
        refresh();
        setSize (340, 420);
        startTimer (300); // (Settings › General › "Show recent changes at the top" switched while this is open)
    }

    ~HistoryPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        title.setBounds (area.removeFromTop (24));
        note.setBounds (area.removeFromTop (22));
        area.removeFromTop (4);
        list.setBounds (area);
    }

private:
    void refresh()
    {
        labels.clear();
        for (int i = 0; i < engine.getHistorySize(); ++i)
        {
            const auto l = engine.getHistoryLabel (i);
            labels.add (tr (l.text, { juce::String::fromUTF8 (l.arg.c_str()) }));
        }

        position = engine.getHistoryPosition();
        // FL's General › Undo history › "Show recent changes at the top" (round 190bx, core/FlSettings.h)
        newestFirst = flsettings::options().historyNewestFirst.load();
        note.setText (newestFirst ? tr ("Click a step to go back (or forward) to it. Newest at the top.")
                                  : tr ("Click a step to go back (or forward) to it. Newest at the bottom."),
                      juce::dontSendNotification);
        list.updateContent();
        list.scrollToEnsureRowIsOnscreen (rowOf (position));
        list.repaint();
    }

    int stepOf (int row) const noexcept { return flsettings::historyStepForRow (row, labels.size(), newestFirst); }
    int rowOf (int step) const noexcept { return flsettings::historyRowForStep (step, labels.size(), newestFirst); }

    void timerCallback() override
    {
        if (newestFirst != flsettings::options().historyNewestFirst.load())
            refresh();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (engine.getHistorySize() != labels.size() || engine.getHistoryPosition() != position)
            refresh();
        else if (labels.size() > 0) // the newest step may have been extended by a drag
        {
            const auto l = engine.getHistoryLabel (labels.size() - 1);
            labels.set (labels.size() - 1, tr (l.text, { juce::String::fromUTF8 (l.arg.c_str()) }));
            list.repaintRow (rowOf (labels.size() - 1));
        }
    }

    int getNumRows() override { return labels.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
    {
        const auto step = stepOf (row);
        const auto current = step == position;
        const auto future = step > position;

        if (current)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
            g.setColour (theme::colour::accent);
            g.fillRect (0, 2, 3, height - 4);
        }

        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textDisabled);
        g.drawText (juce::String (step), 8, 0, 28, height, juce::Justification::centredRight);
        g.setFont (theme::font (theme::type::body, current));
        g.setColour (future ? theme::colour::textDisabled : theme::colour::textPrimary);
        g.drawText (labels[step], 44, 0, width - 50, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override { engine.jumpToHistory (stepOf (row)); }

    AudioEngine& engine;
    juce::Label title, note;
    juce::ListBox list;
    juce::StringArray labels;
    int position = 0;
    bool newestFirst = false;
};
} // namespace

std::unique_ptr<juce::Component> makeHistoryPanel (AudioEngine& engine)
{
    return std::make_unique<HistoryPanel> (engine);
}
} // namespace lynx
