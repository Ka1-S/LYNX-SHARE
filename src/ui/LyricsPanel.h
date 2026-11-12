#pragma once

// The Lyrics / Notes panel (round 190ch, LYNX's own; the text and its rules are core/Lyrics.h). It edits the project's
// Comments (Settings › Info shows the same text), so the notes travel in .lynx and to and from .flp.
//
//   left margin   a line with a position shows it ("9.3"); a click moves the playhead there. A heading whose name is a
//                 Playlist marker's shows a jump mark. The line the song is at is lit while it plays.
//   Section       adds a heading line ([Verse], [Chorus]...) at the caret
//   Marker        "Marker at playhead": the caret's line takes the playhead's position, and a Playlist marker named
//                 after the line is put there
//   Search        finds the text as you type; Enter goes to the next place
//   Export        the notes as a .txt file: positions as written, as times, or left out
//
// One panel for every layout: docked on the right of the Playlist, or floating in its own window (the button in its
// title row switches). Its timer runs only while it is shown.

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Lyrics.h"
#include "ui/TimerWhileShowing.h"

namespace lynx
{
class AudioEngine;

class LyricsPanel final : public juce::Component,
                          private juce::Timer
{
public:
    static constexpr int width = 340;

    struct Hooks
    {
        std::function<void()> close;          // ✕ (docked)
        std::function<void()> toggleFloating; // dock ↔ own window
    };

    LyricsPanel (AudioEngine& engine, Hooks hooks);
    ~LyricsPanel() override;

    // the song changed (undo, another project, Settings › Info): the panel shows the project's text
    void refresh();
    // whether it sits in its own window (the title row's button shows the other choice; no ✕ then)
    void setFloating (bool floating);
    // writes what was typed into the project now (before a save, before the panel goes)
    void commit();

    // --snapshot / --bandlabshot: a search and the playing line as drawn, without a transport
    void showForSnapshot (const juce::String& search, double playheadTicks);
    // (tests of the UI route) the caret's line, "Marker at playhead" at `ticks`, a click on line `index`'s margin
    void setCaretLineForSnapshot (int index);
    void markAtPlayheadForSnapshot (double ticks);
    bool jumpToLineForSnapshot (int index);
    juce::String textForSnapshot() const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override; // taken out of its dock: what was typed is kept

private:
    class Margin;
    void timerCallback() override;
    void textEdited();
    void reparse();
    int caretLine() const;
    void addSection (const juce::String& name);
    void markAtPlayhead (double ticks);
    void jumpTo (int line);
    void searchChanged();
    void nextHit (int step);
    void showSectionMenu();
    void showExportMenu();
    void exportAs (lyrics::Stamps stamps);
    void updateFooter();
    juce::String editorText() const;

    AudioEngine& engine;
    Hooks hooks;
    juce::Label title, searchInfo, footer;
    juce::TextButton closeButton, floatButton, sectionButton, markerButton, exportButton;
    juce::TextEditor search, editor;
    std::unique_ptr<Margin> margin;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ui::TimerWhileShowing> whileShowing;

    std::vector<lyrics::Line> lines;  // of the editor's text
    std::vector<int> lineStartChars;  // each line's first character in the editor
    std::vector<juce::Range<int>> hits;
    int hit = -1, playingLine = -1, lastScrollY = 0;
    double editedAtMs = 0.0, snapshotTicks = -1.0;
    bool dirty = false, updating = false, floating = false;
};
} // namespace lynx
