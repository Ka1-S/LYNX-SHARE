#include "ui/AutoLookEditor.h"

#include "engine/AudioEngine.h"
#include "platform/Paths.h"
#include "ui/ColourPicker.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/TrackIcons.h"

namespace lynx
{
namespace autolookui
{
juce::File file() { return platform::settingsFolder().getChildFile ("auto-look.txt"); }

void load()
{
    const auto f = file();
    autolook::current() = f.existsAsFile() ? autolook::fromText (f.loadFileAsString().toStdString()) : autolook::Rules {};
}

void save()
{
    const auto f = file();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::String::fromUTF8 (autolook::toText (autolook::current()).c_str()));
}
} // namespace autolookui

namespace
{
// a square button that shows a rule's colour (none: a dashed outline) or its icon (none: a dash)
class SwatchButton final : public juce::Button
{
public:
    SwatchButton() : juce::Button ({}) {}
    std::uint32_t colour = 0;
    int icon = -1; // ≥ 0: an icon button (FL's index; 0 = no icon)
    bool iconButton = false;

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (down ? colour::bg5 : over ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (r, 3.0f);
        if (iconButton)
        {
            if (icon > 0)
                ui::drawTrackIcon (g, trackicons::iconFor (icon), r.reduced (4.0f), colour::textPrimary);
            else
            {
                g.setColour (colour::textDisabled);
                g.setFont (font (type::small));
                g.drawText (icon == 0 ? tr ("None") : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")), getLocalBounds(), juce::Justification::centred);
            }
        }
        else if (colour != 0)
        {
            g.setColour (juce::Colour (colour));
            g.fillRoundedRectangle (r.reduced (3.0f), 2.0f);
        }
        else
        {
            g.setColour (colour::textDisabled);
            g.drawRoundedRectangle (r.reduced (3.5f), 2.0f, 1.0f);
            g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getY() + 5.0f, 1.0f);
        }
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }
};

// issue #239 (round 190qo): moves a rule up or down the list (the first matching rule wins, so the order matters); a
// LYNX vector triangle, drawn like the Settings find bar's arrows
class MoveButton final : public juce::Button
{
public:
    explicit MoveButton (bool upwards) : juce::Button ({}), up (upwards) {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (1.5f);
        g.setColour (! isEnabled() ? colour::bg2 : down ? colour::bg5 : over ? colour::bg4 : colour::bg3);
        g.fillRoundedRectangle (r, 3.0f);
        const auto t = r.withSizeKeepingCentre (9.0f, 5.0f);
        juce::Path p;
        if (up)
            p.addTriangle (t.getX(), t.getBottom(), t.getRight(), t.getBottom(), t.getCentreX(), t.getY());
        else
            p.addTriangle (t.getX(), t.getY(), t.getRight(), t.getY(), t.getCentreX(), t.getBottom());
        g.setColour (isEnabled() ? colour::textPrimary : colour::textDisabled);
        g.fillPath (p);
    }

private:
    bool up;
};

juce::String targetsText (int targets)
{
    if (targets == autolook::everything)
        return tr ("Everything");
    juce::StringArray parts;
    if ((targets & autolook::playlistTracks) != 0)
        parts.add (tr ("Tracks"));
    if ((targets & autolook::mixerTracks) != 0)
        parts.add (tr ("Mixer"));
    if ((targets & autolook::channels) != 0)
        parts.add (tr ("Channels"));
    return parts.joinIntoString (", ");
}
} // namespace

struct AutoLookEditor::Row
{
    juce::ComboBox match;
    juce::TextEditor text;
    SwatchButton colour, icon;
    juce::TextButton targets;
    MoveButton moveUp { true }, moveDown { false };
    juce::TextButton remove;
};

AutoLookEditor::AutoLookEditor (AudioEngine& e, bool keep) : engine (e), persistent (keep)
{
    enable.setButtonText (tr ("Color and icon tracks, mixer tracks and channels by their names"));
    enable.setComponentID ("autoLook.on");
    enable.setColour (juce::ToggleButton::textColourId, theme::colour::textPrimary);
    enable.onClick = [this] {
        autolook::current().on = enable.getToggleState();
        changed();
    };
    addAndMakeVisible (enable);
    note.setText (tr ("When an item gets a name a rule matches (a rename, a new track or channel), it takes the rule's color and "
                      "icon. The first matching rule wins. The rules are yours: every project uses them."),
                  juce::dontSendNotification);
    note.setFont (theme::font (theme::type::small));
    note.setColour (juce::Label::textColourId, theme::colour::textDisabled);
    note.setJustificationType (juce::Justification::topLeft);
    note.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (note);
    addButton.setButtonText (tr ("Add rule"));
    addButton.setComponentID ("autoLook.add");
    addButton.onClick = [this] {
        autolook::Rule r;
        r.text = tr ("Kick").toStdString();
        r.colour = 0xff3a6fd8;
        r.icon = trackicons::flIndexOf (trackicons::Icon::kick);
        autolook::current().list.push_back (r);
        autolook::current().on = true;
        changed();
        refresh(); // (issue #239: the rows first, then the page's layout, so Add rule moves below the new row)
        if (! rows.empty())
            rows.back()->text.grabKeyboardFocus();
    };
    applyButton.setButtonText (tr ("Apply to this project"));
    applyButton.setTooltip (tr ("Every track, mixer track and channel whose name a rule matches takes its color and icon now (one undo step)"));
    applyButton.onClick = [this] {
        int changedItems = 0;
        engine.editSong ([&changedItems] (Song& s) { changedItems = autolook::applyAll (s); });
        status.setText (changedItems == 1 ? tr ("1 item changed") : tr ("{0} items changed", { juce::String (changedItems) }), juce::dontSendNotification);
    };
    for (auto* b : { &addButton, &applyButton })
        addAndMakeVisible (*b);
    status.setFont (theme::font (theme::type::small));
    status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    addAndMakeVisible (status);
    refresh();
}

AutoLookEditor::~AutoLookEditor() = default;

void AutoLookEditor::changed (bool relayout)
{
    if (persistent)
        autolookui::save();
    if (relayout && onHeightChange)
        onHeightChange();
}

void AutoLookEditor::refresh()
{
    const auto& rules = autolook::current();
    enable.setToggleState (rules.on, juce::dontSendNotification);
    const auto before = rows.size();
    rows.clear();
    for (int i = 0; i < (int) rules.list.size(); ++i)
        addRow (i);
    resized();
    // issue #239 (round 190qo): a row more or less changes the editor's height. The page used to lay itself out before
    // the new row existed, so the editor kept its old height and clipped "Add rule" (and the buttons) under the new row:
    // after the first rule there was no way to add a second one until Settings was opened again.
    if (rows.size() != before && onHeightChange)
        onHeightChange();
}

void AutoLookEditor::moveRule (int index, int by)
{
    auto& list = autolook::current().list;
    const auto to = index + by;
    if (index < 0 || to < 0 || index >= (int) list.size() || to >= (int) list.size())
        return;
    std::swap (list[(std::size_t) index], list[(std::size_t) to]);
    changed();
    refresh();
    if (to < (int) rows.size()) // (the focus follows the rule, so the arrow can be pressed again)
        (by < 0 ? rows[(std::size_t) to]->moveUp : rows[(std::size_t) to]->moveDown).grabKeyboardFocus();
}

void AutoLookEditor::addRow (int index)
{
    auto& row = *rows.emplace_back (std::make_unique<Row>());
    const auto& rule = autolook::current().list[(std::size_t) index];
    row.match.addItem (tr ("Name contains"), 1);
    row.match.addItem (tr ("Name starts with"), 2);
    row.match.addItem (tr ("Name is"), 3);
    row.match.setSelectedId ((int) rule.match + 1, juce::dontSendNotification);
    row.match.onChange = [this, index, &row] {
        if (index < (int) autolook::current().list.size())
            autolook::current().list[(std::size_t) index].match = (autolook::Match) juce::jlimit (0, 2, row.match.getSelectedId() - 1);
        changed();
    };
    row.text.setText (juce::String::fromUTF8 (rule.text.c_str()), false);
    row.text.setFont (theme::font (theme::type::body));
    row.text.setIndents (6, 5);
    row.text.setTextToShowWhenEmpty (tr ("Name or part of it"), theme::colour::textDisabled);
    row.text.onTextChange = [this, index, &row] {
        if (index < (int) autolook::current().list.size())
            autolook::current().list[(std::size_t) index].text = row.text.getText().toStdString();
        changed();
    };
    row.colour.colour = rule.colour;
    row.colour.setTooltip (tr ("The color the rule gives (none: the color stays)"));
    row.colour.onClick = [this, index, &row] {
        juce::PopupMenu menu;
        menu.addItem (tr ("Leave the color as it is"), true, row.colour.colour == 0, [this, index] {
            if (index < (int) autolook::current().list.size())
                autolook::current().list[(std::size_t) index].colour = 0;
            changed();
            refresh();
        });
        menu.addSeparator();
        const auto area = row.colour.getScreenBounds();
        menu.addSubMenu (tr ("Change color"), ui::colourMenu (row.colour.colour, [this, index] (std::uint32_t c) {
                             if (index < (int) autolook::current().list.size())
                                 autolook::current().list[(std::size_t) index].colour = c;
                             changed();
                             refresh();
                         }, area));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row.colour));
    };
    row.icon.iconButton = true;
    row.icon.icon = rule.icon;
    row.icon.setTooltip (tr ("The icon the rule gives (a dash: the icon stays)"));
    row.icon.onClick = [this, index, &row] {
        juce::PopupMenu menu;
        menu.addItem (tr ("Leave the icon as it is"), true, row.icon.icon < 0, [this, index] {
            if (index < (int) autolook::current().list.size())
                autolook::current().list[(std::size_t) index].icon = -1;
            changed();
            refresh();
        });
        menu.addItem (tr ("Choose icon..."), [this, index, area = row.icon.getScreenBounds(), current = row.icon.icon] {
            ui::showTrackIconPicker (area, juce::jmax (0, current), [this, index] (int flIndex) {
                if (index < (int) autolook::current().list.size())
                    autolook::current().list[(std::size_t) index].icon = flIndex;
                changed();
                refresh();
            });
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row.icon));
    };
    row.targets.setButtonText (targetsText (rule.targets));
    row.targets.setTooltip (tr ("What the rule colors"));
    row.targets.onClick = [this, index, &row] {
        const auto t = index < (int) autolook::current().list.size() ? autolook::current().list[(std::size_t) index].targets : 0;
        juce::PopupMenu menu;
        const std::pair<int, juce::String> kinds[] { { autolook::playlistTracks, tr ("Playlist tracks") },
                                                     { autolook::mixerTracks, tr ("Mixer tracks") },
                                                     { autolook::channels, tr ("Channels") } };
        for (const auto& [bit, name] : kinds)
            menu.addItem (name, true, (t & bit) != 0, [this, index, bit = bit] {
                if (index >= (int) autolook::current().list.size())
                    return;
                auto& targets = autolook::current().list[(std::size_t) index].targets;
                const auto next = targets ^ bit;
                if (next != 0) // (one at least)
                    targets = next;
                changed();
                refresh();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&row.targets));
    };
    row.moveUp.setTooltip (tr ("Move this rule up (the first matching rule wins)"));
    row.moveDown.setTooltip (tr ("Move this rule down"));
    row.moveUp.setEnabled (index > 0);
    row.moveDown.setEnabled (index + 1 < (int) autolook::current().list.size());
    row.moveUp.setComponentID ("autoLook.up" + juce::String (index));
    row.moveDown.setComponentID ("autoLook.down" + juce::String (index));
    // (async: the click's own button is deleted by refresh())
    row.moveUp.onClick = [this, index] {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AutoLookEditor> (this), index] {
            if (safe != nullptr)
                safe->moveRule (index, -1);
        });
    };
    row.moveDown.onClick = [this, index] {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AutoLookEditor> (this), index] {
            if (safe != nullptr)
                safe->moveRule (index, 1);
        });
    };
    row.remove.setComponentID ("autoLook.remove" + juce::String (index));
    row.remove.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")));
    row.remove.setTooltip (tr ("Remove this rule"));
    row.remove.onClick = [this, index] {
        auto& list = autolook::current().list;
        if (index < (int) list.size())
            list.erase (list.begin() + index);
        changed();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<AutoLookEditor> (this)] {
            if (safe != nullptr)
                safe->refresh();
        });
    };
    for (auto* c : std::initializer_list<juce::Component*> { &row.match, &row.text, &row.colour, &row.icon, &row.targets, &row.moveUp,
                                                                     &row.moveDown, &row.remove })
        addAndMakeVisible (*c);
}

int AutoLookEditor::noteHeight (int width) const
{
    // (round 190qo: the note as many lines as it wraps to at this width, as the page measures its own notes; it was
    // two lines always, so a longer translation or a narrow window cut its last line)
    juce::AttributedString text (note.getText());
    text.setFont (note.getFont());
    juce::TextLayout fitted;
    fitted.createLayout (text, (float) juce::jmax (40, width - noteIndent - juce::jmax (8, note.getBorderSize().getLeftAndRight() + 2)));
    return 4 + 15 * juce::jmax (2, fitted.getNumLines());
}

int AutoLookEditor::heightFor (int width) const
{
    return 26 + noteHeight (width) + (int) rows.size() * (rowHeight + gap) + 30 + 4;
}

void AutoLookEditor::resized()
{
    auto r = getLocalBounds();
    enable.setBounds (r.removeFromTop (26));
    note.setBounds (r.removeFromTop (noteHeight (getWidth())).withTrimmedLeft (noteIndent));
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowHeight);
        r.removeFromTop (gap);
        row->match.setBounds (line.removeFromLeft (150));
        line.removeFromLeft (6);
        row->remove.setBounds (line.removeFromRight (rowHeight));
        line.removeFromRight (4);
        row->moveDown.setBounds (line.removeFromRight (22));
        line.removeFromRight (2);
        row->moveUp.setBounds (line.removeFromRight (22));
        line.removeFromRight (6);
        row->targets.setBounds (line.removeFromRight (130));
        line.removeFromRight (6);
        row->icon.setBounds (line.removeFromRight (rowHeight));
        line.removeFromRight (4);
        row->colour.setBounds (line.removeFromRight (rowHeight));
        line.removeFromRight (6);
        row->text.setBounds (line);
    }
    auto buttons = r.removeFromTop (30);
    addButton.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    applyButton.setBounds (buttons.removeFromLeft (180));
    buttons.removeFromLeft (10);
    status.setBounds (buttons);
}
} // namespace lynx
