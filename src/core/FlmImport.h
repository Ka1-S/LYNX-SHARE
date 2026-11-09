#pragma once

// FL Studio Mobile project (.flm) import, round 182c (format: docs/analise/FLM_FORMAT.md; plan: REA_rodada182c.md §5).
// A LYNX-own import command (FL Studio has no native .flm import), clean-room. The project becomes a LYNX song the way an
// .flp or .als does, and takes the same pipeline (samples, report, Missing files window):
//  - slice F1: tempo, time signature, title; one mixer insert per FLM channel; one Playlist track per lane;
//    note lanes → the channel's patterns (one per FLM content id) and pattern clips (each loop pass a clip);
//    drum lanes → one sampler channel per kit sample; audio lanes → audio-clip channels and audio clips;
//    sample paths: "@L@" = the .flm's folder (an FLM share-zip keeps the library tree), "@R@" = FL Studio's copy of
//    the app's resources; missing files are reported;
//  - slice F3: FLM's DW Sampler with a single sample → a LYNX sampler channel (its key range, the module's sample);
//  - slice F2 (182c, 187d): the rack's level, pan, mute and solo on the channel's insert, its held pitch bend on a
//    sampler's pitch, the SEND routes;
//  - slice F6 (182c, 187d): the rack's Level, Pan and send-level automation lanes as automation clips;
//  - the rest is listed in the report: the other generators play LYNX's built-in synth for now (F4), effects are not
//    imported yet (F5), nor automation of their parameters.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/FlmFormat.h"
#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
struct FlmImportOptions
{
    // The .flp import's options that apply here: projectFolder (the .flm's folder = FLM's "@L@" library), fileExists,
    // relocations, report. FL's path roots are not used.
    FlpImportOptions common;
    // FLM's "@R@" resources: FL Studio's "Plugins/Fruity/Generators/FL Studio Mobile/Installed" ("" = none)
    std::string resourceRoot;
};

// FLM's resources inside an FL Studio installation folder (FlPathRoots::factoryData), or "" when there is none.
std::string flmResourceRootIn (const std::string& flInstallFolder);

bool isFlmData (const std::uint8_t* data, std::size_t size);

// The project as a LYNX song, or nullopt with `error` (not an FLM project, damaged).
std::optional<Song> importFlm (const std::vector<std::uint8_t>& fileBytes, const FlmImportOptions& options = {}, std::string* error = nullptr);
std::optional<Song> importFlmProject (const flm::Project& project, const FlmImportOptions& options = {});
} // namespace lynx
