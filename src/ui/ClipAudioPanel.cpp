#include "ui/ClipAudioPanel.h"

#include <cmath>
#include <limits>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/ClipEnvelope.h"
#include "core/ClipLauncher.h"
#include "core/ClipOps.h"
#include "core/ClipSample.h"
#include "core/WarpModes.h"
#include "ui/AudioEditor.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/NumBox.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// The warp modes in Live's order with Live's names, each on the LYNX engine that does that job (round 190dr-2):
// Beats, Tones, Texture and Complex are LYNX's own engines of those names, Re-Pitch is LYNX's resampling (speed and pitch
// together). Live's Complex Pro has no engine of its own in LYNX and is left out. The FL modes a channel may carry
// (General, Drums / transients, Vocal / mono) follow below a line, so its mode always shows.
struct ModeItem
{
    const char* name;
    int method; // StretchMethod, -1 = resample
};
const ModeItem liveModes[] = { { TR_NOOP ("Beats"), (int) StretchMethod::beats }, { TR_NOOP ("Tones"), (int) StretchMethod::tones },
                               { TR_NOOP ("Texture"), (int) StretchMethod::texture }, { TR_NOOP ("Re-Pitch"), -1 },
                               { TR_NOOP ("Complex"), (int) StretchMethod::complex } };
const ModeItem flModes[] = { { TR_NOOP ("General"), (int) StretchMethod::general }, { TR_NOOP ("Drums / transients"), (int) StretchMethod::transients },
                             { TR_NOOP ("Vocal / mono"), (int) StretchMethod::mono } };

// The Clip View's Envelopes tab of an audio clip (round 190gi; Live 12's audio Clip View has the tabs Sample | Envelopes
// [SHOT ableton175a/live/m3-live-clipview2.png] and the envelope choosers Device / Control [MANUAL clip envelopes]). LYNX
// keeps two of Live's audio clip envelopes: Clip › Gain and Mixer › Track Panning (core/ClipEnvelope.h; the .als maps
// them both ways). Live's editing [MANUAL]: a click on the line adds a breakpoint, a click on a breakpoint deletes it, a
// drag moves it, Alt + drag on a segment bends it. Clip time across the clip's content (its start to its end).
class ClipEnvelopeView final : public juce::Component, private juce::ChangeListener
{
public:
    ClipEnvelopeView (AudioEngine& e, int trackIn, int sceneIn) : engine (e), track (trackIn), scene (sceneIn)
    {
        device.addItem (tr ("Clip"), 1);
        device.addItem (tr ("Mixer"), 2);
        device.setTooltip (tr ("Envelope device: the clip itself, or the track's mixer"));
        device.onChange = [this] { choose (device.getSelectedId() == 2 ? clipenv::panLane : clipenv::volumeLane); };
        control.setTooltip (tr ("Envelope control"));
        addAndMakeVisible (device);
        addAndMakeVisible (control);
        engine.addChangeListener (this);
        refresh();
    }
    ~ClipEnvelopeView() override { engine.removeChangeListener (this); }

    void resized() override
    {
        auto bar = getLocalBounds().removeFromTop (barHeight).reduced (4, 3);
        device.setBounds (bar.removeFromLeft (110));
        bar.removeFromLeft (4);
        control.setBounds (bar.removeFromLeft (130));
    }
    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg0);
        g.setColour (colour::bg1);
        g.fillRect (getLocalBounds().removeFromTop (barHeight));
        const auto* c = clip();
        if (c == nullptr)
            return;
        const auto area = body();
        // the beat grid of the clip's content (bars brighter), as Live's sample editor counts them
        const auto& song = engine.getSong();
        const auto perBar = std::max<std::int64_t> (1, song.barTicks()); // (bars of the song's meter, round 190fg; the grid stays on quarters)
        for (auto t = ((std::int64_t) c->offset / ppq) * ppq; t <= c->offset + c->length; t += ppq)
        {
            if (t < c->offset)
                continue;
            const auto x = (float) tickToX (*c, (double) t);
            const auto bar = t % perBar == 0;
            g.setColour (colour::border.withAlpha (bar ? 0.9f : 0.4f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            if (bar)
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (type::caption));
                g.drawText (juce::String (t / perBar + 1), juce::Rectangle<float> (x + 3.0f, area.getY(), 40.0f, 12.0f), juce::Justification::topLeft, false);
            }
        }
        // the clip's audio under the envelope, faint (Live draws the sample there): min / max per 2 px of its content
        if (c->channel >= 0 && c->channel < (int) song.channels.size())
            if (const auto data = engine.getSampleLibrary().findLoaded (juce::String::fromUTF8 (song.channels[(std::size_t) c->channel].samplePath.c_str()));
                data != nullptr && ! data->left.empty())
            {
                const auto& ch = song.channels[(std::size_t) c->channel];
                const auto perTick = clipops::framesPerTick (ch, (double) data->left.size(), data->sampleRate, song.bpm) / juce::jmax (0.01, c->stretch);
                g.setColour (colour::textSecondary.withAlpha (0.22f));
                for (float x = area.getX(); x < area.getRight(); x += 2.0f)
                {
                    const auto f0 = (std::int64_t) (xToTick (*c, x) * perTick), f1 = (std::int64_t) (xToTick (*c, x + 2.0f) * perTick);
                    float lo = 0.0f, hi = 0.0f;
                    for (auto f = juce::jmax<std::int64_t> (0, f0); f < f1 && f < (std::int64_t) data->left.size(); f += juce::jmax<std::int64_t> (1, (f1 - f0) / 64))
                    {
                        lo = juce::jmin (lo, data->left[(std::size_t) f]);
                        hi = juce::jmax (hi, data->left[(std::size_t) f]);
                    }
                    if (hi > lo)
                        g.fillRect (x, area.getCentreY() - hi * area.getHeight() * 0.5f, 2.0f, juce::jmax (1.0f, (hi - lo) * area.getHeight() * 0.5f));
                }
            }
        if (lane == clipenv::panLane)
        {
            g.setColour (colour::border);
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        }
        const auto* e = clipenv::of (song, *c);
        const auto& points = e != nullptr ? clipenv::laneOf (*e, lane) : none;
        const auto valueAt = [&] (double tick) { return points.empty() ? clipenv::neutralOf (lane) : automationCurveAt (points, tick); };
        const auto yAt = [&] (int x) { return valueToY (valueAt (xToTick (*c, (float) x))); };
        juce::Path curve;
        auto lastY = 0.0f;
        for (int x = (int) area.getX(); x <= (int) area.getRight(); x += 2)
        {
            const auto y = yAt (x);
            if (x == (int) area.getX())
                curve.startNewSubPath ((float) x, y);
            else
                automationLineTo (curve, (float) x - 2.0f, lastY, (float) x, y, [&] { return yAt (x - 1); });
            lastY = y;
        }
        g.setColour (colour::accent);
        g.strokePath (curve, juce::PathStrokeType (1.5f));
        for (std::size_t k = 0; k < points.size(); ++k)
        {
            const auto x = (float) tickToX (*c, (double) points[k].tick), y = valueToY (points[k].value);
            g.setColour (colour::bg0);
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.setColour ((int) k == dragPoint ? colour::accent.brighter (0.4f) : colour::accent);
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
        }
        // the value under the mouse or of the point being dragged: dB for the gain, L / R for the panning
        if (! readout.isEmpty())
        {
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            g.drawText (readout, getLocalBounds().removeFromTop (barHeight).reduced (8, 0), juce::Justification::centredRight, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* c = clip();
        if (c == nullptr || ! body().contains (e.position))
            return;
        moved = false;
        dragPoint = pointAt (e.position);
        if (dragPoint < 0 && e.mods.isAltDown()) // Alt + drag on a segment: bend it
        {
            bendSegment = segmentAt (*c, e.position);
            if (bendSegment >= 0)
            {
                bendStart = points()[(std::size_t) bendSegment].tension;
                return;
            }
        }
        if (dragPoint < 0 && std::abs (valueToY (valueNow (*c, xToTick (*c, e.position.x))) - e.position.y) <= 6.0f)
        {
            // a click on the line: a breakpoint there, dragged while the button is held
            AutomationPoint p;
            p.tick = (std::int64_t) std::llround (xToTick (*c, e.position.x));
            p.value = yToValue (e.position.y);
            int added = -1;
            edit ([&] (std::vector<AutomationPoint>& list) { added = insertAutomationPoint (list, p); });
            dragPoint = added;
            moved = true; // (a new point is not deleted on release)
        }
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* c = clip();
        if (c == nullptr)
            return;
        if (bendSegment >= 0)
        {
            const auto up = -(float) e.getDistanceFromDragStartY() / juce::jmax (30.0f, body().getHeight());
            const auto rising = bendSegment + 1 < (int) points().size() && points()[(std::size_t) bendSegment + 1].value >= points()[(std::size_t) bendSegment].value;
            const auto tension = juce::jlimit (-1.0f, 1.0f, bendStart + (rising ? -up : up));
            edit ([seg = bendSegment, tension] (std::vector<AutomationPoint>& list) { setAutomationSegmentTension (list, seg, tension); });
            return;
        }
        if (dragPoint < 0 || dragPoint >= (int) points().size())
            return;
        if (e.getDistanceFromDragStart() > 2)
            moved = true;
        const auto& list = points();
        const auto lo = dragPoint > 0 ? list[(std::size_t) dragPoint - 1].tick : std::numeric_limits<std::int64_t>::min() / 2;
        const auto hi = dragPoint + 1 < (int) list.size() ? list[(std::size_t) dragPoint + 1].tick : std::numeric_limits<std::int64_t>::max() / 2;
        const auto tick = juce::jlimit (lo, hi, (std::int64_t) std::llround (xToTick (*c, e.position.x)));
        const auto value = yToValue (e.position.y);
        edit ([i = dragPoint, tick, value] (std::vector<AutomationPoint>& l) {
            if (i < (int) l.size())
            {
                l[(std::size_t) i].tick = tick;
                l[(std::size_t) i].value = value;
            }
        });
        readout = valueText (value);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        // a click on a breakpoint (no drag): it goes (Live), the last one stays
        if (! moved && dragPoint >= 0 && bendSegment < 0 && points().size() > 1)
            edit ([i = dragPoint] (std::vector<AutomationPoint>& l) { eraseAutomationPoint (l, i); });
        dragPoint = bendSegment = -1;
        repaint();
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto* c = clip();
        readout = c != nullptr && body().contains (e.position) ? valueText (valueNow (*c, xToTick (*c, e.position.x))) : juce::String();
        repaint();
    }

    std::uint8_t laneShown() const noexcept { return lane; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void refresh()
    {
        const auto* c = clip();
        const auto* e = c != nullptr ? clipenv::of (engine.getSong(), *c) : nullptr;
        if (e != nullptr && e->shown != 0)
            lane = e->shown;
        device.setSelectedId (lane == clipenv::panLane ? 2 : 1, juce::dontSendNotification);
        control.clear (juce::dontSendNotification);
        control.addItem (lane == clipenv::panLane ? tr ("Track Panning") : tr ("Gain"), 1);
        control.setSelectedId (1, juce::dontSendNotification);
        repaint();
    }
    void choose (std::uint8_t which)
    {
        lane = which;
        engine.editSong ([this, which] (Song& s) {
            if (s.clipGrid)
                if (auto* c = const_cast<Clip*> (launcher::clipAt (*s.clipGrid, track, scene)); c != nullptr && c->kind == ClipKind::audio)
                    clipenv::show (s, *c, which);
        });
        refresh();
    }
    const Clip* clip() const
    {
        const auto& song = engine.getSong();
        const auto* c = song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
        return c != nullptr && c->kind == ClipKind::audio ? c : nullptr;
    }
    const std::vector<AutomationPoint>& points() const
    {
        const auto* c = clip();
        const auto* e = c != nullptr ? clipenv::of (engine.getSong(), *c) : nullptr;
        return e != nullptr ? clipenv::laneOf (*e, lane) : none;
    }
    // one undoable edit of the lane shown (the clip gets its own envelope, the lane starts flat over the clip)
    void edit (const std::function<void (std::vector<AutomationPoint>&)>& fn)
    {
        engine.editSong ([this, &fn] (Song& s) {
            if (! s.clipGrid)
                return;
            auto* c = const_cast<Clip*> (launcher::clipAt (*s.clipGrid, track, scene));
            if (c == nullptr || c->kind != ClipKind::audio)
                return;
            auto& own = clipenv::own (s, *c);
            own.shown = lane;
            auto& list = clipenv::laneOf (own, lane);
            if (list.empty())
                list = clipenv::defaultLane (*c, lane);
            fn (list);
        });
        repaint();
    }
    juce::Rectangle<float> body() const { return getLocalBounds().withTrimmedTop (barHeight).toFloat().reduced (8.0f, 10.0f); }
    double tickToX (const Clip& c, double tick) const
    {
        const auto a = body();
        return a.getX() + (tick - (double) c.offset) / (double) juce::jmax<std::int64_t> (1, c.length) * a.getWidth();
    }
    double xToTick (const Clip& c, float x) const
    {
        const auto a = body();
        return (double) c.offset + (double) (x - a.getX()) / juce::jmax (1.0f, a.getWidth()) * (double) juce::jmax<std::int64_t> (1, c.length);
    }
    float valueToY (float v) const { const auto a = body(); return a.getBottom() - juce::jlimit (0.0f, 1.0f, v) * a.getHeight(); }
    float yToValue (float y) const { const auto a = body(); return juce::jlimit (0.0f, 1.0f, (a.getBottom() - y) / juce::jmax (1.0f, a.getHeight())); }
    float valueNow (const Clip&, double tick) const
    {
        const auto& list = points();
        return list.empty() ? clipenv::neutralOf (lane) : automationCurveAt (list, tick);
    }
    int pointAt (juce::Point<float> p) const
    {
        const auto* c = clip();
        if (c == nullptr)
            return -1;
        const auto& list = points();
        for (int k = (int) list.size() - 1; k >= 0; --k)
            if (juce::Point<float> ((float) tickToX (*c, (double) list[(std::size_t) k].tick), valueToY (list[(std::size_t) k].value)).getDistanceFrom (p) <= 6.0f)
                return k;
        return -1;
    }
    int segmentAt (const Clip& c, juce::Point<float> p) const
    {
        const auto& list = points();
        const auto tick = xToTick (c, p.x);
        for (int k = 0; k + 1 < (int) list.size(); ++k)
            if (tick >= (double) list[(std::size_t) k].tick && tick < (double) list[(std::size_t) k + 1].tick)
                return k;
        return -1;
    }
    juce::String valueText (float v) const
    {
        if (lane == clipenv::panLane)
        {
            const auto pan = juce::roundToInt (clipenv::panOf (v) * 100.0f);
            return pan == 0 ? tr ("Centered") : juce::String (std::abs (pan)) + (pan < 0 ? "% L" : "% R");
        }
        const auto gain = clipenv::gainOf (v);
        return gain <= 0.0f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (gain), 1) + " dB";
    }

    static constexpr int barHeight = 26;
    static inline const std::vector<AutomationPoint> none {};
    AudioEngine& engine;
    int track, scene;
    std::uint8_t lane = clipenv::volumeLane;
    juce::ComboBox device, control;
    int dragPoint = -1, bendSegment = -1;
    float bendStart = 0.0f;
    bool moved = false;
    juce::String readout;
};

class ClipAudioPanel final : public juce::Component, private juce::ChangeListener
{
public:
    ClipAudioPanel (AudioEngine& e, int trackIn, int sceneIn, int channelIn) : engine (e), track (trackIn), scene (sceneIn), channel (channelIn)
    {
        using namespace theme;
        readFileInfo();

        // Warp | (Follow: left out, its meaning is not known yet)
        warp.setButtonText (tr ("Warp"));
        warp.setClickingTogglesState (true);
        warp.setTooltip (tr ("Warp: the sample follows the song's tempo. Off: it plays at its own speed. It belongs to the channel: every clip of this audio changes with it"));
        warp.onClick = [this] {
            if (refreshing || ! (fileSeconds > 0.0))
                return;
            engine.editSong ([c = channel, on = warp.getToggleState(), seconds = fileSeconds] (Song& s) { clipsample::setWarp (s, c, on, seconds); });
        };

        // the warp mode: Live's names first
        for (const auto& m : liveModes)
            mode.addItem (tr (m.name), m.method + 2);
        mode.addSeparator();
        for (const auto& m : flModes)
            mode.addItem (tr (m.name), m.method + 2);
        mode.setTooltip (tr ("Warp mode: how the audio is stretched. Re-Pitch changes speed and pitch together. Only this clip changes: when other clips play the same audio, it gets its own copy"));
        mode.onChange = [this] {
            if (refreshing)
                return;
            const auto id = mode.getSelectedId() - 2;
            editMode ([id] (Song& s, int c) { warpmodes::setMode (s, c, id); });
        };

        // the mode's own settings (round 190fm; Live's Clip View shows them under the mode): Beats › Preserve, Transient
        // Loop Mode and Transient Envelope; Tones / Texture › Grain Size
        for (int k = 0; k < (int) warpmodes::preserveNames().size(); ++k)
            preserve.addItem (tr (warpmodes::preserveNames()[(std::size_t) k]), k + 1);
        preserve.setTooltip (tr ("Preserve: where Beats cuts the audio: at its transients, or on a note grid"));
        preserve.onChange = [this] {
            if (! refreshing)
                editMode ([v = preserve.getSelectedId() - 1] (Song& s, int c) { warpmodes::setPreserve (s, c, v); });
        };
        for (int k = 0; k < (int) warpmodes::loopNames().size(); ++k)
            loopMode.addItem (tr (warpmodes::loopNames()[(std::size_t) k]), k + 1);
        loopMode.setTooltip (tr ("Transient Loop Mode: what fills the gap after each piece when the audio is slowed down"));
        loopMode.onChange = [this] {
            if (! refreshing)
                editMode ([v = loopMode.getSelectedId() - 1] (Song& s, int c) { warpmodes::setLoopMode (s, c, v); });
        };
        {
            ui::ValueSpec spec;
            spec.name = tr ("Transient Envelope: a fade over each piece (100: no fade)");
            spec.minimum = 0.0;
            spec.maximum = 100.0;
            spec.interval = 1.0;
            spec.pixelsForRange = 200.0;
            spec.defaultValue = 100.0;
            spec.toText = [] (double v) { return juce::String (juce::roundToInt (v)); };
            envelope = std::make_unique<ui::NumBox> (spec, [this] { return (double) sampler().warpModes.beatsEnvelope; }, [this] (double v) {
                editMode ([v] (Song& s, int c) { warpmodes::setEnvelope (s, c, v); });
            });
        }
        // Grain Size: one box per mode, each in Live's range with Live's default (Tones 12…100, 30; Texture 2…263, 65)
        for (auto texture : { false, true })
        {
            ui::ValueSpec spec;
            spec.name = tr ("Grain Size: the length of the pieces the audio is stretched in");
            spec.minimum = texture ? WarpModeSettings::textureGrainMin : WarpModeSettings::tonesGrainMin;
            spec.maximum = texture ? WarpModeSettings::textureGrainMax : WarpModeSettings::tonesGrainMax;
            spec.interval = 1.0;
            spec.pixelsForRange = texture ? 400.0 : 200.0;
            spec.defaultValue = texture ? WarpModeSettings::defaultTextureGrain : WarpModeSettings::defaultTonesGrain;
            spec.toText = [] (double v) { return juce::String (juce::roundToInt (v)); };
            (texture ? grainTexture : grainTones) = std::make_unique<ui::NumBox> (spec, [this, texture] {
                const auto& w = sampler().warpModes;
                return (double) (texture ? w.textureGrain : w.tonesGrain);
            }, [this] (double v) { editMode ([v] (Song& s, int c) { warpmodes::setGrain (s, c, v); }); });
        }

        // BPM with ÷2 and ×2 (the audio editor's commands, round 190cy, on this clip)
        {
            ui::ValueSpec spec;
            spec.name = tr ("BPM: the tempo the sample is read at");
            spec.minimum = 20.0;
            spec.maximum = 999.0;
            spec.interval = 0.01;
            spec.pixelsForRange = 979.0 * 4.0;
            spec.defaultValue = engine.getSong().bpm;
            spec.toText = [] (double v) { return juce::String (v, 2); };
            bpm = std::make_unique<ui::NumBox> (spec, [this] { return clipsample::fileBpm (sampler(), fileSeconds); }, [this] (double v) {
                engine.editSong ([c = channel, t = track, sc = scene, seconds = fileSeconds, v] (Song& s) { clipsample::setFileBpm (s, c, t, sc, seconds, v); });
            });
        }
        halve.setButtonText (juce::String::fromUTF8 ("\xc3\xb7" "2"));
        twice.setButtonText (juce::String::fromUTF8 ("\xc3\x97" "2"));
        halve.setTooltip (tr ("Halves the tempo the sample is read at: the clip plays it twice as fast"));
        twice.setTooltip (tr ("Doubles the tempo the sample is read at: the clip plays it half as fast"));
        halve.onClick = [this] { scaleTempo (0.5); };
        twice.onClick = [this] { scaleTempo (2.0); };

        gain.setSliderStyle (juce::Slider::LinearVertical);
        gain.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 16);
        gain.setRange (-70.0, 24.0, 0.1);
        gain.setDoubleClickReturnValue (true, 0.0);
        gain.setTextValueSuffix (" dB");
        gain.setTooltip (tr ("Clip gain"));
        gain.onValueChange = [this] {
            editClip ([db = gain.getValue()] (Clip& c) { c.gain = db <= -70.0 ? 0.0f : (float) std::pow (10.0, db / 20.0); });
        };

        // Pitch: the knob (semitones) and the semitone and cent boxes under it
        pitchKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pitchKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pitchKnob.setRange (-12.0, 12.0, 1.0);
        pitchKnob.setDoubleClickReturnValue (true, 0.0);
        pitchKnob.setTooltip (tr ("Clip pitch in semitones"));
        pitchKnob.onValueChange = [this] {
            if (refreshing)
                return;
            semitones.setValue (pitchKnob.getValue(), juce::sendNotificationSync);
        };
        for (auto* s : { &semitones, &cents })
        {
            s->setSliderStyle (juce::Slider::LinearBar); // (a number box dragged sideways, as Live's st / ct boxes)
            s->setDoubleClickReturnValue (true, 0.0);
            s->setColour (juce::Slider::trackColourId, colour::bg4);
            s->onValueChange = [this] {
                const auto pitch = (float) juce::jlimit (-12.0, 12.0, semitones.getValue() + cents.getValue() / 100.0);
                editClip ([pitch] (Clip& c) { c.pitch = pitch; });
            };
        }
        semitones.setRange (-12.0, 12.0, 1.0);
        semitones.setTooltip (tr ("Clip pitch in semitones"));
        cents.setRange (-50.0, 50.0, 1.0);
        cents.setTooltip (tr ("Clip pitch, fine: cents"));

        reverse.setButtonText (tr ("Reverse"));
        reverse.setClickingTogglesState (true);
        reverse.setTooltip (tr ("Plays the clip backwards"));
        reverse.onClick = [this] { editClip ([on = reverse.getToggleState()] (Clip& c) { c.reversed = on; }); };
        edit.setButtonText (tr ("Edit"));
        edit.setTooltip (tr ("Opens the sample in the audio editor window"));
        edit.onClick = [this] { openAudioEditorForChannel (engine, channel); };
        fade.setButtonText (tr ("Fade"));
        fade.setClickingTogglesState (true);
        fade.setTooltip (tr ("Fade: short fades at the clip's start and end, so its edges don't click"));
        fade.onClick = [this] {
            editClip ([on = fade.getToggleState(), tempo = engine.getSong().bpm] (Clip& c) { clipsample::setEdgeFades (c, on, tempo); });
        };

        for (auto* b : { &warp, &reverse, &fade })
        {
            b->setColour (juce::TextButton::buttonOnColourId, colour::accent);
            b->setColour (juce::TextButton::textColourOnId, colour::onAccent);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &warp, &mode, bpm.get(), &halve, &twice, &gain, &pitchKnob, &semitones, &cents, &reverse, &edit, &fade })
            addAndMakeVisible (*c);
        for (auto* c : std::initializer_list<juce::Component*> { &preserve, &loopMode, envelope.get(), grainTones.get(), grainTexture.get() })
            addChildComponent (*c);

        editor = makeAudioEditorPanel (engine, channel, -1, true);
        if (editor != nullptr)
        {
            setAudioEditorGridClip (*editor, track, scene); // (round 190cy: ÷2 / ×2 halve / double this clip too)
            addAndMakeVisible (*editor);
        }
        // round 190gi: Live's Sample | Envelopes tabs at the right of the editor's title bar
        for (auto* b : { &sampleTab, &envelopesTab })
        {
            addAndMakeVisible (*b);
            b->setConnectedEdges (0);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, colour::accent);
            b->setColour (juce::TextButton::textColourOnId, colour::onAccent);
        }
        sampleTab.setButtonText (tr ("Sample"));
        envelopesTab.setButtonText (tr ("Envelopes"));
        envelopesTab.setTooltip (tr ("The clip's envelopes: its gain and its panning while it plays"));
        sampleTab.onClick = [this] { showEnvelopes (false); };
        envelopesTab.onClick = [this] { showEnvelopes (true); };
        envelopes = std::make_unique<ClipEnvelopeView> (engine, track, scene);
        addChildComponent (*envelopes);
        showEnvelopes (envelopesShown);
        engine.addChangeListener (this);
        refresh();
    }
    // the tab shown (kept for the next clip, as the pattern clips' Notes | Envelopes)
    void showEnvelopes (bool on)
    {
        envelopesShown = on;
        sampleTab.setToggleState (! on, juce::dontSendNotification);
        envelopesTab.setToggleState (on, juce::dontSendNotification);
        envelopes->setVisible (on);
        if (editor != nullptr)
            editor->setVisible (! on);
        resized();
    }
    ~ClipAudioPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto column = getLocalBounds().removeFromLeft (columnWidth);
        g.setColour (colour::bg1);
        g.fillRect (column);
        g.setColour (colour::border);
        g.fillRect (column.getRight() - 1, 0, 1, getHeight());
        g.setColour (colour::textSecondary);
        g.setFont (font (type::caption, true));
        g.drawText (tr ("Audio"), column.withHeight (18).reduced (8, 0), juce::Justification::centredLeft);
        g.setFont (font (type::caption));
        g.drawText (tr ("BPM"), bpmLabel, juce::Justification::centredLeft);
        if (grainTones->isVisible() || grainTexture->isVisible())
            g.drawText (tr ("Grain Size"), grainLabel, juce::Justification::centredLeft);
        if (envelope->isVisible())
            g.drawText (tr ("Envelope"), envelopeLabel, juce::Justification::centredLeft);
        g.drawText (tr ("Gain"), gainLabel, juce::Justification::centred);
        g.drawText (tr ("Pitch"), pitchLabel, juce::Justification::centred);
        g.drawText ("st", stLabel, juce::Justification::centred);
        g.drawText ("ct", ctLabel, juce::Justification::centred);
        // the sample's title over its editor, as Live's "loop16.wav 48.0 kHz · 16-Bit · 2 Ch"
        if (! titleBar.isEmpty())
        {
            g.setColour (colour::bg2);
            g.fillRect (titleBar);
            g.setColour (colour::border);
            g.fillRect (titleBar.getX(), titleBar.getBottom() - 1, titleBar.getWidth(), 1);
            const auto icon = titleBar.withWidth (22).toFloat().withSizeKeepingCentre (14.0f, 10.0f);
            g.setColour (colour::textSecondary);
            g.drawRect (icon, 1.0f);
            for (int k = 0; k < 4; ++k) // (a small waveform in the mark)
            {
                const auto x = icon.getX() + 3.0f + (float) k * 2.6f, h = k % 2 == 0 ? 5.0f : 2.5f;
                g.fillRect (x, icon.getCentreY() - h * 0.5f, 1.2f, h);
            }
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small));
            g.drawText (titleText, titleBar.withTrimmedLeft (26).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
    }

    // Live's Audio section [SHOT live-clipview-audio-panel.png]: on the left Warp, the mode, BPM ÷2 ×2, then Reverse | Edit
    // and Fade at the bottom; on the right the Gain fader with its dB read-out, then Pitch: the knob, st and ct boxes
    void resized() override
    {
        auto area = getLocalBounds();
        auto column = area.removeFromLeft (columnWidth).reduced (8, 0);
        column.removeFromTop (20);
        column.removeFromBottom (4);
        auto right = column.removeFromRight (86);
        column.removeFromRight (8);
        auto left = column;
        warp.setBounds (left.removeFromTop (20).withWidth (left.getWidth() / 2));
        left.removeFromTop (4);
        mode.setBounds (left.removeFromTop (22));
        // the mode's own settings under it (Beats: Preserve, then the loop mode and the envelope; Tones / Texture: Grain Size)
        {
            left.removeFromTop (4);
            auto row1 = left.removeFromTop (20);
            preserve.setBounds (row1);
            grainLabel = row1.removeFromLeft (row1.getWidth() / 2);
            grainTones->setBounds (row1);
            grainTexture->setBounds (row1);
            left.removeFromTop (4);
            loopMode.setBounds (left.removeFromTop (20));
            left.removeFromTop (4);
            auto row3 = left.removeFromTop (20); // (Envelope on its own row, like Grain Size: its label fits)
            envelopeLabel = row3.removeFromLeft (row3.getWidth() / 2);
            envelope->setBounds (row3);
        }
        fade.setBounds (left.removeFromBottom (18).withWidth (left.getWidth() / 2 - 2));
        left.removeFromBottom (4);
        {
            auto row = left.removeFromBottom (20);
            reverse.setBounds (row.removeFromLeft (row.getWidth() / 2 - 2));
            row.removeFromLeft (4);
            edit.setBounds (row);
        }
        left.removeFromBottom (8);
        {
            auto row = left.removeFromBottom (20);
            bpm->setBounds (row.removeFromLeft (row.getWidth() - 56));
            row.removeFromLeft (4);
            halve.setBounds (row.removeFromLeft (24));
            row.removeFromLeft (2);
            twice.setBounds (row);
            bpmLabel = left.removeFromBottom (14);
        }

        gainLabel = right.removeFromTop (14);
        auto pitchArea = right.removeFromBottom (juce::jmin (88, right.getHeight() / 2 + 10));
        gain.setBounds (right);
        pitchLabel = pitchArea.removeFromTop (14);
        pitchKnob.setBounds (pitchArea.removeFromTop (30).withSizeKeepingCentre (30, 30));
        auto boxes = pitchArea;
        auto labels = boxes.removeFromBottom (12);
        stLabel = labels.removeFromLeft (labels.getWidth() / 2);
        ctLabel = labels;
        semitones.setBounds (boxes.removeFromLeft (boxes.getWidth() / 2 - 2));
        boxes.removeFromLeft (4);
        cents.setBounds (boxes);

        titleBar = area.removeFromTop (20);
        {
            auto tabs = titleBar.reduced (2, 2);
            envelopesTab.setBounds (tabs.removeFromRight (80));
            tabs.removeFromRight (2);
            sampleTab.setBounds (tabs.removeFromRight (64));
        }
        if (editor != nullptr)
            editor->setBounds (area);
        if (envelopes != nullptr)
            envelopes->setBounds (area);
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    const SamplerSettings& sampler() const
    {
        static const SamplerSettings none;
        const auto& song = engine.getSong();
        return juce::isPositiveAndBelow (channel, (int) song.channels.size()) ? song.channels[(std::size_t) channel].sampler : none;
    }
    const Clip* clip() const
    {
        const auto& song = engine.getSong();
        return song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    }
    void editClip (const std::function<void (Clip&)>& fn)
    {
        if (refreshing)
            return;
        engine.editSong ([this, &fn] (Song& s) {
            if (s.clipGrid)
                if (auto* c = const_cast<Clip*> (launcher::clipAt (*s.clipGrid, track, scene)); c != nullptr && c->kind == ClipKind::audio)
                    fn (*c);
        });
    }
    // An edit of the warp mode or its settings: on this clip's own channel (O16: a copy when other clips share it, so only
    // this clip changes); the audio editor follows the new channel.
    void editMode (std::function<void (Song&, int)> fn)
    {
        if (refreshing)
            return;
        auto target = channel;
        engine.editSong ([this, &fn, &target] (Song& s) {
            target = warpmodes::ownChannelForGridClip (s, track, scene);
            if (target >= 0)
                fn (s, target);
        });
        if (target >= 0 && target != channel)
        {
            channel = target;
            editor = makeAudioEditorPanel (engine, channel, -1, true);
            if (editor != nullptr)
            {
                setAudioEditorGridClip (*editor, track, scene);
                addChildComponent (*editor);
                editor->setVisible (! envelopesShown);
            }
            resized();
            refresh();
        }
    }
    void scaleTempo (double factor)
    {
        engine.editSong ([c = channel, t = track, sc = scene, seconds = fileSeconds, factor] (Song& s) { clipsample::scaleWarpedTempo (s, c, t, sc, seconds, factor); });
    }
    void refresh()
    {
        const auto* c = clip();
        const auto& song = engine.getSong();
        if (c == nullptr || ! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        const juce::ScopedValueSetter<bool> guard (refreshing, true);
        const auto& s = sampler();
        const auto n = juce::dontSendNotification;
        mode.setSelectedId (s.keepPitch ? (int) s.stretchMethod + 2 : 1, n);
        const auto warped = clipsample::isWarped (s);
        warp.setToggleState (warped, n);
        warp.setEnabled (fileSeconds > 0.0);
        for (auto* w : std::initializer_list<juce::Component*> { bpm.get(), &halve, &twice })
            w->setEnabled (warped && fileSeconds > 0.0);
        bpm->repaint();
        gain.setValue (c->gain > 0.0f ? juce::jlimit (-70.0, 24.0, 20.0 * std::log10 ((double) c->gain)) : -70.0, n);
        const auto whole = std::round ((double) c->pitch);
        semitones.setValue (whole, n);
        pitchKnob.setValue (whole, n);
        cents.setValue (std::round (((double) c->pitch - whole) * 100.0), n);
        reverse.setToggleState (c->reversed, n);
        // the mode's own settings: shown for the mode they belong to, only while the sample is warped and keeps its pitch
        const auto beats = warped && s.keepPitch && s.stretchMethod == StretchMethod::beats;
        const auto tones = warped && s.keepPitch && s.stretchMethod == StretchMethod::tones;
        const auto texture = warped && s.keepPitch && s.stretchMethod == StretchMethod::texture;
        preserve.setVisible (beats);
        loopMode.setVisible (beats);
        envelope->setVisible (beats);
        grainTones->setVisible (tones);
        grainTexture->setVisible (texture);
        preserve.setSelectedId ((int) s.warpModes.beatsPreserve + 1, n);
        loopMode.setSelectedId ((int) s.warpModes.beatsLoop + 1, n);
        envelope->repaint();
        grainTones->repaint();
        grainTexture->repaint();
        repaint();
        fade.setToggleState (clipsample::hasEdgeFades (*c), n);
    }

    // The file's rate, depth, channels and length, read from its header once (the panel is made again for another clip).
    void readFileInfo()
    {
        const auto& song = engine.getSong();
        if (! juce::isPositiveAndBelow (channel, (int) song.channels.size()))
            return;
        const juce::File file (juce::String::fromUTF8 (song.channels[(std::size_t) channel].samplePath.c_str()));
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        double rate = 0.0;
        int bits = 0, channels = 0;
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr && reader->sampleRate > 0.0)
        {
            rate = reader->sampleRate;
            bits = (int) reader->bitsPerSample;
            channels = (int) reader->numChannels;
            fileSeconds = (double) reader->lengthInSamples / reader->sampleRate;
        }
        else if (const auto data = engine.getSampleLibrary().findLoaded (file.getFullPathName()); data != nullptr && data->sampleRate > 0.0)
        {
            rate = data->sampleRate;
            channels = data->right.empty() && data->mappedRight == nullptr ? 1 : 2;
            fileSeconds = (double) (data->isMapped() ? data->mappedFrames : data->left.size()) / data->sampleRate;
        }
        if (file.getFileName().isNotEmpty())
            titleText = juce::String::fromUTF8 (clipsample::infoText (file.getFileName().toStdString(), rate, bits, channels).c_str());
    }

    static constexpr int columnWidth = 236;
    AudioEngine& engine;
    int track, scene, channel;
    juce::TextButton warp, halve, twice, reverse, edit, fade;
    juce::ComboBox mode, preserve, loopMode;
    std::unique_ptr<ui::NumBox> bpm, envelope, grainTones, grainTexture;
    juce::Slider gain, pitchKnob, semitones, cents;
    juce::Rectangle<int> grainLabel, envelopeLabel, bpmLabel, gainLabel, pitchLabel, stLabel, ctLabel, titleBar;
    juce::String titleText;
    double fileSeconds = 0.0;
    std::unique_ptr<juce::Component> editor;
    juce::TextButton sampleTab, envelopesTab;      // round 190gi
    std::unique_ptr<ClipEnvelopeView> envelopes;
    static inline bool envelopesShown = false;     // (not saved)
    bool refreshing = false;
};
} // namespace

std::unique_ptr<juce::Component> makeClipAudioPanel (AudioEngine& engine, int track, int scene)
{
    const auto& song = engine.getSong();
    const auto* clip = song.clipGrid ? launcher::clipAt (*song.clipGrid, track, scene) : nullptr;
    if (clip == nullptr || clip->kind != ClipKind::audio || ! juce::isPositiveAndBelow (clip->channel, (int) song.channels.size()))
        return {};
    return std::make_unique<ClipAudioPanel> (engine, track, scene, clip->channel);
}
} // namespace lynx
