#include "ui/FpcEditor.h"
#include "ui/FileChoosers.h"

#include <algorithm>
#include <cmath>

#include "core/FlSamplers.h"
#include "core/Fpc.h"
#include "core/Instruments.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "engine/SampleLibrary.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
constexpr float scale = 1.25f; // FL's form (790 × 367), a quarter larger

juce::Rectangle<int> fl (int x, int y, int w, int h)
{
    return juce::Rectangle<float> ((float) x * scale, (float) y * scale, (float) w * scale, (float) h * scale).toNearestInt();
}

// FL's note names (its C5 is MIDI 60)
juce::String keyName (int key)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12);
}

juce::String fileName (const std::string& path)
{
    return juce::String::fromUTF8 (path.c_str()).fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
}

juce::String text (const std::u16string& s)
{
    auto t = s;
    t.erase (std::find (t.begin(), t.end(), u'\0'), t.end());
    return juce::String::fromUTF8 (fpc::toUtf8 (t).c_str());
}

// a layer with a sample (the rule of fpcLayerPaths: a path up to its first NUL)
bool hasPath (const fpc::Layer& layer) { return ! layer.path.empty() && layer.path.front() != u'\0'; }

// ---------------------------------------------------------------------------------------------------------------
// FL's small switch (TQuickBtn with a light)
class Led final : public juce::Component
{
public:
    std::function<void (bool)> onChange;
    std::function<void()> onMenu;

    void set (bool nowOn)
    {
        if (on != nowOn)
        {
            on = nowOn;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::colour::bg4);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const auto d = std::min (r.getWidth(), r.getHeight()) * 0.45f;
        g.setColour (on ? theme::colour::accent : theme::colour::bg0);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (r.getCentre()));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu)
                onMenu();
            return;
        }
        on = ! on;
        repaint();
        if (onChange)
            onChange (on);
    }

private:
    bool on = false;
};

// FL's digit wheel (TVectorDigiWheel): a number dragged up / down
class DigiWheel final : public juce::Component
{
public:
    std::function<void (int)> onChange;
    std::function<juce::String (int)> textOf;
    int low = 0, high = 99, resetTo = 0;

    void set (int v)
    {
        v = std::clamp (v, low, high);
        if (v != value)
        {
            value = v;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 2.0f);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (textOf ? textOf (value) : juce::String (value), getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent&) override { dragFrom = value; }
    void mouseDrag (const juce::MouseEvent& e) override { change (dragFrom - e.getDistanceFromDragStartY() / 4); }
    void mouseDoubleClick (const juce::MouseEvent&) override { change (resetTo); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { change (value + (w.deltaY > 0.0f ? 1 : -1)); }

private:
    void change (int v)
    {
        v = std::clamp (v, low, high);
        if (v == value)
            return;
        value = v;
        repaint();
        if (onChange)
            onChange (v);
    }

    int value = 0, dragFrom = 0;
};

// a sample's waveform (FL's THQWAVScope); a click previews it
class WaveView final : public juce::Component
{
public:
    std::function<std::shared_ptr<const SampleData>()> sample;
    std::function<void()> onClick;
    std::function<void()> onMenu;

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds());
        const auto data = sample ? sample() : nullptr;
        const auto mid = (float) getHeight() * 0.5f;
        g.setColour (theme::colour::border);
        g.drawHorizontalLine ((int) mid, 0.0f, (float) getWidth());
        if (data == nullptr || data->frames() == 0)
            return;
        g.setColour (theme::colour::accent);
        const auto frames = data->frames();
        const auto* l = data->leftData();
        for (int x = 0; x < getWidth(); ++x)
        {
            const auto from = (std::size_t) ((double) x / getWidth() * (double) frames);
            const auto to = std::min (frames, std::max (from + 1, (std::size_t) ((double) (x + 1) / getWidth() * (double) frames)));
            float lo = 0.0f, hi = 0.0f;
            const auto step = std::max<std::size_t> (1, (to - from) / 64);
            for (auto i = from; i < to; i += step)
            {
                lo = std::min (lo, l[i]);
                hi = std::max (hi, l[i]);
            }
            g.drawVerticalLine (x, mid - hi * mid, std::max (mid - lo * mid, mid - hi * mid + 1.0f));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() ? (bool) onMenu : (bool) onClick)
            (e.mods.isPopupMenu() ? onMenu : onClick)();
    }
};

// the pads of the shown bank: FL's 4 × 4 buttons, pad 1 at the bottom left
class PadGrid final : public juce::Component
{
public:
    std::function<juce::String (int)> name;   // pad 0…15 of the bank
    std::function<bool (int)> loaded, muted;
    std::function<int()> selected;
    std::function<void (int)> onPad;

    static juce::Rectangle<int> cell (int pad)
    {
        // btnPadMain1 … 16: columns at 14 / 81 / 148 / 215, rows at 274 / 209 / 144 / 79 (60 × 56), from the grid's corner (14, 79)
        return fl ((pad % 4) * 67, (3 - pad / 4) * 65, 60, 56);
    }

    void paint (juce::Graphics& g) override
    {
        for (int pad = 0; pad < 16; ++pad)
        {
            const auto r = cell (pad).toFloat();
            const auto on = selected && selected() == pad;
            const auto has = loaded && loaded (pad);
            g.setColour (has ? theme::colour::bg5 : theme::colour::bg3);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? theme::colour::accent : theme::colour::borderStrong);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, on ? 2.0f : 1.0f);
            g.setColour (muted && muted (pad) ? theme::colour::textDisabled : has ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            g.drawFittedText (name ? name (pad) : juce::String(), cell (pad).reduced (4), juce::Justification::centredBottom, 2);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int pad = 0; pad < 16; ++pad)
            if (cell (pad).contains (e.getPosition()) && onPad)
                onPad (pad);
    }
};

// a pad's volume or pan envelope as the state holds it (FL's TMEnvEditor; shown, not edited yet)
class EnvView final : public juce::Component
{
public:
    std::function<const fpc::Envelope*()> envelope;

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds());
        const auto* e = envelope ? envelope() : nullptr;
        if (e == nullptr || e->points.empty())
            return;
        double total = 0.0;
        for (const auto& p : e->points)
            total += std::max (0.0, p.time);
        const auto area = getLocalBounds().toFloat().reduced (6.0f);
        juce::Path path;
        double at = 0.0;
        for (std::size_t i = 0; i < e->points.size(); ++i)
        {
            at += std::max (0.0, e->points[i].time);
            const auto x = area.getX() + (total > 0.0 ? (float) (at / total) : 0.0f) * area.getWidth();
            const auto y = area.getBottom() - (float) std::clamp (e->points[i].value, 0.0, 1.0) * area.getHeight();
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
            g.setColour (theme::colour::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, y }));
        }
        if (e->points.size() == 1)
            path.lineTo (area.getRight(), path.getCurrentPosition().y);
        g.setColour (theme::colour::accent);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }
};

// one layer of the pad (FL's WPHiddenLayer row, 465 × 29): number, volume / pan / tune wheels, its sample, its velocity range
class LayerRow final : public juce::Component
{
public:
    LayerRow()
    {
        for (auto* k : { &volume, &pan, &tune })
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->setPopupDisplayEnabled (true, true, nullptr);
            addAndMakeVisible (*k);
        }
        volume.setRange (0.0, 127.0, 1.0);
        volume.setDoubleClickReturnValue (true, 100.0);
        pan.setRange (-128.0, 128.0, 1.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        tune.setRange (-128.0, 128.0, 1.0);
        tune.setDoubleClickReturnValue (true, 0.0);
        tune.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 9.375)) + " " + tr ("cents"); };
        ui::setHint (volume, tr ("Layer volume"));
        ui::setHint (pan, tr ("Layer pan"));
        ui::setHint (tune, tr ("Layer tune"));
        range.setSliderStyle (juce::Slider::TwoValueHorizontal);
        range.setRange (0.0, 127.0, 1.0);
        range.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        range.setPopupDisplayEnabled (true, true, nullptr);
        ui::setHint (range, tr ("Active range: the velocities that play this layer"));
        addAndMakeVisible (range);
        ui::setHint (wave, tr ("Left click to preview"));
        addAndMakeVisible (wave);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (selected ? theme::colour::accentMuted : theme::colour::bg3);
        g.fillRect (getLocalBounds().reduced (0, 1));
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (fl (3, 3, 21, 22).toFloat(), 3.0f);
        g.setColour (selected ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (juce::String (number), fl (3, 3, 21, 22), juce::Justification::centred, false);
    }

    void resized() override
    {
        volume.setBounds (fl (37, 2, 24, 24));
        pan.setBounds (fl (72, 2, 24, 24));
        tune.setBounds (fl (107, 2, 24, 24));
        wave.setBounds (fl (140, 4, 101, 20));
        range.setBounds (fl (247, 2, 215, 24));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onSelect)
            onSelect();
    }

    juce::Slider volume, pan, tune, range;
    WaveView wave;
    std::function<void()> onSelect;
    int number = 1;
    bool selected = false;
};

// ---------------------------------------------------------------------------------------------------------------
class FpcEditor final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener
{
public:
    FpcEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        juce::ignoreUnused (channel);
        auto button = [this] (juce::TextButton& b, const juce::String& caption, const juce::String& hint, std::function<void()> click) {
            b.setButtonText (caption);
            ui::setHint (b, hint);
            b.onClick = std::move (click);
            b.setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        };
        button (previousPad, juce::String::fromUTF8 ("\xe2\x97\x80"), tr ("Previous pad"), [this] { selectPad ((selectedPad + 31) % 32, false); });
        button (nextPad, juce::String::fromUTF8 ("\xe2\x96\xb6"), tr ("Next pad"), [this] { selectPad ((selectedPad + 1) % 32, false); });
        button (createLayer, "CREATE", tr ("Create a new layer"), [this] {
            edit ([this] (fpc::Kit& k) {
                auto& pad = k.pads[(std::size_t) selectedPad];
                pad.layers.emplace_back();
                files[(std::size_t) selectedPad].emplace_back();
                selectedLayer = (int) pad.layers.size() - 1;
            });
        });
        button (deleteLayer, "DELETE", tr ("Delete current layer"), [this] {
            edit ([this] (fpc::Kit& k) {
                auto& pad = k.pads[(std::size_t) selectedPad];
                auto& padFiles = files[(std::size_t) selectedPad];
                if (selectedLayer < 0 || selectedLayer >= (int) pad.layers.size())
                    return;
                pad.layers.erase (pad.layers.begin() + selectedLayer);
                padFiles.erase (padFiles.begin() + selectedLayer);
                selectedLayer = std::max (0, std::min (selectedLayer, (int) pad.layers.size() - 1));
            });
        });
        button (layerUp, juce::String::fromUTF8 ("\xe2\x96\xb2"), tr ("Move layer up"), [this] { moveLayer (-1); });
        button (layerDown, juce::String::fromUTF8 ("\xe2\x96\xbc"), tr ("Move layer down"), [this] { moveLayer (1); });
        button (spreadEven, "SPREAD EVEN", tr ("Spread velocity range across layers"), [this] {
            edit ([this] (fpc::Kit& k) {
                auto& layers = k.pads[(std::size_t) selectedPad].layers;
                const auto n = (int) layers.size();
                for (int i = 0; i < n; ++i)
                {
                    layers[(std::size_t) i].velocityLow = i * 128 / n;
                    layers[(std::size_t) i].velocityHigh = i + 1 == n ? 127 : (i + 1) * 128 / n;
                }
            });
        });
        button (loadButton, juce::String::fromUTF8 ("\xe2\x96\xbe"), tr ("Load sample"), [this] { chooseSample(); });
        button (volumeTab, "VOL", tr ("Volume envelope"), [this] { envelopeTab = 0; refresh(); });
        button (panTab, "PAN", tr ("Pan envelope"), [this] { envelopeTab = 1; refresh(); });
        ui::setHint (bankA, tr ("Bank A"));
        ui::setHint (bankB, tr ("Bank B"));
        bankA.onChange = [this] (bool) { selectPad (selectedPad % 16, false); };
        bankB.onChange = [this] (bool) { selectPad (16 + selectedPad % 16, false); };
        addAndMakeVisible (bankA);
        addAndMakeVisible (bankB);

        currentPad.setJustificationType (juce::Justification::centred);
        padName.setJustificationType (juce::Justification::centred);
        padName.setEditable (false, true, false);
        padName.onTextChange = [this] {
            const auto name = fpc::fromUtf8 (padName.getText().toStdString());
            edit ([this, name] (fpc::Kit& k) { k.pads[(std::size_t) selectedPad].name = name; });
        };
        ui::setHint (padName, tr ("Pad name (double click to rename)"));
        padNote.setJustificationType (juce::Justification::centred);
        padNote.addMouseListener (this, false);
        ui::setHint (padNote, tr ("Click to select a note to trigger this pad"), true);
        sampleName.setJustificationType (juce::Justification::centredLeft);
        ui::setHint (sampleName, tr ("Sample name"));
        for (auto* lab : { &currentPad, &padName, &padNote, &sampleName })
        {
            lab->setFont (theme::font (theme::type::small));
            lab->setColour (juce::Label::backgroundColourId, theme::colour::bg0);
            addAndMakeVisible (*lab);
        }

        auto wheel = [this] (juce::Slider& w, const juce::String& hint, double lo, double hi, double reset, std::function<void (fpc::Pad&, int)> set) {
            w.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            w.setRange (lo, hi, 1.0);
            w.setDoubleClickReturnValue (true, reset);
            w.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            w.setPopupDisplayEnabled (true, true, this);
            ui::setHint (w, hint);
            w.onValueChange = [this, &w, set] {
                if (! loading)
                    edit ([this, &w, set] (fpc::Kit& k) { set (k.pads[(std::size_t) selectedPad], juce::roundToInt (w.getValue())); });
            };
            addAndMakeVisible (w);
        };
        wheel (padVolume, tr ("Pad volume"), 0.0, 127.0, 99.0, [] (fpc::Pad& p, int v) { p.volume = v; });
        wheel (padPan, tr ("Pad pan"), -128.0, 128.0, 0.0, [] (fpc::Pad& p, int v) { p.pan = v; });
        wheel (padTune, tr ("Pad tune"), -128.0, 128.0, 0.0, [] (fpc::Pad& p, int v) { p.tune = v; });
        padTune.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 9.375)) + " " + tr ("cents"); };

        auto led = [this] (Led& b, const juce::String& hint, int bit) {
            ui::setHint (b, hint);
            b.onChange = [this, bit] (bool on) {
                edit ([this, bit, on] (fpc::Kit& k) {
                    auto& flags = k.pads[(std::size_t) selectedPad].flags;
                    flags = on ? (flags | bit) : (flags & ~bit);
                });
            };
            addAndMakeVisible (b);
        };
        led (mute, tr ("Mute"), 1);
        led (solo, tr ("Solo"), 2);
        led (scaleVolume, tr ("Scale volume with velocity"), 4);
        led (randomTrigger, tr ("Trigger overlapping layers randomly"), 8);
        randomTrigger.onMenu = [this] { triggerMenu(); };

        auto digi = [this] (DigiWheel& w, const juce::String& hint, int lo, int hi, int reset, std::function<void (fpc::Pad&, int)> set) {
            w.low = lo;
            w.high = hi;
            w.resetTo = reset;
            ui::setHint (w, hint);
            w.onChange = [this, set] (int v) { edit ([this, set, v] (fpc::Kit& k) { set (k.pads[(std::size_t) selectedPad], v); }); };
            addAndMakeVisible (w);
        };
        digi (cutGroup, tr ("Cut which group?"), -1, 99, -1, [] (fpc::Pad& p, int v) { p.cut = v; });
        digi (cutBy, tr ("Cut by which group?"), -1, 99, -1, [] (fpc::Pad& p, int v) { p.cutBy = v; });
        digi (output, tr ("Pad output offset"), -99, 103, 0, [] (fpc::Pad& p, int v) { p.output = v; });
        cutGroup.textOf = cutBy.textOf = [] (int v) { return v < 0 ? juce::String ("---") : juce::String (v); };
        output.textOf = [] (int v) { return v == 0 ? juce::String ("---") : juce::String (v); };

        grid.name = [this] (int pad) {
            const auto& p = kit.pads[(std::size_t) (bank() * 16 + pad)];
            const auto name = text (p.name);
            return name.isNotEmpty() ? name : tr ("Pad {0}", { juce::String (pad + 1) });
        };
        grid.loaded = [this] (int pad) {
            const auto& layers = kit.pads[(std::size_t) (bank() * 16 + pad)].layers;
            return std::any_of (layers.begin(), layers.end(), hasPath);
        };
        grid.muted = [this] (int pad) { return kit.pads[(std::size_t) (bank() * 16 + pad)].muted(); };
        grid.selected = [this] { return selectedPad % 16; };
        grid.onPad = [this] (int pad) { selectPad (bank() * 16 + pad, true); };
        ui::setHint (grid, tr ("Click a pad to select and play it; drop a sample on a pad to load it"));
        addAndMakeVisible (grid);

        layerView.setViewedComponent (&layerList, false);
        layerView.setScrollBarsShown (true, false);
        addAndMakeVisible (layerView);

        scope.sample = [this] { return layerSample (selectedPad, selectedLayer); };
        scope.onClick = [this] { preview (selectedPad, selectedLayer); };
        ui::setHint (scope, tr ("Left click to preview"));
        addAndMakeVisible (scope);
        envelope.envelope = [this] () -> const fpc::Envelope* {
            for (const auto& env : kit.pads[(std::size_t) selectedPad].envelopes)
                if (env.index == envelopeTab)
                    return &env;
            return nullptr;
        };
        addAndMakeVisible (envelope);

        engine.addChangeListener (this);
        reload();
        setSize (juce::roundToInt (790.0f * scale), juce::roundToInt (367.0f * scale));
    }

    ~FpcEditor() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg1);
        g.fillRect (fl (0, 0, 790, 33));    // the title strip
        g.setColour (theme::colour::bg3);
        g.fillRect (fl (0, 33, 790, 33));   // the pad's settings
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::display, true));
        g.drawText ("FPC", fl (10, 1, 110, 32), juce::Justification::centredLeft, false);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        // FL writes the names beside the controls: M, S, Scale Vol, then one dark strip with Midi Note, Cut, Cut By, Output
        g.drawText ("M", fl (435, 40, 11, 18), juce::Justification::centred, false);
        g.drawText ("S", fl (467, 40, 11, 18), juce::Justification::centred, false);
        g.drawText ("Scale Vol", fl (501, 40, 60, 18), juce::Justification::centredLeft, false);
        g.setColour (theme::colour::bg0);
        g.fillRect (fl (538, 40, 246, 18));
        g.setColour (theme::colour::textPrimary);
        g.drawText ("Midi Note", fl (538, 40, 57, 18), juce::Justification::centredRight, false);
        g.drawText ("Cut", fl (626, 40, 22, 18), juce::Justification::centredRight, false);
        g.drawText ("Cut By", fl (664, 40, 38, 18), juce::Justification::centredRight, false);
        g.drawText ("Output", fl (708, 40, 44, 18), juce::Justification::centredRight, false);
        // the bank bar
        g.setColour (theme::colour::textSecondary);
        g.drawText ("BANK A", fl (36, 334, 60, 19), juce::Justification::centredLeft, false);
        g.drawText ("BANK B", fl (193, 334, 60, 19), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        previousPad.setBounds (fl (8, 41, 15, 17));
        currentPad.setBounds (fl (23, 41, 78, 17));
        nextPad.setBounds (fl (101, 41, 15, 17));
        padName.setBounds (fl (202, 43, 90, 15));
        padVolume.setBounds (fl (301, 36, 24, 24));
        padPan.setBounds (fl (335, 36, 24, 24));
        padTune.setBounds (fl (375, 36, 24, 24));
        mute.setBounds (fl (415, 40, 19, 18));
        solo.setBounds (fl (447, 40, 19, 18));
        scaleVolume.setBounds (fl (479, 40, 19, 18));
        padNote.setBounds (fl (594, 44, 42, 13));
        cutGroup.setBounds (fl (648, 42, 19, 14));
        cutBy.setBounds (fl (702, 42, 19, 14));
        output.setBounds (fl (754, 42, 26, 14));
        grid.setBounds (fl (14, 79, 261, 251));
        bankA.setBounds (fl (16, 335, 17, 17));
        bankB.setBounds (fl (256, 335, 17, 17));
        createLayer.setBounds (fl (299, 73, 54, 19));
        deleteLayer.setBounds (fl (357, 73, 54, 19));
        layerUp.setBounds (fl (417, 73, 13, 10));
        layerDown.setBounds (fl (417, 82, 13, 10));
        spreadEven.setBounds (fl (440, 74, 79, 18));
        randomTrigger.setBounds (fl (762, 74, 19, 18));
        layerView.setBounds (fl (300, 99, 479, 118));
        scope.setBounds (fl (301, 220, 190, 117));
        loadButton.setBounds (fl (301, 341, 18, 15));
        sampleName.setBounds (fl (322, 341, 168, 17));
        envelope.setBounds (fl (495, 220, 269, 137));
        volumeTab.setBounds (fl (766, 220, 20, 67));
        panTab.setBounds (fl (766, 288, 20, 68));
        layoutLayers();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &padNote)
            noteMenu();
    }

    // a sample dropped on a pad loads into its first layer; anywhere else, into the selected layer
    bool isInterestedInFileDrag (const juce::StringArray& dropped) override
    {
        return dropped.size() == 1 && SampleLibrary::isSampleFile (juce::File (dropped[0]));
    }

    void filesDropped (const juce::StringArray& dropped, int x, int y) override
    {
        if (dropped.isEmpty())
            return;
        const auto at = juce::Point<int> (x, y) - grid.getPosition();
        for (int pad = 0; pad < 16; ++pad)
            if (PadGrid::cell (pad).contains (at))
            {
                selectPad (bank() * 16 + pad, false);
                selectedLayer = 0;
            }
        loadSample (dropped[0]);
    }

private:
    const ChannelDef* def() const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }
    int bank() const { return selectedPad / 16; }

    // the kit of the channel's state and each layer's file (ChannelDef::zones, one per layer with a path, in order)
    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        lastState = d->pluginState;
        lastZones = d->zones;
        kit = fpc::read (d->pluginState).value_or (fpc::emptyKit());
        while (kit.pads.size() < 32)
            kit.pads.emplace_back();
        files.assign (kit.pads.size(), {});
        std::size_t next = 0;
        for (std::size_t p = 0; p < kit.pads.size(); ++p)
            for (const auto& layer : kit.pads[p].layers)
            {
                std::string file;
                if (hasPath (layer))
                {
                    file = next < d->zones.size() ? d->zones[next].path : std::string();
                    ++next;
                }
                files[p].push_back (std::move (file));
            }
        selectedPad = std::clamp (selectedPad, 0, 31);
        refresh();
    }

    void edit (const std::function<void (fpc::Kit&)>& change)
    {
        change (kit);
        commit();
    }

    void commit()
    {
        const auto bytes = fpc::write (kit);
        std::vector<SampleZone> zones;
        for (std::size_t p = 0; p < kit.pads.size(); ++p)
            for (std::size_t i = 0; i < kit.pads[p].layers.size(); ++i)
                if (hasPath (kit.pads[p].layers[i]))
                {
                    SampleZone zone;
                    zone.path = i < files[p].size() ? files[p][i] : std::string();
                    zones.push_back (std::move (zone));
                }
        lastState = bytes;
        lastZones = zones;
        engine.editSong ([l = locate, bytes, zones] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                d->zones = zones;
            }
        });
        refresh();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d != nullptr && (d->pluginState != lastState || ! (d->zones == lastZones)))
            reload();
    }

    void selectPad (int pad, bool play)
    {
        selectedPad = std::clamp (pad, 0, 31);
        selectedLayer = 0;
        refresh();
        if (play)
            preview (selectedPad, 0);
    }

    void moveLayer (int by)
    {
        edit ([this, by] (fpc::Kit& k) {
            auto& layers = k.pads[(std::size_t) selectedPad].layers;
            auto& padFiles = files[(std::size_t) selectedPad];
            const auto to = selectedLayer + by;
            if (selectedLayer < 0 || selectedLayer >= (int) layers.size() || to < 0 || to >= (int) layers.size())
                return;
            std::swap (layers[(std::size_t) selectedLayer], layers[(std::size_t) to]);
            std::swap (padFiles[(std::size_t) selectedLayer], padFiles[(std::size_t) to]);
            selectedLayer = to;
        });
    }

    std::shared_ptr<const SampleData> layerSample (int pad, int layer)
    {
        if (pad < 0 || pad >= (int) files.size() || layer < 0 || layer >= (int) files[(std::size_t) pad].size())
            return nullptr;
        const auto& file = files[(std::size_t) pad][(std::size_t) layer];
        return file.empty() ? nullptr : engine.getSampleLibrary().load (juce::String::fromUTF8 (file.c_str()));
    }

    void preview (int pad, int layer)
    {
        if (auto sample = layerSample (pad, layer))
            engine.previewSample (std::move (sample));
    }

    void chooseSample()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Load sample..."), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<FpcEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().existsAsFile())
                self->loadSample (fc.getResult().getFullPathName());
        });
    }

    // the file into the selected layer (a pad without a layer gets one); an unnamed pad takes the file's name
    void loadSample (const juce::String& path)
    {
        const auto file = path.toStdString();
        edit ([this, file, path] (fpc::Kit& k) {
            auto& pad = k.pads[(std::size_t) selectedPad];
            auto& padFiles = files[(std::size_t) selectedPad];
            if (pad.layers.empty())
            {
                pad.layers.emplace_back();
                padFiles.emplace_back();
            }
            selectedLayer = std::clamp (selectedLayer, 0, (int) pad.layers.size() - 1);
            pad.layers[(std::size_t) selectedLayer].path = fpc::fromUtf8 (file);
            padFiles.resize (pad.layers.size());
            padFiles[(std::size_t) selectedLayer] = file;
            if (text (pad.name).isEmpty())
                pad.name = fpc::fromUtf8 (juce::File (path).getFileNameWithoutExtension().toStdString());
        });
    }

    // FL's note popup: one column per octave, the General MIDI drum names beside the notes that have one (its first
    // column — Last hit, Learn, Map notes for entire bank, Show GM note names — is not built yet)
    void noteMenu()
    {
        static const char* const gm[] = { "High Q", "Slap", "Scratch Push", "Scratch Pull", "Sticks", "Square Click", "Metronome Click", "Metronome Bell",
            "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap", "Electric Snare",
            "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat", "Low-Mid Tom", "Hi Mid Tom",
            "Crash Cymbal 1", "High Tom", "Ride Cymbal 1", "Chinese Cymbal", "Ride Bell", "Tambourine", "Splash Cymbal", "Cowbell",
            "Crash Cymbal 2", "Vibra-slap", "Ride Cymbal 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga",
            "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas", "Short Whistle", "Long Whistle", "Short Guiro",
            "Long Guiro", "Claves", "Hi Wood Block", "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle" };
        juce::PopupMenu m;
        const auto now = kit.pads[(std::size_t) selectedPad].note;
        for (int key = 0; key < 128; ++key)
        {
            if (key > 0 && key % 12 == 0)
                m.addColumnBreak();
            auto name = keyName (key);
            if (key >= 27 && key <= 81) // as FL lists them (D#2 High Q … A6 Open Triangle)
                name << " (" << gm[key - 27] << ")";
            m.addItem (key + 1, name, true, key == now);
        }
        juce::Component::SafePointer<FpcEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&padNote), [self] (int r) {
            if (self != nullptr && r > 0)
                self->edit ([self, r] (fpc::Kit& k) { k.pads[(std::size_t) self->selectedPad].note = r - 1; });
        });
    }

    // FL's RandomBtnMenu: how the layers that cover a velocity take turns
    void triggerMenu()
    {
        juce::PopupMenu m;
        const auto now = kit.pads[(std::size_t) selectedPad].trigger[0];
        m.addItem (4, tr ("Cycle"), true, now == 3);
        m.addItem (1, tr ("Random"), true, now == 0);
        m.addItem (2, tr ("Random (avoid previous)"), true, now == 1);
        juce::Component::SafePointer<FpcEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomTrigger), [self] (int r) {
            if (self != nullptr && r > 0)
                self->edit ([self, r] (fpc::Kit& k) { k.pads[(std::size_t) self->selectedPad].trigger[0] = r - 1; });
        });
    }

    void layoutLayers()
    {
        const auto row = fl (0, 0, 465, 29);
        layerList.setSize (row.getWidth(), std::max (1, row.getHeight() * (int) rows.size()));
        for (std::size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, row.getHeight() * (int) i, row.getWidth(), row.getHeight());
    }

    // the controls show the selected pad
    void refresh()
    {
        loading = true;
        const auto& pad = kit.pads[(std::size_t) selectedPad];
        selectedLayer = std::clamp (selectedLayer, 0, std::max (0, (int) pad.layers.size() - 1));
        currentPad.setText (tr ("Pad {0}/32", { juce::String (selectedPad + 1) }), juce::dontSendNotification);
        const auto name = text (pad.name);
        padName.setText (name.isNotEmpty() ? name : tr ("Pad {0}", { juce::String (selectedPad % 16 + 1) }), juce::dontSendNotification);
        padVolume.setValue (std::clamp (pad.volume, 0, 127), juce::dontSendNotification);
        padPan.setValue (pad.pan, juce::dontSendNotification);
        padTune.setValue (pad.tune, juce::dontSendNotification);
        mute.set (pad.muted());
        solo.set (pad.soloed());
        scaleVolume.set ((pad.flags & 4) != 0);
        randomTrigger.set (pad.oneLayerAtATime());
        padNote.setText (keyName (pad.note), juce::dontSendNotification);
        cutGroup.set (pad.cut);
        cutBy.set (pad.cutBy);
        output.set (pad.output);
        bankA.set (bank() == 0);
        bankB.set (bank() == 1);
        volumeTab.setToggleState (envelopeTab == 0, juce::dontSendNotification);
        panTab.setToggleState (envelopeTab == 1, juce::dontSendNotification);

        while (rows.size() > pad.layers.size())
            rows.pop_back();
        while (rows.size() < pad.layers.size())
        {
            auto row = std::make_unique<LayerRow>();
            const auto index = (int) rows.size();
            auto* r = row.get();
            auto change = [this, index] (std::function<void (fpc::Layer&)> set) {
                if (! loading)
                    edit ([this, index, set] (fpc::Kit& k) {
                        auto& layers = k.pads[(std::size_t) selectedPad].layers;
                        if (index < (int) layers.size())
                            set (layers[(std::size_t) index]);
                    });
            };
            r->number = index + 1;
            r->onSelect = [this, index] {
                selectedLayer = index;
                refresh();
            };
            r->volume.onValueChange = [r, change] { change ([v = juce::roundToInt (r->volume.getValue())] (fpc::Layer& l) { l.volume = v; }); };
            r->pan.onValueChange = [r, change] { change ([v = juce::roundToInt (r->pan.getValue())] (fpc::Layer& l) { l.pan = v; }); };
            r->tune.onValueChange = [r, change] { change ([v = juce::roundToInt (r->tune.getValue())] (fpc::Layer& l) { l.tune = v; }); };
            r->range.onValueChange = [r, change] {
                change ([lo = juce::roundToInt (r->range.getMinValue()), hi = juce::roundToInt (r->range.getMaxValue())] (fpc::Layer& l) {
                    l.velocityLow = lo;
                    l.velocityHigh = hi;
                });
            };
            r->wave.sample = [this, index] { return layerSample (selectedPad, index); };
            r->wave.onClick = [this, index] {
                selectedLayer = index;
                refresh();
                preview (selectedPad, index);
            };
            layerList.addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            const auto& layer = pad.layers[i];
            auto& r = *rows[i];
            r.selected = (int) i == selectedLayer;
            r.volume.setValue (layer.volume, juce::dontSendNotification);
            r.pan.setValue (layer.pan, juce::dontSendNotification);
            r.tune.setValue (layer.tune, juce::dontSendNotification);
            r.range.setMinAndMaxValues (std::clamp (layer.velocityLow, 0, 127), std::clamp (std::max (layer.velocityLow, layer.velocityHigh), 0, 127),
                                        juce::dontSendNotification);
            r.repaint();
        }
        layoutLayers();

        const auto file = selectedLayer < (int) files[(std::size_t) selectedPad].size() ? files[(std::size_t) selectedPad][(std::size_t) selectedLayer] : std::string();
        const auto saved = selectedLayer < (int) pad.layers.size() ? text (pad.layers[(std::size_t) selectedLayer].path) : juce::String();
        sampleName.setText (! file.empty() ? fileName (file) : saved.isNotEmpty() ? tr ("{0} (not found)", { fileName (saved.toStdString()) }) : juce::String(),
                            juce::dontSendNotification);
        loading = false;
        repaint();
    }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel;
    fpc::Kit kit;
    std::vector<std::vector<std::string>> files; // per pad, per layer: the file that plays ("" = none / not found)
    std::vector<std::uint8_t> lastState;
    std::vector<SampleZone> lastZones;
    int selectedPad = 0, selectedLayer = 0, envelopeTab = 0;
    bool loading = false;

    juce::TextButton previousPad, nextPad, createLayer, deleteLayer, layerUp, layerDown, spreadEven, loadButton, volumeTab, panTab;
    juce::Label currentPad, padName, padNote, sampleName;
    juce::Slider padVolume, padPan, padTune;
    Led mute, solo, scaleVolume, randomTrigger, bankA, bankB;
    DigiWheel cutGroup, cutBy, output;
    PadGrid grid;
    juce::Viewport layerView;
    juce::Component layerList;
    std::vector<std::unique_ptr<LayerRow>> rows;
    WaveView scope;
    EnvView envelope;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

std::unique_ptr<juce::Component> makeFpcEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<FpcEditor> (engine, std::move (locate), channel);
}
} // namespace lynx
