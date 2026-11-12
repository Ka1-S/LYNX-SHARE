#include "ui/LyricsPanel.h"

#include "engine/AudioEngine.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
constexpr int marginWidth = 46;
constexpr double commitAfterMs = 700.0;

juce::String normalised (juce::String text) { return text.replace ("\r\n", "\n").replace ("\r", "\n"); }
} // namespace

// The left margin: each line's position (a click jumps there), drawn at the editor's own line heights
class LyricsPanel::Margin final : public juce::Component
{
public:
    explicit Margin (LyricsPanel& p) : owner (p) { setWantsKeyboardFocus (false); }

    // the row of line `i` in the margin (the editor's caret rectangle of the line's first character)
    juce::Rectangle<int> rowOf (int i) const
    {
        if (i < 0 || i >= (int) owner.lineStartChars.size())
            return {};
        const auto caret = owner.editor.getCaretRectangleForCharIndex (owner.lineStartChars[(std::size_t) i]);
        return { 0, caret.getY(), getWidth(), juce::jmax (14, caret.getHeight()) };
    }

    int lineAtY (int y) const
    {
        for (int i = 0; i < (int) owner.lines.size(); ++i)
            if (const auto r = rowOf (i); y >= r.getY() && y < r.getBottom())
                return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg1);
        g.setColour (theme::colour::border);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        const auto& song = owner.engine.getSong();
        g.setFont (theme::font (theme::type::caption, true));
        for (int i = 0; i < (int) owner.lines.size(); ++i)
        {
            const auto& line = owner.lines[(std::size_t) i];
            const auto row = rowOf (i);
            if (row.getBottom() < 0 || row.getY() > getHeight())
                continue;
            const auto playing = i == owner.playingLine;
            const auto target = lyrics::jumpTick (song, line);
            if (line.section)
            {
                // a heading: an accent tick at the margin's edge
                g.setColour (theme::colour::accent.withAlpha (target ? 0.9f : 0.45f));
                g.fillRect (getWidth() - 4, row.getY() + 2, 3, row.getHeight() - 4);
            }
            if (! target)
                continue;
            auto chip = row.reduced (4, 1).withTrimmedRight (4).toFloat();
            g.setColour (playing ? theme::colour::accent : i == hover ? theme::colour::bg5 : theme::colour::bg3);
            g.fillRoundedRectangle (chip, theme::radius::sm);
            g.setColour (playing ? theme::colour::onAccent : theme::colour::textSecondary);
            if (line.stamped)
                g.drawText (line.beat <= 1 ? juce::String (line.bar) : juce::String (line.bar) + "." + juce::String (line.beat), chip,
                            juce::Justification::centred, false);
            else
            {
                // a heading that jumps to its Playlist marker: a small flag
                juce::Path flag;
                const auto cx = chip.getCentreX(), cy = chip.getCentreY();
                flag.addTriangle (cx - 3.0f, cy - 4.0f, cx + 4.0f, cy, cx - 3.0f, cy + 4.0f);
                g.fillPath (flag);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto i = lineAtY (e.y);
        const auto jumps = i >= 0 && lyrics::jumpTick (owner.engine.getSong(), owner.lines[(std::size_t) i]).has_value();
        setMouseCursor (jumps ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (const auto next = jumps ? i : -1; next != hover)
        {
            hover = next;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            owner.jumpTo (lineAtY (e.y));
    }

private:
    LyricsPanel& owner;
    int hover = -1;
};

LyricsPanel::LyricsPanel (AudioEngine& e, Hooks h) : engine (e), hooks (std::move (h))
{
    setTitle (tr ("Lyrics and notes"));
    title.setText (tr ("Lyrics and notes"), juce::dontSendNotification);
    title.setFont (theme::font (theme::type::title, true));
    addAndMakeVisible (title);

    closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    closeButton.setTooltip (tr ("Close"));
    closeButton.onClick = [this] {
        if (hooks.close)
            hooks.close();
    };
    addAndMakeVisible (closeButton);
    floatButton.onClick = [this] {
        if (hooks.toggleFloating)
            hooks.toggleFloating();
    };
    addChildComponent (floatButton);
    floatButton.setVisible (hooks.toggleFloating != nullptr);
    setFloating (false);

    search.setFont (theme::font (theme::type::body));
    search.setIndents (8, 5);
    search.setTextToShowWhenEmpty (tr ("Search the notes"), theme::colour::textDisabled);
    search.setTitle (tr ("Search the notes"));
    search.onTextChange = [this] { searchChanged(); };
    search.onReturnKey = [this] { nextHit (juce::ModifierKeys::currentModifiers.isShiftDown() ? -1 : 1); };
    search.onEscapeKey = [this] {
        search.clear();
        searchChanged();
    };
    addAndMakeVisible (search);
    searchInfo.setFont (theme::font (theme::type::small));
    searchInfo.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    searchInfo.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (searchInfo);

    sectionButton.setButtonText (tr ("Section"));
    sectionButton.setTooltip (tr ("Adds a heading line, like [Chorus]"));
    sectionButton.onClick = [this] { showSectionMenu(); };
    addAndMakeVisible (sectionButton);
    markerButton.setButtonText (tr ("Marker at playhead"));
    markerButton.setTooltip (tr ("Gives this line the playhead's position and puts a marker there"));
    markerButton.onClick = [this] { markAtPlayhead (engine.songPlayheadTicks()); };
    addAndMakeVisible (markerButton);
    exportButton.setButtonText (tr ("Export"));
    exportButton.setTooltip (tr ("Saves the notes as a text file"));
    exportButton.onClick = [this] { showExportMenu(); };
    addAndMakeVisible (exportButton);

    editor.setMultiLine (true, true);
    editor.setReturnKeyStartsNewLine (true);
    editor.setScrollbarsShown (true);
    editor.setFont (theme::font (14.0f));
    editor.setLineSpacing (1.3f);
    editor.setIndents (8, 6);
    editor.setTitle (tr ("Lyrics and notes"));
    editor.setTextToShowWhenEmpty (tr ("Write lyrics or notes here.\n[Chorus] on its own line is a heading."), theme::colour::textDisabled);
    editor.onTextChange = [this] { textEdited(); };
    editor.onFocusLost = [this] { commit(); };
    addAndMakeVisible (editor);
    margin = std::make_unique<Margin> (*this);
    addAndMakeVisible (*margin);

    footer.setFont (theme::font (theme::type::caption));
    footer.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    footer.setMinimumHorizontalScale (0.8f);
    addAndMakeVisible (footer);

    refresh();
    whileShowing = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 8);
}

LyricsPanel::~LyricsPanel()
{
    whileShowing.reset();
    stopTimer();
    commit();
}

void LyricsPanel::setFloating (bool f)
{
    floating = f;
    floatButton.setButtonText (floating ? tr ("Dock") : tr ("Float"));
    floatButton.setTooltip (floating ? tr ("Docks the panel beside the Playlist") : tr ("Opens the panel in its own window"));
    closeButton.setVisible (! floating && hooks.close != nullptr);
    resized();
}

juce::String LyricsPanel::editorText() const { return normalised (editor.getText()); }

void LyricsPanel::refresh()
{
    if (dirty)
        return; // (what is being typed wins; it reaches the project at the next pause)
    const auto want = juce::String::fromUTF8 (engine.getSong().info.comments.c_str());
    if (want != editorText())
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto caret = editor.getCaretPosition();
        editor.setText (want, false);
        editor.setCaretPosition (juce::jmin (caret, want.length()));
    }
    reparse();
}

void LyricsPanel::reparse()
{
    const auto text = editorText();
    lines = lyrics::parse (text.toStdString());
    lineStartChars.clear();
    lineStartChars.push_back (0);
    for (int i = 0; i < text.length(); ++i)
        if (text[i] == '\n')
            lineStartChars.push_back (i + 1);
    searchChanged();
    updateFooter();
    if (margin != nullptr)
        margin->repaint();
}

void LyricsPanel::updateFooter()
{
    const auto text = editorText().toStdString();
    int written = 0;
    for (const auto& line : lines)
        if (! line.body.empty() && ! line.section)
            ++written;
    footer.setText (tr ("{0} lines, {1} words", { juce::String (written), juce::String (lyrics::wordCount (text)) }), juce::dontSendNotification);
}

void LyricsPanel::textEdited()
{
    if (updating)
        return;
    dirty = true;
    editedAtMs = juce::Time::getMillisecondCounterHiRes();
    reparse();
}

void LyricsPanel::commit()
{
    if (! dirty)
        return;
    dirty = false;
    const auto text = editorText().trimEnd().toStdString();
    if (text != engine.getSong().info.comments)
        engine.editSong ([&text] (Song& s) { s.info.comments = text; });
}

int LyricsPanel::caretLine() const
{
    const auto caret = editor.getCaretPosition();
    int line = 0;
    for (int i = 0; i < (int) lineStartChars.size(); ++i)
        if (caret >= lineStartChars[(std::size_t) i])
            line = i;
    return line;
}

void LyricsPanel::addSection (const juce::String& name)
{
    // a heading on its own line: before the caret's line when that line has text, else in its place
    const auto at = caretLine();
    const auto start = at < (int) lineStartChars.size() ? lineStartChars[(std::size_t) at] : editor.getTotalNumChars();
    const auto empty = at < (int) lines.size() && lines[(std::size_t) at].body.empty() && ! lines[(std::size_t) at].stamped;
    const auto heading = "[" + name + "]";
    editor.setCaretPosition (start);
    editor.insertTextAtCaret (empty ? heading : heading + "\n");
    editor.grabKeyboardFocus();
}

void LyricsPanel::markAtPlayhead (double ticks)
{
    commit();
    const auto line = caretLine();
    const auto tick = (std::int64_t) std::llround (juce::jmax (0.0, ticks));
    bool ok = false;
    engine.editSong ([&ok, line, tick] (Song& s) { ok = lyrics::markAtPlayhead (s, line, tick); });
    if (! ok)
        return;
    refresh();
    // the caret stays on the line, at its end
    if (line < (int) lines.size())
        editor.setCaretPosition ((line + 1 < (int) lineStartChars.size() ? lineStartChars[(std::size_t) line + 1] - 1 : editor.getTotalNumChars()));
}

void LyricsPanel::jumpTo (int line)
{
    if (line < 0 || line >= (int) lines.size())
        return;
    if (const auto tick = lyrics::jumpTick (engine.getSong(), lines[(std::size_t) line]))
    {
        engine.seek ((double) *tick);
        playingLine = line;
        margin->repaint();
    }
}

void LyricsPanel::searchChanged()
{
    hits.clear();
    hit = -1;
    const auto query = search.getText();
    if (query.isNotEmpty())
    {
        const auto text = editorText();
        for (int at = text.indexOfIgnoreCase (0, query); at >= 0; at = text.indexOfIgnoreCase (at + query.length(), query))
            hits.push_back ({ at, at + query.length() });
    }
    searchInfo.setText (query.isEmpty() ? juce::String() : hits.empty() ? tr ("No match") : tr ("{0} found", { juce::String ((int) hits.size()) }),
                        juce::dontSendNotification);
}

void LyricsPanel::nextHit (int step)
{
    if (hits.empty())
        return;
    hit = (hit + step + (int) hits.size()) % (int) hits.size();
    editor.setHighlightedRegion (hits[(std::size_t) hit]);
    searchInfo.setText (tr ("{0} of {1}", { juce::String (hit + 1), juce::String ((int) hits.size()) }), juce::dontSendNotification);
}

void LyricsPanel::showSectionMenu()
{
    juce::PopupMenu menu;
    // (song sections: "Chorus" and "Bridge" are other things elsewhere in LYNX, so each has its own translation)
    for (const auto& text : { trc ("@song section: Intro"), trc ("@song section: Verse"), trc ("@song section: Pre-chorus"), trc ("@song section: Chorus"),
                              trc ("@song section: Bridge"), trc ("@song section: Hook"), trc ("@song section: Outro") })
        menu.addItem (text, [this, text] { addSection (text); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (sectionButton));
}

void LyricsPanel::showExportMenu()
{
    juce::PopupMenu menu;
    menu.addItem (tr ("Text file, positions as written..."), [this] { exportAs (lyrics::Stamps::keep); });
    menu.addItem (tr ("Text file, positions as times..."), [this] { exportAs (lyrics::Stamps::times); });
    menu.addItem (tr ("Text file, without positions..."), [this] { exportAs (lyrics::Stamps::none); });
    menu.addSeparator();
    menu.addItem (tr ("Remove this line's position"), [this] {
        commit();
        const auto line = caretLine();
        engine.editSong ([line] (Song& s) { s.info.comments = lyrics::withoutStamp (s.info.comments, line); });
        refresh();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (exportButton));
}

void LyricsPanel::exportAs (lyrics::Stamps stamps)
{
    commit();
    const auto& song = engine.getSong();
    auto name = juce::String::fromUTF8 (song.info.title.c_str()).trim();
    if (name.isEmpty())
        name = tr ("Lyrics");
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (name) + ".txt");
    chooser = ui::makeFileChooser (tr ("Export the notes"), start, "*.txt");
    ui::launchChooser (chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                       [this, stamps] (const juce::FileChooser& fc) {
                           auto file = fc.getResult();
                           if (file == juce::File())
                               return;
                           if (! file.hasFileExtension ("txt"))
                               file = file.withFileExtension ("txt");
                           const auto text = lyrics::exportText (engine.getSong(), stamps);
                           if (! file.replaceWithText (juce::String::fromUTF8 (text.c_str()), false, false, "\n"))
                               juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Export the notes"),
                                                                       tr ("Could not write {0}", { file.getFileName() }));
                       });
}

void LyricsPanel::timerCallback()
{
    if (dirty && juce::Time::getMillisecondCounterHiRes() - editedAtMs > commitAfterMs && ! editor.hasKeyboardFocus (true))
        commit();
    else if (dirty && juce::Time::getMillisecondCounterHiRes() - editedAtMs > commitAfterMs * 3.0)
        commit(); // (still typing in bursts: the project follows every couple of seconds)
    // the line the song is at, and the margin following the editor's scrolling
    const auto ticks = snapshotTicks >= 0.0 ? snapshotTicks : engine.songPlayheadTicks();
    const auto now = lyrics::currentLine (engine.getSong(), lines, (std::int64_t) ticks);
    const auto scrollY = editor.getCaretRectangleForCharIndex (0).getY();
    if (now != playingLine || scrollY != lastScrollY)
    {
        playingLine = now;
        lastScrollY = scrollY;
        margin->repaint();
    }
}

void LyricsPanel::showForSnapshot (const juce::String& query, double playheadTicks)
{
    snapshotTicks = playheadTicks;
    search.setText (query, juce::dontSendNotification);
    searchChanged();
    if (! hits.empty())
        nextHit (1);
    else
        editor.setHighlightedRegion ({ editor.getCaretPosition(), editor.getCaretPosition() });
    playingLine = lyrics::currentLine (engine.getSong(), lines, (std::int64_t) playheadTicks);
    margin->repaint();
}

void LyricsPanel::setCaretLineForSnapshot (int index)
{
    if (index >= 0 && index < (int) lineStartChars.size())
        editor.setCaretPosition (lineStartChars[(std::size_t) index]);
}

void LyricsPanel::markAtPlayheadForSnapshot (double ticks) { markAtPlayhead (ticks); }

bool LyricsPanel::jumpToLineForSnapshot (int index)
{
    if (index < 0 || index >= (int) lines.size() || ! lyrics::jumpTick (engine.getSong(), lines[(std::size_t) index]))
        return false;
    jumpTo (index);
    return true;
}

juce::String LyricsPanel::textForSnapshot() const { return editorText(); }

void LyricsPanel::parentHierarchyChanged()
{
    if (getParentComponent() == nullptr)
        commit();
}

void LyricsPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg2);
    g.setColour (theme::colour::border);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void LyricsPanel::resized()
{
    if (margin == nullptr)
        return; // (still being built)
    auto r = getLocalBounds().reduced (12, 10);
    auto head = r.removeFromTop (28);
    if (closeButton.isVisible())
    {
        closeButton.setBounds (head.removeFromRight (28));
        head.removeFromRight (6);
    }
    if (floatButton.isVisible())
        floatButton.setBounds (head.removeFromRight (64).reduced (0, 1));
    title.setBounds (head);
    r.removeFromTop (6);
    auto find = r.removeFromTop (28);
    searchInfo.setBounds (find.removeFromRight (84));
    search.setBounds (find);
    r.removeFromTop (6);
    auto tools = r.removeFromTop (28);
    const auto gap = 6;
    const auto unit = (tools.getWidth() - 2 * gap) / 9;
    sectionButton.setBounds (tools.removeFromLeft (unit * 2));
    tools.removeFromLeft (gap);
    exportButton.setBounds (tools.removeFromRight (unit * 2));
    tools.removeFromRight (gap);
    markerButton.setBounds (tools);
    r.removeFromTop (8);
    footer.setBounds (r.removeFromBottom (18));
    r.removeFromBottom (4);
    margin->setBounds (r.removeFromLeft (marginWidth));
    editor.setBounds (r);
}
} // namespace lynx
