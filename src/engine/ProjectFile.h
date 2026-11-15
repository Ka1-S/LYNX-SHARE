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
} // namespace ProjectFile
} // namespace lynx
