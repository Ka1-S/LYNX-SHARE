#pragma once

// File › Export › "For other apps..." (round 184a, slice S9; the Simple workflow's Finish dock offers it too): one folder
// with stems, MIDI, a Live set, an FL Studio project, the mixdown and a README (the plan: core/ExportBundle.h). Every
// file comes from a writer LYNX already has: engine/Exporter (stems, mixdown), the MIDI file writer, writeAlsProject,
// engine/FlStudioExport. One progress window; a step that fails is listed at the end and the others are still written.

#include <functional>

#include "core/ExportBundle.h"
#include "core/ExportOptions.h"
#include "core/FlpExport.h"
#include "engine/AudioEngine.h"

namespace lynx
{
// The options window (the folder is asked for after it), then the export and its report.
void showOtherAppsExportDialog (AudioEngine& engine, const juce::File& suggestedFolder, const juce::String& projectName);

// Round 190fo: one Playlist track as a file — its stem as a WAV (as the bundle renders stems) or its notes as a MIDI
// file — after a save dialog named as the original names them ("<song> - <track>.wav", "<track>.mid").
void exportOneTrack (AudioEngine& engine, int track, bool asMidi, const juce::File& suggestedFolder, const juce::String& projectName);

// The options window alone (snapshots); nothing is written.
std::unique_ptr<juce::Component> makeOtherAppsExportPanel (AudioEngine& engine, const juce::String& projectName);
// The report window alone (snapshots).
std::unique_ptr<juce::Component> makeOtherAppsReportPanel (const juce::File& folder, const juce::StringArray& written, const juce::StringArray& failed);

namespace otherapps
{
// One render: `song` with `options` into `file` (an engine/Exporter job). `progress` 0..1 returns false to cancel.
using RenderFn = std::function<bool (const Song& song, const exportopts::Options& options, const juce::File& file,
                                     const std::function<bool (double)>& progress, juce::String& error)>;

struct Context
{
    PluginHost* host = nullptr;             // for the Live set's plugin records (message thread)
    flpexport::Options flOptions;           // flstudio::optionsFor (host, renderAudio, song), made on the message thread
    double sampleRate = 44100.0;
    RenderFn render;
    // runs `f` on the message thread and waits for it (the Live set's writer looks plugins up there)
    std::function<void (std::function<void()>)> onMessageThread;
    // 0..1 and what is being done; false = cancel
    std::function<bool (double, const juce::String&)> progress;
};

struct Result
{
    juce::File folder;           // the bundle folder (or the .zip when zipped)
    juce::StringArray written;   // relative paths
    juce::StringArray failed;    // "<path>: <why>"
    bool cancelled = false;
};

// Writes the whole bundle of `song` (the project as it is) into `parent`. The folder gets a number when the name is
// taken. True when the folder was made (even if some steps failed: see Result::failed).
bool write (const Song& song, const juce::String& projectName, const exportbundle::Options& options, const juce::File& parent,
            const Context& context, Result& result);

// A MIDI file of `notes` (one track per channel, named after it) with tempo, time signature and, when the project has
// one, its key as a key signature (meta events at the start).
bool writeMidi (const Song& song, const std::vector<Note>& notes, const juce::File& file, juce::String& error);

// Appends silence to the WAV `file` up to `frames` (same format); nothing when it is that long already.
bool padWav (const juce::File& file, juce::int64 frames, juce::String& error);
} // namespace otherapps
} // namespace lynx
