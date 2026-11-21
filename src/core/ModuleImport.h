#pragma once

// Tracker module import (round 190nw, 0.1.31 queue item 2, stage 1 "M2"; design: study190nd/tracker/DESIGN-tracker.md
// §4 / §6). A ProTracker .mod, FastTracker 2 .xm, Scream Tracker 3 .s3m or Impulse Tracker .it becomes a LYNX project
// the way an .flp or a .flm does (same pipeline: samples, report, Missing files window):
//  - the module is read by libxmp-lite 4.7.4 (MIT, third_party/libxmp-lite): its loaders give the patterns, the
//    instruments and the decoded samples (delta / IT-compressed / 8- and 16-bit), its player walks the song once so
//    the order list, jumps, breaks, pattern loops and delays, and every speed / tempo change are libxmp's own;
//  - every instrument becomes a Sampler channel: one zone per sample and key range (the instrument's note map), the
//    sample's loop (a ping-pong loop unrolled), its tuning (c5spd / relative note / finetune) as the zone's root and tune;
//    the samples are written as WAV files by `saveSample`;
//  - every module pattern becomes a pattern (rows of ppq / rowsPerBeat ticks; rowsPerBeat 4 unless the module's tempo
//    needs more or fewer to stay in LYNX's 10…522 BPM), each run of rows the song plays becomes a pattern clip in the
//    Playlist (a break or a loop starts a new clip with an offset), and each row's real duration (speed, tempo, row
//    delay) becomes the tempo of the tempo markers (a tempo automation clip) — the rows stay on the grid;
//  - a note lasts until its channel's next note, note off or cut (at most to its clip's end), with the volume and pan
//    libxmp plays it at (set volume, default volumes, global / channel / instrument volume, panning) as its velocity and
//    pan; note delay and note cut are exact ticks; tone portamento is the note's porta;
//  - what stage 1 does not import (tick effects: arpeggio, vibrato, slides, retrigger, sample offset...; envelopes, IT
//    new note actions, filters) is counted per kind in the import report.
// JUCE-free (tests: tests/ModuleImport190nwTests.inc).

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/FlpImport.h"
#include "core/Song.h"

namespace lynx
{
struct ModuleImportSummary
{
    std::string format;          // libxmp's name ("Protracker", "Fast Tracker II"...)
    int channels = 0, instruments = 0, samples = 0, patterns = 0;
    int rowsPerBeat = 4;
    std::int64_t playedRows = 0;
    int clips = 0, tempoChanges = 0, notes = 0;
    int heldOverClipEnd = 0;     // notes the module holds past a pattern clip's end (LYNX ends them there)
    double seconds = 0.0;        // the song's length as libxmp plays it (once, without its loop)
    std::int64_t loopTick = -1;  // where the module loops back to (the song loop marker), -1 = it ends
};

struct ModuleImportOptions
{
    // writes one sample (a WAV file's bytes) and returns its path ("" = not written: the zone is left out)
    std::function<std::string (const std::vector<std::uint8_t>& wav, const std::string& name)> saveSample;
    FlpImportReport* report = nullptr; // optional
    ModuleImportSummary* summary = nullptr; // optional
    double maxSeconds = 3600.0;        // a song longer than this (a module that never ends) is cut there
};

// Whether `name` ends with one of the formats LYNX imports (.mod .xm .s3m .it, any case).
bool isModuleFileName (const std::string& name);
// The extensions as a JUCE-style wildcard list ("*.mod;*.xm;*.s3m;*.it") and as "mod;xm;s3m;it".
const char* moduleWildcards() noexcept;
const char* moduleExtensions() noexcept;

// The module as a LYNX song, or nullopt with `error` (not a module libxmp-lite reads, damaged).
std::optional<Song> importModule (const std::vector<std::uint8_t>& bytes, const ModuleImportOptions& options = {}, std::string* error = nullptr);
} // namespace lynx
