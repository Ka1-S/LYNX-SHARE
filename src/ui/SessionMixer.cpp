#include "ui/TextCache.h"
#include "ui/SessionMixer.h"
#include "ui/PaintAB.h"

#include <algorithm>
#include <cmath>

#include "core/ClipLauncher.h"
#include "core/Crossfader.h"
#include "core/TrackStates.h"
#include "engine/AudioEngine.h"
#include "ui/ClipGridComponent.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/LiveLook.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
namespace
{
namespace c = theme::colour;
using Part = SessionMixerComponent::Part;
constexpr int pad = 5;
constexpr int maxSendRows = 6; // (more sends stay reachable in the Mixer)

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// The reference's dB labels beside a Session meter: 0, 12 ... 60 below 0 dB [SHOT session1.png]; drawn on LYNX's own
// meter scale (-60 dB at the bottom, +6 dB at the top: core/MeterBallistics.h).
constexpr int scaleLabels[] = { 0, 12, 24, 36, 48, 60 };

juce::String insertName (const Song& song, int insert)
{
    if (insert == 0)
        return tr ("Main");
    if (insert < 0 || insert >= (int) song.mixer.size())
        return {};
    const auto& name = song.mixer[(std::size_t) insert].name;
    return juce::String (insert) + " " + (name.empty() ? tr ("Insert {0}", { juce::String (insert) }) : utf8 (name));
}

void drawBox (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool arrow, bool hot, bool dim = false)
{
    const auto box = r.toFloat().reduced (0.5f);
    theme::fillRoundRect (g, box, 2.0f, hot ? c::bg5 : c::bg0);
    g.setColour (c::borderStrong);
    g.drawRoundedRectangle (box, 2.0f, 1.0f);
    g.setColour (dim ? c::textDisabled : c::textPrimary);
    g.setFont (theme::font (theme::type::caption));
    auto inner = r.reduced (4, 0);
    if (arrow)
    {
        const auto a = inner.removeFromRight (8).toFloat().withSizeKeepingCentre (7.0f, 4.0f);
        juce::Path tri;
        tri.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
        g.fillPath (tri);
    }
    textcache::drawText (g, text, inner, juce::Justification::centredLeft, true);
}

void drawToggle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool on, juce::Colour onColour, bool hot, bool fit = false)
{
    const auto box = r.toFloat().reduced (0.5f);
    theme::fillRoundRect (g, box, 2.0f, on ? onColour : hot ? c::bg5 : c::bg4);
    if (! on)
    {
        g.setColour (c::borderStrong);
        g.drawRoundedRectangle (box, 2.0f, 1.0f);
    }
    g.setColour (on ? c::bg0 : c::textSecondary);
    g.setFont (theme::font (theme::type::caption, on));
    if (fit) // (round 190oj, the monitor switch: a name wider than its part narrows a little rather than losing letters)
        textcache::drawFittedText (g, text, r.reduced (1, 0), juce::Justification::centred, 1, 0.75f);
    else
        textcache::drawText (g, text, r, juce::Justification::centred, false);
}

// Live's In | Auto | Off switch (round 190oj: in three equal 20 px parts "Auto" was cut to "Aut"): Auto gets two fifths
// of the row, In and Off three tenths each, 2 px apart
juce::Rectangle<int> monitorPart (juce::Rectangle<int> r, int index)
{
    const auto room = r.getWidth() - 4;
    const auto side = room * 3 / 10, middle = room - 2 * side;
    const auto x = index == 0 ? r.getX() : index == 1 ? r.getX() + side + 2 : r.getX() + side + middle + 4;
    return { x, r.getY(), index == 0 ? side : index == 1 ? middle : r.getRight() - x, r.getHeight() };
}

void drawLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
{
    g.setColour (c::textSecondary);
    g.setFont (theme::font (theme::type::caption));
    textcache::drawText (g, text, r, juce::Justification::centredLeft, true);
}

// --- round 190jn (design190ia slice 13, CL-16 + DL-107): Live 12.4's Session mixer -----------------------------------
// Measured in live-session.png (strips x 318..1625, y 575..797: an idle set) and live-clipview-audio.png (strips
// x 318..1000, y 470..636: the same set playing). Every number is px at 100 % from the strip's left edge (tracks: the
// 2 px column separator is the strip's last 2 px; Main: its first 2 px) or from the section's top.
namespace lv = theme::livelook;
bool liveLook() noexcept { return lv::active(); }
// a 2 px separator over the first section; each section ends in its own 2 px separator; a 6 px track-colour stripe
// closes the strip (live-session.png y 575..576, 632..633, 790..791; stripe 792..797)
constexpr int liveTop = 2, liveSeparator = 2, liveStripe = 6;
// the Volume section: peak pill, volume field, pan, Track Activator, Solo, Arm left; the meter right (y 634..789)
constexpr int liveVolumeHeight = 156;
// Sends: knobs in a zigzag, one every 17 px, left / right (y 577..631 for two sends: 21 + 17 per send)
constexpr int liveSendPitch = 17, liveSendsBase = 21;
// the meter: 13 x 144 px (two 6 px wells, a 1 px #3E3E3E line between them), its top 6 px under the section's top
constexpr int liveMeterHeight = 144;
// Live's level scale on that meter (ticks at the meter's left, live-session.png x 446..448): dB -> px under the meter's
// top. The fader and the meter share it (the handle points at the meter level it sets).
struct LiveTick
{
    float db, y;
};
constexpr LiveTick liveScale[] = { { 6.0f, 1.0f },     { 0.0f, 15.0f },    { -6.0f, 29.0f },   { -12.0f, 43.0f },
                                   { -18.0f, 57.0f },  { -24.0f, 71.0f },  { -30.0f, 86.0f },  { -36.0f, 98.0f },
                                   { -42.0f, 109.0f }, { -48.0f, 118.0f }, { -54.0f, 127.0f }, { -60.0f, 134.0f } };
constexpr float liveScaleFloor = 140.0f; // -inf (the last tick)
// Round 190kc: Live's Crossfader section under Volume and the Main strip's Pre / Post toggles, captured in Live 12.4
// (shots/ref/live-session-crossfader.png, live-pre-post.png). The section: 21 px + its 2 px separator; per strip A at
// x 11 and B at x 38, 24 x 13 at y 4 with a 1 px #111111 rim, #1E1E1E off, A lit #FFAD56, B lit #03C3D5 (letters #070707
// lit, #B5B5B5 off). The Main's crossfader: a 2 px #111111 line at y 4..5 from x 9 to 7 px before the strip's right, broken
// 3 px at the position, and a 13 x 10 triangle (#5D5D5D, a #111111 rim) from y 6 under it. Pre / Post (live-session.png
// x 1555..1580 y 589..601 and x 1589..1614 y 606..618): 26 x 13, a 1 px #111111 rim, Post #FFAD56 / Pre #03C3D5 with the
// word in #070707, a 1 px #5D5D5D lead line from the strip's left at the toggle's middle row.
constexpr int liveCrossfadeHeight = 21;
const juce::Colour livePostLine { 0xff5d5d5d }, liveSendOffRing { 0xff2a2a2a }, liveBlue { 0xff03c3d5 }, liveThumb { 0xff5d5d5d };
// Live's colours not in theme::livelook: the meter's 0 dB line and the line between its wells, the level ramp
// (live-clipview-audio.png: green #00F758 to -18 dB, hold lines #E4DA00 at -11 dB and #F69B00 at -5 dB; red over 0 dB is
// Live's clip red, not captured lit), ticks (white at 30 %: #757575 on #363636, #7D7D7D on #464646)
const juce::Colour liveZeroLine { 0xff868686 }, liveWellLine { 0xff3e3e3e }, liveGreen { 0xff00f758 }, liveYellow { 0xffe4da00 },
    liveOrange { 0xfff69b00 }, liveRed { 0xffff3636 };
// Captured in Live 12.4 by round 190jn (shots/ref/live-mixer-solo-deactivated.png, the Patience set with Drums soloed
// and Mello deactivated): Solo lit #3C6AB6 with its S in #070707; while any track is soloed every active activator turns
// #757575 with its number #242424; a deactivated one is #1E1E1E inside a 1 px #919191 ring, its number #696969. A hovered
// S keeps its colours (no hover state).
const juce::Colour liveSoloOn { 0xff3c6ab6 }, liveSoloing { 0xff757575 }, liveSoloingInk { 0xff242424 }, liveOffRing { 0xff919191 },
    liveOffInk { 0xff696969 };

float liveMeterY (float db, int height) noexcept
{
    float y = liveScaleFloor;
    if (! std::isfinite (db) || db <= -80.0f)
        y = liveScaleFloor;
    else if (db >= liveScale[0].db)
        y = liveScale[0].y;
    else if (db <= -60.0f)
        y = 134.0f + (-60.0f - db) / 20.0f * (liveScaleFloor - 134.0f);
    else
        for (std::size_t i = 1; i < std::size (liveScale); ++i)
            if (db >= liveScale[i].db)
            {
                const auto& a = liveScale[i - 1];
                const auto& b = liveScale[i];
                y = b.y + (db - b.db) / (a.db - b.db) * (a.y - b.y);
                break;
            }
    return y * (float) height / (float) liveMeterHeight;
}

double liveDbAt (float y, int height) noexcept
{
    y = y * (float) liveMeterHeight / (float) std::max (1, height);
    if (y >= liveScaleFloor)
        return -INFINITY;
    if (y <= liveScale[0].y)
        return liveScale[0].db;
    if (y > 134.0f)
        return -60.0 - (y - 134.0f) / (liveScaleFloor - 134.0f) * 20.0;
    for (std::size_t i = 1; i < std::size (liveScale); ++i)
        if (y <= liveScale[i].y)
        {
            const auto& a = liveScale[i - 1];
            const auto& b = liveScale[i];
            return a.db + (y - a.y) / (b.y - a.y) * (b.db - a.db);
        }
    return -60.0;
}

juce::Colour liveLevelColour (float db) noexcept
{
    struct Stop
    {
        float db;
        juce::Colour colour;
    };
    const Stop stops[] = { { -18.0f, liveGreen }, { -12.0f, liveYellow }, { -5.0f, liveOrange }, { 0.0f, liveRed } };
    if (db <= stops[0].db)
        return stops[0].colour;
    for (std::size_t i = 1; i < std::size (stops); ++i)
        if (db <= stops[i].db)
            return stops[i - 1].colour.interpolatedWith (stops[i].colour, (db - stops[i - 1].db) / (stops[i].db - stops[i - 1].db));
    return liveRed;
}

juce::Font liveFont (float size = theme::type::caption, bool bold = false)
{
    return theme::font (size, bold).withHorizontalScale (1.12f); // (the clip grid's Live text: Live's wider face)
}

// Live's volume field: one decimal, "0" at unity (live-session.png "-4.0", "-6.3", "0"; live-clipview "0.2", "-14.4")
juce::String liveVolumeText (float position)
{
    const auto db = sessionmixer::volumeDb (position);
    if (! std::isfinite (db) || db < -95.0)
        return "-inf";
    if (std::abs (db) < 0.05)
        return "0";
    return juce::String (db, 1);
}

// The peak pill: the loudest peak in three digits ("-12.9", "-8.66", "-1.91", "-94.4"), "-∞" before any sound
juce::String livePeakText (float gain)
{
    if (gain <= 1.0e-6f)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e");
    const auto db = juce::Decibels::gainToDecibels (gain, -150.0f);
    return juce::String (db, std::abs (db) >= 9.995f ? 1 : 2);
}

// a field with Live's 1 px #111111 rim: #242424 (volume, peak) or #1E1E1E (buttons)
void liveWell (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill)
{
    g.setColour (lv::knobRing);
    g.fillRect (r);
    g.setColour (fill);
    g.fillRect (r.reduced (1));
}

// Live's knob (live-session.png x 403..429 y 581..605): an open ring 27 px across, 3 px thick in #111111, open at the
// bottom (from -135° to +135°), and a 3 px needle from the centre to the rim at the value
void drawLiveKnob (juce::Graphics& g, juce::Rectangle<float> box, float value01)
{
    // (ui/LiveLook.h's mixer knob, from a cached tile: drawn straight, each ring was a new Direct2D path geometry)
    theme::drawLiveKnobTile (g, box, value01, 0.0f, 1, &lv::drawMixerKnobDirect);
}
// Round 190nc (DL-110, 190mv B1 live-solo-cue.png / co3.png, Main x 1557..1583 y 754..780): the Cue Volume knob, Live's
// open ring in its cue blue #3C6AB6 with the #181818 needle and a #838383 headphone glyph in the ring's gap
void drawCueKnobDirect (juce::Graphics& g, juce::Rectangle<float> box, float value01, float)
{
    const auto centre = box.getCentre();
    const auto radius = box.getWidth() * 0.5f - 1.5f;
    constexpr auto from = -juce::MathConstants<float>::pi * 0.75f, to = juce::MathConstants<float>::pi * 0.75f;
    juce::Path ring;
    ring.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, from, to, true);
    g.setColour (juce::Colour (0xff3c6ab6));
    g.strokePath (ring, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const auto angle = from + juce::jlimit (0.0f, 1.0f, value01) * (to - from);
    g.setColour (juce::Colour (0xff181818));
    g.drawLine ({ centre, centre.getPointOnCircumference (radius + 1.0f, angle) }, 3.0f);
    // the headphones: a 6 px band over two ear cups, in the gap at the foot of the ring
    const auto cx = centre.x, top = box.getBottom() - 8.0f;
    g.setColour (juce::Colour (0xff838383));
    juce::Path band;
    band.addCentredArc (cx, top + 3.0f, 3.0f, 3.0f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.strokePath (band, juce::PathStrokeType (1.0f));
    g.fillRect (cx - 3.5f, top + 3.0f, 2.0f, 3.0f);
    g.fillRect (cx + 1.5f, top + 3.0f, 2.0f, 3.0f);
}
// Round 190nc: Live's Cue switch glyph (manual 18.6: "Cue switches with headphone icons"; Live's Cue mode itself was
// not captured): headphones in the 23 x 13 Solo button, in its ink
void drawHeadphones (juce::Graphics& g, juce::Rectangle<int> button, juce::Colour ink)
{
    const auto c = button.toFloat().getCentre();
    g.setColour (ink);
    juce::Path band;
    band.addCentredArc (c.x, c.y + 0.5f, 3.5f, 3.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    g.strokePath (band, juce::PathStrokeType (1.0f));
    g.fillRect (c.x - 4.0f, c.y, 2.0f, 3.5f);
    g.fillRect (c.x + 2.0f, c.y, 2.0f, 3.5f);
}
// round 190kc: a return's send knob that is switched off
void drawLiveKnobOff (juce::Graphics& g, juce::Rectangle<float> box, float value01)
{
    theme::drawLiveKnobTile (g, box, value01, 0.0f, 2, &lv::drawMixerKnobOffDirect);
}
} // namespace

SessionMixerComponent::SessionMixerComponent (AudioEngine& e) : engine (e)
{
    setTitle (tr ("Session mixer"));
    setRepaintsOnMouseActivity (false);
    levels.resize ((std::size_t) maxMixerTracks * 2);
    loudest.resize ((std::size_t) maxMixerTracks, 0.0f);
    rms.resize ((std::size_t) maxMixerTracks * 2, 0.0f);
}

int SessionMixerComponent::returnColumnsFor (const Song& song)
{
    if (song.clipGrid && ! song.clipGrid->mixerView.returns)
        return 0;
    return (int) sessionmixer::returnsOf (song).size();
}

void SessionMixerComponent::setColumns (int count, int width, int scrollX, int tracks, int main, int returns, const std::vector<int>& trackOfColumn)
{
    if (count == columns && width == columnWidth && scrollX == scroll && tracks == tracksWidth && main == mainWidth && returns == returnColumns
        && trackOfColumn == columnTracks)
        return;
    if (count != columns || returns != returnColumns || trackOfColumn != columnTracks)
        cacheStamp = ~std::uint64_t (0);
    columnTracks = trackOfColumn;
    returnColumns = returns;
    columns = count;
    columnWidth = width;
    scroll = scrollX;
    tracksWidth = tracks;
    mainWidth = main;
    layoutSections();
    repaint();
}

// The strips are worked out again only when the song was published again or the columns changed (forTrack scans the
// clips, and the grid asks for the mixer's height at every paint).
void SessionMixerComponent::refreshCache() const
{
    const auto stamp = engine.getPublishCount();
    if (stamp == cacheStamp && (int) strips.size() == columns)
        return;
    cacheStamp = stamp;
    const auto& song = engine.getSong();
    strips.clear();
    std::vector<int> inserts;
    for (int t = 0; t < columns; ++t)
    {
        strips.push_back (sessionmixer::stripOf (song, trackOf (t)));
        inserts.push_back (strips.back().insert);
    }
    returnInserts = sessionmixer::returnsOf (song); // (round 190kc)
    allRows = sessionmixer::sendRows (song, inserts, maxSendRows);
}

sessionmixer::Strip SessionMixerComponent::stripFor (int column) const
{
    if (column == mainColumn)
    {
        sessionmixer::Strip main;
        main.insert = 0;
        main.insertCount = 1;
        return main;
    }
    refreshCache();
    if (column >= returnBase) // round 190kc: a return strip
    {
        sessionmixer::Strip r;
        const auto k = column - returnBase;
        r.insert = k >= 0 && k < (int) returnInserts.size() ? returnInserts[(std::size_t) k] : -1;
        r.insertCount = r.insert > 0 ? 1 : 0;
        return r;
    }
    return column >= 0 && column < (int) strips.size() ? strips[(std::size_t) column] : sessionmixer::Strip {};
}

int SessionMixerComponent::columnOf (int track) const noexcept
{
    if (columnTracks.empty())
        return track;
    const auto at = std::find (columnTracks.begin(), columnTracks.end(), track);
    return at != columnTracks.end() ? (int) (at - columnTracks.begin()) : -1;
}

int SessionMixerComponent::insertFor (int column) const
{
    return column == mainColumn ? 0 : stripFor (column).insert;
}

int SessionMixerComponent::stripX (int column) const noexcept
{
    if (column >= returnBase) // round 190kc: the return strips right of the tracks
        return tracksWidth + (column - returnBase) * columnWidth;
    return column == mainColumn ? tracksWidth + returnColumns * columnWidth : column * columnWidth - scroll;
}

namespace
{
// Round 190jn: the sections in Live's look (each with its 2 px separator under it; the top separator and the stripe
// are added by liveFrame), left out from the top as the classic look leaves them out when they do not fit
sessionmixer::Sections liveSections (const SessionMixerView& view, int sendRowCount, int available)
{
    sessionmixer::Sections s;
    if (! view.shown)
        return s;
    available -= liveTop + liveStripe;
    s.volume = view.volume ? liveVolumeHeight + liveSeparator : 0;
    s.crossfade = view.crossfader ? liveCrossfadeHeight + liveSeparator : 0;
    s.trackOptions = view.trackOptions ? sessionmixer::trackOptionsHeight + liveSeparator : 0;
    s.sends = view.sends && sendRowCount > 0 ? liveSendsBase + liveSendPitch * sendRowCount + liveSeparator : 0;
    s.io = view.io ? sessionmixer::ioHeight + liveSeparator : 0;
    for (auto* part : { &s.io, &s.sends, &s.trackOptions, &s.volume, &s.crossfade })
        if (s.total() > std::max (0, available))
            *part = 0;
    return s;
}
int liveFrame (const sessionmixer::Sections& s) noexcept { return s.total() > 0 ? liveTop + liveStripe : 0; }
} // namespace

int SessionMixerComponent::wantedHeight (int available) const
{
    const auto& song = engine.getSong();
    refreshCache();
    const auto view = song.clipGrid ? song.clipGrid->mixerView : SessionMixerView {};
    if (liveLook())
    {
        const auto s = liveSections (view, (int) allRows.size(), available);
        return s.total() + liveFrame (s);
    }
    return sessionmixer::sectionHeights (view, (int) allRows.size(), available).total();
}

void SessionMixerComponent::layoutSections()
{
    const auto& song = engine.getSong();
    refreshCache();
    rows = allRows;
    const auto view = song.clipGrid ? song.clipGrid->mixerView : SessionMixerView {};
    sections = liveLook() ? liveSections (view, (int) rows.size(), getHeight()) : sessionmixer::sectionHeights (view, (int) rows.size(), getHeight());
    if (sections.sends == 0)
        rows.clear();
}

void SessionMixerComponent::resized() { layoutSections(); }

bool SessionMixerComponent::tick()
{
    auto& meterBank = engine.getMeters();
    std::vector<bool> seen ((std::size_t) maxMixerTracks, false), movedInsert ((std::size_t) maxMixerTracks, false);
    auto moved = false;
    // round 190lr (DL-108): Live's meter shows the RMS level under the peak; the sums are asked for only while it shows
    const auto withRms = liveLook() && sections.volume > 0;
    if (withRms)
    {
        meterBank.powerWanted.store (true, std::memory_order_relaxed);
        meterBank.powerReadMs = juce::Time::getMillisecondCounter();
    }
    const auto take = [&] (int insert) {
        if (insert < 0 || insert >= maxMixerTracks || seen[(std::size_t) insert])
            return;
        seen[(std::size_t) insert] = true;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& meter = levels[(std::size_t) insert * 2 + (std::size_t) ch];
            const auto before = meter;
            const auto blockPeak = meterBank.takeSessionPeak (insert, ch);
            meter.tick (blockPeak);
            if (meter.level != before.level || meter.peak != before.peak || meter.clipped != before.clipped)
                moved = true, movedInsert[(std::size_t) insert] = true;
            // round 190jn (DL-107): Live's peak pill holds the loudest peak until it is clicked
            if (auto& top = loudest[(std::size_t) insert]; blockPeak > top)
                top = blockPeak, moved = true, movedInsert[(std::size_t) insert] = true;
        }
        if (withRms)
        {
            // an RMS over ~300 ms (an exponential average of the mean squares, 30 ticks a second), never above the peak bar
            float l = 0.0f, r = 0.0f;
            const auto got = meterBank.takeSessionPower (insert, l, r);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& shown = rms[(std::size_t) insert * 2 + (std::size_t) ch];
                const auto before = shown;
                const auto meanSquare = got ? (ch == 0 ? l : r) : 0.0f;
                auto power = 0.9f * shown * shown + 0.1f * meanSquare;
                shown = juce::jmin (std::sqrt (power), levels[(std::size_t) insert * 2 + (std::size_t) ch].level);
                if (shown < 0.001f)
                    shown = 0.0f;
                if (std::abs (shown - before) > 1.0e-4f)
                    moved = true, movedInsert[(std::size_t) insert] = true;
            }
        }
    };
    if (sections.volume > 0)
    {
        take (0);
        for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
            take (insertFor (t));
        for (int k = 0; k < returnColumns; ++k) // (round 190kc)
            take (insertFor (returnColumn (k)));
        // round 190ge: only the meters that moved are repainted (their fader part: bars, scale and handle), not the
        // whole mixer — it was repainted whole 30 times a second while the song played (most of the message thread)
        if (moved && ui::paintSavings())
        {
            juce::Rectangle<int> dirty;
            const auto add = [&] (int column) {
                if (const auto insert = insertFor (column); insert >= 0 && insert < maxMixerTracks && movedInsert[(std::size_t) insert])
                {
                    dirty = dirty.getUnion (partBounds (column, Part::fader).expanded (1));
                    if (const auto pill = partBounds (column, Part::peak); ! pill.isEmpty()) // (Live's look: the peak pill)
                        dirty = dirty.getUnion (pill);
                }
            };
            add (mainColumn);
            for (int k = 0; k < returnColumns; ++k)
                add (returnColumn (k));
            for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
                add (t);
            if (! dirty.isEmpty())
                repaint (dirty);
        }
    }
    // (the arm buttons follow the grid's arms, which can change without a song edit: the mixer is repainted then)
    if (ui::paintSavings())
    {
        std::uint64_t arms = 1469598103934665603ull;
        for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
            arms = (arms ^ (engine.isGridTrackArmed (trackOf (t)) ? 2u : 1u)) * 1099511628211ull;
        if (arms != armsShownState)
        {
            armsShownState = arms;
            repaint();
        }
    }
    // round 190jn: Live's look fills the grid's selected track's strip #464646; it follows the selection
    if (liveLook())
        if (auto* parent = getParentComponent())
            if (const auto selected = clipGridSelection (*parent).first; selected != selectedShown)
            {
                for (const auto track : { selectedShown, selected })
                    if (const auto t = columnOf (track); t >= 0 && t < columns)
                        repaint (stripX (t), 0, columnWidth, getHeight());
                selectedShown = selected;
            }
    // the song may have changed (a send, a routing): the rows follow
    const auto before = rows;
    const auto sectionsBefore = sections.total();
    layoutSections();
    if (rows != before || sections.total() != sectionsBefore)
        if (auto* parent = getParentComponent())
            parent->resized();
    return moved;
}

void SessionMixerComponent::addViewItems (juce::PopupMenu& menu)
{
    const auto& song = engine.getSong();
    const auto view = song.clipGrid ? song.clipGrid->mixerView : SessionMixerView {};
    const auto set = [this] (bool SessionMixerView::*field, bool value) {
        return [this, field, value] {
            engine.editSong ([field, value] (Song& s) { launcher::ensureGrid (s).mixerView.*field = value; }, false);
            layoutSections();
            if (auto* parent = getParentComponent())
                parent->resized();
        };
    };
    menu.addItem (tr ("Show the mixer"), true, view.shown, set (&SessionMixerView::shown, ! view.shown));
    menu.addSectionHeader (tr ("Mixer controls"));
    menu.addItem (tr ("In/Out"), view.shown, view.io, set (&SessionMixerView::io, ! view.io));
    menu.addItem (tr ("Sends"), view.shown, view.sends, set (&SessionMixerView::sends, ! view.sends));
    menu.addItem (tr ("Volume"), view.shown, view.volume, set (&SessionMixerView::volume, ! view.volume));
    menu.addItem (tr ("Track Options"), view.shown, view.trackOptions, set (&SessionMixerView::trackOptions, ! view.trackOptions));
    // round 190kc: Live's Mixer Controls › Crossfader, then (after a line) Return Tracks [shots/ref/live-view-mixer-controls.png]
    menu.addItem (tr ("Crossfader"), view.shown, view.crossfader, set (&SessionMixerView::crossfader, ! view.crossfader));
    menu.addSeparator();
    menu.addItem (tr ("Return Tracks"), view.shown, view.returns, set (&SessionMixerView::returns, ! view.returns));
    // round 190kc: Live's Create › Insert Return Track (a new return after the others, "C Return" for the third)
    menu.addSeparator();
    menu.addItem (tr ("Insert Return Track"), (int) sessionmixer::returnsOf (song).size() < sessionmixer::maxReturns, false, [this] {
        engine.editSong ([] (Song& s) {
            launcher::ensureGrid (s);
            sessionmixer::insertReturn (s, "Return");
        });
        cacheStamp = ~std::uint64_t (0);
        layoutSections();
        if (auto* parent = getParentComponent())
            parent->resized();
    });
}

// --- geometry ---------------------------------------------------------------------------------------------------------

// Round 190rb (ASan, UI monkey seed 113: heap-buffer-overflow in livePartBounds while painting): the send rows are laid
// out from the song, and the song can lose inserts (another project opened, inserts deleted) a moment before the next
// layout; a row's insert the song no longer has is no return
bool SessionMixerComponent::returnRow (int insert) const
{
    const auto& mixer = engine.getSong().mixer;
    return insert >= 0 && insert < (int) mixer.size() && mixer[(std::size_t) insert].liveReturn;
}

juce::Rectangle<int> SessionMixerComponent::partBounds (int column, Part part, int row) const
{
    if (column != mainColumn && ! isReturn (column) && (column < 0 || column >= columns))
        return {};
    if (liveLook())
        return livePartBounds (column, part, row);
    const auto x0 = stripX (column), w = stripWidth (column);
    const auto ioY = 0, sendsY = sections.io, optionsY = sendsY + sections.sends, volumeY = optionsY + sections.trackOptions;
    const auto crossY = volumeY + sections.volume; // (round 190kc)
    const auto main = column == mainColumn;
    const auto ret = isReturn (column); // (round 190kc: a return strip: no input, no arm)
    const auto strip = stripFor (column);
    const auto full = [&] (int y, int h) { return juce::Rectangle<int> (x0 + pad, y, w - 2 * pad, h); };
    switch (part)
    {
        case Part::inType:
            return sections.io > 0 && ! main && ! ret ? full (ioY + 15, 16) : juce::Rectangle<int>();
        case Part::inChannel:
            return sections.io > 0 && ! main && ! ret && ! strip.notes && strip.insert > 0 ? full (ioY + 33, 16) : juce::Rectangle<int>();
        case Part::monIn:
        case Part::monAuto:
        case Part::monOff:
        {
            if (sections.io == 0 || main || ret || strip.notes || strip.insert <= 0)
                return {};
            return monitorPart (full (ioY + 63, 16), part == Part::monIn ? 0 : part == Part::monAuto ? 1 : 2);
        }
        case Part::output:
            return sections.io > 0 && ! main && strip.insert > 0 ? full (ioY + 93, 16) : juce::Rectangle<int>();
        case Part::send:
            if (sections.sends == 0 || main || strip.insert <= 0 || row < 0 || row >= (int) rows.size())
                return {};
            return { x0 + pad + 1, sendsY + 3 + row * sessionmixer::sendRowHeight, 22, 22 };
        case Part::delay:
            return sections.trackOptions > 0 && ! main && strip.insert > 0 ? juce::Rectangle<int> (x0 + pad, optionsY + 18, w - 2 * pad - 20, 16) : juce::Rectangle<int>();
        case Part::volume:
            return sections.volume > 0 && strip.insert >= 0 ? juce::Rectangle<int> (x0 + pad, volumeY + 9, 44, 16) : juce::Rectangle<int>();
        case Part::pan:
            return sections.volume > 0 && strip.insert >= 0 ? juce::Rectangle<int> (x0 + pad + 8, volumeY + 29, 28, 28) : juce::Rectangle<int>();
        case Part::activator:
            return sections.volume > 0 && ! main && strip.insert > 0 ? juce::Rectangle<int> (x0 + pad + 8, volumeY + 61, 28, 20) : juce::Rectangle<int>();
        case Part::solo:
            return sections.volume > 0 && ! main && strip.insert > 0 ? juce::Rectangle<int> (x0 + pad + 8, volumeY + 85, 28, 14) : juce::Rectangle<int>();
        case Part::arm:
            return sections.volume > 0 && ! main && ! ret ? juce::Rectangle<int> (x0 + pad + 8, volumeY + 102, 28, 14) : juce::Rectangle<int>();
        case Part::fader:
            // the meter and its handle: from the handle's left to the scale
            return sections.volume > 0 && strip.insert >= 0 ? juce::Rectangle<int> (x0 + pad + 48, volumeY + 6, 20, sessionmixer::volumeHeight - 12) : juce::Rectangle<int>();
        case Part::post: // (round 190kc) a return's Pre / Post toggle, in the Main strip beside that return's row
            if (sections.sends == 0 || ! main || row < 0 || row >= (int) rows.size() || ! returnRow (rows[(std::size_t) row]))
                return {};
            return { x0 + pad + 8, sendsY + 7 + row * sessionmixer::sendRowHeight, 34, 14 };
        case Part::xfadeA:
        case Part::xfadeB:
            if (sections.crossfade == 0 || main || strip.insert <= 0)
                return {};
            return { x0 + w / 2 + (part == Part::xfadeA ? -26 : 2), crossY + 5, 24, 14 };
        case Part::crossfader:
            return sections.crossfade > 0 && main ? juce::Rectangle<int> (x0 + 8, crossY + 4, w - 16, 16) : juce::Rectangle<int>();
        case Part::peak: // (Live's look only)
        case Part::cueOut:
        case Part::mainOut:
        case Part::soloCue:
        case Part::cueVolume:
        case Part::none:
            break;
    }
    return {};
}

// Round 190jn: the parts in Live's look (live-session.png, strip 2 x 396..471: the volume section from y 634, Sends
// from y 577; Main x 1540..1625). The meter sits 21 px (Main 22 px) before the strip's right edge; the peak pill and the
// volume field are 35 px wide, 51 px left of the meter; pan, activator, Solo and Arm are centred under them.
juce::Rectangle<int> SessionMixerComponent::livePartBounds (int column, Part part, int row) const
{
    using R = juce::Rectangle<int>;
    const auto x0 = stripX (column), w = stripWidth (column);
    const auto main = column == mainColumn;
    const auto ret = isReturn (column); // (round 190kc)
    const auto strip = stripFor (column);
    const auto ioY = liveTop, sendsY = ioY + sections.io, optionsY = sendsY + sections.sends, volumeY = optionsY + sections.trackOptions;
    const auto crossY = volumeY + sections.volume;
    const auto contentX = main ? x0 + 2 : x0, contentW = w - 2;
    const auto full = [&] (int y, int h) { return R (contentX + 4, y, contentW - 8, h); };
    const auto meterX = x0 + w - (main ? 22 : 21);
    const auto fieldX = meterX - 51, centreX = fieldX + 17;
    const auto volume = sections.volume > 0 && strip.insert >= 0;
    const auto trackButtons = sections.volume > 0 && ! main && strip.insert > 0;
    switch (part)
    {
        case Part::inType:
            return sections.io > 0 && ! main && ! ret ? full (ioY + 15, 16) : R();
        case Part::inChannel:
            return sections.io > 0 && ! main && ! ret && ! strip.notes && strip.insert > 0 ? full (ioY + 33, 16) : R();
        case Part::monIn:
        case Part::monAuto:
        case Part::monOff:
        {
            if (sections.io == 0 || main || ret || strip.notes || strip.insert <= 0)
                return {};
            return monitorPart (full (ioY + 63, 16), part == Part::monIn ? 0 : part == Part::monAuto ? 1 : 2);
        }
        case Part::output:
            return sections.io > 0 && ! main && strip.insert > 0 ? full (ioY + 93, 16) : R();
        case Part::send:
            if (sections.sends == 0 || main || strip.insert <= 0 || row < 0 || row >= (int) rows.size())
                return {};
            // A at x 7, B at x 40, each 17 px under the one before (x 403..429 y 581; x 436..462 y 598)
            return { x0 + (row % 2 == 0 ? 7 : 40), sendsY + 4 + row * liveSendPitch, 27, 27 };
        case Part::delay:
            return sections.trackOptions > 0 && ! main && strip.insert > 0 ? R (contentX + 4, optionsY + 18, contentW - 8 - 20, 16) : R();
        case Part::peak: // y 640..652
            return volume ? R (fieldX, volumeY + 6, 35, 13) : R();
        case Part::volume: // y 658..670
            return volume ? R (fieldX, volumeY + 24, 35, 13) : R();
        case Part::fader: // the handle (x 43..53), the ticks (50..52) and the meter (55..67), y 640..783
            return volume ? R (meterX - 12, volumeY + 6, 25, liveMeterHeight) : R();
        case Part::pan: // the ring y 700..726 (its centre mark from y 694)
            return volume ? R (centreX - 13, volumeY + 66, 27, 27) : R();
        case Part::activator: // 23 x 23 with its 1 px rim, y 727..749
            return trackButtons ? R (centreX - 11, volumeY + 93, 23, 23) : R();
        case Part::solo: // y 754..766
            return trackButtons ? R (centreX - 11, volumeY + 120, 23, 13) : R();
        case Part::arm: // y 771..783 (round 190kc: a return has none, live-session.png x 1390..1463)
            return sections.volume > 0 && ! main && ! ret ? R (centreX - 11, volumeY + 137, 23, 13) : R();
        case Part::post: // (round 190kc) A at x 13 y 12 from the Main strip's (x 1540) and Sends' (y 577) top, B 34 right and 17 down
            if (sections.sends == 0 || ! main || row < 0 || row >= (int) rows.size() || ! returnRow (rows[(std::size_t) row]))
                return {};
            return { x0 + 15 + (row % 2) * 34, sendsY + 12 + row * liveSendPitch, 26, 13 };
        case Part::xfadeA:
        case Part::xfadeB:
            if (sections.crossfade == 0 || main || strip.insert <= 0)
                return {};
            return { x0 + (part == Part::xfadeA ? 11 : 38), crossY + 4, 24, 13 };
        case Part::crossfader: // (the line x 1549..1618 and the triangle to y 785 on a Main strip at x 1540, section y 769)
            return sections.crossfade > 0 && main ? R (x0 + 9, crossY + 4, w - 16, 13) : R();
        // round 190nc (DL-110, 190mv B1 co3.png, Main x 1540): "Main Out" on the tracks' Audio To row, "Cue Out" 31 px
        // above it (box y 581 vs 612); the Solo/Cue switch 25 x 13 at y 739 and the Cue Volume knob 27 px at y 754,
        // centred under the pan (x 1558..1582, 1557..1583)
        case Part::mainOut:
            return sections.io > 0 && main ? full (ioY + 93, 16) : R();
        case Part::cueOut:
            return sections.io > 0 && main ? full (ioY + 62, 16) : R();
        case Part::soloCue:
            return sections.volume > 0 && main ? R (centreX - 11, volumeY + 105, 25, 13) : R();
        case Part::cueVolume:
            return sections.volume > 0 && main ? R (centreX - 13, volumeY + 120, 27, 27) : R();
        case Part::none:
            break;
    }
    return {};
}

SessionMixerComponent::Hit SessionMixerComponent::hitAt (juce::Point<int> p) const
{
    // (round 190kc: the return strips between the tracks and the Main strip)
    const auto returnsRight = tracksWidth + returnColumns * columnWidth;
    const auto column = p.x >= returnsRight ? (p.x < returnsRight + mainWidth ? mainColumn : -1)
                      : p.x >= tracksWidth  ? returnColumn ((p.x - tracksWidth) / std::max (1, columnWidth))
                                            : (p.x + scroll) / std::max (1, columnWidth);
    if (column != mainColumn && ! isReturn (column) && (column < 0 || column >= columns))
        return {};
    for (auto part : { Part::inType, Part::inChannel, Part::monIn, Part::monAuto, Part::monOff, Part::output, Part::delay, Part::volume, Part::pan,
                       Part::activator, Part::solo, Part::arm, Part::fader, Part::peak, Part::xfadeA, Part::xfadeB, Part::crossfader, Part::cueOut,
                       Part::mainOut, Part::soloCue, Part::cueVolume })
        if (partBounds (column, part).contains (p))
            return { column, part, 0 };
    for (int r = 0; r < (int) rows.size(); ++r)
    {
        if (partBounds (column, Part::send, r).expanded (2).contains (p))
            return { column, Part::send, r };
        if (partBounds (column, Part::post, r).contains (p))
            return { column, Part::post, r };
    }
    return { column, Part::none, 0 };
}

// --- values ---------------------------------------------------------------------------------------------------------

void SessionMixerComponent::mapSpots (std::vector<mapmode::Spot>& out)
{
    std::vector<int> all;
    for (int column = -2; column < columns; ++column)
        if (column != -1)
            all.push_back (column);
    for (int k = 0; k < returnColumns; ++k) // (round 190kc: the return strips)
        all.push_back (returnColumn (k));
    for (const auto column : all)
    {
        if (insertFor (column) < 0) // (a track that plays through no insert of its own shows no controls)
            continue;
        auto add = [&] (Part part, int row) {
            const auto area = partBounds (column, part, row);
            if (area.isEmpty())
                return;
            if (const auto target = specFor ({ column, part, row }).target)
                out.push_back ({ area, remotemap::Target::parameter (*target) });
        };
        add (partBounds (column, Part::fader).isEmpty() ? Part::volume : Part::fader, 0);
        add (Part::pan, 0);
        for (int r = 0; r < (int) rows.size(); ++r)
            add (Part::send, r);
        if (column == mainColumn) // (round 190kc: the crossfader, a continuous control, and its three key positions on it)
        {
            add (Part::crossfader, 0);
            if (const auto box = partBounds (column, Part::crossfader); ! box.isEmpty())
            {
                // (Live's map modes split the line: Left at its first 10 px, Right at its last 10 px, Center at the middle)
                const auto field = std::min (10, box.getWidth() / 6);
                out.push_back ({ box.withWidth (field), remotemap::Target::of (RemoteAction::crossfaderLeft) });
                out.push_back ({ box.withSizeKeepingCentre (field, box.getHeight()), remotemap::Target::of (RemoteAction::crossfaderCentre) });
                out.push_back ({ box.withLeft (box.getRight() - field), remotemap::Target::of (RemoteAction::crossfaderRight) });
            }
        }
    }
}

ui::ValueSpec SessionMixerComponent::specFor (const Hit& hit) const
{
    const auto insert = insertFor (hit.column);
    const auto& song = engine.getSong();
    const auto name = hit.column == mainColumn ? tr ("Main") : isReturn (hit.column) ? utf8 (sessionmixer::returnTitle (song, insert)) : insertName (song, insert);
    switch (hit.part)
    {
        case Part::crossfader: // (round 190kc) -1 (A) .. 1 (B), centre 0
        {
            ui::ValueSpec s;
            s.name = tr ("Main") + " - " + tr ("Crossfade");
            s.minimum = -1.0;
            s.maximum = 1.0;
            s.defaultValue = 0.0;
            s.pixelsForRange = 200.0;
            s.toText = [] (double v) { return std::abs (v) < 0.005 ? juce::String ("C") : (v < 0.0 ? "A " : "B ") + juce::String (juce::roundToInt (std::abs (v) * 100.0)); };
            s.target = AutomationTarget { AutomationTargetKind::crossfader, 0, 0, 0 };
            return s;
        }
        case Part::cueVolume: // round 190nc: Live's volume taper (0.85 = 0 dB)
        {
            ui::ValueSpec s;
            s.name = tr ("Main") + " - " + tr ("Cue Volume");
            s.minimum = 0.0;
            s.maximum = 1.0;
            s.defaultValue = 0.85;
            s.pixelsForRange = 200.0;
            s.toText = [] (double v) {
                const auto g = sessionmixer::cueGain ((float) v);
                return g <= 0.0f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (g), 1) + " dB";
            };
            return s;
        }
        case Part::volume:
        case Part::fader:
        {
            auto s = ui::faderSpec (name + " - " + tr ("Volume"), unityFader);
            s.target = AutomationTarget { AutomationTargetKind::mixerVolume, insert, 0, 0 };
            if (hit.part == Part::fader)
                s.pixelsForRange = sessionmixer::volumeHeight - 12;
            return s;
        }
        case Part::pan:
        {
            auto s = ui::panSpec (name + " - " + tr ("Pan"));
            s.target = AutomationTarget { AutomationTargetKind::mixerPan, insert, 0, 0 };
            return s;
        }
        case Part::send:
        {
            const auto to = hit.row < (int) rows.size() ? rows[(std::size_t) hit.row] : 0;
            auto s = ui::faderSpec (name + " - " + tr ("Send to {0}", { insertName (song, to) }), 0.0);
            if (sessionmixer::sendPosition (song, insert, to) >= 0.0f)
                s.target = AutomationTarget { AutomationTargetKind::mixerSend, insert, to, 0 };
            return s;
        }
        case Part::delay:
        {
            ui::ValueSpec s;
            s.name = name + " - " + tr ("Track Delay");
            s.minimum = -(double) maxTrackLatencyMs;
            s.maximum = (double) maxTrackLatencyMs;
            s.defaultValue = 0.0;
            s.interval = 0.01;
            s.pixelsForRange = 20000.0; // (0.5 ms per pixel)
            s.toText = [] (double v) { return juce::String (v, 2) + " ms"; };
            s.fromText = [] (const juce::String& t) -> std::optional<double> {
                if (! t.trim().containsAnyOf ("0123456789"))
                    return std::nullopt;
                return t.trim().getDoubleValue();
            };
            return s;
        }
        default:
            break;
    }
    return {};
}

double SessionMixerComponent::valueFor (const Hit& hit) const
{
    const auto& song = engine.getSong();
    const auto insert = insertFor (hit.column);
    if (insert < 0 || insert >= (int) song.mixer.size())
        return 0.0;
    const auto& m = song.mixer[(std::size_t) insert];
    switch (hit.part)
    {
        case Part::volume:
        case Part::fader: return m.volume;
        case Part::pan: return m.pan;
        case Part::send: return std::max (0.0f, sessionmixer::sendPosition (song, insert, hit.row < (int) rows.size() ? rows[(std::size_t) hit.row] : 0));
        case Part::delay: return sessionmixer::trackDelayMs (song, insert) + 0.0; // (+ 0.0: no "-0.00")
        case Part::crossfader: return song.crossfader; // (round 190kc)
        case Part::cueVolume: return sessionmixer::cueOf (song).volume; // (round 190nc)
        default: break;
    }
    return 0.0;
}

// Round 190lr (DL-109): the inserts of the other tracks selected with this column (Shift / Ctrl+click on the grid's titles)
std::vector<int> SessionMixerComponent::groupedInserts (int column) const
{
    std::vector<int> out;
    auto* parent = getParentComponent();
    if (parent == nullptr || column < 0 || column >= columns)
        return out;
    // (rollup step 10: the grid's selection holds Playlist tracks; round qafix190kq's take lanes have no column)
    const auto tracks = clipGridSelectedTracks (*parent);
    if (tracks.size() < 2 || std::find (tracks.begin(), tracks.end(), trackOf (column)) == tracks.end())
        return out;
    const auto own = insertFor (column);
    for (const auto t : tracks)
        if (const auto c = columnOf (t), i = c >= 0 && c < columns ? insertFor (c) : -1; i >= 0 && i != own && std::find (out.begin(), out.end(), i) == out.end())
            out.push_back (i);
    return out;
}

void SessionMixerComponent::setValue (const Hit& hit, double v)
{
    const auto insert = insertFor (hit.column);
    if (insert < 0)
        return;
    const auto others = groupedInserts (hit.column); // (round 190lr: the selected tracks move by the same amount)
    switch (hit.part)
    {
        case Part::volume:
        case Part::fader:
            engine.editLevels ([insert, others, v = (float) juce::jlimit (0.0, 1.0, v)] (Song& s) {
                if (insert >= (int) s.mixer.size())
                    return;
                const auto delta = v - s.mixer[(std::size_t) insert].volume;
                s.mixer[(std::size_t) insert].volume = v;
                for (const auto i : others)
                    if (i < (int) s.mixer.size())
                        s.mixer[(std::size_t) i].volume = juce::jlimit (0.0f, 1.0f, s.mixer[(std::size_t) i].volume + delta);
            });
            break;
        case Part::pan:
            engine.editLevels ([insert, others, v = (float) juce::jlimit (-1.0, 1.0, v)] (Song& s) {
                if (insert >= (int) s.mixer.size())
                    return;
                const auto delta = v - s.mixer[(std::size_t) insert].pan;
                s.mixer[(std::size_t) insert].pan = v;
                for (const auto i : others)
                    if (i < (int) s.mixer.size())
                        s.mixer[(std::size_t) i].pan = juce::jlimit (-1.0f, 1.0f, s.mixer[(std::size_t) i].pan + delta);
            });
            break;
        case Part::send:
            if (hit.row < (int) rows.size())
                engine.editSong ([insert, to = rows[(std::size_t) hit.row], v = (float) v] (Song& s) { sessionmixer::setSendPosition (s, insert, to, v); });
            break;
        case Part::delay:
            engine.editSong ([insert, v] (Song& s) { sessionmixer::setTrackDelayMs (s, insert, v); });
            break;
        case Part::crossfader: // (round 190kc: a level, as a fader: the snapshot in use hears it at once)
            engine.editLevels ([v = (float) juce::jlimit (-1.0, 1.0, v)] (Song& s) { s.crossfader = v; });
            break;
        case Part::cueVolume: // (round 190nc: the drag's undo point is set when it starts)
            engine.setCueVolume ((float) v, false);
            break;
        default:
            break;
    }
    repaint();
}

// --- painting ---------------------------------------------------------------------------------------------------------

void SessionMixerComponent::paint (juce::Graphics& g)
{
    if (liveLook())
    {
        // round 190jn: Live's strips on its #242424 separators
        g.fillAll (lv::separator);
        cueChannels = engine.outputChannelCount(); // (round 190nc: the device's outputs, once per paint)
        if (sections.total() == 0)
            return;
        auto* parent = getParentComponent();
        const auto selected = parent != nullptr ? clipGridSelection (*parent).first : -1;
        paintGroup = parent != nullptr ? clipGridSelectedTracks (*parent) : std::vector<int> {}; // (round 190lr, DL-109)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (juce::Rectangle<int> (0, 0, tracksWidth, getHeight()));
            const auto veil = deactivate::anyMarked (engine.getSong()); // (round 190ou: nothing looked up while none is)
            for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
            {
                paintStripLive (g, t, selected);
                if (veil)
                    paintDeactivatedVeil (g, t);
            }
        }
        for (int k = 0; k < returnColumns; ++k) // (round 190kc: the return strips)
            paintStripLive (g, returnColumn (k), selected);
        paintStripLive (g, mainColumn, selected);
        return;
    }
    g.fillAll (c::bg2);
    if (sections.total() == 0)
        return;
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, tracksWidth, getHeight()));
        const auto veil = deactivate::anyMarked (engine.getSong()); // (round 190ou: nothing looked up while none is)
        for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
        {
            paintStrip (g, t);
            if (veil)
                paintDeactivatedVeil (g, t);
        }
    }
    for (int k = 0; k < returnColumns; ++k) // (round 190kc: the return strips)
        paintStrip (g, returnColumn (k));
    paintStrip (g, mainColumn);
    // the sections' edges across the whole width, as the reference separates In/Out, Sends and Volume
    g.setColour (c::border);
    for (const auto y : { sections.io, sections.io + sections.sends, sections.io + sections.sends + sections.trackOptions,
                          sections.io + sections.sends + sections.trackOptions + sections.volume })
        if (y > 0 && y < getHeight())
            g.fillRect (0, y, getWidth(), 1);
    g.fillRect (tracksWidth, 0, 1, getHeight());
    if (returnColumns > 0)
        g.fillRect (stripX (mainColumn), 0, 1, getHeight());
}

// Round 190ou: a deactivated track's strip is dimmed as its title in the grid is (round 190op, P4: bg0 at 45 % over it),
// so the whole column reads as unloaded; the Track Activator inside keeps meaning Live's mute
void SessionMixerComponent::paintDeactivatedVeil (juce::Graphics& g, int column) const
{
    const auto& song = engine.getSong();
    const auto track = trackOf (column);
    if (track < 0 || ! trackstates::trackDeactivated (song, trackstates::unitsOfTrack (song, track)))
        return;
    g.setColour (theme::colour::bg0.withAlpha (0.45f));
    g.fillRect (stripX (column), 0, stripWidth (column), getHeight());
}

void SessionMixerComponent::paintStrip (juce::Graphics& g, int column)
{
    const auto& song = engine.getSong();
    const auto strip = stripFor (column);
    const auto main = column == mainColumn;
    const auto ret = isReturn (column); // (round 190kc)
    const auto x0 = stripX (column), w = stripWidth (column);
    const juce::Rectangle<int> area (x0, 0, w, getHeight());
    g.setColour (main || ret ? c::bg3 : c::bg2);
    g.fillRect (area);
    g.setColour (c::border);
    g.fillRect (area.getRight() - 1, 0, 1, getHeight());
    const auto hot = [this, column] (Part p, int row = 0) { return hover == Hit { column, p, row } || dragging == Hit { column, p, row }; };
    const auto* m = strip.insert >= 0 && strip.insert < (int) song.mixer.size() ? &song.mixer[(std::size_t) strip.insert] : nullptr;

    // In/Out [SHOT: Audio From / Ext. In / 1, Monitor In Auto Off, Audio To / Main]
    if (sections.io > 0 && ! main)
    {
        const auto label = [&] (int y, const juce::String& text) { drawLabel (g, { x0 + pad, y, w - 2 * pad, 12 }, text); };
        if (ret)
            ; // (round 190kc: a return takes no input of its own)
        else if (strip.notes)
        {
            // a track that plays an instrument: what it is played from (an armed or selected track takes every MIDI input)
            label (3, tr ("MIDI From"));
            g.setColour (c::textPrimary);
            g.setFont (theme::font (theme::type::caption));
            textcache::drawText (g, tr ("All Ins"), partBounds (column, Part::inType).reduced (4, 0), juce::Justification::centredLeft, true);
        }
        else if (m != nullptr)
        {
            label (3, tr ("Audio From"));
            const auto names = engine.getInputChannelNames();
            drawBox (g, partBounds (column, Part::inType), m->inputChannel >= 0 ? tr ("Ext. In") : tr ("No Input"), true, hot (Part::inType));
            if (m->inputChannel >= 0)
                drawBox (g, partBounds (column, Part::inChannel), utf8 (sessionmixer::inputChannelText (m->inputChannel, m->inputStereo)), true, hot (Part::inChannel),
                         m->inputChannel >= (int) names.size());
            label (51, tr ("Monitor"));
            const auto mode = sessionmixer::monitorOf (m->monitor);
            drawToggle (g, partBounds (column, Part::monIn), tr ("In"), mode == sessionmixer::Monitor::in, c::warning, hot (Part::monIn), true);
            drawToggle (g, partBounds (column, Part::monAuto), tr ("Auto"), mode == sessionmixer::Monitor::autoMode, c::warning, hot (Part::monAuto), true);
            drawToggle (g, partBounds (column, Part::monOff), tr ("Off"), mode == sessionmixer::Monitor::off, c::warning, hot (Part::monOff), true);
        }
        if (m != nullptr)
        {
            label (81, tr ("Audio To"));
            const auto out = sessionmixer::outputOf (song, strip.insert);
            drawBox (g, partBounds (column, Part::output), out == 0 ? tr ("Main") : out < 0 ? tr ("Sends Only") : insertName (song, out), true, hot (Part::output));
        }
    }

    // Sends: a knob per row (its target's name beside it); round 190kc: a return's knob stays dim until it is switched on
    if (sections.sends > 0 && ! main && m != nullptr)
        for (int r = 0; r < (int) rows.size(); ++r)
        {
            const auto knob = partBounds (column, Part::send, r);
            const auto pos = sessionmixer::sendPosition (song, strip.insert, rows[(std::size_t) r]);
            const auto enabled = sessionmixer::sendEnabled (song, strip.insert, rows[(std::size_t) r]);
            theme::drawKnob (g, knob.toFloat(), std::max (0.0f, pos), false, pos > 0.0f ? c::info : c::knobArc.withAlpha (enabled ? 0.5f : 0.2f), enabled && hot (Part::send, r));
            if (isAutomated ({ AutomationTargetKind::mixerSend, strip.insert, rows[(std::size_t) r], 0 })) // round 190lx (DL-59)
                theme::drawLiveKnobAutomationLed (g, knob.toFloat());
            g.setColour (pos >= 0.0f && enabled ? c::textSecondary : c::textDisabled);
            g.setFont (theme::font (theme::type::caption));
            textcache::drawText (g, utf8 (sessionmixer::returnTitle (song, rows[(std::size_t) r])), juce::Rectangle<int> (knob.getRight() + 3, knob.getY(), x0 + w - pad - knob.getRight() - 3, knob.getHeight()),
                        juce::Justification::centredLeft, true);
        }
    // round 190kc: the Main strip's Pre / Post toggles, one per return row
    if (sections.sends > 0 && main)
        for (int r = 0; r < (int) rows.size(); ++r)
            if (const auto box = partBounds (column, Part::post, r); ! box.isEmpty())
                drawToggle (g, box, sessionmixer::isPre (song, rows[(std::size_t) r]) ? tr ("Pre") : tr ("Post"), true, c::warning, hot (Part::post, r));

    // Track Options: Track Delay
    if (sections.trackOptions > 0 && ! main && m != nullptr)
    {
        const auto y = sections.io + sections.sends;
        drawLabel (g, { x0 + pad, y + 4, w - 2 * pad, 12 }, tr ("Track Delay"));
        const auto box = partBounds (column, Part::delay);
        drawBox (g, box, juce::String (sessionmixer::trackDelayMs (song, strip.insert) + 0.0, 2), false, hot (Part::delay));
        g.setColour (c::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        textcache::drawText (g, "ms", juce::Rectangle<int> (box.getRight() + 3, box.getY(), 18, box.getHeight()), juce::Justification::centredLeft, false);
    }

    // Volume: the volume box, pan, Track Activator, Solo, Arm on the left; the meter with its handle and scale on the right
    if (sections.volume > 0)
    {
        const auto top = sections.total() - sections.volume;
        if (m == nullptr && ! main)
        {
            g.setColour (c::textSecondary); // (round 190oj CS-11: was 2.6:1)
            g.setFont (theme::font (theme::type::caption));
            textcache::drawFittedText (g, strip.insertCount > 1 ? tr ("{0} mixer tracks", { juce::String (strip.insertCount) }) : tr ("No mixer track"),
                              juce::Rectangle<int> (x0 + pad, top + 20, w - 2 * pad, 36), juce::Justification::centred, 2);
        }
        if (m != nullptr)
        {
            drawBox (g, partBounds (column, Part::volume), utf8 (sessionmixer::volumeText (m->volume)), false, hot (Part::volume));
            const auto pan = partBounds (column, Part::pan).toFloat();
            theme::drawKnob (g, pan, (m->pan + 1.0f) * 0.5f, true, c::knobArc, hot (Part::pan));
            if (isAutomated ({ AutomationTargetKind::mixerPan, strip.insert, 0, 0 })) // round 190lx (DL-59)
                theme::drawLiveKnobAutomationLed (g, pan);
            if (! main)
            {
                // the Track Activator carries the track's number (a return's: its letter), lit while the insert plays
                const auto act = partBounds (column, Part::activator);
                theme::fillRoundRect (g, act.toFloat(), 2.0f, m->muted ? c::bg4 : c::ledOn);
                g.setColour (m->muted ? c::textSecondary : c::bg0);
                g.setFont (theme::font (theme::type::small, true));
                textcache::drawText (g, ret ? utf8 (sessionmixer::returnLetter (song, strip.insert)) : juce::String (column + 1), act, juce::Justification::centred, false);
                drawToggle (g, partBounds (column, Part::solo), "S", m->solo, c::accent, hot (Part::solo));
            }
            paintMeter (g, strip.insert, partBounds (column, Part::fader));
        }
        if (! main && ! ret)
        {
            const auto arm = partBounds (column, Part::arm).toFloat();
            const auto armed = engine.isGridTrackArmed (trackOf (column));
            theme::fillRoundRect (g, arm, 2.0f, armed ? c::record : hot (Part::arm) ? c::bg5 : c::bg4);
            if (! armed)
            {
                g.setColour (c::borderStrong);
                g.drawRoundedRectangle (arm.reduced (0.5f), 2.0f, 1.0f);
            }
            g.setColour (armed ? c::bg0 : c::textSecondary);
            g.fillEllipse (arm.withSizeKeepingCentre (7.0f, 7.0f));
        }
    }
    paintCrossfadeSection (g, column, false); // (round 190kc)
}

// Round 190kc: Live's Crossfader section (View › Mixer Controls › Crossfader): each strip's Crossfade Assign buttons A and
// B (lit in Live's orange while assigned), the Main strip's crossfader (a line with a triangle at the position).
void SessionMixerComponent::paintCrossfadeSection (juce::Graphics& g, int column, bool live)
{
    if (sections.crossfade == 0)
        return;
    const auto& song = engine.getSong();
    const auto insert = insertFor (column);
    const auto hot = [this, column] (Part p) { return hover == Hit { column, p, 0 } || dragging == Hit { column, p, 0 }; };
    if (column != mainColumn)
    {
        if (insert <= 0 || insert >= (int) song.mixer.size())
            return;
        const auto side = song.mixer[(std::size_t) insert].crossfade;
        for (const auto part : { Part::xfadeA, Part::xfadeB })
        {
            const auto box = partBounds (column, part);
            const auto lit = side == (part == Part::xfadeA ? CrossfadeAssign::a : CrossfadeAssign::b);
            const auto letter = part == Part::xfadeA ? juce::String ("A") : juce::String ("B");
            if (live)
            {
                liveWell (g, box, lit ? (part == Part::xfadeA ? lv::on : liveBlue) : lv::well);
                g.setFont (liveFont());
                g.setColour (lit ? lv::ink : lv::text);
                textcache::drawText (g, letter, box, juce::Justification::centred, false);
            }
            else
                drawToggle (g, box, letter, lit, c::warning, hot (part));
        }
        return;
    }
    const auto box = partBounds (column, Part::crossfader);
    if (box.isEmpty())
        return;
    const auto pos = juce::jlimit (-1.0f, 1.0f, song.crossfader);
    const auto x = live ? (float) box.getX() + (pos + 1.0f) * 0.5f * (float) (box.getWidth() - 1)
                        : (float) box.getX() + 4.0f + (pos + 1.0f) * 0.5f * (float) (box.getWidth() - 8);
    if (live)
    {
        // the line, broken 3 px at the position, and the triangle under it, in whole-pixel rows (no path)
        const auto cx = juce::jlimit (box.getX() + 1, box.getRight() - 2, (int) std::round (x));
        g.setColour (lv::knobRing);
        g.fillRect (box.getX(), box.getY(), cx - 1 - box.getX(), 2);
        g.fillRect (cx + 2, box.getY(), box.getRight() - cx - 2, 2);
        for (int row = 0; row < 10; ++row) // (#111111 rim rows, then the #5D5D5D fill one px inside)
        {
            const auto half = (row + 1) / 2 + (row >= 6 ? 1 : 0);
            g.setColour (lv::knobRing);
            g.fillRect (cx - half - 1, box.getY() + 2 + row, 2 * half + 3, 1);
            if (row >= 1 && row <= 8)
            {
                g.setColour (liveThumb);
                g.fillRect (cx - half, box.getY() + 2 + row, 2 * half + 1, 1);
            }
        }
    }
    else
    {
        theme::fillRoundRect (g, box.toFloat(), 2.0f, hot (Part::crossfader) ? c::bg5 : c::bg4);
        g.setColour (c::textSecondary);
        g.fillRect (box.getX() + 4, box.getCentreY(), box.getWidth() - 8, 1);
        g.setColour (c::textPrimary);
        g.fillRect (juce::Rectangle<float> (x - 2.0f, (float) box.getY() + 2.0f, 4.0f, (float) box.getHeight() - 4.0f));
    }
}

void SessionMixerComponent::paintMeter (juce::Graphics& g, int insert, juce::Rectangle<int> area) const
{
    const auto& song = engine.getSong();
    if (insert < 0 || insert >= (int) song.mixer.size())
        return;
    // [handle 8 px][bars 2 x 4 px][labels]
    const auto handleArea = area.withWidth (8);
    const auto bars = juce::Rectangle<int> (area.getX() + 9, area.getY(), 9, area.getHeight());
    g.setColour (c::bg0);
    g.fillRect (bars);
    const auto yOf = [&bars] (float db) { return (float) bars.getBottom() - meters::scalePosition (db) * (float) bars.getHeight(); };
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& level = levels[(std::size_t) insert * 2 + (std::size_t) ch];
        const auto bar = juce::Rectangle<int> (bars.getX() + ch * 5, bars.getY(), 4, bars.getHeight());
        if (level.level > 0.0f)
        {
            const auto top = juce::roundToInt (yOf (juce::Decibels::gainToDecibels (level.level, -100.0f)));
            const auto lit = bar.withTop (juce::jlimit (bar.getY(), bar.getBottom(), top));
            juce::ColourGradient gradient (c::meterClip, 0.0f, (float) bar.getY(), c::meterLow, 0.0f, (float) bar.getBottom(), false);
            gradient.addColour (1.0 - meters::scalePosition (0.0f), c::meterClip);
            gradient.addColour (1.0 - meters::scalePosition (-6.0f), c::meterMid);
            gradient.addColour (1.0 - meters::scalePosition (-18.0f), c::meterLow);
            g.setGradientFill (gradient);
            g.fillRect (lit);
        }
        if (level.peak > 0.001f)
        {
            g.setColour (level.peak >= 1.0f ? c::meterClip : c::textPrimary.withAlpha (0.8f));
            g.fillRect (bar.getX(), juce::jlimit (bar.getY(), bar.getBottom() - 1, juce::roundToInt (yOf (juce::Decibels::gainToDecibels (level.peak, -100.0f)))), bar.getWidth(), 1);
        }
    }
    // the scale: 0 ... 60 dB below 0, with ticks
    g.setFont (theme::font (9.0f));
    for (const auto db : scaleLabels)
    {
        const auto y = yOf ((float) -db);
        g.setColour (c::textDisabled);
        g.fillRect ((float) bars.getRight() + 1.0f, y, 3.0f, 1.0f);
        g.setColour (c::textSecondary);
        textcache::drawText (g, juce::String (db), juce::Rectangle<float> ((float) bars.getRight() + 5.0f, y - 5.0f, 18.0f, 10.0f), juce::Justification::centredLeft, false);
    }
    // the volume handle: a triangle pointing at the meter at the fader's level
    const auto volumeDb = sessionmixer::volumeDb (song.mixer[(std::size_t) insert].volume);
    const auto hy = yOf (std::isfinite (volumeDb) ? (float) volumeDb : -60.0f);
    juce::Path tri;
    tri.addTriangle ((float) handleArea.getX(), hy - 5.0f, (float) handleArea.getRight(), hy, (float) handleArea.getX(), hy + 5.0f);
    g.setColour (c::textPrimary.withAlpha (0.85f));
    g.fillPath (tri);
}

// Round 190jn (design190ia CL-16): one strip in Live 12.4's look. Live's text is #B5B5B5 on its greys; the fields
// are #242424 with a 1 px #111111 rim; buttons #1E1E1E; the Track Activator #FFAD56 with its number #070707.
void SessionMixerComponent::paintStripLive (juce::Graphics& g, int column, int selected)
{
    const auto& song = engine.getSong();
    const auto strip = stripFor (column);
    const auto main = column == mainColumn;
    const auto ret = isReturn (column); // (round 190kc)
    const auto x0 = stripX (column), w = stripWidth (column);
    const auto content = juce::Rectangle<int> (main ? x0 + 2 : x0, liveTop, w - 2, sections.total());
    const auto hot = [this, column] (Part p, int row = 0) { return hover == Hit { column, p, row } || dragging == Hit { column, p, row }; };
    const auto* m = strip.insert >= 0 && strip.insert < (int) song.mixer.size() ? &song.mixer[(std::size_t) strip.insert] : nullptr;
    const auto cueShown = ! main && sessionmixer::cueActive (song, cueChannels); // (round 190nc)

    // the strip (selected track #464646), each section's 2 px separator under it, the track-colour stripe at the foot
    // (round 190lr's grouped strips; rollup step 10: the grid's selection holds Playlist tracks, a column shows trackOf (column))
    g.setColour (! main && (trackOf (column) == selected
                            || (paintGroup.size() > 1 && std::find (paintGroup.begin(), paintGroup.end(), trackOf (column)) != paintGroup.end()))
                     ? lv::raised : lv::surface);
    g.fillRect (content);
    g.setColour (lv::separator);
    {
        auto y = liveTop;
        for (const auto h : { sections.io, sections.sends, sections.trackOptions, sections.volume, sections.crossfade })
            if (h > 0)
                g.fillRect (content.getX(), (y += h) - liveSeparator, content.getWidth(), liveSeparator);
    }
    const auto* info = ! main && ! ret && trackOf (column) >= 0 && trackOf (column) < (int) song.playlistTracks.size() ? &song.playlistTracks[(std::size_t) trackOf (column)] : nullptr;
    const auto stripeColour = ret ? (m != nullptr && m->colour != 0 ? juce::Colour (m->colour) : lv::raised) // (a return: its own colour)
                                  : info != nullptr && info->colour != 0 ? juce::Colour (info->colour) : lv::raised;
    g.setColour (main ? lv::mainHeader : stripeColour);
    g.fillRect (content.getX(), content.getBottom(), content.getWidth(), liveStripe);

    const auto text = [&g] (const juce::String& t, juce::Rectangle<int> r, juce::Justification j, juce::Colour colour = lv::text) {
        g.setColour (colour);
        textcache::drawText (g, t, r, j, false);
    };
    g.setFont (liveFont());

    // In/Out and Track Options: Live's greys on LYNX's own layout (Live's In/Out section is hidden in the reference set:
    // not captured; its lit monitor state in Live's orange)
    const auto box = [&] (Part part, const juce::String& t, bool arrow, bool dim = false) {
        const auto r = partBounds (column, part);
        liveWell (g, r, hot (part) ? lv::raised : lv::well);
        auto inner = r.reduced (4, 0);
        if (arrow)
        {
            const auto a = inner.removeFromRight (8).toFloat().withSizeKeepingCentre (7.0f, 4.0f);
            lv::fillDownMark (g, a.getCentreX(), a.getY(), 7, lv::text);
        }
        g.setColour (dim ? lv::text.withAlpha (0.5f) : lv::text);
        textcache::drawText (g, t, inner, juce::Justification::centredLeft, true);
    };
    const auto toggle = [&] (Part part, const juce::String& t, bool on) {
        const auto r = partBounds (column, part);
        liveWell (g, r, on ? lv::on : hot (part) ? lv::raised : lv::well);
        g.setColour (on ? lv::ink : lv::text); // (round 190oj: narrows a little rather than losing letters: "Aut")
        textcache::drawFittedText (g, t, r.reduced (1, 0), juce::Justification::centred, 1, 0.75f);
    };
    const auto label = [&] (int y, const juce::String& t) { text (t, { content.getX() + 4, y, content.getWidth() - 8, 12 }, juce::Justification::centredLeft); };
    if (sections.io > 0 && main) // round 190nc (DL-110): Cue Out over Main Out (190mv B1 co3.png)
    {
        const auto y0 = liveTop;
        const auto& cue = sessionmixer::cueOf (song);
        label (y0 + 50, tr ("Cue Out"));
        const auto cueText = cue.out == outputDefault && ! song.mixer.empty() ? sessionmixer::outputBoxText (song.mixer[0].outputChannel, song.mixer[0].outputMode)
                                                                              : sessionmixer::outputBoxText (cue.out, cue.outMode);
        box (Part::cueOut, utf8 (cueText), true);
        label (y0 + 81, tr ("Main Out"));
        box (Part::mainOut, song.mixer.empty() ? juce::String ("1/2") : utf8 (sessionmixer::outputBoxText (song.mixer[0].outputChannel, song.mixer[0].outputMode)), true);
    }
    if (sections.io > 0 && ! main)
    {
        const auto y0 = liveTop;
        if (ret)
            ; // (round 190kc: a return takes no input of its own)
        else if (strip.notes)
        {
            label (y0 + 3, tr ("MIDI From"));
            text (tr ("All Ins"), partBounds (column, Part::inType).reduced (4, 0), juce::Justification::centredLeft);
        }
        else if (m != nullptr)
        {
            label (y0 + 3, tr ("Audio From"));
            const auto names = engine.getInputChannelNames();
            box (Part::inType, m->inputChannel >= 0 ? tr ("Ext. In") : tr ("No Input"), true);
            if (m->inputChannel >= 0)
                box (Part::inChannel, utf8 (sessionmixer::inputChannelText (m->inputChannel, m->inputStereo)), true, m->inputChannel >= (int) names.size());
            label (y0 + 51, tr ("Monitor"));
            const auto mode = sessionmixer::monitorOf (m->monitor);
            toggle (Part::monIn, tr ("In"), mode == sessionmixer::Monitor::in);
            toggle (Part::monAuto, tr ("Auto"), mode == sessionmixer::Monitor::autoMode);
            toggle (Part::monOff, tr ("Off"), mode == sessionmixer::Monitor::off);
        }
        if (m != nullptr)
        {
            label (y0 + 81, tr ("Audio To"));
            const auto out = sessionmixer::outputOf (song, strip.insert);
            box (Part::output, out == 0 ? tr ("Main") : out < 0 ? tr ("Sends Only") : insertName (song, out), true);
        }
    }

    // Sends: "Sends" at the top right (x 435..465 y 583..591), the knobs in a zigzag with their letter under the centre
    if (sections.sends > 0 && ! main && m != nullptr && ! rows.empty())
    {
        const auto sendsY = liveTop + sections.io;
        text (tr ("Sends"), { x0 + 38, sendsY + 5, 36, 10 }, juce::Justification::centredLeft);
        for (int r = 0; r < (int) rows.size(); ++r)
        {
            const auto knob = partBounds (column, Part::send, r);
            const auto pos = sessionmixer::sendPosition (song, strip.insert, rows[(std::size_t) r]);
            // round 190kc: a return's send stays switched off until "Enable Send" (live-session.png: ring #2A2A2A)
            if (sessionmixer::sendEnabled (song, strip.insert, rows[(std::size_t) r]))
                drawLiveKnob (g, knob.toFloat(), std::max (0.0f, pos));
            else
                drawLiveKnobOff (g, knob.toFloat(), std::max (0.0f, pos));
            if (isAutomated ({ AutomationTargetKind::mixerSend, strip.insert, rows[(std::size_t) r], 0 })) // round 190lx (DL-59)
                theme::drawLiveKnobAutomationLed (g, knob.toFloat());
            // (the letter: the row's return, A for the first; Live's -inf send keeps its letter lit)
            const auto letter = sessionmixer::returnLetter (song, rows[(std::size_t) r]);
            text (letter.empty() ? juce::String::charToString ((juce::juce_wchar) ('A' + r % 26)) : utf8 (letter), { knob.getX(), knob.getY() + 21, knob.getWidth(), 8 },
                  juce::Justification::centred, lv::text);
        }
    }
    // round 190kc: the Main strip's Sends section: "Sends" and each return's Pre / Post toggle on a lead line
    if (sections.sends > 0 && main && ! rows.empty())
    {
        const auto sendsY = liveTop + sections.io;
        text (tr ("Sends"), { x0 + 48, sendsY + 5, 36, 10 }, juce::Justification::centredLeft);
        for (int r = 0; r < (int) rows.size(); ++r)
            if (const auto box = partBounds (column, Part::post, r); ! box.isEmpty())
            {
                g.setColour (livePostLine);
                g.fillRect (x0 + 3, box.getCentreY(), box.getX() - 2 - (x0 + 3), 1);
                const auto pre = sessionmixer::isPre (song, rows[(std::size_t) r]);
                liveWell (g, box, pre ? liveBlue : lv::on);
                text (pre ? tr ("Pre") : tr ("Post"), box, juce::Justification::centred, lv::ink);
            }
    }

    if (sections.trackOptions > 0 && ! main && m != nullptr)
    {
        const auto y = liveTop + sections.io + sections.sends;
        label (y + 4, tr ("Track Delay"));
        const auto r = partBounds (column, Part::delay);
        box (Part::delay, juce::String (sessionmixer::trackDelayMs (song, strip.insert) + 0.0, 2), false);
        text ("ms", { r.getRight() + 3, r.getY(), 18, r.getHeight() }, juce::Justification::centredLeft);
    }

    paintCrossfadeSection (g, column, true); // (round 190kc)
    if (sections.volume <= 0)
        return;
    const auto top = liveTop + sections.io + sections.sends + sections.trackOptions;
    if (m == nullptr && ! main)
    {
        g.setColour (lv::text); // (round 190oj CS-11: at 60 % it read 2.56:1 on the strip)
        textcache::drawFittedText (g, strip.insertCount > 1 ? tr ("{0} mixer tracks", { juce::String (strip.insertCount) }) : tr ("No mixer track"),
                                   juce::Rectangle<int> (content.getX() + 4, top + 20, content.getWidth() - 8, 36), juce::Justification::centred, 2);
    }
    if (m != nullptr)
    {
        // the peak pill (#242424 capsule, 1 px #111111 rim; red once a peak went over 0 dB) and the volume field
        const auto pill = partBounds (column, Part::peak).toFloat();
        const auto peakGain = loudestPeak (strip.insert);
        const auto clipped = peakGain >= 1.0f;
        theme::fillRoundRect (g, pill, pill.getHeight() * 0.5f, lv::knobRing);
        theme::fillRoundRect (g, pill.reduced (1.0f), pill.getHeight() * 0.5f - 1.0f, clipped ? liveRed : hot (Part::peak) ? lv::raised : juce::Colour (0xff242424));
        text (livePeakText (peakGain), pill.toNearestInt(), juce::Justification::centred, clipped ? lv::ink : lv::text);
        const auto field = partBounds (column, Part::volume);
        liveWell (g, field, hot (Part::volume) ? lv::raised : juce::Colour (0xff242424));
        text (liveVolumeText (m->volume), field, juce::Justification::centred);

        // pan: the knob with Live's centre mark over it (x 414..420 y 694..698)
        const auto pan = partBounds (column, Part::pan).toFloat();
        lv::fillDownMark (g, pan.getCentreX(), pan.getY() - 6.0f, 7, lv::knobRing);
        drawLiveKnob (g, pan, (m->pan + 1.0f) * 0.5f);
        if (isAutomated ({ AutomationTargetKind::mixerPan, strip.insert, 0, 0 })) // round 190lx (DL-59)
            theme::drawLiveKnobAutomationLed (g, pan);

        if (! main)
        {
            // the Track Activator carries the track's number: #FFAD56 while the track plays (an inactive one: Live's
            // grey button, not captured lit off), Solo and Arm as 23 x 13 buttons with a 1 px #111111 rim
            const auto act = partBounds (column, Part::activator);
            const auto active = ! m->muted;
            const auto& mixerTracks = engine.getSong().mixer;
            const auto soloing = std::any_of (mixerTracks.begin(), mixerTracks.end(), [] (const auto& t) { return t.solo; });
            if (! active)
            {
                g.setColour (liveOffRing);
                g.drawRect (act.expanded (1), 1);
            }
            liveWell (g, act, ! active ? lv::well : soloing ? liveSoloing : lv::on);
            g.setFont (liveFont (theme::type::body, true));
            // (round 190kc: a return's activator carries its letter, live-session.png x 1400..1424 y 727..750)
            text (ret ? utf8 (sessionmixer::returnLetter (song, strip.insert)) : juce::String (column + 1), act, juce::Justification::centred,
                  ! active ? liveOffInk : soloing ? liveSoloingInk : lv::ink);
            g.setFont (liveFont());
            const auto solo = partBounds (column, Part::solo);
            if (cueShown) // round 190nc (DL-110, Live manual 18.6): in Cue mode the Solo switch is a Cue switch with headphones
            {
                liveWell (g, solo, m->cue ? liveSoloOn : lv::well);
                drawHeadphones (g, solo, m->cue ? lv::ink : lv::text);
            }
            else
            {
                liveWell (g, solo, m->solo ? liveSoloOn : lv::well);
                text ("S", solo, juce::Justification::centred, m->solo ? lv::ink : lv::text);
            }
        }
        else // round 190nc (DL-110): the Main strip's Solo/Cue Mode switch and Cue Volume knob (190mv B1)
        {
            const auto sw = partBounds (column, Part::soloCue);
            const auto separate = sessionmixer::cueOutputSeparate (song, cueChannels);
            const auto cueMode = separate && sessionmixer::cueOf (song).cueMode;
            // (captured with one output pair, the switch unavailable: #757575 with its word in #363636; Cue lit in the
            // Solo blue: from the manual, not captured)
            liveWell (g, sw, cueMode ? liveSoloOn : juce::Colour (0xff757575));
            text (cueMode ? tr ("Cue") : tr ("Solo"), sw, juce::Justification::centred, separate ? lv::ink : lv::surface);
            const auto knob = partBounds (column, Part::cueVolume).toFloat();
            theme::drawLiveKnobTile (g, knob, sessionmixer::cueOf (song).volume, 0.0f, 3, &drawCueKnobDirect);
        }
        paintMeterLive (g, strip.insert, partBounds (column, Part::fader));
    }
    if (! main && ! ret)
    {
        // Arm (x 406..428 y 771..783): a 7 px disc; a track of notes has Live's notch in it (live-session.png strips
        // 1..3; the audio track 14 a plain disc); armed: #FF5559 with the glyph #070707
        const auto arm = partBounds (column, Part::arm);
        const auto armed = engine.isGridTrackArmed (trackOf (column));
        const auto fill = armed ? lv::arm : hot (Part::arm) ? lv::raised : lv::well;
        liveWell (g, arm, fill);
        const auto disc = arm.toFloat().withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (armed ? lv::ink : lv::text);
        g.fillEllipse (disc);
        if (strip.notes)
        {
            const auto cx = disc.getCentreX() + 0.5f, cy = disc.getCentreY() + 1.0f;
            g.setColour (fill);
            g.drawLine (cx, disc.getY() + 1.0f, cx, cy, 1.0f);
            g.drawLine (cx, cy, cx - 2.0f, cy + 1.5f, 1.0f);
        }
    }
}

// Live's meter (live-clipview-audio.png x 527..539 y 479..622): a 13 x 144 #181818 well with rounded ends and a 1 px
// #3E3E3E line between the channels; each bar fills its 5 px from the bottom in Live's level colours; a 1 px hold line;
// the 0 dB line #868686; ticks at Live's scale on the left; the volume handle (#B5B5B5, 11 x 13) points at the level
void SessionMixerComponent::paintMeterLive (juce::Graphics& g, int insert, juce::Rectangle<int> area) const
{
    const auto& song = engine.getSong();
    if (insert < 0 || insert >= (int) song.mixer.size())
        return;
    const auto meter = juce::Rectangle<int> (area.getRight() - 13, area.getY(), 13, area.getHeight());
    const auto yAt = [&meter] (float db) { return meter.getY() + juce::roundToInt (liveMeterY (db, meter.getHeight())); };
    theme::fillRoundRect (g, meter.toFloat(), 1.5f, lv::meterWell);
    g.setColour (liveWellLine);
    g.fillRect (meter.getX() + 6, meter.getY() + 1, 1, meter.getHeight() - 2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& level = levels[(std::size_t) insert * 2 + (std::size_t) ch];
        const auto bar = juce::Rectangle<int> (meter.getX() + (ch == 0 ? 1 : 7), meter.getY() + 1, 5, meter.getHeight() - 2);
        if (level.level > 0.0f)
        {
            const auto db = juce::Decibels::gainToDecibels (level.level, -100.0f);
            const auto lit = bar.withTop (juce::jlimit (bar.getY(), bar.getBottom(), yAt (db)));
            // (one fill per colour band of Live's ramp: green below -18 dB, then towards yellow, orange and red)
            // round 190lr (DL-108, live-meter-peak-rms.png): the RMS part in the ramp's colour (#00F758 green), the peak
            // part above it darker (#08AB42); with no RMS reading yet the whole bar keeps the ramp's colour
            const auto rmsLevel = rms[(std::size_t) insert * 2 + (std::size_t) ch];
            const auto rmsTop = rmsLevel > 0.0f ? juce::jlimit (lit.getY(), lit.getBottom(), yAt (juce::Decibels::gainToDecibels (rmsLevel, -100.0f)))
                                                : lit.getY();
            const auto shade = [&] (juce::Colour c, int y) { return y < rmsTop ? c.withMultipliedBrightness (0.69f) : c; };
            const auto bandY = [&] (float d) { return juce::jlimit (lit.getY(), lit.getBottom(), yAt (d)); };
            const auto green = lit.withTop (bandY (-18.0f));
            g.setColour (liveGreen);
            g.fillRect (green.withTop (juce::jmax (green.getY(), rmsTop)));
            if (rmsTop > green.getY())
            {
                g.setColour (liveGreen.withMultipliedBrightness (0.69f));
                g.fillRect (green.withBottom (juce::jmin (green.getBottom(), rmsTop)));
            }
            for (auto y = lit.getY(); y < bandY (-18.0f); ++y)
            {
                g.setColour (shade (liveLevelColour ((float) liveDbAt ((float) (y - meter.getY()), meter.getHeight())), y));
                g.fillRect (lit.getX(), y, lit.getWidth(), 1);
            }
        }
        if (level.peak > 0.001f)
        {
            const auto db = juce::Decibels::gainToDecibels (level.peak, -100.0f);
            g.setColour (level.peak >= 1.0f ? liveRed : liveLevelColour (db));
            g.fillRect (bar.getX(), juce::jlimit (bar.getY(), bar.getBottom() - 1, yAt (db)), bar.getWidth(), 1);
        }
    }
    g.setColour (liveZeroLine);
    g.fillRect (meter.getX() + 1, yAt (0.0f), 5, 1);
    g.fillRect (meter.getX() + 7, yAt (0.0f), 5, 1);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    for (const auto& tick : liveScale)
        g.fillRect (meter.getX() - 5, yAt (tick.db), 3, 1);
    g.fillRect (meter.getX() - 5, meter.getY() + juce::roundToInt (liveScaleFloor * (float) meter.getHeight() / (float) liveMeterHeight), 3, 1);
    const auto volumeDb = sessionmixer::volumeDb (song.mixer[(std::size_t) insert].volume);
    const auto hy = (float) meter.getY() + liveMeterY (std::isfinite (volumeDb) ? (float) volumeDb : -INFINITY, meter.getHeight());
    theme::fillLaunchTriangle (g, { (float) meter.getX() - 12.0f, hy - 6.5f, 11.0f, 13.0f }, lv::text); // (a cached tile)
    // round 190lr (DL-59, Live manual 25.1 "a little LED has appeared in the slider thumb to indicate that the control is
    // now automated"; capture live-automated-led.png): a 4 x 4 #009AAC dot at the volume slider's tip
    const auto& automated = engine.getAutomationDisplay().automatedTargets();
    if (std::find (automated.begin(), automated.end(), AutomationTarget { AutomationTargetKind::mixerVolume, insert }) != automated.end())
    {
        const juce::Point<float> tip { (float) meter.getX() - 2.5f, hy };
        g.setColour (lv::meterWell); // (its dark ring)
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (tip));
        g.setColour (juce::Colour (0xff009aac));
        g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (tip));
    }
}

bool SessionMixerComponent::isAutomated (const AutomationTarget& t) const
{
    const auto& automated = engine.getAutomationDisplay().automatedTargets();
    return std::find (automated.begin(), automated.end(), t) != automated.end();
}

void SessionMixerComponent::setMeterForSnapshot (int insert, float left, float right, float holdLeft, float holdRight, float maxPeak)
{
    if (insert < 0 || insert >= maxMixerTracks)
        return;
    auto& l = levels[(std::size_t) insert * 2];
    auto& r = levels[(std::size_t) insert * 2 + 1];
    l.level = left, r.level = right, l.peak = holdLeft, r.peak = holdRight;
    rms[(std::size_t) insert * 2] = left * 0.5f, rms[(std::size_t) insert * 2 + 1] = right * 0.5f; // (round 190lr: −6 dB under the peak)
    l.clipped = holdLeft >= 1.0f, r.clipped = holdRight >= 1.0f;
    loudest[(std::size_t) insert] = maxPeak;
    repaint();
}

// --- mouse ------------------------------------------------------------------------------------------------------------

void SessionMixerComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto hit = hitAt (e.getPosition());
    dragging = {};
    if (hit.part == Part::none)
        return;
    const auto& song = engine.getSong();
    const auto insert = insertFor (hit.column);
    const auto popup = e.mods.isPopupMenu();
    switch (hit.part)
    {
        case Part::volume:
        case Part::fader:
        case Part::pan:
        case Part::send:
        case Part::delay:
        case Part::crossfader:
        case Part::cueVolume:
        {
            if (popup && hit.part == Part::send && isReturn (hit.column)) // (round 190kc: Enable Send / Enable All Sends)
            {
                showSendMenu (hit);
                return;
            }
            if (popup && hit.part == Part::crossfader) // (round 190kc: the seven curves)
            {
                showCrossfaderMenu();
                return;
            }
            if (hit.part == Part::send && hit.row < (int) rows.size() && ! sessionmixer::sendEnabled (song, insert, rows[(std::size_t) hit.row]))
                return; // (a return's send that is switched off does not move)
            const auto spec = specFor (hit);
            const auto value = valueFor (hit);
            if (popup)
            {
                ui::showValueMenu (*this, &engine, spec, value, [this, hit] (double v) { setValue (hit, v); });
                return;
            }
            if (ui::isResetClick (e))
            {
                setValue (hit, spec.defaultValue);
                ui::flashValueTip (*this, partBounds (hit.column, hit.part, hit.row), spec.text (spec.defaultValue));
                return;
            }
            if (hit.part == Part::cueVolume) // (round 190nc: one undo step for the whole drag)
                engine.setCueVolume ((float) value, true);
            dragging = hit;
            if (hit.part == Part::fader || hit.part == Part::crossfader)
                mouseDrag (e); // (the handle jumps to the click)
            else
                drag.begin (e, value);
            ui::showValueTip (*this, partBounds (hit.column, hit.part, hit.row), spec.text (valueFor (hit)));
            return;
        }
        case Part::peak: // round 190jn (DL-107): a click resets the peak pill and the meter's clip state
            if (insert >= 0 && insert < maxMixerTracks)
            {
                loudest[(std::size_t) insert] = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    levels[(std::size_t) insert * 2 + (std::size_t) ch].clipped = false;
            }
            break;
        case Part::activator:
            if (! popup)
                engine.editSong ([insert, others = groupedInserts (hit.column), on = ! sessionmixer::isActive (song, insert)] (Song& s) {
                    sessionmixer::setActive (s, insert, on);
                    for (const auto i : others) // (round 190lr, DL-109: every selected track)
                        sessionmixer::setActive (s, i, on);
                });
            break;
        case Part::solo:
            if (! popup && cueInEffect()) // round 190nc (DL-110): Live's Cue switch in Cue mode
                engine.editSong ([insert, exclusive = ! e.mods.isCommandDown()] (Song& s) { sessionmixer::cue (s, insert, exclusive); });
            else if (! popup)
                engine.editSong ([insert, exclusive = ! e.mods.isCommandDown()] (Song& s) { sessionmixer::solo (s, insert, exclusive); });
            break;
        case Part::soloCue: // round 190nc: the switch moves only while the Cue Out is separate (190mv B1: else it stays Solo)
            if (! popup && sessionmixer::cueOutputSeparate (song, engine.outputChannelCount()))
                engine.setCueMode (! sessionmixer::cueOf (song).cueMode);
            break;
        case Part::cueOut:
            showDeviceOutputMenu (true);
            break;
        case Part::mainOut:
            showDeviceOutputMenu (false);
            break;
        case Part::arm:
            if (! popup)
                engine.setGridTrackArmed (trackOf (hit.column), ! engine.isGridTrackArmed (trackOf (hit.column)), ! e.mods.isCommandDown());
            break;
        case Part::post: // round 190kc: a return's Pre / Post toggle
            if (! popup && hit.row < (int) rows.size())
                engine.editSong ([to = rows[(std::size_t) hit.row]] (Song& s) { sessionmixer::setPre (s, to, ! sessionmixer::isPre (s, to)); });
            break;
        case Part::xfadeA:
        case Part::xfadeB: // round 190kc: Crossfade Assign (a lit button again: no assignment)
            if (! popup)
            {
                const auto side = hit.part == Part::xfadeA ? CrossfadeAssign::a : CrossfadeAssign::b;
                engine.editSong ([insert, side] (Song& s) {
                    if (insert > 0 && insert < (int) s.mixer.size())
                    {
                        auto& m = s.mixer[(std::size_t) insert];
                        m.crossfade = m.crossfade == side ? CrossfadeAssign::none : side;
                    }
                });
            }
            break;
        case Part::monIn:
        case Part::monAuto:
        case Part::monOff:
            engine.setInsertMonitor (insert, sessionmixer::inputMonitorOf (hit.part == Part::monIn ? sessionmixer::Monitor::in
                                                                          : hit.part == Part::monOff ? sessionmixer::Monitor::off
                                                                                                     : sessionmixer::Monitor::autoMode));
            break;
        case Part::inType:
            if (! stripFor (hit.column).notes)
                showInputMenu (insert, false);
            break;
        case Part::inChannel:
            showInputMenu (insert, true);
            break;
        case Part::output:
            showOutputMenu (insert);
            break;
        case Part::none:
            break;
    }
    repaint();
}

void SessionMixerComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging.part == Part::none)
        return;
    const auto spec = specFor (dragging);
    double v = 0.0;
    if (dragging.part == Part::crossfader) // (round 190kc: the triangle follows the pointer along the line)
    {
        const auto area = partBounds (dragging.column, Part::crossfader);
        const auto inset = liveLook() ? 0 : 4;
        v = juce::jlimit (-1.0, 1.0, (double) (e.x - area.getX() - inset) / (double) std::max (1, area.getWidth() - 1 - 2 * inset) * 2.0 - 1.0);
    }
    else if (dragging.part == Part::fader)
    {
        // the handle follows the pointer on the meter's dB scale
        const auto area = partBounds (dragging.column, Part::fader);
        if (liveLook()) // (round 190jn: on Live's scale, the meter's own)
            v = sessionmixer::positionForDb (liveDbAt ((float) (e.y - area.getY()), area.getHeight()));
        else
        {
            const auto pos = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - e.y) / (float) std::max (1, area.getHeight()));
            const auto db = pos <= 0.0f ? -INFINITY : -60.0 + 66.0 * pos;
            v = sessionmixer::positionForDb (db);
        }
    }
    else
        v = drag.update (e, spec);
    setValue (dragging, v);
    ui::reportTweak (spec);
    ui::showValueTip (*this, partBounds (dragging.column, dragging.part, dragging.row), spec.text (valueFor (dragging)));
}

void SessionMixerComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragging.part != Part::none)
    {
        drag.end();
        ui::hideValueTip();
    }
    dragging = {};
    repaint();
}

void SessionMixerComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitAt (e.getPosition());
    switch (hit.part)
    {
        case Part::fader: // the reference: a double-click on the meter resets the volume [Info View text, session1.png]
        case Part::pan:
        case Part::send:
        case Part::crossfader: // (round 190kc: back to the centre)
        case Part::cueVolume:  // (round 190nc: 0 dB)
            setValue (hit, specFor (hit).defaultValue);
            break;
        case Part::volume:
        case Part::delay:
            ui::typeValue (*this, specFor (hit), valueFor (hit), [this, hit] (double v) { setValue (hit, v); });
            break;
        default:
            break;
    }
}

void SessionMixerComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto hit = hitAt (e.getPosition());
    if (hit.part == Part::volume || hit.part == Part::fader || hit.part == Part::pan || hit.part == Part::send || hit.part == Part::delay
        || hit.part == Part::crossfader || hit.part == Part::cueVolume)
    {
        const auto spec = specFor (hit);
        if (const auto v = ui::wheelValue (valueFor (hit), e, wheel, spec))
        {
            setValue (hit, *v);
            ui::reportTweak (spec);
            ui::flashValueTip (*this, partBounds (hit.column, hit.part, hit.row), spec.text (valueFor (hit)));
            return;
        }
    }
    if (auto* parent = getParentComponent()) // (elsewhere the grid scrolls)
        parent->mouseWheelMove (e.getEventRelativeTo (parent), wheel);
}

void SessionMixerComponent::mouseMove (const juce::MouseEvent& e)
{
    const auto hit = hitAt (e.getPosition());
    if (! (hit == hover))
    {
        hover = hit;
        repaint();
    }
}

void SessionMixerComponent::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

juce::String SessionMixerComponent::getTooltip() { return tipFor (hover); }

juce::String SessionMixerComponent::tipFor (const Hit& hit) const
{
    switch (hit.part)
    {
        case Part::inType: return tr ("Audio From: the track's audio input (an insert takes one device input)");
        case Part::inChannel: return tr ("The input channel: one channel or a stereo pair");
        case Part::monIn: return tr ("Monitor In: always hear the input");
        case Part::monAuto: return tr ("Monitor Auto: hear the input while the track is armed, not while it plays back");
        case Part::monOff: return tr ("Monitor Off: never hear the input");
        case Part::output: return tr ("Audio To: where the track's mixer track goes (Main, or another mixer track)");
        case Part::send:
            if (liveLook() && hit.row >= 0 && hit.row < (int) rows.size()) // (round 190jn: Live's knob shows only its letter)
                return tr ("Send {0} to {1}. Turning it up from zero makes the send",
                           { juce::String::charToString ((juce::juce_wchar) ('A' + hit.row % 26)), insertName (engine.getSong(), rows[(std::size_t) hit.row]) });
            return tr ("Send level. Turning it up from zero makes the send");
        case Part::delay: return tr ("Track Delay in milliseconds: positive plays the track later, negative earlier");
        case Part::volume: return tr ("Volume in dB: drag up or down, double-click to type");
        case Part::fader: return tr ("Drag the handle to set the volume; double-click the meter for 0 dB");
        case Part::pan: return tr ("Pan");
        case Part::activator: return tr ("Track Activator: switches the track's mixer track on or off");
        case Part::solo:
            return cueInEffect() ? tr ("Cue: hear the track through the Cue Out (Ctrl-click: cue several tracks)")
                                 : tr ("Solo (Ctrl-click: solo several tracks)");
        case Part::arm: return tr ("Arm Recording (Ctrl-click: arm several tracks)");
        case Part::peak: return tr ("Peak level: the loudest peak since the last reset. Click to reset it");
        // round 190kc
        case Part::post: return tr ("Pre / Post: whether the tracks' sends to this return are taken before or after their mixer stage");
        case Part::xfadeA: return tr ("Crossfade Assign A: the crossfader fades this track out as it moves to B");
        case Part::xfadeB: return tr ("Crossfade Assign B: the crossfader fades this track out as it moves to A");
        case Part::crossfader: return tr ("Crossfader: fades between the tracks assigned to A and B. Right-click for its curve");
        case Part::cueOut: return tr ("Cue Out: the outputs that play the cued tracks and the Browser's previews in Cue mode (an output other than Main Out)");
        case Part::mainOut: return tr ("Main Out: the outputs the Main track plays to");
        case Part::soloCue:
            return sessionmixer::cueOutputSeparate (engine.getSong(), engine.outputChannelCount())
                       ? tr ("Solo/Cue Mode: Cue turns the tracks' Solo switches into Cue switches that send them to the Cue Out")
                       : tr ("Solo/Cue Mode: set the Cue Out to an output other than Main Out to use Cue mode");
        case Part::cueVolume: return tr ("Cue Volume: the level of the Cue Out");
        case Part::none: break;
    }
    return {};
}

// --- menus ------------------------------------------------------------------------------------------------------------

void SessionMixerComponent::showInputMenu (int insert, bool channels)
{
    const auto& song = engine.getSong();
    if (insert <= 0 || insert >= (int) song.mixer.size())
        return;
    const auto& m = song.mixer[(std::size_t) insert];
    const auto names = engine.getInputChannelNames();
    const auto count = (int) names.size();
    juce::PopupMenu menu;
    const auto set = [this, insert] (int channel, bool stereo) { return [this, insert, channel, stereo] { engine.setInsertInput (insert, channel, stereo); repaint(); }; };
    if (! channels)
    {
        menu.addItem (tr ("No Input"), true, m.inputChannel < 0, set (-1, true));
        menu.addItem (tr ("Ext. In"), count > 0, m.inputChannel >= 0, set (m.inputChannel >= 0 ? m.inputChannel : 0, count > 1));
        if (count == 0)
            menu.addItem (tr ("No audio input device is open"), false, false, [] {});
    }
    else
    {
        for (int ch = 0; ch < count; ++ch)
            menu.addItem (utf8 (sessionmixer::inputChannelText (ch, false)) + "   " + utf8 (names[(std::size_t) ch]), true, m.inputChannel == ch && ! m.inputStereo, set (ch, false));
        if (count > 1)
        {
            menu.addSeparator();
            for (int ch = 0; ch + 1 < count; ch += 2)
                menu.addItem (utf8 (sessionmixer::inputChannelText (ch, true)), true, m.inputChannel == ch && m.inputStereo, set (ch, true));
        }
        if (count == 0)
            menu.addItem (tr ("No audio input device is open"), false, false, [] {});
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// Round 190kc: a return's send knob [Live manual 18.4: "right-click on a Return track's Send knob and select Enable Send or
// Enable All Sends"]; a switched-on one can be switched off again; the usual value items follow
void SessionMixerComponent::showSendMenu (const Hit& hit)
{
    auto menu = sendMenu (hit);
    if (menu.getNumItems() > 0)
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu SessionMixerComponent::sendMenu (const Hit& hit)
{
    juce::PopupMenu menu;
    const auto& song = engine.getSong();
    const auto from = insertFor (hit.column);
    if (from <= 0 || hit.row < 0 || hit.row >= (int) rows.size())
        return menu;
    const auto to = rows[(std::size_t) hit.row];
    const auto on = sessionmixer::sendEnabled (song, from, to);
    menu.addItem (on ? tr ("Disable Send") : tr ("Enable Send"), from != to, false, [this, from, to, on] {
        engine.editSong ([=] (Song& s) { sessionmixer::setSendEnabled (s, from, to, ! on); });
        repaint();
    });
    // (Live 12.4: "Enable Send / Enable All Sends" on a send that is off, "Disable Send / Disable All Sends" on one that is
    // on, shots/ref/live-return-send-menu-off.png, -on.png)
    menu.addItem (on ? tr ("Disable All Sends") : tr ("Enable All Sends"), true, false, [this, from, on] {
        engine.editSong ([from, on] (Song& s) {
            for (const auto r : sessionmixer::returnsOf (s))
                sessionmixer::setSendEnabled (s, from, r, ! on);
        });
        repaint();
    });
    return menu;
}

// Round 190kc: the crossfader's context menu, Live's seven curves in Live's order (its manual's CrossfaderCurvesContextMenu)
void SessionMixerComponent::showCrossfaderMenu()
{
    crossfaderMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::PopupMenu SessionMixerComponent::crossfaderMenu()
{
    const auto now = engine.getSong().crossfadeCurve;
    juce::PopupMenu menu;
    for (int k = 0; k < crossfader::curveCount; ++k)
        menu.addItem (tr (crossfader::curveName (k)), true, now == k, [this, k] {
            engine.editSong ([k] (Song& s) { s.crossfadeCurve = k; });
            repaint();
        });
    return menu;
}

bool SessionMixerComponent::cueInEffect() const
{
    return sessionmixer::cueActive (engine.getSong(), engine.outputChannelCount());
}

// Round 190nc (DL-110): Live's Cue Out / Main Out choosers list the device's stereo pairs, then its single outputs, in
// Live's own numbering (190mv B1: "1/2, 1, 2, Configure..."; LYNX has no Configure item: the outputs come from the device)
juce::PopupMenu SessionMixerComponent::deviceOutputMenu (bool cue)
{
    const auto& song = engine.getSong();
    const auto& c = sessionmixer::cueOf (song);
    auto now = cue ? c.out : (song.mixer.empty() ? outputDefault : song.mixer[0].outputChannel);
    auto nowMode = cue ? c.outMode : (song.mixer.empty() ? OutputMode::stereo : song.mixer[0].outputMode);
    if (now == outputDefault)
    {
        const auto [first, count] = cue ? sessionmixer::mainOutChannels (song) : std::pair<int, int> { 0, 2 };
        now = first;
        nowMode = count == 2 ? OutputMode::stereo : nowMode;
    }
    juce::PopupMenu menu;
    auto names = engine.getOutputChannelNames();
    for (auto k = (int) names.size(); k < engine.outputChannelCount(); ++k) // (a device given for a check)
        names.push_back ("Out " + std::to_string (k + 1));
    for (const auto& t : outputTargets (names))
    {
        const auto mode = t.mode;
        menu.addItem (utf8 (sessionmixer::outputBoxText (t.channel, mode)), true, t.channel == now && (mode == OutputMode::stereo) == (nowMode == OutputMode::stereo),
                      [this, cue, ch = t.channel, mode] {
                          if (cue)
                              engine.setCueOutput (ch, mode);
                          else
                              engine.setTrackOutput (0, ch, mode);
                          repaint();
                      });
    }
    if (menu.getNumItems() == 0)
        menu.addItem (tr ("No audio device"), false, false, nullptr);
    return menu;
}

void SessionMixerComponent::showDeviceOutputMenu (bool cue)
{
    deviceOutputMenu (cue).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void SessionMixerComponent::showOutputMenu (int insert)
{
    const auto& song = engine.getSong();
    if (insert <= 0)
        return;
    const auto now = sessionmixer::outputOf (song, insert);
    juce::PopupMenu menu;
    for (const auto to : sessionmixer::outputChoices (song, insert))
        menu.addItem (to == 0 ? tr ("Main") : insertName (song, to), true, now == to, [this, insert, to] {
            engine.editSong ([insert, to] (Song& s) { sessionmixer::setOutput (s, insert, to); });
            repaint();
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
} // namespace lynx
