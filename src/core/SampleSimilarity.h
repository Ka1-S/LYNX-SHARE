#pragma once

// "Find similar samples" for the Browser (round 190as, docs/analise/REA_rodada190as.md §1).
//
// FL Studio's "Find similar samples" lives in the Sounds tab (FL Cloud's online library) and in Loop Starter's channel
// menu: FL sends a query to the FL Cloud service, which ranks its own catalogue ("the similarity search includes
// both tone and rhythm", FL's manual). FL analyses nothing on the user's disk and keeps no local index. LYNX gives
// the same workflow for the files in the Browser's folders, with its own analysis, computed on this machine:
//
//  - analyse(): one fixed vector of 49 numbers per file from its first 20 s (after leading silence): tone (12 MFCC
//    means and deviations, spectral centre, its movement, flatness), shape (length, attack, where the energy sits,
//    decay, sustain), rhythm (number and rate of onsets, how regular they are, the period) and pitch classes (chroma).
//  - Index: the vectors of the analysed files, by path, kept in one bounded binary file between sessions.
//  - rank(): every dimension is scaled by its spread over the index, groups are weighted (tone and shape most), the
//    nearest files come first. Fixed and precomputed, so a query is instant once the files are analysed.
//  - Service: one background worker (lowest priority, at most half of one core, paused while LYNX plays or renders)
//    analyses the Browser's folders; a query is served before the pass goes on and analyses the clicked file first
//    when the pass did not reach it yet. Nothing is ever killed: the worker stops between two files.
//    Queries are answered by a second thread at normal priority (round 190dc): an answer never waits for the
//    lowest-priority worker, which a busy machine may not run for many seconds (macOS' background class, Windows'
//    background mode). The pass waits between two files while a query is waiting or being answered.
//
// No JUCE: the disk, the decoder and the index file go through callbacks (tests/Similar190asTests.inc).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/SearchIndex.h"

namespace lynx::similarity
{
inline constexpr int featureCount = 49;
inline constexpr double analysedSeconds = 20.0;     // of each file, after its leading silence
inline constexpr std::size_t maxIndexedFiles = 150000; // the index stops growing here (about 45 MB on disk)
inline constexpr std::int64_t maxFileBytes = 200ll * 1024 * 1024; // larger files are not analysed

using Features = std::array<float, featureCount>;

// The feature vector of mono audio (any sample rate from 8 kHz); false when it is silent or too short to analyse.
bool analyse (const float* mono, std::size_t frames, double sampleRate, Features& out);

struct Entry
{
    std::string path;
    std::int64_t size = 0, time = 0; // of the file when it was analysed
    bool valid = false;              // false: the file could not be decoded or is silent (kept so it is not retried)
    Features features {};
};

struct Match
{
    std::string path;
    float distance = 0.0f; // 0 = the same sound; grows with the difference
};

// How alike a distance reads, 0..100 (100 = the same)
int percent (float distance) noexcept;

class Index
{
public:
    // Adds or replaces the entry of `entry.path`; false when the index is full and the path is new
    bool put (Entry entry);
    const Entry* find (const std::string& path) const;
    bool remove (const std::string& path);
    // Drops the entries directly in `folder` whose file name is not in `names` (the folder was listed again)
    int prune (const std::string& folder, const std::vector<std::string>& names, char separator);
    std::size_t size() const noexcept { return entries.size(); }
    std::size_t validCount() const noexcept { return valid; }
    void clear();
    const std::vector<Entry>& all() const noexcept { return entries; }

    // The files most like `query` (never `exclude`), nearest first, at most `max`; only those under `roots` when given
    std::vector<Match> rank (const Features& query, const std::string& exclude, const std::vector<std::string>& roots, std::size_t max) const;

    // The index file: "LYNXSIM1", then the entries (little-endian)
    std::string toBytes() const;
    bool fromBytes (const std::string& bytes);

private:
    void updateScale() const;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::size_t> byPath;
    std::size_t valid = 0;
    mutable bool scaleDirty = true;
    mutable Features mean {}, deviation {};
};

struct Status
{
    std::size_t files = 0;     // analysed files in the index (usable ones)
    bool loaded = false;       // the index file was read (or there was none)
    bool busy = false;         // a pass is analysing
    bool paused = false;       // ... but waits while LYNX plays / renders
    bool full = false;         // the index reached maxIndexedFiles
    std::size_t analysed = 0;  // files analysed by the running pass
    std::string current;       // the folder the pass is in
};

struct Result
{
    std::uint64_t id = 0;
    std::string query;          // the file asked about
    std::vector<Match> matches; // nearest first
    bool failed = false;        // the file could not be analysed (not audio, silent, unreadable)
    bool partial = false;       // the pass is still analysing: more files can turn up
    std::size_t searched = 0;   // files compared
};

class Service
{
public:
    struct Callbacks
    {
        search::FileSystem fs;                                                                         // folder listings (the Browser's)
        std::function<bool (const std::string& path, std::int64_t& size, std::int64_t& time)> stat;    // false: no such file
        // The file's first `seconds` as mono; false when it can't be read. Called on the worker and the answering
        // thread, at the same time when a query comes in while the pass analyses a file.
        std::function<bool (const std::string& path, double seconds, std::vector<float>& mono, double& sampleRate)> decode;
        std::function<bool (const std::string& fileName)> accepts; // an audio file LYNX can analyse (by name)
        std::function<std::string()> load;                         // the index file's bytes ("" when there is none)
        std::function<void (const std::string&)> save;
        std::function<void()> statusChanged;                       // (worker threads)
        std::function<bool()> shouldPause;                         // LYNX is playing, recording or rendering
        bool throttle = true;                                      // rest as long as each file took (tests: off)
    };

    explicit Service (Callbacks callbacks);
    ~Service(); // stops between two files, saves, joins

    Service (const Service&) = delete;
    Service& operator= (const Service&) = delete;

    // A background pass over these folders: new files are analysed, files gone from a listed folder are dropped.
    void refresh (std::vector<std::string> roots);

    // The files most like `path` under `roots` (all when empty). Replaces a query still waiting. `done` is called on
    // the answering thread, never after a newer findSimilar() or cancel(). Returns the query's id (Result::id).
    std::uint64_t findSimilar (std::string path, std::vector<std::string> roots, std::size_t max, std::function<void (const Result&)> done);
    void cancel();

    Status status() const;
    bool waitIdle (int timeoutMs); // tests, --snapshot

private:
    struct Query
    {
        std::uint64_t id = 0;
        std::string path;
        std::vector<std::string> roots;
        std::size_t max = 0;
        std::function<void (const Result&)> done;
    };

    void run();    // the pass (lowest priority)
    void answer(); // the queries (normal priority)
    void loadIndex();
    void saveIndex();
    void runQuery (Query& query);
    bool runPass (std::vector<std::string>& folders); // false: closing
    bool analyseFile (const std::string& path, Entry& entry);
    bool interrupted() const;
    bool idleLocked() const; // (holding `lock`)
    void setStatus (const std::function<void (Status&)>& change, bool notify = true);
    void rest (double workedSeconds);

    Callbacks callbacks;

    mutable std::mutex indexLock; // guards `index`
    Index index;
    std::atomic<bool> dirty { false };

    mutable std::mutex lock; // guards everything below
    std::condition_variable wake, idleWake;
    bool stopping = false;
    bool loading = true;      // the index file is being read (before any query or pass)
    bool answering = false;   // a query is being answered
    bool passRunning = false; // the worker is in a pass (until its status is updated)
    std::optional<Query> pending;
    std::uint64_t latest = 0;
    std::deque<std::vector<std::string>> passes; // each: the folders still to walk (a stack)
    Status state;

    std::thread asker, worker; // (last: started when everything else exists)
};
} // namespace lynx::similarity
