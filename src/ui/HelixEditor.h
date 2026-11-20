#pragma once

// The editor of Helix, LYNX's own wavetable synth (round 190ja, core/WavetableSynth.h). Laid out as Live's Wavetable
// device (Live 12.4 shots in /mnt/e/lynx-work/wavetable190ja/shots: sub column, oscillator with its table display,
// position and gain, filters with their response and routing, the modulation panel with Mod Sources / Matrix / MIDI /
// MPE, the global column), drawn by LYNX code. Two looks: Live's (the Clip-launch layout's Device View card) and LYNX's
// (the Pattern layout's channel window: LYNX's dark style, red accent). Every knob is automatable (its engine parameter);
// the tables and the matrix are the channel's state (undoable edits).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// the channel window's page (LYNX's look)
std::unique_ptr<juce::Component> makeHelixEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);
// the Device View's card body (Live's look); its natural size is helixDeviceWidth × helixDeviceHeight
std::unique_ptr<juce::Component> makeHelixDevice (AudioEngine& engine, int channel);
constexpr int helixDeviceWidth = 1000, helixDeviceHeight = 178;
namespace helixcards
{
// adds Helix's card to the Device View (keeps the factory that was there for the other instruments)
void install();
// pictures (round 190ja shots): every Helix editor under `root` shows this modulation page (0 Mod Sources, 1 Matrix,
// 2 MIDI, 3 MPE; 4 Macros in LYNX's look, round 190ml), source tab (0 Amp … 4 LFO 2) and oscillator (0 / 1); returns how
// many it found
int showView (juce::Component& root, int modTab, int source, int osc);
// Helix+ 2 (round 190ml): the Device View card's Helix+ panel shown or hidden (every Helix card under `root`, and the
// next ones: its title-bar well); returns the cards found
int showPlusInCard (juce::Component& root, bool shown);
// the table editor of oscillator `osc` (0…2) of a channel, as its window shows it (mode 0 Draw, 1 Harmonics; a frame)
std::unique_ptr<juce::Component> makeTableEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel, int osc, int mode = 0,
                                                  int frame = 0);
} // namespace helixcards
} // namespace lynx
