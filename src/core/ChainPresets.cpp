#include "core/ChainPresets.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Effects.h"
#include "core/GoalKnobs.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)
#endif

namespace lynx::chains
{
namespace
{
Step knob (int goal, float amount)
{
    Step s;
    s.goal = goal;
    s.amount = amount;
    return s;
}

Step plain (EffectKind kind, float mix, std::vector<std::pair<const char*, float>> params = {})
{
    Step s;
    s.kind = kind;
    s.mix = mix;
    s.params = std::move (params);
    return s;
}

bool sameSlot (const EffectSlot& a, const EffectSlot& b) noexcept
{
    if (a.kind != b.kind || a.enabled != b.enabled || a.goal != b.goal || std::abs (a.mix - b.mix) > 1.0e-4f)
        return false;
    if (a.goal != 0 && std::abs (a.goalAmount - b.goalAmount) > 1.0e-4f)
        return false;
    const auto used = std::min (a.params.size(), effectInfo (a.kind).params.size());
    for (std::size_t i = 0; i < used; ++i)
        if (std::abs (a.params[i] - b.params[i]) > 1.0e-4f)
            return false;
    return true;
}
} // namespace

const std::vector<Preset>& factory()
{
    using namespace goals;
    using K = EffectKind;
    // clang-format off
    static const std::vector<Preset> list {
        { "Clear Voice", "Voice", TR_NOOP ("Even, bright and a little room"), 1.0f,
          { knob (level, 0.40f), knob (brighter, 0.30f), knob (room, 0.18f) } }, // (190dg: Level 0.45 → 0.40, Brighter 0.35 → 0.30: peaks)
        { "Radio Booth", "Voice", TR_NOOP ("Close, warm and steady"), 1.0f,
          { knob (level, 0.60f), plain (K::hushDeEsser, 1.0f), knob (warm, 0.30f) } },
        { "Big Chorus", "Voice", TR_NOOP ("Doubled and wide, with space"), 1.0f,
          { knob (level, 0.40f), plain (K::geminiDoubler, 0.35f), knob (room, 0.45f), knob (echo, 0.20f) } },
        { "Whisper Air", "Voice", TR_NOOP ("Airy and soft"), 0.85f,
          { knob (brighter, 0.60f), knob (level, 0.30f), knob (room, 0.30f) } },

        { "Clean Shimmer", "Guitar", TR_NOOP ("Clean, glassy and moving"), 1.0f,
          { knob (level, 0.30f), knob (brighter, 0.30f), plain (K::orbit, 0.30f), knob (room, 0.25f) } },
        { "Crunch Stack", "Guitar", TR_NOOP ("Bite with body"), 1.0f,
          { knob (dirty, 0.45f), knob (bass, 0.25f), knob (room, 0.15f) } },
        { "Wall of Fuzz", "Guitar", TR_NOOP ("Thick, long and loud"), 1.0f,
          { knob (dirty, 0.85f), knob (level, 0.40f), knob (echo, 0.20f) } },

        { "Round Low", "Bass", TR_NOOP ("Full and even"), 1.0f,
          { knob (bass, 0.50f), knob (level, 0.45f) } },
        { "Growl", "Bass", TR_NOOP ("Grit on top, clean lows underneath"), 0.70f,
          { knob (dirty, 0.40f), knob (level, 0.50f) } },
        { "Sub Glue", "Bass", TR_NOOP ("Deep and warm"), 1.0f,
          { knob (warm, 0.40f), knob (bass, 0.60f) } },

        { "Tight Kit", "Drums", TR_NOOP ("Snappy and controlled"), 1.0f,
          { knob (punch, 0.50f), knob (level, 0.30f) } },
        { "Smash Room", "Drums", TR_NOOP ("Crushed room under the dry kit"), 0.60f,
          { knob (punch, 0.40f), knob (dirty, 0.30f), knob (room, 0.35f) } },
        { "Dusty Break", "Drums", TR_NOOP ("Old, soft and gritty"), 1.0f,
          { knob (warm, 0.60f), plain (K::lofi, 0.45f, { { "Rate", 0.35f }, { "Bits", 0.30f } }), knob (level, 0.40f) } },

        { "Soft Felt", "Keys", TR_NOOP ("Warm and close"), 1.0f,
          { knob (warm, 0.45f), knob (room, 0.30f) } },
        { "Glass Keys", "Keys", TR_NOOP ("Bright with a light echo"), 1.0f,
          { knob (brighter, 0.50f), knob (echo, 0.25f), knob (room, 0.20f) } },
        { "Wide Stage", "Keys", TR_NOOP ("Wide and far"), 1.0f,
          { plain (K::orbit, 0.35f), knob (room, 0.50f) } },

        { "Old Tape", "Lo-fi", TR_NOOP ("Saturated, dull and a little wobbly"), 1.0f,
          { knob (warm, 0.80f), plain (K::lofi, 0.30f, { { "Rate", 0.25f }, { "Bits", 0.20f } }), knob (echo, 0.15f) } },
        { "Pocket Radio", "Lo-fi", TR_NOOP ("Small, crunchy speaker"), 1.0f,
          { knob (dirty, 0.30f), plain (K::lofi, 0.70f, { { "Rate", 0.55f }, { "Bits", 0.45f } }), knob (level, 0.50f) } },
        { "Worn Cassette", "Lo-fi", TR_NOOP ("Soft highs and a short room"), 0.90f,
          { knob (warm, 0.65f), knob (level, 0.35f), knob (room, 0.20f) } },

        { "Canyon", "Space", TR_NOOP ("Long echoes in a big room"), 1.0f,
          { knob (echo, 0.60f), knob (room, 0.60f) } },
        { "Night Hall", "Space", TR_NOOP ("A large, bright hall"), 1.0f,
          { knob (room, 0.80f), knob (brighter, 0.20f) } },
        { "Slap Back", "Space", TR_NOOP ("One short echo, close"), 1.0f,
          { knob (echo, 0.30f), knob (room, 0.12f) } },
    };
    // clang-format on
    return list;
}

const std::vector<const char*>& categories()
{
    static const std::vector<const char*> list { TR_NOOP ("Voice"), TR_NOOP ("Guitar"), "Bass", TR_NOOP ("Drums"), "Keys", TR_NOOP ("Lo-fi"), "Space" };
    return list;
}

const Preset* find (const std::string& name)
{
    for (const auto& p : factory())
        if (name == p.name)
            return &p;
    return nullptr;
}

std::vector<EffectSlot> slots (const Preset& preset)
{
    std::vector<EffectSlot> out;
    for (const auto& step : preset.steps)
    {
        if ((int) out.size() >= maxEffectSlots)
            break;
        EffectSlot slot;
        if (step.goal != 0)
            slot = goals::make (step.goal, step.amount);
        else
        {
            slot = makeEffectSlot (step.kind);
            const auto& info = effectInfo (step.kind).params;
            for (const auto& [name, value] : step.params)
                for (std::size_t i = 0; i < info.size() && i < slot.params.size(); ++i)
                    if (std::strcmp (info[i].name, name) == 0)
                        slot.params[i] = std::clamp (value, 0.0f, 1.0f);
            slot.mix = std::clamp (step.mix, 0.0f, 1.0f);
        }
        slot.row = (int) out.size();
        out.push_back (std::move (slot));
    }
    return out;
}

bool applyChain (Song& song, int track, std::vector<EffectSlot> chain, float blend, const std::string& name)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;
    if ((int) chain.size() > maxEffectSlots)
        chain.resize ((std::size_t) maxEffectSlots);
    // rows: kept when they rise and fit FL's 10 places, else packed from the top
    int last = -1;
    bool valid = true;
    for (const auto& s : chain)
    {
        valid = valid && s.row > last && s.row < maxEffectSlots;
        last = s.row;
    }
    if (! valid)
        for (std::size_t i = 0; i < chain.size(); ++i)
            chain[i].row = (int) i;
    auto& t = song.mixer[(std::size_t) track];
    t.effects = std::move (chain);
    t.chainBlend = std::isfinite (blend) ? std::clamp (blend, 0.0f, 1.0f) : 1.0f;
    t.chainPreset = name;
    return true;
}

bool apply (Song& song, int track, const Preset& preset) { return applyChain (song, track, slots (preset), preset.blend, preset.name); }

bool unchanged (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return false;
    const auto& t = song.mixer[(std::size_t) track];
    if (t.chainPreset.empty())
        return false;
    const auto* preset = find (t.chainPreset);
    if (preset == nullptr)
        return true;
    const auto want = slots (*preset);
    if (want.size() != t.effects.size() || std::abs (preset->blend - t.chainBlend) > 1.0e-4f)
        return false;
    for (std::size_t i = 0; i < want.size(); ++i)
        if (! sameSlot (want[i], t.effects[i]))
            return false;
    return true;
}

MixerTrack stateOf (const MixerTrack& track, const std::string& name)
{
    MixerTrack state;
    state.name = name;
    state.effects = track.effects;
    state.chainBlend = track.chainBlend;
    state.chainPreset = name;
    return state;
}
} // namespace lynx::chains
