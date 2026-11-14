#include "ui/AudioCheck.h"

#include "core/AudioCheck.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
const char* osName()
{
   #if JUCE_WINDOWS
    return "windows";
   #elif JUCE_MAC
    return "macos";
   #else
    return "linux";
   #endif
}

// the output level, a bar on a dB scale (−60…0 dBFS) with the highest level since the tone started as a tick
class LevelBar final : public juce::Component
{
public:
    void set (float level, float held)
    {
        if (std::abs (level - shown) < 1.0e-4f && std::abs (held - peak) < 1.0e-4f)
            return;
        shown = level;
        peak = held;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (r, 3.0f);
        auto toX = [&r] (float v) {
            const auto db = juce::Decibels::gainToDecibels (v, -60.0f);
            return r.getX() + r.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        };
        const auto x = toX (shown);
        g.setGradientFill (juce::ColourGradient (theme::colour::meterLow, r.getX(), 0.0f, theme::colour::meterClip, r.getRight(), 0.0f, false));
        g.saveState();
        g.reduceClipRegion (r.withRight (x).toNearestInt());
        g.fillRoundedRectangle (r.reduced (1.0f), 2.0f);
        g.restoreState();
        if (peak > 0.0f)
        {
            g.setColour (theme::colour::textPrimary);
            g.fillRect (juce::Rectangle<float> (toX (peak) - 1.0f, r.getY() + 1.0f, 2.0f, r.getHeight() - 2.0f));
        }
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    }

private:
    float shown = 0.0f, peak = 0.0f;
};

class Panel final : public juce::Component,
                    private juce::Timer,
                    private juce::ChangeListener
{
public:
    Panel (AudioEngine& e, bool keep, std::function<void()> heightChanged)
        : engine (e), persistent (keep), onHeightChanged (std::move (heightChanged))
    {
        for (auto* l : { &device, &question, &advice })
        {
            l->setColour (juce::Label::textColourId, l == &device ? theme::colour::textSecondary : theme::colour::textPrimary);
            l->setFont (theme::font (theme::type::body));
            l->setJustificationType (juce::Justification::topLeft);
            l->setMinimumHorizontalScale (1.0f);
            addAndMakeVisible (*l);
        }
        question.setText (tr ("Did you hear the tone?"), juce::dontSendNotification);
        play.setButtonText (tr ("Play test sound"));
        play.setTooltip (tr ("Plays a one-second 440 Hz tone through the output device; the bar shows the level that reaches it"));
        play.onClick = [this] { playTone(); };
        heard.setButtonText (tr ("I heard it"));
        heard.onClick = [this] { answer (true); };
        nothing.setButtonText (tr ("I heard nothing"));
        nothing.onClick = [this] { answer (false); };
        for (auto* b : { &play, &heard, &nothing })
            addAndMakeVisible (*b);
        addAndMakeVisible (bar);
        if (persistent)
        {
            meter = engine.getDeviceManager().getOutputLevelGetter();
            engine.getDeviceManager().addChangeListener (this);
        }
        refresh();
        startTimerHz (30);
    }

    ~Panel() override
    {
        if (persistent)
            engine.getDeviceManager().removeChangeListener (this);
    }

    int preferredHeight (int width) const
    {
        auto h = 22 + 6 + 28 + 8; // the device line, the play row
        if (stage != Stage::ready)
            h += 26 + 4;          // the question and its answers
        if (! advice.getText().isEmpty())
            h += textHeight (advice, width) + 6;
        h += (int) tries.size() * 30;
        return h + 4;
    }

    // --snapshot: 0 ready, 1 playing, 2 heard, 3 reached but not heard, 4 never reached
    void showState (int state)
    {
        sampleDevice = true;
        refresh();
        if (state == 1)
        {
            stage = Stage::asked;
            bar.set (0.42f, 0.5f);
        }
        else if (state >= 2)
        {
            stage = Stage::asked;
            peakSeen = state == 3 ? 0.5f : state == 4 ? 0.0f : 0.5f;
            bar.set (0.0f, peakSeen);
            answer (state == 2);
        }
        resized();
    }

private:
    enum class Stage
    {
        ready,  // nothing played yet
        asked,  // played: did you hear it?
        answered
    };

    static int textHeight (const juce::Label& l, int width)
    {
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        a.append (l.getText(), l.getFont());
        juce::TextLayout layout;
        layout.createLayout (a, (float) juce::jmax (100, width - 8));
        return (int) std::ceil (layout.getHeight()) + 6;
    }

    juce::String deviceText()
    {
        if (sampleDevice || ! persistent)
            return tr ("Output: {0} > {1} ({2} Hz, {3} samples)", { "Windows Audio", "Speakers (USB Audio CODEC)", "48000", "480" });
        auto* d = engine.getDeviceManager().getCurrentAudioDevice();
        if (d == nullptr)
            return tr ("No audio output device is open.");
        return tr ("Output: {0} > {1} ({2} Hz, {3} samples)", { d->getTypeName(), d->getName(), juce::String ((int) d->getCurrentSampleRate()),
                                                                juce::String (d->getCurrentBufferSizeSamples()) });
    }

    void refresh()
    {
        device.setText (deviceText(), juce::dontSendNotification);
        const auto open = sampleDevice || ! persistent || engine.getDeviceManager().getCurrentAudioDevice() != nullptr;
        play.setEnabled (open);
        question.setVisible (stage != Stage::ready);
        heard.setVisible (stage != Stage::ready);
        nothing.setVisible (stage != Stage::ready);
    }

    void playTone()
    {
        peakSeen = 0.0f;
        playingUntil = juce::Time::getMillisecondCounterHiRes() + 1200.0;
        if (persistent)
            engine.getDeviceManager().playTestSound();
        if (platform::testMode())
            platform::testModeLog ("audio check: test tone on " + deviceText());
        stage = Stage::asked;
        advice.setText ({}, juce::dontSendNotification);
        tries.clear();
        refresh();
        relayout();
    }

    void timerCallback() override
    {
        if (! persistent || sampleDevice)
            return;
        const auto level = meter != nullptr ? (float) meter->getCurrentLevel() : 0.0f;
        if (juce::Time::getMillisecondCounterHiRes() < playingUntil)
            peakSeen = juce::jmax (peakSeen, level);
        bar.set (level, peakSeen);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refresh(); // (another device was chosen, here or in the selector above)
        if (pendingReplay)
        {
            pendingReplay = false;
            juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<Panel> (this)] {
                if (safe != nullptr)
                    safe->playTone();
            });
        }
    }

    void answer (bool yes)
    {
        const auto open = sampleDevice || ! persistent || engine.getDeviceManager().getCurrentAudioDevice() != nullptr;
        stage = Stage::answered;
        showAdvice (audiocheck::verdict (open, peakSeen, yes));
    }

    void showAdvice (audiocheck::Verdict v)
    {
        juce::String text;
        tries.clear();
        switch (v)
        {
            case audiocheck::Verdict::works: text = tr ("Sound works. You're ready to make music."); break;
            case audiocheck::Verdict::reachedNotHeard:
                text = tr ("The tone reached the device, so the sound goes out somewhere you aren't listening: check that your speakers or headphones "
                           "are on this device, turned on and not muted, and the system volume. Or try another device:");
                break;
            case audiocheck::Verdict::notReached:
                text = tr ("The tone never reached the device: the driver isn't playing. Try another device or driver:");
                break;
            case audiocheck::Verdict::noDevice: text = tr ("No audio output device is open. Choose one:"); break;
        }
        advice.setText (text, juce::dontSendNotification);
        advice.setColour (juce::Label::textColourId, v == audiocheck::Verdict::works ? theme::colour::ledOn : theme::colour::textPrimary);
        if (v != audiocheck::Verdict::works)
            for (const auto& c : alternatives())
            {
                auto b = std::make_unique<juce::TextButton> (tr ("Try {0}: {1}", { juce::String::fromUTF8 (c.type.c_str()), juce::String::fromUTF8 (c.name.c_str()) }));
                b->setTooltip (tr ("Switches the output to this device and plays the tone again"));
                b->onClick = [this, c] { switchTo (c); };
                addAndMakeVisible (*b);
                tries.push_back (std::move (b));
            }
        refresh();
        relayout();
    }

    std::vector<audiocheck::Choice> alternatives()
    {
        std::vector<std::pair<std::string, std::vector<std::string>>> available;
        audiocheck::Choice current;
        if (sampleDevice || ! persistent)
        {
            available = { { "Windows Audio", { "Speakers (USB Audio CODEC)", "Headphones (Realtek)" } },
                          { "DirectSound", { "Primary Sound Driver" } },
                          { "ASIO", { "FL Studio ASIO" } } };
            current = { "Windows Audio", "Speakers (USB Audio CODEC)" };
        }
        else
        {
            auto& dm = engine.getDeviceManager();
            for (auto* type : dm.getAvailableDeviceTypes())
            {
                type->scanForDevices();
                std::vector<std::string> names;
                for (const auto& n : type->getDeviceNames (false))
                    names.push_back (n.toStdString());
                available.push_back ({ type->getTypeName().toStdString(), names });
            }
            if (auto* d = dm.getCurrentAudioDevice())
                current = { d->getTypeName().toStdString(), d->getName().toStdString() };
        }
        return audiocheck::alternatives (osName(), current, available);
    }

    void switchTo (const audiocheck::Choice& c)
    {
        if (! persistent || sampleDevice)
            return;
        auto& dm = engine.getDeviceManager();
        const auto type = juce::String::fromUTF8 (c.type.c_str());
        if (dm.getCurrentAudioDeviceType() != type)
            dm.setCurrentAudioDeviceType (type, true);
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = juce::String::fromUTF8 (c.name.c_str());
        setup.useDefaultOutputChannels = true;
        pendingReplay = true;
        const auto error = dm.setAudioDeviceSetup (setup, true);
        if (error.isNotEmpty())
        {
            pendingReplay = false;
            advice.setText (tr ("{0} could not be opened: {1}", { setup.outputDeviceName, error }), juce::dontSendNotification);
            relayout();
            return;
        }
        if (platform::testMode())
            platform::testModeLog ("audio check: switched to " + type + " > " + setup.outputDeviceName);
    }

    void relayout()
    {
        resized();
        if (onHeightChanged)
            onHeightChanged();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        device.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        auto row = r.removeFromTop (28);
        play.setBounds (row.removeFromLeft (juce::jmax (150, play.getBestWidthForHeight (28) + 16)));
        row.removeFromLeft (10);
        bar.setBounds (row.reduced (0, 7));
        r.removeFromTop (8);
        if (stage != Stage::ready)
        {
            row = r.removeFromTop (26);
            nothing.setBounds (row.removeFromRight (juce::jmax (130, nothing.getBestWidthForHeight (26) + 16)));
            row.removeFromRight (6);
            heard.setBounds (row.removeFromRight (juce::jmax (110, heard.getBestWidthForHeight (26) + 16)));
            question.setBounds (row);
            r.removeFromTop (4);
        }
        if (! advice.getText().isEmpty())
        {
            advice.setBounds (r.removeFromTop (textHeight (advice, getWidth())));
            r.removeFromTop (6);
        }
        else
            advice.setBounds ({});
        for (auto& b : tries)
        {
            b->setBounds (r.removeFromTop (26).withWidth (juce::jmin (r.getWidth(), 420)));
            r.removeFromTop (4);
        }
    }

    AudioEngine& engine;
    bool persistent;
    bool sampleDevice = false; // (--snapshot)
    std::function<void()> onHeightChanged;
    juce::AudioDeviceManager::LevelMeter::Ptr meter;
    juce::Label device, question, advice;
    juce::TextButton play, heard, nothing;
    LevelBar bar;
    std::vector<std::unique_ptr<juce::TextButton>> tries;
    Stage stage = Stage::ready;
    float peakSeen = 0.0f;
    double playingUntil = 0.0;
    bool pendingReplay = false;
};

// Help › Test audio output...: the panel in a window, laid out to its height
class Window final : public juce::Component
{
public:
    explicit Window (AudioEngine& engine, bool persistent)
        : panel (engine, persistent, [this] {
              if (ready)
                  fit();
          })
    {
        title.setText (tr ("Test audio output"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title + 3.0f, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);
        addAndMakeVisible (panel);
        close.setButtonText (tr ("Close"));
        close.onClick = [this] {
            if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
                w->exitModalState (0);
        };
        addAndMakeVisible (close);
        setSize (560, 200);
        ready = true;
        fit();
    }
    bool ready = false; // (the panel may ask for a layout while it is being made)
    Panel panel;

private:
    void fit() { setSize (getWidth(), 16 + 30 + 8 + panel.preferredHeight (getWidth() - 32) + 12 + 30 + 16); }
    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        title.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        close.setBounds (r.removeFromBottom (30).removeFromRight (100));
        r.removeFromBottom (12);
        panel.setBounds (r);
    }
    juce::Label title;
    juce::TextButton close;
};
} // namespace

std::unique_ptr<juce::Component> makeAudioCheckPanel (AudioEngine& engine, bool persistent, std::function<void()> onHeightChanged)
{
    return std::make_unique<Panel> (engine, persistent, std::move (onHeightChanged));
}

int audioCheckHeight (const juce::Component& panel, int width)
{
    if (const auto* p = dynamic_cast<const Panel*> (&panel))
        return p->preferredHeight (width);
    return panel.getHeight();
}

void showAudioCheck (AudioEngine& engine)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new Window (engine, true));
    options.dialogTitle = tr ("Test audio output");
    options.dialogBackgroundColour = theme::colour::bg2;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    if (auto* dialog = options.launchAsync())
        keepAboveMainWindow (*dialog);
}

std::unique_ptr<juce::Component> makeAudioCheckForSnapshot (AudioEngine& engine, int state)
{
    auto w = std::make_unique<Window> (engine, false);
    w->panel.showState (state);
    return w;
}
} // namespace lynx
