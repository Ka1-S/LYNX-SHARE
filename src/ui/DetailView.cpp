#include "ui/DetailView.h"
#include "core/StandIns.h"
#include "ui/MapMode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>

#include "core/ClipLauncher.h"
#include "core/ClipSample.h"
#include "core/DetailChain.h"
#include "core/Groove.h"
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
#include "ui/PianoRollComponent.h"
#include "ui/PlaylistComponent.h"
#include "ui/PluginHeader.h"
#include "ui/PluginThumbs.h"
#include "ui/QuickPick.h"
#include "ui/Theme.h"
#include "ui/TimerWhileShowing.h"
#include "ui/ValueControl.h"

namespace lynx
{
using namespace theme;

namespace
{
juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// card geometry (px at 100 %)
constexpr int contextWidth = 132, meterWidth = 22, gap = 4, titleHeight = 20, foldedWidth = 22, addWidth = 44;
constexpr int knobColumn = 62, knobRow = 70, knobSize = 30, instrumentWidth = 196, pluginWidth = 172;
constexpr int maxEffectKnobs = 7; // + the slot's Mix

juce::String insertName (const Song& song, int insert)
{
    if (insert == 0)
        return tr ("Master");
    if (insert < 0 || insert >= (int) song.mixer.size())
        return {};
    const auto own = utf8 (song.mixer[(std::size_t) insert].name);
    return own.isNotEmpty() ? tr ("Insert {0}", { juce::String (insert) }) + " " + own : tr ("Insert {0}", { juce::String (insert) });
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
            // Duplicate are its Edit menu's commands on the selected device
            menu.addItem (tr ("Cut") + "\tCtrl+X", [this, slot] { copyDevice (slot, true); });
            menu.addItem (tr ("Copy") + "\tCtrl+C", [this, slot] { copyDevice (slot, false); });
            menu.addItem (tr ("Paste") + "\tCtrl+V", deviceClipboard().has_value() && effectCount() < maxEffectSlots, false, [this, slot] { pasteDevice (slot + 1); });
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
    }

    ~DeviceChain() override { engine.removeChangeListener (this); }

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
    bool isCardFolded (int i) const { return i >= 0 && i < (int) cards.size() && cards[(std::size_t) i].folded; }
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
        if (i >= 0 && i < (int) cards.size() && cards[(std::size_t) i].kind == Card::effect)
            showCardMenu (i);
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
    bool duplicateCard (int i)
    {
        if (i < 0 || i >= (int) cards.size() || cards[(std::size_t) i].kind != Card::effect)
            return false;
        duplicateDevice (cards[(std::size_t) i].index, -1);
        return true;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg1);
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
                g.setColour (colour::textDisabled);
                g.setFont (font (type::small));
                g.drawText (track < 0 ? tr ("Select a track in the clip grid to see its devices")
                                      : tr ("This track has no clips yet: put a clip on it to see its devices"),
                            area, juce::Justification::centred, true);
            }
            else if (insert >= 0) // the free room after the last card shows the drop hint, as the reference does [SHOT d01]
            {
                const auto room = area.withLeft (cards.empty() ? area.getX() : cards.back().bounds.getRight() + gap);
                if (room.getWidth() > 120)
                {
                    g.setColour (colour::textDisabled);
                    g.setFont (font (type::small));
                    g.drawText (tr ("Drop effects here"), room, juce::Justification::centred, true);
                }
            }
            if (dropBefore >= 0)
            {
                g.setColour (colour::accent);
                g.fillRect (juce::Rectangle<int> (dropLineX (dropBefore) - 1, area.getY() + 2, 3, area.getHeight() - 4));
            }
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
                pasteDevice (slot + 1);
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
               && ((BrowserComponent::isBrowserDrag (details) && BrowserComponent::draggedFile (details).hasFileExtension ("fst"))
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
            if (auto c = BrowserComponent::draggedGenerator (details); c && hooks.instrumentDropped != nullptr)
                hooks.instrumentDropped (insert, std::move (*c));
            return;
        }
        std::optional<EffectSlot> fx;
        if (BrowserComponent::isEffectDrag (details))
            fx = BrowserComponent::draggedEffect (details);
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
        if (channel >= 0)
        {
            Card c;
            c.kind = Card::instrument;
            c.index = channel;
            c.folded = folded.count (keyOf (c)) > 0;
            c.knobs = { 0, 1 };
            c.custom = customFor (keyOf (c), (int) engine.getSong().channels[(std::size_t) channel].instrument, [&] {
                return DetailView::instrumentCardFactory != nullptr
                           ? DetailView::instrumentCardFactory (engine, channel, engine.getSong().channels[(std::size_t) channel])
                           : nullptr;
            });
            if (c.custom != nullptr)
                c.knobs.clear();
            const auto w = c.folded ? foldedWidth : c.custom != nullptr ? customWidth (*c.custom, height, x) : instrumentWidth;
            c.bounds = { x, top, w, height };
            x += w + gap;
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
            x += w + gap;
            cards.push_back (std::move (c));
        }
        if (insert >= 0 && effectCount() < maxEffectSlots)
        {
            Card c;
            c.kind = Card::add;
            c.bounds = { x, top, addWidth, height };
            x += addWidth + gap;
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

    // the custom card of `key`, made once (again when the slot's kind changes); nullptr = an ordinary card
    DetailCard* customFor (std::uint64_t key, int kind, const std::function<std::unique_ptr<DetailCard>()>& make)
    {
        if (DetailView::effectCardFactory == nullptr && DetailView::instrumentCardFactory == nullptr)
            return nullptr;
        auto& entry = customs[key];
        if (entry.kind != kind)
        {
            entry.kind = kind;
            if (entry.card != nullptr)
                cardHost.removeChildComponent (entry.card.get());
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
        card.widthLimit = std::max (area.getWidth() / 2, area.getRight() - (x + scroll) - gap);
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
                c.custom->setBounds (c.bounds.withTrimmedTop (titleHeight + 2).reduced (2, 0).withTrimmedBottom (2) - origin);
                c.custom->setVisible (! c.folded);
                if (auto* extra = c.custom->titleAccessory())
                {
                    const auto w = c.custom->titleAccessoryWidth();
                    extra->setBounds (juce::Rectangle<int> (c.bounds.getRight() - 4 - titleButtonsWidth (c) - w, c.bounds.getY() + 2, w, titleHeight - 4) - origin);
                    extra->setVisible (! c.folded);
                }
            }
        for (auto it = customs.begin(); it != customs.end();) // (slots gone: their cards go too)
        {
            if (it->second.card != nullptr && shown.count (it->second.card.get()) == 0)
            {
                if (auto* extra = it->second.card->titleAccessory())
                    cardHost.removeChildComponent (extra);
                cardHost.removeChildComponent (it->second.card.get());
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
            return cards[(std::size_t) position].bounds.getX() - gap / 2;
        return cards.empty() ? chainArea().getX() : cards.back().bounds.getRight() + gap / 2;
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
        return fx != nullptr && fx->kind != EffectKind::plugin && fx->kind != EffectKind::patcher && pluginheader::PresetBar (engine, insert, slot).hasLynxPresets();
    }
    void savePreset (int slot)
    {
        if (! canSavePreset (slot))
            return;
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
        return 18 + (c.kind == Card::add ? 0 : 18) + (c.kind == Card::effect && canSavePreset (c.index) ? 18 : 0);
    }
    juce::Rectangle<int> saveBounds (const Card& c) const
    {
        return c.kind == Card::effect && canSavePreset (c.index) ? juce::Rectangle<int> (c.bounds.getRight() - 38, c.bounds.getY() + 3, 16, 14) : juce::Rectangle<int>();
    }
    juce::Rectangle<int> swapBounds (const Card& c) const
    {
        if (c.kind == Card::add)
            return {};
        return { c.bounds.getRight() - 38 - (saveBounds (c).isEmpty() ? 0 : 18), c.bounds.getY() + 3, 16, 14 };
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
    ui::ValueSpec knobSpec (const Card& c, int knob) const
    {
        if (c.kind == Card::effect)
            return slotActions.slotParamSpec (c.knobs[(std::size_t) knob], c.index);
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
            return slotActions.slotParamValue (c.knobs[(std::size_t) knob], c.index);
        const auto& song = engine.getSong();
        if (c.index < 0 || c.index >= (int) song.channels.size())
            return 0.0;
        const auto& ch = song.channels[(std::size_t) c.index];
        return c.knobs[(std::size_t) knob] == 1 ? ch.pan : ch.volume;
    }
    void setKnob (const Card& c, int knob, double v)
    {
        if (c.kind == Card::effect)
        {
            slotActions.setSlotParam (c.knobs[(std::size_t) knob], c.index, v);
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
        if (c.kind == Card::instrument)
            return { c.bounds.getX() + 16 + knob * knobColumn, body.getY() + 92, knobColumn - 8, knobRow - 6 };
        if (c.kind == Card::effect)
            if (const auto* fx = effectAt (c.index); fx != nullptr && fx->kind == EffectKind::plugin)
                return { c.bounds.getX() + 8, body.getY() + 92, knobColumn - 8, knobRow - 6 };
        const auto column = knob / 2, row = knob % 2;
        return { c.bounds.getX() + 8 + column * knobColumn, body.getY() + row * knobRow, knobColumn - 8, knobRow - 6 };
    }

    // --- hit testing ---------------------------------------------------------------------------------------------
    juce::Rectangle<int> pickerBounds() const { return { 8, 40, contextWidth - 16, 20 }; }
    static juce::Rectangle<int> titleBounds (const Card& c) { return c.bounds.withHeight (titleHeight); }
    static juce::Rectangle<int> ledBounds (const Card& c) { return { c.bounds.getX() + 5, c.bounds.getY() + 5, 11, 11 }; }
    static juce::Rectangle<int> windowBounds (const Card& c) { return { c.bounds.getRight() - 20, c.bounds.getY() + 3, 16, 14 }; }

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
        g.setColour (colour::bg2);
        g.fillRect (r);
        g.setColour (colour::border);
        g.fillRect (r.getRight(), 0, 1, getHeight());
        r = r.reduced (8, 6);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (track >= 0 ? trackName (song, track) : juce::String(), r.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (tr ("Insert"), r.removeFromTop (14), juce::Justification::centredLeft, true);
        const auto box = pickerBounds();
        const bool choice = inserts.size() > 1;
        if (choice)
        {
            g.setColour (hover.part == Part::picker ? colour::bg5 : colour::bg4);
            g.fillRoundedRectangle (box.toFloat(), 3.0f);
        }
        g.setColour (insert >= 0 ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::small));
        g.drawText ((insert >= 0 ? insertName (song, insert) : tr ("None")) + (choice ? juce::String::fromUTF8 (" \xe2\x96\xbe") : juce::String()),
                    box.reduced (choice ? 6 : 0, 0), juce::Justification::centredLeft, true);
        r = r.withTop (box.getBottom() + 8);
        if (channel >= 0)
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
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawFittedText (tr ("This track plays through {0} inserts", { juce::String ((int) inserts.size()) }), r.removeFromTop (30),
                              juce::Justification::topLeft, 2);
        }
    }

    void paintCard (juce::Graphics& g, int i)
    {
        const auto& c = cards[(std::size_t) i];
        if (! c.bounds.intersects (chainArea()))
            return;
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

    void paintBody (juce::Graphics& g, int i)
    {
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
                               : c.kind == Card::effect && fx != nullptr                ? utf8 (effectInfo (fx->kind).params[(std::size_t) c.knobs[(std::size_t) k]].name)
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
        g.setColour (colour::bg0);
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
        g.setColour (colour::bg0);
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

    void timerCallback() override
    {
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
    juce::ScrollBar scrollBar { false };
    std::unique_ptr<ui::TimerWhileShowing> showingTimer;
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
        position.setTooltip (tr ("Loop Position: moves the looped part, its length stays (the loop starts where the clip starts)"));
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
        signature.setFont (font (type::small));
        signature.setColour (juce::Label::textColourId, colour::textPrimary);
        signature.setTooltip (tr ("The project's time signature (clips have no signature of their own in LYNX SHARE)"));
        addAndMakeVisible (signature);
        loop.setButtonText (tr ("Loop"));
        loop.onClick = [this] { editSlot ([on = loop.getToggleState()] (ClipGridSlot& slot) { slot.loop = on; }); };
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
        duplicate.onClick = [this] { notes ([] (Pattern& p, const std::vector<int>& all, const Song& song) { noteedit::duplicateBars (p, all, song.beatsPerBar); }); };
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
        fitScale.setTooltip (tr ("Moves every note of the clip onto the project's key"));
        for (auto* b : { &timesTwo, &divideTwo, &fitScale })
        {
            b->setConnectedEdges (0);
            addAndMakeVisible (*b);
        }
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
            notes ([] (Pattern& p, const std::vector<int>& all, const Song& song) {
                if (const auto key = projectkey::get (song); key && projectkey::lynxScale (*key) > 0)
                    notetools::snapNotesToScale (p, all, key->root, projectkey::lynxScale (*key));
            });
        };

        // the Clip panel's Scale row: the project key (Live keeps a scale per clip; LYNX SHARE has one per project)
        scaleRoot.addItem (tr ("None"), 1);
        for (int root = 0; root < 12; ++root)
            scaleRoot.addItem (juce::String (projectkey::rootName (root)), root + 2);
        for (auto index : projectkey::commonFirst())
            scaleName.addItem (tr (notetools::flReferenceScales()[(std::size_t) index].name), index + 1);
        for (auto* c : { &scaleRoot, &scaleName })
        {
            c->setTooltip (tr ("The project's key: the Piano roll highlights it and Fit to scale uses it (LYNX SHARE has one key per project)"));
            addAndMakeVisible (*c);
            c->onChange = [this] {
                std::optional<projectkey::Key> key;
                if (scaleRoot.getSelectedId() >= 2)
                    key = projectkey::Key { scaleRoot.getSelectedId() - 2, std::max (0, scaleName.getSelectedId() - 1) };
                if (key && scaleName.getSelectedId() == 0) // first key of a project: Major
                    key->flScale = projectkey::commonFirst().front();
                engine.editSong ([key] (Song& song) { projectkey::set (song, key); });
            };
        }
        engine.addChangeListener (this);
    }
    ~ClipColumn() override { engine.removeChangeListener (this); }

    // the reference's Clip / Launch / Notes panels side by side under the clip's name [SHOT d08]
    static constexpr int clipPanel = 164, clipPanel2 = 164, launchPanel = 136, notesPanel = 172, gap = 8;
    static constexpr int foldedTools = 20;
    int preferredWidth() const
    {
        return gap + clipPanel + gap + clipPanel2 + gap + launchPanel + gap + (hasNotes ? notesPanel + gap + (toolsFolded ? foldedTools : MidiToolsPanel::panelWidth) + gap : 0);
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
        g.fillAll (colour::bg2);
        g.setColour (colour::border);
        g.fillRect (getWidth() - 1, 0, 1, getHeight());
    }

    void resized() override
    {
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
        halves (take (c2, 20), signature, groove);
        scaleLabel.setBounds (take (c2, 14));
        halves (take (c2, 20), scaleRoot, scaleName);

        // Launch: mode, quantisation, legato
        auto l = r.removeFromLeft (launchPanel);
        r.removeFromLeft (gap);
        launchTitle.setBounds (take (l, 14));
        modeLabel.setBounds (take (l, 12));
        mode.setBounds (take (l, 20));
        quantLabel.setBounds (take (l, 12));
        quant.setBounds (take (l, 20));
        legato.setBounds (take (l, 20));

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
    }

private:
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
    void setHere (int which)
    {
        float progress = 0.0f;
        if (! clipPlays (&progress))
            return;
        editClip ([which, progress] (Clip& c, ClipGridSlot&) {
            const auto at = clipsample::playPosition (c, progress);
            if (which == 0)
                clipsample::setStartAt (c, at);
            else if (which == 2)
                clipsample::setPositionAt (c, at);
            else
                clipsample::setEndAt (c, at);
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
        return juce::String (seconds ? clipsample::secondsText (ticks, song.bpm) : clipsample::positionText (ticks, song.beatsPerBar, asLength));
    }
    std::int64_t fieldTicks (const juce::TextEditor& ed, bool asLength, bool seconds) const
    {
        const auto& song = engine.getSong();
        const auto text = ed.getText().trim().toStdString();
        return seconds ? clipsample::parseSeconds (text, song.bpm) : clipsample::parsePosition (text, song.beatsPerBar, asLength);
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
        const auto secs = inSeconds (*c);
        if (! start.hasKeyboardFocus (false))
            start.setText (fieldText (c->offset, false, secs), false);
        if (! end.hasKeyboardFocus (false))
            end.setText (fieldText (c->offset + c->length, false, secs), false);
        if (! position.hasKeyboardFocus (false))
            position.setText (fieldText (c->offset, false, secs), false);
        if (! length.hasKeyboardFocus (false))
            length.setText (fieldText (c->length, true, secs), false);
        signature.setText (juce::String (bpb) + " / 4", n);
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
            groove.setEnabled (c->kind == ClipKind::pattern);
            groove.setTooltip (c->kind == ClipKind::pattern ? tr ("The groove this clip plays through (the Groove Pool)")
                                                            : tr ("Grooves play on MIDI clips only in LYNX SHARE"));
        }
        if (slot != nullptr)
        {
            loop.setToggleState (slot->loop, n);
            for (auto* ed : { &position, &length }) // (Live: the loop's fields are off with Loop off)
                ed->setEnabled (slot->loop);
            legato.setToggleState (slot->legato, n);
            mode.setSelectedId ((int) slot->mode + 1, n);
            quant.setSelectedId ((int) slot->quant + 1, n);
        }
        const bool pattern = c->kind == ClipKind::pattern;
        if (std::exchange (hasNotes, pattern) != pattern)
            if (auto* parent = getParentComponent())
                parent->resized();
        for (auto* b : { &notesTitle })
            b->setVisible (pattern);
        for (auto* b : { &invert, &reverse, &makeLegato, &humanize, &duplicate, &quantize, &randomize, &timesTwo, &divideTwo, &fitScale, &addInterval })
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
        const auto key = projectkey::get (song);
        scaleRoot.setSelectedId (key ? key->root + 2 : 1, n);
        scaleName.setSelectedId (key ? key->flScale + 1 : 0, n);
        scaleName.setEnabled (key.has_value());
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
        // the field that was typed in decides (Start keeps the end, Position keeps the length)
        const auto secs = inSeconds (*c);
        const auto end0 = c->offset + c->length;
        auto offset = c->offset, len = c->length;
        if (start.getText() != fieldText (c->offset, false, secs))
        {
            if (const auto t = fieldTicks (start, false, secs); t >= 0 && t < end0)
            {
                offset = t;
                len = end0 - t;
            }
        }
        else if (position.getText() != fieldText (c->offset, false, secs))
        {
            if (const auto t = fieldTicks (position, false, secs); t >= 0)
                offset = t;
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
        const auto newName = name.getText().trim().toStdString();
        const int t = track, sc = scene;
        engine.editSong ([t, sc, offset, len, newName] (Song& song) {
            if (! song.clipGrid)
                return;
            for (auto& clip : song.clipGrid->clips)
                if (clip.track == t && clip.length > 0 && clip.gridSlot >= 0 && clip.gridSlot < (int) song.clipGrid->slots.size()
                    && song.clipGrid->slots[(std::size_t) clip.gridSlot].scene == sc)
                {
                    clip.offset = offset;
                    clip.length = len;
                    song.clipGrid->slots[(std::size_t) clip.gridSlot].name = newName;
                }
        });
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
    juce::TextButton setStart, setEnd, setPosition, setLength; // round 190dr
    juce::Label positionLabel, grooveLabel;
    juce::ComboBox groove;
    bool loading = false;
    std::unique_ptr<ui::TimerWhileShowing> showingTimer;
    juce::ToggleButton loop, legato;
    juce::ComboBox mode, quant;
    juce::TextButton invert, reverse, makeLegato, humanize, duplicate, quantize, randomize, timesTwo, divideTwo, fitScale;
    juce::Label scaleLabel;
    juce::ComboBox scaleRoot, scaleName; // the project key (round 190o; Live: the clip's Scale)
};

// ---------------------------------------------------------------------------------------------------------------------
// The Clip View (slice 1: the selected slot's clip and a button to its editor; slice 2 docks the editors here).
class DetailView::ClipPage final : public juce::Component, private juce::ChangeListener
{
public:
    ClipPage (AudioEngine& e, Hooks& h) : engine (e), hooks (h), column (e)
    {
        addChildComponent (column); // (slice 2b: shown while a grid clip is)
        column.editorOf = [this] { return dynamic_cast<PianoRollComponent*> (editor); };
        for (auto* b : { &notesTab, &envelopesTab })
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
        notesTab.onClick = [this] { showTab (false); };
        envelopesTab.onClick = [this] { showTab (true); };
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
    void showTab (bool envelopes)
    {
        DetailView::envelopesTab = envelopes;
        if (! envelopes)
            takenBack = false; // (Notes: the Piano roll docks again)
        update();
    }
    void release()
    {
        dropEvents();
        dockedTrack = dockedScene = -1;
        if (editor != nullptr && hooks.undockPianoRoll != nullptr)
        {
            editor = nullptr;
            hooks.undockPianoRoll();
        }
        editor = nullptr;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg1);
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
        if (notesTab.isVisible())
        {
            // the clip's tabs, top right, as the reference's Notes | Envelopes [SHOT d08]
            auto bar = area.removeFromTop (tabHeight).reduced (2, 2);
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
            resized();
        }
        notesTab.setToggleState (! DetailView::envelopesTab, juce::dontSendNotification);
        envelopesTab.setToggleState (DetailView::envelopesTab, juce::dontSendNotification);
        if (tabs && DetailView::envelopesTab)
        {
            if (editor != nullptr && hooks.undockPianoRoll != nullptr) // (the Piano roll goes home while Envelopes shows)
            {
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
                return; // (already showing this clip: the user may have picked another pattern in it meanwhile)
            dockedTrack = track;
            dockedScene = scene;
            auto* e = hooks.dockPianoRoll (clip->pattern);
            if (e != nullptr && e != editor)
            {
                editor = e;
                addAndMakeVisible (*editor);
                resized();
            }
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
    juce::TextButton notesTab, envelopesTab;
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
hotswap::Target DetailView::selectedDeviceTarget() const { return devicesPage && isShowing() ? chain->selectedTarget() : hotswap::Target(); }
void DetailView::hotSwapChanged() { chain->repaint(); }
void DetailView::selectCardForCheck (int card) { chain->selectCard (card); }
bool DetailView::duplicateCardForCheck (int card) { return chain->duplicateCard (card); }
juce::PopupMenu DetailView::cardMenuForSnapshot (int card)
{
    juce::PopupMenu menu;
    EffectsPanel::slotMenuForSnapshot = &menu;
    chain->showCardMenuForSnapshot (card);
    EffectsPanel::slotMenuForSnapshot = nullptr;
    return menu;
}
int DetailView::cardCountForCheck() const { return chain->cardCount(); }
int DetailView::chainInsertForCheck() const { return chain->insertShown(); }
int DetailView::chainChannelForCheck() const { return chain->channelShown(); }
bool DetailView::isFoldedForCheck (int card) const { return chain->isCardFolded (card); }
void DetailView::toggleFoldForCheck (int card) { chain->toggleCardFold (card); }
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
