#include "ui/DirectWaveEditor.h"
#include "ui/FileChoosers.h"

#include <algorithm>
#include <cmath>

#include "core/DirectWave.h"
#include "core/DirectWaveState.h"
#include "core/DirectWaveZoneDrag.h"
#include "core/FlpImport.h"
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
constexpr float scale = 1.25f; // FL's form (990 × 627), a quarter larger

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

juce::String text (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

bool isBlack (int key)
{
    const auto k = key % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}

// the menus' entries (DirectWave's editor form resource, the menu items in order; their tags are the stored values)
const char* const loopModes[] = { TR_NOOP ("Disabled"), TR_NOOP ("One-Shot"), TR_NOOP ("Forward"), TR_NOOP ("Sustained"), TR_NOOP ("Bounce") };
const char* const filterTypes[] = { TR_NOOP ("Off"), TR_NOOP ("Lowpass"), TR_NOOP ("Highpass"), TR_NOOP ("Bandpass"), TR_NOOP ("Notch"), TR_NOOP ("Allpass"), TR_NOOP ("Minisynth"), TR_NOOP ("Vox") };
const char* const waveforms[] = { TR_NOOP ("Sine"), TR_NOOP ("Abs Sine"), TR_NOOP ("Triangle"), TR_NOOP ("Square"), TR_NOOP ("Saw"), TR_NOOP ("Inv Saw"), TR_NOOP ("Random"), TR_NOOP ("LP Random") };
const char* const playModes[] = { TR_NOOP ("Poly"), TR_NOOP ("Mono"), TR_NOOP ("Legato") };
const char* const triggerTypes[] = { TR_NOOP ("Normal"), TR_NOOP ("Cycle"), TR_NOOP ("Random"), TR_NOOP ("Avoid prev.") };
const char* const modSources[] = { TR_NOOP ("none"), TR_NOOP ("Note Key"), TR_NOOP ("Note Velocity"), TR_NOOP ("Mod Wheel"), TR_NOOP ("Pitch Bend"), TR_NOOP ("Program Lfo 1"), TR_NOOP ("Program Lfo 2"),
                                   "<Reserved>", "<Reserved>", TR_NOOP ("Zone Env 1"), TR_NOOP ("Zone Env 2"), TR_NOOP ("Zone Lfo 1"), TR_NOOP ("Zone Lfo 2"), TR_NOOP ("Amp Follower"),
                                   TR_NOOP ("Sample Pos"), TR_NOOP ("Random Value"), TR_NOOP ("Mod Val 1"), TR_NOOP ("Mod Val 2"), TR_NOOP ("Mod Val 3"), TR_NOOP ("Mod Val 4") };
const char* const modDestinations[] = { TR_NOOP ("none"), TR_NOOP ("Voice Pitch"), TR_NOOP ("Voice Gain"), TR_NOOP ("Voice Pan"), TR_NOOP ("Sample Start"), TR_NOOP ("Loop Start"), TR_NOOP ("Loop End"),
                                        TR_NOOP ("Fl1 Cutoff"), TR_NOOP ("Fl1 Resonance"), TR_NOOP ("Fl1 Shape"), TR_NOOP ("Fl2 Cutoff"), TR_NOOP ("Fl2 Resonance"), TR_NOOP ("Fl2 Shape"),
                                        TR_NOOP ("Amp Env Att"), TR_NOOP ("Amp Env Dec"), TR_NOOP ("Amp Env Sus"), TR_NOOP ("Amp Env Rel"), TR_NOOP ("Ring Mode Rate"), TR_NOOP ("Ring Mode Mix"),
                                        TR_NOOP ("Decimator Stp"), TR_NOOP ("Decimator Mix"), TR_NOOP ("Reducer Bits"), TR_NOOP ("Reducer Mix"), TR_NOOP ("Phaser Freq"), TR_NOOP ("Phaser Mix"),
                                        TR_NOOP ("Delay Send"), TR_NOOP ("Chorus Send"), TR_NOOP ("Reverb Send"), TR_NOOP ("Dry Amount"), TR_NOOP ("Ts. Time"), TR_NOOP ("Ts. Grain"), TR_NOOP ("Ts. Smooth"),
                                        TR_NOOP ("Mod Amt P1-1"), TR_NOOP ("Mod Amt P1-2"), TR_NOOP ("Mod Amt P1-3"), TR_NOOP ("Mod Amt P1-4"), TR_NOOP ("Mod Amt P2-1"), TR_NOOP ("Mod Amt P2-2"),
                                        TR_NOOP ("Mod Amt P2-3"), TR_NOOP ("Mod Amt P2-4"), TR_NOOP ("Mod Amt P3-1"), TR_NOOP ("Mod Amt P3-2"), TR_NOOP ("Mod Amt P3-3"), TR_NOOP ("Mod Amt P3-4"),
                                        TR_NOOP ("Mod Amt P4-1"), TR_NOOP ("Mod Amt P4-2"), TR_NOOP ("Mod Amt P4-3"), TR_NOOP ("Mod Amt P4-4") };

template <std::size_t N>
juce::String entry (const char* const (&list)[N], int i)
{
    return i >= 0 && i < (int) N ? juce::String (list[i]) : juce::String ("---");
}

// FL's value texts: the plugin's parameter text (DirectWave_x64.dll 0x180027740 and its helpers 0x180028080 dB,
// 0x1800283d0 time, 0x180028870 LFO rate, 0x1800286d0 phase, 0x180028ca0 pan, 0x180010f30 cutoff; REA_rodada190hz_dw.md §4)
juce::String fmt (const char* f, double v) { return juce::String::formatted (f, v); }
juce::String percent2 (float v) { return fmt ("%.2f%%", (double) v * 100.0); }
juce::String percent1 (float v) { return fmt ("%.1f%%", (double) v * 100.0); }
juce::String cents (float v) { return juce::String::formatted ("%d Cents", (int) ((v - 0.5f) * 2400.0f)); }
juce::String decibels (float v) // the gain law v²
{
    const auto g = v * v;
    return g <= 0.0f ? juce::String ("-oo dB") : fmt ("%.1f dB", 20.0 * std::log10 ((double) g));
}
juce::String panText (float v)
{
    const auto p = (int) (v * 100.0f);
    return p > 50 ? juce::String::formatted ("+%.2d R", p - 50) : p < 50 ? juce::String::formatted ("%.2d L", p - 50) : juce::String ("Center");
}
juce::String seconds (float v) { return fmt ("%.3f s", (double) (v * v * v * v) * 10.0); }
juce::String hertz (float v) { return fmt ("%.1f Hz", std::pow (2.0, (double) (v * 127.0f) / 12.0 - 5.0) * 440.0); }
juce::String phaseText (float v) { return v < 1.0f ? fmt ("%.1f", (double) v * 360.0) : juce::String ("Free Run"); }
juce::String lfoRate (float v, bool sync)
{
    return sync ? juce::String::formatted ("%d Ticks", (int) (v * v * 127.5f + 1.0f)) : fmt ("%.3f Hz", (double) (v * v * 20.0f));
}
juce::String amountText (float v) { return fmt ("%.1f%%", (double) v * 200.0 - 100.0); }
juce::String plain3 (float v) { return fmt ("%.3f", (double) v); }

// ---------------------------------------------------------------------------------------------------------------
// FL's small switch (TVectorCheckBox)
class Led final : public juce::Component
{
public:
    std::function<void (bool)> onChange;

    void set (bool nowOn)
    {
        if (on != nowOn)
        {
            on = nowOn;
            repaint();
        }
    }
    bool get() const { return on; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto d = std::min (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (d, d);
        g.setColour (theme::colour::bg4);
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (r, 2.5f, 1.0f);
        g.setColour (on ? theme::colour::accent : theme::colour::bg0);
        g.fillEllipse (r.reduced (d * 0.27f));
        if (caption.isNotEmpty())
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        on = ! on;
        repaint();
        if (onChange)
            onChange (on);
    }

    juce::String caption;

private:
    bool on = false;
};

// a switch with its caption on the right (FL's checkboxes with text: Fixed, Auto, Current selection…)
class CaptionLed final : public juce::Component
{
public:
    CaptionLed (const juce::String& c, bool upwards = false) : vertical (upwards), caption (c) {}
    std::function<void()> onClick;
    bool on = false, vertical = false;

    void paint (juce::Graphics& g) override
    {
        // FL's round radio switch, its caption to the right (or upwards beside it when `vertical`)
        const auto d = std::min ({ (float) getHeight(), (float) getWidth(), 12.0f * 1.25f }) - 2.0f;
        const auto box = vertical ? juce::Rectangle<float> (d, d).withCentre ({ (float) getWidth() * 0.5f, d * 0.5f + 1.0f })
                       : caption.isEmpty() ? juce::Rectangle<float> (d, d).withCentre (getLocalBounds().toFloat().getCentre())
                                           : juce::Rectangle<float> (1.0f, ((float) getHeight() - d) * 0.5f, d, d);
        g.setColour (on ? theme::colour::accent : theme::colour::bg0);
        g.fillEllipse (box);
        g.setColour (theme::colour::borderStrong);
        g.drawEllipse (box, 1.0f);
        g.setColour (on ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        if (vertical)
        {
            const auto r = getLocalBounds().toFloat().withTrimmedTop (d + 3.0f);
            juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
            g.drawText (caption, juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()), juce::Justification::centred, false);
        }
        else
            g.drawText (caption, getLocalBounds().withTrimmedLeft ((int) d + 5), juce::Justification::centredLeft, false);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }

private:
    juce::String caption;
};

// FL's tab selector (TQuickTabSelector): one cell per page
class Tabs final : public juce::Component
{
public:
    juce::StringArray names;
    int current = 0;
    bool vertical = false;
    std::function<void (int)> onChange;

    juce::Rectangle<int> cell (int i) const
    {
        const auto n = std::max (1, names.size());
        if (vertical)
        {
            const auto h = getHeight() / n;
            return { 0, i * h, getWidth(), h };
        }
        const auto w = getWidth() / n;
        return { i * w, 0, i == n - 1 ? getWidth() - i * w : w, getHeight() };
    }

    void paint (juce::Graphics& g) override
    {
        for (int i = 0; i < names.size(); ++i)
        {
            const auto r = cell (i).reduced (1).toFloat();
            const auto on = i == current;
            g.setColour (on ? theme::colour::bg6 : theme::colour::bg3);
            g.fillRoundedRectangle (r, 2.0f);
            if (on)
            {
                g.setColour (theme::colour::accent);
                if (vertical)
                    g.fillRect (r.withWidth (2.0f));
                else
                    g.fillRect (r.withTop (r.getBottom() - 2.0f));
            }
            g.setColour (on ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption, true));
            if (vertical)
            {
                // the page names run upwards, as in FL's page selector
                juce::Graphics::ScopedSaveState s (g);
                const auto c = cell (i).toFloat();
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.getCentreX(), c.getCentreY()));
                g.drawText (names[i], juce::Rectangle<float> (c.getHeight(), c.getWidth()).withCentre (c.getCentre()), juce::Justification::centred, false);
            }
            else
                g.drawText (names[i], cell (i), juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < names.size(); ++i)
            if (cell (i).contains (e.getPosition()))
            {
                current = i;
                repaint();
                if (onChange)
                    onChange (i);
            }
    }
};

// a wheel (TVectorWheel) on a value 0…1, with FL's label to its left: the name, and the value while it is touched
// FL's label box beside a wheel: the name in capitals over the value
class ValueBox final : public juce::Component
{
public:
    juce::String name, value;

    void paint (juce::Graphics& g) override
    {
        if (name.isEmpty())
            return;
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg3);
        g.fillRoundedRectangle (r, 2.5f);
        const auto valueArea = r.withTrimmedTop (r.getHeight() * 0.5f).reduced (2.0f, 1.5f);
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (valueArea, 2.0f);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::caption, true));
        g.drawText (name.toUpperCase(), r.withHeight (r.getHeight() * 0.5f).withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (value, valueArea.withTrimmedLeft (3.0f), juce::Justification::centredLeft, true);
    }
};

// a wheel (TVectorWheel) on a value 0…1 with FL's label box to its left
struct Wheel
{
    juce::Slider knob;
    ValueBox label;
    juce::String name;
    std::function<juce::String (float)> valueText;
};

// the waveform of the selected zone's sample with its start and loop points (FL's TMEnvEditor in the Sample page)
class SampleView final : public juce::Component
{
public:
    std::function<std::shared_ptr<const SampleData>()> sample;
    std::function<const dw::Zone*()> zone;
    std::function<void()> onClick;

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::colour::bg0);
        g.fillRect (getLocalBounds());
        const auto data = sample ? sample() : nullptr;
        const auto* z = zone ? zone() : nullptr;
        const auto mid = (float) getHeight() * 0.5f;
        g.setColour (theme::colour::border);
        g.drawHorizontalLine ((int) mid, 0.0f, (float) getWidth());
        if (data == nullptr || data->frames() == 0)
            return;
        const auto frames = data->frames();
        const auto* l = data->leftData();
        g.setColour (theme::colour::accent.withAlpha (0.85f));
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
        if (z == nullptr)
            return;
        auto at = [&] (std::int64_t f) { return (float) ((double) f / (double) frames * getWidth()); };
        g.setFont (theme::font (theme::type::caption, true));
        auto marker = [&] (std::int64_t f, const juce::String& tag, juce::Colour c) {
            const auto x = at (f);
            g.setColour (c);
            g.drawVerticalLine (juce::roundToInt (x), 0.0f, (float) getHeight());
            const juce::Rectangle<float> r (x, (float) getHeight() - 14.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag) + 6.0f, 14.0f);
            g.fillRect (r);
            g.setColour (theme::colour::bg0);
            g.drawText (tag, r, juce::Justification::centred, false);
        };
        marker (z->offset, tr ("Start"), theme::colour::textSecondary);
        if (z->loopMode >= 2 && z->loopB > z->loopA)
        {
            marker (z->loopA, tr ("Loop"), theme::colour::warning);
            marker (z->loopB, tr ("End"), theme::colour::warning);
        }
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick)
            onClick();
    }
};

// ---------------------------------------------------------------------------------------------------------------
class DirectWaveEditor final : public juce::Component,
                               public juce::FileDragAndDropTarget,
                               private juce::ChangeListener
{
public:
    DirectWaveEditor (AudioEngine& e, std::function<ChannelDef* (Song&)> l, int c) : engine (e), locate (std::move (l)), channel (c)
    {
        setName ("DirectWaveEditor");
        setComponentID ("directwave-editor");
        buildTop();
        buildGrid();
        buildZonePage();
        buildSamplePage();
        buildProgramPage();
        buildSide();
        reload();
        setSize (juce::roundToInt (990 * scale), juce::roundToInt (627 * scale));
        engine.addChangeListener (this);
    }

    ~DirectWaveEditor() override { engine.removeChangeListener (this); }

    // ---- the editor test (LYNX --dwedittest): "page zone|sample|program", "tab main|time|trigger|filter1|filter2|amp|
    // env1|env2|lfo1|lfo2|matrix1…4", "side browser|options", "select <i>", "add <file>", "automap", "drumkit",
    // "delete", "duplicate", "set <field> <value>" (root, low, high, lowvel, highvel, gain, pan, pitch, loop), "program
    // <next|prev|n>", "name <text>", "save <file.dwp>", "open <file>", "state" (what the state holds), "mapdrag <key> <vel>
    // <dkeys> <dvel> [alt|ctrl|ctrlalt]" (a drag in the zone map from that key / velocity; round 190jd)
    juce::String test (const juce::String& command)
    {
        const auto word = command.upToFirstOccurrenceOf (" ", false, false).trim();
        const auto arg = command.fromFirstOccurrenceOf (" ", false, false).trim();
        if (word == "page")
        {
            setPage (arg == "sample" ? pageSample : arg == "program" ? pageProgram : pageZone);
            return "OK";
        }
        if (word == "tab")
        {
            if (arg == "main" || arg == "time" || arg == "trigger")
                mainTabs.current = arg == "main" ? 0 : arg == "time" ? 1 : 2;
            else if (arg.startsWith ("filter"))
                filterTabs.current = arg.getLastCharacters (1).getIntValue() - 1;
            else if (arg == "amp" || arg.startsWith ("env"))
                envTabs.current = arg == "amp" ? 0 : arg.getLastCharacters (1).getIntValue();
            else if (arg.startsWith ("lfo"))
                lfoTabs.current = arg.getLastCharacters (1).getIntValue() - 1;
            else if (arg.startsWith ("matrix"))
                matrixTabs.current = arg.getLastCharacters (1).getIntValue() - 1;
            refresh();
            return "OK";
        }
        if (word == "side")
        {
            sideTabs.current = arg == "options" ? 1 : 0;
            refresh();
            return "OK";
        }
        if (word == "select")
        {
            selectZone (arg.getIntValue(), false, false);
            return "OK";
        }
        if (word == "add")
        {
            addSamples ({ arg }, false);
            return juce::String ((int) prog.zones.size()) + " zones";
        }
        if (word == "automap" || word == "drumkit" || word == "delete" || word == "duplicate" || word == "deleteall")
        {
            zoneCommand (word == "automap" ? 16606 : word == "drumkit" ? 16607 : word == "delete" ? 16601 : word == "deleteall" ? 16602 : 16603);
            return zonesText();
        }
        if (word == "set")
        {
            const auto field = arg.upToFirstOccurrenceOf (" ", false, false);
            const auto value = arg.fromFirstOccurrenceOf (" ", false, false).getFloatValue();
            editZones ([&] (dw::Zone& z) {
                if (field == "root") z.root = (int) value;
                else if (field == "low") z.lowKey = (int) value;
                else if (field == "high") z.highKey = (int) value;
                else if (field == "lowvel") z.lowVel = (int) value;
                else if (field == "highvel") z.highVel = (int) value;
                else if (field == "gain") z.gain = value;
                else if (field == "pan") z.pan = value;
                else if (field == "pitch") z.pitch = value;
                else if (field == "loop") z.loopMode = (int) value;
            });
            return zonesText();
        }
        if (word == "program")
        {
            showProgram (arg == "next" ? shown + 1 : arg == "prev" ? shown - 1 : arg.getIntValue());
            return juce::String (shown) + " " + text (prog.name);
        }
        if (word == "name")
        {
            editProgram ([&] (dw::Program& p) { p.name = arg.toStdString(); });
            return text (prog.name);
        }
        if (word == "save")
            return saveProgramTo (juce::File (arg)) ? "OK" : "FAILED";
        if (word == "open")
            return openFile (juce::File (arg)) ? "OK" : "FAILED";
        if (word == "state")
            return zonesText();
        if (word == "mapdrag") // (round 190jd) mapdrag <key> <vel> <dkeys> <dvel> [alt|ctrl|ctrlalt]: a left drag in the zone map
        {
            const auto a = juce::StringArray::fromTokens (arg, " ", "");
            const auto from = juce::Point<float> (keyMap.keyX (a[0].getIntValue()) + 0.5f * (keyMap.keyX (1) - keyMap.keyX (0)),
                                                  keyMap.velY (a[1].getIntValue()) + 0.5f * keyMap.velY (1));
            const auto to = from + juce::Point<float> (keyMap.keyX (a[2].getIntValue()) - keyMap.keyX (0), keyMap.velY (a[3].getIntValue()) - keyMap.velY (0));
            const auto mods = juce::ModifierKeys::leftButtonModifier | (a[4].contains ("alt") ? juce::ModifierKeys::altModifier : 0)
                              | (a[4].startsWith ("ctrl") ? juce::ModifierKeys::ctrlModifier : 0);
            auto ev = [&] (juce::Point<float> p, bool dragged) {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys (mods), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &keyMap,
                                         &keyMap, now, from, now, 1, dragged);
            };
            keyMap.mouseDown (ev (from, false));
            for (int i = 1; i <= 4; ++i)
                keyMap.mouseDrag (ev (from + (to - from) * ((float) i / 4.0f), true));
            keyMap.mouseUp (ev (to, true));
            return zonesText();
        }
        return "unknown command";
    }

    // ---- FileDragAndDropTarget: samples dropped on the editor become zones (FL's MainDropFileTarget); a .dwp / .dwb loads
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() == 1 && (files[0].endsWithIgnoreCase (".dwp") || files[0].endsWithIgnoreCase (".dwb") || files[0].endsWithIgnoreCase (".sfz")))
            openFile (juce::File (files[0]));
        else
            addSamples (files, false);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        // the frames of FL's form: the zone area, the bottom pages, the side panel
        g.setColour (theme::colour::bg1);
        g.fillRoundedRectangle (fl (7, 61, 722, 331).toFloat(), 3.0f);
        g.fillRoundedRectangle (fl (7, 396, 722, 225).toFloat(), 3.0f);
        g.fillRoundedRectangle (fl (741, 62, 242, 559).toFloat(), 3.0f);
        // the program bar (FL's PROGRAM tab of the Multi bank / Program / Library selector)
        {
            const auto bar = fl (7 + 250, 38, 230, 23).toFloat();
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (tr ("PROGRAM"), bar.withWidth (52.0f * scale).withTrimmedLeft (6.0f), juce::Justification::centredLeft, false);
        }
        // the group boxes of the pages (FL's shot, the form's panels): a header with the group's name over its controls
        g.setFont (theme::font (theme::type::caption, true));
        const auto group = [&] (int x, int y, int w, int h, const juce::String& name) {
            const auto r = fl (30 + x, 396 + y, w, h).toFloat();
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (r, 3.0f);
            if (name.isNotEmpty())
            {
                g.setColour (theme::colour::bg4);
                g.fillRoundedRectangle (r.withHeight (16.0f * scale), 3.0f);
                g.setColour (theme::colour::textPrimary);
                g.drawText (name, r.withHeight (16.0f * scale).withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);
            }
        };
        if (page == pageZone)
        {
            group (5, 5, 122, 157, {});
            group (131, 5, 122, 157, {});
            group (257, 5, 155, 157, {});
            group (416, 5, 122, 157, {});
            group (542, 5, 122, 157, tr ("MOD MATRIX"));
            static const int bx[] = { 5, 92, 184, 278, 370, 492 }, bw[] = { 84, 89, 91, 89, 119, 172 };
            static const char* const names[] = { TR_NOOP ("RINGMOD"), TR_NOOP ("DECIMATOR"), TR_NOOP ("QUANTIZER"), TR_NOOP ("PHASER"),
                                                 TR_NOOP ("SEND FX"), TR_NOOP ("EDIT / AUTOMATION") };
            for (int k = 0; k < 6; ++k)
                group (bx[k], 167, bw[k], 53, tr (names[k]));
            g.setColour (theme::colour::textSecondary);
            const auto under = [&] (int x, const juce::String& s) {
                g.drawText (s, fl (30 + x - 8, 396 + 210, 46, 10), juce::Justification::centred, false);
            };
            under (14, tr ("Rate")); under (51, tr ("Mix")); under (105, tr ("Step")); under (142, tr ("Mix"));
            under (200, tr ("Bits")); under (236, tr ("Mix")); under (287, tr ("Freq")); under (325, tr ("Mix"));
            under (375, tr ("Delay")); under (414, tr ("Chorus")); under (454, tr ("Reverb"));
            // the envelope sliders' letters
            static const int ex[] = { 14, 47, 80, 114 };
            static const char* const letters[] = { "A", "D", "S", "R" };
            for (int k = 0; k < 4; ++k)
                g.drawText (letters[k], fl (30 + 257 + ex[k], 396 + 25, 20, 11), juce::Justification::centred, false);
        }
        if (page == pageProgram)
        {
            group (5, 5, 211, 52, tr ("MASTER"));
            group (220, 5, 192, 52, tr ("GLIDE"));
            group (416, 5, 122, 52, tr ("FX DRIVE A"));
            group (542, 5, 122, 52, tr ("FX DRIVE B"));
            group (5, 63, 122, 157, tr ("LFO 1"));
            group (131, 63, 122, 157, tr ("LFO 2"));
            group (257, 63, 155, 157, tr ("FX DELAY"));
            group (416, 63, 122, 157, tr ("FX REVERB"));
            group (542, 63, 122, 157, tr ("FX CHORUS"));
        }
        if (page == pageSample)
        {
            group (5, 168, 122, 52, tr ("SAMPLE"));
            group (131, 168, 400, 52, tr ("LOOP"));
        }
        // the dropdowns' boxes: the name over the chosen entry, like the wheels' labels
        for (const auto& m : menuNames)
            if (m.label->isVisible())
            {
                const auto r = m.label->getBounds().getUnion (m.button->getBounds()).toFloat().withTop ((float) m.label->getY() - 15.0f * scale).expanded (2.0f, 1.0f);
                g.setColour (theme::colour::bg4);
                g.fillRoundedRectangle (r, 2.5f);
                g.setColour (theme::colour::textPrimary);
                g.setFont (theme::font (theme::type::caption, true));
                g.drawText (m.name.toUpperCase(), r.withHeight (15.0f * scale).withTrimmedLeft (4.0f), juce::Justification::centredLeft, false);
            }
        if (sideTabs.current == 1)
        {
            const auto r = fl (741 + 5, 62 + 477, 240, 70).toFloat();
            g.setColour (theme::colour::bg3);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::caption, true));
            g.drawText (tr ("Compatibility"), r.withHeight (16.0f * scale).withTrimmedLeft (5.0f), juce::Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        // the top bar
        diskButton.setBounds (fl (7, 13, 24, 18));
        programIndex.setBounds (fl (303 + 2, 41 + 3, 22, 12));
        programPrev.setBounds (fl (303 + 26, 41 + 1, 15, 15));
        programNext.setBounds (fl (303 + 41, 41 + 1, 15, 15));
        programMenu.setBounds (fl (303 + 57, 41 + 1, 15, 15));
        programName.setBounds (fl (303 + 74, 41 + 3, 111, 12));
        // the zone grid and the key / velocity map, the keyboard
        const auto area = fl (7 + 7, 61 + 18, 709, 283);
        const auto gridHeight = juce::roundToInt ((zoneEditorBig ? 33 : 125) * scale);
        grid.setBounds (area.withHeight (gridHeight));
        keyMap.setBounds (area.withTrimmedTop (gridHeight + juce::roundToInt (4 * scale)));
        toggleKeyMap.setBounds (fl (7 + 6, 61 + 302, 16, 27));
        keyboard.setBounds (fl (7 + 27, 61 + 303, 675, 25));
        // the bottom pages
        pageTabs.setBounds (fl (7, 396, 23, 225));
        layoutZonePage();
        layoutSamplePage();
        layoutProgramPage();
        meter.setBounds (fl (691, 400, 26, 217));
        // the side panel
        sideTabs.setBounds (fl (741, 39, 242, 23));
        layoutSide();
    }

private:
    enum { pageSample = 0, pageZone = 1, pageProgram = 2 }; // (FL's page selector, top to bottom)

    // ============================================================================================ building
    void buildTop()
    {
        button (diskButton, juce::String::fromUTF8 ("\xe2\x96\xbe"), tr ("Disk operations"), [this] { diskMenu(); });
        button (programPrev, juce::String::fromUTF8 ("\xe2\x97\x80"), tr ("Previous memory program"), [this] { showProgram (shown - 1); });
        button (programNext, juce::String::fromUTF8 ("\xe2\x96\xb6"), tr ("Next memory program"), [this] { showProgram (shown + 1); });
        button (programMenu, juce::String::fromUTF8 ("\xe2\x96\xbe"), tr ("Memory program menu"), [this] { programListMenu(); });
        for (auto* lab : { &programIndex, &programName })
        {
            lab->setFont (theme::font (theme::type::small));
            lab->setColour (juce::Label::backgroundColourId, theme::colour::bg0);
            lab->setColour (juce::Label::textColourId, theme::colour::textPrimary);
            lab->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*lab);
        }
        programIndex.setJustificationType (juce::Justification::centred);
        ui::setHint (programIndex, tr ("Program index"));
        programName.setEditable (false, true, false);
        ui::setHint (programName, tr ("Change program name"));
        programName.onTextChange = [this] {
            const auto name = programName.getText().toStdString();
            editProgram ([name] (dw::Program& p) { p.name = name; });
        };
    }

    void buildGrid()
    {
        addAndMakeVisible (grid);
        grid.owner = this;
        addAndMakeVisible (keyMap);
        keyMap.owner = this;
        ui::setHint (keyMap, tr ("Zone map: drag a zone to move its keys and velocities (Alt: the root too), its corner squares to resize it"));
        button (toggleKeyMap, juce::String::fromUTF8 ("\xe2\x86\x95"), tr ("Maximize/Minimize zone editor"), [this] {
            zoneEditorBig = ! zoneEditorBig;
            resized();
        });
        addAndMakeVisible (keyboard);
        keyboard.owner = this;
        ui::setHint (keyboard, tr ("Click to play; the selected zone's keys are marked"));
        pageTabs.vertical = true;
        pageTabs.names = { tr ("SAMPLE"), tr ("ZONE"), tr ("PROGRAM") };
        pageTabs.current = page;
        pageTabs.onChange = [this] (int i) { setPage (i); };
        ui::setHint (pageTabs, tr ("Page selector"));
        addAndMakeVisible (pageTabs);
        addAndMakeVisible (meter);
    }

    Wheel& wheel (std::vector<std::unique_ptr<Wheel>>& list, const juce::String& name, std::function<juce::String (float)> valueText,
                  float reset, std::function<void (float)> set)
    {
        auto w = std::make_unique<Wheel>();
        w->name = name;
        w->valueText = std::move (valueText);
        w->knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        w->knob.setRange (0.0, 1.0, 0.0);
        w->knob.setDoubleClickReturnValue (true, reset);
        w->knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        w->knob.setPopupDisplayEnabled (true, true, this);
        auto* raw = w.get();
        w->knob.textFromValueFunction = [raw] (double v) { return raw->valueText ? raw->valueText ((float) v) : percent2 ((float) v); };
        w->knob.onValueChange = [this, raw, set] {
            raw->label.value = raw->knob.getTextFromValue (raw->knob.getValue());
            raw->label.repaint();
            if (! loading)
                set ((float) raw->knob.getValue());
        };
        ui::setHint (w->knob, name);
        w->label.name = name;
        w->label.setInterceptsMouseClicks (false, false);
        addChildComponent (w->knob);
        addChildComponent (w->label);
        list.push_back (std::move (w));
        return *list.back();
    }

    // a zone wheel: shown on page `part`; its value is the current zone's, a change goes to the zones being edited
    Wheel& zoneWheel (int part, const juce::String& name, std::function<juce::String (float)> valueText, float reset,
                      std::function<float& (dw::Zone&)> field)
    {
        auto& w = wheel (zoneWheels, name, std::move (valueText), reset, [this, field] (float v) {
            editZones ([&] (dw::Zone& z) { field (z) = v; });
        });
        zoneWheelPart.push_back (part);
        zoneWheelField.push_back (field);
        return w;
    }

    Wheel& programWheel (const juce::String& name, std::function<juce::String (float)> valueText, float reset,
                         std::function<float& (dw::Program&)> field)
    {
        auto& w = wheel (programWheels, name, std::move (valueText), reset, [this, field] (float v) {
            editProgram ([&] (dw::Program& p) { field (p) = v; });
        });
        programWheelField.push_back (field);
        return w;
    }

    void button (juce::TextButton& b, const juce::String& caption, const juce::String& hint, std::function<void()> click)
    {
        b.setButtonText (caption);
        ui::setHint (b, hint);
        b.onClick = std::move (click);
        b.setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    void menuButton (juce::TextButton& b, juce::Label& label, const juce::String& hint, std::function<void()> click, const juce::String& name = {})
    {
        if (name.isNotEmpty())
            menuNames.push_back ({ &label, &b, name });
        b.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe"));
        ui::setHint (b, hint);
        b.onClick = std::move (click);
        b.setWantsKeyboardFocus (false);
        addChildComponent (b);
        label.setFont (theme::font (theme::type::caption));
        label.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        label.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (label);
    }

    // zone page parts (FL's sub panels): 0 main, 1 time, 2 trigger, 3 filter 1, 4 filter 2, 5 amp, 6 env 1, 7 env 2,
    // 8 lfo 1, 9 lfo 2, 10…13 matrix pages, 14 the effect row (always shown)
    void buildZonePage()
    {
        auto tabs = [this] (Tabs& t, juce::StringArray names, const juce::String& hint) {
            t.names = std::move (names);
            t.onChange = [this] (int) { refresh(); };
            ui::setHint (t, hint);
            addChildComponent (t);
        };
        tabs (mainTabs, { tr ("MAIN"), tr ("TIME"), tr ("TRIGGER") }, tr ("Display mode"));
        tabs (filterTabs, { tr ("FILTER 1"), tr ("FILTER 2") }, tr ("Select filter page"));
        tabs (envTabs, { tr ("AMP"), tr ("ENV 1"), tr ("ENV 2") }, tr ("Select env page"));
        tabs (lfoTabs, { tr ("LFO 1"), tr ("LFO 2") }, tr ("Select LFO page"));
        tabs (matrixTabs, { "1", "2", "3", "4" }, tr ("Select mod matrix page"));

        zoneWheel (0, tr ("Pitch tune"), cents, 0.5f, [] (dw::Zone& z) -> float& { return z.pitch; });
        zoneWheel (0, tr ("Gain"), decibels, 1.0f, [] (dw::Zone& z) -> float& { return z.gain; });
        zoneWheel (0, tr ("Panning"), panText, 0.5f, [] (dw::Zone& z) -> float& { return z.pan; });
        zoneWheel (0, tr ("Post gain"), decibels, 1.0f, [] (dw::Zone& z) -> float& { return z.sends[3]; });
        zoneWheel (1, tr ("Time"), [this] (float v) {
            return current().stretchSync != 0 ? juce::String::formatted ("%d Ticks", 1 - (int) (v * -127.0f)) : fmt ("%.1f%%", std::pow (2.0, (double) ((v - 0.5f) * 8.0f)) * 100.0);
        }, 0.5f, [] (dw::Zone& z) -> float& { return z.stretch[0]; });
        zoneWheel (1, tr ("Grain"), percent1, 0.5f, [] (dw::Zone& z) -> float& { return z.stretch[1]; });
        zoneWheel (1, tr ("Smooth"), percent1, 1.0f, [] (dw::Zone& z) -> float& { return z.stretch[2]; });
        zoneWheel (2, tr ("Frequency"), percent1, 1.0f, [this] (dw::Zone& z) -> float& { return groupOf (z).probability; });
        zoneWheel (2, tr ("Overlap"), percent1, 0.0f, [this] (dw::Zone& z) -> float& { return groupOf (z).velTolerance; });
        for (int k = 0; k < 2; ++k)
        {
            zoneWheel (3 + k, tr ("Cutoff"), hertz, 5.0f / 10.583333f, [k] (dw::Zone& z) -> float& { return z.filter[k].cutoff; });
            zoneWheel (3 + k, tr ("Resonance"), percent2, 0.5f, [k] (dw::Zone& z) -> float& { return z.filter[k].res; });
            zoneWheel (3 + k, tr ("Shape"), percent2, 0.0f, [k] (dw::Zone& z) -> float& { return z.filter[k].shape; });
        }
        for (int k = 0; k < 2; ++k)
        {
            zoneWheel (8 + k, tr ("Rate"), [this, k] (float v) { return lfoRate (v, current().lfo[k].sync != 0); }, 0.1f, [k] (dw::Zone& z) -> float& { return z.lfo[k].rate; });
            zoneWheel (8 + k, tr ("Phase"), phaseText, 1.0f, [k] (dw::Zone& z) -> float& { return z.lfo[k].phase; });
            zoneWheel (8 + k, tr ("Attack"), seconds, 0.0f, [k] (dw::Zone& z) -> float& { return z.lfo[k].fade; });
        }
        for (int s = 0; s < 16; ++s)
            zoneWheel (10 + s / 4, tr ("Amount"), amountText, 0.5f, [s] (dw::Zone& z) -> float& { return z.mods[(std::size_t) s].amount; });
        // the effect row
        zoneWheel (14, tr ("Zone ring modulation rate"), hertz, 0.0f, [] (dw::Zone& z) -> float& { return z.fx[0][0]; });
        zoneWheel (14, tr ("Zone ring modulation mix"), percent1, 1.0f, [] (dw::Zone& z) -> float& { return z.fx[0][1]; });
        zoneWheel (14, tr ("Zone decimator step"), plain3, 0.0f, [] (dw::Zone& z) -> float& { return z.fx[1][0]; });
        zoneWheel (14, tr ("Zone decimator mix"), percent1, 1.0f, [] (dw::Zone& z) -> float& { return z.fx[1][1]; });
        zoneWheel (14, tr ("Zone quantizer bits"), plain3, 0.0f, [] (dw::Zone& z) -> float& { return z.fx[2][0]; });
        zoneWheel (14, tr ("Zone quantizer mix"), percent1, 1.0f, [] (dw::Zone& z) -> float& { return z.fx[2][1]; });
        zoneWheel (14, tr ("Zone phaser frequency"), hertz, 0.5f, [] (dw::Zone& z) -> float& { return z.fx[3][0]; });
        zoneWheel (14, tr ("Zone phaser mix"), percent1, 1.0f, [] (dw::Zone& z) -> float& { return z.fx[3][1]; });
        zoneWheel (14, tr ("Zone send delay"), decibels, 0.5f, [] (dw::Zone& z) -> float& { return z.sends[0]; });
        zoneWheel (14, tr ("Zone send chorus"), decibels, 0.5f, [] (dw::Zone& z) -> float& { return z.sends[1]; });
        zoneWheel (14, tr ("Zone send reverb"), decibels, 0.5f, [] (dw::Zone& z) -> float& { return z.sends[2]; });
        for (std::size_t i = zoneWheels.size() - 11; i < zoneWheels.size(); ++i)
            zoneWheels[i]->label.name = {}; // (named under the row)

        // the envelope sliders (TVectorSlider): A D S R of the amp envelope and the two mod envelopes
        for (int e = 0; e < 3; ++e)
            for (int i = 0; i < 4; ++i)
            {
                auto& s = envSliders[(std::size_t) (e * 4 + i)];
                s.setSliderStyle (juce::Slider::LinearVertical);
                s.setRange (0.0, 1.0, 0.0);
                s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s.setPopupDisplayEnabled (true, true, this);
                s.textFromValueFunction = [i] (double v) { return i == 2 ? percent2 ((float) v) : seconds ((float) v); };
                static const char* const names[] = { TR_NOOP ("Attack"), TR_NOOP ("Decay"), TR_NOOP ("Sustain"), TR_NOOP ("Release") };
                ui::setHint (s, tr (names[i]));
                s.onValueChange = [this, e, i, &s] {
                    if (loading)
                        return;
                    const auto v = (float) s.getValue();
                    editZones ([e, i, v] (dw::Zone& z) { (e == 0 ? z.amp[i] : z.modEnv[e - 1][i]) = v; });
                };
                addChildComponent (s);
            }

        // the menus' buttons and their labels
        menuButton (triggerGroup, triggerGroupLabel, tr ("Zone trigger group"), [this] { numberMenu (0, 99, current().altGroup, [this] (int v) {
                        editZones ([v] (dw::Zone& z) { z.altGroup = v; });
                    }); }, tr ("Group"));
        menuButton (triggerType, triggerTypeLabel, tr ("Zone trigger type"), [this] { listMenu (triggerTypes, groupOf (current()).mode, [this] (int v) {
                        editProgram ([this, v] (dw::Program& p) { p.groups[(std::size_t) std::clamp (current().altGroup, 0, 99)].mode = v; });
                    }); }, tr ("Type"));
        for (int k = 0; k < 2; ++k)
        {
            menuButton (filterType[k], filterTypeLabel[k], tr ("Filter type selector"), [this, k] { listMenu (filterTypes, current().filter[k].type, [this, k] (int v) {
                            editZones ([k, v] (dw::Zone& z) { z.filter[k].type = (std::uint8_t) v; });
                        }); }, tr ("Type"));
            menuButton (lfoWave[k], lfoWaveLabel[k], tr ("Zone mod LFO selector"), [this, k] { listMenu (waveforms, current().lfo[k].type, [this, k] (int v) {
                            editZones ([k, v] (dw::Zone& z) { z.lfo[k].type = (std::uint8_t) v; });
                        }); }, tr ("Waveform"));
            ui::setHint (lfoSync[k], tr ("Zone mod LFO tempo sync"));
            lfoSync[k].onChange = [this, k] (bool on) { editZones ([k, on] (dw::Zone& z) { z.lfo[k].sync = on ? 1 : 0; }); };
            addChildComponent (lfoSync[k]);
        }
        for (int s = 0; s < 16; ++s)
        {
            menuButton (modSrc[s], modSrcLabel[s], tr ("Select mod slot source {0}", { juce::String (s + 1) }), [this, s] {
                listMenu (modSources, current().mods[(std::size_t) s].src, [this, s] (int v) {
                    editZones ([s, v] (dw::Zone& z) { z.mods[(std::size_t) s].src = (std::uint16_t) v; });
                });
            });
            menuButton (modDst[s], modDstLabel[s], tr ("Select mod slot destination P{0}:{1}", { juce::String (s / 4 + 1), juce::String (s % 4 + 1) }), [this, s] {
                listMenu (modDestinations, current().mods[(std::size_t) s].dst, [this, s] (int v) {
                    editZones ([s, v] (dw::Zone& z) { z.mods[(std::size_t) s].dst = (std::uint16_t) v; });
                });
            });
        }
        ui::setHint (stretchMode, tr ("Time stretching mode"));
        stretchMode.onChange = [this] (bool on) { editZones ([on] (dw::Zone& z) { z.stretchMode = on ? 1 : 0; }); };
        addChildComponent (stretchMode);
        ui::setHint (stretchSync, tr ("Time stretching sync"));
        stretchSync.onChange = [this] (bool on) { editZones ([on] (dw::Zone& z) { z.stretchSync = on ? 1 : 0; }); };
        addChildComponent (stretchSync);
        for (int k = 0; k < 4; ++k)
        {
            static const char* const hints[] = { TR_NOOP ("Switch ring modulator effect"), TR_NOOP ("Switch decimator effect"), TR_NOOP ("Switch bit reducer effect"), TR_NOOP ("Switch phase effect") };
            ui::setHint (fxSwitch[k], tr (hints[k]));
            fxSwitch[k].onChange = [this, k] (bool on) { editZones ([k, on] (dw::Zone& z) { z.fxOn[k] = on ? 1 : 0; }); };
            addChildComponent (fxSwitch[k]);
        }
        serial.onClick = [this] { editZones ([] (dw::Zone& z) { z.parallelFilters = false; }); };
        parallel.onClick = [this] { editZones ([] (dw::Zone& z) { z.parallelFilters = true; }); };
        ui::setHint (serial, tr ("Filter routing: serial"));
        ui::setHint (parallel, tr ("Filter routing: parallel"));
        addChildComponent (serial);
        addChildComponent (parallel);
        editSelection.onClick = [this] { editGlobal = false; refresh(); };
        editAll.onClick = [this] { editGlobal = true; refresh(); };
        ui::setHint (editSelection, tr ("Switch zone edit mode"));
        ui::setHint (editAll, tr ("Switch zone edit mode"));
        addChildComponent (editSelection);
        addChildComponent (editAll);
    }

    void buildSamplePage()
    {
        sampleView.sample = [this] { return currentSample(); };
        sampleView.zone = [this]() -> const dw::Zone* { return prog.zones.empty() ? nullptr : &prog.zones[(std::size_t) currentZone]; };
        sampleView.onClick = [this] { previewCurrent(); };
        ui::setHint (sampleView, tr ("Left click to preview"));
        addChildComponent (sampleView);
        auto frameWheel = [this] (const juce::String& name, std::function<int& (dw::Zone&)> field) {
            auto& w = wheel (sampleWheels, name, nullptr, 0.0f, [this, field] (float v) {
                editZones ([&] (dw::Zone& z) { field (z) = (int) std::llround ((double) v * (double) std::max<std::int64_t> (0, z.frames)); });
            });
            sampleWheelField.push_back (field);
            return &w;
        };
        frameWheel (tr ("Start"), [] (dw::Zone& z) -> int& { return z.offset; });
        frameWheel (tr ("Loop start"), [] (dw::Zone& z) -> int& { return z.loopA; });
        frameWheel (tr ("Loop end"), [] (dw::Zone& z) -> int& { return z.loopB; });
        for (auto& w : sampleWheels)
            w->valueText = [this] (float v) { return juce::String ((std::int64_t) std::llround ((double) v * (double) std::max<std::int64_t> (0, current().frames))); };
        menuButton (loopType, loopTypeLabel, tr ("Loop type"), [this] { listMenu (loopModes, current().loopMode, [this] (int v) {
                        editZones ([v] (dw::Zone& z) { z.loopMode = v; });
                    }); }, tr ("Loop type"));
        sampleName.setFont (theme::font (theme::type::small));
        sampleName.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        sampleName.setJustificationType (juce::Justification::centredLeft);
        sampleName.setColour (juce::Label::backgroundColourId, theme::colour::bg0);
        addChildComponent (sampleName);
    }

    void buildProgramPage()
    {
        programWheel (tr ("Volume"), decibels, 1.0f, [] (dw::Program& p) -> float& { return p.gain; });
        programWheel (tr ("Time"), seconds, 0.0f, [] (dw::Program& p) -> float& { return p.glide; });
        programWheel (tr ("Amount"), percent2, 0.0f, [] (dw::Program& p) -> float& { return p.shape1Amount; });
        programWheel (tr ("Amount"), percent2, 0.0f, [] (dw::Program& p) -> float& { return p.shape2Amount; });
        for (int k = 0; k < 2; ++k)
        {
            programWheel (tr ("Rate"), [this, k] (float v) { return lfoRate (v, prog.lfo[k].sync != 0); }, 0.1f, [k] (dw::Program& p) -> float& { return p.lfo[k].rate; });
            programWheel (tr ("Phase"), phaseText, 1.0f, [k] (dw::Program& p) -> float& { return p.lfo[k].phase; });
            programWheel (tr ("Attack"), seconds, 0.0f, [k] (dw::Program& p) -> float& { return p.lfo[k].fade; });
        }
        static const char* const delayNames[] = { TR_NOOP ("Delay"), TR_NOOP ("Feedback"), TR_NOOP ("Low cut"), TR_NOOP ("High cut") };
        static const char* const reverbNames[] = { TR_NOOP ("Room"), TR_NOOP ("Damp"), TR_NOOP ("Diffusion"), TR_NOOP ("Decay") };
        static const char* const chorusNames[] = { TR_NOOP ("Delay"), TR_NOOP ("Depth"), TR_NOOP ("Rate"), TR_NOOP ("Feedback") };
        const std::function<juce::String (float)> delayText[] = { [] (float v) { return fmt ("%.2f", (double) v); }, percent2, hertz, hertz };
        const std::function<juce::String (float)> reverbText[] = { percent2, [] (float v) { return fmt ("%.2f kHz", (double) (v * 21.5f + 0.5f)); }, percent2,
                                                                   [] (float v) { return fmt ("%.2f sec", (double) (v * v * 19.0f + 0.1f)); } };
        const std::function<juce::String (float)> chorusText[] = { [] (float v) { return fmt ("%.2f ms", (double) (v * v * 47.9f + 0.1f)); },
                                                                   [] (float v) { return fmt ("%.2f ms", (double) (v * v * 24.0f)); },
                                                                   [] (float v) { return fmt ("%.2f Hz", (double) (v * v * 20.0f)); }, percent2 };
        for (int i = 0; i < 4; ++i)
            programWheel (tr (delayNames[i]), delayText[i], 0.5f, [i] (dw::Program& p) -> float& { return p.delay[i]; });
        for (int i = 0; i < 4; ++i)
            programWheel (tr (reverbNames[i]), reverbText[i], 0.5f, [i] (dw::Program& p) -> float& { return p.reverb[i]; });
        for (int i = 0; i < 4; ++i)
            programWheel (tr (chorusNames[i]), chorusText[i], 0.5f, [i] (dw::Program& p) -> float& { return p.chorus[i]; });
        menuButton (playMode, playModeLabel, tr ("Select play mode"), [this] { listMenu (playModes, prog.voiceMode, [this] (int v) {
                        editProgram ([v] (dw::Program& p) { p.voiceMode = v; });
                    }); }, tr ("Play mode"));
        for (int k = 0; k < 2; ++k)
        {
            menuButton (progLfoWave[k], progLfoWaveLabel[k], tr ("Select mod LFO {0} waveform", { juce::String (k + 1) }), [this, k] {
                listMenu (waveforms, prog.lfo[k].type, [this, k] (int v) { editProgram ([k, v] (dw::Program& p) { p.lfo[k].type = (std::uint8_t) v; }); });
            }, tr ("Waveform"));
            ui::setHint (progLfoSync[k], tr ("Master LFO {0} rate switch", { juce::String (k + 1) }));
            progLfoSync[k].onChange = [this, k] (bool on) { editProgram ([k, on] (dw::Program& p) { p.lfo[k].sync = on ? 1 : 0; }); };
            addChildComponent (progLfoSync[k]);
        }
        auto fxLed = [this] (Led& l, const juce::String& hint, std::function<void (dw::Program&, bool)> set) {
            ui::setHint (l, hint);
            l.onChange = [this, set] (bool on) { editProgram ([&] (dw::Program& p) { set (p, on); }); };
            addChildComponent (l);
        };
        fxLed (driveA, tr ("Distortion A effect switch"), [] (dw::Program& p, bool on) { p.shape1 = on; });
        fxLed (driveB, tr ("Distortion B effect switch"), [] (dw::Program& p, bool on) { p.shape2 = on; });
        fxLed (delayOn, tr ("Delay effect switch"), [] (dw::Program& p, bool on) { p.delayOn = on; });
        fxLed (reverbOn, tr ("Reverb effect switch"), [] (dw::Program& p, bool on) { p.reverbOn = on; });
        fxLed (chorusOn, tr ("Chorus effect switch"), [] (dw::Program& p, bool on) { p.chorusOn = on; });
        // the delay type pair (bounce = the cross-feedback flag) and the glide mode pair (Auto = glide only between
        // overlapping notes; editor_x64.dll 0x858070 sends the Auto box's state)
        delayBounce.onClick = [this] { editProgram ([] (dw::Program& p) { p.delayCross = true; }); };
        delayNormal.onClick = [this] { editProgram ([] (dw::Program& p) { p.delayCross = false; }); };
        glideFixed.onClick = [this] { editProgram ([] (dw::Program& p) { p.glideLegatoOnly = false; }); };
        glideAuto.onClick = [this] { editProgram ([] (dw::Program& p) { p.glideLegatoOnly = true; }); };
        for (auto* c : { &delayBounce, &delayNormal, &glideFixed, &glideAuto })
            addChildComponent (*c);
        ui::setHint (delayBounce, tr ("Delay effect type"));
        ui::setHint (delayNormal, tr ("Delay effect type"));
        ui::setHint (glideFixed, tr ("Glide mode"));
        ui::setHint (glideAuto, tr ("Glide mode"));
    }

    void buildSide()
    {
        sideTabs.names = { tr ("FOLDERS"), tr ("OPTIONS") };
        sideTabs.onChange = [this] (int) { refresh(); };
        addAndMakeVisible (sideTabs);
        button (browseHome, juce::String::fromUTF8 ("\xe2\x8c\x82"), tr ("Home"), [this] { browseTo (homeFolder()); });
        button (browseParent, tr ("PARENT"), tr ("Parent folder"), [this] { browseTo (folder.getParentDirectory()); });
        button (browseFolder, tr ("BROWSE"), tr ("Browse for folder"), [this] { chooseFolder(); });
        button (browseRefresh, juce::String::fromUTF8 ("\xe2\x9f\xb3"), tr ("Reread structure"), [this] { browseTo (folder); });
        button (browsePreview, tr ("PREVIEW"), tr ("Browser preview"), [this] {
            previewOn = ! previewOn;
            browsePreview.setToggleState (previewOn, juce::dontSendNotification);
        });
        browsePreview.setClickingTogglesState (false);
        browsePreview.setToggleState (previewOn, juce::dontSendNotification);
        browsePath.setFont (theme::font (theme::type::caption));
        browsePath.setColour (juce::Label::backgroundColourId, theme::colour::bg0);
        browsePath.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        ui::setHint (browsePath, tr ("Current browser path"));
        addAndMakeVisible (browsePath);
        files.owner = this;
        ui::setHint (files, tr ("Double click a sample to add it as a zone; right click for Add / Replace sample(s)"));
        addAndMakeVisible (files);
        browseInfo.setFont (theme::font (theme::type::caption));
        browseInfo.setColour (juce::Label::backgroundColourId, theme::colour::bg0);
        browseInfo.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (browseInfo);
        // the options that the program plays (bank flags; the plugin's own settings file holds the others)
        clipVolume.onClick = [this] { setFlag (1, ! clipVolume.on); };
        compatibleVelocity.onClick = [this] { setFlag (2, ! compatibleVelocity.on); };
        ui::setHint (clipVolume, tr ("Try this with old banks/songs that have 'volume' problems"));
        ui::setHint (compatibleVelocity, tr ("Backward compatibility for velocity handling"));
        addChildComponent (clipVolume);
        addChildComponent (compatibleVelocity);
        browseTo (homeFolder());
    }

    // ============================================================================================ layout
    void place (Wheel& w, int x, int y, int lx, int ly, int lw = 63)
    {
        w.knob.setBounds (fl (x, y, 30, 30));
        // FL's box: from the label's x to the wheel, the name line above the value line (the form's label rect)
        w.label.setBounds (lw <= 0 ? juce::Rectangle<int>() : fl (lx - 4, ly - 17, std::max (lw, x - lx + 1), 30));
    }

    void layoutZonePage()
    {
        const int ox = 30, oy = 396; // the page's corner on the form
        auto at = [&] (int x, int y, int w, int h) { return fl (ox + x, oy + y, w, h); };
        mainTabs.setBounds (at (4, 5, 124, 18));
        filterTabs.setBounds (at (130, 5, 124, 18));
        envTabs.setBounds (at (256, 5, 154, 18));
        lfoTabs.setBounds (at (412, 5, 124, 18));
        matrixTabs.setBounds (at (598, 5, 64, 18));
        // wheels by part
        std::array<int, 16> nth {};
        for (std::size_t i = 0; i < zoneWheels.size(); ++i)
        {
            const auto part = zoneWheelPart[i];
            auto& w = *zoneWheels[i];
            const auto k = nth[(std::size_t) part]++;
            if (part <= 2) // main / time / trigger: panel (5, 23); trigger's wheels are its rows 3 and 4
                place (w, ox + 5 + 88, oy + 23 + 4 + 34 * (part == 2 ? k + 2 : k), ox + 5 + 9, oy + 23 + 21 + 34 * (part == 2 ? k + 2 : k));
            else if (part <= 4) // filters (131, 23)
                place (w, ox + 131 + 88, oy + 23 + 4 + 34 * k, ox + 131 + 9, oy + 23 + 21 + 34 * k);
            else if (part <= 9) // LFOs (413, 23)
                place (w, ox + 413 + 88, oy + 23 + 4 + 34 * k, ox + 413 + 9, oy + 23 + 21 + 34 * k);
            else if (part <= 13) // matrix (539, 23): the amount of each slot
                place (w, ox + 539 + 88, oy + 23 + 4 + 34 * k, ox + 539 + 9, oy + 23 + 21 + 34 * k, 0);
            else // the effect row
            {
                static const int xs[] = { 14, 51, 105, 142, 200, 236, 287, 325, 375, 414, 454 };
                w.knob.setBounds (at (xs[k], k >= 8 ? 180 : 181, 30, 30));
                w.label.setBounds ({});
            }
        }
        for (int e = 0; e < 3; ++e)
            for (int i = 0; i < 4; ++i)
            {
                static const int xs[] = { 14, 47, 80, 114 };
                envSliders[(std::size_t) (e * 4 + i)].setBounds (at (257 + xs[i], 23 + 15, 20, 120));
            }
        triggerGroup.setBounds (at (5 + 100, 23 + 19, 21, 15));
        triggerGroupLabel.setBounds (at (5 + 9, 23 + 19, 88, 15));
        triggerType.setBounds (at (5 + 100, 23 + 53, 21, 15));
        triggerTypeLabel.setBounds (at (5 + 9, 23 + 53, 88, 15));
        for (int k = 0; k < 2; ++k)
        {
            filterType[k].setBounds (at (131 + 68, 23 + 121, 21, 15));
            filterTypeLabel[k].setBounds (at (131 + 9, 23 + 121, 58, 15));
            lfoWave[k].setBounds (at (413 + 98, 23 + 121, 21, 15));
            lfoWaveLabel[k].setBounds (at (413 + 9, 23 + 121, 86, 15));
            lfoSync[k].setBounds (at (413 + 68, 23 + 3, 17, 16));
        }
        for (int s = 0; s < 16; ++s)
        {
            const auto row = s % 4;
            modSrc[s].setBounds (at (539 + 72, 23 + 2 + 34 * row, 16, 16));
            modDst[s].setBounds (at (539 + 72, 23 + 18 + 34 * row, 16, 16));
            modSrcLabel[s].setBounds (at (539 + 2, 23 + 2 + 34 * row, 70, 15));
            modDstLabel[s].setBounds (at (539 + 2, 23 + 18 + 34 * row, 70, 15));
        }
        stretchMode.setBounds (at (5 + 28, 23 + 5, 16, 14));
        stretchSync.setBounds (at (5 + 62, 23 + 5, 16, 14));
        static const int sw[] = { 67, 157, 251, 341 };
        for (int k = 0; k < 4; ++k)
            fxSwitch[k].setBounds (at (sw[k], 168, 19, 14));
        serial.setBounds (at (131 + 88, 23 + 107, 13, 32));
        parallel.setBounds (at (131 + 101, 23 + 107, 13, 32));
        editSelection.setBounds (at (513, 187, 110, 14));
        editAll.setBounds (at (513, 202, 110, 14));
    }

    void layoutSamplePage()
    {
        const int ox = 30, oy = 396;
        sampleView.setBounds (fl (ox + 6, oy + 5, 652, 140));
        static const int xs[] = { 90, 217, 332 }, lxs[] = { 12, 134, 252 };
        for (std::size_t i = 0; i < sampleWheels.size(); ++i)
            place (*sampleWheels[i], ox + xs[i], oy + 187, ox + lxs[i], oy + 203, 75);
        loopType.setBounds (fl (ox + 481, oy + 203, 21, 15));
        loopTypeLabel.setBounds (fl (ox + 373, oy + 202, 105, 15));
        sampleName.setBounds (fl (ox + 6, oy + 148, 652, 15));
    }

    void layoutProgramPage()
    {
        const int ox = 30, oy = 396;
        // master volume, glide, drive A / B; then LFO 1, LFO 2 (rate, phase, attack), delay, reverb, chorus (4 each)
        place (*programWheels[0], ox + 181, oy + 24, ox + 120, oy + 41, 58);
        place (*programWheels[1], ox + 320, oy + 24, ox + 230, oy + 41, 85);
        place (*programWheels[2], ox + 503, oy + 24, ox + 426, oy + 41);
        place (*programWheels[3], ox + 629, oy + 24, ox + 550, oy + 41);
        for (int k = 0; k < 2; ++k)
            for (int i = 0; i < 3; ++i)
                place (*programWheels[(std::size_t) (4 + k * 3 + i)], ox + (k == 0 ? 94 : 220), oy + 82 + 34 * i, ox + (k == 0 ? 14 : 140), oy + 99 + 34 * i);
        for (int i = 0; i < 4; ++i)
        {
            place (*programWheels[(std::size_t) (10 + i)], ox + 348, oy + 82 + 34 * i, ox + 268, oy + 99 + 34 * i);
            place (*programWheels[(std::size_t) (14 + i)], ox + 503, oy + 82 + 34 * i, ox + 426, oy + 99 + 34 * i);
            place (*programWheels[(std::size_t) (18 + i)], ox + 629, oy + 82 + 34 * i, ox + 550, oy + 99 + 34 * i);
        }
        playMode.setBounds (fl (ox + 99, oy + 38, 21, 15));
        playModeLabel.setBounds (fl (ox + 14, oy + 38, 82, 15));
        for (int k = 0; k < 2; ++k)
        {
            progLfoWave[k].setBounds (fl (ox + (k == 0 ? 99 : 227), oy + 198, 21, 15));
            progLfoWaveLabel[k].setBounds (fl (ox + (k == 0 ? 14 : 140), oy + 198, 82, 15));
            progLfoSync[k].setBounds (fl (ox + (k == 0 ? 74 : 201), oy + 82, 17, 14));
        }
        driveA.setBounds (fl (ox + 511, oy + 5, 19, 14));
        driveB.setBounds (fl (ox + 637, oy + 5, 19, 14));
        delayOn.setBounds (fl (ox + 385, oy + 63, 19, 14));
        reverbOn.setBounds (fl (ox + 511, oy + 63, 19, 14));
        chorusOn.setBounds (fl (ox + 637, oy + 63, 19, 14));
        delayBounce.setBounds (fl (ox + 386, oy + 96, 19, 51));
        delayNormal.setBounds (fl (ox + 386, oy + 146, 19, 51));
        glideFixed.setBounds (fl (ox + 355, oy + 26, 49, 14));
        glideAuto.setBounds (fl (ox + 355, oy + 41, 48, 14));
    }

    void layoutSide()
    {
        const int ox = 741, oy = 62;
        browsePath.setBounds (fl (ox + 5, oy + 5, 232, 17));
        browseHome.setBounds (fl (ox + 4, oy + 27, 24, 18));
        browseParent.setBounds (fl (ox + 30, oy + 27, 44, 18));
        browseFolder.setBounds (fl (ox + 77, oy + 27, 48, 18));
        browseRefresh.setBounds (fl (ox + 127, oy + 27, 24, 18));
        browsePreview.setBounds (fl (ox + 185, oy + 27, 52, 18));
        files.setBounds (fl (ox + 5, oy + 51, 232, 474));
        browseInfo.setBounds (fl (ox + 5, oy + 532, 232, 22));
        clipVolume.setBounds (fl (ox + 5 + 3, oy + 477 + 37, 200, 21));
        compatibleVelocity.setBounds (fl (ox + 5 + 3, oy + 477 + 20, 200, 21));
    }

    // ============================================================================================ state
    const ChannelDef* def() const { return locate ? locate (const_cast<Song&> (engine.getSong())) : nullptr; }

    void reload()
    {
        const auto* d = def();
        if (d == nullptr)
            return;
        lastState = d->pluginState;
        lastZones = d->zones;
        lastParams = d->modelParams;
        auto parsed = dw::readDoc (d->pluginState);
        doc = parsed ? std::move (*parsed) : dw::freshDoc();
        // the files LYNX plays: the played program's zones are the channel's zones, in order
        programFiles.assign ((std::size_t) dw::programCount (doc), {});
        const auto state = dw::parseState (dw::writeDoc (doc));
        played = -1;
        if (state)
            if (const auto* p = dw::playedProgram (*state))
                played = p->index;
        for (int i = 0; i < dw::programCount (doc); ++i)
        {
            const auto p = dw::program (doc, i);
            auto& f = programFiles[(std::size_t) i];
            for (std::size_t z = 0; z < p.zones.size(); ++z)
                f.push_back (p.index == played && z < d->zones.size() ? d->zones[z].path : resolve (p.zones[z].path));
        }
        if (shown < 0)
            shown = std::max (0, dw::slotProgram (doc, 0));
        loadShown();
    }

    std::string resolve (const std::string& flPath) const
    {
        if (flPath.empty())
            return {};
        static const auto roots = findFlStudioRoots();
        auto path = resolveFlPath (flPath, roots);
        path = existingPathIgnoringCase (path);
        return juce::File::isAbsolutePath (juce::String::fromUTF8 (path.c_str())) && juce::File (juce::String::fromUTF8 (path.c_str())).existsAsFile()
                   ? path
                   : std::string();
    }

    void loadShown()
    {
        shown = std::clamp (shown, 0, std::max (0, dw::programCount (doc) - 1));
        prog = dw::program (doc, shown);
        if ((int) programFiles.size() <= shown)
            programFiles.resize ((std::size_t) shown + 1);
        programFiles[(std::size_t) shown].resize (prog.zones.size());
        selected.assign (prog.zones.size(), false);
        currentZone = std::clamp (currentZone, 0, std::max (0, (int) prog.zones.size() - 1));
        if (! prog.zones.empty())
            selected[(std::size_t) currentZone] = true;
        refresh();
    }

    // the edited program back into the document and the channel: the state, the played program's files and formats
    void commit()
    {
        dw::setProgram (doc, shown, prog);
        prog = dw::program (doc, shown); // the zones now come from their new records
        const auto bytes = dw::writeDoc (doc);
        const auto state = dw::parseState (bytes);
        played = -1;
        std::vector<SampleZone> zones;
        std::vector<bool> int16;
        if (state)
            if (const auto* p = dw::playedProgram (*state))
            {
                played = p->index;
                const auto& f = programFiles[(std::size_t) std::clamp (played, 0, (int) programFiles.size() - 1)];
                for (std::size_t z = 0; z < p->zones.size(); ++z)
                {
                    const auto& pz = p->zones[z];
                    SampleZone sz;
                    sz.path = z < f.size() ? f[z] : std::string();
                    sz.rootKey = std::clamp (pz.root, 0, 127);
                    sz.lowKey = std::clamp (pz.lowKey, 0, 127);
                    sz.highKey = std::clamp (pz.highKey, sz.lowKey, 127);
                    sz.lowVelocity = std::clamp (pz.lowVel, 0, 127);
                    sz.highVelocity = std::clamp (pz.highVel, sz.lowVelocity, 127);
                    int16.push_back (sixteenBit (sz.path));
                    zones.push_back (std::move (sz));
                }
            }
        const auto params = dw::encodeFormats (int16);
        lastState = bytes;
        lastZones = zones;
        lastParams = params;
        engine.editSong ([l = locate, bytes, zones, params] (Song& song) {
            if (auto* d = l ? l (song) : nullptr)
            {
                d->pluginState = bytes;
                d->zones = zones;
                d->modelParams = params;
            }
        });
        refresh();
    }

    bool sixteenBit (const std::string& path) const
    {
        if (path.empty())
            return false;
        juce::MemoryBlock head;
        juce::File (juce::String::fromUTF8 (path.c_str())).loadFileAsData (head);
        const auto* b = (const std::uint8_t*) head.getData();
        return dw::isPcm16Wav (std::vector<std::uint8_t> (b, b + head.getSize()));
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto* d = def();
        if (d != nullptr && (d->pluginState != lastState || ! (d->zones == lastZones) || d->modelParams != lastParams))
            reload();
    }

    // ============================================================================================ editing
    dw::Zone& current()
    {
        static dw::Zone none;
        if (prog.zones.empty())
        {
            none = dw::Zone {};
            return none;
        }
        return prog.zones[(std::size_t) std::clamp (currentZone, 0, (int) prog.zones.size() - 1)];
    }

    dw::Program::AltGroup& groupOf (const dw::Zone& z) { return prog.groups[(std::size_t) std::clamp (z.altGroup, 0, 99)]; }

    // a change to the zones being edited: the selected ones ("Current selection"), or every zone ("Global")
    void editZones (const std::function<void (dw::Zone&)>& change)
    {
        if (prog.zones.empty())
            return;
        for (std::size_t i = 0; i < prog.zones.size(); ++i)
            if (editGlobal || (i < selected.size() && selected[i]) || (int) i == currentZone)
                change (prog.zones[i]);
        commit();
    }

    void editProgram (const std::function<void (dw::Program&)>& change)
    {
        change (prog);
        commit();
    }

    void setFlag (int bit, bool on)
    {
        for (auto& n : doc.top)
            if (n.id == 6 && ! n.container && ! n.body.empty())
                n.body[0] = (std::uint8_t) (on ? (n.body[0] | bit) : (n.body[0] & ~bit));
        commit();
    }

    std::uint8_t flags() const
    {
        for (const auto& n : doc.top)
            if (n.id == 6 && ! n.container && ! n.body.empty())
                return n.body[0];
        return 0;
    }

    void showProgram (int index)
    {
        const auto count = dw::programCount (doc);
        if (count <= 0)
            return;
        shown = ((index % count) + count) % count;
        // FL's program arrows set the current MIDI channel's program (here MIDI channel 1, the one LYNX plays)
        dw::setSlotProgram (doc, 0, shown);
        currentZone = 0;
        loadShown();
        commit();
    }

    void selectZone (int i, bool add, bool range)
    {
        if (i < 0 || i >= (int) prog.zones.size())
            return;
        if (range)
        {
            const auto from = std::min (currentZone, i), to = std::max (currentZone, i);
            for (int k = from; k <= to; ++k)
                selected[(std::size_t) k] = true;
        }
        else if (add)
            selected[(std::size_t) i] = ! selected[(std::size_t) i];
        else
            std::fill (selected.begin(), selected.end(), false), selected[(std::size_t) i] = true;
        currentZone = i;
        refresh();
    }

    std::vector<bool> hasSamples() const
    {
        std::vector<bool> v;
        const auto& f = programFiles[(std::size_t) shown];
        for (std::size_t z = 0; z < prog.zones.size(); ++z)
            v.push_back ((z < f.size() && ! f[z].empty()) || ! prog.zones[z].embedded.empty());
        return v;
    }

    // the zone menu's commands (their tags in FL's form: DirectWave_x64.dll 0x180018c17)
    void zoneCommand (int tag)
    {
        auto& f = programFiles[(std::size_t) shown];
        f.resize (prog.zones.size());
        switch (tag)
        {
            case 16600: // New zone: "<New Zone nnn>" at the end, the only one selected
            {
                auto z = dw::newZone ({}, {}, 0, 0, 44100.0f, 32, 60, 0, 127);
                z.beats = 16;
                z.name = juce::String::formatted ("<New Zone %.3d>", (int) prog.zones.size()).toStdString();
                prog.zones.push_back (z);
                f.emplace_back();
                selectLast();
                break;
            }
            case 16601: // Delete zone(s): the selected ones
                for (int i = (int) prog.zones.size() - 1; i >= 0; --i)
                    if (selected[(std::size_t) i])
                    {
                        prog.zones.erase (prog.zones.begin() + i);
                        f.erase (f.begin() + i);
                    }
                currentZone = std::min (currentZone, std::max (0, (int) prog.zones.size() - 1));
                break;
            case 16602: // Delete all zones
                prog.zones.clear();
                f.clear();
                currentZone = 0;
                break;
            case 16603: // Duplicate zone: a copy of the current zone (and its sample) at the end
                if (! prog.zones.empty())
                {
                    auto copy = prog.zones[(std::size_t) currentZone];
                    const auto file = f[(std::size_t) currentZone];
                    copy.origin = -1;
                    prog.zones.push_back (copy);
                    f.push_back (file);
                    selectLast();
                }
                break;
            case 16606: dw::automap (prog.zones, hasSamples()); break;
            case 16607: dw::remapAsDrumkit (prog.zones, hasSamples()); break;
            case 16608: case 16609: // Lock / Unlock all zones
                for (auto& z : prog.zones)
                    z.output = tag == 16608 ? 1 : 0;
                break;
            default: break;
        }
        selected.resize (prog.zones.size(), false);
        commit();
    }

    void selectLast()
    {
        selected.assign (prog.zones.size(), false);
        if (! prog.zones.empty())
            selected.back() = true;
        currentZone = (int) prog.zones.size() - 1;
    }

    // Add sample(s) / Replace sample(s) (DirectWave_x64.dll 0x18001d4a0, the sample load 0x18001ddb0): a new zone per file
    // at the end, or the files into the selected zones from the current one on (keeping their keys, velocities and root)
    void addSamples (const juce::StringArray& paths, bool replace)
    {
        auto& f = programFiles[(std::size_t) shown];
        f.resize (prog.zones.size());
        int into = currentZone;
        bool any = false;
        for (const auto& path : paths)
        {
            const juce::File file (path);
            const auto data = engine.getSampleLibrary().load (file.getFullPathName());
            if (data == nullptr || data->frames() == 0)
                continue;
            juce::MemoryBlock bytes;
            file.loadFileAsData (bytes);
            const std::vector<std::uint8_t> raw ((const std::uint8_t*) bytes.getData(), (const std::uint8_t*) bytes.getData() + bytes.getSize());
            const auto i16 = dw::isPcm16Wav (raw);
            auto z = dw::newZone (path.toStdString(), dw::zoneNameOf (path.toStdString()), (std::int64_t) data->frames(),
                                  data->isStereo() ? 2 : 1, (float) data->sampleRate, i16 ? 16 : 32, 60, 0, 127);
            if (const auto info = dw::readWavInfo (raw))
            {
                if (info->root >= 0) z.root = std::clamp (info->root, 0, 127);
                if (info->loopMode >= 0) { z.loopMode = info->loopMode; z.loopA = info->loopA; z.loopB = info->loopB; }
                if (info->hasInst)
                {
                    z.fine = info->fine;
                    z.lowKey = std::clamp (info->lowKey, 0, 127);
                    z.highKey = std::clamp (info->highKey, 0, 127);
                    z.lowVel = std::clamp (info->lowVel, 0, 127);
                    z.highVel = std::clamp (info->highVel, 0, 127);
                }
            }
            if (replace && ! prog.zones.empty() && into < (int) prog.zones.size())
            {
                auto& old = prog.zones[(std::size_t) into];
                z.lowKey = old.lowKey; z.highKey = old.highKey; z.lowVel = old.lowVel; z.highVel = old.highVel; z.root = old.root;
                if (paths.size() == 1)
                    z.keyTrack = old.keyTrack;
                z.origin = old.origin; // the record stays; its sample fields change
                // the zone's other settings stay as they were
                auto kept = old;
                kept.path = z.path; kept.name = z.name; kept.frames = z.frames; kept.channels = z.channels; kept.rate = z.rate;
                kept.bits = z.bits; kept.loopMode = z.loopMode; kept.loopA = z.loopA; kept.loopB = z.loopB; kept.offset = 0;
                kept.beats = z.beats; kept.keyTrack = z.keyTrack; kept.embedded.clear();
                old = kept;
                f[(std::size_t) into] = path.toStdString();
                ++into;
            }
            else
            {
                prog.zones.push_back (z);
                f.push_back (path.toStdString());
                selectLast();
            }
            // the first sample names an unnamed program ("---")
            if (prog.name.rfind ("---", 0) == 0)
                prog.name = z.name;
            any = true;
        }
        if (! any)
            return;
        selected.resize (prog.zones.size(), false);
        commit();
    }

    // ============================================================================================ files
    juce::File homeFolder() const
    {
        static const auto roots = findFlStudioRoots();
        const juce::File packs (juce::String::fromUTF8 (resolveFlPath ("%FLStudioFactoryData%\\Data\\Patches\\Packs", roots).c_str()));
        if (juce::File::isAbsolutePath (packs.getFullPathName()) && packs.isDirectory())
            return packs;
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    }

    bool saveProgramTo (const juce::File& file)
    {
        dw::setProgram (doc, shown, prog);
        auto program = dw::program (doc, shown);
        const auto bytes = dw::writeProgramFile (doc, shown);
        const auto ok = file.replaceWithData (bytes.data(), bytes.size());
        if (ok && program.file != file.getFileName().toStdString())
        {
            prog.file = file.getFileName().toStdString();
            lastSaved = file;
            commit();
        }
        return ok;
    }

    bool saveBankTo (const juce::File& file)
    {
        const auto bytes = dw::writeBankFile (doc);
        return file.replaceWithData (bytes.data(), bytes.size());
    }

    // Open program / bank...: a .dwp into the shown program, a .dwb as the whole bank, an .sfz as a program (LYNX's mapping)
    bool openFile (const juce::File& file)
    {
        if (file.hasFileExtension ("sfz"))
            return openSfz (file);
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
            return false;
        const std::vector<std::uint8_t> bytes ((const std::uint8_t*) data.getData(), (const std::uint8_t*) data.getData() + data.getSize());
        if (auto loaded = dw::readProgramFile (bytes))
        {
            loaded->file = file.getFileName().toStdString();
            std::vector<std::string> f;
            for (auto& z : loaded->zones)
            {
                auto path = resolve (z.path);
                if (path.empty()) // a sample next to the program file (FL's "Save for FL Studio Mobile" keeps them there)
                {
                    const auto name = juce::String::fromUTF8 (z.path.c_str()).fromLastOccurrenceOf ("\\", false, false).fromLastOccurrenceOf ("/", false, false);
                    for (const auto& c : { file.getSiblingFile (name), file.getParentDirectory().getChildFile (file.getFileNameWithoutExtension()).getChildFile (name) })
                        if (c.existsAsFile())
                            path = c.getFullPathName().toStdString();
                }
                f.push_back (path);
            }
            dw::loadProgram (doc, shown, *loaded);
            programFiles[(std::size_t) shown] = f;
            currentZone = 0;
            lastSaved = file;
            loadShown();
            commit();
            return true;
        }
        if (auto bank = dw::readBankFile (bytes))
        {
            doc = std::move (*bank);
            programFiles.assign ((std::size_t) dw::programCount (doc), {});
            for (int i = 0; i < dw::programCount (doc); ++i)
                for (const auto& z : dw::program (doc, i).zones)
                    programFiles[(std::size_t) i].push_back (resolve (z.path));
            shown = std::max (0, dw::slotProgram (doc, 0));
            currentZone = 0;
            loadShown();
            commit();
            return true;
        }
        return false;
    }

    bool openSfz (const juce::File& file)
    {
        const auto folderPath = file.getParentDirectory().getFullPathName().toStdString();
        const auto parsed = sfz::parse (file.loadFileAsString().toStdString(), [&file] (const std::string& include) -> std::optional<std::string> {
            const auto f = file.getParentDirectory().getChildFile (juce::String::fromUTF8 (include.c_str()));
            if (! f.existsAsFile())
                return std::nullopt;
            return f.loadFileAsString().toStdString();
        });
        auto program = dw::programFromSfz (parsed, folderPath, [] (const std::string& p) {
            const auto found = existingPathIgnoringCase (p);
            return juce::File (juce::String::fromUTF8 (found.c_str())).existsAsFile() ? found : std::string();
        });
        std::vector<std::string> f;
        std::vector<dw::Zone> zones;
        for (auto& z : program.zones)
        {
            const auto data = engine.getSampleLibrary().load (juce::String::fromUTF8 (z.path.c_str()));
            if (data == nullptr || data->frames() == 0)
                continue;
            juce::MemoryBlock bytes;
            juce::File (juce::String::fromUTF8 (z.path.c_str())).loadFileAsData (bytes);
            const std::vector<std::uint8_t> raw ((const std::uint8_t*) bytes.getData(), (const std::uint8_t*) bytes.getData() + bytes.getSize());
            const auto fresh = dw::newZone (z.path, z.name, (std::int64_t) data->frames(), data->isStereo() ? 2 : 1, (float) data->sampleRate,
                                            dw::isPcm16Wav (raw) ? 16 : 32, z.root, z.lowKey, z.highKey);
            z.frames = fresh.frames; z.channels = fresh.channels; z.rate = fresh.rate; z.bits = fresh.bits; z.beats = fresh.beats;
            if (z.loopMode < 0) // the file's own loop, as FL's sample load reads it
            {
                z.loopMode = data->loopStart >= 0 && data->loopEnd > data->loopStart ? 2 : 0;
                z.loopA = (int) std::max<std::int64_t> (0, data->loopStart);
                z.loopB = (int) std::max<std::int64_t> (0, data->loopEnd);
            }
            f.push_back (z.path);
            zones.push_back (z);
        }
        if (zones.empty())
            return false;
        program.zones = zones;
        program.name = file.getFileNameWithoutExtension().toStdString();
        dw::loadProgram (doc, shown, program);
        programFiles[(std::size_t) shown] = f;
        currentZone = 0;
        loadShown();
        commit();
        return true;
    }

    void diskMenu()
    {
        // FL's Options menu (OptionsMenu in the form): what LYNX performs of it
        juce::PopupMenu m;
        m.addItem (1, tr ("Open program / bank..."));
        m.addSectionHeader (tr ("Program"));
        m.addItem (2, tr ("Save"), lastSaved != juce::File());
        m.addItem (3, tr ("Save as..."));
        m.addSectionHeader (tr ("Bank"));
        m.addItem (4, tr ("Save as..."));
        m.addSectionHeader (tr ("Program"));
        m.addItem (5, tr ("Flush"));
        m.addItem (6, tr ("Flush all"));
        juce::Component::SafePointer<DirectWaveEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&diskButton), [self] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 1)
                self->choose (tr ("Open program / bank..."), "*.dwp;*.dwb;*.sfz", false, [self] (const juce::File& f) { if (self != nullptr) self->openFile (f); });
            else if (r == 2)
                self->saveProgramTo (self->lastSaved);
            else if (r == 3)
                self->choose (tr ("Save program as..."), "*.dwp", true, [self] (const juce::File& f) {
                    if (self != nullptr) self->saveProgramTo (f.withFileExtension (".dwp"));
                });
            else if (r == 4)
                self->choose (tr ("Save bank as..."), "*.dwb", true, [self] (const juce::File& f) {
                    if (self != nullptr) self->saveBankTo (f.withFileExtension (".dwb"));
                });
            else if (r == 5 || r == 6)
            {
                // Flush (16650) clears the program; Flush all (16651) all 128 (0x18002c0d0)
                for (int i = 0; i < dw::programCount (self->doc); ++i)
                    if (r == 6 || i == self->shown)
                    {
                        dw::loadProgram (self->doc, i, dw::program (dw::freshDoc(), i));
                        self->programFiles[(std::size_t) i].clear();
                    }
                self->loadShown();
                self->commit();
            }
        });
    }

    void choose (const juce::String& title, const juce::String& pattern, bool save, std::function<void (const juce::File&)> done)
    {
        chooser = lynx::ui::makeFileChooser (title, lastSaved != juce::File() ? lastSaved : folder, pattern);
        lynx::ui::launchChooser (chooser, (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                                : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles,
                                 [done] (const juce::FileChooser& fc) {
                                     if (fc.getResult() != juce::File())
                                         done (fc.getResult());
                                 });
    }

    void chooseSamples (bool replace)
    {
        chooser = lynx::ui::makeFileChooser (replace ? tr ("Replace sample(s)...") : tr ("Add sample(s)..."), folder, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<DirectWaveEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                                 [self, replace] (const juce::FileChooser& fc) {
                                     if (self == nullptr)
                                         return;
                                     juce::StringArray paths;
                                     for (const auto& r : fc.getResults())
                                         paths.add (r.getFullPathName());
                                     self->addSamples (paths, replace);
                                 });
    }

    void chooseFolder()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Browse for folder"), folder, {});
        juce::Component::SafePointer<DirectWaveEditor> self (this);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [self] (const juce::FileChooser& fc) {
            if (self != nullptr && fc.getResult().isDirectory())
                self->browseTo (fc.getResult());
        });
    }

    // ============================================================================================ menus
    template <std::size_t N>
    void listMenu (const char* const (&list)[N], int now, std::function<void (int)> pick)
    {
        juce::PopupMenu m;
        for (int i = 0; i < (int) N; ++i)
            if (juce::String (list[i]).startsWith ("<Reserved"))
                m.addItem (i + 1, list[i], false, false);
            else
                m.addItem (i + 1, tr (list[i]), true, i == now);
        m.showMenuAsync (juce::PopupMenu::Options(), [pick] (int r) {
            if (r > 0)
                pick (r - 1);
        });
    }

    void numberMenu (int lo, int hi, int now, std::function<void (int)> pick)
    {
        juce::PopupMenu m;
        for (int i = lo; i <= hi; ++i)
            m.addItem (i + 1, i == 0 ? juce::String ("---") : juce::String (i), true, i == now);
        m.showMenuAsync (juce::PopupMenu::Options(), [pick] (int r) {
            if (r > 0)
                pick (r - 1);
        });
    }

    void programListMenu()
    {
        juce::PopupMenu m;
        for (int i = 0; i < dw::programCount (doc); ++i)
        {
            const auto p = dw::program (doc, i);
            m.addItem (i + 1, juce::String (i + 1).paddedLeft ('0', 3) + "  " + text (p.name), true, i == shown);
        }
        juce::Component::SafePointer<DirectWaveEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&programMenu), [self] (int r) {
            if (self != nullptr && r > 0)
                self->showProgram (r - 1);
        });
    }

    // the zone grid's right-click menu (FL's ZoneMenu, in its order)
    void zoneMenu()
    {
        juce::PopupMenu m;
        m.addItem (16600, tr ("New zone"));
        m.addSectionHeader (tr ("Manage zones"));
        const auto any = ! prog.zones.empty();
        m.addItem (16601, tr ("Delete zone(s)"), any);
        m.addItem (16602, tr ("Delete all zones"), any);
        m.addItem (16603, tr ("Duplicate zone"), any);
        m.addSectionHeader (tr ("Samples"));
        m.addItem (16604, tr ("Add sample(s)..."));
        m.addItem (16605, tr ("Replace sample(s)..."), any);
        m.addSectionHeader (tr ("Mapping"));
        m.addItem (16606, tr ("Automap zone(s)"), any);
        m.addItem (16607, tr ("Remap zone(s) as drumkit"), any);
        m.addSectionHeader (tr ("Copy"));
        static const char* const fields[] = { TR_NOOP ("Keyboard tracking"), TR_NOOP ("Fine Tune"), TR_NOOP ("Ticks"), TR_NOOP ("Transpose semitone"), TR_NOOP ("Beat sync"), TR_NOOP ("Wave slice"),
                                              TR_NOOP ("Mute group"), TR_NOOP ("Samplerate"), TR_NOOP ("Root key"), TR_NOOP ("Low key"), TR_NOOP ("High key"), TR_NOOP ("Low velocity"), TR_NOOP ("High velocity") };
        // FL's submenu order: Beat sync, Fine Tune, Keyboard tracking, Mute group, Samplerate, Ticks, Transpose semitone,
        // Wave slice, then (Keys) Root key, Low key, High key, Low velocity, High velocity
        static const int order[] = { 4, 1, 0, 6, 7, 2, 3, 5, -1, 8, 9, 10, 11, 12 };
        for (int target = 0; target < 2; ++target)
        {
            juce::PopupMenu sub;
            for (const int k : order)
                if (k < 0)
                    sub.addSectionHeader (tr ("Keys"));
                else
                    sub.addItem (20000 + target * 100 + k, tr (fields[k]), any);
            m.addSubMenu (target == 0 ? tr ("Copy to all zones") : tr ("Copy to selected zones"), sub, any);
        }
        m.addSectionHeader (tr ("Locking"));
        m.addItem (16608, tr ("Lock all zones"), any);
        m.addItem (16609, tr ("Unlock all zones"), any);
        juce::Component::SafePointer<DirectWaveEditor> self (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [self] (int r) {
            if (self == nullptr || r == 0)
                return;
            if (r == 16604 || r == 16605)
                self->chooseSamples (r == 16605);
            else if (r >= 20000)
            {
                const auto target = (r - 20000) / 100, what = (r - 20000) % 100;
                std::vector<bool> targets;
                if (target == 1)
                    targets = self->selected;
                dw::copyToZones (self->prog.zones, self->currentZone, what, targets);
                self->commit();
            }
            else
                self->zoneCommand (r);
        });
    }

    // ============================================================================================ view
    void setPage (int p)
    {
        page = std::clamp (p, 0, 2);
        pageTabs.current = page;
        refresh();
    }

    std::shared_ptr<const SampleData> currentSample()
    {
        if (prog.zones.empty() || shown >= (int) programFiles.size())
            return nullptr;
        const auto& f = programFiles[(std::size_t) shown];
        const auto& path = (std::size_t) currentZone < f.size() ? f[(std::size_t) currentZone] : std::string();
        return path.empty() ? nullptr : engine.getSampleLibrary().load (juce::String::fromUTF8 (path.c_str()));
    }

    void previewCurrent()
    {
        if (auto s = currentSample())
            engine.previewSample (std::move (s));
    }

    void playKey (int key, bool on)
    {
        if (channel >= 0)
            engine.sendGuiNote (channel, key, 100.0f / 128.0f, on); // (GitHub #198: recorded while recording)
    }

    juce::String zonesText() const
    {
        juce::String s = juce::String ((int) prog.zones.size()) + " zones:";
        for (const auto& z : prog.zones)
            s << " [" << text (z.name) << " " << z.root << " " << z.lowKey << "-" << z.highKey << " v" << z.lowVel << "-" << z.highVel << "]";
        return s;
    }

    void refresh()
    {
        loading = true;
        const auto z = current();
        const auto hasZone = ! prog.zones.empty();
        programIndex.setText (juce::String (shown + 1).paddedLeft ('0', 3), juce::dontSendNotification);
        programName.setText (text (prog.name), juce::dontSendNotification);
        // the zone page: which part of each group is shown
        const auto onZone = page == pageZone, onSample = page == pageSample, onProgram = page == pageProgram;
        for (auto* t : { &mainTabs, &filterTabs, &envTabs, &lfoTabs, &matrixTabs })
        {
            t->setVisible (onZone);
            t->repaint();
        }
        auto partShown = [&] (int part) {
            if (! onZone)
                return false;
            if (part <= 2) return mainTabs.current == part;
            if (part <= 4) return filterTabs.current == part - 3;
            if (part <= 9) return lfoTabs.current == part - 8;
            if (part <= 13) return matrixTabs.current == part - 10;
            return true;
        };
        for (std::size_t i = 0; i < zoneWheels.size(); ++i)
        {
            auto& w = *zoneWheels[i];
            const auto show = partShown (zoneWheelPart[i]);
            w.knob.setVisible (show);
            w.label.setVisible (show);
            w.knob.setEnabled (hasZone);
            auto copy = z;
            w.knob.setValue (zoneWheelField[i] (copy), juce::dontSendNotification);
            w.label.value = w.knob.getTextFromValue (w.knob.getValue());
            w.label.repaint();
        }
        for (int e = 0; e < 3; ++e)
            for (int i = 0; i < 4; ++i)
            {
                auto& s = envSliders[(std::size_t) (e * 4 + i)];
                s.setVisible (onZone && envTabs.current == e);
                s.setValue (e == 0 ? z.amp[i] : z.modEnv[e - 1][i], juce::dontSendNotification);
            }
        const auto trig = partShown (2);
        triggerGroup.setVisible (trig);
        triggerGroupLabel.setVisible (trig);
        triggerType.setVisible (trig);
        triggerTypeLabel.setVisible (trig);
        triggerGroupLabel.setText (tr ("Group") + " " + (z.altGroup == 0 ? juce::String ("---") : juce::String (z.altGroup)), juce::dontSendNotification);
        triggerTypeLabel.setText (tr (entry (triggerTypes, groupOf (z).mode)), juce::dontSendNotification);
        for (int k = 0; k < 2; ++k)
        {
            const auto f = partShown (3 + k), l = partShown (8 + k);
            filterType[k].setVisible (f);
            filterTypeLabel[k].setVisible (f);
            filterTypeLabel[k].setText (tr (entry (filterTypes, z.filter[k].type)), juce::dontSendNotification);
            lfoWave[k].setVisible (l);
            lfoWaveLabel[k].setVisible (l);
            lfoWaveLabel[k].setText (tr (entry (waveforms, z.lfo[k].type)), juce::dontSendNotification);
            lfoSync[k].setVisible (l);
            lfoSync[k].set (z.lfo[k].sync != 0);
        }
        serial.setVisible (partShown (3));
        parallel.setVisible (partShown (3));
        serial.on = ! z.parallelFilters;
        parallel.on = z.parallelFilters;
        for (int s = 0; s < 16; ++s)
        {
            const auto show = partShown (10 + s / 4);
            for (juce::Component* c : { (juce::Component*) &modSrc[s], (juce::Component*) &modDst[s], (juce::Component*) &modSrcLabel[s], (juce::Component*) &modDstLabel[s] })
                c->setVisible (show);
            modSrcLabel[s].setText (tr (entry (modSources, z.mods[(std::size_t) s].src)), juce::dontSendNotification);
            modDstLabel[s].setText (tr (entry (modDestinations, z.mods[(std::size_t) s].dst)), juce::dontSendNotification);
        }
        stretchMode.setVisible (partShown (1));
        stretchSync.setVisible (partShown (1));
        stretchMode.set (z.stretchMode != 0);
        stretchSync.set (z.stretchSync != 0);
        for (int k = 0; k < 4; ++k)
        {
            fxSwitch[k].setVisible (onZone);
            fxSwitch[k].set (z.fxOn[k] != 0);
        }
        editSelection.setVisible (onZone);
        editAll.setVisible (onZone);
        editSelection.on = ! editGlobal;
        editAll.on = editGlobal;
        // the sample page
        sampleView.setVisible (onSample);
        for (std::size_t i = 0; i < sampleWheels.size(); ++i)
        {
            auto& w = *sampleWheels[i];
            w.knob.setVisible (onSample);
            w.label.setVisible (onSample);
            auto copy = z;
            const auto frames = (double) std::max<std::int64_t> (1, z.frames);
            w.knob.setValue ((double) sampleWheelField[i] (copy) / frames, juce::dontSendNotification);
            w.label.value = juce::String (sampleWheelField[i] (copy));
        }
        loopType.setVisible (onSample);
        loopTypeLabel.setVisible (onSample);
        loopTypeLabel.setText (tr (entry (loopModes, z.loopMode)), juce::dontSendNotification);
        sampleName.setVisible (onSample);
        sampleName.setText (text (z.path.empty() ? z.name : z.path), juce::dontSendNotification);
        // the program page
        for (std::size_t i = 0; i < programWheels.size(); ++i)
        {
            auto& w = *programWheels[i];
            w.knob.setVisible (onProgram);
            w.label.setVisible (onProgram);
            auto copy = prog;
            w.knob.setValue (programWheelField[i] (copy), juce::dontSendNotification);
            w.label.value = w.knob.getTextFromValue (w.knob.getValue());
            w.label.repaint();
        }
        playMode.setVisible (onProgram);
        playModeLabel.setVisible (onProgram);
        playModeLabel.setText (tr (entry (playModes, prog.voiceMode)), juce::dontSendNotification);
        for (int k = 0; k < 2; ++k)
        {
            progLfoWave[k].setVisible (onProgram);
            progLfoWaveLabel[k].setVisible (onProgram);
            progLfoWaveLabel[k].setText (tr (entry (waveforms, prog.lfo[k].type)), juce::dontSendNotification);
            progLfoSync[k].setVisible (onProgram);
            progLfoSync[k].set (prog.lfo[k].sync != 0);
        }
        for (auto* l : { &driveA, &driveB, &delayOn, &reverbOn, &chorusOn })
            l->setVisible (onProgram);
        driveA.set (prog.shape1);
        driveB.set (prog.shape2);
        delayOn.set (prog.delayOn);
        reverbOn.set (prog.reverbOn);
        chorusOn.set (prog.chorusOn);
        for (auto* c : { &delayBounce, &delayNormal, &glideFixed, &glideAuto })
            c->setVisible (onProgram);
        delayBounce.on = prog.delayCross;
        delayNormal.on = ! prog.delayCross;
        glideFixed.on = ! prog.glideLegatoOnly;
        glideAuto.on = prog.glideLegatoOnly;
        // the side panel
        const auto browser = sideTabs.current == 0;
        for (juce::Component* c : { (juce::Component*) &browsePath, (juce::Component*) &browseHome, (juce::Component*) &browseParent,
                                    (juce::Component*) &browseFolder, (juce::Component*) &browseRefresh, (juce::Component*) &browsePreview,
                                    (juce::Component*) &files, (juce::Component*) &browseInfo })
            c->setVisible (browser);
        clipVolume.setVisible (! browser);
        compatibleVelocity.setVisible (! browser);
        clipVolume.on = (flags() & 1) != 0;
        compatibleVelocity.on = (flags() & 2) != 0;
        loading = false;
        repaint();
    }

    void browseTo (const juce::File& f)
    {
        if (! f.isDirectory())
            return;
        folder = f;
        browsePath.setText (folder.getFullPathName(), juce::dontSendNotification);
        files.entries.clear();
        for (const auto& c : folder.findChildFiles (juce::File::findDirectories, false))
            files.entries.push_back (c);
        std::sort (files.entries.begin(), files.entries.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        std::vector<juce::File> list;
        for (const auto& c : folder.findChildFiles (juce::File::findFiles, false))
            if (c.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;dwp;dwb;sfz"))
                list.push_back (c);
        std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
        files.entries.insert (files.entries.end(), list.begin(), list.end());
        files.highlighted = -1;
        files.scroll = 0;
        browseInfo.setText (juce::String ((int) list.size()) + " " + tr ("files"), juce::dontSendNotification);
        files.repaint();
    }

    void openEntry (const juce::File& f)
    {
        if (f.isDirectory())
            browseTo (f);
        else if (f.hasFileExtension ("dwp;dwb;sfz"))
            openFile (f);
        else
            addSamples ({ f.getFullPathName() }, false);
    }

    // ============================================================================================ parts
    // the zone grid (FL's ZoneGrid string grid; its column captions from the editor's string table, REA_rodada190hz_dw.md
    // §1): one row per zone, the selected rows highlighted; a double click edits a cell (FL's ZoneGridHiddenEdit)
    struct ZoneGrid final : public juce::Component
    {
        DirectWaveEditor* owner = nullptr;
        int scroll = 0;

        static constexpr int rowHeight = 17;
        struct Column { const char* name; int width; };
        enum { cZone, cName, cSize, cRate, cMute, cRoot, cSemi, cFine, cKeyTrack, cTicks, cSync, cSlice, cLowKey, cHighKey, cLowVel,
               cHighVel, cLock, cVc };
        static const std::vector<Column>& columns()
        {
            static const std::vector<Column> c { { "Zone", 34 }, { "Zone Name", 140 }, { "Size", 56 }, { "Rate", 50 }, { "Mute", 34 },
                                                 { "Root", 36 }, { "Semi", 32 }, { "Fine", 32 }, { "K-Trk", 36 }, { "Ticks", 34 },
                                                 { "SY", 22 }, { "SL", 22 }, { "L-Key", 36 }, { "H-Key", 36 }, { "L-Vel", 34 },
                                                 { "H-Vel", 34 }, { "Lk", 22 }, { "VC", 21 } };
            return c;
        }

        juce::String cell (const dw::Zone& z, int i, int column) const
        {
            switch (column)
            {
                case cZone: return juce::String (i + 1).paddedLeft ('0', 3);
                case cName: return text (z.name);
                case cSize: return juce::String ((std::int64_t) z.frames);
                case cRate: return juce::String (juce::roundToInt (z.rate));
                case cMute: return juce::String (z.exclusiveGroup);
                case cRoot: return juce::String (z.root);
                case cSemi: return juce::String (z.coarse);
                case cFine: return juce::String (z.fine);
                case cKeyTrack: return juce::String (z.keyTrack);
                case cTicks: return juce::String (z.beats);
                case cSync: case cSlice: case cLock: return {}; // (drawn as switches)
                case cLowKey: return juce::String (z.lowKey);
                case cHighKey: return juce::String (z.highKey);
                case cLowVel: return juce::String (z.lowVel);
                case cHighVel: return juce::String (z.highVel);
                case cVc: return juce::String (z.altGroup);
                default: return {};
            }
        }

        static int keyFrom (const juce::String& t, int fallback)
        {
            const auto s = t.trim().toUpperCase();
            if (s.containsOnly ("-0123456789"))
                return s.isEmpty() ? fallback : s.getIntValue();
            static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            for (int k = 11; k >= 0; --k)
                if (s.startsWith (names[k]) && s.substring ((int) std::strlen (names[k])).containsOnly ("-0123456789"))
                    return k + 12 * s.substring ((int) std::strlen (names[k])).getIntValue();
            return fallback;
        }

        // a cell's new text into the zone (the keys accept FL's note names)
        void setCell (dw::Zone& z, int column, const juce::String& t) const
        {
            const auto n = t.getIntValue();
            switch (column)
            {
                case cName: z.name = t.toStdString(); break;
                case cRate: if (n > 0) z.rate = (float) n; break;
                case cMute: z.exclusiveGroup = std::clamp (n, 0, 255); break;
                case cRoot: z.root = std::clamp (keyFrom (t, z.root), 0, 127); break;
                case cSemi: z.coarse = std::clamp (n, -127, 127); break;
                case cFine: z.fine = std::clamp (n, -127, 127); break;
                case cKeyTrack: z.keyTrack = std::clamp (n, -32768, 32767); break;
                case cTicks: z.beats = std::max (0, n); break;
                case cLowKey: z.lowKey = std::clamp (keyFrom (t, z.lowKey), 0, 127); break;
                case cHighKey: z.highKey = std::clamp (keyFrom (t, z.highKey), 0, 127); break;
                case cLowVel: z.lowVel = std::clamp (n, 0, 127); break;
                case cHighVel: z.highVel = std::clamp (n, 0, 127); break;
                case cVc: z.altGroup = std::clamp (n, 0, 99); break;
                default: break;
            }
        }

        int columnAt (int x) const
        {
            int at = 0;
            for (int c = 0; c < (int) columns().size(); ++c)
            {
                at += juce::roundToInt (columns()[(std::size_t) c].width * scale);
                if (x < at)
                    return c;
            }
            return -1;
        }

        int columnX (int column) const
        {
            int at = 0;
            for (int c = 0; c < column; ++c)
                at += juce::roundToInt (columns()[(std::size_t) c].width * scale);
            return at;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::bg0);
            const auto& zones = owner->prog.zones;
            const auto head = juce::roundToInt (rowHeight * scale), h = head;
            g.setFont (theme::font (theme::type::caption, true));
            g.setColour (theme::colour::bg3);
            g.fillRect (0, 0, getWidth(), head);
            for (int c = 0; c < (int) columns().size(); ++c)
            {
                const auto x = columnX (c), w = juce::roundToInt (columns()[(std::size_t) c].width * scale);
                g.setColour (theme::colour::textSecondary);
                g.drawText (tr (columns()[(std::size_t) c].name), x + 3, 0, w - 5, head, juce::Justification::centredLeft, true);
                g.setColour (theme::colour::border);
                g.drawVerticalLine (x + w - 1, 0.0f, (float) getHeight());
            }
            g.setFont (theme::font (theme::type::small));
            for (int row = 0; row + scroll < (int) zones.size() && head + row * h < getHeight(); ++row)
            {
                const auto i = row + scroll;
                const juce::Rectangle<int> r (0, head + row * h, getWidth(), h);
                const auto sel = (std::size_t) i < owner->selected.size() && owner->selected[(std::size_t) i];
                const auto& z = zones[(std::size_t) i];
                g.setColour (sel ? theme::colour::accentMuted : (i % 2 ? theme::colour::bg1 : theme::colour::bg0));
                g.fillRect (r);
                if (i == owner->currentZone)
                {
                    g.setColour (theme::colour::accent);
                    g.drawRect (r, 1);
                }
                for (int c = 0; c < (int) columns().size(); ++c)
                {
                    g.setColour (sel ? theme::colour::textPrimary : theme::colour::textSecondary);
                    if (c == cSync || c == cSlice || c == cLock)
                    {
                        const auto on = c == cSync ? z.syncTempo : c == cSlice ? z.sliceKeys : z.output != 0;
                        const auto d = (float) h * 0.6f;
                        const auto led = juce::Rectangle<float> (d, d).withCentre ({ (float) columnX (c) + juce::roundToInt (columns()[(std::size_t) c].width * scale) * 0.5f, (float) r.getCentreY() });
                        g.setColour (on ? theme::colour::accent : theme::colour::bg3);
                        g.fillEllipse (led);
                        g.setColour (theme::colour::borderStrong);
                        g.drawEllipse (led, 1.0f);
                        continue;
                    }
                    g.drawText (cell (z, i, c), columnX (c) + 3, r.getY(), juce::roundToInt (columns()[(std::size_t) c].width * scale) - 5, h,
                                juce::Justification::centredLeft, true);
                }
            }
            if (zones.empty())
            {
                g.setColour (theme::colour::textDisabled);
                g.drawText (tr ("Drop samples here or right click for New zone / Add sample(s)"), getLocalBounds().withTrimmedTop (head),
                            juce::Justification::centred, true);
            }
        }

        int rowAt (int y) const
        {
            const auto head = juce::roundToInt (rowHeight * scale);
            return y < head ? -1 : scroll + (y - head) / head;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto row = rowAt (e.y);
            if (row >= 0 && row < (int) owner->prog.zones.size())
            {
                const auto alreadySelected = (std::size_t) row < owner->selected.size() && owner->selected[(std::size_t) row];
                if (! (e.mods.isPopupMenu() && alreadySelected))
                    owner->selectZone (row, e.mods.isCtrlDown() || e.mods.isCommandDown(), e.mods.isShiftDown());
                // the flag cells (SY, SL, Lk) switch on a click
                const auto c = columnAt (e.x);
                if (! e.mods.isPopupMenu() && (c == cSync || c == cSlice || c == cLock))
                {
                    auto& z = owner->prog.zones[(std::size_t) row];
                    if (c == cSync) z.syncTempo = ! z.syncTempo;
                    else if (c == cSlice) z.sliceKeys = ! z.sliceKeys;
                    else z.output = z.output != 0 ? 0 : 1;
                    owner->commit();
                }
            }
            if (e.mods.isPopupMenu())
                owner->zoneMenu();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto row = rowAt (e.y), column = columnAt (e.x);
            if (row < 0 || row >= (int) owner->prog.zones.size() || column <= cZone || column == cSize || column == cSync
                || column == cSlice || column == cLock)
                return;
            auto* ed = new juce::TextEditor();
            ed->setText (cell (owner->prog.zones[(std::size_t) row], row, column), false);
            const auto head = juce::roundToInt (rowHeight * scale);
            ed->setBounds (columnX (column), head * (row - scroll + 1), juce::roundToInt (columns()[(std::size_t) column].width * scale) + (column == cName ? 0 : 20), head);
            juce::Component::SafePointer<DirectWaveEditor> self (owner);
            auto done = std::make_shared<bool> (false);
            ed->onReturnKey = [this, ed, self, row, column, done] {
                if (*done)
                    return;
                *done = true;
                if (self != nullptr && row < (int) self->prog.zones.size())
                {
                    setCell (self->prog.zones[(std::size_t) row], column, ed->getText());
                    self->commit();
                }
                juce::MessageManager::callAsync ([ed] { delete ed; });
            };
            ed->onEscapeKey = [ed, done] {
                *done = true;
                juce::MessageManager::callAsync ([ed] { delete ed; });
            };
            ed->onFocusLost = ed->onReturnKey;
            addAndMakeVisible (ed);
            ed->grabKeyboardFocus();
            ed->selectAll();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            scroll = std::clamp (scroll + (w.deltaY < 0.0f ? 1 : -1), 0, std::max (0, (int) owner->prog.zones.size() - 1));
            repaint();
        }
    };

    // the key / velocity map (FL's ZoneEditor under the grid): each zone a box over its keys and velocities
    struct KeyMap final : public juce::Component
    {
        DirectWaveEditor* owner = nullptr;
        int dragZone = -1;
        dw::ZoneHandle dragHandle = dw::ZoneHandle::body; // FL: the box moves the zone, two corner squares resize
        dw::Zone before;
        juce::Point<int> from;

        // FL's map: the keys between margins that hold the velocity scale (000 at the top, 127 at the bottom); the
        // keyboard below spans the same keys
        static constexpr float left = 20.0f, right = 14.0f;
        float keyX (int key) const { return left * scale + (float) key / 128.0f * ((float) getWidth() - (left + right) * scale); }
        float velY (int vel) const { return (float) vel / 128.0f * (float) getHeight(); }
        juce::Rectangle<float> box (const dw::Zone& z) const
        {
            return juce::Rectangle<float>::leftTopRightBottom (keyX (z.lowKey), velY (z.lowVel), keyX (z.highKey + 1), velY (z.highVel + 1));
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::bg0);
            for (int k = 0; k < 128; ++k)
                if (isBlack (k))
                {
                    g.setColour (theme::colour::bg1);
                    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (keyX (k), 0.0f, keyX (k + 1), (float) getHeight()));
                }
            g.setColour (theme::colour::border);
            for (int k = 0; k <= 128; k += 12)
                g.drawVerticalLine (juce::roundToInt (keyX (k)), 0.0f, (float) getHeight());
            g.drawHorizontalLine (juce::roundToInt (velY (64)), keyX (0), keyX (128));
            // the velocity scale on both sides
            g.setColour (theme::colour::textDisabled);
            g.setFont (theme::font (theme::type::caption));
            for (const auto& [v, label] : { std::pair<int, const char*> { 0, "000" }, { 64, "063" }, { 127, "127" } })
            {
                const auto y = std::clamp (velY (v) - 5.0f, 0.0f, (float) getHeight() - 11.0f);
                g.drawText (label, juce::Rectangle<float> (0.0f, y, left * scale, 11.0f), juce::Justification::centred, false);
                g.drawText (label, juce::Rectangle<float> ((float) getWidth() - right * scale - 2.0f, y, right * scale + 2.0f, 11.0f), juce::Justification::centred, false);
            }
            const auto& zones = owner->prog.zones;
            for (int i = 0; i < (int) zones.size(); ++i)
            {
                const auto& z = zones[(std::size_t) i];
                const auto r = box (z);
                const auto sel = (std::size_t) i < owner->selected.size() && owner->selected[(std::size_t) i];
                g.setColour ((sel ? theme::colour::accent : theme::colour::info).withAlpha (sel ? 0.45f : 0.22f));
                g.fillRect (r);
                g.setColour (sel ? theme::colour::accent : theme::colour::info);
                g.drawRect (r, i == owner->currentZone ? 2.0f : 1.0f);
                // the root key mark
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (keyX (z.root), r.getY(), keyX (z.root + 1), r.getY() + 4.0f));
                // FL's two resize squares on the current zone: upper-left = L-Key + L-Vel, lower-right = H-Key + H-Vel
                if (i == owner->currentZone)
                {
                    g.setColour (theme::colour::textPrimary);
                    g.fillRect (cornerSquare (r.getTopLeft()));
                    g.fillRect (cornerSquare (r.getBottomRight()));
                }
                // the name runs upwards through the box, in lower case (as FL draws it)
                if (r.getWidth() > 10.0f)
                {
                    juce::Graphics::ScopedSaveState keep (g);
                    g.reduceClipRegion (r.toNearestInt());
                    g.setColour (sel ? theme::colour::textPrimary : theme::colour::textSecondary);
                    g.setFont (theme::font (theme::type::caption));
                    g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
                    g.drawText (text (z.name).toLowerCase(), juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()),
                                juce::Justification::centred, true);
                }
            }
        }

        juce::Rectangle<float> cornerSquare (juce::Point<float> c) const
        {
            const auto side = std::max (4.0f, std::round (4.0f * scale));
            return juce::Rectangle<float> (side, side).withCentre (c);
        }

        // -1 = not on this zone; otherwise the handle under p (the corner squares exist on the current zone only)
        int handleAt (int index, juce::Point<int> p) const
        {
            const auto& z = owner->prog.zones[(std::size_t) index];
            const auto r = box (z);
            if (index == owner->currentZone)
            {
                if (cornerSquare (r.getTopLeft()).expanded (2.0f).contains (p.toFloat())) return (int) dw::ZoneHandle::lowCorner;
                if (cornerSquare (r.getBottomRight()).expanded (2.0f).contains (p.toFloat())) return (int) dw::ZoneHandle::highCorner;
            }
            return r.expanded (1.0f).contains (p.toFloat()) ? (int) dw::ZoneHandle::body : -1;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto& zones = owner->prog.zones;
            dragZone = -1;
            // the current zone first (overlapping boxes), then the others from the top
            std::vector<int> order;
            if (owner->currentZone < (int) zones.size())
                order.push_back (owner->currentZone);
            for (int i = (int) zones.size() - 1; i >= 0; --i)
                if (i != owner->currentZone)
                    order.push_back (i);
            for (const auto i : order)
            {
                const auto handle = handleAt (i, e.getPosition());
                if (handle >= 0)
                {
                    dragZone = i;
                    dragHandle = (dw::ZoneHandle) handle;
                    before = zones[(std::size_t) i];
                    from = e.getPosition();
                    // FL: Shift+click adds a zone to the selection; Ctrl / Alt are drag modifiers, not selection ones;
                    // a plain click on a zone of a multi-selection keeps the selection
                    const auto keep = i < (int) owner->selected.size() && owner->selected[(std::size_t) i] && ! e.mods.isShiftDown();
                    if (keep)
                        owner->currentZone = i, owner->refresh();
                    else
                        owner->selectZone (i, e.mods.isShiftDown(), false);
                    break;
                }
            }
            if (e.mods.isPopupMenu())
                owner->zoneMenu();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragZone < 0 || dragZone >= (int) owner->prog.zones.size())
                return;
            auto& z = owner->prog.zones[(std::size_t) dragZone];
            const auto dk = juce::roundToInt ((float) (e.x - from.x) / (float) getWidth() * 128.0f);
            const auto dv = juce::roundToInt ((float) (e.y - from.y) / (float) getHeight() * 128.0f);
            // FL: Alt and Ctrl+Alt move the root with the keys; a plain or Ctrl drag moves the keys only
            z = dw::dragZone (before, dragHandle, dk, dv, e.mods.isAltDown());
            repaint();
            owner->grid.repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragZone >= 0)
                owner->commit();
            dragZone = -1;
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int handle = -1;
            const auto n = (int) owner->prog.zones.size();
            if (owner->currentZone >= 0 && owner->currentZone < n)
                handle = handleAt (owner->currentZone, e.getPosition());
            for (int i = n - 1; handle < 0 && i >= 0; --i)
                handle = handleAt (i, e.getPosition());
            setMouseCursor (handle == (int) dw::ZoneHandle::lowCorner ? juce::MouseCursor::TopLeftCornerResizeCursor
                            : handle == (int) dw::ZoneHandle::highCorner ? juce::MouseCursor::BottomRightCornerResizeCursor
                            : handle == (int) dw::ZoneHandle::body ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }
    };

    // the keyboard under the map (FL's MIDIKb): plays the channel; the current zone's keys and its root are marked
    struct Keyboard final : public juce::Component
    {
        DirectWaveEditor* owner = nullptr;
        int down = -1;

        float keyX (int key) const { return (float) key / 128.0f * (float) getWidth(); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::bg0);
            const auto h = (float) getHeight();
            const auto& zones = owner->prog.zones;
            const dw::Zone* z = zones.empty() ? nullptr : &zones[(std::size_t) std::clamp (owner->currentZone, 0, (int) zones.size() - 1)];
            for (int k = 0; k < 128; ++k)
            {
                const auto r = juce::Rectangle<float>::leftTopRightBottom (keyX (k), 0.0f, keyX (k + 1), isBlack (k) ? h * 0.62f : h);
                const auto inZone = z != nullptr && k >= z->lowKey && k <= z->highKey;
                g.setColour (k == down ? theme::colour::accent
                             : z != nullptr && k == z->root ? theme::colour::accentHover
                             : inZone ? (isBlack (k) ? theme::colour::accentMuted : theme::colour::accent.withAlpha (0.35f).overlaidWith (theme::colour::textPrimary.withAlpha (0.55f)))
                             : isBlack (k) ? theme::colour::bg1 : theme::colour::textPrimary.withAlpha (0.85f));
                g.fillRect (r.reduced (0.5f, 0.0f));
            }
            g.setColour (theme::colour::textDisabled);
            g.setFont (theme::font (theme::type::caption));
            for (int k = 0; k < 128; k += 12)
                g.drawText (keyName (k), juce::Rectangle<float> (keyX (k), h - 11.0f, 30.0f, 11.0f), juce::Justification::centredLeft, false);
        }

        int keyAt (juce::Point<int> p) const
        {
            const auto k = std::clamp ((int) ((float) p.x / (float) getWidth() * 128.0f), 0, 127);
            if (p.y < getHeight() * 0.62f)
                for (const auto n : { k - 1, k, k + 1 })
                    if (n >= 0 && n < 128 && isBlack (n) && (float) p.x >= keyX (n) && (float) p.x < keyX (n + 1))
                        return n;
            return k;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            down = keyAt (e.getPosition());
            owner->playKey (down, true);
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (down >= 0)
                owner->playKey (down, false);
            down = -1;
            repaint();
        }
    };

    // the side browser's file list (FL's FileBrowserStringGrid)
    struct FileList final : public juce::Component
    {
        DirectWaveEditor* owner = nullptr;
        std::vector<juce::File> entries;
        int highlighted = -1, scroll = 0;
        static constexpr int rowHeight = 16;

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::colour::bg0);
            g.setFont (theme::font (theme::type::small));
            const auto h = juce::roundToInt (rowHeight * scale);
            for (int row = 0; row + scroll < (int) entries.size() && row * h < getHeight(); ++row)
            {
                const auto i = row + scroll;
                const auto& f = entries[(std::size_t) i];
                const juce::Rectangle<int> r (0, row * h, getWidth(), h);
                if (i == highlighted)
                {
                    g.setColour (theme::colour::accentMuted);
                    g.fillRect (r);
                }
                g.setColour (f.isDirectory() ? theme::colour::warning : f.hasFileExtension ("dwp;dwb") ? theme::colour::info : theme::colour::textPrimary);
                g.drawText ((f.isDirectory() ? juce::String::fromUTF8 ("\xe2\x96\xb8 ") : juce::String ("  ")) + f.getFileName(), r.reduced (4, 0), juce::Justification::centredLeft, true);
            }
        }

        int rowAt (int y) const { return scroll + y / juce::roundToInt (rowHeight * scale); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto i = rowAt (e.y);
            if (i < 0 || i >= (int) entries.size())
                return;
            highlighted = i;
            repaint();
            const auto f = entries[(std::size_t) i];
            if (e.mods.isPopupMenu() && ! f.isDirectory())
            {
                // FL's BrowseFolderMenu
                juce::PopupMenu m;
                m.addItem (1, tr ("Add Sample(s)"));
                m.addItem (2, tr ("Replace Samples(s)"), ! owner->prog.zones.empty());
                juce::Component::SafePointer<DirectWaveEditor> self (owner);
                m.showMenuAsync (juce::PopupMenu::Options(), [self, f] (int r) {
                    if (self != nullptr && r > 0)
                        self->addSamples ({ f.getFullPathName() }, r == 2);
                });
                return;
            }
            if (owner->previewOn && ! f.isDirectory() && ! f.hasFileExtension ("dwp;dwb"))
                if (auto s = owner->engine.getSampleLibrary().load (f.getFullPathName()))
                    owner->engine.previewSample (std::move (s));
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const auto i = rowAt (e.y);
            if (i >= 0 && i < (int) entries.size())
                owner->openEntry (entries[(std::size_t) i]);
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            scroll = std::clamp (scroll + (w.deltaY < 0.0f ? 3 : -3), 0, std::max (0, (int) entries.size() - 1));
            repaint();
        }
    };

    // the peak meter (FL's TWAVVolumeter beside the pages): the channel's output
    struct Meter final : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (theme::colour::bg0);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 2.0f);
        }
    };

    AudioEngine& engine;
    std::function<ChannelDef* (Song&)> locate;
    int channel = -1;

    dw::Doc doc;
    dw::Program prog;
    int shown = -1, played = -1, currentZone = 0, page = pageProgram; // (FL opens on the Program page)
    std::vector<bool> selected;
    std::vector<std::vector<std::string>> programFiles; // per program record, per zone: the file LYNX reads
    std::vector<std::uint8_t> lastState;
    std::vector<SampleZone> lastZones;
    std::vector<float> lastParams;
    bool loading = false, editGlobal = true, zoneEditorBig = false, previewOn = true; // (FL: Global)
    juce::File folder, lastSaved;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::TextButton diskButton, programPrev, programNext, programMenu, toggleKeyMap;
    Tabs pageTabs, sideTabs, mainTabs, filterTabs, envTabs, lfoTabs, matrixTabs;
    juce::Label programIndex, programName;
    ZoneGrid grid;
    KeyMap keyMap;
    Keyboard keyboard;
    Meter meter;

    std::vector<std::unique_ptr<Wheel>> zoneWheels, sampleWheels, programWheels;
    std::vector<int> zoneWheelPart;
    std::vector<std::function<float& (dw::Zone&)>> zoneWheelField;
    std::vector<std::function<int& (dw::Zone&)>> sampleWheelField;
    std::vector<std::function<float& (dw::Program&)>> programWheelField;
    std::array<juce::Slider, 12> envSliders;
    juce::TextButton triggerGroup, triggerType, filterType[2], lfoWave[2], modSrc[16], modDst[16], loopType, playMode, progLfoWave[2];
    juce::Label triggerGroupLabel, triggerTypeLabel, filterTypeLabel[2], lfoWaveLabel[2], modSrcLabel[16], modDstLabel[16], loopTypeLabel,
        playModeLabel, progLfoWaveLabel[2], sampleName;
    Led lfoSync[2], stretchMode, stretchSync, fxSwitch[4], progLfoSync[2], driveA, driveB, delayOn, reverbOn, chorusOn;
    CaptionLed serial { "" }, parallel { "" }, editSelection { tr ("Current selection") }, editAll { tr ("Global") };
    CaptionLed delayBounce { tr ("Bounce"), true }, delayNormal { tr ("Normal"), true }, glideFixed { tr ("Fixed") }, glideAuto { tr ("Auto") };
    CaptionLed clipVolume { tr ("Clip volume") }, compatibleVelocity { tr ("Compatible velocity") };
    SampleView sampleView;
    struct MenuName { juce::Label* label; juce::Component* button; juce::String name; };
    std::vector<MenuName> menuNames;

    juce::TextButton browseHome, browseParent, browseFolder, browseRefresh, browsePreview;
    juce::Label browsePath, browseInfo;
    FileList files;
};
} // namespace

std::unique_ptr<juce::Component> makeDirectWaveEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel)
{
    return std::make_unique<DirectWaveEditor> (engine, std::move (locate), channel);
}

juce::String directWaveEditorTest (juce::Component& root, const juce::String& command)
{
    std::function<DirectWaveEditor* (juce::Component&)> find = [&] (juce::Component& c) -> DirectWaveEditor* {
        if (auto* e = dynamic_cast<DirectWaveEditor*> (&c))
            return e;
        for (auto* child : c.getChildren())
            if (auto* e = find (*child))
                return e;
        return nullptr;
    };
    auto* editor = find (root);
    return editor != nullptr ? editor->test (command) : juce::String ("no DirectWave editor");
}
} // namespace lynx
