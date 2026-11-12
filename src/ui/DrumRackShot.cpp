#include "ui/DrumRackShot.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Effects.h"
#include "core/Rack.h"
#include "engine/AudioEngine.h"
#include "ui/MainComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/RackEditor.h"
#include "ui/ShotScale.h"

namespace lynx::drumshot
{
namespace
{
PatchNode samplerNode (const std::string& name)
{
    PatchNode n;
    n.generator = true;
    n.channel.instrument = InstrumentKind::sampler;
    n.channel.name = name;
    n.channel.sampler.release = 0.0f; // (Live's pad Simpler: one-shot)
    return n;
}

PatchNode effectNode (EffectKind kind)
{
    PatchNode n;
    n.effect = makeEffectSlot (kind);
    return n;
}

// the demo kit: ten pads from C1 up, the hats in choke group 1, two return chains and some sends
PatcherDoc demoKit()
{
    auto doc = rack::makeDrumRack();
    const char* names[] { "Kick", "Snare", "Clap", "Hat Closed", "Tom", "Hat Open", "Rim", "Perc", "Crash", "Ride" };
    for (int i = 0; i < 10; ++i)
        rack::addPad (doc, rack::drumFirstPad + i, names[i], samplerNode (names[i]));
    const auto reverb = rack::addReturn (doc, "A Reverb");
    const auto delay = rack::addReturn (doc, "B Delay");
    auto* r = rack::rackOf (doc);
    const auto chains = (int) r->chains.size();
    rack::addDevice (doc, chains + reverb, effectNode (EffectKind::reverb));
    rack::addDevice (doc, chains + delay, effectNode (EffectKind::delay));
    rack::setChoke (doc, 3, 1);
    rack::setChoke (doc, 5, 1);
    rack::setSend (doc, 1, 0, rack::volumeGain (0.6f)); // Snare → A
    rack::setSend (doc, 2, 0, rack::volumeGain (0.5f)); // Clap → A
    rack::setSend (doc, 7, 1, rack::volumeGain (0.55f)); // Perc → B
    rack::addDevice (doc, 1, effectNode (EffectKind::compressor)); // (the Snare's chain: Sampler → Compressor)
    r = rack::rackOf (doc);
    r->selectedChain = 1;
    rack::compile (doc);
    return doc;
}

void writePng (const juce::Image& image, const juce::String& path)
{
    const auto file = shot::file (juce::File (path));
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
}
} // namespace

void run (const juce::String& prefix)
{
    AudioEngine offline (false);
    MainComponent ui (offline, false);
    ui.setSize (1600, 900);
    juce::String checks;
    const auto check = [&checks] (const juce::String& what, bool ok) { checks << (ok ? "ok   " : "FAIL ") << what << "\n"; };

    // channel 0: the demo kit; channel 1: a new empty Drum Rack; a pattern playing the pads
    offline.editSong ([] (Song& s) {
        if (s.channels.size() < 2)
            s.channels.resize (2);
        auto& kit = s.channels[0];
        kit.name = "Drums";
        kit.instrument = InstrumentKind::patcher;
        kit.patcher = demoKit();
        auto& empty = s.channels[1];
        empty.name = "Drum Rack";
        empty.instrument = InstrumentKind::patcher;
        empty.patcher = rack::makeDrumRack();
        if (s.patterns.empty())
            s.patterns.emplace_back();
        auto& p = s.patterns[0];
        p.notes.clear();
        for (int i = 0; i < 16; ++i)
        {
            Note n;
            n.channel = 0;
            n.key = rack::drumFirstPad + (i % 4 == 0 ? 0 : i % 4 == 2 ? 1 : 3);
            n.start = (std::int64_t) i * ppq / 4;
            n.length = ppq / 4;
            p.notes.push_back (n);
        }
        p.sortNotes();
        s.currentPattern = 0;
    });
    offline.dispatchPendingMessages();

    const auto snap = [&] (int channel, const char* name, std::function<void (RackDoc&)> view) {
        offline.editSong ([channel, view] (Song& s) {
            auto* r = rack::rackOf (s.channels[(std::size_t) channel].patcher);
            view (*r);
            rack::compile (s.channels[(std::size_t) channel].patcher);
        }, false);
        offline.dispatchPendingMessages();
        auto v = RackWindows::makeChannelView (offline, channel, true);
        v->setSize (std::max (420, v->preferredWidth (210)), 232);
        writePng (shot::snapshot (*v), prefix + "-" + name + ".png");
        return v;
    };

    // 1. the pads with the macros (Live opens a Drum Rack this way: ArePadsVisible true, the chain list hidden)
    auto pads = snap (0, "drumrack-pads", [] (RackDoc& r) {
        r.showMacros = true, r.showPads = true, r.showChains = false, r.showDevices = false, r.showIO = false, r.showSends = false, r.showReturns = false;
    });
    const auto c1 = pads->padBounds (rack::drumFirstPad);
    const auto c1Below = pads->padBounds (rack::drumFirstPad + 4);
    check ("C1 is the bottom-left pad of the default view (PadScrollPosition 19)",
           ! c1.isEmpty() && c1.getX() <= pads->padBounds (rack::drumFirstPad + 1).getX() && ! c1Below.isEmpty() && c1Below.getY() < c1.getY()
               && pads->padBounds (rack::drumFirstPad - 1).isEmpty());
    check ("the pad under C1's centre is C1", pads->padKeyAt (c1.getCentre()) == rack::drumFirstPad);

    // a drag of Kick (C1) onto C#1 (Snare): the pads trade places (Live)
    {
        const auto from = c1.getCentre(), to = pads->padBounds (rack::drumFirstPad + 1).getCentre();
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        auto event = [&] (juce::Point<int> p, bool dragged) {
            return juce::MouseEvent (source, p.toFloat(), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, pads.get(), pads.get(),
                                     now, from.toFloat(), now, 1, dragged);
        };
        pads->mouseDown (event (from, false));
        pads->mouseDrag (event (to, true));
        pads->mouseUp (event (to, true));
        offline.dispatchPendingMessages();
        const auto* r = rack::rackOf (offline.getSong().channels[0].patcher);
        check ("a pad dragged onto another swaps them (Kick on C#1, Snare on C1)",
               r != nullptr && rack::padName (*r, rack::drumFirstPad + 1) == "Kick" && rack::padName (*r, rack::drumFirstPad) == "Snare");
        offline.editSong ([] (Song& s) { rack::swapPads (s.channels[0].patcher, rack::drumFirstPad, rack::drumFirstPad + 1); }, false);
        offline.dispatchPendingMessages();
    }
    pads.reset();

    // the overview scrolled to the top rows: C1 no longer shown
    {
        auto top = snap (0, "drumrack-pads-scrolled", [] (RackDoc& r) { r.padScroll = 0; });
        check ("scrolled to row 0: C1 hidden, G8 (127) shown", top->padBounds (rack::drumFirstPad).isEmpty() && ! top->padBounds (127).isEmpty());
    }

    // 2. the chain list with I-O, sends and return chains, the devices of the selected pad (Snare)
    {
        auto full = snap (0, "drumrack-chains-io-sends", [] (RackDoc& r) {
            r.padScroll = rack::drumDefaultScroll;
            r.showMacros = false, r.showPads = true, r.showChains = true, r.showDevices = true, r.showIO = true, r.showSends = true, r.showReturns = true;
            r.selectedChain = 1;
        });
        check ("the chain list view is wider than the pads alone", full->getWidth() > 900);
    }

    // 3. a new empty Drum Rack
    snap (1, "drumrack-empty", [] (RackDoc& r) { r.showChains = true, r.showDevices = true; });

    // 4. the Piano roll's key column: the pad names on their keys
    offline.setSelectedChannel (0);
    ui.setVisible (true);
    ui.selectTabForSnapshot (1);
    offline.dispatchPendingMessages();
    auto& roll = ui.getPianoRollForSnapshot();
    roll.scrollToNotes();
    offline.dispatchPendingMessages();
    if (roll.getWidth() > 0 && roll.getHeight() > 0)
        writePng (shot::snapshot (roll, roll.getLocalBounds().withWidth (std::min (roll.getWidth(), 520))), prefix + "-pianoroll-padnames.png");
    check ("the Piano roll is shown", roll.isShowing() || roll.getWidth() > 0);
    {
        const auto names = rack::padNames (*rack::rackOf (offline.getSong().channels[0].patcher));
        check ("pad names for the key column: 10, C1 = Kick", names.size() == 10 && names.count (rack::drumFirstPad) == 1 && names.at (rack::drumFirstPad) == "Kick");
    }
    juce::File (prefix + "-checks.txt").replaceWithText (checks);
}
} // namespace lynx::drumshot
