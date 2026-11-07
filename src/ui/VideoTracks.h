#pragma once

// The Playlist's video tracks (round 174d, core/VideoTrack.h, docs/analise/REA_rodada174d.md §4): LYNX's own, drawn
// like FL's Playlist. A section between the ruler and the clip tracks:
//  - a timecode row (SMPTE at the project's rate, from the project's start timecode)
//  - one lane per video track: FL-style header (name, mute LED, colour), clips drawn as a filmstrip under a title bar
//  - the divider under the lanes resizes the section (double-click: fit the lanes)
// Mouse on the lanes: drag a clip to move it (snapped to frames; Alt: free), drag its edges to trim, right click a clip
// to delete it (as FL's Playlist), double-click to open the track's video window, right click elsewhere for the menu.
// Video files dropped on the lanes (or anywhere on the Playlist) become clips where they are dropped.
// Each video track can show its picture in its own window (resizable, detachable, full screen, timecode burn-in).

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/VideoTrack.h"
#include "ui/ValueControl.h"

namespace lynx
{
class AudioEngine;
class PlaylistComponent;
struct Song;

namespace videotracks
{
bool isVideoFile (const juce::File& file);
// Adds `file` as a clip at song time `songSeconds` on video track `track` (−1: the first track with room, a new
// track when none has); one undo step. False when no decoder can read it (the user is told why).
bool importVideo (AudioEngine& engine, const juce::File& file, double songSeconds, int track = -1);
// File › Import › Video file...: a file chooser, then importVideo at the play position
void chooseAndImport (AudioEngine& engine, juce::Component* parent);
// The window of video track `track` (opened / brought to front)
void showWindow (AudioEngine& engine, int track);
// Closes the windows of tracks that no longer exist (the song changed)
void syncWindows (AudioEngine& engine);
// Something drawn over every video window's picture (the ADR cue signals: streamer, punch), at song time `seconds`;
// `picture` = where the frame is drawn. Set by the ADR window (empty: none).
using Overlay = std::function<void (juce::Graphics&, juce::Rectangle<int> picture, double seconds)>;
void setOverlay (Overlay overlay);

// Export with video: whether the song has a video clip to put the exported mix under
bool hasVideo (const Song& song);
// Writes `out` = the picture of the first unmuted video track's first clip, from song time `origin` (where the
// exported audio starts) for the audio's length, with `audio` as its sound (AAC). Needs the user's ffmpeg. The picture
// is copied as it is when the clip starts at `origin` from its file's start; otherwise it is re-encoded (black before
// the clip). Blocking (call it off the message thread). Empty result = done, else the error for the user.
juce::String muxWithVideo (const Song& song, double origin, const juce::File& audio, const juce::File& out);
// The file name of the video next to an exported audio file (the video's own container when stream copy keeps it)
juce::File videoFileFor (const Song& song, const juce::File& audio);
} // namespace videotracks

class VideoLanesView final : public juce::Component,
                             public juce::FileDragAndDropTarget,
                             public ui::HintSource,
                             private juce::Timer
{
public:
    VideoLanesView (AudioEngine& engine, PlaylistComponent& playlist);
    ~VideoLanesView() override;

    static constexpr int timecodeHeight = 16;
    static constexpr int defaultLaneHeight = 60;
    static constexpr int dividerHeight = 5;
    // the section's natural height for `song` (0 without video tracks)
    static int contentHeight (const Song& song);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    ui::Hint hintAt (juce::Point<int> position) override;

    void refresh(); // the song changed

private:
    void timerCallback() override;
    struct Hit
    {
        int track = -1, clip = -1;
        enum Part { none, body, head, tail, header, mute, divider, timecode } part = none;
    };
    Hit hitAt (juce::Point<int> p) const;
    int laneTop (int track) const;
    int laneHeight (int track) const;
    double secondsAt (int x) const;
    int xOf (double songSeconds) const;
    void showMenu (const Hit& hit, juce::Point<int> at);
    void showHeaderMenu (int track);
    void extractAudio (int track, int clip);

    AudioEngine& engine;
    PlaylistComponent& playlist;
    enum class Drag { none, move, head, tail, divider };
    Drag drag = Drag::none;
    int dragTrack = -1, dragClip = -1, dragStartHeight = 0;
    double grabOffset = 0.0;
    Hit hover;
    int lastPlayheadX = -1;
    videotrack::TempoMap map;
    bool thumbsWerePending = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VideoLanesView)
};
} // namespace lynx
