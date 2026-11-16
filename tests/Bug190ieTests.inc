// Round 190ie (issue #185): "When adding automation clips for the BPM, instruments using the Delay effect become
// distorted. Doesn't happen with Reverb." The renderer hands every effect the tempo of its block (RackRenderer: the
// tempo holds for the whole block, evaluated at its start), so a tempo clip steps the tempo a little at every block.
// The basic Delay turned each step into a jump of its integer read position: a discontinuity per block (a crackle at the
// block rate), fed back into the line by the feedback. FL's own delay (Fruity Delay 3, REA_rodada52 §3.2,
// docs/analise/REA_rodada190ie.md) glides its tap lengths toward the new length and reads between samples: the echoes
// bend in pitch like a tape, without a click. The basic Delay now does the same on a tempo change; at a steady tempo it
// renders bit for bit what it did before.
//
// The measure: Δ⁴ (the fourth difference) of the output, a high-pass that leaves a smooth chord at about −120 dB and
// turns a step of size a into peaks of about 6a. A tempo ramp must not raise it over the same effect at a steady tempo.
// LYNX_SURVEY_190IE=1 also runs the survey of every built-in effect (default settings and, where the effect has one,
// its tempo sync switched on) and prints the table the round's HANDOFF quotes.

#include <cstring>

#include "../tools/FxAudit.h"

namespace bug190ie
{
constexpr double pi = 3.14159265358979323846;

// a smooth programme: a three-note chord, notes 0.3 s on / 0.2 s off with 15 ms raised-cosine edges (no transients of its own)
inline std::vector<float> chord (int n, double rate)
{
    std::vector<float> x ((std::size_t) n, 0.0f);
    const int on = (int) (0.3 * rate), period = (int) (0.5 * rate), edge = (int) (0.015 * rate);
    for (int i = 0; i < n; ++i)
    {
        const int k = i % period;
        if (k >= on)
            continue;
        const double env = k < edge ? 0.5 - 0.5 * std::cos (pi * k / edge) : k > on - edge ? 0.5 - 0.5 * std::cos (pi * (on - k) / edge) : 1.0;
        const double t = i / rate;
        x[(std::size_t) i] = (float) (env * 0.2 * (std::sin (2 * pi * 220.0 * t) + std::sin (2 * pi * 277.18 * t) + std::sin (2 * pi * 329.63 * t)));
    }
    return x;
}

struct Render
{
    std::vector<float> l, r;
    bool finite = true;
};

// `bpmAt (block start, frames)` gives each block's tempo (held for the block, as the renderer does)
template <typename Tempo>
Render render (const lynx::EffectSlot& slot0, const std::vector<float>& in, double rate, int block, Tempo bpmAt)
{
    Render out;
    auto slot = slot0;
    auto fx = lynx::makeEffect (slot);
    if (fx == nullptr)
        return out;
    fx->prepare (rate);
    const int n = (int) in.size();
    out.l = in;
    out.r = in;
    double tick = 0.0;
    for (int at = 0; at < n; at += block)
    {
        const int len = std::min (block, n - at);
        const double bpm = bpmAt (at, n);
        fx->setSongPosition (tick);
        fx->setPlaying (true);
        fx->process (out.l.data() + at, out.r.data() + at, len, slot.params, bpm);
        tick += len * bpm / 60.0 * lynx::ppq / rate;
    }
    for (int i = 0; i < n && out.finite; ++i)
        out.finite = std::isfinite (out.l[(std::size_t) i]) && std::isfinite (out.r[(std::size_t) i]);
    return out;
}

struct Hf
{
    double energy = 0.0, peak = 0.0, level = 0.0; // Δ⁴ energy and peak; the output's energy
};

inline Hf hf (const Render& r, int from = 0)
{
    Hf h;
    for (const auto* x : { &r.l, &r.r })
        for (std::size_t i = (std::size_t) from + 4; i < x->size(); ++i)
        {
            const double d = (double) (*x)[i] - 4.0 * (*x)[i - 1] + 6.0 * (*x)[i - 2] - 4.0 * (*x)[i - 3] + (double) (*x)[i - 4];
            h.energy += d * d;
            h.peak = std::max (h.peak, std::abs (d));
            h.level += (double) (*x)[i] * (*x)[i];
        }
    return h;
}

inline double db (double ratio) { return 10.0 * std::log10 (std::max (ratio, 1e-30)); }

// FNV-1a over the float bit patterns of both channels
inline std::uint64_t hashOf (const Render& r)
{
    std::uint64_t h = 1469598103934665603ull;
    for (const auto* x : { &r.l, &r.r })
        for (const auto v : *x)
        {
            std::uint32_t bits;
            std::memcpy (&bits, &v, 4);
            for (int b = 0; b < 4; ++b)
                h = (h ^ ((bits >> (8 * b)) & 0xffu)) * 1099511628211ull;
        }
    return h;
}

struct Result
{
    double excessDb = 0.0, peakExcessDb = 0.0, tempoDepDb = -300.0;
    double steadyPeakDb = -300.0, rampPeakDb = -300.0; // Δ⁴ peaks (dBFS)
    std::uint64_t steadyHash = 0;                      // the steady 120 BPM render
    bool finite = true, ran = false;
};

// steady 120 and 140 BPM against a ramp 120 → 140 over the render (blocks of `block`)
// (survey only: LYNX_SURVEY_190IE_DUMP=<folder> writes each row's steady and ramp renders there as raw floats, L then R,
// to compare builds; `dumpName` names the row)
inline std::string dumpName;
inline void dump (const char* what, const std::vector<float>& l, const std::vector<float>& r)
{
    const char* dir = std::getenv ("LYNX_SURVEY_190IE_DUMP");
    if (dir == nullptr || dumpName.empty())
        return;
    if (auto* f = std::fopen ((std::string (dir) + "/" + dumpName + "_" + what + ".f32").c_str(), "wb"))
    {
        std::fwrite (l.data(), sizeof (float), l.size(), f);
        std::fwrite (r.data(), sizeof (float), r.size(), f);
        std::fclose (f);
    }
}

inline Result rampVsSteady (const lynx::EffectSlot& slot, double rate, int block, double seconds)
{
    Result res;
    const auto in = chord ((int) (seconds * rate), rate);
    const auto a = render (slot, in, rate, block, [] (int, int) { return 120.0; });
    if (a.l.empty())
        return res;
    const auto c = render (slot, in, rate, block, [] (int, int) { return 140.0; });
    const auto ramp = render (slot, in, rate, block, [] (int at, int n) { return 120.0 + 20.0 * at / n; });
    res.ran = true;
    dump ("steady", a.l, a.r);
    dump ("ramp", ramp.l, ramp.r);
    res.steadyHash = hashOf (a);
    res.finite = a.finite && c.finite && ramp.finite;
    const auto ha = hf (a), hc = hf (c), hr = hf (ramp);
    const double base = std::max (ha.energy, hc.energy), basePeak = std::max (ha.peak, hc.peak);
    res.excessDb = db ((hr.energy + 1e-24) / (base + 1e-24));
    res.peakExcessDb = 2.0 * db ((hr.peak + 1e-12) / (basePeak + 1e-12));
    res.steadyPeakDb = 2.0 * db (basePeak + 1e-15), res.rampPeakDb = 2.0 * db (hr.peak + 1e-15);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < a.l.size(); ++i)
    {
        diff += (double) (a.l[i] - c.l[i]) * (a.l[i] - c.l[i]) + (double) (a.r[i] - c.r[i]) * (a.r[i] - c.r[i]);
        ref += (double) a.l[i] * a.l[i] + (double) a.r[i] * a.r[i];
    }
    res.tempoDepDb = db ((diff + 1e-30) / (ref + 1e-30));
    return res;
}

inline lynx::EffectSlot delaySlot (float time, float feedback, float mix, float pingPong, float damping)
{
    auto s = lynx::makeEffectSlot (lynx::EffectKind::delay);
    s.params[0] = time, s.params[1] = feedback, s.params[2] = mix, s.params[3] = pingPong, s.params[4] = damping;
    return s;
}

inline bool wantsSync (std::string name)
{
    for (auto& ch : name)
        ch = (char) std::tolower ((unsigned char) ch);
    return name.find ("sync") != std::string::npos; // (not "Tempo source": that one picks the effect's own BPM)
}
// 0.1.28's basic Delay loop, verbatim (the reference of the steady-tempo test: the same arithmetic, so the comparison
// holds on every platform and libm)
struct OldDelay
{
    std::array<std::vector<float>, 2> buffer;
    std::size_t write = 0;
    float damp[2] {};
    double sampleRate = 44100.0;
    explicit OldDelay (double sr) : sampleRate (sr)
    {
        buffer[0].assign ((std::size_t) (3.0 * 192000.0) + 4, 0.0f);
        buffer[1].assign ((std::size_t) (3.0 * 192000.0) + 4, 0.0f);
    }
    void process (float* l, float* r, int n, const lynx::EffectParams& params, double bpm) noexcept
    {
        static constexpr std::array<double, 8> divisions { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        const auto beats = divisions[(std::size_t) std::clamp ((int) (params[0] * 8.0f), 0, 7)];
        const auto seconds = std::min (3.0 - 0.01, beats * 60.0 / std::max (bpm, 20.0));
        const auto delaySamples = std::max<std::size_t> (1, (std::size_t) (seconds * sampleRate));
        const auto feedback = params[1] * 0.95f;
        const auto mix = params[2];
        const auto pingPong = params[3] > 0.5f;
        const auto dampCoeff = 1.0f - params[4] * 0.9f;
        const auto size = buffer[0].size();
        for (int i = 0; i < n; ++i)
        {
            const auto readPos = (write + size - delaySamples) % size;
            auto echoL = buffer[0][readPos];
            auto echoR = buffer[1][readPos];
            damp[0] += dampCoeff * (echoL - damp[0]);
            damp[1] += dampCoeff * (echoR - damp[1]);
            echoL = damp[0];
            echoR = damp[1];
            const auto inL = l[i], inR = r[i];
            if (pingPong)
            {
                buffer[0][write] = 0.5f * (inL + inR) + echoR * feedback;
                buffer[1][write] = echoL * feedback;
            }
            else
            {
                buffer[0][write] = inL + echoL * feedback;
                buffer[1][write] = inR + echoR * feedback;
            }
            l[i] = inL + mix * echoL;
            r[i] = inR + mix * echoR;
            write = (write + 1) % size;
        }
    }
};
// 0.1.28's Pocket Trance Delay (FLM's rings as long as the delay), verbatim: the reference of the steady-tempo test
struct OldTranceDelay
{
    double fs = 44100.0, lastBeats = -1.0;
    int capacity = 0, pl = 0, pr = 0, pp = 0, lastMode = -1;
    std::vector<float> lineL, lineR, lineP;
    float zl = 0.0f, zr = 0.0f, pan = -1.0f;
    explicit OldTranceDelay (double sampleRate)
    {
        fs = sampleRate;
        capacity = std::max (1, (int) (2.0 * fs));
        lineL.assign ((std::size_t) capacity, 0.0f);
        lineR.assign ((std::size_t) capacity, 0.0f);
        lineP.assign ((std::size_t) capacity, 0.0f);
    }
    void process (float* left, float* right, int n, const lynx::EffectParams& p, double bpm) noexcept
    {
        static constexpr double beatsOf[5] { 0.25, 0.375, 0.5, 0.75, 1.0 };
        const auto k = (int) (p[0] * 4.0f);
        const auto beats = k >= 1 && k <= 4 ? beatsOf[k] : 0.25;
        const auto mode = std::clamp ((int) (p[4] * 2.0f + 0.5f), 0, 2);
        lastBeats = beats, lastMode = mode; // (steady: never changes)
        const auto fb = p[1] * 0.99f;
        const auto wet = p[2] * p[2];
        const auto c = 2.0f * p[3];
        const auto lowPass = c <= 1.0f;
        const auto kf = lowPass ? c : (c - 1.0f) * (c - 1.0f) * (c - 1.0f) * (c - 1.0f);
        const auto d = beats * fs * (60.0 / (bpm > 0.0 ? bpm : 120.0));
        auto lenLR = mode == 2 ? (int) (d + d) : (int) d;
        auto lenP = mode == 2 ? (int) d : 0;
        lenLR = std::min (lenLR, capacity);
        lenP = std::min (lenP, capacity);
        const auto filter = [&] (float x, float& z) {
            const auto e = x + 1e-30f;
            z = z + (e - z) * kf;
            return lowPass ? z : e - z;
        };
        const auto target = p[5];
        const auto dryPan = p[6] >= 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const auto inL = left[i], inR = right[i];
            float outL, outR;
            if (mode == 1)
            {
                if (pan < 0.0f)
                    pan = target;
                else if (pan != target)
                {
                    pan = pan + (target - pan) * 0.01f;
                    if (std::abs ((double) pan - (double) target) < 0.001)
                        pan = target;
                }
                const auto gR = std::clamp (2.0f * pan, 0.0f, 1.0f), gL = std::clamp (2.0f * (1.0f - pan), 0.0f, 1.0f);
                const auto aL = gL * inL, aR = gR * inR;
                if (pl >= lenLR)
                    pl = 0;
                const auto fl = filter (lineL[(std::size_t) pl], zl);
                outL = wet * fl + (dryPan ? aL : inL);
                if (pr >= lenLR)
                    pr = 0;
                const auto fr = filter (lineR[(std::size_t) pr], zr);
                outR = wet * fr + (dryPan ? aR : inR);
                lineR[(std::size_t) pr] = (fl + aL) * fb;
                lineL[(std::size_t) pl] = (fr + aR) * fb;
                ++pl;
                ++pr;
            }
            else
            {
                lineP[(std::size_t) pp] = inL;
                if (++pp >= lenP)
                    pp = 0;
                const auto xl = lineP[(std::size_t) pp], xr = inR;
                if (pl >= lenLR)
                    pl = 0;
                const auto fl = filter (lineL[(std::size_t) pl], zl);
                outL = fl * wet + xl;
                lineL[(std::size_t) pl] = (xl + fl) * fb;
                ++pl;
                if (pr >= lenLR)
                    pr = 0;
                const auto fr = filter (lineR[(std::size_t) pr], zr);
                outR = fr * wet + xr;
                lineR[(std::size_t) pr] = (xr + fr) * fb;
                ++pr;
            }
            left[i] = outL;
            right[i] = outR;
        }
    }
};
} // namespace bug190ie

// The report's case: the basic Delay (default settings, a long darker ping-pong, a short one with high feedback) under a
// tempo ramp 120 → 140 BPM, at 44.1 and 48 kHz with the block sizes of common devices. Before the fix the ramp raised
// the Δ⁴ energy by 60-81 dB, with peaks of −13 … −35 dBFS (a click at every block); now the energy stays within 3 dB of
// the steady tempo's and the peaks under −85 dBFS (the glide's own pitch bend).
TEST (bug190ie_delay_tempo_ramp_has_no_clicks)
{
    using namespace bug190ie;
    const lynx::EffectSlot slots[] { lynx::makeEffectSlot (lynx::EffectKind::delay), delaySlot (0.55f, 0.8f, 0.6f, 1.0f, 0.6f),
                                     delaySlot (0.05f, 0.9f, 0.5f, 0.0f, 0.0f) };
    for (const auto& slot : slots)
        for (const auto rate : { 44100.0, 48000.0 })
            for (const auto block : { 64, 256, 512 })
            {
                const auto r = rampVsSteady (slot, rate, block, 4.0);
                std::printf ("  basic Delay time %.2f fb %.2f pp %.0f, %.0f Hz, block %d: ramp Δ⁴ energy %+.1f dB, peak %+.1f dB vs steady (%.1f / %.1f dBFS)\n",
                             slot.params[0], slot.params[1], slot.params[3], rate, block, r.excessDb, r.peakExcessDb, r.rampPeakDb, r.steadyPeakDb);
                CHECK (r.ran && r.finite);
                CHECK (r.excessDb < 3.0);      // 0.1.28: +60 … +81 dB
                CHECK (r.rampPeakDb < -85.0);  // 0.1.28: −13 … −35 dBFS (a click at every block); steady ≈ −108
            }
}

// At a steady tempo the basic Delay renders exactly what 0.1.28 rendered: bit for bit against 0.1.28's loop (OldDelay),
// across rates, tempos, block sizes and every switch, with GCC and clang alike. (Until round 190ie's follow-up,
// core/PitcherDsp.h ended on "#pragma clang fp contract(on)" and clang fused multiply-adds in the rest of
// core/Effects.cpp, this Delay included, on FMA targets; the header now restores the includer's state.) Hashes with
// both compilers: 0x5e06c6510f0a515b 0x5500a6eb0c9d8b30 0x0fa965b2708500a3 0xc15f652d2dc1a417.
TEST (bug190ie_delay_steady_tempo_bit_identical)
{
    using namespace bug190ie;
    struct Case
    {
        lynx::EffectSlot slot;
        double rate, bpm;
        int block;
    };
    const Case cases[] {
        { lynx::makeEffectSlot (lynx::EffectKind::delay), 44100.0, 120.0, 512 },
        { delaySlot (0.55f, 0.8f, 0.6f, 1.0f, 0.6f), 48000.0, 128.0, 256 },
        { delaySlot (0.05f, 0.9f, 0.5f, 0.0f, 0.0f), 96000.0, 87.5, 333 },
        { delaySlot (0.99f, 0.95f, 1.0f, 0.0f, 1.0f), 44100.0, 60.0, 1024 },
    };
    for (const auto& c : cases)
    {
        const auto in = chord ((int) (3.0 * c.rate), c.rate);
        const auto r = render (c.slot, in, c.rate, c.block, [&c] (int, int) { return c.bpm; });
        Render ref { in, in, true };
        OldDelay old (c.rate);
        for (int at = 0; at < (int) in.size(); at += c.block)
            old.process (ref.l.data() + at, ref.r.data() + at, std::min (c.block, (int) in.size() - at), c.slot.params, c.bpm);
        std::printf ("  basic Delay steady %.1f BPM %.0f Hz block %d: hash 0x%016llx, 0.1.28's loop 0x%016llx\n", c.bpm, c.rate, c.block,
                     (unsigned long long) hashOf (r), (unsigned long long) hashOf (ref));
        CHECK (r.l == ref.l && r.r == ref.r);
    }
}

// A tempo step (120 → 140 at 1 s) glides the delay to the new length and then snaps onto it: an impulse 2 s later
// echoes exactly 0.75 beats (the default 1/8 dotted) at 140 BPM later, on the same sample as a delay that ran at 140
// from the start; and a change of the Time knob still jumps at once, as before (only the tempo glides).
TEST (bug190ie_delay_tempo_step_settles_on_the_new_length)
{
    using namespace bug190ie;
    const double rate = 48000.0;
    auto slot = delaySlot (0.3f, 0.0f, 1.0f, 0.0f, 0.0f); // no feedback, no damping: one echo, unfiltered
    const int n = (int) (4.0 * rate), impulseAt = (int) (3.0 * rate);
    std::vector<float> in ((std::size_t) n, 0.0f);
    in[(std::size_t) impulseAt] = 1.0f;
    const auto stepped = render (slot, in, rate, 256, [] (int at, int) { return at < 48000 ? 120.0 : 140.0; });
    const auto steady = render (slot, in, rate, 256, [] (int, int) { return 140.0; });
    const auto expected = (std::size_t) (0.75 * 60.0 / 140.0 * rate); // (the delay's own truncation)
    int echoAt = -1;
    for (std::size_t i = (std::size_t) impulseAt + 1; i < stepped.l.size(); ++i)
        if (std::abs (stepped.l[i]) > 0.5f)
        {
            echoAt = (int) (i - (std::size_t) impulseAt);
            break;
        }
    std::printf ("  tempo step 120 → 140: echo after %d samples (expected %zu)\n", echoAt, expected);
    CHECK (echoAt == (int) expected);
    bool same = true;
    for (std::size_t i = (std::size_t) impulseAt; i < stepped.l.size(); ++i)
        same = same && stepped.l[i] == steady.l[i] && stepped.r[i] == steady.r[i];
    CHECK (same);

    // the Time knob moved at a steady tempo: the read position jumps at once (0.1.28's behaviour, kept)
    auto fx = lynx::makeEffect (slot);
    fx->prepare (rate);
    std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
    l[(std::size_t) impulseAt] = r[(std::size_t) impulseAt] = 1.0f;
    for (int at = 0; at < n; at += 256)
    {
        auto p = slot.params;
        p[0] = at < impulseAt - 4800 ? 0.3f : 0.45f; // 1/8 dotted → 1/4 shortly before the impulse
        fx->process (l.data() + at, r.data() + at, std::min (256, n - at), p, 120.0);
    }
    const auto quarter = (std::size_t) (1.0 * 60.0 / 120.0 * rate);
    CHECK (std::abs (l[(std::size_t) impulseAt + quarter] - 1.0f) < 1e-6f);
}

// Every built-in effect under the same ramp (survey; LYNX_SURVEY_190IE=1): default settings, and with its tempo sync
// switched on where it has such a control. Prints one line per effect; the permanent checks are only "finite".
TEST (bug190ie_survey_tempo_ramp_all_effects)
{
    using namespace bug190ie;
    const char* env = std::getenv ("LYNX_SURVEY_190IE");
    if (env == nullptr || env[0] != '1')
        return;
    const double rate = 48000.0;
    const char* names = std::getenv ("LYNX_SURVEY_190IE_NAMES"); // (optional: a comma list of effect names)
    std::printf ("  %-28s %-8s %9s %9s %9s %11s %11s\n", "effect", "variant", "tempo dB", "Δ⁴ dB", "peak dB", "ramp dBFS", "steady dBFS");
    std::printf ("  (then the steady 120 BPM render's hash)\n");
    for (int k = 0; k <= (int) lynx::lastEffectKind; ++k)
    {
        const auto kind = (lynx::EffectKind) k;
        if (lynx::fxaudit::skipReason (kind) != nullptr || lynx::fxaudit::isNoSound (kind) || kind == lynx::EffectKind::rackMixer)
            continue;
        if (names != nullptr && std::string (",") .append (names).append (",").find (std::string (",") + lynx::effectInfo (kind).name + ",") == std::string::npos)
            continue;
        const auto base = lynx::makeEffectSlot (kind);
        std::vector<std::pair<std::string, lynx::EffectSlot>> variants { { "default", base } };
        auto synced = base;
        bool any = false;
        for (const auto& c : lynx::fxaudit::controlsOf (base))
            if (wantsSync (c.name) && lynx::fxaudit::valueOf (synced, c.param) < 0.5f)
            {
                lynx::fxaudit::setValue (synced, c.param, 1.0f);
                any = true;
            }
        if (any)
            variants.push_back ({ "sync", synced });
        for (const auto& [name, slot] : variants)
        {
            dumpName = std::to_string (k) + "_";
            for (const char* c = lynx::effectInfo (kind).name; *c != 0; ++c)
                dumpName += std::isalnum ((unsigned char) *c) ? *c : '-';
            dumpName += "_" + name;
            const auto r = rampVsSteady (slot, rate, 256, 3.0);
            dumpName.clear();
            if (! r.ran)
                continue;
            std::printf ("  %-28s %-8s %9.1f %9.1f %9.1f %11.1f %11.1f  %016llx%s%s\n", lynx::effectInfo (kind).name, name.c_str(), r.tempoDepDb, r.excessDb, r.peakExcessDb, r.rampPeakDb, r.steadyPeakDb, (unsigned long long) r.steadyHash,
                         r.finite ? "" : "  NOT FINITE", r.tempoDepDb > -60.0 && (r.excessDb > 6.0 || r.peakExcessDb > 6.0) ? "  <-- clicks" : "");
            CHECK (r.finite);
        }
    }
}

// Pocket Trance Delay (FL Studio Mobile's Trance Delay) under the same ramp: 0.1.28 re-cut FLM's rings at every block
// (Δ⁴ peaks of −3 … +5 dBFS, the loudest clicks of the survey); now it glides like the Delay. At a steady tempo it
// renders 0.1.28's samples bit for bit in its three modes (against 0.1.28's loop, OldTranceDelay).
TEST (bug190ie_pocket_trance_delay_tempo)
{
    using namespace bug190ie;
    for (const auto mode : { 0.0f, 0.5f, 1.0f })
    {
        auto slot = lynx::makeEffectSlot (lynx::EffectKind::pocketTranceDelay);
        slot.params[4] = mode;
        slot.params[1] = 0.7f;
        for (const auto block : { 64, 512 })
        {
            const auto r = rampVsSteady (slot, 48000.0, block, 4.0);
            std::printf ("  Pocket Trance Delay mode %.1f, block %d: ramp Δ⁴ energy %+.1f dB, peak %.1f dBFS (steady %.1f)\n", mode, block,
                         r.excessDb, r.rampPeakDb, r.steadyPeakDb);
            CHECK (r.ran && r.finite);
            CHECK (r.excessDb < 15.0);     // 0.1.28: +79 … +83 dB (now +2 … +12, Mirror's two glides the most)
            CHECK (r.rampPeakDb < -75.0);  // 0.1.28: +3 … +5 dBFS; now −79 … −94
        }
        for (const auto& [rate, bpm, block] : { std::tuple { 44100.0, 120.0, 512 }, std::tuple { 48000.0, 97.0, 333 }, std::tuple { 96000.0, 174.0, 64 } })
        {
            for (const auto time : { 0.0f, 0.3f, 0.775f, 1.0f })
            {
                auto s = slot;
                s.params[0] = time;
                const auto in = chord ((int) (3.0 * rate), rate);
                const auto now = render (s, in, rate, block, [bpm] (int, int) { return bpm; });
                Render ref { in, in, true };
                OldTranceDelay old (rate);
                for (int at = 0; at < (int) in.size(); at += block)
                    old.process (ref.l.data() + at, ref.r.data() + at, std::min (block, (int) in.size() - at), s.params, bpm);
                CHECK (now.l == ref.l && now.r == ref.r);
            }
        }
    }
}

// Nebula with its delay synced to the tempo (both engines: the cloud networks of new instances and the classic one of
// older projects): the lines' length change of a tempo step now spreads over the block instead of the fixed glide's
// burst of 0.25 sample per sample (0.1.28: Δ⁴ peaks of −31 … −46 dBFS under the ramp, against −87 … −111 steady).
TEST (bug190ie_nebula_synced_tempo)
{
    using namespace bug190ie;
    for (const auto law : { 1.0f, 0.0f })
        for (const auto mod : { 0.5f, 0.0f })
        {
            auto slot = lynx::makeEffectSlot (lynx::EffectKind::nebula);
            slot.params[1] = 0.3f; // Sync: straight notes
            slot.params[11] = mod;
            slot.params[14] = law;
            for (const auto block : { 64, 256, 512 })
            {
                const auto r = rampVsSteady (slot, 48000.0, block, 3.0);
                std::printf ("  Nebula synced, law %.0f, mod depth %.1f, block %d: ramp Δ⁴ energy %+.1f dB, peak %.1f dBFS (steady %.1f)\n", law, mod,
                             block, r.excessDb, r.rampPeakDb, r.steadyPeakDb);
                CHECK (r.ran && r.finite);
                CHECK (r.excessDb < 30.0);     // 0.1.28: +41 … +70 dB; now −2 … +25 (the cloud's moving all-pass readers, unmodulated)
                CHECK (r.rampPeakDb < -60.0);  // 0.1.28: −31 … −47 dBFS; now −67 … −110
            }
        }
}
