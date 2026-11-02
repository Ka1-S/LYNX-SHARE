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
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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
// the slot's level at `beat` (0…4) as the engine plays it
float levelAt (const Slot& slot, double beat);
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

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
};

std::unique_ptr<Effect> make (const std::vector<std::uint8_t>& container);
} // namespace lynx::grossbeat
