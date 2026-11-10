#include "ui/NewProjectDialog.h"
#include "platform/Paths.h"

#include "core/Preferences.h"
#include "core/Templates.h"
#include "engine/ProjectFile.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/SettingsPages.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

#include <algorithm>

namespace lynx
{
namespace
{
using namespace settingsui;

juce::File newProjectSettingsFile()
{
    return lynx::platform::settingsFile ("new-project.txt");
}

struct Remembered
{
    bool show = true;
    juce::String folder, author;
    bool ownFolder = true;
    juce::String defaultTemplate; // TemplateChoice::toText (issue #123)
    juce::String startupProject;  // startupProjectText ("" = the default template)
};

Remembered loadRemembered()
{
    Remembered r;
    const auto kv = prefs::parseKeyValues (newProjectSettingsFile().loadFileAsString().toStdString());
    auto get = [&kv] (const char* key) { const auto it = kv.find (key); return it == kv.end() ? juce::String() : juce::String::fromUTF8 (it->second.c_str()); };
    r.show = get ("show") != "0";
    r.folder = get ("folder");
    r.author = get ("author");
    r.ownFolder = get ("ownFolder") != "0";
    r.defaultTemplate = get ("defaultTemplate");
    r.startupProject = get ("startupProject");
    return r;
}

void saveRemembered (const Remembered& r)
{
    juce::String text;
    text << "show=" << (r.show ? 1 : 0) << "\nfolder=" << r.folder << "\nauthor=" << r.author << "\nownFolder=" << (r.ownFolder ? 1 : 0) << "\n";
    if (r.defaultTemplate.isNotEmpty())
        text << "defaultTemplate=" << r.defaultTemplate << "\n";
    if (r.startupProject.isNotEmpty())
        text << "startupProject=" << r.startupProject << "\n";
    newProjectSettingsFile().getParentDirectory().createDirectory();
    newProjectSettingsFile().replaceWithText (text);
}

struct TemplateItem
{
    juce::String name, description;
    int builtIn = -2; // -1 = LYNX's default song, 0.. = makeTemplate(index), -2 = a user file
    juce::File file;

    TemplateChoice choice() const
    {
        TemplateChoice c;
        if (builtIn >= 0)
            c = { TemplateChoice::Kind::builtIn, templateNames()[(std::size_t) builtIn] };
        else if (builtIn == -2) // (the --snapshot samples have no file: their name stands for it)
            c = { TemplateChoice::Kind::user, file != juce::File() ? file.getFileName().toStdString() : (name + ProjectFile::extension).toStdString() };
        return c;
    }
};

std::vector<TemplateItem> templateItems (bool sample)
{
    std::vector<TemplateItem> items;
    items.push_back ({ tr ("Default"), tr ("Drums and a synth on the first inserts, a limiter on Master"), -1, {} });
    const auto names = templateNames();
    const char* const descriptions[] = { TR_NOOP ("Nothing in the patterns"), TR_NOOP ("A four-on-the-floor beat to start from"),
                                         TR_NOOP ("140 BPM, fast hats and an 808 with glide"), TR_NOOP ("124 BPM house groove with bass") };
    for (int i = 0; i < (int) names.size(); ++i)
        items.push_back ({ tr (juce::String::fromUTF8 (names[(std::size_t) i].c_str())), i < 4 ? tr (descriptions[i]) : juce::String(), i, {} });

    if (sample)
    {
        items.push_back ({ "Night Drive kit", tr ("Your template"), -2, juce::File() });
        items.push_back ({ "Vocal session", tr ("Your template"), -2, juce::File() });
        return items;
    }

    for (const auto& f : userTemplatesFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension))
        items.push_back ({ juce::String::fromUTF8 (templateDisplayName (f.getFileName().toStdString()).c_str()), tr ("Your template"), -2, f });

    std::stable_sort (items.begin() + 1 + (long) names.size(), items.end(), [] (const TemplateItem& a, const TemplateItem& b) {
        return a.name.compareNatural (b.name) < 0;
    });
    return items;
}

class TemplateList final : public juce::ListBox,
                           private juce::ListBoxModel
{
public:
    std::vector<TemplateItem> items;
    std::function<void()> onPick;
    std::function<void()> onActivate; // Enter or a double click on a template: create the project (round 170h)
    std::function<void (int)> onMenu; // a right-click on a template (issue #123: "Use as default template")
    TemplateChoice defaultChoice;     // the row drawn with the "Default" mark

    TemplateList()
    {
        setModel (this);
        setRowHeight (40);
        setColour (juce::ListBox::backgroundColourId, theme::colour::bg0);
        setColour (juce::ListBox::outlineColourId, theme::colour::border);
        setOutlineThickness (1);
    }

    int getNumRows() override { return (int) items.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) items.size())
            return;
        const auto& item = items[(std::size_t) row];
        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
            g.setColour (theme::colour::accent);
            g.fillRect (0, 0, 3, height);
        }
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::body, true));
        auto nameWidth = width - 20;
        if (item.choice() == defaultChoice) // issue #123: the template LYNX opens at start-up and with File › New
        {
            const auto mark = tr ("Default template");
            g.setFont (theme::font (theme::type::small, true));
            const auto markWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), mark)) + 12;
            const auto r = juce::Rectangle<int> (width - 8 - markWidth, 5, markWidth, 15);
            g.setColour (theme::colour::accent.withAlpha (0.22f));
            g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (theme::colour::accent);
            g.drawText (mark, r, juce::Justification::centred, false);
            nameWidth -= markWidth + 6;
            g.setFont (theme::font (theme::type::body, true));
        }
        g.setColour (theme::colour::textPrimary);
        g.drawText (item.name, 12, 4, nameWidth, 18, juce::Justification::centredLeft, true);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (item.description, 12, 21, width - 20, 15, juce::Justification::centredLeft, true);
        // the line between the built-in templates and the user's
        if (row + 1 < (int) items.size() && item.builtIn != -2 && items[(std::size_t) row + 1].builtIn == -2)
        {
            g.setColour (theme::colour::borderStrong);
            g.fillRect (8, height - 1, width - 16, 1);
        }
    }

    void selectedRowsChanged (int) override
    {
        if (onPick)
            onPick();
    }

    void returnKeyPressed (int) override
    {
        if (onActivate)
            onActivate();
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu)
            onMenu (row);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override
    {
        if (onActivate)
            onActivate();
    }
};

class NewProjectPanel final : public juce::Component
{
public:
    NewProjectPanel (NewProjectCallback callback, bool persistentState)
        : onCreate (std::move (callback)),
          persistent (persistentState)
    {
        const auto remembered = persistent ? loadRemembered() : Remembered { true, "C:/Users/me/Music/LYNX Projects", "Me", true };

        for (auto* t : { &templateTitle, &nameTitle, &timeTitle, &infoTitle })
            addAndMakeVisible (t);
        templateTitle.setText (tr ("Template"));
        nameTitle.setText (tr ("Name and location"));
        timeTitle.setText (tr ("Time settings"));
        infoTitle.setText (tr ("Information"));

        templates.items = templateItems (! persistent);
        templates.defaultChoice = persistent ? defaultTemplate() : TemplateChoice { TemplateChoice::Kind::user, "Vocal session.lynx" };
        templates.updateContent();
        templates.selectRow (0);
        for (int i = 0; i < (int) templates.items.size(); ++i) // the default template is the one picked first (issue #123)
            if (templates.items[(std::size_t) i].choice() == templates.defaultChoice)
                templates.selectRow (i);
        templates.onPick = [this] { templateChosen(); };
        templates.onMenu = [this] (int row) { templateMenu (row); };
        templates.onActivate = [this] { createProject(); };
        addAndMakeVisible (templates);
        templatesFolder.setTooltip (tr ("Templates are .lynx projects in this folder (File › Save as template... puts one there)"));
        templatesFolder.onClick = [] {
            userTemplatesFolder().createDirectory();
            userTemplatesFolder().startAsProcess();
        };
        addAndMakeVisible (templatesFolder);

        auto label = [this] (juce::Label& l, const juce::String& text) {
            styleLabel (l, text);
            addAndMakeVisible (l);
        };
        auto editor = [this] (juce::TextEditor& e, const juce::String& placeholder = {}) {
            styleEditor (e);
            e.onReturnKey = [this] { if (persistent) createProject(); }; // Enter = Create (round 170h)
            e.setTextToShowWhenEmpty (placeholder, theme::colour::textDisabled);
            addAndMakeVisible (e);
        };

        label (nameLabel, tr ("Name"));
        editor (name, tr ("Untitled"));
        name.onTextChange = [this] { updatePath(); };
        label (folderLabel, tr ("Folder"));
        folder = juce::File::isAbsolutePath (remembered.folder) ? juce::File (remembered.folder)
                                                                 : lynx::platform::documentsFolder().getChildFile ("LYNX Projects");
        if (! persistent)
            folderText = remembered.folder;
        folderValue.setFont (theme::font (theme::type::body));
        folderValue.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        folderValue.setColour (juce::Label::backgroundColourId, theme::colour::bg0);
        folderValue.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (folderValue);
        chooseFolder.onClick = [this] { pickFolder(); };
        addAndMakeVisible (chooseFolder);
        saveNow.setButtonText (tr ("Save it there now"));
        saveNow.setTooltip (tr ("Off: the project opens unsaved and asks where to go when you first save"));
        saveNow.setToggleState (persistent ? remembered.folder.isNotEmpty() : true, juce::dontSendNotification);
        saveNow.onClick = [this] { updatePath(); };
        addAndMakeVisible (saveNow);
        ownFolder.setButtonText (tr ("Create a folder for the project"));
        ownFolder.setToggleState (remembered.ownFolder, juce::dontSendNotification);
        ownFolder.onClick = [this] { updatePath(); };
        addAndMakeVisible (ownFolder);
        pathNote.setFont (theme::font (theme::type::small));
        pathNote.setColour (juce::Label::textColourId, theme::colour::textDisabled);
        pathNote.setMinimumHorizontalScale (0.5f);
        addAndMakeVisible (pathNote);

        label (tempoLabel, tr ("Tempo"));
        tempo.setSliderStyle (juce::Slider::IncDecButtons);
        tempo.setRange (10.0, 522.0, 1.0);
        tempo.setTextValueSuffix (" BPM");
        tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 26);
        addAndMakeVisible (tempo);
        label (meterLabel, tr ("Time signature"));
        for (int i = 1; i <= 16; ++i)
            meter.addItem (juce::String (i) + "/4", i);
        addAndMakeVisible (meter);

        label (titleLabel, tr ("Title"));
        editor (title, tr ("same as the name"));
        label (authorLabel, tr ("Author"));
        editor (author);
        author.setText (remembered.author, false);
        label (genreLabel, tr ("Genre"));
        editor (genre);
        label (urlLabel, tr ("Web link"));
        editor (url, "https://");

        dontShow.setButtonText (tr ("Don't show this window for File › New"));
        dontShow.setTooltip (tr ("File › New then starts the default project at once; this window stays in File › New project..."));
        dontShow.setToggleState (! remembered.show, juce::dontSendNotification);
        addAndMakeVisible (dontShow);

        create.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        create.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        create.onClick = [this] { createProject(); };
        cancel.onClick = [this] { closeToolWindowOf (*this); };
        addAndMakeVisible (create);
        addAndMakeVisible (cancel);

        if (! persistent)
        {
            name.setText ("Night Drive", false);
            genre.setText ("Synthwave", false);
            templates.selectRow (2);
        }
        templateChosen();
        setSize (700, 540);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 12);
        auto bottom = area.removeFromBottom (32);
        cancel.setBounds (bottom.removeFromRight (96));
        bottom.removeFromRight (8);
        create.setBounds (bottom.removeFromRight (128));
        dontShow.setBounds (bottom.removeFromLeft (360));
        area.removeFromBottom (10);

        auto left = area.removeFromLeft (240);
        area.removeFromLeft (20);
        templateTitle.setBounds (left.removeFromTop (26));
        left.removeFromTop (8);
        templatesFolder.setBounds (left.removeFromBottom (28));
        left.removeFromBottom (8);
        templates.setBounds (left);

        constexpr int labelWidth = 104, rowHeight = 26, gap = 6;
        auto row = [&] (juce::Label& l, juce::Component& c) {
            auto r = area.removeFromTop (rowHeight);
            l.setBounds (r.removeFromLeft (labelWidth));
            c.setBounds (r);
            area.removeFromTop (gap);
        };

        nameTitle.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        row (nameLabel, name);
        {
            auto r = area.removeFromTop (rowHeight);
            folderLabel.setBounds (r.removeFromLeft (labelWidth));
            chooseFolder.setBounds (r.removeFromRight (90));
            r.removeFromRight (6);
            folderValue.setBounds (r);
            area.removeFromTop (gap);
        }
        auto toggles = area.removeFromTop (22).withTrimmedLeft (labelWidth);
        saveNow.setBounds (toggles.removeFromLeft (toggles.getWidth() / 2 - 10));
        ownFolder.setBounds (toggles);
        pathNote.setBounds (area.removeFromTop (18).withTrimmedLeft (labelWidth));
        area.removeFromTop (6);

        timeTitle.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        {
            auto r = area.removeFromTop (rowHeight);
            tempoLabel.setBounds (r.removeFromLeft (labelWidth));
            tempo.setBounds (r.removeFromLeft (150));
            area.removeFromTop (gap);
            r = area.removeFromTop (rowHeight);
            meterLabel.setBounds (r.removeFromLeft (labelWidth));
            meter.setBounds (r.removeFromLeft (150));
            area.removeFromTop (gap + 4);
        }

        infoTitle.setBounds (area.removeFromTop (26));
        area.removeFromTop (8);
        row (titleLabel, title);
        row (authorLabel, author);
        row (genreLabel, genre);
        row (urlLabel, url);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    // round 170h: Enter creates the project (the dialog's default button), wherever the focus is in it; before, Enter
    // went on to the main window's shortcuts (it maximised the Playlist behind the dialog) and nothing was created
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::returnKey && persistent)
        {
            createProject();
            return true;
        }
        return false;
    }

    // the template list takes the keyboard focus when the dialog opens, so Enter and the arrow keys work at once
    void parentHierarchyChanged() override
    {
        if (persistent)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::Component> (&templates)] {
                if (auto* list = safe.getComponent(); list != nullptr && list->isShowing())
                    list->grabKeyboardFocus();
            });
    }

private:
    void templateMenu (int row)
    {
        if (! persistent || row < 0 || row >= (int) templates.items.size())
            return;
        const auto choice = templates.items[(std::size_t) row].choice();
        juce::PopupMenu menu;
        menu.addItem (tr ("Use as default template"), true, choice == templates.defaultChoice, [this, choice] {
            setDefaultTemplate (choice);
            templates.defaultChoice = choice;
            templates.repaint();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&templates).withMousePosition());
    }

    void templateChosen()
    {
        const auto row = templates.getSelectedRow();
        if (row < 0 || row >= (int) templates.items.size())
            return;
        const auto song = makeSong (templates.items[(std::size_t) row]);
        tempo.setValue (song.bpm, juce::dontSendNotification);
        meter.setSelectedId (juce::jlimit (1, 16, song.beatsPerBar), juce::dontSendNotification);
        updatePath();
    }

    Song makeSong (const TemplateItem& item) const
    {
        if (item.builtIn >= -1) // LYNX's own templates follow Options › Mixer (round 163); a user's template stays as saved
            return makeNewProject (item.builtIn);
        juce::String error;
        if (auto song = ProjectFile::load (item.file, error))
        {
            adoptFlPluginLevels (*song); // (round 190ad: FL's plugin levels, unless it holds plugins made under LYNX's)
            return std::move (*song);
        }
        return makeNewProject();
    }

    NewProjectSetup setup() const
    {
        NewProjectSetup s;
        s.name = name.getText().trim().toStdString();
        s.folder = saveNow.getToggleState() ? (folderText.isNotEmpty() ? folderText : folder.getFullPathName()).toStdString() : std::string();
        s.ownFolder = ownFolder.getToggleState();
        s.bpm = tempo.getValue();
        s.beatsPerBar = meter.getSelectedId();
        s.info.title = title.getText().trim().toStdString();
        s.info.author = author.getText().trim().toStdString();
        s.info.genre = genre.getText().trim().toStdString();
        s.info.url = url.getText().trim().toStdString();
        return s;
    }

    void updatePath()
    {
        ownFolder.setEnabled (saveNow.getToggleState());
        folderValue.setText ("  " + (folderText.isNotEmpty() ? folderText : folder.getFullPathName()), juce::dontSendNotification);
        const auto path = newProjectPath (setup(), ProjectFile::extension);
        pathNote.setText (path.empty() ? tr ("Not saved until you choose File › Save") : juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', juce::File::getSeparatorChar()),
                          juce::dontSendNotification);
        create.setEnabled (true);
    }

    void pickFolder()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Folder for the project"), folder);
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
            if (fc.getResult() == juce::File())
                return;
            folder = fc.getResult();
            folderText = {};
            saveNow.setToggleState (true, juce::dontSendNotification);
            updatePath();
        });
    }

    void createProject()
    {
        const auto row = templates.getSelectedRow();
        if (row < 0 || row >= (int) templates.items.size())
            return;
        const auto s = setup();
        auto song = makeSong (templates.items[(std::size_t) row]);
        applySetup (song, s);
        if (s.info.title.empty() && name.getText().trim().isEmpty())
            song.info.title.clear();

        juce::File file;
        if (const auto path = newProjectPath (s, ProjectFile::extension); ! path.empty())
        {
            file = juce::File (juce::String::fromUTF8 (path.c_str()).replaceCharacter ('/', juce::File::getSeparatorChar()));
            if (file.existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("New project"),
                                                        tr ("{0} already exists. Choose another name or folder.", { file.getFullPathName() }));
                return;
            }
        }

        if (persistent)
            saveRemembered ({ ! dontShow.getToggleState(), saveNow.getToggleState() ? folder.getFullPathName() : juce::String(), author.getText().trim(),
                              ownFolder.getToggleState(), loadRemembered().defaultTemplate, loadRemembered().startupProject });

        auto callback = onCreate;
        closeToolWindowOf (*this); // deletes this panel
        if (callback)
            callback (std::move (song), file);
    }

    NewProjectCallback onCreate;
    bool persistent = true;
    juce::File folder;
    juce::String folderText; // snapshot mode: a fixed path to show
    SectionTitle templateTitle, nameTitle, timeTitle, infoTitle;
    TemplateList templates;
    juce::TextButton templatesFolder { tr ("Open the templates folder") };
    juce::Label nameLabel, folderLabel, folderValue, pathNote, tempoLabel, meterLabel, titleLabel, authorLabel, genreLabel, urlLabel;
    juce::TextEditor name, title, author, genre, url;
    juce::TextButton chooseFolder { tr ("Choose...") };
    juce::ToggleButton saveNow, ownFolder, dontShow;
    juce::Slider tempo;
    juce::ComboBox meter;
    juce::TextButton create { tr ("Create") }, cancel { tr ("Cancel") };
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace

juce::File userTemplatesFolder()
{
    return lynx::platform::documentsFolder().getChildFile ("LYNX/Templates");
}

bool newProjectDialogEnabled()
{
    return loadRemembered().show;
}

TemplateChoice defaultTemplate()
{
    return TemplateChoice::parse (loadRemembered().defaultTemplate.toStdString());
}

void setDefaultTemplate (const TemplateChoice& choice)
{
    auto r = loadRemembered();
    r.defaultTemplate = juce::String::fromUTF8 (choice.toText().c_str());
    saveRemembered (r);
}

StartupProject startupProject()
{
    return parseStartupProject (loadRemembered().startupProject.toStdString());
}

void setStartupProject (StartupProject choice)
{
    auto r = loadRemembered();
    r.startupProject = choice == StartupProject::defaultTemplate ? juce::String() : juce::String (startupProjectText (choice));
    saveRemembered (r);
}

std::vector<TemplateChoice> availableTemplates()
{
    std::vector<TemplateChoice> out;
    out.push_back ({});
    for (const auto& name : templateNames())
        out.push_back ({ TemplateChoice::Kind::builtIn, name });
    auto files = userTemplatesFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + ProjectFile::extension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    for (const auto& f : files)
        out.push_back ({ TemplateChoice::Kind::user, f.getFileName().toStdString() });
    return out;
}

juce::String templateChoiceName (const TemplateChoice& choice)
{
    switch (choice.kind)
    {
        case TemplateChoice::Kind::builtIn: return tr (juce::String::fromUTF8 (choice.name.c_str()));
        case TemplateChoice::Kind::user: return juce::String::fromUTF8 (templateDisplayName (choice.name).c_str());
        case TemplateChoice::Kind::lynxDefault: break;
    }
    return tr ("Default");
}

std::optional<Song> makeFromTemplate (const TemplateChoice& choice)
{
    switch (choice.kind)
    {
        case TemplateChoice::Kind::builtIn:
            if (const auto i = builtInTemplateIndex (choice.name); i >= 0)
                return makeNewProject (i);
            return std::nullopt;
        case TemplateChoice::Kind::user:
        {
            const auto file = userTemplatesFolder().getChildFile (juce::String::fromUTF8 (choice.name.c_str()));
            juce::String error;
            if (! file.existsAsFile())
                return std::nullopt;
            if (auto song = ProjectFile::load (file, error)) // (a user's template stays as saved)
            {
                adoptFlPluginLevels (*song); // (round 190ad: FL's plugin levels, unless it holds plugins made under LYNX's)
                return song;
            }
            return std::nullopt;
        }
        case TemplateChoice::Kind::lynxDefault: break;
    }
    return makeNewProject();
}

Song makeDefaultTemplateProject (bool* missing)
{
    if (missing != nullptr)
        *missing = false;
    if (auto song = makeFromTemplate (defaultTemplate()))
        return std::move (*song);
    if (missing != nullptr)
        *missing = true;
    return makeNewProject();
}

bool saveUserTemplate (const Song& song, const juce::String& name, juce::String& error)
{
    const auto legal = juce::String::fromUTF8 (legalProjectName (name.toStdString()).c_str());
    const auto folder = userTemplatesFolder();
    if (! folder.createDirectory())
    {
        error = tr ("could not create {0}", { folder.getFullPathName() });
        return false;
    }
    return ProjectFile::save (song, folder.getChildFile (legal + ProjectFile::extension), error);
}

void showNewProjectDialog (NewProjectCallback onCreate)
{
    openToolWindow (std::make_unique<NewProjectPanel> (std::move (onCreate), true), tr ("New project"), "new-project");
}

std::unique_ptr<juce::Component> makeNewProjectPanel()
{
    return std::make_unique<NewProjectPanel> (NewProjectCallback {}, false);
}
} // namespace lynx
