#include "ui/FruitySlicerEditor.h"

#include <optional>
#include <utility>
#include "ui/FileChoosers.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/FruitySlicer.h"
#include "core/Instruments.h"
#include "core/Slicer.h"
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
using fslicer::State;

constexpr int scale = 2; // FL's form (377 × 330) at twice its size

juce::Rectangle<int> fl (int x, int y, int w, int h)
{
    return { x * scale, y * scale, w * scale, h * scale };
}

juce::String keyName (int key)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((key % 12) + 12) % 12]) + juce::String (key / 12);
}

juce::String fileName (const std::string& path)
{
    return juce::String::fromUTF8 (path.c_str()).fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
}

// ---------------------------------------------------------------------------------------------------------------
// FL's WAVScope: the (processed) sample with its slices; click selects a slice and plays it, drag moves its start,
// right click: Remove slice / Split slice
class Scope final : public juce::Component
{
public:
    std::function<const State*()> state;
    std::function<std::shared_ptr<const SampleData>()> sample;
    std::function<std::vector<std::int32_t>()> starts; // where the slices start in the shown (processed) sample
    std::function<int()> selected;
    std::function<void (int)> select;
    std::function<void (int, std::int32_t)> moveSlice;   // slice, new start
    std::function<void (int, std::int32_t)> menu;        // slice under the mouse, frame under the mouse

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto* s = state ? state() : nullptr;
        const auto data = sample ? sample() : nullptr;
        const auto r = getLocalBounds().toFloat();
        if (data == nullptr || data->frames() == 0 || s == nullptr)
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::body));
            g.drawText (tr ("Load a sample (Sample menu) or drop one here"), getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto frames = (double) data->frames();
        const auto sel = selected ? selected() : -1;
        const auto at = starts();
        if (sel >= 0 && sel < (int) at.size())
        {
            const auto end = sel + 1 < (int) at.size() ? (double) at[(std::size_t) sel + 1] : frames;
            const auto x0 = xOf (at[(std::size_t) sel], frames), x1 = xOf (end, frames);
            g.setColour (theme::colour::accent.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (x0, r.getY(), std::max (1.0f, x1 - x0), r.getHeight()));
        }
        // the wave: min / max per pixel column
        const auto* left = data->leftData();
        g.setColour (theme::colour::accent.brighter (0.2f));
        const auto mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        for (int x = 0; x < getWidth(); ++x)
        {
            const auto a = (std::int64_t) (frames * x / getWidth()), b = std::max (a + 1, (std::int64_t) (frames * (x + 1) / getWidth()));
            float lo = 0.0f, hi = 0.0f;
            for (auto i = a; i < b && i < (std::int64_t) frames; ++i)
            {
                lo = std::min (lo, left[i]);
                hi = std::max (hi, left[i]);
            }
            g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
        }
        // the slices: a line at each start, with its number
        g.setFont (theme::font (theme::type::caption));
        for (std::size_t i = 0; i < s->slices.size(); ++i)
        {
            const auto x = xOf (i < at.size() ? at[i] : 0, frames);
            g.setColour ((int) i == sel ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            g.drawText (juce::String ((int) i + 1) + (s->slices[i].reversed() ? " R" : ""), juce::Rectangle<float> (x + 2.0f, r.getY() + 1.0f, 40.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* s = state ? state() : nullptr;
        const auto data = sample ? sample() : nullptr;
        if (s == nullptr || data == nullptr || data->frames() == 0)
            return;
        const auto frames = (double) data->frames();
        const auto frame = (std::int32_t) std::clamp (frames * e.x / std::max (1, getWidth()), 0.0, frames - 1.0);
        dragging = -1;
        // a start line under the mouse (not the first): drag it
        const auto at = starts();
        for (std::size_t i = 1; i < at.size(); ++i)
            if (std::abs (xOf (at[i], frames) - (float) e.x) < 4.0f)
                dragging = (int) i;
        const auto hit = sliceAt (at, frame);
        if (e.mods.isPopupMenu())
        {
            if (menu)
                menu (hit, frame);
            return;
        }
        if (dragging < 0 && select)
            select (hit);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto data = sample ? sample() : nullptr;
        if (dragging < 0 || data == nullptr || ! moveSlice)
            return;
        const auto frames = (double) data->frames();
        moveSlice (dragging, (std::int32_t) std::clamp (frames * e.x / std::max (1, getWidth()), 0.0, frames - 1.0));
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    static int sliceAt (const std::vector<std::int32_t>& at, std::int32_t frame)
    {
        int hit = 0;
        for (std::size_t i = 0; i < at.size(); ++i)
            if (at[i] <= frame)
                hit = (int) i;
        return hit;
    }

private:
    float xOf (double position, double frames) const { return (float) (position / frames * getWidth()); }
    int dragging = -1;
};

// FL's SliceScope: the selected slice's wave
class SliceView final : public juce::Component
{
public:
    std::function<std::shared_ptr<const SampleData>()> sample;
    std::function<std::pair<std::int64_t, std::int64_t>()> range; // first frame, frame count

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto data = sample ? sample() : nullptr;
        if (data == nullptr || ! range)
            return;
        const auto [from, count] = range();
        if (count <= 0 || from < 0 || from + count > (std::int64_t) data->frames())
            return;
        const auto* x = data->leftData() + from;
        const auto mid = getHeight() * 0.5f, half = getHeight() * 0.45f;
        g.setColour (theme::colour::accent.brighter (0.2f));
        for (int px = 0; px < getWidth(); ++px)
        {
            const auto a = count * px / getWidth(), b = std::max (a + 1, count * (px + 1) / getWidth());
            float lo = 0.0f, hi = 0.0f;
            for (auto i = a; i < b; ++i)
                lo = std::min (lo, x[i]), hi = std::max (hi, x[i]);
            g.drawVerticalLine (px, mid - hi * half, mid - lo * half + 1.0f);
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
class FruitySlicerEditor final : public juce::Component,
                                 public juce::FileDragAndDropTarget,
                                 private juce::ChangeListener
{
    struct Wheel
    {
        juce::Slider* slider;
        std::function<int (const State&)> get;
        std::function<void (State&, int)> set;
        bool tempo;
    };

public:
    FruitySlicerEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        juce::ignoreUnused (channel);
        auto button = [this] (juce::TextButton& b, const juce::String& text, const juce::String& hint, std::function<void()> click) {
            b.setButtonText (text);
            ui::setHint (b, hint, true);
            b.onClick = std::move (click);
            addAndMakeVisible (b);
        };
        button (openButton, tr ("Open"), tr ("Open a sample to slice (or drop one here)"), [this] { chooseSample(); });
        button (sampleButton, tr ("Sample"), tr ("Sample"), [this] { sampleMenu(); });
        button (slicingButton, tr ("Slicing"), tr ("Slicing"), [this] { slicingMenu(); });
        button (dumpButton, tr ("Dump"), tr ("Dump beat to piano roll"), [this] { dumpMenu(); });

        auto check = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& hint, std::function<void (State&, bool)> set) {
            b.setButtonText (text);
            ui::setHint (b, hint);
            b.onClick = [this, &b, set] { edit ([&b, set] (State& s) { set (s, b.getToggleState()); }); };
            addAndMakeVisible (b);
        };
        check (autoDump, "AUTO-DUMP", tr ("Dump beat to piano roll right after loading"), [] (State& s, bool on) { s.autoDump = on ? 1 : 0; });
        check (autoFit, "AUTO-FIT", tr ("Fit beat to project tempo right after loading"), [] (State& s, bool on) { s.autoFit = on ? 1 : 0; });
        check (declick, "DECLICK", tr ("Click removal"), [] (State& s, bool on) { s.declick = on ? 1 : 0; });
        check (reverse, "REV", tr ("Reverse selected slice"), [this] (State& s, bool on) {
            if (selected >= 0 && selected < (int) s.slices.size())
                s.slices[(std::size_t) selected].flags = (std::uint8_t) ((s.slices[(std::size_t) selected].flags & ~1u) | (on ? 1u : 0u));
        });
        check (animate, "ANIMATE", tr ("Animate display"), [] (State& s, bool on) { s.animate = on ? 1 : 0; });
        check (playToEnd, "PLAY TO END", tr ("Play from slice to end of sample"), [] (State& s, bool on) { s.playToEnd = on ? 1 : 0; });

        nameLabel.setFont (theme::font (theme::type::body));
        ui::setHint (nameLabel, tr ("Sample name"));
        addAndMakeVisible (nameLabel);
        sliceName.setFont (theme::font (theme::type::caption));
        ui::setHint (sliceName, tr ("Selected slice name (double click to rename)"));
        sliceName.addMouseListener (this, false);
        addAndMakeVisible (sliceName);
        sliceKey.setFont (theme::font (theme::type::caption));
        sliceKey.setJustificationType (juce::Justification::centredRight);
        ui::setHint (sliceKey, tr ("Key to trigger selected slice"), true);
        sliceKey.addMouseListener (this, false);
        addAndMakeVisible (sliceKey);

        auto wheel = [this] (juce::Slider& w, const juce::String& label, juce::Label& caption, const juce::String& hint, double lo, double hi,
                             std::function<int (const State&)> get, std::function<void (State&, int)> set, bool tempo) {
            w.setSliderStyle (juce::Slider::LinearVertical);
            w.setRange (lo, hi, 1.0);
            w.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            w.setPopupDisplayEnabled (true, true, this);
            ui::setHint (w, hint, true);
            w.onValueChange = [this, &w, set] {
                if (! loading)
                    edit ([&w, set] (State& s) { set (s, (int) w.getValue()); });
            };
            w.addMouseListener (this, false);
            wheels.push_back ({ &w, std::move (get), set, tempo });
            addAndMakeVisible (w);
            caption.setText (label, juce::dontSendNotification);
            caption.setFont (theme::font (theme::type::caption));
            caption.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (caption);
        };
        wheel (pitch, "PS", psLabel, tr ("Pitch shift"), -1200, 1200, [] (const State& s) { return s.pitchCents; }, [] (State& s, int v) { s.pitchCents = v; }, false);
        wheel (stretch, "TS", tsLabel, tr ("Fit (stretch slices) length"), -20000, 20000, [] (const State& s) { return s.stretch; }, [] (State& s, int v) { s.stretch = v; }, true);
        wheel (fadeIn, "ATT", attLabel, tr ("Slices fade in"), 0, 100, [] (const State& s) { return s.fadeInMs; }, [] (State& s, int v) { s.fadeInMs = v; }, false);
        wheel (fadeOut, "DEC", decLabel, tr ("Slices fade out"), 0, 100, [] (const State& s) { return s.fadeOutMs; }, [] (State& s, int v) { s.fadeOutMs = v; }, false);
        pitch.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " " + tr ("cents"); };
        stretch.textFromValueFunction = [] (double v) { return juce::String (std::pow (2.0, v / 10000.0) * 100.0, 1) + "%"; };
        fadeIn.textFromValueFunction = fadeOut.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " ms"; };

        for (auto* k : { &autoLow, &autoHigh })
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setRange (0.0, 200.0, 1.0);
            k->setValue (100.0, juce::dontSendNotification);
            k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->setPopupDisplayEnabled (true, true, this);
            addAndMakeVisible (*k);
        }
        ui::setHint (autoLow, tr ("Auto-slicing low threshold"));
        ui::setHint (autoHigh, tr ("Auto-slicing high threshold"));
        for (auto* lab : { &lowLabel, &highLabel, &autoLabel, &brandLabel })
        {
            lab->setFont (theme::font (theme::type::caption));
            lab->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*lab);
        }
        lowLabel.setText ("LOW", juce::dontSendNotification);
        highLabel.setText ("HIGH", juce::dontSendNotification);
        autoLabel.setText ("AUTO", juce::dontSendNotification);
        brandLabel.setText ("Fruity Slicer", juce::dontSendNotification);
        brandLabel.setJustificationType (juce::Justification::centredLeft);

        scope.state = [this] { return &state; };
        scope.sample = [this] { return processed; };
        scope.starts = [this] { return original != nullptr ? fslicer::playedStarts (state, (std::int64_t) original->frames()) : std::vector<std::int32_t>(); };
        scope.selected = [this] { return selected; };
        scope.select = [this] (int i) {
            selected = i;
            refresh();
            preview (i);
        };
        scope.moveSlice = [this] (int i, std::int32_t at) {
            if (i <= 0 || i >= (int) state.slices.size())
                return;
            if (state.stretch != 0 && state.method < 2)
                at = (std::int32_t) std::nearbyint ((double) at / (double) fslicer::stretchFactor (state.stretch));
            const auto lo = state.slices[(std::size_t) i - 1].position + 1;
            const auto hi = i + 1 < (int) state.slices.size() ? state.slices[(std::size_t) i + 1].position - 1 : at;
            state.slices[(std::size_t) i].position = std::clamp (at, lo, std::max (lo, hi));
            commit();
        };
        scope.menu = [this] (int slice, std::int32_t frame) { scopeMenu (slice, frame); };
        ui::setHint (scope, tr ("Click a slice to select and play it; drag a slice start to move it; right click: remove or split"), true);
        addAndMakeVisible (scope);

        // BPM / BEATS: the sample's original tempo and length (FL's TempoSelect 10…999, LengthSelect 1…999)
        auto field = [this] (juce::Slider& s, juce::Label& caption, const juce::String& text, const juce::String& hint, double lo, double hi,
                             std::function<void (State&, float)> set) {
            s.setSliderStyle (juce::Slider::IncDecButtons);
            s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
            s.setRange (lo, hi, 1.0);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 30 * scale, 16 * scale);
            ui::setHint (s, hint);
            s.onValueChange = [this, &s, set] {
                if (! loading)
                    edit ([&s, set] (State& st) { set (st, (float) s.getValue()); });
            };
            addAndMakeVisible (s);
            caption.setText (text, juce::dontSendNotification);
            caption.setFont (theme::font (theme::type::caption));
            addAndMakeVisible (caption);
        };
        field (tempo, tempoLabel, "BPM", tr ("Original sample tempo"), 10.0, 999.0, [] (State& st, float v) { st.bpm = v; });
        field (beats, beatsLabel, "BEATS", tr ("Original sample length"), 1.0, 999.0, [] (State& st, float v) { st.lengthBeats = v; });

        sliceView.sample = [this] { return processed; };
        sliceView.range = [this] () -> std::pair<std::int64_t, std::int64_t> {
            if (processed == nullptr || original == nullptr || state.slices.empty())
                return { 0, 0 };
            const auto at = fslicer::playedStarts (state, (std::int64_t) original->frames());
            const auto total = (std::int64_t) processed->frames();
            const auto i = (std::size_t) std::clamp (selected, 0, (int) at.size() - 1);
            const auto from = std::clamp<std::int64_t> (at[i], 0, total);
            const auto end = i + 1 < at.size() ? std::clamp<std::int64_t> (at[i + 1], from, total) : total;
            return { from, end - from };
        };
        addAndMakeVisible (sliceView);

        engine.addChangeListener (this);
        reload();
        setSize (377 * scale, 330 * scale);
    }

    ~FruitySlicerEditor() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg1);
        g.fillRect (fl (0, 0, 377, 22));          // TopBevel
        g.fillRect (fl (0, 266, 377, 64));        // BottomPanel
        g.setColour (theme::colour::bg3);
        g.fillRect (fl (0, 68, 211, 76));         // SliceScopeHeaderBevel
    }

    void resized() override
    {
        nameLabel.setBounds (fl (5, 3, 245, 17));
        tempo.setBounds (fl (253, 4, 30, 16));
        tempoLabel.setBounds (fl (284, 7, 24, 13));
        beats.setBounds (fl (309, 4, 30, 16));
        beatsLabel.setBounds (fl (340, 7, 35, 13));
        sliceView.setBounds (fl (0, 82, 211, 62));
        openButton.setBounds (fl (5, 27, 50, 21));
        sampleButton.setBounds (fl (57, 27, 50, 21));
        slicingButton.setBounds (fl (109, 27, 50, 21));
        dumpButton.setBounds (fl (161, 27, 50, 21));
        autoDump.setBounds (fl (1, 50, 83, 15));
        autoFit.setBounds (fl (83, 50, 69, 15));
        declick.setBounds (fl (154, 50, 67, 15));
        reverse.setBounds (fl (1, 68, 52, 15));
        sliceName.setBounds (fl (58, 70, 123, 11));
        sliceKey.setBounds (fl (182, 70, 26, 11));
        pitch.setBounds (fl (233, 25, 22, 107));
        stretch.setBounds (fl (255, 25, 22, 107));
        fadeIn.setBounds (fl (319, 25, 22, 107));
        fadeOut.setBounds (fl (341, 25, 22, 107));
        psLabel.setBounds (fl (229, 136, 30, 10));
        tsLabel.setBounds (fl (253, 136, 28, 10));
        attLabel.setBounds (fl (314, 136, 32, 10));
        decLabel.setBounds (fl (336, 136, 32, 10));
        scope.setBounds (fl (0, 151, 377, 113));
        animate.setBounds (fl (1, 266 + 4, 69, 15));
        playToEnd.setBounds (fl (71, 266 + 4, 92, 15));
        autoLow.setBounds (fl (279, 266 + 9, 40, 40));
        autoHigh.setBounds (fl (331, 266 + 9, 40, 40));
        lowLabel.setBounds (fl (281, 266 + 49, 32, 15));
        highLabel.setBounds (fl (335, 266 + 49, 32, 15));
        autoLabel.setBounds (fl (311, 266 - 1, 32, 15));
        brandLabel.setBounds (fl (6, 266 + 38, 160, 20));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &sliceKey)
            keyMenu();
        for (auto& w : wheels)
            if (e.eventComponent == w.slider && e.mods.isPopupMenu())
                wheelMenu (w);
    }

    // FL's WheelMenu: Reset, and on TS Autodetect (adapt to the current tempo)
    void wheelMenu (const Wheel& w)
    {
        juce::PopupMenu m;
        m.addItem (1, tr ("Reset"));
        if (w.tempo)
            m.addItem (2, tr ("Autodetect"), original != nullptr);
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        auto set = w.set;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (w.slider), [self, set] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 1)
                self->edit ([set] (State& s) { set (s, 0); });
            else if (self->original != nullptr)
            {
                const auto seconds = (double) self->original->frames() / self->original->sampleRate;
                const auto v = self->autoFitValue (self->state, seconds);
                self->edit ([set, v] (State& s) { set (s, v); });
            }
        });
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &sliceName)
            rename();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && SampleLibrary::isSampleFile (juce::File (files[0]));
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (! files.isEmpty())
            loadSample (files[0]);
    }

    // round 190gk: the Sampler waveform menu's "Open in new Fruity Slicer channel" (FL's SBONewMenuClick 0x11f8330:
    // the plugin's template channel, then the sample loaded into it as the plugin's own Load does)
    void loadSampleNow (const juce::String& path) { loadSample (path); }

private:
    const ChannelDef* def() const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }

    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        lastState = d->pluginState;
        samplePath = d->samplePath;
        if (auto s = fslicer::parse (d->pluginState))
            state = *s;
        else
            state = fslicer::fresh (samplePath, 44100);
        original = samplePath.empty() ? nullptr : engine.getSampleLibrary().load (juce::String::fromUTF8 (samplePath.c_str()));
        reprocess();
        refresh();
    }

    void reprocess()
    {
        processed = original != nullptr ? std::shared_ptr<const SampleData> (fslicer::prepare (*original, state)) : nullptr;
    }

    void refresh()
    {
        loading = true;
        selected = std::clamp (selected, 0, std::max (0, (int) state.slices.size() - 1));
        nameLabel.setText (samplePath.empty() ? tr ("No sample") : fileName (samplePath).upToLastOccurrenceOf (".", false, false), juce::dontSendNotification); // FL: the name without its extension
        autoDump.setToggleState (state.autoDump != 0, juce::dontSendNotification);
        autoFit.setToggleState (state.autoFit != 0, juce::dontSendNotification);
        declick.setToggleState (state.declick != 0, juce::dontSendNotification);
        animate.setToggleState (state.animate != 0, juce::dontSendNotification);
        playToEnd.setToggleState (state.playToEnd != 0, juce::dontSendNotification);
        const auto has = ! state.slices.empty();
        reverse.setToggleState (has && state.slices[(std::size_t) selected].reversed(), juce::dontSendNotification);
        sliceName.setText (has ? (state.slices[(std::size_t) selected].name.empty() ? tr ("Slice #{0}", { juce::String (selected + 1) })
                                                                                  : juce::String::fromUTF8 (state.slices[(std::size_t) selected].name.c_str()))
                               : juce::String(),
                           juce::dontSendNotification);
        sliceKey.setText (has ? keyName (fslicer::sliceKey (state, selected)) : juce::String(), juce::dontSendNotification);
        for (auto& w : wheels)
            w.slider->setValue (w.get (state), juce::dontSendNotification);
        tempo.setValue (std::nearbyint (state.bpm), juce::dontSendNotification);
        beats.setValue (std::nearbyint (state.lengthBeats), juce::dontSendNotification);
        loading = false;
        scope.repaint();
        sliceView.repaint();
    }

    void edit (const std::function<void (State&)>& change, bool undoable = true)
    {
        change (state);
        commit (undoable);
    }

    void commit (bool undoable = true)
    {
        const auto bytes = fslicer::write (state);
        lastState = bytes;
        engine.editSong ([l = locate, bytes] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
                d->pluginState = bytes;
        }, undoable);
        reprocess();
        refresh();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d != nullptr && (d->pluginState != lastState || d->samplePath != samplePath))
            reload();
    }

    void preview (int slice)
    {
        if (processed == nullptr || slice < 0 || slice >= (int) state.slices.size())
            return;
        const auto at = fslicer::playedStarts (state, original != nullptr ? (std::int64_t) original->frames() : 0);
        if (slice >= (int) at.size())
            return;
        const auto total = (std::int64_t) processed->frames();
        const auto from = (std::size_t) std::clamp<std::int64_t> (at[(std::size_t) slice], 0, total);
        const auto end = state.playToEnd != 0 || slice + 1 >= (int) at.size() ? total : std::clamp<std::int64_t> (at[(std::size_t) slice + 1], (std::int64_t) from, total);
        const auto count = (std::size_t) (end - (std::int64_t) from);
        auto part = std::make_shared<SampleData>();
        part->sampleRate = processed->sampleRate;
        part->left.assign (processed->leftData() + from, processed->leftData() + from + count);
        if (processed->isStereo())
            part->right.assign (processed->rightData() + from, processed->rightData() + from + count);
        engine.previewSample (part);
    }

    // FL loads a sample with its own slicing when it has one, else auto-slices it (Medium)
    void loadSample (const juce::String& path)
    {
        const auto sample = engine.getSampleLibrary().load (path);
        if (sample == nullptr || sample->frames() == 0)
            return;
        const auto p = path.toStdString();
        auto s = state;
        s.path = p;
        s.sampleRate = (int) std::lround (sample->sampleRate);
        const auto seconds = (double) sample->frames() / sample->sampleRate;
        // the length in beats: the file's own (ACID) when it has one, else the power of two that puts it at 90…180 BPM
        if (sample->acidBeats > 0.0f)
            s.lengthBeats = sample->acidBeats;
        else
        {
            double beats = 1.0;
            while (beats * 60.0 / seconds < 90.0 && beats < 256.0)
                beats *= 2.0;
            s.lengthBeats = (float) beats;
        }
        s.bpm = (float) (s.lengthBeats * 60.0 / seconds);
        s.slices.clear();
        std::vector<std::int32_t> starts;
        if (! sample->markerStarts.empty())
            for (const auto m : sample->markerStarts)
                starts.push_back ((std::int32_t) m);
        else
            for (const auto f : detectSlices (*sample, 0.5f))
                starts.push_back ((std::int32_t) std::llround ((double) f * (double) sample->frames()));
        setStarts (s, starts);
        if (s.autoFit != 0)
            s.stretch = autoFitValue (s, seconds);
        state = s;
        samplePath = p;
        original = sample;
        const auto bytes = fslicer::write (state);
        lastState = bytes;
        engine.editSong ([l = locate, bytes, p] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                d->samplePath = p;
            }
        });
        reprocess();
        selected = 0;
        refresh();
        if (state.autoDump != 0)
            dump (0);
    }

    // TS › Autodetect: the stretch that makes the sample last its beats at the project's tempo
    int autoFitValue (const State& s, double seconds) const
    {
        const auto wanted = (double) s.lengthBeats * 60.0 / std::max (1.0, engine.getSong().bpm);
        return std::clamp ((int) std::lround (10000.0 * std::log2 (wanted / seconds)), -20000, 20000);
    }

    static void setStarts (State& s, std::vector<std::int32_t> starts)
    {
        std::sort (starts.begin(), starts.end());
        starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
        if (starts.empty() || starts.front() != 0)
            starts.insert (starts.begin(), 0);
        s.slices.clear();
        for (const auto p : starts)
        {
            fslicer::Slice slice;
            slice.position = p;
            s.slices.push_back (slice);
        }
    }

    void chooseSample()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Load sample..."), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().existsAsFile())
                self->loadSample (fc.getResult().getFullPathName());
        });
    }

    void saveSample (bool processedOne)
    {
        const auto data = processedOne ? processed : original;
        if (data == nullptr)
            return;
        chooser = lynx::ui::makeFileChooser (processedOne ? tr ("Save processed sample...") : tr ("Save original sample..."), juce::File(), "*.wav");
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [self, data] (const juce::FileChooser& fc) {
            if (self == nullptr || fc.getResult() == juce::File())
                return;
            auto file = fc.getResult().withFileExtension ("wav");
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
            juce::WavAudioFormat wav;
            const auto channels = data->isStereo() ? 2 : 1;
            std::unique_ptr<juce::AudioFormatWriter> writer (
                stream != nullptr ? wav.createWriterFor (stream.get(), data->sampleRate, (unsigned int) channels, 24, {}, 0) : nullptr);
            if (writer == nullptr)
                return;
            stream.release();
            const float* chans[2] { data->leftData(), data->isStereo() ? data->rightData() : data->leftData() };
            writer->writeFromFloatArrays (chans, channels, (int) data->frames());
        });
    }

    void sampleMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, tr ("Load sample..."));
        m.addSeparator();
        m.addItem (2, tr ("Save original sample..."), original != nullptr);
        m.addItem (3, tr ("Save processed sample..."), processed != nullptr);
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sampleButton), [self] (int r) {
            if (self == nullptr)
                return;
            if (r == 1)
                self->chooseSample();
            else if (r == 2 || r == 3)
                self->saveSample (r == 3);
        });
    }

    // FL's SliceMenu: Slice (built-in, auto, beat grid, none, zero-cross), Edit, Stretching method
    void slicingMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (tr ("Slice"));
        m.addItem (1, tr ("Use sample built-in slicing"), original != nullptr && ! original->markerStarts.empty());
        m.addSeparator();
        m.addItem (20, tr ("Dull auto-slicing"), original != nullptr);
        m.addItem (21, tr ("Medium auto-slicing"), original != nullptr);
        m.addItem (22, tr ("Sharp auto-slicing"), original != nullptr);
        m.addSeparator();
        m.addItem (106, tr ("1/6 beat"), original != nullptr);
        m.addItem (104, tr ("1/4 beat"), original != nullptr);
        m.addItem (103, tr ("1/3 beat"), original != nullptr);
        m.addItem (102, tr ("1/2 beat"), original != nullptr);
        m.addItem (101, tr ("Beat"), original != nullptr);
        m.addItem (2, tr ("No slicing"), original != nullptr);
        m.addSeparator();
        m.addItem (3, tr ("Zero-cross check slices"), original != nullptr);
        m.addSectionHeader (tr ("Stretching method"));
        const char* const methods[] = { TR_NOOP ("Fill gaps"), TR_NOOP ("Alternate fill gaps"), TR_NOOP ("Pro default"), TR_NOOP ("Pro transient"),
                                        TR_NOOP ("Transient"), TR_NOOP ("Tonal"), TR_NOOP ("Monophonic"), TR_NOOP ("Speech") };
        for (int i = 0; i < 8; ++i)
            m.addItem (200 + i, tr (methods[i]), true, state.method == i);
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slicingButton), [self] (int r) {
            if (self != nullptr && r != 0)
                self->slicing (r);
        });
    }

    void slicing (int tag)
    {
        if (tag >= 200)
        {
            edit ([tag] (State& s) { s.method = tag - 200; });
            return;
        }
        if (original == nullptr)
            return;
        const auto frames = (std::int64_t) original->frames();
        std::vector<std::int32_t> starts;
        if (tag == 1)
            for (const auto m : original->markerStarts)
                starts.push_back ((std::int32_t) m);
        else if (tag >= 20 && tag <= 22)
        {
            // LYNX's transient detector (FL's own detector is not reproduced): dull / medium / sharp sensitivity, moved by
            // the LOW / HIGH wheels
            const auto base = tag == 20 ? 0.3f : tag == 21 ? 0.5f : 0.7f;
            const auto nudge = (float) ((autoLow.getValue() + autoHigh.getValue()) / 400.0 - 0.5) * 0.4f;
            for (const auto f : detectSlices (*original, std::clamp (base + nudge, 0.05f, 0.95f)))
                starts.push_back ((std::int32_t) std::llround ((double) f * (double) frames));
        }
        else if (tag >= 101 && tag <= 106)
        {
            const double beats = tag == 101 ? 1.0 : tag == 102 ? 0.5 : tag == 103 ? 1.0 / 3.0 : tag == 104 ? 0.25 : 1.0 / 6.0;
            starts = fslicer::gridSlices (state, frames, beats);
        }
        else if (tag == 2)
            starts = { 0 };
        else if (tag == 3)
        {
            for (const auto& s : state.slices)
                starts.push_back (s.position);
            starts = fslicer::zeroCrossSlices (starts, *original);
        }
        edit ([&starts] (State& s) { setStarts (s, starts); });
    }

    void dumpMenu()
    {
        juce::PopupMenu m;
        struct Item { int tag; const char* name; };
        const Item items[] = { { 0, TR_NOOP ("Normal") }, { 1, TR_NOOP ("Reverse") }, { 4, TR_NOOP ("Random") }, { 5, TR_NOOP ("Flatten (groove)") }, { -1, nullptr },
                               { 10, TR_NOOP ("Shift up") }, { 11, TR_NOOP ("Shift down") }, { -1, nullptr }, { 2, TR_NOOP ("Quantize") }, { 3, TR_NOOP ("Swing") }, { -1, nullptr },
                               { 6, TR_NOOP ("Accentuate beat") }, { 7, TR_NOOP ("Pitch up beat") }, { 8, TR_NOOP ("Widen stereo") }, { 12, TR_NOOP ("Crazy") }, { -1, nullptr },
                               { 13, TR_NOOP ("Stutter (half)") }, { 14, TR_NOOP ("Stutter (fourth)") }, { -1, nullptr }, { 9, TR_NOOP ("Original length") } };
        for (const auto& i : items)
            if (i.tag < 0)
                m.addSeparator();
            else
                m.addItem (i.tag + 1, tr (i.name), ! state.slices.empty() && original != nullptr);
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dumpButton), [self] (int r) {
            if (self != nullptr && r > 0)
                self->dump (r - 1);
        });
    }

    // FL's dump: the channel's notes in the current pattern become the slices (FL's project PPQ 96, scaled to LYNX's)
    void dump (int tag)
    {
        if (original == nullptr || channel < 0)
            return;
        std::mt19937 rng ((unsigned) juce::Time::getMillisecondCounter());
        const auto notes = fslicer::dumpNotes (state, tag, (std::int64_t) original->frames(), original->sampleRate, engine.getSong().bpm, 96,
                                               [&rng] (int n) { return n <= 0 ? 0 : (int) (rng() % (unsigned) n); });
        const auto ch = channel;
        engine.editSong ([notes, ch] (Song& s) {
            if (s.patterns.empty())
                s.patterns.emplace_back();
            auto& p = s.patterns[(std::size_t) std::clamp (s.currentPattern, 0, (int) s.patterns.size() - 1)];
            p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [ch] (const Note& n) { return n.channel == ch; }), p.notes.end());
            for (const auto& d : notes)
            {
                Note n;
                n.channel = ch;
                n.start = d.start * (ppq / 96);
                n.length = std::max<std::int64_t> (1, d.length * (ppq / 96));
                n.key = d.key + d.pitchCents / 100; // FL picks the slice from the note's whole pitch (TriggerVoice)
                n.velocity = d.velocity;
                n.pan = (float) d.pan / 64.0f;
                p.notes.push_back (n);
            }
            p.sortNotes();
        });
    }

    void scopeMenu (int slice, std::int32_t frame)
    {
        juce::PopupMenu m;
        m.addItem (1, tr ("Remove slice"), slice > 0 || state.slices.size() > 1);
        m.addItem (2, tr ("Split slice"));
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, slice, frame] (int r) {
            if (self == nullptr || r == 0)
                return;
            self->edit ([slice, frame, r] (State& s) {
                if (r == 1 && slice >= 0 && slice < (int) s.slices.size() && s.slices.size() > 1)
                {
                    s.slices.erase (s.slices.begin() + slice);
                    if (! s.slices.empty())
                        s.slices.front().position = 0;
                }
                else if (r == 2)
                {
                    fslicer::Slice added;
                    added.position = s.stretch != 0 && s.method < 2 ? (std::int32_t) std::nearbyint ((double) frame / (double) fslicer::stretchFactor (s.stretch)) : frame;
                    s.slices.insert (s.slices.begin() + slice + 1, added);
                }
            });
        });
    }

    void keyMenu()
    {
        if (state.slices.empty())
            return;
        juce::PopupMenu m;
        m.addItem (1, tr ("Default"), true, state.slices[(std::size_t) selected].key < 0);
        m.addItem (2, tr ("Rename..."));
        m.addSeparator();
        for (int k = 0; k < 125; ++k)
            if (k >= 36 && k < 108)
                m.addItem (1000 + k, keyName (k), true, fslicer::sliceKey (state, selected) == k);
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sliceKey), [self] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 2)
            {
                self->rename();
                return;
            }
            const auto key = r == 1 ? -1 : r - 1000;
            const auto i = self->selected;
            self->edit ([i, key] (State& s) {
                if (i >= 0 && i < (int) s.slices.size())
                    s.slices[(std::size_t) i].key = key;
            });
        });
    }

    void rename()
    {
        if (state.slices.empty())
            return;
        auto* w = new juce::AlertWindow (tr ("Rename..."), {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", juce::String::fromUTF8 (state.slices[(std::size_t) selected].name.c_str()));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<FruitySlicerEditor> self (this);
        const auto i = selected;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([self, w, i] (int r) {
            if (self != nullptr && r == 1)
            {
                const auto name = w->getTextEditorContents ("name").toStdString();
                self->edit ([i, name] (State& s) {
                    if (i >= 0 && i < (int) s.slices.size())
                        s.slices[(std::size_t) i].name = name.substr (0, 255);
                });
            }
        }), true);
    }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel;
    State state;
    std::vector<std::uint8_t> lastState;
    std::string samplePath;
    std::shared_ptr<const SampleData> original, processed;
    int selected = 0;
    bool loading = false;
    juce::TextButton openButton, sampleButton, slicingButton, dumpButton;
    juce::ToggleButton autoDump, autoFit, declick, reverse, animate, playToEnd;
    juce::Label nameLabel, sliceName, sliceKey, psLabel, tsLabel, attLabel, decLabel, lowLabel, highLabel, autoLabel, brandLabel;
    juce::Slider pitch, stretch, fadeIn, fadeOut, autoLow, autoHigh;
    std::vector<Wheel> wheels;
    Scope scope;
    juce::Slider tempo, beats;           // FL's TempoSelect / LengthSelect (shown at run time)
    juce::Label tempoLabel, beatsLabel;
    SliceView sliceView;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

static std::optional<std::pair<int, juce::String>>& pendingSample()
{
    static std::optional<std::pair<int, juce::String>> pending;
    return pending;
}

void loadSampleWhenOpened (int channel, const juce::String& path)
{
    if (channel >= 0)
        pendingSample() = std::make_pair (channel, path);
    else
        pendingSample().reset();
}

std::unique_ptr<juce::Component> makeFruitySlicerEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    auto editor = std::make_unique<FruitySlicerEditor> (engine, std::move (locate), channel);
    if (auto& p = pendingSample(); p.has_value() && channel >= 0 && p->first == channel)
    {
        const auto path = p->second;
        p.reset();
        editor->loadSampleNow (path);
    }
    return editor;
}
} // namespace lynx
