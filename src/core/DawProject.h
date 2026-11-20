#pragma once

// DAWproject import / export (round 190jf, B22). DAWproject is the open exchange format of Bitwig and PreSonus (MIT
// licence, github.com/bitwig/dawproject: Project.xsd, MetaData.xsd and the reference's documentation; Bitwig Studio,
// Studio One, Cubase, Cubasis, VST Live and n-Track read and write it). Clean-room: written from the XSDs and the
// README's description, no code taken from the Java reference. JUCE-free.
//
// A .dawproject is a ZIP (core/ZipLite.h) holding project.xml (the project), metadata.xml (title, artist…) and the
// files they refer to: audio (`audio/…`) and plug-in states (`plugins/…`). Times are in beats (quarter notes) or
// seconds; LYNX's ticks are `ppq` per quarter note.
//
// The import, as the .als import does (core/AlsImport.h):
//  - a Track with notes → a channel (its instrument) + a mixer insert + an instrument Playlist track; each notes clip →
//    its own pattern; an audio track → an insert + an audio Playlist track, each audio file (with its warps) → an audio
//    clip channel and the audio clips; a folder track (contentType "tracks", nested Tracks) → a group header whose
//    children sit one folder level deeper (PlaylistTrack::grouped / nest); a folder with a Channel → its insert is the
//    group's bus (the header becomes an audio track of it, arrange::makeGroupBus's layout);
//  - Channel role master → insert 0; effect → an insert fed by the Sends; submix → an insert other Channels name as
//    their destination; Volume / Pan / Mute / Solo / Sends (pre / post, level);
//  - devices: Vst2Plugin / Vst3Plugin / ClapPlugin → PluginRef::foreign with the state carried over (.fxp / .fxb →
//    FL's VST2 record, .vstpreset → FL's VST3 record: what the plugin resolver already reads); LYNX's own devices
//    (deviceVendor "LYNX") back to the same native instrument / effect; Equalizer / Compressor / NoiseGate / Limiter →
//    Facet EQ / Crest Compressor / Sentry Gate / Apex Limiter;
//  - automation (Points on Volume, Pan, a Send's Volume, a device parameter, tempo, time signature) → automation lanes
//    under the track (LYNX's song-time lanes, PlaylistTrack::lanes) or, for a track without a Playlist row (the master,
//    an effect return), an automation clip on a row of its own; tempo → tempo markers (core/TempoMarkers.h); time
//    signature changes → time signature markers; Markers → Playlist markers; metadata.xml → Song::info;
//  - Scenes (the clip launcher's ClipSlots) → the clip grid, as the .als import's Session view (the export leaves the grid out).
// Everything else is listed in the report (FlpImportReport: unsupported / notReproduced / approximated / droppedEffects), never dropped
// silently. The export writes the inverse; what it leaves out is listed in DawProjectExportResult::notes.

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/FlPluginDb.h"
#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
struct DawProjectImportOptions
{
    // The .flp import's options that apply here: projectFolder (the .dawproject's folder: external files are looked
    // for next to it), fileExists, saveEmbedded (an embedded audio file → the path it was saved to; without it the
    // embedded audio is reported missing), relocations, report, sampleSeconds. FL's path roots are not used.
    FlpImportOptions common;
    // A LYNX device's state (deviceVendor "LYNX", written by exportDawProject) back to a channel's instrument / an
    // effect slot. Empty = the core's own readers (core/GeneratorPresets.h text for instruments, LYNX's effect state
    // text for effects). The app passes the project file's readers to restore every field.
    std::function<std::optional<ChannelDef> (const std::string& deviceId, const std::vector<std::uint8_t>& state)> nativeInstrument;
    std::function<std::optional<EffectSlot> (const std::string& deviceId, const std::vector<std::uint8_t>& state)> nativeEffect;
};

// True for a ZIP that holds a project.xml whose root is <Project> (a .dawproject).
bool isDawProjectData (const std::uint8_t* data, std::size_t size);

// The project as a LYNX song, or nullopt with `error` (not a ZIP, no project.xml, malformed XML, not a DAWproject).
std::optional<Song> importDawProject (const std::vector<std::uint8_t>& fileBytes, const DawProjectImportOptions& options = {},
                                      std::string* error = nullptr);

// The third-party plugins a project uses (no import): for the targeted plugin scan before an open.
std::vector<FlpPluginUse> dawProjectForeignPlugins (const std::vector<std::uint8_t>& fileBytes);

struct DawProjectExportOptions
{
    std::string application = "LYNX SHARE", version = "0.1.30";
    // A sample's bytes, to embed it under audio/ (empty = the file is referenced where it is, external="true").
    std::function<std::vector<std::uint8_t> (const std::string& path)> readFile;
    // A sample's length in seconds, rate and channels (Audio's required attributes, and the end warp marker of a
    // stretched clip); false = unknown (written as 0 s, 44100 Hz, 2 channels and listed in the notes).
    std::function<bool (const std::string& path, double& seconds, int& sampleRate, int& channels)> sampleFormat;
    // A loaded plugin's record (format, ids, state in FL's layout: PluginHost::flWrapperRecord), as for the .als export.
    std::function<std::optional<ForeignPlugin> (const PluginRef& ref, bool instrument)> pluginRecord;
    // A LYNX device's state file (see DawProjectImportOptions). Empty = the core's own writers.
    std::function<std::vector<std::uint8_t> (const ChannelDef& channel)> nativeInstrumentState;
    std::function<std::vector<std::uint8_t> (const EffectSlot& slot)> nativeEffectState;
};

struct DawProjectExportResult
{
    std::vector<std::uint8_t> bytes;  // the .dawproject (ZIP)
    std::string projectXml, metadataXml; // the same, as text (tests, debugging)
    std::vector<std::string> notes;   // what was left out or approximated (one line each)
};

DawProjectExportResult exportDawProject (const Song& song, const DawProjectExportOptions& options = {});

namespace dawproject
{
// --- plug-in state files (exposed for the tests) --------------------------------------------------------------------
// VST3's .vstpreset (Steinberg's documented layout: "VST3", version 1, the class id as 32 ASCII hex digits, the offset
// of the chunk list; the chunks; "List" + {id, offset, size}: "Comp" the component state, "Cont" the controller's).
std::vector<std::uint8_t> writeVstPreset (const std::array<std::uint8_t, 16>& classId, const std::vector<std::uint8_t>& component,
                                          const std::vector<std::uint8_t>& controller);
bool readVstPreset (const std::vector<std::uint8_t>& file, std::array<std::uint8_t, 16>& classId, std::vector<std::uint8_t>& component,
                    std::vector<std::uint8_t>& controller);
// A VST3 class id (ForeignPlugin::classId's layout) as the 32 hex digits the preset and DAWproject's deviceID use
// (the FUID's four 32-bit words, %08X each), and back (non-hex characters such as '-' or '{' are skipped).
std::string vst3ClassIdText (const std::array<std::uint8_t, 16>& classId);
bool vst3ClassIdFromText (const std::string& text, std::array<std::uint8_t, 16>& classId);
// FL's VST3 record (what the .als import writes, core/FlWrapper.h splitFlVst3State reads) from the two states.
std::vector<std::uint8_t> flVst3State (const std::vector<std::uint8_t>& component, const std::vector<std::uint8_t>& controller);
// FL's VST2 record ↔ an .fxp / .fxb file (core/PluginPresets.h). Empty / false when there is nothing to carry.
std::vector<std::uint8_t> fxFromFlVst2State (const std::vector<std::uint8_t>& state, std::uint32_t pluginId);
bool flVst2StateFromFx (const std::vector<std::uint8_t>& fx, std::vector<std::uint8_t>& state, std::uint32_t& pluginId);

// LYNX's own effect state text ("LYNX effect state 1": the kind, on / off, mix, smart disable, every parameter as its
// exact float bits, the raw state, sample path and preset name), the core's default for a LYNX effect device.
std::string effectStateText (const EffectSlot& slot);
std::optional<EffectSlot> effectFromStateText (const std::string& text);

// Units of a DAWproject RealParameter value
double gainOfVolume (double value, const std::string& unit, double max) noexcept; // linear / decibel / normalized → linear gain
double panOfValue (double value, const std::string& unit, double min, double max) noexcept; // → −1 … 1
std::uint32_t colourOf (const std::string& text) noexcept;      // "#rrggbb" → 0xFFRRGGBB (0 = none / invalid)
std::string colourText (std::uint32_t argb);                     // 0xFFRRGGBB → "#rrggbb"
} // namespace dawproject
} // namespace lynx
