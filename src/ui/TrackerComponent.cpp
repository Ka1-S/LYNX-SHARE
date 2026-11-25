#include "ui/TrackerComponent.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
#include "ui/PaintAB.h"
#include "ui/ShortcutManager.h"
#include "ui/TextCache.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/WindowOwner.h"
#include "platform/ThreadInstructions.h"

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
        std::vector<juce::String> placeholders; // per field: its dots / dashes (round 190ob: made once, not per cell)
    };
    struct TrackGeom
    {
        int x = 0, width = 0;
        std::vector<ColumnGeom> columns;
    };
    std::vector<TrackGeom> geom;
    std::vector<std::uint64_t> geomHash; // per track: its columns and fields (round 190ob, the chunk signatures)
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
    // (round 190ob: keyed by the colour, the height and the text themselves: no key string is built per cell)
    struct PictureKey
    {
        juce::uint32 argb = 0;
        int height = 0;
        juce::String text;
        bool operator== (const PictureKey& o) const noexcept { return argb == o.argb && height == o.height && text == o.text; }
    };
    struct PictureKeyHash
    {
        std::size_t operator() (const PictureKey& k) const noexcept { return k.text.hash() ^ ((std::size_t) k.argb * 0x9E3779B97F4A7C15ull) ^ (std::size_t) k.height; }
    };
    std::unordered_map<PictureKey, Picture, PictureKeyHash> pictures;
    float picturesScale = 0.0f;
    juce::Font picturesFont { juce::FontOptions {} };
    juce::Font monoNow { juce::FontOptions {} }; // the grid's font (the paint's, once per paint)

    const Picture& picture (const juce::String& text, juce::Colour colour, int height, float scale)
    {
        if (scale != picturesScale || monoNow != picturesFont || pictures.size() > 6000)
        {
            pictures.clear();
            picturesScale = scale;
            picturesFont = monoNow;
        }
        PictureKey key { colour.getARGB(), height, text };
        auto it = pictures.find (key);
        if (it == pictures.end())
        {
            Picture p;
            p.width = (int) std::ceil (cw * (float) text.length()) + 2;
            p.image = juce::Image (juce::Image::ARGB, std::max (1, (int) std::ceil ((float) p.width * scale)), std::max (1, (int) std::ceil ((float) height * scale)), true);
            juce::Graphics ig (p.image);
            ig.addTransform (juce::AffineTransform::scale (scale));
            ig.setFont (monoNow);
            ig.setColour (colour);
            ig.drawText (text, 0, 0, p.width, height, juce::Justification::centredLeft, false);
            it = pictures.emplace (std::move (key), std::move (p)).first;
        }
        return it->second;
    }

    void drawGridText (juce::Graphics& g, const juce::String& text, juce::Colour colour, int x, int y, int height)
    {
        if (text.isEmpty())
            return;
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto& p = picture (text, colour, height, scale);
        if (! g.clipRegionIntersects ({ x, y, p.width, height }))
            return;
        g.setOpacity (1.0f); // (an image is drawn at the current colour's opacity: the picture holds its own colour)
        if (scale == 1.0f)
            g.drawImageAt (p.image, x, y);
        else
            g.drawImage (p.image, juce::Rectangle<float> ((float) x, (float) y, (float) p.width, (float) height));
    }

    // ---- texts made once (round 190ob): the key names, and the numbers by digits and base
    std::array<juce::String, 128> keyTexts;
    std::uint64_t keyTextsHash = 0;
    std::unordered_map<int, juce::String> numberTexts;
    const juce::String& keyName (int key)
    {
        if (key < 0 || key > 127)
        {
            static const juce::String unknown ("???");
            return unknown;
        }
        auto& t = keyTexts[(std::size_t) key];
        if (t.isEmpty())
            t = juce::String::fromUTF8 (keyText (key).c_str());
        return t;
    }
    const juce::String& number (int value, int digits, bool inHex)
    {
        if (value < 0 || value > 0xFFFFF || digits > 7)
        {
            static juce::String scratch;
            scratch = numberText (value, digits, inHex);
            return scratch;
        }
        const auto key = (value << 4) | (digits << 1) | (inHex ? 1 : 0);
        auto it = numberTexts.find (key);
        if (it == numberTexts.end())
        {
            if (numberTexts.size() > 20000)
                numberTexts.clear();
            it = numberTexts.emplace (key, juce::String (numberText (value, digits, inHex))).first;
        }
        return it->second;
    }

    // ---- the row-chunk cache (round 190ob). The cells of chunkRows rows of one track (and the row numbers) are painted
    // once into an opaque image and drawn as that image on every frame that shows them again: scrolling, following the
    // playhead, an edit in another track. A chunk is painted again when what it shows changes (its signature: the
    // track's notes as its cells show them, its columns, the grid's settings, colours and font). The rows of the cursor,
    // of the playhead and of a block are painted straight on top, as before. The same pixels as painting straight
    // (--tracker190nwshot checks it); off at fractional pixel scales, where a row does not start on a pixel.
    static constexpr int chunkRows = 16;
    static constexpr std::size_t chunkBudget = 48u << 20; // bytes of chunk images kept
    struct Chunk
    {
        juce::Image image;
        std::uint64_t signature = 0, used = 0;
        std::size_t bytes = 0;
    };
    std::unordered_map<std::uint64_t, Chunk> chunks; // (track + 1) << 40 | chunk
    std::size_t chunkBytes = 0;
    std::uint64_t paints = 0;
    int chunkRendersLeft = 0; // new chunks this paint may still make
    std::vector<std::uint64_t> trackHash; // per track: its notes as the cells show them
    bool useChunks = true;

    static std::uint64_t mix (std::uint64_t h, std::uint64_t v) noexcept
    {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        h ^= h >> 31;
        h *= 0xBF58476D1CE4E5B9ull;
        return h ^ (h >> 29);
    }
    static std::uint64_t bits (float f) noexcept
    {
        std::uint32_t u = 0;
        std::memcpy (&u, &f, sizeof u);
        return u;
    }

    void dropChunks (int track)
    {
        if (track < -1)
        {
            chunks.clear();
            chunkBytes = 0;
            return;
        }
        for (auto it = chunks.begin(); it != chunks.end();)
            if ((int) (it->first >> 40) - 1 == track)
            {
                chunkBytes -= it->second.bytes;
                it = chunks.erase (it);
            }
            else
                ++it;
    }
    size_t chunkCount() const noexcept { return chunks.size(); }

    void hashTracks()
    {
        const auto& l = owner.layout;
        trackHash.assign (l.tracks.size(), 0x51ED270B27A3C5D1ull);
        const auto p = patternIndex();
        const auto* notes = p >= 0 ? &song().patterns[(std::size_t) p].notes : nullptr;
        for (const auto& pl : l.placed)
        {
            if (pl.track < 0 || pl.track >= (int) trackHash.size())
                continue;
            auto h = trackHash[(std::size_t) pl.track];
            for (const std::uint64_t v : { (std::uint64_t) pl.column, (std::uint64_t) pl.startRow, (std::uint64_t) pl.offRow, (std::uint64_t) pl.startDelay,
                                           (std::uint64_t) pl.offDelay, (std::uint64_t) pl.shortNote, (std::uint64_t) pl.offShown, (std::uint64_t) pl.occupiedEnd })
                h = mix (h, v);
            if (notes != nullptr && pl.note >= 0 && pl.note < (int) notes->size())
            {
                const auto& n = (*notes)[(std::size_t) pl.note];
                for (const std::uint64_t v : { (std::uint64_t) n.key, (std::uint64_t) n.channel, (std::uint64_t) n.muted, (std::uint64_t) n.slide, (std::uint64_t) n.porta,
                                               bits (n.velocity), bits (n.pan), bits (n.chance), bits (n.release), bits (n.finePitch), (std::uint64_t) n.length })
                    h = mix (h, v);
                if (fxActive (n, Fx::bend)) // (round 190om: its Bxx)
                    h = mix (h, 0x100u + (std::uint64_t) fxValueOf (n, Fx::bend, n.length));
            }
            trackHash[(std::size_t) pl.track] = h;
        }
        // round 190om: the channel's Vxx rows
        for (std::size_t t = 0; t < l.tracks.size(); ++t)
            if (l.tracks[t].volumeSlot >= 0)
            {
                auto h = mix (trackHash[t], (std::uint64_t) l.tracks[t].volumeSlot);
                for (std::size_t r = 0; r < l.tracks[t].volumeRows.size(); ++r)
                    if (l.tracks[t].volumeRows[r] >= 0)
                        h = mix (mix (h, (std::uint64_t) r), (std::uint64_t) l.tracks[t].volumeRows[r]);
                trackHash[t] = h;
            }
    }

    // what every chunk shows the same way: the grid's settings, font, colours and note names
    std::uint64_t gridHash (float scale)
    {
        using namespace theme;
        const auto& l = owner.layout;
        std::uint64_t h = 0x2545F4914F6CDD1Dull;
        for (const std::uint64_t v : { (std::uint64_t) hex, bits (cw), (std::uint64_t) rowHeight, bits (scale), (std::uint64_t) l.rows, (std::uint64_t) l.rowsPerBeat,
                                       (std::uint64_t) l.rowsPerBar, (std::uint64_t) l.rowTicks, (std::uint64_t) l.delayDigits, (std::uint64_t) numberWidth,
                                       (std::uint64_t) monoNow.toString().hash() })
            h = mix (h, v);
        for (auto c : { colour::bg0, colour::bg2, colour::bg3, colour::textPrimary, colour::textSecondary, colour::textDisabled, colour::info, colour::warning,
                        colour::accent, colour::border })
            h = mix (h, c.getARGB());
        return mix (h, keyTextsHash);
    }

    // the note names (the app's: English, solfege...): the key texts made once follow them (every paint, cached or not)
    void followNoteNames()
    {
        std::uint64_t names = 0;
        for (int k = 0; k < 12; ++k)
            for (auto ch : notenames::pitchName (k))
                names = mix (names, (unsigned char) ch);
        if (names != keyTextsHash)
        {
            keyTextsHash = names;
            for (auto& t : keyTexts)
                t.clear();
        }
    }

    Impl (TrackerComponent& o, AudioEngine& e)
        : owner (o), engine (e), frames (o, [this] (double) { return frameStep(); }, 60.0)
    {
        preview.setSender ([this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on); });
    }

    const Song& song() const { return engine.getSong(); }

    // Round 190rb (UI monkey test, seed 15 of the release build: TrackerComponent::paint -> paintCell -> tracker::fxActive
    // on a note past the pattern's end): the layout is built again by the song's change message, and a paint (a timer's
    // repaint) or a key can come before it, right after an undo or another project. The note a placed cell stands for,
    // or nullptr when the layout is a step behind the song.
    const Note* noteOf (const Layout& l, const Placed& placed) const
    {
        const auto& s = song();
        if (l.pattern < 0 || l.pattern >= (int) s.patterns.size())
            return nullptr;
        const auto& notes = s.patterns[(std::size_t) l.pattern].notes;
        return placed.note >= 0 && placed.note < (int) notes.size() ? &notes[(std::size_t) placed.note] : nullptr;
    }

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
        hashTracks();
        computeGeometry();
        updateScrollBars();
        updateToolbar();
        return true;
    }

    void computeGeometry()
    {
        dimRuns.clear(); // (keyed by the columns' places)
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
                    cg.placeholders.push_back (dots);
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
        // (round 190ob) what the chunks of each track depend on in the geometry, relative to the track's left
        geomHash.assign (geom.size(), 0);
        for (std::size_t t = 0; t < geom.size(); ++t)
        {
            const auto& tg = geom[t];
            auto h = mix (0x7F4A7C159E3779B9ull, (std::uint64_t) tg.width);
            for (const auto& cg : tg.columns)
            {
                h = mix (mix (mix (h, (std::uint64_t) (cg.x - tg.x)), (std::uint64_t) cg.width), cg.emptyText.hash());
                h = mix (h, cg.sustainText.hash());
                for (const auto& f : cg.fields)
                    for (const std::uint64_t v : { (std::uint64_t) f.field, (std::uint64_t) f.fxSlot, (std::uint64_t) (f.x - tg.x), (std::uint64_t) f.width,
                                                   (std::uint64_t) f.chars, (std::uint64_t) f.offset })
                        h = mix (h, v);
            }
            geomHash[t] = h;
        }
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
        const auto volumeSlot = isVolumeSlot (l, c); // (round 190om: a Vxx on any row)
        if (! volumeSlot && (cell.placed == nullptr || (cell.kind != Cell::Kind::start && ! (cell.kind == Cell::Kind::off && c.field == Field::dly))))
            return;
        if (volumeSlot && c.field != Field::fxValue)
            return;
        std::optional<Fx> fx;
        if (volumeSlot)
            fx = Fx::volume;
        else if (c.field == Field::fxValue && cell.kind == Cell::Kind::start)
        {
            const auto* note = noteOf (l, *cell.placed);
            if (note == nullptr)
                return;
            const auto list = shownFx (l, *cell.placed, *note);
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

    // ---------------------------------------------------------------------------------------- painting the rows
    // (round 190ob: the cells and row backgrounds as functions, so a chunk of the cache and a straight paint draw the
    // very same things; `plain` = without the cursor, the playhead and the block — a cached chunk)
    juce::Colour rowColour (std::int64_t r, bool plain) const
    {
        using namespace theme;
        const auto& l = owner.layout;
        if (! plain && r == owner.playRow)
            return colour::accentMuted;
        return r % std::max (1, l.rowsPerBar) == 0 ? colour::bg3 : r % std::max (1, l.rowsPerBeat) == 0 ? colour::bg2 : colour::bg0;
    }

    // the row's background over `rr` (the playhead's colour, the cursor row's tint) and its number at x `numberX`
    void paintRowBackground (juce::Graphics& g, std::int64_t r, juce::Rectangle<int> rr, int numberX, bool plain)
    {
        using namespace theme;
        const auto& l = owner.layout;
        g.setColour (rowColour (r, plain));
        g.fillRect (rr);
        const auto playing = ! plain && r == owner.playRow;
        if (! plain && r == owner.cursor.row && ! playing)
        {
            g.setColour (colour::textPrimary.withAlpha (0.05f));
            g.fillRect (rr);
        }
        drawGridText (g, number ((int) r, std::max (2, hex ? 2 : decimalDigits (std::max<std::int64_t> (1, l.rows - 1))), hex),
                      playing ? colour::textPrimary : r % std::max (1, l.rowsPerBeat) == 0 ? colour::textSecondary : colour::textDisabled, numberX, rr.getY(),
                      rr.getHeight());
    }

    // the dim placeholder run of a column: its fields in `mask` (bit = field index) as one text, each at its place
    std::map<std::pair<const ColumnGeom*, std::uint32_t>, juce::String> dimRuns;
    const juce::String& dimRun (const ColumnGeom& cg, std::uint32_t mask)
    {
        auto& text = dimRuns[{ &cg, mask }];
        if (text.isEmpty())
            for (std::size_t f = 0; f < cg.fields.size(); ++f)
                if ((mask >> f) & 1u)
                {
                    while (text.length() < cg.fields[f].offset)
                        text << " ";
                    text << cg.placeholders[f];
                }
        return text;
    }

    // one cell (track t, note column c, row r) with the row's top at y; ox: where the grid's x 0 is
    void paintCell (juce::Graphics& g, int t, int c, std::int64_t r, int ox, int y, juce::Colour trackColour, const std::vector<Note>* notes, bool plain,
                    const Selection& sel, bool hasSel)
    {
        using namespace theme;
        const auto& l = owner.layout;
        const auto& cg = geom[(std::size_t) t].columns[(std::size_t) c];
        const auto h = rowHeight;
        if (! g.clipRegionIntersects ({ ox + cg.x - 2, y, cg.width + 10, h }))
            return; // (a playhead step repaints two rows: the others are not even looked at)
        const auto cell = cellAt (l, t, c, r);
        if (! plain && hasSel && sel.contains (l, t, c, r))
        {
            g.setColour (colour::selectionFill);
            g.fillRect (juce::Rectangle<int> (ox + cg.x - 2, y, cg.width + 2, h));
        }
        const Note* n = cell.kind == Cell::Kind::start && notes != nullptr && cell.placed->note >= 0 && cell.placed->note < (int) notes->size()
                            ? &(*notes)[(std::size_t) cell.placed->note]
                            : nullptr; // (round 190rb: nothing when the layout is a step behind the song, see noteOf)
        // the note's fx in their slot order (tracker::shownFx, without a list made per cell)
        std::array<Fx, fxKinds> fxList {};
        int fxCount = 0;
        if (n != nullptr)
        {
            for (int k = 0; k < fxKinds; ++k)
                if ((Fx) k != Fx::cut && fxActive (*n, (Fx) k))
                    fxList[(std::size_t) fxCount++] = (Fx) k;
            if (cell.placed->shortNote)
                fxList[(std::size_t) fxCount++] = Fx::cut;
        }
        if (cell.kind == Cell::Kind::sustain)
        {
            // the sustain line, in the track's colour
            g.setColour (trackColour.withAlpha (0.7f));
            g.fillRect (ox + cg.x + 2, y, 2, h);
        }
        const auto cursorHere = ! plain && r == owner.cursor.row && t == owner.cursor.track && c == owner.cursor.column;
        const auto dim = colour::textDisabled.withAlpha (0.55f);
        // round 190om: the channel's volume event in this row (Vxx in note column 0's volume slot), −1 = none
        const auto& trackInfo = l.tracks[(std::size_t) t];
        const auto volumeHere = c == 0 && trackInfo.volumeSlot >= 0 && r >= 0 && r < (std::int64_t) trackInfo.volumeRows.size() ? trackInfo.volumeRows[(std::size_t) r] : -1;
        if ((cell.kind == Cell::Kind::empty || cell.kind == Cell::Kind::sustain) && ! cursorHere && volumeHere < 0)
        {
            // one text for the whole cell (most cells): the dim placeholders
            drawGridText (g, cell.kind == Cell::Kind::empty ? cg.emptyText : cg.sustainText, dim, ox + cg.x, y, h);
            return;
        }
        static const juce::String offText ("OFF"), noText;
        static const std::array<juce::String, fxKinds> letters = [] {
            std::array<juce::String, fxKinds> out;
            for (int k = 0; k < fxKinds; ++k)
                out[(std::size_t) k] = juce::String::charToString ((juce::juce_wchar) fxLetter ((Fx) k));
            return out;
        }();
        std::uint32_t dimMask = 0;
        for (std::size_t fi = 0; fi < cg.fields.size(); ++fi)
        {
            const auto& f = cg.fields[fi];
            const juce::Rectangle<int> box (ox + f.x, y, f.width + 2, h);
            const juce::String* text = &cg.placeholders[fi];
            auto col = dim;
            switch (f.field)
            {
                case Field::note:
                    if (n != nullptr)
                    {
                        text = &keyName (n->key);
                        col = n->muted ? colour::textDisabled : colour::textPrimary;
                    }
                    else if (cell.kind == Cell::Kind::off)
                    {
                        text = &offText;
                        col = colour::accent;
                    }
                    else if (cell.kind == Cell::Kind::sustain)
                        text = &noText;
                    break;
                case Field::ins:
                    if (n != nullptr)
                    {
                        text = &number (n->channel + 1, f.chars, hex);
                        col = colour::info;
                    }
                    break;
                case Field::vol:
                    if (n != nullptr)
                    {
                        text = &number ((int) std::lround (n->velocity * 127.0f), f.chars, hex);
                        col = colour::textSecondary;
                    }
                    break;
                case Field::pan:
                    if (n != nullptr)
                    {
                        text = &number ((int) std::lround ((n->pan + 1.0f) * 64.0f), f.chars, hex);
                        col = colour::textSecondary;
                    }
                    break;
                case Field::dly:
                {
                    const auto d = n != nullptr ? cell.placed->startDelay : cell.kind == Cell::Kind::off ? cell.placed->offDelay : 0;
                    if (d > 0)
                    {
                        text = &number (d, f.chars, hex);
                        col = colour::info;
                    }
                    break;
                }
                case Field::fxLetter:
                    if (f.fxSlot < fxCount)
                    {
                        text = &letters[(std::size_t) fxList[(std::size_t) f.fxSlot]];
                        col = colour::warning;
                    }
                    else if (volumeHere >= 0 && f.fxSlot == trackInfo.volumeSlot)
                    {
                        text = &letters[(std::size_t) Fx::volume];
                        col = colour::warning;
                    }
                    break;
                case Field::fxValue:
                    if (f.fxSlot < fxCount)
                    {
                        text = &number (fxValueOf (*n, fxList[(std::size_t) f.fxSlot], n->length), f.chars, hex);
                        col = colour::warning;
                    }
                    else if (volumeHere >= 0 && f.fxSlot == trackInfo.volumeSlot)
                    {
                        text = &number (volumeHere, f.chars, hex);
                        col = colour::warning;
                    }
                    break;
            }
            const auto placeholder = text->isNotEmpty() && (text == &cg.placeholders[fi] || text->containsOnly (".") || text->containsOnly ("-"));
            if (placeholder) // (the dim placeholders of the cell go out as one text, below)
            {
                if (text == &cg.placeholders[fi])
                    dimMask |= 1u << fi;
                else
                    dimMask |= 1u << 31; // (a value written as dots: the slow way below)
            }
            else if (text->isNotEmpty())
                drawGridText (g, *text, col, box.getX(), box.getY(), box.getHeight());
            // the cursor
            if (cursorHere && f.field == owner.cursor.field && (f.field != Field::fxLetter && f.field != Field::fxValue ? true : f.fxSlot == owner.cursor.fxSlot))
            {
                g.setColour (editOn ? colour::accent : colour::textPrimary.withAlpha (0.8f));
                g.drawRect (box.expanded (1, 0), 1);
            }
        }
        if (dimMask == 0)
            return;
        if ((dimMask >> 31) == 0)
        {
            drawGridText (g, dimRun (cg, dimMask), dim, ox + cg.x, y, h);
            return;
        }
        // (never in practice: a number shown as dots) the run as the cell's texts say
        juce::String run;
        for (std::size_t fi = 0; fi < cg.fields.size(); ++fi)
        {
            const auto& f = cg.fields[fi];
            juce::String text;
            switch (f.field)
            {
                case Field::ins: text = n != nullptr ? number (n->channel + 1, f.chars, hex) : cg.placeholders[fi]; break;
                case Field::vol: text = n != nullptr ? number ((int) std::lround (n->velocity * 127.0f), f.chars, hex) : cg.placeholders[fi]; break;
                case Field::pan: text = n != nullptr ? number ((int) std::lround ((n->pan + 1.0f) * 64.0f), f.chars, hex) : cg.placeholders[fi]; break;
                case Field::fxValue: text = f.fxSlot < fxCount ? number (fxValueOf (*n, fxList[(std::size_t) f.fxSlot], n->length), f.chars, hex) : cg.placeholders[fi]; break;
                default: text = ((dimMask >> fi) & 1u) != 0 ? cg.placeholders[fi] : juce::String(); break;
            }
            if (text.isNotEmpty() && (text.containsOnly (".") || text.containsOnly ("-")))
            {
                while (run.length() < f.offset)
                    run << " ";
                run << text;
            }
        }
        drawGridText (g, run, dim, ox + cg.x, y, h);
    }

    // a track's rows [r0, r1) with the first one's top at y0: its border line from lineTop to lineBottom, then its cells
    void paintTrackRows (juce::Graphics& g, int t, std::int64_t r0, std::int64_t r1, int ox, int y0, float lineTop, float lineBottom,
                         const std::vector<Note>* notes, bool plain, const Selection& sel, bool hasSel)
    {
        const auto& tg = geom[(std::size_t) t];
        const auto& track = owner.layout.tracks[(std::size_t) t];
        const auto trackColour = channelColour (song(), track.channels.empty() ? 0 : track.channels.front());
        g.setColour (theme::colour::border);
        g.drawVerticalLine (ox + tg.x + tg.width - 1, lineTop, lineBottom);
        for (int c = 0; c < (int) tg.columns.size(); ++c)
            for (auto r = r0; r < r1; ++r)
                paintCell (g, t, c, r, ox, y0 + (int) (r - r0) * rowHeight, trackColour, notes, plain, sel, hasSel);
    }

    // a chunk of the cache drawn at (x, y): track t's (−1: the row numbers) rows k · chunkRows on; painted first when
    // missing or out of date
    void drawChunk (juce::Graphics& g, int t, std::int64_t k, int x, int y, std::uint64_t gridSignature, float scale, const std::vector<Note>* notes)
    {
        const auto& l = owner.layout;
        const auto r0 = k * chunkRows, r1 = std::min<std::int64_t> (r0 + chunkRows, l.rows);
        const auto w = t < 0 ? numberWidth - 1 : geom[(std::size_t) t].width;
        const auto h = (int) (r1 - r0) * rowHeight;
        if (w <= 0 || h <= 0)
            return;
        const auto key = ((std::uint64_t) (t + 1) << 40) | (std::uint64_t) k;
        if (! g.clipRegionIntersects ({ x, y, w, h }))
        {
            // in view but outside this paint's clip (a playhead step): kept fresh for the cache, not painted
            if (const auto it = chunks.find (key); it != chunks.end())
                it->second.used = paints;
            return;
        }
        const auto colourOf = [&] { const auto& ch = l.tracks[(std::size_t) t].channels; return channelColour (song(), ch.empty() ? 0 : ch.front()).getARGB(); };
        const auto signature = t < 0 ? mix (gridSignature, (std::uint64_t) k)
                                     : mix (mix (mix (mix (gridSignature, (std::uint64_t) k), trackHash[(std::size_t) t]), geomHash[(std::size_t) t]), colourOf());
        const auto pw = (int) std::ceil ((float) w * scale), ph = (int) std::ceil ((float) h * scale);
        const auto found = chunks.find (key);
        const auto valid = found != chunks.end() && ! found->second.image.isNull() && found->second.signature == signature
                           && found->second.image.getWidth() == pw && found->second.image.getHeight() == ph;
        if (! valid && chunkRendersLeft <= 0)
        {
            // over this paint's budget of new chunks (a view's first frame, a scroll to far away): this one is painted
            // straight, in view only, and made on a later frame (no timer: the next paint that shows it)
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion ({ x, y, w, h });
            const auto from = std::max (r0, owner.firstRow), to = std::min (r1, owner.firstRow + visibleRows() + 1);
            const auto rowY = [&] (std::int64_t r) { return y + (int) (r - r0) * rowHeight; };
            if (t < 0)
            {
                for (auto r = from; r < to; ++r)
                    paintRowBackground (g, r, { x, rowY (r), w, rowHeight }, x + 6, true);
                g.setColour (theme::colour::border);
                g.drawVerticalLine (x + numberWidth - 2, (float) y, (float) (y + h));
            }
            else if (from < to)
            {
                for (auto r = from; r < to; ++r)
                {
                    g.setColour (rowColour (r, true));
                    g.fillRect (x, rowY (r), w, rowHeight);
                }
                paintTrackRows (g, t, from, to, x - geom[(std::size_t) t].x, rowY (from), (float) y, (float) (y + h), notes, true, {}, false);
            }
            return;
        }
        if (! valid)
            --chunkRendersLeft;
        auto& chunk = chunks[key];
        if (! valid)
        {
            if (chunk.image.isNull() || chunk.image.getWidth() != pw || chunk.image.getHeight() != ph)
            {
                chunkBytes -= chunk.bytes;
                chunk.image = juce::Image (juce::Image::RGB, pw, ph, false);
                chunk.bytes = (std::size_t) pw * (std::size_t) ph * 4;
                chunkBytes += chunk.bytes;
            }
            chunk.signature = signature;
            juce::Graphics cg (chunk.image);
            if (scale != 1.0f)
                cg.addTransform (juce::AffineTransform::scale (scale));
            if (t < 0)
            {
                for (auto r = r0; r < r1; ++r)
                    paintRowBackground (cg, r, { 0, (int) (r - r0) * rowHeight, w, rowHeight }, 6, true);
                cg.setColour (theme::colour::border);
                cg.drawVerticalLine (numberWidth - 2, 0.0f, (float) h);
            }
            else
            {
                for (auto r = r0; r < r1; ++r)
                {
                    cg.setColour (rowColour (r, true));
                    cg.fillRect (0, (int) (r - r0) * rowHeight, w, rowHeight);
                }
                paintTrackRows (cg, t, r0, r1, -geom[(std::size_t) t].x, 0, 0.0f, (float) h, notes, true, {}, false);
            }
        }
        chunk.used = paints;
        if (scale == 1.0f)
            g.drawImageAt (chunk.image, x, y);
        else
            g.drawImage (chunk.image, juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h));
    }

    // over the budget: the chunks shown longest ago go (down to three quarters of it; never the ones in view)
    void trimChunks()
    {
        if (chunkBytes <= chunkBudget)
            return;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> byAge; // (last shown, key)
        byAge.reserve (chunks.size());
        for (const auto& [key, chunk] : chunks)
            if (chunk.used != paints)
                byAge.emplace_back (chunk.used, key);
        std::sort (byAge.begin(), byAge.end());
        for (const auto& [used, key] : byAge)
        {
            if (chunkBytes <= chunkBudget / 4 * 3)
                break;
            const auto it = chunks.find (key);
            chunkBytes -= it->second.bytes;
            chunks.erase (it);
        }
    }

    // ---------------------------------------------------------------------------------------- the track headers
    // track t's header with the grid's x 0 at ox and its top at y: a colour bar, the name, M S, the field captions
    void paintTrackHeader (juce::Graphics& g, int t, int ox, int y)
    {
        using namespace theme;
        const auto& s = song();
        const auto& tg = geom[(std::size_t) t];
        const auto& track = owner.layout.tracks[(std::size_t) t];
        const juce::Rectangle<int> box (ox + tg.x, y, tg.width - 1, headerHeight);
        if (! g.clipRegionIntersects (box))
            return;
        const auto first = track.channels.empty() ? 0 : track.channels.front();
        g.setColour (channelColour (s, first));
        g.fillRect (box.withHeight (3));
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small, true));
        textcache::drawText (g, headerName (t), juce::Rectangle<int> (box.getX() + 6, box.getY() + 4, box.getWidth() - 50, 18), juce::Justification::centredLeft, true);
        // M S
        const auto [muted, solo] = muteSolo (t);
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
                textcache::drawText (g, caption, juce::Rectangle<int> (ox + f.x, y + headerHeight - 18, f.width + (f.field == Field::fxLetter ? 20 : 4), 16),
                                     juce::Justification::centredLeft, false);
            }
        g.setColour (colour::border);
        g.drawVerticalLine (box.getRight(), (float) y, (float) (y + headerHeight));
    }

    juce::String headerName (int t) const
    {
        const auto& track = owner.layout.tracks[(std::size_t) t];
        auto name = juce::String::fromUTF8 (track.name.c_str());
        if (track.group)
            name << " · " << tr ("group of {0}", { juce::String ((int) track.channels.size()) });
        return name;
    }

    std::pair<bool, bool> muteSolo (int t) const
    {
        const auto& s = song();
        const auto& track = owner.layout.tracks[(std::size_t) t];
        bool muted = true, solo = true;
        for (auto ch : track.channels)
            muted = muted && ch < (int) s.channels.size() && s.channels[(std::size_t) ch].muted;
        for (int ch = 0; ch < (int) s.channels.size(); ++ch)
        {
            const auto inTrack = std::find (track.channels.begin(), track.channels.end(), ch) != track.channels.end();
            solo = solo && (inTrack ? ! s.channels[(std::size_t) ch].muted : s.channels[(std::size_t) ch].muted);
        }
        solo = solo && s.channels.size() > track.channels.size();
        return { muted, solo };
    }

    // every track's header as one picture (x 0 = the first track's left); false when it cannot be (too wide)
    juce::Image headerImage;
    std::uint64_t headerSignature = 0;
    bool headerPicture (float scale)
    {
        using namespace theme;
        const auto& l = owner.layout;
        const auto w = gridWidth - numberWidth;
        if (l.tracks.empty() || w <= 0 || (float) w * scale > 16384.0f || geom.size() < l.tracks.size())
            return false;
        // what the picture shows
        std::uint64_t h = mix (mix (mix (0x6A09E667F3BCC908ull, (std::uint64_t) w), bits (scale)), (std::uint64_t) numberWidth);
        for (const auto& f : { font (type::small, true), font (type::caption, true), font (type::caption) })
            h = mix (h, f.toString().hash());
        for (auto c : { colour::bg2, colour::textPrimary, colour::warning, colour::textDisabled, colour::ledOn, colour::textSecondary, colour::border })
            h = mix (h, c.getARGB());
        for (int f = 0; f <= (int) Field::dly; ++f)
            h = mix (h, fieldName ((Field) f).hash());
        for (int t = 0; t < (int) l.tracks.size(); ++t)
        {
            const auto& track = l.tracks[(std::size_t) t];
            const auto [muted, solo] = muteSolo (t);
            h = mix (mix (mix (mix (h, headerName (t).hash()), (std::uint64_t) muted * 2 + (std::uint64_t) solo), geomHash[(std::size_t) t]),
                     (std::uint64_t) geom[(std::size_t) t].x);
            h = mix (h, channelColour (song(), track.channels.empty() ? 0 : track.channels.front()).getARGB());
        }
        if (! headerImage.isNull() && h == headerSignature)
            return true;
        const auto pw = (int) std::ceil ((float) w * scale), ph = (int) std::ceil ((float) headerHeight * scale);
        if (headerImage.isNull() || headerImage.getWidth() != pw || headerImage.getHeight() != ph)
            headerImage = juce::Image (juce::Image::RGB, pw, ph, false);
        headerSignature = h;
        juce::Graphics hg (headerImage);
        if (scale != 1.0f)
            hg.addTransform (juce::AffineTransform::scale (scale));
        hg.setColour (colour::bg2);
        hg.fillRect (0, 0, w, headerHeight);
        for (int t = 0; t < (int) l.tracks.size(); ++t)
            paintTrackHeader (hg, t, -numberWidth, 0);
        return true;
    }

    // ---- a part of the window as a picture (round 190ob: the side panel, the hint bar). `signature` 0 = paint it
    // straight; else the picture is drawn, made again (over `background`) when the signature changes. The part draws in
    // the component's coordinates; the picture starts on whole pixels, so its pixels are the straight paint's. `margin`:
    // the picture reaches that far around the part (over the background), so a shape's anti-aliased edge on the part's
    // border is rasterised inside the picture as it is on the screen.
    struct PartPicture
    {
        juce::Image image;
        std::uint64_t signature = 0;
    };
    PartPicture sidePicture, hintPicture;
    template <typename Draw>
    void drawPart (juce::Graphics& g, PartPicture& p, juce::Rectangle<int> part, float scale, std::uint64_t signature, juce::Colour background, int margin, Draw&& draw)
    {
        if (part.isEmpty() || ! g.clipRegionIntersects (part))
            return;
        if (signature == 0)
        {
            draw (g);
            return;
        }
        const auto area = part.expanded (margin);
        const auto pw = (int) std::ceil ((float) area.getWidth() * scale), ph = (int) std::ceil ((float) area.getHeight() * scale);
        if (p.image.isNull() || p.image.getWidth() != pw || p.image.getHeight() != ph || p.signature != signature)
        {
            if (p.image.isNull() || p.image.getWidth() != pw || p.image.getHeight() != ph)
                p.image = juce::Image (juce::Image::RGB, pw, ph, false);
            p.signature = signature;
            juce::Graphics pg (p.image);
            if (scale != 1.0f)
                pg.addTransform (juce::AffineTransform::scale (scale));
            pg.setOrigin (-area.getX(), -area.getY());
            pg.setColour (background);
            pg.fillRect (area);
            draw (pg);
        }
        if (scale == 1.0f)
            g.drawImageAt (p.image, area.getX(), area.getY());
        else
            g.drawImage (p.image, area.toFloat());
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
                if (isVolumeSlot (l, c))
                    fx = Fx::volume;
                else if (cell.kind == Cell::Kind::start)
                {
                    if (const auto* note = noteOf (l, *cell.placed))
                    {
                        const auto list = shownFx (l, *cell.placed, *note);
                        if (c.fxSlot < (int) list.size())
                            fx = list[(std::size_t) c.fxSlot];
                    }
                }
                text << (fx ? tr (fxDescription (*fx)) : tr ("type G P Y R T M B for a note field"));
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
    i.editButton.setComponentID (compact ? "simple.dock.tracker.edit" : "tracker.edit"); // (round 190pg: --demo)
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

void TrackerComponent::showChannel (int channel)
{
    impl->refresh(); // (the channel was just selected: its track is there now)
    const auto t = layout.trackOfChannel (channel);
    if (t < 0)
        return;
    impl->endValueEntry();
    cursor.track = t;
    cursor.column = 0;
    cursor.field = tracker::Field::note;
    cursor = tracker::clampCursor (layout, impl->shown, cursor);
    hasSelection = false;
    impl->ensureVisible();
    repaint();
}

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
    const auto grid = i.gridArea();
    const auto rows = i.rowsArea();
    // (round 190ob) on whole pixels (a scale of 1, 2...) each pixel is filled once where it can be: the toolbar, the grid
    // and the hint bar by their own parts, the window's background only around them. At a fractional scale the edges
    // are blended, so the backgrounds go down in their old order.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto wholePixels = scale >= 1.0f && scale <= 4.0f && scale == std::floor (scale);
    if (wholePixels)
    {
        juce::RectangleList<int> around (getLocalBounds());
        around.subtract (i.toolbarArea());
        around.subtract (grid);
        around.subtract (i.hintArea());
        g.setColour (colour::bg1);
        g.fillRectList (around);
    }
    else
        g.fillAll (colour::bg1);
    g.setColour (colour::bg3);
    g.fillRect (i.toolbarArea());
    if (! wholePixels)
    {
        g.setColour (colour::bg0);
        g.fillRect (grid);
    }

    const auto pattern = i.patternIndex();
    const auto* notes = pattern >= 0 ? &s.patterns[(std::size_t) pattern].notes : nullptr;
    const auto mono = monoFont (type::small + 1.0f);
    i.monoNow = mono;
    i.followNoteNames();
    const auto visible = i.visibleRows();
    const auto lastRow = std::min (l.rows, firstRow + visible + 1);
    const auto sel = i.selection();
    const auto hasSel = hasSelection;

    // ---- rows: backgrounds (bars, beats), the playhead and the cursor rows, the row numbers, the cells
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (rows);
        // below the last row: the grid's own background
        const auto rowsBottom = rows.getY() + (int) std::max<std::int64_t> (0, lastRow - firstRow) * i.rowHeight;
        if (wholePixels && rowsBottom < rows.getBottom())
        {
            g.setColour (colour::bg0);
            g.fillRect (rows.withTop (rowsBottom));
        }
        const auto remainderLines = [&] { // (below the last row the straight paint's full-height lines show: the cached one draws them there)
            if (rowsBottom >= rows.getBottom())
                return;
            juce::Graphics::ScopedSaveState below (g);
            g.reduceClipRegion (rows.withTop (rowsBottom));
            g.setColour (colour::border);
            g.drawVerticalLine (rows.getX() + i.numberWidth - 2, (float) rows.getY(), (float) rows.getBottom());
            g.reduceClipRegion (rows.withTrimmedLeft (i.numberWidth - 1));
            for (int t = 0; t < (int) l.tracks.size() && t < (int) i.geom.size(); ++t)
            {
                const auto& tg = i.geom[(std::size_t) t];
                g.drawVerticalLine (grid.getX() - scrollX + tg.x + tg.width - 1, (float) rows.getY(), (float) rows.getBottom());
            }
        };
        const auto ox = grid.getX() - scrollX;
        const auto cellsClip = rows.withTrimmedLeft (i.numberWidth - 1);
        int t0 = -1, t1 = -2; // the tracks in view
        for (int t = 0; t < (int) l.tracks.size() && t < (int) i.geom.size(); ++t)
        {
            const auto& tg = i.geom[(std::size_t) t];
            if (ox + tg.x > rows.getRight() || ox + tg.x + tg.width < rows.getX() + i.numberWidth)
                continue;
            if (t0 < 0)
                t0 = t;
            t1 = t;
        }
        const auto cached = i.useChunks && ui::paintCache() && wholePixels && lastRow > firstRow;
        // the rows painted straight over the chunks: the cursor's, the playhead's and the block's
        const auto straight = [&] (std::int64_t r) { return r == playRow || r == cursor.row || (hasSel && r >= sel.row0 && r <= sel.row1); };
        if (! cached)
        {
            // no cache: every row as before the cache (the backgrounds and numbers, the numbers' line, then the tracks)
            for (auto r = firstRow; r < lastRow; ++r)
            {
                const auto rr = i.rowRect (r);
                if (g.clipRegionIntersects (rr))
                    i.paintRowBackground (g, r, rr, rr.getX() + 6, false);
            }
            g.setColour (colour::border);
            g.drawVerticalLine (rows.getX() + i.numberWidth - 2, (float) rows.getY(), (float) rows.getBottom());
            g.reduceClipRegion (cellsClip);
            for (auto t = t0; t >= 0 && t <= t1; ++t)
                i.paintTrackRows (g, t, firstRow, lastRow, ox, i.rowRect (firstRow).getY(), (float) rows.getY(), (float) rows.getBottom(), notes, false, sel, hasSel);
        }
        else
        {
            ++i.paints;
            const auto gridSignature = i.gridHash (scale);
            const auto k0 = firstRow / Impl::chunkRows, k1 = (lastRow - 1) / Impl::chunkRows;
            // a quarter of the chunks in view may be made per paint (enough for a row of chunks scrolling in or an edit
            // in a few tracks); a first frame paints the rest straight and the following paints make them
            i.chunkRendersLeft = std::max (8, (int) ((k1 - k0 + 1) * (std::max (0, t1 - t0 + 1) + 1)) / 4);
            const auto chunkY = [&] (std::int64_t k) { return rows.getY() + (int) (k * Impl::chunkRows - firstRow) * i.rowHeight; };
            for (auto k = k0; k <= k1; ++k)
                i.drawChunk (g, -1, k, rows.getX(), chunkY (k), gridSignature, scale, notes);
            juce::Graphics::ScopedSaveState cells (g);
            g.reduceClipRegion (cellsClip);
            // between the row numbers and the first track, and right of the last one: the rows' backgrounds
            const auto left = t0 >= 0 ? ox + i.geom[(std::size_t) t0].x : rows.getRight();
            const auto right = t0 >= 0 ? ox + i.geom[(std::size_t) t1].x + i.geom[(std::size_t) t1].width : rows.getRight();
            for (auto r = firstRow; r < lastRow; ++r)
            {
                if (straight (r))
                    continue;
                const auto rr = i.rowRect (r);
                g.setColour (i.rowColour (r, true));
                if (left > cellsClip.getX())
                    g.fillRect (cellsClip.getX(), rr.getY(), left - cellsClip.getX(), rr.getHeight());
                if (right < rows.getRight())
                    g.fillRect (right, rr.getY(), rows.getRight() - right, rr.getHeight());
            }
            for (auto t = t0; t >= 0 && t <= t1; ++t)
                for (auto k = k0; k <= k1; ++k)
                    i.drawChunk (g, t, k, ox + i.geom[(std::size_t) t].x, chunkY (k), gridSignature, scale, notes);
        }
        if (cached)
            remainderLines(); // (outside the cells' clip: the row numbers' line too)
        // the cursor's, the playhead's and the block's rows over the chunks, each in its own clip
        for (auto r = firstRow; cached && r < lastRow; ++r)
        {
            if (! straight (r))
                continue;
            const auto rr = i.rowRect (r);
            if (! g.clipRegionIntersects (rr))
                continue;
            juce::Graphics::ScopedSaveState row (g);
            g.reduceClipRegion (rr);
            i.paintRowBackground (g, r, rr, rr.getX() + 6, false);
            g.setColour (colour::border);
            g.drawVerticalLine (rows.getX() + i.numberWidth - 2, (float) rows.getY(), (float) rows.getBottom());
            g.reduceClipRegion (cellsClip);
            for (auto t = t0; t >= 0 && t <= t1; ++t)
                i.paintTrackRows (g, t, r, r + 1, ox, rr.getY(), (float) rows.getY(), (float) rows.getBottom(), notes, false, sel, hasSel);
        }
        if (cached)
            i.trimChunks();
    }

    // ---- the track headers: a colour bar, the name, M S, the field captions (round 190ob: on whole pixels as one
    // picture of every track's header, painted again when a name, a colour, a mute / solo or the columns change)
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
        if (wholePixels && i.useChunks && ui::paintCache() && i.headerPicture (scale))
        {
            if (scale == 1.0f)
                g.drawImageAt (i.headerImage, ox + i.numberWidth, header.getY());
            else
                g.drawImage (i.headerImage, juce::Rectangle<float> ((float) (ox + i.numberWidth), (float) header.getY(), (float) (i.gridWidth - i.numberWidth), (float) headerHeight));
        }
        else
            for (int t = 0; t < (int) l.tracks.size() && t < (int) i.geom.size(); ++t)
                i.paintTrackHeader (g, t, ox, header.getY());
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

    // ---- the side panel: the Sequence (the Playlist's order, read only) and the legend (painted straight: a picture of it
    // moved its rounded corners' anti-aliasing by a level)
    if (i.sidePanel())
    {
        const auto area = i.sideArea();
        i.drawPart (g, i.sidePicture, area, scale, 0, colour::bg1, 0, [&] (juce::Graphics& pg) {
            auto side = area;
            pg.setColour (colour::bg2);
            pg.fillRoundedRectangle (side.toFloat(), radius::md);
            side.reduce (10, 8);
            pg.setColour (colour::textPrimary);
            pg.setFont (font (type::small, true));
            textcache::drawText (pg, tr ("Sequence (the Playlist, read only)"), side.removeFromTop (22), juce::Justification::centredLeft, true);
            std::vector<const Clip*> clips;
            for (const auto& c : s.playlist)
                if (c.kind == ClipKind::pattern && c.pattern >= 0 && c.pattern < (int) s.patterns.size())
                    clips.push_back (&c);
            std::stable_sort (clips.begin(), clips.end(), [] (const Clip* a, const Clip* b) { return a->start != b->start ? a->start < b->start : a->track < b->track; });
            const auto listRows = std::min<int> ((int) clips.size(), 9);
            pg.setFont (monoFont (type::small));
            for (int k = 0; k < listRows; ++k)
            {
                const auto* c = clips[(std::size_t) k];
                auto row = side.removeFromTop (22);
                if (c->pattern == pattern)
                {
                    pg.setColour (colour::accentMuted);
                    pg.fillRoundedRectangle (row.toFloat(), radius::sm);
                }
                pg.setColour (colour::textSecondary);
                textcache::drawText (pg, numberText (k, 2, hex), row.removeFromLeft (30).withTrimmedLeft (4), juce::Justification::centredLeft, false);
                pg.setColour (colour::textPrimary);
                const auto barsValue = (double) c->length / (double) std::max<std::int64_t> (1, s.barTicks());
                const auto bars = std::abs (barsValue - std::round (barsValue)) < 1.0e-9 ? juce::String ((int) std::lround (barsValue)) : juce::String (barsValue, 2);
                textcache::drawText (pg, tr ("{0} bars", { bars }), row.removeFromRight (70).withTrimmedRight (6), juce::Justification::centredRight, false);
                textcache::drawText (pg, juce::String::fromUTF8 (s.patterns[(std::size_t) c->pattern].name.c_str()), row, juce::Justification::centredLeft, true);
            }
            if ((int) clips.size() > listRows)
            {
                pg.setColour (colour::textDisabled);
                textcache::drawText (pg, tr ("... {0} more", { juce::String ((int) clips.size() - listRows) }), side.removeFromTop (18), juce::Justification::centredLeft, false);
            }
            pg.setFont (font (type::caption));
            pg.setColour (colour::textDisabled);
            textcache::drawText (pg, tr ("A pattern placed again is the same pattern (no copy)."), side.removeFromTop (20), juce::Justification::centredLeft, true);
            side.removeFromTop (8);
            pg.setColour (colour::textPrimary);
            pg.setFont (font (type::small, true));
            textcache::drawText (pg, tr ("Fx column (the notes' own fields)"), side.removeFromTop (22), juce::Justification::centredLeft, true);
            pg.setFont (font (type::caption));
            for (int k = 0; k < fxKinds && side.getHeight() > 18; ++k)
            {
                pg.setColour (colour::textSecondary);
                textcache::drawText (pg, tr (fxDescription ((Fx) k)), side.removeFromTop (18), juce::Justification::centredLeft, true);
            }
            side.removeFromTop (6);
            pg.setColour (colour::textDisabled);
            textcache::drawFittedText (pg, tr ("Notes that overlap get extra note columns. A line runs down from a note while it sounds; OFF is where it ends."),
                                       side.removeFromTop (48), juce::Justification::topLeft, 3); // (round 190ob: laid out once)
        });
    }

    // ---- the hint bar (on whole pixels a picture, made again when its text changes)
    {
        const auto area = i.hintArea();
        const auto text = i.hintText();
        std::uint64_t signature = 0;
        if (wholePixels && i.useChunks && ui::paintCache())
        {
            signature = Impl::mix (Impl::mix (Impl::mix (Impl::mix ((std::uint64_t) area.getWidth(), (std::uint64_t) area.getHeight()), Impl::bits (scale)), text.hash()),
                                   tr ("Keys: Z-M / Q-P notes (the typing keyboard), 1 OFF, 0-9 A-F values, Del clear, Insert / Backspace rows, Tab next track, Ctrl+Up/Down transpose, Space play").hash());
            for (const auto& f : { font (type::small), font (type::caption) })
                signature = Impl::mix (signature, f.toString().hash());
            for (auto c : { colour::bg3, colour::textPrimary, colour::textDisabled })
                signature = Impl::mix (signature, c.getARGB());
        }
        i.drawPart (g, i.hintPicture, area, scale, signature, colour::bg3, 0, [&] (juce::Graphics& pg) {
            auto hint = area;
            pg.setColour (colour::bg3);
            pg.fillRect (hint);
            hint.reduce (10, 2);
            pg.setColour (colour::textPrimary);
            pg.setFont (font (type::small));
            textcache::drawText (pg, text, hint.removeFromTop (17), juce::Justification::centredLeft, true);
            pg.setColour (colour::textDisabled);
            pg.setFont (font (type::caption));
            textcache::drawText (pg, tr ("Keys: Z-M / Q-P notes (the typing keyboard), 1 OFF, 0-9 A-F values, Del clear, Insert / Backspace rows, Tab next track, Ctrl+Up/Down transpose, Space play"),
                                 hint, juce::Justification::centredLeft, true);
        });
    }
}

// ------------------------------------------------------------------------------------------------------------- keys
bool TrackerComponent::keyPressed (const juce::KeyPress& key)
{
    auto& i = *impl;
    i.refresh(); // (round 190rb: a key can come before the song's change message; the layout must be the song's)
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
        impl->dropChunks (-2); // (round 190ob: a hidden tracker keeps no chunk images)
    }
    else if (impl->refresh())
        repaint();
}

// ------------------------------------------------------------------------------------------------------------ mouse
void TrackerComponent::mouseDown (const juce::MouseEvent& e)
{
    auto& i = *impl;
    i.refresh(); // (round 190rb: as keyPressed)
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

namespace
{
std::uint64_t pixelHash (const juce::Image& image)
{
    const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
    std::uint64_t h = 1469598103934665603ull;
    for (int y = 0; y < data.height; ++y)
    {
        const auto* line = data.getLinePointer (y);
        for (int x = 0; x < data.width * data.pixelStride; ++x)
            h = (h ^ line[x]) * 1099511628211ull;
    }
    return h;
}
} // namespace

TrackerComponent::BenchResult TrackerComponent::paintBench (BenchFrame kind, int frames)
{
    juce::Image image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
    {
        juce::Graphics g (image);
        paint (g); // (warm the caches)
    }
    const auto start = firstRow;
    platform::ThreadInstructions counter;
    std::vector<double> ms;
    std::vector<long long> instructions;
    for (int f = 0; f < frames; ++f)
    {
        juce::Graphics g (image);
        if (kind == BenchFrame::scroll)
        {
            firstRow = std::clamp<std::int64_t> (start + f + 1, 0, std::max<std::int64_t> (0, layout.rows - impl->visibleRows()));
            impl->updateScrollBars();
        }
        else if (kind == BenchFrame::uncached)
            impl->dropChunks (-2);
        else if (kind == BenchFrame::edit)
            impl->dropChunks (cursor.track);
        if (kind == BenchFrame::playhead)
        {
            juce::RectangleList<int> clip (impl->rowRect (firstRow + 3));
            clip.add (impl->rowRect (firstRow + 4));
            g.reduceClipRegion (clip);
        }
        const auto i0 = counter.now();
        const auto t0 = std::chrono::steady_clock::now();
        paint (g);
        const auto t1 = std::chrono::steady_clock::now();
        const auto i1 = counter.now();
        ms.push_back (std::chrono::duration<double, std::milli> (t1 - t0).count());
        instructions.push_back (i0 >= 0 && i1 >= 0 ? i1 - i0 : -1);
    }
    firstRow = start;
    impl->updateScrollBars();
    BenchResult r;
    if (! ms.empty())
    {
        std::sort (ms.begin(), ms.end());
        std::sort (instructions.begin(), instructions.end());
        r.ms = ms[ms.size() / 2];
        r.instructions = instructions[instructions.size() / 2];
    }
    r.pixels = pixelHash (image);
    return r;
}

std::uint64_t TrackerComponent::framePixelsForCheck (bool cached)
{
    return pixelHash (frameForCheck (cached));
}

int TrackerComponent::framesDifferForCheck (const juce::Image& a, const juce::Image& b, int tolerance, int& largest, int& anyDifference)
{
    largest = 0;
    anyDifference = 0;
    if (a.getBounds() != b.getBounds())
    {
        largest = 255;
        anyDifference = a.getWidth() * a.getHeight();
        return anyDifference;
    }
    const juce::Image::BitmapData da (a, juce::Image::BitmapData::readOnly), db (b, juce::Image::BitmapData::readOnly);
    int over = 0;
    for (int y = 0; y < da.height; ++y)
        for (int x = 0; x < da.width; ++x)
        {
            const auto ca = da.getPixelColour (x, y).getARGB(), cb = db.getPixelColour (x, y).getARGB();
            if (ca == cb)
                continue;
            int d = 0;
            for (int shift = 0; shift < 32; shift += 8)
                d = std::max (d, std::abs ((int) ((ca >> shift) & 0xff) - (int) ((cb >> shift) & 0xff)));
            largest = std::max (largest, d);
            ++anyDifference;
            if (d > tolerance)
                ++over;
        }
    return over;
}

juce::Image TrackerComponent::frameForCheck (bool cached)
{
    // (rollup 0.1.31 step 5: the frame is drawn the way the window draws it. The chunks are images of the platform's own
    // kind (Direct2D bitmaps on Windows) and the window paints with that renderer too, so on Windows a software target
    // compared Direct2D's text and colours in the chunks with the software renderer's in the straight frame: 7,700 text
    // edge pixels and 1-level colour steps apart, though the window shows the same either way)
   #if JUCE_WINDOWS
    juce::Image image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::NativeImageType());
   #else
    juce::Image image (juce::Image::ARGB, std::max (1, getWidth()), std::max (1, getHeight()), true, juce::SoftwareImageType());
   #endif
    const auto was = impl->useChunks;
    impl->useChunks = cached;
    {
        juce::Graphics g (image);
        paint (g);
    }
    impl->useChunks = was;
    // (diagnostics: LYNX_TRACKER_DUMP=<prefix> writes each compared frame)
    if (const auto* dump = std::getenv ("LYNX_TRACKER_DUMP"); dump != nullptr && *dump != 0)
    {
        static int frame = 0;
        juce::FileOutputStream out (juce::File (juce::String (dump) + "-" + juce::String (frame++) + (cached ? "-cached" : "-straight") + ".png"));
        if (out.openedOk())
        {
            out.setPosition (0);
            out.truncate();
            juce::PNGImageFormat().writeImageToStream (image, out);
        }
    }
    return image;
}

void TrackerComponent::scrollToRowForCheck (std::int64_t row)
{
    firstRow = row;
    impl->updateScrollBars();
}

size_t TrackerComponent::cachedChunksForCheck() const noexcept { return impl->chunkCount(); }

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
void openTracker (AudioEngine& engine, int pattern, int channel)
{
    if (auto* w = findToolWindow ("tracker"))
    {
        if (auto* t = dynamic_cast<TrackerComponent*> (w->getContentComponent()))
        {
            t->showPattern (pattern);
            if (channel >= 0)
                t->showChannel (channel);
        }
        w->toFront (true);
        return;
    }
    auto content = std::make_unique<TrackerComponent> (engine);
    content->showPattern (pattern);
    if (channel >= 0)
        content->showChannel (channel);
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
