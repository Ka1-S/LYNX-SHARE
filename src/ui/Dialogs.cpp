#include "ui/Dialogs.h"

#include <cmath>
#include <functional>

#include <set>

#include "core/ClipOps.h"
#include "core/MixerOps.h"
#include "core/RackOps.h"
#include "engine/AudioEngine.h"
#include "engine/MidiFileIo.h"
#include "ui/ColourPicker.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
// a row of swatches: "automatic" first, then the palette
class Swatches final : public juce::Component
{
public:
    explicit Swatches (std::uint32_t c) : colour (c) { setSize (13 * (size + gap), size + 4); }

    std::uint32_t colour;
    std::function<void()> onPick; // a swatch was clicked (or F2)

    void paint (juce::Graphics& g) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
        {
            const auto r = cell (i).toFloat();
            const auto value = i == 0 ? 0u : palette[(std::size_t) i - 1];

            if (i == 0)
            {
                g.setColour (theme::colour::bg4);
                g.fillRoundedRectangle (r, theme::radius::sm);
                g.setColour (theme::colour::textSecondary);
                g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getY() + 5.0f, 1.2f);
            }
            else
            {
                g.setColour (juce::Colour (value));
                g.fillRoundedRectangle (r, theme::radius::sm);
            }

            if (value == colour)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawRoundedRectangle (r.expanded (1.5f), theme::radius::md, 2.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto& palette = lynxPalette();

        for (int i = 0; i <= (int) palette.size(); ++i)
            if (cell (i).expanded (1).contains (e.getPosition()))
            {
                colour = i == 0 ? 0u : palette[(std::size_t) i - 1];
                repaint();
                if (onPick)
                    onPick();
            }
    }

    void pickRandom()
    {
        colour = randomPaletteColour (colour, (std::uint32_t) juce::Time::getMillisecondCounter());
        repaint();
        if (onPick)
            onPick();
    }

private:
    static constexpr int size = 22, gap = 6;
    juce::Rectangle<int> cell (int i) const { return { 2 + i * (size + gap), 2, size, size }; }
};

class NameAndColourWindow final : public juce::AlertWindow
{
public:
    NameAndColourWindow (const juce::String& title, const juce::String& name, std::uint32_t colour)
        : juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon), swatches (colour)
    {
        addTextEditor ("name", name, tr ("Name"));
        addCustomComponent (&swatches);
        hint.setText (tr ("Click a color, pick any color below, or press F2 for a random one"), juce::dontSendNotification);
        hint.setFont (theme::font (theme::type::small));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setSize (swatches.getWidth(), 18);
        addCustomComponent (&hint);
        // any colour: wheel, H / S / V and hex (round 162d, issue #9)
        picker.setSize (swatches.getWidth(), picker.preferredHeight());
        picker.setOriginal (colour != 0 ? juce::Colour (colour) : theme::colour::bg4);
        if (colour != 0)
            picker.setColour (juce::Colour (colour), false);
        picker.onChange = [this] (juce::Colour c) {
            swatches.colour = c.getARGB();
            swatches.repaint();
        };
        swatches.onPick = [this] {
            if (swatches.colour != 0)
                picker.setColour (juce::Colour (swatches.colour), false);
        };
        addCustomComponent (&picker);
        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));

        if (auto* editor = getTextEditor ("name"))
            editor->selectAll();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::F2Key)
        {
            swatches.pickRandom();
            return true;
        }

        return juce::AlertWindow::keyPressed (key);
    }

    std::uint32_t colour() const { return swatches.colour; }

private:
    Swatches swatches;
    juce::Label hint;
    ui::ColourPicker picker { juce::Colours::grey, true };
};

// One big choice of the record dialog: a title line and a note below it. The note is never cut (round 190cj: the
// Russian notes ended in "..."): the dialog gives every choice the width its texts need, up to `maxWidth`, and a
// note longer than that goes on as many lines as it takes (the choice grows).
class RecordChoiceButton final : public juce::Button
{
public:
    static constexpr int minWidth = 380, maxWidth = 620, pad = 12;

    RecordChoiceButton (const juce::String& title, const juce::String& note)
        : juce::Button ({}), noteText (note) // (no component name: the alert window would print it above)
    {
        setButtonText (title);
        fit (minWidth);
    }

    // the width at which neither text is cut nor wrapped
    int wantedWidth() const
    {
        using namespace theme;
        return 2 * pad + 4 + juce::jmax (juce::GlyphArrangement::getStringWidthInt (font (type::body, true), getButtonText()),
                                         juce::GlyphArrangement::getStringWidthInt (font (type::small), noteText));
    }

    void fit (int width)
    {
        setSize (width, 24 + noteHeight (width) + 6);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (down ? colour::bg5 : over ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (r, radius::md);
        g.setColour (over ? colour::accent : colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.0f);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (getButtonText(), getLocalBounds().reduced (pad, 0).withTrimmedTop (5).withHeight (18), juce::Justification::centredLeft, true);
        noteLayout (getWidth()).draw (g, getLocalBounds().reduced (pad, 0).withTrimmedTop (24).toFloat());
    }

private:
    juce::TextLayout noteLayout (int width) const
    {
        juce::AttributedString text;
        text.append (noteText, theme::font (theme::type::small), theme::colour::textSecondary);
        text.setWordWrap (juce::AttributedString::byWord);
        text.setJustification (juce::Justification::topLeft);
        juce::TextLayout layout;
        layout.createLayout (text, (float) juce::jmax (40, width - 2 * pad));
        return layout;
    }

    int noteHeight (int width) const { return juce::jmax (16, (int) std::ceil (noteLayout (width).getHeight())); }

    juce::String noteText;
};

class RecordChoiceWindow final : public juce::AlertWindow
{
public:
    RecordChoiceWindow()
        : juce::AlertWindow (tr ("Recording"), tr ("What would you like to record?"), juce::MessageBoxIconType::NoIcon)
    {
        dontAsk.setButtonText (tr ("Don't ask again (right-click the record button for the recording filter)"));
        // as wide as the longest text needs (round 190cj), within the choices' limits; longer notes wrap
        auto width = juce::jmax ((int) RecordChoiceButton::minWidth,
                                 juce::GlyphArrangement::getStringWidthInt (getLookAndFeel().getAlertWindowMessageFont(), dontAsk.getButtonText()) + 44);
        for (auto* b : { &audio, &notes, &everything, &editor })
            width = juce::jmax (width, b->wantedWidth());
        width = juce::jmin ((int) RecordChoiceButton::maxWidth, width);
        int result = recordChoiceAudio;
        for (auto* b : { &audio, &notes, &everything, &editor })
        {
            b->fit (width);
            b->onClick = [this, result] { exitModalState (result); };
            addCustomComponent (b);
            ++result;
        }
        dontAsk.setSize (width, 24);
        addCustomComponent (&dontAsk);
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    bool dontAskAgain() const { return dontAsk.getToggleState(); }

private:
    RecordChoiceButton audio { tr ("Audio, into the Playlist as an audio clip"),
                               tr ("Arms the selected mixer insert; choose its audio input (microphone) if it has none") },
        notes { tr ("Notes and automation"), tr ("Notes played on a MIDI keyboard or the PC keyboard, and the controls you move") },
        everything { tr ("Everything"), tr ("Notes, automation, and audio from the armed inserts") },
        editor { tr ("Audio, into the audio editor"), tr ("Records the audio input into a new audio editor while the song plays") };
    juce::ToggleButton dontAsk;
};
} // namespace

std::unique_ptr<juce::AlertWindow> makeRecordChoiceDialog()
{
    return std::make_unique<RecordChoiceWindow>();
}

namespace
{
// FL's "Import MIDI data" window (REA rounds 143 / 146b, TMIDIImportForm): its options as LYNX can apply them
class MidiChannelFilter final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    std::uint16_t mask = 0xFFFF;

    MidiChannelFilter() { setSize (240, 44); setTooltip (tr ("Deselect MIDI channels to filter out (right-click: only this one)")); }

    // FL's grid: two rows of eight (REA round 149 [EXEC])
    juce::Rectangle<float> cell (int c) const
    {
        const auto w = getWidth() / 8.0f, h = getHeight() / 2.0f;
        return { (float) (c % 8) * w, (float) (c / 8) * h, w, h };
    }

    void paint (juce::Graphics& g) override
    {
        for (int c = 0; c < 16; ++c)
        {
            const auto r = cell (c).reduced (1.5f);
            const auto on = (mask >> c & 1) != 0;
            g.setColour (on ? theme::colour::accent : theme::colour::bg3);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (on ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (juce::String (c + 1), r, juce::Justification::centred);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto c = juce::jlimit (0, 15, juce::jlimit (0, 7, (int) (e.x / (getWidth() / 8.0f))) + (e.y >= getHeight() / 2 ? 8 : 0));
        if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
            mask = mask == (std::uint16_t) (1u << c) ? (std::uint16_t) 0xFFFF : (std::uint16_t) (1u << c); // solo / all
        else
            mask ^= (std::uint16_t) (1u << c);
        repaint();
    }
};

class MidiImportWindow final : public juce::AlertWindow
{
public:
    MidiImportWindow (const juce::String& fileName, const midiimport::ParsedMidi& midi, const midiimport::Options& o, bool pianoRoll, bool eventLane = false)
        : juce::AlertWindow (tr ("Import MIDI data"), fileName, juce::MessageBoxIconType::NoIcon),
          roll (pianoRoll),
          lane (eventLane),
          kept (o)
    {
        auto add = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& hint, bool on) {
            b.setButtonText (text);
            b.setTooltip (hint);
            b.setToggleState (on, juce::dontSendNotification);
            content.addAndMakeVisible (b);
            optionBoxes.push_back (&b);
        };
        // FL's two columns of options (REA round 149 [EXEC]): new project / realign / time signatures, then one channel
        // per track / mixer tracks / zero velocity notes
        if (eventLane) // FL's Event editor (REA_rodada178a §11): no Blend and no project options
        {
            add (realign, tr ("Realign events"), tr ("Removes any silent part at the beginning"), o.realign);
            add (timeSignatures, tr ("Import time signatures"), tr ("Adds the file's time signature to the pattern's markers"), o.timeSignatures);
            add (zeroVelocity, tr ("Import zero velocity notes"), tr ("Don't treat note on messages with zero velocity as note off"), o.zeroVelocityNotes);
        }
        else if (pianoRoll)
        {
            add (blend, tr ("Blend with existing data"), tr ("Don't delete the channel's notes before importing"), o.blend);
            add (realign, tr ("Realign events"), tr ("Removes any silent part at the beginning"), o.realign);
            add (zeroVelocity, tr ("Import zero velocity notes"), tr ("Don't treat note on messages with zero velocity as note off"), o.zeroVelocityNotes);
        }
        else
        {
            add (newProject, tr ("Start new project"), tr ("Start a blank project, or add to the open one"), o.newProject);
            add (realign, tr ("Realign events"), tr ("Removes any silent part at the beginning"), o.realign);
            add (timeSignatures, tr ("Import time signatures"), tr ("The file's time signature becomes the new project's"), o.timeSignatures);
            add (perTrack, tr ("Create one channel per track"), tr ("One channel per track, or per MIDI channel"), o.perTrack);
            add (mixerTracks, tr ("Set mixer tracks for new channels"), tr ("Route new channels to consecutive mixer tracks"), o.mixerTracks);
            add (zeroVelocity, tr ("Import zero velocity notes"), tr ("Don't treat note on messages with zero velocity as note off"), o.zeroVelocityNotes);
        }

        // Which tracks to import?
        tracks.addItem (tr ("All tracks"), 1);
        trackIds.push_back (-1);
        for (const auto t : midiimport::tracksWithContent (midi))
        {
            const auto& name = t < (int) midi.trackNames.size() ? midi.trackNames[(std::size_t) t] : std::string();
            tracks.addItem (juce::String (t + 1) + ". " + (name.empty() ? tr ("Track {0}", { juce::String (t + 1) }) : juce::String::fromUTF8 (name.c_str())),
                            (int) trackIds.size() + 1);
            trackIds.push_back (t);
        }
        tracks.setSelectedId (1, juce::dontSendNotification);
        for (std::size_t i = 0; i < trackIds.size(); ++i)
            if (trackIds[i] == o.track)
                tracks.setSelectedId ((int) i + 1, juce::dontSendNotification);

        // Which channels to import?
        channels.mask = o.channels;

        // Channel type (FL: FLEX / MIDI Out…; LYNX: its synth or a MIDI Out on the note's MIDI channel)
        channelType.addItem (tr ("LYNX SHARE synth"), 1);
        channelType.addItem (tr ("MIDI Out"), 2);
        channelType.setSelectedId (o.midiOut ? 2 : 1, juce::dontSendNotification);
        channelType.setTooltip (tr ("MIDI Out channels play the plugins on their MIDI port, on the note's MIDI channel"));

        // Which controller to import? (the ones LYNX applies: 7 / 11 volume, 10 pan, when the file has them)
        controller.addItem (eventLane ? juce::String ("---") : tr ("None"), 1); // (FL's "---" in the Event editor: every controller)
        controllerIds.push_back (-1);
        std::set<int> present;
        for (const auto& e : midi.events)
            if ((e.status & 0xF0) == 0xB0)
                present.insert (e.data1);
        if (eventLane) // FL: any controller, typed 0..127 (the lane is the shown control's, not the controller's own)
            for (int cc = 0; cc < 128; ++cc)
            {
                controller.addItem (juce::String (cc) + (present.count (cc) > 0 ? " *" : ""), (int) controllerIds.size() + 1);
                controllerIds.push_back (cc);
            }
        else
        for (const auto& [cc, label] : { std::pair<int, const char*> { 7, TR_NOOP ("CC 7 (volume) → channel volume") },
                                         { 11, TR_NOOP ("CC 11 (expression) → channel volume") }, { 10, TR_NOOP ("CC 10 (pan) → channel pan") } })
            if (present.count (cc) > 0 || cc == o.controller)
            {
                controller.addItem (tr (label), (int) controllerIds.size() + 1);
                controllerIds.push_back (cc);
            }
        controller.setSelectedId (1, juce::dontSendNotification);
        for (std::size_t i = 0; i < controllerIds.size(); ++i)
            if (controllerIds[i] == o.controller)
                controller.setSelectedId ((int) i + 1, juce::dontSendNotification);
        controller.setTooltip (tr ("Which controller to import? (into the pattern's events)"));

        // FL's order: the selections on top (label on the left, the control on the right), the options below
        auto label = [this] (const juce::String& text) {
            auto l = std::make_unique<juce::Label> (juce::String(), text);
            l->setFont (theme::font (theme::type::body));
            l->setJustificationType (juce::Justification::centredRight);
            content.addAndMakeVisible (*l);
            labels.push_back (std::move (l));
        };
        label (tr ("Which tracks to import?"));
        content.addAndMakeVisible (tracks);
        label (tr ("Which channels to import?"));
        content.addAndMakeVisible (channels);
        if (! pianoRoll && ! eventLane)
        {
            label (tr ("Channel type"));
            content.addAndMakeVisible (channelType);
        }
        label (eventLane ? tr ("Which controller to import?") : tr ("Which controller?"));
        content.addAndMakeVisible (controller);

        content.onLayout = [this] { layout(); };
        const auto rows = pianoRoll || eventLane ? 3 : 4;
        content.setSize (440, rows * 30 + 22 + ((int) optionBoxes.size() + 1) / 2 * 26 + 12);
        addCustomComponent (&content);

        addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    midiimport::Options options() const
    {
        auto o = kept;
        if (lane)
            o.timeSignatures = timeSignatures.getToggleState();
        else if (roll)
            o.blend = blend.getToggleState();
        else
        {
            o.newProject = newProject.getToggleState();
            o.perTrack = perTrack.getToggleState();
            o.timeSignatures = timeSignatures.getToggleState();
            o.mixerTracks = mixerTracks.getToggleState();
            o.midiOut = channelType.getSelectedId() == 2;
        }
        o.realign = realign.getToggleState();
        o.zeroVelocityNotes = zeroVelocity.getToggleState();
        const auto t = tracks.getSelectedId() - 1;
        o.track = t >= 0 && t < (int) trackIds.size() ? trackIds[(std::size_t) t] : -1;
        o.channels = channels.mask;
        const auto c = controller.getSelectedId() - 1;
        o.controller = c >= 0 && c < (int) controllerIds.size() ? controllerIds[(std::size_t) c] : -1;
        return o;
    }

private:
    struct Content final : public juce::Component
    {
        std::function<void()> onLayout;
        void resized() override
        {
            if (onLayout)
                onLayout();
        }
    };

    void layout()
    {
        auto area = content.getLocalBounds();
        std::size_t l = 0;
        auto row = [&] (juce::Component& c, int height) {
            auto r = area.removeFromTop (height);
            area.removeFromTop (6);
            if (l < labels.size())
                labels[l++]->setBounds (r.removeFromLeft (190));
            r.removeFromLeft (8);
            c.setBounds (r);
        };
        row (tracks, 24);
        row (channels, 44);
        if (! roll && ! lane)
            row (channelType, 24);
        row (controller, 24);
        area.removeFromTop (6);
        const auto half = (int) (optionBoxes.size() + 1) / 2;
        auto left = area.removeFromLeft (area.getWidth() / 2);
        for (std::size_t i = 0; i < optionBoxes.size(); ++i)
            optionBoxes[i]->setBounds (((int) i < half ? left : area).removeFromTop (26));
    }

    bool roll = false, lane = false;
    midiimport::Options kept;
    Content content;
    juce::ToggleButton newProject, perTrack, realign, timeSignatures, mixerTracks, zeroVelocity, blend;
    std::vector<juce::ToggleButton*> optionBoxes;
    juce::ComboBox tracks, controller, channelType;
    std::vector<int> trackIds, controllerIds;
    MidiChannelFilter channels;
    std::vector<std::unique_ptr<juce::Label>> labels;
};
} // namespace

void showMidiImportDialog (const juce::String& fileName, const midiimport::ParsedMidi& midi, const midiimport::Options& current, bool pianoRoll,
                           std::function<void (const midiimport::Options&)> onAccept)
{
    auto* window = new MidiImportWindow (fileName, midi, current, pianoRoll);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onAccept] (int result) {
                                 if (result == 1)
                                     onAccept (window->options());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

void showEventLaneMidiImportDialog (const juce::String& fileName, const midiimport::ParsedMidi& midi, const midiimport::Options& current,
                                    std::function<void (const midiimport::Options&)> onAccept)
{
    auto* window = new MidiImportWindow (fileName, midi, current, false, true);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onAccept] (int result) {
                                 if (result == 1)
                                     onAccept (window->options());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeMidiImportDialog()
{
    midiimport::ParsedMidi midi;
    midi.trackNames = { "", "Piano", "Strings" };
    midi.events = { { 1, 0, 0x90, 60, 100 }, { 1, 0, 0xB0, 7, 100 }, { 2, 0, 0x91, 64, 90 } };
    return std::make_unique<MidiImportWindow> ("song.mid", midi, midiimport::Options {}, false);
}

void showRecordChoiceDialog (std::function<void (int, bool)> onChoice)
{
    auto* window = new RecordChoiceWindow();
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onChoice] (int result) {
                                 if (result > 0)
                                     onChoice (result, window->dontAskAgain());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour)
{
    return std::make_unique<NameAndColourWindow> (title, name, colour);
}

void showNameAndColourDialog (const juce::String& title, const juce::String& name, std::uint32_t colour,
                              std::function<void (const juce::String&, std::uint32_t)> onDone)
{
    auto* window = new NameAndColourWindow (title, name, colour);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onDone] (int result) {
                                 const auto typed = window->getTextEditorContents ("name").trim();

                                 if (result == 1 && typed.isNotEmpty())
                                     onDone (typed, window->colour());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

namespace
{
// FL's "Gradient editor" (Gradient color..., REA_rodada177a §5 [SHOT pairs/fl-gradient-editor.png]): a First and a Last
// swatch with the blend between them, buttons to copy First into Last, swap them and copy Last into First, and a colour
// picker for the swatch being edited. LYNX's picker replaces FL's colour selector.
class GradientPanel final : public juce::Component
{
public:
    GradientPanel (juce::Colour firstColour, juce::Colour lastColour, int count)
        : first (firstColour), last (lastColour), steps (juce::jmax (2, count))
    {
        const auto setup = [this] (juce::TextButton& b, const juce::String& text, const juce::String& tip) {
            b.setButtonText (text);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setup (toLast, juce::String::fromUTF8 ("\xe2\x86\x92"), tr ("Copy First into Last"));
        setup (swap, juce::String::fromUTF8 ("\xe2\x87\x84"), tr ("Swap First and Last"));
        setup (toFirst, juce::String::fromUTF8 ("\xe2\x86\x90"), tr ("Copy Last into First"));
        toLast.onClick = [this] { last = first; changed (true); };
        toFirst.onClick = [this] { first = last; changed (true); };
        swap.onClick = [this] { std::swap (first, last); changed (true); };
        setSize (360, 132);
    }

    juce::Colour first, last;
    bool editingFirst = true;
    std::function<void (juce::Colour)> onEdit; // the swatch being edited changed: the picker shows this colour

    juce::Colour editing() const { return editingFirst ? first : last; }
    void setEditing (juce::Colour c)
    {
        (editingFirst ? first : last) = c;
        repaint();
    }

    void resized() override
    {
        const auto row = getLocalBounds().removeFromBottom (28);
        const auto w = 56;
        toLast.setBounds (firstArea().getX(), row.getY(), w, row.getHeight());
        swap.setBounds (previewArea().getCentreX() - w / 2, row.getY(), w, row.getHeight());
        toFirst.setBounds (lastArea().getRight() - w, row.getY(), w, row.getHeight());
    }

    void paint (juce::Graphics& g) override
    {
        const auto swatch = [&g] (juce::Rectangle<int> r, juce::Colour c, const juce::String& label, bool on) {
            g.setColour (c);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (on ? theme::colour::accent : theme::colour::borderStrong);
            g.drawRoundedRectangle (r.toFloat().reduced (on ? 1.0f : 0.5f), 4.0f, on ? 2.0f : 1.0f);
            g.setColour (c.getPerceivedBrightness() > 0.6f ? juce::Colours::black : juce::Colours::white);
            g.setFont (theme::font (theme::type::small));
            g.drawText (label, r, juce::Justification::centred);
            if (on) // (FL ticks the swatch being edited; the outline alone vanishes on a red swatch)
                g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93"), r.reduced (6), juce::Justification::bottomRight);
        };
        swatch (firstArea(), first, tr ("First"), editingFirst);
        swatch (lastArea(), last, tr ("Last"), ! editingFirst);
        // the blend the tracks will get, one band per selected track
        const auto p = previewArea();
        for (int k = 0; k < steps; ++k)
        {
            const auto x0 = p.getX() + p.getWidth() * k / steps, x1 = p.getX() + p.getWidth() * (k + 1) / steps;
            g.setColour (juce::Colour (mixerops::gradientColourStep (first.getARGB(), last.getARGB(), k, steps)));
            g.fillRect (x0, p.getY(), x1 - x0, p.getHeight());
        }
        g.setColour (theme::colour::borderStrong);
        g.drawRect (p);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (firstArea().contains (e.getPosition()) || lastArea().contains (e.getPosition()))
        {
            editingFirst = firstArea().contains (e.getPosition());
            changed (true);
        }
    }

private:
    juce::Rectangle<int> swatchRow() const { return getLocalBounds().withTrimmedBottom (36); }
    juce::Rectangle<int> firstArea() const { return swatchRow().withWidth (72); }
    juce::Rectangle<int> lastArea() const { return swatchRow().withLeft (getWidth() - 72); }
    juce::Rectangle<int> previewArea() const { return swatchRow().withLeft (82).withRight (getWidth() - 82).reduced (0, 10); }

    void changed (bool tellPicker)
    {
        repaint();
        if (tellPicker && onEdit != nullptr)
            onEdit (editing());
    }

    int steps;
    juce::TextButton toLast, swap, toFirst;
};

class GradientWindow final : public juce::AlertWindow
{
public:
    GradientWindow (juce::Colour first, juce::Colour last, int count)
        : juce::AlertWindow (tr ("Gradient editor"), {}, juce::MessageBoxIconType::NoIcon), panel (first, last, count)
    {
        addCustomComponent (&panel);
        hint.setText (tr ("Click First or Last, then pick its color below"), juce::dontSendNotification);
        hint.setFont (theme::font (theme::type::small));
        hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        hint.setSize (panel.getWidth(), 18);
        addCustomComponent (&hint);
        picker.setSize (panel.getWidth(), picker.preferredHeight());
        picker.setOriginal (first);
        picker.setColour (first, false);
        picker.onChange = [this] (juce::Colour c) { panel.setEditing (c); };
        panel.onEdit = [this] (juce::Colour c) {
            picker.setOriginal (c);
            picker.setColour (c, false);
        };
        addCustomComponent (&picker);
        // FL's "Reset all" (ResetAllBtnClick 0x7b60c0: every swatch back to the colour the editor opened with, read
        // back from its saved "=$hex" list, REA_rodada177a §5); it doesn't close the editor
        resetAll.setButtonText (tr ("Reset all"));
        resetAll.onClick = [this, first, last] {
            panel.first = first;
            panel.last = last;
            panel.onEdit (panel.editing());
            panel.repaint();
        };
        resetRow.addAndMakeVisible (resetAll);
        resetRow.setSize (panel.getWidth(), 28);
        resetAll.setBounds (0, 0, 96, 28);
        addCustomComponent (&resetRow);
        addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    juce::Colour first() const { return panel.first; }
    juce::Colour last() const { return panel.last; }

private:
    GradientPanel panel;
    juce::Label hint;
    juce::Component resetRow;
    juce::TextButton resetAll;
    ui::ColourPicker picker { juce::Colours::red };
};
} // namespace

void showGradientColourDialog (juce::Colour first, juce::Colour last, int count, std::function<void (juce::Colour, juce::Colour)> onAccept)
{
    auto* window = new GradientWindow (first, last, count);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onAccept] (int result) {
                                 if (result == 1 && onAccept != nullptr)
                                     onAccept (window->first(), window->last());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeGradientColourDialog (juce::Colour first, juce::Colour last, int count)
{
    return std::make_unique<GradientWindow> (first, last, count);
}

namespace
{
class SamplePitchWindow final : public juce::AlertWindow
{
public:
    SamplePitchWindow (float pitch, bool keepLength, bool audioClip)
        : juce::AlertWindow (tr ("Pitch"),
                             audioClip ? tr ("Transposes this audio clip's file (every clip of it follows).")
                                       : tr ("Transposes the sample. The Piano Roll's keys still play it chromatically around its root note."),
                             juce::MessageBoxIconType::NoIcon)
    {
        const auto semitones = (int) std::lround (pitch);
        const auto cents = (int) std::lround ((pitch - (float) semitones) * 100.0f);
        addTextEditor ("semitones", juce::String (semitones), tr ("Semitones (-24 to +24)"));
        addTextEditor ("cents", juce::String (cents), tr ("Cents (-100 to +100)"));
        keep.setButtonText (tr ("Keep length (stretch engine)"));
        keep.setToggleState (keepLength, juce::dontSendNotification);
        keep.setSize (340, 24);
        addCustomComponent (&keep);
        note.setText (tr ("Off: resample, like a tape: higher is shorter, lower is longer."), juce::dontSendNotification);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        note.setSize (340, 18);
        addCustomComponent (&note);
        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Reset"), 2);
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        if (auto* editor = getTextEditor ("semitones"))
            editor->selectAll();
    }

    // semitones + cents/100, clamped like the pitch knob
    double pitch()
    {
        const auto number = [this] (const char* id) {
            return getTextEditorContents (id).trim().replaceCharacter (',', '.').retainCharacters ("+-0123456789.").getDoubleValue();
        };
        return juce::jlimit (-24.0, 24.0, juce::jlimit (-24.0, 24.0, number ("semitones")) + juce::jlimit (-100.0, 100.0, number ("cents")) / 100.0);
    }

    bool keepLength() const { return keep.getToggleState(); }

private:
    juce::ToggleButton keep;
    juce::Label note;
};
} // namespace

std::unique_ptr<juce::AlertWindow> makeSamplePitchDialog (float pitch, bool keepLength, bool audioClip)
{
    return std::make_unique<SamplePitchWindow> (pitch, keepLength, audioClip);
}

void showSamplePitchDialog (AudioEngine& engine, int channel)
{
    const auto& channels = engine.getSong().channels;
    if (channel < 0 || channel >= (int) channels.size())
        return;
    const auto& c = channels[(std::size_t) channel];
    auto* window = new SamplePitchWindow (c.sampler.pitch, c.sampler.keepPitch, c.instrument == InstrumentKind::audioClip);
    const auto path = juce::String::fromUTF8 (c.samplePath.c_str());
    const auto settings = c; // its Trim and region, for the frames its clips play (round 180d)
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, &engine, channel, path, settings] (int result) {
                                 if (result == 0)
                                     return;
                                 const auto pitch = result == 2 ? 0.0 : window->pitch();
                                 const auto keep = window->keepLength();
                                 const auto sample = path.isNotEmpty() ? engine.getSampleLibrary().load (path) : nullptr;
                                 const auto frames = sample != nullptr ? (double) clipops::clipSpan (settings, sample).frames : 0.0;
                                 const auto rate = sample != nullptr ? sample->sampleRate : 0.0;
                                 engine.editSong ([channel, pitch, keep, frames, rate] (Song& s) {
                                     clipops::setChannelPitch (s, channel, pitch, 0.0, keep, frames, rate);
                                 });
                             }),
                             true);
    keepAboveMainWindow (*window);
}
} // namespace lynx

namespace lynx
{
namespace
{
// FL's message text: "|bar-delimited|" words in the accent colour, wrapped at the window's width
class FlMessageText final : public juce::Component
{
public:
    explicit FlMessageText (const juce::String& text)
    {
        const auto parts = juce::StringArray::fromTokens (text, "|", {});
        for (int i = 0; i < parts.size(); ++i)
            layoutText.append (parts[i], theme::font (theme::type::body), i % 2 == 1 ? theme::colour::accent : theme::colour::textPrimary);
        layoutText.setWordWrap (juce::AttributedString::byWord);
        layoutText.setJustification (juce::Justification::topLeft);
        juce::TextLayout layout;
        layout.createLayout (layoutText, (float) width);
        setSize (width, (int) std::ceil (layout.getHeight()) + 8);
    }

    void paint (juce::Graphics& g) override { layoutText.draw (g, getLocalBounds().toFloat().withTrimmedTop (2.0f)); }

private:
    static constexpr int width = 380;
    juce::AttributedString layoutText;
};

class FlMessageWindow final : public juce::AlertWindow
{
public:
    explicit FlMessageWindow (const FlMessage& m)
        : juce::AlertWindow (m.title, {}, juce::MessageBoxIconType::QuestionIcon), text (m.text)
    {
        addCustomComponent (&text);
        if (m.buttons.size() > 2) // FL stacks three or more choices, one under the other ("Playlist tracks"); Escape = 0
        {
            for (int i = 0; i < m.buttons.size(); ++i)
            {
                auto* b = stacked.add (new juce::TextButton (m.buttons[i]));
                b->setSize (220, 28);
                b->onClick = [this, i] { exitModalState (i + 1); };
                auto* row = rows.add (new juce::Component());
                row->setSize (text.getWidth(), 32);
                row->addAndMakeVisible (b);
                b->setTopLeftPosition ((row->getWidth() - b->getWidth()) / 2, 2);
                addCustomComponent (row);
            }
        }
        if (m.rememberBox)
        {
            remember.setButtonText (tr ("Remember my choice"));
            remember.setSize (text.getWidth(), 26);
            addCustomComponent (&remember);
        }
        if (m.buttons.size() <= 2)
            for (int i = 0; i < m.buttons.size(); ++i)
                addButton (m.buttons[i], i + 1,
                           i == 0 ? juce::KeyPress (juce::KeyPress::returnKey)
                                  : i == m.buttons.size() - 1 ? juce::KeyPress (juce::KeyPress::escapeKey) : juce::KeyPress());
    }

    bool remembered() const { return remember.getToggleState(); }

private:
    FlMessageText text;
    juce::OwnedArray<juce::TextButton> stacked;
    juce::OwnedArray<juce::Component> rows;
    juce::ToggleButton remember;
};

std::set<juce::String>& hiddenMessages()
{
    static std::set<juce::String> hidden;
    return hidden;
}
} // namespace

void showFlMessage (const FlMessage& message, std::function<void (int, bool)> onChoice)
{
    auto* window = new FlMessageWindow (message);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onChoice] (int result) {
                                 if (onChoice != nullptr)
                                     onChoice (result, window->remembered());
                             }),
                             true);
    keepAboveMainWindow (*window);
}

std::unique_ptr<juce::AlertWindow> makeFlMessageDialog (const FlMessage& message)
{
    return std::make_unique<FlMessageWindow> (message);
}

namespace messages
{
const std::vector<Entry>& all()
{
    // FL's entries LYNX shows (the rest of FL's list belongs to features with their own setting in LYNX)
    // (round 190ey: FL's "RecAutoSwitchExternalInput", the External input question of issue #152; its heading "Audio" is
    // inferred from FL's table order, not decoded)
    static const std::vector<Entry> entries { { "DeleteMixerTrack", TR_NOOP ("Delete mixer track"), TR_NOOP ("File/Data") },
                                              { "RecAutoSwitchExternalInput", TR_NOOP ("Auto switch to external input only"), TR_NOOP ("Audio") } };
    return entries;
}

bool shown (const juce::String& id) { return hiddenMessages().count (id) == 0; }

void setShown (const juce::String& id, bool show)
{
    if (show == shown (id))
        return;
    if (show)
        hiddenMessages().erase (id);
    else
        hiddenMessages().insert (id);
    if (onChanged != nullptr)
        onChanged();
}

juce::String hiddenList()
{
    juce::StringArray ids;
    for (const auto& id : hiddenMessages())
        ids.add (id);
    return ids.joinIntoString (",");
}

void setHiddenList (const juce::String& list)
{
    hiddenMessages().clear();
    for (const auto& id : juce::StringArray::fromTokens (list, ",", {}))
        if (id.trim().isNotEmpty())
            hiddenMessages().insert (id.trim());
}

juce::PopupMenu manageMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Show all"), [] {
        for (const auto& e : all())
            setShown (e.id, true);
    });
    menu.addItem (tr ("Show none"), [] {
        for (const auto& e : all())
            setShown (e.id, false);
    });
    juce::String heading;
    for (const auto& e : all())
    {
        if (heading != e.category)
            menu.addSectionHeader (tr (heading = e.category));
        menu.addItem (tr (e.label), true, shown (e.id), [id = juce::String (e.id)] { setShown (id, ! shown (id)); });
    }
    return menu;
}
} // namespace messages
} // namespace lynx
