#pragma once

// Round 190ol (research190oe P1): the Sections lane --------------------------------------------------------------------
// A child of the Playlist between the ruler and the video section / clip tracks (PlaylistComponent::sectionsLaneHeight):
// the song's sections (core/Sections.h) as named blocks over the time line, with the commands of core/SectionOps.h. Looks:
//  - Simple (44 px): GarageBand's Arrangement track — rounded blocks in the section's colour with the name, "+" in the
//    header column adds an eight-bar section at the end;
//  - Pattern (18 px, a LYNX extra): the Playlist's marker tags (the ruler's marker painting): a tag in the section's colour
//    with its name at the start, the section's span tinted.
// Mouse: drag a section to move it with everything under it (a line shows where it goes; dropped onto the middle of
// another one, the two swap, GarageBand's drop onto a marker); Ctrl+drag (Option on macOS) copies it; Shift+drag onto
// another replaces that one's content (GarageBand's Command-drag); the right edge resizes it (content stays); double-click
// renames; Delete empties it (its clips go), Delete again removes it and closes the gap. Right-click: the section's menu,
// or the lane's (add, from markers, to markers, scenes). Header only: included by PlaylistComponent.cpp (which defines
// the Playlist's side in SectionsLane190ol.inc) and by the shot route (src/SectionsShot190ol.inc).

// The name a new section gets in the language shown: GarageBand's names in order (translated), the first one no section
// has yet, then "Section N"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/SectionOps.h"
#include "core/Sections.h"
#include "engine/AudioEngine.h"
#include "ui/ColourPicker.h"
#include "ui/DemoMode.h"
#include "ui/FlMods.h"
#include "ui/I18n.h"
#include "ui/PlaylistComponent.h"
#include "ui/SimpleLook.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WorkflowExtras.h"

namespace lynx
{
inline juce::String sectionPresetName (int index)
{
    switch (index)
    {
        case 0: return trc ("@song section: Intro");
        case 1: return trc ("@song section: Verse");
        case 2: return trc ("@song section: Chorus");
        case 3: return trc ("@song section: Bridge");
        default: return trc ("@song section: Outro");
    }
}

inline std::string nextSectionName (const std::vector<SongSection>& list)
{
    const auto taken = [&list] (const juce::String& name) {
        return std::any_of (list.begin(), list.end(), [&name] (const SongSection& s) { return juce::String::fromUTF8 (s.name.c_str()) == name; });
    };
    for (int i = 0; i < (int) std::size (sections::presetNames); ++i)
        if (const auto name = sectionPresetName (i); ! taken (name))
            return name.toStdString();
    for (int n = (int) list.size() + 1;; ++n)
        if (const auto name = tr ("Section {0}", { juce::String (n) }); ! taken (name))
            return name.toStdString();
}

class SectionsLaneView final : public juce::Component,
                               public ui::HintSource,
                               public demo::CellSource
{
public:
    SectionsLaneView (AudioEngine& e, PlaylistComponent& p) : engine (e), playlist (p)
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (true);
        setComponentID ("playlist.sectionsLane");
        setOpaque (true);
    }

    bool simpleLook() const noexcept { return playlist.simpleHeaders; }
    int selected() const noexcept { return sections::indexOfUid (engine.getSong().sections(), selectedUid); }
    void select (int index)
    {
        const auto& list = engine.getSong().sections();
        selectedUid = index >= 0 && index < (int) list.size() ? list[(std::size_t) index].uid : 0;
        repaint();
    }

    // round 190pg (--demo, docs/DEMO_MODE.md): column = a section's block (0 = the first), -1 = the "+" button; row unused
    juce::Rectangle<int> demoCellBounds (int, int column) const override
    {
        const auto& list = engine.getSong().sections();
        if (column == -1)
            return addButton();
        return column >= 0 && column < (int) list.size() ? blockOf (list[(std::size_t) column]).toNearestInt() : juce::Rectangle<int>();
    }

    // --- geometry ------------------------------------------------------------------------------------------------------
    juce::Rectangle<int> addButton() const
    {
        const auto size = simpleLook() ? 24 : 14;
        return { playlist.headerWidth - size - (simpleLook() ? 10 : 4), (getHeight() - size) / 2, size, size };
    }
    juce::Rectangle<float> blockOf (const SongSection& s) const
    {
        const auto x0 = (float) playlist.tickToX ((double) s.start), x1 = (float) playlist.tickToX ((double) s.end());
        return simpleLook() ? juce::Rectangle<float> (x0, 0.0f, juce::jmax (2.0f, x1 - x0), (float) getHeight() - 1.0f)
                            : juce::Rectangle<float> (x0, 1.0f, juce::jmax (1.0f, x1 - x0), (float) getHeight() - 2.0f);
    }

    struct Hit
    {
        int section = -1;
        enum Part { none, body, edge, add, header, lane } part = none;
    };
    Hit hitAt (juce::Point<int> p) const
    {
        Hit h;
        if (p.x < playlist.headerWidth)
        {
            h.part = addButton().expanded (3).contains (p) ? Hit::add : Hit::header;
            return h;
        }
        const auto& list = engine.getSong().sections();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto r = blockOf (list[(std::size_t) i]);
            if (std::abs ((float) p.x - r.getRight()) <= 4.0f && r.getWidth() > 10.0f)
                return { i, Hit::edge };
            if (r.contains (p.toFloat()) || ((float) p.x >= r.getX() && (float) p.x < r.getRight()))
                return { i, Hit::body };
        }
        h.part = Hit::lane;
        return h;
    }

    // --- painting --------------------------------------------------------------------------------------------------------
    // The lane's picture is kept while nothing it shows changes (the sections, the selection, the hover, the view, the
    // look): the Playlist repaints often (the playhead, meters) and the lane is then one image blit. A drag paints directly.
    void paint (juce::Graphics& g) override
    {
        if (drag != Drag::none)
        {
            cacheValid = false;
            paintLane (g);
            return;
        }
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        // (round 190pb, Windows: the key's fields compared where they are; the sections are copied only for a new picture,
        // not on every paint)
        const auto& sectionsNow = engine.getSong().sections();
        const auto same = cacheValid && cacheKey.sections == sectionsNow && cacheKey.selected == selectedUid && cacheKey.hoverSection == hover.section
                          && cacheKey.hoverPart == (int) hover.part && cacheKey.scrollTicks == playlist.scrollTicks
                          && cacheKey.pixelsPerTick == playlist.pixelsPerTick && cacheKey.width == getWidth() && cacheKey.height == getHeight()
                          && cacheKey.headerWidth == playlist.headerWidth && cacheKey.simple == simpleLook() && cacheKey.scale == scale
                          && cacheKey.ground == theme::colour::bg1.getARGB() && cacheKey.marker == theme::colour::warning.getARGB();
        if (! same)
        {
            cacheKey = { sectionsNow, selectedUid, hover.section, (int) hover.part, playlist.scrollTicks, playlist.pixelsPerTick, getWidth(),
                         getHeight(), playlist.headerWidth, simpleLook(), scale, theme::colour::bg1.getARGB(), theme::colour::warning.getARGB() };
            // (round 190pb: the picture of the same size is drawn over again, not made anew: on Windows a new one is a new
            // Direct2D bitmap; paintLane fills every pixel of it)
            const auto w = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
            if (! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
                cache = juce::Image (juce::Image::RGB, w, h, false);
            juce::Graphics cg (cache);
            cg.addTransform (juce::AffineTransform::scale (scale));
            paintLane (cg);
            cacheValid = true;
        }
        g.drawImage (cache, getLocalBounds().toFloat());
    }

    void paintHeader (juce::Graphics& g)
    {
        using namespace theme;
        const auto simple = simpleLook();
        g.setColour (simple ? colour::bg2 : colour::bg3);
        g.fillRect (0, 0, playlist.headerWidth, getHeight());
        g.setColour (colour::textSecondary);
        g.setFont (font (simple ? type::small : type::caption, simple));
        textcache::drawText (g, tr ("Sections"), simple ? 14 : 6, 0, playlist.headerWidth - 40, getHeight(), juce::Justification::centredLeft);
        {
            // (Simple: GarageBand's Add Marker button, a light disc with a dark "+" at the header's right end)
            const auto plus = addButton().toFloat();
            const auto over = hover.part == Hit::add;
            if (simple)
            {
                g.setColour (over ? colour::textPrimary : colour::textPrimary.withAlpha (0.85f));
                g.fillEllipse (plus);
                ui::simplelook::drawGlyph (g, ui::simplelook::Glyph::plus, plus.reduced (6.0f), colour::bg1, 2.0f);
            }
            else
            {
                g.setColour (over ? colour::bg6 : colour::bg4);
                g.fillRoundedRectangle (plus, 2.0f);
                ui::simplelook::drawGlyph (g, ui::simplelook::Glyph::plus, plus.reduced (3.0f), over ? colour::textPrimary : colour::textSecondary, 1.3f);
            }
        }
        g.setColour (colour::border);
        g.fillRect (playlist.headerWidth - 1, 0, 1, getHeight());
        g.setColour (colour::border); // (the lane's bottom line, which paintLane draws over the header too)
        g.fillRect (0, getHeight() - 1, playlist.headerWidth, 1);
    }

    void paintLane (juce::Graphics& g)
    {
        using namespace theme;
        const auto& song = engine.getSong();
        const auto& list = song.sections();
        const auto simple = simpleLook();
        const auto gridRight = getWidth() - PlaylistComponent::scrollBarSize;
        g.fillAll (simple ? colour::bg1 : colour::bg2); // (opaque: the scroll bar's column too)
        // header column: the lane's name and its "+" (round 190pb: from a picture of it, made again when the look, the hover,
        // the size, the palette or the language change; it sits at the lane's origin, so the picture is the same drawing.
        // On Direct2D the "+" glyph is a new path geometry and the name a new glyph run on every rebuild of the lane)
        const auto headerScale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto whole = (int) std::round (headerScale);
        if (ui::paintCache() && whole >= 1 && whole <= 3 && std::abs (headerScale - (float) whole) <= 1.0e-4f && playlist.headerWidth > 0)
        {
            auto key = textcache::detail::mixHash (0x13198a2e03707344ull, (std::uint64_t) whole);
            for (const std::uint64_t v : { (std::uint64_t) playlist.headerWidth, (std::uint64_t) getHeight(), (std::uint64_t) (simple ? 1 : 0),
                                           (std::uint64_t) (hover.part == Hit::add ? 1 : 0), (std::uint64_t) theme::paletteStampNow(),
                                           (std::uint64_t) tr ("Sections").hashCode64(), (std::uint64_t) colour::border.getARGB(),
                                           (std::uint64_t) colour::bg3.getARGB(), (std::uint64_t) juce::roundToInt (font (simple ? type::small : type::caption, simple).getHeight() * 1000.0f) })
                key = textcache::detail::mixHash (key, v);
            if (key != headerKey || ! headerPicture.isValid())
            {
                headerKey = key;
                headerPicture = juce::Image (juce::Image::RGB, playlist.headerWidth * whole, getHeight() * whole, false);
                juce::Graphics hg (headerPicture);
                hg.addTransform (juce::AffineTransform::scale ((float) whole));
                paintHeader (hg);
            }
            g.setColour (juce::Colours::black); // (an image is drawn with the current colour's alpha as its opacity)
            if (whole == 1)
                g.drawImageAt (headerPicture, 0, 0);
            else
                g.drawImage (headerPicture, juce::Rectangle<float> (0.0f, 0.0f, (float) playlist.headerWidth, (float) getHeight()), juce::RectanglePlacement::stretchToFit);
        }
        else
            paintHeader (g);
        // the lane
        g.setColour (simple ? colour::bg1 : colour::bg2);
        g.fillRect (playlist.headerWidth, 0, gridRight - playlist.headerWidth, getHeight());
        g.setColour (colour::border);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (playlist.headerWidth, 0, gridRight - playlist.headerWidth, getHeight());
        const auto sel = selected();
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const auto& s = list[(std::size_t) i];
            const auto r = blockOf (s);
            if (r.getRight() < (float) playlist.headerWidth || r.getX() > (float) gridRight)
                continue;
            const auto dragged = drag == Drag::move && s.uid == dragUid && moved;
            paintBlock (g, s, i, r, i == sel, hover.section == i && hover.part != Hit::none, dragged ? 0.35f : 1.0f);
        }
        if (list.empty() && simple)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::small));
            textcache::drawText (g, tr ("Click + to add a section"), playlist.headerWidth + 10, 0, 400, getHeight(), juce::Justification::centredLeft);
        }
        // a drag: where the section lands
        if (drag == Drag::move && moved)
            if (const auto index = sections::indexOfUid (list, dragUid); index >= 0)
            {
                const auto target = dropTarget();
                const auto copy = copyDrag;
                auto ghost = blockOf (list[(std::size_t) index]).withX ((float) (dragX - grabOffset));
                paintBlock (g, list[(std::size_t) index], index, ghost, true, false, 0.75f);
                g.setColour (copy ? colour::info : colour::accent);
                if (target.onto >= 0)
                    g.drawRoundedRectangle (blockOf (list[(std::size_t) target.onto]).reduced (0.5f), simple ? 6.0f : 2.0f, 2.0f);
                else if (target.before >= 0)
                {
                    const auto x = (float) playlist.tickToX ((double) boundaryTick (target.before));
                    g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) getHeight()));
                }
            }
        if (drag == Drag::resize)
            if (const auto index = sections::indexOfUid (list, dragUid); index >= 0)
            {
                const auto x = (float) playlist.tickToX ((double) resizeEnd);
                g.setColour (colour::accent);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, (float) getHeight()));
            }
    }

    void paintBlock (juce::Graphics& g, const SongSection& s, int index, juce::Rectangle<float> r, bool isSelected, bool hovered, float alpha) const
    {
        using namespace theme;
        // an own colour, else the layout's: GarageBand's greys in Simple (the selected marker light), FL's marker colour in Pattern
        const auto own = index >= 0 && index < (int) engine.getSong().sections().size() && s.colour != 0;
        const auto fill = own ? juce::Colour (s.colour)
                              : simpleLook() ? (isSelected ? juce::Colour (0xffb7b8bd) : juce::Colour (0xff47494f)) : colour::warning;
        const auto name = s.name.empty() ? tr ("Section") : juce::String::fromUTF8 (s.name.c_str());
        if (simpleLook())
        {
            // GarageBand's Arrangement track (Apple's guide, "Add arrangement markers"): flat blocks the lane's height side by
            // side, a dark line between two, the name centred; the selected one light grey
            const auto body = fill.withMultipliedBrightness (hovered && ! isSelected ? 1.12f : 1.0f).brighter (own && isSelected ? 0.35f : 0.0f);
            g.setColour (body.withMultipliedAlpha (alpha));
            g.fillRect (r);
            g.setColour (colour::bg0.withMultipliedAlpha (alpha));
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getY(), 1.0f, r.getHeight()));
            g.setColour (ui::simplelook::inkOn (body).withMultipliedAlpha (alpha));
            g.setFont (font (type::body, false));
            textcache::drawText (g, name, r.reduced (8.0f, 0.0f), juce::Justification::centred, true);
            if (hovered && r.getWidth() > 24.0f) // the right edge's grip
            {
                g.setColour (ui::simplelook::inkOn (fill).withAlpha (0.5f * alpha));
                g.fillRoundedRectangle (juce::Rectangle<float> (r.getRight() - 5.0f, r.getCentreY() - 7.0f, 2.0f, 14.0f), 1.0f);
            }
            return;
        }
        // the Pattern layout: the Playlist's marker tag (ruler paint: a small flag with the name, a line at its place) in the
        // section's colour, over the span tinted with it
        g.setColour (fill.withAlpha ((isSelected ? 0.34f : hovered ? 0.26f : 0.18f) * alpha));
        g.fillRect (r);
        g.setColour (fill.withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (r.getX(), 0.0f, 1.0f, (float) getHeight()));
        g.setFont (font (type::caption, true));
        // (round 190pb: the name's width from the text cache, the same number; JUCE shapes the text on every measure)
        const auto width = juce::jmin (r.getWidth(), (float) (int) std::ceil (textcache::stringWidth (g.getCurrentFont(), name)) + 8.0f);
        const auto tag = juce::Rectangle<float> (r.getX(), (float) getHeight() - 13.0f, width, 11.0f);
        g.fillRoundedRectangle (tag, 2.0f);
        g.setColour (colour::bg0.withMultipliedAlpha (alpha));
        textcache::drawText (g, name, tag.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
        if (isSelected)
        {
            g.setColour (colour::textPrimary.withMultipliedAlpha (alpha));
            g.drawRect (r, 1.0f);
        }
    }

    // --- dragging ------------------------------------------------------------------------------------------------------
    struct Target
    {
        int before = -1; // insert before this section (size(): after the last)
        int onto = -1;   // dropped onto this section (swap / replace)
    };
    std::int64_t boundaryTick (int before) const
    {
        const auto& list = engine.getSong().sections();
        if (list.empty())
            return 0;
        return before < (int) list.size() ? list[(std::size_t) before].start : list.back().end();
    }
    Target dropTarget() const
    {
        Target t;
        const auto& list = engine.getSong().sections();
        const auto index = sections::indexOfUid (list, dragUid);
        if (index < 0)
            return t;
        const auto tick = playlist.xToTick (dragX);
        if (const auto over = sections::indexAt (list, (std::int64_t) tick); over >= 0 && over != index)
        {
            const auto& s = list[(std::size_t) over];
            const auto quarter = (double) s.length / 4.0;
            if (tick >= (double) s.start + quarter && tick <= (double) s.end() - quarter)
            {
                t.onto = over;
                return t;
            }
        }
        // the nearest section edge
        double best = 1.0e300;
        for (int k = 0; k <= (int) list.size(); ++k)
            if (const auto d = std::abs ((double) boundaryTick (k) - tick); d < best)
            {
                best = d;
                t.before = k;
            }
        if (! copyDrag && (t.before == index || t.before == index + 1))
            t.before = -1; // (where it is already)
        return t;
    }

    static bool copyModifier (const juce::ModifierKeys& m)
    {
       #if JUCE_MAC
        return m.isAltDown(); // (macOS: Option-drag, as GarageBand)
       #else
        return flmods::ctrl (m); // (FL's Ctrl role, ui/FlMods.h)
       #endif
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        drag = Drag::none;
        moved = false;
        if (flmods::popup (e.mods))
        {
            if (h.section >= 0)
            {
                select (h.section);
                sectionMenu (h.section).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            }
            else
                laneMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        if (h.part == Hit::add)
        {
            addSection();
            return;
        }
        if (h.section < 0)
        {
            select (-1);
            return;
        }
        select (h.section);
        const auto& s = engine.getSong().sections()[(std::size_t) h.section];
        dragUid = s.uid;
        if (h.part == Hit::edge)
        {
            drag = Drag::resize;
            resizeEnd = s.end();
        }
        else
        {
            drag = Drag::move;
            dragX = e.x;
            grabOffset = e.x - (int) blockOf (s).getX();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::resize)
        {
            const auto index = sections::indexOfUid (engine.getSong().sections(), dragUid);
            if (index < 0)
                return;
            const auto& s = engine.getSong().sections()[(std::size_t) index];
            resizeEnd = std::max (sections::nearestBar (engine.getSong(), (std::int64_t) std::max (0.0, playlist.xToTick (e.x))),
                                  sections::barsAfter (engine.getSong(), s.start, 1));
            repaint();
            return;
        }
        if (drag != Drag::move)
            return;
        moved = moved || e.getDistanceFromDragStart() > 3;
        dragX = e.x;
        copyDrag = copyModifier (e.mods);
        replaceDrag = e.mods.isShiftDown();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto was = drag;
        drag = Drag::none;
        const auto index = sections::indexOfUid (engine.getSong().sections(), dragUid);
        if (was == Drag::resize && index >= 0)
        {
            engine.editSong ([index, end = resizeEnd] (Song& s) { sections::resize (s, index, end); });
        }
        else if (was == Drag::move && moved && index >= 0)
        {
            copyDrag = copyModifier (e.mods);
            replaceDrag = e.mods.isShiftDown();
            const auto target = dropTarget();
            const auto copy = copyDrag, replace = replaceDrag;
            if (target.onto >= 0)
                engine.editSong ([index, onto = target.onto, replace, copy] (Song& s) {
                    if (replace || copy)
                        sections::replace (s, index, onto);
                    else
                        sections::swap (s, index, onto);
                });
            else if (target.before >= 0)
                engine.editSong ([index, before = target.before, copy] (Song& s) {
                    if (copy)
                        sections::copy (s, index, before);
                    else
                        sections::move (s, index, before);
                });
            playlist.setSelection ({});
            playlist.sortClips();
        }
        moved = false;
        playlist.repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        if (h.section >= 0)
            rename (h.section);
        else if (h.part == Hit::lane)
            addSection();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = hitAt (e.getPosition());
        if (h.section != hover.section || h.part != hover.part)
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
        playlist.mouseWheelMove (e.getEventRelativeTo (&playlist), wheel); // (time scroll and zoom: the Playlist's)
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto index = selected();
        if (index < 0)
            return false;
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            // GarageBand: Delete empties the section; Delete again removes it and closes the gap
            const auto empty = sections::clipsIn (engine.getSong(), index).empty();
            engine.editSong ([index, empty] (Song& s) {
                if (empty)
                    sections::remove (s, index, true);
                else
                    sections::clear (s, index);
            });
            playlist.setSelection ({});
            if (empty)
                select (-1);
            playlist.repaint();
            return true;
        }
        if (key == juce::KeyPress::F2Key)
        {
            rename (index);
            return true;
        }
        return false;
    }

    // --- commands ------------------------------------------------------------------------------------------------------
    void addSection()
    {
        engine.editSong ([name = nextSectionName (engine.getSong().sections())] (Song& s) { sections::add (s, name); });
        select ((int) engine.getSong().sections().size() - 1);
        playlist.sectionsLaneChanged();
    }

    void rename (int index)
    {
        const auto& list = engine.getSong().sections();
        if (index < 0 || index >= (int) list.size())
            return;
        auto* window = new juce::AlertWindow (tr ("Rename section"), {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", juce::String::fromUTF8 (list[(std::size_t) index].name.c_str()));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, uid = list[(std::size_t) index].uid, window] (int result) {
                                     if (result != 1)
                                         return;
                                     const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                     setName (uid, name);
                                 }),
                                 true);
    }

    void setName (std::uint64_t uid, const std::string& name)
    {
        engine.editSong ([uid, name] (Song& s) {
            if (const auto i = sections::indexOfUid (s.sections(), uid); i >= 0)
                s.sections()[(std::size_t) i].name = name;
        });
        repaint();
    }

    juce::PopupMenu sectionMenu (int index)
    {
        juce::PopupMenu menu;
        const auto& list = engine.getSong().sections();
        if (index < 0 || index >= (int) list.size())
            return menu;
        const auto& s = list[(std::size_t) index];
        const auto uid = s.uid;
        menu.addSectionHeader (s.name.empty() ? tr ("Section") : juce::String::fromUTF8 (s.name.c_str()));
        {
            // GarageBand's name menu: its names, then Rename
            juce::PopupMenu names;
            for (int i = 0; i < (int) std::size (sections::presetNames); ++i)
            {
                const auto name = sectionPresetName (i);
                names.addItem (name, true, juce::String::fromUTF8 (s.name.c_str()) == name, [this, uid, n = name.toStdString()] { setName (uid, n); });
            }
            names.addSeparator();
            names.addItem (tr ("Rename..."), [this, index] { rename (index); });
            menu.addSubMenu (tr ("Name"), names);
        }
        menu.addItem (tr ("Rename...") + "\tF2", [this, index] { rename (index); });
        menu.addSubMenu (tr ("Change color"),
                         ui::colourMenu (s.colour, [this, uid] (std::uint32_t c) {
                             engine.editSong ([uid, c] (Song& song) {
                                 if (const auto i = sections::indexOfUid (song.sections(), uid); i >= 0)
                                     song.sections()[(std::size_t) i].colour = c;
                             });
                             repaint();
                         },
                                         juce::Rectangle<int> (juce::Desktop::getMousePosition(), juce::Desktop::getMousePosition()).withSizeKeepingCentre (1, 1)));
        menu.addSeparator();
        menu.addItem (tr ("Duplicate"), [this, index] {
            engine.editSong ([index] (Song& song) { sections::duplicate (song, index); });
            playlist.sortClips();
            playlist.repaint();
        });
        const auto hasClips = ! sections::clipsIn (engine.getSong(), index).empty();
        menu.addItem (tr ("Delete its clips") + "\tDel", hasClips, false, [this, index] {
            engine.editSong ([index] (Song& song) { sections::clear (song, index); });
            playlist.setSelection ({});
            playlist.repaint();
        });
        menu.addItem (tr ("Delete the section (keep its clips)"), [this, index] {
            engine.editSong ([index] (Song& song) { sections::remove (song, index, false); });
            select (-1);
            playlist.sectionsLaneChanged();
        });
        menu.addItem (tr ("Delete and close the gap"), [this, index] {
            engine.editSong ([index] (Song& song) { sections::remove (song, index, true); });
            playlist.setSelection ({});
            select (-1);
            playlist.sectionsLaneChanged();
        });
        menu.addSeparator();
        menu.addItem (tr ("Loop this section"), [this, from = s.start, to = s.end()] {
            engine.editSong ([from, to] (Song& song) {
                song.loopStart = from;
                song.loopEnd = to;
            }, false);
            playlist.repaint();
        });
        menu.addItem (tr ("Select its clips"), [this, index] {
            playlist.setSelection (sections::clipsIn (engine.getSong(), index));
            playlist.repaint();
        });
        return menu;
    }

    juce::PopupMenu laneMenu()
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        menu.addItem (tr ("Add a section at the end"), [this] { addSection(); });
        const auto plainMarkers = std::any_of (song.markers().begin(), song.markers().end(), [] (const Marker& m) { return sections::isPlainMarker (m); });
        menu.addItem (tr ("Sections from markers"), plainMarkers, false, [this] {
            engine.editSong ([] (Song& s) { sections::fromMarkers (s); });
            playlist.sectionsLaneChanged();
        });
        menu.addItem (tr ("Markers from sections"), ! song.sections().empty(), false, [this] {
            engine.editSong ([] (Song& s) { sections::toMarkers (s); });
            playlist.repaint();
        });
        menu.addItem (tr ("Create scenes from sections"), ! song.sections().empty(), false, [this] {
            engine.editSong ([] (Song& s) { sections::createScenes (s); });
        });
        menu.addSeparator();
        if (simpleLook())
            menu.addItem (tr ("Hide the Sections lane"), [this] { playlist.toggleSimpleSections(); });
        else
            menu.addItem (tr ("Sections lane") + " (LYNX)", true, true, [] { workflowextras::setExtra ("playlist.sectionsLane", false); });
        return menu;
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        const auto h = hitAt (p);
        const auto& list = engine.getSong().sections();
        if (h.part == Hit::add)
            return { tr ("Add a section of eight bars at the end of the last one"), {} };
        if (h.part == Hit::header)
            return { tr ("Sections: named parts of the song. Drag one to move it with everything under it"), {}, false, true };
        if (h.section >= 0 && h.section < (int) list.size())
        {
            const auto& s = list[(std::size_t) h.section];
            const auto first = engine.getSong().barAt (s.start).number;
            const auto bars = juce::String (first) + juce::String::fromUTF8 ("–") + juce::String (engine.getSong().barAt (std::max<std::int64_t> (0, s.end() - 1)).number);
            if (h.part == Hit::edge)
                return { tr ("Drag to resize the section (its clips stay where they are)"), bars };
            return { tr ("[LMB] drag to move it with its clips, Ctrl+drag a copy, drop onto another section to swap, Shift: replace it  [2xLMB] rename  [Del] delete its clips, again the section  [RMB] menu"),
                     juce::String::fromUTF8 (s.name.c_str()) + "  " + bars, false, true };
        }
        return { tr ("[2xLMB] add a section at the end  [RMB] menu"), {}, false, true };
    }

private:
    struct CacheKey
    {
        std::vector<SongSection> sections;
        std::uint64_t selected = 0;
        int hoverSection = -1, hoverPart = 0;
        double scrollTicks = 0.0, pixelsPerTick = 0.0;
        int width = 0, height = 0, headerWidth = 0;
        bool simple = false;
        float scale = 1.0f;
        juce::uint32 ground = 0, marker = 0;
        bool operator== (const CacheKey&) const = default;
    };
    AudioEngine& engine;
    PlaylistComponent& playlist;
    CacheKey cacheKey;
    juce::Image cache;
    juce::Image headerPicture; // (round 190pb: the header column, see paintLane)
    std::uint64_t headerKey = 0;
    bool cacheValid = false;
    enum class Drag { none, move, resize };
    Drag drag = Drag::none;
    std::uint64_t dragUid = 0, selectedUid = 0;
    int dragX = 0, grabOffset = 0;
    bool moved = false, copyDrag = false, replaceDrag = false;
    std::int64_t resizeEnd = 0;
    Hit hover;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SectionsLaneView)
};
} // namespace lynx
