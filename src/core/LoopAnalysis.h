#pragma once

// Loop analysis (round 190kx; LYNX's own, clean-room): the key and the tempo of the USER'S OWN loops, for the Browser
// (Simple workflow rows, the Sample browser's Loop Browser), the drop paths and the clip menu. No JUCE.
//  - analyse(): the tempo (the WAV's ACID beats, the file name, else detected from the audio), whether the file is a
//    loop of whole beats, and the key (the file name, else detected from the audio), each with where it came from and
//    how sure it is;
//  - Cache: results kept per file (path + size + modification time), shared by the Browser and the drop paths, so a file
//    is analysed once per session; the analysis itself runs on a worker thread (the UI's job), never on idle.
// The detectors are LYNX's own implementations of published methods (references at each function in LoopAnalysis.cpp);
// keyfollow::detectKey and tempodetect::detect stay as they are (FL's "Detect tempo" dialog and Auto-warp use them).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/KeyFollow.h"

namespace lynx
{
struct SampleData;
}

namespace lynx::loopanalysis
{
enum class TempoSource : std::uint8_t
{
    none,
    acid,    // the WAV's ACID beat count
    name,    // the file name
    detected // the audio
};

enum class KeySource : std::uint8_t
{
    none,
    name,
    detected
};

struct Result
{
    double seconds = 0.0;
    double bpm = 0.0;          // 0 = no tempo (a one-shot, a take without a pulse)
    double tempoConfidence = 0.0; // 0..1 (1 for ACID / name)
    double beats = 0.0;        // the file's length in beats at `bpm`
    bool wholeBeats = false;   // a loop of whole beats (4 or more, within 4 % of a beat): it can be fitted to a tempo
    TempoSource tempoSource = TempoSource::none;
    std::optional<keyfollow::LoopKey> key;
    float keyConfidence = 0.0f; // 0..1 (1 for a key in the name)
    KeySource keySource = KeySource::none;

    bool isLoop() const noexcept { return wholeBeats && bpm > 0.0; }
};

// Longest file analysed (longer is a song or a take); the audio detectors read at most this much.
constexpr double maxSeconds = 70.0;

// The detectors' settings (the accuracy tool tries others; the app uses the defaults, measured in round 190kx).
struct KeyOptions
{
    int profile = 2;              // 0 Krumhansl–Kessler, 1 Temperley (Kostka–Payne), 2 Sapp's simple weights
    bool compress = false;        // a peak counts with the square root of its height
    int harmonics = 1;            // 1: the peak's own pitch class; up to 4: also the notes it may be harmonic 2..4 of
    double harmonicWeight = 0.6;  // per harmonic step
    double bassWeight = 0.0;      // the bass profile (peaks below bassHz) added with this weight
    double bassHz = 200.0;
    double lowHz = 55.0, highHz = 2100.0;
    double minCorrelation = 0.6, minMargin = 0.05;
    int minPitchClasses = 4;      // pitch classes of at least 5 % each (a single chord, three notes, sets no key)
    int minChords = 2;            // different sets of the three strongest pitch classes over the frames (one held chord: none)
    bool tonicDecision = true;    // a major key or its relative minor: decided by the tonic and its fifth
    double tonicBass = 1.0, tonicStart = 2.0, startSeconds = 0.5; // ... with the bass and the loop's start weighted in
    bool hpss = true;             // harmonic / percussive separation first (drums leave fewer peaks)
    int hpssFrames = 2;           // the time median's half width (frames of 4096)
};
KeyOptions& keyOptions();

struct TempoOptions
{
    double minConfidence = 0.35;   // below: no tempo (as previewtempo's detection)
    double minBpm = 70.0, maxBpm = 180.0; // the range the tempo is given in
    bool circular = true;          // a loop's onset curve read around (its end leads to its start)
    bool loopCandidates = true;    // the tempos that make the file whole bars decide the pulse's family
    double loopShare = 0.6;        // ... when the comb finds them this strong (of the free pulse's score)
    bool octaveWalk = true;        // the beat level decided by the backbeat, else by the comb with a mild prior
    double backbeatLevel = 2.0;    // the snare on the off-beats: at least this many times the snare curve's mean
    double backbeatRatio = 1.5;    // ... and this many times the snare on the beats
    double kickLevel = 2.0, kickRatio = 2.0; // the kicks on the beats: this many times their mean, and the off-beats' (0: not needed)
    double loopConfidence = 0.5;   // the confidence a pulse the loop's length agrees with gets at least
    bool detectorOctave = true;    // without a backbeat: the octave tempodetect::detect reads, when it is of the same pulse
    double priorCentre = 112.0, priorSigma = 0.8; // ... else the octave preference (BPM, octaves)
    double wholeTolerance = 0.02;  // beats off a whole number (per 8 beats: 0.25 %) for a loop of whole beats
};
TempoOptions& tempoOptions();

// The tempo detector for loops: `bpm` folded into 70–180, its confidence and the length in beats.
struct Tempo
{
    double bpm = 0.0, confidence = 0.0, beats = 0.0;
};
Tempo detectTempo (const SampleData& audio);

// The key detector for loops (a key, or none for drums / noise / one-shots).
keyfollow::Detection detectKey (const SampleData& audio);

// Everything for a file of `fileName` (without folder; the extension is ignored). `useNameAndAcid` = false reads the
// audio only (the accuracy tool). Reads the audio: call it on a worker thread.
Result analyse (const SampleData& audio, const std::string& fileName, bool useNameAndAcid = true);

// "102 BPM", "D minor" / "D min" (short), "102 BPM · D min": what the Browser shows ("" when nothing is known)
std::string tempoText (const Result& r);
std::string keyText (const Result& r, bool shortForm);

// The Simple layout's drops (BandLab fits its loops to the project's tempo and key): a loop whose file and name say
// nothing of its tempo or key also takes what the analysis finds in its audio — a loop of whole beats is fitted to the
// tempo, a key it finds makes it follow the project key. On by default (BandLab's behaviour); Settings › LYNX extras
// and the Simple layout's Browser options have the switch (ui-options.txt "simpleLoopDetect").
inline std::atomic<bool>& dropDetects() noexcept
{
    static std::atomic<bool> on { true };
    return on;
}

// --- the cache --------------------------------------------------------------------------------------------------------
struct Stamp
{
    std::string path;
    std::int64_t size = 0, modified = 0; // bytes, ms since 1970
    bool operator== (const Stamp&) const = default;
};

class Cache
{
public:
    static Cache& shared();
    std::optional<Result> find (const Stamp& stamp) const;
    // the result kept for `path`, whatever its stamp (the worker's newest answer for it)
    std::optional<Result> findPath (const std::string& path) const;
    std::vector<std::pair<Stamp, Result>> all() const; // insertion order (saving)
    void put (const Stamp& stamp, const Result& result);
    // a file analysed with this path but another size / time is out of date: find() misses it
    std::size_t size() const;
    void clear();
    static constexpr std::size_t capacity = 8192; // entries (a few hundred bytes each); the oldest half goes beyond it

private:
    mutable std::mutex lock;
    std::unordered_map<std::string, std::pair<Stamp, Result>> entries;
    std::vector<std::string> order; // insertion order, for trimming
};

// One line per file: "path\tsize\tmodified\tseconds\tbpm\tconfidence\tbeats\twhole\ttempoSource\tkey\tminor\tkeyConfidence\tkeySource"
// (key -1 = none). Paths with a tab or a line break are not kept. fromText() skips lines it can't read.
std::string toText (const Stamp& stamp, const Result& r);
bool fromText (const std::string& line, Stamp& stamp, Result& r);

// --- the worker ---------------------------------------------------------------------------------------------------
// Analyses the files asked for, one at a time, on a thread of the lowest priority; results go into Cache::shared().
// Nothing runs unless asked: the Browser asks for the audio files of a folder it shows, a drop for its files.
class Service
{
public:
    struct Callbacks
    {
        std::function<bool (const std::string& path, std::int64_t& size, std::int64_t& modified)> stat; // false: no such file
        // the first `seconds` of the file as mono SampleData (with its ACID beats) and the whole file's length
        std::function<bool (const std::string& path, double seconds, SampleData& out, double& totalSeconds)> decode;
        std::function<void (const std::string& path)> analysed; // (the worker thread) a result is in the cache
        std::function<bool()> shouldPause;                      // LYNX records or renders
        bool throttle = true;                                   // rest as long as each file took (tests: off)
    };
    explicit Service (Callbacks callbacks);
    ~Service(); // stops between two files, joins

    Service (const Service&) = delete;
    Service& operator= (const Service&) = delete;

    // Files to analyse (those not in the cache yet), ahead of the ones asked for before. Returns how many were queued.
    std::size_t request (const std::vector<std::string>& paths);
    // The stamp of `path` (false: no such file)
    bool stampOf (const std::string& path, Stamp& out) const;
    // The cached result of `path` when its stamp still matches (a cheap stat; any thread)
    std::optional<Result> find (const std::string& path) const;
    std::size_t queued() const;
    std::size_t done() const; // analysed in this session
    bool waitIdle (int timeoutMs); // tests, --snapshot

    static constexpr std::size_t maxQueue = 4096;

private:
    void run();

    Callbacks callbacks;
    mutable std::mutex lock;
    std::condition_variable wake, idleWake;
    std::deque<std::string> queue;
    bool stopping = false, working = false;
    std::size_t analysedCount = 0;
    std::thread worker;
};
} // namespace lynx::loopanalysis
