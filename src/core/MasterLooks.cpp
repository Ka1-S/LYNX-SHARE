#include "core/MasterLooks.h"

#include <algorithm>
#include <cmath>

#include "core/ApexLimiter.h"
#include "core/Effects.h"
#include "core/FruityDynamics.h"
#include "core/FruityMultiband.h"
#include "core/FruityShapers.h"
#include "core/MixerOps.h"
#include "core/ParametricEqFl.h"
#include "core/StereoFx.h"

namespace lynx::looks
{
namespace
{
// One end of a look (Light or Heavy). LYNX's own values, tuned by ear and by loudness / tilt measurements (round
// 189k notes): EQ gains in dB, the multiband's thresholds (dB) and amount (FL's ratio control, 0…1), its make-up gains,
// the drive into the finisher (dB, the multiband's output gain; soft-clipper looks only), the Stereo Enhancer's
// separation (FL's raw −96…96, negative = wider) and the Limiter's gain (dB).
struct Point
{
    float low = 0, mud = 0, mid = 0, presence = 0, air = 0;
    std::array<float, 3> threshold { 0, 0, 0 }; // low, mid, high band
    float amount = 0, attackMs = 20, releaseMs = 200;
    std::array<float, 3> makeup { 0, 0, 0 };
    float drive = 0, separation = 0, limiter = 0;
};

enum class Finisher { softClipper, enhancer };

struct Look
{
    const char* name;
    const char* description;
    Finisher finisher;
    Point light, heavy;
    // the Limiter's ceiling (dB, sample peak): low enough that the peaks between samples stay under −0.5 dBTP on the
    // tuning songs at every intensity (round 189k §3; FL's Limiter holds sample peaks only)
    float ceilingDb = -1.0f;
};

// clang-format off
const Look table[count] {
    { "Balanced", "Even and polished, a little louder", Finisher::softClipper,
      { 0.5f, -0.5f, 0, 0.5f, 1.0f, { -14, -16, -18 }, 0.25f, 20, 200, { 1, 1, 1 }, 1.0f, 0, 3.0f },
      { 0.5f, -1.5f, 0, 1.0f, 2.5f, { -18, -20, -22 }, 0.45f, 20, 200, { 2, 2, 2 }, 2.0f, 0, 6.0f }, -2.9f },
    { "Loud", "As loud as it gets, still clean", Finisher::softClipper,
      { 1.0f, -1.0f, 0, 1.0f, 1.5f, { -16, -18, -20 }, 0.35f, 15, 150, { 1.5f, 1.5f, 1.5f }, 2.0f, 0, 5.0f },
      { 1.0f, -2.0f, 0, 1.5f, 3.0f, { -22, -24, -26 }, 0.60f, 10, 120, { 3, 3, 3 }, 4.0f, 0, 9.0f }, -3.2f },
    { "Clarity", "Brighter and cleaner, less mud", Finisher::enhancer,
      { -0.5f, -1.5f, 0, 1.5f, 2.0f, { -14, -16, -18 }, 0.20f, 20, 200, { 1, 1, 1 }, 0, -10, 3.0f },
      { -1.0f, -3.0f, 0, 3.0f, 4.0f, { -18, -20, -22 }, 0.35f, 20, 200, { 1.5f, 1.5f, 1.5f }, 0, -24, 6.5f }, -3.0f },
    { "Warm tape", "Round lows, soft highs, gentle saturation", Finisher::softClipper,
      { 1.5f, 0.5f, 0, -1.0f, -1.5f, { -14, -16, -18 }, 0.30f, 30, 300, { 1, 1, 1 }, 1.5f, 0, 3.0f },
      { 3.0f, 1.0f, 0, -2.0f, -3.0f, { -18, -20, -22 }, 0.50f, 30, 300, { 1.5f, 1.5f, 1.5f }, 4.0f, 0, 5.0f }, -1.9f },
    { "Gentle", "Light touch, keeps the dynamics", Finisher::enhancer,
      { 0.5f, -0.5f, 0, 0, 0.5f, { -12, -14, -16 }, 0.15f, 25, 250, { 0.5f, 0.5f, 0.5f }, 0, 0, 2.5f },
      { 1.0f, -1.0f, 0, 0.5f, 1.0f, { -16, -18, -20 }, 0.30f, 25, 250, { 1, 1, 1 }, 0, -6, 4.0f }, -1.8f },
    { "Punchy", "Tight lows and strong hits", Finisher::softClipper,
      { 2.0f, -1.0f, 0, 1.0f, 1.0f, { -14, -16, -18 }, 0.30f, 30, 120, { 1.5f, 1.5f, 1.5f }, 1.5f, 0, 4.0f },
      { 3.5f, -2.0f, 0, 2.0f, 1.5f, { -20, -22, -24 }, 0.55f, 30, 100, { 3, 3, 3 }, 3.5f, 0, 7.0f }, -2.8f },
    { "Wide", "A wider stereo image", Finisher::enhancer,
      { 0, -1.0f, 0, 0.5f, 1.5f, { -14, -16, -18 }, 0.20f, 20, 200, { 1, 1, 1 }, 0, -18, 3.5f },
      { 0, -2.0f, 0, 1.0f, 3.0f, { -18, -20, -22 }, 0.35f, 20, 200, { 1.5f, 1.5f, 1.5f }, 0, -36, 7.0f }, -2.9f },
    { "Cinematic", "Big, deep and open", Finisher::enhancer,
      { 1.5f, -1.0f, -0.5f, 0, 1.5f, { -14, -16, -18 }, 0.25f, 25, 400, { 1, 1, 1 }, 0, -14, 2.5f },
      { 3.0f, -2.0f, -1.0f, 0.5f, 3.0f, { -18, -20, -22 }, 0.45f, 25, 400, { 2, 2, 2 }, 0, -28, 6.0f }, -2.4f },
};
// clang-format on

constexpr float lightAt = 0.25f, heavyAt = 0.85f;

float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// the look at an intensity: neutral → Light over 0…0.25, Light → Heavy over 0.25…0.85, Heavy's trend beyond
Point at (const Look& l, float intensity) noexcept
{
    const Point n { 0, 0, 0, 0, 0, { 0, 0, 0 }, 0, l.light.attackMs, l.light.releaseMs, { 0, 0, 0 }, 0, 0, 0 };
    const Point* a = &n;
    const Point* b = &l.light;
    float t = 0;
    const auto i = std::clamp (intensity, 0.0f, 1.0f);
    if (i <= lightAt)
        t = i / lightAt;
    else
        a = &l.light, b = &l.heavy, t = (i - lightAt) / (heavyAt - lightAt);
    Point p;
    p.low = lerp (a->low, b->low, t);
    p.mud = lerp (a->mud, b->mud, t);
    p.mid = lerp (a->mid, b->mid, t);
    p.presence = lerp (a->presence, b->presence, t);
    p.air = lerp (a->air, b->air, t);
    for (std::size_t k = 0; k < 3; ++k)
    {
        p.threshold[k] = std::min (0.0f, lerp (a->threshold[k], b->threshold[k], t));
        p.makeup[k] = lerp (a->makeup[k], b->makeup[k], t);
    }
    p.amount = std::clamp (lerp (a->amount, b->amount, t), 0.0f, 1.0f);
    p.attackMs = lerp (a->attackMs, b->attackMs, t);
    p.releaseMs = lerp (a->releaseMs, b->releaseMs, t);
    p.drive = std::max (0.0f, lerp (a->drive, b->drive, t));
    p.separation = std::clamp (lerp (a->separation, b->separation, t), -96.0f, 96.0f);
    p.limiter = std::max (0.0f, lerp (a->limiter, b->limiter, t));
    return p;
}

int clampi (double v, int lo, int hi) noexcept { return std::clamp ((int) std::lround (v), lo, hi); }
double fromDb (double db) noexcept { return std::pow (10.0, db / 20.0); }

// ---- Fruity Parametric EQ 2 (FL's raw controls; REA_rodada50) ----
int peqHz (double hz) noexcept { return clampi (std::log (hz / 20.0) / std::log (1000.0) * 65536.0, 0, 65536); }
int peqDb (double db) noexcept { return clampi (db * 100.0, -1800, 1800); }

EffectSlot eqSlot (const Point& p, const MasterLookSettings& s)
{
    auto slot = makeEffectSlot (EffectKind::parametricEq);
    peqfl::Settings e;
    e.law = peqfl::Law::peq2;
    // FL types: 3 low cut, 5 low shelf, 6 peaking, 7 high shelf; FL's own widths for shelves / bells
    const std::array<int, 7> type { 3, 5, 6, 6, 6, 7, 7 };
    const std::array<double, 7> hz { 25.0, 110.0, 300.0, 1200.0, 4000.0, 10000.0, 6324.6 };
    const std::array<double, 7> db { 0.0, p.low + s.tone[0], p.mud, p.mid + s.tone[1], p.presence, p.air + s.tone[2], 0.0 };
    for (std::size_t b = 0; b < 7; ++b)
    {
        e.type[b] = type[b];
        e.freq[b] = peqHz (hz[b]);
        e.gain[b] = peqDb (db[b]);
        e.width[b] = type[b] == 6 ? 25412 : 40124; // bells: FL's default width; shelves and the low cut: FL's shelf width (Q ≈ 0.7)
        e.order[b] = 0;
    }
    e.main = peqDb (s.inputDb);
    e.processing = peqfl::Processing::hqAlternative; // what a new FL instance has (HQ, alternative design)
    e.mask = 0x7F;
    peqfl::toParams (e, slot.params);
    return slot;
}

// ---- Fruity Multiband Compressor (REA_rodada56 §2.2 laws) ----
EffectSlot multibandSlot (const Point& p, Finisher finisher)
{
    auto slot = makeEffectSlot (EffectKind::fruityMultiband);
    auto c = flmb::controlDefault;
    auto gainU = [] (double db, double base) { return std::log (fromDb (db) + 1.0) / std::log (base); }; // base^u − 1 = gain
    auto xover = [] (double hz) { const auto s = std::log (hz - 19.0) / std::log (19981.0); return 20.0 + s * s * 19980.0; };
    c[0] = clampi (gainU (finisher == Finisher::softClipper ? p.drive : 0.0, 4.0) * 65536.0, 0, 65536);
    c[1] = 0; // FMC's own limiter off: the Limiter ends the chain
    c[2] = 0; // IIR crossovers (no latency)
    c[4] = c[12] = clampi (xover (4000.0), 20, 20000);
    c[13] = c[21] = clampi (xover (200.0), 20, 20000);
    const int band[3][7] { { 20, 22, 23, 24, 25, 26, 27 }, { 11, 14, 15, 16, 17, 18, 19 }, { 3, 5, 6, 7, 8, 9, 10 } }; // low, mid, high
    for (std::size_t k = 0; k < 3; ++k)
    {
        const auto* i = band[k];
        c[(std::size_t) i[0]] = 0; // compress
        c[(std::size_t) i[1]] = clampi (gainU (p.makeup[k], 8.0) * 65536.0, 0, 65536);
        c[(std::size_t) i[2]] = clampi ((p.threshold[k] + 60.0) / 60.0 * 65536.0, 0, 65536);
        c[(std::size_t) i[3]] = clampi (p.amount * 65536.0, 0, 65536);
        c[(std::size_t) i[4]] = clampi (1.0 + (p.attackMs / 1000.0 - 1.0 / 17000.0) / 0.1 * 999.0, 1, 1000);
        c[(std::size_t) i[5]] = clampi (100.0 + (p.releaseMs / 1000.0 - 0.01) / 0.99 * 9900.0, 100, 10000);
        c[(std::size_t) i[6]] = 52429; // FL's knee
    }
    flmb::State st;
    st.c = c;
    flmb::toParams (st, slot.params.data());
    return slot;
}

EffectSlot finisherSlot (const Point& p, Finisher finisher)
{
    if (finisher == Finisher::enhancer)
    {
        auto slot = makeEffectSlot (EffectKind::stereoEnhancer);
        stereofx::StereoEnhancerState st;
        st.separation = clampi (p.separation, -96, 96);
        stereofx::stereoEnhancerToParams (st, slot.params.data());
        return slot;
    }
    auto slot = makeEffectSlot (EffectKind::fruitySoftClipper);
    flshape::SoftClipperState st;
    st.threshold = clampi (128.0 * fromDb (-1.0), 1, 127); // clips softly from −1 dBFS; the drive sets how much
    st.post = 128;                                          // unity
    flshape::softClipperToParams (st, slot.params.data());
    return slot;
}

EffectSlot limiterSlot (const Point& p, double ceilingDb)
{
    auto slot = makeEffectSlot (EffectKind::fruityLimiter);
    // gain law (7^x − 1) / 6, ceiling law (3^x − 1) / 2, x = raw / 1000 (REA_rodada48)
    const auto gain = clampi (1000.0 * std::log (6.0 * fromDb (p.limiter) + 1.0) / std::log (7.0), 0, 2000);
    const auto ceiling = clampi (1000.0 * std::log (2.0 * fromDb (ceilingDb) + 1.0) / std::log (3.0), 0, 2000);
    slot.params[0] = fldyn::normalised (gain, fldyn::limiterMin[0], fldyn::limiterMax[0]);
    slot.params[2] = fldyn::normalised (ceiling, fldyn::limiterMin[2], fldyn::limiterMax[2]);
    slot.params[16] = 0.0f;                                     // gate threshold as a new FL instance has it (gate off)
    slot.params[(std::size_t) fldyn::limiterControls] = 1.0f; // ceiling exactly
    return slot;
}

// the true-peak stage (round 190ea): LYNX's Apex Limiter at 0 dB gain, True peak on (its guard holds the output's true
// peak at its ceiling within a few hundredths of a dB on a 16× reference meter, round 190co), its ceiling 0.05 dB under
// trueCeilingDb so the stated ceiling holds on that meter too (measured worst −0.98 dBTP at −1.00, −1.00 at −1.05); the other controls at a new instance's values (1 ms
// look-ahead, Clean, 150 ms release, no oversampling, no dither). Below the ceiling it passes the signal unchanged
// (delayed by its constant latency).
EffectSlot truePeakSlotOf()
{
    auto slot = makeEffectSlot (EffectKind::apexLimiter);
    slot.params[(std::size_t) apex::pGain] = 0.0f;
    slot.params[(std::size_t) apex::pCeiling] = (30.0f + trueCeilingDb - truePeakMarginDb) / 30.0f; // (−30 … 0 dB)
    slot.params[(std::size_t) apex::pTruePeak] = 1.0f;
    return slot;
}

std::vector<int> lookSlots (const Song& song)
{
    std::vector<int> out;
    if (song.mixer.empty())
        return out;
    const auto& fx = song.mixer[0].effects;
    for (int i = 0; i < (int) fx.size(); ++i)
        if (fx[(std::size_t) i].look != 0)
            out.push_back (i);
    return out;
}

// the params a slot must hold to still be the look's (enabled, mix and the slot's row may change freely)
bool sameSettings (const EffectSlot& a, const EffectSlot& b) noexcept
{
    if (a.kind != b.kind)
        return false;
    for (std::size_t i = 0; i < a.params.size(); ++i)
        if (std::abs (a.params[i] - b.params[i]) > 1.0e-4f)
            return false;
    return true;
}
} // namespace

EffectSlot truePeakStage() { return truePeakSlotOf(); }

const char* name (int id) noexcept { return id >= 1 && id <= count ? table[id - 1].name : ""; }
const char* description (int id) noexcept { return id >= 1 && id <= count ? table[id - 1].description : ""; }

std::vector<EffectSlot> chain (const MasterLookSettings& s)
{
    if (s.id < 1 || s.id > count)
        return {};
    const auto& l = table[s.id - 1];
    const auto p = at (l, s.intensity);
    std::vector<EffectSlot> out { eqSlot (p, s), multibandSlot (p, l.finisher), finisherSlot (p, l.finisher), limiterSlot (p, l.ceilingDb),
                                  truePeakStage() };
    for (auto& slot : out)
        slot.look = (std::uint8_t) s.id;
    return out;
}

bool apply (Song& song, const MasterLookSettings& s)
{
    if (song.mixer.empty())
        return false;
    auto slots = chain (s);
    if (slots.empty())
    {
        remove (song);
        return true;
    }
    auto& fx = song.mixer[0].effects;
    const auto existing = lookSlots (song);
    if (existing.size() == slots.size())
    {
        // replace in place: same rows, same on/off (A/B state) and mix
        for (std::size_t k = 0; k < slots.size(); ++k)
        {
            auto& old = fx[(std::size_t) existing[k]];
            slots[k].row = old.row;
            slots[k].enabled = old.enabled;
            old = std::move (slots[k]);
        }
        song.masterLook = s;
        return true;
    }
    if (existing.size() + 1 == slots.size() && (int) existing.size() == legacySlots)
    {
        // a look applied before round 190ea (four slots, no true-peak stage): replaced in place like above, and the
        // true-peak slot goes in the row right after its limiter; effects after the look move down a row only where
        // they must (their order stays). Else (no room) the fresh chain below.
        const auto last = existing.back();
        const auto rows = mixerops::effectRows (fx);
        const auto row = rows[(std::size_t) last] + 1;
        std::vector<int> after; // the new rows of the effects after the look
        for (auto prev = row, i = last + 1; i < (int) fx.size(); ++i)
            after.push_back (prev = std::max (rows[(std::size_t) i], prev + 1));
        const auto lastRow = after.empty() ? row : after.back();
        if (existing.back() - existing.front() == legacySlots - 1 && (int) fx.size() < maxEffectSlots && lastRow < maxEffectSlots)
        {
            for (std::size_t k = 0; k < after.size(); ++k)
                if (auto& e = fx[(std::size_t) last + 1 + k]; rows[(std::size_t) last + 1 + k] != after[k])
                    e.row = after[k];
            const bool on = fx[(std::size_t) last].enabled;
            for (std::size_t k = 0; k < existing.size(); ++k)
            {
                auto& old = fx[(std::size_t) existing[k]];
                slots[k].row = old.row;
                slots[k].enabled = old.enabled;
                old = std::move (slots[k]);
            }
            auto& tp = slots.back();
            tp.row = row;
            tp.enabled = on;
            fx.insert (fx.begin() + last + 1, std::move (tp));
            song.masterLook = s;
            return true;
        }
    }
    // a fresh chain after the Master's last effect (an incomplete old one is taken off first; "Hear without" stays)
    const bool wasBypassed = bypassed (song);
    std::vector<EffectSlot> kept;
    for (auto& e : fx)
        if (e.look == 0)
            kept.push_back (e);
    int nextRow = 0;
    for (const auto& e : kept)
        nextRow = std::max (nextRow, (e.row >= 0 ? e.row : nextRow) + 1);
    if ((int) kept.size() + chainSlots > maxEffectSlots || nextRow + chainSlots > maxEffectSlots)
        return false;
    for (auto& slot : slots)
    {
        slot.row = nextRow++;
        slot.enabled = ! wasBypassed;
        kept.push_back (std::move (slot));
    }
    fx = std::move (kept);
    song.masterLook = s;
    return true;
}

void remove (Song& song)
{
    if (! song.mixer.empty())
    {
        auto& fx = song.mixer[0].effects;
        fx.erase (std::remove_if (fx.begin(), fx.end(), [] (const EffectSlot& e) { return e.look != 0; }), fx.end());
    }
    song.masterLook = {};
}

bool present (const Song& song) { return ! lookSlots (song).empty(); }

bool custom (const Song& song)
{
    const auto idx = lookSlots (song);
    if (idx.empty())
        return false;
    auto want = chain (song.masterLook);
    if ((int) idx.size() == legacySlots && want.size() == (std::size_t) chainSlots)
        want.resize ((std::size_t) legacySlots); // (a look saved before round 190ea: its four slots are the chain's first four)
    if (want.size() != idx.size())
        return true;
    for (std::size_t k = 0; k < idx.size(); ++k)
        if (! sameSettings (song.mixer[0].effects[(std::size_t) idx[k]], want[k]))
            return true;
    return false;
}

bool bypassed (const Song& song)
{
    const auto idx = lookSlots (song);
    if (idx.empty())
        return false;
    for (const auto i : idx)
        if (song.mixer[0].effects[(std::size_t) i].enabled)
            return false;
    return true;
}

void setBypassed (Song& song, bool off)
{
    for (const auto i : lookSlots (song))
        song.mixer[0].effects[(std::size_t) i].enabled = ! off;
}

float autoInputGainDb (double inputLufs) noexcept
{
    if (! std::isfinite (inputLufs) || inputLufs < -70.0)
        return 0.0f;
    return (float) std::clamp (referenceLufs - inputLufs, -18.0, 18.0);
}
} // namespace lynx::looks
