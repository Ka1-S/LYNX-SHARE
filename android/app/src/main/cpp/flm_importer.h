#pragma once

// FLM Importer — Convert parsed FLM Project to Android Song model
// Based on desktop FlmImport, simplified for Android engine

#include "flm_parser.h"
#include "song_model.h"
#include <string>
#include <optional>
#include <vector>

namespace lynx::flm_android
{

struct FlmImportOptions
{
    std::string projectFolder;     // .flm's folder = "@L@" library
    std::string resourceRoot;      // "@R@" resources
    std::vector<std::string> userResourceRoots;
    
    // File existence checker
    std::function<bool(const std::string&)> fileExists;
    
    // Sample callbacks
    std::function<std::vector<std::uint8_t>(const std::string&)> readFile;
    std::function<std::int64_t(const std::string&)> sampleFrames;
    std::function<double(const std::string&)> sampleSeconds;
    std::function<std::string(const std::vector<std::uint8_t>&, const std::string&)> saveEmbedded;
};

struct FlmImportReport
{
    std::map<std::string, int> converted;
    std::map<std::string, int> approximated;
    std::map<std::string, int> unsupported;
    std::map<std::string, int> droppedEffects;
    std::map<std::string, int> partialEffects;
    int missingSamples = 0;
    int relocatedSamples = 0;
    std::vector<std::string> missingFiles;
};

std::optional<Song> importFlmProject(const Project& flmProject, const FlmImportOptions& options = {}, FlmImportReport* report = nullptr);

// Helper: resolve FLM paths (@L@, @R@)
std::string resolveFlmPath(const std::string& flmPath, const FlmImportOptions& options);

// Helper: check if FLM data
bool isFlmData(const std::uint8_t* data, std::size_t size);

} // namespace lynx::flm_android