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
#include "core/FlWrapper.h"
#include "core/LastTweaked.h"
#include "engine/PluginHost.h"
#include "engine/PluginSandbox.h"
#include "engine/Text.h"
#include "platform/CrashGuard.h"
#include "ui/I18n.h"
#include "ui/PluginHeader.h"
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

// FL's wrapper Processing › Connections, the outputs column of a generator (round 188d, issue #114; FL 2026's page
// [UI fl188d a31, b23], Fruity Wrapper FUN_0078df00 / FUN_00794c70 [DECOMP]): one row per plugin output with its LED
// (active), "n. name" and the mixer track offset wheel ("---" = 0, the channel's own insert; wheel or vertical drag,
// double-click types a value). Inactive rows are drawn dimmed.
class ConnectionRows final : public juce::Component, public juce::TooltipClient
{
public:
    static constexpr int rowHeight = 27;
    std::vector<PluginOutputInfo> outputs;
    std::function<PluginOutputRoute (int)> route;
    std::function<void (int, bool)> setActive;
    std::function<void (int, int)> setOffset;

    int preferredHeight() const { return (int) outputs.size() * rowHeight + 4; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        for (int k = 0; k < (int) outputs.size(); ++k)
        {
            const auto r = route ? route (k) : PluginOutputRoute {};
            const auto active = on (k);
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
            auto text = rowArea.withTrimmedLeft (ledBounds (k).getRight() - rowArea.getX() + 6).withTrimmedRight (wheelWidth + 10);
            g.setColour (active ? colour::textPrimary : colour::textDisabled);
            g.setFont (font (type::body));
            g.drawFittedText (juce::String (k + 1) + ". " + label (k), text, juce::Justification::centredLeft, 1);
            const auto box = wheelBounds (k).toFloat();
            g.setColour (drawDigitDisplay (g, box, (hot == k && hotPart == Part::wheel) || dragRow == k, ! active, true));
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
            return on (k) ? tr ("Deactivate output {0}", { n }) : tr ("Activate output {0}", { n });
        if (hotPart == Part::wheel)
            return tr ("Mixer track offset for output {0}", { n });
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
        auto* window = new juce::AlertWindow (tr ("Mixer track offset for output {0}", { juce::String (k + 1) }), {}, juce::MessageBoxIconType::NoIcon);
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

    juce::String label (int k) const
    {
        const auto& name = outputs[(std::size_t) k].name;
        return name.isNotEmpty() ? name : tr ("Output {0}", { juce::String (k + 1) });
    }
    // the outputs column: the right half (FL's inputs column, left, is for effects)
    juce::Rectangle<int> rowBounds (int k) const
    {
        return { getWidth() / 2 - 20, 2 + k * rowHeight, getWidth() - (getWidth() / 2 - 20) - 4, rowHeight };
    }
    juce::Rectangle<int> ledBounds (int k) const { return rowBounds (k).removeFromLeft (14).withSizeKeepingCentre (12, 12); }
    juce::Rectangle<int> wheelBounds (int k) const { return rowBounds (k).removeFromRight (wheelWidth).reduced (0, 3); }

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
    WrapperSettingsPage (AudioEngine& e, std::function<PluginRef* (Song&)> f, juce::AudioPluginInstance& p, bool channel)
        : engine (e), find (std::move (f)), plugin (p)
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
        connections = channel && plugin.getTotalNumOutputChannels() > 0;
        outputRows.outputs = pluginOutputs (plugin);
        outputRows.route = [this] (int k) {
            auto* ref = find (const_cast<Song&> (engine.getSong()));
            return ref != nullptr ? ref->outputRoute (k) : PluginOutputRoute {};
        };
        outputRows.setActive = [this] (int k, bool on) {
            // (stored as the plugin's own default when it is one: −1)
            const auto declared = outputRows.outputs[(std::size_t) k].activeByDefault;
            editRoutes ([k, on, declared] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].active = (std::int8_t) (on == declared ? -1 : on ? 1 : 0); });
        };
        outputRows.setOffset = [this] (int k, int v) { editRoutes ([k, v] (std::vector<PluginOutputRoute>& r) { r[(std::size_t) k].offset = v; }); };
        connectionsView.setViewedComponent (&outputRows, false);
        connectionsView.setScrollBarsShown (true, false);
        connectionsView.setScrollBarThickness (8);
        addChildComponent (connectionsView);
        resetIo.setButtonText (tr ("Reset"));
        resetIo.setTooltip (tr ("Set all inputs and outputs to their default values"));
        // FL's Reset [EXEC]: every offset back to 0 ("---"); the LEDs stay as they are
        resetIo.onClick = [this] {
            editRoutes ([n = (int) outputRows.outputs.size()] (std::vector<PluginOutputRoute>& r) { resetOutputOffsets (r, n); });
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

    int preferredHeight() const { return bridge32Supported() ? 474 : 410; } // (+ Options › Bit size where it applies)
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
            auto midi = group (area, headerHeight + (1 + midiRows) * rowHeight + 8, tr ("MIDI"));
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
            outputsGlyph = {};
            if (tab == 1 && connections)
            {
                // the group's header line carries the buttons (FL: Reset, Auto map outputs, the glyph; Refresh at
                // the right); the rows scroll below
                auto r = area.withTrimmedBottom (2);
                groups.push_back ({ r, tr ("Connections") });
                auto header = r.removeFromTop (headerHeight + 6).reduced (10, 3);
                show (refreshIo, header.removeFromRight (26).reduced (4, 3));
                header.removeFromRight (juce::jmax (0, header.getWidth() / 2 - 150));
                outputsGlyph = header.removeFromRight (22);
                header.removeFromRight (4);
                show (autoMap, header.removeFromRight (juce::jmax (110, autoMap.getBestWidthForHeight (header.getHeight()) + 16)));
                header.removeFromRight (4);
                show (resetIo, header.removeFromRight (juce::jmax (56, resetIo.getBestWidthForHeight (header.getHeight()) + 16)));
                auto list = r.reduced (4, 4);
                show (connectionsView, list);
                outputRows.setSize (list.getWidth() - (outputRows.preferredHeight() > list.getHeight() ? 10 : 0), juce::jmax (list.getHeight(), outputRows.preferredHeight()));
                outputRows.repaint();
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

    // Processing › Connections: one edit of the outputs' routes (as many entries as the plugin has outputs)
    void editRoutes (std::function<void (std::vector<PluginOutputRoute>&)> change)
    {
        const auto count = outputRows.outputs.size();
        engine.editSong ([fn = find, count, change = std::move (change)] (Song& song) {
            if (auto* ref = fn (song); ref != nullptr && count > 0)
            {
                if (ref->outputs.size() < count)
                    ref->outputs.resize (count);
                change (ref->outputs);
                trimOutputRoutes (ref->outputs);
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
        outputRows.repaint();
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
    juce::ComboBox inPort, outPort, bendRange, bitSize;
    juce::TextButton latency, locate;
    // Processing › Connections (round 188d)
    bool connections = false;
    ConnectionRows outputRows;
    juce::Viewport connectionsView;
    juce::TextButton resetIo, autoMap;
    juce::ShapeButton refreshIo { "refresh", {}, {}, {} };
    juce::Rectangle<int> outputsGlyph;
    int tab = 0;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> groups;
    std::vector<Label> labels;
};

// The window's content with FL's wrapper header: the plugin's editor, or the wrapper settings page (gear).
class WrappedEditor final : public juce::Component
{
public:
    WrappedEditor (juce::AudioProcessorEditor* e, AudioEngine& engine, std::function<PluginRef* (Song&)> find, juce::AudioPluginInstance& plugin,
                   bool channel, int scTrack = -1, int scSlot = -1)
        : engineRef (engine), sideTrack (scTrack), sideSlot (scSlot),
          editor (e), page (engine, std::move (find), plugin, channel), presets (plugin, [&engine] { engine.markModified(); })
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
        pluginTab.setToggleState (true, juce::dontSendNotification);
        fitEditor();
    }

    ~WrappedEditor() override
    {
        editor = nullptr; // the editor goes first (the plugin may be released right after the window)
    }

    bool isEditorResizable() const { return editor != nullptr && editor->isResizable(); }

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
        if (sidechain.isVisible())
        {
            sidechain.setBounds (header.removeFromRight (juce::jmin (150, juce::jmax (0, header.getWidth() - 160))));
            header.removeFromRight (6);
        }
        presets.setBounds (header.removeFromLeft (juce::jmin (presets.preferredWidth(), header.getWidth())));
        page.setBounds (area);
        if (editor != nullptr && ! settings)
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
        if (child == editor.get() && ! laying && ! settings)
            fitEditor();
    }

private:
    static constexpr int headerHeight = 30, minWidth = 4 + 80 + 4 + 130 + 8 + 250 + 4;
    AudioEngine& engineRef;
    int sideTrack = -1, sideSlot = -1;
    juce::TextButton sidechain;

    void fitEditor()
    {
        if (editor != nullptr)
            setSize (juce::jmax (minWidth, editor->getWidth()), editor->getHeight() + headerHeight);
    }

    void show (bool showSettings)
    {
        settings = showSettings;
        pluginTab.setToggleState (! settings, juce::dontSendNotification);
        settingsTab.setToggleState (settings, juce::dontSendNotification);
        page.setVisible (settings);
        if (editor != nullptr)
        {
            editor->setVisible (! settings);
            // A plugin editor embedded as a native child window (VST3 / LV2 through JUCE's XEmbedComponent and CLAP /
            // VST2 through NativeEmbed on Linux) is not unmapped when its component is hidden: it stayed over the
            // Wrapper settings page and made it unreachable (issue #8). Moving it outside the window clips it away on
            // every platform; resized() puts it back.
            if (settings)
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
    pluginpresetui::PresetBar presets;
    juce::TextButton pluginTab { tr ("Plugin") }, settingsTab { juce::String::fromUTF8 ("\xe2\x9a\x99 ") + tr ("Wrapper settings") };
    bool settings = false, laying = false;
};

class PluginWindow final : public juce::DocumentWindow, private juce::AudioProcessorListener, private juce::Timer
{
public:
    PluginWindow (std::shared_ptr<void> keepAlive, juce::AudioPluginInstance& p, const juce::String& title, PluginWindows::WrapperContext context)
        : DocumentWindow (title, theme::colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          owner (std::move (keepAlive)),
          plugin (p)
    {
        setUsingNativeTitleBar (true);
        useClassicRendering();

        crashguard::note (("opening the editor of " + plugin.getName()).toStdString()); // session breadcrumb (round 111)
        juce::AudioProcessorEditor* editor = plugin.createEditorIfNeeded();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin); // plugin without a UI
        crashguard::note (("editor open: " + plugin.getName()).toStdString());

        const auto resizable = editor->isResizable();
        shownEditor = editor;
        thumbKey = plugin.getPluginDescription().createIdentifierString().toStdString();
        if (context.engine != nullptr && context.find) // FL's wrapper header and settings page (round 146a)
            setContentOwned (new WrappedEditor (editor, *context.engine, std::move (context.find), plugin, context.channel, context.track, context.slot), true);
        else
            setContentOwned (editor, true);
        setResizable (resizable, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
        crashguard::note (("editor shown: " + plugin.getName()).toStdString()); // (its OpenGL / GPU setup runs when shown)
        plugin.addListener (this); // the editor's gestures: Last tweaked, automation recording (REA round 143)
        startTimerHz (10);         // its moves without gestures (round 162d)
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
        crashguard::note (("closing the editor of " + plugin.getName()).toStdString());
        // round 165q: closing the active plugin window (its X, F12, Alt+F12, removing the plugin) let Windows hand the
        // activation to another program (seen: a terminal on the second screen) instead of LYNX, so Space and the
        // shortcuts went there. The main window takes it first, while LYNX is still the foreground program.
        if (isActiveWindow())
            activateMainWindow();
        stopTimer();
        plugin.removeListener (this);
        // the UI must be destroyed before the plugin (which may die together with `owner`)
        clearContentComponent();
    }

    void closeButtonPressed() override;

    juce::AudioPluginInstance* key() const noexcept { return &plugin; }

    void activeWindowStatusChanged() override
    {
        DocumentWindow::activeWindowStatusChanged();
        if (isActiveWindow())
            lastUsed() = &plugin;
    }

    static juce::AudioPluginInstance*& lastUsed()
    {
        static juce::AudioPluginInstance* last = nullptr;
        return last;
    }

    // True when nothing but this window keeps the plugin's adapter alive: the plugin was removed or replaced, its
    // channel deleted or the project closed (issue #9: the removed plugin's editor stayed on screen).
    bool orphaned() const noexcept { return owner != nullptr && owner.use_count() == 1; }

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
        // the Plugin picker's picture of this plugin (round 171c): its editor once it has been drawn for a while
        if (++ticks == 15 && shownEditor != nullptr && shownEditor->isShowing() && ! isMinimised())
            pluginthumbs::captureEditor (thumbKey, *shownEditor);
        const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        const auto active = isActiveWindow() || (isShowing() && getScreenBounds().contains (mouse));
        if (auto* wrapped = dynamic_cast<WrappedEditor*> (getContentComponent()))
            wrapped->refreshSidechain();
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

    std::shared_ptr<void> owner;
    juce::AudioPluginInstance& plugin;
    lasttweak::ChangeFilter changes;
    juce::Component::SafePointer<juce::Component> shownEditor;
    std::string thumbKey;
    int ticks = 0;
};

} // namespace

std::function<void (const juce::AudioPluginInstance&, int, bool)> PluginWindows::onGesture;

namespace
{
std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>>& windows()
{
    static std::map<juce::AudioPluginInstance*, std::unique_ptr<PluginWindow>> open;
    return open;
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
            if (w->getWindowHandle() != nullptr && moduleOfPlugin (module, *plugin))
                return w->getWindowHandle();
        return nullptr;
    }
    // a message for one of the plugin's windows: a child of the editor's window, or a popup up its owner chain (LYNX
    // makes the editor window itself owned by the main window, so the walk stops at the first editor window). LYNX's
    // own frame window around the editor is not the plugin's.
    auto h = GetAncestor (static_cast<HWND> (window), GA_ROOT);
    for (int i = 0; i < 8 && h != nullptr; ++i)
    {
        for (const auto& [plugin, w] : windows())
            if (auto* own = w->getWindowHandle(); own != nullptr && h == static_cast<HWND> (own))
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
        if (w->getWindowHandle() == fault.rootWindow)
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
        if (auto it = open.find (faulted); faulted != nullptr && it != open.end() && it->second->getWindowHandle() == root)
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

void PluginWindow::closeButtonPressed()
{
    const auto* k = key();
    // close after leaving this callback (the window deletes itself)
    juce::MessageManager::callAsync ([k] { windows().erase (const_cast<juce::AudioPluginInstance*> (k)); });
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
void open (std::shared_ptr<void> owner, juce::AudioPluginInstance& plugin, const juce::String& title, WrapperContext context)
{
    auto& open = windows();

    if (auto it = open.find (&plugin); it != open.end())
    {
        it->second->toFront (true);
        return;
    }

    installEditorGuard(); // (issue #32: a fault in the plugin's own window closes the editor, not LYNX)
    open[&plugin] = std::make_unique<PluginWindow> (std::move (owner), plugin, title, std::move (context));
    if (! reaper().isTimerRunning())
        reaper().startTimerHz (4);
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
    const auto* last = PluginWindow::lastUsed();
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
                                     bool channel, int tab, float scale)
{
    WrapperSettingsPage page (engine, std::move (find), plugin, channel);
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
        menu.addItem (instruments ? tr ("More plugins... ({0} instruments)", { juce::String (kind) }) : tr ("More plugins... ({0} effects)", { juce::String (kind) }),
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
