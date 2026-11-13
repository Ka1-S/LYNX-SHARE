#pragma once

// Loops follow the project key (round 190ds; BandLab study /mnt/e/lynx-work/bandlab190p/STUDY.md §1.5 item F, rows
// looper-007 / editing-008). LYNX's own design, clean-room:
//  - an audio file's key is read from its name ("Dm", "G minor", "F#maj", or a bare "C" beside a tempo, as loop packs
//    name their files) or, when the name has none, detected from the audio (a pitch-class profile of its spectrum
//    against major and minor key profiles; percussion and noise give no key);
//  - the key is kept on the file's channel (SamplerSettings::loopKeyRoot / loopKeyMinor / loopKeySource);
//  - each audio clip has a "follow the project key" switch (Clip::followKey). A following clip is moved to the project
//    key by the nearest interval (−6 … +5 semitones between relative-major roots) through the clip's own pitch, which
//    keeps the clip's length (the voice's pitch shifter, core/TimeStretch's VoicePitchShifter, LYNX's own stretch engine);
//    projectkey::set re-tunes every following clip whenever the key changes (core/ProjectKey.h);
//  - off for everything that exists (old projects sound the same); on for loops dropped in the Simple workflow.
// No JUCE.

#include <optional>
#include <string>
#include <vector>

#include "core/ProjectKey.h"
#include "core/Song.h"

namespace lynx
{
struct SampleData;
}

namespace lynx::keyfollow
{
struct LoopKey
{
    int root = 0; // 0 = C … 11 = B
    bool minor = false;
    bool operator== (const LoopKey&) const = default;
};

// "D minor", "F# major" (English; the UI translates "major" / "minor")
std::string keyName (const LoopKey& key);

// The key written in a file's name (without folder; the extension is ignored), or nullopt. Tokens are split at
// anything but letters, digits and '#'. A token is a key when it is a note (A–G, then #, b, "sharp" or "flat") followed
// by m / min / minor / maj / major (in the token or as the next token), e.g. "Dm", "Ebmaj", "C#min", "A minor". A bare
// upper-case note ("C", "F#") counts only when the name also holds a tempo (a number from 60 to 200), as loop packs
// write "Keys_120_C"; a lower-case note without a mode word never does ("am" is a word).
std::optional<LoopKey> keyInName (const std::string& name);

struct Detection
{
    std::optional<LoopKey> key; // nullopt: no clear key (percussion, noise, too short, too ambiguous)
    float correlation = 0.0f;   // the best key's profile correlation (−1 … 1)
    float margin = 0.0f;        // over the best key with other notes (a different relative-major root)
    std::vector<float> chroma;  // the 12 pitch-class weights, C first, normalised to a sum of 1
};

// The key of the audio itself: up to 30 s of it, mixed to mono, as a pitch-class profile (8192-point spectra every
// 4096 frames, 55 Hz – 2.1 kHz, each bin's magnitude above its neighbourhood's median added to its pitch class), scored
// against the Krumhansl–Kessler major and minor profiles on the 12 roots. A key is given when the best correlation is
// at least 0.6 and at least 0.08 above every key with other notes (its relative major / minor has the same notes and
// the same move, so it doesn't count against it).
Detection detectKey (const SampleData& audio);

// The loop key of channel `channel` (nullopt when not known)
std::optional<LoopKey> loopKeyOf (const Song& song, int channel);

// Sets the loop key of `channel` (nullopt: not known) and how it was found, then re-tunes the clips that follow.
void setLoopKey (Song& song, int channel, std::optional<LoopKey> key, int source);

// Switches "follow the project key" on or off for these clips of the current Playlist (indices into Song::playlist)
// and re-tunes them. A clip whose channel has no loop key keeps the switch but doesn't move.
void setFollow (Song& song, const std::vector<int>& clips, bool on);

// The semitones a following clip moves by now (0 when it doesn't follow, or nothing is known)
int shiftOf (const Song& song, const Clip& clip);

// What a dropped keyed loop does to a project (Simple workflow): with no project key the user is asked whether to set
// it to the loop's (the loop then plays as it is either way); with a key the loop is moved to it.
inline bool asksToSetKey (const Song& song) { return ! projectkey::get (song).has_value(); }

// The project key for a loop key: its root with Major or Minor (FL's scale list)
projectkey::Key projectKeyFor (const LoopKey& key);
} // namespace lynx::keyfollow
