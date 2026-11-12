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
// One parameter of a goal: the effect's parameter (by its EffectInfo name) and its 0..1 value with the knob at 0, at
// 50 % and at 100 %, linear in between. A parameter the knob only sets (a style, a switch) has three equal values.
struct Curve
{
    const char* param;
    float at0, at50, at100;
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
        // Aether's dynamic air: the high band opens first and furthest, the mid band follows; the output comes down as
        // the air goes up (−2 dB at half, −3.5 dB at the top), so the knob adds brightness, not loudness
        { TR_NOOP ("Brighter"), TR_NOOP ("More air and presence."), EffectKind::aether, 0.5f,
          { { "Dynamic", 1.0f, 1.0f, 1.0f }, { "High air", 0.0f, 0.34f, 0.62f }, { "Mid air", 0.0f, 0.12f, 0.34f },
            { "Air", 0.0f, 0.0f, 0.0f }, { "Harmonics", 0.0f, 0.0f, 0.0f }, { "Output", 0.5f, 0.4167f, 0.3542f }, { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Echoform, one echo per quarter note: it comes in fast, the repeats grow in the second half (20 … 56 %), each
        // one darker (11 … 7.5 kHz) and without lows (309 Hz); the echo ducks a little under the sound itself
        { TR_NOOP ("Echo"), TR_NOOP ("A tempo-synced echo."), EffectKind::echoform, 0.4f,
          { { "Mix", 0.0f, 0.22f, 0.38f }, { "Feedback", 0.10f, 0.18f, 0.28f }, { "Low cut", 0.15f, 0.15f, 0.15f },
            { "High cut", 0.55f, 0.45f, 0.38f }, { "Ducking", 0.0f, 0.15f, 0.30f }, { "Style", 0.0f, 0.0f, 0.0f },
            { "Sync L", 0.5f, 0.5f, 0.5f }, { "Note L", 0.4f, 0.4f, 0.4f }, { "Sync R", 0.5f, 0.5f, 0.5f }, { "Note R", 0.4f, 0.4f, 0.4f } } },
        // Strike Shaper: more attack (+7.6 dB at half, +13.4 dB at the top), a touch less sustain, the guard catches the
        // new peaks
        { TR_NOOP ("Punch"), TR_NOOP ("Sharper hits, more snap."), EffectKind::strikeShaper, 0.5f,
          { { "Attack", 0.5f, 0.74f, 0.92f }, { "Sustain", 0.5f, 0.47f, 0.42f }, { "Guard", 1.0f, 1.0f, 1.0f },
            { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Atrium: more of the room, and a bigger one (0.4 s, 1 s, 2 s), as the knob goes up; the lows stay out of it
        { TR_NOOP ("Room"), TR_NOOP ("Place the sound in a room."), EffectKind::atrium, 0.35f,
          { { "Mix", 0.0f, 0.20f, 0.40f }, { "Decay", 0.36f, 0.44f, 0.50f }, { "Late size", 0.30f, 0.45f, 0.70f },
            { "Predelay", 0.02f, 0.05f, 0.09f }, { "Low cut", 0.20f, 0.20f, 0.20f } } },
        // Facet EQ: band 1 a low shelf at 90 Hz up to +7 dB, band 3 a wide bell at 300 Hz down to −2.5 dB, the output
        // down by up to 2 dB
        { TR_NOOP ("Bass"), TR_NOOP ("Fuller low end."), EffectKind::facetEq, 0.5f, {} }, // (built from Facet's own laws: bassMoves)
        // Glow Leveler (its plain voicing): the cell is driven harder (+21 dB at half, +34 dB at the top) and the gain
        // makes up what that takes away on a steady sound (+2.4 dB, +5.7 dB), so quiet parts come up to the loud ones
        { TR_NOOP ("Level"), TR_NOOP ("Evens out loud and quiet parts."), EffectKind::glowLeveler, 0.45f,
          { { "Auto makeup", 1.0f, 1.0f, 1.0f }, { "Makeup ref", 0.0f, 0.0f, 0.0f }, { "Voicing", 0.0f, 0.0f, 0.0f },
            { "Peak reduction", 0.0f, 0.68f, 0.90f }, { "Gain", 0.0f, 0.08f, 0.19f }, { "Mix", 1.0f, 1.0f, 1.0f } } },
        // Ember, pushed: the transistor style, the lows kept out of the drive, the output pulled down as the drive goes
        // up (−1 dB, −5 dB). The first half of the knob also brings the driven sound in (Mix), so 0 is the clean sound.
        { TR_NOOP ("Dirty"), TR_NOOP ("Grit and distortion."), EffectKind::ember, 0.4f,
          { { "Style", 0.75f, 0.75f, 0.75f }, { "Drive", 0.0f, 0.40f, 0.70f }, { "Tone", 0.5f, 0.55f, 0.60f },
            { "Low cut", 0.0f, 0.08f, 0.14f }, { "High cut", 1.0f, 1.0f, 1.0f }, { "Auto gain", 1.0f, 1.0f, 1.0f },
            { "Output", 0.5f, 0.4583f, 0.2917f }, { "Mix", 0.0f, 1.0f, 1.0f } } },
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

float at (const Curve& c, float amount) noexcept
{
    return amount <= 0.5f ? lerp (c.at0, c.at50, amount * 2.0f) : lerp (c.at50, c.at100, (amount - 0.5f) * 2.0f);
}

int paramIndex (EffectKind kind, const char* name) noexcept
{
    const auto& params = effectInfo (kind).params;
    for (std::size_t i = 0; i < params.size(); ++i)
        if (std::strcmp (params[i].name, name) == 0)
            return (int) i;
    return -1;
}

std::vector<Move> bassMoves (float amount)
{
    using facet::Shape;
    const auto shelfDb = amount <= 0.5f ? lerp (0.0f, 4.0f, amount * 2.0f) : lerp (4.0f, 7.0f, (amount - 0.5f) * 2.0f);
    const auto cutDb = amount <= 0.5f ? lerp (0.0f, -1.0f, amount * 2.0f) : lerp (-1.0f, -2.5f, (amount - 0.5f) * 2.0f);
    std::vector<Move> moves { { facet::param (0, 0), facetShape (Shape::lowShelf) }, { facet::param (0, 1), facetHz (90.0) },
                              { facet::param (0, 2), facetDb (shelfDb) },
                              { facet::param (2, 0), facetShape (Shape::bell) },     { facet::param (2, 1), facetHz (300.0) },
                              { facet::param (2, 2), facetDb (cutDb) } };
    // the output comes down 1 dB at half and 2 dB at the top (Facet's Output: ±24 dB over 0..1)
    if (const auto out = paramIndex (EffectKind::facetEq, "Output"); out >= 0)
        moves.push_back ({ out, 0.5f - amount * 2.0f / 48.0f });
    return moves;
}

float clamp01 (float v) noexcept { return std::isfinite (v) ? std::clamp (v, 0.0f, 1.0f) : 0.0f; }
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
    const auto* g = goalOf (id);
    if (g == nullptr)
        return {};
    amount = clamp01 (amount);
    if (id == bass)
        return bassMoves (amount);
    std::vector<Move> moves;
    moves.reserve (g->curves.size());
    for (const auto& c : g->curves)
        if (const auto p = paramIndex (g->kind, c.param); p >= 0 && p < maxEffectParams)
            moves.push_back ({ p, clamp01 (at (c, amount)) });
    return moves;
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
    for (const auto& m : mapping (slot.goal, slot.goalAmount))
        if (std::abs (slot.params[(std::size_t) m.param] - m.value) > 1.0e-4f)
            return false;
    return true;
}
} // namespace lynx::goals
