#pragma once

// Live set (.als) export, round 175a (N2). Format: docs/analise/ALS_FORMAT.md. Live 12 and Live 11 sets are written from
// LYNX's generated element skeletons (core/AlsSkeleton.h, tools/als_skeleton.py): Live's loader needs complete objects
// (§9), so each element starts as the skeleton of its kind and gets the song's values. The mapping:
//  - each channel with notes or a plugin → a MIDI track (its notes, pattern clip by pattern clip, as MIDI clips);
//  - each Playlist track with audio clips → an audio track (warp markers from the clip's stretch);
//  - a mixer insert shared by several tracks → a group track; an insert fed only by sends → a return track;
//    an insert with one track → that track's own mixer (volume, pan, mute, solo, send levels);
//  - third-party VST2 / VST3 instruments and effects → plugin devices with their state (byte for byte);
//  - tempo, time signature, markers (→ locators), the song loop, mixer and tempo automation.
//  - the clip grid (round 181c) → Session view: one scene per grid row, grid clips in their tracks' clip slots with
//    their launch settings and follow actions, stop buttons, scene names / tempo / meter, the global quantisation.
// LYNX's own instruments and effects are not converted yet (N3): they are listed in `notes`. Round 190ec: a Drum Rack goes
// out as Live's Drum Rack (pads, choke groups, sends, return chains), the Samplers on its pads as Simplers.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx
{
enum class AlsVersion
{
    live12,
    live11
};

struct AlsExportOptions
{
    AlsVersion version = AlsVersion::live12;
    std::string creator = "LYNX";
    // How a sample's path is written: the path the set refers to (absolute, or relative to the set's folder when
    // `relative` is set: then FileRef's RelativePath is used). Default: the path as LYNX has it, absolute.
    std::function<std::string (const std::string& path, bool& relative)> samplePath;
    // A sample's length in seconds (for the warp markers); 0 = unknown (the markers then span one second).
    std::function<double (const std::string& path)> sampleSeconds;
    // A sample's length in frames and its rate (round 190ec: a Drum Rack pad's Simpler region, SampleStart / SampleEnd in
    // frames); false = unknown (the whole file is written, a region is listed)
    std::function<bool (const std::string& path, double& frames, double& rate)> sampleFormat;
    // A loaded plugin's record (format, ids, state in FL's layout: PluginHost::flWrapperRecord). A plugin LYNX has
    // loaded no longer carries its `foreign` record; without this callback only unloaded records are exported.
    std::function<std::optional<ForeignPlugin> (const PluginRef& ref, bool instrument)> pluginRecord;
};

struct AlsExportResult
{
    std::vector<std::uint8_t> bytes; // the .als file (gzip)
    std::string xml;                 // the same, uncompressed (tests, debugging)
    std::vector<std::string> notes;  // what was left out or approximated (one line each)
};

AlsExportResult exportAls (const Song& song, const AlsExportOptions& options = {});

// True when Live can't read a sample file as it is and it has to be written as a plain WAV first (round 182b). Live picks
// the decoder by the file's extension and reads WAV (PCM / float), AIFF, FLAC, Ogg and MP3 only. FL ships Ogg Vorbis
// inside RIFF/WAVE (format tags 0x674F…0x6751) and also raw Ogg Vorbis streams ("OggS") named .wav: Live 12.4 reports
// "This file does not appear to be a valid WAV file" for both, and crashed loading a set full of them. `extension` is
// the file's extension without the dot (any case); `head` is the start of the file (4 KB is plenty).
bool alsSampleNeedsWav (const std::string& extension, const std::uint8_t* head, std::size_t size);
} // namespace lynx
