#include "ui/SlicexEditor.h"

#include <optional>
#include <utility>
#include "ui/FileChoosers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/FlAutoSlicer.h"
#include "core/Instruments.h"
#include "core/Slicex.h"
#include "core/SlicexEngine.h"
#include "core/SlicexInstrument.h"
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
using slicex::State;

constexpr float scale = 1.25f; // FL's form (714 × 625) a quarter larger

juce::Rectangle<int> fl (int x, int y, int w, int h)
{
    return juce::Rectangle<float> ((float) x * scale, (float) y * scale, (float) w * scale, (float) h * scale).toNearestInt();
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

// A WAV's title (LIST/INFO INAM), "" when it has none: what FL shows as the deck's title
juce::String wavTitle (const std::vector<std::uint8_t>& w)
{
    auto le32 = [&w] (std::size_t p) { return (std::size_t) w[p] | ((std::size_t) w[p + 1] << 8) | ((std::size_t) w[p + 2] << 16) | ((std::size_t) w[p + 3] << 24); };
    for (std::size_t p = 12; p + 12 <= w.size();)
    {
        const auto size = le32 (p + 4);
        const auto end = std::min (w.size(), p + 8 + size);
        if (std::memcmp (w.data() + p, "LIST", 4) == 0 && std::memcmp (w.data() + p + 8, "INFO", 4) == 0)
            for (std::size_t q = p + 12; q + 8 <= end;)
            {
                const auto sub = le32 (q + 4);
                if (std::memcmp (w.data() + q, "INAM", 4) == 0 && q + 8 + sub <= end)
                {
                    const auto* text = reinterpret_cast<const char*> (w.data() + q + 8);
                    return juce::String::fromUTF8 (text, (int) strnlen (text, sub)).trim();
                }
                q += 8 + sub + (sub & 1);
            }
        p += 8 + size + (size & 1);
    }
    return {};
}

// FL's position text: bar : beat : tick (96 ticks a beat, 4 beats a bar), all from 1 / 0
juce::String barBeatTick (double beats)
{
    const auto ticks = (long long) std::llround (std::max (0.0, beats) * 96.0);
    return juce::String (ticks / 384 + 1).paddedLeft ('0', 3) + ":" + juce::String ((ticks / 96) % 4 + 1).paddedLeft ('0', 2) + ":"
           + juce::String (ticks % 96).paddedLeft ('0', 3);
}

juce::Colour deckColour (int deck)
{
    return deck == 0 ? theme::colour::accent : juce::Colour (0xff4aa3df);
}

// ---------------------------------------------------------------------------------------------------------------
// FL's TPaintBoxSelector: a strip of choices, one lit
class Selector final : public juce::Component
{
public:
    juce::StringArray items;
    int value = 0;
    std::function<void (int)> onChange;
    std::function<bool (int)> marked; // a dot on the cell (an articulator part that is switched on)
    std::function<void (juce::Graphics&, int, juce::Rectangle<float>)> drawCell; // an icon instead of the text

    void paint (juce::Graphics& g) override
    {
        const auto n = std::max (1, items.size());
        const auto w = (float) getWidth() / (float) n;
        g.setFont (theme::font (std::min (theme::type::caption, (float) getHeight() - 2.0f)));
        for (int i = 0; i < items.size(); ++i)
        {
            const auto cell = juce::Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (0.5f);
            g.setColour (i == value ? theme::colour::accent : theme::colour::bg4);
            g.fillRoundedRectangle (cell, 2.0f);
            g.setColour (i == value ? theme::colour::onAccent : theme::colour::textSecondary);
            if (drawCell)
                drawCell (g, i, cell.reduced (3.0f));
            else
                ui::drawFittingText (g, items[i], cell.reduced (2.0f, 0.0f), juce::Justification::centred, g.getCurrentFont());
            if (marked && marked (i))
            {
                g.setColour (i == value ? theme::colour::onAccent : theme::colour::accent);
                g.fillEllipse (cell.getRight() - 5.0f, cell.getY() + 2.0f, 3.0f, 3.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto i = std::clamp (e.x * std::max (1, items.size()) / std::max (1, getWidth()), 0, items.size() - 1);
        if (i != value && onChange)
            onChange (i);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const auto i = std::clamp (value + (w.deltaY > 0.0f ? -1 : 1), 0, items.size() - 1);
        if (i != value && onChange)
            onChange (i);
    }
};

// FL's TVectorCheckBox: a caption with its LED (left or right of it)
class Led final : public juce::ToggleButton
{
public:
    bool ledRight = false;

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        auto r = getLocalBounds().toFloat();
        const auto d = std::min (r.getHeight() - 2.0f, 11.0f * scale);
        const auto text = getButtonText();
        const auto led = text.isEmpty() ? r.withSizeKeepingCentre (d, d)
                                        : (ledRight ? r.removeFromRight (d + 2.0f) : r.removeFromLeft (d + 2.0f)).withSizeKeepingCentre (d, d);
        g.setColour (theme::colour::bg0);
        g.fillEllipse (led);
        g.setColour (over ? theme::colour::textSecondary : theme::colour::borderStrong);
        g.drawEllipse (led, 1.0f);
        if (getToggleState())
        {
            g.setColour (theme::colour::accent);
            g.fillEllipse (led.reduced (2.5f));
        }
        if (text.isNotEmpty())
        {
            g.setColour (isEnabled() ? theme::colour::textPrimary : theme::colour::textDisabled);
            ui::drawFittingText (g, text, r.reduced (3.0f, 0.0f), ledRight ? juce::Justification::centredRight : juce::Justification::centredLeft,
                                 juce::Font (theme::font (theme::type::caption)));
        }
    }
};

// A tool button with a drawn icon (FL's TQuickBtn glyphs, drawn by LYNX)
class IconButton final : public juce::Button
{
public:
    enum Icon { loop, play, stop, file, flag, dual, arrow, keys, zoom, select };
    explicit IconButton (Icon i) : juce::Button ({}), icon (i) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto on = getToggleState();
        g.setColour (down ? theme::colour::bg6 : (over ? theme::colour::bg5 : theme::colour::bg4));
        g.fillRoundedRectangle (r, 3.0f);
        const auto c = r.getCentre();
        const auto u = std::min (r.getWidth(), r.getHeight()) * 0.26f;
        g.setColour (on ? theme::colour::accent : theme::colour::textPrimary);
        juce::Path p;
        switch (icon)
        {
            case loop:
                g.drawEllipse (c.x - u * 1.5f, c.y - u * 0.7f, u * 1.5f, u * 1.4f, 1.6f);
                g.drawEllipse (c.x, c.y - u * 0.7f, u * 1.5f, u * 1.4f, 1.6f);
                break;
            case play:
                p.addTriangle (c.x - u * 0.7f, c.y - u, c.x - u * 0.7f, c.y + u, c.x + u, c.y);
                g.fillPath (p);
                break;
            case stop:
                g.fillRect (juce::Rectangle<float> (u * 1.6f, u * 1.6f).withCentre (c));
                break;
            case file:
                p.startNewSubPath (c.x - u, c.y - u * 1.2f);
                p.lineTo (c.x + u * 0.4f, c.y - u * 1.2f);
                p.lineTo (c.x + u, c.y - u * 0.6f);
                p.lineTo (c.x + u, c.y + u * 1.2f);
                p.lineTo (c.x - u, c.y + u * 1.2f);
                p.closeSubPath();
                g.strokePath (p, juce::PathStrokeType (1.5f));
                break;
            case flag:
                g.drawLine (c.x - u * 0.7f, c.y - u * 1.2f, c.x - u * 0.7f, c.y + u * 1.2f, 1.6f);
                p.addTriangle (c.x - u * 0.7f, c.y - u * 1.2f, c.x + u, c.y - u * 0.5f, c.x - u * 0.7f, c.y + u * 0.2f);
                g.fillPath (p);
                break;
            case dual:
                g.drawRect (juce::Rectangle<float> (c.x - u * 1.2f, c.y - u, u * 2.4f, u * 1.2f), 1.3f);
                g.drawRect (juce::Rectangle<float> (c.x - u * 1.2f, c.y + u * 0.4f, u * 2.4f, u * 0.7f), 1.3f);
                break;
            case arrow:
                p.addTriangle (c.x - u, c.y - u * 0.5f, c.x + u, c.y - u * 0.5f, c.x, c.y + u * 0.7f);
                g.fillPath (p);
                break;
            case keys:
                for (int i = -1; i <= 1; ++i)
                    g.fillRect (juce::Rectangle<float> (u * 0.6f, u * 2.2f).withCentre ({ c.x + (float) i * u * 0.9f, c.y }));
                break;
            case zoom:
                g.drawEllipse (c.x - u * 1.1f, c.y - u * 1.1f, u * 1.6f, u * 1.6f, 1.5f);
                g.drawLine (c.x + u * 0.3f, c.y + u * 0.3f, c.x + u * 1.1f, c.y + u * 1.1f, 1.8f);
                break;
            case select: // a selection frame: the four corners
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2)
                    {
                        const auto x = c.x + (float) sx * u * 1.1f, y = c.y + (float) sy * u * 1.1f;
                        g.drawLine (x, y, x - (float) sx * u * 0.7f, y, 1.5f);
                        g.drawLine (x, y, x, y - (float) sy * u * 0.7f, 1.5f);
                    }
                break;
        }
    }

private:
    Icon icon;
};

// FL's TVectorDigiWheel: a number in a well; drag up / down or wheel to change
class Digit final : public juce::Component
{
public:
    int value = 0, lo = 0, hi = 9, reset = 0;
    std::function<juce::String (int)> text;
    std::function<void (int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (text ? text (value) : juce::String (value), getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        start = value;
        if (e.mods.isAltDown() || e.mods.isMiddleButtonDown())
            set (reset);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isAltDown() && ! e.mods.isMiddleButtonDown())
            set (start + (int) std::lround ((double) -e.getDistanceFromDragStartY() / ui::digitWheelPixelsPerStep));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { set (reset); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { set (value + (w.deltaY > 0.0f ? 1 : -1)); }

private:
    void set (int v)
    {
        v = std::clamp (v, lo, hi);
        if (v != value && onChange)
            onChange (v);
    }

    int start = 0;
};

// FL's TQuickXYZ: the Mod X / Y pad
class XyPad final : public juce::Component
{
public:
    int x = 256, y = 256; // 0…512
    std::function<void (int, int)> onChange;

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (theme::colour::border);
        g.drawHorizontalLine (getHeight() / 2, 1.0f, (float) getWidth() - 1.0f);
        g.drawVerticalLine (getWidth() / 2, 1.0f, (float) getHeight() - 1.0f);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        const auto px = r.getX() + r.getWidth() * (float) x / 512.0f, py = r.getBottom() - r.getHeight() * (float) y / 512.0f;
        g.setColour (theme::colour::accent);
        g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override { mouseDrag (e); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto nx = std::clamp ((int) std::lround (512.0 * e.x / std::max (1, getWidth())), 0, 512);
        const auto ny = std::clamp ((int) std::lround (512.0 * (getHeight() - e.y) / std::max (1, getHeight())), 0, 512);
        if (onChange && (nx != x || ny != y))
            onChange (nx, ny);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (onChange)
            onChange (256, 256);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// The articulator editor's graph (FL's TMEnvEditor): the envelope, the LFO's amplitude envelope with its wave, or a
// map. Left drag moves a point, right click on the graph adds one, right click on a point opens its menu.
class CurveView final : public juce::Component
{
public:
    std::function<const slicex::Curve*()> curve;
    std::function<const slicex::Articulator*()> articulator;
    std::function<int()> part; // 0 envelope, 1 LFO, 2…5 maps
    std::function<void (const std::function<void (slicex::Curve&)>&, bool)> change; // (…, undoable)

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto r = area();
        g.setColour (theme::colour::border);
        for (int i = 1; i < 4; ++i)
            g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        for (int i = 1; i < 8; ++i)
            g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr || c->points.empty())
            return;
        const auto p = part ? part() : 0;
        const auto map = p >= 2;
        const auto on = c->on() || map;
        const auto length = span (*c);
        // the LFO's wave under its amplitude envelope
        if (p == 1 && articulator && articulator() != nullptr)
        {
            const auto wave = slicex::lfoCycle (*articulator(), 64);
            const auto amp = slicex::curveLevels (*c, false, std::max (2, (int) r.getWidth()));
            juce::Path w;
            for (int x = 0; x < (int) amp.size(); ++x)
            {
                const auto cycle = (float) x / 24.0f; // (a fixed number of cycles: the graph shows the shape)
                const auto v = wave[(std::size_t) ((int) (cycle * 64.0f) % 64)] * amp[(std::size_t) x];
                const auto y = r.getCentreY() - v * r.getHeight() * 0.5f;
                x == 0 ? w.startNewSubPath (r.getX(), y) : w.lineTo (r.getX() + (float) x, y);
            }
            g.setColour (theme::colour::accent.withAlpha (on ? 0.45f : 0.2f));
            g.strokePath (w, juce::PathStrokeType (1.0f));
        }
        // the curve, with FL's segment shapes
        const auto levels = slicex::curveLevels (*c, map, std::max (2, (int) r.getWidth()));
        juce::Path line;
        for (int x = 0; x < (int) levels.size(); ++x)
        {
            const auto y = r.getBottom() - std::clamp (levels[(std::size_t) x], 0.0f, 1.0f) * r.getHeight();
            x == 0 ? line.startNewSubPath (r.getX(), y) : line.lineTo (r.getX() + (float) x, y);
        }
        auto fill = line;
        fill.lineTo (r.getRight(), r.getBottom());
        fill.lineTo (r.getX(), r.getBottom());
        fill.closeSubPath();
        const auto colour = theme::colour::accent.withMultipliedAlpha (on ? 1.0f : 0.6f); // (FL draws a switched-off curve in its colour too)
        g.setColour (colour.withAlpha (0.12f));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (1.5f));
        // the points; decay, loop start and sustain marked with their letter
        double t = 0.0;
        g.setFont (theme::font (theme::type::caption));
        for (int i = 0; i < (int) c->points.size(); ++i)
        {
            const auto& pt = c->points[(std::size_t) i];
            t += i == 0 ? 0.0 : pt.dt;
            const auto at = juce::Point<float> (r.getX() + (length > 0.0 ? (float) (t / length) : 0.0f) * r.getWidth(),
                                                r.getBottom() - (float) std::clamp (pt.y, 0.0, 1.0) * r.getHeight());
            g.setColour (i == hot ? theme::colour::textPrimary : colour.brighter (0.3f));
            g.fillEllipse (at.x - 3.5f, at.y - 3.5f, 7.0f, 7.0f);
            juce::String tag;
            if (! map)
                tag << (i == c->decay ? "D" : "") << (i == c->loop ? "L" : "") << (i == c->sustain ? "S" : "");
            if (tag.isNotEmpty())
            {
                g.setColour (theme::colour::textSecondary);
                g.drawText (tag, juce::Rectangle<float> (at.x - 12.0f, r.getBottom() - 12.0f, 24.0f, 12.0f), juce::Justification::centred);
                g.drawVerticalLine ((int) at.x, at.y + 4.0f, r.getBottom() - 12.0f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = pointAt (e.position);
        if (h != hot)
        {
            hot = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hot = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr || ! change)
            return;
        dragging = pointAt (e.position);
        moved = false;
        if (e.mods.isPopupMenu())
        {
            if (dragging >= 0)
                pointMenu (dragging);
            else
                addPoint (e.position);
            dragging = -1;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr || dragging < 0 || dragging >= (int) c->points.size() || ! change)
            return;
        const auto r = area();
        const auto length = span (*c);
        const auto i = dragging;
        const auto y = std::clamp ((double) (r.getBottom() - e.position.y) / (double) r.getHeight(), 0.0, 1.0);
        // the time since the previous point; the points after it keep their own distances (FL's default, "slide")
        double before = 0.0;
        for (int k = 1; k < i; ++k)
            before += c->points[(std::size_t) k].dt;
        const auto t = (double) (e.position.x - r.getX()) / (double) r.getWidth() * (length > 0.0 ? length : 1.0);
        const auto dt = i == 0 ? 0.0 : std::max (0.0, t - before);
        change ([i, y, dt] (slicex::Curve& k) {
            if (i < (int) k.points.size())
            {
                k.points[(std::size_t) i].y = y;
                k.points[(std::size_t) i].dt = dt;
            }
        }, ! moved);
        moved = true;
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = -1; }

private:
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (5.0f, 6.0f); }

    static double span (const slicex::Curve& c)
    {
        double s = 0.0;
        for (std::size_t i = 1; i < c.points.size(); ++i)
            s += c.points[i].dt;
        return s;
    }

    int pointAt (juce::Point<float> at) const
    {
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr)
            return -1;
        const auto r = area();
        const auto length = span (*c);
        double t = 0.0;
        for (int i = 0; i < (int) c->points.size(); ++i)
        {
            t += i == 0 ? 0.0 : c->points[(std::size_t) i].dt;
            const auto p = juce::Point<float> (r.getX() + (length > 0.0 ? (float) (t / length) : 0.0f) * r.getWidth(),
                                               r.getBottom() - (float) std::clamp (c->points[(std::size_t) i].y, 0.0, 1.0) * r.getHeight());
            if (p.getDistanceFrom (at) < 6.0f)
                return i;
        }
        return -1;
    }

    void addPoint (juce::Point<float> at)
    {
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr)
            return;
        const auto r = area();
        const auto length = span (*c);
        const auto t = std::max (0.0, (double) (at.x - r.getX()) / (double) r.getWidth() * (length > 0.0 ? length : 1.0));
        const auto y = std::clamp ((double) (r.getBottom() - at.y) / (double) r.getHeight(), 0.0, 1.0);
        change ([t, y] (slicex::Curve& k) {
            double sum = 0.0;
            std::size_t i = 1;
            for (; i < k.points.size(); ++i)
            {
                if (sum + k.points[i].dt > t)
                    break;
                sum += k.points[i].dt;
            }
            slicex::Point p;
            p.dt = t - sum;
            p.y = y;
            if (i < k.points.size())
                k.points[i].dt -= p.dt;
            const auto at2 = (int) i;
            k.points.insert (k.points.begin() + (std::ptrdiff_t) std::min (i, k.points.size()), p);
            for (auto* index : { &k.decay, &k.loop, &k.sustain })
                if (*index >= at2)
                    ++*index;
        }, true);
    }

    void pointMenu (int i)
    {
        const auto* c = curve ? curve() : nullptr;
        if (c == nullptr || i < 0 || i >= (int) c->points.size())
            return;
        const auto map = (part ? part() : 0) >= 2;
        juce::PopupMenu m;
        m.addItem (1, tr ("Delete"), i > 0 && c->points.size() > 2);
        if (! map)
        {
            m.addSeparator();
            if ((part ? part() : 0) == 0)
                m.addItem (2, tr ("Decay"), true, c->decay == i);
            m.addItem (3, tr ("Loop start"), true, c->loop == i);
            m.addItem (4, tr ("Sustain / loop end"), true, c->sustain == i);
        }
        m.addSeparator();
        // FL's curve modes, in its order
        static const char* const modes[] = { "Single curve", "Double curve", "Hold", "Stairs", "Smooth stairs", "Pulse", "Wave",
                                             "Single curve 2", "Double curve 2", "Half sine", "Smooth", "Single curve 3", "Double curve 3" };
        for (int k = 0; k < 13; ++k)
            m.addItem (100 + k, tr (modes[k]), true, c->points[(std::size_t) i].mode == k);
        juce::Component::SafePointer<CurveView> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [self, i] (int r) {
            if (self == nullptr || r == 0 || ! self->change)
                return;
            self->change ([i, r] (slicex::Curve& k) {
                if (i >= (int) k.points.size())
                    return;
                if (r == 1 && i > 0)
                {
                    if (i + 1 < (int) k.points.size())
                        k.points[(std::size_t) i + 1].dt += k.points[(std::size_t) i].dt;
                    k.points.erase (k.points.begin() + i);
                    for (auto* index : { &k.decay, &k.loop, &k.sustain })
                        *index = *index == i ? -1 : (*index > i ? *index - 1 : *index);
                }
                else if (r == 2)
                    k.decay = k.decay == i ? -1 : i;
                else if (r == 3)
                    k.loop = k.loop == i ? -1 : i;
                else if (r == 4)
                    k.sustain = k.sustain == i ? -1 : i;
                else if (r >= 100)
                    k.points[(std::size_t) i].mode = (std::uint8_t) (r - 100);
            }, true);
        });
    }

    int hot = -1, dragging = -1;
    bool moved = false;
};

// ---------------------------------------------------------------------------------------------------------------
// A deck's wave with its regions (FL's WAVEditor). From the top: the zoom / scroll bar (FL's "Scroller above"), the
// row of region tabs, the wave. Wheel zooms at the mouse (Shift + wheel scrolls); the bar's lit part is dragged to
// scroll and by its edges to zoom; a tab is dragged to move the region's start, its right click opens the region's
// menu, and so is the tab at its end (FL's second tag: the region's length). The wave carries FL's selection: a drag
// makes one, with one the nearer edge goes to the mouse, a double click drops it (REA_rodada190aj §2); a plain click
// without a selection picks the region under it and plays it (LYNX's own).
class DeckView final : public juce::Component
{
public:
    int deck = 0;
    std::function<std::shared_ptr<const SampleData>()> sample;
    std::function<const std::vector<slicex::WavRegion>*()> regions;
    std::function<int()> selected; // the region shown in the region settings when it is on this deck, else −1
    std::function<void (int, bool)> select; // (region, play it)
    std::function<void (int)> menu;
    std::function<std::int64_t (int, std::int64_t)> snap;      // where a dragged start lands (region, frame)
    std::function<void (int, std::int64_t)> moveStart;         // a tab was dropped
    std::function<void (int, std::int64_t)> moveEnd;           // an end tab was dropped (region, the first frame after it)
    std::function<void (bool)> selectionChanged;               // (the mouse was released / a command: it can be stored)
    std::function<bool (std::int64_t&, std::int64_t&)> loop;   // the loop points to draw (FL's View ▸ Loop), false = none
    std::function<bool (const juce::KeyPress&)> key;           // the deck's shortcuts (FL's menu keys)
    std::function<bool()> frozen;                              // FL's "Freeze all": the regions cannot be edited
    std::function<void()> viewChanged;

    // the frames shown: [viewStart, viewStart + viewLength); a length of 0 = the whole sample
    double viewStart = 0.0, viewLength = 0.0;
    std::int64_t cursor = -1; // the last frame clicked in the wave (where "Add marker" puts one without a selection)
    // the selection in frames, [selFrom, selTo); none when selTo <= selFrom (FL keeps it in beats: the editor converts)
    double selFrom = -1.0, selTo = -1.0;

    DeckView() { setWantsKeyboardFocus (true); }

    bool hasSelection() const { return selFrom >= 0.0 && selTo > selFrom; }
    void setSelection (double from, double to, bool done = true)
    {
        if (from > to)
            std::swap (from, to);
        const auto total = frames();
        from = std::clamp (from, 0.0, total);
        to = std::clamp (to, 0.0, total);
        if (to > from)
            selFrom = from, selTo = to;
        else
            selFrom = selTo = -1.0;
        repaint();
        if (selectionChanged)
            selectionChanged (done);
    }
    // (from the saved state: no notification)
    void showSelection (double from, double to)
    {
        selFrom = to > from && from >= 0.0 ? from : -1.0;
        selTo = to > from && from >= 0.0 ? to : -1.0;
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override { return key && key (k); }

    void zoomFull()
    {
        viewStart = viewLength = 0.0;
        changed();
    }

    void zoomBy (double factor, double anchorFrame = -1.0)
    {
        const auto total = frames();
        if (total <= 0.0)
            return;
        const auto length = shownLength();
        if (anchorFrame < 0.0)
            anchorFrame = viewStart + length * 0.5;
        const auto next = std::clamp (length * factor, std::min (total, 16.0), total);
        setView (anchorFrame - (anchorFrame - viewStart) * next / length, next);
    }

    void zoomOn (double from, double to)
    {
        if (to > from)
            setView (from, to - from);
    }

    // FL's "Zoom on left / right of selection": half a frame a pixel, centred on `frame`
    void zoomAt (double frame)
    {
        const auto length = (double) getWidth() / (double) scale * slicex::zoomEdgeFramesPerPixel;
        setView (frame - length * 0.5, length);
    }

    // (before the view has its sample: kept as given, clamped when read)
    void openAt (double start, double length)
    {
        viewStart = std::max (0.0, start);
        viewLength = std::max (0.0, length);
        repaint();
    }
    void scrollBy (double part) { setView (viewStart + shownLength() * part, shownLength()); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto data = sample ? sample() : nullptr;
        const auto* list = regions ? regions() : nullptr;
        g.setFont (theme::font (theme::type::caption));
        if (data == nullptr || data->frames() == 0)
        {
            g.setColour (theme::colour::textSecondary);
            g.drawText (deck == 0 ? tr ("Deck A: drop a sample here (or File menu)") : tr ("Deck B: drop a sample here (or File menu)"),
                        getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto total = (double) data->frames();
        const auto v0 = shownStart(), length = shownLength();
        const auto bar = barArea(), tabs = tabArea(), wave = waveArea();

        // the zoom / scroll bar: the whole sample small, the part shown lit
        g.setColour (theme::colour::bg2);
        g.fillRect (bar);
        const auto thumb = thumbArea();
        g.setColour (theme::colour::bg5);
        g.fillRect (thumb);
        g.setColour (deckColour (deck).withMultipliedSaturation (0.6f).withAlpha (0.8f));
        drawWave (g, *data, bar.reduced (0.0f, 1.0f), 0.0, total);
        if (hasSelection()) // (FL tints the selected stretch in the scroller too)
        {
            g.setColour (theme::colour::textPrimary.withAlpha (0.28f));
            g.fillRect (juce::Rectangle<float> ((float) (selFrom / total) * bar.getWidth(), bar.getY() + 1.0f,
                                                std::max (1.0f, (float) ((selTo - selFrom) / total) * bar.getWidth()), bar.getHeight() - 2.0f));
        }
        g.setColour (theme::colour::borderStrong);
        g.drawRect (thumb, 1.0f);

        g.setColour (theme::colour::bg1);
        g.fillRect (tabs);
        const auto sel = selected ? selected() : -1;
        if (list != nullptr && sel >= 0 && sel < (int) list->size())
        {
            const auto& s = (*list)[(std::size_t) sel];
            const auto x0 = std::max (wave.getX(), xOf ((double) startOf (*list, sel))), x1 = std::min (wave.getRight(), xOf ((double) s.end + 1.0));
            if (x1 > x0)
            {
                g.setColour (deckColour (deck).withAlpha (0.2f));
                g.fillRect (juce::Rectangle<float> (x0, tabs.getY(), std::max (1.0f, x1 - x0), wave.getBottom() - tabs.getY()));
            }
        }
        g.setColour (deckColour (deck).brighter (0.15f));
        drawWave (g, *data, wave, v0, length);
        // the selection: a lit stretch with the wave drawn light in it (FL: red with a pink wave)
        if (hasSelection() && selTo > v0 && selFrom < v0 + length)
        {
            const auto x0 = std::max (wave.getX(), xOf (selFrom)), x1 = std::min (wave.getRight(), xOf (selTo));
            const juce::Rectangle<float> lit (x0, wave.getY(), std::max (1.0f, x1 - x0), wave.getHeight());
            g.setColour (theme::colour::textPrimary.withAlpha (0.2f));
            g.fillRect (lit);
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (lit.toNearestInt());
                g.setColour (theme::colour::textPrimary);
                drawWave (g, *data, wave, v0, length);
            }
        }
        if (cursor >= 0 && (double) cursor >= v0 && (double) cursor < v0 + length)
        {
            g.setColour (theme::colour::textPrimary.withAlpha (0.55f));
            g.drawVerticalLine ((int) xOf ((double) cursor), wave.getY(), wave.getBottom());
        }
        // the regions: a line at each start, its tab with the name (and its own key at the bottom)
        if (list != nullptr)
            for (std::size_t i = 0; i < list->size(); ++i)
            {
                const auto x = xOf ((double) startOf (*list, (int) i));
                auto next = wave.getRight();
                for (std::size_t j = i + 1; j < list->size(); ++j)
                    if ((int) j != dragging && (*list)[j].start > (*list)[i].start)
                    {
                        next = std::min (next, xOf ((double) (*list)[j].start));
                        break;
                    }
                if (x < wave.getX() - 1.0f || x > wave.getRight())
                {
                    if (next <= wave.getX() || x > wave.getRight())
                        continue;
                }
                const auto lit = (int) i == sel || (int) i == dragging;
                if (x >= wave.getX() - 1.0f)
                {
                    g.setColour (lit ? theme::colour::textPrimary : deckColour (deck).withMultipliedSaturation (0.8f));
                    g.drawVerticalLine ((int) x, tabs.getY(), wave.getBottom());
                    g.fillRect (juce::Rectangle<float> (x, tabs.getY(), tabWidth, tabs.getHeight() - 2.0f)); // the tab (the handle)
                }
                // a region's end has its own tag, left of where it stops (FL's second tag; it is dragged like the
                // first); the line is drawn where the region stops short of the next one or runs past it
                if (! (*list)[i].marker)
                {
                    const auto endX = xOf ((double) endOf (*list, (int) i) + 1.0);
                    if (endX > wave.getX() && endX <= wave.getRight() + 1.0f)
                    {
                        g.setColour (deckColour (deck).withMultipliedSaturation (0.8f).withAlpha (0.7f));
                        if (std::abs (endX - next) > 1.5f)
                            g.drawVerticalLine ((int) endX, tabs.getY(), wave.getBottom());
                        g.fillRect (juce::Rectangle<float> (endX - tabWidth - 1.0f, tabs.getY(), tabWidth, tabs.getHeight() - 2.0f));
                    }
                }
                const auto textX = std::max (x, wave.getX()) + tabWidth + 2.0f;
                if (next - textX > 10.0f)
                {
                    g.setColour (lit ? theme::colour::textPrimary : theme::colour::textSecondary);
                    const auto name = juce::String::fromUTF8 (slicex::regionName ((*list)[i], (int) i).c_str());
                    g.drawText (name, juce::Rectangle<float> (textX, tabs.getY(), next - textX - 1.0f, tabs.getHeight()), juce::Justification::centredLeft, false);
                    if ((*list)[i].key >= 0) // a region with a trigger note of its own shows it
                    {
                        g.setColour (theme::colour::accentHover);
                        g.drawText (keyName ((*list)[i].key), juce::Rectangle<float> (textX, wave.getBottom() - 13.0f, next - textX - 1.0f, 12.0f), juce::Justification::centredLeft, false);
                    }
                }
            }
        // the loop points (FL's View ▸ Loop, on after "Set loop"): a line at each, their tags at the bottom, "Loop" at the start
        std::int64_t loopFirst = 0, loopLast = 0;
        if (loop && loop (loopFirst, loopLast))
        {
            const auto tagH = std::round (9.0f * scale);
            g.setColour (theme::colour::warning);
            const auto a = xOf ((double) loopFirst), b = xOf ((double) loopLast + 1.0);
            if (a >= wave.getX() - 1.0f && a <= wave.getRight())
            {
                g.fillRect (juce::Rectangle<float> (a, wave.getY(), 2.0f, wave.getHeight()));
                g.fillRect (juce::Rectangle<float> (a, wave.getBottom() - tagH, tabWidth, tagH));
                g.drawText (tr ("Loop"), juce::Rectangle<float> (a + tabWidth + 3.0f, wave.getBottom() - tagH - 3.0f, 60.0f, tagH + 3.0f), juce::Justification::centredLeft, false);
            }
            if (b >= wave.getX() && b <= wave.getRight() + 1.0f)
            {
                g.drawVerticalLine ((int) b - 1, wave.getY(), wave.getBottom());
                g.fillRect (juce::Rectangle<float> (b - tabWidth, wave.getBottom() - tagH, tabWidth, tagH));
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        auto cursorType = juce::MouseCursor::NormalCursor;
        if (barArea().contains (p))
        {
            const auto t = thumbArea();
            if (std::abs (p.x - t.getX()) < edge || std::abs (p.x - t.getRight()) < edge)
                cursorType = juce::MouseCursor::LeftRightResizeCursor;
        }
        else if (tabArea().contains (p) && (endTabAt (p.x) >= 0 || tabAt (p.x) >= 0) && ! (frozen && frozen()))
            cursorType = juce::MouseCursor::LeftRightResizeCursor;
        setMouseCursor (cursorType);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        mode = Mode::none;
        dragging = -1;
        dragEnd = false;
        if (frames() <= 0.0)
            return;
        grabKeyboardFocus();
        const auto p = e.position;
        if (barArea().contains (p))
        {
            if (e.mods.isPopupMenu())
                return;
            const auto t = thumbArea();
            const auto frame = (double) p.x / (double) getWidth() * frames();
            if (std::abs (p.x - t.getX()) < edge)
                mode = Mode::zoomLeft;
            else if (std::abs (p.x - t.getRight()) < edge)
                mode = Mode::zoomRight;
            else
            {
                if (! t.contains (p)) // outside the lit part: it jumps there
                    setView (frame - shownLength() * 0.5, shownLength());
                mode = Mode::scroll;
                grab = frame - shownStart();
            }
            return;
        }
        if (tabArea().contains (p))
        {
            const auto endTab = endTabAt (p.x);
            const auto tab = endTab >= 0 ? endTab : tabAt (p.x);
            const auto hit = tab >= 0 ? tab : regionAt (p.x);
            if (e.mods.isPopupMenu())
            {
                if (menu)
                    menu (hit);
                return;
            }
            if (select && hit >= 0)
                select (hit, false);
            if (tab >= 0 && ! (frozen && frozen()))
            {
                // the tag moves by the mouse's own distance, from where it was (FL: 15 px left = 7412 frames less)
                mode = Mode::tab;
                dragging = tab;
                dragEnd = endTab >= 0;
                const auto& r = (*regions())[(std::size_t) tab];
                dragTo = dragEnd ? r.end + 1 : r.start;
                grab = frameAt (p.x) - (double) dragTo;
                moved = false;
            }
            return;
        }
        if (e.mods.isPopupMenu())
        {
            if (menu)
                menu (regionAt (p.x));
            return;
        }
        // the wave: FL's selection. Without one a drag makes it; with one the nearer edge goes to the mouse (a click
        // alone moves it) and follows it. A plain click without a selection picks the region and plays it (LYNX).
        const auto frame = std::clamp (frameAt (p.x), 0.0, frames());
        mode = Mode::select;
        moved = false;
        hadSelection = hasSelection();
        if (hadSelection)
        {
            anchor = std::abs (frame - selFrom) <= std::abs (frame - selTo) ? selTo : selFrom;
            setSelection (anchor, frame, false);
        }
        else
            anchor = frame;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto total = frames();
        if (total <= 0.0)
            return;
        const auto barFrame = std::clamp ((double) e.position.x / (double) getWidth() * total, 0.0, total);
        const auto v0 = shownStart(), v1 = v0 + shownLength();
        if (mode == Mode::scroll)
            setView (barFrame - grab, shownLength());
        else if (mode == Mode::zoomLeft)
            setView (std::min (barFrame, v1 - 16.0), v1 - std::min (barFrame, v1 - 16.0));
        else if (mode == Mode::zoomRight)
            setView (v0, std::max (16.0, barFrame - v0));
        else if (mode == Mode::tab && dragging >= 0)
        {
            if (! moved && std::abs (e.getDistanceFromDragStartX()) < 3)
                return;
            moved = true;
            const auto& r = (*regions())[(std::size_t) dragging];
            auto frame = (std::int64_t) std::llround (frameAt (e.position.x) - grab);
            if (dragEnd) // the first frame after the region: past its start, up to the sample's end
                dragTo = std::clamp<std::int64_t> (frame, r.start + 1, (std::int64_t) total);
            else
            {
                frame = std::clamp<std::int64_t> (frame, 0, (std::int64_t) total - 1);
                if (snap)
                    frame = snap (dragging, frame);
                dragTo = std::clamp<std::int64_t> (frame, 0, (std::int64_t) total - 1);
            }
            repaint();
        }
        else if (mode == Mode::select)
        {
            if (! moved && ! hadSelection && std::abs (e.getDistanceFromDragStartX()) < 3)
                return;
            moved = true;
            setSelection (anchor, std::clamp (frameAt (e.position.x), 0.0, total), false);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (mode == Mode::tab && dragging >= 0 && moved && (dragEnd ? moveEnd != nullptr : moveStart != nullptr))
        {
            const auto index = dragging;
            const auto end = dragEnd;
            dragging = -1;
            dragEnd = false;
            mode = Mode::none;
            if (end)
                moveEnd (index, dragTo);
            else
                moveStart (index, dragTo);
            return;
        }
        if (mode == Mode::select)
        {
            mode = Mode::none;
            if (hadSelection || moved)
            {
                if (selectionChanged)
                    selectionChanged (true);
            }
            else if (e.getNumberOfClicks() < 2)
            {
                cursor = (std::int64_t) std::clamp (frameAt (e.position.x), 0.0, frames() - 1.0);
                if (select)
                    select (regionAt (e.position.x), true);
            }
        }
        dragging = -1;
        dragEnd = false;
        mode = Mode::none;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (barArea().contains (e.position))
            zoomFull();
        else if (waveArea().contains (e.position) && hasSelection()) // (FL: a double click in the wave deselects)
            setSelection (-1.0, -1.0);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (frames() <= 0.0 || w.deltaY == 0.0f)
            return;
        if (e.mods.isShiftDown())
            scrollBy (w.deltaY > 0.0f ? -0.125 : 0.125);
        else
            zoomBy (w.deltaY > 0.0f ? 0.8 : 1.25, barArea().contains (e.position) ? -1.0 : frameAt (e.position.x));
    }

    // the frames shown, as [from, to)
    std::pair<double, double> shown() const { return { shownStart(), shownStart() + shownLength() }; }

private:
    enum class Mode { none, scroll, zoomLeft, zoomRight, tab, select };
    static constexpr float edge = 5.0f, tabWidth = 5.0f;

    double frames() const
    {
        const auto data = sample ? sample() : nullptr;
        return data != nullptr ? (double) data->frames() : 0.0;
    }
    double shownLength() const
    {
        const auto total = frames();
        return viewLength > 0.0 && viewLength < total ? viewLength : total;
    }
    double shownStart() const { return std::clamp (viewStart, 0.0, std::max (0.0, frames() - shownLength())); }

    void setView (double start, double length)
    {
        const auto total = frames();
        length = std::clamp (length, std::min (total, 16.0), total);
        viewLength = length >= total ? 0.0 : length;
        viewStart = std::clamp (start, 0.0, std::max (0.0, total - length));
        changed();
    }

    void changed()
    {
        repaint();
        if (viewChanged)
            viewChanged();
    }

    juce::Rectangle<float> barArea() const { return getLocalBounds().toFloat().removeFromTop (std::round (14.0f * scale)); }
    juce::Rectangle<float> tabArea() const { return getLocalBounds().toFloat().withTrimmedTop (barArea().getHeight()).removeFromTop (std::round (12.0f * scale)); }
    juce::Rectangle<float> waveArea() const { return getLocalBounds().toFloat().withTrimmedTop (barArea().getHeight() + tabArea().getHeight()); }
    juce::Rectangle<float> thumbArea() const
    {
        const auto bar = barArea();
        const auto total = std::max (1.0, frames());
        const auto x0 = (float) (shownStart() / total) * bar.getWidth(), x1 = (float) ((shownStart() + shownLength()) / total) * bar.getWidth();
        return { x0, bar.getY(), std::max (3.0f, x1 - x0), bar.getHeight() };
    }

    // FL scrolls the deck by whole pixels of its own 714-pixel deck: a zoomed deck's left edge is the shown start
    // taken down to a whole number of those pixels at the current zoom (round 190aj: both ends of a selection dragged
    // in a deck shown from beat 0.5763 came out 0.12 pixel earlier than a plain linear map gives; one zoom measured).
    // LYNX's deck is that deck scaled, so the step is 1/714 of the shown length whatever the width.
    double originFrame() const
    {
        const auto step = shownLength() / 714.0;
        return step > 0.0 ? std::floor (shownStart() / step) * step : 0.0;
    }
    float xOf (double frame) const { return (float) ((frame - originFrame()) / shownLength() * (double) getWidth()); }
    double frameAt (float x) const { return originFrame() + (double) x / (double) std::max (1, getWidth()) * shownLength(); }
    // a region's start as drawn (the one being dragged follows the mouse)
    std::int64_t startOf (const std::vector<slicex::WavRegion>& list, int i) const { return i == dragging && moved && ! dragEnd ? dragTo : list[(std::size_t) i].start; }
    // a region's last frame as drawn
    std::int64_t endOf (const std::vector<slicex::WavRegion>& list, int i) const { return i == dragging && moved && dragEnd ? dragTo - 1 : list[(std::size_t) i].end; }

    // min / max per pixel column (both channels folded) of the frames [from, from + length) across `area`
    void drawWave (juce::Graphics& g, const SampleData& data, juce::Rectangle<float> area, double from, double length) const
    {
        const auto* left = data.leftData();
        const auto* right = data.isStereo() ? data.rightData() : nullptr;
        const auto total = (std::int64_t) data.frames();
        const auto mid = area.getCentreY(), half = area.getHeight() * 0.47f;
        const auto width = (int) area.getWidth();
        const auto step = std::max<std::int64_t> (1, (std::int64_t) (length / (double) std::max (1, width) / 64.0));
        float last = mid;
        for (int x = 0; x < width; ++x)
        {
            const auto a = (std::int64_t) (from + length * x / width), b = std::max (a + 1, (std::int64_t) (from + length * (x + 1) / width));
            float lo = 1.0f, hi = -1.0f;
            for (auto i = std::max<std::int64_t> (0, a); i < b && i < total; i += step)
            {
                lo = std::min (lo, right != nullptr ? std::min (left[i], right[i]) : left[i]);
                hi = std::max (hi, right != nullptr ? std::max (left[i], right[i]) : left[i]);
            }
            if (lo > hi)
                continue;
            // (zoomed in past a frame a column, the columns join up into a line)
            const auto y0 = std::min (mid - hi * half, last), y1 = std::max (mid - lo * half, last);
            g.drawVerticalLine ((int) area.getX() + x, y0, y1 + 1.0f);
            last = mid - (hi + lo) * 0.5f * half;
        }
    }

    // the region whose tab is at x (the last one that starts there), −1 = none. The tab and the name beside it grab
    // (FL: a drag that began 12 px right of a region's line, on its name, moved its start)
    int tabAt (float x) const
    {
        const auto* list = regions ? regions() : nullptr;
        if (list == nullptr || frames() <= 0.0)
            return -1;
        const auto font = theme::font (theme::type::caption);
        int hit = -1;
        for (std::size_t i = 0; i < list->size(); ++i)
        {
            const auto at = xOf ((double) (*list)[i].start);
            auto reach = at + tabWidth + 2.0f;
            if (x > reach && x >= at) // the name, as far as it is drawn (up to the next region's line)
            {
                auto next = (float) getWidth();
                for (std::size_t j = i + 1; j < list->size(); ++j)
                    if ((*list)[j].start > (*list)[i].start)
                    {
                        next = std::min (next, xOf ((double) (*list)[j].start));
                        break;
                    }
                const auto name = juce::String::fromUTF8 (slicex::regionName ((*list)[i], (int) i).c_str());
                reach = std::min (next - tabWidth - 3.0f, at + tabWidth + 2.0f + (float) juce::GlyphArrangement::getStringWidthInt (font, name));
            }
            if (x >= at - 2.0f && x <= reach)
                hit = (int) i;
        }
        return hit;
    }

    // the region whose end tab is at x (left of where the region stops), −1 = none
    int endTabAt (float x) const
    {
        const auto* list = regions ? regions() : nullptr;
        if (list == nullptr || frames() <= 0.0)
            return -1;
        int hit = -1;
        for (std::size_t i = 0; i < list->size(); ++i)
            if (! (*list)[i].marker)
            {
                const auto at = xOf ((double) (*list)[i].end + 1.0);
                if (x >= at - tabWidth - 3.0f && x < at)
                    hit = (int) i;
            }
        return hit;
    }

    int regionAt (float x) const
    {
        const auto* list = regions ? regions() : nullptr;
        if (list == nullptr || frames() <= 0.0)
            return -1;
        const auto frame = (std::int64_t) frameAt (x);
        int hit = -1;
        for (std::size_t i = 0; i < list->size(); ++i)
            if ((*list)[i].start <= frame)
                hit = (int) i;
        // (between a region's end and the next start there is no region)
        return hit >= 0 && frame > (*list)[(std::size_t) hit].end ? -1 : hit;
    }

    Mode mode = Mode::none;
    int dragging = -1;
    std::int64_t dragTo = 0;
    double grab = 0.0, anchor = 0.0;
    bool moved = false, dragEnd = false, hadSelection = false;
};

// ---------------------------------------------------------------------------------------------------------------
// FL's KbPanel: the keyboard with the keys that have regions lit (deck A / deck B colours); click plays, right click
// opens the key's menu
class Keyboard final : public juce::Component
{
public:
    int base = 36; // the first key shown (FL's BaseNoteSel moves it by octaves)
    static constexpr int octaves = 7;
    std::function<int (int)> decksOn;       // key → bit 0 deck A, bit 1 deck B
    std::function<bool (int)> current;      // the key plays the region shown
    std::function<void (int, bool)> play;   // key, on
    std::function<void (int)> menu;

    // FL draws the keyboard as two rows of pads: the black keys above, the white keys below
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        const auto w = pitch();
        static const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
        for (int i = 0; i < octaves * 7; ++i)
        {
            const auto key = base + (i / 7) * 12 + whites[i % 7];
            if (key > 127)
                break;
            pad (g, key, whiteCell (i), false);
            if (const auto n = i % 7; n != 2 && n != 6 && key + 1 <= 127)
                pad (g, key + 1, blackCell (i), true);
        }
        juce::ignoreUnused (w);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto key = keyAt (e.position);
        if (e.mods.isPopupMenu())
        {
            if (menu)
                menu (key);
            return;
        }
        down = key;
        if (play)
            play (key, true);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (down >= 0 && play)
            play (down, false);
        down = -1;
        repaint();
    }

private:
    float pitch() const { return (float) getWidth() / (float) (octaves * 7); }
    juce::Rectangle<float> whiteCell (int i) const
    {
        const auto w = pitch(), h = (float) getHeight();
        return juce::Rectangle<float> (w * (float) i, h * 0.56f, w, h * 0.34f).reduced (w * 0.14f, 0.0f);
    }
    juce::Rectangle<float> blackCell (int i) const // the black key right of white key i
    {
        const auto w = pitch(), h = (float) getHeight();
        return juce::Rectangle<float> (w * ((float) i + 0.5f), h * 0.14f, w, h * 0.34f).reduced (w * 0.14f, 0.0f);
    }

    // a key's pad: lit in its deck's colour when it has regions (both decks: halves), outlined when it plays the
    // region shown
    void pad (juce::Graphics& g, int key, juce::Rectangle<float> at, bool black) const
    {
        const auto decks = decksOn ? decksOn (key) : 0;
        g.setColour (key == down ? theme::colour::accentHover : (black ? juce::Colour (0xff1a1b1f) : juce::Colour (0xffd9dbe0)));
        g.fillRoundedRectangle (at, 1.5f);
        if (decks != 0 && key != down)
        {
            const auto both = decks == 3;
            if ((decks & 1) != 0)
            {
                g.setColour (deckColour (0).withMultipliedSaturation (0.75f).brighter (black ? 0.0f : 0.5f));
                g.fillRoundedRectangle (both ? at.withWidth (at.getWidth() * 0.5f) : at, 1.5f);
            }
            if ((decks & 2) != 0)
            {
                g.setColour (deckColour (1).withMultipliedSaturation (0.75f).brighter (black ? 0.0f : 0.5f));
                g.fillRoundedRectangle (both ? at.withTrimmedLeft (at.getWidth() * 0.5f) : at, 1.5f);
            }
        }
        if (black)
        {
            g.setColour (theme::colour::borderStrong);
            g.drawRoundedRectangle (at, 1.5f, 1.0f);
        }
        if (current && current (key))
        {
            g.setColour (theme::colour::textPrimary);
            g.drawRoundedRectangle (at.expanded (1.0f), 2.0f, 1.2f);
        }
    }

    int keyAt (juce::Point<float> p) const
    {
        static const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
        const auto w = pitch();
        if (p.y < (float) getHeight() * 0.52f)
        {
            const auto i = (int) std::floor (p.x / w - 0.5f);
            if (i >= 0 && i < octaves * 7 && i % 7 != 2 && i % 7 != 6)
                return std::min (127, base + (i / 7) * 12 + whites[i % 7] + 1);
        }
        const auto i = std::clamp ((int) (p.x / w), 0, octaves * 7 - 1);
        return std::min (127, base + (i / 7) * 12 + whites[i % 7]);
    }

    int down = -1;
};

// ---------------------------------------------------------------------------------------------------------------
class SlicexEditor final : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener,
                           private juce::Timer
{
    // a control bound to one stored integer of the state
    struct Bound
    {
        juce::Slider* slider;
        std::function<int (const State&)> get;
    };

public:
    SlicexEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        // ---- the masters (FL: MainVolSlider, MainRandSlider, MainLFOSlider, MainPitchSlider)
        vertical (masterVol, tr ("Master level"), 0, 16000, 12800, slicex::mVolume);
        vertical (masterRand, tr ("Master randomness level"), 0, 256, 256, slicex::mRandom);
        vertical (masterLfo, tr ("Master LFO level"), -128, 128, 0, slicex::mLfo);
        vertical (masterPitch, tr ("Master pitch"), -1200, 1200, 0, slicex::mPitch);
        masterVol.textFromValueFunction = [] (double v) { return juce::String (std::lround (slicex::masterVolume ((std::int32_t) v) * 100.0f)) + "%"; };
        masterRand.textFromValueFunction = [] (double v) { return juce::String (std::lround (v * 100.0 / 256.0)) + "%"; };
        masterLfo.textFromValueFunction = [] (double v) { return juce::String (std::lround (v * 100.0 / 128.0)) + "%"; };
        masterPitch.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " " + tr ("cents"); };

        // ---- Mod X / Y
        pad.onChange = [this] (int x, int y) {
            edit ([x, y] (State& s) {
                s.masters[slicex::mModX] = x;
                s.masters[slicex::mModY] = y;
            }, false);
        };
        ui::setHint (pad, tr ("X / Y modulation"));
        addAndMakeVisible (pad);
        wheel (modX, tr ("Modulation X"), 0, 512, 256, [] (const State& s) { return s.masters[slicex::mModX]; },
               [] (State& s, int v) { s.masters[slicex::mModX] = v; });
        wheel (modY, tr ("Modulation Y"), 0, 512, 256, [] (const State& s) { return s.masters[slicex::mModY]; },
               [] (State& s, int v) { s.masters[slicex::mModY] = v; });
        header (modulationLabel, "MODULATION");
        caption (modXLabel, "X");
        caption (modYLabel, "Y");
        check (modSmooth, "SMOOTH", tr ("Smooth up modulation"), slicex::oModSmooth, true);

        ui::setHint (optionsButton, tr ("Options"), true);
        optionsButton.onClick = [this] { optionsMenu(); };
        addAndMakeVisible (optionsButton);

        // ---- layering
        header (layeringLabel, "LAYERING");
        for (const auto* name : { "All", "All (gain-compensated)", "Velocity-mapped", "Modulation X-mapped", "Modulation Y-mapped", "Random", "Cycle", "Song time-synced" })
            layering.addItem (tr (name), layering.getNumItems() + 1);
        ui::setHint (layering, tr ("Drum layering mode"));
        layering.onChange = [this] {
            if (! loading)
                edit ([v = layering.getSelectedId() - 1] (State& s) { s.setOptionInt (slicex::oLayering, v); });
        };
        addAndMakeVisible (layering);
        check (sameDeck, "SAME DECK", tr ("Layer regions of same deck only"), slicex::oSameDeckOnly);
        caption (crossfadeLabel, "CROSSFADE");
        crossfade.items = { "0", "1", "2", "3", "4" };
        // FL's five curve icons: none (a step), linear, balanced curve, unbalanced curve, adding
        crossfade.drawCell = [] (juce::Graphics& g, int i, juce::Rectangle<float> r) {
            juce::Path p;
            for (int k = 0; k <= 12; ++k)
            {
                const auto t = (float) k / 12.0f;
                float y = t;
                if (i == 0)
                    y = t < 0.5f ? 0.0f : 1.0f;
                else if (i == 2)
                    y = (1.0f - std::cos (t * juce::MathConstants<float>::pi)) * 0.5f;
                else if (i == 3)
                    y = 1.0f - (std::exp ((1.0f - t) * std::log (9.0f)) - 1.0f) * 0.125f;
                else if (i == 4)
                    y = t >= 0.5f ? 1.0f : (1.0f - std::cos (2.0f * t * juce::MathConstants<float>::pi)) * 0.5f;
                const auto at = juce::Point<float> (r.getX() + r.getWidth() * t, r.getBottom() - r.getHeight() * y);
                k == 0 ? p.startNewSubPath (at) : p.lineTo (at);
            }
            g.strokePath (p, juce::PathStrokeType (1.3f));
        };
        crossfade.onChange = [this] (int v) { edit ([v] (State& s) { s.setOptionInt (slicex::oCrossfade, v); }); };
        ui::setHint (crossfade, tr ("Crossfade") + ": " + tr ("None") + " / " + tr ("Linear") + " / " + tr ("Balanced curve") + " / "
                                    + tr ("Unbalanced curve") + " / " + tr ("Adding"));
        addAndMakeVisible (crossfade);
        check (autoDump, "AUTO-DUMP", tr ("Dump deck A to piano roll after editing"), slicex::oAutoDump);
        brand.setText ("SLICEX", juce::dontSendNotification);
        brand.setFont (theme::font (22.0f, true));
        brand.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (brand);

        // ---- region settings
        header (regionHeader, "REGION SETTINGS");
        ui::setHint (regionMenuButton, tr ("More region settings"), true);
        regionMenuButton.onClick = [this] { regionMenu (true); };
        addAndMakeVisible (regionMenuButton);
        ui::setHint (regionSel, tr ("Current region"));
        regionSel.onChange = [this] {
            if (! loading && regionSel.getSelectedId() > 0)
                selectRegion ((regionSel.getSelectedId() - 1) >> 16, (regionSel.getSelectedId() - 1) & 0xffff, false);
        };
        addAndMakeVisible (regionSel);
        digit (output, tr ("Output track offset"), -99, 99, 0, slicex::rOutput, [] (int v) { return v == 0 ? juce::String ("--") : juce::String (v); });
        digit (cutGroup, tr ("Cut group"), 0, 99, 0, slicex::rCutGroup, [] (int v) { return v == 0 ? juce::String ("--") : juce::String (v); });
        caption (outLabel, "OUT");
        caption (cutLabel, "CUT");
        struct Row { const char* name; bool a; const char* hintA; const char* hintB; int fa, fb, lo_a, hi_a, def_a, lo_b, hi_b, def_b, group, amount;
                     const char* hintGroup; const char* hintAmount; };
        const Row rows[4] = {
            { "AMP", true, "Panning", "Volume", slicex::rPan, slicex::rVolume, -6400, 6400, 0, 0, 16000, 8000, slicex::rAmpGroup, slicex::rAmpAmount,
              "Amp articulator group", "Amp articulator amount" },
            { "FILTER", true, "Filter cutoff frequency", "Filter resonance", slicex::rCutoff, slicex::rResonance, 0, 256, 128, 0, 256, 128, slicex::rFilterGroup,
              slicex::rFilterAmount, "Filter articulator group", "Filter articulator amount" },
            { "SPEED", false, nullptr, "Pitch", -1, slicex::rPitch, 0, 0, 0, -1200, 1200, 0, slicex::rSpeedGroup, slicex::rSpeedAmount,
              "Speed articulator group", "Speed articulator amount" },
            { "START", true, "Delay", "Sample start", slicex::rDelay, slicex::rStart, 0, 12800, 0, 0, 12800, 0, slicex::rStartGroup, slicex::rStartAmount,
              "Sample start articulator group", "Sample start articulator amount" },
        };
        for (int i = 0; i < 4; ++i)
        {
            const auto& r = rows[i];
            if (r.a)
                regionWheel (rowA[(std::size_t) i], tr (r.hintA), r.lo_a, r.hi_a, r.def_a, r.fa);
            regionWheel (rowB[(std::size_t) i], tr (r.hintB), r.lo_b, r.hi_b, r.def_b, r.fb);
            caption (rowLabel[(std::size_t) i], r.name);
            rowLabel[(std::size_t) i].setJustificationType (juce::Justification::centredLeft);
            caption (artLabel[(std::size_t) i], "ART");
            // the group is stored −1…7 and shown 0 (none) … 8
            auto& d = rowGroup[(std::size_t) i];
            d.lo = 0;
            d.hi = 8;
            d.reset = 1;
            d.text = [] (int v) { return v == 0 ? juce::String ("-") : juce::String (v); };
            d.onChange = [this, f = r.group] (int v) { editRegion ([f, v] (slicex::RegionRecord& rec) { rec[(std::size_t) f] = v - 1; }); };
            ui::setHint (d, tr (r.hintGroup));
            addAndMakeVisible (d);
            regionWheel (rowAmount[(std::size_t) i], tr (r.hintAmount), 0, 12800, 12800, r.amount);
        }

        // ---- the articulator group, its filter and the articulator editor
        for (int i = 1; i <= 8; ++i)
            artGroup.items.add (juce::String (i));
        artGroup.onChange = [this] (int v) { edit ([v] (State& s) { s.setOptionInt (slicex::oArtGroup, v); }, false); };
        ui::setHint (artGroup, tr ("Articulator group"));
        addAndMakeVisible (artGroup);
        header (artGroupLabel, "ARTICULATOR");
        filterType.items = { "LP", "BP", "BS", "HP" };
        filterType.onChange = [this] (int v) { editGroup ([v] (slicex::Group& g) { g.header[3] = v; }); };
        ui::setHint (filterType, tr ("Filter type"));
        addAndMakeVisible (filterType);
        filterOrder.items = { "OFF", "x1", "x2", "x3" };
        filterOrder.onChange = [this] (int v) { editGroup ([v] (slicex::Group& g) { g.header[4] = v; }); };
        ui::setHint (filterOrder, tr ("Filter mode"));
        addAndMakeVisible (filterOrder);
        groupWheel (filterEnv, tr ("Filter cutoff envelope / LFO amount"), 0);
        groupWheel (filterCut, tr ("Filter cutoff frequency"), 1);
        groupWheel (filterRes, tr ("Filter resonance"), 2);
        caption (filterEnvLabel, "ENV");
        caption (filterCutLabel, "CUT");
        caption (filterResLabel, "RES");
        header (filterLabel, "FILTER");

        target.items = { "PAN", "VOL", "CUT", "RES", "SPEED", "START" };
        target.onChange = [this] (int v) { edit ([v] (State& s) { s.setOptionInt (slicex::oArtTarget, v); }, false); };
        target.marked = [this] (int i) {
            const auto& a = group().art[(std::size_t) i];
            return a.envelope.on() || a.lfo.on();
        };
        ui::setHint (target, tr ("Editor target"));
        addAndMakeVisible (target);
        partSel.items = { "ENV", "LFO", "VEL", "MOD X", "MOD Y", "RAND" };
        partSel.onChange = [this] (int v) { edit ([v] (State& s) { s.setOptionInt (slicex::oArtPart, v); }, false); };
        partSel.marked = [this] (int i) { return i == 0 ? art().envelope.on() : (i == 1 ? art().lfo.on() : false); };
        ui::setHint (partSel, tr ("Articulator part"));
        addAndMakeVisible (partSel);
        curveView.curve = [this] { return &shownCurve(); };
        curveView.articulator = [this] { return &art(); };
        curveView.part = [this] { return part(); };
        curveView.change = [this] (const std::function<void (slicex::Curve&)>& f, bool undoable) {
            edit ([this, &f] (State& s) { f (curveOf (s)); }, undoable);
        };
        addAndMakeVisible (curveView);
        ui::setHint (curveOn, tr ("Enable envelope / LFO"));
        curveOn.onClick = [this] {
            edit ([this, on = curveOn.getToggleState()] (State& s) { curveOf (s).enabled = on ? -1 : 0; });
        };
        addAndMakeVisible (curveOn);
        curveTempo.setButtonText ("TEMPO");
        ui::setHint (curveTempo, tr ("Tempo-based time"));
        curveTempo.onClick = [this] {
            edit ([this, on = curveTempo.getToggleState()] (State& s) {
                auto& c = curveOf (s);
                c.flags = on ? (c.flags | 1) : (c.flags & ~1);
            });
        };
        addAndMakeVisible (curveTempo);
        curveGlobal.setButtonText ("GLOBAL");
        ui::setHint (curveGlobal, tr ("Global LFO phase (runs with the song)"));
        curveGlobal.onClick = [this] {
            edit ([this, on = curveGlobal.getToggleState()] (State& s) {
                auto& c = curveOf (s);
                c.flags = on ? (c.flags | 2) : (c.flags & ~2);
            });
        };
        addAndMakeVisible (curveGlobal);
        static const char* const envNames[] = { "ATT", "DEC", "SUS", "REL" };
        static const char* const envHints[] = { "Attack time scale", "Decay time scale", "Sustain level offset", "Release time scale" };
        for (int i = 0; i < 4; ++i)
        {
            artWheel (envKnob[(std::size_t) i], tr (envHints[i]), 0, 256, i == 2 ? 0 : 128, [i] (const slicex::Articulator& a) { return a.envKnobs[(std::size_t) i]; },
                      [i] (slicex::Articulator& a, int v) { a.envKnobs[(std::size_t) i] = v; });
            caption (envKnobLabel[(std::size_t) i], envNames[i]);
        }
        static const char* const lfoNames[] = { "SPD", "TNS", "SKEW", "PW" };
        static const char* const lfoHints[] = { "LFO speed", "LFO tension", "LFO skew", "LFO pulse width" };
        static const int lfoLo[] = { 0, -128, -128, 0 }, lfoHi[] = { 65536, 128, 128, 128 }, lfoDef[] = { 31102, 0, 0, 0 };
        for (int i = 0; i < 4; ++i)
        {
            artWheel (lfoKnob[(std::size_t) i], tr (lfoHints[i]), lfoLo[i], lfoHi[i], lfoDef[i],
                      [i] (const slicex::Articulator& a) { return a.lfoKnobs[(std::size_t) i]; },
                      [i] (slicex::Articulator& a, int v) { a.lfoKnobs[(std::size_t) i] = v; });
            caption (lfoKnobLabel[(std::size_t) i], lfoNames[i]);
        }

        // ---- the deck tool row
        auto tool = [this] (juce::Button& b, const juce::String& hint, std::function<void()> click, bool hasMenu = false) {
            ui::setHint (b, hint, hasMenu);
            b.onClick = std::move (click);
            addAndMakeVisible (b);
        };
        tool (loopButton, tr ("Loop"), [this] { edit ([on = loopButton.getToggleState()] (State& s) { s.setOptionByte (slicex::oLoop, on ? 1 : 0); }, false); });
        loopButton.setClickingTogglesState (true);
        tool (playButton, tr ("Play / pause"), [this] { playDeck(); });
        tool (stopButton, tr ("Stop"), [this] { engine.previewSample (nullptr); });
        tool (fileButton, tr ("File"), [this] { fileMenu(); }, true);
        tool (regionsButton, tr ("Regions"), [this] { regionsMenu(); }, true);
        tool (selectButton, tr ("Select"), [this] { selectMenu(); }, true);
        tool (zoomButton, tr ("Zoom"), [this] { zoomMenu(); }, true);
        tool (dualButton, tr ("Display single / dual deck"), [this] { edit ([on = dualButton.getToggleState()] (State& s) { s.setOptionByte (slicex::oDualDeck, on ? 1 : 0); }, false); });
        dualButton.setClickingTogglesState (true);
        deckSel.items = { "A", "B" };
        deckSel.onChange = [this] (int v) { edit ([v] (State& s) { s.ui[0] = v; }, false); };
        ui::setHint (deckSel, tr ("Current deck"));
        addAndMakeVisible (deckSel);
        for (auto* lab : { &rateLabel, &tempoLabel, &nameLabel, &selLabel })
        {
            lab->setFont (theme::font (theme::type::caption));
            lab->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (*lab);
        }
        ui::setHint (rateLabel, tr ("Sample rate"));
        ui::setHint (tempoLabel, tr ("Tempo"));
        ui::setHint (nameLabel, tr ("Title"));
        selLabel.setJustificationType (juce::Justification::centredRight);

        // ---- the decks and the keyboard
        for (int d = 0; d < 2; ++d)
        {
            auto& v = deckView[(std::size_t) d];
            v.deck = d;
            v.sample = [this, d] { return samples[(std::size_t) d]; };
            v.regions = [this, d] { return &regions[(std::size_t) d]; };
            v.selected = [this, d] { return regionDeck() == d ? regionIndex() : -1; };
            v.select = [this, d] (int i, bool play) { selectRegion (d, i, play); };
            v.snap = [this, d] (int i, std::int64_t frame) { return snapStart (d, i, frame); };
            v.moveStart = [this, d] (int i, std::int64_t frame) { moveRegionStart (d, i, frame); };
            v.moveEnd = [this, d] (int i, std::int64_t end) { moveRegionEnd (d, i, end); };
            v.selectionChanged = [this, d] (bool done) { selectionMoved (d, done); };
            v.loop = [this, d] (std::int64_t& first, std::int64_t& last) {
                first = loops[(std::size_t) d].first;
                last = loops[(std::size_t) d].second;
                return loopShown[(std::size_t) d];
            };
            v.key = [this, d] (const juce::KeyPress& k) { return deckKey (d, k); };
            v.viewChanged = [this] { startTimer (300); }; // (the zoom goes into the state once it rests, as FL saves it)
            v.frozen = [this, d] { return frozen (d); };
            v.menu = [this, d] (int i) {
                if (i >= 0)
                    selectRegion (d, i, false);
                regionMenu (false);
            };
            addAndMakeVisible (v);
        }
        keyboard.decksOn = [this] (int key) {
            return key >= 0 && key < 128 ? ((maps[0][(std::size_t) key].empty() ? 0 : 1) | (maps[1][(std::size_t) key].empty() ? 0 : 2)) : 0;
        };
        keyboard.current = [this] (int key) { return key == keyOfRegion (regionDeck(), regionIndex()); };
        keyboard.play = [this] (int key, bool on) {
            if (channel >= 0)
                engine.sendLiveNote (channel, key, 0.78125f, on);
        };
        keyboard.menu = [this] (int key) { keyMenu (key); };
        addAndMakeVisible (keyboard);
        octave.lo = 0;
        octave.hi = 4;
        octave.reset = 3;
        octave.text = [] (int v) { return "C" + juce::String (v); };
        octave.onChange = [this] (int v) { edit ([v] (State& s) { s.ui[2] = v; }, false); };
        ui::setHint (octave, tr ("Keyboard base octave"));
        addAndMakeVisible (octave);

        engine.addChangeListener (this);
        reload();
        setSize ((int) std::lround (714.0f * scale), (int) std::lround (625.0f * scale));
    }

    ~SlicexEditor() override
    {
        if (isTimerRunning())
        {
            stopTimer();
            storeViews();
        }
        engine.removeChangeListener (this);
    }

    // ---- the editor test (LYNX --slicexedittest, round 190aj): one command on the current deck, its result as text.
    // Mouse commands take positions as fractions of the deck's width, so a script can repeat what was done in FL's
    // 714-pixel deck: drag a b | click a | dclick a (in the wave), tabdrag a b (in the tag row); menu <entry>;
    // addregion <name> | addmarker <name>; key <pgup|pgdn|home|end|up|left|right|ctrl+left|ctrl+right|ctrl+a>;
    // rest (the zoom goes into the state); state; savewav <file> (the deck's WAV as the state holds it);
    // load <file> (File > Load sample); regions (the deck's region list)
    juce::String test (const juce::StringArray& w)
    {
        const auto deck = currentDeck();
        auto& view = deckView[(std::size_t) deck];
        const auto cmd = w[0];
        const auto waveY = (float) view.getHeight() * 0.6f, tabY = std::round (14.0f * scale) + std::round (12.0f * scale) * 0.5f;
        auto at = [&view] (const juce::String& fraction, float y) { return juce::Point<float> ((float) (fraction.getDoubleValue() * (double) view.getWidth()), y); };
        auto event = [&view] (juce::Point<float> p, juce::Point<float> down, bool dragged, int clicks) {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, &view, &view, now, down, now, clicks, dragged);
        };
        auto gesture = [&] (juce::Point<float> from, juce::Point<float> to, int clicks) {
            view.mouseDown (event (from, from, false, clicks));
            if (to != from)
                for (int i = 1; i <= 8; ++i)
                    view.mouseDrag (event (from + (to - from) * ((float) i / 8.0f), from, true, clicks));
            if (clicks == 2)
                view.mouseDoubleClick (event (to, from, false, clicks));
            view.mouseUp (event (to, from, to != from, clicks));
        };
        if (cmd == "drag") gesture (at (w[1], waveY), at (w[2], waveY), 1);
        else if (cmd == "click") gesture (at (w[1], waveY), at (w[1], waveY), 1);
        else if (cmd == "dclick")
        {
            gesture (at (w[1], waveY), at (w[1], waveY), 1);
            gesture (at (w[1], waveY), at (w[1], waveY), 2);
        }
        else if (cmd == "tabdrag") gesture (at (w[1], tabY), at (w[2], tabY), 1);
        else if (cmd == "menu")
        {
            const auto total = samples[(std::size_t) deck] != nullptr ? (double) samples[(std::size_t) deck]->frames() : 0.0;
            if (w[1] == "zoomin") view.zoomBy (slicex::zoomStep);
            else if (w[1] == "zoomout") view.zoomBy (1.0 / slicex::zoomStep);
            else if (w[1] == "zoomfull") view.zoomFull();
            else if (w[1] == "zoomsel") zoomOnSelection (deck);
            else if (w[1] == "zoomleft" && view.hasSelection()) view.zoomAt (view.selFrom);
            else if (w[1] == "zoomright" && view.hasSelection()) view.zoomAt (view.selTo);
            else if (w[1] == "deselect") view.setSelection (-1.0, -1.0);
            else if (w[1] == "before" && view.hasSelection()) view.setSelection (0.0, view.selFrom);
            else if (w[1] == "after" && view.hasSelection()) view.setSelection (view.selTo, total);
            else if (w[1] == "zoomed") selectShown (deck);
            else if (w[1] == "prev") selectRegionStep (deck, -1);
            else if (w[1] == "next") selectRegionStep (deck, 1);
            else if (w[1] == "setloop") setLoop (deck);
            else return "unknown menu entry";
        }
        else if (cmd == "addregion" || cmd == "addmarker")
        {
            const auto name = w[1].toStdString();
            addRegion (deck, cmd == "addmarker", &name);
        }
        else if (cmd == "key")
        {
            const auto name = w[1].fromLastOccurrenceOf ("+", false, false);
            const auto mods = w[1].startsWith ("ctrl+") ? juce::ModifierKeys::ctrlModifier : (w[1].startsWith ("alt+") ? juce::ModifierKeys::altModifier : 0);
            const auto code = name == "pgup" ? juce::KeyPress::pageUpKey : name == "pgdn" ? juce::KeyPress::pageDownKey : name == "home" ? juce::KeyPress::homeKey
                              : name == "end" ? juce::KeyPress::endKey : name == "up" ? juce::KeyPress::upKey : name == "left" ? juce::KeyPress::leftKey
                              : name == "right" ? juce::KeyPress::rightKey : (int) juce::CharacterFunctions::toUpperCase (name[0]);
            if (! view.keyPressed (juce::KeyPress (code, juce::ModifierKeys (mods), 0)))
                return "key not taken";
        }
        else if (cmd == "rest") { stopTimer(); storeViews(); }
        else if (cmd == "load") loadSample (deck, w[1]);
        else if (cmd == "regions")
        {
            juce::String text;
            for (const auto& r : regions[(std::size_t) deck])
                text << (r.marker ? "marker " : "region ") << juce::String (r.start) << " '" << r.name << "' ";
            return text;
        }
        else if (cmd == "savewav")
        {
            const auto& wav = state.decks[(std::size_t) deck].wav;
            return juce::File (w[1]).replaceWithData (wav.data(), wav.size()) ? "saved " + juce::String ((int) wav.size()) + " bytes" : juce::String ("NOT saved");
        }
        else if (cmd != "state")
            return "unknown command";
        stopTimer(); // (the zoom is written a moment after it settles; the test reads it at once)
        storeViews();
        // what the state holds now (what a save writes)
        const auto& d = state.decks[(std::size_t) deck];
        std::int64_t first = -1, last = -1;
        const auto looped = slicex::wavLoop (d.wav, first, last);
        return "sel " + juce::String (d.selectionStart(), 5) + " " + juce::String (d.selectionEnd(), 5) + " view " + juce::String (d.beatStart(), 5) + " "
               + juce::String (d.beatEnd(), 5) + " shown " + juce::String (regionDeck() == deck ? regionIndex() : -1) + " regions "
               + juce::String ((int) regions[(std::size_t) deck].size()) + " loop " + (looped ? juce::String (first) + " " + juce::String (last) : juce::String ("none"))
               + " viewloop " + juce::String ((int) d.params[0x36]) + " text '" + selLabel.getText() + "'";
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }

    // round 190gk: the Sampler waveform menu's "Open in new Slicex channel" (FL's SBONewMenuClick 0x11f8330: the
    // plugin's template channel, then the sample loaded into it): deck A, as the deck's Load sample does
    void loadSampleNow (const juce::String& path) { loadSample (0, path); }

    void filesDropped (const juce::StringArray& files, int, int y) override
    {
        if (files.isEmpty())
            return;
        // on a deck's wave: that deck; elsewhere: the current deck
        auto deck = currentDeck();
        for (int d = 0; d < 2; ++d)
            if (deckView[(std::size_t) d].isVisible() && deckView[(std::size_t) d].getBounds().contains (getWidth() / 2, y))
                deck = d;
        loadSample (deck, files[0]);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg1);
        g.fillRect (fl (0, 0, 714, 99));           // the top panel
        g.fillRect (fl (0, 296, 714, 66));         // DeckPanel
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (fl (10, 106, 158, 189).toFloat(), 3.0f);  // the region settings
        g.fillRoundedRectangle (fl (173, 106, 160, 189).toFloat(), 3.0f); // the articulator group and its filter
        g.fillRoundedRectangle (fl (341, 6, 222, 50).toFloat(), 3.0f);    // layering
        g.setColour (theme::colour::border);
        g.drawHorizontalLine (fl (0, 99, 1, 1).getY(), 0.0f, (float) getWidth());
        g.drawHorizontalLine (fl (0, 296, 1, 1).getY(), 0.0f, (float) getWidth());

        // the masters' icons under their sliders: level, randomness, LFO, pitch
        g.setColour (theme::colour::accent);
        static const int mx[] = { 22, 46, 71, 95 };
        for (int i = 0; i < 4; ++i)
        {
            const auto r = fl (mx[i] + 3, 83, 14, 10).toFloat();
            juce::Path p;
            if (i == 0)
                p.addTriangle (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), r.getRight(), r.getY());
            else if (i == 1)
            {
                g.drawRoundedRectangle (r.withSizeKeepingCentre (r.getHeight(), r.getHeight()), 2.0f, 1.2f);
                for (const auto d : { -0.22f, 0.22f })
                    g.fillEllipse (juce::Rectangle<float> (2.4f, 2.4f).withCentre ({ r.getCentreX() + d * r.getHeight(), r.getCentreY() + d * r.getHeight() }));
            }
            else if (i == 2)
            {
                p.startNewSubPath (r.getX(), r.getCentreY());
                p.lineTo (r.getX() + r.getWidth() * 0.25f, r.getY());
                p.lineTo (r.getX() + r.getWidth() * 0.75f, r.getBottom());
                p.lineTo (r.getRight(), r.getCentreY());
            }
            else
                for (int k = 0; k < 3; ++k)
                    g.fillRect (juce::Rectangle<float> (r.getX() + r.getWidth() * (0.08f + 0.32f * (float) k), r.getY(), r.getWidth() * 0.2f, r.getHeight()));
            if (i == 0)
                g.fillPath (p);
            else if (i == 2)
                g.strokePath (p, juce::PathStrokeType (1.3f));
        }

        // the deck row's small titles
        g.setColour (theme::colour::textDisabled);
        g.setFont (theme::font (8.5f));
        g.drawText ("SAMPLERATE", fl (5, 336, 64, 9), juce::Justification::centredLeft, false);
        g.drawText ("TEMPO", fl (120, 336, 50, 9), juce::Justification::centredLeft, false);
        g.drawText ("TITLE", fl (263, 336, 50, 9), juce::Justification::centredLeft, false);
        g.drawText ("LENGTH / SEL", fl (547, 336, 88, 9), juce::Justification::centredRight, false);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::body, true));
        g.drawText ("DECK", fl (186, 341, 36, 18), juce::Justification::centredRight, false);
    }

    void resized() override
    {
        // the top panel
        masterVol.setBounds (fl (22, 5, 20, 75));
        masterRand.setBounds (fl (46, 5, 20, 75));
        masterLfo.setBounds (fl (71, 5, 20, 75));
        masterPitch.setBounds (fl (95, 5, 20, 75));
        pad.setBounds (fl (132, 9, 77, 77));
        modX.setBounds (fl (215, 33, 24, 24));
        modY.setBounds (fl (215, 62, 24, 24));
        modXLabel.setBounds (fl (241, 37, 17, 15));
        modYLabel.setBounds (fl (241, 67, 17, 15));
        modulationLabel.setBounds (fl (215, 8, 110, 20));
        modSmooth.setBounds (fl (268, 66, 65, 23));
        optionsButton.setBounds (fl (343, 67, 20, 20));
        layeringLabel.setBounds (fl (345, 9, 76, 19));
        layering.setBounds (fl (424, 9, 132, 19));
        sameDeck.setBounds (fl (345, 35, 78, 15));
        crossfadeLabel.setBounds (fl (420, 35, 57, 16));
        crossfade.setBounds (fl (477, 35, 80, 16));
        autoDump.setBounds (fl (480, 71, 83, 15));
        brand.setBounds (fl (589, 12, 118, 45));

        // the region settings (ArtPanel is at y 99)
        regionMenuButton.setBounds (fl (13, 110, 18, 19));
        regionHeader.setBounds (fl (34, 110, 132, 19));
        regionSel.setBounds (fl (14, 131, 150, 19));
        outLabel.setBounds (fl (16, 157, 32, 23));
        output.setBounds (fl (50, 157, 27, 23));
        cutLabel.setBounds (fl (87, 157, 32, 23));
        cutGroup.setBounds (fl (121, 157, 27, 23));
        static const int ry[] = { 185, 213, 241, 269 };
        for (int i = 0; i < 4; ++i)
        {
            rowA[(std::size_t) i].setBounds (fl (46, ry[i], 23, 23));
            rowB[(std::size_t) i].setBounds (fl (72, ry[i], 23, 23));
            rowLabel[(std::size_t) i].setBounds (fl (14, ry[i] + 4, 32, 15));
            artLabel[(std::size_t) i].setBounds (fl (97, ry[i] + 4, 23, 15));
            rowGroup[(std::size_t) i].setBounds (fl (121, ry[i], 19, 23));
            rowAmount[(std::size_t) i].setBounds (fl (143, ry[i], 23, 23));
        }

        // the group and its filter
        artGroupLabel.setBounds (fl (198, 110, 120, 19));
        artGroup.setBounds (fl (175, 131, 136, 24));
        filterLabel.setBounds (fl (181, 183, 58, 20));
        filterType.setBounds (fl (244, 183, 84, 18));
        filterOrder.setBounds (fl (244, 202, 84, 18));
        filterEnv.setBounds (fl (191, 242, 36, 36));
        filterCut.setBounds (fl (231, 232, 46, 46));
        filterRes.setBounds (fl (281, 242, 36, 36));
        filterEnvLabel.setBounds (fl (191, 279, 36, 12));
        filterCutLabel.setBounds (fl (231, 279, 46, 12));
        filterResLabel.setBounds (fl (281, 279, 36, 12));

        // the articulator editor (MEnvEditor at 338, 139; its graph is 116 high, the knob row below)
        target.setBounds (fl (338, 107, 367, 15));
        partSel.setBounds (fl (338, 123, 367, 15));
        curveView.setBounds (fl (339, 139, 365, 116));
        curveOn.setBounds (fl (340, 264, 15, 15));
        for (int i = 0; i < 4; ++i)
        {
            envKnob[(std::size_t) i].setBounds (fl (358 + i * 23, 261, 21, 21));
            envKnobLabel[(std::size_t) i].setBounds (fl (355 + i * 23, 282, 27, 11));
        }
        for (int i = 0; i < 4; ++i)
        {
            lfoKnob[(std::size_t) i].setBounds (fl (358 + i * 23, 261, 21, 21));
            lfoKnobLabel[(std::size_t) i].setBounds (fl (355 + i * 23, 282, 27, 11));
        }
        curveTempo.setBounds (fl (449, 261, 44, 13));
        curveGlobal.setBounds (fl (449, 277, 44, 13));

        // DeckPanel (y 296)
        loopButton.setBounds (fl (9, 297, 29, 30));
        playButton.setBounds (fl (38, 297, 29, 30));
        stopButton.setBounds (fl (67, 297, 29, 30));
        fileButton.setBounds (fl (160, 298, 24, 34));
        regionsButton.setBounds (fl (264, 298, 24, 34));
        selectButton.setBounds (fl (342, 298, 24, 34));
        zoomButton.setBounds (fl (368, 298, 24, 34));
        dualButton.setBounds (fl (639, 339, 18, 21));
        deckSel.setBounds (fl (224, 341, 31, 18));
        rateLabel.setBounds (fl (5, 345, 64, 15));
        tempoLabel.setBounds (fl (120, 345, 60, 15));
        nameLabel.setBounds (fl (263, 345, 280, 15));
        selLabel.setBounds (fl (475, 345, 160, 15));

        layoutDecks();
        octave.setBounds (fl (3, 574, 26, 48));
        keyboard.setBounds (fl (32, 574, 679, 48));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &nameLabel && e.mods.isPopupMenu())
            fileMenu();
    }

private:
    // ---- construction helpers
    void caption (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (theme::font (theme::type::caption));
        l.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        l.setJustificationType (juce::Justification::centred);
        l.setInterceptsMouseClicks (false, false);
        l.setBorderSize ({});
        addAndMakeVisible (l);
    }

    void bind (juce::Slider& w, const juce::String& hint, int lo, int hi, int def, std::function<int (const State&)> get, std::function<void (State&, int)> set)
    {
        w.setRange (lo, hi, 1.0);
        w.setDoubleClickReturnValue (true, def);
        w.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        w.setPopupDisplayEnabled (true, true, this);
        ui::setHint (w, hint, true);
        w.onDragStart = [this] { dragUndo = true; };
        w.onValueChange = [this, &w, set] {
            if (! loading)
            {
                edit ([&w, set] (State& s) { set (s, (int) w.getValue()); }, dragUndo || ! w.isMouseButtonDown());
                dragUndo = false;
            }
        };
        bound.push_back ({ &w, std::move (get) });
        addAndMakeVisible (w);
    }

    void wheel (juce::Slider& w, const juce::String& hint, int lo, int hi, int def, std::function<int (const State&)> get, std::function<void (State&, int)> set)
    {
        w.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        bind (w, hint, lo, hi, def, std::move (get), std::move (set));
    }

    void vertical (juce::Slider& w, const juce::String& hint, int lo, int hi, int def, int master)
    {
        w.setSliderStyle (juce::Slider::LinearVertical);
        bind (w, hint, lo, hi, def, [master] (const State& s) { return s.masters[(std::size_t) master]; },
              [master] (State& s, int v) { s.masters[(std::size_t) master] = v; });
    }

    // a panel's title (FL: part of the background)
    void header (juce::Label& l, const char* text)
    {
        caption (l, text);
        l.setFont (theme::font (theme::type::body, true));
        l.setJustificationType (juce::Justification::centredLeft);
    }

    void check (Led& b, const char* text, const juce::String& hint, int option, bool ledRight = false)
    {
        b.ledRight = ledRight;
        b.setButtonText (text);
        ui::setHint (b, hint);
        b.onClick = [this, &b, option] { edit ([on = b.getToggleState(), option] (State& s) { s.setOptionByte (option, on ? 1 : 0); }); };
        checks.push_back ({ &b, option });
        addAndMakeVisible (b);
    }

    void regionWheel (juce::Slider& w, const juce::String& hint, int lo, int hi, int def, int field)
    {
        wheel (w, hint, lo, hi, def, [this, field] (const State&) {
            const auto* r = record();
            return r != nullptr ? (*r)[(std::size_t) field] : 0;
        }, [this, field] (State& s, int v) {
            if (auto* r = recordOf (s))
                (*r)[(std::size_t) field] = v;
        });
    }

    void digit (Digit& d, const juce::String& hint, int lo, int hi, int def, int field, std::function<juce::String (int)> text)
    {
        d.lo = lo;
        d.hi = hi;
        d.reset = def;
        d.text = std::move (text);
        d.onChange = [this, field] (int v) { editRegion ([field, v] (slicex::RegionRecord& r) { r[(std::size_t) field] = v; }); };
        ui::setHint (d, hint);
        addAndMakeVisible (d);
    }

    void groupWheel (juce::Slider& w, const juce::String& hint, int index)
    {
        wheel (w, hint, 0, 256, 128, [this, index] (const State&) { return group().header[(std::size_t) index]; },
               [this, index] (State& s, int v) { s.groups[(std::size_t) groupIndex()].header[(std::size_t) index] = v; });
    }

    void artWheel (juce::Slider& w, const juce::String& hint, int lo, int hi, int def, std::function<int (const slicex::Articulator&)> get,
                   std::function<void (slicex::Articulator&, int)> set)
    {
        wheel (w, hint, lo, hi, def, [this, get] (const State&) { return get (art()); },
               [this, set] (State& s, int v) { set (s.groups[(std::size_t) groupIndex()].art[(std::size_t) targetIndex()], v); });
    }

    // ---- what is shown
    int groupIndex() const { return std::clamp ((int) state.optionInt (slicex::oArtGroup), 0, 7); }
    int targetIndex() const { return std::clamp ((int) state.optionInt (slicex::oArtTarget), 0, (int) slicex::targetCount - 1); }
    int part() const { return std::clamp ((int) state.optionInt (slicex::oArtPart), 0, 5); }
    const slicex::Group& group() const { return state.groups[(std::size_t) groupIndex()]; }
    const slicex::Articulator& art() const { return group().art[(std::size_t) targetIndex()]; }
    // the editor lists the maps as velocity, Mod X, Mod Y, random; the state stores Mod X, Mod Y, velocity, random
    static int mapOfPart (int p) { return p == 2 ? slicex::mapVelocity : (p == 3 ? slicex::mapModX : (p == 4 ? slicex::mapModY : slicex::mapRandom)); }
    const slicex::Curve& shownCurve() const
    {
        const auto p = part();
        return p == 0 ? art().envelope : (p == 1 ? art().lfo : art().maps[(std::size_t) mapOfPart (p)]);
    }
    slicex::Curve& curveOf (State& s) const
    {
        auto& a = s.groups[(std::size_t) groupIndex()].art[(std::size_t) targetIndex()];
        const auto p = part();
        return p == 0 ? a.envelope : (p == 1 ? a.lfo : a.maps[(std::size_t) mapOfPart (p)]);
    }
    int currentDeck() const { return std::clamp ((int) state.ui[0], 0, 1); }
    int regionDeck() const { return std::clamp ((int) (state.ui[1] >> 16), 0, 1); }
    int regionIndex() const { return (int) (state.ui[1] & 0xffff); }
    const slicex::RegionRecord* record() const
    {
        const auto& list = state.decks[(std::size_t) regionDeck()].regions;
        return regionIndex() < (int) list.size() ? &list[(std::size_t) regionIndex()] : nullptr;
    }
    slicex::RegionRecord* recordOf (State& s) const
    {
        auto& list = s.decks[(std::size_t) regionDeck()].regions;
        return regionIndex() < (int) list.size() ? &list[(std::size_t) regionIndex()] : nullptr;
    }
    int keyOfRegion (int deck, int index) const
    {
        const auto id = (deck << 16) | index;
        for (int key = 0; key < 128; ++key)
            for (const auto r : maps[(std::size_t) deck][(std::size_t) key])
                if (r == id)
                    return key;
        return -1;
    }

    const ChannelDef* def() const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }

    // ---- state in and out
    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        lastState = d->pluginState;
        paths[0] = d->samplePath;
        paths[1] = slicex::deckBPath (*d);
        if (auto s = slicex::parse (d->pluginState))
            state = *s;
        else
            state = slicex::fresh();
        for (int k = 0; k < 2; ++k)
        {
            const auto i = (std::size_t) k;
            samples[i] = paths[i].empty() ? nullptr : engine.getSampleLibrary().load (juce::String::fromUTF8 (paths[i].c_str()));
            regions[i].clear();
            loopShown[i] = false;
            const auto fresh = samples[i].get() != viewOf[i];
            viewOf[i] = samples[i].get();
            if (samples[i] == nullptr || samples[i]->frames() == 0)
                continue;
            // a sample new to this page opens at the range the state was saved with (FL keeps the deck's zoom in the
            // project: start and end in beats); later edits leave the view alone
            const auto beats = beatsOf (k), perBeat = framesPerBeat (k);
            if (fresh)
            {
                const auto from = state.decks[i].beatStart(), to = state.decks[i].beatEnd();
                if (perBeat > 0.0 && std::isfinite (from) && std::isfinite (to) && from >= 0.0 && to > from && (from > 0.0 || to < beats))
                    deckView[i].openAt (from * perBeat, (to - from) * perBeat);
                else
                    deckView[i].openAt (0.0, 0.0);
            }
            // the selection and the loop points, as saved
            if (state.decks[i].hasSelection() && perBeat > 0.0)
                deckView[i].showSelection (state.decks[i].selectionStart() * perBeat, state.decks[i].selectionEnd() * perBeat);
            else
                deckView[i].showSelection (-1.0, -1.0);
            loopShown[i] = state.decks[i].showsLoop() && slicex::wavLoop (state.decks[i].wav, loops[i].first, loops[i].second);
            // as the instrument does: FL's regions of the state's own WAV, else the file's cue points
            std::int64_t frames = 0;
            if (! state.decks[i].wav.empty())
                regions[i] = slicex::wavRegions (state.decks[i].wav, frames);
            else if (fileName (paths[i]).endsWithIgnoreCase (".wav"))
                regions[i] = slicex::wavRegions (deckWav (k), frames); // a referenced WAV: its own region names and keys
            const auto fromMarkers = slicex::regionsFromMarkers (samples[i]->markerStarts, (std::int64_t) samples[i]->frames());
            // (what the instrument plays; a file's own list is only taken when it is the same cut)
            if (regions[i].empty() || frames != (std::int64_t) samples[i]->frames() || (state.decks[i].wav.empty() && regions[i].size() != fromMarkers.size()))
                regions[i] = fromMarkers;
        }
        refresh();
    }

    void refresh()
    {
        loading = true;
        const auto sameDeckOnly = state.optionByte (slicex::oSameDeckOnly) != 0;
        for (int d = 0; d < 2; ++d)
            maps[(std::size_t) d] = slicex::keyMap (d, regions[0], regions[1], sameDeckOnly);
        for (auto& b : bound)
            b.slider->setValue (b.get (state), juce::dontSendNotification);
        for (auto& c : checks)
            c.first->setToggleState (state.optionByte (c.second) != 0, juce::dontSendNotification);
        pad.x = state.masters[slicex::mModX];
        pad.y = state.masters[slicex::mModY];
        layering.setSelectedId (std::clamp ((int) state.optionInt (slicex::oLayering), 0, 7) + 1, juce::dontSendNotification);
        crossfade.value = std::clamp ((int) state.optionInt (slicex::oCrossfade), 0, 4);

        regionSel.clear (juce::dontSendNotification);
        // the names as FL shows them ("Kick"); a heading per deck only when both decks have regions
        const auto bothDecks = ! regions[0].empty() && ! regions[1].empty();
        for (int d = 0; d < 2; ++d)
        {
            if (bothDecks)
                regionSel.addSectionHeading (d == 0 ? tr ("Deck A") : tr ("Deck B"));
            for (std::size_t i = 0; i < regions[(std::size_t) d].size(); ++i)
                regionSel.addItem (juce::String::fromUTF8 (slicex::regionName (regions[(std::size_t) d][i], (int) i).c_str()), ((d << 16) | (int) i) + 1);
        }
        const auto* rec = record();
        const auto has = rec != nullptr && regionIndex() < (int) regions[(std::size_t) regionDeck()].size();
        regionSel.setSelectedId (has ? ((regionDeck() << 16) | regionIndex()) + 1 : 0, juce::dontSendNotification);
        output.value = has ? (*rec)[slicex::rOutput] : 0;
        cutGroup.value = has ? (*rec)[slicex::rCutGroup] : 0;
        static const int groups[] = { slicex::rAmpGroup, slicex::rFilterGroup, slicex::rSpeedGroup, slicex::rStartGroup };
        for (int i = 0; i < 4; ++i)
            rowGroup[(std::size_t) i].value = has ? (*rec)[(std::size_t) groups[i]] + 1 : 0;
        for (auto* c : std::initializer_list<juce::Component*> { &output, &cutGroup, &rowGroup[0], &rowGroup[1], &rowGroup[2], &rowGroup[3], &rowB[0], &rowB[1],
                                                                 &rowB[2], &rowB[3], &rowA[0], &rowA[1], &rowA[3], &rowAmount[0], &rowAmount[1], &rowAmount[2], &rowAmount[3] })
            c->setEnabled (has);

        artGroup.value = groupIndex();
        filterType.value = std::clamp (group().filterType(), 0, 3);
        filterOrder.value = std::clamp (group().filterOrder(), 0, 3);
        target.value = targetIndex();
        partSel.value = part();
        const auto p = part();
        const auto& c = shownCurve();
        curveOn.setVisible (p < 2);
        curveOn.setToggleState (c.on(), juce::dontSendNotification);
        curveTempo.setVisible (p < 2);
        curveTempo.setToggleState (c.tempoBased(), juce::dontSendNotification);
        curveGlobal.setVisible (p == 1);
        curveGlobal.setToggleState ((c.flags & 2) != 0, juce::dontSendNotification);
        for (int i = 0; i < 4; ++i)
        {
            envKnob[(std::size_t) i].setVisible (p == 0);
            envKnobLabel[(std::size_t) i].setVisible (p == 0);
        }
        for (int i = 0; i < 4; ++i)
        {
            lfoKnob[(std::size_t) i].setVisible (p == 1);
            lfoKnobLabel[(std::size_t) i].setVisible (p == 1);
        }

        loopButton.setToggleState (state.optionByte (slicex::oLoop) != 0, juce::dontSendNotification);
        dualButton.setToggleState (state.optionByte (slicex::oDualDeck) != 0, juce::dontSendNotification);
        deckSel.value = currentDeck();
        octave.value = std::clamp ((int) state.ui[2], 0, 4);
        keyboard.base = 12 * octave.value;
        const auto deck = (std::size_t) currentDeck();
        const auto& sample = samples[deck];
        rateLabel.setText (sample != nullptr ? juce::String ((int) std::lround (sample->sampleRate)) + "Hz" : juce::String(), juce::dontSendNotification);
        const auto beats = beatsOf ((int) deck);
        tempoLabel.setText (sample != nullptr && sample->frames() > 0 && std::isfinite (beats) && beats > 0.0
                                ? juce::String ((int) std::lround (beats * 60.0 * sample->sampleRate / (double) sample->frames())) + "BPM"
                                : juce::String(),
                            juce::dontSendNotification);
        auto title = wavTitle (state.decks[deck].wav);
        if (title.isEmpty())
            title = fileName (paths[deck]).upToLastOccurrenceOf (".", false, false);
        nameLabel.setText (title, juce::dontSendNotification);
        lengthText();
        loading = false;
        layoutDecks();
        repaint();
    }

    // LENGTH / SEL: the selection's range (FL: "001:02:075 to 001:04:030"), else the range of a region picked in this
    // window, else the sample's length, on the deck's beat grid (bar:beat:tick)
    void lengthText()
    {
        const auto deck = (std::size_t) currentDeck();
        const auto& sample = samples[deck];
        const auto beats = beatsOf ((int) deck);
        juce::String length;
        if (sample != nullptr && sample->frames() > 0 && std::isfinite (beats) && beats > 0.0)
        {
            const auto perFrame = beats / (double) sample->frames();
            const auto& view = deckView[deck];
            const auto onDeck = regionDeck() == (int) deck && record() != nullptr && regionIndex() < (int) regions[deck].size();
            if (view.hasSelection())
                length = barBeatTick (view.selFrom * perFrame) + " " + tr ("to") + " " + barBeatTick (view.selTo * perFrame);
            else if (onDeck && lastPicked)
            {
                const auto& r = regions[deck][(std::size_t) regionIndex()];
                length = barBeatTick ((double) r.start * perFrame) + " " + tr ("to") + " " + barBeatTick ((double) (r.end + 1) * perFrame);
            }
            else
                length = barBeatTick (beats);
        }
        selLabel.setText (length, juce::dontSendNotification);
    }

    // ---- the deck's beat grid, zoom and selection
    double fileBeats (int deck) const { return samples[(std::size_t) deck] != nullptr ? slicex::sampleBeats (*samples[(std::size_t) deck]) : 0.0; }
    double beatsOf (int deck) const { return slicex::deckBeats (state.decks[(std::size_t) deck], fileBeats (deck)); }
    double framesPerBeat (int deck) const
    {
        const auto beats = beatsOf (deck);
        const auto& sample = samples[(std::size_t) deck];
        return sample != nullptr && sample->frames() > 0 && std::isfinite (beats) && beats > 0.0 ? (double) sample->frames() / beats : 0.0;
    }

    void timerCallback() override
    {
        stopTimer();
        storeViews();
    }

    // FL keeps each deck's zoom in the project (the range shown, in beats): written when the view has come to rest
    void storeViews()
    {
        std::array<std::pair<double, double>, 2> range;
        bool changed = false;
        for (int d = 0; d < 2; ++d)
        {
            const auto i = (std::size_t) d;
            range[i] = { state.decks[i].beatStart(), state.decks[i].beatEnd() };
            const auto perBeat = framesPerBeat (d);
            if (perBeat <= 0.0)
                continue;
            const auto shown = deckView[i].shown();
            const std::pair<double, double> now { shown.first / perBeat, shown.second / perBeat };
            if (std::abs (now.first - range[i].first) > 1.0e-9 || std::abs (now.second - range[i].second) > 1.0e-9)
            {
                range[i] = now;
                changed = true;
            }
        }
        if (changed)
            edit ([&range] (State& s) {
                for (int d = 0; d < 2; ++d)
                    slicex::setDeckBeats (s, d, range[(std::size_t) d].first, range[(std::size_t) d].second);
            }, false);
    }

    // the view's selection (frames) into the state (beats)
    void storeSelection (State& s, int deck) const
    {
        const auto& view = deckView[(std::size_t) deck];
        const auto perBeat = framesPerBeat (deck);
        if (view.hasSelection() && perBeat > 0.0)
            slicex::setDeckSelection (s, deck, view.selFrom / perBeat, view.selTo / perBeat);
        else
            slicex::setDeckSelection (s, deck, -1.0, -1.0);
    }

    void selectionMoved (int deck, bool done)
    {
        if (done)
            edit ([this, deck] (State& s) { storeSelection (s, deck); }, false);
        else
            lengthText();
    }

    // the frames of the deck's selection as FL makes a region or a loop of it: [first, end)
    slicex::FrameRange selectionFrames (int deck) const
    {
        const auto& view = deckView[(std::size_t) deck];
        const auto perBeat = framesPerBeat (deck);
        if (! view.hasSelection() || perBeat <= 0.0 || samples[(std::size_t) deck] == nullptr)
            return {};
        return slicex::framesOf (view.selFrom / perBeat, view.selTo / perBeat, perBeat, (std::int64_t) samples[(std::size_t) deck]->frames());
    }

    // FL's "Select previous / next region": the region after (before) the one the selection is, going round; from a
    // selection that is no region, the first (the last). The region becomes the one shown in the region settings.
    void selectRegionStep (int deck, int step)
    {
        const auto& list = regions[(std::size_t) deck];
        const auto perBeat = framesPerBeat (deck);
        if (list.empty() || perBeat <= 0.0)
            return;
        const auto n = (int) list.size();
        const auto range = selectionFrames (deck);
        auto is = [&list, &range] (int i) { return i >= 0 && i < (int) list.size() && list[(std::size_t) i].start == range.first && list[(std::size_t) i].end + 1 == range.end; };
        int at = -1;
        if (deckView[(std::size_t) deck].hasSelection())
        {
            if (regionDeck() == deck && is (regionIndex()))
                at = regionIndex();
            else
                for (int i = 0; i < n && at < 0; ++i)
                    if (is (i))
                        at = i;
        }
        const auto to = step > 0 ? (at + 1) % n : (at <= 0 ? n - 1 : at - 1);
        const auto& r = list[(std::size_t) to];
        deckView[(std::size_t) deck].showSelection ((double) r.start, (double) r.end + 1.0);
        lastPicked = true;
        edit ([deck, to, &r, perBeat] (State& s) {
            s.ui[0] = deck;
            s.ui[1] = (deck << 16) | to;
            slicex::setDeckSelection (s, deck, (double) r.start / perBeat, (double) (r.end + 1) / perBeat);
        }, false);
    }

    // the deck's shortcuts, as FL's menus name them (the deck has the keyboard after a click in it)
    bool deckKey (int deck, const juce::KeyPress& k)
    {
        if (samples[(std::size_t) deck] == nullptr)
            return false;
        auto& view = deckView[(std::size_t) deck];
        const auto mods = k.getModifiers();
        const auto plain = ! mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown() && ! mods.isCommandDown();
        const auto ctrl = mods.isCtrlDown() && ! mods.isAltDown() && ! mods.isShiftDown();
        const auto alt = mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown();
        const auto code = k.getKeyCode();
        if (plain && code == juce::KeyPress::pageUpKey) view.zoomBy (slicex::zoomStep);
        else if (plain && code == juce::KeyPress::pageDownKey) view.zoomBy (1.0 / slicex::zoomStep);
        else if (plain && code == juce::KeyPress::homeKey) view.zoomFull();
        else if (plain && code == juce::KeyPress::endKey) zoomOnSelection (deck);
        else if (ctrl && code == juce::KeyPress::leftKey && view.hasSelection()) view.zoomAt (view.selFrom);
        else if (ctrl && code == juce::KeyPress::rightKey && view.hasSelection()) view.zoomAt (view.selTo);
        else if (plain && code == juce::KeyPress::upKey) view.setSelection (-1.0, -1.0);
        else if (ctrl && (code == 'A' || code == 'a')) selectShown (deck);
        else if (plain && code == juce::KeyPress::leftKey) selectRegionStep (deck, -1);
        else if (plain && code == juce::KeyPress::rightKey) selectRegionStep (deck, 1);
        else if (plain && (code == 'M' || code == 'm')) addRegion (deck, true);
        else if (alt && (code == 'M' || code == 'm')) addRegion (deck, false);
        else if (alt && (code == 'L' || code == 'l')) setLoop (deck);
        else
            return false;
        return true;
    }

    void selectShown (int deck)
    {
        const auto shown = deckView[(std::size_t) deck].shown();
        deckView[(std::size_t) deck].setSelection (shown.first, shown.second);
    }

    // FL's "Zoom on selection": the selection fills the view; without one, the region shown (LYNX)
    void zoomOnSelection (int deck)
    {
        auto& view = deckView[(std::size_t) deck];
        if (view.hasSelection())
            view.zoomOn (view.selFrom, view.selTo);
        else if (regionDeck() == deck)
            zoomOnRegion (deck, regionIndex());
    }

    void layoutDecks()
    {
        const auto dual = state.optionByte (slicex::oDualDeck) != 0;
        const auto deck = currentDeck();
        deckView[0].setVisible (dual || deck == 0);
        deckView[1].setVisible (dual || deck == 1);
        if (dual)
        {
            deckView[0].setBounds (fl (0, 362, 714, 144));
            deckView[1].setBounds (fl (0, 507, 714, 62));
        }
        else
            deckView[(std::size_t) deck].setBounds (fl (0, 362, 714, 207));
    }

    void edit (const std::function<void (State&)>& change, bool undoable = true)
    {
        change (state);
        const auto bytes = slicex::write (state);
        lastState = bytes;
        engine.editSong ([l = locate, bytes] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
                d->pluginState = bytes;
        }, undoable);
        refresh();
    }

    void editRegion (const std::function<void (slicex::RegionRecord&)>& change)
    {
        edit ([this, &change] (State& s) {
            if (auto* r = recordOf (s))
                change (*r);
        });
    }

    void editGroup (const std::function<void (slicex::Group&)>& change)
    {
        edit ([this, &change] (State& s) { change (s.groups[(std::size_t) groupIndex()]); });
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d != nullptr && (d->pluginState != lastState || d->samplePath != paths[0] || slicex::deckBPath (*d) != paths[1]))
            reload();
    }

    // ---- actions
    void selectRegion (int deck, int index, bool audition)
    {
        if (deck < 0 || deck > 1 || index < 0 || index >= (int) regions[(std::size_t) deck].size())
            return;
        edit ([deck, index] (State& s) {
            s.ui[1] = (deck << 16) | index;
            s.ui[0] = deck;
        }, false);
        lastPicked = true; // (the range of a region picked in this window is shown as the selection)
        refresh();
        if (audition)
            playRange (deck, regions[(std::size_t) deck][(std::size_t) index].start, regions[(std::size_t) deck][(std::size_t) index].end + 1);
    }

    void playRange (int deck, std::int64_t from, std::int64_t to)
    {
        const auto& data = samples[(std::size_t) deck];
        if (data == nullptr)
            return;
        const auto total = (std::int64_t) data->frames();
        from = std::clamp<std::int64_t> (from, 0, total);
        to = std::clamp<std::int64_t> (to, from, total);
        if (to <= from)
            return;
        auto piece = std::make_shared<SampleData>();
        piece->sampleRate = data->sampleRate;
        piece->left.assign (data->leftData() + from, data->leftData() + to);
        if (data->isStereo())
            piece->right.assign (data->rightData() + from, data->rightData() + to);
        engine.previewSample (piece, state.optionByte (slicex::oLoop) != 0);
    }

    void playDeck()
    {
        const auto deck = currentDeck();
        if (samples[(std::size_t) deck] != nullptr)
            playRange (deck, 0, (std::int64_t) samples[(std::size_t) deck]->frames());
    }

    // A new sample on a deck: by reference, its regions the file's own cue points (FL slices a sample without any; see
    // "Medium auto-slicing" in the Regions menu)
    void loadSample (int deck, const juce::String& path)
    {
        const auto sample = engine.getSampleLibrary().load (path);
        if (sample == nullptr || sample->frames() == 0)
            return;
        const auto p = path.toStdString();
        const auto count = (int) slicex::regionsFromMarkers (sample->markerStarts, (std::int64_t) sample->frames()).size();
        auto s = state;
        slicex::setDeckFile (s, deck, p, std::max (1, count));
        // the beat grid: the file's own beat count (its ACID chunk: "DL Breaker" has 8 beats in 8 s and FL shows 60BPM);
        // a file without one gets FL's guess from its length (slicex::guessBeats)
        slicex::setDeckBeats (s, deck, 0.0, slicex::sampleBeats (*sample));
        slicex::setDeckSelection (s, deck, -1.0, -1.0);
        s.ui[0] = deck;
        s.ui[1] = deck << 16;
        const auto bytes = slicex::write (s);
        engine.editSong ([l = locate, bytes, p, deck] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                if (deck == 0)
                    d->samplePath = p;
                else
                    slicex::setDeckBPath (*d, p);
            }
        }, true);
        reload();
        // A sample that brings no regions of its own: FL slices it as it loads ("Marker #1…#n" on its hits, round
        // 190ac). It is the framework's auto slicer LYNX already has (flslice, decoded in round 165g): on FL's own save
        // of such a load the markers are its output to the frame (round 190aj). The markers live in the deck's WAV, as
        // in FL, so only a WAV file gets them.
        if (sample->markerStarts.empty() && canEmbed (deck) && sample->left.size() == sample->frames())
        {
            const auto starts = flslice::autoSlice (sample->left.data(), sample->right.size() == sample->left.size() ? sample->right.data() : nullptr,
                                                    (std::int64_t) sample->frames(), sample->sampleRate);
            std::vector<slicex::WavRegion> list;
            for (const auto start : starts)
            {
                slicex::WavRegion marker;
                marker.start = start;
                list.push_back (marker);
            }
            if (! list.empty())
                setRegions (deck, std::move (list), std::vector<int> (starts.size(), -1), 0);
        }
    }

    void chooseSample (int deck)
    {
        chooser = std::make_unique<juce::FileChooser> (tr ("Load sample"), juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.wv");
        juce::Component::SafePointer<SlicexEditor> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [self, deck] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().existsAsFile())
                self->loadSample (deck, fc.getResult().getFullPathName());
        });
    }

    // FL's File menu of the deck (New, Load sample, Save sample as; see REA_rodada190ac for the entries left out)
    void fileMenu()
    {
        const auto deck = currentDeck();
        const auto has = samples[(std::size_t) deck] != nullptr;
        juce::PopupMenu m;
        m.addItem (1, tr ("New"), has);
        m.addItem (2, tr ("Load sample..."));
        m.addItem (3, tr ("Save sample as..."), has && ! deckWav (deck).empty());
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fileButton), [self, deck] (int r) {
            if (self == nullptr)
                return;
            if (r == 1)
                self->clearDeck (deck);
            else if (r == 2)
                self->chooseSample (deck);
            else if (r == 3)
                self->saveSample (deck);
        });
    }

    // FL's "New": the deck without a sample
    void clearDeck (int deck)
    {
        auto s = state;
        slicex::setDeckFile (s, deck, {}, 1);
        s.ui[1] = deck << 16;
        const auto bytes = slicex::write (s);
        engine.editSong ([l = locate, bytes, deck] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                if (deck == 0)
                    d->samplePath.clear();
                else
                    slicex::setDeckBPath (*d, {});
            }
        }, true);
        reload();
    }

    // FL's "Save sample as": the deck's WAV with its regions (names, lengths, keys)
    void saveSample (int deck)
    {
        const auto name = nameLabel.getText().isNotEmpty() ? nameLabel.getText() : juce::String ("Slicex");
        chooser = std::make_unique<juce::FileChooser> (tr ("Save sample as"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".wav"), "*.wav");
        juce::Component::SafePointer<SlicexEditor> self (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [self, deck] (const juce::FileChooser& fc) {
                                  if (self == nullptr || fc.getResult() == juce::File())
                                      return;
                                  const auto wav = self->deckWav (deck);
                                  if (! wav.empty())
                                      fc.getResult().withFileExtension ("wav").replaceWithData (wav.data(), wav.size());
                              });
    }

    // a menu entry with the shortcut FL's menu shows beside it
    static void entry (juce::PopupMenu& m, int id, const juce::String& text, bool enabled, const char* keys = nullptr, bool ticked = false)
    {
        juce::PopupMenu::Item item (text);
        item.itemID = id;
        item.isEnabled = enabled;
        item.isTicked = ticked;
        if (keys != nullptr)
            item.shortcutKeyDescription = keys;
        m.addItem (std::move (item));
    }

    // FL's Regions menu of the deck (the entries LYNX has; REA_rodada190ac lists the rest)
    void regionsMenu()
    {
        const auto deck = currentDeck();
        const auto can = canSetKeys (deck) && ! frozen (deck);
        const auto onDeck = regionDeck() == deck && regionIndex() < (int) regions[(std::size_t) deck].size();
        const auto selection = deckView[(std::size_t) deck].hasSelection();
        juce::PopupMenu m, cut;
        entry (m, 5, tr ("Add region..."), canEmbed (deck) && ! frozen (deck) && selection, "Alt+M");
        entry (m, 1, tr ("Add marker..."), canEmbed (deck) && ! frozen (deck), "M");
        entry (m, 6, tr ("Set loop"), canEmbed (deck) && selection, "Alt+L");
        m.addSeparator();
        entry (m, 2, tr ("Delete"), can && onDeck);
        entry (m, 3, tr ("Rename all..."), can);
        cut.addItem (100, tr ("None"));
        for (int i = 1; i <= 16; ++i)
            cut.addItem (100 + i, juce::String (i));
        m.addSubMenu (tr ("Set all cut groups to..."), cut, ! state.decks[(std::size_t) deck].regions.empty());
        m.addSeparator();
        entry (m, 4, tr ("Freeze all"), samples[(std::size_t) deck] != nullptr, nullptr, frozen (deck));
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&regionsButton), [self, deck] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 1)
                self->addRegion (deck, true);
            else if (r == 5)
                self->addRegion (deck, false);
            else if (r == 6)
                self->setLoop (deck);
            else if (r == 2)
                self->deleteRegion (deck, self->regionIndex());
            else if (r == 3)
                self->renameRegion (deck, 0, true);
            else if (r == 4)
                self->edit ([deck] (State& s) {
                    auto& flag = s.decks[(std::size_t) deck].params[0x38];
                    flag = flag != 0 ? 0 : 1;
                });
            else if (r >= 100)
                self->edit ([deck, v = r - 100] (State& s) {
                    for (auto& rec : s.decks[(std::size_t) deck].regions)
                        rec[slicex::rCutGroup] = v;
                });
        });
    }

    // FL's Zoom menu of the deck
    void zoomMenu()
    {
        const auto deck = currentDeck();
        const auto has = samples[(std::size_t) deck] != nullptr;
        const auto onDeck = regionDeck() == deck && regionIndex() < (int) regions[(std::size_t) deck].size();
        const auto selection = has && deckView[(std::size_t) deck].hasSelection();
        juce::PopupMenu m;
        entry (m, 1, tr ("Zoom in"), has, "PgUp");
        entry (m, 2, tr ("Zoom out"), has, "PgDn");
        entry (m, 3, tr ("Zoom out full"), has, "Home");
        m.addSeparator();
        entry (m, 4, tr ("Zoom on selection"), selection || (has && onDeck), "End");
        entry (m, 5, tr ("Zoom on left of selection"), selection, "Ctrl+Left");
        entry (m, 6, tr ("Zoom on right of selection"), selection, "Ctrl+Right");
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&zoomButton), [self, deck] (int r) {
            if (self == nullptr || r == 0)
                return;
            auto& view = self->deckView[(std::size_t) deck];
            if (r == 1)
                view.zoomBy (slicex::zoomStep);
            else if (r == 2)
                view.zoomBy (1.0 / slicex::zoomStep);
            else if (r == 3)
                view.zoomFull();
            else if (r == 4)
                self->zoomOnSelection (deck);
            else if (view.hasSelection())
                view.zoomAt (r == 5 ? view.selFrom : view.selTo);
        });
    }

    // FL's Select menu of the deck
    void selectMenu()
    {
        const auto deck = currentDeck();
        const auto has = samples[(std::size_t) deck] != nullptr;
        const auto selection = has && deckView[(std::size_t) deck].hasSelection();
        const auto any = has && ! regions[(std::size_t) deck].empty();
        juce::PopupMenu m;
        entry (m, 1, tr ("Deselect"), selection, "Up");
        entry (m, 2, tr ("Select before current selection"), selection);
        entry (m, 3, tr ("Select after current selection"), selection);
        entry (m, 4, tr ("Select zoomed part"), has, "Ctrl+A");
        m.addSeparator();
        entry (m, 5, tr ("Select previous region"), any, "Left");
        entry (m, 6, tr ("Select next region"), any, "Right");
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&selectButton), [self, deck] (int r) {
            if (self == nullptr || r == 0 || self->samples[(std::size_t) deck] == nullptr)
                return;
            auto& view = self->deckView[(std::size_t) deck];
            const auto total = (double) self->samples[(std::size_t) deck]->frames();
            if (r == 1)
                view.setSelection (-1.0, -1.0);
            else if (r == 2 && view.hasSelection()) // (FL: from the sample's start to where the selection began)
                view.setSelection (0.0, view.selFrom);
            else if (r == 3 && view.hasSelection()) // (from where it ended to the sample's end)
                view.setSelection (view.selTo, total);
            else if (r == 4)
                self->selectShown (deck);
            else if (r == 5 || r == 6)
                self->selectRegionStep (deck, r == 5 ? -1 : 1);
        });
    }

    void zoomOnRegion (int deck, int index)
    {
        const auto& list = regions[(std::size_t) deck];
        if (index >= 0 && index < (int) list.size())
            deckView[(std::size_t) deck].zoomOn ((double) list[(std::size_t) index].start, (double) list[(std::size_t) index].end + 1.0);
    }

    // FL's "More region settings" (RgnMenuBtn; the same menu on a region's tab in the wave)
    // `fromButton`: under the region settings' menu button; else at the mouse (a right click on the region in the wave)
    void regionMenu (bool fromButton)
    {
        const auto deck = regionDeck(), index = regionIndex();
        if (index >= (int) regions[(std::size_t) deck].size())
            return;
        const auto& region = regions[(std::size_t) deck][(std::size_t) index];
        const auto own = region.key;
        const auto can = canSetKeys (deck) && ! frozen (deck);
        juce::PopupMenu m, keys;
        keys.addItem (1, tr ("Default"), true, own < 0);
        keys.addSeparator();
        for (int k = 0; k < 128; ++k)
            keys.addItem (1000 + k, keyName (k), true, own == k);
        m.addSectionHeader (juce::String::fromUTF8 (slicex::regionName (region, index).c_str()));
        m.addItem (2, tr ("Delete"), can);
        m.addItem (3, tr ("Clone as region"), can);
        m.addItem (4, tr ("Turn into marker"), can && ! region.marker);
        m.addSeparator();
        m.addItem (5, tr ("Rename..."), can);
        m.addSubMenu (tr ("Trigger note"), keys, canSetKeys (deck));
        m.addSeparator();
        m.addItem (6, tr ("Zoom on"));
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (fromButton ? juce::PopupMenu::Options().withTargetComponent (&regionMenuButton) : juce::PopupMenu::Options(), [self, deck, index] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 2)
                self->deleteRegion (deck, index);
            else if (r == 3)
                self->cloneRegion (deck, index);
            else if (r == 4)
                self->turnIntoMarker (deck, index);
            else if (r == 5)
                self->renameRegion (deck, index, false);
            else if (r == 6)
                self->zoomOnRegion (deck, index);
            else
                self->setRegionKey (deck, index, r == 1 ? -1 : r - 1000);
        });
    }

    // A region's key lives in the deck's WAV (FL's trigger list), so a deck that only names its file takes the file in
    // (FL embeds an edited sample the same way). Only a WAV file with cue points can carry keys.
    std::vector<std::uint8_t> deckWav (int deck) const
    {
        const auto& d = state.decks[(std::size_t) deck];
        if (! d.wav.empty())
            return d.wav;
        const juce::File file (juce::String::fromUTF8 (paths[(std::size_t) deck].c_str()));
        juce::MemoryBlock data;
        if (! file.existsAsFile() || file.getSize() > 512 * 1024 * 1024 || ! file.loadFileAsData (data))
            return {};
        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
        return std::vector<std::uint8_t> (bytes, bytes + data.getSize());
    }

    bool canSetKeys (int deck) const
    {
        if (samples[(std::size_t) deck] == nullptr || regions[(std::size_t) deck].empty())
            return false;
        if (! state.decks[(std::size_t) deck].wav.empty())
            return true;
        return fileName (paths[(std::size_t) deck]).endsWithIgnoreCase (".wav");
    }

    // The deck's WAV can be taken into the state and rewritten (regions, loop): it is there already, or the file is a WAV
    bool canEmbed (int deck) const
    {
        if (samples[(std::size_t) deck] == nullptr)
            return false;
        return ! state.decks[(std::size_t) deck].wav.empty() || fileName (paths[(std::size_t) deck]).endsWithIgnoreCase (".wav");
    }

    void setRegionKey (int deck, int index, int key)
    {
        const auto& list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size())
            return;
        std::vector<int> keys;
        for (const auto& r : list)
            keys.push_back (r.key);
        keys[(std::size_t) index] = key;
        const auto wav = slicex::withRegionKeys (deckWav (deck), keys);
        // the WAV must give the same regions back (same cue points, same length as the loaded sample)
        std::int64_t frames = 0;
        if (wav.empty() || slicex::wavRegions (wav, frames).size() != list.size() || samples[(std::size_t) deck] == nullptr
            || frames != (std::int64_t) samples[(std::size_t) deck]->frames())
            return;
        edit ([deck, &wav] (State& s) {
            s.decks[(std::size_t) deck].wav = wav;
            s.decks[(std::size_t) deck].referenceOnly = 0;
        });
        reload();
    }

    bool frozen (int deck) const { return state.decks[(std::size_t) deck].params[0x38] != 0; }

    // The deck's region list rewritten. The list lives in the deck's WAV (FL embeds an edited sample the same way);
    // `from[i]` names the old region whose settings region i keeps (−1: a new region's defaults). The list is put in
    // sample order here, as FL keeps it.
    bool setRegions (int deck, std::vector<slicex::WavRegion> list, std::vector<int> from, int show)
    {
        if (samples[(std::size_t) deck] == nullptr || list.size() != from.size())
            return false;
        std::vector<std::size_t> order (list.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
        std::stable_sort (order.begin(), order.end(), [&list] (std::size_t a, std::size_t b) { return list[a].start < list[b].start; });
        std::vector<slicex::WavRegion> sorted;
        std::vector<slicex::RegionRecord> records;
        const auto& old = state.decks[(std::size_t) deck].regions;
        int shown = 0;
        for (const auto i : order)
        {
            if ((int) i == show)
                shown = (int) sorted.size();
            sorted.push_back (list[i]);
            records.push_back (from[i] >= 0 && from[i] < (int) old.size() ? old[(std::size_t) from[i]] : slicex::defaultRegion());
        }
        const auto wav = slicex::withRegions (deckWav (deck), sorted);
        std::int64_t frames = 0;
        if (wav.empty() || slicex::wavRegions (wav, frames).size() != sorted.size() || frames != (std::int64_t) samples[(std::size_t) deck]->frames())
            return false;
        if (records.empty())
            records.push_back (slicex::defaultRegion()); // (a deck keeps one record)
        edit ([deck, &wav, &records, shown] (State& s) {
            auto& d = s.decks[(std::size_t) deck];
            d.wav = wav;
            d.referenceOnly = 0;
            d.regions = records;
            s.ui[1] = (deck << 16) | shown;
        });
        reload();
        return true;
    }

    static std::vector<int> identity (std::size_t n)
    {
        std::vector<int> v (n);
        for (std::size_t i = 0; i < n; ++i)
            v[i] = (int) i;
        return v;
    }

    void deleteRegion (int deck, int index)
    {
        auto list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck))
            return;
        auto from = identity (list.size());
        list.erase (list.begin() + index);
        from.erase (from.begin() + index);
        setRegions (deck, std::move (list), std::move (from), std::max (0, index - 1));
    }

    // FL's "Clone as region": a second region over the same stretch, with the same settings
    void cloneRegion (int deck, int index)
    {
        auto list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck))
            return;
        auto from = identity (list.size());
        // (FL names it "<name> #2", keeps start, length and purpose; the key stays with the first)
        auto copy = list[(std::size_t) index];
        copy.name = slicex::regionName (copy, index) + " #2";
        copy.marker = false;
        copy.key = -1;
        list.insert (list.begin() + index + 1, copy);
        from.insert (from.begin() + index + 1, index);
        setRegions (deck, std::move (list), std::move (from), index + 1);
    }

    void turnIntoMarker (int deck, int index)
    {
        auto list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck))
            return;
        list[(std::size_t) index].marker = true;
        setRegions (deck, std::move (list), identity (regions[(std::size_t) deck].size()), index);
    }

    // FL's "Add marker…" and "Add region…". The marker goes to the selection's start (without one: the place last
    // clicked in the wave, LYNX; FL: the sample's start). The region is the selection: FL wrote 62259…96846 for
    // 1.41176…2.19608 beats, purpose "beat", in sample order, and left the region shown as it was (REA_rodada190aj §3).
    void addRegion (int deck, bool marker, const std::string* named = nullptr)
    {
        const auto& sample = samples[(std::size_t) deck];
        if (sample == nullptr || frozen (deck) || ! canEmbed (deck))
            return;
        const auto total = (std::int64_t) sample->frames();
        const auto selection = deckView[(std::size_t) deck].hasSelection();
        const auto range = selectionFrames (deck);
        slicex::WavRegion made;
        made.marker = marker;
        if (marker)
            made.start = selection ? range.first : std::max<std::int64_t> (0, deckView[(std::size_t) deck].cursor);
        else
        {
            if (! selection || range.end <= range.first)
                return;
            made.start = range.first;
            made.end = range.end - 1;
            made.purpose = "beat";
        }
        if (made.start < 0 || made.start >= total)
            return;
        if (named != nullptr) // (the editor test: no prompt)
        {
            made.name = *named;
            putRegion (deck, made);
            return;
        }
        auto* box = new juce::AlertWindow (tr ("New region name"), {}, juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("name", {});
        box->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SlicexEditor> self (this);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([self, box, deck, made] (int result) {
                                  if (self == nullptr || result != 1)
                                      return;
                                  auto region = made;
                                  region.name = box->getTextEditorContents ("name").trim().toStdString();
                                  self->putRegion (deck, region);
                              }),
                              true);
    }

    void putRegion (int deck, const slicex::WavRegion& region)
    {
        auto list = regions[(std::size_t) deck];
        auto from = identity (list.size());
        list.push_back (region);
        from.push_back (-1);
        // a marker is shown (as before); a region leaves the one shown where it was
        const auto shown = regionDeck() == deck && regionIndex() < (int) list.size() - 1 ? regionIndex() : (int) list.size() - 1;
        const auto show = region.marker ? (int) list.size() - 1 : shown;
        setRegions (deck, std::move (list), std::move (from), show);
    }

    // FL's "Set loop": the selection becomes the sample's loop (first frame … last frame, in the WAV's sampler chunk)
    // and View ▸ Loop goes on, so the loop points are drawn. FL wrote 111671…161081 for 2.53221…3.65266 beats.
    void setLoop (int deck)
    {
        const auto range = selectionFrames (deck);
        if (range.end <= range.first || ! canEmbed (deck))
            return;
        const auto wav = slicex::withLoop (deckWav (deck), range.first, range.end - 1);
        std::int64_t frames = 0;
        const auto kept = slicex::wavRegions (wav, frames);
        if (wav.empty() || frames != (std::int64_t) samples[(std::size_t) deck]->frames()
            || (! state.decks[(std::size_t) deck].wav.empty() && kept.size() != regions[(std::size_t) deck].size()))
            return;
        edit ([deck, &wav] (State& s) {
            auto& d = s.decks[(std::size_t) deck];
            d.wav = wav;
            d.referenceOnly = 0;
            d.params[0x36] = 1;
        });
        reload();
    }

    // An end tag was dropped: the region's length alone changes; it may run over the regions after it (FL: an end
    // dragged 80 px right went from 14637 to 54166, past two region starts)
    void moveRegionEnd (int deck, int index, std::int64_t end)
    {
        auto list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck) || list[(std::size_t) index].marker || samples[(std::size_t) deck] == nullptr)
            return;
        auto& r = list[(std::size_t) index];
        const auto last = std::clamp<std::int64_t> (end - 1, r.start, (std::int64_t) samples[(std::size_t) deck]->frames() - 1);
        if (last == r.end)
            return;
        r.end = last;
        setRegions (deck, std::move (list), identity (regions[(std::size_t) deck].size()), index);
    }

    void moveRegionStart (int deck, int index, std::int64_t frame)
    {
        auto list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck) || list[(std::size_t) index].start == frame)
            return;
        auto& r = list[(std::size_t) index];
        if (! r.marker)
            frame = std::min (frame, r.end); // (a region keeps its end: FL moves the start tag alone)
        r.start = frame;
        setRegions (deck, std::move (list), identity (regions[(std::size_t) deck].size()), index);
    }

    // Where a dragged region start lands: where the mouse is. FL's Snap menu (grid, regions, zero-crossing…) acts on
    // selections and envelope points; a region tag dragged with "Snap to grid" or "Snap to zero-crossing" on moved by
    // the mouse's distance exactly (REA_rodada190ac §2). It stays before the region's own end.
    std::int64_t snapStart (int deck, int index, std::int64_t frame) const
    {
        const auto& list = regions[(std::size_t) deck];
        if (index >= 0 && index < (int) list.size() && ! list[(std::size_t) index].marker)
            return std::min (frame, list[(std::size_t) index].end);
        return frame;
    }

    // FL's "Rename..." (one region) and "Rename all..." (every region in turn, from `index` on)
    void renameRegion (int deck, int index, bool all)
    {
        const auto& list = regions[(std::size_t) deck];
        if (index < 0 || index >= (int) list.size() || frozen (deck))
            return;
        if (all)
            selectRegion (deck, index, false);
        auto* box = new juce::AlertWindow (tr ("Rename"), {}, juce::MessageBoxIconType::NoIcon, this);
        box->addTextEditor ("name", juce::String::fromUTF8 (slicex::regionName (list[(std::size_t) index], index).c_str()));
        box->addButton (tr ("Accept"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        box->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SlicexEditor> self (this);
        box->enterModalState (true, juce::ModalCallbackFunction::create ([self, box, deck, index, all] (int result) {
                                  if (self == nullptr || result != 1)
                                      return;
                                  auto list = self->regions[(std::size_t) deck];
                                  if (index >= (int) list.size())
                                      return;
                                  const auto name = box->getTextEditorContents ("name").trim().toStdString();
                                  if (name != slicex::regionName (list[(std::size_t) index], index))
                                  {
                                      list[(std::size_t) index].name = name;
                                      self->setRegions (deck, std::move (list), identity (self->regions[(std::size_t) deck].size()), index);
                                  }
                                  if (all && index + 1 < (int) self->regions[(std::size_t) deck].size())
                                      self->renameRegion (deck, index + 1, true);
                              }),
                              true);
    }

    // FL's Options menu (MenuBtn): the settings
    void optionsMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (tr ("Settings"));
        struct Item { const char* text; int option; };
        static const Item items[] = { { "Sample start covers whole length", slicex::oStartWholeLength },
                                      { "Link velocity to volume", slicex::oVelocityToVolume },
                                      { "Loop half of regions", slicex::oLoopHalf },
                                      { "HQ envelopes", slicex::oHqEnvelopes } };
        for (int i = 0; i < 4; ++i)
            m.addItem (1 + i, tr (items[i].text), true, state.optionByte (items[i].option) != 0);
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton), [self] (int r) {
            if (self == nullptr || r < 1 || r > 4)
                return;
            const auto option = items[r - 1].option;
            self->edit ([option] (State& s) { s.setOptionByte (option, s.optionByte (option) != 0 ? 0 : 1); });
        });
    }

    // FL's keyboard menu (MIDIKbMenu)
    void keyMenu (int key)
    {
        juce::PopupMenu m;
        const auto on = key >= 0 && key < 128 && (! maps[0][(std::size_t) key].empty() || ! maps[1][(std::size_t) key].empty());
        m.addItem (1, tr ("Assign current region to this key"), key >= 0 && key < 128 && record() != nullptr && canSetKeys (regionDeck()));
        m.addItem (2, tr ("Select region assigned to this key"), on);
        juce::Component::SafePointer<SlicexEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self, key] (int r) {
            if (self != nullptr && r == 1)
                self->setRegionKey (self->regionDeck(), self->regionIndex(), key);
            if (self == nullptr || r != 2)
                return;
            for (int d = 0; d < 2; ++d)
                if (! self->maps[(std::size_t) d][(std::size_t) key].empty())
                {
                    const auto id = self->maps[(std::size_t) d][(std::size_t) key].front();
                    self->selectRegion (id >> 16, id & 0xffff, false);
                    return;
                }
        });
    }

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel;
    State state = slicex::fresh();
    std::vector<std::uint8_t> lastState;
    std::array<std::string, 2> paths;
    std::array<std::shared_ptr<const SampleData>, 2> samples;
    std::array<std::vector<slicex::WavRegion>, 2> regions;
    std::array<slicex::KeyMap, 2> maps;
    bool loading = false, dragUndo = false, lastPicked = false;
    std::vector<Bound> bound;
    std::vector<std::pair<juce::ToggleButton*, int>> checks;

    juce::Slider masterVol, masterRand, masterLfo, masterPitch, modX, modY;
    XyPad pad;
    juce::Label modulationLabel, modXLabel, modYLabel, layeringLabel, crossfadeLabel, brand, regionHeader;
    Led modSmooth, sameDeck, autoDump;
    IconButton optionsButton { IconButton::arrow }, regionMenuButton { IconButton::arrow };
    juce::ComboBox layering, regionSel;
    Selector crossfade, artGroup, filterType, filterOrder, target, partSel, deckSel;
    Digit output, cutGroup, octave;
    juce::Label outLabel, cutLabel, artGroupLabel, filterLabel, filterEnvLabel, filterCutLabel, filterResLabel;
    std::array<juce::Slider, 4> rowA, rowB, rowAmount;
    std::array<juce::Label, 4> rowLabel, artLabel;
    std::array<Digit, 4> rowGroup;
    juce::Slider filterEnv, filterCut, filterRes;
    CurveView curveView;
    Led curveOn, curveTempo, curveGlobal;
    std::array<juce::Slider, 4> envKnob;
    std::array<juce::Label, 4> envKnobLabel;
    std::array<juce::Slider, 4> lfoKnob;
    std::array<juce::Label, 4> lfoKnobLabel;
    IconButton loopButton { IconButton::loop }, playButton { IconButton::play }, stopButton { IconButton::stop }, fileButton { IconButton::file },
        regionsButton { IconButton::flag }, selectButton { IconButton::select }, zoomButton { IconButton::zoom }, dualButton { IconButton::dual };
    juce::Label rateLabel, tempoLabel, nameLabel, selLabel;
    std::array<DeckView, 2> deckView;
    std::array<const void*, 2> viewOf { nullptr, nullptr }; // the sample each deck view last opened (reload)
    std::array<std::pair<std::int64_t, std::int64_t>, 2> loops {}; // each deck's loop points (first, last frame)
    std::array<bool, 2> loopShown { false, false };          // … and whether they are drawn (FL's View ▸ Loop)
    Keyboard keyboard;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

juce::String slicexEditorTest (juce::Component& root, const juce::String& command)
{
    std::function<SlicexEditor* (juce::Component&)> find = [&find] (juce::Component& c) -> SlicexEditor* {
        if (auto* e = dynamic_cast<SlicexEditor*> (&c))
            return e;
        for (auto* child : c.getChildren())
            if (auto* e = find (*child))
                return e;
        return nullptr;
    };
    auto* editor = find (root);
    return editor != nullptr ? editor->test (juce::StringArray::fromTokens (command, " ", "\"")) : juce::String ("no Slicex editor");
}

static std::optional<std::pair<int, juce::String>>& pendingSample()
{
    static std::optional<std::pair<int, juce::String>> pending;
    return pending;
}

void loadSlicexSampleWhenOpened (int channel, const juce::String& path)
{
    if (channel >= 0)
        pendingSample() = std::make_pair (channel, path);
    else
        pendingSample().reset();
}

std::unique_ptr<juce::Component> makeSlicexEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    auto editor = std::make_unique<SlicexEditor> (engine, std::move (locate), channel);
    if (auto& p = pendingSample(); p.has_value() && channel >= 0 && p->first == channel)
    {
        const auto path = p->second;
        p.reset();
        editor->loadSampleNow (path);
    }
    return editor;
}
} // namespace lynx
