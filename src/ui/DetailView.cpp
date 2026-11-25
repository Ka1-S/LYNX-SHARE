#include "ui/TextCache.h"
#include "ui/ShotShowing.h"
#include "ui/DetailView.h"
#include "core/StandIns.h"
#include "ui/MapMode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include "core/AlsImport.h"
#include "core/Braid.h"
#include "core/ClipLauncher.h"
#include "core/ClipLoopOps.h"
#include "core/ClipSample.h"
#include "core/DetailChain.h"
#include "core/RackOps.h"
#include "core/Groove.h"
#include "core/GrooveAudio.h"
#include "core/Effects.h"
#include "core/FlEffects.h"
#include "core/HotSwap.h"
#include "core/MeterBallistics.h"
#include "core/NoteEdit.h"
#include "core/NoteNames.h"
#include "core/NoteTools.h"
#include "core/PlaylistOps.h"
#include "core/ProjectKey.h"
#include "core/Rack.h"
#include "core/NativeEngines.h"
#include "engine/AudioEngine.h"
#include "engine/FlStudio.h"
#include "ui/BrowserComponent.h"
#include "ui/ClipAudioPanel.h"
#include "ui/ColourPicker.h"
#include "ui/EditorTools.h"
#include "ui/EventEditor.h"
#include "ui/FlMods.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/MidiToolsPanel.h"
#include "ui/MixerComponent.h"
#include "ui/NumBox.h"
#include "ui/LiveClipLook.h"
#include "ui/LiveLook.h"
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/PluginHeader.h"
#include "ui/PluginThumbs.h"
#include "ui/QuickPick.h"
#include "ui/RackEditor.h"
#include "ui/Theme.h"
#include "ui/TimerWhileShowing.h"
#include "ui/ValueControl.h"

namespace lynx
{
using namespace theme;

namespace
{
juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// A size in LYNX's look and in Live's (round 190jn, design190ia CL-21: the Clip-launch layout's Live look); reads as an int.
struct LookMetric
{
    int classic, live;
    operator int() const noexcept { return theme::livelook::active() ? live : classic; }
};

// card geometry (px at 100 %); Live 12.4's device title bar is 17 px (live-session.png Utility y 807..823)
constexpr int contextWidth = 132, meterWidth = 22, gap = 4, foldedWidth = 22, addWidth = 44;
const LookMetric titleHeight { 20, 17 };
constexpr int knobColumn = 62, knobRow = 70, knobSize = 30, instrumentWidth = 196, pluginWidth = 172;
constexpr int maxEffectKnobs = 7; // + the slot's Mix
// Round 190ko: an instrument card can show its engine's main parameters as knobs (Live's generic device look: knob
// columns, two rows), as an effect card does. Knob id = engineKnobBase + the engine's parameter index.
constexpr int engineKnobBase = 100;
const std::vector<int>& engineCardParams (InstrumentKind kind)
{
    namespace b = braid;
    // Braid (Live's Meld): engine A on the top row, B below — oscillator type and macros, filter frequency and macros, volume
    static const std::vector<int> braidKnobs = [] {
        std::vector<int> v;
        for (const int p : { b::eng::oscType, b::eng::oscMacro1, b::eng::oscMacro2, b::eng::filterFreq, b::eng::filterMacro1, b::eng::filterMacro2,
                             b::eng::volume })
            for (int k = 0; k < 2; ++k)
                v.push_back (b::engineBase (k) + p);
        return v;
    }();
    static const std::vector<int> none;
    return kind == InstrumentKind::braid ? braidKnobs : none;
}

juce::String insertName (const Song& song, int insert)
{
    if (insert == 0)
        return tr ("Master");
    if (insert < 0 || insert >= (int) song.mixer.size())
        return {};
    const auto own = utf8 (song.mixer[(std::size_t) insert].name);
    const auto number = tr ("Insert {0}", { juce::String (insert) });
    // (round 190oo CS-13: an insert still called by its default name read "Insert 1 Insert 1")
    const auto isDefault = own == "Insert " + juce::String (insert) || own == number;
    return own.isNotEmpty() && ! isDefault ? number + " " + own : number;
}

juce::String trackName (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return utf8 (song.playlistTracks[(std::size_t) track].name);
    return tr ("Track {0}", { juce::String (track + 1) });
}

void drawVerticalText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area)
{
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, (float) area.getCentreX(), (float) area.getCentreY()));
    const auto turned = juce::Rectangle<int> (area.getHeight(), area.getWidth()).withCentre (area.getCentre());
    g.drawText (text, turned.reduced (4, 0), juce::Justification::centredRight, true);
}

// The window glyph on a card's title bar.
void drawWindowGlyph (juce::Graphics& g, juce::Rectangle<int> r, bool hot)
{
    const auto box = r.toFloat().withSizeKeepingCentre (11.0f, 9.0f);
    g.setColour (hot ? colour::textPrimary : colour::textSecondary);
    g.drawRect (box, 1.0f);
    g.fillRect (box.withHeight (2.5f));
}

// Round 190ce: the hot-swap button of a card's title bar (the reference's round button with two arrows, left of its
// save button [lesson image BrowseButtonInst.png, shot d04]); filled with the accent colour while the Browser is linked.
void drawSwapGlyph (juce::Graphics& g, juce::Rectangle<int> r, bool hot, bool linked)
{
    const auto box = r.toFloat().withSizeKeepingCentre (13.0f, 13.0f);
    if (linked)
    {
        g.setColour (colour::accent);
        g.fillEllipse (box);
    }
    g.setColour (linked ? juce::Colours::white : hot ? colour::textPrimary : colour::textSecondary);
    g.drawEllipse (box, 1.0f);
    const auto cx = box.getCentreX(), cy = box.getCentreY();
    juce::Path p; // two arrows, one each way
    p.startNewSubPath (cx - 3.0f, cy - 2.0f);
    p.lineTo (cx + 3.0f, cy - 2.0f);
    p.lineTo (cx + 1.0f, cy - 4.0f);
    p.startNewSubPath (cx + 3.0f, cy + 2.0f);
    p.lineTo (cx - 3.0f, cy + 2.0f);
    p.lineTo (cx - 1.0f, cy + 4.0f);
    g.strokePath (p, juce::PathStrokeType (1.0f));
}

// Round 190jn (CL-21): Live's title-bar wells (theme::livelook::drawWell) and the LYNX vector glyphs drawn in them.
void drawLiveWellGlyph (juce::Graphics& g, juce::Rectangle<int> r, int kind, bool hot, bool lit)
{
    namespace ll = theme::livelook;
    const auto box = r.toFloat();
    auto ink = ll::drawWell (g, box, lit);
    if (hot && ! lit)
        ink = colour::textPrimary;
    g.setColour (ink);
    const auto cx = box.getCentreX(), cy = box.getCentreY();
    if (kind == 0) // window: a small frame with its title band
    {
        const auto f = juce::Rectangle<float> (7.0f, 6.0f).withCentre ({ cx, cy });
        g.drawRect (f, 1.0f);
        g.fillRect (f.withHeight (2.0f));
    }
    else if (kind == 1) // hot-swap: two arrows, one each way
    {
        juce::Path p;
        p.startNewSubPath (cx - 3.0f, cy - 1.5f);
        p.lineTo (cx + 3.0f, cy - 1.5f);
        p.lineTo (cx + 1.0f, cy - 3.5f);
        p.startNewSubPath (cx + 3.0f, cy + 1.5f);
        p.lineTo (cx - 3.0f, cy + 1.5f);
        p.lineTo (cx - 1.0f, cy + 3.5f);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }
    else // save: a filled disk with its label slot
    {
        const auto d = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, cy });
        g.fillRoundedRectangle (d, 1.0f);
        g.setColour (lit ? ll::on : ll::well);
        g.fillRect (d.getX() + 2.0f, d.getY() + 1.0f, 3.0f, 2.0f);
    }
}

// The save-preset button beside it (the reference's disk).
void drawSaveGlyph (juce::Graphics& g, juce::Rectangle<int> r, bool hot)
{
    const auto box = r.toFloat().withSizeKeepingCentre (11.0f, 11.0f);
    g.setColour (hot ? colour::textPrimary : colour::textSecondary);
    g.drawRoundedRectangle (box, 1.5f, 1.0f);
    g.fillRect (box.getX() + 3.0f, box.getY() + 1.0f, 5.0f, 3.0f);
    g.fillRect (box.getX() + 2.5f, box.getBottom() - 4.5f, 6.0f, 3.0f);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The Device View: the chain of cards.
class DetailView::DeviceChain final : public juce::Component,
                                      public ui::HintSource,
                                      public mapmode::SpotSource,
                                      public juce::DragAndDropTarget,
                                      private juce::ChangeListener,
                                      private juce::Timer,
                                      private juce::ScrollBar::Listener
{
public:
    DeviceChain (AudioEngine& e, Hooks& h) : engine (e), hooks (h), slotActions (e)
    {
        setWantsKeyboardFocus (true);
        setTitle (tr ("Device view"));
        // the cards' menus are the Mixer slot's own (same code), opened over this view, with Fold / Unfold first
        slotActions.setMenuTarget (this);
        slotActions.extendSlotMenu = [this] (juce::PopupMenu& menu, int slot) {
            const auto key = tagOf (slot);
            // round 190ce: the reference's device menu starts with Cut / Copy, then Fold [shot d04]; Paste and
            // Duplicate are its Edit menu's commands on the selected device. Round gap190og (deviceview-0012): "Pasted
            // devices are inserted in front of the selected device" [DOC Live 12 manual, Working with Instruments and
            // Effects] (before: after it)
            menu.addItem (tr ("Cut") + "\tCtrl+X", [this, slot] { copyDevice (slot, true); });
            menu.addItem (tr ("Copy") + "\tCtrl+C", [this, slot] { copyDevice (slot, false); });
            menu.addItem (tr ("Paste") + "\tCtrl+V", deviceClipboard().has_value() && effectCount() < maxEffectSlots, false, [this, slot] { pasteDevice (slot); });
            menu.addItem (tr ("Duplicate") + "\tCtrl+D", effectCount() < maxEffectSlots, false, [this, slot] { duplicateDevice (slot, -1); });
            menu.addSeparator();
            menu.addItem (folded.count (key) > 0 ? tr ("Unfold") : tr ("Fold"), [this, key] { toggleFold (key); });
            menu.addItem (tr ("Hot-swap in the Browser") + "\tQ", hooks.toggleHotSwap != nullptr, linkedTo (hotswap::Kind::effect, key), [this, slot] { toggleSwap (hotswap::targetOfEffect (engine.getSong(), insert, slot)); });
            if (canSavePreset (slot))
                menu.addItem (tr ("Save preset..."), [this, slot] { savePreset (slot); });
            menu.addSeparator();
        };
        addAndMakeVisible (cardHost); // (custom card bodies; clicks elsewhere fall through to this view)
        cardHost.setInterceptsMouseClicks (false, true);
        addChildComponent (scrollBar);
        scrollBar.addListener (this);
        scrollBar.setAutoHide (false);
        engine.addChangeListener (this);
        showingTimer = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 30);
        showingTimer->onRunningChanged = [this] (bool running) {
            if (! running)
                watchDevices (false); // (round 190lr: hidden, the audio thread stops measuring the devices' levels)
        };
    }

    ~DeviceChain() override
    {
        watchDevices (false);
        engine.removeChangeListener (this);
    }

    void setTrack (int t)
    {
        if (t != track)
            chosenInsert = -1;
        track = t;
        refresh();
    }

    int insertShown() const noexcept { return insert; }
    int channelShown() const noexcept { return channel; }
    int cardCount() const noexcept { return (int) cards.size(); }
    // (issue #191's check: where the card's window button is, in this component's coordinates; empty: none)
    juce::Rectangle<int> windowButtonOf (int i) const
    {
        if (i < 0 || i >= (int) cards.size() || cards[(std::size_t) i].kind == Card::add || cards[(std::size_t) i].folded)
            return {};
        return windowBounds (cards[(std::size_t) i]);
    }
    bool isCardFolded (int i) const { return i >= 0 && i < (int) cards.size() && cards[(std::size_t) i].folded; }
    // (--zorder191test, rollup 0.1.31 step 2: the card's window button pressed when a plugin editor covers it on screen)
    bool pressWindowButtonForCheck (int i)
    {
        if (i < 0 || i >= (int) cards.size())
            return false;
        toggleWindow (cards[(std::size_t) i]);
        return true;
    }
    void toggleCardFold (int i)
    {
        if (i >= 0 && i < (int) cards.size() && cards[(std::size_t) i].kind != Card::add)
            toggleFold (keyOf (cards[(std::size_t) i]));
    }
    // round 190ce: what the hot-swap key links the Browser to (the selected card; none: nothing selected)
    hotswap::Target selectedTarget() const
    {
        const auto i = selectedCard();
        return i < 0 ? hotswap::Target() : targetOf (cards[(std::size_t) i]);
    }
    void selectCard (int i) { select (i); }
    void showCardMenuForSnapshot (int i)
    {
        if (i >= 0 && i < (int) cards.size() && cards[(std::size_t) i].kind != Card::add)
            showCardMenu (i);
    }
    // (round 190hr shots and checks) Ctrl+G / Ctrl+Shift+G on the instrument card
    bool groupCardForCheck (int i, bool ungroup)
    {
        if (i < 0 || i >= (int) cards.size() || cards[(std::size_t) i].kind != Card::instrument)
            return false;
        const auto channel = cards[(std::size_t) i].index;
        if (ungroup)
            ungroupInstrument (channel);
        else
            groupInstrument (channel);
        const auto& song = engine.getSong();
        return channel < (int) song.channels.size() && rack::isRack (song.channels[(std::size_t) channel]) != ungroup;
    }
    // the 0 key: the selected device on / off
    bool toggleSelectedOn()
    {
        const auto i = selectedCard();
        if (i < 0)
            return false;
        toggleOn (cards[(std::size_t) i]);
        return true;
    }
    // round gap190og (deviceview-0012): Live's Edit menu on the selected device — Cut, Copy, Paste, Duplicate, Delete
    // [SHOT live190gu/screens-live/live-edit-menu.png; DOC Live 12 manual: "Edit menu commands such as cut, copy, paste,
    // and duplicate can be used on devices", "select Delete from the Edit menu"]. Paste goes in front of the selected
    // device, at the end of the chain with none (or the instrument) selected.
    enum class EditCommand { cut, copy, paste, duplicate, remove };
    bool canEdit (EditCommand command) const
    {
        const auto i = selectedCard();
        const auto effect = i >= 0 && cards[(std::size_t) i].kind == Card::effect;
        switch (command)
        {
            case EditCommand::cut:
            case EditCommand::copy:
            case EditCommand::remove: return effect;
            case EditCommand::paste: return insert >= 0 && deviceClipboard().has_value() && effectCount() < maxEffectSlots;
            case EditCommand::duplicate: return effect && effectCount() < maxEffectSlots;
        }
        return false;
    }
    bool runEdit (EditCommand command)
    {
        if (! canEdit (command))
            return false;
        const auto i = selectedCard();
        const auto effect = i >= 0 && cards[(std::size_t) i].kind == Card::effect;
        const auto slot = effect ? cards[(std::size_t) i].index : -1;
        switch (command)
        {
            case EditCommand::cut: copyDevice (slot, true); break;
            case EditCommand::copy: copyDevice (slot, false); break;
            case EditCommand::paste: pasteDevice (effect ? slot : effectCount()); break;
            case EditCommand::duplicate: duplicateDevice (slot, -1); break;
            case EditCommand::remove:
                slotActions.deleteSlot (slot);
                refresh();
                break;
        }
        return true;
    }

    bool duplicateCard (int i)
    {
        if (i < 0 || i >= (int) cards.size() || cards[(std::size_t) i].kind != Card::effect)
            return false;
        duplicateDevice (cards[(std::size_t) i].index, -1);
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::livelook::active() ? theme::livelook::window : colour::bg1); // (Live: the cards lie on #2A2A2A)
        paintContext (g);

        const auto area = chainArea();
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (area);
            for (int i = 0; i < (int) cards.size(); ++i)
            {
                paintCard (g, i);
                if (const auto& card = cards[(std::size_t) i]; card.kind != Card::add && cardLocked (card))
                {
                    // (round 190ff) a frozen device: dimmed, with the freeze tag on its title
                    g.setColour (colour::bg1.withAlpha (0.45f));
                    g.fillRect (card.bounds.reduced (1));
                    theme::drawFrozenTag (g, juce::Rectangle<float> ((float) card.bounds.getRight() - 22.0f, (float) card.bounds.getY() + 4.0f, 14.0f, 13.0f),
                                          colour::info.withAlpha (0.85f), colour::bg1);
                }
            }
            if (insert < 0 && channel < 0)
            {
                g.setColour (colour::textSecondary); // (round 190oj CS-15: the hints read 3.0-3.2:1)
                g.setFont (font (type::small));
                g.drawText (track < 0 ? tr ("Select a track in the clip grid to see its devices")
                                      : tr ("This track has no clips yet: put a clip on it to see its devices"),
                            area, juce::Justification::centred, true);
            }
            else if (insert >= 0) // the free room after the last card shows the drop hint, as the reference does [SHOT d01]
            {
                const auto room = area.withLeft (cards.empty() ? area.getX() : cards.back().bounds.getRight() + cardGap());
                if (room.getWidth() > 120)
                {
                    g.setColour (colour::textSecondary);
                    g.setFont (font (type::small));
                    g.drawText (tr ("Drop effects here"), room, juce::Justification::centred, true);
                }
            }
            if (dropBefore >= 0)
            {
                g.setColour (colour::accent);
                g.fillRect (juce::Rectangle<int> (dropLineX (dropBefore) - 1, area.getY() + 2, 3, area.getHeight() - 4));
            }
            paintDeviceMeters (g); // round 190lr (DL-87)
        }
        paintMeter (g);
    }

    void resized() override
    {
        const auto area = chainArea();
        scrollBar.setBounds (area.getX(), getHeight() - 8, area.getWidth(), 8);
        cardHost.setBounds (area);
        layoutCards();
    }

    // --- mouse ---------------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto hit = hitAt (e.getPosition());
        dragCard = -1;
        dragging = false;
        if (hit.part == Part::picker)
            return showInsertMenu();
        if (hit.card < 0)
            return;
        const auto& card = cards[(std::size_t) hit.card];
        if (card.kind == Card::add)
        {
            if (! refuseLocked (card))
                slotActions.showAddMenuFor (nextFreeRow());
            return;
        }
        select (hit.card);
        // round 190ld (DL-83): the clicked parameter is the selected one, which Delete returns to its default
        selectedKnobCard = hit.part == Part::knob ? hit.card : -1;
        selectedKnob = hit.part == Part::knob ? hit.knob : -1;
        if (hit.part != Part::title && hit.part != Part::folded && refuseLocked (card, hit.part == Part::knob ? hit.knob : -2))
            return;

        if (hit.part == Part::knob)
        {
            const auto spec = knobSpec (card, hit.knob);
            const auto value = knobValue (card, hit.knob);
            if (flmods::popup (e.mods))
            {
                ui::showValueMenu (*this, &engine, spec, value, [this, card, knob = hit.knob] (double v) { setKnob (card, knob, v); });
                return;
            }
            if (ui::isResetClick (e))
            {
                setKnob (card, hit.knob, spec.defaultValue);
                ui::flashValueTip (*this, knobBounds (card, hit.knob), spec.text (spec.defaultValue));
                return;
            }
            knobDrag = hit.knob;
            knobCard = hit.card;
            drag.begin (e, value);
            ui::showValueTip (*this, knobBounds (card, hit.knob), spec.text (value));
            return;
        }
        if (flmods::popup (e.mods))
            return showCardMenu (hit.card);
        if (hit.part == Part::led)
            return toggleOn (card);
        if (hit.part == Part::window)
            return toggleWindow (card);
        if (hit.part == Part::swap)
            return toggleSwap (targetOf (card));
        if (hit.part == Part::save)
            return savePreset (card.index);
        if (hit.part == Part::title && card.kind == Card::effect)
            dragCard = hit.card; // (a drag moves it; a click only selects; Ctrl + drag copies it)
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (knobDrag >= 0 && knobCard >= 0 && knobCard < (int) cards.size())
        {
            const auto card = cards[(std::size_t) knobCard];
            const auto spec = knobSpec (card, knobDrag);
            const auto v = drag.update (e, spec);
            setKnob (card, knobDrag, v);
            ui::reportTweak (spec);
            ui::showValueTip (*this, knobBounds (card, knobDrag), spec.text (v));
            return;
        }
        // round 190dr: out of the chain, the card goes on as a drag-and-drop the clip grid takes (another track)
        if (dragCard >= 0 && dragCard < (int) cards.size() && cards[(std::size_t) dragCard].kind == Card::effect && insert >= 0
            && (e.y < 0 || e.y >= getHeight()))
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this); container != nullptr && ! container->isDragAndDropActive())
            {
                const auto& card = cards[(std::size_t) dragCard];
                auto image = createComponentSnapshot (card.bounds.getIntersection (getLocalBounds()).withHeight (juce::jmin (card.bounds.getHeight(), 28)));
                container->startDragging (deviceDragDescription (insert, card.index), this, juce::ScaledImage (image), true);
                dragCard = -1;
                dragging = false;
                dropBefore = -1;
                repaint();
                return;
            }
        if (dragCard >= 0 && (dragging || e.getDistanceFromDragStart() > 4))
        {
            dragging = true;
            dropBefore = positionAt (e.x);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (knobDrag >= 0)
        {
            drag.end();
            ui::hideValueTip();
            knobDrag = knobCard = -1;
            return;
        }
        if (dragging && dragCard >= 0 && dragCard < (int) cards.size())
        {
            const auto from = cards[(std::size_t) dragCard].index;
            const auto to = effectPositionFor (positionAt (e.x));
            const auto tag = tagOf (from);
            if (e.mods.isCtrlDown()) // round 190ce: Ctrl + drag leaves the device where it was and drops a copy
                duplicateDevice (from, to);
            else
            {
                engine.editSongStructural ([t = insert, from, to] (Song& song) { detailchain::moveEffectBefore (song, t, from, to); });
                selectedTag = tag;
            }
        }
        dragCard = -1;
        dragging = false;
        dropBefore = -1;
        refresh();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = hitAt (e.getPosition());
        if (hit.card < 0 || cards[(std::size_t) hit.card].kind == Card::add)
            return;
        const auto& card = cards[(std::size_t) hit.card];
        if (hit.part == Part::knob)
        {
            if (! refuseLocked (card, hit.knob))
                setKnob (card, hit.knob, knobSpec (card, hit.knob).defaultValue);
            return;
        }
        if (hit.part == Part::title || hit.part == Part::folded) // fold / unfold (the reference: double-click the title bar)
            toggleFold (keyOf (card));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto hit = hitAt (e.getPosition());
        if (hit.part == Part::knob && ! cardLocked (cards[(std::size_t) hit.card], hit.knob))
        {
            const auto& card = cards[(std::size_t) hit.card];
            const auto spec = knobSpec (card, hit.knob);
            if (const auto v = ui::wheelValue (knobValue (card, hit.knob), e, wheel, spec))
            {
                setKnob (card, hit.knob, *v);
                ui::reportTweak (spec);
                ui::flashValueTip (*this, knobBounds (card, hit.knob), spec.text (*v));
                return;
            }
        }
        const auto delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        if (delta != 0.0f && contentWidth > chainArea().getWidth())
        {
            ui::noteWheelScroll();
            setScroll (scroll - juce::roundToInt (delta * 240.0f));
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto hit = hitAt (e.getPosition());
        if (hit.card != hover.card || hit.part != hover.part || hit.knob != hover.knob)
        {
            hover = hit;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = {};
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto i = selectedCard();
        // Live (manual 17.1, details DL-83): "Delete returns the selected parameter to its default" — the knob clicked
        // last; with no parameter selected Delete removes the selected device as before
        if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)) && ! key.getModifiers().isAnyModifierKeyDown()
            && selectedKnobCard >= 0 && selectedKnobCard == i && selectedKnobCard < (int) cards.size() && selectedKnob >= 0)
        {
            const auto& card = cards[(std::size_t) selectedKnobCard];
            if (! cardLocked (card, selectedKnob))
            {
                const auto spec = knobSpec (card, selectedKnob);
                setKnob (card, selectedKnob, spec.defaultValue);
                ui::flashValueTip (*this, knobBounds (card, selectedKnob), spec.text (spec.defaultValue));
            }
            return true;
        }
        if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)) && i >= 0
            && cards[(std::size_t) i].kind == Card::effect)
        {
            slotActions.deleteSlot (cards[(std::size_t) i].index);
            refresh();
            return true;
        }
        // round 190ce: the reference's Edit commands on the selected device, and its Group / Ungroup keys
        if (key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown() && i >= 0 && cards[(std::size_t) i].kind == Card::effect)
        {
            const auto slot = cards[(std::size_t) i].index;
            const auto shift = key.getModifiers().isShiftDown();
            const auto code = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode());
            if (! shift && (code == 'C' || code == 'X'))
            {
                copyDevice (slot, code == 'X');
                return true;
            }
            if (! shift && code == 'D')
            {
                duplicateDevice (slot, -1);
                return true;
            }
            if (! shift && code == 'V')
            {
                pasteDevice (slot); // (round gap190og: in front of the selected device, Live's rule)
                return true;
            }
            if (code == 'G')
            {
                if (! shift)
                    slotActions.groupIntoRack (slot, false);
                else
                    ungroupDevice (slot);
                refresh();
                return true;
            }
        }
        // round 190hr (racks-061 / -062): Group and Ungroup on the instrument: an Instrument Rack holding it, and back
        if (key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown() && i >= 0 && cards[(std::size_t) i].kind == Card::instrument
            && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) == 'G')
        {
            if (key.getModifiers().isShiftDown())
                ungroupInstrument (cards[(std::size_t) i].index);
            else
                groupInstrument (cards[(std::size_t) i].index);
            return true;
        }
        if (key.getModifiers().isCommandDown() && ! key.getModifiers().isAltDown() && ! key.getModifiers().isShiftDown()
            && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) key.getKeyCode()) == 'V' && insert >= 0 && deviceClipboard().has_value())
        {
            pasteDevice (effectCount()); // (nothing selected, or the instrument: at the end of the chain)
            return true;
        }
        // the reference's 0 key: the selected device on / off (the typing keyboard keeps the key while it is on)
        if (! key.getModifiers().isAnyModifierKeyDown() && (key.getKeyCode() == '0' || key.getTextCharacter() == '0')
            && (hooks.typingKeyboardOn == nullptr || ! hooks.typingKeyboardOn()) && toggleSelectedOn())
            return true;
        if (key.isKeyCode (juce::KeyPress::leftKey) || key.isKeyCode (juce::KeyPress::rightKey))
        {
            const auto step = key.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1;
            const auto last = (int) cards.size() - (cards.empty() || cards.back().kind != Card::add ? 1 : 2);
            if (last >= 0)
                select (juce::jlimit (0, last, (i < 0 ? 0 : i + step)));
            return true;
        }
        // LYNX extra (off by default): W shows or hides the selected card's window
        if (DetailView::extraWindowKey && ! key.getModifiers().isAnyModifierKeyDown() && i >= 0
            && (key.getKeyCode() == 'W' || juce::CharacterFunctions::toUpperCase (key.getTextCharacter()) == 'W'))
        {
            toggleWindow (cards[(std::size_t) i]);
            return true;
        }
        return false;
    }

    // round 190ef: every device knob is mappable (MIDI / Key Map Mode)
    void mapSpots (std::vector<mapmode::Spot>& out) override
    {
        for (const auto& c : cards)
            if (c.kind != Card::add && c.custom == nullptr)
                for (int k = 0; k < (int) c.knobs.size(); ++k)
                    if (const auto r = knobBounds (c, k); ! r.isEmpty())
                        if (const auto target = knobSpec (c, k).target)
                            out.push_back ({ r, remotemap::Target::parameter (*target) });
    }

    // --- hints ---------------------------------------------------------------------------------------------------
    ui::Hint hintAt (juce::Point<int> p) override
    {
        const auto hit = hitAt (p);
        if (hit.part == Part::picker)
            return { tr ("The insert this track plays through: click to pick another of its inserts"), insertName (engine.getSong(), insert), false, true };
        if (hit.card < 0)
            return {};
        const auto& card = cards[(std::size_t) hit.card];
        switch (hit.part)
        {
            case Part::knob:
            {
                const auto spec = knobSpec (card, hit.knob);
                return { spec.name, spec.text (knobValue (card, hit.knob)), spec.target.has_value(), true };
            }
            case Part::led: return { card.kind == Card::instrument ? tr ("Channel on / off (its mute)") : tr ("Effect on / off (bypass)"), {}, false, false };
            case Part::window: return { tr ("Show or hide the window"), {}, false, false };
            case Part::swap: return { tr ("Hot-swap: link the Browser to this device; what you load there takes its place (Q, again to end)"), cardName (card), false, false };
            case Part::save: return { tr ("Save the device's settings as a preset"), cardName (card), false, false };
            case Part::title:
            case Part::folded:
                return { tr ("Click: select. Double-click: fold / unfold. Drag: move it in the chain (Ctrl + drag: copy). Right-click: its menu"), cardName (card), false, true };
            default: break;
        }
        if (card.kind == Card::add)
            return { tr ("Add an effect to this insert (or drop one from the Browser)"), {}, false, true };
        return { {}, cardName (card), false, false };
    }

    // --- drops from the Browser ----------------------------------------------------------------------------------
    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        int from = -1, slot = -1;
        return insert >= 0
               && ((BrowserComponent::isBrowserDrag (details) && BrowserComponent::draggedFile (details).hasFileExtension ("fst;adv;adg"))
                   || BrowserComponent::isEffectDrag (details) || BrowserComponent::isGeneratorDrag (details)
                   || (detailDeviceDrag (details.description, from, slot) && from == insert)); // (round 190dr: back into its own chain)
    }
    void itemDragMove (const SourceDetails& details) override
    {
        const auto at = BrowserComponent::isGeneratorDrag (details) ? -1 : positionAt (details.localPosition.x);
        if (at != dropBefore)
        {
            dropBefore = at;
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override
    {
        dropBefore = -1;
        repaint();
    }
    void itemDropped (const SourceDetails& details) override
    {
        const auto position = effectPositionFor (positionAt (details.localPosition.x));
        dropBefore = -1;
        repaint();
        if (freeze::frozenByInsert (engine.getSong(), insert) >= 0) // (round 190ff: a frozen track's devices stay as they are)
        {
            ui::postNotice ({ tr ("This track is frozen: Unfreeze the track to edit its devices"), {} }, 3000);
            return;
        }
        // round 190dr: a card that left the chain and came back: a move inside the chain (Ctrl: a copy), as a plain drag
        if (int from = -1, slot = -1; detailDeviceDrag (details.description, from, slot))
        {
            if (from != insert)
                return;
            if (juce::ModifierKeys::currentModifiers.isCommandDown())
                duplicateDevice (slot, position);
            else
            {
                const auto tag = tagOf (slot);
                engine.editSongStructural ([t = insert, slot, position] (Song& song) { detailchain::moveEffectBefore (song, t, slot, position); });
                selectedTag = tag;
            }
            refresh();
            return;
        }
        if (BrowserComponent::isGeneratorDrag (details))
        {
            auto c = BrowserComponent::draggedGenerator (details);
            if (! c)
                return;
            // issue #152 point 6 (Live: an instrument dropped onto the track's instrument in the Device View replaces
            // only it; its effects stay): the instrument card under the mouse takes the plugin
            if (const auto hit = hitAt (details.localPosition); juce::isPositiveAndBelow (hit.card, (int) cards.size())
                                                                 && cards[(std::size_t) hit.card].kind == Card::instrument)
            {
                const auto channel = cards[(std::size_t) hit.card].index;
                if (c->instrument != InstrumentKind::audioClip && c->instrument != InstrumentKind::controller)
                {
                    engine.editSong ([channel, gen = std::move (*c)] (Song& song) mutable { replaceGenerator (song, channel, std::move (gen)); });
                    refresh();
                }
                return;
            }
            if (hooks.instrumentDropped != nullptr)
                hooks.instrumentDropped (insert, std::move (*c));
            return;
        }
        std::optional<EffectSlot> fx;
        if (BrowserComponent::isEffectDrag (details))
            fx = BrowserComponent::draggedEffect (details);
        else if (const auto file = BrowserComponent::draggedFile (details); RackWindows::isLivePreset (file))
        {
            // round 190hr: Live's device / rack presets — an effect into the chain; an instrument (an .adv instrument, an
            // Instrument or Drum Rack) onto the instrument card replaces it, elsewhere it makes a channel, as a dropped plugin
            juce::MemoryBlock data;
            if (file.loadFileAsData (data))
            {
                const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                if (auto live = importLiveDevicePreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
                    fx = std::move (live->slot);
            }
            if (! fx)
            {
                juce::String error;
                auto c = RackWindows::instrumentFromLivePreset (file, error);
                if (! c)
                    return ui::postHint ({ error, {} }, 3000);
                if (const auto hit = hitAt (details.localPosition); juce::isPositiveAndBelow (hit.card, (int) cards.size())
                                                                     && cards[(std::size_t) hit.card].kind == Card::instrument)
                {
                    engine.editSong ([channel = cards[(std::size_t) hit.card].index, gen = std::move (*c)] (Song& song) mutable { replaceGenerator (song, channel, std::move (gen)); });
                    refresh();
                }
                else if (hooks.instrumentDropped != nullptr)
                    hooks.instrumentDropped (insert, std::move (*c));
                return;
            }
        }
        else
        {
            juce::MemoryBlock data;
            if (BrowserComponent::draggedFile (details).loadFileAsData (data))
            {
                const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
                fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), engine.getSong().bpm,
                                         natives::flDataReader (flStudioAccess()));
            }
            if (! fx)
            {
                ui::postHint ({ tr ("LYNX SHARE can't build an effect from this preset."), {} }, 3000);
                return;
            }
        }
        if (! fx)
            return;
        dropEffect (std::move (*fx), position);
    }

private:
    struct Card
    {
        enum Kind { instrument, effect, add } kind = effect;
        int index = -1;                 // effect: its slot index; instrument: the channel
        std::uint64_t tag = 0;          // effect: the slot's instance (fold and selection follow it through moves)
        bool folded = false;
        juce::Rectangle<int> bounds;    // in this component (scroll applied)
        std::vector<int> knobs;         // effect: parameter ids (-1 = Mix); instrument: 0 volume, 1 pan
        DetailCard* custom = nullptr;   // a body drawn by another module (DetailView::*CardFactory)
    };
    struct CustomEntry
    {
        int kind = -1; // the slot's EffectKind / the channel's InstrumentKind it was made for
        std::unique_ptr<DetailCard> card;
    };
    enum class Part { none, picker, title, led, window, swap, save, knob, body, folded };
    int selectedKnobCard = -1, selectedKnob = -1; // round 190ld (DL-83): the parameter Delete resets
    struct Hit
    {
        Part part = Part::none;
        int card = -1, knob = -1;
    };

    // --- the model -----------------------------------------------------------------------------------------------
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        const auto& song = engine.getSong();
        const auto c = detailchain::forTrack (song, track);
        auto wanted = c.insert;
        if (chosenInsert >= 0 && chosenInsert < (int) song.mixer.size()
            && (chosenInsert == c.insert || std::find (c.inserts.begin(), c.inserts.end(), chosenInsert) != c.inserts.end()))
            wanted = chosenInsert;
        if (wanted < 0 && ! c.inserts.empty())
            wanted = c.inserts.front(); // several inserts: the first until one is picked
        inserts = c.inserts;
        if (c.insert >= 0 && std::find (inserts.begin(), inserts.end(), c.insert) == inserts.end())
            inserts.insert (std::lower_bound (inserts.begin(), inserts.end(), c.insert), c.insert);
        insert = wanted;
        channel = c.channel >= 0 && detailchain::insertOfChannel (song, c.channel) == insert ? c.channel
                  : c.channel >= 0 && c.fromTrackMode                                       ? c.channel
                                                                                            : -1;
        if (insert >= 0 && insert != actionsInsert)
        {
            slotActions.setTrack (insert);
            actionsInsert = insert;
        }
        layoutCards();
        repaint();
    }

    int effectCount() const
    {
        const auto& song = engine.getSong();
        return insert >= 0 && insert < (int) song.mixer.size() ? (int) song.mixer[(std::size_t) insert].effects.size() : 0;
    }
    const EffectSlot* effectAt (int index) const
    {
        const auto& song = engine.getSong();
        if (insert < 0 || insert >= (int) song.mixer.size() || index < 0 || index >= (int) song.mixer[(std::size_t) insert].effects.size())
            return nullptr;
        return &song.mixer[(std::size_t) insert].effects[(std::size_t) index];
    }
    std::uint64_t tagOf (int index) const
    {
        const auto* fx = effectAt (index);
        return fx != nullptr ? fx->instance.id : 0;
    }
    // fold keys: an effect by its slot instance, the instrument by its channel (high bit set)
    std::uint64_t keyOf (const Card& c) const noexcept
    {
        return c.kind == Card::instrument ? (std::uint64_t (1) << 63) | (std::uint64_t) c.index : c.tag;
    }
    void toggleFold (std::uint64_t key)
    {
        if (folded.erase (key) == 0)
            folded.insert (key);
        layoutCards();
        repaint();
    }

    // Freeze (round 190ff): a frozen track's devices are not loaded and cannot be edited, as in the reference (its frozen
    // devices are greyed out). The instrument card of a frozen channel (its body; with a track freeze its volume and pan
    // too) and every card of an insert whose effects a track freeze holds.
    bool cardLocked (const Card& c, int knob = -2) const
    {
        const auto& song = engine.getSong();
        if (c.kind == Card::instrument)
        {
            if (c.index < 0 || c.index >= (int) song.channels.size() || ! song.channels[(std::size_t) c.index].freeze.on())
                return false;
            const auto levelsLive = song.channels[(std::size_t) c.index].freeze.insert == 0; // (a channel freeze: they apply live)
            return ! (levelsLive && knob >= 0);
        }
        return freeze::frozenByInsert (song, insert) >= 0;
    }
    bool refuseLocked (const Card& c, int knob = -2)
    {
        if (! cardLocked (c, knob))
            return false;
        ui::postNotice ({ tr ("This track is frozen: Unfreeze the track to edit its devices"), {} }, 3000);
        return true;
    }

    void layoutCards()
    {
        cards.clear();
        const auto area = chainArea();
        auto x = area.getX() - scroll;
        const auto top = area.getY(), height = area.getHeight();
        // (round 190rb: `channel` can be a step behind the song too, e.g. right after another project was opened)
        if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        {
            Card c;
            c.kind = Card::instrument;
            c.index = channel;
            c.folded = folded.count (keyOf (c)) > 0;
            c.knobs = { 0, 1 };
            for (const auto p : engineCardParams (engine.getSong().channels[(std::size_t) channel].instrument))
                c.knobs.push_back (engineKnobBase + p);
            c.custom = customFor (keyOf (c), (int) engine.getSong().channels[(std::size_t) channel].instrument, [&] {
                return DetailView::instrumentCardFactory != nullptr
                           ? DetailView::instrumentCardFactory (engine, channel, engine.getSong().channels[(std::size_t) channel])
                           : nullptr;
            });
            if (c.custom != nullptr)
                c.knobs.clear();
            const auto w = c.folded              ? foldedWidth
                           : c.custom != nullptr   ? customWidth (*c.custom, height, x)
                           : c.knobs.size() > 2u ? std::max (instrumentWidth, 16 + (((int) c.knobs.size() + 1) / 2) * knobColumn)
                                                   : instrumentWidth;
            c.bounds = { x, top, w, height };
            x += w + cardGap();
            cards.push_back (std::move (c));
        }
        for (int i = 0; i < effectCount(); ++i)
        {
            const auto& fx = *effectAt (i);
            Card c;
            c.kind = Card::effect;
            c.index = i;
            c.tag = fx.instance.id;
            c.folded = folded.count (c.tag) > 0;
            const auto& info = effectInfo (fx.kind);
            if (fx.kind != EffectKind::plugin)
                for (int p = 0; p < std::min (maxEffectKnobs, (int) info.params.size()); ++p)
                    c.knobs.push_back (p);
            c.knobs.push_back (-1); // the slot's Mix
            c.custom = customFor (c.tag, (int) fx.kind, [&] {
                return DetailView::effectCardFactory != nullptr ? DetailView::effectCardFactory (engine, insert, i, fx) : nullptr;
            });
            if (c.custom != nullptr)
                c.knobs.clear();
            const auto columns = ((int) c.knobs.size() + 1) / 2;
            const auto w = c.folded                ? foldedWidth
                           : c.custom != nullptr      ? customWidth (*c.custom, height, x)
                           : fx.kind == EffectKind::plugin ? pluginWidth
                                                           : std::max (136, 16 + columns * knobColumn);
            c.bounds = { x, top, w, height };
            x += w + cardGap();
            cards.push_back (std::move (c));
        }
        if (insert >= 0 && effectCount() < maxEffectSlots)
        {
            Card c;
            c.kind = Card::add;
            c.bounds = { x, top, addWidth, height };
            x += addWidth + cardGap();
            cards.push_back (std::move (c));
        }
        placeCustomCards();
        for (const auto& c : cards) // (round 190ff: a frozen device's own body takes no clicks)
            if (c.custom != nullptr)
                c.custom->setEnabled (! cardLocked (c));
        contentWidth = x + scroll - area.getX();
        const auto overflow = contentWidth > area.getWidth();
        scrollBar.setVisible (overflow);
        scrollBar.setRangeLimits (0.0, (double) std::max (contentWidth, area.getWidth()), juce::dontSendNotification);
        scrollBar.setCurrentRange ((double) scroll, (double) area.getWidth(), juce::dontSendNotification);
        if (! overflow && scroll != 0)
        {
            scroll = 0;
            layoutCards();
        }
    }

    // Round 190rb (ASan, UI monkey seed 101: heap-use-after-free in the onPreferredWidthChanged lambda below, called from
    // a Rack card's own resize): a card asks for a new layout from inside its own code, and that layout can replace the
    // very card (its slot's kind changed) or drop it (its slot went); deleting it there pulled the card out from under
    // its running code. A card that goes leaves the chain at once and is deleted on the next message.
    void retire (std::unique_ptr<DetailCard> card)
    {
        if (card == nullptr)
            return;
        if (auto* extra = card->titleAccessory())
            cardHost.removeChildComponent (extra);
        cardHost.removeChildComponent (card.get());
        card->setVisible (false);
        retiredCards.push_back (std::move (card));
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<DeviceChain> (this)] {
            if (safe != nullptr)
                safe->retiredCards.clear();
        });
    }

    // the custom card of `key`, made once (again when the slot's kind changes); nullptr = an ordinary card
    DetailCard* customFor (std::uint64_t key, int kind, const std::function<std::unique_ptr<DetailCard>()>& make)
    {
        if (DetailView::effectCardFactory == nullptr && DetailView::instrumentCardFactory == nullptr)
            return nullptr;
        auto& entry = customs[key];
        if (entry.kind != kind)
        {
            entry.kind = kind;
            retire (std::move (entry.card)); // (round 190rb: not deleted here, see retire)
            entry.card = make();
            if (entry.card != nullptr)
            {
                cardHost.addChildComponent (*entry.card);
                entry.card->onPreferredWidthChanged = [this] {
                    layoutCards();
                    repaint();
                };
                if (auto* extra = entry.card->titleAccessory())
                    cardHost.addChildComponent (*extra);
            }
        }
        return entry.card.get();
    }
    int customWidth (DetailCard& card, int height, int x) const
    {
        // (x: the card's left with the scroll applied; the room is measured with the chain at its start, so the width
        // doesn't change while it scrolls)
        const auto area = chainArea();
        card.widthLimit = std::max (area.getWidth() / 2, area.getRight() - (x + scroll) - cardGap());
        return std::max (foldedWidth * 2, card.preferredWidth (height - titleHeight - 6));
    }
    void placeCustomCards()
    {
        std::set<DetailCard*> shown;
        const auto origin = chainArea().getPosition();
        for (const auto& c : cards)
            if (c.custom != nullptr)
            {
                shown.insert (c.custom);
                const auto besideTitle = c.custom->titleBarWidth() > 0; // (round 190nc: Live's inline chain devices)
                c.custom->setBounds ((besideTitle ? c.bounds : c.bounds.withTrimmedTop (titleHeight + 2)).reduced (2, 0).withTrimmedBottom (2) - origin);
                c.custom->setVisible (! c.folded);
                if (auto* extra = c.custom->titleAccessory())
                {
                    const auto w = c.custom->titleAccessoryWidth();
                    extra->setBounds (juce::Rectangle<int> (barRight (c) - 4 - titleButtonsWidth (c) - w, c.bounds.getY() + 2, w, titleHeight - 4) - origin);
                    extra->setVisible (! c.folded);
                }
            }
        for (auto it = customs.begin(); it != customs.end();) // (slots gone: their cards go too)
        {
            if (it->second.card != nullptr && shown.count (it->second.card.get()) == 0)
            {
                retire (std::move (it->second.card));
                it = customs.erase (it);
            }
            else
                ++it;
        }
    }

    juce::Rectangle<int> chainArea() const
    {
        return { contextWidth, 2, std::max (0, getWidth() - contextWidth - meterWidth - 4), std::max (0, getHeight() - 12) };
    }
    void setScroll (int s)
    {
        scroll = juce::jlimit (0, std::max (0, contentWidth - chainArea().getWidth()), s);
        layoutCards();
        repaint();
    }
    void scrollBarMoved (juce::ScrollBar*, double start) override { setScroll ((int) start); }

    // where a drop or a dragged card goes: before card `position` (cards.size() or the "+" card = after the last)
    int positionAt (int x) const
    {
        for (int i = 0; i < (int) cards.size(); ++i)
            if (x < cards[(std::size_t) i].bounds.getCentreX())
                return cards[(std::size_t) i].kind == Card::instrument ? i + 1 : i; // (nothing goes before the instrument)
        return (int) cards.size();
    }
    int dropLineX (int position) const
    {
        if (position >= 0 && position < (int) cards.size())
            return cards[(std::size_t) position].bounds.getX() - cardGap() / 2;
        return cards.empty() ? chainArea().getX() : cards.back().bounds.getRight() + cardGap() / 2;
    }
    // a card position as a place in the effect chain (0 = the first effect)
    int effectPositionFor (int position) const
    {
        int effects = 0;
        for (int i = 0; i < std::min (position, (int) cards.size()); ++i)
            effects += cards[(std::size_t) i].kind == Card::effect ? 1 : 0;
        return effects;
    }
    int nextFreeRow() const
    {
        const auto& song = engine.getSong();
        if (insert < 0 || insert >= (int) song.mixer.size())
            return 0;
        const auto& fx = song.mixer[(std::size_t) insert].effects;
        const auto rows = mixerops::effectRows (fx);
        for (int r = rows.empty() ? 0 : rows.back() + 1; r < maxEffectSlots; ++r)
            if (mixerops::effectAtRow (fx, r) < 0)
                return r;
        for (int r = 0; r < maxEffectSlots; ++r)
            if (mixerops::effectAtRow (fx, r) < 0)
                return r;
        return 0;
    }

    void dropEffect (EffectSlot fx, int position)
    {
        if (effectCount() >= maxEffectSlots)
        {
            ui::postHint ({ tr ("This insert has no free slot (10 effects)"), {} }, 3000);
            return;
        }
        int added = -1;
        engine.editSong ([t = insert, position, &added, fx = std::move (fx)] (Song& song) mutable {
            added = detailchain::insertEffectAt (song, t, std::move (fx), position);
            if (added >= 0)
                mixerops::linkAddedSidechain (song, t, added); // (as the Mixer's add: a sole sidechain sender feeds it)
        });
        if (added >= 0)
        {
            selectedTag = tagOf (added);
            refresh();
            if (const auto* fx2 = effectAt (added); fx2 != nullptr && fx2->kind == EffectKind::plugin)
                slotActions.toggleSlotEditor (added); // (a plugin opens its window when added, as in the Mixer)
        }
    }

    // --- selection and actions -----------------------------------------------------------------------------------
    int selectedCard() const
    {
        for (int i = 0; i < (int) cards.size(); ++i)
            if ((instrumentSelected && cards[(std::size_t) i].kind == Card::instrument)
                || (! instrumentSelected && selectedTag != 0 && cards[(std::size_t) i].tag == selectedTag))
                return i;
        return -1;
    }
    void select (int i)
    {
        if (i < 0 || i >= (int) cards.size() || cards[(std::size_t) i].kind == Card::add)
            return;
        const auto& c = cards[(std::size_t) i];
        instrumentSelected = c.kind == Card::instrument;
        selectedTag = c.tag;
        if (c.kind == Card::effect)
            slotActions.selectSlot (c.index);
        if (DetailView::extraSelectInMixer && hooks.selectInMixer != nullptr && insert >= 0)
            hooks.selectInMixer (insert);
        repaint();
    }
    void toggleOn (const Card& c)
    {
        if (c.kind == Card::instrument)
            engine.editSong ([ch = c.index] (Song& song) {
                if (ch >= 0 && ch < (int) song.channels.size())
                    song.channels[(std::size_t) ch].muted = ! song.channels[(std::size_t) ch].muted;
            });
        else
            engine.editSong ([t = insert, i = c.index] (Song& song) { // (the Mixer LED's edit)
                if (t >= 0 && t < (int) song.mixer.size() && i >= 0 && i < (int) song.mixer[(std::size_t) t].effects.size())
                    song.mixer[(std::size_t) t].effects[(std::size_t) i].enabled = ! song.mixer[(std::size_t) t].effects[(std::size_t) i].enabled;
            });
    }
    void toggleWindow (const Card& c)
    {
        if (c.kind == Card::instrument)
        {
            if (hooks.toggleChannelWindow != nullptr)
                hooks.toggleChannelWindow (c.index);
        }
        else if (c.kind == Card::effect)
            slotActions.toggleSlotEditor (c.index);
    }
    void showCardMenu (int i)
    {
        const auto& c = cards[(std::size_t) i];
        if (c.kind == Card::effect)
        {
            slotActions.showSlotMenuFor (c.index);
            return;
        }
        juce::PopupMenu menu;
        const auto key = keyOf (c);
        menu.addItem (folded.count (key) > 0 ? tr ("Unfold") : tr ("Fold"), [this, key] { toggleFold (key); });
        menu.addItem (tr ("Hot-swap in the Browser") + "\tQ", hooks.toggleHotSwap != nullptr, linkedTo (targetOf (c)), [this, c] { toggleSwap (targetOf (c)); });
        menu.addItem (tr ("Show or hide the window"), [this, c] { toggleWindow (c); });
        if (c.kind == Card::instrument) // round 190hr: Live's Group / Ungroup on the instrument, and a rack's Save Preset
        {
            const auto& song = engine.getSong();
            menu.addSeparator();
            menu.addItem (tr ("Group") + "\tCtrl+G", rack::canGroupChannel (song, c.index), false, [this, ch = c.index] { groupInstrument (ch); });
            menu.addItem (tr ("Ungroup") + "\tCtrl+Shift+G", rack::canUngroupChannel (song, c.index), false, [this, ch = c.index] { ungroupInstrument (ch); });
            if (c.index >= 0 && c.index < (int) song.channels.size() && rack::isRack (song.channels[(std::size_t) c.index]))
                menu.addItem (tr ("Save as Live rack preset (.adg)..."), [this, ch = c.index] { saveRackPreset (ch, -1); });
        }
        if (EffectsPanel::slotMenuForSnapshot != nullptr) // (shots: the menu as built)
        {
            *EffectsPanel::slotMenuForSnapshot = menu;
            return;
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // --- round 190ce: hot-swap, presets, copies ------------------------------------------------------------------------
    hotswap::Target targetOf (const Card& c) const
    {
        const auto& song = engine.getSong();
        return c.kind == Card::instrument ? hotswap::targetOfChannel (song, c.index)
               : c.kind == Card::effect   ? hotswap::targetOfEffect (song, insert, c.index)
                                          : hotswap::Target();
    }
    bool linkedTo (const hotswap::Target& t) const { return t.active() && hooks.hotSwapTarget != nullptr && hooks.hotSwapTarget() == t; }
    bool linkedTo (hotswap::Kind kind, std::uint64_t instance) const { return linkedTo (hotswap::Target { kind, instance }); }
    void toggleSwap (const hotswap::Target& t)
    {
        if (t.active() && hooks.toggleHotSwap != nullptr)
            hooks.toggleHotSwap (t);
        repaint();
    }
    // LYNX's own effects save their settings as presets (ui/PluginHeader.h); a plugin saves in its own window, a rack
    // or a Patcher in its editor.
    bool canSavePreset (int slot) const
    {
        const auto* fx = effectAt (slot);
        if (fx != nullptr && rack::isRack (*fx)) // (round 190hr: a rack saves as Live's rack preset, .adg)
            return true;
        return fx != nullptr && fx->kind != EffectKind::plugin && fx->kind != EffectKind::patcher && pluginheader::PresetBar (engine, insert, slot).hasLynxPresets();
    }
    void savePreset (int slot)
    {
        if (! canSavePreset (slot))
            return;
        if (const auto* fx = effectAt (slot); fx != nullptr && rack::isRack (*fx))
            return saveRackPreset (-1, slot);
        presetBar = std::make_unique<pluginheader::PresetBar> (engine, insert, slot);
        presetBar->savePreset (*this);
    }
    static std::optional<EffectSlot>& deviceClipboard()
    {
        static std::optional<EffectSlot> clip; // (one for every Device View of the session)
        return clip;
    }
    void copyDevice (int slot, bool cut)
    {
        engine.capturePluginStates(); // (a plugin's current settings go with the copy)
        const auto* fx = effectAt (slot);
        if (fx == nullptr)
            return;
        deviceClipboard() = *fx;
        if (cut)
        {
            slotActions.deleteSlot (slot);
            refresh();
        }
    }
    void pasteDevice (int position)
    {
        if (! deviceClipboard().has_value() || insert < 0)
            return;
        if (effectCount() >= maxEffectSlots)
        {
            ui::postHint ({ tr ("This insert has no free slot (10 effects)"), {} }, 3000);
            return;
        }
        int added = -1;
        engine.editSong ([t = insert, position, &added, fx = *deviceClipboard()] (Song& song) mutable { added = detailchain::pasteEffect (song, t, std::move (fx), position); });
        if (added >= 0)
        {
            selectedTag = tagOf (added);
            instrumentSelected = false;
        }
        refresh();
    }
    void duplicateDevice (int slot, int position)
    {
        if (effectCount() >= maxEffectSlots)
        {
            ui::postHint ({ tr ("This insert has no free slot (10 effects)"), {} }, 3000);
            return;
        }
        engine.capturePluginStates();
        int added = -1;
        engine.editSong ([t = insert, slot, position, &added] (Song& song) { added = detailchain::duplicateEffect (song, t, slot, position); });
        if (added >= 0)
        {
            selectedTag = tagOf (added);
            instrumentSelected = false;
        }
        refresh();
    }
    // round 190hr: Group (Ctrl+G) on the instrument card: the channel's instrument into an Instrument Rack (Live's
    // Group on an instrument [STR "Group"; Live 12 manual]); its automated plugin parameters become macros at their values
    void groupInstrument (int channel)
    {
        const auto& song = engine.getSong();
        if (! rack::canGroupChannel (song, channel))
            return;
        if (channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].freeze.on())
            return (void) refuseLocked (Card { Card::instrument, channel });
        engine.capturePluginStates();
        // the plugin's parameters the song automates or links: their values now (read before the edit)
        std::map<int, float> values;
        auto probe = song;
        probe.forEachTarget ([&] (AutomationTarget& t) {
            if (t.kind == AutomationTargetKind::channelParam && t.index == channel && values.count (t.param) == 0)
                values[t.param] = engine.currentTargetValue (t);
        });
        auto value = [values] (int, int param) {
            const auto it = values.find (param);
            return it != values.end() ? it->second : -1.0f;
        };
        auto trial = song;
        if (! rack::groupChannel (trial, channel, value))
        {
            ui::postNotice ({ tr ("This instrument can't go into a rack (more than 16 automated parameters, or their values are unknown)"), {} }, 3000);
            return;
        }
        engine.editSong ([channel, value] (Song& s) { rack::groupChannel (s, channel, value); });
        refresh();
    }
    void ungroupInstrument (int channel)
    {
        if (! rack::canUngroupChannel (engine.getSong(), channel))
            return;
        engine.capturePluginStates();
        engine.editSong ([channel] (Song& s) { rack::ungroupChannel (s, channel); });
        refresh();
    }
    // round 190hr: a rack's Save Preset (Live's .adg): channel `channel`'s Instrument Rack, or effect slot `slot`'s rack
    void saveRackPreset (int channel, int slot)
    {
        const auto& song = engine.getSong();
        juce::String name;
        RackLocator locate;
        if (slot >= 0)
        {
            const auto* fx = effectAt (slot);
            if (fx == nullptr || ! rack::isRack (*fx))
                return;
            name = utf8 (rack::rackOf (fx->patcher)->name);
            locate = [t = insert, slot] (Song& s) -> PatcherDoc* {
                if (t < 0 || t >= (int) s.mixer.size() || slot >= (int) s.mixer[(std::size_t) t].effects.size())
                    return nullptr;
                auto& x = s.mixer[(std::size_t) t].effects[(std::size_t) slot];
                return rack::isRack (x) ? &x.patcher : nullptr;
            };
        }
        else
        {
            if (channel < 0 || channel >= (int) song.channels.size() || ! rack::isRack (song.channels[(std::size_t) channel]))
                return;
            const auto& ch = song.channels[(std::size_t) channel];
            name = utf8 (! rack::rackOf (ch.patcher)->name.empty() ? rack::rackOf (ch.patcher)->name : ch.name);
            locate = [channel] (Song& s) -> PatcherDoc* {
                if (channel >= (int) s.channels.size() || ! rack::isRack (s.channels[(std::size_t) channel]))
                    return nullptr;
                return &s.channels[(std::size_t) channel].patcher;
            };
        }
        RackWindows::saveLivePreset (engine, std::move (locate), name);
    }
    void ungroupDevice (int slot)
    {
        const auto* fx = effectAt (slot);
        if (fx == nullptr || ! rack::isRack (*fx))
            return;
        const auto* r = rack::rackOf (fx->patcher);
        if (r == nullptr || r->kind != RackDoc::Kind::audioEffect || r->chains.size() != 1)
            return; // (as the slot menu's Ungroup: one-chain Audio Effect Racks)
        engine.capturePluginStates();
        engine.editSong ([t = insert, slot] (Song& song) { rack::ungroupSlot (song, t, slot); });
    }
    // the title bar's buttons at the right: [hot-swap] [save] [window]
    int titleButtonsWidth (const Card& c) const
    {
        const auto pitch = theme::livelook::active() ? 16 : 18;
        return pitch + (c.kind == Card::add ? 0 : pitch) + (c.kind == Card::effect && canSavePreset (c.index) ? pitch : 0);
    }
    juce::Rectangle<int> saveBounds (const Card& c) const
    {
        if (! (c.kind == Card::effect && canSavePreset (c.index)))
            return {};
        if (theme::livelook::active())
            return { barRight (c) - 32, c.bounds.getY() + 2, 13, 13 };
        return juce::Rectangle<int> (barRight (c) - 38, c.bounds.getY() + 3, 16, 14);
    }
    juce::Rectangle<int> swapBounds (const Card& c) const
    {
        if (c.kind == Card::add)
            return {};
        if (theme::livelook::active())
            return { barRight (c) - 32 - (saveBounds (c).isEmpty() ? 0 : 16), c.bounds.getY() + 2, 13, 13 };
        return { barRight (c) - 38 - (saveBounds (c).isEmpty() ? 0 : 18), c.bounds.getY() + 3, 16, 14 };
    }
    void showInsertMenu()
    {
        if (inserts.size() < 2)
            return;
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        for (const auto t : inserts)
            menu.addItem (insertName (song, t), true, t == insert, [this, t] {
                chosenInsert = t;
                refresh();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    juce::String cardName (const Card& c) const
    {
        const auto& song = engine.getSong();
        if (c.kind == Card::instrument)
            return c.index >= 0 && c.index < (int) song.channels.size() ? utf8 (song.channels[(std::size_t) c.index].name) : juce::String();
        if (const auto* fx = effectAt (c.index))
            return quickpick::slotName (engine, *fx);
        return {};
    }
    bool cardOn (const Card& c) const
    {
        const auto& song = engine.getSong();
        if (c.kind == Card::instrument)
            return c.index >= 0 && c.index < (int) song.channels.size() && ! song.channels[(std::size_t) c.index].muted;
        const auto* fx = effectAt (c.index);
        return fx != nullptr && fx->enabled;
    }

    // --- knobs ---------------------------------------------------------------------------------------------------
    // --- an engine parameter on the instrument card (round 190ko): the knob moves the parameter's 0..1 (its automation
    // value), shown in the engine's own units
    const EngineParam* engineParamOf (const Card& c, int param) const
    {
        const auto& song = engine.getSong();
        if (c.index < 0 || c.index >= (int) song.channels.size() || song.channels[(std::size_t) c.index].instrument != InstrumentKind::braid)
            return nullptr;
        const auto& list = braidEngineParams();
        return param >= 0 && param < (int) list.size() ? &list[(std::size_t) param] : nullptr;
    }
    juce::String engineKnobLabel (const Card& c, int param) const
    {
        const auto* p = engineParamOf (c, param);
        return p != nullptr ? utf8 (p->name) : juce::String();
    }
    ui::ValueSpec engineKnobSpec (const Card& c, int param) const
    {
        ui::ValueSpec spec;
        const auto* p = engineParamOf (c, param);
        if (p == nullptr)
            return spec;
        spec.name = cardName (c) + " - " + utf8 (p->name);
        spec.minimum = 0.0, spec.maximum = 1.0;
        const auto span = (double) (p->high - p->low);
        spec.defaultValue = span > 0.0 ? (braid::defaults()[(std::size_t) param] - p->low) / span : 0.0;
        if (p->stepped && span > 0.0)
            spec.interval = 1.0 / span;
        spec.toText = [param, lo = (double) p->low, span] (double v) { return utf8 (braid::paramText (param, lo + v * span)); };
        spec.target = AutomationTarget { AutomationTargetKind::channelParam, c.index, 0, param };
        return spec;
    }
    double engineKnobValue (const Card& c, int param) const
    {
        const auto* p = engineParamOf (c, param);
        if (p == nullptr)
            return 0.0;
        const auto& ch = engine.getSong().channels[(std::size_t) c.index];
        const auto raw = param < (int) ch.modelParams.size() ? (double) ch.modelParams[(std::size_t) param] : braid::defaults()[(std::size_t) param];
        const auto span = (double) (p->high - p->low);
        return span > 0.0 ? juce::jlimit (0.0, 1.0, (raw - p->low) / span) : 0.0;
    }
    void setEngineKnob (const Card& c, int param, double v)
    {
        const auto* p = engineParamOf (c, param);
        if (p == nullptr)
            return;
        auto raw = (double) p->low + juce::jlimit (0.0, 1.0, v) * (double) (p->high - p->low);
        if (p->stepped)
            raw = std::round (raw);
        engine.editSong ([ch = c.index, param, raw] (Song& s) {
            if (ch < 0 || ch >= (int) s.channels.size() || s.channels[(std::size_t) ch].instrument != InstrumentKind::braid)
                return;
            auto& mp = s.channels[(std::size_t) ch].modelParams;
            if ((int) mp.size() < braid::stateSize)
            {
                const auto d = braidDefaults();
                const auto have = mp.size();
                mp.resize ((std::size_t) braid::stateSize);
                std::copy (d.begin() + (std::ptrdiff_t) have, d.end(), mp.begin() + (std::ptrdiff_t) have);
            }
            mp[(std::size_t) param] = (float) raw;
        });
        repaint();
    }

    ui::ValueSpec knobSpec (const Card& c, int knob) const
    {
        if (c.kind == Card::effect)
            return slotActions.slotParamSpec (c.knobs[(std::size_t) knob], c.index);
        if (c.knobs[(std::size_t) knob] >= engineKnobBase)
            return engineKnobSpec (c, c.knobs[(std::size_t) knob] - engineKnobBase);
        const auto name = cardName (c);
        if (c.knobs[(std::size_t) knob] == 1)
        {
            auto spec = ui::panSpec (name + " - " + tr ("Pan"));
            spec.target = AutomationTarget { AutomationTargetKind::channelPan, c.index };
            return spec;
        }
        auto spec = ui::percentSpec (name + " - " + tr ("Volume"), 0.78); // (the Channel Rack's knob)
        spec.target = AutomationTarget { AutomationTargetKind::channelVolume, c.index };
        return spec;
    }
    double knobValue (const Card& c, int knob) const
    {
        if (c.kind == Card::effect)
            return shownValue (c, knob, slotActions.slotParamValue (c.knobs[(std::size_t) knob], c.index));
        if (c.knobs[(std::size_t) knob] >= engineKnobBase)
            return engineKnobValue (c, c.knobs[(std::size_t) knob] - engineKnobBase);
        const auto& song = engine.getSong();
        if (c.index < 0 || c.index >= (int) song.channels.size())
            return 0.0;
        const auto& ch = song.channels[(std::size_t) c.index];
        return shownValue (c, knob, c.knobs[(std::size_t) knob] == 1 ? ch.pan : ch.volume);
    }
    // Hotfix 0.1.31.1 (kainproctor, Discord: "tweaking any knob inside any plugin isn't reflected here"): while the song
    // plays, a knob whose control automation drives shows the automation's value (core/AutomationDisplay), as the
    // plugin's own window, the Mixer and FL do; while a mouse button is down the Song's value (the hand wins). `base`:
    // the Song's value in the knob's own range.
    double shownValue (const Card& c, int knob, double base) const
    {
        if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            return base;
        const auto& shown = engine.getAutomationDisplay();
        if (! shown.any())
            return base;
        const auto k = c.knobs[(std::size_t) knob];
        if (c.kind == Card::effect)
        {
            const auto t = k < 0 ? AutomationTarget { AutomationTargetKind::effectMix, insert, c.index, 0 }
                                 : AutomationTarget { AutomationTargetKind::effectParam, insert, c.index, k };
            return (double) shown.shown (t, (float) base);
        }
        if (k == 1) // (pan: the target's 0..1 is the knob's -1..1)
            return (double) shown.shown ({ AutomationTargetKind::channelPan, c.index, 0, 0 }, (float) ((base + 1.0) * 0.5)) * 2.0 - 1.0;
        return (double) shown.shown ({ AutomationTargetKind::channelVolume, c.index, 0, 0 }, (float) base);
    }
    // (the timer's test: a shown value of this chain changed since the last repaint)
    bool automationMoved()
    {
        const auto& shown = engine.getAutomationDisplay();
        if (shown.getGeneration() == shownGeneration)
            return false;
        shownGeneration = shown.getGeneration();
        for (const auto& t : shown.automatedTargets())
            if (((t.kind == AutomationTargetKind::effectParam || t.kind == AutomationTargetKind::effectMix) && t.index == insert && insert >= 0)
                || ((t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan) && t.index == channel && channel >= 0))
                return true;
        return false;
    }
public:
    double knobValueForCheck (int card, int knob) const
    {
        return card >= 0 && card < (int) cards.size() && knob >= 0 && knob < (int) cards[(std::size_t) card].knobs.size()
                   ? knobValue (cards[(std::size_t) card], knob) : -100.0;
    }
private:
    void setKnob (const Card& c, int knob, double v)
    {
        if (c.kind == Card::effect)
        {
            slotActions.setSlotParam (c.knobs[(std::size_t) knob], c.index, v);
            return;
        }
        if (c.knobs[(std::size_t) knob] >= engineKnobBase)
        {
            setEngineKnob (c, c.knobs[(std::size_t) knob] - engineKnobBase, v);
            return;
        }
        const auto pan = c.knobs[(std::size_t) knob] == 1;
        engine.editLevels ([ch = c.index, pan, v = (float) v] (Song& song) {
            if (ch < 0 || ch >= (int) song.channels.size())
                return;
            if (pan)
                song.channels[(std::size_t) ch].pan = juce::jlimit (-1.0f, 1.0f, v);
            else
                song.channels[(std::size_t) ch].volume = juce::jlimit (0.0f, 1.0f, v);
        });
        repaint();
    }
    juce::Rectangle<int> knobBounds (const Card& c, int knob) const
    {
        if (c.folded || knob < 0 || knob >= (int) c.knobs.size())
            return {};
        const auto body = c.bounds.withTrimmedTop (titleHeight + 6);
        if (c.kind == Card::instrument && c.knobs.size() <= 2u)
            return { c.bounds.getX() + 16 + knob * knobColumn, body.getY() + 92, knobColumn - 8, knobRow - 6 };
        if (c.kind == Card::effect)
            if (const auto* fx = effectAt (c.index); fx != nullptr && fx->kind == EffectKind::plugin)
                return { c.bounds.getX() + 8, body.getY() + 92, knobColumn - 8, knobRow - 6 };
        const auto column = knob / 2, row = knob % 2;
        return { c.bounds.getX() + 8 + column * knobColumn, body.getY() + row * knobRow, knobColumn - 8, knobRow - 6 };
    }

    // --- hit testing ---------------------------------------------------------------------------------------------
    juce::Rectangle<int> pickerBounds() const { return { 8, 40, contextWidth - 16, 20 }; }
    // round 190nc (DL-95): a rack with its chain's devices inline has its title bar over its own part only
    static int barRight (const Card& c)
    {
        if (! c.folded && c.custom != nullptr)
            if (const auto w = c.custom->titleBarWidth(); w > 0)
                return c.bounds.getX() + std::min (w, c.bounds.getWidth());
        return c.bounds.getRight();
    }
    static juce::Rectangle<int> titleBounds (const Card& c) { return c.bounds.withHeight (titleHeight).withRight (barRight (c)); }
    // Live's look (round 190jn, CL-21): the 13 px power disc 3 px in, 2 px down; the title bar's round 13 px wells on a
    // 16 px pitch, the last 3 px from the right edge (live-session.png Utility: disc x 1078..1090, wells 1166 / 1182 / 1198)
    static juce::Rectangle<int> ledBounds (const Card& c)
    {
        if (theme::livelook::active())
            return { c.bounds.getX() + 3, c.bounds.getY() + 2, 13, 13 };
        return { c.bounds.getX() + 5, c.bounds.getY() + 5, 11, 11 };
    }
    static juce::Rectangle<int> windowBounds (const Card& c)
    {
        if (theme::livelook::active())
            return { barRight (c) - 16, c.bounds.getY() + 2, 13, 13 };
        return { barRight (c) - 20, c.bounds.getY() + 3, 16, 14 };
    }

    Hit hitAt (juce::Point<int> p) const
    {
        Hit h;
        if (inserts.size() > 1 && pickerBounds().contains (p))
        {
            h.part = Part::picker;
            return h;
        }
        if (! chainArea().contains (p))
            return h;
        for (int i = 0; i < (int) cards.size(); ++i)
        {
            const auto& c = cards[(std::size_t) i];
            if (! c.bounds.contains (p))
                continue;
            h.card = i;
            if (c.kind == Card::add)
                return h;
            if (ledBounds (c).expanded (2).contains (p))
                h.part = Part::led;
            else if (c.folded)
                h.part = Part::folded;
            else if (windowBounds (c).contains (p))
                h.part = Part::window;
            else if (swapBounds (c).contains (p))
                h.part = Part::swap;
            else if (saveBounds (c).contains (p))
                h.part = Part::save;
            else if (titleBounds (c).contains (p))
                h.part = Part::title;
            else
            {
                h.part = Part::body;
                for (int k = 0; k < (int) c.knobs.size(); ++k)
                    if (knobBounds (c, k).contains (p))
                    {
                        h.part = Part::knob;
                        h.knob = k;
                    }
            }
            return h;
        }
        return h;
    }

    // --- painting ------------------------------------------------------------------------------------------------
    void paintContext (juce::Graphics& g)
    {
        const auto& song = engine.getSong();
        auto r = juce::Rectangle<int> (0, 0, contextWidth - 6, getHeight());
        const auto live = theme::livelook::active(); // (round 190jn: Live's greys; Live has no such panel, LYNX keeps it)
        g.setColour (live ? theme::livelook::surface : colour::bg2);
        g.fillRect (r);
        g.setColour (live ? theme::livelook::separator : colour::border);
        g.fillRect (r.getRight(), 0, 1, getHeight());
        r = r.reduced (8, 6);
        g.setColour (live ? theme::livelook::text : colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (track >= 0 ? trackName (song, track) : juce::String(), r.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (tr ("Insert"), r.removeFromTop (14), juce::Justification::centredLeft, true);
        const auto box = pickerBounds();
        const bool choice = inserts.size() > 1;
        if (choice)
        {
            g.setColour (live ? (hover.part == Part::picker ? theme::livelook::raised : theme::livelook::well) : hover.part == Part::picker ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
        }
        g.setColour (insert >= 0 ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::small));
        g.drawText ((insert >= 0 ? insertName (song, insert) : tr ("None")) + (choice ? juce::String::fromUTF8 (" \xe2\x96\xbe") : juce::String()),
                    box.reduced (choice ? 6 : 0, 0), juce::Justification::centredLeft, true);
        r = r.withTop (box.getBottom() + 8);
        if (channel >= 0 && channel < (int) song.channels.size()) // (round 190rb, ASan / monkey seed 104: a step behind the song)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (tr ("Channel"), r.removeFromTop (14), juce::Justification::centredLeft, true);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            g.drawText (utf8 (song.channels[(std::size_t) channel].name), r.removeFromTop (16), juce::Justification::centredLeft, true);
        }
        else if (inserts.size() > 1)
        {
            g.setColour (colour::textSecondary); // (round 190oj CS-14: 2.4:1, and squeezed into two lines)
            g.setFont (font (type::caption));
            g.drawFittedText (tr ("This track plays through {0} inserts", { juce::String ((int) inserts.size()) }), r.removeFromTop (42),
                              juce::Justification::topLeft, 3, 1.0f);
        }
    }

    void paintCard (juce::Graphics& g, int i)
    {
        const auto& c = cards[(std::size_t) i];
        if (! c.bounds.intersects (chainArea()))
            return;
        if (theme::livelook::active())
        {
            paintCardLive (g, i);
            return;
        }
        if (c.kind == Card::add)
        {
            const bool hot = hover.card == i;
            g.setColour (hot ? colour::bg3 : colour::bg2);
            g.fillRect (c.bounds.reduced (0, 1));
            g.setColour (colour::border);
            g.drawRect (c.bounds.reduced (0, 1));
            g.setColour (hot ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (22.0f));
            g.drawText ("+", c.bounds, juce::Justification::centred, false);
            return;
        }
        const bool selected = i == selectedCard();
        const bool on = cardOn (c);
        const auto name = cardName (c);
        g.setColour (c.folded ? colour::bg3 : colour::bg2);
        g.fillRect (c.bounds);
        // the title bar: LED, name, window button
        if (! c.folded)
        {
            g.setColour (colour::bg3);
            g.fillRect (titleBounds (c));
        }
        g.setColour (on ? colour::accent : colour::bg5);
        g.fillEllipse (ledBounds (c).toFloat());
        if (hover.card == i && hover.part == Part::led)
        {
            g.setColour (colour::textPrimary.withAlpha (0.6f));
            g.drawEllipse (ledBounds (c).toFloat().expanded (1.0f), 1.0f);
        }
        g.setColour (on ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::caption, true));
        if (c.folded)
            drawVerticalText (g, name, c.bounds.withTrimmedTop (titleHeight));
        else
        {
            g.drawText (name, titleBounds (c).withTrimmedLeft (21).withTrimmedRight (4 + titleButtonsWidth (c) + (c.custom != nullptr ? c.custom->titleAccessoryWidth() : 0)),
                        juce::Justification::centredLeft, true);
            drawWindowGlyph (g, windowBounds (c), hover.card == i && hover.part == Part::window);
            drawSwapGlyph (g, swapBounds (c), hover.card == i && hover.part == Part::swap, linkedTo (targetOf (c)));
            if (const auto save = saveBounds (c); ! save.isEmpty())
                drawSaveGlyph (g, save, hover.card == i && hover.part == Part::save);
            if (c.custom != nullptr)
            {
                // (its own component draws the body)
            }
            else if (on)
                paintBody (g, i);
            else // switched off: its controls dim, as the reference shows [SHOT d02]
            {
                g.beginTransparencyLayer (0.45f);
                paintBody (g, i);
                g.endTransparencyLayer();
            }
        }
        g.setColour (selected ? colour::accent : colour::border);
        g.drawRect (c.bounds, selected ? 2 : 1);
    }

    // Round 190jn (design190ia CL-21): a card in Live 12.4's device chrome (live-session.png Utility 1075..1213 × 807..995):
    // body #363636 with 3 px corners on the #2A2A2A chain, a 17 px #464646 title bar (selected: #637E86 with #070707 text),
    // the 13 px power disc (#FFAD56 on, #1E1E1E off), the name in #B5B5B5, round 13 px wells for hot-swap / save / window;
    // no outline. A folded card is the title bar turned on its side. The body: Live's open-ring knobs (paintBodyLive).
    void paintCardLive (juce::Graphics& g, int i)
    {
        namespace ll = theme::livelook;
        const auto& c = cards[(std::size_t) i];
        if (c.kind == Card::add)
        {
            const bool hot = hover.card == i;
            g.setColour (hot ? ll::raised : ll::surface);
            g.fillRoundedRectangle (c.bounds.reduced (0, 1).toFloat(), 3.0f);
            g.setColour (hot ? colour::textPrimary : ll::text);
            g.setFont (font (22.0f));
            g.drawText ("+", c.bounds, juce::Justification::centred, false);
            return;
        }
        const bool selected = i == selectedCard();
        const bool on = cardOn (c);
        const auto name = cardName (c);
        const auto box = c.bounds.toFloat();
        const auto bar = selected ? ll::selectionUnfocused : ll::raised;
        g.setColour (c.folded ? bar : ll::surface);
        g.fillRoundedRectangle (box, 3.0f);
        if (! c.folded)
        {
            juce::Path title;
            title.addRoundedRectangle (box.getX(), box.getY(), (float) titleBounds (c).getWidth(), (float) (int) titleHeight, 3.0f, 3.0f, true, true, false, false);
            g.setColour (bar);
            g.fillPath (title);
        }
        ll::drawPower (g, ledBounds (c).toFloat(), on);
        if (hover.card == i && hover.part == Part::led)
        {
            g.setColour (colour::textPrimary.withAlpha (0.6f));
            g.drawEllipse (ledBounds (c).toFloat().expanded (1.0f), 1.0f);
        }
        g.setColour (selected ? ll::ink : ll::text);
        g.setFont (font (type::small));
        if (c.folded)
            drawVerticalText (g, name, c.bounds.withTrimmedTop (titleHeight));
        else
        {
            g.drawText (name, titleBounds (c).withTrimmedLeft (20).withTrimmedRight (4 + titleButtonsWidth (c) + (c.custom != nullptr ? c.custom->titleAccessoryWidth() : 0)),
                        juce::Justification::centredLeft, true);
            drawLiveWellGlyph (g, windowBounds (c), 0, hover.card == i && hover.part == Part::window, false);
            drawLiveWellGlyph (g, swapBounds (c), 1, hover.card == i && hover.part == Part::swap, linkedTo (targetOf (c)));
            if (const auto save = saveBounds (c); ! save.isEmpty())
                drawLiveWellGlyph (g, save, 2, hover.card == i && hover.part == Part::save, false);
            if (c.custom != nullptr)
            {
                // (its own component draws the body)
            }
            else if (on)
                paintBodyLive (g, i);
            else // switched off: its controls dim (Live 12.4: round 183a's d02-utility-off.png)
            {
                g.beginTransparencyLayer (0.45f);
                paintBodyLive (g, i);
                g.endTransparencyLayer();
            }
        }
    }

    // Live's body: label and value in #B5B5B5 (the UI font at 12 px: Live's cap height 7 px), the 25 px open-ring knob
    void paintBodyLive (juce::Graphics& g, int i)
    {
        namespace ll = theme::livelook;
        const auto& c = cards[(std::size_t) i];
        const auto body = c.bounds.withTrimmedTop (titleHeight + 6).reduced (8, 0);
        const auto* fx = c.kind == Card::effect ? effectAt (c.index) : nullptr;
        if (c.kind == Card::instrument || (fx != nullptr && fx->kind == EffectKind::plugin))
            paintPicture (g, c, body.withHeight (80));
        for (int k = 0; k < (int) c.knobs.size(); ++k)
        {
            const auto r = knobBounds (c, k);
            if (r.isEmpty())
                continue;
            const auto spec = knobSpec (c, k);
            const auto value = knobValue (c, k);
            const auto label = c.kind == Card::effect && c.knobs[(std::size_t) k] < 0 ? (fx != nullptr && hasOwnMix (fx->kind) ? tr ("Slot mix") : tr ("Mix"))
                               : c.kind == Card::effect && fx != nullptr                ? knobParamName (*fx, c.knobs[(std::size_t) k])
                               : c.knobs[(std::size_t) k] == 1                          ? tr ("Pan")
                                                                                        : tr ("Volume");
            auto rr = r;
            const auto hot = (hover.card == i && hover.knob == k) || (knobCard == i && knobDrag == k);
            g.setColour (hot ? colour::textPrimary : ll::text);
            g.setFont (font (type::small));
            g.drawText (label, rr.removeFromTop (14), juce::Justification::centred, true);
            const auto knob = rr.removeFromTop (knobSize).toFloat().withSizeKeepingCentre (27.0f, 27.0f);
            const auto range = spec.maximum - spec.minimum;
            const auto v01 = range > 0.0 ? (float) ((value - spec.minimum) / range) : 0.0f;
            const auto home = range > 0.0 ? (float) ((juce::jlimit (spec.minimum, spec.maximum, spec.defaultValue) - spec.minimum) / range) : 0.0f;
            ll::drawKnob (g, knob, juce::jlimit (0.0f, 1.0f, v01), home);
            if (spec.target) // round 190lx (DL-59, capture 190lx L04): Live's red LED on an automated device knob
            {
                const auto& automated = engine.getAutomationDisplay().automatedTargets();
                if (std::find (automated.begin(), automated.end(), *spec.target) != automated.end())
                    theme::drawLiveKnobAutomationLed (g, knob);
            }
            g.setColour (hot ? colour::textPrimary : ll::text);
            g.drawText (spec.text (value), rr.removeFromTop (14), juce::Justification::centred, true);
        }
    }

    void paintBody (juce::Graphics& g, int i)
    {
        const auto& c = cards[(std::size_t) i];
        const auto body = c.bounds.withTrimmedTop (titleHeight + 6).reduced (8, 0);
        const auto* fx = c.kind == Card::effect ? effectAt (c.index) : nullptr;
        if ((c.kind == Card::instrument && c.knobs.size() <= 2u) || (fx != nullptr && fx->kind == EffectKind::plugin))
            paintPicture (g, c, body.withHeight (80));
        for (int k = 0; k < (int) c.knobs.size(); ++k)
        {
            const auto r = knobBounds (c, k);
            if (r.isEmpty())
                continue;
            const auto spec = knobSpec (c, k);
            const auto value = knobValue (c, k);
            const auto label = c.kind == Card::effect && c.knobs[(std::size_t) k] < 0 ? (fx != nullptr && hasOwnMix (fx->kind) ? tr ("Slot mix") : tr ("Mix"))
                               : c.kind == Card::effect && fx != nullptr                ? knobParamName (*fx, c.knobs[(std::size_t) k])
                               : c.knobs[(std::size_t) k] >= engineKnobBase             ? engineKnobLabel (c, c.knobs[(std::size_t) k] - engineKnobBase)
                               : c.knobs[(std::size_t) k] == 1                          ? tr ("Pan")
                                                                                        : tr ("Volume");
            auto rr = r;
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (label, rr.removeFromTop (14), juce::Justification::centred, true);
            const auto knob = rr.removeFromTop (knobSize).toFloat().withSizeKeepingCentre ((float) knobSize, (float) knobSize);
            const auto range = spec.maximum - spec.minimum;
            const auto v01 = range > 0.0 ? (float) ((value - spec.minimum) / range) : 0.0f;
            drawKnob (g, knob, juce::jlimit (0.0f, 1.0f, v01), spec.minimum < 0.0, (hover.card == i && hover.knob == k) || (knobCard == i && knobDrag == k) ? colour::accent : colour::knobArc,
                      hover.card == i && hover.knob == k);
            g.setColour (colour::textPrimary);
            g.drawText (spec.text (value), rr.removeFromTop (14), juce::Justification::centred, true);
        }
    }

    // The Plugin picker's key of a LYNX generator kind ("lynx:gen:<English name>"), from its entries once per run; ""
    // for a hosted plugin or a kind the picker doesn't list.
    std::string generatorKeyOf (InstrumentKind kind)
    {
        static std::map<int, std::string> keys;
        static bool filled = false;
        if (! filled)
        {
            filled = true;
            for (const auto& e : quickpick::generatorEntries (engine))
                if (e.folder == 0 && e.key.rfind ("lynx:gen:", 0) == 0)
                    keys.emplace ((int) e.kind, e.key); // (the first entry of a kind)
        }
        if (kind == InstrumentKind::plugin)
            return {};
        const auto it = keys.find ((int) kind);
        return it != keys.end() ? it->second : std::string();
    }

    // Round 190rb (crash reports 52879e1b / a49ceeeb, 0.1.29: paintBody read effectInfo().params past its end): the cards
    // can be a step behind the song until they are laid out again (an effect just replaced by a plugin, which has no plain
    // parameters, or by another effect): a knob's parameter that the slot's effect does not have has no name
    static juce::String knobParamName (const EffectSlot& fx, int param)
    {
        const auto& params = effectInfo (fx.kind).params;
        return param >= 0 && param < (int) params.size() ? utf8 (params[(std::size_t) param].name) : juce::String();
    }

    // the effect has a parameter called Mix of its own: the slot's dry/wet is then labelled "Slot mix"
    static bool hasOwnMix (EffectKind kind)
    {
        for (const auto& p : effectInfo (kind).params)
            if (juce::String (p.name).equalsIgnoreCase ("Mix"))
                return true;
        return false;
    }

    // the plugin's picture (Plugin picker thumbnails), else a tile with its name
    void paintPicture (juce::Graphics& g, const Card& c, juce::Rectangle<int> area)
    {
        const auto& song = engine.getSong();
        std::string key;
        if (c.kind == Card::instrument && c.index >= 0 && c.index < (int) song.channels.size())
            key = generatorKeyOf (song.channels[(std::size_t) c.index].instrument);
        juce::Image picture;
        if (! key.empty())
            if (const auto file = pluginthumbs::fileFor (key, true); file.existsAsFile())
                picture = juce::ImageCache::getFromFile (file);
        g.setColour (theme::livelook::active() ? theme::livelook::well : colour::bg0);
        g.fillRect (area);
        if (picture.isValid())
        {
            g.drawImageWithin (picture, area.getX(), area.getY(), area.getWidth(), area.getHeight(), juce::RectanglePlacement::centred);
            return;
        }
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small, true));
        g.drawFittedText (cardName (c), area.reduced (6), juce::Justification::centred, 2);
    }

    void paintMeter (juce::Graphics& g)
    {
        const auto r = meterBounds();
        g.setColour (theme::livelook::active() ? theme::livelook::meterWell : colour::bg0);
        g.fillRect (r);
        for (int side = 0; side < 2; ++side)
        {
            const auto bar = juce::Rectangle<int> (r.getX() + 3 + side * 8, r.getY() + 2, 6, r.getHeight() - 4);
            const auto& m = levels[(std::size_t) side];
            const auto db = juce::Decibels::gainToDecibels (m.level, -60.0f);
            const auto h = juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f));
            g.setColour (m.level >= 1.0f ? colour::meterClip : db > -6.0f ? colour::meterMid : colour::meterLow);
            g.fillRect (bar.withTop (bar.getBottom() - h));
        }
    }
    juce::Rectangle<int> meterBounds() const { return { getWidth() - meterWidth, 4, meterWidth - 4, std::max (0, getHeight() - 16) }; }

    // Round 190lr (DL-87, Live manual 23.2 "Devices in Live's tracks have input and output level meters"; capture
    // live-device-meters.png): in Live's look a slim meter stands in the gap after each device, the level that leaves it
    // (after the instrument: what enters the first effect). The slots' peaks come from the audio thread only while this
    // view shows (RackRenderer's levelWanted / slotLevels, the editors' own level taps); mono (the louder side).
    bool deviceMeters() const { return theme::livelook::active(); }
    int cardGap() const { return deviceMeters() ? deviceMeterGap : gap; }
    static constexpr int deviceMeterGap = 9; // (Live, L28: 9 px between two devices, the meter in its middle)
    std::size_t slotKey (int slot) const { return (std::size_t) juce::jmax (0, insert) * (std::size_t) maxEffectSlots + (std::size_t) slot; }
    void watchDevices (bool on)
    {
        std::vector<std::size_t> want;
        if (on && deviceMeters() && insert >= 0)
            for (int i = 0; i < effectCount() && i < maxEffectSlots; ++i)
                want.push_back (slotKey (i));
        if (want == watched)
            return;
        if (auto* times = engine.getProcessTimes())
        {
            for (const auto k : watched)
                if (k < times->levelWanted.size())
                    times->levelWanted[k].fetch_sub (1, std::memory_order_relaxed);
            for (const auto k : want)
                if (k < times->levelWanted.size())
                    times->levelWanted[k].fetch_add (1, std::memory_order_relaxed);
            watched = std::move (want);
        }
        else
            watched.clear();
    }
    // the meter after card `i` (empty: none): Live's 2 + 1 + 2 px well in the middle of the gap, centred on the device's
    // height (L28: 108 of 188 px; its top 11 px cut off by a line, as Live's)
    juce::Rectangle<int> deviceMeterBounds (int i) const
    {
        if (! deviceMeters() || i < 0 || i >= (int) cards.size())
            return {};
        const auto& c = cards[(std::size_t) i];
        if (c.kind == Card::add || (c.kind == Card::instrument && effectCount() == 0))
            return {};
        const auto inset = juce::roundToInt ((float) c.bounds.getHeight() * 0.21f);
        return { c.bounds.getRight() + (deviceMeterGap - 5) / 2 + 1, c.bounds.getY() + inset, 5, juce::jmax (0, c.bounds.getHeight() - 2 * inset) };
    }
    // its level, one side: an effect's output, the instrument's = the first effect's input
    float takeDeviceLevel (const Card& c, int side)
    {
        auto* times = engine.getProcessTimes();
        if (times == nullptr)
            return 0.0f;
        auto& sides = c.kind == Card::effect ? times->slotOutputSides : times->slotInputSides;
        const auto k = (c.kind == Card::effect ? slotKey (c.index) : slotKey (0)) * 2 + (std::size_t) side;
        return k < sides.size() ? sides[k].exchange (0.0f, std::memory_order_relaxed) : 0.0f;
    }
    void tickDeviceMeters()
    {
        watchDevices (true);
        if (! deviceMeters())
            return;
        if (deviceLevels.size() != cards.size() * 2)
            deviceLevels.assign (cards.size() * 2, {});
        for (int i = 0; i < (int) cards.size(); ++i)
        {
            const auto r = deviceMeterBounds (i);
            if (r.isEmpty())
                continue;
            for (int side = 0; side < 2; ++side)
            {
                auto& m = deviceLevels[(std::size_t) i * 2 + (std::size_t) side];
                const auto before = m.level;
                const auto beforePeak = m.peak;
                m.tick (takeDeviceLevel (cards[(std::size_t) i], side));
                if (std::abs (m.level - before) > 1e-4f || std::abs (m.peak - beforePeak) > 1e-4f)
                    repaint (r.expanded (1));
            }
        }
    }
    void paintDeviceMeters (juce::Graphics& g)
    {
        if (! deviceMeters())
            return;
        namespace lv = theme::livelook;
        for (int i = 0; i < (int) cards.size(); ++i)
        {
            const auto r = deviceMeterBounds (i);
            if (r.isEmpty() || ! g.clipRegionIntersects (r))
                continue;
            // (Live, capture L28: two 2 px wells #181818 with the window between, a 1 px #363636 line 11 px under the top;
            // the level #00F758, the peak hold a 1 px line)
            const auto body = r.withTrimmedTop (12);
            for (int side = 0; side < 2; ++side)
            {
                const auto bar = juce::Rectangle<int> (r.getX() + side * 3, r.getY(), 2, r.getHeight());
                g.setColour (lv::meterWell);
                g.fillRect (bar);
                g.setColour (lv::surface);
                g.fillRect (bar.getX(), r.getY() + 11, 2, 1);
                const auto& m = (std::size_t) i * 2 + (std::size_t) side < deviceLevels.size() ? deviceLevels[(std::size_t) i * 2 + (std::size_t) side]
                                                                                               : meters::PeakHold {};
                const auto yOf = [&body] (float gain) {
                    const auto db = juce::Decibels::gainToDecibels (gain, -60.0f);
                    return body.getBottom() - juce::roundToInt ((float) body.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f));
                };
                if (m.level > 0.0f)
                {
                    g.setColour (juce::Colour (0xff00f758)); // (Live's meter green)
                    g.fillRect (bar.withTop (yOf (m.level)).withBottom (body.getBottom()));
                }
                if (m.peak > 0.001f)
                {
                    g.setColour (juce::Colour (0xff00f758));
                    g.fillRect (bar.getX(), juce::jmin (body.getBottom() - 1, yOf (m.peak)), 2, 1);
                }
                if (m.clipped)
                {
                    g.setColour (juce::Colour (0xffff3a3a));
                    g.fillRect (bar.withHeight (11));
                }
            }
        }
    }
    std::vector<std::size_t> watched;
    std::vector<meters::PeakHold> deviceLevels;
public:
    void deviceLevelsForSnapshot (float left, float right)
    {
        deviceLevels.assign (cards.size() * 2, {});
        for (std::size_t i = 0; i < deviceLevels.size(); ++i)
        {
            deviceLevels[i].level = i % 2 == 0 ? left : right;
            deviceLevels[i].peak = juce::jmin (1.0f, (i % 2 == 0 ? left : right) * 1.25f);
        }
        repaint();
    }
private:

    void timerCallback() override
    {
        tickDeviceMeters(); // round 190lr (DL-87)
        if (automationMoved()) // hotfix 0.1.31.1: the knobs follow their automation (only while the chain shows)
            repaint();
        if (insert < 0 || hooks.levelOf == nullptr)
            return;
        bool moved = false;
        for (int side = 0; side < 2; ++side)
        {
            auto& m = levels[(std::size_t) side];
            const auto before = m.level;
            m.tick (hooks.levelOf (insert, side));
            moved = moved || std::abs (m.level - before) > 1e-4f;
        }
        if (moved)
            repaint (meterBounds());
    }

    AudioEngine& engine;
    Hooks& hooks;
    EffectsPanel slotActions; // never shown: the Mixer slot's own menus, windows and edits, on this view's insert
    std::unique_ptr<pluginheader::PresetBar> presetBar; // (round 190ce: the Save preset dialog of the card last asked)
    juce::Component cardHost;  // the chain area: custom card bodies live here (clipped to it)
    std::map<std::uint64_t, CustomEntry> customs;
    std::vector<std::unique_ptr<DetailCard>> retiredCards; // (round 190rb: replaced or dropped cards, until the next message)
    juce::ScrollBar scrollBar { false };
    std::unique_ptr<ui::TimerWhileShowing> showingTimer;
    std::uint32_t shownGeneration = 0; // (hotfix 0.1.31.1: the AutomationDisplay generation last drawn)
    std::array<meters::PeakHold, 2> levels {};

    int track = -1, insert = -1, channel = -1, chosenInsert = -1, actionsInsert = -1;
    std::vector<int> inserts;
    std::vector<Card> cards;
    std::set<std::uint64_t> folded; // UI only, not saved (D2a)
    std::uint64_t selectedTag = 0;
    bool instrumentSelected = false;
    int scroll = 0, contentWidth = 0;
    Hit hover;
    ui::ValueDrag drag;
    int knobDrag = -1, knobCard = -1;
    int dragCard = -1, dropBefore = -1;
    bool dragging = false;
};

// ---------------------------------------------------------------------------------------------------------------------
// Slice 2b: the Clip page's left column, the reference's Clip / Launch / Notes panels [SHOT d08, m3-live-clipview2] over
// LYNX's existing model: the grid clip's offset and length, its slot's loop and launch settings, and the pattern's
// notes through the Piano roll's own note tools. No new model: Signature is the project's, Scale is the project key
// (core/ProjectKey.h; round 190o), Groove has no LYNX model (listed as missing in REA_rodada183a.md).
// The clip's colour and activator in one square, as the reference's clip title bar [LIVE 12.4 Clip View] (round 190s,
// GAPMAP-0126 D15): filled = the clip plays, hollow = deactivated. Click switches it; right-click picks the colour.
// Round 190kc: the Clip box's Commit button. Live 12.4 draws it as a 13 px round well with an arrow beside the Groove
// label (shots/ref/live-clipview-midi-groove.png x 156..168 y 797..809; dim while the clip has no groove); the classic
// look keeps a text button.
class GrooveCommitButton final : public juce::TextButton
{
public:
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (! theme::livelook::active())
        {
            juce::TextButton::paintButton (g, over, down);
            return;
        }
        namespace lv = theme::livelook;
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (13.0f, 13.0f);
        const auto ink = lv::drawWell (g, r, down, true);
        g.setColour (isEnabled() ? ink : ink.withAlpha (0.35f));
        const auto cx = std::round (r.getCentreX()), cy = std::round (r.getCentreY());
        g.fillRect (cx - 3.0f, cy, 6.0f, 1.0f); // (→: the shaft and the head)
        g.fillRect (cx + 1.0f, cy - 2.0f, 1.0f, 5.0f);
        g.fillRect (cx + 2.0f, cy - 1.0f, 1.0f, 3.0f);
    }
};

class ClipChip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ClipChip() { setRepaintsOnMouseActivity (true); }
    void set (juce::Colour c, bool on)
    {
        if (c != fill || on != active)
        {
            fill = c;
            active = on;
            repaint();
        }
    }
    void paint (juce::Graphics& g) override
    {
        if (theme::livelook::active()) // (round 190jn: Live's activator in the clip-coloured title bar: a 9 px square, orange while on)
        {
            const auto sq = getLocalBounds().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (isMouseOver() ? theme::livelook::ink : liveclip::rim);
            g.fillRect (sq);
            g.setColour (active ? theme::livelook::on : theme::livelook::well);
            g.fillRect (sq.reduced (1.0f));
            return;
        }
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (fill);
        if (active)
            g.fillRect (r);
        else
            g.drawRect (r, 2.0f);
        g.setColour (isMouseOver() ? colour::textPrimary : colour::borderStrong);
        g.drawRect (r, 1.0f);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! contains (e.getPosition()))
            return;
        if (e.mods.isPopupMenu())
        {
            if (onColourMenu != nullptr)
                onColourMenu();
        }
        else if (onToggle != nullptr)
            onToggle();
    }
    std::function<void()> onToggle, onColourMenu;

private:
    juce::Colour fill { colour::bg5 };
    bool active = true;
};

class ClipColumn final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ClipColumn (AudioEngine& e)
        : engine (e),
          tools (e, [this] { return toolTarget(); },
                 [this] (const std::vector<int>& indices) {
                     if (auto* roll = editorOf != nullptr ? editorOf() : nullptr)
                         roll->selectMidiToolResult (indices);
                 })
    {
        // Live's Transform section [LIVE 12.4 Clip View] (round 190s): the fourth panel, foldable as Live's sections are
        addChildComponent (tools);
        foldTools.setConnectedEdges (0);
        foldTools.setTooltip (tr ("Transform: show or hide the MIDI Tools"));
        foldTools.onClick = [this] {
            toolsFolded = ! toolsFolded;
            load();
            if (auto* parent = getParentComponent())
                parent->resized();
        };
        addChildComponent (foldTools);

        // round 190s (GAPMAP-0126 D15, D6, D4): the clip's colour / activator square, the notes' pitch range (drag it
        // to transpose) and Add interval with its size
        chip.setTooltip (tr ("The clip's colour. Click: deactivate or activate the clip. Right-click: change its colour"));
        chip.onToggle = [this] { editClip ([] (Clip& c, ClipGridSlot&) { c.muted = ! c.muted; }); };
        chip.onColourMenu = [this] {
            const auto* c = clip();
            const auto& song = engine.getSong();
            if (c == nullptr || c->gridSlot < 0 || c->gridSlot >= (int) song.clipGrid->slots.size())
                return;
            const juce::Component::SafePointer<ClipColumn> safe (this);
            ui::colourMenu (song.clipGrid->slots[(std::size_t) c->gridSlot].colour,
                            [safe] (std::uint32_t colour) {
                                if (safe != nullptr)
                                    safe->editClip ([colour] (Clip&, ClipGridSlot& slot) { slot.colour = colour; });
                            },
                            chip.getScreenBounds())
                .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chip));
        };
        addAndMakeVisible (chip);
        {
            ui::ValueSpec range;
            range.name = tr ("Pitch range: drag up or down to transpose the notes");
            range.minimum = -96.0;
            range.maximum = 96.0;
            range.interval = 1.0;
            range.pixelsForRange = 192.0 * 6.0; // 6 px per semitone
            range.toText = [this] (double) { return rangeText; };
            pitchRange = std::make_unique<ui::NumBox> (range, [this] { return (double) transposed; }, [this] (double v) {
                const auto by = juce::roundToInt (v) - transposed;
                if (by == 0)
                    return;
                transposed += by;
                notes ([by] (Pattern& p, const std::vector<int>& all, const Song&) {
                    for (auto i : all)
                        p.notes[(std::size_t) i].key = std::clamp (p.notes[(std::size_t) i].key + by, 0, 131);
                });
            });
            addAndMakeVisible (*pitchRange);
            ui::ValueSpec size;
            size.name = tr ("Interval size");
            size.minimum = -24.0;
            size.maximum = 24.0;
            size.interval = 1.0;
            size.defaultValue = 0.0;
            size.pixelsForRange = 48.0 * 6.0;
            size.toText = [] (double v) { return juce::String (juce::roundToInt (v)) + " st"; };
            intervalSize = std::make_unique<ui::NumBox> (size, [this] { return (double) interval; }, [this] (double v) { interval = juce::roundToInt (v); });
            addAndMakeVisible (*intervalSize);
        }
        addInterval.setButtonText (tr ("Add interval"));
        addInterval.setTooltip (tr ("Adds to every note a note this many semitones away"));
        addInterval.setConnectedEdges (0);
        addInterval.onClick = [this] {
            if (interval == 0)
                return;
            notes ([by = interval] (Pattern& p, const std::vector<int>& all, const Song&) {
                std::vector<Note> added;
                for (auto i : all)
                {
                    auto n = p.notes[(std::size_t) i];
                    n.key += by;
                    if (n.key >= 0 && n.key <= 131)
                        added.push_back (n);
                }
                p.notes.insert (p.notes.end(), added.begin(), added.end());
            });
        };
        addAndMakeVisible (addInterval);
        for (auto* t : { &clipTitle, &launchTitle, &notesTitle })
        {
            t->setFont (font (type::small, true));
            t->setColour (juce::Label::textColourId, colour::textSecondary);
            addAndMakeVisible (*t);
        }
        clipTitle.setText (tr ("Clip"), juce::dontSendNotification);
        launchTitle.setText (tr ("Launch"), juce::dontSendNotification);
        notesTitle.setText (tr ("Notes"), juce::dontSendNotification);
        const std::pair<juce::Label*, const char*> labels[] = { { &nameLabel, TR_NOOP ("Name") }, { &startLabel, TR_NOOP ("Start") },
                                                                 { &endLabel, TR_NOOP ("End") }, { &lengthLabel, TR_NOOP ("Length") },
                                                                 { &positionLabel, TR_NOOP ("Position") }, { &grooveLabel, TR_NOOP ("Groove") },
                                                                 { &signatureLabel, TR_NOOP ("Signature") }, { &modeLabel, TR_NOOP ("Launch mode") },
                                                                 { &quantLabel, TR_NOOP ("Launch quantisation") }, { &scaleLabel, TR_NOOP ("Scale") } };
        for (const auto& [l, text] : labels)
        {
            l->setText (tr (text), juce::dontSendNotification);
            l->setFont (font (type::caption));
            l->setColour (juce::Label::textColourId, colour::textSecondary);
            addAndMakeVisible (*l);
        }
        for (auto* ed : { &name, &start, &end, &position, &length })
        {
            addAndMakeVisible (*ed);
            ed->setFont (font (type::small));
            ed->onReturnKey = [this] { apply(); };
            ed->onFocusLost = [this] { apply(); };
            ed->onEscapeKey = [this] { load(); };
        }
        // round 190dr-2: Live's fields [SHOT live-clipview-audio-panel.png] in bars. beats. sixteenths ("1. 1. 1"; lengths
        // "8. 0. 0"), seconds for an unwarped audio clip; LYNX's loop starts where the clip starts, so Position is the start
        // moved with the end and Length is the clip's length (core/ClipSample.h)
        start.setTooltip (tr ("Start: where the clip starts in its source (bar. beat. sixteenth; seconds for unwarped audio)"));
        end.setTooltip (tr ("End: where the clip ends in its source"));
        // round 190hr (detail-016): with Loop on, Start is a marker of its own inside the loop (Live's StartRelative)
        position.setTooltip (tr ("Loop Position: where the loop starts; the Start marker stays where it is"));
        length.setTooltip (tr ("Loop Length (bars. beats. sixteenths)"));
        // round 190dr: Live's Set buttons beside the fields: the marker goes to where the playing clip is now; dim while
        // the clip does not play
        for (auto* b : { &setStart, &setEnd, &setPosition, &setLength })
        {
            b->setButtonText (tr ("Set"));
            b->setConnectedEdges (0);
            b->setEnabled (false);
            b->onClick = [this, b] { setHere (b == &setStart ? 0 : b == &setEnd ? 1 : b == &setPosition ? 2 : 3); };
            addAndMakeVisible (*b);
        }
        setStart.setTooltip (tr ("Set: the clip starts where it plays now"));
        setEnd.setTooltip (tr ("Set: the clip ends where it plays now"));
        setPosition.setTooltip (tr ("Set: the loop moves to start where the clip plays now"));
        start.setTooltip (tr ("Start: where the clip starts playing in its source (with Loop on, a marker inside the loop; bar. beat. sixteenth; seconds for unwarped audio)"));
        setLength.setTooltip (tr ("Set: the loop ends where the clip plays now"));
        showingTimer = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 10);
        // the Groove chooser: the project's Groove Pool (round 190bv), for MIDI clips (LYNX's grooves act on MIDI clips)
        groove.onChange = [this] {
            if (loading)
                return;
            const auto& pool = engine.getSong().grooves.grooves;
            const auto index = groove.getSelectedId() - 2;
            const auto id = index >= 0 && index < (int) pool.size() ? pool[(std::size_t) index].id : 0u;
            engine.editSong ([t = track, sc = scene, id] (Song& s) { groove::setClipGroove (s, t, sc, id); });
        };
        addAndMakeVisible (groove);
        // round 190kc: Live's Commit (manual 14.1.2): the groove written into the clip — MIDI notes moved, an audio clip's
        // warp markers at its attacks — and the chooser back to None
        commitGroove.setButtonText (tr ("Commit"));
        commitGroove.setConnectedEdges (0);
        commitGroove.setTooltip (tr ("Commit: writes the groove into the clip (MIDI: the notes move; audio: warp markers at its attacks) and sets the groove to None"));
        commitGroove.onClick = [this] { commitClipGroove(); };
        addAndMakeVisible (commitGroove);
        signature.setFont (font (type::small));
        signature.setColour (juce::Label::textColourId, colour::textPrimary);
        signature.setTooltip (tr ("The project's time signature (clips have no signature of their own in LYNX SHARE)"));
        addAndMakeVisible (signature);
        loop.setButtonText (tr ("Loop"));
        loop.onClick = [this] { editSlot ([on = loop.getToggleState()] (ClipGridSlot& slot) { slot.loop = on; }); };
        // round gap190og (detail-019): Live 12's Clip panel "Duplicate" button under Start / End = Duplicate Loop [SHOT
        // ableton187c/shots/live-01-velocity-lane.png; STR DuplicateLoop in Live's ClipRegionPanel; DOC manual 8.9]
        duplicateLoop.setButtonText (tr ("Duplicate"));
        duplicateLoop.setTooltip (tr ("Duplicate Loop: the loop and its notes twice as long; notes after the loop move on with its end"));
        duplicateLoop.onClick = [this] { duplicateClipLoop(); };
        addAndMakeVisible (duplicateLoop);
        legato.setButtonText (tr ("Legato"));
        legato.setTooltip (tr ("Legato (keep the position)"));
        legato.onClick = [this] { editSlot ([on = legato.getToggleState()] (ClipGridSlot& slot) { slot.legato = on; }); };
        for (auto* b : { &loop, &legato })
            addAndMakeVisible (*b);
        const char* modes[] = { TR_NOOP ("Trigger"), TR_NOOP ("Gate"), TR_NOOP ("Toggle"), TR_NOOP ("Repeat") };
        for (int i = 0; i < 4; ++i)
            mode.addItem (tr (modes[i]), i + 1);
        mode.onChange = [this] { editSlot ([m = mode.getSelectedId() - 1] (ClipGridSlot& slot) { slot.mode = (LaunchMode) juce::jlimit (0, 3, m); }); };
        const char* quants[] = { TR_NOOP ("Global"), TR_NOOP ("None"), TR_NOOP ("8 bars"), TR_NOOP ("4 bars"), TR_NOOP ("2 bars"), TR_NOOP ("1 bar"),
                                 "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        for (int i = 0; i < 15; ++i)
            quant.addItem (i < 6 ? tr (quants[i]) : juce::String (quants[i]), i + 1);
        quant.onChange = [this] { editSlot ([q = quant.getSelectedId() - 1] (ClipGridSlot& slot) { slot.quant = (LaunchQuant) juce::jlimit (0, 14, q); }); };
        for (auto* c : { &mode, &quant })
            addAndMakeVisible (*c);

        // round 190hr (detail-028): the Launch panel's Follow Action controls over the slot's follow action (the same model
        // as the grid slot menu's Follow action submenu). Live 12's controls [STR live-exe-strings-u16.txt 2061-2075:
        // Follow Action Enabled, A, B, Chance A, Chance B, Time, Linked, Multiplier, Jump Index A / B]
        followOn.setButtonText (tr ("Follow Action"));
        followOn.setClickingTogglesState (true);
        followOn.setConnectedEdges (0);
        followOn.setTooltip (tr ("Follow Action: after its time the clip launches another, A or B by their chances"));
        followOn.onClick = [this] { editFollow ([on = followOn.getToggleState()] (ClipFollow& f) { f.enabled = on; }); };
        linked.setButtonText (tr ("Linked"));
        linked.setClickingTogglesState (true);
        linked.setConnectedEdges (0);
        linked.setTooltip (tr ("Linked: the follow action comes after the clip has played Multiplier times; off: after the Time"));
        linked.onClick = [this] { editFollow ([on = linked.getToggleState()] (ClipFollow& f) { f.linked = on; }); };
        for (auto* b : { &followOn, &linked })
            addAndMakeVisible (*b);
        const std::pair<FollowKind, const char*> actions[] = {
            { FollowKind::none, TR_NOOP ("No action") }, { FollowKind::stop, TR_NOOP ("Stop") }, { FollowKind::playAgain, TR_NOOP ("Play again") },
            { FollowKind::previous, TR_NOOP ("Previous") }, { FollowKind::next, TR_NOOP ("Next") }, { FollowKind::first, TR_NOOP ("First") },
            { FollowKind::last, TR_NOOP ("Last") }, { FollowKind::any, TR_NOOP ("Any") }, { FollowKind::other, TR_NOOP ("Other") },
            { FollowKind::jump, TR_NOOP ("Jump") }
        };
        for (auto* box : { &followA, &followB })
        {
            for (const auto& [k, text] : actions)
                box->addItem (tr (text), (int) k + 1);
            box->setTooltip (box == &followA ? tr ("Follow Action A") : tr ("Follow Action B"));
            box->onChange = [this, box] {
                if (loading || box->getSelectedId() < 1)
                    return;
                const auto k = (FollowKind) (box->getSelectedId() - 1);
                editFollow ([k, isA = box == &followA] (ClipFollow& f) { (isA ? f.a : f.b) = k; });
            };
            addAndMakeVisible (*box);
        }
        const auto numberBox = [this] (std::unique_ptr<ui::NumBox>& box, const juce::String& name, double lo, double hi, std::function<juce::String (double)> text,
                                       std::function<double (const ClipFollow&)> get, std::function<void (ClipFollow&, int)> set) {
            ui::ValueSpec spec;
            spec.name = name;
            spec.minimum = lo;
            spec.maximum = hi;
            spec.interval = 1.0;
            spec.pixelsForRange = std::min (300.0, (hi - lo) * 4.0);
            spec.toText = std::move (text);
            box = std::make_unique<ui::NumBox> (spec, [this, get] { const auto* f = follow(); return f != nullptr ? get (*f) : 0.0; },
                                                [this, set] (double v) { editFollow ([set, v] (ClipFollow& f) { set (f, juce::roundToInt (v)); }); });
            addAndMakeVisible (*box);
        };
        const auto percent = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
        numberBox (chanceA, tr ("Follow Action Chance A"), 0, 100, percent, [] (const ClipFollow& f) { return (double) f.chanceA; }, [] (ClipFollow& f, int v) { f.chanceA = v; });
        numberBox (chanceB, tr ("Follow Action Chance B"), 0, 100, percent, [] (const ClipFollow& f) { return (double) f.chanceB; }, [] (ClipFollow& f, int v) { f.chanceB = v; });
        numberBox (multiplier, tr ("Follow Action Multiplier"), 1, 64, [] (double v) { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (juce::roundToInt (v)); },
                   [] (const ClipFollow& f) { return (double) f.iterations; }, [] (ClipFollow& f, int v) { f.iterations = std::max (1, v); });
        const auto scene = [] (double v) { return juce::String (juce::roundToInt (v)); };
        numberBox (jumpA, tr ("Follow Action Jump Index A"), 1, launcher::maxScenes, scene, [] (const ClipFollow& f) { return (double) f.jumpA + 1; }, [] (ClipFollow& f, int v) { f.jumpA = std::max (0, v - 1); });
        numberBox (jumpB, tr ("Follow Action Jump Index B"), 1, launcher::maxScenes, scene, [] (const ClipFollow& f) { return (double) f.jumpB + 1; }, [] (ClipFollow& f, int v) { f.jumpB = std::max (0, v - 1); });
        followTime.setFont (font (type::small));
        followTime.setTooltip (tr ("Follow Action Time (bars. beats. sixteenths): with Linked off, the follow action comes after this time"));
        followTime.onReturnKey = [this] { applyFollowTime(); };
        followTime.onFocusLost = [this] { applyFollowTime(); };
        followTime.onEscapeKey = [this] { load(); };
        addAndMakeVisible (followTime);
        for (auto* l : { &followTitle })
        {
            l->setFont (font (type::small, true));
            l->setColour (juce::Label::textColourId, colour::textSecondary);
            l->setText (tr ("Follow Action"), juce::dontSendNotification);
            addAndMakeVisible (*l);
        }

        // the reference's note tools, each the Piano roll's own operation on every note of the clip's pattern
        const std::pair<juce::TextButton*, const char*> tools[] = {
            { &invert, TR_NOOP ("Invert") },       { &reverse, TR_NOOP ("Reverse") }, { &makeLegato, TR_NOOP ("Make legato") },
            { &humanize, TR_NOOP ("Humanize") },   { &duplicate, TR_NOOP ("Duplicate") }, { &quantize, TR_NOOP ("Quantize") },
            { &randomize, TR_NOOP ("Randomize velocity") } };
        for (const auto& [b, text] : tools)
        {
            b->setButtonText (tr (text));
            b->setConnectedEdges (0);
            addAndMakeVisible (*b);
        }
        invert.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song&) { notetools::flip (p, all, false, true); }); };
        reverse.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song&) { notetools::flip (p, all, true, false); }); };
        makeLegato.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song&) { noteedit::legato (p, all); }); };
        humanize.onClick = [this] {
            notes ([seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt()] (Pattern& p, const std::vector<int>& all, const Song&) {
                notetools::HumanizeOptions o;
                o.seed = seed;
                notetools::humanize (p, all, o);
            });
        };
        duplicate.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song& song) { noteedit::duplicateBars (p, all, song.meter()); }); };
        quantize.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song&) { noteedit::quantize (p, all, ppq / 4); }); };
        randomize.onClick = [this] {
            notes ([seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt()] (Pattern& p, const std::vector<int>& all, const Song&) {
                notetools::RandomiseOptions o;
                o.velocity = 0.5f; // ±0.25 as before round 190ez halved the randomizer's law to FL's
                o.seed = seed;
                notetools::randomise (p, all, o);
            });
        };
        // Live's Stretch ×2 and /2 and Fit to Scale [LIVE 12.4 Clip View, Pitch & Time] (round 190o)
        timesTwo.setButtonText (juce::String::fromUTF8 ("\xc3\x97" "2"));
        divideTwo.setButtonText (juce::String::fromUTF8 ("\xc3\xb7" "2"));
        fitScale.setButtonText (tr ("Fit to scale"));
        timesTwo.setTooltip (tr ("Twice as slow: doubles every note's start and length"));
        divideTwo.setTooltip (tr ("Twice as fast: halves every note's start and length"));
        fitScale.setTooltip (tr ("Moves every note of the clip onto the clip's scale"));
        for (auto* b : { &timesTwo, &divideTwo, &fitScale })
        {
            b->setConnectedEdges (0);
            addAndMakeVisible (*b);
        }
        // LYNX's Chord helper (round 190mc, discussion #105): the docked editor's selection, in Live's look
        chordHelper.setButtonText (tr ("Chord helper"));
        chordHelper.setTooltip (tr ("Select a chord: related keys and borrowed chords to insert after it"));
        chordHelper.setConnectedEdges (0);
        chordHelper.onClick = [this] {
            if (editorOf)
                if (auto* roll = editorOf())
                    roll->openChordHelper();
        };
        addAndMakeVisible (chordHelper);
        const auto stretch = [this] (bool twice) {
            notes ([twice] (Pattern& p, const std::vector<int>& all, const Song&) {
                for (auto i : all)
                {
                    auto& n = p.notes[(std::size_t) i];
                    n.start = twice ? n.start * 2 : n.start / 2;
                    n.length = std::max<std::int64_t> (1, twice ? n.length * 2 : n.length / 2);
                }
                if (p.lengthTicks > 0)
                    p.lengthTicks = std::max<std::int64_t> (1, twice ? p.lengthTicks * 2 : p.lengthTicks / 2);
            });
        };
        timesTwo.onClick = [stretch] { stretch (true); };
        divideTwo.onClick = [stretch] { stretch (false); };
        fitScale.onClick = [this] {
            notes ([key = clipKeyOf (engine.getSong())] (Pattern& p, const std::vector<int>& all, const Song&) {
                if (key && projectkey::lynxScale (*key) > 0)
                    notetools::snapNotesToScale (p, all, key->root, projectkey::lynxScale (*key));
            });
        };

        // the Clip panel's Scale row (round 190hr, detail-022): the clip's own scale, as Live's Clip Scale [SHOT
        // live190gu/warp/live-clipview-beats-defaults-full.png, ableton190o/shots/live-02-clipview-scale-bhairav.png: the
        // Scale Mode switch, the root and the scale name]. A clip follows the project key until its row is changed.
        scaleMode.setButtonText (juce::String::fromUTF8 ("\xe2\x99\xad\xe2\x99\xaf")); // (flat sharp, Live's switch)
        scaleMode.setClickingTogglesState (true);
        scaleMode.setConnectedEdges (0);
        scaleMode.setTooltip (tr ("Scale Mode: the clip's notes, editor and tools follow the clip's scale"));
        scaleMode.onClick = [this] { editClip ([on = scaleMode.getToggleState()] (Clip&, ClipGridSlot& slot) { slot.scaleOn = on; }); };
        addAndMakeVisible (scaleMode);
        for (int root = 0; root < 12; ++root)
            scaleRoot.addItem (juce::String (projectkey::rootName (root)), root + 1);
        for (auto index : projectkey::commonFirst())
            scaleName.addItem (tr (notetools::flReferenceScales()[(std::size_t) index].name), index + 1);
        for (auto* c : { &scaleRoot, &scaleName })
        {
            c->setTooltip (tr ("The clip's scale: its root and name (a new clip follows the project's key)"));
            addAndMakeVisible (*c);
            c->onChange = [this] {
                if (loading || scaleRoot.getSelectedId() < 1)
                    return;
                const auto root = scaleRoot.getSelectedId() - 1;
                const auto index = scaleName.getSelectedId() >= 1 ? scaleName.getSelectedId() - 1 : projectkey::commonFirst().front();
                editClip ([root, index] (Clip&, ClipGridSlot& slot) {
                    slot.ownScale = true;
                    slot.scaleRoot = (std::int8_t) root;
                    slot.scaleIndex = (std::int16_t) index;
                });
            };
        }
        engine.addChangeListener (this);
    }
    ~ClipColumn() override { engine.removeChangeListener (this); }

    // the reference's Clip / Launch / Notes panels side by side under the clip's name [SHOT d08]
    static constexpr int clipPanel = 164, clipPanel2 = 164, launchPanel = 136, followPanel = 144, notesPanel = 172, gap = 8;
    static constexpr int foldedTools = 20;
    int preferredWidth() const
    {
        if (theme::livelook::active()) // (round 190jn: Live's 170 px column + its 2 px separator; the MIDI Tools stay at its right)
            return liveclip::columnWidth + 2 + (hasNotes ? gap + (toolsFolded ? foldedTools : MidiToolsPanel::panelWidth) + gap : 0);
        return gap + clipPanel + gap + clipPanel2 + gap + launchPanel + gap + followPanel + gap + (hasNotes ? notesPanel + gap + (toolsFolded ? foldedTools : MidiToolsPanel::panelWidth) + gap : 0);
    }
    // the docked Piano roll (nullptr while it is not docked here): the MIDI Tools work on its selection
    std::function<PianoRollComponent*()> editorOf;
    MidiToolsPanel& midiTools() noexcept { return tools; }
    static inline bool toolsFolded = false; // UI only, not saved

    void setSlot (int t, int s)
    {
        if (t != track || s != scene)
            transposed = 0; // (the pitch range's drag counts from this clip as it is)
        track = t;
        scene = s;
        load();
    }

    void paint (juce::Graphics& g) override
    {
        if (liveShown)
        {
            paintLive (g);
            return;
        }
        g.fillAll (colour::bg2);
        g.setColour (colour::border);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }

    void resized() override
    {
        applyLook();
        if (liveShown)
        {
            resizedLive();
            return;
        }
        auto r = getLocalBounds().reduced (gap, 4).withTrimmedRight (1);
        const auto take = [] (juce::Rectangle<int>& area, int h) { auto x = area.removeFromTop (h); area.removeFromTop (3); return x; };
        if (hasNotes)
        {
            // Transform: the whole height at the right; folded, only its button
            auto t = r.removeFromRight (toolsFolded ? foldedTools : MidiToolsPanel::panelWidth);
            r.removeFromRight (gap);
            foldTools.setBounds (t.getRight() - 18, t.getY(), 18, 16);
            tools.setBounds (t);
        }
        clipTitle.setBounds (take (r, 16));
        auto top = take (r, 20);
        chip.setBounds (top.removeFromLeft (20));
        top.removeFromLeft (6);
        nameLabel.setBounds (top.removeFromLeft (44));
        name.setBounds (top);
        r.removeFromTop (2);

        // Clip: Start | End, Loop, Length | Signature
        auto c = r.removeFromLeft (clipPanel);
        r.removeFromLeft (gap);
        const auto halves = [] (juce::Rectangle<int> row, juce::Component& a, juce::Component& b) {
            const auto w = (row.getWidth() - 6) / 2;
            a.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
            b.setBounds (row);
        };
        {
            // Start [Set] | End [Set]
            auto row = take (c, 16);
            const auto w = (row.getWidth() - 6) / 2;
            auto a = row.removeFromLeft (w);
            row.removeFromLeft (6);
            setStart.setBounds (a.removeFromRight (30));
            startLabel.setBounds (a);
            setEnd.setBounds (row.removeFromRight (30));
            endLabel.setBounds (row);
        }
        halves (take (c, 20), start, end);
        loop.setBounds (take (c, 20));
        {
            // Position [Set] | Length [Set]
            auto row = take (c, 16);
            const auto w = (row.getWidth() - 6) / 2;
            auto a = row.removeFromLeft (w);
            row.removeFromLeft (6);
            setPosition.setBounds (a.removeFromRight (30));
            positionLabel.setBounds (a);
            setLength.setBounds (row.removeFromRight (30));
            lengthLabel.setBounds (row);
        }
        halves (take (c, 20), position, length);
        // Signature | Groove, then Scale: a second column beside them (Live stacks them below; the dock is lower)
        auto c2 = r.removeFromLeft (clipPanel2);
        r.removeFromLeft (gap);
        halves (take (c2, 14), signatureLabel, grooveLabel);
        commitGroove.setBounds (grooveLabel.getBounds().removeFromRight (46).withTrimmedTop (1)); // (round 190kc)
        halves (take (c2, 20), signature, groove);
        scaleLabel.setBounds (take (c2, 14));
        {
            auto row = take (c2, 20);
            scaleMode.setBounds (row.removeFromLeft (22));
            row.removeFromLeft (3);
            const auto w = (row.getWidth() - 4) * 2 / 5;
            scaleRoot.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (4);
            scaleName.setBounds (row);
        }

        // Launch: mode, quantisation, legato
        auto l = r.removeFromLeft (launchPanel);
        r.removeFromLeft (gap);
        launchTitle.setBounds (take (l, 14));
        modeLabel.setBounds (take (l, 12));
        mode.setBounds (take (l, 20));
        quantLabel.setBounds (take (l, 12));
        quant.setBounds (take (l, 20));
        legato.setBounds (take (l, 20));

        // Follow Action (round 190hr): the switch; A | B with their jump targets; the chances; Linked with the
        // multiplier; the time
        auto f = r.removeFromLeft (followPanel);
        r.removeFromLeft (gap);
        followTitle.setBounds (take (f, 14));
        followOn.setBounds (take (f, 18));
        halves (take (f, 20), followA, followB);
        {
            auto row = take (f, 18);
            const auto w = (row.getWidth() - 6) / 2;
            auto a = row.removeFromLeft (w);
            row.removeFromLeft (6);
            jumpA->setBounds (a.removeFromRight (jumpA->isVisible() ? 30 : 0));
            chanceA->setBounds (a.withTrimmedRight (jumpA->isVisible() ? 3 : 0));
            jumpB->setBounds (row.removeFromRight (jumpB->isVisible() ? 30 : 0));
            chanceB->setBounds (row.withTrimmedRight (jumpB->isVisible() ? 3 : 0));
        }
        halves (take (f, 18), linked, *multiplier);
        followTime.setBounds (take (f, 18));

        // Notes: the note tools, two columns
        auto n = r.removeFromLeft (notesPanel);
        notesTitle.setBounds (take (n, 14));
        // six rows of two (round 190s): the pitch range | Invert; Reverse | Make legato; Humanize | Quantize; x2 /2 |
        // Fit to scale; the interval size | Add interval; Duplicate | Randomize velocity (the wider one)
        const auto w = (n.getWidth() - 4) / 2;
        const auto cell = [&n, w] (int row, int col) { return juce::Rectangle<int> (n.getX() + col * (w + 4), n.getY() + row * 22, w, 19); };
        pitchRange->setBounds (cell (0, 0));
        invert.setBounds (cell (0, 1));
        reverse.setBounds (cell (1, 0));
        makeLegato.setBounds (cell (1, 1));
        humanize.setBounds (cell (2, 0));
        quantize.setBounds (cell (2, 1));
        const auto small = (w - 4) / 2;
        timesTwo.setBounds (cell (3, 0).withWidth (small));
        divideTwo.setBounds (cell (3, 0).withTrimmedLeft (small + 4));
        fitScale.setBounds (cell (3, 1));
        intervalSize->setBounds (cell (4, 0));
        addInterval.setBounds (cell (4, 1));
        duplicate.setBounds (cell (5, 0).withWidth (60));
        randomize.setBounds (cell (5, 0).withTrimmedLeft (64).withRight (n.getRight()));
        chordHelper.setBounds (cell (6, 0).withRight (n.getRight()));
    }
    void reload() { load(); } // (the page docked an editor: it gets the column's hooks)

    // --- round 190jn (design190ia CL-20a / CL-20b): Live 12.4's Clip View column ----------------------------------------
    // A 170 px column (live-clipview-audio.png 249..418, live-01-clipview-notes.png 9..178): the 17 px title bar in the
    // clip's colour (the activator square, the name in colour × 0.24) stays; under it Live's sections, each a 17 px #464646
    // header (click: fold / unfold) over its rows — Clip; Launch (with LYNX's Follow Action, as Live 12 puts it there;
    // folded at first, as Live shows it); for MIDI clips Pitch & Time (LYNX's note tools). Rows are 13 px fields and pills.
    // When the open sections are taller than the view, the wheel scrolls them under the title bar (Live does the same).
    // The fold states are UI state, not saved. The MIDI Tools (Transform / Generate) stay at the column's right.
    static inline bool clipOpen = true, launchOpen = false, notesOpen = true;
    bool isLive() const noexcept { return liveShown; }
    void setScrollForSnapshot (int y)
    {
        scrollY = y;
        resized();
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! liveShown || e.x >= liveclip::columnWidth)
            return;
        for (int k = 0; k < 3; ++k)
            if (headerY[(std::size_t) k] >= liveclip::headerHeight && e.y >= headerY[(std::size_t) k] && e.y < headerY[(std::size_t) k] + liveclip::headerHeight)
            {
                auto& open = k == 0 ? clipOpen : k == 1 ? launchOpen : notesOpen;
                open = ! open;
                resized();
                repaint();
                return;
            }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! liveShown || e.x >= liveclip::columnWidth)
        {
            juce::Component::mouseWheelMove (e, wheel);
            return;
        }
        const auto before = scrollY;
        scrollY = juce::jlimit (0, maxScroll(), scrollY - juce::roundToInt (wheel.deltaY * 120.0f));
        if (scrollY != before)
        {
            resized();
            repaint();
        }
    }

private:
    bool liveShown = false;
    int scrollY = 0, contentHeight = 0;
    std::array<int, 3> headerY {}; // the sections' header tops (Clip, Launch, Pitch & Time; < 17: not shown)
    juce::Colour titleColour { colour::bg5 };
    int maxScroll() const { return std::max (0, contentHeight - getHeight()); }

    std::vector<juce::Component*> styledChildren()
    {
        return { &start, &end, &position, &length, &followTime, &setStart, &setEnd, &setPosition, &setLength, &followOn, &linked, &invert, &reverse,
                 &makeLegato, &humanize, &duplicate, &quantize, &randomize, &timesTwo, &divideTwo, &fitScale, &chordHelper, &addInterval, &scaleMode, &groove, &mode,
                 &quant, &followA, &followB, &scaleRoot, &scaleName, &loop, &duplicateLoop, &legato, &nameLabel, &startLabel, &endLabel, &lengthLabel,
                 &positionLabel, &grooveLabel, &signatureLabel, &modeLabel, &quantLabel, &scaleLabel, &signature, &commitGroove };
    }
    // the look follows the layout's switch (checked on every layout and load; nothing changes while it stays the same)
    void applyLook()
    {
        const auto live = theme::livelook::active();
        if (live == liveShown)
            return;
        liveShown = live;
        for (auto* c : styledChildren())
            liveclip::style (*c, live);
        for (auto* l : { &clipTitle, &launchTitle, &notesTitle, &followTitle, &nameLabel })
            l->setVisible (! live);
        nameColours();
        if (! live)
            scrollY = 0;
    }
    // the name sits on the title bar in the clip's ink (Live: colour × 0.24)
    void nameColours()
    {
        const auto ink = liveMarkOn (titleColour, 0.24f, 4.5f, colour::textPrimary); // (round 190oj: readable on dark clip colours too)
        liveclip::swapColour (name, juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack, liveShown);
        liveclip::swapColour (name, juce::TextEditor::outlineColourId, juce::Colours::transparentBlack, liveShown);
        liveclip::swapColour (name, juce::TextEditor::textColourId, ink, liveShown);
        if (liveShown)
            name.setColour (juce::TextEditor::textColourId, ink);
        name.applyColourToAllText (name.findColour (juce::TextEditor::textColourId), true);
        name.setTextToShowWhenEmpty (nameFallback, name.findColour (juce::TextEditor::textColourId)); // (round 190oj CS-33)
    }

    void paintLive (juce::Graphics& g)
    {
        namespace ll = theme::livelook;
        const auto w = liveclip::columnWidth;
        g.fillAll (ll::window);
        g.setColour (ll::surface);
        g.fillRect (0, 0, w, getHeight());
        g.setColour (ll::separator);
        g.fillRect (w, 0, 2, getHeight());
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (0, liveclip::headerHeight + 1, w, getHeight());
            const char* titles[] { TR_NOOP ("Clip"), TR_NOOP ("Launch"), TR_NOOP ("Pitch & Time") };
            const bool open[] { clipOpen, launchOpen, notesOpen };
            for (int k = 0; k < 3; ++k)
                if (k < 2 || hasNotes)
                    liveclip::paintHeader (g, { 0, headerY[(std::size_t) k], w, liveclip::headerHeight }, tr (titles[k]), k == 2 ? 3 : k, open[k]);
        }
        g.setColour (titleColour);
        g.fillRect (0, 0, w, liveclip::headerHeight);
        g.setColour (ll::separator);
        g.fillRect (0, liveclip::headerHeight, w, 1);
    }

    void resizedLive()
    {
        const auto w = liveclip::columnWidth;
        const auto top = liveclip::headerHeight + 1;
        const int row = liveclip::row, x0 = 11, inner = w - 22, half = (inner - 4) / 2;
        // a child goes where the layout puts it while that is wholly under the title bar and in view; else it hides
        const auto put = [this, top] (juce::Component& c, juce::Rectangle<int> r) {
            c.setBounds (r.getY() >= top && r.getBottom() <= getHeight() ? r : juce::Rectangle<int>());
        };
        const auto hide = [] (std::initializer_list<juce::Component*> list) {
            for (auto* c : list)
                c->setBounds ({});
        };
        chip.setBounds (2, 2, 13, 13);
        name.setBounds (17, 1, w - 21, 15);
        for (auto* l : { &clipTitle, &launchTitle, &notesTitle, &followTitle, &nameLabel })
            l->setBounds ({});
        if (hasNotes)
        {
            auto t = getLocalBounds().withTrimmedLeft (w + 2 + gap).withTrimmedRight (gap).reduced (0, 4);
            foldTools.setBounds (t.getRight() - 18, t.getY(), 18, 16);
            tools.setBounds (t.withWidth (toolsFolded ? foldedTools : MidiToolsPanel::panelWidth).withRightX (t.getRight()));
        }
        int y = top - scrollY;
        const auto labelRow = [&] (juce::Component& a, juce::Component* set, int x, int width) {
            put (a, { x, y, width - (set != nullptr ? 24 : 0), row });
            if (set != nullptr)
                put (*set, { x + width - 22, y + 1, 22, row - 2 });
        };
        // Clip
        headerY[0] = y;
        y += liveclip::headerHeight + 1;
        if (clipOpen)
        {
            y += 4;
            labelRow (startLabel, &setStart, x0, half);
            labelRow (endLabel, &setEnd, x0 + half + 4, half);
            y += row + 1;
            put (start, { x0, y, half, row });
            put (end, { x0 + half + 4, y, half, row });
            y += row + 5;
            if (hasNotes) // (round gap190og, detail-019: Live's Duplicate button between the Start / End fields and Loop)
            {
                put (duplicateLoop, { x0, y, inner, row });
                y += row + 5;
            }
            else
                duplicateLoop.setBounds ({});
            put (loop, { x0, y, inner, row });
            y += row + 5;
            labelRow (positionLabel, &setPosition, x0, half);
            labelRow (lengthLabel, &setLength, x0 + half + 4, half);
            y += row + 1;
            put (position, { x0, y, half, row });
            put (length, { x0 + half + 4, y, half, row });
            y += row + 5;
            put (signatureLabel, { x0, y, half, row });
            put (grooveLabel, { x0 + half + 4, y, half - 16, row });
            put (commitGroove, { x0 + 2 * half + 4 - 14, y, 14, row }); // (round 190kc: Live's round well)
            y += row + 1;
            put (signature, { x0, y, half, row });
            put (groove, { x0 + half + 4, y, half, row });
            y += row + 5;
            put (scaleLabel, { x0, y, inner, row });
            y += row + 1;
            put (scaleMode, { x0, y, 18, row });
            put (scaleRoot, { x0 + 21, y, 50, row });
            put (scaleName, { x0 + 74, y, inner - 74, row });
            y += row + 6;
        }
        else
            hide ({ &startLabel, &setStart, &endLabel, &setEnd, &start, &end, &loop, &duplicateLoop, &positionLabel, &setPosition, &lengthLabel, &setLength, &position,
                    &length, &signatureLabel, &grooveLabel, &signature, &groove, &scaleLabel, &scaleMode, &scaleRoot, &scaleName, &commitGroove });
        // Launch (+ Follow Action)
        headerY[1] = y;
        y += liveclip::headerHeight + 1;
        if (launchOpen)
        {
            y += 4;
            put (modeLabel, { x0, y, inner, row });
            y += row + 1;
            put (mode, { x0, y, inner, row });
            y += row + 5;
            put (quantLabel, { x0, y, inner, row });
            y += row + 1;
            put (quant, { x0, y, inner, row });
            y += row + 5;
            put (legato, { x0, y, inner, row });
            y += row + 5;
            put (followOn, { x0, y, inner, row });
            y += row + 4;
            put (followA, { x0, y, half, row });
            put (followB, { x0 + half + 4, y, half, row });
            y += row + 4;
            {
                const auto jw = 26;
                put (*jumpA, { x0 + half - (jumpA->isVisible() ? jw : 0), y, jumpA->isVisible() ? jw : 0, row });
                put (*chanceA, { x0, y, half - (jumpA->isVisible() ? jw + 3 : 0), row });
                put (*jumpB, { x0 + inner - (jumpB->isVisible() ? jw : 0), y, jumpB->isVisible() ? jw : 0, row });
                put (*chanceB, { x0 + half + 4, y, half - (jumpB->isVisible() ? jw + 3 : 0), row });
            }
            y += row + 4;
            put (linked, { x0, y, half, row });
            put (*multiplier, { x0 + half + 4, y, half, row });
            y += row + 4;
            put (followTime, { x0, y, inner, row });
            y += row + 6;
        }
        else
            hide ({ &modeLabel, &mode, &quantLabel, &quant, &legato, &followOn, &followA, &followB, jumpA.get(), chanceA.get(), jumpB.get(), chanceB.get(),
                    &linked, multiplier.get(), &followTime });
        // Pitch & Time (MIDI clips): LYNX's note tools, two columns of 13 px pills
        headerY[2] = hasNotes ? y : -1;
        if (hasNotes)
        {
            y += liveclip::headerHeight + 1;
            if (notesOpen)
            {
                y += 4;
                const auto cell = [&] (int r, int c) { return juce::Rectangle<int> (x0 + c * (half + 4), y + r * (row + 4), half, row); };
                put (*pitchRange, cell (0, 0));
                put (invert, cell (0, 1));
                put (reverse, cell (1, 0));
                put (makeLegato, cell (1, 1));
                put (humanize, cell (2, 0));
                put (quantize, cell (2, 1));
                const auto small = (half - 4) / 2;
                put (timesTwo, cell (3, 0).withWidth (small));
                put (divideTwo, cell (3, 0).withTrimmedLeft (small + 4));
                put (fitScale, cell (3, 1));
                put (*intervalSize, cell (4, 0));
                put (addInterval, cell (4, 1));
                put (duplicate, cell (5, 0));
                put (randomize, cell (5, 1));
                put (chordHelper, cell (6, 0).withRight (cell (6, 1).getRight()));
                y += 7 * (row + 4) + 4;
            }
            else
                hide ({ pitchRange.get(), &invert, &reverse, &makeLegato, &humanize, &quantize, &timesTwo, &divideTwo, &fitScale, intervalSize.get(),
                        &addInterval, &duplicate, &randomize, &chordHelper });
        }
        contentHeight = y + scrollY;
        if (scrollY > maxScroll()) // (a section folded at the bottom: the column scrolls back)
        {
            scrollY = maxScroll();
            resizedLive();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { load(); }

    // the Set buttons are lit only while this clip plays
    bool clipPlays (float* progress = nullptr) const
    {
        const auto* state = engine.launcherState();
        if (state == nullptr || track < 0 || track >= launcher::maxTracks || clip() == nullptr)
            return false;
        const auto& view = state->tracks[(std::size_t) track];
        if (view.playing.load (std::memory_order_relaxed) != scene || ! state->running.load (std::memory_order_relaxed))
            return false;
        if (progress != nullptr)
            *progress = view.progress.load (std::memory_order_relaxed);
        return true;
    }
    void timerCallback() override
    {
        const auto on = clipPlays();
        for (auto* b : { &setStart, &setEnd })
            if (b->isEnabled() != on)
                b->setEnabled (on);
        for (auto* b : { &setPosition, &setLength }) // (the loop's: only with Loop on)
            if (b->isEnabled() != (on && loop.getToggleState()))
                b->setEnabled (on && loop.getToggleState());
    }
    // 0 Start, 1 End, 2 Position, 3 Length (Length's Set ends the loop there: LYNX's loop ends where the clip does)
    // round 190kc: Commit (the Clip box's button)
    void commitClipGroove()
    {
        const auto& song = engine.getSong();
        const auto* c = song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
        if (c == nullptr)
            return;
        if (c->kind == ClipKind::pattern)
            engine.editSong ([t = track, sc = scene] (Song& s) { groove::commit (s, t, sc); });
        else if (c->kind == ClipKind::audio && c->channel >= 0 && c->channel < (int) song.channels.size())
            if (const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (song.channels[(std::size_t) c->channel].samplePath.c_str())))
                engine.editSong ([t = track, sc = scene, sample] (Song& s) { grooveaudio::commit (s, t, sc, *sample); });
    }

    void setHere (int which)
    {
        float progress = 0.0f;
        if (! clipPlays (&progress))
            return;
        editClip ([which, progress] (Clip& c, ClipGridSlot& slot) {
            const auto at = clipsample::playPosition (c, progress);
            const auto marker = c.offset + (std::int64_t) launcher::startLead (slot, c.length);
            if (which == 0 && slot.loop && at >= c.offset && at < c.offset + c.length)
                slot.startMarker = at - c.offset; // (round 190hr: the Start marker inside the loop)
            else if (which == 0)
            {
                clipsample::setStartAt (c, at);
                slot.startMarker = 0;
            }
            else if (which == 2)
            {
                clipsample::setPositionAt (c, at);
                slot.startMarker = marker >= c.offset && marker < c.offset + c.length ? marker - c.offset : 0;
            }
            else
            {
                clipsample::setEndAt (c, at);
                if (slot.startMarker >= c.length)
                    slot.startMarker = 0;
            }
        });
    }
    // The clip's content is in seconds (an unwarped audio clip) rather than in beats
    bool inSeconds (const Clip& c) const
    {
        const auto& song = engine.getSong();
        return c.kind == ClipKind::audio && c.channel >= 0 && c.channel < (int) song.channels.size()
               && ! clipsample::isWarped (song.channels[(std::size_t) c.channel].sampler);
    }
    juce::String fieldText (std::int64_t ticks, bool asLength, bool seconds) const
    {
        const auto& song = engine.getSong();
        return juce::String (seconds ? clipsample::secondsText (ticks, song.bpm) : clipsample::positionText (ticks, song.meter(), asLength));
    }
    std::int64_t fieldTicks (const juce::TextEditor& ed, bool asLength, bool seconds) const
    {
        const auto& song = engine.getSong();
        const auto text = ed.getText().trim().toStdString();
        return seconds ? clipsample::parseSeconds (text, song.bpm) : clipsample::parsePosition (text, song.meter(), asLength);
    }

    // the grid clip of the slot (const) and its settings
    const Clip* clip() const
    {
        const auto& song = engine.getSong();
        return song.clipGrid && track >= 0 && scene >= 0 ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    }

    void load()
    {
        const auto* c = clip();
        setVisible (c != nullptr);
        if (c == nullptr)
            return;
        const auto& song = engine.getSong();
        const auto bpb = song.beatsPerBar;
        const ClipGridSlot* slot = c->gridSlot >= 0 && c->gridSlot < (int) song.clipGrid->slots.size() ? &song.clipGrid->slots[(std::size_t) c->gridSlot] : nullptr;
        const auto n = juce::dontSendNotification;
        if (! name.hasKeyboardFocus (false))
            name.setText (slot != nullptr ? utf8 (slot->name) : juce::String(), false);
        // round 190oj (CS-33): a clip without its own name shows the name the grid shows (its pattern's or its sample
        // channel's) on the title band, as Live does, in the band's ink; typing gives the clip its own name
        {
            juce::String shownName;
            if (slot == nullptr || ! slot->blankName)
            {
                if (c->kind == ClipKind::pattern && c->pattern >= 0 && c->pattern < (int) song.patterns.size())
                    shownName = utf8 (song.patterns[(std::size_t) c->pattern].name);
                else if (c->kind == ClipKind::audio && c->channel >= 0 && c->channel < (int) song.channels.size())
                    shownName = utf8 (song.channels[(std::size_t) c->channel].name);
            }
            if (shownName != nameFallback)
            {
                nameFallback = shownName;
                name.setTextToShowWhenEmpty (nameFallback, name.findColour (juce::TextEditor::textColourId));
            }
        }
        const auto secs = inSeconds (*c);
        const auto marker = slot != nullptr ? (std::int64_t) launcher::startLead (*slot, c->length) : 0;
        if (! start.hasKeyboardFocus (false))
            start.setText (fieldText (c->offset + marker, false, secs), false);
        if (! end.hasKeyboardFocus (false))
            end.setText (fieldText (c->offset + c->length, false, secs), false);
        if (! position.hasKeyboardFocus (false))
            position.setText (fieldText (c->offset, false, secs), false);
        if (! length.hasKeyboardFocus (false))
            length.setText (fieldText (c->length, true, secs), false);
        signature.setText (juce::String (bpb) + " / " + juce::String (meterUnit (song.beatUnit)), n);
        {
            // the Groove chooser: None + the pool's grooves; MIDI clips only
            const juce::ScopedValueSetter<bool> guard (loading, true);
            groove.clear (n);
            groove.addItem (tr ("None"), 1);
            const auto& pool = song.grooves.grooves;
            for (int i = 0; i < (int) pool.size(); ++i)
                groove.addItem (utf8 (pool[(std::size_t) i].name), i + 2);
            int selected = 1;
            if (slot != nullptr && slot->groove != 0)
                for (int i = 0; i < (int) pool.size(); ++i)
                    if (pool[(std::size_t) i].id == slot->groove)
                        selected = i + 2;
            groove.setSelectedId (selected, n);
            // (round 190kc: audio clips take a groove too, for Commit: LYNX plays grooves live on MIDI clips only)
            groove.setEnabled (c->kind == ClipKind::pattern || c->kind == ClipKind::audio);
            groove.setTooltip (c->kind == ClipKind::pattern ? tr ("The groove this clip plays through (the Groove Pool)")
                                                            : tr ("The groove for this audio clip: Commit writes it as warp markers (LYNX SHARE plays grooves live on MIDI clips only)"));
            commitGroove.setEnabled (slot != nullptr && slot->groove != 0 && (c->kind == ClipKind::pattern || c->kind == ClipKind::audio));
        }
        if (slot != nullptr)
        {
            loop.setToggleState (slot->loop, n);
            for (auto* ed : { &position, &length }) // (Live: the loop's fields are off with Loop off)
                ed->setEnabled (slot->loop);
            legato.setToggleState (slot->legato, n);
            {
                const juce::ScopedValueSetter<bool> guard (loading, true);
                const auto& fa = slot->follow;
                followOn.setToggleState (fa.enabled, n);
                followA.setSelectedId ((int) fa.a + 1, n);
                followB.setSelectedId ((int) fa.b + 1, n);
                linked.setToggleState (fa.linked, n);
                if (! followTime.hasKeyboardFocus (false))
                    followTime.setText (fieldText (fa.ticks, true, false), false);
                followTime.setEnabled (! fa.linked);
                multiplier->setEnabled (fa.linked);
                const auto jumps = jumpA->isVisible() != (fa.a == FollowKind::jump) || jumpB->isVisible() != (fa.b == FollowKind::jump);
                jumpA->setVisible (fa.a == FollowKind::jump);
                jumpB->setVisible (fa.b == FollowKind::jump);
                for (auto* box : { chanceA.get(), chanceB.get(), multiplier.get(), jumpA.get(), jumpB.get() })
                    box->repaint();
                if (jumps)
                    resized();
            }
            mode.setSelectedId ((int) slot->mode + 1, n);
            quant.setSelectedId ((int) slot->quant + 1, n);
        }
        const bool pattern = c->kind == ClipKind::pattern;
        if (std::exchange (hasNotes, pattern) != pattern)
            if (auto* parent = getParentComponent())
                parent->resized();
        for (auto* b : { &notesTitle })
            b->setVisible (pattern);
        for (auto* b : { &invert, &reverse, &makeLegato, &humanize, &duplicate, &quantize, &randomize, &timesTwo, &divideTwo, &fitScale, &addInterval, &chordHelper })
            b->setVisible (pattern);
        pitchRange->setVisible (pattern);
        intervalSize->setVisible (pattern);
        // the notes' lowest and highest pitch, as the reference's "C3 - C4" [LIVE 12.4 Pitch & Time]
        juce::String newRange;
        if (pattern && c->pattern >= 0 && c->pattern < (int) song.patterns.size() && ! song.patterns[(std::size_t) c->pattern].notes.empty())
        {
            const auto& all = song.patterns[(std::size_t) c->pattern].notes;
            const auto [lo, hi] = std::minmax_element (all.begin(), all.end(), [] (const Note& a, const Note& b) { return a.key < b.key; });
            newRange = juce::String (notenames::keyName (lo->key)) + " - " + juce::String (notenames::keyName (hi->key));
        }
        if (newRange != rangeText)
        {
            rangeText = newRange;
            pitchRange->repaint();
        }
        auto fill = slot != nullptr && slot->colour != 0 ? juce::Colour (slot->colour)
                    : c->kind == ClipKind::audio ? colour::audioClip : tame (PlaylistComponent::patternColour (song, c->pattern));
        chip.set (fill, ! c->muted);
        applyLook();
        if (fill != titleColour)
        {
            titleColour = fill;
            if (liveShown)
            {
                nameColours();
                repaint (0, 0, liveclip::columnWidth, liveclip::headerHeight);
            }
        }
        {
            const juce::ScopedValueSetter<bool> guard (loading, true);
            const auto shown = slot != nullptr ? projectkey::clipScale (*slot, song) : projectkey::get (song);
            // (round 190oj CS-34: with no key, Live's defaults "C" / "Major" with the switch off — before, the switch was lit
            // over two empty choosers)
            scaleRoot.setSelectedId (shown ? shown->root + 1 : 1, n);
            scaleName.setSelectedId (shown ? shown->flScale + 1 : projectkey::commonFirst().front() + 1, n);
            scaleMode.setToggleState (shown.has_value() && (slot == nullptr || slot->scaleOn), n);
            for (auto* box : { &scaleRoot, &scaleName })
                box->setEnabled (pattern);
            scaleMode.setEnabled (pattern);
        }
        // round 190hr: the docked editor's Quantize keys use the MIDI Tools' Quantize tool (Live 12's Edit › Quantize /
        // Quantize Settings…)
        if (auto* roll = editorOf != nullptr ? editorOf() : nullptr)
        {
            const juce::Component::SafePointer<ClipColumn> safe (this);
            roll->onLiveQuantize = [safe] {
                if (safe != nullptr)
                    safe->tools.quantizeNow();
            };
            roll->onLiveQuantizeSettings = [safe] {
                if (safe == nullptr)
                    return;
                if (std::exchange (toolsFolded, false))
                {
                    safe->load();
                    if (auto* parent = safe->getParentComponent())
                        parent->resized();
                }
                safe->tools.showQuantize();
            };
        }
        const auto key = clipKeyOf (song);
        // the docked editor shows the clip's scale (Live: the clip's Scale drives its editor), when it changes
        if (auto* roll = editorOf != nullptr ? editorOf() : nullptr; roll != nullptr && pattern && key != shownKey)
        {
            shownKey = key;
            roll->setScale (key ? key->root : 0, key ? projectkey::lynxScale (*key) : 0);
        }
        tools.setVisible (pattern && ! toolsFolded);
        foldTools.setVisible (pattern);
        foldTools.setButtonText (toolsFolded ? juce::String ("+") : juce::String::fromUTF8 ("\xe2\x88\x92")); // (+ / minus sign)
        foldTools.toFront (false);
        fitScale.setEnabled (key && projectkey::lynxScale (*key) > 0);
        timerCallback(); // (the Set buttons)
    }

    // the clip's own fields (one undo step)
    void apply()
    {
        const auto* c = clip();
        if (c == nullptr)
            return;
        // the field that was typed in decides. Loop off: Start keeps the end, Position keeps the length. Loop on (round
        // 190hr, Live's model): Start moves only the Start marker inside the loop; Position moves the loop and the Start
        // marker stays where it is in the source; End / Length change the loop's end.
        const auto secs = inSeconds (*c);
        const auto& song0 = engine.getSong();
        const auto* slot0 = c->gridSlot >= 0 && c->gridSlot < (int) song0.clipGrid->slots.size() ? &song0.clipGrid->slots[(std::size_t) c->gridSlot] : nullptr;
        const auto looping = slot0 != nullptr && slot0->loop;
        const auto marker0 = slot0 != nullptr ? (std::int64_t) launcher::startLead (*slot0, c->length) : 0;
        const auto end0 = c->offset + c->length;
        auto offset = c->offset, len = c->length;
        auto marker = marker0;
        if (start.getText() != fieldText (c->offset + marker0, false, secs))
        {
            if (const auto t = fieldTicks (start, false, secs); t >= 0 && t < end0)
            {
                if (looping && t >= c->offset)
                    marker = t - c->offset;
                else
                {
                    offset = t;
                    len = end0 - t;
                    marker = 0;
                }
            }
        }
        else if (position.getText() != fieldText (c->offset, false, secs))
        {
            if (const auto t = fieldTicks (position, false, secs); t >= 0)
            {
                offset = t;
                const auto at = c->offset + marker0 - t; // (the Start marker keeps its place in the source)
                marker = at >= 0 && at < len ? at : 0;
            }
        }
        else if (end.getText() != fieldText (end0, false, secs))
        {
            if (const auto e = fieldTicks (end, false, secs); e > offset)
                len = e - offset;
        }
        else if (length.getText() != fieldText (c->length, true, secs))
        {
            if (const auto t = fieldTicks (length, true, secs); t > 0)
                len = t;
        }
        if (marker >= len)
            marker = 0;
        const auto newName = name.getText().trim().toStdString();
        const int t = track, sc = scene;
        engine.editSong ([t, sc, offset, len, marker, newName] (Song& song) {
            if (! song.clipGrid)
                return;
            for (auto& clip : song.clipGrid->clips)
                if (clip.track == t && clip.length > 0 && clip.gridSlot >= 0 && clip.gridSlot < (int) song.clipGrid->slots.size()
                    && song.clipGrid->slots[(std::size_t) clip.gridSlot].scene == sc)
                {
                    clip.offset = offset;
                    clip.length = len;
                    song.clipGrid->slots[(std::size_t) clip.gridSlot].name = newName;
                    song.clipGrid->slots[(std::size_t) clip.gridSlot].startMarker = marker;
                }
        });
        load();
    }

    // round 190hr: the slot's follow action (one undo step) and its typed time
    const ClipFollow* follow() const
    {
        const auto* c = clip();
        const auto& song = engine.getSong();
        return c != nullptr && c->gridSlot >= 0 && c->gridSlot < (int) song.clipGrid->slots.size() ? &song.clipGrid->slots[(std::size_t) c->gridSlot].follow : nullptr;
    }
    void editFollow (std::function<void (ClipFollow&)> fn)
    {
        editSlot ([fn] (ClipGridSlot& slot) { fn (slot.follow); });
    }
    void applyFollowTime()
    {
        const auto* f = follow();
        if (f == nullptr)
            return;
        const auto t = fieldTicks (followTime, true, false);
        if (t > 0 && t != f->ticks)
            editFollow ([t] (ClipFollow& x) { x.ticks = t; });
        else
            load();
    }

    // the grid clip of the slot and its settings (one undo step)
    void editClip (std::function<void (Clip&, ClipGridSlot&)> fn)
    {
        const int t = track, sc = scene;
        engine.editSong ([t, sc, fn] (Song& song) {
            if (! song.clipGrid)
                return;
            for (auto& c : song.clipGrid->clips)
                if (c.track == t && c.length > 0 && c.gridSlot >= 0 && c.gridSlot < (int) song.clipGrid->slots.size()
                    && song.clipGrid->slots[(std::size_t) c.gridSlot].scene == sc)
                    fn (c, song.clipGrid->slots[(std::size_t) c.gridSlot]);
        });
    }

    void editSlot (std::function<void (ClipGridSlot&)> fn)
    {
        const int t = track, sc = scene;
        engine.editSong ([t, sc, fn] (Song& song) {
            if (song.clipGrid)
                if (auto* slot = launcher::settingsAt (*song.clipGrid, t, sc))
                    fn (*slot);
        });
    }

    // round gap190og (detail-019): Live's Duplicate Loop on this MIDI clip (core/ClipLoopOps.h; one undo step)
    void duplicateClipLoop()
    {
        const int t = track, sc = scene;
        engine.editSong ([t, sc] (Song& song) {
            if (auto* c = gridClip (song, t, sc))
                cliploop::duplicateLoop (song, *c);
        });
        load();
    }
    static Clip* gridClip (Song& song, int t, int sc)
    {
        if (! song.clipGrid)
            return nullptr;
        for (auto& c : song.clipGrid->clips)
            if (c.track == t && c.length > 0 && c.gridSlot >= 0 && c.gridSlot < (int) song.clipGrid->slots.size()
                && song.clipGrid->slots[(std::size_t) c.gridSlot].scene == sc)
                return &c;
        return nullptr;
    }
public:
    bool duplicateLoopForCheck()
    {
        if (! ui::showingOrShot (duplicateLoop) || duplicateLoop.getBounds().isEmpty() || ! duplicateLoop.isEnabled())
            return false;
        duplicateClipLoop(); // (what its click does; triggerClick would post it)
        return true;
    }
    juce::Rectangle<int> duplicateLoopBoundsForCheck() const { return duplicateLoop.getBounds(); }
private:

    // a note tool on every note of the clip's pattern (one undo step)
    void notes (std::function<void (Pattern&, const std::vector<int>&, const Song&)> fn)
    {
        const auto* c = clip();
        if (c == nullptr || c->kind != ClipKind::pattern)
            return;
        engine.editSong ([pattern = c->pattern, fn] (Song& song) {
            if (pattern < 0 || pattern >= (int) song.patterns.size())
                return;
            auto& p = song.patterns[(std::size_t) pattern];
            std::vector<int> all ((std::size_t) p.notes.size());
            for (int i = 0; i < (int) all.size(); ++i)
                all[(std::size_t) i] = i;
            fn (p, all, song);
            noteedit::sortTracking (p, all);
        });
    }

    // what the MIDI Tools change: the docked Piano roll's selection (or all its channel's notes) while it shows this
    // clip's pattern; otherwise every note of the pattern
    MidiToolsPanel::Target toolTarget() const
    {
        MidiToolsPanel::Target t;
        const auto* c = clip();
        const auto& song = engine.getSong();
        if (c == nullptr || c->kind != ClipKind::pattern || c->pattern < 0 || c->pattern >= (int) song.patterns.size())
            return t;
        t.pattern = c->pattern;
        if (auto* roll = editorOf != nullptr ? editorOf() : nullptr; roll != nullptr && song.currentPattern == c->pattern)
        {
            t.indices = roll->midiToolNotes (t.selected);
            t.grid = roll->midiToolGrid();
            t.from = roll->timeSelectionFrom(); // round 190fm: Generate writes into the time selection…
            t.to = roll->timeSelectionTo();
        }
        if (t.to <= t.from && c->length > 0) // …else the part of the pattern the clip plays (Live: the clip's loop)
        {
            t.from = std::max<std::int64_t> (0, c->offset);
            t.to = t.from + c->length;
        }
        else
        {
            t.indices.resize (song.patterns[(std::size_t) c->pattern].notes.size());
            for (int i = 0; i < (int) t.indices.size(); ++i)
                t.indices[(std::size_t) i] = i;
        }
        return t;
    }

    AudioEngine& engine;
    int track = -1, scene = -1;
    bool hasNotes = true;
    MidiToolsPanel tools;
    juce::TextButton foldTools;
    ClipChip chip;
    std::unique_ptr<ui::NumBox> pitchRange, intervalSize;
    juce::TextButton addInterval;
    juce::String rangeText;
    int transposed = 0, interval = 0;
    juce::Label clipTitle, launchTitle, notesTitle, nameLabel, startLabel, endLabel, lengthLabel, signatureLabel, modeLabel, quantLabel, signature;
    juce::TextEditor name, start, end, position, length;
    juce::String nameFallback; // (round 190oj CS-33: the name shown while the clip has none of its own)
    juce::TextButton setStart, setEnd, setPosition, setLength; // round 190dr
    juce::Label positionLabel, grooveLabel;
    juce::ComboBox groove;
    GrooveCommitButton commitGroove; // round 190kc: Live's Commit button beside the Groove label
    bool loading = false;
    std::unique_ptr<ui::TimerWhileShowing> showingTimer;
    juce::ToggleButton loop, legato;
    // round gap190og (detail-019): Live's Duplicate Loop button of the Clip panel (its Live column only; MIDI clips)
    liveclip::PillButton duplicateLoop { "duplicateLoop" };
    // round 190hr: the Follow Action controls
    juce::Label followTitle;
    juce::TextButton followOn, linked;
    juce::ComboBox followA, followB;
    std::unique_ptr<ui::NumBox> chanceA, chanceB, multiplier, jumpA, jumpB;
    juce::TextEditor followTime;
    juce::ComboBox mode, quant;
    juce::TextButton invert, reverse, makeLegato, humanize, duplicate, quantize, randomize, timesTwo, divideTwo, fitScale, chordHelper;
    juce::Label scaleLabel;
    juce::ComboBox scaleRoot, scaleName; // the clip's scale (round 190hr; before: the project key)
    juce::TextButton scaleMode;          // Live's Scale Mode switch of the clip (IsInKey)
    std::optional<projectkey::Key> shownKey; // the scale last handed to the docked editor
    // the key the clip's tools use: its own scale (or the project's) while its Scale Mode is on
    std::optional<projectkey::Key> clipKeyOf (const Song& song) const
    {
        const auto* c = clip();
        if (c == nullptr || ! song.clipGrid || c->gridSlot < 0 || c->gridSlot >= (int) song.clipGrid->slots.size())
            return projectkey::get (song);
        return projectkey::clipKey (song.clipGrid->slots[(std::size_t) c->gridSlot], song);
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// The Clip View (slice 1: the selected slot's clip and a button to its editor; slice 2 docks the editors here).
class DetailView::ClipPage final : public juce::Component, private juce::ChangeListener
{
public:
    bool duplicateLoopForCheck() { return column.isVisible() && column.duplicateLoopForCheck(); } // (round gap190og)
    ClipPage (AudioEngine& e, Hooks& h) : engine (e), hooks (h), column (e)
    {
        addChildComponent (column); // (slice 2b: shown while a grid clip is)
        column.editorOf = [this] { return dynamic_cast<PianoRollComponent*> (editor); };
        for (auto* b : { &notesTab, &envelopesTab, &mpeTab })
        {
            addChildComponent (*b);
            b->setConnectedEdges (0);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, colour::accent);
            b->setColour (juce::TextButton::textColourOnId, colour::onAccent);
        }
        notesTab.setButtonText (tr ("Notes"));
        envelopesTab.setButtonText (tr ("Envelopes"));
        envelopesTab.setTooltip (tr ("The pattern's events: the levels and controls it changes while it plays"));
        mpeTab.setButtonText (tr ("MPE"));
        mpeTab.setTooltip (tr ("Each note's own pitch bend, slide and pressure curves (MPE)"));
        notesTab.onClick = [this] { showTab (false); };
        envelopesTab.onClick = [this] { showTab (true); };
        mpeTab.onClick = [this] { showTab (false, true); };
        addChildComponent (open);
        open.onClick = [this] {
            if (dockable())
            {
                takenBack = false;
                update();
            }
            else if (hooks.openClip != nullptr)
                hooks.openClip (track, scene);
        };
        engine.addChangeListener (this);
    }
    ~ClipPage() override
    {
        engine.removeChangeListener (this);
        release();
    }

    void setSlot (int t, int s)
    {
        if (t != track || s != scene)
            takenBack = false; // (a new clip docks again)
        track = t;
        scene = s;
        column.setSlot (t, s);
        update();
    }
    MidiToolsPanel& midiTools() noexcept { return column.midiTools(); }
    void sectionsForSnapshot (bool clipOpen, bool launchOpen, bool notesOpen, int scroll)
    {
        ClipColumn::clipOpen = clipOpen;
        ClipColumn::launchOpen = launchOpen;
        ClipColumn::notesOpen = notesOpen;
        column.setScrollForSnapshot (scroll);
    }
    void editorTakenBack()
    {
        if (editor != nullptr)
        {
            editor = nullptr; // (its panel has it now)
            dockedTrack = dockedScene = -1;
            takenBack = true;
        }
        update();
    }
    void showTab (bool envelopes, bool mpe = false)
    {
        DetailView::envelopesTab = envelopes;
        DetailView::mpeTab = mpe && ! envelopes;
        if (! envelopes)
            takenBack = false; // (Notes / MPE: the Piano roll docks again)
        update();
    }
    // (round 190hy) the docked Piano roll shows the MPE lanes while the MPE tab does
    void applyMpeView()
    {
        if (auto* roll = dynamic_cast<PianoRollComponent*> (editor))
        {
            roll->setMpeView (DetailView::mpeTab && ! DetailView::envelopesTab);
            // round 190jn (CL-20b): in Live's look the docked notes take the clip's colour (PianoRollComponent::liveNotes)
            const auto* clip = clipShown();
            const auto colour = theme::livelook::active() && clip != nullptr ? clipColourOf (*clip) : juce::Colours::transparentBlack;
            if (roll->liveNoteColour != colour)
            {
                roll->liveNoteColour = colour;
                roll->liveFooterCheck(); // (round gap190og: Live's Velocity footer comes with the Live notes)
                roll->repaint();
            }
        }
    }
    // the clip's colour as its title bar shows it (ClipColumn::load's rule)
    juce::Colour clipColourOf (const Clip& c) const
    {
        const auto& song = engine.getSong();
        const ClipGridSlot* slot = song.clipGrid && c.gridSlot >= 0 && c.gridSlot < (int) song.clipGrid->slots.size() ? &song.clipGrid->slots[(std::size_t) c.gridSlot] : nullptr;
        return slot != nullptr && slot->colour != 0 ? juce::Colour (slot->colour)
               : c.kind == ClipKind::audio ? colour::audioClip : tame (PlaylistComponent::patternColour (song, c.pattern));
    }

    // Round 190im (R13, DL-74): the docked clip's position in its pattern while it is launched from the grid (the
    // launcher clock − its anchor, wrapped at its length, from its start offset); −1 when it does not play
    double launchedTick() const
    {
        const auto* state = engine.launcherState();
        if (state == nullptr || dockedTrack < 0 || dockedTrack >= launcher::maxTracks || ! state->running.load (std::memory_order_relaxed))
            return -1.0;
        const auto& view = state->tracks[(std::size_t) dockedTrack];
        if (view.playing.load (std::memory_order_relaxed) != dockedScene)
            return -1.0;
        const auto& song = engine.getSong();
        const auto* clip = song.clipGrid ? launcher::clipAt (*song.clipGrid, dockedTrack, dockedScene) : nullptr;
        if (clip == nullptr || clip->kind != ClipKind::pattern || clip->length <= 0)
            return -1.0;
        return (double) clip->offset + std::fmod (juce::jmax (0.0, state->clockNow.load (std::memory_order_relaxed) + engine.playheadAdvanceTicks()
                                                                       - view.anchor.load (std::memory_order_relaxed)), (double) clip->length);
    }

    void release()
    {
        dropEvents();
        dockedTrack = dockedScene = -1;
        if (auto* roll = dynamic_cast<PianoRollComponent*> (editor)) // (round 190jn: its own colours again at home)
        {
            roll->liveNoteColour = juce::Colours::transparentBlack;
            roll->liveFooterCheck(); // (round gap190og: no Live footer outside the dock)
        }
        if (editor != nullptr && hooks.undockPianoRoll != nullptr)
        {
            editor = nullptr;
            hooks.undockPianoRoll();
        }
        editor = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (liveTabs ? theme::livelook::window : colour::bg1);
        if (liveTabs && notesTab.isVisible()) // (round 190jn: Live's 17 px tab bar over the editor, #363636)
        {
            g.setColour (theme::livelook::surface);
            g.fillRect (tabBar);
            g.setColour (theme::livelook::separator);
            g.fillRect (tabBar.getX(), tabBar.getBottom(), tabBar.getWidth(), 1);
        }
        if (editor != nullptr || events != nullptr)
            return;
        g.setColour (colour::textSecondary);
        g.setFont (font (type::body));
        g.drawText (text, getLocalBounds().withTrimmedLeft (column.isVisible() ? column.preferredWidth() : 0).withTrimmedBottom (40),
                    juce::Justification::centred, true);
    }
    void resized() override
    {
        auto area = getLocalBounds();
        if (column.isVisible())
            column.setBounds (area.removeFromLeft (column.preferredWidth()));
        open.setBounds (area.withSizeKeepingCentre (240, 26).translated (0, 16));
        if (audioPanel != nullptr) // round 190cg (ui/ClipAudioPanel)
            audioPanel->setBounds (area);
        applyLook();
        tabBar = {};
        if (notesTab.isVisible() && liveTabs)
        {
            // round 190jn (CL-20b): Live's tab bar — 17 px #363636 across the editor, the tabs (60 px, MPE 40 px) centred,
            // the shown one #464646 (live-01-clipview-notes.png 691..873, live-clipview-audio.png 962..1082)
            tabBar = area.removeFromTop (liveclip::headerHeight);
            area.removeFromTop (1);
            const auto total = 60 + 1 + 60 + 1 + 40;
            auto bar = tabBar.withSizeKeepingCentre (total, liveclip::headerHeight);
            notesTab.setBounds (bar.removeFromLeft (60));
            bar.removeFromLeft (1);
            envelopesTab.setBounds (bar.removeFromLeft (60));
            bar.removeFromLeft (1);
            mpeTab.setBounds (bar);
        }
        else if (notesTab.isVisible())
        {
            // the clip's tabs, top right, as the reference's Notes | Envelopes [SHOT d08] (| MPE: Live 11+, round 190hy)
            auto bar = area.removeFromTop (tabHeight).reduced (2, 2);
            mpeTab.setBounds (bar.removeFromRight (72));
            bar.removeFromRight (2);
            envelopesTab.setBounds (bar.removeFromRight (96));
            bar.removeFromRight (2);
            notesTab.setBounds (bar.removeFromRight (96));
        }
        if (events != nullptr)
            events->setBounds (area);
        if (editor != nullptr)
        {
            editor->setBounds (area);
            if (auto* sharer = dynamic_cast<SharesTitleBar*> (editor))
                sharer->setTitleBarInsets (0, 0); // (its toolbar runs across the top, there is no title bar here)
        }
    }
    void visibilityChanged() override { update(); }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { update(); }

    // round 190jn: the tabs in Live's colours (the shown one #464646, the others #363636, text #B5B5B5) and square, or
    // LYNX's back; follows the layout's switch
    bool liveTabs = false;
    juce::Rectangle<int> tabBar;
    void applyLook()
    {
        const auto live = theme::livelook::active();
        if (live == liveTabs)
            return;
        liveTabs = live;
        for (auto* b : { &notesTab, &envelopesTab, &mpeTab })
        {
            liveclip::swapColour (*b, juce::TextButton::buttonColourId, theme::livelook::surface, live);
            liveclip::swapColour (*b, juce::TextButton::buttonOnColourId, liveclip::header, live);
            liveclip::swapColour (*b, juce::TextButton::textColourOffId, theme::livelook::text, live);
            liveclip::swapColour (*b, juce::TextButton::textColourOnId, theme::livelook::text, live);
            b->setConnectedEdges (live ? juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight : 0);
        }
    }

    // this page and every parent visible (not isShowing(): an off-screen --detailshot has no window, and a minimised
    // window keeps its dock)
    bool onScreen() const
    {
        for (const juce::Component* c = this; c != nullptr; c = c->getParentComponent())
            if (! c->isVisible())
                return false;
        return getParentComponent() != nullptr;
    }

    const Clip* clipShown() const
    {
        const auto& song = engine.getSong();
        return song.clipGrid && track >= 0 && scene >= 0 ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    }
    bool dockable() const
    {
        const auto* clip = clipShown();
        return clip != nullptr && clip->kind == ClipKind::pattern && hooks.dockPianoRoll != nullptr;
    }

    // The Event editor of the clip's pattern, made for this slot (its own target box picks another target): preset to
    // the pattern's first event lane, else the volume of the track's channel (or of the pattern's first channel).
    void showEvents (const Clip& clip)
    {
        if (events != nullptr && eventsTrack == track && eventsScene == scene)
            return;
        dropEvents();
        const auto& song = engine.getSong();
        if (hooks.selectPattern != nullptr && clip.pattern != song.currentPattern)
            hooks.selectPattern (clip.pattern);
        AutomationTarget target;
        if (clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && ! song.patterns[(std::size_t) clip.pattern].events.empty())
            target = song.patterns[(std::size_t) clip.pattern].events.front().target;
        else
        {
            const auto chain = detailchain::forTrack (song, track);
            auto channel = chain.channel;
            if (channel < 0 && clip.pattern >= 0 && clip.pattern < (int) song.patterns.size() && ! song.patterns[(std::size_t) clip.pattern].notes.empty())
                channel = song.patterns[(std::size_t) clip.pattern].notes.front().channel;
            target = AutomationTarget { AutomationTargetKind::channelVolume, std::max (0, channel) };
        }
        events = makeEventEditor (engine, target);
        eventsTrack = track;
        eventsScene = scene;
        addAndMakeVisible (*events);
        resized();
    }
    void dropEvents()
    {
        if (events != nullptr)
            removeChildComponent (events.get());
        events.reset();
        eventsTrack = eventsScene = -1;
    }

    void update()
    {
        if (theme::livelook::active() != liveTabs)
        {
            applyLook();
            resized();
        }
        const auto& song = engine.getSong();
        const auto* clip = clipShown();
        if (column.isVisible() != (clip != nullptr))
        {
            column.setVisible (clip != nullptr);
            resized();
        }
        // a pattern clip, on screen: the Piano roll docks here (unless its panel took it back since this clip was picked)
        const bool patternClip = clip != nullptr && clip->kind == ClipKind::pattern;
        const bool tabs = patternClip && onScreen();
        if (tabs != notesTab.isVisible())
        {
            notesTab.setVisible (tabs);
            envelopesTab.setVisible (tabs);
            mpeTab.setVisible (tabs);
            resized();
        }
        notesTab.setToggleState (! DetailView::envelopesTab && ! DetailView::mpeTab, juce::dontSendNotification);
        envelopesTab.setToggleState (DetailView::envelopesTab, juce::dontSendNotification);
        mpeTab.setToggleState (DetailView::mpeTab && ! DetailView::envelopesTab, juce::dontSendNotification);
        if (tabs && DetailView::envelopesTab)
        {
            if (editor != nullptr && hooks.undockPianoRoll != nullptr) // (the Piano roll goes home while Envelopes shows)
            {
                if (auto* roll = dynamic_cast<PianoRollComponent*> (editor))
                    roll->setMpeView (false);
                editor = nullptr;
                dockedTrack = dockedScene = -1;
                hooks.undockPianoRoll();
            }
            showEvents (*clip);
            open.setVisible (false);
            repaint();
            return;
        }
        dropEvents();
        if (clip != nullptr && clip->kind == ClipKind::pattern && onScreen() && ! takenBack && hooks.dockPianoRoll != nullptr)
        {
            if (editor != nullptr && dockedTrack == track && dockedScene == scene)
            {
                applyMpeView();
                return; // (already showing this clip: the user may have picked another pattern in it meanwhile)
            }
            dockedTrack = track;
            dockedScene = scene;
            auto* e = hooks.dockPianoRoll (clip->pattern);
            if (auto* roll = dynamic_cast<PianoRollComponent*> (e)) // (round 190im, R13: the launched clip's position)
                roll->launchedPlayhead = [safe = juce::Component::SafePointer<ClipPage> (this), roll] {
                    return safe != nullptr && safe->editor == roll ? safe->launchedTick() : -1.0;
                };
            if (e != nullptr && e != editor)
            {
                editor = e;
                addAndMakeVisible (*editor);
                column.reload(); // (round 190hr: the column gives the docked editor its Quantize keys and the clip's scale)
                resized();
            }
            applyMpeView();
            open.setVisible (false);
            repaint();
            return;
        }
        if (editor != nullptr && (! onScreen() || clip == nullptr || clip->kind != ClipKind::pattern))
            release();

        // round 190cg: an audio clip shows its Audio panel and the audio editor (ui/ClipAudioPanel)
        const auto audioKey = clip != nullptr && clip->kind == ClipKind::audio && onScreen() ? (track * 2000 + scene) * 4096 + (clip->channel & 4095) : -1;
        if (audioKey != audioPanelKey)
        {
            audioPanel.reset();
            audioPanelKey = audioKey;
            if (audioKey >= 0)
                if (audioPanel = makeClipAudioPanel (engine, track, scene); audioPanel != nullptr)
                {
                    addAndMakeVisible (*audioPanel);
                    resized();
                }
        }
        if (audioPanel != nullptr)
        {
            open.setVisible (false);
            repaint();
            return;
        }

        if (clip == nullptr)
        {
            text = tr ("Select a clip in the clip grid to see it here");
            open.setVisible (false);
        }
        else if (clip->kind == ClipKind::audio)
        {
            text = clip->channel >= 0 && clip->channel < (int) song.channels.size() ? utf8 (song.channels[(std::size_t) clip->channel].name) : tr ("Audio clip");
            open.setButtonText (tr ("Open in the audio editor"));
            open.setVisible (true);
        }
        else
        {
            text = clip->pattern >= 0 && clip->pattern < (int) song.patterns.size() ? utf8 (song.patterns[(std::size_t) clip->pattern].name) : tr ("Pattern");
            if (takenBack)
                text << " - " << tr ("the piano roll is in its own panel now");
            open.setButtonText (takenBack ? tr ("Dock the piano roll here") : tr ("Open in the piano roll"));
            open.setVisible (true);
        }
        repaint();
    }

    AudioEngine& engine;
    Hooks& hooks;
    juce::TextButton open;
    juce::String text;
    ClipColumn column;
    juce::TextButton notesTab, envelopesTab, mpeTab;
    static constexpr int tabHeight = 26;
    std::unique_ptr<juce::Component> events; // the Envelopes tab's Event editor
    int eventsTrack = -1, eventsScene = -1;
    juce::Component* editor = nullptr; // the docked Piano roll (not owned)
    std::unique_ptr<juce::Component> audioPanel; // round 190cg: an audio clip's panel and editor
    int audioPanelKey = -1;
    int dockedTrack = -1, dockedScene = -1;
    bool takenBack = false;
    int track = -1, scene = -1;
};

// ---------------------------------------------------------------------------------------------------------------------
DetailView::DetailView (AudioEngine& e, Hooks h) : engine (e), hooks (std::move (h))
{
    setTitle (tr ("Detail view"));
    info = std::make_unique<ui::InfoView>();
    addChildComponent (*info);
    info->setInterceptsMouseClicks (false, false);
    chain = std::make_unique<DeviceChain> (engine, hooks);
    clipPage = std::make_unique<ClipPage> (engine, hooks);
    addChildComponent (*chain);
    addChildComponent (*clipPage);
    for (auto* b : { &clipTab, &devicesTab, &optionsButton })
    {
        addAndMakeVisible (*b);
        b->setConnectedEdges (0);
        b->setColour (juce::TextButton::buttonOnColourId, colour::accent);
        b->setColour (juce::TextButton::textColourOnId, colour::onAccent);
    }
    clipTab.setButtonText (tr ("Clip"));
    devicesTab.setButtonText (tr ("Devices"));
    clipTab.setTooltip (tr ("The selected clip (Shift+Tab switches, Ctrl+Alt+3)"));
    devicesTab.setTooltip (tr ("The selected track's devices: its instrument and its insert's effects (Shift+Tab switches, Ctrl+Alt+4)"));
    clipTab.onClick = [this] { setPage (Page::clip); };
    devicesTab.onClick = [this] { setPage (Page::devices); };
    optionsButton.setButtonText (juce::String::fromUTF8 ("\xe2\x8b\xaf"));
    optionsButton.setTooltip (tr ("Detail view options"));
    optionsButton.onClick = [this] { showOptionsMenu(); };
    showPage();
}

DetailView::~DetailView() = default;

void DetailView::showSelection (int t, int s)
{
    track = t;
    scene = s;
    chain->setTrack (t);
    clipPage->setSlot (t, s);
}

void DetailView::setPage (Page page)
{
    devicesPage = page == Page::devices;
    if (! shownSetting)
    {
        shownSetting = true;
        setVisible (true);
    }
    showPage();
    if (hooks.layoutChanged != nullptr)
        hooks.layoutChanged();
}

void DetailView::togglePage() { setPage (devicesPage ? Page::clip : Page::devices); }

void DetailView::editorTakenBack() { clipPage->editorTakenBack(); }
void DetailView::releaseEditor() { clipPage->release(); }
void DetailView::setClipTab (bool envelopes) { clipPage->showTab (envelopes); }
void DetailView::setMpeTab() { clipPage->showTab (false, true); }
bool DetailView::duplicateLoopForCheck() { return clipPage != nullptr && clipPage->duplicateLoopForCheck(); } // (round gap190og)

void DetailView::showPage()
{
    chain->setVisible (devicesPage);
    clipPage->setVisible (! devicesPage);
    clipTab.setToggleState (! devicesPage, juce::dontSendNotification);
    devicesTab.setToggleState (devicesPage, juce::dontSendNotification);
    repaint();
}

int DetailView::heightFor (int available) const noexcept
{
    return juce::jlimit (std::min (minHeight, available / 2), std::max (minHeight, available * 6 / 10), preferredHeight);
}

void DetailView::showOptionsMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("LYNX extras"));
    menu.addItem (tr ("W shows or hides the selected device's window"), true, extraWindowKey, [this] {
        extraWindowKey = ! extraWindowKey;
        if (hooks.layoutChanged != nullptr)
            hooks.layoutChanged();
    });
    menu.addItem (tr ("Selecting a device selects its insert in the Mixer"), true, extraSelectInMixer, [this] {
        extraSelectInMixer = ! extraSelectInMixer;
        if (hooks.layoutChanged != nullptr)
            hooks.layoutChanged();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton));
}

void DetailView::paint (juce::Graphics& g)
{
    if (theme::livelook::active())
    {
        paintLiveStrip (g);
        return;
    }
    g.fillAll (colour::bg1);
    // the top edge: drag to resize
    g.setColour (colour::bg3);
    g.fillRect (0, 0, getWidth(), edgeHeight);
    g.setColour (colour::border);
    g.fillRect (0, edgeHeight - 1, getWidth(), 1);
    g.setColour (colour::bg5);
    g.fillRect (getWidth() / 2 - 16, 2, 32, 1);
    // the bottom strip: what is shown, then the selectors on the right
    const auto strip = getLocalBounds().removeFromBottom (stripHeight);
    g.setColour (colour::border);
    g.fillRect (strip.withHeight (1));
    g.setColour (colour::textSecondary);
    g.setFont (font (type::small));
    const auto& song = engine.getSong();
    juce::String what;
    if (devicesPage)
    {
        what = track >= 0 ? tr ("Devices") + ": " + trackName (song, track) : tr ("Devices");
        if (const auto i = chain->insertShown(); i >= 0)
            what << juce::String::fromUTF8 (" \xc2\xb7 ") << insertName (song, i);
    }
    else
        what = tr ("Clip");
    what << "   (" << tr ("Shift+Tab: {0}", { devicesPage ? tr ("Clip") : tr ("Devices") }) << ")";
    g.drawText (what, strip.reduced (8, 0).withTrimmedRight (240), juce::Justification::centredLeft, true);
}

void DetailView::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (edgeHeight);
    const auto live = theme::livelook::active();
    clipTab.setVisible (! live);
    devicesTab.setVisible (! live);
    info->setVisible (live && infoViewShown);
    if (live)
    {
        // Live: the status strip under everything; the Info View 9..240 then the views from 249 (live-session.png)
        r.removeFromBottom (stripHeight);
        optionsButton.setBounds (getWidth() - 28, getHeight() - stripHeight + 2, 26, stripHeight - 4);
        if (infoViewShown)
        {
            auto left = r.removeFromLeft (249);
            info->setBounds (left.withTrimmedLeft (9).withTrimmedRight (9).withTrimmedTop (2).withTrimmedBottom (1));
        }
        chain->setBounds (r);
        clipPage->setBounds (r);
        return;
    }
    auto strip = r.removeFromBottom (stripHeight).reduced (2, 2);
    optionsButton.setBounds (strip.removeFromRight (26));
    strip.removeFromRight (4);
    devicesTab.setBounds (strip.removeFromRight (96));
    strip.removeFromRight (2);
    clipTab.setBounds (strip.removeFromRight (96));
    chain->setBounds (r);
    clipPage->setBounds (r);
}

void DetailView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (e.y < edgeHeight ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void DetailView::mouseDown (const juce::MouseEvent& e)
{
    resizing = e.y < edgeHeight;
    dragStartHeight = getHeight();
    if (resizing || ! theme::livelook::active() || e.mods.isPopupMenu())
        return;
    if (infoButtonArea().expanded (2).contains (e.getPosition()))
        toggleInfoView();
    else if (pageGlyphArea (false).expanded (1).contains (e.getPosition()))
        setPage (Page::clip);
    else if (pageGlyphArea (true).expanded (1).contains (e.getPosition()))
        setPage (Page::devices);
}

// --- round 190jn: Live's Info View and status strip (design190ia CL-23 / CL-24) -----------------------------------------
void DetailView::setInfoHint (const ui::Hint& hint)
{
    if (info->isVisible())
        info->setHint (hint);
}

void DetailView::toggleInfoView()
{
    infoViewShown = ! infoViewShown;
    resized();
    repaint();
    if (hooks.layoutChanged != nullptr)
        hooks.layoutChanged();
}

ui::InfoView& DetailView::infoViewForSnapshot() { return *info; }

// Live 12.4's status bar (live-session.png y 1004..1022): the (i) button 15 px at x 11 (orange: the Info View shows),
// the status text in a #363636 field from x 42; at the right the selected track's name and the view selector
// (here: a Clip glyph and a Device glyph, 17 px, the shown one lit #FFAD56); LYNX's "..." options last.
juce::Rectangle<int> DetailView::infoButtonArea() const
{
    return { 11, getHeight() - stripHeight + (stripHeight - 15) / 2, 15, 15 };
}

juce::Rectangle<int> DetailView::pageGlyphArea (bool devices) const
{
    const auto right = getWidth() - 34;
    return { right - (devices ? 19 : 38), getHeight() - stripHeight + (stripHeight - 17) / 2, 17, 17 };
}

void DetailView::paintLiveStrip (juce::Graphics& g)
{
    namespace ll = theme::livelook;
    g.fillAll (ll::window);
    // the top edge: drag to resize (a 1 px #464646 line as Live's split, the grip in the middle)
    g.setColour (ll::separator);
    g.fillRect (0, edgeHeight - 1, getWidth(), 1);
    g.setColour (ll::raised);
    g.fillRect (getWidth() / 2 - 16, 2, 32, 1);
    const auto strip = getLocalBounds().removeFromBottom (stripHeight);
    // (i)
    const auto ib = infoButtonArea().toFloat();
    g.setColour (infoViewShown ? ll::on : ll::well);
    g.fillEllipse (ib);
    g.setColour (infoViewShown ? ll::ink : ll::text);
    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre ({ ib.getCentreX(), ib.getY() + 4.0f }));
    g.fillRect (juce::Rectangle<float> (ib.getCentreX() - 1.0f, ib.getY() + 6.0f, 2.0f, 6.0f));
    // the status text field
    const auto& song = engine.getSong();
    const auto glyphs = pageGlyphArea (false).getX();
    const auto name = track >= 0 ? trackName (song, track) : juce::String();
    g.setFont (font (type::small));
    static juce::String measuredName; // (the name's width, measured once per name)
    static int measuredWidth = 0;
    if (name != measuredName)
    {
        measuredName = name;
        measuredWidth = name.isEmpty() ? 0 : juce::jmin (180, juce::roundToInt (juce::TextLayout::getStringWidth (g.getCurrentFont(), name)) + 12);
    }
    const auto nameWidth = measuredWidth;
    auto field = juce::Rectangle<int> (42, strip.getY() + (stripHeight - 19) / 2, std::max (0, glyphs - nameWidth - 12 - 42), 19);
    g.setColour (ll::surface);
    g.fillRoundedRectangle (field.toFloat(), 3.0f);
    juce::String what;
    if (devicesPage)
    {
        what = track >= 0 ? tr ("Devices") + ": " + trackName (song, track) : tr ("Devices");
        if (const auto i = chain->insertShown(); i >= 0)
            what << juce::String::fromUTF8 (" \xc2\xb7 ") << insertName (song, i);
    }
    else
        what = tr ("Clip");
    what << "   (" << tr ("Shift+Tab: {0}", { devicesPage ? tr ("Clip") : tr ("Devices") }) << ")";
    g.setColour (ll::text);
    textcache::drawText (g, what, field.reduced (6, 0), juce::Justification::centredLeft, true);
    if (nameWidth > 0)
        textcache::drawText (g, name, juce::Rectangle<int> (glyphs - nameWidth - 6, strip.getY(), nameWidth, stripHeight), juce::Justification::centredLeft, true);
    // the Clip glyph (a clip block with its waveform) and the Device glyph (Live's three bars)
    for (const auto devices : { false, true })
    {
        const auto r = pageGlyphArea (devices).toFloat();
        const auto lit = devicesPage == devices;
        g.setColour (lit ? ll::on : ll::well);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (lit ? ll::ink : ll::text);
        if (devices)
            for (int k = 0; k < 3; ++k)
            {
                const auto h = k == 1 ? 9.0f : 6.0f;
                g.fillRect (juce::Rectangle<float> (r.getX() + 4.0f + (float) k * 3.5f, r.getBottom() - 4.0f - h, 2.0f, h));
            }
        else
        {
            const auto b = r.reduced (3.5f, 5.0f);
            g.drawRect (b, 1.0f);
            g.drawLine (b.getX() + 2.0f, b.getCentreY(), b.getRight() - 2.0f, b.getCentreY(), 1.0f);
        }
    }
}

juce::String DetailView::getTooltip()
{
    if (! theme::livelook::active())
        return {};
    const auto p = getMouseXYRelative();
    if (infoButtonArea().expanded (2).contains (p))
        return tr ("Info View: shows what the control under the mouse does. Click or press Shift+? to show or hide it");
    if (pageGlyphArea (false).expanded (1).contains (p))
        return clipTab.getTooltip();
    if (pageGlyphArea (true).expanded (1).contains (p))
        return devicesTab.getTooltip();
    return {};
}

void DetailView::mouseDrag (const juce::MouseEvent& e)
{
    if (! resizing)
        return;
    preferredHeight = std::max (minHeight, dragStartHeight - e.getDistanceFromDragStartY());
    if (hooks.layoutChanged != nullptr)
        hooks.layoutChanged();
}

void DetailView::mouseUp (const juce::MouseEvent&)
{
    if (resizing && getParentComponent() != nullptr)
        preferredHeight = getHeight(); // (what the dock allowed)
    resizing = false;
}

MidiToolsPanel& DetailView::midiToolsForSnapshot() { return clipPage->midiTools(); }
void DetailView::clipSectionsForSnapshot (bool clipOpen, bool launchOpen, bool notesOpen, int scroll) { clipPage->sectionsForSnapshot (clipOpen, launchOpen, notesOpen, scroll); }
hotswap::Target DetailView::selectedDeviceTarget() const { return devicesPage && isShowing() ? chain->selectedTarget() : hotswap::Target(); }
void DetailView::hotSwapChanged() { chain->repaint(); }
void DetailView::selectCardForCheck (int card) { chain->selectCard (card); }
// round gap190og (deviceview-0012): the main window's Edit menu (Clip-launch layout) on the Device View
bool DetailView::devicesHaveFocus (const juce::Component* focused) const
{
    return chain != nullptr && chain->isVisible() && ui::showingOrShot (*chain) && focused != nullptr && (focused == chain.get() || chain->isParentOf (focused));
}
bool DetailView::canEditDevice (EditCommand command) const
{
    return chain != nullptr && chain->isVisible() && ui::showingOrShot (*chain) && chain->canEdit ((DeviceChain::EditCommand) command);
}
bool DetailView::editDevice (EditCommand command)
{
    return chain != nullptr && chain->isVisible() && ui::showingOrShot (*chain) && chain->runEdit ((DeviceChain::EditCommand) command);
}
bool DetailView::duplicateCardForCheck (int card) { return chain->duplicateCard (card); }
bool DetailView::groupCardForCheck (int card, bool ungroup) { return chain->groupCardForCheck (card, ungroup); }
juce::PopupMenu DetailView::cardMenuForSnapshot (int card)
{
    juce::PopupMenu menu;
    EffectsPanel::slotMenuForSnapshot = &menu;
    chain->showCardMenuForSnapshot (card);
    EffectsPanel::slotMenuForSnapshot = nullptr;
    return menu;
}
int DetailView::cardCountForCheck() const { return chain->cardCount(); }
juce::Rectangle<int> DetailView::windowButtonForCheck (int card) const
{
    const auto r = chain->windowButtonOf (card);
    return r.isEmpty() ? r : getLocalArea (chain.get(), r);
}
bool DetailView::pressWindowButtonForCheck (int card) { return chain->pressWindowButtonForCheck (card); }
int DetailView::chainInsertForCheck() const { return chain->insertShown(); }
double DetailView::knobValueForCheck (int card, int knob) const { return chain->knobValueForCheck (card, knob); }
int DetailView::chainChannelForCheck() const { return chain->channelShown(); }
bool DetailView::isFoldedForCheck (int card) const { return chain->isCardFolded (card); }
void DetailView::toggleFoldForCheck (int card) { chain->toggleCardFold (card); }
void DetailView::deviceLevelsForSnapshot (float left, float right) { chain->deviceLevelsForSnapshot (left, right); }
// ---------------------------------------------------------------------------------------------------------------------
// Round 190dr: the description of a device card dragged out of the Device View
namespace
{
constexpr const char* deviceDragTag = "lynx-device";
}

juce::String deviceDragDescription (int insert, int slot)
{
    return juce::String (deviceDragTag) + "|" + juce::String (insert) + "|" + juce::String (slot);
}

bool detailDeviceDrag (const juce::var& description, int& insert, int& slot)
{
    const auto parts = juce::StringArray::fromTokens (description.toString(), "|", "");
    if (parts.size() != 3 || parts[0] != deviceDragTag)
        return false;
    insert = parts[1].getIntValue();
    slot = parts[2].getIntValue();
    return insert >= 0 && slot >= 0;
}
} // namespace lynx
