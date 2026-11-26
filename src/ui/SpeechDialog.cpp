#include "ui/SpeechDialog.h"

#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/ValueControl.h"

namespace lynx::speechui
{
namespace
{
const char* const noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// FL's keyboard numbering (key 57 = A, 440 Hz; octaves counted from key 0 = C0).
juce::String keyName (int key) { return juce::String (noteNames[((key % 12) + 12) % 12]) + juce::String (key / 12); }

// The pitch keyboard (FL's form: keys 12..71): click a key to set the voice pitch.
class PitchKeyboard final : public juce::Component
{
public:
    std::function<void (int)> onKey;
    void setKey (int k) { key = k; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto whites = whiteCount();
        const float w = (float) getWidth() / (float) whites, h = (float) getHeight();
        int white = 0;
        for (int k = speech::minKey; k <= speech::maxKey; ++k)
        {
            if (isBlack (k))
                continue;
            const juce::Rectangle<float> r (white * w, 0, w, h);
            g.setColour (k == key ? theme::colour::accent : juce::Colour (0xffd9dce2));
            g.fillRect (r.reduced (0.5f, 0));
            if (k % 12 == 0)
            {
                g.setColour (k == key ? theme::colour::onAccent : juce::Colour (0xff4a4f59));
                g.setFont (theme::font (theme::type::caption));
                g.drawText (keyName (k), r.withTrimmedTop (h - 16), juce::Justification::centred);
            }
            ++white;
        }
        white = 0;
        for (int k = speech::minKey; k <= speech::maxKey; ++k)
        {
            if (isBlack (k))
            {
                const juce::Rectangle<float> r (white * w - w * 0.3f, 0, w * 0.6f, h * 0.6f);
                g.setColour (k == key ? theme::colour::accent : juce::Colour (0xff2b2e35));
                g.fillRect (r);
            }
            else
                ++white;
        }
        g.setColour (theme::colour::borderStrong);
        g.drawRect (getLocalBounds());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const auto k = keyAt (e.position); k >= 0)
        {
            setKey (k);
            if (onKey)
                onKey (k);
        }
    }

    int keyAt (juce::Point<float> p) const
    {
        const auto whites = whiteCount();
        const float w = (float) getWidth() / (float) whites;
        // black keys first (they sit over the white ones)
        if (p.y < getHeight() * 0.6f)
        {
            int white = 0;
            for (int k = speech::minKey; k <= speech::maxKey; ++k)
            {
                if (isBlack (k))
                {
                    if (juce::Rectangle<float> (white * w - w * 0.3f, 0, w * 0.6f, getHeight() * 0.6f).contains (p))
                        return k;
                }
                else
                    ++white;
            }
        }
        int white = 0;
        for (int k = speech::minKey; k <= speech::maxKey; ++k)
            if (! isBlack (k))
            {
                if (p.x >= white * w && p.x < (white + 1) * w)
                    return k;
                ++white;
            }
        return -1;
    }

private:
    static bool isBlack (int k)
    {
        const auto n = k % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }
    static int whiteCount()
    {
        int n = 0;
        for (int k = speech::minKey; k <= speech::maxKey; ++k)
            n += isBlack (k) ? 0 : 1;
        return n;
    }
    int key = 57;
};

class SpeechPanel final : public juce::Component
{
public:
    SpeechPanel (AudioEngine& e, const speech::Settings& s, std::function<void (bool, const speech::Settings&)> done)
        : engine (e), settings (s), onDone (std::move (done))
    {
        title.setText (tr ("Speech properties"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);

        text.setText (juce::String::fromUTF8 (settings.text.c_str()), false);
        text.setFont (theme::font (theme::type::display - 2.0f));
        text.setColour (juce::TextEditor::backgroundColourId, theme::colour::bg0);
        text.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        text.setColour (juce::TextEditor::outlineColourId, theme::colour::borderStrong);
        text.setColour (juce::TextEditor::focusedOutlineColourId, theme::colour::accent);
        text.onTextChange = [this] { settings.text = text.getText().toStdString(); changed = true; };
        text.onReturnKey = [this] { listen(); };
        ui::setHint (text, tr ("Enter here the text to be synthesized by the engine. \"_\" joins words into one slice, \"word(3)\" sings that word 3 semitones higher"));
        addAndMakeVisible (text);

        listenButton.setButtonText (tr ("Listen"));
        listenButton.onClick = [this] { listen(); };
        ui::setHint (listenButton, tr ("Click to preview the sample"));
        addAndMakeVisible (listenButton);

        voiceHeader.setText (tr ("Voice character"), juce::dontSendNotification);
        voiceHeader.setFont (theme::font (theme::type::body, true));
        voiceHeader.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (voiceHeader);

        for (int i = 0; i < speech::personalityCount; ++i)
            personality.addItem (tr (speech::personalityName (i)), i + 1);
        personality.setSelectedId (settings.personality + 1, juce::dontSendNotification);
        personality.onChange = [this] {
            // FL: choosing a personality sets that voice's pitch, rate, style and mode, then you hear it
            settings = speech::personalityDefaults (personality.getSelectedId() - 1, settings);
            showSettings();
            changed = true;
            listen();
        };
        ui::setHint (personality, tr ("Select voice personality"));

        for (int i = 0; i < speech::styleCount; ++i)
            style.addItem (tr (speech::styleName ((speech::Style) i)), i + 1);
        style.onChange = [this] { settings.style = (speech::Style) (style.getSelectedId() - 1); changed = true; listen(); };
        ui::setHint (style, tr ("Select intonation style for the voice"));

        for (int i = 0; i < speech::voicingCount; ++i)
            mode.addItem (tr (speech::voicingName ((speech::Voicing) i)), i + 1);
        mode.onChange = [this] { settings.voicing = (speech::Voicing) (mode.getSelectedId() - 1); changed = true; listen(); };
        ui::setHint (mode, tr ("Select the amount of noise mixed in the voice generated"));

        rate.setSliderStyle (juce::Slider::LinearHorizontal);
        rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 22);
        rate.setRange (speech::minRate, speech::maxRate, 1);
        rate.setDoubleClickReturnValue (true, speech::defaultRate);
        rate.onValueChange = [this] { settings.rate = (int) rate.getValue(); changed = true; };
        rate.onDragEnd = [this] { listen(); }; // FL previews when the wheel is released
        ui::setHint (rate, tr ("Set the speed of the voice in words / minute"));

        for (auto* c : { &personality, &style, &mode })
            addAndMakeVisible (*c);
        addAndMakeVisible (rate);

        for (auto* l : { &personalityLabel, &styleLabel, &modeLabel, &rateLabel, &pitchLabel })
        {
            l->setFont (theme::font (theme::type::body, true));
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (*l);
        }
        personalityLabel.setText (tr ("Personality"), juce::dontSendNotification);
        styleLabel.setText (tr ("Style"), juce::dontSendNotification);
        modeLabel.setText (tr ("Mode"), juce::dontSendNotification);
        rateLabel.setText (tr ("Word rate (WPM)"), juce::dontSendNotification);
        pitchLabel.setJustificationType (juce::Justification::centredLeft);
        pitchLabel.setFont (theme::font (theme::type::body));
        pitchLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);

        keyboard.onKey = [this] (int key) {
            settings.pitch = speech::keyToStoredPitch (key);
            changed = true;
            showPitch();
            listen();
        };
        ui::setHint (keyboard, tr ("Click a key to see the pitch of the voice"));
        addAndMakeVisible (keyboard);

        acceptButton.setButtonText (tr ("Accept"));
        acceptButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        acceptButton.onClick = [this] { finish (true); };
        ui::setHint (acceptButton, tr ("Apply the speech preset to the generator/effect selected"));
        cancelButton.setButtonText (tr ("Cancel"));
        cancelButton.onClick = [this] { finish (false); };
        addAndMakeVisible (acceptButton);
        addAndMakeVisible (cancelButton);

        showSettings();
        setSize (560, 330);
    }

    ~SpeechPanel() override
    {
        engine.previewSample (nullptr);
        // closed with the window's close button: like Cancel (the file loads as it was), once this window is gone
        if (! finished && onDone)
            juce::MessageManager::callAsync ([done = onDone, s = settings] { done (false, s); });
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (voiceArea.toFloat(), 6.0f);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (voiceArea.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        auto row = r.removeFromTop (36);
        listenButton.setBounds (row.removeFromRight (80));
        row.removeFromRight (8);
        text.setBounds (row);
        r.removeFromTop (10);
        auto buttons = r.removeFromBottom (32);
        cancelButton.setBounds (buttons.removeFromRight (96));
        buttons.removeFromRight (8);
        acceptButton.setBounds (buttons.removeFromRight (96));
        r.removeFromBottom (10);

        voiceArea = r;
        auto v = r.reduced (10, 6);
        voiceHeader.setBounds (v.removeFromTop (20));
        const int labelW = theme::labelColumnWidth ({ &personalityLabel, &styleLabel, &modeLabel }, 80, 150);
        const int rateLabelW = theme::labelColumnWidth ({ &rateLabel }, 110, 190);
        auto line = [&v] { auto l = v.removeFromTop (26); v.removeFromTop (6); return l; };
        auto l1 = line();
        personalityLabel.setBounds (l1.removeFromLeft (labelW));
        l1.removeFromLeft (6);
        personality.setBounds (l1.removeFromLeft (160));
        auto l2 = line();
        auto left2 = l2.removeFromLeft (labelW + 6 + 160);
        styleLabel.setBounds (left2.removeFromLeft (labelW));
        left2.removeFromLeft (6);
        style.setBounds (left2);
        l2.removeFromLeft (12);
        rateLabel.setBounds (l2.removeFromLeft (rateLabelW));
        l2.removeFromLeft (6);
        rate.setBounds (l2);
        auto l3 = line();
        auto left3 = l3.removeFromLeft (labelW + 6 + 160);
        modeLabel.setBounds (left3.removeFromLeft (labelW));
        left3.removeFromLeft (6);
        mode.setBounds (left3);
        l3.removeFromLeft (12);
        pitchLabel.setBounds (l3);
        keyboard.setBounds (v.withTrimmedTop (2));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            finish (false);
            return true;
        }
        return false;
    }

private:
    void showSettings()
    {
        personality.setSelectedId (settings.personality + 1, juce::dontSendNotification);
        style.setSelectedId ((int) settings.style + 1, juce::dontSendNotification);
        mode.setSelectedId ((int) settings.voicing + 1, juce::dontSendNotification);
        rate.setValue (juce::jlimit (speech::minRate, speech::maxRate, settings.rate), juce::dontSendNotification);
        showPitch();
    }

    void showPitch()
    {
        const auto key = juce::jlimit (speech::minKey, speech::maxKey, speech::hzToKey (settings.pitch));
        keyboard.setKey (key);
        pitchLabel.setText (tr ("Pitch: {0} Hz ({1})", { juce::String (settings.pitch), keyName (key) }), juce::dontSendNotification);
    }

    void listen()
    {
        if (settings.text.empty())
            return;
        auto spoken = speech::render (settings, 44100.0);
        if (spoken.samples.empty())
            return;
        auto data = std::make_shared<SampleData>();
        data->sampleRate = spoken.sampleRate;
        data->left = std::move (spoken.samples);
        engine.previewSample (std::move (data));
    }

    void finish (bool accepted)
    {
        if (finished)
            return;
        finished = true;
        engine.previewSample (nullptr);
        auto done = onDone;
        const auto s = settings;
        const bool keep = accepted && changed;
        closeToolWindowOf (*this); // (deletes this)
        if (done)
            done (keep, s);
    }

    AudioEngine& engine;
    speech::Settings settings;
    std::function<void (bool, const speech::Settings&)> onDone;
    bool changed = false, finished = false;
    juce::Rectangle<int> voiceArea;

    juce::Label title, voiceHeader, personalityLabel, styleLabel, modeLabel, rateLabel, pitchLabel;
    juce::TextEditor text;
    juce::TextButton listenButton, acceptButton, cancelButton;
    juce::ComboBox personality, style, mode;
    juce::Slider rate;
    PitchKeyboard keyboard;
};
} // namespace

juce::File userSpeechFolder()
{
    return platform::settingsFile ("Presets").getChildFile ("Speech");
}

std::unique_ptr<juce::Component> makePanel (AudioEngine& engine, const speech::Settings& settings, std::function<void (bool, const speech::Settings&)> onDone)
{
    return std::make_unique<SpeechPanel> (engine, settings, std::move (onDone));
}

void editThenLoad (AudioEngine& engine, const juce::File& file, std::function<void (const juce::File&)> then)
{
    speech::Settings settings;
    if (! file.hasFileExtension ("speech") || ! speech::parse (file.loadFileAsString().toStdString(), settings))
    {
        then (file);
        return;
    }
    auto panel = makePanel (engine, settings, [file, then] (bool accepted, const speech::Settings& edited) {
        if (! accepted)
            return then (file);
        // FL suggests "<Text in CamelCase>_<Personality>.speech"; LYNX saves it in its own speech folder (FL's
        // factory folder is read-only) without asking, numbering it when the name is taken
        auto folder = userSpeechFolder();
        folder.createDirectory();
        auto target = folder.getChildFile (juce::String::fromUTF8 (speech::suggestedFileName (edited).c_str()));
        if (target.existsAsFile())
            target = target.getNonexistentSibling (false);
        if (! target.replaceWithText (juce::String::fromUTF8 (speech::write (edited).c_str()), false, false, "\r\n"))
            return then (file);
        then (target);
    });
    openToolWindow (std::move (panel), tr ("Speech properties"), "speech");
}
} // namespace lynx::speechui
