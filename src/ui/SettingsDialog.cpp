#include "ui/SettingsDialog.h"
#include "core/Comping.h"
#include "ui/FileChoosers.h"
#include "ui/ThemeSettings.h"

#include "core/ClipOps.h"
#include "core/MissingFiles.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/MissingFilesDialog.h"
#include "ui/Theme.h"
#include "platform/Paths.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

using namespace settingsui;

// --- Project page ----------------------------------------------------------------------------------
// (round 190bx: a ScrollingPage, as General / Audio / MIDI: in a short window the rows scroll instead of being cut off)
class ProjectPage final : public ScrollingPage,
                          private juce::ChangeListener
{
public:
    ProjectPage (AudioEngine& e, const SettingsHooks& h) : engine (e), hooks (h)
    {
        add (timeTitle);
        add (grooveTitle);
        add (gridTitle);

        for (auto* l : { &tempoLabel, &beatsLabel, &swingLabel, &pitchLabel })
            add (*l);
        styleLabel (tempoLabel, tr ("Tempo"));
        styleLabel (beatsLabel, tr ("Beats per bar"));
        styleLabel (swingLabel, tr ("Swing"));
        styleLabel (pitchLabel, tr ("Master pitch"));

        auto setup = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, double def) {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 26);
            s.setRange (lo, hi, step);
            s.setTextValueSuffix (suffix);
            s.setDoubleClickReturnValue (true, def);
            s.setColour (juce::Slider::trackColourId, theme::colour::bg4); // a quiet bar: the number is what matters here
            s.setColour (juce::Slider::textBoxOutlineColourId, theme::colour::borderStrong);
            add (s);
        };
        setup (tempo, 10.0, 999.0, 0.001, " BPM", 130.0);
        setup (beats, 1.0, 32.0, 1.0, {}, 4.0);
        setup (swing, 0.0, 100.0, 1.0, "%", 0.0);
        setup (pitch, -1200.0, 1200.0, 1.0, " " + tr ("cents"), 0.0);
        tempo.setNumDecimalPlacesToDisplay (3);
        // FL's Options › Project › Time settings combo (REA round 168j, .flp 0x23): how the time settings are shown
        styleLabel (timeModeLabel, tr ("Time settings"));
        add (timeModeLabel);
        timeMode.addItem (tr ("Set as time division"), 1);
        timeMode.addItem (tr ("Set as time signature"), 2);
        timeMode.setTooltip (tr ("Time division: FL Studio's original system (beats per bar, steps per beat). Time signature: traditional time signatures (numerator / denominator)."));
        timeMode.onChange = [this] {
            edit ([on = timeMode.getSelectedId() == 2] (Song& s) { s.timeSignatureMode = on; });
            showTimeMode (timeMode.getSelectedId() == 2);
        };
        add (timeMode);

        tempo.onValueChange = [this] { edit ([v = tempo.getValue()] (Song& s) { clipops::followTempo (s, v); }); };
        beats.onValueChange = [this] { edit ([v = (int) beats.getValue()] (Song& s) { s.beatsPerBar = v; }); };
        swing.onValueChange = [this] { edit ([v = (float) (swing.getValue() / 100.0)] (Song& s) { s.swing = v; }); };
        pitch.onValueChange = [this] { edit ([v = (float) pitch.getValue()] (Song& s) { s.masterPitch = v; }); };

        for (auto* l : { &ppqText, &stepsText, &signatureText })
        {
            styleLabel (*l, {});
            l->setFont (theme::font (theme::type::small));
            add (*l);
        }
        ppqText.setText (tr ("Timebase: {0} ticks per quarter note (PPQ). Imported projects (.flp, .als, .flm) are converted to it.", { juce::String (ppq) }),
                         juce::dontSendNotification);
        signatureText.setText (tr ("Time signature changes along the song: add a time signature marker on the Playlist ruler."),
                               juce::dontSendNotification);

        // FL's Options › Project general settings › Playlist (round 166e)
        add (playlistTitle);
        crossfades.setButtonText (tr ("Automatic crossfades (overlapping audio clips on a track crossfade over the overlap)"));
        crossfades.onClick = [this] { edit ([on = crossfades.getToggleState()] (Song& s) { s.autoCrossfades = on; }); };
        add (crossfades);
        crossfadesNew.setButtonText (tr ("Enable automatic crossfades in new projects"));
        crossfadesNew.onClick = [this] {
            if (hooks.getGeneral == nullptr || hooks.setGeneral == nullptr)
                return;
            auto o = hooks.getGeneral();
            o.crossfadesInNewProjects = crossfadesNew.getToggleState();
            hooks.setGeneral (o);
        };
        crossfadesNew.setEnabled (hooks.getGeneral != nullptr);
        crossfadesNew.setToggleState (hooks.getGeneral != nullptr ? hooks.getGeneral().crossfadesInNewProjects : true, juce::dontSendNotification);
        add (crossfadesNew);
        // Comping (round 177c): the crossfade at a comp's switches, re-applied to every comp when it changes
        styleLabel (compFadeLabel, tr ("Comp crossfade"));
        add (compFadeLabel);
        setup (compFade, 0.0, 100.0, 0.5, " ms", (double) Song::defaultCompCrossfadeMs);
        compFade.onValueChange = [this] {
            edit ([v = (float) compFade.getValue()] (Song& s) {
                s.compCrossfadeMs = v;
                for (int t = 0; t < (int) s.playlistTracks.size(); ++t)
                    if (comp::isComp (s, t))
                        comp::applyCrossfades (s, t, comp::crossfadeTicks (s));
            });
        };
        // FL's Project › "Play truncated notes in clips" (round 167g)
        truncated.setButtonText (tr ("Play truncated notes in clips (a note that starts before a pattern clip plays from the clip's start)"));
        truncated.onClick = [this] { edit ([on = truncated.getToggleState()] (Song& s) { s.playTruncatedNotes = on; }); };
        add (truncated);
        // FL's Options › Project › General › advanced: "Fast declick for cut groups" (round 167h)
        fastDeclick.setButtonText (tr ("Fast declick for cut groups (use faster ramping when cutting notes)"));
        fastDeclick.onClick = [this] { edit ([on = fastDeclick.getToggleState()] (Song& s) { s.fastDeclick = on; }); };
        add (fastDeclick);
        // FL's Options › Project › General › advanced: "Panning law" (round 167j)
        styleLabel (panLawLabel, tr ("Panning law"));
        add (panLawLabel);
        panLaw.addItem (tr ("Circular"), 1);
        panLaw.addItem (tr ("Triangular"), 2);
        panLaw.setTooltip (tr ("Circular: centre at 0 dB, +3 dB on the side panned to. Triangular: the louder side keeps its level. Mixer inserts and Sampler channels."));
        panLaw.onChange = [this] { edit ([on = panLaw.getSelectedId() == 2] (Song& s) { s.panTriangular = on; }); };
        add (panLaw);

        engine.addChangeListener (this);
        load();
    }

    ~ProjectPage() override { engine.removeChangeListener (this); }

private:
    int layout (int width) override
    {
        auto area = juce::Rectangle<int> (0, 0, width, 100000).reduced (20, 14);
        auto row = [&area] (juce::Label& l, juce::Slider& s) {
            auto r = area.removeFromTop (28);
            l.setBounds (r.removeFromLeft (150));
            s.setBounds (r.removeFromLeft (260));
            area.removeFromTop (8);
        };
        timeTitle.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        {
            auto r = area.removeFromTop (28);
            timeModeLabel.setBounds (r.removeFromLeft (150));
            timeMode.setBounds (r.removeFromLeft (260));
            area.removeFromTop (8);
        }
        row (tempoLabel, tempo);
        row (beatsLabel, beats);
        grooveTitle.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        row (swingLabel, swing);
        row (pitchLabel, pitch);
        gridTitle.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        for (auto* l : { &ppqText, &stepsText, &signatureText })
            l->setBounds (area.removeFromTop (22));
        area.removeFromTop (8);
        playlistTitle.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        crossfades.setBounds (area.removeFromTop (26));
        crossfadesNew.setBounds (area.removeFromTop (26));
        auto compRow = area.removeFromTop (26);
        compFadeLabel.setBounds (compRow.removeFromLeft (150));
        compFade.setBounds (compRow.removeFromLeft (180));
        truncated.setBounds (area.removeFromTop (26));
        fastDeclick.setBounds (area.removeFromTop (26));
        area.removeFromTop (4);
        auto lawRow = area.removeFromTop (26);
        panLawLabel.setBounds (lawRow.removeFromLeft (150));
        panLaw.setBounds (lawRow.removeFromLeft (180));
        return area.getY() + 14;
    }

    void edit (std::function<void (Song&)> fn)
    {
        if (loading)
            return;
        engine.editSong (fn);
    }

    void load()
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        const auto& s = engine.getSong();
        tempo.setValue (s.bpm, juce::dontSendNotification);
        beats.setValue (s.beatsPerBar, juce::dontSendNotification);
        shownUnit = meterUnit (s.beatUnit);
        swing.setValue (s.swing * 100.0, juce::dontSendNotification);
        pitch.setValue (s.masterPitch, juce::dontSendNotification);
        crossfades.setToggleState (s.autoCrossfades, juce::dontSendNotification);
        compFade.setValue (s.compCrossfadeMs, juce::dontSendNotification);
        truncated.setToggleState (s.playTruncatedNotes, juce::dontSendNotification);
        fastDeclick.setToggleState (s.fastDeclick, juce::dontSendNotification);
        panLaw.setSelectedId (s.panTriangular ? 2 : 1, juce::dontSendNotification);
        timeMode.setSelectedId (s.timeSignatureMode ? 2 : 1, juce::dontSendNotification);
        showTimeMode (s.timeSignatureMode);
    }

    // FL's time signature mode (FUN_00eccbd0): the numerator is the beats per bar, the denominator sets the steps per
    // beat (16 / denominator). Round 190fg: the song meter's own denominator (4 unless a scene, an .als, .flp or MIDI
    // file set another); the step grid stays sixteenths, so a beat of 6/8 is 2 steps
    int shownUnit = 4;
    void showTimeMode (bool signature)
    {
        beatsLabel.setText (signature ? tr ("Numerator") : tr ("Beats per bar"), juce::dontSendNotification);
        stepsText.setText (signature ? tr ("Denominator: {0} (the Channel Rack's step grid is {1} steps per beat).", { juce::String (shownUnit), juce::String (juce::jmax (1, 16 / shownUnit)) })
                                     : tr ("Steps per beat: {0} (the Channel Rack's step grid).", { juce::String (stepsPerBeat) }),
                           juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (! tempo.isMouseButtonDown() && ! beats.isMouseButtonDown() && ! swing.isMouseButtonDown() && ! pitch.isMouseButtonDown())
            load();
    }

    AudioEngine& engine;
    SettingsHooks hooks;
    bool loading = false;
    SectionTitle timeTitle { tr ("Time settings") }, grooveTitle { tr ("Groove and tuning") }, gridTitle { tr ("Timebase") },
        playlistTitle { tr ("Playlist") };
    juce::Label tempoLabel, beatsLabel, swingLabel, pitchLabel, ppqText, stepsText, signatureText, panLawLabel, timeModeLabel;
    juce::Slider tempo, beats, swing, pitch;
    juce::ToggleButton crossfades, crossfadesNew, truncated, fastDeclick;
    juce::Label compFadeLabel;
    juce::Slider compFade;
    juce::ComboBox panLaw, timeMode;
};

// --- Info page -------------------------------------------------------------------------------------
// (round 190bx: a ScrollingPage too; the comments box takes the room left and never gets shorter than a few lines)
class InfoPage final : public ScrollingPage,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit InfoPage (AudioEngine& e) : engine (e)
    {
        add (infoTitle);
        for (auto* l : { &titleLabel, &authorLabel, &genreLabel, &urlLabel, &commentsLabel })
            add (*l);
        styleLabel (titleLabel, tr ("Title"));
        styleLabel (authorLabel, tr ("Author"));
        styleLabel (genreLabel, tr ("Genre"));
        styleLabel (urlLabel, tr ("Web link"));
        styleLabel (commentsLabel, tr ("Comments"));

        for (auto* t : { &title, &author, &genre, &url })
        {
            styleEditor (*t);
            t->onReturnKey = [this] { commit(); };
            t->onFocusLost = [this] { commit(); };
            add (*t);
        }
        genre.setTextToShowWhenEmpty (tr ("e.g. House, Trap, Lo-fi"), theme::colour::textDisabled);
        url.setTextToShowWhenEmpty ("https://", theme::colour::textDisabled);

        genreMenu.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe"));
        genreMenu.onClick = [this] { showGenres(); };
        add (genreMenu);
        openLink.onClick = [this] {
            const juce::URL link (url.getText().trim());
            if (url.getText().trim().isNotEmpty())
                link.launchInDefaultBrowser();
        };
        add (openLink);

        styleEditor (comments);
        comments.setMultiLine (true, true);
        comments.setReturnKeyStartsNewLine (true);
        comments.setScrollbarsShown (true);
        comments.onFocusLost = [this] { commit(); };
        add (comments);

        showOnOpen.setButtonText (tr ("Show this page when the project is opened"));
        showOnOpen.onClick = [this] { commit(); };
        add (showOnOpen);

        // FL's Project info: when the project was created and how long it has been worked on (REA round 167g)
        styleLabel (timesLabel, {}); // (secondary text colour)
        add (timesLabel);

        // FL's Project info › "Reset working time" (round 190bx: FL's handler restarts the count at once, no question)
        resetTime.setTooltip (tr ("The project's start date becomes now and its working time starts again from zero"));
        resetTime.onClick = [this] {
            engine.resetWorkTime();
            timerCallback();
        };
        add (resetTime);

        engine.addChangeListener (this);
        load();
        startTimer (1000);
    }

    ~InfoPage() override
    {
        commit(); // (typed text not yet confirmed)
        engine.removeChangeListener (this);
    }

private:
    static constexpr int minimumHeight = 14 + 36 + 4 * 36 + 22 + 84 + 8 + 26 + 2 + 26 + 14; // (comments: at least 84 px)

    int layout (int width) override
    {
        const auto height = juce::jmax (minimumHeight, getHeight());
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (20, 14);
        infoTitle.setBounds (area.removeFromTop (28));
        area.removeFromTop (8);
        auto row = [&area] (juce::Label& l) {
            auto r = area.removeFromTop (28);
            l.setBounds (r.removeFromLeft (110));
            area.removeFromTop (8);
            return r;
        };
        title.setBounds (row (titleLabel));
        author.setBounds (row (authorLabel));
        {
            auto r = row (genreLabel);
            genreMenu.setBounds (r.removeFromRight (30));
            r.removeFromRight (4);
            genre.setBounds (r);
        }
        {
            auto r = row (urlLabel);
            openLink.setBounds (r.removeFromRight (70));
            r.removeFromRight (4);
            url.setBounds (r);
        }
        {
            // (FL: the button at the right of the "This project started on" line)
            auto times = area.removeFromBottom (26);
            const auto buttonWidth = juce::jlimit (120, juce::jmax (120, times.getWidth() / 2),
                                                   juce::roundToInt (juce::GlyphArrangement::getStringWidth (getLookAndFeel().getTextButtonFont (resetTime, 26), resetTime.getButtonText())) + 28);
            resetTime.setBounds (times.removeFromRight (buttonWidth));
            times.removeFromRight (8);
            timesLabel.setBounds (times);
        }
        area.removeFromBottom (2);
        showOnOpen.setBounds (area.removeFromBottom (26));
        area.removeFromBottom (8);
        commentsLabel.setBounds (area.removeFromTop (22));
        comments.setBounds (area);
        return height;
    }

    ProjectInfo typed() const
    {
        ProjectInfo i = engine.getSong().info; // (created / working time are not typed)
        i.title = title.getText().trim().toStdString();
        i.author = author.getText().trim().toStdString();
        i.genre = genre.getText().trim().toStdString();
        i.url = url.getText().trim().toStdString();
        i.comments = comments.getText().trimEnd().replace ("\r\n", "\n").toStdString();
        i.showOnOpen = showOnOpen.getToggleState();
        return i;
    }

    void commit()
    {
        if (loading)
            return;
        const auto info = typed();
        if (info != engine.getSong().info)
            engine.editSong ([info] (Song& s) { s.info = info; });
    }

    void load()
    {
        const juce::ScopedValueSetter<bool> guard (loading, true);
        const auto& i = engine.getSong().info;
        auto set = [] (juce::TextEditor& t, const std::string& value) {
            if (! t.hasKeyboardFocus (true) && t.getText() != u8 (value))
                t.setText (u8 (value), juce::dontSendNotification);
        };
        set (title, i.title);
        set (author, i.author);
        set (genre, i.genre);
        set (url, i.url);
        set (comments, i.comments);
        showOnOpen.setToggleState (i.showOnOpen, juce::dontSendNotification);
        timerCallback();
    }

    void timerCallback() override
    {
        const auto& i = engine.getSong().info;
        const auto created = i.created > 0.0 ? juce::Time ((juce::int64) (i.created * 1000.0)).formatted ("%Y-%m-%d %H:%M")
                                             : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));
        const auto work = (juce::int64) engine.workSecondsNow();
        const auto text = tr ("Created: {0}").replace ("{0}", created) + "    " + tr ("Working time: {0}").replace ("{0}", juce::String (work / 3600) + ":"
            + juce::String (work / 60 % 60).paddedLeft ('0', 2) + ":" + juce::String (work % 60).paddedLeft ('0', 2));
        if (timesLabel.getText() != text)
            timesLabel.setText (text, juce::dontSendNotification);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (engine.getSong().info != typed())
            load();
    }

    void showGenres()
    {
        // common genre names (the ID3 list's best-known ones and today's styles)
        static const char* const genres[] = { "Ambient", "Bass house", "Blues", "Classical", "Country", "Dance", "Deep house", "Disco",
                                               "Drum & bass", "Dubstep", "EDM", "Electro", "Electronic", "Funk", "Future bass", "Funk carioca",
                                               "Hardstyle", "Hip-hop", "House", "Jazz", "Lo-fi", "Metal", "Phonk", "Pop", "R&B", "Reggae",
                                               "Reggaeton", "Rock", "Soul", "Synthwave", "Techno", "Trance", "Trap" };
        juce::PopupMenu menu;
        for (const auto* g : genres)
            menu.addItem (g, [this, name = juce::String (g)] {
                genre.setText (name, juce::dontSendNotification);
                commit();
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&genreMenu));
    }

    AudioEngine& engine;
    bool loading = false;
    SectionTitle infoTitle { tr ("Project information") };
    juce::Label titleLabel, authorLabel, genreLabel, urlLabel, commentsLabel;
    juce::TextEditor title, author, genre, url, comments;
    juce::TextButton genreMenu, openLink { tr ("Open") };
    juce::TextButton resetTime { tr ("Reset working time") };
    juce::ToggleButton showOnOpen;
    juce::Label timesLabel;
};

// --- Files page ------------------------------------------------------------------------------------
// One editable folder list (Add... / Remove), loaded and saved through the callbacks.
class FolderList final : public juce::Component,
                         private juce::ListBoxModel
{
public:
    FolderList (juce::String heading, juce::String note, std::function<juce::StringArray()> loadFn,
                std::function<void (const juce::StringArray&)> saveFn, bool checkFolders = true)
        : title (std::move (heading)), load (std::move (loadFn)), save (std::move (saveFn)), check (checkFolders)
    {
        addAndMakeVisible (title);
        styleLabel (noteLabel, note);
        noteLabel.setFont (theme::font (theme::type::small));
        addAndMakeVisible (noteLabel);
        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg0);
        list.setColour (juce::ListBox::outlineColourId, theme::colour::border);
        list.setOutlineThickness (1);
        addAndMakeVisible (list);
        addButton.onClick = [this] { add(); };
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible (addButton);
        addAndMakeVisible (removeButton);
        folders = load();
        refresh();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        title.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        noteLabel.setBounds (area.removeFromTop (20));
        area.removeFromTop (4);
        auto buttons = area.removeFromRight (100);
        area.removeFromRight (8);
        list.setBounds (area);
        addButton.setBounds (buttons.removeFromTop (26));
        buttons.removeFromTop (6);
        removeButton.setBounds (buttons.removeFromTop (26));
        if (extraButton != nullptr)
        {
            buttons.removeFromTop (6);
            extraButton->setBounds (buttons.removeFromTop (26));
        }
    }

    // the least height that shows the heading, the note, two rows and every button
    int minimumHeight() const { return 28 + 4 + 20 + 4 + juce::jmax (2 * 24 + 2, extraButton != nullptr ? 26 + 6 + 26 + 6 + 26 : 26 + 6 + 26); }

    // A third button under Add / Remove (the plugin folders' "Plugin manager...", as FL's File page has it).
    void addExtraButton (const juce::String& text, std::function<void()> onClick)
    {
        extraButton = std::make_unique<juce::TextButton> (text);
        extraButton->onClick = std::move (onClick);
        addAndMakeVisible (*extraButton);
        resized();
    }

private:
    int getNumRows() override { return juce::jmax (1, folders.size()); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (folders.isEmpty())
        {
            g.setFont (theme::font (theme::type::small));
            g.setColour (theme::colour::textDisabled);
            g.drawText (tr ("(none yet)"), 10, 0, width - 20, height, juce::Justification::centredLeft);
            return;
        }
        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }
        const auto exists = ! check || juce::File (folders[row]).isDirectory();
        g.setFont (theme::font (theme::type::body));
        g.setColour (exists ? theme::colour::textPrimary : theme::colour::warning);
        g.drawText (folders[row] + (exists ? juce::String() : "  (" + tr ("not found") + ")"), 10, 0, width - 20, height,
                    juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override { refresh(); }

    void refresh()
    {
        removeButton.setEnabled (! folders.isEmpty() && list.getSelectedRow() >= 0);
        list.updateContent();
        list.repaint();
    }

    void add()
    {
        chooser = lynx::ui::makeFileChooser (tr ("Add a folder"), juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<FolderList> (this)] (const juce::FileChooser& c) {
                                  if (safe == nullptr || ! c.getResult().isDirectory())
                                      return;
                                  safe->folders.addIfNotAlreadyThere (c.getResult().getFullPathName(), true);
                                  safe->save (safe->folders);
                                  safe->refresh();
                              });
    }

    void removeSelected()
    {
        const auto row = list.getSelectedRow();
        if (row < 0 || row >= folders.size())
            return;
        folders.remove (row);
        save (folders);
        list.deselectAllRows();
        refresh();
    }

    SectionTitle title;
    juce::Label noteLabel;
    std::function<juce::StringArray()> load;
    std::function<void (const juce::StringArray&)> save;
    bool check = true;
    juce::StringArray folders;
    juce::ListBox list;
    juce::TextButton addButton { tr ("Add...") }, removeButton { tr ("Remove") };
    std::unique_ptr<juce::TextButton> extraButton;
    std::unique_ptr<juce::FileChooser> chooser;
};

// (rollup 0.1.28: a ScrollingPage since round 190fn added the fourth list: at a short window every list keeps its
// Add / Remove buttons and two rows, and the page scrolls)
class FilesPage final : public ScrollingPage
{
public:
    FilesPage (AudioEngine& engine, bool persistent, std::function<void()> browserChanged, std::function<void()> managePlugins = {})
    {
        auto sample = [persistent] (juce::StringArray real, std::initializer_list<const char*> fake) {
            if (persistent)
                return real;
            juce::StringArray list;
            for (const auto* f : fake)
                list.add (f);
            return list;
        };
        auto& host = engine.getPluginHost();

        searchFolders = std::make_unique<FolderList> (
            tr ("Search folders for missing samples"), tr ("The Missing files window looks in these (and their sub-folders) by itself."),
            [persistent, sample] {
                juce::StringArray list;
                if (persistent)
                    for (const auto& f : loadSearchFolders())
                        list.add (u8 (f));
                return sample (list, { "D:\\Library\\Drums", "D:\\Library\\Loops" });
            },
            [persistent] (const juce::StringArray& list) {
                if (! persistent)
                    return;
                std::vector<std::string> folders;
                for (const auto& f : list)
                    folders.push_back (f.toStdString());
                saveSearchFolders (folders);
            }, persistent);

        browserFolders = std::make_unique<FolderList> (
            tr ("Browser folders"), tr ("Shown in the Browser's folder list; also searched for missing samples."),
            [persistent, sample] { return sample (persistent ? browserSearchFolders() : juce::StringArray(), { "C:\\Users\\me\\Music\\Samples" }); },
            [persistent, browserChanged] (const juce::StringArray& list) {
                if (! persistent)
                    return;
                PluginHost::settingsFolder().getChildFile ("browser-folders.txt").replaceWithText (list.joinIntoString ("\n"));
                if (browserChanged)
                    browserChanged();
            }, persistent);

        pluginFolders = std::make_unique<FolderList> (
            tr ("Plugin folders"), tr ("Scanned for {0} plugins (Options > Scan for plugins).", { platform::pluginFormatNames() }),
            [persistent, sample, &host] {
                juce::StringArray list;
                if (persistent)
                {
                    const auto path = host.pluginFolders();
                    for (int i = 0; i < path.getNumPaths(); ++i)
                        list.add (path[i].getFullPathName());
                }
                return sample (list, { "C:\\Program Files\\Common Files\\VST3", "C:\\Program Files\\Common Files\\CLAP" });
            },
            [persistent, &host] (const juce::StringArray& list) {
                if (! persistent)
                    return;
                juce::FileSearchPath path;
                for (const auto& f : list)
                    path.add (juce::File (f));
                host.setPluginFolders (path);
            }, persistent);

        // Round 190fn (decision O1, LYNX's own): FL Studio Mobile's factory content for .flm projects, from the user's own
        // copy of the app's "Resources" folder (FL's copy, when FL is installed, is searched after these)
        flmResources = std::make_unique<FolderList> (
            tr ("FL Studio Mobile resources"), tr ("Factory sounds for .flm projects: the app's own Resources folder. LYNX SHARE ships none."),
            [persistent, sample] {
                juce::StringArray list;
                if (persistent)
                    list.addLines (PluginHost::settingsFolder().getChildFile ("flm-resources.txt").loadFileAsString());
                list.removeEmptyStrings();
                return sample (list, { "D:\\FL Studio Mobile\\Resources" });
            },
            [persistent] (const juce::StringArray& list) {
                if (persistent)
                    PluginHost::settingsFolder().getChildFile ("flm-resources.txt").replaceWithText (list.joinIntoString ("\n"));
            }, persistent);

        // FL's File page has "Manage plugins" next to the plugin settings (REA round 143b)
        if (managePlugins)
            pluginFolders->addExtraButton (tr ("Plugin manager..."), managePlugins);

        add (*searchFolders);
        add (*browserFolders);
        add (*pluginFolders);
        add (*flmResources);
    }

protected:
    // The four lists share the window's height; none gets less than its heading, two rows and its buttons (below that
    // the page scrolls)
    int layout (int width) override
    {
        constexpr int padX = 20, padY = 14, gap = 12;
        FolderList* lists[] = { searchFolders.get(), browserFolders.get(), pluginFolders.get(), flmResources.get() };
        const auto share = (getHeight() - 2 * padY - 3 * gap) / 4;
        auto y = padY;
        for (auto* list : lists)
        {
            const auto h = juce::jmax (list->minimumHeight(), share);
            list->setBounds (padX, y, juce::jmax (0, width - 2 * padX), h);
            y += h + gap;
        }
        return y - gap + padY;
    }
    int minimumWidth() const override { return 300; }

private:
    std::unique_ptr<FolderList> searchFolders, browserFolders, pluginFolders, flmResources;
};

class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent, SettingsHooks hooks)
    {
        tabs.setTabBarDepth (34);
        tabs.setOutline (0);
        // FL's order: device pages first, then the app, then the project
        const std::pair<SettingsPage, juce::String> order[] = { { SettingsPage::audio, tr ("Audio") },     { SettingsPage::midi, tr ("MIDI") },
                                                                { SettingsPage::general, tr ("General") }, { SettingsPage::theme, tr ("Theme") },
                                                                { SettingsPage::files, tr ("Files") },     { SettingsPage::extras, tr ("LYNX extras") },
                                                                { SettingsPage::project, tr ("Project") }, { SettingsPage::info, tr ("Info") } };
        for (const auto& [p, name] : order)
        {
            juce::Component* content = nullptr;
            switch (p)
            {
                case SettingsPage::audio: content = makeAudioPage (engine, persistent).release(); break;
                case SettingsPage::midi: content = makeMidiPage (engine, hooks, persistent).release(); break;
                case SettingsPage::general: content = makeGeneralPage (engine, hooks, persistent).release(); break;
                case SettingsPage::theme: content = themeui::makeThemePage().release(); break;
                case SettingsPage::files: content = new FilesPage (engine, persistent, hooks.browserFoldersChanged, hooks.managePlugins); break;
                case SettingsPage::project: content = new ProjectPage (engine, hooks); break;
                case SettingsPage::info: content = new InfoPage (engine); break;
                case SettingsPage::extras: content = makeExtrasPage (engine, hooks, persistent).release(); break;
            }
            tabs.addTab (name, theme::colour::bg2, content, true);
            pages.push_back (p);
            if (p == page)
                tabs.setCurrentTabIndex (tabs.getNumTabs() - 1, false);
        }
        addAndMakeVisible (tabs);
        setSize (680, 620);
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }
    void resized() override { tabs.setBounds (getLocalBounds()); }

    SettingsPage currentPage() const
    {
        const auto i = tabs.getCurrentTabIndex();
        return juce::isPositiveAndBelow (i, (int) pages.size()) ? pages[(std::size_t) i] : SettingsPage::audio;
    }

    void showPage (SettingsPage page)
    {
        for (std::size_t i = 0; i < pages.size(); ++i)
            if (pages[i] == page)
                tabs.setCurrentTabIndex ((int) i);
    }

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::vector<SettingsPage> pages; // in tab order
};

SettingsPanel* openSettingsPanel()
{
    auto* window = findToolWindow ("settings");
    if (window == nullptr)
        return nullptr;
    std::function<SettingsPanel* (juce::Component&)> find = [&find] (juce::Component& c) -> SettingsPanel* {
        if (auto* panel = dynamic_cast<SettingsPanel*> (&c))
            return panel;
        for (auto* child : c.getChildren())
            if (auto* panel = find (*child))
                return panel;
        return nullptr;
    };
    return find (*window);
}
} // namespace

int shownSettingsPage()
{
    if (auto* panel = openSettingsPanel())
        return (int) panel->currentPage();
    return -1;
}

bool switchSettingsPage (SettingsPage page)
{
    auto* panel = openSettingsPanel();
    if (panel == nullptr)
        return false;
    panel->showPage (page);
    if (auto* window = findToolWindow ("settings"))
        window->toFront (true);
    return true;
}

void closeSettings()
{
    if (auto* panel = openSettingsPanel())
        closeToolWindowOf (*panel);
}

void showSettings (AudioEngine& engine, SettingsPage page, SettingsHooks hooks)
{
    // one Settings window: opening it again shows the asked page
    openToolWindow (std::make_unique<SettingsPanel> (engine, page, true, std::move (hooks)), tr ("Settings"), "settings");
}

std::unique_ptr<juce::Component> makeSettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent, SettingsHooks hooks)
{
    return std::make_unique<SettingsPanel> (engine, page, persistent, std::move (hooks));
}
} // namespace lynx
