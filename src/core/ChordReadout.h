#pragma once

// The chord panel's names and timing (FL 2026's Chord detection toolbar panel; behaviour measured in
// docs/analise/REA_rodada181b.md, LYNX's own code). No JUCE, no threads: the UI feeds it and shows what it says.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lynx::chordread
{
constexpr const char* noChord = "---";
constexpr int minKeys = 2, maxKeys = 10;
constexpr int channels = 16;
constexpr int foreverSeconds = 10000; // "Show forever"

struct NameOptions
{
    bool sharps = true;                                 // C♯ rather than D♭
    bool roman = false;                                 // numerals against `tonic` (when it is known)
    int tonic = -1;                                     // pitch class; < 0: no key, so letter names
    const std::array<std::string, 12>* notes = nullptr; // other note names ("Do", "Ré"...); nullptr: C D E...
};

struct KeysName
{
    std::string text;
    bool novelty = false; // too many tensions to spell: one of noveltyNames(), for the UI to translate
};

// Names a set of MIDI keys: two keys are an interval ("M 3rd"), three or more are scored against the chord families
// ("C", "Am 7", "G 7,♭9", "C ∆", "Bø", "C/E"). Keys are as played, no duplicates. Fewer than two: an empty name.
KeysName nameKeys (const std::vector<int>& keys, const NameOptions& options = {});
const std::array<const char*, 9>& noveltyNames();

// What the panel shows. Note-on shows at once; note-off waits for the hold time; a Piano Roll selection stays at
// least 2 s. Each MIDI channel counts its own keys (the typing keyboard plays on channel 0).
class Readout
{
public:
    void setOptions (const NameOptions& o) { options = o; }
    const NameOptions& getOptions() const { return options; }

    // The "Display delay for played chords" in milliseconds (0 = None). Lets a waiting change through at the next tick.
    void setDelayMs (std::uint32_t ms);
    std::uint32_t getDelayMs() const { return delayMs; }

    void note (int channel, int key, bool on, std::uint32_t nowMs);
    void selection (std::vector<int> keys, std::uint32_t nowMs);
    // Returns true when the text changed.
    bool tick (std::uint32_t nowMs);

    const std::string& text() const { return shown; }
    bool lastWasNovelty() const { return shownNovelty; }
    const std::vector<int>& held (int channel) const { return keys[(std::size_t) channel]; }

private:
    KeysName describe (const std::vector<int>& k) const;

    NameOptions options;
    std::uint32_t delayMs = 0, holdMs = 0, sinceMs = 0;
    bool waiting = false, waitingNovelty = false, shownNovelty = false;
    std::string shown = noChord, next;
    std::array<std::vector<int>, channels> keys;
};
} // namespace lynx::chordread
