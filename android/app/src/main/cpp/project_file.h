#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "song_model.h"

// ProjectFile — save/load LYNX project in native .lynx format (JUCE-free).
// Uses the ported Song model (Fase 3, PORTING.md).
// Binary format: magic + version + sections (tempo, patterns, mixer, channels, playlist, automations).
// Designed to be extended without breaking old files.

// Save project to binary .lynx file. Returns true on success.
bool saveProject(const std::string& path, const Song& song);

// Load project from binary .lynx file. Returns nullopt on failure.
std::optional<Song> loadProject(const std::string& path);

// Magic bytes for .lynx files
extern const char kLynxProjectMagic[4];
