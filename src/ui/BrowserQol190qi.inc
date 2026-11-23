// Round 190qi (TOP-NEXT R6; DETAILS-2 D2-BR4, BR5, BR6, BR9): the Browser's quality-of-life parts, LYNX-own (FL Studio
// 2026's Browser has none of them; its users ask for them). Included by ui/BrowserComponent.cpp inside namespace lynx,
// before the tree; the members that need the tree are in ui/BrowserQol190qiParts.inc at the end of that file.
//   - MidiPreviewPlayer: a .mid file heard on the selected channel at the project's tempo (core/MidiPreview.h).
//   - SearchGhost: the rest of the search box's completion, greyed after the text (Tab takes it).
//   - FileTypesPanel: Browser ▾ › "File types...", the kinds of files the lists show.
// Nothing here runs while idle: the MIDI preview's timer runs only while one plays.

namespace
{
// core/BrowserView.cpp typeGroups() names them in English
[[maybe_unused]] const char* const typeGroupNames190qi[] = { TR_NOOP ("Audio"), TR_NOOP ("MIDI"), TR_NOOP ("Projects"), TR_NOOP ("Presets"),
                                                             TR_NOOP ("SoundFonts and instruments") };

bool isMidiFile190qi (const juce::File& f)
{
    return f.hasFileExtension ("mid;midi");
}
} // namespace

class BrowserComponent::MidiPreviewPlayer final : private juce::Timer
{
public:
    explicit MidiPreviewPlayer (AudioEngine& e) : engine (e) {}
    ~MidiPreviewPlayer() override { stop(); }

    // The file's notes on `channel` from now; false (and `error`) when it has none
    bool start (const juce::File& f, int on, bool full, juce::String& error)
    {
        stop();
        auto notes = MidiFileIo::loadNotes (f, on, error);
        if (! notes.has_value() || notes->empty())
        {
            if (error.isEmpty())
                error = tr ("This MIDI file has no notes.");
            return false;
        }
        player = midipreview::Player (midipreview::schedule (*notes, juce::jmax (10.0, engine.getSong().bpm), full ? 600.0 : maxSeconds));
        file = f;
        channel = on;
        startMs = juce::Time::getMillisecondCounterHiRes();
        wasPlaying = engine.getTransport().playing.load();
        timerCallback(); // (the notes at 0 ms now)
        startTimer (4);  // (a note waits at most a few ms for its time; the audio block adds the rest, as for any played note)
        return true;
    }

    void stop()
    {
        stopTimer();
        player.stop ([this] (const midipreview::Event& e) { send (e); });
        file = juce::File();
    }

    bool playing() const { return isTimerRunning(); }
    bool isPlaying (const juce::File& f) const { return playing() && f == file; }
    // 0..1 of its length while it plays, 0 when it doesn't
    double progress() const
    {
        if (! playing() || player.lengthMs() <= 0.0)
            return 0.0;
        return juce::jlimit (1.0e-6, 1.0, (juce::Time::getMillisecondCounterHiRes() - startMs) / player.lengthMs());
    }
    int playingChannel() const { return channel; }

    // the preview's longest part unless "Full sample preview" is on (the whole file, up to ten minutes)
    static constexpr double maxSeconds = 30.0;

private:
    void send (const midipreview::Event& e)
    {
        if (channel >= 0 && channel < (int) engine.getSong().channels.size())
            engine.sendLiveNote (channel, e.key, e.velocity, e.on, false); // (a preview: not recorded, not scale-snapped)
    }

    void timerCallback() override
    {
        // the transport starting (or the channel going away) ends it, as a sample preview's player does
        const auto playingNow = engine.getTransport().playing.load();
        if ((playingNow && ! wasPlaying) || channel >= (int) engine.getSong().channels.size())
        {
            stop();
            return;
        }
        wasPlaying = playingNow;
        player.advance (juce::Time::getMillisecondCounterHiRes() - startMs, [this] (const midipreview::Event& e) { send (e); });
        if (player.done())
            stop();
    }

    AudioEngine& engine;
    midipreview::Player player;
    juce::File file;
    int channel = -1;
    double startMs = 0.0;
    bool wasPlaying = false;
};

// The completion's rest, drawn greyed right after the typed text (the search box's own font and indents); it lets
// every click through to the box
class BrowserComponent::SearchGhost final : public juce::Component
{
public:
    explicit SearchGhost (juce::TextEditor& box) : editor (box)
    {
        setInterceptsMouseClicks (false, false);
        setWantsKeyboardFocus (false);
    }

    void setRest (const juce::String& text)
    {
        if (text == rest)
            return;
        rest = text;
        setVisible (rest.isNotEmpty());
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (rest.isEmpty())
            return;
        const auto typed = editor.getText();
        const auto bounds = editor.getTextBounds ({ 0, typed.length() }).getBounds();
        const auto font = editor.getFont();
        const auto x = typed.isEmpty() ? editor.getLeftIndent() : bounds.getRight();
        const auto y = typed.isEmpty() ? editor.getTopIndent() : bounds.getY();
        const auto h = typed.isEmpty() ? juce::roundToInt (font.getHeight()) : juce::jmax (1, bounds.getHeight());
        g.setFont (font);
        g.setColour (theme::colour::textDisabled);
        g.drawText (rest + "  Tab", juce::Rectangle<int> (x, y, juce::jmax (0, getWidth() - x - 2), h), juce::Justification::centredLeft, true);
    }

private:
    juce::TextEditor& editor;
    juce::String rest;
};

// Browser ▾ › "File types...": the kinds of files the lists show (every kind ticked = all, the default), in groups.
// Each tick applies at once; "All types" ticks them all again.
class BrowserComponent::FileTypesPanel final : public juce::Component
{
public:
    explicit FileTypesPanel (BrowserComponent* b) : browser (b)
    {
        note.setText (tr ("Untick the kinds of files you don't want in this Browser's lists and searches. Folders stay listed."),
                      juce::dontSendNotification);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        note.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (note);
        for (const auto& group : browserview::typeGroups())
        {
            auto title = std::make_unique<juce::Label> (juce::String(), tr (group.name));
            title->setFont (theme::font (theme::type::small, true));
            title->setColour (juce::Label::textColourId, theme::colour::textPrimary);
            addAndMakeVisible (*title);
            titles.push_back (std::move (title));
            std::vector<std::unique_ptr<juce::ToggleButton>> row;
            for (const auto& ext : group.extensions)
            {
                auto tick = std::make_unique<juce::ToggleButton> ("." + juce::String (ext));
                tick->onClick = [this, ext] { toggled (ext); };
                tick->setComponentID ("browser.fileTypes." + juce::String (ext)); // (--demo)
                addAndMakeVisible (*tick);
                row.push_back (std::move (tick));
            }
            ticks.push_back (std::move (row));
        }
        allButton.setButtonText (tr ("All types"));
        allButton.onClick = [this] {
            if (browser != nullptr)
                browser->setFileTypes ({});
            refresh();
        };
        close.setButtonText (tr ("Close"));
        close.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        close.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        close.onClick = [this] { closeToolWindowOf (*this); };
        addAndMakeVisible (allButton);
        addAndMakeVisible (close);
        refresh();
        setSize (470, 360);
    }

    void refresh()
    {
        const auto chosen = browser != nullptr ? browser->view.types : std::set<std::string>();
        std::size_t g = 0;
        for (const auto& group : browserview::typeGroups())
        {
            for (std::size_t i = 0; i < group.extensions.size(); ++i)
                ticks[g][i]->setToggleState (chosen.empty() || chosen.count (group.extensions[i]) > 0, juce::dontSendNotification);
            ++g;
        }
        allButton.setEnabled (! chosen.empty());
    }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        note.setBounds (area.removeFromTop (34));
        auto bottom = area.removeFromBottom (28);
        close.setBounds (bottom.removeFromRight (90));
        bottom.removeFromRight (8);
        allButton.setBounds (bottom.removeFromRight (110));
        area.removeFromBottom (8);
        for (std::size_t g = 0; g < ticks.size(); ++g)
        {
            titles[g]->setBounds (area.removeFromTop (20));
            constexpr int perRow = 5, w = 84;
            for (std::size_t i = 0; i < ticks[g].size(); ++i)
            {
                if (i % perRow == 0 && i > 0)
                    area.removeFromTop (22);
                ticks[g][i]->setBounds (area.getX() + (int) (i % perRow) * w, area.getY(), w - 4, 22);
            }
            area.removeFromTop (26);
        }
    }

private:
    void toggled (const std::string& ext)
    {
        if (browser == nullptr)
            return;
        auto chosen = browser->view.types;
        std::set<std::string> every;
        for (const auto& group : browserview::typeGroups())
            every.insert (group.extensions.begin(), group.extensions.end());
        if (chosen.empty())
            chosen = every;
        if (chosen.count (ext) > 0)
            chosen.erase (ext);
        else
            chosen.insert (ext);
        if (chosen == every)
            chosen.clear(); // (all ticked: no filter)
        if (chosen.empty() && browser->view.types.size() == 1) // (the last one unticked: keep it, an empty list helps no one)
            chosen = browser->view.types;
        browser->setFileTypes (chosen);
        refresh();
    }

    juce::Component::SafePointer<BrowserComponent> browser;
    juce::Label note;
    std::vector<std::unique_ptr<juce::Label>> titles;
    std::vector<std::vector<std::unique_ptr<juce::ToggleButton>>> ticks;
    juce::TextButton allButton, close;
};

namespace
{
// The search box's Tab: takes the completion (the box's own key handling lets Tab through when it isn't one)
struct SearchTabKeys final : public juce::KeyListener
{
    std::function<bool()> accept;
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        return key == juce::KeyPress::tabKey && ! key.getModifiers().isAnyModifierKeyDown() && accept != nullptr && accept();
    }
};
} // namespace

void BrowserComponent::startMidiPreview (const juce::File& file)
{
    if (midiPreview == nullptr)
        midiPreview = std::make_unique<MidiPreviewPlayer> (engine);
    const auto channel = engine.getSelectedChannel();
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
    {
        showNote (tr ("Select a channel in the Channel Rack to hear MIDI files"));
        return;
    }
    juce::String error;
    if (! midiPreview->start (file, channel, engine.isFullSamplePreview(), error))
        showNote (error);
}

void BrowserComponent::stopMidiPreview()
{
    if (midiPreview != nullptr)
        midiPreview->stop();
}

void BrowserComponent::askAddTag (const std::vector<std::string>& items)
{
    if (items.size() == 1)
    {
        askAddTag (items.front());
        return;
    }
    auto* window = new juce::AlertWindow (tr ("Add tag"), tr ("Type a tag to add to the {0} selected items. Separate several with commas; "
                                                              "\"Drums/Kick\" puts Kick under Drums.", { juce::String ((int) items.size()) }),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("tags", {});
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BrowserComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, items] (int result) {
                                 const auto text = window->getTextEditorContents ("tags").toStdString();
                                 if (result != 1 || safe == nullptr)
                                     return;
                                 if (tagStore().add (items, text) > 0)
                                 {
                                     saveTags();
                                     safe->tagsChanged();
                                 }
                             }),
                             true);
}

bool BrowserComponent::hidesFolderNamed (const juce::String& name) const
{
    return browserview::folderNameHidden (name.toStdString(), view.hiddenFolderNames);
}

bool BrowserComponent::userHides (const juce::File& file) const
{
    if (! view.types.empty() && ! browserview::typeChosen (file.getFileName().toStdString(), view.types) && ! file.isDirectory())
        return true;
    if (view.hiddenFolderNames.empty())
        return false;
    juce::String root;
    for (const auto& r : allRoots)
        if (file.isAChildOf (r))
        {
            root = r.getFullPathName();
            break;
        }
    return browserview::pathHidden (file.getFullPathName().toStdString(), root.toStdString(), view.hiddenFolderNames);
}

void BrowserComponent::setFileTypes (std::set<std::string> types)
{
    if (types == view.types)
        return;
    view.types = std::move (types);
    saveView();
    refreshViewButton();
    rebuildTree();
    if (searchBox.getText().trim().isNotEmpty())
        runSearch();
}

void BrowserComponent::setHiddenFolderNames (std::vector<std::string> names)
{
    if (names == view.hiddenFolderNames)
        return;
    view.hiddenFolderNames = std::move (names);
    saveView();
    refreshViewButton();
    rebuildTree();
    if (searchBox.getText().trim().isNotEmpty())
        runSearch();
}

void BrowserComponent::showFileTypesDialog()
{
    openToolWindow (std::make_unique<FileTypesPanel> (this), role == Role::samples ? tr ("Sample browser file types") : tr ("Browser file types"),
                    role == Role::samples ? "sample-browser-types" : "browser-types");
}

void BrowserComponent::askHiddenFolderNames()
{
    auto* window = new juce::AlertWindow (tr ("Hide folders named"),
                                          tr ("Folders with these names are left out of this Browser's lists and searches (nothing is deleted). "
                                              "Separate names with commas; * stands for any text: Backup, old*, *_bak"),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("names", juce::String::fromUTF8 (browserview::joinFolderNames (view.hiddenFolderNames).c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BrowserComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe] (int result) {
                                 const auto text = window->getTextEditorContents ("names").toStdString();
                                 if (result == 1 && safe != nullptr)
                                     safe->setHiddenFolderNames (browserview::parseFolderNames (text));
                             }),
                             true);
}
