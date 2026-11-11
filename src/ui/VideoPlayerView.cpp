#include "ui/VideoPlayerView.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <cstring>
#include <thread>

#include "core/VideoPlayer.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/ControllerKnob.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/VideoMedia.h"
#include "ui/VisualEditors.h"

namespace lynx
{
namespace
{
juce::File resolveVideo (const std::string& path)
{
    if (path.empty())
        return {};
    return platform::localFile (path);
}

AudioEngine* installedEngine = nullptr;

// (the decoders: ui/VideoMedia, round 174d)
using media::FrameDecoder;
media::Probe probe (const juce::String& path) { return media::probe (path); }

std::shared_ptr<const video::AudioTrack> provideTrack (const std::string& path, bool wait)
{
    return media::provideTrack (path, wait, [] {
        // the channel's instrument takes it at the next snapshot
        if (installedEngine != nullptr)
            installedEngine->editSong ([] (Song&) {}, false);
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Where the video is drawn (the editor's area or the detached window)
class VideoSurface final : public juce::Component
{
public:
    std::function<void (juce::Rectangle<int>)> onSize;
    std::function<void()> onDoubleClick;
    juce::String message; // drawn when there is no frame
    bool keepAspect = true;
    juce::Image image;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black);
        if (image.isValid())
        {
            const auto area = getLocalBounds().toFloat();
            if (keepAspect)
                g.drawImageWithin (image, 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::centred, false);
            else
                g.drawImage (image, area);
            return;
        }
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body));
        g.drawFittedText (message, getLocalBounds().reduced (16), juce::Justification::centred, 12);
    }

    void resized() override
    {
        if (onSize)
            onSize (getLocalBounds());
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
    }
};

// A small icon button (drawn paths, LYNX's style)
class IconButton final : public juce::Button
{
public:
    enum class Icon { load, options, play, pause, stop, loop, sound, mute, detach, start, end, link };
    // FL's panel (round 174d, FL 2026 on screen): a round folder button, bare icons (▾, the chain, the speaker), the
    // transport as one pill [∞ | ▶ | ■], small square ◂ / ▸ beside the start / end times
    enum class Shape { circle, bare, pillLeft, pillMid, pillRight, small };

    IconButton (Icon i, Shape sh = Shape::bare) : juce::Button ({}), icon (i), shape (sh) {}
    Icon icon;
    Shape shape;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto on = getToggleState();
        const auto fill = down ? theme::colour::bg6 : over ? theme::colour::bg5 : theme::colour::bg4;
        switch (shape)
        {
            case Shape::circle:
                r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
                g.setColour (fill);
                g.fillEllipse (r);
                break;
            case Shape::pillLeft: case Shape::pillMid: case Shape::pillRight:
            {
                const auto left = shape == Shape::pillLeft, right = shape == Shape::pillRight;
                const auto radius = r.getHeight() * 0.5f;
                juce::Path bg;
                bg.addRoundedRectangle (r.getX(), r.getY(), r.getWidth() + (right ? 0.0f : 1.0f), r.getHeight(), radius, radius,
                                        left, right, left, right);
                g.setColour (fill);
                g.fillPath (bg);
                if (! left)
                {
                    g.setColour (theme::colour::bg2);
                    g.fillRect (r.getX(), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f);
                }
                break;
            }
            case Shape::small:
                g.setColour (fill);
                g.fillRoundedRectangle (r, 3.0f);
                break;
            case Shape::bare:
                break;
        }
        g.setColour (on ? theme::colour::accent : over && shape == Shape::bare ? juce::Colours::white : theme::colour::textPrimary);
        const auto c = r.getCentre();
        const auto s = juce::jmin (r.getWidth(), r.getHeight()) * (shape == Shape::small ? 0.26f : 0.30f);
        juce::Path p;
        switch (icon)
        {
            case Icon::link:
                // two chain links, crossing at 45°
                for (const auto d : { -1.0f, 1.0f })
                {
                    juce::Path l;
                    l.addRoundedRectangle (-s * 0.95f, -s * 0.42f, s * 1.9f, s * 0.84f, s * 0.42f);
                    l.applyTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::pi * 0.25f)
                                          .translated (c.x + d * s * 0.45f, c.y - d * s * 0.45f));
                    g.strokePath (l, juce::PathStrokeType (1.6f));
                }
                break;
            case Icon::play:
                p.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y);
                g.fillPath (p);
                break;
            case Icon::pause:
                g.fillRect (juce::Rectangle<float> (c.x - s * 0.8f, c.y - s, s * 0.55f, s * 2.0f));
                g.fillRect (juce::Rectangle<float> (c.x + s * 0.25f, c.y - s, s * 0.55f, s * 2.0f));
                break;
            case Icon::stop:
                g.fillRect (juce::Rectangle<float> (c.x - s * 0.8f, c.y - s * 0.8f, s * 1.6f, s * 1.6f));
                break;
            case Icon::loop:
            {
                // FL's loop: ∞
                for (int k = 0; k <= 64; ++k)
                {
                    const auto t = (float) k / 64.0f * juce::MathConstants<float>::twoPi;
                    const auto d = 1.0f + std::sin (t) * std::sin (t);
                    const auto x = c.x + s * 1.25f * std::cos (t) / d, y = c.y + s * 1.25f * std::sin (t) * std::cos (t) / d;
                    if (k == 0)
                        p.startNewSubPath (x, y);
                    else
                        p.lineTo (x, y);
                }
                g.strokePath (p, juce::PathStrokeType (1.7f));
                break;
            }
            case Icon::sound:
            case Icon::mute:
                p.startNewSubPath (c.x - s, c.y - s * 0.35f);
                p.lineTo (c.x - s * 0.45f, c.y - s * 0.35f);
                p.lineTo (c.x + s * 0.15f, c.y - s);
                p.lineTo (c.x + s * 0.15f, c.y + s);
                p.lineTo (c.x - s * 0.45f, c.y + s * 0.35f);
                p.lineTo (c.x - s, c.y + s * 0.35f);
                p.closeSubPath();
                g.fillPath (p);
                if (icon == Icon::mute)
                {
                    g.drawLine (c.x + s * 0.4f, c.y - s * 0.45f, c.x + s * 1.1f, c.y + s * 0.45f, 1.6f);
                    g.drawLine (c.x + s * 0.4f, c.y + s * 0.45f, c.x + s * 1.1f, c.y - s * 0.45f, 1.6f);
                }
                else
                {
                    juce::Path w;
                    w.addCentredArc (c.x + s * 0.2f, c.y, s * 0.75f, s * 0.75f, 0.0f, 0.6f, 2.55f, true);
                    g.strokePath (w, juce::PathStrokeType (1.4f));
                }
                break;
            case Icon::load:
                p.startNewSubPath (c.x - s * 1.1f, c.y + s * 0.8f);
                p.lineTo (c.x - s * 1.1f, c.y - s * 0.8f);
                p.lineTo (c.x - s * 0.3f, c.y - s * 0.8f);
                p.lineTo (c.x, c.y - s * 0.45f);
                p.lineTo (c.x + s * 1.1f, c.y - s * 0.45f);
                p.lineTo (c.x + s * 1.1f, c.y + s * 0.8f);
                p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (1.5f));
                break;
            case Icon::options:
                p.addTriangle (c.x - s * 0.8f, c.y - s * 0.35f, c.x + s * 0.8f, c.y - s * 0.35f, c.x, c.y + s * 0.5f);
                g.fillPath (p);
                break;
            case Icon::detach:
                g.drawRect (juce::Rectangle<float> (c.x - s, c.y - s * 0.6f, s * 1.5f, s * 1.4f), 1.4f);
                g.drawLine (c.x - s * 0.1f, c.y + s * 0.1f, c.x + s * 1.0f, c.y - s * 1.0f, 1.4f);
                g.drawLine (c.x + s * 0.35f, c.y - s * 1.0f, c.x + s * 1.0f, c.y - s * 1.0f, 1.4f);
                g.drawLine (c.x + s * 1.0f, c.y - s * 1.0f, c.x + s * 1.0f, c.y - s * 0.35f, 1.4f);
                break;
            case Icon::start:
            case Icon::end:
            {
                // FL's small ◂ (set the start here) / ▸ (set the end here)
                const auto dir = icon == Icon::start ? -1.0f : 1.0f;
                p.addTriangle (c.x + dir * s, c.y, c.x - dir * s * 0.7f, c.y - s, c.x - dir * s * 0.7f, c.y + s);
                g.fillPath (p);
                break;
            }
        }
    }
};

// The video position: the whole video, the start…end range, the time now; drag to move
class TimeRail final : public juce::Component
{
public:
    std::function<void (double)> onSeek; // seconds
    double duration = 0.0, start = 0.0, end = 0.0, now = 0.0;

    void paint (juce::Graphics& g) override
    {
        // FL's slider: a full-width groove, the start…end range in colour, a big round handle
        const auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
        const auto rail = r.withSizeKeepingCentre (r.getWidth(), 6.0f);
        g.setColour (theme::colour::bg4);
        g.fillRoundedRectangle (rail, 3.0f);
        const auto x = [&] (double t) { return rail.getX() + (duration > 0.0 ? (float) (juce::jlimit (0.0, 1.0, t / duration)) : 0.0f) * rail.getWidth(); };
        if (duration > 0.0)
        {
            // (FL: the start…end range in its colour, no progress fill)
            const auto e = end > 0.0 ? end : duration;
            g.setColour (theme::colour::accent);
            g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (x (start), rail.getY(), x (e), rail.getBottom()), 3.0f);
        }
        const auto knob = juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x (juce::jmax (0.0, now)), r.getCentreY() });
        g.setColour (theme::colour::bg5);
        g.fillEllipse (knob);
        g.setColour (theme::colour::textPrimary);
        g.drawEllipse (knob.reduced (1.0f), 1.4f);
        g.fillRect (knob.withSizeKeepingCentre (1.6f, 8.0f));
    }

    void mouseDown (const juce::MouseEvent& e) override { seekAt (e.x); }
    void mouseDrag (const juce::MouseEvent& e) override { seekAt (e.x); }

private:
    void seekAt (int px)
    {
        if (duration <= 0.0 || ! onSeek)
            return;
        const auto r = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
        onSeek (juce::jlimit (0.0, duration, (double) (px - r.getX()) / juce::jmax (1.0f, r.getWidth()) * duration));
    }
};

// The control panel's background (docked below the video, or over its bottom like FL's undocked panel)
class PanelBack final : public juce::Component
{
public:
    juce::String status;
    bool overlay = false;
    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg3.withAlpha (overlay ? 0.82f : 1.0f));
        g.fillRect (getLocalBounds());
        g.setColour (theme::colour::border);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (status, getLocalBounds().withTrimmedTop (getHeight() - 18).reduced (8, 0), juce::Justification::centredLeft);
    }
};

class ClickLabel final : public juce::Label
{
public:
    std::function<void()> onLeft, onRight;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRight)
                onRight();
        }
        else if (onLeft)
            onLeft();
    }
};

// FL's typed time (round 174d A2 [DECOMP SetTimeLabelClick 0x72a030]): whole numbers split at ':' — S, S:x, M:S:x or
// H:M:S:x, where x is milliseconds, or frames in an SMPTE time display (frames → ms at the display's whole rate: 24 for
// 23.976 / 24, 25, 30 for 29.97 drop / 30, the video's rate for Auto; FL takes 1 for the other rates, which drops the
// frames — LYNX uses their whole rate); a '-' anywhere makes it negative. Not a number: nothing.
int typingFps (int mode, double videoFps)
{
    switch (mode)
    {
        case video::smpte23976: case video::smpte24: return 24;
        case video::smpte25: return 25;
        case video::smpte2997drop: case video::smpte30: return 30;
        case video::smpteAuto: return juce::jmax (1, juce::roundToInt (videoFps));
        case video::hms: return 1;
        default: return juce::jmax (1, juce::roundToInt (video::timeDisplayFps (mode, videoFps)));
    }
}

std::optional<double> parseTime (const juce::String& typed, int mode = video::hms, double videoFps = 0.0)
{
    const auto negative = typed.containsChar ('-');
    auto parts = juce::StringArray::fromTokens (typed.trim().removeCharacters ("-"), ":", "");
    for (auto& p : parts)
        p = p.trim();
    if (parts.isEmpty() || parts.size() > 4)
        return std::nullopt;
    for (const auto& p : parts)
        if (p.isEmpty() || ! p.containsOnly ("0123456789"))
            return std::nullopt;
    const auto frames = mode != video::hms;
    const auto fps = typingFps (mode, videoFps);
    const auto last = [&] (const juce::String& p) {
        const auto v = p.getLargeIntValue();
        return frames ? (double) juce::roundToInt ((double) ((v % fps) * 1000) / (double) fps) : (double) v;
    };
    double h = 0.0, m = 0.0, sec = 0.0, ms = 0.0;
    if (parts.size() == 1)
        sec = (double) parts[0].getLargeIntValue();
    else if (parts.size() == 2)
        sec = (double) parts[0].getLargeIntValue(), ms = last (parts[1]);
    else if (parts.size() == 3)
        m = (double) parts[0].getLargeIntValue(), sec = (double) parts[1].getLargeIntValue(), ms = last (parts[2]);
    else
        h = (double) parts[0].getLargeIntValue(), m = (double) parts[1].getLargeIntValue(), sec = (double) parts[2].getLargeIntValue(), ms = last (parts[3]);
    const auto t = ((h * 60.0 + m) * 60.0 + sec) + ms / 1000.0;
    return negative ? -t : t;
}

// ---------------------------------------------------------------------------------------------------------------
// FL's in-place type-in (round 174d, FL 2026 on screen): a right click on a time opens a text box over the panel's
// left edge, prefilled with the label's text (selected), and under it a hint row "✓ ✕ <caption>". Enter or ✓ accepts;
// Esc, ✕ or a click elsewhere cancels.
class InlineTypeIn final : public juce::Component, private juce::FocusChangeListener
{
public:
    InlineTypeIn (const juce::String& text, const juce::String& caption) : hint (caption)
    {
        editor.setFont (theme::font (theme::type::display));
        editor.setText (text, false);
        editor.setColour (juce::TextEditor::backgroundColourId, theme::colour::bg0);
        editor.setColour (juce::TextEditor::textColourId, theme::colour::textPrimary);
        editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        editor.setColour (juce::TextEditor::highlightColourId, juce::Colour (0xff1e6fd9));
        editor.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::white);
        editor.setIndents (4, 2);
        editor.onReturnKey = [this] { finish (true); };
        editor.onEscapeKey = [this] { finish (false); };
        addAndMakeVisible (editor);
        setMouseClickGrabsKeyboardFocus (false); // (a click on ✓ / ✕ must not take the focus from the box first)
        juce::Desktop::getInstance().addFocusChangeListener (this);
    }
    ~InlineTypeIn() override { juce::Desktop::getInstance().removeFocusChangeListener (this); }

    static constexpr int boxHeight = 28, hintHeight = 22;

    void start()
    {
        editor.grabKeyboardFocus();
        editor.selectAll();
    }

    void resized() override { editor.setBounds (0, 0, getWidth(), boxHeight); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (boxHeight).toFloat();
        g.setColour (theme::colour::bg0.withAlpha (0.92f));
        g.fillRect (r);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textSecondary);
        g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93"), tickArea().toFloat(), juce::Justification::centred);
        g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x95"), crossArea().toFloat(), juce::Justification::centred);
        g.drawText (hint, r.withTrimmedLeft ((float) crossArea().getRight() + 8.0f), juce::Justification::centredLeft, true);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (tickArea().contains (e.getPosition()))
            finish (true);
        else if (crossArea().contains (e.getPosition()))
            finish (false);
    }

private:
    juce::Rectangle<int> tickArea() const { return { 4, boxHeight, 18, hintHeight }; }
    juce::Rectangle<int> crossArea() const { return { 22, boxHeight, 18, hintHeight }; }

    void globalFocusChanged (juce::Component* focused) override
    {
        if (focused != &editor && focused != this)
            finish (false);
    }

    void finish (bool ok)
    {
        if (done)
            return;
        done = true;
        if (onDone)
            onDone (ok, editor.getText()); // (the owner removes this box asynchronously)
    }

public:
    std::function<void (bool ok, const juce::String& text)> onDone;

private:
    juce::TextEditor editor;
    juce::String hint;
    bool done = false;
};

class VideoPlayerEditor final : public juce::Component,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer,
                                private juce::ChangeListener
{
public:
    // a picture on the screen in place of a video (the Plugin picker's thumbnail, round 175b)
    void showStill (const juce::Image& still)
    {
        surface.image = still;
        locateButton.setVisible (false);
        surface.repaint();
    }

    VideoPlayerEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (surface);
        panelBack.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (panelBack);
        surface.onSize = [this] (juce::Rectangle<int> r) {
            if (window == nullptr)
                sizeDecoder (r);
        };
        surface.onDoubleClick = [this] { detach(); };

        auto icon = [this] (IconButton& b, const juce::String& hint, std::function<void()> click) {
            ui::setHint (b, hint);
            b.onClick = std::move (click);
            addAndMakeVisible (b);
        };
        icon (loadButton, tr ("Load video file..."), [this] { chooseFile(); });
        icon (optionsButton, tr ("Plugin options"), [this] { showOptions(); });
        icon (setStartButton, tr ("Set current position as start time"), [this] { setRange (true, nowTime); });
        icon (setEndButton, tr ("Set current position as end time"), [this] { setRange (false, nowTime); });
        icon (loopButton, tr ("Loop"), [this] { edit ([] (video::Settings& s) { s.loop = ! s.loop; }); });
        icon (playButton, tr ("Play / pause"), [this] { playPause(); });
        icon (stopButton, tr ("Stop"), [this] { stopPlayback(); });
        icon (soundButton, tr ("Mute audio"), [this] { edit ([] (video::Settings& s) { s.mute = ! s.mute; }); });
        icon (detachButton, tr ("Show the video in its own window (double-click the video)"), [this] { detach(); });

        // FL's chain: Slave playback to host
        icon (slaveButton, tr ("Slave playback to host: the song position drives the video (off: the player's own Play / Stop)"),
              [this] { edit ([] (video::Settings& s) { s.slave = ! s.slave; }); });

        ui::ValueSpec vol;
        vol.name = tr ("Volume");
        vol.minimum = 0.0;
        vol.maximum = 127.0;
        vol.interval = 1.0;
        vol.defaultValue = 100.0;
        vol.toText = [] (double v) { return juce::String (juce::roundToInt (v / 100.0 * 100.0)) + "%"; };
        volume = std::make_unique<ctlui::Knob> (&engine, vol, [this] { return (double) settings.volume; },
                                                [this] (double v) { edit ([v] (video::Settings& s) { s.volume = juce::jlimit (0, 127, juce::roundToInt (v)); }); });
        volume->shortName = tr ("Vol");
        volume->compact = true; // (FL: a small dial beside the speaker)
        addAndMakeVisible (*volume);

        for (auto* lab : { &startLabel, &endLabel, &nowLabel })
        {
            lab->setFont (theme::font (theme::type::display)); // (FL: the times are the panel's big white text)
            lab->setColour (juce::Label::textColourId, theme::colour::textPrimary);
            lab->setJustificationType (juce::Justification::centred);
            lab->setBorderSize ({});
            addAndMakeVisible (*lab);
        }
        startLabel.setJustificationType (juce::Justification::centredLeft);
        endLabel.setJustificationType (juce::Justification::centredRight);
        ui::setHint (startLabel, tr ("Start time (click to go to, right click to set)"));
        ui::setHint (endLabel, tr ("End time (click to go to, right click to set)"));
        ui::setHint (nowLabel, tr ("Current time (right click to set)"));
        startLabel.onLeft = [this] { seekVideo (seconds (settings.start)); };
        startLabel.onRight = [this] { typeTime (true); };
        endLabel.onLeft = [this] { seekVideo (endTime()); };
        endLabel.onRight = [this] { typeTime (false); };
        nowLabel.onRight = [this] { typeNow(); };

        rail.onSeek = [this] (double t) { seekVideo (t); };
        ui::setHint (rail, tr ("Video position (use left/right keys for previous/next frame)"));
        addAndMakeVisible (rail);

        locateButton.setButtonText (tr ("Locate ffmpeg..."));
        locateButton.onClick = [this] { chooseFfmpeg(); };
        addChildComponent (locateButton);

        decoder.startThread (juce::Thread::Priority::low);
        engine.addChangeListener (this);
        reload();
        setSize (600, 420);
        startTimerHz (30);
    }

    ~VideoPlayerEditor() override
    {
        engine.removeChangeListener (this);
        window = nullptr;
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto r = getLocalBounds();
        const auto panelHeight = 102;
        panelArea = r.removeFromBottom (panelHeight);
        // FL's "Dock control panel": the panel below the video; else over the video's bottom
        surface.setBounds (settings.dock ? r : getLocalBounds());
        panelBack.overlay = ! settings.dock;
        panelBack.setBounds (panelArea);
        locateButton.setBounds (r.withSizeKeepingCentre (160, 28).translated (0, 40));
        // FL's order (round 174d, FL 2026 on screen): folder, ▾, the chain, [∞ | ▶ | ■] … volume, speaker (LYNX's
        // pop-out after them); the times under the buttons (start ◂ … now … ▸ end); the slider at the bottom
        auto p = panelArea.reduced (10, 6);
        auto row1 = p.removeFromTop (32);
        loadButton.setBounds (row1.removeFromLeft (32));
        row1.removeFromLeft (4);
        optionsButton.setBounds (row1.removeFromLeft (18).withSizeKeepingCentre (18, 22));
        row1.removeFromLeft (52);
        slaveButton.setBounds (row1.removeFromLeft (26).withSizeKeepingCentre (26, 26));
        row1.removeFromLeft (8);
        for (auto* b : { &loopButton, &playButton, &stopButton })
            b->setBounds (row1.removeFromLeft (32).withSizeKeepingCentre (32, 28));
        detachButton.setBounds (row1.removeFromRight (20).withSizeKeepingCentre (20, 20));
        row1.removeFromRight (6);
        soundButton.setBounds (row1.removeFromRight (22).withSizeKeepingCentre (22, 22));
        row1.removeFromRight (2);
        volume->setBounds (row1.removeFromRight (30).withSizeKeepingCentre (30, 30));
        p.removeFromTop (4);
        auto row2 = p.removeFromTop (26);
        const auto timeWidth = juce::jmin (130, row2.getWidth() / 3 - 20);
        startLabel.setBounds (row2.removeFromLeft (timeWidth));
        setStartButton.setBounds (row2.removeFromLeft (18).withSizeKeepingCentre (16, 16));
        endLabel.setBounds (row2.removeFromRight (timeWidth));
        setEndButton.setBounds (row2.removeFromRight (18).withSizeKeepingCentre (16, 16));
        nowLabel.setBounds (row2.reduced (4, 0));
        p.removeFromTop (2);
        rail.setBounds (p.removeFromTop (24));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto frame = 1.0 / (info.fps > 1.0 ? info.fps : 30.0);
        if (key.isKeyCode (juce::KeyPress::leftKey))
        {
            seekVideo (juce::jmax (0.0, nowTime - frame));
            return true;
        }
        if (key.isKeyCode (juce::KeyPress::rightKey))
        {
            seekVideo (nowTime + frame);
            return true;
        }
        return false;
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && juce::File (files[0]).hasFileExtension (videoExtensions);
    }
    void filesDropped (const juce::StringArray& files, int, int) override { load (juce::File (files[0])); }

private:
    static constexpr const char* videoExtensions = "mp4;m4v;mov;avi;mkv;webm;wmv;flv;mpg;mpeg;ts;gif;ogv;3gp";

    static double seconds (std::int64_t units) noexcept { return (double) units / (double) video::unitsPerSecond; }
    static std::int64_t units (double s) noexcept { return (std::int64_t) std::llround (juce::jmax (0.0, s) * (double) video::unitsPerSecond); }
    double endTime() const { return settings.end > 0 ? seconds (settings.end) : info.duration; }

    const ChannelDef* def() const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }

    void edit (std::function<void (video::Settings&)> change, bool undoable = true)
    {
        auto s = settings;
        change (s);
        if (s == settings)
            return;
        settings = s;
        const auto state = video::write (s);
        engine.editSong ([l = locate, state] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
                d->pluginState = state;
        }, undoable);
        refreshControls();
    }

    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        settings = video::readOrDefault (d->pluginState);
        const auto file = resolveVideo (settings.path);
        const auto path = file.getFullPathName();
        if (path != loadedPath)
        {
            loadedPath = path;
            info = path.isNotEmpty() ? probe (path).info : video::Info {};
            decoder.setSource (info.hasVideo ? path : juce::String(), info);
            if (window == nullptr)
                sizeDecoder (surface.getLocalBounds());
        }
        refreshControls();
    }

    void refreshControls()
    {
        loopButton.setToggleState (settings.loop, juce::dontSendNotification);
        slaveButton.setToggleState (settings.slave, juce::dontSendNotification);
        soundButton.icon = settings.mute ? IconButton::Icon::mute : IconButton::Icon::sound;
        soundButton.setToggleState (settings.mute, juce::dontSendNotification);
        surface.keepAspect = settings.keepAspect;
        const auto noDecoder = ! media::canDecodeVideo();
        const auto unreadable = ! settings.path.empty() && resolveVideo (settings.path).existsAsFile() && ! info.hasVideo;
        locateButton.setVisible (noDecoder || (unreadable && ! findFfmpeg().existsAsFile()));
        if (noDecoder)
            surface.message = media::ffmpegInstallHelp();
        else if (settings.path.empty())
            surface.message = tr ("No video") + "\n" + tr ("Load a video file (or drop one here)");
        else if (! resolveVideo (settings.path).existsAsFile())
            surface.message = tr ("The video was not found:") + "\n" + juce::String::fromUTF8 (settings.path.c_str());
        else if (unreadable)
            surface.message = media::cannotReadMessage (resolveVideo (settings.path).getFullPathName());
        else
            surface.message = {};
        juce::String s;
        if (info.hasVideo)
            s << info.width << "x" << info.height << ", " << juce::String (info.fps, 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " fps, "
              << juce::String (video::formatTime (info.duration, video::hms, info.fps)) << (info.hasAudio ? "" : ", " + tr ("no sound"))
              << ", " << probe (loadedPath).backend;
        if (settings.delay > 0)
            s << "  |  " << tr ("Delay playback by {0}", { juce::String (video::formatTime (seconds (settings.delay), settings.timeDisplay, info.fps)) });
        statusLine = s;
        ui::setHint (surface, s); // (FL's panel has no status line: the details are the picture's hint)
        panelBack.repaint();
        if (panelBack.overlay == settings.dock)
            resized();
        rail.duration = info.duration;
        rail.start = seconds (settings.start);
        rail.end = settings.end > 0 ? seconds (settings.end) : 0.0;
        startLabel.setText (video::formatTime (seconds (settings.start), settings.timeDisplay, info.fps), juce::dontSendNotification);
        endLabel.setText (video::formatTime (endTime(), settings.timeDisplay, info.fps), juce::dontSendNotification);
        for (auto* b : { &setStartButton, &setEndButton, &loopButton, &playButton, &stopButton })
            b->setEnabled (info.hasVideo);
        repaint();
    }

    video::Player* player() const
    {
        if (channel < 0)
            return nullptr;
        heldInstrument = engine.getChannelInstrument (channel);
        return video::playerOf (heldInstrument.get());
    }

    double songSeconds() const
    {
        // FL's clock (round 174d A3): the song's real time, tempo changes included
        const auto ticks = engine.getTransport().positionTicks.load();
        return engine.getSong().secondsForTicks ((std::int64_t) ticks);
    }

    void timerCallback() override
    {
        const auto& t = engine.getTransport();
        bool moving = false;
        if (settings.slave)
        {
            nowTime = video::Clock::videoTime (songSeconds(), settings, info.duration);
            moving = t.playing.load();
        }
        else if (auto* p = player())
        {
            nowTime = p->currentTime();
            moving = p->running();
        }
        const auto shownTime = juce::jmax (0.0, nowTime < 0.0 ? seconds (settings.start) : nowTime);
        if (info.hasVideo)
            decoder.request (shownTime, moving && nowTime >= 0.0 && (settings.loop || nowTime < endTime() - 1e-3));
        double ft = 0.0;
        auto img = decoder.frame (ft);
        auto* target = window != nullptr ? detachedSurface : &surface;
        if (target != nullptr && (img.getPixelData() != target->image.getPixelData() || frames != decoder.frameCount()))
        {
            frames = decoder.frameCount();
            target->image = img;
            target->keepAspect = settings.keepAspect;
            target->repaint();
        }
        if (window != nullptr && surface.image.isValid())
        {
            surface.image = {};
            surface.message = tr ("The video is in its own window");
            surface.repaint();
        }
        playButton.icon = moving ? IconButton::Icon::pause : IconButton::Icon::play;
        playButton.repaint();
        nowLabel.setText (video::formatTime (nowTime, settings.timeDisplay, info.fps), juce::dontSendNotification);
        if (std::abs (rail.now - nowTime) > 1e-4)
        {
            rail.now = nowTime;
            rail.repaint();
        }
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        if (d->pluginState != lastState)
        {
            lastState = d->pluginState;
            reload();
        }
    }

    void sizeDecoder (juce::Rectangle<int> r)
    {
        if (! info.hasVideo || r.isEmpty())
            return;
        const auto scale = juce::jmin (1.0, juce::jmin ((double) r.getWidth() / info.width, (double) r.getHeight() / info.height));
        const auto dpi = juce::jmax (1.0f, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                                               ? (float) juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale
                                               : 1.0f);
        const auto s = juce::jmin (1.0, scale * dpi);
        decoder.setSize (juce::roundToInt (info.width * s), juce::roundToInt (info.height * s));
    }

    // the song position (slaved) or the player's clock that shows video time `t`
    void seekVideo (double t)
    {
        t = juce::jlimit (0.0, juce::jmax (0.0, info.duration), t);
        if (settings.slave)
        {
            const auto song = t - seconds (settings.start) + seconds (settings.delay);
            const auto ticks = juce::jmax (0.0, song) * juce::jmax (1.0, engine.getSong().bpm) / 60.0 * (double) ppq;
            engine.seek (ticks);
        }
        else if (auto* p = player())
        {
            const auto wasRunning = p->running();
            p->ownPlay (t);
            if (! wasRunning)
                p->ownPause();
        }
        nowTime = t;
    }

    void playPause()
    {
        if (settings.slave)
        {
            if (engine.getTransport().playing.load())
                engine.stop();
            else
                engine.play();
            return;
        }
        if (auto* p = player())
        {
            if (p->running())
                p->ownPause();
            else
                p->ownPlay (nowTime >= endTime() - 1e-3 ? seconds (settings.start) : juce::jmax (seconds (settings.start), nowTime));
        }
    }

    void stopPlayback()
    {
        if (settings.slave)
            engine.stop();
        else if (auto* p = player())
            p->ownStop();
    }

    void setRange (bool isStart, double t)
    {
        edit ([this, isStart, t] (video::Settings& s) {
            if (isStart)
            {
                s.start = units (t);
                if (s.end > 0 && s.end < s.start)
                    s.end = s.start;
            }
            else
            {
                s.end = units (juce::jmin (t, info.duration));
                if (s.end < s.start)
                    s.start = s.end;
            }
        });
    }

    // FL's in-place box (InlineTypeIn) over the panel's left edge, above the time row
    void typeInPlace (const juce::Component& label, const juce::String& text, const juce::String& caption,
                      std::function<void (const juce::String&)> accept)
    {
        if (typeIn != nullptr)
            return; // (one at a time)
        typeIn = std::make_unique<InlineTypeIn> (text, caption);
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        typeIn->onDone = [self, accept = std::move (accept)] (bool ok, const juce::String& typed) {
            juce::MessageManager::callAsync ([self, ok, typed, accept] {
                if (self == nullptr)
                    return;
                self->typeIn.reset();
                if (ok && accept)
                    accept (typed);
            });
        };
        const auto row = getLocalArea (label.getParentComponent(), label.getBounds());
        const auto width = juce::jmin (getWidth() - 8, 200);
        const auto height = InlineTypeIn::boxHeight + InlineTypeIn::hintHeight;
        typeIn->setBounds (juce::jlimit (4, juce::jmax (4, getWidth() - width - 4), 10),
                           juce::jlimit (0, juce::jmax (0, getHeight() - height), row.getCentreY() - InlineTypeIn::boxHeight / 2), width, height);
        addAndMakeVisible (*typeIn);
        typeIn->toFront (false);
        typeIn->start();
    }

    void typeTime (bool isStart)
    {
        const auto frames = settings.timeDisplay != video::hms;
        const auto caption = isStart ? (frames ? tr ("Start time (H:M:S:Frame)") : tr ("Start time (H:M:S:MS)"))
                                     : (frames ? tr ("End time (H:M:S:Frame)") : tr ("End time (H:M:S:MS)"));
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        // (FL: the box starts with the label's text, in the time display)
        typeInPlace (isStart ? (const juce::Component&) startLabel : (const juce::Component&) endLabel,
                     video::formatTime (isStart ? seconds (settings.start) : endTime(), settings.timeDisplay, info.fps), caption,
                     [self, isStart] (const juce::String& typed) {
                         if (self != nullptr)
                             if (const auto t = parseTime (typed, self->settings.timeDisplay, self->info.fps))
                                 self->setRange (isStart, juce::jmax (0.0, *t));
                     });
    }

public:
    void openTypeIn (int which)
    {
        if (which == 0 || which == 1)
            typeTime (which == 0);
        else
            typeNow();
    }

private:

    void typeNow()
    {
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        typeInPlace (nowLabel, video::formatTime (juce::jmax (0.0, nowTime), settings.timeDisplay, info.fps),
                     settings.timeDisplay != video::hms ? tr ("Current time (H:M:S:Frame)") : tr ("Current time (H:M:S:MS)"),
                     [self] (const juce::String& typed) {
                         if (self != nullptr)
                             if (const auto t = parseTime (typed, self->settings.timeDisplay, self->info.fps))
                                 self->seekVideo (self->info.duration > 0.0 ? juce::jlimit (0.0, self->info.duration, *t) : juce::jmax (0.0, *t));
                     });
    }

    void typeDelay()
    {
        auto* w = new juce::AlertWindow (tr ("Delay playback (H:M:S:MS)"), tr ("The video starts when the song reaches this time."),
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("t", video::formatTime (seconds (settings.delay), video::hms, info.fps));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w] (int r) {
            if (r == 1 && self != nullptr)
                if (const auto t = parseTime (w->getTextEditorContents ("t")))
                    self->edit ([d = units (*t)] (video::Settings& s) { s.delay = d; });
        }), true);
    }

    void showOptions()
    {
        juce::PopupMenu m;
        m.addItem (tr ("Enable audio"), true, settings.enableAudio, [this] { edit ([] (video::Settings& s) { s.enableAudio = ! s.enableAudio; }); });
        m.addItem (tr ("Dock control panel"), true, settings.dock, [this] { edit ([] (video::Settings& s) { s.dock = ! s.dock; }); });
        // (FL 2026 hides "Ignore tempo changes" (Visible = False in its menu form, round 174d): the flag stays in the
        // state, and FL's clock does not read it)
        m.addItem (tr ("Keep aspect ratio"), true, settings.keepAspect, [this] { edit ([] (video::Settings& s) { s.keepAspect = ! s.keepAspect; }); });
        juce::PopupMenu display;
        // FL's menu order (the modes' numbers are FL's)
        for (const auto mode : { video::hms, video::smpte23976, video::smpte24, video::smpte25, video::smpte2997drop, video::smpte2997, video::smpte30,
                                 video::smpte48, video::smpte50, video::smpte5994drop, video::smpte5994, video::smpte72, video::smpte60, video::smpte120,
                                 video::smpteAuto })
        {
            // FL's menu form (round 174d): 72 before 60; Auto reads "SMPTE (Auto : <fps> fps)" and is enabled for a video
            // of 0 < fps <= 120 (OptionsMenuPopup 0x727970)
            const auto autoItem = mode == video::smpteAuto;
            const auto text = mode == video::hms ? tr ("Hours:Minutes:Seconds:Ms")
                              : autoItem          ? "SMPTE (Auto : " + juce::String (info.fps, 2) + " fps)"
                                                  : juce::String (video::timeDisplayName (mode));
            display.addItem (text, ! autoItem || (info.fps > 0.0 && info.fps <= 120.0), settings.timeDisplay == mode,
                             [this, mode] { edit ([mode] (video::Settings& s) { s.timeDisplay = mode; }); });
        }
        m.addSubMenu (tr ("Time display"), display);
        m.addSeparator();
        m.addItem (tr ("Send audio to playlist"), info.hasAudio, false, [this] { sendAudioToPlaylist(); });
        juce::PopupMenu delay;
        delay.addSectionHeader (tr ("Delay playback by {0}", { juce::String (video::formatTime (seconds (settings.delay), settings.timeDisplay, info.fps)) }));
        delay.addItem (tr ("Set..."), [this] { typeDelay(); });
        delay.addItem (tr ("Delay playback until the song position"), [this] {
            const auto ticks = engine.getTransport().positionTicks.load();
            edit ([d = units (ticks / (double) ppq * 60.0 / juce::jmax (1.0, engine.getSong().bpm))] (video::Settings& s) { s.delay = d; });
        });
        delay.addItem (tr ("No delay"), settings.delay != 0, false, [this] { edit ([] (video::Settings& s) { s.delay = 0; }); });
        m.addSubMenu (tr ("Delay playback"), delay);
        m.addSeparator();
        m.addItem (tr ("Locate ffmpeg..."), [this] { chooseFfmpeg(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton));
    }

    void chooseFile()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Load video file..."), resolveVideo (settings.path).getParentDirectory(),
                                                       juce::String ("*.") + juce::String (videoExtensions).replace (";", ";*."));
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().existsAsFile())
                self->load (fc.getResult());
        });
    }

    void load (const juce::File& file)
    {
        const auto pr = probe (file.getFullPathName());
        edit ([&] (video::Settings& s) {
            s.path = file.getFullPathName().toStdString();
            s.start = 0;
            s.end = 0;
            if (pr.info.hasVideo)
            {
                s.width = pr.info.width;
                s.height = pr.info.height;
            }
        });
        reload();
    }

    void chooseFfmpeg()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Locate ffmpeg"), juce::File(),
#if JUCE_WINDOWS
                                                       "ffmpeg.exe");
#else
                                                       "*");
#endif
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self == nullptr || ! fc.getResult().existsAsFile())
                return;
            setFfmpegLocation (fc.getResult());
            media::clearCaches();
            self->loadedPath = {};
            self->reload();
            self->engine.editSong ([] (Song&) {}, false); // (the instrument asks for its sound track again)
        });
    }

    // FL's "Send audio to playlist": the sound track as a WAV in an audio clip where the video starts
    void sendAudioToPlaylist()
    {
        const auto file = resolveVideo (settings.path);
        if (! file.existsAsFile() || ! info.hasAudio)
            return;
        auto folder = platform::cacheFolder ("video-audio");
        folder.createDirectory();
        const auto key = juce::String::toHexString (file.getFullPathName().hashCode64()) + "-" + juce::String::toHexString (file.getLastModificationTime().toMilliseconds());
        const auto wav = folder.getChildFile (file.getFileNameWithoutExtension() + " (" + tr ("audio") + ") " + key.substring (0, 8) + ".wav");
        const auto delay = seconds (settings.delay);
        juce::Component::SafePointer<VideoPlayerEditor> self (this);
        statusLine = tr ("Extracting the sound...");
        ui::setHint (surface, statusLine);
        panelBack.repaint();
        std::thread ([self, file, wav, delay] {
            const bool ok = wav.existsAsFile() || media::extractAudioToWav (file, wav);
            juce::MessageManager::callAsync ([self, wav, ok, delay] {
                if (self == nullptr)
                    return;
                if (ok)
                    self->addClip (wav, delay);
                self->refreshControls();
            });
        }).detach();
    }

    void addClip (const juce::File& wav, double delaySeconds)
    {
        const auto data = engine.getSampleLibrary().load (wav.getFullPathName());
        if (data == nullptr)
            return;
        const auto bpm = juce::jmax (1.0, engine.getSong().bpm);
        const auto length = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) ((double) data->frames() / data->sampleRate * bpm / 60.0 * ppq));
        const auto start = (std::int64_t) std::llround (delaySeconds * bpm / 60.0 * ppq);
        const auto path = wav.getFullPathName().toStdString();
        const auto name = wav.getFileNameWithoutExtension().toStdString();
        engine.editSong ([=] (Song& s) {
            int track = 0;
            for (;; ++track)
                if (std::none_of (s.playlist.begin(), s.playlist.end(), [&] (const Clip& c) { return c.track == track && c.start < start + length && c.end() > start; }))
                    break;
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
            clip.track = track;
            clip.start = start;
            clip.length = length;
            s.playlist.push_back (clip);
            s.mode = PlayMode::song;
        });
    }

    // the video in its own resizable window (double-click: full screen)
    class DetachedWindow final : public juce::DocumentWindow
    {
    public:
        DetachedWindow (VideoPlayerEditor& o) : juce::DocumentWindow ("Video", theme::colour::bg1, juce::DocumentWindow::allButtons), owner (o)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            auto* s = new VideoSurface();
            s->onSize = [this] (juce::Rectangle<int> r) { owner.sizeDecoder (r); };
            s->onDoubleClick = [this] { setFullScreen (! isFullScreen()); };
            surface = s;
            setContentOwned (s, false);
            const auto w = owner.info.width > 0 ? owner.info.width : 640, h = owner.info.height > 0 ? owner.info.height : 360;
            const auto scale = juce::jmin (1.0, 960.0 / w, 540.0 / h);
            centreWithSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
            setVisible (true);
        }
        void closeButtonPressed() override
        {
            juce::MessageManager::callAsync ([o = juce::Component::SafePointer<VideoPlayerEditor> (&owner)] {
                if (o != nullptr)
                    o->attach();
            });
        }
        VideoSurface* surface = nullptr;

    private:
        VideoPlayerEditor& owner;
    };

    void detach()
    {
        if (window != nullptr)
        {
            window->toFront (true);
            return;
        }
        if (! info.hasVideo)
            return;
        window = std::make_unique<DetachedWindow> (*this);
        window->setName (juce::File (juce::String::fromUTF8 (settings.path.c_str())).getFileName());
        detachedSurface = window->surface;
        detachButton.setToggleState (true, juce::dontSendNotification);
        frames = -1;
    }

    void attach()
    {
        window = nullptr;
        detachedSurface = nullptr;
        detachButton.setToggleState (false, juce::dontSendNotification);
        surface.message = {};
        sizeDecoder (surface.getLocalBounds());
        refreshControls();
        frames = -1;
    }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel = -1;
    video::Settings settings;
    video::Info info;
    juce::String loadedPath, statusLine;
    std::vector<std::uint8_t> lastState;
    mutable std::shared_ptr<Instrument> heldInstrument;
    double nowTime = 0.0;
    int frames = -1;

    FrameDecoder decoder;
    VideoSurface surface;
    VideoSurface* detachedSurface = nullptr;
    std::unique_ptr<DetachedWindow> window;
    juce::Rectangle<int> panelArea;
    PanelBack panelBack;
    using Shape = IconButton::Shape;
    IconButton loadButton { IconButton::Icon::load, Shape::circle }, optionsButton { IconButton::Icon::options },
        slaveButton { IconButton::Icon::link }, loopButton { IconButton::Icon::loop, Shape::pillLeft },
        playButton { IconButton::Icon::play, Shape::pillMid }, stopButton { IconButton::Icon::stop, Shape::pillRight },
        soundButton { IconButton::Icon::sound }, detachButton { IconButton::Icon::detach },
        setStartButton { IconButton::Icon::start, Shape::small }, setEndButton { IconButton::Icon::end, Shape::small };
    juce::TextButton locateButton;
    std::unique_ptr<ctlui::Knob> volume;
    ClickLabel startLabel, endLabel, nowLabel;
    TimeRail rail;
    std::unique_ptr<InlineTypeIn> typeIn; // FL's in-place type-in, while open
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

namespace videoview
{
void install (AudioEngine& engine)
{
    installedEngine = &engine;
    video::setAudioProvider ([] (const std::string& path, bool wait) { return provideTrack (path, wait); });
}

juce::String report (const juce::String& path)
{
    return probe (path).report;
}

void openTypeIn (juce::Component& editor, int which)
{
    if (auto* e = dynamic_cast<VideoPlayerEditor*> (&editor))
        e->openTypeIn (which);
}
} // namespace videoview

std::unique_ptr<juce::Component> makeVideoPlayerEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<VideoPlayerEditor> (engine, std::move (locate), channel);
}

void showVideoStill (juce::Component& root, const juce::Image& still)
{
    if (auto* editor = dynamic_cast<VideoPlayerEditor*> (&root))
    {
        editor->showStill (still);
        return;
    }
    for (auto* child : root.getChildren())
        showVideoStill (*child, still);
}

std::unique_ptr<juce::Component> makeVideoPlayerEditor (AudioEngine& engine, int channel)
{
    return makeVideoPlayerEditor (engine, [channel] (Song& s) -> ChannelDef* {
        return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
    }, channel);
}
} // namespace lynx
