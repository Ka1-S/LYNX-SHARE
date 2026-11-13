#include "ui/SimpleDock.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

#include "core/ChainPresets.h"
#include "core/DetailChain.h"
#include "core/GoalKnobs.h"
#include "core/KeyFollow.h"
#include "core/MixerOps.h"
#include "core/SimplePads.h"
#include "core/SimpleTracks.h"
#include "core/SoundLibrary.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/ProjectFile.h"
#include "platform/Paths.h"
#include "ui/FileChoosers.h"
#include "ui/BrowserComponent.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/QuickPick.h"
#include "ui/SoundFontEditor.h"
#include "ui/Theme.h"
#include "ui/TouchKeyboard.h"

namespace lynx
{
using namespace theme;

namespace
{
juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
juce::String glyph (const char* bytes) { return juce::String::fromUTF8 (bytes); }

// geometry (px at 100 %)
constexpr int sideWidth = 212, cardWidth = 148, cardGap = 8, addWidth = 52, titleHeight = 26, pad = 8;
constexpr int rowHeight = 24, categoryWidth = 168;

// Words that mean something else elsewhere in the UI get their own translation here ("Keys" the instruments, not
// the computer's keys; "Punch" the effect, not punch-in recording)
juce::String goalText (int id) { return id == goals::punch ? trc ("@one-knob effect: Punch") : tr (goals::name (id)); }

juce::String soundCategoryText (soundlib::Category c)
{
    using C = soundlib::Category;
    return c == C::keys ? trc ("@sound category: Keys") : c == C::bass ? trc ("@instrument: Bass") : c == C::lead ? trc ("@sound category: Lead")
           : c == C::effects ? trc ("@sound category: Effects") : tr (soundlib::categoryName (c));
}

juce::String chainCategoryText (const char* name)
{
    const juce::String n (name);
    return n == "Keys" ? trc ("@sound category: Keys") : n == "Bass" ? trc ("@instrument: Bass") : n == "Space" ? trc ("@preset category: Space") : tr (name);
}

juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

juce::String trackTitle (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return utf8 (song.playlistTracks[(std::size_t) track].name);
    return tr ("Track {0}", { juce::String (track + 1) });
}

// One rotary control of the dock: a value 0..1 with its text under the ring. Drag up / down (Shift: fine), the wheel,
// the arrow keys; a double click goes back to `resetValue`. While it is dragged it keeps its own value, so the
// song's change messages don't fight the hand.
class DockKnob final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    std::function<void (float)> onChange;
    std::function<juce::String (float)> valueText;
    float resetValue = 0.5f;
    bool big = false;

    DockKnob()
    {
        setWantsKeyboardFocus (true);
        getProperties().set ("lynxClickKeepsKeys", true); // (round 190fc: clicked, the arrow keys turn it; Space still plays)
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    float value() const noexcept { return shown; }
    void setValue (float v)
    {
        v = juce::jlimit (0.0f, 1.0f, v);
        if (dragging || std::abs (v - shown) < 1.0e-6f)
            return;
        shown = v;
        repaint();
    }
    void setDimmed (bool d)
    {
        if (dimmed != d)
        {
            dimmed = d;
            repaint();
        }
    }
    // the user's move (tests call it as a drag does)
    void turnTo (float v)
    {
        shown = juce::jlimit (0.0f, 1.0f, v);
        repaint();
        if (onChange)
            onChange (shown);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const auto textHeight = big ? 20.0f : 16.0f;
        const auto text = r.removeFromBottom (textHeight);
        const auto d = juce::jmin (r.getWidth(), r.getHeight());
        const auto ring = juce::Rectangle<float> (d, d).withCentre (r.getCentre()).reduced (big ? 3.0f : 2.0f);
        const bool hot = dragging || isMouseOver() || hasKeyboardFocus (false);
        if (big) // a face under the ring, so the one knob reads as the card's control
        {
            g.setColour (colour::bg0);
            g.fillEllipse (ring.reduced (ring.getWidth() * 0.16f));
            g.setColour (hot ? colour::borderStrong : colour::border);
            g.drawEllipse (ring.reduced (ring.getWidth() * 0.16f), 1.0f);
        }
        drawKnob (g, ring, shown, false, dimmed ? colour::textDisabled : dragging ? colour::accent : colour::knobArc, hot);
        g.setColour (dimmed ? colour::textDisabled : colour::textPrimary);
        g.setFont (font (big ? type::body : type::small, big));
        g.drawText (valueText ? valueText (shown) : percent (shown), text, juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        dragging = true;
        start = shown;
        grabKeyboardFocus();
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        const auto span = e.mods.isShiftDown() ? 640.0f : 160.0f;
        turnTo (start - (float) e.getDistanceFromDragStartY() / span);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = false;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { turnTo (resetValue); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs (wheel.deltaY) > 0.0f)
            turnTo (shown + (wheel.deltaY > 0.0f ? 0.02f : -0.02f));
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto step = key.getModifiers().isShiftDown() ? 0.01f : 0.05f;
        if (key.isKeyCode (juce::KeyPress::upKey) || key.isKeyCode (juce::KeyPress::rightKey))
            return turnTo (shown + step), true;
        if (key.isKeyCode (juce::KeyPress::downKey) || key.isKeyCode (juce::KeyPress::leftKey))
            return turnTo (shown - step), true;
        return false;
    }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    float shown = 0.0f, start = 0.0f;
    bool dragging = false, dimmed = false;
};

// A small flat button of a card's title bar (‹ › ×)
class GlyphButton final : public juce::Button
{
public:
    explicit GlyphButton (const juce::String& text) : juce::Button (text) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        if (over || down)
            fillRoundRect (g, getLocalBounds().toFloat().reduced (1.0f), radius::sm, down ? colour::bg6 : colour::bg5);
        g.setColour (! isEnabled() ? colour::textDisabled : over ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::title));
        g.drawText (getName(), getLocalBounds(), juce::Justification::centred, false);
    }
};

// The last "card" of the chain: a big + in a dashed frame
class AddCardButton final : public juce::Button
{
public:
    AddCardButton() : juce::Button ("+") {}
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        fillRoundRect (g, r, radius::lg, down ? colour::bg4 : over ? colour::bg3 : colour::bg2);
        juce::Path frame;
        frame.addRoundedRectangle (r.reduced (0.5f), radius::lg);
        const float dashes[] { 4.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, frame, dashes, 2);
        g.setColour (! isEnabled() ? colour::border : over ? colour::textSecondary : colour::borderStrong);
        g.fillPath (dashed);
        const auto c = r.getCentre();
        g.setColour (! isEnabled() ? colour::textDisabled : over ? colour::textPrimary : colour::textSecondary);
        g.fillRect (juce::Rectangle<float> (18.0f, 2.0f).withCentre (c));
        g.fillRect (juce::Rectangle<float> (2.0f, 18.0f).withCentre (c));
    }
};

void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (font (type::caption));
    label.setColour (juce::Label::textColourId, colour::textSecondary);
    label.setInterceptsMouseClicks (false, false);
}
} // namespace

// =====================================================================================================================
// Effects
// =====================================================================================================================
class SimpleDock::EffectsPage final : public juce::Component
{
public:
    explicit EffectsPage (AudioEngine& e) : engine (e), slotActions (e)
    {
        addChildComponent (slotActions); // (never shown: the Mixer slot's own menus, windows and edits on this insert)
        slotActions.setMenuTarget (this);

        styleCaption (presetCaption, tr ("Preset"));
        addAndMakeVisible (presetCaption);
        presetButton.setTooltip (tr ("A ready chain of effects for this track"));
        presetButton.onClick = [this] { presetMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton)); };
        addAndMakeVisible (presetButton);
        saveButton.setButtonText (tr ("Save"));
        saveButton.setTooltip (tr ("Keeps this chain as one of your presets"));
        saveButton.onClick = [this] { askSaveName(); };
        addAndMakeVisible (saveButton);

        styleCaption (blendCaption, tr ("Blend"));
        addAndMakeVisible (blendCaption);
        blend.resetValue = 1.0f;
        blend.setTooltip (tr ("How much of the effects you hear: 0 % is the track without them"));
        blend.setTitle (tr ("Blend"));
        blend.onChange = [this] (float v) {
            const auto t = insert;
            if (t >= 0)
                engine.editSong ([t, v] (Song& s) {
                    if (t < (int) s.mixer.size())
                        s.mixer[(std::size_t) t].chainBlend = v;
                });
        };
        addAndMakeVisible (blend);
        // round 190fo: the track's send into the shared "Reverb" insert (the original's per-track Reverb knob)
        styleCaption (reverbCaption, tr ("Reverb"));
        addAndMakeVisible (reverbCaption);
        reverb.resetValue = 0.0f;
        reverb.setTooltip (tr ("How much of the track goes to the shared reverb (one Reverb insert for every track)"));
        reverb.setTitle (tr ("Reverb"));
        reverb.onChange = [this] (float v) {
            const auto t = insert;
            if (t > 0)
                engine.editSong ([t, v] (Song& s) { simpletracks::setReverbSend (s, t, v < 0.005f ? 0.0f : v); });
        };
        addAndMakeVisible (reverb);

        view.setViewedComponent (&strip, false);
        view.setScrollBarsShown (false, true);
        view.setScrollBarThickness (8);
        addAndMakeVisible (view);

        addCard.setTooltip (tr ("Add an effect"));
        addCard.setComponentID ("simple.dock.addEffect"); // (round 190dv: the tour's ring)
        addCard.onClick = [this] { addMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addCard)); };
        strip.addAndMakeVisible (addCard);

        styleCaption (emptyHint, {});
        emptyHint.setFont (font (type::body));
        emptyHint.setJustificationType (juce::Justification::centredLeft);
        strip.addAndMakeVisible (emptyHint);
    }

    void show (int newInsert)
    {
        if (insert != newInsert)
        {
            insert = newInsert;
            cards.clear();
            view.setViewPosition (0, 0);
        }
        slotActions.setTrack (juce::jmax (0, insert));
        refresh();
    }

    void refresh()
    {
        const auto& song = engine.getSong();
        const bool has = insert >= 0 && insert < (int) song.mixer.size();
        presetButton.setEnabled (has);
        blend.setEnabled (has);
        reverb.setEnabled (has && insert > 0 && insert != simpletracks::sharedReverb (song));
        reverb.setValue (has ? simpletracks::reverbSend (song, insert) : 0.0f);
        addCard.setEnabled (has);
        if (! has)
        {
            cards.clear();
            presetButton.setButtonText (tr ("None"));
            saveButton.setEnabled (false);
            blend.setValue (1.0f);
            emptyHint.setText (tr ("Select a track to see its effects."), juce::dontSendNotification);
            layoutStrip();
            return;
        }
        const auto& mixer = song.mixer[(std::size_t) insert];
        const auto& fx = mixer.effects;
        // the cards are kept while the chain has the same shape: a knob being dragged stays the same component
        bool same = cards.size() == fx.size();
        for (std::size_t i = 0; same && i < fx.size(); ++i)
            same = cards[i]->kind == fx[i].kind && cards[i]->goal == (goals::active (fx[i]) ? (int) fx[i].goal : 0);
        if (! same)
        {
            cards.clear();
            for (std::size_t i = 0; i < fx.size(); ++i)
            {
                cards.push_back (std::make_unique<Card> (*this, (int) i));
                strip.addAndMakeVisible (*cards.back());
            }
        }
        for (std::size_t i = 0; i < fx.size(); ++i)
            cards[i]->update (fx[i], (int) i, (int) fx.size());

        auto name = mixer.chainPreset.empty() ? tr ("None") : utf8 (mixer.chainPreset);
        if (! mixer.chainPreset.empty() && ! chains::unchanged (song, insert))
            name << " " << tr ("(edited)");
        presetButton.setButtonText (name + glyph (" \xe2\x96\xbe"));
        saveButton.setEnabled (! fx.empty());
        blend.setValue (mixer.chainBlend);
        blend.setDimmed (fx.empty());
        addCard.setEnabled ((int) fx.size() < maxEffectSlots);
        emptyHint.setText (fx.empty() ? tr ("No effects yet. Press + to add one, or pick a preset.") : juce::String(), juce::dontSendNotification);
        layoutStrip();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad);
        auto side = r.removeFromLeft (sideWidth - pad);
        presetCaption.setBounds (side.removeFromTop (16));
        auto row = side.removeFromTop (26);
        saveButton.setBounds (row.removeFromRight (58));
        row.removeFromRight (6);
        presetButton.setBounds (row);
        side.removeFromTop (10);
        auto captions = side.removeFromTop (16);
        auto knobs = side.removeFromTop (juce::jmin (84, side.getHeight()));
        blendCaption.setBounds (captions.removeFromLeft (90));
        blend.setBounds (knobs.removeFromLeft (90).withWidth (72));
        reverbCaption.setBounds (captions);
        reverb.setBounds (knobs.withWidth (72));
        r.removeFromLeft (pad);
        view.setBounds (r);
        layoutStrip();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::border);
        g.fillRect (sideWidth, pad, 1, getHeight() - 2 * pad);
    }

    // --- menus ------------------------------------------------------------------------------------------------------
    juce::PopupMenu presetMenu()
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        const auto current = insert >= 0 && insert < (int) song.mixer.size() ? song.mixer[(std::size_t) insert].chainPreset : std::string();
        auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::mixerTrackStateExtension);
        files.sort();
        // round 190dg: the favourites first, under their own heading (a factory preset or one of the user's)
        const auto favs = favouritePresets();
        if (! favs.isEmpty())
        {
            juce::PopupMenu starred;
            for (const auto& f : favs)
            {
                const auto name = f.toStdString();
                if (chains::find (name) != nullptr)
                    starred.addItem (f, true, current == name, [this, name] { applyFactory (name); });
                else if (const auto file = userPresetFolder().getChildFile (f + ProjectFile::mixerTrackStateExtension); file.existsAsFile())
                    starred.addItem (f, true, current == name, [this, file] { loadUserPreset (engine, insert, file); });
            }
            if (starred.getNumItems() > 0)
            {
                menu.addSubMenu (glyph ("\xe2\x98\x85 ") + tr ("Favourites"), starred, true, nullptr, favs.contains (utf8 (current)));
                menu.addSeparator();
            }
        }
        for (const auto* category : chains::categories())
        {
            juce::PopupMenu sub;
            bool ticked = false;
            for (const auto& p : chains::factory())
                if (std::string (p.category) == category)
                {
                    const bool on = current == p.name;
                    ticked = ticked || on;
                    sub.addItem (utf8 (p.name) + glyph (" \xe2\x80\x94 ") + tr (p.description), true, on, [this, name = std::string (p.name)] { applyFactory (name); });
                }
            menu.addSubMenu (chainCategoryText (category), sub, true, nullptr, ticked);
        }
        menu.addSeparator();
        juce::PopupMenu mine;
        for (const auto& f : files)
            mine.addItem (f.getFileNameWithoutExtension(), true, current == f.getFileNameWithoutExtension().toStdString(),
                          [this, f] { loadUserPreset (engine, insert, f); });
        if (files.isEmpty())
            mine.addItem (tr ("(none yet)"), false, false, nullptr);
        menu.addSubMenu (tr ("My presets"), mine);
        menu.addItem (tr ("Save as my preset..."), hasEffects(), false, [this] { askSaveName(); });
        if (! current.empty()) // the track's own preset as a favourite (or not any more)
        {
            const auto shown = utf8 (current);
            const bool starred = favs.contains (shown);
            menu.addItem (starred ? tr ("Remove \"{0}\" from favourites", { shown }) : tr ("Add \"{0}\" to favourites", { shown }),
                          [shown, starred] { setFavouritePreset (shown, ! starred); });
        }
        if (! files.isEmpty()) // (to the system's bin: a preset removed by mistake can be taken back)
        {
            juce::PopupMenu bin;
            for (const auto& f : files)
                bin.addItem (f.getFileNameWithoutExtension(), [f] { f.moveToTrash(); });
            menu.addSubMenu (tr ("Delete one of my presets"), bin);
        }
        menu.addSeparator();
        menu.addItem (tr ("Remove all effects"), hasEffects(), false, [this] {
            const auto t = insert;
            engine.editSongStructural ([t] (Song& s) { chains::applyChain (s, t, {}, 1.0f, {}); });
        });
        return menu;
    }

    juce::PopupMenu addMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("One-knob effects"));
        for (int id = 1; id <= goals::count; ++id)
            menu.addItem (goalText (id) + glyph (" \xe2\x80\x94 ") + tr (goals::description (id)), [this, id] { addOneKnob (id); });
        menu.addSeparator();
        menu.addItem (tr ("More effects..."), [this] { slotActions.showAddMenuFor (nextFreeRow()); });
        return menu;
    }

    // --- edits (each through the Mixer's own paths) -------------------------------------------------------------------
    void addOneKnob (int goal)
    {
        const auto t = insert;
        if (t < 0)
            return;
        engine.editSongStructural ([t, goal] (Song& s) {
            if (t < (int) s.mixer.size())
                detailchain::insertEffectAt (s, t, goals::make (goal), (int) s.mixer[(std::size_t) t].effects.size());
        });
    }

    void turn (int index, bool oneKnob, float v)
    {
        const auto t = insert;
        if (oneKnob)
            engine.editSong ([t, index, v] (Song& s) {
                if (t >= 0 && t < (int) s.mixer.size() && index < (int) s.mixer[(std::size_t) t].effects.size())
                    goals::setAmount (s.mixer[(std::size_t) t].effects[(std::size_t) index], v);
            });
        else
            slotActions.setSlotParam (-1, index, v); // the slot's Mix, as the Mixer sets it
    }

    void toggle (int index)
    {
        const auto t = insert;
        engine.editSong ([t, index] (Song& s) {
            if (t >= 0 && t < (int) s.mixer.size() && index < (int) s.mixer[(std::size_t) t].effects.size())
                s.mixer[(std::size_t) t].effects[(std::size_t) index].enabled = ! s.mixer[(std::size_t) t].effects[(std::size_t) index].enabled;
        });
    }

    void move (int index, int delta)
    {
        const auto t = insert;
        // (moveEffectBefore: `position` is the card it lands before, counted with the moved card still in place)
        const auto position = delta < 0 ? index - 1 : index + 2;
        if (position < 0 || position > (int) cards.size())
            return;
        engine.editSongStructural ([t, index, position] (Song& s) { detailchain::moveEffectBefore (s, t, index, position); });
    }

    void moveTo (int index, int position)
    {
        const auto t = insert;
        if (position == index || position == index + 1)
            return;
        engine.editSongStructural ([t, index, position] (Song& s) { detailchain::moveEffectBefore (s, t, index, position); });
    }

    void remove (int index) { slotActions.deleteSlot (index); }
    void openEditor (int index) { slotActions.toggleSlotEditor (index); }

    void applyFactory (const std::string& name)
    {
        const auto t = insert;
        if (const auto* preset = chains::find (name); preset != nullptr && t >= 0)
            engine.editSongStructural ([t, preset] (Song& s) { chains::apply (s, t, *preset); });
    }

    void askSaveName()
    {
        if (! hasEffects())
            return;
        const auto& mixer = engine.getSong().mixer[(std::size_t) insert];
        auto* window = new juce::AlertWindow (tr ("Save as my preset"), tr ("A name for this chain of effects. A preset of yours with the same name is replaced."),
                                              juce::MessageBoxIconType::NoIcon);
        const auto factory = chains::find (mixer.chainPreset) != nullptr;
        window->addTextEditor ("name", mixer.chainPreset.empty() ? tr ("My preset") : factory ? tr ("{0} (my version)", { utf8 (mixer.chainPreset) }) : utf8 (mixer.chainPreset));
        window->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<EffectsPage> safe (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe] (int result) {
                                     if (result == 1 && safe != nullptr)
                                         saveUserPreset (safe->engine, safe->insert, window->getTextEditorContents ("name"));
                                 }),
                                 true);
    }

    // --- dragging a card by its title --------------------------------------------------------------------------------
    void dragCard (int index, int stripX)
    {
        dragging = index;
        // the gap the card would land in: before the first card whose middle is right of the pointer
        int position = (int) cards.size();
        for (int i = 0; i < (int) cards.size(); ++i)
            if (stripX < cards[(std::size_t) i]->getBounds().getCentreX())
            {
                position = i;
                break;
            }
        if (dropAt != position)
        {
            dropAt = position;
            strip.repaint();
        }
    }
    void dropCard()
    {
        const auto from = dragging, to = dropAt;
        dragging = dropAt = -1;
        strip.repaint();
        if (from >= 0 && to >= 0)
            moveTo (from, to);
    }

    int cardCount() const noexcept { return (int) cards.size(); }
    int oneKnobCards() const noexcept
    {
        return (int) std::count_if (cards.begin(), cards.end(), [] (const auto& c) { return c->goal != 0; });
    }
    void turnForCheck (int card, float v)
    {
        if (card >= 0 && card < (int) cards.size())
            cards[(std::size_t) card]->knob.turnTo (v);
    }
    void blendForCheck (float v) { blend.turnTo (v); }
    void reverbForCheck (float v) { reverb.turnTo (v); }
    const juce::Component* cardForSnapshot (int card) const
    {
        return card >= 0 && card < (int) cards.size() ? cards[(std::size_t) card].get() : nullptr;
    }

    AudioEngine& engine;
    int insert = -1;

private:
    // One slot of the insert. A one-knob effect shows its goal and one big knob; any other effect its name and its Mix.
    class Card final : public juce::Component
    {
    public:
        Card (EffectsPage& o, int i) : owner (o), index (i)
        {
            knob.big = true;
            knob.onChange = [this] (float v) { owner.turn (index, goal != 0, v); };
            addAndMakeVisible (knob);
            editor.setButtonText (tr ("Editor"));
            editor.setTooltip (tr ("Opens the effect's own window with every control"));
            editor.onClick = [this] { owner.openEditor (index); };
            addAndMakeVisible (editor);
            left.setTooltip (tr ("Move earlier in the chain"));
            left.onClick = [this] { owner.move (index, -1); };
            right.setTooltip (tr ("Move later in the chain"));
            right.onClick = [this] { owner.move (index, 1); };
            close.setTooltip (tr ("Remove this effect"));
            close.onClick = [this] { owner.remove (index); };
            for (auto* b : { &left, &right, &close })
                addAndMakeVisible (*b);
        }

        void update (const EffectSlot& slot, int newIndex, int count)
        {
            index = newIndex;
            kind = slot.kind;
            goal = goals::active (slot) ? (int) slot.goal : 0;
            on = slot.enabled;
            const auto effectName = quickpick::slotName (owner.engine, slot);
            title = goal != 0 ? goalText (goal) : effectName;
            sub = goal != 0 ? effectName : tr ("Mix");
            knob.resetValue = goal != 0 ? goals::defaultAmount (goal) : 1.0f;
            knob.setValue (goal != 0 ? slot.goalAmount : slot.mix);
            knob.setDimmed (! on);
            knob.setTitle (title);
            knob.setTooltip (goal != 0 ? tr (goals::description (goal)) : tr ("How much of this effect is mixed in"));
            left.setEnabled (index > 0);
            right.setEnabled (index + 1 < count);
            repaint();
        }

        juce::Rectangle<int> ledBounds() const { return { 8, 7, 12, 12 }; }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bar = r.removeFromTop (titleHeight);
            close.setBounds (bar.removeFromRight (22).reduced (0, 2));
            right.setBounds (bar.removeFromRight (20).reduced (0, 2));
            left.setBounds (bar.removeFromRight (20).reduced (0, 2));
            r.reduce (pad, 6);
            editor.setBounds (r.removeFromBottom (24));
            r.removeFromBottom (4);
            r.removeFromBottom (16); // (the effect's own name, painted)
            knob.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 96), juce::jmin (r.getHeight(), 104)));
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            fillRoundRect (g, r, radius::lg, colour::bg2);
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (getLocalBounds().withHeight (titleHeight));
                fillRoundRect (g, r.withHeight ((float) titleHeight + radius::lg), radius::lg, colour::bg3);
            }
            g.setColour (owner.dragging == index ? colour::accent : colour::border);
            g.drawRoundedRectangle (r.reduced (0.5f), radius::lg, 1.0f);
            g.setColour (on ? colour::accent : colour::ledOff);
            g.fillEllipse (ledBounds().toFloat());
            if (ledHover)
            {
                g.setColour (colour::textPrimary.withAlpha (0.6f));
                g.drawEllipse (ledBounds().toFloat().expanded (1.5f), 1.0f);
            }
            g.setColour (on ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (type::body, true));
            g.drawText (title, juce::Rectangle<int> (26, 0, left.getX() - 28, titleHeight), juce::Justification::centredLeft, true);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (sub, juce::Rectangle<int> (pad, editor.getY() - 20, getWidth() - 2 * pad, 16), juce::Justification::centred, true);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool over = ledBounds().expanded (3).contains (e.getPosition());
            if (over != ledHover)
            {
                ledHover = over;
                repaint();
            }
            setMouseCursor (! over && e.y < titleHeight ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            ledHover = false;
            repaint();
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            pressedLed = ledBounds().expanded (3).contains (e.getPosition());
            pressedTitle = ! pressedLed && e.y < titleHeight;
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (pressedTitle && e.getDistanceFromDragStart() > 5)
                owner.dragCard (index, e.getEventRelativeTo (getParentComponent()).x);
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (owner.dragging == index)
                owner.dropCard();
            else if (pressedLed && ledBounds().expanded (3).contains (e.getPosition()))
                owner.toggle (index);
            pressedLed = pressedTitle = false;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.y < titleHeight && ! ledBounds().expanded (3).contains (e.getPosition()))
                owner.openEditor (index);
        }

        EffectsPage& owner;
        int index = 0, goal = 0;
        EffectKind kind = EffectKind::eq;
        bool on = true, ledHover = false, pressedLed = false, pressedTitle = false;
        juce::String title, sub;
        DockKnob knob;
        juce::TextButton editor;
        GlyphButton left { glyph ("\xe2\x80\xb9") }, right { glyph ("\xe2\x80\xba") }, close { glyph ("\xc3\x97") };
    };

    // The row of cards (inside the viewport): it paints the drop mark while a card is dragged
    class Strip final : public juce::Component
    {
    public:
        explicit Strip (EffectsPage& o) : owner (o) {}
        void paintOverChildren (juce::Graphics& g) override
        {
            if (owner.dragging < 0 || owner.dropAt < 0)
                return;
            const auto x = owner.dropAt < (int) owner.cards.size() ? owner.cards[(std::size_t) owner.dropAt]->getX() - cardGap / 2
                                                                  : owner.addCard.getX() - cardGap / 2;
            g.setColour (colour::accent);
            g.fillRect (x - 1, 0, 2, getHeight());
        }
        EffectsPage& owner;
    };

    void layoutStrip()
    {
        const auto h = juce::jmax (40, view.getHeight() - (view.isHorizontalScrollBarShown() ? view.getScrollBarThickness() : 0));
        int x = 0;
        for (auto& c : cards)
        {
            c->setBounds (x, 0, cardWidth, h);
            x += cardWidth + cardGap;
        }
        addCard.setBounds (x, 0, addWidth, h);
        x += addWidth;
        const auto hintWidth = emptyHint.getText().isEmpty() ? 0 : 360;
        emptyHint.setBounds (x + 12, 0, hintWidth, h);
        strip.setSize (juce::jmax (view.getWidth(), x + (hintWidth > 0 ? hintWidth + 12 : 0)), h);
    }

    bool hasEffects() const
    {
        const auto& song = engine.getSong();
        return insert >= 0 && insert < (int) song.mixer.size() && ! song.mixer[(std::size_t) insert].effects.empty();
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

    EffectsPanel slotActions;
    juce::Label presetCaption, blendCaption, reverbCaption, emptyHint;
    juce::TextButton presetButton, saveButton;
    AddCardButton addCard;
    DockKnob blend, reverb;
    juce::Viewport view;
    Strip strip { *this };
    std::vector<std::unique_ptr<Card>> cards;
    int dragging = -1, dropAt = -1;
};

// =====================================================================================================================
// Instrument
// =====================================================================================================================
class SimpleDock::InstrumentPage final : public juce::Component,
                                         private juce::Timer
{
public:
    InstrumentPage (AudioEngine& e, Hooks& h) : engine (e), hooks (h), categoryModel (*this), soundModel (*this)
    {
        styleCaption (soundCaption, tr ("Sound"));
        addAndMakeVisible (soundCaption);
        prev.setTooltip (tr ("Previous sound"));
        prev.onClick = [this] { stepSound (-1); };
        next.setTooltip (tr ("Next sound"));
        next.onClick = [this] { stepSound (1); };
        addAndMakeVisible (prev);
        addAndMakeVisible (next);
        name.setFont (font (type::title, true));
        name.setColour (juce::Label::textColourId, colour::textPrimary);
        name.setJustificationType (juce::Justification::centred);
        name.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (name);
        styleCaption (detail, {});
        detail.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (detail);
        editorButton.setButtonText (tr ("Open full editor"));
        editorButton.setTooltip (tr ("Opens the instrument's own window with every control"));
        editorButton.onClick = [this] {
            if (channel >= 0 && hooks.toggleChannelWindow)
                hooks.toggleChannelWindow (channel);
        };
        addAndMakeVisible (editorButton);
        keysButton.setButtonText (tr ("Keyboard"));
        keysButton.setTooltip (tr ("Shows the on-screen keyboard to play this track"));
        keysButton.onClick = [this] {
            if (hooks.showKeys)
                hooks.showKeys();
        };
        addAndMakeVisible (keysButton);

        search.setTextToShowWhenEmpty (tr ("Search sounds"), colour::textDisabled);
        search.onTextChange = [this] { refilter(); };
        search.setTitle (tr ("Search sounds"));
        addAndMakeVisible (search);
        ownButton.setButtonText (tr ("LYNX sounds"));
        fontsButton.setButtonText (tr ("My SoundFonts"));
        fontsButton.setTooltip (tr ("The sounds of your own .sf2 and .sf3 files"));
        pluginsButton.setButtonText (tr ("My plugins"));
        pluginsButton.setTooltip (tr ("The instrument plugins installed on this computer"));
        for (auto* b : { &ownButton, &fontsButton, &pluginsButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1902);
            addAndMakeVisible (*b);
        }
        ownButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        fontsButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        pluginsButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        ownButton.setToggleState (true, juce::dontSendNotification);
        ownButton.onClick = [this] {
            if (ownButton.getToggleState())
                setSource (From::own);
        };
        fontsButton.onClick = [this] {
            if (fontsButton.getToggleState())
                setSource (From::fonts);
        };
        pluginsButton.onClick = [this] {
            if (pluginsButton.getToggleState())
                setSource (From::plugins);
        };
        folderButton.setButtonText (tr ("Add folder..."));
        folderButton.setTooltip (tr ("Adds a folder with your SoundFont files to the list"));
        folderButton.onClick = [this] { addFolder(); };
        addChildComponent (folderButton);

        categories.setModel (&categoryModel);
        categories.setRowHeight (rowHeight);
        categories.setColour (juce::ListBox::backgroundColourId, colour::bg1);
        categories.setTitle (tr ("Category"));
        addAndMakeVisible (categories);
        sounds.setModel (&soundModel);
        sounds.setRowHeight (rowHeight);
        sounds.setColour (juce::ListBox::backgroundColourId, colour::bg1);
        sounds.setTitle (tr ("Sounds"));
        addAndMakeVisible (sounds);
        styleCaption (listHint, {});
        listHint.setFont (font (type::body));
        listHint.setJustificationType (juce::Justification::centred);
        addChildComponent (listHint);
    }

    ~InstrumentPage() override { stopTimer(); }

    void show (int newChannel)
    {
        channel = newChannel;
        refresh();
    }

    void refresh()
    {
        if (! isVisible()) // (the Effects tab is in front: nothing of the list is worked out until it shows)
        {
            stale = true;
            return;
        }
        stale = false;
        const auto& song = engine.getSong();
        const bool has = channel >= 0 && channel < (int) song.channels.size();
        prev.setEnabled (has);
        next.setEnabled (has);
        editorButton.setEnabled (has);
        if (! has)
        {
            name.setText ({}, juce::dontSendNotification);
            detail.setText ({}, juce::dontSendNotification);
            current = -1;
            sounds.repaint();
            return;
        }
        const auto& def = song.channels[(std::size_t) channel];
        // which sound of the list the channel plays (looked up again only when its instrument changed)
        if (! known || ! soundlib::sameSound (lastDef, def) || lastListSize != list().size())
        {
            current = soundlib::find (list(), def);
            currentOwn = from != From::own ? soundlib::find (soundlib::builtIn(), def) : current;
            lastDef = def;
            lastListSize = list().size();
            known = true;
        }
        const soundlib::Sound* sound = current >= 0 ? &list()[(std::size_t) current]
                                       : currentOwn >= 0 ? &soundlib::builtIn()[(std::size_t) currentOwn] : nullptr;
        if (sound != nullptr)
        {
            name.setText (utf8 (sound->name), juce::dontSendNotification);
            detail.setText (soundCategoryText (sound->category) + glyph (" \xc2\xb7 ") + utf8 (sound->origin), juce::dontSendNotification);
        }
        else
        {
            name.setText (utf8 (def.name), juce::dontSendNotification);
            detail.setText (tr ("Its own sound"), juce::dontSendNotification);
        }
        if (current >= 0 && ! categoryPicked)
            category = (int) list()[(std::size_t) current].category;
        else if (! categoryPicked && soundlib::filter (list(), (soundlib::Category) category).empty())
            for (int c = 0; c < (int) soundlib::Category::count; ++c) // (the first category this list has sounds in)
                if (! soundlib::filter (list(), (soundlib::Category) c).empty())
                {
                    category = c;
                    break;
                }
        refilter();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad);
        auto side = r.removeFromLeft (sideWidth - pad);
        soundCaption.setBounds (side.removeFromTop (16));
        auto row = side.removeFromTop (30);
        prev.setBounds (row.removeFromLeft (26));
        next.setBounds (row.removeFromRight (26));
        name.setBounds (row);
        detail.setBounds (side.removeFromTop (18));
        side.removeFromTop (8);
        editorButton.setBounds (side.removeFromTop (26));
        side.removeFromTop (6);
        keysButton.setBounds (side.removeFromTop (26));
        r.removeFromLeft (pad + 1 + pad);
        auto top = r.removeFromTop (26);
        ownButton.setBounds (top.removeFromLeft (juce::jmin (120, top.getWidth() / 5)));
        fontsButton.setBounds (top.removeFromLeft (juce::jmin (130, top.getWidth() / 4)));
        pluginsButton.setBounds (top.removeFromLeft (juce::jmin (110, top.getWidth() / 3)));
        top.removeFromLeft (pad);
        if (folderButton.isVisible())
        {
            folderButton.setBounds (top.removeFromRight (juce::jmin (130, top.getWidth() / 3)));
            top.removeFromRight (pad);
        }
        search.setBounds (top.withWidth (juce::jmin (top.getWidth(), 280)));
        r.removeFromTop (6);
        categories.setBounds (r.removeFromLeft (juce::jmin (categoryWidth, r.getWidth() / 3)));
        r.removeFromLeft (6);
        sounds.setBounds (r);
        listHint.setBounds (r.reduced (12));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::border);
        g.fillRect (sideWidth, pad, 1, getHeight() - 2 * pad);
    }

    void visibilityChanged() override
    {
        if (isVisible() && stale)
            refresh();
        updateTimer();
    }
    void parentHierarchyChanged() override { updateTimer(); }

    // --- the list ----------------------------------------------------------------------------------------------------
    enum class From { own, fonts, plugins };
    const std::vector<soundlib::Sound>& list() const
    {
        return from == From::fonts ? fontSounds : from == From::plugins ? pluginSounds : soundlib::builtIn();
    }

    void setSource (From source)
    {
        from = source;
        fonts = from == From::fonts;
        (from == From::fonts ? fontsButton : from == From::plugins ? pluginsButton : ownButton).setToggleState (true, juce::dontSendNotification);
        folderButton.setVisible (fonts);
        if (fonts && ! injected)
        {
            ensureSoundFontIndex();
            takeFonts();
        }
        if (from == From::plugins && ! pluginsInjected)
            takePlugins();
        known = false;
        categoryPicked = false;
        resized();
        refresh();
        updateTimer();
    }

    void setFonts (std::vector<sf::IndexedFont> indexed, bool inject)
    {
        injected = inject;
        fontSounds = soundlib::fromFonts (indexed);
        known = false;
    }

    void pickCategory (int c)
    {
        category = juce::jlimit (0, (int) soundlib::Category::count - 1, c);
        categoryPicked = true;
        refilter();
    }

    void refilter()
    {
        rows = soundlib::filter (list(), (soundlib::Category) category, search.getText().toStdString());
        counts.assign ((std::size_t) soundlib::Category::count, 0);
        for (int c = 0; c < (int) soundlib::Category::count; ++c)
            counts[(std::size_t) c] = (int) soundlib::filter (list(), (soundlib::Category) c, search.getText().toStdString()).size();
        // the categories this source has sounds in (a search greys the ones without a match, it doesn't remove them)
        shownCategories.clear();
        for (int c = 0; c < (int) soundlib::Category::count; ++c)
            if (! soundlib::filter (list(), (soundlib::Category) c).empty())
                shownCategories.push_back (c);
        categories.updateContent();
        const auto shown = std::find (shownCategories.begin(), shownCategories.end(), category);
        if (shown != shownCategories.end())
            categories.selectRow ((int) (shown - shownCategories.begin()), true, true);
        else
            categories.deselectAllRows();
        categories.repaint();
        sounds.updateContent();
        const auto it = std::find (rows.begin(), rows.end(), current);
        if (it != rows.end())
            sounds.selectRow ((int) (it - rows.begin()), true, true);
        else
            sounds.deselectAllRows();
        sounds.repaint();
        const bool noFonts = fonts && fontSounds.empty();
        const bool noPlugins = from == From::plugins && pluginSounds.empty();
        listHint.setVisible (noFonts || noPlugins || rows.empty());
        listHint.setText (noFonts ? (sf::PresetIndex::shared().busy() && ! injected
                                         ? tr ("Looking for your SoundFont files...")
                                         : tr ("No SoundFont files found yet. Add a folder with your .sf2 or .sf3 files."))
                          : noPlugins ? tr ("No instrument plugins found yet. Scan for plugins in the Plugin manager.")
                          : rows.empty() ? tr ("No sounds here.") : juce::String(),
                          juce::dontSendNotification);
    }

    // --- picking and hearing -----------------------------------------------------------------------------------------
    bool audition (const soundlib::Sound& s)
    {
        const auto def = soundlib::channelFor (s);
        return def && engine.auditionSound (*def, soundlib::auditionKeys (s));
    }

    void pick (int listIndex, bool hear)
    {
        if (listIndex < 0 || listIndex >= (int) list().size() || channel < 0)
            return;
        const auto sound = list()[(std::size_t) listIndex];
        const auto c = channel;
        bool done = false;
        engine.editSongStructural ([c, &sound, &done] (Song& s) {
            if (c >= (int) s.channels.size())
                return;
            if (auto next = soundlib::withSound (s.channels[(std::size_t) c], sound))
            {
                s.channels[(std::size_t) c] = std::move (*next);
                done = true;
            }
        });
        if (done && hear)
        {
            if (sound.source == soundlib::Source::plugin) // (heard on the track itself, as it loaded)
                engine.auditionChannelSound (c);
            else
                audition (sound);
        }
    }

    void stepSound (int delta)
    {
        const auto fallback = (soundlib::Category) category;
        const auto to = soundlib::step (list(), current, delta, fallback);
        if (to >= 0)
        {
            categoryPicked = false;
            pick (to, true);
        }
    }

    void clickRow (int row, bool previewOnly)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        // (a plugin can only be heard once it runs on the track: its ▶ uses it too, and Ctrl+Z goes back)
        if (previewOnly && list()[(std::size_t) rows[(std::size_t) row]].source != soundlib::Source::plugin)
            audition (list()[(std::size_t) rows[(std::size_t) row]]);
        else
            pick (rows[(std::size_t) row], true);
    }

    int rowCount() const noexcept { return (int) rows.size(); }
    void searchFor (const juce::String& text)
    {
        search.setText (text, false);
        refilter(); // (what typing does, at once: the editor's own change message is posted)
    }
    juce::String shownName() const { return name.getText() + " [" + detail.getText() + "]"; }

    AudioEngine& engine;
    Hooks& hooks;
    int channel = -1;

private:
    struct CategoryModel final : juce::ListBoxModel
    {
        explicit CategoryModel (InstrumentPage& p) : page (p) {}
        int getNumRows() override { return (int) page.shownCategories.size(); }
        void paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (index < 0 || index >= (int) page.shownCategories.size())
                return;
            const auto row = page.shownCategories[(std::size_t) index];
            const auto n = row < (int) page.counts.size() ? page.counts[(std::size_t) row] : 0;
            if (selected)
                fillRoundRect (g, juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f), radius::sm, colour::accentMuted);
            g.setColour (n == 0 ? colour::textDisabled : selected ? colour::textPrimary : colour::textSecondary);
            g.setFont (font (type::body, selected));
            g.drawText (soundCategoryText ((soundlib::Category) row), 10, 0, width - 50, height, juce::Justification::centredLeft, true);
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawText (juce::String (n), width - 40, 0, 32, height, juce::Justification::centredRight, false);
        }
        void selectedRowsChanged (int index) override
        {
            if (index >= 0 && index < (int) page.shownCategories.size() && page.shownCategories[(std::size_t) index] != page.category)
                page.pickCategory (page.shownCategories[(std::size_t) index]);
        }
        juce::String getNameForRow (int index) override
        {
            return index >= 0 && index < (int) page.shownCategories.size() ? soundCategoryText ((soundlib::Category) page.shownCategories[(std::size_t) index])
                                                                           : juce::String();
        }
        InstrumentPage& page;
    };

    struct SoundModel final : juce::ListBoxModel
    {
        explicit SoundModel (InstrumentPage& p) : page (p) {}
        int getNumRows() override { return (int) page.rows.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool) override
        {
            if (row < 0 || row >= (int) page.rows.size())
                return;
            const auto index = page.rows[(std::size_t) row];
            const auto& s = page.list()[(std::size_t) index];
            const bool isCurrent = index == page.current;
            if (isCurrent)
                fillRoundRect (g, juce::Rectangle<float> (0.0f, 1.0f, (float) width, (float) height - 2.0f), radius::sm, colour::accentMuted);
            else if (row % 2 == 1)
                g.fillAll (colour::bg2.withAlpha (0.5f));
            // ▶: hear it without choosing it
            juce::Path play;
            const auto cx = 15.0f, cy = (float) height * 0.5f;
            play.addTriangle (cx - 4.0f, cy - 5.5f, cx - 4.0f, cy + 5.5f, cx + 5.5f, cy);
            g.setColour (isCurrent ? colour::accent : colour::textSecondary);
            g.fillPath (play);
            g.setColour (isCurrent ? colour::textPrimary : colour::textPrimary.withAlpha (0.9f));
            g.setFont (font (type::body, isCurrent));
            const auto originWidth = juce::jmin (180, width / 3);
            g.drawText (utf8 (s.name), 32, 0, width - 40 - originWidth, height, juce::Justification::centredLeft, true);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small));
            g.drawText (utf8 (s.origin), width - originWidth - 8, 0, originWidth, height, juce::Justification::centredRight, true);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override { page.clickRow (row, e.x < 30); }
        void returnKeyPressed (int row) override { page.clickRow (row, false); }
        juce::String getNameForRow (int row) override
        {
            return row >= 0 && row < (int) page.rows.size() ? utf8 (page.list()[(std::size_t) page.rows[(std::size_t) row]].name) : juce::String();
        }
        juce::String getTooltipForRow (int) override
        {
            return page.from == From::plugins ? tr ("Click to load this plugin on the track and hear it (Ctrl+Z goes back)")
                                              : tr ("Click to use this sound; the arrow plays it without choosing it");
        }
        InstrumentPage& page;
    };

    // the plugin host's scanned instruments (the list the plugin picker shows)
    void takePlugins()
    {
        std::vector<soundlib::InstalledPlugin> installed;
        for (const auto& t : engine.getPluginHost().getKnownPlugins().getTypes())
            if (t.isInstrument)
                installed.push_back ({ t.createIdentifierString().toStdString(), t.name.toStdString(), t.manufacturerName.toStdString(),
                                       t.pluginFormatName.toStdString(), t.category.toStdString() });
        pluginSounds = soundlib::fromPlugins (installed);
        known = false;
    }

public:
    void setPlugins (std::vector<soundlib::InstalledPlugin> installed)
    {
        pluginsInjected = true;
        pluginSounds = soundlib::fromPlugins (installed);
        known = false;
    }

private:
    void takeFonts()
    {
        auto& index = sf::PresetIndex::shared();
        generation = index.generation();
        fontSounds = soundlib::fromFonts (index.fonts());
        known = false;
    }

    // the SoundFont index fills in the background: follow it only while its list is on screen
    void updateTimer()
    {
        if (fonts && ! injected && isShowing())
            startTimer (500);
        else
            stopTimer();
    }
    void timerCallback() override
    {
        if (sf::PresetIndex::shared().generation() != generation)
        {
            takeFonts();
            refresh();
        }
    }

    void addFolder()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Add a SoundFont folder"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory));
        juce::Component::SafePointer<InstrumentPage> safe (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                 [safe] (const juce::FileChooser& fc) {
                                     if (fc.getResult() == juce::File())
                                         return;
                                     // (the same list the LYNX Soundfont editor's "SoundFont folders" menu keeps)
                                     auto lines = juce::StringArray::fromLines (soundFontFoldersFile().loadFileAsString());
                                     lines.removeEmptyStrings();
                                     lines.addIfNotAlreadyThere (fc.getResult().getFullPathName());
                                     soundFontFoldersFile().getParentDirectory().createDirectory();
                                     soundFontFoldersFile().replaceWithText (lines.joinIntoString ("\n") + "\n");
                                     ensureSoundFontIndex (true);
                                     if (safe != nullptr)
                                         safe->refilter();
                                 });
    }

    juce::Label soundCaption, name, detail, listHint;
    GlyphButton prev { glyph ("\xe2\x80\xb9") }, next { glyph ("\xe2\x80\xba") };
    juce::TextButton editorButton, keysButton, ownButton, fontsButton, pluginsButton, folderButton;
    juce::TextEditor search;
    CategoryModel categoryModel;
    SoundModel soundModel;
    juce::ListBox categories, sounds;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<soundlib::Sound> fontSounds, pluginSounds;
    std::vector<int> rows, counts, shownCategories;
    ChannelDef lastDef;
    std::size_t lastListSize = 0;
    int category = 0, current = -1, currentOwn = -1, generation = -1;
    bool fonts = false, injected = false, pluginsInjected = false, known = false, categoryPicked = false, stale = true;
    From from = From::own;
};

// =====================================================================================================================
// The dock
// =====================================================================================================================
// --- Editor: the Piano roll docked on the track's pattern (round 190ds) ---------------------------------------------
class SimpleDock::EditorPage final : public juce::Component
{
public:
    EditorPage (AudioEngine& e, Hooks& h) : engine (e), hooks (h)
    {
        fold.setButtonText (tr ("Only notes in key"));
        fold.setTooltip (tr ("Shows only the rows of the project key's notes"));
        fold.onClick = [this] {
            if (hooks.setFoldToKey)
                hooks.setFoldToKey (fold.getToggleState());
        };
        open.setButtonText (tr ("Open in the Piano roll"));
        open.setTooltip (tr ("The full Piano roll window, with every tool"));
        open.onClick = [this] {
            if (hooks.openPianoRollPanel)
                hooks.openPianoRollPanel();
        };
        again.setButtonText (tr ("Show the editor here"));
        again.onClick = [this] {
            takenBack = false;
            update();
        };
        makePattern.setButtonText (tr ("New pattern"));
        makePattern.setTooltip (tr ("An empty 4-bar pattern on this track, to write notes into"));
        makePattern.onClick = [this] { make(); };
        addAndMakeVisible (fold);
        addAndMakeVisible (open);
        addChildComponent (again);
        addChildComponent (makePattern);
    }

    ~EditorPage() override { release(); }

    void show (int newTrack, int newChannel)
    {
        if (newTrack != track)
            takenBack = false;
        track = newTrack;
        channel = newChannel;
        update();
    }

    // Docks the Piano roll on the track's pattern while this page shows. A track without a pattern gets one the first
    // time (after this call returns: no song edit inside the dock's own refresh); after an undo removed it, a button.
    void update()
    {
        const auto& song = engine.getSong();
        const auto pattern = track >= 0 ? simpletracks::patternOf (song, track) : -1;
        const auto canHave = track >= 0 && (simpletracks::kindOf (song, track) == simpletracks::Kind::instrument
                                            || simpletracks::kindOf (song, track) == simpletracks::Kind::sampler);
        if (! shownInTree() || ! canHave)
        {
            release();
            refreshControls (pattern);
            return;
        }
        if (pattern < 0)
        {
            release();
            if (made.count (song.playlistTracks[(std::size_t) track].channel) == 0 && ! pendingMake)
            {
                pendingMake = true;
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<EditorPage> (this)] {
                    if (safe != nullptr)
                    {
                        safe->pendingMake = false;
                        if (safe->shownInTree())
                            safe->make();
                    }
                });
            }
            refreshControls (pattern);
            return;
        }
        if (channel >= 0 && engine.getSelectedChannel() != channel)
            engine.setSelectedChannel (channel); // (the Piano roll shows the selected channel's notes)
        if (! takenBack && (editor == nullptr || dockedPattern != pattern || song.currentPattern != pattern) && hooks.dockPianoRoll)
        {
            if (auto* e = hooks.dockPianoRoll (pattern))
            {
                editor = e;
                dockedPattern = pattern;
                addAndMakeVisible (*editor);
                resized();
            }
        }
        refreshControls (pattern);
    }

    void release()
    {
        if (editor != nullptr && hooks.undockPianoRoll)
        {
            editor = nullptr;
            hooks.undockPianoRoll();
        }
        editor = nullptr;
        dockedPattern = -1;
    }

    void takenBackByPanel()
    {
        if (editor != nullptr)
        {
            editor = nullptr;
            dockedPattern = -1;
            takenBack = true;
        }
        refreshControls (dockedPattern);
    }

    bool docked() const noexcept { return editor != nullptr; }
    void clickFold (bool on)
    {
        fold.setToggleState (on, juce::dontSendNotification);
        if (fold.onClick)
            fold.onClick();
    }

    void visibilityChanged() override { update(); }
    void parentHierarchyChanged() override
    {
        if (! shownInTree())
            release();
    }

    // visible with all its parents (isShowing() also asks for a window on the desktop, which --snapshot has not)
    bool shownInTree() const
    {
        for (auto* c = static_cast<const juce::Component*> (this); c != nullptr; c = c->getParentComponent())
            if (! c->isVisible())
                return false;
        return getParentComponent() != nullptr;
    }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (10, 4);
        open.setBounds (bar.removeFromRight (170));
        bar.removeFromRight (8);
        fold.setBounds (bar.removeFromRight (160));
        const auto body = getLocalBounds().withTrimmedTop (barHeight);
        again.setBounds (body.withSizeKeepingCentre (220, 30));
        makePattern.setBounds (body.withSizeKeepingCentre (160, 30));
        if (editor != nullptr)
            editor->setBounds (body);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg1);
        const auto& song = engine.getSong();
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (12, 0);
        const auto pattern = track >= 0 ? simpletracks::patternOf (song, track) : -1;
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        if (pattern >= 0)
            g.drawText (tr ("Notes of {0}", { utf8 (song.patterns[(std::size_t) pattern].name) }), bar, juce::Justification::centredLeft, true);
        if (editor == nullptr)
        {
            g.setFont (font (type::body));
            const auto body = getLocalBounds().withTrimmedTop (barHeight).withTrimmedBottom (80);
            g.drawText (takenBack ? tr ("The Piano roll is open in its own window") : pattern < 0 ? tr ("This track has no pattern yet") : juce::String(),
                        body, juce::Justification::centred, true);
        }
    }

    static constexpr int barHeight = 30;

private:
    void make()
    {
        if (track < 0)
            return;
        const auto t = track;
        int ch = -1;
        if (t < (int) engine.getSong().playlistTracks.size())
            ch = engine.getSong().playlistTracks[(std::size_t) t].channel;
        made.insert (ch);
        engine.editSong ([t] (Song& s) { simpletracks::ensurePattern (s, t, 4); });
        update();
    }

    void refreshControls (int pattern)
    {
        const auto& song = engine.getSong();
        fold.setEnabled (projectkey::get (song).has_value() && editor != nullptr);
        fold.setToggleState (hooks.foldedToKey && hooks.foldedToKey(), juce::dontSendNotification);
        again.setVisible (takenBack && editor == nullptr);
        makePattern.setVisible (! takenBack && editor == nullptr && pattern < 0 && track >= 0 && ! pendingMake);
        repaint();
    }

    AudioEngine& engine;
    Hooks& hooks;
    juce::ToggleButton fold;
    juce::TextButton open, again, makePattern;
    juce::Component* editor = nullptr; // the main window's Piano roll while docked here (not owned)
    int track = -1, channel = -1, dockedPattern = -1;
    bool takenBack = false, pendingMake = false;
    std::set<int> made; // channels whose pattern this page made once (an undo then asks, it doesn't make it again)
};

// =====================================================================================================================
// Pads (round 190fo): the Sampler track's 16 pads (core/SimplePads.h)
// =====================================================================================================================
namespace
{
// LYNX's own pad colours (index 1…8; 0 = the track's colour)
juce::Colour padColour (int index, juce::Colour track)
{
    static const juce::uint32 palette[simplepads::colourCount] { 0xffe5383b, 0xffe8833a, 0xffe2c044, 0xff5fbf6a,
                                                                0xff3fb6b0, 0xff4f8ee0, 0xff9a6fe0, 0xffe06aa8 };
    return index >= 1 && index <= simplepads::colourCount ? juce::Colour (palette[index - 1]) : track;
}

juce::String padColourName (int index)
{
    switch (index)
    {
        case 1: return trc ("@colour: Red");
        case 2: return trc ("@colour: Orange");
        case 3: return trc ("@colour: Yellow");
        case 4: return trc ("@colour: Green");
        case 5: return trc ("@colour: Teal");
        case 6: return trc ("@colour: Blue");
        case 7: return trc ("@colour: Violet");
        case 8: return trc ("@colour: Pink");
        default: break;
    }
    return tr ("Track colour");
}

// the pad knobs' names
juce::String padKnobName (int k)
{
    switch (k)
    {
        case 0: return tr ("Attack");
        case 1: return tr ("Release");
        case 2: return tr ("Tone");
        case 3: return tr ("Pitch");
        case 4: return trc ("@mixer: Pan");
        default: break;
    }
    return trc ("@mixer: Volume");
}

juce::String modeText (simplepads::Mode m)
{
    return m == simplepads::Mode::gate ? trc ("@pad mode: Gate") : m == simplepads::Mode::oneShot ? tr ("One-shot") : trc ("@pad mode: Loop");
}

juce::String toneLabel (float tone)
{
    const auto t = simplepads::toneFilter (tone);
    if (! t.on)
        return tr ("Off");
    const auto hz = t.cutoff >= 1000.0f ? juce::String (t.cutoff / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (t.cutoff)) + " Hz";
    return (t.filterType == 0 ? tr ("Low-pass") : tr ("High-pass")) + " " + hz;
}

// the knobs' laws: the dock's knob is 0…1
float attackFrom (float v) { return v * v * simplepads::maxAttack; }
float attackTo (float s) { return std::sqrt (juce::jlimit (0.0f, 1.0f, s / simplepads::maxAttack)); }
float releaseFrom (float v) { return v * v * simplepads::maxRelease; }
float releaseTo (float s) { return std::sqrt (juce::jlimit (0.0f, 1.0f, s / simplepads::maxRelease)); }
juce::String secondsText (float s) { return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms" : juce::String (s, 2) + " s"; }
juce::String panText (float pan)
{
    const auto p = juce::roundToInt (pan * 100.0f);
    return p == 0 ? tr ("Centre") : p < 0 ? "L " + juce::String (-p) : "R " + juce::String (p);
}
juce::String pitchText (float semis)
{
    const auto cents = juce::roundToInt (semis * 100.0f);
    if (cents % 100 == 0)
        return (cents > 0 ? "+" : "") + juce::String (cents / 100) + " st";
    return (semis > 0.0f ? "+" : "") + juce::String (semis, 2) + " st";
}
} // namespace

class SimpleDock::PadsPage final : public juce::Component,
                                   public juce::FileDragAndDropTarget,
                                   public juce::DragAndDropTarget,
                                   private juce::Timer
{
public:
    explicit PadsPage (AudioEngine& e) : engine (e)
    {
        load.setButtonText (tr ("Import..."));
        load.setTooltip (tr ("Put an audio file on the selected pad"));
        load.onClick = [this] { chooseFile (selected); };
        browse.setButtonText (tr ("Browse sounds"));
        browse.setTooltip (tr ("The sample browser: drag a sound onto a pad"));
        browse.onClick = [this] {
            if (onBrowse)
                onBrowse();
        };
        kit.setButtonText (tr ("Kit"));
        kit.setTooltip (tr ("Save the pads as a kit, open one of your kits, or fill the pads from a folder"));
        kit.onClick = [this] { showKitMenu(); };
        reverse.setButtonText (tr ("Reverse"));
        reverse.onClick = [this] { edit ([on = reverse.getToggleState()] (ChannelDef& c) { c.sampler.reverse = on; }); };
        normalize.setButtonText (tr ("Normalize"));
        normalize.setTooltip (tr ("Brings the pad's loudest point to full level"));
        normalize.onClick = [this] { normalizePad(); };
        group.setTooltip (tr ("Pads of the same group cut each other off (an open and a closed hi-hat)"));
        group.onClick = [this] { showGroupMenu(); };
        for (int m = 0; m < 3; ++m)
        {
            auto& b = modes[(std::size_t) m];
            b.setButtonText (modeText ((simplepads::Mode) m));
            b.setClickingTogglesState (true);
            b.setRadioGroupId (1902);
            b.onClick = [this, m, &b] {
                if (b.getToggleState())
                    edit ([m] (ChannelDef& c) { c.simplePad.mode = (std::uint8_t) m; });
            };
            addAndMakeVisible (b);
        }
        modes[0].setTooltip (tr ("Plays while the key is held"));
        modes[1].setTooltip (tr ("Plays to its end, however short the hit"));
        modes[2].setTooltip (tr ("Repeats while the key is held"));

        for (int k = 0; k < knobCount; ++k)
        {
            auto& knob = knobs[(std::size_t) k];
            styleCaption (captions[(std::size_t) k], padKnobName (k));
            captions[(std::size_t) k].setJustificationType (juce::Justification::centred);
            addAndMakeVisible (captions[(std::size_t) k]);
            knob.onChange = [this, k] (float v) { turn (k, v); };
            addAndMakeVisible (knob);
        }
        knobs[0].resetValue = 0.0f;
        knobs[0].valueText = [] (float v) { return secondsText (attackFrom (v)); };
        knobs[1].resetValue = 0.0f;
        knobs[1].valueText = [] (float v) { return secondsText (releaseFrom (v)); };
        knobs[2].valueText = [] (float v) { return toneLabel (v * 2.0f - 1.0f); };
        knobs[3].valueText = [] (float v) { return pitchText ((v * 2.0f - 1.0f) * simplepads::maxPitch); };
        knobs[4].valueText = [] (float v) { return panText (v * 2.0f - 1.0f); };
        knobs[5].resetValue = 0.78f;
        knobs[2].setTooltip (tr ("Left: darker (low-pass). Right: thinner (high-pass). Middle: off"));
        knobs[3].setTooltip (tr ("In semitones; Shift + drag for cents"));
        for (auto* c : std::initializer_list<juce::Component*> { &load, &browse, &kit, &reverse, &normalize, &group })
            addAndMakeVisible (c);
        setWantsKeyboardFocus (false);
    }

    std::function<void()> onBrowse;

    void show (int newTrack)
    {
        track = newTrack;
        update();
    }

    void update()
    {
        const auto& song = engine.getSong();
        channels = simplepads::padChannels (song, track);
        const auto c = channelOf (selected);
        const bool has = c != nullptr && ! c->samplePath.empty();
        if (c != nullptr)
        {
            const auto mode = simplepads::modeOf (*c);
            modes[(std::size_t) mode].setToggleState (true, juce::dontSendNotification);
            knobs[0].setValue (attackTo (c->sampler.attack));
            knobs[1].setValue (releaseTo (c->simplePad.release));
            knobs[2].setValue ((c->simplePad.tone + 1.0f) * 0.5f);
            knobs[3].setValue ((c->sampler.pitch / simplepads::maxPitch + 1.0f) * 0.5f);
            knobs[4].setValue ((c->pan + 1.0f) * 0.5f);
            knobs[5].setValue (c->volume);
            reverse.setToggleState (c->sampler.reverse, juce::dontSendNotification);
            group.setButtonText (tr ("Group: {0}", { c->simplePad.group > 0 ? juce::String (c->simplePad.group) : tr ("None") }));
            knobs[1].setDimmed (mode == simplepads::Mode::oneShot);
        }
        for (auto* comp : std::initializer_list<juce::Component*> { &reverse, &normalize, &group, &modes[0], &modes[1], &modes[2] })
            comp->setEnabled (has);
        for (auto& k : knobs)
            k.setEnabled (has);
        waveFor = {};
        resized();
        repaint();
    }

    int selectedPad() const noexcept { return selected; }
    void select (int pad)
    {
        selected = juce::jlimit (0, simplepads::padCount - 1, pad);
        update();
    }
    // a click on pad `pad`, as the mouse does (tests, --reabl190foshot)
    void hit (int pad)
    {
        select (pad);
        lightPad (selected);
        if (const auto ch = channelIndex (selected); ch >= 0 && hasSound (selected))
        {
            engine.sendLiveNote (ch, simplepads::padKey, 0.8f, true, true);
            engine.sendLiveNote (ch, simplepads::padKey, 0.0f, false, true);
        }
        repaint();
    }
    // tests / shots: the same edits the controls make
    void loadFileForCheck (int pad, const juce::File& file) { loadFile (pad, file); }
    void turnForCheck (int knob, float v) { knobs[(std::size_t) knob].turnTo (v); }
    void setModeForCheck (int mode) { modes[(std::size_t) mode].triggerClick(); }
    void dragPadForCheck (int from, int to, bool copy) { dropPad (from, to, copy); }
    void setRegionForCheck (float start, float end)
    {
        edit ([start, end] (ChannelDef& c) {
            c.sampler.regionStart = start;
            c.sampler.regionEnd = end;
        });
    }
    void normalizeForCheck() { normalizePad(); }
    juce::PopupMenu kitMenuForSnapshot() { return kitMenu(); }
    juce::PopupMenu padMenuForSnapshot (int pad) { return padMenu (pad); }

    void paint (juce::Graphics& g) override
    {
        const auto& song = engine.getSong();
        const auto trackColour = juce::Colour (track >= 0 && track < (int) song.playlistTracks.size() && song.playlistTracks[(std::size_t) track].colour != 0
                                                   ? song.playlistTracks[(std::size_t) track].colour
                                                   : simpletracks::defaultColour (simpletracks::Kind::pads));
        for (int p = 0; p < simplepads::padCount; ++p)
        {
            const auto r = padBounds (p).toFloat();
            const auto* c = channelOf (p);
            const bool has = c != nullptr && ! c->samplePath.empty();
            const auto tint = padColour (c != nullptr ? c->simplePad.colour : 0, trackColour);
            const bool lit = flash[(std::size_t) p] > 0;
            if (has)
            {
                fillRoundRect (g, r, radius::md, tint.withMultipliedBrightness (lit ? 1.0f : 0.55f).withMultipliedSaturation (lit ? 1.0f : 0.8f));
                g.setColour (lit ? colour::onAccent : colour::textPrimary);
                g.setFont (font (type::small, true));
                g.drawFittedText (utf8 (c->name), r.reduced (6.0f, 4.0f).withTrimmedTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);
            }
            else
            {
                fillRoundRect (g, r, radius::md, p == dropTarget ? colour::bg4 : colour::bg2);
                juce::Path frame, dashed;
                frame.addRoundedRectangle (r.reduced (0.5f), radius::md);
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, frame, dashes, 2);
                g.setColour (colour::borderStrong);
                g.fillPath (dashed);
                g.setColour (colour::textDisabled);
                g.fillRect (juce::Rectangle<float> (10.0f, 1.5f).withCentre (r.getCentre()));
                g.fillRect (juce::Rectangle<float> (1.5f, 10.0f).withCentre (r.getCentre()));
            }
            // the typing key that plays it, top left
            g.setColour (has ? colour::onAccent.withAlpha (0.75f) : colour::textSecondary);
            g.setFont (font (type::caption, true));
            g.drawText (juce::String::charToString ((juce::juce_wchar) simplepads::typingKey (p)), r.reduced (6.0f, 3.0f).toNearestInt(),
                        juce::Justification::topLeft, false);
            if (p == selected)
            {
                g.setColour (colour::accent);
                g.drawRoundedRectangle (r.reduced (1.0f), radius::md, 2.0f);
            }
            if (p == dropTarget)
            {
                g.setColour (colour::textPrimary);
                g.drawRoundedRectangle (r.reduced (2.5f), radius::md, 1.0f);
            }
        }

        // the selected pad: its title and its waveform with the played region
        const auto* c = channelOf (selected);
        const bool has = c != nullptr && ! c->samplePath.empty();
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        const auto title = tr ("Pad {0}", { juce::String (selected + 1) })
                           + (has ? juce::String::fromUTF8 (" \xc2\xb7 ") + utf8 (c->name) : juce::String())
                           + juce::String::fromUTF8 ("  \xc2\xb7  ") + tr ("key {0}", { juce::String::charToString ((juce::juce_wchar) simplepads::typingKey (selected)) });
        g.drawFittedText (title, titleArea, juce::Justification::centredLeft, 1, 0.8f);
        const auto w = waveArea.toFloat();
        fillRoundRect (g, w, radius::md, colour::bg0);
        if (! has)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small));
            g.drawFittedText (tr ("Drop a sound on a pad, import a file or browse the sounds. Click a pad or press its key to play it."),
                              waveArea.reduced (10, 4), juce::Justification::centred, 2, 0.9f);
            return;
        }
        drawWave (g, *c, w);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (pad, 6);
        const auto side = juce::jlimit (34, 64, (r.getHeight() - 3 * 4) / 4);
        gridArea = r.removeFromLeft (side * 4 + 3 * 4).withHeight (side * 4 + 3 * 4);
        r.removeFromLeft (14);
        auto top = r.removeFromTop (26);
        kit.setBounds (top.removeFromRight (64));
        top.removeFromRight (6);
        browse.setBounds (top.removeFromRight (124));
        top.removeFromRight (6);
        load.setBounds (top.removeFromRight (84));
        titleArea = top;
        r.removeFromTop (4);
        auto controls = r.removeFromBottom (juce::jmin (84, r.getHeight() / 2 + 8));
        waveArea = r.reduced (0, 2);
        auto row = controls.removeFromTop (24);
        for (auto& b : modes)
            b.setBounds (row.removeFromLeft (84)), row.removeFromLeft (2);
        row.removeFromLeft (10);
        reverse.setBounds (row.removeFromLeft (84));
        row.removeFromLeft (6);
        normalize.setBounds (row.removeFromLeft (92));
        row.removeFromLeft (6);
        group.setBounds (row.removeFromLeft (110));
        controls.removeFromTop (4);
        const auto kw = juce::jmin (78, controls.getWidth() / knobCount);
        for (int k = 0; k < knobCount; ++k)
        {
            auto cell = controls.removeFromLeft (kw);
            captions[(std::size_t) k].setBounds (cell.removeFromTop (13));
            knobs[(std::size_t) k].setBounds (cell);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragFrom = -1;
        regionDrag = 0;
        if (const auto p = padAt (e.getPosition()); p >= 0)
        {
            if (e.mods.isPopupMenu())
            {
                select (p);
                padMenu (p).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }
            dragFrom = p;
            // a click plays the pad while the button is held (a Gate pad stops when it is let go)
            select (p);
            lightPad (p);
            if (const auto ch = channelIndex (p); ch >= 0 && hasSound (p))
            {
                engine.sendLiveNote (ch, simplepads::padKey, 0.8f, true, true);
                heldChannel = ch;
            }
            return;
        }
        if (waveArea.contains (e.getPosition()))
        {
            const auto* c = channelOf (selected);
            if (c == nullptr || c->samplePath.empty())
                return;
            const auto x = (float) (e.x - waveArea.getX()) / (float) juce::jmax (1, waveArea.getWidth());
            regionDrag = std::abs (x - c->sampler.regionStart) <= std::abs (x - c->sampler.regionEnd) ? 1 : 2;
            mouseDrag (e);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (regionDrag != 0)
        {
            const auto x = juce::jlimit (0.0f, 1.0f, (float) (e.x - waveArea.getX()) / (float) juce::jmax (1, waveArea.getWidth()));
            const bool start = regionDrag == 1;
            edit ([x, start] (ChannelDef& c) {
                if (start)
                    c.sampler.regionStart = juce::jmin (x, c.sampler.regionEnd - 0.01f);
                else
                    c.sampler.regionEnd = juce::jmax (x, c.sampler.regionStart + 0.01f);
            });
            return;
        }
        if (dragFrom >= 0 && e.getDistanceFromDragStart() > 6)
        {
            const auto over = padAt (e.getPosition());
            if (over != dropTarget)
            {
                dropTarget = over == dragFrom ? -1 : over;
                repaint();
            }
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (heldChannel >= 0)
        {
            engine.sendLiveNote (heldChannel, simplepads::padKey, 0.0f, false, true);
            heldChannel = -1;
        }
        if (dragFrom >= 0 && dropTarget >= 0)
            dropPad (dragFrom, dropTarget, e.mods.isAltDown());
        dragFrom = dropTarget = -1;
        regionDrag = 0;
        repaint();
    }

    // files from the system and sounds from LYNX's browser land on the pad under the pointer
    bool isInterestedInFileDrag (const juce::StringArray& files) override { return track >= 0 && ! files.isEmpty(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override { setDropTarget (padAt ({ x, y })); }
    void fileDragExit (const juce::StringArray&) override { setDropTarget (-1); }
    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        auto at = padAt ({ x, y });
        setDropTarget (-1);
        if (at < 0)
            at = selected;
        // several files fill the pads from there on
        for (const auto& f : files)
            if (SampleLibrary::isSampleFile (juce::File (f)) && at < simplepads::padCount)
                loadFile (at++, juce::File (f));
    }
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return track >= 0 && BrowserComponent::isBrowserDrag (d) && SampleLibrary::isSampleFile (BrowserComponent::draggedFile (d));
    }
    void itemDragMove (const SourceDetails& d) override { setDropTarget (padAt (d.localPosition)); }
    void itemDragExit (const SourceDetails&) override { setDropTarget (-1); }
    void itemDropped (const SourceDetails& d) override
    {
        const auto at = padAt (d.localPosition);
        setDropTarget (-1);
        loadFile (at >= 0 ? at : selected, BrowserComponent::draggedFile (d));
    }

    // a short light on a pad played by a key or a click (a timer runs only while a pad is lit)
    void timerCallback() override
    {
        bool any = false;
        for (auto& f : flash)
            if (f > 0)
                --f, any = true;
        repaint();
        if (! any)
            stopTimer();
    }
    void lightPad (int pad)
    {
        if (pad >= 0 && pad < simplepads::padCount)
        {
            flash[(std::size_t) pad] = 5;
            repaint();
            if (! isTimerRunning())
                startTimerHz (30);
        }
    }

private:
    static constexpr int knobCount = 6;

    const ChannelDef* channelOf (int p) const
    {
        const auto& song = engine.getSong();
        const auto i = p >= 0 && p < (int) channels.size() ? channels[(std::size_t) p] : -1;
        return i >= 0 && i < (int) song.channels.size() ? &song.channels[(std::size_t) i] : nullptr;
    }
    int channelIndex (int p) const { return p >= 0 && p < (int) channels.size() ? channels[(std::size_t) p] : -1; }
    bool hasSound (int p) const
    {
        const auto* c = channelOf (p);
        return c != nullptr && ! c->samplePath.empty();
    }

    // the grid: pad 0 bottom left, pad 15 top right (the keys' shape)
    juce::Rectangle<int> padBounds (int p) const
    {
        const auto side = (gridArea.getWidth() - 3 * 4) / 4;
        const auto col = p % 4, row = 3 - p / 4;
        return { gridArea.getX() + col * (side + 4), gridArea.getY() + row * (side + 4), side, side };
    }
    int padAt (juce::Point<int> pos) const
    {
        for (int p = 0; p < simplepads::padCount; ++p)
            if (padBounds (p).contains (pos))
                return p;
        return -1;
    }
    void setDropTarget (int p)
    {
        if (p != dropTarget)
        {
            dropTarget = p;
            repaint();
        }
    }

    std::int64_t framesOf (const ChannelDef& c)
    {
        if (c.samplePath.empty())
            return 0;
        const auto data = engine.getSampleLibrary().load (utf8 (c.samplePath));
        return data != nullptr ? (std::int64_t) data->frames() : 0;
    }

    // one undoable edit of the selected pad's channel; the pad's own settings are applied to the channel after it
    void edit (const std::function<void (ChannelDef&)>& change, int pad = -1)
    {
        const auto p = pad >= 0 ? pad : selected;
        const auto ch = channelIndex (p);
        if (ch < 0)
            return;
        const auto frames = framesOf (engine.getSong().channels[(std::size_t) ch]);
        engine.editSong ([ch, change, frames] (Song& s) {
            if (ch < (int) s.channels.size())
            {
                change (s.channels[(std::size_t) ch]);
                simplepads::apply (s.channels[(std::size_t) ch], frames);
            }
        });
        update();
    }

    void turn (int k, float v)
    {
        switch (k)
        {
            case 0: edit ([s = attackFrom (v)] (ChannelDef& c) { c.sampler.attack = s; }); break;
            case 1: edit ([s = releaseFrom (v)] (ChannelDef& c) { c.simplePad.release = s; }); break;
            case 2: edit ([t = v * 2.0f - 1.0f] (ChannelDef& c) { c.simplePad.tone = std::abs (t) < 0.02f ? 0.0f : t; }); break;
            case 3: edit ([p = (v * 2.0f - 1.0f) * simplepads::maxPitch] (ChannelDef& c) { c.sampler.pitch = p; }); break;
            case 4: edit ([p = v * 2.0f - 1.0f] (ChannelDef& c) { c.pan = std::abs (p) < 0.01f ? 0.0f : p; }); break;
            default: edit ([v] (ChannelDef& c) { c.volume = v; }); break;
        }
    }

    // the same value on every pad (the knob's menu): what the original's "copy value to other pads" does
    void copyKnobToAll (int k)
    {
        const auto* src = channelOf (selected);
        if (src == nullptr)
            return;
        const auto from = *src;
        const auto chans = channels;
        std::vector<std::int64_t> frames;
        for (auto ch : chans)
            frames.push_back (ch >= 0 ? framesOf (engine.getSong().channels[(std::size_t) ch]) : 0);
        engine.editSong ([k, from, chans, frames] (Song& s) {
            for (std::size_t i = 0; i < chans.size(); ++i)
            {
                if (chans[i] < 0 || chans[i] >= (int) s.channels.size())
                    continue;
                auto& c = s.channels[(std::size_t) chans[i]];
                switch (k)
                {
                    case 0: c.sampler.attack = from.sampler.attack; break;
                    case 1: c.simplePad.release = from.simplePad.release; break;
                    case 2: c.simplePad.tone = from.simplePad.tone; break;
                    case 3: c.sampler.pitch = from.sampler.pitch; break;
                    case 4: c.pan = from.pan; break;
                    default: c.volume = from.volume; break;
                }
                simplepads::apply (c, frames[i]);
            }
        });
        update();
    }

    void loadFile (int p, const juce::File& file)
    {
        if (p < 0 || ! file.existsAsFile())
            return;
        const auto data = engine.getSampleLibrary().load (file.getFullPathName());
        if (data == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"),
                                                    tr ("Could not read {0}", { file.getFileName() }));
            return;
        }
        const auto t = track;
        const auto path = file.getFullPathName().toStdString(), name = file.getFileNameWithoutExtension().toStdString();
        const auto frames = (std::int64_t) data->frames();
        const auto seconds = data->sampleRate > 0.0 ? (double) frames / data->sampleRate : 0.0;
        engine.editSong ([t, p, path, name, frames, seconds] (Song& s) { simplepads::loadSample (s, t, p, path, name, frames, seconds); });
        folder = file.getParentDirectory();
        select (p);
    }

    void chooseFile (int p)
    {
        chooser = ui::makeFileChooser (tr ("Choose sample"), folder, engine.getSampleLibrary().wildcard());
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<PadsPage> (this), p] (const juce::FileChooser& fc) {
                               if (safe != nullptr)
                                   safe->loadFile (p, fc.getResult());
                           });
    }

    void dropPad (int from, int to, bool copy)
    {
        if (from < 0 || to < 0 || from == to || ! hasSound (from))
            return;
        const auto t = track;
        const auto ch = channelIndex (from);
        const auto frames = ch >= 0 ? framesOf (engine.getSong().channels[(std::size_t) ch]) : 0;
        // onto an empty pad it moves, onto a pad with a sound the two swap; with Alt it is copied
        engine.editSong ([t, from, to, copy, frames] (Song& s) {
            if (copy)
                simplepads::copyPad (s, t, from, to, frames);
            else
                simplepads::swapPads (s, t, from, to);
        });
        select (to);
    }

    void normalizePad()
    {
        const auto* c = channelOf (selected);
        if (c == nullptr || c->samplePath.empty())
            return;
        const auto data = engine.getSampleLibrary().load (utf8 (c->samplePath));
        if (data == nullptr || data->frames() == 0)
            return;
        const auto n = (std::int64_t) data->frames();
        const auto a = (std::int64_t) (c->sampler.regionStart * (float) n), b = juce::jmax (a + 1, (std::int64_t) (c->sampler.regionEnd * (float) n));
        float peak = 0.0f;
        for (auto i = a; i < b && i < n; ++i)
            peak = juce::jmax (peak, std::abs (data->leftData()[i]), std::abs (data->rightData()[i]));
        edit ([g = simplepads::normalizeGainDb (peak)] (ChannelDef& ch) { ch.sampler.gainDb = g; });
    }

    void showGroupMenu()
    {
        juce::PopupMenu m;
        const auto* c = channelOf (selected);
        const auto now = c != nullptr ? c->simplePad.group : 0;
        m.addItem (tr ("None"), true, now == 0, [this] { edit ([] (ChannelDef& ch) { ch.simplePad.group = 0; }); });
        for (int g = 1; g <= simplepads::groupCount; ++g)
            m.addItem (tr ("Group {0}", { juce::String (g) }), true, now == g, [this, g] { edit ([g] (ChannelDef& ch) { ch.simplePad.group = g; }); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&group));
    }

    juce::PopupMenu padMenu (int p)
    {
        juce::PopupMenu m;
        const bool has = hasSound (p);
        m.addItem (tr ("Import..."), [this, p] { chooseFile (p); });
        m.addSeparator();
        m.addItem (tr ("Copy"), has, false, [this, p] { clipboard = p; });
        m.addItem (tr ("Paste"), clipboard >= 0 && clipboard != p && hasSound (clipboard), false, [this, p] { dropPad (clipboard, p, true); });
        m.addItem (tr ("Clear the pad"), has, false, [this, p] {
            const auto t = track;
            engine.editSong ([t, p] (Song& s) { simplepads::clearPad (s, t, p); });
            update();
        });
        juce::PopupMenu colours;
        const auto* c = channelOf (p);
        const auto now = c != nullptr ? c->simplePad.colour : 0;
        for (int i = 0; i <= simplepads::colourCount; ++i)
            colours.addItem (padColourName (i), true, now == i, [this, p, i] { edit ([i] (ChannelDef& ch) { ch.simplePad.colour = i; }, p); });
        m.addSubMenu (tr ("Colour"), colours);
        juce::PopupMenu copyTo;
        for (int k = 0; k < knobCount; ++k)
            copyTo.addItem (padKnobName (k), has, false, [this, p, k] {
                select (p);
                copyKnobToAll (k);
            });
        m.addSubMenu (tr ("Copy a value to every pad"), copyTo, has);
        return m;
    }

    // kits: the pads' sounds and settings in a file of the user's (Documents/LYNX/Sampler kits/<name>.lynxkit)
    static juce::File kitFolder() { return platform::documentsFolder().getChildFile ("LYNX/Sampler kits"); }

    juce::PopupMenu kitMenu()
    {
        juce::PopupMenu m;
        m.addItem (tr ("Save the pads as a kit..."), track >= 0, false, [this] { askKitName(); });
        juce::PopupMenu mine, rename, remove;
        const auto files = kitFolder().findChildFiles (juce::File::findFiles, false, "*.lynxkit");
        auto sorted = files;
        std::sort (sorted.begin(), sorted.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        for (const auto& f : sorted)
        {
            mine.addItem (f.getFileNameWithoutExtension(), [this, f] { openKit (f); });
            rename.addItem (f.getFileNameWithoutExtension(), [this, f] { askRename (f); });
            remove.addItem (f.getFileNameWithoutExtension(), [f] { f.moveToTrash(); });
        }
        m.addSubMenu (tr ("My kits"), mine, ! sorted.isEmpty());
        m.addItem (tr ("Fill the pads from a folder..."), track >= 0, false, [this] { chooseKitFolder(); });
        m.addSeparator();
        m.addSubMenu (tr ("Rename a kit"), rename, ! sorted.isEmpty());
        m.addSubMenu (tr ("Delete a kit"), remove, ! sorted.isEmpty());
        return m;
    }
    void showKitMenu() { kitMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&kit)); }

    void askKitName()
    {
        auto* w = new juce::AlertWindow (tr ("Save the pads as a kit"), tr ("Name of the kit:"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", utf8 (track >= 0 && track < (int) engine.getSong().playlistTracks.size() ? engine.getSong().playlistTracks[(std::size_t) track].name : std::string()));
        w->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, safe = juce::Component::SafePointer<PadsPage> (this)] (int r) {
                                if (r == 1 && safe != nullptr)
                                    saveKit (w->getTextEditorContents ("name").trim());
                            }),
                            true);
    }

    void askRename (const juce::File& kitFile)
    {
        auto* w = new juce::AlertWindow (tr ("Rename the kit"), tr ("New name of the kit:"), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", kitFile.getFileNameWithoutExtension());
        w->addButton (tr ("Rename"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, kitFile] (int r) {
                                if (r == 1)
                                    renameKit (kitFile, w->getTextEditorContents ("name"));
                            }),
                            true);
    }

public:
    // a kit file gets another name (2 to 50 characters, not taken); false when it can't
    static bool renameKit (const juce::File& kitFile, const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName).trim();
        const auto target = kitFile.getSiblingFile (name + ".lynxkit");
        if (name.length() < 2 || name.length() > 50 || target.exists() || ! kitFile.existsAsFile())
            return false;
        auto json = juce::JSON::parse (kitFile.loadFileAsString());
        if (auto* o = json.getDynamicObject())
            o->setProperty ("name", name);
        return target.replaceWithText (juce::JSON::toString (json)) && kitFile.deleteFile();
    }

    bool saveKit (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName).trim();
        if (name.length() < 2 || name.length() > 50) // (the original's rule for a kit's name)
            return false;
        auto pads = juce::Array<juce::var>();
        for (int p = 0; p < simplepads::padCount; ++p)
        {
            const auto* c = channelOf (p);
            if (c == nullptr || c->samplePath.empty())
                continue;
            auto o = std::make_unique<juce::DynamicObject>();
            o->setProperty ("pad", p);
            o->setProperty ("file", utf8 (c->samplePath));
            o->setProperty ("name", utf8 (c->name));
            o->setProperty ("mode", (int) c->simplePad.mode);
            o->setProperty ("tone", c->simplePad.tone);
            o->setProperty ("release", c->simplePad.release);
            o->setProperty ("group", c->simplePad.group);
            o->setProperty ("colour", c->simplePad.colour);
            o->setProperty ("attack", c->sampler.attack);
            o->setProperty ("pitch", c->sampler.pitch);
            o->setProperty ("start", c->sampler.regionStart);
            o->setProperty ("end", c->sampler.regionEnd);
            o->setProperty ("reverse", c->sampler.reverse);
            o->setProperty ("gainDb", c->sampler.gainDb);
            o->setProperty ("volume", c->volume);
            o->setProperty ("pan", c->pan);
            pads.add (juce::var (o.release()));
        }
        auto root = std::make_unique<juce::DynamicObject>();
        root->setProperty ("lynxSamplerKit", 1);
        root->setProperty ("name", name);
        root->setProperty ("pads", pads);
        kitFolder().createDirectory();
        return kitFolder().getChildFile (name + ".lynxkit").replaceWithText (juce::JSON::toString (juce::var (root.release())));
    }

    bool openKit (const juce::File& file)
    {
        const auto json = juce::JSON::parse (file.loadFileAsString());
        if ((int) json.getProperty ("lynxSamplerKit", 0) != 1)
            return false;
        struct Loaded
        {
            int pad;
            ChannelDef def;
            std::int64_t frames;
        };
        std::vector<Loaded> loaded;
        if (const auto* list = json.getProperty ("pads", {}).getArray())
            for (const auto& v : *list)
            {
                const auto p = (int) v.getProperty ("pad", -1);
                const juce::File f (v.getProperty ("file", "").toString());
                const auto data = f.existsAsFile() ? engine.getSampleLibrary().load (f.getFullPathName()) : nullptr;
                if (p < 0 || p >= simplepads::padCount || data == nullptr)
                    continue;
                ChannelDef d;
                d.samplePath = f.getFullPathName().toStdString();
                d.name = v.getProperty ("name", f.getFileNameWithoutExtension()).toString().toStdString();
                d.simplePad.mode = (std::uint8_t) juce::jlimit (0, 2, (int) v.getProperty ("mode", 0));
                d.simplePad.tone = (float) v.getProperty ("tone", 0.0);
                d.simplePad.release = (float) v.getProperty ("release", 0.0);
                d.simplePad.group = (int) v.getProperty ("group", 0);
                d.simplePad.colour = (int) v.getProperty ("colour", 0);
                d.sampler.attack = (float) v.getProperty ("attack", 0.0);
                d.sampler.pitch = (float) v.getProperty ("pitch", 0.0);
                d.sampler.regionStart = (float) v.getProperty ("start", 0.0);
                d.sampler.regionEnd = (float) v.getProperty ("end", 1.0);
                d.sampler.reverse = (bool) v.getProperty ("reverse", false);
                d.sampler.gainDb = (float) v.getProperty ("gainDb", 0.0);
                d.volume = (float) v.getProperty ("volume", 0.78);
                d.pan = (float) v.getProperty ("pan", 0.0);
                loaded.push_back ({ p, std::move (d), (std::int64_t) data->frames() });
            }
        const auto t = track;
        engine.editSong ([t, loaded] (Song& s) {
            for (int p = 0; p < simplepads::padCount; ++p)
                simplepads::clearPad (s, t, p);
            for (const auto& l : loaded)
            {
                const auto ch = simplepads::padChannel (s, t, l.pad);
                if (ch < 0)
                    continue;
                auto& c = s.channels[(std::size_t) ch];
                c.samplePath = l.def.samplePath;
                c.name = l.def.name;
                c.sampler.attack = l.def.sampler.attack;
                c.sampler.pitch = l.def.sampler.pitch;
                c.sampler.regionStart = l.def.sampler.regionStart;
                c.sampler.regionEnd = l.def.sampler.regionEnd;
                c.sampler.reverse = l.def.sampler.reverse;
                c.sampler.gainDb = l.def.sampler.gainDb;
                c.volume = l.def.volume;
                c.pan = l.def.pan;
                const auto index = c.simplePad.index;
                c.simplePad = l.def.simplePad;
                c.simplePad.index = index;
                simplepads::apply (c, l.frames);
            }
        });
        update();
        return true;
    }

    // the first 16 sounds of a folder (by name), pad by pad
    int fillFromFolder (const juce::File& dir)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false);
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        int p = 0;
        for (const auto& f : files)
            if (p < simplepads::padCount && SampleLibrary::isSampleFile (f))
                loadFile (p++, f);
        select (0);
        return p;
    }

private:
    void chooseKitFolder()
    {
        chooser = ui::makeFileChooser (tr ("Choose a folder of sounds"), folder, {});
        ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                           [safe = juce::Component::SafePointer<PadsPage> (this)] (const juce::FileChooser& fc) {
                               if (safe != nullptr && fc.getResult().isDirectory())
                                   safe->fillFromFolder (fc.getResult());
                           });
    }

    void drawWave (juce::Graphics& g, const ChannelDef& c, juce::Rectangle<float> w)
    {
        const auto width = juce::jmax (1, (int) w.getWidth());
        if (waveFor != c.samplePath || (int) wave.size() != width)
        {
            wave.assign ((std::size_t) width, 0.0f);
            if (const auto data = engine.getSampleLibrary().load (utf8 (c.samplePath)); data != nullptr && data->frames() > 0)
            {
                const auto n = (std::int64_t) data->frames();
                for (int x = 0; x < width; ++x)
                {
                    const auto a = n * x / width, b = juce::jmax (a + 1, n * (x + 1) / width);
                    float peak = 0.0f;
                    for (auto i = a; i < b; i += juce::jmax<std::int64_t> (1, (b - a) / 64))
                        peak = juce::jmax (peak, std::abs (data->leftData()[i]), std::abs (data->rightData()[i]));
                    wave[(std::size_t) x] = peak;
                }
            }
            waveFor = c.samplePath;
        }
        const auto mid = w.getCentreY(), half = w.getHeight() * 0.45f;
        const auto s = w.getX() + c.sampler.regionStart * w.getWidth(), e = w.getX() + c.sampler.regionEnd * w.getWidth();
        for (int x = 0; x < width; ++x)
        {
            const auto px = w.getX() + (float) x;
            const auto inside = px >= s && px <= e;
            const auto idx = c.sampler.reverse ? width - 1 - x : x;
            const auto h = juce::jmax (0.5f, wave[(std::size_t) idx] * half);
            g.setColour (inside ? colour::accent.withAlpha (0.9f) : colour::textDisabled.withAlpha (0.6f));
            g.fillRect (px, mid - h, 1.0f, h * 2.0f);
        }
        // the region's handles
        g.setColour (colour::textPrimary);
        for (auto x : { s, e })
        {
            g.fillRect (x - 1.0f, w.getY(), 2.0f, w.getHeight());
            g.fillRect (x - 4.0f, w.getY(), 8.0f, 4.0f);
        }
        if (simplepads::modeOf (c) == simplepads::Mode::loop)
        {
            g.setFont (font (type::caption, true));
            g.setColour (colour::textSecondary);
            g.drawText (juce::String::fromUTF8 ("\xe2\x9f\xb3 ") + trc ("@pad mode: Loop"), w.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::topRight, false);
        }
    }

    AudioEngine& engine;
    int track = -1, selected = 0, dragFrom = -1, dropTarget = -1, regionDrag = 0, clipboard = -1, heldChannel = -1;
    std::vector<int> channels;
    std::array<int, simplepads::padCount> flash {};
    juce::Rectangle<int> gridArea, titleArea, waveArea;
    juce::TextButton load, browse, kit, normalize, group;
    juce::ToggleButton reverse;
    std::array<juce::TextButton, 3> modes;
    std::array<DockKnob, knobCount> knobs;
    std::array<juce::Label, knobCount> captions;
    std::vector<float> wave;
    std::string waveFor;
    juce::File folder;
    std::unique_ptr<juce::FileChooser> chooser;
};

SimpleDock::SimpleDock (AudioEngine& e, Hooks h) : engine (e), hooks (std::move (h))
{
    // the Instrument tab's "Keyboard" button shows the dock's own Keyboard tab (round 190ds)
    hooks.showKeys = [this] { showKeyboard(); };
    effectsPage = std::make_unique<EffectsPage> (engine);
    instrumentPage = std::make_unique<InstrumentPage> (engine, hooks);
    editorPage = std::make_unique<EditorPage> (engine, hooks);
    padsPage = std::make_unique<PadsPage> (engine);
    padsPage->onBrowse = [this] {
        if (onBrowseSounds)
            onBrowseSounds();
    };
    addChildComponent (*padsPage);
    addChildComponent (*effectsPage);
    addChildComponent (*instrumentPage);
    addChildComponent (*editorPage);

    instrumentButton.setButtonText (tr ("Instrument"));
    keyboardButton.setButtonText (tr ("Keyboard"));
    keyboardButton.setTooltip (tr ("Play the track: piano, Scale keys or Chord pad"));
    editorButton.setButtonText (tr ("Editor"));
    editorButton.setTooltip (tr ("Write the track's notes"));
    effectsButton.setButtonText (tr ("Effects"));
    padsButton.setButtonText (tr ("Pads"));
    padsButton.setTooltip (tr ("The Sampler track's 16 pads: their sounds and how they play"));
    padsButton.setComponentID ("simple.dock.pads");
    // round 190dv: what the first-song tour's ring finds
    keyboardButton.setComponentID ("simple.dock.keyboard");
    editorButton.setComponentID ("simple.dock.editor");
    effectsButton.setComponentID ("simple.dock.effects");
    const std::pair<juce::TextButton*, Tab> tabs[] { { &instrumentButton, Tab::instrument }, { &keyboardButton, Tab::keyboard },
                                                     { &editorButton, Tab::editor }, { &padsButton, Tab::pads }, { &effectsButton, Tab::effects } };
    for (auto [b, t] : tabs)
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1901);
        addAndMakeVisible (*b);
        b->onClick = [this, b = b, t = t] {
            if (b->getToggleState())
            {
                tabChosen = true;
                setTab (t);
            }
        };
    }
    followButton.setButtonText (tr ("Follow the project key"));
    followButton.setTooltip (tr ("The loop is moved to the project key (its length stays); off: it plays in its own key"));
    followButton.onClick = [this] { clickFollowForCheck(); };
    addChildComponent (followButton);
    hideButton.setButtonText (glyph ("\xe2\x96\xbe"));
    hideButton.setTooltip (tr ("Hide the track dock"));
    hideButton.onClick = [this] {
        shownSetting = false;
        if (hooks.layoutChanged)
            hooks.layoutChanged();
    };
    addAndMakeVisible (hideButton);
    showTrack (-1);
}

SimpleDock::~SimpleDock() = default;

void SimpleDock::showTrack (int newTrack)
{
    if (newTrack != track)
        tabChosen = false;
    track = newTrack;
    refresh();
}

void SimpleDock::refresh()
{
    const auto& song = engine.getSong();
    insert = channel = -1;
    instrumentTab = false;
    playTabs = false;
    padsTab = track >= 0 && simpletracks::kindOf (song, track) == simpletracks::Kind::pads;
    if (track >= 0)
    {
        const auto chain = detailchain::forTrack (song, track);
        insert = chain.insert;
        channel = chain.channel;
        // a sound can be picked for a track that plays one instrument (not a sample, an audio clip or a drum kit)
        if (channel >= 0 && channel < (int) song.channels.size())
        {
            const auto kind = song.channels[(std::size_t) channel].instrument;
            instrumentTab = kind != InstrumentKind::audioClip && kind != InstrumentKind::sampler
                            && simpletracks::kindOf (song, track).value_or (simpletracks::Kind::instrument) == simpletracks::Kind::instrument;
            // the track plays notes: it can be played and written (round 190ds)
            const auto trackKind = simpletracks::kindOf (song, track);
            playTabs = trackKind == simpletracks::Kind::instrument || trackKind == simpletracks::Kind::sampler;
        }
    }
    instrumentButton.setVisible (instrumentTab);
    keyboardButton.setVisible (playTabs);
    editorButton.setVisible (playTabs);
    padsButton.setVisible (padsTab);
    if ((tab == Tab::instrument && ! instrumentTab) || ((tab == Tab::keyboard || tab == Tab::editor) && ! playTabs) || (tab == Tab::pads && ! padsTab))
        tab = Tab::effects;
    if (! tabChosen && (instrumentTab || playTabs))
        tab = instrumentTab ? Tab::instrument : Tab::keyboard; // (an instrument track opens on its sound; an audio track on its effects)
    else if (! tabChosen && padsTab)
        tab = Tab::pads; // (round 190fo: a Sampler track opens on its pads)
    else if (! tabChosen)
        tab = Tab::effects;
    effectsPage->show (insert);
    instrumentPage->show (instrumentTab ? channel : -1);
    showPage();
    editorPage->show (playTabs ? track : -1, channel);
    padsPage->show (padsTab ? track : -1);
    updateFollow();
    resized();
    repaint();
}

void SimpleDock::setTab (Tab t)
{
    tabChosen = true; // (a refresh keeps a tab that was asked for)
    if ((t == Tab::instrument && ! instrumentTab) || ((t == Tab::keyboard || t == Tab::editor) && ! playTabs) || (t == Tab::pads && ! padsTab))
        t = Tab::effects;
    tab = t;
    showPage();
}

void SimpleDock::cycleTab()
{
    tabChosen = true;
    std::vector<Tab> shown;
    if (instrumentTab)
        shown.push_back (Tab::instrument);
    if (playTabs)
        shown.push_back (Tab::keyboard), shown.push_back (Tab::editor);
    if (padsTab)
        shown.push_back (Tab::pads);
    shown.push_back (Tab::effects);
    const auto at = std::find (shown.begin(), shown.end(), tab);
    setTab (at == shown.end() || at + 1 == shown.end() ? shown.front() : *(at + 1));
}

void SimpleDock::showPage()
{
    auto& button = tab == Tab::instrument ? instrumentButton : tab == Tab::keyboard ? keyboardButton : tab == Tab::editor ? editorButton
                 : tab == Tab::pads                                         ? padsButton
                                                                            : effectsButton;
    button.setToggleState (true, juce::dontSendNotification);
    if (tab == Tab::keyboard && keyboardPage == nullptr)
    {
        keyboardPage = makeKeyboardPanel (engine, [this] { return channel; });
        addChildComponent (*keyboardPage);
        resized();
    }
    if (tab == Tab::keyboard && channel >= 0 && engine.getSelectedChannel() != channel)
        engine.setSelectedChannel (channel); // (the typing keys play the selected channel: this track)
    instrumentPage->setVisible (tab == Tab::instrument);
    effectsPage->setVisible (tab == Tab::effects);
    editorPage->setVisible (tab == Tab::editor);
    padsPage->setVisible (tab == Tab::pads);
    if (keyboardPage != nullptr)
        keyboardPage->setVisible (tab == Tab::keyboard);
}

void SimpleDock::showKeyboard()
{
    tabChosen = true;
    setTab (Tab::keyboard);
}

void SimpleDock::editorTakenBack() { editorPage->takenBackByPanel(); }
void SimpleDock::padsLoadForCheck (int pad, const juce::File& file) { padsPage->loadFileForCheck (pad, file); }
void SimpleDock::padsSelectForCheck (int pad) { padsPage->select (pad); }
void SimpleDock::padsModeForCheck (int mode) { padsPage->setModeForCheck (mode); }
void SimpleDock::padsKnobForCheck (int knob, float v) { padsPage->turnForCheck (knob, v); }
void SimpleDock::padsRegionForCheck (float start, float end) { padsPage->setRegionForCheck (start, end); }
void SimpleDock::padsNormalizeForCheck() { padsPage->normalizeForCheck(); }
void SimpleDock::padsHitForCheck (int pad) { padsPage->hit (pad); }
void SimpleDock::padsDragForCheck (int from, int to, bool copy) { padsPage->dragPadForCheck (from, to, copy); }
void SimpleDock::padsUpdateForCheck() { padsPage->update(); }
juce::PopupMenu SimpleDock::padMenuForSnapshot (int pad) { return padsPage->padMenuForSnapshot (pad); }
juce::PopupMenu SimpleDock::kitMenuForSnapshot() { return padsPage->kitMenuForSnapshot(); }
bool SimpleDock::saveKitForCheck (const juce::String& name) { return padsPage->saveKit (name); }
bool SimpleDock::openKitForCheck (const juce::File& file) { return padsPage->openKit (file); }
bool SimpleDock::renameKitForCheck (const juce::File& file, const juce::String& name) { return PadsPage::renameKit (file, name); }
bool SimpleDock::recordHintShown() const
{
    const auto& song = engine.getSong();
    if (track < 0 || insert <= 0 || insert >= (int) song.mixer.size() || ! engine.isInsertArmed (insert) || song.mixer[(std::size_t) insert].inputChannel < 0)
        return false;
    const auto kind = simpletracks::kindOf (song, track);
    return kind && simpletracks::isAudioKind (*kind) && *kind != simpletracks::Kind::audio;
}

void SimpleDock::lightPad (int pad)
{
    if (padsTab && tab == Tab::pads)
        padsPage->lightPad (pad);
}
void SimpleDock::releaseEditor() { editorPage->release(); }
bool SimpleDock::isEditorDocked() const noexcept { return editorPage->docked(); }

std::vector<int> SimpleDock::trackAudioClips() const
{
    std::vector<int> out;
    const auto& song = engine.getSong();
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (const auto& c = song.playlist[(std::size_t) i]; c.kind == ClipKind::audio && c.track == track && track >= 0)
            out.push_back (i);
    return out;
}

// "Loop key: G minor · moved −5" and the switch, for an audio track whose loop has a known key (round 190ds)
void SimpleDock::updateFollow()
{
    const auto& song = engine.getSong();
    std::optional<keyfollow::LoopKey> key;
    bool all = true;
    int shift = 0;
    for (const auto i : trackAudioClips())
    {
        const auto& c = song.playlist[(std::size_t) i];
        if (const auto k = keyfollow::loopKeyOf (song, c.channel); k && ! key)
        {
            key = k;
            shift = keyfollow::shiftOf (song, c);
        }
        all = all && c.followKey;
    }
    const auto show = key.has_value() && simpletracks::kindOf (song, track) == simpletracks::Kind::audio;
    followButton.setVisible (show);
    followText.clear();
    if (show)
    {
        followButton.setToggleState (all, juce::dontSendNotification);
        const auto name = utf8 (projectkey::rootName (key->root)) + " " + (key->minor ? tr ("minor") : tr ("major"));
        followText = tr ("Loop key: {0}", { name }) + glyph ("  \xc2\xb7  ")
                     + (! projectkey::get (song) ? tr ("no project key") : ! all ? tr ("plays in its own key")
                        : shift == 0 ? tr ("already in key") : tr ("moved {0} semitones", { (shift > 0 ? "+" : "") + juce::String (shift) }));
    }
}

bool SimpleDock::followShownForCheck() const { return followButton.isVisible(); }

void SimpleDock::clickFollowForCheck()
{
    const auto clips = trackAudioClips();
    bool all = ! clips.empty();
    for (const auto i : clips)
        all = all && engine.getSong().playlist[(std::size_t) i].followKey;
    engine.editSong ([clips, on = ! all] (Song& s) { keyfollow::setFollow (s, clips, on); });
    updateFollow();
    repaint();
}

void SimpleDock::clickFoldForCheck (bool on) { editorPage->clickFold (on); }

juce::Component* SimpleDock::keyboardPanelForSnapshot()
{
    setTab (Tab::keyboard);
    return keyboardPage.get();
}

int SimpleDock::heightFor (int available) const noexcept
{
    return juce::jlimit (std::min (minHeight, available / 2), std::max (minHeight, available * 6 / 10), preferredHeight);
}

void SimpleDock::paint (juce::Graphics& g)
{
    g.fillAll (colour::bg1);
    auto strip = getLocalBounds().withHeight (stripHeight);
    g.setColour (colour::bg3);
    g.fillRect (strip);
    g.setColour (colour::border);
    g.fillRect (0, 0, getWidth(), 1);
    g.fillRect (0, stripHeight - 1, getWidth(), 1);
    // the resize grip in the middle of the top edge
    g.setColour (colour::borderStrong);
    g.fillRoundedRectangle ((float) getWidth() * 0.5f - 16.0f, 2.0f, 32.0f, 2.0f, 1.0f);

    const auto& song = engine.getSong();
    // round 190dg: the track's chain preset, right of the tabs (the header's FX button is too small to name it)
    const auto rightEdge = followButton.isVisible() ? followButton.getX() - 10 : hideButton.getX() - 10;
    if (followButton.isVisible()) // round 190ds: the loop's key and what following does
    {
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        const auto area = strip.withLeft (effectsButton.getRight() + 16).withRight (rightEdge);
        g.drawText (followText, area, juce::Justification::centredRight, true);
    }
    else if (recordHintShown())
    {
        // round 190fo: a track armed to record from an input (the original's mobile app warns the same way)
        const auto area = strip.withLeft (effectsButton.getRight() + 16).withRight (rightEdge);
        g.setColour (colour::warning);
        g.setFont (font (type::small));
        g.drawFittedText (tr ("Recording: wear headphones, so the microphone does not pick up the playback"), area, juce::Justification::centredRight, 1, 0.8f);
    }
    else if (insert > 0 && insert < (int) song.mixer.size() && ! song.mixer[(std::size_t) insert].chainPreset.empty())
    {
        auto name = utf8 (song.mixer[(std::size_t) insert].chainPreset);
        if (! chains::unchanged (song, insert))
            name << " " << tr ("(edited)");
        const auto area = strip.withLeft (effectsButton.getRight() + 16).withRight (rightEdge);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (tr ("Preset") + glyph (": ") + name, area, juce::Justification::centredRight, true);
    }
    auto text = strip.withTrimmedLeft (12).withRight (instrumentButton.isVisible() ? instrumentButton.getX() - 12
                                                      : keyboardButton.isVisible() ? keyboardButton.getX() - 12
                                                      : padsButton.isVisible()     ? padsButton.getX() - 12
                                                                                   : effectsButton.getX() - 12);
    if (track < 0)
    {
        g.setColour (colour::textSecondary);
        g.setFont (font (type::body));
        g.drawText (tr ("No track selected"), text, juce::Justification::centredLeft, true);
        return;
    }
    const auto own = track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track].colour : 0u;
    const auto kind = simpletracks::kindOf (song, track);
    g.setColour (own != 0 ? juce::Colour (own) : kind ? juce::Colour (simpletracks::defaultColour (*kind)) : colour::textSecondary);
    g.fillRoundedRectangle ((float) text.getX(), (float) strip.getCentreY() - 7.0f, 4.0f, 14.0f, 2.0f);
    text.removeFromLeft (12);
    g.setColour (colour::textPrimary);
    g.setFont (font (type::body, true));
    const auto title = trackTitle (song, track);
    const auto titleWidth = juce::jmin (text.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 4);
    g.drawText (title, text.removeFromLeft (titleWidth), juce::Justification::centredLeft, true);
    if (kind)
    {
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (glyph ("\xc2\xb7  ") + tr (simpletracks::defaultName (*kind)), text.withTrimmedLeft (6), juce::Justification::centredLeft, true);
    }
}

void SimpleDock::resized()
{
    auto strip = getLocalBounds().withHeight (stripHeight).reduced (0, 4);
    hideButton.setBounds (strip.removeFromRight (34).withTrimmedRight (8));
    followButton.setBounds (strip.removeFromRight (190).withTrimmedRight (10));
    // the tabs sit after the track's name, left of the middle
    const auto count = 1 + (instrumentTab ? 1 : 0) + (playTabs ? 2 : 0) + (padsTab ? 1 : 0);
    auto tabs = strip.withLeft (juce::jmin (300, getWidth() / 3)).withWidth (100 * count);
    std::vector<juce::TextButton*> shown;
    if (instrumentTab)
        shown.push_back (&instrumentButton);
    if (playTabs)
        shown.push_back (&keyboardButton), shown.push_back (&editorButton);
    if (padsTab)
        shown.push_back (&padsButton);
    shown.push_back (&effectsButton);
    for (std::size_t i = 0; i < shown.size(); ++i)
    {
        shown[i]->setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < shown.size() ? juce::Button::ConnectedOnRight : 0));
        shown[i]->setBounds (tabs.removeFromLeft (100));
    }
    const auto body = getLocalBounds().withTrimmedTop (stripHeight);
    effectsPage->setBounds (body);
    instrumentPage->setBounds (body);
    editorPage->setBounds (body);
    padsPage->setBounds (body);
    if (keyboardPage != nullptr)
        keyboardPage->setBounds (body);
}

void SimpleDock::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (e.y < edgeHeight ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void SimpleDock::mouseDown (const juce::MouseEvent& e)
{
    resizing = e.y < edgeHeight;
    dragStartHeight = getHeight();
}

void SimpleDock::mouseDrag (const juce::MouseEvent& e)
{
    if (! resizing)
        return;
    const auto wanted = juce::jmax (minHeight, dragStartHeight - e.getDistanceFromDragStartY());
    if (wanted != preferredHeight)
    {
        preferredHeight = wanted;
        if (auto* parent = getParentComponent())
            parent->resized();
    }
}

void SimpleDock::mouseUp (const juce::MouseEvent&)
{
    if (resizing && hooks.layoutChanged)
        hooks.layoutChanged(); // (saves the height)
    resizing = false;
}

// --- the user's own chain presets ------------------------------------------------------------------------------------
juce::File SimpleDock::userPresetFolder() { return platform::documentsFolder().getChildFile ("LYNX/Chain presets"); }

// round 190dg: the favourite chain presets, one name a line, in the order they were starred
juce::File SimpleDock::favouritesFile() { return userPresetFolder().getChildFile ("Favourites.txt"); }

juce::StringArray SimpleDock::favouritePresets()
{
    auto lines = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
    lines.trim();
    lines.removeEmptyStrings();
    lines.removeDuplicates (false);
    return lines;
}

bool SimpleDock::setFavouritePreset (const juce::String& name, bool on)
{
    auto lines = favouritePresets();
    if (on)
        lines.addIfNotAlreadyThere (name.trim());
    else
        lines.removeString (name.trim());
    favouritesFile().getParentDirectory().createDirectory();
    return favouritesFile().replaceWithText (lines.joinIntoString ("\n") + (lines.isEmpty() ? "" : "\n"));
}

bool SimpleDock::saveUserPreset (AudioEngine& engine, int insert, const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty() || insert < 0 || insert >= (int) engine.getSong().mixer.size())
        return false;
    engine.capturePluginStates(); // the plugins' current settings go into the preset
    const auto file = userPresetFolder().getChildFile (name + ProjectFile::mixerTrackStateExtension);
    file.getParentDirectory().createDirectory();
    const auto state = chains::stateOf (engine.getSong().mixer[(std::size_t) insert], name.toStdString());
    if (! file.replaceWithText (ProjectFile::mixerTrackStateToJson (state)))
        return false;
    engine.editSong ([insert, n = name.toStdString()] (Song& s) {
        if (insert < (int) s.mixer.size())
            s.mixer[(std::size_t) insert].chainPreset = n;
    });
    return true;
}

bool SimpleDock::loadUserPreset (AudioEngine& engine, int insert, const juce::File& file)
{
    juce::String error;
    auto state = ProjectFile::mixerTrackStateFromJson (file.loadFileAsString(), error);
    if (! state || insert < 0 || insert >= (int) engine.getSong().mixer.size())
        return false;
    const auto name = file.getFileNameWithoutExtension().toStdString();
    engine.editSongStructural ([insert, &state, &name] (Song& s) { chains::applyChain (s, insert, state->effects, state->chainBlend, name); });
    return true;
}

// --- tests and --simplefxshot ----------------------------------------------------------------------------------------
int SimpleDock::cardCountForCheck() const { return effectsPage->cardCount(); }
int SimpleDock::oneKnobCardsForCheck() const { return effectsPage->oneKnobCards(); }
juce::PopupMenu SimpleDock::presetMenuForSnapshot() { return effectsPage->presetMenu(); }
juce::PopupMenu SimpleDock::addMenuForSnapshot() { return effectsPage->addMenu(); }
void SimpleDock::setFontsForSnapshot (std::vector<sf::IndexedFont> fonts)
{
    instrumentPage->setFonts (std::move (fonts), true);
    instrumentPage->setSource (InstrumentPage::From::fonts);
}
void SimpleDock::setPluginsForSnapshot (std::vector<soundlib::InstalledPlugin> plugins)
{
    instrumentPage->setPlugins (std::move (plugins));
    instrumentPage->setSource (InstrumentPage::From::plugins);
}
void SimpleDock::showCategoryForSnapshot (int category) { instrumentPage->pickCategory (category); }
int SimpleDock::soundRowsForCheck() const { return instrumentPage->rowCount(); }
juce::String SimpleDock::soundNameForCheck() const { return instrumentPage->shownName(); }
void SimpleDock::clickSoundForCheck (int row) { instrumentPage->clickRow (row, false); }
void SimpleDock::stepSoundForCheck (int delta) { instrumentPage->stepSound (delta); }
void SimpleDock::turnKnobForCheck (int card, float value) { effectsPage->turnForCheck (card, value); }
void SimpleDock::moveCardForCheck (int card, int delta) { effectsPage->move (card, delta); }
void SimpleDock::removeCardForCheck (int card) { effectsPage->remove (card); }
void SimpleDock::addOneKnobForCheck (int goal) { effectsPage->addOneKnob (goal); }
void SimpleDock::setBlendForCheck (float value) { effectsPage->blendForCheck (value); }
void SimpleDock::setReverbForCheck (float value) { effectsPage->reverbForCheck (value); }
juce::Rectangle<int> SimpleDock::cardBoundsForSnapshot (int card) const
{
    const auto* c = effectsPage->cardForSnapshot (card);
    return c != nullptr ? getLocalArea (c, c->getLocalBounds()) : juce::Rectangle<int>();
}
void SimpleDock::setSearchForSnapshot (const juce::String& text) { instrumentPage->searchFor (text); }
} // namespace lynx
