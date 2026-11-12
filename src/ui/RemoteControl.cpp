#include "ui/RemoteControl.h"

#include "core/LinkFormula.h"
#include "core/MidiControls.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
// FL's form is 256 x 490 with its own 26 px caption bar; LYNX's window has the system title bar, so the panel is the
// form's client area below the caption, enlarged for LYNX's 13 px text.
constexpr float formScale = 1.3f;
constexpr int captionHeight = 26;

juce::Rectangle<int> form (int x, int y, int w, int h)
{
    return juce::Rectangle<float> ((float) x * formScale, (float) (y - captionHeight) * formScale, (float) w * formScale, (float) h * formScale)
        .toNearestInt();
}

const juce::String noneText = juce::String::fromUTF8 ("\xe2\x8b\xaf"); // FL's number wheels show "⋯" for "none"

// FL's TVectorDigiWheel: a number dragged up and down, one step per wheel notch; [RMB] its menu when it has one.
class WheelBox final : public juce::Component
{
public:
    WheelBox (ui::ValueSpec s, std::function<double()> get, std::function<void (double)> set)
        : spec (ui::digitWheel (std::move (s))), getter (std::move (get)), setter (std::move (set))
    {
        setRepaintsOnMouseActivity (true);
        ui::setHint (*this, spec.name, false, false);
    }
    std::function<void()> onMenu;

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::radius::sm);
        g.setColour (isMouseOverOrDragging() ? theme::colour::accent : theme::colour::borderStrong);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), theme::radius::sm, 1.0f);
        g.setColour (isEnabled() ? theme::colour::textPrimary : theme::colour::textDisabled);
        g.setFont (theme::font (theme::type::body));
        g.drawText (spec.text (getter()), getLocalBounds().reduced (2, 0), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        if (ui::isResetClick (e))
        {
            setter (spec.defaultValue);
            repaint();
            return;
        }
        drag.begin (e, getter());
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! drag.isActive())
            return;
        setter (drag.update (e, spec));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { drag.end(); }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            ui::typeValue (*this, spec, getter(), [this] (double v) {
                setter (v);
                repaint();
            });
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (const auto v = ui::wheelValue (getter(), e, wheel, spec))
        {
            setter (*v);
            repaint();
        }
    }

private:
    ui::ValueSpec spec;
    std::function<double()> getter;
    std::function<void (double)> setter;
    ui::ValueDrag drag;
};

// FL's FGraph: the mapping's curve, input left to right, output bottom to top, over a grid of quarters.
class MappingGraph final : public juce::Component
{
public:
    linkformula::Formula formula;
    juce::Colour curve = theme::colour::textSecondary;

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (area, theme::radius::sm);
        g.setColour (theme::colour::border);
        for (int i = 1; i < 4; ++i)
        {
            const auto x = area.getX() + area.getWidth() * (float) i / 4.0f;
            const auto y = area.getY() + area.getHeight() * (float) i / 4.0f;
            g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f);
            g.drawLine (area.getX(), y, area.getRight(), y, 1.0f);
        }
        const auto inner = area.reduced (3.0f);
        juce::Path path;
        const auto steps = juce::jmax (8, (int) inner.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const auto in = (float) i / (float) steps;
            const auto out = formula.map (in);
            const auto x = inner.getX() + inner.getWidth() * in;
            const auto y = inner.getBottom() - inner.getHeight() * out;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (curve);
        g.strokePath (path, juce::PathStrokeType (1.6f));
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (area, theme::radius::sm, 1.0f);
    }
};

class RemoteControlPanel final : public juce::Component, private juce::Timer
{
public:
    RemoteControlPanel (AudioEngine& e, std::vector<AutomationTarget> t, bool isGlobal, std::function<void()> close)
        : engine (e), targets (std::move (t)), global (isGlobal), onClose (std::move (close))
    {
        if (targets.empty())
            targets.push_back ({});

        // --- "Current parameter" / "Current assignment"
        ui::setHint (eventSelect, tr ("Current parameter"));
        eventSelect.onChange = [this] {
            if (eventSelect.getSelectedId() - 1 != index)
                showTarget (eventSelect.getSelectedId() - 1);
        };
        addAndMakeVisible (eventSelect);
        ui::setHint (entrySelect, tr ("Current assignment"));
        entrySelect.onChange = [this] {
            if (entrySelect.getSelectedId() - 1 != entry)
            {
                store();
                showEntry (entrySelect.getSelectedId() - 1);
            }
        };
        if (! global)
            addAndMakeVisible (entrySelect);

        // --- MIDI controller
        for (auto* l : { &portLabel, &channelLabel, &ctrlLabel, &timeLabel })
        {
            l->setFont (theme::font (theme::type::small));
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            l->setJustificationType (juce::Justification::centredRight);
            l->setBorderSize ({});
            l->setMinimumHorizontalScale (0.7f);
            addAndMakeVisible (l);
        }
        portLabel.setText (tr ("Port"), juce::dontSendNotification);
        channelLabel.setText (tr ("Channel"), juce::dontSendNotification);
        ctrlLabel.setText (tr ("Ctrl"), juce::dontSendNotification);
        timeLabel.setText (tr ("Time"), juce::dontSendNotification);

        const auto wheel = [this] (const juce::String& hint, int minimum, int maximum, int standard, int& value, bool noneBelowZero, int noneAt) {
            ui::ValueSpec spec;
            spec.name = hint;
            spec.minimum = minimum;
            spec.maximum = maximum;
            spec.defaultValue = standard;
            spec.interval = 1.0;
            spec.toText = [noneBelowZero, noneAt] (double v) {
                const auto n = juce::roundToInt (v);
                return (noneBelowZero && n < 0) || n == noneAt ? noneText : juce::String (n);
            };
            auto box = std::make_unique<WheelBox> (spec, [&value] { return (double) value; }, [this, &value, minimum, maximum] (double v) {
                const auto now = juce::jlimit (minimum, maximum, juce::roundToInt (v));
                if (now == value)
                    return;
                value = now;
                edited();
            });
            addAndMakeVisible (*box);
            return box;
        };
        portBox = wheel (tr ("MIDI input port number"), -1, 255, -1, port, true, -1000);
        // (FL's channel wheel runs 1...16; 0 = any channel is LYNX's own, for the links older versions made that way)
        channelBox = wheel (tr ("MIDI channel number"), 0, 16, 1, channel, false, 0);
        ctrlBox = wheel (tr ("Continuous Controller number"), -1, 127, -1, cc, true, -1000);
        ctrlBox->onMenu = [this] {
            remoteControlControllerMenu ([safe = juce::Component::SafePointer<RemoteControlPanel> (this)] (int number) {
                if (safe != nullptr)
                {
                    safe->cc = number;
                    safe->edited();
                }
            }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (ctrlBox.get()));
        };

        autoDetect.setButtonText (tr ("Auto detect"));
        autoDetect.setToggleState (true, juce::dontSendNotification);
        ui::setHint (autoDetect, tr ("Just move your MIDI controller"));
        addAndMakeVisible (autoDetect);
        omni.setButtonText (tr ("Omni"));
        ui::setHint (omni, tr ("Input goes to the selected channel / track"));
        omni.onClick = [this] { edited(); };
        if (! global)
            addAndMakeVisible (omni);

        // --- Internal controller
        ui::setHint (internalSelect, tr ("Internal controller"));
        if (! global)
            addAndMakeVisible (internalSelect);

        // --- Mapping formula
        addAndMakeVisible (graph);
        formulaEdit.setFont (theme::font (theme::type::small));
        formulaEdit.setSelectAllWhenFocused (false);
        formulaEdit.onTextChange = [this] { formulaChanged(); };
        formulaEdit.onReturnKey = [this] { formulaChanged(); };
        ui::setHint (formulaEdit, tr ("Input to output mapping formula"));
        addAndMakeVisible (formulaEdit);
        presetsButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe"));
        ui::setHint (presetsButton, tr ("Presets"));
        presetsButton.onClick = [this] {
            remoteControlFormulaMenu ([safe = juce::Component::SafePointer<RemoteControlPanel> (this)] (const juce::String& text) {
                if (safe != nullptr)
                {
                    safe->formulaEdit.setText (text, juce::dontSendNotification);
                    safe->formulaChanged();
                }
            }).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton));
        };
        addAndMakeVisible (presetsButton);
        errorLabel.setFont (theme::font (theme::type::caption));
        errorLabel.setJustificationType (juce::Justification::topLeft);
        errorLabel.setBorderSize ({});
        errorLabel.setMinimumHorizontalScale (0.75f);
        addAndMakeVisible (errorLabel);

        // --- Smoothing
        smoothOn.setButtonText (tr ("Smoothing"));
        ui::setHint (smoothOn, tr ("Enable automation smoothing"));
        smoothOn.onClick = [this] { smoothingChanged (false); };
        addAndMakeVisible (smoothOn);
        smoothTime.setSliderStyle (juce::Slider::LinearHorizontal);
        smoothTime.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        smoothTime.setRange (linkformula::smoothingMin, linkformula::smoothingMax, 1.0);
        smoothTime.setDoubleClickReturnValue (true, linkformula::smoothingDefault);
        smoothTime.setValue (linkformula::smoothingDefault, juce::dontSendNotification);
        smoothTime.onValueChange = [this] { smoothingChanged (true); };
        addAndMakeVisible (smoothTime);
        smoothValue.setFont (theme::font (theme::type::caption));
        smoothValue.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        smoothValue.setJustificationType (juce::Justification::centredLeft);
        smoothValue.setBorderSize ({});
        smoothValue.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (smoothValue);

        // --- Remove conflicts, Reset / Skip / Accept
        conflicts.setButtonText (tr ("Remove conflicts"));
        conflicts.setToggleState (true, juce::dontSendNotification);
        ui::setHint (conflicts, tr ("Auto-remove other conflicting assignments"));
        if (! global)
            addAndMakeVisible (conflicts);
        resetButton.setButtonText (tr ("Reset"));
        ui::setHint (resetButton, tr ("Reset all settings"));
        resetButton.onClick = [this] { reset(); };
        skipButton.setButtonText (tr ("Skip"));
        skipButton.onClick = [this] { next(); };
        acceptButton.setButtonText (tr ("Accept"));
        acceptButton.onClick = [this] { accept(); };
        for (auto* b : { &resetButton, &skipButton, &acceptButton })
            addAndMakeVisible (b);

        for (int i = 0; i < (int) targets.size(); ++i)
            eventSelect.addItem (describeAutomationTarget (engine, targets[(std::size_t) i]), i + 1);

        seenSerial = engine.lastControllerSeen().serial;
        setSize (juce::roundToInt (256.0f * formScale), juce::roundToInt ((float) ((global ? 377 : 490) - captionHeight) * formScale));
        showTarget (0);
        startTimerHz (20);
    }

    // FL's layout: the project form as it is; the global form has no assignment list, internal controller or
    // Remove conflicts, and its boxes move up.
    void resized() override
    {
        const auto up = global ? 113 : 0; // (the global form's boxes sit this much higher from the formula box on)
        eventSelect.setBounds (form (0, 26, 256, 26).reduced (4, 2));
        entrySelect.setBounds (form (0, 63, 256, 23).reduced (4, 0));
        midiBox = form (1, global ? 63 : 93, 254, 96);
        const auto m = [this] (int x, int y, int w, int h) {
            return form (1 + x, (global ? 63 : 93) + y, w, h);
        };
        portLabel.setBounds (m (2, 33, 28, 15));
        portBox->setBounds (m (34, 28, 40, 25));
        channelLabel.setBounds (m (78, 33, 55, 15));
        channelBox->setBounds (m (137, 28, 32, 25));
        ctrlLabel.setBounds (m (173, 33, 31, 15));
        ctrlBox->setBounds (m (208, 28, 40, 25));
        autoDetect.setBounds (m (3, 66, 100, 21));
        omni.setBounds (m (105, 66, 70, 21));

        internalBox = form (1, 197, 254, 75);
        internalSelect.setBounds (form (1 + 6, 197 + 26, 242, 23));

        graph.setBounds (form (3, 280 - up, 76, 75));
        formulaBox = form (79, 280 - up, 176, 77);
        formulaEdit.setBounds (form (79 + 6, 280 - up + 24, 149, 22));
        presetsButton.setBounds (form (79 + 157, 280 - up + 24, 15, 22));
        errorLabel.setBounds (form (79 + 6, 280 - up + 50, 164, 25));

        smoothingBox = form (1, 365 - up, 254, 42);
        smoothOn.setBounds (form (1 + 2, 365 - up + 1, 100, 19));
        timeLabel.setBounds (form (1 + 96, 365 - up + 15, 34, 15));
        smoothTime.setBounds (form (1 + 130, 365 - up + 8, 97, 26));
        smoothValue.setBounds (form (1 + 4, 365 - up + 22, 92, 15));

        conflicts.setBounds (form (2, 415, 150, 21));
        const auto buttons = global ? 344 : 457;
        resetButton.setBounds (form (1, buttons, 68, 33).reduced (2, 3));
        skipButton.setBounds (form (81, buttons, 68, 33).reduced (2, 3));
        acceptButton.setBounds (form (170, buttons, 84, 33).reduced (2, 3));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        const auto box = [&g] (juce::Rectangle<int> area, const juce::String& caption) {
            g.setColour (theme::colour::bg2);
            g.fillRoundedRectangle (area.toFloat().reduced (1.0f), theme::radius::md);
            g.setColour (theme::colour::border);
            g.drawRoundedRectangle (area.toFloat().reduced (1.0f), theme::radius::md, 1.0f);
            if (caption.isNotEmpty())
            {
                g.setColour (theme::colour::textSecondary);
                g.setFont (theme::font (theme::type::small, true));
                g.drawText (caption, area.reduced (9, 5).removeFromTop (16), juce::Justification::centredLeft);
            }
        };
        box (midiBox, tr ("MIDI controller"));
        if (! global)
            box (internalBox, tr ("Internal controller"));
        box (formulaBox, tr ("Mapping formula"));
        box (smoothingBox, {}); // (its caption is the Smoothing switch, as on FL's form)
    }

    // --- offscreen drivers
    void setFormulaText (const juce::String& text)
    {
        formulaEdit.setText (text, juce::dontSendNotification);
        formulaChanged();
    }
    void setController (int newPort, int newChannel, int newCc)
    {
        port = juce::jlimit (-1, 255, newPort);
        channel = juce::jlimit (0, 16, newChannel);
        cc = juce::jlimit (-1, 127, newCc);
        edited();
    }
    void setSmoothing (bool on, int position)
    {
        smoothTime.setValue (position, juce::dontSendNotification);
        smoothOn.setToggleState (on, juce::dontSendNotification);
        smoothingChanged (false);
    }
    void poll() { timerCallback(); }
    void press (int button)
    {
        if (button == 0)
            reset();
        else if (button == 1)
            next();
        else
            accept();
    }
    bool isFinished() const noexcept { return finished; }
    juce::String state() const
    {
        return juce::String (port) + "|" + juce::String (channel) + "|" + juce::String (cc) + "|" + formulaEdit.getText() + "|" + errorLabel.getText()
             + "|" + juce::String (conflictCount) + "|" + eventSelect.getText();
    }

private:
    const AutomationTarget& target() const { return targets[(std::size_t) index]; }

    // The current control's links: the project's (or its global one), then FL's "New link".
    void showTarget (int which)
    {
        index = juce::jlimit (0, (int) targets.size() - 1, which);
        eventSelect.setSelectedId (index + 1, juce::dontSendNotification);
        eventSelect.setEnabled (targets.size() > 1);
        entries.clear();
        if (global)
        {
            if (const auto g = engine.globalLinkOf (target()))
                entries.push_back (*g);
        }
        else
            for (const auto& l : engine.getSong().midiLinks)
                if (l.target == target())
                    entries.push_back (l);
        const auto existing = (int) entries.size();
        MidiLink fresh;
        fresh.cc = -1;
        fresh.channel = 0; // (channel 1, as FL's wheel starts)
        fresh.target = target();
        entries.push_back (fresh);

        entrySelect.clear (juce::dontSendNotification);
        for (int i = 0; i < existing; ++i)
            entrySelect.addItem (tr ("Link {0}", { juce::String (i + 1) }), i + 1);
        entrySelect.addItem (tr ("New link"), existing + 1);
        entrySelect.setEnabled (entrySelect.getNumItems() > 1);

        // FL: Omni for a channel's controls and a mixer slot's controls
        omni.setEnabled (linkOmniAvailable (target().kind));

        outputs = target().kind == AutomationTargetKind::tempo ? std::vector<ControllerOutput>() : controllerOutputs (engine.getSong());
        internalSelect.clear (juce::dontSendNotification);
        internalSelect.addItem (tr ("(none)"), 1);
        internalNow = 0;
        const auto& automations = engine.getSong().automations;
        const auto live = std::find_if (automations.begin(), automations.end(), [this] (const Automation& a) { return a.isLive() && a.target == target(); });
        for (int i = 0; i < (int) outputs.size(); ++i)
        {
            const auto& o = outputs[(std::size_t) i];
            internalSelect.addItem (o.label, i + 2);
            if (live != automations.end() && live->source.track == o.track && live->source.slot == o.slot && live->source.output == o.output
                && live->source.channel == o.channel)
                internalNow = i + 1;
        }
        internalSelect.setSelectedId (internalNow + 1, juce::dontSendNotification);
        internalSelect.setEnabled (! outputs.empty());

        entry = -1;
        showEntry (0);
    }

    void showEntry (int which)
    {
        entry = juce::jlimit (0, (int) entries.size() - 1, which);
        entrySelect.setSelectedId (entry + 1, juce::dontSendNotification);
        const auto& l = entries[(std::size_t) entry];
        port = l.port;
        channel = l.channel + 1; // (0 = any channel)
        cc = l.cc;
        omni.setToggleState (l.omni && omni.isEnabled(), juce::dontSendNotification);
        formulaEdit.setText (l.formula.empty() ? juce::String ("Input") : juce::String::fromUTF8 (l.formula.c_str()), juce::dontSendNotification);
        smoothTime.setValue (l.smoothing, juce::dontSendNotification);
        smoothOn.setToggleState (l.smooth, juce::dontSendNotification);
        formulaChanged();
        smoothingChanged (false);
        edited();
    }

    MidiLink current() const
    {
        MidiLink l;
        l.target = target();
        l.port = port;
        l.channel = channel - 1;
        l.cc = cc;
        l.omni = ! global && omni.isEnabled() && omni.getToggleState();
        const auto text = formulaEdit.getText().trim().toStdString();
        l.formula = linkformula::isDefaultMapping (text) ? std::string() : text;
        l.smooth = smoothOn.getToggleState();
        l.smoothing = juce::roundToInt (smoothTime.getValue());
        return l;
    }

    void store()
    {
        if (entry >= 0 && entry < (int) entries.size())
            entries[(std::size_t) entry] = current();
    }

    // A field changed: repaint the wheels and FL's conflict warning (Remove conflicts in red while another control uses
    // this controller).
    void edited()
    {
        for (auto* b : { portBox.get(), channelBox.get(), ctrlBox.get() })
            b->repaint();
        conflictCount = global ? 0 : countLinkConflicts (engine.getSong().midiLinks, current());
        conflicts.setColour (juce::ToggleButton::textColourId, conflictCount > 0 ? theme::colour::record : theme::colour::textPrimary);
    }

    void formulaChanged()
    {
        const auto text = formulaEdit.getText().toStdString();
        std::string message;
        int at = -1;
        graph.formula = linkformula::Formula::compile (text, &message, &at);
        if (at == -2)
        {
            errorLabel.setText (tr ("(default 1:1 mapping)"), juce::dontSendNotification);
            graph.curve = theme::colour::textSecondary;
        }
        else if (at == -1)
        {
            errorLabel.setText (tr ("Compiled ok."), juce::dontSendNotification);
            graph.curve = theme::colour::info;
        }
        else
        {
            errorLabel.setText (juce::String::fromUTF8 (message.c_str()), juce::dontSendNotification);
            graph.curve = theme::colour::record;
        }
        const auto bad = at >= 0;
        errorLabel.setColour (juce::Label::textColourId, bad ? theme::colour::record : theme::colour::textSecondary);
        formulaEdit.applyColourToAllText (bad ? theme::colour::record : theme::colour::textPrimary);
        graph.repaint();
    }

    // FL: moving the slider to 0 switches smoothing off, any other position switches it on; the hint gives the
    // filter's poles and time.
    void smoothingChanged (bool fromSlider)
    {
        const auto position = juce::roundToInt (smoothTime.getValue());
        if (fromSlider)
            smoothOn.setToggleState (position != 0, juce::dontSendNotification);
        juce::String text;
        if (! smoothOn.getToggleState() || position == 0)
            text = tr ("Disabled");
        else
            text = (linkformula::smoothingPoles (position) == 1 ? tr ("1 pole") : tr ("3 poles")) + " "
                 + juce::String (juce::roundToInt (linkformula::smoothingMs (position))) + "ms";
        smoothValue.setText (text, juce::dontSendNotification);
        ui::setHint (smoothTime, tr ("Smoothing envelope time") + ": " + text);
    }

    // FL's Reset: no controller, no internal controller, the 1:1 formula, smoothing off at its default time.
    void reset()
    {
        port = -1;
        channel = 1;
        cc = -1;
        omni.setToggleState (false, juce::dontSendNotification);
        internalSelect.setSelectedId (1, juce::dontSendNotification);
        formulaEdit.setText ("Input", juce::dontSendNotification);
        smoothTime.setValue (linkformula::smoothingDefault, juce::dontSendNotification);
        smoothOn.setToggleState (false, juce::dontSendNotification);
        formulaChanged();
        smoothingChanged (false);
        edited();
    }

    void accept()
    {
        store();
        const auto t = target();
        if (global)
            engine.setGlobalLink (t, entries.front().cc >= 0 ? std::optional<MidiLink> (entries.front()) : std::nullopt);
        else
        {
            engine.setTargetLinks (t, entries, conflicts.getToggleState());
            const auto chosen = internalSelect.getSelectedId() - 1; // 0 = (none)
            if (chosen != internalNow)
            {
                if (chosen > 0 && chosen <= (int) outputs.size())
                    linkToController (engine, t, outputs[(std::size_t) (chosen - 1)]);
                else
                    engine.editSong ([t] (Song& s) {
                        for (int i = (int) s.automations.size() - 1; i >= 0; --i)
                            if (s.automations[(std::size_t) i].isLive() && s.automations[(std::size_t) i].target == t)
                                s.removeAutomation (i);
                    });
            }
        }
        next();
    }

    // FL's Skip (and what follows Accept): the next queued control, or the window closes after the last one.
    void next()
    {
        if (index + 1 < (int) targets.size())
        {
            showTarget (index + 1);
            return;
        }
        stopTimer();
        finished = true;
        if (onClose)
            juce::MessageManager::callAsync ([close = onClose] { close(); });
    }

    // FL's Auto detect: the controller that sends next fills port, channel and controller number.
    void timerCallback() override
    {
        const auto seen = engine.lastControllerSeen();
        if (seen.serial == seenSerial)
            return;
        seenSerial = seen.serial;
        if (! autoDetect.getToggleState() || seen.cc < 0 || eventSelect.isPopupActive() || entrySelect.isPopupActive()
            || internalSelect.isPopupActive())
            return;
        if (port == seen.port && channel == seen.channel + 1 && cc == seen.cc)
            return;
        port = seen.port;
        channel = seen.channel + 1;
        cc = seen.cc;
        edited();
    }

    AudioEngine& engine;
    std::vector<AutomationTarget> targets;
    bool global = false;
    std::function<void()> onClose;
    int index = 0;
    bool finished = false; // the last control was accepted or skipped (the window closes)
    std::vector<MidiLink> entries;
    int entry = -1;
    int port = -1, channel = 1, cc = -1;
    int conflictCount = 0;
    std::vector<ControllerOutput> outputs;
    int internalNow = 0; // 0 = none, else 1 + index into `outputs`
    std::uint64_t seenSerial = 0;

    juce::ComboBox eventSelect, entrySelect, internalSelect;
    juce::Label portLabel, channelLabel, ctrlLabel, timeLabel, errorLabel, smoothValue;
    std::unique_ptr<WheelBox> portBox, channelBox, ctrlBox;
    juce::ToggleButton autoDetect, omni, smoothOn, conflicts;
    MappingGraph graph;
    juce::TextEditor formulaEdit;
    juce::TextButton presetsButton, resetButton, skipButton, acceptButton;
    juce::Slider smoothTime;
    juce::Rectangle<int> midiBox, internalBox, formulaBox, smoothingBox;
};

class RemoteControlWindow final : public juce::DocumentWindow
{
public:
    RemoteControlWindow (AudioEngine& engine, std::vector<AutomationTarget> targets, bool global)
        : juce::DocumentWindow (global ? tr ("Global link settings") : tr ("Remote control settings"), theme::colour::bg1, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new RemoteControlPanel (engine, std::move (targets), global, [] { closeRemoteControlSettings(); }), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([] { closeRemoteControlSettings(); });
    }
    static std::unique_ptr<RemoteControlWindow>& instance()
    {
        static std::unique_ptr<RemoteControlWindow> window;
        return window;
    }
};
} // namespace

void openRemoteControlSettings (AudioEngine& engine, std::vector<AutomationTarget> targets, bool global)
{
    targets.erase (std::remove_if (targets.begin(), targets.end(), [] (const AutomationTarget& t) { return t.kind == AutomationTargetKind::none; }),
                   targets.end());
    if (targets.empty())
        return;
    auto& window = RemoteControlWindow::instance();
    window.reset();
    window = std::make_unique<RemoteControlWindow> (engine, std::move (targets), global);
}

void closeRemoteControlSettings()
{
    RemoteControlWindow::instance().reset();
}

std::unique_ptr<juce::Component> makeRemoteControlPanel (AudioEngine& engine, std::vector<AutomationTarget> targets, bool global, std::function<void()> close)
{
    return std::make_unique<RemoteControlPanel> (engine, std::move (targets), global, std::move (close));
}

void remoteControlPanelSetFormula (juce::Component& panel, const juce::String& formula)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setFormulaText (formula);
}

void remoteControlPanelSetController (juce::Component& panel, int port, int channel, int cc)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setController (port, channel, cc);
}

void remoteControlPanelSetSmoothing (juce::Component& panel, bool on, int position)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setSmoothing (on, position);
}

void remoteControlPanelPoll (juce::Component& panel)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->poll();
}

void remoteControlPanelPress (juce::Component& panel, int button)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->press (button);
}

juce::String remoteControlPanelState (juce::Component& panel)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        return p->state();
    return {};
}

bool remoteControlPanelFinished (juce::Component& panel)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        return p->isFinished();
    return false;
}

void addMultilinkControlsItems (juce::PopupMenu& menu, AudioEngine& engine)
{
    const auto queued = engine.multilinkTargets();
    const auto any = ! queued.empty();
    menu.addSectionHeader (tr ("Controls") + " (" + juce::String ((int) queued.size()) + ")");
    menu.addSectionHeader (tr ("Automation"));
    menu.addItem (tr ("Init song with these positions"), any, false, [&engine, queued] {
        std::vector<std::pair<AutomationTarget, float>> values;
        for (const auto& t : queued)
            if (const auto v = engine.currentTargetValue (t); v >= 0.0f)
                values.push_back ({ t, v });
        engine.editSong ([values] (Song& s) {
            for (const auto& [t, v] : values)
                s.setInitialValue (t, v);
        });
        engine.setMultilink (false);
    });
    menu.addItem (tr ("Create automation clips"), any, false, [&engine, queued] {
        engine.setMultilink (false);
        for (const auto& t : queued)
            createAutomationClip (engine, t);
    });
    menu.addSectionHeader (tr ("Remote control"));
    menu.addItem (tr ("Link to controllers..."), any, false, [&engine, queued] {
        engine.setMultilink (false);
        openRemoteControlSettings (engine, queued, false);
    });
    menu.addItem (tr ("Override global links..."), any, false, [&engine, queued] {
        engine.setMultilink (false);
        openRemoteControlSettings (engine, queued, true);
    });
    menu.addSectionHeader (tr ("Value"));
    menu.addItem (tr ("Cancel"), engine.isMultilinkOn(), false, [&engine] { engine.setMultilink (false); });
}

// FL's FormulaMenu: its sections, dividing lines and presets, in its order.
juce::PopupMenu remoteControlFormulaMenu (std::function<void (const juce::String&)> pick)
{
    juce::PopupMenu menu;
    for (const auto& p : linkformula::presets())
    {
        if (p.formula == nullptr)
        {
            menu.addSectionHeader (tr (p.name));
            continue;
        }
        if (p.separatorBefore)
            menu.addSeparator();
        menu.addItem (tr (p.name), [pick, formula = juce::String (p.formula)] { pick (formula); });
    }
    return menu;
}

// FL's CtrlMenu, the part LYNX's links can follow: the standard continuous controllers by name ("CC #7 (Volume)").
juce::PopupMenu remoteControlControllerMenu (std::function<void (int)> pick)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Standard Continuous Controllers"));
    for (int n = 0; n < 128; ++n)
    {
        const juce::String name = midictl::ccName (n);
        if (name.isNotEmpty())
            menu.addItem ("CC #" + juce::String (n) + " (" + tr (name) + ")", [pick, n] { pick (n); });
    }
    return menu;
}
} // namespace lynx
