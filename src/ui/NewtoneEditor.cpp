#include "ui/SlotWatch.h"
#include "ui/FileChoosers.h"
#include "ui/NewtoneEditor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "core/AudioEdit.h"
#include "core/MixerOps.h"
#include "core/NewtoneFx.h"
#include "core/VocalPitch.h"
#include "engine/MidiFileIo.h"
#include "engine/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/MidiClipboard.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
bool isNewtoneKind (EffectKind k)
{
    return k == EffectKind::newtone || k == EffectKind::newtime;
}

const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// FL's naming, as LYNX's piano roll (round 149: the editor said D4 where the piano roll says D5): key 60 = C5
juce::String noteName (double midi)
{
    const auto k = (int) std::lround (midi);
    return juce::String (noteNames[((k % 12) + 12) % 12]) + juce::String (k / 12);
}

// Round 190kj: FL's Newtone window (FL 2026, measured on a 100 % capture): its header, piano-roll grid and note look.
// Colours read off the capture; every shape is LYNX's own vector drawing.
namespace flnt
{
// header: FL's form data (TSynthEditorForm in Newtone_x64.dll: control rectangles, TColor values, hints); note view: the
// 100 % capture of round 190gu
const juce::Colour header { 0xff1a2227 }, headerDark { 0xff151b1f }, transportFace { 0xff292f32 }, buttonFace { 0xff2f3437 },
    buttonEdge { 0xff12171a }, iconInk { 0xffe4eaee }, caption { 0xff474f54 }, nameInk { 0xffebeef9 }, valueInk { 0xffb0b2b7 },
    knobLabel { 0xffbccaca }, rail { 0xff3f484d }, cap { 0xffaeb4b7 }, centreArc { 0xff33c18c }, variationArc { 0xffffb00d },
    transArc { 0xff33c1e1 }, slaveOn { 0xffff9b37 }, followOn { 0xff40c65a }, cutOn { 0xff799bbd }, advancedOn { 0xffc460a0 },
    padOff { 0xff2a2a2a }, rulerBg { 0xff1e2930 }, rulerLine { 0xff2d3940 }, rulerInk { 0xffb4c0c8 }, rowIn { 0xff3d4e59 },
    rowOut { 0xff2d3d47 }, rowLine { 0xff2b3b45 }, stepLine { 0xff2a3b46 }, beatLine { 0xff283b46 }, barLine { 0xff102632 },
    keyWhite { 0xffcdd1d9 }, keyBlack { 0xff2d3038 }, keyLine { 0xff8b8e96 }, keyC { 0xffadb1b8 }, blob { 0xffced7dd },
    orange { 0xffff7900 }, orangeLine { 0xffff9b30 }, pitchCore { 0xff14120f }, green { 0xff40c65a }, scroll { 0xff323a3f };
constexpr int headerHeight = 88, formWidth = 727, keyboardWidth = 76, rulerHeight = 16, scrollSize = 18, defaultRow = 22;
// FL's TVectorWheel sweep: from 145.7 degrees left of the top, 304 degrees clockwise (StartAngle / EndAngle)
constexpr float knobStart = -2.5428f, knobSweep = 5.3058f;
} // namespace flnt

// Snap to scale (FL's Edit › Snap to scale / Scale): pitch classes from the key, bit n = key + n
struct ScaleDef
{
    const char* name;
    std::uint16_t mask;
};
const ScaleDef scales[] = {
    { TR_NOOP ("Chromatic"), 0xFFF },
    { TR_NOOP ("Major"), 0b101010110101 },
    { TR_NOOP ("Minor"), 0b010110101101 },
    { TR_NOOP ("Harmonic minor"), 0b100110101101 },
    { TR_NOOP ("Melodic minor"), 0b101010101101 },
    { TR_NOOP ("Dorian"), 0b011010101101 },
    { TR_NOOP ("Mixolydian"), 0b011010110101 },
    { TR_NOOP ("Major pentatonic"), 0b001010010101 },
    { TR_NOOP ("Minor pentatonic"), 0b010010101001 },
    { TR_NOOP ("Blues"), 0b010011101001 },
};

// A labelled bar slider of the editor's knob rows / note panel
struct BarSlider
{
    juce::Label label;
    juce::Slider slider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };

    void setUp (juce::Component& owner, const juce::String& caption, double lo, double hi, double step, double def, const juce::String& tip)
    {
        label.setText (caption, juce::dontSendNotification);
        label.setFont (theme::font (theme::type::small));
        label.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        label.setJustificationType (juce::Justification::centredLeft);
        slider.setRange (lo, hi, step);
        slider.setDoubleClickReturnValue (true, def);
        slider.setValue (def, juce::dontSendNotification);
        slider.setTooltip (tip);
        label.setTooltip (tip);
        slider.setWantsKeyboardFocus (false);
        slider.setColour (juce::Slider::trackColourId, theme::colour::accentMuted);
        slider.setColour (juce::Slider::textBoxOutlineColourId, theme::colour::borderStrong);
        owner.addAndMakeVisible (label);
        owner.addAndMakeVisible (slider);
    }
    void setVisible (bool v)
    {
        label.setVisible (v);
        slider.setVisible (v);
    }
    void place (juce::Rectangle<int> r, int labelWidth)
    {
        label.setBounds (r.removeFromLeft (labelWidth));
        slider.setBounds (r);
    }
};

class NewtoneEditor final : public juce::Component,
                            public juce::SettableTooltipClient,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    NewtoneEditor (AudioEngine& e, int t, int s) : engine (e), track (t), slot (s)
    {
        for (auto* b : { &load, &analyse, &cutMode, &snapAll, &reset, &preview, &save, &scoreButton, &stretch, &snapButton, &toPlaylist })
        {
            addAndMakeVisible (*b);
            b->setWantsKeyboardFocus (false);
        }
        addAndMakeVisible (slave);
        addAndMakeVisible (status);
        status.setFont (theme::font (theme::type::small));
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);

        load.setButtonText (tr ("Load sample..."));
        analyse.setButtonText (tr ("Analyse"));
        cutMode.setButtonText (tr ("Cut"));
        cutMode.setClickingTogglesState (true);
        cutMode.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        cutMode.setTooltip (tr ("Cut mode (C): click a note to slice it there; Ctrl+click a note to glue it to the one before"));
        snapAll.setButtonText (tr ("Snap all to notes"));
        reset.setButtonText (tr ("Reset edits"));
        preview.setButtonText (tr ("Preview"));
        save.setButtonText (tr ("Save as WAV..."));
        scoreButton.setButtonText (tr ("Score"));
        scoreButton.setTooltip (tr ("The notes as a score: send them to a channel's piano roll, or save them as a MIDI file"));
        stretch.setButtonText (tr ("Length..."));
        slave.setButtonText (tr ("Play with the song"));
        slave.setTooltip (tr ("Slave to host playback: while the song plays, the edited sample plays from the song's start"));
        load.setTooltip (tr ("Choose the audio file to edit (WAV, AIFF, FLAC, OGG, MP3)"));
        analyse.setTooltip (tr ("Find the notes of the sample again (discards the note edits)"));

        snapButton.setButtonText (tr ("Snap"));
        snapButton.setTooltip (tr ("Snap to grid: moved notes (Ctrl+drag) and warp markers land on the song's beat grid"));
        toPlaylist.setButtonText (tr ("Send to Playlist"));
        toPlaylist.setTooltip (tr ("Send to the Playlist as an audio clip"));
        snapButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
        snapButton.onClick = [this] { showSnapMenu(); };
        toPlaylist.onClick = [this] { sendToPlaylist(); };
        load.onClick = [this] { chooseSample(); };
        analyse.onClick = [this] { reanalyse(); };
        cutMode.onClick = [this] { updateButtons(); };
        snapAll.onClick = [this] {
            for (int k = 0; k < (int) notes.pitches.size(); ++k)
                snapNote (k);
            commit();
        };
        reset.onClick = [this] {
            if (warpMode())
                warp.markers.clear();
            else
            {
                for (auto& p : notes.pitches)
                {
                    const auto origPitch = p.origPitch;
                    const auto a = p.origStart, b = p.origEnd;
                    const auto si = p.origSmoothIn, so = p.origSmoothOut;
                    p = {};
                    p.origPitch = origPitch;
                    p.origStart = p.curStart = a;
                    p.origEnd = p.curEnd = b;
                    p.smoothIn = p.origSmoothIn = si;
                    p.smoothOut = p.origSmoothOut = so;
                }
                notes.centre = 0.0;
                notes.variation = notes.transition = 1.0;
                notes.globalFormant = 1.0;
            }
            commit();
        };
        preview.onClick = [this] { togglePreview(); };
        save.onClick = [this] { saveWav(); };
        scoreButton.onClick = [this] { showScoreMenu(); };
        stretch.onClick = [this] { showLengthMenu(); };
        slave.onClick = [this] {
            const auto on = slave.getToggleState();
            engine.editSong ([this, on] (Song& song) {
                if (auto* fx = slotIn (song))
                    fx->params[0] = on ? 1.0f : 0.0f;
            });
        };

        // FL's global knobs: they act on the selected notes, or on every note when none is selected
        centreKnob.setUp (*this, tr ("Center"), 0.0, 100.0, 1.0, 0.0, tr ("Pulls the notes toward their semitone: 0 % as sung, 100 % exactly on it"));
        variationKnob.setUp (*this, tr ("Variation"), 0.0, 100.0, 1.0, 100.0, tr ("The notes' vibrato and pitch drift: 100 % as sung, 0 % flat"));
        transitionKnob.setUp (*this, tr ("Transition"), 0.0, 200.0, 1.0, 100.0, tr ("How long the pitch takes to move from one note to the next (100 % = as analysed)"));
        centreKnob.slider.setTextValueSuffix (" %");
        variationKnob.slider.setTextValueSuffix (" %");
        transitionKnob.slider.setTextValueSuffix (" %");
        hookSlider (centreKnob, [this] (double v) { newtonefx::applyCentre (notes, v / 100.0, selection()); });
        hookSlider (variationKnob, [this] (double v) { newtonefx::applyVariation (notes, v / 100.0, selection()); });
        hookSlider (transitionKnob, [this] (double v) { newtonefx::applyTransition (notes, v / 100.0, selection()); });

        for (int k = 0; k < 12; ++k)
            keyBox.addItem (noteNames[k], k + 1);
        for (int k = 0; k < (int) std::size (scales); ++k)
            scaleBox.addItem (tr (scales[k].name), k + 1);
        keyBox.setSelectedId (1, juce::dontSendNotification);
        scaleBox.setSelectedId (1, juce::dontSendNotification);
        keyBox.setTooltip (tr ("The key of the scale the notes snap to"));
        scaleBox.setTooltip (tr ("Snap to scale: notes move only to the notes of this scale (Chromatic = every semitone)"));
        for (auto* c : { &keyBox, &scaleBox })
        {
            addAndMakeVisible (*c);
            c->setWantsKeyboardFocus (false);
        }

        // the note panel (FL's Advanced Edit): the selected notes' own settings
        addAndMakeVisible (panelTitle);
        panelTitle.setFont (theme::font (theme::type::small, true));
        panelTitle.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        pitchKnob.setUp (*this, tr ("Pitch"), -24.0, 24.0, 0.01, 0.0, tr ("The note's pitch change in semitones (Alt+drag a note does the same)"));
        formantKnob.setUp (*this, tr ("Formant"), -12.0, 12.0, 0.1, 0.0, tr ("Moves the voice's formants (its apparent size, age or sex) without changing the pitch"));
        volumeKnob.setUp (*this, tr ("Volume"), -24.0, 12.0, 0.1, 0.0, tr ("The note's level in dB"));
        driftKnob.setUp (*this, tr ("Variation"), 0.0, 100.0, 1.0, 100.0, tr ("This note's vibrato and pitch drift: 100 % as sung, 0 % flat"));
        rampInKnob.setUp (*this, tr ("Ramp in"), -12.0, 12.0, 0.05, 0.0, tr ("Starts the note this many semitones off and glides to it over the transition time"));
        rampOutKnob.setUp (*this, tr ("Ramp out"), -12.0, 12.0, 0.05, 0.0, tr ("Glides the note's end this many semitones away over the transition time"));
        vibRateKnob.setUp (*this, tr ("Vibrato rate"), 0.0, 12.0, 0.05, 0.0, tr ("An added vibrato's speed in Hz (0 = none)"));
        vibStartKnob.setUp (*this, tr ("Vibrato start"), 0.0, 2.0, 0.01, 0.0, tr ("The added vibrato's depth at the note's start, in semitones"));
        vibEndKnob.setUp (*this, tr ("Vibrato end"), 0.0, 2.0, 0.01, 0.0, tr ("The added vibrato's depth at the note's end, in semitones"));
        tremoloKnob.setUp (*this, tr ("Tremolo"), 0.0, 100.0, 1.0, 0.0, tr ("A volume wobble in step with the added vibrato"));
        pitchKnob.slider.setTextValueSuffix (" st");
        formantKnob.slider.setTextValueSuffix (" st");
        volumeKnob.slider.setTextValueSuffix (" dB");
        driftKnob.slider.setTextValueSuffix (" %");
        rampInKnob.slider.setTextValueSuffix (" st");
        rampOutKnob.slider.setTextValueSuffix (" st");
        vibRateKnob.slider.setTextValueSuffix (" Hz");
        vibStartKnob.slider.setTextValueSuffix (" st");
        vibEndKnob.slider.setTextValueSuffix (" st");
        tremoloKnob.slider.setTextValueSuffix (" %");
        auto perNote = [this] (BarSlider& b, std::function<void (newtonefx::Note&, double)> set) {
            hookSlider (b, [this, set] (double v) {
                for (const auto k : selection())
                    set (notes.pitches[(std::size_t) k], v);
            });
        };
        perNote (pitchKnob, [] (newtonefx::Note& p, double v) { p.shift = v; });
        perNote (formantKnob, [] (newtonefx::Note& p, double v) { p.formant = std::exp2 (v / 12.0); });
        perNote (volumeKnob, [] (newtonefx::Note& p, double v) { p.volume = std::pow (10.0, v / 20.0); });
        perNote (driftKnob, [] (newtonefx::Note& p, double v) { p.drift = v / 100.0; });
        perNote (rampInKnob, [] (newtonefx::Note& p, double v) { p.rampIn = v; });
        perNote (rampOutKnob, [] (newtonefx::Note& p, double v) { p.rampOut = v; });
        perNote (vibRateKnob, [] (newtonefx::Note& p, double v) { p.vibPeriod = v > 0.0 ? 1.0 / v : 0.0; });
        perNote (vibStartKnob, [] (newtonefx::Note& p, double v) { p.vibAmpStart = v; });
        perNote (vibEndKnob, [] (newtonefx::Note& p, double v) { p.vibAmpEnd = v; });
        perNote (tremoloKnob, [] (newtonefx::Note& p, double v) { p.tremAmp = v / 100.0; });

        for (auto* bar : { &hScroll, &vScroll })
        {
            addChildComponent (*bar);
            bar->addListener (this);
            bar->setAutoHide (false);
            bar->setColour (juce::ScrollBar::backgroundColourId, flnt::scroll.darker (0.3f));
            bar->setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff5d6b73));
        }
        setWantsKeyboardFocus (true);
        engine.addChangeListener (this);
        reloadFromSong();
        // (no timer while idle: it runs only while the preview plays, round 190kj)
        setSize (flLook() ? flnt::formWidth : 1040, flLook() ? 582 : 520); // (FL's TSynthEditorForm: 727 x 582)
        if (flLook())
            centreRowsOnNotes();
    }

    ~NewtoneEditor() override
    {
        engine.removeChangeListener (this);
        if (previewing)
            engine.previewSample (nullptr);
    }

    // ------------------------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        if (flLook())
        {
            paintFl (g);
            return;
        }
        g.fillAll (theme::colour::bg2);
        const auto area = canvasArea();
        g.setColour (theme::colour::bg0);
        g.fillRect (area);
        if (! warpMode() && sample != nullptr)
        {
            g.setColour (theme::colour::bg1);
            g.fillRect (panelArea());
            g.fillRect (rulerArea());
        }

        if (sample == nullptr)
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::body));
            g.drawText (validSlot() ? tr ("Load a sample to edit it (Load sample... or drop an audio file here)") : tr ("This slot no longer holds this plugin"),
                        area, juce::Justification::centred);
            return;
        }

        if (warpMode())
            paintWarp (g, area);
        else
        {
            paintNotes (g, area);
            paintRuler (g, rulerArea());
        }

        // the rubber band of a selection drag
        if (! band.isEmpty())
        {
            g.setColour (theme::colour::selectionFill);
            g.fillRect (band);
            g.setColour (theme::colour::selection);
            g.drawRect (band, 1.0f);
        }

        // playhead of the preview
        if (previewing)
            if (const auto pos = engine.getPreviewPosition(); pos >= 0.0)
            {
                const auto x = xOfOutput (pos);
                g.setColour (theme::colour::playhead);
                g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
            }
    }

    void resized() override
    {
        if (flLook())
        {
            resizedFl();
            return;
        }
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (26);
        for (auto* b : { &load, &analyse, &cutMode, &snapAll, &stretch, &reset, &preview, &save, &toPlaylist, &scoreButton, &snapButton })
            if (b->isVisible())
            {
                b->setBounds (top.removeFromLeft (juce::jmax (60, b->getBestWidthForHeight (26) + 14)));
                top.removeFromLeft (4);
            }
        slave.setBounds (top.removeFromRight (170));
        r.removeFromTop (4);
        if (! warpMode())
        {
            auto knobs = r.removeFromTop (24);
            const auto w = juce::jmin (230, (knobs.getWidth() - 260) / 3);
            const auto lw = theme::labelColumnWidth ({ &centreKnob.label, &variationKnob.label, &transitionKnob.label }, 64, 120);
            for (auto* k : { &centreKnob, &variationKnob, &transitionKnob })
            {
                k->place (knobs.removeFromLeft (w), lw);
                knobs.removeFromLeft (8);
            }
            knobs.removeFromLeft (8);
            keyBox.setBounds (knobs.removeFromLeft (64));
            knobs.removeFromLeft (4);
            scaleBox.setBounds (knobs.removeFromLeft (160));
            r.removeFromTop (4);
        }
        status.setBounds (r.removeFromBottom (18));
        if (! warpMode())
        {
            auto panel = panelArea().reduced (6);
            panelTitle.setBounds (panel.removeFromTop (20));
            const auto lw = theme::labelColumnWidth ({ &pitchKnob.label, &formantKnob.label, &volumeKnob.label, &driftKnob.label, &rampInKnob.label, &rampOutKnob.label,
                                                       &vibRateKnob.label, &vibStartKnob.label, &vibEndKnob.label, &tremoloKnob.label },
                                                     92, 130);
            for (auto* k : { &pitchKnob, &formantKnob, &volumeKnob, &driftKnob, &rampInKnob, &rampOutKnob, &vibRateKnob, &vibStartKnob, &vibEndKnob, &tremoloKnob })
            {
                k->place (panel.removeFromTop (22), lw);
                panel.removeFromTop (3);
            }
        }
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto cmd = key.getModifiers().isCommandDown();
        if (cmd && key.getKeyCode() == 'Z')
        {
            if (key.getModifiers().isShiftDown())
                engine.redo();
            else
                engine.undo();
            return true;
        }
        if (cmd && key.getKeyCode() == 'Y')
        {
            engine.redo();
            return true;
        }
        if (flLook() && flKey (key))
            return true;
        if (warpMode() || sample == nullptr)
            return false;
        if (cmd && key.getKeyCode() == 'A')
        {
            selected.clear();
            for (int k = 0; k < (int) notes.pitches.size(); ++k)
                selected.insert (k);
            selectionChanged();
            return true;
        }
        if (! cmd && (key.getKeyCode() == 'C' || key.getKeyCode() == 'c'))
        {
            cutMode.setToggleState (! cutMode.getToggleState(), juce::dontSendNotification);
            updateButtons();
            return true;
        }
        if (key == juce::KeyPress::escapeKey)
        {
            selected.clear();
            selectionChanged();
            return true;
        }
        if (selected.empty())
            return false;
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            for (const auto k : selected)
                notes.pitches[(std::size_t) k].shift += key == juce::KeyPress::upKey ? 1.0 : -1.0;
            commit();
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            const auto mute = std::any_of (selected.begin(), selected.end(), [this] (int k) { return notes.pitches[(std::size_t) k].enabled; });
            for (const auto k : selected)
                notes.pitches[(std::size_t) k].enabled = ! mute;
            commit();
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        drag = {};
        band = {};
        if (flLook() && flMouseDown (e))
            return;
        if (sample == nullptr)
            return;
        if (! warpMode() && rulerArea().contains (e.getPosition()))
        {
            // FL's ruler click: the selected notes all move to that pitch (auto-flatten)
            const auto key = std::round (pitchOfY (e.position.y));
            for (const auto k : selection())
            {
                auto& p = notes.pitches[(std::size_t) k];
                p.shift = key - p.origPitch;
            }
            commit();
            return;
        }
        if (! canvasArea().contains (e.getPosition()))
            return;
        if (warpMode())
        {
            const auto m = markerAt (e.position.x);
            if (e.mods.isPopupMenu())
            {
                if (m >= 0)
                {
                    warp.markers.erase (warp.markers.begin() + m);
                    commit();
                }
                return;
            }
            drag.marker = m;
            return;
        }

        const auto hit = noteAt (e.position);
        const auto cutting = cutMode.getToggleState() || (flLook() && e.mods.isShiftDown()); // (FL: Shift = Cut mode while held)
        if (cutting && hit >= 0 && ! e.mods.isPopupMenu())
        {
            if (e.mods.isCtrlDown() || e.mods.isCommandDown() || (flLook() && e.mods.isAltDown()))
            {
                if (newtonefx::glueNote (notes, (std::size_t) hit))
                {
                    selected = { hit - 1 };
                    commit();
                }
            }
            else
                cutAt (hit, e.position.x);
            return;
        }
        if (hit < 0)
        {
            if (! e.mods.isShiftDown())
                selected.clear();
            drag.banding = ! e.mods.isPopupMenu();
            drag.bandStart = e.position;
            selectionChanged();
            return;
        }
        if (! flLook() && e.mods.isShiftDown())
        {
            if (! selected.erase (hit))
                selected.insert (hit);
            selectionChanged();
            return;
        }
        if (selected.count (hit) == 0)
        {
            selected = { hit };
            selectionChanged();
        }
        if (e.mods.isPopupMenu())
        {
            // FL: right-click snaps the note to its nearest semitone (Shift+right-click: LYNX's note menu)
            if (flLook() && ! e.mods.isShiftDown())
            {
                for (const auto q : selection())
                    snapNote (q);
                commit();
                return;
            }
            showNoteMenu (hit);
            return;
        }
        // a drag moves every selected note (pitch, or time with Ctrl)
        drag.note = hit;
        drag.time = e.mods.isCtrlDown() || e.mods.isCommandDown();
        for (const auto k : selected)
        {
            const auto& p = notes.pitches[(std::size_t) k];
            drag.from.push_back ({ k, p.shift, p.curStart, p.curEnd });
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (flLook() && flMouseDrag (e))
            return;
        if (warpMode())
        {
            if (drag.marker < 0 || drag.marker >= (int) warp.markers.size())
                return;
            auto& m = warp.markers[(std::size_t) drag.marker];
            const auto lo = drag.marker > 0 ? warp.markers[(std::size_t) drag.marker - 1].output + 1 : 1;
            const auto hi = drag.marker + 1 < (int) warp.markers.size() ? warp.markers[(std::size_t) drag.marker + 1].output - 1
                                                                        : (std::int64_t) (4 * frames());
            const auto at = snapPosition ((std::int64_t) std::llround (outputOfX (e.position.x)), e.mods.isAltDown());
            m.output = std::clamp<std::int64_t> (at, lo, std::max (lo, hi));
            drag.moved = true;
            repaint();
            return;
        }
        if (drag.banding)
        {
            band = juce::Rectangle<float> (drag.bandStart, e.position);
            auto picked = e.mods.isShiftDown() ? selectedBefore : std::set<int> {};
            for (int k = 0; k < (int) notes.pitches.size(); ++k)
                if (band.intersects (noteBounds (k)))
                    picked.insert (k);
            selected = picked;
            selectionChanged();
            return;
        }
        if (drag.note < 0)
            return;
        auto anchor = std::find_if (drag.from.begin(), drag.from.end(), [this] (const auto& f) { return f.note == drag.note; });
        if (anchor == drag.from.end())
            return;
        for (const auto& f : drag.from)
        {
            if (f.note < 0 || f.note >= (int) notes.pitches.size())
                continue;
            auto& p = notes.pitches[(std::size_t) f.note];
            if (drag.time)
            {
                auto dx = (std::int64_t) std::llround ((double) e.getDistanceFromDragStartX() / pixelsPerSample());
                if (const auto& a = *anchor; snapDivision > 0)
                    dx = snapPosition (a.start + dx, e.mods.isAltDown()) - a.start; // the dragged note's start on the grid, the others follow
                const auto len = f.end - f.start;
                p.curStart = std::clamp<std::int64_t> (f.start + dx, 0, std::max<std::int64_t> (0, frames() - len));
                p.curEnd = p.curStart + len;
            }
            else
            {
                // the dragged note snaps (to the scale; Alt = free) and the others follow it by the same amount
                auto s = anchor->shift - (double) e.getDistanceFromDragStartY() / rowHeight();
                const auto& a = notes.pitches[(std::size_t) anchor->note];
                if (! e.mods.isAltDown())
                    s = snapped (a.origPitch + s) - a.origPitch;
                p.shift = std::clamp (f.shift + (s - anchor->shift), -24.0, 24.0);
            }
        }
        drag.moved = true;
        updatePanel();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (flLook())
            flMouseUp (e);
        if (drag.moved)
            commit();
        drag = {};
        band = {};
        selectedBefore = selected;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (flLook())
        {
            flMouseMove (e);
            return;
        }
        const auto cutting = ! warpMode() && cutMode.getToggleState() && noteAt (e.position) >= 0;
        setMouseCursor (cutting ? juce::MouseCursor::IBeamCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverHit != Hit::none)
        {
            hoverHit = Hit::none;
            repaint (headerArea());
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (sample == nullptr || ! canvasArea().contains (e.getPosition()))
            return;
        if (warpMode())
        {
            // a new marker where the sample plays now (it does not move anything until dragged)
            const auto out = outputOfX (e.position.x);
            const auto src = (std::int64_t) std::llround (newtonefx::warpToSource (warp, frames(), out));
            newtonefx::WarpMarker m { src, (std::int64_t) std::llround (out) };
            if (m.source <= 0 || m.source >= frames())
                return;
            auto at = std::lower_bound (warp.markers.begin(), warp.markers.end(), m, [] (const auto& a, const auto& b) { return a.source < b.source; });
            if (at != warp.markers.end() && at->source == m.source)
                return;
            warp.markers.insert (at, m);
            // the first marker also pins the sample's end where it plays now, so moving a marker stretches one side
            // and squeezes the other instead of changing the whole length
            if (warp.markers.back().source < frames() - 1)
                warp.markers.push_back ({ frames() - 1, (std::int64_t) std::llround (newtonefx::warpToOutput (warp, frames(), (double) (frames() - 1))) });
            commit();
            return;
        }
        if (cutMode.getToggleState())
            return;
        if (flLook())
        {
            // FL: double-click a note = Advanced Edit on it; double-click the empty roll leaves Advanced Edit
            if (hitAt (e.position) != Hit::none || ! flCanvas().contains (e.getPosition()))
                return;
            if (const auto k = noteAt (e.position); k >= 0)
            {
                currentNote = k;
                advancedEdit = true;
                // FL zooms in on the note (rows ~1.5 x taller), keeping it where it was clicked
                const auto anchor = pitchOfY (e.position.y);
                rowH = std::max (rowH, 33.0);
                topPitch = anchor + (double) (e.position.y - (float) flCanvas().getY()) / rowH;
                const auto at = outputOfX (e.position.x);
                zoom = std::clamp (zoom * 1.35, 1.0, 512.0);
                scroll = at - (e.position.x - (float) flCanvas().getX()) / pixelsPerSample();
                clampScroll();
                updateScrollBars();
            }
            else
                advancedEdit = false, currentNote = -1;
            selectionChanged();
            resized();
            repaint();
            return;
        }
        if (const auto k = noteAt (e.position); k >= 0)
        {
            snapNote (k);
            commit();
        }
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return validSlot() && files.size() == 1 && SampleLibrary::isSampleFile (juce::File (files[0]));
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (isInterestedInFileDrag (files))
            useSample (juce::File (files[0]));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (sample == nullptr)
            return;
        if (flLook())
        {
            flWheel (e, w);
            return;
        }
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const auto anchor = outputOfX (e.position.x);
            zoom = std::clamp (zoom * (w.deltaY > 0 ? 1.25 : 0.8), 1.0, 64.0);
            scroll = anchor - (e.position.x - (float) canvasArea().getX()) / pixelsPerSample();
        }
        else
            scroll -= (w.deltaY != 0.0f ? w.deltaY : w.deltaX) * 0.25 * visibleLength();
        clampScroll();
        repaint();
    }

private:
    struct Drag
    {
        struct From
        {
            int note = -1;
            double shift = 0.0;
            std::int64_t start = 0, end = 0;
        };
        int note = -1, marker = -1;
        std::vector<From> from;
        bool time = false, moved = false, banding = false;
        juce::Point<float> bandStart;
    };

    AudioEngine& engine;
    int track, slot;
    juce::TextButton load, analyse, cutMode, snapAll, reset, preview, save, scoreButton, stretch, snapButton, toPlaylist;
    int snapDivision = 0; // FL's Snap to grid: beat / division, 0 = Off
    juce::ToggleButton slave;
    juce::Label status, panelTitle;
    BarSlider centreKnob, variationKnob, transitionKnob;
    BarSlider pitchKnob, formantKnob, volumeKnob, driftKnob, rampInKnob, rampOutKnob, vibRateKnob, vibStartKnob, vibEndKnob, tremoloKnob;
    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<juce::FileChooser> chooser;

    EffectKind kind = EffectKind::newtone;
    std::string samplePath;
    std::vector<std::uint8_t> savedState; // the state shown (to tell our own edits from outside changes)
    std::shared_ptr<const SampleData> sample;
    std::vector<float> peaks;              // |sample| maxima per 256 frames
    std::vector<double> pitchTrack;        // the sample's pitch (MIDI, < 0 unvoiced) per pitchHop frames
    int pitchHop = 256;
    newtonefx::Track notes;
    newtonefx::WarpMap warp;
    std::set<int> selected, selectedBefore;
    Drag drag;
    juce::Rectangle<float> band;
    double zoom = 1.0, scroll = 0.0;       // horizontal view: zoom over the whole length, first visible sample
    bool previewing = false;

    // ------------------------------------------------------------------------------------------------ selection
    // the notes an edit applies to: the selection, or every note
    std::vector<int> selection() const
    {
        if (! selected.empty())
            return { selected.begin(), selected.end() };
        std::vector<int> all ((std::size_t) notes.pitches.size());
        for (int k = 0; k < (int) all.size(); ++k)
            all[(std::size_t) k] = k;
        return all;
    }

    void selectionChanged()
    {
        for (auto it = selected.begin(); it != selected.end();)
            it = *it < 0 || *it >= (int) notes.pitches.size() ? selected.erase (it) : std::next (it);
        updatePanel();
        repaint();
    }

    void hookSlider (BarSlider& b, std::function<void (double)> apply)
    {
        b.slider.onValueChange = [this, &b, apply] {
            if (updatingPanel || sample == nullptr || warpMode())
                return;
            apply (b.slider.getValue());
            if (! b.slider.isMouseButtonDown())
                commit();
            else
                repaint();
        };
        b.slider.onDragEnd = [this] {
            if (! updatingPanel)
                commit();
        };
    }
    bool updatingPanel = false;

    void updatePanel()
    {
        const juce::ScopedValueSetter<bool> guard (updatingPanel, true);
        const auto noteMode = ! warpMode() && sample != nullptr;
        for (auto* k : { &centreKnob, &variationKnob, &transitionKnob })
            k->setVisible (! warpMode());
        keyBox.setVisible (! warpMode());
        scaleBox.setVisible (! warpMode());
        panelTitle.setVisible (noteMode);
        const auto one = noteMode && ! selected.empty();
        for (auto* k : { &pitchKnob, &formantKnob, &volumeKnob, &driftKnob, &rampInKnob, &rampOutKnob, &vibRateKnob, &vibStartKnob, &vibEndKnob, &tremoloKnob })
        {
            k->setVisible (noteMode);
            k->slider.setEnabled (one);
        }
        centreKnob.slider.setValue (notes.centre * 100.0, juce::dontSendNotification);
        variationKnob.slider.setValue (notes.variation * 100.0, juce::dontSendNotification);
        transitionKnob.slider.setValue (notes.transition * 100.0, juce::dontSendNotification);
        panelTitle.setText (selected.empty() ? tr ("Note: select one or more notes")
                                             : selected.size() == 1 ? tr ("Note {0}", { juce::String (*selected.begin() + 1) })
                                                                    : tr ("{0} notes selected", { juce::String ((int) selected.size()) }),
                            juce::dontSendNotification);
        if (! one)
            return;
        const auto& p = notes.pitches[(std::size_t) *selected.begin()];
        pitchKnob.slider.setValue (p.shift);
        formantKnob.slider.setValue (12.0 * std::log2 (std::max (1e-6, p.formant)));
        volumeKnob.slider.setValue (20.0 * std::log10 (std::max (1e-6, p.volume)));
        driftKnob.slider.setValue (p.drift * 100.0);
        rampInKnob.slider.setValue (p.rampIn);
        rampOutKnob.slider.setValue (p.rampOut);
        vibRateKnob.slider.setValue (p.vibPeriod > 0.0 ? 1.0 / p.vibPeriod : 0.0);
        vibStartKnob.slider.setValue (p.vibAmpStart);
        vibEndKnob.slider.setValue (p.vibAmpEnd);
        tremoloKnob.slider.setValue (p.tremAmp * 100.0);
    }

    // the scale: FL's look uses the track's key (FL's KeyIdx: the detected key, or the one picked in Edit › Scale)
    std::uint16_t scaleMask() const
    {
        if (flLook())
            return newtonefx::keyScaleMask (notes.key);
        const auto i = std::clamp (scaleBox.getSelectedId() - 1, 0, (int) std::size (scales) - 1);
        return scales[i].mask;
    }
    int scaleKey() const { return flLook() ? std::max (0, notes.key) % 12 : std::max (0, keyBox.getSelectedId() - 1); }
    double snapped (double pitch) const
    {
        return newtonefx::nearestInScale (pitch, scaleKey(), flLook() && ! snapScale ? (std::uint16_t) 0xFFF : scaleMask());
    }
    void snapNote (int k)
    {
        auto& p = notes.pitches[(std::size_t) k];
        if (p.origPitch > 0.0)
            p.shift = snapped (p.origPitch + p.shift) - p.origPitch;
    }

    void cutAt (int k, float x)
    {
        const auto at = (std::int64_t) std::llround (outputOfX (x));
        struct Ctx
        {
            const std::vector<double>* track;
            int hop;
        } ctx { &pitchTrack, pitchHop };
        auto pitchOf = [] (const void* c, std::int64_t a, std::int64_t b) {
            const auto& q = *static_cast<const Ctx*> (c);
            std::vector<double> v;
            for (auto f = std::max<std::int64_t> (0, a / q.hop); f < std::min<std::int64_t> ((std::int64_t) q.track->size(), b / q.hop + 1); ++f)
                if ((*q.track)[(std::size_t) f] > 0.0)
                    v.push_back ((*q.track)[(std::size_t) f]);
            if (v.empty())
                return 0.0;
            std::nth_element (v.begin(), v.begin() + (std::ptrdiff_t) (v.size() / 2), v.end());
            return v[v.size() / 2];
        };
        if (newtonefx::splitNote (notes, (std::size_t) k, at, pitchOf, &ctx))
        {
            selected = { k + 1 };
            commit();
        }
    }

    // --------------------------------------------------------------------------------------------------- the slot
    EffectSlot* slotIn (Song& song) const
    {
        if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
            return nullptr;
        auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
        return isNewtoneKind (fx.kind) ? &fx : nullptr;
    }

    const EffectSlot* currentSlot() const
    {
        const auto& song = engine.getSong();
        if (track < 0 || track >= (int) song.mixer.size() || slot < 0 || slot >= (int) song.mixer[(std::size_t) track].effects.size())
            return nullptr;
        const auto& fx = song.mixer[(std::size_t) track].effects[(std::size_t) slot];
        return isNewtoneKind (fx.kind) ? &fx : nullptr;
    }

    bool validSlot() const { return currentSlot() != nullptr; }
    bool warpMode() const { return kind == EffectKind::newtime; }
    std::int64_t frames() const { return sample != nullptr ? (std::int64_t) sample->frames() : 0; }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* fx = currentSlot();
        if (fx == nullptr || fx->samplePath != samplePath || fx->state != savedState || fx->kind != kind)
            reloadFromSong();
        else
            slave.setToggleState (fx->params[0] >= 0.5f, juce::dontSendNotification);
    }

    void reloadFromSong()
    {
        const auto* fx = currentSlot();
        if (fx == nullptr)
        {
            sample = nullptr;
            updateButtons();
            repaint();
            return;
        }
        kind = fx->kind;
        slave.setToggleState (fx->params[0] >= 0.5f, juce::dontSendNotification);
        savedState = fx->state;
        auto fresh = false;
        if (fx->samplePath != samplePath || sample == nullptr)
        {
            samplePath = fx->samplePath;
            sample = samplePath.empty() ? nullptr : engine.getSampleLibrary().load (juce::String::fromUTF8 (samplePath.c_str()));
            analyseView();
            readSampleTempo();
            fresh = true;
        }
        if (warpMode())
        {
            warp = newtonefx::warpOf (fx->state).value_or (newtonefx::WarpMap {}); // (LYNX's own, or FL's Newtime markers)
            warp.frames = frames();
        }
        else if (auto t = newtonefx::trackOf (fx->state); t && sample != nullptr)
        {
            notes = *t;
            fineAnalysis = notes.fineSplit; // (the analysis the notes came from)
            if (notes.key < 0)
                notes.key = newtonefx::detectKey (notes); // (an older state: the key the notes give)
        }
        else if (sample == nullptr)
            notes = {};
        else
        {
            // a sample without notes yet (a fresh slot, or a sample chosen from the slot menu): analyse it now. Round
            // 190li: a new slot renders with LYNX's natural engine; owner 2026-11-18: with "Separate close notes" on
            notes = newtonefx::analyseNewSlot (*sample);
            fineAnalysis = notes.fineSplit;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NewtoneEditor> (this)] {
                if (safe != nullptr)
                    safe->commit();
            });
        }
        selectionChanged();
        if (fresh && flLook() && getHeight() > 0)
            centreRowsOnNotes();
        updateButtons();
        repaint();
    }

    // the sample's own tempo (a WAV's acid chunk, FL's Tempo read-out; 0 = none, shown as "-")
    void readSampleTempo()
    {
        sampleTempo = 0.0;
        tempoSync = false;
        const juce::File f (juce::String::fromUTF8 (samplePath.c_str()));
        if (! f.hasFileExtension ("wav") || f.getSize() > (juce::int64) 512 * 1024 * 1024)
            return;
        juce::MemoryBlock data;
        if (! f.loadFileAsData (data))
            return;
        audioedit::Document doc;
        if (audioedit::decodeWavMarkers ((const std::uint8_t*) data.getData(), data.getSize(), doc))
            sampleTempo = doc.props.tempo;
    }

    // the waveform and pitch curve shown (not saved)
    void analyseView()
    {
        peaks.clear();
        pitchTrack.clear();
        zoom = 1.0;
        scroll = 0.0;
        if (sample == nullptr)
            return;
        const auto n = (std::int64_t) sample->frames();
        for (std::int64_t i = 0; i < n; i += 256)
        {
            float m = 0.0f;
            for (auto j = i; j < std::min (n, i + 256); ++j)
                m = std::max ({ m, std::abs (sample->leftData()[j]), sample->isStereo() ? std::abs (sample->rightData()[j]) : 0.0f });
            peaks.push_back (m);
        }
        if (warpMode())
            return;
        std::vector<float> mono (sample->leftData(), sample->leftData() + n);
        if (sample->isStereo())
            for (std::int64_t i = 0; i < n; ++i)
                mono[(std::size_t) i] = 0.5f * (mono[(std::size_t) i] + sample->rightData()[i]);
        pitchHop = std::max (64, (int) std::lround (256.0 * sample->sampleRate / 44100.0));
        for (const auto& f : vocal::analysePitch (mono.data(), (int) n, sample->sampleRate, pitchHop))
            pitchTrack.push_back (f.hz > 0.0 ? vocal::hzToNote (f.hz) : -1.0);
    }

    void commit()
    {
        auto state = warpMode() ? newtonefx::writeWarp (warp) : newtonefx::writeNative (notes);
        savedState = state;
        engine.editSong ([this, state] (Song& song) {
            if (auto* fx = slotIn (song))
                fx->state = state;
        });
        if (previewing)
            startPreview();
        updateButtons();
        updatePanel();
        repaint();
    }

    bool fineAnalysis = false; // (round 190kx: "Separate close notes")
    newtonefx::AnalyseOptions analyseOptions() const
    {
        newtonefx::AnalyseOptions o;
        o.fineSplit = fineAnalysis;
        return o;
    }

    void reanalyse()
    {
        if (sample == nullptr || warpMode())
            return;
        const auto engineWas = notes.engine; // (the slot keeps its engine)
        notes = newtonefx::analyseNotes (*sample, analyseOptions());
        notes.engine = engineWas;
        selected.clear();
        commit();
    }

    void chooseSample()
    {
        const juce::File current (juce::String::fromUTF8 (samplePath.c_str()));
        chooser = lynx::ui::makeFileChooser (tr ("Source sample"), current.existsAsFile() ? current : juce::File(), engine.getSampleLibrary().wildcard());
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<NewtoneEditor> (this)] (const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->useSample (fc.getResult());
                              });
    }

    void useSample (const juce::File& file)
    {
        if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Sample"), tr ("Could not read {0}", { file.getFileName() }));
            return;
        }
        const auto path = file.getFullPathName().toStdString();
        sample = nullptr;
        samplePath.clear();
        selected.clear();
        engine.editSong ([this, path] (Song& song) {
            if (auto* fx = slotIn (song))
            {
                fx->samplePath = path;
                fx->state.clear(); // (a new sample: its own notes / no markers)
            }
        });
        reloadFromSong();
    }

    void togglePreview()
    {
        if (previewing)
        {
            previewing = false;
            engine.previewSample (nullptr);
            stopTimer();
        }
        else
            startPreview();
        updateButtons();
        repaint();
    }

    void startPreview()
    {
        const auto* fx = currentSlot();
        if (fx == nullptr || sample == nullptr)
            return;
        previewing = true;
        silentTicks = 0;
        auto out = newtonefx::renderedSource (sample, savedState);
        if (flLook() && out != nullptr && (hasTimeSelection() || cursorAt > 0))
            out = newtonefx::cropped (*out, previewOffset(), hasTimeSelection() ? timeSelB : (std::int64_t) out->frames()); // (FL plays the selection, or from the cursor)
        previewSource = out;
        engine.previewSample (out, flLook() && looping, 0.8f);
        startTimerHz (30);
    }

    // FL's Snap to grid (Newtone / Newtime Snap menu): Beat, 1/2, 1/3, 1/4, 1/6, 1/8 beat, Off; Alt = free
    std::int64_t snapPosition (std::int64_t position, bool free) const
    {
        if (free || sample == nullptr)
            return position;
        return newtonefx::snapToBeatGrid (position, sample->sampleRate, engine.getSong().bpm, snapDivision);
    }

    void showSnapMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Snap to grid"));
        const std::pair<int, const char*> items[] { { 1, "Beat" }, { 2, "1/2 beat" }, { 3, "1/3 beat" }, { 4, "1/4 beat" },
                                                    { 6, "1/6 beat" }, { 8, "1/8 beat" }, { 0, "Off" } };
        for (const auto& [division, name] : items)
            menu.addItem (tr (name), true, snapDivision == division, [this, d = division] {
                snapDivision = d;
                snapButton.setToggleState (d > 0, juce::dontSendNotification);
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapButton));
    }

    // FL's Send to playlist as audio clip (SendToPlaylistBtnClick): the edited sample written as <sample name>.wav
    // and placed in the Playlist as an audio clip
    void sendToPlaylist()
    {
        if (sample == nullptr)
            return;
        auto out = newtonefx::renderedSource (sample, savedState);
        if (out == nullptr)
            return;
        const auto part = flLook() && hasTimeSelection();
        if (part)
            out = newtonefx::cropped (*out, timeSelA, timeSelB);
        const juce::File src (juce::String::fromUTF8 (samplePath.c_str()));
        const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("LYNX/Edits");
        folder.createDirectory();
        const auto title = src.getFileNameWithoutExtension().isNotEmpty() ? src.getFileNameWithoutExtension() : juce::String ("Dragged sample");
        const auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (title), ".wav");
        if (part)
        {
            if (! writeWav (*out, file))
                return;
        }
        else
        {
            // FL sends the audio with the note boundaries as markers (round 190kj)
            const auto bytes = audioedit::encodeWav (newtonefx::regionsDocument (*out, notes, true));
            if (! file.replaceWithData (bytes.data(), bytes.size()))
                return;
        }
        const auto& song = engine.getSong();
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) ((double) out->frames() / out->sampleRate * song.bpm / 60.0 * ppq));
        const auto start = (std::int64_t) (engine.getTransport().positionTicks.load() / ppq) * ppq;
        int lane = 0;
        while (std::any_of (song.playlist.begin(), song.playlist.end(),
                            [&] (const Clip& c) { return c.track == lane && c.start < start + length && c.end() > start; }))
            ++lane;
        const auto path = file.getFullPathName().toStdString();
        const auto name = title.toStdString();
        engine.editSong ([=] (Song& s) {
            ChannelDef c;
            c.name = name;
            c.instrument = InstrumentKind::audioClip;
            c.sampler.declickMode = audioClipDeclickMode; // FL's AudioTrack.fst
            c.samplePath = path;
            c.mixerTrack = s.claimFreeInsert();
            s.channels.push_back (c);
            Clip clip;
            clip.kind = ClipKind::audio;
            clip.channel = (int) s.channels.size() - 1;
            clip.track = lane;
            clip.start = start;
            clip.length = length;
            s.playlist.push_back (clip);
        });
        status.setText (tr ("Sent to the Playlist (track {0})", { juce::String (lane + 1) }), juce::dontSendNotification);
    }

    static bool writeWav (const SampleData& out, const juce::File& file)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
        juce::WavAudioFormat wav;
        const auto channels = out.isStereo() ? 2 : 1;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            stream != nullptr ? wav.createWriterFor (stream.get(), out.sampleRate, (unsigned int) channels, 24, {}, 0) : nullptr);
        if (writer == nullptr)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save"), tr ("Could not write {0}", { file.getFullPathName() }));
            return false;
        }
        stream.release();
        const float* data[2] { out.leftData(), out.isStereo() ? out.rightData() : out.leftData() };
        return writer->writeFromFloatArrays (data, channels, (int) out.frames());
    }

    void saveWav()
    {
        if (sample == nullptr)
            return;
        const juce::File src (juce::String::fromUTF8 (samplePath.c_str()));
        chooser = lynx::ui::makeFileChooser (tr ("Save the edited audio"), src.getSiblingFile (src.getFileNameWithoutExtension() + " (edited).wav"), "*.wav");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<NewtoneEditor> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  const auto out = newtonefx::renderedSource (safe->sample, safe->savedState);
                                  const auto target = fc.getResult().withFileExtension ("wav");
                                  if (safe->flLook() && safe->exportRegionsOnSave) // (FL's File › Export regions: the notes as regions)
                                  {
                                      const auto bytes = audioedit::encodeWav (newtonefx::regionsDocument (*out, safe->notes));
                                      target.replaceWithData (bytes.data(), bytes.size());
                                  }
                                  else
                                      writeWav (*out, target);
                              });
    }

    // FL's Create Score / Save as MIDI: the notes as they sound now
    std::vector<Note> scoreNotes (int channel) const
    {
        std::vector<Note> out;
        const auto bpm = std::max (1.0, engine.getSong().bpm);
        for (const auto& n : newtonefx::score (notes, sample->sampleRate))
        {
            Note note;
            note.channel = channel;
            note.start = (std::int64_t) std::llround (n.start * bpm / 60.0 * ppq);
            note.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (n.length * bpm / 60.0 * ppq));
            note.key = n.key;
            note.velocity = n.velocity;
            out.push_back (note);
        }
        return out;
    }

    void showScoreMenu()
    {
        if (sample == nullptr || warpMode())
            return;
        juce::PopupMenu menu, channels;
        const auto& song = engine.getSong();
        for (int c = 0; c < (int) song.channels.size(); ++c)
            channels.addItem (juce::String (c + 1) + ". " + juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()), [this, c] {
                const auto add = scoreNotes (c);
                engine.editSong ([add] (Song& s) {
                    if (s.patterns.empty())
                        return;
                    auto& pattern = s.patterns[(std::size_t) std::clamp (s.currentPattern, 0, (int) s.patterns.size() - 1)];
                    pattern.notes.insert (pattern.notes.end(), add.begin(), add.end());
                    pattern.sortNotes();
                });
            });
        menu.addSubMenu (tr ("Send to the piano roll of"), channels, ! song.channels.empty() && ! song.patterns.empty());
        menu.addItem (tr ("Save as MIDI..."), [this] { saveMidi(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scoreButton));
    }

    void saveMidi()
    {
        const juce::File src (juce::String::fromUTF8 (samplePath.c_str()));
        chooser = lynx::ui::makeFileChooser (tr ("Save the notes as MIDI"), src.getSiblingFile (src.getFileNameWithoutExtension() + ".mid"), "*.mid");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<NewtoneEditor> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  Song song;
                                  song.bpm = safe->engine.getSong().bpm;
                                  song.channels.resize (1);
                                  song.channels[0].name = "Newtone";
                                  song.patterns.resize (1);
                                  song.patterns[0].notes = safe->scoreNotes (0);
                                  juce::String error;
                                  if (! MidiFileIo::savePattern (song, 0, fc.getResult().withFileExtension ("mid"), error))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save"), error);
                              });
    }

    void showLengthMenu()
    {
        if (sample == nullptr)
            return;
        juce::PopupMenu menu;
        const auto bpm = engine.getSong().bpm;
        const auto seconds = (double) frames() / sample->sampleRate;
        // the whole sample stretched to a length (one marker at its end)
        auto setLength = [this] (double ratio) {
            warp.markers.clear();
            if (std::abs (ratio - 1.0) > 1e-6)
                warp.markers.push_back ({ frames() - 1, (std::int64_t) std::llround ((double) (frames() - 1) * ratio) });
            commit();
        };
        for (const auto ratio : { 0.5, 0.75, 1.0, 1.25, 1.5, 2.0 })
            menu.addItem (juce::String (ratio * 100.0, 0) + " %", [setLength, ratio] { setLength (ratio); });
        menu.addSeparator();
        // to whole bars of the song's tempo
        const auto bar = 4.0 * 60.0 / std::max (1.0, bpm);
        for (const auto bars : { 1, 2, 4, 8, 16 })
            menu.addItem (tr ("{0} bar(s) at {1} BPM", { juce::String (bars), juce::String (bpm, 2) }),
                          [setLength, ratio = bars * bar / std::max (1e-6, seconds)] { setLength (ratio); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stretch));
    }

    void showNoteMenu (int k)
    {
        juce::PopupMenu menu;
        auto& p = notes.pitches[(std::size_t) k];
        menu.addItem (tr ("Snap to the nearest note"), [this] {
            for (const auto q : selection())
                snapNote (q);
            commit();
        });
        menu.addItem (tr ("Reset this note"), [this] {
            for (const auto q : selection())
            {
                auto& n = notes.pitches[(std::size_t) q];
                n.shift = 0.0;
                n.curStart = n.origStart;
                n.curEnd = n.origEnd;
                n.enabled = true;
                n.volume = n.formant = n.drift = 1.0;
                n.vibPeriod = n.vibAmpStart = n.vibAmpEnd = n.tremAmp = n.rampIn = n.rampOut = 0.0;
                n.smoothIn = n.origSmoothIn;
                n.smoothOut = n.origSmoothOut;
            }
            commit();
        });
        menu.addItem (p.enabled ? tr ("Mute") : tr ("Unmute"), [this, on = ! p.enabled] {
            for (const auto q : selection())
                notes.pitches[(std::size_t) q].enabled = on;
            commit();
        });
        menu.addItem (tr ("Slice here"), ! warpMode(), false, [this, k, x = getMouseXYRelative().toFloat().x] { cutAt (k, x); });
        menu.addItem (tr ("Glue to the note before"), k > 0, false, [this, k] {
            if (newtonefx::glueNote (notes, (std::size_t) k))
            {
                selected = { k - 1 };
                commit();
            }
        });
        juce::PopupMenu volume;
        for (const auto db : { 6.0, 3.0, 0.0, -3.0, -6.0, -12.0 })
            volume.addItem ((db > 0 ? "+" : "") + juce::String (db, 0) + " dB", true, std::abs (20.0 * std::log10 (std::max (1e-6, p.volume)) - db) < 0.1, [this, db] {
                for (const auto q : selection())
                    notes.pitches[(std::size_t) q].volume = std::pow (10.0, db / 20.0);
                commit();
            });
        menu.addSubMenu (tr ("Volume"), volume);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void updateButtons()
    {
        const auto has = sample != nullptr && validSlot();
        analyse.setVisible (! warpMode());
        cutMode.setVisible (! warpMode());
        snapAll.setVisible (! warpMode());
        scoreButton.setVisible (! warpMode());
        stretch.setVisible (warpMode());
        for (auto* b : { &analyse, &cutMode, &snapAll, &reset, &preview, &save, &scoreButton, &stretch, &toPlaylist })
            b->setEnabled (has);
        load.setEnabled (validSlot());
        slave.setEnabled (validSlot());
        preview.setButtonText (previewing ? tr ("Stop") : tr ("Preview"));
        juce::String text;
        if (sample != nullptr)
        {
            const juce::File f (juce::String::fromUTF8 (samplePath.c_str()));
            text = f.getFileName() + "  ·  " + juce::String ((double) frames() / sample->sampleRate, 2) + " s  ·  ";
            text += warpMode() ? tr ("{0} warp markers. Double-click: add a marker, drag it to move that point, right-click: remove", { juce::String ((int) warp.markers.size()) })
                    : cutMode.getToggleState()
                        ? tr ("{0} notes. Cut mode: click a note to slice it, Ctrl+click to glue it to the one before (C: leave)", { juce::String ((int) notes.pitches.size()) })
                        : tr ("{0} notes. Drag: pitch (Alt: free), Ctrl+drag: time, Shift+click / drag on empty space: select, ruler: set pitch, right-click: menu",
                              { juce::String ((int) notes.pitches.size()) });
        }
        status.setText (text, juce::dontSendNotification);
        updatePanel();
        resized();
    }

    void timerCallback() override
    {
        if (previewing)
        {
            if (engine.getPreviewPosition() < 0.0 && ++silentTicks > 15) // the preview ended
            {
                previewing = false;
                stopTimer();
                updateButtons();
                repaint();
            }
            else if (engine.getPreviewPosition() >= 0.0)
            {
                silentTicks = 0;
                // FL's header meter: the preview's peak over the last 33 ms
                if (flLook() && previewSource != nullptr)
                {
                    const auto at = (std::int64_t) engine.getPreviewPosition();
                    const auto from = std::max<std::int64_t> (0, at - (std::int64_t) (0.033 * previewSource->sampleRate));
                    float peak = 0.0f;
                    for (auto i = from; i < std::min<std::int64_t> (at, (std::int64_t) previewSource->frames()); ++i)
                        peak = std::max ({ peak, std::abs (previewSource->leftData()[i]), previewSource->isStereo() ? std::abs (previewSource->rightData()[i]) : 0.0f });
                    previewLevel = std::max (peak * 0.8f, previewLevel * 0.82f);
                    repaint (headerArea());
                }
                // Auto-scroll (FL's A): the view pages along with the playback position
                if (flLook() && autoScroll)
                {
                    const auto pos = engine.getPreviewPosition() + previewOffset();
                    if (pos < scroll || pos > scroll + visibleLength() * 0.95)
                    {
                        scroll = pos - visibleLength() * 0.05;
                        clampScroll();
                        updateScrollBars();
                        repaint();
                    }
                }
            }
            repaint (flLook() ? flBody() : canvasArea());
        }
        else
            stopTimer();
    }
    int silentTicks = 0;

    // -------------------------------------------------------------------------------------------------- geometry
    juce::Rectangle<int> contentArea() const
    {
        return getLocalBounds().reduced (8).withTrimmedTop (warpMode() ? 30 : 58).withTrimmedBottom (22);
    }
    juce::Rectangle<int> panelArea() const
    {
        if (flLook())
            return flPanel();
        auto r = contentArea();
        return warpMode() ? juce::Rectangle<int>() : r.removeFromRight (250);
    }
    juce::Rectangle<int> rulerArea() const
    {
        if (flLook())
            return flKeyboard();
        auto r = contentArea();
        if (warpMode())
            return {};
        r.removeFromRight (256);
        return r.removeFromLeft (36);
    }
    juce::Rectangle<int> canvasArea() const
    {
        if (flLook())
            return flCanvas();
        auto r = contentArea();
        if (! warpMode())
        {
            r.removeFromRight (256);
            r.removeFromLeft (38);
        }
        return r;
    }

    double totalLength() const
    {
        if (warpMode())
            return (double) std::max<std::int64_t> (1, std::max (newtonefx::warpedLength (warp, frames()), frames()));
        std::int64_t end = frames();
        for (const auto& p : notes.pitches)
            end = std::max (end, p.curEnd);
        return (double) std::max<std::int64_t> (1, end);
    }
    double visibleLength() const { return totalLength() / zoom; }
    double pixelsPerSample() const { return (double) canvasArea().getWidth() / visibleLength(); }
    void clampScroll() { scroll = std::clamp (scroll, 0.0, std::max (0.0, totalLength() - visibleLength())); }
    float xOfOutput (double s) const { return (float) canvasArea().getX() + (float) ((s - scroll) * pixelsPerSample()); }
    double outputOfX (float x) const { return scroll + (x - (float) canvasArea().getX()) / pixelsPerSample(); }

    std::pair<double, double> pitchRange() const
    {
        double lo = 1e9, hi = -1e9;
        for (const auto& p : notes.pitches)
            if (p.origPitch > 0.0)
            {
                lo = std::min (lo, p.origPitch + p.shift);
                hi = std::max (hi, p.origPitch + p.shift);
            }
        if (lo > hi)
            lo = 55.0, hi = 70.0;
        return { std::floor (lo) - 4.0, std::ceil (hi) + 4.0 };
    }
    double rowHeight() const
    {
        if (flLook())
            return rowH;
        const auto [lo, hi] = pitchRange();
        return (double) canvasArea().getHeight() / std::max (1.0, hi - lo);
    }
    float yOfPitch (double midi) const
    {
        if (flLook())
            return (float) flCanvas().getY() + (float) ((topPitch - midi) * rowH);
        const auto [lo, hi] = pitchRange();
        const auto area = canvasArea();
        return (float) area.getBottom() - (float) ((midi - lo) * (double) area.getHeight() / std::max (1.0, hi - lo));
    }
    double pitchOfY (float y) const
    {
        if (flLook())
            return topPitch - (double) (y - (float) flCanvas().getY()) / rowH;
        const auto [lo, hi] = pitchRange();
        const auto area = canvasArea();
        return lo + (double) ((float) area.getBottom() - y) * std::max (1.0, hi - lo) / (double) std::max (1, area.getHeight());
    }

    juce::Rectangle<float> noteBounds (int k) const
    {
        const auto& p = notes.pitches[(std::size_t) k];
        const auto h = (float) std::max (6.0, rowHeight() * (flLook() ? 1.0 : 0.8));
        const auto x0 = xOfOutput ((double) p.curStart), x1 = xOfOutput ((double) p.curEnd);
        return { x0, yOfPitch (p.origPitch + p.shift) - 0.5f * h, std::max (2.0f, x1 - x0), h };
    }

    int noteAt (juce::Point<float> pos) const
    {
        const auto h = (float) rowHeight();
        for (int k = (int) notes.pitches.size() - 1; k >= 0; --k)
        {
            const auto& p = notes.pitches[(std::size_t) k];
            if (p.origPitch <= 0.0 || ! p.voiced())
                continue;
            const auto y = yOfPitch (p.origPitch + p.shift);
            if (pos.x >= xOfOutput ((double) p.curStart) && pos.x <= xOfOutput ((double) p.curEnd) && std::abs (pos.y - y) <= std::max (5.0f, 0.5f * h))
                return k;
        }
        return -1;
    }

    int markerAt (float x) const
    {
        for (int k = 0; k < (int) warp.markers.size(); ++k)
            if (std::abs (xOfOutput ((double) warp.markers[(std::size_t) k].output) - x) <= 5.0f)
                return k;
        return -1;
    }

    // -------------------------------------------------------------------------------------------------- painting
    void paintWave (juce::Graphics& g, juce::Rectangle<int> area, std::function<double (double)> sourceOfOutput, juce::Colour colour) const
    {
        if (peaks.empty())
            return;
        g.setColour (colour);
        const auto mid = (float) area.getCentreY(), half = 0.48f * (float) area.getHeight();
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto a = sourceOfOutput (outputOfX ((float) x)), b = sourceOfOutput (outputOfX ((float) x + 1.0f));
            if (b < 0.0 || a >= (double) frames())
                continue;
            float m = 0.0f;
            for (auto i = (std::int64_t) std::max (0.0, std::min (a, b)) / 256; i <= (std::int64_t) std::max (a, b) / 256 && i < (std::int64_t) peaks.size(); ++i)
                m = std::max (m, peaks[(std::size_t) i]);
            g.drawVerticalLine (x, mid - m * half, mid + m * half);
        }
    }

    // the keyboard ruler: click a key to move the selected notes to it
    void paintRuler (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        const auto [lo, hi] = pitchRange();
        g.setFont (theme::font (theme::type::caption));
        for (auto m = (int) lo; m < (int) hi; ++m)
        {
            const auto y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
            const auto cls = ((m % 12) + 12) % 12;
            const auto black = juce::String ("010100101010").substring (cls, cls + 1) == "1";
            const auto inScale = (scaleMask() >> (((m - scaleKey()) % 12 + 12) % 12) & 1u) != 0;
            g.setColour (black ? theme::colour::bg3 : theme::colour::textSecondary.withAlpha (inScale ? 0.85f : 0.4f));
            g.fillRect ((float) area.getX(), y0 + 0.5f, (float) area.getWidth() * (black ? 0.62f : 1.0f), y1 - y0 - 1.0f);
            if (cls == 0 && y1 - y0 >= 8.0f)
            {
                g.setColour (theme::colour::bg0);
                g.drawText (noteName (m), area.getX(), (int) y0, area.getWidth() - 2, (int) (y1 - y0), juce::Justification::centredRight);
            }
        }
    }

    void paintNotes (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        const auto [lo, hi] = pitchRange();
        // semitone rows (black keys darker; notes outside the scale dimmer) with C labels
        g.setFont (theme::font (theme::type::caption));
        for (auto m = (int) lo; m < (int) hi; ++m)
        {
            const auto y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
            const auto black = juce::String ("010100101010").substring (((m % 12) + 12) % 12, ((m % 12) + 12) % 12 + 1) == "1";
            const auto inScale = (scaleMask() >> (((m - scaleKey()) % 12 + 12) % 12) & 1u) != 0;
            g.setColour ((black ? theme::colour::bg0.darker (0.2f) : theme::colour::bg0.brighter (0.03f)).darker (inScale ? 0.0f : 0.25f));
            g.fillRect ((float) area.getX(), y0, (float) area.getWidth(), y1 - y0);
        }
        // the waveform, faint, at the bottom
        paintWave (g, area.withTrimmedTop (area.getHeight() * 3 / 4), [] (double s) { return s; }, theme::colour::textDisabled.withAlpha (0.35f));

        const auto h = (float) std::max (6.0, rowHeight() * 0.8);
        for (int k = 0; k < (int) notes.pitches.size(); ++k)
        {
            const auto& p = notes.pitches[(std::size_t) k];
            if (p.origPitch <= 0.0)
                continue; // (FL's unvoiced stretches between the notes)
            const auto x0 = xOfOutput ((double) p.curStart), x1 = xOfOutput ((double) p.curEnd);
            if (x1 < (float) area.getX() || x0 > (float) area.getRight())
                continue;
            const auto target = p.origPitch + p.shift;
            const auto y = yOfPitch (target);
            const auto edited = p.edited (notes.globalFormant);
            const auto isSelected = selected.count (k) > 0;
            auto colour = ! p.enabled ? theme::colour::bg5 : edited ? theme::colour::accent : theme::colour::info;
            g.setColour (colour.withAlpha (isSelected ? 0.75f : 0.45f));
            g.fillRoundedRectangle (x0, y - 0.5f * h, std::max (2.0f, x1 - x0), h, 3.0f);
            if (isSelected)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawRoundedRectangle (x0, y - 0.5f * h, std::max (2.0f, x1 - x0), h, 3.0f, 1.2f);
            }
            // the sung pitch inside the note, moved with it
            if (! pitchTrack.empty())
            {
                juce::Path curve;
                bool started = false;
                for (auto f = std::max<std::int64_t> (0, p.origStart / pitchHop); f < std::min<std::int64_t> ((std::int64_t) pitchTrack.size(), p.origEnd / pitchHop + 1); ++f)
                {
                    const auto v = pitchTrack[(std::size_t) f];
                    if (v < 0.0 || std::abs (v - p.origPitch) > 1.5 || f * pitchHop < p.origStart || f * pitchHop >= p.origEnd) // (the note's own pitch only)
                    {
                        started = false;
                        continue;
                    }
                    const auto px = xOfOutput ((double) p.curStart + (double) (f * pitchHop - p.origStart) / std::max (1e-6, p.stretch));
                    const auto py = yOfPitch (p.origPitch + p.shift + p.drift * (v - p.origPitch));
                    if (! started)
                        curve.startNewSubPath (px, py);
                    else
                        curve.lineTo (px, py);
                    started = true;
                }
                g.setColour (theme::colour::textPrimary.withAlpha (p.enabled ? 0.8f : 0.3f));
                g.strokePath (curve, juce::PathStrokeType (1.2f));
            }
            if (x1 - x0 > 34.0f)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawText (noteName (target) + (std::abs (p.shift) > 0.01 ? juce::String::formatted (" %+.1f", p.shift) : juce::String()),
                            juce::Rectangle<float> (x0 + 3.0f, y - 0.5f * h - 14.0f, std::max (40.0f, x1 - x0), 13.0f), juce::Justification::left);
            }
        }
    }

    // ============================================================================================================
    // Round 190kj: FL's Newtone window. The header (transport, slave / auto-scroll, name, title, the File / Edit /
    // Select / Undo menus, Create score / Save as / Drag selection / Send selection to Playlist, Length / Sel, Tempo +
    // Sync, the Center / Variation / Trans knobs, Cut mode and Advanced Edit) and the note view (keyboard, bar ruler,
    // grid rows by scale, notes as FL draws them) at FL's measured sizes. Newtime keeps its own layout.
    enum class Hit
    {
        none, loop, play, stop, slave, follow, fileMenu, editMenu, selectMenu, undo, score, saveAs, dragOut, toPlaylist, sync,
        cut, advanced, knobCentre, knobVariation, knobTrans
    };
    bool flLook() const noexcept { return ! warpMode() && ! oldLook; }
    bool oldLook = false; // (--newtoneshot "lynxlook": the round-165g layout, for the paint before / after only)
public:
    juce::PopupMenu editMenuForSnapshot() { return editMenu(); } // (round 190li: --newtoneshot190li)
    void shotState (const juce::String& state)
    {
        const auto arg = state.fromFirstOccurrenceOf (":", false, false);
        if (state.startsWith ("select:"))
        {
            selected.clear();
            for (const auto& t : juce::StringArray::fromTokens (arg, ",", ""))
                selected.insert (t.getIntValue());
            selectionChanged();
        }
        else if (state == "advanced")
        {
            advancedEdit = true;
            currentNote = selected.empty() ? 1 : *selected.begin();
            selected.clear();
            rowH = 33.0;
            zoom = 1.35;
            if (currentNote < (int) notes.pitches.size())
                scroll = std::max (0.0, (double) notes.pitches[(std::size_t) currentNote].curStart - 0.35 * visibleLength());
            clampScroll();
        }
        else if (state == "vibrato")
            vibratoEdit = true;
        else if (state.startsWith ("cursor:"))
            cursorAt = arg.getLargeIntValue();
        else if (state == "cut")
            cutMode.setToggleState (true, juce::dontSendNotification);
        else if (state.startsWith ("timesel:"))
            timeSelA = arg.upToFirstOccurrenceOf (",", false, false).getLargeIntValue(), timeSelB = arg.fromFirstOccurrenceOf (",", false, false).getLargeIntValue();
        else if (state.startsWith ("zoom:"))
            zoom = std::max (1.0, arg.getDoubleValue());
        else if (state.startsWith ("shift:"))
        {
            const auto k = arg.upToFirstOccurrenceOf (",", false, false).getIntValue();
            if (k >= 0 && k < (int) notes.pitches.size())
                notes.pitches[(std::size_t) k].shift = arg.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        }
        else if (state == "lynxlook")
        {
            oldLook = true;
            for (auto* b : { &load, &analyse, &cutMode, &snapAll, &reset, &preview, &save, &scoreButton, &snapButton, &toPlaylist })
                b->setVisible (true);
            slave.setVisible (true);
            status.setVisible (true);
            hScroll.setVisible (false);
            vScroll.setVisible (false);
            setSize (1040, 520);
            updateButtons();
        }
        if (flLook() && ! (state == "advanced"))
            centreRowsOnNotes();
        else if (flLook() && currentNote >= 0 && currentNote < (int) notes.pitches.size())
            topPitch = notes.pitches[(std::size_t) currentNote].origPitch + 0.5 * (double) flCanvas().getHeight() / rowH;
        resized();
        repaint();
    }
private:
    bool looping = false, autoScroll = true, advancedEdit = false, snapScale = false; // (FL: Auto-scroll on at start)
    // Edit › Advanced edit vibrato: LYNX shows its note panel (the selected notes' vibrato, tremolo, formant... as
    // numbers) beside the roll; FL draws its vibrato handles on the note (not reproduced)
    bool vibratoEdit = false;
    std::int64_t cursorAt = 0;  // FL's play cursor (a click on the roll puts it there; playback starts from it)
    int currentNote = -1;       // the note Advanced edit shows its handles on (the one double-clicked)
    double rowH = flnt::defaultRow, topPitch = 74.0; // the note view's row height and the pitch at its top edge
    std::int64_t timeSelA = -1, timeSelB = -1;     // FL's time selection (output samples), -1 = none
    Hit pressedHit = Hit::none, hoverHit = Hit::none;
    double knobFrom = 0.0;
    bool rulerSelecting = false;
    juce::ScrollBar hScroll { false }, vScroll { true };

    bool hasTimeSelection() const noexcept { return timeSelA >= 0 && timeSelB > timeSelA; }
    juce::Rectangle<int> headerArea() const { return getLocalBounds().removeFromTop (flnt::headerHeight); }
    juce::Rectangle<int> flBody() const { return getLocalBounds().withTrimmedTop (flnt::headerHeight); }
    juce::Rectangle<int> flKeyboard() const
    {
        return flBody().withTrimmedTop (flnt::rulerHeight).withTrimmedBottom (flnt::scrollSize).removeFromLeft (flnt::keyboardWidth);
    }
    juce::Rectangle<int> timeRulerArea() const
    {
        auto r = flBody().removeFromTop (flnt::rulerHeight).withTrimmedLeft (flnt::keyboardWidth).withTrimmedRight (flnt::scrollSize);
        if (vibratoEdit)
            r.removeFromRight (252);
        return r;
    }
    juce::Rectangle<int> flCanvas() const
    {
        auto r = flBody().withTrimmedTop (flnt::rulerHeight).withTrimmedLeft (flnt::keyboardWidth).withTrimmedRight (flnt::scrollSize)
                     .withTrimmedBottom (flnt::scrollSize);
        if (vibratoEdit)
            r.removeFromRight (252);
        return r;
    }
    juce::Rectangle<int> flPanel() const
    {
        if (! vibratoEdit)
            return {};
        return flBody().withTrimmedTop (flnt::rulerHeight).withTrimmedRight (flnt::scrollSize).withTrimmedBottom (flnt::scrollSize)
            .removeFromRight (252);
    }

    // header hit boxes, FL's positions at its 735 px window; the right-hand groups keep their distance to the right edge
    juce::Rectangle<float> hitBox (Hit h) const
    {
        // FL's control rectangles; the right-anchored ones (akRight) keep their distance to the right edge
        const auto shift = (float) (getWidth() - flnt::formWidth);
        auto at = [&] (float x, float y, float w, float hh, bool right) { return juce::Rectangle<float> (x + (right ? shift : 0.0f), y, w, hh); };
        switch (h)
        {
            case Hit::loop: return at (5, 11, 29, 28, false);
            case Hit::play: return at (34, 11, 29, 28, false);
            case Hit::stop: return at (63, 11, 29, 28, false);
            case Hit::slave: return at (5, 41, 35, 17, false);
            case Hit::follow: return at (43, 41, 35, 17, false);
            case Hit::fileMenu: return at (326, 11, 26, 29, true);
            case Hit::editMenu: return at (350, 11, 26, 29, true);
            case Hit::selectMenu: return at (374, 11, 26, 29, true);
            case Hit::undo: return at (398, 11, 26, 29, true);
            case Hit::score: return at (437, 11, 26, 29, true);
            case Hit::saveAs: return at (461, 11, 26, 29, true);
            case Hit::dragOut: return at (485, 11, 26, 29, true);
            case Hit::toPlaylist: return at (509, 11, 26, 29, true);
            case Hit::sync: return at (514, 60, 31, 12, true);
            case Hit::cut: return at (600, 64, 31, 18, true);
            case Hit::advanced: return at (633, 64, 31, 18, true);
            case Hit::knobCentre: return at (566, 9, 40, 40, true);
            case Hit::knobVariation: return at (612, 9, 40, 40, true);
            case Hit::knobTrans: return at (658, 9, 40, 40, true);
            case Hit::none: break;
        }
        return {};
    }
    Hit hitAt (juce::Point<float> p) const
    {
        if (! headerArea().toFloat().contains (p))
            return Hit::none;
        for (int h = (int) Hit::loop; h <= (int) Hit::knobTrans; ++h)
            if (hitBox ((Hit) h).contains (p))
                return (Hit) h;
        return Hit::none;
    }
    juce::String hitTip (Hit h) const
    {
        switch (h) // (FL's hints, TSynthEditorForm)
        {
            case Hit::loop: return tr ("Loop") + " (L)";
            case Hit::play: return tr ("Play / pause") + " (" + tr ("Space") + ")";
            case Hit::stop: return tr ("Stop") + " (" + tr ("Space") + ")";
            case Hit::slave: return tr ("Slave playback to host (H)");
            case Hit::follow: return tr ("Scroll to follow cursor") + " (A)";
            case Hit::fileMenu: return tr ("File");
            case Hit::editMenu: return tr ("Edit");
            case Hit::selectMenu: return tr ("Select");
            case Hit::undo: return tr ("Undo / Redo");
            case Hit::score: return tr ("Send to pianoroll / Copy to MIDI clipboard");
            case Hit::saveAs: return tr ("Save as...");
            case Hit::dragOut: return tr ("Drag selection");
            case Hit::toPlaylist: return tr ("Send to playlist");
            case Hit::sync: return tr ("Tempo-sync");
            case Hit::cut: return tr ("Cut mode") + " (C)";
            case Hit::advanced: return tr ("Advanced edit") + " (Ctrl+E)";
            case Hit::knobCentre: return tr ("Center");
            case Hit::knobVariation: return tr ("Variation");
            case Hit::knobTrans: return tr ("Transition");
            case Hit::none: break;
        }
        return {};
    }

    // ---------------------------------------------------------------------------------------------- header paint
    static void flButtonFace (juce::Graphics& g, juce::Rectangle<float> r, bool down, bool hover)
    {
        g.setColour (flnt::buttonEdge);
        g.fillRoundedRectangle (r, 2.0f);
        auto face = r.reduced (1.0f);
        juce::ColourGradient grad (flnt::buttonFace.brighter (hover ? 0.18f : 0.08f), face.getX(), face.getY(), flnt::buttonFace.darker (down ? 0.3f : 0.1f),
                                   face.getX(), face.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (face, 1.5f);
    }
    static void flDropArrow (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::Path tri;
        const auto cx = r.getCentreX(), y = r.getBottom() - 6.0f;
        tri.addTriangle (cx - 3.5f, y, cx + 3.5f, y, cx, y + 3.5f);
        g.setColour (flnt::caption);
        g.fillPath (tri);
    }
    void flIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> r, float size, bool menu, juce::Colour ink = flnt::iconInk) const
    {
        auto box = juce::Rectangle<float> (size, size).withCentre ({ r.getCentreX(), r.getY() + (menu ? 11.5f : r.getHeight() * 0.5f) });
        icons::draw (g, icon, box, ink);
        if (menu)
            flDropArrow (g, r);
    }
    void flKnob (juce::Graphics& g, Hit h, double value01, juce::Colour arc, const juce::String& label, float labelX, float labelW) const
    {
        const auto box = hitBox (h);
        const auto c = box.getCentre();
        const auto r = 17.5f;
        juce::Path rail;
        rail.addCentredArc (c.x, c.y, r, r, 0.0f, flnt::knobStart, flnt::knobStart + flnt::knobSweep, true);
        g.setColour (flnt::rail);
        g.strokePath (rail, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        const auto angle = flnt::knobStart + flnt::knobSweep * (float) std::clamp (value01, 0.0, 1.0);
        if (value01 > 0.001)
        {
            juce::Path a;
            a.addCentredArc (c.x, c.y, r, r, 0.0f, flnt::knobStart, angle, true);
            g.setColour (arc);
            g.strokePath (a, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
        const auto capBox = juce::Rectangle<float> (25.0f, 25.0f).withCentre (c);
        juce::ColourGradient grad (flnt::cap.brighter (0.45f), capBox.getX() + 5.0f, capBox.getY() + 3.0f, flnt::cap.darker (0.55f), capBox.getRight(), capBox.getBottom(), true);
        g.setGradientFill (grad);
        g.fillEllipse (capBox);
        g.setColour (juce::Colour (0xff3a4247));
        g.drawEllipse (capBox, 1.0f);
        const juce::Point<float> tip (c.x + 11.0f * std::sin (angle), c.y - 11.0f * std::cos (angle));
        g.setColour (juce::Colour (0xff20272b));
        g.drawLine ({ c, tip }, 2.2f);
        g.setColour (flnt::knobLabel);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)).withHorizontalScale (0.86f));
        g.drawText (label, juce::Rectangle<float> (labelX + (float) (getWidth() - flnt::formWidth), 50.0f, labelW, 11.0f), juce::Justification::centred, false);
    }
    void paintFlHeader (juce::Graphics& g) const
    {
        const auto area = headerArea().toFloat();
        // (FL's toolbar as drawn: #191f24 at the top to #161c20 at the bottom, measured on the 2026 capture)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff191f24), 0.0f, area.getY(), juce::Colour (0xff161c20), 0.0f, area.getBottom(), false));
        g.fillRect (area);
        g.setColour (flnt::buttonEdge);
        g.drawHorizontalLine ((int) area.getBottom() - 1, area.getX(), area.getRight());
        const auto shift = (float) (getWidth() - flnt::formWidth);

        // transport (FL's FS_Sober buttons, a rounded border)
        auto transport = [&] (Hit h, Icon icon, float size, juce::Colour ink) {
            const auto r = hitBox (h);
            g.setColour (flnt::buttonEdge);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (flnt::transportFace.brighter (pressedHit == h ? -0.2f : hoverHit == h ? 0.15f : 0.0f));
            g.fillRoundedRectangle (r.reduced (1.0f), 2.5f);
            flIcon (g, icon, r, size, false, ink);
        };
        transport (Hit::loop, Icon::loop, 16.0f, looping ? flnt::slaveOn : flnt::iconInk);
        transport (Hit::play, Icon::play, 12.0f, previewing ? flnt::green : flnt::iconInk);
        transport (Hit::stop, Icon::stop, 10.0f, flnt::iconInk);
        // FL's pads (FS_Pad): lit in their own colour when on
        auto pad = [&] (Hit h, Icon icon, bool on, juce::Colour lit) {
            const auto r = hitBox (h);
            g.setColour (flnt::buttonEdge);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (on ? lit : flnt::padOff.brighter (hoverHit == h ? 0.15f : 0.0f));
            g.fillRoundedRectangle (r.reduced (1.0f), 2.5f);
            flIcon (g, icon, r, 12.0f, false, on ? juce::Colour (0xff1b1b1b) : juce::Colour (0xffc0c0c0));
        };
        pad (Hit::slave, Icon::link, slave.getToggleState(), flnt::slaveOn);
        pad (Hit::follow, Icon::follow, autoScroll, flnt::followOn);
        pad (Hit::cut, Icon::knife, cutMode.getToggleState(), flnt::cutOn);
        pad (Hit::advanced, Icon::envelope, advancedEdit, flnt::advancedOn);

        // the plugin's name where FL draws its logo (LYNX writes the name; FL's artwork is not used)
        g.setColour (juce::Colour (0xff8796a0));
        g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
        g.drawText ("Newtone", juce::Rectangle<float> (107.0f, 12.0f, 150.0f, 30.0f), juce::Justification::centredLeft, false);

        // the menu buttons and the actions (FS_ChannelPulse, a down arrow on the menus)
        auto menuButton = [&] (Hit h, Icon icon, bool arrow) {
            const auto r = hitBox (h);
            g.setColour (flnt::buttonEdge);
            g.fillRect (r);
            g.setColour (flnt::buttonFace.brighter (pressedHit == h ? -0.2f : hoverHit == h ? 0.18f : 0.0f));
            g.fillRect (r.reduced (1.0f));
            flIcon (g, icon, r, 15.0f, arrow);
        };
        menuButton (Hit::fileMenu, Icon::save, true);
        menuButton (Hit::editMenu, Icon::scissors, true);
        menuButton (Hit::selectMenu, Icon::select, true);
        menuButton (Hit::undo, Icon::undo, false);
        menuButton (Hit::score, Icon::note, false);
        menuButton (Hit::saveAs, Icon::save, false);
        menuButton (Hit::dragOut, Icon::audioFile, false);
        menuButton (Hit::toPlaylist, Icon::playlist, false);

        // captions (Segoe 8, dark grey) and values
        const auto captionFont = juce::Font (juce::FontOptions (9.5f)).withHorizontalScale (0.95f);
        g.setFont (captionFont);
        g.setColour (flnt::caption);
        g.drawText (tr ("TITLE"), juce::Rectangle<float> (5.0f, 61.0f, 60.0f, 9.0f), juce::Justification::centredLeft, false);
        g.drawText (tr ("LENGTH / SEL"), juce::Rectangle<float> (400.0f + shift, 61.0f, 72.0f, 9.0f), juce::Justification::centred, false);
        g.drawText (tr ("TEMPO"), juce::Rectangle<float> (477.0f + shift, 61.0f, 37.0f, 9.0f), juce::Justification::centred, false);
        g.setColour (flnt::nameInk);
        g.setFont (juce::FontOptions (12.0f));
        const juce::File f (juce::String::fromUTF8 (samplePath.c_str()));
        g.drawText (sample != nullptr ? f.getFileName() : juce::String(), juce::Rectangle<float> (6.0f, 70.0f, 301.0f + shift, 14.0f), juce::Justification::centredLeft, true);
        g.setColour (flnt::valueInk);
        const auto lengthText = sample == nullptr ? juce::String()
                                : hasTimeSelection() ? juce::String (timeSelB - timeSelA)
                                                     : juce::String (frames());
        g.drawText (lengthText, juce::Rectangle<float> (362.0f + shift, 70.0f, 104.0f, 15.0f), juce::Justification::centredRight, false);
        g.drawText (sampleTempo > 0.0 ? juce::String (sampleTempo, 3) + "BPM" : juce::String ("-"), juce::Rectangle<float> (480.0f + shift, 70.0f, 70.0f, 15.0f),
                    juce::Justification::centredLeft, false);
        {
            const auto r = hitBox (Hit::sync);
            g.setColour (tempoSync ? flnt::slaveOn : juce::Colour (0xff11171a));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (tempoSync ? juce::Colour (0xff1b1b1b) : flnt::valueInk.withAlpha (sampleTempo > 0.0 ? 0.9f : 0.45f));
            g.setFont (juce::FontOptions (7.5f, juce::Font::bold));
            g.drawText ("SYNC", r, juce::Justification::centred, false);
        }

        // the knobs: CENTER 0..100 %, VARIATION 0..200 % (100 % at the top), TRANS 0..100 % (FL's Max / default values)
        flKnob (g, Hit::knobCentre, notes.centre, flnt::centreArc, tr ("CENTER"), 566.0f, 40.0f);
        flKnob (g, Hit::knobVariation, notes.variation / 2.0, flnt::variationArc, tr ("VARIATION"), 607.0f, 50.0f);
        {
            // (FL marks a knob's non-zero default with a dot outside the ring: Variation's 100 %)
            const auto c = hitBox (Hit::knobVariation).getCentre();
            const auto a = flnt::knobStart + flnt::knobSweep * 0.5f;
            g.setColour (flnt::variationArc);
            g.fillEllipse (juce::Rectangle<float> (2.5f, 2.5f).withCentre ({ c.x + 22.5f * std::sin (a), c.y - 22.5f * std::cos (a) }));
        }
        flKnob (g, Hit::knobTrans, std::min (1.0, notes.transition), flnt::transArc, tr ("TRANS"), 658.0f, 40.0f);

        // FL's level meter at the right edge (the preview's level while it plays)
        const auto meter = juce::Rectangle<float> (710.0f + shift, 2.0f, 14.0f, 84.0f);
        g.setColour (juce::Colour (0xff0d1114));
        g.fillRect (meter);
        if (previewing && previewLevel > 0.0f)
        {
            const auto h = meter.getHeight() * std::min (1.0f, previewLevel);
            g.setColour (previewLevel >= 1.0f ? juce::Colour (0xffe03c3c) : flnt::green);
            g.fillRect (meter.reduced (3.0f, 0.0f).withTop (meter.getBottom() - h));
        }
    }

    // --------------------------------------------------------------------------------------------- note view paint
    void paintFlKeyboard (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        g.setColour (flnt::keyWhite);
        g.fillRect (area);
        const auto lo = (int) std::floor (pitchOfY ((float) area.getBottom())) - 1, hi = (int) std::ceil (pitchOfY ((float) area.getY())) + 1;
        // white keys: FL's keyboard splits the octave into 7 equal keys (the black keys sit on their own rows)
        g.setFont (juce::FontOptions (11.0f));
        for (auto oct = (int) std::floor (lo / 12.0) * 12; oct <= hi; oct += 12)
        {
            const auto yTop = yOfPitch (oct + 11.5), yBottom = yOfPitch (oct - 0.5);
            const auto keyH = (yBottom - yTop) / 7.0f;
            for (int w = 0; w < 7; ++w)
            {
                const auto y = yBottom - keyH * (float) (w + 1);
                g.setColour (flnt::keyLine);
                g.drawHorizontalLine ((int) std::round (y), (float) area.getX(), (float) area.getRight());
                if (w == 0)
                {
                    g.setColour (flnt::keyC);
                    g.fillRect (juce::Rectangle<float> ((float) area.getX() + 26.0f, y + 1.0f, (float) area.getWidth() - 26.0f, keyH - 1.0f));
                    g.setColour (juce::Colour (0xff3a3d44));
                    g.drawText (noteName (oct), juce::Rectangle<float> ((float) area.getX(), y, (float) area.getWidth() - 3.0f, keyH), juce::Justification::bottomRight, false);
                }
            }
            for (const auto b : { 1, 3, 6, 8, 10 })
            {
                const auto y0 = yOfPitch (oct + b + 0.5), y1 = yOfPitch (oct + b - 0.5);
                g.setColour (flnt::keyBlack);
                g.fillRect (juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth() * 0.6f, y1 - y0));
            }
        }
        g.setColour (flnt::buttonEdge);
        g.drawVerticalLine (area.getRight() - 1, (float) area.getY(), (float) area.getBottom());
    }

    double beatSamples() const
    {
        const auto bpm = std::max (1.0, engine.getSong().bpm);
        return (sample != nullptr ? sample->sampleRate : 44100.0) * 60.0 / bpm;
    }

    void paintFlRuler (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        g.setColour (flnt::rulerBg);
        g.fillRect (area);
        g.setColour (flnt::rulerLine);
        g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
        if (hasTimeSelection())
        {
            g.setColour (flnt::orange.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOfOutput ((double) timeSelA), (float) area.getY(), xOfOutput ((double) timeSelB), (float) area.getBottom()));
        }
        const auto bar = 4.0 * beatSamples();
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (flnt::rulerInk);
        const auto first = std::max (0.0, std::floor (outputOfX ((float) area.getX()) / bar));
        for (auto b = first; b * bar < outputOfX ((float) area.getRight()); b += 1.0)
        {
            const auto x = xOfOutput (b * bar);
            if (xOfOutput (bar) - xOfOutput (0.0) < 18.0f && std::fmod (b, 4.0) != 0.0)
                continue;
            g.drawText (juce::String ((int) b + 1), juce::Rectangle<float> (x + 2.0f, (float) area.getY() + 1.0f, 40.0f, (float) area.getHeight() - 2.0f),
                        juce::Justification::centredLeft, false);
        }
        // FL's play cursor marker: an orange triangle pointing down
        juce::Path flag;
        const auto xc = xOfOutput ((double) cursorAt);
        flag.addTriangle (xc - 7.0f, (float) area.getY() + 2.0f, xc + 7.0f, (float) area.getY() + 2.0f, xc, (float) area.getBottom() - 3.0f);
        g.setColour (juce::Colour (0xffffc04a));
        g.fillPath (flag);
    }

    // a note's voiced part in the output (where its blob is drawn)
    std::pair<double, double> voicedSpan (const newtonefx::Note& p) const
    {
        const auto len = (double) (p.voicedEnd() - p.origStart) / std::max (1e-6, p.stretch);
        return { (double) p.curStart, std::min ((double) p.curEnd, (double) p.curStart + len) };
    }

    void paintFlNotes (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (area);
        // rows: in the scale lighter, out of it darker (FL's grid), a line between rows
        const auto lo = (int) std::floor (pitchOfY ((float) area.getBottom())) - 1, hi = (int) std::ceil (pitchOfY ((float) area.getY())) + 1;
        for (auto m = lo; m <= hi; ++m)
        {
            const auto y0 = yOfPitch (m + 0.5), y1 = yOfPitch (m - 0.5);
            const auto inScale = (scaleMask() >> (((m - scaleKey()) % 12 + 12) % 12) & 1u) != 0;
            g.setColour (inScale ? flnt::rowIn : flnt::rowOut);
            g.fillRect (juce::Rectangle<float> ((float) area.getX(), y0, (float) area.getWidth(), y1 - y0));
            g.setColour (flnt::rowLine);
            g.drawHorizontalLine ((int) std::round (y1) - 1, (float) area.getX(), (float) area.getRight());
        }
        // time grid: steps (1/4 beat), beats, bars
        const auto beat = beatSamples();
        const auto step = beat / 4.0;
        if (xOfOutput (step) - xOfOutput (0.0) >= 6.0f)
        {
            const auto first = std::max (0.0, std::floor (outputOfX ((float) area.getX()) / step));
            for (auto k = first; k * step < outputOfX ((float) area.getRight()); k += 1.0)
            {
                const auto x = (int) std::round (xOfOutput (k * step));
                const auto kk = (long long) k;
                g.setColour (kk % 16 == 0 ? flnt::barLine : kk % 4 == 0 ? flnt::beatLine : flnt::stepLine);
                g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
            }
        }
        if (hasTimeSelection())
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOfOutput ((double) timeSelA), (float) area.getY(), xOfOutput ((double) timeSelB), (float) area.getBottom()));
        }

        float peakAll = 1e-6f;
        for (const auto v : peaks)
            peakAll = std::max (peakAll, v);
        juce::Path pitchLine;
        bool lineOpen = false;
        for (int k = 0; k < (int) notes.pitches.size(); ++k)
        {
            const auto& p = notes.pitches[(std::size_t) k];
            if (p.origPitch <= 0.0 || ! p.voiced())
            {
                lineOpen = false;
                continue; // (the lead-in before the first note and FL's unvoiced notes: nothing drawn)
            }
            const auto x0 = xOfOutput ((double) p.curStart), x1 = xOfOutput ((double) p.curEnd);
            if (x1 < (float) area.getX() - 200.0f || x0 > (float) area.getRight() + 200.0f)
            {
                lineOpen = false;
                continue;
            }
            const auto target = p.origPitch + p.shift;
            const auto y = yOfPitch (target);
            const auto isSelected = selected.count (k) > 0;
            const auto alpha = p.enabled ? 1.0f : 0.35f;
            // the blob: the note's level, as FL draws its volume around the note
            const auto [v0, v1] = voicedSpan (p);
            const auto bx0 = xOfOutput (v0), bx1 = xOfOutput (v1);
            juce::Path blob;
            std::vector<juce::Point<float>> lower;
            // FL's blob follows the note's level envelope (smoothed, ~2 rows tall when loud, tails tapering; fitted on the
            // r5.wav capture: a square-root scale over the file's loudest peak, a ±30 ms envelope)
            const auto half = (float) rowH * 1.25f;
            const auto gain = (float) std::min (2.0, p.volume);
            const auto reach = (std::int64_t) std::max (1.0, 0.03 * (sample != nullptr ? sample->sampleRate : 44100.0) / 256.0);
            for (auto x = std::max (bx0, (float) area.getX() - 2.0f); x <= std::min (bx1, (float) area.getRight() + 2.0f); x += 1.0f)
            {
                const auto src = (double) p.origStart + (outputOfX (x) - (double) p.curStart) * p.stretch;
                const auto src2 = (double) p.origStart + (outputOfX (x + 1.0f) - (double) p.curStart) * p.stretch;
                const auto lo = std::max<std::int64_t> ((std::int64_t) std::max (0.0, src) / 256 - reach, p.origStart / 256);
                const auto hi = std::min<std::int64_t> ((std::int64_t) std::max (0.0, src2) / 256 + reach, std::min<std::int64_t> ((std::int64_t) peaks.size() - 1, p.voicedEnd() / 256));
                float m = 0.0f;
                for (auto i = lo; i <= hi; ++i)
                    m = std::max (m, peaks[(std::size_t) i]);
                const auto h = std::max (1.0f, half * std::sqrt (std::min (1.0f, gain * m / peakAll)));
                if (lower.empty())
                    blob.startNewSubPath (x, y - h); // (juce::Path::isEmpty() stays true until a line: a flag, not it)
                else
                    blob.lineTo (x, y - h);
                lower.push_back ({ x, y + h });
            }
            for (auto it = lower.rbegin(); it != lower.rend(); ++it)
                blob.lineTo (*it);
            blob.closeSubPath();
            g.setColour (flnt::blob.withAlpha (alpha));
            g.fillPath (blob);
            // FL shades the nearest semitone of a note sung off it (seen: 24 c shaded, 12 c not; LYNX: beyond 20 c)
            const auto semi = std::round (target);
            if (std::abs (target - semi) > 0.2)
            {
                g.setColour (flnt::orange.withAlpha (0.35f * alpha));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, yOfPitch (semi + 0.5) + 1.0f, x1, yOfPitch (semi - 0.5) - 1.0f));
            }
            // the note's average pitch: an orange outline, red when the note is selected (FL)
            g.setColour ((isSelected ? juce::Colour (0xffff3c3c) : flnt::orange).withAlpha (alpha));
            g.drawRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (x0 + 0.75f, y - 0.5f * (float) rowH + 0.75f, std::max (x0 + 3.0f, x1 - 0.75f),
                                                                                 y + 0.5f * (float) rowH - 0.75f),
                                    4.0f, 1.4f);
            if (advancedEdit && k == currentNote)
                paintAdvancedHandles (g, p, x0, x1, y);

            // the detected pitch, moved with the note (drift and variation applied), joined to the note before
            if (! pitchTrack.empty())
            {
                const auto [s0, s1] = std::pair<std::int64_t, std::int64_t> { p.origStart, p.voicedEnd() };
                for (auto f = std::max<std::int64_t> (0, s0 / pitchHop); f < std::min<std::int64_t> ((std::int64_t) pitchTrack.size(), s1 / pitchHop + 1); ++f)
                {
                    const auto v = pitchTrack[(std::size_t) f];
                    if (v < 0.0 || std::abs (v - p.origPitch) > 1.5 || f * pitchHop < s0 || f * pitchHop >= s1)
                        continue;
                    const auto px = xOfOutput ((double) p.curStart + (double) (f * pitchHop - p.origStart) / std::max (1e-6, p.stretch));
                    const auto py = yOfPitch (p.origPitch + p.shift + p.drift * (v - p.origPitch));
                    if (! lineOpen)
                        pitchLine.startNewSubPath (px, py);
                    else
                        pitchLine.lineTo (px, py);
                    lineOpen = true;
                }
                // a note followed by silence ends its line; a legato one joins the next note
                lineOpen = lineOpen && p.voicedEnd() + (std::int64_t) (0.02 * (sample != nullptr ? sample->sampleRate : 44100.0)) >= p.origEnd;
            }
        }
        g.setColour (flnt::orangeLine);
        g.strokePath (pitchLine, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (flnt::pitchCore);
        g.strokePath (pitchLine, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ---------------------------------------------------------------------------------------------- header actions
    void headerClick (Hit h, bool right = false)
    {
        switch (h)
        {
            case Hit::loop: looping = ! looping; if (previewing) startPreview(); break;
            case Hit::play: togglePreview(); break;
            case Hit::stop: if (previewing) togglePreview(); break;
            case Hit::slave: slave.setToggleState (! slave.getToggleState(), juce::sendNotification); break;
            case Hit::follow: autoScroll = ! autoScroll; break;
            case Hit::fileMenu: showFileMenu(); break;
            case Hit::editMenu: showEditMenu(); break;
            case Hit::selectMenu: showSelectMenu(); break;
            case Hit::undo: engine.undo(); break;
            case Hit::score:
                if (right)
                    copyScoreToMidiClipboard();
                else
                    sendScoreToSelectedChannel();
                break;
            case Hit::saveAs: saveWav(); break;
            case Hit::dragOut: break; // (a drag starts in mouseDrag)
            case Hit::toPlaylist: sendToPlaylist(); break;
            case Hit::sync: toggleTempoSync(); break;
            case Hit::cut: cutMode.setToggleState (! cutMode.getToggleState(), juce::dontSendNotification); updateButtons(); break;
            case Hit::advanced:
                advancedEdit = ! advancedEdit;
                if (advancedEdit && currentNote < 0 && ! selected.empty())
                    currentNote = *selected.begin();
                updatePanel();
                resized();
                break;
            case Hit::knobCentre:
            case Hit::knobVariation:
            case Hit::knobTrans:
            case Hit::none: break;
        }
        repaint();
    }
    // the knobs' 0..1 positions: Center 0..100 %, Variation 0..200 % (FL's default 100 % at the top), Trans 0..100 %
    double knobValue (Hit h) const
    {
        return h == Hit::knobCentre ? notes.centre : h == Hit::knobVariation ? notes.variation / 2.0 : std::min (1.0, notes.transition);
    }
    void setKnob (Hit h, double v01, bool done)
    {
        v01 = std::clamp (v01, 0.0, 1.0);
        if (h == Hit::knobCentre)
            newtonefx::applyCentre (notes, v01, selection());
        else if (h == Hit::knobVariation)
            newtonefx::applyVariation (notes, v01 * 2.0, selection());
        else
            newtonefx::applyTransition (notes, v01, selection());
        if (done)
            commit();
        else
            repaint();
    }

    // FL's WAVOptionsMenu (TSynthEditorForm): File, Edit, Select and Zoom; the header buttons open their own part, a
    // right-click on the empty note view the whole menu. Items FL has that LYNX does not do yet are left out (no stubs):
    // New, the slave options, Use project tempo on load, Edit properties, About, Cut / Copy / Paste.
    juce::PopupMenu fileMenu()
    {
        juce::PopupMenu m;
        m.addItem (tr ("Load sample...") + "\tCtrl+O", validSlot(), false, [this] { chooseSample(); });
        m.addItem (tr ("Save sample as...") + "\tCtrl+S", sample != nullptr, false, [this] { saveWav(); });
        m.addSeparator();
        m.addItem (tr ("Save as MIDI..."), sample != nullptr, false, [this] { saveMidi(); });
        m.addSeparator();
        m.addItem (tr ("Export regions"), true, exportRegionsOnSave, [this] { exportRegionsOnSave = ! exportRegionsOnSave; });
        m.addItem (tr ("Slave to host playback") + "\tH", validSlot(), slave.getToggleState(), [this] { slave.setToggleState (! slave.getToggleState(), juce::sendNotification); repaint(); });
        return m;
    }
    juce::PopupMenu editMenu()
    {
        juce::PopupMenu m, scaleMenu, gridMenu, channels;
        m.addItem (tr ("Undo") + "\tCtrl+Z", engine.canUndo(), false, [this] { engine.undo(); });
        m.addItem (tr ("Redo") + "\tCtrl+Y", true, false, [this] { engine.redo(); });
        m.addSeparator();
        m.addItem (tr ("Copy to MIDI clipboard"), sample != nullptr, false, [this] { copyScoreToMidiClipboard(); });
        m.addSeparator();
        m.addItem (tr ("Delete") + "\tDel", sample != nullptr && ! selected.empty(), false, [this] {
            for (const auto k : selected)
                notes.pitches[(std::size_t) k].enabled = false;
            commit();
        });
        m.addSeparator();
        m.addItem (tr ("Quantize time") + "\tCtrl+Q", sample != nullptr, false, [this] { quantize(); });
        // round 190kx (LYNX extra; off = FL's analysis): notes a fraction of a semitone apart, or a new attack on one
        // pitch, become notes of their own (as Melodyne's Melodic detection), and the sample is analysed again
        m.addItem (tr ("Separate close notes (analyse again)"), sample != nullptr && ! warpMode(), fineAnalysis, [this] {
            fineAnalysis = ! fineAnalysis;
            reanalyse();
        });
        // round 190li (LYNX extras): the natural engine (a new slot has it; older projects keep the engine they were made
        // with until it is ticked), and Melodyne-style pitch modulation (vibrato) / pitch drift amounts for the selected
        // notes (all when none is selected), which the natural engine renders
        {
            const auto noteMode = sample != nullptr && ! warpMode();
            m.addItem (tr ("Natural pitch engine (LYNX)"), noteMode, notes.engine >= 1, [this] {
                notes.engine = notes.engine >= 1 ? 0 : newtonefx::latestEngine;
                commit();
            });
            const auto natural = noteMode && notes.engine >= 1;
            auto amounts = [this] (bool modulation) {
                juce::PopupMenu sub;
                const auto* first = notes.pitches.empty() ? nullptr : &notes.pitches[(std::size_t) (selected.empty() ? 0 : *selected.begin())];
                const auto now = first == nullptr ? 1.0 : modulation ? first->modulation : first->pitchDrift;
                for (const auto v : { 0, 25, 50, 75, 100, 125, 150, 200 })
                    sub.addItem (juce::String (v) + " %", true, std::abs (now * 100.0 - v) < 0.5, [this, v, modulation] {
                        if (modulation)
                            newtonefx::applyModulation (notes, v / 100.0, selection());
                        else
                            newtonefx::applyPitchDrift (notes, v / 100.0, selection());
                        commit();
                    });
                return sub;
            };
            m.addSubMenu (tr ("Pitch modulation"), amounts (true), natural);
            m.addSubMenu (tr ("Pitch drift"), amounts (false), natural);
        }
        m.addSeparator();
        m.addItem (tr ("Advanced edit pitch") + "\tCtrl+E", sample != nullptr, advancedEdit, [this] { headerClick (Hit::advanced); });
        m.addItem (tr ("Advanced edit vibrato"), sample != nullptr, vibratoEdit, [this] {
            vibratoEdit = ! vibratoEdit;
            updatePanel();
            resized();
            repaint();
        });
        m.addItem (tr ("Cut mode") + "\tC", sample != nullptr, cutMode.getToggleState(), [this] {
            cutMode.setToggleState (! cutMode.getToggleState(), juce::dontSendNotification);
            updateButtons();
            repaint();
        });
        m.addSeparator();
        m.addItem (tr ("Snap to scale"), true, snapScale, [this] { snapScale = ! snapScale; repaint(); });
        // FL's Scale menu: major and minor in every key (FL's KeyIdx 0-23), two columns
        scaleMenu.addSectionHeader (tr ("Major"));
        for (int k = 0; k < 24; ++k)
        {
            if (k == 12)
            {
                scaleMenu.addColumnBreak();
                scaleMenu.addSectionHeader (tr ("Minor"));
            }
            scaleMenu.addItem (noteNames[k % 12], true, notes.key == k, [this, k] {
                notes.key = k;
                commit();
            });
        }
        m.addSubMenu (tr ("Scale"), scaleMenu);
        const std::pair<int, const char*> items[] { { 1, "Beat" }, { 2, "1/2 beat" }, { 3, "1/3 beat" }, { 4, "1/4 beat" }, { 6, "1/6 beat" }, { 8, "1/8 beat" }, { 0, "Off" } };
        for (const auto& [division, name] : items)
            gridMenu.addItem (tr (name), true, snapDivision == division, [this, d = division] { snapDivision = d; });
        m.addSubMenu (tr ("Snap to grid"), gridMenu);
        m.addSeparator();
        m.addItem (tr ("Send to playlist as audio clip"), sample != nullptr, false, [this] { sendToPlaylist(); });
        const auto& song = engine.getSong();
        for (int c = 0; c < (int) song.channels.size(); ++c)
            channels.addItem (juce::String (c + 1) + ". " + juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()), [this, c] { sendScoreTo (c, true); });
        m.addSubMenu (tr ("Send to piano roll"), channels, sample != nullptr && ! song.channels.empty() && ! song.patterns.empty());
        return m;
    }
    juce::PopupMenu selectMenu()
    {
        juce::PopupMenu m;
        const auto has = sample != nullptr && ! notes.pitches.empty();
        m.addItem (tr ("Deselect") + "\t" + tr ("Up"), has, false, [this] { selectNone(); });
        m.addItem (tr ("Select time around selection") + "\tCtrl+Enter", has, false, [this] { selectTimeAround(); });
        m.addItem (tr ("Select before current selection"), has, false, [this] { selectBeside (false); });
        m.addItem (tr ("Select after current selection"), has, false, [this] { selectBeside (true); });
        m.addItem (tr ("Select all (time)"), has, false, [this] { selectAllTime(); });
        m.addItem (tr ("Select all (slices)") + "\tCtrl+A", has, false, [this] { selectAllNotes(); });
        m.addItem (tr ("Select all (time and slices)"), has, false, [this] { selectAllTime(); selectAllNotes(); });
        return m;
    }
    juce::PopupMenu zoomMenu()
    {
        juce::PopupMenu m;
        const auto has = sample != nullptr;
        m.addItem (tr ("Zoom in") + "\tPgUp", has, false, [this] { zoomBy (1.5); });
        m.addItem (tr ("Zoom out") + "\tPgDn", has, false, [this] { zoomBy (1.0 / 1.5); });
        m.addItem (tr ("Zoom out full") + "\tHome", has, false, [this] { zoomFull(); });
        m.addSeparator();
        m.addItem (tr ("Zoom on selection") + "\tEnd", has, false, [this] { zoomOnSelection (0); });
        m.addItem (tr ("Zoom on left of selection") + "\tCtrl+Left", has, false, [this] { zoomOnSelection (-1); });
        m.addItem (tr ("Zoom on right of selection") + "\tCtrl+Right", has, false, [this] { zoomOnSelection (1); });
        return m;
    }
    void showMenuAt (juce::PopupMenu m, Hit h)
    {
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (hitBox (h).toNearestInt())));
    }
    void showFileMenu() { showMenuAt (fileMenu(), Hit::fileMenu); }
    void showEditMenu() { showMenuAt (editMenu(), Hit::editMenu); }
    void showSelectMenu() { showMenuAt (selectMenu(), Hit::selectMenu); }
    void showWholeMenu()
    {
        juce::PopupMenu m;
        m.addSubMenu (tr ("File"), fileMenu());
        m.addSubMenu (tr ("Edit"), editMenu());
        m.addSubMenu (tr ("Select"), selectMenu());
        m.addSubMenu (tr ("Zoom"), zoomMenu());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    // Zoom
    void zoomBy (double factor)
    {
        const auto centre = scroll + 0.5 * visibleLength();
        zoom = std::clamp (zoom * factor, 1.0, 512.0);
        scroll = centre - 0.5 * visibleLength();
        clampScroll();
        updateScrollBars();
        repaint();
    }
    void zoomFull()
    {
        zoom = 1.0;
        scroll = 0.0;
        updateScrollBars();
        repaint();
    }
    // on the time selection (0), or on its left / right half (-1 / 1); with none, on the selected notes' time
    void zoomOnSelection (int side)
    {
        auto a = timeSelA, b = timeSelB;
        if (! hasTimeSelection())
        {
            if (selected.empty())
                return;
            std::tie (a, b) = newtonefx::timeAround (notes, { selected.begin(), selected.end() });
        }
        if (b <= a)
            return;
        if (side < 0)
            b = a + (b - a) / 2;
        else if (side > 0)
            a = a + (b - a) / 2;
        zoom = std::clamp (totalLength() / (double) (b - a), 1.0, 512.0);
        scroll = (double) a;
        clampScroll();
        updateScrollBars();
        repaint();
    }
    void selectAllTime()
    {
        timeSelA = 0;
        timeSelB = (std::int64_t) totalLength();
        repaint();
    }
    // Edit › Quantize time: the selected notes' starts (all when none) to the snap grid (a beat when Snap is Off)
    void quantize()
    {
        const auto division = snapDivision > 0 ? snapDivision : 1;
        newtonefx::quantizeTime (notes, { selected.begin(), selected.end() }, beatSamples() / division);
        commit();
    }

    void selectNone()
    {
        selected.clear();
        timeSelA = timeSelB = -1;
        selectionChanged();
    }
    void selectAllNotes()
    {
        selected.clear();
        for (int k = 0; k < (int) notes.pitches.size(); ++k)
            if (notes.pitches[(std::size_t) k].voiced())
                selected.insert (k);
        selectionChanged();
    }
    // FL's Select › Select time around selection (Ctrl+Enter): the time the selected notes take; with nothing selected,
    // the part of the sample in view
    void selectTimeAround()
    {
        if (selected.empty())
        {
            timeSelA = (std::int64_t) std::max (0.0, scroll);
            timeSelB = (std::int64_t) std::min (totalLength(), scroll + visibleLength());
        }
        else
        {
            const auto r = newtonefx::timeAround (notes, { selected.begin(), selected.end() });
            timeSelA = r.first;
            timeSelB = r.second;
        }
        repaint();
    }
    // Select before / after current selection: the time from the start up to the selection, or from it to the end
    void selectBeside (bool after)
    {
        if (! hasTimeSelection())
            selectTimeAround();
        if (! hasTimeSelection())
            return;
        const auto total = (std::int64_t) totalLength();
        if (after)
            timeSelA = timeSelB, timeSelB = total;
        else
            timeSelB = timeSelA, timeSelA = 0;
        if (timeSelB <= timeSelA)
            timeSelA = timeSelB = -1;
        selected.clear();
        for (const auto k : newtonefx::notesIn (notes, timeSelA, timeSelB))
            selected.insert (k);
        selectionChanged();
    }

    // FL's Create score: the score to the selected channel's piano roll in the current pattern (its notes there replaced)
    void sendScoreToSelectedChannel()
    {
        const auto& song = engine.getSong();
        if (song.channels.empty() || song.patterns.empty())
            return;
        sendScoreTo (std::clamp (engine.getSelectedChannel(), 0, (int) song.channels.size() - 1), true);
    }
    void sendScoreTo (int channel, bool replace = false)
    {
        if (sample == nullptr)
            return;
        const auto add = scoreNotes (channel);
        engine.editSong ([add, channel, replace] (Song& s) {
            if (s.patterns.empty())
                return;
            auto& pattern = s.patterns[(std::size_t) std::clamp (s.currentPattern, 0, (int) s.patterns.size() - 1)];
            if (replace)
                pattern.notes.erase (std::remove_if (pattern.notes.begin(), pattern.notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                                     pattern.notes.end());
            pattern.notes.insert (pattern.notes.end(), add.begin(), add.end());
            pattern.sortNotes();
        });
    }
    void copyScoreToMidiClipboard()
    {
        if (sample == nullptr)
            return;
        midiclipboard::put (MidiFileIo::writeNotes (engine.getSong(), scoreNotes (0), "Newtone"));
    }

    bool exportRegionsOnSave = true; // FL's File › Export regions (a checked option for Save sample as; on in FL 2026)
    std::int64_t previewOffset() const noexcept { return ! flLook() ? 0 : hasTimeSelection() ? timeSelA : cursorAt; }
    std::shared_ptr<const SampleData> previewSource;
    float previewLevel = 0.0f;

    // Sync: the whole sample stretched from its own tempo to the project's (FL's SYNC; the notes keep their pitch)
    double sampleTempo = 0.0;
    bool tempoSync = false;
    void toggleTempoSync()
    {
        if (sampleTempo <= 0.0 || sample == nullptr)
            return;
        tempoSync = ! tempoSync;
        newtonefx::stretchAll (notes, tempoSync ? sampleTempo / std::max (1.0, engine.getSong().bpm) : 1.0);
        commit();
    }

    // Drag selection: the selection (or the whole sample) as a WAV dragged out of the window
    void dragSelectionOut()
    {
        if (sample == nullptr)
            return;
        auto out = newtonefx::renderedSource (sample, savedState);
        if (out == nullptr)
            return;
        if (hasTimeSelection())
            out = newtonefx::cropped (*out, timeSelA, timeSelB);
        const juce::File src (juce::String::fromUTF8 (samplePath.c_str()));
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("LYNX")
                              .getChildFile (juce::File::createLegalFileName (src.getFileNameWithoutExtension() + " (Newtone).wav"));
        file.getParentDirectory().createDirectory();
        if (writeWav (*out, file))
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
    }

    void scrollBarMoved (juce::ScrollBar* bar, double start) override
    {
        if (bar == &hScroll)
            scroll = start;
        else
            topPitch = 127.5 - start;
        repaint();
    }
    void updateScrollBars()
    {
        if (! flLook())
            return;
        hScroll.setRangeLimits (0.0, totalLength(), juce::dontSendNotification);
        hScroll.setCurrentRange (scroll, visibleLength(), juce::dontSendNotification);
        const auto rows = (double) flCanvas().getHeight() / rowH;
        vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
        vScroll.setCurrentRange (127.5 - topPitch, rows, juce::dontSendNotification);
    }
    // the view's rows centred on the notes (on a new sample)
    void centreRowsOnNotes()
    {
        double lo = 1e9, hi = -1e9;
        for (const auto& p : notes.pitches)
            if (p.origPitch > 0.0 && p.voiced())
                lo = std::min (lo, p.origPitch + p.shift), hi = std::max (hi, p.origPitch + p.shift);
        if (lo > hi)
            lo = 55.0, hi = 70.0;
        const auto rows = (double) std::max (1, flCanvas().getHeight()) / rowH;
        topPitch = std::round (0.5 * (lo + hi) + 0.5 * rows);
    }

    // ---------------------------------------------------------------------------------------------- FL mouse / keys
    struct FlDrag
    {
        bool pan = false, knob = false, out = false, toggleOnClick = false;
        Hit knobHit = Hit::none;
        double scrollFrom = 0.0, topFrom = 0.0;
        int edgeNote = -1;      // a note edge being moved
        bool edgeLeft = false, detach = false;
        std::int64_t edgeFrom = 0;
        std::vector<std::pair<std::int64_t, std::int64_t>> spans; // the notes' [curStart, curEnd) at the start
        int toggleNote = -1;
    } fl;

    // a note's edge under the point: (note, left edge?)
    std::pair<int, bool> edgeAt (juce::Point<float> pos) const
    {
        for (int k = 0; k < (int) notes.pitches.size(); ++k)
        {
            const auto& p = notes.pitches[(std::size_t) k];
            if (! p.voiced() || p.origPitch <= 0.0)
                continue;
            const auto y = yOfPitch (p.origPitch + p.shift);
            if (std::abs (pos.y - y) > 0.5f * (float) rowH)
                continue;
            if (std::abs (pos.x - xOfOutput ((double) p.curStart)) <= 3.5f)
                return { k, true };
            if (std::abs (pos.x - xOfOutput ((double) p.curEnd)) <= 3.5f)
                return { k, false };
        }
        return { -1, false };
    }

    bool flMouseDown (const juce::MouseEvent& e)
    {
        fl = {};
        if (const auto h = hitAt (e.position); h != Hit::none)
        {
            pressedHit = h;
            if (h == Hit::knobCentre || h == Hit::knobVariation || h == Hit::knobTrans)
            {
                fl.knob = true;
                fl.knobHit = h;
                knobFrom = knobValue (h);
            }
            else if (h == Hit::dragOut)
                fl.out = true;
            else
                headerClick (h, e.mods.isPopupMenu());
            repaint (headerArea());
            return true;
        }
        if (headerArea().contains (e.getPosition()))
            return true;
        if (sample == nullptr)
            return true;
        if (e.mods.isMiddleButtonDown() || (e.mods.isCtrlDown() && e.mods.isAltDown()))
        {
            fl.pan = true;
            fl.scrollFrom = scroll;
            fl.topFrom = topPitch;
            return true;
        }
        if (timeRulerArea().contains (e.getPosition()))
        {
            rulerSelecting = true;
            timeSelA = timeSelB = std::clamp<std::int64_t> ((std::int64_t) outputOfX (e.position.x), 0, (std::int64_t) totalLength());
            repaint();
            return true;
        }
        if (! flCanvas().contains (e.getPosition()))
            return false; // (the keyboard: the shared code moves the selection to the key clicked)
        if (! e.mods.isPopupMenu())
            if (const auto z = advZoneAt (e.position); z != AdvZone::none)
            {
                advDrag = z;
                advFrom = notes.pitches[(std::size_t) currentNote];
                return true;
            }
        // FL: a click puts the play cursor there (also on a note; a drag still moves the note)
        if (! e.mods.isPopupMenu() && ! e.mods.isAnyModifierKeyDown())
        {
            cursorAt = std::clamp<std::int64_t> ((std::int64_t) outputOfX (e.position.x), 0, (std::int64_t) totalLength());
            repaint();
        }
        // a right-click on the empty roll: FL's whole menu (File, Edit, Select, Zoom)
        if (e.mods.isPopupMenu() && ! e.mods.isCtrlDown() && noteAt (e.position) < 0)
        {
            showWholeMenu();
            return true;
        }
        // Ctrl+right-click: zoom to the time selection, or to the whole sample
        if (e.mods.isPopupMenu() && e.mods.isCtrlDown())
        {
            if (hasTimeSelection())
            {
                zoom = std::clamp (totalLength() / (double) (timeSelB - timeSelA), 1.0, 512.0);
                scroll = (double) timeSelA;
            }
            else
                zoom = 1.0, scroll = 0.0;
            clampScroll();
            updateScrollBars();
            repaint();
            return true;
        }
        if (! cutMode.getToggleState() && ! e.mods.isShiftDown() && ! e.mods.isPopupMenu())
            if (const auto [k, left] = edgeAt (e.position); k >= 0)
            {
                fl.edgeNote = k;
                fl.edgeLeft = left;
                fl.detach = e.mods.isCtrlDown();
                const auto& p = notes.pitches[(std::size_t) k];
                fl.edgeFrom = left ? p.curStart : p.curEnd;
                for (const auto& q : notes.pitches)
                    fl.spans.push_back ({ q.curStart, q.curEnd });
                return true;
            }
        // Ctrl+click on a note: add it to / take it from the selection (a Ctrl+drag still moves the notes in time)
        if (e.mods.isCtrlDown() && ! e.mods.isPopupMenu() && ! cutMode.getToggleState())
            if (const auto k = noteAt (e.position); k >= 0)
            {
                fl.toggleNote = k;
                if (selected.count (k) == 0)
                    selected.insert (k), fl.toggleOnClick = false;
                else
                    fl.toggleOnClick = true;
                selectionChanged();
            }
        timeSelA = timeSelB = -1;
        return false;
    }

    // a note boundary moved: the note (and its joined neighbour unless detached) stretched to the new edge
    void moveEdge (std::int64_t to)
    {
        const auto k = fl.edgeNote;
        auto& p = notes.pitches[(std::size_t) k];
        auto restretch = [] (newtonefx::Note& q) {
            const auto out = std::max<std::int64_t> (1, q.curEnd - q.curStart);
            q.stretch = (double) (q.origEnd - q.origStart) / (double) out;
            if (std::abs (q.stretch - 1.0) < 1e-9)
                q.stretch = 1.0;
        };
        for (std::size_t i = 0; i < notes.pitches.size() && i < fl.spans.size(); ++i)
            notes.pitches[i].curStart = fl.spans[i].first, notes.pitches[i].curEnd = fl.spans[i].second;
        if (fl.edgeLeft)
        {
            const auto lo = k > 0 ? notes.pitches[(std::size_t) k - 1].curStart + 1 : 0;
            to = std::clamp<std::int64_t> (to, lo, p.curEnd - 1);
            const auto joined = k > 0 && ! fl.detach && fl.spans[(std::size_t) k - 1].second == fl.spans[(std::size_t) k].first;
            p.curStart = to;
            if (joined)
                notes.pitches[(std::size_t) k - 1].curEnd = to, restretch (notes.pitches[(std::size_t) k - 1]);
        }
        else
        {
            const auto hi = k + 1 < (int) notes.pitches.size() ? notes.pitches[(std::size_t) k + 1].curEnd - 1 : (std::int64_t) (4 * frames());
            to = std::clamp<std::int64_t> (to, p.curStart + 1, hi);
            const auto joined = k + 1 < (int) notes.pitches.size() && ! fl.detach && fl.spans[(std::size_t) k + 1].first == fl.spans[(std::size_t) k].second;
            p.curEnd = to;
            if (joined)
                notes.pitches[(std::size_t) k + 1].curStart = to, restretch (notes.pitches[(std::size_t) k + 1]);
        }
        restretch (p);
    }

    bool flMouseDrag (const juce::MouseEvent& e)
    {
        if (advDrag != AdvZone::none)
        {
            advancedDrag (e);
            return true;
        }
        if (fl.knob)
        {
            setKnob (fl.knobHit, knobFrom - (double) e.getDistanceFromDragStartY() / 150.0, false);
            return true;
        }
        if (fl.out)
        {
            if (e.getDistanceFromDragStart() > 6)
            {
                fl.out = false;
                pressedHit = Hit::none;
                dragSelectionOut();
            }
            return true;
        }
        if (pressedHit != Hit::none)
            return true;
        if (fl.pan)
        {
            scroll = fl.scrollFrom - (double) e.getDistanceFromDragStartX() / pixelsPerSample();
            topPitch = std::clamp (fl.topFrom + (double) e.getDistanceFromDragStartY() / rowH, 10.0, 127.5);
            clampScroll();
            updateScrollBars();
            repaint();
            return true;
        }
        if (rulerSelecting)
        {
            const auto at = std::clamp<std::int64_t> (snapPosition ((std::int64_t) outputOfX (e.position.x), e.mods.isAltDown()), 0, (std::int64_t) totalLength());
            const auto from = (std::int64_t) std::clamp (outputOfX ((float) e.getMouseDownX()), 0.0, totalLength());
            timeSelA = std::min (from, at);
            timeSelB = std::max (from, at);
            repaint();
            return true;
        }
        if (fl.edgeNote >= 0 && fl.edgeNote < (int) notes.pitches.size())
        {
            const auto to = snapPosition (fl.edgeFrom + (std::int64_t) std::llround ((double) e.getDistanceFromDragStartX() / pixelsPerSample()), e.mods.isAltDown());
            moveEdge (to);
            drag.moved = true;
            repaint();
            return true;
        }
        if (fl.toggleNote >= 0 && e.getDistanceFromDragStart() > 3)
            fl.toggleOnClick = false; // (a drag: a time move, not a toggle)
        return false;
    }

    void flMouseUp (const juce::MouseEvent&)
    {
        if (fl.knob)
            commit();
        advDrag = AdvZone::none;
        if (fl.toggleNote >= 0 && fl.toggleOnClick && ! drag.moved)
        {
            selected.erase (fl.toggleNote);
            selectionChanged();
        }
        if (pressedHit != Hit::none)
        {
            pressedHit = Hit::none;
            repaint (headerArea());
        }
        rulerSelecting = false;
        fl = {};
    }

    void flMouseMove (const juce::MouseEvent& e)
    {
        const auto h = hitAt (e.position);
        if (h != hoverHit)
        {
            hoverHit = h;
            setTooltip (hitTip (h));
            repaint (headerArea());
        }
        auto cursor = juce::MouseCursor::NormalCursor;
        if (sample != nullptr && flCanvas().contains (e.getPosition()))
        {
            const auto onNote = noteAt (e.position) >= 0;
            if ((cutMode.getToggleState() || e.mods.isShiftDown()) && onNote)
                cursor = juce::MouseCursor::IBeamCursor;
            else if (edgeAt (e.position).first >= 0)
                cursor = juce::MouseCursor::LeftRightResizeCursor;
            else if (onNote)
                cursor = juce::MouseCursor::UpDownResizeCursor;
        }
        setMouseCursor (cursor);
    }

    // FL: wheel = up / down, Shift+wheel = sideways, Ctrl+wheel = zoom in time, Alt+wheel = zoom the rows
    void flWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const auto d = w.deltaY != 0.0f ? w.deltaY : w.deltaX;
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            const auto anchor = outputOfX (e.position.x);
            zoom = std::clamp (zoom * (d > 0 ? 1.25 : 0.8), 1.0, 512.0);
            scroll = anchor - (e.position.x - (float) canvasArea().getX()) / pixelsPerSample();
        }
        else if (e.mods.isAltDown())
        {
            const auto anchor = pitchOfY (e.position.y);
            rowH = std::clamp (rowH * (d > 0 ? 1.15 : 1.0 / 1.15), 8.0, 64.0);
            topPitch = anchor + (double) (e.position.y - (float) flCanvas().getY()) / rowH;
        }
        else if (e.mods.isShiftDown() || w.deltaX != 0.0f)
            scroll -= d * 0.25 * visibleLength();
        else
            topPitch = std::clamp (topPitch + d * 6.0, 10.0, 127.5);
        clampScroll();
        updateScrollBars();
        repaint();
    }

    bool flKey (const juce::KeyPress& key)
    {
        const auto cmd = key.getModifiers().isCommandDown();
        const auto code = key.getKeyCode();
        if (key == juce::KeyPress::spaceKey)
        {
            togglePreview();
            return true;
        }
        if (cmd && (code == 'O' || code == 'o'))
        {
            chooseSample();
            return true;
        }
        if (cmd && (code == 'S' || code == 's'))
        {
            saveWav();
            return true;
        }
        if (cmd && key == juce::KeyPress::returnKey)
        {
            selectTimeAround();
            return true;
        }
        if (sample != nullptr)
        {
            // FL's shortcuts (TSynthEditorForm menus): Ctrl+E Advanced edit pitch, Ctrl+A Select all (slices), Up Deselect,
            // Ctrl+Q Quantize time, PgUp / PgDn / Home / End / Ctrl+Left / Ctrl+Right the Zoom menu
            if (cmd && (code == 'E' || code == 'e'))
                return headerClick (Hit::advanced), true;
            if (cmd && (code == 'A' || code == 'a'))
                return selectAllNotes(), true;
            if (cmd && (code == 'Q' || code == 'q'))
                return quantize(), true;
            if (! cmd && key == juce::KeyPress::upKey)
                return selectNone(), true;
            if (! cmd && key == juce::KeyPress::downKey)
                return true; // (FL's look: Up deselects; the old layout's Up / Down transposition is not here)
            if (key == juce::KeyPress::pageUpKey)
                return zoomBy (1.5), true;
            if (key == juce::KeyPress::pageDownKey)
                return zoomBy (1.0 / 1.5), true;
            if (key == juce::KeyPress::homeKey)
                return zoomFull(), true;
            if (key == juce::KeyPress::endKey)
                return zoomOnSelection (0), true;
            if (cmd && key.getKeyCode() == juce::KeyPress::leftKey)
                return zoomOnSelection (-1), true;
            if (cmd && key.getKeyCode() == juce::KeyPress::rightKey)
                return zoomOnSelection (1), true;
        }
        if (! cmd && ! key.getModifiers().isAltDown())
        {
            if (code == 'L' || code == 'l')
                return headerClick (Hit::loop), true;
            if (code == 'A' || code == 'a')
                return headerClick (Hit::follow), true;
            if (code == 'H' || code == 'h')
                return headerClick (Hit::slave), true;
        }
        return false;
    }

    // ---------------------------------------------------------------------------------------- FL's Advanced edit
    // The double-clicked note opens three rows tall (FL 2026 capture): the top band "volume" (drag up / down; the green
    // and red bars there are the volume fade-in / fade-out times, dragged sideways), the middle row the pitch (the
    // green arrows on the left: ramp-in, the red ones on the right: ramp-out, the comb: formant; elsewhere the pitch),
    // the bottom band "variation" (drag up / down; its bars are the pitch smooth-in / smooth-out times).
    enum class AdvZone { none, volume, variation, fadeIn, fadeOut, smoothIn, smoothOut, rampIn, rampOut, formant };
    juce::Rectangle<float> advancedBox (const newtonefx::Note& p) const
    {
        const auto x0 = xOfOutput ((double) p.curStart), x1 = xOfOutput ((double) p.curEnd);
        const auto y = yOfPitch (p.origPitch + p.shift);
        return juce::Rectangle<float>::leftTopRightBottom (x0, y - 1.5f * (float) rowH, std::max (x0 + 24.0f, x1), y + 1.5f * (float) rowH);
    }
    float timeX (const newtonefx::Note& p, double samples, bool fromEnd) const
    {
        const auto out = samples / std::max (1e-9, p.stretch);
        return xOfOutput (fromEnd ? (double) p.curEnd - out : (double) p.curStart + out);
    }
    AdvZone advZoneAt (juce::Point<float> pos) const
    {
        if (! advancedEdit || currentNote < 0 || currentNote >= (int) notes.pitches.size())
            return AdvZone::none;
        const auto& p = notes.pitches[(std::size_t) currentNote];
        const auto box = advancedBox (p);
        if (! box.contains (pos))
            return AdvZone::none;
        const auto band = (pos.y - box.getY()) / std::max (1.0f, box.getHeight());
        const auto near = [&] (float x) { return std::abs (pos.x - x) <= 4.0f; };
        if (band < 1.0f / 3.0f)
            return near (timeX (p, p.fadeInTime, false)) ? AdvZone::fadeIn : near (timeX (p, p.fadeOutTime, true)) ? AdvZone::fadeOut : AdvZone::volume;
        if (band > 2.0f / 3.0f)
            return near (timeX (p, p.smoothIn, false)) ? AdvZone::smoothIn : near (timeX (p, p.smoothOut, true)) ? AdvZone::smoothOut : AdvZone::variation;
        const auto w = box.getWidth();
        if (pos.x < box.getX() + std::min (22.0f, 0.2f * w))
            return AdvZone::rampIn;
        if (pos.x > box.getRight() - 12.0f)
            return AdvZone::formant;
        if (pos.x > box.getRight() - std::min (34.0f, 0.3f * w))
            return AdvZone::rampOut;
        return AdvZone::none; // (the pitch itself: the shared note drag)
    }
    void paintAdvancedHandles (juce::Graphics& g, const newtonefx::Note& p, float, float, float y) const
    {
        const auto box = advancedBox (p);
        const auto band = box.getHeight() / 3.0f;
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (flnt::orange);
        g.drawRoundedRectangle (box.reduced (0.75f), 3.0f, 1.6f);
        g.setColour (juce::Colour (0xff3b4550));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (tr ("volume"), box.withHeight (band), juce::Justification::centred, false);
        g.drawText (tr ("variation"), box.withTrimmedTop (2.0f * band), juce::Justification::centred, false);
        const juce::Colour green (0xff2f9a3c), red (0xffb03030);
        auto bar = [&] (float x, float top, juce::Colour c) {
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (x - 1.5f, top, 3.0f, band));
        };
        bar (timeX (p, p.fadeInTime, false), box.getY(), green);
        bar (timeX (p, p.fadeOutTime, true), box.getY(), red);
        bar (timeX (p, p.smoothIn, false), box.getY() + 2.0f * band, green);
        bar (timeX (p, p.smoothOut, true), box.getY() + 2.0f * band, red);
        // the middle row's handles: ramp-in arrows (green), ramp-out arrows (red), the formant comb
        auto arrows = [&] (float cx, juce::Colour c) {
            juce::Path a;
            a.addTriangle (cx - 6.0f, y - 2.0f, cx + 6.0f, y - 2.0f, cx, y - 9.0f);
            a.addTriangle (cx - 6.0f, y + 2.0f, cx + 6.0f, y + 2.0f, cx, y + 9.0f);
            g.setColour (c.withAlpha (0.8f));
            g.fillPath (a);
        };
        arrows (box.getX() + std::min (11.0f, 0.1f * box.getWidth()), green);
        arrows (box.getRight() - std::min (23.0f, 0.2f * box.getWidth()), red);
        g.setColour (red.withAlpha (0.85f));
        for (int k = 0; k < 4; ++k)
            g.fillRect (juce::Rectangle<float> (box.getRight() - 10.0f + 2.0f * (float) k, y - 5.0f + (float) (k % 2) * 2.0f, 1.2f, 9.0f - (float) (k % 2) * 2.0f));
    }
    AdvZone advDrag = AdvZone::none;
    newtonefx::Note advFrom;
    void advancedDrag (const juce::MouseEvent& e)
    {
        if (currentNote < 0 || currentNote >= (int) notes.pitches.size())
            return;
        auto& p = notes.pitches[(std::size_t) currentNote];
        const auto dy = -(double) e.getDistanceFromDragStartY() / std::max (1.0, rowH);   // rows up
        const auto dx = (double) e.getDistanceFromDragStartX() / pixelsPerSample() * p.stretch; // source samples
        const auto len = (double) (p.origEnd - p.origStart);
        switch (advDrag)
        {
            case AdvZone::volume: p.volume = std::clamp (advFrom.volume * std::pow (10.0, 6.0 * dy / 20.0), 0.0, 4.0); break;
            case AdvZone::variation: p.drift = std::clamp (advFrom.drift + 0.5 * dy, 0.0, 2.0); break;
            case AdvZone::fadeIn: p.fadeInTime = std::clamp (advFrom.fadeInTime + dx, 0.0, len); p.fadeInGain = p.fadeInTime > 0.0 ? 0.0 : 1.0; break;
            case AdvZone::fadeOut: p.fadeOutTime = std::clamp (advFrom.fadeOutTime - dx, 0.0, len); p.fadeOutGain = p.fadeOutTime > 0.0 ? 0.0 : 1.0; break;
            case AdvZone::smoothIn: p.smoothIn = std::clamp (advFrom.smoothIn + dx, 0.0, len); break;
            case AdvZone::smoothOut: p.smoothOut = std::clamp (advFrom.smoothOut - dx, 0.0, len); break;
            case AdvZone::rampIn: p.rampIn = std::clamp (advFrom.rampIn + dy, -12.0, 12.0); break;
            case AdvZone::rampOut: p.rampOut = std::clamp (advFrom.rampOut + dy, -12.0, 12.0); break;
            case AdvZone::formant: p.formant = std::clamp (advFrom.formant * std::exp2 (dy / 12.0), 0.25, 4.0); break;
            case AdvZone::none: break;
        }
        drag.moved = true;
        updatePanel();
        repaint();
    }

    void paintFl (juce::Graphics& g)
    {
        g.fillAll (flnt::headerDark);
        paintFlHeader (g);
        const auto canvas = flCanvas();
        g.setColour (flnt::rowOut);
        g.fillRect (canvas);
        if (sample == nullptr)
        {
            paintFlKeyboard (g, flKeyboard());
            g.setColour (flnt::caption);
            g.setFont (theme::font (theme::type::body));
            g.drawText (validSlot() ? tr ("Load a sample to edit it (Load sample... or drop an audio file here)") : tr ("This slot no longer holds this plugin"),
                        canvas, juce::Justification::centred);
            return;
        }
        paintFlNotes (g, canvas);
        paintFlRuler (g, timeRulerArea());
        paintFlKeyboard (g, flKeyboard());
        if (vibratoEdit)
        {
            g.setColour (flnt::header);
            g.fillRect (flPanel());
            g.setColour (flnt::buttonEdge);
            g.drawVerticalLine (flPanel().getX(), (float) flPanel().getY(), (float) flPanel().getBottom());
        }
        g.setColour (flnt::rulerBg);
        g.fillRect (flBody().removeFromTop (flnt::rulerHeight).removeFromLeft (flnt::keyboardWidth));
        if (! band.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (band);
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawRect (band, 1.0f);
        }
        if (cursorAt > 0 && ! previewing)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawVerticalLine ((int) xOfOutput ((double) cursorAt), (float) canvas.getY(), (float) canvas.getBottom());
        }
        if (previewing)
            if (const auto pos = engine.getPreviewPosition(); pos >= 0.0)
            {
                const auto x = xOfOutput (pos + previewOffset());
                g.setColour (juce::Colour (0xff6fe36f));
                g.drawVerticalLine ((int) x, (float) timeRulerArea().getY(), (float) canvas.getBottom());
            }
    }

    void resizedFl()
    {
        for (auto* b : { &load, &analyse, &cutMode, &snapAll, &reset, &preview, &save, &scoreButton, &stretch, &snapButton, &toPlaylist })
            b->setVisible (false);
        slave.setVisible (false);
        status.setVisible (false);
        keyBox.setVisible (false);
        scaleBox.setVisible (false);
        for (auto* k : { &centreKnob, &variationKnob, &transitionKnob })
            k->setVisible (false);
        const auto body = flBody();
        hScroll.setVisible (true);
        vScroll.setVisible (true);
        hScroll.setBounds (body.withTrimmedLeft (flnt::keyboardWidth).withTrimmedRight (flnt::scrollSize).removeFromBottom (flnt::scrollSize));
        vScroll.setBounds (body.withTrimmedTop (flnt::rulerHeight).withTrimmedBottom (flnt::scrollSize).removeFromRight (flnt::scrollSize));
        const auto showPanel = vibratoEdit && sample != nullptr;
        panelTitle.setVisible (showPanel);
        for (auto* k : { &pitchKnob, &formantKnob, &volumeKnob, &driftKnob, &rampInKnob, &rampOutKnob, &vibRateKnob, &vibStartKnob, &vibEndKnob, &tremoloKnob })
            k->setVisible (showPanel);
        if (showPanel)
        {
            auto panel = flPanel().reduced (6);
            panelTitle.setBounds (panel.removeFromTop (20));
            const auto lw = theme::labelColumnWidth ({ &pitchKnob.label, &formantKnob.label, &volumeKnob.label, &driftKnob.label, &rampInKnob.label, &rampOutKnob.label,
                                                       &vibRateKnob.label, &vibStartKnob.label, &vibEndKnob.label, &tremoloKnob.label },
                                                     92, 130);
            for (auto* k : { &pitchKnob, &formantKnob, &volumeKnob, &driftKnob, &rampInKnob, &rampOutKnob, &vibRateKnob, &vibStartKnob, &vibEndKnob, &tremoloKnob })
            {
                k->place (panel.removeFromTop (22), lw);
                panel.removeFromTop (3);
            }
        }
        clampScroll();
        updateScrollBars();
    }

    void paintWarp (juce::Graphics& g, juce::Rectangle<int> area) const
    {
        // the warped waveform (where each part of the sample plays now)
        paintWave (g, area, [this] (double out) { return newtonefx::warpToSource (warp, frames(), out); }, theme::colour::info.withAlpha (0.7f));
        // bars of the song's tempo
        const auto bpm = engine.getSong().bpm;
        const auto beat = sample->sampleRate * 60.0 / std::max (1.0, bpm);
        g.setColour (theme::colour::border);
        for (double s = 0.0; s < totalLength(); s += beat)
        {
            const auto x = xOfOutput (s);
            if (x >= (float) area.getX() && x <= (float) area.getRight())
                g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getY() + 8.0f);
        }
        // the end of the original length, and the markers
        g.setColour (theme::colour::textDisabled);
        g.drawVerticalLine ((int) xOfOutput ((double) frames()), (float) area.getY(), (float) area.getBottom());
        for (const auto& m : warp.markers)
        {
            const auto x = xOfOutput ((double) m.output);
            g.setColour (theme::colour::warning);
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
            juce::Path tri;
            tri.addTriangle (x - 5.0f, (float) area.getY(), x + 5.0f, (float) area.getY(), x, (float) area.getY() + 8.0f);
            g.fillPath (tri);
        }
    }
};

class NewtoneWindow final : public juce::DocumentWindow
{
public:
    NewtoneWindow (AudioEngine& engine, int track, int slot, const juce::String& title)
        : DocumentWindow (title, theme::colour::bg2, DocumentWindow::closeButton),
          gone (engine, track, slot, [this] { closeButtonPressed(); })
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new NewtoneEditor (engine, track, slot), true);
        setResizable (true, false);
        setResizeLimits (600, 400, 4000, 2000); // (round 190kj: FL's Newtone window is 735 px wide)
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        dockOrKeepAbove (*this); // FL's docked plugin window (round 190ev)
    }

    void closeButtonPressed() override;

private:
    ui::SlotGoneWatcher gone;
};

std::map<std::pair<int, int>, std::unique_ptr<NewtoneWindow>>& newtoneWindows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<NewtoneWindow>> open;
    return open;
}

void NewtoneWindow::closeButtonPressed()
{
    for (auto& [key, window] : newtoneWindows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { newtoneWindows().erase (k); });
            return;
        }
}
} // namespace

namespace NewtoneWindows
{
bool handles (EffectKind kind)
{
    return isNewtoneKind (kind);
}

void open (AudioEngine& engine, int track, int slot)
{
    auto& open = newtoneWindows();
    const auto key = std::make_pair (track, slot);
    if (auto it = open.find (key); it != open.end())
    {
        it->second->toFront (true);
        return;
    }
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return;
    const auto kind = mixer[(std::size_t) track].effects[(std::size_t) slot].kind;
    const auto name = track == 0 ? tr ("Master") : juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str());
    open[key] = std::make_unique<NewtoneWindow> (engine, track, slot,
                                                 utf8 (effectInfo (kind).name) + " - " + (name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) }))
                                                     + " (" + tr ("slot {0}", { juce::String (slot + 1) }) + ")");
}

void closeAll()
{
    newtoneWindows().clear();
}
} // namespace NewtoneWindows

void openSampleInMasterEditor (AudioEngine& engine, EffectKind kind, const std::string& path)
{
    int slot = -1;
    engine.editSong ([&] (Song& song) { slot = mixerops::openSampleInMasterEditor (song, kind, path); });
    if (slot < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 (effectInfo (kind).name),
                                                tr ("The Master track has no free effect slot."));
        return;
    }
    newtoneWindows().erase ({ 0, slot }); // (an open window shows the old sample: it opens again with the new one)
    NewtoneWindows::open (engine, 0, slot);
}

void setNewtoneShotState (juce::Component& editor, const juce::String& state)
{
    if (auto* e = dynamic_cast<NewtoneEditor*> (&editor))
        e->shotState (state);
}

juce::PopupMenu newtoneEditMenuForSnapshot (juce::Component& editor)
{
    if (auto* e = dynamic_cast<NewtoneEditor*> (&editor))
        return e->editMenuForSnapshot();
    return {};
}

double newtonePaintMs (juce::Component& editor, int paints)
{
    juce::Image image (juce::Image::ARGB, std::max (1, editor.getWidth()), std::max (1, editor.getHeight()), true);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int k = 0; k < paints; ++k)
    {
        juce::Graphics g (image);
        editor.paintEntireComponent (g, true);
    }
    return (juce::Time::getMillisecondCounterHiRes() - t0) / std::max (1, paints);
}

std::unique_ptr<juce::Component> makeNewtoneEditor (AudioEngine& engine, int track, int slot)
{
    return std::make_unique<NewtoneEditor> (engine, track, slot);
}
} // namespace lynx
