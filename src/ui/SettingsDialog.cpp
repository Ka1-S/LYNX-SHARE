#include "ui/SettingsDialog.h"
#include "ui/PlaylistComponent.h"
#include "core/Comping.h"
#include "ui/FileChoosers.h"
#include "ui/ThemeSettings.h"

#include "core/ClipOps.h"
#include "core/MissingFiles.h"
#include "core/PlaylistOps.h"
#include "core/SearchQuery.h"
#include "engine/PluginHost.h"
#include "engine/JsfxFormat.h"
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
        // LYNX extra (round 190gz, issue #170): Playlist tracks linked to mixer tracks, for this project and new ones
        linkTracks.setButtonText (tr ("Link Playlist tracks to mixer tracks (Track N with insert N; mute, solo, volume and pan on linked track headers)"));
        // (round 190ib: switching on lists what it changes and asks first; Cancel leaves the box as the project is)
        linkTracks.onClick = [this] {
            PlaylistComponent::switchLinkTracks (engine, linkTracks.getToggleState(),
                                                 [safe = juce::Component::SafePointer<ProjectPage> (this)] (bool) {
                                                     if (safe != nullptr)
                                                         safe->linkTracks.setToggleState (safe->engine.getSong().linkTracksToMixer, juce::dontSendNotification);
                                                 });
        };
        add (linkTracks);
        linkTracksNew.setButtonText (tr ("Link Playlist tracks to mixer tracks in new projects"));
        linkTracksNew.onClick = [this] {
            if (hooks.getGeneral == nullptr || hooks.setGeneral == nullptr)
                return;
            auto o = hooks.getGeneral();
            o.linkTracksInNewProjects = linkTracksNew.getToggleState();
            hooks.setGeneral (o);
        };
        linkTracksNew.setEnabled (hooks.getGeneral != nullptr);
        linkTracksNew.setToggleState (hooks.getGeneral != nullptr && hooks.getGeneral().linkTracksInNewProjects, juce::dontSendNotification);
        add (linkTracksNew);
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
        linkTracks.setBounds (area.removeFromTop (26));
        linkTracksNew.setBounds (area.removeFromTop (26));
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
        linkTracks.setToggleState (s.linkTracksToMixer, juce::dontSendNotification);
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
    juce::ToggleButton crossfades, crossfadesNew, linkTracks, linkTracksNew, truncated, fastDeclick;
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
    // issue #182: typing jumps to the folder whose name starts with it (JUCE fix 37 reads the names)
    juce::String getNameForRow (int row) override { return juce::isPositiveAndBelow (row, folders.size()) ? juce::File (folders[row]).getFileName() : juce::String(); }
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

        // Round 190pj (issue #241): JSFX scripts. Documents/LYNX/JSFX, the bundled pack and REAPER's Effects folder are
        // always searched; these are the user's other folders
        jsfxFolders = std::make_unique<FolderList> (
            tr ("JSFX folders"), tr ("JSFX scripts are also found in Documents/LYNX/JSFX, the bundled pack and REAPER's Effects folder."),
            [persistent, sample] { return sample (persistent ? jsfx::userFolders() : juce::StringArray(), { "D:\\JSFX\\Saike" }); },
            [persistent, &host] (const juce::StringArray& list) {
                if (! persistent)
                    return;
                jsfx::setUserFolders (list);
                // (the scripts there listed by the host's own scan thread: headers only, quick)
                host.startBackgroundScanOfFiles (JsfxPluginFormat().searchPathsForPlugins (jsfx::roots(), true, false));
            }, persistent);

        // FL's File page has "Manage plugins" next to the plugin settings (REA round 143b)
        if (managePlugins)
            pluginFolders->addExtraButton (tr ("Plugin manager..."), managePlugins);

        add (*searchFolders);
        add (*browserFolders);
        add (*pluginFolders);
        add (*jsfxFolders);
        add (*flmResources);
    }

protected:
    // The five lists share the window's height; none gets less than its heading, two rows and its buttons (below that
    // the page scrolls)
    int layout (int width) override
    {
        constexpr int padX = 20, padY = 14, gap = 12;
        FolderList* lists[] = { searchFolders.get(), browserFolders.get(), pluginFolders.get(), jsfxFolders.get(), flmResources.get() };
        const auto share = (getHeight() - 2 * padY - 4 * gap) / 5;
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
    std::unique_ptr<FolderList> searchFolders, browserFolders, pluginFolders, jsfxFolders, flmResources;
};

// Round 190jp (design190ia FW-19): FL's Settings tabs, their captions a step larger (type::title, was 7 px caps at
// type::body) in a 30 px row; the LYNX identity stays (the selected tab lifted, the red underline). LYNX's own tabs come
// after a 12 px gap, as FL sets apart its Debug / Account / About.
class SettingsTab final : public juce::TabBarButton
{
public:
    SettingsTab (const juce::String& name, juce::TabbedButtonBar& bar, bool gapBefore) : juce::TabBarButton (name, bar), gap (gapBefore ? groupGap : 0) {}

    // issue #247 (round 190oq): while Settings' search has text, a tab without a match is drawn dimmed (−1: no search)
    int matches = -1;

    static constexpr int groupGap = 12;
    static juce::Font captionFont() { return theme::font (theme::type::title); }

    int getBestTabLength (int) override
    {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (captionFont(), getButtonText())) + 26 + gap;
    }
    bool hitTest (int x, int y) override { return x >= gap && juce::TabBarButton::hitTest (x, y); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme::colour;
        auto area = getActiveArea().toFloat().withTrimmedLeft ((float) gap);
        const auto front = isFrontTab();
        if (front || over || down)
        {
            g.setColour (front ? bg3 : bg2);
            g.fillRoundedRectangle (area.reduced (2.0f, 3.0f), theme::radius::md);
        }
        if (front)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (area.withTop (area.getBottom() - 2.0f).reduced (10.0f, 0.0f), 1.0f);
        }
        g.setColour ((front ? textPrimary : over ? textPrimary.withAlpha (0.85f) : textSecondary).withMultipliedAlpha (matches == 0 ? 0.35f : 1.0f));
        g.setFont (captionFont());
        g.drawFittedText (getButtonText(), area.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

private:
    int gap;
};

class SettingsTabs final : public juce::TabbedComponent
{
public:
    SettingsTabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}
    int firstOwnTab = -1; // the tab with the gap before it

    juce::TabBarButton* createTabButton (const juce::String& name, int index) override
    {
        return new SettingsTab (name, getTabbedButtonBar(), index == firstOwnTab);
    }
};

// Issue #247 (round 190oq), a LYNX extra (FL 2026's Settings have no search): "Find a setting" along the bottom of the
// Settings window, as a browser's find bar — type and it jumps to the first setting whose caption, note, section title,
// list item or tooltip holds the words (any order; the Browser's search syntax: OR, -word, "a phrase"), on whichever page;
// Enter / F3 and the arrows step through the matches (Shift: back), "3 of 12" counts them, pages without a match dim
// their tab, the match is outlined in the accent. Ctrl+F puts the caret in the box; Esc empties it (then closes the
// window). No timer: the outline is drawn only while there is a match.
class SettingsFind final : public juce::Component
{
public:
    struct Match
    {
        int page = 0;
        juce::Component::SafePointer<juce::Component> target;
        juce::String text;
    };

    std::function<juce::TabbedComponent&()> tabsOf;
    std::function<void (const std::vector<int>&)> onCounts; // matches per page (empty: no search)

    SettingsFind()
    {
        box.setTextToShowWhenEmpty (tr ("Find a setting (Ctrl+F)"), theme::colour::textDisabled);
        box.setFont (theme::font (theme::type::body));
        box.setIndents (26, 5);
        box.setComponentID ("settings.find");
        focusOnOpen (box); // (issue #247: the window opens ready to type, as #244's search windows)
        box.onTextChange = [this] { search (true); };
        box.onReturnKey = [this] { step (juce::ModifierKeys::currentModifiers.isShiftDown() ? -1 : 1); };
        box.onEscapeKey = [this] {
            if (box.getText().isNotEmpty())
                box.clear();
            else
                closeToolWindowOf (*this);
        };
        addAndMakeVisible (box);
        for (auto* b : { &prev, &next })
        {
            b->setColour (juce::TextButton::buttonColourId, theme::colour::bg3);
            addAndMakeVisible (*b);
        }
        prev.setTooltip (tr ("Previous match (Shift+Enter, Shift+F3)"));
        next.setTooltip (tr ("Next match (Enter, F3)"));
        prev.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        count.setFont (theme::font (theme::type::small));
        count.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (count);
        ring.setInterceptsMouseClicks (false, false);
        updateButtons();
    }

    ~SettingsFind() override { removeRing(); }

    void focus() { box.grabKeyboardFocus(); box.selectAll(); }
    juce::TextEditor& field() noexcept { return box; }
    int matchCount() const noexcept { return (int) matches.size(); }
    int currentIndex() const noexcept { return current; }
    const std::vector<Match>& allMatches() const noexcept { return matches; }
    juce::Component* ringForTests() noexcept { return ring.getParentComponent() != nullptr ? &ring : nullptr; }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::F3Key)
        {
            step (key.getModifiers().isShiftDown() ? -1 : 1);
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        g.setColour (theme::colour::border);
        g.fillRect (0, 0, getWidth(), 1);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // a magnifier in the box and the two arrows (LYNX vectors)
        const auto b = box.getBounds().toFloat();
        const auto c = juce::Point<float> (b.getX() + 13.0f, b.getCentreY() - 1.0f);
        g.setColour (theme::colour::textSecondary);
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);
        g.drawLine (c.x + 3.6f, c.y + 3.6f, c.x + 7.0f, c.y + 7.0f, 1.8f);
        for (auto* button : { &prev, &next })
        {
            const auto r = button->getBounds().toFloat().withSizeKeepingCentre (9.0f, 5.0f);
            juce::Path p;
            if (button == &prev)
                p.addTriangle (r.getX(), r.getBottom(), r.getRight(), r.getBottom(), r.getCentreX(), r.getY());
            else
                p.addTriangle (r.getX(), r.getY(), r.getRight(), r.getY(), r.getCentreX(), r.getBottom());
            g.setColour (button->isEnabled() ? theme::colour::textPrimary : theme::colour::textDisabled);
            g.fillPath (p);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 5).withTrimmedTop (1);
        count.setBounds (r.removeFromRight (110));
        next.setBounds (r.removeFromRight (28).reduced (2, 0));
        prev.setBounds (r.removeFromRight (28).reduced (2, 0));
        r.removeFromRight (4);
        box.setBounds (r.removeFromLeft (juce::jmin (360, r.getWidth())));
    }

    // what every page holds, in page order and top to bottom
    void search (bool jump)
    {
        matches.clear();
        current = -1;
        const auto query = box.getText().trim();
        std::vector<int> perPage;
        if (query.isNotEmpty() && tabsOf)
        {
            auto& tabs = tabsOf();
            perPage.assign ((std::size_t) tabs.getNumTabs(), 0);
            // (lowered for other scripts too; the query language's OR / AND / NOT stay as they are)
            juce::StringArray words;
            words.addTokens (query, " ", {});
            for (auto& w : words)
                if (w != "OR" && w != "AND" && w != "NOT")
                    w = w.toLowerCase();
            const auto q = words.joinIntoString (" ").toStdString();
            for (int p = 0; p < tabs.getNumTabs(); ++p)
                if (auto* page = tabs.getTabContentComponent (p))
                {
                    std::vector<std::pair<int, Match>> found;
                    collect (*page, *page, p, q, found);
                    std::stable_sort (found.begin(), found.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
                    for (auto& f : found)
                        matches.push_back (std::move (f.second));
                    perPage[(std::size_t) p] = (int) found.size();
                }
        }
        if (onCounts)
            onCounts (perPage);
        if (matches.empty())
            removeRing();
        else if (jump && tabsOf)
        {
            // the first match on the page shown, else the first one
            const auto shown = tabsOf().getCurrentTabIndex();
            current = 0;
            for (int i = 0; i < (int) matches.size(); ++i)
                if (matches[(std::size_t) i].page == shown)
                {
                    current = i;
                    break;
                }
            show();
        }
        updateButtons();
    }

    void step (int direction)
    {
        if (matches.empty())
            return;
        const auto n = (int) matches.size();
        current = current < 0 ? (direction > 0 ? 0 : n - 1) : ((current + direction) % n + n) % n;
        show();
        updateButtons();
    }

private:
    static juce::String textOf (juce::Component& c)
    {
        juce::String t;
        if (auto* label = dynamic_cast<juce::Label*> (&c))
            t = label->getText();
        else if (auto* button = dynamic_cast<juce::Button*> (&c))
            t = button->getButtonText();
        else if (auto* title = dynamic_cast<SectionTitle*> (&c))
            t = title->getText();
        else if (auto* combo = dynamic_cast<juce::ComboBox*> (&c))
            for (int i = 0; i < combo->getNumItems(); ++i)
                t << combo->getItemText (i) << "\n";
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (&c))
            t << "\n" << tip->getTooltip();
        if (const auto& own = c.getProperties()["lynxSearchText"]; own.isString())
            t << "\n" << own.toString();
        return t;
    }

    void collect (juce::Component& page, juce::Component& c, int pageIndex, const std::string& query, std::vector<std::pair<int, Match>>& out)
    {
        if (&c != &page && ! c.isVisible())
            return;
        if (dynamic_cast<juce::TextEditor*> (&c) == nullptr && dynamic_cast<juce::ScrollBar*> (&c) == nullptr)
            if (const auto text = textOf (c).trim(); text.isNotEmpty())
                if (search::matches (query, text.toLowerCase().toStdString()))
                {
                    out.push_back ({ page.getLocalPoint (&c, juce::Point<int>()).y, { pageIndex, &c, text.upToFirstOccurrenceOf ("\n", false, false) } });
                    return; // (a match's children are not listed again)
                }
        for (auto* child : c.getChildren())
            collect (page, *child, pageIndex, query, out);
    }

    void removeRing()
    {
        if (auto* parent = ring.getParentComponent())
            parent->removeChildComponent (&ring);
    }

    void show()
    {
        if (current < 0 || current >= (int) matches.size() || ! tabsOf)
            return;
        auto& m = matches[(std::size_t) current];
        auto* target = m.target.getComponent();
        if (target == nullptr)
            return;
        auto& tabs = tabsOf();
        if (tabs.getCurrentTabIndex() != m.page)
        {
            tabs.setCurrentTabIndex (m.page);
            // round 190rb (ASan, UI monkey seed 107: heap-use-after-free): a page that comes into view gets the look and
            // feel again, and a slider makes its +/- buttons anew then, so the match's control can be gone. The search
            // runs again on the page now shown (it stays shown: no second switch)
            target = m.target.getComponent();
            if (target == nullptr)
            {
                search (true);
                return;
            }
        }
        // scroll the page's viewport to it (a third from the top)
        if (auto* viewport = target->findParentComponentOfClass<juce::Viewport>())
            if (auto* content = viewport->getViewedComponent())
            {
                const auto y = content->getLocalPoint (target, juce::Point<int>()).y;
                const auto h = viewport->getViewHeight();
                if (y < viewport->getViewPositionY() || y + target->getHeight() > viewport->getViewPositionY() + h)
                    viewport->setViewPosition (viewport->getViewPositionX(), juce::jmax (0, y - h / 3));
            }
        // the outline goes into the match's own parent, so it scrolls with it
        if (auto* parent = target->getParentComponent())
        {
            removeRing();
            parent->addAndMakeVisible (ring);
            ring.setBounds (target->getBounds().expanded (3, 2));
            ring.toFront (false);
        }
    }

    void updateButtons()
    {
        const auto n = (int) matches.size();
        prev.setEnabled (n > 0);
        next.setEnabled (n > 0);
        if (box.getText().trim().isEmpty())
            count.setText ({}, juce::dontSendNotification);
        else if (n == 0)
            count.setText (tr ("No match"), juce::dontSendNotification);
        else
            count.setText (tr ("{0} of {1}", { juce::String (juce::jmax (1, current + 1)), juce::String (n) }), juce::dontSendNotification);
        repaint();
    }

    class Ring final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (theme::colour::accent.withAlpha (0.12f));
            g.fillRoundedRectangle (r, theme::radius::md);
            g.setColour (theme::colour::accent);
            g.drawRoundedRectangle (r, theme::radius::md, 1.5f);
        }
    };

    juce::TextEditor box;
    juce::TextButton prev, next;
    juce::Label count;
    Ring ring;
    std::vector<Match> matches;
    int current = -1;
};

class SettingsPanel final : public juce::Component
{
public:
    SettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent, SettingsHooks hooks)
    {
        tabs.setTabBarDepth (30);
        tabs.setOutline (0);
        // FL's order (round 190jp, design190ia FW-19: MIDI, Audio, General, File, Theme, Project, Info), then LYNX's own
        // pages after a gap (Ableton Link, named in full as Ableton's Link integration guidelines ask of other apps)
        const std::pair<SettingsPage, juce::String> order[] = { { SettingsPage::midi, tr ("MIDI") },       { SettingsPage::audio, tr ("Audio") },
                                                                { SettingsPage::general, tr ("General") }, { SettingsPage::files, tr ("Files") },
                                                                { SettingsPage::theme, tr ("Theme") },     { SettingsPage::project, tr ("Project") },
                                                                { SettingsPage::info, tr ("Info") },       { SettingsPage::link, tr ("Ableton Link") },
                                                                { SettingsPage::extras, tr ("LYNX extras") } };
        tabs.firstOwnTab = 7;
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
                case SettingsPage::link: content = makeLinkPage (engine, hooks).release(); break;
            }
            tabs.addTab (name, theme::colour::bg2, content, true);
            pages.push_back (p);
            if (p == page)
                tabs.setCurrentTabIndex (tabs.getNumTabs() - 1, false);
        }
        addAndMakeVisible (tabs);
        // issue #247 (round 190oq): the find bar along the bottom
        find.tabsOf = [this]() -> juce::TabbedComponent& { return tabs; };
        find.onCounts = [this] (const std::vector<int>& counts) {
            auto& bar = tabs.getTabbedButtonBar();
            for (int i = 0; i < bar.getNumTabs(); ++i)
                if (auto* tab = dynamic_cast<SettingsTab*> (bar.getTabButton (i)))
                {
                    tab->matches = counts.empty() ? -1 : counts[(std::size_t) i];
                    tab->repaint();
                }
        };
        addAndMakeVisible (find);
        setWantsKeyboardFocus (false);
        setSize (680, 620 + findHeight);
    }

    static constexpr int findHeight = 36;
    SettingsFind& finder() noexcept { return find; }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0) || key.getKeyCode() == juce::KeyPress::F3Key)
        {
            if (key.getKeyCode() == juce::KeyPress::F3Key)
                find.step (key.getModifiers().isShiftDown() ? -1 : 1);
            else
                find.focus();
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }
    void resized() override
    {
        auto r = getLocalBounds();
        find.setBounds (r.removeFromBottom (findHeight));
        tabs.setBounds (r);
    }

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
    SettingsTabs tabs;
    SettingsFind find;
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

SettingsFindState settingsFindForSnapshot (juce::Component& panel, const juce::String& query, int steps)
{
    SettingsFindState out;
    auto* p = dynamic_cast<SettingsPanel*> (&panel);
    if (p == nullptr)
        return out;
    auto& find = p->finder();
    find.field().setText (query, juce::dontSendNotification); // (a TextEditor posts its change: searched here at once)
    find.search (true);
    for (int i = 0; i < std::abs (steps); ++i)
        find.step (steps > 0 ? 1 : -1);
    out.matches = find.matchCount();
    out.current = find.currentIndex();
    out.page = (int) p->currentPage();
    if (out.current >= 0)
    {
        const auto& m = find.allMatches()[(std::size_t) out.current];
        out.text = m.text;
        out.target = m.target.getComponent();
    }
    out.ring = find.ringForTests();
    out.field = &find.field();
    return out;
}

std::unique_ptr<juce::Component> makeSettingsPanel (AudioEngine& engine, SettingsPage page, bool persistent, SettingsHooks hooks)
{
    return std::make_unique<SettingsPanel> (engine, page, persistent, std::move (hooks));
}
} // namespace lynx
