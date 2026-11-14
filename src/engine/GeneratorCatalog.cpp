#include "engine/GeneratorCatalog.h"
#include "engine/AudioEngine.h"
#include "core/ControlSurface.h"
#include "core/Rack.h"
#include "core/FlSynths.h"
#include "core/FruityDance.h"
#include "core/MidiControls.h"
#include "core/NativeEngines.h"
#include "core/SoundFont.h"
#include "core/VideoPlayer.h"
#include "core/VoltageController.h"
#include "core/Vfx.h"
#include <algorithm>

namespace lynx::generators
{
std::vector<Entry> entries (AudioEngine& engine, bool withUnavailable)
{
    std::vector<Entry> out;
    auto add = [&out] (std::string english, juce::String label, juce::String detail, int folder, int section, InstrumentKind kind,
                       std::function<std::optional<ChannelDef>()> make) {
        out.push_back ({ "lynx:gen:" + english, std::move (label), std::move (detail), folder, std::move (make), section, kind });
    };
    // a built-in whose channel is `kind` with the English name; `setup` fills its state
    auto builtIn = [&add] (const char* english, juce::String label, int section, InstrumentKind kind, std::function<void (ChannelDef&)> setup = nullptr) {
        add (english, std::move (label), juce::String ("built-in"), 0, section, kind, [kind, name = std::string (english), setup] {
            ChannelDef c;
            c.name = name;
            c.instrument = kind;
            if (setup)
                setup (c);
            return std::optional<ChannelDef> (c);
        });
    };

    // the first block, FL's menu order (Kick … Synth)
    builtIn ("Kick", juce::String ("Kick"), 0, InstrumentKind::kick);
    builtIn ("Snare", juce::String ("Snare"), 0, InstrumentKind::snare);
    builtIn ("Hat", juce::String ("Hat"), 0, InstrumentKind::hat);
    builtIn ("Clap", juce::String ("Clap"), 0, InstrumentKind::clap);
    builtIn ("Synth", juce::String ("Synth"), 0, InstrumentKind::synth);

    // built-in controllers and tools (rounds 133–165): no sound of their own
    builtIn ("Envelope Controller", juce::String ("Envelope Controller"), 1, InstrumentKind::controller,
             [] (ChannelDef& c) { c.controllerState = writeEnvelopeController (defaultEnvelopeController()); });
    builtIn ("Keyboard Controller", juce::String ("Keyboard Controller"), 1, InstrumentKind::controller,
             [] (ChannelDef& c) { c.controllerState = defaultKeyboardController(); });
    add ("MIDI Out", juce::String ("MIDI Out"), juce::String ("built-in"), 0, 1, InstrumentKind::midiOut, [&engine] {
        ChannelDef c;
        c.name = "MIDI Out";
        c.instrument = InstrumentKind::midiOut;
        // FL gives each new MIDI Out the next MIDI channel, skipping the drum channel 10 (REA_rodada16 §1.1)
        int count = 0;
        for (const auto& ch : engine.getSong().channels)
            count += ch.instrument == InstrumentKind::midiOut;
        midictl::MidiOutSettings settings;
        settings.channel = count % 15 >= 9 ? count % 15 + 1 : count % 15;
        midictl::writeMidiOut (c.pluginState, settings);
        return std::optional<ChannelDef> (c);
    });
    builtIn ("Layer", juce::String ("Layer"), 1, InstrumentKind::layer);
    builtIn ("Dashboard", "Dashboard", 1, InstrumentKind::dashboard, [] (ChannelDef& c) { c.patcher.surface = surface::starterControls(); });
    builtIn ("Voltage Controller", "Voltage Controller", 1, InstrumentKind::voltage,
             [] (ChannelDef& c) { c.pluginState = voltage::write (voltage::Settings {}); });
    builtIn ("Fruity Dance", "Fruity Dance", 1, InstrumentKind::fruitydance, [] (ChannelDef& c) { c.pluginState = dance::toState (dance::Settings {}); });
    builtIn ("Fruity Video Player", "Fruity Video Player", 1, InstrumentKind::videoplayer,
             [] (ChannelDef& c) { c.pluginState = video::write (video::Settings {}); });
    builtIn ("Patcher", juce::String ("Patcher"), 2, InstrumentKind::patcher);
    builtIn ("Instrument Rack", juce::String ("Instrument Rack"), 2, InstrumentKind::patcher,
             [] (ChannelDef& c) { c.patcher = rack::makeRack (RackDoc::Kind::instrument); });
    builtIn ("Drum Rack", juce::String ("Drum Rack"), 2, InstrumentKind::patcher, [] (ChannelDef& c) { c.patcher = rack::makeDrumRack(); });
    builtIn ("MIDI Effect Rack", juce::String ("MIDI Effect Rack"), 2, InstrumentKind::patcher,
             [] (ChannelDef& c) { c.patcher = rack::makeRack (RackDoc::Kind::midiEffect); });
    add ("LYNX Soundfont", "LYNX Soundfont", juce::String ("built-in"), 0, 2, InstrumentKind::soundfont, [] { return std::optional<ChannelDef> (sf::makeChannel()); });

    // FL's own instrument engines (round 142)
    const auto& fl = flStudioAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto available = natives::available (e, fl);
        if (available || withUnavailable)
        {
            add (e.name, juce::String::fromUTF8 (e.name), juce::String ("built-in FL engine"), 1, 1, e.kind, [kind = e.kind] { return natives::makeChannel (kind, flStudioAccess()); });
            out.back().available = available;
            if (! available) out.back().unavailableReason = "Requires a usable FL Studio installation and preset";
        }
    }

    // FL's VFX note processors (round 164b): channels with no sound, they play other channels / Patcher nodes
    for (const auto kind : vfx::allKinds)
        add (vfx::flName (kind), vfx::flName (kind), juce::String ("built-in"), 2, 1, InstrumentKind::vfx, [kind] {
            ChannelDef c;
            vfx::makeChannel (c, kind);
            return std::optional<ChannelDef> (c);
        });

    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (const auto& t : types)
        if (t.isInstrument)
        {
            const auto id = t.createIdentifierString().toStdString();
            out.push_back ({ id, t.name, t.manufacturerName + " · " + t.pluginFormatName, 3, [id, name = t.name.toStdString()] {
                                ChannelDef c;
                                c.name = name;
                                c.instrument = InstrumentKind::plugin;
                                c.plugin.id = id;
                                c.plugin.name = name;
                                return std::optional<ChannelDef> (c);
                            }, 1, InstrumentKind::plugin });
            out.back().format = t.pluginFormatName;
            out.back().manufacturer = t.manufacturerName;
        }
    return out;
}

}
