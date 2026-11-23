#include "ui/ChordHelper.h"

#include <limits>

#include "core/LayoutStyle.h"
#include "core/ProjectKey.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/SimpleLook.h"
#include "ui/Theme.h"

namespace lynx
{
using namespace modalhelp;

// --- favourites (round 190nu) ------------------------------------------------------------------------------------
namespace
{
bool isFavourite (const std::string& id)
{
    const auto& f = ChordHelperPanel::favourites;
    return std::find (f.begin(), f.end(), id) != f.end();
}
void toggleFavourite (const std::string& id)
{
    auto& f = ChordHelperPanel::favourites;
    if (const auto it = std::find (f.begin(), f.end(), id); it != f.end())
        f.erase (it);
    else
        f.push_back (id);
}
} // namespace

// --- the three looks ------------------------------------------------------------------------------------------------
struct ChordHelperPanel::Palette
{
    juce::Colour back, panel, headFill, rowFill, rowAlt, hover, selected, selectedEdge, text, dim, accent, onAccent, border;
    float radius = 3.0f;
    float body = 13.0f, small = 12.0f, caption = 11.0f;
    int rowH = 36, headH = 22, gap = 8;
};

ChordHelperPanel::Palette ChordHelperPanel::palette() const
{
    Palette p;
    switch (look)
    {
        case Look::live: // Live 12.4 Dark (theme::livelook, measured by round 190ia): flat greys, square, light-blue selection
            p.back = theme::livelook::window;
            p.panel = theme::livelook::surface;
            p.headFill = theme::livelook::raised;
            p.rowFill = theme::livelook::surface;
            p.rowAlt = theme::livelook::surface.brighter (0.04f);
            p.hover = theme::livelook::raised;
            p.selected = theme::livelook::selectionFocused;
            p.selectedEdge = theme::livelook::selectionFocused;
            p.text = theme::livelook::text;
            p.dim = theme::livelook::text.withMultipliedBrightness (0.72f);
            p.accent = theme::livelook::on;
            p.onAccent = theme::livelook::ink;
            p.border = theme::livelook::separator;
            p.radius = 0.0f;
            p.body = 11.5f, p.small = 11.0f, p.caption = 10.0f;
            p.rowH = 32, p.headH = 17, p.gap = 2;
            break;
        case Look::simple: // the Simple layout: rounded cards, larger rows, the red accent (BandLab / FL Studio Mobile)
            p.back = theme::colour::bg1;
            p.panel = theme::colour::bg2;
            p.headFill = theme::colour::bg3;
            p.rowFill = theme::colour::bg3;
            p.rowAlt = theme::colour::bg3;
            p.hover = theme::colour::bg4;
            p.selected = theme::colour::accentMuted;
            p.selectedEdge = theme::colour::accent;
            p.text = theme::colour::textPrimary;
            p.dim = theme::colour::textSecondary;
            p.accent = theme::colour::accent;
            p.onAccent = theme::colour::onAccent;
            p.border = theme::colour::border;
            p.radius = 10.0f;
            p.body = theme::type::body + 1.0f, p.small = theme::type::small + 1.0f, p.caption = theme::type::caption + 1.0f;
            p.rowH = ui::simplelook::px (42, 50), p.headH = 26, p.gap = 10;
            break;
        case Look::fl: // the Pattern layout: LYNX's FL-style tool window (the Chord progression tool's tokens)
            p.back = theme::colour::bg2;
            p.panel = theme::colour::bg0;
            p.headFill = theme::colour::bg3;
            p.rowFill = theme::colour::bg0;
            p.rowAlt = theme::colour::bg1;
            p.hover = theme::colour::bg4;
            p.selected = theme::colour::accentMuted;
            p.selectedEdge = theme::colour::accent;
            p.text = theme::colour::textPrimary;
            p.dim = theme::colour::textSecondary;
            p.accent = theme::colour::accent;
            p.onAccent = theme::colour::onAccent;
            p.border = theme::colour::borderStrong;
            p.radius = theme::radius::sm;
            p.body = theme::type::body, p.small = theme::type::small, p.caption = theme::type::caption;
            p.rowH = 40, p.headH = 22, p.gap = 8;
            break;
    }
    return p;
}

ChordHelperPanel::Look ChordHelperPanel::lookNow()
{
    switch (layoutstyle::current())
    {
        case layoutstyle::Style::clipLaunch: return theme::livelook::active() ? Look::live : Look::fl;
        case layoutstyle::Style::simple: return Look::simple;
        case layoutstyle::Style::pattern: break;
    }
    return Look::fl;
}

juce::Point<int> ChordHelperPanel::preferredSize (Look look)
{
    switch (look)
    {
        case Look::live: return { 940, 400 };
        case Look::simple: return { 1060, ui::simplelook::largeControls() ? 560 : 500 };
        case Look::fl: break;
    }
    return { 980, 470 };
}

ChordHelperPanel::ChordHelperPanel (AudioEngine& e, std::function<Target()> t, std::function<void (const std::vector<int>&)> s, Look l)
    : engine (e), target (std::move (t)), select (std::move (s)), look (l)
{
    const auto size = preferredSize (look);
    setSize (size.x, size.y);
    setRepaintsOnMouseActivity (false);
    setComponentID ("chordHelper"); // (round 190pg: --demo)
    engine.addChangeListener (this);
    compute (true);
}

ChordHelperPanel::~ChordHelperPanel()
{
    stopPreview();
    engine.removeChangeListener (this);
}

// --- the analysis ---------------------------------------------------------------------------------------------------
std::vector<int> ChordHelperPanel::selectedKeys (const Song& song, const Target& t) const
{
    std::vector<int> out;
    if (t.pattern < 0 || t.pattern >= (int) song.patterns.size())
        return out;
    const auto& notes = song.patterns[(std::size_t) t.pattern].notes;
    for (const auto i : t.selected)
        if (i >= 0 && i < (int) notes.size() && ! notes[(std::size_t) i].muted && notes[(std::size_t) i].channel == t.channel)
            out.push_back (notes[(std::size_t) i].key);
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

void ChordHelperPanel::refresh() { compute(); }

void ChordHelperPanel::changeListenerCallback (juce::ChangeBroadcaster*) { compute(); }

void ChordHelperPanel::compute (bool force)
{
    const auto t = target ? target() : Target {};
    const auto& song = engine.getSong();
    const auto now = selectedKeys (song, t);
    std::int64_t from = std::numeric_limits<std::int64_t>::max();
    std::vector<std::vector<int>> context;
    if (t.pattern >= 0 && t.pattern < (int) song.patterns.size())
    {
        const auto& notes = song.patterns[(std::size_t) t.pattern].notes;
        for (const auto i : t.selected)
            if (i >= 0 && i < (int) notes.size())
                from = std::min (from, notes[(std::size_t) i].start);
        if (! now.empty())
            context = contextChords (notes, t.channel, from, 4, t.selected);
    }
    // what this analysis reads: the keys, the context, the project key and the user's choices (FNV-1a)
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h] (std::int64_t v) {
        h ^= (std::uint64_t) v;
        h *= 1099511628211ull;
    };
    for (const auto k : now)
        mix (k);
    mix (-1);
    for (const auto& c : context)
    {
        for (const auto k : c)
            mix (k);
        mix (-2);
    }
    const auto pk = projectkey::get (song);
    mix (pk ? pk->root * 1000 + pk->flScale : -3);
    mix (forcedTonic.value_or (-4));
    mix (forcedMode ? (int) *forcedMode : -5);
    mix ((int) extension);
    if (! force && h == seen)
        return;
    seen = h;
    ++computeCount;
    currentKeys = now;
    std::optional<Scale> projectScale;
    if (pk)
    {
        // the project key as a church mode, when its notes are one (its tonic and mode from the scale's degrees)
        const auto& iv = projectkey::intervals (*pk);
        for (int m = 0; m < churchModes && ! projectScale; ++m)
        {
            const auto& st = steps ((Mode) m);
            if (iv.size() == 7 && std::equal (iv.begin(), iv.end(), st.begin()))
                projectScale = Scale { pk->root, (Mode) m };
        }
    }
    const auto previousSource = keyRow >= 0 && keyRow < (int) keys.size() ? std::optional<Scale> (keys[(std::size_t) keyRow].scale) : std::nullopt;
    result = analyse (now, context, projectScale, forcedTonic, forcedMode);
    keys = modalhelp::relatedKeys (result);
    if (pinnedFirst) // (round 190nu: the pinned keys first; the first row is then a pinned one)
        modalhelp::pinnedFirst (keys, [this] (const RelatedKey& k) { return isFavourite (favouriteId (result.key, k)); });
    keyRow = 0; // (the most common source, rank 1, even when pinned keys stand above it)
    for (int i = 0; i < (int) keys.size(); ++i)
        if (keys[(std::size_t) i].rank == 1)
            keyRow = i;
    if (previousSource) // (the same related key stays chosen while it is still listed)
        for (int i = 0; i < (int) keys.size(); ++i)
            if (keys[(std::size_t) i].scale == *previousSource)
                keyRow = i;
    chords = keys.empty() ? std::vector<Borrowed> {} : modalhelp::borrowedChords (result, keys[(std::size_t) keyRow], extension);
    order();
    hoveredChord = -1;
    chordScroll = 0;
    stopPreview();
    repaint();
}

void ChordHelperPanel::selectKeyRow (int row)
{
    if (row < 0 || row >= (int) keys.size())
        return;
    keyRow = row;
    chords = modalhelp::borrowedChords (result, keys[(std::size_t) keyRow], extension);
    order();
    chordScroll = 0;
    hoveredChord = -1;
    stopPreview();
    repaint();
}

void ChordHelperPanel::setExtension (Extension e)
{
    extension = e;
    if (keyRow >= 0 && keyRow < (int) keys.size())
        chords = modalhelp::borrowedChords (result, keys[(std::size_t) keyRow], extension);
    order();
    repaint();
}


bool ChordHelperPanel::isPinnedKey (int row) const
{
    return result.valid && row >= 0 && row < (int) keys.size() && isFavourite (favouriteId (result.key, keys[(std::size_t) row]));
}

bool ChordHelperPanel::isPinnedChord (int row) const
{
    return result.valid && row >= 0 && row < (int) chords.size() && isFavourite (favouriteId (result.key, chords[(std::size_t) row]));
}

// the pinned chords first (the related keys are put in order by compute(), where the chosen row is decided)
void ChordHelperPanel::order()
{
    if (pinnedFirst && result.valid)
        modalhelp::pinnedFirst (chords, [this] (const Borrowed& b) { return isFavourite (favouriteId (result.key, b)); });
}

void ChordHelperPanel::favouritesChanged()
{
    if (mixeroptions::changed) // (the main window writes ui-options.txt)
        mixeroptions::changed();
}

void ChordHelperPanel::togglePinKey (int row)
{
    if (row < 0 || row >= (int) keys.size() || ! result.valid)
        return;
    toggleFavourite (favouriteId (result.key, keys[(std::size_t) row]));
    favouritesChanged();
    compute (true); // (the order changes; the chosen key stays chosen)
}

void ChordHelperPanel::togglePinChord (int row)
{
    if (row < 0 || row >= (int) chords.size() || ! result.valid)
        return;
    toggleFavourite (favouriteId (result.key, chords[(std::size_t) row]));
    favouritesChanged();
    if (pinnedFirst)
        compute (true);
    else
        repaint();
}

void ChordHelperPanel::setPinnedFirst (bool on)
{
    pinnedFirst = on;
    favouritesChanged();
    compute (true);
}

void ChordHelperPanel::setInversion (Inversion i)
{
    inversion = i;
    repaint();
}

void ChordHelperPanel::overrideKey (std::optional<int> tonic, std::optional<Mode> mode)
{
    forcedTonic = tonic;
    forcedMode = mode;
    compute (true);
}

std::vector<int> ChordHelperPanel::voicedChord (int row) const
{
    if (row < 0 || row >= (int) chords.size())
        return {};
    return modalhelp::voice (chords[(std::size_t) row].chord, currentKeys, inversion);
}

// Inserts chord `row` after the selection: the selection's length and mean velocity, voice-led from it; the new chord
// becomes the selection (one undo step), so the next chord follows on from it.
bool ChordHelperPanel::insertChordRow (int row)
{
    const auto keysToWrite = voicedChord (row);
    const auto t = target ? target() : Target {};
    const auto& song = engine.getSong();
    if (keysToWrite.empty() || t.pattern < 0 || t.pattern >= (int) song.patterns.size() || currentKeys.empty())
        return false;
    const auto& notes = song.patterns[(std::size_t) t.pattern].notes;
    std::int64_t start = std::numeric_limits<std::int64_t>::max(), end = 0;
    double velocity = 0.0;
    int count = 0;
    for (const auto i : t.selected)
        if (i >= 0 && i < (int) notes.size() && notes[(std::size_t) i].channel == t.channel)
        {
            const auto& n = notes[(std::size_t) i];
            start = std::min (start, n.start);
            end = std::max (end, n.start + n.length);
            velocity += n.velocity;
            ++count;
        }
    if (count == 0)
        return false;
    const auto length = std::max<std::int64_t> (1, end - start);
    const auto vel = (float) (velocity / count);
    stopPreview();
    std::vector<Note> added;
    for (const auto k : keysToWrite)
    {
        Note n;
        n.start = end;
        n.length = length;
        n.channel = t.channel;
        n.key = k;
        n.velocity = vel;
        added.push_back (n);
    }
    engine.editSong ([p = t.pattern, added] (Song& s) {
        if (p < 0 || p >= (int) s.patterns.size())
            return;
        auto& pattern = s.patterns[(std::size_t) p];
        for (const auto& n : added)
            pattern.notes.push_back (n);
        if (pattern.lengthTicks > 0 && added.front().start + added.front().length > pattern.lengthTicks)
            pattern.lengthTicks = 0; // (the pattern grows to its notes, as a drawn note makes it)
    });
    // the new notes, wherever the edit put them
    std::vector<int> fresh;
    const auto& after = engine.getSong().patterns[(std::size_t) t.pattern].notes;
    for (int i = 0; i < (int) after.size(); ++i)
    {
        const auto& n = after[(std::size_t) i];
        if (n.start == end && n.length == length && n.channel == t.channel
            && std::find (keysToWrite.begin(), keysToWrite.end(), n.key) != keysToWrite.end())
            fresh.push_back (i);
    }
    if (select)
        select (fresh);
    compute (true);
    return true;
}

// --- preview --------------------------------------------------------------------------------------------------------
void ChordHelperPanel::startPreview (int row)
{
    stopPreview();
    if (! audition || row < 0 || row >= (int) chords.size() || currentKeys.empty())
        return;
    previewRow = row;
    previewStage = 0;
    previewTicks = 8; // (a quarter second of dwell before anything sounds)
    startTimerHz (30);
}

void ChordHelperPanel::stopPreview()
{
    for (const auto k : sounding)
        engine.sendLiveNote (soundingChannel, k, 0.0f, false);
    sounding.clear();
    previewRow = -1;
    previewStage = 0;
    stopTimer();
}

void ChordHelperPanel::timerCallback()
{
    if (--previewTicks > 0)
        return;
    for (const auto k : sounding)
        engine.sendLiveNote (soundingChannel, k, 0.0f, false);
    sounding.clear();
    const auto t = target ? target() : Target {};
    soundingChannel = t.channel;
    if (previewStage == 0 || previewStage == 1)
    {
        // the move: the selected chord, then the hovered one
        const auto keysNow = previewStage == 0 ? currentKeys : voicedChord (previewRow);
        for (const auto k : keysNow)
            if (k >= 0 && k <= 127)
            {
                engine.sendLiveNote (soundingChannel, k, 0.75f, true);
                sounding.push_back (k);
            }
        previewTicks = previewStage == 0 ? 14 : 24;
        ++previewStage;
        return;
    }
    stopPreview();
}

// --- geometry -------------------------------------------------------------------------------------------------------
int ChordHelperPanel::rowHeight() const { return palette().rowH; }
int ChordHelperPanel::headHeight() const { return palette().headH; }

juce::Rectangle<int> ChordHelperPanel::headerArea() const
{
    const auto p = palette();
    return getLocalBounds().reduced (p.gap, p.gap).removeFromTop (look == Look::simple ? 44 : look == Look::live ? 30 : 36);
}

juce::Rectangle<int> ChordHelperPanel::hintArea() const
{
    const auto p = palette();
    return getLocalBounds().reduced (p.gap, 0).removeFromBottom (look == Look::live ? 18 : 24);
}

juce::Rectangle<int> ChordHelperPanel::sideArea() const
{
    const auto p = palette();
    auto r = getLocalBounds().reduced (p.gap, 0);
    r.setTop (headerArea().getBottom() + p.gap);
    r.setBottom (hintArea().getY() - 2);
    return r.removeFromRight (look == Look::simple ? 150 : 136);
}

juce::Rectangle<int> ChordHelperPanel::keysArea() const
{
    const auto p = palette();
    auto r = getLocalBounds().reduced (p.gap, 0);
    r.setTop (headerArea().getBottom() + p.gap);
    r.setBottom (hintArea().getY() - 2);
    return r.removeFromLeft (look == Look::simple ? 300 : look == Look::live ? 270 : 290);
}

juce::Rectangle<int> ChordHelperPanel::chordsArea() const
{
    const auto p = palette();
    auto r = getLocalBounds().reduced (p.gap, 0);
    r.setTop (headerArea().getBottom() + p.gap);
    r.setBottom (hintArea().getY() - 2);
    r.setLeft (keysArea().getRight() + p.gap);
    r.setRight (sideArea().getX() - p.gap);
    return r;
}

juce::Rectangle<int> ChordHelperPanel::keyChip() const
{
    auto h = headerArea();
    const auto chipH = look == Look::simple ? 32 : look == Look::live ? 18 : 24;
    return juce::Rectangle<int> (h.getX() + 44, h.getCentreY() - chipH / 2, look == Look::simple ? 64 : 56, chipH);
}

juce::Rectangle<int> ChordHelperPanel::modeChip() const
{
    const auto k = keyChip();
    return k.withX (k.getRight() + 54).withWidth (look == Look::simple ? 150 : 132);
}

juce::Rectangle<int> ChordHelperPanel::auditionBox() const
{
    const auto h = headerArea();
    const auto k = keyChip();
    return juce::Rectangle<int> (h.getRight() - 150, k.getY(), 150, k.getHeight());
}

// round 190nu: a row's pin, at its right end (the row of list `area` scrolled by `scroll`)
juce::Rectangle<int> ChordHelperPanel::starOf (juce::Rectangle<int> area, int row, int scroll) const
{
    const auto y = area.getY() + headHeight() + (row - scroll) * rowHeight();
    const auto size = look == Look::simple ? 22 : 18;
    return { area.getRight() - size - (look == Look::simple ? 10 : 6), y + (rowHeight() - size) / 2, size, size };
}

// round 190nu: "Pinned first", on the hint line's right end
static juce::Rectangle<int> pinnedBoxOf (juce::Rectangle<int> hint) { return hint.removeFromRight (130); }

int ChordHelperPanel::keyRowAt (juce::Point<int> p) const
{
    const auto a = keysArea();
    if (! a.contains (p) || p.y < a.getY() + headHeight())
        return -1;
    const auto row = (p.y - a.getY() - headHeight()) / rowHeight() + keyScroll;
    return row < (int) keys.size() ? row : -1;
}

juce::Rectangle<int> ChordHelperPanel::demoCellBounds (int row, int column) const
{
    const auto a = row == 0 ? keysArea() : row == 1 ? chordsArea() : juce::Rectangle<int>();
    const auto count = row == 0 ? (int) keys.size() : (int) chords.size();
    const auto shown = column - (row == 0 ? keyScroll : chordScroll);
    if (a.isEmpty() || column < 0 || column >= count || shown < 0)
        return {};
    return juce::Rectangle<int> (a.getX(), a.getY() + headHeight() + shown * rowHeight(), a.getWidth(), rowHeight()).getIntersection (a);
}

int ChordHelperPanel::chordRowAt (juce::Point<int> p) const
{
    const auto a = chordsArea();
    if (! a.contains (p) || p.y < a.getY() + headHeight())
        return -1;
    const auto row = (p.y - a.getY() - headHeight()) / rowHeight() + chordScroll;
    return row < (int) chords.size() ? row : -1;
}

juce::Rectangle<int> ChordHelperPanel::sideItem (int id) const
{
    auto a = sideArea();
    const auto line = look == Look::simple ? 30 : look == Look::live ? 18 : 22;
    a.removeFromTop (headHeight());
    if (id < 10)
        return a.withHeight (line).translated (0, 2 + id * line);
    a.removeFromTop (5 * line + 8 + headHeight());
    return a.withHeight (line).translated (0, 2 + (id - 10) * line);
}

int ChordHelperPanel::sideItemAt (juce::Point<int> p) const
{
    for (int i = 0; i < 5; ++i)
        if (sideItem (i).contains (p))
            return i;
    for (int i = 0; i < 6; ++i)
        if (sideItem (10 + i).contains (p))
            return 10 + i;
    return -1;
}

void ChordHelperPanel::resized() {}

// --- texts ----------------------------------------------------------------------------------------------------------
juce::String ChordHelperPanel::keyText (const Scale& s) const
{
    const auto tonic = juce::String::fromUTF8 (spelledName (spellDegree (s, 0)).c_str());
    if (s.mode == Mode::ionian)
        return tr ("{0} major", { tonic });
    if (s.mode == Mode::aeolian)
        return tr ("{0} minor", { tonic });
    return tonic + " " + tr (modeName (s.mode));
}

juce::String ChordHelperPanel::signatureText (const Scale& s) const
{
    const auto sig = signatureOf (s);
    juce::StringArray names;
    for (const auto& n : sig.notes)
        names.add (juce::String::fromUTF8 (spelledName (n).c_str()));
    juce::String text;
    if (sig.notes.empty())
        text = tr ("No sharps or flats");
    else if (sig.sharps > 0)
        text = sig.sharps == 1 ? tr ("1 sharp ({0})", { names.joinIntoString (", ") }) : tr ("{0} sharps ({1})", { juce::String (sig.sharps), names.joinIntoString (", ") });
    else
        text = sig.flats == 1 ? tr ("1 flat ({0})", { names.joinIntoString (", ") }) : tr ("{0} flats ({1})", { juce::String (sig.flats), names.joinIntoString (", ") });
    if (sig.raised)
        text += " + " + juce::String::fromUTF8 (spelledName (*sig.raised).c_str());
    return text;
}

juce::String ChordHelperPanel::chordText (const Borrowed& b) const
{
    return juce::String::fromUTF8 ((spelledName (b.root) + chordSuffix (b.chord.intervals)).c_str());
}

juce::String ChordHelperPanel::movementText (const Borrowed& b) const
{
    if (b.move.up == 0)
        return tr ("Same root");
    const auto up = tr (intervalName (b.move.up)), down = tr (intervalName (b.move.down));
    if (b.move.up == 6)
        return tr ("A tritone up or down");
    return b.move.upFirst ? tr ("Up a {0} (or down a {1})", { up, down }) : tr ("Down a {0} (or up a {1})", { down, up });
}

juce::String ChordHelperPanel::detailText (const Borrowed& b) const
{
    juce::String text;
    if (b.quality != result.quality)
        text = tr (qualityName (result.quality)) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + tr (qualityName (b.quality));
    else
        text = tr ("same quality ({0})", { tr (qualityName (b.quality)) });
    text += juce::String::fromUTF8 ("  \xc2\xb7  ");
    text += b.commonTones == 0 ? tr ("no common tones") : b.commonTones == 1 ? tr ("1 common tone") : tr ("{0} common tones", { juce::String (b.commonTones) });
    if (! b.target.empty() && b.secondaryDominant)
        text += juce::String::fromUTF8 ("  \xc2\xb7  ") + tr ("leads to {0}", { juce::String::fromUTF8 (b.target.c_str()) });
    return text;
}

juce::String ChordHelperPanel::moodText (const Borrowed& b) const
{
    juce::StringArray words;
    for (const auto* w : b.mood)
        words.add (tr (w));
    auto text = words.joinIntoString (", ");
    if (text.isNotEmpty())
        text = text.substring (0, 1).toUpperCase() + text.substring (1);
    return text;
}

// --- painting -------------------------------------------------------------------------------------------------------
void ChordHelperPanel::paint (juce::Graphics& g)
{
    const auto p = palette();
    g.fillAll (p.back);
    const auto font = [&p] (float size, bool bold = false) { return theme::font (size, bold); };
    const auto box = [&g, &p] (juce::Rectangle<int> r, juce::Colour fill, juce::Colour edge = {}) {
        g.setColour (fill);
        if (p.radius > 0.0f)
            g.fillRoundedRectangle (r.toFloat(), p.radius);
        else
            g.fillRect (r);
        if (! edge.isTransparent())
        {
            g.setColour (edge);
            if (p.radius > 0.0f)
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), p.radius, 1.0f);
            else
                g.drawRect (r);
        }
    };

    // round 190nu: a pin (a five-point star), filled in the accent when pinned, an outline on the hovered row
    const auto star = [&g, &p] (juce::Rectangle<int> r, bool pinned, bool hot, juce::Colour ink) {
        if (! pinned && ! hot)
            return;
        juce::Path path;
        path.addStar (r.toFloat().getCentre(), 5, (float) r.getWidth() * 0.22f, (float) r.getWidth() * 0.48f);
        if (pinned)
        {
            g.setColour (p.accent);
            g.fillPath (path);
        }
        else
        {
            g.setColour (ink.withAlpha (0.75f));
            g.strokePath (path, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved));
        }
    };

    // header: Key [C] Mode [Ionian] Chord C (I) — the context — Audition
    {
        const auto h = headerArea();
        if (look != Look::live)
            box (h, p.headFill);
        const auto k = keyChip(), m = modeChip();
        g.setFont (font (p.small));
        g.setColour (p.dim);
        g.drawText (tr ("Key"), juce::Rectangle<int> (h.getX() + 8, h.getY(), k.getX() - h.getX() - 12, h.getHeight()), juce::Justification::centredRight);
        g.drawText (tr ("Mode"), juce::Rectangle<int> (k.getRight() + 4, h.getY(), m.getX() - k.getRight() - 10, h.getHeight()), juce::Justification::centredRight);
        const auto chip = [&] (juce::Rectangle<int> r, const juce::String& text, bool forced) {
            box (r, look == Look::live ? theme::livelook::well : p.hover, forced ? p.accent : juce::Colour());
            g.setColour (p.text);
            g.setFont (font (p.body, true));
            g.drawText (text, r.reduced (6, 0).withTrimmedRight (10), juce::Justification::centredLeft, true);
            juce::Path arrow; // (▾: a menu)
            const auto c = juce::Point<float> ((float) r.getRight() - 9.0f, (float) r.getCentreY());
            arrow.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
            g.setColour (p.dim);
            g.fillPath (arrow);
        };
        if (result.valid)
        {
            chip (k, juce::String::fromUTF8 (spelledName (spellDegree (result.key, 0)).c_str()), result.forcedTonic);
            chip (m, tr (modeName (result.key.mode)), result.forcedMode);
            auto rest = juce::Rectangle<int> (m.getRight() + 14, h.getY(), auditionBox().getX() - m.getRight() - 20, h.getHeight());
            g.setColour (p.dim);
            g.setFont (font (p.small));
            g.drawText (tr ("Chord"), rest.removeFromLeft (44), juce::Justification::centredLeft);
            const auto name = juce::String::fromUTF8 ((spelledName (spellAs (result.chord.root, tonicLetter (result.key))) + chordSuffix (result.chord.intervals)).c_str());
            const auto roman = juce::String::fromUTF8 (numeral (result.key, result.chord.root, result.quality).c_str());
            g.setColour (p.text);
            g.setFont (font (p.body + 1.0f, true));
            const auto nameW = juce::jmin (rest.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name + " (" + roman + ")") + 12);
            g.drawText (name + " (" + roman + ")", rest.removeFromLeft (nameW), juce::Justification::centredLeft, true);
            g.setColour (p.dim);
            g.setFont (font (p.caption));
            const auto context = result.contextChords == 0 ? tr ("no chords before it")
                               : result.contextChords == 1 ? tr ("read with 1 chord before it")
                                                           : tr ("read with {0} chords before it", { juce::String (result.contextChords) });
            g.drawFittedText (context, rest, juce::Justification::centredLeft, 1, 0.8f);
        }
        else
        {
            g.setColour (p.dim);
            g.setFont (font (p.body));
            g.drawText (tr ("Select a chord in the Piano roll: two or more notes that play together"),
                        h.withTrimmedLeft (12).withTrimmedRight (auditionBox().getWidth() + 8), juce::Justification::centredLeft, true);
        }
        // audition switch
        const auto a = auditionBox();
        const auto tick = a.withWidth (a.getHeight()).reduced (look == Look::simple ? 6 : 3);
        box (tick, audition ? p.accent : (look == Look::live ? theme::livelook::well : p.hover));
        if (audition)
        {
            juce::Path mark;
            const auto t = tick.toFloat().reduced (tick.getWidth() * 0.22f);
            mark.startNewSubPath (t.getX(), t.getCentreY());
            mark.lineTo (t.getX() + t.getWidth() * 0.4f, t.getBottom());
            mark.lineTo (t.getRight(), t.getY());
            g.setColour (p.onAccent);
            g.strokePath (mark, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (p.text);
        g.setFont (font (p.small));
        g.drawText (tr ("Hear on hover"), a.withTrimmedLeft (a.getHeight() + 4), juce::Justification::centredLeft, true);
    }

    const auto listFrame = [&] (juce::Rectangle<int> r, const juce::String& title, const std::vector<std::pair<juce::String, int>>& columns) {
        box (r, p.panel, look == Look::live ? juce::Colour() : p.border);
        auto head = r.removeFromTop (p.headH);
        if (look == Look::live)
            g.setColour (p.headFill), g.fillRect (head);
        g.setColour (look == Look::live ? p.text : p.dim);
        g.setFont (font (p.caption, true));
        auto x = head.getX() + 6;
        for (const auto& [text, w] : columns)
        {
            g.drawText (text, juce::Rectangle<int> (x, head.getY(), w <= 0 ? head.getRight() - x - 4 : w - 4, head.getHeight()), juce::Justification::centredLeft, true);
            x += w;
        }
        juce::ignoreUnused (title);
    };

    // panel 1: the related keys
    {
        const auto a = keysArea();
        listFrame (a, tr ("Related keys"), { { "#", 24 }, { tr ("Key"), 128 }, { tr ("Key signature"), 0 } });
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (a.withTrimmedTop (headHeight()).reduced (1, 1));
        for (int i = keyScroll; i < (int) keys.size(); ++i)
        {
            const auto y = a.getY() + headHeight() + (i - keyScroll) * rowHeight();
            if (y > a.getBottom())
                break;
            auto r = juce::Rectangle<int> (a.getX() + 1, y, a.getWidth() - 2, rowHeight()).reduced (look == Look::simple ? 4 : 0, look == Look::simple ? 2 : 0);
            const auto selected = i == keyRow;
            const auto pinned = isPinnedKey (i);
            auto fill = selected ? p.selected : i == hoveredKey ? p.hover : (i % 2 == 0 ? p.rowFill : p.rowAlt);
            if (pinned && ! selected) // (round 190nu: a pinned row is tinted)
                fill = fill.interpolatedWith (p.accent, look == Look::live ? 0.10f : 0.12f);
            box (r, fill, selected && look != Look::live ? p.selectedEdge : juce::Colour());
            const auto ink = selected && look == Look::live ? p.onAccent : p.text;
            const auto dim = selected && look == Look::live ? p.onAccent.withAlpha (0.7f) : p.dim;
            const auto& k = keys[(std::size_t) i];
            auto x = r.getX() + 6;
            g.setColour (dim);
            g.setFont (font (p.small));
            g.drawText (juce::String (k.rank), juce::Rectangle<int> (x, r.getY(), 20, r.getHeight()), juce::Justification::centredLeft);
            x += 24;
            const auto two = r.getHeight() / 2;
            g.setColour (ink);
            g.setFont (font (p.body, true));
            g.drawText (keyText (k.scale), juce::Rectangle<int> (x, r.getY() + 2, 124, two), juce::Justification::bottomLeft, true);
            g.setColour (dim);
            g.setFont (font (p.caption));
            g.drawText (tr (relationName (k.relation, result.key.mode, k.scale.mode)), juce::Rectangle<int> (x, r.getY() + two, 124, two - 2),
                        juce::Justification::topLeft, true);
            x += 128;
            g.setColour (ink);
            g.setFont (font (p.small));
            const auto pin = starOf (a, i, keyScroll);
            g.drawFittedText (signatureText (k.scale), juce::Rectangle<int> (x, r.getY(), pin.getX() - x - 4, r.getHeight()), juce::Justification::centredLeft, 2, 0.85f);
            star (pin, pinned, i == hoveredKey, ink);
        }
        if (keys.empty() && result.valid)
        {
            g.setColour (p.dim);
            g.setFont (font (p.small));
            g.drawFittedText (tr ("Nothing to borrow here"), a.withTrimmedTop (headHeight()).reduced (10), juce::Justification::centredTop, 2);
        }
    }

    // panel 2: the borrowed chords
    {
        const auto a = chordsArea();
        const auto root = result.valid ? juce::String::fromUTF8 (spelledName (spellAs (result.chord.root, tonicLetter (result.key))).c_str()) : juce::String ("-");
        const auto chordW = 92, moveW = juce::jmax (200, (a.getWidth() - 24 - chordW) * 60 / 100);
        listFrame (a, tr ("Borrowed chords"), { { "#", 24 }, { tr ("Chord"), chordW }, { tr ("Movement from {0}", { root }), moveW }, { tr ("Mood"), 0 } });
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (a.withTrimmedTop (headHeight()).reduced (1, 1));
        for (int i = chordScroll; i < (int) chords.size(); ++i)
        {
            const auto y = a.getY() + headHeight() + (i - chordScroll) * rowHeight();
            if (y > a.getBottom())
                break;
            auto r = juce::Rectangle<int> (a.getX() + 1, y, a.getWidth() - 2, rowHeight()).reduced (look == Look::simple ? 4 : 0, look == Look::simple ? 2 : 0);
            const auto hot = i == hoveredChord;
            const auto pinned = isPinnedChord (i);
            auto fill = hot ? (look == Look::live ? p.selected : p.selected) : (i % 2 == 0 ? p.rowFill : p.rowAlt);
            if (pinned && ! hot) // (round 190nu)
                fill = fill.interpolatedWith (p.accent, look == Look::live ? 0.10f : 0.12f);
            box (r, fill, hot && look != Look::live ? p.selectedEdge : juce::Colour());
            const auto ink = hot && look == Look::live ? p.onAccent : p.text;
            const auto dim = hot && look == Look::live ? p.onAccent.withAlpha (0.7f) : p.dim;
            const auto& b = chords[(std::size_t) i];
            const auto two = r.getHeight() / 2;
            auto x = r.getX() + 6;
            g.setColour (dim);
            g.setFont (font (p.small));
            g.drawText (juce::String (b.rank), juce::Rectangle<int> (x, r.getY(), 20, r.getHeight()), juce::Justification::centredLeft);
            x += 24;
            g.setColour (ink);
            g.setFont (font (p.body + 1.0f, true));
            g.drawText (chordText (b), juce::Rectangle<int> (x, r.getY() + 2, chordW - 6, two), juce::Justification::bottomLeft, true);
            g.setColour (dim);
            g.setFont (font (p.caption));
            g.drawText (juce::String::fromUTF8 (b.numeral.c_str()), juce::Rectangle<int> (x, r.getY() + two, chordW - 6, two - 2), juce::Justification::topLeft, true);
            x += chordW;
            g.setColour (ink);
            g.setFont (font (p.small));
            g.drawFittedText (movementText (b), juce::Rectangle<int> (x, r.getY() + 2, moveW - 8, two), juce::Justification::bottomLeft, 1, 0.8f);
            g.setColour (dim);
            g.setFont (font (p.caption));
            g.drawFittedText (detailText (b), juce::Rectangle<int> (x, r.getY() + two, moveW - 8, two - 2), juce::Justification::topLeft, 1, 0.8f);
            x += moveW;
            g.setColour (ink);
            g.setFont (font (p.small));
            const auto pin = starOf (a, i, chordScroll);
            g.drawFittedText (moodText (b), juce::Rectangle<int> (x, r.getY(), pin.getX() - x - 4, r.getHeight()), juce::Justification::centredLeft, 2, 0.85f);
            star (pin, pinned, hot, ink);
        }
        if (chords.empty() && result.valid)
        {
            g.setColour (p.dim);
            g.setFont (font (p.small));
            g.drawFittedText (tr ("Every chord of this key is already in the home key"), a.withTrimmedTop (headHeight()).reduced (10), juce::Justification::centredTop, 2);
        }
        // a scroll hint: more rows below
        const auto shown = (a.getHeight() - headHeight()) / juce::jmax (1, rowHeight());
        if ((int) chords.size() - chordScroll > shown)
        {
            g.setColour (p.dim);
            g.setFont (font (p.caption));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe ") + tr ("{0} more", { juce::String ((int) chords.size() - chordScroll - shown) }),
                        a.withTrimmedTop (a.getHeight() - 14).withTrimmedRight (6), juce::Justification::centredRight);
        }
    }

    // the side menu: inversion and extension
    {
        const auto a = sideArea();
        box (a, p.panel, look == Look::live ? juce::Colour() : p.border);
        const auto title = [&] (juce::Rectangle<int> r, const juce::String& text) {
            if (look == Look::live)
                g.setColour (p.headFill), g.fillRect (r);
            g.setColour (look == Look::live ? p.text : p.dim);
            g.setFont (font (p.caption, true));
            g.drawText (text, r.reduced (6, 0), juce::Justification::centredLeft, true);
        };
        title (a.withHeight (headHeight()), tr ("Inversion"));
        const auto second = sideItem (10).getY() - headHeight() - 4;
        title (juce::Rectangle<int> (a.getX(), second, a.getWidth(), headHeight()), tr ("Extension"));
        const auto item = [&] (int id, const juce::String& text, bool on) {
            const auto r = sideItem (id).reduced (4, 1);
            if (on)
                box (r, look == Look::live ? p.accent : p.selected, look == Look::live ? juce::Colour() : p.selectedEdge);
            else if (id == hoveredSide)
                box (r, p.hover);
            g.setColour (on && look == Look::live ? p.onAccent : p.text);
            g.setFont (font (p.small, on));
            g.drawText (text, r.reduced (6, 0), juce::Justification::centredLeft, true);
        };
        for (int i = 0; i < 5; ++i)
            item (i, tr (inversionName ((Inversion) i)), inversion == (Inversion) i);
        for (int i = 0; i < 6; ++i)
            item (10 + i, tr (extensionName ((Extension) i)), extension == (Extension) i);
    }

    // the hint line, and "Pinned first" at its right end (round 190nu)
    const auto pinBox = pinnedBoxOf (hintArea());
    g.setColour (p.dim);
    g.setFont (font (p.caption));
    g.drawFittedText (tr ("Click a chord to insert it after the selection. Click a key to see what it lends. The star pins a relationship in every key."),
                      hintArea().withTrimmedRight (pinBox.getWidth() + 8), juce::Justification::centredLeft, 1, 0.8f);
    {
        const auto tick = pinBox.withWidth (pinBox.getHeight()).reduced (look == Look::simple ? 5 : 4);
        box (tick, pinnedFirst ? p.accent : (look == Look::live ? theme::livelook::well : p.hover));
        if (pinnedFirst)
        {
            juce::Path mark;
            const auto t = tick.toFloat().reduced (tick.getWidth() * 0.22f);
            mark.startNewSubPath (t.getX(), t.getCentreY());
            mark.lineTo (t.getX() + t.getWidth() * 0.4f, t.getBottom());
            mark.lineTo (t.getRight(), t.getY());
            g.setColour (p.onAccent);
            g.strokePath (mark, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (p.text);
        g.setFont (font (p.small));
        g.drawText (tr ("Pinned first"), pinBox.withTrimmedLeft (pinBox.getHeight() + 2), juce::Justification::centredLeft, true);
    }
}

juce::String ChordHelperPanel::getTooltip()
{
    const auto m = getMouseXYRelative();
    if (keyChip().contains (m))
        return tr ("The tonic: the selected chord's root (click to choose another)");
    if (modeChip().contains (m))
        return tr ("The mode the chord and the chords before it imply (click to choose another)");
    if (const auto row = chordRowAt (m); row >= 0 && starOf (chordsArea(), row, chordScroll).contains (m))
        return isPinnedChord (row) ? tr ("Unpin this relationship") : tr ("Pin this relationship: highlighted in every key, first with \"Pinned first\"");
    if (const auto row = keyRowAt (m); row >= 0 && starOf (keysArea(), row, keyScroll).contains (m))
        return isPinnedKey (row) ? tr ("Unpin this relationship") : tr ("Pin this relationship: highlighted in every key, first with \"Pinned first\"");
    if (pinnedBoxOf (hintArea()).contains (m))
        return tr ("Pinned relationships first in their lists (off: only highlighted)");
    if (const auto row = chordRowAt (m); row >= 0)
        return tr ("[LMB] insert {0} after the selection", { chordText (chords[(std::size_t) row]) });
    if (const auto row = keyRowAt (m); row >= 0)
        return tr ("[LMB] show the chords this key lends");
    return {};
}

// --- mouse ----------------------------------------------------------------------------------------------------------
void ChordHelperPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto k = keyRowAt (e.getPosition()), c = chordRowAt (e.getPosition()), s = sideItemAt (e.getPosition());
    if (k != hoveredKey || s != hoveredSide)
    {
        hoveredKey = k;
        hoveredSide = s;
        repaint();
    }
    if (c != hoveredChord)
    {
        hoveredChord = c;
        if (c >= 0)
            startPreview (c);
        else
            stopPreview();
        repaint();
    }
}

void ChordHelperPanel::mouseExit (const juce::MouseEvent&)
{
    hoveredKey = hoveredChord = hoveredSide = -1;
    stopPreview();
    repaint();
}

void ChordHelperPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto at = e.getPosition();
    if (keyChip().contains (at) && result.valid)
        return showKeyMenu();
    if (modeChip().contains (at) && result.valid)
        return showModeMenu();
    if (auditionBox().contains (at))
    {
        audition = ! audition;
        if (! audition)
            stopPreview();
        repaint();
        return;
    }
    if (pinnedBoxOf (hintArea()).contains (at)) // (round 190nu)
        return setPinnedFirst (! pinnedFirst);
    if (const auto k = keyRowAt (at); k >= 0 && starOf (keysArea(), k, keyScroll).contains (at))
        return togglePinKey (k);
    if (const auto c = chordRowAt (at); c >= 0 && starOf (chordsArea(), c, chordScroll).contains (at))
        return togglePinChord (c);
    if (const auto s = sideItemAt (at); s >= 0)
    {
        if (s < 10)
            setInversion ((Inversion) s);
        else
            setExtension ((Extension) (s - 10));
        return;
    }
    if (const auto k = keyRowAt (at); k >= 0)
        return selectKeyRow (k);
    if (const auto c = chordRowAt (at); c >= 0)
        insertChordRow (c);
}

void ChordHelperPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto step = wheel.deltaY < 0 ? 1 : wheel.deltaY > 0 ? -1 : 0;
    const auto clampScroll = [this] (int value, int count, juce::Rectangle<int> area) {
        const auto shown = (area.getHeight() - headHeight()) / juce::jmax (1, rowHeight());
        return juce::jlimit (0, juce::jmax (0, count - shown), value);
    };
    if (keysArea().contains (e.getPosition()))
        keyScroll = clampScroll (keyScroll + step, (int) keys.size(), keysArea());
    else if (chordsArea().contains (e.getPosition()))
        chordScroll = clampScroll (chordScroll + step, (int) chords.size(), chordsArea());
    repaint();
}

void ChordHelperPanel::showKeyMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<ChordHelperPanel> safe (this);
    menu.addItem (tr ("Auto: the chord's root"), true, ! forcedTonic, [safe] {
        if (safe != nullptr)
            safe->overrideKey (std::nullopt, safe->forcedMode);
    });
    menu.addSeparator();
    for (int t = 0; t < 12; ++t)
        menu.addItem (juce::String::fromUTF8 (spelledName (spellDegree ({ t, result.key.mode }, 0)).c_str()), true, forcedTonic && *forcedTonic == t, [safe, t] {
            if (safe != nullptr)
                safe->overrideKey (t, safe->forcedMode);
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (keyChip())));
}

void ChordHelperPanel::showModeMenu()
{
    juce::PopupMenu menu;
    const juce::Component::SafePointer<ChordHelperPanel> safe (this);
    menu.addItem (tr ("Auto: from the chord and the chords before it"), true, ! forcedMode, [safe] {
        if (safe != nullptr)
            safe->overrideKey (safe->forcedTonic, std::nullopt);
    });
    menu.addSeparator();
    // the modes that hold the chord first (their fit with the context decides the order)
    std::vector<int> order (churchModes);
    for (int m = 0; m < churchModes; ++m)
        order[(std::size_t) m] = m;
    std::stable_sort (order.begin(), order.end(), [this] (int x, int y) { return result.modeScore[(std::size_t) x] > result.modeScore[(std::size_t) y]; });
    for (const auto m : order)
        menu.addItem (tr (modeName ((Mode) m)), true, result.key.mode == (Mode) m && forcedMode, [safe, m] {
            if (safe != nullptr)
                safe->overrideKey (safe->forcedTonic, (Mode) m);
        });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (modeChip())));
}
} // namespace lynx
