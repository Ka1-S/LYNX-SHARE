#pragma once

// FL Studio's Edison as a LYNX mixer effect (round 190cd; facts in docs/analise/REA_rodada190cd.md, clean-room: the
// behaviour in our own words, no Image-Line code or data in this repository).
//
// Edison is an audio editor that sits in an effect slot. Its render passes the slot's input through untouched (or
// silences it with "Mute input"), records that input into a take while Record is on, and adds its own playback to
// the output. The audio it holds belongs to the project.
//
// Three parts:
//  - Gate: the record-mode state machine (Now / On input / Input / On play), block by block. Pure, tested alone.
//  - Processor: the Effect. The audio thread writes the take into a lock-free ring and plays a clip or, slaved to the
//    host, the content at the song time; the message thread drains the ring and hands it clips.
//  - Session: the slot's document (audio + markers/regions + loop) and the take being recorded, on the message
//    thread. One per slot instance (EffectSlot::instance), found again when the effect is rebuilt, shared by the
//    editor window and by every Processor built for that slot (the device's and an offline render's).
//
// Where the audio is kept: a WAV (32-bit float, markers and loop inside) in the project's recording folder, its path in
// EffectSlot::samplePath: the rule of every recording of a project (packaging, relocation, missing-file search and the
// unused-recordings prompt follow it). A slot imported from an .flp keeps FL's state in EffectSlot::state and reads the
// WAV image inside it until its audio changes.
//
// Parameters (EffectKind::edison), all 0..1 in EffectSlot::params: see Param.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/AudioEdit.h"
#include "core/Effects.h"

namespace lynx
{
struct SampleData;
}

namespace lynx::edisonfx
{
enum Param
{
    recordMode = 0,     // Mode / 3
    append,             // on: a new take goes after the audio already there; off: it replaces it
    muteInput,          // on: the slot's input is not passed on
    slave,              // "Slave playback to host": while the song plays, the content plays at the song time
    loopPlay,           // the editor's Loop switch
    maxTime,            // index / 6 into maxMinutes
    noiseThreshold,     // linear peak the input must exceed (On input / Input)
    ignoreOwnSelection, // slave: the content starts at its first frame, not at the selection
    paramCount
};

enum class Mode : std::uint8_t
{
    now,     // "Start recording immediately"
    onInput, // "Start recording when input is detected"
    input,   // "Record detected input": pauses when the input stops
    onPlay   // "Record when host is playing"
};

constexpr int modeCount = 4;
constexpr int maxMinutes[] = { 0, 1, 3, 5, 10, 30, 60 }; // 0 = forever
constexpr int maxTimeCount = 7;
constexpr float defaultNoiseThreshold = 0.02f;
constexpr double holdSeconds = 1.0;    // Input: how long the recording goes on after the last block above the threshold
constexpr double preRollSeconds = 0.1; // On input / Input: what is kept from before the input was heard

const EffectInfo& info();
std::string paramText (int param, float value);
const char* modeName (Mode m) noexcept;       // "Now", "On input", "Input", "On play"
const char* modeHint (Mode m) noexcept;       // FL's own descriptions of the four
Mode modeOf (const EffectParams& params) noexcept;
float modeValue (Mode m) noexcept;
// The settings FL gives the Edison that a mixer track's "Open audio editor and record" (Shift+E) loads: On play, 5 min,
// Append off, Slave playback on (live FL check 190cq, shot edison190cd-05b; it is armed as well). A plain "Open audio
// editor" and a fresh slot keep info()'s defaults: On input, 30 min, Append on, Slave off.
void setOpenAndRecord (EffectParams& params) noexcept;
int maxTimeIndexOf (const EffectParams& params) noexcept;
// Frames a take may hold at `sampleRate` (0 = no limit).
std::int64_t maxFramesOf (const EffectParams& params, double sampleRate) noexcept;

// --- the record-mode state machine ---------------------------------------------------------------------------------
enum class Phase : std::uint8_t
{
    off,      // Record is off
    waiting,  // armed: waiting for the input (On input / Input) or for the song to play (On play)
    recording // blocks are written to the take
};

class Gate
{
public:
    void prepare (double sampleRate) noexcept;
    void arm (Mode mode) noexcept;
    void disarm() noexcept;

    struct Decision
    {
        bool write = false;        // this block goes to the take
        bool preRoll = false;      // …after the pre-roll kept from the blocks before it
        bool jumpMarker = false;   // a "Song jump" marker belongs at the start of this block
    };
    // One block of the slot's input: its peak, its length, whether the song plays and whether its position jumped.
    Decision block (float peak, int frames, bool hostPlaying, bool hostJumped, float threshold) noexcept;

    Phase phase() const noexcept { return state; }
    Mode mode() const noexcept { return current; }
    int holdFrames() const noexcept { return holdLength; }
    int preRollFrames() const noexcept { return preRollLength; }

private:
    Mode current = Mode::onInput;
    Phase state = Phase::off;
    int hold = 0, holdLength = 44100, preRollLength = 4410;
    bool jumpPending = false;
};

// --- the effect ------------------------------------------------------------------------------------------------------
class Processor final : public Effect
{
public:
    Processor();
    ~Processor() override;

    void prepare (double sampleRate) noexcept override;
    void reset() noexcept override {}          // (its take and its playback are not a tail to clear)
    void transportReset() noexcept override {} // (nor does the transport stop a recording)
    void process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept override;
    void setSongPosition (double ticks) noexcept override { songTicks = ticks; }
    void setPlaying (bool playing) noexcept override { hostPlaying = playing; }
    // (the slot's state is only what an .flp brought; the audio is in the session: no reason to build a new effect)
    bool acceptState (const std::vector<std::uint8_t>&) noexcept override { return true; }

    double sampleRate() const noexcept { return rate.load (std::memory_order_relaxed); }

    // --- message thread: recording ---
    void setRecording (bool on) noexcept;
    bool isRecording() const noexcept { return wantRecord.load (std::memory_order_relaxed); }
    Phase phase() const noexcept { return (Phase) shownPhase.load (std::memory_order_relaxed); }
    // The take reached the maximum recording time and Record went off by itself.
    bool reachedLimit() const noexcept { return limitHit.load (std::memory_order_relaxed); }
    // Appends what was recorded since the last call; `jumps`: the take frames where the song position jumped.
    // `takeFrames`: how many frames the take held before this call. Returns the frames appended.
    int drain (std::vector<float>& l, std::vector<float>& r, std::vector<std::int64_t>& jumps, std::int64_t takeFrames);
    std::int64_t droppedFrames() const noexcept { return dropped.load (std::memory_order_relaxed); }
    float inputPeak() const noexcept { return meter.exchange (0.0f, std::memory_order_relaxed); }

    // --- message thread: playback ---
    // Plays `clip` into the slot's output (nullptr stops). `gain` linear.
    void play (std::shared_ptr<const audioedit::Audio> clip, bool loop, float gain = 1.0f);
    // The frame of the clip being played, or −1.
    double playPosition() const noexcept { return shownPosition.load (std::memory_order_relaxed); }
    // What slave mode plays: the whole content, and the content frame that sounds at song time 0.
    void setContent (std::shared_ptr<const audioedit::Audio> content, std::int64_t offset);

    static constexpr std::size_t ringCapacity = std::size_t (1) << 19; // ~11 s at 48 kHz between two drains

private:
    struct Source
    {
        std::shared_ptr<const audioedit::Audio> audio;
        bool loop = false;
        float gain = 1.0f;
        std::int64_t offset = 0;
    };
    bool tryLock() noexcept { return ! lock.test_and_set (std::memory_order_acquire); }
    void unlock() noexcept { lock.clear (std::memory_order_release); }
    void write (const float* l, const float* r, int n) noexcept;
    void writePreRoll() noexcept;
    void keepPreRoll (const float* l, const float* r, int n) noexcept;
    void render (const Source& s, double& position, double step, float* l, float* r, int n, bool stopAtEnd) noexcept;

    std::atomic<double> rate { 44100.0 };
    double songTicks = 0.0, expectedTicks = 0.0;
    bool hostPlaying = false, wasPlaying = false, slaveEngaged = false;

    // recording (audio thread, except the atomics)
    Gate gate;
    std::atomic<bool> wantRecord { false }, limitHit { false };
    bool recording = false;
    std::atomic<int> shownPhase { 0 };
    std::int64_t written = 0;
    std::vector<float> ringL, ringR;
    std::atomic<std::uint64_t> writeIndex { 0 }, readIndex { 0 };
    std::atomic<std::int64_t> dropped { 0 };
    std::vector<float> preL, preR; // circular: the last pre-roll of what was not written
    int prePos = 0, preCount = 0;
    static constexpr int jumpSlots = 64;
    std::int64_t jumpAt[jumpSlots] {};
    std::atomic<std::uint32_t> jumpWrite { 0 }, jumpRead { 0 };
    mutable std::atomic<float> meter { 0.0f };

    // playback: the message thread sets `pending…` under the lock, the audio thread only tries it
    std::atomic_flag lock = ATOMIC_FLAG_INIT;
    Source pendingClip, clip, retiredClip, pendingContent, content, retiredContent;
    bool hasPendingClip = false, hasPendingContent = false;
    double clipPosition = 0.0, slavePosition = 0.0;
    std::atomic<double> shownPosition { -1.0 };
};

// --- the slot's document -------------------------------------------------------------------------------------------
// `base` with a take: Append on → after the audio already there; off → the take alone (the title is kept). `jumps`: take
// frames that get a "Song jump" marker (On play: where each stretch of playback starts, so frame 0 / the join too, and
// every jump of the song).
audioedit::Document withTake (const audioedit::Document& base, const audioedit::Audio& take, const std::vector<std::int64_t>& jumps,
                              bool appendTake);

// Message thread only.
class Session
{
public:
    explicit Session (std::uint64_t instanceId);

    std::uint64_t id() const noexcept { return instance; }
    const audioedit::Document& document() const noexcept { return *doc; }
    std::shared_ptr<const audioedit::Document> shared() const noexcept { return doc; }
    // Counts every change of the document (the editor window follows it).
    std::uint64_t revision() const noexcept { return rev; }
    // The file the document was read from or last written to ("" = none), and whether it has changed since.
    const std::string& path() const noexcept { return file; }
    bool dirty() const noexcept { return changed; }

    // The editor's edits. `selectionStart`: where slave playback starts (−1: at the first frame).
    void setDocument (audioedit::Document d);
    void setSlaveOffset (std::int64_t selectionStart);
    void load (const std::string& path, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback);
    void loadFlState (const std::vector<std::uint8_t>& state);
    // Writes the document to `path` (a WAV) and remembers it; an empty document forgets its file instead.
    bool save (const std::string& path);
    void attach (const std::shared_ptr<Processor>& p);

    // --- recording ---
    bool start (const std::shared_ptr<Processor>& recorder, bool appendTake);
    // Moves what was recorded into the take; ends the recording when the recorder stopped by itself or is gone.
    // Returns whether a recording is still going on.
    bool pump();
    void stop(); // the take joins the document (nothing recorded: the document is left alone)
    bool recording() const noexcept { return active; }
    Phase phase() const;
    std::int64_t takeFrames() const noexcept { return (std::int64_t) takeL.size(); }
    std::int64_t droppedFrames() const noexcept { return lost; }
    // The document as it will be when the recording stops now.
    audioedit::Document preview() const;
    // Where the take starts in preview().
    std::int64_t takeStart() const noexcept;

private:
    void changedDocument();
    audioedit::Audio take() const;

    std::uint64_t instance;
    std::shared_ptr<const audioedit::Document> doc;
    std::uint64_t rev = 0;
    std::string file;
    bool changed = false;
    std::int64_t slaveOffset = 0;
    std::vector<std::weak_ptr<Processor>> players;

    bool active = false, appending = true;
    std::weak_ptr<Processor> recorder;
    double takeRate = 44100.0;
    std::vector<float> takeL, takeR;
    std::vector<std::int64_t> jumps;
    std::int64_t lost = 0;
};

// The session of a slot instance: found, or made (its audio read from the slot's file, else from FL's state in it).
std::shared_ptr<Session> sessionFor (const EffectSlot& slot,
                                     const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback = {});
std::shared_ptr<Session> findSession (std::uint64_t instanceId);
// Every session alive (the pump of recordings whose window is closed, the pre-save flush).
std::vector<std::shared_ptr<Session>> sessions();
// The slot's path changed while its effect kept running (relocation, undo): an empty session reads it; one that holds
// audio keeps it (the next flush points the slot at the session's file again).
void followPath (const EffectSlot& slot, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback = {});

// The slot's effect with its session (UI thread).
std::shared_ptr<Effect> make (const EffectSlot& slot, double sampleRate,
                              const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback = {});

bool writeWavFile (const audioedit::Document& doc, const std::string& path);
bool readWavFile (const std::string& path, audioedit::Document& out);

// --- FL's saved state (slot record 0xD5) ---------------------------------------------------------------------------
struct FlState
{
    int version = 0;
    Mode mode = Mode::onInput;
    bool newProject = false; // = Append off
    bool loop = false, muteInput = false, noAutoScroll = false;
    int maxTime = 5; // (only for states too old to hold it: FL's default, 30 min)
    bool slave = false, ignoreHostSelection = false, ignoreOwnSelection = false;
    float noiseThreshold = defaultNoiseThreshold;
    std::size_t wavOffset = 0, wavSize = 0; // the RIFF/WAVE image of the sample inside the state
};
std::optional<FlState> parseFlState (const std::vector<std::uint8_t>& state);
void toParams (const FlState& s, EffectParams& params);
} // namespace lynx::edisonfx
