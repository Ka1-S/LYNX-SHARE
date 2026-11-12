#include "ui/FinishDock.h"

#include <cmath>
#include <thread>

#include "core/MasterLooks.h"
#include "core/Mastering.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// --- each look's picture: LYNX's own simple line drawing in a 1×1 box -------------------------------------------------
juce::Path lookGlyph (int id)
{
    juce::Path p;
    switch (id)
    {
        case looks::balanced: // a level with two even pans
            p.startNewSubPath (0.5f, 0.12f); p.lineTo (0.5f, 0.88f);
            p.startNewSubPath (0.12f, 0.3f); p.lineTo (0.88f, 0.3f);
            p.addArc (0.06f, 0.3f, 0.26f, 0.26f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, true);
            p.addArc (0.68f, 0.3f, 0.26f, 0.26f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, true);
            p.startNewSubPath (0.3f, 0.88f); p.lineTo (0.7f, 0.88f);
            break;
        case looks::loud: // a meter in the red
            for (int i = 0; i < 5; ++i)
            {
                const auto x = 0.14f + 0.16f * (float) i, h = 0.18f + 0.15f * (float) i;
                p.addRoundedRectangle (x, 0.88f - h, 0.1f, h, 0.02f);
            }
            break;
        case looks::clarity: // a diamond with sparkles
            p.startNewSubPath (0.42f, 0.2f); p.lineTo (0.72f, 0.5f); p.lineTo (0.42f, 0.86f); p.lineTo (0.12f, 0.5f); p.closeSubPath();
            p.startNewSubPath (0.82f, 0.1f); p.lineTo (0.82f, 0.3f);
            p.startNewSubPath (0.72f, 0.2f); p.lineTo (0.92f, 0.2f);
            break;
        case looks::warmTape: // a cassette
            p.addRoundedRectangle (0.08f, 0.24f, 0.84f, 0.56f, 0.06f);
            p.addEllipse (0.24f, 0.4f, 0.16f, 0.16f);
            p.addEllipse (0.6f, 0.4f, 0.16f, 0.16f);
            p.startNewSubPath (0.32f, 0.4f); p.lineTo (0.68f, 0.4f);
            break;
        case looks::gentle: // a soft wave
            p.startNewSubPath (0.08f, 0.5f);
            p.cubicTo (0.25f, 0.25f, 0.4f, 0.25f, 0.5f, 0.5f);
            p.cubicTo (0.6f, 0.75f, 0.75f, 0.75f, 0.92f, 0.5f);
            break;
        case looks::punchy: // a burst
            for (int i = 0; i < 8; ++i)
            {
                const auto a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                const auto r0 = 0.16f, r1 = i % 2 == 0 ? 0.42f : 0.3f;
                p.startNewSubPath (0.5f + r0 * std::cos (a), 0.5f + r0 * std::sin (a));
                p.lineTo (0.5f + r1 * std::cos (a), 0.5f + r1 * std::sin (a));
            }
            break;
        case looks::wide: // arrows out
            p.startNewSubPath (0.1f, 0.5f); p.lineTo (0.9f, 0.5f);
            p.startNewSubPath (0.24f, 0.36f); p.lineTo (0.1f, 0.5f); p.lineTo (0.24f, 0.64f);
            p.startNewSubPath (0.76f, 0.36f); p.lineTo (0.9f, 0.5f); p.lineTo (0.76f, 0.64f);
            p.startNewSubPath (0.5f, 0.3f); p.lineTo (0.5f, 0.7f);
            break;
        case looks::cinematic: // a film frame
            p.addRoundedRectangle (0.1f, 0.22f, 0.8f, 0.56f, 0.04f);
            for (int i = 0; i < 4; ++i)
            {
                p.addRectangle (0.18f + 0.18f * (float) i, 0.27f, 0.08f, 0.06f);
                p.addRectangle (0.18f + 0.18f * (float) i, 0.67f, 0.08f, 0.06f);
            }
            break;
        default: break;
    }
    return p;
}

juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
} // namespace

class FinishDock::LookTile final : public juce::Component,
                                   public juce::SettableTooltipClient
{
public:
    LookTile (int i) : id (i)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (tr (looks::name (id)));
        setTooltip (tr (looks::description (id)));
    }

    std::function<void()> onClick;
    bool selected = false;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto hover = isMouseOver (true);
        g.setColour (selected ? theme::colour::bg5 : hover ? theme::colour::bg4 : theme::colour::bg3);
        g.fillRoundedRectangle (r, theme::radius::lg);
        g.setColour (selected || hasKeyboardFocus (false) ? theme::colour::accent : hover ? theme::colour::borderStrong : theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::lg, selected ? 1.5f : 1.0f);
        const auto side = juce::jmin (28.0f, r.getHeight() - 26.0f);
        const juce::Rectangle<float> box (r.getCentreX() - side / 2.0f, r.getY() + 8.0f, side, side);
        auto glyph = lookGlyph (id);
        glyph.applyTransform (juce::AffineTransform::scale (side).translated (box.getX(), box.getY()));
        g.setColour (selected ? theme::colour::accent : theme::colour::textSecondary);
        g.strokePath (glyph, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (selected ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body, selected));
        g.drawFittedText (tr (looks::name (id)), getLocalBounds().removeFromBottom (24).reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && onClick)
        {
            onClick();
            return true;
        }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    int id;
};

// "Save the song as a file": one row per format — its name, what it is good for, and about how big the file will be
class FinishDock::SaveRow final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    SaveRow (juce::String n, juce::String h) : name (std::move (n)), help (std::move (h))
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (name);
        setDescription (help);
    }

    std::function<void()> onClick;

    void setSize (const juce::String& text)
    {
        if (size != text)
        {
            size = text;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto hover = isMouseOver (true), down = isMouseButtonDown();
        g.setColour (down ? theme::colour::bg5 : hover ? theme::colour::bg4 : theme::colour::bg3);
        g.fillRoundedRectangle (r, theme::radius::md);
        g.setColour (hasKeyboardFocus (false) ? theme::colour::accent : hover ? theme::colour::borderStrong : theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::md, 1.0f);
        auto text = getLocalBounds().reduced (10, 0);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::body, true));
        g.drawText (name, text.removeFromLeft (40), juce::Justification::centredLeft, false);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (size, text.removeFromRight (64), juce::Justification::centredRight, false);
        g.drawFittedText (help, text, juce::Justification::centredLeft, 1, 0.75f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        repaint();
        if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && onClick)
        {
            onClick();
            return true;
        }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    juce::String name, help, size;
};

FinishDock::FinishDock (AudioEngine& e, std::function<void()> close, Saving s)
    : engine (e), onClose (std::move (close)), saving (std::move (s)), measuring (std::make_shared<std::atomic<int>> (0)),
      measuredLufs (std::make_shared<std::atomic<float>> (0.0f))
{
    setTitle (tr ("Master looks"));
    title.setText (tr ("Master looks"), juce::dontSendNotification);
    title.setFont (theme::font (theme::type::title, true));
    addAndMakeVisible (title);
    status.setFont (theme::font (theme::type::small));
    status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    addAndMakeVisible (status);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    closeButton.setTooltip (tr ("Close"));
    closeButton.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton);

    hearWithout.setButtonText (tr ("Hear without"));
    hearWithout.setTooltip (tr ("Switches the look off and on, to compare (A/B)"));
    hearWithout.setClickingTogglesState (true);
    hearWithout.onClick = [this] {
        if (updating)
            return;
        const auto off = hearWithout.getToggleState();
        engine.editSong ([off] (Song& s) { looks::setBypassed (s, off); });
    };
    addAndMakeVisible (hearWithout);

    for (int i = 0; i < looks::count; ++i)
    {
        tiles[i] = std::make_unique<LookTile> (i + 1);
        tiles[i]->onClick = [this, id = i + 1] { choose (id); };
        addAndMakeVisible (*tiles[i]);
    }

    intensityLabel.setText (tr ("Intensity"), juce::dontSendNotification);
    intensityLabel.setFont (theme::font (theme::type::body, true));
    addAndMakeVisible (intensityLabel);
    intensity.setSliderStyle (juce::Slider::LinearHorizontal);
    intensity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    intensity.setRange (0.0, 1.0, 0.01);
    intensity.setDoubleClickReturnValue (true, 0.5);
    intensity.textFromValueFunction = [] (double v) { return percent ((float) v); };
    intensity.setPopupDisplayEnabled (true, false, this);
    intensity.onValueChange = [this] { changed (false); };
    addAndMakeVisible (intensity);
    const char* markNames[3] { "Light", "Normal", "Heavy" };
    for (int i = 0; i < 3; ++i)
    {
        marks[i].setText (tr (markNames[i]), juce::dontSendNotification);
        marks[i].setFont (theme::font (theme::type::small));
        marks[i].setColour (juce::Label::textColourId, theme::colour::textSecondary);
        marks[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (marks[i]);
    }

    toneLabel.setText (tr ("Tone"), juce::dontSendNotification);
    toneLabel.setFont (theme::font (theme::type::body, true));
    addAndMakeVisible (toneLabel);
    const char* toneText[3] { "Low", "Mid", "High" };
    for (int i = 0; i < 3; ++i)
    {
        auto& k = tone[i];
        k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.setRange (-6.0, 6.0, 0.1);
        k.setDoubleClickReturnValue (true, 0.0);
        k.textFromValueFunction = [] (double v) { return db ((float) v); };
        k.setPopupDisplayEnabled (true, false, this);
        k.onValueChange = [this] { changed (false); };
        addAndMakeVisible (k);
        toneNames[i].setText (tr (toneText[i]), juce::dontSendNotification);
        toneNames[i].setFont (theme::font (theme::type::small));
        toneNames[i].setColour (juce::Label::textColourId, theme::colour::textSecondary);
        toneNames[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (toneNames[i]);
    }

    autoButton.setButtonText (tr ("Set the input level for me"));
    autoButton.setTooltip (tr ("Measures the mix without the look and sets the look's input level for it"));
    autoButton.onClick = [this] { autoLevel(); };
    addAndMakeVisible (autoButton);
    levelInfo.setFont (theme::font (theme::type::small));
    levelInfo.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    levelInfo.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (levelInfo);

    // the looks hold sample peaks (FL's Limiter); a release file with a true-peak ceiling comes from the mastering export
    const auto arrow = juce::String::fromUTF8 (" \xe2\x80\xba ");
    peakHint.setText (tr ("For a file with a true-peak ceiling: {0}", { tr ("File") + arrow + tr ("Export") + arrow + tr ("Master...") }),
                      juce::dontSendNotification);
    peakHint.setFont (theme::font (theme::type::caption));
    peakHint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    peakHint.setJustificationType (juce::Justification::centredLeft);
    peakHint.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (peakHint);

    removeButton.setButtonText (tr ("Remove look"));
    removeButton.onClick = [this] {
        engine.editSongStructural ([] (Song& s) { looks::remove (s); });
        refresh();
    };
    addAndMakeVisible (removeButton);

    // round 190ch: measure a finished track and match its tone and loudness (its own window)
    if (saving.referenceMatch)
    {
        matchButton.setButtonText (tr ("Match a reference track..."));
        matchButton.setTooltip (tr ("Measures a finished track and your song, and suggests an EQ curve and a level"));
        matchButton.onClick = [this] { saving.referenceMatch(); };
        addAndMakeVisible (matchButton);
    }

    // the last step in plain words: the song as one file, with the look on (round 190t)
    if (saving.exportAudio)
    {
        saveLabel.setText (tr ("Save the song as a file"), juce::dontSendNotification);
        saveLabel.setFont (theme::font (theme::type::body, true));
        addAndMakeVisible (saveLabel);
        saveRows[0] = std::make_unique<SaveRow> ("WAV", tr ("Best quality, larger file"));
        saveRows[1] = std::make_unique<SaveRow> ("MP3", tr ("Small, for sharing"));
        for (int i = 0; i < 2; ++i)
        {
            saveRows[i]->onClick = [this, i] { save (i); };
            addAndMakeVisible (*saveRows[i]);
        }
        if (saving.exportOtherApps)
        {
            otherApps.setButtonText (tr ("For other apps..."));
            otherApps.onClick = [this] { save (-1); };
            addAndMakeVisible (otherApps);
        }
    }

    refresh();
}

void FinishDock::save (int format)
{
    // "Hear without" is for comparing; the file gets the look
    if (looks::bypassed (engine.getSong()))
    {
        engine.editSong ([] (Song& s) { looks::setBypassed (s, false); });
        refresh();
    }
    if (format < 0 && saving.exportOtherApps)
        saving.exportOtherApps();
    else if (format >= 0 && saving.exportAudio)
        saving.exportAudio (format);
}

FinishDock::~FinishDock()
{
    stopTimer();
}

void FinishDock::refresh()
{
    const juce::ScopedValueSetter<bool> guard (updating, true);
    const auto& song = engine.getSong();
    const auto has = looks::present (song);
    shown = has ? song.masterLook : MasterLookSettings {};
    if (! has)
        shown.id = 0;
    const auto edited = has && looks::custom (song);
    for (int i = 0; i < looks::count; ++i)
    {
        tiles[i]->selected = has && ! edited && shown.id == i + 1;
        tiles[i]->repaint();
    }
    status.setText (! has ? tr ("Choose a look for the whole song.")
                    : edited ? tr ("Custom: the look was changed in the Mixer.")
                             : tr ("{0}, on the Master's last slots.", { tr (looks::name (shown.id)) }),
                    juce::dontSendNotification);
    intensity.setValue (has ? shown.intensity : 0.5, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        tone[i].setValue (has ? shown.tone[(std::size_t) i] : 0.0, juce::dontSendNotification);
    hearWithout.setToggleState (looks::bypassed (song), juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &hearWithout, &intensity, &tone[0], &tone[1], &tone[2], &autoButton, &removeButton })
        c->setEnabled (has);
    if (measuring->load() == 0)
        levelInfo.setText (has ? tr ("Input level: {0}", { db (shown.inputDb) }) : juce::String(), juce::dontSendNotification);
    // about how big each file will be, from the song's length and the Export window's own defaults
    for (int i = 0; i < 2; ++i)
        if (saveRows[i] != nullptr)
        {
            exportopts::Options o;
            o.format = i == 0 ? exportopts::Format::wav : exportopts::Format::mp3;
            const auto seconds = exportMusicSeconds (song, o);
            saveRows[i]->setSize (seconds > 0.0 ? juce::File::descriptionOfSizeInBytes (exportopts::estimatedBytes (o, seconds)) : juce::String());
        }
}

void FinishDock::choose (int id)
{
    auto s = shown;
    if (s.id == 0)
        s = MasterLookSettings {};
    s.id = id;
    bool ok = true;
    // adds or replaces Master slots (another look may swap the Soft Clipper for the Stereo Enhancer): a structural edit
    engine.editSongStructural ([&ok, s] (Song& song) { ok = looks::apply (song, s); });
    if (! ok)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Master looks"),
                                                tr ("The Master has no room for the look: it needs five free effect slots."));
    refresh();
}

void FinishDock::changed (bool)
{
    if (updating || shown.id == 0)
        return;
    shown.intensity = (float) intensity.getValue();
    for (int i = 0; i < 3; ++i)
        shown.tone[(std::size_t) i] = (float) tone[i].getValue();
    const auto s = shown;
    engine.editSong ([s] (Song& song) { looks::apply (song, s); });
}

void FinishDock::autoLevel()
{
    if (measuring->load() != 0 || shown.id == 0)
        return;
    // the song without the look, rendered offline on another thread; the loudness sets the look's input level
    auto song = engine.getSong();
    looks::setBypassed (song, true);
    measuring->store (1);
    levelInfo.setText (tr ("Measuring the mix..."), juce::dontSendNotification);
    autoButton.setEnabled (false);
    auto flag = measuring;
    auto result = measuredLufs;
    std::thread ([song = std::move (song), flag, result] {
        exportopts::Options o;
        o.format = exportopts::Format::wav;
        o.wavBits = 32;
        o.sampleRate = 44100.0;
        const auto file = juce::File::createTempFile (".wav");
        ExportJob job (song, o, file);
        job.setExtraData (false);
        juce::String error;
        float lufs = -100.0f;
        if (job.run ([] (double) { return true; }, error))
        {
            juce::WavAudioFormat wav;
            if (std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true)); reader != nullptr)
            {
                const auto n = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (44100.0 * 600.0));
                juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader->numChannels), n);
                reader->read (&buf, 0, n, 0, true, true);
                std::vector<float> l (buf.getReadPointer (0), buf.getReadPointer (0) + n), r (buf.getReadPointer (1), buf.getReadPointer (1) + n);
                double value = 0.0, peak = 0.0;
                mastering::measure (l, r, 44100.0, value, peak);
                lufs = (float) value;
            }
        }
        file.deleteFile();
        result->store (lufs);
        flag->store (2);
    }).detach();
    startTimer (100);
}

void FinishDock::timerCallback()
{
    if (measuring->load() != 2)
        return;
    stopTimer();
    measuring->store (0);
    autoButton.setEnabled (shown.id != 0);
    const auto lufs = measuredLufs->load();
    if (lufs < -70.0f)
    {
        levelInfo.setText (tr ("The mix is silent: nothing to measure."), juce::dontSendNotification);
        return;
    }
    shown.inputDb = looks::autoInputGainDb (lufs);
    const auto s = shown;
    engine.editSong ([s] (Song& song) { looks::apply (song, s); });
    levelInfo.setText (tr ("Mix {0} LUFS: input level {1}", { juce::String (lufs, 1), db (shown.inputDb) }), juce::dontSendNotification);
}

void FinishDock::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg2);
    g.setColour (theme::colour::border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void FinishDock::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (28);
    closeButton.setBounds (head.removeFromRight (28));
    title.setBounds (head);
    status.setBounds (r.removeFromTop (34));
    r.removeFromTop (4);
    // (a short window: the look tiles give way so that the Save rows stay in view)
    const auto saveH = (saveRows[0] != nullptr ? 22 + 2 * 36 + (otherApps.isVisible() ? 34 : 0) + 10 : 0) + (matchButton.isVisible() ? 34 : 0);
    const auto gap = 6;
    const auto tileH = juce::jlimit (46, 64, (r.getHeight() - 306 - saveH) / 4 - gap);
    const auto colW = (r.getWidth() - gap) / 2;
    for (int i = 0; i < looks::count; ++i)
    {
        const auto row = i / 2, col = i % 2;
        tiles[i]->setBounds (r.getX() + col * (colW + gap), r.getY() + row * (tileH + gap), colW, tileH);
    }
    r.removeFromTop (4 * (tileH + gap) + 6);
    intensityLabel.setBounds (r.removeFromTop (22));
    auto bar = r.removeFromTop (24);
    intensity.setBounds (bar);
    auto markRow = r.removeFromTop (18);
    // the marks under their values (Light 25 %, Normal 50 %, Heavy 85 %), inside the slider's track
    const auto x0 = bar.getX() + 10, w = bar.getWidth() - 20;
    const float at[3] { 0.25f, 0.5f, 0.85f };
    for (int i = 0; i < 3; ++i)
        marks[i].setBounds (juce::Rectangle<int> (60, markRow.getHeight()).withCentre ({ x0 + (int) (at[i] * (float) w), markRow.getCentreY() }));
    r.removeFromTop (10);
    toneLabel.setBounds (r.removeFromTop (22));
    auto knobs = r.removeFromTop (58);
    const auto kw = knobs.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto cell = knobs.removeFromLeft (kw);
        toneNames[i].setBounds (cell.removeFromBottom (16));
        tone[i].setBounds (cell.withSizeKeepingCentre (42, 42));
    }
    r.removeFromTop (12);
    autoButton.setBounds (r.removeFromTop (30));
    levelInfo.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (30);
    hearWithout.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    removeButton.setBounds (buttons);
    r.removeFromTop (8);
    if (matchButton.isVisible())
    {
        matchButton.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
    }
    peakHint.setBounds (r.removeFromTop (32));
    if (saveRows[0] != nullptr)
    {
        r.removeFromTop (10);
        saveLabel.setBounds (r.removeFromTop (22));
        for (auto& row : saveRows)
        {
            row->setBounds (r.removeFromTop (32));
            r.removeFromTop (4);
        }
        if (otherApps.isVisible())
            otherApps.setBounds (r.removeFromTop (30));
    }
}
} // namespace lynx
