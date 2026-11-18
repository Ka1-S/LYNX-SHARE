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
// One note's edit (PitchNet's NoteEdit without the per-frame pitch curve)
struct Edit
{
    float semitones = 0.0f; // pitch offset (whole semitones from the UI; cents with Alt)
    float gainDb = 0.0f;    // level
    double moveSeconds = 0.0;
    float stretch = 1.0f;   // length ratio 0.25 .. 4 (the attack keeps its speed)
    bool mute = false;
    bool identity() const noexcept { return ! mute && semitones == 0.0f && gainDb == 0.0f && moveSeconds == 0.0 && stretch == 1.0f; }
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

    bool edited() const noexcept;
    double frameSeconds() const noexcept { return sampleRate > 0.0 ? hop / sampleRate : 0.0; }
    bool operator== (const Model&) const = default;
};

// The analysis of a clip's sample (mono mix of its channels). `evidence` may be null (DSP-only note filter). Runs on
// the calling thread (a worker: seconds per minute of audio); false when cancelled.
bool analyse (const SampleData& source, const polyphonic::NoteEvidence* evidence, Model& out, const std::atomic<bool>* cancel = nullptr,
              const std::function<void (float)>& progress = {});

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
} // namespace polyedit
} // namespace lynx
