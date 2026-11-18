#pragma once

// Round 190kx: the app's side of the loop analysis (core/LoopAnalysis.h). One worker for the process, made when
// something first asks for it (a Browser showing a folder with audio in the Simple layout, the Sample browser's Loop
// Browser, a drop, the clip menu's "Detect the key"), kept while anyone holds it; its results stay in
// core/LoopAnalysis's Cache for the session and in "loop-analysis.txt" in the settings folder between sessions (each
// entry with its file's size and modification time: a changed file is analysed again). Message thread only, except
// where noted.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <juce_events/juce_events.h>

#include "core/LoopAnalysis.h"
#include "core/Song.h"

namespace lynx
{
class AudioEngine;
}

namespace lynx::loopinfo
{
class Shared final : public juce::ChangeBroadcaster
{
public:
    // the process's worker (made on the first call; it ends when the last holder lets go)
    static std::shared_ptr<Shared> get (AudioEngine& engine);
    ~Shared() override;

    // files to analyse, ahead of what was asked before (audio files only; others are skipped)
    void request (const std::vector<std::string>& paths);
    // what is known of `path` in this session (no file access): nullopt until the worker has answered for it
    std::optional<loopanalysis::Result> known (const std::string& path) const;
    // the worker (tests, --snapshot)
    loopanalysis::Service& service() { return *worker; }
    std::size_t knownCount() const;

    explicit Shared (AudioEngine& engine);

private:
    void answered (const std::string& path); // (worker thread)

    struct Reader;
    std::unique_ptr<Reader> reader;
    mutable std::mutex lock;
    std::unordered_map<std::string, loopanalysis::Result> results;
    std::unique_ptr<loopanalysis::Service> worker; // (last: goes first)
};

// What a Browser row of a loop shows (BandLab's rows: "4 bars · 102bpm · D min"; LYNX's words): the bars (a loop of
// whole bars of `meter`), the tempo and the key; "" when nothing is known.
juce::String rowText (const loopanalysis::Result& r, Meter meter);
} // namespace lynx::loopinfo
