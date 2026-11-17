#include "ui/SampleProperties.h"

#include <cmath>
#include <cstdio>

#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
namespace sp = sampleprops;

// FL's FloatToStrF (value, ffGeneral, 15, 0): the shortest of 15 significant digits
juce::String general (double v)
{
    char buffer[64];
    std::snprintf (buffer, sizeof (buffer), "%.15g", v);
    return buffer;
}

// One of FL's TVectorPanel boxes: a caption band, then the controls (placed at the DFM's offsets inside it)
struct Box
{
    juce::String caption;
    juce::Rectangle<int> bounds;
};

// FL's TQuickVectorMIDIKb in the Sampler box ("Select range & offset"): the 128 keys, the low…high range lit, the
// middle note marked. A click sets the middle note; a drag sets the range.
class RangeKeyboard final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int low, int high)> onRange;
    std::function<void (int middle)> onMiddle;
    int low = 0, high = 127, middle = 60;
    float offset = 0.0f; // the keys' scroll (the window shows about 35 of the 75 white keys)

    // scrolls so that `key` is in the middle
    void centreOn (int key)
    {
        int whitesBefore = 0;
        for (int k = 0; k < key; ++k)
            whitesBefore += isBlack (k) ? 0 : 1;
        offset = juce::jlimit ((float) getWidth() - 1001.0f, 0.0f, (float) getWidth() * 0.5f - ((float) whitesBefore + 0.5f) * keyWidth());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto whites = whiteKeys();
        const auto w = keyWidth();
        for (int i = 0; i < (int) whites.size(); ++i)
        {
            const auto key = whites[(std::size_t) i];
            auto r = juce::Rectangle<float> ((float) i * w + offset, 0.0f, w, (float) getHeight());
            const auto inRange = key >= low && key <= high;
            g.setColour (inRange ? juce::Colour (0xffe6e8ec) : juce::Colour (0xff8a8f99));
            g.fillRect (r.reduced (0.5f, 0.0f));
            if (key == middle)
            {
                g.setColour (colour::accent);
                g.fillRect (r.reduced (1.5f, 0.0f).removeFromBottom (6.0f));
            }
            if (key % 12 == 0)
            {
                g.setColour (juce::Colour (0xff30333a));
                g.setFont (font (type::caption * 0.85f));
                g.drawText (juce::String (sp::noteName (key)), r.withTrimmedBottom (7.0f).toNearestInt(), juce::Justification::centredBottom, false);
            }
        }
        for (int key = 0; key < 128; ++key)
            if (isBlack (key))
            {
                const auto r = blackRect (key);
                const auto inRange = key >= low && key <= high;
                g.setColour (inRange ? juce::Colour (0xff1c1e23) : juce::Colour (0xff3a3d44));
                g.fillRect (r);
                if (key == middle)
                {
                    g.setColour (colour::accent);
                    g.fillRect (r.reduced (1.0f, 0.0f).removeFromBottom (4.0f));
                }
            }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        anchor = keyAt (e.position);
        dragged = false;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto k = keyAt (e.position);
        if (k != anchor || dragged)
        {
            dragged = true;
            if (onRange)
                onRange (std::min (anchor, k), std::max (anchor, k));
        }
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragged && onMiddle && anchor >= 0)
            onMiddle (anchor);
    }

private:
    static bool isBlack (int key) noexcept { return ((0x54a >> (key % 12)) & 1) != 0; }
    static std::vector<int> whiteKeys()
    {
        std::vector<int> v;
        for (int k = 0; k < 128; ++k)
            if (! isBlack (k))
                v.push_back (k);
        return v;
    }
    float keyWidth() const noexcept { return 1001.0f / 75.0f; } // (FL: 1001 px for the 75 white keys)
    juce::Rectangle<float> blackRect (int key) const
    {
        const auto w = keyWidth();
        int whitesBefore = 0;
        for (int k = 0; k < key; ++k)
            whitesBefore += isBlack (k) ? 0 : 1;
        return { (float) whitesBefore * w - w * 0.3f + offset, 0.0f, w * 0.6f, (float) getHeight() * 0.6f };
    }
    int keyAt (juce::Point<float> p) const
    {
        for (int key = 0; key < 128; ++key)
            if (isBlack (key) && blackRect (key).contains (p))
                return key;
        const auto i = juce::jlimit (0, 74, (int) std::floor ((p.x - offset) / keyWidth()));
        return whiteKeys()[(std::size_t) i];
    }
    int anchor = -1;
    bool dragged = false;
};

// A button of FL's narrow 42 px boxes (Round, Limit): LYNX's look, the word in the small UI size with 2 px margins, so it
// is not cut ("Rou...") where FL's smaller form font fits it
class CompactButton final : public juce::TextButton
{
public:
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto& lf = getLookAndFeel();
        lf.drawButtonBackground (g, *this, findColour (getToggleState() ? buttonOnColourId : buttonColourId), over, down);
        g.setFont (theme::font (theme::type::small));
        g.setColour (findColour (textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (2, 0), juce::Justification::centred, 1, 0.8f);
    }
};

class SamplePropertiesPanel final : public juce::Component
{
public:
    SamplePropertiesPanel (const audioedit::Document& doc, const std::string& fileName, SamplePropertiesHost h) : host (std::move (h))
    {
        form = sp::formOf (doc, fileName);
        frames = host.selectionFrames > 0.0 ? host.selectionFrames : (double) doc.audio.frames();
        currentRate = doc.audio.sampleRate;

        // the boxes at FL's places (DFM top − the 26 px caption, which is the window's here)
        boxes = { { tr ("Info"), { 1, 0, 470, 174 } }, { tr ("Format"), { 1, 182, 470, 96 } }, { tr ("Tempo"), { 1, 286, 470, 96 } }, { tr ("Sampler"), { 1, 390, 470, 146 } } };

        // Info
        label (titleLabel, tr ("Title"), 0, 71, 30, 23);
        label (commentsLabel, tr ("Comments"), 0, 38, 56, 56);
        label (fileNameLabel, tr ("Filename"), 0, 47, 143, 47);
        edit (nameEdit, 0, 101, 31, 360, 20, juce::String::fromUTF8 (form.name.c_str()));
        edit (commentsEdit, 0, 101, 59, 360, 77, juce::String::fromUTF8 (form.comments.c_str()));
        commentsEdit.setMultiLine (true, true);
        commentsEdit.setReturnKeyStartsNewLine (true);
        edit (fileNameEdit, 0, 101, 144, 360, 20, juce::String::fromUTF8 (form.fileName.c_str()));

        // Format
        label (rateLabel, tr ("Samplerate (Hz)"), 1, 11, 30, 83);
        edit (rateEdit, 1, 101, 32, 98, 19, general (form.sampleRate));
        rateEdit.onTextChange = [this] { resampleBox.setEnabled (rateEdit.getText().getDoubleValue() != currentRate); };
        button (rateSet, juce::String::fromUTF8 ("\xe2\x96\xbe"), 1, 202, 30, 19, 23);
        rateSet.setTooltip (tr ("Sample rates"));
        rateSet.onClick = [this] {
            juce::PopupMenu m;
            for (const auto rate : { 8000, 11025, 16000, 22050, 32000, 44100, 48000, 88200, 96000, 192000 })
                m.addItem (juce::String (rate), [this, rate] { rateEdit.setText (juce::String (rate), juce::sendNotificationSync); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rateSet));
        };
        toggle (resampleBox, tr ("Resample"), 1, 258, 31, 102, 21, false);
        resampleBox.setTooltip (tr ("Resample to this samplerate"));
        resampleBox.setEnabled (false);
        label (formatLabel, tr ("Format"), 1, 57, 62, 37);
        combo (formatSel, { "16Bit int", "32Bit float" }, 1, 100, 62, 79, 23, form.int16 ? 1 : 2);
        combo (channelsSel, { tr ("Stereo"), tr ("Mono - Merged"), tr ("Mono - Left only"), tr ("Mono - Right only") }, 1, 183, 62, 146, 23, (int) form.channels + 1);

        // Tempo
        label (bpmLabel, tr ("Tempo (BPM)"), 2, 25, 30, 69);
        label (beatsLabel, host.partSelected ? tr ("Sel (beats)") : tr ("Length (beats)"), 2, 19, 62, 75);
        edit (tempoEdit, 2, 101, 32, 98, 19, form.tempo != 0.0 ? general (form.tempo) : juce::String());
        tempoEdit.setTooltip (tr ("If you know the tempo"));
        edit (beatsEdit, 2, 101, 64, 98, 19, form.tempo != 0.0 ? general (sp::beatsOf (form.tempo, frames, currentRate)) : juce::String());
        beatsEdit.setTooltip (tr ("If you know the length"));
        tempoEdit.onTextChange = [this] { tempoTyped(); };
        beatsEdit.onTextChange = [this] { beatsTyped(); };
        button (roundTempo, tr ("Round"), 2, 202, 30, 42, 23);
        button (roundBeats, tr ("Round"), 2, 202, 62, 42, 23);
        roundTempo.onClick = [this] { roundBox (tempoEdit); };
        roundBeats.onClick = [this] { roundBox (beatsEdit); };
        button (limit, tr ("Limit"), 2, 244, 30, 42, 23);
        limit.onClick = [this] {
            juce::PopupMenu m;
            for (const auto low : { 50, 75, 100, 150 })
                m.addItem (juce::String (low) + " " + tr ("to") + " " + juce::String (low * 2), [this, low] {
                    tempoEdit.setText (general (sp::limitTempo (tempoEdit.getText().getDoubleValue(), low)), juce::sendNotificationSync);
                });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&limit));
        };
        toggle (tempoSync, tr ("Tempo-sync"), 2, 258, 63, 102, 21, form.tempoSync);
        tempoSync.setTooltip (tr ("Check for rhythmic content"));
        tempoSync.onClick = [this] {
            if (tempoSync.getToggleState() && tempoEdit.getText().isEmpty())
                defaultTempo();
        };
        button (bpmDetect, tr ("Autodetect"), 2, 381, 27, 81, 29);
        bpmDetect.setTooltip (tr ("Detect by scanning audio content (for songs)"));
        bpmDetect.onClick = [this] {
            juce::PopupMenu m;
            m.addItem (tr ("Quick estimation for short loops"), [this] { quickTempo(); });
            m.addItem (tr ("Detection for songs with constant tempo"), [this] { detectTempo (1); });
            m.addItem (tr ("Detection for songs with variable tempo"), [this] { detectTempo (2); });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bpmDetect));
        };
        button (bpmDefault, tr ("Default"), 2, 381, 59, 81, 29);
        bpmDefault.setTooltip (tr ("Default (project's) tempo"));
        bpmDefault.onClick = [this] { defaultTempo(); };

        // Sampler
        label (middleLabel, tr ("Middle note"), 3, 30, 30, 64);
        label (lowLabel, tr ("low"), 3, 171, 30, 19);
        label (highLabel, tr ("high"), 3, 263, 30, 23);
        juce::StringArray notes;
        for (int k = 0; k < 128; ++k)
            notes.add (sp::noteName (k));
        combo (middleSel, notes, 3, 100, 30, 52, 23, form.middleNote + 1);
        combo (lowSel, notes, 3, 196, 30, 52, 23, form.lowNote + 1);
        combo (highSel, notes, 3, 292, 30, 52, 23, form.highNote + 1);
        middleSel.setTooltip (tr ("Middle mapping note"));
        lowSel.setTooltip (tr ("Low mapping note"));
        highSel.setTooltip (tr ("High mapping note"));
        for (auto* c : { &middleSel, &lowSel, &highSel })
            c->onChange = [this] { syncKeyboard(); };
        label (fineLabel, tr ("Fine tune (cents)"), 3, 7, 62, 87);
        edit (fineEdit, 3, 101, 64, 98, 19, form.fineTune != 0.0 ? general (form.fineTune) : juce::String());
        button (roundPitch, tr ("Round"), 3, 202, 62, 42, 23);
        roundPitch.onClick = [this] {
            auto middle = middleSel.getSelectedId() - 1;
            auto fine = fineEdit.getText().getDoubleValue();
            sp::roundPitch (middle, fine);
            middleSel.setSelectedId (middle + 1);
            fineEdit.setText ("0", false);
        };
        button (pitchDetect, tr ("Autodetect"), 3, 381, 27, 81, 29);
        pitchDetect.onClick = [this] {
            int middle = 60, fine = 0;
            if (host.detectPitch && sp::fromDetectedPitch (host.detectPitch(), middle, fine))
            {
                fineEdit.setText (juce::String (fine), false);
                middleSel.setSelectedId (middle + 1);
            }
        };
        button (pitchDefault, tr ("Default"), 3, 381, 59, 81, 29);
        pitchDefault.onClick = [this] {
            middleSel.setSelectedId (61);
            lowSel.setSelectedId (1);
            highSel.setSelectedId (128);
            fineEdit.setText ({}, false);
        };
        keyboard.setTooltip (tr ("Select range & offset"));
        keyboard.onMiddle = [this] (int k) { middleSel.setSelectedId (k + 1); };
        keyboard.onRange = [this] (int lo, int hi) {
            lowSel.setSelectedId (lo + 1);
            highSel.setSelectedId (hi + 1);
        };
        addAndMakeVisible (keyboard);
        syncKeyboard();

        accept.setButtonText (tr ("Accept"));
        accept.onClick = [this] { doAccept(); };
        addAndMakeVisible (accept);
        setSize (samplePropertiesWidth, samplePropertiesHeight);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        for (const auto& b : boxes)
        {
            const auto r = b.bounds.toFloat();
            g.setColour (colour::bg3);
            g.fillRect (r);
            g.setColour (colour::border);
            g.drawRect (r, 1.0f);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small, true));
            g.drawText (b.caption, r.withHeight (22.0f).reduced (8.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }
        // FL's "F" link mark between the tempo and the length (LinkLabel 6, 48): a bracket joining the two rows
        const auto t = boxes[2].bounds;
        g.setColour (colour::textSecondary);
        const auto x = (float) t.getX() + 10.0f, y0 = (float) t.getY() + 41.0f, y1 = (float) t.getY() + 74.0f;
        g.drawLine (x + 6.0f, y0, x, y0, 1.0f);
        g.drawLine (x, y0, x, y1, 1.0f);
        g.drawLine (x, y1, x + 6.0f, y1, 1.0f);
    }

    void resized() override
    {
        for (auto& [c, box, area] : placed)
            c->setBounds (area.translated (boxes[(std::size_t) box].bounds.getX(), boxes[(std::size_t) box].bounds.getY()));
        const auto s = boxes[3].bounds;
        keyboard.setBounds (s.getX(), s.getY() + 97, s.getWidth(), 36); // (FL: a 1001 px keyboard in a 470 px box, from −25)
        keyboard.centreOn (keyboard.middle);
        accept.setBounds (312, 548, 60, 29);
    }

    sp::Form formForTests() const { return readForm(); }

private:
    struct Placed
    {
        juce::Component* c;
        int box;
        juce::Rectangle<int> area;
    };
    void place (juce::Component& c, int box, int x, int y, int w, int h)
    {
        placed.push_back ({ &c, box, { x, y, w, h } });
        addAndMakeVisible (c);
    }
    void label (juce::Label& l, const juce::String& text, int box, int x, int y, int w)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredRight);
        l.setFont (theme::font (theme::type::small));
        l.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        l.setBorderSize ({});
        place (l, box, x, y, w, 23);
    }
    void edit (juce::TextEditor& e, int box, int x, int y, int w, int h, const juce::String& text)
    {
        e.setText (text, false);
        e.setFont (theme::font (theme::type::small));
        e.setIndents (4, std::max (0, (h - 15) / 2));
        place (e, box, x, y, w, h);
    }
    void button (juce::TextButton& b, const juce::String& text, int box, int x, int y, int w, int h)
    {
        b.setButtonText (text);
        place (b, box, x, y, w, h);
    }
    void toggle (juce::ToggleButton& b, const juce::String& text, int box, int x, int y, int w, int h, bool on)
    {
        b.setButtonText (text);
        b.setToggleState (on, juce::dontSendNotification);
        place (b, box, x, y, w, h);
    }
    void combo (juce::ComboBox& c, const juce::StringArray& items, int box, int x, int y, int w, int h, int selected)
    {
        c.addItemList (items, 1);
        c.setSelectedId (selected, juce::dontSendNotification);
        place (c, box, x, y, w, h);
    }

    // the Tempo and Length boxes follow each other (FL 0x7b2910 / 0x7b2a80)
    void tempoTyped()
    {
        if (linking)
            return;
        const juce::ScopedValueSetter<bool> guard (linking, true);
        const auto tempo = sp::clampTypedTempo (tempoEdit.getText().getDoubleValue());
        beatsEdit.setText (general (sp::beatsOf (tempo, frames, currentRate)), false);
    }
    void beatsTyped()
    {
        if (linking)
            return;
        const juce::ScopedValueSetter<bool> guard (linking, true);
        tempoEdit.setText (general (sp::tempoOf (beatsEdit.getText().getDoubleValue(), frames, currentRate)), false);
    }
    void roundBox (juce::TextEditor& e) { e.setText (general (std::nearbyint (e.getText().getDoubleValue())), juce::sendNotificationSync); }
    void defaultTempo()
    {
        tempoEdit.setText (general (host.projectTempo), juce::sendNotificationSync);
        tempoSync.setToggleState (true, juce::dontSendNotification);
    }
    void quickTempo()
    {
        double tempo = 0.0;
        const auto beats = sp::quickBeats (frames, currentRate, host.projectTempo, tempo);
        beatsEdit.setText (juce::String (beats), juce::sendNotificationSync);
        tempoSync.setToggleState (beats > 0, juce::dontSendNotification);
    }
    void detectTempo (int method)
    {
        const auto tempo = host.detectTempo ? host.detectTempo (method) : 0.0;
        tempoEdit.setText (general (tempo), juce::sendNotificationSync);
        tempoSync.setToggleState (tempo != 0.0, juce::dontSendNotification);
    }
    void syncKeyboard()
    {
        keyboard.middle = middleSel.getSelectedId() - 1;
        keyboard.low = lowSel.getSelectedId() - 1;
        keyboard.high = highSel.getSelectedId() - 1;
        keyboard.repaint();
    }
    sp::Form readForm() const
    {
        auto f = form;
        f.name = nameEdit.getText().toStdString();
        f.comments = commentsEdit.getText().toStdString();
        f.fileName = fileNameEdit.getText().toStdString();
        f.sampleRate = rateEdit.getText().getDoubleValue();
        f.resample = resampleBox.getToggleState() && resampleBox.isEnabled();
        f.int16 = formatSel.getSelectedId() == 1;
        f.channels = (sp::Channels) juce::jlimit (0, 3, channelsSel.getSelectedId() - 1);
        f.tempo = tempoEdit.getText().getDoubleValue();
        f.tempoSync = tempoSync.getToggleState();
        f.fineTune = fineEdit.getText().getDoubleValue();
        f.middleNote = middleSel.getSelectedId() - 1;
        f.lowNote = lowSel.getSelectedId() - 1;
        f.highNote = highSel.getSelectedId() - 1;
        return f;
    }
    void doAccept()
    {
        if (host.accept)
            host.accept (readForm());
        closeToolWindowOf (*this);
    }

    SamplePropertiesHost host;
    sp::Form form;
    double frames = 0.0, currentRate = 44100.0;
    std::vector<Box> boxes;
    std::vector<Placed> placed;
    bool linking = false;
    juce::Label titleLabel, commentsLabel, fileNameLabel, rateLabel, formatLabel, bpmLabel, beatsLabel, middleLabel, lowLabel, highLabel, fineLabel;
    juce::TextEditor nameEdit, commentsEdit, fileNameEdit, rateEdit, tempoEdit, beatsEdit, fineEdit;
    juce::TextButton rateSet, bpmDetect, bpmDefault, pitchDetect, pitchDefault, accept;
    CompactButton roundTempo, roundBeats, limit, roundPitch; // (FL's 42 px boxes: their words drawn small enough to fit)
    juce::ToggleButton resampleBox, tempoSync;
    juce::ComboBox formatSel, channelsSel, middleSel, lowSel, highSel;
    RangeKeyboard keyboard;
};
} // namespace

std::unique_ptr<juce::Component> makeSamplePropertiesPanel (const audioedit::Document& doc, const std::string& fileName, SamplePropertiesHost host)
{
    return std::make_unique<SamplePropertiesPanel> (doc, fileName, std::move (host));
}
} // namespace lynx
