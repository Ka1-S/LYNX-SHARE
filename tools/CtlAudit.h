#pragma once

// "Every control changes the render" (round 170i3): for one FL-native generator channel, each control the LYNX editor
// shows is flipped to the far end of its range and a short note pattern is rendered through the same three paths the
// app uses:
//   state — a new instrument built from the edited channel (a project saved and opened again)
//   live  — the edited channel reaches the running instrument through RackState::build (previous) → configure, as an
//           editor knob's ChannelRef::edit does while notes play
//   auto  — Instrument::setParameter (index, 0…1) on the running instrument, as an automation clip / MIDI link does
// A control "moves" a path when that path's render differs from the unedited one by more than −100 dB (relative RMS).
// Used by tools/ctlaudit (the survey) and by tests/CtlAudit170i3Tests.inc (the permanent checks).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <optional>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/EngineParams.h"
#include "core/FlKeys.h"
#include "core/Harmor.h"
#include "core/SytrusEngine.h"
#include "core/ThreeOsc.h"
#include "core/ModelParams.h"
#include "core/NativeEngines.h"
#include "core/RackRenderer.h"
#include "core/Song.h"

namespace lynx::ctlaudit
{
// one editor control of a channel: how to read it and write it as the editor does (raw value, the control's range)
// FL's installation for the controls that load files (natives::followControl), set by the caller; nullptr = none
inline const natives::FlAccess* flAccess = nullptr;

struct Control
{
    int index = 0;            // the engine's parameter number (automation target param), −1 = not automatable
    std::string name;
    float low = 0.0f, high = 1.0f;
    std::function<std::optional<float> (const ChannelDef&)> get;
    std::function<bool (ChannelDef&, float)> set;
    bool automatable = true; // (the engines without a parameter list take no channelParam automation)
};

// the listed parameters (core/ModelParams.h) of the channel's engine, as the designed editor builds its knobs
inline std::vector<Control> modelControls (const ChannelDef& c)
{
    std::vector<Control> out;
    const auto* table = modelparams::tableOf (c.instrument);
    if (table == nullptr || table->params == nullptr)
        return out;
    for (const auto& p : *table->params)
    {
        Control k;
        k.index = p.index, k.name = p.name, k.low = p.low, k.high = p.high;
        const auto index = p.index;
        k.get = [index] (const ChannelDef& d) { return modelparams::rawValue (d, index); };
        k.set = [index] (ChannelDef& d, float raw) {
            const auto ok = modelparams::setRawValue (d, index, raw);
            if (ok && flAccess != nullptr)
                natives::followControl (d, index, *flAccess); // (as the editor's knob does)
            return ok;
        };
        out.push_back (std::move (k));
    }
    return out;
}

// the knobs of the engines with their own (unlisted) controls, exactly as src/ui/ChannelSettings.cpp's ModelPanel
// builds them: 3x Osc, FL Keys, Sytrus's main settings, Harmor's part A knobs, BooBass's three knobs
inline std::vector<Control> ownControls (const ChannelDef& c)
{
    std::vector<Control> out;
    auto add = [&out] (int index, std::string name, float low, float high, auto get, auto set) {
        Control k;
        k.index = index, k.name = std::move (name), k.low = low, k.high = high, k.get = get, k.set = set, k.automatable = false;
        out.push_back (std::move (k));
    };
    switch (c.instrument)
    {
        case InstrumentKind::threeosc:
        {
            static const char* const names[] = {
                "Osc 1 pan", "Osc 1 shape", "Osc 1 coarse", "Osc 1 fine", "Osc 1 phase", "Osc 1 detune", "Osc 2 mix", "Osc 2 pan",
                "Osc 2 shape", "Osc 2 coarse", "Osc 2 fine", "Osc 2 phase", "Osc 2 detune", "Osc 3 mix", "Osc 3 pan", "Osc 3 shape",
                "Osc 3 coarse", "Osc 3 fine", "Osc 3 phase", "Osc 3 detune", "Phase rand", "Invert 1", "Invert 2", "Invert 3",
                "Osc 3 AM", "HQ" };
            for (int i = 0; i < threeOscParamCount(); ++i)
            {
                const auto [low, high] = threeOscParamRange (i);
                add (i, i < 26 ? names[i] : "Param " + std::to_string (i), (float) low, (float) high,
                     [i] (const ChannelDef& d) -> std::optional<float> {
                         const auto v = threeOscParam (d.pluginState, i);
                         return v ? std::optional<float> ((float) *v) : std::nullopt;
                     },
                     [i] (ChannelDef& d, float v) { return threeOscSetParam (d.pluginState, i, (int) std::lround (v)); });
            }
            break;
        }
        case InstrumentKind::flkeys:
            for (int i = 0; i < 15; ++i)
                add (i, flkeys::knobRange[i].name, (float) flkeys::knobRange[i].min, (float) flkeys::knobRange[i].max,
                     [i] (const ChannelDef& d) -> std::optional<float> {
                         const auto st = flkeys::parseState (d.pluginState);
                         return st ? std::optional<float> ((float) st->knobs[(std::size_t) i]) : std::nullopt;
                     },
                     [i] (ChannelDef& d, float v) {
                         auto st = flkeys::parseState (d.pluginState);
                         if (! st)
                             return false;
                         st->knobs[(std::size_t) i] = (int) std::lround (v);
                         d.pluginState = flkeys::writeState (*st);
                         return true;
                     });
            break;
        case InstrumentKind::sytrus:
            for (int i = 0; i < (int) sytrus::settings().size(); ++i)
            {
                const auto& st = sytrus::settings()[(std::size_t) i];
                add (i, st.name, (float) st.low, (float) st.high,
                     [i] (const ChannelDef& d) -> std::optional<float> {
                         const auto v = sytrus::settingValue (d.pluginState, i);
                         return v ? std::optional<float> ((float) *v) : std::nullopt;
                     },
                     [i] (ChannelDef& d, float v) { return sytrus::setSetting (d.pluginState, i, (int) std::lround (v)); });
            }
            break;
        case InstrumentKind::harmor:
        {
            struct K { int part, index; const char* name; float low, high; };
            static const K knobs[] = {
                { -1, 12, "Master vol", 0, 2000 }, { -1, 10, "A/B mix", 0, 1000 }, { 0, 11, "A volume", -1000, 1000 },
                { 0, 4, "Timbre mix", 0, 1000 }, { 0, 52, "Cutoff", 0, 8192 }, { 0, 57, "Resonance", 0, 1000 },
                { 0, 75, "Pluck", 0, 1000 }, { 0, 34, "Unison", 1, 9 }, { 0, 38, "Uni detune", 0, 256 },
                { 0, 27, "Harmonizer", 0, 1000 }, { 0, 76, "Phaser mix", 0, 1000 }, { 0, 23, "Harm. blur", 0, 10000 },
                { 0, 45, "Vibrato", 0, 1000 }, { 0, 17, "Tremolo", 0, 1000 } };
            for (const auto& k : knobs)
                add (k.index, k.name, k.low, k.high,
                     [k] (const ChannelDef& d) -> std::optional<float> {
                         const auto at = harmorParamOffset (d.pluginState, k.part, k.index);
                         if (at < 0)
                             return std::nullopt;
                         std::int32_t v = 0;
                         std::memcpy (&v, d.pluginState.data() + at, 4);
                         return (float) v;
                     },
                     [k] (ChannelDef& d, float v) {
                         const auto at = harmorParamOffset (d.pluginState, k.part, k.index);
                         if (at < 0)
                             return false;
                         const auto x = (std::int32_t) std::lround (v);
                         std::memcpy (d.pluginState.data() + at, &x, 4);
                         return true;
                     });
            break;
        }
        case InstrumentKind::boobass:
            for (const auto& [i, name] : { std::pair<int, const char*> { 0, "Bass" }, { 1, "Mid" }, { 2, "Treble" } })
                add (i, name, 0.0f, 65535.0f,
                     [i] (const ChannelDef& d) -> std::optional<float> {
                         return (std::size_t) i < d.modelParams.size() ? std::optional<float> (d.modelParams[(std::size_t) i]) : std::nullopt;
                     },
                     [i] (ChannelDef& d, float v) {
                         if ((std::size_t) i >= d.modelParams.size())
                             return false;
                         d.modelParams[(std::size_t) i] = v;
                         return true;
                     });
            break;
        default:
            break;
    }
    return out;
}

// what the editor shows for this channel: the parameter list when the engine has one (and can read the state), its
// own knobs otherwise
inline std::vector<Control> editorControls (const ChannelDef& c)
{
    const auto* table = modelparams::tableOf (c.instrument);
    if (table != nullptr && table->params != nullptr && ! table->params->empty() && modelparams::rawValue (c, table->params->front().index))
        return modelControls (c);
    return ownControls (c);
}

struct Pattern
{
    std::vector<int> keys { 48, 60, 64, 67 };
    std::vector<int> keys2 { 50, 62, 65, 69 }; // a second chord after the edit (most engines read a control at note on)
    int secondBlock = 16;
    float velocity = 0.8f;
    double rate = 44100.0;
    int heldBlocks = 40;     // 512-sample blocks with the keys down (0.46 s)
    int releaseBlocks = 30;  // then released (0.35 s)
    int editBlock = 8;       // the live / automation edit lands here (0.09 s in)
    double bpm = 120.0;
};

// the pattern for an engine: drum engines play their pads
inline Pattern patternFor (InstrumentKind kind)
{
    Pattern p;
    if (kind == InstrumentKind::drumaxx)
        p.keys = p.keys2 = { 36, 37, 38, 42, 43, 44, 45, 49 }; // (FL's Default Kit's pads, hit again after the edit)
    else if (kind == InstrumentKind::drumpad || kind == InstrumentKind::bassdrum || kind == InstrumentKind::fruitkick)
        p.keys = { 60 }, p.keys2 = { 60 };
    return p;
}

using Samples = std::function<std::shared_ptr<const SampleData> (const std::string&)>;

struct Player
{
    Song song;
    std::unique_ptr<const RackState> state;
    Samples samples;
    double rate;
    Player (const ChannelDef& c, const Pattern& p, const Samples& s) : samples (s), rate (p.rate)
    {
        song.mixer = Song::defaultMixer (2);
        song.bpm = p.bpm;
        auto ch = c;
        ch.mixerTrack = 1;
        song.channels.push_back (ch);
        song.patterns.emplace_back();
        state = RackState::build (song, nullptr, rate, samples);
    }
    Instrument* inst() const { return state != nullptr && ! state->instruments.empty() ? state->instruments[0].get() : nullptr; }
    void rebuild (const ChannelDef& c)
    {
        auto next = song;
        auto ch = c;
        ch.mixerTrack = 1;
        next.channels[0] = ch;
        auto s = RackState::build (next, state.get(), rate, samples);
        song = std::move (next);
        state = std::move (s);
    }
    void block (std::vector<float>& out)
    {
        std::vector<float> l (512, 0.0f), r (512, 0.0f);
        if (auto* i = inst())
            i->render (l.data(), r.data(), 512, 1.0f, 1.0f);
        for (int k = 0; k < 512; ++k)
            out.push_back (l[(std::size_t) k]), out.push_back (r[(std::size_t) k]);
    }
};

enum class Path { state, live, automation };

// the pattern's render; `edit` (when set) runs at the edit block on the playing player
inline std::vector<float> render (const ChannelDef& c, const Pattern& p, const Samples& s, const std::function<void (Player&)>& edit = {})
{
    Player pl (c, p, s);
    std::vector<float> out;
    auto* i = pl.inst();
    if (i == nullptr)
        return out;
    for (const auto k : p.keys)
        i->noteOn (k, p.velocity);
    for (int b = 0; b < p.heldBlocks + p.releaseBlocks; ++b)
    {
        if (b == p.editBlock && edit)
            edit (pl);
        if (b == p.secondBlock)
            if (auto* now = pl.inst())
                for (const auto k : p.keys2)
                    now->noteOn (k, p.velocity);
        if (b == p.heldBlocks)
            if (auto* now = pl.inst())
            {
                for (const auto k : p.keys)
                    now->noteOff (k);
                for (const auto k : p.keys2)
                    if (std::find (p.keys.begin(), p.keys.end(), k) == p.keys.end())
                        now->noteOff (k);
            }
        pl.block (out);
    }
    return out;
}

// relative RMS difference in dB (−300 = identical, 0 = as loud as the reference)
inline double diffDb (const std::vector<float>& a, const std::vector<float>& b)
{
    double e = 0.0, n = 0.0;
    const auto len = std::min (a.size(), b.size());
    for (std::size_t i = 0; i < len; ++i)
    {
        const double d = (double) a[i] - (double) b[i];
        e += d * d;
        n += (double) b[i] * (double) b[i];
    }
    if (a.size() != b.size())
        return 0.0;
    if (e == 0.0)
        return -300.0;
    return 10.0 * std::log10 (e / std::max (n, 1e-30));
}

inline double peak (const std::vector<float>& a)
{
    double p = 0.0;
    for (const auto v : a)
        p = std::max (p, (double) std::abs (v));
    return p;
}

// the far end of the control's range from its current value
inline float flipped (const Control& k, float v)
{
    return v - k.low > 0.5f * (k.high - k.low) ? k.low : k.high;
}

struct Result
{
    std::string name;
    int index = 0;
    float from = 0.0f, to = 0.0f;
    double state = -300.0, live = -300.0, automation = -300.0; // dB vs the unedited render (−300: no change)
    bool rebuilt = false; // the live edit replaced the instrument (its sounding notes end) instead of configuring it
    bool automatable = true;
};

struct Report
{
    double basePeak = 0.0;
    double noise = -300.0; // the unedited pattern rendered twice (random-phase engines are not deterministic)
    std::vector<Result> results;
};

constexpr double movedDb = -100.0;

// every control of `controls` on channel `c` (prepare `c` first: sections on, a send, …)
inline Report audit (const ChannelDef& c, const std::vector<Control>& controls, const Pattern& p, const Samples& s, bool live = true,
                     bool automation = true)
{
    Report rep;
    const auto base = render (c, p, s);
    rep.basePeak = peak (base);
    rep.noise = diffDb (render (c, p, s), base);
    const auto liveBase = render (c, p, s, [&c] (Player& pl) { pl.rebuild (c); });
    for (const auto& k : controls)
    {
        Result r;
        r.name = k.name, r.index = k.index;
        const auto v = k.get (c);
        if (! v)
            continue;
        r.from = *v;
        r.to = flipped (k, *v);
        auto changed = c;
        if (! k.set (changed, r.to))
            continue;
        r.state = diffDb (render (changed, p, s), base);
        if (live)
            r.live = diffDb (render (c, p, s, [&changed, &r] (Player& pl) {
                                 const auto* before = pl.inst();
                                 pl.rebuild (changed);
                                 r.rebuilt = pl.inst() != before;
                             }),
                             liveBase);
        r.automatable = automation && k.automatable && k.index >= 0;
        if (r.automatable)
        {
            const auto* param = modelparams::find (c.instrument, k.index);
            const auto x = param != nullptr ? param->normalised (r.to) : (k.high > k.low ? (r.to - k.low) / (k.high - k.low) : 0.0f);
            r.automation = diffDb (render (c, p, s, [&] (Player& pl) {
                                       if (auto* i = pl.inst())
                                           i->setParameter (k.index, x);
                                   }),
                                   base);
        }
        rep.results.push_back (r);
    }
    return rep;
}

inline bool moved (double db, double noise) { return db > std::max (movedDb, noise + 20.0); }

// A control can be silent only because another one switches its section off (an LFO amount of 0, an effect bypassed,
// unison 1…). For the controls whose flip moved nothing, `contexts` random settings of all the other controls are tried
// (a fixed seed): a control that moves in none of them is reported (its index in `controls`). Uniform values inside
// each control's range; the master levels keep their values so the context is not silent.
inline std::vector<int> stillDead (const ChannelDef& c, const std::vector<Control>& controls, const std::vector<int>& dead, const Pattern& p,
                                   const Samples& s, int contexts = 6, std::uint32_t seed = 170)
{
    std::vector<int> left = dead;
    auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (float) ((seed >> 8) & 0xffffff) / 16777216.0f;
    };
    for (int k = 0; k < contexts && ! left.empty(); ++k)
    {
        auto ctx = c;
        for (const auto& ctl : controls)
        {
            std::string lower = ctl.name;
            std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
            if (lower.find ("master") != std::string::npos || lower == "volume" || lower == "output" || lower.find ("mute") != std::string::npos)
                continue;
            ctl.set (ctx, ctl.low + rnd() * (ctl.high - ctl.low));
        }
        const auto base = render (ctx, p, s);
        if (peak (base) < 1e-4)
            continue;
        std::vector<int> next;
        for (const auto i : left)
        {
            const auto& ctl = controls[(std::size_t) i];
            const auto v = ctl.get (ctx);
            auto changed = ctx;
            if (! v || ! ctl.set (changed, flipped (ctl, *v)) || ! moved (diffDb (render (changed, p, s), base), -300.0))
                next.push_back (i);
        }
        left = std::move (next);
    }
    return left;
}
} // namespace lynx::ctlaudit
