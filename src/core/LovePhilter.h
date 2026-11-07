#pragma once

// FL Studio's Fruity Love Philter as a LYNX mixer effect: the plugin's own engine rebuilt from the installation's DLL
// in docs/analise/REA_rodada66.md (clean-room: layouts, formulas and behaviour in our own words; no Image-Line code or
// data in this repository). Rounds 16–20 and 27 decoded the state and the laws; round 66 decoded the render itself
// (oversampling, control ticks, articulators, the twelve filter kernels, waveshaper, envelope follower, pan law) and
// verified it against the DLL.
//
// The effect slot keeps FL's saved state (plugin record 0xD5) in EffectSlot::state. The 48 slot parameters are the
// controls LYNX's editor and automation reach, each as (raw − min) / (max − min) of FL's raw range:
//   [0…7]   the globals: master level, master LFO level, follower level, follower attack, follower release, pattern,
//           Mod X, Mod Y;
//   [8 + 5u + 0…4] unit u (0…7): cutoff, resonance, envelope amount, pan, volume.
// The other unit controls (L/B/H, waveshaper, IN, NEXT) and the articulators live in the state only.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Effects.h"

namespace lynx::philter
{
constexpr int paramCount = 48 + 55; // (round 173d3: + the units' L, B, H, WS, IN, NEXT)

// FL parameter index (0…358, REA_rodada16 §2.4) of slot parameter `param`
int flIndexOf (int param) noexcept;
// slot parameter of FL parameter index k (−1 when the slot does not expose it)
int paramOfFlIndex (int k) noexcept;
// FL's raw range of FL parameter k (for the 48 exposed ones)
void rawRange (int k, int& lo, int& hi) noexcept;

// FL's state → the engine's view; nullopt when it cannot be read (version outside 1…12, bad zlib, truncated)
struct State;
std::shared_ptr<const State> parse (const std::vector<std::uint8_t>& flState);
int rawParam (const State& s, int k) noexcept; // current raw value of FL parameter k (exposed ones)

// The plugin object. Host model: FL's mixer (rate, process mode, tempo and transport messages, parameter changes).
class Engine
{
public:
    explicit Engine (std::shared_ptr<const State> state);
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;

    void setRenderMode (bool rendering) noexcept; // FL's process-mode flag (HQ non-realtime): oversampling ×2, fine ticks
    void setRate (int sampleRate) noexcept;       // the sample-rate message: everything restarts at the song start
    void setTempo (double bpm) noexcept;          // FL's tempo event (default 140)
    // transport: FL's "playing" message (with the song position in beats); while playing, envelopes and LFOs are
    // positioned at the song position, otherwise they run freely from where they are restarted
    void setPlaying (bool playing, double songBeats) noexcept;
    void jump (double songBeats) noexcept; // song position changed while playing
    void setParam (int k, int raw) noexcept; // FL parameter k (0…358), raw value
    void render (const float* in, float* out, int n) noexcept; // interleaved stereo; in may equal out

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// ---- The editable state (round 173d3: FL's unit page and articulator editor) --------------------------------------
// FL's whole state as the editor sees it: read from the slot state and written back in FL's own layout (version
// word + zlib), so the engine, FL export and FL itself read the edit. Fields the plugin keeps but the editor does not
// touch travel as raw bytes.
struct DocPoint
{
    double dt = 0, y = 0;  // time since the previous point (seconds / beats / map x) and level 0…1
    float tension = 0;     // −1…1
    std::uint8_t mode = 0; // FL's curve mode (0 single curve, 1 double, 2 hold, 3 stairs, … 10 smooth, 12 double curve 3)
    std::array<std::uint8_t, 3> pad {};
};
struct DocEnv // FL's envelope record (TMEnvelope): the pattern envelopes, the LFO amplitude, the maps, the shaper
{
    int kind = 1, ver = 3;
    std::vector<DocPoint> pts;
    int flags = 0; // bit 0 tempo-based time, bit 2 bipolar LFO tension, bit 3 frozen LFO (the others kept)
    int on = 0, decay = -1, loop = -1, sus = -1;
};
struct DocArt // one articulator of a unit (targets: 0 pan, 1 volume, 2 cutoff, 3 resonance, 4 L/gain/drive, 5 band, 6 high, 7 shaper mix)
{
    std::int32_t lead = 0;
    float base = 0, envDepth = 1, lfoDepth = 0.5f;
    std::array<DocEnv, 10> pat;                             // the pattern envelopes (pattern 1…10)
    std::array<std::array<std::uint8_t, 16>, 10> patKnobs {}; // their ATT / DEC / SUS / REL ints (FL keeps, the plugin ignores)
    DocEnv lfo;                                              // the LFO's amplitude envelope (on = the LFO is on)
    std::vector<std::int32_t> lfoKnobs;                      // speed, tension, skew, pulse width (, phase)
    std::array<DocEnv, 3> maps;                              // Mod X, Mod Y, envelope follower
};
// FL's short name of a filter type in the unit's type box (Fruity Love Philter_x64.dll's label table, same order as the
// long names of its hint bar: SVF LP HP LP2 BP BS HP2 AP LS PK HS LP3)
const char* typeShortName (int type) noexcept;

struct DocUnit
{
    std::array<std::int32_t, 12> p {}; // cutoff, res, env, L, B, H, WS pre, WS mix, pan, vol, IN, NEXT (FL raw)
    std::int32_t mode = 1, type = 0;   // mode 0 off, 1 single … 5 alt triple; type 0…11 (Chocolate SVF … Mango LP)
    std::uint8_t flat = 0, enabled = 0, wsOn = 0;
    DocEnv ws;                         // the waveshaper curve (a map)
    std::uint8_t wsBip = 0;            // 1 = unipolar
    std::array<DocArt, 8> art;
};
struct Doc
{
    std::uint32_t versionWord = 0;
    int version = 0;
    std::vector<std::uint8_t> head; // globals: 6 ints, the options block, USE1 and the name, raw
    std::array<DocUnit, 8> u;
    std::vector<std::uint8_t> tail;
    // the globals in `head` (FL raw): 0 level, 1 LFO, 2 follower level, 3 attack, 4 release, 5 pattern
    int global (int i) const noexcept;
    void setGlobal (int i, int raw) noexcept;
    // the options block: 0 Mod X, 1 Mod Y, 4 solo (−1 none), 5 HQ envelopes, 6 oversampling (0…), as ints;
    // bytes: smooth 28, center 29, sync patterns 30
    int option (int i) const noexcept;
    void setOption (int i, int raw) noexcept;
    int optionByte (int offset) const noexcept;
    void setOptionByte (int offset, int value) noexcept;
    int use1() const noexcept;
    void setUse1 (int v) noexcept;
};
// the curve as the plugin steps it, at `count` evenly spaced x over 0…span (a map: over 0…1, normalised): the editor's drawing
std::vector<float> curveShape (const DocEnv& e, bool map, int count, double span);
std::optional<Doc> readDoc (const std::vector<std::uint8_t>& flState);
std::vector<std::uint8_t> writeDoc (const Doc& doc);

// the unit controls FL automates beside the 48 above: slot parameters 48 + 7u + j, j = L, B, H, WS pre, WS mix, IN,
// NEXT (unit 8 has no NEXT: 55 in all)
constexpr int unitExtraFields = 7, unitExtraCount = 8 * unitExtraFields - 1;
// fill the parameters a project saved before they existed (savedCount ≤ 48) from the slot's state
void upgradeParams (EffectSlot& slot, std::size_t savedCount) noexcept;

// FL import: the exact Love Philter slot for a saved state (nullopt when the state cannot be read)
std::optional<EffectSlot> slotFromFl (const std::vector<std::uint8_t>& flState);
const EffectInfo& info();
std::unique_ptr<Effect> make (const EffectSlot& slot);
std::string paramText (int param, float value01);
} // namespace lynx::philter
