#pragma once

// Writer for .flp (FL Studio) projects: the inverse of flp::parseProject (Flp.h). It writes an flp::Project the way
// FL Studio 2026 (26.1.x) saves one: the same events, in the same order, with FL's own defaults for everything the
// model leaves out (docs/analise/REA_rodada171e.md §1, from FL 2026's own saved projects and templates).
// Does not depend on JUCE, like the reader.

#include <cstdint>
#include <string>
#include <vector>

#include "flp/Flp.h"

namespace flp
{
struct WriteOptions
{
    // what the file says saved it (event 0xC7 and the build number 0x9F); FL 2026 opens projects of its own version
    // without the "made with a newer version" question
    std::string version = "26.1.7.5654";
    std::uint32_t build = 5654;
    // mixer track records written (Master + inserts + FL's "current" track): FL 2026's default mixer is 127; more when
    // the project uses more inserts (FL 2026 has up to 500)
    int minMixerTracks = 127;
};

// The whole file (FLhd + FLdt). Channels, patterns, arrangements and mixer inserts are written in the order of their
// vectors; their `index` fields are the indices FL sees.
std::vector<std::uint8_t> writeProject (const Project& project, const WriteOptions& options = {});

// FL 2026's default records, as its own saved projects hold them (exposed for tests and for the exporter)
std::vector<std::uint8_t> defaultChannelParams();          // 0xD7, 168 bytes
std::vector<std::uint8_t> defaultEnvelope (int record);    // 0xDA, 68 bytes: 0 pan, 1 volume, 2 cutoff, 3 resonance, 4 pitch
std::vector<std::uint8_t> defaultPlaylistTrack (int index); // 0xEE, 70 bytes (index 0 = the first track)

// Text of a data event as FL 2026 writes it: UTF-16LE with the terminating zero.
std::vector<std::uint8_t> utf16Text (const std::string& utf8);
} // namespace flp
