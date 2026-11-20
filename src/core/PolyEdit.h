#pragma once

// Round 190lk: "Edit notes (polyphonic)" — per-note editing of chords and polyphonic audio clips (pitch, timing, length,
// level, mute of single notes inside a chord). A LYNX extra: FL Studio has no polyphonic note editor (its Newtone is
// monophonic); Melodyne's DNA does this as a plugin (LYNX hosts it through ARA, round 190kx). No JUCE.
//
// The engine is PitchNet's polyphonic engine (third_party/pitchnet-poly, AGPL-3.0, Session Loops and contributors;
// docs in its folder): multi-pitch estimation by harmonic summation with iterative estimation and cancellation
// (Klapuri), a learned note filter (optionally with Basic Pitch's note posteriorgram as evidence), a pitch-informed
// harmonic NMF sound model (Ewert & Müller) giving each note its share of the bins around its partials, and a
// renderer that moves an edited note's share in the spectrum with phase-coherent accumulation (Laroche & Dolson):
// frame spectrum = input − the edited note's share + that share transformed, then the inverse STFT. Notes without an
// edit are never touched (an unedited render is the input, sample for sample). Unpitched sound (attacks, breath) is
// not an object of its own; with "take background" an edited note takes its part of what no note owns along.
// The owner decided (2026-11-18) to ship this method knowing Celemony's DNA patent (US 8,022,286 to 2029-06-15,
// EP 2 099 024 in DE/FR/GB to 2029-02-05); round 190lh's research and round 190lk's STUDY.md have the comparison with
// a harmonic+noise design-around that was measured and not chosen.
//
// What LYNX keeps and does around it:
// - ChannelDef::polyEdit (an audio clip channel; its clips share the edits, as a channel's sample in Edison / Newtone /
//   an ARA plugin): the analysed notes (pitch track per frame, inharmonicity, level, score) and the edits, in a compact
//   binary blob saved with the project ("LXPE"). Reopening a project does not analyse again.
// - The masks (the NMF shares) are not kept or saved: the blocks an edit reaches are rebuilt when the edited clip is
//   rendered (third_party/pitchnet-poly/PolyLazy.cpp, a LYNX change) — memory follows the edited spans, not the length
//   of the clip (PitchNet keeps ~0.6 MB of masks per second of dense chords).
// - Playback: the clip channel plays the edited render of its sample (renderedFor), made once per edit state and cached;
//   nothing of this runs on the audio thread, and a channel without polyEdit costs nothing.

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polyphonic
{
struct Analysis;
struct NoteEvidence;
} // namespace polyphonic

namespace lynx
{
struct SampleData;

namespace polyedit
{
// One note's edit (PitchNet's NoteEdit; round 190lo: the pitch curve as Melodyne's / Newtone's modulation and drift scales
// plus a drawn curve)
struct Edit
{
    float semitones = 0.0f; // pitch offset (whole semitones from the UI; cents with Alt)
    float gainDb = 0.0f;    // level
    double moveSeconds = 0.0;
    float stretch = 1.0f;   // length ratio 0.25 .. 4 (the attack keeps its speed)
    bool mute = false;
    // round 190lo: the note's own pitch movement, split as in Newtone's natural engine (round 190li): the drift (the slow
    // wander around the note's centre, below ~3 Hz) and the modulation (vibrato, the rest), each scaled 0 .. 2 (1 = as
    // played, 0 = flat)
    float modulation = 1.0f;
    float drift = 1.0f;
    // round 190lo: a drawn pitch curve, semitones per analysis frame from the note's first frame (added to the rest;
    // empty = none)
    std::vector<float> curve;
    bool pitchMoves() const noexcept;
    bool identity() const noexcept { return ! mute && semitones == 0.0f && gainDb == 0.0f && moveSeconds == 0.0 && stretch == 1.0f && ! pitchMoves(); }
    bool operator== (const Edit&) const = default;
};

// One analysed note (an active note of PitchNet's analysis). Frames are analysis hops.
struct Note
{
    int startFrame = 0, endFrame = 0; // inclusive
    std::vector<float> f0;            // Hz per frame, startFrame..endFrame
    float inharmonicity = 0.0f;
    double energy = 0.0;
    float probability = 1.0f;
    // round 190lo: false = a candidate the note filter rejected (an overtone, a ghost, or a note it missed): drawn faint,
    // and the user can make it a note ("note assignment", as Melodyne's); true for every note of a round-190lk blob
    bool active = true;
    float medianMidi() const;         // the note's pitch (MIDI, fractional)
    float f0At (int frame) const;     // clamped to the note's frames
    bool operator== (const Note&) const = default;
};

struct Model
{
    double sampleRate = 0.0;
    std::int64_t frames = 0;          // the source's length when it was analysed (another length: analyse again)
    int fftSize = 4096, hop = 512;
    bool evidence = false;            // Basic Pitch's posteriorgram was used by the note filter
    std::vector<Note> notes;          // sorted by start
    std::map<int, Edit> edits;        // note index -> edit (identity edits are not kept)
    bool takeBackground = true;       // edited notes take their part of what no note owns (PitchNet's Lab default)
    // round 190lo: a moved note's copy joins what stays by level, not by a complex sum that partly cancels in the bins it
    // shares (third_party/pitchnet-poly Params::overlapAwareMoves); false for round-190lk blobs (they play as they did)
    bool overlapAwareMoves = true;

    bool edited() const noexcept;
    double frameSeconds() const noexcept { return sampleRate > 0.0 ? hop / sampleRate : 0.0; }
    bool operator== (const Model&) const = default;
};

// Round 190lo: how notes are found
enum class Detector
{
    harmonic, // PitchNet's harmonic-sum detector and note filter (with Basic Pitch's posteriorgram as evidence when given)
    guided,   // PitchNet's Basic Pitch-guided detector everywhere (needs evidence)
    fallback  // harmonic, then the guided detector in the regions where Basic Pitch hears notes the harmonic one missed
              // (LYNX, round 190lo: pads / detuned saws); = harmonic without evidence
};
struct AnalyseOptions
{
    Detector detector = Detector::fallback;
    bool keepCandidates = true; // keep the rejected candidates (Note::active false) for note assignment ...
    float candidateFloor = 0.1f; // ... those the note filter gave at least this probability, 0.1 s or longer (most
                                 // candidates are overtones and fragments below 0.1: ~200 for 9 s of a pad)
    bool slowReattacks = true;  // split notes at slow re-attacks (pads, strings: the same pitch in the next chord)
    bool rescueHeard = true;    // with evidence: a candidate the filter nearly took and Basic Pitch hears becomes a note
};

// The analysis of a clip's sample (mono mix of its channels). `evidence` may be null (DSP-only note filter). Runs on
// the calling thread (a worker: seconds per minute of audio); false when cancelled.
bool analyse (const SampleData& source, const polyphonic::NoteEvidence* evidence, Model& out, const std::atomic<bool>* cancel = nullptr,
              const std::function<void (float)>& progress = {}, const AnalyseOptions& options = {});

// Round 190lo: the index of model note `k` in toAnalysis()'s object (the engine wants the active notes first; candidates
// follow), -1 when out of range. Changing a note's `active` changes the mapping: build a new analysis then.
int analysisIndex (const Model& model, int k);
// the active notes' indices, in order (the identity of an analysis object: same list = the same masks)
std::vector<int> activeNotes (const Model& model);

// Round 190lo: a note's pitch track split for the modulation / drift scales: per frame of the note, the centre (MIDI),
// the drift (slow wander around it, semitones) and the modulation (the rest)
struct PitchParts
{
    float centre = 0.0f;
    std::vector<float> drift, modulation;
};
PitchParts pitchParts (const Note& note, double frameSeconds);
// the per-frame pitch offset (semitones, without Edit::semitones) the engine applies for `edit` on `note`; empty = none
std::vector<float> pitchCurveOf (const Note& note, const Edit& edit, double frameSeconds);

// Saved form ("LXPE" + version): notes and edits. An empty blob = not edited in the polyphonic editor.
std::vector<std::uint8_t> write (const Model& model);
std::optional<Model> parse (const std::vector<std::uint8_t>& blob);

// PitchNet's analysis object for a model (no masks yet), and the edits in its form
std::unique_ptr<polyphonic::Analysis> toAnalysis (const Model& model);

// The edited render of `source` (nullptr when no edit changes anything, or the model does not fit the source).
// `analysis` (optional) is reused between renders and keeps the masks it built (the editor passes its own).
std::shared_ptr<SampleData> render (const SampleData& source, const Model& model, polyphonic::Analysis* analysis = nullptr,
                                    const std::atomic<bool>* cancel = nullptr);

// One note (with its edit) on its own, from source frame `from` on (the editor's audition of a note): the samples the
// note reaches; empty when muted / nothing.
std::shared_ptr<SampleData> renderNote (const SampleData& source, const Model& model, int note, polyphonic::Analysis& analysis,
                                        std::int64_t* from = nullptr);

// What a channel plays: the cached render of `source` with the channel's polyEdit (`key` = the sample's path); the
// source itself when the blob is empty or has no edit. Renders on a cache miss (UI thread, at project load: the edited
// blocks only). The cache keeps the last few renders.
std::shared_ptr<const SampleData> renderedFor (const std::string& key, std::shared_ptr<const SampleData> source, const std::vector<std::uint8_t>& blob);
// Puts a render the editor already made into that cache (so the next rack build plays it at once).
void remember (const std::string& key, const std::vector<std::uint8_t>& blob, std::shared_ptr<const SampleData> rendered);
// A short key of a blob's edit state (for stretch / precompute caches)
std::string stateKey (const std::vector<std::uint8_t>& blob);
// Drops every cached render (tests, low memory)
void clearCache();

// --- round 190lo (UI): analyses prepared ahead of time ---------------------------------------------------------------------
// The editor needs PitchNet's analysis object with the masks of the edited notes' blocks (built on demand, 0.5–1.6 s per
// edited span) before it can render or let a dragged note be heard. "Prepare edited clips when a project opens" (a LYNX
// extra, off by default) builds them on a background thread for the clips that already have edits; the editor takes the
// prepared object when it opens. At most 4 objects / ~256 MB of masks are kept; the oldest go first.
// Builds the masks of every edited note's span of `model` in `analysis` (an object from toAnalysis (model)); false when
// cancelled or the model does not fit the source.
bool prepareMasks (const SampleData& source, const Model& model, polyphonic::Analysis& analysis, const std::atomic<bool>* cancel = nullptr);
void keepPrepared (const std::string& key, const Model& model, std::shared_ptr<polyphonic::Analysis> analysis);
// the prepared object for this sample and these notes (the same active set), taken out of the store; nullptr when none
std::shared_ptr<polyphonic::Analysis> takePrepared (const std::string& key, const Model& model);
std::size_t preparedCount();
void clearPrepared();
// Two models with the same analysed notes (their `active` flags may differ: a note assignment)
bool sameNotes (const Model& a, const Model& b);
} // namespace polyedit
} // namespace lynx
