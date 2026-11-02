// LYNX's own Fruity DX10 presets (round 168o): designed for LYNX on DX10's engine (core/Dx10.h), clean-room — no FL
// preset was read or converted. Each preset gives the 22 knobs as positions 0…1 (raw = position × 65536; Coarse −3…3
// is given as octaves). Laws (REA_rodada42): modulator ratio = ⌊40.1·c²⌋ + fine (fine ≥ 0.5: ¼, ⅓, ½, ⅔, ¾), level
// 0.0002·x² per sample, amplitude decay ≥ 0.98 = hold.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Dx10.h"
#include "core/LynxBanks.h"

namespace lynx::banks
{
namespace
{
// knob positions in the plugin's order (core/ModelParamsSynths.cpp)
struct Dx
{
    float att = 0, dec = 0.65f, rel = 0.45f;
    float m1coarse = 0.19f, m1fine = 0, m1init = 0.4f, m1time = 0.55f, m1sus = 0.2f, m1rel = 0.5f, m1vel = 0.5f;
    float vib = 0, wave = 0, thru = 0, lfo = 0.4f;
    float m2coarse = 0.19f, m2fine = 0, m2init = 0, m2time = 0.5f, m2sus = 0, m2rel = 0.5f, m2vel = 0.5f;
    int octave = 0;
};

// the coarse knob position that gives modulator ratio n (⌊40.1·c²⌋ = n, centred in its step)
float coarseFor (int n) { return n <= 0 ? 0.05f : (float) std::sqrt (((double) n + 0.5) / 40.1); }
// fine positions for the fixed fractions ⅓, ½, ⅔, ¾ (roundEven (8.9·f) = 5…8)
constexpr float third = 0.57f, half = 0.68f, twoThirds = 0.79f, threeQuarters = 0.9f;

std::vector<std::uint8_t> stateOf (const Dx& d)
{
    const float v[22] = { d.att, d.dec, d.rel, d.m1coarse, d.m1fine, d.m1init, d.m1time, d.m1sus, d.m1rel, d.m1vel, d.vib,
                          d.wave, d.thru, d.lfo, d.m2coarse, d.m2fine, d.m2init, d.m2time, d.m2sus, d.m2rel, d.m2vel, 0.0f };
    std::vector<std::uint8_t> s (4 + 4 * 22, 0);
    const std::int32_t version = 1;
    std::memcpy (s.data(), &version, 4);
    for (int i = 0; i < 21; ++i)
    {
        const auto raw = (std::int32_t) std::lround (std::clamp (v[i], 0.0f, 1.0f) * 65536.0f);
        std::memcpy (s.data() + 4 + 4 * i, &raw, 4);
    }
    const std::int32_t oct = std::clamp (d.octave, -3, 3);
    std::memcpy (s.data() + 4 + 4 * 21, &oct, 4);
    return s;
}

Preset make (const char* name, const char* category, std::vector<std::string> tags, const Dx& d)
{
    const auto state = stateOf (d);
    return { name, category, std::move (tags), [state] (ChannelDef& c) { c.pluginState = state; } };
}
} // namespace

std::vector<Preset> dx10Bank()
{
    std::vector<Preset> b;
    {
        Dx d;
        d.m1init = 0.3f, d.m1sus = 0.15f;
        b.push_back (make ("Init FM", "Init", { "init", "fm" }, d));
    }
    {
        Dx d; // a bright tine on ratio 1 with a high, fast second modulator
        d.dec = 0.72f, d.rel = 0.5f, d.m1coarse = coarseFor (1), d.m1init = 0.5f, d.m1time = 0.62f, d.m1sus = 0.12f, d.m1vel = 0.7f;
        d.m2coarse = coarseFor (14), d.m2init = 0.22f, d.m2time = 0.42f, d.m2sus = 0.0f, d.m2vel = 0.6f, d.vib = 0.15f;
        b.push_back (make ("Tine E-Piano", "Keys", { "epiano", "fm", "classic" }, d));
    }
    {
        Dx d;
        d.dec = 0.6f, d.rel = 0.42f, d.m1coarse = coarseFor (2), d.m1init = 0.45f, d.m1time = 0.5f, d.m1sus = 0.3f, d.m1vel = 0.6f;
        d.m2coarse = coarseFor (1), d.m2fine = 0.03f, d.m2init = 0.2f, d.m2time = 0.55f, d.m2sus = 0.15f;
        b.push_back (make ("Soft Clav", "Keys", { "clav", "pluck" }, d));
    }
    {
        Dx d; // drawbar-ish: sustained, ratio 2 and 3 modulators
        d.att = 0.05f, d.dec = 1.0f, d.rel = 0.3f, d.m1coarse = coarseFor (2), d.m1init = 0.18f, d.m1time = 0.4f, d.m1sus = 0.18f,
        d.m1vel = 0.1f, d.m2coarse = coarseFor (3), d.m2init = 0.12f, d.m2time = 0.4f, d.m2sus = 0.12f, d.vib = 0.22f, d.lfo = 0.55f;
        b.push_back (make ("FM Organ", "Keys", { "organ", "sustained" }, d));
    }
    {
        Dx d;
        d.dec = 0.55f, d.rel = 0.25f, d.m1coarse = coarseFor (1), d.m1init = 0.62f, d.m1time = 0.45f, d.m1sus = 0.28f, d.m1vel = 0.5f;
        d.octave = -1;
        b.push_back (make ("Punch FM Bass", "Bass", { "bass", "punchy" }, d));
    }
    {
        Dx d;
        d.dec = 1.0f, d.rel = 0.25f, d.m1coarse = coarseFor (1), d.m1init = 0.42f, d.m1time = 0.35f, d.m1sus = 0.4f, d.m1vel = 0.3f,
        d.wave = 0.25f, d.octave = -1;
        b.push_back (make ("Solid Sub FM", "Bass", { "bass", "sustained" }, d));
    }
    {
        Dx d;
        d.dec = 0.5f, d.rel = 0.22f, d.m1coarse = coarseFor (3), d.m1fine = 0.04f, d.m1init = 0.55f, d.m1time = 0.38f, d.m1sus = 0.08f,
        d.m1vel = 0.7f, d.octave = -1;
        b.push_back (make ("Slap Bass", "Bass", { "bass", "slap" }, d));
    }
    {
        Dx d; // inharmonic bell: ratio 3½ and 7
        d.dec = 0.88f, d.rel = 0.72f, d.m1coarse = coarseFor (3), d.m1fine = half, d.m1init = 0.55f, d.m1time = 0.78f, d.m1sus = 0.08f,
        d.m1vel = 0.5f, d.m2coarse = coarseFor (7), d.m2init = 0.18f, d.m2time = 0.6f, d.m2sus = 0.0f;
        b.push_back (make ("Crystal Bell", "Pluck", { "bell", "metallic" }, d));
    }
    {
        Dx d;
        d.dec = 0.45f, d.rel = 0.35f, d.m1coarse = coarseFor (4), d.m1init = 0.38f, d.m1time = 0.35f, d.m1sus = 0.0f, d.m1vel = 0.6f,
        d.m2coarse = coarseFor (1), d.m2init = 0.15f, d.m2time = 0.3f;
        b.push_back (make ("Marimba", "Pluck", { "mallet", "wood" }, d));
    }
    {
        Dx d;
        d.dec = 0.5f, d.rel = 0.4f, d.m1coarse = coarseFor (2), d.m1fine = third, d.m1init = 0.35f, d.m1time = 0.5f, d.m1sus = 0.05f,
        d.m1vel = 0.6f, d.thru = 0.3f;
        b.push_back (make ("Glass Pluck", "Pluck", { "glass", "bright" }, d));
    }
    {
        Dx d; // brass: slow attack on the modulator index
        d.att = 0.32f, d.dec = 1.0f, d.rel = 0.38f, d.m1coarse = coarseFor (1), d.m1init = 0.12f, d.m1time = 0.42f, d.m1sus = 0.45f,
        d.m1vel = 0.45f, d.vib = 0.2f, d.lfo = 0.5f;
        b.push_back (make ("FM Brass", "Lead", { "brass", "sustained" }, d));
    }
    {
        Dx d;
        d.dec = 1.0f, d.rel = 0.4f, d.m1coarse = coarseFor (1), d.m1fine = 0.05f, d.m1init = 0.3f, d.m1time = 0.4f, d.m1sus = 0.35f,
        d.m1vel = 0.4f, d.wave = 0.35f, d.vib = 0.3f, d.lfo = 0.5f;
        b.push_back (make ("Reedy Lead", "Lead", { "lead", "vibrato" }, d));
    }
    {
        Dx d;
        d.att = 0.55f, d.dec = 1.0f, d.rel = 0.7f, d.m1coarse = coarseFor (1), d.m1fine = 0.06f, d.m1init = 0.08f, d.m1time = 0.75f,
        d.m1sus = 0.25f, d.m1vel = 0.2f, d.m2coarse = coarseFor (2), d.m2fine = 0.05f, d.m2init = 0.0f, d.m2time = 0.8f, d.m2sus = 0.12f,
        d.vib = 0.12f, d.lfo = 0.3f;
        b.push_back (make ("Warm FM Pad", "Pad", { "pad", "slow" }, d));
    }
    {
        Dx d;
        d.att = 0.45f, d.dec = 1.0f, d.rel = 0.75f, d.m1coarse = coarseFor (5), d.m1fine = twoThirds, d.m1init = 0.0f, d.m1time = 0.85f,
        d.m1sus = 0.3f, d.m1vel = 0.1f, d.thru = 0.25f;
        b.push_back (make ("Metal Haze", "Pad", { "pad", "metallic" }, d));
    }
    {
        Dx d; // FX: high, slowly falling index, vibrato
        d.dec = 0.8f, d.rel = 0.6f, d.m1coarse = coarseFor (11), d.m1fine = threeQuarters, d.m1init = 0.8f, d.m1time = 0.85f, d.m1sus = 0.0f,
        d.m1vel = 0.3f, d.vib = 0.6f, d.lfo = 0.75f;
        b.push_back (make ("Laser Drop", "FX", { "fx", "metallic" }, d));
    }
    return b;
}
} // namespace lynx::banks
