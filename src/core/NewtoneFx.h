#pragma once

// FL Studio's Newtone (pitch editor) and Newtime (time warper) as LYNX mixer effects, described in
// docs/analise/REA_rodada73.md (clean-room: layouts and behaviour in our own words; no Image-Line or zplane code or data
// in this repository).
//
// Both plugins are editors: the user loads a sample into them, edits it, and exports the result. On a mixer insert their
// render passes the insert's input through unchanged, and only when the plugin's "slave to host playback" switch is on
// AND FL is playing does the plugin's engine add the edited sample, positioned at the song time (REA_rodada73 §3).
// LYNX reproduces the pass-through exactly and, in slave mode, adds the source sample as it is (the edits themselves —
// zplane's pitch/time engines — are not reproduced; see §5).
//
// Parameters (EffectKind::newtone / EffectKind::newtime):  [0] slave to host playback (0/1).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"
#include "core/TimeStretch.h"

namespace lynx
{
struct SampleData;
namespace audioedit
{
struct Document;
}
}

namespace lynx::newtonefx
{
constexpr int paramCount = 1;

// One note ("pitch object", XML "Pitch_N") as Newtone's render uses it (REA_rodada95 §2). Times in source samples.
struct Note
{
    std::int64_t origStart = 0, origEnd = 0; // iOrigStartSamplePos / iOrigEndSamplePos: where it lies in the source
    std::int64_t curStart = 0, curEnd = 0;   // iCurStartSamplePos / iCurEndSamplePos: where it plays (it owns [curStart, curEnd))
    double origPitch = 0.0;                  // fOrigPitch (MIDI note; shown, not used by the render)
    double shift = 0.0;                      // fCurrentPitchOffset + fAdditionalPitchOffset, semitones (fCurPitch is not read)
    double stretch = 1.0;                    // fCurInvStretchFactor: source samples per output sample
    double volume = 1.0, formant = 1.0, drift = 1.0;   // fCurVolume, fCurFormantFactor, fDrift
    double smoothIn = 0.0, smoothOut = 0.0;            // fPitchSmoothInTime / fPitchSmoothOutTime (samples)
    double origSmoothIn = 0.0, origSmoothOut = 0.0;    // fOrigPitchSmooth*Time: the analysed ones (the Transition knob scales them)
    double fadeInTime = 0.0, fadeOutTime = 0.0, fadeInGain = 1.0, fadeOutGain = 1.0;
    double vibPeriod = 0.0, vibAmpStart = 0.0, vibAmpEnd = 0.0, tremAmp = 0.0; // fVibFreq is a period in seconds
    double rampIn = 0.0, rampOut = 0.0;      // fRampIn / fRampOut: semitones at the note's start / end, gone after smoothIn / before smoothOut
    bool enabled = true;
    // round 190kj: FL's analysis tiles the whole file (a note owns the silence after it); iOrigValidNoteEnd = where its
    // voiced part ends in the source (0 = an unvoiced note, e.g. the lead-in before the first note; -1 = not known:
    // the whole note), fOrigVelocity = its analysed level 0..1 (-1 = not known)
    std::int64_t validEnd = -1;
    double velocity = -1.0;
    // round 190li (LYNX extras, as Melodyne's Pitch Modulation / Pitch Drift tools): the sung deviation from the note's
    // centre split into its slow part (drift, under ~2.5 Hz) and its fast part (vibrato, modulation), each scaled on its
    // own on top of `drift` (FL's Variation, which scales both). 1 = as sung.
    double modulation = 1.0, pitchDrift = 1.0;
    // round 190li: the note's sung pitch centre (MIDI, fractional): the mean of its pitch over the held part (the scoop
    // into it and the glide out of it left out; exact periods, not FL's whole-sample ones). Engine 1's Center and its
    // drift / modulation pivot use it; 0 = not known (an older state: origPitch is used).
    double sungPitch = 0.0;
    bool voiced() const noexcept { return validEnd != 0; }
    std::int64_t voicedEnd() const noexcept { return validEnd > origStart ? std::min (validEnd, origEnd) : origEnd; }

    // the render differs from the source over this note
    bool edited (double globalFormant) const noexcept;
};

// One track of the engine's project (XML "Trk_N")
struct Track
{
    int number = 0;
    bool timeWarp = false;        // "CzplJTimelineWarpSource" (Newtime); otherwise "CzplJPositionablePSOLASource" (Newtone)
    std::string filePath;         // the engine's working copy of the sample (a temporary file of the session that saved it)
    int notes = 0;                // pitch objects ("NumOfPitches")
    std::int64_t frames = 0;      // "OriginalFileLength"
    double selfBpm = 0.0;         // "SelfBPM"
    bool edited = false;          // a note or a global setting differs from the analysis (pitch, time, volume, formant...)
    std::vector<Note> pitches;    // the notes, in order
    double globalFormant = 1.0;   // GlobalFormantShift (the one global the render reads)
    // the editor's global knobs as last set (FL folds them into the notes when it saves; so does LYNX): Center 0..1
    // (pull toward the note's semitone), Variation 0..1 (the notes' drift), Transition 0..2 (× the analysed times)
    double centre = 0.0, variation = 1.0, transition = 1.0;
    int key = -1; // FL's KeyIdx (the sample's detected key: 0-11 C..B major, 12-23 C..B minor); -1 = not known
    // round 190li: the render engine's revision. 0 = the engine as built up to round 190kx (FL's transitions, plain
    // TD-PSOLA; every older project and FL import keeps it, bit for bit); 1 = LYNX's natural engine (round 190li,
    // scored against Melodyne 5: tools/vocallab): a voicing-aware pitch-synchronous resynthesis, smooth note joins,
    // the drift / modulation split. A new Newtone slot gets `latestEngine`.
    int engine = 0;
    // rollup 0.1.30 step 8 (owner, 2026-11-18): the notes come from the "Separate close notes" analysis
    // (AnalyseOptions::fineSplit). Only an analysis sets it; saved as layout 5, only when set (else as before, byte for
    // byte). The editor's tick shows it.
    bool fineSplit = false;
};
constexpr int latestEngine = 1;

// The plugin's saved state (record 0xD5 of a mixer slot), REA_rodada73 §2
struct State
{
    int version = 0;                 // Newtone 0, Newtime 1
    int editorWidth = 0, editorHeight = 0;
    std::uint32_t flags = 0;         // options (bit 2: slave to host playback, bit 3: ignore host selection, bit 4: ignore own selection...)
    std::string sourcePath;          // record 3: the sample the user loaded, as the host stored it (ANSI); "" = none
    std::string projectXml;          // the engine's project (JUCE XML) from record 2
    std::vector<Track> tracks;

    bool slave() const noexcept { return (flags & 4u) != 0; }
    bool edited() const noexcept;
};

// nullopt = not a Newtone/Newtime state (wrong version, broken records)
std::optional<State> parse (const std::vector<std::uint8_t>& state);

// Parameters for a slot built from FL's state
void toParams (const State& s, EffectParams& params) noexcept;
std::string paramText (int param, float value01);

// The effect: the input passes through; with [0] on, while the host plays, `source` is added from the song time on.
// `source` may be null (sample not found): the effect is then a pass-through.
std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source);
// The same for a Newtone slot with FL's saved state: with note edits, the edited render of the first track plays
// instead of the plain source (renderEdits; Newtime's warping is still not reproduced).
std::unique_ptr<Effect> make (std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& state);

// LYNX's Newtone engine (REA_rodada95 §2-3): renders a track's note edits over its source sample — pitch offsets with
// the note transitions, drift, vibrato, formant factor, volume and fades, timing (moved / stretched notes),
// disabled notes — with LYNX's own pitch analysis (YIN) and TD-PSOLA (core/VocalPitch.h). Unedited notes are copied
// sample for sample. nullptr when nothing differs from the source.
std::shared_ptr<SampleData> renderEdits (const SampleData& source, const Track& track);

// ------------------------------------------------------------------------------------------------------------------
// LYNX's own Newtone / Newtime projects (round 142, issue #5): a slot made in a LYNX project keeps its edits in its
// own format in EffectSlot::state (FL's XML project is only read). The source sample is EffectSlot::samplePath.

// LYNX's note analysis of a sample (YIN pitch track, core/VocalPitch.h): voiced stretches of steady pitch become
// notes (nothing edited). Newtone: timeWarp = false. Round 190kj, as FL's Newtone analyses (measured on its own DLL,
// REA_rodada190kj): the notes tile the file back to back (a lead-in note from 0 to the first one, each note owns the
// silence after it, validEnd = where its voice ends), origPitch from whole-sample periods (fs / floor(fs / f0)), volume
// fades 0.2 x the note's length, pitch smooth-in / out times measured from the glides (FL's ~73 ms after silence).
// Round 190kx (LYNX extra, off by default = FL's analysis above): `fineSplit` also ends a note where the pitch steps
// by a third of a semitone or more and holds (FL: 0.7), and where the level dips between two voiced stretches (a new
// attack on the same breath). Melodyne 5 (its Melodic algorithm, round 190kx's reference captures) separates notes 45
// cents apart; FL's analysis joins them into one note between the two pitches, which Center then moves to the wrong
// semitone.
// Round 190li: `octaveGuard` (on by default): where the FL-fitted loose voicing threshold (0.3) takes a period one or
// more octaves off the strict one (0.15), the strict one's pitch is used (YIN's first dip under a loose threshold can
// be a formant's sub-period: a low male voice came out up to two octaves high, breaking Center and Snap to scale);
// the voicing and the segmentation rules are FL's as before. Stored notes never change: only a new analysis uses it.
struct AnalyseOptions
{
    bool fineSplit = false;
    bool octaveGuard = true;
};
Track analyseNotes (const SampleData& source, const AnalyseOptions& options = {});
// The analysis a NEW Newtone slot gets (owner, 2026-11-18): with LYNX's natural engine (latestEngine ≥ 1)
// "Separate close notes" is on; FL imports, older projects and any other analysis keep FL's (AnalyseOptions {}).
AnalyseOptions newSlotAnalyseOptions() noexcept;
// A new slot's notes: newSlotAnalyseOptions()'s analysis, rendered with `latestEngine`
Track analyseNewSlot (const SampleData& source);

// ------------------------------------------------------------------------------------------------------------------
// LYNX's Newtone editor tools (round 165g, FL's workflow interpreted: Cut mode / glue, the Center / Variation /
// Transition knobs, snap to scale, score export). `which` = the notes to change; empty = every note.

// Cut mode: note k becomes two at output sample `at` (strictly inside it); the halves keep every edit. `pitchOf`
// gives the source's median pitch over a source range (each half's origPitch); null keeps the note's.
bool splitNote (Track& track, std::size_t k, std::int64_t at, double (*pitchOf) (const void*, std::int64_t, std::int64_t) = nullptr,
                const void* context = nullptr);
// Glue: note k joins the note before it (adjacent in the source and in time, same stretch); the joined note keeps
// the earlier one's edits, its pitch = the length-weighted mean.
bool glueNote (Track& track, std::size_t k);
// Center: every chosen note lands on its semitone by `amount` (0 = as sung, 1 = exactly), keeping the semitone it
// was moved to. Variation: their drift (0 = flat, 1 = as sung). Transition: their smooth times = analysed × factor.
void applyCentre (Track& track, double amount, const std::vector<int>& which = {});
void applyVariation (Track& track, double amount, const std::vector<int>& which = {});
void applyTransition (Track& track, double factor, const std::vector<int>& which = {});
// Round 190li (LYNX extras): the chosen notes' pitch modulation (vibrato: the fast deviation) and pitch drift (the slow
// one), 0 = removed, 1 = as sung, 2 = doubled — Melodyne's Pitch Modulation / Pitch Drift tools; FL's Variation scales
// both together. Correct pitch (Melodyne's macro): centre `centre` and drift `drift` amounts, 0..1, vibrato kept.
void applyModulation (Track& track, double amount, const std::vector<int>& which = {});
void applyPitchDrift (Track& track, double amount, const std::vector<int>& which = {});
void correctPitch (Track& track, double centre, double drift, const std::vector<int>& which = {});
// The nearest allowed note to `pitch` (MIDI) in a scale: `mask` bit n = pitch class (key + n) % 12 allowed;
// 0xFFF = chromatic. Ties go down.
double nearestInScale (double pitch, int key, std::uint16_t mask) noexcept;
// Snap to scale: the chosen notes move to the nearest allowed note (whole semitones, the sung detune removed)
void snapToScale (Track& track, int key, std::uint16_t mask, const std::vector<int>& which = {});

// The track's notes as a score (FL's Create Score / Save as MIDI): enabled voiced notes, key = the semitone they
// sound at, times in seconds
struct ScoreNote
{
    double start = 0.0, length = 0.0;
    int key = 60;
    float velocity = 0.78f;
};
std::vector<ScoreNote> score (const Track& track, double sampleRate);

// Edison's "Convert to score and dump to piano roll" (round 167c, REA_rodada167c.md §4): FL runs its pitch analyser
// (the one Newtone uses) on the whole sample and takes each note it finds: key = round(pitch - the sample's overall
// detune), only keys 0..127; volume = 0.5 + 0.5 x the note's level. LYNX: analyseNotes(), the detune = the notes'
// length-weighted mean offset from their semitones, the level = the note's RMS over the loudest note's (0..1).
// Times in source frames, [start, end).
struct DetectedNote
{
    std::int64_t start = 0, end = 0;
    int key = 60;
    float velocity = 1.0f;
};
std::vector<DetectedNote> detectScore (const SampleData& source, double* detune = nullptr);
// FL hands them to the Piano roll of the selected channel in the current pattern, replacing that channel's notes
// there (the plugin SDK's note list: piano roll, flags "empty first" + "use selection", pattern -1, channel -1):
// position = frames / rate x tempo / 60 x PPQ (rounded), notes of 3 ticks or less at FL's PPQ 96 (LYNX: 30 ticks)
// are left out. Returns how many notes went in.
int dumpScore (Song& song, int channel, const std::vector<DetectedNote>& notes, double sampleRate);

// The key of a track's notes as FL's KeyIdx (0-11 C..B major, 12-23 C..B minor): Krumhansl-Schmuckler on the voiced
// notes' pitch classes weighted by length (FL's own detector is zplane's: this matched it on 17 of 23 files, the
// others a relative or neighbouring key); -1 without voiced notes. And a key's scale as a Snap-to-scale mask.
int detectKey (const Track& track);
std::uint16_t keyScaleMask (int key) noexcept; // bits from the key's tonic (natural minor for 12-23)

// Round 190kj, FL's Newtone editor commands:
// Select › Select time around selection: the output time [start, end) the chosen notes take (their voiced parts).
std::pair<std::int64_t, std::int64_t> timeAround (const Track& track, const std::vector<int>& which);
// The voiced notes that overlap output time [a, b) (Select before / after current selection).
std::vector<int> notesIn (const Track& track, std::int64_t a, std::int64_t b);
// File › Export regions: `audio` (the edited sample) with one region per voiced note, named after the note it sounds
// (FL's key names, C5 = 60) and linked to that key; Send to Playlist as audio clip: the same marks as markers at the
// notes' starts (`asMarkers`).
audioedit::Document regionsDocument (const SampleData& audio, const Track& track, bool asMarkers = false);
// Edit › Quantize time (Ctrl+Q): the chosen notes' starts (all when none) moved to the nearest line of a grid of `grid`
// output samples; the note before a joined one ends there; both keep their audio, stretched to the new lengths.
void quantizeTime (Track& track, const std::vector<int>& which, double grid);
// Sync: every note placed and stretched by `ratio` (output length / source length; 1 = as recorded), pitch kept.
void stretchAll (Track& track, double ratio);
// Frames [a, b) of a sample (a new sample; the whole one when the range is empty).
std::shared_ptr<SampleData> cropped (const SampleData& s, std::int64_t a, std::int64_t b);

// Newtone: the track's notes. "LXNT" + version + frames + global formant + the knobs (+ the key, version 3) + the notes.
std::vector<std::uint8_t> writeNative (const Track& track);
// The same in an older layout (2: without the key and the notes' voiced end / velocity, as 0.1.28 reads it; rollup
// 0.1.30 step 6); clamped to 2…the current version.
std::vector<std::uint8_t> writeNative (const Track& track, std::uint32_t version);
// The layout version of a state ("LXNT"; 0: not one)
inline std::uint32_t nativeVersionOf (const std::vector<std::uint8_t>& state)
{
    return state.size() >= 8 && state[0] == 'L' && state[1] == 'X' && state[2] == 'N' && state[3] == 'T'
               ? (std::uint32_t) state[4] | ((std::uint32_t) state[5] << 8) | ((std::uint32_t) state[6] << 16) | ((std::uint32_t) state[7] << 24)
               : 0u;
}
std::optional<Track> parseNative (const std::vector<std::uint8_t>& state);
// The track a slot's state describes: LYNX's own, or the first Newtone track of FL's state (nullopt: none)
std::optional<Track> trackOf (const std::vector<std::uint8_t>& state);

// Newtime: warp markers (source sample → output sample), in increasing order on both axes; between two markers the
// source plays stretched (pitch kept) so that it lasts the output distance. Implicit markers at 0 → 0 and at the
// end (the last marker's slope continued). "LXNW" + version + frames + the markers.
struct WarpMarker
{
    std::int64_t source = 0, output = 0;
    bool operator== (const WarpMarker&) const = default;
};
struct WarpMap
{
    std::int64_t frames = 0; // the source's length when the markers were placed
    std::vector<WarpMarker> markers;
    bool operator== (const WarpMap&) const = default;
};
std::vector<std::uint8_t> writeWarp (const WarpMap& map);
std::optional<WarpMap> parseWarp (const std::vector<std::uint8_t>& state);
// The output length of the warped source; and where a source sample lands (and back)
std::int64_t warpedLength (const WarpMap& map, std::int64_t frames);
double warpToOutput (const WarpMap& map, std::int64_t frames, double source);
double warpToSource (const WarpMap& map, std::int64_t frames, double output);
// The warped source (nullptr: no marker moves anything)
std::shared_ptr<SampleData> renderWarp (const SampleData& source, const WarpMap& map);
// FL's Newtime warp (round 167b): a TimelineWarp track's segments become the markers (each segment's source start →
// its output start, and the last one's end); nullopt = not a Newtime track
std::optional<WarpMap> warpFromTrack (const Track& track);
// The warp a slot's state describes: LYNX's own ("LXNW") or the first Newtime track of FL's state
std::optional<WarpMap> warpOf (const std::vector<std::uint8_t>& state);

// FL Newtone / Newtime's Snap to grid (Beat, 1/2 ... 1/8 beat, Off): a sample position moved to the nearest line
// of a grid of beat / division at the song tempo, counted from the sample's start; division <= 0 = Off
std::int64_t snapToBeatGrid (std::int64_t position, double sampleRate, double bpm, int division);

// What a slot plays in slave mode: the source with its edits (Newtone notes / Newtime warp; LYNX's or FL's state)
std::shared_ptr<const SampleData> renderedSource (std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& state);
} // namespace lynx::newtonefx
