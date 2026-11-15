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
//  - slice F5 (188f): Equalizer, Graphic EQ, Filter, Autoduck (legacy), Reverb (legacy) and Reverb 2 on the insert
//    as LYNX effects (core/FlmEffects.h), with their parameters' automation lanes;
//  - the rest is listed in the report: the other generators play LYNX's built-in synth for now (F4), the other effects
//    (MultiFX and the rest of FLM_FORMAT §7) are listed.

#include <cstdint>
#include <functional>
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
    // Round 190fn (decision O1): the user's own copies of FL Studio Mobile's "Resources" folder (Settings › Files),
    // searched before `resourceRoot`. FL's copy holds only part of the app's factory content (77 of the 168 files the
    // demo songs use, no EXP packs), and users without FL have none: LYNX never ships Image-Line content.
    std::vector<std::string> userResourceRoots;
};

// FLM's resources inside an FL Studio installation folder (FlPathRoots::factoryData), or "" when there is none.
std::string flmResourceRootIn (const std::string& flInstallFolder);

// Round 190fn (O1): the folder a user picked as FL Studio Mobile's resources → the folder "@R@" paths are relative to.
// The app keeps its factory content (Drum Samples, DirectWave Samples, Synth Presets2, oscdata.dat, …) in a folder
// named "Resources" (FL's copy: "…/FL Studio Mobile/Installed"): a pick of that folder is kept; a pick of the folder
// above it, or of an FL Studio install folder, goes down to it. Anything else is kept as picked (a partial copy).
std::string flmResourceRootFrom (const std::string& picked, const std::function<bool (const std::string&)>& exists);
// Whether `folder` holds FL Studio Mobile factory content (one of the app's own resource folders or data files).
bool looksLikeFlmResources (const std::string& folder, const std::function<bool (const std::string&)>& exists);

bool isFlmData (const std::uint8_t* data, std::size_t size);

// The project as a LYNX song, or nullopt with `error` (not an FLM project, damaged).
std::optional<Song> importFlm (const std::vector<std::uint8_t>& fileBytes, const FlmImportOptions& options = {}, std::string* error = nullptr);
std::optional<Song> importFlmProject (const flm::Project& project, const FlmImportOptions& options = {});

// Round 190fn (FLM coverage instruments-0022): the "FL Studio Mobile Rack" generator of an FL project (.flp; FLM's own
// "Save as FLP" writes every instrument track as one, FLM_FORMAT §8). Its plugin state (event 0xD5) is "SSLF" + an FLM
// project body ("10LF" HEAD RACK) holding one rack: the rack is imported as a one-channel FLM song with the same code
// as a .flm. `channel` = the rack's instrument on LYNX's engine (FL's channel settings are the caller's to keep);
// `insert` = the rack's level, pan and effects, as FLM's own rack applies them after the instrument; `insertUsed` when
// the insert changes the sound. nullopt (`why` = the report's line) when the state is not a rack or LYNX cannot rebuild
// it yet (a drum kit, several instruments, rack parts, note effects): the caller keeps its stand-in.
// Round 190fn (FLM coverage effects-0048): the "FL Studio Mobile Rack FX" effect of an .flp mixer slot. Its state has the
// same layout as the generator's, with effect modules only: they become one LYNX container slot (one branch: the modules'
// slots in the rack's order, at the rack's level; its pan as a utility after them; switched off when the rack is muted),
// each module on the same engine as in a .flm. nullopt when the state is not a rack or nothing in it maps (listed).
std::optional<EffectSlot> flmRackFxSlot (const std::vector<std::uint8_t>& pluginState, FlpImportReport* report);

struct FlmRackChannel
{
    ChannelDef channel;
    MixerTrack insert;
    bool insertUsed = false;
};
std::optional<FlmRackChannel> importFlmRackState (const std::vector<std::uint8_t>& pluginState, const FlmImportOptions& options,
                                                  double bpm, const std::string& channelName, std::string* why = nullptr);
} // namespace lynx
