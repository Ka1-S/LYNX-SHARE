#include "ui/TempoTapper.h"

#include "core/ClipOps.h"
#include "core/ValueMath.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
// A button that reports press and release (the nudge buttons act while held).
class HoldButton final : public juce::TextButton
{
public:
    std::function<void (bool)> onHold;

    void buttonStateChanged() override
    {
        const auto down = isDown();
        if (down != held)
        {
            held = down;
            if (onHold != nullptr)
                onHold (held);
        }
    }

private:
    bool held = false;
};

class TempoTapperPanel final : public juce::Component,
                               private juce::Timer
{
public:
    explicit TempoTapperPanel (AudioEngine& e) : engine (e)
    {
        setWantsKeyboardFocus (true);

        applyTempo.setButtonText (tr ("Apply tempo"));
        applyTempo.setToggleState (true, juce::dontSendNotification);
        applyTempo.onClick = [this] {
            if (applyTempo.getToggleState() && lastBpm > 0.0)
                setTempo (lastBpm);
            refreshHints();
        };
        sync.setButtonText (tr ("Sync"));
        sync.onClick = [this] { refreshHints(); };

        nudgeSlower.setButtonText (tr ("- nudge"));
        nudgeFaster.setButtonText (tr ("nudge +"));
        nudgeSlower.onHold = [this] (bool down) { nudge (down, false); };
        nudgeFaster.onHold = [this] (bool down) { nudge (down, true); };

        for (auto* c : std::initializer_list<juce::Component*> { &applyTempo, &sync, &nudgeSlower, &nudgeFaster })
        {
            c->setWantsKeyboardFocus (false);
            c->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (c);
        }

        refreshHints();
        setSize (240, 330);
        startTimerHz (30);
    }

    ~TempoTapperPanel() override
    {
        if (nudging)
            nudge (false, false);
    }

    void tap()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (const auto bpm = taps.tap (now))
        {
            lastBpm = *bpm;
            if (applyTempo.getToggleState())
                setTempo (*bpm);
        }

        // Sync: during playback the playhead jumps to the nearest beat, so the beat falls on the tap
        auto& transport = engine.getTransport();
        if (sync.getToggleState() && transport.playing.load())
            engine.seek (controls::nearestBeatTicks (transport.positionTicks.load()));

        flash = 1.0f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        const auto pad = padBounds().toFloat();
        g.setColour (colour::bg4.interpolatedWith (colour::accent, flash));
        g.fillRoundedRectangle (pad, radius::lg);
        g.setColour (colour::border);
        g.drawRoundedRectangle (pad.reduced (0.5f), radius::lg, 1.0f);
        icons::draw (g, Icon::tap, pad.withSizeKeepingCentre (28.0f, 28.0f).translated (0.0f, -22.0f), colour::textPrimary);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::display, true));
        g.drawText (tr ("TAP"), pad.withTrimmedTop (pad.getHeight() * 0.5f).withHeight (30.0f), juce::Justification::centred);

        // the measured tempo and the state
        auto info = getLocalBounds().reduced (12).withTop (padBounds().getBottom() + 8).withHeight (44);
        g.setFont (monoFont (22.0f));
        g.setColour (lastBpm > 0.0 ? colour::textPrimary : colour::textDisabled);
        g.drawText (lastBpm > 0.0 ? juce::String (lastBpm, 2) + " BPM" : juce::String ("--- BPM"), info.removeFromTop (26), juce::Justification::centred);
        g.setFont (font (type::small));
        g.setColour (colour::textSecondary);
        g.drawText (stateText(), info, juce::Justification::centred);

        // the time left before the taps start over
        const auto gauge = getLocalBounds().reduced (12).withTop (info.getBottom() + 2).withHeight (4).toFloat();
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (gauge, 2.0f);
        if (const auto left = waitLeft(); left > 0.0)
        {
            g.setColour (colour::accent.withAlpha (0.8f));
            g.fillRoundedRectangle (gauge.withWidth (gauge.getWidth() * (float) left), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        r.removeFromTop (padBounds().getHeight() + 8 + 44 + 10);
        auto nudges = r.removeFromTop (30);
        nudgeSlower.setBounds (nudges.removeFromLeft (nudges.getWidth() / 2).reduced (2, 0));
        nudgeFaster.setBounds (nudges.reduced (2, 0));
        r.removeFromTop (6);
        auto toggles = r.removeFromTop (24);
        applyTempo.setBounds (toggles.removeFromLeft (toggles.getWidth() * 3 / 5));
        sync.setBounds (toggles);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (padBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu())
            tap();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
            return false; // the tool window closes
        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            return true;  // the nudge keys act while held (keyStateChanged)
        tap();            // any other key taps, like FL's tapper
        return true;
    }

    bool keyStateChanged (bool) override
    {
        const auto left = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::leftKey);
        const auto right = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::rightKey);
        if (left != keyNudge[0] || right != keyNudge[1])
        {
            keyNudge = { left, right };
            nudge (left || right, right);
        }
        return false;
    }

    void setTapsForSnapshot (int count, double bpm)
    {
        const auto interval = 60000.0 / std::max (1.0, bpm);
        const auto start = juce::Time::getMillisecondCounterHiRes() - interval * (count - 1);
        for (int i = 0; i < count; ++i)
            if (const auto b = taps.tap (start + interval * i))
                lastBpm = *b;
        repaint();
    }

private:
    juce::Rectangle<int> padBounds() const { return getLocalBounds().reduced (12).withHeight (140); }

    double waitLeft() const
    {
        if (taps.count() == 0)
            return 0.0;
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - taps.lastTap();
        return juce::jlimit (0.0, 1.0, 1.0 - elapsed / controls::TapTempo::resetMs);
    }

    juce::String stateText() const
    {
        if (taps.count() > 0 && waitLeft() <= 0.0)
            return tr ("Click the pad or press any key on each beat");
        switch (taps.state())
        {
            case controls::TapTempo::State::waiting: return taps.count() == 0 ? tr ("Click the pad or press any key on each beat") : tr ("Waiting for more taps...");
            case controls::TapTempo::State::tapping: return tr ("Tapping... ({0} taps)", { juce::String (taps.count()) });
            case controls::TapTempo::State::stable: return tr ("Stable ({0} taps)", { juce::String (taps.count()) });
        }
        return {};
    }

    void setTempo (double bpm)
    {
        bpm = controls::clampTempo (bpm);
        if (std::abs (bpm - engine.getSong().bpm) > 1e-9)
            engine.editSong ([bpm] (Song& s) { clipops::followTempo (s, bpm); });
    }

    // while a nudge is held the song runs 4% slower or faster (no undo step); the release restores the tempo
    void nudge (bool down, bool faster)
    {
        if (down && ! nudging)
        {
            nudging = true;
            nudgeBase = engine.getSong().bpm;
            const auto bpm = controls::nudgedTempo (nudgeBase, faster);
            engine.editSong ([bpm] (Song& s) { s.bpm = bpm; }, false);
        }
        else if (! down && nudging)
        {
            nudging = false;
            const auto bpm = nudgeBase;
            engine.editSong ([bpm] (Song& s) { s.bpm = bpm; }, false);
        }
    }

    void refreshHints()
    {
        ui::setHint (applyTempo, tr ("Apply tempo: the song's tempo follows the taps (off: the tapper only measures)"));
        ui::setHint (sync, tr ("Sync: during playback each tap puts the playhead on the nearest beat"));
        ui::setHint (nudgeSlower, tr ("Hold to run the song 4% slower and drift the beat back ([Left] key)"));
        ui::setHint (nudgeFaster, tr ("Hold to run the song 4% faster and drift the beat forward ([Right] key)"));
        ui::setHint (*this, tr ("Tempo tapper: click the pad or press any key on each beat; the tempo follows from the second tap"));
    }

    void timerCallback() override
    {
        if (flash > 0.0f)
            flash = std::max (0.0f, flash - 0.2f);
        if (flash > 0.0f || taps.count() > 0)
            repaint();
        if (taps.count() > 0 && waitLeft() <= 0.0)
            taps.reset();
    }

    AudioEngine& engine;
    controls::TapTempo taps;
    double lastBpm = 0.0, nudgeBase = 0.0;
    float flash = 0.0f;
    bool nudging = false;
    std::array<bool, 2> keyNudge {};
    juce::ToggleButton applyTempo, sync;
    HoldButton nudgeSlower, nudgeFaster;
};
} // namespace

void openTempoTapper (AudioEngine& engine)
{
    auto panel = std::make_unique<TempoTapperPanel> (engine);
    auto* raw = panel.get();
    openToolWindow (std::move (panel), tr ("Tempo tapper"), "tempo-tapper");
    raw->grabKeyboardFocus();
}

std::unique_ptr<juce::Component> makeTempoTapperForSnapshot (AudioEngine& engine, int taps, double bpm)
{
    auto panel = std::make_unique<TempoTapperPanel> (engine);
    panel->setTapsForSnapshot (taps, bpm);
    return panel;
}
} // namespace lynx
