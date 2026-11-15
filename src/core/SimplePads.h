#pragma once

// The Simple workflow's Sampler track (round 190fo; facts: /mnt/e/lynx-work/reabl190fo/BANDLAB-FACTS.md §1): 16 pads in a
// 4 × 4 grid, each holding one sound, played by a click, by the typing keys or by notes, and recorded like any track.
// BandLab's web Studio has this "multipad sampler" (pad modes Gate / One-shot / Loop, attack, release, tone, pitch ±12,
// pan, volume, a choke group of 4, colour, crop, reverse, normalize, copy / move); LYNX builds it from what FL already
// has, so every layout, the .flp and the .als exports read it as an ordinary project:
//  - the track is FL's audio track linked to one insert, like the Drums track (core/SimpleTracks.h decision D3);
//  - each pad is one FL Sampler channel in the track's Channel Rack group, playing through that insert; a pad's notes
//    are that channel's notes at C5 in the track's pattern (FL's own way of writing a drum part);
//  - a pad's settings are the channel's own fields (sample, region, reverse, attack, pitch, gain, volume, pan, cut
//    groups); ChannelDef::SimplePad keeps the few a channel has no field for (mode, tone, release, group, colour) and
//    `apply` turns them into the channel's settings, so the sound never depends on the UI.
// No JUCE.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::simplepads
{
constexpr int padCount = 16;
constexpr int padKey = 60;      // the note a pad channel plays its sample at (FL's C5)
constexpr int groupCount = 4;   // choke groups 1…4 (0 = none)
constexpr int colourCount = 8;  // pad colours 1…8 (0 = the track's)
constexpr float maxAttack = 1.0f, maxRelease = 4.0f, maxPitch = 12.0f;

enum class Mode : std::uint8_t { gate = 0, oneShot = 1, loop = 2 };
inline Mode modeOf (const ChannelDef& c) noexcept { return c.simplePad.mode <= 2 ? (Mode) c.simplePad.mode : Mode::gate; }
const char* modeName (Mode mode) noexcept; // "Gate", "One-shot", "Loop" (English, for tr())

// The typing key that plays pad `pad` (0…15, bottom-left first): four rows of the keyboard, bottom to top
// V B N M / F G H J / R T Y U / 4 5 6 7 (the pad grid's own shape). 0 for a pad out of range.
char typingKey (int pad) noexcept;
// The pad a typing key plays (any case), −1 when the key is not one of them.
int padForKey (int keyCode) noexcept;

// Tone: one knob from a low-pass (below 0) through off (0) to a high-pass (above 0). LYNX's law: an even sweep per
// octave, low-pass 22.05 kHz → 100 Hz, high-pass 20 Hz → 8 kHz (the same ends as the original's).
struct Tone
{
    bool on = false;
    int filterType = 0; // SamplerSettings::filterType: 0 low-pass, 2 high-pass
    float cutoff = 20000.0f;
};
Tone toneFilter (float tone) noexcept;
std::string toneText (float tone); // "Off", "Low-pass 1.2 kHz", "High-pass 300 Hz" (English; the UI translates the words)

// Puts a pad's own settings into its channel's sampler: the mode (gate: a short release at least; one-shot: plays the
// region to its end; loop: the region repeats while the note is held, through one zone whose loop is the region), the
// release, the tone filter and the choke group (a cut group of this insert only). `sampleFrames` is the sample's
// length (0 = unknown: a loop pad then plays as a gate pad).
void apply (ChannelDef& channel, std::int64_t sampleFrames);

// The 16 empty pads of a new Sampler track (no sample yet; the caller adds them as the track's kit): Sampler channels
// named "Pad 1"…"Pad 16".
std::vector<ChannelDef> emptyKit();

// The channel of pad `pad` on Sampler track `track`, −1 when there is none.
int padChannel (const Song& song, int track, int pad);
// The channels of the track's pads by pad index (−1 for a missing pad), always padCount entries.
std::vector<int> padChannels (const Song& song, int track);
// True when the pad has a sample.
bool hasSample (const Song& song, int track, int pad);

// Edits (one undoable edit each in the UI). Each returns false when the track or the pad is not there.
// A sample on a pad: the channel's sample and name; region, reverse, gain and the pad's tone / pitch reset (volume, pan,
// group and colour stay: they belong to the pad, as in the original). The mode follows the sound when its length is
// known (`seconds` > 0): a short hit (up to oneShotSeconds) plays One-shot, anything longer Gate — the original marks its
// library's one-shots and loads them One-shot, a user's file carries no such mark, so its length stands in for it.
// `seconds` = 0 keeps the pad's mode.
constexpr double oneShotSeconds = 1.5;
bool loadSample (Song& song, int track, int pad, const std::string& path, const std::string& name, std::int64_t sampleFrames,
                 double seconds = 0.0);
bool clearPad (Song& song, int track, int pad);
// Copies pad `from` (sound and settings) onto pad `to`.
bool copyPad (Song& song, int track, int from, int to, std::int64_t sampleFrames);
// Swaps two pads (sound, settings and notes go with the pad: the pads' places change, the music doesn't).
bool swapPads (Song& song, int track, int a, int b);

// Normalize: the gain (dB, within the Sampler's ±24) that brings `peak` (linear, of the played region) to 0 dBFS.
float normalizeGainDb (float peak) noexcept;
} // namespace lynx::simplepads
