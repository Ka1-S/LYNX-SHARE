#include "core/GoalKnobs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#include "core/Effects.h"
#include "core/FacetEq.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::goals
{
namespace
{
// One parameter of a goal: the effect's parameter (by its EffectInfo name) and its 0..1 value with the knob at 0, 25,
// 50, 75 and 100 %, linear in between (round 190dg: five points, so the level can be matched along the whole turn, not
// only at its middle; round 190bu's three-point laws are the same values with their midpoints). A parameter the knob
// only sets (a style, a switch) has equal values.
struct Curve
{
    const char* param;
    float v[5];
    Curve (const char* p, float at0, float at50, float at100) : param (p), v { at0, (at0 + at50) * 0.5f, at50, (at50 + at100) * 0.5f, at100 } {}
    Curve (const char* p, float a, float b, float c, float d, float e) : param (p), v { a, b, c, d, e } {}
};

struct Goal
{
    const char* name;
    const char* description;
    EffectKind kind;
    float start; // the knob's place in a new slot
    std::vector<Curve> curves;
};

// Facet EQ's values come from its own laws (Hz, dB), so the table reads in units
float facetHz (double hz) { return facet::hzValue (hz); }
float facetDb (double db) { return facet::gainValue (db); }
float facetShape (facet::Shape s) { return facet::shapeValue (s); }

const std::array<Goal, count>& table()
{
    // clang-format off
    static const std::array<Goal, count> goals { {
        // Aether's dynamic air: mostly the mid band (its detector is instant), a little of the high band (its 2 ms attack
        // lets a hit's first milliseconds through, +8 dB on a hot snare at 62 %: round 190dg keeps it at 10 % or less);
        // the output comes down as the air goes up (−1.3 dB at half, −1.9 dB at the top), so the knob adds brightness,
        // not loudness. Measured (round 190dg): +4.4 dB above 5 kHz on pink noise at the top, peaks +2.1 dB at most
        { TR_NOOP ("Brighter"), TR_NOOP ("More air and presence."), EffectKind::aether, 0.5f,
          { { "Dynamic", 1.0f, 1.0f, 1.0f }, { "High air", 0.0f, 0.03f, 0.06f, 0.08f, 0.10f }, { "Mid air", 0.0f, 0.20f, 0.40f, 0.52f, 0.62f },
            { "Air", 0.0f, 0.0f, 0.0f }, { "Harmonics", 0.0f, 0.0f, 0.0f }, { "Output", 0.5f, 0.47f, 0.445f, 0.43f, 0.42f }, { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Echoform, one echo per quarter note: it comes in fast, the repeats grow in the second half (20 … 56 %), each
        // one darker (11 … 7.5 kHz) and without lows (309 Hz); the echo ducks a little under the sound itself
        { TR_NOOP ("Echo"), TR_NOOP ("A tempo-synced echo."), EffectKind::echoform, 0.4f,
          { { "Mix", 0.0f, 0.22f, 0.38f }, { "Feedback", 0.10f, 0.18f, 0.28f }, { "Low cut", 0.15f, 0.15f, 0.15f },
            { "High cut", 0.55f, 0.45f, 0.38f }, { "Ducking", 0.0f, 0.15f, 0.30f }, { "Style", 0.0f, 0.0f, 0.0f },
            { "Sync L", 0.5f, 0.5f, 0.5f }, { "Note L", 0.4f, 0.4f, 0.4f }, { "Sync R", 0.5f, 0.5f, 0.5f }, { "Note R", 0.4f, 0.4f, 0.4f } } },
        // Strike Shaper: more attack (+7.6 dB at half, +13.4 dB at the top), a touch less sustain, the guard catches the
        // new peaks at full scale; the output comes down 1.5 dB at the top (round 190dg: louder hits are not a louder track)
        { TR_NOOP ("Punch"), TR_NOOP ("Sharper hits, more snap."), EffectKind::strikeShaper, 0.5f,
          { { "Attack", 0.5f, 0.62f, 0.74f, 0.83f, 0.92f }, { "Sustain", 0.5f, 0.485f, 0.47f, 0.445f, 0.42f }, { "Guard", 1.0f, 1.0f, 1.0f },
            { "Output", 0.5f, 0.49f, 0.475f, 0.46f, 0.45f }, { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Atrium: more of the room, and a longer one (0.4 s, 1 s, 2 s), as the knob goes up; the lows stay out of it.
        // Round 190dg: the room's size and pre-delay stay put (moving them while the sound plays clicks: they change the
        // reverb's delay lengths; 11× the steady slope jump on a held tone, 0.8× with only Mix and Decay moving)
        { TR_NOOP ("Room"), TR_NOOP ("Place the sound in a room."), EffectKind::atrium, 0.35f,
          { { "Mix", 0.0f, 0.20f, 0.40f }, { "Decay", 0.36f, 0.44f, 0.50f }, { "Late size", 0.45f, 0.45f, 0.45f },
            { "Predelay", 0.05f, 0.05f, 0.05f }, { "Low cut", 0.20f, 0.20f, 0.20f } } },
        // Facet EQ: band 1 a low shelf at 90 Hz up to +7 dB, band 3 a wide bell at 300 Hz down to −1.6 dB, the output
        // down by up to 2.2 dB (eachMove; round 190dg: five points, and ±0.02 dB at 0 keeps both bands built — a band
        // that appears restarts from a clean state, a click when the knob leaves 0)
        { TR_NOOP ("Bass"), TR_NOOP ("Fuller low end."), EffectKind::facetEq, 0.5f, {} }, // (built from Facet's own laws: bassMoves)
        // Glow Leveler (its plain voicing): the cell is driven harder as the knob goes up and the gain makes up what that
        // takes away on a steady sound, so quiet parts come up to the loud ones. Round 190dg: five points (the old law
        // fell 2 dB between 40 and 50 % on drums), and less make-up at the top: the cell's slow attack lets a hit's start
        // through, so make-up gain is peak gain (+2.7 dB at most now)
        { TR_NOOP ("Level"), TR_NOOP ("Evens out loud and quiet parts."), EffectKind::glowLeveler, 0.45f,
          { { "Auto makeup", 1.0f, 1.0f, 1.0f }, { "Makeup ref", 0.0f, 0.0f, 0.0f }, { "Voicing", 0.0f, 0.0f, 0.0f },
            { "Peak reduction", 0.0f, 0.42f, 0.64f, 0.74f, 0.82f }, { "Gain", 0.0f, 0.03f, 0.075f, 0.105f, 0.135f }, { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Ember, pushed: the transistor style, the knob brings the driven sound in (Mix) as it drives it harder, up to 80 %
        // driven at the top: the 20 % clean keeps a loud sound from sinking (a saturator with auto gain squashes hot
        // sounds and lifts quiet ones). Round 190dg: no low cut (it removed the lows of a bass and, half mixed with the
        // clean sound, cancelled them: −2 dB at 30 %), less drive at the top (+21 dB, was +25) and 0.5 dB of trim (was 5:
        // it sank hot drums 6.6 dB): every test sound within −6 … +3 dB of the clean one
        { TR_NOOP ("Dirty"), TR_NOOP ("Grit and distortion."), EffectKind::ember, 0.4f,
          { { "Style", 0.75f, 0.75f, 0.75f }, { "Drive", 0.0f, 0.20f, 0.36f, 0.48f, 0.58f }, { "Tone", 0.5f, 0.52f, 0.55f, 0.58f, 0.60f },
            { "Low cut", 0.0f, 0.0f, 0.0f }, { "High cut", 1.0f, 1.0f, 1.0f }, { "Auto gain", 1.0f, 1.0f, 1.0f },
            { "Output", 0.5f, 0.5f, 0.5f, 0.49f, 0.48f }, { "Mix", 0.0f, 0.40f, 0.70f, 0.75f, 0.80f } } },
        // Ember, gentle: the tube style, less drive, the highs rounded off (15.9 kHz, 12.6 kHz)
        { TR_NOOP ("Warm"), TR_NOOP ("Gentle saturation, softer highs."), EffectKind::ember, 0.4f,
          { { "Style", 0.0f, 0.0f, 0.0f }, { "Drive", 0.0f, 0.26f, 0.48f }, { "Tone", 0.5f, 0.44f, 0.38f },
            { "Low cut", 0.0f, 0.0f, 0.0f }, { "High cut", 1.0f, 0.90f, 0.80f }, { "Auto gain", 1.0f, 1.0f, 1.0f },
            { "Output", 0.5f, 0.5f, 0.5f }, { "Mix", 0.0f, 1.0f, 1.0f } } },
    } };
    // clang-format on
    return goals;
}

const Goal* goalOf (int id) noexcept { return id >= 1 && id <= count ? &table()[(std::size_t) (id - 1)] : nullptr; }

float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

float at (const float (&v)[5], float amount) noexcept
{
    const auto x = std::clamp (amount, 0.0f, 1.0f) * 4.0f;
    const auto i = std::min (3, (int) x);
    return lerp (v[i], v[i + 1], x - (float) i);
}
float at (const Curve& c, float amount) noexcept { return at (c.v, amount); }

int paramIndex (EffectKind kind, const char* name) noexcept
{
    const auto& params = effectInfo (kind).params;
    for (std::size_t i = 0; i < params.size(); ++i)
        if (std::strcmp (params[i].name, name) == 0)
            return (int) i;
    return -1;
}

float clamp01 (float v) noexcept { return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f; }

// Every parameter goal `id` writes with its knob at `amount`, in the table's order: f (param, value). No allocation
// (the renderer's glide runs it on the audio thread, round 190dg).
template <typename F>
void eachMove (int id, float amount, F&& f)
{
    const auto* g = goalOf (id);
    if (g == nullptr)
        return;
    amount = clamp01 (amount);
    if (id == bass)
    {
        using facet::Shape;
        // band 1 a low shelf at 90 Hz, band 3 a wide bell at 300 Hz, the output (dB at 0 / 25 / 50 / 75 / 100 %)
        static constexpr float shelf[5] { 0.02f, 2.2f, 4.0f, 5.6f, 7.0f };
        static constexpr float cut[5] { -0.02f, -0.4f, -0.8f, -1.2f, -1.6f };
        static constexpr float output[5] { 0.0f, -0.5f, -1.0f, -1.6f, -2.2f };
        f (facet::param (0, 0), facetShape (Shape::lowShelf));
        f (facet::param (0, 1), facetHz (90.0));
        f (facet::param (0, 2), facetDb (at (shelf, amount)));
        f (facet::param (2, 0), facetShape (Shape::bell));
        f (facet::param (2, 1), facetHz (300.0));
        f (facet::param (2, 2), facetDb (at (cut, amount)));
        // (Facet's Output: ±24 dB over 0..1)
        if (const auto out = paramIndex (EffectKind::facetEq, "Output"); out >= 0)
            f (out, 0.5f + at (output, amount) / 48.0f);
        return;
    }
    for (const auto& c : g->curves)
        if (const auto p = paramIndex (g->kind, c.param); p >= 0 && p < maxEffectParams)
            f (p, clamp01 (at (c, amount)));
}

} // namespace

const char* name (int id) noexcept
{
    const auto* g = goalOf (id);
    return g != nullptr ? g->name : "";
}

const char* description (int id) noexcept
{
    const auto* g = goalOf (id);
    return g != nullptr ? g->description : "";
}

EffectKind kindOf (int id) noexcept
{
    const auto* g = goalOf (id);
    return g != nullptr ? g->kind : EffectKind::eq;
}

float defaultAmount (int id) noexcept
{
    const auto* g = goalOf (id);
    return g != nullptr ? g->start : 0.5f;
}

int idOfName (const std::string& text) noexcept
{
    for (int id = 1; id <= count; ++id)
        if (text == name (id))
            return id;
    return none;
}

std::vector<Move> mapping (int id, float amount)
{
    std::vector<Move> moves;
    eachMove (id, amount, [&moves] (int p, float v) { moves.push_back ({ p, v }); });
    return moves;
}

void writeParams (int id, float amount, EffectParams& params) noexcept
{
    eachMove (id, amount, [&params] (int p, float v) { params[(std::size_t) p] = v; });
}

EffectSlot make (int id, float amount)
{
    const auto* g = goalOf (id);
    if (g == nullptr)
        return makeEffectSlot (EffectKind::eq);
    auto slot = makeEffectSlot (g->kind);
    slot.goal = (std::uint8_t) id;
    setAmount (slot, amount < 0.0f ? g->start : amount);
    return slot;
}

void setAmount (EffectSlot& slot, float amount)
{
    const auto* g = goalOf (slot.goal);
    if (g == nullptr || slot.kind != g->kind)
        return;
    slot.goalAmount = clamp01 (amount);
    for (const auto& m : mapping (slot.goal, slot.goalAmount))
        slot.params[(std::size_t) m.param] = m.value;
}

bool active (const EffectSlot& slot) noexcept
{
    const auto* g = goalOf (slot.goal);
    if (g == nullptr || slot.kind != g->kind)
        return false;
    bool same = true;
    eachMove (slot.goal, slot.goalAmount, [&slot, &same] (int p, float v) { same = same && std::abs (slot.params[(std::size_t) p] - v) <= 1.0e-4f; });
    return same;
}
} // namespace lynx::goals
