#include "ui/WaveTravellerEditor.h"
#include "ui/PluginSkin.h"

#include <algorithm>
#include <cmath>

#include "core/FlpImport.h"
#include "core/WaveTraveller.h"
#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/ControllerKnob.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"

namespace lynx
{
namespace
{
using wavetrav::Patch;
using wavetrav::Point;

// the segment types FL's table builder knows (core/WaveTraveller.cpp buildTable); our names
struct SegmentType
{
    int type;
    const char* name;
};
constexpr SegmentType segmentTypes[] = {
    { 1, "Linear" }, { 2, "Smooth (cosine)" }, { 3, "Bezier" }, { 4, "Sine" }, { 5, "Ease in" }, { 6, "Tension" }, { 8, "Silence (zero)" },
};

juce::String keyName (int key)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[key % 12]) + juce::String (key / 12);
}

bool customised (const Patch& p)
{
    static const auto d = wavetrav::defaultPatch();
    auto samePoints = [] (const std::vector<Point>& a, const std::vector<Point>& b) {
        return a.size() == b.size() && std::equal (a.begin(), a.end(), b.begin(), [] (const Point& x, const Point& y) {
                   return x.x == y.x && x.y == y.y && x.type == y.type;
               });
    };
    return ! p.name.empty() || ! samePoints (p.wave, d.wave) || ! samePoints (p.vol, d.vol) || p.speedBeats != d.speedBeats
           || p.absSpeed != d.absSpeed || p.attackMs != d.attackMs || p.releaseMs != d.releaseMs || p.start != d.start || p.end != d.end
           || p.splineType != d.splineType || p.mutes.size() != d.mutes.size();
}

class WaveTravellerEditor;

// ---------------------------------------------------------------------------------------------------------------
// The curve of the selected key (position or volume), its points and the mute switches below it
class CurveView final : public juce::Component
{
public:
    std::function<Patch*()> patch;
    std::function<void()> changed; // after an edit of the patch
    bool volume = false;

    void paint (juce::Graphics& g) override
    {
        { // round 170j: a display well of the page's art (Themed), else the plain dark field
            const skin::StyleScope styleScope (skin::resolve (skin::Style::themed, skin::Art::waveTraveller), skin::Art::waveTraveller);
            if (skin::style() == skin::Style::themed)
                skin::well (g, getLocalBounds().toFloat(), theme::radius::md, juce::Colours::transparentBlack);
            else
                g.fillAll (theme::colour::bg0);
        }
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr)
            return;
        const auto area = curveArea();
        g.setColour (theme::colour::border);
        for (int i = 1; i < 8; ++i)
        {
            const auto x = area.getX() + area.getWidth() * (float) i / 8.0f;
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        }
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * (float) i / 4.0f), area.getX(), area.getRight());

        // the table the voices read (what FL builds from the points)
        const auto* table = volume ? p->volTable.data() : p->waveTable.data();
        const auto n = volume ? p->volN : p->waveN;
        if (n > 1)
        {
            juce::Path curve;
            for (int i = 0; i < n; ++i)
            {
                const auto pt = toScreen ((float) i / (float) (n - 1), table[(std::size_t) i]);
                if (i == 0)
                    curve.startNewSubPath (pt);
                else
                    curve.lineTo (pt);
            }
            g.setColour (volume ? theme::colour::automation : theme::colour::accent);
            g.strokePath (curve, juce::PathStrokeType (1.6f));
        }
        const auto& pts = points (*p);
        for (std::size_t i = 0; i < pts.size(); ++i)
        {
            const auto c = toScreen (pts[i].x, pts[i].y);
            g.setColour ((int) i == hover || (int) i == dragging ? theme::colour::textPrimary : theme::colour::knobArc);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (c));
        }

        // the mute switches: muted spans shaded
        const auto strip = muteArea();
        g.setColour (theme::colour::bg3);
        g.fillRect (strip);
        auto ms = p->mutes;
        std::stable_sort (ms.begin(), ms.end(), [] (const wavetrav::Mute& a, const wavetrav::Mute& b) { return a.at < b.at; });
        for (std::size_t i = 0; i < ms.size(); ++i)
        {
            const auto from = strip.getX() + strip.getWidth() * std::clamp (ms[i].at, 0.0f, 1.0f);
            const auto to = i + 1 < ms.size() ? strip.getX() + strip.getWidth() * std::clamp (ms[i + 1].at, 0.0f, 1.0f) : strip.getRight();
            if (! ms[i].on)
            {
                g.setColour (theme::colour::accentMuted);
                g.fillRect (juce::Rectangle<float> (from, strip.getY(), std::max (0.0f, to - from), strip.getHeight()));
            }
            g.setColour (theme::colour::textSecondary);
            g.drawVerticalLine ((int) from, strip.getY(), strip.getBottom());
        }
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (tr ("Mutes (click: switch, right click: remove)"), strip.reduced (6.0f, 0.0f), juce::Justification::centredLeft);
        g.setColour (theme::colour::border);
        g.drawRect (getLocalBounds());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = pointAt (e.position);
        if (h != hover)
        {
            hover = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr)
            return;
        if (muteArea().contains (e.position))
        {
            muteClick (*p, e);
            return;
        }
        const auto i = pointAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (i >= 0)
                pointMenu (i);
            return;
        }
        dragging = i;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr || dragging < 0)
            return;
        auto& pts = points (*p);
        if (dragging >= (int) pts.size())
            return;
        auto v = fromScreen (e.position);
        auto& q = pts[(std::size_t) dragging];
        // the ends stay at 0 and 1; the others between their neighbours
        if (dragging == 0)
            v.x = 0.0f;
        else if (dragging == (int) pts.size() - 1)
            v.x = 1.0f;
        else
            v.x = std::clamp (v.x, pts[(std::size_t) dragging - 1].x, pts[(std::size_t) dragging + 1].x);
        q.x = v.x;
        q.y = std::clamp (v.y, 0.0f, 1.0f);
        edited (*p);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr || ! curveArea().contains (e.position))
            return;
        auto& pts = points (*p);
        const auto i = pointAt (e.position);
        if (i > 0 && i < (int) pts.size() - 1) // double click on a point removes it
        {
            pts.erase (pts.begin() + i);
            edited (*p);
            return;
        }
        const auto v = fromScreen (e.position);
        const auto at = std::upper_bound (pts.begin(), pts.end(), v.x, [] (float x, const Point& q) { return x < q.x; });
        if (at == pts.begin() || at == pts.end())
            return;
        auto q = wavetrav::newPoint (v.x, std::clamp (v.y, 0.0f, 1.0f));
        q.type = (at - 1)->type; // the new point continues the segment it splits
        pts.insert (at, q);
        edited (*p);
    }

private:
    juce::Rectangle<float> curveArea() const { return getLocalBounds().toFloat().withTrimmedBottom (22.0f).reduced (8.0f, 8.0f); }
    juce::Rectangle<float> muteArea() const { return getLocalBounds().toFloat().removeFromBottom (20.0f).reduced (8.0f, 2.0f); }

    juce::Point<float> toScreen (float x, float y) const
    {
        const auto a = curveArea();
        return { a.getX() + a.getWidth() * x, a.getBottom() - a.getHeight() * y };
    }

    juce::Point<float> fromScreen (juce::Point<float> s) const
    {
        const auto a = curveArea();
        return { std::clamp ((s.x - a.getX()) / std::max (1.0f, a.getWidth()), 0.0f, 1.0f), (a.getBottom() - s.y) / std::max (1.0f, a.getHeight()) };
    }

    std::vector<Point>& points (Patch& p) const { return volume ? p.vol : p.wave; }

    int pointAt (juce::Point<float> s) const
    {
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr)
            return -1;
        const auto& pts = points (*p);
        int best = -1;
        float bestD = 7.0f;
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (const auto d = toScreen (pts[i].x, pts[i].y).getDistanceFrom (s); d < bestD)
                best = (int) i, bestD = d;
        return best;
    }

    void edited (Patch& p)
    {
        p.rebuild();
        repaint();
        if (changed)
            changed();
    }

    void pointMenu (int index)
    {
        auto* p = patch ? patch() : nullptr;
        if (p == nullptr)
            return;
        const auto& q = points (*p)[(std::size_t) index];
        juce::PopupMenu m, types, tension;
        for (const auto& t : segmentTypes)
            types.addItem (100 + t.type, tr (t.name), true, q.type == t.type);
        for (int i = 1; i <= 9; ++i)
            tension.addItem (200 + i, juce::String ((double) i / 10.0, 1), true, std::abs (q.tension - (float) i / 10.0f) < 1e-4f);
        m.addSubMenu (tr ("Segment type"), types);
        m.addSubMenu (tr ("Tension"), tension, q.type == 6);
        m.addSeparator();
        const auto last = (int) points (*p).size() - 1;
        m.addItem (1, tr ("Delete point"), index > 0 && index < last);
        juce::Component::SafePointer<CurveView> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [self, index] (int r) {
            if (self == nullptr || r == 0)
                return;
            auto* p = self->patch ? self->patch() : nullptr;
            if (p == nullptr)
                return;
            auto& pts = self->points (*p);
            if (index >= (int) pts.size())
                return;
            if (r == 1)
                pts.erase (pts.begin() + index);
            else if (r >= 200)
                pts[(std::size_t) index].tension = (float) (r - 200) / 10.0f;
            else if (r >= 100)
                pts[(std::size_t) index].type = r - 100;
            self->edited (*p);
        });
    }

    void muteClick (Patch& p, const juce::MouseEvent& e)
    {
        const auto strip = muteArea();
        const auto at = std::clamp ((e.position.x - strip.getX()) / std::max (1.0f, strip.getWidth()), 0.0f, 1.0f);
        if (e.mods.isPopupMenu())
        {
            int best = -1;
            float bestD = 6.0f;
            for (std::size_t i = 0; i < p.mutes.size(); ++i)
                if (const auto d = std::abs (strip.getX() + strip.getWidth() * p.mutes[i].at - e.position.x); d < bestD)
                    best = (int) i, bestD = d;
            if (best >= 0)
            {
                p.mutes.erase (p.mutes.begin() + best);
                edited (p);
            }
            return;
        }
        // the state there, switched from here on
        bool on = true;
        float from = -1.0f;
        for (const auto& m : p.mutes)
            if (m.at <= at && m.at >= from)
                on = m.on, from = m.at;
        p.mutes.push_back ({ ! on, at });
        std::stable_sort (p.mutes.begin(), p.mutes.end(), [] (const wavetrav::Mute& a, const wavetrav::Mute& b) { return a.at < b.at; });
        edited (p);
    }

    int hover = -1, dragging = -1;
};

// ---------------------------------------------------------------------------------------------------------------
// The 120 keys (C0 … B9): the one whose patch the editor shows; keys with a patch of their own are marked
class KeyStrip final : public juce::Component
{
public:
    std::function<int()> selected;
    std::function<bool (int)> marked;
    std::function<void (int)> select;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        const auto w = (float) getWidth() / (float) wavetrav::keyCount;
        const auto sel = selected ? selected() : -1;
        for (int k = 0; k < wavetrav::keyCount; ++k)
        {
            const auto black = juce::String ("010100101010").substring (k % 12, k % 12 + 1) == "1";
            auto r = juce::Rectangle<float> ((float) k * w, 0.0f, w, (float) getHeight()).reduced (0.5f, 0.0f);
            g.setColour (k == sel ? theme::colour::accent : black ? theme::colour::bg3 : theme::colour::bg5);
            g.fillRect (black ? r.withTrimmedBottom ((float) getHeight() * 0.35f) : r);
            if (marked && marked (k))
            {
                g.setColour (k == sel ? theme::colour::onAccent : theme::colour::ledOn);
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getCentreX(), (float) getHeight() - 5.0f }));
            }
            if (k % 12 == 0)
            {
                g.setColour (theme::colour::textSecondary);
                g.setFont (theme::font (theme::type::caption));
                g.drawText (keyName (k), juce::Rectangle<float> (r.getX() + 1.0f, 0.0f, w * 4.0f, 12.0f), juce::Justification::left);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { pick (e); }
    void mouseDrag (const juce::MouseEvent& e) override { pick (e); }

private:
    void pick (const juce::MouseEvent& e)
    {
        const auto k = std::clamp ((int) (e.position.x / std::max (1.0f, (float) getWidth()) * (float) wavetrav::keyCount), 0, wavetrav::keyCount - 1);
        if (select && (! selected || selected() != k))
            select (k);
    }
};

// ---------------------------------------------------------------------------------------------------------------
class WaveTravellerEditor final : public juce::Component,
                                  public juce::FileDragAndDropTarget,
                                  private juce::ChangeListener
{
public:
    WaveTravellerEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        juce::ignoreUnused (channel);
        auto button = [this] (juce::TextButton& b, const juce::String& text, const juce::String& hint, std::function<void()> click) {
            b.setButtonText (text);
            ui::setHint (b, hint);
            b.onClick = std::move (click);
            b.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
            addAndMakeVisible (b);
        };
        button (loadButton, tr ("Load sample..."), tr ("The sample the curves travel through (or drop a sample here)"), [this] { chooseSample(); });
        button (presetButton, tr ("Presets"), tr ("FL Studio's Wave Traveller presets: the curves of this key"), [this] { presetMenu(); });
        button (keyButton, tr ("Key"), tr ("Copy or reset this key's patch"), [this] { keyMenu(); });
        button (waveButton, tr ("Position"), tr ("Edit the position curve: where the note is in the sample over its time"), [this] { showVolume (false); });
        button (volButton, tr ("Volume"), tr ("Edit the volume curve"), [this] { showVolume (true); });
        waveButton.setRadioGroupId (1);
        volButton.setRadioGroupId (1);
        waveButton.setClickingTogglesState (true);
        volButton.setClickingTogglesState (true);
        waveButton.setToggleState (true, juce::dontSendNotification);

        mode.addItem (tr ("Tempo based spline"), 1);
        mode.addItem (tr ("Absolute speed based spline"), 2);
        mode.onChange = [this] {
            const auto absolute = mode.getSelectedId() == 2;
            edit ([absolute] (Patch& p) { p.splineType = absolute ? 1 : 0; });
        };
        ui::setHint (mode, tr ("Speed mode: the curve lasts a number of beats, or plays the sample at a speed factor"));
        addAndMakeVisible (mode);

        sampleLabel.setFont (theme::font (theme::type::body));
        sampleLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (sampleLabel);
        keyLabel.setFont (theme::font (theme::type::body));
        keyLabel.setColour (juce::Label::textColourId, theme::colour::accent);
        keyLabel.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (keyLabel);

        auto knob = [this] (std::unique_ptr<ctlui::Knob>& k, ui::ValueSpec spec, const juce::String& shortName, std::function<double (const Patch&)> get,
                            std::function<void (Patch&, double)> set) {
            k = std::make_unique<ctlui::Knob> (&engine, std::move (spec), [this, get] { return get (current()); },
                                               [this, set] (double v) { edit ([set, v] (Patch& p) { set (p, v); }); });
            k->shortName = shortName;
            addAndMakeVisible (*k);
        };
        auto spec = [] (const juce::String& name, double lo, double hi, double def, std::function<juce::String (double)> text) {
            ui::ValueSpec s;
            s.name = name;
            s.minimum = lo, s.maximum = hi, s.defaultValue = def;
            s.toText = std::move (text);
            return s;
        };
        const auto beats = [] (double v) { return juce::String (v, 3) + " " + tr ("beats"); };
        const auto factor = [] (double v) { return juce::String (v, 2) + "x"; };
        const auto ms = [] (double v) { return juce::String (v, 1) + " ms"; };
        const auto percent = [] (double v) { return juce::String (v * 100.0, 1) + "%"; };
        knob (speedTempo, spec (tr ("Speed (beats the curve lasts)"), 0.125, 8.0, 4.0, beats), tr ("Speed"),
              [] (const Patch& p) { return (double) p.speedBeats; }, [] (Patch& p, double v) { p.speedBeats = (float) v; });
        knob (speedAbs, spec (tr ("Speed (factor of the sample's own speed)"), 0.25, 4.0, 1.0, factor), tr ("Speed"),
              [] (const Patch& p) { return (double) p.absSpeed; }, [] (Patch& p, double v) { p.absSpeed = (float) v; });
        knob (attack, spec (tr ("Attack"), 1.0, 500.0, 1.0, ms), tr ("Attack"), [] (const Patch& p) { return (double) p.attackMs; },
              [] (Patch& p, double v) { p.attackMs = (float) v; });
        knob (release, spec (tr ("Release"), 1.0, 1000.0, 1.0, ms), tr ("Release"), [] (const Patch& p) { return (double) p.releaseMs; },
              [] (Patch& p, double v) { p.releaseMs = (float) v; });
        knob (start, spec (tr ("Start (the sample's part the curve spans)"), 0.0, 1.0, 0.0, percent), tr ("Start"),
              [] (const Patch& p) { return (double) p.start; }, [] (Patch& p, double v) { p.start = (float) v; });
        knob (end, spec (tr ("End (the sample's part the curve spans)"), 0.0, 1.0, 1.0, percent), tr ("End"),
              [] (const Patch& p) { return (double) p.end; }, [] (Patch& p, double v) { p.end = (float) v; });

        curve.patch = [this] { return &state.patches[(std::size_t) std::clamp (state.selectedKey, 0, wavetrav::keyCount - 1)]; };
        curve.changed = [this] { commit(); };
        ui::setHint (curve, tr ("Drag a point; double click adds or removes a point; right click: segment type"));
        addAndMakeVisible (curve);

        keys.selected = [this] { return state.selectedKey; };
        keys.marked = [this] (int k) { return customised (state.patches[(std::size_t) k]); };
        keys.select = [this] (int k) {
            state.selectedKey = k;
            commit (false);
        };
        ui::setHint (keys, tr ("The key whose patch is shown: each key plays its own curves"));
        addAndMakeVisible (keys);

        engine.addChangeListener (this);
        reload();
        setSize (760, 470);
    }

    ~WaveTravellerEditor() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override // round 170j: the art's canvas (Themed), else the plain panel
    {
        const skin::StyleScope styleScope (skin::resolve (skin::Style::themed, skin::Art::waveTraveller), skin::Art::waveTraveller);
        if (skin::style() == skin::Style::themed)
            skin::canvas (g, getLocalBounds().toFloat(), skin::artAccent (skin::Art::waveTraveller));
        else
            g.fillAll (theme::colour::bg2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (28);
        loadButton.setBounds (top.removeFromLeft (120));
        top.removeFromLeft (6);
        presetButton.setBounds (top.removeFromLeft (90));
        top.removeFromLeft (6);
        keyButton.setBounds (top.removeFromLeft (60));
        top.removeFromLeft (8);
        keyLabel.setBounds (top.removeFromRight (150));
        sampleLabel.setBounds (top);
        r.removeFromTop (6);
        auto knobs = r.removeFromTop (74);
        const auto w = 72;
        mode.setBounds (knobs.removeFromLeft (210).withSizeKeepingCentre (210, 26));
        knobs.removeFromLeft (10);
        for (auto* k : { speedTempo.get(), speedAbs.get(), attack.get(), release.get(), start.get(), end.get() })
            if (k != speedAbs.get())
                k->setBounds (knobs.removeFromLeft (w));
        speedAbs->setBounds (speedTempo->getBounds());
        r.removeFromTop (6);
        keys.setBounds (r.removeFromBottom (44));
        r.removeFromBottom (6);
        auto tabs = r.removeFromTop (24);
        waveButton.setBounds (tabs.removeFromLeft (100));
        tabs.removeFromLeft (4);
        volButton.setBounds (tabs.removeFromLeft (100));
        r.removeFromTop (4);
        curve.setBounds (r);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() == 1 && juce::File (files[0]).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (! files.isEmpty())
            setSample (files[0]);
    }

private:
    const ChannelDef* def() const
    {
        return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr;
    }

    Patch& current() { return state.patches[(std::size_t) std::clamp (state.selectedKey, 0, wavetrav::keyCount - 1)]; }

    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        lastState = d->pluginState;
        if (! wavetrav::parseState (d->pluginState, state))
            state = wavetrav::defaultState();
        samplePath = d->samplePath;
        refresh();
    }

    void refresh()
    {
        const auto& p = current();
        sampleLabel.setText (samplePath.empty() ? tr ("No sample: the channel is silent (load or drop one)")
                                                : juce::String::fromUTF8 (samplePath.c_str()).fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false),
                             juce::dontSendNotification);
        keyLabel.setText (tr ("Key") + " " + keyName (state.selectedKey) + (p.name.empty() ? juce::String() : " - " + juce::String::fromUTF8 (p.name.c_str())),
                          juce::dontSendNotification);
        mode.setSelectedId (p.splineType == 1 ? 2 : 1, juce::dontSendNotification);
        speedTempo->setVisible (p.splineType != 1);
        speedAbs->setVisible (p.splineType == 1);
        repaint();
        curve.repaint();
        keys.repaint();
    }

    void edit (const std::function<void (Patch&)>& change)
    {
        change (current());
        current().rebuild();
        commit();
    }

    // writes the state in FL's layout into the channel
    void commit (bool undoable = true)
    {
        const auto bytes = wavetrav::writeState (state);
        lastState = bytes;
        engine.editSong ([l = locate, bytes] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
                d->pluginState = bytes;
        }, undoable);
        refresh();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d != nullptr && (d->pluginState != lastState || d->samplePath != samplePath))
            reload();
    }

    void showVolume (bool v)
    {
        curve.volume = v;
        curve.repaint();
    }

    void setSample (const juce::String& path)
    {
        const auto p = path.toStdString();
        state.samplePath = p; // (FL's state names the sample too)
        samplePath = p;
        const auto bytes = wavetrav::writeState (state);
        lastState = bytes;
        engine.editSong ([l = locate, bytes, p] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                d->samplePath = p;
            }
        });
        refresh();
    }

    void chooseSample()
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("Load sample..."), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<WaveTravellerEditor> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().existsAsFile())
                self->setSample (fc.getResult().getFullPathName());
        });
    }

    // FL's .spln presets, from the user's installation (never copied: read when picked)
    void presetMenu()
    {
        const auto folder = resolveFlPath ("%FLStudioFactoryData%\\Plugins\\Fruity\\Generators\\Wave Traveller\\Presets", flStudioAccess().roots);
        presets.clear();
        if (! folder.empty())
            presets = platform::localFile (folder).findChildFiles (juce::File::findFiles, false, "*.spln");
        std::sort (presets.begin(), presets.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        juce::PopupMenu m;
        for (int i = 0; i < presets.size(); ++i)
            m.addItem (i + 1, presets[i].getFileNameWithoutExtension());
        if (presets.isEmpty())
            m.addItem (-1, tr ("No presets found (FL Studio is not installed)"), false);
        juce::Component::SafePointer<WaveTravellerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [self] (int r) {
            if (self == nullptr || r <= 0 || r > self->presets.size())
                return;
            juce::MemoryBlock data;
            if (! self->presets[r - 1].loadFileAsData (data))
                return;
            const auto* b = static_cast<const std::uint8_t*> (data.getData());
            const std::vector<std::uint8_t> bytes (b, b + data.getSize());
            auto& p = self->current();
            if (wavetrav::applyPreset (bytes, p))
            {
                p.name = self->presets[r - 1].getFileNameWithoutExtension().toStdString();
                self->commit();
            }
        });
    }

    void keyMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, tr ("Copy this key's patch to all keys"));
        m.addItem (2, tr ("Reset this key's patch"));
        juce::Component::SafePointer<WaveTravellerEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton), [self] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 1)
            {
                const auto p = self->current();
                for (auto& q : self->state.patches)
                    q = p;
            }
            else
                self->current() = wavetrav::defaultPatch();
            self->commit();
        });
    }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel;
    wavetrav::State state;
    std::vector<std::uint8_t> lastState;
    std::string samplePath;
    juce::TextButton loadButton, presetButton, keyButton, waveButton, volButton;
    juce::ComboBox mode;
    juce::Label sampleLabel, keyLabel;
    std::unique_ptr<ctlui::Knob> speedTempo, speedAbs, attack, release, start, end;
    CurveView curve;
    KeyStrip keys;
    juce::Array<juce::File> presets;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

std::unique_ptr<juce::Component> makeWaveTravellerEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<WaveTravellerEditor> (engine, std::move (locate), channel);
}

std::unique_ptr<juce::Component> makeWaveTravellerEditor (AudioEngine& engine, int channel)
{
    return makeWaveTravellerEditor (engine, [channel] (Song& s) -> ChannelDef* {
        return channel >= 0 && channel < (int) s.channels.size() ? &s.channels[(std::size_t) channel] : nullptr;
    }, channel);
}
} // namespace lynx
