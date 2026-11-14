#include "ui/WindowOwner.h"
#include "ui/PluginWindow.h"
#include "ui/PluginPicker.h"
#include "ui/PluginPresetMenu.h"
#include "ui/PluginThumbs.h"

#include <algorithm>
#include <map>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

#include "core/CrashReport.h"
#include "core/FlControlMenus.h"
#include "core/MidiControls.h"
#include "core/EditorLimit.h"
#include "core/PinOnTop.h"
#include "core/Preferences.h"
#include "core/FlWrapper.h"
#include "core/MixerOps.h"
#include "core/LastTweaked.h"
#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"
#include "engine/Text.h"
#include "platform/CrashGuard.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/NativeChild.h"
#include "ui/LiveRemoteUi.h"
#include "ui/PluginHeader.h"
#include "ui/RemoteControl.h"
#include "ui/Workspace.h"
#include "platform/Paths.h"
#include "ui/Theme.h"

#if JUCE_WINDOWS
namespace juce
{
// cmake/LynxPatchJuce.cmake fix 15 (juce_events, Windows): the message loop's DispatchMessage through this hook
extern void (*lynxDispatchMessageHook) (const void*);
} // namespace juce
#endif

namespace lynx
{
namespace
{
void toggleWrapperOption (AudioEngine& engine, const std::function<PluginRef* (Song&)>& find, juce::AudioPluginInstance* instance,
                          bool PluginRef::*option, bool recreates);

// FL's wrapper Processing › Connections, one column (round 188d, issue #114; FL 2026's page [UI fl188d a31, b23, k65],
// Fruity Wrapper FUN_0078df00 / FUN_00794c70 [DECOMP]): one row per plugin output (or input) with its LED (active),
// "n. name" and the value wheel ("---" = 0; wheel or vertical drag, double-click types a value) — a generator's
// output: the mixer track offset from the channel's insert; an effect's input / output: the sidechain index. The
// outputs column has the LED left and the wheel right; the inputs column is its mirror. Inactive rows are dimmed.
class ConnectionRows final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int rowHeight = 27;
    bool inputSide = false;  // the inputs column (an effect's)
    bool generator = true;   // a generator's outputs (mixer track offsets) or an effect's rows (sidechain indexes)
    std::vector<PluginOutputInfo> outputs; // (the rows: outputs, or the inputs of the inputs column)
    std::function<PluginOutputRoute (int)> route;
    std::function<void (int, bool)> setActive;
    std::function<void (int, int)> setOffset;
    std::function<bool()> processInactive; // "Process inactive inputs and outputs": an inactive row is not dimmed

    int preferredHeight() const { return (int) outputs.size() * rowHeight + 4; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        for (int k = 0; k < (int) outputs.size(); ++k)
        {
            const auto r = route ? route (k) : PluginOutputRoute {};
            const auto active = on (k);
            const auto plays = active || (processInactive && processInactive()); // (FL [UI fl188d k29])
            const auto rowArea = rowBounds (k);
            // LED: the accent when active (FL's orange LED), hollow grey when not
            const auto led = ledBounds (k).toFloat();
            g.setColour (active ? colour::accent : colour::ledOff);
            g.fillEllipse (led);
            g.setColour (active ? colour::accentHover : colour::borderStrong);
            g.drawEllipse (led.reduced (0.5f), 1.0f);
            if (hot == k && hotPart == Part::led)
            {
                g.setColour (colour::textSecondary.withAlpha (0.7f));
                g.drawEllipse (led.expanded (2.0f), 1.0f);
            }
            auto text = inputSide ? rowArea.withTrimmedLeft (wheelWidth + 8).withTrimmedRight (24)
                                  : rowArea.withTrimmedLeft (ledBounds (k).getRight() - rowArea.getX() + 6).withTrimmedRight (wheelWidth + 10);
            g.setColour (plays ? colour::textPrimary : colour::textDisabled);
            g.setFont (font (type::body));
            g.drawFittedText (juce::String (k + 1) + ". " + label (k), text, juce::Justification::centredLeft, 1);
            const auto box = wheelBounds (k).toFloat();
            g.setColour (drawDigitDisplay (g, box, (hot == k && hotPart == Part::wheel) || dragRow == k, ! plays, true));
            g.setFont (font (type::body, true));
            g.drawText (r.offset == 0 ? juce::String ("---") : juce::String (r.offset),
                        box.withTrimmedRight ((float) digitMarksWidth + 2.0f).toNearestInt(), juce::Justification::centredRight, false);
        }
    }

    juce::String getTooltip() override
    {
        const auto k = hot;
        if (k < 0 || k >= (int) outputs.size())
            return {};
        const auto n = juce::String (k + 1);
        if (hotPart == Part::led)
        {
            if (inputSide)
                return on (k) ? tr ("Deactivate input {0}", { n }) : tr ("Activate input {0}", { n });
            return on (k) ? tr ("Deactivate output {0}", { n }) : tr ("Activate output {0}", { n });
        }
        if (hotPart == Part::wheel)
            return wheelHint (k);
        return {};
    }

    void mouseMove (const juce::MouseEvent& e) override { track (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override
    {
        hot = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        track (e.getPosition());
        if (hot < 0)
            return;
        if (hotPart == Part::led && setActive && route)
            setActive (hot, ! on (hot));
        else if (hotPart == Part::wheel && route)
        {
            dragRow = hot;
            dragStart = route (hot).offset;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragRow < 0 || ! setOffset)
            return;
        // FL's wheel: up = more, one step per 4 pixels (Shift: finer)
        const auto step = e.mods.isShiftDown() ? 12 : 4;
        const auto value = juce::jlimit (-999, 999, dragStart - e.getDistanceFromDragStartY() / step);
        if (route && value != route (dragRow).offset)
            setOffset (dragRow, value);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragRow = -1;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        track (e.getPosition());
        if (hot < 0 || hotPart != Part::wheel || ! setOffset || ! route || w.deltaY == 0.0f)
        {
            Component::mouseWheelMove (e, w); // (the list scrolls)
            return;
        }
        setOffset (hot, juce::jlimit (-999, 999, route (hot).offset + (w.deltaY > 0.0f ? 1 : -1)));
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        track (e.getPosition());
        if (hot < 0 || hotPart != Part::wheel || ! route)
            return;
        const auto k = hot;
        auto* window = new juce::AlertWindow (wheelHint (k), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("value", juce::String (route (k).offset));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ConnectionRows> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, k] (int result) {
            if (result == 1 && safe != nullptr && safe->setOffset)
                safe->setOffset (k, juce::jlimit (-999, 999, window->getTextEditorContents ("value").trim().getIntValue()));
        }), true);
    }

private:
    enum class Part { none, led, name, wheel };
    bool on (int k) const { return route && route (k).isActive (outputs[(std::size_t) k].activeByDefault); }
    static constexpr int wheelWidth = 42;
    int hot = -1, dragRow = -1, dragStart = 0;
    Part hotPart = Part::none;

    juce::String wheelHint (int k) const
    {
        const auto n = juce::String (k + 1);
        if (generator)
            return tr ("Mixer track offset for output {0}", { n });
        return inputSide ? tr ("Sidechain index for input {0}", { n }) : tr ("Sidechain index for output {0}", { n });
    }
    juce::String label (int k) const
    {
        const auto& name = outputs[(std::size_t) k].name;
        return name.isNotEmpty() ? name : (inputSide ? tr ("Input {0}", { juce::String (k + 1) }) : tr ("Output {0}", { juce::String (k + 1) }));
    }
    // the column fills this component (the page places the inputs column left, the outputs column right)
    juce::Rectangle<int> rowBounds (int k) const { return { 4, 2 + k * rowHeight, juce::jmax (0, getWidth() - 8), rowHeight }; }
    juce::Rectangle<int> ledBounds (int k) const
    {
        return (inputSide ? rowBounds (k).removeFromRight (14) : rowBounds (k).removeFromLeft (14)).withSizeKeepingCentre (12, 12);
    }
    juce::Rectangle<int> wheelBounds (int k) const
    {
        return (inputSide ? rowBounds (k).removeFromLeft (wheelWidth) : rowBounds (k).removeFromRight (wheelWidth)).reduced (0, 3);
    }

    void track (juce::Point<int> p)
    {
        auto k = -1;
        auto part = Part::none;
        for (int i = 0; i < (int) outputs.size(); ++i)
            if (rowBounds (i).contains (p))
            {
                k = i;
                part = ledBounds (i).expanded (4).contains (p) ? Part::led : wheelBounds (i).contains (p) ? Part::wheel : Part::name;
            }
        if (k != hot || part != hotPart)
        {
            hot = k;
            hotPart = part;
            repaint();
        }
    }
};

// FL's plugin wrapper settings page (REA rounds 146a / 149), in LYNX's style with the tabs and groups of FL's wrapper
// form (Fruity Wrapper's DFM [DATA], FL 2026's page [EXEC]): Settings (Info with the latency, MIDI ports), Processing
// (Options) and Troubleshooting (Compatibility: the fixed buffers; Location). Options that re-create the plugin keep
// its state and close this window (open it again from the channel or the slot).
class WrapperSettingsPage final : public juce::Component, private juce::ChangeListener
{
public:
    // FL's Typing keyboard priority (the window caption's keyboard button, round 187a): on = the plugin gets keys first
    bool pluginKeysFirst() const
    {
        const auto* ref = find (const_cast<Song&> (engine.getSong()));
        return ref == nullptr || ! ref->hostKeysFirst;
    }
    void setPluginKeysFirst (bool on)
    {
        engine.editSong ([this, on] (Song& song) {
            if (auto* ref = find (song))
                ref->hostKeysFirst = ! on;
        }, false);
        engine.markModified();
    }
    bool keysReachHost() const { return sandboxedPluginProcessId (plugin) == 0 && ! isSandboxedPluginDead (plugin); }

    bool hostProcessesKeys() const
    {
        const auto* ref = find (const_cast<Song&> (engine.getSong()));
        return ref != nullptr && ref->hostKeysFirst && sandboxedPluginProcessId (plugin) == 0 && ! isSandboxedPluginDead (plugin);
    }

    // The channel whose plugin this is (-1: a mixer slot's, or gone)
    int channelIndex() const
    {
        auto& song = const_cast<Song&> (engine.getSong());
        const auto* ref = find (song);
        for (std::size_t c = 0; ref != nullptr && c < song.channels.size(); ++c)
            if (&song.channels[c].plugin == ref)
                return (int) c;
        return -1;
    }
    int channelMixerTrack (int channel) const
    {
        const auto& song = engine.getSong();
        return channel >= 0 && channel < (int) song.channels.size() ? song.channels[(std::size_t) channel].mixerTrack : -1;
    }

    WrapperSettingsPage (AudioEngine& e, std::function<PluginRef* (Song&)> f, juce::AudioPluginInstance& p, bool channel, int track = -1,
                         int slot = -1)
        : engine (e), find (std::move (f)), plugin (p), instrumentChannel (channel), mixerTrack (track), mixerSlot (slot)
    {
        const char* tabNames[] = { TR_NOOP ("Settings"), TR_NOOP ("Processing"), TR_NOOP ("Troubleshooting") };
        for (int t = 0; t < 3; ++t)
        {
            auto* b = tabs.add (new juce::TextButton (tr (tabNames[t])));
            b->setClickingTogglesState (false); // LYNX's tab look (as the channel window's): the shown one filled
            b->onClick = [this, t] { showTab (t); };
            addAndMakeVisible (b);
        }
        const auto& options = wrapperOptions();
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            auto* t = toggles.add (new juce::ToggleButton (tr (options[i].label)));
            t->setTooltip (tr (options[i].hint));
            t->setColour (juce::ToggleButton::textColourId, theme::colour::textPrimary);
            t->setColour (juce::ToggleButton::tickColourId, theme::colour::accent);
            t->onClick = [this, i] {
                auto* en = &engine;
                auto fn = find;
                auto* inst = &plugin;
                const auto& o = wrapperOptions()[i];
                juce::MessageManager::callAsync ([en, fn, inst, member = o.member, re = o.recreates] { toggleWrapperOption (*en, fn, inst, member, re); });
            };
            addChildComponent (t);
        }
        for (auto* box : { &inPort, &outPort })
        {
            box->addItem (juce::String::fromUTF8 ("\xe2\x80\x94"), 1);
            for (int i = 0; i < 256; ++i)
                box->addItem (juce::String (i), i + 2);
            addChildComponent (box);
        }
        for (int st = 1; st <= 48; ++st)
            bendRange.addItem (juce::String (st), st);
        bendRange.setTooltip (tr ("The pitch bend range sent to the plugin (semitones)"));
        bendRange.onChange = [this] {
            // as the toggles beside it: the new instance takes the range, and starts from the current settings
            juce::MessageManager::callAsync ([en = &engine, fn = find, inst = &plugin, v = bendRange.getSelectedId()] {
                if (PluginWindows::isOpen (*inst))
                    PluginWindows::close (*inst);
                en->capturePluginStates();
                en->editSong ([fn, v] (Song& song) {
                    if (auto* ref = fn (song); ref != nullptr && v > 0)
                        ref->bendRange = v;
                });
            });
        };
        addChildComponent (bendRange);
        // round 190ao (pain point 12; LYNX's own, FL's wrapper has no velocity setting): how notes played into this
        // instrument from MIDI devices reach it. The first item follows Settings › MIDI › Velocity (the default)
        velocityMode.addItem (tr ("Follow the MIDI settings"), 1);
        velocityMode.addItem (tr ("As played"), 2);
        velocityMode.addItem (tr ("Soft (light playing sounds louder)"), 3);
        velocityMode.addItem (tr ("Hard (needs harder playing)"), 4);
        velocityMode.addItem (tr ("Fixed"), 5);
        velocityMode.setTooltip (tr ("Velocity of the notes played into this plugin from MIDI devices: the curve of the MIDI settings, "
                                     "or this plugin's own curve or fixed velocity"));
        velocityMode.onChange = [this] {
            engine.editSong ([fn = find, mode = velocityMode.getSelectedId() - 1] (Song& song) {
                if (auto* ref = fn (song); ref != nullptr && mode >= 0)
                    ref->velocityMode = prefs::clampPluginVelocityMode (mode);
            });
        };
        addChildComponent (velocityMode);
        for (int v = 1; v <= 127; ++v)
            fixedVelocity.addItem (juce::String (v), v);
        fixedVelocity.setTooltip (tr ("The velocity every played note gets (1 to 127)"));
        fixedVelocity.onChange = [this] {
            engine.editSong ([fn = find, v = fixedVelocity.getSelectedId()] (Song& song) {
                if (auto* ref = fn (song); ref != nullptr && v > 0)
                    ref->fixedVelocity = prefs::clampFixedVelocity (v);
            });
        };
        addChildComponent (fixedVelocity);
        inPort.setTooltip (tr ("MIDI input port number"));
        inPort.onChange = [this] { setPort (inPort.getSelectedId() - 2, false); };
        // FL's Output port: the notes the plugin sends out play the plugins on that input port (instruments in the
        // Channel rack that send MIDI)
        outPort.setTooltip (tr ("MIDI output port number: the notes this plugin sends play the plugins whose input port it is"));
        outPort.onChange = [this] { setPort (outPort.getSelectedId() - 2, true); };
        outPort.setEnabled (channel && plugin.producesMidi());
        // FL's Info › Latency: what the plugin reports, or set by hand (menu: Reset, Set in ms / samples / beats)
        latency.setTooltip (tr ("Latency used by the delay compensation: what the plugin reports, or a value set by hand"));
        latency.onClick = [this] { showLatencyMenu(); };
        addChildComponent (latency);
        // FL's Settings › Options › "Bit size" (round 176a, Windows x64): which build of a plugin installed as both
        // 32-bit and 64-bit loads [UI fl36c]. Changing it re-creates the plugin with its state, like "Make bridged".
        bitSize.addItem (tr ("Native"), 1);
        bitSize.addItem (tr ("32 bit"), 2);
        bitSize.addItem (tr ("64 bit"), 3);
        bitSize.setTooltip (tr ("Which build of the plugin loads when both a 32-bit and a 64-bit one are installed"));
        bitSize.onChange = [this] {
            const auto value = bitSize.getSelectedId() == 2 ? 32 : bitSize.getSelectedId() == 3 ? 64 : 0;
            auto* ref = find (const_cast<Song&> (engine.getSong()));
            if (ref == nullptr || ref->bitSize == value)
                return;
            juce::MessageManager::callAsync ([en = &engine, fn = find, inst = &plugin, value] {
                if (PluginWindows::isOpen (*inst))
                    PluginWindows::close (*inst);
                en->capturePluginStates(); // the other build starts from the current settings
                en->editSong ([fn, value] (Song& song) {
                    if (auto* r = fn (song))
                        r->bitSize = value;
                });
            });
        };
        addChildComponent (bitSize);
        // Processing › Connections (round 188d): a generator's outputs, each to the insert its offset from the
        // channel's says. FL's header: Reset, Auto map outputs, the outputs glyph and Refresh at the right [UI fl188d].
        // An effect on an insert (FL [UI fl188d k65]): its inputs column too, each input's sidechain index into the
        // insert's inputs list (core/MixerOps.h insertInputs); input 2 is the side input LYNX feeds from the slot's
        // sidechain, which the index sets. Header: inputs glyph, Auto map inputs, Reset, Auto map outputs, outputs glyph.
        effectSlot = ! channel && mixerTrack > 0 && mixerSlot >= 0;
        connections = (channel && plugin.getTotalNumOutputChannels() > 0) || (effectSlot && plugin.getBusCount (true) > 0);
        outputRows.generator = channel;
        inputRows.inputSide = true;
        inputRows.generator = false;
        inputRows.outputs = effectSlot ? pluginInputs (plugin) : std::vector<PluginOutputInfo> {};
        inputRows.route = [this] (int k) { return inputRoute (k); };
        inputRows.setActive = [this] (int k, bool on) {
            editInputs ([k, on] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].active = (std::int8_t) (on ? -1 : 0); });
        };
        inputRows.setOffset = [this] (int k, int v) {
            editInputs ([k, v] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].offset = juce::jmax (0, v); });
        };
        inputRows.processInactive = outputRows.processInactive = [this] {
            auto* ref = find (const_cast<Song&> (engine.getSong()));
            return ref != nullptr && ref->processInactive;
        };
        columns.addAndMakeVisible (inputRows);
        columns.addAndMakeVisible (outputRows);
        autoMapIn.setButtonText (tr ("Auto map inputs"));
        autoMapIn.setTooltip (tr ("Assign all inputs incrementally"));
        // (FL's handler is the outputs' one, FUN_0078aea0 with Tag 0)
        autoMapIn.onClick = [this] {
            editInputs ([n = (int) inputRows.outputs.size()] (std::vector<PluginOutputRoute>& r) { autoMapOutputs (r, n); });
        };
        addChildComponent (autoMapIn);
        outputRows.outputs = pluginOutputs (plugin);
        outputRows.route = [this] (int k) {
            auto* ref = find (const_cast<Song&> (engine.getSong()));
            return ref != nullptr ? ref->outputRoute (k) : PluginOutputRoute {};
        };
        outputRows.setActive = [this] (int k, bool on) {
            // (stored as the plugin's own default when it is one: −1)
            const auto declared = outputRows.outputs[(std::size_t) k].activeByDefault;
            editRoutes ([k, on, declared] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].active = (std::int8_t) (on == declared ? -1 : on ? 1 : 0); },
                        ! on); // FL [EXEC fl188d a38, b23, k19]: turning an LED off turns "Process inactive inputs and outputs" off
        };
        outputRows.setOffset = [this] (int k, int v) { editRoutes ([k, v] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].offset = v; }); };
        connectionsView.setViewedComponent (&columns, false);
        connectionsView.setScrollBarsShown (true, false);
        connectionsView.setScrollBarThickness (8);
        addChildComponent (connectionsView);
        resetIo.setButtonText (tr ("Reset"));
        resetIo.setTooltip (tr ("Set all inputs and outputs to their default values"));
        // FL's Reset [EXEC]: every offset back to 0 ("---"); the LEDs stay as they are
        resetIo.onClick = [this] {
            editRoutes ([n = (int) outputRows.outputs.size()] (std::vector<PluginOutputRoute>& r) { resetOutputOffsets (r, n); });
            if (effectSlot)
                editInputs ([n = (int) inputRows.outputs.size()] (std::vector<PluginOutputRoute>& r) { resetOutputOffsets (r, n); });
        };
        addChildComponent (resetIo);
        autoMap.setButtonText (tr ("Auto map outputs"));
        autoMap.setTooltip (tr ("Assign all outputs incrementally"));
        // FL's Auto map outputs (FUN_0078aea0 [DECOMP], checked [EXEC]): from the first output's offset, one more for
        // each next output, inactive ones included
        autoMap.onClick = [this] {
            editRoutes ([n = (int) outputRows.outputs.size()] (std::vector<PluginOutputRoute>& r) { autoMapOutputs (r, n); });
        };
        addChildComponent (autoMap);
        {
            // FL's refresh glyph: a circular arrow
            juce::Path arrow;
            arrow.addCentredArc (0.0f, 0.0f, 6.0f, 6.0f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            juce::Path shape;
            juce::PathStrokeType (1.6f).createStrokedPath (shape, arrow);
            shape.addTriangle (2.2f, -8.8f, 6.8f, -5.2f, 1.6f, -2.6f);
            refreshIo.setShape (shape, true, true, false);
            refreshIo.setOutline (juce::Colours::transparentBlack, 0.0f);
            refreshIo.setColours (theme::colour::textSecondary, theme::colour::textPrimary, theme::colour::accent);
        }
        refreshIo.setTooltip (tr ("Refresh inputs and outputs (after a change in the plugin)"));
        refreshIo.onClick = [this] {
            outputRows.outputs = pluginOutputs (plugin);
            if (effectSlot)
                inputRows.outputs = pluginInputs (plugin);
            resized();
        };
        addChildComponent (refreshIo);
        locate.setButtonText (tr ("Locate"));
        locate.setTooltip (tr ("Show the plugin's file in the file manager"));
        locate.onClick = [this] { juce::File (plugin.getPluginDescription().fileOrIdentifier).revealToUser(); };
        addChildComponent (locate);
        engine.addChangeListener (this);
        refresh();
        showTab (0);
        setSize (520, preferredHeight());
    }

    ~WrapperSettingsPage() override { engine.removeChangeListener (this); }

    // (+ Options › Bit size where it applies; + the MIDI group's Velocity row for an instrument, round 190ao)
    int preferredHeight() const { return (bridge32Supported() ? 474 : 410) + (instrumentChannel ? rowHeight : 0); }
    void selectTab (int t) { showTab (juce::jlimit (0, 2, t)); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        // the plugin's own version; nothing when it reports none (VST2's effGetVendorVersion 0 arrives as "0")
        if (const auto version = plugin.getPluginDescription().version.trim(); version.containsAnyOf ("123456789"))
            g.drawText (tr ("version {0}", { version }), getWidth() - 200, 8, 184, tabHeight, juce::Justification::centredRight);
        for (const auto& [area, title] : groups)
        {
            g.setColour (colour::bg2);
            g.fillRoundedRectangle (area.toFloat(), radius::md);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption, true));
            g.drawText (caps (title), area.withHeight (headerHeight).reduced (10, 0), juce::Justification::centredLeft);
        }
        if (tab == 1 && connections && ! inputsGlyph.isEmpty()) // FL's inputs glyph: an arrow entering a box
        {
            const auto b = inputsGlyph.toFloat().withSizeKeepingCentre (14.0f, 12.0f);
            juce::Path p;
            p.addRoundedRectangle (b.getRight() - b.getWidth() * 0.55f, b.getY(), b.getWidth() * 0.55f, b.getHeight(), 2.0f);
            g.setColour (colour::textSecondary);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            juce::Path arrow;
            arrow.startNewSubPath (b.getX(), b.getCentreY());
            arrow.lineTo (b.getX() + b.getWidth() * 0.62f, b.getCentreY());
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            const auto tip = b.getX() + b.getWidth() * 0.66f;
            head.addTriangle (tip + 1.0f, b.getCentreY(), tip - 4.0f, b.getCentreY() - 4.0f, tip - 4.0f, b.getCentreY() + 4.0f);
            g.fillPath (head);
        }
        if (tab == 1 && connections && ! outputsGlyph.isEmpty()) // FL's outputs glyph: an arrow leaving a box
        {
            const auto b = outputsGlyph.toFloat().withSizeKeepingCentre (14.0f, 12.0f);
            juce::Path p;
            p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth() * 0.55f, b.getHeight(), 2.0f);
            g.setColour (colour::textSecondary);
            g.strokePath (p, juce::PathStrokeType (1.4f));
            juce::Path arrow;
            arrow.startNewSubPath (b.getX() + b.getWidth() * 0.35f, b.getCentreY());
            arrow.lineTo (b.getRight(), b.getCentreY());
            g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.addTriangle (b.getRight() + 1.0f, b.getCentreY(), b.getRight() - 4.0f, b.getCentreY() - 4.0f, b.getRight() - 4.0f, b.getCentreY() + 4.0f);
            g.fillPath (head);
        }
        g.setFont (font (type::body));
        for (const auto& [area, text, primary] : labels)
        {
            g.setColour (primary ? colour::textPrimary : colour::textSecondary);
            g.drawFittedText (text, area, juce::Justification::centredLeft, 1);
        }
    }

    void resized() override
    {
        groups.clear();
        labels.clear();
        auto area = getLocalBounds().reduced (10, 8);
        auto bar = area.removeFromTop (tabHeight);
        for (auto* t : tabs)
            t->setBounds (bar.removeFromLeft (juce::jmax (80, t->getBestWidthForHeight (tabHeight) + 16)));
        area.removeFromTop (8);
        for (auto* c : getChildren())
            if (! tabs.contains (dynamic_cast<juce::TextButton*> (c)))
                c->setVisible (false);

        auto group = [this] (juce::Rectangle<int>& from, int height, const juce::String& title) {
            auto r = from.removeFromTop (height);
            from.removeFromTop (8);
            groups.push_back ({ r, title });
            return r.withTrimmedTop (headerHeight).reduced (10, 4);
        };
        auto label = [this] (juce::Rectangle<int> r, const juce::String& text, bool primary = false) { labels.push_back ({ r, text, primary }); };
        auto show = [] (juce::Component& c, juce::Rectangle<int> r) {
            c.setBounds (r);
            c.setVisible (true);
        };

        if (tab == 0)
        {
            const auto d = plugin.getPluginDescription();
            auto info = group (area, headerHeight + 6 * rowHeight + 8, tr ("Info"));
            const std::pair<juce::String, juce::String> lines[] = {
                { tr ("Name"), d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String()) },
                { tr ("Type"), d.pluginFormatName + (d.isInstrument ? "  " + tr ("instrument") : "  " + tr ("effect")) + bitsText() },
                { tr ("Category"), d.category },
                { tr ("Inputs / outputs"), juce::String (plugin.getTotalNumInputChannels()) + " / " + juce::String (plugin.getTotalNumOutputChannels()) },
                { tr ("Parameters"), juce::String (plugin.getParameters().size()) },
            };
            for (const auto& [name, value] : lines)
            {
                auto row = info.removeFromTop (rowHeight);
                label (row.removeFromLeft (110), name);
                label (row, value, true);
            }
            auto row = info.removeFromTop (rowHeight);
            label (row.removeFromLeft (110), tr ("Latency"));
            show (latency, row.removeFromLeft (260).reduced (0, 2));

            int midiRows = 0;
            for (const auto& o : wrapperOptions())
                midiRows += juce::String (o.section) == "MIDI" ? 1 : 0;
            auto midi = group (area, headerHeight + (1 + midiRows + (instrumentChannel ? 1 : 0)) * rowHeight + 8, tr ("MIDI"));
            auto line = midi.removeFromTop (rowHeight);
            label (line.removeFromLeft (80), tr ("Input port"));
            show (inPort, line.removeFromLeft (70).reduced (0, 2));
            line.removeFromLeft (30);
            label (line.removeFromLeft (90), tr ("Output port"));
            show (outPort, line.removeFromLeft (70).reduced (0, 2));
            // FL's MIDI options (round 151): release velocity, the pitch bend range RPN (with its value), MOD X as
            // polyphonic aftertouch, All Notes Off on stop
            for (std::size_t i = 0; i < wrapperOptions().size(); ++i)
                if (juce::String (wrapperOptions()[i].section) == "MIDI")
                {
                    auto row = midi.removeFromTop (rowHeight);
                    if (wrapperOptions()[i].member == &PluginRef::sendBendRange)
                        show (bendRange, row.removeFromRight (70).reduced (0, 2));
                    show (*toggles[(int) i], row);
                }
            if (instrumentChannel) // LYNX's own per-plugin velocity, under FL's MIDI options (round 190ao)
            {
                auto row = midi.removeFromTop (rowHeight);
                label (row.removeFromLeft (80), tr ("Velocity"));
                show (velocityMode, row.removeFromLeft (250).reduced (0, 2));
                row.removeFromLeft (8);
                show (fixedVelocity, row.removeFromLeft (70).reduced (0, 2));
            }
            if (bridge32Supported())
            {
                auto options = group (area, headerHeight + rowHeight + 8, tr ("Options"));
                auto row = options.removeFromTop (rowHeight);
                label (row.removeFromLeft (80), tr ("Bit size"));
                show (bitSize, row.removeFromLeft (110).reduced (0, 2));
            }
        }
        else
        {
            const juce::String section = tab == 1 ? "Processing" : "Compatibility";
            int rows = 0;
            for (std::size_t i = 0; i < wrapperOptions().size(); ++i)
                rows += section == wrapperOptions()[i].section && ! hiddenOption (i) ? 1 : 0;
            auto box = group (area, headerHeight + rows * rowHeight + 8, tr (tab == 1 ? "Options" : "Compatibility"));
            for (std::size_t i = 0; i < wrapperOptions().size(); ++i)
                if (section == wrapperOptions()[i].section && ! hiddenOption (i))
                    show (*toggles[(int) i], box.removeFromTop (rowHeight).withTrimmedLeft (wrapperOptions()[i].needsFixed ? 22 : 0));
            outputsGlyph = inputsGlyph = {};
            if (tab == 1 && connections)
            {
                // the group's header line carries the buttons (FL: Reset, Auto map outputs, the glyph; Refresh at
                // the right); the rows scroll below
                auto r = area.withTrimmedBottom (2);
                groups.push_back ({ r, tr ("Connections") });
                auto header = r.removeFromTop (headerHeight + 6).reduced (10, 3);
                show (refreshIo, header.removeFromRight (26).reduced (4, 3));
                header.removeFromRight (effectSlot ? 6 : juce::jmax (0, header.getWidth() / 2 - 150));
                header.removeFromLeft (100); // (the group's caption)
                outputsGlyph = header.removeFromRight (22);
                header.removeFromRight (4);
                show (autoMap, header.removeFromRight (effectSlot ? juce::jmin (112, header.getWidth())
                                                                  : juce::jmin (juce::jmax (110, autoMap.getBestWidthForHeight (header.getHeight()) + 16), header.getWidth())));
                header.removeFromRight (4);
                show (resetIo, header.removeFromRight (effectSlot ? 56 : juce::jmax (56, resetIo.getBestWidthForHeight (header.getHeight()) + 16)));
                if (effectSlot)
                {
                    header.removeFromRight (4);
                    show (autoMapIn, header.removeFromRight (juce::jmin (juce::jmax (110, autoMapIn.getBestWidthForHeight (header.getHeight()) + 16), juce::jmax (0, header.getWidth() - 26))));
                    header.removeFromRight (4);
                    inputsGlyph = header.removeFromRight (22);
                }
                auto list = r.reduced (4, 4);
                show (connectionsView, list);
                const auto rows = juce::jmax (outputRows.preferredHeight(), inputRows.preferredHeight());
                columns.setSize (list.getWidth() - (rows > list.getHeight() ? 10 : 0), juce::jmax (list.getHeight(), rows));
                // FL: inputs in the left half, outputs from just left of the middle to the right edge
                const auto split = columns.getWidth() / 2 - 20;
                inputRows.setBounds (0, 0, juce::jmax (0, split - 30), columns.getHeight());
                outputRows.setBounds (split, 0, columns.getWidth() - split, columns.getHeight());
                inputRows.setVisible (effectSlot);
                outputRows.repaint();
                inputRows.repaint();
            }
            if (tab == 2)
            {
                auto where = group (area, headerHeight + rowHeight + 8, tr ("Location"));
                show (locate, where.removeFromRight (90).withHeight (rowHeight).reduced (0, 2));
                label (where.removeFromLeft (50).withHeight (rowHeight), tr ("Path"));
                label (where.withHeight (rowHeight), plugin.getPluginDescription().fileOrIdentifier, true);
            }
        }
        repaint();
    }

private:
    static constexpr int tabHeight = 24, headerHeight = 24, rowHeight = 24;

    // A 32-bit plugin always runs bridged: FL's Processing page has no "Make bridged" for it [UI fl25c vs fl34c]
    bool hiddenOption (std::size_t i) const
    {
        return wrapperOptions()[i].member == &PluginRef::separateProcess && isBridged32 (plugin);
    }

    // FL's Info › Type ends with the build and how it runs: "(32bit - bridged)", "(64bit - native)" [UI fl23c, fl33c]
    juce::String bitsText() const
    {
        if (! bridge32Supported())
            return {};
        if (isBridged32 (plugin))
            return "  (" + tr ("32bit - bridged") + ")";
        return "  (" + (sandboxedPluginProcessId (plugin) != 0 || isSandboxedPluginDead (plugin) ? tr ("64bit - bridged") : tr ("64bit - native")) + ")";
    }

    void showTab (int t)
    {
        tab = t;
        for (int k = 0; k < tabs.size(); ++k)
            tabs[k]->setToggleState (k == t, juce::dontSendNotification);
        resized();
    }

    void setPort (int value, bool output)
    {
        engine.editSong ([fn = find, value, output] (Song& song) {
            if (auto* ref = fn (song))
                (output ? ref->midiOutPort : ref->midiInPort) = value;
        });
    }

    double rate() const { return plugin.getSampleRate() > 0 ? plugin.getSampleRate() : 44100.0; }

    void setLatencyMs (float ms)
    {
        engine.editSong ([fn = find, ms] (Song& song) {
            if (auto* ref = fn (song))
                ref->latencyOverrideMs = ms;
        });
    }

    void showLatencyMenu()
    {
        // FL's latency menu [EXEC]: Reset, Set in ms, Set in samples, Set in beats (beats at the song's tempo)
        juce::PopupMenu menu;
        menu.addItem (tr ("Reset"), [this] { setLatencyMs (-1.0f); });
        auto ask = [this] (int unit) {
            auto* ref = find (const_cast<Song&> (engine.getSong()));
            const auto bpm = juce::jmax (10.0, (double) engine.getSong().bpm);
            const auto ms = ref != nullptr && ref->latencyOverrideMs >= 0.0f ? (double) ref->latencyOverrideMs
                                                                              : plugin.getLatencySamples() * 1000.0 / rate();
            const auto current = unit == 0 ? ms : unit == 1 ? ms * rate() / 1000.0 : ms * bpm / 60000.0;
            const char* titles[] = { TR_NOOP ("Latency in ms"), TR_NOOP ("Latency in samples"), TR_NOOP ("Latency in beats") };
            auto* window = new juce::AlertWindow (tr ("Latency"), tr (titles[unit]), juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("value", juce::String (current, unit == 1 ? 0 : 3));
            window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<WrapperSettingsPage> safe (this);
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, unit, bpm] (int result) {
                if (result != 1 || safe == nullptr)
                    return;
                const auto v = juce::jmax (0.0, window->getTextEditorContents ("value").getDoubleValue());
                const auto ms = unit == 0 ? v : unit == 1 ? v * 1000.0 / safe->rate() : v * 60000.0 / bpm;
                safe->setLatencyMs ((float) juce::jlimit (0.0, 2000.0, ms));
            }), true);
        };
        menu.addItem (tr ("Set in ms..."), [ask] { ask (0); });
        menu.addItem (tr ("Set in samples..."), [ask] { ask (1); });
        menu.addItem (tr ("Set in beats..."), [ask] { ask (2); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&latency));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    // An effect's input k as the page shows it: the stored route; input 2 without a stored index shows the slot's
    // sidechain source (picked with LYNX's "SC" picker) as its index in the insert's inputs list
    PluginOutputRoute inputRoute (int k) const
    {
        const auto& song = engine.getSong();
        auto* ref = find (const_cast<Song&> (song));
        auto route = ref != nullptr && k < (int) ref->inputs.size() ? ref->inputs[(std::size_t) k] : PluginOutputRoute {};
        if (k == 1 && route.offset == 0 && mixerTrack < (int) song.mixer.size() && mixerSlot < (int) song.mixer[(std::size_t) mixerTrack].effects.size())
            if (const auto source = song.mixer[(std::size_t) mixerTrack].effects[(std::size_t) mixerSlot].sidechain; source > 0)
                route.offset = mixerops::sidechainIndexOf (song, mixerTrack, source);
        return route;
    }

    // One edit of an effect's inputs; input 2's index then sets the slot's sidechain source (FL's index → the insert
    // it picks; 0 or inactive: none)
    void editInputs (std::function<void (std::vector<PluginOutputRoute>&)> change)
    {
        const auto count = inputRows.outputs.size();
        std::vector<PluginOutputRoute> shown;
        for (int k = 0; k < (int) count; ++k)
            shown.push_back (inputRoute (k));
        engine.editSong ([fn = find, count, shown, change = std::move (change), t = mixerTrack, s = mixerSlot] (Song& song) {
            auto* ref = fn (song);
            if (ref == nullptr || count == 0)
                return;
            ref->inputs = shown;
            change (ref->inputs);
            if (count > 1 && t > 0 && t < (int) song.mixer.size() && s >= 0 && s < (int) song.mixer[(std::size_t) t].effects.size())
            {
                const auto& side = ref->inputs[1];
                const auto source = side.isActive (true) ? mixerops::sidechainIndexSource (song, t, side.offset) : -1;
                song.mixer[(std::size_t) t].effects[(std::size_t) s].sidechain = source > 0 && source != t ? source : -1;
            }
            trimOutputRoutes (ref->inputs);
        });
        inputRows.repaint();
    }

    // Processing › Connections: one edit of the outputs' routes (as many entries as the plugin has outputs)
    void editRoutes (std::function<void (std::vector<PluginOutputRoute>&)> change, bool stopProcessingInactive = false)
    {
        const auto count = outputRows.outputs.size();
        engine.editSong ([fn = find, count, change = std::move (change), stopProcessingInactive] (Song& song) {
            if (auto* ref = fn (song); ref != nullptr && count > 0)
            {
                if (ref->outputs.size() < count)
                    ref->outputs.resize (count);
                change (ref->outputs);
                trimOutputRoutes (ref->outputs);
                if (stopProcessingInactive)
                    ref->processInactive = false;
            }
        });
        outputRows.repaint();
    }

    void refresh()
    {
        auto* ref = find (const_cast<Song&> (engine.getSong()));
        if (ref == nullptr)
            return;
        const auto& options = wrapperOptions();
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            toggles[(int) i]->setToggleState (ref->*(options[i].member), juce::dontSendNotification);
            toggles[(int) i]->setEnabled (! options[i].needsFixed || ref->fixedBuffers);
        }
        // with Options › "Run plugins in a separate process" every plugin already runs bridged
        for (std::size_t i = 0; i < options.size(); ++i)
            if (options[i].member == &PluginRef::separateProcess && pluginSandboxEnabled())
                toggles[(int) i]->setToggleState (true, juce::dontSendNotification), toggles[(int) i]->setEnabled (false);
        bitSize.setSelectedId (ref->bitSize == 32 ? 2 : ref->bitSize == 64 ? 3 : 1, juce::dontSendNotification);
        inPort.setSelectedId (ref->midiInPort + 2, juce::dontSendNotification);
        bendRange.setSelectedId (ref->bendRange, juce::dontSendNotification);
        bendRange.setEnabled (ref->sendBendRange);
        outPort.setSelectedId (ref->midiOutPort + 2, juce::dontSendNotification);
        velocityMode.setSelectedId (prefs::clampPluginVelocityMode (ref->velocityMode) + 1, juce::dontSendNotification);
        fixedVelocity.setSelectedId (prefs::clampFixedVelocity (ref->fixedVelocity), juce::dontSendNotification);
        fixedVelocity.setEnabled (ref->velocityMode == prefs::pluginVelocityFixed);
        outputRows.repaint();
        inputRows.repaint();
        const auto reported = plugin.getLatencySamples();
        const auto text = ref->latencyOverrideMs >= 0.0f
                              ? tr ("{0} ms (set by hand)", { juce::String (ref->latencyOverrideMs, 2) })
                              : (reported > 0 ? tr ("{0} samples ({1} ms)", { juce::String (reported), juce::String (reported * 1000.0 / rate(), 1) })
                                              : tr ("none"));
        latency.setButtonText (text + juce::String::fromUTF8 ("  \xe2\x96\xbe"));
    }

    struct Label
    {
        juce::Rectangle<int> area;
        juce::String text;
        bool primary = false;
    };

    AudioEngine& engine;
    std::function<PluginRef* (Song&)> find;
    juce::AudioPluginInstance& plugin;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::ComboBox inPort, outPort, bendRange, bitSize, velocityMode, fixedVelocity;
    juce::TextButton latency, locate;
    const bool instrumentChannel; // a Channel rack instrument: the MIDI group has LYNX's Velocity row
    // Processing › Connections (round 188d)
    bool connections = false;
    int mixerTrack = -1, mixerSlot = -1; // an effect's insert and slot (−1: a channel's generator)
    bool effectSlot = false;
    ConnectionRows inputRows, outputRows;
    juce::Component columns; // the two columns side by side, in the viewport
    juce::Viewport connectionsView;
    juce::TextButton resetIo, autoMap, autoMapIn;
    juce::Rectangle<int> inputsGlyph;
    juce::ShapeButton refreshIo { "refresh", {}, {}, {} };
    juce::Rectangle<int> outputsGlyph;
    int tab = 0;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> groups;
    std::vector<Label> labels;
};

// FL's Typing keyboard priority button in the plugin window's caption (FLEngine PluginForm.NewCaption.KeyboardFocusBtn,
// round 187a [UI shots/fl a00-a06]): a keyboard glyph left of the presets, bright while the plugin handles keys first
// (FL's default for a new plugin), dim when LYNX goes first; outlined under the mouse.
class KeyboardPriorityButton final : public juce::Button
{
public:
    KeyboardPriorityButton() : juce::Button ("Typing keyboard priority") { setClickingTogglesState (false); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.5f);
        if (over || down)
        {
            g.setColour (theme::colour::textSecondary);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
        }
        const auto on = getToggleState() && isEnabled();
        g.setColour (on ? theme::colour::textPrimary : theme::colour::textDisabled);
        const auto side = juce::jmin (r.getWidth(), r.getHeight()) * 0.62f;
        auto key = juce::Rectangle<float> (side, side).withCentre (r.getCentre());
        g.drawRoundedRectangle (key, 1.5f, 1.2f);
        // the 3 x 3 grid of keys
        const auto cell = (key.getWidth() - 3.0f) / 3.0f;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                g.fillRect (key.getX() + 1.5f + (float) col * cell + 0.6f, key.getY() + 1.5f + (float) row * cell + 0.6f, cell - 1.2f, cell - 1.2f);
    }
};

// Round 190ao (pain point 11, LYNX's own; FL has no pin): "Pin on top" at the right end of the wrapper header of a
// detached plugin window. A pushpin: upright and in the accent colour while the window is pinned, tilted and dim when not.
class PinButton final : public juce::Button
{
public:
    PinButton() : juce::Button ("Pin on top") { setClickingTogglesState (false); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        if (over || down)
        {
            g.setColour (theme::colour::textSecondary);
            g.drawRoundedRectangle (r, 4.0f, 1.2f);
        }
        const auto on = getToggleState() && isEnabled();
        juce::Path pin; // in a 16 x 16 box: cap, body, flange, needle
        pin.addRoundedRectangle (5.0f, 1.5f, 6.0f, 2.0f, 1.0f);
        pin.addRectangle (6.0f, 3.5f, 4.0f, 5.0f);
        pin.addRoundedRectangle (3.5f, 8.5f, 9.0f, 2.0f, 1.0f);
        pin.addRectangle (7.4f, 10.5f, 1.2f, 4.5f);
        const auto side = juce::jmin (r.getWidth(), r.getHeight()) * 0.78f;
        auto place = juce::AffineTransform::translation (-8.0f, -8.0f);
        if (! on)
            place = place.rotated (juce::MathConstants<float>::pi * 0.25f);
        place = place.scaled (side / 16.0f).translated (r.getCentreX(), r.getCentreY());
        g.setColour (! isEnabled() ? theme::colour::textDisabled : on ? theme::colour::accent : theme::colour::textSecondary);
        g.fillPath (pin, place);
    }
};

// Round 190ao: what a plugin window shows in place of the editor when its plugin went down (Bitwig turns the plugin's
// panel into a notice with "Reload plug-in"). Nothing reloads by itself: a plugin that crashes again shows this again.
class CrashNotice final : public juce::Component
{
public:
    CrashNotice()
    {
        reload.setButtonText (tr ("Reload"));
        reload.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        reload.onClick = [this] {
            if (onReload)
                onReload();
        };
        addAndMakeVisible (reload);
    }

    void setPluginName (const juce::String& name)
    {
        title = tr ("{0} crashed and was switched off.", { name });
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        auto r = getLocalBounds().reduced (16, 12);
        r.removeFromBottom (40);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawFittedText (title, r.removeFromTop (r.getHeight() / 2), juce::Justification::centredBottom, 2);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::body));
        g.drawFittedText (tr ("Reload starts it again with the settings from the last save or backup."), r.withTrimmedTop (4),
                          juce::Justification::centredTop, 3);
    }

    void resized() override { reload.setBounds (getLocalBounds().reduced (16, 12).removeFromBottom (28).withSizeKeepingCentre (140, 28)); }

    std::function<void()> onReload;

private:
    juce::String title;
    juce::TextButton reload;
};

// The window's content with FL's wrapper header: the plugin's editor, or the wrapper settings page (gear).
class WrappedEditor final : public juce::Component
{
public:
    WrappedEditor (juce::AudioProcessorEditor* e, AudioEngine& engine, std::function<PluginRef* (Song&)> find, juce::AudioPluginInstance& plugin,
                   bool channel, int scTrack = -1, int scSlot = -1)
        : engineRef (engine), hosted (plugin), sideTrack (scTrack), sideSlot (scSlot),
          editor (e), page (engine, std::move (find), plugin, channel, scTrack, scSlot), presets (plugin, [&engine] { engine.markModified(); })
    {
        // round 177b: a plugin with a sidechain input gets the picker in its header (the input is fed from the slot's
        // sidechain, engine/PluginHost.h PluginEffect::setSidechain)
        if (sideTrack >= 0 && sideSlot >= 0 && hasSideBus (plugin))
        {
            addAndMakeVisible (sidechain);
            sidechain.setTooltip (tr ("Sidechain input: the insert this plugin's side input listens to. Picking one routes it here at level 0; it keeps its own outputs"));
            sidechain.onClick = [this] {
                pluginheader::showSidechainPicker (engineRef, sideTrack, sideSlot, &sidechain);
            };
            refreshSidechain();
        }
        addAndMakeVisible (presets); // FL's wrapper presets: ◀ ▶ and the menu (round 162d, issue #8)
        // round 190ao: the preset menu's "Play a note when a preset loads" (instruments of the Channel rack)
        if (channel)
            presets.onLoaded = [&engine, &plugin] {
                if (pluginpresetui::auditionOnLoad())
                    engine.auditionPreset (plugin);
            };
        // FL's Typing keyboard priority, left of the presets (issue #96 slice 2). A plugin in another process has its
        // own keyboard (FL: bridged plugins always take it), so there it stays greyed.
        addAndMakeVisible (keyboardPriority);
        keyboardPriority.setTooltip (tr ("Allow the plugin to handle keystrokes before the host does"));
        keyboardPriority.onClick = [this] {
            page.setPluginKeysFirst (! page.pluginKeysFirst());
            refreshKeyboardPriority();
        };
        refreshKeyboardPriority();
        // round 190ao: "Pin on top" (the host turns it on when it is a detached window: setPin)
        addChildComponent (pin);
        pin.onClick = [this] {
            if (onPin)
                onPin (! pin.getToggleState());
        };
        for (auto* b : { &pluginTab, &settingsTab })
        {
            b->setClickingTogglesState (false); // LYNX's tab look (as the channel window's): the shown one filled
            addAndMakeVisible (b);
        }
        pluginTab.setTooltip (tr ("The plugin's own editor"));
        settingsTab.setTooltip (tr ("Plugin wrapper settings: processing, bridging and MIDI options"));
        pluginTab.onClick = [this] { show (false); };
        settingsTab.onClick = [this] { show (true); };
        addAndMakeVisible (editor.get());
        addChildComponent (page);
        addChildComponent (notice);
        notice.setPluginName (plugin.getName());
        pluginTab.setToggleState (true, juce::dontSendNotification);
        fitEditor();
    }

    ~WrappedEditor() override
    {
        editor = nullptr; // the editor goes first (the plugin may be released right after the window)
    }

    bool isEditorResizable() const { return editor != nullptr && editor->isResizable(); }
    bool hostProcessesKeys() const { return page.hostProcessesKeys(); }
    void showPage (bool showSettings) { show (showSettings); }
    void openPresetMenu() { presets.openMenu(); } // (tests / shots)

    // round 190ao: the pin of a detached window. `available` false: shown greyed (a plugin in a separate process: its
    // editor is a window of that process)
    void setPin (std::function<void (bool)> set, bool available)
    {
        onPin = std::move (set);
        pin.setVisible (onPin != nullptr);
        pin.setEnabled (available);
        pin.setTooltip (available ? tr ("Pin on top: keep this window above LYNX SHARE's other windows")
                                  : tr ("Pin on top is not available for a plugin in a separate process"));
        resized();
    }
    void showPinned (bool on) { pin.setToggleState (on, juce::dontSendNotification); }

    // round 190ao: the plugin went down (the window's timer): the notice with "Reload" takes the editor's place
    void setDown (bool isDown, std::function<void()> reload)
    {
        if (isDown == down)
            return;
        down = isDown;
        notice.onReload = std::move (reload);
        show (settings);
    }
    bool isDown() const noexcept { return down; }
    // the host's items (Detached, Pin on top) come last, after FL's spare state and parameter items (round 190df)
    void setMenuExtension (std::function<void (juce::PopupMenu&)> extend)
    {
        presets.extendMenu = [this, extend = std::move (extend)] (juce::PopupMenu& menu) {
            const auto channel = page.channelIndex();
            const auto base = channel >= 0 ? AutomationTarget { AutomationTargetKind::channelParam, channel, 0, 0 }
                              : sideTrack >= 0 && sideSlot >= 0 ? AutomationTarget { AutomationTargetKind::effectParam, sideTrack, sideSlot, 0 }
                                                                : AutomationTarget {};
            addPluginWrapperItems (menu, engineRef, hosted, base);
            if (extend)
            {
                menu.addSeparator();
                extend (menu);
            }
        };
    }

    void refreshKeyboardPriority()
    {
        keyboardPriority.setEnabled (page.keysReachHost());
        keyboardPriority.setToggleState (page.pluginKeysFirst(), juce::dontSendNotification);
    }

    // FL's Auto select linked modules (round 187a): the channel and the mixer insert this window belongs to
    std::pair<int, int> linked() const
    {
        if (sideTrack >= 0)
            return { -1, sideTrack };
        const auto channel = page.channelIndex();
        return { channel, page.channelMixerTrack (channel) };
    }

    // the picker's label follows the slot's side input (the window's timer)
    void refreshSidechain()
    {
        if (! sidechain.isVisible())
            return;
        const auto& song = engineRef.getSong();
        juce::String source;
        if (sideTrack < (int) song.mixer.size() && sideSlot < (int) song.mixer[(std::size_t) sideTrack].effects.size())
            source = pluginheader::sidechainSourceText (song, song.mixer[(std::size_t) sideTrack].effects[(std::size_t) sideSlot]);
        const auto text = tr ("SC") + ": " + (source.isNotEmpty() ? source : tr ("None")) + juce::String::fromUTF8 (" \xe2\x96\xbe");
        if (text != sidechain.getButtonText())
        {
            sidechain.setButtonText (text);
            sidechain.setToggleState (source.isNotEmpty(), juce::dontSendNotification);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds().removeFromTop (headerHeight));
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto header = area.removeFromTop (headerHeight).reduced (4, 3);
        pluginTab.setBounds (header.removeFromLeft (80));
        header.removeFromLeft (4);
        settingsTab.setBounds (header.removeFromLeft (130));
        header.removeFromLeft (8);
        if (pin.isVisible())
        {
            pin.setBounds (header.removeFromRight (26));
            header.removeFromRight (4);
        }
        if (sidechain.isVisible())
        {
            sidechain.setBounds (header.removeFromRight (juce::jmin (150, juce::jmax (0, header.getWidth() - 160))));
            header.removeFromRight (6);
        }
        keyboardPriority.setBounds (header.removeFromLeft (26));
        header.removeFromLeft (4);
        presets.setBounds (header.removeFromLeft (juce::jmin (presets.preferredWidth(), header.getWidth())));
        page.setBounds (area);
        notice.setBounds (area);
        if (editor != nullptr && ! settings && ! down)
        {
            const juce::ScopedValueSetter<bool> guard (laying, true);
            if (editor->isResizable())
                editor->setBounds (area);
            else
                editor->setTopLeftPosition (area.getPosition());
        }
    }

    void childBoundsChanged (juce::Component* child) override
    {
        if (child == editor.get() && ! laying && ! settings && ! down)
            fitEditor();
    }

private:
    static constexpr int headerHeight = 30, minWidth = 4 + 80 + 4 + 130 + 8 + 30 + 250 + 4;
    AudioEngine& engineRef;
    juce::AudioPluginInstance& hosted; // (round 190df: the plugin menu's spare state and parameter items)
    int sideTrack = -1, sideSlot = -1;
    juce::TextButton sidechain;
    KeyboardPriorityButton keyboardPriority;
    PinButton pin;
    std::function<void (bool)> onPin;

    void fitEditor()
    {
        if (down) // (the notice: at least a readable size, the editor's when that is larger)
            setSize (juce::jmax (minWidth, getWidth()), juce::jmax (150 + headerHeight, getHeight()));
        else if (editor != nullptr)
            setSize (juce::jmax (minWidth, editor->getWidth()), editor->getHeight() + headerHeight);
    }

    void show (bool showSettings)
    {
        settings = showSettings;
        pluginTab.setToggleState (! settings, juce::dontSendNotification);
        settingsTab.setToggleState (settings, juce::dontSendNotification);
        page.setVisible (settings);
        notice.setVisible (down && ! settings);
        if (editor != nullptr)
        {
            editor->setVisible (! settings && ! down);
            // A plugin editor embedded as a native child window (VST3 / LV2 through JUCE's XEmbedComponent and CLAP /
            // VST2 through NativeEmbed on Linux) is not unmapped when its component is hidden: it stayed over the
            // Wrapper settings page and made it unreachable (issue #8). Moving it outside the window clips it away on
            // every platform; resized() puts it back.
            if (settings || down)
                editor->setTopLeftPosition (-12000, -12000); // (X11 positions are 16-bit, also when scaled)
        }
        if (settings)
            setSize (juce::jmax (getWidth(), page.getWidth()), juce::jmax (getHeight(), page.preferredHeight() + headerHeight));
        else
            fitEditor();
        resized();
        if (editor != nullptr)
            followNativeChildren (*editor);
    }

    // JUCE's XEmbedComponent only moves its X window when it is itself moved, not when a parent (the editor) is:
    // tell every one inside the editor (round 162d)
    static void followNativeChildren (juce::Component& c)
    {
       #if JUCE_LINUX || JUCE_BSD
        if (auto* x = dynamic_cast<juce::XEmbedComponent*> (&c))
            x->updateEmbeddedBounds();
       #endif
        for (auto* child : c.getChildren())
            followNativeChildren (*child);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    WrapperSettingsPage page;
    CrashNotice notice;
    pluginpresetui::PresetBar presets;
    juce::TextButton pluginTab { tr ("Plugin") }, settingsTab { juce::String::fromUTF8 ("\xe2\x9a\x99 ") + tr ("Wrapper settings") };
    bool settings = false, laying = false, down = false;
};

// Issue #116: FL's docked plugin windows (developer switch, ui/NativeChild.h). The desktop that hosts them, and whether
// one plugin's editor opens detached: always for a plugin in another process (its window belongs to that process) and
// without the switch; else the PluginRef's "Detached" (FL's plugin menu).
Workspace*& dockWorkspace()
{
    static Workspace* w = nullptr;
    return w;
}

bool canDock (const juce::AudioPluginInstance& plugin)
{
    return nativechild::dockingEnabled() && dockWorkspace() != nullptr && ! isSandboxedPlugin (plugin);
}

bool opensDetached (const juce::AudioPluginInstance& plugin, const PluginWindows::WrapperContext& context)
{
    if (! canDock (plugin))
        return true;
    if (context.engine != nullptr && context.find)
        if (const auto* ref = context.find (const_cast<Song&> (context.engine->getSong())))
            return ref->detached;
    return false;
}

void setDetachedAndReopen (juce::AudioPluginInstance& plugin, bool detached, bool store = true);

// What a plugin editor's host does, whether it is a window of its own (detached) or a frame docked in the desktop: it
// keeps the plugin alive while it is open, makes the editor (with FL's wrapper header when there is an engine) and
// follows the editor's gestures and moves (Last tweaked, automation recording, the picker's thumbnail).
class EditorHost : private juce::AudioProcessorListener, private juce::Timer
{
    std::shared_ptr<void> owner; // (first member: released last, after the editor)

public:
    EditorHost (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& t, PluginWindows::WrapperContext c)
        : owner (std::move (keepAlive)), plugin (p), title (t), context (c)
    {
        crashguard::note (("opening the editor of " + plugin.getName()).toStdString()); // session breadcrumb (round 111)
        juce::AudioProcessorEditor* editor = plugin.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin); // plugin without a UI
        crashguard::note (("editor open: " + plugin.getName()).toStdString());

        editorResizable = editor->isResizable();
        shownEditor = editor;
        // (issue #96: as in FL, what it leaves unused plays the typing keyboard. A plugin in another process has its
        // own keyboard: issue #99 is that case, this path stays away from it.)
        if (sandboxedPluginProcessId (plugin) == 0 && ! isSandboxedPluginDead (plugin))
        {
            listenForKeysPluginLeaves (*editor);
            keysFromPluginView = true; // (the window marks itself: SecondaryKeyForwarder hands it what the view leaves)
        }
        thumbKey = plugin.getPluginDescription().createIdentifierString().toStdString();
        if (c.engine != nullptr && c.find) // FL's wrapper header and settings page (round 146a)
        {
            auto* wrapped = new WrappedEditor (editor, *c.engine, c.find, plugin, c.channel, c.track, c.slot);
            // FL's plugin menu ends with "Detached" (issue #116); a detached window then has LYNX's "Pin on top"
            wrapped->setMenuExtension ([this] (juce::PopupMenu& menu) {
                if (canDock (this->plugin))
                    addDetachedItem (menu);
                addPinItem (menu);
            });
            content.reset (wrapped);
        }
        else
            content.reset (editor);
    }

    virtual ~EditorHost() override { stopHost(); }

    virtual juce::Component& hostComponent() = 0;
    virtual bool hostActive() = 0;
    virtual void bringToFront() = 0;
    virtual bool hostMinimised() { return false; }
    // round 190ao: "Pin on top" (a detached window only)
    virtual bool pinnedOnTop() const { return false; }
    virtual void setPinnedOnTop (bool) {}
    virtual void addPinItem (juce::PopupMenu&) {}
    void* nativeHandle() { return hostComponent().getWindowHandle(); }

    juce::AudioPluginInstance* key() const noexcept { return &plugin; }

    static juce::AudioPluginInstance*& lastUsed()
    {
        static juce::AudioPluginInstance* last = nullptr;
        return last;
    }

    // True when nothing but this window keeps the plugin's adapter alive: the plugin was removed or replaced, its
    // channel deleted or the project closed (issue #9: the removed plugin's editor stayed on screen).
    bool orphaned() const noexcept { return owner != nullptr && owner.use_count() == 1; }

    // what reopening the editor elsewhere needs (issue #116: Detached closes it and opens it again, never re-parents it)
    std::shared_ptr<void> keepAlive() const { return owner; }
    const juce::String& hostTitle() const noexcept { return title; }
    const PluginWindows::WrapperContext& getContext() const noexcept { return context; }

    // tests / shots: the periodic check now (the crash notice follows the plugin), and the window's content
    void pollNow() { timerCallback(); }
    juce::Component* contentForShot() const noexcept { return shownContent; }

    void addDetachedItem (juce::PopupMenu& menu)
    {
        auto* p = &plugin;
        const auto detached = opensDetached (plugin, context);
        menu.addItem (tr ("Detached"), true, detached, [p, detached] {
            juce::MessageManager::callAsync ([p, detached] { setDetachedAndReopen (*p, ! detached); });
        });
    }

protected:
    void startHost()
    {
        crashguard::note (("editor shown: " + plugin.getName()).toStdString()); // (its OpenGL / GPU setup runs when shown)
        plugin.addListener (this); // the editor's gestures: Last tweaked, automation recording (REA round 143)
        startTimerHz (10);         // its moves without gestures (round 162d)
        // round 190gq: while its editor is open, the plugin's channel / insert stays real-time (render-ahead): what the
        // editor shows and what a tweak does are heard at once
        if (context.engine != nullptr)
            context.engine->setEditorOpen (this, context.find, context.channel ? -1 : context.track, true);
    }

    void stopHost()
    {
        if (context.engine != nullptr)
            context.engine->setEditorOpen (this, {}, -1, false);
        editorlimit::forget (&plugin);
        crashguard::note (("closing the editor of " + plugin.getName()).toStdString());
        stopTimer();
        plugin.removeListener (this);
    }

    std::unique_ptr<juce::Component> content; // until the host takes it
    juce::Component* shownContent = nullptr;   // the host's content (owned by the host)

    // FL's Auto select linked modules [DECOMP FUN_00f5c5f0 -> FUN_00f5c340] (round 187a): called when the host
    // (floating window or docked frame) becomes the one in use
    void focusLinked()
    {
        if (auto* wrapped = dynamic_cast<WrappedEditor*> (shownContent))
        {
            const auto [channel, insert] = wrapped->linked();
            markLinkedWindow (hostComponent(), channel, insert);
            linkedWindowFocused (hostComponent());
        }
    }
    bool editorResizable = false;
    bool keysFromPluginView = false; // issue #96: the editor's unused keys come back (listenForKeysPluginLeaves)

private:
    // round 162d (issue #8): many plugins move their parameters without begin/end gestures, so their knobs never
    // became "Last tweaked". A change made on the UI thread while this window is active or under the mouse, when it
    // is the only parameter that moved since the last tick, counts as a tweak (core/LastTweaked.h; automation and
    // links run on the audio thread, preset loads move many parameters at once).
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
            changes.changed (index);
    }
    void timerCallback() override
    {
        auto& host = hostComponent();
        // the Plugin picker's picture of this plugin (round 171c): its editor once it has been drawn for a while
        if (++ticks == 15 && shownEditor != nullptr && shownEditor->isShowing() && ! hostMinimised())
            pluginthumbs::captureEditor (thumbKey, *shownEditor);
        const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        const auto active = hostActive() || (host.isShowing() && host.getScreenBounds().contains (mouse));
        if (auto* wrapped = dynamic_cast<WrappedEditor*> (shownContent))
        {
            wrapped->refreshSidechain();
            wrapped->refreshKeyboardPriority(); // (undo, a project loaded)
            // round 190ao: the plugin went down while its window is open: the notice with "Reload" (only on request)
            if (context.reload && ! wrapped->isDown() && (isSandboxedPluginDead (plugin) || (context.down && context.down())))
                wrapped->setDown (true, [reload = context.reload] { juce::MessageManager::callAsync (reload); });
        }
        if (const auto moved = changes.tick (active); moved >= 0 && PluginWindows::onGesture)
        {
            PluginWindows::onGesture (plugin, moved, true);
            PluginWindows::onGesture (plugin, moved, false);
        }
    }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { gesture (index, true); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { gesture (index, false); }
    void gesture (int index, bool starting)
    {
        if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && juce::MessageManager::getInstance()->isThisTheMessageThread())
            changes.gesture (index, starting);
        auto* p = &plugin;
        juce::MessageManager::callAsync ([p, index, starting] {
            if (PluginWindows::onGesture)
                PluginWindows::onGesture (*p, index, starting);
        });
    }

    juce::AudioPluginInstance& plugin;
    juce::String title;
    PluginWindows::WrapperContext context;
    lasttweak::ChangeFilter changes;
    juce::Component::SafePointer<juce::Component> shownEditor;
    std::string thumbKey;
    int ticks = 0;
};

void closeHostOf (const juce::AudioPluginInstance* plugin);
void pinnedWindowsChanged();

// FL's "Detached": the editor in a top-level window of its own, kept above the main window (LYNX's only kind so far).
class PluginWindow final : public juce::DocumentWindow, public EditorHost
{
public:
    PluginWindow (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title, PluginWindows::WrapperContext context)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          EditorHost (std::move (keepAlive), p, title, std::move (context))
    {
        setUsingNativeTitleBar (true);
        useClassicRendering();
        if (keysFromPluginView)
            getProperties().set ("lynxPluginWindow", true);
        shownContent = content.get();
        setContentOwned (content.release(), true);
        setResizable (editorResizable, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
        startHost();
        // round 190ao: LYNX's "Pin on top", remembered with the plugin in the project
        if (auto* wrapped = dynamic_cast<WrappedEditor*> (shownContent))
        {
            wrapped->setPin ([this] (bool on) { setPinned (on, true); }, pinnable());
            const auto& c = getContext();
            const auto* ref = c.engine != nullptr && c.find ? c.find (const_cast<Song&> (c.engine->getSong())) : nullptr;
            if (ref != nullptr && ref->pinned && pinnable())
                setPinned (true, false);
        }
    }

    // A plugin in a separate process shows its editor in a window of that process: LYNX can't keep that one in front.
    bool pinnable() const { return ! isSandboxedPlugin (*key()); }
    bool pinnedOnTop() const override { return pinned; }
    void setPinnedOnTop (bool on) override { setPinned (on, true); }

    void setPinned (bool on, bool save)
    {
        pinned = on && pinnable();
        if (auto* wrapped = dynamic_cast<WrappedEditor*> (shownContent))
            wrapped->showPinned (pinned);
        if (const auto& c = getContext(); save && c.engine != nullptr && c.find)
        {
            c.engine->editSong ([find = c.find, on = pinned] (Song& song) {
                if (auto* ref = find (song))
                    ref->pinned = on;
            }, false);
            c.engine->markModified();
        }
       #if JUCE_LINUX || JUCE_BSD
        // X11 has no owner windows and JUCE can only raise a window there by activating it, which would take the
        // keyboard from the window being used: the window manager's "always on top" instead
        setAlwaysOnTop (pinned);
       #else
        pinnedWindowsChanged();
       #endif
    }

    void addPinItem (juce::PopupMenu& menu) override
    {
        menu.addItem (tr ("Pin on top"), pinnable(), pinned, [safe = juce::Component::SafePointer<PluginWindow> (this)] {
            if (safe != nullptr)
                safe->setPinned (! safe->pinned, true);
        });
    }

    // round 157a: the window that holds a third-party editor draws LYNX's few pixels with GDI, not Direct2D (JUCE 8's
    // default). A plugin's OpenGL child window then sits in a classic redirected top-level window, as in every other
    // host — a Direct2D / DXGI swap-chain parent is where Intel's OpenGL driver crashed in SetPixelFormat (her
    // Auto-Tune Pro editor, 2026-10-25). (Re-applied whenever the native window is re-created.)
    void addToDesktop (int styleFlags, void* nativeWindowToAttachTo) override
    {
        DocumentWindow::addToDesktop (styleFlags, nativeWindowToAttachTo);
        useClassicRendering();
    }

    void useClassicRendering()
    {
        getProperties().set ("lynxGdi", true);
       #if JUCE_WINDOWS
        if (auto* peer = getPeer())
            if (const auto engines = peer->getAvailableRenderingEngines(); engines.size() > 1 && peer->getCurrentRenderingEngine() != 0)
                peer->setCurrentRenderingEngine (0); // "Software Renderer" (GDI)
       #endif
    }

    ~PluginWindow() override
    {
        // round 165q: closing the active plugin window (its X, F12, Alt+F12, removing the plugin) let Windows hand the
        // activation to another program (seen: a terminal on the second screen) instead of LYNX, so Space and the
        // shortcuts went there. The main window takes it first, while LYNX is still the foreground program.
        if (isActiveWindow())
            activateMainWindow();
        stopHost();
        // the UI must be destroyed before the plugin (which may die together with the host's keep-alive)
        clearContentComponent();
    }

    void closeButtonPressed() override { closeHostOf (key()); }

    juce::Component& hostComponent() override { return *this; }
    bool hostActive() override { return isActiveWindow(); }
    bool hostMinimised() override { return isMinimised(); }
    void bringToFront() override { toFront (true); }

    void activeWindowStatusChanged() override
    {
        DocumentWindow::activeWindowStatusChanged();
        if (isActiveWindow())
        {
            lastUsed() = key();
            editorlimit::touch (key()); // (round 189o: the limit closes the least recently used editors)
            focusLinked();
        }
    }

private:
    bool pinned = false;
};

// FL's docked plugin window (issue #116): the editor in a frame that floats in the desktop, a native child window of the
// main window, so a clicked panel (also a native child) comes in front of it. FL's title strip: the plugin menu at the
// left (its last item "Detached"), the title, minimise and close at the right.
class DockedPluginFrame final : public juce::Component, public EditorHost
{
public:
    DockedPluginFrame (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title, PluginWindows::WrapperContext context,
                       Workspace& w)
        : EditorHost (std::move (keepAlive), p, title, std::move (context)),
          workspace (w)
    {
        setName (title);
        setOpaque (true);
        setWantsKeyboardFocus (true);
        for (auto* b : { &menuButton, &minimiseButton, &closeButton })
        {
            b->setFlat (true);
            b->setIconSizing (0.62f, 14.0f);
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        menuButton.setTooltip (tr ("Plugin options"));
        menuButton.onClick = [this] { showMenu(); };
        minimiseButton.setTooltip (tr ("Minimize"));
        minimiseButton.onClick = [this] { setMinimisedFrame (! minimised); };
        closeButton.setTooltip (tr ("Close"));
        closeButton.onClick = [this] { closeHostOf (key()); };

        shownContent = content.get();
        addAndMakeVisible (*content);
        content->addMouseListener (this, true); // a click anywhere in it brings the frame in front
        if (editorResizable)
        {
            corner = std::make_unique<juce::ResizableCornerComponent> (this, &limits);
            limits.setMinimumSize (200, titleHeight + 60);
            addAndMakeVisible (*corner);
        }
        const auto size = juce::Rectangle<int> (content->getWidth(), content->getHeight() + titleHeight);
        setBounds (workspace.placeForDockedFrame (size.getWidth(), size.getHeight()));

        workspace.addDockedFrame (*this);
        if (! nativechild::attach (*this, workspace))
            workspace.addAndMakeVisible (*this); // (no native window: a plain child of the desktop)
        nativechild::forwardUnusedKeys (*this);
        // round 190ev (issue #152): a click in the plugin's own view (its native window, which JUCE never hears of)
        // brings the frame in front too, as a click on the frame does
        nativechild::raiseOnAnyClick (*this, [this] {
            lastUsed() = key();
            editorlimit::touch (key());
            focusLinked();
        });
        getProperties().set ("lynxPluginWindow", true);
        useClassicRendering();
        setVisible (true);
        toFront (false);
        nativechild::raiseOverlays();
        startHost();
    }

    ~DockedPluginFrame() override
    {
        stopHost();
        workspace.removeDockedFrame (*this);
        if (content != nullptr)
            content->removeMouseListener (this);
        corner = nullptr;
        // the UI goes before the plugin (which may die together with the host's keep-alive), and the native window
        // before the frame
        removeChildComponent (content.get());
        content = nullptr;
        nativechild::detach (*this);
        if (auto* parent = getParentComponent())
            parent->removeChildComponent (this);
    }

    juce::Component& hostComponent() override { return *this; }
    bool hostMinimised() override { return minimised; }

    bool hostActive() override
    {
        if (hasKeyboardFocus (true))
            return true;
       #if JUCE_WINDOWS
        if (auto* own = static_cast<HWND> (getWindowHandle()))
            if (const auto focus = GetFocus(); focus != nullptr && (focus == own || IsChild (own, focus)))
                return true;
       #endif
        return false;
    }

    void bringToFront() override
    {
        if (minimised)
            setMinimisedFrame (false);
        toFront (true);
        nativechild::raiseOverlays();
        lastUsed() = key();
        editorlimit::touch (key()); // (round 189o: the limit closes the least recently used editors)
        focusLinked();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        toFront (false);
        nativechild::raiseOverlays();
        lastUsed() = key();
        editorlimit::touch (key()); // (round 189o: the limit closes the least recently used editors)
        focusLinked();
        if (e.eventComponent != this)
            return;
        if (e.mods.isPopupMenu() && e.y < titleHeight)
        {
            showMenu();
            return;
        }
        dragging = e.y < titleHeight;
        dragStart = nativechild::boundsIn (*this, workspace);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging || e.eventComponent != this)
            return;
        const auto delta = e.getScreenPosition() - e.getMouseDownScreenPosition();
        auto r = dragStart.translated (delta.x, delta.y);
        // the title strip stays reachable inside the desktop
        const auto area = workspace.getLocalBounds();
        r.setX (juce::jlimit (-r.getWidth() + 80, juce::jmax (0, area.getWidth() - 80), r.getX()));
        r.setY (juce::jlimit (0, juce::jmax (0, area.getHeight() - titleHeight), r.getY()));
        nativechild::place (*this, workspace, r);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this && e.y < titleHeight)
            setMinimisedFrame (! minimised);
    }

    bool keyPressed (const juce::KeyPress& press) override
    {
        if (press == juce::KeyPress::escapeKey) // FL closes the focused plugin window with Esc (round 170h)
        {
            juce::MessageManager::callAsync ([k = key()] { closeHostOf (k); });
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg1);
        auto strip = getLocalBounds().removeFromTop (titleHeight);
        g.setColour (colour::bg3);
        g.fillRect (strip);
        g.setColour (colour::border);
        g.fillRect (strip.removeFromBottom (1));
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        auto text = getLocalBounds().removeFromTop (titleHeight).withTrimmedLeft (menuButton.getRight() + 4).withTrimmedRight (2 * titleHeight + 6);
        g.drawFittedText (hostTitle(), text, juce::Justification::centredLeft, 1, 0.9f);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (theme::colour::borderStrong);
        g.drawRect (getLocalBounds(), 1);
    }

    void resized() override
    {
        auto strip = getLocalBounds().removeFromTop (titleHeight).reduced (3, 2);
        menuButton.setBounds (strip.removeFromLeft (strip.getHeight()));
        closeButton.setBounds (strip.removeFromRight (strip.getHeight()));
        strip.removeFromRight (2);
        minimiseButton.setBounds (strip.removeFromRight (strip.getHeight()));
        if (shownContent != nullptr && ! minimised)
        {
            const juce::ScopedValueSetter<bool> guard (laying, true);
            if (editorResizable)
                shownContent->setBounds (getLocalBounds().withTrimmedTop (titleHeight));
            else
                shownContent->setTopLeftPosition (0, titleHeight);
        }
        if (corner != nullptr)
            corner->setBounds (getWidth() - 14, getHeight() - 14, 14, 14);
    }

    // the editor resized itself (or its wrapper switched pages): the frame follows
    void childBoundsChanged (juce::Component* child) override
    {
        if (child == shownContent && ! laying && ! minimised)
            setSize (child->getWidth(), child->getHeight() + titleHeight);
    }

private:
    static constexpr int titleHeight = 24;

    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (hostTitle());
        addDetachedItem (menu);
        menu.addSeparator();
        menu.addItem (tr ("Close"), [k = key()] { juce::MessageManager::callAsync ([k] { closeHostOf (k); }); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
    }

    // FL's minimise of a docked window: only the title strip stays
    void setMinimisedFrame (bool shouldBe)
    {
        if (minimised == shouldBe || shownContent == nullptr)
            return;
        minimised = shouldBe;
        shownContent->setVisible (! minimised);
        if (corner != nullptr)
            corner->setVisible (! minimised);
        if (minimised)
        {
            restoredHeight = getHeight();
            // a plugin's native view isn't always unmapped with its component (issue #8): out of the frame, it is clipped
            shownContent->setTopLeftPosition (-12000, -12000);
            setSize (juce::jmin (getWidth(), 320), titleHeight);
        }
        else
        {
            setSize (shownContent->getWidth(), restoredHeight);
            resized();
        }
    }

    void useClassicRendering()
    {
       #if JUCE_WINDOWS
        if (auto* peer = getPeer()) // (as PluginWindow: GDI around a third-party editor)
            if (const auto engines = peer->getAvailableRenderingEngines(); engines.size() > 1 && peer->getCurrentRenderingEngine() != 0)
                peer->setCurrentRenderingEngine (0);
       #endif
    }

    Workspace& workspace;
    IconButton menuButton { "menu", Icon::panelMenu }, minimiseButton { "minimise", Icon::minimise }, closeButton { "close", Icon::close };
    juce::ComponentBoundsConstrainer limits;
    std::unique_ptr<juce::ResizableCornerComponent> corner;
    juce::Rectangle<int> dragStart;
    bool dragging = false, minimised = false, laying = false;
    int restoredHeight = 0;
};

} // namespace

std::function<void (const juce::AudioPluginInstance&, int, bool)> PluginWindows::onGesture;

namespace
{
std::map<juce::AudioPluginInstance*, std::unique_ptr<EditorHost>>& windows()
{
    static std::map<juce::AudioPluginInstance*, std::unique_ptr<EditorHost>> open;
    return open;
}

void closeHostOf (const juce::AudioPluginInstance* plugin)
{
    // close after leaving this callback (the window deletes itself)
    juce::MessageManager::callAsync ([plugin] { windows().erase (const_cast<juce::AudioPluginInstance*> (plugin)); });
}

// Round 190ao, "Pin on top": pinned plugin windows stay above LYNX's other secondary windows. Like them they are owned by
// the main window (WindowOwner: above LYNX, not above other programs); when another of those windows is brought in
// front of a pinned one, the pinned one is put back in front without being activated, so the keyboard stays where it
// is. Checked a few times a second while a window is pinned. Not while a dialog is open (it must stay reachable) or
// while LYNX is in the background.
void raisePinnedWindows()
{
    if (juce::Component::getNumCurrentlyModalComponents() > 0 || ! juce::Process::isForegroundProcess())
        return;
    auto& desktop = juce::Desktop::getInstance();
    std::vector<juce::Component*> order; // back to front
    std::vector<bool> pinned;
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        auto* c = desktop.getComponent (i);
        if (dynamic_cast<juce::TopLevelWindow*> (c) == nullptr || ! c->isVisible() || c->isAlwaysOnTop() || ! isOwnedByMainWindow (*c))
            continue;
        if (auto* window = dynamic_cast<juce::ResizableWindow*> (c); window != nullptr && window->isMinimised())
            continue;
        auto isPinned = false;
        for (const auto& [plugin, host] : windows())
            isPinned = isPinned || (&host->hostComponent() == c && host->pinnedOnTop());
        order.push_back (c);
        pinned.push_back (isPinned);
    }
    for (const auto i : pinontop::toRaise (pinned))
        order[(std::size_t) i]->toFront (false);
}

class PinKeeper final : public juce::Timer
{
public:
    void timerCallback() override
    {
        auto any = false;
        for (const auto& [plugin, host] : windows())
            any = any || host->pinnedOnTop();
        if (! any)
        {
            stopTimer();
            return;
        }
        raisePinnedWindows();
    }
};

void pinnedWindowsChanged()
{
    static auto* keeper = new PinKeeper(); // (never destroyed: a static Timer must not outlive the message manager)
    if (! keeper->isTimerRunning())
        keeper->startTimerHz (8);
}

std::unique_ptr<EditorHost> makeHost (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title,
                                      PluginWindows::WrapperContext context)
{
    if (! opensDetached (plugin, context))
        return std::make_unique<DockedPluginFrame> (std::move (owner), plugin, title, std::move (context), *dockWorkspace());
    return std::make_unique<PluginWindow> (std::move (owner), plugin, title, std::move (context));
}

// Issue #116: FL's plugin menu › Detached. The editor closes and opens again in the other host (never re-parented:
// some editors break when their native view changes parent); the choice is saved with the plugin in the project.
void setDetachedAndReopen (juce::AudioPluginInstance& plugin, bool detached, bool store)
{
    auto& open = windows();
    auto it = open.find (&plugin);
    if (it == open.end())
        return;
    auto keep = it->second->keepAlive();
    const auto title = it->second->hostTitle();
    const auto context = it->second->getContext();
    open.erase (it);
    if (store && context.engine != nullptr && context.find)
        context.engine->editSong ([find = context.find, detached] (Song& song) {
            if (auto* ref = find (song))
                ref->detached = detached;
        });
    PluginWindows::open (std::move (keep), plugin, title, context);
}

// Closes the windows of plugins the engine no longer hosts (checked 4 times a second while a window is open; a removed
// adapter is released by the engine within a few frames, when the audio thread has let go of its last snapshot).
class OrphanReaper final : public juce::Timer
{
public:
    void timerCallback() override
    {
        auto& open = windows();
        bool any = false;
        for (const auto& [key, window] : open)
            any = any || window->orphaned();
        // issue #36: the window is the plugin's last owner here, so closing it frees the plugin; that happens from a
        // message of its own (editor first, then the plugin), not inside this timer callback while timers are running
        if (any && ! reaping)
        {
            reaping = true;
            juce::MessageManager::callAsync ([this] {
                reaping = false;
                auto& all = windows();
                for (auto it = all.begin(); it != all.end();)
                    it = it->second->orphaned() ? all.erase (it) : std::next (it);
                if (all.empty())
                    stopTimer();
            });
        }
        else if (open.empty())
            stopTimer();
    }

private:
    bool reaping = false;
};

OrphanReaper& reaper()
{
    static auto* r = new OrphanReaper(); // (never destroyed: a static Timer must not outlive the message manager)
    return *r;
}

// --- a fault inside a plugin's own editor window (issue #32, round 174a) ------------------------------------------
std::vector<PluginWindows::EditorCrash>& editorCrashList()
{
    static std::vector<PluginWindows::EditorCrash> list;
    return list;
}

#if JUCE_WINDOWS
// the module path belongs to this plugin: its file, or a file inside its bundle (a .vst3 / .clap folder)
bool moduleOfPlugin (const juce::String& module, const juce::AudioPluginInstance& plugin)
{
    const auto file = plugin.getPluginDescription().fileOrIdentifier;
    if (file.isEmpty())
        return false;
    return module.equalsIgnoreCase (file) || module.startsWithIgnoreCase (file.trimCharactersAtEnd ("\\/") + "\\");
}

// crashguard's claim: the open editor whose window (or, for a timer / thread message, whose plugin module) faulted
void* claimEditorWindow (void* window, const wchar_t* faultModule)
{
    const auto module = juce::String (faultModule);
    if (window == nullptr) // a timer / thread message: by the faulting module
    {
        for (const auto& [plugin, w] : windows())
            if (w->nativeHandle() != nullptr && moduleOfPlugin (module, *plugin))
                return w->nativeHandle();
        return nullptr;
    }
    // a message for one of the plugin's windows: a child of the editor's window, or a popup up its owner chain (LYNX
    // makes the editor window itself owned by the main window, so the walk stops at the first editor window). LYNX's
    // own frame window around the editor is not the plugin's.
    // a docked editor's frame is a child window of the main window (issue #116): its parents first
    for (auto p = static_cast<HWND> (window); p != nullptr && p != GetDesktopWindow(); p = GetAncestor (p, GA_PARENT))
        for (const auto& [plugin, w] : windows())
            if (auto* own = w->nativeHandle(); own != nullptr && p == static_cast<HWND> (own))
                return window != own ? own : nullptr;
    auto h = GetAncestor (static_cast<HWND> (window), GA_ROOT);
    for (int i = 0; i < 8 && h != nullptr; ++i)
    {
        for (const auto& [plugin, w] : windows())
            if (auto* own = w->nativeHandle(); own != nullptr && h == static_cast<HWND> (own))
                return window != own ? own : nullptr;
        const auto owner = GetWindow (h, GW_OWNER);
        h = owner != nullptr ? GetAncestor (owner, GA_ROOT) : nullptr;
    }
    return nullptr;
}

void onEditorFault (const crashguard::WindowFault& fault)
{
    juce::AudioPluginInstance* faulted = nullptr;
    for (const auto& [plugin, w] : windows())
        if (w->nativeHandle() == fault.rootWindow)
            faulted = plugin;
    const auto name = faulted != nullptr ? faulted->getPluginDescription().name : juce::String ("?");

    // the trace next to the crash / hang files; the session names it (a later report of this session carries it)
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S");
    const auto folder = platform::settingsFile ("autosave");
    folder.createDirectory();
    const auto traceFile = folder.getChildFile ("editor-crash " + stamp + ".txt");
    traceFile.replaceWithText ("LYNX: the editor of " + name + " crashed in its own window; LYNX closed the editor and went on\n"
                               + juce::String::fromUTF8 (fault.trace.c_str()));
    crashguard::note ((juce::String (crashreport::editorCrashStepPrefix) + name + " (" + crashguard::describe (fault.code) + " at "
                       + juce::String::fromUTF8 (fault.where.c_str()) + "), trace in autosave\\" + traceFile.getFileName())
                          .toStdString());
    PluginHost::settingsFolder().getChildFile ("plugin-crashes.log")
        .appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + name + "  editor: " + crashguard::describe (fault.code) + " 0x"
                     + juce::String::toHexString ((juce::int64) fault.code) + " at " + juce::String::fromUTF8 (fault.where.c_str()) + "\n");
    editorCrashList().push_back ({ name, juce::String (crashguard::describe (fault.code)) + " at " + juce::String::fromUTF8 (fault.where.c_str()), traceFile });

    // close the editor from a message of its own (not inside the dispatch that faulted). Its teardown calls into the
    // plugin again (editor close): when that faults too, the window object is left behind (hidden) rather than
    // taking LYNX down
    auto* root = fault.rootWindow;
    juce::MessageManager::callAsync ([faulted, root] {
        auto& open = windows();
        if (auto it = open.find (faulted); faulted != nullptr && it != open.end() && it->second->nativeHandle() == root)
        {
            auto window = std::move (it->second);
            open.erase (it);
            auto* raw = window.release();
            if (const auto code = crashguard::run ([raw] { delete raw; }); code != 0)
                crashguard::note (("the editor's teardown crashed too (" + juce::String (crashguard::describe (code)) + "): left behind").toStdString());
        }
        crashguard::forgetWindow (root);
    });
}
#endif

void installEditorGuard()
{
   #if JUCE_WINDOWS
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    crashguard::setWindowGuard (claimEditorWindow, onEditorFault);
    juce::lynxDispatchMessageHook = crashguard::dispatchGuarded;
   #endif
}


class ScanThread final : public juce::ThreadWithProgressWindow
{
public:
    ScanThread (AudioEngine& e, std::function<void (int)> done)
        : juce::ThreadWithProgressWindow (tr ("Scanning for plugins ({0})...", { platform::pluginFormatNames() }), true, true),
          engine (e),
          onDone (std::move (done))
    {
    }

    // called on the UI thread when the scan finishes (or is cancelled)
    void threadComplete (bool) override
    {
        if (onDone)
            onDone (found);

        juce::MessageManager::callAsync ([this] { delete this; });
    }

    void run() override
    {
        found = engine.getPluginHost().scan (engine.getPluginHost().pluginFolders(), [this] (float progress, const juce::String& file) {
            setProgress (progress);
            setStatusMessage (juce::File (file).getFileName());
            return ! threadShouldExit();
        });
    }

    int found = 0;

private:
    AudioEngine& engine;
    std::function<void (int)> onDone;
};
} // namespace

namespace PluginWindows
{
bool letsHostProcessKeys (juce::Component& window)
{
    auto* w = dynamic_cast<PluginWindow*> (&window);
    auto* wrapped = w != nullptr ? dynamic_cast<WrappedEditor*> (w->getContentComponent()) : nullptr;
    return wrapped != nullptr && wrapped->hostProcessesKeys();
}

bool saveSettingsPageShot (juce::Component& window, const juce::File& png)
{
    auto* w = dynamic_cast<PluginWindow*> (&window);
    auto* wrapped = w != nullptr ? dynamic_cast<WrappedEditor*> (w->getContentComponent()) : nullptr;
    if (wrapped == nullptr)
        return false;
    wrapped->refreshKeyboardPriority(); // (the window's timer may not have run since the change)
    wrapped->showPage (true);
    const auto image = wrapped->createComponentSnapshot (wrapped->getLocalBounds(), true, 1.0f);
    wrapped->showPage (false);
    png.deleteFile();
    juce::FileOutputStream out (png);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
}

bool saveWindowShot (const juce::AudioPluginInstance& plugin, const juce::File& png, bool settingsPage)
{
    const auto it = windows().find (const_cast<juce::AudioPluginInstance*> (&plugin));
    if (it == windows().end() || it->second->contentForShot() == nullptr)
        return false;
    it->second->pollNow();
    auto* content = it->second->contentForShot();
    auto* wrapped = dynamic_cast<WrappedEditor*> (content);
    if (wrapped != nullptr && settingsPage)
        wrapped->showPage (true);
    const auto image = content->createComponentSnapshot (content->getLocalBounds(), true, 1.0f);
    if (wrapped != nullptr && settingsPage)
        wrapped->showPage (false);
    png.deleteFile();
    juce::FileOutputStream out (png);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out);
}

void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title, WrapperContext context)
{
    auto& open = windows();

    if (auto it = open.find (&plugin); it != open.end())
    {
        it->second->bringToFront();
        editorlimit::touch (&plugin);
        return;
    }

    installEditorGuard(); // (issue #32: a fault in the plugin's own window closes the editor, not LYNX)
    open[&plugin] = makeHost (std::move (owner), plugin, title, std::move (context));
    // FL's "Limit the number of open plugin editors" (round 189o, core/EditorLimit.h): past 25, the oldest closes
    editorlimit::opened (&plugin, [k = &plugin] {
        editorlimit::forget (k);
        closeHostOf (k); // (closes after this callback)
    });
    if (! reaper().isTimerRunning())
        reaper().startTimerHz (4);
}

void setDockWorkspace (Workspace* workspace)
{
    dockWorkspace() = workspace;
}

void setDetached (juce::AudioPluginInstance& plugin, bool detached)
{
    setDetachedAndReopen (plugin, detached);
}

void setAllDetached (AudioEngine& engine, bool detached)
{
    // FL's Tools › Macros › "Switch all plugin editors to" (round 190i): the flag of every hosted plugin in one edit,
    // then each open editor that is in the other host opens again where it belongs now (those that can't dock stay)
    engine.editSong ([detached] (Song& song) { setAllPluginsDetached (song, detached); });
    for (auto* plugin : openPlugins())
        if (plugin != nullptr && canDock (*plugin) && isDocked (*plugin) == detached)
            setDetachedAndReopen (*plugin, detached, false);
}

bool isDocked (const juce::AudioPluginInstance& plugin)
{
    const auto it = windows().find (const_cast<juce::AudioPluginInstance*> (&plugin));
    return it != windows().end() && dynamic_cast<DockedPluginFrame*> (it->second.get()) != nullptr;
}

void* hostWindowHandle (const juce::AudioPluginInstance& plugin)
{
    const auto it = windows().find (const_cast<juce::AudioPluginInstance*> (&plugin));
    return it != windows().end() ? it->second->nativeHandle() : nullptr;
}

bool showPresetMenu (const juce::AudioPluginInstance& plugin)
{
    const auto it = windows().find (const_cast<juce::AudioPluginInstance*> (&plugin));
    auto* wrapped = it != windows().end() ? dynamic_cast<WrappedEditor*> (it->second->contentForShot()) : nullptr;
    if (wrapped != nullptr)
        wrapped->openPresetMenu();
    return wrapped != nullptr;
}

bool isPinnedOnTop (const juce::AudioPluginInstance& plugin)
{
    const auto it = windows().find (const_cast<juce::AudioPluginInstance*> (&plugin));
    return it != windows().end() && it->second->pinnedOnTop();
}

void setPinnedOnTop (juce::AudioPluginInstance& plugin, bool pinned)
{
    if (const auto it = windows().find (&plugin); it != windows().end())
        it->second->setPinnedOnTop (pinned);
}

void closeAll()
{
    windows().clear();
    reaper().stopTimer();
}

void closeOrphans()
{
    reaper().timerCallback();
}

std::vector<juce::AudioPluginInstance*> openPlugins()
{
    std::vector<juce::AudioPluginInstance*> out;
    const auto* last = EditorHost::lastUsed();
    for (auto& [plugin, window] : windows())
        if (plugin == last)
            out.insert (out.begin(), plugin);
        else
            out.push_back (plugin);
    return out;
}

bool isOpen (const juce::AudioPluginInstance& plugin)
{
    return windows().count (const_cast<juce::AudioPluginInstance*> (&plugin)) > 0;
}

void close (const juce::AudioPluginInstance& plugin)
{
    windows().erase (const_cast<juce::AudioPluginInstance*> (&plugin));
}

namespace
{
juce::AudioPluginInstance* instanceAt (AudioEngine& engine, AudioEngine::HostedSlot where)
{
    if (where.track < 0)
    {
        auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (where.channel).get());
        return p != nullptr ? &p->getInstance() : nullptr;
    }
    auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (where.track, where.slot).get());
    return p != nullptr ? &p->getInstance() : nullptr;
}

const PluginRef* refAt (const Song& song, AudioEngine::HostedSlot where)
{
    if (where.track < 0)
        return where.channel >= 0 && where.channel < (int) song.channels.size() ? &song.channels[(std::size_t) where.channel].plugin : nullptr;
    if (where.track >= (int) song.mixer.size() || where.slot < 0 || where.slot >= (int) song.mixer[(std::size_t) where.track].effects.size())
        return nullptr;
    return &song.mixer[(std::size_t) where.track].effects[(std::size_t) where.slot].plugin;
}
} // namespace

int reload (AudioEngine& engine, const std::vector<AudioEngine::HostedSlot>& which)
{
    juce::StringArray names;
    for (const auto& w : which)
    {
        if (auto* instance = instanceAt (engine, w)) // (a window keeps the old instance alive)
            close (*instance);
        if (const auto* ref = refAt (engine.getSong(), w))
            names.add (juce::String::fromUTF8 (ref->name.c_str()));
    }
    const auto running = engine.reloadHostedPlugins (which);
    if (running < (int) which.size())
    {
        juce::StringArray failed;
        for (std::size_t i = 0; i < which.size(); ++i)
            if (instanceAt (engine, which[i]) == nullptr || engine.hostedPluginDown (which[i]))
                failed.addIfNotAlreadyThere (names[(int) i]);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Plugin"),
                                                tr ("{0} could not be started again.", { failed.joinIntoString (", ") }));
    }
    return running;
}

void promptReload (AudioEngine& engine, AudioEngine::HostedSlot where, std::function<void()> reopen)
{
    const auto* ref = refAt (engine.getSong(), where);
    auto* instance = instanceAt (engine, where);
    if (ref == nullptr || instance == nullptr)
        return;
    const auto name = juce::String::fromUTF8 (ref->name.c_str());
    // a plugin that ran inside LYNX can come back in a process of its own, where its next crash can't reach LYNX
    const auto offerSeparate = ! isSandboxedPlugin (*instance) && ! pluginSandboxEnabled() && ! ref->separateProcess && ! pluginAlwaysSeparate (name);
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle (tr ("Plugin crashed"))
                       .withMessage (tr ("{0} crashed and was switched off.", { name }) + "\n\n"
                                     + tr ("Reload starts it again with the settings from the last save or backup."))
                       .withButton (tr ("Reload"));
    if (offerSeparate)
        options = options.withButton (tr ("Reload in a separate process"));
    options = options.withButton (tr ("Close"));
    juce::AlertWindow::showAsync (options, [&engine, where, offerSeparate, reopen = std::move (reopen)] (int result) {
        // (JUCE: the first button is 1, the last one 0)
        if (result == 2 && offerSeparate)
        {
            // FL's "Make bridged" for this plugin: the changed option makes it again, from the state captured last
            if (auto* p = instanceAt (engine, where))
                close (*p);
            engine.capturePluginStates();
            engine.editSong ([where] (Song& song) {
                if (auto* r = const_cast<PluginRef*> (refAt (song, where)))
                    r->separateProcess = true;
            });
            if (reopen && instanceAt (engine, where) != nullptr && ! engine.hostedPluginDown (where))
                reopen();
        }
        else if (result == 1 && reload (engine, { where }) == 1 && reopen)
            reopen();
    });
}

int editorCrashCount()
{
    return (int) editorCrashList().size();
}

std::vector<EditorCrash> editorCrashes()
{
    return editorCrashList();
}
} // namespace PluginWindows

void scanPluginsWithProgress (AudioEngine& engine, std::function<void (int)> onDone)
{
    (new ScanThread (engine, std::move (onDone)))->launchThread(); // deletes itself when done
}

namespace
{
void toggleWrapperOption (AudioEngine& engine, const std::function<PluginRef* (Song&)>& find, juce::AudioPluginInstance* instance,
                          bool PluginRef::*option, bool recreates)
{
    if (recreates)
    {
        if (instance != nullptr && PluginWindows::isOpen (*instance))
            PluginWindows::close (*instance);
        engine.capturePluginStates(); // the new instance starts from the current settings
    }
    engine.editSong ([find, option] (Song& song) {
        if (auto* ref = find (song))
            ref->*option = ! (ref->*option);
    });
}
} // namespace

juce::Image wrapperSettingsSnapshot (AudioEngine& engine, std::function<PluginRef* (Song&)> find, juce::AudioPluginInstance& plugin,
                                     bool channel, int tab, float scale, int track, int slot)
{
    WrapperSettingsPage page (engine, std::move (find), plugin, channel, track, slot);
    page.setSize (520, page.preferredHeight());
    page.selectTab (tab);
    return page.createComponentSnapshot (page.getLocalBounds(), true, scale);
}

juce::PopupMenu pluginProcessingMenu (AudioEngine& engine, const PluginRef& current, juce::AudioPluginInstance* instance,
                                      std::function<PluginRef* (Song&)> find)
{
    auto toggle = [&engine, find, instance] (bool PluginRef::*option) {
        if (instance != nullptr)
            PluginWindows::close (*instance);
        engine.capturePluginStates(); // the new instance starts from the current settings
        engine.editSong ([find, option] (Song& song) {
            if (auto* ref = find (song))
                ref->*option = ! (ref->*option);
        });
    };

    juce::PopupMenu menu;
    menu.addItem (tr ("Use fixed size buffers (adds latency)"), true, current.fixedBuffers, [toggle] { toggle (&PluginRef::fixedBuffers); });
    // FL's options of fixed buffers (REA round 134): 2 ms blocks, or the whole device buffer
    menu.addItem (tr ("Process maximum size buffers (more latency, only when necessary)"), current.fixedBuffers, current.fixedBuffers && current.maxSizeBuffers,
                  [toggle] { toggle (&PluginRef::maxSizeBuffers); });
    // FL's "Use maximum buffer size from host" (REA round 139): the plugin is prepared for the host's largest block
    // instead of the fixed block (for plugins that need it to size their buffers)
    menu.addItem (tr ("Use maximum buffer size from host"), current.fixedBuffers, current.fixedBuffers && current.hostMaxBuffer,
                  [toggle] { toggle (&PluginRef::hostMaxBuffer); });
    // with Options › "Run plugins in a separate process" on, every plugin already runs in one
    const auto all = pluginSandboxEnabled();
    // a 32-bit plugin always runs in the 32-bit bridge: no "Make bridged" for it (FL's wrapper has none either, round 176a)
    const auto bits32 = instance != nullptr && isBridged32 (*instance);
    if (! bits32)
        menu.addItem (all ? tr ("Run in a separate process (on for all plugins in Options)") : tr ("Run in a separate process"), ! all,
                      all || current.separateProcess, [toggle] { toggle (&PluginRef::separateProcess); });
    // round 157a: this plugin always in its own process, in every project (e.g. after its editor crashed LYNX)
    if (! all && ! bits32 && ! current.name.empty())
    {
        const auto name = juce::String::fromUTF8 (current.name.c_str());
        const auto always = pluginAlwaysSeparate (name);
        menu.addItem (tr ("Always run {0} in a separate process", { name }), true, always, [&engine, find, instance, name, always] {
            if (instance != nullptr)
                PluginWindows::close (*instance);
            engine.capturePluginStates();
            setPluginAlwaysSeparate (name, ! always);
            // this instance follows at once (a changed option re-creates it); others when their project opens
            engine.editSong ([find, on = ! always] (Song& song) { if (auto* ref = find (song)) ref->separateProcess = on; });
        });
    }
    // FL's "Allow threaded processing" (on by default): off keeps this plugin on the audio thread itself, for plugins
    // that misbehave when called from several threads. Nothing is re-created.
    menu.addItem (tr ("Allow threaded processing"), true, current.threaded, [&engine, find] {
        engine.editSong ([find] (Song& song) {
            if (auto* ref = find (song))
                ref->threaded = ! ref->threaded;
        });
    });

    // FL's wrapper "Allow smart disable" (on by default): off = never skipped in silence, even with the slot's
    // "Smart disable" on (for plugins that misbehave when they are not called)
    menu.addItem (tr ("Allow smart disable"), true, current.allowSmartDisable, [&engine, find] {
        engine.editSong ([find] (Song& song) {
            if (auto* ref = find (song))
                ref->allowSmartDisable = ! ref->allowSmartDisable;
        });
    });

    // FL's wrapper "MIDI input port": the MIDI Out channels on this port play the plugin (REA_rodada133 §4)
    juce::PopupMenu ports;
    auto setPort = [&engine, find] (int port) {
        engine.editSong ([find, port] (Song& song) {
            if (auto* ref = find (song))
                ref->midiInPort = port;
        });
    };
    ports.addItem (tr ("None"), true, current.midiInPort < 0, [setPort] { setPort (-1); });
    for (int port = 0; port < 32; ++port)
        ports.addItem (tr ("Port {0}", { juce::String (port) }), true, current.midiInPort == port, [setPort, port] { setPort (port); });
    menu.addSubMenu (current.midiInPort >= 0 ? tr ("MIDI input port ({0})", { juce::String (current.midiInPort) }) : tr ("MIDI input port"), ports);
    return menu;
}

void addPluginItems (juce::PopupMenu& menu, AudioEngine& engine, bool instruments, std::function<void (PluginRef)> onChosen, juce::Component* owner,
                     std::function<void (ChannelDef)> onBuiltIn)
{
    // FL's way (round 151): the favourites right here, every other plugin in the picker ("More plugins...", FL's
    // list of every generator / effect with its categories, search and favourite stars). The old menu listed all
    // of them: hundreds of items in four columns on a PC with many plugins.
    // Online Sessions P5-A1 (round 190ai): a guest of a live session gets the plugins the host shares first, under
    // "On {host}'s computer", each marked "played by {host}"
    liveui::addSharedPluginItems (menu, instruments, onChosen);
    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    const auto& favourites = engine.getPluginHost().favourites();
    int kind = 0;
    for (const auto& type : types)
    {
        if (type.isInstrument != instruments)
            continue;
        ++kind;
        PluginRef ref;
        ref.id = type.createIdentifierString().toStdString();
        ref.name = type.name.toStdString();
        if (favourites.count (ref.id) == 0)
            continue;
        menu.addItem (juce::String::fromUTF8 ("\xe2\x98\x85 ") + type.name + " (" + type.manufacturerName + ")  [" + type.pluginFormatName + "]",
                      [ref, onChosen] { onChosen (ref); });
    }
    if (menu.getNumItems() > 0)
        menu.addSeparator();

    if (kind == 0 && ! onBuiltIn)
        menu.addItem (tr ("(no plugins found; use \"Scan for plugins\")"), false, false, [] {});
    else
        menu.addItem (kind == 1 ? (instruments ? tr ("More plugins... (1 instrument)") : tr ("More plugins... (1 effect)")) // (round 190bj)
                      : instruments ? tr ("More plugins... ({0} instruments)", { juce::String (kind) }) : tr ("More plugins... ({0} effects)", { juce::String (kind) }),
                      [&engine, instruments, onChosen, onBuiltIn, safe = juce::Component::SafePointer<juce::Component> (owner), hasOwner = owner != nullptr] {
                          showPluginPicker (
                              engine, instruments,
                              [onChosen, safe, hasOwner] (PluginRef ref) {
                                  if (! hasOwner || safe != nullptr)
                                      onChosen (ref);
                              },
                              ! onBuiltIn ? std::function<void (ChannelDef)>()
                                          : [onBuiltIn, safe, hasOwner] (ChannelDef c) {
                                                if (! hasOwner || safe != nullptr)
                                                    onBuiltIn (std::move (c));
                                            });
                      });

    menu.addSeparator();
    menu.addItem (tr ("Scan for plugins ({0})...", { platform::pluginFormatNames() }), [&engine] { engine.getPluginHost().startBackgroundScan(); });
}
} // namespace lynx

namespace lynx
{
std::vector<AutomationTarget> allPluginParameterTargets (juce::AudioPluginInstance& plugin, const AutomationTarget& base)
{
    std::vector<AutomationTarget> targets;
    if (base.kind != AutomationTargetKind::channelParam && base.kind != AutomationTargetKind::effectParam)
        return targets;
    const auto count = juce::jmin (plugin.getParameters().size(), midictl::ccBase); // (the indexes plugin parameters use)
    for (int i = 0; i < count; ++i)
    {
        auto t = base;
        t.param = i;
        targets.push_back (t);
    }
    return targets;
}

void addPluginWrapperItems (juce::PopupMenu& menu, AudioEngine& engine, juce::AudioPluginInstance& plugin, const AutomationTarget& base)
{
    // FL's "Spare state" section [PluginMenu Sparestate1], with FL's names (FL-native equivalents keep FL's names); the
    // kept state belongs to this plugin instance
    static std::map<const void*, juce::String> keptFor; // (a kept state is only flipped into the same plugin)
    const auto id = plugin.getPluginDescription().createIdentifierString();
    auto& spare = sparestate::shared();
    if (const auto it = keptFor.find (&plugin); it != keptFor.end() && it->second != id)
    {
        spare.forget (&plugin);
        keptFor.erase (it);
    }
    menu.addSectionHeader (tr ("Spare state"));
    menu.addItem (tr ("Store in spare state"), [&plugin, id] {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        sparestate::shared().store (&plugin, { (const std::uint8_t*) state.getData(), (const std::uint8_t*) state.getData() + state.getSize() });
        keptFor[&plugin] = id;
    });
    menu.addItem (tr ("Flip with spare state"), spare.has (&plugin), false, [&engine, &plugin] {
        juce::MemoryBlock state;
        plugin.getStateInformation (state);
        if (const auto load = sparestate::shared().flip (&plugin, { (const std::uint8_t*) state.getData(),
                                                                    (const std::uint8_t*) state.getData() + state.getSize() }))
        {
            plugin.setStateInformation (load->data(), (int) load->size());
            engine.markModified();
        }
    });
    // FL's "Parameters" section [PluginMenu N51]: Link all parameters... (Browse parameters is left out: LYNX's
    // Browser does not list a plugin's parameters)
    const auto targets = allPluginParameterTargets (plugin, base);
    if (base.kind == AutomationTargetKind::none)
        return;
    menu.addSectionHeader (tr ("Parameters"));
    menu.addItem (tr ("Link all parameters..."), ! targets.empty(), false, [&engine, targets] { openRemoteControlSettings (engine, targets, false); });
}
} // namespace lynx
