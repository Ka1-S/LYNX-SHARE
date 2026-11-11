#include "ui/VersionsPanel.h"

#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
using safety::versions::Entry;
using safety::versions::Kind;
using safety::versions::Labels;

std::vector<safety::versions::FileInfo> filesOf (const juce::File& folder, const juce::String& extension)
{
    std::vector<safety::versions::FileInfo> out;
    if (! folder.isDirectory())
        return out;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*" + extension))
        out.push_back ({ f.getFileNameWithoutExtension().toStdString(), f.getLastModificationTime().toMilliseconds(), f.getSize() });
    return out;
}

juce::String familyOf (const juce::String& stem)
{
    return juce::String::fromUTF8 (safety::versions::familyOf (stem.toStdString()).c_str());
}

juce::File labelsFile (const juce::File& folder, const juce::String& stem)
{
    return folder.getChildFile (juce::String::fromUTF8 (safety::versions::labelsFileName (familyOf (stem).toStdString()).c_str()));
}

Labels readLabels (const juce::File& file)
{
    return file.existsAsFile() ? safety::versions::labelsFromText (file.loadFileAsString().toStdString()) : Labels();
}

juce::String currentStem (const VersionsContext& c)
{
    return c.projectFile != juce::File() ? c.projectFile.getFileNameWithoutExtension() : c.projectStem;
}

juce::String kindText (const Entry& e)
{
    if (e.kind == Kind::version)
        return tr ("Version {0}", { juce::String (e.number) });
    return e.kind == Kind::overwritten ? tr ("Before a save") : tr ("Backup");
}

class VersionsPanel final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::Timer
{
public:
    explicit VersionsPanel (VersionsActions a) : actions (std::move (a))
    {
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        status.setFont (theme::font (theme::type::small));
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        status.setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (status);
        empty.setFont (theme::font (theme::type::body));
        empty.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        empty.setJustificationType (juce::Justification::centred);
        empty.setText (tr ("No saved versions or backups of this project yet.") + "\n"
                           + tr ("File › Save new version keeps the project as it is now; backups are written while you work."),
                       juce::dontSendNotification);
        addChildComponent (empty);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        openButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        openButton.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        openButton.setTooltip (tr ("Opens the chosen version or backup as a new, unsaved project: the file itself and the open project's file stay as they are"));
        showButton.setTooltip (tr ("Opens the file manager on the chosen file"));
        labelButton.setTooltip (tr ("A short note for the chosen version or backup (kept in a text file beside it, not inside the project). A labelled backup is never removed to make room for newer ones"));
        newVersionButton.setTooltip (tr ("Saves the project under the next number (Song_2, Song_3...) and keeps working in it"));
        for (auto* b : { &openButton, &showButton, &labelButton, &newVersionButton })
            addAndMakeVisible (b);
        openButton.onClick = [this] { openSelected(); };
        showButton.onClick = [this] { showSelected(); };
        labelButton.onClick = [this] { labelSelected(); };
        newVersionButton.onClick = [this] {
            if (actions.saveNewVersion)
                actions.saveNewVersion();
            refresh();
        };

        refresh();
        setSize (620, 440);
        startTimer (3000);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        title.setBounds (area.removeFromTop (24));
        status.setBounds (area.removeFromTop (20));
        area.removeFromTop (6);
        auto buttons = area.removeFromBottom (30);
        area.removeFromBottom (8);
        openButton.setBounds (buttons.removeFromLeft (140));
        buttons.removeFromLeft (8);
        showButton.setBounds (buttons.removeFromLeft (124));
        buttons.removeFromLeft (8);
        labelButton.setBounds (buttons.removeFromLeft (96));
        newVersionButton.setBounds (buttons.removeFromRight (150));
        list.setBounds (area);
        empty.setBounds (area.reduced (24, 0));
    }

    // --versionsshot: the row to show selected
    void selectRowForSnapshot (int row) { list.selectRow (row); }

private:
    static constexpr int rowHeight = 44;

    void timerCallback() override
    {
        if (isShowing())
            refresh();
    }

    void refresh()
    {
        const auto context = actions.context ? actions.context() : VersionsContext();
        const auto selected = list.getSelectedRow() >= 0 && list.getSelectedRow() < (int) entries.size() ? entries[(std::size_t) list.getSelectedRow()].name
                                                                                                          : std::string();
        auto fresh = versionEntries (context);
        const auto now = juce::Time::currentTimeMillis();
        const auto minute = now / 60000;
        if (fresh == entries && context.projectFile == shown.projectFile && context.projectStem == shown.projectStem && minute == shownMinute)
            return;
        shown = context;
        shownMinute = minute;
        entries = std::move (fresh);

        const auto name = currentStem (context);
        title.setText (tr ("Versions") + (name.isNotEmpty() ? juce::String::fromUTF8 ("  ·  ") + name : juce::String()), juce::dontSendNotification);
        juce::int64 newestBackup = 0;
        int versions = 0, backups = 0;
        for (const auto& e : entries)
        {
            (e.kind == Kind::version ? versions : backups) += 1;
            if (e.kind == Kind::autosaved)
                newestBackup = juce::jmax (newestBackup, (juce::int64) e.time);
        }
        auto line = tr ("{0} saved version(s), {1} backup(s)", { juce::String (versions), juce::String (backups) });
        if (newestBackup > 0)
            line << "  ·  " << tr ("Last backup: {0}", { versionAgo (newestBackup, now) });
        if (context.projectFile == juce::File())
            line << "  ·  " << tr ("Not saved yet");
        status.setText (line, juce::dontSendNotification);

        list.updateContent();
        auto row = -1;
        for (int i = 0; i < (int) entries.size(); ++i)
            if (entries[(std::size_t) i].name == selected)
                row = i;
        if (row >= 0)
            list.selectRow (row);
        else
            list.deselectAllRows();
        empty.setVisible (entries.empty());
        list.setVisible (! entries.empty());
        list.repaint();
        updateButtons();
    }

    const Entry* selectedEntry() const
    {
        const auto row = list.getSelectedRow();
        return row >= 0 && row < (int) entries.size() ? &entries[(std::size_t) row] : nullptr;
    }

    void updateButtons()
    {
        const auto any = selectedEntry() != nullptr;
        openButton.setEnabled (any);
        showButton.setEnabled (any);
        labelButton.setEnabled (any);
        newVersionButton.setEnabled (static_cast<bool> (actions.saveNewVersion));
    }

    void openSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr || ! actions.openAsCopy)
            return;
        const auto file = versionFile (shown, *e);
        const auto name = juce::String::fromUTF8 (safety::versions::copyName (*e, currentStem (shown).toStdString()).c_str());
        actions.openAsCopy (file, name); // (may close this panel: nothing after it)
    }

    void showSelected()
    {
        if (const auto* e = selectedEntry())
            versionFile (shown, *e).revealToUser();
    }

    void labelSelected()
    {
        const auto* e = selectedEntry();
        if (e == nullptr)
            return;
        const auto entry = *e;
        auto* w = new juce::AlertWindow (tr ("Label"), tr ("A short note for {0} (empty: no label)", { juce::String::fromUTF8 (entry.name.c_str()) }),
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("label", juce::String::fromUTF8 (entry.label.c_str()));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, entry, safe = juce::Component::SafePointer<VersionsPanel> (this)] (int result) {
                                if (result != 1 || safe == nullptr)
                                    return;
                                if (! setVersionLabel (safe->shown, entry, w->getTextEditorContents ("label")))
                                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Label"),
                                                                            tr ("The label could not be written next to the file."));
                                safe->refresh();
                            }),
                            true);
    }

    int getNumRows() override { return (int) entries.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) entries.size())
            return;
        const auto& e = entries[(std::size_t) row];
        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }
        if (e.current)
        {
            g.setColour (theme::colour::accent);
            g.fillRect (0, 3, 3, height - 6);
        }
        g.setColour (theme::colour::border);
        g.fillRect (0, height - 1, width, 1);

        const auto version = e.kind == Kind::version;
        const int left = 12, dateWidth = 196, sizeWidth = 74;
        const auto textRight = width - dateWidth - sizeWidth - 16;

        // first line: what it is, then the label (or "current")
        g.setFont (theme::font (theme::type::body, version));
        g.setColour (version ? theme::colour::textPrimary : theme::colour::textSecondary);
        const auto kind = kindText (e);
        const auto kindWidth = juce::jmin (textRight - left, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), kind) + 2);
        g.drawText (kind, left, 4, kindWidth, 18, juce::Justification::centredLeft, true);
        auto x = left + kindWidth + 10;
        if (e.current)
        {
            const auto text = tr ("open now");
            g.setFont (theme::font (theme::type::caption));
            const auto w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 12;
            if (x + w < textRight)
            {
                g.setColour (theme::colour::accent);
                g.drawRoundedRectangle ((float) x, 6.0f, (float) w, 14.0f, 3.0f, 1.0f);
                g.drawText (text, x, 6, w, 14, juce::Justification::centred, false);
                x += w + 10;
            }
        }
        if (! e.label.empty() && x < textRight - 20)
        {
            g.setFont (theme::font (theme::type::body));
            g.setColour (theme::colour::textPrimary);
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9c")) + juce::String::fromUTF8 (e.label.c_str()) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x9d")), x, 4,
                        textRight - x, 18, juce::Justification::centredLeft, true);
        }
        // second line: the file's name
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textDisabled);
        g.drawText (juce::String::fromUTF8 (e.name.c_str()) + shown.extension, left, 22, textRight - left, 16, juce::Justification::centredLeft, true);

        // the date (and how long ago), the size
        const auto when = juce::Time (e.time);
        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::textPrimary);
        g.drawText (when.formatted ("%Y-%m-%d  %H:%M"), width - dateWidth - sizeWidth - 8, 4, dateWidth, 18, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textSecondary);
        g.drawText (versionAgo (e.time, juce::Time::currentTimeMillis()), width - dateWidth - sizeWidth - 8, 22, dateWidth, 16, juce::Justification::centredLeft, true);
        g.drawText (juce::File::descriptionOfSizeInBytes (e.size), width - sizeWidth - 8, 4, sizeWidth, 18, juce::Justification::centredRight, true);
    }

    void selectedRowsChanged (int) override { updateButtons(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { openSelected(); }
    void returnKeyPressed (int) override { openSelected(); }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || row < 0 || row >= (int) entries.size())
            return;
        list.selectRow (row);
        juce::PopupMenu menu;
        menu.addItem (tr ("Open as a copy"), [this] { openSelected(); });
        menu.addItem (tr ("Show in folder"), [this] { showSelected(); });
        menu.addItem (tr ("Label..."), [this] { labelSelected(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&list));
    }

    juce::String getTooltipForRow (int row) override
    {
        if (row < 0 || row >= (int) entries.size())
            return {};
        return versionFile (shown, entries[(std::size_t) row]).getFullPathName();
    }

    VersionsActions actions;
    VersionsContext shown;
    juce::int64 shownMinute = -1;
    std::vector<Entry> entries;
    juce::Label title, status, empty;
    juce::ListBox list;
    juce::TextButton openButton { tr ("Open as a copy") }, showButton { tr ("Show in folder") }, labelButton { tr ("Label...") },
        newVersionButton { tr ("Save new version") };
};
} // namespace

std::vector<safety::versions::Entry> versionEntries (const VersionsContext& c)
{
    const auto saved = c.projectFile != juce::File();
    const auto stem = currentStem (c);
    const auto folder = saved ? c.projectFile.getParentDirectory() : juce::File();
    // (a project opened from the backup folder itself: its neighbours are backups, not versions)
    const auto ownFolder = saved && folder != c.backupFolder;
    return safety::versions::list (ownFolder ? filesOf (folder, c.extension) : std::vector<safety::versions::FileInfo>(), filesOf (c.backupFolder, c.extension),
                                   stem.toStdString(), ownFolder, ownFolder ? readLabels (labelsFile (folder, stem)) : Labels(),
                                   readLabels (labelsFile (c.backupFolder, stem)));
}

juce::File versionFile (const VersionsContext& c, const safety::versions::Entry& entry)
{
    const auto folder = entry.kind == Kind::version ? c.projectFile.getParentDirectory() : c.backupFolder;
    return folder.getChildFile (juce::String::fromUTF8 (entry.name.c_str()) + c.extension);
}

bool setVersionLabel (const VersionsContext& c, const safety::versions::Entry& entry, const juce::String& label)
{
    const auto folder = entry.kind == Kind::version ? c.projectFile.getParentDirectory() : c.backupFolder;
    const auto file = labelsFile (folder, currentStem (c));
    auto labels = readLabels (file);
    const auto clean = safety::versions::cleanLabel (label.toStdString());
    if (clean.empty())
        labels.erase (entry.name);
    else
        labels[entry.name] = clean;
    // (labels of files that are gone, a rotated backup's for one, go with them)
    labels = safety::versions::pruned (labels, [&] (const std::string& name) {
        return folder.getChildFile (juce::String::fromUTF8 (name.c_str()) + c.extension).existsAsFile();
    });
    const auto text = safety::versions::labelsToText (labels);
    if (text.empty())
        return ! file.existsAsFile() || file.deleteFile();
    return file.replaceWithText (juce::String::fromUTF8 (text.c_str()), false, false, "\n");
}

safety::versions::Labels backupLabels (const juce::File& backupFolder, const juce::String& projectStem)
{
    return readLabels (labelsFile (backupFolder, projectStem));
}

juce::String versionAgo (juce::int64 fileTimeMs, juce::int64 nowMs)
{
    int unit = 0;
    const auto n = safety::versions::agoValue ((nowMs - fileTimeMs) / 1000, unit);
    if (unit == 0)
        return n < 1 ? tr ("just now") : tr ("{0} min ago", { juce::String (n) });
    return unit == 1 ? tr ("{0} h ago", { juce::String (n) }) : tr ("{0} d ago", { juce::String (n) });
}

std::unique_ptr<juce::Component> makeVersionsPanel (VersionsActions actions)
{
    return std::make_unique<VersionsPanel> (std::move (actions));
}

juce::String versionsSelfTest()
{
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-versions-selftest-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    const auto project = root.getChildFile ("project"), backup = root.getChildFile ("backup");
    if (! project.createDirectory() || ! backup.createDirectory())
        return "could not create " + root.getFullPathName();
    const juce::ScopeGuard cleanUp { [&root] { root.deleteRecursively(); } };

    std::map<juce::String, juce::String> contents;
    const auto write = [&contents] (const juce::File& f, const juce::String& text) {
        contents[f.getFullPathName()] = text;
        return f.replaceWithText (text);
    };
    auto ok = write (project.getChildFile ("Song.lynx"), "one") && write (project.getChildFile ("Song_2.lynx"), "two two")
              && write (project.getChildFile ("Song_3.lynx"), "three") && write (project.getChildFile ("Other.lynx"), "other")
              && write (backup.getChildFile ("Song_3 (autosaved on 2026-10-20 at 14h19).lynx"), "auto")
              && write (backup.getChildFile ("Song_2 (overwritten on 2026-10-20 at 13h00).lynx"), "before")
              && write (backup.getChildFile ("Else (autosaved on 2026-10-20 at 14h19).lynx"), "else");
    if (! ok)
        return "could not write the test files";

    VersionsContext c { project.getChildFile ("Song_3.lynx"), "Song_3", backup, ".lynx" };
    auto entries = versionEntries (c);
    if (entries.size() != 5)
        return "listed " + juce::String ((int) entries.size()) + " entries, not 5";
    const auto find = [&entries] (const char* name) -> const safety::versions::Entry* {
        for (const auto& e : entries)
            if (e.name == name)
                return &e;
        return nullptr;
    };
    const auto* v2 = find ("Song_2");
    const auto* a3 = find ("Song_3 (autosaved on 2026-10-20 at 14h19)");
    const auto* cur = find ("Song_3");
    if (v2 == nullptr || a3 == nullptr || cur == nullptr || ! cur->current || v2->current || v2->size != 7)
        return "the entries are not the files written";
    if (versionFile (c, *v2) != project.getChildFile ("Song_2.lynx") || versionFile (c, *a3).getParentDirectory() != backup)
        return "an entry points to the wrong file";

    if (! setVersionLabel (c, *v2, "  before the\tdrop ") || ! setVersionLabel (c, *a3, "good bass"))
        return "a label could not be written";
    entries = versionEntries (c);
    v2 = find ("Song_2");
    a3 = find ("Song_3 (autosaved on 2026-10-20 at 14h19)");
    if (v2 == nullptr || a3 == nullptr || v2->label != "before the drop" || a3->label != "good bass")
        return "the labels did not come back";
    if (! project.getChildFile ("Song.version-labels.txt").existsAsFile() || ! backup.getChildFile ("Song.version-labels.txt").existsAsFile())
        return "the labels are not in files beside the projects";
    if (backupLabels (backup, "Song_3").size() != 1)
        return "the backup labels are not read for the rotation";

    // a label of a file that is gone leaves with the next write; an empty label removes it (and the empty file)
    backup.getChildFile ("Song_3 (autosaved on 2026-10-20 at 14h19).lynx").deleteFile();
    contents.erase (backup.getChildFile ("Song_3 (autosaved on 2026-10-20 at 14h19).lynx").getFullPathName());
    entries = versionEntries (c);
    const auto* o2 = find ("Song_2 (overwritten on 2026-10-20 at 13h00)");
    if (o2 == nullptr || ! setVersionLabel (c, *o2, "x") || backupLabels (backup, "Song_3").size() != 1 || ! setVersionLabel (c, *o2, "")
        || backup.getChildFile ("Song.version-labels.txt").existsAsFile())
        return "labels of removed files are not cleaned up";

    // the project files are exactly as written, and nothing but the labels file was added
    for (const auto& [path, text] : contents)
        if (juce::File (path).loadFileAsString() != text)
            return "a project file changed: " + path;
    if (project.getNumberOfChildFiles (juce::File::findFilesAndDirectories) != 5 || backup.getNumberOfChildFiles (juce::File::findFilesAndDirectories) != 2)
        return "unexpected files were written";

    // a project never saved: only the backups filed under its name
    VersionsContext fresh { juce::File(), "Else", backup, ".lynx" };
    if (versionEntries (fresh).size() != 1)
        return "an unsaved project does not list just its backups";
    return "OK";
}
} // namespace lynx
