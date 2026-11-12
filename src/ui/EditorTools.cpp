#include "ui/FlMods.h"
#include "ui/EditorTools.h"

#include "core/NoteEdit.h"
#include "core/NoteTools.h"
#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
// --- edit tools ------------------------------------------------------------------------

const std::vector<EditToolInfo>& editTools()
{
    static const std::vector<EditToolInfo> list {
        { EditTool::pencil, Icon::draw, TR_NOOP ("Pencil"), TR_NOOP ("Click adds, drag moves, drag an edge resizes"), 'P' },
        { EditTool::brush, Icon::brush, TR_NOOP ("Brush"), TR_NOOP ("Drag to paint a run of notes or clips"), 'B' },
        { EditTool::drumPaint, Icon::drumBrush, TR_NOOP ("Drum brush"),
          TR_NOOP ("Drag to paint one short hit per grid cell, on whatever key is under the mouse"), 'N' },
        { EditTool::eraser, Icon::erase, TR_NOOP ("Eraser"), TR_NOOP ("Click or drag to delete"), 'D' },
        { EditTool::mute, Icon::mute, TR_NOOP ("Mute"), TR_NOOP ("Click or drag to mute or unmute"), 'T' },
        { EditTool::knife, Icon::knife, TR_NOOP ("Knife"), TR_NOOP ("Drag a line to cut; a click cuts at the cursor"), 'C' },
        { EditTool::lasso, Icon::select, TR_NOOP ("Lasso"), TR_NOOP ("Click or drag to select (Shift adds)"), 'E' },
        { EditTool::zoom, Icon::zoom, TR_NOOP ("Zoom"), TR_NOOP ("Click zooms in, drag a range to zoom to it; right-click zooms out"), 'Z' },
        { EditTool::listen, Icon::listen, TR_NOOP ("Listen"), TR_NOOP ("Click or drag to hear what is under the cursor"), 'Y' },
        { EditTool::slip, Icon::slip, TR_NOOP ("Slip"), TR_NOOP ("Drag inside a clip to move its content under it"), 'S' },
    };
    return list;
}

const char* editToolId (EditTool tool)
{
    switch (tool)
    {
        case EditTool::pencil: return "pencil";
        case EditTool::brush: return "brush";
        case EditTool::drumPaint: return "drumPaint";
        case EditTool::eraser: return "eraser";
        case EditTool::mute: return "mute";
        case EditTool::knife: return "knife";
        case EditTool::lasso: return "lasso";
        case EditTool::zoom: return "zoom";
        case EditTool::listen: return "listen";
        case EditTool::slip: return "slip";
    }
    return "pencil";
}

const EditToolInfo& editToolInfo (EditTool tool)
{
    for (const auto& t : editTools())
        if (t.tool == tool)
            return t;

    return editTools().front();
}

namespace
{
void setKeyTip (IconButton& button, const EditToolInfo& info, const juce::String& key)
{
    button.setTooltip (tr (info.name) + (key.isNotEmpty() ? " (" + key + ")" : juce::String()) + "\n" + tr (info.description) + "\n"
                       + tr ("Every tool: right-click deletes, Alt ignores the grid, Ctrl+drag selects"));
    ui::setQuickTip (button, tr (info.name), key);
}
} // namespace

namespace
{
// Round 170e3: the tools in three groups, each in its own well (FL users look for them the same way): drawing
// (pencil, brushes), changing (delete, mute, cut, slip) and looking (select, zoom, listen).
int toolGroup (EditTool t)
{
    switch (t)
    {
        case EditTool::pencil:
        case EditTool::brush:
        case EditTool::drumPaint: return 0;
        case EditTool::eraser:
        case EditTool::mute:
        case EditTool::knife:
        case EditTool::slip: return 1;
        default: return 2;
    }
}
constexpr int toolGroupGap = 6;
} // namespace

void ToolStrip::refreshKeys (const std::function<juce::String (EditTool)>& keyOf)
{
    for (std::size_t i = 0; i < tools.size() && i < buttons.size(); ++i)
        setKeyTip (*buttons[i], editToolInfo (tools[i]), keyOf (tools[i]));
}

ToolStrip::ToolStrip (bool includeSlip)
{
    for (int group = 0; group < 3; ++group)
    for (const auto& info : editTools())
    {
        // the Playlist has Slip, the Piano Roll the drum brush
        if ((info.tool == EditTool::slip && ! includeSlip) || (info.tool == EditTool::drumPaint && includeSlip))
            continue;
        if (toolGroup (info.tool) != group) // drawing, changing, looking (round 170e3)
            continue;

        auto button = std::make_unique<IconButton> (info.name, info.icon);
        button->setRadioGroupId (0x7001);
        button->setClickingTogglesState (true);
        button->setFlat (true);
        button->setWantsKeyboardFocus (false);
        button->setMouseClickGrabsKeyboardFocus (false);
        button->setOnColour (theme::colour::accent.withAlpha (0.3f)); // round 170e3: the tool in use reads at once
        button->setIconColourWhenOn (theme::colour::accentHover);
        setKeyTip (*button, info, juce::String::charToString ((juce::juce_wchar) info.key));
        button->setToggleState (info.tool == tool, juce::dontSendNotification);
        button->onClick = [this, t = info.tool] {
            if (tool != t)
            {
                tool = t;
                if (onChange)
                    onChange (t);
            }
        };
        addAndMakeVisible (*button);
        buttons.push_back (std::move (button));
        tools.push_back (info.tool);
    }
}

void ToolStrip::setTool (EditTool newTool)
{
    for (std::size_t i = 0; i < tools.size(); ++i)
        if (tools[i] == newTool)
        {
            tool = newTool;
            buttons[i]->setToggleState (true, juce::dontSendNotification);

            for (std::size_t j = 0; j < buttons.size(); ++j)
                if (j != i)
                    buttons[j]->setToggleState (false, juce::dontSendNotification);

            if (onChange)
                onChange (tool);
            return;
        }
}


int ToolStrip::getIdealWidth (int height) const
{
    int groups = 0;
    for (std::size_t i = 0; i < tools.size(); ++i)
        groups += (i == 0 || toolGroup (tools[i]) != toolGroup (tools[i - 1])) ? 1 : 0;
    return (int) buttons.size() * toolbarlayout::toolButtonWidth (iconSize, height) + juce::jmax (0, groups - 1) * toolGroupGap;
}

void ToolStrip::resized()
{
    const auto size = getHeight();
    const auto width = toolbarlayout::toolButtonWidth (iconSize, size);
    const auto icon = toolbarlayout::toolIconPx (iconSize);

    wells.clear();
    int x = 0;
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        if (i > 0 && toolGroup (tools[i]) != toolGroup (tools[i - 1]))
            x += toolGroupGap;
        // (narrower than its ideal width: a button that does not fit whole goes)
        const auto fits = x + width <= getWidth();
        if (fits && (i == 0 || toolGroup (tools[i]) != toolGroup (tools[i - 1])))
            wells.push_back ({ x, 0, 0, size });
        buttons[i]->setIconSizing (1.0f, icon);
        buttons[i]->setBounds (x, 0, width, size);
        buttons[i]->setVisible (fits);
        x += width;
        if (fits)
            wells.back().setRight (x);
    }
}

void ToolStrip::paint (juce::Graphics& g)
{
    if (buttons.empty())
        return;
    for (const auto& r : wells)
    {
        const auto well = r.toFloat().reduced (0.5f);
        g.setColour (theme::colour::bg1.withAlpha (0.55f));
        g.fillRoundedRectangle (well, theme::radius::md);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (well, theme::radius::md, 1.0f);
    }
}

void ToolStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (wheel.deltaY == 0.0f || tools.empty())
        return;

    const auto at = (int) (std::find (tools.begin(), tools.end(), tool) - tools.begin());
    const auto next = juce::jlimit (0, (int) tools.size() - 1, at + (wheel.deltaY < 0.0f ? 1 : -1));
    setTool (tools[(std::size_t) next]);
}

// --- cursors ---------------------------------------------------------------------------

namespace
{
// A tool icon as a cursor: drawn at 2x, light strokes with a dark outline so it reads on the dark grid
// and on bright notes alike. `hot` is the hotspot on the icon's 24-unit grid.
juce::MouseCursor iconCursor (Icon icon, juce::Point<float> hot)
{
    constexpr int size = 24, scale = 2;
    juce::Image image (juce::Image::ARGB, size * scale, size * scale, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale ((float) scale));
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size);

        for (auto [dx, dy] : { std::pair { -1.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f },
                               { -0.7f, -0.7f }, { 0.7f, 0.7f }, { -0.7f, 0.7f }, { 0.7f, -0.7f } })
            icons::draw (g, icon, area.translated (dx, dy), theme::colour::bg0);

        icons::draw (g, icon, area, theme::colour::textPrimary);
    }

    return juce::MouseCursor (juce::ScaledImage (image, (double) scale), hot.roundToInt());
}
} // namespace

juce::MouseCursor editorCursor (gesture::Cursor cursor)
{
    using C = gesture::Cursor;
    // icons are drawn with a margin (fitted into the 24x24 box), so the hotspots are the icon's tips
    static const auto pencil = iconCursor (Icon::draw, { 4.0f, 20.0f });
    static const auto brush = iconCursor (Icon::brush, { 4.0f, 20.0f });
    static const auto eraser = iconCursor (Icon::erase, { 6.0f, 18.0f });
    static const auto mute = iconCursor (Icon::mute, { 12.0f, 12.0f });
    static const auto knife = iconCursor (Icon::knife, { 4.0f, 20.0f });
    static const auto zoom = iconCursor (Icon::zoom, { 10.0f, 10.0f });
    static const auto listen = iconCursor (Icon::listen, { 12.0f, 12.0f });
    static const auto slip = iconCursor (Icon::slip, { 12.0f, 12.0f });

    switch (cursor)
    {
        case C::draw: return pencil;
        case C::paint: return brush;
        case C::erase: return eraser;
        case C::mute: return mute;
        case C::slice: return knife;
        case C::zoom: return zoom;
        case C::listen: return listen;
        case C::slip: return slip;
        case C::move: return juce::MouseCursor::UpDownLeftRightResizeCursor;
        case C::copy: return juce::MouseCursor::CopyingCursor;
        case C::resizeH: return juce::MouseCursor::LeftRightResizeCursor;
        case C::resizeV: return juce::MouseCursor::UpDownResizeCursor;
        case C::select: return juce::MouseCursor::CrosshairCursor;
        case C::point: return juce::MouseCursor::PointingHandCursor;
        case C::pan: return juce::MouseCursor::DraggingHandCursor;
        case C::normal: break;
    }

    return juce::MouseCursor::NormalCursor;
}

juce::String actionHint (gesture::Action action, bool playlist)
{
    using A = gesture::Action;
    const auto common = tr ("[RMB] delete  [Ctrl][LMB] select  [Alt] no snap");

    switch (action)
    {
        case A::create:
            return (playlist ? tr ("[LMB] place the Picker's clip, drag to position it")
                             : tr ("[LMB] add a note, drag to position it"))
                   + "  " + common;
        case A::paint:
            return (playlist ? tr ("[LMB] drag paints clips end to end") : tr ("[LMB] drag paints a run of notes  [Shift] one at a time"))
                   + "  " + common;
        case A::move:
            return (playlist ? tr ("Clip: [LMB] drag moves  [Shift][LMB] copies  [2xLMB] opens  [RMB] deletes")
                             : tr ("Note: [LMB] drag moves  [Shift][LMB] copies  [2xLMB] properties  [RMB] deletes  [Alt][Wheel] velocity"));
        case A::copyMove: return playlist ? tr ("[Shift][LMB] drags a copy of the clip (selection)") : tr ("[Shift][LMB] drags a copy of the note (selection)");
        case A::resizeRight:
            return (playlist ? tr ("Clip end: [LMB] drag resizes") : tr ("Note end: [LMB] drag resizes")) + "  " + tr ("[Alt] no snap");
        case A::resizeLeft:
            return (playlist ? tr ("Clip start: [LMB] drag trims, the content stays in place") : tr ("Note start: [LMB] drag resizes, the end stays"))
                   + "  " + tr ("[Alt] no snap");
        case A::slipContent: return tr ("[LMB] drag slides the clip's content inside it (the clip stays)");
        case A::toggleSelect: return tr ("[Ctrl][LMB] adds to / removes from the selection");
        case A::boxSelect: return tr ("[LMB] drag draws a selection box  [Shift] adds");
        case A::erase: return tr ("[LMB] click or drag deletes");
        case A::toggleMute: return tr ("[LMB] click or drag mutes / unmutes");
        case A::slice: return tr ("[LMB] drag a line to cut across, a click cuts here") + "  " + tr ("[Alt] no snap");
        case A::zoomRange: return tr ("[LMB] click zooms in, drag a range to zoom to it  [RMB] zooms out");
        case A::listen: return playlist ? tr ("[LMB] click or drag plays from here") : tr ("[LMB] click or drag plays the notes under the cursor");
        case A::fade: return tr ("Fade: [LMB] drag sets its length");
        case A::clipGain: return tr ("Clip gain: [LMB] drag up / down (the selected clips together)  [RMB] menu");
        case A::movePoint: return tr ("Point: [LMB] drag moves, then [Shift] keeps time, [Ctrl] keeps value  [Alt] no snap  [RMB] menu");
        case A::addPoint: return tr ("Curve: [LMB] or [RMB] adds a point (hold and drag to place it)");
        case A::bendSegment: return tr ("Tension: [LMB] drag up / down bends the segment ([Ctrl] fine)  [RMB] straight");
        case A::openMenu: return tr ("[LMB] clip menu");
        case A::none: break;
    }

    return {};
}

// --- grid --------------------------------------------------------------------------------

namespace grid
{
const std::vector<Option>& options()
{
    static const std::vector<Option> list {
        { Kind::none, 1, TR_NOOP ("No snap") },
        { Kind::screen, 0, TR_NOOP ("Line (follows zoom)") },
        { Kind::nearest, 0, TR_NOOP ("Events (edges)") },
        { Kind::ticks, ticksPerStep / 6, TR_NOOP ("1/6 step") },
        { Kind::ticks, ticksPerStep / 4, TR_NOOP ("1/4 step") },
        { Kind::ticks, ticksPerStep / 3, TR_NOOP ("1/3 step") },
        { Kind::ticks, ticksPerStep / 2, TR_NOOP ("1/2 step") },
        { Kind::ticks, ticksPerStep, TR_NOOP ("Step") },
        { Kind::ticks, ppq / 3, TR_NOOP ("1/3 beat") },
        { Kind::ticks, ppq / 2, TR_NOOP ("1/2 beat") },
        { Kind::ticks, ppq, TR_NOOP ("Beat") },
        { Kind::bar, 0, TR_NOOP ("Bar") },
    };
    return list;
}

juce::String mainText()
{
    const auto& option = options()[(std::size_t) juce::jlimit (0, (int) options().size() - 1, EditorGlobals::get().getGridOption())];
    return tr ("Main: {0}", { tr (option.name).upToFirstOccurrenceOf (" (", false, false) });
}

void fillEditorBox (juce::ComboBox& box)
{
    box.clear (juce::dontSendNotification);
    // the snap in the toolbar it follows, by name ("Main: Line"; round 170e: "Main (toolbar snap)" did not say
    // what it snaps to, and Busy Works could not tell it was a snap at all)
    box.addItem (mainText(), followId);
    box.addSeparator();

    for (int i = 0; i < (int) options().size(); ++i)
        box.addItem (tr (options()[(std::size_t) i].name), i + 2);

    box.setSelectedId (followId, juce::dontSendNotification);
}

void fillGlobalBox (juce::ComboBox& box)
{
    box.clear (juce::dontSendNotification);

    for (int i = 0; i < (int) options().size(); ++i)
        box.addItem (tr (options()[(std::size_t) i].name), i + 1);

    box.setSelectedId (EditorGlobals::get().getGridOption() + 1, juce::dontSendNotification);
}

const Option& resolve (const juce::ComboBox& editorBox)
{
    const auto id = editorBox.getSelectedId();
    const auto index = id <= followId ? EditorGlobals::get().getGridOption() : id - 2;
    return options()[(std::size_t) juce::jlimit (0, (int) options().size() - 1, index)];
}

juce::String describe (const Option& option)
{
    switch (option.kind)
    {
        case Kind::none: return tr ("free: notes and clips move by single ticks");
        case Kind::screen: return tr ("snaps to the grid lines drawn at this zoom (zoom in for a finer grid)");
        case Kind::nearest: return tr ("snaps to the nearest start or end of another note or clip (ghost notes too); elsewhere to the lines");
        case Kind::bar: return tr ("snaps to whole bars");
        case Kind::ticks: break;
    }
    return tr ("a fixed grid of {0}", { tr (option.name) });
}
} // namespace grid

// --- icon drop-down (round 170e) ---------------------------------------------------------------------

void IconComboBox::setIcon (Icon newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        resized();
        repaint();
    }
}

juce::Rectangle<float> IconComboBox::iconArea() const
{
    if (icon == Icon::none)
        return {};
    const auto size = juce::jlimit (12.0f, 16.0f, (float) getHeight() - 8.0f);
    return { 7.0f, ((float) getHeight() - size) * 0.5f, size, size };
}

void IconComboBox::paint (juce::Graphics& g)
{
    juce::ComboBox::paint (g);
    if (icon == Icon::none)
        return;
    const auto lit = iconLit != nullptr && iconLit();
    const auto colour = ! isEnabled() ? theme::colour::textDisabled : lit ? theme::colour::accent : theme::colour::textSecondary;
    icons::draw (g, icon, iconArea(), colour);
}

void IconComboBox::resized()
{
    juce::ComboBox::resized();
    if (icon == Icon::none)
        return;
    // the text starts after the icon
    for (auto* child : getChildren())
        if (auto* label = dynamic_cast<juce::Label*> (child))
        {
            const auto right = juce::roundToInt (iconArea().getRight()) + 1;
            if (label->getX() < right)
                label->setBounds (label->getBounds().withLeft (right));
        }
}

SnapBox::SnapBox (bool isEditor) : IconComboBox (isEditor ? Icon::magnet : Icon::none), editor (isEditor)
{
    setScrollWheelEnabled (true);
    if (editor)
    {
        // FL's editor snap is a magnet button (TEventEditForm.SnapBtn, "Snap to grid"): the magnet, lit while it snaps
        iconLit = [this] { return getSelectedId() > 0 && grid::resolve (*this).kind != grid::Kind::none; };
        watch.owner = this;
        EditorGlobals::get().addChangeListener (&watch);
    }
}

SnapBox::~SnapBox()
{
    if (editor)
        EditorGlobals::get().removeChangeListener (&watch);
}

void SnapBox::refreshMainText()
{
    if (! editor || getNumItems() == 0)
        return;
    changeItemText (grid::followId, grid::mainText());
    if (getSelectedId() == grid::followId) // the label shows the new name
        setSelectedId (grid::followId, juce::dontSendNotification);
    repaint();
}

ui::Hint SnapBox::hintAt (juce::Point<int>)
{
    ui::Hint h;
    h.text = getProperties().contains ("lynxHint") ? getProperties()["lynxHint"].toString() : getTooltip();
    h.hasMenu = true;

    if (getSelectedId() <= 0)
        return h;

    const auto followsMain = editor && getSelectedId() == grid::followId;
    const auto& option = followsMain ? grid::options()[(std::size_t) juce::jlimit (0, (int) grid::options().size() - 1,
                                                                                    EditorGlobals::get().getGridOption())]
                                     : editor ? grid::resolve (*this)
                                              : grid::options()[(std::size_t) juce::jlimit (0, (int) grid::options().size() - 1, getSelectedId() - 1)];
    const auto what = followsMain ? tr ("Main: follows the toolbar snap, now {0}", { tr (option.name) }) + " - " + grid::describe (option)
                                  : tr (option.name) + ": " + grid::describe (option);
    h.text = h.text.isEmpty() ? what : what + "  |  " + h.text;
    h.value = tr (option.name).upToFirstOccurrenceOf (" (", false, false);
    return h;
}

EditorGlobals& EditorGlobals::get()
{
    static EditorGlobals globals;
    return globals;
}

void EditorGlobals::setGridOption (int option)
{
    option = juce::jlimit (0, (int) grid::options().size() - 1, option);

    if (option != gridOption)
    {
        if (gridOption != grid::noneOption)
            lastGridOption = gridOption;
        gridOption = option;
        sendChangeMessage();
    }
}

void EditorGlobals::toggleGrid()
{
    setGridOption (gridOption == grid::noneOption ? lastGridOption : grid::noneOption);
    // FL does the same (Backspace in the Playlist toggles the toolbar snap to "(none)", r168s f05), but the only sign is
    // the toolbar box: issue #32 #3 read it as "snap stops working until relaunch". Say it in the hint bar too.
    ui::Hint h;
    h.text = gridOption == grid::noneOption ? tr ("Snap off: notes and clips move freely. [Backspace] turns snap back on")
                                            : tr ("Snap on: {0}. [Backspace] turns it off", { tr (grid::options()[(std::size_t) gridOption].name) });
    ui::postHint (h, 3000);
}

void EditorGlobals::setDefaultNoteLength (std::int64_t ticks)
{
    if (ticks != defaultNoteLength)
    {
        defaultNoteLength = juce::jmax<std::int64_t> (0, ticks);
        sendChangeMessage();
    }
}

void EditorGlobals::setFollowing (bool pianoRoll, bool on, bool save)
{
    auto& flag = pianoRoll ? followPianoRoll : followPlaylist;

    if (flag != on)
    {
        flag = on;
        sendChangeMessage();
        if (save && onOptionsChanged)
            onOptionsChanged();
    }
}

void EditorGlobals::setFollowingView (FollowView v, bool on, bool save)
{
    if (v == FollowView::playlist || v == FollowView::pianoRoll)
        return setFollowing (v == FollowView::pianoRoll, on, save);
    auto& flag = v == FollowView::channelRack ? followRack : followAudioEditor;
    if (flag != on)
    {
        flag = on;
        sendChangeMessage();
        if (save && onOptionsChanged)
            onOptionsChanged();
    }
}

GlobalGridBox::GlobalGridBox() : SnapBox (false)
{
    grid::fillGlobalBox (*this);
    setTooltip (tr ("Global grid: used by the Piano Roll and Playlist when their grid follows it. Backspace turns it off and on; "
                    "right-click sets the length of new notes"));
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    onChange = [this] {
        if (getSelectedId() > 0)
            EditorGlobals::get().setGridOption (getSelectedId() - 1);
    };
    EditorGlobals::get().addChangeListener (this);
}

GlobalGridBox::~GlobalGridBox()
{
    EditorGlobals::get().removeChangeListener (this);
}

void GlobalGridBox::mouseDown (const juce::MouseEvent& e)
{
    if (flmods::popup (e.mods))
    {
        showDefaultLengthMenu (this);
        return;
    }

    juce::ComboBox::mouseDown (e);
}

void GlobalGridBox::showDefaultLengthMenu (juce::Component* target)
{
    struct Length
    {
        const char* name;
        std::int64_t ticks;
    };
    static const Length lengths[] = {
        { TR_NOOP ("Last clicked note"), 0 }, { TR_NOOP ("1/4 step"), ticksPerStep / 4 }, { TR_NOOP ("1/2 step"), ticksPerStep / 2 },
        { TR_NOOP ("Step"), ticksPerStep },   { TR_NOOP ("1/2 beat"), ppq / 2 },          { TR_NOOP ("Beat"), ppq },
        { TR_NOOP ("2 beats"), 2 * ppq },     { TR_NOOP ("Bar"), 4 * ppq },
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Default note length"));
    const auto current = EditorGlobals::get().getDefaultNoteLength();

    for (const auto& l : lengths)
        menu.addItem (tr (l.name), true, current == l.ticks, [ticks = l.ticks] { EditorGlobals::get().setDefaultNoteLength (ticks); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void GlobalGridBox::changeListenerCallback (juce::ChangeBroadcaster*)
{
    setSelectedId (EditorGlobals::get().getGridOption() + 1, juce::dontSendNotification);
}

// --- overview strip ------------------------------------------------------------------------

OverviewBar::OverviewBar()
{
    setOpaque (true);
    ui::setHint (*this, tr ("Overview: [LMB] drag the window to scroll, drag its edges to zoom, click elsewhere to jump there  "
                            "[2xLMB] show everything"));
}

void OverviewBar::setRange (double newTotal, double newStart, double newLength)
{
    newTotal = juce::jmax (1.0, newTotal);

    if (newTotal != total || newStart != start || newLength != length)
    {
        total = newTotal;
        start = newStart;
        length = newLength;
        repaint();
    }
}

juce::Rectangle<float> OverviewBar::windowArea() const
{
    const auto w = (double) getWidth();
    const auto x0 = (float) (start / total * w), x1 = (float) ((start + length) / total * w);
    return { x0, 0.0f, juce::jmax (3.0f, x1 - x0), (float) getHeight() - 1.0f };
}

gesture::OverviewGrab OverviewBar::grabAt (int x) const
{
    return gesture::overviewGrabAt (x, getWidth(), total, { start, length });
}

void OverviewBar::updateCursor (gesture::OverviewGrab g)
{
    using G = gesture::OverviewGrab;
    setMouseCursor (g == G::left || g == G::right ? juce::MouseCursor::LeftRightResizeCursor
                    : g == G::move               ? juce::MouseCursor::DraggingHandCursor
                                                 : juce::MouseCursor::PointingHandCursor);

    if (g != hoverGrab)
    {
        hoverGrab = g;
        repaint();
    }
}

void OverviewBar::paint (juce::Graphics& g)
{
    using namespace theme::colour;
    const auto area = getLocalBounds().toFloat();
    g.fillAll (bg0);

    if (paintContent)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds().withTrimmedBottom (1));
        paintContent (g, area.withTrimmedBottom (1.0f), total);
    }

    // outside the window: dimmed; the window: a light frame, its edges lit while hovered or dragged
    const auto win = windowArea();
    g.setColour (bg0.withAlpha (0.5f));
    g.fillRect (area.withRight (win.getX()));
    g.fillRect (area.withLeft (win.getRight()));
    g.setColour (textPrimary.withAlpha (hoverGrab == gesture::OverviewGrab::move || dragging ? 0.08f : 0.04f));
    g.fillRect (win);
    g.setColour (textSecondary.withAlpha (0.55f));
    g.drawRect (win, 1.0f);

    const auto lit = dragging ? grab : hoverGrab;
    g.setColour (accent);
    if (lit == gesture::OverviewGrab::left)
        g.fillRect (win.withWidth (2.0f));
    if (lit == gesture::OverviewGrab::right)
        g.fillRect (win.withLeft (win.getRight() - 2.0f));

    g.setColour (border);
    g.fillRect (area.withTop (area.getBottom() - 1.0f));
}

void OverviewBar::mouseMove (const juce::MouseEvent& e)
{
    updateCursor (grabAt (e.x));
}

void OverviewBar::mouseExit (const juce::MouseEvent&)
{
    if (! dragging && hoverGrab != gesture::OverviewGrab::none)
    {
        hoverGrab = gesture::OverviewGrab::none;
        repaint();
    }
}

void OverviewBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown())
        return;

    grab = grabAt (e.x);
    grabFrom = { start, length };
    dragging = true;

    if (grab == gesture::OverviewGrab::none) // jump: centre the window on the click, then drag it
    {
        grabFrom = gesture::overviewCentredOn (e.x / juce::jmax (1.0, (double) getWidth()) * total, grabFrom, total);
        if (onChange)
            onChange (grabFrom.start, grabFrom.length);
    }

    repaint();
}

void OverviewBar::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto w = gesture::overviewDrag (grab, grabFrom, e.getDistanceFromDragStartX(), getWidth(), total, minLength);

    if (onChange)
        onChange (w.start, w.length);
}

void OverviewBar::mouseUp (const juce::MouseEvent& e)
{
    dragging = false;
    grab = gesture::OverviewGrab::none;
    updateCursor (grabAt (e.x));
    repaint();
}

void OverviewBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onShowAll)
        onShowAll();
}

// --- Note inspector ------------------------------------------------------------------------

namespace
{
class NoteInspector final : public juce::Component
{
public:
    NoteInspector (AudioEngine& e, std::vector<int> i, int f)
        : engine (e), indices (std::move (i)), focus (f)
    {
        const auto& song = engine.getSong();
        patternIndex = song.currentPattern;
        const auto& notes = song.patterns[(std::size_t) patternIndex].notes;

        for (auto index : indices)
            originals.push_back (notes[(std::size_t) index]);

        if (std::find (indices.begin(), indices.end(), focus) == indices.end())
            focus = indices.front();

        shown = notes[(std::size_t) focus];

        title.setText (indices.size() > 1 ? tr ("Note inspector: {0} notes", { juce::String ((int) indices.size()) }) : tr ("Note inspector"),
                       juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);

        addRow (velocity, TR_NOOP ("Velocity"), 0.0, 100.0, "%");
        addRow (pan, TR_NOOP ("Pan"), -100.0, 100.0, "");
        addRow (release, TR_NOOP ("Release"), 0.0, 100.0, "%");
        addRow (finePitch, TR_NOOP ("Fine pitch"), -120.0, 120.0, " ct");
        addRow (modX, TR_NOOP ("Mod X"), 0.0, 100.0, "%");
        addRow (modY, TR_NOOP ("Mod Y"), 0.0, 100.0, "%");
        // FL's Note properties also have the colour group (its "Color group / MIDI channel" button, 1..16; round 168g)
        addRow (colour, TR_NOOP ("Colour group"), 1.0, (double) noteColourCount, "");
        // Live 12's note Chance and Velocity Range (round 187c, LYNX additions)
        addRow (chance, TR_NOOP ("Chance"), 0.0, 100.0, "%");
        addRow (velocityRange, TR_NOOP ("Vel. range"), -100.0, 100.0, "%");
        chance.slider.setTooltip (tr ("How likely the note is to play each time it comes round (LYNX; Live's note Chance)"));
        velocityRange.slider.setTooltip (tr ("Each time it plays, the velocity is drawn between the note's velocity and velocity + this range (LYNX)"));
        release.slider.setTooltip (tr ("Release velocity: sent with the note's end to the instruments that use it (plugins, Patcher)"));
        modX.slider.setTooltip (tr ("Free modulation value (kept with the note; not routed yet)"));
        modY.slider.setTooltip (modX.slider.getTooltip());

        for (auto* t : { &slide, &porta })
        {
            t->setColour (juce::ToggleButton::tickColourId, theme::colour::accent);
            t->onClick = [this, t] { (t == &slide ? slideTouched : portaTouched) = true; };
            addAndMakeVisible (*t);
        }
        slide.setButtonText (tr ("Slide"));
        porta.setButtonText (tr ("Porta"));

        single = indices.size() == 1;
        for (auto* row : { &time, &length })
        {
            row->label.setColour (juce::Label::textColourId, theme::colour::textSecondary);
            row->label.setFont (theme::font (theme::type::small));
            row->editor.setFont (theme::monoFont (theme::type::body));
            row->editor.setJustification (juce::Justification::centredLeft);
            row->editor.onTextChange = [row] { row->touched = true; };
            addChildComponent (row->label);
            addChildComponent (row->editor);
            row->label.setVisible (single);
            row->editor.setVisible (single);
        }
        time.label.setText (tr ("Time (bar:step:tick)"), juce::dontSendNotification);
        length.label.setText (tr ("Length (bars:steps:ticks)"), juce::dontSendNotification);

        applyButton.setButtonText (tr ("Apply"));
        revertButton.setButtonText (tr ("Revert"));
        applyButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        applyButton.onClick = [this] { apply(); };
        revertButton.onClick = [this] { load(); };
        // FL's Reset (TPRNotePropForm ResetBtn): the levels back to their defaults (FL's wheel defaults VEL 100/128,
        // PAN / REL 64, MOD 128, PITCH 120 = LYNX's note defaults)
        resetButton.setButtonText (tr ("Reset"));
        resetButton.onClick = [this] { resetLevels(); };
        addAndMakeVisible (applyButton);
        addAndMakeVisible (revertButton);
        addAndMakeVisible (resetButton);

        load();
        setSize (320, 34 + 9 * rowHeight + rowHeight + (single ? 2 * rowHeight : 0) + 44);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg3);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 8);
        title.setBounds (area.removeFromTop (26));

        for (auto* row : { &velocity, &pan, &release, &finePitch, &modX, &modY, &colour, &chance, &velocityRange })
        {
            auto r = area.removeFromTop (rowHeight);
            row->label.setBounds (r.removeFromLeft (80));
            row->slider.setBounds (r);
        }

        auto toggles = area.removeFromTop (rowHeight);
        slide.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
        porta.setBounds (toggles);

        if (single)
            for (auto* row : { &time, &length })
            {
                auto r = area.removeFromTop (rowHeight);
                row->label.setBounds (r.removeFromLeft (160));
                row->editor.setBounds (r.reduced (0, 2));
            }

        auto buttons = area.removeFromBottom (30);
        applyButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        revertButton.setBounds (buttons.removeFromRight (90));
        resetButton.setBounds (buttons.removeFromLeft (80));
    }

    void resetLevels()
    {
        const Note defaults;
        const auto set = [] (SliderRow& row, double v) {
            row.slider.setValue (v, juce::dontSendNotification);
            row.touched = true;
        };
        set (velocity, std::round (defaults.velocity * 100.0f));
        set (pan, 0.0);
        set (release, std::round (defaults.release * 100.0f));
        set (finePitch, 0.0);
        set (modX, std::round (defaults.modX * 100.0f));
        set (modY, std::round (defaults.modY * 100.0f));
        set (chance, 100.0);
        set (velocityRange, 0.0);
    }

private:
    static constexpr int rowHeight = 26;

    struct SliderRow
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        bool touched = false;
    };

    struct TextRow
    {
        juce::Label label;
        juce::TextEditor editor;
        bool touched = false;
    };

    void addRow (SliderRow& row, const char* name, double min, double max, const juce::String& suffix)
    {
        row.label.setText (tr (name), juce::dontSendNotification);
        row.label.setFont (theme::font (theme::type::small));
        row.label.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        row.slider.setRange (min, max, 1.0);
        row.slider.setTextValueSuffix (suffix);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        row.slider.setColour (juce::Slider::trackColourId, theme::colour::accent);
        row.slider.onValueChange = [&row] { row.touched = true; };
        addAndMakeVisible (row.label);
        addAndMakeVisible (row.slider);
    }

    void load()
    {
        const auto set = [] (SliderRow& row, double v) {
            row.slider.setValue (v, juce::dontSendNotification);
            row.touched = false;
        };
        set (velocity, std::round (shown.velocity * 100.0f));
        set (pan, std::round (shown.pan * 100.0f));
        set (release, std::round (shown.release * 100.0f));
        set (finePitch, std::round (shown.finePitch));
        set (modX, std::round (shown.modX * 100.0f));
        set (modY, std::round (shown.modY * 100.0f));
        set (colour, (double) (shown.colour + 1));
        set (chance, std::round (shown.chance * 100.0f));
        set (velocityRange, std::round (shown.velocityRange * 100.0f));
        slide.setToggleState (shown.slide, juce::dontSendNotification);
        porta.setToggleState (shown.porta, juce::dontSendNotification);
        slideTouched = portaTouched = false;
        const auto bpb = engine.getSong().beatsPerBar;
        time.editor.setText (noteedit::formatTime (shown.start, bpb), juce::dontSendNotification);
        length.editor.setText (noteedit::formatTime (shown.length, bpb, true), juce::dontSendNotification);
        time.touched = length.touched = false;
        time.editor.setColour (juce::TextEditor::outlineColourId, theme::colour::borderStrong);
        length.editor.setColour (juce::TextEditor::outlineColourId, theme::colour::borderStrong);
    }

    void apply()
    {
        const auto bpb = engine.getSong().beatsPerBar;
        std::int64_t start = shown.start, len = shown.length;
        auto ok = true;

        if (single && time.touched && ! noteedit::parseTime (time.editor.getText().toStdString(), bpb, start))
        {
            time.editor.setColour (juce::TextEditor::outlineColourId, theme::colour::warning);
            ok = false;
        }

        if (single && length.touched && (! noteedit::parseTime (length.editor.getText().toStdString(), bpb, len, true) || len <= 0))
        {
            length.editor.setColour (juce::TextEditor::outlineColourId, theme::colour::warning);
            ok = false;
        }

        if (! ok)
        {
            repaint();
            return;
        }

        // only what was changed is written, so notes with different values keep theirs
        engine.editSong ([&] (Song& s) {
            if (patternIndex >= (int) s.patterns.size())
                return;

            auto& notes = s.patterns[(std::size_t) patternIndex].notes;

            for (std::size_t k = 0; k < indices.size(); ++k)
            {
                const auto i = indices[k];

                if (i >= (int) notes.size() || notes[(std::size_t) i].start != originals[k].start || notes[(std::size_t) i].key != originals[k].key)
                    continue; // the pattern changed under the inspector

                auto& n = notes[(std::size_t) i];
                if (velocity.touched) n.velocity = (float) velocity.slider.getValue() / 100.0f;
                if (pan.touched) n.pan = (float) pan.slider.getValue() / 100.0f;
                if (release.touched) n.release = (float) release.slider.getValue() / 100.0f;
                if (finePitch.touched) n.finePitch = (float) finePitch.slider.getValue();
                if (modX.touched) n.modX = (float) modX.slider.getValue() / 100.0f;
                if (modY.touched) n.modY = (float) modY.slider.getValue() / 100.0f;
                if (colour.touched) n.colour = juce::jlimit (0, noteColourCount - 1, (int) colour.slider.getValue() - 1);
                if (chance.touched) n.chance = (float) chance.slider.getValue() / 100.0f;
                if (velocityRange.touched) n.velocityRange = (float) velocityRange.slider.getValue() / 100.0f;
                if (slideTouched) n.slide = slide.getToggleState();
                if (portaTouched) n.porta = porta.getToggleState();
                if (single)
                {
                    n.start = start;
                    n.length = len;
                }
            }

            if (chance.touched) // a probability group has one Chance (round 187c)
                notetools::spreadGroupChance (s.patterns[(std::size_t) patternIndex], indices);
            if (single)
                s.patterns[(std::size_t) patternIndex].sortNotes();
        });

        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    AudioEngine& engine;
    std::vector<int> indices;
    std::vector<Note> originals;
    int focus = 0, patternIndex = 0;
    Note shown;
    bool single = false, slideTouched = false, portaTouched = false;
    juce::Label title;
    SliderRow velocity, pan, release, finePitch, modX, modY, colour, chance, velocityRange;
    juce::ToggleButton slide, porta;
    TextRow time, length;
    juce::TextButton applyButton, revertButton, resetButton;
};
} // namespace

std::unique_ptr<juce::Component> makeNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus)
{
    const auto& song = engine.getSong();
    const auto count = (int) song.activePattern().notes.size();
    indices.erase (std::remove_if (indices.begin(), indices.end(), [count] (int i) { return i < 0 || i >= count; }), indices.end());

    if (indices.empty())
        return nullptr;

    return std::make_unique<NoteInspector> (engine, std::move (indices), focus);
}

void showNoteInspector (AudioEngine& engine, std::vector<int> indices, int focus, juce::Rectangle<int> screenArea)
{
    if (auto content = makeNoteInspector (engine, std::move (indices), focus))
        juce::CallOutBox::launchAsynchronously (std::move (content), screenArea, nullptr);
}
} // namespace lynx
