#pragma once

// Round 190pf (research190oe P13): the Chord lane ---------------------------------------------------------------------
// A child of the Playlist under the ruler (and under the Sections lane when it shows; PlaylistComponent::chordLaneHeight):
// the song's chords (core/ChordLane.h) as named blocks over the time line, Cubase's / Logic's / Studio Pro's chord track
// done LYNX's way. A LYNX extra in every layout, off by default (Playlist menu › View › "Chord lane", Settings › LYNX
// extras; the Simple ruler's menu has it too). Looks:
//  - Pattern (18 px): FL's marker-tag style as the Sections lane's Pattern look, in a steel blue (sections are orange);
//  - Clip-launch (18 px): flat Live-grey blocks with the name;
//  - Simple (30 px): rounded blocks, the name centred.
// Mouse: double-click an empty place (or right-click › Add a chord here) picks a chord for the bar there; drag a block to
// move it (on the snap), its right edge to resize it; double-click or right-click a block to change it (the key's degree
// chords first — round 190nu's Scale degree chord — then any root and shape, and a bass); Delete removes it. The lane's
// menu detects chords from the clips under the time selection (or the selected clips; a selected audio clip through the
// polyphonic converter), makes them from the key regions, exports them as MIDI, gives them to the Chord pad and sets which
// channels follow them. Header only: included by PlaylistComponent.cpp (its side in ChordLane190pf.inc) and the shot route.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ChordLane.h"
#include "core/ChordLaneOps.h"
#include "core/NoteNames.h"
#include "core/ScaleKeys.h"
#include "engine/AudioEngine.h"
#include "ui/AudioToMidi.h"
#include "ui/FlMods.h"
#include "ui/I18n.h"
#include "ui/PlaylistComponent.h"
#include "ui/SimpleLook.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/WorkflowExtras.h"

namespace lynx
{
// The lane's name of a chord in the spelling the app shows (flats when the note names use flats)
inline juce::String chordLaneName (const SongChord& c)
{
    return juce::String::fromUTF8 (chordlane::name (c, notenames::currentFlats.load()).c_str());
}

// The chord picker (a block's menu and a new block's): the degree chords of the key in force there (triads, then a
// submenu of sevenths), any root with any shape, and the bass. `pick` gets the harmony (start / length unused).
inline void addChordPicker (juce::PopupMenu& menu, const Song& song, std::int64_t tick, const SongChord* current, std::function<void (SongChord)> pick)
{
    const auto flats = notenames::currentFlats.load();
    const auto same = [current] (const SongChord& c) {
        return current != nullptr && current->root == c.root && current->quality == c.quality && current->extensions == c.extensions;
    };
    const auto key = chordlane::keyAt (song, tick).value_or (projectkey::Key { 0, keyregions::majorScale() });
    menu.addSectionHeader (tr ("Chords of {0}", { juce::String (projectkey::rootName (key.root)) + " " + tr (notetools::flReferenceScales()[(std::size_t) std::max (0, key.flScale)].name) }));
    for (const auto& d : chordlane::degreeChords (key))
        menu.addItem (juce::String::fromUTF8 (d.numeral.c_str()) + "\t" + chordLaneName (d.chord), true, same (d.chord), [pick, c = d.chord] { pick (c); });
    juce::PopupMenu sevenths;
    for (const auto& d : chordlane::degreeChords (key, keyregions::DegreeSize::seventh))
        sevenths.addItem (juce::String::fromUTF8 (d.numeral.c_str()) + "\t" + chordLaneName (d.chord), true, same (d.chord), [pick, c = d.chord] { pick (c); });
    menu.addSubMenu (tr ("Sevenths"), sevenths);
    menu.addSeparator();
    juce::PopupMenu roots;
    for (int r = 0; r < 12; ++r)
    {
        juce::PopupMenu shapes;
        for (int s = 0; s < chordlane::shapeCount; ++s)
        {
            const auto c = chordlane::make (r, s);
            shapes.addItem (chordLaneName (c), true, same (c), [pick, c, current] {
                auto x = c;
                if (current != nullptr && current->root == x.root)
                    x.bass = current->bass; // (a new shape on the same root keeps its bass)
                pick (x);
            });
        }
        roots.addSubMenu (chordlane::rootName (r, flats), shapes, true, nullptr, current != nullptr && current->root == r);
    }
    menu.addSubMenu (tr ("Other chord"), roots);
    if (current != nullptr)
    {
        juce::PopupMenu bass;
        bass.addItem (tr ("The root"), true, current->bass < 0, [pick, c = *current] {
            auto x = c;
            x.bass = -1;
            pick (x);
        });
        for (int r = 0; r < 12; ++r)
            if (r != current->root)
                bass.addItem (juce::String ("/") + chordlane::rootName (r, flats), true, current->bass == r, [pick, c = *current, r] {
                    auto x = c;
                    x.bass = r;
                    pick (x);
                });
        menu.addSubMenu (tr ("Bass note"), bass);
    }
}

class ChordLaneView final : public juce::Component,
                            public ui::HintSource
{
public:
    ChordLaneView (AudioEngine& e, PlaylistComponent& p) : engine (e), playlist (p)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        setComponentID ("playlist.chordLane");
        setOpaque (true);
    }

    enum class Look { fl, live, simple };
    Look look() const noexcept { return playlist.simpleHeaders ? Look::simple : playlist.liveStyle ? Look::live : Look::fl; }
    int selected() const noexcept { return chordlane::indexOfUid (engine.getSong().chords(), selectedUid); }
    void select (int index)
    {
        const auto& list = engine.getSong().chords();
        selectedUid = index >= 0 && index < (int) list.size() ? list[(std::size_t) index].uid : 0;
        repaint();
    }

    // --- geometry ------------------------------------------------------------------------------------------------------
    juce::Rectangle<float> blockOf (const SongChord& c) const
    {
        const auto x0 = (float) playlist.tickToX ((double) c.start), x1 = (float) playlist.tickToX ((double) c.end());
        return look() == Look::simple ? juce::Rectangle<float> (x0 + 1.0f, 3.0f, juce::jmax (2.0f, x1 - x0 - 2.0f), (float) getHeight() - 6.0f)
                                      : juce::Rectangle<float> (x0, 1.0f, juce::jmax (1.0f, x1 - x0), (float) getHeight() - 2.0f);
    }

    struct Hit
    {
        int chord = -1;
        enum Part { none, body, edge, header, lane } part = none;
    };
    Hit hitAt (juce::Point<int> p) const
    {
        Hit h;
        if (p.x < playlist.headerWidth)
        {
            h.part = Hit::header;
            return h;
        }
        const auto& list = engine.getSong().chords();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto r = blockOf (list[(std::size_t) i]);
            if (std::abs ((float) p.x - r.getRight()) <= 4.0f && r.getWidth() > 10.0f)
                return { i, Hit::edge };
            if ((float) p.x >= r.getX() && (float) p.x < r.getRight())
                return { i, Hit::body };
        }
        h.part = Hit::lane;
        return h;
    }

    // --- painting --------------------------------------------------------------------------------------------------------
    // Kept as a picture while nothing it shows changes (as the Sections lane: the Playlist repaints often); a drag paints
    // directly.
    void paint (juce::Graphics& g) override
    {
        if (drag != Drag::none)
        {
            cacheValid = false;
            paintLane (g);
            return;
        }
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        CacheKey key { engine.getSong().chords(), selectedUid, hover.chord, (int) hover.part, playlist.scrollTicks, playlist.pixelsPerTick, getWidth(),
                       getHeight(), playlist.headerWidth, (int) look(), scale, theme::colour::bg1.getARGB(), notenames::currentFlats.load() };
        if (! cacheValid || ! (key == cacheKey))
        {
            cache = juce::Image (juce::Image::RGB, juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)),
                                 juce::jmax (1, juce::roundToInt ((float) getHeight() * scale)), false);
            juce::Graphics cg (cache);
            cg.addTransform (juce::AffineTransform::scale (scale));
            paintLane (cg);
            cacheKey = std::move (key);
            cacheValid = true;
        }
        g.drawImage (cache, getLocalBounds().toFloat());
    }

    static juce::Colour laneColour() noexcept { return juce::Colour (0xff5b8fc9); } // (steel blue: the sections are FL's orange)

    void paintLane (juce::Graphics& g)
    {
        using namespace theme;
        const auto& list = engine.getSong().chords();
        const auto lk = look();
        const auto simple = lk == Look::simple;
        const auto gridRight = getWidth() - PlaylistComponent::scrollBarSize;
        g.fillAll (simple ? colour::bg1 : colour::bg2);
        g.setColour (simple ? colour::bg2 : colour::bg3);
        g.fillRect (0, 0, playlist.headerWidth, getHeight());
        g.setColour (colour::textSecondary);
        g.setFont (font (simple ? type::small : type::caption, simple));
        textcache::drawText (g, tr ("Chords"), simple ? 14 : 6, 0, playlist.headerWidth - 12, getHeight(), juce::Justification::centredLeft);
        g.setColour (colour::border);
        g.fillRect (playlist.headerWidth - 1, 0, 1, getHeight());
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (playlist.headerWidth, 0, gridRight - playlist.headerWidth, getHeight());
        const auto sel = selected();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& c = list[(std::size_t) i];
            auto r = blockOf (c);
            if (r.getRight() < (float) playlist.headerWidth || r.getX() > (float) gridRight)
                continue;
            if (drag == Drag::move && c.uid == dragUid && moved)
            {
                paintBlock (g, c, r, true, false, 0.35f);
                r = r.withX ((float) playlist.tickToX ((double) dragStart));
                paintBlock (g, c, r, true, false, 0.8f);
                continue;
            }
            if (drag == Drag::resize && c.uid == dragUid)
                r = r.withRight (juce::jmax (r.getX() + 2.0f, (float) playlist.tickToX ((double) resizeEnd)));
            paintBlock (g, c, r, i == sel, hover.chord == i && hover.part != Hit::none, 1.0f);
        }
        if (list.empty())
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (simple ? type::small : type::caption, simple));
            textcache::drawText (g, tr ("Double-click to add a chord, or right-click to detect them"), playlist.headerWidth + 8, 0, 520, getHeight(),
                                 juce::Justification::centredLeft);
        }
    }

    void paintBlock (juce::Graphics& g, const SongChord& c, juce::Rectangle<float> r, bool isSelected, bool hovered, float alpha) const
    {
        using namespace theme;
        const auto name = chordLaneName (c);
        switch (look())
        {
            case Look::simple:
            {
                const auto body = (isSelected ? juce::Colour (0xff7aa6d8) : juce::Colour (0xff3f5f86)).withMultipliedBrightness (hovered && ! isSelected ? 1.15f : 1.0f);
                g.setColour (body.withMultipliedAlpha (alpha));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (ui::simplelook::inkOn (body).withMultipliedAlpha (alpha));
                g.setFont (font (type::body, true));
                textcache::drawText (g, name, r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
                return;
            }
            case Look::live:
            {
                const auto body = isSelected ? juce::Colour (0xffb4c8dc) : juce::Colour (hovered ? 0xff6e7a86 : 0xff5d6874);
                g.setColour (body.withMultipliedAlpha (alpha));
                g.fillRect (r);
                g.setColour (colour::bg0.withMultipliedAlpha (alpha));
                g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), 1.0f, r.getHeight()));
                g.setColour ((isSelected ? juce::Colour (0xff101214) : colour::textPrimary).withMultipliedAlpha (alpha));
                g.setFont (font (type::caption, false));
                textcache::drawText (g, name, r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
                return;
            }
            case Look::fl:
            default:
            {
                // the Playlist's marker tag in the lane's colour over its span, tinted (the Sections lane's Pattern look)
                const auto fill = laneColour();
                g.setColour (fill.withAlpha ((isSelected ? 0.34f : hovered ? 0.26f : 0.16f) * alpha));
                g.fillRect (r);
                g.setColour (fill.withMultipliedAlpha (alpha));
                g.fillRect (juce::Rectangle<float> (r.getX(), 0.0f, 1.0f, (float) getHeight()));
                g.setFont (font (type::caption, true));
                const auto width = juce::jmin (r.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 8.0f);
                const auto tag = juce::Rectangle<float> (r.getX(), (float) getHeight() - 13.0f, width, 11.0f);
                g.fillRoundedRectangle (tag, 2.0f);
                g.setColour (colour::bg0.withMultipliedAlpha (alpha));
                textcache::drawText (g, name, tag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
                if (isSelected)
                {
                    g.setColour (colour::textPrimary.withMultipliedAlpha (alpha));
                    g.drawRect (r, 1.0f);
                }
                return;
            }
        }
    }

    // --- mouse -----------------------------------------------------------------------------------------------------------
    std::int64_t snapped (double tick) const { return std::max<std::int64_t> (0, playlist.snap (std::max (0.0, tick), true)); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        drag = Drag::none;
        moved = false;
        if (flmods::popup (e.mods))
        {
            if (h.chord >= 0)
            {
                select (h.chord);
                chordMenu (h.chord).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            }
            else
                laneMenu ((std::int64_t) std::max (0.0, playlist.xToTick (e.x)))
                    .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        if (h.chord < 0)
        {
            select (-1);
            return;
        }
        select (h.chord);
        const auto& c = engine.getSong().chords()[(std::size_t) h.chord];
        dragUid = c.uid;
        if (h.part == Hit::edge)
        {
            drag = Drag::resize;
            resizeEnd = c.end();
        }
        else
        {
            drag = Drag::move;
            grabTicks = playlist.xToTick (e.x) - (double) c.start;
            dragStart = c.start;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto index = chordlane::indexOfUid (engine.getSong().chords(), dragUid);
        if (index < 0)
            return;
        const auto& c = engine.getSong().chords()[(std::size_t) index];
        if (drag == Drag::resize)
        {
            resizeEnd = std::max (snapped (playlist.xToTick (e.x)), c.start + 1);
            repaint();
        }
        else if (drag == Drag::move)
        {
            moved = moved || e.getDistanceFromDragStart() > 3;
            dragStart = snapped (playlist.xToTick (e.x) - grabTicks);
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        const auto was = drag;
        drag = Drag::none;
        const auto index = chordlane::indexOfUid (engine.getSong().chords(), dragUid);
        if (was == Drag::resize && index >= 0)
            engine.editSong ([index, end = resizeEnd] (Song& s) { chordlane::resize (s.chords(), index, end); });
        else if (was == Drag::move && moved && index >= 0)
        {
            std::uint64_t uid = 0;
            engine.editSong ([index, at = dragStart, &uid] (Song& s) {
                const auto i = chordlane::move (s.chords(), index, at);
                uid = i >= 0 ? s.chords()[(std::size_t) i].uid : 0;
            });
            selectedUid = uid;
        }
        moved = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        if (h.chord >= 0)
            chordMenu (h.chord).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        else if (h.part == Hit::lane)
            addMenu ((std::int64_t) std::max (0.0, playlist.xToTick (e.x))).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        if (h.chord != hover.chord || h.part != hover.part)
        {
            hover = h;
            setMouseCursor (h.part == Hit::edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = {};
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        playlist.mouseWheelMove (e.getEventRelativeTo (&playlist), wheel);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto index = selected();
        if (index < 0)
            return false;
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            engine.editSong ([index] (Song& s) { chordlane::remove (s.chords(), index); });
            select (-1);
            return true;
        }
        return false;
    }

    // --- commands --------------------------------------------------------------------------------------------------------
    // A new block at `tick`: from the bar line there to the next bar (or to the next chord when it starts sooner)
    void addChordAt (std::int64_t tick, SongChord harmony)
    {
        const auto& song = engine.getSong();
        const auto bar = song.barAt (tick);
        auto c = harmony;
        c.start = bar.tick;
        c.length = std::max<std::int64_t> (1, bar.length);
        const auto& list = song.chords();
        if (const auto inside = chordlane::indexAt (list, c.start); inside >= 0 && list[(std::size_t) inside].start < c.start)
            c.start = std::max (c.start, list[(std::size_t) inside].start); // (cuts the one it lands in)
        for (const auto& other : list)
            if (other.start > c.start && other.start < c.end())
                c.length = other.start - c.start;
        std::uint64_t uid = 0;
        engine.editSong ([c, &uid] (Song& s) {
            const auto i = chordlane::put (s.chords(), c);
            uid = i >= 0 ? s.chords()[(std::size_t) i].uid : 0;
        });
        selectedUid = uid;
        repaint();
    }

    juce::PopupMenu addMenu (std::int64_t tick)
    {
        juce::PopupMenu menu;
        addChordPicker (menu, engine.getSong(), tick, nullptr, [this, tick] (SongChord c) { addChordAt (tick, c); });
        return menu;
    }

    juce::PopupMenu chordMenu (int index)
    {
        juce::PopupMenu menu;
        const auto& list = engine.getSong().chords();
        if (index < 0 || index >= (int) list.size())
            return menu;
        const auto c = list[(std::size_t) index];
        const auto uid = c.uid;
        menu.addSectionHeader (chordLaneName (c));
        addChordPicker (menu, engine.getSong(), c.start, &c, [this, uid] (SongChord pick) {
            engine.editSong ([uid, pick] (Song& s) { chordlane::setChord (s.chords(), chordlane::indexOfUid (s.chords(), uid), pick); });
            repaint();
        });
        menu.addSeparator();
        menu.addItem (tr ("Duplicate"), [this, uid] {
            engine.editSong ([uid] (Song& s) {
                const auto i = chordlane::indexOfUid (s.chords(), uid);
                if (i < 0)
                    return;
                auto copy = s.chords()[(std::size_t) i];
                copy.start = copy.end();
                chordlane::put (s.chords(), copy);
            });
            repaint();
        });
        menu.addItem (tr ("Delete") + "\tDel", [this, uid] {
            engine.editSong ([uid] (Song& s) { chordlane::remove (s.chords(), chordlane::indexOfUid (s.chords(), uid)); });
            select (-1);
        });
        menu.addSeparator();
        menu.addItem (tr ("Loop this chord"), [this, from = c.start, to = c.end()] {
            engine.editSong ([from, to] (Song& song) {
                song.loopStart = from;
                song.loopEnd = to;
            }, false);
            playlist.repaint();
        });
        return menu;
    }

    // The range the lane's commands work on: the time selection, else the song (to the end of its last clip)
    std::pair<std::int64_t, std::int64_t> workRange() const
    {
        const auto& song = engine.getSong();
        if (song.loopEnd > song.loopStart)
            return { song.loopStart, song.loopEnd };
        std::int64_t end = 0;
        for (const auto& c : song.playlist)
            if (c.gridSlot < 0 && c.kind != ClipKind::automation)
                end = std::max (end, c.end());
        return { 0, end };
    }

    // the selected Playlist clips of a kind
    std::vector<int> selectedOf (ClipKind kind) const
    {
        std::vector<int> out;
        const auto& song = engine.getSong();
        for (const auto i : playlist.selection)
            if (i >= 0 && i < (int) song.playlist.size() && song.playlist[(std::size_t) i].kind == kind)
                out.push_back (i);
        return out;
    }

    void detectFromNotes (chordlane::Resolution resolution)
    {
        auto [from, to] = workRange();
        const auto clips = selectedOf (ClipKind::pattern);
        const auto& song = engine.getSong();
        if (! clips.empty() && song.loopEnd <= song.loopStart)
        {
            // the selected clips' span
            from = std::numeric_limits<std::int64_t>::max();
            to = 0;
            for (const auto i : clips)
            {
                from = std::min (from, song.playlist[(std::size_t) i].start);
                to = std::max (to, song.playlist[(std::size_t) i].end());
            }
        }
        const auto found = chordlane::detect (song, chordlane::songNotes (song, from, to, clips), from, to, resolution);
        if (found.empty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Chord lane"), tr ("No chords were found in the notes there."));
            return;
        }
        engine.editSong ([from = from, to = to, found] (Song& s) { chordlane::write (s, from, to, found); });
        repaint();
    }

    void detectFromAudio (int clipIndex)
    {
        juce::Component::SafePointer<ChordLaneView> self (this);
        detectClipNotes (engine, clipIndex, [self] (std::vector<chordlane::TimedNote> notes, std::int64_t from, std::int64_t to) {
            if (self == nullptr)
                return;
            const auto found = chordlane::detect (self->engine.getSong(), notes, from, to, chordlane::Resolution::halfBar);
            if (found.empty())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Chord lane"), tr ("No chords were found in this audio."));
                return;
            }
            self->engine.editSong ([from, to, found] (Song& s) { chordlane::write (s, from, to, found); });
            self->repaint();
        });
    }

    juce::PopupMenu laneMenu (std::int64_t tick)
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        juce::PopupMenu add;
        addChordPicker (add, song, tick, nullptr, [this, tick] (SongChord c) { addChordAt (tick, c); });
        menu.addSubMenu (tr ("Add a chord here"), add);
        menu.addSeparator();
        juce::PopupMenu detect;
        detect.addItem (tr ("A chord per bar"), [this] { detectFromNotes (chordlane::Resolution::bar); });
        detect.addItem (tr ("A chord per half bar"), [this] { detectFromNotes (chordlane::Resolution::halfBar); });
        detect.addItem (tr ("A chord per beat"), [this] { detectFromNotes (chordlane::Resolution::beat); });
        menu.addSubMenu (tr ("Detect chords from the notes"), detect);
        const auto audio = selectedOf (ClipKind::audio);
        menu.addItem (tr ("Detect chords from the selected audio clip"), audio.size() == 1, false, [this, clip = audio.empty() ? -1 : audio.front()] { detectFromAudio (clip); });
        menu.addItem (tr ("Chords from the key regions"), [this] {
            const auto [from, to] = workRange();
            const auto found = chordlane::fromKeyRegions (engine.getSong(), from, std::max (to, from + engine.getSong().barAt (from).length));
            if (! found.empty())
                engine.editSong ([from = found.front().start, to = found.back().end(), found] (Song& s) { chordlane::write (s, from, to, found); });
            repaint();
        });
        menu.addSeparator();
        const auto any = ! song.chords().empty();
        menu.addItem (tr ("Export chords as MIDI"), any, false, [this] {
            const auto channel = engine.getSelectedChannel();
            const auto& s = engine.getSong();
            const auto usable = channel >= 0 && channel < (int) s.channels.size() && chordlane::harmonic (s.channels[(std::size_t) channel])
                                && a2m::canPlayNotes (s.channels[(std::size_t) channel]);
            engine.editSong ([c = usable ? channel : -1, p = tr ("Chords").toStdString()] (Song& song) { chordlane::exportAsPattern (song, c, p, p); });
            playlist.sortClips();
            playlist.repaint();
        });
        menu.addItem (tr ("Use these chords on the Chord pad"), any, false, [this] {
            engine.editSong ([] (Song& s) { s.keyboardChords = chordlane::padSet (s.chords()); });
            scalekeys::current().projectChords = true;
            scalekeys::current().mode = scalekeys::Mode::chordPad;
        });
        // the channels that follow the chords (Cubase's Follow Chord Track): Off / Chord tones / Bass, per instrument channel
        {
            juce::PopupMenu follow;
            int shown = 0;
            for (int c = 0; c < (int) song.channels.size() && shown < 64; ++c)
            {
                const auto& ch = song.channels[(std::size_t) c];
                if (! a2m::canPlayNotes (ch) || (! chordlane::harmonic (ch) && ch.followChords == 0))
                    continue;
                ++shown;
                juce::PopupMenu modes;
                const std::pair<const char*, int> items[] = { { TR_NOOP ("Off"), 0 }, { TR_NOOP ("Chord tones"), 1 }, { TR_NOOP ("Bass note"), 2 } };
                for (const auto& [text, mode] : items)
                    modes.addItem (tr (text), true, ch.followChords == mode, [this, c, m = mode] {
                        engine.editSong ([c, m] (Song& s) { s.channels[(std::size_t) c].followChords = (std::uint8_t) m; });
                    });
                follow.addSubMenu (juce::String (c + 1) + "  " + juce::String::fromUTF8 (ch.name.c_str()), modes, true, nullptr, ch.followChords != 0);
            }
            menu.addSubMenu (tr ("Channels that follow the chords"), follow, shown > 0);
        }
        menu.addSeparator();
        menu.addItem (tr ("Clear the chords in the time selection"), any, false, [this] {
            const auto [from, to] = workRange();
            engine.editSong ([from, to] (Song& s) { chordlane::clearRange (s.chords(), from, std::max (to, from + 1)); });
            select (-1);
        });
        menu.addItem (tr ("Chord lane") + " (LYNX)", true, true, [] { workflowextras::setExtra ("playlist.chordLane", false); });
        return menu;
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        const auto h = hitAt (p);
        const auto& list = engine.getSong().chords();
        if (h.part == Hit::header)
            return { tr ("Chords: the harmony of the song. Channels can follow them; the Piano roll can light them"), {}, false, true };
        if (h.chord >= 0 && h.chord < (int) list.size())
        {
            const auto& c = list[(std::size_t) h.chord];
            if (h.part == Hit::edge)
                return { tr ("Drag to resize the chord"), chordLaneName (c) };
            return { tr ("[LMB] drag to move  [2xLMB] change the chord  [Del] delete  [RMB] menu"), chordLaneName (c), false, true };
        }
        return { tr ("[2xLMB] add a chord  [RMB] menu: detect chords, follow, export"), {}, false, true };
    }

private:
    struct CacheKey
    {
        std::vector<SongChord> chords;
        std::uint64_t selected = 0;
        int hoverChord = -1, hoverPart = 0;
        double scrollTicks = 0.0, pixelsPerTick = 0.0;
        int width = 0, height = 0, headerWidth = 0, look = 0;
        float scale = 1.0f;
        juce::uint32 ground = 0;
        bool flats = false;
        bool operator== (const CacheKey&) const = default;
    };
    AudioEngine& engine;
    PlaylistComponent& playlist;
    CacheKey cacheKey;
    juce::Image cache;
    bool cacheValid = false;
    enum class Drag { none, move, resize };
    Drag drag = Drag::none;
    std::uint64_t dragUid = 0, selectedUid = 0;
    double grabTicks = 0.0;
    std::int64_t dragStart = 0, resizeEnd = 0;
    bool moved = false;
    Hit hover;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordLaneView)
};
} // namespace lynx
