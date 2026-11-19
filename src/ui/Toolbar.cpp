#include "ui/Toolbar.h"

#include <juce_dsp/juce_dsp.h>

#include "core/ClipLauncher.h"
#include "core/CountIn.h"
#include "core/Details190mv.h" // (round 190mv: Master pitch in Hz)
#include "core/LayoutStyle.h"
#include "core/MarkerPlan.h"
#include "core/Timecode.h"
#include "core/TempoMap.h"
#include "core/ValueMath.h" // (round 190mv: the phase nudge)
#include "ui/FrameTicker.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/PaintAB.h"
#include "ui/Theme.h"
#include "ui/TextCache.h"

namespace lynx::toolbar
{
namespace
{
// --- round 190iv: Live's control-bar boxes and BandLab's capsule text (shared by the fields below) ---
namespace live
{
const juce::Colour band { 0xff2a2a2a }, box { 0xff363636 }, boxHover { 0xff464646 }, text { 0xffb5b5b5 }, lit { 0xffffad56 }, onLit { 0xff070707 };
// the count-in in the position field (Live 12 manual 19.6 "in blue"): filmed in Live 12.4 (round 190ld, details2_190ld
// shots/ref/live-countin-*.png) the whole field turns #03c3d5 with the count in Live's #070707 ink (190kn guessed blue text)
const juce::Colour countIn { 0xff03c3d5 };
}
void paintLiveBox (juce::Graphics& g, juce::Rectangle<int> r, bool hover, bool lit = false)
{
    g.setColour (lit ? live::lit : hover ? live::boxHover : live::box);
    g.fillRect (r);
}
void paintLiveText (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, bool lit = false,
                    juce::Justification j = juce::Justification::centred)
{
    g.setColour (lit ? live::onLit : live::text);
    g.setFont (theme::font (theme::type::caption));
    g.drawFittedText (t, r.reduced (3, 0), j, 1, 0.75f);
}
// a small "▾" at the right of a box
void paintLiveArrow (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c)
{
    const auto a = r.toFloat().removeFromRight (10.0f).withSizeKeepingCentre (6.0f, 3.5f);
    juce::Path tri;
    tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
    g.setColour (c);
    g.fillPath (tri);
}
// the hover wash of a capsule segment (the capsule's fill is drawn behind by the main window)
void paintCapsuleHover (juce::Graphics& g, juce::Rectangle<int> r, bool hover, bool roundRight = false)
{
    if (hover)
    {
        const auto f = r.toFloat();
        const auto radius = f.getHeight() / 2.0f;
        juce::Path p;
        p.addRoundedRectangle (f.getX(), f.getY(), f.getWidth(), f.getHeight(), radius, radius, false, roundRight, false, roundRight);
        g.setColour (theme::colour::bg5);
        g.fillPath (p);
    }
}
// Round 190jo (design190ia FM-6, details190if DF-18): FL's time panel in B:S:T, "1:01:00" = bar 1, step 01, tick 00. The
// steps are the sixteenths of the bar (01-16 in 4/4, with the Playlist's time signatures in Song mode) and the ticks are
// FL's at its default 96 PPQ: 24 per step (00-23).
juce::String barStepTick (const Song& song, double ticks, Meter meter)
{
    int bar = 0;
    double intoBar = 0.0;
    if (song.mode == PlayMode::song)
    {
        const auto current = song.barAt ((std::int64_t) ticks);
        bar = current.number;
        intoBar = ticks - (double) current.tick;
    }
    else
    {
        const auto perBar = (double) meter.bar();
        bar = (int) (ticks / perBar) + 1;
        intoBar = std::fmod (ticks, perBar);
    }
    intoBar = juce::jmax (0.0, intoBar);
    const auto step = (int) (intoBar / ticksPerStep) + 1;
    constexpr int flTicksPerStep = 24; // (FL: 96 PPQ / 4 steps a beat)
    const auto tick = juce::jlimit (0, flTicksPerStep - 1, (int) (std::fmod (intoBar, (double) ticksPerStep) * flTicksPerStep / ticksPerStep));
    return juce::String::formatted ("%d:%02d:%02d", bar, step, tick);
}

juce::String barBeatStep (const Song& song, double ticks, Meter meter)
{
    // (round 190fg: beats of the meter in force, eighths in 6/8; the steps are sixteenths into the beat)
    int bar = 0;
    double intoBar = 0.0;
    auto beatLength = (double) meter.beat();

    if (song.mode == PlayMode::song) // the Playlist ruler applies (with time signature changes)
    {
        const auto current = song.barAt ((std::int64_t) ticks);
        bar = current.number;
        intoBar = ticks - (double) current.tick;
        const auto sig = markers::signatureAt (song, (std::int64_t) ticks);
        beatLength = (double) ppq * 4.0 / (double) juce::jmax (1, sig.second);
    }
    else
    {
        const auto perBar = (double) meter.bar();
        bar = (int) (ticks / perBar) + 1;
        intoBar = std::fmod (ticks, perBar);
    }

    const auto beat = (int) (intoBar / beatLength) + 1;
    const auto step = (int) (std::fmod (intoBar, beatLength) / ticksPerStep) + 1;
    return juce::String::formatted ("%d : %d : %d", bar, beat, step);
}

double loopLength (const Song& song)
{
    if (song.patterns.empty())
        return 1.0;

    const auto length = song.mode == PlayMode::song
                            ? song.songLength()
                            : song.patterns[(std::size_t) juce::jlimit (0, (int) song.patterns.size() - 1, song.currentPattern)].effectiveLength (song.meter());
    return (double) juce::jmax<std::int64_t> (1, length);
}

double playheadTicks (AudioEngine& engine)
{
    if (engine.getSong().mode == PlayMode::song)
        return engine.songPlayheadTicks(); // (counts on past the song's end while a take records, like FL)
    return std::fmod (juce::jmax (0.0, engine.getTransport().positionTicks.load()), loopLength (engine.getSong()));
}

juce::String centsText (double cents)
{
    const auto c = juce::roundToInt (cents);
    return (c > 0 ? "+" : "") + juce::String (c) + " " + tr ("cents");
}
} // namespace

// --- song position -------------------------------------------------------------------------------

SongPositionBar::SongPositionBar (AudioEngine& e) : engine (e)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

double SongPositionBar::length() const { return loopLength (engine.getSong()); }
double SongPositionBar::position() const { return playheadTicks (engine); }

juce::Rectangle<float> SongPositionBar::track() const
{
    return getLocalBounds().toFloat().reduced (5.0f, 0.0f).withSizeKeepingCentre ((float) getWidth() - 10.0f, 4.0f);
}

void SongPositionBar::update()
{
    const auto fraction = position() / length();

    if (std::abs (fraction - shownFraction) * getWidth() >= 0.5)
    {
        shownFraction = fraction;
        repaint();
    }
}

void SongPositionBar::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto t = track();
    const auto& song = engine.getSong();
    const auto active = dragging || isMouseOver();

    g.setColour (colour::bg0);
    g.fillRoundedRectangle (t.expanded (0.0f, 1.0f), 2.5f);

    // the Playlist loop (Song mode)
    if (song.mode == PlayMode::song && song.hasLoop())
    {
        const auto x0 = t.getX() + t.getWidth() * (float) ((double) song.loopStart / length());
        const auto x1 = t.getX() + t.getWidth() * (float) juce::jmin (1.0, (double) song.loopEnd / length());
        g.setColour (colour::info.withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (x0, t.getY() - 1.0f, juce::jmax (1.0f, x1 - x0), t.getHeight() + 2.0f));
    }

    const auto fraction = (float) juce::jlimit (0.0, 1.0, position() / length());
    const auto x = t.getX() + t.getWidth() * fraction;
    g.setColour (engine.getTransport().playing.load() ? colour::accent : colour::accent.withMultipliedSaturation (0.55f).withMultipliedBrightness (0.8f));
    g.fillRoundedRectangle (t.withRight (juce::jmax (t.getX() + 1.0f, x)), 2.0f);

    const auto radius = active ? 5.0f : 3.5f;
    g.setColour (active ? colour::textPrimary : colour::textSecondary);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre ({ x, t.getCentreY() }));
}

void SongPositionBar::seekTo (int x)
{
    const auto t = track();
    const auto fraction = juce::jlimit (0.0, 1.0, (double) ((float) x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
    auto ticks = fraction * length();

    if (! juce::ModifierKeys::currentModifiers.isAltDown()) // Alt: no snap (as everywhere)
        ticks = std::round (ticks / ticksPerStep) * ticksPerStep;

    engine.seek (juce::jlimit (0.0, length() - 1.0, ticks));
    update();
    repaint();
}

void SongPositionBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown())
        return;

    dragging = true;
    seekTo (e.x);
}

void SongPositionBar::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        seekTo (e.x);
}

void SongPositionBar::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

void SongPositionBar::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;

    if (delta == 0.0f)
        return;

    const auto bar = (double) engine.getSong().barTicks();
    const auto current = std::round (position() / bar) * bar;
    engine.seek (juce::jlimit (0.0, juce::jmax (0.0, length() - bar), current + (delta > 0.0f ? -bar : bar) * (wheel.isReversed ? -1.0 : 1.0)));
    update();
}

ui::Hint SongPositionBar::hintAt (juce::Point<int>)
{
    const auto& song = engine.getSong();
    ui::Hint h;
    h.text = (song.mode == PlayMode::song ? tr ("Song position") : tr ("Pattern position"))
             + ": " + tr ("[LMB] click or drag to move the playhead  [Wheel] one bar  [Alt] no snap");
    const auto bars = juce::jmax (1, juce::roundToInt (length() / ((double) song.barTicks())));
    h.value = tr ("Bar {0} of {1}", { barBeatStep (song, position(), song.meter()).upToFirstOccurrenceOf (" ", false, false), juce::String (bars) });
    return h;
}

// --- time display --------------------------------------------------------------------------------

namespace
{
// Round 190gr: Song::secondsForTicks sums the song's tempo from tick 0 at every call (with a tempo automation: one tempo
// lookup, a walk over the clips, per 16th), and the time display asks for it at every timer tick and paint while the song
// plays. The sum of the whole 16ths is kept and continued from where it stopped: the same additions in the same order,
// so the same seconds to the last bit. Message thread.
double secondsForTicksCached (const Song& song, std::uint64_t stamp, std::int64_t ticks)
{
    if (! tempomap::active (song) || ! ui::paintCache())
        return song.secondsForTicks (ticks);
    struct Sum
    {
        std::uint64_t stamp = 0;
        double bpm = 0.0;
        int mode = -1, pattern = -1;
        std::int64_t whole = 0; // ticks summed (whole 16ths)
        double seconds = 0.0;
    };
    static Sum sum;
    if (sum.stamp != stamp || sum.bpm != song.bpm || sum.mode != (int) song.mode || sum.pattern != song.currentPattern || ticks < sum.whole)
        sum = { stamp, song.bpm, (int) song.mode, song.currentPattern, 0, 0.0 };
    constexpr std::int64_t step = ticksPerStep;
    for (; sum.whole + step <= ticks; sum.whole += step)
        sum.seconds += (double) step * 60.0 / (std::max (1.0, song.tempoAt ((double) sum.whole)) * ppq);
    auto seconds = sum.seconds;
    if (const auto rest = ticks - sum.whole; rest > 0)
        seconds += (double) rest * 60.0 / (std::max (1.0, song.tempoAt ((double) sum.whole)) * ppq);
    return seconds;
}
} // namespace

juce::String TimeDisplay::text() const
{
    const auto& song = engine.getSong();
    // FL's count-in (round 190jd, live check 190iw Q7): "REC IN 4" ... "REC IN 1", one per beat, before the position
    // (the Pattern layout's FL look; the Clip-launch and Simple layouts keep showing the position)
    if (layoutstyle::current() == layoutstyle::Style::pattern)
        if (const auto left = countin::beatsLeft (engine.getTransport().countIn.load(), song.beatUnit, ppq); left > 0)
            return tr ("REC IN {0}", { juce::String (left) });

    // round 190kn (DL-44, Live 12 manual 19.6): Live's Arrangement Position field shows the count-in "in blue as
    // bars-beats-sixteenths", from -bars.1.1 up to 1.1.1 (the Clip-launch control bar)
    if (look == toolbarlayout::Look::live && engine.isCountingIn())
        if (const auto p = countin::livePosition (engine.getTransport().countIn.load(), song.meter().beats, song.meter().unit, ppq); p.bar < 0)
            return juce::String (p.bar) + " : " + juce::String (p.beat) + " : " + juce::String (p.sixteenth);

    const auto ticks = playheadTicks (engine);

    // round 190jj (Simple, capture190ji §1d): BandLab shows its count-in only as negative time counting up to zero
    // ("-00:01.7" … "-00:00.2"), in minutes : seconds : hundredths whatever the display's mode
    if (look == toolbarlayout::Look::simple && engine.isCountingIn())
    {
        const auto left = juce::jmax (0.0, engine.getTransport().countIn.load()) * 60.0 / (juce::jmax (1.0, song.bpm) * ppq);
        const auto whole = (int) left;
        return juce::String::formatted ("-%d : %02d : %02d", whole / 60, whole % 60, juce::jmin (99, (int) ((left - whole) * 100.0)));
    }

    const auto fl = look == toolbarlayout::Look::fl;
    if (mode == Mode::bars && fl)
        return barStepTick (song, ticks, song.meter());
    if (mode == Mode::bars || mode == Mode::beats)
        return barBeatStep (song, ticks, song.meter());

    const auto seconds = secondsForTicksCached (song, engine.getSongStamp(), (std::int64_t) ticks);
    if (mode == Mode::smpte)
        return juce::String (timecode::format (seconds, smpteRate));
    const auto whole = (int) seconds;
    // (round 190jo FM-6: FL's "M:S:CS" without the spaces in the Pattern toolbar)
    return juce::String::formatted (fl ? "%d:%02d:%02d" : "%d : %02d : %02d", whole / 60, whole % 60, (int) ((seconds - whole) * 100.0));
}

void TimeDisplay::update()
{
    // (round 190jj: the Simple capsule's time is red while a take records or counts in)
    const auto nowRed = look == toolbarlayout::Look::simple && engine.isRecording()
                        && (engine.getTransport().playing.load() || engine.isCountingIn());
    // (round 190jo, perf: FL's B:S:T ticks run 24 to a step, at the timer's rate; a change of the ticks alone repaints at
    // most every 100 ms, as often as bar : beat : step changed at 150 BPM; a new step or bar shows at once)
    const auto t = text();
    const auto now = juce::Time::getMillisecondCounter();
    const auto ticksOnly = look == toolbarlayout::Look::fl && mode == Mode::bars && t.length() == shown.length() && t.length() > 3
                           && t.dropLastCharacters (2) == shown.dropLastCharacters (2);
    if ((t != shown && ! (ticksOnly && now - shownAt < 100u)) || nowRed != red)
    {
        shown = t;
        shownAt = now;
        red = nowRed;
        repaint();
    }
    // (round 190gl) the reference DAW's position display with Link: a progress bar of the session through the quantum
    // while the transport is stopped or waits to start (another peer connected); Play starts when it is full
    const auto& link = engine.getLink();
    const auto progress = link.isEnabled() ? link.startProgress() : -1.0;
    if (std::abs (progress - linkProgress) > 0.004 || (progress < 0.0) != (linkProgress < 0.0))
    {
        linkProgress = progress;
        repaint();
    }
}

void TimeDisplay::setLook (toolbarlayout::Look l)
{
    if (l != look)
    {
        look = l;
        shown = {}; // (round 190jo: the Pattern toolbar's B:S:T reads differently from the other looks' bar : beat : step)
        repaint();
    }
}

// round 190iv: BandLab's time segment (the last segment of the transport capsule, 15 px digits) and Live's position box
// ("1. 1. 1": bar, beat, sixteenth with Live's dotted separators)
void TimeDisplay::paintLook (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds();
    const auto readout = shown.isEmpty() ? text() : shown;
    if (look == toolbarlayout::Look::live)
    {
        paintLiveBox (g, area, false);
        if (linkProgress >= 0.0)
        {
            g.setColour (live::lit.withAlpha (0.45f));
            g.fillRect (area.withWidth (juce::roundToInt (area.getWidth() * juce::jlimit (0.0, 1.0, linkProgress))));
        }
        const auto countingIn = engine.isCountingIn() && readout.startsWith ("-");
        if (countingIn) // (round 190kn: Live's count-in, always as bars. beats. sixteenths; round 190ld: Live 12.4's colours)
        {
            g.setColour (live::countIn);
            g.fillRect (area);
            g.setColour (live::onLit);
            g.setFont (theme::font (theme::type::caption));
            g.drawFittedText (readout.replace (" : ", ". "), area.withTrimmedRight (4).reduced (3, 0), juce::Justification::centredRight, 1, 0.75f);
            return;
        }
        paintLiveText (g, mode == Mode::bars ? readout.replace (" : ", ". ") : readout.replace (" ", ""), area.withTrimmedRight (4), false,
                       juce::Justification::centredRight);
        return;
    }
    paintCapsuleHover (g, area, isMouseOver(), true); // (the transport capsule's last segment)
    if (linkProgress >= 0.0)
    {
        g.setColour (colour::accent.withAlpha (0.42f));
        g.fillRect (area.withWidth (juce::roundToInt (area.getWidth() * juce::jlimit (0.0, 1.0, linkProgress))));
    }
    auto f = monoFont (15.0f);
    for (int i = 0; i < 8 && textcache::stringWidth (f, readout) > (float) area.getWidth() - 10.0f; ++i)
        f = f.withHeight (f.getHeight() * 0.92f);
    g.setFont (f);
    g.setColour (red ? colour::record : colour::textPrimary);
    g.drawText (readout, area, juce::Justification::centred, false);
}

void TimeDisplay::paint (juce::Graphics& g)
{
    using namespace theme;
    if (look != toolbarlayout::Look::fl)
    {
        paintLook (g);
        return;
    }
    const auto area = getLocalBounds();
    g.setColour (colour::bg1);
    g.fillRoundedRectangle (area.toFloat(), radius::md);
    if (linkProgress >= 0.0) // (round 190gl) the Link session's progress bar, filled from the left
    {
        g.saveState();
        g.reduceClipRegion (area.withWidth (juce::roundToInt (area.getWidth() * juce::jlimit (0.0, 1.0, linkProgress))));
        g.setColour (colour::accent.withAlpha (0.42f));
        g.fillRoundedRectangle (area.toFloat(), radius::md);
        g.restoreState();
    }

    if (isMouseOver())
    {
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius::md, 1.0f);
    }

    g.setColour (colour::textPrimary);
    // round 170e3: the song position is the toolbar's centre (FL users look there first): a larger readout
    const auto readout = shown.isEmpty() ? text() : shown;
    // round 190jo (design190ia FM-6): FL's digits are 26 px tall (cap) in its 33 px panel; LYNX's cap 12 becomes cap 20
    // (font 40 in the 46 px box) with FL's mode caption at the top right ("B:S:T", "M:S:CS") instead of the line under it
    if (mode != Mode::beats)
    {
        auto big = monoFont (juce::jmin (40.0f, (float) area.getHeight() * 0.87f));
        const auto caption = mode == Mode::clock ? juce::String ("M:S:CS") : mode == Mode::smpte ? "SMPTE " + juce::String (timecode::rates[smpteRate].name)
                                                                                                 : juce::String ("B:S:T");
        const auto captionFont = font (9.0f, true);
        const auto key = Fit { readout.length(), area.getWidth(), area.getHeight(), (int) mode * 64 + smpteRate, (float) theme::textSize() };
        if (key.length != fit.length || key.width != fit.width || key.height != fit.height || key.mode != fit.mode || key.scale != fit.scale)
        {
            fit = key;
            fit.caption = textcache::stringWidth (captionFont, caption);
            const auto room = (float) area.getWidth() - 8.0f;
            for (int i = 0; i < 16 && textcache::stringWidth (big, readout) > room; ++i)
                big = big.withHeight (big.getHeight() * 0.94f);
            fit.digits = big.getHeight();
        }
        big = big.withHeight (fit.digits);
        const auto captionWidth = fit.caption;
        g.setFont (big);
        g.drawText (readout, area.withTrimmedTop (2), juce::Justification::centred, false);
        g.setColour (colour::textSecondary);
        g.setFont (captionFont);
        g.drawText (caption, area.withTrimmedRight (4).withTrimmedTop (2).withHeight (10).withLeft (area.getRight() - 4 - (int) std::ceil (captionWidth) - 1),
                    juce::Justification::topRight, false);
        return;
    }
    auto readoutFont = monoFont (juce::jmin (26.0f, (float) area.getHeight() * 0.52f));
    // a narrow display (the compact toolbar) shrinks the digits instead of cutting them off
    for (int i = 0; i < 12 && textcache::stringWidth (readoutFont, readout) > (float) area.getWidth() - 8.0f; ++i)
        readoutFont = readoutFont.withHeight (readoutFont.getHeight() * 0.92f);
    g.setFont (readoutFont);
    g.drawText (readout, area.withTrimmedBottom (11), juce::Justification::centred, false);
    g.setColour (colour::textDisabled);
    g.setFont (font (type::caption - 1.0f));
    // (fitted: a translation or a larger Text size (issue #189) narrows the letters instead of cutting the label off)
    g.drawFittedText (mode == Mode::clock   ? tr ("MIN : SEC : CS")
                      : mode == Mode::smpte ? "SMPTE " + juce::String (timecode::rates[smpteRate].name)
                                            : tr ("BAR : BEAT : STEP"), area.withTrimmedTop (area.getHeight() - 14).withTrimmedBottom (2).reduced (2, 0),
                      juce::Justification::centred, 1, 0.6f);
}

void TimeDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }

    mode = mode == Mode::bars || mode == Mode::beats ? Mode::clock : mode == Mode::clock ? Mode::smpte : Mode::bars;
    shown = {};
    update();
    repaint();
}

void TimeDisplay::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Mode"));
    auto set = [this] (Mode m, int rate) {
        mode = m;
        if (rate >= 0)
            smpteRate = rate;
        shown = {};
        update();
        repaint();
    };
    if (look == toolbarlayout::Look::fl) // (round 190jo: FL's B:S:T first, LYNX's bar : beat : step kept)
    {
        menu.addItem (tr ("Bar : Step : Tick"), true, mode == Mode::bars, [set] { set (Mode::bars, -1); });
        menu.addItem (tr ("Bar : Beat : Step"), true, mode == Mode::beats, [set] { set (Mode::beats, -1); });
    }
    else
        menu.addItem (tr ("Bar : Beat : Step"), true, mode == Mode::bars || mode == Mode::beats, [set] { set (Mode::bars, -1); });
    menu.addItem (tr ("Minute : Second : Centisecond"), true, mode == Mode::clock, [set] { set (Mode::clock, -1); });
    for (int i = 0; i < timecode::rateCount; ++i)
        menu.addItem ("SMPTE " + juce::String (timecode::rates[i].name) + " fps", true, mode == Mode::smpte && smpteRate == i, [set, i] { set (Mode::smpte, i); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

ui::Hint TimeDisplay::hintAt (juce::Point<int>)
{
    ui::Hint h;
    h.text = (mode == Mode::clock   ? tr ("Time: minutes : seconds : hundredths")
              : mode == Mode::smpte ? tr ("Time: SMPTE time code (hours : minutes : seconds : frames)")
              : mode == Mode::bars && look == toolbarlayout::Look::fl ? tr ("Time: bar : step : tick")
                                    : tr ("Time: bar : beat : step"))
             + "  " + tr ("[LMB] next mode (bars, minutes, SMPTE)  [RMB] mode and frame rate");
    h.value = text();
    return h;
}

// --- master volume and pitch ---------------------------------------------------------------------

MasterKnob::MasterKnob (AudioEngine& e, Kind k) : engine (e), kind (k)
{
    setRepaintsOnMouseActivity (true);
}

bool MasterKnob::available() const
{
    return kind == Kind::pitch || ! engine.getSong().mixer.empty();
}

double MasterKnob::value() const
{
    const auto& song = engine.getSong();

    if (kind == Kind::pitch)
        return song.masterPitch;

    return song.mixer.empty() ? (double) unityFader : (double) song.mixer.front().volume;
}

void MasterKnob::setValue (double v)
{
    const auto s = spec();
    v = juce::jlimit (s.minimum, s.maximum, v);

    if (! available() || std::abs (v - value()) < 1e-9)
        return;

    if (kind == Kind::pitch)
        engine.editSong ([v] (Song& song) { song.masterPitch = (float) v; });
    else
        engine.editLevels ([v] (Song& song) {
            if (! song.mixer.empty())
                song.mixer.front().volume = (float) v;
        });

    update();
}

ui::ValueSpec MasterKnob::spec() const
{
    if (kind == Kind::volume)
    {
        auto s = ui::faderSpec (tr ("Master volume"), unityFader);
        s.target = AutomationTarget { AutomationTargetKind::mixerVolume, 0 };
        return s;
    }

    ui::ValueSpec s;
    s.name = tr ("Master pitch");
    s.minimum = -1200.0;
    s.maximum = 1200.0;
    s.defaultValue = 0.0;
    s.pixelsForRange = 360.0;
    s.toText = [] (double v) { return centsText (v); };
    s.fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto number = t.retainCharacters ("+-0123456789.");
        if (number.isEmpty())
            return std::nullopt;
        return number.getDoubleValue();
    };
    // FL's Master pitch › "Type in value..." (round 190mv, DF-22; capture 190mv A6: a box "Master pitch (Hz)" showing 440):
    // the tuning of A in Hz, 440 = no shift; LYNX's range ±1200 cents = 220…880 Hz
    auto hz = std::make_shared<ui::ValueSpec>();
    hz->name = tr ("Master pitch (Hz)");
    hz->minimum = 220.0;
    hz->maximum = 880.0;
    hz->defaultValue = 440.0;
    hz->toText = [] (double v) { return std::abs (v - std::round (v)) < 0.005 ? juce::String ((int) std::round (v)) : juce::String (v, 2); };
    hz->fromText = [] (const juce::String& t) -> std::optional<double> {
        const auto number = t.retainCharacters ("0123456789.");
        if (number.isEmpty() || number.getDoubleValue() <= 0.0)
            return std::nullopt;
        return juce::jlimit (220.0, 880.0, number.getDoubleValue());
    };
    s.typeInSpec = hz;
    s.toTypeIn = [] (double cents) { return masterpitch::hzOfCents (cents); };
    s.fromTypeIn = [] (double hzValue) { return masterpitch::centsOfHz (hzValue); };
    return s;
}

void MasterKnob::update()
{
    if (const auto v = value(); v != shown)
    {
        shown = v;
        repaint();
    }
}

void MasterKnob::setLook (toolbarlayout::Look l)
{
    if (l == look)
        return;
    look = l;
    meter = {};
    meterPixels = { -1, -1 };
    meterMs = -1.0;
    repaint();
}

juce::Rectangle<float> MasterKnob::sliderRail() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (26.0f);  // the speaker
    area.removeFromRight (66.0f); // the value
    return area.withSizeKeepingCentre (area.getWidth(), 11.0f);
}

void MasterKnob::tickMeter (double nowMs)
{
    if (! isSlider() || ! isShowing())
    {
        meterMs = -1.0;
        return;
    }
    // (BandLab: ~0.6 s from full to the rest stub, no peak hold; a peak rises at once)
    const auto dt = meterMs < 0.0 ? 1.0 / 30.0 : juce::jlimit (0.0, 0.25, (nowMs - meterMs) / 1000.0);
    meterMs = nowMs;
    const auto rail = sliderRail();
    auto& meters = engine.getMeters();
    auto moved = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto target = juce::jlimit (0.0f, 1.0f, faderPosition (meters.takeMasterBarPeak (ch)));
        auto& m = meter[(std::size_t) ch];
        m = target >= m ? target : juce::jmax (target, m - (float) (dt / 0.6));
        const auto px = juce::roundToInt (m * rail.getWidth());
        moved = moved || px != meterPixels[(std::size_t) ch];
        meterPixels[(std::size_t) ch] = px;
    }
    if (moved)
        repaint (rail.toNearestInt().expanded (1, 3)); // (the rail only; the thumb sits on it and is painted with it)
}

void MasterKnob::paintSlider (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds().toFloat();
    const auto rail = sliderRail();
    const auto ink = available() ? (isMouseOver() || drag.isActive() || sliderDrag ? colour::textPrimary : colour::textSecondary) : colour::textDisabled;

    // the speaker (a LYNX vector: a box, a cone and two waves)
    {
        const auto s = juce::Rectangle<float> (area.getX() + 3.0f, area.getCentreY() - 8.0f, 16.0f, 16.0f);
        juce::Path body;
        body.startNewSubPath (s.getX() + 1.0f, s.getY() + 6.0f);
        body.lineTo (s.getX() + 4.0f, s.getY() + 6.0f);
        body.lineTo (s.getX() + 8.0f, s.getY() + 2.5f);
        body.lineTo (s.getX() + 8.0f, s.getBottom() - 2.5f);
        body.lineTo (s.getX() + 4.0f, s.getBottom() - 6.0f);
        body.lineTo (s.getX() + 1.0f, s.getBottom() - 6.0f);
        body.closeSubPath();
        juce::Path waves;
        waves.addCentredArc (s.getX() + 8.5f, s.getCentreY(), 3.5f, 3.5f, 0.0f, 0.75f, 2.39f, true);
        waves.addCentredArc (s.getX() + 8.5f, s.getCentreY(), 6.5f, 6.5f, 0.0f, 0.75f, 2.39f, true);
        g.setColour (ink);
        g.strokePath (body, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (waves, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // the rail: two 4 px bars with a 3 px gap (BandLab's stereo master meter), the meter filled green to yellow along
    // the rail (#4baa69 at the left, #a9c423 at the slider's thumb), a short green stub at rest
    const auto value01 = (float) juce::jlimit (0.0, 1.0, value());
    const auto cx = rail.getX() + value01 * rail.getWidth();
    const juce::Colour railColour { 0xff2e3138 }, low { 0xff4baa69 }, high { 0xffa9c423 };
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto bar = juce::Rectangle<float> (rail.getX(), rail.getY() + (float) ch * 7.0f, rail.getWidth(), 4.0f);
        g.setColour (railColour);
        g.fillRoundedRectangle (bar, 2.0f);
        const auto w = juce::jmax (5.0f, meter[(std::size_t) ch] * rail.getWidth());
        juce::ColourGradient fill (low, rail.getX(), 0.0f, high, juce::jmax (rail.getX() + 8.0f, cx), 0.0f, false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (bar.withWidth (w), 2.0f);
    }
    // the thumb: a dark disc in a white ring on the value
    const auto thumb = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ cx, rail.getCentreY() });
    g.setColour (colour::bg2);
    g.fillEllipse (thumb);
    g.setColour (available() ? juce::Colours::white : colour::textDisabled);
    g.drawEllipse (thumb.reduced (1.0f), 2.0f);

    // the value ("+0.0 dB"), right of the rail
    g.setColour (ink == colour::textDisabled ? ink : colour::textPrimary);
    g.setFont (font (type::small, true));
    const auto db = juce::Decibels::gainToDecibels (faderGain (value01), -100.0f);
    juce::String text;
    if (! available())
        text = tr ("no mixer");
    else if (db <= -99.0f)
        text = "-inf dB";
    else
    {
        const auto r = std::round (db * 10.0f) / 10.0f;
        text = (r >= 0.0f ? "+" : "") + juce::String (r == 0.0f ? 0.0f : r, 1) + " dB";
    }
    g.drawText (text, area.withLeft (rail.getRight() + 10.0f), juce::Justification::centredLeft, false);
}

void MasterKnob::setFromRail (float x)
{
    const auto rail = sliderRail();
    setValue ((double) juce::jlimit (0.0f, 1.0f, (x - rail.getX()) / juce::jmax (1.0f, rail.getWidth())));
}

void MasterKnob::paint (juce::Graphics& g)
{
    using namespace theme;
    if (isSlider())
    {
        paintSlider (g);
        return;
    }
    auto area = getLocalBounds().toFloat();
    const auto s = spec();
    const auto v = value();
    const auto hover = isMouseOver() || drag.isActive();
    const auto value01 = (float) ((v - s.minimum) / (s.maximum - s.minimum));
    // round 170e3: on the toolbar's two rows the knob is big, its name and value under it
    if (area.getHeight() >= 48.0f)
    {
        const auto text = area.removeFromBottom (22.0f);
        const auto side = juce::jmin (area.getHeight(), area.getWidth());
        drawKnob (g, area.withSizeKeepingCentre (side, side), value01, kind == Kind::pitch,
                  available() ? colour::knobArc : colour::textDisabled, hover && available());
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption - 1.0f, true));
        g.drawText (kind == Kind::volume ? tr ("MASTER") : tr ("PITCH"), text.withHeight (10.0f).translated (0.0f, 1.0f), juce::Justification::centred, true);
        g.setColour (drag.isActive() ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::caption));
        g.drawText (available() ? s.text (v) : tr ("no mixer"), text.withTrimmedTop (11.0f), juce::Justification::centred, true);
        return;
    }
    const auto knob = area.removeFromLeft (juce::jmin (area.getHeight(), 26.0f));
    drawKnob (g, knob, value01, kind == Kind::pitch, available() ? colour::knobArc : colour::textDisabled, hover && available());

    area.removeFromLeft (5.0f);
    auto caption = area.removeFromTop (area.getHeight() / 2.0f);
    g.setColour (colour::textDisabled);
    g.setFont (font (type::caption - 1.0f, true));
    g.drawText (kind == Kind::volume ? tr ("MASTER") : tr ("PITCH"), caption.withTrimmedTop (1.0f), juce::Justification::bottomLeft, true);
    g.setColour (drag.isActive() ? colour::textPrimary : colour::textSecondary);
    g.setFont (font (type::caption));
    g.drawText (available() ? s.text (v) : tr ("no mixer"), area, juce::Justification::topLeft, true);
}

void MasterKnob::mouseDown (const juce::MouseEvent& e)
{
    if (! available())
        return;

    if (e.mods.isPopupMenu())
    {
        ui::showValueMenu (*this, kind == Kind::volume ? &engine : nullptr, spec(), value(), [this] (double v) { setValue (v); });
        return;
    }

    if (ui::isResetClick (e))
    {
        setValue (spec().defaultValue);
        return;
    }

    if (isSlider() && sliderRail().expanded (8.0f, 6.0f).contains (e.position)) // (round 190jj: BandLab's slider follows the mouse)
    {
        sliderDrag = true;
        setFromRail (e.position.x);
        repaint();
        return;
    }

    drag.begin (e, value());
    repaint();
}

void MasterKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (sliderDrag)
    {
        setFromRail (e.position.x);
        return;
    }
    if (! drag.isActive())
        return;

    auto v = drag.update (e, spec());

    if (kind == Kind::pitch && ! e.mods.isShiftDown()) // semitone steps; Shift: free (as FL's master pitch)
        v = std::round (v / 100.0) * 100.0;

    setValue (v);
}

void MasterKnob::mouseUp (const juce::MouseEvent&)
{
    sliderDrag = false;
    drag.end();
    repaint();
}

void MasterKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (available() && ! e.mods.isPopupMenu() && ! ui::isResetClick (e))
        ui::typeValue (*this, spec(), value(), [this] (double v) { setValue (v); });
}

void MasterKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! available())
        return;

    if (kind == Kind::pitch)
    {
        const auto delta = std::abs (wheel.deltaY) >= std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (delta != 0.0f)
            setValue (value() + (delta > 0.0f ? 1.0 : -1.0) * (wheel.isReversed ? -1.0 : 1.0) * (e.mods.isCtrlDown() ? 1.0 : 100.0));
        return;
    }

    if (const auto v = ui::wheelValue (value(), e, wheel, spec()))
        setValue (*v);
}

ui::Hint MasterKnob::hintAt (juce::Point<int>)
{
    if (! available())
        return { tr ("Master volume: this project has no mixer"), {}, false, false };

    const auto gesture = kind == Kind::volume
                             ? tr ("level of everything you hear (the Master insert's fader)") + "  " + ui::valueGestures()
                             : tr ("tunes the built-in instruments  [LMB] drag (semitones)  [Shift] cents  [Wheel] semitone  [Ctrl][Wheel] cent  [Alt][LMB] reset  [RMB] menu");
    return ui::valueHint (spec(), value(), gesture);
}

// --- Live's phase nudge (round 190mv, DL-51) ----------------------------------------------------------------------

void PhaseNudge::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds();
    if (held)
        g.fillAll (juce::Colour (0xffffc281)); // (capture 190mv B2: 255,194,129)
    else if (isMouseOver())
        g.fillAll (juce::Colour (0xff464646));
    g.setColour (held ? juce::Colours::black : juce::Colour (0xffb5b5b5));
    // four bars, the pair on the side it nudges to drawn heavier (Live's "||||" glyphs mirror each other)
    const auto h = (float) box.getHeight() * 0.62f, y = (float) box.getCentreY() - h * 0.5f;
    const float xs[] = { -5.0f, -2.0f, 1.0f, 4.0f };
    for (int i = 0; i < 4; ++i)
    {
        const auto heavy = faster ? i >= 2 : i < 2;
        g.fillRect (juce::Rectangle<float> ((float) box.getCentreX() + xs[i] - (heavy ? 0.5f : 0.0f), y, heavy ? 2.0f : 1.0f, h));
    }
}

void PhaseNudge::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown() || held)
        return;
    held = true;
    base = engine.getSong().bpm;
    const auto bpm = controls::nudgedTempo (base, faster);
    engine.editSong ([bpm] (Song& song) { song.bpm = bpm; }, false); // (outside the history: the tempo comes back)
    repaint();
}

void PhaseNudge::release()
{
    if (! held)
        return;
    held = false;
    const auto bpm = base;
    engine.editSong ([bpm] (Song& song) { song.bpm = bpm; }, false);
    repaint();
}

ui::Hint PhaseNudge::hintAt (juce::Point<int>)
{
    return { faster ? tr ("Phase Nudge Up: Temporarily increase song tempo to synchronize to external music.")
                    : tr ("Phase Nudge Down: Temporarily decrease song tempo to synchronize to external music."),
             {}, false, false };
}

// --- CPU, memory, polyphony ----------------------------------------------------------------------

void PerformanceMeter::setValues (double cpu01, std::int64_t memoryBytes, int polyphony)
{
    cpu = cpu01;
    cpuSmoothed = cpuSmoothed <= 0.0 ? cpu01 : cpuSmoothed * 0.8 + cpu01 * 0.2;
    memory = memoryBytes;
    voices = polyphony;
    peakVoices = juce::jmax (peakVoices, polyphony);
    // FL's CPU graph (round 190kn): one point per update, the newest on the right
    std::rotate (cpuHistory.begin(), cpuHistory.begin() + 1, cpuHistory.end());
    cpuHistory.back() = (float) juce::jlimit (0.0, 1.0, cpu01);
    cpuHistoryCount = juce::jmin (historySize, cpuHistoryCount + 1);
    // repaint only when a pixel would change: the texts, the bar, or the graph's rows (an idle project's 1-3 % load
    // draws the same flat graph update after update)
    const auto graph = graphArea();
    const auto rows = juce::jmax (1.0f, graph.getHeight() - 1.0f);
    juce::int64 sig = juce::roundToInt (cpuSmoothed * 100.0);
    sig = sig * 31 + juce::roundToInt (cpuSmoothed * (double) barBox().getWidth());
    sig = sig * 31 + (juce::int64) (memory / (1024 * 1024));
    sig = sig * 31 + (secondView == framesPerSecond ? 100000 + fps : voices);
    sig = sig * 31 + juce::jmin (cpuHistoryCount, 2);
    for (std::size_t i = 0; i < cpuHistory.size(); ++i)
        sig = sig * 1000003 + (juce::int64) std::round (cpuHistory[i] * rows) * 7 + (juce::int64) i;
    if (sig != paintedSignature)
    {
        paintedSignature = sig;
        repaint();
    }
}

juce::Rectangle<float> PerformanceMeter::barBox() const
{
    auto area = getLocalBounds();
    auto row1 = area.removeFromTop (area.getHeight() / 2).reduced (0, 4);
    row1.removeFromLeft (30);
    row1.removeFromRight (34);
    if (underruns > 0)
        row1.removeFromRight (26);
    return row1.reduced (2, 3).toFloat();
}

juce::Rectangle<float> PerformanceMeter::graphArea() const
{
    if (look == toolbarlayout::Look::live)
        return {};
    // FL: the load bar on top, the running graph below it in the same box; here the box's lower part, under a 5 px bar
    const auto box = barBox();
    return box.getHeight() >= 11.0f ? box.withTrimmedTop (6.0f).reduced (1.0f, 0.0f) : juce::Rectangle<float>();
}

void PerformanceMeter::setFramesPerSecond (int framesPerSecondIn)
{
    fps = juce::jmax (0, framesPerSecondIn); // (painted with the next setValues, which compares what would change)
}

void PerformanceMeter::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        makeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

juce::PopupMenu PerformanceMeter::makeMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Refresh"));
    const std::pair<int, juce::String> rates[] { { slow, tr ("Slow") }, { medium, tr ("Medium") }, { fast, tr ("Fast") } };
    for (const auto& [id, name] : rates)
        menu.addItem (name, true, refresh == id, [this, id = id] {
            refresh = id;
            if (onRefreshChanged)
                onRefreshChanged();
        });
    // FL's "Misc" section (FL 2026 capture details190kn raw/f08): the voice count / FPS radio pair, then greyed info lines
    menu.addSectionHeader (tr ("Misc"));
    const std::pair<int, juce::String> views[] { { voiceCount, tr ("View voice count") }, { framesPerSecond, tr ("View FPS") } };
    for (const auto& [id, name] : views)
        menu.addItem (name, true, secondView == id, [this, id = id] {
            secondView = id;
            paintedSignature = -1;
            repaint();
            if (onRefreshChanged)
                onRefreshChanged();
        });
    if (const auto [gdi, user] = guiHandles(); gdi >= 0)
        menu.addItem (tr ("Handles: {0} (GDI) / {1} (USER)", { juce::String (gdi), juce::String (user) }), false, false, [] {});
    if (const auto threads = threadCount(); threads > 0)
        menu.addItem (tr ("Threads: {0}", { juce::String (threads) }), false, false, [] {});
    menu.addSeparator();
    menu.addItem (tr ("Plugin performance monitor..."), onOpenMonitor != nullptr, false, [this] {
        if (onOpenMonitor)
            onOpenMonitor();
    });
    return menu;
}

void PerformanceMeter::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && onOpenMonitor) // FL: "Double-Click the CPU panel to open the Plugin Performance Monitor"
        onOpenMonitor();
}

void PerformanceMeter::setRealtime (std::uint64_t over, std::uint64_t late, double peak, const juce::StringArray& units, int device)
{
    const auto count = RtStats::underrunCount (over, late, device);
    if (over > overDeadline || late > lateCalls || count > underruns)
        lastGlitchAt = juce::jmax<juce::uint32> (1, juce::Time::getMillisecondCounter());
    // (round 190kn: repaint only when the panel would look different: a new count, the peak mark's pixel, the red
    // glitch edge coming or going; the hint reads the rest when asked)
    const auto box = barBox();
    auto markX = [&box] (double p) { return p > 0.0 ? juce::roundToInt (box.getWidth() * (float) juce::jlimit (0.0, 1.0, p)) * 2 + (p < 0.85 ? 0 : 1) : -1; };
    const bool glitchLit = lastGlitchAt != 0 && juce::Time::getMillisecondCounter() - lastGlitchAt < 5000;
    const bool changed = count != underruns || markX (peak) != markX (peakLoad) || glitchLit != glitchShown;
    overDeadline = over;
    lateCalls = late;
    deviceUnderruns = device;
    underruns = count;
    peakLoad = peak;
    heaviest = units;
    glitchShown = glitchLit;
    if (changed)
        repaint();
}

void PerformanceMeter::setLook (toolbarlayout::Look l)
{
    if (l != look)
    {
        look = l;
        repaint();
    }
}

void PerformanceMeter::paint (juce::Graphics& g)
{
    using namespace theme;
    if (look == toolbarlayout::Look::live) // round 190iv: Live's CPU box (CL-2: "26 %" over its orange load bar)
    {
        const auto area = getLocalBounds();
        paintLiveBox (g, area, false);
        const auto load = (float) juce::jlimit (0.0, 1.0, cpuSmoothed);
        if (load > 0.0f)
        {
            g.setColour (live::lit);
            g.fillRect (area.withWidth (juce::jmax (1, juce::roundToInt ((float) area.getWidth() * load))));
        }
        if (lastGlitchAt != 0 && juce::Time::getMillisecondCounter() - lastGlitchAt < 5000)
        {
            g.setColour (colour::meterClip);
            g.drawRect (area, 1);
        }
        paintLiveText (g, juce::String (juce::roundToInt (cpuSmoothed * 100.0)) + " %", area);
        return;
    }
    auto area = getLocalBounds();
    auto row1 = area.removeFromTop (area.getHeight() / 2).reduced (0, 4);
    auto row2 = area.reduced (0, 4);
    const juce::Font caption (font (type::caption - 1.0f, true)), number (font (type::caption));

    // CPU: caption, bar, percent
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    g.drawText (tr ("CPU"), row1.removeFromLeft (30), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (colour::textSecondary);
    g.drawText (juce::String (juce::roundToInt (cpuSmoothed * 100.0)) + "%", row1.removeFromRight (34), juce::Justification::centredRight, false);
    if (underruns > 0) // FL's underrun counter, next to the percentage
    {
        g.setColour (colour::meterClip);
        g.drawText (underruns > 999 ? juce::String ("999+") : juce::String ((juce::int64) underruns), row1.removeFromRight (26),
                    juce::Justification::centredRight, false);
    }
    const auto box = row1.reduced (2, 3).toFloat();
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (box, 2.0f);
    // round 190kn: FL's running CPU graph in the box's lower part (the load bar shrinks to the top 5 px)
    const auto graph = graphArea();
    const auto bar = graph.isEmpty() ? box : box.withHeight (5.0f);
    if (! graph.isEmpty() && cpuHistoryCount > 1)
    {
        const auto n = juce::jmin (cpuHistoryCount, juce::jmax (2, (int) graph.getWidth()));
        const auto step = graph.getWidth() / (float) (n - 1);
        g.setColour (colour::textSecondary.withAlpha (0.8f));
        auto yOf = [&] (float v) { return std::round (graph.getBottom() - 1.0f - v * (graph.getHeight() - 1.0f)); };
        // newest on the right: 1 px columns joined, so a spike shows as FL's thin vertical line
        float lastY = yOf (cpuHistory[(std::size_t) (historySize - n)]);
        for (int i = 1; i < n; ++i)
        {
            const auto y = yOf (cpuHistory[(std::size_t) (historySize - n + i)]);
            const auto x0 = std::round (graph.getRight() - (float) (n - i) * step), x1 = std::round (graph.getRight() - (float) (n - 1 - i) * step);
            g.fillRect (juce::Rectangle<float> (x0, juce::jmin (lastY, y), 1.0f, std::abs (y - lastY) + 1.0f));
            g.fillRect (juce::Rectangle<float> (x0, y, juce::jmax (1.0f, x1 - x0), 1.0f));
            lastY = y;
        }
    }
    const auto load = (float) juce::jlimit (0.0, 1.0, cpuSmoothed);
    g.setColour (load < 0.6f ? colour::meterLow : load < 0.85f ? colour::meterMid : colour::meterClip);
    if (load > 0.0f)
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * load)), 2.0f);
    // the highest callback load since the last update (a thin mark), and a red edge for a few seconds after a
    // callback missed its deadline (an audible crackle)
    if (peakLoad > 0.0)
    {
        const auto x = bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, peakLoad);
        g.setColour (peakLoad < 0.85 ? colour::textSecondary : colour::meterClip);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
    }
    if (lastGlitchAt != 0 && juce::Time::getMillisecondCounter() - lastGlitchAt < 5000)
    {
        g.setColour (colour::meterClip);
        g.drawRoundedRectangle (bar.expanded (1.0f), 2.0f, 1.0f);
    }

    // memory and polyphony
    auto left = row2.removeFromLeft (row2.getWidth() * 58 / 100);
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    g.drawText (tr ("RAM"), left.removeFromLeft (30), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (colour::textSecondary);
    g.drawText (memory > 0 ? juce::String (juce::roundToInt ((double) memory / (1024.0 * 1024.0))) + " MB" : juce::String ("-"), left,
                juce::Justification::centredLeft, false);
    g.setFont (caption);
    g.setColour (colour::textDisabled);
    if (secondView == framesPerSecond) // round 190ld: FL's "View FPS" (FL 2026: "143" and a 14 x 11 grey "FPS" badge after it)
    {
        const auto badge = row2.removeFromRight (16).withSizeKeepingCentre (16, 11).toFloat();
        g.setColour (juce::Colour (0xff596267));
        g.fillRoundedRectangle (badge, 1.5f);
        g.setColour (colour::bg0);
        g.setFont (font (type::caption - 3.0f, true));
        g.drawText ("FPS", badge, juce::Justification::centred, false);
        row2.removeFromRight (3);
        g.setFont (number);
        g.setColour (fps > 0 ? colour::textPrimary : colour::textSecondary);
        g.drawText (juce::String (fps), row2, juce::Justification::centredRight, false);
        return;
    }
    g.drawText (tr ("POLY"), row2.removeFromLeft (32), juce::Justification::centredLeft, false);
    g.setFont (number);
    g.setColour (voices > 0 ? colour::textPrimary : colour::textSecondary);
    g.drawText (juce::String (voices), row2, juce::Justification::centredRight, false);
}

// --- MIDI activity light ---------------------------------------------------------------------------

void MidiActivityLight::update (std::uint32_t count, const juce::StringArray& activeInputs, std::uint32_t projectLinked, std::uint32_t globalLinked)
{
    const auto wasLit = litFrames > 0;
    const auto shownKind = paintedKind;
    if (seenCount && (count != lastCount || projectLinked != lastProject || globalLinked != lastGlobal))
    {
        litFrames = 6; // ~0.2 s after the last message
        // (a link takes its message on the message thread a frame after the count: the link's colour wins)
        kind = projectLinked != lastProject ? Kind::project : globalLinked != lastGlobal ? Kind::global
             : (kind == Kind::project || kind == Kind::global) && wasLit ? kind : Kind::unhandled;
    }
    else if (litFrames > 0)
        --litFrames;
    seenCount = true;
    lastCount = count;
    lastProject = projectLinked;
    lastGlobal = globalLinked;
    paintedKind = kind;

    if (activeInputs != inputs)
    {
        inputs = activeInputs;
        setTooltip (hintAt ({}).text);
    }

    if (wasLit != (litFrames > 0) || (litFrames > 0 && shownKind != kind))
        repaint();
}

void MidiActivityLight::beat (bool barStart)
{
    if (litFrames > 0 && (kind == Kind::project || kind == Kind::global || kind == Kind::unhandled) && litFrames > 3)
        return; // (incoming MIDI just lit it: that stays readable)
    kind = paintedKind = barStart ? Kind::bar : Kind::beat;
    litFrames = 3; // a short blink (~0.1 s)
    repaint();
}

void ChordPanel::setText (const juce::String& newText)
{
    if (newText != text)
    {
        text = newText;
        repaint();
    }
}

void ChordPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto area = getLocalBounds();
    g.setColour (colour::bg1);
    g.fillRoundedRectangle (area.toFloat(), radius::md);
    g.setColour (colour::textPrimary);
    // past eight characters the name gets smaller, down to what fits (FL: its size minus 1.15 per character)
    const auto base = juce::jmin (20.0f, (float) area.getHeight() * 0.55f);
    auto readoutFont = monoFont (juce::jmax (9.0f, base - 1.15f * (float) juce::jmax (0, text.length() - 8)));
    for (int i = 0; i < 10 && textcache::stringWidth (readoutFont, text) > (float) area.getWidth() - 6.0f; ++i)
        readoutFont = readoutFont.withHeight (readoutFont.getHeight() * 0.92f);
    g.setFont (readoutFont);
    g.drawText (text, area, juce::Justification::centred, false);
}

void ChordPanel::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu)
        onMenu();
}

ui::Hint ChordPanel::hintAt (juce::Point<int>)
{
    return { tr ("Detected chord"), {}, false, false };
}

// --- Ableton Link toggle (round 190gl) ---------------------------------------------------------------------------

juce::String LinkToggle::text() const
{
    if (! on)
        return trc ("@Ableton Link: Link");
    return (peers == 1 ? trc ("@Ableton Link: {0} Link") : trc ("@Ableton Link: {0} Links")).replace ("{0}", juce::String (peers));
}

int LinkToggle::idealWidth (int)
{
    // (rollup 0.1.28, coordinator: a small switch like the reference's control-bar toggle and the toolbar's own
    // switches — the small type, a short box, a narrow arrow)
    const auto f = theme::font (theme::type::small, true);
    auto widest = 0.0f;
    for (const auto* t : { "@Ableton Link: Link", "@Ableton Link: {0} Links" })
        widest = juce::jmax (widest, textcache::stringWidth (f, trc (t).replace ("{0}", "88")));
    return juce::roundToInt (widest) + 12 + arrowWidth + 2;
}

void LinkToggle::update()
{
    const auto& link = engine.getLink();
    const auto nowOn = link.isEnabled();
    const auto nowPeers = nowOn ? link.peers() : 0;
    if (nowOn != on || nowPeers != peers)
    {
        on = nowOn;
        peers = nowPeers;
        repaint();
    }
}

void LinkToggle::setLook (toolbarlayout::Look l)
{
    if (l != look)
    {
        look = l;
        repaint();
    }
}

void LinkToggle::paint (juce::Graphics& g)
{
    using namespace theme;
    if (look == toolbarlayout::Look::live) // round 190iv: Live's toggle box, lit orange while on
    {
        const auto arrowBox = arrowArea();
        const auto main = boxArea().withTrimmedRight (arrowBox.getWidth() + 2);
        const auto over = isMouseOver (true);
        paintLiveBox (g, main, over, on);
        paintLiveText (g, text(), main, on);
        paintLiveBox (g, arrowBox, over);
        paintLiveArrow (g, arrowBox, live::text);
        return;
    }
    const auto arrow = arrowArea();
    const auto main = boxArea().withTrimmedRight (arrow.getWidth() + 2);
    const auto over = isMouseOver (true);
    // the switch, small like the reference's control-bar toggle and lit like the toolbar's other switches (metronome,
    // count-in: the raised surface with the theme's accent on it; rollup 0.1.28, coordinator)
    g.setColour (on ? colour::bg6 : (over ? colour::bg5 : colour::bg4));
    g.fillRoundedRectangle (main.toFloat(), radius::sm);
    if (on)
    {
        g.setColour (colour::accent.withAlpha (0.55f));
        g.drawRoundedRectangle (main.toFloat().reduced (0.5f), radius::sm, 1.0f);
    }
    g.setColour (on ? colour::accent : (over ? colour::textPrimary : colour::textSecondary));
    auto f = font (type::small, true);
    const auto label = text();
    for (int i = 0; i < 8 && textcache::stringWidth (f, label) > (float) main.getWidth() - 6.0f; ++i)
        f = f.withHeight (f.getHeight() * 0.92f);
    g.setFont (f);
    g.drawText (label, main, juce::Justification::centred, false);
    // the drop-down arrow beside it
    if (over)
    {
        g.setColour (colour::bg5);
        g.fillRoundedRectangle (arrow.toFloat(), radius::sm);
    }
    const auto a = arrow.toFloat().withSizeKeepingCentre (7.0f, 4.0f);
    juce::Path tri;
    tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
    g.setColour (colour::textSecondary);
    g.fillPath (tri);
}

void LinkToggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || arrowArea().contains (e.getPosition()))
    {
        showMenu();
        return;
    }
    if (e.mods.isLeftButtonDown())
    {
        engine.setLinkEnabled (! engine.getLink().isEnabled());
        update();
    }
}

void LinkToggle::showMenu()
{
    makeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

juce::PopupMenu LinkToggle::makeMenu()
{
    // the reference DAW's drop-down: Link on / off and "Configure Link..." (its Link Audio items: LYNX has no Link Audio)
    juce::PopupMenu menu;
    const auto linkOn = engine.getLink().isEnabled();
    menu.addItem (trc ("@Ableton Link: Link"), true, linkOn, [this, linkOn] {
        engine.setLinkEnabled (! linkOn);
        update();
    });
    menu.addSeparator();
    menu.addItem (tr ("Configure Link..."), [this] {
        if (onConfigure)
            onConfigure();
    });
    return menu;
}

ui::Hint LinkToggle::hintAt (juce::Point<int> position)
{
    ui::Hint h;
    if (arrowArea().contains (position))
        h.text = tr ("Ableton Link options");
    else
        h.text = on ? tr ("Ableton Link is on: the tempo and the beat are shared with the other apps and devices in the session. Click to leave it")
                    : tr ("Ableton Link: share the tempo and the beat with other apps and devices on the network. Click to join");
    return h;
}

void KeyPanel::setText (const juce::String& keyName)
{
    if (keyName != text)
    {
        text = keyName;
        repaint();
    }
}

void KeyPanel::setLook (toolbarlayout::Look l)
{
    if (l != look)
    {
        look = l;
        repaint();
    }
}

void KeyPanel::paint (juce::Graphics& g)
{
    using namespace theme;
    if (look == toolbarlayout::Look::live) // round 190iv: Live's key / scale box ("C# minor ▾")
    {
        const auto area = getLocalBounds();
        paintLiveBox (g, area, isMouseOver (true));
        paintLiveText (g, text.isEmpty() ? tr ("none") : text, area.withTrimmedRight (10), false, juce::Justification::centredLeft);
        paintLiveArrow (g, area, live::text);
        return;
    }
    if (look == toolbarlayout::Look::simple) // round 190iv: BandLab's Key capsule ("D min"; "Key" while there is none)
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (isMouseOver (true) ? colour::bg5 : colour::bg4);
        g.fillRoundedRectangle (area, area.getHeight() / 2.0f);
        const auto none = text.isEmpty();
        auto f = font (15.0f, ! none);
        const auto shown = none ? tr ("Key") : text;
        for (int i = 0; i < 8 && textcache::stringWidth (f, shown) > area.getWidth() - 20.0f; ++i)
            f = f.withHeight (f.getHeight() * 0.92f);
        g.setFont (f);
        g.setColour (none ? colour::textSecondary : colour::textPrimary);
        g.drawText (shown, area, juce::Justification::centred, false);
        return;
    }
    auto area = getLocalBounds();
    g.setColour (isMouseOver (true) ? colour::bg2 : colour::bg1);
    g.fillRoundedRectangle (area.toFloat(), radius::md);
    area.reduce (7, 3);
    // the menu arrow on the right
    const auto arrow = area.removeFromRight (9).toFloat();
    juce::Path tri;
    tri.addTriangle (arrow.getX(), arrow.getCentreY() - 2.0f, arrow.getRight(), arrow.getCentreY() - 2.0f, arrow.getCentreX(), arrow.getCentreY() + 3.0f);
    g.setColour (colour::textSecondary);
    g.fillPath (tri);
    area.removeFromRight (3);
    // "Key" over the name (no key: "none", dimmed)
    const auto caption = area.removeFromTop (juce::jmax (12, area.getHeight() * 2 / 5));
    g.setFont (font (type::caption));
    g.drawText (tr ("Key"), caption, juce::Justification::bottomLeft, false);
    const auto none = text.isEmpty();
    g.setColour (none ? colour::textSecondary : colour::textPrimary);
    auto nameFont = font (type::body, ! none).withHeight (juce::jmin (17.0f, (float) area.getHeight() * 0.8f));
    const auto shown = none ? tr ("none") : text;
    for (int i = 0; i < 8 && textcache::stringWidth (nameFont, shown) > (float) area.getWidth(); ++i)
        nameFont = nameFont.withHeight (nameFont.getHeight() * 0.92f);
    g.setFont (nameFont);
    g.drawText (shown, area, juce::Justification::centredLeft, true);
}

void KeyPanel::mouseDown (const juce::MouseEvent& e)
{
    if ((e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) && onClick)
        onClick();
}

ui::Hint KeyPanel::hintAt (juce::Point<int>)
{
    return { tr ("Project key: the scale the song is in. Click to choose it, or to guess it from the notes"), {}, false, false };
}

void ChordHint::setText (const juce::String& newText)
{
    if (newText != text)
    {
        text = newText;
        repaint();
    }
}

void ChordHint::paint (juce::Graphics& g)
{
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText (theme::font (theme::type::body, true).withHeight (17.0f), text, 2.0f, 0.0f, (float) getWidth() - 4.0f, (float) getHeight(),
                          juce::Justification::centredLeft, 1, 1.0f);
    juce::Path outline;
    glyphs.createPath (outline);
    g.setColour (juce::Colours::black);
    g.strokePath (outline, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white);
    g.fillPath (outline);
}

void MidiActivityLight::paint (juce::Graphics& g)
{
    const auto size = juce::jmin ((float) getWidth(), (float) getHeight()) * 0.36f;
    const auto led = getLocalBounds().toFloat().withSizeKeepingCentre (size, size);
    const auto lit = litFrames > 0;
    // FL's colours (round 190ld): orange = a project link took it, blue = a global link (and the bar's first beat of
    // the MIDI Sync light), green = no link used it (and the other beats)
    const auto litColour = kind == Kind::project ? theme::colour::warning
                         : kind == Kind::global || kind == Kind::bar ? theme::colour::info : theme::colour::ledOn;

    if (lit) // a soft glow around the lit light
    {
        g.setColour (litColour.withAlpha (0.25f));
        g.fillEllipse (led.expanded (size * 0.35f));
    }
    g.setColour (lit ? litColour : (inputs.isEmpty() ? theme::colour::bg4 : theme::colour::ledOff));
    g.fillEllipse (led);
    g.setColour (theme::colour::borderStrong);
    g.drawEllipse (led, 1.0f);
}

void MidiActivityLight::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && onReconnect != nullptr)
        onReconnect();
}

ui::Hint MidiActivityLight::hintAt (juce::Point<int>)
{
    ui::Hint h;
    h.text = tr ("MIDI input: blinks when a connected device sends MIDI: orange = a project link used it, blue = a global link, green = nothing linked. With master sync sent it blinks on each beat, blue on each bar. Click to reconnect the MIDI inputs.");
    h.text << "  " << (inputs.isEmpty() ? tr ("No MIDI input is open.") : tr ("Open: {0}", { inputs.joinIntoString (", ") }));
    return h;
}

ui::Hint PerformanceMeter::hintAt (juce::Point<int> p)
{
    ui::Hint h;

    if (p.y < getHeight() / 2)
    {
        h.text = tr ("CPU: how much of the audio deadline the processing takes; near 100% the sound breaks up");
        if (underruns > 0)
            h.text << "  " << tr ("{0} underruns (the sound broke up; red number)", { juce::String ((juce::int64) underruns) });
        if (overDeadline > 0 || lateCalls > 0)
            h.text << "  " << tr ("{0} audio blocks missed the deadline (crackles), the device called late {1} times; log: audio-glitches.log",
                                  { juce::String ((juce::int64) overDeadline), juce::String ((juce::int64) lateCalls) });
        if (! heaviest.isEmpty())
            h.text << "  " << tr ("Heaviest: {0}", { heaviest.joinIntoString (", ") });
        if (dropoutText.isNotEmpty())
            h.text << "  " << dropoutText;
        if (underruns > 0 && driverTip.isNotEmpty())
            h.text << "  " << tr ("Tip: Windows Audio (shared) drops out more easily; choose the ASIO driver \"{0}\" in Options > Audio settings.",
                                  { driverTip });
        h.value = juce::String (cpu * 100.0, 1) + "%" + (peakLoad > 0.0 ? "  (" + tr ("peak {0}%", { juce::String (juce::roundToInt (peakLoad * 100.0)) }) + ")" : juce::String());
        if (look != toolbarlayout::Look::live)
            h.text << "  " << tr ("The graph under the bar is the load over the last seconds.");
        h.text << "  " << tr ("[Double-click] plugin performance monitor  [RMB] refresh rate");
        h.hasMenu = true;
    }
    else if (p.x < getWidth() * 58 / 100)
    {
        h.text = tr ("RAM: memory used by LYNX SHARE (samples, plugins, undo history)");
        h.value = juce::String ((double) memory / (1024.0 * 1024.0), 0) + " MB";
    }
    else
    {
        h.text = tr ("Polyphony: notes sounding now in the song and from the keyboard (voices still releasing are not counted)");
        h.value = juce::String (voices) + "  (" + tr ("max {0}", { juce::String (peakVoices) }) + ")";
    }

    return h;
}

// --- output monitor ------------------------------------------------------------------------------

bool OutputMonitor::update (double nowMs)
{
    if (! isShowing() || getWidth() <= 0)
    {
        stopOverlay();
        return false;
    }

    // the time since the last frame (the falloffs below are per 1/30 s, as when this ran on a 30 Hz timer)
    const auto elapsed = lastMs > 0.0 ? juce::jlimit (0.0, 250.0, nowMs - lastMs) : 1000.0 / 30.0;
    lastMs = nowMs;
    constexpr auto columnPeriod = 1000.0 / 30.0;

    engine.copyScope (samples.data(), (int) samples.size());
    if (stereoScope) // (round 190mv: FL's Stereo, the sides from mid and side)
        engine.copyScopeSide (sideSamples.data(), (int) sideSamples.size());
    float now = 0.0f;
    for (auto s : samples)
        now = juce::jmax (now, std::abs (s));

    peak = juce::jmax (now, peak * ui::fallFor (0.82f, elapsed)); // fast attack, smooth fall

    holdMs -= elapsed;
    if (now >= held || holdMs <= 0.0)
    {
        held = now;
        holdMs = 1000.0; // the hold line stays about a second
    }

    if (now >= 0.999f)
        clipMs = 2000.0;
    else
        clipMs = juce::jmax (0.0, clipMs - elapsed);

    // round 190kn: the stereo bars (FL's output panel), the same laws per side as the mono meter above
    float sideNow[2] {};
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto v = engine.takeOutputPeak (ch);
        sideNow[ch] = v;
        const auto c = (std::size_t) ch;
        sidePeak[c] = juce::jmax (v, sidePeak[c] * ui::fallFor (0.82f, elapsed));
        sideHoldMs[c] -= elapsed;
        if (v >= sideHeld[c] || sideHoldMs[c] <= 0.0)
        {
            sideHeld[c] = v;
            sideHoldMs[c] = 1000.0;
        }
        sideClipMs[c] = v >= 0.999f ? 2000.0 : juce::jmax (0.0, sideClipMs[c] - elapsed);
    }
    now = juce::jmax (now, juce::jmax (sideNow[0], sideNow[1]));

    // silence after everything has fallen to zero (meter, hold, clip, the long view's history, the spectrum bands)
    // looks the same frame after frame: stop repainting while nothing plays (REA round 116, idle CPU). Below -80 dB
    // (a plugin's noise floor, a reverb's last tail) nothing shows: a 0.003 px wiggle on the scope, under the
    // spectrum's -72 dB floor (round 174e: it kept the toolbar repainting at the display's rate while stopped)
    constexpr auto visible = 1.0e-4f;
    silentMs = now > visible ? 0.0 : juce::jmin (silentMs + elapsed, 1.0e9);
    if (silentMs > ((double) history.size() + 90.0) * columnPeriod)
    {
        peak = held = column = 0.0f;
        sidePeak.fill (0.0f);
        sideHeld.fill (0.0f);
        bands.fill (0.0f);
        columnMs = 0.0;
        stopOverlay();
        return false;
    }

    // long view: the level scrolls from right to left, one column per 1/30 s (the loudest frame of it)
    column = juce::jmax (column, now);
    columnMs += elapsed;
    if (columnMs >= columnPeriod)
    {
        const auto columns = juce::jmin ((int) history.size(), (int) (columnMs / columnPeriod));
        columnMs -= columns * columnPeriod;
        std::rotate (history.begin(), history.begin() + columns, history.end());
        std::fill (history.end() - columns, history.end(), column);
        column = 0.0f;
        if (mode == longView)
            frameChanged();
    }

    if (mode == spectrum)
    {
        // Hann-windowed FFT of the last 1024 samples, folded into log-spaced bands (20 Hz..20 kHz)
        static juce::dsp::FFT fft (10);
        std::array<float, 2048> data {};
        for (std::size_t i = 0; i < samples.size(); ++i)
            data[i] = samples[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (samples.size() - 1)));
        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        const auto rate = juce::jmax (8000.0, engine.getTransport().sampleRate.load());
        const auto fall = ui::fallFor (0.85f, elapsed);
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            const auto f0 = 20.0 * std::pow (1000.0, (double) b / (double) bands.size());
            const auto f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / (double) bands.size());
            auto i0 = (int) (f0 / rate * 1024.0), i1 = juce::jmax (i0 + 1, (int) (f1 / rate * 1024.0));
            i0 = juce::jlimit (1, 511, i0);
            i1 = juce::jlimit (i0 + 1, 512, i1);
            float m = 0.0f;
            for (auto i = i0; i < i1; ++i)
                m = juce::jmax (m, data[(std::size_t) i]);
            const auto db = juce::Decibels::gainToDecibels (m / 256.0f, -72.0f);
            const auto level = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
            bands[b] = juce::jmax (level, bands[b] * fall); // fast up, smooth fall
        }
    }

    // (the long view only moves with its columns, above; the scope and the spectrum every frame)
    if (mode != longView)
        frameChanged();
    else if (gpuDisplay.isShowing())
        frameChanged(); // (the stereo bars move every frame: on the overlay that is the whole picture)
    else
        repaint (barsArea().getSmallestIntegerContainer()); // (the stereo bars move every frame)
    return true;
}

std::atomic<int>& OutputMonitor::overlayFrames()
{
    static std::atomic<int> n { 0 };
    return n;
}

void OutputMonitor::frameChanged()
{
    static const bool wanted = [] {
#if JUCE_WINDOWS
        const auto* v = std::getenv ("LYNX_SCOPE_OVERLAY");
        return v == nullptr || juce::String (v) != "0";
#else
        return false; // (the X11 / macOS overlays send a whole picture per frame: no gain over the repaint there)
#endif
    }();
    if (wanted)
    {
        const auto was = gpuDisplay.isShowing();
        const auto area = getLocalBounds();
        if (gpuDisplay.show ({ 0.0f, area, area, ++overlayKey }))
        {
            ++overlayFrames();
            if (! was)
                repaint(); // (once: the window shows what is behind the display, the overlay's picture over it)
            return;
        }
        if (was)
            gpuDisplay.hide();
    }
    repaint();
}

void OutputMonitor::stopOverlay()
{
    if (gpuDisplay.isShowing())
    {
        gpuDisplay.hide (true); // (it stays a few frames: the repaint below is on screen first)
        repaint();
    }
}

juce::String OutputMonitor::modeName (int m)
{
    switch (m)
    {
        case longView: return tr ("Long view (level over time)");
        case spectrum: return tr ("Spectrum (low frequencies on the left)");
        default: return tr ("Oscilloscope (waveform)");
    }
}

void OutputMonitor::setMode (int newMode)
{
    mode = juce::jlimit (0, modeCount - 1, newMode);
    bands.fill (0.0f);
    repaint();
    if (onModeChanged)
        onModeChanged();
}

void OutputMonitor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Output display"));
        for (int m = 0; m < modeCount; ++m)
            menu.addItem (modeName (m), true, mode == m, [this, m] { setMode (m); });
        // FL's output panel menu › Oscilloscope › "Stereo" (round 190mv, DF-24; capture 190mv A8)
        menu.addSectionHeader (tr ("Oscilloscope"));
        menu.addItem (tr ("Stereo"), true, stereoScope, [this] {
            stereoScope = ! stereoScope;
            repaint();
            if (onModeChanged)
                onModeChanged(); // (saved with the mode)
        });
        menu.addSeparator();
        menu.addItem (tr ("Reset the peak hold and clip light"), [this] {
            held = 0.0f;
            clipMs = holdMs = 0.0;
            sideHeld.fill (0.0f);
            sideClipMs.fill (0.0);
            sideHoldMs.fill (0.0);
            repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }

    if (e.mods.isLeftButtonDown())
        setMode ((mode + 1) % modeCount);
}

void OutputMonitor::paint (juce::Graphics& g)
{
    if (gpuDisplay.isShowing())
        return; // (round 190kz: the overlay shows it)
    paintDisplay (g, false);
}

void OutputMonitor::paintDisplay (juce::Graphics& g, bool software)
{
    using namespace theme;
    auto area = getLocalBounds().toFloat();
    g.setColour (colour::bg0);
    g.fillRoundedRectangle (area, radius::md);

    // round 190kn (FL's output panel, DF-23): the left and right peak bars beside the display, -60..0 dB from the left;
    // a side's bar turns red while it clipped recently (FL: "If the bars turn red your project is clipping")
    auto level = [] (float gain) { return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, -60.0f) + 60.0f) / 60.0f); };
    area.removeFromRight (getLocalBounds().toFloat().getRight() - barsArea().getX() + 2.0f);
    for (int side = 0; side < 2; ++side)
    {
        const auto bar = barArea (side);
        const auto c = (std::size_t) side;
        g.setColour (colour::bg3);
        g.fillRect (bar);
        const auto filled = bar.withWidth (bar.getWidth() * level (sidePeak[c]));
        if (sideClipMs[c] > 0.0)
            g.setColour (colour::meterClip);
        else
            g.setGradientFill (juce::ColourGradient::horizontal (colour::meterLow, bar.getX() + bar.getWidth() * 0.6f, colour::meterMid, bar.getRight()));
        g.fillRect (filled);
        if (sideHeld[c] > 0.0f)
        {
            g.setColour (sideClipMs[c] > 0.0 ? colour::meterClip : colour::textSecondary);
            g.fillRect (juce::Rectangle<float> (bar.getX() + bar.getWidth() * level (sideHeld[c]) - 1.0f, bar.getY(), 1.0f, bar.getHeight()));
        }
    }

    auto scope = area.reduced (4.0f, 3.0f);
    const auto lineColour = (clipMs > 0.0 ? colour::meterClip : colour::accent).withAlpha (0.9f);

    if (mode == longView) // the level of the last seconds, newest on the right, mirrored around the centre
    {
        g.setColour (colour::border);
        g.drawHorizontalLine ((int) scope.getCentreY(), scope.getX(), scope.getRight());
        const auto columns = juce::jmax (1, (int) scope.getWidth());
        g.setColour (lineColour.withAlpha (0.75f));
        for (int x = 0; x < columns; ++x)
        {
            const auto index = (std::size_t) (x * (int) history.size() / columns);
            const auto h = juce::jmax (0.5f, level (history[index]) * scope.getHeight() * 0.5f);
            g.fillRect (scope.getX() + (float) x, scope.getCentreY() - h, 1.0f, h * 2.0f);
        }
        return;
    }

    if (mode == spectrum) // bars per log-spaced band
    {
        const auto w = scope.getWidth() / (float) bands.size();
        g.setColour (lineColour.withAlpha (0.8f));
        for (std::size_t b = 0; b < bands.size(); ++b)
        {
            const auto h = bands[b] * scope.getHeight();
            g.fillRect (scope.getX() + (float) b * w, scope.getBottom() - h, juce::jmax (1.0f, w - 0.5f), h);
        }
        return;
    }

    // oscilloscope
    g.setColour (colour::border);
    g.drawHorizontalLine ((int) scope.getCentreY(), scope.getX(), scope.getRight());
    wave.clear(); // (round 190kz: the member keeps its storage, no allocation per frame)
    const auto width = juce::jmax (1, (int) scope.getWidth());
    // FL's Stereo (round 190mv, DF-24; capture 190mv A8): the left side's line in the upper half, the right's in the
    // lower half of the same box; off = one line of both
    const auto addLine = [&] (float mid, float half, int side) {
        for (int x = 0; x < width; ++x)
        {
            const auto index = (std::size_t) (x * (int) samples.size() / width);
            const auto v = side == 0 ? samples[index] : side == 1 ? samples[index] + sideSamples[index] : samples[index] - sideSamples[index];
            const auto y = mid - juce::jlimit (-1.0f, 1.0f, v) * half;
            if (x == 0)
                wave.startNewSubPath (scope.getX(), y);
            else
                wave.lineTo (scope.getX() + (float) x, y);
        }
    };
    if (stereoScope)
    {
        addLine (scope.getY() + scope.getHeight() * 0.25f, scope.getHeight() * 0.23f, 1);
        addLine (scope.getY() + scope.getHeight() * 0.75f, scope.getHeight() * 0.23f, 2);
    }
    else
        addLine (scope.getCentreY(), scope.getHeight() * 0.47f, 0);

    const auto stroke = juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    // round 174e2: the same stroke drawn by the software renderer into a small image, then the image to the window.
    // Direct2D builds and tessellates a new path geometry (and stroke style) for every strokePath: at the display's
    // rate that was the toolbar's biggest cost. LYNX_SCOPE_IMAGE=0: the direct stroke, for comparisons.
    static const bool viaImage = [] {
        const auto* v = std::getenv ("LYNX_SCOPE_IMAGE");
        return v == nullptr || juce::String (v) != "0";
    }();
    if (! viaImage || software) // (software: the overlay's picture, already drawn by the CPU)
    {
        g.setColour (lineColour);
        g.strokePath (wave, stroke);
        return;
    }
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto box = scope.expanded (2.0f).getSmallestIntegerContainer();
    const auto w = juce::jmax (1, juce::roundToInt ((float) box.getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) box.getHeight() * scale));
    if (scopeImage.getWidth() != w || scopeImage.getHeight() != h)
        scopeImage = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    else
        scopeImage.clear (scopeImage.getBounds());
    {
        juce::Graphics ig (scopeImage);
        ig.addTransform (juce::AffineTransform::translation ((float) -box.getX(), (float) -box.getY()).scaled (scale));
        ig.setColour (lineColour);
        ig.strokePath (wave, stroke);
    }
    // into a persistent window-type image (drawing the software image itself would convert it to a new GPU bitmap on
    // every frame): a write marks it for a small upload of these few pixels
    if (scopeNative.getWidth() != w || scopeNative.getHeight() != h)
        scopeNative = juce::Image (juce::Image::ARGB, w, h, true);
    {
        const juce::Image::BitmapData from (scopeImage, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData to (scopeNative, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            std::memcpy (to.getLinePointer (y), from.getLinePointer (y), (std::size_t) w * (std::size_t) from.pixelStride);
    }
    g.setColour (juce::Colours::black);
    g.drawImage (scopeNative, box.toFloat());
}

ui::Hint OutputMonitor::hintAt (juce::Point<int> p)
{
    ui::Hint h;
    h.hasMenu = true;
    const auto bars = barsArea();
    if (bars.expanded (1.0f, 2.0f).contains (p.toFloat())) // FL: the level at the pointer's position on the peak meter
    {
        const auto x01 = juce::jlimit (0.0f, 1.0f, ((float) p.x + 0.5f - bars.getX()) / juce::jmax (1.0f, bars.getWidth()));
        h.text = tr ("Output peak meter, left and right (a bar turns red when that side clipped). The value is the level at the pointer");
        // (FL 2026 hovering its output meter: "Peak meter (master)  -7.19dB 0.44", details190kn raw/f07)
        const auto db = levelAt (x01);
        h.value = juce::String (db, 2) + " dB  " + juce::String (juce::Decibels::decibelsToGain (db), 2);
        return h;
    }
    h.text = tr ("Output: {0} and the peak level of what you hear (the meter's top turns red when it clipped)  [LMB] next display  [RMB] display menu",
                 { modeName (mode).toLowerCase() });
    h.value = held <= 1.0e-5f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (held), 1) + " dB";
    return h;
}

juce::Rectangle<float> OutputMonitor::barsArea() const
{
    // FL keeps its bars beside the scope (about as wide as the scope); here the right 40 % of the panel, 2 bars with a
    // 1 px gap, centred on the panel's height
    auto area = getLocalBounds().toFloat().reduced (3.0f, 4.0f);
    const auto w = juce::jlimit (24.0f, 90.0f, std::round (area.getWidth() * 0.4f));
    const auto h = juce::jmin (area.getHeight(), 21.0f);
    return area.removeFromRight (w).withSizeKeepingCentre (w, h);
}

juce::Rectangle<float> OutputMonitor::barArea (int side) const
{
    const auto bars = barsArea();
    const auto each = std::floor ((bars.getHeight() - 1.0f) / 2.0f);
    return side == 0 ? bars.withHeight (each) : bars.withTrimmedTop (bars.getHeight() - each);
}

float OutputMonitor::levelAt (float x01) noexcept
{
    return -60.0f + 60.0f * juce::jlimit (0.0f, 1.0f, x01);
}

void OutputMonitor::setScopeSidesForShot (float leftPhase, float rightPhase)
{
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto t = (float) i / (float) samples.size() * juce::MathConstants<float>::twoPi * 6.0f;
        const auto l = 0.45f * std::sin (t + leftPhase), r = 0.3f * std::sin (1.5f * t + rightPhase);
        samples[i] = 0.5f * (l + r);
        sideSamples[i] = 0.5f * (l - r);
    }
    repaint();
}

void OutputMonitor::setSidesForShot (float left, float right, bool clipLeft, bool clipRight)
{
    sidePeak = { left, right };
    sideHeld = { juce::jmin (1.0f, left * 1.12f), juce::jmin (1.0f, right * 1.12f) };
    sideClipMs = { clipLeft ? 2000.0 : 0.0, clipRight ? 2000.0 : 0.0 };
    repaint();
}

// --- customize -------------------------------------------------------------------------------------

CustomizePanel::CustomizePanel (const toolbarlayout::Layout& l) : layout (l)
{
    setWantsKeyboardFocus (true);
    upButton.setButtonText (tr ("Move up"));
    downButton.setButtonText (tr ("Move down"));
    resetButton.setButtonText (tr ("Reset to default"));
    upButton.onClick = [this] { move (-1); };
    downButton.onClick = [this] { move (1); };
    resetButton.onClick = [this] {
        layout = toolbarlayout::Layout::makeDefault();
        iconSizeBox.setSelectedId ((int) layout.iconSize + 1, juce::dontSendNotification);
        scaleBox.setSelectedId (layout.uiScale, juce::dontSendNotification);
        rebuild();
        changed();
    };

    iconSizeBox.addItem (tr ("Small icons"), 1);
    iconSizeBox.addItem (tr ("Medium icons"), 2);
    iconSizeBox.addItem (tr ("Large icons"), 3);
    iconSizeBox.setSelectedId ((int) layout.iconSize + 1, juce::dontSendNotification);
    iconSizeBox.onChange = [this] {
        layout.iconSize = (toolbarlayout::IconSize) (iconSizeBox.getSelectedId() - 1);
        changed();
    };

    for (auto percent : toolbarlayout::uiScales)
        scaleBox.addItem (juce::String (percent) + "%", percent);
    if (scaleBox.indexOfItemId (layout.uiScale) < 0)
        scaleBox.addItem (juce::String (layout.uiScale) + "%", layout.uiScale);
    scaleBox.setSelectedId (layout.uiScale, juce::dontSendNotification);
    scaleBox.onChange = [this] {
        layout.uiScale = toolbarlayout::clampScale (scaleBox.getSelectedId());
        changed();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &upButton, &downButton, &resetButton, &iconSizeBox, &scaleBox })
        addAndMakeVisible (c);

    ui::setHint (iconSizeBox, tr ("Size of the toolbar icons and of the editors' tool icons (Piano Roll, Playlist)"));
    ui::setHint (scaleBox, tr ("Scales the whole interface: text, icons and windows. Useful on high-resolution screens"));
    rebuild();
    setSize (460, 2 * 16 + 24 + toolbarlayout::groupCount * rowHeight + rowHeight + 16 + 2 * 34 + 12 + 30);
}

juce::Rectangle<int> CustomizePanel::listArea() const
{
    return getLocalBounds().reduced (16).withTrimmedTop (24).withHeight ((toolbarlayout::groupCount + 1) * rowHeight).withTrimmedRight (140);
}

void CustomizePanel::rebuild()
{
    checks.clear();

    for (const auto& entry : layout.entries)
    {
        auto* check = checks.add (new juce::ToggleButton (tr (toolbarlayout::groupName (entry.group))));
        check->setToggleState (entry.shown, juce::dontSendNotification);
        check->setWantsKeyboardFocus (false);
        const auto group = entry.group;
        check->onClick = [this, check, group] {
            layout.setShown (group, check->getToggleState());
            select (group);
            changed();
        };
        // the transport can be hidden, but it is the last group to drop when the window is narrow
        addAndMakeVisible (check);
    }

    resized();
    repaint();
}

void CustomizePanel::select (toolbarlayout::Group g)
{
    selected = g;
    const auto at = layout.indexOf (g);
    upButton.setEnabled (at > 0);
    downButton.setEnabled (at >= 0 && at < (int) layout.entries.size() - 1);
    repaint();
}

void CustomizePanel::changed()
{
    if (onChange)
        onChange (layout);
}

void CustomizePanel::move (int delta)
{
    if (layout.move (selected, delta))
    {
        rebuild();
        select (selected);
        changed();
    }
}

void CustomizePanel::resized()
{
    auto list = listArea();
    list.removeFromTop (rowHeight); // the fixed first row: the menu and the hint bar

    for (auto* check : checks)
        check->setBounds (list.removeFromTop (rowHeight).reduced (8, 3));

    auto side = getLocalBounds().reduced (16).withTrimmedTop (24 + rowHeight).withLeft (listArea().getRight() + 12);
    upButton.setBounds (side.removeFromTop (28));
    side.removeFromTop (6);
    downButton.setBounds (side.removeFromTop (28));

    auto bottom = getLocalBounds().reduced (16).withTop (listArea().getBottom() + 16);
    auto row = bottom.removeFromTop (28);
    row.removeFromLeft (110);
    iconSizeBox.setBounds (row.removeFromLeft (140));
    bottom.removeFromTop (6);
    row = bottom.removeFromTop (28);
    row.removeFromLeft (110);
    scaleBox.setBounds (row.removeFromLeft (140));
    bottom.removeFromTop (12);
    resetButton.setBounds (bottom.removeFromTop (28).removeFromLeft (160));
    select (selected);
}

void CustomizePanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg2);
    auto area = getLocalBounds().reduced (16);
    g.setFont (font (type::small, true));
    g.setColour (colour::textSecondary);
    g.drawText (tr ("Toolbar groups, left to right (uncheck to hide):"), area.removeFromTop (20), juce::Justification::centredLeft);

    auto list = listArea();
    g.setColour (colour::bg1);
    g.fillRoundedRectangle (list.toFloat(), radius::md);

    auto first = list.removeFromTop (rowHeight).reduced (8, 3);
    g.setFont (font (type::body));
    g.setColour (colour::textDisabled);
    g.drawText (tr ("Menu and hint bar (always first)"), first.withTrimmedLeft (28), juce::Justification::centredLeft);

    const auto at = layout.indexOf (selected);
    if (at >= 0)
    {
        g.setColour (colour::accent.withAlpha (0.18f));
        g.fillRoundedRectangle (list.withY (list.getY() + at * rowHeight).withHeight (rowHeight).reduced (2).toFloat(), radius::md);
    }

    auto bottom = getLocalBounds().reduced (16).withTop (listArea().getBottom() + 16);
    g.setColour (colour::textSecondary);
    g.drawText (tr ("Icon size"), bottom.removeFromTop (28).withWidth (104), juce::Justification::centredLeft);
    bottom.removeFromTop (6);
    g.drawText (tr ("UI scale"), bottom.removeFromTop (28).withWidth (104), juce::Justification::centredLeft);
}

void CustomizePanel::mouseDown (const juce::MouseEvent& e)
{
    auto list = listArea();
    list.removeFromTop (rowHeight);

    if (list.contains (e.getPosition()))
    {
        const auto row = (e.y - list.getY()) / rowHeight;
        if (row >= 0 && row < (int) layout.entries.size())
            select (layout.entries[(std::size_t) row].group);
    }
}

bool CustomizePanel::keyPressed (const juce::KeyPress& key)
{
    const auto up = key.getKeyCode() == juce::KeyPress::upKey, down = key.getKeyCode() == juce::KeyPress::downKey;
    if (! up && ! down)
        return false;

    if (key.getModifiers().isAltDown())
        move (up ? -1 : 1);
    else
    {
        const auto at = juce::jlimit (0, (int) layout.entries.size() - 1, layout.indexOf (selected) + (up ? -1 : 1));
        select (layout.entries[(std::size_t) at].group);
    }
    return true;
}

// --- edit mode ---------------------------------------------------------------------------------------

Icon EditOverlay::groupIcon (toolbarlayout::Group g)
{
    using toolbarlayout::Group;
    switch (g)
    {
        case Group::transport: return Icon::play;
        case Group::master: return Icon::tuningFork;
        case Group::record: return Icon::metronome;
        case Group::snap: return Icon::magnet;
        case Group::windows: return Icon::playlist;
        case Group::file: return Icon::save;
        case Group::meters: return Icon::settings;
        case Group::output: return Icon::listen;
        case Group::chord: return Icon::pianoRoll;
        case Group::multilink: return Icon::link;
        case Group::key: return Icon::scaleSnap;
        case Group::count_: break;
    }
    return Icon::none;
}

EditOverlay::EditOverlay (Host h, int height) : host (std::move (h)), toolbarHeight (height)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);

    for (int p = 0; p < toolbarlayout::presetCount; ++p)
        presetBox.addItem (tr (toolbarlayout::presetName ((toolbarlayout::Preset) p)), p + 1);
    presetBox.setTextWhenNothingSelected (tr ("Custom"));
    presetBox.onChange = [this] {
        const auto id = presetBox.getSelectedId();
        if (id > 0)
            apply (toolbarlayout::applyPreset (host.layout(), (toolbarlayout::Preset) (id - 1)));
    };
    ui::setHint (presetBox, tr ("Toolbar presets: which panels show (LYNX SHARE's own layouts)"), false);
    resetButton.setButtonText (tr ("Reset"));
    resetButton.onClick = [this] {
        auto l = toolbarlayout::Layout::makeDefault();
        const auto now = host.layout();
        l.iconSize = now.iconSize;
        l.uiScale = now.uiScale;
        apply (l);
    };
    ui::setHint (resetButton, tr ("Every panel back, in the default order"));
    doneButton.setButtonText (tr ("Done"));
    doneButton.onClick = [this] {
        if (host.done)
            host.done();
    };
    ui::setHint (doneButton, tr ("Leave the toolbar's edit mode  [Esc]"));

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &resetButton, &doneButton })
    {
        c->setMouseCursor (juce::MouseCursor::NormalCursor);
        addAndMakeVisible (c);
    }
    layoutChanged();
}

void EditOverlay::setToolbarHeight (int height)
{
    toolbarHeight = height;
    resized();
    repaint();
}

void EditOverlay::layoutChanged()
{
    const auto p = toolbarlayout::matchingPreset (host.layout());
    presetBox.setSelectedId (p < toolbarlayout::presetCount ? p + 1 : 0, juce::dontSendNotification);
    repaint();
}

void EditOverlay::resized()
{
    auto row = trayArea().reduced (12, 0).removeFromBottom (40).withSizeKeepingCentre (getWidth() - 24, 28);
    doneButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    resetButton.setBounds (row.removeFromRight (90));
    row.removeFromRight (8);
    presetBox.setBounds (row.removeFromRight (190));
}

juce::Rectangle<int> EditOverlay::chipArea() const
{
    auto area = trayArea().reduced (12, 0);
    area.removeFromTop (22);
    area.removeFromBottom (6);
    return area.withTrimmedRight (390 + 12).withHeight (juce::jmin (area.getHeight(), 60));
}

juce::Rectangle<int> EditOverlay::chipBounds (int index) const
{
    const auto area = chipArea();
    const auto w = juce::jlimit (60, 132, area.getWidth() / toolbarlayout::groupCount);
    return { area.getX() + index * w, area.getY(), w - 6, area.getHeight() };
}

int EditOverlay::panelAt (juce::Point<int> p) const
{
    if (p.y >= toolbarHeight)
        return -1;
    const auto panels = host.panels();
    for (int g = 0; g < toolbarlayout::groupCount; ++g)
        if (panels[(std::size_t) g].contains (p))
            return g;
    return -1;
}

int EditOverlay::chipAt (juce::Point<int> p) const
{
    for (int g = 0; g < toolbarlayout::groupCount; ++g)
        if (chipBounds (g).contains (p))
            return g;
    return -1;
}

int EditOverlay::dropX() const
{
    if (! dragging || pressedGroup < 0 || dragPos.y >= toolbarHeight || dragPos.y < 0)
        return -1;
    const auto panels = host.panels();
    const auto layout = host.layout();
    std::vector<std::pair<toolbarlayout::Group, int>> shown;
    int lastRight = -1;
    for (const auto& e : layout.entries)
        if (const auto& r = panels[(std::size_t) e.group]; ! r.isEmpty())
        {
            shown.push_back ({ e.group, r.getCentreX() });
            if (e.group != (toolbarlayout::Group) pressedGroup)
                lastRight = juce::jmax (lastRight, r.getRight());
        }
    const auto before = toolbarlayout::dropBefore (shown, (toolbarlayout::Group) pressedGroup, dragPos.x);
    if (before >= toolbarlayout::groupCount)
        return lastRight >= 0 ? lastRight + 4 : host.fixedArea().getRight() + 8;
    return panels[(std::size_t) before].getX() - 5;
}

void EditOverlay::apply (toolbarlayout::Layout layout)
{
    if (host.setLayout)
        host.setLayout (layout);
    layoutChanged();
}

void EditOverlay::move (toolbarlayout::Group g, int delta)
{
    // among the panels on the toolbar: before the previous one, or before the one after the next
    auto layout = host.layout();
    std::vector<toolbarlayout::Group> shown;
    for (const auto& e : layout.entries)
        if (e.shown)
            shown.push_back (e.group);
    const auto at = (int) (std::find (shown.begin(), shown.end(), g) - shown.begin());
    if (at >= (int) shown.size())
        return;
    if (delta < 0 && at > 0)
        layout.placeBefore (g, &shown[(std::size_t) at - 1]);
    else if (delta > 0 && at + 1 < (int) shown.size())
    {
        if (at + 2 < (int) shown.size())
            layout.placeBefore (g, &shown[(std::size_t) at + 2]);
        else
            layout.placeBefore (g, nullptr);
    }
    apply (layout);
}

void EditOverlay::showPanelMenu (toolbarlayout::Group g, bool inTray)
{
    const auto layout = host.layout();
    const auto shown = layout.isShown (g);
    juce::PopupMenu menu;
    menu.addSectionHeader (tr (toolbarlayout::groupName (g)));
    if (shown)
    {
        menu.addItem (tr ("Remove from the toolbar"), [this, g] {
            auto l = host.layout();
            l.setShown (g, false);
            apply (l);
        });
        menu.addItem (tr ("Move left"), ! inTray, false, [this, g] { move (g, -1); });
        menu.addItem (tr ("Move right"), ! inTray, false, [this, g] { move (g, 1); });
    }
    else
        menu.addItem (tr ("Add to the toolbar"), [this, g] {
            auto l = host.layout();
            l.setShown (g, true);
            apply (l);
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void EditOverlay::paint (juce::Graphics& g)
{
    using namespace theme;
    const auto layout = host.layout();
    const auto panels = host.panels();

    // the toolbar: a wash over it, each panel outlined with its name
    g.setColour (colour::bg0.withAlpha (0.45f));
    g.fillRect (0, 0, getWidth(), toolbarHeight);

    if (const auto fixed = host.fixedArea(); ! fixed.isEmpty())
    {
        g.setColour (colour::bg0.withAlpha (0.35f));
        g.fillRoundedRectangle (fixed.toFloat().reduced (1.0f), radius::md);
    }

    for (int i = 0; i < toolbarlayout::groupCount; ++i)
    {
        const auto r = panels[(std::size_t) i].toFloat().reduced (1.5f, 3.0f);
        if (r.isEmpty())
            continue;
        const auto isDragged = dragging && ! pressedInTray && pressedGroup == i;
        const auto hot = hoverPanel == i && ! dragging;
        g.setColour (isDragged ? colour::accent.withAlpha (0.10f) : colour::bg4.withAlpha (hot ? 0.35f : 0.15f));
        g.fillRoundedRectangle (r, radius::md);
        g.setColour (isDragged || hot ? colour::accent : colour::borderStrong);
        g.drawRoundedRectangle (r, radius::md, isDragged ? 1.5f : 1.0f);

        // the name, on a small tab at the top left
        const auto name = tr (toolbarlayout::groupName ((toolbarlayout::Group) i));
        const juce::Font f (font (type::caption - 1.0f, true));
        const auto w = juce::jmin ((int) r.getWidth() - 6, juce::GlyphArrangement::getStringWidthInt (f, name) + 10);
        const auto tab = juce::Rectangle<float> (r.getX() + 3.0f, r.getY() + 2.0f, (float) w, 13.0f);
        g.setColour (hot || isDragged ? colour::accent : colour::bg6);
        g.fillRoundedRectangle (tab, 3.0f);
        g.setColour (colour::onAccent);
        g.setFont (f);
        g.drawText (name, tab, juce::Justification::centred, true);
    }

    // where a dragged panel lands
    if (const auto x = dropX(); x >= 0)
    {
        g.setColour (colour::accent);
        g.fillRoundedRectangle ((float) x - 1.5f, 4.0f, 3.0f, (float) toolbarHeight - 8.0f, 1.5f);
    }

    // the tray
    const auto tray = trayArea();
    const auto overTray = dragging && ! pressedInTray && tray.contains (dragPos);
    g.setColour (colour::bg2);
    g.fillRect (tray);
    g.setColour (overTray ? colour::accent : colour::border);
    g.fillRect (tray.withHeight (overTray ? 2 : 1));

    g.setFont (font (type::small, true));
    g.setColour (overTray ? colour::accentHover : colour::textSecondary);
    g.drawText (overTray ? tr ("Release to remove this panel from the toolbar")
                         : tr ("Edit toolbar: drag panels to move them, drag a panel here to remove it, drag one from here onto the toolbar to add it"),
                tray.reduced (12, 0).withHeight (22), juce::Justification::centredLeft, true);

    for (int i = 0; i < toolbarlayout::groupCount; ++i)
    {
        const auto group = (toolbarlayout::Group) i;
        const auto shown = layout.isShown (group);
        const auto r = chipBounds (i).toFloat();
        const auto hot = hoverChip == i && ! dragging;
        g.setColour (shown ? colour::bg3 : colour::bg4);
        g.fillRoundedRectangle (r, radius::md);
        g.setColour (hot ? colour::accent : shown ? colour::border : colour::borderStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.0f);

        auto inner = r.reduced (6.0f, 5.0f);
        const auto iconColour = shown ? colour::textDisabled : colour::textPrimary;
        icons::draw (g, groupIcon (group), inner.removeFromTop (18.0f).withSizeKeepingCentre (18.0f, 18.0f), iconColour);
        g.setFont (font (type::caption - 1.0f));
        g.setColour (shown ? colour::textDisabled : colour::textPrimary);
        g.drawFittedText (tr (toolbarlayout::groupName (group)), inner.toNearestInt(), juce::Justification::centred, 2, 0.85f);
        if (shown) // a tick: it is on the toolbar
        {
            g.setColour (colour::ledOn.withAlpha (0.8f));
            icons::draw (g, Icon::check, { r.getRight() - 15.0f, r.getY() + 3.0f, 12.0f, 12.0f }, colour::ledOn.withAlpha (0.8f));
        }
    }

    // the dragged panel follows the mouse
    if (dragging && dragImage.isValid())
    {
        g.setOpacity (0.85f);
        g.drawImageAt (dragImage, dragPos.x - grabOffset.x, dragPos.y - grabOffset.y);
    }
}

void EditOverlay::mouseMove (const juce::MouseEvent& e)
{
    const auto p = panelAt (e.getPosition()), c = chipAt (e.getPosition());
    if (p != hoverPanel || c != hoverChip)
    {
        hoverPanel = p;
        hoverChip = c;
        repaint();
    }
}

void EditOverlay::mouseExit (const juce::MouseEvent&)
{
    hoverPanel = hoverChip = -1;
    repaint();
}

void EditOverlay::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    pressedGroup = panelAt (e.getPosition());
    pressedInTray = false;
    if (pressedGroup < 0)
    {
        pressedGroup = chipAt (e.getPosition());
        pressedInTray = pressedGroup >= 0;
    }

    if (pressedGroup >= 0 && e.mods.isPopupMenu())
    {
        showPanelMenu ((toolbarlayout::Group) pressedGroup, pressedInTray);
        pressedGroup = -1;
    }
}

void EditOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (pressedGroup < 0 || ! e.mods.isLeftButtonDown())
        return;

    if (! dragging && e.getDistanceFromDragStart() > 4)
    {
        dragging = true;
        const auto from = pressedInTray ? chipBounds (pressedGroup) : host.panels()[(std::size_t) pressedGroup];
        grabOffset = e.getMouseDownPosition() - from.getPosition();
        dragImage = ! pressedInTray && host.snapshot ? host.snapshot (from) : juce::Image();
        if (! dragImage.isValid()) // a chip: its own picture
        {
            dragImage = juce::Image (juce::Image::ARGB, from.getWidth(), from.getHeight(), true);
            juce::Graphics ig (dragImage);
            ig.setColour (theme::colour::bg4);
            ig.fillRoundedRectangle (dragImage.getBounds().toFloat(), theme::radius::md);
            ig.setColour (theme::colour::accent);
            ig.drawRoundedRectangle (dragImage.getBounds().toFloat().reduced (0.5f), theme::radius::md, 1.0f);
            icons::draw (ig, groupIcon ((toolbarlayout::Group) pressedGroup), { 6.0f, 5.0f, 18.0f, 18.0f }, theme::colour::textPrimary);
            ig.setFont (theme::font (theme::type::caption));
            ig.setColour (theme::colour::textPrimary);
            ig.drawFittedText (tr (toolbarlayout::groupName ((toolbarlayout::Group) pressedGroup)), dragImage.getBounds().reduced (6).withTrimmedTop (18),
                               juce::Justification::centred, 2);
        }
    }

    if (dragging)
    {
        dragPos = e.getPosition();
        repaint();
    }
}

void EditOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging || pressedGroup < 0)
    {
        pressedGroup = -1;
        dragging = false;
        return;
    }

    const auto group = (toolbarlayout::Group) pressedGroup;
    const auto at = e.getPosition();
    auto layout = host.layout();

    if (at.y >= 0 && at.y < toolbarHeight) // onto the toolbar: shown, before the panel right of the drop point
    {
        const auto panels = host.panels();
        std::vector<std::pair<toolbarlayout::Group, int>> shown;
        for (const auto& entry : layout.entries)
            if (! panels[(std::size_t) entry.group].isEmpty())
                shown.push_back ({ entry.group, panels[(std::size_t) entry.group].getCentreX() });
        const auto before = toolbarlayout::dropBefore (shown, group, at.x);
        layout.setShown (group, true);
        if (before >= toolbarlayout::groupCount)
            layout.placeBefore (group, nullptr);
        else
        {
            const auto target = (toolbarlayout::Group) before;
            layout.placeBefore (group, &target);
        }
    }
    else if (! pressedInTray && trayArea().contains (at)) // into the tray: removed
        layout.setShown (group, false);

    dragging = false;
    pressedGroup = -1;
    dragImage = {};
    apply (layout);
    repaint();
}

void EditOverlay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto c = chipAt (e.getPosition()); c >= 0)
    {
        auto layout = host.layout();
        layout.setShown ((toolbarlayout::Group) c, ! layout.isShown ((toolbarlayout::Group) c));
        apply (layout);
    }
}

bool EditOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        if (host.done)
            host.done();
        return true;
    }
    return false;
}

ui::Hint EditOverlay::hintAt (juce::Point<int> p)
{
    ui::Hint h;
    h.hasMenu = true;
    if (const auto panel = panelAt (p); panel >= 0)
    {
        h.text = tr (toolbarlayout::groupName ((toolbarlayout::Group) panel)) + ": "
                 + tr ("[LMB] drag to move it  drag it into the tray to remove it  [RMB] menu");
        return h;
    }
    if (const auto chip = chipAt (p); chip >= 0)
    {
        const auto shown = host.layout().isShown ((toolbarlayout::Group) chip);
        h.text = tr (toolbarlayout::groupName ((toolbarlayout::Group) chip)) + ": "
                 + (shown ? tr ("on the toolbar  [2xLMB] remove it  [RMB] menu") : tr ("drag it onto the toolbar to add it there  [2xLMB] add it at the end  [RMB] menu"));
        return h;
    }
    if (p.y < toolbarHeight)
        h.text = tr ("Edit toolbar: the menu and the hint bar stay first  [Esc] done");
    else
        h.text = tr ("Edit toolbar: drag panels to and from the toolbar  [Esc] done");
    h.hasMenu = false;
    return h;
}

// --- round 190iv: SignatureBox, QuantBox, SaveStatus ------------------------------------------------------------------

void SignatureBox::setLook (toolbarlayout::Look l)
{
    if (l != look)
    {
        look = l;
        repaint();
    }
}

juce::String SignatureBox::text() const
{
    const auto& song = engine.getSong();
    return juce::String (song.beatsPerBar) + " / " + juce::String (song.beatUnit);
}

void SignatureBox::update()
{
    // (the timer's check compares two numbers; the text is made only when they changed)
    const auto& song = engine.getSong();
    if (const auto now = song.beatsPerBar * 1000 + song.beatUnit; now != shownSignature)
    {
        shownSignature = now;
        shown = text();
        repaint();
    }
}

void SignatureBox::paint (juce::Graphics& g)
{
    const auto t = shown.isEmpty() ? text() : shown;
    if (look == toolbarlayout::Look::live)
    {
        paintLiveBox (g, getLocalBounds(), isMouseOver());
        paintLiveText (g, t, getLocalBounds());
        return;
    }
    paintCapsuleHover (g, getLocalBounds(), isMouseOver() && isEnabled(), true); // (the tempo capsule's last segment)
    g.setFont (theme::font (15.0f, true));
    g.setColour (isEnabled() ? theme::colour::textPrimary : theme::colour::textDisabled); // (round 190jj: greyed while recording)
    g.drawText (t, getLocalBounds(), juce::Justification::centred, false);
}

juce::PopupMenu SignatureBox::makeMenu()
{
    // (the Pattern menu's "Song time signature" list)
    juce::PopupMenu menu;
    const auto& song = engine.getSong();
    for (int beats : { 2, 3, 4, 5, 6, 7, 8, 9, 12 })
        menu.addItem (juce::String (beats) + "/4", true, song.beatsPerBar == beats && song.beatUnit == 4, [this, beats] {
            engine.editSong ([beats] (Song& s) {
                s.beatsPerBar = beats;
                s.beatUnit = 4;
            });
            update();
        });
    return menu;
}

void SignatureBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
        makeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

ui::Hint SignatureBox::hintAt (juce::Point<int>)
{
    return { tr ("Song time signature"), {}, false, true };
}

namespace
{
// the launch quantisations, as the clip grid's footer box lists them (ClipGridComponent.cpp quantNames, Global left out)
const std::pair<LaunchQuant, const char*> globalQuantNames[] = {
    { LaunchQuant::none, TR_NOOP ("None") }, { LaunchQuant::bars8, TR_NOOP ("8 bars") }, { LaunchQuant::bars4, TR_NOOP ("4 bars") },
    { LaunchQuant::bars2, TR_NOOP ("2 bars") }, { LaunchQuant::bar, TR_NOOP ("1 bar") }, { LaunchQuant::half, "1/2" },
    { LaunchQuant::halfTriplet, "1/2T" }, { LaunchQuant::quarter, "1/4" }, { LaunchQuant::quarterTriplet, "1/4T" },
    { LaunchQuant::eighth, "1/8" }, { LaunchQuant::eighthTriplet, "1/8T" }, { LaunchQuant::sixteenth, "1/16" },
    { LaunchQuant::sixteenthTriplet, "1/16T" }, { LaunchQuant::thirtySecond, "1/32" }
};
juce::String quantName (LaunchQuant q)
{
    for (const auto& [value, name] : globalQuantNames)
        if (value == q)
            return value == LaunchQuant::none || value <= LaunchQuant::bar ? tr (name) : juce::String (name);
    return {};
}
LaunchQuant globalQuantOf (const Song& song)
{
    return song.clipGrid ? song.clipGrid->globalQuant : ClipGrid {}.globalQuant;
}
} // namespace

juce::String QuantBox::text() const { return quantName (globalQuantOf (engine.getSong())); }

void QuantBox::update()
{
    if (const auto now = (int) globalQuantOf (engine.getSong()); now != shownQuant)
    {
        shownQuant = now;
        shown = text();
        repaint();
    }
}

void QuantBox::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    paintLiveBox (g, area, isMouseOver());
    paintLiveText (g, shown.isEmpty() ? text() : shown, area.withTrimmedRight (9), false, juce::Justification::centredLeft);
    paintLiveArrow (g, area, live::text);
}

juce::PopupMenu QuantBox::makeMenu()
{
    juce::PopupMenu menu;
    const auto now = globalQuantOf (engine.getSong());
    for (const auto& [q, name] : globalQuantNames)
        menu.addItem (quantName (q), true, q == now, [this, q = q] {
            engine.editSong ([q] (Song& s) { launcher::ensureGrid (s).globalQuant = q; }, false);
            update();
        });
    return menu;
}

void QuantBox::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() || e.mods.isPopupMenu())
        makeMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

ui::Hint QuantBox::hintAt (juce::Point<int>)
{
    return { tr ("Launch quantisation: when a launched clip starts"), {}, false, true };
}

void SaveStatus::set (toolbarlayout::SaveState newState, const juce::String& newText)
{
    if (known && newState == state && newText == text)
        return;
    known = true;
    state = newState;
    text = newText;
    repaint();
}

void SaveStatus::paint (juce::Graphics& g)
{
    using namespace theme;
    using toolbarlayout::SaveState;
    const auto pill = getLocalBounds().toFloat().withSizeKeepingCentre ((float) getWidth(), (float) toolbarlayout::simplebar::statusHeight);
    if (isMouseOver())
    {
        g.setColour (colour::bg4);
        g.fillRoundedRectangle (pill, pill.getHeight() / 2.0f);
    }
    auto content = pill.reduced (10.0f, 0.0f);
    const auto iconBox = content.removeFromLeft (14.0f).withSizeKeepingCentre (14.0f, 14.0f);
    switch (state)
    {
        case SaveState::saved: icons::draw (g, Icon::cloudCheck, iconBox.withSizeKeepingCentre (18.0f, 18.0f), colour::ledOn); break;
        case SaveState::backedUp: icons::draw (g, Icon::cloud, iconBox.withSizeKeepingCentre (18.0f, 18.0f), colour::textSecondary); break;
        case SaveState::unsaved:
            g.setColour (colour::warning);
            g.fillEllipse (iconBox.withSizeKeepingCentre (8.0f, 8.0f));
            break;
        case SaveState::fresh: icons::draw (g, Icon::cloud, iconBox.withSizeKeepingCentre (18.0f, 18.0f), colour::textDisabled); break;
    }
    content.removeFromLeft (6.0f);
    g.setFont (font (type::small));
    g.setColour (state == SaveState::fresh ? colour::textSecondary : colour::textPrimary);
    g.drawFittedText (text, content.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

void SaveStatus::mouseUp (const juce::MouseEvent& e)
{
    if (getLocalBounds().contains (e.getPosition()) && ! e.mods.isPopupMenu() && onSave)
        onSave();
}

ui::Hint SaveStatus::hintAt (juce::Point<int>)
{
    return { tr ("LYNX backs your work up by itself. Click to save the project (Ctrl+S)"), {}, false, false };
}
} // namespace lynx::toolbar

// The process memory comes from the OS; its headers go last (windows.h defines macros such as `small`).
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define PSAPI_VERSION 2 // GetProcessMemoryInfo from kernel32 (K32GetProcessMemoryInfo): no extra library
 #include <windows.h>
 #include <psapi.h>
 #include <tlhelp32.h>
#endif

std::int64_t lynx::toolbar::PerformanceMeter::processPrivateBytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX counters {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &counters, sizeof (counters)))
        return (std::int64_t) counters.PrivateUsage;
    return 0;
#else
    return processMemoryBytes();
#endif
}

int lynx::toolbar::PerformanceMeter::threadCount()
{
#if JUCE_WINDOWS
    const auto snap = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    THREADENTRY32 entry {};
    entry.dwSize = sizeof (entry);
    int n = 0;
    const auto self = GetCurrentProcessId();
    for (auto ok = Thread32First (snap, &entry); ok; ok = Thread32Next (snap, &entry))
        if (entry.th32OwnerProcessID == self)
            ++n;
    CloseHandle (snap);
    return n;
#elif JUCE_LINUX
    for (const auto& line : juce::StringArray::fromLines (juce::File ("/proc/self/status").loadFileAsString()))
        if (line.startsWith ("Threads:"))
            return line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
    return 0;
#else
    return 0;
#endif
}

std::pair<int, int> lynx::toolbar::PerformanceMeter::guiHandles()
{
#if JUCE_WINDOWS
    return { (int) GetGuiResources (GetCurrentProcess(), 0 /* GR_GDIOBJECTS */), (int) GetGuiResources (GetCurrentProcess(), 1 /* GR_USEROBJECTS */) };
#else
    return { -1, -1 };
#endif
}

std::int64_t lynx::toolbar::PerformanceMeter::processMemoryBytes()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), &counters, sizeof (counters)))
        return (std::int64_t) counters.WorkingSetSize;
    return 0;
#elif JUCE_LINUX
    // resident pages, from /proc
    const auto fields = juce::StringArray::fromTokens (juce::File ("/proc/self/statm").loadFileAsString(), " ", "");
    return fields.size() > 1 ? (std::int64_t) fields[1].getLargeIntValue() * 4096 : 0;
#else
    return 0;
#endif
}
