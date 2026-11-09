#pragma once

// Auto-warp (round 175a, warp markers M2; modelled on Ableton Live's auto-warp, the method is LYNX's own): the warp
// markers that lock a sample to the song's beat. A loop (up to a minute) gets two markers: the file's start and its last
// whole beat, one tempo — its name's tempo, else a power of two of beats nearest the song's tempo (Live's own choices
// for 195 of 203 Core Library loops' start and 162 of 203 tempos, read from its .asd analysis files). A longer
// recording gets a marker on the first beat of each bar, on beats tracked through the whole file (dynamic programming
// over the onset curve) and moved onto the nearest attack, so a performance that drifts stays on the grid.
// Measurements: /mnt/e/lynx-work/ableton175a/m2.

#include <string>
#include <vector>

#include "core/Instruments.h"
#include "core/Song.h"

namespace lynx::autowarp
{
struct Options
{
    double songBpm = 120.0;  // a loop too short to read a tempo from: its length in a power of two of beats at this tempo
    double knownBpm = 0.0;   // the sample's tempo when known (its file name, ACID data); 0 = detected
    double beatsPerBar = 4.0;
    double trackBars = 8.0;  // a recording over a minute and at least this many bars: the markers follow its bars
};

struct Result
{
    std::vector<WarpPoint> markers; // seconds → beats, increasing; empty = nothing to lock to (silence)
    double bpm = 0.0;               // the tempo the markers start from
    double firstBeat = 0.0;         // seconds: where beat 0 is
    bool tracked = false;           // one marker per bar (a long sample)
};

Result analyse (const SampleData& sample, const Options& options = {});

// A tempo written in a file name ("Break Scatty 174 bpm", "loop_128BPM", "90bpm groove"): 40…300, else 0.
double bpmInName (const std::string& name);
} // namespace lynx::autowarp
