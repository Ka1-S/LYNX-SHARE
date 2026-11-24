#pragma once

#include <functional>
#include <vector>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
enum class RemoteAction : std::uint8_t;
class SessionMixerComponent;

// The clip grid of the Clip-launch workflow (round 181a, core/ClipLauncher.h): Playlist tracks as columns, scenes as rows.
// Own design and art. Made only the first time the grid is shown; never in Pattern workflow. It repaints from a timer that
// runs only while it is on screen.
std::unique_ptr<juce::Component> makeClipGrid (AudioEngine& engine);

// What the grid tells the Detail View (round 183a): a slot selected (click or arrow keys; a click on a column's title
// selects that track), a filled slot double-clicked (show the clip), a column's title double-clicked (show its devices).
enum class GridPick { select, openClip, openTrack };
void setClipGridPickListener (juce::Component& grid, std::function<void (int track, int scene, GridPick pick)> listener);
// Round 190bv: shows or hides the Groove Pool under the grid (ui/GroovePoolComponent.h).
void setClipGridGroovePoolShown (juce::Component& grid, bool shown);
// Round 190iv (design190ia CL-25): the footer row of buttons. Live's look (the Clip-launch control bar) hides it: Back to
// Arrangement, Session Record and the launch quantisation are in the control bar, Stop All Clips in the Main column's
// status cell, the Groove Pool and the mixer sections in the View and "⋯" menus. Shown again as an option.
void setClipGridButtonRowShown (juce::Component& grid, bool shown);
bool clipGridGroovePoolShown (juce::Component& grid);
void addClipGridMixerViewItems (juce::Component& grid, juce::PopupMenu& menu);
// The menu of a slot (the right-click menu; --gridshot draws it).
juce::PopupMenu clipGridSlotMenu (juce::Component& grid, int track, int scene);
// A clip dragged out of a slot (the Playlist takes it as a clip of its own): true and its slot when `description` is one.
bool clipGridDrag (const juce::var& description, int& track, int& scene);
// The grid's selected track and scene ({ 0, 0 } before any click).
std::pair<int, int> clipGridSelection (juce::Component& grid);
// Hotfix 0.1.31.1 (Live: one selected track for the Session and the Arrangement): the track chosen in the Arrangement
// becomes the grid's selected track, as a click on its column title (the pick listener hears GridPick::select, so the
// Device View shows that track's devices)
void selectClipGridTrack (juce::Component& grid, int track);
// round 190lr (DL-109): the tracks selected together with Shift / Ctrl+click on their titles (one: the selected track)
std::vector<int> clipGridSelectedTracks (juce::Component& grid);
void clipGridSelectTracksForSnapshot (juce::Component& grid, std::vector<int> tracks);
// Round 190dr: the Session mixer under the slots (ui/SessionMixer.h), and the column a dragged device would land in
// (-1: none), for --liveshot190dr.
SessionMixerComponent* clipGridMixer (juce::Component& grid);
void syncClipGridMixerForSnapshot (juce::Component& grid); // (round 190ou: the Session mixer's columns, as the grid's timer sets them)
void setClipGridDeviceDropForSnapshot (juce::Component& grid, int track);

// Round 190ef (docs/analise/REA_rodada190ef.md): the reference's View › "Scene Tempo And Time Signature" (a preference,
// kept with the UI settings: each scene shows its tempo and time signature fields); refreshClipGrid after a change; the
// listener hears the grid's own switch (in its options menu).
bool& clipGridSceneTempoShown();
// Round 190ld: Live's scene-launch preferences (Main track menu): select the next scene on launch, record on launch (DL-36)
bool& clipGridSelectNextOnLaunch();
bool& clipGridRecordOnSceneLaunch();
// Round 190ig: View › "Classic LYNX look for the clip grid" (a preference, off by default: the grid takes Live's look in the
// Clip-launch layout; on: round 181a's look); refreshClipGrid after a change.
bool& clipGridClassicLook();
// Round 190ig (--design190igs4shot): draw the waiting launches' blink in one phase (0 dark, 1 lit; -1: by the clock, as normal).
void setClipGridBlinkPhaseForSnapshot (int phase);
// Round 190im (--details190imshot): a scene launched as its button launches it (its pulse, the next scene selected); one
// tick of the grid's timer; a cell's rectangle (kind 0 a slot, 1 a track's status cell, 2 a scene cell); the selected
// scene and whether the scene is the selection; a slot selected; the hint at a point (text, value); a key pressed.
void launchClipGridSceneForSnapshot (juce::Component& grid, int scene);
// Round 190kc: Live's Relative Session Mapping Strip acting on the grid (Scene Up / Down, Launch Selected Scene, a track's
// clip at the selected scene: press and release).
void clipGridRelativeSession (juce::Component& grid, RemoteAction action, int track, bool press);
void setClipGridMapStrip (juce::Component& grid, bool on); // (shown while a map mode is on)
void tickClipGridForSnapshot (juce::Component& grid);
// Round 190kn (DL-7): Live's Cancel Scene Launch (the scene menu's item) and whether a launched scene still waits.
void cancelClipGridSceneLaunch (juce::Component& grid);
bool clipGridSceneLaunchQueued (juce::Component& grid);
juce::Rectangle<int> clipGridCellForSnapshot (juce::Component& grid, int kind, int track, int scene);
// round 190lr (--details190lrshot): Live's hover (0 none, 1 a slot's button, 2 a track's Stop, 3 a scene's ▶), a rectangle
// selection of slots and how many slots it selects
void clipGridHoverForSnapshot (juce::Component& grid, int kind, int track, int scene);
void clipGridSelectRangeForSnapshot (juce::Component& grid, int track0, int scene0, int track1, int scene1);
int clipGridSelectedCountForSnapshot (juce::Component& grid);
std::pair<int, bool> clipGridSceneSelectionForSnapshot (juce::Component& grid);
void selectClipGridSlotForSnapshot (juce::Component& grid, int track, int scene);
std::pair<juce::String, juce::String> clipGridHintForSnapshot (juce::Component& grid, juce::Point<int> p);
bool clipGridKeyForSnapshot (juce::Component& grid, const juce::KeyPress& key);
void refreshClipGrid (juce::Component& grid);
// Round qafix190kq (QA-2): the Playlist track of each grid column, left to right (take lanes have none); --gridshot
// lists them.
std::vector<int> clipGridColumnTracks (juce::Component& grid);
void setClipGridSceneFieldsListener (juce::Component& grid, std::function<void()> listener);
// The menus of a scene, a column title and a scene field (1 tempo, 2 time signature), as a right-click opens them
// (--sceneshot190ef draws them).
juce::PopupMenu clipGridSceneMenu (juce::Component& grid, int scene);
juce::PopupMenu clipGridHeaderMenu (juce::Component& grid, int track);
juce::PopupMenu clipGridSceneFieldMenu (juce::Component& grid, int scene, int field);
// --sceneshot190ef: select a scene; Duplicate it; Capture and Insert Scene with the given rows playing per track (nothing
// plays off-screen). The new row, −1: none.
void selectClipGridScene (juce::Component& grid, int scene);
// round 190em: select a scene's field (1 tempo, 2 / 3 the time signature), as a click on it does (Del then returns it to
// its default)
void selectClipGridSceneField (juce::Component& grid, int scene, int field);
int duplicateClipGridScene (juce::Component& grid, int scene);
int captureClipGridScene (juce::Component& grid, int scene, const std::vector<int>& playing);
// Is `component` the clip grid or inside it? (the Playlist starts a clip's drag out over it)
bool isClipGrid (const juce::Component* component);
// A Playlist clip dragged out of the Playlist (index into the current playlist): its drag description, and back.
juce::String playlistClipDragDescription (int index);
bool playlistClipDrag (const juce::var& description, int& index);
} // namespace lynx
