#include "ui/DrumRackShot.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Effects.h"
#include "core/Rack.h"
#include "engine/AudioEngine.h"
#include "ui/EditorTools.h"
#include "ui/I18n.h"
#include "ui/MainComponent.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
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
    const auto reverb = rack::addReturn (doc, "Mix Bus"); // (shown "a Mix Bus", "b Reverb" as Live's)
    const auto delay = rack::addReturn (doc, "Reverb");
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
    rack::setAudioTo (doc, 2, 0); // (Clap into a Mix Bus: "<rack> | a Mix Bus")
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

// a popup menu drawn as LYNX draws it (the look and feel's own item painting), for the Live | LYNX menu pairs
juce::Image menuImage (const juce::PopupMenu& menu, int width)
{
    auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
    struct Row { const juce::PopupMenu::Item* item; int height; };
    std::vector<Row> rows;
    int total = 8;
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        const auto& item = it.getItem();
        int w = 0, h = 0;
        lf.getIdealPopupMenuItemSize (item.text, item.isSeparator, -1, w, h);
        rows.push_back ({ &item, h });
        total += h;
    }
    const auto scale = 2.0f;
    juce::Image image (juce::Image::ARGB, juce::roundToInt ((float) width * scale), juce::roundToInt ((float) total * scale), true);
    juce::Graphics g (image);
    g.addTransform (juce::AffineTransform::scale (scale));
    lf.drawPopupMenuBackground (g, width, total);
    int y = 4;
    for (const auto& row : rows)
    {
        const auto& item = *row.item;
        const juce::Rectangle<int> area (0, y, width, row.height);
        if (item.isSectionHeader)
            lf.drawPopupMenuSectionHeader (g, area, item.text);
        else
            lf.drawPopupMenuItem (g, area, item.isSeparator, item.isEnabled, false, item.isTicked, item.subMenu != nullptr, item.text, item.shortcutKeyDescription,
                                  item.image.get(), item.colour != juce::Colour() ? &item.colour : nullptr);
        y += row.height;
    }
    return image;
}

// the loop the Slice to New MIDI Track pictures slice: 2 bars at 120 BPM, a hit every half beat
juce::File writeLoop (const juce::File& folder)
{
    folder.createDirectory();
    const auto file = folder.getChildFile ("drums.wav");
    juce::AudioBuffer<float> audio (1, 4 * 44100);
    audio.clear();
    juce::Random random (190);
    for (int hit = 0; hit < 8; ++hit)
        for (int i = 0; i < 44100 * 2 / 5; ++i)
        {
            const auto t = (double) i / 44100.0;
            const auto v = hit % 4 == 0 ? std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 100.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 8.0)
                                        : (random.nextFloat() * 2.0f - 1.0f) * std::exp (-t * (hit % 2 == 0 ? 15.0 : 50.0));
            audio.setSample (0, hit * 22050 + i, (float) (0.8 * v));
        }
    file.deleteFile();
    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (juce::WavAudioFormat().createWriterFor (out.get(), 44100.0, 1, 16, {}, 0)))
        {
            out.release();
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }
    return file;
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
        if (s.channels.size() < 3)
            s.channels.resize (3);
        auto& kit = s.channels[0];
        kit.name = "Drums";
        kit.instrument = InstrumentKind::patcher;
        kit.patcher = demoKit();
        auto& empty = s.channels[1];
        empty.name = "Drum Rack";
        empty.instrument = InstrumentKind::patcher;
        empty.patcher = rack::makeDrumRack();
        // channel 2: a four-pad kit with two return chains, as Live's chain list zoom (190ei)
        auto& small = s.channels[2];
        small.name = "909 Kit";
        small.instrument = InstrumentKind::patcher;
        small.patcher = rack::makeDrumRack();
        for (const auto& [key, name] : { std::pair<int, const char*> { 36, "Bass Drum" }, { 37, "Rim Shot" }, { 43, "Snare Drum" }, { 39, "Hand Clap" } })
            rack::addPad (small.patcher, key, name, samplerNode (name));
        rack::addReturn (small.patcher, "Mix Bus");
        rack::addReturn (small.patcher, "Reverb");
        rack::rackOf (small.patcher)->name = "909 Core Kit";
        rack::setAudioTo (small.patcher, 3, 1);
        rack::setReceive (small.patcher, 1, -1);
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

    // 2. the chain list with I-O, sends and return chains in Live's column order (190ei shots/live-drumrack-chainlist-
    // zoom.png), the devices of the selected pad (Snare)
    {
        auto full = snap (0, "drumrack-chains-io-sends", [] (RackDoc& r) {
            r.padScroll = rack::drumDefaultScroll;
            r.showMacros = false, r.showPads = true, r.showChains = true, r.showDevices = true, r.showIO = true, r.showSends = true, r.showReturns = true;
            r.selectedChain = 1;
        });
        check ("the chain list view is wider than the pads alone", full->getWidth() > 900);
        // Live's whole view of a kit (190ei shots/live-drumrack-chains-io-sends-returns.png): macros, pads, chain list
        snap (0, "drumrack-kit-full", [] (RackDoc& r) {
            r.showMacros = true, r.showPads = true, r.showChains = true, r.showDevices = false, r.showIO = true, r.showSends = true, r.showReturns = true;
            r.selectedChain = 0;
        });
        snap (2, "drumrack-chainlist-returns", [] (RackDoc& r) {
            r.showMacros = false, r.showPads = false, r.showChains = true, r.showDevices = false, r.showIO = true, r.showSends = true, r.showReturns = true;
            r.selectedChain = 0;
        });
        // the pad menu (Live's items, then LYNX's) and the Audio To menu
        auto menuView = RackWindows::makeChannelView (offline, 0, true);
        const auto padMenu = menuView->padMenuForSnapshot (rack::drumFirstPad);
        writePng (menuImage (padMenu, 300), prefix + "-drumrack-pad-menu.png");
        juce::StringArray items;
        for (juce::PopupMenu::MenuItemIterator it (padMenu); it.next();)
            items.add (it.getItem().isSeparator ? juce::String ("---") : it.getItem().text + (it.getItem().shortcutKeyDescription.isNotEmpty() ? "  [" + it.getItem().shortcutKeyDescription + "]" : juce::String())
                                                                                + (it.getItem().isEnabled ? "" : "  (greyed)"));
        juce::File (prefix + "-drumrack-pad-menu.txt").replaceWithText (items.joinIntoString ("\n"));
        check ("pad menu in Live's order: Rename, Delete, Save as Default Pad, Next Similar … then Duplicate, Choke",
               items.size() == 12 && items[0].startsWith ("Rename") && items[1].startsWith ("Delete") && items[3].startsWith ("Save as Default Pad")
                   && items[5].startsWith ("Next Similar") && items[8].startsWith ("Return to Reference") && items[10].startsWith ("Duplicate"));
        const auto audioToMenu = menuView->ioMenuForSnapshot (2, 3); // (kept: the iterator below refers to it)
        writePng (menuImage (audioToMenu, 240), prefix + "-drumrack-audioto-menu.png");
        juce::StringArray audioTo;
        for (juce::PopupMenu::MenuItemIterator it (audioToMenu); it.next();)
            audioTo.add (it.getItem().text);
        check ("Audio To: Rack Output, Drum Rack | a Mix Bus, Drum Rack | b Reverb",
               audioTo == juce::StringArray { "Rack Output", "Drum Rack | a Mix Bus", "Drum Rack | b Reverb" });
    }

    // 3. a new empty Drum Rack, as it opens (the pads and the devices pane only, C1 selected)
    {
        auto empty = RackWindows::makeChannelView (offline, 1, true);
        empty->setSize (std::max (420, empty->preferredWidth (210)), 232);
        writePng (shot::snapshot (*empty), prefix + "-drumrack-empty.png");
        const auto* r = rack::rackOf (offline.getSong().channels[1].patcher);
        check ("a new Drum Rack: pads + devices only", r != nullptr && r->showPads && r->showDevices && ! r->showMacros && ! r->showChains);
    }

    // 4. the Piano roll's key column: the pad names on their keys (Pattern layout), the pad rows (Clip-launch layout)
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
    EditorGlobals::get().clipLaunchWorkflow = true;
    EditorGlobals::get().sendSynchronousChangeMessage();
    offline.dispatchPendingMessages();
    roll.scrollToNotes();
    offline.dispatchPendingMessages();
    if (roll.getWidth() > 0 && roll.getHeight() > 0)
        writePng (shot::snapshot (roll, roll.getLocalBounds().withWidth (std::min (roll.getWidth(), 520))), prefix + "-pianoroll-padrows.png");
    EditorGlobals::get().clipLaunchWorkflow = false;
    EditorGlobals::get().sendSynchronousChangeMessage();

    // 5. Slice to New MIDI Track: the dialog on an audio clip of the loop, and the sliced rack (per 1/4 note, 8 pads)
    {
        const auto loop = writeLoop (juce::File (prefix + "-samples"));
        int clipIndex = -1;
        offline.editSong ([&] (Song& s) {
            s.bpm = 120.0;
            ChannelDef audio;
            audio.name = "drums";
            audio.instrument = InstrumentKind::audioClip;
            audio.samplePath = loop.getFullPathName().toStdString();
            s.channels.push_back (audio);
            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = 4;
            clip.start = 0;
            clip.length = 8 * ppq;
            s.playlist.push_back (clip);
            clipIndex = (int) s.playlist.size() - 1;
        }, false);
        offline.dispatchPendingMessages();
        for (const auto& [per, name] : { std::pair<int, const char*> { (int) rack::SlicePer::transient, "slice-dialog" }, { (int) rack::SlicePer::quarter, "slice-dialog-quarter" } })
        {
            auto dialog = makeSliceDialogForSnapshot (offline, clipIndex, per);
            writePng (shot::snapshot (*dialog), prefix + "-" + name + ".png");
        }
        juce::PopupMenu perMenu; // (the "Create one slice per" list, as its combo box drops it)
        for (const auto* text : { "Warp Marker", "Transient", "Bar", "1/2 Note", "1/4 Note", "1/8 Note", "1/16 Note", "1/32 Note" })
            perMenu.addItem (tr (text), true, juce::String (text) == "Transient", [] {});
        writePng (menuImage (perMenu, 160), prefix + "-slice-per-menu.png");

        const auto slices = rack::gridSlices (4.0, 0.0f, 1.0f, 0.5);
        auto drum = rack::sliceToDrumRack (loop.getFullPathName().toStdString(), slices, tr ("Slice").toStdString(), rack::SliceTemplate::gated, 1.0f, "drums");
        auto notes = rack::sliceNotes (slices, 1.0f, [] (float f) { return (double) f * 8.0 * ppq; }, 0);
        int sliced = -1;
        offline.editSong ([&] (Song& s) { sliced = rack::addSliceTrack (s, clipIndex, drum, notes, "drums"); }, false);
        offline.dispatchPendingMessages();
        check ("sliced: a new channel, track and pattern named drums, pads Slice 1…8",
               sliced >= 0 && offline.getSong().channels[(std::size_t) sliced].name == "drums" && offline.getSong().playlistTracks.size() > 5
                   && offline.getSong().playlistTracks[5].name == "drums" && rack::padName (*rack::rackOf (offline.getSong().channels[(std::size_t) sliced].patcher), 36) == "Slice 1");
        if (sliced >= 0)
            snap (sliced, "slice-result", [] (RackDoc& r) {
                r.showMacros = false, r.showPads = true, r.showChains = true, r.showDevices = true, r.showIO = true;
            });
    }
    juce::File (prefix + "-checks.txt").replaceWithText (checks);
}
} // namespace lynx::drumshot
