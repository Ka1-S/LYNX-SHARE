#pragma once

// Loop Starter, local (round 168b): pick a genre and LYNX builds a drum loop on the Channel Rack — kit channels with
// one-shots from the local sample library and generated steps — that the dice re-roll (samples or steps) and "Send to
// Playlist" places in the song. The workflow follows FL Studio 25's Loop Starter (REA_rodada168b-loopstarter.md):
//  - FL's genre configuration (System/Tools/Chord progression/Notes/genres.lstj: genres, labels, tempo ranges, the
//    kit slots "Kit 1".."Kit 6" with their one-shot categories, the kit weights) and the drum presets of the genres
//    (Notes/content/<genre> presets.lstj: 16-step lanes with per-bar variants) are read from the FL installation at
//    run time; without FL, LYNX uses a small configuration and patterns of its own.
//  - FL-exact parts: FL's own random generator (toolbox rand_int / rand_uniform / rand_tri), the tempo pick
//    (update_pattern_tempo), the blank kit slots (DrumKit.set_blanks), the per-channel preset pick weighted by the
//    presets' weights, the step tokens (rhythm / repeat / shift / octave / note / velocity loops of drum_setup.py), the
//    "Loop Starter #N" pattern name, which channels the dice touch (locked, selected) and the send to the playlist.
//  - LYNX's own parts: FL draws its audio loops and one-shots from FL Cloud and generates the steps with a Markov
//    sequencer that morphs the chosen preset; LYNX uses the local library (FL's installed packs + its built-in drums)
//    and plays the preset's own bars (its learnt variants) without the Markov morph.
// No JUCE here: the UI scans the folders and hands file paths in; tests drive everything below.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::loopstarter
{
// FL's random generator (toolbox.py rand_int, a xoroshiro-like 32-bit generator): the same seed gives the same
// numbers as FL's Python code.
class FlRand
{
public:
    explicit FlRand (std::uint64_t seed);
    std::uint32_t nextInt();           // 0 .. 2^31-1
    double uniform();                  // rand_uniform: nextInt / 2^31
    // rand_tri (mode 0..1, curvature >= 1): a triangular-ish draw in 0..1 peaking at `mode`
    double triangular (double mode, int curvature);
    // Python's random.shuffle semantics are not reproducible (FL uses the unseeded global generator): Fisher-Yates
    template <typename T>
    void shuffle (std::vector<T>& v)
    {
        for (std::size_t i = v.size(); i > 1; --i)
            std::swap (v[i - 1], v[(std::size_t) (nextInt() % i)]);
    }

private:
    std::uint64_t s0, s1;
};

struct KitSlot
{
    std::string label;                 // "Kit 1"
    std::vector<std::string> options;  // one-shot categories, first = preferred ("snare", "clap")
};

struct Genre
{
    std::string id;                    // "trap"
    std::string label;                 // "Trap"
    double weight = 1.0;
    std::vector<std::pair<std::string, double>> kit; // category -> weight (kept / blank odds)
    int sizeMin = 0;                   // kit size [min, max]; max < 0 = none
    int sizeMax = -1;
    double defaultTempo = 120.0, tempoMin = 0.0, tempoMax = 0.0;
    double defaultSwing = 0.0;
    std::vector<KitSlot> kitSlots;     // the template's Sampler "Drum" channels
    bool hasContent = true;            // FL lists only genres with "loops" or "oneshots" content
    double kitWeight (const std::string& category) const;
};

struct Config
{
    std::vector<Genre> genres;
    std::string defaultGenre;
    int sameSampleBlockCount = 3;      // same_sample_block_count.oneshot: a re-roll avoids the last N samples
    bool fromFl = false;
    const Genre* find (const std::string& id) const;
    std::vector<const Genre*> listed() const; // the genre picker's entries, in the file's order
};

// FL's genres.lstj (JSON); nullopt when it is not one.
std::optional<Config> parseConfig (const std::string& json);
// LYNX's own configuration (no FL installation).
Config builtinConfig();

// One bar of a preset lane: 16 tokens per string (FL's rhythm_strs etc.); empty = not given (use the lane's main bar).
struct LaneBar
{
    std::string rhythm, repeat, shift, oct, note, vel;
};

struct Lane
{
    std::vector<std::string> kit;      // the categories this lane plays ("ghost snare|clap" -> 2)
    LaneBar main;
    std::vector<LaneBar> bars;         // the preset's per-bar variants ("refs"); empty = one bar
};

struct Preset
{
    std::string name, genre;
    double weight = 1.0;
    std::vector<Lane> lanes;
    const Lane* laneFor (const std::string& category) const; // the lane whose first category matches, else any
};

// FL's "<genre> presets.lstj" (a JSON object of presets).
std::vector<Preset> parsePresets (const std::string& json);
// LYNX's own patterns for a genre (used when FL has no preset file for it, or without FL).
std::vector<Preset> builtinPresets (const std::string& genreId);
// The preset file name FL loads for a genre (util.all_preset_names_weights: "<genre> presets.lstj").
std::string presetFileName (const std::string& genreId);

// --- the steps ---------------------------------------------------------------------------------------------------
// The token tables of FL's drum loops (Notes/drum_setup.py): velocity states 72..124 step 4 (/128, default 7),
// shift states a..x = 0..23/24 of a step, octave q/r/s = -1/0/+1, note 0..b = semitones, repeat states 1..7 = a hit
// every {0.75, 2/3, 0.5, 0.375, 1/3, 0.25, 1/6} of a step (FL's note repeat; LYNX writes the repeated hits as notes).
float velocityOfToken (char token);    // 0..1
double shiftOfToken (char token);      // fraction of a step
int octaveOfToken (char token);
int semitoneOfToken (char token);
double repeatProportionOfToken (char token); // 1 = no repeat

constexpr int loopBars = 4; // the pattern length of a generated loop (bars)

// The lane's notes on `bars` bars (bar b plays the lane's variant b % its count) for a channel at `key`.
std::vector<Note> laneNotes (const Lane& lane, int bars, int channel, int key);

// --- the local sample library ------------------------------------------------------------------------------------
// The one-shot category of a sample from its path (folders + name), "" = not a drum one-shot (LYNX's own rules; FL
// tags its cloud samples instead).
std::string classifySample (const std::string& path);
// categories a slot / channel can fall back on when the library has none of its own (LYNX's own)
std::vector<std::string> fallbackCategories (const std::string& category);

struct Library
{
    std::map<std::string, std::vector<std::string>> byCategory;
    void add (const std::string& path); // classifies it; ignores non-drums
    bool empty() const noexcept { return byCategory.empty(); }
    std::size_t size() const noexcept;
};

// The built-in LYNX drum for a category (no sample library).
InstrumentKind builtinDrumFor (const std::string& category);

// --- generation --------------------------------------------------------------------------------------------------
struct ChannelPlan
{
    std::string slot;                  // "Kit 1"
    std::string category;              // "snare"
    std::string samplePath;            // "" = built-in drum
    bool blank = false;                // set_blanks: a slot left without steps
    std::vector<Note> notes;           // channel index 0, rebased by apply()
    int bars = 1;
};

struct Plan
{
    std::string genre;
    double bpm = 120.0;
    float swing = 0.0f;
    int bars = 1;
    std::vector<ChannelPlan> channels;
};

struct Sources
{
    const Config* config = nullptr;
    // presets per genre id (FL's file, else LYNX's own); filled by the caller
    std::map<std::string, std::vector<Preset>> presets;
    const Library* library = nullptr;
};

double pickTempo (const Genre& genre, FlRand& rng); // update_pattern_tempo (randomise)
// The tempo a genre suggests, as a whole number: what a starter button shows ("Lo-fi  84 BPM") and what the project
// gets when the starter is added with "Use the suggested tempo" — the same number, not pickTempo's random one from the
// genre's range (round 190t). The second form looks the genre up like generate() does (unknown: LYNX's own default).
int suggestedTempo (const Genre& genre);
int suggestedTempo (const Sources& sources, const std::string& genreId);
// picks a sample of the category (or of its fallbacks), avoiding `recent`; "" = none in the library
std::string pickSample (const Library& library, const std::string& category, FlRand& rng,
                        const std::vector<std::string>& recent = {}, std::string* pickedCategory = nullptr);
// picks the steps of one channel: a preset of the genre with a lane for the category (weighted), its notes
std::vector<Note> generateSteps (const Sources& sources, const std::string& genreId, const std::string& category,
                                 FlRand& rng, int* barsOut = nullptr, std::string* presetOut = nullptr);
Plan generate (const Sources& sources, const std::string& genreId, std::uint64_t seed);
// DrumKit.set_blanks: marks up to 2 slots blank (odds (1 - kit weight) * 0.3, lightest first), >= 2 stay
void setBlanks (const Genre& genre, std::vector<ChannelPlan>& channels, FlRand& rng, const std::vector<bool>& locked = {});

// --- applying it to a song ---------------------------------------------------------------------------------------
// "Loop Starter #N", N the first number no pattern uses
std::string nextPatternName (const Song& song);
// new pattern + channels (one shared insert "LS Steps"); returns the pattern index
int apply (Song& song, const Plan& plan);
// the Loop Starter channels of a pattern (channels whose loopStarter.pattern is it)
std::vector<int> channelsOf (const Song& song, int pattern);
void setChannelNotes (Song& song, int pattern, int channel, const std::vector<Note>& notes, int bars);
// The Simple workflow's starter beats (round 184a, core/SimpleTracks.h): the plan's kit as plain channels (one per row,
// in the plan's order, not Loop Starter channels: a sample or the built-in drum of the category) and its steps as one
// pattern whose notes' `channel` is the row, `bars` × 16 steps long. No insert, no group: SimpleTracks places them.
std::vector<ChannelDef> kitChannels (const Plan& plan);
Pattern kitPattern (const Plan& plan);

// Send to Playlist: the pattern at `at` on the first track with no clip from there to its end; the channels leave
// Loop Starter (FL: "unset channel Loop Starter status"). Returns the track, -1 if no track is free.
int sendToPlaylist (Song& song, int pattern, std::int64_t at);
} // namespace lynx::loopstarter
