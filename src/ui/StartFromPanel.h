#pragma once

// "Start from" (round 190ch, LYNX's own): the Simple layout's list of what a new project can start from — LYNX's demo
// song (core/ShowcaseSong.h) and the templates already there (LYNX's default song, the built-in templates, the user's
// own .lynx files in Documents/LYNX/Templates). Nothing new is shipped with it: it is a picker with a preview.
//
// ▶ plays the first bars of an entry (rendered offline in the background, then played through the Browser's preview
// output) without opening it; Open (or a double click) starts a new, untitled project from it.

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Song.h"
#include "core/Templates.h"

namespace lynx
{
class AudioEngine;

namespace startfrom
{
struct Entry
{
    enum class Kind { demo, lynxDefault, builtIn, user };
    Kind kind = Kind::demo;
    juce::String name;      // shown
    TemplateChoice choice;  // (not for the demo)
    int demo = 0;           // which demo song (core/DemoSongs.h; round 190dv: 0 the showcase, 1 hip hop, 2 pop)
    double bpm = 0.0;       // 0 = not read yet (a user's file is read when it is first needed)
    int channels = 0, bars = 0;
    bool playable = false;  // it has notes or clips: there is something to preview
};

// Everything the list offers, in order: the demo songs (round 190dv: three), LYNX's default song, the built-in templates, the user's.
std::vector<Entry> entries();
// The song of an entry; nullopt when a user's file is gone or unreadable.
std::optional<Song> songOf (const Entry& entry);
// Fills an entry's tempo, size and whether it can be previewed from its song.
void describe (Entry& entry, const Song& song);
// The first bars of a song as a 16-bit WAV file (at most `bars` bars, the Playlist when it has clips, else the
// current pattern). Any thread but the audio thread. False when nothing could be rendered.
bool renderPreview (const Song& song, const juce::File& file, int bars = 8);
} // namespace startfrom

class StartFromPanel final : public juce::Component,
                             private juce::Timer
{
public:
    struct Hooks
    {
        std::function<void (Song song)> open; // starts a new project from the song
        std::function<void()> close;          // ✕, Esc, a click outside the card
    };

    StartFromPanel (AudioEngine& engine, Hooks hooks);
    ~StartFromPanel() override;

    void stopPreview();

    // --bandlabshot
    void selectForSnapshot (int index);
    int entryCountForSnapshot() const noexcept { return (int) list.size(); }
    bool openForSnapshot (int index);

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    class Row;
    struct Shared;
    juce::Rectangle<int> card() const;
    void select (int index);
    void open (int index);
    void togglePreview (int index);
    void timerCallback() override;
    void updateRows();

    AudioEngine& engine;
    Hooks hooks;
    std::vector<startfrom::Entry> list;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Component rowHolder;
    juce::Viewport viewport;
    juce::Label title, hint;
    juce::TextButton closeButton, openButton;
    std::shared_ptr<Shared> shared;
    juce::File previewFile;
    int selected = 0, previewing = -1, rendering = -1;
    bool heard = false; // the preview was seen sounding (its end is then its going silent)
};
} // namespace lynx
