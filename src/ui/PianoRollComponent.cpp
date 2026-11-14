#include "ui/FlMods.h"
#include "ui/Motion.h"
#include "ui/FileChoosers.h"
#include "ui/PianoRollComponent.h"
#include "ui/PaintAB.h"
#include "core/ChordReadout.h"
#include "core/KeyMap.h"
#include "core/NoteNames.h"

#include "core/TransportEdit.h"
#include "ui/UiTrace.h"
#include "ui/TextCache.h"

#include "core/EditorPolish.h"
#include "core/EventEdit.h"
#include "core/MarkerPlan.h"
#include "core/NoteEdit.h"
#include "core/NoteTools.h"
#include "core/Rack.h"

#include <array>
#include "engine/MidiFileIo.h"
#include "engine/Mp3Format.h"
#include "core/PianoRollTools.h"
#include "ui/BrowserComponent.h"
#include "ui/Dialogs.h"
#include "ui/MidiClipboard.h"
#include "ui/MidiToolsPanel.h"
#include "ui/ToolWindow.h"
#include "engine/Text.h"

#include <optional>
#include "ui/AutomationUi.h"
#include "ui/EventEditor.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
bool isBlackKey (int key)
{
    const auto n = key % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// FL-style name: key 60 = C5.
juce::String keyName (int key)
{
    return juce::String (notenames::keyName (key)); // Settings › General › Note names (round 165f)
}

// (round 190ea: never out of range: an empty pattern when a hand-built song has none; an edit keeps one first)
const Pattern& currentPattern (const Song& s)
{
    return s.activePattern();
}

Pattern& currentPattern (Song& s)
{
    return s.activePattern();
}

// "\t" + the key map's shortcut of `id` for a menu item (empty when unbound or no key map is connected)
juce::String keyText (const std::string& id)
{
    return PianoRollComponent::shortcutText ? PianoRollComponent::shortcutText (id) : juce::String();
}

struct NamedIntervals
{
    const char* name;
    std::vector<int> intervals;
};

const std::vector<NamedIntervals>& chords()
{
    static const std::vector<NamedIntervals> list {
        { TR_NOOP ("Note"), { 0 } }, { TR_NOOP ("Major"), { 0, 4, 7 } }, { TR_NOOP ("Minor"), { 0, 3, 7 } },     { "Dim", { 0, 3, 6 } },
        { "Aug", { 0, 4, 8 } },    { "Sus2", { 0, 2, 7 } },      { "Sus4", { 0, 5, 7 } },      { "7", { 0, 4, 7, 10 } },
        { "Maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } }, { "m9", { 0, 3, 7, 10, 14 } }, { TR_NOOP ("Octave"), { 0, 12 } },
    };
    return list;
}

const std::vector<notetools::ScaleInfo>& scales()
{
    return notetools::scales();
}

// a detected chord in the chosen note names (Settings › General, round 165f): "Do m7/Mib"; "" when none
juce::String chordName (const std::vector<int>& keys)
{
    // FL's own names (round 181b, core/ChordReadout.h): the same for the panel, this readout and the bubble
    if ((int) keys.size() < chordread::minKeys || (int) keys.size() > chordread::maxKeys)
        return {};
    std::array<std::string, 12> notes;
    for (int pc = 0; pc < 12; ++pc)
    {
        auto name = juce::String (notenames::pitchName (pc));
        if (name.endsWithChar ('#'))
            name = name.dropLastCharacters (1) + juce::String::charToString ((juce::juce_wchar) 0x266F);
        else if (name.length() > 1 && name.endsWithChar ('b'))
            name = name.dropLastCharacters (1) + juce::String::charToString ((juce::juce_wchar) 0x266D);
        notes[(std::size_t) pc] = name.toStdString();
    }
    chordread::NameOptions options;
    options.notes = &notes;
    const auto named = chordread::nameKeys (keys, options);
    const auto text = juce::String::fromUTF8 (named.text.c_str());
    return named.novelty ? tr (text) : text;
}

// View › Background waveform (round 168g): one for every editor, like FL's (PTR_DAT_015e2698 / DAT_016ba824)
struct BackgroundWave
{
    juce::File file;
    std::vector<float> lo, hi; // per peak block
    double sampleRate = 44100.0;
    std::int64_t samples = 0;
    static constexpr int perPeak = 256;
};

std::shared_ptr<const BackgroundWave>& backgroundWave()
{
    static std::shared_ptr<const BackgroundWave> wave;
    return wave;
}

bool& backgroundWaveOn()
{
    static bool on = false;
    return on;
}

bool isScoreFile (const juce::File& f) { return f.hasFileExtension ("fsc"); }
bool isMidiFile (const juce::File& f) { return f.hasFileExtension ("mid;midi"); }

} // namespace


PianoRollComponent::PianoRollComponent (AudioEngine& e)
    : engine (e)
{
    auto sender = [this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on); };
    previewNotes.setSender (sender);
    listenNotes.setSender (sender);
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    for (auto* bar : { &hBar, &vBar })
    {
        bar->addListener (this);
        bar->setAutoHide (false);
        addAndMakeVisible (bar);
    }

    // the overview strip is the horizontal scroller (FL's Piano Roll has its preview bar on top)
    hBar.setVisible (false);
    overview.paintContent = [this] (juce::Graphics& g, juce::Rectangle<float> area, double total) { paintOverview (g, area, total); };
    overview.onChange = [this] (double start, double length) {
        const auto width = (double) gridArea().getWidth();
        pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, width / juce::jmax (1.0, length));
        scrollTicks = juce::jmax (0.0, start);
        updateScrollBars();
        repaint();
    };
    overview.onShowAll = [this] { zoomAll(); };
    addAndMakeVisible (overview);

    grid::fillEditorBox (snapBox);
    snapBox.setTooltip (tr ("Piano Roll snap (magnet): \"Main\" follows the snap in the toolbar, or pick this window's own; hold Alt while dragging to ignore it"));
    ui::setQuickTip (snapBox, tr ("Piano Roll snap"));
    snapBox.setWantsKeyboardFocus (false);
    snapBox.setMouseClickGrabsKeyboardFocus (false);
    snapBox.onChange = [this] { repaint(); };
    addAndMakeVisible (snapBox);

    tools.onChange = [this] (EditTool) {
        stopListening();
        updateCursor (juce::ModifierKeys::currentModifiers);
    };
    addAndMakeVisible (tools);

    channelBox.setTooltip (tr ("Channel being edited (marked: has notes in this pattern); \"All\" at the top edits every channel's notes at once. Wheel: channels with notes; Ctrl+wheel: all"));
    channelBox.setWantsKeyboardFocus (false);
    channelBox.setMouseClickGrabsKeyboardFocus (false);
    channelBox.onChange = [this] {
        if (channelBox.getSelectedId() == allChannelsId)
        {
            setAllChannels (true);
            return;
        }
        const auto channel = channelBox.getSelectedId() - 1;
        setAllChannels (false);
        if (channel >= 0 && channel < (int) engine.getSong().channels.size() && channel != engine.getSelectedChannel())
            engine.setSelectedChannel (channel);
    };
    channelBox.onStep = [this] (int direction, bool all) {
        setAllChannels (false);
        stepChannel (direction, ! all);
    };
    addAndMakeVisible (channelBox);

    ghostButton.setClickingTogglesState (true);
    ghostButton.setToggleState (showGhosts, juce::dontSendNotification);
    ghostButton.setOnColour (theme::colour::bg6);
    ghostButton.setIconColourWhenOn (theme::colour::textPrimary);
    ghostButton.setFlat (true);
    ghostButton.setTooltip (tr ("Ghost notes: show the other channels' notes (Alt+V) and, outlined, this channel's notes in the clips "
                                "playing at the same time; double right-click one to edit its channel; Ctrl+Alt+V makes them editable"));
    ui::setQuickTip (ghostButton, tr ("Ghost notes"), "Alt+V");
    ui::setQuickTip (moreButton, tr ("Note tools"));
    ui::setQuickTip (scaleSnapButton, tr ("Snap to scale"));
    ghostButton.onClick = [this] {
        showGhosts = ghostButton.getToggleState();
        repaint();
    };
    ghostButton.onRightClick = [this] { // FL: the ghost notes button's menu picks the channels (round 166d)
        ghostChannelsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&ghostButton));
    };

    moreButton.setFlat (true);
    moreButton.setTooltip (tr ("Note operations, left-edge resizing and MIDI import/export"));
    moreButton.onClick = [this] { showMoreMenu(); };

    scaleSnapButton.setOnColour (theme::colour::bg6);
    scaleSnapButton.setIconColourWhenOn (theme::colour::accent);
    scaleSnapButton.setFlat (true);
    scaleSnapButton.setTooltip (tr ("Snap notes to the scale: new and moved notes, and incoming MIDI, land on the chosen scale"));
    scaleSnapButton.onClick = [this] { setScaleSnap (! scaleSnap); };

    // Follow (FL: "Auto scrolling"): the view pages along with the playhead while the pattern plays
    followButton.setOnColour (theme::colour::bg6);
    followButton.setIconColourWhenOn (theme::colour::accent);
    followButton.setFlat (true);
    followButton.setClickingTogglesState (false);
    followButton.setToggleState (EditorGlobals::get().isFollowing (true), juce::dontSendNotification);
    followButton.setTooltip (tr ("Follow: the Piano Roll scrolls along with the playhead while this pattern plays; off, you can look around freely"));
    ui::setQuickTip (followButton, tr ("Follow the playhead"));
    followButton.onClick = [this] { runAction ("pianoRoll.follow"); };

    playButton.setFlat (true);
    playButton.setClickingTogglesState (false);
    playButton.setIconColourWhenOn (theme::colour::accent); // FL: stays a play icon, in the theme colour while playing
    playButton.setTooltip (tr ("Play / pause the pattern  [RMB] stop and go back to where playback started (again: to the pattern start)"));
    playButton.onClick = [this] { if (onPlayPause) onPlayPause(); };
    playButton.onRightClick = [this] { if (onPlayRightClick) onPlayRightClick(); };

    for (auto* b : { &playButton, &ghostButton, &scaleSnapButton, &moreButton, &followButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    for (int i = 0; i < (int) chords().size(); ++i)
        chordBox.addItem (tr (chords()[(std::size_t) i].name), i + 1);
    chordBox.setSelectedId (1, juce::dontSendNotification);
    chordBox.setTooltip (tr ("Clicking creates this chord (the clicked note is the root)"));

    // scale: root x type
    for (int s = 0; s < (int) scales().size(); ++s)
    {
        if (s == 0)
        {
            scaleBox.addItem (tr (scales()[0].name), 1);
            continue;
        }

        for (int root = 0; root < 12; ++root)
            scaleBox.addItem (tr ("{0} {1}", { juce::String (notenames::pitchName (root)), tr (scales()[(std::size_t) s].name) }), s * 100 + root + 1);
    }
    scaleBox.setSelectedId (1, juce::dontSendNotification);
    scaleSnapButton.setEnabled (false); // until a scale is chosen
    scaleBox.setTooltip (tr ("Highlights the notes of a scale"));

    // round 170e: an icon (FL's slide tool is one) instead of the word "Slide"
    slideButton.setClickingTogglesState (true);
    slideButton.setTooltip (tr ("New notes are \"slide\" notes: the currently sounding note glides to them (808 glide)"));
    ui::setQuickTip (slideButton, tr ("Slide notes"));
    slideButton.setWantsKeyboardFocus (false);
    slideButton.setMouseClickGrabsKeyboardFocus (false);
    slideButton.setOnColour (theme::colour::bg6);
    slideButton.setIconColourWhenOn (theme::colour::accent);
    slideButton.setFlat (true);
    addAndMakeVisible (slideButton);
    ui::setQuickTip (chordBox, tr ("Chord stamp"));
    ui::setQuickTip (scaleBox, tr ("Scale"));
    chordBox.iconLit = [this] { return chordBox.getSelectedId() > 1; };
    scaleBox.iconLit = [this] { return scaleIndex() > 0; };

    colourButton.setTooltip (tr ("Note colour: new notes get this colour. Click: choose it, colour the selection (Alt+C), "
                                 "select by colour (Shift+C)"));
    ui::setQuickTip (colourButton, tr ("Note colour"));
    colourButton.setWantsKeyboardFocus (false);
    colourButton.setMouseClickGrabsKeyboardFocus (false);
    colourButton.onClick = [this] { showColourMenu(); };
    addAndMakeVisible (colourButton);

    chordLabel.setJustificationType (juce::Justification::centred);
    chordLabel.setFont (theme::font (theme::type::caption, true));
    chordLabel.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    chordLabel.setTooltip (tr ("Chord: the chord of the keys held on the MIDI keyboard or the typing piano, else of the selected notes"));
    chordLabel.setInterceptsMouseClicks (true, false);
    addAndMakeVisible (chordLabel);

    // "Show chord name for selection" (round 168g): FL shows it as a hint 16 px into the editor's top-left corner
    chordBubble.setJustificationType (juce::Justification::centred);
    chordBubble.setFont (theme::font (theme::type::body, true));
    chordBubble.setColour (juce::Label::textColourId, theme::colour::textPrimary);
    chordBubble.setColour (juce::Label::backgroundColourId, theme::colour::bg4.withAlpha (0.94f));
    chordBubble.setColour (juce::Label::outlineColourId, theme::colour::accent.withAlpha (0.8f));
    chordBubble.setInterceptsMouseClicks (false, false);
    addChildComponent (chordBubble);

    for (auto* box : { &chordBox, &scaleBox })
    {
        box->setWantsKeyboardFocus (false);
        box->setMouseClickGrabsKeyboardFocus (false);
        box->onChange = [this] {
            publishScale();
            scaleSnapButton.setEnabled (scaleIndex() > 0);
            if (rows.count < 128 || foldToScale)
                updateKeyRows(); // the folded rows follow the scale
            repaint();
        };
        addAndMakeVisible (box);
    }

    addNoteLanes();
    laneBox.setTooltip (tr ("What the bottom lane edits"));
    laneBox.setSelectedId (1, juce::dontSendNotification);
    laneBox.setWantsKeyboardFocus (false);
    laneBox.setMouseClickGrabsKeyboardFocus (false);
    laneBox.onChange = [this] { repaint(); };
    addAndMakeVisible (laneBox);

    setOpaque (true); // paints every pixel: nothing behind it is redrawn while scrolling

    engine.addChangeListener (this);
    EditorGlobals::get().addChangeListener (this);
    refreshChannelBox();
    showingTimer = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 30); // (stopped while hidden; round 174f)
}

PianoRollComponent::~PianoRollComponent()
{
    stopListening();
    previewNotes.releaseAll();
    EditorGlobals::get().removeChangeListener (this);
    engine.removeChangeListener (this);
}

juce::Rectangle<int> PianoRollComponent::gridArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth).withTrimmedTop (headerHeight)
        .withTrimmedRight (scrollBarSize).withTrimmedBottom (velocityHeight);
}

juce::Rectangle<int> PianoRollComponent::velocityArea() const
{
    const auto grid = gridArea();
    return { grid.getX(), grid.getBottom(), grid.getWidth(), velocityHeight };
}

double PianoRollComponent::tickToX (double tick) const
{
    return keyboardWidth + (tick - scrollTicks) * pixelsPerTick;
}

double PianoRollComponent::xToTick (int x) const
{
    return (x - keyboardWidth) / pixelsPerTick + scrollTicks;
}

int PianoRollComponent::keyToY (int key) const
{
    return headerHeight + rowFor (key) * rowHeight - scrollY;
}

int PianoRollComponent::rowFor (int key) const noexcept
{
    key = juce::jlimit (0, 127, key);
    const auto row = rows.rowOfKey[(std::size_t) key];
    return row >= 0 ? row : rows.rowOfKey[(std::size_t) rows.step (key, 0)]; // a folded key: the row above it
}

void PianoRollComponent::updateKeyRows()
{
    // keep the key at the top of the view where it is when the rows change (fold on/off, another scale)
    const auto topKey = yToKey (headerHeight);
    rows = ! padRows.empty() ? notetools::keyRowsOf (padRows) : notetools::keyRows (scaleRoot(), scaleIndex(), foldToScale);
    scrollY = juce::jlimit (0, juce::jmax (0, rows.count * rowHeight - gridArea().getHeight()), rowFor (topKey) * rowHeight);
    updateScrollBars();
    repaint();
}

std::vector<int> PianoRollComponent::wantedPadRows() const
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (! EditorGlobals::get().clipLaunchWorkflow || allChannels || channel < 0 || channel >= (int) song.channels.size()
        || ! rack::isDrumRack (song.channels[(std::size_t) channel]))
        return {};
    return rack::filledPadKeys (*rack::rackOf (song.channels[(std::size_t) channel].patcher));
}

void PianoRollComponent::syncPadRows()
{
    if (auto wanted = wantedPadRows(); wanted != padRows)
    {
        padRows = std::move (wanted);
        updateKeyRows();
    }
}

void PianoRollComponent::setFoldToScale (bool on)
{
    foldToScale = on;
    updateKeyRows();
}

int PianoRollComponent::yToKey (int y) const
{
    const auto row = juce::jlimit (0, juce::jmax (0, rows.count - 1), (y - headerHeight + scrollY) / rowHeight);
    return rows.keyOfRow[(std::size_t) row];
}

void PianoRollComponent::setKeyStyle (pianokeys::Style style)
{
    if (style == keyStyle)
        return;
    keyStyle = style;
    if (EditorGlobals::get().onOptionsChanged)
        EditorGlobals::get().onOptionsChanged(); // (the main window saves it and repaints the other editors)
    repaint();
}

int PianoRollComponent::keyboardKeyAt (int x, int y) const
{
    const auto key = yToKey (y);
    if (keyStyle != pianokeys::Style::piano || folded())
        return key;
    // "Piano keys": right of a black key the white keys above and below it meet in the middle of its row
    const auto next = pianokeys::keyAt (keyStyle, key, y - keyToY (key), rowHeight, x, keyboardWidth * 3 / 5);
    return juce::isPositiveAndBelow (next, 128) && rows.shown (next) ? next : key;
}

std::int64_t PianoRollComponent::gridTicks() const
{
    const auto bar = engine.getSong().barTicks();
    const auto& option = grid::resolve (snapBox);

    switch (option.kind)
    {
        case grid::Kind::none: return 1;
        case grid::Kind::ticks: return option.ticks;
        case grid::Kind::bar: return bar;
        case grid::Kind::screen:
        case grid::Kind::nearest: break;
    }

    return noteedit::screenStep (pixelsPerTick, bar);
}

std::vector<std::int64_t> PianoRollComponent::snapCandidates() const
{
    // edges of this channel's notes (except the ones being dragged) and of the visible ghosts
    std::vector<std::int64_t> out;
    const auto& notes = currentPattern (engine.getSong()).notes;

    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const auto& n = notes[(std::size_t) i];

        if ((! edits (n) && (! showGhosts || ! ghostShown (n.channel))) || std::find (dragIndices.begin(), dragIndices.end(), i) != dragIndices.end())
            continue;

        out.push_back (n.start);
        out.push_back (n.start + n.length);
    }

    return out;
}

std::int64_t PianoRollComponent::snap (double tick, bool enabled) const
{
    if (! enabled)
        return (std::int64_t) std::floor (tick);

    if (grid::resolve (snapBox).kind == grid::Kind::nearest)
        return noteedit::snapNearest (tick, snapCandidates(), 10.0 / pixelsPerTick, gridTicks());

    return noteedit::snapDown (tick, gridTicks());
}

std::int64_t PianoRollComponent::newNoteLength() const
{
    const auto fixed = EditorGlobals::get().getDefaultNoteLength();
    return fixed > 0 ? fixed : lastLength;
}

std::int64_t PianoRollComponent::viewLengthTicks() const
{
    const auto& song = engine.getSong();
    const auto bar = song.barTicks();
    // pattern + 4 bars of headroom to grow
    return currentPattern (song).effectiveLength (song.meter()) + 4 * bar;
}

void PianoRollComponent::updateScrollBars()
{
    const auto grid = gridArea();
    const auto total = (double) viewLengthTicks();
    hBar.setRangeLimits (0.0, total, juce::dontSendNotification);
    hBar.setCurrentRange (scrollTicks, grid.getWidth() / pixelsPerTick, juce::dontSendNotification);
    vBar.setRangeLimits (0.0, (double) rows.count * rowHeight, juce::dontSendNotification);
    vBar.setCurrentRange (scrollY, grid.getHeight(), juce::dontSendNotification);
    scrollTicks = hBar.getCurrentRangeStart();
    scrollY = (int) vBar.getCurrentRangeStart();
    overview.minLength = grid.getWidth() / (120.0 / ticksPerStep);
    overview.setRange (total, scrollTicks, grid.getWidth() / pixelsPerTick);
}

void PianoRollComponent::setTitleBarInsets (int left, int right)
{
    if (left != titleInsetLeft || right != titleInsetRight)
    {
        titleInsetLeft = left;
        titleInsetRight = right;
        resized();
    }
}

void PianoRollComponent::resized()
{
    const auto h = toolbarHeight - 6;
    auto bar = juce::Rectangle<int> (4 + titleInsetLeft, 3, juce::jmax (0, getWidth() - 8 - titleInsetLeft - titleInsetRight), h);

    // groups that don't fit in a narrow window are hidden (the tools and the grid always show). The menu button always
    // keeps its place: the optional groups only take what is left beside it (round 190j: on a 1024 px screen the
    // groups before it used up the bar and the Piano roll menu could not be opened).
    const auto reserved = h + 6;
    auto place = [&bar, reserved] (juce::Component& c, int width, bool optional = false) {
        const auto fits = ! optional || width <= bar.getWidth() - reserved;
        c.setVisible (fits);
        c.setBounds (fits ? bar.removeFromLeft (juce::jmin (width, bar.getWidth())) : juce::Rectangle<int>());
        bar.removeFromLeft (juce::jmin (6, bar.getWidth()));
    };

    place (playButton, h);
    place (channelBox, 150);
    place (tools, tools.getIdealWidth (h));
    place (snapBox, 128);
    place (ghostButton, h);
    place (followButton, h);
    place (slideButton, h, true);
    place (colourButton, h, true);
    // the chord stamp shrinks to its icon and arrow before it is hidden (discussion #10; FL's stamp is an icon button)
    place (chordBox, 100 <= bar.getWidth() - reserved ? 100 : pianoroll::compactChordBoxWidth, true);
    place (scaleBox, 164, true);
    if (scaleBox.isVisible())
        place (scaleSnapButton, h);
    else
        scaleSnapButton.setVisible (false);
    place (moreButton, h);
    place (chordLabel, 84, true);
    laneBox.setBounds (2, velocityArea().getY() + 4, keyboardWidth - 4, 20);
    overview.setBounds (keyboardWidth, toolbarHeight, getWidth() - keyboardWidth - zoomWheelSize, overviewHeight);
    vBar.setBounds (getWidth() - scrollBarSize, headerHeight, scrollBarSize, getHeight() - headerHeight - velocityHeight);
    updateScrollBars();
}

void PianoRollComponent::squeezeToolbarForSnapshot (int pixels)
{
    setTitleBarInsets (titleInsetLeft, titleInsetRight + pixels);
}

void PianoRollComponent::scrollToNotes (bool fitPattern)
{
    const auto& song = engine.getSong();
    int low = 128, high = -1;

    for (const auto& n : currentPattern (song).notes)
        if (edits (n))
        {
            low = juce::jmin (low, n.key);
            high = juce::jmax (high, n.key);
        }

    refreshChannelBox();
    if (fitKeysToNotes && high >= 0 && gridArea().getHeight() > 0) // docked: every note's row in the short view, as far as the key height allows
    {
        const auto span = std::abs (rowFor (low) - rowFor (high)) + 3;
        rowHeight = juce::jlimit (gesture::minKeyHeight, undockedRowHeight, gridArea().getHeight() / span);
    }
    const auto centreKey = high >= 0 ? (low + high) / 2 : 66;
    scrollY = rowFor (centreKey) * rowHeight - gridArea().getHeight() / 2;
    scrollTicks = 0.0;

    // FL opens the Piano Roll with the whole pattern across the view (at least one bar)
    if (fitPattern && gridArea().getWidth() > 0)
    {
        const auto bar = (double) song.barTicks();
        const auto length = juce::jmax (bar, (double) currentPattern (song).effectiveLength (song.meter()));
        pixelsPerTick = juce::jlimit (4.0 / ticksPerStep, 64.0 / ticksPerStep, gridArea().getWidth() / length);
    }
    updateScrollBars();
    repaint();
}

// --- selection ---------------------------------------------------------------

std::vector<int> PianoRollComponent::chordIntervals() const
{
    const auto index = juce::jlimit (0, (int) chords().size() - 1, chordBox.getSelectedId() - 1);
    auto intervals = chords()[(std::size_t) index].intervals;
    // FL 2026's top-down stamp (REA round 153): the clicked key is the chord's highest note
    if (chordTopDown && intervals.size() > 1)
    {
        const auto top = *std::max_element (intervals.begin(), intervals.end());
        for (auto& i : intervals)
            i -= top;
        std::reverse (intervals.begin(), intervals.end()); // the clicked (top) note first: it leads the drag
    }
    return intervals;
}

int PianoRollComponent::scaleIndex() const
{
    const auto id = scaleBox.getSelectedId();
    return id <= 1 ? 0 : id / 100;
}

int PianoRollComponent::scaleRoot() const
{
    const auto id = scaleBox.getSelectedId();
    return id <= 1 ? 0 : (id % 100) - 1;
}

bool PianoRollComponent::inScale (int key) const
{
    return scaleIndex() > 0 && notetools::inScale (key, scaleRoot(), scaleIndex());
}

int PianoRollComponent::snapKey (int key) const
{
    return scalesnap::drawKey (snapScale(), key);
}

scalesnap::Scale PianoRollComponent::snapScale() const
{
    return { scaleRoot(), scaleIndex(), scaleSnap };
}

void PianoRollComponent::setScaleSnap (bool on)
{
    scaleSnap = on;
    scaleSnapButton.setToggleState (on, juce::dontSendNotification);
    publishScale();
    repaint();
}

void PianoRollComponent::publishScale()
{
    engine.setInputScale (scaleSnap ? scaleIndex() : 0, scaleRoot());
}

bool PianoRollComponent::isSelected (int index) const
{
    return std::binary_search (selection.begin(), selection.end(), index);
}

std::vector<int> PianoRollComponent::channelNotes() const
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<int> out;

    for (int i = 0; i < (int) notes.size(); ++i)
        if (edits (notes[(std::size_t) i]) && rows.shown (notes[(std::size_t) i].key)) // (folded-away notes are left alone)
            out.push_back (i);

    return out;
}

std::vector<int> PianoRollComponent::selectionOrAll() const
{
    return selection.empty() ? channelNotes() : selection;
}

void PianoRollComponent::setSelection (std::vector<int> indices)
{
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    selection = std::move (indices);
    repaint();
}

void PianoRollComponent::selectAll()
{
    setSelection (channelNotes());
}

bool PianoRollComponent::refuseFrozenEdit()
{
    const auto& song = engine.getSong();
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= (int) song.channels.size() || ! song.channels[(std::size_t) channel].freeze.on())
        return false;
    ui::postNotice ({ tr ("{0} is frozen: Unfreeze to edit ([RMB] on the channel, Unfreeze)", { juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) }), {} },
                    3000);
    return true;
}

void PianoRollComponent::editNotes (const std::function<void (Pattern&)>& edit, bool undoable)
{
    if (refuseFrozenEdit())
        return;
    engine.editSong ([&edit] (Song& s) { edit (currentPattern (s)); }, undoable);
    knownNoteCount = currentPattern (engine.getSong()).notes.size();
}

void PianoRollComponent::sortNotes()
{
    auto tracked = selection;
    tracked.push_back (dragNote);
    // only reorders: no undo step of its own needed
    engine.editSong ([&tracked] (Song& s) { noteedit::sortTracking (currentPattern (s), tracked); }, false);
    dragNote = tracked.back();
    tracked.pop_back();
    setSelection (tracked);
}

juce::Rectangle<float> PianoRollComponent::stretchHandle() const
{
    // a tab on the right end of the selection, vertically centred on it (clamped into the grid)
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::int64_t start = 0, end = 0;
    int top = -1, bottom = 128;
    bool any = false;

    for (auto i : selection)
    {
        if (i < 0 || i >= (int) notes.size() || ! edits (notes[(std::size_t) i]))
            continue;
        const auto& n = notes[(std::size_t) i];
        start = any ? juce::jmin (start, n.start) : n.start;
        end = any ? juce::jmax (end, n.start + n.length) : n.start + n.length;
        top = juce::jmax (top, n.key);
        bottom = juce::jmin (bottom, n.key);
        any = true;
    }

    if (! any || end <= start)
        return {};

    const auto grid = gridArea().toFloat();
    const auto x = (float) tickToX ((double) end);
    const auto y0 = (float) keyToY (top), y1 = (float) (keyToY (bottom) + rowHeight);
    const auto cy = juce::jlimit (grid.getY() + 10.0f, juce::jmax (grid.getY() + 10.0f, grid.getBottom() - 10.0f), (y0 + y1) * 0.5f);
    const auto r = juce::Rectangle<float> (x + 2.0f, cy - 9.0f, 9.0f, 18.0f);
    return grid.intersects (r) ? r : juce::Rectangle<float>();
}

void PianoRollComponent::drumPaintAt (juce::Point<int> p, bool snapOn)
{
    // one short hit per grid cell and key, on whatever key is under the mouse (FL's drum painting)
    const auto cell = juce::jmax<std::int64_t> (1, snapOn ? gridTicks() : ticksPerStep);
    const auto start = juce::jmax<std::int64_t> (0, snapOn ? snap (xToTick (p.x), true) : noteedit::snapDown (xToTick (p.x), cell));
    const auto key = snapKey (yToKey (p.y));

    if (start == lastDrumCell && key == lastDrumKey)
        return;

    lastDrumCell = start;
    lastDrumKey = key;
    const auto channel = engine.getSelectedChannel();

    if (polish::hasNoteInCell (currentPattern (engine.getSong()), channel, key, start, cell))
        return;

    Note n;
    noteTemplate.applyTo (n);
    n.start = start;
    n.length = cell;
    n.key = key;
    n.channel = channel;
    n.colour = noteColour;
    editNotes ([&n] (Pattern& pat) { noteedit::paste (pat, { n }, 0); });

    if (key != previewKey)
        previewOn (key, n.velocity);
}

// --- painting ------------------------------------------------------------------

void PianoRollComponent::paintOverview (juce::Graphics& g, juce::Rectangle<float> area, double total)
{
    // the channel's notes across the whole view length, pitch mapped onto the strip's height; the part past
    // the pattern's end is darker; bar lines every bar (every 4 when they get dense)
    using namespace theme::colour;
    const auto& song = engine.getSong();
    const auto& pattern = currentPattern (song);
    const auto channel = engine.getSelectedChannel();
    const auto scale = area.getWidth() / juce::jmax (1.0, total);
    const auto bar = (double) song.barTicks();
    const auto end = (float) (pattern.effectiveLength (song.meter()) * scale);

    g.setColour (bg2);
    g.fillRect (area.withWidth (juce::jmin (area.getWidth(), end)));

    const auto every = bar * scale < 6.0 ? 4 : 1;
    g.setColour (gridBar.withAlpha (0.6f));
    for (int b = every; b * bar < total; b += every)
        g.fillRect ((float) (area.getX() + b * bar * scale), area.getY(), 1.0f, area.getHeight());

    int low = 128, high = -1;
    for (const auto& n : pattern.notes)
        if (edits (n))
        {
            low = juce::jmin (low, n.key);
            high = juce::jmax (high, n.key);
        }

    if (high < 0)
        return;

    const auto span = (float) juce::jmax (12, high - low + 1);
    const auto mid = (low + high) / 2.0f;
    const auto inner = area.reduced (0.0f, 2.0f);
    auto colour = accent;
    if (channel < (int) song.channels.size() && song.channels[(std::size_t) channel].colour != 0)
        colour = theme::tame (juce::Colour (song.channels[(std::size_t) channel].colour));
    colour = colour.interpolatedWith (textPrimary, 0.35f); // reads on the dark strip at any channel colour

    for (const auto& n : pattern.notes)
    {
        if (! edits (n))
            continue;

        const auto y = inner.getCentreY() - ((float) n.key - mid) / span * inner.getHeight();
        const auto x = area.getX() + (float) (n.start * scale);
        g.setColour (n.muted ? colour.withAlpha (0.35f) : colour);
        g.fillRect (x, juce::jlimit (inner.getY(), inner.getBottom() - 2.0f, y - 1.0f),
                    juce::jmax (1.0f, (float) (n.length * scale) - 0.5f), 2.0f);
    }
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("PianoRollComponent paint", true, g.getClipBounds());
    const auto& song = engine.getSong();
    const auto& pattern = currentPattern (song);
    const auto channel = engine.getSelectedChannel();
    const auto grid = gridArea();
    const auto patternLength = pattern.effectiveLength (song.meter());
    const auto bar = song.barTicks();
    const auto snapGrid = gridTicks();

    // the overview follows the notes (edits, undo, another channel or pattern) and the view
    {
        auto signature = (std::size_t) song.currentPattern * 1000003u + (std::size_t) channel * 7919u + pattern.notes.size();
        for (const auto& n : pattern.notes)
            signature = signature * 31u + (std::size_t) (n.start * 131 + n.length * 7 + n.key + n.channel * 257 + (n.muted ? 1 : 0));
        if (signature != overviewSignature)
        {
            overviewSignature = signature;
            overview.repaint();
        }
        overview.setRange ((double) viewLengthTicks(), scrollTicks, grid.getWidth() / pixelsPerTick);
    }

    using namespace theme::colour;
    g.fillAll (bg1);
    g.setColour (bg3);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.setColour (border);
    g.fillRect (0, toolbarHeight - 1, getWidth(), 1);

    auto drawVerticalGrid = [&] (juce::Rectangle<int> area) {
        // grid lines: bar > beat > snap (unless too dense)
        const auto step = snapGrid * pixelsPerTick >= 6.0 ? snapGrid : (std::int64_t) ppq;
        const auto first = (std::int64_t) std::floor (scrollTicks / (double) step);
        const auto last = (std::int64_t) std::ceil (xToTick (area.getRight()) / (double) step);

        for (auto i = first; i <= last; ++i)
        {
            const auto tick = i * step;
            const auto x = (float) tickToX ((double) tick);
            g.setColour (tick % bar == 0 ? gridBar : tick % ppq == 0 ? gridBeat : gridStep);
            g.drawVerticalLine ((int) x, (float) area.getY(), (float) area.getBottom());
        }
    };

    // round 190ge: what this paint may change. A moving playhead repaints a 14-pixel column many times a second, and
    // every note drawn outside it (a rounded rectangle is a path to build and fill) was work for no pixel: the notes,
    // ghosts and velocity markers whose drawing lies wholly outside are skipped (the pixels stay the same)
    const auto dirtyArea = g.getClipBounds();
    const auto cull = ui::paintSavings(); // (LYNX_PAINT_SAVINGS=0: everything drawn, as before)
    const auto outside = [&dirtyArea, cull] (const juce::Rectangle<float>& r, float margin) {
        return cull && ! r.expanded (margin).getSmallestIntegerContainer().intersects (dirtyArea);
    };

    // --- grid ------------------------------------------------------------
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid);

        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (! rows.shown (key) || y > grid.getBottom() || y + rowHeight < grid.getY())
                continue;

            // Note grid highlights (Alt+B, round 168g): off, every row looks the same (FL's "BlackKeys" option)
            auto row = gridHighlights && isBlackKey (key) ? rowBlack : rowWhite;
            if (gridHighlights && inScale (key))
                row = row.interpolatedWith (info, 0.10f);
            if (key == hover.key && drag == Drag::none)
                row = row.interpolatedWith (textPrimary, 0.04f); // the row under the mouse
            g.setColour (row);
            g.fillRect (grid.getX(), y, grid.getWidth(), rowHeight);

            if (! padRows.empty() || key % 12 == (folded() ? scaleRoot() : 0)) // folded: the octave line under each root (pads: each row)
            {
                g.setColour (gridBeat);
                g.drawHorizontalLine (y + rowHeight - 1, (float) grid.getX(), (float) grid.getRight());
            }
        }

        drawVerticalGrid (grid);
        paintBackgroundWave (g, grid);

        // the time selection (Ctrl+drag on the ruler): Insert / Delete space, Trim
        if (timeFrom >= 0 && timeTo > timeFrom)
        {
            g.setColour (selectionFill.withMultipliedAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> ((float) tickToX ((double) timeFrom), (float) grid.getY(),
                                                (float) ((double) (timeTo - timeFrom) * pixelsPerTick), (float) grid.getHeight()));
        }

        // outside the pattern is darkened
        const auto endX = (float) tickToX ((double) patternLength);
        g.setColour (bg0.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (endX, (float) grid.getY(), (float) grid.getRight() - endX, (float) grid.getHeight()));

        // ghost notes (Alt+V). This channel's notes in the other clips that play at the same time in the
        // Playlist: hollow outlines. The other channels of this pattern: solid grey (a bit brighter, with
        // their channel colour on the edge, when Ctrl+Alt+V makes them editable)
        if (showGhosts)
        {
            for (const auto& n : polish::clipGhostNotes (song, song.currentPattern, channel))
            {
                if (! rows.shown (n.key))
                    continue;
                const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                       juce::jmax (4.0f, (float) (n.length * pixelsPerTick) - 1.0f), (float) rowHeight - 2);
                if (r.getRight() < (float) grid.getX() || r.getX() > (float) grid.getRight() || outside (r, 2.0f))
                    continue;
                g.setColour (textPrimary.withAlpha (0.30f));
                g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);
            }
        }

        for (const auto& n : pattern.notes)
        {
            if (edits (n) || ! showGhosts || ! ghostShown (n.channel) || ! rows.shown (n.key))
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   (float) (n.length * pixelsPerTick), (float) rowHeight - 2);
            if (outside (r, 2.0f))
                continue;
            const auto hovered = &n - pattern.notes.data() == hover.ghost;
            g.setColour (textPrimary.withAlpha (hovered ? 0.16f : editGhosts ? 0.12f : 0.09f));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (textPrimary.withAlpha (hovered ? 0.35f : editGhosts ? 0.22f : 0.12f));
            g.drawRoundedRectangle (r, 2.0f, 1.0f);

            if (editGhosts && n.channel >= 0 && n.channel < (int) song.channels.size() && song.channels[(std::size_t) n.channel].colour != 0)
            {
                g.setColour (theme::tame (juce::Colour (song.channels[(std::size_t) n.channel].colour)).withAlpha (0.7f));
                g.fillRect (r.withWidth (2.0f).reduced (0.0f, 1.0f));
            }
        }

        for (int i = 0; i < (int) pattern.notes.size(); ++i)
        {
            const auto& n = pattern.notes[(std::size_t) i];

            if (! edits (n) || ! rows.shown (n.key))
                continue;

            const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                   juce::jmax (4.0f, (float) (n.length * pixelsPerTick) - 1.0f), (float) rowHeight - 2);
            if (outside (r, 2.0f)) // (its outline, grips, marks and label all lie inside r)
                continue;
            const auto selected = isSelected (i);
            const auto hovered = i == hover.note && drag == Drag::none;
            // a selected muted note keeps a muted look (round 167k: Alt+M on a selection showed no change)
            auto fill = selected ? (n.muted ? accentHover.interpolatedWith (noteMuted, 0.6f) : accentHover.brighter (0.15f))
                                 : n.muted ? noteMuted : theme::noteGroupColour (n.colour, n.velocity);
            if (hovered)
                fill = fill.brighter (0.18f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (selected ? textPrimary : hovered ? textPrimary.withAlpha (0.75f) : bg0.withAlpha (0.6f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);

            // "All" channels: whose note it is, by the channel's colour on its left edge (issue #12)
            if (allChannels && n.channel >= 0 && n.channel < (int) song.channels.size() && r.getWidth() > 5.0f)
            {
                const auto c = song.channels[(std::size_t) n.channel].colour;
                g.setColour ((c != 0 ? theme::tame (juce::Colour (c)) : textPrimary.withAlpha (0.5f)).withAlpha (0.9f));
                g.fillRect (r.withWidth (2.5f).reduced (0.0f, 1.5f).translated (1.0f, 0.0f));
            }

            // the edge under the mouse shows its grip
            if (hovered && (hover.zone == gesture::Zone::rightEdge || hover.zone == gesture::Zone::leftEdge))
            {
                const auto gripX = hover.zone == gesture::Zone::rightEdge ? r.getRight() - 3.0f : r.getX() + 1.0f;
                g.setColour (textPrimary);
                g.fillRoundedRectangle (gripX, r.getY() + 2.0f, 2.0f, juce::jmax (2.0f, r.getHeight() - 4.0f), 1.0f);
            }

            if (n.group != 0 && r.getWidth() > 10.0f) // grouped: a small link mark at the top right
            {
                g.setColour (bg0.withAlpha (0.7f));
                g.fillEllipse (r.getRight() - 6.0f, r.getY() + 1.5f, 3.5f, 3.5f);
            }

            // a Chance below 100 % (round 187c): a small triangle in the top-left corner, as Live 12 marks it ("A small
            // triangle is displayed in the upper-left corner of notes with probability values less than 100%")
            if ((n.chance < 1.0f || n.chanceGroup != 0) && r.getWidth() > 6.0f && r.getHeight() > 5.0f)
            {
                juce::Path corner;
                const auto size = juce::jmin (6.0f, r.getHeight() - 2.0f);
                corner.addTriangle (r.getX() + 1.0f, r.getY() + 1.0f, r.getX() + 1.0f + size, r.getY() + 1.0f, r.getX() + 1.0f, r.getY() + 1.0f + size);
                g.setColour (bg0.withAlpha (0.75f));
                g.fillPath (corner);
            }

            if (n.porta) // porta: slanted stroke at the start, like the glide
            {
                g.setColour (bg0.withAlpha (0.65f));
                g.drawLine (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getX() + 9.0f, r.getY() + 2.0f, 2.0f);
            }

            if (n.slide) // slide arrow at the start of the note
            {
                juce::Path arrow;
                arrow.addTriangle (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getX() + 2.0f, r.getY() + 2.0f, r.getX() + 9.0f, r.getCentreY());
                g.setColour (bg0.withAlpha (0.65f));
                g.fillPath (arrow);
            }

            if (gesture::noteLabelFits (r.getWidth(), rowHeight)) // the note's name, readable on its colour
            {
                g.setColour (fill.getPerceivedBrightness() > 0.55f ? bg0.withAlpha (0.85f) : textPrimary.withAlpha (0.9f));
                g.setFont (theme::font (gesture::labelHeight (rowHeight) - 1.0f));
                textcache::drawText (g, keyName (n.key), r.reduced (n.slide || n.porta ? 11.0f : 4.0f, 0.0f), juce::Justification::centredLeft, true);
            }
        }

        // round 177d: the notes being played into a take grow with the playhead until they are released (then the
        // recorded note takes their place); FL 2026 draws them so, as ordinary notes (REA_rodada177d §1.3, g03)
        if (engine.isLiveRecording())
            for (const auto& n : engine.liveRecording().held)
            {
                if (! edits (n) || ! rows.shown (n.key))
                    continue;
                const auto r = juce::Rectangle<float> ((float) tickToX ((double) n.start), (float) keyToY (n.key) + 1,
                                                       juce::jmax (4.0f, (float) (n.length * pixelsPerTick) - 1.0f), (float) rowHeight - 2);
                const auto fill = theme::noteGroupColour (n.colour, n.velocity);
                g.setColour (fill);
                g.fillRoundedRectangle (r, 2.5f);
                g.setColour (bg0.withAlpha (0.6f));
                g.drawRoundedRectangle (r, 2.5f, 1.0f);
                if (gesture::noteLabelFits (r.getWidth(), rowHeight))
                {
                    g.setColour (fill.getPerceivedBrightness() > 0.55f ? bg0.withAlpha (0.85f) : textPrimary.withAlpha (0.9f));
                    g.setFont (theme::font (gesture::labelHeight (rowHeight) - 1.0f));
                    textcache::drawText (g, keyName (n.key), r.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
                }
            }

        // the selection's stretch handle: a tab at its right end with two grips
        if (const auto handle = stretchHandle(); ! handle.isEmpty() && (drag == Drag::none || drag == Drag::stretch))
        {
            const auto lit = hover.stretch || drag == Drag::stretch;
            g.setColour (lit ? accentHover : accent.withAlpha (0.85f));
            g.fillRoundedRectangle (handle, 3.0f);
            g.setColour (onAccent.withAlpha (0.9f));
            g.fillRect (handle.getCentreX() - 2.0f, handle.getY() + 5.0f, 1.0f, handle.getHeight() - 10.0f);
            g.fillRect (handle.getCentreX() + 1.0f, handle.getY() + 5.0f, 1.0f, handle.getHeight() - 10.0f);

            if (lit) // the stretched span, from the anchor to the handle
            {
                g.setColour (accent.withAlpha (0.5f));
                const auto x0 = (float) tickToX ((double) (drag == Drag::stretch ? stretchAnchor : 0));
                if (drag == Drag::stretch)
                    g.drawVerticalLine ((int) x0, (float) grid.getY(), (float) grid.getBottom());
                g.drawVerticalLine ((int) handle.getX() - 2, (float) grid.getY(), (float) grid.getBottom());
            }
        }

        if (! rubberBand.isEmpty())
        {
            g.setColour (selectionFill);
            g.fillRect (rubberBand);
            g.setColour (theme::colour::selection);
            g.drawRect (rubberBand);
        }

        if (drag == Drag::knife)
        {
            juce::Path cut;
            cut.startNewSubPath (knifeFrom.toFloat());
            cut.lineTo (knifeTo.toFloat());
            juce::Path dashed;
            const float dash[] = { 5.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, cut, dash, 2);
            g.setColour (textPrimary);
            g.fillPath (dashed);
        }

        if (playheadTick >= 0.0)
        {
            g.setColour (playhead);
            g.drawVerticalLine ((int) tickToX (playheadTick), (float) grid.getY(), (float) grid.getBottom());
        }
    }

    // --- velocity lane -----------------------------------------------------
    {
        const auto lane = velocityArea();
        g.setColour (bg0);
        g.fillRect (lane);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (lane);
        drawVerticalGrid (lane);
        g.setColour (border);
        g.drawHorizontalLine (lane.getY(), (float) lane.getX(), (float) lane.getRight());

        const auto usable = (float) lane.getHeight() - 8.0f;
        const auto bipolar = laneIsBipolar();
        const auto base = bipolar ? (float) lane.getBottom() - 2.0f - usable * 0.5f : (float) lane.getBottom();

        if (bipolar)
        {
            g.setColour (textPrimary.withAlpha (0.15f));
            g.drawHorizontalLine ((int) base, (float) lane.getX(), (float) lane.getRight());
        }

        if (const auto target = eventLaneTarget()) // pattern events: held values, a dot on each event
        {
            const auto* events = eventedit::findLane (pattern, *target);
            if (events != nullptr && ! events->events.empty())
            {
                juce::Path held;
                const auto yOf = [&] (float v) { return (float) lane.getBottom() - 2.0f - usable * v; };
                auto x = (float) tickToX (0.0);
                auto y = yOf (events->events.front().value);
                held.startNewSubPath (x, y);
                for (const auto& ev : events->events)
                {
                    const auto ex = (float) tickToX ((double) ev.tick);
                    held.lineTo (ex, y);
                    y = yOf (ev.value);
                    held.lineTo (ex, y);
                }
                held.lineTo ((float) lane.getRight(), y);
                g.setColour (accent.withAlpha (0.85f));
                g.strokePath (held, juce::PathStrokeType (1.5f));
                for (const auto& ev : events->events)
                    g.fillEllipse ((float) tickToX ((double) ev.tick) - 2.5f, yOf (ev.value) - 2.5f, 5.0f, 5.0f);
            }
            else
            {
                g.setColour (textDisabled);
                g.setFont (theme::font (theme::type::small));
                textcache::drawText (g, tr ("No events yet: draw them here"), lane.reduced (8, 4), juce::Justification::centredLeft, true);
            }
        }
        // the lane's name in it, as FL writes it next to "Control" (round 149: the box on the left is narrow)
        g.setColour (theme::colour::textSecondary.withAlpha (0.8f));
        g.setFont (theme::font (theme::type::caption, true));
        textcache::drawText (g, laneBox.getText(), lane.reduced (8, 3).removeFromTop (14), juce::Justification::topLeft, true);

        for (int i = 0; i < (int) pattern.notes.size() && ! eventLaneTarget(); ++i)
        {
            const auto& n = pattern.notes[(std::size_t) i];

            if (! edits (n) || ! rows.shown (n.key))
                continue;

            const auto x = (float) tickToX ((double) n.start);
            // (round 190ge: outside the repainted area — the marker's ±6 pixels, the Velocity Range box over the note's
            // length — nothing to draw; a probability group's line reaches another note, so those are always drawn)
            if (! (laneIsChance() && n.chanceGroup != 0)
                && outside (juce::Rectangle<float> (x - 6.0f, (float) lane.getY(), 12.0f + juce::jmax (3.0f, (float) (n.length * pixelsPerTick)), (float) lane.getHeight()), 1.0f))
                continue;
            const auto top = (float) lane.getBottom() - 2.0f - usable * laneValue (n);
            auto colour = isSelected (i) ? accentHover.brighter (0.15f) : theme::noteGroupColour (n.colour, n.velocity);
            if (n.muted)
                colour = noteMuted;
            const auto hovered = i == hover.note && drag == Drag::none;
            // the velocity lane shows a note's Velocity Range (round 187c) as a shaded box over the note's length, from
            // its velocity to where the range reaches, as Live 12 draws it [LIVE 12.4 shot, live-02]
            if (laneBox.getSelectedId() == laneVelocity && n.velocityRange != 0.0f)
            {
                const auto reach = juce::jlimit (1.0f / 127.0f, 1.0f, n.velocity + n.velocityRange);
                const auto y2 = (float) lane.getBottom() - 2.0f - usable * reach;
                const auto w = juce::jmax (3.0f, (float) (n.length * pixelsPerTick) - 1.0f);
                g.setColour (colour.withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (x, juce::jmin (top, y2), w, std::abs (y2 - top)));
                g.setColour (colour.withAlpha (0.5f));
                g.drawHorizontalLine ((int) y2, x, x + w);
            }
            // a probability group (round 187c): its members' Chance markers joined by a line (the group has one Chance)
            if (laneIsChance() && n.chanceGroup != 0)
                for (int j = i + 1; j < (int) pattern.notes.size(); ++j)
                    if (const auto& m = pattern.notes[(std::size_t) j]; m.chanceGroup == n.chanceGroup && edits (m) && rows.shown (m.key))
                    {
                        g.setColour (colour.withAlpha (0.5f));
                        const auto dash = std::array<float, 2> { 3.0f, 3.0f };
                        g.drawDashedLine (juce::Line<float> (x, top, (float) tickToX ((double) m.start), top), dash.data(), 2, n.chanceOne ? 1.0f : 2.0f);
                        break;
                    }
            g.setColour (hovered ? colour.brighter (0.3f) : colour.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x, juce::jmin (top, base), hovered ? 3.0f : 2.0f, std::abs (base - top)));
            g.fillEllipse (x - 3.0f, top - 3.0f, 7.0f, 7.0f);
            if (hovered)
                g.drawEllipse (x - 5.0f, top - 5.0f, 11.0f, 11.0f, 1.0f);
        }

        if (drag == Drag::velocityLine) // the line being drawn
        {
            g.setColour (textPrimary.withAlpha (0.8f));
            g.drawLine (juce::Line<int> (knifeFrom, knifeTo).toFloat(), 1.5f);
        }
    }

    // --- time ruler --------------------------------------------------------
    {
        const auto ruler = juce::Rectangle<int> (keyboardWidth, rulerTop, grid.getWidth(), timelineHeight);
        g.setColour (bg2);
        g.fillRect (ruler);
        g.setColour (border);
        g.fillRect (ruler.withTop (ruler.getBottom() - 1));
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (ruler);
        g.setFont (theme::font (theme::type::caption));

        const auto firstBar = (std::int64_t) std::floor (scrollTicks / (double) bar);
        const auto lastBar = (std::int64_t) std::ceil (xToTick (grid.getRight()) / (double) bar);

        if (timeFrom >= 0 && timeTo > timeFrom) // the time selection on the ruler
        {
            const auto x0 = (float) tickToX ((double) timeFrom), x1 = (float) tickToX ((double) timeTo);
            g.setColour (accent.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x0, (float) rulerTop, x1 - x0, (float) timelineHeight - 1.0f));
            g.setColour (accent);
            g.fillRect (juce::Rectangle<float> (x0, (float) rulerTop, x1 - x0, 2.0f));
        }

        // View › "Ruler: song bars" (round 177d, LYNX's own; off = FL's pattern bars): the bar numbers the Playlist shows
        // where the pattern's first clip plays
        const auto songOrigin = songBarsRuler ? polish::patternSongOrigin (song, song.currentPattern) : std::nullopt;
        for (auto b = firstBar; b <= lastBar; ++b)
        {
            const auto x = (int) tickToX ((double) (b * bar));
            g.setColour (textSecondary);
            g.drawVerticalLine (x, (float) rulerTop + 10.0f, (float) headerHeight);
            const std::int64_t number = songOrigin.has_value() ? polish::songBarNumber (*songOrigin, b, bar) : (std::int64_t) b + 1;
            if (number > 0)
                textcache::drawText (g, juce::String (number), x + 3, rulerTop + 2, 40, timelineHeight - 4, juce::Justification::centredLeft);
        }

        // the pattern's time markers (round 168g): a flag and the name in the ruler's lower half, like the Playlist's
        for (const auto& m : currentPattern (song).markers)
        {
            const auto x = (float) tickToX ((double) m.tick);
            if (x < (float) keyboardWidth - 120.0f || x > (float) ruler.getRight())
                continue;
            const auto colour = m.isSignature() ? textPrimary : accent;
            g.setColour (colour);
            g.fillRect (juce::Rectangle<float> (x, (float) rulerTop + 11.0f, 1.5f, (float) timelineHeight - 11.0f));
            juce::Path flag;
            flag.addTriangle (x, (float) rulerTop + 11.0f, x + 6.0f, (float) rulerTop + 14.0f, x, (float) rulerTop + 17.0f);
            g.fillPath (flag);
            auto label = juce::String::fromUTF8 (m.name.c_str());
            if (label.isEmpty() && m.action != 0)
                label = tr (markers::actionName (m.action));
            textcache::drawText (g, label, (int) x + 8, rulerTop + 10, 110, timelineHeight - 11, juce::Justification::centredLeft);
        }

        if (playheadTick >= 0.0)
        {
            const auto x = (float) tickToX (playheadTick);
            juce::Path tri;
            tri.addTriangle (x - 5, (float) rulerTop, x + 5, (float) rulerTop, x, (float) rulerTop + 8);
            g.setColour (playhead);
            g.fillPath (tri);
        }
    }

    // --- keyboard ------------------------------------------------------------
    // flat keys; every C is labelled with its octave, the other white keys too when the rows are tall
    // enough; the key under the mouse (and the one sounding) are lit
    {
        const auto keys = juce::Rectangle<int> (0, headerHeight, keyboardWidth, grid.getHeight());
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (keys);
        g.setColour (bg0);
        g.fillRect (keys);
        const auto fontHeight = gesture::labelHeight (rowHeight);
        const auto blackWidth = keyboardWidth * 3 / 5;
        static const std::map<int, std::string> noLabels;
        const auto selectedChannel = engine.getSelectedChannel();
        const auto& ownLabels = selectedChannel >= 0 && selectedChannel < (int) song.channels.size()
                                    ? song.channels[(std::size_t) selectedChannel].keyLabels : noLabels;
        // a Drum Rack's pad names on its keys (round 190ec, Live's note editor); a key label of the user's wins
        std::map<int, std::string> padLabels;
        if (selectedChannel >= 0 && selectedChannel < (int) song.channels.size() && rack::isDrumRack (song.channels[(std::size_t) selectedChannel]))
        {
            padLabels = rack::padNames (*rack::rackOf (song.channels[(std::size_t) selectedChannel].patcher));
            for (const auto& [key, label] : ownLabels)
                padLabels[key] = label;
        }
        const auto& labels = padLabels.empty() ? ownLabels : padLabels;

        // folded to the scale (round 166d) the keys no longer alternate like a piano: one full-width named row each
        const auto list = folded();
        const auto piano = keyStyle == pianokeys::Style::piano && ! list;
        const auto keyColour = [&] (int key) {
            const auto black = isBlackKey (key) && ! list;
            const auto pressed = key == previewKey || isLit (key);
            auto colour = black ? keyBlack : keyWhite;
            if (inScale (key))
                colour = colour.interpolatedWith (info, black ? 0.18f : 0.12f);
            if (key == hover.key && ! pressed)
                colour = colour.interpolatedWith (accent, black ? 0.35f : 0.22f);
            return pressed ? accent : colour;
        };
        for (int key = 0; key < 128; ++key)
        {
            const auto y = keyToY (key);

            if (! rows.shown (key) || y > keys.getBottom() || y + rowHeight < keys.getY())
                continue;

            const auto black = isBlackKey (key) && ! list;
            const auto pressed = key == previewKey || isLit (key);
            const auto hovered = key == hover.key && ! pressed;
            const auto colour = keyColour (key);
            const auto width = black ? blackWidth : keyboardWidth - 1;

            if (piano)
            {
                // "Piano keys" (round 190j, discussion #136): the white keys meet behind the black key, each in its own
                // colour (hover, pressed, scale), with a dark line between every two white keys
                const auto line = bg0.withAlpha (0.85f);
                if (black)
                {
                    const auto upper = pianokeys::upperPart (rowHeight);
                    g.setColour (keyColour (juce::jmin (127, key + 1)));
                    g.fillRect (0, y, keyboardWidth - 1, upper);
                    g.setColour (keyColour (juce::jmax (0, key - 1)));
                    g.fillRect (0, y + upper, keyboardWidth - 1, rowHeight - upper);
                    g.setColour (line);
                    g.drawHorizontalLine (y + upper, (float) blackWidth, (float) (keyboardWidth - 1));
                    // the black key: a little shorter than its row when there is room, so the white shows around it
                    const auto inset = rowHeight >= 12 ? 1 : 0;
                    g.setColour (bg0);
                    g.fillRect (0, y + inset, blackWidth + 1, rowHeight - 2 * inset);
                    g.setColour (colour);
                    g.fillRect (0, y + inset, blackWidth, rowHeight - 2 * inset);
                    if (! pressed && rowHeight >= 12) // a light edge along the top, as on a real black key
                    {
                        g.setColour (keyWhite.withAlpha (0.14f));
                        g.drawHorizontalLine (y + inset, 0.0f, (float) blackWidth);
                    }
                }
                else
                {
                    g.setColour (colour);
                    g.fillRect (0, y, width, rowHeight);
                    if (pianokeys::lineBelow (key))
                    {
                        g.setColour (line);
                        g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) width);
                    }
                }
            }
            else
            {
                g.setColour (colour);
                g.fillRect (0, y, width, rowHeight);
                g.setColour (black ? bg0 : keyWhite.darker (0.3f));
                g.drawHorizontalLine (y + rowHeight - 1, 0.0f, (float) width);
            }

            const auto isC = key % 12 == 0;

            // every key named (LYNX, on by default: notenames::everyKey) once the rows are tall enough to read
            const auto everyKey = notenames::everyKey.load (std::memory_order_relaxed) && rowHeight >= 8;
            if (isC || hovered || pressed || everyKey || (! black && rowHeight >= (list ? 8 : 16)))
            {
                g.setFont (theme::font (fontHeight, isC));
                g.setColour (pressed ? onAccent : black ? textPrimary.withAlpha (hovered ? 1.0f : 0.85f) : isC ? bg0 : bg0.withAlpha (everyKey ? 0.72f : 0.55f));
                const auto area = black ? juce::Rectangle<int> (2, y, blackWidth - 4, rowHeight)
                                        : juce::Rectangle<int> (blackWidth, y, keyboardWidth - blackWidth - 5, rowHeight);
                textcache::drawText (g, keyName (key), area, black ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
            }

            // a key label (FL 2026's custom key labels) replaces the name on the white part of the key
            if (const auto label = labels.find (key); label != labels.end() && rowHeight >= 8)
            {
                const auto area = juce::Rectangle<int> (black ? blackWidth + 2 : 3, y, keyboardWidth - (black ? blackWidth + 2 : 3) - 4, rowHeight);
                g.setColour (black ? keyWhite : colour);
                g.fillRect (area);
                g.setFont (theme::font (fontHeight, false));
                g.setColour (pressed ? onAccent : bg0);
                g.drawFittedText (juce::String::fromUTF8 (label->second.c_str()), area.reduced (1, 0), juce::Justification::centredLeft, 1, 0.8f);
            }
            if (! padRows.empty()) // (a Drum Rack's pad rows: a strip at the right in the key's piano colour, as Live's list)
            {
                g.setColour (isBlackKey (key) ? keyBlack : keyWhite.darker (0.25f));
                g.fillRect (keyboardWidth - 12, y, 11, rowHeight - 1);
            }
        }

        g.setColour (border);
        g.fillRect (keyboardWidth - 1, keys.getY(), 1, keys.getHeight());
    }

    // velocity lane label
    {
        const auto lane = velocityArea();
        g.setColour (bg2);
        g.fillRect (0, lane.getY(), keyboardWidth, lane.getHeight());
        juce::ignoreUnused (lane); // the property combo sits here
    }

    // the zoom wheel (round 190j): FL's small square with a mark in its middle, at the end of the time scroll bar
    {
        const auto wheel = zoomWheelArea().toFloat().reduced (1.0f);
        const auto over = drag == Drag::keyZoom;
        g.setColour (bg2);
        g.fillRect (zoomWheelArea());
        g.setColour (over ? bg5 : bg4);
        g.fillRoundedRectangle (wheel, 2.0f);
        g.setColour (over ? textPrimary : textSecondary);
        g.fillRect (juce::Rectangle<float> (4.0f, 5.0f).withCentre (wheel.getCentre()));
    }

    // top-left corner: channel name
    g.setColour (bg3);
    g.fillRect (0, 0, keyboardWidth, toolbarHeight);
    g.setColour (bg2);
    g.fillRect (0, toolbarHeight, keyboardWidth, headerHeight - toolbarHeight);
    g.setColour (border);
    g.fillRect (0, headerHeight - 1, keyboardWidth, 1);
    g.fillRect (keyboardWidth - 1, 0, 1, headerHeight);

    if (channel < (int) song.channels.size())
    {
        const auto& ch = song.channels[(std::size_t) channel];
        g.setColour (ch.colour != 0 ? theme::tame (juce::Colour (ch.colour)) : accent);
        g.fillRoundedRectangle (4.0f, (float) toolbarHeight + 6.0f, 3.0f, (float) (headerHeight - toolbarHeight) - 12.0f, 1.0f);
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::caption, true));
        textcache::drawFittedText (g, juce::String::fromUTF8 (ch.name.c_str()), 11, toolbarHeight, keyboardWidth - 14, headerHeight - toolbarHeight,
                          juce::Justification::centredLeft, 2, 0.8f);
        // Freeze (round 190ay): the frozen mark over the notes' top right corner, with what to do
        if (ch.freeze.on())
        {
            const auto text = tr ("Frozen: Unfreeze to edit");
            g.setFont (theme::font (theme::type::caption, true));
            const auto grid = gridArea();
            const auto width = juce::jmin (grid.getWidth() - 8, 34 + juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text));
            if (width > 40)
            {
                const auto pill = juce::Rectangle<float> ((float) grid.getRight() - (float) width - 6.0f, (float) grid.getY() + 6.0f, (float) width, 20.0f);
                theme::fillRoundRect (g, pill, theme::radius::md, bg1.withAlpha (0.88f));
                theme::drawFrozenTag (g, juce::Rectangle<float> (pill.getX() + 4.0f, pill.getCentreY() - 6.5f, 14.0f, 13.0f), info.withAlpha (0.85f), bg1);
                g.setColour (textSecondary);
                g.drawText (text, pill.withTrimmedLeft (24.0f).withTrimmedRight (6.0f), juce::Justification::centredLeft, true);
            }
        }
    }
}

// --- mouse -------------------------------------------------------------------

int PianoRollComponent::noteAt (int x, int y, bool& onRightEdge) const
{
    bool left = false;
    return noteAt (x, y, onRightEdge, left);
}

int PianoRollComponent::noteAt (int x, int y, bool& onRightEdge, bool& onLeftEdge) const
{
    // the same hit test as the hover (cursor and highlight), so a click does what the cursor showed
    const auto h = hoverAt ({ x, y });
    onRightEdge = h.zone == gesture::Zone::rightEdge;
    onLeftEdge = h.zone == gesture::Zone::leftEdge;
    return gridArea().contains (x, y) ? h.note : -1;
}

int PianoRollComponent::ghostAt (int x, int y) const
{
    if (! showGhosts)
        return -1;

    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto key = yToKey (y);

    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(std::size_t) i];

        if (! edits (n) && n.key == key && ghostShown (n.channel) && x >= tickToX ((double) n.start) && x < tickToX ((double) (n.start + n.length)))
            return i;
    }

    return -1;
}

float PianoRollComponent::keyVelocityAt (int x, int key) const
{
    // FL: a click near the left of a key plays it softly, at its right end at full velocity (black keys are shorter)
    return gesture::keyClickVelocity ((double) x, (double) (isBlackKey (key) ? keyboardWidth * 3 / 5 : keyboardWidth - 1));
}

void PianoRollComponent::previewOn (int key, float velocity)
{
    // one preview note at a time; it remembers its channel, so a channel switch meanwhile can't strand it
    previewNotes.releaseAll();
    previewKey = key;
    previewNotes.press (engine.getSelectedChannel(), key, velocity);
    repaint (0, headerHeight, keyboardWidth, getHeight());
}

void PianoRollComponent::previewOff()
{
    previewNotes.releaseAll();

    if (previewKey >= 0)
    {
        previewKey = -1;
        repaint (0, headerHeight, keyboardWidth, getHeight());
    }
}

void PianoRollComponent::visibilityChanged()
{
    if (! isShowing()) // hidden mid-gesture (window closed, tab switched): nothing keeps sounding
    {
        previewOff();
        stopListening();
    }
}

// The note properties the bottom lane edits: FL's (velocity, pan, fine pitch, release, Mod X / Y: its "Control" list,
// FLEngine strings "Note release", "Note modulation X / Y"), then Live 12's Chance and Velocity Range (round 187c, LYNX
// additions marked "(LYNX)")
void PianoRollComponent::addNoteLanes()
{
    laneBox.addItem (tr ("Velocity"), laneVelocity);
    laneBox.addItem (tr ("Pan"), lanePan);
    laneBox.addItem (tr ("Fine pitch"), laneFinePitch);
    laneBox.addItem (tr ("Release"), laneRelease);
    laneBox.addItem (tr ("Mod X"), laneModX);
    laneBox.addItem (tr ("Mod Y"), laneModY);
    laneBox.addItem (tr ("Chance"), laneChance);
    laneBox.addItem (tr ("Velocity range (LYNX)"), laneVelocityRange);
}

float PianoRollComponent::laneValue (const Note& n) const
{
    switch (laneBox.getSelectedId())
    {
        case lanePan: return (n.pan + 1.0f) * 0.5f;
        case laneFinePitch: return (n.finePitch + 120.0f) / 240.0f;
        case laneRelease: return n.release;
        case laneModX: return n.modX;
        case laneModY: return n.modY;
        case laneChance: return n.chance;
        case laneVelocityRange: return (n.velocityRange + 1.0f) * 0.5f;
        default: return n.velocity;
    }
}

void PianoRollComponent::setLaneValue (Note& n, float v) const
{
    v = juce::jlimit (0.0f, 1.0f, v);

    switch (laneBox.getSelectedId())
    {
        case lanePan: n.pan = std::abs (v - 0.5f) < 0.02f ? 0.0f : v * 2.0f - 1.0f; break;   // snaps to center
        case laneFinePitch: n.finePitch = std::round (v * 240.0f - 120.0f); break;
        case laneRelease: n.release = v; break;
        case laneModX: n.modX = v; break;
        case laneModY: n.modY = v; break;
        case laneChance: n.chance = std::round (v * 100.0f) / 100.0f; break; // whole percents, as Live shows them
        case laneVelocityRange: // whole Live velocity steps (1/127); snaps to none
            n.velocityRange = std::abs (v - 0.5f) < 0.02f ? 0.0f : std::round ((v * 2.0f - 1.0f) * 127.0f) / 127.0f;
            break;
        default: n.velocity = v; break;
    }
}

bool PianoRollComponent::laneIsBipolar() const
{
    const auto id = laneBox.getSelectedId();
    return id == lanePan || id == laneFinePitch || id == laneVelocityRange || id >= eventLaneIdBase;
}

bool PianoRollComponent::laneIsChance() const { return laneBox.getSelectedId() == laneChance; }

void PianoRollComponent::applyVelocity (int fromX, int toX, int y)
{
    const auto lane = velocityArea();
    applyLaneValue (toX, juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - y) / (float) (lane.getHeight() - 8)), fromX);
}

void PianoRollComponent::applyLaneValue (int toX, float value, int fromX)
{
    const auto lo = juce::jmin (fromX, toX) - 3, hi = juce::jmax (fromX, toX) + 3;
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<int> hits;

    for (auto i : selectionOrAll())
    {
        const auto x = (int) tickToX ((double) notes[(std::size_t) i].start);

        if (x >= lo && x <= hi)
            hits.push_back (i);
    }

    if (! hits.empty())
        editNotes ([this, &hits, value] (Pattern& p) {
            for (auto i : hits)
                setLaneValue (p.notes[(std::size_t) i], value);
            if (laneIsChance())
                notetools::spreadGroupChance (p, hits); // a probability group has one Chance
        });
}

void PianoRollComponent::togglePorta()
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    // like mute: if any note lacks porta, enable it on all; otherwise disable
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto porta = std::any_of (target.begin(), target.end(), [&notes] (int i) { return ! notes[(std::size_t) i].porta; });
    editNotes ([&target, porta] (Pattern& p) {
        for (auto i : target)
            p.notes[(std::size_t) i].porta = porta;
    });
}

void PianoRollComponent::setNotesMuted (bool mute)
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    editNotes ([&target, mute] (Pattern& p) {
        for (auto i : target)
            p.notes[(std::size_t) i].muted = mute;
    });
}

void PianoRollComponent::paintNoteAt (std::int64_t start, int key, bool monophonic)
{
    // a brush stroke lays single notes (or the chosen chord) at the default length
    Note n;
    noteTemplate.applyTo (n);
    n.start = juce::jmax<std::int64_t> (0, start);
    n.length = paintLength;
    n.key = key;
    n.channel = engine.getSelectedChannel();
    n.slide = slideButton.getToggleState();
    n.colour = noteColour;

    std::vector<Note> stamp;
    for (auto interval : chordIntervals())
        if (n.key + interval <= 127 && n.key + interval >= 0)
        {
            auto chordNote = n;
            chordNote.key = snapKey (n.key + interval); // with scale snap the chord follows the scale
            if (std::none_of (stamp.begin(), stamp.end(), [&chordNote] (const Note& o) { return o.key == chordNote.key; }))
                stamp.push_back (chordNote);
        }

    editNotes ([&] (Pattern& p) {
        if (monophonic) // Shift (or no Shift with "Monophonic painting"): the new note replaces what this channel plays then
        {
            std::vector<int> overlapping;
            for (int i = 0; i < (int) p.notes.size(); ++i)
            {
                const auto& o = p.notes[(std::size_t) i];
                if (o.channel == n.channel && o.start < n.start + n.length && o.start + o.length > n.start)
                    overlapping.push_back (i);
            }
            noteedit::remove (p, overlapping);
        }

        noteedit::paste (p, stamp, 0);
    });
}

juce::Rectangle<int> PianoRollComponent::zoomWheelArea() const
{
    // FL's VZoomSelect: the small square at the right end of the time scroll bar, over the vertical scroll bar
    return { getWidth() - zoomWheelSize, toolbarHeight, zoomWheelSize, overviewHeight };
}

int PianoRollComponent::zoomAnchorKey() const
{
    // FL (FUN_00e83850, REA round 167t): the middle of the notes in view, or the middle of the view
    const auto grid = gridArea();
    const auto top = yToKey (grid.getY()), bottom = yToKey (grid.getBottom() - 1);
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<int> keys;
    for (auto i : channelNotes())
        if (const auto key = notes[(std::size_t) i].key; key <= top && key >= bottom)
            keys.push_back (key);
    return gesture::zoomWheelAnchor (keys, top, bottom);
}

void PianoRollComponent::setRowHeightAround (int height, int key, int centreY)
{
    rowHeight = juce::jlimit (gesture::minKeyHeight, gesture::maxKeyHeight, height);
    scrollY = juce::jlimit (0, juce::jmax (0, rows.count * rowHeight - gridArea().getHeight()), rowFor (key) * rowHeight + rowHeight / 2 - (centreY - headerHeight));
    updateScrollBars();
    repaint();
}

void PianoRollComponent::beginKeyZoom (const juce::MouseEvent& e)
{
    drag = Drag::keyZoom;
    keyZoomKey = zoomWheelArea().contains (e.getPosition()) ? zoomAnchorKey() : yToKey (e.y);
    keyZoomStartRow = rowHeight;
    keyZoomCentreY = keyToY (keyZoomKey) + rowHeight / 2;
    keyZoomDrag.begin (e.y);
    e.source.enableUnboundedMouseMovement (true); // FL hides the pointer while the zoom follows the travel
}

void PianoRollComponent::beginTimeZoom (const juce::MouseEvent& e)
{
    drag = Drag::timeZoom;
    const auto width = (double) juce::jmax (1, gridArea().getWidth());
    timeZoomDrag.begin (width / pixelsPerTick, scrollTicks + width * 0.5 / pixelsPerTick, e.x, e.y);
    e.source.enableUnboundedMouseMovement (true);
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (engine.getSong().channels.empty())
        return;

    downMods = e.mods;
    startedOnEmpty = erasedSomething = holdExpired = false;

    const auto onKeyboard = e.x < keyboardWidth && e.y >= headerHeight && e.y < gridArea().getBottom();
    hover = {};
    gliding = false;

    // the zoom wheel (round 190j, discussion #10; FL's VZoomSelect): drag up = taller keys, one level (2 px) per 4 px of
    // travel, per 16 px with Ctrl
    if (zoomWheelArea().contains (e.getPosition()))
    {
        if (e.mods.isLeftButtonDown() || e.mods.isMiddleButtonDown())
            beginKeyZoom (e);
        return;
    }

    if (e.mods.isMiddleButtonDown())
    {
        // FL 2026 (round 167t, issue #26): middle-drag on the keyboard and Ctrl+middle-drag on the notes zoom
        // vertically around the key under the mouse; middle-drag on the ruler and Alt+middle-drag on the notes
        // zoom the time (drag up = zoom in, sideways = scroll)
        const auto onNotes = gridArea().contains (e.getPosition());
        const auto onRuler = e.y >= rulerTop && e.y < headerHeight && e.x >= keyboardWidth;
        if (onKeyboard || (onNotes && flmods::ctrl (e.mods) && ! e.mods.isAltDown()))
        {
            beginKeyZoom (e);
            return;
        }
        if (onRuler || (onNotes && e.mods.isAltDown()))
        {
            beginTimeZoom (e);
            return;
        }

        drag = Drag::pan; // elsewhere it pans both ways, in every tool
        panStartTicks = scrollTicks;
        panStartY = scrollY;
        setMouseCursor (editorCursor (gesture::Cursor::pan));
        return;
    }

    if (onKeyboard)
    {
        if (flmods::ctrl (e.mods)) // Ctrl+click / drag: selects the notes of a key (range)
        {
            drag = Drag::keySelect;
            selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
            mouseDrag (e);
            return;
        }

        if (e.mods.isRightButtonDown()) // key labels (FL 2026: right-click a key > Rename)
        {
            showKeyMenu (keyboardKeyAt (e.x, e.y));
            return;
        }

        if (e.mods.isLeftButtonDown())
        {
            drag = Drag::keyboard;
            previewOn (keyboardKeyAt (e.x, e.y), keyVelocityAt (e.x, keyboardKeyAt (e.x, e.y)));
        }
        return;
    }

    // ruler: a right press on a pattern marker opens its menu (FL's TimePanelMouseDown: the marker menu when the press
    // is on the marker lane, REA_rodada168t.md table); the Time markers items are also in the More menu (round 168g)
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= keyboardWidth && e.mods.isRightButtonDown() && ! flmods::ctrl (e.mods))
        if (const auto mk = markerAtX (e.x); mk >= 0)
        {
            showPatternMarkerMenu (mk);
            return;
        }

    // ruler: a time-range drag selects the notes in it. FL 2026 (one ruler handler for the Piano roll and the Playlist,
    // TimePanelMouseDown 0xe56bf0, REA_rodada168t.md): Ctrl+drag, a double click held and dragged, the Select tool, or
    // a right-drag (on a Mac Cmd or Control: core/ModifierMap.h)
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= keyboardWidth
        && ((e.mods.isLeftButtonDown() && (flmods::ctrl (e.mods) || e.getNumberOfClicks() >= 2 || tools.getTool() == EditTool::lasso))
            || (e.mods.isRightButtonDown() && ! flmods::ctrl (e.mods))))
    {
        drag = Drag::rulerSelect;
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        return;
    }

    // ruler: click or drag = position (FL), Alt = off the grid
    if (e.y >= rulerTop && e.y < headerHeight && e.x >= keyboardWidth && e.mods.isLeftButtonDown() && ! e.mods.isShiftDown())
    {
        drag = Drag::seek;
        seekAt (e.x, ! e.mods.isAltDown());
        return;
    }

    // Freeze (round 190ay): a frozen channel's notes and their lane are read-only (the keyboard, the ruler and the
    // view gestures above still work)
    if ((velocityArea().contains (e.getPosition()) || gridArea().contains (e.getPosition())) && refuseFrozenEdit())
        return;

    if (velocityArea().contains (e.getPosition()) && eventLaneTarget())
    {
        // pattern events: drag draws one event per grid step, right-drag erases
        lastVelocityX = e.x;
        drag = e.mods.isRightButtonDown() ? Drag::eventErase : Drag::eventDraw;
        drawEvents (e.x, e.x, e.y, drag == Drag::eventErase);
        return;
    }

    if (velocityArea().contains (e.getPosition()))
    {
        const auto lane = velocityArea();
        const auto value = juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - e.y) / (float) (lane.getHeight() - 8));
        lastVelocityX = e.x;

        if (e.mods.isAltDown() && e.mods.isLeftButtonDown()) // Alt+click resets the notes under the mouse
        {
            drag = Drag::velocity;
            applyLaneValue (e.x, laneValue (Note()), e.x); // (each property's default)
            drag = Drag::none;
            return;
        }

        laneOriginals = currentPattern (engine.getSong()).notes;

        if (e.mods.isRightButtonDown()) // right-drag draws a straight line of values
        {
            drag = Drag::velocityLine;
            laneSetValue = value;
            return;
        }

        if (e.mods.isShiftDown()) // Shift+drag: every note crossed gets the value of the press
        {
            drag = Drag::velocitySet;
            laneSetValue = value;
            applyLaneValue (e.x, value, e.x);
            return;
        }

        drag = Drag::velocity;
        applyVelocity (e.x, e.x, e.y);
        return;
    }

    if (! gridArea().contains (e.getPosition()))
        return;

    // the selection's stretch handle: drag scales the selected notes in time from the selection's start
    if (e.mods.isLeftButtonDown() && ! flmods::ctrl (e.mods) && stretchHandle().expanded (2.0f).contains (e.position))
    {
        const auto& notes = currentPattern (engine.getSong()).notes;
        dragIndices.clear();
        dragOriginals.clear();

        for (auto i : selection)
            if (i >= 0 && i < (int) notes.size() && edits (notes[(std::size_t) i]))
            {
                dragIndices.push_back (i);
                dragOriginals.push_back (notes[(std::size_t) i]);
            }

        noteedit::span (dragOriginals, stretchAnchor, stretchEnd);
        dragNote = dragIndices.empty() ? -1 : dragIndices.front();
        drag = Drag::stretch;
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    bool rightEdge = false, leftEdge = false;
    const auto hit = noteAt (e.x, e.y, rightEdge, leftEdge);
    const auto tool = tools.getTool();

    // editable ghost notes (Ctrl+Alt+V): a left press on one switches to its channel and grabs the note there
    if (hit < 0 && editGhosts && e.mods.isLeftButtonDown() && ! flmods::ctrl (e.mods))
        if (const auto ghost = ghostAt (e.x, e.y); ghost >= 0)
        {
            engine.setSelectedChannel (currentPattern (engine.getSong()).notes[(std::size_t) ghost].channel);
            if (noteAt (e.x, e.y, rightEdge, leftEdge) >= 0) // now it is one of this channel's notes
                mouseDown (e);
            return;
        }

    // Zoom tool: right-click zooms out around the point (a click zooms in, a drag zooms to the range)
    if (tool == EditTool::zoom && e.mods.isRightButtonDown() && ! flmods::ctrl (e.mods))
    {
        zoomOut (e.x);
        return;
    }

    // every tool: right click deletes (double right-click on a ghost note switches to its channel; with
    // editable ghost notes a right-click deletes the ghost note itself)
    if (e.mods.isRightButtonDown())
    {
        if (hit < 0 && editGhosts)
            if (const auto ghost = ghostAt (e.x, e.y); ghost >= 0)
            {
                setSelection ({});
                editNotes ([ghost] (Pattern& p) { noteedit::remove (p, { ghost }); });
                return;
            }

        if (hit < 0 && e.getNumberOfClicks() >= 2)
            if (const auto ghost = ghostAt (e.x, e.y); ghost >= 0)
            {
                engine.setSelectedChannel (currentPattern (engine.getSong()).notes[(std::size_t) ghost].channel);
                return;
            }

        drag = Drag::erase;
        startedOnEmpty = hit < 0;
        setSelection ({}); // indices change while deleting

        if (hit >= 0)
        {
            erasedSomething = true;
            editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
        }
        return;
    }

    // every tool: Ctrl+click on a note adds/removes it from the selection; Ctrl+drag selects (Shift adds)
    if (hit >= 0 && flmods::ctrl (e.mods))
    {
        auto s = selection;

        if (isSelected (hit))
            s.erase (std::find (s.begin(), s.end(), hit));
        else
            s.push_back (hit);

        setSelection (s);
        return;
    }

    if ((hit < 0 && flmods::ctrl (e.mods)) || tool == EditTool::lasso
        || (hit < 0 && e.mods.isShiftDown() && tool != EditTool::brush && tool != EditTool::drumPaint))
    {
        drag = Drag::select;
        dragNote = hit; // a click without dragging selects this note (Lasso)
        selectionBefore = e.mods.isShiftDown() ? selection : std::vector<int> {};
        rubberBand = {};
        setSelection (selectionBefore);
        return;
    }

    const auto snapOn = ! e.mods.isAltDown();

    switch (tool)
    {
        case EditTool::eraser:
            drag = Drag::erase;
            setSelection ({});
            if (hit >= 0)
                editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
            return;

        case EditTool::mute:
            drag = Drag::muting;
            touchedNotes.clear();
            if (hit >= 0)
            {
                muteTarget = ! currentPattern (engine.getSong()).notes[(std::size_t) hit].muted;
                touchedNotes.push_back (hit);
                editNotes ([hit, m = muteTarget] (Pattern& p) { p.notes[(std::size_t) hit].muted = m; });
            }
            else
                muteTarget = true;
            return;

        case EditTool::knife:
            drag = Drag::knife;
            knifeFrom = knifeTo = e.getPosition();
            repaint();
            return;

        case EditTool::zoom:
            drag = Drag::zoomRange;
            rubberBand = {};
            return;

        case EditTool::listen:
            drag = Drag::listen;
            listenAt (e.x);
            return;

        case EditTool::pencil:
        case EditTool::brush:
        case EditTool::drumPaint:
        case EditTool::lasso:
        case EditTool::slip:
            break;
    }

    // Pencil (and Brush on an existing note): Shift+drag on a note drags a copy
    if (hit >= 0 && e.mods.isShiftDown() && ! rightEdge && ! leftEdge)
    {
        const auto sources = isSelected (hit) ? selection : notetools::groupOf (currentPattern (engine.getSong()), hit);
        const auto grabbedAt = (std::size_t) (std::find (sources.begin(), sources.end(), hit) - sources.begin());
        std::vector<int> added;

        // the copy comes out in the same order as the originals (copy sorts by start)
        std::vector<Note> ordered;
        for (auto i : sources)
            ordered.push_back (currentPattern (engine.getSong()).notes[(std::size_t) i]);

        editNotes ([&] (Pattern& p) {
            added = noteedit::paste (p, ordered, 0);
            notetools::regroup (p, added);
        });
        setSelection (added);
        dragIndices = added;
        dragOriginals = ordered;
        lastScaleDragKeys = -1000;
        dragNote = added[grabbedAt];
        drag = Drag::move;
        grabOffset = ordered[grabbedAt].start - snap (xToTick (e.x), snapOn);
        previewOn (ordered[grabbedAt].key, ordered[grabbedAt].velocity);
        return;
    }

    if (hit >= 0)
    {
        if (! isSelected (hit)) // a grouped note brings its group along (FL's note groups)
        {
            const auto members = notetools::groupOf (currentPattern (engine.getSong()), hit);
            setSelection (members.size() > 1 ? members : std::vector<int> {});
        }

        const auto& notes = currentPattern (engine.getSong()).notes;
        const auto& n = notes[(std::size_t) hit];
        dragNote = hit;
        dragIndices = selection.empty() ? std::vector<int> { hit } : selection;
        dragOriginals.clear();

        for (auto i : dragIndices)
            dragOriginals.push_back (notes[(std::size_t) i]);

        drag = rightEdge ? Drag::resize : leftEdge ? Drag::resizeLeft : Drag::move;
        lastScaleDragKeys = 0; // (no scale step made yet)
        grabOffset = n.start - snap (xToTick (e.x), snapOn);
        lastLength = n.length;
        noteTemplate.takeFrom (n); // FL: new notes get the clicked note's properties (round 166e)
        previewOn (n.key, n.velocity);
        return;
    }

    // new note (or the chosen chord, with the clicked note as the root)
    setSelection ({});
    createdAt = juce::Time::getMillisecondCounter();

    if (tool == EditTool::drumPaint)
    {
        drag = Drag::drumPaint;
        lastDrumCell = -1;
        lastDrumKey = -1;
        drumPaintAt (e.getPosition(), snapOn);
        return;
    }

    if (tool == EditTool::brush)
    {
        drag = Drag::paint;
        paintLength = juce::jmax<std::int64_t> (1, newNoteLength());
        const auto start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
        paintNoteAt (start, snapKey (yToKey (e.y)), noteedit::monophonicPaint (e.mods.isShiftDown(), monophonicPainting));
        nextPaintTick = start + paintLength;
        previewOn (snapKey (yToKey (e.y)), noteTemplate.velocity);
        return;
    }

    Note n;
    noteTemplate.applyTo (n);
    n.start = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn));
    n.length = newNoteLength();
    n.key = snapKey (yToKey (e.y));
    n.channel = engine.getSelectedChannel();
    n.slide = slideButton.getToggleState();
    n.colour = noteColour;

    std::vector<Note> stamp;
    for (auto interval : chordIntervals())
        if (n.key + interval <= 127 && n.key + interval >= 0)
        {
            auto chordNote = n;
            chordNote.key = snapKey (n.key + interval); // with scale snap the chord follows the scale
            if (std::none_of (stamp.begin(), stamp.end(), [&chordNote] (const Note& o) { return o.key == chordNote.key; }))
                stamp.push_back (chordNote);
        }

    std::vector<int> added;
    editNotes ([&] (Pattern& p) { added = noteedit::paste (p, stamp, 0); });
    dragNote = added.front(); // the root leads the drag
    dragIndices = added;
    dragOriginals = stamp;
    lastScaleDragKeys = -1000;

    if (added.size() > 1)
        setSelection (added);
    drag = Drag::move;
    grabOffset = 0;
    previewOn (n.key, n.velocity);
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    const auto snapOn = ! e.mods.isAltDown();
    // after the step below, the hint bar says what the gesture is doing
    const struct PostAfter
    {
        PianoRollComponent& roll;
        const juce::MouseEvent& event;
        ~PostAfter() { roll.postDragHint (event); }
    } postAfter { *this, e };

    switch (drag)
    {
        case Drag::none:
            return;

        case Drag::pan:
            scrollTicks = juce::jmax (0.0, panStartTicks - e.getDistanceFromDragStartX() / pixelsPerTick);
            scrollY = juce::jlimit (0, juce::jmax (0, rows.count * rowHeight - gridArea().getHeight()), panStartY - e.getDistanceFromDragStartY());
            updateScrollBars();
            repaint();
            return;

        case Drag::keyboard:
        {
            const auto key = keyboardKeyAt (e.x, e.y);
            if (key != previewKey)
                previewOn (key, keyVelocityAt (e.x, key));
            return;
        }

        case Drag::keyZoom:
        {
            // up = taller keys (Ctrl: fine); the centre of the clicked key stays where it was
            keyZoomDrag.move (e.y, flmods::ctrl (e.mods));
            rowHeight = keyZoomDrag.height (keyZoomStartRow);
            scrollY = juce::jlimit (0, juce::jmax (0, rows.count * rowHeight - gridArea().getHeight()),
                                    rowFor (keyZoomKey) * rowHeight + rowHeight / 2 - (keyZoomCentreY - headerHeight));
            updateScrollBars();
            repaint();
            return;
        }

        case Drag::timeZoom:
        {
            const auto width = (double) juce::jmax (1, gridArea().getWidth());
            timeZoomDrag.move (e.x, e.y, flmods::ctrl (e.mods));
            pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, width / timeZoomDrag.span);
            scrollTicks = juce::jmax (0.0, timeZoomDrag.centre - width * 0.5 / pixelsPerTick);
            updateScrollBars();
            repaint();
            return;
        }

        case Drag::keySelect:
        {
            const auto k0 = yToKey (e.getMouseDownY()), k1 = yToKey (e.y);
            const auto& notes = currentPattern (engine.getSong()).notes;
            auto s = selectionBefore;
            for (auto i : channelNotes())
                if (const auto k = notes[(std::size_t) i].key; k >= juce::jmin (k0, k1) && k <= juce::jmax (k0, k1))
                    s.push_back (i);
            setSelection (s);
            return;
        }

        case Drag::rulerSelect:
        {
            const auto t0 = (double) snap (xToTick (juce::jmin (e.getMouseDownX(), e.x)), snapOn);
            const auto t1 = (double) snap (xToTick (juce::jmax (e.getMouseDownX(), e.x)) + (snapOn ? (double) gridTicks() : 0.0), snapOn);
            const auto& notes = currentPattern (engine.getSong()).notes;
            auto s = selectionBefore;
            for (auto i : channelNotes())
                if (const auto& n = notes[(std::size_t) i]; (double) n.start >= t0 && (double) n.start < t1)
                    s.push_back (i);
            rubberBand = juce::Rectangle<int>::leftTopRightBottom ((int) tickToX (t0), gridArea().getY(), (int) tickToX (t1), gridArea().getBottom());
            timeFrom = (std::int64_t) t0; // (round 168g: the range stays as the time selection)
            timeTo = (std::int64_t) t1;
            setSelection (s);
            return;
        }

        case Drag::velocity:
            applyVelocity (lastVelocityX, e.x, e.y);
            lastVelocityX = e.x;
            return;

        case Drag::eventDraw:
        case Drag::eventErase:
            drawEvents (lastVelocityX, e.x, e.y, drag == Drag::eventErase);
            lastVelocityX = e.x;
            return;

        case Drag::velocitySet:
            applyLaneValue (e.x, laneSetValue, lastVelocityX);
            lastVelocityX = e.x;
            return;

        case Drag::velocityLine:
        {
            // from the press to the mouse: notes in between follow the line, the others keep their value
            const auto lane = velocityArea();
            const auto value = juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - e.y) / (float) (lane.getHeight() - 8));
            const auto x0 = e.getMouseDownX(), x1 = e.x;
            const auto lo = juce::jmin (x0, x1) - 3, hi = juce::jmax (x0, x1) + 3;
            const auto targets = selectionOrAll();
            knifeFrom = { x0, juce::roundToInt ((float) lane.getBottom() - 2.0f - laneSetValue * (float) (lane.getHeight() - 8)) };
            knifeTo = e.getPosition();
            editNotes ([&] (Pattern& p) {
                for (auto i : targets)
                {
                    if ((std::size_t) i >= laneOriginals.size() || (std::size_t) i >= p.notes.size())
                        continue;
                    auto& n = p.notes[(std::size_t) i];
                    n = laneOriginals[(std::size_t) i];
                    const auto x = (int) tickToX ((double) n.start);
                    if (x >= lo && x <= hi)
                        setLaneValue (n, (float) gesture::lineValue (x0, laneSetValue, x1, value, x));
                }
                if (laneIsChance())
                    notetools::spreadGroupChance (p, targets);
            });
            repaint();
            return;
        }

        case Drag::select:
        case Drag::zoomRange:
        {
            if (e.getDistanceFromDragStart() < 3 && rubberBand.isEmpty())
                return;

            const auto area = gridArea();
            rubberBand = juce::Rectangle<int> (e.getMouseDownPosition(), e.getPosition()).getIntersection (area);

            if (drag == Drag::zoomRange)
            {
                rubberBand = rubberBand.withY (area.getY()).withHeight (area.getHeight());
                repaint();
                return;
            }

            const auto t0 = xToTick (rubberBand.getX()), t1 = xToTick (rubberBand.getRight());
            const auto kTop = yToKey (rubberBand.getY()), kBottom = yToKey (rubberBand.getBottom());
            const auto& notes = currentPattern (engine.getSong()).notes;
            auto s = selectionBefore;

            for (auto i : channelNotes())
            {
                const auto& n = notes[(std::size_t) i];

                if ((double) (n.start + n.length) > t0 && (double) n.start < t1 && n.key <= kTop && n.key >= kBottom)
                    s.push_back (i);
            }

            setSelection (s);
            return;
        }

        case Drag::erase:
        {
            // right press-and-hold on empty grid, then drag: a temporary Knife
            if (e.mods.isRightButtonDown() && startedOnEmpty && ! erasedSomething && ! holdExpired)
            {
                if (e.getLengthOfMousePress() >= 350)
                {
                    drag = Drag::knife;
                    knifeFrom = e.getMouseDownPosition();
                    knifeTo = e.getPosition();
                    repaint();
                    return;
                }

                if (e.getDistanceFromDragStart() > 3)
                    holdExpired = true; // moved right away: it is an erase stroke
            }

            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0)
            {
                erasedSomething = true;
                editNotes ([hit] (Pattern& p) { noteedit::remove (p, { hit }); });
            }
            return;
        }

        case Drag::muting:
        {
            bool edge = false;
            const auto hit = noteAt (e.x, e.y, edge);

            if (hit >= 0 && std::find (touchedNotes.begin(), touchedNotes.end(), hit) == touchedNotes.end())
            {
                touchedNotes.push_back (hit);
                editNotes ([hit, m = muteTarget] (Pattern& p) { p.notes[(std::size_t) hit].muted = m; });
            }
            return;
        }

        case Drag::knife:
            knifeTo = e.getPosition();
            repaint();
            return;

        case Drag::listen:
            listenAt (e.x);
            return;

        case Drag::seek:
            seekAt (e.x, snapOn);
            return;

        case Drag::drumPaint:
            drumPaintAt (e.getPosition(), snapOn);
            return;

        case Drag::stretch:
        {
            if (dragOriginals.empty() || stretchEnd <= stretchAnchor)
                return;

            // the new end of the selection: on the grid (the nearest line), or free with Alt
            const auto minimum = snapOn ? juce::jmax<std::int64_t> (1, gridTicks()) : (std::int64_t) 10;
            auto end = snapOn ? snap (xToTick (e.x) + (double) gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            end = juce::jmax (stretchAnchor + minimum, end);
            const auto stretched = polish::stretchNotes (dragOriginals, stretchAnchor, stretchEnd, end);

            editNotes ([this, &stretched] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size() && j < stretched.size(); ++j)
                    if ((std::size_t) dragIndices[j] < p.notes.size())
                        p.notes[(std::size_t) dragIndices[j]] = stretched[j];
            });
            repaint();
            return;
        }

        case Drag::paint:
        {
            // lays notes to the right, one per default length, at the pitch under the mouse
            const auto tick = snap (xToTick (e.x), snapOn);
            for (int guard = 0; tick >= nextPaintTick && guard < 64; ++guard)
            {
                paintNoteAt (nextPaintTick, snapKey (yToKey (e.y)), noteedit::monophonicPaint (e.mods.isShiftDown(), monophonicPainting));
                nextPaintTick += paintLength;
            }

            if (snapKey (yToKey (e.y)) != previewKey)
                previewOn (snapKey (yToKey (e.y)), noteTemplate.velocity);
            return;
        }

        case Drag::move:
        {
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            auto deltaTicks = juce::jmax<std::int64_t> (0, snap (xToTick (e.x), snapOn) + grabOffset) - grabbed.start;
            auto deltaKeys = snapKey (yToKey (e.y)) - grabbed.key;
            auto rawKeys = yToKey (e.y) - grabbed.key; // (the mouse's own row, before any snap)

            // locks: Shift pressed after the click keeps the pitch, Ctrl keeps the time
            if (e.mods.isShiftDown() && ! downMods.isShiftDown())
                deltaKeys = rawKeys = 0;
            if (flmods::ctrl (e.mods) && ! flmods::ctrl (downMods))
                deltaTicks = 0;

            const auto& current = currentPattern (engine.getSong()).notes[(std::size_t) dragNote];
            // snap to scale (core/ScaleSnap.h, round 190an): the grabbed note follows the mouse onto the scale and the
            // whole group moves that many scale steps; on the note's own row the pitch is left alone, also off the scale
            const auto scale = snapScale();
            const auto onScale = scale.active() && ! folded();
            const auto scaleSteps = onScale ? scalesnap::dragSteps (scale, grabbed.key, grabbed.key + rawKeys) : 0;

            if (current.start == grabbed.start + deltaTicks
                && (onScale ? scaleSteps == lastScaleDragKeys : current.key == grabbed.key + deltaKeys))
                return;
            lastScaleDragKeys = scaleSteps;

            const auto oldKey = current.key;

            // from the original positions: the group keeps its shape even when hitting the limits
            // folded to the scale (round 166d): the notes move by rows (scale degrees), so they stay on shown keys
            const auto deltaRows = folded() ? rowFor (grabbed.key) - rowFor (grabbed.key + deltaKeys) : 0;
            editNotes ([this, &deltaTicks, &deltaKeys, scale, scaleSteps, onScale, deltaRows] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                    p.notes[(std::size_t) dragIndices[j]] = dragOriginals[j];

                if (folded())
                {
                    auto noKeys = 0;
                    noteedit::move (p, dragIndices, deltaTicks, noKeys);
                    if (deltaRows != 0)
                        for (std::size_t j = 0; j < dragIndices.size(); ++j)
                            p.notes[(std::size_t) dragIndices[j]].key = rows.step (dragOriginals[j].key, deltaRows);
                    return;
                }
                if (! onScale)
                {
                    noteedit::move (p, dragIndices, deltaTicks, deltaKeys);
                    return;
                }
                // snap to scale: the time moves as a group, the pitch by scale steps (a chord stays a chord of the scale)
                auto noKeys = 0;
                noteedit::move (p, dragIndices, deltaTicks, noKeys);
                scalesnap::moveBySteps (p, dragIndices, scaleSteps, scale);
            });

            const auto& moved = currentPattern (engine.getSong()).notes[(std::size_t) dragNote];

            if (moved.key != oldKey)
                previewOn (moved.key, moved.velocity);
            return;
        }

        case Drag::resize:
        {
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            const auto minimum = snapOn ? gridTicks() : (std::int64_t) 10;
            const auto end = snapOn ? snap (xToTick (e.x) + (double) gridTicks() / 2.0, true) : (std::int64_t) xToTick (e.x);
            const auto delta = juce::jmax (minimum, end - grabbed.start) - grabbed.length;

            if (currentPattern (engine.getSong()).notes[(std::size_t) dragNote].length == grabbed.length + delta)
                return;

            lastLength = grabbed.length + delta;
            editNotes ([this, delta, minimum] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                    p.notes[(std::size_t) dragIndices[j]].length = juce::jmax (minimum, dragOriginals[j].length + delta);
            });
            return;
        }

        case Drag::resizeLeft:
        {
            // the start moves, the end stays (selected notes together)
            const auto& grabbed = dragOriginals[(std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin())];
            const auto minimum = snapOn ? gridTicks() : (std::int64_t) 10;
            auto delta = juce::jmax<std::int64_t> (0, snap (xToTick (e.x) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn)) - grabbed.start;

            for (const auto& o : dragOriginals)
                delta = juce::jlimit (-o.start, o.length - juce::jmin (minimum, o.length), delta);

            if (currentPattern (engine.getSong()).notes[(std::size_t) dragNote].start == grabbed.start + delta)
                return;

            editNotes ([this, delta] (Pattern& p) {
                for (std::size_t j = 0; j < dragIndices.size(); ++j)
                {
                    auto& n = p.notes[(std::size_t) dragIndices[j]];
                    n.start = dragOriginals[j].start + delta;
                    n.length = dragOriginals[j].length - delta;
                }
            });
            lastLength = grabbed.length - delta;
            return;
        }
    }
}

void PianoRollComponent::postDragHint (const juce::MouseEvent& e)
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto count = juce::String ((int) std::max<std::size_t> (1, dragIndices.size()));
    auto steps = [] (std::int64_t ticks) {
        const auto value = (double) ticks / ticksPerStep;
        return (value > 0 ? "+" : "") + juce::String (value, std::abs (value - std::round (value)) < 1e-9 ? 0 : 2);
    };
    ui::Hint hint;

    switch (drag)
    {
        case Drag::move:
        {
            const auto at = (std::size_t) (std::find (dragIndices.begin(), dragIndices.end(), dragNote) - dragIndices.begin());
            if (dragNote < 0 || dragNote >= (int) notes.size() || at >= dragOriginals.size())
                return;
            const auto& now = notes[(std::size_t) dragNote];
            const auto& was = dragOriginals[at];
            hint.text = tr ("Moving {0} note(s): then [Shift] keeps the pitch  [Ctrl] keeps the time  [Alt] no snap", { count });
            hint.value = keyName (now.key) + "  " + juce::String (now.key - was.key >= 0 ? "+" : "") + juce::String (now.key - was.key) + " st  "
                         + steps (now.start - was.start) + " " + tr ("steps");
            break;
        }
        case Drag::resize:
        case Drag::resizeLeft:
            if (dragNote < 0 || dragNote >= (int) notes.size())
                return;
            hint.text = tr ("Resizing {0} note(s)  [Alt] no snap", { count });
            hint.value = juce::String ((double) notes[(std::size_t) dragNote].length / ticksPerStep, 2) + " " + tr ("steps");
            break;
        case Drag::eventDraw:
        case Drag::eventErase:
        {
            const auto lane = velocityArea();
            const auto value = juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - e.y) / (float) (lane.getHeight() - 8));
            hint.text = drag == Drag::eventErase ? tr ("Deleting events") : tr ("Drawing events: {0}", { laneBox.getText() });
            const auto target = eventLaneTarget();
            hint.value = target && target->kind == AutomationTargetKind::tempo ? juce::String (tempoFromValue (value), 3) + " BPM" // (issue #86: as FL's hint)
                                                                               : juce::String (juce::roundToInt (value * 100.0f)) + "%";
            break;
        }
        case Drag::velocity:
        case Drag::velocitySet:
        case Drag::velocityLine:
        {
            const auto lane = velocityArea();
            const auto value = drag == Drag::velocitySet ? laneSetValue
                                                         : juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - e.y) / (float) (lane.getHeight() - 8));
            hint.text = tr ("Setting {0}", { laneBox.getText() });
            hint.value = juce::String (juce::roundToInt (value * 100.0f)) + "%";
            break;
        }
        case Drag::select:
        case Drag::keySelect:
        case Drag::rulerSelect:
            hint.text = tr ("Selecting  [Shift] adds to the selection");
            hint.value = tr ("{0} selected", { juce::String ((int) selection.size()) });
            break;
        case Drag::erase: hint.text = tr ("Deleting what the mouse crosses"); break;
        case Drag::muting: hint.text = tr ("Muting / unmuting what the mouse crosses"); break;
        case Drag::knife: hint.text = tr ("Cutting along the line  [Alt] no snap"); break;
        case Drag::paint: hint.text = tr ("Painting notes  [Alt] no snap"); break;
        case Drag::drumPaint: hint.text = tr ("Drum brush: one hit per grid cell, on the key under the mouse  [Alt] no snap"); break;
        case Drag::stretch:
        {
            std::int64_t from = 0, to = 0;
            std::vector<Note> now;
            for (auto i : dragIndices)
                if (i >= 0 && i < (int) notes.size())
                    now.push_back (notes[(std::size_t) i]);
            noteedit::span (now, from, to);
            hint.text = tr ("Stretching {0} note(s) in time  [Alt] no snap", { count });
            hint.value = "x" + juce::String ((double) (to - stretchAnchor) / (double) juce::jmax<std::int64_t> (1, stretchEnd - stretchAnchor), 2);
            break;
        }
        case Drag::keyZoom: hint.text = tr ("Key height"); hint.value = tr ("Zoom level {0}", { juce::String (gesture::zoomLevel (rowHeight)) }) + "  " + juce::String (rowHeight) + " px"; break;
        case Drag::timeZoom: hint.text = tr ("Zooming: drag up = closer, sideways = scroll  [Ctrl] zoom only"); break;
        case Drag::keyboard:
            hint.text = tr ("Hearing the key: further right on a key = louder");
            hint.value = keyName (previewKey) + "  " + tr ("vel {0}", { juce::String (juce::roundToInt (keyVelocityAt (e.x, previewKey) * 127.0f)) });
            break;
        case Drag::pan:
        case Drag::seek: hint.text = tr ("Position  [Alt] no snap"); break;
        case Drag::listen:
        case Drag::zoomRange:
        case Drag::none: return;
    }

    ui::postHint (hint);
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& e)
{
    ui::clearPostedHint();
    if (drag == Drag::knife)
        cutWithKnife (knifeFrom, knifeTo, ! e.mods.isAltDown());

    if (drag == Drag::zoomRange)
    {
        if (rubberBand.getWidth() > 4)
            zoomToTicks (xToTick (rubberBand.getX()), xToTick (rubberBand.getRight()));
        else
            zoomIn (e.x); // a click zooms in around the point (right-click zooms out)
    }

    if (drag == Drag::select && rubberBand.isEmpty() && dragNote >= 0)
    {
        // Lasso click on a note: selects it (Shift: adds / removes)
        auto s = selectionBefore;
        if (auto it = std::find (s.begin(), s.end(), dragNote); it != s.end())
            s.erase (it);
        else
            s.push_back (dragNote);
        setSelection (s);
    }

    if (drag == Drag::listen)
        stopListening();

    if (drag == Drag::move || drag == Drag::resize || drag == Drag::resizeLeft || drag == Drag::paint || drag == Drag::drumPaint
        || drag == Drag::stretch)
        sortNotes();

    previewOff();
    drag = Drag::none;
    dragNote = -1;
    dragIndices.clear();
    dragOriginals.clear();
    touchedNotes.clear();
    laneOriginals.clear();
    rubberBand = {};
    hover = {};
    updateHover (e.getPosition(), e.mods); // the cursor of what is under the mouse now
    repaint();
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    updateHover (e.getPosition(), e.mods);
}

void PianoRollComponent::mouseExit (const juce::MouseEvent&)
{
    lastMouse = { -1, -1 };

    if (! (hover == Hover {}))
    {
        hover = {};
        repaint();
    }
}

void PianoRollComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
{
    // Ctrl / Shift / Alt change what a click would do: the cursor follows without moving the mouse
    if (drag == Drag::none && lastMouse.x >= 0)
        updateCursor (mods);
}

PianoRollComponent::Hover PianoRollComponent::hoverAt (juce::Point<int> p) const
{
    Hover h;

    if (p.x < keyboardWidth && p.y >= headerHeight && p.y < gridArea().getBottom())
    {
        h.key = keyboardKeyAt (p.x, p.y);
        return h;
    }

    if (velocityArea().contains (p))
    {
        // the stem nearest to the mouse (within a few pixels)
        const auto& notes = currentPattern (engine.getSong()).notes;
        auto best = 5.0;
        for (auto i : channelNotes())
            if (const auto d = std::abs (tickToX ((double) notes[(std::size_t) i].start) - p.x); d < best)
            {
                best = d;
                h.note = i;
            }
        return h;
    }

    if (! gridArea().contains (p))
        return h;

    h.key = yToKey (p.y);

    if (stretchHandle().expanded (2.0f).contains (p.toFloat()))
    {
        h.stretch = true;
        return h;
    }

    const auto& notes = currentPattern (engine.getSong()).notes;
    gesture::EdgeRules rules;
    rules.allowLeft = resizeFromLeft;

    // back to front: the note drawn on top wins
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& n = notes[(std::size_t) i];

        if (! edits (n) || n.key != h.key)
            continue;

        if (const auto zone = gesture::zoneAt (p.x, tickToX ((double) n.start), tickToX ((double) (n.start + n.length)), rules);
            zone != gesture::Zone::outside)
        {
            h.note = i;
            h.zone = zone;
            return h;
        }
    }

    h.ghost = ghostAt (p.x, p.y);
    return h;
}

gesture::Target PianoRollComponent::targetAt (const Hover& h) const
{
    return h.note >= 0 ? gesture::targetFor (h.zone) : gesture::Target::empty;
}

void PianoRollComponent::updateHover (juce::Point<int> p, juce::ModifierKeys mods)
{
    lastMouse = p;
    const auto h = hoverAt (p);

    if (! (h == hover))
    {
        // repaint only what changed: the old and new note rows, the keyboard, the lane
        auto dirty = [this] (const Hover& o) {
            if (o.key >= 0)
                repaint (0, keyToY (o.key) - 1, getWidth(), rowHeight + 2);
            const auto& notes = currentPattern (engine.getSong()).notes;
            for (auto i : { o.note, o.ghost })
                if (i >= 0 && i < (int) notes.size())
                {
                    repaint (0, keyToY (notes[(std::size_t) i].key) - 1, getWidth(), rowHeight + 2);
                    repaint (velocityArea());
                }
        };
        dirty (hover);
        hover = h;
        dirty (hover);
    }

    if (drag == Drag::none)
        updateCursor (mods);
}

void PianoRollComponent::updateCursor (juce::ModifierKeys mods)
{
    const auto p = lastMouse;
    auto cursor = gesture::Cursor::normal;

    if (p.x < keyboardWidth && p.y >= headerHeight && p.y < gridArea().getBottom())
        cursor = flmods::ctrl (mods) ? gesture::Cursor::select : gesture::Cursor::point;
    else if (velocityArea().contains (p))
        cursor = gesture::Cursor::draw;
    else if (gridArea().contains (p) && hover.stretch && ! flmods::ctrl (mods))
        cursor = gesture::Cursor::resizeH;
    else if (gridArea().contains (p) && editGhosts && hover.note < 0 && hover.ghost >= 0 && ! flmods::ctrl (mods))
        cursor = gesture::Cursor::move;
    else if (gridArea().contains (p))
    {
        const gesture::Mods m { mods.isShiftDown(), flmods::ctrl (mods), mods.isAltDown() };
        const auto tool = tools.getTool();
        cursor = gesture::cursorFor (gesture::leftAction (tool, targetAt (hover), m, false), tool);
    }
    else if (p.y >= rulerTop && p.y < headerHeight && p.x >= keyboardWidth && flmods::ctrl (mods))
        cursor = gesture::Cursor::select;
    else if (zoomWheelArea().contains (p))
        cursor = gesture::Cursor::resizeV; // (FL: crSizeNS)

    setMouseCursor (editorCursor (cursor));
}

ui::Hint PianoRollComponent::hintAt (juce::Point<int> p)
{
    ui::Hint hint;
    const auto mods = juce::ModifierKeys::currentModifiers;
    const auto& song = engine.getSong();
    const auto& notes = currentPattern (song).notes;
    if (zoomWheelArea().contains (p))
    {
        hint.text = tr ("Key height: drag up / down or turn the wheel  [Ctrl] fine");
        hint.value = tr ("Zoom level {0}", { juce::String (gesture::zoomLevel (rowHeight)) });
        return hint;
    }
    const auto h = hoverAt (p);
    auto noteValue = [&] (int i) {
        const auto& n = notes[(std::size_t) i];
        return keyName (n.key) + "  " + juce::String (juce::roundToInt (n.velocity * 100.0f)) + "%";
    };

    if (p.x < keyboardWidth && p.y >= headerHeight && p.y < gridArea().getBottom())
    {
        hint.text = tr ("Keyboard: [LMB] hear the key (further right = louder)  [Ctrl][LMB] select its notes  [MMB] drag: taller / shorter keys");
        hint.value = keyName (h.key) + "  " + tr ("vel {0}", { juce::String (juce::roundToInt (keyVelocityAt (p.x, h.key) * 127.0f)) });
        return hint;
    }

    if (velocityArea().contains (p))
    {
        hint.text = tr ("{0} lane: [LMB] drag draws  [RMB] drag draws a line  [Shift][LMB] one value  [Alt][LMB] reset  [Alt][Wheel] over a note: adjust",
                        { laneBox.getText() });
        if (h.note >= 0)
            hint.value = noteValue (h.note);
        return hint;
    }

    if (p.y >= rulerTop && p.y < headerHeight && p.x >= keyboardWidth)
    {
        hint.text = tr ("Ruler: [LMB] position  [Alt] no snap  [Ctrl][LMB], [RMB] or [2xLMB] drag selects the notes in a time range  [Ctrl][Wheel] or a pinch: zoom  [Shift][Wheel] scroll");
        return hint;
    }

    if (! gridArea().contains (p))
        return hint;

    if (h.stretch)
    {
        hint.text = tr ("Stretch handle: [LMB] drag stretches or squeezes the selected notes in time (from the selection's start)  [Alt] no snap");
        hint.value = tr ("{0} selected", { juce::String ((int) selection.size()) });
        return hint;
    }

    if (h.note < 0 && h.ghost >= 0)
    {
        const auto ghostChannel = notes[(std::size_t) h.ghost].channel;
        const auto name = ghostChannel >= 0 && ghostChannel < (int) song.channels.size()
                              ? juce::String::fromUTF8 (song.channels[(std::size_t) ghostChannel].name.c_str())
                              : juce::String();
        hint.text = editGhosts ? tr ("Ghost note of {0} (editable): [LMB] edit it (switches to {0})  [RMB] deletes it  [Ctrl][Alt][V] read-only ghosts",
                                     { name })
                               : tr ("Ghost note of {0}: double [RMB] edits that channel  [Alt][V] hides ghost notes  [Ctrl][Alt][V] makes them editable",
                                     { name });
        hint.value = keyName (notes[(std::size_t) h.ghost].key);
        return hint;
    }

    const gesture::Mods m { mods.isShiftDown(), flmods::ctrl (mods), mods.isAltDown() };
    hint.text = actionHint (gesture::leftAction (tools.getTool(), targetAt (h), m, false), false);
    hint.value = h.note >= 0 ? noteValue (h.note) : keyName (h.key);
    return hint;
}

void PianoRollComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! gridArea().contains (e.getPosition()) || e.mods.isRightButtonDown() || flmods::ctrl (e.mods))
        return;

    // the first click of this double click created the note: that was drawing, not opening it
    if (juce::Time::getMillisecondCounter() - createdAt < 800)
        return;

    bool edge = false;
    const auto hit = noteAt (e.x, e.y, edge);

    if (hit < 0)
        return;

    const auto indices = isSelected (hit) ? selection : std::vector<int> { hit };
    const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) hit];
    const auto area = juce::Rectangle<int> ((int) tickToX ((double) n.start), keyToY (n.key), juce::jmax (8, (int) (n.length * pixelsPerTick)), rowHeight);
    showNoteInspector (engine, indices, hit, localAreaToGlobal (area));
}

std::unique_ptr<juce::Component> PianoRollComponent::makeNoteInspectorForSnapshot (bool singleNote)
{
    if (selection.empty())
        selectAll();

    if (selection.empty())
        return nullptr;

    return makeNoteInspector (engine, singleNote ? std::vector<int> { selection.front() } : selection, selection.front());
}

ui::Hint PianoRollComponent::hoverForSnapshot()
{
    rowHeight = 18;
    pixelsPerTick = 64.0 / ticksPerStep;
    scrollToNotes (false);
    const auto notes = channelNotes();

    if (notes.empty())
        return {};

    const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) notes.front()];
    const auto at = juce::Point<int> ((int) tickToX ((double) (n.start + n.length)) - 2, keyToY (n.key) + rowHeight / 2);
    updateHover (at, {});
    return hintAt (at);
}

// --- tools ---------------------------------------------------------------------------

void PianoRollComponent::cutWithKnife (juce::Point<int> from, juce::Point<int> to, bool snapOn)
{
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<std::pair<int, std::int64_t>> cuts;

    if (from.getDistanceFrom (to) < 3)
    {
        // a click: cuts the note under the cursor
        bool edge = false;
        if (const auto hit = noteAt (from.x, from.y, edge); hit >= 0)
            cuts.push_back ({ hit, snap (xToTick (from.x) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn) });
    }
    else
    {
        // a line: every note row it crosses is cut where the line passes
        const auto top = juce::jmin (from.y, to.y), bottom = juce::jmax (from.y, to.y);

        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(std::size_t) i];

            if (! edits (n))
                continue;

            const auto rowY = keyToY (n.key) + rowHeight / 2;

            if (rowY < top - rowHeight / 2 || rowY > bottom + rowHeight / 2)
                continue;

            const auto y = juce::jlimit (top, bottom, rowY);
            const auto x = from.y == to.y ? (double) from.x : from.x + (double) (y - from.y) * (to.x - from.x) / (double) (to.y - from.y);
            const auto tick = snap (xToTick ((int) std::round (x)) + (snapOn ? (double) gridTicks() / 2.0 : 0.0), snapOn);

            if (tick > n.start && tick < n.start + n.length)
                cuts.push_back ({ i, tick });
        }
    }

    if (cuts.empty())
        return;

    setSelection ({});
    editNotes ([&cuts] (Pattern& p) {
        for (auto [index, tick] : cuts)
            noteedit::split (p, index, tick);
    });
    sortNotes();
}

void PianoRollComponent::zoomToTicks (double from, double to)
{
    if (to - from < 1.0)
        return;

    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, gridArea().getWidth() / (to - from));
    scrollTicks = juce::jmax (0.0, from);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomIn (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - keyboardWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomOut (int anchorX)
{
    const auto anchor = xToTick (anchorX);
    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick / 2.0);
    scrollTicks = juce::jmax (0.0, anchor - (anchorX - keyboardWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::seekAt (int x, bool snapOn)
{
    const auto& song = engine.getSong();
    const auto tick = snapOn ? (double) snap (xToTick (x), true) : xToTick (x);
    const auto target = transportedit::patternSeekTarget (song, song.currentPattern, tick, engine.getTransport().positionTicks.load());
    if (target < 0.0)
    {
        // Song mode and no clip of this pattern: play the pattern on its own from there
        engine.editSong ([] (Song& s) { s.mode = PlayMode::pattern; }, false);
        engine.seek (juce::jmax (0.0, tick));
        return;
    }
    engine.seek (target);
}

void PianoRollComponent::listenAt (int x)
{
    // plays the channel's notes that sound at the tick under the mouse
    const auto tick = (std::int64_t) xToTick (x);
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto channel = engine.getSelectedChannel();
    std::vector<int> keys;
    std::vector<float> velocities;

    for (const auto& n : notes)
        if (n.channel == channel && ! n.muted && n.start <= tick && n.start + n.length > tick
            && std::find (keys.begin(), keys.end(), n.key) == keys.end())
        {
            keys.push_back (n.key);
            velocities.push_back (n.velocity);
        }

    for (auto k : listenKeys)
        if (std::find (keys.begin(), keys.end(), k) == keys.end())
            listenNotes.release (listenChannel, k);

    if (channel != listenChannel)
        listenNotes.releaseAll();

    for (std::size_t i = 0; i < keys.size(); ++i)
        if (! listenNotes.isHeld (channel, keys[i]))
            listenNotes.press (channel, keys[i], velocities[i]);

    listenKeys = keys;
    listenChannel = channel;
}

void PianoRollComponent::stopListening()
{
    listenNotes.releaseAll();
    listenKeys.clear();
}

void PianoRollComponent::refreshChannelBox()
{
    const auto& song = engine.getSong();
    std::vector<bool> hasNotes (song.channels.size(), false);

    for (const auto& n : currentPattern (song).notes)
        if (n.channel >= 0 && n.channel < (int) hasNotes.size())
            hasNotes[(std::size_t) n.channel] = true;

    juce::StringArray names;
    names.add (tr ("All")); // (issue #12: every channel's notes; drawn bold)
    for (std::size_t c = 0; c < song.channels.size(); ++c)
        names.add ((hasNotes[c] ? juce::String::fromUTF8 ("\xe2\x97\x8f ") : juce::String ("    ")) + juce::String::fromUTF8 (song.channels[c].name.c_str()));

    // only rebuilds when something changed (rebuilding closes an open menu)
    bool same = channelBox.getNumItems() == names.size();
    for (int i = 0; same && i < names.size(); ++i)
        same = channelBox.getItemText (i) == names[i];

    if (! same)
    {
        // the bold "All" at the top, as a custom item (a ComboBox item has no font of its own)
        struct BoldItem final : juce::PopupMenu::CustomComponent
        {
            explicit BoldItem (juce::String t) : text (std::move (t)) {}
            void getIdealSize (int& w, int& h) override
            {
                w = 160;
                h = 24;
            }
            void paint (juce::Graphics& g) override
            {
                if (isItemHighlighted())
                {
                    g.setColour (theme::colour::accent.withAlpha (0.35f));
                    g.fillRect (getLocalBounds());
                }
                g.setColour (theme::colour::textPrimary);
                g.setFont (theme::font (theme::type::body, true));
                g.drawText (text, getLocalBounds().withTrimmedLeft (12), juce::Justification::centredLeft, true);
            }
            juce::String text;
        };
        channelBox.clear (juce::dontSendNotification);
        juce::PopupMenu::Item all (names[0]);
        all.itemID = allChannelsId;
        all.customComponent = new BoldItem (names[0]);
        channelBox.getRootMenu()->addItem (std::move (all));
        channelBox.getRootMenu()->addSeparator();
        for (int i = 1; i < names.size(); ++i)
            channelBox.addItem (names[i], i);
    }

    channelBox.setSelectedId (allChannels ? allChannelsId : engine.getSelectedChannel() + 1, juce::dontSendNotification);
}

void PianoRollComponent::setAllChannels (bool on)
{
    if (allChannels == on)
        return;
    allChannels = on;
    selection.clear();
    stopListening();
    refreshChannelBox();
    repaint();
}

void PianoRollComponent::stepChannel (int direction, bool withNotesOnly)
{
    const auto& song = engine.getSong();
    const auto count = (int) song.channels.size();

    if (count == 0)
        return;

    std::vector<bool> hasNotes ((std::size_t) count, false);
    for (const auto& n : currentPattern (song).notes)
        if (n.channel >= 0 && n.channel < count)
            hasNotes[(std::size_t) n.channel] = true;

    auto channel = engine.getSelectedChannel();

    for (int step = 0; step < count; ++step)
    {
        channel += direction;

        if (channel < 0 || channel >= count)
            return; // stops at the ends

        if (! withNotesOnly || hasNotes[(std::size_t) channel])
        {
            engine.setSelectedChannel (channel);
            return;
        }
    }
}

void PianoRollComponent::runOperation (const std::function<std::vector<int> (Pattern&, const std::vector<int>&)>& op)
{
    const auto target = selectionOrAll();

    if (target.empty())
        return;

    const auto hadSelection = ! selection.empty();
    std::vector<int> result;
    editNotes ([&] (Pattern& p) { result = op (p, target); });
    setSelection (hadSelection ? result : std::vector<int> {});
    sortNotes();
}

void PianoRollComponent::showMoreMenu()
{
    const auto grid = gridTicks();
    const auto bar = engine.getSong().barTicks();
    juce::PopupMenu menu;
    menu.addSubMenu (theme::mnemonic (tr ("File"), 'F'), fileMenu());
    menu.addSubMenu (theme::mnemonic (tr ("Edit"), 'E'), editMenu());
    menu.addSubMenu (theme::mnemonic (tr ("View"), 'V'), viewMenu());
    menu.addSubMenu (tr ("Time markers"), markersMenu());
    menu.addSeparator();
    menu.addSectionHeader (tr ("Note operations (selection, or all notes)"));
    menu.addItem (tr ("Chop to grid") + "\tCtrl+U", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { return noteedit::chop (p, t, grid); }); });
    menu.addItem (tr ("Join touching notes") + "\tCtrl+G", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::join (p, t); return std::vector<int> {}; }); });
    menu.addItem (tr ("Make legato") + "\tCtrl+L", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::legato (p, t); return t; }); });
    menu.addItem (tr ("Shrink to grid") + "\tShift+D", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::shrinkToGrid (p, t, grid); return t; }); });
    menu.addItem (tr ("Quantise starts") + keyText ("pianoRoll.quantizeStarts"), [this] { quantizeOperation (0); });
    menu.addItem (tr ("Quantise starts 50% (halfway to the grid)") + keyText ("pianoRoll.quantizeHalf"), [this] { quantizeOperation (2); });
    menu.addItem (tr ("Quantise starts and lengths") + keyText ("pianoRoll.quickQuantize"), [this] { quantizeOperation (1); });
    menu.addItem (tr ("Rotate left within the bar") + "\tCtrl+Shift+Left", [this, grid, bar] { runOperation ([grid, bar] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, -1); return t; }); });
    menu.addItem (tr ("Rotate right within the bar") + "\tCtrl+Shift+Right", [this, grid, bar] { runOperation ([grid, bar] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, 1); return t; }); });
    menu.addSeparator();
    menu.addSubMenu (theme::mnemonic (tr ("Tools"), 'T'), toolsMenu());
    menu.addSubMenu (theme::mnemonic (tr ("Select"), 'S'), selectMenu());
    {
        juce::PopupMenu zoom;
        zoom.addItem (tr ("Zoom in") + "\tPageUp", [this] { runAction ("pianoRoll.zoomIn"); });
        zoom.addItem (tr ("Zoom out") + "\tPageDown", [this] { runAction ("pianoRoll.zoomOut"); });
        zoom.addItem (tr ("Zoom: one bar") + "\tShift+1", [this] { runAction ("pianoRoll.zoom1"); });
        zoom.addItem (tr ("Zoom: four bars") + "\tShift+2", [this] { runAction ("pianoRoll.zoom2"); });
        zoom.addItem (tr ("Zoom: sixteen bars") + "\tShift+3", [this] { runAction ("pianoRoll.zoom3"); });
        zoom.addItem (tr ("Zoom to show everything") + "\tShift+4", [this] { runAction ("pianoRoll.zoomAll"); });
        zoom.addItem (tr ("Zoom to the selection") + "\tShift+5", [this] { runAction ("pianoRoll.zoomSelection"); });
        zoom.addItem (tr ("Centre the view on the playhead") + "\tShift+0", [this] { runAction ("pianoRoll.centrePlayhead"); });
        zoom.addSeparator();
        zoom.addItem (tr ("Taller keys") + "\tCtrl+Alt+Wheel", [this] { rowHeight = juce::jmin (gesture::maxKeyHeight, rowHeight + 2); updateScrollBars(); repaint(); });
        zoom.addItem (tr ("Shorter keys"), [this] { rowHeight = juce::jmax (gesture::minKeyHeight, rowHeight - 2); updateScrollBars(); repaint(); });
        menu.addSubMenu (tr ("Zoom"), zoom);
    }
    menu.addSeparator();
    menu.addItem (tr ("Snap notes to the scale") + keyText ("pianoRoll.scaleSnap"), scaleIndex() > 0, scaleSnap, [this] { setScaleSnap (! scaleSnap); });
    menu.addItem (tr ("Fold to the scale (hide the other keys and their notes)"), scaleIndex() > 0, foldToScale,
                  [this] { setFoldToScale (! foldToScale); });
    menu.addItem (tr ("Chord stamp: the clicked key is the top note"), true, chordTopDown, [this] { chordTopDown = ! chordTopDown; });
    // the chord stamp's list (round 190j, discussion #10): the toolbar's chord box is hidden in a narrow window, so
    // the stamp is always in this menu too
    menu.addSubMenu (tr ("Chord stamp"), stampMenu(), true, juce::Image(), chordBox.getSelectedId() > 1);
    menu.addItem (tr ("Stamp: none (single notes)") + keyText ("pianoRoll.stampNone"), chordBox.getSelectedId() != 1, false,
                  [this] { runAction ("pianoRoll.stampNone"); });
    menu.addItem (tr ("Move the notes onto the scale"), scaleIndex() > 0, false, [this] {
        const auto root = scaleRoot(), scale = scaleIndex();
        runOperation ([root, scale] (Pattern& p, const std::vector<int>& t) { notetools::snapNotesToScale (p, t, root, scale); return t; });
    });
    menu.addSeparator();
    menu.addItem (tr ("Note names on every key"), true, notenames::everyKey.load(), [this] {
        notenames::everyKey.store (! notenames::everyKey.load());
        if (onNoteNamesChanged != nullptr)
            onNoteNamesChanged(); // (saved with the settings)
        repaint();
    });
    menu.addItem (tr ("Resize notes from the left edge too") + "\tCtrl+Alt+Home", true, resizeFromLeft, [this] { resizeFromLeft = ! resizeFromLeft; });
    menu.addItem (tr ("Show ghost notes") + "\tAlt+V", true, showGhosts,
                  [this] { ghostButton.setToggleState (! ghostButton.getToggleState(), juce::sendNotificationSync); });
    menu.addItem (tr ("Ghost notes are editable") + "\tCtrl+Alt+V", true, editGhosts, [this] {
        editGhosts = ! editGhosts;
        if (editGhosts && ! showGhosts)
            ghostButton.setToggleState (true, juce::sendNotificationSync);
        repaint();
    });
    menu.addSubMenu (tr ("Ghost channels"), ghostChannelsMenu());
    menu.addSeparator();
    menu.addSubMenu ("MIDI", midiMenu());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton));
}

bool PianoRollComponent::ghostShown (int channel) const noexcept
{
    const auto& channels = engine.getSong().channels;
    return channel < 0 || channel >= (int) channels.size() || ! channels[(std::size_t) channel].ghostHidden;
}

void PianoRollComponent::setGhostChannels (const std::function<bool (int channel, bool shown)>& show)
{
    engine.editSong ([&show] (Song& s) {
        for (int c = 0; c < (int) s.channels.size(); ++c)
            s.channels[(std::size_t) c].ghostHidden = ! show (c, ! s.channels[(std::size_t) c].ghostHidden);
    }, false);
    engine.markModified();
    if (! showGhosts) // choosing what to ghost shows the ghosts
        ghostButton.setToggleState (true, juce::sendNotificationSync);
    repaint();
}

juce::PopupMenu PianoRollComponent::ghostChannelsMenu()
{
    // FL 2026's ghost notes button menu (round 166d [SHOT], right-click on it): "Ghost channels", All, None, then every
    // channel with a check mark (the edited one in bold); a click toggles that channel's ghost notes
    struct ChannelItem final : juce::PopupMenu::CustomComponent
    {
        ChannelItem (juce::String t, bool tick, bool bold) : text (std::move (t)), ticked (tick), strong (bold) {}
        void getIdealSize (int& w, int& h) override
        {
            w = juce::jlimit (140, 420, 40 + juce::GlyphArrangement::getStringWidthInt (theme::font (theme::type::body, true), text));
            h = 22;
        }
        void paint (juce::Graphics& g) override
        {
            if (isItemHighlighted())
            {
                g.setColour (theme::colour::accent.withAlpha (0.35f));
                g.fillRect (getLocalBounds());
            }
            auto area = getLocalBounds();
            const auto tickArea = area.removeFromLeft (24).toFloat();
            if (ticked)
                icons::draw (g, Icon::check, tickArea.withSizeKeepingCentre (12.0f, 12.0f), theme::colour::textPrimary);
            g.setColour (ticked ? theme::colour::textPrimary : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::body, strong));
            g.drawText (text, area.withTrimmedRight (8), juce::Justification::centredLeft, true);
        }
        juce::String text;
        bool ticked, strong;
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Ghost channels"));
    menu.addItem (tr ("All"), [this] { setGhostChannels ([] (int, bool) { return true; }); });
    menu.addItem (tr ("None"), [this] { setGhostChannels ([] (int, bool) { return false; }); });
    menu.addSeparator();
    const auto& song = engine.getSong();
    const auto current = engine.getSelectedChannel();
    for (int c = 0; c < (int) song.channels.size(); ++c)
    {
        const auto name = juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str());
        juce::PopupMenu::Item item (name);
        item.customComponent = new ChannelItem (name, ghostShown (c), c == current);
        item.action = [this, c] { setGhostChannels ([c] (int k, bool shown) { return k == c ? ! shown : shown; }); };
        menu.addItem (std::move (item));
    }
    return menu;
}

void PianoRollComponent::openMidiTools()
{
    const juce::Component::SafePointer<PianoRollComponent> safe (this);
    auto panel = std::make_unique<MidiToolsPanel> (
        engine,
        [safe] {
            MidiToolsPanel::Target t;
            if (safe != nullptr)
            {
                t.pattern = safe->engine.getSong().currentPattern;
                t.indices = safe->midiToolNotes (t.selected);
                t.grid = safe->midiToolGrid();
                t.from = safe->timeSelectionFrom(); // round 190fm: Generate writes into the time selection
                t.to = safe->timeSelectionTo();
                t.channel = safe->engine.getSelectedChannel();
            }
            return t;
        },
        [safe] (const std::vector<int>& indices) {
            if (safe != nullptr)
                safe->selectMidiToolResult (indices);
        });
    struct Padded final : juce::Component // (the panel with a margin, on the window's background)
    {
        explicit Padded (std::unique_ptr<MidiToolsPanel> p) : panel (std::move (p))
        {
            addAndMakeVisible (*panel);
            setSize (MidiToolsPanel::panelWidth + 16, MidiToolsPanel::panelHeight + 12);
        }
        void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }
        void resized() override { panel->setBounds (getLocalBounds().reduced (8, 6)); }
        std::unique_ptr<MidiToolsPanel> panel;
    };
    openToolWindow (std::make_unique<Padded> (std::move (panel)), tr ("MIDI Tools"), "midiTools");
}

juce::PopupMenu PianoRollComponent::toolsMenu()
{
    // FL's piano roll Tools menu (REA round 143, EXEC capture): its groups and keys, in its order
    juce::PopupMenu menu;
    const auto grid = gridTicks();
    auto tool = [this, &menu] (NoteTool t, const char* key) {
        menu.addItem (tr (noteToolTitle (t)) + "..." + (key != nullptr ? juce::String ("\t") + key : juce::String()), [this, t] { openNoteTool (t); });
    };
    menu.addSectionHeader (tr ("Built-in"));
    tool (NoteTool::riffMachine, "Alt+E");
    menu.addItem (tr ("Generate chord progression...") + "\tAlt+P", [this] { openNoteTool (NoteTool::chordProgression); });
    menu.addSeparator();
    menu.addItem (tr ("Quick legato") + "\tCtrl+L", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::legato (p, t); return t; }); });
    tool (NoteTool::articulate, "Alt+L");
    menu.addSeparator();
    menu.addItem (tr ("Quick quantize") + keyText ("pianoRoll.quickQuantize"), [this] { quantizeOperation (1); });
    menu.addItem (tr ("Quick quantize start times") + keyText ("pianoRoll.quantizeStarts"), [this] { quantizeOperation (0); });
    menu.addItem (tr ("Quick quantize 50% (halfway to the grid)") + keyText ("pianoRoll.quantizeHalf"), [this] { quantizeOperation (2); });
    tool (NoteTool::quantize, "Alt+Q");
    menu.addSeparator();
    menu.addItem (tr ("Quick chop") + "\tCtrl+U", [this, grid] { runOperation ([grid] (Pattern& p, const std::vector<int>& t) { return noteedit::chop (p, t, grid); }); });
    tool (NoteTool::chop, "Alt+U");
    menu.addSeparator();
    menu.addItem (tr ("Glue notes") + "\tCtrl+G", [this] { runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::join (p, t); return std::vector<int> {}; }); });
    menu.addSeparator();
    tool (NoteTool::arpeggiate, "Alt+A");
    tool (NoteTool::strum, "Alt+S");
    tool (NoteTool::flam, "Alt+F");
    tool (NoteTool::claw, "Alt+W");
    tool (NoteTool::limit, "Alt+K");
    tool (NoteTool::flip, "Alt+Y");
    tool (NoteTool::randomise, "Alt+R");
    menu.addSeparator();
    tool (NoteTool::scaleLevels, "Alt+X");
    tool (NoteTool::lfo, "Alt+O");

    // FL's second column ("Scripts": Python tools shipped with FL); LYNX has its own built-in versions
    menu.addSectionHeader (tr ("More tools"));
    tool (NoteTool::euclidean, nullptr);
    tool (NoteTool::humanize, nullptr);
    tool (NoteTool::noteRepeat, nullptr);
    // Live 12's MIDI Transformations as a docked-style panel (round 190s; LYNX addition beside FL's tools above)
    menu.addItem (tr ("MIDI Tools") + "...", [this] { openMidiTools(); });
    {
        juce::PopupMenu velocity;
        auto choose = [this] (std::function<std::vector<int> (const Pattern&, const std::vector<int>&)> pick) {
            const auto& song = engine.getSong();
            setSelection (pick (song.activePattern(), channelNotes()));
        };
        velocity.addItem (tr ("Loudest quarter"), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectLoudest (p, t, 0.25f, true); }); });
        velocity.addItem (tr ("Quietest quarter"), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectLoudest (p, t, 0.25f, false); }); });
        velocity.addSeparator();
        for (const auto& [label, lo, hi] : { std::tuple { "75-100%", 0.75f, 1.0f }, { "50-75%", 0.5f, 0.75f }, { "25-50%", 0.25f, 0.5f }, { "0-25%", 0.0f, 0.25f } })
            velocity.addItem (tr ("Velocity {0}", { juce::String (label) }), [choose, lo = lo, hi = hi] {
                choose ([lo, hi] (const Pattern& p, const std::vector<int>& t) { return notetools::selectByVelocity (p, t, lo, hi); });
            });
        if (! selection.empty())
        {
            const auto& notes = engine.getSong().activePattern().notes;
            const auto v = selection.front() < (int) notes.size() ? notes[(std::size_t) selection.front()].velocity : 0.0f;
            velocity.addSeparator();
            velocity.addItem (tr ("Same velocity as the selected note ({0}%)", { juce::String (juce::roundToInt (v * 100.0f)) }), [choose, v] {
                choose ([v] (const Pattern& p, const std::vector<int>& t) { return notetools::selectByVelocity (p, t, v - 0.005f, v + 0.005f); });
            });
        }
        menu.addSubMenu (tr ("Select by velocity"), velocity);
    }
    {
        // Live 12's note Chance (round 187c, LYNX additions): probability groups, a reset and a new draw for the project
        juce::PopupMenu chance;
        const auto several = selection.size() > 1;
        chance.addItem (tr ("Group the selected notes: play all"), several, false, [this] {
            runOperation ([] (Pattern& p, const std::vector<int>& t) { notetools::groupChance (p, t, false); return t; });
        });
        chance.addItem (tr ("Group the selected notes: play one"), several, false, [this] {
            runOperation ([] (Pattern& p, const std::vector<int>& t) { notetools::groupChance (p, t, true); return t; });
        });
        chance.addItem (tr ("Ungroup the chance of the selected notes"), [this] {
            runOperation ([] (Pattern& p, const std::vector<int>& t) { notetools::ungroupChance (p, t); return t; });
        });
        chance.addSeparator();
        chance.addItem (tr ("Reset chance and velocity range"), [this] {
            runOperation ([] (Pattern& p, const std::vector<int>& t) {
                notetools::ungroupChance (p, t);
                for (auto i : t)
                    if (i >= 0 && i < (int) p.notes.size())
                        p.notes[(std::size_t) i].chance = 1.0f, p.notes[(std::size_t) i].velocityRange = 0.0f;
                return t;
            });
        });
        chance.addItem (tr ("New chance seed (draws again for the whole project)"), [this] {
            const auto seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt64();
            engine.editSong ([seed] (Song& s) { s.chanceSeed = seed == 0 ? 1u : seed; });
            repaint();
        });
        chance.addSeparator();
        chance.addItem (tr ("Show the chance lane"), true, laneIsChance(), [this] {
            laneBox.setSelectedId (laneChance);
        });
        menu.addSubMenu (tr ("Chance (LYNX)"), chance);
    }
    menu.addSeparator();
    menu.addItem (tr ("Edit events..."), [this] {
        openEventEditor (engine, { AutomationTargetKind::channelVolume, engine.getSelectedChannel() });
    });

    // FL's second column: "Scripts", the Piano roll's Python scripts (round 190as)
    menu.addColumnBreak();
    menu.addSectionHeader (tr ("Scripts"));
    addScriptItems (menu);

    return menu;
}

void PianoRollComponent::openNoteTool (NoteTool tool)
{
    const auto target = selectionOrAll();

    if (target.empty() && ! noteToolGenerates (tool))
        return;

    showNoteTool (engine, tool, target, gridTicks(), [safe = juce::Component::SafePointer<PianoRollComponent> (this)] {
        if (safe != nullptr)
            safe->setSelection ({});
    });
}

void PianoRollComponent::zoomAroundCentre (double factor)
{
    const auto grid = gridArea();
    const auto centre = xToTick (grid.getCentreX());
    pixelsPerTick = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * factor);
    scrollTicks = juce::jmax (0.0, centre - (grid.getCentreX() - keyboardWidth) / pixelsPerTick);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::zoomToBars (int bars)
{
    const auto bar = (double) engine.getSong().barTicks();
    const auto from = std::floor (scrollTicks / bar) * bar;
    zoomToTicks (from, from + bars * bar);
}

void PianoRollComponent::zoomAll()
{
    const auto& song = engine.getSong();
    zoomToTicks (0.0, (double) currentPattern (song).effectiveLength (song.meter()));
}

void PianoRollComponent::zoomToSelection()
{
    const auto target = selectionOrAll();

    if (target.empty())
        return zoomAll();

    const auto& notes = currentPattern (engine.getSong()).notes;
    std::int64_t from = INT64_MAX, to = 0;
    int low = 127, high = 0;

    for (auto i : target)
    {
        const auto& n = notes[(std::size_t) i];
        from = juce::jmin (from, n.start);
        to = juce::jmax (to, n.start + n.length);
        low = juce::jmin (low, n.key);
        high = juce::jmax (high, n.key);
    }

    const auto margin = (double) ticksPerStep;
    zoomToTicks (juce::jmax (0.0, (double) from - margin), (double) to + margin);
    scrollY = juce::jmax (0, rowFor ((low + high) / 2) * rowHeight - gridArea().getHeight() / 2);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::centreOnPlayhead()
{
    const auto& song = engine.getSong();
    const auto length = (double) juce::jmax<std::int64_t> (1, currentPattern (song).effectiveLength (song.meter()));
    auto tick = transportedit::patternPlayhead (song, song.currentPattern, engine.getTransport().positionTicks.load());
    if (tick < 0.0)
        tick = std::fmod (engine.getTransport().positionTicks.load(), length);
    scrollTicks = juce::jmax (0.0, tick - gridArea().getWidth() / pixelsPerTick / 2.0);
    updateScrollBars();
    repaint();
}

void PianoRollComponent::ColourSwatch::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (theme::noteGroupColour (colourIndex, 0.85f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (over || down ? theme::colour::textPrimary : theme::colour::bg0.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    g.setColour (theme::colour::bg0.withAlpha (0.85f));
    g.setFont (theme::font (theme::type::caption, true));
    g.drawText (juce::String (colourIndex + 1), r, juce::Justification::centred, false);
}

void PianoRollComponent::colourSelection (int colour)
{
    const auto target = selection;
    if (target.empty())
        return;
    editNotes ([&] (Pattern& p) { notetools::setColour (p, target, colour); });
    repaint();
}

void PianoRollComponent::selectByColour (int colour)
{
    setSelection (notetools::selectByColour (currentPattern (engine.getSong()), channelNotes(), colour));
}

void PianoRollComponent::showColourMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Colour for new notes"));
    for (int c = 0; c < noteColourCount; ++c)
        menu.addColouredItem (c + 1, tr ("Colour {0}", { juce::String (c + 1) }), theme::noteGroupColour (c, 0.9f), true, c == noteColour);
    menu.addSeparator();
    menu.addItem (1000, tr ("Colour the selected notes with it") + "\tAlt+C", ! selection.empty());
    {
        juce::PopupMenu colourAs;
        for (int c = 0; c < noteColourCount; ++c)
            colourAs.addColouredItem (2001 + c, tr ("Colour {0}", { juce::String (c + 1) }), theme::noteGroupColour (c, 0.9f), true, false);
        menu.addSubMenu (tr ("Colour the selected notes"), colourAs, ! selection.empty());
    }
    {
        juce::PopupMenu select;
        const auto& pattern = currentPattern (engine.getSong());
        for (int c = 0; c < noteColourCount; ++c)
            select.addColouredItem (3001 + c, tr ("Colour {0}", { juce::String (c + 1) }), theme::noteGroupColour (c, 0.9f),
                                    ! notetools::selectByColour (pattern, channelNotes(), c).empty(), false);
        menu.addSubMenu (tr ("Select by colour") + "\tShift+C", select);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&colourButton), [safe = juce::Component::SafePointer (this)] (int id) {
        if (safe == nullptr || id <= 0)
            return;
        if (id <= noteColourCount)
        {
            safe->noteColour = id - 1;
            safe->colourButton.colourIndex = id - 1;
            safe->colourButton.repaint();
        }
        else if (id == 1000)
            safe->colourSelection (safe->noteColour);
        else if (id > 2000 && id <= 2000 + noteColourCount)
            safe->colourSelection (id - 2001);
        else if (id > 3000 && id <= 3000 + noteColourCount)
            safe->selectByColour (id - 3001);
    });
}

juce::PopupMenu PianoRollComponent::selectMenu()
{
    // FL's piano roll Select submenu (its manual): all, invert, odd, random, muted, overlapping, stacked, by colour
    juce::PopupMenu menu;
    auto choose = [this] (std::function<std::vector<int> (const Pattern&, const std::vector<int>&)> pick) {
        setSelection (pick (currentPattern (engine.getSong()), channelNotes()));
    };
    menu.addItem (theme::mnemonic (tr ("All"), 'a') + "\tCtrl+A", [this] { selectAll(); });
    menu.addItem (theme::mnemonic (tr ("Invert selection"), 'I') + "\tShift+I", [this] { runAction ("pianoRoll.invertSelection"); });
    menu.addItem (theme::mnemonic (tr ("Deselect"), 'D') + "\tEsc", ! selection.empty(), false, [this] { setSelection ({}); });
    menu.addSeparator();
    // FL's Select items with their keys (round 168g): they add to the selection
    menu.addItem (theme::mnemonic (tr ("Select one at random"), 'r') + keyText ("pianoRoll.selectRandomOne"), [this] { runAction ("pianoRoll.selectRandomOne"); });
    menu.addItem (theme::mnemonic (tr ("Select more at random"), 'r') + keyText ("pianoRoll.selectRandomMore"), [this] { runAction ("pianoRoll.selectRandomMore"); });
    menu.addItem (theme::mnemonic (tr ("Select odd (swung) notes"), 'o') + keyText ("pianoRoll.selectOddSteps"), [this] { runAction ("pianoRoll.selectOddSteps"); });
    menu.addSeparator();
    menu.addItem (tr ("Odd notes (every other one)"), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectOdd (p, t); }); });
    menu.addItem (tr ("Even notes"), [choose] {
        choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::invertSelection (t, notetools::selectOdd (p, t)); });
    });
    menu.addItem (tr ("Random (half of the notes)"), [choose] {
        const auto seed = (std::uint32_t) juce::Random::getSystemRandom().nextInt();
        choose ([seed] (const Pattern& p, const std::vector<int>& t) { return notetools::selectRandom (p, t, 0.5f, seed); });
    });
    menu.addItem (theme::mnemonic (tr ("Muted notes"), 'm'), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectMuted (p, t); }); });
    menu.addItem (tr ("Overlapping notes"), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectOverlapping (p, t); }); });
    menu.addItem (tr ("Stacked notes (hidden duplicates)"), [choose] { choose ([] (const Pattern& p, const std::vector<int>& t) { return notetools::selectStacked (p, t); }); });
    menu.addItem (theme::mnemonic (tr ("Same colour as the selection"), 'c') + "\tShift+C", [this] { runAction ("pianoRoll.selectByColour"); });
    menu.addSeparator();
    menu.addItem (tr ("Group the selected notes") + "\tShift+G", selection.size() > 1, false, [this] { runAction ("pianoRoll.groupNotes"); });
    menu.addItem (tr ("Ungroup the selected notes") + "\tAlt+G", ! selection.empty(), false, [this] { runAction ("pianoRoll.ungroupNotes"); });
    return menu;
}

void PianoRollComponent::showKeyMenu (int key)
{
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= (int) engine.getSong().channels.size() || key < 0 || key > 127)
        return;
    const auto& labels = engine.getSong().channels[(std::size_t) channel].keyLabels;
    const auto current = labels.find (key);
    auto setLabels = [this, channel] (std::function<void (std::map<int, std::string>&)> edit) {
        engine.editSong ([channel, edit] (Song& s) {
            if (channel < (int) s.channels.size())
                edit (s.channels[(std::size_t) channel].keyLabels);
        });
        repaint();
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (keyName (key));
    menu.addItem (tr ("Rename this key..."), [key, setLabels, text = current != labels.end() ? current->second : std::string()] {
        auto* window = new juce::AlertWindow (tr ("Key label"), tr ("The name shown on {0} for this channel", { keyName (key) }), juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("label", juce::String::fromUTF8 (text.c_str()));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, key, setLabels] (int result) {
                                     if (result != 1)
                                         return;
                                     const auto label = window->getTextEditorContents ("label").trim().toStdString();
                                     setLabels ([key, label] (std::map<int, std::string>& l) {
                                         if (label.empty())
                                             l.erase (key);
                                         else
                                             l[key] = label;
                                     });
                                 }),
                                 true);
    });
    menu.addItem (tr ("Remove this key's label"), current != labels.end(), false,
                  [setLabels, key] { setLabels ([key] (std::map<int, std::string>& l) { l.erase (key); }); });
    menu.addSeparator();
    menu.addItem (tr ("Label the keys with the General MIDI drum map"), [setLabels] {
        setLabels ([] (std::map<int, std::string>& l) {
            for (int k = 35; k <= 81; ++k)
                if (const auto* name = notetools::generalMidiDrumName (k))
                    l[k] = name;
        });
    });
    menu.addItem (tr ("Remove all key labels of this channel"), ! labels.empty(), false,
                  [setLabels] { setLabels ([] (std::map<int, std::string>& l) { l.clear(); }); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void PianoRollComponent::updateChordReadout()
{
    auto keys = engine.heldInputKeys();
    if (keys.size() < 2)
    {
        keys.clear();
        const auto& notes = currentPattern (engine.getSong()).notes;
        for (auto i : selection)
            if (i >= 0 && i < (int) notes.size())
                keys.push_back (notes[(std::size_t) i].key);
    }
    const auto text = chordName (keys);
    if (chordLabel.getText() != text)
        chordLabel.setText (text, juce::dontSendNotification);
    updateChordBubble();
}

void PianoRollComponent::updateChordBubble()
{
    const auto now = juce::Time::getMillisecondCounter();
    if (selection != chordPanelSelection)
    {
        // the toolbar's chord panel follows the selection whether this bubble is on or not (FL: FUN_00d32da0)
        chordPanelSelection = selection;
        if (onSelectedKeys)
        {
            const auto& notes = currentPattern (engine.getSong()).notes;
            std::vector<int> keys;
            for (auto i : selection)
                if (i >= 0 && i < (int) notes.size() && ! notes[(std::size_t) i].muted)
                    keys.push_back (notes[(std::size_t) i].key);
            onSelectedKeys (keys);
        }
    }
    if (! showChordName)
    {
        chordBubble.setVisible (false);
        chordBubbleSelection = selection;
        return;
    }
    if (selection != chordBubbleSelection)
    {
        // FL (FUN_00e5a850 → FUN_00d32da0, on every selection change): the different keys of the selected notes, muted
        // ones left out; 2 to 10 of them name a chord
        chordBubbleSelection = selection;
        const auto& notes = currentPattern (engine.getSong()).notes;
        std::vector<int> keys;
        for (auto i : selection)
            if (i >= 0 && i < (int) notes.size() && ! notes[(std::size_t) i].muted)
                keys.push_back (notes[(std::size_t) i].key);
        std::sort (keys.begin(), keys.end());
        keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
        const auto text = keys.size() >= 2 && keys.size() <= 10 ? chordName (keys) : juce::String();
        if (text.isNotEmpty())
        {
            chordBubble.setText (text, juce::dontSendNotification);
            const auto font = chordBubble.getFont();
            const auto width = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + 20;
            const auto grid = gridArea();
            chordBubble.setBounds (grid.getX() + 16, grid.getY() + 16, width, juce::roundToInt (font.getHeight()) + 10);
            chordBubble.setVisible (true);
            chordBubble.toFront (false);
            chordBubbleUntil = now + 2000; // (FL keeps its chord readout at least 2 s: FUN_00d32da0 +0xac)
        }
        else
            chordBubble.setVisible (false);
    }
    else if (chordBubble.isVisible() && now > chordBubbleUntil)
        chordBubble.setVisible (false);
}

bool PianoRollComponent::runAction (const std::string& id)
{
    auto is = [&id] (const char* name) { return id == name; };

    if (is ("pianoRoll.arpeggiate")) openNoteTool (NoteTool::arpeggiate);
    else if (is ("pianoRoll.strum")) openNoteTool (NoteTool::strum);
    else if (is ("pianoRoll.flip")) openNoteTool (NoteTool::flip);
    else if (is ("pianoRoll.randomise")) openNoteTool (NoteTool::randomise);
    else if (is ("pianoRoll.scaleLevels")) openNoteTool (NoteTool::scaleLevels);
    else if (is ("pianoRoll.limit")) openNoteTool (NoteTool::limit);
    else if (is ("pianoRoll.quantize")) openNoteTool (NoteTool::quantize);
    else if (is ("pianoRoll.chop")) openNoteTool (NoteTool::chop);
    else if (is ("pianoRoll.flam")) openNoteTool (NoteTool::flam);
    else if (is ("pianoRoll.claw")) openNoteTool (NoteTool::claw);
    else if (is ("pianoRoll.lfo")) openNoteTool (NoteTool::lfo);
    else if (is ("pianoRoll.articulate")) openNoteTool (NoteTool::articulate);
    else if (is ("pianoRoll.riffMachine")) openNoteTool (NoteTool::riffMachine);
    else if (is ("pianoRoll.chordProgression")) openNoteTool (NoteTool::chordProgression);
    else if (is ("pianoRoll.runLastScript")) runLastScript();
    else if (is ("pianoRoll.zoomIn")) zoomAroundCentre (1.25);
    else if (is ("pianoRoll.zoomOut")) zoomAroundCentre (1.0 / 1.25);
    else if (is ("pianoRoll.zoom1")) zoomToBars (1);
    else if (is ("pianoRoll.zoom2")) zoomToBars (4);
    else if (is ("pianoRoll.zoom3")) zoomToBars (16);
    else if (is ("pianoRoll.zoomAll")) zoomAll();
    else if (is ("pianoRoll.zoomSelection")) zoomToSelection();
    else if (is ("pianoRoll.centrePlayhead")) centreOnPlayhead();
    else if (is ("pianoRoll.follow")) EditorGlobals::get().setFollowing (true, ! EditorGlobals::get().isFollowing (true));
    else if (is ("pianoRoll.quantizeStarts")) quantizeOperation (0);
    else if (is ("pianoRoll.quickQuantize")) quantizeOperation (1);
    else if (is ("pianoRoll.quantizeHalf")) quantizeOperation (2);
    else if (is ("pianoRoll.colourSelected")) colourSelection (noteColour);
    else if (is ("pianoRoll.selectByColour"))
    {
        // the colour of the selected note (FL: the selection's colour), else the current note colour
        const auto& notes = currentPattern (engine.getSong()).notes;
        selectByColour (! selection.empty() && selection.front() < (int) notes.size() ? notes[(std::size_t) selection.front()].colour : noteColour);
    }
    else if (is ("pianoRoll.invertSelection")) setSelection (notetools::invertSelection (channelNotes(), selection));
    // round 168g: FL's View / Select / Stamp / Edit / File items
    else if (is ("pianoRoll.gridHighlights"))
    {
        gridHighlights = ! gridHighlights;
        if (EditorGlobals::get().onOptionsChanged)
            EditorGlobals::get().onOptionsChanged();
        repaint();
    }
    else if (is ("pianoRoll.backgroundWave")) setBackgroundWaveShown (! isBackgroundWaveShown());
    else if (is ("pianoRoll.songBarsRuler"))
    {
        songBarsRuler = ! songBarsRuler;
        if (EditorGlobals::get().onOptionsChanged)
            EditorGlobals::get().onOptionsChanged();
        repaint();
    }
    else if (is ("pianoRoll.showChordName"))
    {
        showChordName = ! showChordName;
        chordBubbleSelection.clear(); // shows the current selection's chord at once
        if (EditorGlobals::get().onOptionsChanged)
            EditorGlobals::get().onOptionsChanged();
        updateChordBubble();
    }
    else if (is ("pianoRoll.selectRandomOne") || is ("pianoRoll.selectRandomMore"))
        setSelection (noteedit::selectRandomMore (currentPattern (engine.getSong()), channelNotes(), selection, is ("pianoRoll.selectRandomMore"),
                                                  (std::uint32_t) juce::Random::getSystemRandom().nextInt()));
    else if (is ("pianoRoll.selectOddSteps"))
        setSelection (noteedit::selectOddSteps (currentPattern (engine.getSong()), channelNotes(), selection, ticksPerStep));
    else if (is ("pianoRoll.stampNone")) chordBox.setSelectedId (1, juce::sendNotificationSync);
    else if (is ("pianoRoll.scaleSnap"))
    {
        if (scaleIndex() > 0) // (like its menu item and button: only with a scale chosen)
            setScaleSnap (! scaleSnap);
    }
    else if (is ("pianoRoll.insertSpace")) spaceOperation (0);
    else if (is ("pianoRoll.sliceInsertSpace")) spaceOperation (1);
    else if (is ("pianoRoll.deleteSpace")) spaceOperation (2);
    else if (is ("pianoRoll.trimSpace")) spaceOperation (3);
    else if (is ("pianoRoll.deleteAll")) deleteAllNotes();
    else if (is ("pianoRoll.insertControllerValue")) insertControllerValue();
    else if (is ("pianoRoll.importMidi") || is ("pianoRoll.openScore"))
    {
        // FL: Import MIDI and Open score share one file window (FUN_0120b340): .fsc, .mid or both
        const auto score = is ("pianoRoll.openScore");
        chooser = lynx::ui::makeFileChooser (score ? tr ("Open score") : tr ("Import MIDI"), juce::File(),
                                                       score ? "*.fsc;*.mid;*.midi" : "*.mid;*.midi");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<PianoRollComponent> (this)] (const juce::FileChooser& fc) {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->loadFile (fc.getResult());
                              });
    }
    else if (is ("pianoRoll.exportMidi"))
    {
        const auto& song = engine.getSong();
        const auto name = juce::String::fromUTF8 (song.activePattern().name.c_str());
        chooser = lynx::ui::makeFileChooser (tr ("Export MIDI"), juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name + ".mid"),
                                                       "*.mid");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<PianoRollComponent> (this)] (const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (! file.hasFileExtension ("mid;midi"))
                                      file = file.withFileExtension (".mid");
                                  juce::String error;
                                  if (! MidiFileIo::savePattern (safe->engine.getSong(), safe->engine.getSong().currentPattern, file, error))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
                              });
    }
    else if (is ("pianoRoll.saveScore"))
    {
        const auto channel = engine.getSelectedChannel();
        const auto& song = engine.getSong();
        auto name = channel >= 0 && channel < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str())
                                                                        : juce::String ("Score");
        chooser = lynx::ui::makeFileChooser (tr ("Save score as"),
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (juce::File::createLegalFileName (name) + ".fsc"),
                                                       "*.fsc;*.mid");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<PianoRollComponent> (this)] (const juce::FileChooser& fc) {
                                  auto file = fc.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (! file.hasFileExtension ("fsc;mid;midi"))
                                      file = file.withFileExtension (".fsc");
                                  if (! safe->saveScore (file))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save score as"),
                                                                              tr ("There are no notes to save, or the file could not be written."));
                              });
    }
    else if (is ("pianoRoll.copyMidiClipboard")) copyToMidiClipboard();
    else if (is ("pianoRoll.pasteMidiClipboard")) pasteFromMidiClipboard();
    else if (is ("pianoRoll.markerAdd")) editPatternMarker (markerKeyTick(), -1);
    else if (is ("pianoRoll.markerAuto"))
    {
        const auto tick = snap ((double) markerKeyTick(), true);
        if (std::none_of (currentPattern (engine.getSong()).markers.begin(), currentPattern (engine.getSong()).markers.end(),
                          [tick] (const Marker& m) { return m.tick == tick; })) // (FL keeps the marker there, no undo step)
            engine.editSong ([tick] (Song& s) { noteedit::addPatternAutoMarker (currentPattern (s), tick); });
        repaint();
    }
    else if (is ("pianoRoll.markerSignature")) addPatternSignatureByKey();
    else if (is ("pianoRoll.browseScores"))
    {
        if (onBrowseScores)
            onBrowseScores();
    }
    else if (is ("pianoRoll.groupNotes") || is ("pianoRoll.ungroupNotes"))
    {
        const auto target = selection;
        if (target.empty())
            return true;
        const auto group = is ("pianoRoll.groupNotes");
        editNotes ([&] (Pattern& p) {
            if (group)
                notetools::groupNotes (p, target);
            else
                notetools::ungroupNotes (p, target);
        });
        repaint();
    }
    else
        return false;

    return true;
}

void PianoRollComponent::wheelNoteProperty (int note, float deltaY, bool fine, juce::Point<int> mouse)
{
    // FL 2026 [SHOT round 166e]: the whole selection wherever the mouse is, else the one note
    const auto targets = note < 0 ? selection : std::vector<int> { note };
    if (targets.empty())
        return;
    const auto now = juce::Time::getMillisecondCounter();
    const auto newBurst = now - lastWheelEdit > 700; // one undo step per burst of wheel moves
    lastWheelEdit = now;
    editNotes ([&] (Pattern& p) {
        for (auto i : targets)
        {
            auto& n = p.notes[(std::size_t) i];
            setLaneValue (n, (float) gesture::wheelStepValue (laneValue (n), deltaY, fine));
        }
        if (laneIsChance())
            notetools::spreadGroupChance (p, targets);
    }, newBurst);

    // hear it (FL previews the single note; the selection's nearest note too is LYNX's own, issue #16) and a single
    // note becomes the template for new notes (FL)
    const auto& notes = currentPattern (engine.getSong()).notes;
    const auto heard = note >= 0 ? note : noteedit::nearestNote (notes, targets, xToTick (mouse.x), yToKey (mouse.y), pixelsPerTick, rowHeight);
    if (heard < 0 || heard >= (int) notes.size())
        return;
    const auto& n = notes[(std::size_t) heard];
    if (note >= 0)
        noteTemplate.takeFrom (n);
    previewOn (n.key, n.velocity);
    const auto serial = ++wheelPreviewSerial;
    juce::Timer::callAfterDelay (180, [safe = juce::Component::SafePointer<PianoRollComponent> (this), serial] {
        if (safe != nullptr && safe->wheelPreviewSerial == serial && safe->drag == Drag::none)
            safe->previewOff();
    });
}

void PianoRollComponent::glideBy (double ticks, double pixels, bool immediate)
{
    if (! gliding)
    {
        glideX.jumpTo (scrollTicks);
        glideY.jumpTo (scrollY);
        gliding = true;
    }

    const auto grid = gridArea();
    glideX.target = juce::jlimit (0.0, juce::jmax (0.0, (double) viewLengthTicks() - grid.getWidth() / pixelsPerTick), glideX.target + ticks);
    glideY.target = juce::jlimit (0.0, juce::jmax (0.0, (double) rows.count * rowHeight - grid.getHeight()), glideY.target + pixels);
    if (immediate || ! ui::motion::enabled()) // (Settings › General › Animations off: the wheel jumps)
    {
        scrollTicks = glideX.target;
        scrollY = (int) std::round (glideY.target);
        gliding = false;
        updateScrollBars();
        glideX.jumpTo (scrollTicks);
        glideY.jumpTo (scrollY);
        repaint();
        return;
    }
    glideFrame();
}

void PianoRollComponent::glideFrame()
{
    if (! gliding)
        return;

    // someone else moved the view (scroll bar, zoom, pan): the glide gives way
    if (std::abs (scrollTicks - glideX.current) > 1e-6 || std::abs ((double) scrollY - std::round (glideY.current)) > 0.5)
    {
        gliding = false;
        return;
    }

    scrollTicks = glideX.step (0.45, 0.5 / pixelsPerTick);
    scrollY = (int) std::round (glideY.step (0.45, 1.0));
    glideY.current = scrollY;
    updateScrollBars();
    glideX.current = scrollTicks; // the scroll bar may have clamped it
    gliding = glideX.isMoving() || glideY.isMoving();
    repaint();
}

void PianoRollComponent::mouseMagnify (const juce::MouseEvent& e, float scale)
{
    gliding = false;
    const auto factor = gesture::pinchZoomFactor (scale);
    if (e.mods.isAltDown()) // taller / shorter keys, keeping the key under the pointer in place
    {
        const auto key = yToKey (e.y);
        const auto offset = e.y - keyToY (key);
        const auto height = juce::jlimit (gesture::minKeyHeight, gesture::maxKeyHeight, juce::roundToInt (rowHeight * factor));
        rowHeight = height != rowHeight ? height : juce::jlimit (gesture::minKeyHeight, gesture::maxKeyHeight, rowHeight + (factor > 1.0 ? 1 : factor < 1.0 ? -1 : 0));
        scrollY = juce::jmax (0, rowFor (key) * rowHeight + offset - (e.y - headerHeight));
    }
    else
    {
        const auto anchorPixels = (double) juce::jmax (0, e.x - keyboardWidth);
        const auto zoomed = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * factor);
        scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchorPixels);
        pixelsPerTick = zoomed;
    }
    updateScrollBars();
    repaint();
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& rawWheel)
{
    const auto wheel = flmods::normalisedWheel (rawWheel); // Windows touchpads: pieces of a notch (round 170e)
    const auto ctrl = flmods::ctrl (e.mods), alt = e.mods.isAltDown(), shift = e.mods.isShiftDown();
    const auto overGrid = gridArea().contains (e.getPosition()) || velocityArea().contains (e.getPosition());

    // the wheel over the zoom wheel: one zoom level a notch (FL: the only wheel gesture of the vertical zoom)
    if (zoomWheelArea().contains (e.getPosition()))
    {
        if (wheel.deltaY != 0.0f)
        {
            const auto key = zoomAnchorKey();
            setRowHeightAround (rowHeight + (wheel.deltaY > 0 ? 2 : -2), key, keyToY (key) + rowHeight / 2);
        }
        return;
    }

    // Alt+wheel: the velocity (or the lane's property) of the selected notes wherever the mouse is, with nothing
    // selected of the note nearest to the mouse (FL 2026, round 166e); Ctrl+Alt: fine steps
    if (alt && ! shift && overGrid && wheel.deltaY != 0.0f)
    {
        auto note = -1;
        if (selection.empty())
        {
            note = hoverAt (e.getPosition()).note;
            if (note < 0)
                note = noteedit::nearestNote (currentPattern (engine.getSong()).notes, channelNotes(), xToTick (e.x), yToKey (e.y), pixelsPerTick,
                                              velocityArea().contains (e.getPosition()) ? 0.0 : rowHeight);
        }
        if (note >= 0 || ! selection.empty())
        {
            wheelNoteProperty (note, wheel.deltaY, ctrl, e.getPosition());
            return;
        }
    }

    if (shift && alt) // nudge the selection by the grid
    {
        if (wheel.deltaY != 0.0f && ! selection.empty())
        {
            const auto now = juce::Time::getMillisecondCounter();
            const auto newBurst = now - lastWheelEdit > 700;
            lastWheelEdit = now;
            const auto ticks = (wheel.deltaY > 0 ? 1 : -1) * gridTicks();
            editNotes ([this, ticks] (Pattern& p) {
                auto deltaTicks = ticks;
                auto deltaKeys = 0;
                noteedit::move (p, selection, deltaTicks, deltaKeys);
            }, newBurst);
            sortNotes();
        }
        return;
    }

    gliding = false;

    if (ctrl && alt)
    {
        // vertical zoom keeping the key under the mouse in place
        const auto key = yToKey (e.y);
        const auto offset = e.y - keyToY (key);
        rowHeight = juce::jlimit (gesture::minKeyHeight, gesture::maxKeyHeight, rowHeight + (wheel.deltaY > 0 ? 2 : -2));
        scrollY = juce::jmax (0, rowFor (key) * rowHeight + offset - (e.y - headerHeight));
    }
    else if (ctrl)
    {
        // zoom keeping the tick under the mouse in place
        const auto anchorPixels = (double) juce::jmax (0, e.x - keyboardWidth);
        const auto zoomed = juce::jlimit (2.0 / ticksPerStep, 120.0 / ticksPerStep, pixelsPerTick * gesture::wheelZoomFactor (wheel.deltaY, wheel.isSmooth));
        scrollTicks = gesture::zoomAnchored (scrollTicks, pixelsPerTick, zoomed, anchorPixels);
        pixelsPerTick = zoomed;
    }
    else
    {
        // plain wheel: up/down (Shift: left/right); a horizontal wheel or trackpad swipe scrolls sideways
        const auto vertical = gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 3.0 * rowHeight);
        const auto sideways = gesture::wheelScrollPixels (wheel.deltaX, wheel.isSmooth, 60.0);
        if (shift)
            glideBy (gesture::wheelScrollPixels (wheel.deltaY, wheel.isSmooth, 80.0) / pixelsPerTick, 0.0, wheel.isSmooth);
        else
            glideBy (sideways / pixelsPerTick, vertical, wheel.isSmooth); // a trackpad moves the view at once
        return;
    }

    updateScrollBars();
    repaint();
}

// --- keyboard ----------------------------------------------------------------

void PianoRollComponent::deleteSelection()
{
    if (selection.empty())
        return;

    const auto doomed = selection;
    setSelection ({});
    editNotes ([&doomed] (Pattern& p) { noteedit::remove (p, doomed); });
}

void PianoRollComponent::copySelection()
{
    clipboard = noteedit::copy (currentPattern (engine.getSong()), selectionOrAll());
}

// Pain point 26 (round 177d, LYNX's own; FL's ghost notes can only be looked at): the ghost notes on show (the other
// channels' notes in this pattern, and this channel's notes in the clips that play at the same time) go to the note
// clipboard, so Ctrl+V pastes them into the channel being edited, where they were drawn.
std::vector<Note> PianoRollComponent::visibleGhostNotes() const
{
    std::vector<Note> out;
    if (! showGhosts || allChannels)
        return out;
    const auto& song = engine.getSong();
    for (const auto& n : currentPattern (song).notes)
        if (! edits (n) && ghostShown (n.channel))
            out.push_back (n);
    for (const auto& n : polish::clipGhostNotes (song, song.currentPattern, engine.getSelectedChannel()))
        out.push_back (n);
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

void PianoRollComponent::copyGhostNotes()
{
    auto ghosts = visibleGhostNotes();
    if (ghosts.empty())
        return;
    const auto count = (int) ghosts.size();
    clipboard = std::move (ghosts);
    ui::postHint ({ tr ("Copied {0} ghost notes: Ctrl+V pastes them into this channel", { juce::String (count) }) }, 2500);
}

void PianoRollComponent::paste()
{
    if (clipboard.empty())
        return;

    // as in FL: pastes at the same position, already selected for dragging
    std::vector<int> added;
    const auto channel = allChannels ? -1 : engine.getSelectedChannel(); // ("All": each note keeps its channel)
    editNotes ([&added, channel] (Pattern& p) {
        added = noteedit::paste (p, clipboard, 0, channel);
        notetools::regroup (p, added); // copies of grouped notes form groups of their own
    });
    setSelection (added);
    sortNotes();
}

void PianoRollComponent::duplicateSelection()
{
    const auto source = selectionOrAll();

    if (source.empty())
        return;

    std::vector<int> added;
    const auto meter = engine.getSong().meter();
    editNotes ([&added, &source, meter] (Pattern& p) {
        added = noteedit::duplicateBars (p, source, meter); // FL: whole bars later (round 170h)
        notetools::regroup (p, added);
    });
    setSelection (added);
    sortNotes();
}

void PianoRollComponent::quantizeOperation (int kind)
{
    const auto grid = gridTicks();
    runOperation ([grid, kind] (Pattern& p, const std::vector<int>& t) {
        if (kind == 1)
            noteedit::quantizeFull (p, t, grid);
        else
            transportedit::quantizeNoteStarts (p, t, grid, kind == 2 ? 0.5 : 1.0);
        return t;
    });
}

void PianoRollComponent::nudge (std::int64_t ticks, int keys)
{
    if (selection.empty())
        return;

    // folded to the scale (round 166d): Up / Down step through the shown keys (an octave stays an octave)
    if (folded() && keys != 0 && std::abs (keys) < 12)
    {
        editNotes ([this, &ticks, keys] (Pattern& p) {
            auto noKeys = 0;
            noteedit::move (p, selection, ticks, noKeys);
            for (auto i : selection)
                if (i >= 0 && i < (int) p.notes.size())
                    p.notes[(std::size_t) i].key = rows.step (p.notes[(std::size_t) i].key, keys);
        });
    }
    else
    {
        if (keys == 1 || keys == -1) // snap to scale: up / down step along the scale (every selected note, issue #14)
            if (const auto scale = snapScale(); scale.active())
            {
                editNotes ([this, keys, scale] (Pattern& p) { scalesnap::moveBySteps (p, selection, keys, scale); });
                keys = 0;
                if (ticks == 0)
                {
                    sortNotes();
                    return;
                }
            }

        editNotes ([this, &ticks, &keys] (Pattern& p) { noteedit::move (p, selection, ticks, keys); });
    }

    if (keys != 0 && selection.size() == 1)
    {
        const auto& n = currentPattern (engine.getSong()).notes[(std::size_t) selection[0]];
        previewOn (n.key, n.velocity);
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<PianoRollComponent> (this)] {
            if (safe != nullptr)
                safe->previewOff();
        });
    }

    sortNotes();
}

bool PianoRollComponent::handleKey (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto cmd = mods.isCommandDown();
    const auto alt = mods.isAltDown();
    const auto shift = mods.isShiftDown();
    const auto code = key.getKeyCode();
    const auto letter = keys::letterOfKeyCode (code); // (0 for F-keys, numpad, arrows: issue #12)
    const auto grid = gridTicks();
    const auto bar = engine.getSong().barTicks();

    if (code == juce::KeyPress::backspaceKey)
    {
        EditorGlobals::get().toggleGrid();
        return true;
    }

    if (code == juce::KeyPress::deleteKey && ! cmd) // (Ctrl+Shift+Del: pattern.delete; Ctrl+Del: pianoRoll.deleteSpace)
    {
        deleteSelection();
        return true;
    }

    if (code == juce::KeyPress::escapeKey)
    {
        setSelection ({});
        setTimeSelection (-1, -1);
        return true;
    }

    if (alt && cmd && letter == 'V') // editable ghost notes
    {
        editGhosts = ! editGhosts;
        if (editGhosts && ! showGhosts)
            ghostButton.setToggleState (true, juce::sendNotificationSync);
        repaint();
        return true;
    }

    if (alt && ! cmd && letter == 'V')
    {
        ghostButton.setToggleState (! ghostButton.getToggleState(), juce::sendNotificationSync);
        return true;
    }

    if (cmd && alt && code == juce::KeyPress::homeKey)
    {
        resizeFromLeft = ! resizeFromLeft;
        return true;
    }

    if (cmd && letter == 'A')
    {
        selectAll();
        return true;
    }

    if (cmd && ! shift && letter == 'C') // (Ctrl+Shift+C: pattern.clone)
    {
        copySelection();
        return true;
    }

    if (cmd && letter == 'X')
    {
        copySelection();
        deleteSelection();
        return true;
    }

    if (cmd && ! shift && letter == 'V') // (Ctrl+Shift+V: pianoRoll.pasteMidiClipboard)
    {
        paste();
        return true;
    }

    if (cmd && letter == 'B')
    {
        duplicateSelection();
        return true;
    }

    // quick note operations: one undo step each, on the selection (or all notes). The quantize ones are key map
    // actions (pianoRoll.quickQuantize / quantizeStarts / quantizeHalf) so they can be reassigned.
    if (cmd && letter == 'U')
    {
        runOperation ([grid] (Pattern& p, const std::vector<int>& t) { return noteedit::chop (p, t, grid); });
        return true;
    }

    if (cmd && letter == 'G')
    {
        runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::join (p, t); return std::vector<int> {}; });
        return true;
    }

    if (cmd && letter == 'L')
    {
        runOperation ([] (Pattern& p, const std::vector<int>& t) { noteedit::legato (p, t); return t; });
        return true;
    }

    if (shift && ! cmd && ! alt && letter == 'D')
    {
        runOperation ([grid] (Pattern& p, const std::vector<int>& t) { noteedit::shrinkToGrid (p, t, grid); return t; });
        return true;
    }

    if (cmd && shift && (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey))
    {
        const auto steps = code == juce::KeyPress::rightKey ? 1 : -1;
        runOperation ([grid, bar, steps] (Pattern& p, const std::vector<int>& t) { noteedit::rotate (p, t, grid, bar, steps); return t; });
        return true;
    }

    // FL's editor keys (ShortcutsModule EEMuteAction / EEUnmuteAction / EEDeselAction, round 167j): Alt+M mutes,
    // Shift+Alt+M unmutes, Ctrl+D deselects. (Ctrl+M stays FL's metronome.)
    if (alt && ! cmd && letter == 'M')
    {
        setNotesMuted (! shift);
        return true;
    }

    if (cmd && ! alt && ! shift && letter == 'D')
    {
        setSelection ({});
        setTimeSelection (-1, -1);
        return true;
    }

    if (cmd && letter == 'P')
    {
        togglePorta();
        return true;
    }

    // FL's editor arrow keys (noteedit::arrowMove): Shift+Up/Down a semitone, Ctrl+Up/Down an octave,
    // Shift+Left/Right one grid step
    if (! selection.empty())
    {
        using noteedit::Arrow;
        const auto arrow = code == juce::KeyPress::upKey ? std::optional<Arrow> (Arrow::up)
                         : code == juce::KeyPress::downKey ? std::optional<Arrow> (Arrow::down)
                         : code == juce::KeyPress::leftKey ? std::optional<Arrow> (Arrow::left)
                         : code == juce::KeyPress::rightKey ? std::optional<Arrow> (Arrow::right)
                         : std::nullopt;
        if (arrow)
            if (const auto move = noteedit::arrowMove (*arrow, cmd, shift, alt); move.handled)
            {
                nudge (move.gridSteps * grid, move.keys);
                return true;
            }
    }

    // (the tools and the other single letters are key map actions now: runKeyAction, issue #12)
    return false;
}

bool PianoRollComponent::runKeyAction (const std::string& id)
{
    for (const auto& t : editTools())
        if (t.tool != EditTool::slip && id == "pianoRoll.tool." + std::string (editToolId (t.tool)))
        {
            tools.setTool (t.tool);
            return true;
        }

    if (id == "pianoRoll.slide") slideButton.setToggleState (! slideButton.getToggleState(), juce::dontSendNotification);
    else if (id == "pianoRoll.previousChannel") stepChannel (-1, false);
    else if (id == "pianoRoll.nextChannel") stepChannel (1, false);
    else if (id == "pianoRoll.previousChannelWithNotes") stepChannel (-1, true);
    else if (id == "pianoRoll.nextChannelWithNotes") stepChannel (1, true);
    else return false;
    return true;
}

void PianoRollComponent::refreshToolKeys (const std::function<juce::String (const std::string&)>& keyOf)
{
    tools.refreshKeys ([&keyOf] (EditTool t) { return keyOf ("pianoRoll.tool." + std::string (editToolId (t))); });
    // round 179a: the slide toggle is only an icon, so its tips name its key the way the tool buttons do
    const auto slideKey = keyOf ("pianoRoll.slide");
    slideButton.setTooltip (tr ("New notes are \"slide\" notes: the currently sounding note glides to them (808 glide)")
                            + (slideKey.isNotEmpty() ? " (" + slideKey + ")" : juce::String()));
    ui::setQuickTip (slideButton, tr ("Slide notes"), slideKey);
}

juce::PopupMenu PianoRollComponent::midiMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Import .mid into this channel...") + keyText ("pianoRoll.importMidi"), [this] { runAction ("pianoRoll.importMidi"); });
    menu.addItem (tr ("Export pattern as .mid...") + keyText ("pianoRoll.exportMidi"), [this] { runAction ("pianoRoll.exportMidi"); });
    return menu;
}

void PianoRollComponent::importMidi (const midiimport::ParsedMidi& midi, const juce::String& name)
{
    // FL's "Import MIDI data" window with Blend (REA round 146b): which tracks / channels / controller
    static midiimport::Options remembered;
    juce::Component::SafePointer<PianoRollComponent> safe (this);
    showMidiImportDialog (name, midi, remembered, true, [safe, midi] (const midiimport::Options& o) {
        remembered = o;
        if (safe == nullptr)
            return;
        std::vector<int> added;
        std::string err;
        const auto channel = safe->engine.getSelectedChannel();
        safe->engine.editSong ([&] (Song& s) { added = midiimport::importIntoChannel (s, channel, midi, o, err); });
        if (added.empty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", juce::String::fromUTF8 (err.c_str()));
            return;
        }
        safe->knownNoteCount = currentPattern (safe->engine.getSong()).notes.size();
        safe->setSelection (added);
        safe->scrollToNotes();
        safe->repaint();
    });
}

// --- other -------------------------------------------------------------------

void PianoRollComponent::scrollBarMoved (juce::ScrollBar* bar, double start)
{
    if (bar == &hBar)
        scrollTicks = start;
    else
        scrollY = (int) start;

    repaint();
}

void PianoRollComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // pattern/channel switched or the pattern changed externally (undo, another window): the selection no longer applies
    const auto& song = engine.getSong();
    const auto count = currentPattern (song).notes.size();

    if (song.currentPattern != knownPattern || engine.getSelectedChannel() != knownChannel || count != knownNoteCount)
    {
        if (engine.getSelectedChannel() != knownChannel)
        {
            stopListening();
            if (knownChannel >= 0)
                allChannels = false; // another channel picked (Channel Rack, keys): back to it alone
        }

        if (song.currentPattern != knownPattern)
            timeFrom = timeTo = -1; // (the time selection belongs to the pattern it was made in)
        knownPattern = song.currentPattern;
        knownChannel = engine.getSelectedChannel();
        knownNoteCount = count;

        if (drag == Drag::none)
            selection.clear();
    }

    followButton.setToggleState (EditorGlobals::get().isFollowing (true), juce::dontSendNotification);
    syncPadRows();
    refreshChannelBox();
    rebuildLaneBox();
    updateScrollBars();
    repaint();
}

std::optional<AutomationTarget> PianoRollComponent::eventLaneTarget() const
{
    const auto k = laneBox.getSelectedId() - eventLaneIdBase;
    if (k >= 0 && k < (int) eventTargets.size())
        return eventTargets[(std::size_t) k];
    return std::nullopt;
}

void PianoRollComponent::rebuildLaneBox (std::optional<AutomationTarget> shown, bool force)
{
    // the pattern's event lanes, plus the one shown (it may have no events yet)
    if (! shown)
        shown = eventLaneTarget();
    std::vector<AutomationTarget> targets;
    for (const auto& lane : currentPattern (engine.getSong()).events)
        if (lane.target.kind != AutomationTargetKind::none && ! lane.events.empty())
            targets.push_back (lane.target);
    if (shown && std::find (targets.begin(), targets.end(), *shown) == targets.end())
        targets.push_back (*shown);
    if (targets == eventTargets && ! force)
        return;

    eventTargets = targets;
    const auto previous = laneBox.getSelectedId();
    laneBox.clear (juce::dontSendNotification);
    addNoteLanes();
    if (! eventTargets.empty())
        laneBox.addSectionHeading (tr ("Pattern events"));
    for (int k = 0; k < (int) eventTargets.size(); ++k)
        laneBox.addItem (describeAutomationTarget (engine, eventTargets[(std::size_t) k]), eventLaneIdBase + k);
    auto id = previous >= 1 && previous <= laneLast ? previous : 1;
    if (shown)
        id = eventLaneIdBase + (int) (std::find (eventTargets.begin(), eventTargets.end(), *shown) - eventTargets.begin());
    laneBox.setSelectedId (id, juce::dontSendNotification);
}

void PianoRollComponent::showEventLane (const AutomationTarget& target)
{
    rebuildLaneBox (target, true);
    repaint();
}

void PianoRollComponent::drawEvents (int fromX, int toX, int y, bool erase)
{
    const auto target = eventLaneTarget();
    if (! target)
        return;
    const auto lane = velocityArea();
    const auto value = juce::jlimit (0.0f, 1.0f, (float) (lane.getBottom() - 2 - y) / (float) (lane.getHeight() - 8));
    const auto step = std::max<std::int64_t> (1, gridTicks());
    const auto t0 = std::max<std::int64_t> (0, (std::int64_t) std::floor (xToTick (juce::jmin (fromX, toX)) / (double) step) * step);
    const auto t1 = std::max (t0 + step, (std::int64_t) std::floor (xToTick (juce::jmax (fromX, toX)) / (double) step) * step + step);
    const auto pattern = engine.getSong().currentPattern;
    engine.editSong ([&] (Song& s) {
        if (pattern < 0 || pattern >= (int) s.patterns.size())
            return;
        auto& p = s.patterns[(std::size_t) pattern];
        if (erase)
        {
            if (auto* l = eventedit::findLane (p, *target))
                eventedit::erase (*l, t0, t1);
            eventedit::removeEmptyLanes (p);
        }
        else
            eventedit::fill (eventedit::laneFor (p, *target), t0, t1, value, step);
    });
    repaint (velocityArea());
}

NoteMask PianoRollComponent::soundingNow() const
{
    if (! allChannels)
        return engine.soundingKeys (engine.getSelectedChannel());
    // "All": every channel with notes in this pattern
    NoteMask all {};
    const auto& song = engine.getSong();
    std::vector<char> seen (song.channels.size(), 0);
    for (const auto& n : currentPattern (song).notes)
        if (n.channel >= 0 && n.channel < (int) seen.size() && ! seen[(std::size_t) n.channel])
        {
            seen[(std::size_t) n.channel] = 1;
            const auto k = engine.soundingKeys (n.channel);
            all[0] |= k[0], all[1] |= k[1];
        }
    if (const auto selected = engine.getSelectedChannel(); selected >= 0 && selected < (int) seen.size() && ! seen[(std::size_t) selected])
    {
        const auto k = engine.soundingKeys (selected);
        all[0] |= k[0], all[1] |= k[1];
    }
    return all;
}

void PianoRollComponent::timerCallback()
{
    if (channelBox.getSelectedId() != (allChannels ? allChannelsId : engine.getSelectedChannel() + 1))
        refreshChannelBox();
    updateChordReadout();

    // the keys the channel plays light up on the keyboard (song, pattern, typing keyboard, MIDI)
    if (const auto lit = isShowing() ? soundingNow() : NoteMask {}; lit != litKeys)
    {
        litKeys = lit;
        repaint (0, headerHeight, keyboardWidth, getHeight() - headerHeight);
    }

    if (const bool playing = engine.getTransport().playing.load(); playButton.getToggleState() != playing)
        playButton.setToggleState (playing, juce::dontSendNotification);
}

bool PianoRollComponent::playheadFrame (double)
{
    auto& transport = engine.getTransport();
    auto tick = -1.0;
    const auto playing = transport.playing.load();

    // the playhead shows while the edited pattern plays: Pattern mode, or a clip of it in Song mode (round 162);
    // stopped in Step edit too (round 170e5): it is where the next played note goes (FL enters step-edit notes at the
    // song position, which it draws in the Piano roll), and it moves a snap step on after each one
    if ((playing || engine.isStepEdit()) && isShowing())
    {
        const auto& song = engine.getSong();
        // (between two audio blocks it moves on at the song's tempo: smooth at 144 Hz, round 174e)
        tick = transportedit::patternPlayhead (song, song.currentPattern, transport.positionTicks.load() + engine.playheadAdvanceTicks(),
                                               engine.patternLinearOrigin());
    }

    // whenever it moved: the line is anti-aliased, so even a fraction of a pixel shows (zoomed out, a playhead crossing
    // a few pixels a second glides at the display's rate instead of stepping, as FL's does)
    const auto moved = (tick < 0.0) != (playheadTick < 0.0) || std::abs ((tick - playheadTick) * pixelsPerTick) >= 0.002;
    if (moved)
    {
        // (old and new column as one rectangle when they overlap: each rectangle of a repaint is a paint)
        auto column = [this] (double t) {
            return t >= 0.0 ? juce::Rectangle<int> ((int) tickToX (t) - 7, 0, 14, getHeight()) : juce::Rectangle<int>();
        };
        auto dirty = [this] (juce::Rectangle<int> r) {
            if (! r.isEmpty())
                repaint (r);
        };

        const auto before = column (playheadTick);
        playheadTick = tick;

        // Follow: a page further when the playhead leaves the view (off: the view stays where it is)
        const auto grid = gridArea();
        // (Pattern mode loops the pattern: the view holds still at its end and comes back in one jump at the wrap;
        // a take with Loop record off, and Song mode, go on past it: plain paging)
        const auto& followSong = engine.getSong();
        const auto loops = followSong.mode == PlayMode::pattern && engine.patternLinearOrigin() < 0.0 && followSong.currentPattern >= 0
                           && followSong.currentPattern < (int) followSong.patterns.size();
        const auto loopTicks = loops ? (double) followSong.activePattern().effectiveLength (followSong.meter()) : 0.0;
        const auto newStart = transportedit::followViewStartInLoop (scrollTicks, grid.getWidth() / pixelsPerTick, tick, 20.0 / pixelsPerTick,
                                                                    20.0 / pixelsPerTick, loopTicks);

        if (EditorGlobals::get().isFollowing (true) && tick >= 0.0 && newStart != scrollTicks && drag == Drag::none)
        {
            scrollTicks = newStart;
            updateScrollBars();
            repaint();
        }
        else if (const auto after = column (tick); ! before.isEmpty() && ! after.isEmpty() && std::abs (after.getX() - before.getX()) <= 14)
            dirty (before.getUnion (after));
        else
        {
            dirty (before);
            dirty (after);
        }
    }
    // round 177d: the notes held in a take grow with the playhead (only their rows, from their start)
    if (engine.isLiveRecording() && isShowing())
    {
        juce::Rectangle<int> area;
        for (const auto& n : engine.liveRecording().held)
        {
            const auto x0 = (int) tickToX ((double) n.start) - 2;
            const auto x1 = juce::jmax (x0 + 8, (int) tickToX ((double) (n.start + n.length)) + 8);
            area = area.getUnion (juce::Rectangle<int> (x0, keyToY (n.key), x1 - x0, rowHeight + 1));
        }
        const auto dirty = area.getUnion (liveHeldArea).getIntersection (gridArea());
        liveHeldArea = area;
        if (! dirty.isEmpty())
            repaint (dirty);
    }
    else if (! liveHeldArea.isEmpty())
    {
        repaint (liveHeldArea);
        liveHeldArea = {};
    }
    return playing || moved;
}
// --- round 168g: FL's Piano roll File / Edit / View items ----------------------------------------------------------------

void PianoRollComponent::setTimeSelection (std::int64_t from, std::int64_t to)
{
    if (from < 0 || to <= from)
        from = to = -1;
    if (from == timeFrom && to == timeTo)
        return;
    timeFrom = from;
    timeTo = to;
    repaint();
}

void PianoRollComponent::spaceOperation (int kind)
{
    // FL needs a time selection for these (FUN_00eb35f0: nothing without one)
    if (timeFrom < 0 || timeTo <= timeFrom)
    {
        ui::postHint ({ tr ("Select a time range on the ruler first (Ctrl+drag)") }, 2000);
        return;
    }
    std::vector<int> channel;
    const auto& notes = currentPattern (engine.getSong()).notes;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (edits (notes[(std::size_t) i]))
            channel.push_back (i);

    const auto from = timeFrom, to = timeTo;
    const auto& song = engine.getSong();
    const auto end = currentPattern (song).effectiveLength (song.meter());
    const auto who = allChannels ? -1 : engine.getSelectedChannel();
    std::vector<int> moved;
    setSelection ({});
    editNotes ([&] (Pattern& p) {
        if (kind == 0 || kind == 1)
            moved = noteedit::insertSpace (p, channel, from, to, kind == 1);
        else if (kind == 2)
            moved = noteedit::deleteSpace (p, channel, from, to);
        else
            noteedit::trimToSelection (p, who, from, to, end);
        noteedit::sortTracking (p, moved);
    });
    if (kind == 3)
        setTimeSelection (-1, -1); // (FL clears the time selection after a trim)
    knownNoteCount = currentPattern (engine.getSong()).notes.size();
    setSelection (moved); // FL selects the notes that moved (FUN_00e3b1a0)
}

void PianoRollComponent::insertControllerValue()
{
    // FL's Ctrl+I (EEInitAction → FUN_00eb38d0): the event lane's control, its current value, at the start of the
    // time selection (or at the pattern's start)
    const auto target = eventLaneTarget();
    if (! target)
    {
        ui::postHint ({ tr ("Show a control's events in the bottom lane first (its box on the left)") }, 2000);
        return;
    }
    const auto value = engine.getSong().targetValue (*target);
    const auto at = timeFrom >= 0 ? timeFrom : (std::int64_t) 0;
    engine.editSong ([&] (Song& s) { eventedit::setEvent (eventedit::laneFor (currentPattern (s), *target), at, value); });
    repaint (velocityArea());
}

void PianoRollComponent::deleteAllNotes()
{
    // FL's Edit › Delete all (FUN_00e3d230): every note of the edited channel in the pattern, one undo step
    std::vector<int> doomed;
    const auto& notes = currentPattern (engine.getSong()).notes;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (edits (notes[(std::size_t) i]))
            doomed.push_back (i);
    if (doomed.empty())
        return;
    setSelection ({});
    editNotes ([&doomed] (Pattern& p) { noteedit::remove (p, doomed); });
}

bool PianoRollComponent::openScore (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return false;
    const auto score = prtools::parseScore (std::span<const std::uint8_t> (static_cast<const std::uint8_t*> (bytes.getData()), bytes.getSize()));
    if (! score)
        return false;
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return false;
    // FL's score loading (FUN_00ff85c0): the channel's notes in the pattern are replaced by the score's
    const auto loaded = prtools::notesFromScore (*score, channel);
    std::vector<int> added;
    setSelection ({});
    editNotes ([&] (Pattern& p) {
        std::vector<int> old;
        for (int i = 0; i < (int) p.notes.size(); ++i)
            if (p.notes[(std::size_t) i].channel == channel)
                old.push_back (i);
        noteedit::remove (p, old);
        added = noteedit::paste (p, loaded, 0, channel);
        noteedit::sortTracking (p, added);
    });
    knownNoteCount = currentPattern (engine.getSong()).notes.size();
    repaint();
    return true;
}

bool PianoRollComponent::saveScore (const juce::File& file) const
{
    // FL (FUN_0121b1b0): every note of the edited channel; "*.mid" in the same window saves a MIDI file
    const auto& notes = currentPattern (engine.getSong()).notes;
    std::vector<Note> mine;
    for (const auto& n : notes)
        if (n.channel == engine.getSelectedChannel())
            mine.push_back (n);
    if (mine.empty())
        return false;
    if (file.hasFileExtension ("mid;midi"))
    {
        const auto& song = engine.getSong();
        const auto c = engine.getSelectedChannel();
        const auto smf = MidiFileIo::writeNotes (song, mine, c >= 0 && c < (int) song.channels.size() ? song.channels[(std::size_t) c].name : std::string());
        return file.replaceWithData (smf.getData(), smf.getSize());
    }
    const auto bytes = prtools::writeScore (mine);
    return file.replaceWithData (bytes.data(), bytes.size());
}

bool PianoRollComponent::copyToMidiClipboard() const
{
    const auto& song = engine.getSong();
    const auto c = engine.getSelectedChannel();
    std::vector<Note> mine;
    for (const auto& n : currentPattern (song).notes)
        if (n.channel == c)
            mine.push_back (n);
    if (mine.empty())
        return false;
    const auto smf = MidiFileIo::writeNotes (song, mine, c >= 0 && c < (int) song.channels.size() ? song.channels[(std::size_t) c].name : std::string());
    const auto ok = midiclipboard::put (smf);
    ui::postHint ({ ok ? tr ("Copied {0} notes to the MIDI clipboard", { juce::String ((int) mine.size()) }) : tr ("The clipboard is busy: try again") }, 2000);
    return ok;
}

void PianoRollComponent::pasteFromMidiClipboard()
{
    const auto smf = midiclipboard::get();
    if (! smf)
    {
        ui::postHint ({ tr ("There is no MIDI data in the clipboard") }, 2000);
        return;
    }
    juce::String error;
    const auto midi = MidiFileIo::parseBytes (smf->getData(), smf->getSize(), error);
    if (! midi)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", tr ("Error parsing the MIDI data..."));
        return;
    }
    importMidi (*midi, tr ("MIDI from clipboard")); // (FL: the same "Import MIDI data" window, FUN_00e1d090)
}

bool PianoRollComponent::loadFile (const juce::File& file)
{
    if (isScoreFile (file))
        return openScore (file);
    if (isMidiFile (file))
    {
        juce::String error;
        const auto midi = MidiFileIo::parseFile (file, error);
        if (! midi)
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "MIDI", error);
            return true;
        }
        importMidi (*midi, file.getFileName());
        return true;
    }
    if (BrowserComponent::isAudioFile (file))
    {
        loadBackgroundWave (file);
        return true;
    }
    return false;
}

bool PianoRollComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (const juce::File file (f); isScoreFile (file) || isMidiFile (file) || BrowserComponent::isAudioFile (file))
            return true;
    return false;
}

void PianoRollComponent::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (loadFile (juce::File (f)))
            return;
}

bool PianoRollComponent::isInterestedInDragSource (const juce::DragAndDropTarget::SourceDetails& details)
{
    if (! BrowserComponent::isBrowserDrag (details))
        return false;
    const auto file = BrowserComponent::draggedFile (details);
    return isScoreFile (file) || isMidiFile (file) || BrowserComponent::isAudioFile (file);
}

void PianoRollComponent::itemDropped (const juce::DragAndDropTarget::SourceDetails& details)
{
    loadFile (BrowserComponent::draggedFile (details));
}

bool PianoRollComponent::isBackgroundWaveShown() noexcept
{
    return backgroundWaveOn() && backgroundWave() != nullptr;
}

void PianoRollComponent::setBackgroundWaveShown (bool on)
{
    backgroundWaveOn() = on;
    if (on && backgroundWave() == nullptr)
        ui::postHint ({ tr ("Drop an audio file on the Piano roll to see it as the background waveform") }, 2500);
    repaint();
}

void PianoRollComponent::loadBackgroundWave (const juce::File& file)
{
    // peaks (min / max per 256 samples, every channel) read off the message thread
    juce::Component::SafePointer<PianoRollComponent> safe (this);
    juce::Thread::launch ([file, safe] {
        auto wave = std::make_shared<BackgroundWave>();
        wave->file = file;
        juce::AudioFormatManager formats;
        registerAudioFormats (formats);
        if (std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file)); reader != nullptr && reader->lengthInSamples > 0)
        {
            wave->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
            wave->samples = juce::jmin<std::int64_t> (reader->lengthInSamples, (std::int64_t) (wave->sampleRate * 60.0 * 30.0)); // (30 min at most)
            const auto channels = (int) juce::jlimit (1u, 8u, reader->numChannels);
            constexpr int chunk = BackgroundWave::perPeak * 256;
            juce::AudioBuffer<float> buffer (channels, chunk);
            for (std::int64_t pos = 0; pos < wave->samples; pos += chunk)
            {
                const auto n = (int) juce::jmin<std::int64_t> (chunk, wave->samples - pos);
                buffer.clear();
                reader->read (&buffer, 0, n, pos, true, channels > 1);
                for (int b = 0; b < n; b += BackgroundWave::perPeak)
                {
                    auto lo = 0.0f, hi = 0.0f;
                    const auto m = juce::jmin (BackgroundWave::perPeak, n - b);
                    for (int c = 0; c < channels; ++c)
                    {
                        const auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (c, b), m);
                        lo = juce::jmin (lo, range.getStart());
                        hi = juce::jmax (hi, range.getEnd());
                    }
                    wave->lo.push_back (lo);
                    wave->hi.push_back (hi);
                }
            }
        }
        juce::MessageManager::callAsync ([safe, wave] {
            if (wave->lo.empty())
            {
                ui::postHint ({ tr ("This audio file could not be read") }, 2000);
                return;
            }
            backgroundWave() = wave;
            backgroundWaveOn() = true; // FL: loading one turns the view on (FUN_00ead950 (1))
            if (safe != nullptr)
                safe->repaint();
        });
    });
}

void PianoRollComponent::paintBackgroundWave (juce::Graphics& g, juce::Rectangle<int> grid)
{
    const auto wave = backgroundWave();
    if (! backgroundWaveOn() || wave == nullptr || wave->lo.empty())
        return;
    // FL (FUN_00e45a00): from the pattern's start, as long as the audio lasts at the song tempo, across the whole grid
    // height (centred, 90% of the half height)
    const auto& song = engine.getSong();
    const auto ticksPerSecond = juce::jmax (1.0, song.bpm) / 60.0 * ppq;
    const auto ticksPerPeak = BackgroundWave::perPeak / wave->sampleRate * ticksPerSecond;
    const auto centre = (float) grid.getCentreY();
    const auto amplitude = (float) grid.getHeight() * 0.5f * 0.9f;
    const auto x0 = juce::jmax (grid.getX(), (int) std::floor (tickToX (0.0)));
    const auto x1 = juce::jmin (grid.getRight(), (int) std::ceil (tickToX ((double) wave->lo.size() * ticksPerPeak)));
    if (x1 <= x0)
        return;
    juce::RectangleList<float> bars;
    for (int x = x0; x < x1; ++x)
    {
        const auto p0 = (std::int64_t) std::floor (xToTick (x) / ticksPerPeak);
        const auto p1 = juce::jmax (p0 + 1, (std::int64_t) std::floor (xToTick (x + 1) / ticksPerPeak));
        auto lo = 0.0f, hi = 0.0f;
        for (auto p = juce::jmax<std::int64_t> (0, p0); p < juce::jmin<std::int64_t> ((std::int64_t) wave->lo.size(), p1); ++p)
        {
            lo = juce::jmin (lo, wave->lo[(std::size_t) p]);
            hi = juce::jmax (hi, wave->hi[(std::size_t) p]);
        }
        const auto top = centre - juce::jlimit (0.0f, 1.0f, hi) * amplitude, bottom = centre - juce::jlimit (-1.0f, 0.0f, lo) * amplitude;
        bars.addWithoutMerging ({ (float) x, top, 1.0f, juce::jmax (1.0f, bottom - top) });
    }
    g.setColour (theme::colour::textPrimary.withAlpha (0.16f));
    g.fillRectList (bars);
}

juce::PopupMenu PianoRollComponent::fileMenu()
{
    // FL's EEFileMenu (DFM): Open score, Browse scores, Save score as | Import / Export MIDI, Paste from / Copy to MIDI
    // clipboard
    juce::PopupMenu menu;
    menu.addItem (theme::mnemonic (tr ("Open score..."), 'O') + keyText ("pianoRoll.openScore"), [this] { runAction ("pianoRoll.openScore"); });
    menu.addItem (theme::mnemonic (tr ("Browse scores"), 'B') + keyText ("pianoRoll.browseScores"), onBrowseScores != nullptr, false, [this] { runAction ("pianoRoll.browseScores"); });
    menu.addItem (theme::mnemonic (tr ("Save score as..."), 'S') + keyText ("pianoRoll.saveScore"), [this] { runAction ("pianoRoll.saveScore"); });
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Import MIDI file..."), 'I') + keyText ("pianoRoll.importMidi"), [this] { runAction ("pianoRoll.importMidi"); });
    menu.addItem (theme::mnemonic (tr ("Export as MIDI file..."), 'E') + keyText ("pianoRoll.exportMidi"), [this] { runAction ("pianoRoll.exportMidi"); });
    menu.addItem (theme::mnemonic (tr ("Paste from MIDI clipboard..."), 'P') + keyText ("pianoRoll.pasteMidiClipboard"), midiclipboard::hasMidi(), false,
                  [this] { runAction ("pianoRoll.pasteMidiClipboard"); });
    menu.addItem (theme::mnemonic (tr ("Copy to MIDI clipboard"), 'C') + keyText ("pianoRoll.copyMidiClipboard"), [this] { runAction ("pianoRoll.copyMidiClipboard"); });
    return menu;
}

juce::PopupMenu PianoRollComponent::editMenu()
{
    // FL's EEEditMenu, the items that apply to notes (DFM; Shift / Transpose = the arrow keys, round 167j)
    const auto hasTime = timeFrom >= 0 && timeTo > timeFrom;
    const auto grid = gridTicks();
    juce::PopupMenu menu;
    menu.addItem (theme::mnemonic (tr ("Cut"), 't') + "\tCtrl+X", ! selection.empty(), false, [this] { copySelection(); deleteSelection(); });
    menu.addItem (theme::mnemonic (tr ("Copy"), 'C') + "\tCtrl+C", [this] { copySelection(); });
    menu.addItem (tr ("Copy ghost notes"), ! visibleGhostNotes().empty(), false, [this] { copyGhostNotes(); }); // (LYNX, round 177d)
    menu.addItem (theme::mnemonic (tr ("Paste"), 'P') + "\tCtrl+V", ! clipboard.empty(), false, [this] { paste(); });
    menu.addItem (theme::mnemonic (tr ("Duplicate"), 'u') + "\tCtrl+B", [this] { duplicateSelection(); });
    menu.addItem (theme::mnemonic (tr ("Delete"), 'D') + "\tDel", ! selection.empty(), false, [this] { deleteSelection(); });
    menu.addItem (theme::mnemonic (tr ("Delete all"), 'a') + keyText ("pianoRoll.deleteAll"), [this] { runAction ("pianoRoll.deleteAll"); });
    menu.addSeparator();
    menu.addItem (tr ("Shift left") + "\tShift+Left", ! selection.empty(), false, [this, grid] { nudge (-grid, 0); });
    menu.addItem (tr ("Shift right") + "\tShift+Right", ! selection.empty(), false, [this, grid] { nudge (grid, 0); });
    menu.addItem (tr ("Transpose up") + "\tShift+Up", ! selection.empty(), false, [this] { nudge (0, 1); });
    menu.addItem (tr ("Transpose down") + "\tShift+Down", ! selection.empty(), false, [this] { nudge (0, -1); });
    menu.addItem (tr ("Transpose one octave up") + "\tCtrl+Up", ! selection.empty(), false, [this] { nudge (0, 12); });
    menu.addItem (tr ("Transpose one octave down") + "\tCtrl+Down", ! selection.empty(), false, [this] { nudge (0, -12); });
    menu.addSeparator();
    menu.addItem (tr ("Insert space") + keyText ("pianoRoll.insertSpace"), hasTime, false, [this] { runAction ("pianoRoll.insertSpace"); });
    menu.addItem (tr ("Slice & insert space") + keyText ("pianoRoll.sliceInsertSpace"), hasTime, false, [this] { runAction ("pianoRoll.sliceInsertSpace"); });
    menu.addItem (tr ("Delete space") + keyText ("pianoRoll.deleteSpace"), hasTime, false, [this] { runAction ("pianoRoll.deleteSpace"); });
    menu.addItem (tr ("Trim") + keyText ("pianoRoll.trimSpace"), hasTime, false, [this] { runAction ("pianoRoll.trimSpace"); });
    menu.addSeparator();
    menu.addItem (tr ("Insert current controller value") + keyText ("pianoRoll.insertControllerValue"), eventLaneTarget().has_value(), false,
                  [this] { runAction ("pianoRoll.insertControllerValue"); });
    return menu;
}

juce::PopupMenu PianoRollComponent::stampMenu()
{
    // the toolbar chord box's items, the chosen one ticked ("Note" = no stamp: single notes)
    juce::PopupMenu menu;
    for (int i = 0; i < chordBox.getNumItems(); ++i)
    {
        const auto id = chordBox.getItemId (i);
        menu.addItem (chordBox.getItemText (i) + (id == 1 ? keyText ("pianoRoll.stampNone") : juce::String()), true, chordBox.getSelectedId() == id,
                      [this, id] { chordBox.setSelectedId (id, juce::sendNotificationSync); });
    }
    return menu;
}

juce::PopupMenu PianoRollComponent::viewMenu()
{
    // FL's EEViewMenu, its "Helpers" items (DFM)
    juce::PopupMenu menu;
    menu.addItem (tr ("Note grid highlights") + keyText ("pianoRoll.gridHighlights"), true, gridHighlights, [this] { runAction ("pianoRoll.gridHighlights"); });
    menu.addItem (tr ("Show chord name for selection") + keyText ("pianoRoll.showChordName"), true, showChordName,
                  [this] { runAction ("pianoRoll.showChordName"); });
    menu.addItem (tr ("Background waveform") + keyText ("pianoRoll.backgroundWave"), true, isBackgroundWaveShown(),
                  [this] { runAction ("pianoRoll.backgroundWave"); });
    menu.addItem (tr ("Ruler: song bars (LYNX)") + keyText ("pianoRoll.songBarsRuler"), true, songBarsRuler, [this] { runAction ("pianoRoll.songBarsRuler"); });
    {
        // round 190j (discussion #136): flat = LYNX's keyboard so far, the default. (FL 2026's own keyboard has the white
        // keys meeting behind the black ones, with lines only between B / C and E / F: flshots/fl-pr-keys-x3.png.)
        juce::PopupMenu style;
        const auto add = [this, &style] (const juce::String& name, pianokeys::Style value) {
            style.addItem (name, true, keyStyle == value, [this, value] { setKeyStyle (value); });
        };
        add (tr ("Flat keys"), pianokeys::Style::flat);
        add (tr ("Piano keys (lines between the white keys)"), pianokeys::Style::piano);
        menu.addSubMenu (tr ("Keyboard style"), style);
    }
    if (const auto wave = backgroundWave())
        menu.addItem (tr ("Remove the background waveform ({0})", { wave->file.getFileName() }), [this] {
            backgroundWave().reset();
            backgroundWaveOn() = false;
            repaint();
        });
    return menu;
}

// --- the pattern's time markers (round 168g, docs/analise/REA_rodada168g.md §M13) --------------------------------------
juce::PopupMenu PianoRollComponent::markersMenu()
{
    // FL's EEMarkerMenu (the Piano roll shows the Playlist's Time markers items, on the pattern's own markers)
    juce::PopupMenu menu;
    menu.addItem (tr ("Add one...") + keyText ("pianoRoll.markerAdd"), [this] { runAction ("pianoRoll.markerAdd"); });
    menu.addItem (tr ("Add auto") + keyText ("pianoRoll.markerAuto"), [this] { runAction ("pianoRoll.markerAuto"); });
    menu.addItem (tr ("Add time signature change...") + keyText ("pianoRoll.markerSignature"), [this] { runAction ("pianoRoll.markerSignature"); });
    const auto any = ! currentPattern (engine.getSong()).markers.empty();
    menu.addSeparator();
    menu.addItem (tr ("Delete all markers"), any, false, [this] {
        engine.editSong ([] (Song& s) { currentPattern (s).markers.clear(); });
        repaint();
    });
    return menu;
}

std::int64_t PianoRollComponent::markerKeyTick() const
{
    // like the Playlist's (FL FUN_0122b0d0 / FUN_01229d20): the time selection's start; else the playhead when it is
    // in view; else the bar line nearest the view's left edge
    if (timeFrom >= 0 && timeTo > timeFrom)
        return timeFrom;
    const auto left = std::max (0.0, scrollTicks);
    const auto right = xToTick (gridArea().getRight());
    if (playheadTick >= left && playheadTick <= right)
        return (std::int64_t) playheadTick;
    const auto bar = engine.getSong().barTicks();
    const auto at = (std::int64_t) left;
    return at % bar > bar / 2 ? (at / bar + 1) * bar : at / bar * bar;
}

void PianoRollComponent::editPatternMarker (std::int64_t tick, int existing)
{
    const auto& markers = currentPattern (engine.getSong()).markers;
    const auto current = existing >= 0 && existing < (int) markers.size() ? juce::String::fromUTF8 (markers[(std::size_t) existing].name.c_str())
                                                                          : tr ("Marker");
    // FL's "Marker name" prompt
    auto* window = new juce::AlertWindow (existing >= 0 ? tr ("Rename marker") : tr ("New marker"), {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", current);
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PianoRollComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, tick, existing, window] (int result) {
                                 if (result != 1 || safe == nullptr)
                                     return;
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                 safe->engine.editSong ([tick, existing, name] (Song& s) {
                                     auto& p = currentPattern (s);
                                     if (existing < 0)
                                         noteedit::addPatternMarker (p, { tick, name, 0, 4 });
                                     else if (existing < (int) p.markers.size())
                                         p.markers[(std::size_t) existing].name = name;
                                 });
                                 safe->repaint();
                             }),
                             true);
}

void PianoRollComponent::addPatternSignatureByKey()
{
    const auto& song = engine.getSong();
    const auto hasTime = timeFrom >= 0 && timeTo > timeFrom;
    const auto from = hasTime ? timeFrom : snap ((double) markerKeyTick(), true);
    const auto to = hasTime ? timeTo : (std::int64_t) -1;
    const auto current = noteedit::patternSignatureAt (currentPattern (song), from, song.meter());

    auto* window = new juce::AlertWindow (tr ("Add time signature change..."), {}, juce::MessageBoxIconType::NoIcon, this);
    juce::StringArray numerators, denominators;
    for (int n = 1; n <= 32; ++n)
        numerators.add (juce::String (n));
    for (int d : { 1, 2, 4, 8, 16, 32 })
        denominators.add (juce::String (d));
    window->addComboBox ("numerator", numerators, tr ("Numerator"));
    window->addComboBox ("denominator", denominators, tr ("Denominator"));
    window->getComboBoxComponent ("numerator")->setText (juce::String (current.first), juce::dontSendNotification);
    window->getComboBoxComponent ("denominator")->setText (juce::String (current.second), juce::dontSendNotification);
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PianoRollComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, from, to, window] (int result) {
                                 if (result != 1 || safe == nullptr)
                                     return;
                                 const auto n = window->getComboBoxComponent ("numerator")->getText().getIntValue();
                                 const auto d = window->getComboBoxComponent ("denominator")->getText().getIntValue();
                                 safe->engine.editSong ([from, to, n, d] (Song& s) {
                                     noteedit::addPatternSignatureChange (currentPattern (s), from, to, n, d, s.meter());
                                 });
                                 safe->repaint();
                             }),
                             true);
}

int PianoRollComponent::markerAtX (int x) const
{
    // the flag and the name to its right (the last one drawn wins, as it is on top)
    const auto& markers = currentPattern (engine.getSong()).markers;
    for (int i = (int) markers.size() - 1; i >= 0; --i)
    {
        const auto mx = (int) tickToX ((double) markers[(std::size_t) i].tick);
        if (x >= mx - 3 && x <= mx + 60)
            return i;
    }
    return -1;
}

void PianoRollComponent::showPatternMarkerMenu (int index)
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Rename marker..."), [this, index] { editPatternMarker (0, index); });
    menu.addItem (tr ("Delete marker"), [this, index] {
        engine.editSong ([index] (Song& s) {
            auto& markers = currentPattern (s).markers;
            if (index >= 0 && index < (int) markers.size())
                markers.erase (markers.begin() + index);
        });
        repaint();
    });
    menu.addSeparator();
    menu.addSubMenu (tr ("Time markers"), markersMenu());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}
} // namespace lynx
