#pragma once

#include <functional>
#include <vector>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
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
// The menu of a slot (the right-click menu; --gridshot draws it).
juce::PopupMenu clipGridSlotMenu (juce::Component& grid, int track, int scene);
// A clip dragged out of a slot (the Playlist takes it as a clip of its own): true and its slot when `description` is one.
bool clipGridDrag (const juce::var& description, int& track, int& scene);
// The grid's selected track and scene ({ 0, 0 } before any click).
std::pair<int, int> clipGridSelection (juce::Component& grid);
// Round 190dr: the Session mixer under the slots (ui/SessionMixer.h), and the column a dragged device would land in
// (-1: none), for --liveshot190dr.
SessionMixerComponent* clipGridMixer (juce::Component& grid);
void setClipGridDeviceDropForSnapshot (juce::Component& grid, int track);

// Round 190ef (docs/analise/REA_rodada190ef.md): the reference's View › "Scene Tempo And Time Signature" (a preference,
// kept with the UI settings: each scene shows its tempo and time signature fields); refreshClipGrid after a change; the
// listener hears the grid's own switch (in its options menu).
bool& clipGridSceneTempoShown();
void refreshClipGrid (juce::Component& grid);
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
