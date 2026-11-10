#pragma once

// "Convert to MIDI" (round a2m189b; Ableton Live's Convert Melody / Harmony / Drums to New MIDI Track, done the FL way:
// a new pattern on new channels). The part without JUCE and without the model runtime:
//  - melody and harmony read Spotify's Basic Pitch model (Apache-2.0; a 230 KB ONNX graph run by ui/AudioToMidi with the
//    ONNX Runtime LYNX already ships): its note and onset posteriorgrams are stitched here from overlapping 2 s windows
//    and decoded into notes with Basic Pitch's own note-creation rules (ported from basic_pitch/note_creation.py);
//  - melody keeps one note at a time, the most salient one;
//  - drums are LYNX's own: onsets in three filtered bands (kick, snare, hi-hat), no model;
//  - the result becomes one pattern on new channels, placed on a new Playlist track under the source clip (one undo step).

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::a2m
{
enum class Mode
{
    melody,
    harmony,
    drums
};

// A found note, in seconds of the analysed audio.
struct Note
{
    double start = 0.0, end = 0.0;
    int key = 60;
    float velocity = 0.78f; // 0..1
};

// --- Basic Pitch (ICASSP 2022 model) -----------------------------------------------------------------------------
inline constexpr double modelRate = 22050.0;
inline constexpr int windowSamples = 43844;   // 2 s minus one hop: the graph's fixed input
inline constexpr int hop = 256;               // model frames are 256 samples apart (86 per second)
inline constexpr int overlapFrames = 30;      // windows overlap by 30 frames; half of it is dropped on each side
inline constexpr int overlapSamples = overlapFrames * hop;
inline constexpr int windowStride = windowSamples - overlapSamples;
inline constexpr int keys = 88;               // A0 (MIDI 21) .. C8 (108), one bin per semitone
inline constexpr int lowestKey = 21;

// The model's two outputs over the whole audio, frame-major: value (frame, key bin) at [frame * keys + bin].
struct Posteriorgram
{
    int frames = 0;
    std::vector<float> note, onset;
};

inline int numWindows (std::int64_t samples) noexcept
{
    const auto padded = samples + overlapSamples / 2;
    return (int) std::max<std::int64_t> (1, (padded + windowStride - 1) / windowStride);
}

// Runs `model` over 22.05 kHz mono audio and stitches its windows into one posteriorgram, frame k at sample k * hop.
// `model (in, note, onset, framesPerWindow)` gets one window of `windowSamples` (the first starts half an overlap
// before the audio, padded with silence) and fills `framesPerWindow` x `keys` values of each output; false stops.
// `progress` gets 0..1 after each window. False when the model stopped.
bool runModel (const std::vector<float>& mono, int framesPerWindow,
               const std::function<bool (const float* in, float* note, float* onset)>& model,
               const std::function<void (double)>& progress, Posteriorgram& out);

struct NoteOptions
{
    float onsetThreshold = 0.5f; // Basic Pitch's defaults
    float frameThreshold = 0.3f;
    double minNoteSeconds = 0.1277;
    bool melodiaTrick = true;    // notes without a clear onset, followed from the strongest frame
    bool inferOnsets = true;     // onsets also from sudden rises of the note posteriorgram
    int minKey = 21, maxKey = 108;
    int energyTolerance = 11;    // frames below the threshold a note may span
};

// Basic Pitch's polyphonic note creation (note_creation.py, output_to_notes_polyphonic), on frame k = k * hop / modelRate s
// (shifted by `timeOffset`, the model's latency).
std::vector<Note> notesFromPosteriorgram (const Posteriorgram& pg, const NoteOptions& options, double timeOffset = 0.0);

// One note at a time (Melody): of notes starting together the loudest; a later one ends the last, unless it is a weaker
// note inside a louder one (an overtone). Notes left shorter than `minSeconds` are dropped.
std::vector<Note> monophonic (std::vector<Note> notes, double minSeconds);

// The settings each mode uses (chosen on round a2m189b's measured clips: higher onset and frame thresholds than Basic
// Pitch's defaults; melody keeps notes from 50 ms and the melodia trick, harmony leaves the melodia trick out).
NoteOptions optionsFor (Mode mode) noexcept;

// Melody or harmony notes of a posteriorgram with the mode's settings; melody also moves each note's start back to where
// its key's activation reached half its level (at most 5 frames, never into the previous note).
std::vector<Note> transcribe (const Posteriorgram& pg, Mode mode);

// --- drums (LYNX's own) ----------------------------------------------------------------------------------------------
inline constexpr int kickKey = 36, snareKey = 38, hatKey = 42; // GM / Live's Convert Drums keys (C1, D1, F#1)

struct DrumOptions
{
    float sensitivity = 0.5f; // 0..1: higher finds softer hits
};

// Drum hits of mono audio: kick / snare / closed hi-hat notes (1/16 s long), velocity from the hit's level.
std::vector<Note> drumHits (const float* mono, std::int64_t frames, double sampleRate, const DrumOptions& options = {});

// --- into the song ---------------------------------------------------------------------------------------------------
// Where an audio clip's notes go: the clip's file frames [firstFrame, firstFrame + frames) are analysed; frame f lands at
// clip tick (f - zeroFrame) / framesPerTick - offset (mirrored when it plays backwards).
struct ClipTiming
{
    double sampleRate = 44100.0;  // of the file
    double zeroFrame = 0.0;       // the file frame at the clip's tick 0 before its offset (its played span's start)
    double framesPerTick = 1.0;
    double spanFrames = 0.0;      // the played span's length (for reversed clips)
    bool backwards = false;
};

// The part of the file an audio clip shows, in file frames [first, first + count), clamped to the file.
struct FrameRange
{
    std::int64_t first = 0, count = 0;
};
FrameRange clipFrames (const Clip& clip, const ClipTiming& timing, std::int64_t fileFrames) noexcept;

// The (translated) names of what is added.
struct Names
{
    std::string pattern;
    std::string channel;                 // melody / harmony: the Synth channel
    std::string kick, snare, hat;        // drums: the three channels
};

// A new pattern holding `notes` (seconds), on new channels: melody and harmony one Synth channel, drums Kick, Snare and
// Hat channels (LYNX's built-in drums, one note per hit on the step grid's key, as FL's Channel Rack). `tickOf` gives a
// time's pattern tick; notes outside [0, lengthTicks) are cut or left out (lengthTicks 0: no limit). Returns the new
// pattern's index, or -1 (song untouched) when no note is left.
int addPattern (Song& song, Mode mode, const std::vector<Note>& notes, const std::function<double (double)>& tickOf,
                std::int64_t lengthTicks, const Names& names);

// The audio clip menu's conversion: the notes (seconds from file frame `analysedFirst`) as a new pattern, played by a new
// pattern clip on a new track right under the audio clip, aligned with it and as long. Run inside AudioEngine::editSong
// (one undo step). Returns the new pattern's index, or -1 (song untouched) when `clipIndex` is no longer an audio clip of
// `sourcePath` or no note falls inside the clip.
int addClipPattern (Song& song, int clipIndex, const std::string& sourcePath, Mode mode, const std::vector<Note>& notes,
                    std::int64_t analysedFirst, const ClipTiming& timing, const Names& names);
} // namespace lynx::a2m
