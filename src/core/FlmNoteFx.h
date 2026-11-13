#pragma once

// FL Studio Mobile's note effects (round 190bs): Transpose, Scale and Randomizer as FLM plays them, so that an imported
// song's notes are the ones FLM sends to its instruments. Clean-room: the laws below were worked out from the behaviour
// of FLM's FL plugin (static analysis, docs/analise/FLM_FORMAT.md §7.2) and are written in LYNX's own words; no
// Image-Line code or table file was copied.
//
// A rack runs its note effects over every note of its note lanes before the instruments see them, in the rack's module
// order and wherever they sit among the instruments; drum lanes are not touched. A switched-off effect (Enable, the
// first parameter, below 0.5) passes the notes through.
//
// Parameters (PRMS index, all 0…1):
// - Transpose (type 48): 1 Octave = round(8v − 4), 2 Semitone = round(24v − 12); the key moves by semitone + 12·octave
//   and stays inside 0…127.
// - Scale (type 50): 1 Key (root = round(11v)), 2 Scale (round(33v): 33 named scales, the last value = Custom),
//   3 Snap, 4…15 one switch per pitch class C…B. The switches are the scale that is used. A note outside it is moved
//   to the nearest key inside (Snap on) or not played at all (Snap off). "Nearest" looks one semitone further each
//   turn; a named scale looks upwards first when it is a major-like one and downwards first otherwise (the table
//   below), a custom set looks upwards first.
// - Randomizer (type 49): 1 Pitch (range = round(24v) semitones either way), 2 Velocity (± v), 3 Pan (± v of the
//   whole width), 4 Gate (the chance a note plays; 0 = none).
// Note Echo (47) and Arpeggiator (51) act in time: see arpeggiate() and echo() below (round 190de).

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lynx::flmnotefx
{
constexpr std::uint32_t noteEcho = 47, transpose = 48, randomizer = 49, scale = 50, arpeggiator = 51;

struct ScaleInfo
{
    const char* name;
    std::uint16_t mask; // bit i = the interval of i semitones above the root
    int direction;      // +1 looks upwards first, −1 downwards first, 0 = no preference (Chromatic)
};
constexpr int scaleCount = 33;
const ScaleInfo& scaleInfo (int index) noexcept; // 0…32

// Transpose's shift in semitones (0 when switched off)
int transposeShift (std::span<const float> params) noexcept;

// Scale: the pitch classes it lets through (bit i = pitch class i, C = 0) and where it looks first
struct ScaleSetting
{
    bool enabled = false, snap = false;
    std::uint16_t mask = 0x0FFF;
    int direction = 0;
};
ScaleSetting scaleSetting (std::span<const float> params) noexcept;
// The key Scale plays for `key` (the same key when it is in the scale or the set is empty)
int scaleKey (const ScaleSetting& s, int key) noexcept;

// A note as the effects see it: pan 0 = left … 1 = right
struct Note
{
    int key = 60;
    float velocity = 0.8f, pan = 0.5f;
};

// A rack's note effects, in its module order
class Chain
{
public:
    // Takes the module when it is a note effect this layer plays; false for anything else (Note Echo and Arpeggiator
    // included: the caller lists them)
    bool add (std::uint32_t type, std::span<const float> params);

    bool empty() const noexcept { return steps.empty(); }
    // A Randomizer that changes something is in the chain: FLM draws anew at every playback, `apply` draws once
    bool random() const noexcept { return hasRandom; }
    // Scale without Snap may drop notes
    bool filters() const noexcept { return hasFilter; }

    // Runs the chain over one note-on; false = the note is not played
    bool apply (Note& note);

private:
    struct Step
    {
        std::uint32_t type = 0;
        int shift = 0;                 // Transpose
        ScaleSetting scale;            // Scale
        int range = 0;                 // Randomizer
        float velocity = 0.0f, pan = 0.0f, gate = 1.0f;
    };
    std::vector<Step> steps;
    bool hasRandom = false, hasFilter = false;
    std::uint32_t state = 0x190B5u; // the draws repeat from import to import
    double uniform() noexcept;      // 0 ≤ x < 1
};
// --- round 190de (FLM_FORMAT §7.8): the note effects that act in time, baked on the rack's song timeline. A note in
// song beats; pan 0…1.
struct TimedNote
{
    double start = 0.0, end = 0.0;
    int key = 60;
    float velocity = 0.8f, pan = 0.5f;
};
// The Arpeggiator (type 51): both arps over the notes the rack receives (all of them, in song time from 0 at `bpm`,
// `beatsPerBar` beats a bar); the input notes are replaced by what the arps play. `random` = a random mode was used
// (LYNX draws with its own seed; FLM's draws change at every playback).
std::vector<TimedNote> arpeggiate (std::span<const float> params, const std::vector<TimedNote>& in, double bpm, int beatsPerBar, bool& random);
// Note Echo (type 47): the dry notes (a dry note still held at its first echo ends there) and their echoes.
// `random` = Delay Rand. is above 0 (LYNX draws with its own seed).
std::vector<TimedNote> echo (std::span<const float> params, const std::vector<TimedNote>& in, double bpm, bool& random);
// The module does something to the notes: Note Echo with repetitions, an Arpeggiator (a module switched off passes
// them; an Arpeggiator with both arps off is silent)
bool timeEffectActive (std::uint32_t type, std::span<const float> params) noexcept;
} // namespace lynx::flmnotefx
