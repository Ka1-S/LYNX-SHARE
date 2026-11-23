#pragma once

#include <optional>

#include <juce_core/juce_core.h>

#include "core/Song.h"

namespace lynx
{
// Native project format: versioned JSON (.lynx).
// Notes are stored as compact arrays [start, length, channel, key, velocity].
namespace ProjectFile
{
constexpr const char* extension = ".lynx";
constexpr int currentVersion = 1;

// `stampVersion` writes "savedBy"; false when the text is hashed (a new version must not change what a hash says).
juce::String toJson (const Song& song, bool stampVersion = true);
// `savedBy` (optional) receives the version of LYNX SHARE that wrote the project ("" for projects from before round
// 177d, which did not write it): a newer one may hold things this build drops (safety::savedByNewerVersion).
std::optional<Song> fromJson (const juce::String& json, juce::String& error, juce::String* savedBy = nullptr);
// Same native JSON fields, retaining live model float precision for remote snapshots/transactions.
juce::String toJsonExact (const Song& song);

// Atomic (temporary file + rename), then read back: false, with `error`, unless the file on disk is what was written.
bool save (const Song& song, const juce::File& file, juce::String& error);
// `file` named as a project: "beat.v2" -> "beat.v2.lynx" (File::withFileExtension would make it "beat.lynx").
juce::File withProjectExtension (const juce::File& file);
std::optional<Song> load (const juce::File& file, juce::String& error, juce::String* savedBy = nullptr);
// Round 190mr (DF-141, owner decision F3): the window layout saved with a project, as FL keeps its windows' places in
// the .flp. View only, so it stays outside the Song: a save is given it, a load hands it back. `style` =
// layoutstyle::Style it belongs to; `text` = the workspace's layout text ("" = none: every project before this round).
struct WindowLayout
{
    int style = 0;
    juce::String text;
    // round 190oa (DF-130): the places of the project's channel / plugin windows (ui/WindowPlaces.h toVar; void = none)
    juce::var places;
};
bool save (const Song& song, const juce::File& file, juce::String& error, const WindowLayout& layout);
std::optional<Song> load (const juce::File& file, juce::String& error, juce::String* savedBy, WindowLayout* layout);
// the project text with the layout added at the end of its root object (unchanged when the layout is empty)
juce::String withWindowLayout (const juce::String& json, const WindowLayout& layout);
// the layout a project text holds (empty text: none)
WindowLayout windowLayoutOf (const juce::var& root);

// One mixer track as stored in a project (sends, input and effects included).
juce::var mixerTrackToVar (const MixerTrack& track);
MixerTrack mixerTrackFromVar (const juce::var& v);

// FL's "Save mixer track state as..." / "Open mixer track state..." (Mixer track menu › File), in LYNX's own format
// (JSON, extension `mixerTrackStateExtension`): the track's effects (with their states), level, pan, stereo
// separation, phase, swap, EQ, name and colour. Routing, audio input, mute and solo are not part of a state.
constexpr const char* mixerTrackStateExtension = ".lynxtrack";
juce::String mixerTrackStateToJson (const MixerTrack& track);
std::optional<MixerTrack> mixerTrackStateFromJson (const juce::String& json, juce::String& error);
// Puts a state on a track (its routing stays); `takeName` = also the state's name and colour ("Insert from state").
void applyMixerTrackState (MixerTrack& target, const MixerTrack& state, bool takeName);

// Round 190jf (B22): a channel / an effect slot as the project stores it (JSON bytes), for LYNX's own devices inside a
// DAWproject file; nullopt when the bytes are not such a state
std::vector<std::uint8_t> channelStateBytes (const ChannelDef& channel);
std::optional<ChannelDef> channelFromStateBytes (const std::vector<std::uint8_t>& bytes);
std::vector<std::uint8_t> effectStateBytes (const EffectSlot& slot);
std::optional<EffectSlot> effectFromStateBytes (const std::vector<std::uint8_t>& bytes);
} // namespace ProjectFile
} // namespace lynx
