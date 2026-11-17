// Round 190kw (Timeless 3 translator): FabFilter Timeless 3 states on Echoform (/mnt/e/lynx-work/rea190kw/tl3).
// - Echoform gets a hidden line law (past the info's parameters, no control, no text; saved while non-zero): the clean
//   digital line measured on refcap030's Timeless 3 captures (its dry / wet law, a short linear-phase read filter, a wide
//   soft limit, whole-sample delays). Only a translated Timeless 3 state sets it; every project saved before keeps 0.
// - The [fabfilter-timeless] substitute plays Echoform (was Fruity Delay 3 at FL's default) and translates the FFBS state.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/EchoSuite.h"
#include "core/Effects.h"
#include "core/FabFilterStates.h"
#include "core/PluginSubstitutes.h"

namespace parity190kwtl3
{
// a Timeless 3 component state: FFBS v1, 1007 values at the reference's defaults (350 ms, feedback 0, mix 50 %, width
// 100 %, filters unused at 75.6 kHz / Clean / 12 dB/oct, wet level 1), then the given values
inline std::vector<float> defaults()
{
    std::vector<float> v (1007, 0.0f);
    v[0] = 0.29334f;
    for (std::size_t k = 0; k < 15; ++k)
    {
        v[8 + 5 * k] = 1.0f;
        v[9 + 5 * k] = 0.5f;
    }
    v[82] = v[83] = 1.0f;
    for (std::size_t k = 0; k < 6; ++k)
    {
        v[90 + 9 * k] = 16.2064f;
        v[94 + 9 * k] = 10.0f;
        v[96 + 9 * k] = v[97 + 9 * k] = 1.0f;
    }
    v[157] = v[158] = 1.0f;
    return v;
}
inline std::vector<std::uint8_t> ffbs (const std::vector<float>& values)
{
    std::vector<std::uint8_t> b { 'F', 'F', 'B', 'S' };
    auto put = [&b] (std::uint32_t u) {
        for (int i = 0; i < 4; ++i)
            b.push_back ((std::uint8_t) (u >> (8 * i)));
    };
    put (1);
    put ((std::uint32_t) values.size());
    for (const auto v : values)
    {
        std::uint32_t u;
        std::memcpy (&u, &v, 4);
        put (u);
    }
    return b;
}
inline lynx::EffectSlot substitute (const std::vector<std::uint8_t>& state, int& translated)
{
    lynx::PluginIdentity id;
    id.format = lynx::ForeignPlugin::Format::vst3;
    id.name = "FabFilter Timeless 3";
    id.state = state;
    const auto* entry = lynx::findPluginSubstitute (id, false);
    CHECK (entry != nullptr);
    translated = 0;
    return entry != nullptr ? lynx::makeSubstituteSlot (*entry, &id, &translated) : lynx::EffectSlot {};
}
inline float at (const lynx::EffectSlot& s, int k) { return s.params[(std::size_t) k]; }
// the left channel's impulse response (an impulse of `amp` on both sides at sample 0)
inline std::vector<float> impulse (const lynx::EffectSlot& s, double sr, int n, float amp = 0.5f)
{
    auto fx = lynx::makeEffect (s);
    fx->prepare (sr);
    fx->reset();
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[0] = r[0] = amp;
    for (int off = 0; off < n; off += 512)
        fx->process (l.data() + off, r.data() + off, std::min (512, n - off), s.params, 120.0);
    return l;
}
} // namespace parity190kwtl3

TEST (parity190kw_echoform_line_law_is_hidden_off_by_default_and_reset_on_older_projects)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto& info = effectInfo (EffectKind::echoform);
    CHECK (info.params.size() == (std::size_t) e::paramCount); // no new control or string
    CHECK (e::lineLaw == e::revision + 1 && e::lineLaw < maxEffectParams);
    auto s = makeEffectSlot (EffectKind::echoform);
    CHECK (s.params[(std::size_t) e::lineLaw] == 0.0f);
    CHECK (savedEffectParamCount (s) <= (std::size_t) e::revision + 1); // nothing new saved for a plain Echoform
    s.params[(std::size_t) e::lineLaw] = 1.0f;
    CHECK (savedEffectParamCount (s) == (std::size_t) e::lineLaw + 1);
    // a project saved before (its parameters end before the line law) loads without it
    auto old = s;
    upgradeLoadedEffectParams (old, (std::size_t) e::revision + 1);
    CHECK (old.params[(std::size_t) e::lineLaw] == 0.0f);
    auto kept = s;
    upgradeLoadedEffectParams (kept, (std::size_t) e::lineLaw + 1);
    CHECK (kept.params[(std::size_t) e::lineLaw] == 1.0f);
}

TEST (parity190kw_timeless3_delay_time_law)
{
    using lynx::fabfilter::timeless3DelayMs;
    // the reference's own points (display texts and echo positions, refcap030)
    CHECK (std::abs (timeless3DelayMs (0.0f) - 5.0) < 1e-6);
    CHECK (std::abs (timeless3DelayMs (0.2f) - 174.8125) < 0.01);
    CHECK (std::abs (timeless3DelayMs (0.247028f) - 249.979) < 0.01);
    CHECK (std::abs (timeless3DelayMs (0.29334f) - 350.0) < 0.01);
    CHECK (std::abs (timeless3DelayMs (0.361589f) - 500.0) < 0.01);
    CHECK (std::abs (timeless3DelayMs (0.5f) - 804.19) < 0.01);
    CHECK (std::abs (timeless3DelayMs (0.8f) - 3176.81) < 0.01);
    CHECK (std::abs (timeless3DelayMs (1.0f) - 5000.0) < 1e-3);
    double last = 0.0;
    for (int i = 0; i <= 1000; ++i) // monotone in between
    {
        const auto ms = timeless3DelayMs ((float) i / 1000.0f);
        CHECK (ms >= last);
        last = ms;
    }
}

TEST (parity190kw_timeless3_state_is_translated_onto_echoform)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    using namespace parity190kwtl3;
    int n = 0;
    // S3 of refcap030: 500 ms, feedback 60 %, width 0 %, mix 40 %
    auto v = defaults();
    v[0] = 0.361589f;
    v[85] = 0.6f;
    v[157] = 0.0f;
    v[160] = -0.2f;
    const auto s = substitute (ffbs (v), n);
    CHECK (s.kind == EffectKind::echoform && n > 0);
    CHECK (e::modeOf (at (s, e::mode)) == e::Mode::clean && e::styleOf (at (s, e::style)) == e::Style::single);
    CHECK (std::abs (e::timeMs (at (s, e::timeL)) - 500.0) < 0.05 && std::abs (e::timeMs (at (s, e::timeR)) - 500.0) < 0.05);
    CHECK (e::syncKind (at (s, e::syncL)) == 0);
    CHECK (std::abs (e::feedbackGain (at (s, e::feedback)) - 0.6) < 1e-6); // the loop gain is the feedback
    CHECK (std::abs (at (s, e::mix) - 0.4f) < 1e-6f);
    CHECK (std::abs (at (s, e::width) - 0.5f) < 1e-6f);                    // 0 % = mono echoes
    CHECK (at (s, e::lineLaw) == 1.0f && at (s, e::age) == 0.0f && at (s, e::modDepth) == 0.0f && at (s, e::drive) == 0.0f);
    CHECK (std::abs (e::lowCutHz (at (s, e::lowCut)) - 15.5) < 0.01 && at (s, e::highCut) == 1.0f);
    // ping pong L-R / R-L: the feedback per hop (Echoform's ping-pong loops √gain per side)
    v = defaults();
    v[6] = 2.0f;
    v[85] = 0.4f;
    const auto p = substitute (ffbs (v), n);
    CHECK (e::styleOf (at (p, e::style)) == e::Style::pingPong && at (p, e::startSide) == 1.0f && at (p, e::pairs) == 0.0f);
    CHECK (std::abs (std::sqrt (e::feedbackGain (at (p, e::feedback))) - 0.4) < 1e-6);
    // a filter in use: low pass 2 kHz (shape 0) -> the high cut, high pass 120 Hz (shape 1) -> the low cut; unused ones
    // (every capture's, whatever their frequency) change nothing
    v = defaults();
    v[90] = (float) std::log2 (2000.0);
    v[90 + 8] = 1.0f;
    v[99] = (float) std::log2 (120.0);
    v[99 + 5] = 1.0f;
    v[99 + 8] = 1.0f;
    v[108] = (float) std::log2 (300.0); // filter 3 unused
    v[108 + 5] = 1.0f;
    const auto f = substitute (ffbs (v), n);
    CHECK (std::abs (e::lowCutHz (at (f, e::lowCut)) - 120.0) < 0.5);
    const auto hc = at (f, e::highCut);
    CHECK (hc < 0.8f && std::abs (e::highCutHz (hc) * 0.5 - 2000.0) < 15.0); // the measured high cut: a one-pole at half its label
    // synced to 1/8 note (sync index 3), offset 100 %
    v = defaults();
    v[3] = 3.0f;
    const auto y = substitute (ffbs (v), n);
    CHECK (e::syncKind (at (y, e::syncL)) == 1 && e::noteIndex (at (y, e::noteL)) == 3 && std::abs (e::sideMs (y.params, false, 120.0) - 250.0) < 1e-6);
}

TEST (parity190kw_timeless3_unknown_layouts_keep_the_preset)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    using namespace parity190kwtl3;
    int n = 0;
    auto v = defaults();
    v.resize (1006); // another layout (Timeless 2, another version): not translated
    v[0] = 0.8f;
    const auto s = substitute (ffbs (v), n);
    CHECK (s.kind == EffectKind::echoform && n == 0);
    // the entry's preset: the reference's default, a single 350 ms echo, no feedback, mix 50 %, the line law
    CHECK (std::abs (e::timeMs (at (s, e::timeL)) - 350.0) < 0.01 && e::feedbackGain (at (s, e::feedback)) == 0.0);
    CHECK (std::abs (at (s, e::mix) - 0.5f) < 1e-6f && at (s, e::lineLaw) == 1.0f && e::modeOf (at (s, e::mode)) == e::Mode::clean);
    // and a direct call on the wrong kind or layout changes nothing
    const auto st = fabfilter::parseState (ffbs (v));
    CHECK (st.has_value());
    auto slot = makeEffectSlot (EffectKind::echoform);
    const auto before = slot.params;
    CHECK (fabfilter::fromTimeless3 (*st, slot) == 0 && slot.params == before);
    const auto ok = fabfilter::parseState (ffbs (defaults()));
    auto delay3 = makeEffectSlot (EffectKind::fruityDelay3);
    const auto d3 = delay3.params;
    CHECK (fabfilter::fromTimeless3 (*ok, delay3) == 0 && delay3.params == d3);
}

TEST (parity190kw_line_law_echo_dry_at_unity_whole_samples_and_the_feedback_per_pass)
{
    using namespace lynx;
    using namespace parity190kwtl3;
    int n = 0;
    // S1 of refcap030: 250 ms (0.247028 = 249.979 ms: 11999 samples at 48 kHz), feedback 50 %, mix 50 %
    auto v = defaults();
    v[0] = 0.247028f;
    v[85] = 0.5f;
    const auto s = substitute (ffbs (v), n);
    const auto h = impulse (s, 48000.0, 48000);
    CHECK (std::abs (h[0] - 0.5f) < 2e-3f); // the dry at unity (the read filter's taps reach it only through the echo)
    const auto energy = [&h] (int c) {
        double e = 0.0;
        for (int i = c - 40; i <= c + 40; ++i)
            e += (double) h[(std::size_t) i] * h[(std::size_t) i];
        return std::sqrt (e) / 0.5;
    };
    int peak = 1000;
    for (int i = 1000; i < 20000; ++i)
        if (std::abs (h[(std::size_t) i]) > std::abs (h[(std::size_t) peak]))
            peak = i;
    CHECK (peak == 11999);
    CHECK (std::abs (h[(std::size_t) peak] / 0.5f - 0.916f) < 0.01f); // the reference's first echo: 0.9159 of the impulse
    CHECK (std::abs (energy (11999) - 0.929) < 0.01);                 // … and its energy 0.9287 (the read filter)
    CHECK (std::abs (20.0 * std::log10 (energy (2 * 11999) / energy (11999)) - 20.0 * std::log10 (0.5 * 0.9618)) < 0.3);
}
