#include "ui/RackEditor.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "core/Patcher.h"
#include "core/Rack.h"
#include "core/Vfx.h"
#include "engine/PluginHost.h"
#include "ui/ControllerKnob.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/PatcherEditor.h"
#include "ui/PluginPicker.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

// Layout from Live 12.4's rack (REA_rodada183c §8 [SHOT] live/ref-live-rack-deviceview.png), drawn in LYNX's style:
// view column | variations | macros (2 rows) | chain list | zone editor | the selected chain's devices.

namespace lynx
{
namespace
{
namespace col = theme::colour;

constexpr int viewColumn = 22, variationsWidth = 150, macroWidth = 64, chainListWidth = 236, zoneWidth = 268, deviceWidth = 112, mapListWidth = 330;
constexpr int rowHeight = 18, headerStrip = 22;

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// a chain volume as Live shows it ("-6.0 dB", "-inf dB")
juce::String volumeText (float gain)
{
    if (gain <= 0.0f)
        return "-inf dB";
    return juce::String (20.0f * std::log10 (gain), 1) + " dB";
}

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
    int lastWidth = 0;

    // the sections laid out by place()
    juce::Rectangle<int> header, column, variations, macros, chains, zones, devices, mapList;
    // a drag in progress
    enum class Drag { none, volume, pan, selector, zoneMove, zoneLo, zoneHi, zoneFadeLo, zoneFadeHi, morph, mapMin, mapMax };
    Drag drag = Drag::none;
    int dragChain = -1, dragMacro = -1, dragMapping = -1;
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
                    return str (formatEffectParam (node.effect.kind, x.param, v, engine.getSong().bpm));
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
            return node.generator ? tr ("Instrument Rack") : tr ("Audio Effect Rack");
        return str (patcher::nodeName (node));
    }

    juce::String chainOfNode (int n) const
    {
        if (const auto* r = rack())
            for (const auto& c : r->chains)
                if (std::find (c.nodes.begin(), c.nodes.end(), n) != c.nodes.end())
                    return str (c.name);
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

    int width (int /*height*/) const
    {
        const auto* r = rack();
        if (r == nullptr)
            return 200;
        int w = viewColumn;
        if (r->showVariations)
            w += variationsWidth;
        if (r->showMacros)
            w += macroColumns() * macroWidth + 8;
        if (r->showChains)
            w += chainListWidth + (r->zoneEditor >= 0 ? zoneWidth : 0);
        if (r->showDevices)
            w += (int) (std::max<std::size_t> (1, rack::devices (*r, r->selectedChain).size()) + 1) * (deviceWidth + 4) + 8;
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
        chains = r != nullptr && r->showChains ? area.removeFromLeft (chainListWidth) : juce::Rectangle<int>();
        zones = r != nullptr && r->showChains && r->zoneEditor >= 0 ? area.removeFromLeft (zoneWidth) : juce::Rectangle<int>();
        devices = r != nullptr && r->showDevices ? area : juce::Rectangle<int>();

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
            k.setBounds (cell.reduced (2, 0));
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

    juce::Rectangle<int> columnButton (int k) const { return { column.getX() + 2, column.getY() + 4 + k * 20, viewColumn - 4, 18 }; }
    juce::Rectangle<int> chainRow (int c) const { return { chains.getX(), chains.getY() + headerStrip + c * rowHeight, chains.getWidth(), rowHeight }; }
    juce::Rectangle<int> zoneRow (int c) const { return { zones.getX() + 6, zones.getY() + headerStrip + c * rowHeight, zones.getWidth() - 12, rowHeight }; }
    juce::Rectangle<int> variationRow (int v) const { return { variations.getX() + 4, variations.getY() + 28 + v * rowHeight, variations.getWidth() - 8, rowHeight }; }
    juce::Rectangle<int> deviceCard (int i) const { return { devices.getX() + 4 + i * (deviceWidth + 4), devices.getY() + 4, deviceWidth, devices.getHeight() - 8 }; }
    juce::Rectangle<int> morphSlider() const { return { variations.getX() + 8, variations.getBottom() - 22, variations.getWidth() - 16, 14 }; }
    juce::Rectangle<int> mapRow (int i) const { return { mapList.getX() + 4, mapList.getY() + 24 + i * rowHeight, mapList.getWidth() - 8, rowHeight }; }

    // the zone the editor shows for chain c
    RackZone zoneOf (const RackChain& c) const
    {
        const auto* r = rack();
        const auto which = r != nullptr ? r->zoneEditor : 2;
        return which == 0 ? c.key : which == 1 ? c.velocity : c.select;
    }
    int zoneMin() const { const auto* r = rack(); return r != nullptr && r->zoneEditor == 1 ? 1 : 0; }
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
        (r->zoneEditor == 0 ? chain.key : r->zoneEditor == 1 ? chain.velocity : chain.select) = z;
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
            g.setColour (col::textPrimary);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (r->kind == RackDoc::Kind::instrument ? tr ("Instrument Rack") : r->kind == RackDoc::Kind::midiEffect ? tr ("MIDI Effect Rack") : tr ("Audio Effect Rack"),
                        header.reduced (8, 0), juce::Justification::centredLeft);
            g.setFont (theme::font (theme::type::caption));
        }

        // view column: macros, chains, devices, variations, + / − (Live's selectors [SHOT])
        g.setColour (col::bg3);
        g.fillRect (column);
        const bool on[] { r->showMacros, r->showChains, r->showDevices, r->showVariations, false, false };
        const char* labels[] { "M", "C", "D", "V", "+", "-" };
        for (int k = 0; k < 6; ++k)
        {
            const auto b = columnButton (k).toFloat();
            g.setColour (on[k] ? col::accent : col::bg4);
            g.fillRoundedRectangle (b, theme::radius::sm);
            g.setColour (on[k] ? col::onAccent : col::textSecondary);
            g.drawText (labels[k], b, juce::Justification::centred);
        }

        if (! variations.isEmpty())
            paintVariations (g, *r);
        if (! macros.isEmpty())
            paintMacros (g, *r);
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
        // the zone editor's buttons (Live: above the chain list [SHOT])
        auto strip = chains.withHeight (headerStrip).reduced (4, 3);
        const auto instrumentLike = r.kind != RackDoc::Kind::audioEffect;
        const juce::String names[] { tr ("Key"), tr ("Vel"), tr ("Chain"), tr ("Hide") };
        for (int k = 3; k >= 0; --k)
        {
            if (! instrumentLike && (k == 0 || k == 1))
                continue;
            auto b = strip.removeFromRight (40).reduced (1, 0).toFloat();
            const auto lit = k < 3 ? r.zoneEditor == k : r.zoneEditor < 0;
            g.setColour (lit ? col::accent : col::bg4);
            g.fillRoundedRectangle (b, theme::radius::sm);
            g.setColour (lit ? col::onAccent : col::textSecondary);
            g.drawText (names[k], b, juce::Justification::centred);
        }
        for (int c = 0; c < (int) r.chains.size(); ++c)
        {
            const auto& chain = r.chains[(std::size_t) c];
            auto row = chainRow (c);
            if (row.getBottom() > chains.getBottom())
                break;
            if (c == r.selectedChain)
            {
                g.setColour (col::accentMuted);
                g.fillRect (row);
            }
            auto cells = row.reduced (2, 1);
            auto nameCell = cells.removeFromLeft (92).toFloat();
            g.setColour (col::bg4);
            g.fillRoundedRectangle (nameCell, theme::radius::sm);
            g.setColour (chain.active ? col::textPrimary : col::textDisabled);
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
        }
        const auto hintTop = headerStrip + (int) r.chains.size() * rowHeight + 6;
        if (hintTop < chains.getHeight() - 14)
        {
            g.setColour (col::textDisabled);
            g.drawText (r.kind == RackDoc::Kind::instrument ? tr ("Drop an instrument or effect here") : tr ("Drop effects here"),
                        chains.withTrimmedTop (hintTop), juce::Justification::centredTop);
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
        for (int c = 0; c < (int) r.chains.size(); ++c)
        {
            const auto row = zoneRow (c);
            if (row.getBottom() > zones.getBottom())
                break;
            const auto z = zoneOf (r.chains[(std::size_t) c]);
            const auto x0 = zoneX (row, (float) z.lo), x1 = zoneX (row, (float) z.hi + 1.0f);
            const auto f0 = zoneX (row, (float) z.fadeLo), f1 = zoneX (row, (float) z.fadeHi + 1.0f);
            const auto top = (float) row.getY() + 3.0f, mid = (float) row.getY() + 7.0f, bottom = (float) row.getBottom() - 2.0f;
            g.setColour (col::info.withAlpha (0.85f));
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

    void paintDevices (juce::Graphics& g, const RackDoc& r)
    {
        const auto& list = rack::devices (r, r.selectedChain);
        // the chain's bracket (Live: the rack's devices sit between brackets)
        g.setColour (col::borderStrong);
        g.drawRect (devices.reduced (1));
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
        // the "+" card: add a device to the selected chain
        const auto plus = deviceCard (i).toFloat();
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

        for (int k = 0; k < 6; ++k)
            if (columnButton (k).contains (p))
            {
                edit ([k] (PatcherDoc& d) {
                    auto* x = rack::rackOf (d);
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
        auto strip = chains.withHeight (headerStrip).reduced (4, 3);
        const auto instrumentLike = r.kind != RackDoc::Kind::audioEffect;
        for (int k = 3; k >= 0; --k)
        {
            if (! instrumentLike && (k == 0 || k == 1))
                continue;
            if (strip.removeFromRight (40).contains (p))
                return edit ([k] (PatcherDoc& d) { rack::rackOf (d)->zoneEditor = k < 3 ? k : -1; }, false);
        }
        for (int c = 0; c < (int) r.chains.size(); ++c)
        {
            const auto row = chainRow (c);
            if (! row.contains (p))
                continue;
            if (e.mods.isPopupMenu())
                return showChainMenu (c);
            auto cells = row.reduced (2, 1);
            const auto nameCell = cells.removeFromLeft (92);
            const auto volCell = r.kind != RackDoc::Kind::midiEffect ? cells.removeFromLeft (52) : juce::Rectangle<int>();
            const auto panCell = r.kind != RackDoc::Kind::midiEffect ? cells.removeFromLeft (30) : juce::Rectangle<int>();
            const auto ledCell = cells.removeFromLeft (18);
            const auto soloCell = cells.removeFromLeft (18);
            (void) nameCell;
            edit ([c] (PatcherDoc& d) { rack::rackOf (d)->selectedChain = c; }, false);
            if (ledCell.contains (p))
                return edit ([c, on = ! r.chains[(std::size_t) c].active] (PatcherDoc& d) { rack::setActive (d, c, on); });
            if (soloCell.contains (p))
                return edit ([c, on = ! r.chains[(std::size_t) c].solo] (PatcherDoc& d) { rack::setSolo (d, c, on); });
            if (volCell.contains (p))
            {
                drag = Drag::volume;
                dragChain = c;
                dragStartValue = rack::volumeValue (r.chains[(std::size_t) c].volume);
                if (ui::isResetClick (e))
                    edit ([c] (PatcherDoc& d) { rack::setChainVolume (d, c, 1.0f); });
                return;
            }
            if (panCell.contains (p))
            {
                drag = Drag::pan;
                dragChain = c;
                dragStartValue = r.chains[(std::size_t) c].pan;
                if (ui::isResetClick (e))
                    edit ([c] (PatcherDoc& d) { rack::setChainPan (d, c, 0.0f); });
                return;
            }
            if (owner.isShowing() && r.showDevices == false)
                edit ([] (PatcherDoc& d) { rack::rackOf (d)->showDevices = true; }, false);
            return;
        }
        if (e.mods.isPopupMenu())
            showChainMenu (-1);
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
        const auto& list = rack::devices (r, r.selectedChain);
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
            showAddMenu (r.selectedChain);
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
            case Drag::none: break;
        }
    }

    void mouseUp (const juce::MouseEvent&)
    {
        if (drag != Drag::none)
            edit ([] (PatcherDoc&) {}); // (one undo step for the whole drag)
        drag = Drag::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto* r = rack();
        if (r == nullptr)
            return;
        for (int c = 0; c < (int) r->chains.size(); ++c)
            if (chainRow (c).withWidth (96).contains (e.getPosition()))
                return renameChain (c);
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

    void renameChain (int c)
    {
        const auto* r = rack();
        askName (tr ("Rename chain"), str (r->chains[(std::size_t) c].name), [this, c] (juce::String n) {
            if (n.isNotEmpty())
                edit ([c, n] (PatcherDoc& d) { rack::rackOf (d)->chains[(std::size_t) c].name = n.toStdString(); });
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
        menu.addItem (tr ("Add chain"), [this] { edit ([] (PatcherDoc& d) { rack::addChain (d); }); });
        if (c >= 0)
        {
            menu.addItem (tr ("Rename"), [this, c] { renameChain (c); });
            menu.addItem (tr ("Duplicate"), [this, c] { edit ([c] (PatcherDoc& d) { rack::duplicateChain (d, c); }); });
            menu.addItem (tr ("Delete"), rack()->chains.size() > 1, false, [this, c] { edit ([c] (PatcherDoc& d) { rack::removeChain (d, c); }); });
            menu.addSeparator();
            menu.addItem (tr ("Move up"), c > 0, false, [this, c] { edit ([c] (PatcherDoc& d) { rack::moveChain (d, c, c - 1); }); });
            menu.addItem (tr ("Move down"), c + 1 < (int) rack()->chains.size(), false, [this, c] { edit ([c] (PatcherDoc& d) { rack::moveChain (d, c, c + 1); }); });
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
        if (r->kind != RackDoc::Kind::audioEffect)
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
        if (r->kind == RackDoc::Kind::instrument)
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

void RackView::setMapMode (bool on)
{
    mapping = on;
    impl->map.setToggleState (on, juce::dontSendNotification);
    resized();
    repaint();
}

void RackView::paint (juce::Graphics& g) { impl->paint (g); }
void RackView::resized() { impl->place(); }
void RackView::mouseDown (const juce::MouseEvent& e) { impl->mouseDown (e); }
void RackView::mouseDrag (const juce::MouseEvent& e) { impl->mouseDrag (e); }
void RackView::mouseUp (const juce::MouseEvent& e) { impl->mouseUp (e); }
void RackView::mouseDoubleClick (const juce::MouseEvent& e) { impl->mouseDoubleClick (e); }
void RackView::mouseMove (const juce::MouseEvent&) {}
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
    return std::make_unique<RackView> (engine, slotLocator (track, slot), t, "rack-fx-" + juce::String (track) + "-" + juce::String (slot), ownHeader,
                                       [&engine, track, slot] (int n) { PatcherWindows::openNodeEditor (engine, false, track, slot, n); });
}

std::unique_ptr<RackView> makeChannelView (AudioEngine& engine, int channel, bool ownHeader)
{
    AutomationTarget t;
    t.kind = AutomationTargetKind::channelParam;
    t.index = channel;
    return std::make_unique<RackView> (engine, channelLocator (channel), t, channelKey (channel), ownHeader,
                                       [&engine, channel] (int n) { PatcherWindows::openNodeEditor (engine, true, channel, 0, n); });
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
    open (makeChannelView (engine, channel, true), name.isNotEmpty() ? name : tr ("Instrument Rack"), channelKey (channel));
}

bool isChannelOpen (int channel) { return findToolWindow (channelKey (channel)) != nullptr; }

void closeChannel (int channel)
{
    if (auto* w = findToolWindow (channelKey (channel)))
        delete w;
}
} // namespace RackWindows
} // namespace lynx
