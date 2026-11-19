#pragma once

// FL Studio's Gross Beat as a LYNX mixer effect: the plugin's own time-travel and volume engine, rebuilt from the
// installation's DLL in docs/analise/REA_rodada53.md (clean-room: behaviour and formulas only, no Image-Line code).
//
// The effect slot carries FL's saved state (plugin record 0xD5: parameters, options and the 72 envelope slots) inside
// LYNX's own small container in EffectSlot::state:
//
//   "LXGB", u8 container version (1), u8 0, u16 0, i32 project PPQ (FL's ticks per beat), then FL's state bytes.
//
// The eight FL parameters live in EffectSlot::params (0..1, see paramFromRaw) so the LYNX UI and automation can change
// them: 0 time slot, 1 volume slot, 2 time mix, 3 volume mix, 4 attack, 5 release, 6 volume tension, 7 hold.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/Effects.h"

namespace lynx::grossbeat
{
constexpr int paramCount = 8;
constexpr int slotsPerKind = 36;

// One point of a slot envelope (FL's TCustomMEnvelope "map": Δx in beats, level 0..1, tension, curve type) plus the
// per-point coefficients the plugin derives from the tension when the state is loaded.
struct Point
{
    double dx = 0.0;
    float y = 0.0f, tension = 0.0f;
    std::uint8_t type = 0;
    std::int8_t dir = 0; // 0 none, ±1 sign of the tension, 2 = evaluated as a straight line
    float k14 = 0.0f, k18 = 0.0f, k1c = 0.0f;
};

struct Slot
{
    std::string name;
    int triggerSync = 0, positionSync = 0, hold = 0; // sync combos 0 off, 1 ¼ beat … 5 4 beats; hold > 0 repeats
    bool link = false, keyHeld = false, oneShot = false;
    std::vector<Point> points;
    bool flat = true; // every level equal to the default 1.0: the slot does nothing
};

struct State
{
    int version = 8;
    std::array<int, 9> params {};      // FL's raw parameters 0…8
    std::array<std::uint8_t, 9> options {}; // option bytes (state offsets 0x168…0x170 of the plugin object)
    std::array<Slot, 2 * slotsPerKind> slots; // 0…35 time travel, 36…71 volume
    // Options › "Trigger slots from MIDI notes" (round 190ee; Gross Beat_x64.dll: plugin field 0x731, state loader
    // 0x83f890). FL saves it as one byte after the slots in its version-9 state; every older state (all demo projects
    // and presets) reads as on. A bare new FL instance (constructor 0x83fcf0) has it off.
    bool midiTrigger = true;
};

// MIDI control of the slots (round 190ee, the editor's MIDI handler 0x848330): the 36 time slots answer the keys
// 60…95 (C5…B7), the 36 volume slots the keys 24…59 (C2…B4) — FL's fixed per-kind base keys (constructor 0x83fcf0).
constexpr int timeBaseKey = 60, volumeBaseKey = 24;
// The slot a key selects: kind 0 time / 1 volume and the slot index, or {-1, -1} for a key outside both ranges.
std::pair<int, int> slotForKey (int key) noexcept;

// What FL's editor does with MIDI notes (0x848330), without the engine: which slot each note selects, the key-held
// stack (a Key-held slot goes back to the previous slot when its key is released) and the notes that are held.
class MidiSlots
{
public:
    // a note on (velocity > 0) or off on MIDI channel `channel` (0…15); returns the (kind, slot) to select, or
    // {-1, -1} when nothing changes. `current` = the slot each kind plays now; `keyHeld(kind, slot)` = that slot's
    // Key-held switch.
    template <typename KeyHeld>
    std::pair<int, int> note (bool on, int channel, int key, const std::array<int, 2>& current, KeyHeld&& keyHeld) noexcept
    {
        const auto id = (std::size_t) (((channel & 15) << 8 | (key & 127)) & 0xfff);
        const auto [kind, slot] = slotForKey (key);
        if (on)
        {
            held[id] = true;
            if (kind < 0)
                return { -1, -1 };
            auto& st = stacks[(std::size_t) kind];
            if (keyHeld (kind, slot))
            {
                if (st.count == 0)
                    push (st, current[(std::size_t) kind]);
                if (! contains (st, slot))
                    push (st, slot);
            }
            return { kind, slot };
        }
        if (! held[id]) // a release FL never saw pressed is ignored
            return { -1, -1 };
        held[id] = false;
        if (kind < 0 || ! keyHeld (kind, slot))
            return { -1, -1 };
        auto& st = stacks[(std::size_t) kind];
        remove (st, slot);
        if (st.count == 0)
            return { -1, -1 };
        const auto back = st.items[(std::size_t) st.count - 1];
        if (st.count == 1)
            st.count = 0;
        return { kind, back };
    }
    void clear() noexcept
    {
        held.fill (false);
        stacks = {};
    }

private:
    struct Stack
    {
        std::array<int, slotsPerKind + 1> items {};
        int count = 0;
    };
    static bool contains (const Stack& s, int v) noexcept
    {
        for (int i = 0; i < s.count; ++i)
            if (s.items[(std::size_t) i] == v)
                return true;
        return false;
    }
    static void push (Stack& s, int v) noexcept
    {
        if (s.count < (int) s.items.size())
            s.items[(std::size_t) s.count++] = v;
    }
    static void remove (Stack& s, int v) noexcept
    {
        for (int i = 0; i < s.count; ++i)
            if (s.items[(std::size_t) i] == v)
            {
                for (int j = i + 1; j < s.count; ++j)
                    s.items[(std::size_t) j - 1] = s.items[(std::size_t) j];
                --s.count;
                return;
            }
    }
    std::array<bool, 4096> held {};
    std::array<Stack, 2> stacks {};
};

// FL's state (0xD5) → parsed slots; nullopt when it is not a Gross Beat state this engine reads.
std::optional<State> parseState (const std::uint8_t* data, std::size_t size);

// The state written back in FL's own layout (version 8, REA_rodada14 §2.1), so an edited slot travels in the LXGB
// container and reads again with parseState (and in FL).
std::vector<std::uint8_t> writeState (const State& state);

// A new FL instance before its default preset loads (constructor FUN_0083fcf0, REA_rodada168m §2): every slot unnamed,
// trigger and position sync on 4 beats, a flat bar of two hold points at level 1; parameters and options at FL's
// defaults (high quality and declick on).
State emptyState();

// LYNX's own factory bank (round 168m): empty slot 1 on both sides, then time and volume slots made for LYNX from the
// behaviour FL documents (stutters, half speed, tape stop, reverse, gates, pumps, fades); nothing copied from FL's files.
State factoryState();
// factoryState() packed in the LXGB container (PPQ 96): what a new Gross Beat starts with.
std::vector<std::uint8_t> factoryContainer();

// The state of a container to show and edit: the saved one, else (empty or unreadable) the factory bank.
State editableState (const std::vector<std::uint8_t>& container, int* ppq = nullptr);
// `state` written back into a container (keeps the container's PPQ, 96 for a new one).
std::vector<std::uint8_t> withState (const std::vector<std::uint8_t>& container, const State& state);

// ---- point editing (FL's MEnvEditor on a slot: x in beats 0…4, level 0…1) ----------------------------------------
struct EditPoint
{
    double x = 0.0;
    float y = 1.0f, tension = 0.0f;
    std::uint8_t type = 2; // FL's curve type of the segment that ends at the point (Gross Beat adds hold points)
};
constexpr double barBeats = 4.0;
constexpr int maxEditPoints = 512; // the running engine takes edits of up to this many points per slot in place

std::vector<EditPoint> editPoints (const Slot& slot);
// replaces the slot's points: x kept in order inside 0…4, the first at 0 and the last at 4; coefficients recomputed
void setPoints (Slot& slot, const std::vector<EditPoint>& points);
// a point at x (after the points at or before x); its index
int addPoint (std::vector<EditPoint>& points, double x, float y, std::uint8_t type = 2, float tension = 0.0f);
// FL keeps the first point and at least two points; the last one is the end of the bar
bool removePoint (std::vector<EditPoint>& points, int index);
// moves a point between its neighbours (the first and the last only up and down)
void movePoint (std::vector<EditPoint>& points, int index, double x, float y);
// round 190mf: FL's "Slide remaining points": the point moves (not before the one before it) and every later point but the
// bar's end moves by the same time, kept inside the bar and in order
void slidePoint (std::vector<EditPoint>& points, int index, double x, float y);
// round 190mf: FL's Step editing: a flat hold at `y` from x0 to x1 (the points strictly inside are replaced): a hold point
// at x0 and, unless a point is there, one at x1 back at the curve's old level there
void stepDraw (std::vector<EditPoint>& points, double x0, double x1, float y);
// the slot's level at `beat` (0…4) as the engine plays it
float levelAt (const Slot& slot, double beat);

// --- round 190mk (DP-7, capture 190mk A1): the rest of FL's mapping options menu -------------------------------------
// FL's "Envelope state file (*.fnv)" (State › Open / Save state file..., the Presets in FL's Data\System\Articulator\Maps)
// is the envelope's own TCustomMEnvelope "map" stream, as in the plugin's state: kind 3, version, point count, the points
// (Δx, level, tension, curve type), five integers. Read: the Δx are stretched to the bar as the plugin does (the
// factory maps span 1.0); nullopt when it is not such a stream. Write: version 3, the slot's points as the state has them.
std::optional<std::vector<EditPoint>> readEnvelopeFile (const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> writeEnvelopeFile (const Slot& slot);
// State › "Prepare for smooth editing": one smooth point per beat at the level the envelope has there (the bar's end
// takes the start's level: it loops), every segment an S-curve (FL: 5 points, the −0.75 step between beats gone)
std::vector<EditPoint> prepareForSmoothEditing (const Slot& slot);
// Points selection › Cut / Copy / Paste (insert) / Paste (replace) / Duplicate (capture 190mk A1.4). The clipboard keeps
// the selected points from the first selected one (x relative to it). Insert: the copy goes right after the selection
// (at its last point's x; none selected: at `at`) and every later point moves right by the copy's length. Replace: the
// selected points go, the copy starts where they started, later points move by the length difference. Duplicate: an
// insert of the selection itself; the copy becomes the selection. Cut: the selected points go and the later points close
// the gap (they move left by the time from the point before the selection to the point after it). LYNX keeps the bar's
// end point at the bar's end (its level kept; a point pushed past the end is dropped, the end then takes the level the
// envelope had there) — FL lets an envelope run past or stop short of the bar while it is edited.
struct PointClip
{
    std::vector<EditPoint> points; // x from 0
    double length() const noexcept { return points.empty() ? 0.0 : points.back().x; }
};
PointClip copyPoints (const std::vector<EditPoint>& points, const std::vector<int>& selected);
// the result's points and the indices of the points to select afterwards
std::pair<std::vector<EditPoint>, std::vector<int>> pasteInsert (const std::vector<EditPoint>& points, const std::vector<int>& selected,
                                                                 const PointClip& clip, double at);
std::pair<std::vector<EditPoint>, std::vector<int>> pasteReplace (const std::vector<EditPoint>& points, const std::vector<int>& selected,
                                                                  const PointClip& clip);
std::pair<std::vector<EditPoint>, std::vector<int>> duplicatePoints (const std::vector<EditPoint>& points, const std::vector<int>& selected);
std::vector<EditPoint> cutPoints (const std::vector<EditPoint>& points, const std::vector<int>& selected);
// Tools › "Analyze audio file...": the whole file's level over the bar (loud = 1, silence = 0) as single-curve points,
// dense where it changes fast (FL: ~100 points for an 8 s drum loop). `peaks` = the file's peak level per equal slice.
std::vector<EditPoint> envelopeFromLevels (const std::vector<float>& peaks, int maxPoints = 110);
// n levels over the bar (for drawing)
std::vector<float> renderSlot (const Slot& slot, int n);
// FL's snap selector (off, 1/16, 1/12, 1/8, 1/6, 1/4, 1/3 beat — table 0x8b6ae8): the step in beats (0 = off)
double snapStep (int selector) noexcept;

// Container helpers
std::vector<std::uint8_t> pack (const std::vector<std::uint8_t>& flState, int ppq);
bool unpack (const std::vector<std::uint8_t>& container, std::vector<std::uint8_t>& flState, int& ppq);
void setPpq (std::vector<std::uint8_t>& container, int ppq);

// FL raw parameter value ↔ LYNX's 0..1 parameter
float paramFromRaw (int index, int raw) noexcept;
int rawFromParam (int index, float value) noexcept;

// The exact effect slot for an FL state (nullopt if the state cannot be read)
std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState, int ppq = 96);
// Names of the 36 time (volume = false) or volume slots of a container, for the UI
std::vector<std::string> slotNames (const std::vector<std::uint8_t>& container, bool volume);

// The engine, also usable without the Effect wrapper (tools and tests). Host model: FL's mixer — the plugin is "playing"
// between start() and stop(), the song position is given in FL ticks (PPQ of the state) at every block start.
class Engine
{
public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    bool load (const State& state, int ppq);
    // an edited state (same PPQ) taken while running: slots, options and per-slot settings change in place, the
    // buffer and the playing slots' positions are kept. Audio thread, no allocation: false when a slot has more points
    // than maxEditPoints (rebuild the engine instead).
    bool update (const State& state) noexcept;
    void prepare (int sampleRate, int maxBlock);
    void setTempo (float bpm);
    void setParam (int index, int raw); // FL's ProcessParam
    void start (double ticks);          // FL's "playing" dispatcher (enable + re-sync at the position)
    void stop();
    void jump (double ticks);           // song position changed while playing (re-sync)
    bool playing() const noexcept;
    // in place on the two channels; `ticks` = song position at the block start
    void process (float* left, float* right, int n, double ticks) noexcept;

    // Round 190ih (FL manual, Gross Beat › Envelope Mapping Panel: "the Playback locator ... visible only when the host
    // is playing"; Time Slots and Volume Slots: "Green rectangle = Queued for playback. Green filled = Playing"): where
    // each kind's envelope is now, 0 … 1 of its 4 beats, the slot it plays and the slot queued (-1 = none). Read on the
    // thread that calls process(), after it; no allocation.
    struct Live
    {
        bool playing = false;
        std::array<double, 2> position { 0.0, 0.0 }; // [0] time, [1] volume
        std::array<int, 2> slot { -1, -1 }, queued { -1, -1 };
    };
    Live live() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& container);

// Round 190gk: in FL a MIDI note selects a slot exactly as a click in the slot grids does (REA_rodada190ee §1, the
// editor's handler 0x848330): it sets the Time slot / Volume slot parameter, so the grid shows the picked slot and the
// project keeps it. The running effect publishes each pick here (audio thread: slots first, then `serial`); the
// engine's timer writes it into the slot's parameter (AudioEngine::pumpGrossBeatPicks), and the effect knows that
// parameter change as its own pick coming back.
struct MidiPicks
{
    std::atomic<int> timeSlot { -1 }, volumeSlot { -1 }; // the slot each kind plays after the last pick (-1 = none yet)
    std::atomic<std::uint32_t> serial { 0 };             // bumped after every pick
    std::atomic<std::uint32_t> taken { 0 };              // the serial the message thread last wrote into the parameters
    int slotOf (int kind) const noexcept { return (kind == 0 ? timeSlot : volumeSlot).load (std::memory_order_relaxed); }
};
MidiPicks* midiPicksOf (Effect* effect); // nullptr: not a running Gross Beat

// The parameter changes that bring a slot's Time slot / Volume slot parameters (`params`, 0..1) to the picked slots,
// as (parameter index, value) pairs; empty when they already match (message thread).
std::vector<std::pair<int, float>> paramsForPicks (const MidiPicks& picks, const EffectParams& params);
} // namespace lynx::grossbeat
