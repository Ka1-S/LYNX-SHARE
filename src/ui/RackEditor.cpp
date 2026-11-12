#include "ui/RackEditor.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "core/NoteNames.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/Vfx.h"
#include "engine/PluginHost.h"
#include "engine/SampleLibrary.h"
#include "ui/ControllerKnob.h"
#include "ui/DetailView.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/PatcherEditor.h"
#include "ui/PluginPicker.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

// Layout from Live 12.4's rack (REA_rodada183c §8 [SHOT] live/ref-live-rack-deviceview.png), drawn in LYNX's style:
// view column | variations | macros (2 rows) | chain list | zone editor | the selected chain's devices.
// A Drum Rack (round 190ec, docs/analise/REA_rodada190ec.md) adds its pad view between the macros and the chain list (the
// 4 × 4 pads and the overview of all 128 at their left, Live's ArePadsVisible / PadScrollPosition [DATA]) and, in the
// chain list, the I-O (Receive, Play, Choke, Audio To), S (a send per return chain) and R (the return chains) sections
// (Live's IsMidiSectionVisible / AreSendsVisible / IsReturnBranchesListVisible [DATA]).

namespace lynx
{
namespace
{
namespace col = theme::colour;

constexpr int viewColumn = 22, variationsWidth = 150, macroWidth = 64, chainListWidth = 236, zoneWidth = 268, deviceWidth = 112, mapListWidth = 330;
constexpr int rowHeight = 18, headerStrip = 22;
// Drum Rack (round 190ec): the pad view (overview strip + 4 × 4 pads), the I-O columns, a send column, the S / R / I-O toggles
constexpr int padOverviewWidth = 26, padCellMax = 50, padsWidth = 6 + padOverviewWidth + 6 + 4 * padCellMax + 6;
constexpr int ioWidths[] { 36, 36, 34, 52 }, ioWidth = 36 + 36 + 34 + 52, sendWidth = 38, toggleWidth = 26;

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// a chain volume as Live shows it ("-6.0 dB", "-inf dB")
juce::String volumeText (float gain)
{
    if (gain <= 0.0f)
        return "-inf dB";
    return juce::String (20.0f * std::log10 (gain), 1) + " dB";
}

// a chain's colour: its own (ARGB), else LYNX's hue wheel by its place (Live colours its chains [SHOT])
juce::Colour chainColour (const RackChain& c, int index)
{
    return c.colour >= 0 ? juce::Colour ((juce::uint32) c.colour).withAlpha (1.0f) : theme::noteGroupHue (1 + (index * 5) % 15);
}

juce::String keyText (int key) { return juce::String (notenames::keyName (std::clamp (key, 0, 127))); }

juce::String panText (float pan)
{
    const auto p = (int) std::lround (pan * 50.0f);
    return p == 0 ? juce::String ("C") : juce::String (std::abs (p)) + (p < 0 ? "L" : "R");
}

// the name of a device's parameter p (built-in: its info; a running plugin: its own; else "Parameter p + 1")
juce::String parameterName (const PatchNode& node, int p, const std::shared_ptr<Effect>& running)
{
    if (! node.generator && node.effect.kind != EffectKind::plugin && node.effect.kind != EffectKind::patcher)
    {
        const auto& info = effectInfo (node.effect.kind);
        if (p >= 0 && p < (int) info.params.size())
            return info.params[(std::size_t) p].name;
    }
    if (auto* fx = dynamic_cast<PluginEffect*> (running.get()); fx != nullptr && ! fx->hasCrashed())
        if (auto& instance = fx->getInstance(); p >= 0 && p < instance.getParameters().size())
            return instance.getParameters()[p]->getName (40);
    if (rack::isRack (node.effect) || rack::isRack (node.channel))
        return tr ("Macro {0}", { juce::String (p + 1) });
    return tr ("Parameter {0}", { juce::String (p + 1) });
}
} // namespace

struct RackView::Impl
{
    RackView& owner;
    AudioEngine& engine;
    RackLocator locate;
    std::optional<AutomationTarget> automation;
    juce::String key;
    bool ownHeader = false;
    std::function<void (int)> openNode;

    PatcherDoc shown; // what was last painted
    std::vector<std::unique_ptr<ctlui::Knob>> knobs;
    juce::TextButton rand { "Rand" }, map { "Map" };
    std::uint32_t seed = 0x183c183cu;
    std::function<bool()> isOn;
    std::function<void()> toggleOn;
    int lastWidth = 0;

    // the sections laid out by place()
    juce::Rectangle<int> header, column, variations, macros, chains, zones, devices, mapList;
    int deviceScroll = 0; // the devices pane scrolls sideways when the card is narrower than its devices (Device View)
    // a drag in progress
    enum class Drag { none, volume, pan, selector, zoneMove, zoneLo, zoneHi, zoneFadeLo, zoneFadeHi, morph, mapMin, mapMax, padScroll, pad, send };
    Drag drag = Drag::none;
    int dragChain = -1, dragMacro = -1, dragMapping = -1;
    // a Drum Rack (round 190ec): its pad view, an empty pad picked (view-local), the pad dragged / previewed, the chain
    // list's scroll (rows), the sample chooser
    juce::Rectangle<int> pads, padGrid, overview;
    int emptyPadKey = -1, dragKey = -1, dragReturn = -1, previewKey = -1, listScroll = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    float dragStartValue = 0.0f;
    juce::Point<int> dragStart;
    RackZone dragZone;
    // a variation launched with LYNX's glide (decision D6): the macros move from `from` to `to` over glideMs
    struct Glide
    {
        std::array<float, rackMacroCount> from {}, to {};
        std::array<bool, rackMacroCount> moves {};
        double start = 0.0, length = 0.0;
        int variation = -1;
    };
    std::optional<Glide> glide;

    Impl (RackView& o, AudioEngine& e, RackLocator l, std::optional<AutomationTarget> a, juce::String k, bool h, std::function<void (int)> open)
        : owner (o), engine (e), locate (std::move (l)), automation (std::move (a)), key (std::move (k)), ownHeader (h), openNode (std::move (open))
    {
        for (int m = 0; m < rackMacroCount; ++m)
        {
            ui::ValueSpec spec;
            spec.name = tr ("Macro {0}", { juce::String (m + 1) });
            spec.minimum = 0.0;
            spec.maximum = 127.0;
            spec.defaultValue = 0.0;
            spec.toText = [this, m] (double v) { return macroText (m, (float) v); };
            if (automation)
            {
                auto t = *automation;
                t.param = m;
                spec.target = t;
            }
            auto knob = std::make_unique<ctlui::Knob> (
                &engine, spec,
                [this, m] {
                    const auto* r = rack::rackOf (shown);
                    return r != nullptr ? (double) r->macros[(std::size_t) m].value : 0.0;
                },
                [this, m] (double v) { edit ([m, v] (PatcherDoc& d) { rack::setMacro (d, m, (float) v); }, false); });
            knob->compact = true; // (the rack draws the name plate above and the value below, as Live [SHOT])
            owner.addChildComponent (*knob);
            knobs.push_back (std::move (knob));
        }
        rand.setTooltip (tr ("Randomize the mapped macros"));
        map.setTooltip (tr ("Map mode: assign parameters to the macros"));
        map.setClickingTogglesState (true);
        rand.onClick = [this] { edit ([this] (PatcherDoc& d) { rack::randomize (d, seed); }); };
        map.onClick = [this] { owner.setMapMode (map.getToggleState()); };
        if (ownHeader)
        {
            owner.addAndMakeVisible (rand);
            owner.addAndMakeVisible (map);
        }
    }

    PatcherDoc* doc (Song& song) const { return locate ? locate (song) : nullptr; }
    const RackDoc* rack() const { return rack::rackOf (shown); }
    bool drum() const { const auto* r = rack(); return r != nullptr && r->drum; }

    void refresh()
    {
        auto& song = const_cast<Song&> (engine.getSong());
        const auto* d = doc (song);
        if (d == nullptr ? ! shown.empty() : ! (*d == shown))
        {
            shown = d != nullptr ? *d : PatcherDoc {};
            owner.resized();
            owner.repaint();
        }
    }

    void edit (std::function<void (PatcherDoc&)> fn, bool undoable = true)
    {
        engine.editSong ([l = locate, fn] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
                fn (*d);
        },
                         undoable);
        refresh();
    }

    // --- text -------------------------------------------------------------------------------------------------------
    juce::String macroName (int m) const
    {
        const auto* r = rack();
        if (r == nullptr)
            return {};
        std::string param;
        if (r->macros[(std::size_t) m].mappings.size() == 1)
            param = mappingName (r->macros[(std::size_t) m].mappings.front()).toStdString();
        return str (rack::macroName (*r, m, param));
    }

    juce::String macroText (int m, float value) const
    {
        const auto* r = rack();
        if (r == nullptr)
            return {};
        const auto& macro = r->macros[(std::size_t) m];
        if (macro.mappings.size() == 1) // (one mapping: the target's own units, as Live shows them)
        {
            const auto& x = macro.mappings.front();
            const auto v = rack::mappedValue (x, value);
            if (x.target == RackMapping::Target::chainVolume)
                return volumeText (rack::volumeGain (v));
            if (x.target == RackMapping::Target::chainPan)
                return panText (v * 2.0f - 1.0f);
            if (x.target == RackMapping::Target::selector)
                return juce::String (juce::roundToInt (v * 127.0f));
            if (x.node >= 0 && x.node < (int) shown.nodes.size())
            {
                const auto& node = shown.nodes[(std::size_t) x.node];
                if (! node.generator && node.effect.kind != EffectKind::plugin && node.effect.kind != EffectKind::patcher)
                    if (const auto text = str (formatEffectParam (node.effect.kind, x.param, v, engine.getSong().bpm)); text.isNotEmpty())
                        return text;
            }
        }
        return juce::String (juce::roundToInt (value));
    }

    std::shared_ptr<Effect> runningNode (int n) const
    {
        if (! automation)
            return nullptr;
        auto* graph = automation->kind == AutomationTargetKind::channelParam ? patcher::graphOf (engine.getChannelInstrument (automation->index).get())
                                                                              : patcher::graphOf (engine.getEffect (automation->index, automation->slot).get());
        return graph != nullptr && n >= 0 && n < graph->nodeCount() ? graph->effectAt (n) : nullptr;
    }

    juce::String nodeName (int n) const
    {
        if (n < 0 || n >= (int) shown.nodes.size())
            return "?";
        const auto& node = shown.nodes[(std::size_t) n];
        if (! node.label.empty())
            return str (node.label);
        if (rack::isRack (node.effect) || rack::isRack (node.channel))
            return node.generator ? (rack::isDrumRack (node.channel) ? tr ("Drum Rack") : tr ("Instrument Rack")) : tr ("Audio Effect Rack");
        return str (patcher::nodeName (node));
    }

    juce::String chainOfNode (int n) const
    {
        if (const auto* r = rack())
            for (int slot = 0; slot < rack::chainSlots (*r); ++slot)
                if (const auto* c = rack::chainAt (*r, slot); std::find (c->nodes.begin(), c->nodes.end(), n) != c->nodes.end())
                    return str (c->name);
        return {};
    }

    juce::String mappingName (const RackMapping& x) const
    {
        switch (x.target)
        {
            case RackMapping::Target::chainVolume: return tr ("Chain Volume");
            case RackMapping::Target::chainPan: return tr ("Chain Pan");
            case RackMapping::Target::selector: return tr ("Chain Selector");
            case RackMapping::Target::param: break;
        }
        if (x.node < 0 || x.node >= (int) shown.nodes.size())
            return "?";
        return parameterName (shown.nodes[(std::size_t) x.node], x.param, runningNode (x.node));
    }

    juce::String mappingPath (const RackMapping& x) const
    {
        const auto* r = rack();
        if (r == nullptr)
            return {};
        if (x.target == RackMapping::Target::selector)
            return tr ("Rack");
        if (x.target != RackMapping::Target::param)
            return x.chain >= 0 && x.chain < (int) r->chains.size() ? str (r->chains[(std::size_t) x.chain].name) + " | " + tr ("Mixer") : "?";
        return chainOfNode (x.node) + " | " + nodeName (x.node);
    }

    // --- layout -----------------------------------------------------------------------------------------------------
    int macroColumns() const
    {
        const auto* r = rack();
        const auto n = r != nullptr ? r->shownMacros : 8;
        return n <= 1 ? 1 : (n + 1) / 2;
    }

    // the devices pane's content: every device card and the "+" card
    static int devicesContentWidth (const RackDoc& r)
    {
        return (int) (std::max<std::size_t> (1, rack::devices (r, r.selectedChain).size()) + 1) * (deviceWidth + 4) + 8;
    }

    // the chain list's width: a Drum Rack's grows by its I-O and send columns
    static int chainsWidth (const RackDoc& r)
    {
        if (! r.drum)
            return chainListWidth;
        return chainListWidth + (r.showIO ? ioWidth : 0) + (r.showSends ? sendWidth * (int) r.returns.size() : 0);
    }

    // (`devicesOnly`: the narrowest width, with the devices pane one card wide; it scrolls)
    int width (int /*height*/, bool devicesOnly = false) const
    {
        const auto* r = rack();
        if (r == nullptr)
            return 200;
        int w = viewColumn;
        if (r->showVariations)
            w += variationsWidth;
        if (r->showMacros)
            w += macroColumns() * macroWidth + 8;
        if (r->drum && r->showPads)
            w += padsWidth;
        if (r->showChains)
            w += chainsWidth (*r) + (r->zoneEditor >= 0 ? zoneWidth : 0);
        if (r->showDevices)
            w += devicesOnly ? deviceWidth + 12 : devicesContentWidth (*r);
        if (owner.mapping && ownHeader)
            w += mapListWidth;
        return std::max (w, viewColumn + 60);
    }

    void place()
    {
        auto area = owner.getLocalBounds();
        header = ownHeader ? area.removeFromTop (headerStrip) : juce::Rectangle<int>();
        if (ownHeader)
        {
            auto h = header.reduced (3, 2);
            map.setBounds (h.removeFromRight (34));
            h.removeFromRight (3);
            rand.setBounds (h.removeFromRight (40));
        }
        const auto* r = rack();
        column = area.removeFromLeft (viewColumn);
        if (owner.mapping && ownHeader)
            mapList = area.removeFromRight (mapListWidth);
        else
            mapList = {};
        variations = r != nullptr && r->showVariations ? area.removeFromLeft (variationsWidth) : juce::Rectangle<int>();
        macros = r != nullptr && r->showMacros ? area.removeFromLeft (macroColumns() * macroWidth + 8) : juce::Rectangle<int>();
        pads = r != nullptr && r->drum && r->showPads ? area.removeFromLeft (padsWidth) : juce::Rectangle<int>();
        if (! pads.isEmpty())
        {
            auto inner = pads.reduced (6);
            overview = inner.removeFromLeft (padOverviewWidth);
            inner.removeFromLeft (6);
            const auto cell = std::max (12, std::min (padCellMax, std::min (inner.getWidth(), inner.getHeight()) / 4));
            padGrid = inner.withSize (cell * 4, cell * 4);
        }
        else
            overview = padGrid = {};
        chains = r != nullptr && r->showChains ? area.removeFromLeft (chainsWidth (*r)) : juce::Rectangle<int>();
        zones = r != nullptr && r->showChains && r->zoneEditor >= 0 ? area.removeFromLeft (zoneWidth) : juce::Rectangle<int>();
        devices = r != nullptr && r->showDevices ? area : juce::Rectangle<int>();
        deviceScroll = r != nullptr ? juce::jlimit (0, std::max (0, devicesContentWidth (*r) - devices.getWidth()), deviceScroll) : 0;

        // the macro knobs: 2 rows (Live: 2 × 4 for 8 shown), a Map button under each in Map mode
        const auto shownCount = r != nullptr ? r->shownMacros : 0;
        const auto cols = macroColumns();
        for (int m = 0; m < rackMacroCount; ++m)
        {
            auto& k = *knobs[(std::size_t) m];
            const auto visible = ! macros.isEmpty() && m < shownCount;
            k.setVisible (visible);
            if (! visible)
                continue;
            const auto rowsArea = macros.reduced (4);
            const auto cellH = rowsArea.getHeight() / 2;
            auto cell = juce::Rectangle<int> (rowsArea.getX() + (m % cols) * macroWidth, rowsArea.getY() + (m / cols) * cellH, macroWidth, cellH);
            cell.removeFromTop (16); // (the name plate, painted)
            if (owner.mapping)
                cell.removeFromBottom (15); // (the Map button, painted)
            cell.removeFromBottom (13); // (the value, painted)
            k.setBounds (cell.reduced (4, 1));
        }
        const auto w = width (owner.getHeight());
        if (w != lastWidth)
        {
            lastWidth = w;
            if (owner.onPreferredWidthChanged)
                owner.onPreferredWidthChanged();
        }
    }

    juce::Rectangle<int> macroCell (int m) const
    {
        const auto rowsArea = macros.reduced (4);
        const auto cols = macroColumns();
        const auto cellH = rowsArea.getHeight() / 2;
        return { rowsArea.getX() + (m % cols) * macroWidth, rowsArea.getY() + (m / cols) * cellH, macroWidth, cellH };
    }

    juce::Rectangle<int> powerLed() const { return { header.getX() + 7, header.getCentreY() - 5, 10, 10 }; }
    juce::Rectangle<int> columnButton (int k) const { return { column.getX() + 2, column.getY() + 4 + k * 20, viewColumn - 4, 18 }; }
    int columnButtons() const { return drum() ? 7 : 6; }
    juce::Rectangle<int> chainRow (int c) const { return { chains.getX(), chains.getY() + headerStrip + (c - listScroll) * rowHeight, chains.getWidth(), rowHeight }; }
    juce::Rectangle<int> zoneRow (int c) const { return { zones.getX() + 6, zones.getY() + headerStrip + (c - listScroll) * rowHeight, zones.getWidth() - 12, rowHeight }; }
    // the chain list's rows: the chains, then (a Drum Rack with R on) a divider and the return chains. A row's chain slot
    // (rack::chainAt), the slots listed, and a slot's row
    int listedSlots (const RackDoc& r) const { return (int) r.chains.size() + (r.drum && r.showReturns ? (int) r.returns.size() : 0); }
    juce::Rectangle<int> slotRow (const RackDoc& r, int slot) const { return chainRow (slot < (int) r.chains.size() ? slot : slot + 1); }
    bool rowVisible (const juce::Rectangle<int>& row) const { return row.getY() >= chains.getY() + headerStrip; } // (not scrolled off the top)
    juce::Rectangle<int> variationRow (int v) const { return { variations.getX() + 4, variations.getY() + 28 + v * rowHeight, variations.getWidth() - 8, rowHeight }; }
    juce::Rectangle<int> deviceCard (int i) const
    {
        return { devices.getX() + 4 + i * (deviceWidth + 4) - deviceScroll, devices.getY() + 4, deviceWidth, devices.getHeight() - 8 };
    }
    // the wheel over a devices pane that doesn't show them all; false = not handled (the Device View's chain scrolls)
    bool wheel (juce::Point<int> p, float delta)
    {
        const auto* r = rack();
        if (r != nullptr && r->drum && pads.contains (p) && delta != 0.0f) // (the pad view scrolls a row a step)
        {
            setPadScroll (r->padScroll + (delta > 0.0f ? -1 : 1));
            return true;
        }
        if (r != nullptr && r->drum && chains.contains (p) && delta != 0.0f)
        {
            const auto rows = listedSlots (*r) + (r->showReturns && ! r->returns.empty() ? 1 : 0);
            const auto fits = std::max (1, (chains.getHeight() - headerStrip) / rowHeight);
            listScroll = juce::jlimit (0, std::max (0, rows - fits), listScroll + (delta > 0.0f ? -1 : 1));
            owner.repaint();
            return true;
        }
        if (r == nullptr || ! devices.contains (p) || devicesContentWidth (*r) <= devices.getWidth())
            return false;
        const auto before = deviceScroll;
        deviceScroll = juce::jlimit (0, devicesContentWidth (*r) - devices.getWidth(), deviceScroll - juce::roundToInt (delta * 240.0f));
        owner.repaint();
        return deviceScroll != before || delta != 0.0f;
    }
    juce::Rectangle<int> morphSlider() const { return { variations.getX() + 8, variations.getBottom() - 22, variations.getWidth() - 16, 14 }; }
    juce::Rectangle<int> mapRow (int i) const { return { mapList.getX() + 4, mapList.getY() + 24 + i * rowHeight, mapList.getWidth() - 8, rowHeight }; }

    // the zone the editor shows for chain c
    RackZone zoneOf (const RackChain& c) const
    {
        return zoneEditorKind() == 0 ? c.key : zoneEditorKind() == 1 ? c.velocity : c.select;
    }
    // the zone editor shown (a Drum Rack has no Key zones, its pads take the key: Vel instead)
    int zoneEditorKind() const
    {
        const auto* r = rack();
        const auto which = r != nullptr ? r->zoneEditor : 2;
        return r != nullptr && r->drum && which == 0 ? 1 : which;
    }
    int zoneMin() const { return zoneEditorKind() == 1 ? 1 : 0; }
    float zoneX (const juce::Rectangle<int>& row, float v) const { return (float) row.getX() + (float) row.getWidth() * (v - (float) zoneMin()) / (128.0f - (float) zoneMin()); }
    float zoneValueAt (const juce::Rectangle<int>& row, int x) const
    {
        return std::clamp ((float) zoneMin() + (float) (x - row.getX()) * (128.0f - (float) zoneMin()) / (float) row.getWidth(), (float) zoneMin(), 127.0f);
    }

    void setZone (PatcherDoc& d, int c, const RackZone& z)
    {
        auto* r = rack::rackOf (d);
        if (r == nullptr || c < 0 || c >= (int) r->chains.size())
            return;
        auto& chain = r->chains[(std::size_t) c];
        const auto which = r->drum && r->zoneEditor == 0 ? 1 : r->zoneEditor;
        (which == 0 ? chain.key : which == 1 ? chain.velocity : chain.select) = z;
        rack::compile (d);
    }

    // --- paint ------------------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g)
    {
        g.fillAll (col::bg2);
        const auto* r = rack();
        if (r == nullptr)
            return;
        g.setFont (theme::font (theme::type::caption));
        if (ownHeader)
        {
            g.setColour (col::bg3);
            g.fillRect (header);
            if (isOn) // the on / off LED (Live's title bar [SHOT])
            {
                g.setColour (isOn() ? col::ledOn : col::ledOff);
                g.fillEllipse (powerLed().toFloat());
            }
            g.setColour (col::textPrimary);
            g.setFont (theme::font (theme::type::small, true));
            auto title = ! r->name.empty() ? str (r->name)
                         : r->drum                                ? tr ("Drum Rack")
                         : r->kind == RackDoc::Kind::instrument ? tr ("Instrument Rack") : r->kind == RackDoc::Kind::midiEffect ? tr ("MIDI Effect Rack") : tr ("Audio Effect Rack");
            if (r->kind == RackDoc::Kind::midiEffect)
                title << "  -  " << (r->children.empty() ? tr ("No children yet: select channels in the Channel Rack, then use Set children in the chain menu.")
                                                          : tr ("Plays {0} channels", { juce::String ((int) r->children.size()) }));
            g.drawText (title, header.reduced (8, 0).withTrimmedLeft (isOn ? 16 : 0), juce::Justification::centredLeft);
            g.setFont (theme::font (theme::type::caption));
        }

        // view column: macros, chains, devices, variations, + / − (Live's selectors [SHOT])
        g.setColour (col::bg3);
        g.fillRect (column);
        // (a Drum Rack: P, the pad view, before + / −)
        const bool on[] { r->showMacros, r->showChains, r->showDevices, r->showVariations, false, false };
        const char* labels[] { "M", "C", "D", "V", "+", "-" };
        const bool drumOn[] { r->showMacros, r->showChains, r->showDevices, r->showVariations, r->showPads, false, false };
        const char* drumLabels[] { "M", "C", "D", "V", "P", "+", "-" };
        for (int k = 0; k < columnButtons(); ++k)
        {
            const auto b = columnButton (k).toFloat();
            if (r->drum)
            {
                g.setColour (drumOn[k] ? col::accent : col::bg4);
                g.fillRoundedRectangle (b, theme::radius::sm);
                g.setColour (drumOn[k] ? col::onAccent : col::textSecondary);
                g.drawText (drumLabels[k], b, juce::Justification::centred);
                continue;
            }
            g.setColour (on[k] ? col::accent : col::bg4);
            g.fillRoundedRectangle (b, theme::radius::sm);
            g.setColour (on[k] ? col::onAccent : col::textSecondary);
            g.drawText (labels[k], b, juce::Justification::centred);
        }

        if (! variations.isEmpty())
            paintVariations (g, *r);
        if (! macros.isEmpty())
            paintMacros (g, *r);
        if (! pads.isEmpty())
            paintPads (g, *r);
        if (! chains.isEmpty())
            paintChains (g, *r);
        if (! zones.isEmpty())
            paintZones (g, *r);
        if (! devices.isEmpty())
            paintDevices (g, *r);
        if (! mapList.isEmpty())
            paintMapList (g, *r);
    }

    void paintVariations (juce::Graphics& g, const RackDoc& r)
    {
        g.setColour (col::bg1);
        g.fillRect (variations.reduced (1));
        auto newButton = juce::Rectangle<int> (variations.getX() + 40, variations.getY() + 5, variations.getWidth() - 80, 18).toFloat();
        g.setColour (col::bg4);
        g.fillRoundedRectangle (newButton, theme::radius::sm);
        g.setColour (col::textPrimary);
        g.drawText (tr ("New"), newButton, juce::Justification::centred);
        for (int v = 0; v < (int) r.variations.size(); ++v)
        {
            const auto row = variationRow (v);
            if (row.getBottom() > variations.getBottom() - (r.morphOn ? 26 : 2))
                break;
            if (v == r.selectedVariation)
            {
                g.setColour (col::accentMuted);
                g.fillRect (row);
            }
            g.setColour (col::textSecondary);
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xa3"), row.withWidth (16), juce::Justification::centred); // (store: overwrite)
            g.setColour (col::textPrimary);
            auto name = str (r.variations[(std::size_t) v].name);
            if (r.morphOn && v == r.morphA)
                name = "A  " + name;
            else if (r.morphOn && v == r.morphB)
                name = "B  " + name;
            g.drawText (name, row.withTrimmedLeft (18).withTrimmedRight (18), juce::Justification::centredLeft);
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6"), row.withTrimmedLeft (row.getWidth() - 16), juce::Justification::centred); // (launch)
        }
        if (r.variations.empty())
        {
            g.setColour (col::textDisabled);
            g.drawText (tr ("Macro Variations"), variations.withTrimmedTop (30), juce::Justification::centred);
        }
        if (r.morphOn) // LYNX's own morph (decision D6): A → B
        {
            const auto s = morphSlider().toFloat();
            g.setColour (col::bg4);
            g.fillRoundedRectangle (s, theme::radius::sm);
            g.setColour (col::info);
            g.fillRoundedRectangle (s.withWidth (s.getWidth() * r.morph), theme::radius::sm);
            g.setColour (col::textPrimary);
            g.drawText (tr ("Morph"), s, juce::Justification::centred);
        }
    }

    void paintMacros (juce::Graphics& g, const RackDoc& r)
    {
        for (int m = 0; m < r.shownMacros && m < rackMacroCount; ++m)
        {
            const auto cell = macroCell (m);
            auto plate = cell.withHeight (16).reduced (2, 1).toFloat();
            const auto mapped = ! r.macros[(std::size_t) m].mappings.empty();
            g.setColour (mapped ? col::info.withAlpha (0.35f) : col::bg3);
            g.fillRoundedRectangle (plate, theme::radius::sm);
            g.setColour (mapped ? col::textPrimary : col::textDisabled);
            g.drawFittedText (macroName (m), plate.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
            auto valueRow = cell.withTrimmedBottom (owner.mapping ? 15 : 0);
            valueRow = valueRow.removeFromBottom (13);
            g.setColour (mapped ? col::textPrimary : col::textSecondary);
            g.drawFittedText (macroText (m, r.macros[(std::size_t) m].value), valueRow, juce::Justification::centred, 1, 0.8f);
            if (owner.mapping)
            {
                auto button = cell.withTrimmedTop (cell.getHeight() - 15).reduced (8, 1).toFloat();
                g.setColour (col::accentMuted);
                g.fillRoundedRectangle (button, theme::radius::sm);
                g.setColour (col::textPrimary);
                g.drawText (tr ("Map"), button, juce::Justification::centred);
            }
        }
    }

    void paintChains (juce::Graphics& g, const RackDoc& r)
    {
        g.setColour (col::bg1);
        g.fillRect (chains.reduced (1));
        // the zone editor's buttons (Live: above the chain list [SHOT]); a Drum Rack has no Key zones (its pads take the key)
        // and its I-O / S / R toggles at the left (their place: needs a live check)
        auto strip = chains.withWidth (chainListWidth).withHeight (headerStrip).reduced (4, 3);
        const auto instrumentLike = r.kind != RackDoc::Kind::audioEffect;
        const juce::String names[] { tr ("Key"), tr ("Vel"), tr ("Chain"), tr ("Hide") };
        for (int k = 3; k >= 0; --k)
        {
            if ((! instrumentLike && (k == 0 || k == 1)) || (r.drum && k == 0))
                continue;
            auto b = strip.removeFromRight (40).reduced (1, 0).toFloat();
            const auto lit = k < 3 ? zoneEditorKind() == k : r.zoneEditor < 0;
            g.setColour (lit ? col::accent : col::bg4);
            g.fillRoundedRectangle (b, theme::radius::sm);
            g.setColour (lit ? col::onAccent : col::textSecondary);
            g.drawText (names[k], b, juce::Justification::centred);
        }
        if (r.drum)
        {
            const bool lit[] { r.showIO, r.showSends, r.showReturns };
            const char* labels[] { "I-O", "S", "R" };
            for (int k = 0; k < 3; ++k)
            {
                auto b = drumToggle (k).toFloat();
                g.setColour (lit[k] ? col::accent : col::bg4);
                g.fillRoundedRectangle (b, theme::radius::sm);
                g.setColour (lit[k] ? col::onAccent : col::textSecondary);
                g.drawText (labels[k], b, juce::Justification::centred);
            }
            // the I-O and send columns' titles over their columns
            g.setColour (col::textSecondary);
            const juce::String io[] { tr ("Receive"), tr ("Play"), tr ("Choke"), tr ("Audio To") };
            auto head = chains.withHeight (headerStrip).withTrimmedLeft (chainListWidth);
            if (r.showIO)
                for (int k = 0; k < 4; ++k)
                    g.drawFittedText (io[k], head.removeFromLeft (ioWidths[k]), juce::Justification::centred, 1, 0.7f);
            if (r.showSends)
                for (int k = 0; k < (int) r.returns.size(); ++k)
                    g.drawFittedText (str (r.returns[(std::size_t) k].name).substring (0, 1).toUpperCase(), head.removeFromLeft (sendWidth), juce::Justification::centred, 1, 0.7f);
        }
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (chains.withTrimmedTop (headerStrip));
        for (int slot = 0; slot < listedSlots (r); ++slot)
        {
            const auto& chain = *rack::chainAt (r, slot);
            const auto isReturn = rack::isReturnSlot (r, slot);
            auto row = slotRow (r, slot);
            if (row.getBottom() > chains.getBottom() && ! r.drum)
                break;
            if (! row.intersects (chains))
                continue;
            if (isReturn && slot == (int) r.chains.size()) // (the line between the chains and the return chains)
            {
                g.setColour (col::borderStrong);
                g.drawHorizontalLine (row.getY() - rowHeight / 2, (float) chains.getX() + 4.0f, (float) chains.getRight() - 4.0f);
            }
            if (slot == r.selectedChain && emptyPadKey < 0)
            {
                g.setColour (col::accentMuted);
                g.fillRect (row);
            }
            auto cells = row.reduced (2, 1);
            auto nameCell = cells.removeFromLeft (92).toFloat();
            const auto tint = chainColour (chain, slot);
            g.setColour (chain.active ? tint : tint.withSaturation (0.1f).darker (0.6f));
            g.fillRoundedRectangle (nameCell, theme::radius::sm);
            g.setColour (tint.getPerceivedBrightness() > 0.55f ? col::bg0 : col::textPrimary);
            g.drawText (str (chain.name), nameCell.reduced (4, 0), juce::Justification::centredLeft);
            if (r.kind != RackDoc::Kind::midiEffect) // (MIDI chains have no volume / pan)
            {
                g.setColour (col::textPrimary);
                g.drawText (volumeText (chain.volume), cells.removeFromLeft (52), juce::Justification::centred);
                g.drawText (panText (chain.pan), cells.removeFromLeft (30), juce::Justification::centred);
            }
            auto led = cells.removeFromLeft (18).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (chain.active ? col::ledOn : col::ledOff);
            g.fillEllipse (led);
            auto solo = cells.removeFromLeft (18).toFloat().reduced (1.0f, 2.0f);
            g.setColour (chain.solo ? col::warning : col::bg4);
            g.fillRoundedRectangle (solo, theme::radius::sm);
            g.setColour (chain.solo ? col::bg0 : col::textSecondary);
            g.drawText ("S", solo, juce::Justification::centred);
            if (! r.drum || isReturn)
                continue;
            auto extra = row.withTrimmedLeft (chainListWidth).reduced (0, 1);
            if (r.showIO)
                for (int k = 0; k < 4; ++k)
                {
                    auto cell = extra.removeFromLeft (ioWidths[k]).reduced (1, 0).toFloat();
                    g.setColour (col::bg4);
                    g.fillRoundedRectangle (cell, theme::radius::sm);
                    g.setColour (col::textPrimary);
                    g.drawFittedText (ioText (r, chain, k), cell.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
                }
            if (r.showSends)
                for (int k = 0; k < (int) r.returns.size(); ++k)
                {
                    const auto send = k < (int) chain.sends.size() ? chain.sends[(std::size_t) k] : RackSend {};
                    auto cell = extra.removeFromLeft (sendWidth).reduced (1, 0).toFloat();
                    g.setColour (col::bg4);
                    g.fillRoundedRectangle (cell, theme::radius::sm);
                    g.setColour (col::info.withAlpha (send.on ? 0.55f : 0.2f));
                    g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * std::clamp (rack::volumeValue (send.level) / rack::unityVolume, 0.0f, 1.0f)), theme::radius::sm);
                    g.setColour (send.on ? col::textPrimary : col::textDisabled);
                    g.drawFittedText (send.level > 0.0f ? juce::String (juce::roundToInt (20.0f * std::log10 (send.level))) : juce::String ("-inf"), cell.toNearestInt(),
                                      juce::Justification::centred, 1, 0.7f);
                }
        }
        const auto rows = listedSlots (r) + (r.drum && r.showReturns && ! r.returns.empty() ? 1 : 0);
        const auto hintTop = headerStrip + (rows - listScroll) * rowHeight + 6;
        if (hintTop < chains.getHeight() - 14)
        {
            g.setColour (col::textDisabled);
            g.drawText (r.drum ? tr ("Drop an instrument or sample here")
                        : r.kind == RackDoc::Kind::instrument ? tr ("Drop an instrument or effect here") : tr ("Drop effects here"),
                        chains.withTrimmedTop (hintTop), juce::Justification::centredTop);
        }
    }

    juce::Rectangle<int> drumToggle (int k) const { return { chains.getX() + 4 + k * (toggleWidth + 2), chains.getY() + 3, toggleWidth, headerStrip - 6 }; }

    // an I-O cell's text: Receive (a key / All), Play (a key), Choke (None / 1…16), Audio To (Rack / a return chain)
    juce::String ioText (const RackDoc& r, const RackChain& c, int column) const
    {
        switch (column)
        {
            case 0: return c.receive < 0 ? tr ("All Notes") : keyText (c.receive);
            case 1: return keyText (c.play);
            case 2: return c.choke <= 0 ? tr ("None") : juce::String (c.choke);
            default: return c.audioTo >= 0 && c.audioTo < (int) r.returns.size() ? str (r.returns[(std::size_t) c.audioTo].name) : tr ("Rack");
        }
    }

    // --- the pad view (Drum Rack) -----------------------------------------------------------------------------------
    // a pad's place in the 4 × 4 grid (empty when its row isn't shown)
    juce::Rectangle<int> padRect (const RackDoc& r, int key) const
    {
        const auto row = rack::padRow (key) - r.padScroll;
        if (row < 0 || row >= 4 || padGrid.isEmpty())
            return {};
        const auto cell = padGrid.getWidth() / 4;
        return { padGrid.getX() + (key % rack::drumPadColumns) * cell, padGrid.getY() + row * cell, cell - 3, cell - 3 };
    }
    // a pad's Mute, Play and Solo buttons (its bottom strip, Live's pad [MAN])
    juce::Rectangle<int> padButton (const juce::Rectangle<int>& pad, int k) const
    {
        auto strip = pad.withTrimmedTop (pad.getHeight() - 14).reduced (2, 1);
        const auto w = strip.getWidth() / 3;
        return strip.withX (strip.getX() + k * w).withWidth (w - 1);
    }
    int padAt (const RackDoc& r, juce::Point<int> p) const
    {
        for (int row = r.padScroll; row < r.padScroll + 4 && row < rack::drumPadRows; ++row)
            for (int col = 0; col < rack::drumPadColumns; ++col)
            {
                const auto key = rack::rowFirstKey (row) + col;
                if (padRect (r, key).contains (p))
                    return key;
            }
        return -1;
    }
    int selectedPadKey (const RackDoc& r) const
    {
        if (emptyPadKey >= 0)
            return emptyPadKey;
        return r.selectedChain >= 0 && r.selectedChain < (int) r.chains.size() ? r.chains[(std::size_t) r.selectedChain].receive : -1;
    }
    static int firstFreeKey (const RackDoc& r)
    {
        for (int k = rack::drumFirstPad; k < rack::drumPadCount; ++k)
            if (rack::padChain (r, k) < 0)
                return k;
        for (int k = 0; k < rack::drumFirstPad; ++k)
            if (rack::padChain (r, k) < 0)
                return k;
        return rack::drumFirstPad;
    }
    void setPadScroll (int row)
    {
        edit ([v = juce::jlimit (0, rack::drumPadRows - 4, row)] (PatcherDoc& d) { rack::rackOf (d)->padScroll = v; }, false);
    }
    int channelOfRack() const { return automation && automation->kind == AutomationTargetKind::channelParam ? automation->index : -1; }

    void paintPads (juce::Graphics& g, const RackDoc& r)
    {
        g.setColour (col::bg1);
        g.fillRect (pads.reduced (1));
        // the overview: all 128 pads, 32 rows of 4 (C-2 at the bottom), the shown rows framed
        const auto rowH = (float) overview.getHeight() / (float) rack::drumPadRows;
        const auto colW = (float) overview.getWidth() / (float) rack::drumPadColumns;
        for (int row = 0; row < rack::drumPadRows; ++row)
            for (int c = 0; c < rack::drumPadColumns; ++c)
            {
                const auto key = rack::rowFirstKey (row) + c;
                const auto chain = rack::padChain (r, key);
                g.setColour (chain >= 0 ? chainColour (r.chains[(std::size_t) chain], chain) : col::bg3);
                g.fillRect (juce::Rectangle<float> ((float) overview.getX() + (float) c * colW, (float) overview.getY() + (float) row * rowH, colW - 1.0f, std::max (1.0f, rowH - 1.0f)));
            }
        g.setColour (col::accent);
        g.drawRect (juce::Rectangle<float> ((float) overview.getX() - 1.0f, (float) overview.getY() + (float) r.padScroll * rowH - 1.0f, (float) overview.getWidth() + 1.0f, rowH * 4.0f + 1.0f), 1.0f);

        const auto selected = selectedPadKey (r);
        for (int row = r.padScroll; row < r.padScroll + 4 && row < rack::drumPadRows; ++row)
            for (int c = 0; c < rack::drumPadColumns; ++c)
            {
                const auto key = rack::rowFirstKey (row) + c;
                const auto pad = padRect (r, key);
                const auto chainIndex = rack::padChain (r, key);
                if (chainIndex < 0)
                {
                    g.setColour (col::bg2);
                    g.fillRoundedRectangle (pad.toFloat(), theme::radius::sm);
                    g.setColour (col::border);
                    g.drawRoundedRectangle (pad.toFloat().reduced (0.5f), theme::radius::sm, 1.0f);
                    g.setColour (col::textDisabled);
                    g.drawText (keyText (key), pad, juce::Justification::centred);
                }
                else
                {
                    const auto& chain = r.chains[(std::size_t) chainIndex];
                    const auto tint = chainColour (chain, chainIndex);
                    g.setColour (col::bg3);
                    g.fillRoundedRectangle (pad.toFloat(), theme::radius::sm);
                    g.setColour (chain.active ? tint : tint.withSaturation (0.1f).darker (0.6f));
                    g.fillRect (pad.withHeight (3).reduced (2, 0));
                    g.setColour (chain.active ? col::textPrimary : col::textDisabled);
                    g.drawFittedText (str (chain.name), pad.withTrimmedTop (4).withTrimmedBottom (15).reduced (3, 0), juce::Justification::centred, 2, 0.75f);
                    // Mute (lit when the pad is muted), Play (held: the preview), Solo
                    const bool lit[] { ! chain.active, previewKey == key, chain.solo };
                    const char* labels[] { "M", "\xe2\x96\xb6", "S" };
                    const juce::Colour on[] { col::warning, col::accent, col::warning };
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto b = padButton (pad, k).toFloat();
                        g.setColour (lit[k] ? on[k] : col::bg4);
                        g.fillRoundedRectangle (b, 2.0f);
                        g.setColour (lit[k] ? col::bg0 : col::textSecondary);
                        g.drawText (juce::String::fromUTF8 (labels[k]), b, juce::Justification::centred);
                    }
                }
                if (key == selected)
                {
                    g.setColour (col::accent);
                    g.drawRoundedRectangle (pad.toFloat().reduced (0.5f), theme::radius::sm, 2.0f);
                }
                if (drag == Drag::pad && dragKey >= 0 && key != dragKey && pad.contains (owner.getMouseXYRelative()))
                {
                    g.setColour (col::accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (pad.toFloat(), theme::radius::sm);
                }
            }
    }

    void paintZones (juce::Graphics& g, const RackDoc& r)
    {
        g.setColour (col::bg0);
        g.fillRect (zones.reduced (1));
        const auto rulerRow = zoneRow (0).withY (zones.getY() + 2).withHeight (headerStrip - 4);
        g.setColour (col::textSecondary);
        for (int v = 0; v <= 127; v += 16)
            g.drawText (juce::String (v), juce::Rectangle<float> (zoneX (rulerRow, (float) v) - 10.0f, (float) rulerRow.getY(), 20.0f, (float) rulerRow.getHeight()),
                        juce::Justification::centred);
        g.setColour (col::bg2);
        for (int v = 8; v < 128; v += 8)
        {
            const auto x = zoneX (rulerRow, (float) v);
            g.drawVerticalLine (juce::roundToInt (x), (float) (zones.getY() + headerStrip), (float) zones.getBottom() - 2.0f);
        }
        for (int c = 0; c < (int) r.chains.size(); ++c)
        {
            const auto row = zoneRow (c);
            if (row.getBottom() > zones.getBottom())
                break;
            if (row.getY() < zones.getY() + headerStrip) // (a Drum Rack's list scrolled past it)
                continue;
            if (c == r.selectedChain)
            {
                g.setColour (col::accentMuted.withAlpha (0.45f));
                g.fillRect (row);
            }
            const auto z = zoneOf (r.chains[(std::size_t) c]);
            const auto x0 = zoneX (row, (float) z.lo), x1 = zoneX (row, (float) z.hi + 1.0f);
            const auto f0 = zoneX (row, (float) z.fadeLo), f1 = zoneX (row, (float) z.fadeHi + 1.0f);
            const auto top = (float) row.getY() + 3.0f, mid = (float) row.getY() + 7.0f, bottom = (float) row.getBottom() - 2.0f;
            g.setColour (chainColour (r.chains[(std::size_t) c], c).interpolatedWith (col::info, 0.55f).withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, mid, x1, bottom));
            // the fades: a line ramping up to the full part [SHOT]
            juce::Path ramp;
            ramp.startNewSubPath (x0, mid);
            ramp.lineTo (f0, top);
            ramp.lineTo (f1, top);
            ramp.lineTo (x1, mid);
            g.setColour (col::textPrimary);
            g.strokePath (ramp, juce::PathStrokeType (1.2f));
        }
        if (r.zoneEditor == 2) // the chain selector [SHOT: a marker over the ruler]
        {
            const auto x = zoneX (rulerRow, r.selector + 0.5f);
            g.setColour (col::automation);
            g.fillRect (juce::Rectangle<float> (x - 2.0f, (float) zones.getY() + 2.0f, 4.0f, (float) (zones.getHeight() - 4)).withHeight ((float) headerStrip));
            g.drawText (juce::String (juce::roundToInt (r.selector)), juce::Rectangle<float> (x + 3.0f, (float) zones.getY() + 2.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
        }
    }

    // the devices pane's chain: the selected one (a Drum Rack's empty pad picked, or no pads yet: none)
    const std::vector<int>& shownDevices (const RackDoc& r) const
    {
        static const std::vector<int> none;
        if (r.drum && (emptyPadKey >= 0 || rack::chainAt (r, r.selectedChain) == nullptr))
            return none;
        return rack::devices (r, r.selectedChain);
    }
    bool emptyPadPane (const RackDoc& r) const { return r.drum && (emptyPadKey >= 0 || rack::chainAt (r, r.selectedChain) == nullptr); }

    void paintDevices (juce::Graphics& g, const RackDoc& r)
    {
        const auto& list = shownDevices (r);
        // the chain's bracket (Live: the rack's devices sit between brackets)
        g.setColour (col::borderStrong);
        g.drawRect (devices.reduced (1));
        const juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (devices.reduced (2));
        if (deviceScroll > 0 || devicesContentWidth (r) - deviceScroll > devices.getWidth()) // (more to the side: a scroll bar)
        {
            const auto track = devices.reduced (6, 0).removeFromBottom (5).translated (0, -2).toFloat();
            const auto content = (float) devicesContentWidth (r);
            g.setColour (col::bg4);
            g.fillRoundedRectangle (track.withX (track.getX() + track.getWidth() * (float) deviceScroll / content)
                                        .withWidth (track.getWidth() * (float) devices.getWidth() / content), 2.0f);
        }
        int i = 0;
        for (const auto n : list)
        {
            const auto card = deviceCard (i++);
            const auto& node = shown.nodes[(std::size_t) n];
            g.setColour (col::bg3);
            g.fillRoundedRectangle (card.toFloat(), theme::radius::md);
            auto title = card.withHeight (20).reduced (4, 2);
            const auto enabled = node.generator || node.effect.enabled;
            g.setColour (enabled ? col::ledOn : col::ledOff);
            g.fillEllipse (title.removeFromLeft (12).toFloat().withSizeKeepingCentre (9.0f, 9.0f));
            g.setColour (col::textPrimary);
            g.drawFittedText (nodeName (n), title.withTrimmedLeft (4), juce::Justification::centredLeft, 1, 0.8f);
            g.setColour (col::textSecondary);
            const auto kind = node.generator ? (node.channel.instrument == InstrumentKind::vfx ? tr ("MIDI effect") : tr ("Instrument")) : tr ("Effect");
            g.drawText (kind, card.withTrimmedTop (24).withHeight (14), juce::Justification::centred);
            auto open = card.withTrimmedTop (card.getHeight() - 26).reduced (10, 4).toFloat();
            g.setColour (col::bg4);
            g.fillRoundedRectangle (open, theme::radius::sm);
            g.setColour (col::textPrimary);
            g.drawText (tr ("Edit"), open, juce::Justification::centred);
        }
        // the "+" card: add a device to the selected chain (a Drum Rack's empty pad: its instrument, Live's "Drop an
        // instrument or sample here")
        const auto plus = deviceCard (i).toFloat();
        if (emptyPadPane (r))
        {
            g.setColour (col::textDisabled);
            g.drawFittedText (tr ("Drop an instrument or sample here"), devices.withTrimmedLeft (deviceWidth + 12).reduced (8), juce::Justification::centredLeft, 3);
        }
        g.setColour (col::bg1);
        g.fillRoundedRectangle (plus, theme::radius::md);
        g.setColour (col::textSecondary);
        g.setFont (theme::font (theme::type::title));
        g.drawText ("+", plus, juce::Justification::centred);
        g.setFont (theme::font (theme::type::caption));
    }

    void paintMapList (juce::Graphics& g, const RackDoc& r)
    {
        g.setColour (col::bg1);
        g.fillRect (mapList);
        auto head = mapList.withHeight (22).reduced (4, 2);
        g.setColour (col::textSecondary);
        g.setFont (theme::font (theme::type::caption, true));
        const int widths[] { 66, 104, 84, 34, 34 };
        const juce::String titles[] { tr ("Macro"), tr ("Path"), tr ("Name"), tr ("Min"), tr ("Max") };
        for (int k = 0; k < 5; ++k)
            g.drawText (titles[k], head.removeFromLeft (widths[k]), juce::Justification::centredLeft);
        g.setFont (theme::font (theme::type::caption));
        int i = 0;
        for (int m = 0; m < rackMacroCount; ++m)
            for (const auto& x : r.macros[(std::size_t) m].mappings)
            {
                auto row = mapRow (i++);
                if (row.getBottom() > mapList.getBottom())
                    return;
                g.setColour (col::textPrimary);
                g.drawFittedText (macroName (m), row.removeFromLeft (widths[0]), juce::Justification::centredLeft, 1, 0.8f);
                g.drawFittedText (mappingPath (x), row.removeFromLeft (widths[1]), juce::Justification::centredLeft, 1, 0.8f);
                g.drawFittedText (mappingName (x), row.removeFromLeft (widths[2]), juce::Justification::centredLeft, 1, 0.8f);
                for (const auto v : { x.min, x.max })
                {
                    auto cell = row.removeFromLeft (widths[3]).reduced (1, 1).toFloat();
                    g.setColour (col::bg4);
                    g.fillRoundedRectangle (cell, theme::radius::sm);
                    g.setColour (col::info);
                    g.drawText (juce::String (juce::roundToInt (v * 100.0f)) + "%", cell, juce::Justification::centred);
                }
            }
        if (i == 0)
        {
            g.setColour (col::textDisabled);
            g.drawFittedText (tr ("Click a macro's Map button to assign a parameter"), mapList.withTrimmedTop (30).reduced (8), juce::Justification::centredTop, 2);
        }
    }

    // --- mouse ------------------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e)
    {
        const auto* r = rack();
        if (r == nullptr)
            return;
        const auto p = e.getPosition();
        drag = Drag::none;
        dragStart = p;
        if (ownHeader && toggleOn && powerLed().expanded (3).contains (p))
            return toggleOn();
        if (ownHeader && header.contains (p) && e.getNumberOfClicks() > 1)
            return renameRack();

        for (int k = 0; k < columnButtons(); ++k)
            if (columnButton (k).contains (p))
            {
                edit ([k] (PatcherDoc& d) {
                    auto* x = rack::rackOf (d);
                    if (x->drum && k >= 4) // (a Drum Rack's column: P, then + / −)
                    {
                        if (k == 4)
                            x->showPads = ! x->showPads;
                        else
                            x->shownMacros = k == 5 ? rack::moreMacros (x->shownMacros) : rack::fewerMacros (x->shownMacros);
                        return;
                    }
                    switch (k)
                    {
                        case 0: x->showMacros = ! x->showMacros; break;
                        case 1: x->showChains = ! x->showChains; break;
                        case 2: x->showDevices = ! x->showDevices; break;
                        case 3: x->showVariations = ! x->showVariations; break;
                        case 4: x->shownMacros = rack::moreMacros (x->shownMacros); break;
                        default: x->shownMacros = rack::fewerMacros (x->shownMacros); break;
                    }
                });
                return;
            }

        if (variations.contains (p))
            return variationsDown (e, *r);
        if (macros.contains (p) && owner.mapping)
            for (int m = 0; m < r->shownMacros; ++m)
                if (macroCell (m).withTrimmedTop (macroCell (m).getHeight() - 15).contains (p))
                    return showMapMenu (m);
        if (macros.contains (p) && e.mods.isPopupMenu())
            for (int m = 0; m < r->shownMacros; ++m)
                if (macroCell (m).withHeight (16).contains (p))
                    return showMacroMenu (m);
        if (pads.contains (p))
            return padsDown (e, *r);
        if (chains.contains (p))
            return chainsDown (e, *r);
        if (zones.contains (p))
            return zonesDown (e, *r);
        if (devices.contains (p))
            return devicesDown (e, *r);
        if (mapList.contains (p))
            return mapListDown (e, *r);
    }

    void variationsDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        const auto p = e.getPosition();
        if (juce::Rectangle<int> (variations.getX() + 40, variations.getY() + 5, variations.getWidth() - 80, 18).contains (p))
            return edit ([] (PatcherDoc& d) { rack::storeVariation (d); });
        if (r.morphOn && morphSlider().contains (p))
        {
            drag = Drag::morph;
            return mouseDrag (e);
        }
        for (int v = 0; v < (int) r.variations.size(); ++v)
        {
            const auto row = variationRow (v);
            if (! row.contains (p))
                continue;
            if (e.mods.isPopupMenu())
                return showVariationMenu (v);
            if (p.x < row.getX() + 16)
                return edit ([v] (PatcherDoc& d) { rack::overwriteVariation (d, v); });
            if (p.x > row.getRight() - 16)
                return launch (v);
            return edit ([v] (PatcherDoc& d) { rack::rackOf (d)->selectedVariation = v; }, false);
        }
        if (e.mods.isPopupMenu())
            showVariationMenu (-1);
    }

    void chainsDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        const auto p = e.getPosition();
        // the zone editor's buttons
        auto strip = chains.withWidth (chainListWidth).withHeight (headerStrip).reduced (4, 3);
        const auto instrumentLike = r.kind != RackDoc::Kind::audioEffect;
        for (int k = 3; k >= 0; --k)
        {
            if ((! instrumentLike && (k == 0 || k == 1)) || (r.drum && k == 0))
                continue;
            if (strip.removeFromRight (40).contains (p))
                return edit ([k] (PatcherDoc& d) { rack::rackOf (d)->zoneEditor = k < 3 ? k : -1; }, false);
        }
        if (r.drum)
            for (int k = 0; k < 3; ++k)
                if (drumToggle (k).contains (p))
                    return edit ([k] (PatcherDoc& d) {
                        auto* x = rack::rackOf (d);
                        (k == 0 ? x->showIO : k == 1 ? x->showSends : x->showReturns) = ! (k == 0 ? x->showIO : k == 1 ? x->showSends : x->showReturns);
                    }, false);
        if (p.y < chains.getY() + headerStrip)
            return;
        for (int c = 0; c < listedSlots (r); ++c)
        {
            const auto row = slotRow (r, c);
            if (! row.contains (p) || ! rowVisible (row))
                continue;
            if (e.mods.isPopupMenu())
                return showChainMenu (c);
            const auto& chain = *rack::chainAt (r, c);
            auto cells = row.reduced (2, 1);
            const auto nameCell = cells.removeFromLeft (92);
            const auto volCell = r.kind != RackDoc::Kind::midiEffect ? cells.removeFromLeft (52) : juce::Rectangle<int>();
            const auto panCell = r.kind != RackDoc::Kind::midiEffect ? cells.removeFromLeft (30) : juce::Rectangle<int>();
            const auto ledCell = cells.removeFromLeft (18);
            const auto soloCell = cells.removeFromLeft (18);
            (void) nameCell;
            emptyPadKey = -1;
            edit ([c] (PatcherDoc& d) { rack::rackOf (d)->selectedChain = c; }, false);
            if (ledCell.contains (p))
                return edit ([c, on = ! chain.active] (PatcherDoc& d) { rack::setActive (d, c, on); });
            if (soloCell.contains (p))
                return edit ([c, on = ! chain.solo] (PatcherDoc& d) { rack::setSolo (d, c, on); });
            if (volCell.contains (p))
            {
                drag = Drag::volume;
                dragChain = c;
                dragStartValue = rack::volumeValue (chain.volume);
                if (ui::isResetClick (e))
                    edit ([c] (PatcherDoc& d) { rack::setChainVolume (d, c, 1.0f); });
                return;
            }
            if (panCell.contains (p))
            {
                drag = Drag::pan;
                dragChain = c;
                dragStartValue = chain.pan;
                if (ui::isResetClick (e))
                    edit ([c] (PatcherDoc& d) { rack::setChainPan (d, c, 0.0f); });
                return;
            }
            if (r.drum && ! rack::isReturnSlot (r, c) && p.x >= chains.getX() + chainListWidth)
                return drumCellDown (e, r, c, row);
            if (owner.isShowing() && r.showDevices == false)
                edit ([] (PatcherDoc& d) { rack::rackOf (d)->showDevices = true; }, false);
            return;
        }
        if (e.mods.isPopupMenu())
            showChainMenu (-1);
    }

    // a Drum Rack chain's I-O cell (a menu) or send cell (a drag; a reset click: −inf)
    void drumCellDown (const juce::MouseEvent& e, const RackDoc& r, int c, juce::Rectangle<int> row)
    {
        const auto p = e.getPosition();
        auto extra = row.withTrimmedLeft (chainListWidth).reduced (0, 1);
        if (r.showIO)
            for (int k = 0; k < 4; ++k)
                if (extra.removeFromLeft (ioWidths[k]).contains (p))
                    return showIoMenu (c, k);
        if (r.showSends)
            for (int k = 0; k < (int) r.returns.size(); ++k)
                if (extra.removeFromLeft (sendWidth).contains (p))
                {
                    const auto& chain = r.chains[(std::size_t) c];
                    const auto send = k < (int) chain.sends.size() ? chain.sends[(std::size_t) k] : RackSend {};
                    if (ui::isResetClick (e))
                        return edit ([c, k] (PatcherDoc& d) { rack::setSend (d, c, k, 0.0f); });
                    drag = Drag::send;
                    dragChain = c;
                    dragReturn = k;
                    dragStartValue = rack::volumeValue (send.level);
                    return;
                }
    }

    // Receive (All Notes + the keys by octave), Play (the keys), Choke (None, 1…16), Audio To (the rack's output — "Rack":
    // Live's wording needs a live check — or a return chain)
    void showIoMenu (int c, int column)
    {
        const auto* r = rack();
        if (r == nullptr || c < 0 || c >= (int) r->chains.size())
            return;
        const auto& chain = r->chains[(std::size_t) c];
        juce::PopupMenu menu;
        auto keys = [this, c] (juce::PopupMenu& m, int current, bool receive) {
            for (int octave = 0; octave < 11; ++octave)
            {
                juce::PopupMenu sub;
                bool has = false;
                for (int key = octave * 12; key < std::min (128, octave * 12 + 12); ++key)
                {
                    has = has || key == current;
                    sub.addItem (keyText (key), true, key == current, [this, c, key, receive] {
                        edit ([c, key, receive] (PatcherDoc& d) { receive ? rack::setReceive (d, c, key) : rack::setPlay (d, c, key); });
                    });
                }
                m.addSubMenu (keyText (octave * 12) + " - " + keyText (std::min (127, octave * 12 + 11)), sub, true, juce::Image(), has);
            }
        };
        switch (column)
        {
            case 0:
                menu.addItem (tr ("All Notes"), true, chain.receive < 0, [this, c] { edit ([c] (PatcherDoc& d) { rack::setReceive (d, c, -1); }); });
                menu.addSeparator();
                keys (menu, chain.receive, true);
                break;
            case 1: keys (menu, chain.play, false); break;
            case 2:
                for (int group = 0; group <= rack::drumChokeGroups; ++group)
                    menu.addItem (group == 0 ? tr ("None") : juce::String (group), true, chain.choke == group, [this, c, group] {
                        edit ([c, group] (PatcherDoc& d) { rack::setChoke (d, c, group); });
                    });
                break;
            default:
                menu.addItem (tr ("Rack"), true, chain.audioTo < 0, [this, c] { edit ([c] (PatcherDoc& d) { rack::setAudioTo (d, c, -1); }); });
                for (int k = 0; k < (int) r->returns.size(); ++k)
                    menu.addItem (str (r->returns[(std::size_t) k].name), true, chain.audioTo == k, [this, c, k] { edit ([c, k] (PatcherDoc& d) { rack::setAudioTo (d, c, k); }); });
                break;
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // the pad view: the overview scrolls, a pad selects its chain (or, empty, itself), its M / ▶ / S buttons, a drag
    // onto another pad swaps them (Live), the right button its menu
    void padsDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        const auto p = e.getPosition();
        if (overview.expanded (2).contains (p))
        {
            drag = Drag::padScroll;
            return mouseDrag (e);
        }
        const auto key = padAt (r, p);
        if (key < 0)
            return;
        const auto c = rack::padChain (r, key);
        if (e.mods.isPopupMenu())
            return c >= 0 ? showPadMenu (key, c) : showPadAddMenu (key);
        if (c >= 0)
        {
            const auto pad = padRect (r, key);
            const auto& chain = r.chains[(std::size_t) c];
            if (padButton (pad, 0).contains (p))
                return edit ([c, on = ! chain.active] (PatcherDoc& d) { rack::setActive (d, c, on); });
            if (padButton (pad, 2).contains (p))
                return edit ([c, on = ! chain.solo] (PatcherDoc& d) { rack::setSolo (d, c, on); });
            if (padButton (pad, 1).contains (p))
            {
                if (const auto channel = channelOfRack(); channel >= 0)
                {
                    previewKey = key;
                    engine.sendLiveNote (channel, key, 0.8f, true);
                    owner.repaint();
                }
                return;
            }
            emptyPadKey = -1;
            edit ([c] (PatcherDoc& d) { rack::rackOf (d)->selectedChain = c; }, false);
        }
        else
        {
            emptyPadKey = key;
            owner.repaint();
        }
        drag = Drag::pad;
        dragKey = key;
    }

    void zonesDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        const auto p = e.getPosition();
        if (r.zoneEditor == 2 && p.y < zones.getY() + headerStrip)
        {
            drag = Drag::selector;
            return mouseDrag (e);
        }
        for (int c = 0; c < (int) r.chains.size(); ++c)
        {
            const auto row = zoneRow (c);
            if (! row.expanded (0, 1).contains (p))
                continue;
            const auto z = zoneOf (r.chains[(std::size_t) c]);
            const auto x0 = zoneX (row, (float) z.lo), x1 = zoneX (row, (float) z.hi + 1.0f);
            const auto f0 = zoneX (row, (float) z.fadeLo), f1 = zoneX (row, (float) z.fadeHi + 1.0f);
            const auto upper = p.y < row.getY() + 7; // the upper strip sets the fades (Live [MAN] 25.5)
            const auto nearX = [&p] (float x) { return std::abs ((float) p.x - x) <= 4.0f; };
            dragChain = c;
            dragZone = z;
            if (upper)
                drag = nearX (f0) ? Drag::zoneFadeLo : nearX (f1) ? Drag::zoneFadeHi : Drag::none;
            else
                drag = nearX (x0) ? Drag::zoneLo : nearX (x1) ? Drag::zoneHi : ((float) p.x > x0 && (float) p.x < x1 ? Drag::zoneMove : Drag::none);
            edit ([c] (PatcherDoc& d) { rack::rackOf (d)->selectedChain = c; }, false);
            return;
        }
    }

    void devicesDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        const auto p = e.getPosition();
        const auto& list = shownDevices (r);
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto card = deviceCard (i);
            if (! card.contains (p))
                continue;
            const auto n = list[(std::size_t) i];
            if (e.mods.isPopupMenu())
                return showDeviceMenu (r.selectedChain, i, n);
            if (card.withHeight (20).reduced (4, 2).withWidth (16).contains (p) && ! shown.nodes[(std::size_t) n].generator)
                return edit ([n] (PatcherDoc& d) {
                    if (n < (int) d.nodes.size())
                    {
                        d.nodes[(std::size_t) n].effect.enabled = ! d.nodes[(std::size_t) n].effect.enabled;
                        rack::compile (d);
                    }
                });
            if (card.withTrimmedTop (card.getHeight() - 26).contains (p) || e.getNumberOfClicks() > 1)
                openDevice (n);
            return;
        }
        if (deviceCard ((int) list.size()).contains (p))
        {
            if (emptyPadPane (r))
                return showPadAddMenu (emptyPadKey >= 0 ? emptyPadKey : firstFreeKey (r));
            showAddMenu (r.selectedChain);
        }
    }

    void mapListDown (const juce::MouseEvent& e, const RackDoc& r)
    {
        int i = 0;
        for (int m = 0; m < rackMacroCount; ++m)
            for (int k = 0; k < (int) r.macros[(std::size_t) m].mappings.size(); ++k)
            {
                auto row = mapRow (i++);
                if (! row.contains (e.getPosition()))
                    continue;
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu menu;
                    menu.addItem (tr ("Invert Range"), [this, m, k] { edit ([m, k] (PatcherDoc& d) { rack::invertRange (d, m, k); }); });
                    menu.addItem (tr ("Delete"), [this, m, k] { edit ([m, k] (PatcherDoc& d) { rack::unmap (d, m, k); }); });
                    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner));
                    return;
                }
                row.removeFromLeft (66 + 104 + 84);
                const auto minCell = row.removeFromLeft (34), maxCell = row.removeFromLeft (34);
                const auto& x = r.macros[(std::size_t) m].mappings[(std::size_t) k];
                if (minCell.contains (e.getPosition()) || maxCell.contains (e.getPosition()))
                {
                    drag = minCell.contains (e.getPosition()) ? Drag::mapMin : Drag::mapMax;
                    dragMacro = m;
                    dragMapping = k;
                    dragStartValue = drag == Drag::mapMin ? x.min : x.max;
                }
                return;
            }
    }

    void mouseDrag (const juce::MouseEvent& e)
    {
        const auto* r = rack();
        if (r == nullptr || drag == Drag::none)
            return;
        const auto dy = (float) (dragStart.y - e.getPosition().y) * (e.mods.isCtrlDown() ? 0.1f : 1.0f);
        switch (drag)
        {
            case Drag::volume:
            {
                const auto v = std::clamp (dragStartValue + dy / 150.0f, 0.0f, 1.0f);
                edit ([c = dragChain, v] (PatcherDoc& d) { rack::setChainVolume (d, c, rack::volumeGain (v)); }, false);
                break;
            }
            case Drag::pan:
                edit ([c = dragChain, v = std::clamp (dragStartValue + dy / 100.0f, -1.0f, 1.0f)] (PatcherDoc& d) { rack::setChainPan (d, c, v); }, false);
                break;
            case Drag::selector:
                edit ([v = std::round (zoneValueAt (zoneRow (0), e.getPosition().x))] (PatcherDoc& d) { rack::setSelector (d, v); }, false);
                break;
            case Drag::morph:
            {
                const auto s = morphSlider();
                edit ([v = std::clamp ((float) (e.getPosition().x - s.getX()) / (float) s.getWidth(), 0.0f, 1.0f)] (PatcherDoc& d) { rack::setMorph (d, v); }, false);
                break;
            }
            case Drag::mapMin:
            case Drag::mapMax:
            {
                const auto v = std::clamp (dragStartValue + dy / 150.0f, 0.0f, 1.0f);
                edit ([m = dragMacro, k = dragMapping, low = drag == Drag::mapMin, v] (PatcherDoc& d) {
                    auto* x = rack::rackOf (d);
                    if (k < (int) x->macros[(std::size_t) m].mappings.size())
                    {
                        auto& mapping = x->macros[(std::size_t) m].mappings[(std::size_t) k];
                        (low ? mapping.min : mapping.max) = v;
                        rack::compile (d);
                    }
                }, false);
                break;
            }
            case Drag::zoneMove:
            case Drag::zoneLo:
            case Drag::zoneHi:
            case Drag::zoneFadeLo:
            case Drag::zoneFadeHi:
            {
                const auto row = zoneRow (dragChain);
                const auto steps = (int) std::lround ((float) (e.getPosition().x - dragStart.x) * (128.0f - (float) zoneMin()) / (float) row.getWidth());
                auto z = dragZone;
                const auto lo = zoneMin();
                switch (drag)
                {
                    case Drag::zoneMove:
                    {
                        const auto shift = std::clamp (steps, lo - z.lo, 127 - z.hi);
                        z.lo += shift, z.hi += shift, z.fadeLo += shift, z.fadeHi += shift;
                        break;
                    }
                    case Drag::zoneLo:
                        z.lo = std::clamp (z.lo + steps, lo, z.hi);
                        z.fadeLo = std::max (z.fadeLo, z.lo);
                        z.fadeHi = std::max (z.fadeHi, z.fadeLo);
                        break;
                    case Drag::zoneHi:
                        z.hi = std::clamp (z.hi + steps, z.lo, 127);
                        z.fadeHi = std::min (z.fadeHi, z.hi);
                        z.fadeLo = std::min (z.fadeLo, z.fadeHi);
                        break;
                    case Drag::zoneFadeLo: z.fadeLo = std::clamp (z.fadeLo + steps, z.lo, z.fadeHi); break;
                    default: z.fadeHi = std::clamp (z.fadeHi + steps, z.fadeLo, z.hi); break;
                }
                edit ([this, c = dragChain, z] (PatcherDoc& d) { setZone (d, c, z); }, false);
                break;
            }
            case Drag::padScroll: // (the overview: the row under the mouse becomes the second shown)
            {
                const auto rowH = (float) overview.getHeight() / (float) rack::drumPadRows;
                const auto row = (int) std::floor ((float) (e.getPosition().y - overview.getY()) / std::max (1.0f, rowH)) - 1;
                if (juce::jlimit (0, rack::drumPadRows - 4, row) != r->padScroll)
                    setPadScroll (row);
                break;
            }
            case Drag::pad: owner.repaint(); break;
            case Drag::send:
            {
                const auto v = std::clamp (dragStartValue + dy / 150.0f, 0.0f, rack::unityVolume); // (−inf … 0 dB)
                edit ([c = dragChain, k = dragReturn, v] (PatcherDoc& d) {
                    const auto* x = rack::rackOf (d);
                    const auto on = k < (int) x->chains[(std::size_t) c].sends.size() ? x->chains[(std::size_t) c].sends[(std::size_t) k].on : true;
                    rack::setSend (d, c, k, v <= 0.0f ? 0.0f : rack::volumeGain (v), on);
                }, false);
                break;
            }
            case Drag::none: break;
        }
    }

    void mouseUp (const juce::MouseEvent& e)
    {
        if (previewKey >= 0) // (a pad's Play button let go: the note ends)
        {
            if (const auto channel = channelOfRack(); channel >= 0)
                engine.sendLiveNote (channel, previewKey, 0.0f, false);
            previewKey = -1;
            owner.repaint();
        }
        if (drag == Drag::pad)
        {
            drag = Drag::none;
            const auto* r = rack();
            const auto to = r != nullptr ? padAt (*r, e.getPosition()) : -1;
            const auto from = dragKey;
            dragKey = -1;
            if (r != nullptr && to >= 0 && to != from && e.getDistanceFromDragStart() > 4)
            {
                // Live's drag of a pad onto another: the two trade places (an empty one too)
                const auto had = rack::padChain (*r, from) >= 0;
                edit ([from, to] (PatcherDoc& d) { rack::swapPads (d, from, to); });
                emptyPadKey = had ? -1 : to;
            }
            else if (r != nullptr && from >= 0 && rack::padChain (*r, from) < 0 && e.getDistanceFromDragStart() <= 4)
                showPadAddMenu (from); // (a click on an empty pad: what to put on it)
            owner.repaint();
            return;
        }
        if (drag == Drag::padScroll)
        {
            drag = Drag::none;
            return;
        }
        if (drag != Drag::none)
            edit ([] (PatcherDoc&) {}); // (one undo step for the whole drag)
        drag = Drag::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto* r = rack();
        if (r == nullptr)
            return;
        for (int c = 0; c < listedSlots (*r); ++c)
            if (slotRow (*r, c).withWidth (96).contains (e.getPosition()) && chains.contains (e.getPosition()))
                return renameChain (c);
        if (r->drum && pads.contains (e.getPosition()))
            if (const auto key = padAt (*r, e.getPosition()); key >= 0 && rack::padChain (*r, key) >= 0)
                return renameChain (rack::padChain (*r, key));
        for (int v = 0; v < (int) r->variations.size(); ++v)
            if (variationRow (v).reduced (18, 0).contains (e.getPosition()))
                return renameVariation (v);
        for (int m = 0; m < r->shownMacros; ++m)
            if (macroCell (m).withHeight (16).contains (e.getPosition()))
                return renameMacro (m);
    }

    // --- menus and dialogs ------------------------------------------------------------------------------------------
    void askName (const juce::String& title, const juce::String& current, std::function<void (juce::String)> done)
    {
        auto* box = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, &owner);
        box->addTextEditor ("name", current);
        box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<RackView> safe (&owner);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([box, safe, done] (int result) {
                                  if (result == 1 && safe != nullptr)
                                      done (box->getTextEditorContents ("name").trim());
                              }),
                              true);
    }

    void renameRack()
    {
        askName (tr ("Rename"), str (rack()->name), [this] (juce::String n) {
            edit ([n] (PatcherDoc& d) { rack::rackOf (d)->name = n.toStdString(); });
        });
    }

    void renameChain (int c)
    {
        const auto* r = rack();
        const auto* chain = r != nullptr ? rack::chainAt (*r, c) : nullptr;
        if (chain == nullptr)
            return;
        askName (tr ("Rename chain"), str (chain->name), [this, c] (juce::String n) {
            if (n.isNotEmpty())
                edit ([c, n] (PatcherDoc& d) {
                    if (auto* x = rack::chainAt (*rack::rackOf (d), c))
                        x->name = n.toStdString();
                });
        });
    }

    void renameVariation (int v)
    {
        const auto* r = rack();
        askName (tr ("Rename variation"), str (r->variations[(std::size_t) v].name), [this, v] (juce::String n) {
            edit ([v, n] (PatcherDoc& d) { rack::renameVariation (d, v, n.toStdString()); });
        });
    }

    void renameMacro (int m)
    {
        askName (tr ("Rename macro"), macroName (m), [this, m] (juce::String n) {
            edit ([m, n] (PatcherDoc& d) {
                rack::rackOf (d)->macros[(std::size_t) m].name = n.toStdString();
                rack::compile (d);
            });
        });
    }

    void showChainMenu (int c)
    {
        juce::PopupMenu menu;
        if (drum()) // (round 190ec) a Drum Rack: a new chain on the first free pad; return chains
        {
            const auto* r = rack();
            menu.addItem (tr ("Add chain"), [this, key = firstFreeKey (*r)] {
                emptyPadKey = -1;
                edit ([key] (PatcherDoc& d) { rack::addPad (d, key, tr ("Chain").toStdString()); });
            });
            menu.addItem (tr ("Add return chain"), [this] { edit ([] (PatcherDoc& d) { rack::addReturn (d); rack::rackOf (d)->showReturns = true; }); });
            if (rack::isReturnSlot (*r, c))
            {
                menu.addItem (tr ("Rename"), [this, c] { renameChain (c); });
                menu.addItem (tr ("Delete return chain"), [this, k = c - (int) r->chains.size()] { edit ([k] (PatcherDoc& d) { rack::removeReturn (d, k); }); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
                return;
            }
        }
        else
            menu.addItem (tr ("Add chain"), [this] { edit ([] (PatcherDoc& d) { rack::addChain (d); }); });
        if (c >= 0)
        {
            menu.addItem (tr ("Rename"), [this, c] { renameChain (c); });
            menu.addItem (tr ("Duplicate"), [this, c] { edit ([c] (PatcherDoc& d) { rack::duplicateChain (d, c); }); });
            menu.addItem (tr ("Delete"), rack()->chains.size() > 1 || drum(), false, [this, c] { edit ([c] (PatcherDoc& d) { rack::removeChain (d, c); }); });
            menu.addSeparator();
            menu.addItem (tr ("Move up"), c > 0, false, [this, c] { edit ([c] (PatcherDoc& d) { rack::moveChain (d, c, c - 1); }); });
            menu.addItem (tr ("Move down"), c + 1 < (int) rack()->chains.size(), false, [this, c] { edit ([c] (PatcherDoc& d) { rack::moveChain (d, c, c + 1); }); });
        }
        // a standalone MIDI Effect Rack (decision R1): the channels it plays, set from the Channel Rack's selection as a
        // Layer's children are
        if (rack()->kind == RackDoc::Kind::midiEffect && automation && automation->kind == AutomationTargetKind::channelParam)
        {
            menu.addSeparator();
            menu.addItem (tr ("Set children"), [this, self = automation->index] {
                auto selected = engine.getSelectedChannels();
                std::erase (selected, self);
                edit ([selected] (PatcherDoc& d) { rack::rackOf (d)->children = std::vector<int> (selected.begin(), selected.end()); });
            });
            menu.addItem (tr ("Show children"), ! rack()->children.empty(), false, [this] {
                const auto children = rack()->children;
                engine.setChannelSelection (children, children.front());
            });
        }
        menu.addSeparator();
        menu.addItem (tr ("Chain Selector Filters MIDI Ctrl"), rack()->kind != RackDoc::Kind::audioEffect, rack()->selectorFiltersCtrl, [this] {
            edit ([] (PatcherDoc& d) {
                rack::rackOf (d)->selectorFiltersCtrl = ! rack::rackOf (d)->selectorFiltersCtrl;
                rack::compile (d);
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    void showVariationMenu (int v)
    {
        const auto* r = rack();
        juce::PopupMenu menu;
        menu.addItem (tr ("New"), [this] { edit ([] (PatcherDoc& d) { rack::storeVariation (d); }); });
        if (v >= 0)
        {
            menu.addItem (tr ("Launch"), [this, v] { launch (v); });
            menu.addItem (tr ("Overwrite"), [this, v] { edit ([v] (PatcherDoc& d) { rack::overwriteVariation (d, v); }); });
            menu.addItem (tr ("Rename"), [this, v] { renameVariation (v); });
            menu.addItem (tr ("Duplicate"), [this, v] { edit ([v] (PatcherDoc& d) { rack::duplicateVariation (d, v); }); });
            menu.addItem (tr ("Delete"), [this, v] { edit ([v] (PatcherDoc& d) { rack::deleteVariation (d, v); }); });
        }
        // LYNX's own (decision D6): morph between two variations, off by default
        menu.addSeparator();
        menu.addItem (tr ("Variation morph (LYNX)"), true, r->morphOn, [this] {
            edit ([] (PatcherDoc& d) {
                auto* x = rack::rackOf (d);
                x->morphOn = ! x->morphOn;
                if (x->morphOn && x->morphA < 0 && x->variations.size() >= 2)
                    x->morphA = 0, x->morphB = 1;
                rack::compile (d);
            });
        });
        juce::PopupMenu glideMenu; // LYNX's own (decision D6): a launch moves the macros over a time instead of jumping
        for (const auto ms : { 0, 100, 250, 500, 1000, 2000, 4000 })
            glideMenu.addItem (ms == 0 ? tr ("Off") : juce::String (ms) + " ms", true, std::abs (r->glideMs - (float) ms) < 0.5f, [this, ms] {
                edit ([ms] (PatcherDoc& d) { rack::rackOf (d)->glideMs = (float) ms; });
            });
        menu.addSubMenu (tr ("Launch glide (LYNX)"), glideMenu);
        if (v >= 0 && r->morphOn)
        {
            menu.addItem (tr ("Morph from this variation (A)"), [this, v] { edit ([v] (PatcherDoc& d) { rack::rackOf (d)->morphA = v; rack::compile (d); }); });
            menu.addItem (tr ("Morph to this variation (B)"), [this, v] { edit ([v] (PatcherDoc& d) { rack::rackOf (d)->morphB = v; rack::compile (d); }); });
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // a variation's launch: Live's instant jump, or LYNX's glide when one is set
    void launch (int v)
    {
        const auto* r = rack();
        if (r == nullptr || v < 0 || v >= (int) r->variations.size())
            return;
        if (r->glideMs <= 0.0f)
            return edit ([v] (PatcherDoc& d) { rack::launchVariation (d, v); });
        Glide g;
        const auto& x = r->variations[(std::size_t) v];
        for (int m = 0; m < rackMacroCount; ++m)
        {
            g.from[(std::size_t) m] = r->macros[(std::size_t) m].value;
            g.to[(std::size_t) m] = x.values[(std::size_t) m];
            g.moves[(std::size_t) m] = x.has[(std::size_t) m] && ! r->macros[(std::size_t) m].excludeFromVariations;
        }
        g.start = juce::Time::getMillisecondCounterHiRes();
        g.length = r->glideMs;
        g.variation = v;
        glide = g;
    }

    void stepGlide()
    {
        if (! glide)
            return;
        const auto t = std::clamp ((juce::Time::getMillisecondCounterHiRes() - glide->start) / std::max (1.0, glide->length), 0.0, 1.0);
        const auto g = *glide;
        if (t >= 1.0)
            glide.reset();
        edit ([g, t] (PatcherDoc& d) {
            for (int m = 0; m < rackMacroCount; ++m)
                if (g.moves[(std::size_t) m])
                    rack::setMacro (d, m, g.from[(std::size_t) m] + (g.to[(std::size_t) m] - g.from[(std::size_t) m]) * (float) t);
            if (t >= 1.0)
                rack::rackOf (d)->selectedVariation = g.variation;
        }, t >= 1.0);
    }

    void showMacroMenu (int m)
    {
        const auto* r = rack();
        const auto& macro = r->macros[(std::size_t) m];
        juce::PopupMenu menu;
        menu.addItem (tr ("Rename"), [this, m] { renameMacro (m); });
        menu.addItem (tr ("Exclude Macro from Randomization"), true, macro.excludeFromRandom, [this, m] {
            edit ([m] (PatcherDoc& d) { auto& x = rack::rackOf (d)->macros[(std::size_t) m]; x.excludeFromRandom = ! x.excludeFromRandom; });
        });
        menu.addItem (tr ("Exclude Macro from Variations"), true, macro.excludeFromVariations, [this, m] {
            edit ([m] (PatcherDoc& d) {
                auto& x = rack::rackOf (d)->macros[(std::size_t) m];
                x.excludeFromVariations = ! x.excludeFromVariations;
                rack::compile (d);
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // Map: what macro m can drive (LYNX: a menu of the rack's devices and their parameters, the chains' volume / pan and
    // the chain selector; Live highlights the parameters themselves [MAN] 25.7.1)
    void showMapMenu (int m)
    {
        const auto* r = rack();
        juce::PopupMenu menu;
        auto add = [this, m] (juce::PopupMenu& to, const juce::String& name, RackMapping x) {
            const auto owner_ = rack::macroOf (*rack(), x);
            to.addItem (name, true, owner_ == m, [this, m, x] { edit ([m, x] (PatcherDoc& d) { rack::map (d, m, x); }); });
        };
        for (int c = 0; c < (int) r->chains.size(); ++c)
        {
            const auto& chain = r->chains[(std::size_t) c];
            juce::PopupMenu sub;
            if (r->kind != RackDoc::Kind::midiEffect)
            {
                RackMapping v;
                v.target = RackMapping::Target::chainVolume, v.chain = c, v.min = 0.0f, v.max = rack::unityVolume;
                add (sub, tr ("Chain Volume"), v);
                RackMapping p;
                p.target = RackMapping::Target::chainPan, p.chain = c;
                add (sub, tr ("Chain Pan"), p);
            }
            for (const auto n : chain.nodes)
            {
                const auto& node = shown.nodes[(std::size_t) n];
                juce::PopupMenu params;
                int count = 0;
                if (rack::isRack (node.effect) || rack::isRack (node.channel))
                    count = rackMacroCount;
                else if (! node.generator && node.effect.kind != EffectKind::plugin && node.effect.kind != EffectKind::patcher)
                    count = (int) effectInfo (node.effect.kind).params.size();
                else if (auto* fx = dynamic_cast<PluginEffect*> (runningNode (n).get()); fx != nullptr && ! fx->hasCrashed())
                    count = std::min (256, fx->getInstance().getParameters().size());
                else
                    count = node.generator ? 0 : 32;
                for (int k = 0; k < count; ++k)
                {
                    RackMapping x;
                    x.node = n, x.param = k;
                    add (params, parameterName (node, k, runningNode (n)), x);
                }
                if (params.getNumItems() > 0)
                    sub.addSubMenu (nodeName (n), params);
            }
            menu.addSubMenu (str (chain.name), sub);
        }
        RackMapping sel;
        sel.target = RackMapping::Target::selector;
        add (menu, tr ("Chain Selector"), sel);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    void showDeviceMenu (int chain, int position, int node)
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Edit"), [this, node] { openDevice (node); });
        menu.addItem (tr ("Move left"), position > 0, false, [this, chain, position] { edit ([chain, position] (PatcherDoc& d) { rack::moveDevice (d, chain, position, position - 1); }); });
        menu.addItem (tr ("Move right"), true, false, [this, chain, position] { edit ([chain, position] (PatcherDoc& d) { rack::moveDevice (d, chain, position, position + 1); }); });
        menu.addSeparator();
        menu.addItem (tr ("Delete"), [this, chain, position] { edit ([chain, position] (PatcherDoc& d) { rack::removeDevice (d, chain, position); }); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    void addDevice (int chain, PatchNode node)
    {
        edit ([chain, node] (PatcherDoc& d) {
            if (rack::addDevice (d, chain, node) < 0)
                juce::ignoreUnused (d);
        });
    }

    // the "+" card: what the rack kind takes (Live [MAN] 25.2): audio effects; MIDI effects; or both and one instrument
    void showAddMenu (int chain)
    {
        const auto* r = rack();
        juce::PopupMenu menu;
        juce::Component::SafePointer<RackView> safe (&owner);
        if (r->kind != RackDoc::Kind::midiEffect)
        {
            juce::PopupMenu builtIn;
            for (const auto kind : addableEffects())
                builtIn.addItem (addableEffectName (kind), addableEffectAvailable (kind), false, [this, chain, kind] {
                    if (const auto fx = newAddableEffect (kind, engine.getSong().bpm))
                    {
                        PatchNode n;
                        n.effect = *fx;
                        addDevice (chain, n);
                    }
                });
            juce::PopupMenu plugins;
            addPluginItems (plugins, engine, false, [safe, chain] (PluginRef ref) {
                if (safe == nullptr)
                    return;
                PatchNode n;
                n.effect.kind = EffectKind::plugin;
                n.effect.plugin = ref;
                safe->impl->addDevice (chain, n);
            }, &owner);
            menu.addSubMenu (tr ("Effects"), builtIn);
            menu.addSubMenu (tr ("Plugin effects"), plugins);
            juce::PopupMenu rackMenu;
            rackMenu.addItem (tr ("Audio Effect Rack"), [this, chain] {
                PatchNode n;
                n.effect = rack::makeRackSlot (RackDoc::Kind::audioEffect);
                addDevice (chain, n);
            });
            menu.addSubMenu (tr ("Racks"), rackMenu);
        }
        const auto returnSlot = rack::isReturnSlot (*r, chain); // (a Drum Rack's return chain: audio effects only)
        if (r->kind != RackDoc::Kind::audioEffect && ! returnSlot)
        {
            juce::PopupMenu midi;
            for (const auto kind : vfx::allKinds)
                midi.addItem (vfx::flName (kind), [this, chain, kind] {
                    PatchNode n;
                    n.generator = true;
                    vfx::makeChannel (n.channel, kind);
                    addDevice (chain, n);
                });
            menu.addSubMenu (tr ("MIDI effects"), midi);
        }
        if (r->kind == RackDoc::Kind::instrument && ! returnSlot)
        {
            juce::PopupMenu instruments;
            instruments.addItem (tr ("Synth"), [this, chain] {
                PatchNode n;
                n.generator = true;
                n.channel.instrument = InstrumentKind::synth;
                n.channel.name = tr ("Synth").toStdString();
                addDevice (chain, n);
            });
            instruments.addItem (tr ("Instrument Rack"), [this, chain] {
                PatchNode n;
                n.generator = true;
                n.channel.instrument = InstrumentKind::patcher;
                n.channel.patcher = rack::makeRack (RackDoc::Kind::instrument);
                addDevice (chain, n);
            });
            instruments.addItem (tr ("Drum Rack"), [this, chain] { // (round 190ec: Live nests a Drum Rack in a chain too)
                PatchNode n;
                n.generator = true;
                n.channel.instrument = InstrumentKind::patcher;
                n.channel.patcher = rack::makeDrumRack();
                addDevice (chain, n);
            });
            juce::PopupMenu plugins;
            addPluginItems (plugins, engine, true, [safe, chain] (PluginRef ref) {
                if (safe == nullptr)
                    return;
                PatchNode n;
                n.generator = true;
                n.channel.instrument = InstrumentKind::plugin;
                n.channel.name = ref.name;
                n.channel.plugin = ref;
                safe->impl->addDevice (chain, n);
            }, &owner);
            instruments.addSubMenu (tr ("Plugin instruments"), plugins);
            menu.addSubMenu (tr ("Instruments"), instruments);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // --- Drum Rack pads (round 190ec) ---------------------------------------------------------------------------------
    // a pad's menu: Rename, Duplicate, Delete, its choke group
    void showPadMenu (int key, int c)
    {
        const auto* r = rack();
        if (r == nullptr || c < 0 || c >= (int) r->chains.size())
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader (keyText (key));
        menu.addItem (tr ("Rename"), [this, c] { renameChain (c); });
        menu.addItem (tr ("Duplicate"), [this, c] { edit ([c] (PatcherDoc& d) { rack::duplicateChain (d, c); }); });
        menu.addItem (tr ("Delete"), [this, c, key] {
            emptyPadKey = key;
            edit ([c] (PatcherDoc& d) { rack::removeChain (d, c); });
        });
        juce::PopupMenu choke;
        for (int group = 0; group <= rack::drumChokeGroups; ++group)
            choke.addItem (group == 0 ? tr ("None") : juce::String (group), true, r->chains[(std::size_t) c].choke == group,
                           [this, c, group] { edit ([c, group] (PatcherDoc& d) { rack::setChoke (d, c, group); }); });
        menu.addSubMenu (tr ("Choke"), choke);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // what to put on an empty pad: a sample (a Sampler, as Live's pad Simpler), the Synth, a rack, a plugin, an empty chain
    void showPadAddMenu (int key)
    {
        juce::PopupMenu menu;
        juce::Component::SafePointer<RackView> safe (&owner);
        menu.addSectionHeader (keyText (key));
        menu.addItem (tr ("Load sample..."), [this, key] { chooseSample (key); });
        menu.addSeparator();
        auto generator = [] (InstrumentKind kind, const juce::String& name) {
            PatchNode n;
            n.generator = true;
            n.channel.instrument = kind;
            n.channel.name = name.toStdString();
            return n;
        };
        menu.addItem (tr ("Synth"), [this, key, generator] { addPadDevice (key, tr ("Synth"), generator (InstrumentKind::synth, tr ("Synth"))); });
        menu.addItem (tr ("Instrument Rack"), [this, key, generator] {
            auto n = generator (InstrumentKind::patcher, tr ("Instrument Rack"));
            n.channel.patcher = rack::makeRack (RackDoc::Kind::instrument);
            addPadDevice (key, tr ("Instrument Rack"), n);
        });
        menu.addItem (tr ("Drum Rack"), [this, key, generator] {
            auto n = generator (InstrumentKind::patcher, tr ("Drum Rack"));
            n.channel.patcher = rack::makeDrumRack();
            addPadDevice (key, tr ("Drum Rack"), n);
        });
        juce::PopupMenu plugins;
        addPluginItems (plugins, engine, true, [safe, key] (PluginRef ref) {
            if (safe == nullptr)
                return;
            PatchNode n;
            n.generator = true;
            n.channel.instrument = InstrumentKind::plugin;
            n.channel.name = ref.name;
            n.channel.plugin = ref;
            safe->impl->addPadDevice (key, str (ref.name), n);
        }, &owner);
        menu.addSubMenu (tr ("Plugin instruments"), plugins);
        menu.addSeparator();
        menu.addItem (tr ("Empty chain"), [this, key] {
            emptyPadKey = -1;
            edit ([key] (PatcherDoc& d) { rack::addPad (d, key, tr ("Chain").toStdString()); });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&owner).withMousePosition());
    }

    // `node` on pad `key`: a new pad chain, or in place of the pad's instrument (the chain takes its name, as Live's does)
    void addPadDevice (int key, const juce::String& name, PatchNode node)
    {
        emptyPadKey = -1;
        edit ([key, name = name.toStdString(), node] (PatcherDoc& d) {
            auto* r = rack::rackOf (d);
            if (r == nullptr || ! r->drum)
                return;
            const auto c = rack::padChain (*r, key);
            if (c < 0)
            {
                rack::addPad (d, key, name, node);
                return;
            }
            const auto list = r->chains[(std::size_t) c].nodes;
            for (int i = 0; i < (int) list.size(); ++i)
                if (const auto& x = d.nodes[(std::size_t) list[(std::size_t) i]]; x.generator && x.channel.instrument != InstrumentKind::vfx)
                {
                    rack::removeDevice (d, c, i);
                    break;
                }
            rack::addDevice (d, c, node);
            r = rack::rackOf (d);
            r->chains[(std::size_t) c].name = name;
            r->selectedChain = c;
        });
    }

    // a sample on pad `key`: a Sampler playing it whole (release 0: one-shot, Live's default pad Simpler [DATA])
    void loadSampleOnPad (int key, const juce::File& file)
    {
        PatchNode n;
        n.generator = true;
        n.channel.instrument = InstrumentKind::sampler;
        n.channel.name = file.getFileNameWithoutExtension().toStdString();
        n.channel.samplePath = file.getFullPathName().toStdString();
        n.channel.sampler.release = 0.0f;
        addPadDevice (key, file.getFileNameWithoutExtension(), n);
    }

    void chooseSample (int key)
    {
        chooser = lynx::ui::makeFileChooser (tr ("Load sample..."), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<RackView> safe (&owner);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, key] (const juce::FileChooser& fc) {
            if (safe != nullptr && fc.getResult().existsAsFile())
                safe->impl->loadSampleOnPad (key, fc.getResult());
        });
    }

    // samples dropped on the pad view (from the pad they land on up, one pad each) or on the devices of the selected pad
    bool dropSamples (const juce::StringArray& files, juce::Point<int> p)
    {
        const auto* r = rack();
        if (r == nullptr || ! r->drum)
            return false;
        auto key = padAt (*r, p);
        if (key < 0 && (devices.contains (p) || chains.contains (p)))
            key = selectedPadKey (*r);
        if (key < 0)
            key = firstFreeKey (*r);
        for (const auto& f : files)
            if (const juce::File file (f); SampleLibrary::isSampleFile (file) && key < rack::drumPadCount)
                loadSampleOnPad (key++, file);
        return true;
    }

    // a device's own editor: a nested rack opens as a rack; anything else as in the Patcher (plugin window, built-in editor)
    void openDevice (int n)
    {
        if (n < 0 || n >= (int) shown.nodes.size())
            return;
        const auto& node = shown.nodes[(std::size_t) n];
        if (rack::isRack (node.effect) || rack::isRack (node.channel))
        {
            const auto generator = node.generator;
            RackLocator inner = [outer = locate, n, generator] (Song& song) -> PatcherDoc* {
                auto* d = outer ? outer (song) : nullptr;
                if (d == nullptr || n >= (int) d->nodes.size())
                    return nullptr;
                auto& x = d->nodes[(std::size_t) n];
                return generator ? (rack::isRack (x.channel) ? &x.channel.patcher : nullptr) : (rack::isRack (x.effect) ? &x.effect.patcher : nullptr);
            };
            auto view = std::make_unique<RackView> (engine, inner, std::nullopt, key + "-" + juce::String (n), true, nullptr);
            view->setSize (720, 230);
            if (auto* w = openToolWindow (std::move (view), nodeName (n), key + "-rack-" + juce::String (n)))
                w->setResizable (true, false);
            return;
        }
        if (openNode)
            openNode (n);
    }
};

RackView::RackView (AudioEngine& engine, RackLocator locate, std::optional<AutomationTarget> automation, juce::String key, bool ownHeader,
                    std::function<void (int)> openNode)
    : impl (std::make_unique<Impl> (*this, engine, std::move (locate), std::move (automation), std::move (key), ownHeader, std::move (openNode)))
{
    impl->refresh();
    startTimerHz (30);
}

RackView::~RackView() = default;

int RackView::preferredWidth (int height) const { return impl->width (height); }
int RackView::minimumWidth (int height) const { return impl->width (height, true); }
void RackView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! impl->wheel (e.getPosition(), std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY))
        juce::Component::mouseWheelMove (e, w);
}

std::unique_ptr<juce::Component> RackView::makeTitleAccessory()
{
    struct Accessory final : juce::Component
    {
        juce::TextButton rand { "Rand" }, map { "Map" };
        void resized() override
        {
            auto a = getLocalBounds();
            map.setBounds (a.removeFromRight (30));
            a.removeFromRight (2);
            rand.setBounds (a.removeFromRight (34));
        }
    };
    auto a = std::make_unique<Accessory>();
    a->addAndMakeVisible (a->rand);
    a->addAndMakeVisible (a->map);
    a->map.setClickingTogglesState (true);
    juce::Component::SafePointer<RackView> safe (this);
    a->rand.setTooltip (impl->rand.getTooltip());
    a->map.setTooltip (impl->map.getTooltip());
    a->rand.onClick = [safe] {
        if (safe != nullptr)
            safe->impl->rand.onClick();
    };
    a->map.onClick = [safe, m = &a->map] {
        if (safe != nullptr)
            safe->setMapMode (m->getToggleState());
    };
    return a;
}

namespace
{
// Map mode in a title bar that isn't the rack's own (the Device View card, decision R2): the mapping list as a pop-up
// next to the rack, drawn and handled by the rack view itself
struct MapListPopup final : juce::Component, private juce::Timer
{
    juce::Component::SafePointer<RackView> view;
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> paintList;
    std::function<void (const juce::MouseEvent&, juce::Rectangle<int>, int)> mouse; // (0 down, 1 drag, 2 up)
    MapListPopup()
    {
        setSize (mapListWidth, 220);
        startTimerHz (15);
    }
    void paint (juce::Graphics& g) override
    {
        if (view != nullptr)
            paintList (g, getLocalBounds());
    }
    void mouseDown (const juce::MouseEvent& e) override { if (view != nullptr) mouse (e, getLocalBounds(), 0); }
    void mouseDrag (const juce::MouseEvent& e) override { if (view != nullptr) mouse (e, getLocalBounds(), 1); }
    void mouseUp (const juce::MouseEvent& e) override { if (view != nullptr) mouse (e, getLocalBounds(), 2); }
    void timerCallback() override { repaint(); }
};
} // namespace

void RackView::setPowerSwitch (std::function<bool()> isOn, std::function<void()> toggle)
{
    impl->isOn = std::move (isOn);
    impl->toggleOn = std::move (toggle);
    repaint();
}

void RackView::setMapMode (bool on)
{
    mapping = on;
    impl->map.setToggleState (on, juce::dontSendNotification);
    resized();
    repaint();
    if (on && ! impl->ownHeader && isShowing())
    {
        auto popup = std::make_unique<MapListPopup>();
        popup->view = this;
        juce::Component::SafePointer<RackView> safe (this);
        popup->paintList = [safe] (juce::Graphics& g, juce::Rectangle<int> area) {
            if (safe == nullptr || safe->impl->rack() == nullptr)
                return;
            const auto saved = safe->impl->mapList;
            safe->impl->mapList = area;
            safe->impl->paintMapList (g, *safe->impl->rack());
            safe->impl->mapList = saved;
        };
        popup->mouse = [safe] (const juce::MouseEvent& e, juce::Rectangle<int> area, int phase) {
            if (safe == nullptr || safe->impl->rack() == nullptr)
                return;
            auto& impl = *safe->impl;
            const auto saved = impl.mapList;
            impl.mapList = area;
            impl.dragStart = phase == 0 ? e.getPosition() : impl.dragStart;
            if (phase == 0)
            {
                impl.drag = Impl::Drag::none;
                impl.mapListDown (e, *impl.rack());
            }
            else if (phase == 1)
                impl.mouseDrag (e);
            else
                impl.mouseUp (e);
            impl.mapList = saved;
        };
        juce::CallOutBox::launchAsynchronously (std::move (popup), getScreenBounds().withWidth (40), nullptr);
    }
}

void RackView::paint (juce::Graphics& g) { impl->paint (g); }
void RackView::resized() { impl->place(); }
void RackView::mouseDown (const juce::MouseEvent& e) { impl->mouseDown (e); }
void RackView::mouseDrag (const juce::MouseEvent& e) { impl->mouseDrag (e); }
void RackView::mouseUp (const juce::MouseEvent& e) { impl->mouseUp (e); }
void RackView::mouseDoubleClick (const juce::MouseEvent& e) { impl->mouseDoubleClick (e); }
void RackView::mouseMove (const juce::MouseEvent&) {}

juce::Rectangle<int> RackView::padBounds (int key) const
{
    const auto* r = impl->rack();
    return r != nullptr && r->drum && ! impl->pads.isEmpty() ? impl->padRect (*r, key) : juce::Rectangle<int>();
}

int RackView::padKeyAt (juce::Point<int> p) const
{
    const auto* r = impl->rack();
    return r != nullptr && r->drum && ! impl->pads.isEmpty() ? impl->padAt (*r, p) : -1;
}

bool RackView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! impl->drum())
        return false;
    for (const auto& f : files)
        if (SampleLibrary::isSampleFile (juce::File (f)))
            return true;
    return false;
}

void RackView::filesDropped (const juce::StringArray& files, int x, int y) { impl->dropSamples (files, { x, y }); }
void RackView::timerCallback()
{
    impl->stepGlide();
    impl->refresh();
}

// ---------------------------------------------------------------------------------------------------------------

namespace RackWindows
{
namespace
{
RackLocator slotLocator (int track, int slot)
{
    return [track, slot] (Song& song) -> PatcherDoc* {
        if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
            return nullptr;
        auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
        return rack::isRack (fx) ? &fx.patcher : nullptr;
    };
}

RackLocator channelLocator (int channel)
{
    return [channel] (Song& song) -> PatcherDoc* {
        if (channel < 0 || channel >= (int) song.channels.size())
            return nullptr;
        auto& c = song.channels[(std::size_t) channel];
        return rack::isRack (c) ? &c.patcher : nullptr;
    };
}

juce::String channelKey (int channel) { return "rack-ch-" + juce::String (channel); }

void open (std::unique_ptr<RackView> view, const juce::String& title, const juce::String& key)
{
    const auto w = std::max (420, view->preferredWidth (210));
    view->setSize (w, 232);
    auto* raw = view.get();
    if (auto* window = openToolWindow (std::move (view), title, key))
    {
        window->setResizable (true, false);
        juce::Component::SafePointer<juce::DocumentWindow> safe (window);
        raw->onPreferredWidthChanged = [safe, raw] {
            if (safe != nullptr)
                safe->setSize (std::max (420, raw->preferredWidth (raw->getHeight())), safe->getHeight());
        };
    }
}
} // namespace

std::unique_ptr<RackView> makeSlotView (AudioEngine& engine, int track, int slot, bool ownHeader)
{
    AutomationTarget t;
    t.kind = AutomationTargetKind::effectParam;
    t.index = track;
    t.slot = slot;
    auto view = std::make_unique<RackView> (engine, slotLocator (track, slot), t, "rack-fx-" + juce::String (track) + "-" + juce::String (slot), ownHeader,
                                            [&engine, track, slot] (int n) { PatcherWindows::openNodeEditor (engine, false, track, slot, n); });
    auto slotOf = [&engine, track, slot] () -> const EffectSlot* {
        const auto& song = engine.getSong();
        return track >= 0 && track < (int) song.mixer.size() && slot >= 0 && slot < (int) song.mixer[(std::size_t) track].effects.size()
                   ? &song.mixer[(std::size_t) track].effects[(std::size_t) slot]
                   : nullptr;
    };
    view->setPowerSwitch ([slotOf] { const auto* s = slotOf(); return s != nullptr && s->enabled; },
                          [&engine, track, slot] {
                              engine.editSong ([track, slot] (Song& song) {
                                  if (track >= 0 && track < (int) song.mixer.size() && slot >= 0 && slot < (int) song.mixer[(std::size_t) track].effects.size())
                                      song.mixer[(std::size_t) track].effects[(std::size_t) slot].enabled = ! song.mixer[(std::size_t) track].effects[(std::size_t) slot].enabled;
                              });
                          });
    return view;
}

std::unique_ptr<RackView> makeChannelView (AudioEngine& engine, int channel, bool ownHeader)
{
    AutomationTarget t;
    t.kind = AutomationTargetKind::channelParam;
    t.index = channel;
    auto view = std::make_unique<RackView> (engine, channelLocator (channel), t, channelKey (channel), ownHeader,
                                            [&engine, channel] (int n) { PatcherWindows::openNodeEditor (engine, true, channel, 0, n); });
    view->setPowerSwitch ([&engine, channel] {
                              const auto& song = engine.getSong();
                              return channel >= 0 && channel < (int) song.channels.size() && ! song.channels[(std::size_t) channel].muted;
                          },
                          [&engine, channel] {
                              engine.editSong ([channel] (Song& song) {
                                  if (channel >= 0 && channel < (int) song.channels.size())
                                      song.channels[(std::size_t) channel].muted = ! song.channels[(std::size_t) channel].muted;
                              });
                          });
    return view;
}

void openSlot (AudioEngine& engine, int track, int slot)
{
    const auto& song = engine.getSong();
    const auto name = track >= 0 && track < (int) song.mixer.size() ? str (song.mixer[(std::size_t) track].name) : juce::String();
    open (makeSlotView (engine, track, slot, true), tr ("Audio Effect Rack") + (name.isNotEmpty() ? " - " + name : juce::String()),
          "rack-fx-" + juce::String (track) + "-" + juce::String (slot));
}

void openChannel (AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    const auto name = channel >= 0 && channel < (int) song.channels.size() ? str (song.channels[(std::size_t) channel].name) : juce::String();
    const auto drumRack = channel >= 0 && channel < (int) song.channels.size() && rack::isDrumRack (song.channels[(std::size_t) channel]);
    open (makeChannelView (engine, channel, true), name.isNotEmpty() ? name : drumRack ? tr ("Drum Rack") : tr ("Instrument Rack"), channelKey (channel));
}

bool isChannelOpen (int channel) { return findToolWindow (channelKey (channel)) != nullptr; }

void closeChannel (int channel)
{
    if (auto* w = findToolWindow (channelKey (channel)))
        delete w;
}
} // namespace RackWindows

namespace rackcards
{
namespace
{
class RackCard final : public DetailCard
{
public:
    explicit RackCard (std::unique_ptr<RackView> v) : view (std::move (v))
    {
        addAndMakeVisible (*view);
        accessory = view->makeTitleAccessory();
        view->onPreferredWidthChanged = [this] {
            if (onPreferredWidthChanged)
                onPreferredWidthChanged();
        };
    }
    // as wide as the rack wants, but not past the chain's right edge (so its title bar's Rand / Map stay in view): the
    // devices pane gives way first and scrolls
    int preferredWidth (int bodyHeight) const override
    {
        const auto wanted = view->preferredWidth (bodyHeight);
        return widthLimit > 0 ? std::max (std::min (wanted, widthLimit), view->minimumWidth (bodyHeight)) : wanted;
    }
    juce::Component* titleAccessory() override { return accessory.get(); }
    int titleAccessoryWidth() const override { return RackView::titleAccessoryWidth; }
    void resized() override { view->setBounds (getLocalBounds()); }

private:
    std::unique_ptr<RackView> view;
    std::unique_ptr<juce::Component> accessory;
};
} // namespace

void install()
{
    DetailView::effectCardFactory = [] (AudioEngine& engine, int insert, int slot, const EffectSlot& fx) -> std::unique_ptr<DetailCard> {
        if (! rack::isRack (fx))
            return nullptr;
        return std::make_unique<RackCard> (RackWindows::makeSlotView (engine, insert, slot, false));
    };
    DetailView::instrumentCardFactory = [] (AudioEngine& engine, int channel, const ChannelDef& c) -> std::unique_ptr<DetailCard> {
        if (! rack::isRack (c))
            return nullptr;
        return std::make_unique<RackCard> (RackWindows::makeChannelView (engine, channel, false));
    };
}
} // namespace rackcards
} // namespace lynx
