#include "ui/SimpleTuner.h"

#include <cmath>
#include <vector>

#include "core/DetailChain.h"
#include "core/FlExtraFx.h"
#include "core/SimpleTracks.h"
#include "core/SimpleTuner.h"
#include "core/VisualFx.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx::simpletunerui
{
using namespace theme;

namespace
{
namespace st = simpletuner;
namespace ft = flextra::tuner;

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::File referenceFile() { return PluginHost::settingsFolder().getChildFile ("simple-tuner.txt"); }

double& referenceValue()
{
    static double hz = [] {
        const auto text = referenceFile().loadFileAsString().trim();
        return text.isEmpty() ? st::defaultReference : st::clampReference (text.getDoubleValue());
    }();
    return hz;
}

// GarageBand's Tuner button glyph, drawn as LYNX's own vector: a tuning fork (two tines on a stem), 24-unit grid
void drawFork (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour ink, float stroke)
{
    const auto s = juce::jmin (box.getWidth(), box.getHeight()) / 24.0f;
    const auto o = box.getCentre() - juce::Point<float> (12.0f * s, 12.0f * s);
    auto p = [&] (float x, float y) { return o + juce::Point<float> (x * s, y * s); };
    juce::Path fork;
    fork.startNewSubPath (p (8.0f, 3.0f));
    fork.lineTo (p (8.0f, 11.0f));
    fork.quadraticTo (p (8.0f, 15.0f), p (12.0f, 15.0f));
    fork.quadraticTo (p (16.0f, 15.0f), p (16.0f, 11.0f));
    fork.lineTo (p (16.0f, 3.0f));
    fork.startNewSubPath (p (12.0f, 15.0f));
    fork.lineTo (p (12.0f, 21.0f));
    g.setColour (ink);
    g.strokePath (fork, juce::PathStrokeType (stroke * s * 1.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse (juce::Rectangle<float> (3.2f * s, 3.2f * s).withCentre (p (12.0f, 21.0f)));
}

class TunerButton final : public juce::Button
{
public:
    TunerButton() : juce::Button ("Tuner")
    {
        setComponentID ("simple.tuner");
        setTooltip (tr ("Tuner: tune a guitar, a bass or a voice on the selected track's input"));
        setTitle (tr ("Tuner"));
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }
    bool open = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat();
        if (over || down || open)
        {
            // the capsule is one segment: the hover fill is the capsule's own round shape
            g.setColour (down ? colour::bg6 : colour::bg5);
            g.fillRoundedRectangle (r.reduced (1.0f), r.getHeight() * 0.5f - 1.0f);
        }
        const auto box = r.withSizeKeepingCentre (22.0f, 22.0f);
        drawFork (g, box, open ? colour::accent : over ? colour::textPrimary : colour::textSecondary, 1.5f);
    }
};

class Panel final : public juce::Component,
                    private juce::Timer
{
public:
    Panel (AudioEngine& e, std::function<int()> t, bool forSnapshot) : engine (e), track (std::move (t)), snapshot (forSnapshot)
    {
        setSize (panelWidth, panelHeight);
        setComponentID ("simple.tuner.panel");
        setWantsKeyboardFocus (false);
        refreshSource();
    }

    ~Panel() override
    {
        stopTimer();
        stopListening();
    }

    double fixedReference = 0.0; // (shots: shown instead of the user's setting)
    double shownReference() const { return fixedReference > 0.0 ? fixedReference : referenceValue(); }

    void showReading (const ft::Reading& r, const juce::String& sourceText)
    {
        reading = r;
        source = sourceText;
        repaint();
    }

    CheckReading feed (double sineHz, double seconds, double rate)
    {
        analyzer.prepare (rate);
        applyOptions();
        const auto total = (int) (seconds * rate);
        std::vector<float> block (512);
        for (int at = 0; at < total; at += (int) block.size())
        {
            const auto n = juce::jmin ((int) block.size(), total - at);
            for (int i = 0; i < n; ++i)
                block[(std::size_t) i] = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * sineHz * (double) (at + i) / rate);
            analyzer.push (block.data(), n);
        }
        reading = analyzer.reading();
        return { reading.valid, reading.note, reading.cents, reading.hz };
    }

    // --- painting ------------------------------------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (r, radius::lg);
        // title row: "Tuner" and where it listens
        auto top = getLocalBounds().reduced (14, 10).removeFromTop (20);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        g.drawText (tr ("Tuner"), top, juce::Justification::centredLeft, false);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawFittedText (source, top.withTrimmedLeft (60), juce::Justification::centredRight, 1, 0.8f);

        paintDial (g);
    }

    juce::Rectangle<float> dialArea() const { return getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (26.0f); }

    juce::Point<float> pivot() const
    {
        const auto a = dialArea();
        return { a.getCentreX(), a.getY() + a.getHeight() * 0.80f };
    }
    float radiusOfArc() const { return juce::jmin (dialArea().getWidth() * 0.42f, dialArea().getHeight() * 0.66f); }

    void paintDial (juce::Graphics& g)
    {
        const auto c = pivot();
        const auto rad = radiusOfArc();
        const auto has = reading.valid;
        const auto good = has && st::inTune (reading.cents);
        const auto state = ! has ? colour::textDisabled : good ? colour::ledOn : colour::warning;

        // the scale: a 6 px track from −50 to +50 cents, ticks every 10 (longer at 0), numbers every 10
        juce::Path track;
        track.addCentredArc (c.x, c.y, rad, rad, 0.0f, (float) -st::maxAngle, (float) st::maxAngle, true);
        g.setColour (colour::bg4);
        g.strokePath (track, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        g.setFont (font (type::caption));
        for (int cents = -50; cents <= 50; cents += 10)
        {
            const auto angle = (float) st::needleAngle (cents);
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            const auto inner = rad - (cents == 0 ? 12.0f : 7.0f), outer = rad + 4.0f;
            g.setColour (cents == 0 ? colour::textPrimary : colour::borderStrong);
            g.drawLine ({ c + dir * inner, c + dir * outer }, cents == 0 ? 2.0f : 1.0f);
            const auto label = c + dir * (rad + 16.0f);
            g.setColour (colour::textSecondary);
            g.drawText (juce::String (cents), juce::Rectangle<float> (30.0f, 14.0f).withCentre (label), juce::Justification::centred, false);
        }
        if (has)
        {
            // the part of the scale between the centre and the needle (GarageBand's lit segments) and the needle
            const auto angle = (float) st::needleAngle (reading.cents);
            juce::Path lit;
            lit.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (0.0f, angle), juce::jmax (0.0f, angle) + (std::abs (angle) < 0.02f ? 0.02f : 0.0f), true);
            g.setColour (state);
            g.strokePath (lit, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
            juce::Path wedge; // a faint wedge behind the needle
            wedge.addPieSegment (juce::Rectangle<float> (2.0f * (rad - 4.0f), 2.0f * (rad - 4.0f)).withCentre (c), juce::jmin (0.0f, angle),
                                 juce::jmax (0.0f, angle) + 0.001f, 0.45f);
            g.setColour (state.withAlpha (0.12f));
            g.fillPath (wedge);
            g.setColour (state);
            g.drawLine ({ c + dir * (rad * 0.45f), c + dir * (rad + 2.0f) }, 2.0f);
        }
        // the Keynote: the big letter and its sharp
        const auto [letter, sharp] = has ? st::keynote (reading.note) : std::pair<std::string, bool> { "-", false };
        const auto noteBox = juce::Rectangle<float> (90.0f, 60.0f).withCentre (c.translated (0.0f, -6.0f));
        g.setColour (state);
        g.setFont (font (52.0f, true));
        g.drawText (utf8 (letter), noteBox, juce::Justification::centred, false);
        if (sharp)
        {
            g.setFont (font (22.0f, true));
            g.drawText ("#", noteBox.withTrimmedLeft (noteBox.getWidth() * 0.5f + 14.0f).withHeight (28.0f), juce::Justification::centredLeft, false);
        }
        if (has)
        {
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small));
            g.drawText (juce::String (ft::octaveOf (reading.note)), noteBox.withTrimmedLeft (noteBox.getWidth() * 0.5f + 14.0f).withTrimmedTop (34.0f),
                        juce::Justification::centredLeft, false);
        }
        // Reference Tuning (left) and Tune Deviation (right), on the note's line
        const auto line = juce::Rectangle<float> (dialArea().getX(), noteBox.getCentreY() - 13.0f, dialArea().getWidth(), 26.0f);
        g.setFont (font (type::title, true));
        g.setColour (draggingReference || isMouseOverReference ? colour::textPrimary : colour::textSecondary);
        g.drawText (utf8 (st::referenceText (shownReference())), referenceHit().toFloat(), juce::Justification::centredLeft, false);
        g.setColour (has ? colour::textSecondary : colour::textDisabled);
        g.drawText (has ? utf8 (st::centsText (reading.cents)) : juce::String ("--"), line.withTrimmedLeft (line.getWidth() * 0.70f),
                    juce::Justification::centredRight, false);
        // the hint under the dial
        g.setFont (font (type::caption));
        g.setColour (colour::textSecondary);
        const auto hint = ! has ? tr ("Play one string or sing one note")
                          : good ? tr ("In tune")
                                 : reading.cents < 0.0 ? tr ("Flat: tune up") : tr ("Sharp: tune down");
        g.drawText (hint, getLocalBounds().toFloat().removeFromBottom (24.0f), juce::Justification::centred, false);
    }

    // --- the reference: drag up / down (0.1 Hz per 2 px; Shift: finer), double-click 440.0 Hz ---------------------------
    juce::Rectangle<int> referenceHit() const
    {
        const auto line = juce::Rectangle<float> (dialArea().getX(), pivot().y - 19.0f, dialArea().getWidth() * 0.32f, 26.0f);
        return line.toNearestInt();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto over = referenceHit().contains (e.getPosition());
        if (over != isMouseOverReference)
        {
            isMouseOverReference = over;
            setMouseCursor (over ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
            repaint (referenceHit().expanded (4));
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        isMouseOverReference = false;
        repaint (referenceHit().expanded (4));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        draggingReference = referenceHit().contains (e.getPosition());
        startReference = referenceValue();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! draggingReference || fixedReference > 0.0)
            return;
        const auto perPixel = e.mods.isShiftDown() ? 0.01 : 0.05;
        setReference (startReference - perPixel * e.getDistanceFromDragStartY());
        applyOptions();
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        draggingReference = false;
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (referenceHit().contains (e.getPosition()) && fixedReference <= 0.0)
        {
            setReference (st::defaultReference);
            applyOptions();
            repaint();
        }
    }

    // --- listening only while shown -------------------------------------------------------------------------------------
    void visibilityChanged() override { followShowing(); }
    void parentHierarchyChanged() override { followShowing(); }

private:
    void followShowing()
    {
        if (snapshot)
            return;
        if (isShowing())
        {
            if (! listening)
                startListening();
        }
        else if (listening)
            stopListening();
    }

    void refreshSource()
    {
        const auto& song = engine.getSong();
        const auto t = track ? track() : -1;
        insert = t >= 0 ? detailchain::forTrack (song, t).insert : -1;
        const auto kind = t >= 0 ? simpletracks::kindOf (song, t) : std::nullopt;
        // a Bass track (the chooser's Bass tile names it "Bass", "Bass 2"...): FL's Bass range, down to F0
        const auto trackName = t >= 0 && t < (int) song.playlistTracks.size() ? utf8 (song.playlistTracks[(std::size_t) t].name) : juce::String();
        bass = trackName.containsIgnoreCase ("bass") || trackName.containsIgnoreCase (tr ("Bass"));
        const auto names = engine.getInputChannelNames();
        const auto [first, count] = st::inputFor (song, insert, (int) names.size());
        inputFirst = first;
        inputCount = count;
        if (count == 0)
            source = tr ("No audio input");
        else
        {
            auto name = utf8 (names[(std::size_t) first]);
            if (count > 1 && first + 1 < (int) names.size())
                name << " + " << utf8 (names[(std::size_t) first + 1]);
            if (t >= 0 && insert > 0 && insert < (int) song.mixer.size() && song.mixer[(std::size_t) insert].inputChannel >= 0 && kind)
                source = (trackName.isNotEmpty() ? trackName : tr (simpletracks::defaultName (*kind))) + juce::String::fromUTF8 ("  \xc2\xb7  ") + name;
            else
                source = name;
        }
    }

    void applyOptions() { analyzer.setOptions (referenceValue(), 0.5f, bass); }

    void startListening()
    {
        listening = true;
        refreshSource();
        if (inputCount > 0)
        {
            if (! engine.hasAudioInput())
            {
                engine.openAudioInput();
                openedInput = true;
                refreshSource();
            }
            engine.setTunerInput (inputFirst, inputCount);
        }
        const auto& tap = engine.tunerTap();
        preparedRate = 0.0;
        lastRead = tap.written();
        startTimerHz (30);
    }

    void stopListening()
    {
        if (! listening)
            return;
        listening = false;
        stopTimer();
        engine.setTunerInput (-1, 0);
        if (openedInput)
        {
            openedInput = false;
            engine.releaseAudioInputIfUnused();
        }
    }

    void timerCallback() override
    {
        const auto& tap = engine.tunerTap();
        const auto rate = tap.sampleRate.load();
        if (rate != preparedRate)
        {
            analyzer.prepare (rate);
            preparedRate = rate;
            lastRead = tap.written();
        }
        applyOptions();
        const auto written = tap.written();
        const auto fresh = (int) juce::jmin<std::uint64_t> (written - lastRead, (std::uint64_t) visualfx::tapSize / 2);
        lastRead = written;
        if (fresh > 0)
        {
            left.resize ((std::size_t) fresh);
            right.resize ((std::size_t) fresh);
            tap.read (left.data(), right.data(), fresh);
            analyzer.push (left.data(), fresh);
        }
        const auto next = analyzer.reading();
        if (next.valid != reading.valid || next.note != reading.note || std::abs (next.cents - reading.cents) > 0.3)
        {
            reading = next;
            repaint();
        }
    }

    AudioEngine& engine;
    std::function<int()> track;
    bool snapshot = false, listening = false, openedInput = false, bass = false;
    int insert = -1, inputFirst = 0, inputCount = 0;
    juce::String source;
    ft::Analyzer analyzer;
    ft::Reading reading;
    double preparedRate = 0.0, startReference = st::defaultReference;
    std::uint64_t lastRead = 0;
    std::vector<float> left, right;
    bool draggingReference = false, isMouseOverReference = false;
};
} // namespace

std::unique_ptr<juce::Button> makeButton() { return std::make_unique<TunerButton>(); }

void setOpen (juce::Button& button, bool open)
{
    if (auto* b = dynamic_cast<TunerButton*> (&button); b != nullptr && b->open != open)
    {
        b->open = open;
        b->repaint();
    }
}

std::unique_ptr<juce::Component> makePanel (AudioEngine& engine, std::function<int()> track)
{
    return std::make_unique<Panel> (engine, std::move (track), false);
}

std::unique_ptr<juce::Component> makePanelForSnapshot (AudioEngine& engine, int midiNote, double cents, double hz, const juce::String& source)
{
    auto p = std::make_unique<Panel> (engine, [] { return -1; }, true);
    p->fixedReference = st::clampReference (hz);
    ft::Reading r;
    r.valid = midiNote >= 0;
    r.note = juce::jmax (0, midiNote);
    r.cents = cents;
    r.hz = hz * std::pow (2.0, ((double) midiNote + cents / 100.0 - 69.0) / 12.0);
    p->showReading (r, source);
    return p;
}

CheckReading feedForCheck (juce::Component& panel, double sineHz, double seconds, double sampleRate)
{
    if (auto* p = dynamic_cast<Panel*> (&panel))
        return p->feed (sineHz, seconds, sampleRate);
    return {};
}

double reference() { return referenceValue(); }

void setReference (double hz)
{
    const auto v = st::clampReference (hz);
    if (std::abs (v - referenceValue()) < 1.0e-9)
        return;
    referenceValue() = v;
    referenceFile().getParentDirectory().createDirectory();
    referenceFile().replaceWithText (juce::String (v, 1));
}
} // namespace lynx::simpletunerui
