#pragma once

// FL's plugin wrapper menu, the ▸ at the top left of a plugin or channel window (round 190in, details190if R8:
// DF-118, DF-119, DF-120, DF-131; queue item 15 "FL channel wrapper menu"). One builder for every window that has it:
// a channel's window (native generators), a hosted plugin's window (a channel or a mixer slot).
//
// FL's layout (FL manual, Plugin wrapper › Plugin Menu; capture live190gu/genpreset/fl-wrapper-menu-slicex.png): two
// columns. Left: Piano roll, Replace ▸, Delete... | Presets: Presets ▸, Browse presets, Save preset as..., Add to plugin
// database (flag as favorite), Display preset name in preset selector | Spare state: Store in spare state, Flip with
// spare state | Parameters: Link all parameters..., Browse parameters | CPU: Allow threaded processing, Smart disable |
// Make editor thumbnail, Rename, color and icon... F2, Change color..., Random color, Change icon... | Scaling ▸,
// Detached, Help. Right (a channel only): Channel: Save channel state as..., Browse channel states | Route to free mixer
// track Ctrl+L.
//
// Every one of FL's items is listed in FL's order; an item this window can't do is shown greyed (FL greys its own items
// the same way, e.g. "Flip with spare state" with nothing stored), never left out. LYNX's own items (Pin on top...) go
// after FL's, under a line.

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx::wrappermenu
{
using Action = std::function<void()>;
using Fill = std::function<void (juce::PopupMenu&)>;

// round 190oa (DF-120, capture 190oa B4): FL's Browse parameters — the Browser goes to Current project › Effects (an
// effect: mixer track `index`, slot `slot`) or › Generators (a channel: `index`, slot −1) with that entry open on its
// parameters. The main window sets it.
inline std::function<void (bool effect, int index, int slot)>& browseParametersHandler()
{
    static std::function<void (bool, int, int)> handler;
    return handler;
}

struct Spec
{
    // first block
    Action pianoRoll;
    Fill replace; // the Replace ▸ submenu
    Action remove; // Delete...
    // Presets
    Fill presets; // the Presets ▸ submenu (the list, Randomize / Hybridize where the editor has them)
    Action browsePresets, savePresetAs, addToDatabase;
    std::optional<bool> displayPresetName;
    Action toggleDisplayPresetName;
    // Spare state
    Action storeSpare, flipSpare;
    // Parameters
    Action linkAll, browseParameters;
    // CPU
    std::optional<bool> threaded, smartDisable;
    Action toggleThreaded, toggleSmartDisable;
    // the window
    Action thumbnail, rename;
    Fill changeColour; // the colours (FL's "Change color..." opens a picker; LYNX: its colour menu)
    Action randomColour, changeIcon;
    Fill scaling;
    std::optional<bool> detached;
    Action toggleDetached;
    Action help;
    bool showHelp = true; // (round 190oa, capture 190oa B4: FL's sample channel ▸ menu has no Help item)
    // the Channel column (a channel's window only)
    bool channel = false;
    Action saveChannelState, browseChannelStates, routeToFreeMixerTrack;
    Fill sample; // (round 190oa, DF-118, capture 190oa B4: a sample channel's Sample ▸, the waveform's menu)
    // LYNX's own items after FL's (Pin on top...)
    Fill extra;
};

inline void item (juce::PopupMenu& menu, const juce::String& text, const Action& action, bool ticked = false, const juce::String& key = {})
{
    juce::PopupMenu::Item i (text);
    i.isEnabled = action != nullptr;
    i.isTicked = ticked;
    i.shortcutKeyDescription = key;
    if (action != nullptr)
        i.action = action;
    menu.addItem (std::move (i));
}

inline void sub (juce::PopupMenu& menu, const juce::String& text, const Fill& fill)
{
    juce::PopupMenu inner;
    if (fill != nullptr)
        fill (inner);
    menu.addSubMenu (text, inner, fill != nullptr && inner.getNumItems() > 0);
}

inline juce::PopupMenu build (const Spec& s)
{
    juce::PopupMenu m;
    item (m, tr ("Piano roll"), s.pianoRoll);
    sub (m, tr ("Replace"), s.replace);
    item (m, tr ("Delete..."), s.remove);

    m.addSectionHeader (tr ("Presets"));
    sub (m, tr ("Presets"), s.presets);
    item (m, tr ("Browse presets"), s.browsePresets);
    item (m, tr ("Save preset as..."), s.savePresetAs);
    item (m, tr ("Add to plugin database (flag as favorite)"), s.addToDatabase);
    item (m, tr ("Display preset name in preset selector"), s.toggleDisplayPresetName, s.displayPresetName.value_or (true));

    m.addSectionHeader (tr ("Spare state"));
    item (m, tr ("Store in spare state"), s.storeSpare);
    item (m, tr ("Flip with spare state"), s.flipSpare);

    m.addSectionHeader (tr ("Parameters"));
    item (m, tr ("Link all parameters..."), s.linkAll);
    item (m, tr ("Browse parameters"), s.browseParameters);

    m.addSectionHeader (tr ("CPU"));
    item (m, tr ("Allow threaded processing"), s.toggleThreaded, s.threaded.value_or (false));
    item (m, tr ("Smart disable"), s.toggleSmartDisable, s.smartDisable.value_or (false));

    m.addSeparator();
    item (m, tr ("Make editor thumbnail"), s.thumbnail);
    item (m, tr ("Rename, color and icon..."), s.rename, false, "F2");
    sub (m, tr ("Change color..."), s.changeColour);
    item (m, tr ("Random color"), s.randomColour);
    item (m, tr ("Change icon..."), s.changeIcon);

    m.addSeparator();
    sub (m, tr ("Scaling"), s.scaling);
    item (m, tr ("Detached"), s.toggleDetached, s.detached.value_or (false));
    if (s.showHelp)
        item (m, tr ("Help"), s.help);

    if (s.extra != nullptr)
    {
        juce::PopupMenu more;
        s.extra (more);
        if (more.getNumItems() > 0)
        {
            m.addSeparator();
            for (juce::PopupMenu::MenuItemIterator it (more); it.next();)
                m.addItem (it.getItem());
        }
    }

    if (s.channel)
    {
        m.addColumnBreak();
        m.addSectionHeader (tr ("Channel"));
        item (m, tr ("Save channel state as..."), s.saveChannelState);
        item (m, tr ("Browse channel states"), s.browseChannelStates);
        m.addSeparator();
        item (m, tr ("Route to free mixer track"), s.routeToFreeMixerTrack, false, "Ctrl+L");
        if (s.sample != nullptr) // (FL: last in the Channel column)
            sub (m, tr ("Sample"), s.sample);
    }
    return m;
}

// The ▸ that opens the menu: FL's small triangle, drawn as LYNX's native editor headers draw theirs.
class MenuArrow final : public juce::Button
{
public:
    MenuArrow() : juce::Button ("menu")
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setTooltip (tr ("Plugin menu"));
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        juce::Path p;
        p.addTriangle (-3.0f, -5.0f, -3.0f, 5.0f, 4.0f, 0.0f);
        p.applyTransform (juce::AffineTransform::translation (r.getCentreX(), r.getCentreY()));
        g.setColour (down ? theme::colour::textPrimary : over ? theme::colour::accent : theme::colour::textSecondary);
        g.fillPath (p);
    }
};
} // namespace lynx::wrappermenu
