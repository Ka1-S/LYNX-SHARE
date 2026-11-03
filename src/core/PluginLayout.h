#pragma once

// Layouts of LYNX's native plugin editors (no JUCE: the data is tested in tests/tests.cpp).
//
// Each native plugin that is compatible with an FL Studio plugin gets an editor window with the same
// controls, sections, labels and arrangement as that plugin, so FL users find everything where they
// expect it; the drawing is LYNX's own (src/ui/PluginEditors.cpp). The positions below were measured on
// the FL Studio 2026 windows (docs/analise/UI_STUDY_plugins93.md): one design unit = one pixel of FL's
// window at 100 % zoom, body only (FL's wrapper title bar excluded). The editor scales them.
//
// Value controls name the effect parameter they edit (EffectInfo::params[].name of their EffectKind), so a
// layout never depends on parameter indices.

#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::plugui
{
enum class Item : std::uint8_t
{
    section,  // a titled panel (FL's grey header strip with a caps title); `label` = title ("" = no header)
    label,    // static caption text
    knob,     // rotary control; caption under it (`label`)
    hslider,  // horizontal slider (thin track, thumb)
    vslider,  // vertical fader
    radio,    // a column of options with LEDs (`options` = "A|B|C"); option i sets the value `values[i]`
    tabs,     // a row of segmented buttons (`options`), like radio but horizontal
    toggle,   // an on/off switch with a caption (value 0 / 1)
    stepper,  // a number box with small up/down arrows (drag, wheel or click the arrows)
    display,  // a custom graph drawn by the editor (`display` names it)
    value,    // the parameter's value as text (FL's read-outs next to a knob)
    menu,     // a button that opens a list of choices (`options` / `values`), showing the current one's mark
};

// style flags
enum : std::uint16_t
{
    bipolar = 1 << 0,     // knob arc grows from the centre
    showValue = 1 << 1,   // the value is written under the caption (FL's compressor style)
    valueAbove = 1 << 2,  // the value sits in a small box above the knob (FL's LuxeVerb style)
    big = 1 << 3,         // a big knob (main control of a section)
    noCaption = 1 << 4,   // no caption (FL draws none, or the section title names it)
    captionLeft = 1 << 5, // caption at the left of a slider instead of under it
    dim = 1 << 6,         // secondary caption colour (FL's greyed labels)
    pageTabs = 1 << 7,    // tabs that switch the editor's page instead of editing a parameter
    boxed = 1 << 8,       // label: drawn as a header box (FL's caption plates); section: a plain cell without a title strip
    alignLeft = 1 << 9,   // value / label text aligned left
    raised = 1 << 10,     // section: a lighter inset panel inside another one
    inverted = 1 << 11,   // toggle: lit while the value is off (FL's white "enabled" LEDs over mute switches)
    captionAbove = 1 << 12, // knob: the caption above the knob and the value under it (the reference reverbs' panels, round 114)
    momentary = 1 << 13,  // toggle: a push button (never shown lit); each click flips the value, which the engine reads
                          // as "pressed" (Nebula's Clear, round 129)
};

struct LayoutItem
{
    Item type = Item::label;
    float x = 0, y = 0, w = 0, h = 0;  // design units
    const char* param = nullptr;       // value controls: the parameter's EffectInfo name
    const char* label = nullptr;       // caption / title (English; translated by the editor)
    const char* options = nullptr;     // radio / tabs: "|"-separated captions
    std::vector<float> values {};      // radio / tabs: the value of each option (empty = i / (n − 1))
    std::uint16_t style = 0;
    int page = -1;                     // only shown on this page (−1 = always)
    const char* display = nullptr;     // display: which graph ("limiter", "compressor", "eq", ...)
    int steps = 0;                     // discrete values: number of steps over 0..1 (stepper, stepped knobs)
    const char* whenParam = nullptr;   // only shown while this parameter is within [whenMin, whenMax) (1 included)
    float whenMin = 0.0f, whenMax = 1.0f;
    const char* when2Param = nullptr;  // a second condition
    float when2Min = 0.0f, when2Max = 1.0f;
    int colour = -1;                   // ≥ 0: one of the editor's band colours (EQ bands, multiband bands)
    int columns = 0;                   // radio: options in a grid of this many columns (0 = one column)
    const char* param2 = nullptr;      // display "xy": the parameter on the vertical axis (`param` is the horizontal one)
};

struct Layout
{
    const char* fl = "";   // the FL Studio plugin it follows ("" = a generated layout)
    int width = 0, height = 0;
    std::vector<LayoutItem> items;
    std::vector<const char*> pages {}; // page names when the plugin has pages (tabs with `pageTabs`)
    std::vector<const char*> menuSwitches {}; // on / off parameters FL keeps in a menu: listed in the options menu
    std::vector<const char*> editedElsewhere {}; // parameters edited from another control's menu (EQ band slopes)
};

// The editor layout of an effect: FL's arrangement for the plugins studied so far, otherwise a generated grid
// of knobs (sections of up to 6) so that every native effect gets a window.
const Layout& layoutFor (EffectKind kind);
bool hasFlLayout (EffectKind kind);
// the display graphs that take the mouse in the plugin window (edited there: EQ bands, pads, point editors…); the
// other displays are pictures. Round 168x (issue #40): Maximus' In/Out curve "maxcurve" joined them.
bool displayTakesMouse (const char* display);

// The kinds that have an FL arrangement (in the order of docs/analise/UI_STUDY_plugins93.md).
const std::vector<EffectKind>& flLayoutKinds();

// LYNX's own effects with a designed panel (not an FL arrangement: `fl` is ""), e.g. the mixing suite of round 96.
const std::vector<EffectKind>& ownLayoutKinds();

// Generators: FL's arrangement of the channel's plugin page (nullptr = the plain grid of knobs). Value items name
// the knobs of the channel settings' model panel (their juce::Slider names); values are 0..1 over the knob's range.
const Layout* instrumentLayout (InstrumentKind kind);
const std::vector<InstrumentKind>& instrumentLayoutKinds();

// Soundgoodizer slots (EffectKind::maximus with the Soundgoodizer build): its own page, edited in the slot's state.
const Layout& soundgoodizerLayout();

// Parameter index of `name`: first the effect's own parameters (effectInfo (kind)), then, for kinds with controls in
// the slot's state (core/StateParams.h: Maximus, Fruity Delay Bank, Multiband Delay, EQUO, Fruity Convolver), those
// controls numbered on after them (−1 = no such parameter).
int paramIndex (EffectKind kind, const char* name);
bool usesStateParams (EffectKind kind) noexcept;
int paramCount (EffectKind kind);
const char* paramName (EffectKind kind, int index);
// The state control of parameter `param` (index into stateparams::tableFor (kind)), −1 = a plain parameter
int stateIndex (EffectKind kind, int param);
// A state control's value over its raw range (0…1) in a slot state, and the raw value of a 0…1 position
float stateValue01 (EffectKind kind, int param, const std::vector<std::uint8_t>& state);
int stateRaw (EffectKind kind, int param, float value01);

// Options of a radio / tabs item and the value each one sets.
std::vector<std::string> optionLabels (const LayoutItem& item);
float optionValue (const LayoutItem& item, int option);
// The option whose value is closest to `value`.
int optionAt (const LayoutItem& item, float value);

bool isValueControl (Item type) noexcept;

// Is the item shown on `page` with these parameter values? (pages and the conditions on other parameters)
bool isShown (EffectKind kind, const LayoutItem& item, int page, const EffectParams& params, const std::vector<std::uint8_t>* state = nullptr);
} // namespace lynx::plugui
