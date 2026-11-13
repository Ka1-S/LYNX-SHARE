#include "ui/TempoDetectionDialog.h"

#include "core/TempoDetect.h"
#include "core/TempoDialog.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx::ui
{
namespace
{
// FL's dialog (shot PL-8): a darker text panel with a large "?" on its left, then the choices as a column of
// equal buttons centred under it
class TempoDetectionPanel final : public juce::Component
{
public:
    explicit TempoDetectionPanel (TempoDetectionRequest r) : request (std::move (r))
    {
        const auto* sample = request.sample.get();
        if (sample != nullptr && sample->frames() > 0 && sample->sampleRate > 0.0)
        {
            seconds = (double) sample->frames() / sample->sampleRate;
            estimate = tempodetect::detect (sample->leftData(), sample->isStereo() ? sample->rightData() : nullptr, sample->frames(), sample->sampleRate);
        }
        text.setReadOnly (true);
        text.setMultiLine (true);
        text.setCaretVisible (false);
        text.setScrollbarsShown (false);
        text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        text.setFont (juce::Font (theme::font (15.0f)));
        text.setIndents (0, 0);
        juce::String body = tr ("Please select the tempo range (in BPM) this sample is the most likely to be in, or choose to keep the tempo this sample is currently assigned to.");
        if (! request.hasTempoInfo)
            body << "\n\n"
                 << (estimate.bpm > 0.0 ? tr ("This sample did not come with tempo information, so it is advised that you select a range if the estimated tempo ({0} BPM, {1} beats) doesn't look correct.",
                                              { juce::String (tempodlg::bpmText (estimate.bpm)), juce::String (tempodlg::bpmText (estimate.beats)) })
                                        : tr ("This sample did not come with tempo information, so it is advised that you select a range."));
        body << "\n\n" << tr ("If you simply wish to synchronize this sample with the project, select the project's tempo.") << "\n\n"
             << tr ("If you believe this sample should not be tempo-synchronized, select no tempo.");
        text.setText (body, false);
        addAndMakeVisible (text);

        for (const auto& b : tempodlg::buttons (estimate.bpm, request.projectBpm))
        {
            auto* button = buttons.add (new juce::TextButton (label (b)));
            button->onClick = [this, b] { choose (b); };
            addAndMakeVisible (button);
            choices.push_back (b);
        }
        setSize (552, panelHeight + 14 + (int) choices.size() * 37 + 8);
    }

    static juce::String label (const tempodlg::Button& b)
    {
        switch (b.choice)
        {
            case tempodlg::Choice::estimated: return tr ("{0} (estimated)", { juce::String (tempodlg::bpmText (b.bpm)) });
            case tempodlg::Choice::range: return tr ("{0} to {1}", { juce::String (tempodlg::bpmText (b.low)), juce::String (tempodlg::bpmText (b.high)) });
            case tempodlg::Choice::typeBpm: return tr ("Type in (BPM)");
            case tempodlg::Choice::typeBeats: return tr ("Type in (beats)");
            case tempodlg::Choice::project: return tr ("{0} (project)", { juce::String (tempodlg::bpmText (b.bpm)) });
            case tempodlg::Choice::noTempo: return tr ("No tempo");
        }
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg3);
        g.fillRect (panel);
        g.setColour (theme::colour::textSecondary.withAlpha (0.55f));
        g.setFont (theme::font (64.0f, true));
        g.drawText ("?", panel.withWidth (80).withTrimmedTop (8).removeFromTop (80), juce::Justification::centred, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        panel = r.removeFromTop (panelHeight);
        text.setBounds (panel.withTrimmedLeft (88).withTrimmedRight (16).withTrimmedTop (18).withTrimmedBottom (8));
        r.removeFromTop (14);
        for (auto* b : buttons)
        {
            b->setBounds (r.removeFromTop (31).withSizeKeepingCentre (104, 31).reduced (0, 2));
            r.removeFromTop (6);
        }
    }

    // a click: what the choice gives the sample; returns the button's text
    juce::String press (int index)
    {
        if (index < 0 || index >= (int) choices.size())
            return {};
        const auto b = choices[(std::size_t) index];
        if (b.choice != tempodlg::Choice::typeBpm && b.choice != tempodlg::Choice::typeBeats)
            choose (b);
        return label (b);
    }

private:
    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    void finish (double beats)
    {
        if (request.apply != nullptr)
            request.apply (beats);
        close();
    }

    void choose (const tempodlg::Button& b)
    {
        switch (b.choice)
        {
            case tempodlg::Choice::estimated: finish (estimate.beats); return;
            case tempodlg::Choice::project: finish (tempodlg::beatsAt (seconds, request.projectBpm)); return;
            case tempodlg::Choice::noTempo: finish (0.0); return;
            case tempodlg::Choice::range:
            {
                const auto* sample = request.sample.get();
                if (sample == nullptr)
                    return;
                tempodetect::Options options;
                options.minBpm = b.low;
                options.maxBpm = b.high;
                const auto found = tempodetect::detect (sample->leftData(), sample->isStereo() ? sample->rightData() : nullptr, sample->frames(),
                                                        sample->sampleRate, options);
                if (found.bpm <= 0.0)
                {
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Detect tempo"), tr ("No steady tempo was found in this sample."));
                    return;
                }
                finish (found.beats);
                return;
            }
            case tempodlg::Choice::typeBpm:
            case tempodlg::Choice::typeBeats:
            {
                const auto bpm = b.choice == tempodlg::Choice::typeBpm;
                auto* window = new juce::AlertWindow (bpm ? tr ("Type in (BPM)") : tr ("Type in (beats)"), bpm ? tr ("Tempo (BPM)") : tr ("Length (beats)"),
                                                      juce::MessageBoxIconType::NoIcon, this);
                const auto now = bpm ? (estimate.bpm > 0.0 ? estimate.bpm : request.projectBpm) : tempodlg::beatsAt (seconds, estimate.bpm > 0.0 ? estimate.bpm : request.projectBpm);
                window->addTextEditor ("value", juce::String (tempodlg::bpmText (now)));
                window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
                window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
                window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<TempoDetectionPanel> (this), window, bpm] (int result) {
                                             if (result != 1 || safe == nullptr)
                                                 return;
                                             const auto v = window->getTextEditorContents ("value").retainCharacters ("0123456789.,").replaceCharacter (',', '.').getDoubleValue();
                                             if (v <= 0.0)
                                                 return;
                                             safe->finish (bpm ? tempodlg::beatsAt (safe->seconds, v) : v);
                                         }),
                                         true);
                return;
            }
        }
    }

    TempoDetectionRequest request;
    tempodetect::Result estimate;
    double seconds = 0.0;
    juce::TextEditor text;
    juce::OwnedArray<juce::TextButton> buttons;
    std::vector<tempodlg::Button> choices;
    juce::Rectangle<int> panel;
    static constexpr int panelHeight = 300; // (FL's text panel)
};
} // namespace

juce::String tempoDetectionTitle (const juce::String& name)
{
    return tr ("Tempo detection") + " - " + name;
}

std::unique_ptr<juce::Component> makeTempoDetectionPanel (TempoDetectionRequest request)
{
    return std::make_unique<TempoDetectionPanel> (std::move (request));
}

juce::String pressTempoDetectionButton (juce::Component& panel, int index)
{
    auto* p = dynamic_cast<TempoDetectionPanel*> (&panel);
    return p != nullptr ? p->press (index) : juce::String();
}

void showTempoDetectionDialog (TempoDetectionRequest request)
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = tempoDetectionTitle (request.name);
    o.content.setOwned (makeTempoDetectionPanel (std::move (request)).release());
    o.dialogBackgroundColour = theme::colour::bg2;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    if (auto* w = o.launchAsync())
        keepAboveMainWindow (*w);
}
} // namespace lynx::ui
