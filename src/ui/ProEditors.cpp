#include "ui/SlotWatch.h"
#include "ui/ProEditors.h"
#include "ui/PluginEditors.h"

#include <cmath>
#include <map>
#include <tuple>

#include "core/WindowFit.h"
#include "core/MixerOps.h"
#include "core/ApexLimiter.h"
#include "core/DynamicsSuite.h"
#include "core/FacetEq.h"
#include "core/MixSuite.h"
#include "core/StrikeShaper.h"
#include "core/PocketFx.h"
#include "ui/DynamicsDisplays.h"
#include "ui/ParametricEqEditor.h"
#include "ui/ProEditorBase.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
using namespace pro;
using namespace theme;

// a synthetic programme for the --plugui snapshots: a beat (kick on the beat, snare on the off-beat) over a
// sustained part, 10 ms columns, deterministic
float demoInDb (int i)
{
    const auto t = (double) i * 0.01;
    const auto beat = std::fmod (t, 0.5), off = std::fmod (t + 0.25, 0.5);
    const auto noise = (float) ((std::sin ((double) i * 12.9898) * 43758.5453) - std::floor (std::sin ((double) i * 12.9898) * 43758.5453));
    const auto body = -20.0 + 3.0 * std::sin (t * 0.9) + 2.5 * (double) noise;
    const auto kick = 13.0 * std::exp (-beat / 0.05);
    const auto snare = 9.0 * std::exp (-off / 0.035);
    return (float) juce::jmin (-0.5, body + juce::jmax (kick, snare));
}

juce::Colour inColour() { return juce::Colour (0xff7d8597); }
juce::Colour outColour() { return colour::info; }

// --- the scrolling level display shared by the limiter, compressor, gate and de-esser ----------------------
// dB → y over a display area: a linear part from 0 to `knee` dB over the top 70 % and the rest down to `floor`
struct LevelScale
{
    float knee = -15.0f, floor = -48.0f, top = 0.0f;
    float y (float db, juce::Rectangle<float> a) const
    {
        const auto d = juce::jlimit (floor, top + 6.0f, db) - top;
        const auto k = knee - top;
        const auto f = d >= k ? 0.7f * d / k : 0.7f + 0.3f * (d - k) / (floor - knee);
        return a.getY() + a.getHeight() * f;
    }
    float db (float yy, juce::Rectangle<float> a) const
    {
        const auto f = (yy - a.getY()) / juce::jmax (1.0f, a.getHeight());
        const auto k = knee - top;
        return f <= 0.7f ? top + k * f / 0.7f : knee + (floor - knee) * (f - 0.7f) / 0.3f;
    }
};

void scaleLabels (juce::Graphics& g, const LevelScale& s, juce::Rectangle<float> a, float labelX, std::initializer_list<float> marks, bool lines = true)
{
    g.setFont (font (type::caption));
    for (const auto m : marks)
    {
        const auto y = s.y (m, a);
        if (lines)
        {
            g.setColour (colour::gridBeat);
            g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
        }
        g.setColour (colour::textDisabled);
        g.drawText (m == 0.0f ? juce::String ("0 dB") : juce::String ((int) m), juce::Rectangle<float> (labelX, y - 7.0f, 40.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }
}

// =============================================================================================================
// Apex Limiter — the reference limiter's workflow: the scrolling display is the window (input after gain in grey,
// output in blue, gain reduction hanging from the top in red, the biggest reductions tagged), the GAIN slider on
// its left edge, the output / gain-reduction meters on the right, the style / look-ahead / release / link panel at
// the bottom left (folds away), true-peak and the output ceiling in the bottom bar.
class ApexEditor final : public ProEditor
{
public:
    ApexEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::apexLimiter, d, 950, 600) { knobRanges = captionAbove = true; fillDemo(); }

private:
    enum { idPanel = 1, idGain, idCeiling, idTp, idAdvanced, idLoudness, idLoudReset, idLevel };
    // round 190ar: the levelling stage (Level threshold; 0 = off, as every project saved before has it) is switched
    // by a toggle that remembers the threshold it had (-12 dB the first time)
    float levelThreshold = 1.0f / 3.0f;
    bool levelling() const { return v (apex::pLevelThreshold) > 0.0f; }
    // round tp172b4: the reference opens with its knob panel folded into the tab at the lower left (its idle view)
    bool panelOpen = false, advancedOpen = false;
    static constexpr const char* styles = "Clean|Glue|Punch|Dynamic|Dense|Loud|Bus|Safe";

    // the display / meter scale chosen with the meter-scale menu (round 128): −16 / −32 / −48 dB, K-12 / K-14 / K-20
    int meterScale() const { return apex::meterScale (v (apex::pMeterScale)); }
    LevelScale scaleNow() const
    {
        switch (meterScale())
        {
            case 0: return { -6.0f, -16.0f };
            case 1: return { -12.0f, -32.0f };
            case 3: return { -24.0f, -60.0f };
            case 4: return { -26.0f, -62.0f };
            case 5: return { -32.0f, -68.0f };
            default: return { -15.0f, -48.0f };
        }
    }
    float kOffset() const { const auto m = meterScale(); return m == 3 ? 12.0f : m == 4 ? 14.0f : m == 5 ? 20.0f : 0.0f; }
    float outHold = -120.0f, grHold = 0.0f;
    int holdAge = 0;

    // round 135: the display modes (Fast = 10 ms a pixel, Slow = 30 ms, Slow down = recent fast and the past
    // compressed, Infinite = 0.5 s a pixel from a long history, Off = meters only) and the loudness panel
    int displayMode() const { return apex::displayMode (v (apex::pDisplayMode)); }
    bool loudnessShown() const { return v (apex::pLoudnessShown) >= 0.5f; }
    bool truePeakMeter() const { return v (apex::pPeakMeter) >= 0.5f; }
    static constexpr int longSpan = 50; // history columns (10 ms) per column of the infinite history
    std::vector<Column> longHistory = std::vector<Column> ((std::size_t) historyLength);
    int longPos = 0, longCount = 0, seenPos = 0;
    Column longAcc;
    static void merge (Column& into, const Column& c)
    {
        into.in = juce::jmax (into.in, c.in);
        into.out = juce::jmax (into.out, c.out);
        into.gr = juce::jmax (into.gr, c.gr);
    }
    // the columns drawn, newest first (one per pixel)
    std::vector<Column> view (int n) const
    {
        std::vector<Column> out ((std::size_t) juce::jmax (0, n));
        const auto mode = displayMode();
        if (mode == 0)
        {
            for (int k = 0; k < n; ++k) out[(std::size_t) k] = columnBack (k);
            return out;
        }
        if (mode == 3 && ! demo)
        {
            for (int k = 0; k < n; ++k)
            {
                auto c = longHistory[(std::size_t) ((longPos - 1 - k + historyLength * 4) % historyLength)];
                if (k == 0) merge (c, longAcc);
                out[(std::size_t) k] = c;
            }
            return out;
        }
        // Slow: 3 columns a pixel; Slow down: 1 column a pixel at the right edge, the span growing to the left
        // (demo snapshots show the infinite mode as Slow)
        double start = 0.0;
        for (int k = 0; k < n; ++k)
        {
            const double span = mode == 2 ? 1.0 + 3.0 * ((double) k / juce::jmax (1, n)) * ((double) k / juce::jmax (1, n)) : 3.0;
            const auto a = (int) start, b = juce::jmax (a + 1, (int) (start + span));
            Column c { -120.0f, -120.0f, 0.0f };
            for (int j = a; j < b && j < historyLength; ++j) merge (c, columnBack (j));
            out[(std::size_t) k] = c;
            start += span;
        }
        return out;
    }
    juce::Rectangle<float> loudnessArea() const { return bodyArea().withTrimmedLeft (bodyArea().getWidth() - 196.0f).reduced (6.0f, 8.0f); }

    juce::String title() const override { return "Apex Limiter"; }
    juce::String gestures() const override { return tr ("Drag GAIN on the left  -  drag the red ceiling line  -  wheel on the display: gain"); }
    bool usesHistory() const override { return true; }
    float grDb() const override { return liveMeter (0) * 30.0f; }

    double gainDb() const { return apex::gainDb (v (0)); }
    double ceilingDb() const { return apex::ceilingDb (v (1)); }

    juce::Rectangle<float> display() const { return bodyArea().withTrimmedRight (104.0f + (loudnessShown() ? 196.0f : 0.0f)); }
    juce::Rectangle<float> plot() const { return display().withTrimmedTop (14.0f).withTrimmedBottom (6.0f); }
    juce::Rectangle<float> gainTrack() const
    {
        const auto p = plot();
        return { p.getX() + 14.0f, p.getY() + 6.0f, 34.0f, p.getHeight() * 0.62f };
    }
    juce::Rectangle<float> gainBox() const
    {
        const auto tr_ = gainTrack();
        const auto y = tr_.getBottom() - tr_.getHeight() * v (0);
        return { tr_.getX() - 4.0f, y - 11.0f, 50.0f, 22.0f };
    }
    juce::Rectangle<float> panel() const
    {
        const auto p = plot();
        return { p.getX() + 8.0f, p.getBottom() - 104.0f, 560.0f, 96.0f };
    }

    Column demoColumn (int i) const override
    {
        // gain into the ceiling, instant attack and the release recovering ~0.25 dB per column
        float gr = 0.0f;
        for (int k = 0; k < 80 && i - k >= 0; ++k)
            gr = juce::jmax (gr, demoInDb (i - k) + (float) gainDb() - (float) ceilingDb() - 0.25f * (float) k);
        const auto in = demoInDb (i);
        return { in, in + (float) gainDb() - gr, gr };
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        out.push_back (custom (gainTrack().expanded (8.0f, 14.0f), idGain, 0));
        const auto d = plot();
        out.push_back (custom ({ d.getX() + 60.0f, scaleNow().y ((float) ceilingDb(), d) - 5.0f, d.getWidth() - 60.0f, 10.0f }, idCeiling, 1,
                               tr ("Output ceiling: drag up or down (the bottom bar's Out does the same)")));
        // round tp172b4: the reference's strip at the display's lower right — TP, the loudness meters, the meter scale —
        // with the display speed left of it
        out.push_back (button ({ d.getRight() - 154.0f, d.getBottom() - 26.0f, 30.0f, 20.0f }, idTp, "TP", v (5) >= 0.5f,
                               tr ("True peak: the ceiling also holds between the samples (4x interpolation)")));
        out.push_back (button ({ d.getRight() - 123.0f, d.getBottom() - 26.0f, 46.0f, 20.0f }, idLoudness, "LUFS", loudnessShown(),
                               tr ("Loudness meter: momentary, short term and integrated LUFS, loudness range, peak")));
        {
            auto dm = menu ({ d.getRight() - 240.0f, d.getBottom() - 26.0f, 82.0f, 20.0f }, apex::pDisplayMode, {}, "Fast|Slow|Slow down|Infinite|Off");
            dm.hint = tr ("Display speed: Fast, Slow, Slow down (the past compressed), Infinite (the whole take), Off (meters only)");
            out.push_back (dm);
        }
        if (loudnessShown())
        {
            const auto la = loudnessArea();
            auto tgt = menu ({ la.getRight() - 84.0f, la.getY() + 4.0f, 80.0f, 20.0f }, apex::pLoudnessTarget, {}, "-23 LUFS|-18 LUFS|-16 LUFS|-14 LUFS|-11 LUFS|-9 LUFS");
            tgt.hint = tr ("Loudness target: the line on the loudness bars");
            out.push_back (tgt);
            out.push_back (tabs ({ la.getX() + 4.0f, la.getBottom() - 54.0f, la.getWidth() - 8.0f, 22.0f }, apex::pPeakMeter, "Sample peak|True peak"));
            out.push_back (button ({ la.getX() + 4.0f, la.getBottom() - 26.0f, la.getWidth() - 8.0f, 22.0f }, idLoudReset, tr ("Reset"), false,
                                   tr ("Reset the integrated loudness, the range and the peak hold")));
        }
        if (panelOpen)
        {
            const auto p = panel();
            out.push_back (button ({ p.getX() - 2.0f, p.getY() + 6.0f, 16.0f, 18.0f }, idPanel, juce::String::fromUTF8 ("\xc2\xab"), false, tr ("Hide the knobs")));
            out.push_back (menu ({ p.getX() + 16.0f, p.getY() + 40.0f, 96.0f, 26.0f }, apex::pStyle, {}, styles));
            out.push_back (knob ({ p.getX() + 122.0f, p.getY() + 22.0f, 68.0f, 72.0f }, apex::pLookahead, tr ("Lookahead")));
            out.push_back (knob ({ p.getX() + 192.0f, p.getY() + 22.0f, 68.0f, 72.0f }, apex::pAttack, tr ("Attack")));
            out.push_back (knob ({ p.getX() + 262.0f, p.getY() + 22.0f, 68.0f, 72.0f }, apex::pRelease, tr ("Release")));
            // (the linking knobs a row lower: their names sit under the CHANNEL LINKING heading, as the reference's)
            out.push_back (knob ({ p.getX() + 346.0f, p.getY() + 34.0f, 80.0f, 66.0f }, apex::pLink, tr ("Transients")));
            out.push_back (knob ({ p.getX() + 428.0f, p.getY() + 34.0f, 80.0f, 66.0f }, apex::pLinkRelease, tr ("Release")));
            out.push_back (button ({ p.getRight() - 30.0f, p.getY() + 4.0f, 26.0f, p.getHeight() - 8.0f }, idAdvanced,
                                   juce::String::fromUTF8 (advancedOpen ? "\xc2\xab" : "\xc2\xbb"), advancedOpen,
                                   tr ("Advanced: DC offset filter, unity gain, audition limiting, side-chain triggering")));
            if (advancedOpen)
            {
                const auto ad = advanced();
                out.push_back (toggle ({ ad.getX() + 10.0f, ad.getY() + 22.0f, ad.getWidth() - 20.0f, 17.0f }, apex::pDcFilter, tr ("DC offset filter")));
                out.push_back (toggle ({ ad.getX() + 10.0f, ad.getY() + 40.0f, ad.getWidth() - 20.0f, 17.0f }, apex::pUnity, tr ("Unity gain")));
                out.push_back (toggle ({ ad.getX() + 10.0f, ad.getY() + 58.0f, ad.getWidth() - 20.0f, 17.0f }, apex::pAudition, tr ("Audition limiting")));
                auto sc = toggle ({ ad.getX() + 10.0f, ad.getY() + 76.0f, ad.getWidth() - 20.0f, 17.0f }, apex::pSidechain, tr ("Side-chain triggering"));
                sc.hint = tr ("The limiter reacts to the slot's side-chain input (mixer: right-click the slot - Side chain)");
                out.push_back (sc);
                // round 190ar: the levelling stage under the ceiling — off unless switched on here
                auto lv = toggle ({ ad.getX() + 10.0f, ad.getY() + 104.0f, ad.getWidth() - 20.0f, 17.0f }, -1, tr ("Levelling"));
                lv.id = idLevel;
                lv.lit = levelling();
                lv.hint = tr ("Levelling: holds the level at a threshold under the ceiling with a slow attack, the ceiling catches the rest (off by default)");
                out.push_back (lv);
                auto lt = barValue ({ ad.getX() + 6.0f, ad.getY() + 123.0f, ad.getWidth() - 12.0f, 17.0f }, apex::pLevelThreshold, tr ("Threshold"));
                lt.hint = tr ("Levelling threshold: the level held under the ceiling (Off: no levelling)");
                out.push_back (lt);
                auto lat = barValue ({ ad.getX() + 6.0f, ad.getY() + 141.0f, ad.getWidth() - 12.0f, 17.0f }, apex::pLevelAttack, tr ("Attack"));
                lat.hint = tr ("Levelling attack: how much of a transient passes before the level is held");
                out.push_back (lat);
            }
        }
        else // (its caption, CONTROLS, is written up the tab by paintBody)
            out.push_back (button ({ 0.0f, plot().getBottom() - 120.0f, 20.0f, 106.0f }, idPanel, {}, false, tr ("Show the knobs: style, look-ahead, attack, release, channel linking")));
        // the meter scale next to the TP button (bottom right of the display)
        {
            const auto d = plot();
            auto ms = menu ({ d.getRight() - 76.0f, d.getBottom() - 26.0f, 70.0f, 20.0f }, apex::pMeterScale, {}, "-16 dB|-32 dB|-48 dB|K-12|K-14|K-20");
            ms.hint = tr ("Meter scale of the display and the meters");
            out.push_back (ms);
        }
        const auto b = barArea();
        const auto cy = b.getCentreY();
        // round tp172b4: the reference's order — true peak limiting, oversampling, dither (then our noise shaping)
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 160.0f, 22.0f }, apex::pTruePeak, tr ("True peak limiting"), "Off|On"));
        out.push_back (barMenu ({ 176.0f, cy - 11.0f, 140.0f, 22.0f }, apex::pOversampling, tr ("Oversampling"), "Off|2x|4x|8x"));
        out.push_back (barMenu ({ 322.0f, cy - 11.0f, 130.0f, 22.0f }, apex::pDither, tr ("Dither"), "Off|16 bits|18 bits|20 bits|22 bits|24 bits"));
        out.push_back (barMenu ({ 458.0f, cy - 11.0f, 160.0f, 22.0f }, apex::pShaping, tr ("Noise shaping"), "None|Basic|Optimized|Weighted"));
        out.push_back (barValue ({ b.getRight() - 150.0f, cy - 11.0f, 140.0f, 22.0f }, 1, tr ("Out")));
        out.push_back (barValue ({ b.getRight() - 300.0f, cy - 11.0f, 140.0f, 22.0f }, 0, tr ("Gain")));
    }

    juce::Rectangle<float> advanced() const
    {
        // (round 190ar: taller by the levelling rows, and its foot above the display's lower-right strip — display
        // speed, TP, LUFS, meter scale — which the panel used to cover)
        const auto pn = panel();
        return { pn.getRight() + 6.0f, pn.getY() - 96.0f, 170.0f, pn.getHeight() + 66.0f };
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idPanel) panelOpen = ! panelOpen;
        if (c.id == idAdvanced) advancedOpen = ! advancedOpen;
        if (c.id == idLevel)
        {
            if (levelling())
            {
                levelThreshold = v (apex::pLevelThreshold);
                set ({ { apex::pLevelThreshold, 0.0f } });
            }
            else
                set ({ { apex::pLevelThreshold, levelThreshold } });
        }
        if (c.id == idTp) set ({ { 5, v (5) >= 0.5f ? 0.0f : 1.0f } });
        if (c.id == idLoudness) set ({ { apex::pLoudnessShown, loudnessShown() ? 0.0f : 1.0f } });
        if (c.id == idLoudReset) set ({ { apex::pLoudnessReset, v (apex::pLoudnessReset) >= 0.5f ? 0.0f : 1.0f } }, false);
        repaint();
    }

    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (ui::isResetClick (e))
        {
            set ({ { c.param, (float) specFor (c.param).defaultValue } });
            return;
        }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        customDrag (c, e);
    }

    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id == idGain)
        {
            const auto tr_ = gainTrack();
            auto x = (tr_.getBottom() - e.position.y) / tr_.getHeight();
            if (e.mods.isCtrlDown()) x = v (0) + (x - v (0)) * 0.25f;
            set ({ { 0, juce::jlimit (0.0f, 1.0f, x) } });
            ui::showValueTip (*this, gainBox().toNearestInt(), tr ("Gain") + " " + text (0));
        }
        else if (c.id == idCeiling)
        {
            const auto db = juce::jlimit (-30.0f, 0.0f, scaleNow().db (e.position.y, plot()));
            set ({ { 1, (float) (db / 30.0 + 1.0) } });
            ui::showValueTip (*this, juce::Rectangle<int> (60, 16).withCentre (e.position.toInt()), tr ("Out") + " " + text (1));
        }
    }

    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! plot().contains (e.position)) return false;
        const auto spec = specFor (0);
        if (const auto x = ui::wheelValue (v (0), e, w, spec))
        {
            set ({ { 0, (float) *x } });
            ui::flashValueTip (*this, gainBox().toNearestInt(), tr ("Gain") + " " + spec.text (*x));
        }
        return true;
    }

    ui::Hint bodyHint (juce::Point<float> p) override
    {
        ui::Hint h;
        if (plot().contains (p))
            h.text = tr ("Grey: the input after the gain; blue: the output; red from the top: the gain reduction. Wheel: gain");
        return h;
    }

    void tick() override
    {
        const auto& c = columnBack (0);
        if (++holdAge > 60) { outHold = -120.0f; grHold = 0.0f; holdAge = 0; }
        outHold = juce::jmax (outHold, truePeakMeter() && ! demo ? dbOf (liveMeter (3)) : c.out);
        grHold = juce::jmax (grHold, c.gr);
        // the infinite history: one column per 0.5 s (the loudest of its 50 columns)
        for (int guard = 0; seenPos != historyPos && guard < historyLength; ++guard)
        {
            merge (longAcc, history[(std::size_t) seenPos]);
            seenPos = (seenPos + 1) % historyLength;
            if (++longCount >= longSpan)
            {
                longHistory[(std::size_t) longPos] = longAcc;
                longPos = (longPos + 1) % historyLength;
                longAcc = { -120.0f, -120.0f, 0.0f };
                longCount = 0;
            }
        }
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto d = display();
        const auto p = plot();
        displayBackground (g, bodyArea());
        // the history, newest at the right edge
        const auto n = displayMode() == 4 ? 0 : (int) p.getWidth();
        const auto cols = view (n);
        const auto gain = (float) gainDb();
        juce::Path inPath, outPath, grPath, outLine;
        inPath.startNewSubPath (p.getRight(), p.getBottom());
        outPath.startNewSubPath (p.getRight(), p.getBottom());
        grPath.startNewSubPath (p.getRight(), p.getY());
        for (int k = 0; k < n; ++k)
        {
            const auto& c = cols[(std::size_t) k];
            const auto x = p.getRight() - (float) k;
            inPath.lineTo (x, scaleNow().y (c.in + gain, p));
            outPath.lineTo (x, scaleNow().y (c.out, p));
            grPath.lineTo (x, scaleNow().y (-c.gr, p));
            if (k == 0) outLine.startNewSubPath (x, scaleNow().y (c.out, p));
            else outLine.lineTo (x, scaleNow().y (c.out, p));
        }
        inPath.lineTo (p.getX(), p.getBottom());
        outPath.lineTo (p.getX(), p.getBottom());
        grPath.lineTo (p.getX(), p.getY());
        for (auto* path : { &inPath, &outPath, &grPath }) path->closeSubPath();
        fillLevel (g, inPath, inColour(), p, 0.55f, 0.22f);
        fillLevel (g, outPath, outColour(), p, 0.7f, 0.18f);
        g.setColour (colour::textPrimary.withAlpha (0.6f));
        g.strokePath (outLine, juce::PathStrokeType (1.0f)); // (no glow on the 100-column-a-second history: it is redrawn at 30 Hz)
        fillLevel (g, grPath, colour::accent, p, fx.enabled ? 0.8f : 0.25f, fx.enabled ? 0.35f : 0.1f);
        drawScale (g, p, d.getRight() + 6.0f);
        if (n == 0)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::small));
            g.drawText (tr ("Display off"), p.withTrimmedBottom (p.getHeight() * 0.5f), juce::Justification::centred, false);
        }

        // the biggest reductions, tagged (one tag per 1.5 s at most)
        g.setFont (font (type::caption, true));
        for (int k0 = 20; k0 + 150 < n; k0 += 150)
        {
            int best = -1;
            float bestGr = 1.0f;
            for (int k = k0; k < k0 + 150; ++k)
                if (cols[(std::size_t) k].gr > bestGr) { bestGr = cols[(std::size_t) k].gr; best = k; }
            if (best < 0) continue;
            const auto x = p.getRight() - (float) best;
            const auto r = juce::Rectangle<float> (46.0f, 16.0f).withCentre ({ x, scaleNow().y (-bestGr, p) + 12.0f });
            g.setColour (colour::bg3.withAlpha (0.92f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (colour::accent);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (colour::textPrimary);
            g.drawText (juce::String (-bestGr, 1) + " dB", r, juce::Justification::centred, false);
        }

        // the ceiling line (red) with its value at the right
        const auto cy = scaleNow().y ((float) ceilingDb(), p);
        g.setColour (accent.withMultipliedBrightness (0.8f)); // round tp172b4: the reference's dimmer red
        g.drawHorizontalLine ((int) cy, p.getX(), p.getRight());
        g.setFont (font (type::caption, true));
        g.drawText (tr ("Out") + " " + text (1), juce::Rectangle<float> (p.getRight() - 130.0f, cy + 2.0f, 124.0f, 14.0f), juce::Justification::centredRight, false);

        // the GAIN slider on the left edge (round tp172b4: the reference's look — a column of dim red light from the top
        // of the display down to the slider's foot, framed on the left and below, a slate value box)
        const auto tr_ = gainTrack();
        {
            const auto col = juce::Rectangle<float> (tr_.getX() - 2.0f, p.getY() - 14.0f, tr_.getWidth() + 14.0f, tr_.getBottom() + 14.0f - (p.getY() - 14.0f));
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.20f), col.getCentreX(), col.getY(), accent.withAlpha (0.0f), col.getRight() + 6.0f, col.getY(), false));
            g.fillRect (col);
            g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, 0.0f, col.getY(), colour::bg0.withAlpha (0.35f), 0.0f, col.getBottom(), false));
            g.fillRect (col);
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawVerticalLine ((int) col.getX(), col.getY(), col.getBottom());
            g.drawHorizontalLine ((int) col.getBottom(), col.getX(), col.getRight());
        }
        const auto box = gainBox();
        const auto hot = box.expanded (6.0f).contains (mouse) || (dragCtl == -2 && pressed.id == idGain);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff6a7392 : 0xff5d6681), 0.0f, box.getY(), juce::Colour (hot ? 0xff4f5672 : 0xff474e66), 0.0f, box.getBottom(), false));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (hot ? accent : juce::Colour (0xff2a2d3a));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small, true));
        g.drawText ((gainDb() >= 0.05 ? "+" : "") + juce::String (gainDb(), 1), box, juce::Justification::centred, false);
        caption (g, juce::Rectangle<float> (tr_.getX() - 10.0f, tr_.getBottom() + 18.0f, 54.0f, 14.0f), tr ("Gain"));

        // the meters: output and gain reduction
        const auto m = juce::Rectangle<float> (d.getRight() + 46.0f, p.getY(), 52.0f, p.getHeight());
        const auto outR = m.withWidth (22.0f);
        const auto grR = m.withTrimmedLeft (28.0f);
        const auto newest = columnBack (0);
        const auto outY = scaleNow().y (truePeakMeter() && ! demo ? dbOf (liveMeter (3)) : newest.out, p);
        skin::meterSolid (g, outR, (outR.getBottom() - outY) / outR.getHeight(), outColour(), false);
        skin::meterSolid (g, grR, (scaleNow().y (-newest.gr, p) - grR.getY()) / grR.getHeight(), colour::accent, true);
        g.setColour (colour::bg0.withAlpha (0.5f));
        for (auto y = m.getY() + 3.0f; y < m.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, m.getX(), m.getRight());
        g.setFont (font (type::caption, true));
        g.setColour (colour::ledOn);
        g.drawText (outHold > -100.0f ? juce::String (outHold, 1) : "-inf", outR.withY (d.getY()).withHeight (14.0f).expanded (6.0f, 0.0f), juce::Justification::centred, false);
        g.setColour (colour::accent);
        g.drawText (juce::String (-grHold, 1), grR.withY (d.getY()).withHeight (14.0f).expanded (6.0f, 0.0f), juce::Justification::centred, false);

        // the knob panel
        if (panelOpen)
        {
            const auto pn = panel();
            drawPanel (g, pn);
            caption (g, { pn.getX() + 16.0f, pn.getY() + 8.0f, 96.0f, 14.0f }, tr ("Style"));
            caption (g, { pn.getX() + 340.0f, pn.getY() + 8.0f, 176.0f, 14.0f }, tr ("Channel linking"));
            g.setColour (colour::border);
            g.drawVerticalLine ((int) pn.getX() + 116, pn.getY() + 10.0f, pn.getBottom() - 10.0f);
            g.drawVerticalLine ((int) pn.getX() + 336, pn.getY() + 10.0f, pn.getBottom() - 10.0f);
            if (advancedOpen)
            {
                const auto ad = advanced();
                drawPanel (g, ad);
                caption (g, { ad.getX(), ad.getY() + 6.0f, ad.getWidth(), 14.0f }, tr ("Advanced"));
                g.setColour (colour::border); // (round 190ar: the levelling rows under the four switches)
                g.drawHorizontalLine ((int) ad.getY() + 98, ad.getX() + 10.0f, ad.getRight() - 10.0f);
            }
        }
        // latency (constant: the maximum look-ahead, + the oversampler, + the true-peak guard with True peak on)
        {
            // on a dark plate at the top left of the display, right of the GAIN slider
            const auto lr = juce::Rectangle<float> (p.getX() + 64.0f, p.getY() + 2.0f, 132.0f, 15.0f);
            g.setColour (colour::textDisabled); // round tp172b4: plain dim text (the reference has no plate there)
            g.setFont (font (type::caption));
            g.drawText (tr ("latency {0} samples", { juce::String (apex::latencyFor (48000.0, apex::oversamplingFactor (v (apex::pOversampling)), v (apex::pTruePeak) >= 0.5f)) }),
                        lr.reduced (5.0f, 0.0f), juce::Justification::centredLeft, true);
        }
        if (loudnessShown()) paintLoudness (g);
    }

    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        if (! panelOpen) // the folded knob panel's tab: CONTROLS written up it, a » at its top
        {
            Ctl tab;
            for (const auto& c : controls())
                if (c.type == K::button && c.id == idPanel) tab = c;
            if (! tab.r.isEmpty())
            {
                g.setColour (colour::textSecondary);
                g.setFont (font (type::small, true));
                g.drawText (juce::String::fromUTF8 ("\xc2\xbb"), tab.r.withHeight (18.0f), juce::Justification::centred, false);
                g.saveState();
                g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, tab.r.getCentreX(), tab.r.getCentreY() + 8.0f));
                g.setFont (font (type::caption, true));
                g.drawText (caps (tr ("Controls")), juce::Rectangle<float> (tab.r.getHeight() - 22.0f, 14.0f).withCentre ({ tab.r.getCentreX(), tab.r.getCentreY() + 8.0f }),
                            juce::Justification::centred, false);
                g.restoreState();
            }
        }
    }

    // the loudness panel (round 135): short-term and momentary bars with the target line, integrated / range / peak
    void paintLoudness (juce::Graphics& g)
    {
        const auto la = loudnessArea();
        drawPanel (g, la);
        caption (g, { la.getX() + 6.0f, la.getY() + 7.0f, 90.0f, 14.0f }, tr ("Loudness"), juce::Justification::centredLeft);
        auto lufs = [this] (int k, float demoValue) { return demo ? demoValue : liveMeter (k); };
        const auto momentary = lufs (1, -11.8f), shortTerm = lufs (4, -12.6f), integrated = lufs (5, -13.1f), range = lufs (6, 5.4f), peak = lufs (7, -0.3f);
        const auto target = (float) apex::loudnessTarget (v (apex::pLoudnessTarget));
        // the bars: −48 … 0 LUFS
        const auto bars = juce::Rectangle<float> (la.getX() + 10.0f, la.getY() + 34.0f, 64.0f, la.getHeight() - 34.0f - 64.0f);
        auto bar = [&] (juce::Rectangle<float> r, float l, const juce::String& name) {
            const auto f = juce::jlimit (0.0f, 1.0f, (l + 48.0f) / 48.0f);
            skin::meterSolid (g, r.withTrimmedBottom (14.0f), f, l > target + 1.0f ? colour::accent : outColour(), false);
            caption (g, r.withTop (r.getBottom() - 13.0f), name);
        };
        bar (bars.withWidth (26.0f), shortTerm, "S");
        bar (bars.withTrimmedLeft (34.0f).withWidth (26.0f), momentary, "M");
        const auto meterH = bars.getHeight() - 14.0f;
        const auto ty = bars.getY() + meterH * (1.0f - juce::jlimit (0.0f, 1.0f, (target + 48.0f) / 48.0f));
        g.setColour (colour::textPrimary.withAlpha (0.8f));
        g.drawHorizontalLine ((int) ty, bars.getX() - 3.0f, bars.getRight() + 3.0f);
        g.setFont (font (type::caption));
        g.setColour (colour::textDisabled);
        for (int m = 0; m >= -48; m -= 12)
            g.drawText (juce::String (m), juce::Rectangle<float> (bars.getRight() + 2.0f, bars.getY() + meterH * (float) -m / 48.0f - 6.0f, 22.0f, 12.0f), juce::Justification::centredLeft, false);
        // the read-outs
        auto readout = [&] (float y, const juce::String& name, float value, const juce::String& unit, bool big) {
            const auto x = bars.getRight() + 26.0f, w = la.getRight() - x - 6.0f;
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawText (name, juce::Rectangle<float> (x, y, w, 12.0f), juce::Justification::centredLeft, false);
            g.setColour (colour::textPrimary);
            g.setFont (font (big ? type::title : type::small, true));
            g.drawText (value <= -100.0f ? juce::String ("-inf") : juce::String (value, 1) + " " + unit, juce::Rectangle<float> (x, y + 12.0f, w, big ? 22.0f : 16.0f),
                        juce::Justification::centredLeft, false);
        };
        auto y = bars.getY();
        readout (y, tr ("Integrated"), integrated, "LUFS", true); y += 40.0f;
        readout (y, tr ("Short term"), shortTerm, "LUFS", false); y += 32.0f;
        readout (y, tr ("Momentary"), momentary, "LUFS", false); y += 32.0f;
        readout (y, tr ("Range"), range, "LU", false); y += 32.0f;
        readout (y, truePeakMeter() ? tr ("True peak") : tr ("Peak"), peak, truePeakMeter() ? "dBTP" : "dB", false);
    }

    void paintCustom (juce::Graphics&, const Ctl&) override {}

    // the dB marks of the chosen meter scale (K scales are labelled relative to their 0 VU)
    void drawScale (juce::Graphics& g, juce::Rectangle<float> p, float labelX) const
    {
        const auto sc = scaleNow();
        const auto k = kOffset();
        std::vector<float> marks;
        switch (meterScale())
        {
            case 0: marks = { 0.0f, -2.0f, -4.0f, -6.0f, -9.0f, -12.0f, -16.0f }; break;
            case 1: marks = { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -18.0f, -24.0f, -32.0f }; break;
            case 2: marks = { 0.0f, -3.0f, -6.0f, -9.0f, -12.0f, -15.0f, -24.0f, -36.0f }; break;
            default: for (float m = 8.0f; m >= -36.0f; m -= (m > -12.0f ? 4.0f : 12.0f)) marks.push_back (m - k); break;
        }
        g.setFont (font (type::caption));
        for (const auto m : marks)
        {
            const auto y = sc.y (m, p);
            g.setColour (colour::gridBeat);
            g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
            g.setColour (colour::textDisabled);
            const auto label = k > 0.0f ? (m + k > 0.0f ? "+" : "") + juce::String ((int) std::lround (m + k)) : (m == 0.0f ? juce::String ("0 dB") : juce::String ((int) m));
            g.drawText (label, juce::Rectangle<float> (labelX, y - 7.0f, 40.0f, 14.0f), juce::Justification::centredLeft, false);
        }
    }
};

// =============================================================================================================
// Hush De-esser — the reference de-esser's workflow: the scrolling waveform on top with the reduction marked in
// red and the threshold line, the big THRESHOLD and RANGE knobs with the listen button, the frequency slider with
// the detection region under them, the mode buttons in the middle, release / output on the right, the input and
// reduction meters at the right edge.
class HushEditor final : public ProEditor
{
public:
    // round 190dm: + a row for Air and Mix (500 → 596)
    HushEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::hushDeEsser, d, 720, 596)
    {
        knobRanges = true; // round tp172b4: each knob's scale under it, the value on hover
        fillDemo();
        if (! displayShown()) setSize (720, 596 - (int) waveH);
    }

private:
    enum { idThreshold = 1, idFreq, idMeterThreshold, idDisplay, idExternal, idOutMeter, idGrMeter, idLink };
    float grHoldDb = 0.0f;     // round 144: the attenuation meter's infinite peak hold (DeEsser's Attn meter), click to reset
    float outPeakDb = -100.0f; // round 135w: the output meter's highest peak since the last click (DeEsser's read-out)
    static constexpr float waveH = 194.0f; // the waveform display (DISPLAY hides it and the window shrinks)
    bool displayShown() const { return v (P ("Display")) >= 0.5f; }
    // which handle of the frequency slider is being dragged: 0 = high-pass (Frequency), 1 = low-pass
    int dragHandle = 0;
    static float freqPos (double hz) { return (float) (std::log (hz / 2000.0) / std::log (10.0)); } // 2 .. 20 kHz
    // round 165x: the extended law's high-pass reaches 20 kHz (the old law and the classic voicing: 16 kHz)
    bool ext() const { return v (P ("Range law")) >= 0.5f && v (P ("Voicing")) < 0.5f; }
    double hpHz() const { return mixsuite::hush::hpHz (v (pFreq()), ext()); }
    float hpPos() const { return freqPos (hpHz()); }
    // round 165x: the 0 … 200 % link knob as one 0 … 1 position (Stereo link fills the first half, Link only the second)
    float linkPos() const { return 0.5f * (v (P ("Stereo link")) + (v (P ("Stereo link")) >= 0.999f ? v (P ("Link only")) : 0.0f)); }
    void setLinkPos (double u)
    {
        u = juce::jlimit (0.0, 1.0, u);
        set ({ { P ("Stereo link"), (float) juce::jmin (1.0, 2.0 * u) }, { P ("Link only"), (float) juce::jmax (0.0, 2.0 * u - 1.0) } });
    }
    juce::String linkText() const
    {
        const auto u = linkPos();
        if (u <= 0.5f)
            return juce::String (juce::roundToInt (200.0f * u)) + " %";
        return "100 %, " + juce::String (juce::roundToInt (200.0f * u - 100.0f)) + " % " + (v (P ("Link only side")) >= 0.5f ? tr ("side only") : tr ("mid only"));
    }
    ui::ValueSpec linkSpec() const
    {
        auto spec = ui::percentSpec (tr ("Stereo link"), 0.5);
        spec.toText = [] (double u) { return u <= 0.5 ? juce::String (juce::roundToInt (200.0 * u)) + " %" : juce::String (juce::roundToInt (200.0 * u)) + " % (100 % + " + juce::String (juce::roundToInt (200.0 * u - 100.0)) + " %)"; };
        spec.fromText = ui::inverseOf (spec.toText, 0.0, 1.0);
        return spec;
    }
    float lpPos() const { return v (P ("Low-pass freq.")) >= 0.999f ? 1.0f : freqPos (mixsuite::hush::lowPassHz (v (P ("Low-pass freq.")))); }

    juce::String title() const override { return "Hush De-esser"; }
    juce::String gestures() const override { return tr ("Drag the threshold line or the meter's arrow  -  drag the frequency slider") ; }
    bool usesHistory() const override { return true; }
    float grDb() const override { return liveMeter (0) * 30.0f; }

    int pFreq() const { return P ("Frequency"); }
    int pThr() const { return P ("Threshold"); }
    juce::Rectangle<float> wave() const
    {
        // round 135w: 142 px on the right for the IN / GR / OUT meters and their scale (was 112: IN / GR)
        return displayShown() ? bodyArea().withTrimmedRight (142.0f).withHeight (190.0f).reduced (0.0f, 4.0f)
                              : bodyArea().withTrimmedRight (142.0f).withHeight (0.0f).translated (0.0f, -4.0f);
    }
    // round 135w: the input meter moved left to make room for the output meter (In | GR | Out)
    juce::Rectangle<float> meters() const { return { (float) getWidth() - 100.0f, headerH + 18.0f, 26.0f, bodyArea().getHeight() - 26.0f }; }
    juce::Rectangle<float> outMeter() const { return meters().translated (56.0f, 0.0f).withWidth (16.0f).withTrimmedTop (12.0f).withTrimmedBottom (16.0f); }
    juce::Rectangle<float> freqTrack() const { return { 34.0f, wave().getBottom() + 182.0f, 250.0f, 24.0f }; }

    double thresholdDb() const { return mixsuite::hush::thresholdDb (v (pThr())); }

    // mirrored waveform: the height follows the amplitude (linear), like a waveform view; 0 dBFS at the edges
    float waveY (float db, bool upper) const
    {
        const auto a = wave();
        const auto f = juce::jlimit (0.0f, 1.0f, std::pow (10.0f, db / 20.0f));
        return upper ? a.getCentreY() - a.getHeight() * 0.48f * f : a.getCentreY() + a.getHeight() * 0.48f * f;
    }
    float meterY (float db) const
    {
        const auto m = meters();
        return m.getBottom() - m.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    }

    Column demoColumn (int i) const override
    {
        // sibilant bursts every 0.8 s over speech
        const auto t = (double) i * 0.01;
        const auto s = std::fmod (t, 0.8);
        const auto in = demoInDb (i) - 4.0f + (float) (s > 0.42 && s < 0.56 ? 7.0 : 0.0);
        const auto over = in - (float) thresholdDb();
        const auto gr = juce::jlimit (0.0f, (float) mixsuite::hush::rangeDb (v (P ("Range"))), over > 0.0f ? over * 0.8f : 0.0f);
        return { in, in - gr, gr };
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        const auto w = wave();
        const auto y0 = w.getBottom() + 10.0f;
        // threshold line on the waveform (both halves)
        if (displayShown())
        {
            out.push_back (custom ({ w.getX(), waveY ((float) thresholdDb(), true) - 5.0f, w.getWidth(), 10.0f }, idThreshold, pThr(), tr ("Threshold: drag up or down")));
            out.push_back (custom ({ w.getX(), waveY ((float) thresholdDb(), false) - 5.0f, w.getWidth(), 10.0f }, idThreshold, pThr(), tr ("Threshold: drag up or down")));
        }
        auto thr = knob ({ 44.0f, y0 + 4.0f, 110.0f, 142.0f }, pThr(), tr ("Threshold"), 2);
        thr.colour = colour::warning;
        out.push_back (thr);
        auto rng = knob ({ 170.0f, y0 + 4.0f, 110.0f, 142.0f }, P ("Range"), tr ("Range"), 2);
        rng.colour = colour::warning;
        out.push_back (rng);
        out.push_back (custom (freqTrack().expanded (0.0f, 4.0f), idFreq, pFreq(), tr ("Drag the left handle: where the band starts (high-pass); the right one: where it ends (low-pass, far right = off)")));
        auto aud = toggle ({ freqTrack().getCentreX() - 50.0f, freqTrack().getBottom() + 8.0f, 100.0f, 22.0f }, P ("Listen"), tr ("Audition"));
        aud.hint = tr ("Listen: hear only what the de-esser takes away (the detection band)");
        out.push_back (aud);
        // middle: display, mode, processing, side chain
        const auto mx = 304.0f;
        // round tp172b4: a dark tab, as the reference's (the display above shows whether it is on)
        out.push_back (button ({ mx + 40.0f, y0 - 2.0f, 110.0f, 20.0f }, idDisplay, tr ("Display"), false, tr ("Show or hide the scrolling display")));
        auto md = tabs ({ mx, y0 + 40.0f, 190.0f, 28.0f }, P ("Allround"), "Single vocal|Allround", { 0.0f, 1.0f });
        md.hint = tr ("Single vocal: one voice; Allround: a mix or other material (the sibilance must also stand out from the whole signal)");
        out.push_back (md);
        out.push_back (tabs ({ mx, y0 + 96.0f, 190.0f, 28.0f }, P ("Wideband"), "Wide band|Split band", { 1.0f, 0.0f }));
        out.push_back (custom ({ mx + 90.0f, y0 + 148.0f, 100.0f, 26.0f }, idExternal, P ("External SC"), tr ("Side chain: detect this signal (In) or another track (Ext)")));
        // right: stereo link (with MID) and look-ahead (with its power button)
        if (ext())
        {
            // round 165x: the reference's link knob runs 0 … 200 %: up to 100 % the channels share their detection (100 % =
            // the mid), past it a share of the processing acts on the mid (or side) signal only — one knob over Stereo link
            // and Link only
            out.push_back (custom ({ 512.0f, y0 + 2.0f, 84.0f, 90.0f }, idLink, P ("Stereo link"),
                                   tr ("Stereo link: up to 100 % the channels share their detection; past 100 % a share of the processing acts on the mid (or side) signal only")));
            auto ms = tabs ({ 516.0f, y0 + 96.0f, 76.0f, 18.0f }, P ("Link only side"), "Mid|Side", { 0.0f, 1.0f });
            ms.hint = tr ("Which signal the processing past 100 % link acts on alone: the mid or the side");
            out.push_back (ms);
        }
        else
        {
            auto link = knob ({ 512.0f, y0 + 2.0f, 84.0f, 90.0f }, P ("Stereo link"), tr ("Stereo link"));
            link.cap = juce::Colour (0xff53524f); // round tp172b4: the reference's small knobs are grey
            out.push_back (link);
            auto mid = toggle ({ 522.0f, y0 + 96.0f, 64.0f, 18.0f }, P ("Link mid"), "MID");
            mid.hint = tr ("Mid: detect on the middle of the stereo image only");
            out.push_back (mid);
        }
        auto lat = knob ({ 512.0f, y0 + 120.0f, 84.0f, 90.0f }, P ("Lookahead time"), tr ("Lookahead"));
        lat.cap = juce::Colour (0xff53524f);
        out.push_back (lat);
        auto la = toggle ({ 522.0f, y0 + 214.0f, 64.0f, 18.0f }, P ("Lookahead"), tr ("On"));
        la.hint = tr ("Look-ahead on / off");
        out.push_back (la);
        // round 190dm: Air (the high shelf on the processed signal) and Mix (dry / processed), beside the output meters
        auto air = knob ({ 420.0f, y0 + 244.0f, 84.0f, 90.0f }, P ("Air"), tr ("Air"));
        air.cap = juce::Colour (0xff53524f);
        out.push_back (air);
        auto mix = knob ({ 512.0f, y0 + 244.0f, 84.0f, 90.0f }, P ("Mix"), tr ("Mix"));
        mix.cap = juce::Colour (0xff53524f);
        out.push_back (mix);
        // the meter's threshold arrow
        out.push_back (custom ({ meters().getX() - 12.0f, meterY ((float) thresholdDb()) - 7.0f, 12.0f, 14.0f }, idMeterThreshold, pThr(), tr ("Threshold: drag up or down")));
        // round 135w: the output meter (−30 … 0 dBFS, the reference's), its peak read-out and clip lamp: click to reset
        // round 144: the reduction meter's held peak: click to reset (the reference's Attn meter)
        out.push_back (custom (meters().translated (32.0f, 0.0f).withWidth (14.0f).expanded (3.0f, 0.0f), idGrMeter, -1,
                               tr ("Gain reduction (dB); the number and the line hold the deepest reduction; click to reset")));
        out.push_back (custom (outMeter().expanded (4.0f, 28.0f), idOutMeter, -1, tr ("Output level (peak, dBFS): the lamp lights when the output clips, the number holds the highest peak; click to reset")));
        const auto b = barArea();
        const auto cy = b.getCentreY();
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 130.0f, 22.0f }, P ("Band detector"), tr ("Detection"), "High-pass|Band", { 0.0f, 1.0f }));
        out.push_back (barValue ({ 146.0f, cy - 11.0f, 110.0f, 22.0f }, P ("Release"), tr ("Release")));
        // round 135: oversampling (the reference's bottom bar; Hush's own voicing)
        out.push_back (barMenu ({ 262.0f, cy - 11.0f, 112.0f, 22.0f }, P ("Oversampling"), tr ("Oversampling"), "Off|2x|4x|8x"));
        // round 148: the measured single-vocal detector (Vocal voicing)
        auto vocal = barMenu ({ 380.0f, cy - 11.0f, 118.0f, 22.0f }, P ("Vocal voicing"), tr ("Detector"), "Hush|Vocal", { 0.0f, 1.0f });
        vocal.hint = tr ("Vocal: the measured single-vocal detector (sibilants that stand out from the voice)");
        out.push_back (vocal);
        out.push_back (barValue ({ b.getRight() - 214.0f, cy - 11.0f, 100.0f, 22.0f }, P ("Input"), tr ("In")));
        out.push_back (barValue ({ b.getRight() - 108.0f, cy - 11.0f, 100.0f, 22.0f }, P ("Output"), tr ("Out")));
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idDisplay)
        {
            const auto show = ! displayShown();
            set ({ { P ("Display"), show ? 1.0f : 0.0f } }, false);
            setSize (720, show ? 596 : 596 - (int) waveH);
        }
        repaint();
    }

    void showSideChainMenu()
    {
        const auto* fx = current();
        if (fx == nullptr) return;
        juce::PopupMenu menu;
        const auto ext = v (P ("External SC")) >= 0.5f;
        menu.addItem (tr ("In: detect this signal"), true, ! ext, [this] { set ({ { P ("External SC"), 0.0f } }); });
        juce::PopupMenu from;
        const auto assign = [this] (int source) {
            const auto p = P ("External SC");
            engine.editSong ([tk = track, sl = slot, source, p] (Song& s) {
                // round 177b: the pick makes the route (a level-0 send) as the mixer's picker does; a loop changes nothing
                if (mixerops::sidechainFrom (s, tk, sl, source) == mixerops::SidechainPick::cycle)
                    return;
                auto& target = s.mixer[(std::size_t) tk].effects[(std::size_t) sl];
                target.params[(std::size_t) p] = source >= 0 ? 1.0f : 0.0f;
            });
        };
        const auto& song = engine.getSong();
        for (int t = 1; t < (int) song.mixer.size(); ++t)
            if (t != track)
                from.addItem (juce::String (t) + " " + juce::String::fromUTF8 (song.mixer[(std::size_t) t].name.c_str()), true, ext && fx->sidechain == t, [assign, t] { assign (t); });
        menu.addSubMenu (tr ("Ext: side chain from"), from);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id == idOutMeter)
        {
            outPeakDb = -100.0f;
            repaint();
            return;
        }
        if (c.id == idGrMeter) // round 144
        {
            grHoldDb = 0.0f;
            repaint();
            return;
        }
        if (c.id == idLink) // round 165x: the 0 … 200 % link knob
        {
            if (ui::isResetClick (e))
                setLinkPos (0.5);
            else if (e.mods.isPopupMenu())
                ui::showValueMenu (*this, nullptr, linkSpec(), linkPos(), [this] (double x) { setLinkPos (x); });
            else
                valueDrag.begin (e, linkPos());
            return;
        }
        if (c.id == idExternal)
        {
            const auto* fx = current();
            const auto ext = e.position.x >= c.r.getCentreX();
            if (ext && (fx == nullptr || fx->sidechain < 0 || e.mods.isPopupMenu())) showSideChainMenu();
            else set ({ { c.param, ext ? 1.0f : 0.0f } });
            return;
        }
        if (c.id == idFreq)
        {
            const auto x = (e.position.x - freqTrack().getX()) / freqTrack().getWidth();
            dragHandle = std::abs (x - lpPos()) < std::abs (x - hpPos()) ? 1 : 0;
        }
        if (ui::isResetClick (e))
        {
            const auto p = c.id == idFreq && dragHandle == 1 ? P ("Low-pass freq.") : c.param;
            set ({ { p, (float) specFor (p).defaultValue } });
            return;
        }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        customDrag (c, e);
    }

    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id == idOutMeter || c.id == idGrMeter)
            return;
        if (c.id == idLink)
        {
            if (valueDrag.isActive())
            {
                setLinkPos (valueDrag.update (e, linkSpec()));
                ui::showValueTip (*this, c.r.toNearestInt(), linkText());
            }
            return;
        }
        if (c.id == idFreq)
        {
            const auto tr_ = freqTrack();
            const auto x = juce::jlimit (0.0, 1.0, (double) ((e.position.x - tr_.getX()) / tr_.getWidth()));
            const auto hz = 2000.0 * std::pow (10.0, x);
            if (dragHandle == 1)
            {
                const auto lp = P ("Low-pass freq.");
                set ({ { lp, x >= 0.985 ? 1.0f : invert ([] (float y) { return mixsuite::hush::lowPassHz (y); }, juce::jmax (hz, hpHz())) } });
                ui::showValueTip (*this, tr_.toNearestInt(), tr ("Low pass") + " " + text (lp));
            }
            else
            {
                set ({ { pFreq(), mixsuite::hush::hpValue (juce::jmin (hz, ext() ? 20000.0 : 16000.0), ext()) } });
                ui::showValueTip (*this, tr_.toNearestInt(), tr ("Frequency") + " " + text (pFreq()));
            }
            return;
        }
        float db;
        if (c.id == idMeterThreshold) db = (meters().getBottom() - e.position.y) / meters().getHeight() * 60.0f - 60.0f;
        else
        {
            const auto a = wave();
            const auto f = std::abs (e.position.y - a.getCentreY()) / (a.getHeight() * 0.48f);
            db = 20.0f * std::log10 (juce::jlimit (0.001f, 1.0f, f));
        }
        set ({ { pThr(), invert ([] (float x) { return mixsuite::hush::thresholdDb (x); }, juce::jlimit (-60.0f, 0.0f, db)) } });
        ui::showValueTip (*this, juce::Rectangle<int> (80, 16).withCentre (e.position.toInt()), tr ("Threshold") + " " + text (pThr()));
    }

    ui::Hint bodyHint (juce::Point<float> p) override
    {
        ui::Hint h;
        if (wave().contains (p))
            h.text = tr ("The voice scrolls by; red: what the de-esser takes away. Drag the dashed line to set the threshold");
        return h;
    }

    void paintCustom (juce::Graphics& g, const Ctl& c) override
    {
        if (c.id == idFreq)
        {
            const auto tr_ = freqTrack();
            const auto band = v (P ("Band detector")) >= 0.5f;
            const auto x1 = tr_.getX() + tr_.getWidth() * juce::jlimit (0.0f, 1.0f, hpPos());
            const auto x2 = tr_.getX() + tr_.getWidth() * juce::jlimit (0.0f, 1.0f, lpPos());
            g.setColour (colour::bg1);
            g.fillRoundedRectangle (tr_, 4.0f);
            // the detection region: between the two handles (band mode: a band around the left one, cut at the right one)
            g.setColour (colour::warning.withAlpha (0.35f));
            const auto left = band ? x1 - 34.0f : x1;
            const auto right = band ? juce::jmin (x1 + 34.0f, x2) : x2;
            g.fillRect (juce::Rectangle<float> (left, tr_.getY() + 2.0f, juce::jmax (1.0f, right - left), tr_.getHeight() - 4.0f).getIntersection (tr_));
            g.setColour (colour::borderStrong);
            g.drawRoundedRectangle (tr_, 4.0f, 1.0f);
            const auto hot = c.r.contains (mouse) || (dragCtl == -2 && pressed.id == idFreq);
            for (const auto [x, k] : { std::pair<float, int> { x1, 0 }, std::pair<float, int> { x2, 1 } })
            {
                juce::Path handle;
                handle.addTriangle (x - 9.0f, tr_.getBottom() - 1.0f, x + 9.0f, tr_.getBottom() - 1.0f, x, tr_.getY() + 2.0f);
                g.setColour (hot && dragHandle == k ? colour::textPrimary : colour::textSecondary);
                g.fillPath (handle);
            }
            g.setFont (font (type::small, true));
            g.setColour (colour::warning);
            g.drawText (text (pFreq()), juce::Rectangle<float> (tr_.getX() - 6.0f, tr_.getBottom() + 10.0f, 70.0f, 14.0f), juce::Justification::centredLeft, false);
            const auto lpOff = v (P ("Low-pass freq.")) >= 0.999f;
            g.drawText (lpOff ? juce::String ("20 kHz") : text (P ("Low-pass freq.")), juce::Rectangle<float> (tr_.getRight() - 64.0f, tr_.getBottom() + 10.0f, 70.0f, 14.0f),
                        juce::Justification::centredRight, false);
        }
        else if (c.id == idLink)
        {
            const auto d = juce::jmin (c.r.getWidth() - 10.0f, c.r.getHeight() - 34.0f);
            const auto knobR = juce::Rectangle<float> (c.r.getCentreX() - d / 2.0f, c.r.getY() + 2.0f, d, d);
            const auto hover = c.r.contains (mouse), active = valueDrag.isActive() && pressed.id == idLink;
            skin::KnobLook look;
            look.value = linkPos();
            look.accent = accent;
            look.hover = hover ? 1.0f : 0.0f;
            look.active = active;
            look.enabled = current() == nullptr || current()->enabled;
            look.capTint = juce::Colour (0xff53524f); // round tp172b4: grey, as the other small knob
            skin::knob (g, knobR, look);
            // round tp172b4: its scale (0 … 200 %) under it, the value while hovered or dragged, then the caption
            const auto row = knobR.withY (knobR.getBottom() + 1.0f).withHeight (12.0f);
            if (hover || active)
            {
                g.setColour (skin::valueColour (true));
                g.setFont (font (type::caption, true));
                g.drawText (linkText(), row.expanded (40.0f, 0.0f), juce::Justification::centred, false);
            }
            else
            {
                g.setColour (colour::textDisabled);
                g.setFont (font (type::caption));
                g.drawText ("0 %", row.withWidth (row.getWidth() * 0.5f + 10.0f).translated (-10.0f, 0.0f), juce::Justification::centredLeft, false);
                g.drawText ("200 %", row.withTrimmedLeft (row.getWidth() * 0.5f - 10.0f).translated (10.0f, 0.0f), juce::Justification::centredRight, false);
            }
            g.setColour (skin::captionColour().interpolatedWith (colour::textPrimary, hover ? 0.5f : 0.0f));
            g.setFont (font (type::caption, true));
            g.drawText (caps (tr ("Stereo link")), juce::Rectangle<float> (c.r.getX() - 14.0f, knobR.getBottom() + 14.0f, c.r.getWidth() + 28.0f, 14.0f),
                        juce::Justification::centred, false);
        }
        else if (c.id == idExternal)
        {
            const auto ext = v (c.param) >= 0.5f;
            const auto half = c.r.withWidth (c.r.getWidth() / 2.0f);
            skin::button (g, half, ! ext, half.contains (mouse), accent, radius::sm);
            skin::button (g, half.translated (half.getWidth(), 0.0f), ext, half.translated (half.getWidth(), 0.0f).contains (mouse), accent, radius::sm);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small, true));
            g.drawText (tr ("In"), half, juce::Justification::centred, false);
            g.drawText (tr ("Ext"), half.translated (half.getWidth(), 0.0f), juce::Justification::centred, false);
            caption (g, { c.r.getX() - 92.0f, c.r.getY(), 88.0f, c.r.getHeight() }, tr ("Side chain"), juce::Justification::centredRight);
        }
        else if (c.id == idMeterThreshold)
        {
            juce::Path a;
            const auto r = c.r;
            a.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getCentreY());
            g.setColour (colour::warning);
            g.fillPath (a);
        }
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto w = wave();
        displayBackground (g, bodyArea());
        if (displayShown())
            paintWave (g, fx, w);
        paintControlsArea (g, w);
    }

    void paintWave (juce::Graphics& g, const EffectSlot& fx, juce::Rectangle<float> w)
    {
        // round tp172b4: the waveform lies on the field (the reference's display has no well), faint level lines across
        g.setColour (juce::Colours::white.withAlpha (0.045f));
        for (const auto db : { -2.0f, -6.0f, -12.0f, -24.0f })
            for (const auto upper : { true, false })
                g.drawHorizontalLine ((int) waveY (db, upper), w.getX(), w.getRight());
        g.setColour (colour::gridBeat);
        g.drawHorizontalLine ((int) w.getCentreY(), w.getX(), w.getRight());
        const auto n = (int) w.getWidth();
        juce::Path inP, outP;
        for (int k = 0; k < n; ++k)
        {
            const auto& c = columnBack (k);
            const auto x = w.getRight() - (float) k;
            inP.addRectangle (x, waveY (c.in, true), 1.0f, juce::jmax (1.0f, waveY (c.in, false) - waveY (c.in, true)));
            outP.addRectangle (x, waveY (c.out, true), 1.0f, juce::jmax (1.0f, waveY (c.out, false) - waveY (c.out, true)));
        }
        g.setColour (colour::accent.withAlpha (fx.enabled ? 0.8f : 0.25f)); // the reduction = input − output
        g.fillPath (inP);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.95f), 0.0f, w.getY(), juce::Colour (0xffc8ccd6).withAlpha (0.9f), 0.0f, w.getCentreY(), true));
        if (! outP.isEmpty()) g.fillPath (outP); // (round 170c: an empty path with a gradient flooded macOS editors)
        // threshold lines
        const auto dashes = std::array<float, 2> { 6.0f, 4.0f };
        g.setColour (colour::warning.withAlpha (0.9f));
        for (const auto upper : { true, false })
        {
            const auto y = waveY ((float) thresholdDb(), upper);
            g.drawDashedLine (juce::Line<float> (w.getX(), y, w.getRight(), y), dashes.data(), 2, 1.2f);
        }
        g.setFont (font (type::caption, true));
        g.drawText (tr ("Threshold") + " " + text (pThr()), juce::Rectangle<float> (w.getX() + 6.0f, waveY ((float) thresholdDb(), true) - 16.0f, 160.0f, 14.0f),
                    juce::Justification::centredLeft, false);
    }

    void paintControlsArea (juce::Graphics& g, juce::Rectangle<float> w)
    {
        // the control area
        // round tp172b4: the knobs sit on the field itself (no card), as in the reference
        caption (g, { 304.0f, w.getBottom() + 34.0f, 190.0f, 14.0f }, tr ("Mode"));
        caption (g, { 304.0f, w.getBottom() + 90.0f, 190.0f, 14.0f }, tr ("Processing"));

        // meters: input level and reduction, with the dB scale between them
        const auto m = meters();
        const auto newest = columnBack (0);
        drawMeter (g, m, newest.in, -60.0f, colour::meterLow.withAlpha (0.85f));
        // round 147: the detector's energy (what the threshold is compared with, DeEsser's Energy Detector) as a
        // blue bar inside the input meter, against the same scale as the threshold arrow
        if (const auto det = liveMeter (1); det > 0.0f)
        {
            const auto y = juce::jlimit (m.getY(), m.getBottom(), meterY (det * 60.0f - 60.0f));
            g.setColour (juce::Colour (0xff4aa3df).withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (m.getCentreX() - 3.0f, y, 6.0f, m.getBottom() - y));
        }
        const auto grR = m.translated (32.0f, 0.0f).withWidth (14.0f);
        g.setColour (colour::bg1);
        g.fillRect (grR);
        g.setColour (colour::accent);
        g.fillRect (grR.withHeight (grR.getHeight() * juce::jlimit (0.0f, 1.0f, newest.gr / 24.0f)));
        // round 144: the held peak (a line on the bar and the number under it), as the reference's Attn meter
        grHoldDb = juce::jmax (grHoldDb, newest.gr);
        if (grHoldDb > 0.05f)
        {
            g.setColour (colour::textPrimary);
            g.fillRect (grR.withY (grR.getY() + grR.getHeight() * juce::jlimit (0.0f, 1.0f, grHoldDb / 24.0f) - 1.0f).withHeight (2.0f));
        }
        g.setFont (font (type::caption));
        g.setColour (colour::textSecondary);
        g.drawText (juce::String (grHoldDb, 1), juce::Rectangle<float> (grR.getX() - 8.0f, grR.getBottom() - 13.0f, grR.getWidth() + 16.0f, 12.0f), juce::Justification::centred, false);
        g.setFont (font (type::caption));
        g.setColour (colour::textDisabled);
        for (const auto db : { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (m.getX() - 34.0f, meterY (db) - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight, false);
        caption (g, { m.getX() - 6.0f, m.getY() - 16.0f, 38.0f, 14.0f }, tr ("In"));
        caption (g, { grR.getX() - 10.0f, m.getY() - 16.0f, 34.0f, 14.0f }, "GR");
        // round 135w: the output meter, −30 … 0 dBFS, a clip lamp above it and the highest peak under it
        const auto o = outMeter();
        outPeakDb = juce::jmax (outPeakDb, newest.out);
        drawMeter (g, o, newest.out, -30.0f, colour::meterLow.withAlpha (0.85f));
        const auto lamp = juce::Rectangle<float> (o.getX(), o.getY() - 10.0f, o.getWidth(), 6.0f);
        g.setColour (outPeakDb > -0.1f ? juce::Colour (0xffe5383b) : colour::bg1);
        g.fillRoundedRectangle (lamp, 2.0f);
        caption (g, { o.getX() - 10.0f, m.getY() - 16.0f, 36.0f, 14.0f }, tr ("Out"));
        g.setFont (font (type::caption));
        g.setColour (outPeakDb > -0.1f ? juce::Colour (0xffe5383b) : colour::textSecondary);
        g.drawText (outPeakDb > -99.0f ? juce::String (outPeakDb, 1) : juce::String ("-inf"), juce::Rectangle<float> (o.getX() - 10.0f, o.getBottom() + 2.0f, o.getWidth() + 20.0f, 12.0f),
                    juce::Justification::centred, false);
    }
};

// =============================================================================================================
// Crest Compressor — the reference compressor's workflow: the display is the window (input in grey, output in
// blue, the gain reduction as a red line from the top, the threshold as a dashed line that can be dragged), the
// knee / transfer curve slides out from the left edge, the knobs float over the bottom of the display (THRESHOLD,
// RATIO, the style button, ATTACK, RELEASE; KNEE, RANGE, LOOKAHEAD, HOLD under them; make-up gain and mix on the
// right), and the side chain opens below the display.
class CrestEditor final : public ProEditor
{
public:
    CrestEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::crestCompressor, d, 860, 560) { hoverValues = true; fillDemo(); }

private:
    enum { idThreshold = 1, idKnee, idSideChain, idInMeter, idScNode0 = 10 };
    // the display's level scale: the meter-scale setting (9 … 90 dB) is its floor, the upper 70 % shows its top 42 %
    LevelScale sc() const
    {
        const auto range = (float) dyn::crest::meterRangeDb (v (dyn::crest::meterScale));
        return { -range * 30.0f / 72.0f, -range };
    }
    bool kneeOpen = true, scOpen = false; // round tp172b4: the transfer curve shows by default, as in the reference
    static constexpr float scH = 170.0f;
    int scDrag = -1;

    juce::String title() const override { return "Crest Compressor"; }
    juce::String gestures() const override { return tr ("Drag the dashed threshold line  -  KNEE on the left: the curve  -  SIDE CHAIN: the detector"); }
    bool usesHistory() const override { return true; }
    float grDb() const override { return liveMeter (0) * 30.0f; }

    juce::Rectangle<float> main() const { return bodyArea().withTrimmedBottom (scOpen ? scH : 0.0f); }
    juce::Rectangle<float> plot() const { return main().withTrimmedRight (100.0f).withTrimmedTop (10.0f).withTrimmedBottom (8.0f); }
    juce::Rectangle<float> scPanel() const { return { 0.0f, main().getBottom(), (float) getWidth(), scH }; }
    // round tp172b4: down to just above the knob row's Auto toggles (it covered them)
    juce::Rectangle<float> kneeArea() const { auto p = plot(); return { p.getX(), p.getY(), juce::jmin (330.0f, p.getWidth() * 0.42f), juce::jmax (80.0f, p.getHeight() - 222.0f) }; }
    double thresholdDb() const { return dyn::crest::thresholdDb (v (dyn::crest::threshold)); }

    Column demoColumn (int i) const override
    {
        const auto* fx = current();
        const auto in = demoInDb (i) + 6.0f;
        float gr = 0.0f;
        if (fx != nullptr)
            for (int k = 0; k < 30 && i - k >= 0; ++k)
                gr = juce::jmax (gr, (float) -dyn::crest::staticGainDb (fx->params, demoInDb (i - k) + 6.0f) - 0.4f * (float) k);
        return { in, in - gr, gr };
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        using namespace dyn::crest;
        const auto p = plot();
        out.push_back (custom ({ p.getX() + 24.0f, sc().y ((float) thresholdDb(), p) - 5.0f, p.getWidth() - 24.0f, 10.0f }, idThreshold, threshold,
                               tr ("Threshold: drag up or down")));
        out.push_back (button ({ kneeOpen ? kneeArea().getRight() : p.getX(), p.getY() + 80.0f, 20.0f, 64.0f }, idKnee,
                               {}, false, tr ("Knee: show the transfer curve"))); // (a grey tab, as the reference's; its arrow and KNEE: paintOverlay)
        // the knob row over the bottom of the display
        const auto by = p.getBottom() - 190.0f;
        // round tp172b5: no Auto buttons — AUTO is a word in the caption (AUTO THRESHOLD, AUTO RELEASE, AUTO GAIN), clicked
        // there, dim when off and lit when on, as the reference prints it
        const auto withAuto = [] (Ctl c, int a) { c.autoParam = a; return c; };
        out.push_back (withAuto (knob ({ 96.0f, by, 104.0f, 118.0f }, threshold, tr ("Threshold"), 2), autoThreshold));
        out.push_back (knob ({ 214.0f, by, 104.0f, 118.0f }, ratio, tr ("Ratio"), 2));
        // round tp172b4: the style button at the knobs' height, then the character and the voicing under it (labelled),
        // where the reference keeps its character control
        // round tp172b4 (second pass): the style a single pill button at the knobs' height, as the reference's; the
        // character and the voicing (LYNX's) small text buttons under it ("Character: Warm")
        {
            auto st = menu ({ 334.0f, by + 30.0f, 96.0f, 24.0f }, style, {}, "Clean|Allround|Punch|Upward|Two-way|Vari-mu|Classic|Opto|Vocal|Bus|Pumping");
            st.look = 1;
            out.push_back (st);
        }
        out.push_back (barMenu ({ 318.0f, by + 62.0f, 128.0f, 18.0f }, character, tr ("Character"), "Off|Warm|Crisp|Bright"));
        // round tp172b2: Pro-C 3's Smooth / Op-El / Mastering characters, a voicing over the style
        out.push_back (barMenu ({ 318.0f, by + 82.0f, 128.0f, 18.0f }, voicing, tr ("Voicing"), "None|Smooth|Op-El|Mastering"));
        out.push_back (knob ({ 446.0f, by, 104.0f, 118.0f }, attack, tr ("Attack"), 2));
        out.push_back (withAuto (knob ({ 564.0f, by, 104.0f, 118.0f }, release, tr ("Release"), 2), autoRelease));
        out.push_back (withAuto (knob ({ 680.0f, by - 6.0f, 62.0f, 82.0f }, P ("Output"), tr ("Gain"), 1, true), autoGain));
        out.push_back (knob ({ 680.0f, by + 100.0f, 62.0f, 82.0f }, dryGain, tr ("Dry")));
        // the display's scale (bottom right, like the reference's dB box) and the input meter switch
        out.push_back (menu ({ p.getRight() - 16.0f, p.getBottom() - 20.0f, 60.0f, 20.0f }, meterScale, {}, "9 dB|18 dB|36 dB|72 dB|90 dB"));
        out.push_back (button ({ p.getRight() + 42.0f, main().getY(), 18.0f, 12.0f }, idInMeter, "IN", v (inputMeter) >= 0.5f,
                               tr ("Input meter: show or hide")));
        // round tp172b4 (second pass): KNEE, RANGE, LOOKAHEAD and HOLD as the reference's short curved bars under the big
        // knobs (DRIVE, LYNX's, the same in the middle), the side-chain button under them
        const auto sy = p.getBottom() - 70.0f;
        for (const auto& [x, prm, cap] : { std::tuple<float, int, juce::String> { 108.0f, (int) knee, tr ("Knee") }, { 226.0f, (int) range, tr ("Range") },
                                           { 342.0f, (int) drive, tr ("Drive") }, { 458.0f, (int) lookahead, tr ("Lookahead") },
                                           { 576.0f, (int) hold, tr ("Hold") } })
        {
            auto bar = knob ({ x, sy, 80.0f, 46.0f }, prm, cap, 0);
            bar.look = 1;
            out.push_back (bar);
        }
        out.push_back (button ({ 334.0f, sy + 48.0f, 96.0f, 20.0f }, idSideChain, tr ("Side chain"), scOpen, tr ("Open the side chain: detector EQ, level, external input, stereo link")));
        if (scOpen)
        {
            const auto s = scPanel();
            out.push_back (toggle ({ 14.0f, s.getY() + 14.0f, 104.0f, 22.0f }, scListen, tr ("Listen")));
            out.push_back (tabs ({ 14.0f, s.getY() + 44.0f, 104.0f, 24.0f }, scExternal, "Int|Ext"));
            out.push_back (knob ({ 130.0f, s.getY() + 12.0f, 70.0f, 82.0f }, scLevel, tr ("Level"), 1, true));
            out.push_back (knob ({ 130.0f, s.getY() + 92.0f, 70.0f, 70.0f }, stereoLink, tr ("Stereo link"), 0));
            out.push_back (menu ({ 14.0f, s.getY() + 108.0f, 104.0f, 22.0f }, linkMode, tr ("Link mode"), "Stereo|Mid only|Side only|Mid > side|Side > mid"));
            for (int b = 0; b < scBands; ++b)
                out.push_back (menu ({ 216.0f, s.getY() + 16.0f + 46.0f * (float) b, 92.0f, 22.0f }, scParam (b, 0), {},
                                     "Off|Bell|Low shelf|High shelf|Low cut|High cut|Notch|Band pass|Tilt shelf"));
            out.push_back (custom (scEqArea(), idScNode0, -1, tr ("Detector EQ: drag a node (frequency / gain), wheel = Q")));
            // round 135: the wet (compressed) and dry paths' stereo balance, at the panel's right end
            out.push_back (knob ({ s.getRight() - 136.0f, s.getY() + 14.0f, 62.0f, 82.0f }, wetPan, tr ("Wet pan"), 0, true));
            out.push_back (knob ({ s.getRight() - 70.0f, s.getY() + 14.0f, 62.0f, 82.0f }, dryPan, tr ("Dry pan"), 0, true));
            // round 137: the trigger (the reference's side-chain Host Sync / MIDI inputs): the detector hears a pulse
            const auto tx = s.getRight() - 268.0f;
            out.push_back (menu ({ tx, s.getY() + 16.0f, 120.0f, 22.0f }, trigger, tr ("Trigger"), "Off|Host sync|MIDI"));
            const auto trig = triggerOf (v (trigger));
            if (trig == Trigger::hostSync)
            {
                out.push_back (menu ({ tx, s.getY() + 46.0f, 120.0f, 22.0f }, triggerSync, tr ("Note"), "1/1|1/2|1/4|1/8|1/16"));
                out.push_back (knob ({ tx, s.getY() + 74.0f, 58.0f, 78.0f }, triggerRate, tr ("Rate"), 0));
                out.push_back (knob ({ tx + 62.0f, s.getY() + 74.0f, 58.0f, 78.0f }, triggerLength, tr ("Length"), 0));
            }
            else if (trig == Trigger::midi)
                out.push_back (knob ({ tx + 20.0f, s.getY() + 50.0f, 80.0f, 92.0f }, triggerChannel, tr ("Channel"), 0));
        }
        const auto b = barArea();
        const auto cy = b.getCentreY();
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 150.0f, 22.0f }, oversampling, tr ("Oversampling"), "Off|2x|4x|8x"));
        out.push_back (barMenu ({ 165.0f, cy - 11.0f, 140.0f, 22.0f }, maxLookahead, tr ("Lookahead"), "Off|1 ms|2 ms|5 ms|10 ms|20 ms"));
        out.push_back (barMenu ({ 310.0f, cy - 11.0f, 125.0f, 22.0f }, characterPre, tr ("Character"), "Post|Pre"));
        out.push_back (barValue ({ 440.0f, cy - 11.0f, 110.0f, 22.0f }, mix, tr ("Mix")));
        out.push_back (barValue ({ b.getRight() - 300.0f, cy - 11.0f, 140.0f, 22.0f }, P ("Input"), tr ("In")));
        out.push_back (barValue ({ b.getRight() - 150.0f, cy - 11.0f, 140.0f, 22.0f }, P ("Output"), tr ("Out")));
    }

    juce::Rectangle<float> scEqArea() const
    {
        const auto s = scPanel();
        return { 320.0f, s.getY() + 10.0f, s.getWidth() - 334.0f - 140.0f - 132.0f, s.getHeight() - 20.0f }; // round 137: − the trigger column
    }
    float scX (double hz) const
    {
        const auto a = scEqArea();
        return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0));
    }
    float scY (double db) const
    {
        const auto a = scEqArea();
        return a.getCentreY() - a.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, (float) db / 24.0f);
    }
    juce::Point<float> scNode (int b) const
    {
        using namespace dyn::crest;
        return { scX (facet::hz (v (scParam (b, 1)))), scY (facet::gainDb (v (scParam (b, 2)))) };
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idKnee) kneeOpen = ! kneeOpen;
        if (c.id == idInMeter) set ({ { dyn::crest::inputMeter, v (dyn::crest::inputMeter) >= 0.5f ? 0.0f : 1.0f } });
        if (c.id == idSideChain)
        {
            scOpen = ! scOpen;
            setSize (getWidth(), getHeight() + (int) (scOpen ? scH : -scH));
        }
        repaint();
    }

    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (c.id == idScNode0)
        {
            scDrag = -1;
            for (int b = 0; b < dyn::crest::scBands; ++b)
                if (facet::shape (v (dyn::crest::scParam (b, 0))) != facet::Shape::off && scNode (b).getDistanceFrom (e.position) < 10.0f)
                    scDrag = b;
            return;
        }
        if (ui::isResetClick (e)) { set ({ { c.param, (float) specFor (c.param).defaultValue } }); return; }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        customDrag (c, e);
    }

    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        using namespace dyn::crest;
        if (c.id == idScNode0)
        {
            if (scDrag < 0) return;
            const auto a = scEqArea();
            const auto hz = 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((e.position.x - a.getX()) / a.getWidth())));
            const auto db = juce::jlimit (-24.0, 24.0, (double) ((a.getCentreY() - e.position.y) / (a.getHeight() * 0.5f)) * 24.0);
            set ({ { scParam (scDrag, 1), facet::hzValue (hz) }, { scParam (scDrag, 2), facet::gainValue (db) } });
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (scNode (scDrag).toInt()), text (scParam (scDrag, 1)) + "  " + text (scParam (scDrag, 2)));
            return;
        }
        const auto db = juce::jlimit (-60.0f, 0.0f, sc().db (e.position.y, plot()));
        set ({ { threshold, invert ([] (float x) { return dyn::crest::thresholdDb (x); }, db) } });
        ui::showValueTip (*this, juce::Rectangle<int> (80, 16).withCentre (e.position.toInt()), tr ("Threshold") + " " + text (threshold));
    }

    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (! scOpen || ! scEqArea().contains (e.position)) return false;
        for (int b = 0; b < dyn::crest::scBands; ++b)
            if (scNode (b).getDistanceFrom (e.position) < 14.0f)
            {
                const auto p = dyn::crest::scParam (b, 3);
                if (const auto x = ui::wheelValue (v (p), e, w, specFor (p))) set ({ { p, (float) *x } });
                return true;
            }
        return false;
    }

    void paintCustom (juce::Graphics& g, const Ctl& c) override
    {
        if (c.id != idScNode0) return;
        const auto* fx = current();
        if (fx == nullptr) return;
        dynui::History none;
        dynui::paint (g, "crestsceq", c.r, 1.0f, *fx, [] (int) { return 0.0f; }, none);
        for (int b = 0; b < dyn::crest::scBands; ++b)
        {
            if (facet::shape (v (dyn::crest::scParam (b, 0))) == facet::Shape::off) continue;
            const auto n = scNode (b);
            g.setColour (peqview::bandColour (b));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (n));
            g.setColour (colour::bg0);
            g.setFont (font (type::caption, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (12.0f, 12.0f).withCentre (n), juce::Justification::centred, false);
        }
    }

    // round tp172b5: the knee tab carries its arrow and KNEE written down it, as the reference's
    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        using namespace theme;
        Ctl tab;
        for (const auto& c : controls())
            if (c.type == K::button && c.id == idKnee) tab = c;
        if (tab.r.isEmpty()) return;
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small, true));
        g.drawText (juce::String::fromUTF8 (kneeOpen ? "\xc2\xab" : "\xc2\xbb"), tab.r.withHeight (18.0f), juce::Justification::centred, false);
        const juce::Point<float> centre (tab.r.getCentreX(), tab.r.getCentreY() + 8.0f);
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, centre.x, centre.y));
        g.setFont (font (type::caption, true));
        g.drawText (caps (tr ("Knee")), juce::Rectangle<float> (tab.r.getHeight() - 20.0f, 14.0f).withCentre (centre), juce::Justification::centred, false);
        g.restoreState();
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto m = main();
        const auto p = plot();
        displayBackground (g, bodyArea());
        {
            // the marks of the chosen scale: six steps over the upper part, two more down to the floor
            const auto s = sc();
            const auto step = -s.knee / 5.0f;
            const auto lower = std::round ((s.knee + s.floor) * 0.5f / 3.0f) * 3.0f; // one mark between the knee and the floor
            const float marks[] { 0.0f, -step, -2.0f * step, -3.0f * step, -4.0f * step, s.knee, lower };
            g.setFont (font (type::caption));
            for (const auto mk : marks)
            {
                const auto y = s.y (mk, p);
                g.setColour (colour::gridBeat);
                g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
                g.setColour (colour::textDisabled);
                const auto label = mk == 0.0f ? juce::String ("0 dB")
                                   : std::abs (mk) < 10.0f && std::abs (mk - std::round (mk)) > 0.05f ? juce::String (mk, 1) : juce::String (juce::roundToInt (mk));
                g.drawText (label, juce::Rectangle<float> (p.getRight() + 8.0f, y - 6.0f, 34.0f, 12.0f), juce::Justification::centredLeft, false);
            }
        }
        const auto n = (int) p.getWidth();
        juce::Path inP, outLine, grLine;
        inP.startNewSubPath (p.getRight(), p.getBottom());
        for (int k = 0; k < n; ++k)
        {
            const auto& c = columnBack (k);
            const auto x = p.getRight() - (float) k;
            inP.lineTo (x, sc().y (c.in, p));
            const juce::Point<float> o { x, sc().y (c.out, p) }, r { x, sc().y (-c.gr, p) };
            if (k == 0) { outLine.startNewSubPath (o); grLine.startNewSubPath (r); }
            else { outLine.lineTo (o); grLine.lineTo (r); }
        }
        inP.lineTo (p.getX(), p.getBottom());
        inP.closeSubPath();
        fillLevel (g, inP, inColour(), p, 0.42f, 0.12f);
        g.setColour (outColour().withAlpha (0.9f));
        g.strokePath (outLine, juce::PathStrokeType (1.2f));
        g.setColour (colour::accent.withAlpha (fx.enabled ? 1.0f : 0.3f));
        g.strokePath (grLine, juce::PathStrokeType (2.0f));
        // threshold
        const auto ty = sc().y ((float) thresholdDb(), p);
        const auto dashes = std::array<float, 2> { 7.0f, 5.0f };
        g.setColour (colour::textSecondary);
        g.drawDashedLine (juce::Line<float> (p.getX(), ty, p.getRight(), ty), dashes.data(), 2, 1.2f);
        g.setFont (font (type::caption, true));
        g.drawText (text (dyn::crest::threshold), juce::Rectangle<float> (p.getRight() - 90.0f, ty - 16.0f, 84.0f, 14.0f), juce::Justification::centredRight, false);
        // a darker band under the knobs keeps them readable over the history
        const auto knobs = juce::Rectangle<float> (p.getX(), p.getBottom() - 206.0f, p.getWidth(), 206.0f);
        g.setGradientFill (juce::ColourGradient (colour::bg0.withAlpha (0.0f), 0.0f, knobs.getY(), colour::bg0.withAlpha (0.85f), 0.0f, knobs.getY() + 60.0f, false));
        g.fillRect (knobs);
        // the transfer curve, slid out from the left
        if (kneeOpen)
        {
            // round tp172b4: as the reference draws it — a plain light line on the field (no box, no fill), the unity
            // diagonal faint, a divider at the panel's edge, the live input as a dot
            const auto k = kneeArea();
            constexpr float lo = -60.0f;
            const auto xOf = [&k] (float db) { return k.getX() + k.getWidth() * (db - lo) / -lo; };
            const auto yOf = [&k] (float db) { return k.getBottom() - k.getHeight() * (juce::jlimit (lo, 0.0f, db) - lo) / -lo; };
            g.setColour (colour::textDisabled.withAlpha (0.35f));
            g.drawLine (k.getX(), k.getBottom(), k.getRight(), k.getY(), 1.0f);
            g.drawVerticalLine ((int) k.getRight(), k.getY(), k.getBottom());
            juce::Path curve;
            for (int i = 0; i <= 120; ++i)
            {
                const auto in = lo + -lo * (float) i / 120.0f;
                const juce::Point<float> pt { xOf (in), yOf (in + (float) dyn::crest::staticGainDb (fx.params, in)) };
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
            g.setColour (fx.enabled ? colour::textSecondary : colour::textDisabled);
            g.strokePath (curve, juce::PathStrokeType (1.6f));
            const auto lm = liveMeter (2);
            const auto live = lm > 1e-6f ? 20.0f * std::log10 (lm) : -120.0f;
            if (fx.enabled && live > lo)
            {
                g.setColour (colour::textPrimary);
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ xOf (live), yOf (live + (float) dyn::crest::staticGainDb (fx.params, live)) }));
            }
        }
        // meters: input, output and gain reduction
        const auto mr = juce::Rectangle<float> (p.getRight() + 46.0f, p.getY(), 50.0f, p.getHeight());
        const auto nw = columnBack (0);
        if (v (dyn::crest::inputMeter) >= 0.5f)
            drawMeter (g, mr.withWidth (14.0f), nw.in, -60.0f, colour::meterLow.withAlpha (0.8f));
        drawMeter (g, mr.withX (mr.getX() + 17.0f).withWidth (14.0f), nw.out, -60.0f, outColour());
        const auto gr = mr.withX (mr.getX() + 34.0f).withWidth (14.0f);
        skin::meterSolid (g, gr, (sc().y (-nw.gr, p) - gr.getY()) / juce::jmax (1.0f, gr.getHeight()), colour::accent, true);
        g.setFont (font (type::caption));
        g.setColour (colour::textDisabled);
        g.drawText ("OUT  GR", mr.withY (m.getY()).withHeight (12.0f).withTrimmedLeft (17.0f).expanded (6.0f, 0.0f), juce::Justification::centred, false);
        if (scOpen)
        {
            const auto s = scPanel();
            g.setColour (colour::bg2);
            g.fillRect (s);
            g.setColour (colour::border);
            g.drawHorizontalLine ((int) s.getY(), 0.0f, s.getRight());
            for (int b = 0; b < dyn::crest::scBands; ++b)
                caption (g, { 216.0f, s.getY() + 40.0f + 46.0f * (float) b, 92.0f, 12.0f }, tr ("Band {0}", { juce::String (b + 1) }));
        }
    }

    dynui::History none;
};

// =============================================================================================================
// Sentry Gate — the reference gate's workflow: THRESHOLD (big) with RATIO and RANGE on the left, the display in the
// middle (the input level scrolling behind the transfer curve, the threshold dashed — drag it), the style button and
// ATTACK / RELEASE on the right with HOLD, KNEE and LOOKAHEAD beside them, the EXPERT strip (side chain, filters,
// upward expansion, dry, output) opening below.
class SentryEditor final : public ProEditor
{
public:
    SentryEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::sentryGate, d, 730, 380) { knobRanges = true; fillDemo(); }

private:
    enum { idThreshold = 1, idExpert, idFilters };
    bool expertOpen = false;
    static constexpr float expertH = 240.0f;
    int filterDrag = -1;

    juce::String title() const override { return "Sentry Gate"; }
    juce::String gestures() const override { return tr ("Drag the dashed threshold  -  EXPERT: side chain and filters"); }
    bool usesHistory() const override { return true; }
    float grDb() const override { return liveMeter (0) * 30.0f; }

    juce::Rectangle<float> main() const { return bodyArea().withTrimmedBottom (expertOpen ? expertH : 0.0f); }
    juce::Rectangle<float> disp() const { const auto m = main(); return { 196.0f, m.getY() + 12.0f, 270.0f, m.getHeight() - 58.0f }; }
    juce::Rectangle<float> expert() const { return { 0.0f, main().getBottom(), (float) getWidth(), expertH }; }
    double thresholdDb() const { return dyn::sentry::thresholdDb (v (dyn::sentry::threshold)); }
    float yDb (float db) const { const auto d = disp(); return d.getY() + d.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 66.0f); }
    float xDb (float db) const { const auto d = disp(); return d.getX() + d.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 66.0f) / 66.0f); }
    juce::Rectangle<float> filterTrack() const { const auto e = expert(); return { 240.0f, e.getY() + 60.0f, 220.0f, 22.0f }; }

    Column demoColumn (int i) const override
    {
        const auto* fx = current();
        const auto in = demoInDb (i) - 8.0f;
        const auto g = fx != nullptr ? (float) -dyn::sentry::staticGainDb (fx->params, in) : 0.0f;
        return { in, in - g, juce::jmax (0.0f, g) };
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        using namespace dyn::sentry;
        const auto m = main();
        const auto d = disp();
        out.push_back (custom ({ d.getX(), yDb ((float) thresholdDb()) - 5.0f, d.getWidth(), 10.0f }, idThreshold, threshold, tr ("Threshold: drag up or down")));
        out.push_back (custom ({ xDb ((float) thresholdDb()) - 5.0f, d.getY(), 10.0f, d.getHeight() }, idThreshold, threshold, tr ("Threshold: drag left or right")));
        out.push_back (knob ({ 30.0f, m.getY() + 18.0f, 130.0f, 152.0f }, threshold, tr ("Threshold"), 2));
        out.push_back (knob ({ 10.0f, m.getY() + 182.0f, 84.0f, 104.0f }, ratio, tr ("Ratio")));
        out.push_back (knob ({ 98.0f, m.getY() + 182.0f, 84.0f, 104.0f }, range, tr ("Range")));
        out.push_back (menu ({ 530.0f, m.getY() + 12.0f, 100.0f, 26.0f }, style, {}, "Classic|Clean|Vocal|Guitar|Upward"));
        if (v (ducking) >= 0.5f)
            out.push_back (toggle ({ 530.0f, m.getY() + 39.0f, 100.0f, 14.0f }, ducking, tr ("Ducking")));
        out.push_back (knob ({ 530.0f, m.getY() + 54.0f, 100.0f, 108.0f }, attack, tr ("Attack")));
        out.push_back (knob ({ 530.0f, m.getY() + 178.0f, 100.0f, 108.0f }, release, tr ("Release")));
        // round tp172b4: the reference's small knobs have darker slate caps
        for (const auto& [y, prm, cap] : { std::tuple<float, int, juce::String> { 8.0f, (int) hold, tr ("Hold") }, { 102.0f, (int) knee, tr ("Knee") },
                                           { 196.0f, (int) lookahead, tr ("Lookahead") } })
        {
            auto k = knob ({ 644.0f, m.getY() + y, 76.0f, 88.0f }, prm, cap, 0);
            k.cap = juce::Colour (0xff4f5370);
            out.push_back (k);
        }
        out.push_back (toggle ({ 646.0f, m.getY() + 290.0f, 72.0f, 20.0f }, lookaheadOn, tr ("On")));
        // round 190dm: the trigger (Level = the gate before; Transient = a drum gate) under the ratio / range knobs, its
        // release shape under the style column (only with Transient)
        out.push_back (tabs ({ 14.0f, m.getBottom() - 30.0f, 168.0f, 24.0f }, trigger, "Level|Transient", { 0.0f, 1.0f }));
        if (v (trigger) >= 0.5f)
            out.push_back (menu ({ 530.0f, m.getBottom() - 30.0f, 100.0f, 24.0f }, releaseShape, {}, "Smooth|Mid|Sharp", { 0.0f, 0.5f, 1.0f }));
        out.push_back (button ({ d.getCentreX() - 60.0f, m.getBottom() - 30.0f, 120.0f, 24.0f }, idExpert, tr ("Expert"), expertOpen,
                               tr ("Expert: side chain (internal / external, gain and mix per channel), side-chain filters, upward expansion, ducking, wet / dry level and pan")));
        if (expertOpen)
        {
            const auto e = expert();
            out.push_back (knob ({ 14.0f, e.getY() + 14.0f, 84.0f, 100.0f }, upThreshold, tr ("Up threshold")));
            out.push_back (knob ({ 104.0f, e.getY() + 14.0f, 84.0f, 100.0f }, upRatio, tr ("Up ratio")));
            out.push_back (tabs ({ filterTrack().getCentreX() - 70.0f, e.getY() + 18.0f, 140.0f, 24.0f }, scExternal, "Int|Ext"));
            out.push_back (custom (filterTrack().expanded (8.0f, 6.0f), idFilters, -1, tr ("Side-chain filters: drag the left handle (high pass) or the right one (low pass)")));
            out.push_back (toggle ({ filterTrack().getCentreX() - 50.0f, filterTrack().getBottom() + 14.0f, 100.0f, 22.0f }, scListen, tr ("Audition")));
            out.push_back (knob ({ 530.0f, e.getY() + 14.0f, 84.0f, 100.0f }, dry, tr ("Dry")));
            out.push_back (knob ({ 630.0f, e.getY() + 14.0f, 84.0f, 100.0f }, P ("Output"), tr ("Output"), 1, true));
            // round 128: the side-chain gain and mix of each detector channel (left / right, or mid / side), ducking,
            // the wet and dry pans
            const bool msMode = v (channelMode) >= 0.5f;
            const auto ry = e.getY() + 130.0f;
            out.push_back (knob ({ 10.0f, ry, 70.0f, 96.0f }, scGainL, msMode ? tr ("Gain M") : tr ("Gain L"), 1, true));
            out.push_back (knob ({ 80.0f, ry, 70.0f, 96.0f }, scMixL, msMode ? tr ("Mix M") : tr ("Mix L"), 0, true));
            out.push_back (knob ({ 160.0f, ry, 70.0f, 96.0f }, scGainR, msMode ? tr ("Gain S") : tr ("Gain R"), 1, true));
            out.push_back (knob ({ 230.0f, ry, 70.0f, 96.0f }, scMixR, msMode ? tr ("Mix S") : tr ("Mix R"), 0, true));
            out.push_back (toggle ({ 330.0f, ry + 36.0f, 110.0f, 22.0f }, ducking, tr ("Ducking")));
            // round 190dm: the transient detector's sensitivity (with Transient)
            if (v (trigger) >= 0.5f)
                out.push_back (knob ({ 446.0f, ry, 80.0f, 96.0f }, sensitivity, tr ("Sensitivity")));
            out.push_back (knob ({ 530.0f, ry, 84.0f, 96.0f }, wetPan, tr ("Wet pan"), 0, true));
            out.push_back (knob ({ 630.0f, ry, 84.0f, 96.0f }, dryPan, tr ("Dry pan"), 0, true));
        }
        const auto b = barArea();
        const auto cy = b.getCentreY();
        // round 135: the oversampling menu (Pro-G's bottom bar); the look-ahead switch stays under its knob
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 140.0f, 22.0f }, oversampling, tr ("Oversampling"), "Off|2x|4x|8x"));
        out.push_back (barMenu ({ 155.0f, cy - 11.0f, 145.0f, 22.0f }, scExternal, tr ("Side chain"), "Internal|External"));
        out.push_back (barMenu ({ 305.0f, cy - 11.0f, 150.0f, 22.0f }, channelMode, tr ("Channel mode"), "Left/Right|Mid/Side"));
        out.push_back (barValue ({ b.getRight() - 275.0f, cy - 11.0f, 128.0f, 22.0f }, input, tr ("In")));
        out.push_back (barValue ({ b.getRight() - 142.0f, cy - 11.0f, 132.0f, 22.0f }, P ("Output"), tr ("Out")));
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idExpert)
        {
            expertOpen = ! expertOpen;
            setSize (getWidth(), getHeight() + (int) (expertOpen ? expertH : -expertH));
        }
        repaint();
    }

    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        using namespace dyn::sentry;
        if (c.id == idFilters)
        {
            const auto t = filterTrack();
            const auto xh = t.getX() + t.getWidth() * v (highPass), xl = t.getX() + t.getWidth() * v (lowPass);
            filterDrag = std::abs (e.position.x - xh) < std::abs (e.position.x - xl) ? highPass : lowPass;
            if (ui::isResetClick (e)) { set ({ { filterDrag, filterDrag == highPass ? 0.0f : 1.0f } }); filterDrag = -1; return; }
            customDrag (c, e);
            return;
        }
        if (ui::isResetClick (e)) { set ({ { c.param, (float) specFor (c.param).defaultValue } }); return; }
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (c.param), v (c.param), [this, p = c.param] (double x) { set ({ { p, (float) x } }); });
            return;
        }
        customDrag (c, e);
    }

    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        using namespace dyn::sentry;
        if (c.id == idFilters)
        {
            if (filterDrag < 0) return;
            const auto t = filterTrack();
            set ({ { filterDrag, juce::jlimit (0.0f, 1.0f, (e.position.x - t.getX()) / t.getWidth()) } });
            ui::showValueTip (*this, t.toNearestInt(), text (filterDrag));
            return;
        }
        const auto d = disp();
        const auto vertical = c.r.getWidth() < 20.0f; // the vertical line moves left / right
        const auto db = vertical ? (e.position.x - d.getX()) / d.getWidth() * 66.0f - 66.0f : -(e.position.y - d.getY()) / d.getHeight() * 66.0f;
        set ({ { threshold, invert ([] (float x) { return dyn::sentry::thresholdDb (x); }, juce::jlimit (-60.0f, 0.0f, db)) } });
        ui::showValueTip (*this, juce::Rectangle<int> (80, 16).withCentre (e.position.toInt()), tr ("Threshold") + " " + text (threshold));
    }

    void paintCustom (juce::Graphics& g, const Ctl& c) override
    {
        using namespace dyn::sentry;
        if (c.id != idFilters) return;
        const auto t = filterTrack();
        const auto xh = t.getX() + t.getWidth() * v (highPass), xl = t.getX() + t.getWidth() * v (lowPass);
        g.setColour (colour::bg1);
        g.fillRoundedRectangle (t, 4.0f);
        g.setColour (colour::info.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (xh, t.getY() + 2.0f, juce::jmax (0.0f, xl - xh), t.getHeight() - 4.0f));
        for (const auto x : { xh, xl })
        {
            g.setColour (colour::textPrimary);
            g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, t.getHeight() + 6.0f).withCentre ({ x, t.getCentreY() }), 2.0f);
        }
        g.setFont (font (type::small));
        g.setColour (colour::warning);
        g.drawText (text (highPass), juce::Rectangle<float> (t.getX() - 30.0f, t.getBottom() + 4.0f, 80.0f, 14.0f), juce::Justification::centredLeft, false);
        g.drawText (text (lowPass), juce::Rectangle<float> (t.getRight() - 50.0f, t.getBottom() + 4.0f, 80.0f, 14.0f), juce::Justification::centredRight, false);
        caption (g, { t.getX(), t.getY() - 16.0f, t.getWidth(), 14.0f }, tr ("Side-chain filters"));
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        using namespace dyn::sentry;
        const auto d = disp();
        skin::canvas (g, bodyArea(), accent);
        // round tp172b4: the transfer graph lies on the field (the reference draws no well), only its grid
        // the input level scrolling behind (newest at the right), on the vertical dB scale
        const auto n = (int) d.getWidth();
        juce::Path in;
        in.startNewSubPath (d.getRight(), d.getBottom());
        for (int k = 0; k < n; ++k)
            in.lineTo (d.getRight() - (float) k, yDb (columnBack (k / 2).in));
        in.lineTo (d.getX(), d.getBottom());
        in.closeSubPath();
        fillLevel (g, in, inColour(), d, 0.45f, 0.12f);
        // grid and labels
        g.setFont (font (type::caption));
        for (int db = 0; db >= -60; db -= 10)
        {
            g.setColour (colour::gridBeat);
            g.drawHorizontalLine ((int) yDb ((float) db), d.getX(), d.getRight());
            g.drawVerticalLine ((int) xDb ((float) db), d.getY(), d.getBottom());
            g.setColour (colour::textDisabled);
            g.drawText (juce::String (db), juce::Rectangle<float> (d.getRight() + 6.0f, yDb ((float) db) - 6.0f, 26.0f, 12.0f), juce::Justification::centredLeft, false);
            g.drawText (juce::String (db), juce::Rectangle<float> (xDb ((float) db) - 14.0f, d.getBottom() + 4.0f, 28.0f, 12.0f), juce::Justification::centred, false);
        }
        // the transfer curve (input → output)
        juce::Path curve;
        for (int i = 0; i <= 132; ++i)
        {
            const auto inDb = -66.0f + (float) i * 0.5f;
            const auto outDb = inDb + (float) staticGainDb (fx.params, inDb);
            const juce::Point<float> pt { xDb (inDb), yDb (outDb) };
            if (i == 0) curve.startNewSubPath (pt);
            else curve.lineTo (pt);
        }
        if (! fx.enabled)
            skin::glowStroke (g, curve, colour::textDisabled, 2.2f, 0.0f);
        else
        {
            g.setColour (accent.withAlpha (0.25f));
            g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour::textPrimary);
            g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        // the live input on the curve
        const auto now = columnBack (0).in;
        if (now > -66.0f)
        {
            g.setColour (colour::accent);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ xDb (now), yDb (now + (float) staticGainDb (fx.params, now)) }));
        }
        // threshold
        const auto dashes = std::array<float, 2> { 5.0f, 4.0f };
        const auto tx = xDb ((float) thresholdDb()), ty = yDb ((float) thresholdDb());
        g.setColour (colour::warning.withAlpha (0.85f));
        g.drawDashedLine (juce::Line<float> (d.getX(), ty, d.getRight(), ty), dashes.data(), 2, 1.0f);
        g.drawDashedLine (juce::Line<float> (tx, d.getY(), tx, d.getBottom()), dashes.data(), 2, 1.0f);
        // meter: input level and gain reduction (round tp172b4: slim dark slots, a pale level, as the reference's)
        const auto mr = juce::Rectangle<float> (d.getRight() + 32.0f, d.getY(), 10.0f, d.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillRect (mr);
        g.fillRect (mr.translated (12.0f, 0.0f));
        g.setColour (juce::Colour (0xffc9cede).withAlpha (0.8f));
        g.fillRect (mr.withTop (yDb (now)));
        g.setColour (colour::accent);
        g.fillRect (mr.translated (12.0f, 0.0f).withBottom (d.getY() + d.getHeight() * juce::jlimit (0.0f, 1.0f, columnBack (0).gr / 66.0f)));
        if (expertOpen)
        {
            const auto e = expert();
            g.setColour (colour::bg2);
            g.fillRect (e);
            g.setColour (colour::border);
            g.drawHorizontalLine ((int) e.getY(), 0.0f, e.getRight());
            caption (g, { 14.0f, e.getY() + 2.0f, 174.0f, 12.0f }, tr ("Upward expansion"));
            caption (g, { filterTrack().getCentreX() - 70.0f, e.getY() + 2.0f, 140.0f, 12.0f }, tr ("Side chain"));
            caption (g, { 10.0f, e.getY() + 118.0f, 290.0f, 12.0f }, v (channelMode) >= 0.5f ? tr ("Side-chain mid / side") : tr ("Side-chain left / right"));
            caption (g, { 530.0f, e.getY() + 118.0f, 184.0f, 12.0f }, tr ("Wet / dry balance"));
            g.setColour (colour::border.withAlpha (0.5f));
            g.drawHorizontalLine ((int) (e.getY() + 114.0f), 10.0f, e.getRight() - 10.0f);
        }
    }
};
// =============================================================================================================
// Prism Multiband — the reference multiband's workflow: the spectrum is the window, each band a coloured column with
// a node at its output level (on the 0 dB line by default) — drag it up / down for the level, left / right to move the
// band; drag a column's edge to move a crossover; the band's reduction curve (the shape of its range) is filled in the
// band's colour and the orange line is the gain right now; click a band for its floating panel (threshold, range,
// attack, release, output with pan on its ring, mode, ratio, knee, lookahead; side chain, slopes and link behind EXPERT).
class PrismEditor final : public ProEditor
{
public:
    PrismEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::prismMultiband, d, 920, 600)
    {
        knobRanges = true; // round tp172b4: each knob's scale under it (as the reference's band panel), the value on hover
        referenceLabels = true;
        specArea = plot();
        // round tp172b6 [EXEC, the reference's grid on its picture: 30 Hz at 16 px, 280 px a decade, 30 kHz at the right
        // edge]: about 26 Hz ... 30 kHz, so a band can reach 30 kHz in view
        specMinHz = 30.0 * std::pow (10.0, -16.0 / 280.0);
        specMaxHz = 30000.0;
        fillDemo();
    }

private:
    enum { idPower = 1, idBandMenu, idClose, idRange, idAnalyzer, idClassic, idExpert, idClassicDet, idSplit, idFree, idAudition, idLinkMode, idSolo0 = 20, idMute0 = 30 };
    // round tp172b4: no band panel until a band is clicked (the reference opens on the bare display)
    int selected = -1, hoverBand = -1, dragBand = -1, dragEdge = -1;
    int dragScHandle = -1; // round tp172b6: 0 / 1 = the free side-chain filter's low / high handle in the drag
    bool expert = false; // round 147: the band panel's expert part (side-chain filtering, stereo link), view only
    double rangeView = 12.0;
    int analyzer = 3; // 0 off, 1 pre, 2 post, 3 pre + post (view only)
    juce::Point<float> dragStart;
    float dragX0[dyn::prism::bands - 1] {};
    float dragLo0 = 0.0f, dragHi0 = 0.0f; // round 150: the free band's edges when its drag started

    juce::String title() const override { return "Prism Multiband"; }
    juce::String gestures() const override { return tr ("Drag a node: level (up / down), band (left / right)  -  drag an edge: crossover  -  double-click: new band  -  wheel: band width"); }
    bool usesSpectrum() const override { return analyzer > 0; }

    juce::Rectangle<float> plot() const { return bodyArea().withTrimmedRight (74.0f).withTrimmedTop (8.0f).withTrimmedBottom (24.0f); }
    // round tp172b6 [EXEC, the reference's editor]: the panel is centred on its band's node (a notch points at it) and
    // kept inside the window, so it follows the band as it moves
    juce::Rectangle<float> panelArea() const
    {
        const auto p = plot();
        const auto w = expert ? panelW + expertW : panelW;
        const auto cx = selected >= 0 ? 0.5f * (bandLoX (selected) + bandHiX (selected)) : p.getCentreX();
        const auto x = juce::jlimit (p.getX() + 14.0f, juce::jmax (p.getX() + 14.0f, (float) getWidth() - 20.0f - w), cx - w * 0.5f);
        return { x, p.getBottom() - 176.0f, w, 164.0f };
    }
    static constexpr float panelW = 460.0f, expertW = 116.0f; // (the reference's widths)
    // round tp172b6 [EXEC, the reference's editor]: Free side-chain filtering: a bar in the band's colour above the panel with
    // a grey handle at each end (they replace LYNX's Low / High knobs), joined to the Free key by a thin line
    bool scFreeShown() const { return selected >= 0 && expert && v (dyn::prism::bandScFree0 + selected) >= 0.5f; }
    float scBarY() const { return panelArea().getY() - 22.0f; }
    float scHandleX (int h) const
    {
        using namespace dyn::prism;
        const auto value = v ((h == 0 ? bandScLow0 : bandScHigh0) + selected);
        return juce::jlimit (plot().getX() + 2.0f, plot().getRight() - 2.0f, xForHz (30.0 * std::pow (1000.0, (double) value), plot()));
    }
    juce::Rectangle<float> scHandleBox (int h) const
    {
        const auto x = scHandleX (h);
        return { h == 0 ? x : x - 18.0f, scBarY() - 8.0f, 18.0f, 17.0f };
    }
    juce::Rectangle<float> expertTab() const { const auto a = panelArea(); return { a.getRight() - 14.0f, a.getY() + 40.0f, 14.0f, 86.0f }; }
    // the band colours, measured on the reference's six bands (magenta, blue, green, violet, red, cyan)
    static juce::Colour bandHue (int b)
    {
        static const juce::uint32 hues[dyn::prism::bands] { 0xfff848ffu, 0xff5787f0u, 0xff82e364u, 0xff9660e7u, 0xfff45363u, 0xff4be6fcu };
        return juce::Colour (hues[juce::jlimit (0, dyn::prism::bands - 1, b)]);
    }

    static int bp (int b, int f) { return dyn::prism::param (b, f); }
    bool bandOn (int b) const { return v (bp (b, dyn::prism::fEnabled)) >= 0.5f; }
    // round 135: up to six bands (the band count); a double-click splits a band, the band menu removes one
    int nb() const
    {
        const auto* fx = current();
        return fx != nullptr ? dyn::prism::bandCount (fx->params) : dyn::prism::legacyBands;
    }
    double xoHz (int i) const
    {
        const auto* fx = current();
        return fx != nullptr ? dyn::prism::crossovers (fx->params)[(std::size_t) i] : dyn::prism::crossoverHz (v (dyn::prism::crossoverParam (i)));
    }
    float edgeX (int i) const // 0 … band count
    {
        const auto p = plot();
        if (i <= 0) return p.getX();
        if (i >= nb()) return p.getRight();
        return xForHz (xoHz (i - 1), p);
    }
    // round 150: the free layout (Pro-MB's): each band has its own edges, the room between bands passes untouched
    bool isFree() const { return v (dyn::prism::bandLayout) >= 0.5f; }
    dyn::prism::Layout layout() const
    {
        const auto* fx = current();
        if (fx != nullptr) return dyn::prism::layoutOf (fx->params);
        EffectParams p {};
        const auto& in = effectInfo (EffectKind::prismMultiband);
        for (std::size_t k = 0; k < in.params.size(); ++k) p[k] = in.params[k].defaultValue;
        return dyn::prism::layoutOf (p);
    }
    float edgeXOfHz (double hz) const
    {
        const auto p = plot();
        if (hz <= 0.0) return p.getX();
        return juce::jlimit (p.getX(), p.getRight(), xForHz (hz, p));
    }
    float bandLoX (int b) const
    {
        if (! isFree()) return edgeX (b);
        const auto L = layout();
        return L.segmentOf[(std::size_t) b] < 0 ? plot().getRight() : edgeXOfHz (L.loHz[(std::size_t) b]);
    }
    float bandHiX (int b) const
    {
        if (! isFree()) return edgeX (b + 1);
        const auto L = layout();
        if (L.segmentOf[(std::size_t) b] < 0) return plot().getRight();
        return L.hiHz[(std::size_t) b] <= 0.0 ? plot().getRight() : edgeXOfHz (L.hiHz[(std::size_t) b]);
    }
    // every per-band parameter of band b (fields, look-ahead, pan)
    static std::vector<int> bandParams (int b)
    {
        using namespace dyn::prism;
        std::vector<int> out;
        for (int f = 0; f < fieldsPerBand; ++f) out.push_back (bp (b, f));
        out.push_back (bandLookaheadParam (b));
        out.push_back (bandPanParam (b));
        // round 147 / 150: the expert panel, the free layout's edges / slopes and the side-chain input go with the band
        for (const int base : { (int) bandLink0, (int) bandLinkMode0, (int) bandScFree0, (int) bandScLow0, (int) bandScHigh0,
                                (int) bandLo0, (int) bandHi0, (int) bandLoSlope0, (int) bandHiSlope0, (int) bandScInput0 })
            out.push_back (base + b);
        return out;
    }
    // round 150: a new free band around `hz` (an octave wide, inside the gap there), the defaults of a band
    void addFreeBand (double hz)
    {
        using namespace dyn::prism;
        const auto n = nb();
        if (n >= bands) return;
        const auto L = layout();
        // round tp172b6 [EXEC, Pro-MB, 4 double-clicks]: a new band is two octaves wide around the click, cut at its neighbours'
        // edges, the top edge goes to the top of the range near it; whatever the height of the click, Range -6 dB on the
        // reference's band defaults (threshold -18, 4 : 1, attack and release 20 %, knee 24 dB, lookahead 1 ms, SC 300 - 3000 Hz)
        double lo = hz / 2.0, hi = hz * 2.0;
        for (int b = 0; b < n; ++b) // stay inside the gap
        {
            if (L.segmentOf[(std::size_t) b] < 0) continue;
            const auto blo = L.loHz[(std::size_t) b], bhi = L.hiHz[(std::size_t) b] > 0.0 ? L.hiHz[(std::size_t) b] : 30000.0;
            if (bhi <= hz) lo = std::max (lo, bhi);
            if (blo >= hz) hi = std::min (hi, blo);
        }
        if (hi < lo * 1.1) return;
        if (hi >= 20000.0) hi = 30000.0;
        std::vector<std::pair<int, float>> c;
        const auto& in = effectInfo (kind);
        for (const auto k : bandParams (n)) c.push_back ({ k, in.params[(std::size_t) k].defaultValue });
        c.push_back ({ bandLo0 + n, edgeValue (lo) });
        c.push_back ({ bandHi0 + n, edgeValue (hi) });
        c.push_back ({ bp (n, fEnabled), 1.0f });
        c.push_back ({ bp (n, fRange), 0.4f }); // -6 dB
        c.push_back ({ bp (n, fThreshold), thresholdValue (-18.0) });
        c.push_back ({ bp (n, fRatio), dyn::crest::ratioValue (4.0) });
        c.push_back ({ bp (n, fAttack), 0.2f });
        c.push_back ({ bp (n, fRelease), 0.2f });
        c.push_back ({ bp (n, fKnee), 0.5f }); // 24 dB
        c.push_back ({ bandLookaheadParam (n), 0.05f }); // 1 ms
        c.push_back ({ bandScLow0 + n, 1.0f / 3.0f }); // 300 Hz
        c.push_back ({ bandScHigh0 + n, 2.0f / 3.0f }); // 3000 Hz
        c.push_back ({ bandCountParam, bandCountValue (n + 1) });
        set (c);
        selected = n;
    }
    // round 150: switching the layout keeps the sound — the free bands take the split's crossovers and slopes, the
    // split takes the free bands' edges (between two bands: the middle of their gap)
    void setLayout (bool free)
    {
        using namespace dyn::prism;
        if (free == isFree()) return;
        const auto L = layout();
        std::vector<std::pair<int, float>> c;
        const auto n = nb();
        if (free)
        {
            for (int b = 0; b < n; ++b)
            {
                c.push_back ({ bandLo0 + b, edgeValue (L.loHz[(std::size_t) b] > 0.0 ? L.loHz[(std::size_t) b] : 30.0) });
                c.push_back ({ bandHi0 + b, edgeValue (L.hiHz[(std::size_t) b] > 0.0 ? L.hiHz[(std::size_t) b] : 30000.0) });
                c.push_back ({ bandLoSlope0 + b, slopeValue (L.loSlope[(std::size_t) b]) });
                c.push_back ({ bandHiSlope0 + b, slopeValue (L.hiSlope[(std::size_t) b]) });
            }
            c.push_back ({ bandLayout, 1.0f });
        }
        else
        {
            for (int k = 0; k + 1 < n; ++k)
            {
                const auto a = L.hiHz[(std::size_t) k] > 0.0 ? L.hiHz[(std::size_t) k] : 18000.0;
                const auto b = L.loHz[(std::size_t) (k + 1)] > 0.0 ? L.loHz[(std::size_t) (k + 1)] : a;
                c.push_back ({ crossoverParam (k), crossoverValue (std::sqrt (a * b)) });
            }
            c.push_back ({ bandLayout, 0.0f });
        }
        set (c);
    }
    // split band b at `hz`: the bands above move up one, the new band (b + 1) starts as a copy of b
    void splitBand (int b, double hz)
    {
        using namespace dyn::prism;
        const auto n = nb();
        if (n >= bands || b < 0 || b >= n) return;
        const auto lo = b > 0 ? xoHz (b - 1) * 1.08 : 30.0, hi = b < n - 1 ? xoHz (b) / 1.08 : 18000.0;
        if (lo >= hi) return;
        std::vector<std::pair<int, float>> c;
        for (int k = n - 1; k >= b; --k)
        {
            const auto from = bandParams (k), to = bandParams (k + 1);
            for (std::size_t j = 0; j < from.size(); ++j) c.push_back ({ to[j], v (from[j]) });
        }
        for (int k = n - 2; k >= b; --k) c.push_back ({ crossoverParam (k + 1), v (crossoverParam (k)) });
        c.push_back ({ crossoverParam (b), crossoverValue (juce::jlimit (lo, hi, hz)) });
        c.push_back ({ bandCountParam, bandCountValue (n + 1) });
        set (c);
        selected = b + 1;
    }
    // remove band b: the bands above move down one, its neighbour takes its range
    void removeBand (int b)
    {
        using namespace dyn::prism;
        const auto n = nb();
        if (n <= 1 || b < 0 || b >= n) return;
        std::vector<std::pair<int, float>> c;
        for (int k = b; k < n - 1; ++k)
        {
            const auto from = bandParams (k + 1), to = bandParams (k);
            for (std::size_t j = 0; j < from.size(); ++j) c.push_back ({ to[j], v (from[j]) });
        }
        const auto gone = b < n - 1 ? b : b - 1; // the crossover that disappears
        for (int k = gone; k < n - 2; ++k) c.push_back ({ crossoverParam (k), v (crossoverParam (k + 1)) });
        c.push_back ({ bandCountParam, bandCountValue (n - 1) });
        set (c);
        selected = juce::jmin (b, n - 2);
    }
    float gainY (double db) const
    {
        const auto p = plot();
        return p.getY() + p.getHeight() * 0.30f - p.getHeight() * 0.276f * juce::jlimit (-2.5f, 1.08f, (float) (db / rangeView));
    }
    double dbAtY (float y) const
    {
        const auto p = plot();
        return (p.getY() + p.getHeight() * 0.30f - y) / (p.getHeight() * 0.276f) * rangeView;
    }
    float specY (float db) const
    {
        const auto p = plot();
        return p.getY() + p.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 100.0f);
    }
    juce::Point<float> node (int b) const
    {
        return { 0.5f * (bandLoX (b) + bandHiX (b)), gainY (dyn::prism::levelDb (v (bp (b, dyn::prism::fLevel)))) };
    }
    int nodeAt (juce::Point<float> p) const
    {
        for (int b = 0; b < nb(); ++b)
            if (node (b).getDistanceFrom (p) < 11.0f) return b;
        return -1;
    }
    int edgeAt (juce::Point<float> p) const
    {
        if (! plot().contains (p)) return -1;
        if (isFree()) // round 150: 2b + 1 = band b's low edge, 2b + 2 its high edge
        {
            for (int b = 0; b < nb(); ++b)
            {
                if (std::abs (bandLoX (b) - p.x) < 5.0f && bandLoX (b) > plot().getX() + 1.0f) return 2 * b + 1;
                if (std::abs (bandHiX (b) - p.x) < 5.0f && bandHiX (b) < plot().getRight() - 1.0f) return 2 * b + 2;
            }
            return -1;
        }
        for (int i = 1; i < nb(); ++i)
            if (std::abs (edgeX (i) - p.x) < 5.0f) return i;
        return -1;
    }
    int bandAtX (float x) const // round 150: −1 in a gap of the free layout
    {
        if (isFree())
        {
            for (int b = 0; b < nb(); ++b)
                if (x >= bandLoX (b) && x < bandHiX (b)) return b;
            return -1;
        }
        for (int b = 0; b < nb(); ++b)
            if (x < edgeX (b + 1)) return b;
        return nb() - 1;
    }

    double liveDb (int b) const
    {
        if (demo) return b == 1 ? -4.5 : b == 2 ? -2.0 : b == 3 ? 1.5 : -1.0;
        if (! bandOn (b)) return 0.0;
        // round tp172b6: with nothing playing the band rests where its curve puts silence (an upward compressor or a
        // downward expander already at its range), as the reference's line shows
        const auto* fx = current();
        if (fx != nullptr && liveMeter (5) <= 1.0e-5f)
            return dyn::prism::bandStaticDb (fx->params, b, -200.0) - dyn::prism::levelDb (v (bp (b, dyn::prism::fLevel)));
        return (double) liveMeter (dyn::prism::bandMeter (b)) * 30.0;
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        using namespace dyn::prism;
        const auto p = plot();
        for (int b = 0; b < nb(); ++b)
        {
            const auto x = bandLoX (b) + 4.0f;
            if (bandHiX (b) - bandLoX (b) < 40.0f) continue;
            out.push_back (button ({ x, p.getY() + 4.0f, 18.0f, 16.0f }, idSolo0 + b, "S", v (bp (b, fSolo)) >= 0.5f, tr ("Solo the band")));
            out.push_back (button ({ x, p.getY() + 22.0f, 18.0f, 16.0f }, idMute0 + b, "M", ! bandOn (b), tr ("Bypass the band (the band passes untouched)")));
        }
        out.push_back (button ({ p.getRight() + 8.0f, p.getY() + 2.0f, 50.0f, 20.0f }, idRange, juce::String ((int) rangeView) + " dB", false,
                               tr ("Gain scale: click to switch between 3, 6, 12 and 30 dB")));
        if (selected >= 0)
        {
            // round tp172b6: the reference's panel: five chunky knobs (OUTPUT with pan on its ring), MODE bottom left in
            // line with RATIO, KNEE and LOOKAHEAD; side chain, slopes, link and the pan mode behind the EXPERT tab
            const auto a = panelArea();
            const auto b = selected;
            auto chunky = [] (Ctl c) { c.look = 2; return c; };
            const auto top = a.getY() + 9.0f;
            const auto w = 86.0f, h = 100.0f; // a 66 px knob
            {
                auto thr = chunky (knob ({ a.getX() + 8.0f, top, w, h }, bp (b, fThreshold), tr ("Threshold")));
                // round tp172b6 [EXEC]: the band's detector level on the threshold's own scale (a -36 / -12 dB tone in the band:
                // the arc ends where the knob reads -36 / -12 dB; a tone outside it: near the start; silence: no arc)
                if (! demo && bandOn (b))
                    if (const auto lv = levelMeterDb (liveMeter (bandLevelMeter (b))); lv > -119.0)
                        thr.levelArc = thresholdValue (lv);
                out.push_back (thr);
            }
            out.push_back (chunky (knob ({ a.getX() + 92.0f, top, w, h }, bp (b, fRange), tr ("Range"), 1, true)));
            out.push_back (chunky (knob ({ a.getX() + 180.0f, top, w, h }, bp (b, fAttack), tr ("Attack"))));
            out.push_back (chunky (knob ({ a.getX() + 264.0f, top, w, h }, bp (b, fRelease), tr ("Release"))));
            {
                const auto outerBox = juce::Rectangle<float> (66.0f, 66.0f).withCentre ({ a.getX() + 352.0f + w * 0.5f, top + 2.0f + 33.0f });
                auto ring = knob (outerBox, bandPanParam (b), tr ("Pan"), 1, true);
                ring.look = 4;
                ring.showValue = false;
                out.push_back (ring);
                auto inner = knob (juce::Rectangle<float> (38.0f, 38.0f).withCentre (outerBox.getCentre()), bp (b, fLevel), tr ("Output"), 1, true);
                inner.look = 3;
                inner.outer = outerBox;
                inner.ringParam = bandPanParam (b);
                const auto midSide = v (panMode) >= 0.5f;
                inner.ringLow = midSide ? "M" : "L";
                inner.ringHigh = midSide ? "S" : "R";
                out.push_back (inner);
            }
            {
                auto mode = tabs ({ a.getX() + 16.0f, a.getY() + 114.0f, 140.0f, 26.0f }, bp (b, fMode), "Compress|Expand");
                mode.look = 1;
                out.push_back (mode);
            }
            // round tp172b5: RATIO, KNEE and LOOKAHEAD are horizontal sliders, as the reference's band panel draws them
            out.push_back (hslider ({ a.getX() + 168.0f, a.getY() + 114.0f, 84.0f, 44.0f }, bp (b, fRatio), tr ("Ratio")));
            {
                auto kneeSlider = hslider ({ a.getX() + 260.0f, a.getY() + 114.0f, 84.0f, 44.0f }, bp (b, fKnee), tr ("Knee"));
                kneeSlider.endLow = caps (tr ("Hard")); // (the reference's words; the value in dB on hover)
                kneeSlider.endHigh = caps (tr ("Soft"));
                out.push_back (kneeSlider);
            }
            out.push_back (hslider ({ a.getX() + 352.0f, a.getY() + 114.0f, 84.0f, 44.0f }, bandLookaheadParam (b), tr ("Lookahead")));
            const auto bare = [] (Ctl c) { c.bare = true; return c; }; // (their faces: paintOverlay)
            out.push_back (bare (button ({ a.getX(), a.getY(), 22.0f, 22.0f }, idPower, {}, false, tr ("Band on / off: a band switched off passes clean"))));
            out.push_back (button ({ a.getRight() - 22.0f, a.getY(), 22.0f, 22.0f }, idClose, {}, false, tr ("Close the band panel (click on the empty display does the same)")));
            out.back().bare = true;
            out.push_back (bare (button ({ a.getRight() - 46.0f, a.getY() + 4.0f, 18.0f, 18.0f }, idBandMenu, {}, false, tr ("Band menu"))));
            out.push_back (bare (button (expertTab(), idExpert, {}, expert,
                                   tr ("Show the band's expert settings: side-chain filtering (band or a free range) and the stereo link"))));
            if (expert)
            {
                const auto x0 = a.getX() + panelW - 14.0f;
                const auto* fxNow = current();
                const auto auditionNow = fxNow != nullptr ? auditionBand (fxNow->params) : -1;
                const auto pill = [&] (juce::Rectangle<float> r, int prm, const juce::String& options) { auto t = tabs (r, prm, options); t.look = 1; t.keepCase = true; out.push_back (t); };
                // SIDE CHAIN, as the reference stacks it: band or free range, the input, Audition; the free range's two
                // knobs have a column of their own (the reference sets them on its own side-chain curve)
                pill ({ x0 + 12.0f, a.getY() + 16.0f, 90.0f, 20.0f }, bandScFree0 + b, "Band|Free");
                {
                    // In / Ext, the reference's two keys (a project that follows the global switch reads as In until a key is
                    // pressed; the three-way choice and the global switch are in the band menu)
                    auto input = tabs ({ x0 + 12.0f, a.getY() + 42.0f, 90.0f, 20.0f }, bandScInput0 + b, "In|Ext", { 0.5f, 1.0f });
                    input.look = 1;
                    input.keepCase = true;
                    out.push_back (input);
                }
                out.push_back (button ({ x0 + 12.0f, a.getY() + 68.0f, 90.0f, 20.0f }, idAudition, tr ("Audition"), auditionNow == b,
                                       tr ("Audition: hear what this band's detector hears")));
                out.back().bare = true; // (a raised grey key: paintOverlay)
                auto link = hslider ({ x0 + 8.0f, a.getY() + 114.0f, 98.0f, 44.0f }, bandLink0 + b, tr ("Stereo link"));
                link.rowTrim = 28.0f;
                out.push_back (link);
                out.push_back (button ({ x0 + 72.0f, a.getY() + 133.0f, 30.0f, 13.0f }, idLinkMode, caps (tr (v (bandLinkMode0 + b) >= 0.5f ? "Side" : "Mid")), false,
                                       tr ("Stereo link")));
            }
        }
        const auto bar = barArea();
        const auto cy = bar.getCentreY();
        static const char* const analyzerNames[] { TR_NOOP ("Off"), TR_NOOP ("Pre"), TR_NOOP ("Post"), TR_NOOP ("Pre + Post") };
        // round 144: the reference's bottom bar order — processing mode, oversampling, look-ahead, analyser; the
        // crossover slope and the side-chain input moved into the band panel (where the reference has them)
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 150.0f, 22.0f }, processingMode, tr ("Phase"), "Linear|Dynamic|Minimum"));
        out.push_back (barMenu ({ 166.0f, cy - 11.0f, 134.0f, 22.0f }, oversampling, tr ("Oversampling"), "Off|2x|4x"));
        out.push_back (barValue ({ 306.0f, cy - 11.0f, 124.0f, 22.0f }, lookahead, tr ("Lookahead")));
        out.push_back (button ({ 436.0f, cy - 11.0f, 150.0f, 22.0f }, idAnalyzer, tr ("Analyzer:") + " " + tr (analyzerNames[analyzer]) + (specFrozen ? " *" : ""), false,
                               tr ("Spectrum analyzer: off, pre, post or pre + post; speed; freeze")));
        // round 147: a project saved before the measured Dynamic phase keeps its classic engine; one click updates it
        // round 150: the layout (split = contiguous bands on crossovers; free = Pro-MB's bands anywhere, the gaps pass
        // untouched) and the old-project indicators, in the column right of the display (the bottom bar is full)
        {
            const auto x = p.getRight() + 6.0f;
            // (inside the plot's top right corner: in the column they covered the +9 of the level scale)
            out.push_back (button ({ p.getRight() - 66.0f, p.getY() + 4.0f, 30.0f, 20.0f }, idSplit, tr ("Split"), ! isFree(),
                                   tr ("Split layout: the bands sit side by side on crossovers")));
            out.push_back (button ({ p.getRight() - 34.0f, p.getY() + 4.0f, 30.0f, 20.0f }, idFree, tr ("Free"), isFree(),
                                   tr ("Free layout (as Pro-MB): each band has its own edges and slopes; the room between bands passes untouched")));
            auto y = p.getY() + 54.0f;
            // round 147: a project saved before the measured Dynamic phase keeps its classic engine; one click updates it
            if (v (dynamicEngine) < 0.5f && phaseOf (v (processingMode)) == Phase::dynamic)
            {
                out.push_back (button ({ x, y, 62.0f, 20.0f }, idClassic, tr ("Classic"), true,
                                       tr ("This project's Dynamic phase runs the engine it was saved with (before round 147). Click to use the measured one (Pro-MB's response).")));
                y += 24.0f;
            }
            if (v (detectorLaw) < 0.5f || v (rev152) < 0.5f)
                out.push_back (button ({ x, y, 62.0f, 20.0f }, idClassicDet, tr ("Old det."), true,
                                       tr ("This project's bands use the detector they were saved with (before round 152). Click to use the measured one (Pro-MB's attack, release and range).")));
        }
        out.push_back (barValue ({ bar.getRight() - 312.0f, cy - 11.0f, 92.0f, 22.0f }, mix, tr ("Mix")));
        out.push_back (barValue ({ bar.getRight() - 216.0f, cy - 11.0f, 100.0f, 22.0f }, input, tr ("In")));
        out.push_back (barValue ({ bar.getRight() - 112.0f, cy - 11.0f, 104.0f, 22.0f }, output, tr ("Out")));
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        using namespace dyn::prism;
        if (c.id == idPower && selected >= 0) set ({ { bp (selected, fEnabled), bandOn (selected) ? 0.0f : 1.0f } });
        else if (c.id == idBandMenu && selected >= 0) { showBandMenu (selected); return; }
        else if (c.id == idAudition && selected >= 0)
            set ({ { audition, current() != nullptr && auditionBand (current()->params) == selected ? 0.0f : (float) (selected + 1) / (float) bands } }); // [EXEC] the button toggles: Band n, then Off
        else if (c.id == idLinkMode && selected >= 0) set ({ { bandLinkMode0 + selected, v (bandLinkMode0 + selected) >= 0.5f ? 0.0f : 1.0f } });
        else if (c.id == idClose) selected = -1;
        else if (c.id == idRange) rangeView = rangeView >= 30.0 ? 3.0 : rangeView >= 12.0 ? 30.0 : rangeView >= 6.0 ? 12.0 : 6.0;
        else if (c.id == idAnalyzer) { showAnalyzerMenu(); return; }
        else if (c.id == idClassic) set ({ { dynamicEngine, 1.0f } });
        else if (c.id == idClassicDet) set ({ { detectorLaw, 1.0f }, { rev152, 1.0f } });
        else if (c.id == idSplit || c.id == idFree) setLayout (c.id == idFree);
        else if (c.id == idExpert) expert = ! expert;
        else if (c.id >= idSolo0 && c.id < idSolo0 + bands) set ({ { bp (c.id - idSolo0, fSolo), v (bp (c.id - idSolo0, fSolo)) >= 0.5f ? 0.0f : 1.0f } });
        else if (c.id >= idMute0 && c.id < idMute0 + bands) set ({ { bp (c.id - idMute0, fEnabled), bandOn (c.id - idMute0) ? 0.0f : 1.0f } });
        repaint();
    }

    // the analyser's options, as the reference's analyser menu: what it shows, its speed, freeze
    void showAnalyzerMenu()
    {
        static const char* const names[] { TR_NOOP ("Off"), TR_NOOP ("Pre"), TR_NOOP ("Post"), TR_NOOP ("Pre + Post") };
        static const char* const speeds[] { TR_NOOP ("Very slow"), TR_NOOP ("Slow"), TR_NOOP ("Medium"), TR_NOOP ("Fast"), TR_NOOP ("Very fast") };
        static const float falls[] { 0.3f, 0.75f, 1.5f, 3.0f, 6.0f };
        juce::PopupMenu m;
        for (int k = 0; k < 4; ++k)
            m.addItem (tr (names[k]), true, analyzer == k, [this, k] { analyzer = k; repaint(); });
        m.addSeparator();
        juce::PopupMenu sp;
        for (int k = 0; k < 5; ++k)
            sp.addItem (tr (speeds[k]), true, specFall == falls[k], [this, k] { specFall = falls[k]; });
        m.addSubMenu (tr ("Speed"), sp);
        // round 144: the reference's resolution and tilt choices
        static const char* const resolutions[] { TR_NOOP ("Low"), TR_NOOP ("Medium"), TR_NOOP ("High"), TR_NOOP ("Maximum") };
        juce::PopupMenu res;
        for (int k = 0; k < 4; ++k)
            res.addItem (tr (resolutions[k]), true, specResolution == k, [this, k] { specResolution = k; });
        m.addSubMenu (tr ("Resolution"), res);
        juce::PopupMenu tilt;
        for (const double t : { 0.0, 1.5, 3.0, 4.5, 6.0 })
            tilt.addItem (juce::String (t, 1) + " dB/oct", true, specTilt == t, [this, t] { specTilt = t; });
        m.addSubMenu (tr ("Tilt"), tilt);
        m.addItem (tr ("Freeze"), true, specFrozen, [this] { specFrozen = ! specFrozen; repaint(); });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition()); // round 152 (checked by hand): at the mouse, not under the window
    }

    void showBandMenu (int b)
    {
        using namespace dyn::prism;
        juce::PopupMenu m;
        // round tp172b6: the panel's < > moved here (the reference's panel has only its node to change bands)
        if (nb() > 1)
        {
            m.addItem (tr ("Previous / next band") + " <", [this, b] { selected = (b + nb() - 1) % nb(); repaint(); });
            m.addItem (tr ("Previous / next band") + " >", [this, b] { selected = (b + 1) % nb(); repaint(); });
            m.addSeparator();
        }
        m.addItem (tr ("On"), true, bandOn (b), [this, b] { set ({ { bp (b, fEnabled), bandOn (b) ? 0.0f : 1.0f } }); });
        m.addItem (tr ("Solo"), true, v (bp (b, fSolo)) >= 0.5f, [this, b] { set ({ { bp (b, fSolo), v (bp (b, fSolo)) >= 0.5f ? 0.0f : 1.0f } }); });
        m.addSeparator();
        m.addItem (tr ("Compress"), true, v (bp (b, fMode)) < 0.5f, [this, b] { set ({ { bp (b, fMode), 0.0f } }); });
        m.addItem (tr ("Expand"), true, v (bp (b, fMode)) >= 0.5f, [this, b] { set ({ { bp (b, fMode), 1.0f } }); });
        m.addSeparator();
        // round tp172b6: LYNX's own settings the reference's panel doesn't show: the slopes, the pan ring's mode, the side chain's three-way input
        const auto choice = [&] (const char* title, int param, const juce::String& options) {
            const auto c = menu ({}, param, {}, options);
            juce::PopupMenu sub;
            for (int i = 0; i < c.options.size(); ++i)
            {
                const auto value = optionValue (c, i);
                sub.addItem (tr (c.options[i]), true, optionIndex (c) == i, [this, param, value] { set ({ { param, value } }); });
            }
            m.addSubMenu (tr (title), sub);
        };
        choice ("Slope", slope, "12 dB|24 dB|48 dB");
        if (isFree())
        {
            choice ("Low slope", bandLoSlope0 + b, "6|12|18|24|30|36|42|48");
            choice ("High slope", bandHiSlope0 + b, "6|12|18|24|30|36|42|48");
        }
        else
        {
            if (b > 0) choice ("Low slope", xoverSlope0 + b - 1, "Global|12|24|48");
            if (b < nb() - 1) choice ("High slope", xoverSlope0 + b, "Global|12|24|48");
        }
        choice ("Pan", panMode, "L/R|M/S");
        choice ("Input", bandScInput0 + b, "Global|Plug-in|External");
        choice ("Side chain", scExternal, "Int|Ext");
        m.addSeparator();
        m.addItem (tr ("Reset the band"), [this, b] {
            std::vector<std::pair<int, float>> c;
            const auto& in = effectInfo (kind);
            for (const auto k : bandParams (b)) c.push_back ({ k, in.params[(std::size_t) k].defaultValue });
            set (c);
        });
        m.addSeparator();
        if (! isFree())
            m.addItem (tr ("Split the band"), nb() < bands, false, [this, b] { splitBand (b, std::sqrt ((b > 0 ? xoHz (b - 1) : 30.0) * (b < nb() - 1 ? xoHz (b) : 18000.0))); });
        m.addItem (tr ("Remove the band"), nb() > 1, false, [this, b] { removeBand (b); });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition()); // round 152 (checked by hand): at the mouse, not under the window
    }

    bool bodyDown (const juce::MouseEvent& e) override
    {
        using namespace dyn::prism;
        const auto p = e.position;
        if (selected >= 0 && panelArea().contains (p)) return false;
        if (! plot().contains (p)) return false;
        dragBand = dragEdge = -1;
        dragScHandle = -1;
        if (scFreeShown())
            for (int h = 0; h < 2; ++h)
                if (scHandleBox (h).contains (p))
                {
                    dragScHandle = h;
                    return true;
                }
        if (const auto b = nodeAt (p); b >= 0)
        {
            selected = b;
            if (e.mods.isPopupMenu()) { showBandMenu (b); return false; }
            if (e.mods.isAltDown()) { set ({ { bp (b, fEnabled), 0.0f } }); return false; }
            dragBand = b;
            dragStart = p;
            for (int i = 0; i < nb() - 1; ++i) dragX0[i] = edgeX (i + 1);
            dragLo0 = bandLoX (b);
            dragHi0 = bandHiX (b);
            return true;
        }
        if (const auto i = edgeAt (p); i > 0 && ! e.mods.isPopupMenu())
        {
            dragEdge = i;
            return true;
        }
        const auto b = bandAtX (p.x);
        if (b < 0) // round 150: a gap of the free layout
        {
            if (e.mods.isPopupMenu() && nb() < bands)
            {
                const auto hz = hzForX (p.x, plot());
                juce::PopupMenu m;
                m.addItem (tr ("New band here"), [this, hz] { addFreeBand (hz); });
                m.showMenuAsync (juce::PopupMenu::Options().withMousePosition()); // round 152 (checked by hand): at the mouse, not under the window
                return false;
            }
            selected = -1;
            repaint();
            return false;
        }
        if (e.mods.isPopupMenu()) { showBandMenu (b); return false; }
        selected = selected == b ? -1 : b; // a click on a band opens its panel, a second click closes it
        repaint();
        return false;
    }

    void moveEdge (int i, float x, std::vector<std::pair<int, float>>& c) const
    {
        using namespace dyn::prism;
        if (isFree()) // round 150: one band's own edge (2b + 1 low, 2b + 2 high), kept inside the display and the band
        {
            const auto b = (i - 1) / 2;
            const bool high = (i - 1) % 2 != 0;
            const auto pl = plot();
            const auto xx = high ? juce::jlimit (bandLoX (b) + 12.0f, pl.getRight() - 2.0f, x) : juce::jlimit (pl.getX() + 2.0f, bandHiX (b) - 12.0f, x);
            c.push_back ({ (high ? bandHi0 : bandLo0) + b, edgeValue (juce::jlimit (30.0, 30000.0, hzForX (xx, pl))) });
            return;
        }
        const auto lo = i > 1 ? edgeX (i - 1) + 12.0f : plot().getX() + 4.0f;
        const auto hi = i < nb() - 1 ? edgeX (i + 1) - 12.0f : plot().getRight() - 4.0f;
        const auto hz = hzForX (juce::jlimit (lo, hi, x), plot());
        c.push_back ({ crossoverParam (i - 1), crossoverValue (juce::jlimit (30.0, 18000.0, hz)) });
    }

    void bodyDrag (const juce::MouseEvent& e) override
    {
        using namespace dyn::prism;
        std::vector<std::pair<int, float>> c;
        if (dragScHandle >= 0 && selected >= 0) // the free side-chain filter's handle, 30 Hz ... 30 kHz, the two kept apart
        {
            const auto prm = (dragScHandle == 0 ? bandScLow0 : bandScHigh0) + selected;
            const auto hz = juce::jlimit (30.0, 30000.0, hzForX (e.position.x, plot()));
            const auto value = (float) (std::log (hz / 30.0) / std::log (1000.0));
            set ({ { prm, dragScHandle == 0 ? juce::jmin (value, v (bandScHigh0 + selected) - 0.01f) : juce::jmax (value, v (bandScLow0 + selected) + 0.01f) } });
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (e.position.toInt()), text (prm));
            return;
        }
        if (dragEdge > 0)
        {
            moveEdge (dragEdge, e.position.x, c);
            set (c);
            const auto prm = isFree() ? ((dragEdge - 1) % 2 != 0 ? bandHi0 : bandLo0) + (dragEdge - 1) / 2 : crossoverParam (dragEdge - 1);
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (e.position.toInt()), tr ("Crossover") + " " + text (prm));
            return;
        }
        if (dragBand < 0) return;
        const auto b = dragBand;
        const auto d = e.position - dragStart;
        const auto fine = e.mods.isCtrlDown() ? 0.25f : 1.0f;
        if (std::abs (d.y) > 2.0f || ! e.mods.isShiftDown())
            c.push_back ({ bp (b, fLevel), (float) ((juce::jlimit (-30.0, 30.0, dbAtY (dragStart.y + d.y * fine)) + 30.0) / 60.0) }); // round tp172b6 [EXEC]: the node is the band's level (60 px down: Band Level -5.00 dB; its Range stays)
        if (std::abs (d.x) > 3.0f && ! (e.mods.isShiftDown() && std::abs (d.y) > std::abs (d.x)))
        {
            // move the band: its edges follow the mouse (the outer bands move one edge)
            if (isFree()) // round 150: the free band's own edges (an edge at the display's end stays there)
            {
                const auto pl = plot();
                if (dragLo0 > pl.getX() + 1.0f)
                    c.push_back ({ bandLo0 + b, edgeValue (juce::jlimit (30.0, 30000.0, hzForX (dragLo0 + d.x * fine, pl))) });
                if (dragHi0 < pl.getRight() - 1.0f)
                    c.push_back ({ bandHi0 + b, edgeValue (juce::jlimit (30.0, 30000.0, hzForX (dragHi0 + d.x * fine, pl))) });
            }
            else if (b > 0) c.push_back ({ crossoverParam (b - 1), crossoverValue (juce::jlimit (30.0, 18000.0, hzForX (dragX0[b - 1] + d.x * fine, plot()))) });
            if (b < nb() - 1) c.push_back ({ crossoverParam (b), crossoverValue (juce::jlimit (30.0, 18000.0, hzForX (dragX0[b] + d.x * fine, plot()))) });
        }
        set (c);
        ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (node (b).toInt()), tr ("Band {0}", { juce::String (b + 1) }) + "  " + text (bp (b, fLevel)));
    }

    bool bodyDoubleClick (const juce::MouseEvent& e) override
    {
        using namespace dyn::prism;
        if (! plot().contains (e.position)) return false;
        // round 135: double-click on a node = on / off; on the empty display = a new band there (the band under the
        // mouse splits at that frequency), as the reference creates bands anywhere
        if (isFree()) // round 150: on a node = on / off; in a gap = a new band there (up to six)
        {
            if (const auto nodeB = nodeAt (e.position); nodeB >= 0)
            {
                set ({ { bp (nodeB, fEnabled), bandOn (nodeB) ? 0.0f : 1.0f } });
                selected = nodeB;
            }
            else if (bandAtX (e.position.x) < 0)
                addFreeBand (hzForX (e.position.x, plot()));
            return true;
        }
        if (const auto nodeB = nodeAt (e.position); nodeB >= 0 || nb() >= bands)
        {
            const auto b = nodeB >= 0 ? nodeB : bandAtX (e.position.x);
            set ({ { bp (b, fEnabled), bandOn (b) ? 0.0f : 1.0f } });
            selected = b;
            return true;
        }
        splitBand (bandAtX (e.position.x), hzForX (e.position.x, plot()));
        return true;
    }

    // round tp172b6 [EXEC, Pro-MB]: the wheel over a band widens (up) or narrows it, both edges alike; nothing over a gap. One
    // notch moves each edge by 0.00028 of the 30 Hz - 30 kHz scale when notches come 360 ms apart, 0.0012 at 65 ms
    juce::uint32 lastWheelMs = 0;
    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        using namespace dyn::prism;
        if (! plot().contains (e.position)) return false;
        auto b = nodeAt (e.position);
        if (b < 0) b = bandAtX (e.position.x);
        if (b < 0 || w.deltaY == 0.0f) return true;
        const auto now = juce::Time::getMillisecondCounter();
        const auto dt = juce::jlimit (65.0, 360.0, (double) (now - lastWheelMs));
        lastWheelMs = now;
        const auto u = std::log (dt / 65.0) / std::log (360.0 / 65.0);
        const auto perNotch = 0.0012 * std::pow (0.00028 / 0.0012, u);
        const auto notches = (double) w.deltaY / 0.234375; // (JUCE's one notch)
        const auto octaves = perNotch * notches * std::log2 (1000.0); // the scale's width in octaves
        std::vector<std::pair<int, float>> c;
        const auto lo = isFree() ? layout().loHz[(std::size_t) b] : (b > 0 ? xoHz (b - 1) : 0.0);
        const auto hiEdge = isFree() ? layout().hiHz[(std::size_t) b] : (b < nb() - 1 ? xoHz (b) : 0.0);
        const auto factor = std::pow (2.0, octaves);
        if (isFree())
        {
            if (lo > 0.0) c.push_back ({ bandLo0 + b, edgeValue (juce::jlimit (30.0, 30000.0, lo / factor)) });
            if (hiEdge > 0.0) c.push_back ({ bandHi0 + b, edgeValue (juce::jlimit (30.0, 30000.0, hiEdge * factor)) });
        }
        else
        {
            if (lo > 0.0) c.push_back ({ crossoverParam (b - 1), crossoverValue (juce::jlimit (30.0, 18000.0, lo / factor)) });
            if (hiEdge > 0.0) c.push_back ({ crossoverParam (b), crossoverValue (juce::jlimit (30.0, 18000.0, hiEdge * factor)) });
        }
        if (! c.empty()) set (c);
        return true;
    }

    void bodyMove (juce::Point<float> p) override { hoverBand = nodeAt (p); }

    ui::Hint bodyHint (juce::Point<float> p) override
    {
        ui::Hint h;
        if (! plot().contains (p)) return h;
        if (nodeAt (p) >= 0)
            h.text = tr ("Band node: drag up / down = level, left / right = move the band (Shift: one direction, Ctrl: fine); wheel = band width; Alt-click = bypass; right-click = menu");
        else if (edgeAt (p) > 0)
            h.text = tr ("Crossover: drag left or right");
        else if (isFree() && bandAtX (p.x) < 0)
            h.text = tr ("Free layout: this range passes untouched; double-click = a new band here (up to six)");
        else
            h.text = tr ("Click a band to open its panel; double-click = a new band here (up to six); right-click = band menu");
        h.hasMenu = true;
        return h;
    }

    // round tp172b6: each band's reduction curve, as the reference's display draws it: its response at the band's level
    // and at level + range (the crossover filters' real shape, the other bands flat), rebuilt only when something changed
    struct BandCurve { juce::Path fill, bound; };
    std::array<BandCurve, dyn::prism::bands> bandCurves;
    EffectParams curvesFor {};
    juce::Rectangle<float> curvesPlot;
    double curvesView = 0.0;
    void updateBandCurves (const EffectSlot& fx, juce::Rectangle<float> p)
    {
        using namespace dyn::prism;
        if (curvesFor == fx.params && curvesPlot == p && curvesView == rangeView) return;
        curvesFor = fx.params;
        curvesPlot = p;
        curvesView = rangeView;
        auto q = fx.params; // (a soloed band must not change the others' shapes)
        for (int b = 0; b < nb(); ++b) q[(std::size_t) bp (b, fSolo)] = 0.0f;
        for (int b = 0; b < bands; ++b)
        {
            auto& curve = bandCurves[(std::size_t) b];
            curve.fill.clear();
            curve.bound.clear();
            if (b >= nb()) continue;
            const auto level = levelDb (q[(std::size_t) bp (b, fLevel)]), range = rangeDb (q[(std::size_t) bp (b, fRange)]);
            if (std::abs (range) < 0.01) continue;
            static constexpr int points = 140;
            std::array<juce::Point<float>, points + 1> near, far;
            double gains[bands] {};
            for (int i = 0; i <= points; ++i)
            {
                const auto x = p.getX() + p.getWidth() * (float) i / (float) points;
                const auto hz = hzForX (x, p);
                gains[b] = level;
                near[(std::size_t) i] = { x, gainY (responseDb (q, gains, 48000.0, hz)) };
                gains[b] = level + range;
                far[(std::size_t) i] = { x, gainY (responseDb (q, gains, 48000.0, hz)) };
            }
            curve.fill.startNewSubPath (near[0]);
            curve.bound.startNewSubPath (near[0]);
            for (int i = 1; i <= points; ++i) { curve.fill.lineTo (near[(std::size_t) i]); curve.bound.lineTo (near[(std::size_t) i]); }
            curve.bound.startNewSubPath (far[0]);
            for (int i = 1; i <= points; ++i) curve.bound.lineTo (far[(std::size_t) i]);
            for (int i = points; i >= 0; --i) curve.fill.lineTo (far[(std::size_t) i]);
            curve.fill.closeSubPath();
        }
    }

    // over the controls: the panel's power and menu marks, the EXPERT tab's words, the hovered node's read-out
    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        using namespace dyn::prism;
        if (selected >= 0)
        {
            const auto a = panelArea();
            // the corner tiles (power, close) and the EXPERT tab, as the reference's: raised, one with the panel's corner
            const auto cornerTile = [&] (juce::Rectangle<float> r, bool left, bool hot, juce::Colour face) {
                juce::Path tile;
                tile.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 5.0f, 5.0f, left, ! left, ! left, left);
                g.setColour (face.brighter (hot ? 0.18f : 0.0f));
                g.fillPath (tile);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.strokePath (tile, juce::PathStrokeType (1.0f));
            };
            const auto power = juce::Rectangle<float> (a.getX(), a.getY(), 22.0f, 22.0f);
            const auto lit = bandOn (selected);
            if (expert) // Audition, a raised grey key as the reference's (an accent marker while it plays this band); drawn
                        // before the dimming of a switched-off band's panel
            {
                const auto key = juce::Rectangle<float> (a.getX() + panelW - 14.0f + 12.0f, a.getY() + 68.0f, 90.0f, 20.0f);
                const auto playing = current() != nullptr && auditionBand (current()->params) == selected;
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (key, 4.0f);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5d5d5d).brighter (key.contains (mouse) ? 0.12f : 0.0f), 0.0f, key.getY(), juce::Colour (0xff444343).brighter (key.contains (mouse) ? 0.12f : 0.0f), 0.0f, key.getBottom(), false));
                g.fillRoundedRectangle (key.reduced (1.0f), 3.0f);
                if (playing)
                {
                    g.setColour (juce::Colour (0xffdca103));
                    g.fillRect (key.reduced (1.0f).withWidth (4.0f));
                }
                g.setColour (juce::Colour (0xffd2d2d2));
                g.setFont (font (type::small));
                g.drawText (tr ("Audition"), key, juce::Justification::centred, false);
            }
            if (! lit) // a switched-off band's panel is dimmed, as the reference's; its power tile stays bright
            {
                g.setColour (juce::Colour (0xff3c3c3c).withAlpha (0.45f));
                g.fillRoundedRectangle (a, 7.0f);
            }
            cornerTile (power, true, power.contains (mouse), lit ? juce::Colour (0xff404040) : juce::Colour (0xffb32a2a));
            const auto ct = power.getCentre();
            juce::Path ring;
            ring.addCentredArc (ct.x, ct.y, 5.5f, 5.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.6f, true);
            g.setColour (lit ? juce::Colour (0xffa8a8a8) : colour::textPrimary);
            g.strokePath (ring, juce::PathStrokeType (1.4f));
            g.drawLine (ct.x, ct.y - 7.0f, ct.x, ct.y - 1.0f, 1.4f);
            const auto closeR = juce::Rectangle<float> (a.getRight() - 22.0f, a.getY(), 22.0f, 22.0f);
            cornerTile (closeR, false, closeR.contains (mouse), juce::Colour (0xff414141));
            const auto cc = closeR.getCentre();
            g.setColour (juce::Colour (0xffb4b4b4));
            g.drawLine (cc.x - 4.0f, cc.y - 4.0f, cc.x + 4.0f, cc.y + 4.0f, 2.0f);
            g.drawLine (cc.x - 4.0f, cc.y + 4.0f, cc.x + 4.0f, cc.y - 4.0f, 2.0f);
            const auto m = juce::Point<float> (a.getRight() - 37.0f, a.getY() + 13.0f);
            juce::Path mark;
            mark.addTriangle (m.x - 5.0f, m.y - 2.5f, m.x + 5.0f, m.y - 2.5f, m.x, m.y + 2.5f);
            g.setColour (juce::Colour (0xffb4b4b4));
            g.fillPath (mark);
            const auto tab = expertTab();
            juce::Path tabShape;
            tabShape.addRoundedRectangle (tab.getX(), tab.getY(), tab.getWidth(), tab.getHeight(), 6.0f, 6.0f, true, false, true, false);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff747474).brighter (tab.contains (mouse) ? 0.15f : 0.0f), tab.getX(), 0.0f, juce::Colour (0xff5c5c5c).brighter (tab.contains (mouse) ? 0.15f : 0.0f), tab.getRight(), 0.0f, false));
            g.fillPath (tabShape);
            g.setColour (juce::Colour (0xff222222));
            g.setFont (font (15.0f, true));
            g.drawText (juce::String::fromUTF8 (expert ? "\xc2\xab" : "\xc2\xbb"), tab.withHeight (18.0f).translated (0.0f, 2.0f), juce::Justification::centred, false);
            g.saveState();
            const auto mid = juce::Point<float> (tab.getCentreX(), tab.getCentreY() + 8.0f);
            g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, mid.x, mid.y));
            g.setColour (juce::Colour (0xff303030));
            g.setFont (font (11.5f));
            g.drawText (caps (tr ("Expert")), juce::Rectangle<float> (tab.getHeight() - 18.0f, 14.0f).withCentre (mid), juce::Justification::centred, false);
            g.restoreState();
        }
        // the hovered node's read-out, a light tag under it (above it if the open panel is there)
        if (hoverBand >= 0 && hoverBand < nb() && dragCtl == -1)
        {
            const auto n = node (hoverBand);
            const auto hz = isFree() ? std::sqrt (juce::jmax (1.0, layout().loHz[(std::size_t) hoverBand]) * juce::jmax (1.0, layout().hiHz[(std::size_t) hoverBand] > 0.0 ? layout().hiHz[(std::size_t) hoverBand] : 30000.0))
                                     : hzForX (n.x, plot());
            auto r = juce::Rectangle<float> (n.x - 31.0f, n.y + 15.0f, 62.0f, 45.0f).constrainedWithin (plot());
            if (selected >= 0 && r.intersects (panelArea()))
                r = juce::Rectangle<float> (n.x - 31.0f, n.y - 60.0f, 62.0f, 45.0f).constrainedWithin (plot());
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (r.expanded (1.0f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6e6e6), 0.0f, r.getY(), juce::Colour (0xffc2c2c2), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (juce::Colour (0xff202020));
            g.setFont (font (type::small, true));
            g.drawText (tr ("Band {0}", { juce::String (hoverBand + 1) }), r.withHeight (16.0f).translated (0.0f, 2.0f), juce::Justification::centred, false);
            g.setFont (font (type::caption));
            g.drawText (text (bp (hoverBand, fLevel)), r.withTrimmedTop (17.0f).withHeight (13.0f), juce::Justification::centred, false);
            g.drawText (juce::String (hz, 1) + " Hz", r.withTrimmedTop (30.0f).withHeight (13.0f), juce::Justification::centred, false);
        }
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        using namespace dyn::prism;
        if (selected >= nb()) selected = nb() - 1; // a band removed (or a smaller preset): keep the panel on a band
        const auto p = plot();
        displayBackground (g, bodyArea());
        // round tp172b4: the reference's full log grid (every 1 … 9 of each decade, faint)
        for (double dec = 10.0; dec <= 10000.0; dec *= 10.0)
            for (int k = 2; k <= 9; ++k)
                if (const auto hz = dec * k; hz > specMinHz && hz < specMaxHz && k != 2 && k != 5)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.045f));
                    g.drawVerticalLine ((int) xForHz (hz, p), p.getY(), p.getBottom());
                }
        frequencyLabels (g, p, p.getBottom() + 12.0f);
        // the analyser
        if (analyzer == 1 || analyzer == 3)
            paintSpectrum (g, specPre, [this] (float db) { return specY (db); }, inColour().withAlpha (0.18f), inColour().withAlpha (0.4f));
        if (analyzer >= 2)
            paintSpectrum (g, specPost, [this] (float db) { return specY (db); }, outColour().withAlpha (0.12f), outColour().withAlpha (0.5f));
        // gain scale labels (right) and the 0 dB line; round tp172b6: every 3 dB on the 12 dB view, as the reference's
        g.setFont (font (type::caption));
        const auto step = rangeView >= 30.0 ? 10.0 : rangeView / 4.0;
        for (auto db = -rangeView; db <= rangeView; db += step)
        {
            const auto y = gainY (db);
            if (y < p.getY() + 30.0f) continue; // (the scale button's room)
            g.setColour (std::abs (db) < 0.01 ? colour::gridBar : colour::gridBeat);
            g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
            g.setColour (accent.withAlpha (0.85f));
            g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 6.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        for (auto db = -10.0f; db >= -100.0f; db -= 10.0f)
        {
            g.setColour (colour::textDisabled);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (p.getRight() + 34.0f, specY (db) - 7.0f, 26.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        // the bands: a tinted column, stronger on the selected band; a faint glow where the range ends
        double gains[bands] {};
        updateBandCurves (fx, p);
        for (int b = 0; b < nb(); ++b)
        {
            const auto c = bandHue (b);
            const auto region = juce::Rectangle<float> (bandLoX (b), p.getY(), juce::jmax (0.0f, bandHiX (b) - bandLoX (b)), p.getHeight());
            const auto on = bandOn (b);
            // (measured on the reference's picture: a faint tint of a darkened hue over the plot's grey; a selected band a little more)
            const auto deep = c.withMultipliedBrightness (0.6f);
            const auto strength = on ? (b == selected ? 0.075f : 0.04f) : 0.015f;
            g.setGradientFill (juce::ColourGradient (deep.withAlpha (strength), 0.0f, region.getY(), deep.withAlpha (strength * 0.8f), 0.0f, region.getBottom(), false));
            g.fillRect (region);
            gains[b] = on ? liveDb (b) + levelDb (v (bp (b, fLevel))) : 0.0;
            // the band's reduction curve: filled between its level and its range, as the reference draws it
            if (const auto& curve = bandCurves[(std::size_t) b]; ! curve.fill.isEmpty())
            {
                // (the reference's fill darkens the plot too: a deep hue at 40 %; its line about two thirds of the full hue)
                g.setColour (c.withMultipliedBrightness (0.42f).withMultipliedSaturation (1.3f).withAlpha (0.40f));
                if (on)
                    g.fillPath (curve.fill);
                g.setColour (c.withMultipliedBrightness (0.68f).withMultipliedSaturation (1.3f).withAlpha (on ? 1.0f : 0.8f));
                g.strokePath (curve.bound, juce::PathStrokeType (1.3f));
                const auto ry = gainY (levelDb (v (bp (b, fLevel))) + rangeDb (v (bp (b, fRange))));
                const auto glow = juce::Rectangle<float> (region.getX() + 8.0f, ry - 7.0f, juce::jmax (0.0f, region.getWidth() - 16.0f), 7.0f);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.0f), 0.0f, glow.getY(), c.withAlpha (on ? 0.14f : 0.04f), 0.0f, glow.getBottom(), false));
                g.fillRect (glow);
            }
        }
        const auto edgeLine = [&] (float x) {
            g.setColour (std::abs (mouse.x - x) < 5.0f && p.contains (mouse) ? colour::textPrimary : colour::textSecondary.withAlpha (0.5f));
            const auto dashes = std::array<float, 2> { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (x, p.getY(), x, p.getBottom()), dashes.data(), 2, 1.0f);
        };
        if (isFree()) // round 150: each band's own edges
        {
            for (int b = 0; b < nb(); ++b)
            {
                if (bandLoX (b) > p.getX() + 1.0f) edgeLine (bandLoX (b));
                if (bandHiX (b) < p.getRight() - 1.0f) edgeLine (bandHiX (b));
            }
        }
        else
            for (int i = 1; i < nb(); ++i)
                edgeLine (edgeX (i));
        // the summed response with the live band gains
        juce::Path resp;
        for (int i = 0; i <= 300; ++i)
        {
            const auto x = p.getX() + p.getWidth() * (float) i / 300.0f;
            const auto db = responseDb (fx.params, gains, 48000.0, hzForX (x, p));
            if (i == 0) resp.startNewSubPath (x, gainY (db));
            else resp.lineTo (x, gainY (db));
        }
        skin::glowStroke (g, resp, fx.enabled ? accent : colour::textDisabled, 2.0f, fx.enabled ? 0.35f : 0.0f); // (tp172b4: the reference's orange)
        // the nodes: the selected one bigger, in a white ring
        for (int b = 0; b < nb(); ++b)
        {
            const auto n = node (b);
            const auto c = bandHue (b);
            const auto big = b == hoverBand || b == selected;
            const auto r = juce::Rectangle<float> (big ? 18.0f : 13.0f, big ? 18.0f : 13.0f).withCentre (n);
            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.fillEllipse (r.expanded (1.0f));
            g.setColour (b == selected || b == hoverBand ? juce::Colours::white : juce::Colour (0xff2a2a2a));
            g.fillEllipse (r);
            g.setColour (c); // (the reference keeps a switched-off band's node in its colour)
            g.fillEllipse (r.reduced (big ? 2.0f : 1.0f));
        }
        if (scFreeShown())
        {
            const auto y = scBarY();
            const auto hue = bandHue (selected);
            const auto x0 = scHandleX (0), x1 = scHandleX (1);
            g.setColour (juce::Colour (0xff4a4a4a));
            const auto key = panelArea().getX() + panelW - 14.0f + 12.0f + 67.5f; // over the Free key
            g.fillRect (key - 1.0f, y, 2.0f, panelArea().getY() + 16.0f - y);
            g.setColour (hue.withMultipliedBrightness (0.62f));
            g.fillRect (x0, y - 2.0f, x1 - x0, 4.0f);
            g.setColour (hue.withMultipliedBrightness (0.78f).withAlpha (0.7f));
            g.fillRect (x0, y - 2.0f, x1 - x0, 1.0f);
            g.fillRect (x0, y + 1.0f, x1 - x0, 1.0f);
            for (int h = 0; h < 2; ++h)
            {
                const auto r = scHandleBox (h);
                juce::Path t;
                if (h == 0) t.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(), r.getRight(), r.getBottom());
                else t.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getRight(), r.getBottom());
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xffb0b0b0).brighter (r.contains (mouse) ? 0.15f : 0.0f), 0.0f, r.getY(), juce::Colour (0xff808080).brighter (r.contains (mouse) ? 0.15f : 0.0f), 0.0f, r.getBottom(), false));
                g.fillPath (t, juce::AffineTransform());
                g.strokePath (t, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
        // the band panel: dark and neutral, one shape with the notch that points at its node
        if (selected >= 0)
        {
            const auto a = panelArea();
            const auto nx = juce::jlimit (a.getX() + 18.0f, a.getRight() - 18.0f, node (selected).x);
            juce::Path shape;
            shape.addRoundedRectangle (a, 7.0f);
            juce::Path notch;
            notch.addTriangle (nx - 10.0f, a.getY() + 1.0f, nx, a.getY() - 10.0f, nx + 10.0f, a.getY() + 1.0f);
            shape.addPath (notch);
            shape.setUsingNonZeroWinding (true);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.strokePath (shape, juce::PathStrokeType (3.0f));
            g.setColour (juce::Colour (0xff3c3c3c).withAlpha (0.93f));
            g.fillPath (shape);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawRoundedRectangle (a.reduced (0.5f), 7.0f, 1.0f);
            captionStyle (g, false, 0.0f, accent, type::caption);
            g.drawText (caps (tr ("Mode")), juce::Rectangle<float> (a.getX() + 16.0f, a.getY() + 147.0f, 140.0f, 14.0f), juce::Justification::centred, false);
            if (expert) // the expert part's separator and captions
            {
                const auto x0 = a.getX() + panelW - 14.0f;
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawVerticalLine ((int) x0, a.getY() + 2.0f, a.getBottom() - 2.0f);
                captionStyle (g, false, 0.0f, accent, type::caption);
                g.drawText (caps (tr ("Side chain")), juce::Rectangle<float> (x0 + 8.0f, a.getY() + 91.0f, 98.0f, 14.0f), juce::Justification::centred, false);
            }
        }
        // output meter
        const auto m = juce::Rectangle<float> ((float) getWidth() - 14.0f, p.getY(), 8.0f, p.getHeight());
        drawMeter (g, m, dbOf (liveMeter (5)), -60.0f, colour::meterLow.withAlpha (0.85f));
    }
};

// =============================================================================================================
// Ember Saturator — the reference saturator's workflow: the spectrum on top with the bands side by side (drag a
// crossover line to move it, drag a band's handle up / down for its drive, double-click to split a band in two),
// the selected band's panel floating below (style, mix, the big DRIVE, the tone faders, level, remove), global
// settings in the bottom bar.
class EmberEditor final : public ProEditor
{
public:
    EmberEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::ember, d, 960, 540)
    {
        specArea = disp();
        specMinHz = 20.0;
        specMaxHz = 20000.0;
        fillDemo();
    }

private:
    enum { idRemove = 1, idPrev, idNext, idPower, idSolo, idMute, idBands, idMod, idClassic };
    int selected = 0, dragHandle = -1, dragEdge = -1, hoverHandle = -1;
    bool showMod = false; // round 152: the panel shows the modulation (LFOs, follower, four slots) instead of the band
    int xp (int b, int f) const { return mixsuite::ember::bandExtra (b, f); } // round 128: a band's power, state, tone, feedback, dynamics
    bool powered (int b) const { return v (xp (b, 0)) >= 0.5f; }
    mixsuite::ember::BandState state (int b) const { return mixsuite::ember::bandState (v (xp (b, 1))); }

    juce::String title() const override { return "Ember Saturator"; }
    juce::String gestures() const override { return tr ("Double-click: split a band  -  drag a band's handle: drive  -  drag a line: crossover"); }
    bool usesSpectrum() const override { return true; }

    juce::Rectangle<float> disp() const { return bodyArea().withTrimmedBottom (24.0f).withTrimmedTop (4.0f); }
    juce::Rectangle<float> panelArea() const
    {
        const auto d = disp();
        return { d.getCentreX() - 420.0f, d.getBottom() - 176.0f, 840.0f, 168.0f };
    }
    int count() const
    {
        // round 150: up to six bands (Bands + More bands)
        const auto more = juce::jlimit (0, 2, juce::roundToInt (v (mixsuite::ember::bandsMore) * 2.0f));
        return more > 0 ? mixsuite::ember::firstBands + more : mixsuite::ember::bandCount (v (mixsuite::ember::bands));
    }
    int chr (int b) const { return mixsuite::ember::characterIndex (v (mixsuite::ember::characterParam (b))); }
    // round 135: the Decapitator-role strip along the top of the display (voice row, PUNISH, THUMP, STEEP, the wide
    // filter ranges) and the in / out meter at its right; the band handles start under it
    static constexpr float handleTop = 64.0f;
    juce::Rectangle<float> stripArea() const { const auto d = disp(); return { d.getX() + 8.0f, d.getY() + 6.0f, d.getWidth() - 16.0f, 24.0f }; }
    juce::Rectangle<float> vuArea() const { const auto d = disp(); return { d.getRight() - 64.0f, d.getY() + 38.0f, 56.0f, 120.0f }; }
    bool wide() const { return v (mixsuite::ember::wideCuts) >= 0.5f; }
    int fp (int b, int f) const { return mixsuite::ember::bandParam (b, f); }
    double xoHz (int i) const { return mixsuite::ember::crossoverHz (v (mixsuite::ember::crossoverParam (i))); }
    float edgeX (int i) const
    {
        const auto d = disp();
        if (i <= 0) return d.getX();
        if (i >= count()) return d.getRight();
        return xForHz (xoHz (i - 1), d);
    }
    float handleY (int b) const
    {
        const auto d = disp();
        return d.getY() + handleTop + (panelArea().getY() - 30.0f - d.getY() - handleTop) * (1.0f - v (fp (b, 1)));
    }
    juce::Rectangle<float> handle (int b) const { return juce::Rectangle<float> (38.0f, 16.0f).withCentre ({ 0.5f * (edgeX (b) + edgeX (b + 1)), handleY (b) }); }
    int handleAt (juce::Point<float> p) const
    {
        for (int b = 0; b < count(); ++b)
            if (handle (b).expanded (4.0f).contains (p)) return b;
        return -1;
    }
    int edgeAt (juce::Point<float> p) const
    {
        for (int i = 1; i < count(); ++i)
            if (std::abs (edgeX (i) - p.x) < 5.0f) return i;
        return -1;
    }
    int bandAtX (float x) const
    {
        for (int b = 0; b < count(); ++b)
            if (x < edgeX (b + 1)) return b;
        return count() - 1;
    }
    float specY (float db) const { const auto d = disp(); return d.getY() + d.getHeight() * juce::jlimit (0.0f, 1.0f, (-db - 12.0f) / 90.0f); }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        using namespace mixsuite::ember;
        if (selected >= count()) return;
        const auto a = panelArea();
        const auto b = selected;
        auto add = [&] (Ctl c) { c.colour = peqview::bandColour (b); out.push_back (c); };
        // round 152 (Saturn 2's modulation, measured laws): MOD swaps the band panel for the modulation panel
        out.push_back (button ({ a.getRight() - 146.0f, a.getY() + 10.0f, 66.0f, 20.0f }, idMod, tr ("Mod"), showMod,
                               tr ("Modulation: two LFOs and an envelope follower moving up to four targets")));
        if (showMod)
        {
            out.push_back (knob ({ a.getX() + 10.0f, a.getY() + 40.0f, 60.0f, 74.0f }, lfo1Rate, tr ("LFO 1")));
            out.push_back (menu ({ a.getX() + 10.0f, a.getY() + 122.0f, 60.0f, 22.0f }, lfo1Shape, {}, tr ("Sine|Triangle|Square|Ease in|Ease out")));
            out.push_back (knob ({ a.getX() + 76.0f, a.getY() + 40.0f, 60.0f, 74.0f }, lfo2Rate, tr ("LFO 2")));
            out.push_back (menu ({ a.getX() + 76.0f, a.getY() + 122.0f, 60.0f, 22.0f }, lfo2Shape, {}, tr ("Sine|Triangle|Square|Ease in|Ease out")));
            out.push_back (knob ({ a.getX() + 150.0f, a.getY() + 40.0f, 60.0f, 74.0f }, envAttack, tr ("Attack")));
            out.push_back (knob ({ a.getX() + 214.0f, a.getY() + 40.0f, 60.0f, 74.0f }, envRelease, tr ("Release")));
            juce::String targets;
            for (int t = 0; t < modTargets; ++t) targets << (t > 0 ? "|" : "") << tr (modTargetName (t));
            for (int k = 0; k < modSlots; ++k)
            {
                const auto y = a.getY() + 38.0f + 31.0f * (float) k;
                out.push_back (menu ({ a.getX() + 300.0f, y, 96.0f, 24.0f }, mod1Source + 3 * k, {}, tr ("Off|LFO 1|LFO 2|Envelope")));
                out.push_back (menu ({ a.getX() + 402.0f, y, 140.0f, 24.0f }, mod1Target + 3 * k, {}, targets));
                out.push_back (barValue ({ a.getX() + 548.0f, y, 120.0f, 24.0f }, mod1Amount + 3 * k, tr ("Amount")));
            }
            return;
        }
        // round 152: the band's own crossover slope (its upper edge; Global = the bottom bar's Crossover) and the
        // processing mode (linear phase: zero-phase bands, 64 ms latency at 48 kHz)
        if (b < count() - 1)
            add (menu ({ a.getX() + 10.0f, a.getY() + 36.0f, 60.0f, 20.0f }, crossSlope1 + b, {}, tr ("Global") + "|6|12|24|36|48"));
        out.push_back (toggle ({ a.getRight() - 146.0f, a.getY() + 32.0f, 136.0f, 18.0f }, processing, tr ("Linear phase")));
        // Saturn's band panel order (round 128): power, solo / mute, style | MIX, FEEDBACK, FREQ, DYNAMICS | DRIVE |
        // the band's TONE (bass, mid, treble, presence) | LEVEL; the whole effect's auto gain and dry / wet on the right
        out.push_back (button ({ a.getX() + 70.0f, a.getY() + 12.0f, 32.0f, 22.0f }, idPower, tr ("On"), powered (b), tr ("Band on / off: a band switched off passes clean")));
        out.push_back (button ({ a.getX() + 106.0f, a.getY() + 12.0f, 22.0f, 22.0f }, idSolo, "S", state (b) == BandState::solo, tr ("Solo the band")));
        out.push_back (button ({ a.getX() + 130.0f, a.getY() + 12.0f, 22.0f, 22.0f }, idMute, "M", state (b) == BandState::mute, tr ("Mute the band")));
        add (menu ({ a.getX() + 160.0f, a.getY() + 10.0f, 92.0f, 26.0f }, fp (b, 0), {}, "Tube|Tape|Transistor|Fuzz"));
        // round 150: the band's character (the 28 voicings measured on the reference's styles; Off = the Style menu)
        {
            juce::String opts = tr ("Off");
            for (int k = 1; k <= characters; ++k) opts << "|" << tr (characterName (k));
            add (menu ({ a.getX() + 256.0f, a.getY() + 10.0f, 92.0f, 26.0f }, characterParam (b), {}, opts));
        }
        add (knob ({ a.getX() + 10.0f, a.getY() + 60.0f, 66.0f, 92.0f }, fp (b, 2), tr ("Mix")));
        add (knob ({ a.getX() + 78.0f, a.getY() + 60.0f, 66.0f, 92.0f }, xp (b, 6), tr ("Feedback")));
        add (knob ({ a.getX() + 146.0f, a.getY() + 60.0f, 66.0f, 92.0f }, xp (b, 7), tr ("Freq")));
        add (knob ({ a.getX() + 214.0f, a.getY() + 60.0f, 70.0f, 92.0f }, xp (b, 8), tr ("Dynamics"), 1, true));
        add (knob ({ a.getX() + 290.0f, a.getY() + 40.0f, 124.0f, 128.0f }, fp (b, 1), tr ("Drive"), 2));
        // round 147 (Saturn 2): the drive pan beside the drive (the far channel's drive shrinks), the band's pan under
        // its level; both act on left / right or on mid / side (the bottom bar's Channel)
        add (knob ({ a.getX() + 352.0f, a.getY() + 2.0f, 74.0f, 52.0f }, bandDrivePanParam (b), tr ("Drive pan"), 0, true));
        add (fader ({ a.getX() + 432.0f, a.getY() + 14.0f, 42.0f, 148.0f }, xp (b, 2), tr ("Bass"), true));
        add (fader ({ a.getX() + 476.0f, a.getY() + 14.0f, 42.0f, 148.0f }, xp (b, 3), tr ("Mid"), true));
        add (fader ({ a.getX() + 520.0f, a.getY() + 14.0f, 42.0f, 148.0f }, xp (b, 4), tr ("Treble"), true));
        add (fader ({ a.getX() + 564.0f, a.getY() + 14.0f, 42.0f, 148.0f }, xp (b, 5), tr ("Presence"), true));
        add (knob ({ a.getX() + 612.0f, a.getY() + 8.0f, 70.0f, 78.0f }, fp (b, 3), tr ("Level"), 1, true));
        add (knob ({ a.getX() + 612.0f, a.getY() + 88.0f, 70.0f, 78.0f }, bandPanParam (b), tr ("Pan"), 1, true));
        if (count() > 1)
        {
            out.push_back (button ({ a.getRight() - 72.0f, a.getY() + 10.0f, 20.0f, 20.0f }, idPrev, "<", false, tr ("Previous / next band")));
            out.push_back (button ({ a.getRight() - 50.0f, a.getY() + 10.0f, 20.0f, 20.0f }, idNext, ">", false, tr ("Previous / next band")));
            out.push_back (button ({ a.getRight() - 26.0f, a.getY() + 10.0f, 20.0f, 20.0f }, idRemove, "x", false, tr ("Remove the band (its neighbour takes its range)")));
        }
        out.push_back (toggle ({ a.getRight() - 146.0f, a.getY() + 52.0f, 136.0f, 22.0f }, autoGain, tr ("Auto gain")));
        out.push_back (knob ({ a.getRight() - 148.0f, a.getY() + 84.0f, 66.0f, 80.0f }, mix, tr ("Dry / wet"), 0));
        out.push_back (knob ({ a.getRight() - 78.0f, a.getY() + 84.0f, 66.0f, 80.0f }, output, tr ("Output"), 0, true)); // round 135: on the panel
        // round 135: the strip (one click per voicing, the reference's switches)
        const auto s = stripArea();
        out.push_back (tabs ({ s.getX(), s.getY(), 372.0f, s.getHeight() }, voice, tr ("Style menu|Tape pre|Console|Grit|Triode|Pentode"),
                             { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }));
        out.push_back (toggle ({ s.getX() + 376.0f, s.getY(), 76.0f, s.getHeight() }, punish, tr ("Punish")));
        out.push_back (toggle ({ s.getX() + 456.0f, s.getY(), 70.0f, s.getHeight() }, thump, tr ("Thump")));
        out.push_back (toggle ({ s.getX() + 530.0f, s.getY(), 64.0f, s.getHeight() }, steep, tr ("Steep")));
        out.push_back (toggle ({ s.getX() + 598.0f, s.getY(), 92.0f, s.getHeight() }, wideCuts, tr ("Wide cuts")));
        // round 150 (Saturn 2): the whole effect's input / output pan (left / right, or mid / side with Channel M/S);
        // they end left of the IN / OUT meter
        out.push_back (barValue ({ s.getX() + 698.0f, s.getY(), 86.0f, s.getHeight() }, inputPan, tr ("In pan")));
        out.push_back (barValue ({ s.getX() + 788.0f, s.getY(), 86.0f, s.getHeight() }, outputPan, tr ("Out pan")));
        // round tp172d: back to the Classic panel (the Voicing on; Ember's Classic window replaces this one)
        out.push_back (button ({ s.getX() + 880.0f, s.getY(), 64.0f, s.getHeight() }, idClassic, tr ("Classic"), false,
                                   tr ("The Classic panel: one-screen hardware saturator (Drive, Punish, five styles)")));
        const auto bar = barArea();
        const auto cy = bar.getCentreY();
        out.push_back (barMenu ({ 10.0f, cy - 11.0f, 150.0f, 22.0f }, xoverSlope, tr ("Crossover"), "12 dB/oct|24 dB/oct|48 dB/oct", { 0.0f, 0.5f, 1.0f }));
        out.push_back (barMenu ({ 166.0f, cy - 11.0f, 130.0f, 22.0f }, quality, tr ("Oversampling"), "2x|4x|8x", { 0.0f, 0.5f, 1.0f }));
        out.push_back (button ({ 302.0f, cy - 11.0f, 90.0f, 22.0f }, idBands, tr ("Bands") + " " + juce::String (count()), false,
                               tr ("Number of bands (1 to 6)")));
        out.push_back (barValue ({ 398.0f, cy - 11.0f, 90.0f, 22.0f }, inputGain, tr ("In")));
        out.push_back (barValue ({ 494.0f, cy - 11.0f, 80.0f, 22.0f }, tone, tr ("Tilt")));
        out.push_back (barValue ({ 580.0f, cy - 11.0f, 110.0f, 22.0f }, wide() ? lowCutWide : lowCut, tr ("Low cut")));
        out.push_back (barValue ({ 696.0f, cy - 11.0f, 120.0f, 22.0f }, wide() ? highCutWide : highCut, tr ("High cut")));
        // round 147 (Saturn 2's channel mode): the bands work on left / right or on mid / side
        out.push_back (barMenu ({ 822.0f, cy - 11.0f, 128.0f, 22.0f }, channelMode, tr ("Channel"), "L/R|M/S", { 0.0f, 1.0f }));
    }

    // band settings as a block (style, drive, mix, level + round 128's power, state, tone, feedback, dynamics) to move
    // bands around when splitting / removing
    static constexpr int blockSize = 4 + mixsuite::ember::extraFields + 3; // round 147: + drive pan, pan; round 150: + character
    int blockParam (int b, int k) const
    {
        if (k == blockSize - 3) return mixsuite::ember::bandDrivePanParam (b);
        if (k == blockSize - 2) return mixsuite::ember::bandPanParam (b);
        if (k == blockSize - 1) return mixsuite::ember::characterParam (b);
        return k < 4 ? fp (b, k) : xp (b, k - 4);
    }
    std::array<float, blockSize> bandValues (int b) const
    {
        std::array<float, blockSize> out {};
        for (int k = 0; k < blockSize; ++k) out[(std::size_t) k] = v (blockParam (b, k));
        return out;
    }

    void splitAt (float x)
    {
        using namespace mixsuite::ember;
        const auto n = count();
        if (n >= maxBands) return;
        const auto hz = juce::jlimit (30.0, 18000.0, hzForX (x, disp()));
        const auto at = bandAtX (x); // the band split in two: band at + 1 is the new upper half
        std::vector<double> xo;
        std::vector<float> sl; // round 152: each crossover's own slope moves with it
        for (int i = 0; i < n - 1; ++i) { xo.push_back (xoHz (i)); sl.push_back (v (crossSlope1 + i)); }
        xo.insert (xo.begin() + at, hz);
        sl.insert (sl.begin() + at, at < (int) sl.size() ? sl[(std::size_t) at] : 0.0f);
        std::vector<std::array<float, blockSize>> vals;
        for (int b = 0; b < n; ++b) vals.push_back (bandValues (b));
        vals.insert (vals.begin() + at + 1, vals[(std::size_t) at]);
        apply (xo, vals, sl);
        selected = at + 1;
    }

    void removeBand (int b)
    {
        const auto n = count();
        if (n <= 1) return;
        std::vector<double> xo;
        std::vector<float> sl;
        for (int i = 0; i < n - 1; ++i) { xo.push_back (xoHz (i)); sl.push_back (v (mixsuite::ember::crossSlope1 + i)); }
        xo.erase (xo.begin() + juce::jmin (b, n - 2)); // the line on its right (the last band: on its left)
        sl.erase (sl.begin() + juce::jmin (b, n - 2));
        std::vector<std::array<float, blockSize>> vals;
        for (int k = 0; k < n; ++k)
            if (k != b) vals.push_back (bandValues (k));
        apply (xo, vals, sl);
        selected = juce::jmin (b, n - 2);
    }

    void apply (const std::vector<double>& xo, const std::vector<std::array<float, blockSize>>& vals, const std::vector<float>& slopes = {})
    {
        using namespace mixsuite::ember;
        std::vector<std::pair<int, float>> c;
        const auto n = (int) vals.size();
        const auto [bv, mv] = bandCountValues (n);
        c.push_back ({ bands, bv });
        c.push_back ({ bandsMore, mv });
        for (int i = 0; i < (int) xo.size(); ++i) c.push_back ({ crossoverParam (i), crossoverValue (xo[(std::size_t) i]) });
        for (int i = 0; i < (int) slopes.size() && i < maxBands - 1; ++i) c.push_back ({ crossSlope1 + i, slopes[(std::size_t) i] });
        for (int b = 0; b < n; ++b)
            for (int f = 0; f < blockSize; ++f) c.push_back ({ blockParam (b, f), vals[(std::size_t) b][(std::size_t) f] });
        set (c);
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        using mixsuite::ember::BandState;
        if (c.id == idRemove) removeBand (selected);
        else if (c.id == idPower) set ({ { xp (selected, 0), powered (selected) ? 0.0f : 1.0f } });
        else if (c.id == idSolo) set ({ { xp (selected, 1), state (selected) == BandState::solo ? 0.0f : 0.5f } });
        else if (c.id == idMute) set ({ { xp (selected, 1), state (selected) == BandState::mute ? 0.0f : 1.0f } });
        else if (c.id == idPrev) selected = (selected + count() - 1) % count();
        else if (c.id == idNext) selected = (selected + 1) % count();
        else if (c.id == idBands) showBandsMenu();
        else if (c.id == idMod) showMod = ! showMod; // round 152
        else if (c.id == idClassic) // round tp172d
        {
            set ({ { mixsuite::ember::voicing, 1.0f } });
            if (! demo) NativeEffectWindows::switchEmberEditor (engine, track, slot, true);
            return;
        }
        repaint();
    }

    // round 150: 1 … 6 bands; more bands split the top band, fewer remove the top ones
    void showBandsMenu()
    {
        juce::PopupMenu m;
        for (int k = 1; k <= mixsuite::ember::maxBands; ++k)
            m.addItem (juce::String (k), true, count() == k, [this, k] { setBandCount (k); });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition()); // round 152 (checked by hand): at the mouse, not under the window
    }

    void setBandCount (int target)
    {
        while (count() > target) removeBand (count() - 1);
        while (count() < target)
        {
            const auto n = count();
            const auto lo = n > 1 ? edgeX (n - 1) : disp().getX();
            const auto before = n;
            splitAt (0.5f * (lo + disp().getRight()));
            if (count() == before) break;
        }
        repaint();
    }

    bool bodyDown (const juce::MouseEvent& e) override
    {
        const auto p = e.position;
        dragHandle = dragEdge = -1;
        if (selected < count() && panelArea().contains (p)) return false;
        if (! disp().contains (p)) return false;
        if (const auto h = handleAt (p); h >= 0)
        {
            selected = h;
            if (e.mods.isAltDown()) { removeBand (h); return false; }
            if (ui::isResetClick (e)) { set ({ { fp (h, 1), 0.0f } }); return false; }
            dragHandle = h;
            return true;
        }
        if (const auto i = edgeAt (p); i > 0) { dragEdge = i; return true; }
        selected = bandAtX (p.x);
        repaint();
        return false;
    }

    void bodyDrag (const juce::MouseEvent& e) override
    {
        using namespace mixsuite::ember;
        const auto d = disp();
        if (dragHandle >= 0)
        {
            const auto f = 1.0f - (e.position.y - d.getY() - handleTop) / (panelArea().getY() - 30.0f - d.getY() - handleTop);
            set ({ { fp (dragHandle, 1), juce::jlimit (0.0f, 1.0f, f) } });
            ui::showValueTip (*this, handle (dragHandle).toNearestInt(), tr ("Drive") + " " + text (fp (dragHandle, 1)));
            return;
        }
        if (dragEdge > 0)
        {
            const auto lo = edgeX (dragEdge - 1) + 14.0f, hi = edgeX (dragEdge + 1) - 14.0f;
            const auto hz = juce::jlimit (30.0, 18000.0, hzForX (juce::jlimit (lo, hi, e.position.x), d));
            set ({ { crossoverParam (dragEdge - 1), crossoverValue (hz) } });
            ui::showValueTip (*this, juce::Rectangle<int> (16, 16).withCentre (e.position.toInt()), tr ("Crossover") + " " + text (crossoverParam (dragEdge - 1)));
        }
    }

    bool bodyDoubleClick (const juce::MouseEvent& e) override
    {
        if (! disp().contains (e.position) || (selected < count() && panelArea().contains (e.position))) return false;
        if (const auto h = handleAt (e.position); h >= 0) { removeBand (h); return true; }
        splitAt (e.position.x);
        return true;
    }

    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto h = handleAt (e.position);
        if (h < 0) return false;
        const auto prm = fp (h, 1);
        if (const auto x = ui::wheelValue (v (prm), e, w, specFor (prm))) set ({ { prm, (float) *x } });
        return true;
    }

    void bodyMove (juce::Point<float> p) override { hoverHandle = handleAt (p); }

    ui::Hint bodyHint (juce::Point<float> p) override
    {
        ui::Hint h;
        if (! disp().contains (p)) return h;
        if (handleAt (p) >= 0) h.text = tr ("Band handle: drag up / down = drive, wheel = drive, Alt-click or double-click = remove the band");
        else if (edgeAt (p) > 0) h.text = tr ("Crossover: drag left or right");
        else h.text = tr ("Click a band to edit it; double-click to split it in two (up to six bands)");
        return h;
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        using namespace mixsuite::ember;
        const auto d = disp();
        displayBackground (g, bodyArea());
        frequencyLabels (g, d, d.getBottom() + 12.0f);
        paintSpectrum (g, specPost, [this] (float db) { return specY (db); }, inColour().withAlpha (0.14f), inColour().withAlpha (0.45f));
        const auto n = count();
        for (int b = 0; b < n; ++b)
        {
            const auto c = peqview::bandColour (b);
            const auto region = juce::Rectangle<float> (edgeX (b), d.getY(), edgeX (b + 1) - edgeX (b), d.getHeight());
            g.setColour (c.withAlpha (b == selected ? 0.12f : 0.04f));
            g.fillRect (region);
            // the drive level line across the band, its handle and the band's name / drive
            const auto h = handle (b);
            g.setColour (c.withAlpha (0.5f));
            g.drawHorizontalLine ((int) h.getCentreY(), region.getX(), region.getRight());
            const auto hot = b == hoverHandle || b == dragHandle;
            g.setColour (hot ? c.brighter (0.3f) : c);
            g.fillRoundedRectangle (h, 4.0f);
            g.setColour (colour::bg0);
            g.drawRoundedRectangle (h, 4.0f, 1.0f);
            g.setColour (colour::textPrimary);
            g.setFont (font (type::caption, true));
            // round 135: band 1 shows the voicing when one is chosen (it replaces the style)
            const auto vk = mixsuite::ember::voiceIndex (v (mixsuite::ember::voice));
            juce::String tag = caps (chr (b) > 0 ? tr (mixsuite::ember::characterName (chr (b)))
                                                 : (b == 0 && vk > 0 && n == 1 ? tr (mixsuite::ember::voiceName (vk)) : text (fp (b, 0)))) + "  " + text (fp (b, 1));
            if (! powered (b)) tag = tr ("OFF") + "  " + tag;
            else if (state (b) == mixsuite::ember::BandState::solo) tag = "S  " + tag;
            else if (state (b) == mixsuite::ember::BandState::mute) tag = "M  " + tag;
            g.drawText (tag, juce::Rectangle<float> (region.getX() + 2.0f, h.getY() - 18.0f, region.getWidth() - 4.0f, 14.0f), juce::Justification::centred, true);
        }
        // round 135: the in / out level meter (the reference's VU: what the drive does to the level)
        {
            const auto m = vuArea();
            g.setColour (colour::bg0.withAlpha (0.7f));
            g.fillRoundedRectangle (m.expanded (4.0f, 16.0f).withTrimmedBottom (-2.0f), 4.0f);
            const auto bars = m.withTrimmedTop (2.0f).withTrimmedBottom (14.0f);
            const auto in = dbOf (liveMeter (2)), out = dbOf (liveMeter (3));
            drawMeter (g, bars.withWidth (bars.getWidth() * 0.5f - 2.0f), in, -48.0f, inColour());
            drawMeter (g, bars.withTrimmedLeft (bars.getWidth() * 0.5f + 2.0f), out, -48.0f, colour::accent);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (tr ("IN"), juce::Rectangle<float> (bars.getX(), m.getY() - 14.0f, bars.getWidth() * 0.5f, 12.0f), juce::Justification::centred, false);
            g.drawText (tr ("OUT"), juce::Rectangle<float> (bars.getCentreX(), m.getY() - 14.0f, bars.getWidth() * 0.5f, 12.0f), juce::Justification::centred, false);
            g.drawText (juce::String (out > -100.0f ? out : -99.0f, 1), juce::Rectangle<float> (m.getX() - 4.0f, bars.getBottom() + 1.0f, m.getWidth() + 8.0f, 12.0f), juce::Justification::centred, false);
        }
        for (int i = 1; i < n; ++i)
        {
            const auto x = edgeX (i);
            const auto hot = std::abs (mouse.x - x) < 5.0f && d.contains (mouse);
            g.setColour (hot || dragEdge == i ? colour::textPrimary : colour::textSecondary.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, d.getY(), d.getBottom());
            const auto knobR = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ x, d.getY() + 44.0f }); // round 135: under the strip
            g.setColour (colour::bg5);
            g.fillRoundedRectangle (knobR, 3.0f);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::caption));
            g.drawText (text (mixsuite::ember::crossoverParam (i - 1)), juce::Rectangle<float> (x + 10.0f, d.getY() + 37.0f, 70.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        if (selected < n)
        {
            const auto a = panelArea();
            const auto c = peqview::bandColour (selected);
            drawPanel (g, a, c);
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (a.getX() + 14.0f, a.getY() - 1.0f, 46.0f, 3.0f), 1.5f);
            g.setColour (colour::textSecondary);
            g.setFont (font (type::small, true));
            if (showMod) // round 152: the modulation panel's captions
            {
                g.drawText (tr ("Modulation"), juce::Rectangle<float> (a.getX() + 12.0f, a.getY() + 10.0f, 120.0f, 26.0f), juce::Justification::centredLeft, false);
                caption (g, { a.getX() + 150.0f, a.getY() + 24.0f, 124.0f, 12.0f }, tr ("Envelope"));
                caption (g, { a.getX() + 300.0f, a.getY() + 20.0f, 368.0f, 12.0f }, tr ("Source  /  target  /  amount"));
                return;
            }
            g.drawText (tr ("Band {0}", { juce::String (selected + 1) }), juce::Rectangle<float> (a.getX() + 12.0f, a.getY() + 10.0f, 58.0f, 26.0f), juce::Justification::centredLeft, false);
            caption (g, { a.getX() + 432.0f, a.getY() + 2.0f, 174.0f, 12.0f }, tr ("Tone"));
            // the curve of the band's style, as a small preview
            const auto cv = juce::Rectangle<float> (a.getX() + 228.0f, a.getY() + 10.0f, 0.0f, 0.0f);
            juce::ignoreUnused (cv, fx);
        }
    }
};

// =============================================================================================================
// Aether — the reference "air" processor's workflow: the output level on an arc meter fills the top; under it two
// big knobs side by side (MID AIR = the presence exciter, HIGH AIR = the air shelf) with the link button between
// them, the power switch at the bottom left and the small TRIM at the bottom right. LYNX's extra settings (the two
// frequencies, the exciter's character, mix) sit in the bottom bar.
class AetherEditor final : public ProEditor
{
public:
    AetherEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::aether, d, 720, 520) { fillDemo(); }

private:
    enum { idLink = 1, idPower };
    bool linked = false;

    juce::String title() const override { return "Aether"; }
    juce::String gestures() const override { return tr ("Link: both knobs move together"); }
    bool usesHistory() const override { return true; }

    // the dynamic air model (round 124) has its own two amounts; the classic mode drives the exciter and the shelf
    bool dynamicModel() const { return v (P ("Dynamic")) >= 0.5f; }
    int pMid() const { return dynamicModel() ? P ("Mid air") : P ("Harmonics"); }
    int pHigh() const { return dynamicModel() ? P ("High air") : P ("Air"); }
    juce::Rectangle<float> display() const { return bodyArea().withHeight (206.0f); }
    juce::Rectangle<float> panel() const { return bodyArea().withTrimmedTop (206.0f); }

    Column demoColumn (int i) const override
    {
        const auto in = demoInDb (i) - 4.0f;
        return { in, in + 1.5f, 0.0f };
    }

    void linkChanges (std::vector<std::pair<int, float>>& changes) const override
    {
        if (! linked || changes.size() != 1) return;
        const auto [p, x] = changes.front();
        if (p != pMid() && p != pHigh()) return;
        const auto other = p == pMid() ? pHigh() : pMid();
        changes.push_back ({ other, juce::jlimit (0.0f, 1.0f, v (other) + (x - v (p))) });
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        const auto a = panel();
        const auto cy = a.getY() + 12.0f;
        out.push_back (knob ({ 125.0f, cy + 10.0f, 150.0f, 184.0f }, pMid(), tr ("Mid air"), 2));
        out.push_back (knob ({ 445.0f, cy + 10.0f, 150.0f, 184.0f }, pHigh(), tr ("High air"), 2));
        out.push_back (button ({ 330.0f, cy + 70.0f, 60.0f, 28.0f }, idLink, tr ("Link"), linked,
                               tr ("Link: move both knobs together (keeps their difference)")));
        out.push_back (custom ({ 28.0f, a.getBottom() - 70.0f, 48.0f, 48.0f }, idPower, -1, tr ("Power: switch the effect on or off")));
        out.push_back (knob ({ 626.0f, a.getBottom() - 92.0f, 72.0f, 86.0f }, P ("Output"), tr ("Trim"), 0, true));
        const auto b = barArea();
        const auto y = b.getCentreY() - 11.0f;
        out.push_back (barValue ({ 8.0f, y, 110.0f, 22.0f }, P ("Dynamic"), tr ("Dynamic")));
        if (dynamicModel())
        {
            out.push_back (barValue ({ 490.0f, y, 110.0f, 22.0f }, P ("Mix"), tr ("Mix")));
            out.push_back (barValue ({ 604.0f, y, 110.0f, 22.0f }, P ("Output"), tr ("Trim")));
            return;
        }
        out.push_back (barValue ({ 122.0f, y, 120.0f, 22.0f }, P ("Exciter freq."), tr ("Mid freq")));
        out.push_back (barValue ({ 246.0f, y, 120.0f, 22.0f }, P ("Character"), tr ("Even / odd")));
        out.push_back (barValue ({ 370.0f, y, 116.0f, 22.0f }, P ("Air freq."), tr ("High freq")));
        out.push_back (barValue ({ 490.0f, y, 110.0f, 22.0f }, P ("Mix"), tr ("Mix")));
        out.push_back (barValue ({ 604.0f, y, 110.0f, 22.0f }, P ("Output"), tr ("Trim")));
    }

    void buttonPressed (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idLink) { linked = ! linked; repaint(); }
    }
    void customDown (const Ctl& c, const juce::MouseEvent&) override
    {
        if (c.id == idPower) toggleEnabled();
    }

    // --- the frosted panel (round tp172c: the reference's light lower half, drawn here from scratch) -------------
    static inline const juce::Colour kSlate { 0xff33414d }, kSlateDim { 0xff6a7884 };

    void frostFill (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto a = panel();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3edf2), 0.0f, a.getY(), juce::Colour (0xffc6d6df), 0.0f, a.getBottom(), false));
        g.fillRect (r);
    }

    static void frostDisc (juce::Graphics& g, juce::Rectangle<float> r, bool hover)
    {
        const auto R = r.getWidth() * 0.5f;
        for (int i = 4; i >= 1; --i) // a soft shadow under the disc
        {
            g.setColour (juce::Colour (0xff4f6878).withAlpha (0.05f));
            g.fillEllipse (r.translated (0.0f, R * 0.05f * (float) i).expanded (R * 0.025f * (float) i));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xffffffff : 0xfffbfdfe), r.getX() + R * 0.6f, r.getY() + R * 0.3f,
                                                 juce::Colour (0xffd7e3ea), r.getRight() - R * 0.3f, r.getBottom(), true));
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xffb7c7d1));
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        g.setColour (juce::Colours::white);
        g.drawEllipse (r.reduced (R * 0.07f), 1.0f);
    }

    void slateText (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, bool bold, juce::Colour col = kSlate) const
    {
        g.setColour (col);
        g.setFont (juce::Font (theme::font (size, bold)).withExtraKerningFactor (bold ? 0.08f : 0.0f));
        g.drawText (bold ? caps (t) : t, r, juce::Justification::centred, false);
    }

    // a disc knob: the value inside, a short pointer at the rim (angles as LYNX's knobs: −135°…+135°)
    void discKnob (juce::Graphics& g, juce::Rectangle<float> slot, juce::Rectangle<float> disc, int param, const juce::String& cap, bool big)
    {
        frostFill (g, slot);
        const auto hover = slot.contains (mouse);
        frostDisc (g, disc, hover || dragCtl == param);
        const auto R = disc.getWidth() * 0.5f;
        const auto c = disc.getCentre();
        const auto ang = juce::MathConstants<float>::pi * (-0.75f + 1.5f * v (param));
        const juce::Point<float> d { std::sin (ang), -std::cos (ang) };
        g.setColour (dragCtl == param ? accent.darker (0.3f) : kSlate);
        g.drawLine ({ c + d * R * 0.70f, c + d * R * 0.90f }, big ? 2.6f : 2.0f);
        if (big)
        {
            slateText (g, juce::Rectangle<float> (disc.getWidth(), 34.0f).withCentre (c.translated (0.0f, -8.0f)), text (param), 24.0f, false);
            slateText (g, juce::Rectangle<float> (disc.getWidth(), 14.0f).withCentre (c.translated (0.0f, 18.0f)), cap, 11.0f, true, kSlateDim);
        }
        else
        {
            slateText (g, disc, text (param), 10.5f, false);
            slateText (g, juce::Rectangle<float> (disc.getWidth() + 40.0f, 14.0f).withCentre ({ c.x, disc.getY() - 12.0f }), cap, 10.5f, true);
        }
    }

    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        const auto a = panel();
        const auto cy = a.getY() + 12.0f;
        // the two big discs (the slots drawn by layoutControls; their captions and values move inside the disc)
        for (const auto& [x, p, cap, top] : { std::tuple<float, int, juce::String, const char*> { 125.0f, pMid(), tr ("Mid air"), "100" },
                                              std::tuple<float, int, juce::String, const char*> { 445.0f, pHigh(), tr ("High air"), "15 dB" } })
        {
            const auto slot = juce::Rectangle<float> (x - 14.0f, cy + 10.0f, 178.0f, 184.0f);
            const auto disc = juce::Rectangle<float> (146.0f, 146.0f).withCentre ({ x + 75.0f, cy + 12.0f + 73.0f });
            discKnob (g, slot, disc, p, cap, true);
            // the scale's ends beside the disc's foot
            g.setColour (kSlateDim);
            g.setFont (theme::font (10.5f, true));
            g.drawText ("0", juce::Rectangle<float> (disc.getX() - 34.0f, disc.getBottom() - 16.0f, 30.0f, 14.0f), juce::Justification::centredRight, false);
            g.drawText (top, juce::Rectangle<float> (disc.getRight() + 4.0f, disc.getBottom() - 16.0f, 52.0f, 14.0f), juce::Justification::centredLeft, false);
        }
        // the trim: a small disc
        const auto trimSlot = juce::Rectangle<float> (612.0f, a.getBottom() - 112.0f, 100.0f, 108.0f);
        discKnob (g, trimSlot, juce::Rectangle<float> (46.0f, 46.0f).withCentre ({ 662.0f, a.getBottom() - 62.0f }), P ("Output"), tr ("Trim"), false);
        // the link: a frosted pill with two chain links, rimmed in the accent while linked
        const auto link = juce::Rectangle<float> (330.0f, cy + 70.0f, 60.0f, 28.0f);
        frostFill (g, link.expanded (2.0f));
        const auto hover = link.contains (mouse);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hover ? 0xffffffff : 0xfff6fafc), 0.0f, link.getY(), juce::Colour (0xffd5e1e8), 0.0f, link.getBottom(), false));
        g.fillRoundedRectangle (link, link.getHeight() * 0.5f);
        g.setColour (linked ? accent.darker (0.2f) : juce::Colour (0xffa9bbc6));
        g.drawRoundedRectangle (link.reduced (0.5f), link.getHeight() * 0.5f, linked ? 1.8f : 1.0f);
        g.setColour (linked ? accent.darker (0.35f) : kSlate);
        const auto lc = link.getCentre();
        for (const auto dx : { -5.0f, 5.0f })
            g.drawRoundedRectangle (juce::Rectangle<float> (13.0f, 8.0f).withCentre (lc.translated (dx, 0.0f)), 4.0f, 1.6f);
    }

    void paintCustom (juce::Graphics& g, const Ctl& c) override
    {
        if (c.id != idPower) return;
        const auto* fx = current();
        const auto on = fx != nullptr && fx->enabled;
        // round tp172c: a small frosted disc on the pale panel, the power glyph lit in the accent
        const auto r = c.r.reduced (2.0f);
        frostDisc (g, r, c.r.contains (mouse));
        juce::Path glyph;
        const auto cx = r.getCentreX(), cy = r.getCentreY(), rr = r.getWidth() * 0.22f;
        glyph.addCentredArc (cx, cy, rr, rr, 0.0f, 0.75f, juce::MathConstants<float>::twoPi - 0.75f, true);
        glyph.startNewSubPath (cx, cy - rr * 1.3f);
        glyph.lineTo (cx, cy - rr * 0.2f);
        g.setColour (on ? accent.darker (0.35f) : kSlateDim);
        g.strokePath (glyph, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        slateText (g, c.r.translated (0.0f, -18.0f).withHeight (14.0f).expanded (20.0f, 0.0f), tr ("Power"), 10.5f, true);
    }

    // the arc meter's scale: compressed at the bottom, open near 0 dB, like a programme meter
    static float arcFrac (float db)
    {
        static constexpr std::array<std::pair<float, float>, 8> marks { { { -120.0f, 0.0f }, { -96.0f, 0.08f }, { -48.0f, 0.26f }, { -24.0f, 0.46f },
                                                                          { -12.0f, 0.64f }, { -6.0f, 0.78f }, { -3.0f, 0.87f }, { 0.0f, 0.95f } } };
        if (db <= marks.front().first) return 0.0f;
        if (db >= 0.0f) return juce::jmin (1.0f, 0.95f + db * 0.0167f);
        for (std::size_t k = 1; k < marks.size(); ++k)
            if (db <= marks[k].first)
            {
                const auto [d0, f0] = marks[k - 1];
                const auto [d1, f1] = marks[k];
                return f0 + (f1 - f0) * (db - d0) / (d1 - d0);
            }
        return 0.95f;
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        const auto d = display();
        displayBackground (g, bodyArea());
        juce::ColourGradient bg (colour::bg1.brighter (0.06f), d.getCentreX(), d.getY(), colour::bg0, d.getCentreX(), d.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRect (d);

        // the output level (peak of the last 100 ms, with a hold)
        auto level = -120.0f, hold = -120.0f;
        for (int k = 0; k < 10; ++k) level = juce::jmax (level, columnBack (k).out);
        for (int k = 0; k < 150; ++k) hold = juce::jmax (hold, columnBack (k).out);
        if (! fx.enabled) level = hold = -120.0f;

        const juce::Point<float> centre { d.getCentreX(), d.getBottom() + 40.0f };
        const auto radius = d.getHeight() * 0.86f;
        const auto a0 = -0.36f * juce::MathConstants<float>::pi, a1 = 0.36f * juce::MathConstants<float>::pi;
        const auto angleOf = [&] (float db) { return a0 + (a1 - a0) * arcFrac (db); };
        const auto at = [&] (float angle, float rad) { return centre + juce::Point<float> { std::sin (angle), -std::cos (angle) } * rad; };

        // the track and the lit part (blue, red above −6 dB)
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff08090b));
        g.strokePath (track, juce::PathStrokeType (13.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour::bg4);
        g.strokePath (track, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (level > -119.0f)
        {
            const auto split = angleOf (-6.0f), now = angleOf (level);
            juce::Path lit;
            lit.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, a0, juce::jmin (now, split), true);
            g.setColour (accent.withAlpha (0.14f));
            g.strokePath (lit, juce::PathStrokeType (24.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setGradientFill (juce::ColourGradient (colour::info, centre.x - radius, centre.y, accent, centre.x + radius * 0.4f, centre.y - radius, false));
            g.strokePath (lit, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            if (now > split)
            {
                juce::Path hot;
                hot.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, split, now, true);
                g.setColour (colour::accent);
                g.strokePath (hot, juce::PathStrokeType (10.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            }
        }
        if (hold > -119.0f)
        {
            g.setColour (hold > -6.0f ? colour::accent : colour::textPrimary);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (at (angleOf (hold), radius)));
        }
        // the red zone and the scale
        juce::Path zone;
        zone.addCentredArc (centre.x, centre.y, radius + 12.0f, radius + 12.0f, 0.0f, angleOf (-6.0f), a1, true);
        g.setColour (colour::accent.withAlpha (0.6f));
        g.strokePath (zone, juce::PathStrokeType (2.0f));
        g.setFont (font (type::caption, true));
        for (const auto db : { -120.0f, -96.0f, -48.0f, -24.0f, -12.0f, -6.0f, -3.0f, 0.0f })
        {
            const auto angle = angleOf (db);
            g.setColour (db >= -6.0f ? colour::accent : colour::textSecondary);
            g.drawLine ({ at (angle, radius + 8.0f), at (angle, radius + 15.0f) }, 1.4f);
            g.drawText (db <= -120.0f ? juce::String::fromUTF8 ("-\xe2\x88\x9e") : juce::String ((int) db),
                        juce::Rectangle<float> (40.0f, 14.0f).withCentre (at (angle, radius + 26.0f)), juce::Justification::centred, false);
        }
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        g.drawText (level > -119.0f ? juce::String (level, 1) + " dB" : juce::String::fromUTF8 ("-\xe2\x88\x9e dB"),
                    juce::Rectangle<float> (160.0f, 22.0f).withCentre ({ centre.x, d.getBottom() - 46.0f }), juce::Justification::centred, false);
        caption (g, juce::Rectangle<float> (160.0f, 14.0f).withCentre ({ centre.x, d.getBottom() - 26.0f }), tr ("Output peak"));

        // the knob panel (round tp172c): a pale frosted plate under the dark display, as on the reference
        const auto a = panel();
        frostFill (g, a);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawHorizontalLine ((int) a.getY(), 0.0f, a.getRight());
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawHorizontalLine ((int) a.getY() + 1, 0.0f, a.getRight());
    }
};

// round tp172c7: Kiln Channel, LYNX's console channel — the compact dark single-channel console of its role, drawn here
// from scratch: near-black side columns (a stereo meter pair on a 0 .. 66 dB scale with -inf read-outs, IN / OUT with a
// big knob and its dB read-out), the brushed centre module with corner screws, LYNX's kiln emblem over an amber VU with a
// dual scale (VU over %), the name plate, Input / Output on dotted rings with the GROUP I/O and LINK I/O lamps between,
// the chicken-head console switch with its six models round it, Drive and Group with their numbers printed round them,
// and the Noise reduction / Group bypass keys with their labels below; the name and an info mark under the module.
class KilnEditor final : public ProEditor
{
public:
    KilnEditor (AudioEngine& e, int t, int s, bool d) : ProEditor (e, t, s, EffectKind::kilnChannel, d, 460, 680) {}

private:
    static constexpr int idParam = 100, idModel = 200, idInfo = 300;
    static constexpr float y0 = headerH; // the body's top

    juce::String title() const override { return "Kiln Channel"; }
    juce::String gestures() const override { return {}; }

    // geometry (design units = pixels at 100 %)
    static juce::Point<float> at (float x, float y) { return { x, y0 + y }; }
    static juce::Point<float> knobCentre (int p)
    {
        namespace k = mixsuite::kiln;
        switch (p)
        {
            case k::input: return at (172.0f, 208.0f);
            case k::output: return at (288.0f, 208.0f);
            case k::console: return at (230.0f, 316.0f);
            case k::drive: return at (178.0f, 432.0f);
            case k::group: return at (282.0f, 432.0f);
            case k::mainIn: return at (51.0f, 536.0f);
            case k::mainOut: return at (409.0f, 536.0f);
            default: return {};
        }
    }
    static float knobRadius (int p)
    {
        namespace k = mixsuite::kiln;
        return p == k::console ? 30.0f : (p == k::mainIn || p == k::mainOut) ? 25.0f : 18.0f;
    }
    static int stepsOf (int p) { namespace k = mixsuite::kiln; return p == k::console ? 5 : p == k::group ? 8 : 0; }
    static juce::Rectangle<float> lampRect (int p) { return juce::Rectangle<float> (20.0f, 13.0f).withCentre (at (230.0f, p == mixsuite::kiln::ioGroup ? 198.0f : 222.0f)); }
    static juce::Rectangle<float> keyRect (int p) { return juce::Rectangle<float> (50.0f, 22.0f).withCentre (at (p == mixsuite::kiln::noiseReduction ? 178.0f : 282.0f, 516.0f)); }
    static float stopAngle (int step, int steps) { return juce::MathConstants<float>::pi * (-0.75f + 1.5f * (float) step / (float) steps); }
    static juce::Point<float> polar (juce::Point<float> c, float a, float r) { return c + juce::Point<float> { std::sin (a), -std::cos (a) } * r; }
    juce::Rectangle<float> modelLabel (int step) const
    {
        const auto c = knobCentre (mixsuite::kiln::console);
        const auto a = stopAngle (step, 5);
        const auto p = polar (c, a, 50.0f);
        const auto w = 74.0f;
        const auto x = std::sin (a) < -0.2f ? p.x - w : std::sin (a) > 0.2f ? p.x : p.x - w * 0.5f;
        return { x, p.y - 7.0f, w, 14.0f };
    }

    void layoutControls (std::vector<Ctl>& out) const override
    {
        namespace k = mixsuite::kiln;
        for (const auto p : { k::input, k::output, k::console, k::drive, k::group, k::mainIn, k::mainOut })
        {
            const auto r = knobRadius (p) + 8.0f;
            out.push_back (custom (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (knobCentre (p)), idParam + p, -1,
                                   p == k::mainOut ? hintFor (p) + ". " + tr ("Power: switch the effect on or off") : hintFor (p)));
        }
        for (int s = 0; s < 6; ++s)
            out.push_back (custom (modelLabel (s), idModel + s, -1, tr ("Console") + ": " + utf8 (mixsuite::kiln::consoleName (s))));
        for (const auto p : { k::ioGroup, k::ioLink })
            out.push_back (custom (lampRect (p).expanded (8.0f, 4.0f), idParam + p, -1, hintFor (p)));
        for (const auto p : { k::noiseReduction, k::groupBypass })
            out.push_back (custom (keyRect (p).withHeight (40.0f), idParam + p, -1, hintFor (p)));
        out.push_back (custom (juce::Rectangle<float> (16.0f, 16.0f).withCentre (at (266.0f, 601.0f)), idInfo, -1,
                               tr ("Kiln Channel: LYNX's console channel. Drag a knob, Alt+click resets it, right-click for automation")));
    }

    juce::String hintFor (int p) const
    {
        const auto& info = effectInfo (kind);
        const auto name = tr (info.params[(std::size_t) p].name);
        const auto what = pluginheader::describe (kind, info.params[(std::size_t) p].name);
        return what.isNotEmpty() ? name + juce::String::fromUTF8 (" \xe2\x80\x94 ") + what : name;
    }

    // --- edits: Link I/O moves Output against Input; Group I/O carries the trims to the group's other channels with
    // Group I/O on, and Group bypass to every channel of the group (the original's keys move together)
    void edit (int p, float x)
    {
        namespace k = mixsuite::kiln;
        const auto* fx = current();
        if (fx == nullptr) return;
        x = juce::jlimit (0.0f, 1.0f, x);
        if (const auto steps = stepsOf (p); steps > 0)
            x = (float) std::lround (x * (float) steps) / (float) steps;
        bar.willEdit();
        const auto changes = k::linkedEdit (fx->params, p, x);
        const auto grp = k::groupOf (fx->params[(std::size_t) k::group]);
        std::vector<std::pair<int, float>> deltas;
        if (grp > 0 && fx->params[(std::size_t) k::ioGroup] >= 0.5f)
            for (const auto& [q, val] : changes)
                if (q == k::input || q == k::output)
                    deltas.push_back ({ q, val - fx->params[(std::size_t) q] });
        const bool bypassAll = grp > 0 && p == k::groupBypass;
        engine.editSong ([tk = track, sl = slot, changes, deltas, bypassAll, grp, x] (Song& song) {
            if (tk < 0 || tk >= (int) song.mixer.size() || sl < 0 || sl >= (int) song.mixer[(std::size_t) tk].effects.size()) return;
            auto& self = song.mixer[(std::size_t) tk].effects[(std::size_t) sl];
            for (const auto& [q, val] : changes)
                self.params[(std::size_t) q] = juce::jlimit (0.0f, 1.0f, val);
            if (deltas.empty() && ! bypassAll) return;
            for (auto& strip : song.mixer)
                for (auto& other : strip.effects)
                {
                    if (&other == &self || other.kind != EffectKind::kilnChannel || k::groupOf (other.params[(std::size_t) k::group]) != grp)
                        continue;
                    if (bypassAll)
                        other.params[(std::size_t) k::groupBypass] = x;
                    if (other.params[(std::size_t) k::ioGroup] >= 0.5f)
                        for (const auto& [q, d] : deltas)
                            other.params[(std::size_t) q] = juce::jlimit (0.0f, 1.0f, other.params[(std::size_t) q] + d);
                }
        });
    }

    int dragParam = -1;
    bool powerPending = false;
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (powerPending && ! e.mouseWasDraggedSinceMouseDown())
            toggleEnabled();
        powerPending = false;
        ProEditor::mouseUp (e);
    }
    bool dragging (int p) const { return dragParam == p && isMouseButtonDown(); }
    void customDown (const Ctl& c, const juce::MouseEvent& e) override
    {
        namespace k = mixsuite::kiln;
        dragParam = -1;
        if (c.id >= idModel && c.id < idModel + 6)
        {
            edit (k::console, k::consoleValue (c.id - idModel));
            return;
        }
        if (c.id < idParam || c.id >= idModel) return;
        const auto p = c.id - idParam;
        if (e.mods.isPopupMenu())
        {
            ui::showValueMenu (*this, &engine, specFor (p, stepsOf (p)), v (p), [this, p] (double x) { edit (p, (float) x); });
            return;
        }
        if (p == k::ioGroup || p == k::ioLink || p == k::noiseReduction || p == k::groupBypass)
        {
            edit (p, v (p) >= 0.5f ? 0.0f : 1.0f);
            return;
        }
        // the power symbol on the OUT knob (measured on the original, tp172c8): a click on it (no drag) switches the
        // plugin on / bypassed; a drag, from the symbol or the ring, moves the main output gain
        powerPending = p == k::mainOut && e.position.getDistanceFrom (knobCentre (p)) < knobRadius (p) * 0.55f;
        if (ui::isResetClick (e))
        {
            powerPending = false;
            edit (p, (float) specFor (p).defaultValue);
            return;
        }
        dragParam = p;
        valueDrag.begin (e, v (p));
    }
    void customDrag (const Ctl& c, const juce::MouseEvent& e) override
    {
        if (dragParam < 0) return;
        const auto spec = specFor (dragParam, stepsOf (dragParam));
        const auto x = (float) valueDrag.update (e, spec);
        edit (dragParam, x);
        ui::showValueTip (*this, c.r.toNearestInt(), spec.text (v (dragParam)));
    }
    bool bodyWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        Ctl c;
        if (! controlAt (e.position, c) || c.id < idParam || c.id >= idModel) return false;
        const auto p = c.id - idParam;
        if (knobRadius (p) <= 0.0f || knobCentre (p).y <= 0.0f) return false;
        const auto spec = specFor (p, stepsOf (p));
        if (const auto x = ui::wheelValue (v (p), e, wheel, spec))
        {
            edit (p, (float) *x);
            ui::flashValueTip (*this, c.r.toNearestInt(), spec.text (v (p)));
        }
        return true;
    }

    // --- metering: VU ballistics on the output, peak bars with holds on both sides
    float vuDb = -40.0f;
    std::array<float, 4> barDb { -120.0f, -120.0f, -120.0f, -120.0f }, holdDb { -120.0f, -120.0f, -120.0f, -120.0f };
    bool moving = false;
    void tick() override
    {
        const auto* fx = current();
        const auto on = fx != nullptr && fx->enabled;
        auto toDb = [] (float lin) { return lin > 1e-6f ? 20.0f * std::log10 (lin) : -120.0f; };
        moving = false;
        const auto vuTarget = on ? toDb (liveMeter (3)) + 18.0f - 3.0f : -40.0f; // 0 VU = -18 dBFS (sine RMS ~ peak - 3 dB)
        const auto vuNew = vuDb + (juce::jmax (-40.0f, vuTarget) - vuDb) * 0.3f;
        moving = moving || std::abs (vuNew - vuDb) > 0.05f;
        vuDb = vuNew;
        for (int i = 0; i < 4; ++i)
        {
            const auto now = on ? toDb (liveMeter (4 + i)) : -120.0f;
            const auto next = now > barDb[(std::size_t) i] ? now : juce::jmax (-120.0f, barDb[(std::size_t) i] - 1.5f); // ~45 dB/s fall
            moving = moving || std::abs (next - barDb[(std::size_t) i]) > 0.01f;
            barDb[(std::size_t) i] = next;
            holdDb[(std::size_t) i] = juce::jmax (holdDb[(std::size_t) i], now);
        }
        if (moving) repaint();
    }
    bool bodyDown (const juce::MouseEvent& e) override
    {
        // a click on a side column's read-outs resets the peak holds
        if (e.position.y < y0 + 64.0f && (e.position.x < 100.0f || e.position.x > 360.0f))
        {
            holdDb.fill (-120.0f);
            repaint();
            return true;
        }
        return false;
    }

    // --- drawing --------------------------------------------------------------------------------------------
    static void text (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, float size, bool bold, juce::Colour c,
                      juce::Justification j = juce::Justification::centred, float kern = 0.0f)
    {
        g.setColour (c);
        g.setFont (juce::Font (theme::font (size, bold)).withExtraKerningFactor (kern));
        g.drawText (t, r, j, false);
    }
    static inline const juce::Colour kWhite { 0xffeeeeea }, kGrey { 0xffa9abae }, kAmber { 0xfff3a93a };

    static void screw (juce::Graphics& g, juce::Point<float> c, float r)
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5b5d61), c.x - r, c.y - r, juce::Colour (0xff1a1b1d), c.x + r, c.y + r, false));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);
        g.setColour (juce::Colour (0xff0c0c0d));
        for (const auto a : { 0.6f, 0.6f + juce::MathConstants<float>::halfPi }) // a cross head
            g.drawLine ({ polar (c, a, r * 0.7f), polar (c, a + juce::MathConstants<float>::pi, r * 0.7f) }, 1.4f);
    }

    static void emblem (juce::Graphics& g, juce::Point<float> c, float r)
    {
        // LYNX's kiln mark: an arched kiln mouth with a flame inside, on a dark badge with a light rim
        const auto badge = juce::Rectangle<float> (r * 1.9f, r * 2.3f).withCentre (c);
        g.setColour (juce::Colour (0xff0b0b0c));
        g.fillRoundedRectangle (badge, r * 0.3f);
        g.setColour (juce::Colour (0xffc9cbcf));
        g.drawRoundedRectangle (badge.reduced (1.0f), r * 0.3f, 1.6f);
        const auto m = badge.reduced (r * 0.4f, r * 0.42f);
        juce::Path mouth;
        mouth.startNewSubPath (m.getX(), m.getBottom());
        mouth.lineTo (m.getX(), m.getY() + m.getHeight() * 0.42f);
        mouth.quadraticTo (m.getCentreX(), m.getY() - m.getHeight() * 0.2f, m.getRight(), m.getY() + m.getHeight() * 0.42f);
        mouth.lineTo (m.getRight(), m.getBottom());
        g.strokePath (mouth, juce::PathStrokeType (1.6f));
        juce::Path flame;
        const auto fx = m.getCentreX(), fb = m.getBottom() - 1.5f, fh = m.getHeight() * 0.62f, fw = m.getWidth() * 0.26f;
        flame.startNewSubPath (fx, fb - fh);
        flame.cubicTo (fx + fw * 1.4f, fb - fh * 0.45f, fx + fw, fb, fx, fb);
        flame.cubicTo (fx - fw, fb, fx - fw * 1.4f, fb - fh * 0.45f, fx, fb - fh);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd27a), fx, fb, juce::Colour (0xffd2461e), fx, fb - fh, false));
        g.fillPath (flame);
    }

    // a black console knob: a ridged skirt, a dark cap and a white pointer line
    void consoleKnob (juce::Graphics& g, juce::Point<float> c, float r, float value, bool hot) const
    {
        const auto skirt = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillEllipse (skirt.translated (0.0f, (float) i * 1.2f).expanded ((float) i));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff3e4044 : 0xff323337), c.x, c.y - r, juce::Colour (0xff0b0b0c), c.x, c.y + r, false));
        g.fillEllipse (skirt);
        g.setColour (juce::Colour (0xff060607));
        for (int i = 0; i < 24; ++i) // the knurl
        {
            const auto a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
            g.drawLine ({ polar (c, a, r * 0.84f), polar (c, a, r) }, 1.0f);
        }
        const auto cap = skirt.reduced (r * 0.2f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2d31), c.x - r * 0.4f, c.y - r * 0.6f, juce::Colour (0xff111113), c.x + r * 0.4f, c.y + r * 0.6f, false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawEllipse (cap, 1.0f);
        const auto a = juce::MathConstants<float>::pi * (-0.75f + 1.5f * value);
        g.setColour (kWhite);
        g.drawLine ({ polar (c, a, r * 0.25f), polar (c, a, r * 0.78f) }, juce::jmax (2.0f, r * 0.11f));
    }

    // the chicken-head selector: a round boss with a long tapered pointer
    void chickenHead (juce::Graphics& g, juce::Point<float> c, float r, int step, bool hot) const
    {
        const auto a = stopAngle (step, 5);
        for (int i = 3; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.2f));
            g.fillEllipse (juce::Rectangle<float> (r * 1.5f, r * 1.5f).withCentre (c).translated (0.0f, (float) i * 1.5f).expanded ((float) i));
        }
        juce::Path head;
        // the round boss and a long tapered beak toward the stop (one path; the fill joins them)
        head.addEllipse (-r * 0.74f, -r * 0.74f, r * 1.48f, r * 1.48f);
        head.startNewSubPath (-r * 0.5f, -r * 0.2f);
        head.quadraticTo (-r * 0.22f, -r * 0.9f, 0.0f, -r * 1.6f);
        head.quadraticTo (r * 0.22f, -r * 0.9f, r * 0.5f, -r * 0.2f);
        head.closeSubPath();
        const auto t = juce::AffineTransform::rotation (a).translated (c.x, c.y);
        g.setGradientFill (juce::ColourGradient (juce::Colour (hot ? 0xff3c3e42 : 0xff2e2f33), c.x, c.y - r, juce::Colour (0xff09090a), c.x, c.y + r, false));
        g.fillPath (head, t);
        g.setColour (juce::Colours::black);
        g.strokePath (head, juce::PathStrokeType (1.2f), t);
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawEllipse (juce::Rectangle<float> (r * 1.1f, r * 1.1f).withCentre (c), 1.0f);
        g.setColour (kWhite); // the pointer line along the beak
        g.drawLine ({ polar (c, a, r * 0.45f), polar (c, a, r * 1.45f) }, 2.2f);
    }

    void dottedRing (juce::Graphics& g, juce::Point<float> c, float r, int dots, int lit) const
    {
        for (int i = 0; i < dots; ++i)
        {
            const auto a = stopAngle (i, dots - 1);
            g.setColour (i == lit ? kWhite : kGrey.withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre (polar (c, a, r)));
        }
    }

    void hoverValue (juce::Graphics& g, int p, juce::Point<float> c, float r) const
    {
        if (! juce::Rectangle<float> ((r + 8.0f) * 2.0f, (r + 8.0f) * 2.0f).withCentre (c).contains (mouse) || dragging (p)) return;
        const auto t = ProEditor::text (p);
        const auto box = juce::Rectangle<float> (64.0f, 16.0f).withCentre (c.translated (0.0f, r + 18.0f));
        g.setColour (juce::Colour (0xe0101012));
        g.fillRoundedRectangle (box, 3.0f);
        text (g, box, t, 10.0f, false, kWhite);
    }

    void meterColumn (juce::Graphics& g, juce::Rectangle<float> col, int first, const juce::String& name, int mainParam) const
    {
        g.setColour (juce::Colour (0xff0d0d0e));
        g.fillRoundedRectangle (col, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (col.reduced (0.5f), 6.0f, 1.0f);
        // the well: two thin bars, the scale between them, the peak read-outs on top
        const auto well = juce::Rectangle<float> (col.getX() + 10.0f, col.getY() + 8.0f, col.getWidth() - 20.0f, 384.0f);
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (well, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (well, 4.0f, 1.0f);
        const auto top = well.getY() + 34.0f, pxPerDb = 5.0f;
        const float barX[2] { well.getCentreX() - 25.0f, well.getCentreX() + 19.0f }; // left bar | numbers | right bar
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto h = holdDb[(std::size_t) (first + ch)];
            text (g, { barX[ch] - 14.0f, well.getY() + 8.0f, 34.0f, 12.0f }, h > -119.0f ? juce::String (h, 1) : juce::String ("-inf"), 9.0f, true,
                  h > -0.1f ? juce::Colour (0xffe5383b) : kGrey);
            const auto slot = juce::Rectangle<float> (barX[ch], top - 4.0f, 6.0f, 66.0f * pxPerDb + 8.0f);
            g.setColour (juce::Colour (0xff1b1c1f));
            g.fillRect (slot);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRect (slot, 1.0f);
            const auto db = barDb[(std::size_t) (first + ch)];
            if (db > -66.0f)
            {
                const auto y = top + juce::jlimit (0.0f, 66.0f, -db) * pxPerDb;
                const auto lit = juce::Rectangle<float> (slot.getX() + 1.0f, y, slot.getWidth() - 2.0f, slot.getBottom() - 4.0f - y);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2fbf8f), 0.0f, top + 66.0f * pxPerDb, juce::Colour (0xff7be0b9), 0.0f, top, false));
                g.fillRect (lit);
                if (db > -6.0f)
                {
                    g.setColour (juce::Colour (0xffe8b23a));
                    g.fillRect (lit.withHeight (juce::jmin (lit.getHeight(), (6.0f + db) * pxPerDb)));
                }
            }
        }
        for (const auto mark : { 0, 6, 12, 18, 24, 36, 48, 66 })
        {
            const auto y = top + (float) mark * pxPerDb;
            text (g, { well.getCentreX() - 14.0f, y - 6.0f, 28.0f, 12.0f }, juce::String (mark), 9.5f, true, kWhite.withAlpha (0.85f));
            g.setColour (kGrey.withAlpha (0.5f));
            for (const auto x : { barX[0] + 8.0f, barX[1] - 5.0f })
                g.drawHorizontalLine ((int) y, x, x + 3.0f);
        }
        // IN / OUT, the big knob, its read-out
        text (g, { col.getX(), y0 + 452.0f, col.getWidth(), 22.0f }, name, 15.0f, true, kWhite, juce::Justification::centred, 0.08f);
        const auto c = knobCentre (mainParam);
        consoleKnob (g, c, knobRadius (mainParam), v (mainParam), juce::Rectangle<float> (60.0f, 60.0f).withCentre (c).contains (mouse));
        if (mainParam == mixsuite::kiln::mainOut) // the power symbol: lit while the plugin runs, dim while bypassed
        {
            const auto* fx = current();
            const auto on = fx != nullptr && fx->enabled;
            const auto rr = knobRadius (mainParam) * 0.3f;
            juce::Path glyph;
            glyph.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            glyph.startNewSubPath (c.x, c.y - rr * 1.25f);
            glyph.lineTo (c.x, c.y - rr * 0.25f);
            g.setColour (juce::Colour (0xff141416));
            g.fillEllipse (juce::Rectangle<float> (rr * 3.4f, rr * 3.4f).withCentre (c));
            g.setColour (on ? kWhite : kGrey.withAlpha (0.5f));
            g.strokePath (glyph, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        text (g, { col.getX(), c.y + 38.0f, col.getWidth(), 16.0f }, juce::String (mixsuite::kiln::mainDb (v (mainParam)), 2) + "dB", 11.0f, true, kWhite);
    }

    void vu (juce::Graphics& g, juce::Rectangle<float> r, bool on) const
    {
        // the bezel, the lamp-lit amber face, VU over %, the needle from a pivot under the face
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r.expanded (5.0f), 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (r.expanded (5.0f).reduced (0.5f), 7.0f, 1.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xfffbd88a : 0xffb59a63), r.getCentreX(), r.getCentreY() + r.getHeight() * 0.1f,
                                                 juce::Colour (on ? 0xffc88c2e : 0xff6e5532), r.getX(), r.getY(), true));
        g.fillRoundedRectangle (r, 4.0f);
        const juce::Point<float> pivot { r.getCentreX(), r.getBottom() + r.getHeight() * 0.32f };
        const auto R = r.getHeight() * 1.02f;
        const auto fracOf = [] (float db) {
            const auto lo = std::pow (10.0f, -1.0f), hi = std::pow (10.0f, 3.0f / 20.0f);
            return juce::jlimit (0.0f, 1.0f, (std::pow (10.0f, db / 20.0f) - lo) / (hi - lo));
        };
        const auto angleOf = [] (float frac) { return juce::MathConstants<float>::pi * (-0.26f + 0.52f * frac); };
        const juce::Colour ink (0xff2a2016), red (0xffc0281e);
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, R * 0.8f, R * 0.8f, 0.0f, angleOf (0.0f), angleOf (1.0f), true);
        g.setColour (ink);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        juce::Path redArc;
        redArc.addCentredArc (pivot.x, pivot.y, R * 0.82f, R * 0.82f, 0.0f, angleOf (fracOf (0.0f)), angleOf (1.0f), true);
        g.setColour (red);
        g.strokePath (redArc, juce::PathStrokeType (3.0f));
        for (const auto db : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
        {
            const auto a = angleOf (fracOf (db));
            g.setColour (db > 0.0f ? red : ink);
            g.drawLine ({ polar (pivot, a, R * 0.8f), polar (pivot, a, R * 0.88f) }, 1.2f);
            text (g, juce::Rectangle<float> (18.0f, 10.0f).withCentre (polar (pivot, a, R * 0.95f)), db > 0.0f ? juce::String ((int) db) : juce::String ((int) -db),
                  7.5f, true, db > 0.0f ? red : ink);
        }
        for (int pc = 0; pc <= 100; pc += 20) // the % scale under the arc
        {
            const auto a = angleOf ((float) pc / 100.0f * fracOf (0.0f));
            g.setColour (ink);
            g.drawLine ({ polar (pivot, a, R * 0.72f), polar (pivot, a, R * 0.78f) }, 1.0f);
            text (g, juce::Rectangle<float> (18.0f, 9.0f).withCentre (polar (pivot, a, R * 0.66f)), juce::String (pc), 6.5f, false, ink);
        }
        text (g, juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ r.getX() + 16.0f, r.getBottom() - 14.0f }), "VU", 9.0f, true, ink);
        text (g, juce::Rectangle<float> (40.0f, 10.0f).withCentre ({ r.getRight() - 18.0f, r.getBottom() - 14.0f }), "%", 8.0f, true, ink);
        const auto a = angleOf (fracOf (juce::jlimit (-22.0f, 4.0f, vuDb)));
        g.saveState();
        g.reduceClipRegion (r.toNearestInt());
        g.setColour (juce::Colour (0xff17120d));
        g.drawLine ({ polar (pivot, a, R * 0.32f), polar (pivot, a, R * 0.98f) }, 1.4f);
        g.restoreState();
        g.setColour (juce::Colours::black.withAlpha (0.45f)); // the face's inner shadow
        g.drawRoundedRectangle (r, 4.0f, 2.0f);
    }

    void key (juce::Graphics& g, int p, const juce::String& label) const
    {
        const auto r = keyRect (p);
        const auto on = v (p) >= 0.5f;
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setGradientFill (on ? juce::ColourGradient (juce::Colour (0xffffd78a), r.getCentreX(), r.getCentreY(), juce::Colour (0xffd8801e), r.getX(), r.getY(), true)
                              : juce::ColourGradient (juce::Colour (0xff8d8f93), 0.0f, r.getY(), juce::Colour (0xff4b4d51), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (on ? juce::Colour (0xfffff1cf).withAlpha (0.8f) : juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (2.5f), 1.5f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        text (g, { r.getCentreX() - 60.0f, r.getBottom() + 7.0f, 120.0f, 12.0f }, label, 9.0f, true, kWhite, juce::Justification::centred, 0.04f);
    }

    void lamp (juce::Graphics& g, int p) const
    {
        const auto r = lampRect (p);
        const auto on = v (p) >= 0.5f;
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (r.expanded (2.0f), 2.0f);
        if (on)
        {
            g.setColour (kAmber.withAlpha (0.25f));
            g.fillRoundedRectangle (r.expanded (5.0f), 5.0f);
        }
        g.setGradientFill (on ? juce::ColourGradient (juce::Colour (0xffffe3a0), r.getCentreX(), r.getCentreY(), juce::Colour (0xffe08a1c), r.getX(), r.getY(), true)
                              : juce::ColourGradient (juce::Colour (0xff4a3a24), r.getCentreX(), r.getCentreY(), juce::Colour (0xff221a10), r.getX(), r.getY(), true));
        g.fillRoundedRectangle (r, 1.5f);
    }

    // the shared bottom bar carries nothing here: the panel's black runs down to the description line
    void paintOverlay (juce::Graphics& g, const EffectSlot&) override
    {
        g.setColour (juce::Colour (0xff060606));
        g.fillRect (barArea());
    }

    void paintBody (juce::Graphics& g, const EffectSlot& fx) override
    {
        namespace k = mixsuite::kiln;
        const auto body = bodyArea();
        g.setColour (juce::Colour (0xff060606));
        g.fillRect (body);
        meterColumn (g, { 6.0f, y0 + 6.0f, 90.0f, 610.0f }, 0, caps (tr ("In")), k::mainIn);
        meterColumn (g, { 364.0f, y0 + 6.0f, 90.0f, 610.0f }, 2, caps (tr ("Out")), k::mainOut);

        // the centre column: its frame, the module, the name under it
        const auto column = juce::Rectangle<float> (102.0f, y0 + 6.0f, 256.0f, 610.0f);
        g.setColour (juce::Colour (0xff0d0d0e));
        g.fillRoundedRectangle (column, 6.0f);
        const auto mod = juce::Rectangle<float> (108.0f, y0 + 10.0f, 244.0f, 578.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2b2e), mod.getX(), mod.getY(), juce::Colour (0xff161719), mod.getRight(), mod.getBottom(), false));
        g.fillRect (mod);
        juce::Random rnd (172);
        for (int i = 0; i < 1800; ++i) // the brushing: a fine, low-contrast grain (short strokes, barely lighter or darker)
        {
            const auto x = mod.getX() + rnd.nextFloat() * mod.getWidth();
            const auto y = mod.getY() + rnd.nextFloat() * mod.getHeight();
            const auto len = 3.0f + rnd.nextFloat() * 22.0f;
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.008f + 0.016f * rnd.nextFloat()));
            g.drawVerticalLine ((int) x, y, juce::jmin (mod.getBottom(), y + len));
        }
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), mod.getCentreX(), mod.getY() + 60.0f, juce::Colours::transparentWhite,
                                                 mod.getCentreX(), mod.getY() + 340.0f, true));
        g.fillRect (mod);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRect (mod, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawRect (mod.reduced (2.0f), 1.0f);
        for (const auto& c : { mod.getTopLeft().translated (11.0f, 11.0f), mod.getTopRight().translated (-11.0f, 11.0f),
                               mod.getBottomLeft().translated (11.0f, -11.0f), mod.getBottomRight().translated (-11.0f, -11.0f) })
            screw (g, c, 6.0f);
        emblem (g, at (230.0f, 30.0f), 12.0f);
        vu (g, { 160.0f, y0 + 54.0f, 140.0f, 78.0f }, fx.enabled);
        text (g, { mod.getX(), y0 + 144.0f, mod.getWidth(), 24.0f }, "KILN CHANNEL", 17.0f, false, kWhite, juce::Justification::centred, 0.06f);

        // Input / Output: dotted rings, 0 on top, -18 / +18 at the ends, the caption under
        for (const auto [p, cap] : { std::pair<int, const char*> { k::input, TR_NOOP ("Input") }, std::pair<int, const char*> { k::output, TR_NOOP ("Output") } })
        {
            const auto c = knobCentre (p);
            dottedRing (g, c, 26.0f, 11, -1);
            text (g, juce::Rectangle<float> (20.0f, 10.0f).withCentre (polar (c, 0.0f, 34.0f)), "0", 8.5f, true, kWhite);
            text (g, juce::Rectangle<float> (24.0f, 10.0f).withCentre (polar (c, stopAngle (0, 1), 30.0f).translated (-4.0f, 9.0f)), "-18", 8.5f, true, kWhite);
            text (g, juce::Rectangle<float> (24.0f, 10.0f).withCentre (polar (c, stopAngle (1, 1), 30.0f).translated (4.0f, 9.0f)), "+18", 8.5f, true, kWhite);
            consoleKnob (g, c, knobRadius (p), v (p), dragging (p));
            text (g, { c.x - 40.0f, c.y + 30.0f, 80.0f, 12.0f }, caps (tr (cap)), 9.0f, true, kWhite, juce::Justification::centred, 0.04f);
        }
        // GROUP I/O over LINK I/O, between them
        text (g, juce::Rectangle<float> (80.0f, 10.0f).withCentre (at (230.0f, 184.0f)), caps (tr ("Group I/O")), 8.0f, true, kWhite, juce::Justification::centred, 0.04f);
        lamp (g, k::ioGroup);
        lamp (g, k::ioLink);
        text (g, juce::Rectangle<float> (80.0f, 10.0f).withCentre (at (230.0f, 237.0f)), caps (tr ("Link I/O")), 8.0f, true, kWhite, juce::Justification::centred, 0.04f);

        // the console switch and its six models
        const auto cc = knobCentre (k::console);
        const auto step = k::consoleStep (v (k::console));
        for (int s = 0; s < 6; ++s)
        {
            const auto a = stopAngle (s, 5);
            g.setColour (s == step ? kWhite : kGrey);
            g.drawLine ({ polar (cc, a, 36.0f), polar (cc, a, 44.0f) }, s == step ? 1.8f : 1.2f);
            const auto lr = modelLabel (s);
            const auto j = lr.getCentreX() < cc.x - 10.0f ? juce::Justification::centredRight : lr.getCentreX() > cc.x + 10.0f ? juce::Justification::centredLeft : juce::Justification::centred;
            text (g, lr.reduced (2.0f, 0.0f), tr (k::consoleName (s)), 11.0f, false, kWhite, j);
        }
        chickenHead (g, cc, knobRadius (k::console), step, dragging (k::console) || juce::Rectangle<float> (76.0f, 76.0f).withCentre (cc).contains (mouse));

        // Drive and Group with their numbers round them
        {
            const auto c = knobCentre (k::drive);
            for (int i = 0; i <= 6; ++i)
                text (g, juce::Rectangle<float> (18.0f, 10.0f).withCentre (polar (c, stopAngle (i, 6), 30.0f)), juce::String (i * 3), 8.0f, true, kWhite);
            consoleKnob (g, c, knobRadius (k::drive), v (k::drive), dragging (k::drive));
            text (g, { c.x - 40.0f, c.y + 32.0f, 80.0f, 12.0f }, caps (tr ("Drive")), 9.0f, true, kGrey, juce::Justification::centred, 0.04f);
        }
        {
            const auto c = knobCentre (k::group);
            for (int i = 0; i <= 8; ++i)
                text (g, juce::Rectangle<float> (i == 0 ? 30.0f : 14.0f, 10.0f).withCentre (polar (c, stopAngle (i, 8), i == 0 ? 33.0f : 30.0f)),
                      i == 0 ? tr ("None") : juce::String (i), 8.0f, true, kWhite);
            consoleKnob (g, c, knobRadius (k::group), (float) k::groupOf (v (k::group)) / 8.0f, dragging (k::group));
            text (g, { c.x - 40.0f, c.y + 32.0f, 80.0f, 12.0f }, caps (tr ("Group")), 9.0f, true, kGrey, juce::Justification::centred, 0.04f);
        }
        key (g, k::noiseReduction, caps (tr ("Noise reduction")));
        key (g, k::groupBypass, caps (tr ("Group bypass")));

        // the name and the info mark under the module
        text (g, { column.getX(), y0 + 590.0f, column.getWidth() - 24.0f, 22.0f }, "Kiln Channel", 12.0f, true, kWhite);
        const auto info = juce::Rectangle<float> (13.0f, 13.0f).withCentre (at (266.0f, 601.0f));
        g.setColour (kWhite);
        g.drawEllipse (info, 1.2f);
        text (g, info, "i", 9.0f, true, kWhite);

        for (const auto p : { k::input, k::output, k::drive, k::group, k::mainIn, k::mainOut })
            hoverValue (g, p, knobCentre (p), knobRadius (p));
    }
};

#include "ui/ProEditorsStrips.inc"
#include "ui/ProEditorsStrike.inc" // round trans189d: Strike Shaper
#include "ui/PocketEditors.inc"

// --- windows -------------------------------------------------------------------------------------------------
std::unique_ptr<ProEditor> make (AudioEngine& engine, int track, int slot, bool demo)
{
    const auto& mixer = engine.getSong().mixer;
    if (track < 0 || track >= (int) mixer.size() || slot < 0 || slot >= (int) mixer[(std::size_t) track].effects.size())
        return nullptr;
    switch (mixer[(std::size_t) track].effects[(std::size_t) slot].kind)
    {
        case EffectKind::apexLimiter: return std::make_unique<ApexEditor> (engine, track, slot, demo);
        case EffectKind::hushDeEsser: return std::make_unique<HushEditor> (engine, track, slot, demo);
        case EffectKind::crestCompressor: return std::make_unique<CrestEditor> (engine, track, slot, demo);
        case EffectKind::sentryGate: return std::make_unique<SentryEditor> (engine, track, slot, demo);
        case EffectKind::prismMultiband: return std::make_unique<PrismEditor> (engine, track, slot, demo);
        case EffectKind::ember: return std::make_unique<EmberEditor> (engine, track, slot, demo);
        case EffectKind::aether: return std::make_unique<AetherEditor> (engine, track, slot, demo);
        case EffectKind::kilnChannel: return std::make_unique<KilnEditor> (engine, track, slot, demo);
        case EffectKind::fretChannel: return std::make_unique<FretEditor> (engine, track, slot, demo);
        case EffectKind::shellChannel: return std::make_unique<ShellEditor> (engine, track, slot, demo);
        case EffectKind::strikeShaper: return std::make_unique<StrikeEditor> (engine, track, slot, demo);
        case EffectKind::pocketDistortion: return std::make_unique<PocketDistortionEditor> (engine, track, slot, demo);
        case EffectKind::pocketLimiterClassic: return std::make_unique<PocketLimiterClassicEditor> (engine, track, slot, demo);
        case EffectKind::pocketLimiter: return std::make_unique<PocketLimiterEditor> (engine, track, slot, demo);
        case EffectKind::pocketTranceDelay: return std::make_unique<PocketTranceDelayEditor> (engine, track, slot, demo);
        case EffectKind::pocketLeveller: return std::make_unique<PocketLevellerEditor> (engine, track, slot, demo);
        case EffectKind::pocketGate: return std::make_unique<PocketGateEditor> (engine, track, slot, demo);
        case EffectKind::pocketTapeDelay: return std::make_unique<PocketTapeDelayEditor> (engine, track, slot, demo);
        case EffectKind::pocketMultiFx: return std::make_unique<PocketMultiFxEditor> (engine, track, slot, demo);
        default: return nullptr;
    }
}

// The editor drawn at the window's size (round 128: the originals' windows can be resized): the layout is fixed, the
// whole editor is scaled (mouse positions follow the transform), the window keeps the editor's proportions.
class ScaledHolder final : public juce::Component
{
public:
    explicit ScaledHolder (std::unique_ptr<ProEditor> e) : editor (std::move (e))
    {
        baseW = juce::jmax (1, editor->getWidth());
        baseH = juce::jmax (1, editor->getHeight());
        addAndMakeVisible (*editor);
        setSize (baseW, baseH);
    }
    void resized() override
    {
        const auto s = juce::jmin ((float) getWidth() / (float) baseW, (float) getHeight() / (float) baseH);
        editor->setBounds (0, 0, baseW, baseH);
        editor->setTransform (juce::AffineTransform::scale (s));
    }
    std::unique_ptr<ProEditor> editor;
    int baseW = 1, baseH = 1;
};

// round 190bj (QA: the Apex Limiter's bottom status row was cut off by the window edge at its default size): JUCE's
// setFixedAspectRatio applies the ratio to the whole window, native title bar and borders included, so the content was
// about 30 px shorter than the editor; and the open animation then reset the holder's scale, so the editor was cut
// instead of scaled. The ratio now applies to the content (core/WindowFit.h aspectContent).
class ContentAspectConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    ContentAspectConstrainer (juce::Component& w, int bw, int bh) : window (w), baseW (bw), baseH (bh) {}

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>&, const juce::Rectangle<int>&,
                      bool top, bool left, bool bottom, bool right) override
    {
        // (JUCE passes the outer bounds: the content plus the native frame)
        juce::BorderSize<int> frame;
        if (auto* peer = window.getPeer())
            if (const auto size = peer->getFrameSizeIfPresent())
                frame = *size;
        const auto out = windowfit::aspectContent ({ bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight() },
                                                   { frame.getLeft(), frame.getTop(), frame.getRight(), frame.getBottom() },
                                                   baseW, baseH, minScale, maxScale, top, left, bottom, right);
        bounds = { out.x, out.y, out.w, out.h };
    }

    static constexpr double minScale = 0.7, maxScale = 2.0;

private:
    juce::Component& window;
    int baseW, baseH;
};

class ProEditorWindow final : public juce::DocumentWindow
{
public:
    ProEditorWindow (std::unique_ptr<ProEditor> editor, const juce::String& title, AudioEngine& engine, int track, int slot)
        : DocumentWindow (title, colour::bg1, DocumentWindow::closeButton | DocumentWindow::minimiseButton),
          gone (engine, track, slot, [this] { closeButtonPressed(); })
    {
        setUsingNativeTitleBar (true);
        content = editor.get();
        auto holder = std::make_unique<ScaledHolder> (std::move (editor));
        const auto w = holder->baseW, h = holder->baseH;
        setContentOwned (holder.release(), true);
        aspect = std::make_unique<ContentAspectConstrainer> (*this, w, h);
        setConstrainer (aspect.get());
        setResizable (true, false);
        centreWithSize (w, h);
        setVisible (true);
        dockOrKeepAbove (*this); // FL's docked plugin window (round 190ev)
        // round 126: fade / scale in — on the holder: the editor's own transform is the holder's scale (round 190bj)
        if (content != nullptr)
            if (auto* holder = getContentComponent())
                skin::playOpenAnimation (*holder, content->editorStyle());
    }
    ~ProEditorWindow() override { setConstrainer (nullptr); }
    void closeButtonPressed() override;
    ProEditor* content = nullptr;

private:
    ui::SlotGoneWatcher gone;
    std::unique_ptr<ContentAspectConstrainer> aspect;
};

std::map<std::pair<int, int>, std::unique_ptr<ProEditorWindow>>& windows()
{
    static std::map<std::pair<int, int>, std::unique_ptr<ProEditorWindow>> open;
    return open;
}

void ProEditorWindow::closeButtonPressed()
{
    for (auto& [key, window] : windows())
        if (window.get() == this)
        {
            juce::MessageManager::callAsync ([k = key] { windows().erase (k); });
            return;
        }
}
} // namespace

namespace ProEditorWindows
{
bool handles (EffectKind kind) noexcept
{
    return kind == EffectKind::apexLimiter || kind == EffectKind::hushDeEsser || kind == EffectKind::crestCompressor
           || kind == EffectKind::sentryGate || kind == EffectKind::prismMultiband || kind == EffectKind::ember
           || kind == EffectKind::aether || kind == EffectKind::kilnChannel || kind == EffectKind::fretChannel
           || kind == EffectKind::shellChannel || kind == EffectKind::strikeShaper
           || kind == EffectKind::pocketDistortion || kind == EffectKind::pocketLimiterClassic
           || kind == EffectKind::pocketLimiter || kind == EffectKind::pocketTranceDelay || kind == EffectKind::pocketLeveller
           || kind == EffectKind::pocketGate || kind == EffectKind::pocketTapeDelay || kind == EffectKind::pocketMultiFx;
}

void open (AudioEngine& engine, int track, int slot)
{
    auto& open = windows();
    const auto key = std::make_pair (track, slot);
    if (auto it = open.find (key); it != open.end())
    {
        if (it->second->content != nullptr && it->second->content->current() != nullptr)
        {
            it->second->toFront (true);
            return;
        }
        open.erase (it); // the slot now holds another effect
    }
    auto editor = make (engine, track, slot, false);
    if (editor == nullptr) return;
    const auto& mixer = engine.getSong().mixer;
    const auto name = track == 0 ? tr ("Master") : juce::String::fromUTF8 (mixer[(std::size_t) track].name.c_str());
    const auto title = utf8 (effectInfo (mixer[(std::size_t) track].effects[(std::size_t) slot].kind).name) + " - "
                       + (name.isNotEmpty() ? name : tr ("Insert {0}", { juce::String (track) })) + " (" + tr ("slot {0}", { juce::String (slot + 1) }) + ")";
    open[key] = std::make_unique<ProEditorWindow> (std::move (editor), title, engine, track, slot);
}

juce::Image windowContentForSnapshot (AudioEngine& engine, int track, int slot, bool juceAspect)
{
    auto editor = make (engine, track, slot, true);
    if (editor == nullptr)
        return {};
    ScaledHolder holder (std::move (editor));
    const auto w = holder.baseW, h = holder.baseH;
    const windowfit::Frame frame { 8, 31, 8, 8 }; // a native Windows 10/11 frame at 100 % (title bar, borders)
    windowfit::Box outer { 100, 100, w + frame.left + frame.right, h + frame.top + frame.bottom };
    if (juceAspect) // before round 190bj: JUCE's fixed ratio on the whole window, then the open animation's end
    {
        juce::ComponentBoundsConstrainer c;
        c.setFixedAspectRatio ((double) w / (double) h);
        c.setMinimumSize (juce::roundToInt (w * 0.7), juce::roundToInt (h * 0.7));
        c.setMaximumSize (w * 2, h * 2);
        juce::Rectangle<int> b (outer.x, outer.y, outer.w, outer.h);
        c.checkBounds (b, b, { 0, 0, 10000, 10000 }, false, false, false, false);
        outer = { b.getX(), b.getY(), b.getWidth(), b.getHeight() };
    }
    else
        outer = windowfit::aspectContent (outer, frame, w, h, ContentAspectConstrainer::minScale, ContentAspectConstrainer::maxScale);
    holder.setSize (outer.w - frame.left - frame.right, outer.h - frame.top - frame.bottom);
    if (juceAspect)
        holder.editor->setTransform ({}); // (the animation reset the editor's transform: the holder's scale was lost)
    juce::Image image (juce::Image::ARGB, holder.getWidth(), holder.getHeight(), true);
    {
        juce::Graphics g (image);
        g.fillAll (colour::bg1);
        holder.paintEntireComponent (g, false);
    }
    return image;
}

void close (int track, int slot) { windows().erase ({ track, slot }); }
void closeAll() { windows().clear(); }
} // namespace ProEditorWindows

std::unique_ptr<juce::Component> makeProEditor (AudioEngine& engine, int track, int slot, bool demo)
{
    return make (engine, track, slot, demo);
}
} // namespace lynx
