#include "ui/SessionMixer.h"
#include "ui/PaintAB.h"

#include <algorithm>
#include <cmath>

#include "core/ClipLauncher.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

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
    g.drawText (text, inner, juce::Justification::centredLeft, true);
}

void drawToggle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text, bool on, juce::Colour onColour, bool hot)
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
    g.drawText (text, r, juce::Justification::centred, false);
}

void drawLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& text)
{
    g.setColour (c::textSecondary);
    g.setFont (theme::font (theme::type::caption));
    g.drawText (text, r, juce::Justification::centredLeft, true);
}
} // namespace

SessionMixerComponent::SessionMixerComponent (AudioEngine& e) : engine (e)
{
    setTitle (tr ("Session mixer"));
    setRepaintsOnMouseActivity (false);
    levels.resize ((std::size_t) maxMixerTracks * 2);
}

void SessionMixerComponent::setColumns (int count, int width, int scrollX, int tracks, int main)
{
    if (count == columns && width == columnWidth && scrollX == scroll && tracks == tracksWidth && main == mainWidth)
        return;
    if (count != columns)
        cacheStamp = ~std::uint64_t (0);
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
        strips.push_back (sessionmixer::stripOf (song, t));
        inserts.push_back (strips.back().insert);
    }
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
    return column >= 0 && column < (int) strips.size() ? strips[(std::size_t) column] : sessionmixer::Strip {};
}

int SessionMixerComponent::insertFor (int column) const
{
    return column == mainColumn ? 0 : stripFor (column).insert;
}

int SessionMixerComponent::stripX (int column) const noexcept
{
    return column == mainColumn ? tracksWidth : column * columnWidth - scroll;
}

int SessionMixerComponent::wantedHeight (int available) const
{
    const auto& song = engine.getSong();
    refreshCache();
    return sessionmixer::sectionHeights (song.clipGrid ? song.clipGrid->mixerView : SessionMixerView {}, (int) allRows.size(), available).total();
}

void SessionMixerComponent::layoutSections()
{
    const auto& song = engine.getSong();
    refreshCache();
    rows = allRows;
    sections = sessionmixer::sectionHeights (song.clipGrid ? song.clipGrid->mixerView : SessionMixerView {}, (int) rows.size(), getHeight());
    if (sections.sends == 0)
        rows.clear();
}

void SessionMixerComponent::resized() { layoutSections(); }

bool SessionMixerComponent::tick()
{
    auto& meterBank = engine.getMeters();
    std::vector<bool> seen ((std::size_t) maxMixerTracks, false), movedInsert ((std::size_t) maxMixerTracks, false);
    auto moved = false;
    const auto take = [&] (int insert) {
        if (insert < 0 || insert >= maxMixerTracks || seen[(std::size_t) insert])
            return;
        seen[(std::size_t) insert] = true;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& meter = levels[(std::size_t) insert * 2 + (std::size_t) ch];
            const auto before = meter;
            meter.tick (meterBank.takeSessionPeak (insert, ch));
            if (meter.level != before.level || meter.peak != before.peak || meter.clipped != before.clipped)
                moved = true, movedInsert[(std::size_t) insert] = true;
        }
    };
    if (sections.volume > 0)
    {
        take (0);
        for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
            take (insertFor (t));
        // round 190ge: only the meters that moved are repainted (their fader part: bars, scale and handle), not the
        // whole mixer — it was repainted whole 30 times a second while the song played (most of the message thread)
        if (moved && ui::paintSavings())
        {
            juce::Rectangle<int> dirty;
            const auto add = [&] (int column) {
                if (const auto insert = insertFor (column); insert >= 0 && insert < maxMixerTracks && movedInsert[(std::size_t) insert])
                    dirty = dirty.getUnion (partBounds (column, Part::fader).expanded (1));
            };
            add (mainColumn);
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
            arms = (arms ^ (engine.isGridTrackArmed (t) ? 2u : 1u)) * 1099511628211ull;
        if (arms != armsShownState)
        {
            armsShownState = arms;
            repaint();
        }
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
}

// --- geometry ---------------------------------------------------------------------------------------------------------

juce::Rectangle<int> SessionMixerComponent::partBounds (int column, Part part, int row) const
{
    if (column != mainColumn && (column < 0 || column >= columns))
        return {};
    const auto x0 = stripX (column), w = stripWidth (column);
    const auto ioY = 0, sendsY = sections.io, optionsY = sendsY + sections.sends, volumeY = optionsY + sections.trackOptions;
    const auto main = column == mainColumn;
    const auto strip = stripFor (column);
    const auto full = [&] (int y, int h) { return juce::Rectangle<int> (x0 + pad, y, w - 2 * pad, h); };
    switch (part)
    {
        case Part::inType:
            return sections.io > 0 && ! main ? full (ioY + 15, 16) : juce::Rectangle<int>();
        case Part::inChannel:
            return sections.io > 0 && ! main && ! strip.notes && strip.insert > 0 ? full (ioY + 33, 16) : juce::Rectangle<int>();
        case Part::monIn:
        case Part::monAuto:
        case Part::monOff:
        {
            if (sections.io == 0 || main || strip.notes || strip.insert <= 0)
                return {};
            const auto r = full (ioY + 63, 16);
            const auto each = (r.getWidth() - 4) / 3;
            const auto i = part == Part::monIn ? 0 : part == Part::monAuto ? 1 : 2;
            return { r.getX() + i * (each + 2), r.getY(), i == 2 ? r.getRight() - (r.getX() + 2 * (each + 2)) : each, r.getHeight() };
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
            return sections.volume > 0 && ! main ? juce::Rectangle<int> (x0 + pad + 8, volumeY + 102, 28, 14) : juce::Rectangle<int>();
        case Part::fader:
            // the meter and its handle: from the handle's left to the scale
            return sections.volume > 0 && strip.insert >= 0 ? juce::Rectangle<int> (x0 + pad + 48, volumeY + 6, 20, sessionmixer::volumeHeight - 12) : juce::Rectangle<int>();
        case Part::none:
            break;
    }
    return {};
}

SessionMixerComponent::Hit SessionMixerComponent::hitAt (juce::Point<int> p) const
{
    const auto column = p.x >= tracksWidth ? (p.x < tracksWidth + mainWidth ? mainColumn : -1) : (p.x + scroll) / std::max (1, columnWidth);
    if (column != mainColumn && (column < 0 || column >= columns))
        return {};
    for (auto part : { Part::inType, Part::inChannel, Part::monIn, Part::monAuto, Part::monOff, Part::output, Part::delay, Part::volume, Part::pan,
                       Part::activator, Part::solo, Part::arm, Part::fader })
        if (partBounds (column, part).contains (p))
            return { column, part, 0 };
    for (int r = 0; r < (int) rows.size(); ++r)
        if (partBounds (column, Part::send, r).expanded (2).contains (p))
            return { column, Part::send, r };
    return { column, Part::none, 0 };
}

// --- values ---------------------------------------------------------------------------------------------------------

void SessionMixerComponent::mapSpots (std::vector<mapmode::Spot>& out)
{
    for (int column = -2; column < columns; ++column)
    {
        if (column == -1 || insertFor (column) < 0) // (a track that plays through no insert of its own shows no controls)
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
    }
}

ui::ValueSpec SessionMixerComponent::specFor (const Hit& hit) const
{
    const auto insert = insertFor (hit.column);
    const auto& song = engine.getSong();
    const auto name = hit.column == mainColumn ? tr ("Main") : insertName (song, insert);
    switch (hit.part)
    {
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
        default: break;
    }
    return 0.0;
}

void SessionMixerComponent::setValue (const Hit& hit, double v)
{
    const auto insert = insertFor (hit.column);
    if (insert < 0)
        return;
    switch (hit.part)
    {
        case Part::volume:
        case Part::fader:
            engine.editLevels ([insert, v = (float) juce::jlimit (0.0, 1.0, v)] (Song& s) {
                if (insert < (int) s.mixer.size())
                    s.mixer[(std::size_t) insert].volume = v;
            });
            break;
        case Part::pan:
            engine.editLevels ([insert, v = (float) juce::jlimit (-1.0, 1.0, v)] (Song& s) {
                if (insert < (int) s.mixer.size())
                    s.mixer[(std::size_t) insert].pan = v;
            });
            break;
        case Part::send:
            if (hit.row < (int) rows.size())
                engine.editSong ([insert, to = rows[(std::size_t) hit.row], v = (float) v] (Song& s) { sessionmixer::setSendPosition (s, insert, to, v); });
            break;
        case Part::delay:
            engine.editSong ([insert, v] (Song& s) { sessionmixer::setTrackDelayMs (s, insert, v); });
            break;
        default:
            break;
    }
    repaint();
}

// --- painting ---------------------------------------------------------------------------------------------------------

void SessionMixerComponent::paint (juce::Graphics& g)
{
    g.fillAll (c::bg2);
    if (sections.total() == 0)
        return;
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, tracksWidth, getHeight()));
        for (int t = std::max (0, scroll / std::max (1, columnWidth)); t < columns && stripX (t) < tracksWidth; ++t)
            paintStrip (g, t);
    }
    paintStrip (g, mainColumn);
    // the sections' edges across the whole width, as the reference separates In/Out, Sends and Volume
    g.setColour (c::border);
    for (const auto y : { sections.io, sections.io + sections.sends, sections.io + sections.sends + sections.trackOptions })
        if (y > 0 && y < getHeight())
            g.fillRect (0, y, getWidth(), 1);
    g.fillRect (tracksWidth, 0, 1, getHeight());
}

void SessionMixerComponent::paintStrip (juce::Graphics& g, int column)
{
    const auto& song = engine.getSong();
    const auto strip = stripFor (column);
    const auto main = column == mainColumn;
    const auto x0 = stripX (column), w = stripWidth (column);
    const juce::Rectangle<int> area (x0, 0, w, getHeight());
    g.setColour (main ? c::bg3 : c::bg2);
    g.fillRect (area);
    g.setColour (c::border);
    g.fillRect (area.getRight() - 1, 0, 1, getHeight());
    const auto hot = [this, column] (Part p, int row = 0) { return hover == Hit { column, p, row } || dragging == Hit { column, p, row }; };
    const auto* m = strip.insert >= 0 && strip.insert < (int) song.mixer.size() ? &song.mixer[(std::size_t) strip.insert] : nullptr;

    // In/Out [SHOT: Audio From / Ext. In / 1, Monitor In Auto Off, Audio To / Main]
    if (sections.io > 0 && ! main)
    {
        const auto label = [&] (int y, const juce::String& text) { drawLabel (g, { x0 + pad, y, w - 2 * pad, 12 }, text); };
        if (strip.notes)
        {
            // a track that plays an instrument: what it is played from (an armed or selected track takes every MIDI input)
            label (3, tr ("MIDI From"));
            g.setColour (c::textPrimary);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (tr ("All Ins"), partBounds (column, Part::inType).reduced (4, 0), juce::Justification::centredLeft, true);
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
            drawToggle (g, partBounds (column, Part::monIn), tr ("In"), mode == sessionmixer::Monitor::in, c::warning, hot (Part::monIn));
            drawToggle (g, partBounds (column, Part::monAuto), tr ("Auto"), mode == sessionmixer::Monitor::autoMode, c::warning, hot (Part::monAuto));
            drawToggle (g, partBounds (column, Part::monOff), tr ("Off"), mode == sessionmixer::Monitor::off, c::warning, hot (Part::monOff));
        }
        if (m != nullptr)
        {
            label (81, tr ("Audio To"));
            const auto out = sessionmixer::outputOf (song, strip.insert);
            drawBox (g, partBounds (column, Part::output), out == 0 ? tr ("Main") : out < 0 ? tr ("Sends Only") : insertName (song, out), true, hot (Part::output));
        }
    }

    // Sends: a knob per row (its target's name beside it)
    if (sections.sends > 0 && ! main && m != nullptr)
        for (int r = 0; r < (int) rows.size(); ++r)
        {
            const auto knob = partBounds (column, Part::send, r);
            const auto pos = sessionmixer::sendPosition (song, strip.insert, rows[(std::size_t) r]);
            theme::drawKnob (g, knob.toFloat(), std::max (0.0f, pos), false, pos > 0.0f ? c::info : c::knobArc.withAlpha (0.5f), hot (Part::send, r));
            g.setColour (pos >= 0.0f ? c::textSecondary : c::textDisabled);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (insertName (song, rows[(std::size_t) r]), juce::Rectangle<int> (knob.getRight() + 3, knob.getY(), x0 + w - pad - knob.getRight() - 3, knob.getHeight()),
                        juce::Justification::centredLeft, true);
        }

    // Track Options: Track Delay
    if (sections.trackOptions > 0 && ! main && m != nullptr)
    {
        const auto y = sections.io + sections.sends;
        drawLabel (g, { x0 + pad, y + 4, w - 2 * pad, 12 }, tr ("Track Delay"));
        const auto box = partBounds (column, Part::delay);
        drawBox (g, box, juce::String (sessionmixer::trackDelayMs (song, strip.insert) + 0.0, 2), false, hot (Part::delay));
        g.setColour (c::textSecondary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText ("ms", juce::Rectangle<int> (box.getRight() + 3, box.getY(), 18, box.getHeight()), juce::Justification::centredLeft, false);
    }

    // Volume: the volume box, pan, Track Activator, Solo, Arm on the left; the meter with its handle and scale on the right
    if (sections.volume > 0)
    {
        const auto top = sections.total() - sections.volume;
        if (m == nullptr && ! main)
        {
            g.setColour (c::textDisabled);
            g.setFont (theme::font (theme::type::caption));
            g.drawFittedText (strip.insertCount > 1 ? tr ("{0} mixer tracks", { juce::String (strip.insertCount) }) : tr ("No mixer track"),
                              juce::Rectangle<int> (x0 + pad, top + 20, w - 2 * pad, 36), juce::Justification::centred, 2);
        }
        if (m != nullptr)
        {
            drawBox (g, partBounds (column, Part::volume), utf8 (sessionmixer::volumeText (m->volume)), false, hot (Part::volume));
            const auto pan = partBounds (column, Part::pan).toFloat();
            theme::drawKnob (g, pan, (m->pan + 1.0f) * 0.5f, true, c::knobArc, hot (Part::pan));
            if (! main)
            {
                // the Track Activator carries the track's number, lit while the insert plays
                const auto act = partBounds (column, Part::activator);
                theme::fillRoundRect (g, act.toFloat(), 2.0f, m->muted ? c::bg4 : c::ledOn);
                g.setColour (m->muted ? c::textSecondary : c::bg0);
                g.setFont (theme::font (theme::type::small, true));
                g.drawText (juce::String (column + 1), act, juce::Justification::centred, false);
                drawToggle (g, partBounds (column, Part::solo), "S", m->solo, c::accent, hot (Part::solo));
            }
            paintMeter (g, strip.insert, partBounds (column, Part::fader));
        }
        if (! main)
        {
            const auto arm = partBounds (column, Part::arm).toFloat();
            const auto armed = engine.isGridTrackArmed (column);
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
        g.drawText (juce::String (db), juce::Rectangle<float> ((float) bars.getRight() + 5.0f, y - 5.0f, 18.0f, 10.0f), juce::Justification::centredLeft, false);
    }
    // the volume handle: a triangle pointing at the meter at the fader's level
    const auto volumeDb = sessionmixer::volumeDb (song.mixer[(std::size_t) insert].volume);
    const auto hy = yOf (std::isfinite (volumeDb) ? (float) volumeDb : -60.0f);
    juce::Path tri;
    tri.addTriangle ((float) handleArea.getX(), hy - 5.0f, (float) handleArea.getRight(), hy, (float) handleArea.getX(), hy + 5.0f);
    g.setColour (c::textPrimary.withAlpha (0.85f));
    g.fillPath (tri);
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
        {
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
            dragging = hit;
            if (hit.part == Part::fader)
                mouseDrag (e); // (the handle jumps to the click)
            else
                drag.begin (e, value);
            ui::showValueTip (*this, partBounds (hit.column, hit.part, hit.row), spec.text (valueFor (hit)));
            return;
        }
        case Part::activator:
            if (! popup)
                engine.editSong ([insert, on = ! sessionmixer::isActive (song, insert)] (Song& s) { sessionmixer::setActive (s, insert, on); });
            break;
        case Part::solo:
            if (! popup)
                engine.editSong ([insert, exclusive = ! e.mods.isCommandDown()] (Song& s) { sessionmixer::solo (s, insert, exclusive); });
            break;
        case Part::arm:
            if (! popup)
                engine.setGridTrackArmed (hit.column, ! engine.isGridTrackArmed (hit.column), ! e.mods.isCommandDown());
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
    if (dragging.part == Part::fader)
    {
        // the handle follows the pointer on the meter's dB scale
        const auto area = partBounds (dragging.column, Part::fader);
        const auto pos = juce::jlimit (0.0f, 1.0f, (float) (area.getBottom() - e.y) / (float) std::max (1, area.getHeight()));
        const auto db = pos <= 0.0f ? -INFINITY : -60.0 + 66.0 * pos;
        v = sessionmixer::positionForDb (db);
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
    if (hit.part == Part::volume || hit.part == Part::fader || hit.part == Part::pan || hit.part == Part::send || hit.part == Part::delay)
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
        case Part::send: return tr ("Send level. Turning it up from zero makes the send");
        case Part::delay: return tr ("Track Delay in milliseconds: positive plays the track later, negative earlier");
        case Part::volume: return tr ("Volume in dB: drag up or down, double-click to type");
        case Part::fader: return tr ("Drag the handle to set the volume; double-click the meter for 0 dB");
        case Part::pan: return tr ("Pan");
        case Part::activator: return tr ("Track Activator: switches the track's mixer track on or off");
        case Part::solo: return tr ("Solo (Ctrl-click: solo several tracks)");
        case Part::arm: return tr ("Arm Recording (Ctrl-click: arm several tracks)");
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
