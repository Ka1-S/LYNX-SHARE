#include "ui/TrackImportDialog.h"

#include "ui/DialogFrame.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
juce::String summaryOf (const trackimport::SourceTrack& t)
{
    juce::StringArray parts;
    if (t.patternClips > 0)
        parts.add (tr ("{0} pattern clip(s)", { juce::String (t.patternClips) }));
    if (t.audioClips > 0)
        parts.add (tr ("{0} audio clip(s)", { juce::String (t.audioClips) }));
    if (t.automationClips > 0)
        parts.add (tr ("{0} automation clip(s)", { juce::String (t.automationClips) }));
    if (t.gridClips > 0)
        parts.add (tr ("{0} clip slot(s)", { juce::String (t.gridClips) }));
    if (t.lanes > 0)
        parts.add (tr ("{0} automation lane(s)", { juce::String (t.lanes) }));
    if (t.versions > 0)
        parts.add (tr ("{0} versions", { juce::String (t.versions) }));
    if (t.compTrack)
        parts.add (tr ("comp track (with its take lanes)"));
    else if (t.folder)
        parts.add (tr ("folder (with its tracks)"));
    if (t.takeLane)
        parts.add (tr ("take lane"));
    if (! t.insert.empty())
        parts.add (tr ("plays through {0}", { juce::String::fromUTF8 (t.insert.c_str()) }));
    if (parts.isEmpty())
        parts.add (tr ("empty"));
    return parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

// One row: the switch with the track's name, what it holds in the secondary colour
class Row final : public juce::Component
{
public:
    Row (const juce::String& name, const juce::String& summary, int indent, bool section = false) : about (summary), depth (indent), header (section)
    {
        if (! header)
        {
            box.setButtonText (name);
            addAndMakeVisible (box);
        }
        else
            title = name;
    }
    juce::ToggleButton box;
    void resized() override
    {
        if (header)
            return;
        auto r = getLocalBounds().withTrimmedLeft (depth * 14);
        const auto width = juce::jmin (r.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (theme::font (theme::type::body), box.getButtonText()) + 34);
        box.setBounds (r.removeFromLeft (juce::jmax (120, width)));
    }
    void paint (juce::Graphics& g) override
    {
        if (header)
        {
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (title, getLocalBounds().withTrimmedLeft (4), juce::Justification::bottomLeft, true);
            return;
        }
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawFittedText (about, getLocalBounds().withTrimmedLeft (box.getRight() + 6).withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.85f);
    }

private:
    juce::String about, title;
    int depth = 0;
    bool header = false;
};

class TrackImportWindow final : public ui::dialog::Alert
{
public:
    TrackImportWindow (const juce::String& fileName, const Song& source, std::int64_t selection, const std::vector<int>& picked)
        : ui::dialog::Alert (tr ("Import tracks from another project"), tr ("Tracks of {0}:", { fileName }), juce::MessageBoxIconType::NoIcon, nullptr, 600),
          selectionStart (selection)
    {
        for (const auto& t : trackimport::listTracks (source))
        {
            auto row = std::make_unique<Row> (juce::String (t.track + 1) + ". " + juce::String::fromUTF8 (t.name.c_str()), summaryOf (t), t.depth);
            row->box.setToggleState (std::find (picked.begin(), picked.end(), t.track) != picked.end(), juce::dontSendNotification);
            trackRows.push_back ({ t.track, row.get() });
            rows.push_back (std::move (row));
        }
        const auto loose = trackimport::looseChannels (source);
        if (! loose.empty())
        {
            rows.push_back (std::make_unique<Row> (tr ("Channel Rack channels no clip plays:"), juce::String(), 0, true));
            for (const auto& c : loose)
            {
                auto row = std::make_unique<Row> (juce::String::fromUTF8 (c.name.c_str()), tr ("channel"), 0);
                channelRows.push_back ({ c.channel, row.get() });
                rows.push_back (std::move (row));
            }
        }
        if (trackRows.empty() && channelRows.empty())
            rows.push_back (std::make_unique<Row> (tr ("The project has no tracks to import."), juce::String(), 0, true));
        for (auto& r : rows)
            list.addAndMakeVisible (*r);

        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        content.addAndMakeVisible (viewport);

        all.setButtonText (tr ("All"));
        none.setButtonText (tr ("None"));
        all.onClick = [this] { setAll (true); };
        none.onClick = [this] { setAll (false); };
        content.addAndMakeVisible (all);
        content.addAndMakeVisible (none);

        withInserts.setButtonText (tr ("With mixer inserts and effects"));
        withInserts.setTooltip (tr ("The inserts the tracks play through come too, with their effects, levels and sends (and the buses they send to). Off: the new channels play to the Master"));
        withInserts.setToggleState (true, juce::dontSendNotification);
        withAutomation.setButtonText (tr ("With automation"));
        withAutomation.setTooltip (tr ("Automation clips (also on other tracks) and lanes that drive what comes in"));
        withAutomation.setToggleState (true, juce::dontSendNotification);
        content.addAndMakeVisible (withInserts);
        content.addAndMakeVisible (withAutomation);

        placeLabel.setText (tr ("Place them at"), juce::dontSendNotification);
        placeLabel.setFont (theme::font (theme::type::body));
        place.addItem (tr ("The song start"), 1);
        place.addItem (tr ("The start of the time selection"), 2);
        place.setItemEnabled (2, selectionStart >= 0);
        place.setSelectedId (1, juce::dontSendNotification);
        content.addAndMakeVisible (placeLabel);
        content.addAndMakeVisible (place);

        content.onLayout = [this] { layout(); };
        content.setSize (560, listHeight + 28 + 6 + 26 + 26 + 6 + 26);
        addCustomComponent (&content);

        addButton (tr ("Import"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    TrackImportChoice choice() const
    {
        TrackImportChoice c;
        for (const auto& [track, row] : trackRows)
            if (row->box.getToggleState())
                c.tracks.push_back (track);
        for (const auto& [channel, row] : channelRows)
            if (row->box.getToggleState())
                c.options.channels.push_back (channel);
        c.options.withInserts = withInserts.getToggleState();
        c.options.withAutomation = withAutomation.getToggleState();
        c.options.at = place.getSelectedId() == 2 && selectionStart >= 0 ? selectionStart : 0;
        return c;
    }

private:
    struct Content final : public juce::Component
    {
        std::function<void()> onLayout;
        void resized() override
        {
            if (onLayout)
                onLayout();
        }
    };

    void setAll (bool on)
    {
        for (auto& [track, row] : trackRows)
            row->box.setToggleState (on, juce::dontSendNotification);
        for (auto& [channel, row] : channelRows)
            row->box.setToggleState (on, juce::dontSendNotification);
    }

    void layout()
    {
        auto area = content.getLocalBounds();
        auto top = area.removeFromTop (listHeight);
        viewport.setBounds (top);
        const auto width = top.getWidth() - viewport.getScrollBarThickness();
        int y = 0;
        for (auto& r : rows)
        {
            r->setBounds (0, y, width, 24);
            y += 24;
        }
        list.setSize (width, juce::jmax (y, 1));
        area.removeFromTop (6);
        auto buttons = area.removeFromTop (24);
        none.setBounds (buttons.removeFromRight (70));
        buttons.removeFromRight (6);
        all.setBounds (buttons.removeFromRight (70));
        area.removeFromTop (4);
        withInserts.setBounds (area.removeFromTop (26));
        withAutomation.setBounds (area.removeFromTop (26));
        area.removeFromTop (6);
        auto placeRow = area.removeFromTop (26);
        placeLabel.setBounds (placeRow.removeFromLeft (130));
        place.setBounds (placeRow.removeFromLeft (260));
    }

    static constexpr int listHeight = 264;
    std::int64_t selectionStart = -1;
    Content content;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::pair<int, Row*>> trackRows, channelRows;
    juce::TextButton all, none;
    juce::ToggleButton withInserts, withAutomation;
    juce::Label placeLabel;
    juce::ComboBox place;
};
} // namespace

juce::AlertWindow* buildTrackImportWindow (const juce::String& fileName, const Song& source, std::int64_t selectionStart, const std::vector<int>& picked)
{
    return new TrackImportWindow (fileName, source, selectionStart, picked);
}

TrackImportChoice trackImportChoiceOf (juce::AlertWindow& window)
{
    if (auto* w = dynamic_cast<TrackImportWindow*> (&window))
        return w->choice();
    return {};
}

void showTrackImportDialog (const juce::String& fileName, const Song& source, std::int64_t selectionStart,
                            std::function<void (const TrackImportChoice&)> onAccept)
{
    auto* window = buildTrackImportWindow (fileName, source, selectionStart);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onAccept] (int result) {
                                 if (result == 1 && onAccept)
                                     onAccept (trackImportChoiceOf (*window));
                             }),
                             true);
    keepAboveMainWindow (*window);
}

juce::String trackImportReport (const trackimport::Result& r, const juce::String& fileName, const juce::StringArray& missingPlugins)
{
    juce::StringArray lines;
    lines.add (tr ("From {0}:", { fileName }));
    lines.add (tr ("{0} track(s), {1} channel(s), {2} insert(s), {3} pattern(s), {4} automation(s), {5} clip(s)",
                   { juce::String ((int) r.tracks.size()), juce::String ((int) r.channels.size()), juce::String ((int) r.inserts.size()),
                     juce::String (r.patterns), juce::String (r.automations), juce::String (r.clips + r.gridClips) }));
    for (const auto& n : r.notes)
        lines.add (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + juce::String::fromUTF8 (n.c_str()));
    for (const auto& p : missingPlugins)
        lines.add (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + tr ("Plugin not found here: {0}", { p }));
    return lines.joinIntoString ("\n");
}
} // namespace lynx
