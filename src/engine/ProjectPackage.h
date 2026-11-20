#pragma once

// Project packages (REA round 118; FL Studio's "Export > Project data files" and "Zipped loop package" interpreted):
// a .zip holding the project (.lynx) and every sample it uses under "samples/", the project's sample paths rewritten
// to point there; opening one extracts it into a folder of its own and opens the project inside. Also "collect":
// copying the samples a project uses into one folder.

#include <juce_core/juce_core.h>

#include <map>
#include <string>

#include "core/Song.h"

namespace lynx::ProjectPackage
{
struct Result
{
    int files = 0;            // samples written into the package / folder
    juce::StringArray missing; // samples the project references that are not on this PC (left out)
};

// Writes `zip` (replaced only once complete) with "<projectName>.lynx" and its samples.
bool exportZip (const Song& song, const juce::String& projectName, const juce::File& zip, Result& result, juce::String& error);

// Copies the project's samples into `folder` (names made unique, existing files are never overwritten); `relink`
// receives original path -> copy (for applyRelocations). FL's "collect project data files".
bool collectSamples (const Song& song, const juce::File& folder, std::map<std::string, std::string>& relink, Result& result,
                     juce::String& error);

// Extracts a package into a new folder under `destinationRoot` (named after the zip, made unique) and returns the
// project file inside (the first .lynx, else the first .flp; round 190ic: else the first GarageBand .band folder, as
// GarageBand for iOS shares a song); an invalid file on failure.
juce::File extractZip (const juce::File& zip, const juce::File& destinationRoot, juce::String& error);

// Where opened packages are extracted: Documents/LYNX/Projects/Unpacked.
juce::File defaultExtractRoot();

// Round 190ic (issue #184): the GarageBand package (.band folder) `file` is or lies in ("CC Jingle.band", or its
// "projectData" picked in a file dialog that can't choose folders); an invalid file when there is none.
juce::File garageBandPackage (const juce::File& file);
} // namespace lynx::ProjectPackage
