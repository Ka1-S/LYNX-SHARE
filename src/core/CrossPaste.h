#pragma once

#include <vector>

#include "core/Song.h"
#include "core/TrackClips.h"

// Issue #232, stage 2 (round 190nz, docs/design/PROJECT_TABS.md; a LYNX extra: FL Studio's clipboard stays inside one
// copy of the program): copy in one LYNX SHARE window, paste in another. What is copied travels as a "fragment", a Song
// that holds only what the copied things need (src/engine/CrossClipboard.h writes it as LYNX's JSON on the system
// clipboard); the other window merges it into its own project here. JUCE-free.
namespace lynx::crosspaste
{
// A channel the other project already has: the same name and the same sound source (instrument kind; the plugin for
// a plugin channel, the sample for a Sampler / audio clip channel). Its settings and plugin state may differ: an album's
// songs share their channels, each tuned for its song.
bool sameChannel (const ChannelDef& a, const ChannelDef& b);

// --- Playlist / clip grid clips ----------------------------------------------------------------------------------
// The fragment for `clips` (clips of `song`): the patterns they play (the copied track clips of `carried` first), the
// audio clip channels and automation clips they play, the channels the patterns' notes, loop points and event lanes use
// and the channels the automation clips drive, the audio clips' envelopes and the clips' grooves. The clips go into the
// fragment's playlist with every index into the fragment (and no uid). `sourceChannels`, when given, gets the song's
// channel index of each fragment channel (for their plugins' live states).
Song clipsFragment (const Song& song, const std::vector<Clip>& clips, const trackclips::Carried& carried = {},
                    std::vector<int>* sourceChannels = nullptr);
// Merges a fragment into `song` (one edit, e.g. inside AudioEngine::editSong) and returns its clips with indices into
// `song`, ready to be placed. Channels the song already has (sameChannel) are used as they are; the others are added at
// the end of the Channel Rack. A pattern or automation clip equal to one of the song's is that one (pasting the same
// thing twice adds nothing new); the rest is added: patterns, automation clips, clip envelopes, grooves (one with the
// same name and settings is reused). Mixer, VCA and tempo targets keep their numbers.
std::vector<Clip> importClips (Song& song, const Song& fragment);

// --- channels (Channel Rack › Copy channels / Paste channels) -----------------------------------------------------
// The fragment for the channels `channels` of `song` (in rack order): the channels with their instruments, plugin
// states and samples (paths), and the rack groups they are in.
Song channelsFragment (const Song& song, std::vector<int> channels);
// Adds the fragment's channels to `song` from index `at` on (clamped), as copies: a frozen channel plays its
// instrument, a pad of a Simple Sampler track becomes a plain channel, a Loop Starter channel an ordinary one, layer
// children stay among the pasted channels, rack groups are found by name (added when missing). Each keeps its mixer
// insert number when the song has it (else Master). Returns the new channels' indices.
std::vector<int> importChannels (Song& song, const Song& fragment, int at);
} // namespace lynx::crosspaste
