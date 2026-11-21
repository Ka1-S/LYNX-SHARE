#include "ui/TrackerComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <unordered_map>

#include "core/HeldNotes.h"
#include "core/KeyMap.h"
#include "core/NoteEdit.h"
#include "core/FlSettings.h"
#include "core/NoteNames.h"
#include "engine/AudioEngine.h"
#include "ui/FrameTicker.h"
#include "ui/I18n.h"
#include "ui/ShortcutManager.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
using namespace tracker;

constexpr int toolbarHeight = 34, hintHeight = 38, headerHeight = 42, sidePanelWidth = 300, scrollBarSize = 12;

juce::Colour channelColour (const Song& song, int channel)
{
    if (channel >= 0 && channel < (int) song.channels.size() && song.channels[(std::size_t) channel].colour != 0)
        return juce::Colour (song.channels[(std::size_t) channel].colour);
    static const juce::uint32 palette[] = { 0xffe5383b, 0xfff5a742, 0xff4cc9c0, 0xff6ea8fe, 0xff9d7cf0, 0xff3ddc84, 0xffe86fb0, 0xffc5c9d1 };
    return juce::Colour (palette[(std::size_t) std::max (0, channel) % 8]);
}

int decimalDigits (std::int64_t v)
{
    int d = 1;
    while (v >= 10)
    {
        v /= 10;
        ++d;
    }
    return d;
}

// the hint bar's text for a field
juce::String fieldName (Field f)
{
    switch (f)
    {
        case Field::note: return tr ("Note");
        case Field::ins: return tr ("Ins");
        case Field::vol: return tr ("Vol");
        case Field::pan: return tr ("Pan");
        case Field::dly: return tr ("Dly");
        case Field::fxLetter:
        case Field::fxValue: return "Fx";
    }
    return {};
}
} // namespace

// ------------------------------------------------------------------------------------------------------------- Impl
struct TrackerComponent::Impl
{
    TrackerComponent& owner;
    AudioEngine& engine;
    int fixedPattern = -1;
    std::optional<std::vector<int>> onlyChannels;
    ShownColumns shown;
    HeldNotes preview;
    ValueEntry entry;
    bool gestureOpen = false;
    Clipboard clipboard;
    juce::String clipboardTextCopied;
    std::map<std::tuple<int, std::int64_t, int>, int> preferred; // (channel, start, key) -> the column it was typed in
    std::map<int, int> extraColumns;                             // channel of a track -> extra empty note columns
    bool editOn = false;
    int lastInstrument = -1;
    float lastVelocity = Note {}.velocity;
    // what the layout was built from (rebuilt only when it changes)
    std::vector<Note> builtNotes;
    std::int64_t builtLength = 0;
    std::vector<std::pair<std::string, std::uint32_t>> builtChannels;
    TrackerSettings builtSettings;
    int builtPattern = -2, builtSelected = -1;
    // geometry
    float cw = 7.0f; // the mono font's character width
    int rowHeight = 17;
    struct FieldGeom
    {
        Field field;
        int fxSlot = 0;
        int x = 0, width = 0, chars = 0, offset = 0; // (offset: in characters from the column's start)
    };
    struct ColumnGeom
    {
        int x = 0, width = 0;
        std::vector<FieldGeom> fields;
        // an empty cell and a sustained one drawn as one text each (the fields line up: a monospace font)
        juce::String emptyText, sustainText;
    };
    struct TrackGeom
    {
        int x = 0, width = 0;
        std::vector<ColumnGeom> columns;
    };
    std::vector<TrackGeom> geom;
    int gridWidth = 0, numberWidth = 40;
    // children
    juce::ComboBox patternBox, rpbBox;
    juce::Label patternLabel, rpbLabel, highlightLabel, octaveLabel, stepLabel;
    juce::TextButton octaveDown { "-" }, octaveUp { "+" }, stepDown { "-" }, stepUp { "+" };
    juce::TextButton hexButton, editButton, followButton, columnsButton;
    juce::ScrollBar vbar { true }, hbar { false };
    ui::FrameTicker frames;
    // the grid's texts drawn once as pictures (the cells repeat a few hundred texts: one picture blit instead of a glyph
    // fill per character, the paint bench's cost), per text, colour and pixel scale; emptied when the font changes
    struct Picture
    {
        juce::Image image;
        int width = 0;
    };
    std::unordered_map<juce::String, Picture> pictures;
    float picturesScale = 0.0f;
    juce::Font picturesFont { juce::FontOptions {} };

    void drawGridText (juce::Graphics& g, const juce::String& text, juce::Colour colour, int x, int y, int height)
    {
        if (text.isEmpty())
            return;
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto font = theme::monoFont (theme::type::small + 1.0f);
        if (scale != picturesScale || font != picturesFont || pictures.size() > 6000)
        {
            pictures.clear();
            picturesScale = scale;
            picturesFont = font;
        }
        const auto key = juce::String::toHexString ((juce::int64) colour.getARGB()) + juce::String::charToString ('|') + juce::String (height) + "|" + text;
        auto it = pictures.find (key);
        if (it == pictures.end())
        {
            Picture p;
            p.width = (int) std::ceil (cw * (float) text.length()) + 2;
            p.image = juce::Image (juce::Image::ARGB, std::max (1, (int) std::ceil ((float) p.width * scale)), std::max (1, (int) std::ceil ((float) height * scale)), true);
            juce::Graphics ig (p.image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            ig.setFont (font);
            ig.setColour (colour);
            ig.drawText (text, 0, 0, p.width, height, juce::Justification::centredLeft, false);
            it = pictures.emplace (key, std::move (p)).first;
        }
        if (! g.clipRegionIntersects ({ x, y, it->second.width, height }))
            return;
        g.setOpacity (1.0f); // (an image is drawn at the current colour's opacity: the picture holds its own colour)
        if (scale == 1.0f)
            g.drawImageAt (it->second.image, x, y);
        else
            g.drawImage (it->second.image, juce::Rectangle<float> ((float) x, (float) y, (float) it->second.width, (float) height));
    }

    Impl (TrackerComponent& o, AudioEngine& e)
        : owner (o), engine (e), frames (o, [this] (double) { return frameStep(); }, 60.0)
    {
        preview.setSender ([this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on); });
    }

    const Song& song() const { return engine.getSong(); }

    int patternIndex() const
    {
        const auto& s = song();
        const auto p = fixedPattern >= 0 ? fixedPattern : s.currentPattern;
        return p >= 0 && p < (int) s.patterns.size() ? p : -1;
    }

    BuildOptions buildOptions() const
    {
        const auto& s = song();
        BuildOptions o;
        o.rowsPerBeat = s.tracker.rowsPerBeat;
        resolveGroups (s, s.tracker, o);
        o.onlyChannels = onlyChannels;
        if (! onlyChannels && engine.getSelectedChannel() >= 0)
            o.alsoShow.push_back (engine.getSelectedChannel());
        for (const auto& [c, n] : extraColumns)
            o.extraColumns.push_back ({ c, n });
        o.preferredColumn = [this] (const Note& n) {
            const auto it = preferred.find ({ n.channel, n.start, n.key });
            return it != preferred.end() ? it->second : -1;
        };
        return o;
    }

    // rebuilds the layout when what it shows changed; true if it did
    bool refresh (bool force = false)
    {
        const auto& s = song();
        const auto p = patternIndex();
        std::vector<std::pair<std::string, std::uint32_t>> channels;
        channels.reserve (s.channels.size());
        for (const auto& c : s.channels)
            channels.emplace_back (c.name, c.colour | (c.muted ? 1u : 0u));
        const auto selected = onlyChannels ? -1 : engine.getSelectedChannel();
        const auto* pattern = p >= 0 ? &s.patterns[(std::size_t) p] : nullptr;
        const auto length = pattern != nullptr ? pattern->effectiveLength (s.meter()) : 0;
        if (! force && p == builtPattern && selected == builtSelected && channels == builtChannels && s.tracker == builtSettings && length == builtLength
            && (pattern == nullptr ? builtNotes.empty() : pattern->notes == builtNotes))
            return false;
        builtPattern = p;
        builtSelected = selected;
        builtChannels = std::move (channels);
        builtSettings = s.tracker;
        builtLength = length;
        builtNotes = pattern != nullptr ? pattern->notes : std::vector<Note> {};
        shown.pan = s.tracker.showPan;
        shown.dly = s.tracker.showDelay;
        shown.fx = s.tracker.showFx;
        owner.layout = p >= 0 ? build (s, p, buildOptions()) : Layout {};
        owner.cursor = clampCursor (owner.layout, shown, owner.cursor);
        computeGeometry();
        updateScrollBars();
        updateToolbar();
        return true;
    }

    void computeGeometry()
    {
        const auto font = theme::monoFont (theme::type::small + 1.0f);
        cw = std::max (5.0f, juce::GlyphArrangement::getStringWidth (font, "0000000000") / 10.0f);
        rowHeight = (int) std::ceil (font.getHeight() + 4.0f);
        const auto& l = owner.layout;
        numberWidth = (int) std::ceil (cw * (float) std::max (3, (hex ? 1 : 0) + decimalDigits (std::max<std::int64_t> (1, l.rows)))) + 12;
        // the widest note name of the app's note names (solfege: "Sol#")
        int noteChars = 3;
        for (int k = 0; k < 12; ++k)
            noteChars = std::max (noteChars, (int) notenames::pitchName (k).size() + 1);
        const auto insChars = hex ? std::max (2, (int) std::ceil (std::log ((double) song().channels.size() + 1.0) / std::log (16.0))) : 3;
        const auto valueChars = hex ? 2 : 3;
        const auto dlyChars = hex ? l.delayDigits : std::max (2, decimalDigits (l.rowTicks - 1));
        const auto fxChars = hex ? l.delayDigits : std::max (3, decimalDigits (l.rowTicks - 1));
        geom.clear();
        auto x = numberWidth;
        for (int t = 0; t < (int) l.tracks.size(); ++t)
        {
            TrackGeom tg;
            tg.x = x;
            auto cx = x + 6;
            const auto fields = fieldsOf (l, t, shown);
            for (int c = 0; c < l.tracks[(std::size_t) t].noteColumns; ++c)
            {
                ColumnGeom cg;
                cg.x = cx;
                int offset = 0; // in characters
                for (const auto& f : fields)
                {
                    FieldGeom fg;
                    fg.field = f.field;
                    fg.fxSlot = f.fxSlot;
                    fg.chars = f.field == Field::note ? noteChars : f.field == Field::ins ? insChars : f.field == Field::dly ? dlyChars
                               : f.field == Field::fxLetter ? 1 : f.field == Field::fxValue ? fxChars : valueChars;
                    fg.x = cx + (int) std::lround (cw * (float) offset);
                    fg.offset = offset;
                    fg.width = (int) std::ceil (cw * (float) fg.chars);
                    const auto dots = juce::String::repeatedString (f.field == Field::note ? "-" : ".", fg.chars);
                    cg.emptyText << dots;
                    cg.sustainText << (f.field == Field::note ? juce::String::repeatedString (" ", fg.chars) : dots);
                    // the fx letter sits right against its value; other fields have a space between them
                    const auto gap = f.field == Field::fxLetter ? 0 : 1;
                    offset += fg.chars + gap;
                    if (gap > 0)
                        cg.emptyText << " ", cg.sustainText << " ";
                    cg.fields.push_back (fg);
                }
                cg.emptyText = cg.emptyText.trimEnd();
                cg.sustainText = cg.sustainText.trimEnd();
                cg.width = (int) std::ceil (cw * (float) offset);
                cx += cg.width + (int) cw;
                tg.columns.push_back (cg);
            }
            const auto nameWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (theme::font (theme::type::small, true), juce::String::fromUTF8 (l.tracks[(std::size_t) t].name.c_str()).substring (0, 14))) + 48;
            tg.width = std::max (cx - x + 4, std::min (nameWidth + (l.tracks[(std::size_t) t].group ? 70 : 0), 240));
            x += tg.width;
            geom.push_back (std::move (tg));
        }
        gridWidth = x;
    }

    // ---------------------------------------------------------------------------------------- areas
    juce::Rectangle<int> toolbarArea() const { return owner.getLocalBounds().removeFromTop (toolbarHeight); }
    juce::Rectangle<int> hintArea() const { return owner.getLocalBounds().removeFromBottom (hintHeight); }
    bool sidePanel() const { return ! owner.compact && owner.getWidth() >= 900; }
    juce::Rectangle<int> sideArea() const
    {
        if (! sidePanel())
            return {};
        auto r = owner.getLocalBounds().withTrimmedTop (toolbarHeight).withTrimmedBottom (hintHeight);
        return r.removeFromRight (sidePanelWidth).reduced (6, 6);
    }
    juce::Rectangle<int> gridArea() const // header + rows, without the scroll bars
    {
        auto r = owner.getLocalBounds().withTrimmedTop (toolbarHeight).withTrimmedBottom (hintHeight);
        if (sidePanel())
            r.removeFromRight (sidePanelWidth);
        r.removeFromRight (scrollBarSize);
        r.removeFromBottom (scrollBarSize);
        return r;
    }
    juce::Rectangle<int> rowsArea() const { return gridArea().withTrimmedTop (headerHeight); }
    int visibleRows() const { return std::max (1, rowsArea().getHeight() / std::max (1, rowHeight)); }
    juce::Rectangle<int> rowRect (std::int64_t row) const
    {
        const auto area = rowsArea();
        return { area.getX(), area.getY() + (int) (row - owner.firstRow) * rowHeight, area.getWidth(), rowHeight };
    }

    void updateScrollBars()
    {
        const auto& l = owner.layout;
        const auto visible = visibleRows();
        owner.firstRow = std::clamp<std::int64_t> (owner.firstRow, 0, std::max<std::int64_t> (0, l.rows - visible));
        vbar.setRangeLimits (0.0, (double) std::max<std::int64_t> (l.rows, 1), juce::dontSendNotification);
        vbar.setCurrentRange ((double) owner.firstRow, (double) visible, juce::dontSendNotification);
        const auto width = rowsArea().getWidth();
        owner.scrollX = std::clamp (owner.scrollX, 0, std::max (0, gridWidth - width));
        hbar.setRangeLimits (0.0, (double) std::max (gridWidth, 1), juce::dontSendNotification);
        hbar.setCurrentRange ((double) owner.scrollX, (double) width, juce::dontSendNotification);
    }

    void updateToolbar()
    {
        const auto& s = song();
        if (! owner.compact)
        {
            patternBox.clear (juce::dontSendNotification);
            for (int p = 0; p < (int) s.patterns.size(); ++p)
                if (! s.patterns[(std::size_t) p].trackClip || p == patternIndex())
                    patternBox.addItem (juce::String (p + 1) + "  " + juce::String::fromUTF8 (s.patterns[(std::size_t) p].name.c_str()), p + 1);
            patternBox.setSelectedId (patternIndex() + 1, juce::dontSendNotification);
        }
        rpbBox.setSelectedId (s.tracker.rowsPerBeat, juce::dontSendNotification);
        highlightLabel.setText (tr ("Highlight {0} / {1}", { juce::String (owner.layout.rowsPerBeat), juce::String (owner.layout.rowsPerBar) }), juce::dontSendNotification);
        octaveLabel.setText (tr ("Octave {0}", { juce::String (octave) }), juce::dontSendNotification);
        stepLabel.setText (tr ("Step {0}", { juce::String (editStep) }), juce::dontSendNotification);
        hexButton.setButtonText (hex ? tr ("Hex") : tr ("Dec"));
        editButton.setToggleState (editOn, juce::dontSendNotification);
        followButton.setToggleState (follow, juce::dontSendNotification);
    }

    void settingsChanged()
    {
        updateToolbar();
        if (onSettingsChanged)
            onSettingsChanged();
    }

    // ---------------------------------------------------------------------------------------- hit testing
    struct Hit
    {
        bool header = false, cell = false;
        int track = -1, column = 0;
        Field field = Field::note;
        int fxSlot = 0;
        std::int64_t row = -1;
        bool mute = false, solo = false;
    };

    Hit hitAt (juce::Point<int> p) const
    {
        Hit h;
        const auto grid = gridArea();
        if (! grid.contains (p))
            return h;
        const auto x = p.x - grid.getX() + owner.scrollX;
        for (int t = 0; t < (int) geom.size(); ++t)
        {
            const auto& tg = geom[(std::size_t) t];
            if (x < tg.x || x >= tg.x + tg.width)
                continue;
            h.track = t;
            if (p.y < grid.getY() + headerHeight)
            {
                h.header = true;
                const auto right = grid.getX() + tg.x + tg.width - owner.scrollX;
                h.solo = p.y < grid.getY() + 22 && p.x >= right - 20;
                h.mute = p.y < grid.getY() + 22 && p.x >= right - 40 && p.x < right - 20;
                return h;
            }
            h.cell = true;
            h.row = owner.firstRow + (p.y - rowsArea().getY()) / std::max (1, rowHeight);
            // the nearest field
            int best = 1 << 30;
            for (int c = 0; c < (int) tg.columns.size(); ++c)
                for (const auto& f : tg.columns[(std::size_t) c].fields)
                {
                    const auto d = x < f.x ? f.x - x : x >= f.x + f.width ? x - (f.x + f.width) + 1 : 0;
                    if (d < best)
                    {
                        best = d;
                        h.column = c;
                        h.field = f.field;
                        h.fxSlot = f.fxSlot;
                    }
                }
            return h;
        }
        return h;
    }

    // ---------------------------------------------------------------------------------------- edits
    void edit (const std::function<bool (Pattern&)>& fn)
    {
        const auto p = patternIndex();
        if (p < 0)
            return;
        // a dry run on a copy: an edit that changes nothing makes no undo step
        auto copy = song().patterns[(std::size_t) p];
        if (! fn (copy))
            return;
        engine.editSong ([p, &copy] (Song& s) {
            if (p < (int) s.patterns.size())
                s.patterns[(std::size_t) p].notes = copy.notes;
        });
        refresh();
        owner.repaint();
    }

    void endValueEntry()
    {
        entry.reset();
        if (gestureOpen)
        {
            gestureOpen = false;
            engine.endUndoGesture();
        }
    }

    void advance()
    {
        moveCursorRows (editStep);
    }

    void moveCursorRows (std::int64_t delta, bool extend = false)
    {
        endValueEntry();
        const auto before = owner.cursor;
        owner.cursor.row = std::clamp<std::int64_t> (owner.cursor.row + delta, 0, std::max<std::int64_t> (0, owner.layout.rows - 1));
        cursorMoved (before, extend);
    }

    void cursorMoved (const Cursor& before, bool extend)
    {
        if (extend)
        {
            if (! owner.hasSelection)
            {
                owner.anchor = before;
                owner.hasSelection = true;
            }
        }
        else
            owner.hasSelection = false;
        ensureVisible();
        owner.repaint();
    }

    void ensureVisible()
    {
        const auto visible = visibleRows();
        if (owner.cursor.row < owner.firstRow)
            owner.firstRow = owner.cursor.row;
        else if (owner.cursor.row >= owner.firstRow + visible)
            owner.firstRow = owner.cursor.row - visible + 1;
        if (owner.cursor.track >= 0 && owner.cursor.track < (int) geom.size())
        {
            const auto& tg = geom[(std::size_t) owner.cursor.track];
            const auto width = rowsArea().getWidth() - numberWidth;
            if (tg.x - numberWidth < owner.scrollX)
                owner.scrollX = tg.x - numberWidth;
            else if (tg.x + tg.width - numberWidth > owner.scrollX + width)
                owner.scrollX = tg.x + tg.width - numberWidth - width;
        }
        updateScrollBars();
    }

    int channelForEntry() const
    {
        const auto& l = owner.layout;
        if (owner.cursor.track < 0 || owner.cursor.track >= (int) l.tracks.size())
            return -1;
        const auto& channels = l.tracks[(std::size_t) owner.cursor.track].channels;
        if (std::find (channels.begin(), channels.end(), lastInstrument) != channels.end())
            return lastInstrument;
        if (std::find (channels.begin(), channels.end(), engine.getSelectedChannel()) != channels.end())
            return engine.getSelectedChannel();
        return channels.empty() ? -1 : channels.front();
    }

    void typeKey (int key)
    {
        const auto channel = channelForEntry();
        if (channel < 0)
            return;
        auto c = owner.cursor;
        c.field = Field::note;
        int typed = -1;
        const auto& l = owner.layout;
        edit ([&] (Pattern& p) {
            typed = typeNote (p, l, c, key, channel, lastVelocity);
            if (typed >= 0)
            {
                const auto& n = p.notes[(std::size_t) typed];
                preferred[{ n.channel, n.start, n.key }] = c.column;
            }
            return typed >= 0;
        });
        lastInstrument = channel;
        preview.releaseAll();
        if (! engine.getTransport().playing.load())
            preview.press (channel, key, lastVelocity);
        advance();
    }

    void typeOffHere()
    {
        const auto c = owner.cursor;
        const auto& l = owner.layout;
        edit ([&] (Pattern& p) { return typeOff (p, l, c); });
        advance();
    }

    void typeDigit (int digit)
    {
        auto& c = owner.cursor;
        const auto& l = owner.layout;
        const auto cell = cellAt (l, c.track, c.column, c.row);
        if (cell.placed == nullptr || (cell.kind != Cell::Kind::start && ! (cell.kind == Cell::Kind::off && c.field == Field::dly)))
            return;
        std::optional<Fx> fx;
        if (c.field == Field::fxValue && cell.kind == Cell::Kind::start)
        {
            const auto list = shownFx (l, *cell.placed, song().patterns[(std::size_t) l.pattern].notes[(std::size_t) cell.placed->note]);
            if (c.fxSlot >= (int) list.size())
                return;
            fx = list[(std::size_t) c.fxSlot];
        }
        const auto maxDigits = hex ? (c.field == Field::dly || c.field == Field::fxValue ? l.delayDigits : 2)
                                   : decimalDigits (maxValue (l, c.field, fx));
        bool done = false;
        if (! gestureOpen)
        {
            gestureOpen = true;
            engine.beginUndoGesture();
        }
        const auto value = std::min (entry.add (digit, hex, maxDigits, done), maxValue (l, c.field, fx));
        const auto at = c;
        const auto channels = (int) song().channels.size();
        edit ([&] (Pattern& p) { return setField (p, l, at, value, channels); });
        if (c.field == Field::vol)
            lastVelocity = std::clamp (value, 0, 127) / 127.0f;
        if (done)
        {
            endValueEntry();
            advance();
        }
    }

    void typeFxLetter (Fx fx)
    {
        const auto c = owner.cursor;
        const auto& l = owner.layout;
        edit ([&] (Pattern& p) { return setFxLetter (p, l, c, fx); });
        // on to the value
        owner.cursor = moveHorizontal (l, shown, clampCursor (owner.layout, shown, c), 1);
        owner.repaint();
    }

    Selection selection() const
    {
        if (owner.hasSelection)
            return Selection::of (owner.anchor, owner.cursor);
        Selection s;
        s.track0 = s.track1 = owner.cursor.track;
        s.column0 = s.column1 = owner.cursor.column;
        s.row0 = s.row1 = owner.cursor.row;
        return s;
    }

    void copySelection (bool cut)
    {
        const auto p = patternIndex();
        if (p < 0)
            return;
        const auto s = selection();
        clipboard = tracker::copy (song().patterns[(std::size_t) p], owner.layout, s);
        clipboardTextCopied = juce::String::fromUTF8 (clipboardText (song(), owner.layout, s, hex).c_str());
        juce::SystemClipboard::copyTextToClipboard (clipboardTextCopied);
        if (cut)
        {
            const auto& l = owner.layout;
            edit ([&] (Pattern& pat) { return tracker::erase (pat, l, s); });
        }
    }

    void pasteHere()
    {
        auto clip = clipboard;
        const auto text = juce::SystemClipboard::getTextFromClipboard();
        if (text.isNotEmpty() && text != clipboardTextCopied) // copied from elsewhere (another tracker, a text): read it
            if (auto parsed = parseClipboardText (text.toStdString(), owner.layout.rowTicks))
                clip = *parsed;
        if (clip.empty())
            return;
        const auto at = owner.cursor;
        const auto& l = owner.layout;
        edit ([&] (Pattern& p) { return ! tracker::paste (p, l, at, clip).empty(); });
    }

    // ---------------------------------------------------------------------------------------- playback
    double patternTickNow() const
    {
        const auto& s = song();
        const auto p = patternIndex();
        if (p < 0 || ! engine.getTransport().playing.load())
            return -1.0;
        const auto length = s.patterns[(std::size_t) p].effectiveLength (s.meter());
        if (s.mode != PlayMode::song)
            return s.currentPattern == p ? std::fmod (engine.getTransport().positionTicks.load(), (double) std::max<std::int64_t> (1, length)) : -1.0;
        const auto at = engine.songPlayheadTicks();
        for (const auto& c : s.playlist)
            if (c.kind == ClipKind::pattern && c.pattern == p && at >= (double) c.start && at < (double) c.end())
                return patternTickInClip (c, at, length);
        return -1.0;
    }

    bool frameStep()
    {
        const auto row = rowAt (owner.layout, patternTickNow());
        if (row == owner.playRow)
            return false;
        const auto old = owner.playRow;
        owner.playRow = row;
        if (follow && row >= 0)
        {
            const auto visible = visibleRows();
            const auto wanted = std::clamp<std::int64_t> (row - visible / 2, 0, std::max<std::int64_t> (0, owner.layout.rows - visible));
            if (wanted != owner.firstRow)
            {
                owner.firstRow = wanted;
                updateScrollBars();
                owner.repaint (rowsArea());
                owner.lastRowsRepainted = visible;
                return true;
            }
        }
        // only the two rows that changed
        owner.lastRowsRepainted = 0;
        for (auto r : { old, row })
            if (r >= owner.firstRow && r < owner.firstRow + visibleRows())
            {
                owner.repaint (rowRect (r));
                ++owner.lastRowsRepainted;
            }
        return true;
    }

    juce::String hintText() const
    {
        const auto& l = owner.layout;
        const auto& c = owner.cursor;
        if (l.tracks.empty())
            return tr ("No notes in this pattern yet: select a channel in the Channel rack, turn Edit on and type notes");
        const auto beat = c.row / std::max (1, l.rowsPerBeat), bar = c.row / std::max (1, l.rowsPerBar);
        auto text = tr ("Row {0} · bar {1} beat {2} · {3}", { numberText ((int) c.row, 2, hex), juce::String (bar + 1),
                                                                 juce::String (beat % std::max (1, l.rowsPerBar / std::max (1, l.rowsPerBeat)) + 1),
                                                                 juce::String::fromUTF8 (l.tracks[(std::size_t) c.track].name.c_str()) })
                    + " · " + fieldName (c.field) + ": ";
        const auto cell = cellAt (l, c.track, c.column, c.row);
        switch (c.field)
        {
            case Field::note: text << (cell.kind == Cell::Kind::off ? tr ("OFF ends the note above here") : tr ("the key; 1 = OFF (the note above ends here)")); break;
            case Field::ins: text << tr ("the rack channel of the note (a track group)"); break;
            case Field::vol: text << tr ("velocity 00 to 7F"); break;
            case Field::pan: text << tr ("pan 00 left, 40 centre, 80 right"); break;
            case Field::dly: text << tr ("ticks into the row (exact)"); break;
            case Field::fxLetter:
            case Field::fxValue:
            {
                std::optional<Fx> fx;
                if (cell.kind == Cell::Kind::start)
                {
                    const auto list = shownFx (l, *cell.placed, song().patterns[(std::size_t) l.pattern].notes[(std::size_t) cell.placed->note]);
                    if (c.fxSlot < (int) list.size())
                        fx = list[(std::size_t) c.fxSlot];
                }
                text << (fx ? tr (fxDescription (*fx)) : tr ("type G P Y R T M for a note field"));
                break;
            }
        }
        return text;
    }
};

// ------------------------------------------------------------------------------------------------- TrackerComponent
TrackerComponent::TrackerComponent (AudioEngine& e, bool compactIn) : engine (e), compact (compactIn)
{
    impl = std::make_unique<Impl> (*this, e);
    auto& i = *impl;
    setWantsKeyboardFocus (true);
    setComponentID (compact ? "simple.dock.tracker" : "tracker");

    auto setupLabel = [this] (juce::Label& l) {
        l.setFont (theme::font (theme::type::small));
        l.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };
    if (! compact)
    {
        i.patternLabel.setText (tr ("Pattern"), juce::dontSendNotification);
        setupLabel (i.patternLabel);
        i.patternBox.onChange = [this] {
            const auto id = impl->patternBox.getSelectedId();
            if (id <= 0)
                return;
            if (impl->fixedPattern >= 0)
                impl->fixedPattern = id - 1;
            else
                engine.editSong ([p = id - 1] (Song& s) { s.currentPattern = p; }, false);
            impl->refresh (true);
            repaint();
        };
        i.patternBox.setTooltip (tr ("The pattern the tracker shows (the Piano roll's notes)"));
        addAndMakeVisible (i.patternBox);
    }
    i.rpbLabel.setText (tr ("Rows/beat"), juce::dontSendNotification);
    setupLabel (i.rpbLabel);
    for (int r = TrackerSettings::minRowsPerBeat; r <= TrackerSettings::maxRowsPerBeat; ++r)
        i.rpbBox.addItem (juce::String (r), r);
    i.rpbBox.setTooltip (tr ("Rows per beat: how fine the grid is (the notes stay where they are)"));
    i.rpbBox.onChange = [this] {
        const auto r = impl->rpbBox.getSelectedId();
        if (r > 0 && r != engine.getSong().tracker.rowsPerBeat)
            engine.editSong ([r] (Song& s) { s.tracker.rowsPerBeat = r; });
    };
    addAndMakeVisible (i.rpbBox);
    setupLabel (i.highlightLabel);
    setupLabel (i.octaveLabel);
    setupLabel (i.stepLabel);
    for (auto* b : { &i.octaveDown, &i.octaveUp, &i.stepDown, &i.stepUp })
        addAndMakeVisible (*b);
    i.octaveDown.onClick = [this] { octave = std::max (minOctave, octave - 1); impl->settingsChanged(); };
    i.octaveUp.onClick = [this] { octave = std::min (maxOctave, octave + 1); impl->settingsChanged(); };
    i.stepDown.onClick = [this] { editStep = std::max (0, editStep - 1); impl->settingsChanged(); };
    i.stepUp.onClick = [this] { editStep = std::min (maxEditStep, editStep + 1); impl->settingsChanged(); };
    i.octaveDown.setTooltip (tr ("Octave down (Numpad /)"));
    i.octaveUp.setTooltip (tr ("Octave up (Numpad *)"));
    i.stepDown.setTooltip (tr ("Edit step down (Ctrl+Numpad /): rows the cursor moves after an entry"));
    i.stepUp.setTooltip (tr ("Edit step up (Ctrl+Numpad *)"));
    i.hexButton.setTooltip (tr ("Show the values in hexadecimal or decimal"));
    i.hexButton.onClick = [this] {
        hex = ! hex;
        impl->computeGeometry();
        impl->updateScrollBars();
        impl->settingsChanged();
        repaint();
    };
    addAndMakeVisible (i.hexButton);
    i.editButton.setButtonText (tr ("Edit"));
    i.editButton.setClickingTogglesState (true);
    i.editButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::accent);
    i.editButton.setTooltip (tr ("Edit (Ctrl+E here): the typing keys write notes at the cursor; off, they only play"));
    i.editButton.onClick = [this] {
        impl->editOn = impl->editButton.getToggleState();
        impl->endValueEntry();
        grabKeyboardFocus();
        repaint();
    };
    addAndMakeVisible (i.editButton);
    i.followButton.setButtonText (tr ("Follow"));
    i.followButton.setClickingTogglesState (true);
    i.followButton.setColour (juce::TextButton::buttonOnColourId, theme::colour::bg6);
    i.followButton.setTooltip (tr ("The rows follow the playhead while the pattern plays"));
    i.followButton.onClick = [this] {
        follow = impl->followButton.getToggleState();
        impl->settingsChanged();
    };
    addAndMakeVisible (i.followButton);
    i.columnsButton.setButtonText (tr ("Columns"));
    i.columnsButton.setTooltip (tr ("The columns every track shows"));
    i.columnsButton.onClick = [this] {
        juce::PopupMenu m;
        const auto& t = engine.getSong().tracker;
        m.addItem (tr ("Pan"), true, t.showPan, [this] { engine.editSong ([] (Song& s) { s.tracker.showPan = ! s.tracker.showPan; }); });
        m.addItem (tr ("Delay"), true, t.showDelay, [this] { engine.editSong ([] (Song& s) { s.tracker.showDelay = ! s.tracker.showDelay; }); });
        m.addItem (tr ("Effects"), true, t.showFx, [this] { engine.editSong ([] (Song& s) { s.tracker.showFx = ! s.tracker.showFx; }); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&impl->columnsButton));
    };
    addAndMakeVisible (i.columnsButton);
    i.vbar.addListener (this);
    i.hbar.addListener (this);
    i.vbar.setAutoHide (false);
    i.hbar.setAutoHide (false);
    addAndMakeVisible (i.vbar);
    addAndMakeVisible (i.hbar);
    engine.addChangeListener (this);
    i.refresh (true);
    setSize (compact ? 800 : 1180, compact ? 240 : 680);
}

TrackerComponent::~TrackerComponent()
{
    engine.removeChangeListener (this);
    impl->endValueEntry();
    impl->preview.releaseAll();
}

void TrackerComponent::showPattern (int pattern)
{
    impl->fixedPattern = pattern;
    impl->refresh (true);
    repaint();
}

int TrackerComponent::shownPattern() const noexcept { return impl->patternIndex(); }

void TrackerComponent::setOnlyChannels (std::optional<std::vector<int>> channels)
{
    impl->onlyChannels = std::move (channels);
    impl->refresh (true);
    repaint();
}

void TrackerComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (impl->refresh())
        repaint();
}

void TrackerComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &impl->vbar)
        firstRow = (std::int64_t) std::llround (start);
    else
        scrollX = (int) std::lround (start);
    repaint();
}

void TrackerComponent::resized()
{
    auto& i = *impl;
    auto top = getLocalBounds().removeFromTop (toolbarHeight).reduced (6, 4);
    auto place = [&top] (juce::Component& c, int w) {
        c.setBounds (top.removeFromLeft (w));
        top.removeFromLeft (4);
    };
    if (! compact)
    {
        place (i.patternLabel, theme::labelColumnWidth ({ &i.patternLabel }, 50, 90));
        place (i.patternBox, 170);
    }
    place (i.rpbLabel, theme::labelColumnWidth ({ &i.rpbLabel }, 64, 110));
    place (i.rpbBox, 54);
    place (i.highlightLabel, theme::labelColumnWidth ({ &i.highlightLabel }, 90, 150));
    // (round 190oj WG-14: 12 px between the groups, so "[−] Octave 4 [+]" and "[−] Step 1 [+]" read as pairs — with 4 px
    // everywhere the Octave "−" seemed to belong to Highlight)
    top.removeFromLeft (8);
    place (i.octaveDown, 22);
    place (i.octaveLabel, theme::labelColumnWidth ({ &i.octaveLabel }, 62, 110));
    place (i.octaveUp, 22);
    top.removeFromLeft (8);
    place (i.stepDown, 22);
    place (i.stepLabel, theme::labelColumnWidth ({ &i.stepLabel }, 52, 100));
    place (i.stepUp, 22);
    top.removeFromLeft (8);
    place (i.hexButton, theme::buttonWidth (i.hexButton, 46, 80));
    place (i.editButton, theme::buttonWidth (i.editButton, 56, 110));
    place (i.followButton, theme::buttonWidth (i.followButton, 62, 120));
    place (i.columnsButton, theme::buttonWidth (i.columnsButton, 74, 140));
    const auto grid = i.gridArea();
    i.vbar.setBounds (grid.getRight(), grid.getY(), scrollBarSize, grid.getHeight());
    i.hbar.setBounds (grid.getX(), grid.getBottom(), grid.getWidth(), scrollBarSize);
    i.computeGeometry();
    i.updateScrollBars();
    i.updateToolbar();
}

// ------------------------------------------------------------------------------------------------------------ paint
void TrackerComponent::paint (juce::Graphics& g)
{
    using namespace theme;
    auto& i = *impl;
    const auto& s = engine.getSong();
    const auto& l = layout;
    g.fillAll (colour::bg1);
    g.setColour (colour::bg3);
    g.fillRect (i.toolbarArea());

    const auto grid = i.gridArea();
    const auto rows = i.rowsArea();
    g.setColour (colour::bg0);
    g.fillRect (grid);

    const auto pattern = i.patternIndex();
    const auto* notes = pattern >= 0 ? &s.patterns[(std::size_t) pattern].notes : nullptr;
    const auto mono = monoFont (type::small + 1.0f);
    const auto visible = i.visibleRows();
    const auto lastRow = std::min (l.rows, firstRow + visible + 1);
    const auto sel = i.selection();
    const auto hasSel = hasSelection;

    // ---- rows: backgrounds (bars, beats), the playhead and the cursor rows
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (rows);
        for (auto r = firstRow; r < lastRow; ++r)
        {
            const auto rr = i.rowRect (r);
            if (! g.clipRegionIntersects (rr))
                continue;
            auto bg = r % std::max (1, l.rowsPerBar) == 0 ? colour::bg3 : r % std::max (1, l.rowsPerBeat) == 0 ? colour::bg2 : colour::bg0;
            if (r == playRow)
                bg = colour::accentMuted;
            g.setColour (bg);
            g.fillRect (rr);
            if (r == cursor.row && r != playRow)
            {
                g.setColour (colour::textPrimary.withAlpha (0.05f));
                g.fillRect (rr);
            }
            // the row number
            i.drawGridText (g, numberText ((int) r, std::max (2, hex ? 2 : decimalDigits (std::max<std::int64_t> (1, l.rows - 1))), hex),
                            r == playRow ? colour::textPrimary : r % std::max (1, l.rowsPerBeat) == 0 ? colour::textSecondary : colour::textDisabled,
                            rr.getX() + 6, rr.getY(), rr.getHeight());
        }
        g.setColour (colour::border);
        g.drawVerticalLine (rows.getX() + i.numberWidth - 2, (float) rows.getY(), (float) rows.getBottom());

        // ---- the cells
        g.reduceClipRegion (rows.withTrimmedLeft (i.numberWidth - 1));
        const auto ox = grid.getX() - scrollX;
        for (int t = 0; t < (int) l.tracks.size() && t < (int) i.geom.size(); ++t)
        {
            const auto& tg = i.geom[(std::size_t) t];
            if (ox + tg.x > rows.getRight() || ox + tg.x + tg.width < rows.getX() + i.numberWidth)
                continue;
            const auto& track = l.tracks[(std::size_t) t];
            const auto trackColour = channelColour (s, track.channels.empty() ? 0 : track.channels.front());
            g.setColour (colour::border);
            g.drawVerticalLine (ox + tg.x + tg.width - 1, (float) rows.getY(), (float) rows.getBottom());
            for (int c = 0; c < (int) tg.columns.size(); ++c)
            {
                const auto& cg = tg.columns[(std::size_t) c];
                for (auto r = firstRow; r < lastRow; ++r)
                {
                    const auto rr = i.rowRect (r);
                    if (! g.clipRegionIntersects (rr.withX (ox + cg.x - 2).withWidth (cg.width + 10)))
                        continue; // (a playhead step repaints two rows: the others are not even looked at)
                    const auto cell = cellAt (l, t, c, r);
                    const auto selected = hasSel && sel.contains (l, t, c, r);
                    if (selected)
                    {
                        g.setColour (colour::selectionFill);
                        g.fillRect (juce::Rectangle<int> (ox + cg.x - 2, rr.getY(), cg.width + 2, rr.getHeight()));
                    }
                    const Note* n = cell.kind == Cell::Kind::start && notes != nullptr ? &(*notes)[(std::size_t) cell.placed->note] : nullptr;
                    std::vector<Fx> fxList;
                    if (n != nullptr)
                        fxList = shownFx (l, *cell.placed, *n);
                    if (cell.kind == Cell::Kind::sustain)
                    {
                        // the sustain line, in the track's colour
                        g.setColour (trackColour.withAlpha (0.7f));
                        g.fillRect (ox + cg.x + 2, rr.getY(), 2, rr.getHeight());
                    }
                    const auto cursorHere = r == cursor.row && t == cursor.track && c == cursor.column;
                    if ((cell.kind == Cell::Kind::empty || cell.kind == Cell::Kind::sustain) && ! cursorHere)
                    {
                        // one text for the whole cell (most cells): the dim placeholders
                        g.setColour (colour::textDisabled.withAlpha (0.55f));
                        i.drawGridText (g, cell.kind == Cell::Kind::empty ? cg.emptyText : cg.sustainText, colour::textDisabled.withAlpha (0.55f), ox + cg.x,
                                        rr.getY(), rr.getHeight());
                        continue;
                    }
                    juce::String dimRun;
                    for (const auto& f : cg.fields)
                    {
                        const juce::Rectangle<int> box (ox + f.x, rr.getY(), f.width + 2, rr.getHeight());
                        juce::String text;
                        auto col = colour::textDisabled.withAlpha (0.55f);
                        switch (f.field)
                        {
                            case Field::note:
                                if (n != nullptr)
                                {
                                    text = juce::String::fromUTF8 (keyText (n->key).c_str());
                                    col = n->muted ? colour::textDisabled : colour::textPrimary;
                                }
                                else if (cell.kind == Cell::Kind::off)
                                {
                                    text = "OFF";
                                    col = colour::accent;
                                }
                                else if (cell.kind == Cell::Kind::sustain)
                                    text.clear();
                                else
                                    text = juce::String::repeatedString ("-", f.chars);
                                break;
                            case Field::ins:
                                text = n != nullptr ? juce::String (numberText (n->channel + 1, f.chars, hex)) : juce::String::repeatedString (".", f.chars);
                                if (n != nullptr)
                                    col = colour::info;
                                break;
                            case Field::vol:
                                text = n != nullptr ? juce::String (numberText ((int) std::lround (n->velocity * 127.0f), f.chars, hex)) : juce::String::repeatedString (".", f.chars);
                                if (n != nullptr)
                                    col = colour::textSecondary;
                                break;
                            case Field::pan:
                                text = n != nullptr ? juce::String (numberText ((int) std::lround ((n->pan + 1.0f) * 64.0f), f.chars, hex)) : juce::String::repeatedString (".", f.chars);
                                if (n != nullptr)
                                    col = colour::textSecondary;
                                break;
                            case Field::dly:
                            {
                                const auto d = n != nullptr ? cell.placed->startDelay : cell.kind == Cell::Kind::off ? cell.placed->offDelay : 0;
                                text = d > 0 ? juce::String (numberText (d, f.chars, hex)) : juce::String::repeatedString (".", f.chars);
                                if (d > 0)
                                    col = colour::info;
                                break;
                            }
                            case Field::fxLetter:
                                if (f.fxSlot < (int) fxList.size())
                                {
                                    text = juce::String::charToString ((juce::juce_wchar) fxLetter (fxList[(std::size_t) f.fxSlot]));
                                    col = colour::warning;
                                }
                                else
                                    text = ".";
                                break;
                            case Field::fxValue:
                                if (f.fxSlot < (int) fxList.size())
                                {
                                    text = numberText (fxValueOf (*n, fxList[(std::size_t) f.fxSlot], n->length), f.chars, hex);
                                    col = colour::warning;
                                }
                                else
                                    text = juce::String::repeatedString (".", f.chars);
                                break;
                        }
                        const auto placeholder = text.isNotEmpty() && (text.containsOnly (".") || text.containsOnly ("-"));
                        if (placeholder) // (the dim placeholders of the cell go out as one text, below)
                        {
                            while (dimRun.length() < f.offset)
                                dimRun << " ";
                            dimRun << text;
                        }
                        else if (text.isNotEmpty())
                            i.drawGridText (g, text, col, box.getX(), box.getY(), box.getHeight());
                        // the cursor
                        if (r == cursor.row && t == cursor.track && c == cursor.column && f.field == cursor.field
                            && (f.field != Field::fxLetter && f.field != Field::fxValue ? true : f.fxSlot == cursor.fxSlot))
                        {
                            g.setColour (i.editOn ? colour::accent : colour::textPrimary.withAlpha (0.8f));
                            g.drawRect (box.expanded (1, 0), 1);
                        }
                    }
                    if (dimRun.isNotEmpty())
                    {
                        i.drawGridText (g, dimRun, colour::textDisabled.withAlpha (0.55f), ox + cg.x, rr.getY(), rr.getHeight());
                    }
                }
            }
        }
    }

    // ---- the track headers: a colour bar, the name, M S, the field captions
    {
        juce::Graphics::ScopedSaveState state (g);
        const auto header = grid.withHeight (headerHeight);
        g.setColour (colour::bg2);
        g.fillRect (header);
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption));
        textcache::drawText (g, tr ("Row"), juce::Rectangle<int> (header.getX() + 6, header.getBottom() - 18, i.numberWidth, 16), juce::Justification::centredLeft, false);
        g.reduceClipRegion (header.withTrimmedLeft (i.numberWidth - 1));
        const auto ox = grid.getX() - scrollX;
        for (int t = 0; t < (int) l.tracks.size() && t < (int) i.geom.size(); ++t)
        {
            const auto& tg = i.geom[(std::size_t) t];
            const auto& track = l.tracks[(std::size_t) t];
            const juce::Rectangle<int> box (ox + tg.x, header.getY(), tg.width - 1, headerHeight);
            if (! g.clipRegionIntersects (box))
                continue;
            const auto first = track.channels.empty() ? 0 : track.channels.front();
            g.setColour (channelColour (s, first));
            g.fillRect (box.withHeight (3));
            g.setColour (colour::textPrimary);
            g.setFont (font (type::small, true));
            auto name = juce::String::fromUTF8 (track.name.c_str());
            if (track.group)
                name << " · " << tr ("group of {0}", { juce::String ((int) track.channels.size()) });
            textcache::drawText (g, name, juce::Rectangle<int> (box.getX() + 6, box.getY() + 4, box.getWidth() - 50, 18), juce::Justification::centredLeft, true);
            // M S
            bool muted = true, solo = true;
            for (auto ch : track.channels)
                muted = muted && ch < (int) s.channels.size() && s.channels[(std::size_t) ch].muted;
            for (int ch = 0; ch < (int) s.channels.size(); ++ch)
            {
                const auto inTrack = std::find (track.channels.begin(), track.channels.end(), ch) != track.channels.end();
                solo = solo && (inTrack ? ! s.channels[(std::size_t) ch].muted : s.channels[(std::size_t) ch].muted);
            }
            solo = solo && s.channels.size() > track.channels.size();
            g.setFont (font (type::caption, true));
            g.setColour (muted ? colour::warning : colour::textDisabled);
            textcache::drawText (g, "M", juce::Rectangle<int> (box.getRight() - 40, box.getY() + 4, 18, 18), juce::Justification::centred, false);
            g.setColour (solo ? colour::ledOn : colour::textDisabled);
            textcache::drawText (g, "S", juce::Rectangle<int> (box.getRight() - 20, box.getY() + 4, 18, 18), juce::Justification::centred, false);
            // the field captions of each note column
            g.setFont (font (type::caption));
            g.setColour (colour::textSecondary);
            for (const auto& cg : tg.columns)
                for (const auto& f : cg.fields)
                {
                    if (f.field == Field::fxValue)
                        continue;
                    const auto caption = f.field == Field::fxLetter ? juce::String ("Fx") : fieldName (f.field);
                    textcache::drawText (g, caption, juce::Rectangle<int> (ox + f.x, header.getBottom() - 18, f.width + (f.field == Field::fxLetter ? 20 : 4), 16),
                                         juce::Justification::centredLeft, false);
                }
            g.setColour (colour::border);
            g.drawVerticalLine (box.getRight(), (float) header.getY(), (float) header.getBottom());
        }
        g.setColour (colour::borderStrong);
        g.drawHorizontalLine (header.getBottom() - 1, (float) header.getX(), (float) header.getRight());
    }

    if (l.tracks.empty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (font (type::body));
        g.drawFittedText (pattern < 0 ? tr ("No pattern") : tr ("This pattern has no notes yet. Select a channel, turn Edit on and type notes with the typing keys."),
                          rows.reduced (30), juce::Justification::centred, 3);
    }

    // ---- the side panel: the Sequence (the Playlist's order, read only) and the legend
    if (i.sidePanel())
    {
        auto side = i.sideArea();
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (side.toFloat(), radius::md);
        side.reduce (10, 8);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small, true));
        textcache::drawText (g, tr ("Sequence (the Playlist, read only)"), side.removeFromTop (22), juce::Justification::centredLeft, true);
        std::vector<const Clip*> clips;
        for (const auto& c : s.playlist)
            if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) s.patterns.size())
                clips.push_back (&c);
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip* a, const Clip* b) { return a->start != b->start ? a->start < b->start : a->track < b->track; });
        const auto listRows = std::min<int> ((int) clips.size(), 9);
        g.setFont (monoFont (type::small));
        for (int k = 0; k < listRows; ++k)
        {
            const auto* c = clips[(std::size_t) k];
            auto row = side.removeFromTop (22);
            if (c->pattern == pattern)
            {
                g.setColour (colour::accentMuted);
                g.fillRoundedRectangle (row.toFloat(), radius::sm);
            }
            g.setColour (colour::textSecondary);
            textcache::drawText (g, numberText (k, 2, hex), row.removeFromLeft (30).withTrimmedLeft (4), juce::Justification::centredLeft, false);
            g.setColour (colour::textPrimary);
            const auto barsValue = (double) c->length / (double) std::max<std::int64_t> (1, s.barTicks());
            const auto bars = std::abs (barsValue - std::round (barsValue)) < 1.0e-9 ? juce::String ((int) std::lround (barsValue)) : juce::String (barsValue, 2);
            textcache::drawText (g, tr ("{0} bars", { bars }), row.removeFromRight (70).withTrimmedRight (6), juce::Justification::centredRight, false);
            textcache::drawText (g, juce::String::fromUTF8 (s.patterns[(std::size_t) c->pattern].name.c_str()), row, juce::Justification::centredLeft, true);
        }
        if ((int) clips.size() > listRows)
        {
            g.setColour (colour::textDisabled);
            textcache::drawText (g, tr ("... {0} more", { juce::String ((int) clips.size() - listRows) }), side.removeFromTop (18), juce::Justification::centredLeft, false);
        }
        g.setFont (font (type::caption));
        g.setColour (colour::textDisabled);
        textcache::drawText (g, tr ("A pattern placed again is the same pattern (no copy)."), side.removeFromTop (20), juce::Justification::centredLeft, true);
        side.removeFromTop (8);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small, true));
        textcache::drawText (g, tr ("Fx column (the notes' own fields)"), side.removeFromTop (22), juce::Justification::centredLeft, true);
        g.setFont (font (type::caption));
        for (int k = 0; k < fxKinds && side.getHeight() > 18; ++k)
        {
            g.setColour (colour::textSecondary);
            textcache::drawText (g, tr (fxDescription ((Fx) k)), side.removeFromTop (18), juce::Justification::centredLeft, true);
        }
        side.removeFromTop (6);
        g.setColour (colour::textDisabled);
        g.drawFittedText (tr ("Notes that overlap get extra note columns. A line runs down from a note while it sounds; OFF is where it ends."),
                          side.removeFromTop (48), juce::Justification::topLeft, 3);
    }

    // ---- the hint bar
    {
        auto hint = i.hintArea();
        g.setColour (colour::bg3);
        g.fillRect (hint);
        hint.reduce (10, 2);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small));
        textcache::drawText (g, i.hintText(), hint.removeFromTop (17), juce::Justification::centredLeft, true);
        g.setColour (colour::textDisabled);
        g.setFont (font (type::caption));
        textcache::drawText (g, tr ("Keys: Z-M / Q-P notes (the typing keyboard), 1 OFF, 0-9 A-F values, Del clear, Insert / Backspace rows, Tab next track, Ctrl+Up/Down transpose, Space play"),
                             hint, juce::Justification::centredLeft, true);
    }
}

// ------------------------------------------------------------------------------------------------------------- keys
bool TrackerComponent::keyPressed (const juce::KeyPress& key)
{
    auto& i = *impl;
    const auto& l = layout;
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    const auto ctrl = mods.isCommandDown() || mods.isCtrlDown();
    const auto shift = mods.isShiftDown();
    const auto alt = mods.isAltDown();
    const auto before = cursor;

    // the editor's own toggles
    if (ctrl && ! alt && (code == 'E' || code == 'e'))
    {
        i.editOn = ! i.editOn;
        i.updateToolbar();
        i.endValueEntry();
        repaint();
        return true;
    }
    if (code == juce::KeyPress::numberPadMultiply || code == juce::KeyPress::numberPadDivide)
    {
        const auto up = code == juce::KeyPress::numberPadMultiply;
        if (ctrl)
            editStep = std::clamp (editStep + (up ? 1 : -1), 0, maxEditStep);
        else
            octave = std::clamp (octave + (up ? 1 : -1), minOctave, maxOctave);
        i.settingsChanged();
        return true;
    }
    if (l.tracks.empty())
        return false;

    // moving
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const auto dir = code == juce::KeyPress::upKey ? -1 : 1;
        if (ctrl) // transpose the selection (or the note at the cursor): a semitone, Shift an octave
        {
            const auto s = i.selection();
            i.edit ([&] (Pattern& p) { return tracker::transpose (p, l, s, -dir * (shift ? 12 : 1)); });
            return true;
        }
        i.moveCursorRows (dir, shift);
        return true;
    }
    if (code == juce::KeyPress::pageUpKey || code == juce::KeyPress::pageDownKey)
    {
        i.moveCursorRows ((code == juce::KeyPress::pageUpKey ? -1 : 1) * std::max (1, l.rowsPerBar), shift);
        return true;
    }
    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::endKey)
    {
        i.moveCursorRows (code == juce::KeyPress::homeKey ? -cursor.row : l.rows - 1 - cursor.row, shift);
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        i.endValueEntry();
        if (alt) // a note column more / less on the cursor's track (for chords)
        {
            const auto& track = l.tracks[(std::size_t) cursor.track];
            auto& extra = i.extraColumns[track.channels.front()];
            extra = std::max (0, extra + (code == juce::KeyPress::rightKey ? 1 : -1));
            i.refresh (true);
            repaint();
            return true;
        }
        cursor = moveHorizontal (l, i.shown, cursor, code == juce::KeyPress::rightKey ? 1 : -1);
        i.cursorMoved (before, shift);
        return true;
    }
    if (code == juce::KeyPress::tabKey)
    {
        i.endValueEntry();
        cursor.track = std::clamp (cursor.track + (shift ? -1 : 1), 0, (int) l.tracks.size() - 1);
        cursor.column = 0;
        cursor.field = Field::note;
        i.cursorMoved (before, false);
        return true;
    }
    if (ctrl && (code == 'A' || code == 'a'))
    {
        anchor = cursor;
        anchor.track = 0, anchor.column = 0, anchor.row = 0;
        cursor.track = (int) l.tracks.size() - 1;
        cursor.column = l.tracks.back().noteColumns - 1;
        cursor.row = l.rows - 1;
        hasSelection = true;
        repaint();
        return true;
    }
    if (ctrl && (code == 'C' || code == 'c' || code == 'X' || code == 'x'))
    {
        i.copySelection (code == 'X' || code == 'x');
        return true;
    }
    if (ctrl && (code == 'V' || code == 'v'))
    {
        i.pasteHere();
        return true;
    }
    if (ctrl && (code == 'I' || code == 'i'))
    {
        const auto s = i.selection();
        i.edit ([&] (Pattern& p) { return interpolateVolume (p, l, s); });
        return true;
    }
    if (code == juce::KeyPress::deleteKey && ! ctrl)
    {
        if (hasSelection)
        {
            const auto s = i.selection();
            i.edit ([&] (Pattern& p) { return tracker::erase (p, l, s); });
            hasSelection = false;
            return true;
        }
        const auto c = cursor;
        i.edit ([&] (Pattern& p) { return clearCell (p, l, c); });
        if (i.editOn)
            i.advance();
        return true;
    }
    if (code == juce::KeyPress::insertKey && ! ctrl)
    {
        const auto c = cursor;
        i.edit ([&] (Pattern& p) { return insertRow (p, l, c.track, c.row); });
        return true;
    }
    if (code == juce::KeyPress::backspaceKey && ! ctrl && cursor.row > 0)
    {
        const auto c = cursor;
        i.edit ([&] (Pattern& p) { return deleteRow (p, l, c.track, c.row - 1); });
        i.moveCursorRows (-1);
        return true;
    }
    if (code == juce::KeyPress::escapeKey && hasSelection)
    {
        hasSelection = false;
        repaint();
        return true;
    }

    // entering: only with Edit on and no Ctrl / Alt (the typing keyboard plays otherwise, through the main window)
    if (! i.editOn || ctrl || alt)
        return false;
    const auto ch = keys::letterOfKeyCode (code);
    if (cursor.field == Field::note)
    {
        if (ch == '1' && ! shift)
        {
            i.typeOffHere();
            return true;
        }
        if (! shift)
        {
            const auto place = keys::typingpiano::physicalKey (code, [] (int c) { return scanCodeOfKeyCode (c); });
            const auto atFour = keys::typingPianoNote (keys::letterOfKeyCode (place), 4);
            if (atFour >= 0)
            {
                const auto k = atFour - 48 + 12 * octave;
                if (k >= 0 && k <= 127)
                    i.typeKey (k);
                return true;
            }
        }
        return false;
    }
    if (cursor.field == Field::fxLetter)
    {
        if (const auto fx = ch != 0 ? fxFromLetter ((char) ch) : std::nullopt; fx && *fx != Fx::cut)
        {
            i.typeFxLetter (*fx);
            return true;
        }
        if (const auto d = digitOf (ch, hex); d >= 0)
        {
            cursor = moveHorizontal (l, i.shown, cursor, 1);
            i.typeDigit (d);
            return true;
        }
        return false;
    }
    if (const auto d = digitOf (ch, hex); d >= 0 && ! shift)
    {
        i.typeDigit (d);
        return true;
    }
    return false;
}

bool TrackerComponent::keyStateChanged (bool isKeyDown)
{
    if (! isKeyDown)
        impl->preview.releaseAll();
    return false;
}

void TrackerComponent::focusLost (FocusChangeType)
{
    impl->preview.releaseAll();
    impl->endValueEntry();
}

void TrackerComponent::visibilityChanged()
{
    if (! isVisible())
    {
        impl->preview.releaseAll();
        impl->endValueEntry();
    }
    else if (impl->refresh())
        repaint();
}

// ------------------------------------------------------------------------------------------------------------ mouse
void TrackerComponent::mouseDown (const juce::MouseEvent& e)
{
    auto& i = *impl;
    grabKeyboardFocus();
    const auto hit = i.hitAt (e.getPosition());
    if (hit.track < 0)
        return;
    const auto& track = layout.tracks[(std::size_t) hit.track];
    if (hit.header)
    {
        if (hit.mute)
            engine.editSong ([channels = track.channels] (Song& s) {
                bool allMuted = true;
                for (auto c : channels)
                    allMuted = allMuted && c < (int) s.channels.size() && s.channels[(std::size_t) c].muted;
                for (auto c : channels)
                    if (c < (int) s.channels.size())
                        s.channels[(std::size_t) c].muted = ! allMuted;
            });
        else if (hit.solo)
            engine.editSong ([channels = track.channels] (Song& s) {
                if (channels.size() == 1)
                    noteedit::soloChannel (s, channels.front(), &flsettings::channelSoloMemory(), flsettings::options().restoreStateAfterSolo.load());
                else
                    for (int c = 0; c < (int) s.channels.size(); ++c)
                        s.channels[(std::size_t) c].muted = std::find (channels.begin(), channels.end(), c) == channels.end();
            });
        else if (e.mods.isPopupMenu())
        {
            // groups: this track with the next one, or apart again
            juce::PopupMenu m;
            const auto t = hit.track;
            m.addItem (tr ("Group with the next track (one track, an Ins column)"), t + 1 < (int) layout.tracks.size(), false, [this, t] {
                const auto& l = layout;
                std::vector<std::uint64_t> uids;
                for (auto k : { t, t + 1 })
                    for (auto c : l.tracks[(std::size_t) k].channels)
                        if (c < (int) engine.getSong().channels.size())
                            uids.push_back (engine.getSong().channels[(std::size_t) c].uid);
                const auto name = l.tracks[(std::size_t) t].name;
                engine.editSong ([uids, name] (Song& s) {
                    // the channels leave the groups they were in
                    for (auto& g : s.tracker.groups)
                        g.channels.erase (std::remove_if (g.channels.begin(), g.channels.end(), [&uids] (std::uint64_t u) {
                            return std::find (uids.begin(), uids.end(), u) != uids.end();
                        }), g.channels.end());
                    s.tracker.groups.erase (std::remove_if (s.tracker.groups.begin(), s.tracker.groups.end(), [] (const TrackerGroup& g) { return g.channels.size() < 2; }),
                                            s.tracker.groups.end());
                    s.tracker.groups.push_back ({ name, uids });
                });
            });
            m.addItem (tr ("Ungroup"), track.group, false, [this, channels = track.channels] {
                std::vector<std::uint64_t> uids;
                for (auto c : channels)
                    if (c < (int) engine.getSong().channels.size())
                        uids.push_back (engine.getSong().channels[(std::size_t) c].uid);
                engine.editSong ([uids] (Song& s) {
                    s.tracker.groups.erase (std::remove_if (s.tracker.groups.begin(), s.tracker.groups.end(), [&uids] (const TrackerGroup& g) {
                        return ! g.channels.empty() && std::find (uids.begin(), uids.end(), g.channels.front()) != uids.end();
                    }), s.tracker.groups.end());
                });
            });
            m.addSeparator();
            m.addItem (tr ("Add a note column (Alt+Right)"), [this, c = track.channels.front()] {
                ++impl->extraColumns[c];
                impl->refresh (true);
                repaint();
            });
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        }
        else if (! track.channels.empty())
            engine.setSelectedChannel (track.channels.front());
        return;
    }
    if (! hit.cell)
        return;
    i.endValueEntry();
    const auto before = cursor;
    cursor.track = hit.track;
    cursor.column = hit.column;
    cursor.field = hit.field;
    cursor.fxSlot = hit.fxSlot;
    cursor.row = std::clamp<std::int64_t> (hit.row, 0, std::max<std::int64_t> (0, layout.rows - 1));
    cursor = clampCursor (layout, i.shown, cursor);
    if (e.mods.isShiftDown())
        i.cursorMoved (before, true);
    else
    {
        hasSelection = false;
        anchor = cursor;
        selecting = true;
    }
    repaint();
}

void TrackerComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! selecting)
        return;
    auto& i = *impl;
    const auto hit = i.hitAt (e.getPosition());
    if (! hit.cell)
        return;
    Cursor c = cursor;
    c.track = hit.track;
    c.column = hit.column;
    c.field = hit.field;
    c.fxSlot = hit.fxSlot;
    c.row = std::clamp<std::int64_t> (hit.row, 0, std::max<std::int64_t> (0, layout.rows - 1));
    if (c.track != anchor.track || c.column != anchor.column || c.row != anchor.row)
        hasSelection = true;
    cursor = clampCursor (layout, i.shown, c);
    i.ensureVisible();
    repaint();
}

void TrackerComponent::mouseUp (const juce::MouseEvent&) { selecting = false; }

void TrackerComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    auto& i = *impl;
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const auto d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollX -= (int) std::lround (d * 200.0f);
    }
    else
        firstRow -= (std::int64_t) std::lround (wheel.deltaY * 12.0f);
    i.updateScrollBars();
    repaint();
}

// ------------------------------------------------------------------------------------------------------- for checks
void TrackerComponent::setCursorForCheck (const tracker::Cursor& c)
{
    cursor = clampCursor (layout, impl->shown, c);
    impl->ensureVisible();
    repaint();
}

void TrackerComponent::setSelectionForCheck (const tracker::Cursor& from, const tracker::Cursor& to)
{
    anchor = from;
    cursor = to;
    hasSelection = true;
    repaint();
}

void TrackerComponent::setEditForCheck (bool on)
{
    impl->editOn = on;
    impl->updateToolbar();
    repaint();
}

void TrackerComponent::setPlayRowForSnapshot (std::int64_t row)
{
    playRow = row;
    repaint();
}

double TrackerComponent::paintRowsBenchMs (int frames)
{
    // what a playhead step paints: two rows (the old and the new playhead row)
    juce::Image image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true);
    const auto a = impl->rowRect (firstRow + 3), b = impl->rowRect (firstRow + 4);
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f)
    {
        juce::Graphics g (image);
        juce::RectangleList<int> clip (a);
        clip.add (b);
        g.reduceClipRegion (clip);
        paint (g);
    }
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / std::max (1, frames);
}

double TrackerComponent::paintBenchMs (int frames)
{
    juce::Image image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true);
    juce::Graphics g (image);
    paint (g); // (warm the caches)
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f)
    {
        juce::Graphics gf (image);
        paint (gf);
    }
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / std::max (1, frames);
}

// ------------------------------------------------------------------------------------------------------- the window
void openTracker (AudioEngine& engine, int pattern)
{
    if (auto* w = findToolWindow ("tracker"))
    {
        if (auto* t = dynamic_cast<TrackerComponent*> (w->getContentComponent()))
            t->showPattern (pattern);
        w->toFront (true);
        return;
    }
    auto content = std::make_unique<TrackerComponent> (engine);
    content->showPattern (pattern);
    openToolWindow (std::move (content), tr ("Tracker"), "tracker", true);
}

void closeTracker()
{
    if (auto* w = findToolWindow ("tracker"))
        delete w;
}

bool isTrackerOpen() { return findToolWindow ("tracker") != nullptr; }

std::unique_ptr<TrackerComponent> makeTracker (AudioEngine& engine, bool compact) { return std::make_unique<TrackerComponent> (engine, compact); }
} // namespace lynx
