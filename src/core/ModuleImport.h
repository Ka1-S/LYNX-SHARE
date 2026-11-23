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
//  - round 190ob (stage 2 start): the volume of a sounding note as libxmp plays it on every frame (volume slides and
//    sets, the volume column, global / channel volume, tremolo, tremor, the volume envelope while the note is held)
//    becomes the automation of its channel's Volume multiplier (Levels adjustment, FL's channel parameter 0x11) on
//    the .lynx file's value steps (a set on a row's first frame as a jump, a slide's ticks as straight lines); an instrument sounding
//    in several module channels at once, each with its own changes, gets a channel per module channel; libxmp's linear
//    pan law: a channel whose notes share one pan level (MOD / S3M) gets that level as its gain and the notes libxmp's
//    left / right balance;
//  - what is not imported yet (tick effects: arpeggio, vibrato, pitch slides, retrigger, sample offset...; pan changes,
//    pan / pitch / filter envelopes, IT new note actions, filters) is counted per kind in the import report.
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
    // round 190ob: the volume changes of sounding notes as the channels' Volume multiplier automation
    int volumeChangedNotes = 0;  // played notes whose volume changes while they sound
    int volumeAutomations = 0, volumePoints = 0;
    int splitChannels = 0;       // channels added for an instrument sounding in several module channels at once
    // round 190om: volume changes as pattern events (exact timing) where a pattern's every play has the same ones, the
    // channel's automation clip (190ob) where they differ; pitch changes of sounding notes as the notes' pitch curves
    int volumeEventChannels = 0, volumeEventLanes = 0, volumeEvents = 0;
    int pitchCurveNotes = 0, pitchCurvePoints = 0, fineTunedNotes = 0;
    int continuedPortas = 0;     // tone portamentos that slide the sounding note on (no new note)
    int pitchDifferentPlays = 0; // later plays of a note whose pitch moves otherwise (the first play's curve is kept)
    int panEventChannels = 0;    // channels whose notes' pans move: the pan offset and the multiplier as pattern events
};

struct ModuleImportOptions
{
    // writes one sample (a WAV file's bytes) and returns its path ("" = not written: the zone is left out)
    std::function<std::string (const std::vector<std::uint8_t>& wav, const std::string& name)> saveSample;
    FlpImportReport* report = nullptr; // optional
    ModuleImportSummary* summary = nullptr; // optional
    double maxSeconds = 3600.0;        // a song longer than this (a module that never ends) is cut there
    // round 190om: a channel's volume changes as pattern events where they are exact (false: every channel's as an
    // automation clip over the song, round 190ob's way)
    bool volumeAsPatternEvents = true;
};

// Whether `name` ends with one of the formats LYNX imports (.mod .xm .s3m .it, any case).
bool isModuleFileName (const std::string& name);
// The extensions as a JUCE-style wildcard list ("*.mod;*.xm;*.s3m;*.it") and as "mod;xm;s3m;it".
const char* moduleWildcards() noexcept;
const char* moduleExtensions() noexcept;

// The module as a LYNX song, or nullopt with `error` (not a module libxmp-lite reads, damaged).
std::optional<Song> importModule (const std::vector<std::uint8_t>& bytes, const ModuleImportOptions& options = {}, std::string* error = nullptr);
} // namespace lynx
