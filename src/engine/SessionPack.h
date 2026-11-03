#pragma once

// Online Sessions, phase 1 (docs/design/ONLINE_SESSIONS.md §9 P1, round 168a): project packages without a network.
//
// File › Send project…: a .lynxpack (a zip) with the project, every sample and recording it uses, the stand-in renders
// of its third-party plugin parts and a manifest (format, LYNX SHARE version, who it is from and for, what the friend
// may keep, the media with their SHA-256, the plugins used). The friend opens it in his LYNX SHARE: it plays the same,
// a part whose plugin he lacks plays its render with a badge. He records his part and uses File › Send my part back: a
// .lynxpack of kind "part" with only his area (core/SessionParts.h) and his media / stand-ins. The host's
// File › Merge a part… brings it into his project in one undo step.

#include <functional>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

#include "core/Song.h"
#include "engine/PluginHost.h"

namespace lynx::sessionpack
{
constexpr const char* extension = ".lynxpack";
constexpr int formatVersion = 1;

struct MediaEntry
{
    juce::String id;    // "sha256-<64 hex>": the bytes' identity (the same file twice is stored once)
    juce::String name;  // the original file name
    juce::int64 size = 0;
    juce::String entry; // where it is in the zip ("media/<hex>.<ext>")
};

struct Manifest
{
    bool part = false;         // "part" (a guest's area sent back) or "project"
    juce::String lynxVersion;  // the LYNX SHARE that made it
    juce::String projectName, projectId, created;
    SessionParticipant from, to; // who made it / who it is for (project packages); to.keepFull = decision 1
    std::vector<MediaEntry> media;
    std::vector<PluginHost::Identity> plugins; // the third-party plugins the song uses (only those: privacy)
    juce::StringArray missing; // samples the sender's PC did not have (left out)
    int standIns = 0;          // stand-in renders inside
};
juce::String manifestToJson (const Manifest& m);
std::optional<Manifest> manifestFromJson (const juce::String& json, juce::String& error);

// The stand-ins a package needs: the channels and inserts with a third-party plugin that is installed here (only the
// owner can render them) — of participant `onlyTag`'s area, or of everyone's (0).
struct RenderItem
{
    bool insert = false;
    int index = 0;         // channel / insert
    bool timeline = false; // a whole-song stem (else pattern stems)
    juce::String name, plugins;
};
std::vector<RenderItem> planRenders (const Song& song, PluginHost& host, std::uint32_t onlyTag);

// What the render of the source depends on (its JSON, notes, tempo, sample rate): a stand-in with another hash is out
// of date. pattern = -1: a timeline stem.
std::string sourceHash (const Song& song, bool insert, int index, int pattern, double sampleRate);

struct Rendered
{
    bool insert = false;
    std::uint64_t uid = 0; // channels: the channel's uid (the song may change while rendering)
    int index = 0;
    std::vector<StandIn> standIns;
};
// Renders the items into `folder` with private engines (ExportJob; their constructors run on the message thread, the
// renders on the calling thread — call it from a background thread, or from the message thread in command-line
// modes). Stand-ins whose hash still matches and whose file exists are kept as they are. progress (0..1, what) returns
// false to cancel.
bool render (const Song& song, const std::vector<RenderItem>& items, double sampleRate, const juce::File& folder, const juce::String& by,
             std::vector<Rendered>& out, const std::function<bool (double, const juce::String&)>& progress, juce::String& error);
// Puts the renders on the song (by uid for channels).
void apply (Song& song, const std::vector<Rendered>& rendered);

struct WriteResult
{
    int media = 0;              // files stored (after dedupe)
    int duplicates = 0;         // references that pointed at bytes already stored
    juce::int64 bytes = 0;
    juce::StringArray missing;  // not on this PC
};
// Writes the package (replaced only when complete): the song as "project.lynx" with its sample paths pointing into
// "media/", the media and the manifest. manifest.media / plugins / missing / standIns are filled here.
bool write (const Song& song, Manifest manifest, PluginHost& host, const juce::File& file, WriteResult& result, juce::String& error);

struct Opened
{
    Manifest manifest;
    juce::File folder, project;
    Song song;
    juce::StringArray corrupt; // media whose bytes do not match their hash (damaged in transfer)
};
// Reads a package's manifest only (for the open dialog).
std::optional<Manifest> readManifest (const juce::File& file, juce::String& error);
// Extracts it into a new folder under `destinationRoot` (named after it), checks every medium against its hash and
// loads the project (its media found in the folder).
std::optional<Opened> open (const juce::File& file, const juce::File& destinationRoot, juce::String& error);

// The plugins of a received song on this PC: a PluginRef whose plugin is found here by its identity (another PC's
// JUCE id holds another file path) gets this PC's id — when it is the same version, or any version with
// `useOtherVersions`; the rest stay as they are and play their stand-ins.
struct PluginReport
{
    juce::StringArray found, otherVersion, missing;
};
PluginReport resolvePlugins (Song& song, const Manifest& manifest, PluginHost& host, bool useOtherVersions);

juce::File receivedRoot(); // Documents/LYNX/Projects/Received
// Where a project's stand-ins are written: "<project folder>/Stand-ins", or Documents/LYNX/Stand-ins/<projectId>.
juce::File standInFolder (const juce::File& projectFile, const Song& song);
// Where a merged part's media go: "<project folder>/Session media/<guest>", or Documents/LYNX/Session media/<projectId>/<guest>.
juce::File sessionMediaFolder (const juce::File& projectFile, const Song& song, const juce::String& guest);
// A received song may only read its own media: every sample outside `folder`, and every other path the engine reads
// (effect samples, Patcher graphs) outside `folder` and `engineFiles` (FL Studio's own data: FL Keys' wavetables, the
// engines' DLLs), becomes "<folder>/missing/<name>"; a file an insert records to outside them is cleared, and the
// sender's data folder goes. open() does it to a package, live sessions to every change they receive.
void confine (Song& song, const juce::File& folder, const juce::File& engineFiles);
// A file's SHA-256 (64 hex digits; empty when it cannot be read), read in 1 MiB pieces.
juce::String hashFile (const juce::File& file);
// Copies the part's media into `folder` (by hash: a file already there is reused) and points the song at the copies.
bool adoptMedia (Song& song, const juce::File& folder, juce::String& error);
} // namespace lynx::sessionpack
