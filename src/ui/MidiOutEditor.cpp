// FL's MIDI Out editor as LYNX SHARE draws it (round 167i, issue #17; REA_rodada167i §1: FL's TSynthEditorForm and
// TCtrlSetForm in "MIDI Out_x64.dll"): channel, patch, bank MSB / LSB and port; 8 pages of 3 × 3 knobs, each a CC,
// RPN, NRPN, aftertouch or note set up by its cog ("Configure..."); Reset, Deactivate all, "Send pan, volume and
// pitch", "Map note color to MIDI channel", the Options menu (what a reset sends) and FL's presets. LYNX's own extras
// stay: "Send to a plugin...", "Automate MIDI controller..." and the list of the plugins its port plays.

#include "ui/ControllerEditors.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/MidiControls.h"
#include "core/MidiOut.h"
#include "core/MidiOutView.h"
#include "engine/PluginHost.h"
#include "engine/Text.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
using midiout::Control;
using midiout::State;

// The General MIDI program names (FL's "GM Instruments.txt" lists the same 128 standard names after "(none)")
const char* const gmPrograms[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2",
    "Harpsichord", "Clavi", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)",
    "Overdriven Guitar", "Distortion Guitar", "Guitar harmonics", "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
    "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass",
    "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani", "String Ensemble 1", "String Ensemble 2", "SynthStrings 1",
    "SynthStrings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
    "French Horn", "Brass Section", "SynthBrass 1", "SynthBrass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe",
    "English Horn", "Bassoon", "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle",
    "Ocarina", "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)",
    "Lead 7 (fifths)", "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)",
    "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)", "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bag pipe",
    "Fiddle", "Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

juce::String utf (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
juce::String nameOf (const std::string& s) { return utf (s); }

// FL's cog: the glyph an unassigned knob shows instead of a name
juce::Path cogPath (juce::Rectangle<float> r)
{
    juce::Path p;
    const auto c = r.getCentre();
    const auto outer = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, inner = outer * 0.72f;
    for (int i = 0; i < 16; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * (float) i / 16.0f;
        const auto rad = (i / 2) % 2 == 0 ? outer : inner;
        const auto pt = c + juce::Point<float> (std::sin (a), -std::cos (a)) * rad;
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    p.closeSubPath();
    p.addEllipse (juce::Rectangle<float> (outer * 0.7f, outer * 0.7f).withCentre (c));
    p.setUsingNonZeroWinding (false);
    return p;
}

juce::Path pencilPath()
{
    juce::Path p;
    p.startNewSubPath (2.0f, 14.0f);
    p.lineTo (3.0f, 10.0f);
    p.lineTo (11.0f, 2.0f);
    p.lineTo (14.0f, 5.0f);
    p.lineTo (6.0f, 13.0f);
    p.closeSubPath();
    return p;
}

Control& clipboardControl()
{
    static Control c;
    return c;
}
bool& clipboardFull()
{
    static bool full = false;
    return full;
}

// ---------------------------------------------------------------------------------------------------
// FL's "Configure..." window of a knob (TCtrlSetForm): type, full / short name, controller, range, channel override,
// note-off value; Copy, Paste, Reset, Accept.
class ControlSettings final : public juce::Component
{
public:
    ControlSettings (const Control& start, std::function<void (const Control&)> accept) : control (start), onAccept (std::move (accept))
    {
        for (int t = 0; t <= 4; ++t)
            type.addItem (t == 3 ? tr ("Channel aftertouch") : t == 4 ? tr ("Note") : juce::String (midiout::typeName ((midiout::Type) t)), t + 1);
        type.onChange = [this] { updateRanges(); };
        for (int c = 0; c <= 16; ++c)
            channel.addItem (c == 0 ? tr ("Default") : juce::String (c), c + 1);
        auto setupNumber = [this] (juce::Slider& s, int lo, int hi) {
            s.setSliderStyle (juce::Slider::IncDecButtons);
            s.setRange (lo, hi, 1);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, numberTextWidth, rowHeight);
            addAndMakeVisible (s);
        };
        setupNumber (number, 0, 127);
        setupNumber (minimum, 0, 16383);
        setupNumber (maximum, 0, 16383);
        setupNumber (noteOff, 0, 16383);
        for (auto* e : { &fullName, &shortName })
            addAndMakeVisible (*e);
        addAndMakeVisible (type);
        addAndMakeVisible (channel);
        const std::pair<juce::Label*, const char*> labels[] = { { &typeLabel, TR_NOOP ("Type") },
                                                                { &fullLabel, TR_NOOP ("Full name") },
                                                                { &shortLabel, TR_NOOP ("Short name") },
                                                                { &numberLabel, TR_NOOP ("Controller") },
                                                                { &rangeLabel, TR_NOOP ("Range") },
                                                                { &channelLabel, TR_NOOP ("Channel override") },
                                                                { &noteOffLabel, TR_NOOP ("Note off") } };
        for (const auto& [label, text] : labels)
        {
            label->setText (tr (text), juce::dontSendNotification);
            label->setFont (theme::font (theme::type::small));
            addAndMakeVisible (*label);
        }
        copy.setButtonText (tr ("Copy"));
        paste.setButtonText (tr ("Paste"));
        reset.setButtonText (tr ("Reset"));
        ok.setButtonText (tr ("Accept"));
        copy.onClick = [this] {
            clipboardControl() = read();
            clipboardFull() = true;
            paste.setEnabled (true);
        };
        paste.onClick = [this] {
            if (clipboardFull())
                show (clipboardControl());
        };
        paste.setEnabled (clipboardFull());
        reset.onClick = [this] { show (Control {}); };
        ok.onClick = [this] {
            const auto c = read();
            auto done = onAccept;
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
            done (c);
        };
        for (auto* b : { &copy, &paste, &reset, &ok })
            addAndMakeVisible (*b);
        show (control);
        setSize (dialogWidth, dialogHeight);
    }

    // Round 167p: every number field has the same size (its - / + buttons too), the window is as tall as its rows
    static constexpr int rowHeight = 24, rowGap = 6, padding = 14, labelWidth = 116, numberTextWidth = 52, numberWidth = 116;
    static constexpr int rows = 7, dialogWidth = padding + labelWidth + 2 * numberWidth + 24 + padding;
    static constexpr int dialogHeight = padding + rows * (rowHeight + rowGap) + 8 + 28 + padding;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (juce::CharPointer_UTF8 ("\xe2\x80\x93"), rangeDash, juce::Justification::centred); // between min and max
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (padding);
        auto row = [&] (juce::Label& l) {
            auto r = area.removeFromTop (rowHeight);
            area.removeFromTop (rowGap);
            l.setBounds (r.removeFromLeft (labelWidth));
            return r;
        };
        type.setBounds (row (typeLabel));
        fullName.setBounds (row (fullLabel));
        shortName.setBounds (row (shortLabel));
        number.setBounds (row (numberLabel).removeFromLeft (numberWidth));
        auto range = row (rangeLabel);
        minimum.setBounds (range.removeFromLeft (numberWidth));
        rangeDash = range.removeFromLeft (24);
        maximum.setBounds (range.removeFromLeft (numberWidth));
        channel.setBounds (row (channelLabel).removeFromLeft (numberWidth));
        noteOff.setBounds (row (noteOffLabel).removeFromLeft (numberWidth));
        area.removeFromTop (8);
        auto buttons = area.removeFromTop (28);
        const auto w = buttons.getWidth() / 4;
        for (auto* b : { &copy, &paste, &reset, &ok })
            b->setBounds (buttons.removeFromLeft (w).reduced (3, 0));
    }

private:
    void show (const Control& c)
    {
        type.setSelectedId ((int) c.type + 1, juce::dontSendNotification);
        fullName.setText (utf (c.name), false);
        shortName.setText (utf (c.shortName), false);
        updateRanges();
        number.setValue (juce::jmax (0, c.number), juce::dontSendNotification);
        minimum.setValue (c.min, juce::dontSendNotification);
        maximum.setValue (c.max, juce::dontSendNotification);
        channel.setSelectedId (c.channel + 2, juce::dontSendNotification);
        noteOff.setValue (c.noteOff, juce::dontSendNotification);
        active = c.active || ! c.assigned();
    }

    void updateRanges()
    {
        const auto t = (midiout::Type) juce::jmax (0, type.getSelectedId() - 1);
        // FL: RPN / NRPN numbers are 14-bit; a note's "controller" is its velocity
        number.setRange (0, t == midiout::Type::rpn || t == midiout::Type::nrpn ? 16383 : 127, 1);
        number.setEnabled (t != midiout::Type::aftertouch);
        numberLabel.setText (t == midiout::Type::note ? tr ("Velocity") : tr ("Controller"), juce::dontSendNotification);
        noteOff.setEnabled (t == midiout::Type::note);
    }

    Control read() const
    {
        Control c = control;
        c.type = (midiout::Type) juce::jlimit (0, 4, type.getSelectedId() - 1);
        c.name = fullName.getText().toStdString();
        c.shortName = shortName.getText().toStdString();
        c.number = (int) number.getValue();
        c.min = (int) minimum.getValue();
        c.max = (int) maximum.getValue();
        c.channel = channel.getSelectedId() - 2;
        c.noteOff = (int) noteOff.getValue();
        c.active = active;
        return c;
    }

    Control control;
    std::function<void (const Control&)> onAccept;
    bool active = true;
    juce::ComboBox type, channel;
    juce::TextEditor fullName, shortName;
    juce::Slider number, minimum, maximum, noteOff;
    juce::Label typeLabel, fullLabel, shortLabel, numberLabel, rangeLabel, channelLabel, noteOffLabel;
    juce::TextButton copy, paste, reset, ok;
    juce::Rectangle<int> rangeDash;
};

// ---------------------------------------------------------------------------------------------------

class MidiOutEditor final : public juce::Component,
                            private juce::ChangeListener
{
public:
    // `locate` finds the MIDI Out's ChannelDef in a song (a Channel Rack channel, or a node inside a Patcher: round
    // 162); `c` = its channel index, −1 inside a Patcher (no channel automation there)
    MidiOutEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        for (int m = 1; m <= 16; ++m)
            midiChannel.addItem (juce::String (m), m);
        midiChannel.onChange = [this] { edit ([v = midiChannel.getSelectedId() - 1] (State& s) { s.channel = juce::jlimit (0, 15, v); }); };
        patch.addItem (tr ("(none)"), 1);
        for (int p = 0; p < 128; ++p)
            patch.addItem (juce::String (p + 1) + " " + gmPrograms[p], p + 2);
        patch.onChange = [this] { edit ([v = patch.getSelectedId() - 1] (State& s) { s.program = juce::jlimit (0, 128, v); }); };
        auto number = [this] (juce::Slider& s, int lo, int hi, std::function<void (State&, int)> set) {
            s.setSliderStyle (juce::Slider::IncDecButtons);
            s.setRange (lo, hi, 1);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 22);
            s.textFromValueFunction = [lo] (double v) { return lo < 0 && v < 0 ? juce::String ("-") : juce::String ((int) v); };
            s.valueFromTextFunction = [] (const juce::String& t) { return t.trim() == "-" ? -1.0 : t.getDoubleValue(); };
            s.updateText();
            s.onValueChange = [this, &s, set] { edit ([set, v = (int) s.getValue()] (State& st) { set (st, v); }); };
            addAndMakeVisible (s);
        };
        number (bankMsb, -1, 127, [] (State& s, int v) { s.bankMsb = v; });
        number (bankLsb, -1, 127, [] (State& s, int v) { s.bankLsb = v; });
        number (port, 0, 255, [] (State& s, int v) { s.port = v; });
        ui::setHint (bankMsb, tr ("Bank MSB (CC 0), sent before the patch; - = not sent"));
        ui::setHint (bankLsb, tr ("Bank LSB (CC 32), sent before the patch; - = not sent"));
        ui::setHint (port, tr ("MIDI output port: the plugins whose MIDI input port is this port play it"));
        ui::setHint (patch, tr ("The patch (program change) this channel selects"));
        addAndMakeVisible (midiChannel);
        addAndMakeVisible (patch);

        page.onChange = [this] { edit ([v = page.getSelectedId() - 1] (State& s) { s.page = juce::jlimit (0, midiout::pageCount - 1, v); }); };
        addAndMakeVisible (page);
        rename.setShape (pencilPath(), false, true, false);
        rename.setOutline (theme::colour::textSecondary, 1.0f);
        rename.onClick = [this] { renamePage(); };
        ui::setHint (rename, tr ("Rename this page"));
        addAndMakeVisible (rename);

        for (int k = 0; k < midiout::knobsPerPage; ++k)
        {
            knobs[(std::size_t) k] = std::make_unique<KnobCell> (*this, k);
            addAndMakeVisible (*knobs[(std::size_t) k]);
        }

        resetButton.setButtonText (tr ("Reset"));
        resetButton.onClick = [this] { sendReset (true); };
        ui::setHint (resetButton, tr ("Reset all controllers (CC #121) and everything"));
        deactivateAll.setButtonText (tr ("Deactivate all"));
        deactivateAll.onClick = [this] {
            edit ([] (State& s) {
                for (auto& c : s.controls)
                    c.active = false;
            });
        };
        ui::setHint (deactivateAll, tr ("Deactivates every control: they send nothing until they get a value"));
        sendPanVol.setButtonText (tr ("Send pan, volume and pitch"));
        sendPanVol.onClick = [this] { edit ([v = sendPanVol.getToggleState()] (State& s) { s.sendPanVolPitch = v; }); };
        ui::setHint (sendPanVol, tr ("The channel's pan, volume and pitch go out as CC 10, CC 7 and pitch bend"));
        mapColour.setButtonText (tr ("Map note color to MIDI channel"));
        mapColour.onClick = [this] { edit ([v = mapColour.getToggleState()] (State& s) { s.mapNoteColour = v; }); };
        ui::setHint (mapColour, tr ("Each note goes out on the MIDI channel of its color (color 1 = channel 1...)"));
        presets.setButtonText (tr ("Presets"));
        presets.onClick = [this] { showPresets(); };
        options.setButtonText (tr ("Options"));
        options.onClick = [this] { showOptions(); };
        for (auto* b : { &resetButton, &deactivateAll, &presets, &options })
            addAndMakeVisible (*b);
        addAndMakeVisible (sendPanVol);
        addAndMakeVisible (mapColour);

        const std::pair<juce::Label*, const char*> labels[] = { { &channelLabel, TR_NOOP ("Channel") }, { &patchLabel, TR_NOOP ("Patch") },
                                                                { &bankLabel, TR_NOOP ("Bank MSB / LSB") }, { &portLabel, TR_NOOP ("Port") },
                                                                { &pageLabel, TR_NOOP ("Page") } };
        for (const auto& [label, text] : labels)
        {
            label->setText (tr (text), juce::dontSendNotification);
            label->setFont (theme::font (theme::type::small));
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (*label);
        }

        connect.setButtonText (tr ("Send to a plugin..."));
        connect.onClick = [this] { showConnectMenu(); };
        ui::setHint (connect, tr ("Sets a plugin channel's MIDI input port to this port, so it plays this channel's notes"));
        addAndMakeVisible (connect);
        automate.setButtonText (tr ("Automate MIDI controller..."));
        automate.onClick = [this] {
            juce::PopupMenu menu;
            addMidiParameterItems (menu, engine, { AutomationTargetKind::channelParam, channel, 0, 0 });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&automate));
        };
        ui::setHint (automate, tr ("CCs, aftertouch and pitch bend this channel sends to the plugins on its port"));
        addAndMakeVisible (automate);
        automate.setVisible (channel >= 0);
        connect.setVisible (channel >= 0);
        listeners.setFont (theme::font (theme::type::small));
        listeners.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        listeners.setJustificationType (juce::Justification::centredLeft);
        listeners.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (listeners);

        engine.addChangeListener (this);
        refresh();
        setSize (editorWidth, editorHeight);
    }

    ~MidiOutEditor() override { engine.removeChangeListener (this); }

    // Round 167p: the editor uses the channel window's whole width (its Misc tab is 800 px wide, so a narrower page
    // left empty margins), the page row lines up with the Channel / Patch columns, and the knob grid has room for each
    // knob's cog (top-right) and name (under it) without touching the next row.
    static constexpr int editorWidth = 800, margin = 16, rowGap = 10, knobRowHeight = 100;
    static constexpr int editorHeight = margin + 44 + rowGap + 26 + rowGap + (8 + 3 * knobRowHeight + 8) + rowGap + 28 + 8 + 30 + margin;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (knobArea.toFloat(), 6.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (margin);

        auto top = area.removeFromTop (44);
        auto column = [&] (juce::Label& l, int w, std::initializer_list<juce::Component*> cs) {
            auto c = top.removeFromLeft (w);
            l.setBounds (c.removeFromTop (18));
            c = c.withSizeKeepingCentre (c.getWidth(), 24);
            const auto each = c.getWidth() / (int) cs.size();
            for (auto* x : cs)
                x->setBounds (c.removeFromLeft (each).reduced (cs.size() > 1 ? 2 : 0, 0));
            top.removeFromLeft (10);
            return l.getBounds().getUnion (c);
        };
        const auto channelColumn = column (channelLabel, 64, { &midiChannel });
        const auto patchWidth = juce::jmax (180, top.getWidth() - 236 - 10 - 120 - 10);
        const auto patchColumn = column (patchLabel, patchWidth, { &patch });
        column (bankLabel, 236, { &bankMsb, &bankLsb });
        column (portLabel, top.getWidth(), { &port });
        area.removeFromTop (rowGap);

        // the page selector under Channel / Patch; Presets and Options on the right (room for the preset arrows that
        // every plugin window header gets, right of Presets)
        auto pageRow = area.removeFromTop (26);
        pageLabel.setBounds (channelColumn.getX(), pageRow.getY(), channelColumn.getWidth(), pageRow.getHeight());
        page.setBounds (patchColumn.getX(), pageRow.getY(), patchColumn.getWidth(), pageRow.getHeight());
        rename.setBounds (juce::Rectangle<int> (page.getRight() + 6, pageRow.getY(), 24, pageRow.getHeight()).reduced (3));
        pageRow.removeFromRight (56);
        presets.setBounds (pageRow.removeFromRight (90));
        pageRow.removeFromRight (6);
        options.setBounds (pageRow.removeFromRight (90));
        area.removeFromTop (rowGap);

        knobArea = area.removeFromTop (8 + 3 * knobRowHeight + 8);
        auto grid = knobArea.reduced (24, 8);
        const auto cw = grid.getWidth() / 3;
        for (int k = 0; k < midiout::knobsPerPage; ++k)
            knobs[(std::size_t) k]->setBounds (grid.getX() + (k % 3) * cw, grid.getY() + (k / 3) * knobRowHeight, cw, knobRowHeight);
        area.removeFromTop (rowGap);

        // FL's bottom bar: Reset, Deactivate all, the two options
        auto buttons = area.removeFromTop (28);
        resetButton.setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (6);
        deactivateAll.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (16);
        const auto toggleWidth = [] (juce::ToggleButton& b) {
            return juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (theme::font (theme::type::body)), b.getButtonText())) + 40;
        };
        sendPanVol.setBounds (buttons.removeFromLeft (juce::jmin (buttons.getWidth() / 2, toggleWidth (sendPanVol))));
        buttons.removeFromLeft (8);
        mapColour.setBounds (buttons);
        area.removeFromTop (8);

        // LYNX's own: send to a plugin's MIDI input, automate a controller, who plays this port
        auto extra = area.removeFromTop (30);
        if (connect.isVisible())
        {
            connect.setBounds (extra.removeFromLeft (180).withSizeKeepingCentre (180, 28));
            extra.removeFromLeft (8);
            automate.setBounds (extra.removeFromLeft (220).withSizeKeepingCentre (220, 28));
            extra.removeFromLeft (12);
        }
        listeners.setBounds (extra);
    }

private:
    // --- one knob of the page: FL's VectorWheel + its label (click: (de)activate; cog: configure) ----------------
    class KnobCell final : public juce::Component
    {
    public:
        KnobCell (MidiOutEditor& o, int k) : owner (o), slot (k) { setRepaintsOnMouseActivity (true); }

        int index() const noexcept { return owner.state.page * midiout::knobsPerPage + slot; }
        const Control& control() const noexcept { return owner.state.controls[(std::size_t) index()]; }
        int value() const noexcept { return owner.state.values[(std::size_t) index()]; }

        ui::ValueSpec spec() const
        {
            const auto& c = control();
            ui::ValueSpec s;
            s.name = owner.knobName (index());
            s.minimum = juce::jmin (c.min, c.max);
            s.maximum = juce::jmax ((double) c.max, s.minimum + 1.0);
            s.interval = 1.0;
            s.defaultValue = s.minimum;
            s.toText = [] (double v) { return juce::String ((int) std::lround (v)); };
            if (owner.channel >= 0)
                s.target = AutomationTarget { AutomationTargetKind::channelParam, owner.channel, 0, index() + 1 };
            return s;
        }

        midiout::view::Cell cell() const { return midiout::view::layout ({ 0, 0, getWidth(), getHeight() }); }
        static juce::Rectangle<int> rect (const midiout::view::Rect& r) { return { r.x, r.y, r.w, r.h }; }
        juce::Rectangle<int> labelArea() const { return rect (cell().label); }
        // the cog's click area: a little larger than the glyph
        juce::Rectangle<int> gearArea() const { return rect (cell().gear).expanded (3); }

        void paint (juce::Graphics& g) override
        {
            using midiout::view::Look;
            const auto& c = control();
            const auto layout = cell();
            const auto look = midiout::view::look (c);
            const auto box = rect (layout.knob).toFloat();
            const auto lo = (double) juce::jmin (c.min, c.max), hi = (double) juce::jmax (c.max, c.min + 1);
            const auto v = juce::jlimit (0.0, 1.0, ((double) value() - lo) / juce::jmax (1.0, hi - lo));
            constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;
            const auto hover = isMouseOverOrDragging() && ! hoverGear && ! labelArea().contains (getMouseXYRelative());
            const auto dim = look == Look::unassigned ? 0.45f : 1.0f;

            g.setColour (theme::colour::bg4.withMultipliedAlpha (dim));
            g.fillEllipse (box.reduced (6.0f));
            juce::Path track;
            const auto radius = box.getWidth() * 0.5f - 2.0f;
            track.addCentredArc (box.getCentreX(), box.getCentreY(), radius, radius, 0.0f, -sweep * 0.5f, sweep * 0.5f, true);
            g.setColour (theme::colour::bg6.withMultipliedAlpha (dim));
            g.strokePath (track, juce::PathStrokeType (3.5f));
            if (look != Look::unassigned)
            {
                juce::Path arc;
                arc.addCentredArc (box.getCentreX(), box.getCentreY(), radius, radius, 0.0f, -sweep * 0.5f, -sweep * 0.5f + sweep * (float) v, true);
                g.setColour (look == Look::sending ? (hover ? theme::colour::accent.brighter (0.25f) : theme::colour::accent)
                                                   : (hover ? theme::colour::textSecondary : theme::colour::textDisabled));
                g.strokePath (arc, juce::PathStrokeType (3.5f));
                // the pointer from the centre, like the other LYNX knobs
                const auto angle = -sweep * 0.5f + sweep * (float) v;
                const auto inner = box.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius - 9.0f);
                const auto outer = box.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * (radius - 4.0f);
                g.drawLine ({ inner, outer }, 2.0f);
                g.setColour (look == Look::sending ? theme::colour::textPrimary : theme::colour::textSecondary);
                g.setFont (theme::font (theme::type::caption, look == Look::sending));
                g.drawText (utf (midiout::view::readout (c, value())), box.reduced (8.0f), juce::Justification::centred);
            }

            // the cog: always there, always "Configure..."
            g.setColour (hoverGear ? theme::colour::accent : look == Look::unassigned ? theme::colour::textSecondary : theme::colour::textDisabled);
            g.fillPath (cogPath (rect (layout.gear).toFloat().reduced (1.5f)));

            if (look == Look::unassigned)
                return;
            const auto labelHover = isMouseOver() && labelArea().contains (getMouseXYRelative());
            g.setColour (look == Look::sending ? theme::colour::textPrimary : labelHover ? theme::colour::textSecondary : theme::colour::textDisabled);
            ui::drawFittingText (g, utf (midiout::label (c)), labelArea().toFloat(), juce::Justification::centredTop,
                                 juce::Font (theme::font (theme::type::small, look == Look::sending)));
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto& c = control();
            const auto overGear = gearArea().contains (e.getPosition());
            if (overGear != hoverGear)
            {
                hoverGear = overGear;
                setMouseCursor (overGear ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            }
            repaint();
            if (overGear)
                ui::setHint (*this, tr ("Configure..."));
            else if (labelArea().contains (e.getPosition()) && c.assigned())
                ui::setHint (*this, tr ("{0}: click to activate / deactivate", { utf (midiout::label (c)) }));
            else
                ui::setHint (*this, c.assigned() ? utf (midiout::description (c)) : tr ("(right click to configure) Unassigned"), true,
                             c.assigned() && owner.channel >= 0);
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hoverGear = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                showMenu();
                return;
            }
            if (gearArea().contains (e.getPosition()))
            {
                owner.configure (index());
                return;
            }
            if (! control().assigned())
                return; // nothing to turn: the cog (or a right-click) sets it up
            if (labelArea().contains (e.getPosition()))
            {
                owner.edit ([i = index()] (State& s) { s.controls[(std::size_t) i].active = ! s.controls[(std::size_t) i].active; });
                return;
            }
            if (ui::isResetClick (e))
            {
                set (spec().defaultValue);
                return;
            }
            drag.begin (e, value());
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! drag.isActive())
                return;
            const auto s = spec();
            set (drag.update (e, s));
            ui::showValueTip (*this, rect (cell().knob), s.text (value()));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            drag.end();
            ui::hideValueTip();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (! control().assigned())
                return;
            const auto s = spec();
            if (const auto v = ui::wheelValue (value(), e, wheel, s))
            {
                set (*v);
                ui::flashValueTip (*this, getLocalBounds(), s.text (value()));
            }
        }

    private:
        // FL: turning a knob switches its control on again and sends it (FUN_00701900)
        void set (double v)
        {
            owner.edit ([i = index(), v = (int) std::lround (v)] (State& s) {
                auto& c = s.controls[(std::size_t) i];
                s.values[(std::size_t) i] = juce::jlimit (juce::jmin (c.min, c.max), juce::jmax (c.min, c.max), v);
                if (c.assigned())
                    c.active = true;
            });
        }

        void showMenu()
        {
            juce::PopupMenu menu;
            menu.addItem (tr ("Configure..."), [this] { owner.configure (index()); });
            menu.addSeparator();
            const auto s = spec();
            ui::addValueItems (menu, *this, &owner.engine, s, value(), [this] (double v) { set (v); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        MidiOutEditor& owner;
        int slot;
        bool hoverGear = false;
        ui::ValueDrag drag;
    };

    juce::String knobName (int i) const
    {
        const auto l = midiout::label (state.controls[(std::size_t) i]);
        return l.empty() ? tr ("Control {0}", { juce::String (i + 1) }) : utf (l);
    }

    void configure (int i)
    {
        auto content = std::make_unique<ControlSettings> (state.controls[(std::size_t) i], [this, i] (const Control& c) {
            edit ([i, c] (State& s) { s.controls[(std::size_t) i] = c; });
        });
        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = tr ("Configure") + " - " + knobName (i);
        o.content.setOwned (content.release());
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = theme::colour::bg2;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        // like LYNX's other dialogs (Export, Customize toolbar...): modal, above the LYNX windows, LYNX title bar
        if (auto* w = o.launchAsync())
        {
            keepAboveMainWindow (*w);
            // On Windows the native title bar's height came out added to the client area once the window was shown,
            // leaving an empty band under the buttons: give the client area back the content's height
            juce::Timer::callAfterDelay (60, [safe = juce::Component::SafePointer<juce::DialogWindow> (w)] {
                if (safe == nullptr)
                    return;
                const auto extra = safe->getHeight() - safe->getContentComponentBorder().getTopAndBottom() - ControlSettings::dialogHeight;
                if (extra > 0)
                    safe->setSize (safe->getWidth(), safe->getHeight() - extra);
            });
        }
    }

    void renamePage()
    {
        auto* box = new juce::AlertWindow (tr ("Rename this page"), {}, juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("name", utf (state.pageNames[(std::size_t) state.page]));
        box->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        box->enterModalState (true, juce::ModalCallbackFunction::create ([this, box, p = state.page, safe = juce::Component::SafePointer<MidiOutEditor> (this)] (int r) {
                                  if (r == 1 && safe != nullptr)
                                  {
                                      const auto name = box->getTextEditorContents ("name").trim().toStdString();
                                      edit ([p, name] (State& s) { s.pageNames[(std::size_t) p] = name; });
                                  }
                              }),
                              true);
    }

    void sendReset (bool allControllersFirst)
    {
        if (channel < 0)
            return;
        if (auto instrument = engine.getChannelInstrument (channel))
            midiout::requestReset (*instrument, allControllersFirst);
    }

    void showOptions()
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Send all control values now"), channel >= 0, false, [this] { sendReset (false); });
        menu.addSectionHeader (tr ("On reset"));
        const std::pair<int, const char*> flags[] = { { midiout::resetProgram, TR_NOOP ("Send program change") },
                                                      { midiout::resetPan, TR_NOOP ("Send pan") },
                                                      { midiout::resetVolume, TR_NOOP ("Send volume") },
                                                      { midiout::resetPitch, TR_NOOP ("Send pitch") },
                                                      { midiout::resetBendRange, TR_NOOP ("Send pitch bend range") },
                                                      { midiout::resetCutNotes, TR_NOOP ("Cut playing notes") } };
        for (const auto& [flag, text] : flags)
            menu.addItem (tr (text), true, (state.resetFlags & flag) != 0, [this, f = flag] { edit ([f] (State& s) { s.resetFlags ^= f; }); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&options));
    }

    // --- presets: FL's own (.fst in Data\Patches\Plugin presets\Generators\MIDI Out) and the user's -------------
    static juce::File flPresetFolder()
    {
        const auto roots = findFlStudioRoots();
        if (roots.data.empty())
            return {};
        return juce::File (utf (roots.data)).getChildFile ("Patches").getChildFile ("Plugin presets").getChildFile ("Generators").getChildFile ("MIDI Out");
    }
    static juce::File userPresetFolder() { return PluginHost::settingsFolder().getChildFile ("Presets").getChildFile ("MIDI Out"); }

    void addFolder (juce::PopupMenu& menu, const juce::File& folder)
    {
        auto sorted = [] (juce::Array<juce::File> files) {
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
            return files;
        };
        for (const auto& d : sorted (folder.findChildFiles (juce::File::findDirectories, false)))
        {
            juce::PopupMenu sub;
            addFolder (sub, d);
            if (sub.getNumItems() > 0)
                menu.addSubMenu (d.getFileName(), sub);
        }
        for (const auto& f : sorted (folder.findChildFiles (juce::File::findFiles, false, "*.fst")))
            menu.addItem (f.getFileNameWithoutExtension(), [this, f] { loadPreset (f); });
    }

    void showPresets()
    {
        juce::PopupMenu menu, fl, mine;
        if (const auto f = flPresetFolder(); f.isDirectory())
            addFolder (fl, f);
        addFolder (mine, userPresetFolder());
        if (fl.getNumItems() > 0)
            menu.addSubMenu (tr ("FL Studio presets"), fl);
        if (mine.getNumItems() > 0)
            menu.addSubMenu (tr ("My presets"), mine);
        if (menu.getNumItems() > 0)
            menu.addSeparator();
        menu.addItem (tr ("Load preset..."), [this] {
            chooser = std::make_unique<juce::FileChooser> (tr ("Load preset..."), flPresetFolder().isDirectory() ? flPresetFolder() : userPresetFolder(), "*.fst");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
                if (fc.getResult().existsAsFile())
                    loadPreset (fc.getResult());
            });
        });
        menu.addItem (tr ("Save preset as..."), [this] {
            userPresetFolder().createDirectory();
            chooser = std::make_unique<juce::FileChooser> (tr ("Save preset as..."), userPresetFolder().getChildFile ("MIDI Out.fst"), "*.fst");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file == juce::File())
                    return;
                file = file.withFileExtension ("fst");
                const auto bytes = midiout::writeFst (state);
                if (! file.replaceWithData (bytes.data(), bytes.size()))
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Presets"), tr ("Could not write {0}", { file.getFullPathName() }));
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presets));
    }

    void loadPreset (const juce::File& file)
    {
        juce::MemoryBlock data;
        std::vector<std::uint8_t> blob;
        if (file.loadFileAsData (data))
            blob = midiout::stateFromFst ({ (const std::uint8_t*) data.getData(), (const std::uint8_t*) data.getData() + data.getSize() });
        const auto loaded = midiout::parse (blob);
        if (! loaded)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Presets"), tr ("Could not read {0}", { file.getFileName() }));
            return;
        }
        // FL keeps the channel and port of the instance when it loads a preset (they say where it plays, not what)
        edit ([p = *loaded] (State& s) {
            const auto ch = s.channel, port = s.port;
            s = p;
            s.channel = ch;
            s.port = port;
        });
    }

    // --- song access ---------------------------------------------------------------------------------------------
    State current() const
    {
        const auto* c = locate (const_cast<Song&> (engine.getSong()));
        return c != nullptr ? midiout::stateOf (c->pluginState) : State {};
    }

    void edit (std::function<void (State&)> change)
    {
        engine.editSong ([this, change] (Song& song) {
            auto* c = locate (song);
            if (c == nullptr || c->instrument != InstrumentKind::midiOut)
                return;
            auto s = midiout::stateOf (c->pluginState);
            change (s);
            c->pluginState = midiout::write (s);
        });
        refresh();
    }

    void showConnectMenu()
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        const auto p = state.port;
        for (int c = 0; c < (int) song.channels.size(); ++c)
            if (song.channels[(std::size_t) c].instrument == InstrumentKind::plugin)
                menu.addItem (nameOf (song.channels[(std::size_t) c].name), true, song.channels[(std::size_t) c].plugin.midiInPort == p, [this, c, p] {
                    engine.editSong ([c, p] (Song& s) {
                        auto& port = s.channels[(std::size_t) c].plugin.midiInPort;
                        port = port == p ? -1 : p;
                    });
                });
        if (menu.getNumItems() == 0)
            menu.addItem (tr ("(no plugin channel in the Channel Rack)"), false, false, [] {});
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&connect));
    }

    void refresh()
    {
        state = current();
        const auto& s = state;
        midiChannel.setSelectedId (s.channel + 1, juce::dontSendNotification);
        patch.setSelectedId (s.program + 1, juce::dontSendNotification);
        bankMsb.setValue (s.bankMsb, juce::dontSendNotification);
        bankLsb.setValue (s.bankLsb, juce::dontSendNotification);
        port.setValue (s.port, juce::dontSendNotification);
        page.clear (juce::dontSendNotification);
        for (int p = 0; p < midiout::pageCount; ++p)
        {
            const auto& name = s.pageNames[(std::size_t) p];
            page.addItem (juce::String (p + 1) + ". " + (name.empty() ? tr ("Page {0}", { juce::String (p + 1) }) : utf (name)), p + 1);
        }
        page.setSelectedId (s.page + 1, juce::dontSendNotification);
        sendPanVol.setToggleState (s.sendPanVolPitch, juce::dontSendNotification);
        mapColour.setToggleState (s.mapNoteColour, juce::dontSendNotification);
        for (auto& k : knobs)
            k->repaint();

        if (channel < 0) // inside a Patcher (round 162)
        {
            listeners.setText (tr ("Inside a Patcher: cable its \"MIDI out\" pin to an instrument or to a \"MIDI port\" pin of \"To LYNX SHARE\"; "
                                   "its notes go out on this MIDI channel."),
                               juce::dontSendNotification);
            return;
        }
        juce::StringArray names;
        const auto& song = engine.getSong();
        for (const auto& c : song.channels)
            if (c.instrument == InstrumentKind::plugin && c.plugin.midiInPort == s.port)
                names.add (nameOf (c.name));
        listeners.setText (names.isEmpty() ? tr ("No plugin listens on port {0} yet: use \"Send to a plugin...\" or the plugin's Processing > MIDI input port.",
                                                 { juce::String (s.port) })
                                           : tr ("Plays: {0}", { names.joinIntoString (", ") }),
                           juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel;
    State state;
    juce::ComboBox midiChannel, patch, page;
    juce::Slider bankMsb, bankLsb, port;
    juce::ShapeButton rename { "rename", theme::colour::textSecondary, theme::colour::textPrimary, theme::colour::accent };
    std::array<std::unique_ptr<KnobCell>, midiout::knobsPerPage> knobs;
    juce::Rectangle<int> knobArea;
    juce::TextButton resetButton, deactivateAll, presets, options, connect, automate;
    juce::ToggleButton sendPanVol, mapColour;
    juce::Label channelLabel, patchLabel, bankLabel, portLabel, pageLabel, listeners;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

std::unique_ptr<juce::Component> makeMidiOutEditor (AudioEngine& engine, int channel)
{
    return std::make_unique<MidiOutEditor> (engine, [channel] (Song& song) -> ChannelDef* {
        return channel >= 0 && channel < (int) song.channels.size() ? &song.channels[(std::size_t) channel] : nullptr;
    }, channel);
}

std::unique_ptr<juce::Component> makeMidiOutEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate)
{
    return std::make_unique<MidiOutEditor> (engine, std::move (locate), -1);
}
} // namespace lynx
