#pragma once

// "Every effect control does something" (round 173d, issue #77): for one built-in effect, every control its editor
// shows (core/PluginLayout.h) and every parameter it lists (effectInfo) is set to a few values with the others at
// their defaults, and a short programme (drums, a sustained vocal-like pad, a full mix, a tail of silence; −18 dBFS
// RMS) is rendered through the effect as the mixer runs it: one instance, the parameters arriving with every block
// (the app keeps the running effect when only parameters change), controls stored in the slot's state through
// acceptState or a new instance, as core/RackRenderer.cpp does.
//
//   continuous controls  0, ½ and 1 of the range (the ends the editor reaches)
//   choices              every option (layout radio / tabs / menu / toggle / stepper, or a parameter whose value text
//                        takes at most `maxChoiceTexts` different values)
//
// A control is DEAD when no tested value changes the output by more than `movedDb` (relative RMS of the difference,
// −60 dB) — nor the slot's sends, nor its control outputs (controllers). A dead control is tried again:
//   1. as the editor shows it (its layout conditions met, its "idle" switch off);
//   2. with side-chain inputs (a kick on the side-chain, a saw chord on the second input);
//   3. in random settings of the others (sections switched on); the first single other control that wakes it up is
//      named as its prerequisite (dependent-ok).
// BROKEN outputs: NaN / inf anywhere; and for the default and every choice option: silence, a peak over +6 dBFS, a DC
// offset, a click when the option is switched while the pad plays.
// Used by tools/fxaudit.cpp (the survey, scripts/fxaudit.sh) and tests/FxAudit173dTests.inc (the permanent checks).

#include <cstdlib>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/ConvolverFx.h"
#include "core/EquoFx.h"
#include "core/LovePhilter.h"
#include "core/Effects.h"
#include "core/FacetEq.h"
#include "core/PluginLayout.h"
#include "core/StateParams.h"
#include "core/Vocodex.h"

namespace lynx::fxaudit
{
constexpr double rate = 44100.0;
constexpr int blockSize = 512;
constexpr double movedDb = -60.0;
constexpr int maxChoiceTexts = 12;
constexpr int maxSteppedTexts = 64;

// ---------------------------------------------------------------------------------------------------------------------
// the programme
struct Programme
{
    std::vector<float> l, r;       // the effect's input
    std::vector<float> scL, scR;   // the side-chain (a kick), used only in the side-chain context
    std::vector<float> sc2L, sc2R; // the second side input (a saw chord: a vocoder's carrier)
    int segment = 0;               // frames per part (drums, pad, mix), then a tail of silence
    int frames() const { return (int) l.size(); }
    int padMiddle() const { return segment + segment / 2; } // a steady pad (switch clicks are measured here)
};

inline Programme programme (double segmentSeconds = 0.6, double tailSeconds = 0.25)
{
    Programme p;
    p.segment = (int) (segmentSeconds * rate);
    const int tail = (int) (tailSeconds * rate);
    const int n = 3 * p.segment + tail;
    p.l.assign ((std::size_t) n, 0.0f), p.r.assign ((std::size_t) n, 0.0f);
    p.scL.assign ((std::size_t) n, 0.0f), p.sc2L.assign ((std::size_t) n, 0.0f);
    std::uint32_t seed = 173;
    auto noise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return (float) ((double) (seed >> 8) / 8388608.0 - 1.0);
    };
    constexpr double pi = 3.14159265358979323846;
    // drums: 16ths at 120 bpm (0.125 s): kick on 0 and 3, snare on 2 and 6, hats on every 16th
    std::vector<float> dl ((std::size_t) n, 0.0f), dr ((std::size_t) n, 0.0f), kick ((std::size_t) n, 0.0f);
    const int step = (int) (0.125 * rate);
    float hpState = 0.0f, prev = 0.0f;
    for (int at = 0; at < n - tail; at += step)
    {
        const int s16 = (at / step) % 8;
        for (int k = 0; k < (int) (0.25 * rate) && at + k < n; ++k)
        {
            const double t = k / rate;
            float v = 0.0f, side = 0.0f;
            if (s16 == 0 || s16 == 3)
            {
                const double ph = 2.0 * pi * (50.0 * t + 100.0 * 0.04 * (1.0 - std::exp (-t / 0.04)));
                const float kv = (float) (std::sin (ph) * std::exp (-t / 0.15));
                v += kv;
                kick[(std::size_t) (at + k)] += kv;
            }
            if (s16 == 2 || s16 == 6)
                v += (float) ((0.5 * noise() + 0.4 * std::sin (2.0 * pi * 200.0 * t)) * std::exp (-t / 0.08)), side += 0.15f * noise() * (float) std::exp (-t / 0.08);
            if (k < (int) (0.05 * rate))
            {
                const float w = noise();
                hpState = 0.6f * (hpState + w - prev); // a crude high-pass
                prev = w;
                v += 0.25f * hpState * (float) std::exp (-t / 0.02);
                side += 0.1f * hpState * (float) std::exp (-t / 0.02) * (s16 % 2 == 0 ? 1.0f : -1.0f);
            }
            dl[(std::size_t) (at + k)] += v + side;
            dr[(std::size_t) (at + k)] += v - side;
        }
    }
    // the pad: an "ah" vowel chord (additive saws weighted by two formants), vibrato, detuned between L and R, and an
    // "s" (6–10 kHz noise) in its middle; a 55 Hz bass under the mix
    std::vector<float> pl ((std::size_t) n, 0.0f), pr ((std::size_t) n, 0.0f), carrier ((std::size_t) n, 0.0f);
    const double notes[] = { 220.0, 277.18, 329.63 };
    auto formant = [] (double hz) {
        auto f = [hz] (double c, double w) { return std::exp (-0.5 * std::pow ((hz - c) / w, 2.0)); };
        return 0.15 + f (700.0, 180.0) + 0.6 * f (1200.0, 250.0) + 0.25 * f (2600.0, 400.0);
    };
    std::vector<double> phase (6 * 24, 0.0);
    float bp1 = 0.0f, bp2 = 0.0f;
    for (int i = 0; i < n - tail; ++i)
    {
        const double t = i / rate;
        const double vib = 1.0 + 0.004 * std::sin (2.0 * pi * 5.0 * t);
        double l = 0.0, r = 0.0;
        for (int v = 0; v < 3; ++v)
            for (int h = 1; h <= 24; ++h)
            {
                const double base = notes[v] * h * vib;
                if (base > 0.45 * rate)
                    break;
                const double g = formant (base) / h;
                auto& pL = phase[(std::size_t) (v * 24 + h - 1)];
                auto& pR = phase[(std::size_t) (72 + v * 24 + h - 1)];
                pL += 2.0 * pi * base / rate;
                pR += 2.0 * pi * base * 1.003 / rate;
                l += g * std::sin (pL);
                r += g * std::sin (pR);
            }
        const double env = std::min (1.0, t * 40.0);
        pl[(std::size_t) i] = (float) (l * env), pr[(std::size_t) i] = (float) (r * env);
        // the carrier: a plain saw chord
        double c = 0.0;
        for (const auto f : notes)
            c += 2.0 * (std::fmod (t * f, 1.0) - 0.5);
        carrier[(std::size_t) i] = (float) c;
        // "s": band-passed noise for 80 ms at the middle of each pad
        const float w = noise();
        bp1 += 0.9f * (w - bp1);
        bp2 = 0.5f * (bp1 - bp2) + 0.5f * bp2;
        const double inPad = std::fmod (t, segmentSeconds) - 0.6 * segmentSeconds;
        if (inPad > 0.0 && inPad < 0.08)
        {
            pl[(std::size_t) i] += 0.4f * (bp1 - bp2);
            pr[(std::size_t) i] += 0.4f * (bp1 - bp2);
        }
    }
    auto rms = [] (const std::vector<float>& a, int from, int to) {
        double e = 0.0;
        for (int i = from; i < to; ++i)
            e += (double) a[(std::size_t) i] * a[(std::size_t) i];
        return std::sqrt (e / std::max (1, to - from));
    };
    const double target = std::pow (10.0, -18.0 / 20.0);
    const double dg = target / std::max (1e-9, rms (dl, 0, p.segment)), pg = target / std::max (1e-9, rms (pl, p.segment, 2 * p.segment));
    for (int i = 0; i < n; ++i)
    {
        const auto at = (std::size_t) i;
        const int part = std::min (3, i / std::max (1, p.segment));
        const double bass = part == 2 ? 0.5 * target * std::sin (2.0 * pi * 55.0 * i / rate) : 0.0;
        if (part == 0)
            p.l[at] = (float) (dg * dl[at]), p.r[at] = (float) (dg * dr[at]);
        else if (part == 1)
            p.l[at] = (float) (pg * pl[at]), p.r[at] = (float) (pg * pr[at]);
        else if (part == 2)
            p.l[at] = (float) (0.6 * (dg * dl[at] + pg * pl[at]) + bass), p.r[at] = (float) (0.6 * (dg * dr[at] + pg * pr[at]) + bass);
        p.scL[at] = 2.0f * kick[at]; // (a hot key: +6 dBFS kicks, over any default threshold or ceiling)
        p.sc2L[at] = (float) (target * 0.5 * carrier[at]);
    }
    // (the right key: the kicks a 16th later at 0.7 ×: a control that mixes or links the side-chain's channels is
    // heard either way, whichever channel a linked detector follows)
    const auto sixteenth = (std::size_t) std::lround (rate * 0.125);
    p.scR.assign (p.scL.size(), 0.0f);
    for (std::size_t i = sixteenth; i < p.scL.size(); ++i)
        p.scR[i] = 0.7f * p.scL[i - sixteenth];
    p.sc2R = p.sc2L;
    return p;
}

// ---------------------------------------------------------------------------------------------------------------------
// the controls
enum class Type { continuous, choice, toggle };

struct Condition
{
    int param = -1;
    float lo = 0.0f, hi = 1.0f;
    bool holds (float v) const { return v >= lo && (v < hi || hi >= 1.0f); }
};

struct Control
{
    int param = -1;        // plugui::paramIndex numbering (state controls after the effect's own parameters)
    std::string name;
    Type type = Type::continuous;
    std::vector<float> values;      // 0…1 positions to render
    std::vector<std::string> texts; // what each value reads as
    bool state = false;             // stored in the slot's state (core/StateParams.h)
    bool inLayout = false;          // the editor shows it
    bool stepped = false;           // many steps (12 of them rendered): judged like a knob (no mode checks)
    bool momentary = false;         // a push button: the engine reacts to a flip while playing
    std::vector<Condition> shownWhen, idleWhen;
};

inline bool isNoSound (EffectKind k)
{
    switch (k)
    {
        case EffectKind::peakController: case EffectKind::formulaController: case EffectKind::waveCandy: case EffectKind::visualizer:
        case EffectKind::xyController: case EffectKind::xyzController: case EffectKind::controlSurface: case EffectKind::bigClock:
        case EffectKind::tuner: case EffectKind::htmlNotebook:
            return true;
        default:
            return false;
    }
}

// effects the audit can't run alone: containers, hosted plugins, a device round trip, sample players without a sample
inline const char* skipReason (EffectKind k)
{
    switch (k)
    {
        case EffectKind::plugin: return "a hosted plugin";
        case EffectKind::parallel: return "a container (its children are audited on their own)";
        case EffectKind::patcher: return "a container (its nodes are audited on their own)";
        case EffectKind::hardwareInsert: return "needs an audio device (round trip through hardware)";
        case EffectKind::waveCandy: case EffectKind::visualizer: case EffectKind::bigClock: case EffectKind::htmlNotebook:
        case EffectKind::tuner:
            return "visual only: no sound, no control output";
        case EffectKind::edison: // (round 190cd)
            return "a recorder / editor: its controls act on its own recording and playback, the input passes through";
        default: return nullptr;
    }
}

inline float valueOf (const EffectSlot& s, int param)
{
    if (plugui::stateIndex (s.kind, param) >= 0)
        return plugui::stateValue01 (s.kind, param, s.state);
    return param >= 0 && param < maxEffectParams ? s.params[(std::size_t) param] : 0.0f;
}

// writes a control as the editor does (PluginEditors.cpp setValue)
inline void setValue (EffectSlot& s, int param, float v)
{
    if (const auto si = plugui::stateIndex (s.kind, param); si >= 0)
    {
        if (const auto* t = stateparams::tableFor (s.kind))
            s.state = t->with (s.state, si, plugui::stateRaw (s.kind, param, v));
        return;
    }
    if (param >= 0 && param < maxEffectParams)
        s.params[(std::size_t) param] = v;
}

inline std::string textOf (const EffectSlot& s, int param, float v)
{
    if (const auto si = plugui::stateIndex (s.kind, param); si >= 0)
    {
        const auto* t = stateparams::tableFor (s.kind);
        return t != nullptr && t->text ? t->text (s.state, si, plugui::stateRaw (s.kind, param, v)) : std::to_string (plugui::stateRaw (s.kind, param, v));
    }
    return formatEffectParam (s.kind, param, v, 120.0, s.params);
}

inline std::vector<Control> controlsOf (const EffectSlot& slot)
{
    const auto kind = slot.kind;
    std::vector<Control> out;
    std::map<int, std::size_t> at;
    const auto& layout = plugui::layoutFor (kind);
    auto cond = [kind] (const char* p, float lo, float hi, std::vector<Condition>& to) {
        if (p == nullptr)
            return;
        const auto i = plugui::paramIndex (kind, p);
        if (i >= 0)
            to.push_back ({ i, lo, hi });
    };
    for (const auto& item : layout.items)
    {
        if (! plugui::isValueControl (item.type) || item.param == nullptr || (item.style & plugui::pageTabs) != 0 || item.type == plugui::Item::value)
            continue;
        const auto p = plugui::paramIndex (kind, item.param);
        if (p < 0)
            continue;
        Control c;
        c.param = p;
        c.name = item.param;
        c.inLayout = true;
        c.state = plugui::stateIndex (kind, p) >= 0;
        cond (item.whenParam, item.whenMin, item.whenMax, c.shownWhen);
        cond (item.when2Param, item.when2Min, item.when2Max, c.shownWhen);
        cond (item.when3Param, item.when3Min, item.when3Max, c.shownWhen);
        cond (item.idleParam, item.idleMin, item.idleMax, c.idleWhen);
        cond (item.idle2Param, item.idle2Min, item.idle2Max, c.idleWhen);
        if (item.type == plugui::Item::radio || item.type == plugui::Item::tabs || item.type == plugui::Item::menu)
        {
            c.type = Type::choice;
            const auto n = (int) plugui::optionLabels (item).size();
            for (int o = 0; o < n; ++o)
                c.values.push_back (plugui::optionValue (item, o));
        }
        else if (item.type == plugui::Item::toggle)
            c.type = Type::toggle, c.values = { 0.0f, 1.0f }, c.momentary = (item.style & plugui::momentary) != 0;
        else if (item.steps > 0 && item.steps <= maxChoiceTexts)
        {
            c.type = Type::choice;
            for (int k = 0; k <= item.steps; ++k)
                c.values.push_back ((float) k / (float) item.steps);
        }
        if (auto it = at.find (p); it != at.end())
        {
            // the same parameter twice (one per page / voicing): keep the richer description, merge the conditions
            auto& old = out[it->second];
            if (old.type == Type::continuous && c.type != Type::continuous)
                old.type = c.type, old.values = c.values;
            if (old.shownWhen.empty() || c.shownWhen.empty())
                old.shownWhen.clear();
            continue;
        }
        at[p] = out.size();
        out.push_back (std::move (c));
    }
    // the parameters the editor doesn't show (automation still reaches them)
    const auto count = plugui::paramCount (kind);
    for (int p = 0; p < count; ++p)
        if (at.find (p) == at.end())
        {
            Control c;
            c.param = p;
            c.name = plugui::paramName (kind, p);
            c.state = plugui::stateIndex (kind, p) >= 0;
            at[p] = out.size();
            out.push_back (std::move (c));
        }
    // a continuous control whose value text takes few values is a choice; the values to render
    for (auto& c : out)
    {
        if (c.type == Type::continuous)
        {
            std::vector<std::pair<std::string, std::vector<float>>> runs;
            constexpr int samples = 257;
            for (int k = 0; k < samples; ++k)
            {
                const float v = (float) k / (float) (samples - 1);
                auto t = textOf (slot, c.param, v);
                if (runs.empty() || runs.back().first != t)
                    runs.push_back ({ t, {} });
                runs.back().second.push_back (v);
                if ((int) runs.size() > maxSteppedTexts)
                    break;
            }
            // a stepped control with many steps (a slot number…): every run would cost too much, 12 spread ones
            const bool stepped = (int) runs.size() <= maxSteppedTexts && runs.size() > maxChoiceTexts
                                 && std::all_of (runs.begin(), runs.end(), [] (const auto& r) { return r.second.size() >= 2; });
            if (stepped)
            {
                std::vector<std::pair<std::string, std::vector<float>>> pick;
                for (int k = 0; k < maxChoiceTexts; ++k)
                    pick.push_back (runs[(std::size_t) std::lround ((double) k * (double) (runs.size() - 1) / (maxChoiceTexts - 1))]);
                runs = std::move (pick);
            }
            if (((int) runs.size() <= maxChoiceTexts || stepped) && runs.size() >= 2)
            {
                c.type = runs.size() == 2 ? Type::toggle : Type::choice;
                c.stepped = stepped;
                for (const auto& [t, vs] : runs)
                    c.values.push_back (vs.front() == 0.0f ? 0.0f : vs.back() == 1.0f ? 1.0f : vs[vs.size() / 2]);
            }
            else
                c.values = { 0.0f, 0.5f, 1.0f };
        }
        for (const auto v : c.values)
            c.texts.push_back (textOf (slot, c.param, v));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// rendering
struct Output
{
    std::vector<float> l, r;
    std::vector<float> send; // every send / extra output, interleaved
    std::vector<float> ctl;  // control outputs 0…7 per block (every control's of a Control Surface)
    bool finite = true;
    int latency = 0;
};

struct RunOptions
{
    bool sidechain = false;
    double startBeat = 0.0;               // the song position of the first block (beats)
    float inputGain = 1.0f;               // the programme × this (a hot input: clippers, ceilings)
    double bpm = 120.0;                   // the song tempo
    int moveParam = -1;                   // this control jumps to `moveValue` at the pad's middle (a knob turned while playing)
    float moveValue = 0.0f;
    int switchBlock = -1;                 // the slot `after` takes over at this block (a mode switched while playing)
    const EffectSlot* after = nullptr;
};

inline Output run (const EffectSlot& built, const EffectSlot& played, const Programme& prog, const RunOptions& o = {})
{
    Output out;
    auto slot = built; // (some effects keep a pointer to their slot's parameters: it stays alive)
    auto fx = makeEffect (slot);
    if (fx == nullptr)
        return out;
    fx->prepare (rate);
    auto apply = [&fx, &slot] (const EffectSlot& next) {
        if (next.state != slot.state)
        {
            if (! fx->acceptState (next.state))
            {
                slot = next;
                fx = makeEffect (slot);
                fx->prepare (rate);
                return;
            }
        }
        slot.params = next.params;
        slot.state = next.state;
    };
    apply (played);
    out.latency = fx->latencySamples();
    const int n = prog.frames();
    out.l.resize ((std::size_t) n), out.r.resize ((std::size_t) n);
    std::vector<float> s0l (blockSize), s0r (blockSize), s1l (blockSize), s1r (blockSize), s2l (blockSize), s2r (blockSize);
    // C4 E4 G4 B4 held on every channel of the rack (an effect that follows one MIDI channel hears them on any)
    std::array<NoteMask, 1024> held; // (RackRenderer::maxChannels)
    held.fill ({ 1ull << 60, (1ull << (64 - 64)) | (1ull << (67 - 64)) | (1ull << (71 - 64)) });
    double tick = o.startBeat * ppq;
    const double ticksPerSample = o.bpm / 60.0 * ppq / rate;
    for (int at = 0, b = 0; at < n; at += blockSize, ++b)
    {
        if (b == o.switchBlock && o.after != nullptr)
            apply (*o.after);
        if (o.moveParam >= 0 && b == prog.padMiddle() / blockSize)
        {
            auto next = slot;
            setValue (next, o.moveParam, o.moveValue);
            apply (next);
        }
        const int len = std::min (blockSize, n - at);
        for (int k = 0; k < len; ++k)
            out.l[(std::size_t) (at + k)] = o.inputGain * prog.l[(std::size_t) (at + k)], out.r[(std::size_t) (at + k)] = o.inputGain * prog.r[(std::size_t) (at + k)];
        std::fill (s0l.begin(), s0l.end(), 0.0f), std::fill (s0r.begin(), s0r.end(), 0.0f);
        std::fill (s1l.begin(), s1l.end(), 0.0f), std::fill (s1r.begin(), s1r.end(), 0.0f);
        std::fill (s2l.begin(), s2l.end(), 0.0f), std::fill (s2r.begin(), s2r.end(), 0.0f);
        fx->setSendOutput (s0l.data(), s0r.data());
        fx->setExtraOutput (0, s1l.data(), s1r.data());
        fx->setExtraOutput (1, s2l.data(), s2r.data());
        fx->setSongPosition (tick);
        fx->setPlaying (true);
        fx->setHeldNotes (held.data(), (int) held.size());
        // round 190ee: Pitcher takes its notes from a MIDI port (a MIDI Out on that port): the same chord arrives there
        // (a held note's repeated note on changes nothing)
        if (slot.kind == EffectKind::pitchCorrect)
            for (const auto key : { 60, 64, 67, 71 })
                fx->midiNote (key, 100, 0);
        if (o.sidechain)
        {
            fx->setSidechain (prog.scL.data() + at, prog.scR.data() + at);
            fx->setSidechain2 (prog.sc2L.data() + at, prog.sc2R.data() + at);
        }
        fx->process (out.l.data() + at, out.r.data() + at, len, slot.params, o.bpm);
        tick += len * ticksPerSample;
        for (int k = 0; k < len; ++k)
        {
            out.send.push_back (s0l[(std::size_t) k] + s1l[(std::size_t) k] + 0.5f * s2l[(std::size_t) k]);
            out.send.push_back (s0r[(std::size_t) k] + s1r[(std::size_t) k] + 0.5f * s2r[(std::size_t) k]);
        }
        // the editor's meters and the automation sources read outputs 0…7; a Control Surface sends one per control
        const int outputs = slot.kind == EffectKind::controlSurface ? maxEffectParams : 8;
        for (int c = 0; c < outputs; ++c)
            out.ctl.push_back (fx->controlOutput (c));
    }
    out.latency = fx->latencySamples(); // (after the blocks: some effects learn their settings in process())
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (out.l[(std::size_t) i]) || ! std::isfinite (out.r[(std::size_t) i]))
        {
            out.finite = false;
            break;
        }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// measures
inline double relDiffDb (const std::vector<float>& a, const std::vector<float>& b, double floorEnergy = 1e-12)
{
    double e = 0.0, ref = 0.0;
    const auto len = std::min (a.size(), b.size());
    for (std::size_t i = 0; i < len; ++i)
    {
        const double d = (double) a[i] - (double) b[i];
        if (std::isfinite (d))
            e += d * d;
        ref += (double) b[i] * b[i];
    }
    if (e == 0.0)
        return -300.0;
    return 10.0 * std::log10 (e / std::max (ref, floorEnergy * (double) std::max<std::size_t> (1, len)));
}

inline double audioDiffDb (const Output& a, const Output& b)
{
    std::vector<float> x, y;
    x.reserve (a.l.size() * 2), y.reserve (b.l.size() * 2);
    x.insert (x.end(), a.l.begin(), a.l.end()), x.insert (x.end(), a.r.begin(), a.r.end());
    y.insert (y.end(), b.l.begin(), b.l.end()), y.insert (y.end(), b.r.begin(), b.r.end());
    return relDiffDb (x, y);
}

inline double peakOf (const Output& o)
{
    double p = 0.0;
    for (std::size_t i = 0; i < o.l.size(); ++i)
        p = std::max ({ p, (double) std::abs (o.l[i]), (double) std::abs (o.r[i]) });
    return p;
}

inline double dcOf (const Output& o)
{
    double sl = 0.0, sr = 0.0;
    for (std::size_t i = 0; i < o.l.size(); ++i)
        sl += o.l[i], sr += o.r[i];
    const auto n = (double) std::max<std::size_t> (1, o.l.size());
    return std::max (std::abs (sl / n), std::abs (sr / n));
}

// octave-band power (mid signal, 2048-point Hann frames): bands 31.5 Hz … 16 kHz
inline std::array<double, 10> octaveBands (const Output& o)
{
    std::array<double, 10> bands {};
    constexpr int N = 2048;
    std::vector<std::complex<double>> buf (N);
    for (std::size_t start = 0; start + N <= o.l.size(); start += N)
    {
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / (N - 1));
            buf[(std::size_t) i] = w * 0.5 * ((double) o.l[start + (std::size_t) i] + o.r[start + (std::size_t) i]);
        }
        // in-place radix-2 FFT
        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j)
                std::swap (buf[(std::size_t) i], buf[(std::size_t) j]);
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const auto w = std::polar (1.0, -2.0 * 3.14159265358979323846 / len);
            for (int i = 0; i < N; i += len)
            {
                std::complex<double> wk (1.0);
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto u = buf[(std::size_t) (i + k)], v = buf[(std::size_t) (i + k + len / 2)] * wk;
                    buf[(std::size_t) (i + k)] = u + v, buf[(std::size_t) (i + k + len / 2)] = u - v;
                    wk *= w;
                }
            }
        }
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * rate / N;
            const int band = (int) std::floor (std::log2 (hz / 22.3));
            if (band >= 0 && band < 10)
                bands[(std::size_t) band] += std::norm (buf[(std::size_t) k]);
        }
    }
    return bands;
}

// the largest change of an octave band's level (dB), over the bands within 70 dB of the loudest
inline double bandDiffDb (const std::array<double, 10>& a, const std::array<double, 10>& b)
{
    const double top = std::max (*std::max_element (a.begin(), a.end()), *std::max_element (b.begin(), b.end()));
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::max (a[i], b[i]) > top * 1e-7)
            worst = std::max (worst, std::abs (10.0 * std::log10 ((a[i] + 1e-30) / (b[i] + 1e-30))));
    return worst;
}

// the change of the side signal (L − R), relative to the reference's whole signal, in dB
inline double stereoDiffDb (const Output& a, const Output& b)
{
    double e = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < std::min (a.l.size(), b.l.size()); ++i)
    {
        const double sa = 0.5 * ((double) a.l[i] - a.r[i]), sb = 0.5 * ((double) b.l[i] - b.r[i]);
        e += (sa - sb) * (sa - sb);
        ref += 0.5 * ((double) b.l[i] * b.l[i] + (double) b.r[i] * b.r[i]);
    }
    return e == 0.0 ? -300.0 : 10.0 * std::log10 (e / std::max (ref, 1e-12));
}

// the biggest sample step in [from, to) of both channels
inline double maxStep (const Output& o, int from, int to)
{
    double m = 0.0;
    for (int i = std::max (1, from); i < std::min ((int) o.l.size(), to); ++i)
        m = std::max ({ m, (double) std::abs (o.l[(std::size_t) i] - o.l[(std::size_t) i - 1]),
                        (double) std::abs (o.r[(std::size_t) i] - o.r[(std::size_t) i - 1]) });
    return m;
}

// ---------------------------------------------------------------------------------------------------------------------
// the audit
struct Change
{
    float value = 0.0f;
    std::string text;
    double audio = -300.0, send = -300.0, ctl = -300.0, band = 0.0, stereo = -300.0;
    double peak = 0.0, dc = 0.0;
    bool finite = true;
    bool latency = false; // the reported latency (PDC) changed
};

struct Finding
{
    std::string control, text, what; // what: "NaN", "silent", "clip", "dc", "click"
    double value = 0.0;
};

enum class Status { ok, dependent, dead, nonAudio };

struct ControlResult
{
    Control control;
    std::vector<Change> changes;   // (dependent: measured in the prerequisite's context)
    double bestDefault = -300.0;   // dependent: the best move in the editor's default context
    Status status = Status::ok;
    std::string prerequisite;  // dependent: what wakes it up
    std::string prereqKey;     // … as a key for Options::known ("" = as the editor shows it)
    bool onlyNewInstance = false; // dead live, moves in a new instance (a construction-time setting)
    double best = -300.0;      // the largest change (dB) over the tested values and paths
};

struct KindReport
{
    EffectKind kind {};
    std::string name;
    const char* skipped = nullptr;
    double noise = -300.0;  // the default rendered twice (non-deterministic engines)
    double peak = 0.0;
    int listed = 0;          // controls the effect has (more than `controls` when sampled)
    std::vector<ControlResult> controls;
    std::vector<Finding> broken;
    int dead() const { return (int) std::count_if (controls.begin(), controls.end(), [] (const ControlResult& c) { return c.status == Status::dead; }); }
    int dependent() const { return (int) std::count_if (controls.begin(), controls.end(), [] (const ControlResult& c) { return c.status == Status::dependent; }); }
};

// settings known to wake up a group of controls (an envelope's points, a band's type…): tried before the searches
struct Hint
{
    std::string note;
    std::vector<std::pair<int, float>> sets; // param, 0…1 value
    double bpm = 0.0;                        // > 0: also at this tempo
    bool sidechain = false;                  // also with the side-chain input
    int move = -1;                           // ≥ 0: this parameter jumps to moveTo in the middle of the pad
    float moveTo = 0.0f;
    std::function<void (EffectSlot&)> edit = nullptr; // a change no parameter makes (a send track kept in the state)
};

struct Options
{
    std::vector<Hint> hints;
    bool clicks = true;    // switch every choice while playing
    int contexts = 16;     // random contexts for the dead controls
    bool quick = false;    // continuous controls: only the far end from the default (not 0, ½, 1)
    std::vector<std::string> only; // only the controls with these names (empty = all)
    // the prerequisite key a previous survey found for (effect name, control name), "" = none: tried first
    std::function<std::string (const std::string&, const std::string&)> known;
    int maxControls = 120;  // more than this: the state controls are sampled
    int searchBudget = 160; // single-control prerequisites tried per dead control
    // (effect, control) → true: a control known to be inert (the regression test's allowed list): not searched
    std::function<bool (const std::string&, const std::string&)> inert;
};

// the hints the audit knows per effect: settings that wake up a group of controls together (an envelope's points, a
// section that needs two switches) — parameter names, 0…1 values
inline std::vector<Hint> hintsFor (EffectKind kind)
{
    std::vector<Hint> out;
    auto named = [kind] (std::string note, std::vector<std::pair<const char*, float>> sets) {
        Hint h { std::move (note), {} };
        for (const auto& [n, v] : sets)
            if (const auto i = plugui::paramIndex (kind, n); i >= 0)
                h.sets.push_back ({ i, v });
        return h;
    };
    switch (kind)
    {
        case EffectKind::rhythmGain:
        {
            Hint h { "12 points spread over a 1-beat period", { { 0, 1.0f / 64.0f }, { 1, 1.0f } } };
            for (int k = 0; k < 12; ++k)
                h.sets.push_back ({ 2 + 3 * k, (float) (k + 1) / 13.0f }), h.sets.push_back ({ 3 + 3 * k, k % 2 == 0 ? 0.2f : 1.0f }),
                    h.sets.push_back ({ 4 + 3 * k, 0.5f });
            out.push_back (h);
            break;
        }
        case EffectKind::vintagePhaser:
            out.push_back (named ("the delay on and mixed in", { { "Delay", 1.0f }, { "Delay mix", 1.0f }, { "Delay fdbk", 0.5f }, { "Delay time", 0.3f } }));
            out.push_back (named ("the LFO synced to the tempo", { { "Tempo sync", 1.0f }, { "LFO speed", 0.5f }, { "Mix", 1.0f } }));
            break;
        case EffectKind::fruityDelayBank:
        {
            Hint on { "every bank on", {} }, shelf { "every bank on, both filters peaking", {} },
                low { "every bank on, both filters low-pass at a mid cutoff, feedback up, a gain pattern (time-varying: filter 1 before or after the line differs)", {} };
            for (int b = 1; b <= 8; ++b)
            {
                const auto n = "B" + std::to_string (b) + " ";
                for (auto* h : { &on, &shelf, &low })
                    if (const auto i = plugui::paramIndex (kind, (n + "On").c_str()); i >= 0)
                        h->sets.push_back ({ i, 1.0f });
                for (const char* f : { "Filter 1 type", "Filter 2 type" })
                    if (const auto i = plugui::paramIndex (kind, (n + f).c_str()); i >= 0)
                        shelf.sets.push_back ({ i, 6.0f / 7.0f }), low.sets.push_back ({ i, 1.0f / 7.0f });
                for (const char* f : { "Filter 1 gain", "Filter 2 gain" })
                    if (const auto i = plugui::paramIndex (kind, (n + f).c_str()); i >= 0)
                        shelf.sets.push_back ({ i, 0.8f });
                for (const char* f : { "Filter 1 cutoff", "Filter 2 cutoff", "Filter 1 resonance", "Filter 2 resonance" })
                    if (const auto i = plugui::paramIndex (kind, (n + f).c_str()); i >= 0)
                        shelf.sets.push_back ({ i, 0.5f }), low.sets.push_back ({ i, 0.5f }); // (the default cutoff sits near Nyquist)
                if (const auto i = plugui::paramIndex (kind, (n + "Feedback level").c_str()); i >= 0)
                    low.sets.push_back ({ i, 0.8f });
                for (const char* f : { "Steps", "Shape" })
                    if (const auto i = plugui::paramIndex (kind, (n + f).c_str()); i >= 0)
                        low.sets.push_back ({ i, 0.3f });
            }
            out.push_back (on);
            out.push_back (shelf);
            auto slow = on;
            slow.note = "every bank on, at 90 BPM (a synced time equals the free one at 120 BPM)", slow.bpm = 90.0;
            out.push_back (slow);
            out.push_back (low);
            break;
        }
        case EffectKind::vintageChorus:
            out.push_back (named ("the manual mode, its LFO synced to the tempo", { { "Mode", 1.0f }, { "LFO sync", 1.0f } }));
            break;
        case EffectKind::fruityLimiter:
            out.push_back (named ("the compressor on (threshold low, ratio up)", { { "Comp. threshold", 0.1f }, { "Ratio", 0.8f } }));
            out.push_back (named ("the gate on", { { "Gate gain", 0.0f }, { "Gate threshold", 0.6f } }));
            break;
        case EffectKind::sentryGate:
        {
            out.push_back (named ("Upward style, threshold under the signal", { { "Style", 1.0f }, { "Up threshold", 0.0f } }));
            auto ext = named ("the external side-chain (its right channel differs)", { { "External SC", 1.0f } });
            ext.sidechain = true;
            out.push_back (ext);
            break;
        }
        case EffectKind::autoPan:
            out.push_back (named ("the Rhythm voicing triggered by the audio", { { "Voicing", 1.0f }, { "Trigger", 0.5f }, { "Trig threshold", 0.5f }, { "Amount", 1.0f } }));
            out.push_back (named ("the Rhythm voicing", { { "Voicing", 1.0f } }));
            break;
        case EffectKind::ember:
        {
            // the modulation: every slot on one source, full amount, on a target; six bands with their feedback up;
            // the Classic panel driven
            for (const float src : { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f })
                for (const float target : { 0.1f, 0.3f })
                {
                    Hint h { "the modulation slots on one source (LFO 1 / LFO 2 / envelope), full amount", {} };
                    for (int m = 1; m <= 4; ++m)
                    {
                        const auto n = "Mod " + std::to_string (m) + " ";
                        if (const auto i = plugui::paramIndex (kind, (n + "source").c_str()); i >= 0) h.sets.push_back ({ i, src });
                        if (const auto i = plugui::paramIndex (kind, (n + "target").c_str()); i >= 0) h.sets.push_back ({ i, target });
                        if (const auto i = plugui::paramIndex (kind, (n + "amount").c_str()); i >= 0) h.sets.push_back ({ i, 1.0f });
                    }
                    out.push_back (h);
                }
            {
                // the envelope source sits near +1.4 units on a −18 dBFS input, so a full amount pins Mix / a drive at
                // its end; a small amount on the output gain keeps it inside the range (the follower's times then act)
                Hint env { "the modulation slots on the envelope, a small amount on the output gain", {} };
                for (int m = 1; m <= 4; ++m)
                {
                    const auto n = "Mod " + std::to_string (m) + " ";
                    if (const auto i = plugui::paramIndex (kind, (n + "source").c_str()); i >= 0) env.sets.push_back ({ i, 1.0f });
                    if (const auto i = plugui::paramIndex (kind, (n + "target").c_str()); i >= 0) env.sets.push_back ({ i, 2.0f / 26.0f });
                    if (const auto i = plugui::paramIndex (kind, (n + "amount").c_str()); i >= 0) env.sets.push_back ({ i, 0.6f });
                }
                out.push_back (env);
            }
            Hint six { "six bands, each band's feedback up", {} };
            for (const char* n : { "Bands", "More bands" })
                if (const auto i = plugui::paramIndex (kind, n); i >= 0) six.sets.push_back ({ i, 1.0f });
            for (int b = 1; b <= 6; ++b)
                if (const auto i = plugui::paramIndex (kind, ("B" + std::to_string (b) + " feedback").c_str()); i >= 0) six.sets.push_back ({ i, 1.0f });
            out.push_back (six);
            out.push_back (named ("the Classic panel driven hard", { { "Classic drive", 0.9f } }));
            break;
        }
        case EffectKind::prismMultiband:
        {
            Hint la { "6 bands compressing hard, the global look-ahead at its maximum", {} },
                free { "6 bands, the free layout with every band's edges inside the programme", {} },
                sc { "6 bands, each side-chain filter free and narrow, linked 100 %", {} };
            for (auto* h : { &la, &free, &sc })
                if (const auto i = plugui::paramIndex (kind, "Bands"); i >= 0)
                    h->sets.push_back ({ i, 1.0f });
            if (const auto i = plugui::paramIndex (kind, "Lookahead"); i >= 0)
                la.sets.push_back ({ i, 1.0f });
            if (const auto i = plugui::paramIndex (kind, "Band layout"); i >= 0)
                free.sets.push_back ({ i, 1.0f });
            for (int b = 1; b <= 6; ++b)
            {
                const auto n = "B" + std::to_string (b) + " ";
                auto set = [&] (Hint& h, const char* what, float v) {
                    if (const auto i = plugui::paramIndex (kind, (n + what).c_str()); i >= 0)
                        h.sets.push_back ({ i, v });
                };
                set (la, "threshold", 0.0f), set (la, "ratio", 1.0f), set (la, "range", 0.0f);
                set (free, "low", 0.35f), set (free, "high", 0.65f), set (free, "threshold", 0.0f);
                set (sc, "SC filter", 1.0f), set (sc, "SC low", 0.4f), set (sc, "SC high", 0.6f), set (sc, "link", 1.0f), set (sc, "threshold", 0.0f);
            }
            out.push_back (la);
            out.push_back (free);
            out.push_back (sc);
            auto ext = la;
            ext.note = "6 bands compressing hard, with a side-chain input", ext.sidechain = true;
            out.push_back (ext);
            break;
        }
        case EffectKind::ridgeStrip:
            out.push_back (named ("the Vocal strip's compander working (low thresholds)",
                                  { { "Compander", 1.0f }, { "Compander threshold", 0.1f }, { "Expander threshold", 0.6f } }));
            out.push_back (named ("the dynamics on: a closing gate, a hard compressor, the filters in",
                                  { { "Dynamics on", 1.0f }, { "Gate range", 1.0f }, { "Gate threshold", 0.7f }, { "Comp. threshold", 0.1f },
                                    { "Ratio", 1.0f }, { "HPF", 0.6f }, { "LPF", 0.4f } }));
            for (const float t : { 0.4f, 0.55f, 0.7f }) // (a soft knee acts within 6 dB of the threshold)
                out.push_back (named ("the compander's threshold near the signal, a high ratio",
                                      { { "Compander", 1.0f }, { "Compander threshold", t }, { "Compander ratio", 1.0f } }));
            break;
        case EffectKind::echoform:
            // the Rhythm taps 1…15: Rhythm on with 16 repeats in one beat (inside the programme), the free edit (the tap times count)
            out.push_back (named ("Rhythm on, 16 repeats, free tap times in one beat",
                                  { { "Rhythm", 1.0f }, { "Repeats", 1.0f }, { "Rhythm edit", 0.5f }, { "Length", 0.0f }, { "Decay", 0.0f } }));
            break;
        case EffectKind::facetEq:
        {
            auto h = named ("band 1 a dynamic bell (range −30 dB, a low threshold), with a side-chain input",
                            { { "1 Shape", 2.0f / 16.0f }, { "1 Range", 0.0f }, { "1 Threshold", 0.3f } });
            h.sidechain = true;
            out.push_back (h);
            // every band a spectral dynamic bell (its Density / Spectral Tilt act only there), and the linear phase
            // with a narrow low boost (a flat EQ is a delta at any FIR length: Resolution acts only on a shaped one)
            for (int b = 1; b <= 8; ++b)
            {
                const auto n = std::to_string (b) + " ";
                Hint sp { "band " + std::to_string (b) + " a spectral dynamic bell (range −30 dB, a low threshold)", {} };
                for (const auto& [f, v] : { std::pair<const char*, float> { "Shape", 2.0f / 16.0f }, { "Range", 0.0f }, { "Threshold", 0.3f }, { "Spectral", 1.0f } })
                    if (const auto i = plugui::paramIndex (kind, (n + f).c_str()); i >= 0)
                        sp.sets.push_back ({ i, v });
                sp.sidechain = true;
                out.push_back (sp);
            }
            out.push_back (named ("the linear phase, band 1 a narrow +30 dB bell at a low frequency",
                                  { { "Phase", 1.0f }, { "1 Shape", 2.0f / 16.0f }, { "1 Gain", 1.0f }, { "1 Q", 1.0f }, { "1 Freq", 0.15f } }));
            break;
        }
        case EffectKind::equo:
        case EffectKind::multibandDelay:
            for (int b = 1; b < 8; ++b) // the morph on bank b + 1 (a bank's cells are heard only there)
                out.push_back (named ("the morph on bank " + std::to_string (b + 1), { { "Morph", (float) b / 7.0f } }));
            if (kind == EffectKind::equo) // round 173d3: the band sends go to an insert (FL's send box)
                for (int b = 0; b < 8; ++b)
                {
                    auto h = named (b == 0 ? std::string ("the send to insert 2") : "the send to insert 2, the morph on bank " + std::to_string (b + 1),
                                    { { "Morph", (float) b / 7.0f } });
                    if (b == 0)
                        h.sets.clear();
                    h.edit = [] (EffectSlot& e) {
                        e.sendTracks[0] = 2;
                        equofx::setSendInsert (e.state, 2);
                    };
                    out.push_back (h);
                }
            break;
        case EffectKind::lovePhilter:
        {
            // round 173d3: FL's default has unit 1 on and no articulator drawn, so the units, envelopes, LFOs and maps
            // are woken by drawing them in the state (FL: the unit page's enable boxes and the articulator editor)
            const auto points = [] (std::initializer_list<std::pair<double, double>> xy) {
                std::vector<philter::DocPoint> out;
                double last = 0.0;
                for (const auto& [x, y] : xy)
                {
                    philter::DocPoint q;
                    q.dt = x - last, q.y = y;
                    last = x;
                    out.push_back (q);
                }
                return out;
            };
            const auto drawn = [points] (bool shaper, bool allTargets) {
                return [points, shaper, allTargets] (EffectSlot& e) {
                    auto d = philter::readDoc (e.state);
                    if (! d)
                        return;
                    for (int u = 0; u < 8; ++u)
                    {
                        auto& un = d->u[(std::size_t) u];
                        un.enabled = 1;
                        if (un.mode == 0)
                            un.mode = 1;
                        if (shaper) // an S curve
                        {
                            un.wsOn = 1;
                            un.ws.pts = points ({ { 0.0, 0.0 }, { 0.3, 0.1 }, { 0.5, 0.5 }, { 0.7, 0.9 }, { 1.0, 1.0 } });
                        }
                        for (int t = 0; t < 8; ++t)
                        {
                            if (! allTargets && t != 2)
                                continue;
                            auto& a = d->u[(std::size_t) u].art[(std::size_t) t];
                            for (int p = 0; p < 10; ++p) // a different envelope per pattern
                            {
                                a.pat[(std::size_t) p].on = 1;
                                a.pat[(std::size_t) p].pts = points ({ { 0.0, 0.1 }, { 0.25 + 0.1 * p, 0.95 }, { 1.0 + 0.1 * p, 0.2 + 0.06 * p } });
                            }
                            a.lfo.on = 1;
                            a.lfo.pts = points ({ { 0.0, 1.0 }, { 4.0, 1.0 } });
                            a.lfoDepth = 1.0f;
                            a.maps[0].pts = points ({ { 0.0, 1.0 }, { 1.0, 0.0 } });
                            a.maps[1].pts = points ({ { 0.0, 0.0 }, { 0.5, 1.0 }, { 1.0, 0.2 } });
                            a.maps[2].pts = points ({ { 0.0, 1.0 }, { 0.3, 0.2 }, { 1.0, 0.0 } });
                        }
                    }
                    e.state = philter::writeDoc (*d);
                };
            };
            Hint units { "every unit on (FL's enable boxes)", {} };
            units.edit = [] (EffectSlot& e) {
                if (auto d = philter::readDoc (e.state))
                {
                    for (auto& un : d->u)
                        un.enabled = 1, un.mode = un.mode == 0 ? 1 : un.mode;
                    e.state = philter::writeDoc (*d);
                }
            };
            out.push_back (units);
            Hint cut { "every unit on, the cutoff articulators drawn: a pattern envelope per pattern, the LFO, the Mod X / Mod Y / follower maps", {} };
            cut.edit = drawn (false, false);
            out.push_back (cut);
            Hint all { "every unit on with an S-curve waveshaper, every articulator drawn (envelopes, LFOs, maps)", {} };
            all.edit = drawn (true, true);
            out.push_back (all);
            // (the envelopes' and LFOs' TEMPO: FL's free time runs at 2 units a second, its tempo time at BPM / 60, the same at
            // 120 BPM; the B / H articulators scale the band / high levels, so they act once those are up)
            auto tempo = named ("every articulator drawn, the band and high levels up, at 90 BPM", {});
            for (int u = 1; u <= 8; ++u)
                for (const auto* f : { " band", " high" })
                    if (const auto i = plugui::paramIndex (kind, ("Unit " + std::to_string (u) + f).c_str()); i >= 0)
                        tempo.sets.push_back ({ i, 0.6f });
            tempo.edit = drawn (true, true);
            tempo.bpm = 90.0;
            out.push_back (tempo);
            break;
        }
        case EffectKind::convolver:
        {
            // round 173d3 (gap173d-3): FL's default EQ curve is flat (its gain acts at any EQ amount), so the EQ amount
            // is heard only once a curve is drawn: a low shelf down, a high boost
            Hint h { "an EQ curve drawn (lows down, highs up)", {} };
            h.edit = [] (EffectSlot& e) {
                std::vector<convolver::EnvPoint> pts (4);
                pts[0].y = 0.15, pts[1].dt = 0.3, pts[1].y = 0.5, pts[2].dt = 0.4, pts[2].y = 0.5, pts[3].dt = 0.3, pts[3].y = 0.95;
                convolver::writeEqCurve (e.state, pts);
            };
            out.push_back (h);
            break;
        }
        case EffectKind::frequencySplitter:
        {
            // (the low band down: the bands no longer sum back to the input, so the crossover is heard)
            auto h = named ("FFT mode, the low band down, the crossover moved while it plays (the fade between the old and new filters)",
                            { { "FFT mode", 1.0f }, { "Low level", 0.0f } });
            h.move = plugui::paramIndex (kind, "Low/mid freq."), h.moveTo = 0.6f;
            out.push_back (h);
            break;
        }
        case EffectKind::horizon:
            out.push_back (named ("the echo up and synced to the tempo", { { "Echo level", 1.0f }, { "Echo sync", 1.0f / 3.0f }, { "Echo feedback", 0.5f } }));
            break;
        case EffectKind::reeverb2:
        {
            auto h = named ("a predelay, at 90 BPM (the tempo's predelay equals the free one at 120 BPM)", { { "Predelay", 0.5f } });
            h.bpm = 90.0;
            out.push_back (h);
            break;
        }
        case EffectKind::distructor:
            out.push_back (named ("every module's output up (the final clipper's range)", { { "M1 Out", 1.0f }, { "M2 Out", 1.0f }, { "M3 Out", 1.0f }, { "M4 Out", 1.0f } }));
            break;
        case EffectKind::lynxTune:
            out.push_back (named ("the created vibrato on, at once", { { "Vibrato shape", 1.0f / 3.0f }, { "Vibrato depth", 1.0f }, { "Onset delay", 0.0f }, { "Vibrato rate", 0.5f } }));
            out.push_back (named ("the reference tone on, MIDI channel 1", { { "MIDI", 1.0f / 128.0f }, { "Reference tone", 1.0f } }));
            break;
        case EffectKind::pitchCorrect:
            out.push_back (named ("Harmonize from the held notes, mixed", { { "Harmonize", 1.0f }, { "MIDI", 1.0f }, { "Mix", 1.0f } }));
            break;
        case EffectKind::vocodex:
        {
            // round 173d3 (gap173d-4): a new instance's gate map ("Modulator noise level") is flat at 0, so the
            // Subtractive gate switch has no threshold to act on until the map is drawn
            Hint h { "the gate map drawn (a rising line)", {} };
            h.edit = [] (EffectSlot& e) { e.state = vocodex::withMap (e.state, 3, { { 0.0, 0.0 }, { 1.0, 0.8 } }); };
            out.push_back (h);
            break;
        }
        case EffectKind::pocketMultiband:
            // (round 190cm) FLM's defaults compress nothing (thresholds at 0 dB, ratios 1:1): a band's attack and
            // release are heard once it works
            for (const char* band : { "Low", "Mid", "High" })
            {
                const auto t = std::string (band) + " threshold", r = std::string (band) + " ratio";
                out.push_back (named (std::string ("the ") + band + " band compressing (threshold −34 dB, ratio 31:1)", { { t.c_str(), 0.3f }, { r.c_str(), 1.0f } }));
            }
            break;
        case EffectKind::pocketTapeStop:
        {
            // (round 190cm) FLM's Tape Stop acts on an edge of Play while the song runs (spec-tapestop.md §3.2): the
            // times, Warp and Sync shape the ramp that edge starts; held on it passes the input, held off it is silent
            const auto play = plugui::paramIndex (kind, "Play");
            auto stop = named ("Sync on, Play switched off while the pad plays (the stop ramp)", { { "Sync", 1.0f } });
            stop.move = play, stop.moveTo = 0.0f;
            auto start = named ("Play off, switched on while the pad plays (the start ramp)", { { "Play", 0.0f } });
            start.move = play, start.moveTo = 1.0f;
            auto startSynced = named ("Sync on and Play off, switched on while the pad plays (the start ramp)", { { "Sync", 1.0f }, { "Play", 0.0f } });
            startSynced.move = play, startSynced.moveTo = 1.0f;
            out.push_back (stop), out.push_back (start), out.push_back (startSynced);
            break;
        }
        default:
            break;
    }
    return out;
}

inline double threshold (double noise) { return std::max (movedDb, noise + 10.0); }

inline Change measure (const Output& v, const Output& base, float value, std::string text)
{
    Change c;
    c.value = value, c.text = std::move (text);
    c.audio = audioDiffDb (v, base);
    c.send = relDiffDb (v.send, base.send, 1e-10);
    c.ctl = relDiffDb (v.ctl, base.ctl, 1e-6);
    c.band = bandDiffDb (octaveBands (v), octaveBands (base));
    c.stereo = stereoDiffDb (v, base);
    c.peak = peakOf (v), c.dc = dcOf (v), c.finite = v.finite;
    c.latency = v.latency != base.latency;
    return c;
}

inline double moveOf (const Change& c) { return std::max ({ c.audio, c.send, c.ctl, c.latency ? 0.0 : -300.0 }); }

inline std::string lower (std::string s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
    return s;
}

inline std::vector<float> valuesToTest (const Control& c, float current, bool quick)
{
    std::vector<float> vs;
    // (a phase knob wraps: its far end is its start, so the quick value is half a turn away)
    const bool phase = lower (c.name).find ("phase") != std::string::npos;
    if (c.type == Type::continuous && quick)
    {
        // the far end, and the near one too when the knob doesn't sit on it (a threshold's "off" end)
        vs.push_back (phase ? std::fmod (current + 0.5f, 1.0f) : current > 0.5f ? 0.0f : 1.0f);
        if (! phase && current > 0.02f && current < 0.98f)
            vs.push_back (current > 0.5f ? 1.0f : 0.0f);
    }
    else
        for (std::size_t i = 0; i < c.values.size(); ++i)
        {
            const auto v = c.values[i];
            if (std::abs (v - current) <= 1e-6f)
                continue;
            // (a choice option that reads as the current one is the current one)
            if (c.type != Type::continuous && ! c.momentary && i < c.texts.size() && i < c.values.size())
            {
                const auto near = std::min_element (c.values.begin(), c.values.end(), [current] (float a, float b) {
                    return std::abs (a - current) < std::abs (b - current);
                });
                if (c.texts[(std::size_t) (near - c.values.begin())] == c.texts[i] && std::abs (*near - current) < 0.5f / (float) c.values.size())
                    continue;
            }
            vs.push_back (v);
        }
    return vs;
}

// a render with control `c` at `played`'s value (a push button: pressed while the pad plays)
inline Output variant (const EffectSlot& ctx, const EffectSlot& played, const Control& c, const Programme& prog, RunOptions o = {})
{
    // the mixer builds a new instance for this edit (RackRenderer's reuse rule): Facet EQ's resolution
    if (ctx.kind == EffectKind::facetEq && c.param == facet::resolutionParam && ! c.state)
        return run (played, played, prog, o);
    if (! c.momentary)
        return run (ctx, played, prog, o);
    o.switchBlock = prog.padMiddle() / blockSize;
    o.after = &played;
    return run (ctx, ctx, prog, o);
}

// the best change of control `c` over its values, from context `ctx`
inline double bestMove (const EffectSlot& ctx, const Control& c, const Programme& prog, const RunOptions& o, double thr, bool quick,
                        std::vector<Change>* record = nullptr)
{
    const auto base = run (ctx, ctx, prog, o);
    double best = -300.0;
    for (const auto v : valuesToTest (c, valueOf (ctx, c.param), quick))
    {
        auto played = ctx;
        setValue (played, c.param, v);
        if (played.params == ctx.params && played.state == ctx.state)
            continue; // (a state control that can't be edited)
        const auto out = variant (ctx, played, c, prog, o);
        const auto ch = measure (out, base, v, textOf (ctx, c.param, v));
        best = std::max (best, moveOf (ch));
        if (record != nullptr)
            record->push_back (ch);
        if (record == nullptr && best > thr)
            break;
    }
    return best;
}


// controls that set what the editor shows or how the engine runs, not the sound (meters, displays, threads)
inline bool nonAudio (const std::string& name)
{
    const auto n = lower (name);
    for (const char* w : { "display", "meter", "vu ", "threaded", "loudness reset", "loudness target", "view", "analyzer" })
        if (n.find (w) != std::string::npos)
            return true;
    return n == "vu";
}

inline bool keepInContext (const std::string& name)
{
    const auto n = lower (name);
    for (const char* w : { "output", "out gain", "volume", "master", "mute", "bypass", "power", "dry/wet", "mix", "wet", "level" })
        if (n.find (w) != std::string::npos)
            return true;
    return false;
}

// The number of parameters a kind had when the last survey recorded its "rand:k" contexts (tests/FxAudit173dKnown.inc).
// A random context draws one value per control in the controls' order, so a control added later — in the middle of
// the editor, or before the parameters the editor doesn't show — would hand every control after it the next one's
// draw and the recorded contexts would mean other settings (round 190bb: LYNX Tune's Breathiness made "B dir" →
// rand:3 dead). Parameters from this index on draw after all the others. Regenerate the known keys and drop the
// entry after a new survey.
inline int surveyedParamCount (EffectKind kind)
{
    switch (kind)
    {
        case EffectKind::lynxTune: return 95; // round 190bb: + Breathiness (95)
        default: return 1 << 30;
    }
}

// the context a prerequisite key describes (see audit), nullopt = not usable here; `why` = its description
inline std::optional<EffectSlot> prerequisite (const std::string& key, const EffectSlot& ctx, const std::vector<Control>& controls,
                                               const Control& c, RunOptions& ro, std::string& why)
{
    if (key.rfind ("move:", 0) == 0)
    {
        const auto colon = key.find (':', 5);
        if (colon == std::string::npos)
            return std::nullopt;
        ro.moveParam = std::atoi (key.c_str() + 5), ro.moveValue = (float) std::atof (key.c_str() + colon + 1);
        if (ro.moveParam == c.param)
            return std::nullopt;
        why = std::string ("acts while ") + plugui::paramName (ctx.kind, ro.moveParam) + " moves";
        return ctx;
    }
    if (key.rfind ("bpm:", 0) == 0)
    {
        ro.bpm = std::atof (key.c_str() + 4), why = "another tempo (" + key.substr (4) + " BPM)";
        return ctx;
    }
    if (key == "hot")
    {
        ro.inputGain = 8.0f, why = "a hot input (+18 dB, peaks over 0 dBFS)";
        return ctx;
    }
    if (key == "sc")
    {
        ro.sidechain = true, why = "a side-chain input";
        return ctx;
    }
    if (key.rfind ("beat:", 0) == 0)
    {
        ro.startBeat = std::atof (key.c_str() + 5), why = "a later song position (beat " + key.substr (5) + ")";
        return ctx;
    }
    if (key.rfind ("hint:", 0) == 0)
    {
        const auto hints = hintsFor (ctx.kind);
        const auto h = (std::size_t) std::atoi (key.c_str() + 5);
        if (h >= hints.size())
            return std::nullopt;
        auto hc = ctx;
        for (const auto& [p, v] : hints[h].sets)
            if (p != c.param)
                setValue (hc, p, v);
        if (hints[h].edit)
            hints[h].edit (hc);
        why = hints[h].note;
        if (hints[h].bpm > 0.0)
            ro.bpm = hints[h].bpm;
        if (hints[h].sidechain)
            ro.sidechain = true;
        if (hints[h].move >= 0 && hints[h].move != c.param)
            ro.moveParam = hints[h].move, ro.moveValue = hints[h].moveTo;
        return hc;
    }
    if (key.rfind ("set:", 0) == 0)
    {
        const auto colon = key.find (':', 4);
        if (colon == std::string::npos)
            return std::nullopt;
        const int p = std::atoi (key.c_str() + 4);
        const float v = (float) std::atof (key.c_str() + colon + 1);
        if (p == c.param)
            return std::nullopt;
        auto one = ctx;
        setValue (one, p, v);
        why = std::string (p < plugui::paramCount (ctx.kind) ? plugui::paramName (ctx.kind, p) : "?") + " = " + textOf (one, p, v);
        return one;
    }
    if (key.rfind ("rand:", 0) == 0)
    {
        const int want = std::atoi (key.c_str() + 5);
        std::uint32_t seed = 173u + (std::uint32_t) c.param * 7919u + (std::uint32_t) want * 104729u;
        auto rnd = [&seed] {
            seed = seed * 1664525u + 1013904223u;
            return (float) ((seed >> 8) & 0xffffff) / 16777216.0f;
        };
        auto rc = ctx;
        const auto surveyed = surveyedParamCount (ctx.kind);
        for (const bool late : { false, true }) // (the parameters added since the survey draw last)
            for (const auto& o : controls)
            {
                if (o.param == c.param || keepInContext (o.name) || (o.param >= surveyed) != late)
                    continue;
                const auto v = o.type == Type::continuous ? rnd() : o.values[(std::size_t) std::min ((int) o.values.size() - 1, (int) (rnd() * (float) o.values.size()))];
                setValue (rc, o.param, v);
            }
        why = "a combination of other settings (random context " + key.substr (5) + ")";
        return rc;
    }
    return std::nullopt;
}

inline KindReport audit (const EffectSlot& slot0, const Programme& prog, const Options& opt = {})
{
    KindReport rep;
    rep.kind = slot0.kind;
    rep.name = effectInfo (slot0.kind).name;
    rep.skipped = skipReason (slot0.kind);
    if (rep.skipped != nullptr)
        return rep;
    auto controls = controlsOf (slot0);
    const auto allControls = controls; // (the prerequisite search looks at all of them)
    rep.listed = (int) controls.size();
    // big state tables (EQUO's 837 band cells, the delay banks' 200+ controls): every own parameter, and an even
    // sample of the state controls up to opt.maxControls
    if ((int) controls.size() > opt.maxControls)
    {
        std::vector<Control> own, states, keep;
        for (auto& c : controls)
            (c.state ? states : own).push_back (std::move (c));
        const int room = std::max (8, opt.maxControls - (int) own.size());
        keep = std::move (own);
        for (int k = 0; k < room && ! states.empty(); ++k)
        {
            const auto at = (std::size_t) ((double) k * (double) states.size() / room);
            if (at < states.size() && (keep.empty() || keep.back().param != states[at].param))
                keep.push_back (states[at]);
        }
        controls = std::move (keep);
    }
    const auto base = run (slot0, slot0, prog);
    rep.noise = audioDiffDb (run (slot0, slot0, prog), base);
    rep.noise = std::max ({ rep.noise, relDiffDb (run (slot0, slot0, prog).ctl, base.ctl, 1e-6) });
    rep.peak = peakOf (base);
    const bool sound = ! isNoSound (slot0.kind);
    auto check = [&rep, sound, &prog] (const Output& o, const std::string& ctl, const std::string& text, double value, bool full) {
        if (! o.finite)
            rep.broken.push_back ({ ctl, text, "NaN", value });
        if (! full || ! o.finite)
            return;
        double sendPeak = 0.0;
        for (const auto v : o.send)
            sendPeak = std::max (sendPeak, (double) std::abs (v));
        const auto p = peakOf (o);
        if (sound && p < 1e-5 && sendPeak < 1e-5)
            rep.broken.push_back ({ ctl, text, "silent", p });
        if (p > 2.0)
            rep.broken.push_back ({ ctl, text, "clip", 20.0 * std::log10 (p) });
        // (the programme's own mean, a kick's asymmetric decay, is not an offset the effect made)
        double inL = 0.0, inR = 0.0;
        for (int i = 0; i < prog.frames(); ++i)
            inL += prog.l[(std::size_t) i], inR += prog.r[(std::size_t) i];
        const auto inDc = std::max (std::abs (inL), std::abs (inR)) / std::max (1, prog.frames());
        if (dcOf (o) > inDc + 0.02)
            rep.broken.push_back ({ ctl, text, "dc", dcOf (o) });
    };
    check (base, "(default)", "", 0.0, true);
    const auto thr = threshold (rep.noise);
    for (const auto& c : controls)
    {
        if (! opt.only.empty() && std::find (opt.only.begin(), opt.only.end(), c.name) == opt.only.end())
            continue;
        ControlResult res;
        res.control = c;
        // as the editor shows it: its conditions met, its idle switch off
        auto ctx = slot0;
        auto satisfy = [&controls, &ctx] (const Condition& k, bool inside) {
            if (k.holds (valueOf (ctx, k.param)) == inside)
                return;
            const Control* owner = nullptr;
            for (const auto& o : controls)
                if (o.param == k.param)
                    owner = &o;
            std::vector<float> cands = owner != nullptr ? owner->values : std::vector<float> {};
            for (float v = 0.0f; v <= 1.0001f; v += 0.125f)
                cands.push_back (std::min (v, 1.0f));
            for (const auto v : cands)
                if (k.holds (v) == inside)
                {
                    setValue (ctx, k.param, v);
                    return;
                }
        };
        for (const auto& k : c.shownWhen)
            satisfy (k, true);
        for (const auto& k : c.idleWhen)
            satisfy (k, false);
        const bool contextual = ctx.params != slot0.params || ctx.state != slot0.state;
        const auto cbase = contextual ? run (ctx, ctx, prog) : base;
        const auto current = valueOf (ctx, c.param);
        for (const auto v : valuesToTest (c, current, opt.quick))
        {
            auto played = ctx;
            setValue (played, c.param, v);
            const auto text = textOf (ctx, c.param, v);
            const auto out = variant (ctx, played, c, prog);
            auto ch = measure (out, cbase, v, text);
            res.best = std::max (res.best, moveOf (ch));
            res.changes.push_back (ch);
            check (out, c.name, text, v, c.type != Type::continuous && ! c.stepped);
            if (opt.clicks && c.type != Type::continuous && ! c.stepped && ! c.momentary && ! c.state && out.finite)
            {
                // switched while the pad plays: the step at the switch against both modes' own steps there
                RunOptions ro;
                ro.switchBlock = prog.padMiddle() / blockSize;
                ro.after = &played;
                const auto sw = run (ctx, ctx, prog, ro);
                const int at = ro.switchBlock * blockSize + std::max (0, out.latency);
                const double jump = maxStep (sw, at - 64, at + 512);
                const double own = std::max (maxStep (cbase, at - 2048, at + 2048), maxStep (out, at - 2048, at + 2048));
                if (jump > 0.05 && jump > 3.0 * own)
                    rep.broken.push_back ({ c.name, text, "click", 20.0 * std::log10 (jump / std::max (own, 1e-9)) });
            }
        }
        if (! c.shownWhen.empty() || ! c.idleWhen.empty())
            if (contextual && res.best > thr)
                res.status = Status::dependent, res.prerequisite = "as the editor shows it";
        if (res.best <= thr && nonAudio (c.name))
            res.status = Status::nonAudio;
        else if (res.best <= thr)
        {
            // a new instance (the setting may be read only when the effect is built)
            for (const auto v : valuesToTest (c, current, opt.quick))
            {
                auto played = ctx;
                setValue (played, c.param, v);
                if (moveOf (measure (run (played, played, prog), cbase, v, "")) > thr)
                    res.onlyNewInstance = true;
            }
            // the prerequisites to try, in order: the one a previous survey found (opt.known), side-chain inputs, a
            // later song position (tempo-locked patterns longer than the programme), the kind's hints, one other
            // control alone (each option of the choices and toggles, the ends of the knobs), random settings
            std::vector<std::string> keys;
            if (opt.known)
                if (auto k = opt.known (rep.name, c.name); ! k.empty())
                    keys.push_back (k);
            if (! opt.inert || ! opt.inert (rep.name, c.name))
            {
                keys.push_back ("sc");
                keys.push_back ("hot");
                keys.push_back ("bpm:90");
                for (const int beat : { 2, 4, 8, 16, 32, 64 })
                    keys.push_back ("beat:" + std::to_string (beat));
                const auto hints = opt.hints.empty() ? hintsFor (slot0.kind) : opt.hints;
                for (std::size_t h = 0; h < hints.size(); ++h)
                    keys.push_back ("hint:" + std::to_string (h));
                // switches and modes first, and the controls of the same group ("B3 …", "Unit 2 …", "Delay …") before
                // the others
                auto group = [] (const std::string& n) { return n.substr (0, n.find (' ')); };
                std::vector<const Control*> order;
                for (const auto& o : allControls)
                    if (o.param != c.param)
                        order.push_back (&o);
                std::stable_sort (order.begin(), order.end(), [&] (const Control* a, const Control* b) {
                    const auto ka = (group (a->name) == group (c.name) ? 0 : 2) + (a->type == Type::continuous ? 1 : 0);
                    const auto kb = (group (b->name) == group (c.name) ? 0 : 2) + (b->type == Type::continuous ? 1 : 0);
                    return ka < kb;
                });
                int budget = opt.searchBudget;
                for (const auto* op : order)
                {
                    const auto& o = *op;
                    for (const auto v : o.type == Type::continuous ? std::vector<float> { 0.0f, 1.0f } : o.values)
                        if (std::abs (v - valueOf (ctx, o.param)) >= 1e-6f && budget-- > 0)
                        {
                            char b[64];
                            std::snprintf (b, sizeof b, "set:%d:%.9g", o.param, (double) v);
                            keys.push_back (b);
                        }
                }
                int moves = opt.searchBudget / 2;
                for (const auto& o : controls)
                    if (o.param != c.param && o.type == Type::continuous && ! o.state && moves-- > 0)
                    {
                        char b[64];
                        std::snprintf (b, sizeof b, "move:%d:%.9g", o.param, valueOf (ctx, o.param) > 0.5f ? 0.0 : 1.0);
                        keys.push_back (b);
                    }
                for (int k = 0; k < opt.contexts; ++k)
                    keys.push_back ("rand:" + std::to_string (k));
            }
            for (const auto& key : keys)
            {
                RunOptions ro;
                std::string why;
                const auto from = prerequisite (key, ctx, allControls, c, ro, why);
                if (! from)
                    continue;
                const auto moved = bestMove (*from, c, prog, ro, thr, true);
                if (std::getenv ("LYNX_FXAUDIT_DEBUG") != nullptr)
                    std::printf ("    [%s] %s: %s → %.0f dB\n", c.name.c_str(), key.c_str(), why.c_str(), moved);
                if (moved > thr)
                {
                    res.status = Status::dependent, res.prerequisite = why, res.prereqKey = key;
                    // report the move measured in the prerequisite's context (every value), not the default one's
                    res.bestDefault = res.best;
                    std::vector<Change> there;
                    res.best = bestMove (*from, c, prog, ro, thr, opt.quick, &there);
                    res.changes = std::move (there);
                    break;
                }
            }
            if (res.status != Status::dependent)
                res.status = Status::dead;
        }
        rep.controls.push_back (std::move (res));
    }
    return rep;
}

inline const char* statusText (Status s)
{
    return s == Status::ok ? "ok" : s == Status::dependent ? "dependent-ok" : s == Status::nonAudio ? "non-audio" : "DEAD";
}
} // namespace lynx::fxaudit
