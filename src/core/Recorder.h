#pragma once

// Recording of notes played live (MIDI or PC keyboard) into the current pattern.
// Runs on the UI thread; events arrive with the absolute transport position.

#include <cstdint>
#include <map>
#include <vector>

#include "core/ScoreLog.h"
#include "core/Song.h"

namespace lynx
{
struct RecordedEvent
{
    double tick = 0.0; // absolute transport position (may go past the end of the loop)
    int channel = 0;
    int key = 60;
    float velocity = 0.8f;
    bool on = true;
};

// Where an absolute transport position falls within the current pattern: in pattern mode,
// the position within the loop; in song mode, within the current pattern's clip that is
// playing there (or, without a clip, as if it were the pattern loop).
std::int64_t patternPositionForTick (const Song& song, double tick, std::int64_t loopLength);

// Song mode, a take inside pattern clip `c` at song `tick`: its place in the pattern, along the clip (FL: the pattern
// grows, round 168k), also when `c` loops its pattern with stretchedClipsLoop() on (issue #126: a take is not folded).
// clipLoopLength = the pattern's length when `c` loops it that way, else 0.
std::int64_t clipPatternTick (const Song& song, const Clip& c, std::int64_t tick);
std::int64_t clipLoopLength (const Song& song, const Clip& c);

// --- recording options (docs/WORKFLOW_FL.md P2: recording set) ---------------------------------

enum class RecordMode : std::uint8_t
{
    overdub, // recorded notes are added to what the pattern has
    replace  // a channel's first recorded note of a take clears that channel's notes in the pattern
};

// Commits finished notes of a take into `pattern`: starts quantised to the nearest multiple of `grid`
// (grid <= 1: as played; lengths kept). In replace mode, `clearedChannels` remembers the channels already
// cleared during this take, so later notes of the same take stay.
// What "Blend recorded notes" starts as (round 177c). A new install starts off, as FL Studio's factory form does.
// Before 0.1.25 the switch wasn't saved and every session started on, so a settings file that has no answer yet keeps it on.
[[nodiscard]] constexpr RecordMode startingRecordMode (bool settingsFileExists, bool hasSavedBlend, bool savedBlend) noexcept
{
    if (hasSavedBlend)
        return savedBlend ? RecordMode::overdub : RecordMode::replace;
    return settingsFileExists ? RecordMode::overdub : RecordMode::replace;
}

void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, std::int64_t grid, std::vector<int>& clearedChannels);

// FL's record menu "Quantize" section (REA round 139, FLEngine FUN_012ca7b0; settings QuantizeNoteStartTime,
// QuantizeNoteEndTime 0/1/2, RecToSS):
//   starts         "Note start time": starts to the nearest multiple of `grid`;
//   end            what happens to the note's end: keepEnd (neither item: the end stays where it was played),
//                  quantize ("Note end time": the end to the nearest grid line, at least one grid long),
//                  keepLength ("Leave note duration": the played length after the moved start);
//   stepSequencer  "Record to step sequencer": starts to the step grid and every note a step note (one step long;
//                  the other items do not apply).
enum class NoteEnd : std::uint8_t { keepEnd, quantize, keepLength };
struct TakeQuantize
{
    std::int64_t grid = 0; // ticks (<= 1: no grid)
    bool starts = true;
    NoteEnd end = NoteEnd::keepLength;
    bool stepSequencer = false;
};
void quantizeTake (std::vector<Note>& notes, const TakeQuantize& q);
void commitTake (Pattern& pattern, std::vector<Note> notes, RecordMode mode, const TakeQuantize& q, std::vector<int>& clearedChannels);

// FL's record menu "Disarm on stop" (round 166e, measured in FL 2026): when playback stops after playing with the
// record switch on, the switch goes off. update() is called with the transport's state; true = switch it off now.
struct RecordOffOnStop
{
    bool recordedWhilePlaying = false;
    bool update (bool playing, bool recording, bool enabled) noexcept
    {
        if (! recording)
        {
            recordedWhilePlaying = false;
            return false;
        }
        if (playing)
        {
            recordedWhilePlaying = true;
            return false;
        }
        const auto stopped = recordedWhilePlaying;
        recordedWhilePlaying = false;
        return stopped && enabled;
    }
};

// FL's "Enable recording markers" (REA round 139, setting EnablePunchInOutMarkers): the Playlist's "Start recording"
// markers open a range and the next "Stop recording" closes it (no stop: to the end; a stop with no start before it:
// from the song start). No such markers = no ranges (everything is recorded).
struct PunchRange
{
    std::int64_t in = 0, out = 0; // [in, out) ticks
};
std::vector<PunchRange> punchRanges (const std::vector<Marker>& markers);
bool inPunchRanges (const std::vector<PunchRange>& ranges, double tick) noexcept;
// The parts of a recorded audio clip [start, start + length) that fall inside the ranges, as (start, length, offset)
// with `offset` the source offset of each part (the clip's own offset added).
struct ClipPart
{
    std::int64_t start = 0, length = 0, offset = 0;
};
std::vector<ClipPart> punchParts (const std::vector<PunchRange>& ranges, std::int64_t start, std::int64_t length, std::int64_t offset);

// Song mode: makes sure a clip of `pattern` plays at `tick`. If none does, one is placed at the start of
// that bar, on the first track with nothing from there on (it grows while recording). Returns true when a clip was added.
bool placePatternClipAt (Song& song, int pattern, std::int64_t tick);
// The Playlist track such a clip starting at `start` goes to (round 177d: the live display shows the take there before
// the clip is placed).
int patternClipTrack (const Song& song, std::int64_t start);

// Length of a count-in of `bars` bars at the song position `tick` (bars follow the time signature there).
std::int64_t countInLength (const Song& song, int bars, std::int64_t tick = 0);

// --- automation recording (REA round 143) ---------------------------------------------------------------
// FL's recording filter "Automation" (FLEngine RecMenu: Recfilter1Menu, bit 1 of the setting RecordingFilter2; bits 2
// notes, 4 audio, 8 clips): while recording, the parameters that are moved are written as automation. Song mode: into
// the automation clip of that parameter playing there (a new automation clip when none plays); pattern mode: into the
// current pattern's events (the event editor's lanes). The record menu's Quantize › "Automations"
// (RecQuantizeAutomationsMenu, setting QuantizeAutomations; FUN_012ca680) puts the points on the snap.
struct RecordedValue
{
    double tick = 0.0; // absolute transport position, as RecordedEvent
    float value = 0.0f; // 0..1
};

// The song position of a recorded transport position (song mode loops over the loop region or the whole song).
// `linear`: the song played on past its end (a take recorded in Song mode without a loop region): no wrap.
std::int64_t songTickForRecorded (const Song& song, double tick, bool linear = false);

// One parameter's part of a take. `continuing` = the points follow the previous batch of the same gesture (what lies
// between them is replaced too). Returns the automation written (or -1); `automation` is a hint: the automation this
// take created for the target (its clip grows with the take).
struct AutomationWrite
{
    AutomationTarget target;
    std::string name;           // for a new automation
    int automation = -1;        // in/out: the automation this take writes (song mode)
    std::int64_t lastLocal = -1; // in/out: the last point written (the automation's or the pattern's ticks)
    bool createdByTake = false; // in/out
};
// Song mode.
void commitAutomationPoints (Song& song, AutomationWrite& w, std::vector<RecordedValue> points, std::int64_t grid, bool linear = false);
// While recording goes on (song mode): the take's own clip reaches the playhead at `tick` (it keeps growing, holding
// the last value, until recording stops — FL); `finished`: the take ended, the clip ends at the end of that bar.
void growTakeClip (Song& song, const AutomationWrite& w, std::int64_t tick, bool finished);
// Pattern mode: into `pattern`'s event lane for the target (one event per tick; held until the next one).
void commitEventPoints (Song& song, int pattern, AutomationWrite& w, std::vector<RecordedValue> points, std::int64_t loopLength,
                        std::int64_t grid);
// Removes the points between two of the same value (no change of the curve) in [from, to].
void thinAutomation (std::vector<AutomationPoint>& points, std::int64_t from, std::int64_t to);

// --- Song-mode note takes (round 168k, the YouTube review: "it just maxes out at two bars") ----------------------
// FL (REA_rodada165z-b fh2–fh5, round 168k [EXEC]): a take recorded in Song mode goes into the current pattern at the
// song position it was played at — nothing folds into the pattern's first bars. Where no clip of the pattern plays, the
// pattern's start lies at the take's origin: the bar of the take's first note (its clip is placed there and grows with
// the pattern to the last recorded bar).
//
// The origin of a take whose first note is played at song position `tick`: inside a clip of the current pattern, the
// song position of that clip's pattern start (the take extends what the clip shows); elsewhere the start of that bar.
// `fromClip` (optional) tells which one it was (a clip is placed for the take only when it was not).
std::int64_t takeOriginFor (const Song& song, std::int64_t tick, bool* fromClip = nullptr);
// Where a note played at song position `tick` goes in the current pattern: inside a clip of the pattern, that clip's
// position; elsewhere `tick − origin` (0 at least).
std::int64_t songNotePosition (const Song& song, std::int64_t tick, std::int64_t origin);
// After a batch of a Song-mode take was committed (`notes`: its notes, pattern positions): a pattern with a set length
// (Channel Rack › Length) grows by whole bars to the notes recorded past its end (an automatic-length pattern grows by
// itself), and when no clip of the pattern plays at song position `reach` (the latest note placed from the origin, -1:
// none) the clip at the take's origin grows to that bar. Returns true when something changed.
bool growForSongTake (Song& song, int pattern, std::int64_t origin, std::int64_t reach, const std::vector<Note>& notes);

// --- live note timing and the count-in catch (round 170d, the Busy Works video: "the first chord on the downbeat after
// the pre-count is not recorded", FL drops it too; coord/recording-research-0.1.24.md) -----------------------------------
// The audio clock as the last audio callback left it: `tick` = the musical position the next block starts at (during a
// count-in, before the take's start: the start minus the count-in left, as the metronome counts it), `timeMs` = when it
// was stamped (juce::Time::getMillisecondCounterHiRes, the clock JUCE stamps MIDI input with), `ticksPerMs` at the
// tempo then, `running` = playing or counting in.
struct LiveClock
{
    double tick = 0.0, timeMs = 0.0, ticksPerMs = 0.0;
    bool running = false;
};
// The musical position of an event that happened at `eventMs` (the MIDI device's own time stamp, not when the UI saw
// it), moved back by `compensationMs`: what is heard comes out of the output that much after the clock rendered it
// (output buffer + the driver's output latency; Pro Tools / Cubase / REAPER do the same). A stopped clock: its tick.
double liveTickAt (const LiveClock& clock, double eventMs, double compensationMs) noexcept;

// Notes played during the count-in of a take that starts at `start` (Logic keeps them at their real time, Cubase's
// "MIDI Record Catch Range" catches the ones just before the start): a note-on at most `window` ticks before the start
// goes onto the start (its played length kept when it is released before the start); an earlier one keeps its real
// song position in Song mode (dropped before the song start), and in Pattern mode its place in the pattern loop, as if
// the loop had played before (`wrap` = a whole number of pattern loops at least as long as the count-in; a note held
// over the start then ends at the loop's end). start < 0: no count-in, nothing changes.
struct CountInCatch
{
    std::int64_t start = -1;
    std::int64_t window = 0;
    bool songMode = true;
    std::int64_t wrap = 0;
    struct Moved
    {
        int channel = 0, key = 0;
        double shift = 0.0; // how far its note-on was moved
        bool wrapped = false;
    };
    std::vector<Moved> moved; // the notes held whose note-on was moved
};
// The pattern-loop wrap for a count-in of `countIn` ticks over a pattern loop of `loopLength` ticks.
std::int64_t countInWrap (std::int64_t countIn, std::int64_t loopLength) noexcept;
// Applies the catch to an event of the take (its tick in place); false = the event is dropped.
bool catchEarlyNote (RecordedEvent& e, CountInCatch& c);

class NoteRecorder;
// One batch of a take's finished notes into the song, as AudioEngine::updateRecording commits it (the caller wraps it in
// one undoable edit, which also lets the pattern's clips follow its length): quantised and committed into the current
// pattern; in Song mode the take's clip is placed at its origin with the first batch (`placeClip`: the record option
// "place a clip") and grows with the take (growForSongTake).
void commitNoteBatch (Song& song, const NoteRecorder& recorder, std::vector<Note> notes, RecordMode mode, const TakeQuantize& q,
                      std::vector<int>& clearedChannels, bool placeClip, bool firstBatch);

class NoteRecorder
{
public:
    // Starts a take. `loopLength` = pattern loop length at the start of the take
    // (does not change while the recorded notes grow the pattern).
    void begin (std::int64_t loopLength);
    bool isActive() const noexcept { return active; }

    // Song mode: the song plays on past its end while the take records (no wrap of the positions; round 168k).
    void setLinear (bool on) noexcept { linear = on; }
    // Pattern mode, Loop record off (round 170e5): the notes go at their distance from this transport tick (the
    // pattern played once from there), not folded into the loop; < 0: folded (the default)
    void setPatternOrigin (double tick) noexcept { patternOrigin = tick; }
    double getPatternOrigin() const noexcept { return patternOrigin; }
    // Song mode: the take's origin (see takeOriginFor), -1 until its first note; `originFromClip` = it is a clip's.
    std::int64_t songOrigin() const noexcept { return origin; }
    bool originFromClip() const noexcept { return fromClip; }
    // Song mode: the song position of the latest note start placed from the origin (outside every clip), -1: none.
    std::int64_t originReach() const noexcept { return reach; }

    // Delivers an event; when it's the "off" of a held note, the finished note goes to finished().
    void add (const Song& song, const RecordedEvent& e);

    // Closes the notes still held at position `tick` (playback stopped) and ends the take.
    void end (const Song& song, double tick);

    // Notes completed since the last call.
    std::vector<Note> takeFinished();

    // Round 177d (live display while recording; MusicTech's FL 2025 review: "recorded MIDI is not shown while
    // recording"): the notes still held, as they would be committed if released at transport position `nowTick` (pattern
    // positions, not quantised), with the take's origin and reach including them (Song mode; -1: none yet). Const: the
    // take is not changed.
    struct Preview
    {
        std::vector<Note> notes;
        std::int64_t origin = -1, reach = -1;
        bool fromClip = false;
    };
    Preview preview (const Song& song, double nowTick) const;
    bool hasHeldNotes() const noexcept { return ! held.empty(); }

private:
    struct Held
    {
        double tick;
        float velocity;
    };

    void finish (const Song& song, int channel, int key, const Held& held, double endTick);
    Note makeNote (const Song& song, int channel, int key, const Held& h, double endTick, std::int64_t& takeOrigin, bool& takeFromClip,
                   std::int64_t& takeReach) const;

    std::int64_t songTick (const Song& song, double tick) const;

    bool active = false;
    bool linear = false;
    double patternOrigin = -1.0;
    std::int64_t loop = 0;
    std::int64_t origin = -1;
    std::int64_t reach = -1;
    bool fromClip = false;
    std::map<std::pair<int, int>, Held> held; // (channel, key)
    std::vector<Note> finished;
};

// --- Capture MIDI (round 170d, LYNX's own; Ableton "Capture MIDI", Logic "Capture as Recording", Cubase "Retrospective
// Record") ----------------------------------------------------------------------------------------------------------------
// The score log keeps every note played (scorelog::Entry::startTick = the transport position when it was played while
// playing, -1 when stopped; `recorded` = it went into a take). captureMidi() puts the last phrase played and not
// recorded into the current pattern: the newest notes back to a silence longer than `gapMs` or a change of transport
// state, only the ones newer than `afterMs`. Played while playing: where they were played, as a take would have put them
// (NoteRecorder + commitNoteBatch); played while stopped: from the playhead's bar (Song mode, a clip placed there) or the
// pattern start (Pattern mode) at the project tempo, the first note on that start, the timing kept. The record options'
// quantize and overdub / replace apply. Notes still held end at `nowMs`.
struct CaptureOptions
{
    double afterMs = -1.0;
    double nowMs = 0.0;
    double gapMs = 4000.0;
    std::int64_t playhead = 0; // the song position of the playhead (stopped phrases, Song mode)
    RecordMode mode = RecordMode::overdub;
    TakeQuantize quantize;
    bool placeClip = true;
};
struct CaptureResult
{
    int notes = 0;
    double newestMs = -1.0; // the newest note captured (the next capture takes only newer ones)
    bool played = false;    // they were played while the transport ran
};
// The phrase captureMidi() would take (oldest first; empty: nothing to capture).
std::vector<scorelog::Entry> capturePhrase (const Song& song, const std::vector<scorelog::Entry>& log, const CaptureOptions& options);
CaptureResult captureMidi (Song& song, const std::vector<scorelog::Entry>& log, const CaptureOptions& options);
} // namespace lynx
