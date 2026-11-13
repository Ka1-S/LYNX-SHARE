#include "ui/RemoteControl.h"

#include "core/FlControlMenus.h"
#include "core/LinkFormula.h"
#include "core/MidiControls.h"
#include "ui/AutomationUi.h"
#include "ui/EventEditor.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
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

// FL's FGraph: the mapping's curve, input left to right, output bottom to top, over a grid of quarters. FL's colours
// (live check 190cq, sampled): background #363f45 with a grid that barely shows, the 1:1 line #b8bec3, a compiled
// formula's curve cream #f2d99f, a formula with a mistake red #fe3820. Hovering reads "In: 0.48 out: 0.48".
class MappingGraph final : public juce::Component, public ui::HintSource
{
public:
    enum class State { standard, compiled, error };
    linkformula::Formula formula;
    State state = State::standard;

    static juce::Colour curveColour (State s)
    {
        return s == State::compiled ? juce::Colour (0xfff2d99f) : s == State::error ? juce::Colour (0xfffe3820) : juce::Colour (0xffb8bec3);
    }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff363f45));
        g.fillRect (area);
        g.setColour (juce::Colour (0xff3d474e));
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
        g.setColour (curveColour (state));
        g.strokePath (path, juce::PathStrokeType (1.6f));
    }

    ui::Hint hintAt (juce::Point<int> position) override
    {
        const auto inner = getLocalBounds().toFloat().reduced (4.0f);
        const auto in = juce::jlimit (0.0f, 1.0f, ((float) position.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth()));
        return { tr ("In: {0} out: {1}", { juce::String (in, 2), juce::String (formula.map (in), 2) }) };
    }
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
};

// FL's ErrorLabel: one line under the formula, cut where the line ends (FL's "Syntax error: function or operand has
// too few arguments" reads "Syntax error: function or operand").
class MessageLine final : public juce::Component
{
public:
    juce::String text;
    juce::Colour colour = theme::colour::textSecondary;
    void set (const juce::String& t, juce::Colour c)
    {
        text = t;
        colour = c;
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const juce::Font font (theme::font (theme::type::caption, true));
        g.setFont (font);
        g.setColour (colour);
        juce::GlyphArrangement line;
        line.addJustifiedText (font, text, 0.0f, font.getAscent(), (float) getWidth(), juce::Justification::left);
        const auto lineHeight = font.getHeight();
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (0, 0, getWidth(), (int) std::ceil (lineHeight));
        line.draw (g);
    }
};

// The smoothing "Time" slider with FL's read-out in its hint ("286ms  3 poles", "7ms 1 pole") [H 0xf8b6b0].
class SmoothingSlider final : public juce::Slider, public ui::HintSource
{
public:
    juce::String readOut;
    ui::Hint hintAt (juce::Point<int>) override { return { tr ("Smoothing envelope time"), readOut }; }
};

// The caption FL gives a control in the window: its event name ("Channel volume", "Channel panning"; FL's own words,
// live check 190cq), the mixer's "Insert 1 - Volume" as LYNX names them elsewhere.
juce::String parameterCaption (AudioEngine& engine, const AutomationTarget& t)
{
    if (t.kind == AutomationTargetKind::channelVolume)
        return tr ("Channel volume");
    if (t.kind == AutomationTargetKind::channelPan)
        return tr ("Channel panning");
    return describeAutomationTarget (engine, t);
}

class RemoteControlPanel final : public juce::Component, private juce::Timer
{
public:
    RemoteControlPanel (AudioEngine& e, std::vector<AutomationTarget> t, bool isGlobal, std::function<void()> close)
        : engine (e), targets (std::move (t)), global (isGlobal), onClose (std::move (close))
    {
        if (targets.empty())
            targets.push_back ({});

        // --- "Current parameter" / "Current assignment" (FL shows the assignment list only once the control has a
        // link, and the parameter list of the global window only while several controls are queued; live check 190cq)
        ui::setHint (eventSelect, tr ("Current parameter"));
        eventSelect.onChange = [this] {
            if (eventSelect.getSelectedId() - 1 != index)
                showTarget (eventSelect.getSelectedId() - 1);
        };
        addChildComponent (eventSelect);
        ui::setHint (entrySelect, tr ("Current assignment"));
        entrySelect.onChange = [this] {
            if (entrySelect.getSelectedId() - 1 != entry)
            {
                store();
                showEntry (entrySelect.getSelectedId() - 1);
            }
        };
        addChildComponent (entrySelect);

        // --- MIDI controller
        for (auto* l : { &portLabel, &channelLabel, &ctrlLabel, &modeLabel, &timeLabel, &recordLabel })
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
        modeLabel.setText (tr ("Mode"), juce::dontSendNotification);
        timeLabel.setText (tr ("Time"), juce::dontSendNotification);
        recordLabel.setText (tr ("Record automation"), juce::dontSendNotification);

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
                auto now = juce::jlimit (minimum, maximum, juce::roundToInt (v));
                // the Ctrl wheel: controllers 0...127, then keys from 256 (FL's numbers between are what LYNX cannot
                // link: aftertouch, pitch bend)
                if (&value == &cc && now > 127 && now < linkNoteBase)
                    now = now > value ? linkNoteBase : 127;
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
        ctrlBox = wheel (tr ("Continuous Controller number"), -1, linkNoteLast, -1, cc, true, -1000);
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

        // FL's NoteLinkModeBox (round 190cs): Default / Hold / Latch / Inc / Stay, in both windows
        for (int m = 0; m < linkformula::linkModeCount; ++m)
            modeSelect.addItem (tr (modeName (m)), m + 1);
        modeSelect.setSelectedId (1, juce::dontSendNotification);
        modeSelect.onChange = [this] { modeChanged(); };
        addAndMakeVisible (modeSelect);
        modeChanged();

        // --- Internal controller (FL shows the group only when the project has one; live check 190cq)
        ui::setHint (internalSelect, tr ("Internal controller"));
        internalSelect.onChange = [this] {
            if (internalSelect.getSelectedId() > 1) // FL: picking a controller clears the MIDI controller number
            {
                cc = -1;
                edited();
            }
        };
        addChildComponent (internalSelect);
        ui::setHint (recordAutomation, tr ("Allow recording of automation"));
        ui::setHint (recordLabel, tr ("Allow recording of automation"));
        addChildComponent (recordAutomation);

        // --- Mapping formula (FL compiles it when Enter is pressed, not while typing; live check 190cq)
        addAndMakeVisible (graph);
        formulaEdit.setFont (theme::font (theme::type::small));
        formulaEdit.setSelectAllWhenFocused (false);
        formulaEdit.onReturnKey = [this] { formulaChanged(); };
        ui::setHint (formulaEdit, tr ("Input to output mapping formula"));
        addAndMakeVisible (formulaEdit);
        presetsButton.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xb8"));
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
        addAndMakeVisible (message);

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

        // --- Remove conflicts, Reset / Skip / Accept (FL: Skip only while several controls are queued)
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
        addAndMakeVisible (resetButton);
        addChildComponent (skipButton);
        addAndMakeVisible (acceptButton);

        const auto queued = (int) targets.size();
        for (int i = 0; i < queued; ++i)
            eventSelect.addItem ((queued > 1 ? juce::String (i + 1) + "/" + juce::String (queued) + "  " : juce::String())
                                     + parameterCaption (engine, targets[(std::size_t) i]),
                                 i + 1);

        seenSerial = engine.lastControllerSeen().serial;
        showTarget (0);
        startTimerHz (20);
    }

    // FL's layout: the form's places; the groups FL hides close up (the window gets shorter, as FL's does).
    void resized() override
    {
        auto y = 0; // how much the hidden groups above have taken away (form units)
        if (! eventSelect.isVisible())
            y += 37;
        eventSelect.setBounds (form (0, 26, 256, 26).reduced (4, 2));
        entrySelect.setBounds (form (0, 63 - y, 256, 23).reduced (4, 0));
        if (! entrySelect.isVisible())
            y += 30;
        const auto midiTop = 93 - y;
        midiBox = form (1, midiTop, 254, 96);
        const auto m = [midiTop] (int x, int yy, int w, int h) { return form (1 + x, midiTop + yy, w, h); };
        portLabel.setBounds (m (2, 33, 28, 15));
        portBox->setBounds (m (34, 28, 40, 25));
        channelLabel.setBounds (m (78, 33, 55, 15));
        channelBox->setBounds (m (137, 28, 32, 25));
        ctrlLabel.setBounds (m (173, 33, 31, 15));
        ctrlBox->setBounds (m (208, 28, 40, 25));
        autoDetect.setBounds (m (3, 66, 88, 21));
        omni.setBounds (m (91, 66, 58, 21));
        modeLabel.setBounds (m (147, 69, 35, 15));
        modeSelect.setBounds (m (186, 65, 62, 23));

        const auto internalTop = 197 - y;
        internalBox = form (1, internalTop, 254, 75);
        internalSelect.setBounds (form (1 + 6, internalTop + 26, 242, 23));
        recordLabel.setBounds (form (1 + 120, internalTop + 54, 108, 18));
        recordAutomation.setBounds (form (1 + 230, internalTop + 54, 20, 18));
        if (! internalSelect.isVisible())
            y += 83;

        const auto formulaTop = 280 - y;
        graph.setBounds (form (3, formulaTop, 76, 75));
        formulaBox = form (79, formulaTop, 176, 77);
        formulaEdit.setBounds (form (79 + 6, formulaTop + 24, 149, 22));
        presetsButton.setBounds (form (79 + 157, formulaTop + 24, 15, 22));
        message.setBounds (form (79 + 6, formulaTop + 50, 164, 25));

        const auto smoothingTop = 365 - y;
        smoothingBox = form (1, smoothingTop, 254, 42);
        smoothOn.setBounds (form (1 + 2, smoothingTop + 1, 100, 19));
        timeLabel.setBounds (form (1 + 96, smoothingTop + 15, 34, 15));
        smoothTime.setBounds (form (1 + 130, smoothingTop + 8, 97, 26));

        conflicts.setBounds (form (2, 415 - y, 150, 21));
        // (the global window has no Remove conflicts: its buttons follow the smoothing group; FL 269 x 306)
        const auto buttons = global ? smoothingTop + 47 : 457 - y;
        resetButton.setBounds (form (1, buttons, 68, 33).reduced (2, 3));
        skipButton.setBounds (form (81, buttons, 68, 33).reduced (2, 3));
        acceptButton.setBounds (form (170, buttons, 84, 33).reduced (2, 3));
        panelHeight = form (0, buttons + 33, 1, 1).getY() + 4;
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
        if (internalSelect.isVisible())
            box (internalBox, tr ("Internal controller"));
        box (formulaBox, tr ("Mapping formula"));
        box (smoothingBox, {}); // (its caption is the Smoothing switch, as on FL's form)
    }

    // --- offscreen drivers
    void setFormulaText (const juce::String& text, bool pressEnter)
    {
        formulaEdit.setText (text, juce::dontSendNotification);
        if (pressEnter)
            formulaChanged();
    }
    void setController (int newPort, int newChannel, int newCc)
    {
        port = juce::jlimit (-1, 255, newPort);
        channel = juce::jlimit (0, 16, newChannel);
        cc = juce::jlimit (-1, linkNoteLast, newCc);
        edited();
    }
    void setSmoothing (bool on, int position)
    {
        smoothTime.setValue (position, juce::dontSendNotification);
        smoothOn.setToggleState (on, juce::dontSendNotification);
        smoothingChanged (false);
    }
    void setMode (int mode)
    {
        modeSelect.setSelectedId (juce::jlimit (0, linkformula::linkModeCount - 1, mode) + 1, juce::dontSendNotification);
        modeChanged();
    }
    void setInternal (int which, bool record)
    {
        internalSelect.setSelectedId (which + 1, juce::sendNotificationSync);
        recordAutomation.setToggleState (record, juce::dontSendNotification);
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
        return juce::String (port) + "|" + juce::String (channel) + "|" + juce::String (cc) + "|" + formulaEdit.getText() + "|" + message.text + "|"
             + juce::String (conflictCount) + "|" + eventSelect.getText() + "|mode=" + juce::String (modeSelect.getSelectedId() - 1)
             + "|shown=" + (eventSelect.isVisible() ? "P" : "") + (entrySelect.isVisible() ? "A" : "") + (internalSelect.isVisible() ? "I" : "")
             + (omni.isVisible() ? "O" : "") + (conflicts.isVisible() ? "C" : "") + (skipButton.isVisible() ? "S" : "")
             + "|omni=" + (omni.isEnabled() ? "on" : "off") + "|h=" + juce::String (getHeight());
    }

private:
    const AutomationTarget& target() const { return targets[(std::size_t) index]; }

    static const char* modeName (int mode)
    {
        static const char* const names[] { "Default", "Hold", "Latch", "Inc", "Stay" };
        return names[juce::jlimit (0, linkformula::linkModeCount - 1, mode)];
    }
    static const char* modeHint (int mode)
    {
        static const char* const hints[] { "Preserve controller default mode", "Hold to keep at velocity", "Press again to switch",
                                           "Increment value", "Keep at velocity" };
        return hints[juce::jlimit (0, linkformula::linkModeCount - 1, mode)];
    }
    void modeChanged()
    {
        const auto mode = modeSelect.getSelectedId() - 1;
        ui::setHint (modeSelect, tr ("How to treat a note or button link") + " - " + tr (modeHint (mode)));
    }

    // The current control's links: the project's (or its global one), then FL's "New link".
    void showTarget (int which)
    {
        index = juce::jlimit (0, (int) targets.size() - 1, which);
        eventSelect.setSelectedId (index + 1, juce::dontSendNotification);
        eventSelect.setVisible (! global || targets.size() > 1);
        skipButton.setVisible (targets.size() > 1);
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
        entrySelect.setVisible (! global && existing > 0);

        // FL: Omni for a channel's controls and a mixer track's controls (not the tempo)
        omni.setEnabled (linkOmniAvailable (target().kind));

        outputs = target().kind == AutomationTargetKind::tempo ? std::vector<ControllerOutput>() : controllerOutputs (engine.getSong());
        internalSelect.clear (juce::dontSendNotification);
        internalSelect.addItem (tr ("(none)"), 1);
        internalNow = 0;
        recordNow = false;
        const auto& automations = engine.getSong().automations;
        const auto live = std::find_if (automations.begin(), automations.end(), [this] (const Automation& a) { return a.isLive() && a.target == target(); });
        for (int i = 0; i < (int) outputs.size(); ++i)
        {
            const auto& o = outputs[(std::size_t) i];
            internalSelect.addItem (o.label, i + 2);
            if (live != automations.end() && live->source.track == o.track && live->source.slot == o.slot && live->source.output == o.output
                && live->source.channel == o.channel)
            {
                internalNow = i + 1;
                recordNow = live->recordLive;
            }
        }
        internalSelect.setSelectedId (internalNow + 1, juce::dontSendNotification);
        recordAutomation.setToggleState (recordNow, juce::dontSendNotification);
        const auto internal = ! global && ! outputs.empty();
        internalSelect.setVisible (internal);
        recordAutomation.setVisible (internal);
        recordLabel.setVisible (internal);

        entry = -1;
        showEntry (0);
        resized();
        setSize (juce::roundToInt (256.0f * formScale), panelHeight);
        repaint();
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
        modeSelect.setSelectedId (juce::jlimit (0, linkformula::linkModeCount - 1, l.mode) + 1, juce::dontSendNotification);
        modeChanged();
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
        l.mode = modeSelect.getSelectedId() - 1;
        // FL's store compiles the edit's text: a formula with a mistake is not kept, the link maps 1:1 ("Input" when
        // the window opens again; live check 190cq, [H 0xf8bc70])
        const auto text = formulaEdit.getText().trim().toStdString();
        int at = -1;
        linkformula::Formula::compile (text, nullptr, &at);
        l.formula = at == -1 ? text : std::string();
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

    // FL's compile (Enter, a preset, Reset): the message, the curve's colour, and on a mistake the edit's text marked red
    void formulaChanged()
    {
        const auto text = formulaEdit.getText().toStdString();
        std::string error;
        int at = -1;
        graph.formula = linkformula::Formula::compile (text, &error, &at);
        const juce::Colour red (0xfffe3820);
        if (at == -2)
        {
            message.set (tr ("(default 1:1 mapping)"), theme::colour::textSecondary);
            graph.state = MappingGraph::State::standard;
        }
        else if (at == -1)
        {
            message.set (tr ("Compiled ok."), theme::colour::textSecondary);
            graph.state = MappingGraph::State::compiled;
        }
        else
        {
            message.set (juce::String::fromUTF8 (error.c_str()), red);
            graph.state = MappingGraph::State::error;
        }
        const auto bad = at >= 0;
        formulaEdit.setColour (juce::TextEditor::highlightColourId, bad ? red : theme::colour::selectionFill);
        formulaEdit.setColour (juce::TextEditor::highlightedTextColourId, bad ? juce::Colours::white : theme::colour::textPrimary);
        formulaEdit.applyColourToAllText (theme::colour::textPrimary);
        if (bad)
            formulaEdit.selectAll();
        else
            formulaEdit.setHighlightedRegion ({});
        graph.repaint();
    }

    // FL: moving the slider to 0 switches smoothing off, any other position switches it on; the hint gives the
    // filter's time and poles as FL writes them ("286ms  3 poles", "7ms 1 pole").
    void smoothingChanged (bool fromSlider)
    {
        const auto position = juce::roundToInt (smoothTime.getValue());
        if (fromSlider)
            smoothOn.setToggleState (position != 0, juce::dontSendNotification);
        smoothTime.readOut = position == 0 ? tr ("Disabled")
                                           : juce::String (juce::roundToInt (linkformula::smoothingMs (position))) + "ms"
                                                 + (linkformula::smoothingPoles (position) == 1 ? " " + tr ("1 pole") : "  " + tr ("3 poles"));
    }

    // FL's Reset clears both groups (MIDI controller with its Mode, internal controller), the formula to "Input" and
    // smoothing off at its default time [H 0xf8a060].
    void reset()
    {
        port = -1;
        channel = 1;
        cc = -1;
        omni.setToggleState (false, juce::dontSendNotification);
        modeSelect.setSelectedId (1, juce::dontSendNotification);
        modeChanged();
        internalSelect.setSelectedId (1, juce::dontSendNotification);
        recordAutomation.setToggleState (false, juce::dontSendNotification);
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
            // "Record automation" of the internal controller link
            const auto record = recordAutomation.getToggleState();
            if (chosen > 0 && (chosen != internalNow || record != recordNow))
                engine.editSong ([t, record] (Song& s) {
                    for (auto& a : s.automations)
                        if (a.isLive() && a.target == t)
                            a.recordLive = record;
                });
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

    // FL's Auto detect: the controller (or key) that sends next fills port, channel and controller number.
    void timerCallback() override
    {
        const auto seen = engine.lastControllerSeen();
        if (seen.serial == seenSerial)
            return;
        seenSerial = seen.serial;
        if (! autoDetect.getToggleState() || seen.cc < 0 || eventSelect.isPopupActive() || entrySelect.isPopupActive()
            || internalSelect.isPopupActive() || modeSelect.isPopupActive())
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
    bool recordNow = false;
    std::uint64_t seenSerial = 0;
    int panelHeight = 0;

    juce::ComboBox eventSelect, entrySelect, internalSelect, modeSelect;
    juce::Label portLabel, channelLabel, ctrlLabel, modeLabel, timeLabel, recordLabel;
    std::unique_ptr<WheelBox> portBox, channelBox, ctrlBox;
    juce::ToggleButton autoDetect, omni, smoothOn, conflicts, recordAutomation;
    MappingGraph graph;
    MessageLine message;
    juce::TextEditor formulaEdit;
    juce::TextButton presetsButton, resetButton, skipButton, acceptButton;
    SmoothingSlider smoothTime;
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

void remoteControlPanelSetFormula (juce::Component& panel, const juce::String& formula, bool pressEnter)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setFormulaText (formula, pressEnter);
}

void remoteControlPanelSetMode (juce::Component& panel, int mode)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setMode (mode);
}

void remoteControlPanelSetInternal (juce::Component& panel, int which, bool recordAutomation)
{
    if (auto* p = dynamic_cast<RemoteControlPanel*> (&panel))
        p->setInternal (which, recordAutomation);
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

// The parameters FL's multilink menu acts on: the queued controls while multilink is on, else the last two tweaked
// parameters [0x1233e10 copies them into the queue when multilink is off]
std::vector<AutomationTarget> multilinkMenuTargets (AudioEngine& engine)
{
    if (engine.isMultilinkOn())
        return engine.multilinkTargets();
    std::vector<AutomationTarget> targets;
    for (int which = 0; which < 2; ++which)
        if (const auto t = engine.getLastTweaked (which); t && t->kind != AutomationTargetKind::none)
            targets.push_back (*t);
    return targets;
}

void runMultilinkAction (AudioEngine& engine, mlmenu::Action action, const std::vector<AutomationTarget>& targets, std::uint32_t seed)
{
    const auto on = engine.isMultilinkOn();
    using A = mlmenu::Action;
    switch (action)
    {
        case A::editEvents:
            // one event editor per parameter (FL calls its opener for each [0x1234110])
            for (int i = 0; i < (int) targets.size(); ++i)
                openToolWindow (makeEventEditor (engine, targets[(std::size_t) i]), tr ("Event editor"),
                                i == 0 ? juce::String ("eventeditor") : "eventeditor-" + juce::String (i));
            break;
        case A::initSong:
        case A::initPattern:
        {
            std::vector<std::pair<AutomationTarget, float>> values;
            for (const auto& t : targets)
                if (const auto v = engine.currentTargetValue (t); v >= 0.0f)
                    values.push_back ({ t, v });
            const auto pattern = action == A::initPattern;
            engine.editSong ([values, pattern] (Song& s) {
                for (const auto& [t, v] : values)
                    if (pattern)
                        mlmenu::initPattern (s, s.currentPattern, t, v);
                    else
                        s.setInitialValue (t, v);
            });
            break;
        }
        case A::createClips:
            for (const auto& t : targets)
                createAutomationClip (engine, t);
            break;
        case A::linkToControllers:
        case A::overrideGlobal:
            break; // (after the queue is cleared, below)
        case A::randomize:
        case A::humanize:
        {
            juce::Random random (seed != 0 ? (juce::int64) seed : juce::Time::currentTimeMillis());
            std::vector<std::pair<AutomationTarget, float>> values;
            for (const auto& t : targets)
            {
                const auto r = (std::uint32_t) random.nextInt();
                if (action == A::randomize)
                    values.push_back ({ t, mlmenu::randomize (r) });
                else if (const auto v = engine.currentTargetValue (t); v >= 0.0f)
                    values.push_back ({ t, mlmenu::humanize (v, r) });
            }
            engine.applyTargetValues (values);
            break;
        }
        case A::cancel:
        case A::none:
            break;
    }
    if (on && mlmenu::endsMultilink (action, on))
        engine.setMultilink (false);
    if (action == A::linkToControllers || action == A::overrideGlobal)
        openRemoteControlSettings (engine, targets, action == A::overrideGlobal);
}

void addMultilinkControlsItems (juce::PopupMenu& menu, AudioEngine& engine)
{
    // FL's MLWheelMenu (round 190df): its caption, sections, items and laws [MLWheelMenuPopup 0x1234ae0; live shot
    // link190bt-09a], acting on the queued controls or, with multilink off, on the last two tweaked parameters
    const auto targets = multilinkMenuTargets (engine);
    const auto on = engine.isMultilinkOn();
    const auto count = (int) targets.size();
    auto first = true;
    for (const auto& e : mlmenu::build (on, count))
    {
        using K = mlmenu::Entry::Kind;
        if (e.kind == K::separator)
            menu.addSeparator();
        else if (e.kind == K::caption)
        {
            if (first && on && count > 0) // (FL's "%d parameter(s) selected")
                menu.addSectionHeader (count < 2 ? tr ("{0} parameter selected", { juce::String (count) })
                                                 : tr ("{0} parameters selected", { juce::String (count) }));
            else
                menu.addSectionHeader (tr (e.text.c_str()));
            first = false;
        }
        else
            menu.addItem (tr (e.text.c_str()), e.enabled, false, [&engine, action = e.action] {
                runMultilinkAction (engine, action, multilinkMenuTargets (engine), 0);
            });
    }
}

juce::PopupMenu multilinkButtonMenu (AudioEngine& engine, const std::function<void (juce::PopupMenu&)>& lastTweakedColumn)
{
    juce::PopupMenu menu;
    // Live check 190ew CTL-1 (FL 2026 shot CTL-1-multilink-off-menu.png): with multilink off FL shows two columns, the
    // last tweaked control's own menu (captioned with its name) on the left, then the menu of the last tweaked
    // parameters; LYNX's greyed one-step items are not shown then (mlmenu::showsLynxOneStepItems)
    const auto on = engine.isMultilinkOn();
    if (mlmenu::showsOwnColumn (on, engine.getLastTweaked (0).has_value()) && lastTweakedColumn != nullptr)
    {
        lastTweakedColumn (menu);
        menu.addColumnBreak();
    }
    // FL's menu, in FL's order (live shot link190bt-09a); LYNX's own one-step items (round 177b-4: the queued controls
    // link to the next controllers moved) after it, below a line, while multilink is on
    addMultilinkControlsItems (menu, engine);
    if (mlmenu::showsLynxOneStepItems (on))
    {
        menu.addSeparator();
        menu.addItem (tr ("Link to controller"), [&engine] { engine.multilinkToControllers (false); });
        menu.addItem (tr ("Override global links"), [&engine] { engine.multilinkToControllers (true); });
    }
    return menu;
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
    // "Special": of FL's four, the key link (Note C0 = number 256; round 190cs). Channel / key aftertouch and the pitch
    // bend wheel are left out: LYNX's links cannot follow them.
    menu.addSectionHeader (tr ("Special"));
    menu.addItem (tr ("Note C0"), [pick] { pick (linkNoteBase); });
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
