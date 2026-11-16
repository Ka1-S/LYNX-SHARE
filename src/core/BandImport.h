#pragma once

// GarageBand project (.band) import, round 190ic (issue #184). A LYNX-own import (FL Studio has none), clean-room: the
// reader is core/BandFormat.h. The song becomes a Simple-workflow project (core/SimpleTracks.h), one track per
// GarageBand track — an ordinary FL-style project that every layout and export reads:
//  - tempo, signature, key, title; GarageBand for iOS's song sections as markers; signature changes as markers;
//  - a software-instrument track → an Instrument track whose MIDI regions are patterns at their places (the notes a
//    region's bounds hide in GarageBand are left out); a drum kit (by its name) → a Drums track whose notes go to
//    LYNX's built-in kick / snare / clap / hat by General MIDI key; the instrument sound itself is not stored in a
//    readable form, so the track plays LYNX's built-in synth with a preset chosen by the patch name (listed);
//  - a track whose Sampler instrument the user made (GarageBand for iOS: its .exs and recordings in Media/Sampler) → a
//    Sampler track playing those samples (root key, key / velocity range, fine tune; issue #184's project);
//  - the patch's category (GarageBand for iOS 2.3+: "Drums", "Smart Drums") makes a drum kit too, whatever its name;
//  - an audio track → an Audio track whose regions are audio clips of the files in the package's Media folder (trim
//    and length kept); files that are not in the package (Apple Loops from the library) are listed as missing;
//  - each track's fader, pan and mute on its insert / track.
// Everything else is listed in the report: effects, Smart Drums / Drummer / pattern regions, folders and take folders,
// tempo changes, MIDI controllers, region fades.

#include <optional>
#include <string>

#include "core/BandFormat.h"
#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
struct BandImportOptions
{
    // fileExists, readFile, relocations and report are used (the others are FL's)
    FlpImportOptions common;
    // the .band folder (an extracted zip's copy for a shared zip): its Media folder holds the audio
    std::string packageFolder;
};

// The project as a LYNX song.
Song importBand (const band::Project& project, const BandImportOptions& options);

// Reads the package at options.packageFolder with options.common.readFile ("Alternatives/000/ProjectData" or
// "projectData", then the summary plist) and imports it. nullopt + `error` (English) when it is not readable.
std::optional<Song> importBandPackage (const BandImportOptions& options, std::string* error = nullptr);

// The built-in synth preset a GarageBand patch name plays with (one of synthPresets()), and whether the name is a drum
// kit's. Exposed for tests.
std::string bandPresetFor (const std::string& patchName);
bool bandDrumKitName (const std::string& patchName);
// Whether a GarageBand track plays a drum kit: its patch's category or instrument when the project names them, else its name.
bool bandDrumTrack (const band::Track& track);
// A General MIDI drum key → the built-in drum it plays (kick, snare, clap or hat).
InstrumentKind bandDrumFor (int key) noexcept;
} // namespace lynx
